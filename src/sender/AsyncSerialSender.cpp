#include "sender/AsyncSerialSender.h"

#include "hpgl/HpglPlotter.h"
#include "hpgl/Tokenizer.h"

#include <algorithm>
#include <chrono>

namespace serialpowertools {

using namespace std::chrono_literals;

AsyncSerialSender::AsyncSerialSender(hpgl::HpglPlotter* plotter)
    : plotter_(plotter) {}

AsyncSerialSender::~AsyncSerialSender() {
    stop();
}

void AsyncSerialSender::start() {
    if (started_) return;
    started_ = true;
    worker_ = std::thread([this] { run(); });
}

void AsyncSerialSender::stop() {
    stopped_.store(true, std::memory_order_release);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AsyncSerialSender::abort() {
    abortQueue_.store(true, std::memory_order_release);
    cv_.notify_all();
}

void AsyncSerialSender::togglePause() {
    bool was = paused_.load(std::memory_order_acquire);
    paused_.store(!was, std::memory_order_release);
    cv_.notify_all();
}

void AsyncSerialSender::setBatchSize(int bsize) {
    std::lock_guard<std::mutex> lk(mu_);
    commandBatch_ = std::max(1, bsize);
}

int AsyncSerialSender::batchSize() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
    return commandBatch_;
}

void AsyncSerialSender::setMemoryLimit(int limit) {
    std::lock_guard<std::mutex> lk(mu_);
    memoryLimit_ = std::max(1, limit);
}

int AsyncSerialSender::memoryLimit() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
    return memoryLimit_;
}

void AsyncSerialSender::addCommands(std::vector<std::string> commands,
                                    ProgressCb cb,
                                    std::size_t currIndex) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        commands_ = std::move(commands);
        commandBatch_ = std::min<int>(commandBatch_,
                                      std::max<int>(1, static_cast<int>(commands_.size())));
        progressCb_ = std::move(cb);
        if (currIndex > commands_.size()) currIndex = commands_.size();
        currentIndex_.store(currIndex, std::memory_order_release);
        total_.store(commands_.size(), std::memory_order_release);
    }
    cv_.notify_all();
}

void AsyncSerialSender::insertCommands(const std::vector<std::string>& commands) {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t pos = currentIndex_.load(std::memory_order_acquire);
    if (pos > commands_.size()) pos = commands_.size();
    commands_.insert(commands_.begin() + static_cast<std::ptrdiff_t>(pos),
                     commands.begin(), commands.end());
    total_.store(commands_.size(), std::memory_order_release);
}

void AsyncSerialSender::run() {
    while (!stopped_.load(std::memory_order_acquire)) {
        // Inner loop: send batches until the queue is exhausted.
        while (!stopped_.load(std::memory_order_acquire)) {
            std::size_t idx = currentIndex_.load(std::memory_order_acquire);
            std::size_t total;
            {
                std::lock_guard<std::mutex> lk(mu_);
                total = commands_.size();
                if (idx >= total) break;
            }

            if (abortQueue_.exchange(false, std::memory_order_acq_rel)) {
                plotter_->abort();
                std::lock_guard<std::mutex> lk(mu_);
                commands_.clear();
                currentIndex_.store(0, std::memory_order_release);
                total_.store(0, std::memory_order_release);
                break;
            }

            std::string cmds;
            std::size_t endIndex;
            int memoryLimit;
            ProgressCb cb;
            {
                std::lock_guard<std::mutex> lk(mu_);
                endIndex = std::min(idx + static_cast<std::size_t>(commandBatch_),
                                    commands_.size());
                cmds = hpgl::concatCommands(commands_, idx, endIndex);
                memoryLimit = memoryLimit_;
                cb = progressCb_;
            }

            if (doSoftwareHandshake_.load(std::memory_order_acquire)) {
                int requested = static_cast<int>(cmds.size());
                int freeMem = std::min(plotter_->freeMemory(), memoryLimit);
                if (freeMem < requested) {
                    // Match Python: sleep 1 s and re-check (no plot progress meanwhile).
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait_for(lk, 1s, [&] {
                        return stopped_.load(std::memory_order_acquire) ||
                               abortQueue_.load(std::memory_order_acquire);
                    });
                    continue;
                }
            }

            plotter_->write(cmds);

            // Pause: use cv to avoid busy-waiting (mirrors Python's 100 ms sleep but cleaner).
            while (paused_.load(std::memory_order_acquire) &&
                   !stopped_.load(std::memory_order_acquire) &&
                   !abortQueue_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, 100ms, [&] {
                    return !paused_.load(std::memory_order_acquire) ||
                           stopped_.load(std::memory_order_acquire) ||
                           abortQueue_.load(std::memory_order_acquire);
                });
            }

            currentIndex_.store(endIndex, std::memory_order_release);
            std::this_thread::sleep_for(10ms);

            if (cb) cb(endIndex, total);
        }

        // Outer wait: queue exhausted. Sleep until new commands arrive or shutdown.
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, 1s, [&] {
            return stopped_.load(std::memory_order_acquire) ||
                   currentIndex_.load(std::memory_order_acquire) < commands_.size();
        });
    }
}

}
