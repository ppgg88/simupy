// solver accuracy, algebraic loops, events, widths

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


/// A Constant feeding an Integrator must produce a perfect ramp.
void testIntegratorRamp() {
    beginTest("Integrator of a constant is a ramp");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{2.0});
    Block* integrator = model.addBlock("Integrator", 200, 0);
    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(source->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);

    model.solver().stopTime = 5.0;
    model.solver().method = SolverSettings::Method::RK45;

    checkClose(runToEnd(model), 10.0, 1e-6, "x(5) = 2 * 5");
}

/// Step response of 1/(s+1) is 1 - exp(-t).
void testFirstOrderStepResponse() {
    beginTest("Transfer function 1/(s+1) step response");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{1.0});
    plant->params().set("denominator", std::vector<double>{1.0, 1.0});
    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(step->id(), 0, plant->id(), 0);
    model.connect(plant->id(), 0, scope->id(), 0);

    model.solver().stopTime = 3.0;
    model.solver().relTol = 1e-8;
    model.solver().absTol = 1e-10;

    checkClose(runToEnd(model), 1.0 - std::exp(-3.0), 1e-6, "y(3)");
}

/// A damped oscillator exercises a second-order state vector.
void testSecondOrderOscillator() {
    beginTest("Second-order response of 1/(s^2 + 2s + 1)");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{1.0});
    plant->params().set("denominator", std::vector<double>{1.0, 2.0, 1.0});
    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(step->id(), 0, plant->id(), 0);
    model.connect(plant->id(), 0, scope->id(), 0);

    model.solver().stopTime = 4.0;
    model.solver().relTol = 1e-9;
    model.solver().absTol = 1e-11;

    // Critically damped: y(t) = 1 - (1 + t) e^-t.
    const double expected = 1.0 - (1.0 + 4.0) * std::exp(-4.0);
    checkClose(runToEnd(model), expected, 1e-7, "y(4)");
}

/// State space and transfer function forms of the same system must agree.
void testStateSpaceMatchesTransferFcn() {
    beginTest("StateSpace agrees with the equivalent TransferFcn");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);

    Block* ss = model.addBlock("StateSpace", 200, 0);
    ss->params().set("states", 1.0);
    ss->params().set("A", std::vector<double>{-2.0});
    ss->params().set("B", std::vector<double>{1.0});
    ss->params().set("C", std::vector<double>{3.0});
    ss->params().set("D", std::vector<double>{0.0});

    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(step->id(), 0, ss->id(), 0);
    model.connect(ss->id(), 0, scope->id(), 0);

    model.solver().stopTime = 2.0;
    model.solver().relTol = 1e-9;

    // 3/(s+2): y(t) = 1.5 (1 - e^-2t).
    checkClose(runToEnd(model), 1.5 * (1.0 - std::exp(-4.0)), 1e-6, "y(2)");
}

/// A stateless feedback loop has a well-defined answer; the solver has to
/// find it rather than refuse the model.
void testAlgebraicLoopSolved() {
    beginTest("Algebraic loop is solved by Newton iteration");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* sum = model.addBlock("Sum", 200, 0);
    sum->params().set("signs", std::string("+-"));
    Block* gain = model.addBlock("Gain", 400, 0);
    gain->params().set("gain", std::vector<double>{2.0});
    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(step->id(), 0, sum->id(), 0);
    model.connect(gain->id(), 0, sum->id(), 1);
    model.connect(sum->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);

    model.solver().stopTime = 1.0;

    // e = u - 2e  =>  e = u/3, and the gain output is 2u/3.
    checkClose(runToEnd(model), 2.0 / 3.0, 1e-9, "the loop settles on 2u/3");
}

