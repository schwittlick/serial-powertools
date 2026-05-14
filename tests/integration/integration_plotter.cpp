// Integration driver for slice 2: HpglPlotter + AsyncSerialSender ↔ fake plotter
// over a socat-created pty pair.
//
// Usage: integration_plotter <tty>
//   <tty>  serial device (one end of a socat pty pair).
//
// Exit codes:
//   0  success
//   1  test assertion failed
//   2  setup error (port / arg)

#include "hpgl/HpglPlotter.h"
#include "hpgl/Tokenizer.h"
#include "sender/AsyncSerialSender.h"

#include <libserialport.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kWaitTimeoutMs = 10'000;

bool openPort(const char* tty, sp_port** out) {
    if (sp_get_port_by_name(tty, out) != SP_OK) {
        std::fprintf(stderr, "sp_get_port_by_name failed: %s\n", tty);
        return false;
    }
    if (sp_open(*out, SP_MODE_READ_WRITE) != SP_OK) {
        std::fprintf(stderr, "sp_open failed for %s\n", tty);
        return false;
    }
    sp_set_baudrate(*out, 9600);
    sp_set_bits(*out, 8);
    sp_set_parity(*out, SP_PARITY_NONE);
    sp_set_stopbits(*out, 1);
    sp_set_flowcontrol(*out, SP_FLOWCONTROL_NONE);
    return true;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <tty>\n", argv[0]);
        return 2;
    }
    const char* tty = argv[1];

    sp_port* port = nullptr;
    if (!openPort(tty, &port)) return 2;

    hpgl::HpglPlotter plotter(port);

    // identify() — triggers ESC.A; if the fake says "7550A,..." the plotter
    // will follow up with ESC.T... and ESC.@... memory init.
    std::string model = plotter.applyModelInit();
    std::printf("integration_plotter: identified as '%s'\n", model.c_str());
    if (model.empty()) {
        std::fprintf(stderr, "FAIL: empty model from identify()\n");
        return 1;
    }

    // freeMemory() — triggers ESC.B; fake returns 1024.
    int freeMem = plotter.freeMemory();
    std::printf("integration_plotter: freeMemory = %d\n", freeMem);
    if (freeMem <= 0) {
        std::fprintf(stderr, "FAIL: freeMemory returned %d (expected > 0)\n", freeMem);
        return 1;
    }

    // sendWait() — ESC.L round trip.
    std::string waitReply = plotter.sendWait();
    std::printf("integration_plotter: sendWait reply = '%s'\n", waitReply.c_str());

    // Drive AsyncSerialSender through a mix of normal + LB commands.
    serialpowertools::AsyncSerialSender sender(&plotter);
    sender.setBatchSize(2);
    sender.setSoftwareHandshake(true);
    sender.start();

    std::atomic<std::size_t> lastIdx{0};
    std::atomic<std::size_t> lastTot{0};
    auto cb = [&](std::size_t i, std::size_t n) {
        lastIdx.store(i, std::memory_order_release);
        lastTot.store(n, std::memory_order_release);
    };

    std::string input = "IN;PU;PA0,0;PD;PA1000,1000;LBhello\x03PA0,0;PU;";
    auto tokens = hpgl::tokenize(input);
    std::size_t expected = tokens.size();
    std::printf("integration_plotter: sending %zu tokens\n", expected);
    sender.addCommands(std::move(tokens), cb);

    // Poll until idx == expected or timeout.
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <
           std::chrono::milliseconds(kWaitTimeoutMs)) {
        if (lastIdx.load(std::memory_order_acquire) >= expected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::size_t finalIdx = lastIdx.load();
    std::size_t finalTot = lastTot.load();
    std::printf("integration_plotter: final idx=%zu/%zu\n", finalIdx, finalTot);

    sender.stop();
    sp_close(port);
    sp_free_port(port);

    if (finalIdx != expected) {
        std::fprintf(stderr,
                     "FAIL: idx %zu != expected %zu (sender did not drain)\n",
                     finalIdx, expected);
        return 1;
    }

    std::printf("integration_plotter: OK\n");
    return 0;
}
