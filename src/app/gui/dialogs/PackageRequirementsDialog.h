#pragma once

#include "io/CustomBlock.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace simupy {

/// Edits the Python packages a library declares.
///
/// Per library rather than per block: the packages are what the whole file
/// needs, and it is the file that gets shared.
class PackageRequirementsDialog : public QDialog {
    Q_OBJECT

public:
    PackageRequirementsDialog(CustomLibrary& library, QWidget* parent = nullptr);

    /// What the table holds now, in library order.
    std::vector<PackageRequirement> requirements() const;

private:
    void addRow(const PackageRequirement& need);
    void detectFromBlocks();
    void removeSelected();
    void refreshStatus();

    CustomLibrary& library_;
    QTableWidget* table_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* removeButton_ = nullptr;
};

}
