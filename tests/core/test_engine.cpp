
#include "support/Harness.h"
#include "support/Models.h"

#include "blocks/ControlBlocks.h"
#include "blocks/SinkBlocks.h"
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

void testMismatchedLogicWidthRefused() {
    beginTest("Comparing or combining incompatible widths is refused");

    const auto compiles = [](const char* type,
                             const std::vector<double>& right) {
        Model model;
        Block* a = model.addBlock("Constant", 0, 0);
        a->params().set("value", std::vector<double>{1.0, 2.0, 3.0});
        Block* b = model.addBlock("Constant", 0, 100);
        b->params().set("value", right);

        Block* combine = model.addBlock(type, 200, 0);
        Block* scope = model.addBlock("Scope", 350, 0);
        model.connect(a->id(), 0, combine->id(), 0);
        model.connect(b->id(), 0, combine->id(), 1);
        model.connect(combine->id(), 0, scope->id(), 0);
        model.solver().stopTime = 0.1;

        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
        } catch (const ModelError&) {
            return false;
        }
        return true;
    };

    check(!compiles("Relational", {4.0, 5.0}),
          "Relational refuses 3 against 2");
    check(compiles("Relational", {4.0}), "Relational still broadcasts a scalar");
    check(compiles("Relational", {4.0, 5.0, 6.0}),
          "Relational accepts matching widths");

    check(!compiles("Logic", {4.0, 5.0}), "Logic refuses 3 against 2");
    check(compiles("Logic", {4.0}), "Logic still broadcasts a scalar");
    check(compiles("Logic", {4.0, 5.0, 6.0}), "Logic accepts matching widths");

    check(!compiles("MinMax", {4.0, 5.0}), "MinMax refuses 3 against 2");
    check(compiles("MinMax", {4.0}), "MinMax still broadcasts a scalar");
    check(compiles("MinMax", {4.0, 5.0, 6.0}), "MinMax accepts matching widths");
}

void testNonFiniteStateIsReported() {
    beginTest("A state that becomes NaN stops the run instead of spreading");

    // Every comparison against NaN is false, so nothing downstream notices.
    const auto failure = [](SolverSettings::Method method) {
        Model model;
        Block* source = model.addBlock("PythonFunction", 0, 0);
        source->params().set("code", std::string(R"(
class Block:
    def output(self, t, u):
        return 0.0 if t < 0.05 else float("nan")
)"));
        Block* integrator = model.addBlock("Integrator", 150, 0);
        Block* scope = model.addBlock("Scope", 300, 0);
        model.connect(source->id(), 0, integrator->id(), 0);
        model.connect(integrator->id(), 0, scope->id(), 0);

        model.solver().method = method;
        model.solver().fixedStep = 0.01;
        model.solver().stopTime = 0.2;

        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
            simulator.run();
        } catch (const ModelError& error) {
            return std::string(error.what());
        }
        return std::string();
    };

    for (const auto& [name, method] :
         std::vector<std::pair<const char*, SolverSettings::Method>>{
             {"RK45", SolverSettings::Method::RK45},
             {"RK4", SolverSettings::Method::RK4},
             {"Euler", SolverSettings::Method::Euler},
             {"SDIRK2", SolverSettings::Method::SDIRK2}}) {
        const std::string message = failure(method);
        check(!message.empty(),
              std::string(name) + " refuses to carry a NaN forward");
        check(message.find("not a number") != std::string::npos ||
                  message.find("stopped being a number") != std::string::npos,
              std::string(name) + " says what actually went wrong");
    }
}

