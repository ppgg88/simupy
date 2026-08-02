#pragma once

#include "model/Block.h"

#include <mutex>

namespace simupy {

/// The value is published under a lock: written by the solver thread, read by
/// the interface thread while the run is still going.
class DisplaySink : public Block {
public:
    Vec latest() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return latest_;
    }

    void publish(const Vec& value) {
        std::lock_guard<std::mutex> guard(mutex_);
        latest_ = value;
    }

    void clearValue() {
        std::lock_guard<std::mutex> guard(mutex_);
        latest_ = Vec();
    }

private:
    mutable std::mutex mutex_;
    Vec latest_;
};

bool isDisplay(const Block* block);

/// Empty until the block has seen a major step.
Vec displayedValue(const Block* block);

}
