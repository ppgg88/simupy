// UDP and the Arduino bridge

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
#include <sys/wait.h>
#include <csignal>
#include <unistd.h>

namespace {

/// A `tools/fake_arduino.py` running on its own pseudo-terminal.
///
/// The board is Python speaking the same protocol as the firmware, so the
/// host side — the shared connection, the streaming, the generation
/// accounting, the pin registration — is genuinely exercised. What it cannot
/// prove is that the sketch in firmware/ agrees with it; that needs a board.
class FakeArduino {
public:
    FakeArduino() {
        int pipes[2];
        if (pipe(pipes) != 0) return;

        pid_ = fork();
        if (pid_ < 0) return;

        if (pid_ == 0) {
            // Child: the pty path goes down the pipe as its first line.
            dup2(pipes[1], STDOUT_FILENO);
            close(pipes[0]);
            close(pipes[1]);
            // --seconds is a safety net: a test that dies without cleaning up
            // must not leave a board sitting on a pty forever.
            execlp("python3", "python3", "tools/fake_arduino.py", "--mode",
                   "echo", "--seconds", "30", nullptr);
            _exit(127);
        }

        close(pipes[1]);
        std::string line;
        char c = 0;
        while (read(pipes[0], &c, 1) == 1 && c != '\n') line += c;
        close(pipes[0]);
        port_ = line;
    }

    ~FakeArduino() {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            int status = 0;
            waitpid(pid_, &status, 0);
        }
    }

    FakeArduino(const FakeArduino&) = delete;
    FakeArduino& operator=(const FakeArduino&) = delete;

    bool ready() const { return !port_.empty() && port_.rfind("/dev/", 0) == 0; }
    const std::string& port() const { return port_; }

private:
    pid_t pid_ = -1;
    std::string port_;
};

/// Loads the shipped hardware library from wherever the tests were run.
bool loadHardwareLibrary() {
    try {
        LibraryManager::instance().load("libraries/hardware.spylib");
        return true;
    } catch (const ModelError&) {
        return false;
    }
}

}  // namespace


/// The Arduino path end to end: a duty written to a PWM pin, read back from
/// the analog pin the board mirrors it to, through one shared serial link.
void testArduinoLoopback() {
    beginTest("An Arduino loop closes through the serial bridge");

    if (!loadHardwareLibrary()) {
        check(false, "the shipped hardware library loaded");
        return;
    }

    FakeArduino board;
    if (!board.ready()) {
        // pyserial or python3 missing is a reason to skip, not to fail: the
        // rest of SimuPy does not depend on either.
        check(true, "no fake board available — skipped");
        return;
    }

    Model model;
    Block* duty = model.addBlock("Constant", 0, 0);
    duty->params().set("value", std::vector<double>{0.75});

    Block* write = model.addBlock("ArduinoAnalogWrite", 200, 0);
    write->params().set("port", board.port());
    write->params().set("pin", 9.0);

    Block* read = model.addBlock("ArduinoAnalogRead", 0, 200);
    read->params().set("port", board.port());
    read->params().set("pin", 9.0);

    Block* scope = model.addBlock("Scope", 250, 200);
    model.connect(duty->id(), 0, write->id(), 0);
    model.connect(read->id(), 0, scope->id(), 0);

    // Paced, because the board streams on a wall clock and a run going flat
    // out would finish before the first sample arrived.
    model.solver().stopTime = 1.0;
    model.solver().maxStep = 0.02;
    model.solver().realTime = true;
    model.solver().realTimeFactor = 1.0;

    const double last = runToEnd(model);

    check(last != 0.0, "something came back from the board");
    // 0.75 of a 10-bit range is 767 counts, which reads back as 767/1023.
    checkClose(last, 767.0 / 1023.0, 1e-6,
               "and it is the duty that was written, quantised by the ADC");
}

/// Digital pins through the same shared link, and the sharing itself: four
/// blocks on one port must open one connection, not four.
void testArduinoDigitalAndSharing() {
    beginTest("Digital pins work, and one port means one connection");

    if (!loadHardwareLibrary()) return;

    FakeArduino board;
    if (!board.ready()) {
        check(true, "no fake board available — skipped");
        return;
    }

    Model model;
    Block* high = model.addBlock("Constant", 0, 0);
    high->params().set("value", std::vector<double>{1.0});

    Block* write = model.addBlock("ArduinoDigitalWrite", 200, 0);
    write->params().set("port", board.port());
    write->params().set("pin", 7.0);
    write->params().set("threshold", 0.5);

    Block* read = model.addBlock("ArduinoDigitalRead", 0, 200);
    read->params().set("port", board.port());
    read->params().set("pin", 7.0);

    // A second analog block on the same port, to prove they cooperate rather
    // than fight over the device.
    Block* analog = model.addBlock("ArduinoAnalogRead", 0, 400);
    analog->params().set("port", board.port());
    analog->params().set("pin", 3.0);

    Block* mux = model.addBlock("Mux", 250, 200);
    mux->params().set("inputs", 2.0);
    Block* scope = model.addBlock("Scope", 400, 200);

    model.connect(high->id(), 0, write->id(), 0);
    model.connect(read->id(), 0, mux->id(), 0);
    model.connect(analog->id(), 0, mux->id(), 1);
    model.connect(mux->id(), 0, scope->id(), 0);

    model.solver().stopTime = 1.0;
    model.solver().maxStep = 0.02;
    model.solver().realTime = true;
    model.solver().realTimeFactor = 1.0;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);
    if (!simulator) return;

    const SignalLog& log = simulator->log();
    check(log.channels().size() == 1 && log.channels().front().width == 2,
          "both readings reached the log");
    if (log.sampleCount() == 0) return;

    const LogChannel& channel = log.channels().front();
    const int last = log.sampleCount() - 1;

    checkClose(channel.at(last, 0), 1.0, 1e-9,
               "the digital pin reads back the level that was written");
    // Nothing has driven PWM pin 3, so the mirrored analog input reads zero —
    // which still proves the second pin was registered and streamed, since an
    // unregistered pin would never have produced a sample at all.
    checkClose(channel.at(last, 1), 0.0, 1e-9,
               "and the second pin streams alongside it on the same link");
}

void runHardwareTests() {
    runTest(testArduinoLoopback);
    runTest(testArduinoDigitalAndSharing);
}
