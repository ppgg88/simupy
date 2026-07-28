
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

namespace {

void buildRamp(Model& model) {
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{1.0});
    Block* integrator = model.addBlock("Integrator", 200, 0);
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(source->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);
    model.solver().stopTime = 1.0;
    model.solver().maxStep = 0.01;
}

}

void testUnboundedRunIgnoresStopTime() {
    beginTest("An unbounded run does not stop at the stop time");

    Model model;
    buildRamp(model);
    model.solver().unbounded = true;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();

    check(!simulator.finished(), "a fresh unbounded run is not finished");
    checkClose(simulator.progress(), 0.0, 1e-12,
               "and reports no progress, having no end to progress towards");

    // Well past the stop time of 1 s, which a bounded run would have honoured.
    int steps = 0;
    while (simulator.time() < 5.0 && steps < 100000) {
        if (!simulator.step()) break;
        ++steps;
    }
    simulator.terminate();

    check(simulator.time() > 4.9,
          "the run carried on past its stop time (reached t = " +
              std::to_string(simulator.time()) + ")");
    check(!simulator.finished(), "and still does not consider itself finished");
}

void testUnboundedRunStopsWhenAsked() {
    beginTest("An unbounded run ends when asked to");

    Model model;
    buildRamp(model);
    model.solver().unbounded = true;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();

    // The same hook the Stop button and Ctrl+C both use.
    int polls = 0;
    simulator.run([&polls] { return ++polls >= 200; });

    check(polls >= 200, "the stop hook was polled");
    check(simulator.time() > 0.0, "the run advanced before stopping");
    check(simulator.time() < 5.0, "and stopped rather than running away");
}

void testUnboundedPicksAStepBound() {
    beginTest("An unbounded run bounds its own step");

    SolverSettings settings;
    settings.startTime = 0.0;
    settings.stopTime = 10.0;
    settings.maxStep = 0.0;

    checkClose(settings.effectiveMaxStep(), 10.0 / 200.0, 1e-12,
               "a bounded run divides its span");

    settings.unbounded = true;
    check(std::isfinite(settings.effectiveMaxStep()),
          "an unbounded run still has a finite bound");
    checkClose(settings.effectiveMaxStep(),
               SolverSettings::kUnboundedMaxStep, 1e-12,
               "and it is the documented default");

    settings.maxStep = 0.002;
    checkClose(settings.effectiveMaxStep(), 0.002, 1e-12,
               "an explicit max step is respected either way");

    check(std::isinf(settings.effectiveStopTime()),
          "and the effective stop time is infinite");
}

void testUnboundedLogStaysBounded() {
    beginTest("An unbounded run keeps its log bounded");

    Model model;
    buildRamp(model);
    model.solver().unbounded = true;
    model.solver().maxStep = 0.01;
    // A small cap, so the halving is reached quickly.
    model.solver().maxLoggedSamples = 500;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();

    int steps = 0;
    while (steps < 20000 && simulator.step()) ++steps;
    simulator.terminate();

    check(steps >= 19000, "the run took plenty of steps");
    check(simulator.log().sampleCount() <= 500,
          "yet the log never exceeded its cap (held " +
              std::to_string(simulator.log().sampleCount()) + ")");
    check(simulator.log().sampleCount() > 100,
          "while still keeping enough to plot");

    // The newest kept sample can lag the present by one decimation stride.
    const SignalLog& log = simulator.log();
    const LogChannel& channel = log.channels().front();
    const double first = channel.at(0, 0);
    const double last = channel.at(log.sampleCount() - 1, 0);

    checkClose(first, 0.0, 1e-6, "the log still starts at the beginning");
    check(last > simulator.time() * 0.98,
          "and still reaches the present (" + std::to_string(last) + " of " +
              std::to_string(simulator.time()) + ")");
    check(last <= simulator.time() + 1e-9,
          "without inventing samples from the future");
}

void testUnboundedSkipsStopTimeCheck() {
    beginTest("An unbounded run accepts a meaningless stop time");

    Model model;
    buildRamp(model);
    model.solver().startTime = 0.0;
    model.solver().stopTime = 0.0;

    bool threw = false;
    try {
        Simulator bounded(model, pythonExpressionEvaluator());
        bounded.initialize();
    } catch (const ModelError&) {
        threw = true;
    }
    check(threw, "a bounded run still rejects it");

    model.solver().unbounded = true;
    threw = false;
    try {
        Simulator simulator(model, pythonExpressionEvaluator());
        simulator.initialize();
        simulator.step();
        simulator.terminate();
    } catch (const ModelError&) {
        threw = true;
    }
    check(!threw, "an unbounded one does not care");
}

