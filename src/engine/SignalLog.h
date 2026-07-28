#pragma once

#include "model/Types.h"

#include <string>
#include <vector>

namespace simupy {

struct LogChannel {
    std::string blockPath;
    std::string blockName;
    std::string portName;
    /// What the user called the signal, empty when it was never named. Takes
    /// precedence in label(), because a name someone chose says more than the
    /// block that happens to produce it.
    std::string signalName;
    int port = 0;
    int width = 1;
    std::vector<double> data;

    double at(int sample, int component) const {
        return data[static_cast<std::size_t>(sample) * width + component];
    }

    std::vector<double> series(int component) const {
        const std::size_t n = data.size() / static_cast<std::size_t>(width);
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = data[i * width + component];
        return out;
    }

    std::string label(int component) const {
        std::string base =
            signalName.empty() ? blockName + " : " + portName : signalName;
        if (width > 1) base += "[" + std::to_string(component) + "]";
        return base;
    }
};

class SignalLog {
public:
    void configure(std::vector<LogChannel> channels, int maxSamples);
    void clear();

    void append(double t, const std::vector<const Vec*>& values);

    int sampleCount() const { return static_cast<int>(times_.size()); }
    const std::vector<double>& times() const { return times_; }
    const std::vector<LogChannel>& channels() const { return channels_; }
    bool empty() const { return times_.empty(); }

    int decimation() const { return decimation_; }

private:
    void decimate();

    std::vector<LogChannel> channels_;
    std::vector<double> times_;
    int maxSamples_ = 200000;
    int decimation_ = 1;
    long long pending_ = 0;
};

}  // namespace simupy