/// A vector-valued loop exercises the multi-unknown path through the Jacobian.
void testAlgebraicLoopVectorValued() {
    beginTest("Algebraic loop with a vector signal");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{3.0, 6.0});
    Block* sum = model.addBlock("Sum", 200, 0);
    sum->params().set("signs", std::string("+-"));
    Block* gain = model.addBlock("Gain", 400, 0);
    gain->params().set("gain", std::vector<double>{0.5, 2.0});
    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(source->id(), 0, sum->id(), 0);
    model.connect(gain->id(), 0, sum->id(), 1);
    model.connect(sum->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);

    model.solver().stopTime = 0.5;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    simulator.run();

    const SignalLog& log = simulator.log();
    check(log.channels().size() == 1 && log.channels()[0].width == 2,
          "the logged signal is two wide");
    if (log.channels().empty() || log.channels()[0].width != 2) return;

    // Element-wise: y = k(u - y) => y = k u / (1 + k).
    const int last = log.sampleCount() - 1;
    checkClose(log.channels()[0].at(last, 0), 0.5 * 3.0 / 1.5, 1e-9, "element 0");
    checkClose(log.channels()[0].at(last, 1), 2.0 * 6.0 / 3.0, 1e-9, "element 1");
}

/// Unity positive feedback has no solution at all; that must be said plainly.
void testAlgebraicLoopSingular() {
    beginTest("A singular algebraic loop is reported clearly");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* sum = model.addBlock("Sum", 200, 0);
    sum->params().set("signs", std::string("++"));
    Block* gain = model.addBlock("Gain", 400, 0);
    gain->params().set("gain", std::vector<double>{1.0});
    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(step->id(), 0, sum->id(), 0);
    model.connect(gain->id(), 0, sum->id(), 1);
    model.connect(sum->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);

    model.solver().stopTime = 1.0;

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

    check(threw, "an unsolvable loop is rejected");
    check(message.find("algebraic loop") != std::string::npos,
          "the message names the problem");
    check(message.find("Sum") != std::string::npos ||
              message.find("Gain") != std::string::npos,
          "the message names the blocks involved");
}

/// The same loop is fine once an Integrator breaks the feedthrough chain.
void testFeedbackLoopWithIntegrator() {
    beginTest("Feedback through an Integrator settles at the setpoint");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* sum = model.addBlock("Sum", 150, 0);
    sum->params().set("signs", std::string("+-"));
    Block* gain = model.addBlock("Gain", 300, 0);
    gain->params().set("gain", std::vector<double>{5.0});
    Block* integrator = model.addBlock("Integrator", 450, 0);
    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(step->id(), 0, sum->id(), 0);
    model.connect(integrator->id(), 0, sum->id(), 1);
    model.connect(sum->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);

    model.solver().stopTime = 5.0;
    model.solver().relTol = 1e-9;

    // Closed loop 5/(s+5): y(t) = 1 - e^-5t, essentially 1 after 5 seconds.
    checkClose(runToEnd(model), 1.0 - std::exp(-25.0), 1e-7, "y(5)");
}

/// Unit delays and the discrete solver keep to the sample grid.
void testDiscreteUnitDelay() {
    beginTest("Unit delay accumulates on its sample grid");

    Model model;
    Block* constant = model.addBlock("Constant", 0, 0);
    constant->params().set("value", std::vector<double>{1.0});

    Block* sum = model.addBlock("Sum", 150, 0);
    sum->params().set("signs", std::string("++"));
    Block* delay = model.addBlock("UnitDelay", 300, 0);
    delay->params().set("sampleTime", 0.1);
    Block* scope = model.addBlock("Scope", 450, 0);

    model.connect(constant->id(), 0, sum->id(), 0);
    model.connect(delay->id(), 0, sum->id(), 1);
    model.connect(sum->id(), 0, delay->id(), 0);
    model.connect(delay->id(), 0, scope->id(), 0);

    model.solver().method = SolverSettings::Method::DiscreteOnly;
    model.solver().stopTime = 1.0;
    model.solver().fixedStep = 0.1;

    // The delay holds k after k sample hits, so it reads 10 at t = 1.0.
    checkClose(runToEnd(model), 10.0, 1e-9, "counter after 10 samples");
}

