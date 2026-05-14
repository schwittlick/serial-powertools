#include "app/EtaEstimator.h"

#include <cmath>
#include <cstdio>

namespace serialpowertools {

EtaEstimator::EtaEstimator(std::size_t windowSize) : windowSize_(windowSize) {}

double EtaEstimator::update(double progress) {
    return updateAt(progress, Clock::now());
}

double EtaEstimator::updateAt(double progress, Clock::time_point now) {
    samples_.push_back({now, progress});
    if (samples_.size() > windowSize_) samples_.pop_front();
    if (samples_.size() < 2) return 0.0;

    const auto& first = samples_.front();
    const auto& last  = samples_.back();
    double dt = std::chrono::duration<double>(last.t - first.t).count();
    double dp = last.progress - first.progress;
    if (dt <= 0.0 || dp <= 0.0) return 0.0;

    double rate = dp / dt;
    double remaining = 1.0 - progress;
    if (remaining <= 0.0) return 0.0;
    return remaining / rate;
}

void EtaEstimator::reset() {
    samples_.clear();
}

std::string EtaEstimator::formatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    long long s = static_cast<long long>(seconds + 0.5);
    long long h = s / 3600;
    long long m = (s % 3600) / 60;
    long long sec = s % 60;

    char buf[64];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lldh %lldm %llds", h, m, sec);
    } else if (m > 0) {
        std::snprintf(buf, sizeof(buf), "%lldmin %llds", m, sec);
    } else {
        std::snprintf(buf, sizeof(buf), "%llds", sec);
    }
    return std::string(buf);
}

}
