#include "app/AppState.h"

#include "discovery/PortDiscovery.h"
#include "hpgl/HpglPlotter.h"
#include "hpgl/Tokenizer.h"
#include "reporter/ProgressReporter.h"
#include "sender/AsyncSerialSender.h"

#include <libserialport.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace serialpowertools {

namespace {

constexpr std::size_t kMaxLogLines = 10'000;

}

AppState::AppState() = default;

AppState::~AppState() {
    disconnect();
    if (discoveryThread_.joinable()) discoveryThread_.join();
}

void AppState::pushLog(const std::string& line) {
    {
        std::lock_guard<std::mutex> lk(logMu_);
        log_.push_back(line);
        if (log_.size() > kMaxLogLines) log_.pop_front();
    }
    wake();
}

std::deque<std::string> AppState::snapshotLog() const {
    std::lock_guard<std::mutex> lk(logMu_);
    return log_;
}

void AppState::clearLog() {
    std::lock_guard<std::mutex> lk(logMu_);
    log_.clear();
}

void AppState::wake() {
    if (wakeFn_) wakeFn_();
}

bool AppState::connect(const std::string& device, int baud) {
    if (connected()) {
        pushLog("Already connected to " + connectedDevice_);
        return false;
    }

    if (sp_get_port_by_name(device.c_str(), &port_) != SP_OK) {
        pushLog("Failed to open port: " + device);
        return false;
    }
    if (sp_open(port_, SP_MODE_READ_WRITE) != SP_OK) {
        pushLog("sp_open failed for " + device);
        sp_free_port(port_);
        port_ = nullptr;
        return false;
    }
    sp_set_baudrate(port_, baud);
    sp_set_bits(port_, 8);
    sp_set_parity(port_, SP_PARITY_NONE);
    sp_set_stopbits(port_, 1);
    sp_set_flowcontrol(port_, SP_FLOWCONTROL_NONE);
    // Match pyserial: assert DTR/RTS — plotters gate their transmitter on DTR.
    sp_set_dtr(port_, SP_DTR_ON);
    sp_set_rts(port_, SP_RTS_ON);

    plotter_ = std::make_unique<hpgl::HpglPlotter>(port_);
    detectedModel_ = plotter_->applyModelInit();
    pushLog("Connected to " + device + " @ " + std::to_string(baud) +
            " — model: " + (detectedModel_.empty() ? "(unknown)" : detectedModel_));

    sender_ = std::make_unique<AsyncSerialSender>(plotter_.get());
    sender_->setBatchSize(1);
    sender_->setSoftwareHandshake(true);
    sender_->start();

    connectedDevice_ = device;
    connectedBaud_ = baud;
    connected_.store(true, std::memory_order_release);
    return true;
}

void AppState::disconnect() {
    if (!connected() && !port_) return;
    if (sender_) {
        sender_->stop();
        sender_.reset();
    }
    plotter_.reset();
    if (port_) {
        sp_close(port_);
        sp_free_port(port_);
        port_ = nullptr;
    }
    if (reporter_) {
        reporter_->finish();
        reporter_.reset();
    }
    connected_.store(false, std::memory_order_release);
    connectedDevice_.clear();
    connectedBaud_ = 0;
    detectedModel_.clear();
    progressIdx_.store(0);
    progressTotal_.store(0);
    pushLog("Disconnected.");
}

void AppState::sendCommand(const std::string& cmd) {
    if (!connected() || !sender_) {
        pushLog("Not connected.");
        return;
    }
    auto tokens = hpgl::tokenize(cmd);
    sender_->addCommands(std::move(tokens),
                         [this](std::size_t i, std::size_t n) {
                             onFileProgress(i, n);
                         });
    pushLog("Sent: " + cmd);
}

void AppState::insertCommand(const std::string& cmd) {
    if (!connected() || !sender_) {
        pushLog("Not connected.");
        return;
    }
    auto tokens = hpgl::tokenize(cmd);
    sender_->insertCommands(tokens);
    pushLog("Inserted: " + cmd);
}

