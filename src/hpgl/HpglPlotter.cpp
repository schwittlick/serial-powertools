#include "hpgl/HpglPlotter.h"

#include "hpgl/Constants.h"
#include "hpgl/Hp7550aMemoryConfig.h"
#include "hpgl/SerialIo.h"

#include <libserialport.h>

#include <cctype>
#include <chrono>
#include <thread>

namespace hpgl {

HpglPlotter::HpglPlotter(sp_port* port) : port_(port) {}

void HpglPlotter::write(std::string_view data) {
    writeAll(port_, data);
}

std::string HpglPlotter::readUntil(char terminator,
                                   std::chrono::milliseconds timeout) {
    bool ok = true;
    std::string out = readUntilChar(port_, terminator, timeout, &ok);
    if (!ok) {
        sp_close(port_);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sp_open(port_, SP_MODE_READ_WRITE);
        return "";
    }
    return out;
}

std::string HpglPlotter::identify() {
    write(OUTPUT_IDENTIFICATION);
    std::string answer = readUntil();
    auto comma = answer.find(',');
    if (comma != std::string::npos) answer.resize(comma);
    return answer;
}

int HpglPlotter::freeMemory() {
    write(OUTPUT_BUFFER_SPACE);
    std::string reply = readUntil();
    if (reply.empty()) return 0;
    // Trim trailing whitespace/CR-LF artefacts.
    while (!reply.empty() && std::isspace(static_cast<unsigned char>(reply.back())))
        reply.pop_back();
    // Strict integer parse — leading whitespace tolerated, no trailing junk.
    std::size_t start = 0;
    while (start < reply.size() && std::isspace(static_cast<unsigned char>(reply[start])))
        ++start;
    if (start == reply.size()) return 0;
    int sign = 1;
    if (reply[start] == '-') { sign = -1; ++start; }
    else if (reply[start] == '+') { ++start; }
    if (start == reply.size()) return 0;
    long long acc = 0;
    for (std::size_t i = start; i < reply.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(reply[i]))) return 0;
        acc = acc * 10 + (reply[i] - '0');
        if (acc > 2'000'000'000LL) return 0;
    }
    return static_cast<int>(sign * acc);
}

std::string HpglPlotter::sendWait() {
    write(WAIT);
    return readUntil();
}

void HpglPlotter::abort() {
    write(ABORT_GRAPHICS);
    write(WAIT);
    readUntil();
}

void HpglPlotter::applyConfig(std::string_view memoryConfig,
                              std::string_view plotterConfig) {
    write(memoryConfig);
    write(WAIT);
    readUntil();
    write(plotterConfig);
    write(WAIT);
    readUntil();
}

std::string HpglPlotter::applyModelInit() {
    std::string model = identify();
    if (model == "7550A") {
        auto [mem, plotter] = hp7550aMemoryAllocCmd(12752, 4, 0, 0, 44);
        applyConfig(mem, plotter);
    }
    return model;
}

}
