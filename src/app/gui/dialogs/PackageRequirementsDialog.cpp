#include "PackageRequirementsDialog.h"

#include "scripting/PythonPackages.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace simupy {
namespace {

enum Column { kModule = 0, kPackage = 1, kPurpose = 2, kStatus = 3 };

}

PackageRequirementsDialog::PackageRequirementsDialog(CustomLibrary& library,
                                                     QWidget* parent)
    : QDialog(parent), library_(library) {
    setWindowTitle(tr("%1 — Python packages")
                       .arg(QString::fromStdString(library.name)));
    setModal(true);
    resize(640, 380);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {tr("Module"), tr("pip name"), tr("What for"), tr("Status")});
    table_->horizontalHeader()->setSectionResizeMode(kPurpose,
                                                     QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    note_ = new QLabel(this);
    note_->setWordWrap(true);
    note_->setText(
        tr("What the blocks import. The pip name is only needed when it "
           "differs from the module — <code>serial</code> comes from "
           "<code>pyserial</code>. Packages install into SimuPy's own folder, "
           "never the system's Python."));

    auto* detectButton = new QPushButton(tr("Detect from blocks"), this);
    detectButton->setToolTip(
        tr("Read the library's Python sources and add what they import."));
    auto* addButton = new QPushButton(tr("Add"), this);
    removeButton_ = new QPushButton(tr("Remove"), this);

    auto* actions = new QHBoxLayout;
    actions->addWidget(detectButton);
    actions->addWidget(addButton);
    actions->addWidget(removeButton_);
    actions->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note_);
    layout->addWidget(table_, 1);
    layout->addLayout(actions);
    layout->addWidget(buttons);

    connect(detectButton, &QPushButton::clicked, this,
            &PackageRequirementsDialog::detectFromBlocks);
    connect(addButton, &QPushButton::clicked, this,
            [this] { addRow({}); });
    connect(removeButton_, &QPushButton::clicked, this,
            &PackageRequirementsDialog::removeSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        removeButton_->setEnabled(!table_->selectedItems().isEmpty());
    });
    connect(table_, &QTableWidget::itemChanged, this,
            [this] { refreshStatus(); });

    for (const PackageRequirement& need : library_.requires_) addRow(need);
    removeButton_->setEnabled(false);
    refreshStatus();
}

void PackageRequirementsDialog::addRow(const PackageRequirement& need) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, kModule,
                    new QTableWidgetItem(QString::fromStdString(need.module)));
    table_->setItem(row, kPackage,
                    new QTableWidgetItem(QString::fromStdString(need.package)));
    table_->setItem(row, kPurpose,
                    new QTableWidgetItem(QString::fromStdString(need.purpose)));

    auto* status = new QTableWidgetItem;
    status->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, kStatus, status);
}

void PackageRequirementsDialog::removeSelected() {
    std::set<int, std::greater<int>> rows;
    for (const QTableWidgetItem* item : table_->selectedItems())
        rows.insert(item->row());
    for (int row : rows) table_->removeRow(row);
}

void PackageRequirementsDialog::detectFromBlocks() {
    std::set<std::string> already;
    for (int row = 0; row < table_->rowCount(); ++row)
        already.insert(table_->item(row, kModule)->text().toStdString());

    int added = 0;
    for (const CustomBlockDef& def : library_.blocks) {
        std::string source = def.code;
        if (!def.parameterScript.empty())
            source += "\n" + def.parameterScript;

        for (const PackageRequirement& need : detectRequirements(source)) {
            if (!already.insert(need.module).second) continue;
            addRow(need);
            ++added;
        }
    }

    note_->setText(added > 0
                       ? tr("Added %n module(s) found in the sources. A static "
                            "read cannot see an import built at runtime, so "
                            "check the list.",
                            "", added)
                       : tr("Nothing new: the sources import nothing beyond "
                            "the standard library and what SimuPy ships."));
    refreshStatus();
}

void PackageRequirementsDialog::refreshStatus() {
    for (int row = 0; row < table_->rowCount(); ++row) {
        QTableWidgetItem* cell = table_->item(row, kStatus);
        QTableWidgetItem* module = table_->item(row, kModule);
        if (!cell || !module) continue;

        const std::string name = module->text().trimmed().toStdString();
        if (name.empty()) {
            cell->setText({});
            continue;
        }

        const PackageStatus status = PythonPackages::instance().status(name);
        cell->setText(status.installed
                          ? (status.version.empty()
                                 ? tr("installed")
                                 : tr("installed, %1")
                                       .arg(QString::fromStdString(
                                           status.version)))
                          : tr("missing"));
    }
}

std::vector<PackageRequirement> PackageRequirementsDialog::requirements() const {
    std::vector<PackageRequirement> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        PackageRequirement need;
        need.module = table_->item(row, kModule)->text().trimmed().toStdString();
        if (need.module.empty()) continue;
        need.package =
            table_->item(row, kPackage)->text().trimmed().toStdString();
        need.purpose =
            table_->item(row, kPurpose)->text().trimmed().toStdString();
        result.push_back(std::move(need));
    }
    return result;
}

}