void testUnusableSolverSettingsAreRefused() {
    beginTest("Solver settings that cannot work are refused up front");

    const auto rejects = [](void (*spoil)(SolverSettings&)) {
        Model model;
        Block* source = model.addBlock("Constant", 0, 0);
        Block* integrator = model.addBlock("Integrator", 150, 0);
        Block* scope = model.addBlock("Scope", 300, 0);
        model.connect(source->id(), 0, integrator->id(), 0);
        model.connect(integrator->id(), 0, scope->id(), 0);
        model.solver().stopTime = 0.2;
        spoil(model.solver());

        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
            simulator.run();
        } catch (const ModelError&) {
            return true;
        }
        return false;
    };

    check(rejects([](SolverSettings& s) { s.fixedStep = 0.0; }),
          "a zero fixed step is refused");
    check(rejects([](SolverSettings& s) { s.minStep = 0.0; }),
          "a zero minimum step is refused");
    check(rejects([](SolverSettings& s) { s.relTol = 0.0; }),
          "a zero relative tolerance is refused");
    check(rejects([](SolverSettings& s) { s.absTol = -1.0; }),
          "a negative absolute tolerance is refused");
    check(rejects([](SolverSettings& s) { s.maxLoopIterations = 0; }),
          "zero loop iterations is refused");
    check(rejects([](SolverSettings& s) { s.loopTolerance = 0.0; }),
          "a zero loop tolerance is refused");

    // An inverted clamp range is UB, but the run is still meaningful.
    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    Block* integrator = model.addBlock("Integrator", 150, 0);
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(source->id(), 0, integrator->id(), 0);
    model.connect(integrator->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;
    model.solver().maxStep = 0.01;
    model.solver().minStep = 1.0;

    bool ran = false;
    try {
        Simulator simulator(model, pythonExpressionEvaluator());
        simulator.initialize();
        simulator.run();
        ran = true;
    } catch (const ModelError&) {
    }
    check(ran, "a minimum step above the maximum is pulled back, not refused");
}

void testStiffSolverHonoursItsStepSettings() {
    beginTest("SDIRK2 starts on its own step settings, not the fixed step");

    Model model;
    Block* step = model.addBlock("Step", 0, 0);
    step->params().set("stepTime", 0.0);
    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{1.0});
    plant->params().set("denominator", std::vector<double>{1.0, 1.0});
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(step->id(), 0, plant->id(), 0);
    model.connect(plant->id(), 0, scope->id(), 0);

    model.solver().method = SolverSettings::Method::SDIRK2;
    model.solver().stopTime = 1.0;
    // A wide cap, so nothing but h_ itself decides the first step.
    model.solver().maxStep = 1.0;
    model.solver().initialStep = 1e-4;
    model.solver().fixedStep = 0.5;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();
    simulator.run();

    const std::vector<double>& times = simulator.log().times();
    check(times.size() > 1, "the run logged more than its starting point");
    if (times.size() < 2) return;

    // Starting on fixedStep would attempt 0.5 s and reject its way down.
    checkClose(times[1] - times[0], 1e-4, 1e-9,
               "the first step is exactly initialStep");
    check(simulator.rejectedSteps() == 0,
          "and nothing was wasted rejecting an over-ambitious first step");

    checkClose(runToEnd(model), 1.0 - std::exp(-1.0), 1e-4,
               "and the answer is still right");
}

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

    // 1e5/(s(s+1)(s+1e5)) = 1 - (1e5/99999) e^-t + (1/99999) e^-1e5 t.
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

    // Derivative evaluations include the implicit method's Jacobian passes.
    check(implicitRun.derivativeEvaluations() * 3 <
              explicitRun.derivativeEvaluations(),
          "the implicit method is far cheaper (" +
              std::to_string(implicitRun.derivativeEvaluations()) + " vs " +
              std::to_string(explicitRun.derivativeEvaluations()) +
              " derivative evaluations)");
}

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

