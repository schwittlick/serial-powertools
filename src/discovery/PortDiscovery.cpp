#include "discovery/PortDiscovery.h"

#include "hpgl/Constants.h"
#include "hpgl/SerialIo.h"

#include <libserialport.h>

#include <array>
#include <cstdio>
#include <thread>

namespace serialpowertools {

namespace {

struct PortGuard {
    sp_port* p = nullptr;
    ~PortGuard() { if (p) { sp_close(p); sp_free_port(p); } }
};

}

bool PortDiscovery::portInUse(const std::string& device) {
    // `lsof <device>` writes to stdout if any process holds it. Match the
    // Python behaviour: any non-empty output = in use.
    std::string cmd = "lsof " + device + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return false;
    std::array<char, 256> buf{};
    bool any = false;
    while (std::fgets(buf.data(), buf.size(), f)) {
        if (buf[0] != '\0' && buf[0] != '\n') { any = true; break; }
    }
    pclose(f);
    return any;
}

DiscoveredPort PortDiscovery::probePort(const std::string& device, int baudrate,
                                        std::chrono::milliseconds timeout) {
    DiscoveredPort out;
    out.device = device;

    PortGuard g;
    if (sp_get_port_by_name(device.c_str(), &g.p) != SP_OK) return out;
    if (sp_open(g.p, SP_MODE_READ_WRITE) != SP_OK) return out;
    sp_set_baudrate(g.p, baudrate);
    sp_set_bits(g.p, 8);
    sp_set_parity(g.p, SP_PARITY_NONE);
    sp_set_stopbits(g.p, 1);
    sp_set_flowcontrol(g.p, SP_FLOWCONTROL_NONE);

    // OI; probe — works for HP7470A / Roland DXY which don't respond to ESC.A.
    hpgl::writeAll(g.p, hpgl::MODEL_IDENTIFICATION);
    std::string model = hpgl::readUntilChar(g.p, hpgl::CR, timeout);

    // Trim whitespace.
    while (!model.empty() && (model.back() == ' ' || model.back() == '\r' ||
                              model.back() == '\n' || model.back() == '\t'))
        model.pop_back();
    if (model.empty()) return out;

    out.model = model;
    return out;
}

std::vector<DiscoveredPort> PortDiscovery::discover(int baudrate,
                                                    std::chrono::milliseconds timeout) {
    sp_port** portList = nullptr;
    if (sp_list_ports(&portList) != SP_OK || !portList) return {};

    std::vector<std::string> candidates;
    for (int i = 0; portList[i]; ++i) {
        const char* name = sp_get_port_name(portList[i]);
        if (name) candidates.emplace_back(name);
    }
    sp_free_port_list(portList);

    // Filter out ports held by other processes.
    std::vector<std::string> available;
    for (const auto& dev : candidates) {
        if (!portInUse(dev)) available.push_back(dev);
    }

    // Probe in parallel — one thread per candidate.
    std::vector<DiscoveredPort> results(available.size());
    std::vector<std::thread> workers;
    workers.reserve(available.size());
    for (std::size_t i = 0; i < available.size(); ++i) {
        workers.emplace_back([&, i] {
            results[i] = probePort(available[i], baudrate, timeout);
        });
    }
    for (auto& t : workers) t.join();

    // Drop entries with empty model.
    std::vector<DiscoveredPort> out;
    out.reserve(results.size());
    for (auto& r : results) {
        if (!r.model.empty()) out.push_back(std::move(r));
    }
    return out;
}

}
