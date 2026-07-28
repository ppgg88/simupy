#include "BlockUtils.h"

#include <cmath>

namespace simupy {
namespace {

double requireSampleTime(const Parameters& params) {
    const double sampleTime = params.real("sampleTime", 0.1);
    if (sampleTime <= 0.0)
        throw ModelError("the sample time must be strictly positive");
    return sampleTime;
}

class UnitDelayBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        const std::vector<double> initial =
            params().vector("initialCondition", {0.0});
        int width = s.inputConnected[0] ? s.inputWidths[0] : 1;
        width = std::max(width, static_cast<int>(initial.size()));

        initial_ = broadcast(initial, width, "the initial condition");
        s.outputWidths[0] = width;
        s.discreteStates = width;
        s.sampleTime = requireSampleTime(params());
        s.directFeedthrough[0] = false;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        xd = initial_;
    }

    void computeOutputs(const EvalContext& c) override { c.out(0) = c.xdisc(); }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < xdNext.size(); ++i)
            xdNext[i] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
    }

private:
    Vec initial_;
};

class ZeroOrderHoldBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        width_ = s.inheritedWidth();
        sampleTime_ = requireSampleTime(params());
        held_ = Vec::Zero(width_);
        lastSample_ = -std::numeric_limits<double>::infinity();

        s.outputWidths[0] = width_;
        s.sampleTime = sampleTime_;
        s.directFeedthrough[0] = true;
    }

    void computeOutputs(const EvalContext& c) override {
        if (c.majorStep && isSampleInstant(c.t)) {
            const Vec& u = c.in(0);
            for (int i = 0; i < width_; ++i)
                held_[i] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
            lastSample_ = c.t;
        }
        c.out(0) = held_;
    }

private:
    bool isSampleInstant(double t) const {
        if (lastSample_ == -std::numeric_limits<double>::infinity()) return true;
        return t - lastSample_ >= sampleTime_ * (1.0 - 1e-9);
    }

    int width_ = 1;
    double sampleTime_ = 0.1;
    double lastSample_ = -std::numeric_limits<double>::infinity();
    Vec held_;
};

class DiscreteIntegratorBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        gain_ = params().real("gain", 1.0);
        sampleTime_ = requireSampleTime(params());
        forwardEuler_ = params().text("method", "forward euler") !=
                        "backward euler";

        saturate_ = params().boolean("limitOutput", false);
        upper_ = params().real("upperLimit", 1.0);
        lower_ = params().real("lowerLimit", -1.0);

        const std::vector<double> initial =
            params().vector("initialCondition", {0.0});
        int width = s.inputConnected[0] ? s.inputWidths[0] : 1;
        width = std::max(width, static_cast<int>(initial.size()));
        initial_ = broadcast(initial, width, "the initial condition");

        s.outputWidths[0] = width;
        s.discreteStates = width;
        s.sampleTime = sampleTime_;
        s.directFeedthrough[0] = !forwardEuler_;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        xd = initial_;
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        y = c.xdisc();
        if (!forwardEuler_) {
            const Vec& u = c.in(0);
            for (Eigen::Index i = 0; i < y.size(); ++i)
                y[i] += sampleTime_ * gain_ *
                        (u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0));
        }
        if (saturate_) y = y.cwiseMax(lower_).cwiseMin(upper_);
    }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < xdNext.size(); ++i) {
            const double value =
                u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
            xdNext[i] = c.xd[i] + sampleTime_ * gain_ * value;
            if (saturate_) xdNext[i] = std::clamp(xdNext[i], lower_, upper_);
        }
    }

private:
    Vec initial_;
    double gain_ = 1.0, sampleTime_ = 0.1;
    double upper_ = 1.0, lower_ = -1.0;
    bool forwardEuler_ = true, saturate_ = false;
};

class DiscreteTransferFcnBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        b_ = params().vector("numerator", {1.0});
        a_ = params().vector("denominator", {1.0, -0.5});
        if (a_.empty() || a_[0] == 0.0)
            throw ModelError("the denominator must have a non-zero leading "
                             "coefficient");

        const double lead = a_[0];
        for (double& value : a_) value /= lead;
        for (double& value : b_) value /= lead;

        order_ = static_cast<int>(std::max(a_.size(), b_.size())) - 1;
        if (order_ < 1)
            throw ModelError("a discrete transfer function needs at least one "
                             "delay; use a Gain block for a static ratio");
        a_.resize(order_ + 1, 0.0);
        b_.resize(order_ + 1, 0.0);

        s.outputWidths[0] = 1;
        s.discreteStates = order_;
        s.sampleTime = requireSampleTime(params());
        s.directFeedthrough[0] = b_[0] != 0.0;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0)[0] = b_[0] * intermediate(c) + weightedState(c);
    }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        const double w = intermediate(c);
        for (int i = order_ - 1; i > 0; --i) xdNext[i] = c.xd[i - 1];
        xdNext[0] = w;
    }

private:
    double intermediate(const EvalContext& c) const {
        const Vec& u = c.in(0);
        double w = u.size() > 0 ? u[0] : 0.0;
        for (int i = 1; i <= order_; ++i) w -= a_[i] * c.xd[i - 1];
        return w;
    }

    double weightedState(const EvalContext& c) const {
        double sum = 0.0;
        for (int i = 1; i <= order_; ++i) sum += b_[i] * c.xd[i - 1];
        return sum;
    }

    std::vector<double> a_, b_;
    int order_ = 1;
};

class IntegerDelayBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        delay_ = params().integer("delay", 1);
        if (delay_ < 1) throw ModelError("the delay must be at least 1 sample");

        width_ = s.inheritedWidth();
        initialValue_ = params().real("initialCondition", 0.0);

        s.outputWidths[0] = width_;
        s.discreteStates = width_ * delay_;
        s.sampleTime = requireSampleTime(params());
        s.directFeedthrough[0] = false;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        xd.setConstant(initialValue_);
    }

    void computeOutputs(const EvalContext& c) override {
        for (int i = 0; i < width_; ++i)
            c.out(0)[i] = c.xd[(delay_ - 1) * width_ + i];
    }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        for (int slot = delay_ - 1; slot > 0; --slot)
            for (int i = 0; i < width_; ++i)
                xdNext[slot * width_ + i] = c.xd[(slot - 1) * width_ + i];

        const Vec& u = c.in(0);
        for (int i = 0; i < width_; ++i)
            xdNext[i] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
    }

private:
    int delay_ = 1, width_ = 1;
    double initialValue_ = 0.0;
};

}  // namespace

void registerDiscreteBlocks() {
    registerBlockType<UnitDelayBlock>(
        "UnitDelay", "Discrete", "Delays the signal by one sample period.",
        {realParam("sampleTime", "Sample time (s)", 0.1),
         vectorParam("initialCondition", "Initial condition", {0.0})},
        70.0, 60.0);

    registerBlockType<ZeroOrderHoldBlock>(
        "ZeroOrderHold", "Discrete",
        "Samples at a fixed rate and holds between samples.",
        {realParam("sampleTime", "Sample time (s)", 0.1)}, 70.0, 60.0);

    registerBlockType<DiscreteIntegratorBlock>(
        "DiscreteIntegrator", "Discrete", "Accumulates its input each sample.",
        {realParam("gain", "Gain", 1.0),
         realParam("sampleTime", "Sample time (s)", 0.1),
         choiceParam("method", "Integration method",
                     {"forward euler", "backward euler"},
                     std::string("forward euler")),
         vectorParam("initialCondition", "Initial condition", {0.0}),
         boolParam("limitOutput", "Limit output", false),
         realParam("upperLimit", "Upper limit", 1.0),
         realParam("lowerLimit", "Lower limit", -1.0)},
        80.0, 60.0);

    registerBlockType<DiscreteTransferFcnBlock>(
        "DiscreteTransferFcn", "Discrete",
        "A linear transfer function in the z domain.",
        {vectorParam("numerator", "Numerator coefficients", {1.0},
                     "Ascending powers of z^-1."),
         vectorParam("denominator", "Denominator coefficients", {1.0, -0.5},
                     "Ascending powers of z^-1."),
         realParam("sampleTime", "Sample time (s)", 0.1)},
        110.0, 60.0);

    registerBlockType<IntegerDelayBlock>(
        "IntegerDelay", "Discrete", "Delays the signal by N sample periods.",
        {intParam("delay", "Delay (samples)", 1, 1, 100000),
         realParam("sampleTime", "Sample time (s)", 0.1),
         realParam("initialCondition", "Initial condition", 0.0)},
        70.0, 60.0);
}

}  // namespace simupy
