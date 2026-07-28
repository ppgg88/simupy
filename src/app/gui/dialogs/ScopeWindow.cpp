#include "ScopeWindow.h"

#include "app/gui/style/Theme.h"

#include <QCheckBox>
#include <QChart>
#include <QChartView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>
#include <QValueAxis>
#include <QVBoxLayout>

#include <fstream>
#include <limits>

namespace simupy {
namespace {

constexpr int kMaxPlottedPoints = 20000;

std::string csvField(const std::string& text) {
    if (text.find_first_of(",\"\n\r") == std::string::npos) return text;

    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + '"';
}

constexpr int kOpenGlThreshold = 4000;

}

ScopeWindow::ScopeWindow(QString blockId, QString title, QWidget* parent)
    : QWidget(parent), blockId_(std::move(blockId)) {
    setWindowTitle(title);
    resize(760, 480);

    chart_ = new QChart;
    chart_->setBackgroundBrush(theme::palette().canvas);
    chart_->setPlotAreaBackgroundBrush(theme::palette().canvas);
    chart_->setPlotAreaBackgroundVisible(true);
    chart_->legend()->setAlignment(Qt::AlignBottom);
    chart_->legend()->setLabelColor(theme::palette().blockText);
    chart_->setMargins(QMargins(4, 4, 4, 4));
    chart_->setTitleBrush(theme::palette().blockText);

    axisX_ = new QValueAxis;
    axisX_->setTitleText(tr("time (s)"));
    axisY_ = new QValueAxis;
    for (QValueAxis* axis : {axisX_, axisY_}) {
        axis->setLabelsColor(theme::palette().blockText);
        axis->setTitleBrush(theme::palette().blockText);
        axis->setGridLineColor(theme::palette().grid);
        axis->setLinePenColor(theme::palette().gridStrong);
    }
    chart_->addAxis(axisX_, Qt::AlignBottom);
    chart_->addAxis(axisY_, Qt::AlignLeft);

    view_ = new QChartView(chart_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setRubberBand(QChartView::RectangleRubberBand);

    autoScaleBox_ = new QCheckBox(tr("Auto-scale"), this);
    autoScaleBox_->setChecked(true);
    connect(autoScaleBox_, &QCheckBox::toggled, this, [this] { rescale(); });

    auto* resetButton = new QPushButton(tr("Reset zoom"), this);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        chart_->zoomReset();
        rescale();
    });

    auto* csvButton = new QPushButton(tr("Export CSV…"), this);
    connect(csvButton, &QPushButton::clicked, this, &ScopeWindow::exportCsv);

    auto* imageButton = new QPushButton(tr("Save image…"), this);
    connect(imageButton, &QPushButton::clicked, this, &ScopeWindow::exportImage);

    onTopBox_ = new QCheckBox(tr("On top"), this);
    onTopBox_->setToolTip(
        tr("Keep this plot above the main window, so it stays visible while "
           "you work on the diagram."));
    connect(onTopBox_, &QCheckBox::toggled, this,
            [this](bool on) { emit alwaysOnTopRequested(on); });

    dockButton_ = new QPushButton(tr("Dock"), this);
    dockButton_->setCheckable(true);
    dockButton_->setToolTip(
        tr("Attach the plot to the main window, as a tab beside the "
           "Console."));
    connect(dockButton_, &QPushButton::toggled, this,
            [this](bool docked) { emit dockRequested(docked); });

