#pragma once

// Tiny inline test harness so tests have zero deps beyond their target lib.
// One header, one macro, no framework. Mirrors the style of hpgl-viewer's
// tests so the two projects stay consistent.

#include <cstdio>
#include <cstdlib>
#include <string>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: EXPECT(%s) failed\n", \
                         __FILE__, __LINE__, #cond); \
            std::exit(1); \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        auto _av = (a); auto _bv = (b); \
        if (!(_av == _bv)) { \
            std::fprintf(stderr, "%s:%d: EXPECT_EQ failed: %s != %s\n", \
                         __FILE__, __LINE__, #a, #b); \
            std::exit(1); \
        } \
    } while (0)

inline std::string vis(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\x03')      out += "<LB>";
        else if (c == '\x1B') out += "<ESC>";
        else if (c == '\r')   out += "<CR>";
        else if (c == '\n')   out += "<LF>";
        else if (c == '\t')   out += "<TAB>";
        else                  out += c;
    }
    return out;
}
