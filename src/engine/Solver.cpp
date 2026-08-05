#include "Solver.h"

#include <algorithm>
#include <cmath>

namespace simupy {
namespace {

/// Flagged, so the caller can name the cause rather than blame the tolerance.
StepOutcome nonFiniteStep(double h) {
    StepOutcome outcome;
    outcome.accepted = false;
    outcome.stepUsed = 0.0;
    outcome.nextStep = h;
    outcome.nonFinite = true;
    return outcome;
}

class EulerSolver : public OdeSolver {
public:
    const char* name() const override { return "Euler"; }
    int order() const override { return 1; }

    StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                     bool force) override {
        (void)force;
        k_.resize(x.size());
        f(t, x, k_);
        next_ = x + h * k_;
        if (!next_.allFinite()) return nonFiniteStep(h);
        x = next_;
        return {true, h, h, 0.0};
    }

private:
    Vec k_, next_;
};

class HeunSolver : public OdeSolver {
public:
    const char* name() const override { return "Heun"; }
    int order() const override { return 2; }

    StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                     bool force) override {
        (void)force;
        const Eigen::Index n = x.size();
        k1_.resize(n);
        k2_.resize(n);
        tmp_.resize(n);

        f(t, x, k1_);
        tmp_ = x + h * k1_;
        f(t + h, tmp_, k2_);
        next_ = x + (h * 0.5) * (k1_ + k2_);
        if (!next_.allFinite()) return nonFiniteStep(h);
        x = next_;
        return {true, h, h, 0.0};
    }

private:
    Vec k1_, k2_, tmp_, next_;
};

class RK4Solver : public OdeSolver {
public:
    const char* name() const override { return "Runge-Kutta 4"; }
    int order() const override { return 4; }

    StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                     bool force) override {
        (void)force;
        const Eigen::Index n = x.size();
        k1_.resize(n);
        k2_.resize(n);
        k3_.resize(n);
        k4_.resize(n);
        tmp_.resize(n);

        f(t, x, k1_);
        tmp_ = x + (h * 0.5) * k1_;
        f(t + h * 0.5, tmp_, k2_);
        tmp_ = x + (h * 0.5) * k2_;
        f(t + h * 0.5, tmp_, k3_);
        tmp_ = x + h * k3_;
        f(t + h, tmp_, k4_);

        next_ = x + (h / 6.0) * (k1_ + 2.0 * k2_ + 2.0 * k3_ + k4_);
        if (!next_.allFinite()) return nonFiniteStep(h);
        x = next_;
        return {true, h, h, 0.0};
    }

private:
    Vec k1_, k2_, k3_, k4_, tmp_, next_;
};

class DormandPrince45 : public OdeSolver {
public:
    DormandPrince45(double relTol, double absTol, double minStep)
        : relTol_(relTol), absTol_(absTol), minStep_(minStep) {}

    const char* name() const override { return "Dormand-Prince 5(4)"; }
    bool isAdaptive() const override { return true; }
    int order() const override { return 5; }

    void invalidateCache() override { hasCachedSlope_ = false; }

    StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                     bool force) override {
        const Eigen::Index n = x.size();
        resize(n);

        if (hasCachedSlope_ && cachedSlope_.size() == n) {
            k1_ = cachedSlope_;
        } else {
            f(t, x, k1_);
        }

        tmp_ = x + h * (a21 * k1_);
        f(t + c2 * h, tmp_, k2_);

        tmp_ = x + h * (a31 * k1_ + a32 * k2_);
        f(t + c3 * h, tmp_, k3_);

        tmp_ = x + h * (a41 * k1_ + a42 * k2_ + a43 * k3_);
        f(t + c4 * h, tmp_, k4_);

        tmp_ = x + h * (a51 * k1_ + a52 * k2_ + a53 * k3_ + a54 * k4_);
        f(t + c5 * h, tmp_, k5_);

        tmp_ = x + h * (a61 * k1_ + a62 * k2_ + a63 * k3_ + a64 * k4_ +
                        a65 * k5_);
        f(t + h, tmp_, k6_);

        solution_ = x + h * (b1 * k1_ + b3 * k3_ + b4 * k4_ + b5 * k5_ +
                             b6 * k6_);
        f(t + h, solution_, k7_);

        error_ = h * (e1 * k1_ + e3 * k3_ + e4 * k4_ + e5 * k5_ + e6 * k6_ +
                      e7 * k7_);

        if (!solution_.allFinite()) {
            hasCachedSlope_ = false;
            return nonFiniteStep(h);
        }

        double errorNorm = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            const double scale =
                absTol_ + relTol_ * std::max(std::abs(x[i]),
                                             std::abs(solution_[i]));
            const double ratio = error_[i] / scale;
            errorNorm += ratio * ratio;
        }
        errorNorm = n > 0 ? std::sqrt(errorNorm / static_cast<double>(n)) : 0.0;

        StepOutcome outcome;
        outcome.errorNorm = errorNorm;

        constexpr double kSafety = 0.9;
        constexpr double kMinFactor = 0.2;
        constexpr double kMaxFactor = 5.0;

        // NaN compares false against everything: treat it as the worst error.
        double factor = kMaxFactor;
        if (!std::isfinite(errorNorm))
            factor = kMinFactor;
        else if (errorNorm > 1e-12)
            factor = kSafety * std::pow(errorNorm, -0.2);
        factor = std::clamp(factor, kMinFactor, kMaxFactor);

        if (force || errorNorm <= 1.0 || h <= minStep_ * 1.000001) {
            x = solution_;
            cachedSlope_ = k7_;
            hasCachedSlope_ = true;
            outcome.accepted = true;
            outcome.stepUsed = h;
            outcome.nextStep = h * factor;
        } else {
            hasCachedSlope_ = false;
            outcome.accepted = false;
            outcome.stepUsed = 0.0;
            outcome.nextStep = h * std::min(factor, 1.0);
        }
        return outcome;
    }

private:
    void resize(Eigen::Index n) {
        if (k1_.size() == n) return;
        k1_.resize(n); k2_.resize(n); k3_.resize(n); k4_.resize(n);
        k5_.resize(n); k6_.resize(n); k7_.resize(n);
        tmp_.resize(n); solution_.resize(n); error_.resize(n);
        hasCachedSlope_ = false;
    }

    static constexpr double c2 = 1.0 / 5.0;
    static constexpr double c3 = 3.0 / 10.0;
    static constexpr double c4 = 4.0 / 5.0;
    static constexpr double c5 = 8.0 / 9.0;

    static constexpr double a21 = 1.0 / 5.0;
    static constexpr double a31 = 3.0 / 40.0;
    static constexpr double a32 = 9.0 / 40.0;
    static constexpr double a41 = 44.0 / 45.0;
    static constexpr double a42 = -56.0 / 15.0;
    static constexpr double a43 = 32.0 / 9.0;
    static constexpr double a51 = 19372.0 / 6561.0;
    static constexpr double a52 = -25360.0 / 2187.0;
    static constexpr double a53 = 64448.0 / 6561.0;
    static constexpr double a54 = -212.0 / 729.0;
    static constexpr double a61 = 9017.0 / 3168.0;
    static constexpr double a62 = -355.0 / 33.0;
    static constexpr double a63 = 46732.0 / 5247.0;
    static constexpr double a64 = 49.0 / 176.0;
    static constexpr double a65 = -5103.0 / 18656.0;

    static constexpr double b1 = 35.0 / 384.0;
    static constexpr double b3 = 500.0 / 1113.0;
    static constexpr double b4 = 125.0 / 192.0;
    static constexpr double b5 = -2187.0 / 6784.0;
    static constexpr double b6 = 11.0 / 84.0;

    static constexpr double e1 = b1 - 5179.0 / 57600.0;
    static constexpr double e3 = b3 - 7571.0 / 16695.0;
    static constexpr double e4 = b4 - 393.0 / 640.0;
    static constexpr double e5 = b5 + 92097.0 / 339200.0;
    static constexpr double e6 = b6 - 187.0 / 2100.0;
    static constexpr double e7 = -1.0 / 40.0;

    double relTol_;
    double absTol_;
    double minStep_;

    Vec k1_, k2_, k3_, k4_, k5_, k6_, k7_;
    Vec tmp_, solution_, error_, cachedSlope_;
    bool hasCachedSlope_ = false;
};

