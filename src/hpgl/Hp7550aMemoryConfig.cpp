#include "hpgl/Hp7550aMemoryConfig.h"

#include "hpgl/Constants.h"

#include <cassert>
#include <string>

namespace hpgl {

std::pair<std::string, std::string> hp7550aMemoryAllocCmd(
    int io, int polygon, int charBuf, int replot, int vector) {

    assert(io      >= 2  && io      <= 12752);
    assert(polygon >= 4  && polygon <= 12754);
    assert(charBuf >= 0  && charBuf <= 12750);
    assert(replot  >= 0  && replot  <= 12750);
    assert(vector  >= 44 && vector  <= 12794);
    assert(io + polygon + charBuf + replot + vector <= 12800);

    std::string buffers =
        std::string(1, ESC) + ".T" +
        std::to_string(io)      + ";" +
        std::to_string(polygon) + ";" +
        std::to_string(charBuf) + ";" +
        std::to_string(replot)  + ";" +
        std::to_string(vector)  + ESC_TERM;

    std::string logical =
        std::string(1, ESC) + ".@" +
        std::to_string(io) + ESC_TERM;

    return { buffers, logical };
}

}
