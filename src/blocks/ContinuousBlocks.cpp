#include "BlockUtils.h"

#include <cmath>

namespace simupy {
namespace {

class IntegratorBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        saturate_ = params().boolean("limitOutput", false);
        upper_ = params().real("upperLimit", 1.0);
        lower_ = params().real("lowerLimit", -1.0);

        const std::vector<double> initial = params().vector("initialCondition",
                                                            {0.0});
        int width = 1;
        if (s.inputCount() > 0 && s.inputConnected[0])
            width = s.inputWidths[0];
        width = std::max(width, static_cast<int>(initial.size()));

        initial_ = broadcast(initial, width, "the initial condition");
        width_ = width;
        s.outputWidths[0] = width;
        s.continuousStates = width;
        s.directFeedthrough.assign(s.inputCount(), false);
        s.zeroCrossings = saturate_ ? 2 * width : 0;
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        for (int i = 0; i < width_; ++i) {
            zc[2 * i] = c.xc[i] - upper_;
            zc[2 * i + 1] = c.xc[i] - lower_;
        }
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xd;
        xc = initial_;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c.x();
        if (saturate_) c.out(0) = c.out(0).cwiseMax(lower_).cwiseMin(upper_);
    }

    void computeDerivatives(const EvalContext& c,
                            Eigen::Ref<Vec> dxc) override {
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < dxc.size(); ++i) {
            double rate = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
            if (saturate_) {
                const double state = c.xc[i];
                if ((state >= upper_ && rate > 0.0) ||
                    (state <= lower_ && rate < 0.0))
                    rate = 0.0;
            }
            dxc[i] = rate;
        }
    }

private:
    Vec initial_;
    int width_ = 1;
    bool saturate_ = false;
    double upper_ = 1.0, lower_ = -1.0;
};

class DerivativeBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        width_ = s.inheritedWidth();
        s.outputWidths[0] = width_;
        s.discreteStates = width_ + 1;
        s.sampleTime = kContinuousSampleTime;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        xd.setZero();
        xd[0] = std::numeric_limits<double>::quiet_NaN();
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const double previousTime = c.xd[0];
        const double dt = c.t - previousTime;

        if (std::isnan(previousTime) || dt <= 0.0) {
            y.setZero();
            return;
        }
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            const double current = u.size() == 1 ? u[0] : u[i];
            y[i] = (current - c.xd[i + 1]) / dt;
        }
    }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        const Vec& u = c.in(0);
        xdNext[0] = c.t;
        for (int i = 0; i < width_; ++i)
            xdNext[i + 1] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
    }

private:
    int width_ = 1;
};

class TransferFcnBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        std::vector<double> numerator = params().vector("numerator", {1.0});
        std::vector<double> denominator = params().vector("denominator",
                                                          {1.0, 1.0});

        while (denominator.size() > 1 && denominator.front() == 0.0)
            denominator.erase(denominator.begin());
        while (numerator.size() > 1 && numerator.front() == 0.0)
            numerator.erase(numerator.begin());

        if (denominator.empty() || denominator[0] == 0.0)
            throw ModelError("the denominator must have a non-zero leading "
                             "coefficient");
        if (numerator.size() > denominator.size())
            throw ModelError(
                "the transfer function is improper: the numerator has degree " +
                std::to_string(numerator.size() - 1) +
                " but the denominator only " +
                std::to_string(denominator.size() - 1));

        order_ = static_cast<int>(denominator.size()) - 1;
        if (order_ == 0)
            throw ModelError("a transfer function needs at least one pole; use "
                             "a Gain block for a static ratio");

        const double lead = denominator[0];
        a_.assign(denominator.size(), 0.0);
        for (std::size_t i = 0; i < denominator.size(); ++i)
            a_[i] = denominator[i] / lead;

        b_.assign(denominator.size(), 0.0);
        const std::size_t offset = denominator.size() - numerator.size();
        for (std::size_t i = 0; i < numerator.size(); ++i)
            b_[offset + i] = numerator[i] / lead;

        d_ = b_[0];
        c_.resize(order_);
        for (int i = 0; i < order_; ++i)
            c_[i] = b_[i + 1] - a_[i + 1] * d_;

        s.outputWidths[0] = 1;
        s.continuousStates = order_;
        s.directFeedthrough[0] = d_ != 0.0;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xd;
        xc.setZero();
    }

    void computeOutputs(const EvalContext& c) override {
        double y = d_ * scalarInput(c);
        for (int i = 0; i < order_; ++i) y += c_[i] * c.xc[i];
        c.out(0)[0] = y;
    }

    void computeDerivatives(const EvalContext& c,
                            Eigen::Ref<Vec> dxc) override {
        double first = scalarInput(c);
        for (int i = 0; i < order_; ++i) first -= a_[i + 1] * c.xc[i];
        dxc[0] = first;
        for (int i = 1; i < order_; ++i) dxc[i] = c.xc[i - 1];
    }

