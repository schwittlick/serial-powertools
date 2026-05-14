#include "reporter/ProgressReporter.h"

#include "test_harness.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using serialpowertools::ProgressReporter;

// 8-char hex job id, regenerated each call.
static void test_makeJobId() {
    std::string a = ProgressReporter::makeJobId();
    std::string b = ProgressReporter::makeJobId();
    EXPECT_EQ(a.size(), static_cast<std::size_t>(8));
    EXPECT_EQ(b.size(), static_cast<std::size_t>(8));
    for (char c : a) {
        EXPECT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    // Very unlikely to collide; not strictly guaranteed, but practically safe.
    EXPECT(a != b);
}

static void test_resolveTarget() {
    // Explicit args win.
    {
        std::string h; std::uint16_t p = 0;
        ProgressReporter::resolveTarget("myhost", 4321, h, p);
        EXPECT_EQ(h, std::string("myhost"));
        EXPECT_EQ(p, static_cast<std::uint16_t>(4321));
    }

    // Env when no arg.
    {
        setenv("PROGRESS_HOST", "envhost", 1);
        setenv("PROGRESS_PORT", "12345", 1);
        std::string h; std::uint16_t p = 0;
        ProgressReporter::resolveTarget("", 0, h, p);
        EXPECT_EQ(h, std::string("envhost"));
        EXPECT_EQ(p, static_cast<std::uint16_t>(12345));
        unsetenv("PROGRESS_HOST");
        unsetenv("PROGRESS_PORT");
    }

    // Defaults.
    {
        unsetenv("PROGRESS_HOST");
        unsetenv("PROGRESS_PORT");
        std::string h; std::uint16_t p = 0;
        ProgressReporter::resolveTarget("", 0, h, p);
        EXPECT_EQ(h, std::string("localhost"));
        EXPECT_EQ(p, static_cast<std::uint16_t>(9876));
    }
}

static void test_buildMessage() {
    ProgressReporter r("my-label", "abcd1234", "localhost", 1, 1.0);
    std::string msg = r.buildMessage();
    // Required pieces.
    EXPECT(msg.find("\"type\": \"report\"") != std::string::npos);
    EXPECT(msg.find("\"id\": \"abcd1234\"") != std::string::npos);
    EXPECT(msg.find("\"label\": \"my-label\"") != std::string::npos);
    EXPECT(msg.find("\"progress\": 0") != std::string::npos);
    EXPECT(msg.find("\"done\": false") != std::string::npos);
    EXPECT(msg.back() == '\n');
}

// Stand-up a TCP listener on an ephemeral port, run a reporter against it,
// capture the lines, sanity-check the protocol.
static void test_endToEnd() {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT(sfd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    EXPECT_EQ(bind(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    socklen_t alen = sizeof(addr);
    EXPECT_EQ(getsockname(sfd, reinterpret_cast<sockaddr*>(&addr), &alen), 0);
    EXPECT_EQ(listen(sfd, 16), 0);
    std::uint16_t port = ntohs(addr.sin_port);

    std::vector<std::string> received;
    std::atomic<bool> stopAcceptor{false};

    std::thread acceptor([&] {
        while (!stopAcceptor.load(std::memory_order_acquire)) {
            timeval tv{0, 200'000};
            fd_set fds; FD_ZERO(&fds); FD_SET(sfd, &fds);
            int s = select(sfd + 1, &fds, nullptr, nullptr, &tv);
            if (s <= 0) continue;
            int c = accept(sfd, nullptr, nullptr);
            if (c < 0) continue;
            std::string line;
            char buf[256];
            while (true) {
                ssize_t n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                line.append(buf, buf + n);
            }
            close(c);
            received.push_back(line);
        }
    });

    {
        // min_interval 0.05 s — keep test snappy.
        ProgressReporter r("e2e", "deadbeef", "127.0.0.1", port, 0.05);
        r.start();
        for (int i = 1; i <= 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            r.report(0.2 * i);
        }
        r.finish();
    }

    // Give the acceptor a moment to drain the final connection.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stopAcceptor.store(true, std::memory_order_release);
    acceptor.join();
    close(sfd);

    EXPECT(!received.empty());

    // Every received line should be JSON-ish and end with \n.
    bool sawDone = false;
    for (const auto& line : received) {
        EXPECT(!line.empty());
        EXPECT_EQ(line.back(), '\n');
        EXPECT(line.find("\"type\": \"report\"") != std::string::npos);
        EXPECT(line.find("\"id\": \"deadbeef\"") != std::string::npos);
        EXPECT(line.find("\"label\": \"e2e\"") != std::string::npos);
        if (line.find("\"done\": true") != std::string::npos) sawDone = true;
    }
    EXPECT(sawDone);
}

int main() {
    test_makeJobId();
    test_resolveTarget();
    test_buildMessage();
    test_endToEnd();
    std::printf("test_progress_reporter OK\n");
    return 0;
}
