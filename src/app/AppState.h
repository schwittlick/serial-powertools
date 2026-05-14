#pragma once

#include "app/EtaEstimator.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct sp_port;

namespace hpgl       { class HpglPlotter; }
namespace serialpowertools {
class AsyncSerialSender;
class ProgressReporter;
struct DiscoveredPort;
}

namespace serialpowertools {

// Single owner of plotter, sender thread, log buffer, file-send state. The
// UI layer polls this each frame; worker threads push state through it.
class AppState {
public:
    AppState();
    ~AppState();

    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    // ─── Connection ────────────────────────────────────────────────────────
    bool connect(const std::string& device, int baud);
    void disconnect();
    bool connected() const { return connected_.load(std::memory_order_acquire); }
    const std::string& connectedDevice() const { return connectedDevice_; }
    int connectedBaud() const { return connectedBaud_; }
    const std::string& detectedModel() const { return detectedModel_; }

    // ─── Manual command ────────────────────────────────────────────────────
    void sendCommand(const std::string& cmd);
    void insertCommand(const std::string& cmd);

    // ─── File send ─────────────────────────────────────────────────────────
    bool sendFile(const std::string& path, double startPercent);
    void abortFile();
    void togglePause();
    bool paused() const;

    int  batchSize() const;
    void setBatchSize(int b);
    int  memoryLimit() const;
    void setMemoryLimit(int m);

    double progressFraction() const;
    double etaSeconds() const { return etaSeconds_.load(std::memory_order_acquire); }
    double elapsedSeconds() const;
    const std::string& currentFile() const { return currentFile_; }

    // ─── Discovery ─────────────────────────────────────────────────────────
    void startRefreshPorts();
    bool discoveryInFlight() const { return discoveryRunning_.load(std::memory_order_acquire); }
    // Drain the latest discovery results. Returns true and writes into `out`
    // if a result is available; false otherwise.
    bool takeDiscoveryResults(std::vector<DiscoveredPort>& out);

    // ─── Logging ───────────────────────────────────────────────────────────
    void pushLog(const std::string& line);
    // Snapshot the current log; cheap copy of a deque of strings.
    std::deque<std::string> snapshotLog() const;
    void clearLog();

    // For UI: opaque pointer (avoid leaking GLFW into headers everywhere). Set
    // by main.cpp so worker threads can wake the GUI on state change.
    using WakeFn = void(*)();
    void setWakeFn(WakeFn fn) { wakeFn_ = fn; }
    void wake();

private:
    void onFileProgress(std::size_t idx, std::size_t total);

    // Connection state.
    sp_port*                                 port_ = nullptr;
    std::unique_ptr<hpgl::HpglPlotter>       plotter_;
    std::unique_ptr<AsyncSerialSender>       sender_;
    std::atomic<bool>                        connected_{false};
    std::string                              connectedDevice_;
    int                                      connectedBaud_ = 0;
    std::string                              detectedModel_;

    // File-send state.
    std::string                              currentFile_;
    std::atomic<std::size_t>                 progressIdx_{0};
    std::atomic<std::size_t>                 progressTotal_{0};
    std::atomic<double>                      etaSeconds_{0.0};
    std::chrono::steady_clock::time_point    sendStart_{};
    EtaEstimator                             eta_;
    std::unique_ptr<ProgressReporter>        reporter_;

    // Discovery.
    std::atomic<bool>                        discoveryRunning_{false};
    std::thread                              discoveryThread_;
    mutable std::mutex                       discoveryMu_;
    std::vector<DiscoveredPort>              discoveryResults_;
    bool                                     discoveryFresh_ = false;

    // Log buffer.
    mutable std::mutex                       logMu_;
    std::deque<std::string>                  log_;

    WakeFn                                   wakeFn_ = nullptr;
};

}
