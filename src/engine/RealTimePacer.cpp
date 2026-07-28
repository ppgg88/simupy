#include "RealTimePacer.h"

#include <algorithm>
#include <thread>

namespace simupy {
namespace {

using Seconds = std::chrono::duration<double>;

}

RealTimePacer::RealTimePacer(double startTime, double factor)
    : origin_(Clock::now()),
      originTime_(startTime),
      factor_(factor > 0.0 ? factor : 1.0) {}

// Measured from the origin: per-period deadlines let one slow step drift.
RealTimePacer::Clock::time_point RealTimePacer::deadlineFor(double t) const {
    return origin_ +
           std::chrono::duration_cast<Clock::duration>(
               Seconds((t - originTime_) / factor_));
}

void RealTimePacer::resync(double t) {
    origin_ = Clock::now();
    originTime_ = t;
}

double RealTimePacer::waitUntil(double t, const std::function<bool()>& abort) {
    const Clock::time_point deadline = deadlineFor(t);
    Clock::time_point now = Clock::now();

    if (now >= deadline) {
        const double lag = Seconds(now - deadline).count();
        worstLag_ = std::max(worstLag_, lag);

        if (lag > kResyncThreshold) {
            ++resyncs_;
            resync(t);
        }
        return lag;
    }

    while (now < deadline) {
        if (abort && abort()) return 0.0;

        const Clock::duration remaining = deadline - now;
        std::this_thread::sleep_for(std::min<Clock::duration>(
            remaining, std::chrono::duration_cast<Clock::duration>(kSliceLimit)));
        now = Clock::now();
    }
    return 0.0;
}

}
