// real-time pacing and interactive controls

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
#include <chrono>
#include <thread>

namespace {

/// Wall-clock milliseconds taken by `work`.
template <typename Fn>
double millisecondsFor(Fn&& work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace


/// The pacer has to actually hold a run back, and by the right amount.
///
/// Bounds are loose on the high side and tight on the low side: a busy machine
/// can always take longer, but nothing can make a paced run finish early, so
/// that is the direction where a failure means a real bug.
void testPacerHoldsTheClock() {
    beginTest("The pacer holds a run to the wall clock");

    // 0.2 s of model time at 4x should take about 50 ms.
    const double elapsed = millisecondsFor([] {
        RealTimePacer pacer(0.0, 4.0);
        for (int i = 1; i <= 10; ++i) pacer.waitUntil(i * 0.02);
    });

    check(elapsed >= 45.0,
          "0.2 s at 4x waits at least 45 ms (took " +
              std::to_string(int(elapsed)) + " ms)");
    check(elapsed < 400.0, "and does not overshoot wildly");

    // The same span at 40x should be ten times quicker.
    const double fast = millisecondsFor([] {
        RealTimePacer pacer(0.0, 40.0);
        for (int i = 1; i <= 10; ++i) pacer.waitUntil(i * 0.02);
    });
    check(fast < elapsed, "a higher factor finishes sooner");
}

/// Deadlines are absolute, so one slow step must not push the rest late.
void testPacerAbsorbsOneSlowStep() {
    beginTest("A slow step does not push the schedule out");

    RealTimePacer pacer(0.0, 10.0);  // 1 s of model time in 100 ms

    const double elapsed = millisecondsFor([&pacer] {
        // Spend the whole budget of the first four steps in one go, then ask
        // for the rest. Incremental deadlines would add that overrun to every
        // later step; absolute ones absorb it.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        for (int i = 1; i <= 10; ++i) pacer.waitUntil(i * 0.1);
    });

    check(elapsed >= 90.0, "the run still takes its full 100 ms");
    check(elapsed < 220.0,
          "and the overrun is absorbed rather than added on (took " +
              std::to_string(int(elapsed)) + " ms)");
}

/// Falling behind must be visible, not silently absorbed.
void testPacerReportsLag() {
    beginTest("Falling behind is measured and reported");

    RealTimePacer pacer(0.0, 1.0);

    // Ask for an instant that is already in the past: the model has taken
    // longer to compute than the time it represents.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double lag = pacer.waitUntil(0.001);

    check(lag > 0.0, "a missed deadline returns a positive lag");
    check(pacer.worstLag() >= lag, "and is remembered as the worst so far");
    check(pacer.heldRealTime(),
          "a small overrun is still real time — no resync yet");

    // Past the threshold, the pacer gives up on catching up and says so.
    RealTimePacer hopeless(0.0, 1.0);
    hopeless.waitUntil(-2.0 * RealTimePacer::kResyncThreshold);
    check(!hopeless.heldRealTime(), "a large overrun is reported as a failure");
    check(hopeless.resyncCount() == 1, "and the clock is restarted once");
}

/// A long wait must not make Stop unresponsive.
void testPacerCanBeAborted() {
    beginTest("A paced wait can be abandoned");

    RealTimePacer pacer(0.0, 1.0);

    // Two seconds of model time at 1x, abandoned immediately.
    const double elapsed = millisecondsFor([&pacer] {
        pacer.waitUntil(2.0, [] { return true; });
    });

    check(elapsed < 200.0,
          "an abort cuts a two-second wait short (took " +
              std::to_string(int(elapsed)) + " ms)");
}

/// Pacing must not change a single number the solver produces.
void testPacingDoesNotChangeResults() {
    beginTest("Pacing changes the speed, not the answer");

    auto build = [](Model& model) {
        Block* source = model.addBlock("Constant", 0, 0);
        source->params().set("value", std::vector<double>{1.0});
        Block* plant = model.addBlock("TransferFcn", 200, 0);
        plant->params().set("numerator", std::vector<double>{1.0});
        plant->params().set("denominator", std::vector<double>{1.0, 1.0});
        Block* scope = model.addBlock("Scope", 400, 0);
        model.connect(source->id(), 0, plant->id(), 0);
        model.connect(plant->id(), 0, scope->id(), 0);
        model.solver().stopTime = 0.2;
    };

    Model flatOut;
    build(flatOut);
    const double fast = runToEnd(flatOut);

    Model paced;
    build(paced);
    paced.solver().realTime = true;
    paced.solver().realTimeFactor = 20.0;

    Simulator simulator(paced);
    simulator.initialize();
    RealTimePacer pacer(paced.solver().startTime, paced.solver().realTimeFactor);
    while (simulator.step()) pacer.waitUntil(simulator.time());
    simulator.terminate();

    const SignalLog& log = simulator.log();
    const double slow = log.channels().front().at(log.sampleCount() - 1, 0);

    checkClose(slow, fast, 1e-12, "the paced run gives the identical answer");
}

/// The setting travels with the model, or turning it on would be a per-session
/// decision rather than a property of the model.
void testPacingRoundTrip() {
    beginTest("Real-time settings round-trip through JSON");

    Model original;
    original.addBlock("Constant", 0, 0);
    original.solver().realTime = true;
    original.solver().realTimeFactor = 0.25;

    Model restored;
    ModelSerializer::fromJson(ModelSerializer::toJson(original), restored);

    check(restored.solver().realTime, "the flag survives");
    checkClose(restored.solver().realTimeFactor, 0.25, 1e-12,
               "and so does the factor");
}

/// A block that needs a package nobody has installed must say which one, not
/// bury it in a traceback through SimuPy's own machinery.
void testMissingOptionalDependency() {
    beginTest("A missing optional package is named plainly");

    Model model;
    Block* block = model.addBlock("PythonFunction", 0, 0);
    block->params().set("code", std::string(R"(
from simupy import Block, require


class NeedsSomething(Block):
    inputs = 0
    outputs = ["y"]

    def setup(self, widths):
        self.driver = require("definitely_not_installed", "some-package",
                              "driving a widget")

    def output(self, t, u):
        return 0.0
)"));
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(block->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.1;

    std::string message;
    try {
        Simulator simulator(model, pythonExpressionEvaluator());
        simulator.initialize();
    } catch (const ModelError& error) {
        message = error.what();
    }

    check(message.find("some-package") != std::string::npos,
          "the message names the package to install");
    check(message.find("pip install") != std::string::npos,
          "and how to install it");
    check(message.find("driving a widget") != std::string::npos,
          "and what it was needed for");
}

/// A block may import a base class from simupy without becoming ambiguous.
void testImportedBaseIsNotACandidate() {
    beginTest("An imported base class is not mistaken for the block");

    Model model;
    Block* block = model.addBlock("PythonFunction", 0, 0);
    // HardwareBlock is a Block subclass sitting in the script's namespace.
    // Counting it would make every hardware block ask which class to use.
    block->params().set("code", std::string(R"(
from simupy import HardwareBlock


class OnlyOne(HardwareBlock):
    inputs = 0
    outputs = ["y"]
    direct_feedthrough = False

    def open_device(self):
        return None

    def close_device(self, device):
        pass

    def output(self, t, u):
        return 7.0
)"));
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(block->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.1;

    checkClose(runToEnd(model), 7.0, 1e-12,
               "the one class the script defines is found without help");
}

/// The lifecycle a hardware block relies on: opened once, closed exactly once,
/// however the run ended.
void testHardwareBlockLifecycle() {
    beginTest("A hardware block opens and closes exactly once");

    Model model;
    Block* block = model.addBlock("PythonFunction", 0, 0);
    block->params().set("code", std::string(R"(
import simupy
from simupy import HardwareBlock

# Parked on the simupy module, and only initialised once: the block's script
# is re-executed at the start of every run, so a plain assignment here would
# reset the counters and measure nothing.
if not hasattr(simupy, "_test_opens"):
    simupy._test_opens = 0
    simupy._test_closes = 0


class Counted(HardwareBlock):
    inputs = 0
    outputs = ["y"]
    direct_feedthrough = False

    def open_device(self):
        simupy._test_opens += 1
        return object()

    def close_device(self, device):
        simupy._test_closes += 1

    def output(self, t, u):
        # Touching the device is what opens it, exactly as a real block does
        # when it reads a port.
        assert self.device is not None
        return float(simupy._test_opens)
)"));
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(block->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;

    checkClose(runToEnd(model), 1.0, 1e-12,
               "the device is opened once for the run");

    // Once more for the next run, not twice over: a block that opened its
    // port on every setup attempt would fail the second time round.
    checkClose(runToEnd(model), 2.0, 1e-12,
               "and once again for the second run");

    // Closing is checked through a third block that reads the counter, since
    // terminate() runs after the log's last sample.
    Model probe;
    Block* reader = probe.addBlock("PythonFunction", 0, 0);
    reader->params().set("code", std::string(R"(
import simupy
from simupy import Block


class Reader(Block):
    inputs = 0
    outputs = ["y"]
    direct_feedthrough = False

    def output(self, t, u):
        return float(simupy._test_closes)
)"));
    Block* sink = probe.addBlock("Scope", 300, 0);
    probe.connect(reader->id(), 0, sink->id(), 0);
    probe.solver().stopTime = 0.1;

    checkClose(runToEnd(probe), 2.0, 1e-12,
               "and closed once per run, both times");
}

/// The hardware path end to end, through a real socket: one block sends, one
/// receives, and the value crosses between them.
///
/// No hardware and no second process — but the datagram is genuine, so the
/// send, the bind, the non-blocking drain and the lifecycle are all really
/// exercised.
void testUdpLoopback() {
    beginTest("A signal makes a round trip through a UDP socket");

    bool loaded = false;
    for (const char* candidate : {"../libraries/hardware.spylib",
                                  "libraries/hardware.spylib"}) {
        try {
            LibraryManager::instance().load(candidate);
            loaded = true;
            break;
        } catch (const ModelError&) {
            continue;
        }
    }
    check(loaded, "the shipped hardware library loaded");
    if (!loaded) return;

    check(BlockRegistry::instance().find("UdpSend") != nullptr,
          "UdpSend is registered");
    check(BlockRegistry::instance().find("UdpReceive") != nullptr,
          "UdpReceive is registered");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{12.5});

    Block* send = model.addBlock("UdpSend", 200, 0);
    send->params().set("host", std::string("127.0.0.1"));
    send->params().set("port", 55432.0);
    send->params().set("decimation", 1.0);

    Block* receive = model.addBlock("UdpReceive", 0, 200);
    receive->params().set("host", std::string("127.0.0.1"));
    receive->params().set("port", 55432.0);
    receive->params().set("width", 1.0);

    Block* scope = model.addBlock("Scope", 250, 200);
    model.connect(source->id(), 0, send->id(), 0);
    model.connect(receive->id(), 0, scope->id(), 0);

    model.solver().stopTime = 0.2;
    model.solver().method = SolverSettings::Method::RK4;
    model.solver().fixedStep = 0.005;

    // Paced, because the datagram is real and needs wall-clock time to cross
    // the stack. Loopback on Linux hands it over inside the send, so a run
    // going flat out happens to work there; Windows delivers asynchronously
    // and a whole flat-out run can finish before the first one lands.
    model.solver().realTime = true;
    model.solver().realTimeFactor = 1.0;

    const double last = runPaced(model);

    check(last != 0.0,
          "something arrived (the receiver holds zero until it does)");
    checkClose(last, 12.5, 1e-9,
               "and it is the value that was sent, intact");
}

void runRuntimeTests() {
    runTest(testPacerHoldsTheClock);
    runTest(testPacerAbsorbsOneSlowStep);
    runTest(testPacerReportsLag);
    runTest(testPacerCanBeAborted);
    runTest(testPacingDoesNotChangeResults);
    runTest(testPacingRoundTrip);
    runTest(testMissingOptionalDependency);
    runTest(testImportedBaseIsNotACandidate);
    runTest(testHardwareBlockLifecycle);
    runTest(testUdpLoopback);
}
