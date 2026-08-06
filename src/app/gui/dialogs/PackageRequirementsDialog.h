#pragma once

#include "io/CustomBlock.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace simupy {

/// Edits the Python packages a model or a library declares.
///
/// Per file rather than per block: the packages are what the whole thing needs,
/// and it is the whole thing that gets shared or handed on.
class PackageRequirementsDialog : public QDialog {
    Q_OBJECT

public:
    /// `sources` is the Python the detector reads — every block's script, and
    /// a model's init script.
    PackageRequirementsDialog(const QString& subject,
                              std::vector<PackageRequirement> declared,
                              std::vector<std::string> sources,
                              QWidget* parent = nullptr);

    /// What the table holds now, in order.
    std::vector<PackageRequirement> requirements() const;

private:
    void addRow(const PackageRequirement& need);
    void addBlankRow();
    void installMissing();
    std::vector<PackageRequirement> missing() const;
    void detectFromBlocks();
    void removeSelected();
    void refreshStatus();

    std::vector<std::string> sources_;
    QTableWidget* table_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* installButton_ = nullptr;
};

}
