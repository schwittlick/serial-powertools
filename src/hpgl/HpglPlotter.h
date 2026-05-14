#pragma once

#include "hpgl/Constants.h"

#include <chrono>
#include <string>
#include <string_view>

struct sp_port;

namespace hpgl {

// Thin wrapper around a libserialport port that speaks HPGL. The port is
// owned by the caller; HpglPlotter does not close or free it. Mirrors
// reference/hpgl/plotter/plotter.py::HPGLPlotter.
class HpglPlotter {
public:
    explicit HpglPlotter(sp_port* port);

    // Send `OUTPUT_IDENTIFICATION` (ESC.A) and return the model token before
    // the first comma. e.g. "7550A,..." -> "7550A".
    std::string identify();

    // Returns 0 (matching the Python behaviour) when the reply isn't an int.
    int freeMemory();

    // ESC.L + read-until-CR. Returns the raw reply string.
    std::string sendWait();

    // ESC.K + ESC.L + read-until-CR.
    void abort();

    // Raw write — passes the bytes through with no terminator added.
    void write(std::string_view data);

    // Read until CR (or `timeout`). On a hard serial error, closes-and-reopens
    // the port (0.5 s pause) and returns "" — same recovery as the Python.
    std::string readUntil(char terminator = hpgl::CR,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

    // Idempotent: only sends the 7550A buffer/logical-buffer config when the
    // identify reply matches. Returns the model string.
    std::string applyModelInit();

    // Send memory_config + ESC.L (read), then logical + ESC.L (read).
    void applyConfig(std::string_view memoryConfig, std::string_view plotterConfig);

    sp_port* port() const { return port_; }

private:
    sp_port* port_;
};

}
