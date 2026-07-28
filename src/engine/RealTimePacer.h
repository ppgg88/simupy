#pragma once

#include <chrono>
#include <functional>

namespace simupy {

class RealTimePacer {
public:
    RealTimePacer(double startTime, double factor);

    double waitUntil(double t, const std::function<bool()>& abort = {});

    void resync(double t);

    double worstLag() const { return worstLag_; }

    int resyncCount() const { return resyncs_; }

    bool heldRealTime() const { return resyncs_ == 0; }

    double factor() const { return factor_; }

    static constexpr double kResyncThreshold = 1.0;

    static constexpr std::chrono::milliseconds kSliceLimit{20};

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point deadlineFor(double t) const;

    Clock::time_point origin_;
    double originTime_ = 0.0;
    double factor_ = 1.0;
    double worstLag_ = 0.0;
    int resyncs_ = 0;
};

}
