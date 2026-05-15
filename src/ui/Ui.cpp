#include "ui/Ui.h"

#include "app/AppState.h"
#include "app/EtaEstimator.h"
#include "discovery/PortDiscovery.h"
#include "hpgl/Constants.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace serialpowertools::ui {

namespace {

// ─── Per-UI persistent state ──────────────────────────────────────────────
// These live in the UI module — not in AppState — because they're pure widget
// scratch buffers. AppState only holds things worker threads care about.

struct UiState {
    std::vector<std::string> discoveredPorts;
    int  portIndex = -1;
    int  baudIndex = 1; // matches Qt default of "9600"
    char cmdBuf[256]    = {};
    char insertBuf[256] = {};
    int  batch          = 5;
    int  memoryLimit    = 64;
    float startPercent  = 0.0f;
    std::string selectedFile;
    bool autoScroll     = true;
};

UiState& state() {
    static UiState s;
    return s;
}

const std::array<const char*, 2> kBaudOptions = {"1200", "9600"};

std::string shellEscapeSingleQuoted(const std::string& s) {
    std::string out;
    for (char c : s) out += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return out;
}

std::string openFileDialog(const std::string& startDir) {
    std::string dir = startDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    std::string cmd = "kdialog --getopenfilename '";
    cmd += shellEscapeSingleQuoted(dir.empty() ? "." : dir);
    cmd += "' '*.hpgl *.plt *.hgl' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    std::array<char, 4096> buf{};
    std::string path;
    if (std::fgets(buf.data(), buf.size(), f)) {
        path = buf.data();
        if (!path.empty() && path.back() == '\n') path.pop_back();
    }
    pclose(f);
    return path;
}

std::string randomPa() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> d(0, 10000);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "PA%d,%d;", d(rng), d(rng));
    return buf;
}

void presetButton(AppState& app, const char* label, std::string command, float widthFrac) {
    float w = ImGui::GetContentRegionAvail().x * widthFrac;
    if (ImGui::Button(label, ImVec2(w, 0))) {
        app.sendCommand(command);
    }
}

void presetGrid(AppState& app, const std::vector<std::pair<const char*, std::string>>& items,
                int perRow) {
    for (std::size_t i = 0; i < items.size(); i += static_cast<std::size_t>(perRow)) {
        for (int j = 0; j < perRow; ++j) {
            std::size_t k = i + static_cast<std::size_t>(j);
            if (k >= items.size()) break;
            if (j > 0) ImGui::SameLine();
            float frac = 1.0f / static_cast<float>(perRow - j);
            ImGui::PushID(static_cast<int>(k));
            presetButton(app, items[k].first, items[k].second, frac);
            ImGui::PopID();
        }
    }
}

}

void applyDefaultLayout(unsigned dockspaceId) {
    ImGuiID id = static_cast<ImGuiID>(dockspaceId);
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->Size);

    ImGuiID left, right;
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.30f, &left, &right);
    ImGui::DockBuilderDockWindow("Controls", left);
    ImGui::DockBuilderDockWindow("Output", right);
    ImGui::DockBuilderFinish(id);
}

