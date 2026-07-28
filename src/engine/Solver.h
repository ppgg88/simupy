#pragma once

#include "model/Model.h"
#include "model/Types.h"

#include <functional>
#include <memory>

namespace simupy {

using DerivativeFn = std::function<void(double t, const Vec& x, Vec& dx)>;

struct StepOutcome {
    bool accepted = true;
    double stepUsed = 0.0;  ///< h actually applied (0 when rejected)
    double nextStep = 0.0;  ///< suggested h for the following attempt
    double errorNorm = 0.0;
};

class OdeSolver {
public:
    virtual ~OdeSolver() = default;

    virtual const char* name() const = 0;
    virtual bool isAdaptive() const { return false; }
    virtual int order() const = 0;

    /// Attempts to advance `x` from time `t` by `h`.
    ///
    /// On acceptance `x` holds the new state. On rejection `x` is left
    /// untouched and `nextStep` carries a smaller suggestion. Fixed-step
    /// methods never reject.
    ///
    /// `force` applies the step whatever the error estimate says. Event
    /// location needs it: a trial step that straddles a discontinuity gets a
    /// meaningless error estimate and would be refused, which would stall the
    /// search precisely where it has to converge.
    virtual StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                             bool force = false) = 0;

    virtual void invalidateCache() {}

    static std::unique_ptr<OdeSolver> create(const SolverSettings& settings);
};

}  // namespace simupy
