#include "hpgl/Tokenizer.h"

#include "test_harness.h"

#include <string>
#include <vector>

using hpgl::tokenize;
using V = std::vector<std::string>;

int main() {
    // Empty input.
    EXPECT(tokenize("").empty());

    // Trailing-semicolon only is dropped.
    EXPECT(tokenize(";;;;").empty());

    // Basic split.
    EXPECT_EQ(tokenize("IN;PU;PA0,0;"), V({"IN", "PU", "PA0,0"}));

    // Missing trailing semicolon — the last token still emerges.
    EXPECT_EQ(tokenize("IN;PU"), V({"IN", "PU"}));

    // Label batch: pre-LB part splits on ';', LB part stays whole.
    // "PA0,0;LBhello\x03PU;" -> ["PA0,0", "LBhello", "PU"]
    {
        std::string in = "PA0,0;LBhello\x03PU;";
        EXPECT_EQ(tokenize(in), V({"PA0,0", "LBhello", "PU"}));
    }

    // Label only inside the batch.
    {
        std::string in = "LBjust-a-label\x03";
        EXPECT_EQ(tokenize(in), V({"LBjust-a-label"}));
    }

    // The plotter trick — label containing "LB" inside its own text.
    // "LB1234LB\x03" → one token, preserved whole.
    {
        std::string in = "LB1234LB\x03";
        EXPECT_EQ(tokenize(in), V({"LB1234LB"}));
    }

    // Two batches separated by LB terminator.
    {
        std::string in = "SP1;LBhi\x03PA100,100;";
        EXPECT_EQ(tokenize(in), V({"SP1", "LBhi", "PA100,100"}));
    }

    // Pre-LB part is empty inside a batch — LB at start, no semicolons before it.
    {
        std::string in = "LBabc\x03";
        EXPECT_EQ(tokenize(in), V({"LBabc"}));
    }

    // Empty batches between LB terminators are dropped.
    {
        std::string in = std::string("\x03\x03") + "PA0,0;\x03";
        EXPECT_EQ(tokenize(in), V({"PA0,0"}));
    }

    std::printf("test_tokenizer OK\n");
    return 0;
}
