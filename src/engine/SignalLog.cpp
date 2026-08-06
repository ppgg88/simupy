#include "SignalLog.h"

#include <algorithm>
#include <cmath>

namespace simupy {

void SignalLog::configure(std::vector<LogChannel> channels, int maxSamples) {
    channels_ = std::move(channels);
    maxSamples_ = maxSamples > 16 ? maxSamples : 16;
    clear();
}

void SignalLog::clear() {
    times_.clear();
    for (LogChannel& channel : channels_) channel.data.clear();
    decimation_ = 1;
    pending_ = 0;
}

void SignalLog::append(double t, const std::vector<const Vec*>& values) {
    if (pending_++ % decimation_ != 0) return;

    times_.push_back(t);
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        LogChannel& channel = channels_[i];
        const Vec& value = *values[i];
        const int width = channel.width;
        for (int c = 0; c < width; ++c)
            channel.data.push_back(c < value.size() ? value[c] : 0.0);
    }

    if (static_cast<int>(times_.size()) >= maxSamples_) decimate();
}

SignalLog SignalLog::sampled(int maxSamples) const {
    const std::size_t total = times_.size();
    if (maxSamples < 2 || total <= static_cast<std::size_t>(maxSamples))
        return *this;

    const std::size_t want = static_cast<std::size_t>(maxSamples);

    SignalLog out;
    out.maxSamples_ = maxSamples_;
    out.decimation_ = decimation_;
    out.pending_ = pending_;
    // Metadata only: assigning the channels would copy every sample first.
    out.channels_.reserve(channels_.size());
    for (const LogChannel& from : channels_) {
        LogChannel to;
        to.blockPath = from.blockPath;
        to.blockName = from.blockName;
        to.portName = from.portName;
        to.signalName = from.signalName;
        to.port = from.port;
        to.width = from.width;
        to.data.reserve(want * static_cast<std::size_t>(from.width));
        out.channels_.push_back(std::move(to));
    }
    out.times_.reserve(want);

    // Evenly spaced, ends included, strictly increasing.
    std::size_t previous = total;
    for (std::size_t i = 0; i < want; ++i) {
        const std::size_t pick = i * (total - 1) / (want - 1);
        if (previous != total && pick <= previous) continue;
        previous = pick;

        out.times_.push_back(times_[pick]);
        for (std::size_t c = 0; c < channels_.size(); ++c) {
            const LogChannel& from = channels_[c];
            const std::size_t width = static_cast<std::size_t>(from.width);
            LogChannel& to = out.channels_[c];
            for (std::size_t k = 0; k < width; ++k)
                to.data.push_back(from.data[pick * width + k]);
        }
    }
    return out;
}

void SignalLog::decimate() {
    const std::size_t total = times_.size();
    const std::size_t kept = (total + 1) / 2;

    // Of each pair keep the further from the last kept, or a spike on an odd
    // index is lost outright.
    for (std::size_t out = 0; out < kept; ++out) {
        const std::size_t first = out * 2;
        const std::size_t second = first + 1;

        std::size_t pick = first;
        if (second < total && out > 0) {
            double reachFirst = 0.0;
            double reachSecond = 0.0;
            for (const LogChannel& channel : channels_) {
                const std::size_t width =
                    static_cast<std::size_t>(channel.width);
                for (std::size_t c = 0; c < width; ++c) {
                    const double previous = channel.data[(out - 1) * width + c];
                    reachFirst = std::max(
                        reachFirst,
                        std::abs(channel.data[first * width + c] - previous));
                    reachSecond = std::max(
                        reachSecond,
                        std::abs(channel.data[second * width + c] - previous));
                }
            }
            if (reachSecond > reachFirst) pick = second;
        }

        // Safe in place: out <= pick, and later picks only grow.
        times_[out] = times_[pick];
        for (LogChannel& channel : channels_) {
            const std::size_t width = static_cast<std::size_t>(channel.width);
            for (std::size_t c = 0; c < width; ++c)
                channel.data[out * width + c] =
                    channel.data[pick * width + c];
        }
    }

    times_.resize(kept);
    for (LogChannel& channel : channels_)
        channel.data.resize(kept * static_cast<std::size_t>(channel.width));

    decimation_ *= 2;
    pending_ = 0;
}

}
