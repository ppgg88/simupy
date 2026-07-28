#pragma once

#include "model/Model.h"

#include <QWidget>

class QFormLayout;
class QLabel;
class QScrollArea;

namespace simupy {

/// Editor for the selected block's parameters.
///
/// The rows are generated from the block type's ParamSpec list, so a new
/// block type gets a usable editor without any GUI code.
class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setBlock(Model* model, Block* block);

    void refresh();

    Block* currentBlock() const { return block_; }

    void setMaskParameters(const QStringList& names);

signals:
    /// A parameter or the name changed; the canvas should refresh the block.
    void blockModified(const QString& blockId);
    void codeEditRequested(const QString& blockId, const QString& paramName);

private:
    void rebuild();
    void commit(const std::string& name, ParamValue value);

    QWidget* withExpressionToggle(const ParamSpec& spec, QWidget* editor);

    Model* model_ = nullptr;
    Block* block_ = nullptr;
    QStringList maskParameters_;

    QScrollArea* scroll_;
    QWidget* content_;
    QFormLayout* form_;
    QLabel* header_;
    QLabel* description_;
};

}  // namespace simupy
