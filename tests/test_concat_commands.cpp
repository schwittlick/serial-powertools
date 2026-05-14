#include "hpgl/Tokenizer.h"

#include "test_harness.h"

#include <string>
#include <vector>

using hpgl::concatCommands;
using V = std::vector<std::string>;

int main() {
    // Empty.
    EXPECT_EQ(concatCommands({}), std::string());

    // Normal commands are joined with ';' and terminated with ';'.
    EXPECT_EQ(concatCommands({"IN", "PU", "PA0,0"}), std::string("IN;PU;PA0,0;"));

    // LB commands get LB_TERMINATOR (\x03), not ';'.
    {
        std::string got = concatCommands({"LBhello"});
        std::string expected = std::string("LBhello") + '\x03';
        EXPECT_EQ(vis(got), vis(expected));
    }

    // Mixed: normal LB normal.
    {
        std::string got = concatCommands({"SP1", "LBhi", "PA100,100"});
        std::string expected = std::string("SP1;LBhi") + '\x03' + "PA100,100;";
        EXPECT_EQ(vis(got), vis(expected));
    }

    // Slice variant skips the head.
    {
        V cmds = {"A", "B", "C", "D"};
        EXPECT_EQ(concatCommands(cmds, 1, 3), std::string("B;C;"));
        EXPECT_EQ(concatCommands(cmds, 0, 0), std::string());
        // Out-of-range end is clamped.
        EXPECT_EQ(concatCommands(cmds, 2, 100), std::string("C;D;"));
    }

    // Round-trip: tokenize(concat(...)) should give back the same tokens for
    // plain commands.
    {
        V src = {"IN", "PU", "PA0,0", "PD"};
        std::string wire = concatCommands(src);
        EXPECT_EQ(hpgl::tokenize(wire), src);
    }

    // Round-trip with an LB token preserved.
    {
        V src = {"SP1", "LBhello", "PA100,100"};
        std::string wire = concatCommands(src);
        EXPECT_EQ(hpgl::tokenize(wire), src);
    }

    std::printf("test_concat_commands OK\n");
    return 0;
}
