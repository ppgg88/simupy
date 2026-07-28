
#include "support/Harness.h"
#include "support/Models.h"

#include "blocks/ControlBlocks.h"
#include "blocks/SubsystemBlock.h"
#include "blocks/SubsystemGrouping.h"
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

void testGroupIntoSubsystem() {
    beginTest("Grouping blocks into a subsystem preserves the model");

    Model model;
    Block* source = model.addBlock("Constant", 0, 100);
    source->params().set("value", std::vector<double>{2.0});
    Block* gain = model.addBlock("Gain", 200, 40);
    gain->params().set("gain", std::vector<double>{3.0});
    Block* sum = model.addBlock("Sum", 380, 100);
    sum->params().set("signs", std::string("++"));
    Block* scope = model.addBlock("Scope", 560, 60);
    Block* display = model.addBlock("Display", 560, 200);

    // One source into two selected blocks, and one selected block out to two.
    model.connect(source->id(), 0, gain->id(), 0);
    model.connect(source->id(), 0, sum->id(), 1);
    model.connect(gain->id(), 0, sum->id(), 0);
    model.connect(sum->id(), 0, scope->id(), 0);
    model.connect(sum->id(), 0, display->id(), 0);
    model.solver().stopTime = 0.5;

    const std::string sourceId = source->id();
    const std::string scopeId = scope->id();
    const std::string displayId = display->id();

    checkClose(runToEnd(model), 3.0 * 2.0 + 2.0, 1e-12, "before grouping");

    const GroupResult result =
        groupIntoSubsystem(model, {gain->id(), sum->id()});

    check(result.inputs == 1, "one signal entered, so one Inport");
    check(result.outputs == 1, "one signal left, so one Outport");
    check(model.blocks().size() == 4,
          "the two blocks are replaced by one on the top level");

    auto* subsystem =
        dynamic_cast<SubsystemBlock*>(model.block(result.subsystemId));
    check(subsystem != nullptr, "and the replacement is a subsystem");
    if (!subsystem) return;

    check(subsystem->ports().inputs.size() == 1 &&
              subsystem->ports().outputs.size() == 1,
          "it shows one port on each side");
    check(subsystem->contents().blocks().size() == 4,
          "the two blocks plus their two port blocks are inside");
    check(subsystem->contents().connections().size() == 4,
          "wired to the ports and to each other");

    const Connection* fed = model.incoming(result.subsystemId, 0);
    check(fed != nullptr && fed->sourceBlock == sourceId,
          "the outside source now feeds the subsystem");

    const Connection* toScope = model.incoming(scopeId, 0);
    const Connection* toDisplay = model.incoming(displayId, 0);
    check(toScope != nullptr && toScope->sourceBlock == result.subsystemId &&
              toDisplay != nullptr &&
              toDisplay->sourceBlock == result.subsystemId,
          "and both destinations are fed from its output port");

    checkClose(runToEnd(model), 3.0 * 2.0 + 2.0, 1e-12,
               "and the grouped model computes the same thing");
}

