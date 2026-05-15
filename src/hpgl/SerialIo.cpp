#include "hpgl/SerialIo.h"

#include <libserialport.h>

namespace hpgl {

std::string readUntilChar(sp_port* port, char terminator,
                          std::chrono::milliseconds timeout,
                          bool* okOut) {
    if (okOut) *okOut = true;
    std::string data;
    if (!port) {
        if (okOut) *okOut = false;
        return data;
    }

    // Mirror reference read_until_char (reference/hpgl/__init__.py): the loop
    // budget is `timeout`, but each single-byte read blocks up to the full
    // `timeout` — pyserial's per-read port timeout. The loop condition is
    // checked *before* each read, so the last read may overshoot the budget,
    // giving an effective worst case of ~2x `timeout`, exactly as in Python.
    const unsigned readMs =
        timeout.count() > 0 ? static_cast<unsigned>(timeout.count()) : 0u;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        char byte = 0;
        int n = sp_blocking_read(port, &byte, 1, readMs);
        if (n < 0) {
            if (okOut) *okOut = false;
            return data;
        }
        if (n == 0) continue;
        if (byte == terminator) return data;
        data.push_back(byte);
    }
    return data;
}

int writeAll(sp_port* port, std::string_view data) {
    if (!port || data.empty()) return 0;
    return sp_blocking_write(port, data.data(), data.size(), /*timeout_ms=*/1000);
}

}
