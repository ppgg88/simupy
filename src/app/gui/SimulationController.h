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

class SimulationController : public QThread {
    Q_OBJECT

public:
    explicit SimulationController(QObject* parent = nullptr);
    ~SimulationController() override;

    void start(Model& model);

    void requestStop();

    /// False means the thread is still running, and still reading the model.
    bool stopAndWait(unsigned long milliseconds);

    bool isBusy() const { return isRunning(); }

signals:
    void compiled(CompiledModelPtr compiled);
    void progressed(double time, double fraction);
    void dataUpdated(SignalLogPtr log);
    void completed(bool ok, QString error, QString summary);
    void output(QString text, bool isError);

protected:
    void run() override;

private:
    Model* model_ = nullptr;
    std::atomic<bool> stopRequested_{false};
};

}

Q_DECLARE_METATYPE(simupy::SignalLogPtr)
Q_DECLARE_METATYPE(simupy::CompiledModelPtr)
