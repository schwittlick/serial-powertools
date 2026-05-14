#pragma once

#include <string>
#include <utility>

namespace hpgl {

// HP 7550A memory allocation. Mirrors reference/hpgl/plotter/memory_config.py
// (HP7550AMemoryConfig). Returns (buffer_sizes_cmd, logical_buffer_size_cmd)
// — both already include their ESC.T / ESC.@ prefix and ":" terminator.
//
// Asserted limits (per the Python source):
//   io      ∈ [2, 12752]
//   polygon ∈ [4, 12754]
//   char    ∈ [0, 12750]
//   replot  ∈ [0, 12750]
//   vector  ∈ [44, 12794]
//   sum     ≤ 12800
//
// The Python defaults are (1024, 1778, 0, 9954, 44). The C++ caller (HpglPlotter)
// uses (12752, 4, 0, 0, 44) to match the call in plotter.py.
std::pair<std::string, std::string> hp7550aMemoryAllocCmd(
    int io = 1024, int polygon = 1778, int charBuf = 0,
    int replot = 9954, int vector = 44);

}