void testZeroCrossingAccuracy() {
    beginTest("Event location makes a switching model exact");

    auto build = [](Model& model, bool detect) {
        Block* sine = model.addBlock("Sine", 0, 0);
        sine->params().set("frequency", 2.0 * kPi);
        // A quarter-turn of phase, so the run does not start on an ambiguous zero.
        sine->params().set("phase", kPi / 2.0);
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

    // Flips at 0.25, 0.75, 1.25, 1.75, 2.25: 0.25 - 0.5 + 0.5 - 0.5 + 0.5 - 0.35.
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
        straddledError = 1.0;
    }

    check(straddledError > std::abs(locatedValue - expected) * 10.0,
          "straddling the discontinuity is markedly less accurate (error " +
              std::to_string(straddledError) + " vs " +
              std::to_string(std::abs(locatedValue - expected)) + ")");
}

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

// y(k) = 0.7 y(k-1) + 0.5 u(k-1), so the weights [a1, b1] settle at
// [-0.7, 0.5]. A white input keeps both directions excited.
static void runIdentification(const std::string& algorithm,
                              const std::string& feedback, double tolerance) {
    Model model;
    Block* noise = model.addBlock("RandomNumber", 0, 0);
    noise->params().set("sampleTime", 0.05);
    noise->params().set("seed", 7.0);

    Block* plant = model.addBlock("DiscreteTransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{0.0, 0.5});
    plant->params().set("denominator", std::vector<double>{1.0, -0.7});
    plant->params().set("sampleTime", 0.05);

    Block* fit = model.addBlock("DiscreteAdaptiveTransferFcn", 400, 0);
    fit->params().set("order", 1.0);
    fit->params().set("sampleTime", 0.05);
    fit->params().set("algorithm", algorithm);
    fit->params().set("feedback", feedback);
    fit->params().set("stepSize", 0.5);

    const std::string what = algorithm + "/" + feedback;
    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(noise->id(), 0, plant->id(), 0);
    model.connect(noise->id(), 0, fit->id(), 0);
    model.connect(plant->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 60.0;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const SignalLog& log = simulator->log();
    const LogChannel& weights = log.channels().front();
    check(weights.width == 2, what + ": the w output carries 2N weights");

    const int last = log.sampleCount() - 1;
    checkClose(weights.at(last, 0), -0.7, tolerance, what + ": a1");
    checkClose(weights.at(last, 1), 0.5, tolerance, what + ": b1");
}

void testDiscreteAdaptiveTransferFcnConverges() {
    beginTest("Adaptive transfer function recovers the plant it watches");

    runIdentification("rls", "measured", 1e-6);
    runIdentification("nlms", "measured", 5e-3);
    runIdentification("rls", "model", 1e-6);
    runIdentification("nlms", "model", 5e-3);
}

void testDiscreteAdaptiveTransferFcnDirectTerm() {
    beginTest("Adaptive transfer function picks up a same-sample response");

    Model model;
    Block* noise = model.addBlock("RandomNumber", 0, 0);
    noise->params().set("sampleTime", 0.05);
    noise->params().set("seed", 11.0);

    // y(k) = 0.6 y(k-1) + 0.4 u(k) + 0.2 u(k-1): the b0 term only shows up
    // once the block is allowed to adapt one.
    Block* plant = model.addBlock("DiscreteTransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{0.4, 0.2});
    plant->params().set("denominator", std::vector<double>{1.0, -0.6});
    plant->params().set("sampleTime", 0.05);

    Block* fit = model.addBlock("DiscreteAdaptiveTransferFcn", 400, 0);
    fit->params().set("order", 1.0);
    fit->params().set("sampleTime", 0.05);
    fit->params().set("algorithm", std::string("rls"));
    fit->params().set("directTerm", true);

    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(noise->id(), 0, plant->id(), 0);
    model.connect(noise->id(), 0, fit->id(), 0);
    model.connect(plant->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 60.0;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const LogChannel& weights = simulator->log().channels().front();
    check(weights.width == 3, "the w output carries 2N + 1 weights");

    const int last = simulator->log().sampleCount() - 1;
    checkClose(weights.at(last, 0), -0.6, 1e-6, "a1");
    checkClose(weights.at(last, 1), 0.4, 1e-6, "b0");
    checkClose(weights.at(last, 2), 0.2, 1e-6, "b1");
}

void testDiscreteAdaptiveTransferFcnFrozenWithoutExcitation() {
    beginTest("Adaptive transfer function holds still when nothing drives it");

    Model model;
    Block* silence = model.addBlock("Constant", 0, 0);
    silence->params().set("value", std::vector<double>{0.0});

    Block* fit = model.addBlock("DiscreteAdaptiveTransferFcn", 200, 0);
    fit->params().set("order", 2.0);
    fit->params().set("sampleTime", 0.05);
    fit->params().set("initialWeights", std::vector<double>{0.3, -0.1, 2.0,
                                                            1.5});

    Block* scope = model.addBlock("Scope", 400, 0);

    model.connect(silence->id(), 0, fit->id(), 0);
    model.connect(silence->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 5.0;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const LogChannel& weights = simulator->log().channels().front();
    const int last = simulator->log().sampleCount() - 1;
    const std::vector<double> expected{0.3, -0.1, 2.0, 1.5};
    for (int i = 0; i < 4; ++i)
        checkClose(weights.at(last, i), expected[i], 1e-12,
                   "weight " + std::to_string(i) + " is untouched");
}

// 2/(s + 3), so the weights [a1, b1] settle at [3, 2]. The excitation has to
// keep moving or nothing pins the parameters down.
static void runContinuousIdentification(const std::string& algorithm,
                                        double tolerance) {
    Model model;
    Block* noise = model.addBlock("RandomNumber", 0, 0);
    noise->params().set("sampleTime", 0.05);
    noise->params().set("seed", 5.0);

    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{2.0});
    plant->params().set("denominator", std::vector<double>{1.0, 3.0});

    Block* fit = model.addBlock("AdaptiveTransferFcn", 400, 0);
    fit->params().set("order", 1.0);
    fit->params().set("algorithm", algorithm);
    fit->params().set("filterCutoff", 20.0);
    fit->params().set("adaptationGain", 50.0);

    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(noise->id(), 0, plant->id(), 0);
    model.connect(noise->id(), 0, fit->id(), 0);
    model.connect(plant->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 60.0;
    model.solver().relTol = 1e-8;
    model.solver().absTol = 1e-10;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const SignalLog& log = simulator->log();
    const LogChannel& weights = log.channels().front();
    check(weights.width == 2, algorithm + ": the w output carries 2N weights");

    const int last = log.sampleCount() - 1;
    checkClose(weights.at(last, 0), 3.0, tolerance, algorithm + ": a1");
    checkClose(weights.at(last, 1), 2.0, tolerance, algorithm + ": b1");
}

void testContinuousAdaptiveTransferFcn() {
    beginTest("Continuous adaptive transfer function recovers its plant");

    runContinuousIdentification("rls", 1e-3);
    runContinuousIdentification("gradient", 1e-3);
}

void testContinuousAdaptiveSecondOrder() {
    beginTest("Continuous adaptive fit handles a second-order plant");

    Model model;
    Block* noise = model.addBlock("RandomNumber", 0, 0);
    noise->params().set("sampleTime", 0.05);
    noise->params().set("seed", 3.0);

    // 4 / (s^2 + 3s + 2), so [a1, a2, b1, b2] = [3, 2, 0, 4].
    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{4.0});
    plant->params().set("denominator", std::vector<double>{1.0, 3.0, 2.0});

    Block* fit = model.addBlock("AdaptiveTransferFcn", 400, 0);
    fit->params().set("order", 2.0);
    fit->params().set("algorithm", std::string("rls"));
    fit->params().set("filterCutoff", 20.0);

    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(noise->id(), 0, plant->id(), 0);
    model.connect(noise->id(), 0, fit->id(), 0);
    model.connect(plant->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 60.0;
    model.solver().relTol = 1e-8;
    model.solver().absTol = 1e-10;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const LogChannel& weights = simulator->log().channels().front();
    check(weights.width == 4, "the w output carries 2N weights");
    if (weights.width != 4) return;

    const int last = simulator->log().sampleCount() - 1;
    const std::vector<double> expected{3.0, 2.0, 0.0, 4.0};
    const std::vector<std::string> names{"a1", "a2", "b1", "b2"};
    for (int i = 0; i < 4; ++i)
        checkClose(weights.at(last, i), expected[i], 1e-3, names[i]);
}

void testContinuousAdaptiveTransferFcnDirectTerm() {
    beginTest("Continuous adaptive fit picks up a feedthrough term");

    Model model;
    Block* noise = model.addBlock("RandomNumber", 0, 0);
    noise->params().set("sampleTime", 0.05);
    noise->params().set("seed", 13.0);

    // (0.5 s + 4) / (s + 2): b0 = 0.5, b1 = 4, a1 = 2.
    Block* plant = model.addBlock("TransferFcn", 200, 0);
    plant->params().set("numerator", std::vector<double>{0.5, 4.0});
    plant->params().set("denominator", std::vector<double>{1.0, 2.0});

    Block* fit = model.addBlock("AdaptiveTransferFcn", 400, 0);
    fit->params().set("order", 1.0);
    fit->params().set("algorithm", std::string("rls"));
    fit->params().set("filterCutoff", 20.0);
    fit->params().set("directTerm", true);

    Block* scope = model.addBlock("Scope", 600, 0);

    model.connect(noise->id(), 0, plant->id(), 0);
    model.connect(noise->id(), 0, fit->id(), 0);
    model.connect(plant->id(), 0, fit->id(), 1);
    model.connect(fit->id(), 2, scope->id(), 0);

    model.solver().stopTime = 60.0;
    model.solver().relTol = 1e-8;
    model.solver().absTol = 1e-10;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);

    const LogChannel& weights = simulator->log().channels().front();
    check(weights.width == 3, "the w output carries 2N + 1 weights");

    const int last = simulator->log().sampleCount() - 1;
    checkClose(weights.at(last, 0), 2.0, 1e-3, "a1");
    checkClose(weights.at(last, 1), 0.5, 1e-3, "b0");
    checkClose(weights.at(last, 2), 4.0, 1e-3, "b1");
}

void testDisplayPublishesItsValue() {
    beginTest("Display publishes the value it was handed");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{3.5, -0.25});
    Block* display = model.addBlock("Display", 200, 0);
    model.connect(source->id(), 0, display->id(), 0);

    model.solver().stopTime = 0.5;

    check(isDisplay(display), "the block is recognised as a display");
    check(displayedValue(display).size() == 0,
          "nothing is shown before a run");

    runToEnd(model);

    const Vec shown = displayedValue(display);
    check(shown.size() == 2, "the display keeps the full signal width");
    if (shown.size() != 2) return;
    checkClose(shown[0], 3.5, 1e-12, "element 0");
    checkClose(shown[1], -0.25, 1e-12, "element 1");
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
    runTest(testMismatchedLogicWidthRefused);
    runTest(testNonFiniteStateIsReported);
    runTest(testUnusableSolverSettingsAreRefused);
    runTest(testStiffSolverHonoursItsStepSettings);
    runTest(testSolverOrders);
    runTest(testStiffSolver);
    runTest(testSerializationRoundTrip);
    runTest(testZeroCrossingAccuracy);
    runTest(testSaturatedIntegratorEvent);
    runTest(testSwitchEventTiming);
    runTest(testDiscreteAdaptiveTransferFcnConverges);
    runTest(testDiscreteAdaptiveTransferFcnDirectTerm);
    runTest(testContinuousAdaptiveTransferFcn);
    runTest(testContinuousAdaptiveSecondOrder);
    runTest(testContinuousAdaptiveTransferFcnDirectTerm);
    runTest(testDisplayPublishesItsValue);
    runTest(testDiscreteAdaptiveTransferFcnFrozenWithoutExcitation);
}
