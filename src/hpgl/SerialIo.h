#pragma once

#include "hpgl/Constants.h"

#include <chrono>
#include <string>
#include <string_view>

struct sp_port;

namespace hpgl {

// Read bytes from `port` until `terminator` is seen or `timeout` elapses.
// Mirrors reference/hpgl/__init__.py::read_until_char. The terminator byte is
// consumed but not returned. If `okOut` is non-null it is set to false when
// libserialport reports a hard error (port disconnect) — the caller can then
// recover by closing/reopening the port.
std::string readUntilChar(sp_port* port,
                          char terminator = hpgl::CR,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
                          bool* okOut = nullptr);

// Convenience: write a string view to `port` in one libserialport call.
// Returns the number of bytes actually written (or -1 on hard error).
int writeAll(sp_port* port, std::string_view data);

}