/// Second order, L-stable, gamma = 1 - 1/sqrt(2).
class SDIRK2Solver : public OdeSolver {
public:
    SDIRK2Solver(double relTol, double absTol, double minStep)
        : relTol_(relTol), absTol_(absTol), minStep_(minStep) {}

    const char* name() const override { return "SDIRK2 (implicit, stiff)"; }
    bool isAdaptive() const override { return true; }
    int order() const override { return 2; }

    void invalidateCache() override { jacobianValid_ = false; }

    StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                     bool force) override {
        const Eigen::Index n = x.size();
        if (n == 0) return {true, h, h, 0.0};
        resize(n);

        if (!jacobianValid_) {
            computeJacobian(f, t, x);
            factorize(h, n);
        } else if (factoredScale_ != h * kGamma) {
            factorize(h, n);
        }

        stage1_ = x;
        if (!solveStage(f, t + kGamma * h, x, h, stage1_, k1_)) {
            computeJacobian(f, t, x);
            factorize(h, n);
            stage1_ = x;
            if (!solveStage(f, t + kGamma * h, x, h, stage1_, k1_))
                return rejection(h);
        }

        rhs2_ = x + (h * (1.0 - kGamma)) * k1_;
        stage2_ = stage1_;
        if (!solveStage(f, t + h, rhs2_, h, stage2_, k2_)) {
            computeJacobian(f, t, x);
            factorize(h, n);
            stage2_ = stage1_;
            if (!solveStage(f, t + h, rhs2_, h, stage2_, k2_))
                return rejection(h);
        }

        solution_ = stage2_;
        error_ = (h * kGamma) * (k2_ - k1_);

        if (!solution_.allFinite()) {
            jacobianValid_ = false;
            return nonFiniteStep(h);
        }

        double errorNorm = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            const double scale =
                absTol_ + relTol_ * std::max(std::abs(x[i]),
                                             std::abs(solution_[i]));
            const double ratio = error_[i] / scale;
            errorNorm += ratio * ratio;
        }
        errorNorm = std::sqrt(errorNorm / static_cast<double>(n));

        StepOutcome outcome;
        outcome.errorNorm = errorNorm;

        constexpr double kSafety = 0.9;
        double factor = 5.0;
        if (!std::isfinite(errorNorm))
            factor = 0.2;               // see the note in DormandPrince45
        else if (errorNorm > 1e-12)
            factor = kSafety * std::pow(errorNorm, -1.0 / 3.0);
        factor = std::clamp(factor, 0.2, 5.0);

        if (force || errorNorm <= 1.0 || h <= minStep_ * 1.000001) {
            x = solution_;
            if (lastIterations_ > 4) jacobianValid_ = false;
            outcome.accepted = true;
            outcome.stepUsed = h;
            outcome.nextStep = h * factor;
        } else {
            outcome.accepted = false;
            outcome.stepUsed = 0.0;
            outcome.nextStep = h * std::min(factor, 1.0);
        }
        return outcome;
    }

