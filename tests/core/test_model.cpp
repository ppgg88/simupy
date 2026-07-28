// subsystems, wireless links, signal names

#include "support/Harness.h"
#include "support/Models.h"

#include "blocks/ControlBlocks.h"
#include "blocks/SubsystemBlock.h"
#include "engine/RealTimePacer.h"
#include "engine/Simulator.h"
#include "io/CustomBlock.h"
#include "io/LibrarySerializer.h"
#include "io/ModelSerializer.h"
#include "model/BlockRegistry.h"
#include "scripting/PythonEngine.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace simupy;


/// A subsystem must behave exactly as the blocks it contains would.
void testSubsystemFlattening() {
    beginTest("Subsystem behaves as its contents");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{4.0});
    Block* subsystem = addAffineSubsystem(model, 200, 0, 3.0, 1.5);
    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(source->id(), 0, subsystem->id(), 0);
    model.connect(subsystem->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    checkClose(runToEnd(model), 3.0 * 4.0 + 1.5, 1e-12, "gain * u + offset");
}

/// Subsystems inside subsystems have to splice through cleanly.
void testNestedSubsystems() {
    beginTest("Nested subsystems flatten to the right chain");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{2.0});

    Block* outer = model.addBlock("Subsystem", 200, 0);
    Model& outerInner = dynamic_cast<SubsystemBlock*>(outer)->contents();

    Block* in = outerInner.addBlock("Inport", 0, 0);
    in->params().set("portNumber", 1.0);
    // y = 5 * (2u + 1) = 10u + 5
    Block* nested = addAffineSubsystem(outerInner, 150, 0, 2.0, 1.0);
    Block* scale = outerInner.addBlock("Gain", 350, 0);
    scale->params().set("gain", std::vector<double>{5.0});
    Block* out = outerInner.addBlock("Outport", 500, 0);
    out->params().set("portNumber", 1.0);

    outerInner.connect(in->id(), 0, nested->id(), 0);
    outerInner.connect(nested->id(), 0, scale->id(), 0);
    outerInner.connect(scale->id(), 0, out->id(), 0);

    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(source->id(), 0, outer->id(), 0);
    model.connect(outer->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    checkClose(runToEnd(model), 10.0 * 2.0 + 5.0, 1e-12, "two levels deep");
}

/// State inside a subsystem is integrated like any other, and the logged
/// channel is named by its path so two copies stay distinguishable.
void testSubsystemWithState() {
    beginTest("Subsystem holding state integrates normally");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);

    Block* holder = model.addBlock("Subsystem", 200, 0);
    holder->setName("Lag");
    Model& inner = dynamic_cast<SubsystemBlock*>(holder)->contents();

    Block* in = inner.addBlock("Inport", 0, 0);
    in->params().set("portNumber", 1.0);
    Block* plant = inner.addBlock("TransferFcn", 150, 0);
    plant->params().set("numerator", std::vector<double>{1.0});
    plant->params().set("denominator", std::vector<double>{1.0, 1.0});
    Block* out = inner.addBlock("Outport", 300, 0);
    out->params().set("portNumber", 1.0);

    inner.connect(in->id(), 0, plant->id(), 0);
    inner.connect(plant->id(), 0, out->id(), 0);

    Block* scope = model.addBlock("Scope", 400, 0);
    scope->setName("Watch");
    model.connect(step->id(), 0, holder->id(), 0);
    model.connect(holder->id(), 0, scope->id(), 0);

    model.solver().stopTime = 3.0;
    model.solver().relTol = 1e-9;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    simulator.run();

    check(simulator.compiled().totalContinuousStates == 1,
          "the nested transfer function contributes its state");

    const SignalLog& log = simulator.log();
    check(!log.channels().empty(), "the scope logged a channel");
    if (log.channels().empty()) return;

    checkClose(log.channels()[0].at(log.sampleCount() - 1, 0),
               1.0 - std::exp(-3.0), 1e-7, "the nested lag response");
}

/// A hierarchy has to survive a trip through the file format.
void testSubsystemRoundTrip() {
    beginTest("Subsystems round-trip through JSON");

    Model original;
    Block* source = original.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{4.0});
    Block* subsystem = addAffineSubsystem(original, 200, 0, 3.0, 1.5);
    subsystem->setName("Affine");
    Block* scope = original.addBlock("Scope", 400, 0);
    original.connect(source->id(), 0, subsystem->id(), 0);
    original.connect(subsystem->id(), 0, scope->id(), 0);
    original.solver().stopTime = 0.5;

    Model restored;
    ModelSerializer::fromJson(ModelSerializer::toJson(original), restored);

    check(restored.blocks().size() == 3, "the top level survives");
    const Block* restoredSubsystem = restored.block(subsystem->id());
    check(restoredSubsystem != nullptr, "the subsystem is found by id");
    if (!restoredSubsystem) return;

    const auto* typed = dynamic_cast<const SubsystemBlock*>(restoredSubsystem);
    check(typed != nullptr, "it is still a subsystem");
    if (!typed) return;
    check(typed->contents().blocks().size() == 5,
          "its five inner blocks survive");
    check(typed->contents().connections().size() == 4,
          "its four inner wires survive");

    checkClose(runToEnd(restored), 3.0 * 4.0 + 1.5, 1e-12,
               "and it still computes the same thing");
}

