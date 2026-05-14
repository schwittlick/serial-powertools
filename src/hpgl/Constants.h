#pragma once

#include <string_view>

namespace hpgl {

constexpr char ESC            = '\x1B';
constexpr char CR             = '\x0D';
constexpr char LF             = '\x0A';
constexpr char LB_TERMINATOR  = '\x03';
constexpr char ESC_TERM       = ':';

constexpr std::string_view OUTPUT_BUFFER_SPACE     = "\x1B.B";
constexpr std::string_view OUTPUT_EXTENDED_STATUS  = "\x1B.O";
constexpr std::string_view OUTPUT_IDENTIFICATION   = "\x1B.A";
constexpr std::string_view MODEL_IDENTIFICATION    = "OI;";
constexpr std::string_view ABORT_GRAPHICS          = "\x1B.K";
constexpr std::string_view RESET_DEVICE            = "\x1B.R";
constexpr std::string_view WAIT                    = "\x1B.L";
constexpr std::string_view OUTPUT_DIMENSIONS       = "OH;";
constexpr std::string_view OUTPUT_POSITION         = "OA;";

}
