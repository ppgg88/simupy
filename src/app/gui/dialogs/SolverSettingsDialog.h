#pragma once

#include "model/Model.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
QT_END_NAMESPACE

namespace simupy {

class CodeEditor;
class PythonHighlighter;

class SolverSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SolverSettingsDialog(Model& model, QWidget* parent = nullptr);

    void apply();

private:
    void updateEnabledState();
    void load();

    Model& model_;

    QComboBox* method_;
    QLineEdit* startTime_;
    QLineEdit* stopTime_;
    QLineEdit* fixedStep_;
    QLineEdit* maxStep_;
    QLineEdit* initialStep_;
    QLineEdit* relTol_;
    QLineEdit* absTol_;
    QSpinBox* maxSamples_;
    QCheckBox* unbounded_;
    QCheckBox* realTime_;
    QComboBox* realTimeFactor_;
    QLabel* realTimeHint_;
    QLabel* hint_;
    CodeEditor* initScript_;
    PythonHighlighter* highlighter_;
};

}  // namespace simupy
