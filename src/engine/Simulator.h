#pragma once

#include "Scheduler.h"
#include "SignalLog.h"
#include "Solver.h"

#include <functional>
#include <memory>

namespace simupy {

class Simulator {
public:
    explicit Simulator(Model& model, ExpressionEvaluator evaluate = {});
    ~Simulator();

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    void initialize();

    bool step();

    void run(const std::function<bool()>& shouldStop = {});

    void terminate();

    double time() const { return t_; }
    bool finished() const;
    double progress() const;

    const SignalLog& log() const { return log_; }
    const CompiledModel& compiled() const { return compiled_; }

    long long majorSteps() const { return majorSteps_; }
    long long rejectedSteps() const { return rejectedSteps_; }
    long long derivativeEvaluations() const { return derivativeEvals_; }
    long long locatedEvents() const { return locatedEvents_; }
    const char* solverName() const {
        return solver_ ? solver_->name() : "none";
    }

    const Vec& signalValue(int signalId) const { return signals_[signalId]; }

private:
    struct LoopSolverState {
        Vec guess, value, residual, perturbed, column, delta;
        Mat jacobian;
    };

    void allocate();
    void evaluateBlock(int index, double t, const double* xcData,
                       const double* xdData, bool majorStep);
    void evaluateLoopBlocks(const AlgebraicLoop& loop, double t,
                            const double* xcData, const double* xdData,
                            bool majorStep);
    void gatherTears(const AlgebraicLoop& loop, Vec& out) const;
    void scatterTears(const AlgebraicLoop& loop, const Vec& values);
    void solveLoop(int loopIndex, double t, const double* xcData,
                   const double* xdData, bool majorStep);

    void evaluateOutputs(double t, const Vec& xc, bool majorStep);
    void evaluateDerivatives(double t, const Vec& xc, Vec& dxc);
    void evaluateZeroCrossings(Vec& zc);
    bool runDiscreteUpdates(double t);
    void record(double t);
    double nextSampleHit(double t) const;

    /// Takes one accepted solver step, never past `tLimit`. Returns the step
    /// size actually used.
    double advanceOneStep(double tLimit);

    bool detectCrossings(const Vec& zc);

    bool locateEvent(double tStart, const Vec& xStart, double stepUsed);

    Model& model_;
    ExpressionEvaluator evaluate_;
    CompiledModel compiled_;
    SolverSettings settings_;
    std::unique_ptr<OdeSolver> solver_;
    DerivativeFn derivativeFn_;

    Vec xc_, xd_, xdNext_, dxc_;

    /// Zero-crossing values at the current point, scratch for the trial
    /// points examined while bracketing, and the pre-event state kept for
    /// the accepted event step.
    Vec zc_, zcTrial_, preEventState_;

    /// Latched sign of every crossing function.
    ///
    /// Comparing against a latch rather than against the previous value is
    /// what makes an event fire exactly once. A handled crossing is disarmed
    /// by latching zero, so a state-based function that ends up sitting on
    /// its own switching surface cannot retrigger forever.
    std::vector<signed char> zcSign_;
    std::vector<char> zcCrossed_;
    std::vector<Vec> signals_;
    std::vector<Vec> danglingBuffers_;
    std::vector<std::vector<const Vec*>> inputPointers_;
    std::vector<EvalContext> contexts_;
    std::vector<LoopSolverState> loopStates_;

    SignalLog log_;
    std::vector<const Vec*> logSources_;

    double t_ = 0.0;
    double h_ = 0.0;
    long long majorSteps_ = 0;
    long long rejectedSteps_ = 0;
    long long derivativeEvals_ = 0;
    long long locatedEvents_ = 0;
    bool initialized_ = false;
    bool terminated_ = false;
};

}  // namespace simupy
