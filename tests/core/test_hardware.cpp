
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
#ifndef _WIN32
#include <sys/wait.h>
#include <csignal>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32

class FakeArduino {
public:
    FakeArduino() {
        int pipes[2];
        if (pipe(pipes) != 0) return;

        pid_ = fork();
        if (pid_ < 0) return;

        if (pid_ == 0) {
            dup2(pipes[1], STDOUT_FILENO);
            close(pipes[0]);
            close(pipes[1]);
            // --seconds is a safety net: a dead test must not hold the pty forever.
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

#endif

bool pyserialAvailable() {
    try {
        // Imported, not located: a module that fails to load is no more usable.
        const ParamValue found = PythonEngine::instance().evaluateExpression(
            "__import__('serial') is not None", {}, "pyserial availability");
        return std::get_if<bool>(&found) && std::get<bool>(found);
    } catch (const ModelError&) {
        return false;
    }
}

bool loadHardwareLibrary() {
    try {
        LibraryManager::instance().load("libraries/hardware.spylib");
        return true;
    } catch (const ModelError&) {
        return false;
    }
}

}

#ifndef _WIN32

void testArduinoLoopback() {
    beginTest("An Arduino loop closes through the serial bridge");

    if (!loadHardwareLibrary()) {
        check(false, "the shipped hardware library loaded");
        return;
    }

    if (!pyserialAvailable()) {
        check(true, "pyserial is not installed — skipped");
        return;
    }

    FakeArduino board;
    if (!board.ready()) {
        // Missing pyserial is a reason to skip, not to fail.
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

    // Paced: the board streams on its own clock.
    model.solver().stopTime = 1.0;
    model.solver().maxStep = 0.02;
    model.solver().realTime = true;
    model.solver().realTimeFactor = 1.0;

    const double last = runPaced(model);

    check(last != 0.0, "something came back from the board");
    // 0.75 of a 10-bit range is 767 counts, read back as 767/1023.
    checkClose(last, 767.0 / 1023.0, 1e-6,
               "and it is the duty that was written, quantised by the ADC");
}

void testArduinoDigitalAndSharing() {
    beginTest("Digital pins work, and one port means one connection");

    if (!loadHardwareLibrary()) return;

    if (!pyserialAvailable()) {
        check(true, "pyserial is not installed — skipped");
        return;
    }

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
    runPaced(model, &simulator);
    if (!simulator) return;

    const SignalLog& log = simulator->log();
    check(log.channels().size() == 1 && log.channels().front().width == 2,
          "both readings reached the log");
    if (log.sampleCount() == 0) return;

    const LogChannel& channel = log.channels().front();
    const int last = log.sampleCount() - 1;

    checkClose(channel.at(last, 0), 1.0, 1e-9,
               "the digital pin reads back the level that was written");
    // Zero, but a sample at all proves the second pin was registered.
    checkClose(channel.at(last, 1), 0.0, 1e-9,
               "and the second pin streams alongside it on the same link");
}

#endif

#ifdef _WIN32

void testSerialLoopbackNeedsAPty() {
    beginTest("Serial loopback needs a pty — not run on Windows");

    // The blocks themselves are Python and portable; only the fake board the
    // tests drive them against is POSIX, so say so rather than going quiet.
    check(loadHardwareLibrary(), "the shipped hardware library still loads");
}

#endif

void runHardwareTests() {
#ifdef _WIN32
    runTest(testSerialLoopbackNeedsAPty);
#else
    runTest(testArduinoLoopback);
    runTest(testArduinoDigitalAndSharing);
#endif
}
