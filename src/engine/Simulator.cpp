#include "Simulator.h"

#include <algorithm>
#include <cmath>

namespace simupy {
namespace {

double timeTolerance(const SolverSettings& s) {
    const double span = std::max(std::abs(s.stopTime - s.startTime), 1.0);
    return span * 1e-12;
}

}  // namespace

Simulator::Simulator(Model& model, ExpressionEvaluator evaluate)
    : model_(model), evaluate_(std::move(evaluate)) {}

Simulator::~Simulator() {
    if (initialized_ && !terminated_) {
        try {
            terminate();
        } catch (...) {
            // Never let a block's terminate() escape a destructor.
        }
    }
}

void Simulator::initialize() {
    compiled_ = Scheduler::compile(model_, evaluate_);
    settings_ = model_.solver();

    if (!settings_.unbounded && settings_.stopTime <= settings_.startTime)
        throw ModelError("stop time must be greater than start time");

    allocate();

    solver_ = OdeSolver::create(settings_);
    derivativeFn_ = [this](double t, const Vec& x, Vec& dx) {
        evaluateOutputs(t, x, false);
        evaluateDerivatives(t, x, dx);
        ++derivativeEvals_;
    };

    // Initial step size: the user's choice, otherwise a fiftieth of the
    // largest allowed step, which is conservative enough for a first attempt.
    const double maxStep = settings_.effectiveMaxStep();
    h_ = settings_.initialStep > 0.0 ? settings_.initialStep : maxStep / 50.0;
    h_ = std::clamp(h_, settings_.minStep, maxStep);
    if (settings_.method != SolverSettings::Method::RK45)
        h_ = settings_.fixedStep;

    t_ = settings_.startTime;
    majorSteps_ = 0;
    rejectedSteps_ = 0;
    derivativeEvals_ = 0;
    locatedEvents_ = 0;
    terminated_ = false;
    initialized_ = true;

    for (CompiledBlock& cb : compiled_.blocks) {
        cb.block->initialize(xc_.segment(cb.xcOffset, cb.xcCount),
                             xd_.segment(cb.xdOffset, cb.xdCount));
    }
    xdNext_ = xd_;

    evaluateOutputs(t_, xc_, true);

    zc_ = Vec::Zero(compiled_.totalZeroCrossings);
    zcTrial_ = zc_;
    zcSign_.assign(compiled_.totalZeroCrossings, 0);
    zcCrossed_.assign(compiled_.totalZeroCrossings, 0);
    evaluateZeroCrossings(zc_);
    for (std::size_t i = 0; i < zcSign_.size(); ++i)
        zcSign_[i] = static_cast<signed char>((zc_[i] > 0.0) - (zc_[i] < 0.0));

    record(t_);
}

void Simulator::allocate() {
    const std::size_t blockCount = compiled_.blocks.size();

    xc_ = Vec::Zero(compiled_.totalContinuousStates);
    xd_ = Vec::Zero(compiled_.totalDiscreteStates);
    xdNext_ = xd_;
    dxc_ = Vec::Zero(compiled_.totalContinuousStates);

    signals_.clear();
    signals_.reserve(compiled_.signalWidths.size());
    for (int width : compiled_.signalWidths)
        signals_.push_back(Vec::Zero(width));

    // Dangling inputs read from shared zero buffers, so blocks never see a
    // null pointer and unconnected ports behave as a zero signal.
    std::size_t danglingCount = 0;
    for (const CompiledBlock& cb : compiled_.blocks)
        for (int signal : cb.inputSignal)
            if (signal < 0) ++danglingCount;

    danglingBuffers_.clear();
    danglingBuffers_.reserve(danglingCount);

    inputPointers_.assign(blockCount, {});
    contexts_.assign(blockCount, EvalContext{});

    for (std::size_t i = 0; i < blockCount; ++i) {
        const CompiledBlock& cb = compiled_.blocks[i];
        std::vector<const Vec*>& pointers = inputPointers_[i];
        pointers.resize(cb.inputSignal.size());
        for (std::size_t p = 0; p < cb.inputSignal.size(); ++p) {
            const int signal = cb.inputSignal[p];
            if (signal >= 0) {
                pointers[p] = &signals_[signal];
            } else {
                danglingBuffers_.push_back(Vec::Zero(cb.fallbackWidth));
                pointers[p] = &danglingBuffers_.back();
            }
        }
    }

    for (std::size_t i = 0; i < blockCount; ++i) {
        const CompiledBlock& cb = compiled_.blocks[i];
        EvalContext& ctx = contexts_[i];
        ctx.u = inputPointers_[i].empty() ? nullptr : inputPointers_[i].data();
        ctx.nu = static_cast<int>(inputPointers_[i].size());
        ctx.y = cb.outputSignal.empty() ? nullptr : &signals_[cb.outputSignal[0]];
        ctx.ny = static_cast<int>(cb.outputSignal.size());
        ctx.nxc = cb.xcCount;
        ctx.nxd = cb.xdCount;
    }

    loopStates_.assign(compiled_.loops.size(), LoopSolverState{});

    std::vector<LogChannel> channels;
    logSources_.clear();
    for (std::size_t i = 0; i < blockCount; ++i) {
        const CompiledBlock& cb = compiled_.blocks[i];
        if (!cb.block->logsInputs()) continue;

        const PortLayout layout = cb.block->ports();
        for (std::size_t p = 0; p < cb.inputSignal.size(); ++p) {
            LogChannel channel;
            channel.blockPath = cb.path;
            channel.blockName = cb.path;
            channel.portName = p < layout.inputs.size() ? layout.inputs[p]
                                                        : "in";
            const int signal = cb.inputSignal[p];
            if (signal >= 0 &&
                signal < static_cast<int>(compiled_.signalNames.size()))
                channel.signalName = compiled_.signalNames[signal];
            channel.port = static_cast<int>(p);
            channel.width = static_cast<int>(inputPointers_[i][p]->size());
            channels.push_back(std::move(channel));
            logSources_.push_back(inputPointers_[i][p]);
        }
    }
    log_.configure(std::move(channels), settings_.maxLoggedSamples);
}

void Simulator::evaluateBlock(int index, double t, const double* xcData,
                              const double* xdData, bool majorStep) {
    const CompiledBlock& cb = compiled_.blocks[index];
    EvalContext& ctx = contexts_[index];
    ctx.t = t;
    ctx.majorStep = majorStep;
    ctx.xc = xcData ? xcData + cb.xcOffset : nullptr;
    ctx.xd = xdData ? xdData + cb.xdOffset : nullptr;
    cb.block->computeOutputs(ctx);
}

void Simulator::evaluateLoopBlocks(const AlgebraicLoop& loop, double t,
                                   const double* xcData, const double* xdData,
                                   bool majorStep) {
    for (int index : loop.blocks)
        evaluateBlock(index, t, xcData, xdData, majorStep);
}

void Simulator::gatherTears(const AlgebraicLoop& loop, Vec& out) const {
    if (out.size() != loop.unknowns) out.resize(loop.unknowns);
    Eigen::Index at = 0;
    for (int signal : loop.tearSignals) {
        const Vec& value = signals_[signal];
        out.segment(at, value.size()) = value;
        at += value.size();
    }
}

void Simulator::scatterTears(const AlgebraicLoop& loop, const Vec& values) {
    Eigen::Index at = 0;
    for (int signal : loop.tearSignals) {
        Vec& target = signals_[signal];
        target = values.segment(at, target.size());
        at += target.size();
    }
}

void Simulator::solveLoop(int loopIndex, double t, const double* xcData,
                          const double* xdData, bool majorStep) {
    const AlgebraicLoop& loop = compiled_.loops[loopIndex];
    LoopSolverState& state = loopStates_[loopIndex];
    const Eigen::Index n = loop.unknowns;

    gatherTears(loop, state.guess);
    if (!state.guess.allFinite()) state.guess.setZero();

    bool converged = false;

    double referenceScale = 0.0;

    // Every iteration runs with majorStep false: a sink or a latch must not
    // see the intermediate guesses the search passes through.
    for (int iteration = 0; iteration < settings_.maxLoopIterations;
         ++iteration) {
        scatterTears(loop, state.guess);
        evaluateLoopBlocks(loop, t, xcData, xdData, false);
        gatherTears(loop, state.value);

        if (!state.value.allFinite())
            throw ModelError(
                "the algebraic loop " + loop.description +
                " produced a non-finite value. Check for a division by zero "
                "or a gain that drives the loop out of range.");

        if (iteration == 0)
            referenceScale = std::max({1.0, state.value.lpNorm<Eigen::Infinity>(),
                                       state.guess.lpNorm<Eigen::Infinity>()});

        state.residual = state.value - state.guess;
        if (state.residual.lpNorm<Eigen::Infinity>() <=
            settings_.loopTolerance * referenceScale) {
            converged = true;
            break;
        }

        if (state.guess.lpNorm<Eigen::Infinity>() > 1e12 * referenceScale)
            throw ModelError(
                "the algebraic loop " + loop.description +
                " diverges: the iteration ran away instead of settling.\n\n"
                "Break the loop with an Integrator, a Unit Delay, or a "
                "strictly proper Transfer Function.");

        state.jacobian.resize(n, n);
        for (Eigen::Index j = 0; j < n; ++j) {
            const double delta =
                std::max(1e-7, std::abs(state.guess[j]) * 1e-7);
            state.perturbed = state.guess;
            state.perturbed[j] += delta;

            scatterTears(loop, state.perturbed);
            evaluateLoopBlocks(loop, t, xcData, xdData, false);
            gatherTears(loop, state.column);

            state.jacobian.col(j) = (state.column - state.value) / delta;
        }

        const Mat system = state.jacobian - Mat::Identity(n, n);

        Eigen::FullPivLU<Mat> decomposition(system);
        if (system.norm() <=
                1e-9 * std::max(1.0, state.jacobian.norm()) ||
            !decomposition.isInvertible())
            throw ModelError(
                "the algebraic loop " + loop.description +
                " has no unique solution — the loop gain makes it singular.\n\n"
                "A unity positive feedback around a gain of 1 is the usual "
                "cause. Break the loop with an Integrator, a Unit Delay, or a "
                "strictly proper Transfer Function.");

        state.delta = decomposition.solve(-state.residual);
        if (!state.delta.allFinite())
            throw ModelError("the algebraic loop " + loop.description +
                             " diverged.");
        state.guess += state.delta;
    }

    if (!converged)
        throw ModelError(
            "the algebraic loop " + loop.description + " did not converge at t = " +
            std::to_string(t) + " after " +
            std::to_string(settings_.maxLoopIterations) +
            " iterations.\n\nThe loop may be discontinuous or have several "
            "solutions. Breaking it with a Unit Delay makes the model "
            "explicit.");

    scatterTears(loop, state.guess);
    evaluateLoopBlocks(loop, t, xcData, xdData, majorStep);
}

void Simulator::evaluateOutputs(double t, const Vec& xc, bool majorStep) {
    const double* xcData = xc.size() > 0 ? xc.data() : nullptr;
    const double* xdData = xd_.size() > 0 ? xd_.data() : nullptr;

    for (const ExecutionStep& step : compiled_.executionOrder) {
        if (step.isLoop())
            solveLoop(step.loop, t, xcData, xdData, majorStep);
        else
            evaluateBlock(step.block, t, xcData, xdData, majorStep);
    }
}

void Simulator::evaluateDerivatives(double t, const Vec& xc, Vec& dxc) {
    if (dxc.size() != compiled_.totalContinuousStates)
        dxc.resize(compiled_.totalContinuousStates);

    const double* xcData = xc.size() > 0 ? xc.data() : nullptr;

    for (const CompiledBlock& cb : compiled_.blocks) {
        if (cb.xcCount == 0) continue;
        EvalContext& ctx = contexts_[cb.index];
        ctx.t = t;
        ctx.xc = xcData ? xcData + cb.xcOffset : nullptr;
        cb.block->computeDerivatives(ctx, dxc.segment(cb.xcOffset, cb.xcCount));
    }
}

void Simulator::evaluateZeroCrossings(Vec& zc) {
    if (compiled_.totalZeroCrossings == 0) return;
    if (zc.size() != compiled_.totalZeroCrossings)
        zc.resize(compiled_.totalZeroCrossings);

    for (const CompiledBlock& cb : compiled_.blocks) {
        if (cb.zcCount == 0) continue;
        cb.block->computeZeroCrossings(contexts_[cb.index],
                                       zc.segment(cb.zcOffset, cb.zcCount));
    }
}

bool Simulator::runDiscreteUpdates(double t) {
    if (compiled_.totalDiscreteStates == 0) return false;

    const double tolerance = timeTolerance(settings_);
    bool updated = false;
    xdNext_ = xd_;

    for (const CompiledBlock& cb : compiled_.blocks) {
        if (cb.xdCount == 0) continue;

        if (cb.sampleTime > 0.0) {
            const double elapsed = t - settings_.startTime;
            const double periods = elapsed / cb.sampleTime;
            const double nearest = std::round(periods);
            if (std::abs(periods - nearest) * cb.sampleTime > tolerance)
                continue;
        }

        EvalContext& ctx = contexts_[cb.index];
        ctx.t = t;
        ctx.majorStep = true;
        ctx.xc = xc_.size() > 0 ? xc_.data() + cb.xcOffset : nullptr;
        ctx.xd = xd_.data() + cb.xdOffset;
        cb.block->updateDiscrete(ctx, xdNext_.segment(cb.xdOffset, cb.xdCount));
        updated = true;
    }

    if (updated) xd_ = xdNext_;
    return updated;
}

void Simulator::record(double t) {
    log_.append(t, logSources_);
}

double Simulator::nextSampleHit(double t) const {
    double next = settings_.effectiveStopTime();
    const double tolerance = timeTolerance(settings_);

    for (double period : compiled_.sampleTimes) {
        const double elapsed = t - settings_.startTime;
        const double index = std::floor(elapsed / period + 1e-9) + 1.0;
        double hit = settings_.startTime + index * period;
        if (hit <= t + tolerance) hit += period;
        next = std::min(next, hit);
    }
    return next;
}

/// Performs exactly one *accepted* solver step, never overshooting `tLimit`.
///
/// One accepted step is one major step, so the log gains a sample wherever the
/// solver decided it needed one. That is what makes a variable-step plot
/// dense through fast transients and sparse through quiet stretches.
double Simulator::advanceOneStep(double tLimit) {
    const double tolerance = timeTolerance(settings_);
    const double maxStep = settings_.effectiveMaxStep();
    const bool adaptive = solver_->isAdaptive();

    int consecutiveRejections = 0;

    while (true) {
        double h = adaptive ? h_ : settings_.fixedStep;
        h = std::min(h, tLimit - t_);
        if (h <= 0.0) {
            t_ = tLimit;
            return 0.0;
        }

        const StepOutcome outcome = solver_->step(derivativeFn_, t_, xc_, h);

        if (outcome.accepted) {
            t_ += outcome.stepUsed;
            if (adaptive)
                h_ = std::clamp(outcome.nextStep, settings_.minStep, maxStep);
            // Snap when we land on the limit, so repeated additions never
            // drift off a discrete block's sample grid.
            if (std::abs(t_ - tLimit) <= tolerance) t_ = tLimit;
            return outcome.stepUsed;
        }

        ++rejectedSteps_;
        h_ = std::clamp(outcome.nextStep, settings_.minStep, maxStep);
        if (++consecutiveRejections > 50 ||
            h_ <= settings_.minStep * 1.0000001) {
            throw ModelError(
                "the solver could not reach the requested accuracy at t = " +
                std::to_string(t_) +
                " (step size fell to the minimum). The model may be stiff "
                "or contain a discontinuity — try loosening the relative "
                "tolerance or using a fixed-step solver.");
        }
    }
}

/// Bisects the accepted step to find where a zero-crossing function first
/// changes sign, and leaves the simulation standing on the event.
///
/// Each trial re-integrates from (tStart, xStart) with a shorter step. The
/// solver's first-same-as-last cache has to be dropped before every trial:
/// an accepted trial leaves the cached slope belonging to the *end* of that
/// trial, and reusing it as the next trial's first stage would silently
/// integrate the wrong problem.
bool Simulator::detectCrossings(const Vec& zc) {
    bool any = false;
    for (std::size_t i = 0; i < zcSign_.size(); ++i) {
        const int now = (zc[i] > 0.0) - (zc[i] < 0.0);
        // A latch of zero means "disarmed": either we have just handled this
        // crossing, or the function has never taken a definite sign.
        const bool hit = zcSign_[i] != 0 && now != 0 && now != zcSign_[i];
        zcCrossed_[i] = hit ? 1 : 0;
        any = any || hit;
    }
    return any;
}

/// Shrinks an accepted step onto the earliest discontinuity it crossed.
/// The state kept is the one from *before* the event: a Runge-Kutta step
/// ending on the crossing evaluates its last stages on the far side.
bool Simulator::locateEvent(double tStart, const Vec& xStart, double stepUsed) {
    if (!detectCrossings(zc_)) return false;

    const std::vector<char> triggered = zcCrossed_;

    double low = 0.0;
    double high = stepUsed;
    const double resolution =
        std::max(stepUsed * 1e-10, timeTolerance(settings_));

    preEventState_ = xStart;

    for (int iteration = 0; iteration < 60 && high - low > resolution;
         ++iteration) {
        const double middle = 0.5 * (low + high);

        xc_ = xStart;
        solver_->invalidateCache();
        // Forced: half of these trials straddle the discontinuity on purpose,
        // and the error controller would refuse exactly those.
        solver_->step(derivativeFn_, tStart, xc_, middle, /*force=*/true);

        evaluateOutputs(tStart + middle, xc_, false);
        evaluateZeroCrossings(zcTrial_);

        const bool crossedHere = detectCrossings(zcTrial_);

        if (crossedHere) {
            high = middle;
        } else {
            low = middle;
            preEventState_ = xc_;
        }
    }

    xc_ = preEventState_;
    t_ = tStart + high;
    zcCrossed_ = triggered;
    ++locatedEvents_;
    return true;
}

bool Simulator::step() {
    if (!initialized_) throw ModelError("simulator was not initialized");

    const double tolerance = timeTolerance(settings_);
    if (t_ >= settings_.effectiveStopTime() - tolerance) return false;

    const bool watchEvents = settings_.detectZeroCrossings &&
                             compiled_.totalZeroCrossings > 0;
    bool eventLocated = false;

    double tTarget =
        std::min(settings_.effectiveStopTime(), nextSampleHit(t_));
    if (compiled_.hasContinuousStates()) {
        tTarget = std::min(tTarget, t_ + settings_.effectiveMaxStep());

        const double tStart = t_;
        const Vec xStart = xc_;
        const double stepUsed = advanceOneStep(tTarget);

        if (watchEvents && stepUsed > 0.0) {
            evaluateOutputs(t_, xc_, false);
            evaluateZeroCrossings(zc_);
            eventLocated = locateEvent(tStart, xStart, stepUsed);
        }
    } else {
        if (compiled_.sampleTimes.empty())
            tTarget =
                std::min(settings_.effectiveStopTime(), t_ + settings_.fixedStep);
        t_ = tTarget;
    }

    evaluateOutputs(t_, xc_, true);

    if (runDiscreteUpdates(t_)) {
        solver_->invalidateCache();
        evaluateOutputs(t_, xc_, false);
    }

    if (watchEvents) {
        evaluateZeroCrossings(zc_);
        for (std::size_t i = 0; i < zcSign_.size(); ++i)
            zcSign_[i] = static_cast<signed char>((zc_[i] > 0.0) - (zc_[i] < 0.0));

        if (eventLocated) {
            for (std::size_t i = 0; i < zcSign_.size(); ++i)
                if (zcCrossed_[i]) zcSign_[i] = 0;

            solver_->invalidateCache();
            h_ = std::max(settings_.minStep, h_ * 0.25);
        }
    }

    record(t_);
    ++majorSteps_;

    return t_ < settings_.effectiveStopTime() - tolerance;
}

void Simulator::run(const std::function<bool()>& shouldStop) {
    if (!initialized_) initialize();
    while (step()) {
        if (shouldStop && shouldStop()) break;
    }
    terminate();
}

bool Simulator::finished() const {
    return t_ >= settings_.effectiveStopTime() - timeTolerance(settings_);
}

double Simulator::progress() const {
    if (settings_.unbounded) return 0.0;

    const double span = settings_.stopTime - settings_.startTime;
    if (span <= 0.0) return 1.0;
    return std::clamp((t_ - settings_.startTime) / span, 0.0, 1.0);
}

void Simulator::terminate() {
    if (terminated_ || !initialized_) return;
    terminated_ = true;
    for (const CompiledBlock& cb : compiled_.blocks) cb.block->terminate();
}

}  // namespace simupy
