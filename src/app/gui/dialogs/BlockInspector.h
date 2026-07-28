#pragma once

#include "model/Model.h"

#include <QDialog>
#include <QStringList>

namespace simupy {

class PropertyPanel;

class BlockInspector : public QDialog {
    Q_OBJECT

public:
    BlockInspector(Model& model, Block* block, const QStringList& maskParameters,
                   bool editable, QWidget* parent = nullptr);

signals:
    void blockModified(const QString& blockId);
    void codeEditRequested(const QString& blockId, const QString& paramName);

private:
    QWidget* buildPorts(bool inputs);

    Model& model_;
    Block* block_;
    PropertyPanel* properties_ = nullptr;
};

}
