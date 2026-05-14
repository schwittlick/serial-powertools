#include "app/EtaEstimator.h"

#include "test_harness.h"

#include <chrono>
#include <cmath>

using serialpowertools::EtaEstimator;
using Clock = EtaEstimator::Clock;

int main() {
    // No samples → 0.
    {
        EtaEstimator e;
        EXPECT_EQ(e.update(0.0), 0.0);
    }

    // One sample → 0 (need ≥ 2).
    {
        EtaEstimator e;
        EXPECT_EQ(e.update(0.1), 0.0);
    }

    // Steady 10 %/sec: at t=10s, progress=1.0, eta should be 0; at t=5s,
    // progress=0.5, eta should be ~5s. We supply explicit timestamps.
    {
        EtaEstimator e(500);
        auto t0 = Clock::time_point{} + std::chrono::seconds(100);
        EXPECT_EQ(e.updateAt(0.0, t0), 0.0);
        double eta = e.updateAt(0.5, t0 + std::chrono::seconds(5));
        EXPECT(std::fabs(eta - 5.0) < 1e-6);
    }

    // Window cap: only the last N samples are used. Pushing 600 entries
    // should not push sampleCount above 500.
    {
        EtaEstimator e(500);
        auto t0 = Clock::time_point{} + std::chrono::seconds(0);
        for (int i = 0; i < 600; ++i) {
            e.updateAt(static_cast<double>(i) / 600.0,
                       t0 + std::chrono::milliseconds(i * 10));
        }
        EXPECT_EQ(e.sampleCount(), static_cast<std::size_t>(500));
    }

    // Non-monotonic / negative progress diff → 0.
    {
        EtaEstimator e;
        auto t0 = Clock::time_point{} + std::chrono::seconds(0);
        e.updateAt(0.5, t0);
        double eta = e.updateAt(0.3, t0 + std::chrono::seconds(1));
        EXPECT_EQ(eta, 0.0);
    }

    // Formatting.
    EXPECT_EQ(EtaEstimator::formatSeconds(0.0), std::string("0s"));
    EXPECT_EQ(EtaEstimator::formatSeconds(45.0), std::string("45s"));
    EXPECT_EQ(EtaEstimator::formatSeconds(125.0), std::string("2min 5s"));
    EXPECT_EQ(EtaEstimator::formatSeconds(3725.0), std::string("1h 2m 5s"));
    // Negative / non-finite are clamped to 0.
    EXPECT_EQ(EtaEstimator::formatSeconds(-1.0), std::string("0s"));
    EXPECT_EQ(EtaEstimator::formatSeconds(std::nan("")), std::string("0s"));

    std::printf("test_eta_estimator OK\n");
    return 0;
}