void testGroupingPortsAndRefusals() {
    beginTest("Grouped ports follow the drawing, bad selections are refused");

    Model model;
    Block* top = model.addBlock("Constant", 0, 0);
    top->params().set("value", std::vector<double>{1.0});
    Block* bottom = model.addBlock("Constant", 0, 300);
    bottom->params().set("value", std::vector<double>{2.0});

    Block* upper = model.addBlock("Gain", 200, 0);
    Block* lower = model.addBlock("Gain", 200, 300);
    Block* upperScope = model.addBlock("Scope", 400, 0);
    Block* lowerScope = model.addBlock("Scope", 400, 300);

    model.connect(top->id(), 0, upper->id(), 0);
    model.connect(bottom->id(), 0, lower->id(), 0);
    model.connect(upper->id(), 0, upperScope->id(), 0);
    model.connect(lower->id(), 0, lowerScope->id(), 0);

    const std::string topId = top->id();
    const std::string upperScopeId = upperScope->id();

    const GroupResult result =
        groupIntoSubsystem(model, {lower->id(), upper->id()});
    check(result.inputs == 2 && result.outputs == 2, "two ports each side");

    const Connection* first = model.incoming(result.subsystemId, 0);
    check(first != nullptr && first->sourceBlock == topId,
          "the higher branch takes the first input port, whatever order the "
          "selection came in");

    const Connection* out = model.incoming(upperScopeId, 0);
    check(out != nullptr && out->sourcePort == 0,
          "and leaves through the first output port");

    // Moving a port block deeper would drop a port from the enclosing block.
    auto* subsystem =
        dynamic_cast<SubsystemBlock*>(model.block(result.subsystemId));
    check(subsystem != nullptr, "the subsystem is there to look inside");
    if (!subsystem) return;

    Model& contents = subsystem->contents();
    const std::size_t before = contents.blocks().size();

    std::vector<std::string> everythingInside;
    for (const BlockPtr& block : contents.blocks())
        everythingInside.push_back(block->id());

    bool threw = false;
    try {
        groupIntoSubsystem(contents, everythingInside);
    } catch (const ModelError&) {
        threw = true;
    }
    check(threw, "grouping a port block is refused");
    check(contents.blocks().size() == before,
          "and the refusal leaves the diagram exactly as it was");
}

void testUngroupSubsystem() {
    beginTest("Breaking a subsystem open restores the diagram");

    Model model;
    Block* source = model.addBlock("Constant", 0, 100);
    source->params().set("value", std::vector<double>{2.0});
    Block* gain = model.addBlock("Gain", 200, 40);
    gain->params().set("gain", std::vector<double>{3.0});
    Block* sum = model.addBlock("Sum", 380, 100);
    sum->params().set("signs", std::string("++"));
    Block* scope = model.addBlock("Scope", 560, 60);
    Block* display = model.addBlock("Display", 560, 200);

    model.connect(source->id(), 0, gain->id(), 0);
    model.connect(source->id(), 0, sum->id(), 1);
    model.connect(gain->id(), 0, sum->id(), 0);
    model.connect(sum->id(), 0, scope->id(), 0);
    model.connect(sum->id(), 0, display->id(), 0);
    model.solver().stopTime = 0.5;

    const std::string scopeId = scope->id();
    const std::string displayId = display->id();
    const std::size_t wiresBefore = model.connections().size();

    const GroupResult grouped =
        groupIntoSubsystem(model, {gain->id(), sum->id()});
    const UngroupResult opened =
        ungroupSubsystem(model, grouped.subsystemId);

    check(opened.blockIds.size() == 2, "both blocks came back up");
    check(model.blocks().size() == 5, "and the diagram is whole again");
    check(model.block(grouped.subsystemId) == nullptr, "the box itself is gone");
    check(model.connections().size() == wiresBefore,
          "with the wires it had before, no more and no fewer");

    // The fan-out that grouping folded into one port has to fan out again.
    const Connection* toScope = model.incoming(scopeId, 0);
    const Connection* toDisplay = model.incoming(displayId, 0);
    check(toScope != nullptr && toDisplay != nullptr &&
              toScope->sourceBlock == toDisplay->sourceBlock,
          "both destinations are fed by the same block again");

    checkClose(runToEnd(model), 3.0 * 2.0 + 2.0, 1e-12,
               "and it computes what it always did");
}

