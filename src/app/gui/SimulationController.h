#pragma once

#include "model/Model.h"
#include "engine/Scheduler.h"
#include "engine/SignalLog.h"

#include <QElapsedTimer>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

namespace simupy {

using SignalLogPtr = std::shared_ptr<const SignalLog>;
using CompiledModelPtr = std::shared_ptr<const CompiledModel>;

/// Runs a simulation on a worker thread and streams progress back to the GUI.
///
/// The model must not be edited while a run is in flight — the main window
/// disables editing for the duration.
class SimulationController : public QThread {
    Q_OBJECT

public:
    explicit SimulationController(QObject* parent = nullptr);
    ~SimulationController() override;

    void start(Model& model);

    void requestStop();

    bool isBusy() const { return isRunning(); }

signals:
    /// The model compiled; carries signal widths for the canvas to display.
    void compiled(CompiledModelPtr compiled);
    void progressed(double time, double fraction);
    void dataUpdated(SignalLogPtr log);
    /// `error` is empty on success, and holds a user-facing message otherwise.
    void completed(bool ok, QString error, QString summary);
    void output(QString text, bool isError);

protected:
    void run() override;

private:
    Model* model_ = nullptr;
    std::atomic<bool> stopRequested_{false};
};

}  // namespace simupy

Q_DECLARE_METATYPE(simupy::SignalLogPtr)
Q_DECLARE_METATYPE(simupy::CompiledModelPtr)
