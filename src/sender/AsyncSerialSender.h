#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hpgl { class HpglPlotter; }

namespace serialpowertools {

// Background thread that streams tokenized HPGL commands to an HpglPlotter
// in configurable-size batches with optional software flow control (polling
// ESC.B and waiting for enough free I/O buffer). Mirrors the Python
// AsyncSerialSender in reference/tools/serial_powertools/seriallib.py.
class AsyncSerialSender {
public:
    using ProgressCb = std::function<void(std::size_t index, std::size_t total)>;

    explicit AsyncSerialSender(hpgl::HpglPlotter* plotter);
    ~AsyncSerialSender();

    AsyncSerialSender(const AsyncSerialSender&) = delete;
    AsyncSerialSender& operator=(const AsyncSerialSender&) = delete;

    // Launch the worker thread. Safe to call exactly once.
    void start();

    // Request worker shutdown. Joins the thread.
    void stop();

    // Tell the worker to abort the current queue. The plotter receives
    // ESC.K + ESC.L and the queue is cleared.
    void abort();

    void togglePause();
    bool paused() const { return paused_.load(std::memory_order_acquire); }

    void setBatchSize(int bsize);
    int  batchSize() const;

    void setMemoryLimit(int limit);
    int  memoryLimit() const;

    void setSoftwareHandshake(bool on) { doSoftwareHandshake_ = on; }
    bool softwareHandshake() const     { return doSoftwareHandshake_; }

    // Replace the command queue and reset position to `currIndex`. The progress
    // callback is invoked from the worker thread after each batch.
    void addCommands(std::vector<std::string> commands,
                     ProgressCb cb,
                     std::size_t currIndex = 0);

    // Insert at the current send position (mid-stream).
    void insertCommands(const std::vector<std::string>& commands);

    // Live counters for the UI.
    std::size_t currentIndex() const { return currentIndex_.load(std::memory_order_acquire); }
    std::size_t total()        const { return total_.load(std::memory_order_acquire); }

private:
    void run();

    hpgl::HpglPlotter* plotter_;

    std::mutex mu_;
    std::condition_variable cv_;

    std::vector<std::string> commands_;        // guarded by mu_
    int  commandBatch_   = 5;                  // guarded by mu_
    int  memoryLimit_    = 64;                 // guarded by mu_
    ProgressCb progressCb_;                    // guarded by mu_

    std::atomic<std::size_t> currentIndex_{0};
    std::atomic<std::size_t> total_{0};

    std::atomic<bool> stopped_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> abortQueue_{false};
    std::atomic<bool> doSoftwareHandshake_{true};

    std::thread worker_;
    bool started_ = false;
};

}
