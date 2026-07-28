#include "BlockInspector.h"

#include "app/gui/panels/PropertyPanel.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace simupy {
namespace {

/// A block with an unreadable parameter still has to be inspectable.
PortLayout portsOf(const Block& block) {
    try {
        return block.ports();
    } catch (const ModelError&) {
        return {};
    }
}

QString portLabel(int index, const std::string& name) {
    const QString number = QString::number(index + 1);
    const QString text = QString::fromStdString(name);
    return text.isEmpty() ? number : QStringLiteral("%1 · %2").arg(number, text);
}

QString describeSource(const Model& model, const Connection& wire) {
    const Block* source = model.block(wire.sourceBlock);
    if (!source) return QObject::tr("an unknown block");

    const PortLayout ports = portsOf(*source);
    const QString port =
        wire.sourcePort < static_cast<int>(ports.outputs.size())
            ? QString::fromStdString(ports.outputs[wire.sourcePort])
            : QString::number(wire.sourcePort + 1);

    const std::string signal = source->signalName(wire.sourcePort);
    const QString from = QObject::tr("from %1 (%2)")
                             .arg(QString::fromStdString(source->name()), port);
    return signal.empty()
               ? from
               : QStringLiteral("%1 — %2").arg(
                     from, QString::fromStdString(signal));
}

QString describeTarget(const Model& model, const Connection& wire) {
    const Block* target = model.block(wire.targetBlock);
    if (!target) return QObject::tr("an unknown block");

    const PortLayout ports = portsOf(*target);
    const QString port =
        wire.targetPort < static_cast<int>(ports.inputs.size())
            ? QString::fromStdString(ports.inputs[wire.targetPort])
            : QString::number(wire.targetPort + 1);

    return QObject::tr("to %1 (%2)")
        .arg(QString::fromStdString(target->name()), port);
}

}

BlockInspector::BlockInspector(Model& model, Block* block,
                               const QStringList& maskParameters, bool editable,
                               QWidget* parent)
    : QDialog(parent), model_(model), block_(block) {
    setWindowTitle(tr("%1 — %2")
                       .arg(QString::fromStdString(block->name()),
                            QString::fromStdString(block->typeName())));
    setModal(true);

    properties_ = new PropertyPanel(this);
    properties_->setMaskParameters(maskParameters);
    properties_->setBlock(&model_, block_);
    properties_->setEnabled(editable);
    connect(properties_, &PropertyPanel::blockModified, this,
            [this](const QString& blockId) {
                setWindowTitle(tr("%1 — %2")
                                   .arg(QString::fromStdString(block_->name()),
                                        QString::fromStdString(
                                            block_->typeName())));
                emit blockModified(blockId);
            });
    connect(properties_, &PropertyPanel::codeEditRequested, this,
            &BlockInspector::codeEditRequested);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    if (QWidget* inputs = buildPorts(true)) layout->addWidget(inputs);
    if (QWidget* outputs = buildPorts(false)) layout->addWidget(outputs);
    layout->addWidget(properties_, 1);

    if (!editable) {
        auto* note = new QLabel(
            tr("A run is in flight, so the parameters are read-only."), this);
        note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        layout->addWidget(note);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    setMinimumWidth(420);
    resize(460, std::min(560, sizeHint().height() + 40));
}

QWidget* BlockInspector::buildPorts(bool inputs) {
    const PortLayout ports = portsOf(*block_);
    const std::vector<std::string>& names =
        inputs ? ports.inputs : ports.outputs;
    if (names.empty()) return nullptr;

    auto* group = new QGroupBox(inputs ? tr("Inputs") : tr("Outputs"), this);
    auto* form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        QStringList wired;
        for (const Connection& wire : model_.connections()) {
            if (inputs && wire.targetBlock == block_->id() &&
                wire.targetPort == i)
                wired << describeSource(model_, wire);
            if (!inputs && wire.sourceBlock == block_->id() &&
                wire.sourcePort == i)
                wired << describeTarget(model_, wire);
        }

        auto* value = new QLabel(wired.isEmpty()
                                     ? tr("not connected")
                                     : wired.join(QStringLiteral(", ")));
        value->setWordWrap(true);
        if (wired.isEmpty())
            value->setStyleSheet(
                QStringLiteral("color: palette(placeholder-text);"));
        form->addRow(portLabel(i, names[i]), value);
    }
    return group;
}

}
