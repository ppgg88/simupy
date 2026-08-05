#include "BlockUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

/// Online identification of an order-N Laplace transfer function.
///
/// Derivatives of a measured signal cannot be taken directly, so both sides of
/// A(s) y = B(s) u are passed through a state variable filter 1/(s + lambda)^N.
/// That turns the unmeasurable derivatives into filter states and leaves a
/// plain linear regression, y_f^(N) = theta' phi, whose weights are then
/// driven downhill continuously alongside the rest of the model.
class AdaptiveTransferFcnBlock : public Block {
public:
    PortLayout ports() const override { return {{"u", "d"}, {"y", "e", "w"}}; }

    void setup(BlockSetup& s) override {
        order_ = params().integer("order", 2);
        if (order_ < 1) throw ModelError("the model order must be at least 1");

        directTerm_ = params().boolean("directTerm", false);
        leastSquares_ = params().text("algorithm", "gradient") == "rls";

        cutoff_ = params().real("filterCutoff", 10.0);
        gain_ = params().real("adaptationGain", 1.0);
        forgetting_ = params().real("forgettingRate", 0.0);
        covariance_ = params().real("initialCovariance", 1e6);

        if (cutoff_ <= 0.0)
            throw ModelError("the filter cutoff must be strictly positive");
        if (gain_ <= 0.0)
            throw ModelError("the adaptation gain must be strictly positive");
        if (forgetting_ < 0.0)
            throw ModelError("the forgetting rate cannot be negative");
        if (covariance_ <= 0.0)
            throw ModelError("the initial covariance must be strictly "
                             "positive");

        // (s + lambda)^N, so lambda_[i] multiplies s^(N - i).
        lambda_.assign(order_ + 1, 1.0);
        for (int i = 1; i <= order_; ++i)
            lambda_[i] = lambda_[i - 1] * cutoff_ * (order_ - i + 1) / i;

        // The filter is run at unity DC gain instead of 1/lambda^N. Both sides
        // of the equation scale together so the weights are untouched, but the
        // regressor keeps the size of the signal rather than shrinking by
        // lambda^N — which would otherwise leave the prior outweighing the
        // measurements and the fit stranded short of the answer.
        scale_ = lambda_[order_];

        weights_ = 2 * order_ + (directTerm_ ? 1 : 0);
        initial_ = broadcast(params().vector("initialWeights", {0.0}), weights_,
                             "the initial weights");
        phi_.resize(weights_);

        s.outputWidths[0] = 1;
        s.outputWidths[1] = 1;
        s.outputWidths[2] = weights_;
        s.continuousStates = weights_ + 2 * order_ +
                             (leastSquares_ ? weights_ * weights_ : 0);
        s.directFeedthrough[0] = directTerm_;
        s.directFeedthrough[1] = true;
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xd;
        xc.setZero();
        xc.head(weights_) = initial_;
        if (leastSquares_)
            for (int i = 0; i < weights_; ++i)
                xc[covarianceOffset() + i * weights_ + i] = covariance_;
    }

    void computeOutputs(const EvalContext& c) override {
        const double d = scalar(c, 1);
        buildRegressor(c.xc, scalar(c, 0));

        // y = theta' phi + the filter's own contribution, undone by the same
        // scale, which makes the equation error and the output error the one
        // signal up to that factor.
        const double tail = filterTail(c.xc);
        const double prediction = (theta(c.xc).dot(phi_) + tail) / scale_;

        c.out(0)[0] = prediction;
        c.out(1)[0] = d - prediction;
        c.out(2) = theta(c.xc);
    }

    void computeDerivatives(const EvalContext& c,
                            Eigen::Ref<Vec> dxc) override {
        dxc.setZero();

        const double u = scalar(c, 0);
        const double d = scalar(c, 1);
        buildRegressor(c.xc, u);

        const double topOutput = scale_ * d - filterTail(c.xc);
        const double error = topOutput - theta(c.xc).dot(phi_);
        const double normal = 1.0 + phi_.squaredNorm();

        if (leastSquares_) adaptLeastSquares(c.xc, error, normal, dxc);
        else dxc.head(weights_) = (gain_ * error / normal) * phi_;

        advanceFilter(c.xc, outputFilterOffset(), topOutput, dxc);
        advanceFilter(c.xc, inputFilterOffset(), topInput(c.xc, u), dxc);

        // A fit that has run away must not drag the solver down with it.
        if (!dxc.allFinite()) dxc.setZero();
    }

private:
    static double scalar(const EvalContext& c, int port) {
        const Vec& u = c.in(port);
        return u.size() > 0 ? u[0] : 0.0;
    }

    Eigen::Map<const Vec> theta(const double* xc) const {
        return Eigen::Map<const Vec>(xc, weights_);
    }

