#pragma once

#include "model/Model.h"
#include "model/Types.h"

#include <functional>
#include <memory>

namespace simupy {

using DerivativeFn = std::function<void(double t, const Vec& x, Vec& dx)>;

struct StepOutcome {
    bool accepted = true;
    double stepUsed = 0.0;
    double nextStep = 0.0;
    double errorNorm = 0.0;
};

class OdeSolver {
public:
    virtual ~OdeSolver() = default;

    virtual const char* name() const = 0;
    virtual bool isAdaptive() const { return false; }
    virtual int order() const = 0;

    /// `force` applies the step whatever the error says; event location needs it.
    virtual StepOutcome step(const DerivativeFn& f, double t, Vec& x, double h,
                             bool force = false) = 0;

    virtual void invalidateCache() {}

    static std::unique_ptr<OdeSolver> create(const SolverSettings& settings);
};

}
