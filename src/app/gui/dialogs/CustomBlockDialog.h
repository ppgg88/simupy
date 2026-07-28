#pragma once

#include "io/CustomBlock.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
QT_END_NAMESPACE

namespace simupy {

class CustomBlockDialog : public QDialog {
    Q_OBJECT

public:
    CustomBlockDialog(CustomBlockDef def, QWidget* parent = nullptr);

    const CustomBlockDef& definition() const { return def_; }

    QString targetLibrary() const;

    void pinLibrary(const QString& name);

private slots:
    void chooseIconFile();
    void clearIcon();
    void addParameter();
    void removeParameter();
    void moveParameter(int delta);

private:
    void buildIdentityPage(QWidget* page);
    void buildMaskPage(QWidget* page);
    void buildIconPage(QWidget* page);

    void loadParameters();
    bool commitParameters(QString* problem);
    void refreshIconPreview();
    void accept() override;

    CustomBlockDef def_;

    QComboBox* library_ = nullptr;
    QLineEdit* newLibrary_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* displayName_ = nullptr;
    QComboBox* category_ = nullptr;
    QPlainTextEdit* description_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;

    QTableWidget* parameters_ = nullptr;

    QComboBox* iconKind_ = nullptr;
    QLineEdit* iconText_ = nullptr;
    QLabel* iconSource_ = nullptr;
    QLabel* iconPreview_ = nullptr;
    BlockIcon pendingIcon_;
};

}