/// Vector signals flow through Mux, Gain and Demux with the right widths.
void testVectorWidthPropagation() {
    beginTest("Signal widths propagate through Mux and Demux");

    Model model;
    Block* a = model.addBlock("Constant", 0, 0);
    a->params().set("value", std::vector<double>{1.0, 2.0});
    Block* b = model.addBlock("Constant", 0, 100);
    b->params().set("value", std::vector<double>{3.0});

    Block* mux = model.addBlock("Mux", 200, 0);
    mux->params().set("inputs", 2.0);
    Block* gain = model.addBlock("Gain", 350, 0);
    gain->params().set("gain", std::vector<double>{2.0});
    Block* scope = model.addBlock("Scope", 500, 0);

    model.connect(a->id(), 0, mux->id(), 0);
    model.connect(b->id(), 0, mux->id(), 1);
    model.connect(mux->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);

    model.solver().stopTime = 0.1;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    simulator.run();

    const SignalLog& log = simulator.log();
    check(!log.channels().empty(), "the scope logged a channel");
    if (log.channels().empty()) return;

    const LogChannel& channel = log.channels().front();
    check(channel.width == 3, "the muxed signal is 3 wide");
    const int last = log.sampleCount() - 1;
    if (channel.width == 3 && last >= 0) {
        checkClose(channel.at(last, 0), 2.0, 1e-12, "element 0");
        checkClose(channel.at(last, 1), 4.0, 1e-12, "element 1");
        checkClose(channel.at(last, 2), 6.0, 1e-12, "element 2");
    }
}

/// Fixed-step methods should show their textbook order of accuracy.
void testSolverOrders() {
    beginTest("Fixed-step solvers reach their expected accuracy");

    auto errorFor = [](SolverSettings::Method method, double step) {
        Model model;
        Block* constant = model.addBlock("Constant", 0, 0);
        constant->params().set("value", std::vector<double>{1.0});
        Block* plant = model.addBlock("TransferFcn", 200, 0);
        plant->params().set("numerator", std::vector<double>{1.0});
        plant->params().set("denominator", std::vector<double>{1.0, 1.0});
        Block* scope = model.addBlock("Scope", 400, 0);
        model.connect(constant->id(), 0, plant->id(), 0);
        model.connect(plant->id(), 0, scope->id(), 0);

        model.solver().method = method;
        model.solver().fixedStep = step;
        model.solver().maxStep = step;
        model.solver().stopTime = 1.0;

        return std::abs(runToEnd(model) - (1.0 - std::exp(-1.0)));
    };

    const double eulerError = errorFor(SolverSettings::Method::Euler, 0.01);
    const double heunError = errorFor(SolverSettings::Method::Heun, 0.01);
    const double rk4Error = errorFor(SolverSettings::Method::RK4, 0.01);

    check(eulerError < 2e-3, "Euler is within 2e-3 at h = 0.01");
    check(heunError < eulerError / 100.0, "Heun is far more accurate than Euler");
    check(rk4Error < 1e-9, "RK4 is within 1e-9 at h = 0.01");
}

/// A plant with poles at -1 and -100000 is stiff: an explicit method has to
/// keep stepping at the pace of the fast mode long after it has died, purely
/// to stay stable, while an L-stable implicit one can follow the slow one.
///
/// The comparison is made at a tolerance a control engineer would actually
/// use. At very tight tolerances a fifth-order explicit method wins anyway,
/// because a second-order method needs small steps for accuracy regardless of
/// stability — stiffness has to be severe before the implicit method pays.
void testStiffSolver() {
    beginTest("Implicit solver handles a stiff plant efficiently");

    auto build = [](Model& model, SolverSettings::Method method) {
        Block* step = model.addBlock("Step", 0, 0);
        step->params().set("stepTime", 0.0);
        Block* plant = model.addBlock("TransferFcn", 200, 0);
        // 1e5 / ((s + 1)(s + 1e5)), so the DC gain is 1.
        plant->params().set("numerator", std::vector<double>{1.0e5});
        plant->params().set("denominator",
                            std::vector<double>{1.0, 100001.0, 1.0e5});
        Block* scope = model.addBlock("Scope", 400, 0);
        model.connect(step->id(), 0, plant->id(), 0);
        model.connect(plant->id(), 0, scope->id(), 0);

        model.solver().method = method;
        model.solver().stopTime = 2.0;
        model.solver().relTol = 1e-6;
        model.solver().absTol = 1e-10;
    };

    // 1e5/(s(s+1)(s+1e5)) inverts to
    //   1 - (1e5/99999) e^-t + (1/99999) e^-1e5 t.
    const double expected = 1.0 - (1.0e5 / 99999.0) * std::exp(-2.0) +
                            std::exp(-2.0e5) / 99999.0;

    Model stiff;
    build(stiff, SolverSettings::Method::SDIRK2);
    Simulator implicitRun(stiff);
    implicitRun.initialize();
    implicitRun.run();
    const double implicitValue = implicitRun.log().channels().front().at(
        implicitRun.log().sampleCount() - 1, 0);

    checkClose(implicitValue, expected, 1e-5, "SDIRK2 tracks the slow mode");

    Model explicitModel;
    build(explicitModel, SolverSettings::Method::RK45);
    Simulator explicitRun(explicitModel);
    explicitRun.initialize();
    explicitRun.run();
    const double explicitValue = explicitRun.log().channels().front().at(
        explicitRun.log().sampleCount() - 1, 0);

    checkClose(explicitValue, expected, 1e-7, "RK45 gets the same answer");

    // Derivative evaluations are the honest cost: they include the extra
    // passes the implicit method spends on Jacobians and Newton iterations.
    check(implicitRun.derivativeEvaluations() * 3 <
              explicitRun.derivativeEvaluations(),
          "the implicit method is far cheaper (" +
              std::to_string(implicitRun.derivativeEvaluations()) + " vs " +
              std::to_string(explicitRun.derivativeEvaluations()) +
              " derivative evaluations)");
}

