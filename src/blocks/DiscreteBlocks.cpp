#include "BlockUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

/// Online identification of an order-N transfer function. The weights live in
/// the discrete state, so they evolve with the run and rewind with a reset.
class DiscreteAdaptiveTransferFcnBlock : public Block {
public:
    PortLayout ports() const override { return {{"u", "d"}, {"y", "e", "w"}}; }

    void setup(BlockSetup& s) override {
        order_ = params().integer("order", 2);
        if (order_ < 1) throw ModelError("the model order must be at least 1");

        directTerm_ = params().boolean("directTerm", false);
        leastSquares_ = params().text("algorithm", "nlms") == "rls";
        outputError_ = params().text("feedback", "measured") == "model";

        stepSize_ = params().real("stepSize", 0.1);
        epsilon_ = params().real("regularization", 1e-6);
        forgetting_ = params().real("forgettingFactor", 0.99);
        covariance_ = params().real("initialCovariance", 1000.0);

        if (stepSize_ <= 0.0)
            throw ModelError("the step size must be strictly positive");
        if (epsilon_ <= 0.0)
            throw ModelError("the regularisation must be strictly positive");
        if (forgetting_ <= 0.0 || forgetting_ > 1.0)
            throw ModelError("the forgetting factor must lie in (0, 1]");
        if (covariance_ <= 0.0)
            throw ModelError("the initial covariance must be strictly "
                             "positive");

        weights_ = 2 * order_ + (directTerm_ ? 1 : 0);
        initial_ = broadcast(params().vector("initialWeights", {0.0}), weights_,
                             "the initial weights");
        phi_.resize(weights_);

        s.outputWidths[0] = 1;
        s.outputWidths[1] = 1;
        s.outputWidths[2] = weights_;
        s.discreteStates =
            weights_ + 2 * order_ + (leastSquares_ ? weights_ * weights_ : 0);
        s.sampleTime = requireSampleTime(params());
        s.directFeedthrough[0] = directTerm_;
        s.directFeedthrough[1] = true;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        xd.setZero();
        xd.head(weights_) = initial_;
        if (leastSquares_)
            for (int i = 0; i < weights_; ++i)
                xd[covarianceOffset() + i * weights_ + i] = covariance_;
    }

    void computeOutputs(const EvalContext& c) override {
        buildRegressor(c.xd, scalar(c, 0));
        const double prediction = theta(c.xd).dot(phi_);

        c.out(0)[0] = prediction;
        c.out(1)[0] = scalar(c, 1) - prediction;
        c.out(2) = theta(c.xd);
    }

    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override {
        const double u = scalar(c, 0);
        const double d = scalar(c, 1);

        buildRegressor(c.xd, u);
        const Eigen::Map<const Vec> current = theta(c.xd);
        const double prediction = current.dot(phi_);
        const double error = d - prediction;

        Eigen::Ref<Vec> updated = xdNext.head(weights_);
        updated = current;
        if (leastSquares_) {
            adaptLeastSquares(c.xd, error, xdNext);
        } else {
            const double norm = epsilon_ + phi_.squaredNorm();
            updated += (stepSize_ * error / norm) * phi_;
        }

        // A diverging run must not poison the weights for good.
        if (!updated.allFinite()) updated = current;

        shiftHistory(c.xd, xdNext, u, outputError_ ? prediction : d);
    }

private:
    static double scalar(const EvalContext& c, int port) {
        const Vec& u = c.in(port);
        return u.size() > 0 ? u[0] : 0.0;
    }

    Eigen::Map<const Vec> theta(const double* xd) const {
        return Eigen::Map<const Vec>(xd, weights_);
    }

    int outputHistoryOffset() const { return weights_; }
    int inputHistoryOffset() const { return weights_ + order_; }
    int covarianceOffset() const { return weights_ + 2 * order_; }

    /// phi = [-y(k-1)..-y(k-N), u(k), u(k-1)..u(k-N)], matching
    /// theta = [a1..aN, b0, b1..bN] so that A(z) y = B(z) u.
    void buildRegressor(const double* xd, double u) {
        const double* pastOutputs = xd + outputHistoryOffset();
        const double* pastInputs = xd + inputHistoryOffset();

        int index = 0;
        for (int i = 0; i < order_; ++i) phi_[index++] = -pastOutputs[i];
        if (directTerm_) phi_[index++] = u;
        for (int i = 0; i < order_; ++i) phi_[index++] = pastInputs[i];
    }