void drawControls(AppState& app) {
    UiState& s = state();

    // Drain discovery results.
    std::vector<DiscoveredPort> fresh;
    if (app.takeDiscoveryResults(fresh)) {
        s.discoveredPorts.clear();
        for (auto& p : fresh) s.discoveredPorts.push_back(p.device + " -> " + p.model);
        if (s.portIndex >= static_cast<int>(s.discoveredPorts.size())) s.portIndex = -1;
        if (s.portIndex == -1 && !s.discoveredPorts.empty()) s.portIndex = 0;
    }

    if (!ImGui::Begin("Controls")) { ImGui::End(); return; }

    // ─── Port / Baud ─────────────────────────────────────────────────────
    const char* portPreview = (s.portIndex >= 0 && s.portIndex < static_cast<int>(s.discoveredPorts.size()))
        ? s.discoveredPorts[s.portIndex].c_str()
        : "<none>";
    if (ImGui::BeginCombo("Port", portPreview)) {
        for (int i = 0; i < static_cast<int>(s.discoveredPorts.size()); ++i) {
            bool sel = (i == s.portIndex);
            if (ImGui::Selectable(s.discoveredPorts[i].c_str(), sel)) s.portIndex = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Baud", kBaudOptions[s.baudIndex])) {
        for (int i = 0; i < static_cast<int>(kBaudOptions.size()); ++i) {
            bool sel = (i == s.baudIndex);
            if (ImGui::Selectable(kBaudOptions[i], sel)) s.baudIndex = i;
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Refresh")) app.startRefreshPorts();
    ImGui::SameLine();
    const char* connectLabel = app.connected() ? "Disconnect" : "Connect";
    if (ImGui::Button(connectLabel)) {
        if (app.connected()) {
            app.disconnect();
        } else if (s.portIndex >= 0 && s.portIndex < static_cast<int>(s.discoveredPorts.size())) {
            const std::string& entry = s.discoveredPorts[s.portIndex];
            auto sp = entry.find(' ');
            std::string dev = (sp == std::string::npos) ? entry : entry.substr(0, sp);
            int baud = std::atoi(kBaudOptions[s.baudIndex]);
            app.connect(dev, baud);
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(app.connected() ? "Status: Connected" : "Status: Disconnected");
    if (app.discoveryInFlight()) ImGui::TextUnformatted("Discovering ports...");

    ImGui::Separator();

    // ─── Manual command ──────────────────────────────────────────────────
    bool send = ImGui::InputText("##cmd", s.cmdBuf, sizeof(s.cmdBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Send CMD") || send) {
        if (s.cmdBuf[0]) {
            app.sendCommand(s.cmdBuf);
            s.cmdBuf[0] = '\0';
        }
    }

    // ─── Preset grid ─────────────────────────────────────────────────────
    std::vector<std::pair<const char*, std::string>> presets = {
        {"IN;", "IN;"},
        {"OA;", "OA;"},
        {"OE;", "OE;"},
        {"OH;", "OH;"},
        {"OI;", "OI;"},
        {"PU;", "PU;"},
        {"PD;", "PD;"},
        {"PA0,0;", "PA0,0;"},
        {"PA10000,10000;", "PA10000,10000;"},
        {"PArandom();", randomPa()},
        {"ESC.R (reset)", std::string(hpgl::RESET_DEVICE) + ";"},
        {"ESC.K (abort)", std::string(hpgl::ABORT_GRAPHICS) + ";"},
    };
    presetGrid(app, presets, 2);

    static const std::array<std::pair<const char*, const char*>, 9> kPens = {{
        {"SP0;","SP0;"}, {"SP1;","SP1;"}, {"SP2;","SP2;"},
        {"SP3;","SP3;"}, {"SP4;","SP4;"}, {"SP5;","SP5;"},
        {"SP6;","SP6;"}, {"SP7;","SP7;"}, {"SP8;","SP8;"},
    }};
    std::vector<std::pair<const char*, std::string>> pens;
    pens.reserve(kPens.size());
    for (auto& p : kPens) pens.emplace_back(p.first, p.second);
    presetGrid(app, pens, 3);

    std::vector<std::pair<const char*, std::string>> vs = {
        {"VS1;",   "VS1;"},
        {"VS10;",  "VS10;"},
        {"VS20;",  "VS20;"},
        {"VS40;",  "VS40;"},
        {"VS80;",  "VS80;"},
        {"VS100;", "VS100;"},
    };
    presetGrid(app, vs, 3);

    ImGui::Separator();

    // ─── File send ───────────────────────────────────────────────────────
    ImGui::TextUnformatted(s.selectedFile.empty()
        ? "No file selected"
        : std::filesystem::path(s.selectedFile).filename().string().c_str());

    if (ImGui::Button("Select File")) {
        std::string dir = s.selectedFile.empty()
            ? std::string()
            : std::filesystem::path(s.selectedFile).parent_path().string();
        std::string p = openFileDialog(dir);
        if (!p.empty()) s.selectedFile = p;
    }
    ImGui::SameLine();
    if (ImGui::Button("Send")) {
        if (!s.selectedFile.empty()) app.sendFile(s.selectedFile, s.startPercent);
    }
    ImGui::SameLine();
    if (ImGui::Button(app.paused() ? "Resume" : "Pause")) app.togglePause();
    ImGui::SameLine();
    if (ImGui::Button("Abort")) app.abortFile();

    if (ImGui::SliderInt("Batch", &s.batch, 1, 40)) app.setBatchSize(s.batch);
    ImGui::InputFloat("Start %", &s.startPercent, 0.0f, 0.0f, "%.0f");
    if (s.startPercent < 0)   s.startPercent = 0;
    if (s.startPercent > 100) s.startPercent = 100;
    if (ImGui::SliderInt("Memory", &s.memoryLimit, 32, 1024)) app.setMemoryLimit(s.memoryLimit);

    float frac = static_cast<float>(app.progressFraction());
    ImGui::ProgressBar(frac, ImVec2(-1, 0));
    char etaBuf[128];
    std::snprintf(etaBuf, sizeof(etaBuf), "Elapsed: %s   Remaining: %s",
                  EtaEstimator::formatSeconds(app.elapsedSeconds()).c_str(),
                  EtaEstimator::formatSeconds(app.etaSeconds()).c_str());
    ImGui::TextUnformatted(etaBuf);

    ImGui::Separator();

    // ─── Insert command ──────────────────────────────────────────────────
    bool insert = ImGui::InputText("##insert", s.insertBuf, sizeof(s.insertBuf),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Insert Command") || insert) {
        if (s.insertBuf[0]) {
            app.insertCommand(s.insertBuf);
            s.insertBuf[0] = '\0';
        }
    }

    ImGui::End();
}

void drawOutput(AppState& app) {
    if (!ImGui::Begin("Output")) { ImGui::End(); return; }

    UiState& s = state();
    ImGui::Checkbox("Auto-scroll", &s.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear output")) app.clearLog();
    ImGui::SameLine();
    bool copyAll = ImGui::Button("Copy");

    auto log = app.snapshotLog();
    if (copyAll) {
        std::string joined;
        for (const auto& line : log) { joined += line; joined += '\n'; }
        ImGui::SetClipboardText(joined.c_str());
    }

    ImGui::BeginChild("##scroll", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    // Plain text lines keep auto-scroll working; the "Copy" button above grabs
    // the whole buffer (ImGui TextUnformatted has no mouse text selection).
    for (const auto& line : log) ImGui::TextUnformatted(line.c_str());
    if (s.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

}
