#pragma once

#include "app/gui/SimulationController.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QDoubleSpinBox;
class QPushButton;
QT_END_NAMESPACE

QT_FORWARD_DECLARE_CLASS(QChart)
QT_FORWARD_DECLARE_CLASS(QChartView)
QT_FORWARD_DECLARE_CLASS(QLineSeries)
QT_FORWARD_DECLARE_CLASS(QValueAxis)

namespace simupy {

class ScopeWindow : public QWidget {
    Q_OBJECT

public:
    ScopeWindow(QString blockId, QString title, QWidget* parent = nullptr);

    const QString& blockId() const { return blockId_; }

    void setLog(const SignalLogPtr& log);
    void clear();

    void setYRange(bool autoScale, double minimum, double maximum);

    /// Seconds of history to keep on screen; 0 shows the whole run.
    void setTimeWindow(double seconds);

    void setPlacement(bool docked, bool alwaysOnTop);

signals:
    void dockRequested(bool docked);
    void alwaysOnTopRequested(bool onTop);
    void timeWindowChanged(double seconds);

private:
    void exportCsv();
    void exportImage();
    void rescale();
    int firstVisibleSample() const;
    double windowEnd() const;

    QString blockId_;
    SignalLogPtr log_;

    QChart* chart_;
    QChartView* view_;
    QValueAxis* axisX_;
    QValueAxis* axisY_;
    QList<QLineSeries*> series_;

    QCheckBox* autoScaleBox_;
    QDoubleSpinBox* windowBox_;
    QCheckBox* onTopBox_;
    QPushButton* dockButton_;
    QLabel* info_;
    double window_ = 0.0;
    double yMin_ = -1.0;
    double yMax_ = 1.0;
};

}
