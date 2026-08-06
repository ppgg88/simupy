#pragma once

#include "io/CustomBlock.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace simupy {

class LibraryManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit LibraryManagerDialog(QWidget* parent = nullptr);

signals:
    void librariesChanged();

private slots:
    void importLibrary();
    void createLibrary();
    void exportSelected();
    void removeSelected();
    void editSelected();
    void onSelectionChanged();
    void installPackages();
    void editPackages();

private:
    void reload();
    QString selectedLibrary() const;
    QString packageReport(const CustomLibrary& library) const;
    std::vector<PackageRequirement> missingPackages(const CustomLibrary& library) const;
    QString selectedBlock() const;

    QTreeWidget* tree_ = nullptr;
    QLabel* details_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* packagesButton_ = nullptr;
    QPushButton* declareButton_ = nullptr;
};

}