/// Saving and reloading must preserve structure and parameters.
void testSerializationRoundTrip() {
    beginTest("Models round-trip through JSON");

    Model original;
    Block* sine = original.addBlock("Sine", 10, 20);
    sine->params().set("frequency", 3.5);
    sine->params().set("amplitude", std::vector<double>{2.0, 4.0});
    sine->setName("Drive");
    Block* scope = original.addBlock("Scope", 300, 20);
    original.connect(sine->id(), 0, scope->id(), 0);
    original.solver().stopTime = 12.5;
    original.setName("round trip");

    const std::string json = ModelSerializer::toJson(original);

    Model restored;
    ModelSerializer::fromJson(json, restored);

    check(restored.name() == "round trip", "the model name survives");
    check(restored.blocks().size() == 2, "both blocks survive");
    check(restored.connections().size() == 1, "the wire survives");
    checkClose(restored.solver().stopTime, 12.5, 0.0, "the stop time survives");

    const Block* restoredSine = restored.block(sine->id());
    check(restoredSine != nullptr, "the sine block is found by id");
    if (restoredSine) {
        check(restoredSine->name() == "Drive", "the block name survives");
        checkClose(restoredSine->params().real("frequency"), 3.5, 0.0,
                   "a scalar parameter survives");
        const std::vector<double> amplitude =
            restoredSine->params().vector("amplitude");
        check(amplitude.size() == 2, "a vector parameter keeps its length");
        if (amplitude.size() == 2)
            checkClose(amplitude[1], 4.0, 0.0, "a vector parameter keeps its "
                                               "values");
    }

    checkClose(restored.geometry(sine->id()).x, 10.0, 0.0, "geometry survives");
}

/// Integrating sign(sin(2*pi*t)) gives an exact triangle wave, so the answer
/// is known in closed form — and it is wrong unless the solver lands on each
/// sign change instead of integrating across it.
void testZeroCrossingAccuracy() {
    beginTest("Event location makes a switching model exact");

    auto build = [](Model& model, bool detect) {
        Block* sine = model.addBlock("Sine", 0, 0);
        sine->params().set("frequency", 2.0 * M_PI);
        // A quarter-turn of phase makes this a cosine, so the run does not
        // start on a zero where sign() is genuinely ambiguous.
        sine->params().set("phase", M_PI / 2.0);
        Block* sign = model.addBlock("Sign", 200, 0);
        Block* integrator = model.addBlock("Integrator", 400, 0);
        Block* scope = model.addBlock("Scope", 600, 0);

        model.connect(sine->id(), 0, sign->id(), 0);
        model.connect(sign->id(), 0, integrator->id(), 0);
        model.connect(integrator->id(), 0, scope->id(), 0);

        model.solver().stopTime = 2.6;
        model.solver().relTol = 1e-6;
        model.solver().absTol = 1e-9;
        model.solver().detectZeroCrossings = detect;
    };

    // sign(cos(2*pi*t)) flips at 0.25, 0.75, 1.25, 1.75 and 2.25, so the
    // integral is 0.25 - 0.5 + 0.5 - 0.5 + 0.5 - 0.35.
    const double expected = -0.10;

    Model located;
    build(located, true);
    Simulator withEvents(located);
    withEvents.initialize();
    withEvents.run();
    const double locatedValue =
        withEvents.log().channels().front().at(withEvents.log().sampleCount() - 1, 0);

    checkClose(locatedValue, expected, 1e-9, "integral with event location");
    check(withEvents.locatedEvents() >= 5,
          "every sign change was located (found " +
              std::to_string(withEvents.locatedEvents()) + ")");

    Model straddled;
    build(straddled, false);
    double straddledError = 0.0;
    try {
        Simulator without(straddled);
        without.initialize();
        without.run();
        straddledError = std::abs(
            without.log().channels().front().at(without.log().sampleCount() - 1, 0) -
            expected);
    } catch (const ModelError&) {
        // Without event location the step controller can fail outright,
        // which is itself the point being made.
        straddledError = 1.0;
    }

    check(straddledError > std::abs(locatedValue - expected) * 10.0,
          "straddling the discontinuity is markedly less accurate (error " +
              std::to_string(straddledError) + " vs " +
              std::to_string(std::abs(locatedValue - expected)) + ")");
}