    int outputFilterOffset() const { return weights_; }
    int inputFilterOffset() const { return weights_ + order_; }
    int covarianceOffset() const { return weights_ + 2 * order_; }

    /// The filter state holds Y_0..Y_(N-1); the top derivative Y_N is what is
    /// left of the measurement once the filter's own poles are accounted for.
    double filterTail(const double* xc) const {
        const double* y = xc + outputFilterOffset();
        double sum = 0.0;
        for (int i = 1; i <= order_; ++i) sum += lambda_[i] * y[order_ - i];
        return sum;
    }

    double topInput(const double* xc, double u) const {
        const double* uf = xc + inputFilterOffset();
        double sum = scale_ * u;
        for (int i = 1; i <= order_; ++i) sum -= lambda_[i] * uf[order_ - i];
        return sum;
    }

    /// phi = [-Y_(N-1)..-Y_0, U_N, U_(N-1)..U_0], matching
    /// theta = [a1..aN, b0, b1..bN] so that A(s) y = B(s) u.
    void buildRegressor(const double* xc, double u) {
        const double* y = xc + outputFilterOffset();
        const double* uf = xc + inputFilterOffset();

        int index = 0;
        for (int i = 0; i < order_; ++i) phi_[index++] = -y[order_ - 1 - i];
        if (directTerm_) phi_[index++] = topInput(xc, u);
        for (int i = 0; i < order_; ++i) phi_[index++] = uf[order_ - 1 - i];
    }

    /// Each filter is a chain of integrators: the state below is the integral
    /// of the one above, and the top one is fed by the signal.
    void advanceFilter(const double* xc, int offset, double top,
                       Eigen::Ref<Vec> dxc) const {
        for (int i = 0; i < order_ - 1; ++i)
            dxc[offset + i] = xc[offset + i + 1];
        dxc[offset + order_ - 1] = top;
    }

    void adaptLeastSquares(const double* xc, double error, double normal,
                           Eigen::Ref<Vec> dxc) const {
        const int offset = covarianceOffset();
        Eigen::Map<const Mat> p(xc + offset, weights_, weights_);
        const Vec pPhi = p * phi_;

        dxc.head(weights_) = (error / normal) * pPhi;

        // Forgetting with no excitation would blow the covariance up, and the
        // weights with it: hold off once the matrix has grown far enough.
        const double limit = covariance_ * weights_;
        const double rate = p.trace() < limit ? forgetting_ : 0.0;

        const Mat derivative = rate * p - (pPhi * pPhi.transpose()) / normal;
        Eigen::Map<Mat>(dxc.data() + offset, weights_, weights_) = derivative;
    }

    std::vector<double> lambda_;
    Vec initial_, phi_;
    int order_ = 2, weights_ = 4;
    double cutoff_ = 10.0, gain_ = 1.0, scale_ = 10.0;
    double forgetting_ = 0.0, covariance_ = 1000.0;
    bool directTerm_ = false, leastSquares_ = false;
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

    registerBlockType<AdaptiveTransferFcnBlock>(
        "AdaptiveTransferFcn", "Continuous",
        "Fits an order-N Laplace transfer function to a measured signal while "
        "the model runs.",
        {intParam("order", "Model order N", 2, 1, 32,
                  "Number of poles. The block adapts N denominator "
                  "coefficients and N (or N+1) numerator coefficients."),
         realParam("filterCutoff", "State filter cutoff (rad/s)", 10.0,
                   "Bandwidth of the filter that stands in for the "
                   "derivatives. Put it well above the plant's own dynamics "
                   "but below the noise you do not want amplified."),
         choiceParam("algorithm", "Adaptation", {"gradient", "rls"},
                     std::string("gradient"),
                     "gradient: normalised steepest descent, cheap and "
                     "forgiving. rls: recursive least squares, converges in "
                     "far less time but carries an N^2 covariance matrix."),
         realParam("adaptationGain", "Gradient gain", 1.0,
                   "Larger tracks faster and rings more."),
         realParam("forgettingRate", "RLS forgetting rate (1/s)", 0.0,
                   "Above 0 the fit follows a drifting plant; 0 weighs the "
                   "whole run equally."),
         realParam("initialCovariance", "RLS initial covariance", 1e6,
                   "How far the weights may move early on. Too small and the "
                   "initial guess outweighs the measurements: the fit then "
                   "stalls short of the answer instead of reaching it."),
         boolParam("directTerm", "Adapt a direct term (b0)", false,
                   "On when the plant passes its input straight through. Off "
                   "keeps the block free of direct feedthrough on u."),
         vectorParam("initialWeights", "Initial weights", {0.0},
                     "[a1..aN, b0, b1..bN], matching the w output.")},
        130.0, 90.0);

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
