#pragma once

#include "app/gui/SimulationController.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
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

    void setPlacement(bool docked, bool alwaysOnTop);

signals:
    void dockRequested(bool docked);
    void alwaysOnTopRequested(bool onTop);

private:
    void exportCsv();
    void exportImage();
    void rescale();

    QString blockId_;
    SignalLogPtr log_;

    QChart* chart_;
    QChartView* view_;
    QValueAxis* axisX_;
    QValueAxis* axisY_;
    QList<QLineSeries*> series_;

    QCheckBox* autoScaleBox_;
    QCheckBox* onTopBox_;
    QPushButton* dockButton_;
    QLabel* info_;
    double yMin_ = -1.0;
    double yMax_ = 1.0;
};

}  // namespace simupy
