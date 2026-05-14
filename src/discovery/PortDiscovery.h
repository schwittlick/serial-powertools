#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace serialpowertools {

struct DiscoveredPort {
    std::string device;  // e.g. "/dev/ttyUSB0"
    std::string model;   // e.g. "7550A"
};

class PortDiscovery {
public:
    // Enumerate serial ports via libserialport, skip ports currently held by
    // other processes (via `lsof`), and probe the remaining ones in parallel
    // with `OI;`. Mirrors reference/tools/discovery.py::discover().
    //
    // Default baud / timing matches the Python discover() call from main_qt5.py.
    static std::vector<DiscoveredPort> discover(
        int baudrate = 9600,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

    // Visible-for-test helper: probe a single port. Returns std::nullopt-like
    // empty model on failure (empty `model` string).
    static DiscoveredPort probePort(const std::string& device,
                                    int baudrate,
                                    std::chrono::milliseconds timeout);

    // Visible-for-test helper: check whether a device path is held by another
    // process. Uses `lsof <device>` — same as the Python version.
    static bool portInUse(const std::string& device);
};

}
