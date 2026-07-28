#pragma once

#include "engine/Simulator.h"
#include "model/Model.h"

namespace harness {

double runToEnd(simupy::Model& model, simupy::Simulator** out = nullptr);

double runPaced(simupy::Model& model, simupy::Simulator** out = nullptr);

simupy::Block* addAffineSubsystem(simupy::Model& model, double x, double y,
                                  double gain, double offset);

}

using harness::addAffineSubsystem;
using harness::runPaced;
using harness::runToEnd;
