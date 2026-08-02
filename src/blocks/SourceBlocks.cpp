#include "BlockUtils.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace simupy {
namespace {

class SourceBlock : public Block {
public:
    PortLayout ports() const override { return {{}, {"out"}}; }
};

class ConstantBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        value_ = params().eigenVector("value", Vec::Ones(1));
        if (value_.size() == 0)
            throw ModelError("the constant value cannot be empty");
        s.outputWidths[0] = static_cast<int>(value_.size());
    }

    void computeOutputs(const EvalContext& c) override { c.out(0) = value_; }

private:
    Vec value_;
};

class StepBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        stepTime_ = params().real("stepTime", 1.0);
        initial_ = params().vector("initialValue", {0.0});
        final_ = params().vector("finalValue", {1.0});

        const int width = static_cast<int>(
            std::max(initial_.size(), final_.size()));
        initialVec_ = broadcast(initial_, width, "the initial value");
        finalVec_ = broadcast(final_, width, "the final value");
        s.outputWidths[0] = width;
        s.zeroCrossings = 1;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c.t < stepTime_ ? initialVec_ : finalVec_;
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        zc[0] = c.t - stepTime_;
    }

private:
    double stepTime_ = 1.0;
    std::vector<double> initial_, final_;
    Vec initialVec_, finalVec_;
};

class RampBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        slope_ = params().real("slope", 1.0);
        startTime_ = params().real("startTime", 0.0);
        initial_ = params().real("initialOutput", 0.0);
        s.outputWidths[0] = 1;
    }

    void computeOutputs(const EvalContext& c) override {
        const double elapsed = c.t - startTime_;
        c.out(0)[0] = elapsed <= 0.0 ? initial_ : initial_ + slope_ * elapsed;
    }

private:
    double slope_ = 1.0, startTime_ = 0.0, initial_ = 0.0;
};

class SineBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        amplitude_ = params().vector("amplitude", {1.0});
        frequency_ = params().real("frequency", 1.0);
        phase_ = params().real("phase", 0.0);
        bias_ = params().vector("bias", {0.0});
        inHertz_ = params().text("frequencyUnit", "rad/s") == "Hz";

        const int width =
            static_cast<int>(std::max(amplitude_.size(), bias_.size()));
        amplitudeVec_ = broadcast(amplitude_, width, "the amplitude");
        biasVec_ = broadcast(bias_, width, "the bias");
        omega_ = inHertz_ ? 2.0 * kPi * frequency_ : frequency_;
        s.outputWidths[0] = width;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = biasVec_ + amplitudeVec_ * std::sin(omega_ * c.t + phase_);
    }

private:
    std::vector<double> amplitude_, bias_;
    Vec amplitudeVec_, biasVec_;
    double frequency_ = 1.0, phase_ = 0.0, omega_ = 1.0;
    bool inHertz_ = false;
};

class ClockBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override { s.outputWidths[0] = 1; }
    void computeOutputs(const EvalContext& c) override { c.out(0)[0] = c.t; }
};

class PulseGeneratorBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        amplitude_ = params().real("amplitude", 1.0);
        period_ = params().real("period", 1.0);
        dutyCycle_ = params().real("dutyCycle", 50.0);
        phaseDelay_ = params().real("phaseDelay", 0.0);

        if (period_ <= 0.0) throw ModelError("the period must be positive");
        if (dutyCycle_ < 0.0 || dutyCycle_ > 100.0)
            throw ModelError("the duty cycle must be between 0 and 100 %");
        s.outputWidths[0] = 1;
        s.zeroCrossings = 2;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0)[0] = level(c.t);
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const double elapsed = c.t - phaseDelay_;
        if (elapsed < 0.0) {
            zc[0] = elapsed;
            zc[1] = elapsed;
            return;
        }

        const double phase = std::fmod(elapsed, period_) / period_;
        zc[0] = wrapCentered(phase);
        zc[1] = wrapCentered(phase - dutyCycle_ / 100.0);
    }

private:
    static double wrapCentered(double phase) {
        double wrapped = std::fmod(phase, 1.0);
        if (wrapped < -0.5) wrapped += 1.0;
        if (wrapped >= 0.5) wrapped -= 1.0;
        return wrapped;
    }

    double level(double t) const {
        const double elapsed = t - phaseDelay_;
        if (elapsed < 0.0) return 0.0;
        const double phase = std::fmod(elapsed, period_) / period_;
        return phase < dutyCycle_ / 100.0 ? amplitude_ : 0.0;
    }

    double amplitude_ = 1.0, period_ = 1.0, dutyCycle_ = 50.0,
           phaseDelay_ = 0.0;
};

class ChirpBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        amplitude_ = params().real("amplitude", 1.0);
        f0_ = params().real("startFrequency", 0.1);
        f1_ = params().real("endFrequency", 10.0);
        sweepTime_ = params().real("sweepTime", 10.0);
        if (sweepTime_ <= 0.0)
            throw ModelError("the sweep duration must be positive");
        s.outputWidths[0] = 1;
    }

    void computeOutputs(const EvalContext& c) override {
        const double rate = (f1_ - f0_) / sweepTime_;
        const double phase = 2.0 * kPi * (f0_ * c.t + 0.5 * rate * c.t * c.t);
        c.out(0)[0] = amplitude_ * std::sin(phase);
    }

private:
    double amplitude_ = 1.0, f0_ = 0.1, f1_ = 10.0, sweepTime_ = 10.0;
};

class RandomNumberBlock : public SourceBlock {
public:
    void setup(BlockSetup& s) override {
        mean_ = params().real("mean", 0.0);
        deviation_ = params().real("deviation", 1.0);
        seed_ = static_cast<unsigned>(params().integer("seed", 0));
        uniform_ = params().text("distribution", "gaussian") == "uniform";

        const int width = std::max(1, params().integer("width", 1));
        const double sampleTime = params().real("sampleTime", 0.1);
        if (sampleTime <= 0.0)
            throw ModelError("the sample time must be positive");

        s.outputWidths[0] = width;
        s.discreteStates = width;
        s.sampleTime = sampleTime;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        engine_.seed(seed_);
        draw(xd);
    }

    void computeOutputs(const EvalContext& c) override { c.out(0) = c.xdisc(); }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        (void)c;
        draw(xdNext);
    }

private:
    void draw(Eigen::Ref<Vec> out) {
        if (uniform_) {
            const double half = deviation_ * std::sqrt(3.0);
            std::uniform_real_distribution<double> dist(mean_ - half,
                                                        mean_ + half);
            for (Eigen::Index i = 0; i < out.size(); ++i) out[i] = dist(engine_);
        } else {
            std::normal_distribution<double> dist(mean_, deviation_);
            for (Eigen::Index i = 0; i < out.size(); ++i) out[i] = dist(engine_);
        }
    }

    double mean_ = 0.0, deviation_ = 1.0;
    unsigned seed_ = 0;
    bool uniform_ = false;
    std::mt19937 engine_;
};

}

void registerSourceBlocks() {
    registerBlockType<ConstantBlock>(
        "Constant", "Sources", "Emits a fixed scalar or vector value.",
        {vectorParam("value", "Constant value", {1.0},
                     "A single number, or a comma-separated list for a vector "
                     "signal.")});

    registerBlockType<StepBlock>(
        "Step", "Sources", "Switches from one level to another at a given time.",
        {realParam("stepTime", "Step time (s)", 1.0),
         vectorParam("initialValue", "Initial value", {0.0}),
         vectorParam("finalValue", "Final value", {1.0})});

    registerBlockType<RampBlock>(
        "Ramp", "Sources", "A signal increasing linearly with time.",
        {realParam("slope", "Slope (per second)", 1.0),
         realParam("startTime", "Start time (s)", 0.0),
         realParam("initialOutput", "Initial output", 0.0)});

    registerBlockType<SineBlock>(
        "Sine", "Sources", "A sine wave with configurable amplitude and phase.",
        {vectorParam("amplitude", "Amplitude", {1.0}),
         realParam("frequency", "Frequency", 1.0),
         choiceParam("frequencyUnit", "Frequency unit", {"rad/s", "Hz"},
                     std::string("rad/s")),
         realParam("phase", "Phase (rad)", 0.0),
         vectorParam("bias", "Bias", {0.0})});

    registerBlockType<ClockBlock>("Clock", "Sources",
                                  "Outputs the current simulation time.", {},
                                  70.0, 50.0);

    registerBlockType<PulseGeneratorBlock>(
        "Pulse", "Sources", "A periodic rectangular pulse train.",
        {realParam("amplitude", "Amplitude", 1.0),
         realParam("period", "Period (s)", 1.0),
         realParam("dutyCycle", "Duty cycle (%)", 50.0),
         realParam("phaseDelay", "Phase delay (s)", 0.0)});

    registerBlockType<ChirpBlock>(
        "Chirp", "Sources", "A sine whose frequency sweeps linearly with time.",
        {realParam("amplitude", "Amplitude", 1.0),
         realParam("startFrequency", "Start frequency (Hz)", 0.1),
         realParam("endFrequency", "End frequency (Hz)", 10.0),
         realParam("sweepTime", "Sweep duration (s)", 10.0)});

    registerBlockType<RandomNumberBlock>(
        "RandomNumber", "Sources", "Random values redrawn at a fixed rate.",
        {choiceParam("distribution", "Distribution", {"gaussian", "uniform"},
                     std::string("gaussian")),
         realParam("mean", "Mean", 0.0),
         realParam("deviation", "Standard deviation", 1.0),
         realParam("sampleTime", "Sample time (s)", 0.1),
         intParam("width", "Signal width", 1),
         intParam("seed", "Random seed", 0, 0, 1e9)});
}

}
