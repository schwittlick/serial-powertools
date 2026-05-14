#include "hpgl/Hp7550aMemoryConfig.h"

#include "test_harness.h"

#include <string>

int main() {
    // Default values from the Python reference.
    auto [buffers, logical] = hpgl::hp7550aMemoryAllocCmd();
    EXPECT_EQ(buffers, std::string("\x1B.T1024;1778;0;9954;44:"));
    EXPECT_EQ(logical, std::string("\x1B.@1024:"));

    // The actual call made by HpglPlotter on identify().
    auto [b2, l2] = hpgl::hp7550aMemoryAllocCmd(12752, 4, 0, 0, 44);
    EXPECT_EQ(b2, std::string("\x1B.T12752;4;0;0;44:"));
    EXPECT_EQ(l2, std::string("\x1B.@12752:"));

    std::printf("test_memory_config OK\n");
    return 0;
}