bool AppState::sendFile(const std::string& path, double startPercent) {
    if (!connected() || !sender_) {
        pushLog("Not connected.");
        return false;
    }
    std::ifstream f(path);
    if (!f) {
        pushLog("Failed to open file: " + path);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    auto tokens = hpgl::tokenize(text);
    if (tokens.empty()) {
        pushLog("File is empty or unparseable: " + path);
        return false;
    }
    if (startPercent < 0.0) startPercent = 0.0;
    if (startPercent > 100.0) startPercent = 100.0;
    std::size_t startIdx = static_cast<std::size_t>(
        (startPercent / 100.0) * static_cast<double>(tokens.size()));

    currentFile_ = path;
    progressIdx_.store(startIdx);
    progressTotal_.store(tokens.size());
    sendStart_ = std::chrono::steady_clock::now();
    eta_.reset();
    etaSeconds_.store(0.0);

    std::string label = std::filesystem::path(path).filename().string();
    reporter_ = std::make_unique<ProgressReporter>(label);
    reporter_->start();

    sender_->addCommands(std::move(tokens),
                         [this](std::size_t i, std::size_t n) {
                             onFileProgress(i, n);
                         },
                         startIdx);

    pushLog("Sending " + label + (startPercent > 0.0
              ? " (resume " + std::to_string(static_cast<int>(startPercent)) + "%)"
              : ""));
    return true;
}

void AppState::onFileProgress(std::size_t idx, std::size_t total) {
    progressIdx_.store(idx, std::memory_order_release);
    progressTotal_.store(total, std::memory_order_release);
    double frac = total > 0 ? static_cast<double>(idx) / static_cast<double>(total) : 0.0;
    double eta = eta_.update(frac);
    etaSeconds_.store(eta, std::memory_order_release);
    if (reporter_) {
        if (frac >= 1.0) {
            reporter_->finish();
            reporter_.reset();
        } else {
            reporter_->report(frac);
        }
    }
    wake();
}

void AppState::abortFile() {
    if (sender_) sender_->abort();
    if (reporter_) {
        reporter_->finish();
        reporter_.reset();
    }
    progressIdx_.store(0);
    pushLog("Aborted.");
}

void AppState::togglePause() {
    if (sender_) {
        sender_->togglePause();
        pushLog(sender_->paused() ? "Paused." : "Resumed.");
    }
}

bool AppState::paused() const {
    return sender_ ? sender_->paused() : false;
}

int  AppState::batchSize()    const { return sender_ ? sender_->batchSize()    : 5; }
void AppState::setBatchSize(int b)  { if (sender_) sender_->setBatchSize(b); }
int  AppState::memoryLimit()  const { return sender_ ? sender_->memoryLimit()  : 64; }
void AppState::setMemoryLimit(int m){ if (sender_) sender_->setMemoryLimit(m); }

double AppState::progressFraction() const {
    std::size_t t = progressTotal_.load(std::memory_order_acquire);
    if (t == 0) return 0.0;
    std::size_t i = progressIdx_.load(std::memory_order_acquire);
    double f = static_cast<double>(i) / static_cast<double>(t);
    if (f < 0.0) return 0.0;
    if (f > 1.0) return 1.0;
    return f;
}

double AppState::elapsedSeconds() const {
    if (progressTotal_.load() == 0) return 0.0;
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - sendStart_).count();
}

void AppState::startRefreshPorts() {
    if (discoveryRunning_.exchange(true)) return;
    if (discoveryThread_.joinable()) discoveryThread_.join();
    discoveryThread_ = std::thread([this] {
        auto results = PortDiscovery::discover();
        {
            std::lock_guard<std::mutex> lk(discoveryMu_);
            discoveryResults_ = std::move(results);
            discoveryFresh_ = true;
        }
        discoveryRunning_.store(false, std::memory_order_release);
        pushLog("Discovery complete.");
        wake();
    });
}

bool AppState::takeDiscoveryResults(std::vector<DiscoveredPort>& out) {
    std::lock_guard<std::mutex> lk(discoveryMu_);
    if (!discoveryFresh_) return false;
    out = discoveryResults_;
    discoveryFresh_ = false;
    return true;
}

}
