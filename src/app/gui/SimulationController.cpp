#include "SimulationController.h"

#include "engine/RealTimePacer.h"
#include "engine/Simulator.h"
#include "scripting/PythonEngine.h"

#include <QMetaType>

namespace simupy {
namespace {

constexpr qint64 kSnapshotIntervalMs = 120;
constexpr qint64 kRealTimeSnapshotIntervalMs = 40;

constexpr int kLiveSnapshotSamples = 4000;

}

SimulationController::SimulationController(QObject* parent) : QThread(parent) {
    qRegisterMetaType<SignalLogPtr>("simupy::SignalLogPtr");
    qRegisterMetaType<CompiledModelPtr>("simupy::CompiledModelPtr");
}

SimulationController::~SimulationController() {
    requestStop();
    // A QThread destroyed while running aborts the process; stalling is safer.
    if (!wait(5000)) wait();
}

bool SimulationController::stopAndWait(unsigned long milliseconds) {
    requestStop();
    return wait(milliseconds);
}

void SimulationController::start(Model& model) {
    if (isRunning()) return;
    model_ = &model;
    stopRequested_ = false;
    QThread::start();
}

void SimulationController::requestStop() { stopRequested_ = true; }

void SimulationController::run() {
    if (!model_) {
        emit completed(false, tr("No model to run"), {});
        return;
    }

    PythonEngine& python = PythonEngine::instance();
    python.setOutputHandler([this](const std::string& text, bool isError) {
        emit output(QString::fromStdString(text), isError);
    });

    // One acquisition for the whole run: the GIL per evaluation costs more.
    ScopedGil gil;

    QElapsedTimer wallClock;
    wallClock.start();
    qint64 lastSnapshot = 0;

    try {
        if (!model_->initScript().empty())
            python.execute(model_->initScript(), "<model init script>");

        Simulator simulator(*model_, pythonExpressionEvaluator());
        simulator.initialize();

        emit compiled(std::make_shared<CompiledModel>(simulator.compiled()));

        const SolverSettings& settings = model_->solver();
        const bool realTime = settings.realTime;
        const qint64 snapshotInterval =
            realTime ? kRealTimeSnapshotIntervalMs : kSnapshotIntervalMs;

        RealTimePacer pacer(settings.startTime, settings.realTimeFactor);
        const auto aborted = [this] { return stopRequested_.load(); };

        bool stoppedEarly = false;
        while (simulator.step()) {
            if (stopRequested_) {
                stoppedEarly = true;
                break;
            }

            // After the step: the deadline belongs to the instant just reached.
            if (realTime) {
                // The GIL goes back during the wait, for hardware blocks' own threads.
                ScopedGilRelease unlocked;
                pacer.waitUntil(simulator.time(), aborted);
            }

            const qint64 now = wallClock.elapsed();
            if (now - lastSnapshot >= snapshotInterval) {
                lastSnapshot = now;
                emit progressed(simulator.time(), simulator.progress());
                // Thinned; the whole log goes out once, below, at the end.
                emit dataUpdated(std::make_shared<SignalLog>(
                    simulator.log().sampled(kLiveSnapshotSamples)));
            }
        }

        simulator.terminate();

        emit progressed(simulator.time(), simulator.progress());
        emit dataUpdated(std::make_shared<SignalLog>(simulator.log()));

        const double seconds = wallClock.elapsed() / 1000.0;

        QString pacing;
        if (realTime) {
            if (pacer.heldRealTime())
                pacing = tr(" — real time held, worst lag %1 ms")
                             .arg(pacer.worstLag() * 1000.0, 0, 'f', 1);
            else
                pacing = tr(" — COULD NOT KEEP UP: worst lag %1 s, resynced "
                            "%2 time(s); the model is too slow for %3x real "
                            "time")
                             .arg(pacer.worstLag(), 0, 'f', 2)
                             .arg(pacer.resyncCount())
                             .arg(pacer.factor(), 0, 'g', 3);
        }

        const QString summary =
            stoppedEarly
                ? tr("Stopped at t = %1 s after %2 steps (%3 s wall clock)")
                      .arg(simulator.time(), 0, 'g', 6)
                      .arg(simulator.majorSteps())
                      .arg(seconds, 0, 'f', 2) + pacing
                : tr("Finished at t = %1 s — %2 steps, %3 rejected, "
                     "%4 derivative evaluations, %5 s wall clock")
                      .arg(simulator.time(), 0, 'g', 6)
                      .arg(simulator.majorSteps())
                      .arg(simulator.rejectedSteps())
                      .arg(simulator.derivativeEvaluations())
                      .arg(seconds, 0, 'f', 2) + pacing;

        emit completed(true, {}, summary);
    } catch (const ModelError& error) {
        emit completed(false, QString::fromStdString(error.what()), {});
    } catch (const std::exception& error) {
        emit completed(false,
                       tr("Unexpected failure: %1")
                           .arg(QString::fromStdString(error.what())),
                       {});
    }

    python.setOutputHandler({});
}

}