private:
    static constexpr double kGamma = 0.2928932188134524756;
    static constexpr int kMaxNewton = 12;

    StepOutcome rejection(double h) {
        jacobianValid_ = false;
        // A non-finite residual is a diverging model, not a step too big.
        if (stageNonFinite_) return nonFiniteStep(h);
        return {false, 0.0, std::max(h * 0.25, minStep_), 0.0};
    }

    void resize(Eigen::Index n) {
        if (stage1_.size() == n) return;
        stage1_.resize(n); stage2_.resize(n); rhs2_.resize(n);
        k1_.resize(n); k2_.resize(n);
        solution_.resize(n); error_.resize(n);
        residual_.resize(n); increment_.resize(n);
        f0_.resize(n); fPerturbed_.resize(n); perturbed_.resize(n);
        jacobianValid_ = false;
    }

    /// Weighted: one large state must not mask an error in a small one.
    double scaledNorm(const Vec& v, const Vec& reference) const {
        double sum = 0.0;
        for (Eigen::Index i = 0; i < v.size(); ++i) {
            const double scale =
                absTol_ + relTol_ * std::abs(reference[i]);
            const double ratio = v[i] / scale;
            sum += ratio * ratio;
        }
        return std::sqrt(sum / static_cast<double>(v.size()));
    }

    void computeJacobian(const DerivativeFn& f, double t, const Vec& x) {
        const Eigen::Index n = x.size();
        jacobian_.resize(n, n);
        f(t, x, f0_);

        for (Eigen::Index j = 0; j < n; ++j) {
            const double delta =
                1.49e-8 * std::max(std::abs(x[j]), 1e-5);
            perturbed_ = x;
            perturbed_[j] += delta;
            f(t, perturbed_, fPerturbed_);
            jacobian_.col(j) = (fPerturbed_ - f0_) / delta;
        }
        jacobianValid_ = true;
    }

    void factorize(double h, Eigen::Index n) {
        factoredScale_ = h * kGamma;
        decomposition_.compute(Mat::Identity(n, n) -
                               factoredScale_ * jacobian_);
    }

    bool solveStage(const DerivativeFn& f, double tStage, const Vec& rhs,
                    double h, Vec& Y, Vec& k) {
        const double scale = h * kGamma;
        stageNonFinite_ = false;

        for (int iteration = 1; iteration <= kMaxNewton; ++iteration) {
            f(tStage, Y, k);
            residual_ = Y - rhs - scale * k;
            increment_ = decomposition_.solve(-residual_);
            if (!increment_.allFinite()) {
                stageNonFinite_ = true;
                return false;
            }

            Y += increment_;
            lastIterations_ = iteration;

            if (scaledNorm(increment_, Y) < 0.1) {
                f(tStage, Y, k);
                return true;
            }
        }
        return false;
    }

    double relTol_;
    double absTol_;
    double minStep_;

    Mat jacobian_;
    Eigen::PartialPivLU<Mat> decomposition_;
    double factoredScale_ = 0.0;
    bool jacobianValid_ = false;
    bool stageNonFinite_ = false;
    int lastIterations_ = 0;

    Vec stage1_, stage2_, rhs2_, k1_, k2_;
    Vec solution_, error_, residual_, increment_;
    Vec f0_, fPerturbed_, perturbed_;
};

}

std::unique_ptr<OdeSolver> OdeSolver::create(const SolverSettings& settings) {
    switch (settings.method) {
        case SolverSettings::Method::Euler:
            return std::make_unique<EulerSolver>();
        case SolverSettings::Method::Heun:
            return std::make_unique<HeunSolver>();
        case SolverSettings::Method::RK4:
            return std::make_unique<RK4Solver>();
        case SolverSettings::Method::SDIRK2:
            return std::make_unique<SDIRK2Solver>(settings.relTol,
                                                  settings.absTol,
                                                  settings.minStep);
        case SolverSettings::Method::DiscreteOnly:
            return std::make_unique<EulerSolver>();
        case SolverSettings::Method::RK45:
            break;
    }
    return std::make_unique<DormandPrince45>(settings.relTol, settings.absTol,
                                             settings.minStep);
}

}
