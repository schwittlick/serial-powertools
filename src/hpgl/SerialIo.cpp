#include "hpgl/SerialIo.h"

#include <libserialport.h>

#include <algorithm>

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

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        char byte = 0;
        auto remaining = timeout - (std::chrono::steady_clock::now() - start);
        auto slice = std::min<std::chrono::milliseconds>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
            std::chrono::milliseconds(50));
        unsigned ms = slice.count() < 0 ? 0u : static_cast<unsigned>(slice.count());
        int n = sp_blocking_read(port, &byte, 1, ms);
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
