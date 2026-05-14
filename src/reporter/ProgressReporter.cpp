#include "reporter/ProgressReporter.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

namespace serialpowertools {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}

std::string ProgressReporter::makeJobId() {
    static thread_local std::mt19937_64 rng{
        std::random_device{}() ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count())
    };
    std::uint32_t v = static_cast<std::uint32_t>(rng() & 0xFFFFFFFFu);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return std::string(buf);
}

void ProgressReporter::resolveTarget(const std::string& hostArg,
                                     std::uint16_t portArg,
                                     std::string& hostOut,
                                     std::uint16_t& portOut) {
    if (!hostArg.empty()) {
        hostOut = hostArg;
    } else if (const char* env = std::getenv("PROGRESS_HOST")) {
        hostOut = env;
    } else {
        hostOut = "localhost";
    }

    if (portArg != 0) {
        portOut = portArg;
    } else if (const char* env = std::getenv("PROGRESS_PORT")) {
        long p = std::strtol(env, nullptr, 10);
        portOut = (p > 0 && p < 65536) ? static_cast<std::uint16_t>(p) : 9876;
    } else {
        portOut = 9876;
    }
}

ProgressReporter::ProgressReporter(std::string label, std::string jobId,
                                   std::string host, std::uint16_t port,
                                   double minIntervalSec)
    : label_(std::move(label)),
      jobId_(jobId.empty() ? makeJobId() : std::move(jobId)),
      minIntervalSec_(minIntervalSec) {
    resolveTarget(host, port, host_, port_);
}

ProgressReporter::~ProgressReporter() {
    finish();
}

void ProgressReporter::start() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (started_) return;
        started_ = true;
    }
    worker_ = std::thread([this] { run(); });
    report(0.0);
}

void ProgressReporter::report(double p) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (p < 0.0) p = 0.0;
        if (p > 1.0) p = 1.0;
        progress_ = p;
        dirty_ = true;
    }
    cv_.notify_all();
}

void ProgressReporter::finish() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!started_) return;
        progress_ = 1.0;
        done_ = true;
        dirty_ = true;
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        // Python uses join(timeout=5) but std::thread::join has no timeout.
        // The worker checks `stop_` and exits in ≤ minIntervalSec + send timeout (3s),
        // so realistic upper bound ~4s.
        worker_.join();
    }
    std::lock_guard<std::mutex> lk(mu_);
    started_ = false;
}

std::string ProgressReporter::buildMessage() {
    std::lock_guard<std::mutex> lk(mu_);
    // Match Python json.dumps key ordering: type, id, label, progress, done.
    // The server doesn't care about key order, but matching makes tcpdump
    // diffs trivially clean.
    char prog[32];
    std::snprintf(prog, sizeof(prog), "%g", progress_);
    std::string msg;
    msg += "{\"type\": \"report\", \"id\": \"";
    msg += jsonEscape(jobId_);
    msg += "\", \"label\": \"";
    msg += jsonEscape(label_);
    msg += "\", \"progress\": ";
    msg += prog;
    msg += ", \"done\": ";
    msg += done_ ? "true" : "false";
    msg += "}\n";
    return msg;
}

bool ProgressReporter::sendLine(const std::string& line) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portBuf[8];
    std::snprintf(portBuf, sizeof(portBuf), "%u", static_cast<unsigned>(port_));

    addrinfo* res = nullptr;
    if (getaddrinfo(host_.c_str(), portBuf, &hints, &res) != 0 || !res) return false;

    bool ok = false;
    for (addrinfo* ai = res; ai && !ok; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            const char* data = line.data();
            std::size_t remaining = line.size();
            ok = true;
            while (remaining > 0) {
                ssize_t n = send(fd, data, remaining, MSG_NOSIGNAL);
                if (n <= 0) { ok = false; break; }
                data += n;
                remaining -= static_cast<std::size_t>(n);
            }
        }
        close(fd);
    }
    freeaddrinfo(res);
    return ok;
}

void ProgressReporter::run() {
    using clock = std::chrono::steady_clock;
    auto lastSent = clock::now() - std::chrono::seconds(3600);

    while (true) {
        bool shouldStop = false;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::duration<double>(minIntervalSec_),
                         [&] { return dirty_ || stop_; });
            dirty_ = false;
            shouldStop = stop_;
        }

        auto now = clock::now();
        if (std::chrono::duration<double>(now - lastSent).count() >= minIntervalSec_) {
            if (sendLine(buildMessage())) lastSent = now;
        }

        if (shouldStop) break;
    }
    // Final send to guarantee done=true reaches the server.
    sendLine(buildMessage());
}

}