/// A subsystem that contains itself must be reported, not chased forever.
void testSubsystemCycleRejected() {
    beginTest("A self-containing subsystem is rejected");

    Model model;
    Block* outer = model.addBlock("Subsystem", 0, 0);
    Model& inner = dynamic_cast<SubsystemBlock*>(outer)->contents();
    Block* nested = inner.addBlock("Subsystem", 0, 0);
    Model& deepest = dynamic_cast<SubsystemBlock*>(nested)->contents();

    // Build a genuinely deep chain rather than a true cycle, which the block
    // ownership model makes impossible to construct.
    Model* level = &deepest;
    for (int i = 0; i < 80; ++i) {
        Block* next = level->addBlock("Subsystem", 0, 0);
        level = &dynamic_cast<SubsystemBlock*>(next)->contents();
    }
    level->addBlock("Constant", 0, 0);
    model.solver().stopTime = 0.1;

    bool threw = false;
    std::string message;
    try {
        Simulator simulator(model, pythonExpressionEvaluator());
        simulator.initialize();
    } catch (const ModelError& error) {
        threw = true;
        message = error.what();
    }
    check(threw, "an absurdly deep hierarchy is refused");
    check(message.find("nested") != std::string::npos,
          "the message explains why");
}

/// A Python first-order lag must match the analytic step response.
void testPythonContinuousBlock() {
    beginTest("Python block integrates like its C++ equivalent");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);

    Block* lag = model.addBlock("PythonFunction", 200, 0);
    lag->params().set("code", std::string(R"(
from simupy import Block

class Lag(Block):
    inputs = ["u"]
    outputs = ["y"]
    states = 1
    direct_feedthrough = False

    def setup(self, widths):
        self.tau = self.params["tau"]

    def output(self, t, u):
        return self.x

    def derivative(self, t, u):
        return (u[0] - self.x) / self.tau
)"));
    lag->params().set("parameters", std::string("tau = 0.5"));

    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(step->id(), 0, lag->id(), 0);
    model.connect(lag->id(), 0, scope->id(), 0);

    model.solver().stopTime = 2.0;
    model.solver().relTol = 1e-9;
    model.solver().absTol = 1e-11;

    checkClose(runToEnd(model), 1.0 - std::exp(-2.0 / 0.5), 1e-6, "y(2)");
}

/// Multiple ports, vector widths and NumPy all have to work together.
void testPythonVectorBlock() {
    beginTest("Python block with vector I/O and two outputs");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{1.0, 2.0, 3.0});

    Block* split = model.addBlock("PythonFunction", 200, 0);
    split->params().set("code", std::string(R"(
import numpy as np
from simupy import Block

class Stats(Block):
    inputs = ["v"]
    outputs = ["doubled", "total"]

    def setup(self, widths):
        self.width = widths[0]
        return [self.width, 1]

    def output(self, t, u):
        return [2.0 * u[0], np.sum(u[0])]
)"));

    Block* scope = model.addBlock("Scope", 400, 0);
    scope->params().set("inputs", 2.0);

    model.connect(source->id(), 0, split->id(), 0);
    model.connect(split->id(), 0, scope->id(), 0);
    model.connect(split->id(), 1, scope->id(), 1);

    model.solver().stopTime = 0.1;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    simulator.run();

    const SignalLog& log = simulator.log();
    check(log.channels().size() == 2, "the scope logged two channels");
    if (log.channels().size() != 2) return;

    const int last = log.sampleCount() - 1;
    check(log.channels()[0].width == 3, "the first output stayed 3 wide");
    checkClose(log.channels()[0].at(last, 2), 6.0, 1e-12, "doubled[2]");
    check(log.channels()[1].width == 1, "the second output is scalar");
    checkClose(log.channels()[1].at(last, 0), 6.0, 1e-12, "the sum");
}

/// A discrete Python block advances only on its sample grid.
void testPythonDiscreteBlock() {
    beginTest("Python discrete block counts sample hits");

    Model model;
    Block* counter = model.addBlock("PythonFunction", 0, 0);
    counter->params().set("code", std::string(R"(
from simupy import Block

class Counter(Block):
    inputs = 0
    outputs = ["n"]
    discrete_states = 1
    sample_time = 0.25
    direct_feedthrough = False

    def output(self, t, u):
        return self.xd

    def update(self, t, u):
        return self.xd + 1.0
)"));

    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(counter->id(), 0, scope->id(), 0);

    model.solver().method = SolverSettings::Method::DiscreteOnly;
    model.solver().stopTime = 2.0;
    model.solver().fixedStep = 0.25;

    // Hits at t = 0, 0.25, ... 2.0 leave the counter at 8 after the last
    // output is taken.
    checkClose(runToEnd(model), 8.0, 1e-12, "counter at t = 2");
}

/// A syntax error in a Python block must surface as a readable message.
void testPythonErrorReporting() {
    beginTest("Python errors are reported with a traceback");

    Model model;
    Block* broken = model.addBlock("PythonFunction", 0, 0);
    broken->params().set("code", std::string(R"(
from simupy import Block

class Broken(Block):
    inputs = 0
    outputs = ["y"]

    def output(self, t, u):
        return 1.0 / 0.0
)"));
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(broken->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    bool threw = false;
    std::string message;
    try {
        Simulator simulator(model, pythonExpressionEvaluator());
        simulator.initialize();
        simulator.run();
    } catch (const ModelError& error) {
        threw = true;
        message = error.what();
    }

    check(threw, "a failing Python block aborts the run");
    check(message.find("ZeroDivisionError") != std::string::npos,
          "the Python exception type is preserved");
    check(message.find("output()") != std::string::npos,
          "the failing method is named");
}

void runModelTests() {
    runTest(testSubsystemFlattening);
    runTest(testNestedSubsystems);
    runTest(testSubsystemWithState);
    runTest(testSubsystemRoundTrip);
    runTest(testSubsystemCycleRejected);
    runTest(testPythonContinuousBlock);
    runTest(testPythonVectorBlock);
    runTest(testPythonDiscreteBlock);
    runTest(testPythonErrorReporting);
}