private:
    static double scalarInput(const EvalContext& c) {
        const Vec& u = c.in(0);
        return u.size() > 0 ? u[0] : 0.0;
    }

    std::vector<double> a_, b_;
    Vec c_;
    double d_ = 0.0;
    int order_ = 1;
};

class StateSpaceBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        const int states = params().integer("states", 1);
        const int inputs = params().integer("inputs", 1);
        const int outputs = params().integer("outputs", 1);
        if (states < 1 || inputs < 1 || outputs < 1)
            throw ModelError("the state, input and output counts must all be "
                             "at least 1");

        a_ = reshape("A", states, states);
        b_ = reshape("B", states, inputs);
        c_ = reshape("C", outputs, states);
        d_ = reshape("D", outputs, inputs);

        const std::vector<double> initial =
            params().vector("initialCondition", {0.0});
        initial_ = broadcast(initial, states, "the initial condition");

        if (s.inputConnected[0] && s.inputWidths[0] != inputs)
            throw ModelError("the input signal is " +
                             std::to_string(s.inputWidths[0]) +
                             " wide but B expects " + std::to_string(inputs));

        s.outputWidths[0] = outputs;
        s.continuousStates = states;
        s.directFeedthrough[0] = !d_.isZero(0.0);
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xd;
        xc = initial_;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c_ * c.x() + d_ * sizedInput(c);
    }

    void computeDerivatives(const EvalContext& c,
                            Eigen::Ref<Vec> dxc) override {
        dxc = a_ * c.x() + b_ * sizedInput(c);
    }

private:
    Vec sizedInput(const EvalContext& c) const {
        const Vec& u = c.in(0);
        if (u.size() == b_.cols()) return u;
        Vec sized = Vec::Zero(b_.cols());
        for (Eigen::Index i = 0; i < std::min(u.size(), b_.cols()); ++i)
            sized[i] = u[i];
        return sized;
    }

    Mat reshape(const std::string& key, int rows, int cols) const {
        const std::vector<double> values = params().vector(key, {});
        const int expected = rows * cols;
        Mat matrix = Mat::Zero(rows, cols);
        if (values.empty()) return matrix;
        if (values.size() == 1) {
            matrix.setConstant(values[0]);
            return matrix;
        }
        if (static_cast<int>(values.size()) != expected)
            throw ModelError("matrix " + key + " needs " +
                             std::to_string(expected) + " values (" +
                             std::to_string(rows) + "x" + std::to_string(cols) +
                             ") but got " + std::to_string(values.size()));
        for (int r = 0; r < rows; ++r)
            for (int col = 0; col < cols; ++col)
                matrix(r, col) = values[static_cast<std::size_t>(r) * cols + col];
        return matrix;
    }

    Mat a_, b_, c_, d_;
    Vec initial_;
};

