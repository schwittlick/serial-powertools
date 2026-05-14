#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace serialpowertools {

// TCP newline-JSON progress reporter. Byte-for-byte wire-compatible with
// tools/progress/progress_reporter.py — the protocol is documented in
// PLAN.md "ProgressReporter (TCP)".
//
// Resolution order for server location:
//   1. host / port ctor args (host non-empty / port != 0).
//   2. PROGRESS_HOST / PROGRESS_PORT env vars.
//   3. defaults: localhost / 9876.
class ProgressReporter {
public:
    ProgressReporter(std::string label,
                     std::string jobId = "",
                     std::string host = "",
                     std::uint16_t port = 0,
                     double minIntervalSec = 1.0);
    ~ProgressReporter();

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;

    // Launch worker thread + send the initial 0.0 report. Idempotent.
    void start();
    void report(double progress);     // thread-safe, non-blocking
    void finish();                    // sets 1.0, done=true, joins worker

    // Visible-for-test: build the JSON line that would be sent right now.
    std::string buildMessage();

    // Visible-for-test: generate an 8-char hex job id.
    static std::string makeJobId();

    // Visible-for-test: pick a target host/port using the resolution order.
    static void resolveTarget(const std::string& hostArg, std::uint16_t portArg,
                              std::string& hostOut, std::uint16_t& portOut);

private:
    void run();
    bool sendLine(const std::string& line);

    std::string label_;
    std::string jobId_;
    std::string host_;
    std::uint16_t port_;
    double minIntervalSec_;

    std::mutex mu_;
    std::condition_variable cv_;
    double progress_ = 0.0;
    bool   done_     = false;
    bool   dirty_    = false;
    bool   stop_     = false;
    bool   started_  = false;
    std::thread worker_;
};

}
