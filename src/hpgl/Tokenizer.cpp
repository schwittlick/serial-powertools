#include "hpgl/Tokenizer.h"

#include "hpgl/Constants.h"

namespace hpgl {

namespace {

void splitOnSemicolon(std::string_view part, std::vector<std::string>& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < part.size(); ++i) {
        if (part[i] == ';') {
            if (i > start) out.emplace_back(part.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < part.size()) out.emplace_back(part.substr(start));
}

}

std::vector<std::string> tokenize(std::string_view input) {
    std::vector<std::string> commands;
    std::size_t start = 0;
    while (start <= input.size()) {
        std::size_t end = input.find(LB_TERMINATOR, start);
        std::string_view batch = (end == std::string_view::npos)
            ? input.substr(start)
            : input.substr(start, end - start);

        if (!batch.empty()) {
            std::size_t lb = batch.find("LB");
            if (lb != std::string_view::npos && lb > 0) {
                std::string_view pre = batch.substr(0, lb);
                std::string_view lbPart = batch.substr(lb);
                splitOnSemicolon(pre, commands);
                commands.emplace_back(lbPart);
            } else if (lb == 0) {
                // Whole batch is the label.
                commands.emplace_back(batch);
            } else {
                splitOnSemicolon(batch, commands);
            }
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return commands;
}

std::string concatCommands(const std::vector<std::string>& commands,
                           std::size_t begin, std::size_t end) {
    std::string out;
    if (end > commands.size()) end = commands.size();
    for (std::size_t i = begin; i < end; ++i) {
        const std::string& c = commands[i];
        out += c;
        if (c.size() >= 2 && c[0] == 'L' && c[1] == 'B') {
            out += LB_TERMINATOR;
        } else {
            out += ';';
        }
    }
    return out;
}

std::string concatCommands(const std::vector<std::string>& commands) {
    return concatCommands(commands, 0, commands.size());
}

}