    void adaptLeastSquares(const double* xd, double error,
                           Eigen::Ref<Vec> xdNext) {
        const int offset = covarianceOffset();
        const int entries = weights_ * weights_;
        Eigen::Map<const Mat> p(xd + offset, weights_, weights_);

        const Vec pPhi = p * phi_;
        const double denominator = forgetting_ + phi_.dot(pPhi);
        if (!(std::abs(denominator) > 1e-12)) {
            xdNext.segment(offset, entries) =
                Eigen::Map<const Vec>(xd + offset, entries);
            return;
        }

        const Vec gain = pPhi / denominator;
        xdNext.head(weights_) += gain * error;

        Mat next = (p - gain * pPhi.transpose()) / forgetting_;
        next = 0.5 * (next + next.transpose()).eval();
        if (!next.allFinite()) next = p;
        Eigen::Map<Mat>(xdNext.data() + offset, weights_, weights_) = next;
    }

    void shiftHistory(const double* xd, Eigen::Ref<Vec> xdNext, double u,
                      double y) const {
        const int outputs = outputHistoryOffset();
        const int inputs = inputHistoryOffset();
        for (int i = order_ - 1; i > 0; --i) {
            xdNext[outputs + i] = xd[outputs + i - 1];
            xdNext[inputs + i] = xd[inputs + i - 1];
        }
        xdNext[outputs] = y;
        xdNext[inputs] = u;
    }

    Vec initial_, phi_;
    int order_ = 2, weights_ = 4;
    double stepSize_ = 0.1, epsilon_ = 1e-6;
    double forgetting_ = 0.99, covariance_ = 1000.0;
    bool directTerm_ = false, leastSquares_ = false, outputError_ = false;
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

}

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

    registerBlockType<DiscreteAdaptiveTransferFcnBlock>(
        "DiscreteAdaptiveTransferFcn", "Discrete",
        "Fits an order-N transfer function to a measured signal while the "
        "model runs.",
        {intParam("order", "Model order N", 2, 1, 64,
                  "Number of poles. The block adapts N denominator "
                  "coefficients and N (or N+1) numerator coefficients."),
         realParam("sampleTime", "Sample time (s)", 0.1),
         choiceParam("algorithm", "Adaptation", {"nlms", "rls"},
                     std::string("nlms"),
                     "nlms: normalised gradient descent, cheap and forgiving. "
                     "rls: recursive least squares, converges in far fewer "
                     "samples but costs N^2 per step."),
         realParam("stepSize", "NLMS step size", 0.1,
                   "0 < mu < 2. Larger tracks faster and rattles more."),
         realParam("regularization", "NLMS regularisation", 1e-6,
                   "Keeps the normalisation finite while the input is quiet."),
         realParam("forgettingFactor", "RLS forgetting factor", 0.99,
                   "0 < lambda <= 1. Below 1 the fit tracks a drifting plant; "
                   "1 weighs every past sample equally."),
         realParam("initialCovariance", "RLS initial covariance", 1000.0,
                   "How far the weights may move early on: large means the "
                   "initial guess is not trusted."),
         boolParam("directTerm", "Adapt a direct term (b0)", false,
                   "On when the plant responds within the same sample. Off "
                   "keeps the block free of direct feedthrough on u."),
         choiceParam("feedback", "Regressor feedback", {"measured", "model"},
                     std::string("measured"),
                     "measured: the regressor uses past d, so y is a one step "
                     "ahead prediction that cannot run away. model: the "
                     "regressor uses past y, so y is a free running "
                     "simulation, closer to the final transfer function but "
                     "able to diverge."),
         vectorParam("initialWeights", "Initial weights", {0.0},
                     "[a1..aN, b0, b1..bN], matching the w output.")},
        130.0, 90.0);

    registerBlockType<IntegerDelayBlock>(
        "IntegerDelay", "Discrete", "Delays the signal by N sample periods.",
        {intParam("delay", "Delay (samples)", 1, 1, 100000),
         realParam("sampleTime", "Sample time (s)", 0.1),
         realParam("initialCondition", "Initial condition", 0.0)},
        70.0, 60.0);
}

}
