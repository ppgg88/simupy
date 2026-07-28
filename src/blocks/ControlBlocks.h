#pragma once

#include "model/Block.h"

#include <atomic>
#include <string>

namespace simupy {

struct ControlDescriptor {
    enum class Kind {
        Slider,  ///< a value dragged anywhere between two bounds
        Toggle,  ///< two values, latched
        Button,  ///< two values, the second held only while pressed
    };

    Kind kind = Kind::Slider;
    std::string label;

    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.0;

    double offValue = 0.0;
    double onValue = 1.0;
};

/// A source whose output the user drives while the model is running.
///
/// The value lives in an atomic because it is written by the interface thread
/// and read by the simulation thread — two genuinely different threads, where
/// a torn double is a real bug rather than a theoretical one. Nothing else is
/// shared: the block's parameters are read at setup and never again, so moving
/// a slider mid-run cannot race with anything the solver is doing.
///
/// A change lands at the next evaluation. With a variable-step solver that
/// means it takes effect somewhere inside the current step rather than at a
/// declared instant — see the note on interactive controls in the README.
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

    /// Called from the interface thread, at any time, running or not.
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
    /// False until the value has come from somewhere — the parameters at the
    /// first setup, or the user at any time.
    std::atomic<bool> seeded_{false};
};

bool isInteractive(const Block* block);

void registerControlBlocks();

}  // namespace simupy
