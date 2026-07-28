#include "SignalLog.h"

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

void SignalLog::decimate() {
    const std::size_t kept = (times_.size() + 1) / 2;

    for (std::size_t i = 0; i < kept; ++i) times_[i] = times_[i * 2];
    times_.resize(kept);

    for (LogChannel& channel : channels_) {
        const std::size_t width = static_cast<std::size_t>(channel.width);
        for (std::size_t i = 0; i < kept; ++i)
            for (std::size_t c = 0; c < width; ++c)
                channel.data[i * width + c] = channel.data[i * 2 * width + c];
        channel.data.resize(kept * width);
    }

    decimation_ *= 2;
    pending_ = 0;
}

}