/// A saturated integrator must stop exactly at its limit, not somewhere past.
void testSaturatedIntegratorEvent() {
    beginTest("Saturated integrator stops exactly on its limit");

    Model model;
    Block* constant = model.addBlock("Constant", 0, 0);
    constant->params().set("value", std::vector<double>{1.0});
    Block* integrator = model.addBlock("Integrator", 200, 0);
    integrator->params().set("limitOutput", true);
    integrator->params().set("upperLimit", 0.5);
    integrator->params().set("lowerLimit", -0.5);
    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(constant->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);

    model.solver().stopTime = 2.0;
    model.solver().relTol = 1e-8;

    checkClose(runToEnd(model), 0.5, 1e-9, "the state is held at the limit");
}

/// The Switch block changes which input it passes; the crossing has to be
/// found or the output jumps at an arbitrary point inside a step.
void testSwitchEventTiming() {
    beginTest("Switch changeover is located in time");

    Model model;
    Block* low = model.addBlock("Constant", 0, 0);
    low->params().set("value", std::vector<double>{1.0});
    Block* high = model.addBlock("Constant", 0, 200);
    high->params().set("value", std::vector<double>{-1.0});

    // The control ramps through the threshold at t = 1.
    Block* clock = model.addBlock("Clock", 0, 100);
    Block* offset = model.addBlock("Sum", 150, 100);
    offset->params().set("signs", std::string("+-"));
    Block* one = model.addBlock("Constant", 0, 300);
    one->params().set("value", std::vector<double>{1.0});

    Block* selector = model.addBlock("Switch", 320, 100);
    selector->params().set("threshold", 0.0);
    Block* integrator = model.addBlock("Integrator", 480, 100);
    Block* scope = model.addBlock("Scope", 640, 100);

    model.connect(clock->id(), 0, offset->id(), 0);
    model.connect(one->id(), 0, offset->id(), 1);
    model.connect(low->id(), 0, selector->id(), 0);
    model.connect(offset->id(), 0, selector->id(), 1);
    model.connect(high->id(), 0, selector->id(), 2);
    model.connect(selector->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);

    model.solver().stopTime = 3.0;
    model.solver().relTol = 1e-8;

    // -1 for t < 1, then +1: the integral is -1 + 2 = 1 at t = 3.
    checkClose(runToEnd(model), 1.0, 1e-9, "integral across the changeover");
}

void runEngineTests() {
    runTest(testIntegratorRamp);
    runTest(testFirstOrderStepResponse);
    runTest(testSecondOrderOscillator);
    runTest(testStateSpaceMatchesTransferFcn);
    runTest(testAlgebraicLoopSolved);
    runTest(testAlgebraicLoopVectorValued);
    runTest(testAlgebraicLoopSingular);
    runTest(testFeedbackLoopWithIntegrator);
    runTest(testDiscreteUnitDelay);
    runTest(testVectorWidthPropagation);
    runTest(testSolverOrders);
    runTest(testStiffSolver);
    runTest(testSerializationRoundTrip);
    runTest(testZeroCrossingAccuracy);
    runTest(testSaturatedIntegratorEvent);
    runTest(testSwitchEventTiming);
}
