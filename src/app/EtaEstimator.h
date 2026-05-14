#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

namespace serialpowertools {

// Sliding-window remaining-time estimator. Mirrors
// SerialInspectorGUI.estimate_remaining_time in main_qt5.py — 500-sample
// window, rate computed from oldest-to-newest, robust to progress jumps.
class EtaEstimator {
public:
    using Clock = std::chrono::steady_clock;

    explicit EtaEstimator(std::size_t windowSize = 500);

    // Add a sample. progress in [0, 1]. Returns the estimated remaining
    // seconds, or 0.0 if too few samples / non-positive rate.
    double update(double progress);

    // For testing: same as update() but using an explicit timestamp.
    double updateAt(double progress, Clock::time_point now);

    void reset();
    std::size_t sampleCount() const { return samples_.size(); }

    // Format a non-negative seconds count as "Ns" / "Xmin Ys" / "Hh Mm Ss".
    static std::string formatSeconds(double seconds);

private:
    struct Sample { Clock::time_point t; double progress; };
    std::deque<Sample> samples_;
    std::size_t windowSize_;
};

}
