#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hpgl {

// LB-aware HPGL tokenizer. Mirrors reference/hpgl/hpgl_tokenize.py:
//   1. Split input on LB_TERMINATOR (\x03), drop empty batches.
//   2. For each batch: if it contains "LB", split off the pre-LB part on ';'
//      and keep the LB token whole; else split the batch on ';'.
//   3. Drop empty tokens.
std::vector<std::string> tokenize(std::string_view input);

// Join commands for transmission. Mirrors seriallib.py::concat_commands:
//   - Commands starting with "LB" are terminated with LB_TERMINATOR (\x03).
//   - All other commands are terminated with ';'.
std::string concatCommands(const std::vector<std::string>& commands);

// Same, but operating on a [begin, end) slice — avoids copying for batched sends.
std::string concatCommands(const std::vector<std::string>& commands,
                           std::size_t begin, std::size_t end);

}
