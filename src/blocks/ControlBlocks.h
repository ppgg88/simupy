#pragma once

#include "model/Block.h"

#include <atomic>
#include <string>

namespace simupy {

struct ControlDescriptor {
    enum class Kind {
        Slider,
        Toggle,
        Button,
    };

    Kind kind = Kind::Slider;
    std::string label;

    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.0;

    double offValue = 0.0;
    double onValue = 1.0;
};

/// The value is atomic: written by the interface thread, read by the solver.
class InteractiveBlock : public Block {
public:
    PortLayout ports() const override { return {{}, {"out"}}; }

    void setup(BlockSetup& s) override {
        bool expected = false;
        if (seeded_.compare_exchange_strong(expected, true,
                                            std::memory_order_relaxed))
            live_.store(initialValue(), std::memory_order_relaxed);
        s.outputWidths[0] = 1;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0).setConstant(live_.load(std::memory_order_relaxed));
    }

    double liveValue() const { return live_.load(std::memory_order_relaxed); }

    void setLiveValue(double value) {
        live_.store(value, std::memory_order_relaxed);
        seeded_.store(true, std::memory_order_relaxed);
    }

    void reseedFromParams() {
        live_.store(initialValue(), std::memory_order_relaxed);
        seeded_.store(true, std::memory_order_relaxed);
    }

    virtual ControlDescriptor descriptor() const = 0;

    virtual double initialValue() const = 0;

    virtual void commitValue(double value) = 0;

protected:
    std::atomic<double> live_{0.0};
    std::atomic<bool> seeded_{false};
};

bool isInteractive(const Block* block);

void registerControlBlocks();

}