void testUnboundedRoundTrip() {
    beginTest("The unbounded flag round-trips through JSON");

    Model original;
    original.addBlock("Constant", 0, 0);
    original.solver().unbounded = true;
    original.solver().stopTime = 42.0;

    Model restored;
    ModelSerializer::fromJson(ModelSerializer::toJson(original), restored);

    check(restored.solver().unbounded, "the flag survives");
    checkClose(restored.solver().stopTime, 42.0, 1e-12,
               "and the stop time is kept, so turning it off restores the end");
}

void testControlChangesMidRun() {
    beginTest("A control changes the model while it runs");

    Model model;
    Block* slider = model.addBlock("Slider", 0, 0);
    slider->params().set("minimum", 0.0);
    slider->params().set("maximum", 10.0);
    slider->params().set("value", 2.0);

    Block* gain = model.addBlock("Gain", 200, 0);
    gain->params().set("gain", std::vector<double>{3.0});
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(slider->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);
    model.solver().stopTime = 1.0;
    model.solver().method = SolverSettings::Method::RK4;
    model.solver().fixedStep = 0.01;

    auto* interactive = dynamic_cast<InteractiveBlock*>(slider);
    check(interactive != nullptr, "a Slider is an interactive block");
    if (!interactive) return;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    checkClose(interactive->liveValue(), 2.0, 1e-12,
               "the run starts at the parameter value");

    // Move the slider mid-run, as the panel does but from this thread.
    while (simulator.step()) {
        if (simulator.time() >= 0.5 && interactive->liveValue() == 2.0)
            interactive->setLiveValue(7.0);
    }
    simulator.terminate();

    const SignalLog& log = simulator.log();
    check(log.sampleCount() > 10, "the run logged something");
    if (log.sampleCount() <= 10) return;

    const LogChannel& channel = log.channels().front();
    checkClose(channel.at(0, 0), 6.0, 1e-9, "it starts at 3 * 2");
    checkClose(channel.at(log.sampleCount() - 1, 0), 21.0, 1e-9,
               "and ends at 3 * 7, without a recompile");
}

void testToggleAndButtonValues() {
    beginTest("Toggle latches and PushButton does not");

    Model model;
    Block* toggle = model.addBlock("Toggle", 0, 0);
    toggle->params().set("offValue", -1.0);
    toggle->params().set("onValue", 4.0);
    toggle->params().set("on", false);

    Block* button = model.addBlock("PushButton", 0, 200);
    button->params().set("offValue", 0.0);
    button->params().set("onValue", 9.0);

    auto* toggleBlock = dynamic_cast<InteractiveBlock*>(toggle);
    auto* buttonBlock = dynamic_cast<InteractiveBlock*>(button);
    check(toggleBlock && buttonBlock, "both are interactive");
    if (!toggleBlock || !buttonBlock) return;

    checkClose(toggleBlock->initialValue(), -1.0, 1e-12,
               "an off toggle reads its off value");
    toggle->params().set("on", true);
    checkClose(toggleBlock->initialValue(), 4.0, 1e-12,
               "and an on one reads its on value");

    // A toggle remembers, so committing writes the state back.
    toggleBlock->commitValue(-1.0);
    check(!toggle->params().boolean("on"), "committing off stores the state");

    // A button springs back, so committing must not latch the pressed value.
    checkClose(buttonBlock->initialValue(), 0.0, 1e-12,
               "a button rests at its off value");
    buttonBlock->commitValue(9.0);
    checkClose(buttonBlock->initialValue(), 0.0, 1e-12,
               "and stays there after being committed while pressed");
}

