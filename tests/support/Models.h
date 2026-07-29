#pragma once

#include "engine/Simulator.h"
#include "model/Model.h"

namespace harness {

/// Runs a model to completion and returns the last value of the first logged
/// channel. `out` yields the simulator for tests that need its counters.
double runToEnd(simupy::Model& model, simupy::Simulator** out = nullptr);

/// Runs a model against the wall clock, the way the application and the
/// command line do. `Simulator::run()` goes flat out — real-time pacing lives
/// in the callers — so a model talking to a device that streams on its own
/// clock has to be stepped this way or it finishes before the first reading.
double runPaced(simupy::Model& model, simupy::Simulator** out = nullptr);

/// gain * u + offset, wrapped in a subsystem. Used by the subsystem tests and
/// again as the body of a saved library block.
simupy::Block* addAffineSubsystem(simupy::Model& model, double x, double y,
                                  double gain, double offset);

}  // namespace harness

using harness::addAffineSubsystem;
using harness::runPaced;
using harness::runToEnd;