    info_ = new QLabel(tr("No data yet — run the model."), this);
    info_->setEnabled(false);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addWidget(autoScaleBox_);
    controls->addWidget(resetButton);
    controls->addStretch(1);
    controls->addWidget(info_);
    controls->addWidget(csvButton);
    controls->addWidget(imageButton);
    controls->addSpacing(12);
    controls->addWidget(onTopBox_);
    controls->addWidget(dockButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(view_, 1);
    layout->addLayout(controls);
}

void ScopeWindow::setPlacement(bool docked, bool alwaysOnTop) {
    QSignalBlocker dockGuard(dockButton_);
    QSignalBlocker topGuard(onTopBox_);

    dockButton_->setChecked(docked);
    dockButton_->setText(docked ? tr("Undock") : tr("Dock"));
    onTopBox_->setChecked(alwaysOnTop);
    onTopBox_->setEnabled(!docked);
}

void ScopeWindow::setYRange(bool autoScale, double minimum, double maximum) {
    autoScaleBox_->setChecked(autoScale);
    yMin_ = minimum;
    yMax_ = maximum;
    rescale();
}

void ScopeWindow::clear() {
    log_.reset();
    chart_->removeAllSeries();
    series_.clear();
    info_->setText(tr("No data yet — run the model."));
}

void ScopeWindow::setLog(const SignalLogPtr& log) {
    log_ = log;
    if (!log_ || log_->sampleCount() == 0) {
        clear();
        return;
    }

    QList<const LogChannel*> channels;
    for (const LogChannel& channel : log_->channels())
        if (blockId_.isEmpty() ||
            QString::fromStdString(channel.blockPath) == blockId_)
            channels.append(&channel);

    int seriesNeeded = 0;
    for (const LogChannel* channel : channels) seriesNeeded += channel->width;

    if (series_.size() != seriesNeeded) {
        chart_->removeAllSeries();
        series_.clear();
        const QList<QColor>& colors = theme::palette().series;

        int index = 0;
        for (const LogChannel* channel : channels) {
            for (int c = 0; c < channel->width; ++c) {
                auto* line = new QLineSeries;
                line->setName(QString::fromStdString(channel->label(c)));
                QPen pen(colors[index % colors.size()]);
                pen.setWidthF(1.8);
                line->setPen(pen);
                chart_->addSeries(line);
                line->attachAxis(axisX_);
                line->attachAxis(axisY_);
                series_.append(line);
                ++index;
            }
        }
    }

    const int sampleCount = log_->sampleCount();
    const int stride = std::max(1, sampleCount / kMaxPlottedPoints);
    const std::vector<double>& times = log_->times();

    int seriesIndex = 0;
    for (const LogChannel* channel : channels) {
        for (int c = 0; c < channel->width; ++c) {
            QList<QPointF> points;
            points.reserve(sampleCount / stride + 2);
            for (int i = 0; i < sampleCount; i += stride)
                points.append(QPointF(times[i], channel->at(i, c)));
            if ((sampleCount - 1) % stride != 0)
                points.append(QPointF(times[sampleCount - 1],
                                      channel->at(sampleCount - 1, c)));

            QLineSeries* line = series_[seriesIndex++];
            line->setUseOpenGL(points.size() > kOpenGlThreshold);
            line->replace(points);
        }
    }

    info_->setText(tr("%1 samples · %2 signals")
                       .arg(sampleCount)
                       .arg(series_.size()));
    rescale();
}

void ScopeWindow::rescale() {
    if (!log_ || log_->sampleCount() == 0) return;

    const std::vector<double>& times = log_->times();
    axisX_->setRange(times.front(), std::max(times.back(), times.front() + 1e-9));

    if (!autoScaleBox_->isChecked()) {
        axisY_->setRange(yMin_, yMax_);
        return;
    }

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const QLineSeries* line : std::as_const(series_)) {
        for (const QPointF& point : line->points()) {
            minimum = std::min(minimum, point.y());
            maximum = std::max(maximum, point.y());
        }
    }

    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        axisY_->setRange(-1.0, 1.0);
        return;
    }
    if (maximum - minimum < 1e-12) {
        const double padding = std::max(std::abs(maximum) * 0.1, 0.5);
        axisY_->setRange(minimum - padding, maximum + padding);
        return;
    }

    const double margin = (maximum - minimum) * 0.08;
    axisY_->setRange(minimum - margin, maximum + margin);
}

void ScopeWindow::exportCsv() {
    if (!log_ || log_->sampleCount() == 0) {
        QMessageBox::information(this, tr("Export"), tr("There is no data yet."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export signals"), QStringLiteral("signals.csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    std::ofstream stream(path.toStdString(), std::ios::out | std::ios::trunc);
    if (!stream) {
        QMessageBox::warning(this, tr("Export"),
                             tr("Could not write to %1").arg(path));
        return;
    }
    stream.precision(12);

    QList<const LogChannel*> channels;
    for (const LogChannel& channel : log_->channels())
        if (blockId_.isEmpty() ||
            QString::fromStdString(channel.blockPath) == blockId_)
            channels.append(&channel);

    stream << "time";
    for (const LogChannel* channel : channels)
        for (int c = 0; c < channel->width; ++c)
            stream << ',' << csvField(channel->label(c));
    stream << '\n';

    for (int i = 0; i < log_->sampleCount(); ++i) {
        stream << log_->times()[i];
        for (const LogChannel* channel : channels)
            for (int c = 0; c < channel->width; ++c)
                stream << ',' << channel->at(i, c);
        stream << '\n';
    }
}

void ScopeWindow::exportImage() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save plot"), QStringLiteral("scope.png"),
        tr("PNG images (*.png)"));
    if (path.isEmpty()) return;

    QPixmap pixmap = view_->grab();
    if (!pixmap.save(path))
        QMessageBox::warning(this, tr("Save image"),
                             tr("Could not save to %1").arg(path));
}

}
