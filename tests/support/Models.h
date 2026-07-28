#pragma once

#include "engine/Simulator.h"
#include "model/Model.h"

namespace harness {

/// Runs a model to completion and returns the last value of the first logged
/// channel. `out` yields the simulator for tests that need its counters.
double runToEnd(simupy::Model& model, simupy::Simulator** out = nullptr);

/// gain * u + offset, wrapped in a subsystem. Used by the subsystem tests and
/// again as the body of a saved library block.
simupy::Block* addAffineSubsystem(simupy::Model& model, double x, double y,
                                  double gain, double offset);

}  // namespace harness

using harness::addAffineSubsystem;
using harness::runToEnd;