void testUngroupResolvesMaskParameters() {
    beginTest("Breaking open a masked subsystem keeps its parameter values");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{4.0});
    Block* subsystem = model.addBlock("Subsystem", 200, 0);
    subsystem->params().set("k", 2.5);
    Block* scope = model.addBlock("Scope", 400, 0);

    Model& inner = dynamic_cast<SubsystemBlock*>(subsystem)->contents();
    Block* in = inner.addBlock("Inport", 0, 0);
    in->params().set("portNumber", 1.0);
    Block* gain = inner.addBlock("Gain", 150, 0);
    gain->setParamExpression("gain", "[k]");
    Block* out = inner.addBlock("Outport", 300, 0);
    out->params().set("portNumber", 1.0);
    inner.connect(in->id(), 0, gain->id(), 0);
    inner.connect(gain->id(), 0, out->id(), 0);

    model.connect(source->id(), 0, subsystem->id(), 0);
    model.connect(subsystem->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    checkClose(runToEnd(model), 2.5 * 4.0, 1e-12, "the mask drives the gain");

    // Without an evaluator the expression would leave the scope that defines it.
    bool threw = false;
    try {
        ungroupSubsystem(model, subsystem->id());
    } catch (const ModelError&) {
        threw = true;
    }
    check(threw, "refused when there is no way to work the mask out");
    check(model.block(subsystem->id()) != nullptr,
          "and the subsystem is still there");

    const UngroupResult opened = ungroupSubsystem(model, subsystem->id(),
                                                  pythonExpressionEvaluator());
    check(opened.resolvedParameters == 1, "the one expression was resolved");
    check(opened.blockIds.size() == 1, "and the gain came up on its own");
    if (opened.blockIds.size() != 1) return;

    const Block* landed = model.block(opened.blockIds.front());
    check(landed != nullptr && landed->paramExpressions().empty(),
          "carrying a value rather than an expression that no longer resolves");

    checkClose(runToEnd(model), 2.5 * 4.0, 1e-12,
               "so the model still computes the same thing");
}

void testSubsystemCycleRejected() {
    beginTest("A self-containing subsystem is rejected");

    Model model;
    Block* outer = model.addBlock("Subsystem", 0, 0);
    Model& inner = dynamic_cast<SubsystemBlock*>(outer)->contents();
    Block* nested = inner.addBlock("Subsystem", 0, 0);
    Model& deepest = dynamic_cast<SubsystemBlock*>(nested)->contents();

    // A deep chain, since a true cycle cannot be constructed.
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

    // Hits at t = 0, 0.25 ... 2.0 leave the counter at 8.
    checkClose(runToEnd(model), 8.0, 1e-12, "counter at t = 2");
}

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

void testUngroupRefusesUnusableMaskValue() {
    beginTest("Breaking open refuses a mask value the block cannot hold");

    Model model;
    Block* subsystem = model.addBlock("Subsystem", 0, 0);
    subsystem->params().set("k", std::string("fast"));

    Model& inner = dynamic_cast<SubsystemBlock*>(subsystem)->contents();
    Block* gain = inner.addBlock("Gain", 0, 0);
    gain->setParamExpression("gain", "k");
    const std::string gainId = gain->id();

    bool threw = false;
    try {
        ungroupSubsystem(model, subsystem->id(), pythonExpressionEvaluator());
    } catch (const ModelError&) {
        threw = true;
    }
    // Text cannot become a gain, and writing it in would kill the next repaint.
    check(threw, "the resolved value is refused");
    check(model.block(subsystem->id()) != nullptr,
          "and the subsystem is still there");

    const Block* untouched = inner.block(gainId);
    check(untouched != nullptr && untouched->paramExpression("gain") == "k",
          "with the expression it had, not a value it cannot read");
}

void runModelTests() {
    runTest(testSubsystemFlattening);
    runTest(testNestedSubsystems);
    runTest(testSubsystemWithState);
    runTest(testSubsystemRoundTrip);
    runTest(testSubsystemCycleRejected);
    runTest(testGroupIntoSubsystem);
    runTest(testGroupingPortsAndRefusals);
    runTest(testUngroupSubsystem);
    runTest(testUngroupResolvesMaskParameters);
    runTest(testUngroupRefusesUnusableMaskValue);
    runTest(testPythonContinuousBlock);
    runTest(testPythonVectorBlock);
    runTest(testPythonDiscreteBlock);
    runTest(testPythonErrorReporting);
}
