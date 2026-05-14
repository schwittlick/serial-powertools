// Integration driver for slice 1: ProgressReporter ↔ progress_server.py.
//
// Resolves the server from PROGRESS_HOST / PROGRESS_PORT (set by
// docker-compose). Sends a fixed sequence of reports + finish(). The
// orchestrator (run_inside.sh) then queries the server and asserts.

#include "reporter/ProgressReporter.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    std::string label = (argc >= 2) ? argv[1] : "integration-job";
    std::string jobId = (argc >= 3) ? argv[2] : "intgtest";

    if (jobId.size() != 8) {
        std::fprintf(stderr, "job id must be exactly 8 chars (got '%s')\n", jobId.c_str());
        return 2;
    }

    // min_interval shrunk to 0.1 s so the test runs quickly.
    serialpowertools::ProgressReporter r(label, jobId, "", 0, 0.1);
    r.start();

    for (int i = 1; i <= 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        r.report(0.2 * i);
    }
    r.finish();

    std::printf("integration_reporter: job=%s label=%s sent 5 reports + finish\n",
                jobId.c_str(), label.c_str());
    return 0;
}
