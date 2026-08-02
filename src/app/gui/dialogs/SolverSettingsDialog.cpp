#include "SolverSettingsDialog.h"

#include "app/gui/NumberInput.h"

#include "PythonEditor.h"
#include "PythonHighlighter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace simupy {
namespace {

QLineEdit* numberField(double value, QWidget* parent) {
    auto* field = new QLineEdit(numbers::format(value), parent);
    numbers::constrain(field);
    return field;
}

double fieldValue(const QLineEdit* field, double fallback) {
    bool ok = false;
    const double value = numbers::parse(field->text(), &ok);
    return ok ? value : fallback;
}

struct MethodEntry {
    const char* key;
    const char* label;
};

constexpr MethodEntry kMethods[] = {
    {"rk45", "Dormand-Prince 4(5) — variable step, explicit"},
    {"sdirk2", "SDIRK2 — variable step, implicit (stiff models)"},
    {"rk4", "Runge-Kutta 4 — fixed step"},
    {"heun", "Heun (RK2) — fixed step"},
    {"euler", "Euler — fixed step"},
    {"discrete", "Discrete only — no continuous states"},
};

}

SolverSettingsDialog::SolverSettingsDialog(Model& model, QWidget* parent)
    : QDialog(parent), model_(model) {
    setWindowTitle(tr("Simulation settings"));
    resize(620, 560);

    method_ = new QComboBox(this);
    for (const MethodEntry& entry : kMethods)
        method_->addItem(tr(entry.label), QString::fromLatin1(entry.key));

    const SolverSettings& solver = model_.solver();
    startTime_ = numberField(solver.startTime, this);
    stopTime_ = numberField(solver.stopTime, this);
    fixedStep_ = numberField(solver.fixedStep, this);
    maxStep_ = numberField(solver.maxStep, this);
    initialStep_ = numberField(solver.initialStep, this);
    relTol_ = numberField(solver.relTol, this);
    absTol_ = numberField(solver.absTol, this);

    maxStep_->setPlaceholderText(tr("0 = automatic"));
    initialStep_->setPlaceholderText(tr("0 = automatic"));

    maxSamples_ = new QSpinBox(this);
    maxSamples_->setRange(1000, 20000000);
    maxSamples_->setSingleStep(10000);
    maxSamples_->setGroupSeparatorShown(true);
    maxSamples_->setValue(solver.maxLoggedSamples);

    unbounded_ = new QCheckBox(tr("Run until stopped"), this);
    unbounded_->setChecked(solver.unbounded);
    unbounded_->setToolTip(
        tr("Ignore the stop time and keep going until Stop is pressed.\n"
           "Memory stays bounded: the log decimates itself as it fills."));

    realTime_ = new QCheckBox(tr("Run in real time"), this);
    realTime_->setChecked(solver.realTime);
    realTime_->setToolTip(
        tr("Hold the simulation to the wall clock instead of finishing as "
           "fast as the machine allows.\n"
           "The solver takes exactly the same steps, so results are "
           "identical — only the pace changes."));

    realTimeFactor_ = new QComboBox(this);
    for (double factor : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 50.0})
        realTimeFactor_->addItem(
            factor == 1.0 ? tr("1x — real time")
                          : tr("%1x").arg(factor, 0, 'g', 3),
            factor);
    {
        int index = realTimeFactor_->findData(solver.realTimeFactor);
        if (index < 0) {
            realTimeFactor_->addItem(
                tr("%1x").arg(solver.realTimeFactor, 0, 'g', 3),
                solver.realTimeFactor);
            index = realTimeFactor_->count() - 1;
        }
        realTimeFactor_->setCurrentIndex(index);
    }

    realTimeHint_ = new QLabel(this);
    realTimeHint_->setWordWrap(true);
    realTimeHint_->setEnabled(false);
    maxSamples_->setToolTip(
        tr("Once this many samples are recorded the log halves itself, "
           "keeping every other sample. Memory stays bounded on long runs."));

    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setEnabled(false);

    auto* timeGroup = new QGroupBox(tr("Time"), this);
    auto* timeForm = new QFormLayout(timeGroup);
    timeForm->addRow(tr("Start time (s)"), startTime_);
    timeForm->addRow(tr("Stop time (s)"), stopTime_);
    timeForm->addRow(QString(), unbounded_);

    auto* solverGroup = new QGroupBox(tr("Solver"), this);
    auto* solverForm = new QFormLayout(solverGroup);
    solverForm->addRow(tr("Method"), method_);
    solverForm->addRow(tr("Fixed step (s)"), fixedStep_);
    solverForm->addRow(tr("Max step (s)"), maxStep_);
    solverForm->addRow(tr("Initial step (s)"), initialStep_);
    solverForm->addRow(tr("Relative tolerance"), relTol_);
    solverForm->addRow(tr("Absolute tolerance"), absTol_);
    solverForm->addRow(hint_);

    auto* paceGroup = new QGroupBox(tr("Pacing"), this);
    auto* paceForm = new QFormLayout(paceGroup);
    paceForm->addRow(QString(), realTime_);
    paceForm->addRow(tr("Speed"), realTimeFactor_);
    paceForm->addRow(realTimeHint_);

    auto* dataGroup = new QGroupBox(tr("Data logging"), this);
    auto* dataForm = new QFormLayout(dataGroup);
    dataForm->addRow(tr("Max samples per signal"), maxSamples_);

    auto* solverPage = new QWidget;
    auto* solverLayout = new QVBoxLayout(solverPage);
    solverLayout->addWidget(timeGroup);
    solverLayout->addWidget(solverGroup);
    solverLayout->addWidget(paceGroup);
    solverLayout->addWidget(dataGroup);
    solverLayout->addStretch(1);

    initScript_ = new CodeEditor;
    highlighter_ = new PythonHighlighter(initScript_->document());
    initScript_->setPlainText(QString::fromStdString(model_.initScript()));

    auto* scriptPage = new QWidget;
    auto* scriptLayout = new QVBoxLayout(scriptPage);
    auto* scriptHint = new QLabel(
        tr("Python executed once before each run. Use it for shared "
           "constants and helper functions; Python blocks can import "
           "anything it puts on sys.modules."));
    scriptHint->setWordWrap(true);
    scriptHint->setEnabled(false);
    scriptLayout->addWidget(scriptHint);
    scriptLayout->addWidget(initScript_, 1);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(solverPage, tr("Solver"));
    tabs->addTab(scriptPage, tr("Init script"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    connect(method_, &QComboBox::currentIndexChanged, this,
            &SolverSettingsDialog::updateEnabledState);
    connect(realTime_, &QCheckBox::toggled, this,
            &SolverSettingsDialog::updateEnabledState);
    connect(unbounded_, &QCheckBox::toggled, this,
            &SolverSettingsDialog::updateEnabledState);
    load();
}

void SolverSettingsDialog::load() {
    const QString key =
        QString::fromLatin1(SolverSettings::methodName(model_.solver().method));
    const int index = method_->findData(key);
    method_->setCurrentIndex(index >= 0 ? index : 0);
    updateEnabledState();
}

void SolverSettingsDialog::updateEnabledState() {
    const QString key = method_->currentData().toString();
    const bool implicit = key == QLatin1String("sdirk2");
    const bool adaptive = key == QLatin1String("rk45") || implicit;

    stopTime_->setEnabled(!unbounded_->isChecked());

    const bool realTime = realTime_->isChecked();
    realTimeFactor_->setEnabled(realTime);
    realTimeHint_->setText(
        realTime
            ? tr("A run that cannot compute fast enough falls behind rather "
                 "than skipping work; the summary says so, and by how much.")
            : tr("The run finishes as fast as the machine allows — often far "
                 "faster than the time being simulated."));

    fixedStep_->setEnabled(!adaptive);
    initialStep_->setEnabled(adaptive);
    relTol_->setEnabled(adaptive);
    absTol_->setEnabled(adaptive);

    if (implicit) {
        hint_->setText(
            tr("Unconditionally stable, so the step size follows the "
               "solution rather than the fastest decaying mode. Worth it "
               "when a model mixes very fast and very slow dynamics; it is "
               "only second order, so at tight tolerances the explicit "
               "method is usually cheaper."));
    } else if (adaptive) {
        hint_->setText(
            tr("The step size adapts to keep the local error within the "
               "tolerances. Max step also bounds how far the solver may "
               "jump between logged samples."));
    } else {
        hint_->setText(
            tr("Every step is exactly the fixed step. Accuracy depends "
               "entirely on that choice — halve it and compare if you are "
               "unsure."));
    }
}

void SolverSettingsDialog::apply() {
    SolverSettings& solver = model_.solver();

    solver.method = SolverSettings::methodFromName(
        method_->currentData().toString().toStdString());
    solver.startTime = fieldValue(startTime_, solver.startTime);
    solver.stopTime = fieldValue(stopTime_, solver.stopTime);
    solver.fixedStep = fieldValue(fixedStep_, solver.fixedStep);
    solver.maxStep = fieldValue(maxStep_, solver.maxStep);
    solver.initialStep = fieldValue(initialStep_, solver.initialStep);
    solver.relTol = fieldValue(relTol_, solver.relTol);
    solver.absTol = fieldValue(absTol_, solver.absTol);
    solver.maxLoggedSamples = maxSamples_->value();
    solver.unbounded = unbounded_->isChecked();
    solver.realTime = realTime_->isChecked();
    solver.realTimeFactor = realTimeFactor_->currentData().toDouble();

    if (solver.stopTime <= solver.startTime)
        solver.stopTime = solver.startTime + 1.0;
    if (solver.fixedStep <= 0.0) solver.fixedStep = 0.001;
    if (solver.relTol <= 0.0) solver.relTol = 1e-4;
    if (solver.absTol <= 0.0) solver.absTol = 1e-7;

    model_.setInitScript(initScript_->toPlainText().toStdString());
}

}