void testControlSetBeforeRunSurvives() {
    beginTest("A control set before Run is not reset by it");

    Model model;
    Block* toggle = model.addBlock("Toggle", 0, 0);
    toggle->params().set("on", false);
    toggle->params().set("offValue", 0.0);
    toggle->params().set("onValue", 5.0);
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(toggle->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.1;

    auto* interactive = dynamic_cast<InteractiveBlock*>(toggle);
    if (!interactive) return;

    interactive->setLiveValue(5.0);
    checkClose(runToEnd(model), 5.0, 1e-12,
               "the run starts from what the control was set to");

    // The headless case, where no interface ever seeds a value.
    Model fresh;
    Block* other = fresh.addBlock("Toggle", 0, 0);
    other->params().set("on", true);
    other->params().set("offValue", 0.0);
    other->params().set("onValue", 3.0);
    Block* sink = fresh.addBlock("Scope", 300, 0);
    fresh.connect(other->id(), 0, sink->id(), 0);
    fresh.solver().stopTime = 0.1;

    checkClose(runToEnd(fresh), 3.0, 1e-12,
               "and an untouched one takes its parameter");
}

void testControlRoundTrip() {
    beginTest("Control settings round-trip through JSON");

    Model original;
    Block* slider = original.addBlock("Slider", 0, 0);
    slider->params().set("minimum", -5.0);
    slider->params().set("maximum", 5.0);
    slider->params().set("value", 1.25);
    slider->params().set("label", std::string("setpoint"));

    Model restored;
    ModelSerializer::fromJson(ModelSerializer::toJson(original), restored);

    Block* back = restored.block(slider->id());
    check(back != nullptr, "the block survives");
    if (!back) return;

    auto* interactive = dynamic_cast<InteractiveBlock*>(back);
    check(interactive != nullptr, "and is still interactive");
    if (!interactive) return;

    checkClose(interactive->initialValue(), 1.25, 1e-12, "the value survives");
    const ControlDescriptor d = interactive->descriptor();
    checkClose(d.minimum, -5.0, 1e-12, "and the range");
    check(d.label == "setpoint", "and the label");
}

void testSliderRejectsBadRange() {
    beginTest("A reversed slider range is repaired");

    Model model;
    Block* slider = model.addBlock("Slider", 0, 0);
    slider->params().set("minimum", 5.0);
    slider->params().set("maximum", 1.0);

    auto* interactive = dynamic_cast<InteractiveBlock*>(slider);
    if (!interactive) return;

    const ControlDescriptor d = interactive->descriptor();
    check(d.maximum > d.minimum, "the range is made usable");
    check(interactive->initialValue() >= d.minimum &&
              interactive->initialValue() <= d.maximum,
          "and the value sits inside it");
}

void testWirelessLinkInsideALoop() {
    beginTest("A loop closed by a wireless link is still solved");

    // y = 2 * (10 - y)  =>  y = 20/3, routed back without a wire.
    Model model;
    Block* setpoint = model.addBlock("Constant", 0, 0);
    setpoint->params().set("value", std::vector<double>{10.0});
    Block* sum = model.addBlock("Sum", 150, 0);
    sum->params().set("signs", std::string("+-"));
    Block* gain = model.addBlock("Gain", 300, 0);
    gain->params().set("gain", std::vector<double>{2.0});
    Block* send = model.addBlock("Goto", 450, 0);
    send->params().set("tag", std::string("y"));
    Block* receive = model.addBlock("From", 150, 150);
    receive->params().set("tag", std::string("y"));
    Block* scope = model.addBlock("Scope", 450, 150);

    model.connect(setpoint->id(), 0, sum->id(), 0);
    model.connect(sum->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, send->id(), 0);
    model.connect(receive->id(), 0, sum->id(), 1);
    model.connect(gain->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;

    checkClose(runToEnd(model), 20.0 / 3.0, 1e-6,
               "the loop solver sees through the wireless link");
}

void runUnboundedTests() {
    runTest(testUnboundedRunIgnoresStopTime);
    runTest(testUnboundedRunStopsWhenAsked);
    runTest(testUnboundedPicksAStepBound);
    runTest(testUnboundedLogStaysBounded);
    runTest(testUnboundedSkipsStopTimeCheck);
    runTest(testUnboundedRoundTrip);
    runTest(testControlChangesMidRun);
    runTest(testToggleAndButtonValues);
    runTest(testControlSetBeforeRunSurvives);
    runTest(testControlRoundTrip);
    runTest(testSliderRejectsBadRange);
    runTest(testWirelessLinkInsideALoop);
}