class PidBlock : public Block {
public:
    PortLayout ports() const override { return {{"e"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        p_ = params().real("P", 1.0);
        i_ = params().real("I", 0.0);
        d_ = params().real("D", 0.0);
        n_ = params().real("N", 100.0);
        clamp_ = params().boolean("limitOutput", false);
        upper_ = params().real("upperLimit", 1.0);
        lower_ = params().real("lowerLimit", -1.0);

        if (n_ <= 0.0)
            throw ModelError("the derivative filter coefficient N must be "
                             "positive");

        useIntegral_ = i_ != 0.0;
        useDerivative_ = d_ != 0.0;

        s.outputWidths[0] = 1;
        s.continuousStates = (useIntegral_ ? 1 : 0) + (useDerivative_ ? 1 : 0);
        s.directFeedthrough[0] = true;
    }

    void computeOutputs(const EvalContext& c) override {
        const double e = error(c);
        double y = p_ * e;

        int index = 0;
        if (useIntegral_) y += i_ * c.xc[index++];
        if (useDerivative_) y += d_ * n_ * (e - n_ * c.xc[index++]);

        if (clamp_) y = std::clamp(y, lower_, upper_);
        c.out(0)[0] = y;
    }

    void computeDerivatives(const EvalContext& c,
                            Eigen::Ref<Vec> dxc) override {
        const double e = error(c);
        int index = 0;
        if (useIntegral_) {
            double rate = e;
            if (clamp_) {
                const double unclamped = rawOutput(c, e);
                if ((unclamped > upper_ && e > 0.0) ||
                    (unclamped < lower_ && e < 0.0))
                    rate = 0.0;
            }
            dxc[index++] = rate;
        }
        if (useDerivative_) {
            const double w = c.xc[index];
            dxc[index++] = e - n_ * w;
        }
    }

private:
    double error(const EvalContext& c) const {
        const Vec& u = c.in(0);
        return u.size() > 0 ? u[0] : 0.0;
    }

    double rawOutput(const EvalContext& c, double e) const {
        double y = p_ * e;
        int index = 0;
        if (useIntegral_) y += i_ * c.xc[index++];
        if (useDerivative_) y += d_ * n_ * (e - n_ * c.xc[index++]);
        return y;
    }

    double p_ = 1.0, i_ = 0.0, d_ = 0.0, n_ = 100.0;
    double upper_ = 1.0, lower_ = -1.0;
    bool clamp_ = false, useIntegral_ = false, useDerivative_ = false;
};

}

void registerContinuousBlocks() {
    registerBlockType<IntegratorBlock>(
        "Integrator", "Continuous", "Integrates its input over time (1/s).",
        {vectorParam("initialCondition", "Initial condition", {0.0}),
         boolParam("limitOutput", "Limit output", false),
         realParam("upperLimit", "Upper limit", 1.0),
         realParam("lowerLimit", "Lower limit", -1.0)},
        70.0, 60.0);

    registerBlockType<DerivativeBlock>(
        "Derivative", "Continuous",
        "Finite-difference derivative between successive steps (du/dt).", {},
        70.0, 60.0);

    registerBlockType<TransferFcnBlock>(
        "TransferFcn", "Continuous",
        "A linear transfer function in the Laplace domain.",
        {vectorParam("numerator", "Numerator coefficients", {1.0},
                     "Descending powers of s, e.g. \"1\" for 1, \"1, 0\" for s."),
         vectorParam("denominator", "Denominator coefficients", {1.0, 1.0},
                     "Descending powers of s, e.g. \"1, 2, 1\" for "
                     "s^2 + 2s + 1.")},
        110.0, 60.0);

    registerBlockType<StateSpaceBlock>(
        "StateSpace", "Continuous", "dx/dt = A x + B u,  y = C x + D u.",
        {intParam("states", "Number of states", 1),
         intParam("inputs", "Number of inputs", 1),
         intParam("outputs", "Number of outputs", 1),
         vectorParam("A", "A matrix (row-major)", {-1.0}),
         vectorParam("B", "B matrix (row-major)", {1.0}),
         vectorParam("C", "C matrix (row-major)", {1.0}),
         vectorParam("D", "D matrix (row-major)", {0.0}),
         vectorParam("initialCondition", "Initial state", {0.0})},
        110.0, 70.0);

    registerBlockType<PidBlock>(
        "PID", "Continuous",
        "Parallel PID controller with a filtered derivative term.",
        {realParam("P", "Proportional gain", 1.0),
         realParam("I", "Integral gain", 0.0),
         realParam("D", "Derivative gain", 0.0),
         realParam("N", "Derivative filter coefficient", 100.0),
         boolParam("limitOutput", "Limit output (with anti-windup)", false),
         realParam("upperLimit", "Upper limit", 1.0),
         realParam("lowerLimit", "Lower limit", -1.0)},
        80.0, 60.0);
}

}
