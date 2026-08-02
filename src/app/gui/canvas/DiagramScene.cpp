#include "DiagramScene.h"

#include "BlockItem.h"
#include "app/gui/style/Theme.h"
#include "WireItem.h"
#include "blocks/SubsystemBlock.h"
#include "blocks/SubsystemGrouping.h"
#include "model/BlockRegistry.h"
#include "io/ModelSerializer.h"
#include "scripting/PythonEngine.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QGraphicsPathItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>

#include <cmath>

namespace simupy {

const char* DiagramScene::kBlockMimeType = "application/x-simupy-block";

DiagramScene::DiagramScene(Model& model, QObject* parent)
    : QGraphicsScene(parent), model_(&model) {
    setSceneRect(-2000, -2000, 8000, 8000);
    rebuild();
}

void DiagramScene::setModel(Model& model) {
    model_ = &model;
    rebuild();
}

QPointF DiagramScene::snap(const QPointF& point) {
    return QPointF(std::round(point.x() / kGridStep) * kGridStep,
                   std::round(point.y() / kGridStep) * kGridStep);
}

void DiagramScene::rebuild() {
    cancelPendingWire();
    blockItems_.clear();
    wireItems_.clear();
    clear();

    for (const BlockPtr& block : model_->blocks()) {
        auto* item = new BlockItem(*model_, block.get());
        const QString id = item->blockId();
        blockItems_.insert(id, item);
        addItem(item);

        connect(item, &BlockItem::geometryChanged, this,
                [this] { rerouteWires(); });
        connect(item, &BlockItem::editRequested, this,
                [this, item] { emit blockEditRequested(item); });
        connect(item, &BlockItem::controlChanged, this,
                &DiagramScene::controlChanged);
        item->setInteractionLocked(locked_);
    }

    for (const Connection& connection : model_->connections())
        addWireItem(connection);
    rerouteWires();
}

void DiagramScene::addWireItem(const Connection& connection) {
    BlockItem* source =
        blockItems_.value(QString::fromStdString(connection.sourceBlock));
    BlockItem* target =
        blockItems_.value(QString::fromStdString(connection.targetBlock));
    if (!source || !target) return;

    auto* wire = new WireItem(QString::fromStdString(connection.id), source,
                              connection.sourcePort, target,
                              connection.targetPort);
    wire->setSignalName(QString::fromStdString(
        source->block()->signalName(connection.sourcePort)));
    wireItems_.append(wire);
    addItem(wire);
}

void DiagramScene::promptSignalName(WireItem* wire) {
    if (!wire) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        nullptr, tr("Signal name"),
        tr("Name for this signal.\n\nIt labels every branch of it and appears "
           "in the scope legend and the CSV header."),
        QLineEdit::Normal, wire->signalName(), &ok);
    if (!ok) return;

    renameSignal(wire, name.trimmed());
}

void DiagramScene::renameSignal(WireItem* wire, const QString& name) {
    if (locked_ || !wire || !wire->sourceItem()) return;

    Block* source = wire->sourceItem()->block();
    source->setSignalName(wire->sourcePort(), name.toStdString());

    const QString stored =
        QString::fromStdString(source->signalName(wire->sourcePort()));
    for (WireItem* other : std::as_const(wireItems_))
        if (other->sourceItem() == wire->sourceItem() &&
            other->sourcePort() == wire->sourcePort())
            other->setSignalName(stored);

    emit modelEdited();
}

BlockItem* DiagramScene::itemForBlock(const QString& blockId) const {
    return blockItems_.value(blockId);
}

namespace {

QString clipboardSelection() {
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    if (!data) return {};

    if (data->hasFormat(QLatin1String(ModelSerializer::kClipboardMimeType)))
        return QString::fromUtf8(data->data(
            QLatin1String(ModelSerializer::kClipboardMimeType)));

    if (data->hasText() &&
        ModelSerializer::isPastable(data->text().toStdString()))
        return data->text();

    return {};
}

}

bool DiagramScene::clipboardHasBlocks() { return !clipboardSelection().isEmpty(); }

void DiagramScene::copySelection() {
    const QList<BlockItem*> blocks = selectedBlocks();
    if (blocks.isEmpty()) return;

    std::vector<std::string> ids;
    ids.reserve(blocks.size());
    for (BlockItem* item : blocks) ids.push_back(item->block()->id());

    const std::string text = ModelSerializer::copySelection(*model_, ids);

    auto* data = new QMimeData;
    data->setData(QLatin1String(ModelSerializer::kClipboardMimeType),
                  QByteArray::fromStdString(text));
    data->setText(QString::fromStdString(text));
    QGuiApplication::clipboard()->setMimeData(data);

    emit statusMessage(tr("Copied %n block(s)", "", blocks.size()));
}

void DiagramScene::cutSelection() {
    if (locked_) return;

    if (selectedBlocks().isEmpty()) return;
    copySelection();
    deleteSelection();
}

void DiagramScene::pasteAt(const QPointF& scenePos) {
    if (locked_) return;

    const QString text = clipboardSelection();
    if (text.isEmpty()) {
        emit statusMessage(tr("The clipboard holds no blocks."));
        return;
    }

    std::vector<std::string> created;
    try {
        created = ModelSerializer::pasteInto(*model_, text.toStdString());
    } catch (const ModelError& error) {
        emit statusMessage(tr("Could not paste: %1")
                               .arg(QString::fromStdString(error.what())));
        return;
    }
    if (created.empty()) return;

    QRectF bounds;
    for (const std::string& id : created) {
        const BlockGeometry& g = model_->geometry(id);
        bounds = bounds.united(QRectF(g.x, g.y, g.width, g.height));
    }

    const QPointF target = snap(scenePos);
    const QPointF delta = target - bounds.topLeft();
    for (const std::string& id : created) {
        BlockGeometry& g = model_->geometry(id);
        g.x += delta.x();
        g.y += delta.y();
    }

    rebuild();

    clearSelection();
    for (const std::string& id : created)
        if (BlockItem* item = itemForBlock(QString::fromStdString(id)))
            item->setSelected(true);

    emit statusMessage(tr("Pasted %n block(s)", "", int(created.size())));
    emit modelEdited();
}

void DiagramScene::groupSelection() {
    if (locked_) return;

    const QList<BlockItem*> blocks = selectedBlocks();
    if (blocks.isEmpty()) {
        emit statusMessage(tr("Select the blocks to group first."));
        return;
    }

    std::vector<std::string> ids;
    ids.reserve(blocks.size());
    for (BlockItem* item : blocks) ids.push_back(item->block()->id());

    GroupResult result;
    try {
        result = groupIntoSubsystem(*model_, ids);
    } catch (const ModelError& error) {
        emit statusMessage(tr("Could not group: %1")
                               .arg(QString::fromStdString(error.what())));
        return;
    }

    rebuild();

    clearSelection();
    const QString subsystemId = QString::fromStdString(result.subsystemId);
    if (BlockItem* item = itemForBlock(subsystemId)) item->setSelected(true);

    emit statusMessage(tr("Grouped %n block(s) into a subsystem — %1 in, %2 out",
                          "", int(ids.size()))
                           .arg(result.inputs)
                           .arg(result.outputs));
    emit modelEdited();
}

void DiagramScene::ungroupSelection() {
    if (locked_) return;

    const QList<BlockItem*> blocks = selectedBlocks();
    if (blocks.size() != 1 || !isSubsystem(blocks.first()->block())) {
        emit statusMessage(tr("Select a single Subsystem block to break open."));
        return;
    }

    const std::string subsystemId = blocks.first()->block()->id();

    UngroupResult result;
    try {
        // The evaluator is what lets a masked subsystem give up its parameters.
        result = ungroupSubsystem(*model_, subsystemId,
                                  pythonExpressionEvaluator());
    } catch (const ModelError& error) {
        emit statusMessage(tr("Could not break it open: %1")
                               .arg(QString::fromStdString(error.what())));
        return;
    }

    rebuild();

    clearSelection();
    for (const std::string& id : result.blockIds)
        if (BlockItem* item = itemForBlock(QString::fromStdString(id)))
            item->setSelected(true);

    if (result.resolvedParameters > 0) {
        emit statusMessage(
            tr("Broke the subsystem open — %n block(s) came up, with the mask "
               "parameters written in as values",
               "", int(result.blockIds.size())));
    } else {
        emit statusMessage(tr("Broke the subsystem open — %n block(s) came up",
                              "", int(result.blockIds.size())));
    }
    emit modelEdited();
}

void DiagramScene::duplicateSelection() {
    if (locked_) return;

    const QList<BlockItem*> blocks = selectedBlocks();
    if (blocks.isEmpty()) return;

    std::vector<std::string> ids;
    ids.reserve(blocks.size());
    for (BlockItem* item : blocks) ids.push_back(item->block()->id());

    // Not through the clipboard: a duplicate must not discard what was copied.
    const std::string text = ModelSerializer::copySelection(*model_, ids);

    std::vector<std::string> created;
    try {
        created = ModelSerializer::pasteInto(*model_, text, kGridStep * 2,
                                             kGridStep * 2);
    } catch (const ModelError& error) {
        emit statusMessage(tr("Could not duplicate: %1")
                               .arg(QString::fromStdString(error.what())));
        return;
    }

    rebuild();

    clearSelection();
    for (const std::string& id : created)
        if (BlockItem* item = itemForBlock(QString::fromStdString(id)))
            item->setSelected(true);

    emit statusMessage(tr("Duplicated %n block(s)", "", int(created.size())));
    emit modelEdited();
}

void DiagramScene::setInteractionLocked(bool locked) {
    if (locked_ == locked) return;
    locked_ = locked;
    cancelPendingWire();
    for (BlockItem* item : std::as_const(blockItems_))
        item->setInteractionLocked(locked);
}

WireItem* DiagramScene::selectedWire() const {
    WireItem* found = nullptr;
    for (QGraphicsItem* item : selectedItems()) {
        if (item->type() != WireItem::Type) continue;
        if (found) return nullptr;
        found = static_cast<WireItem*>(item);
    }
    return found;
}

BlockItem* DiagramScene::addBlock(const QString& typeName,
                                  const QPointF& scenePos) {
    const BlockType* type =
        BlockRegistry::instance().find(typeName.toStdString());
    if (!type) {
        emit statusMessage(tr("Unknown block type '%1'").arg(typeName));
        return nullptr;
    }

    const QPointF corner =
        snap(scenePos - QPointF(type->defaultWidth / 2, type->defaultHeight / 2));

    Block* block = nullptr;
    try {
        block = model_->addBlock(typeName.toStdString(), corner.x(), corner.y());
    } catch (const ModelError& error) {
        emit statusMessage(QString::fromStdString(error.what()));
        return nullptr;
    }

    auto* item = new BlockItem(*model_, block);
    blockItems_.insert(item->blockId(), item);
    addItem(item);
    connect(item, &BlockItem::geometryChanged, this,
            [this] { rerouteWires(); });
    connect(item, &BlockItem::editRequested, this,
            [this, item] { emit blockEditRequested(item); });

    clearSelection();
    item->setSelected(true);
    rerouteWires();
    emit modelEdited();
    return item;
}

void DiagramScene::refreshBlock(const QString& blockId) {
    BlockItem* item = blockItems_.value(blockId);
    if (!item) return;

    item->refresh();

    const std::size_t before = model_->connections().size();
    model_->pruneDanglingConnections();
    if (model_->connections().size() != before) {
        for (WireItem* wire : wireItems_) removeItem(wire);
        qDeleteAll(wireItems_);
        wireItems_.clear();
        for (const Connection& connection : model_->connections())
            addWireItem(connection);
        emit statusMessage(tr("Removed %1 wire(s) left dangling by the change")
                               .arg(before - model_->connections().size()));
    }
    rerouteWires();
    emit modelEdited();
}

void DiagramScene::rerouteWires() {
    obstacles_.clear();
    obstacles_.reserve(blockItems_.size());
    for (BlockItem* item : std::as_const(blockItems_)) {
        obstacles_.append(item->mapRectToScene(item->boundingRect()));
    }

    WireField placed;
    for (WireItem* wire : wireItems_) wire->updatePath(obstacles_, &placed);
}

QList<BlockItem*> DiagramScene::selectedBlocks() const {
    QList<BlockItem*> result;
    for (QGraphicsItem* item : selectedItems())
        if (item->type() == BlockItem::Type)
            result.append(static_cast<BlockItem*>(item));
    return result;
}

void DiagramScene::deleteSelection() {
    if (locked_) return;

    const QList<QGraphicsItem*> selection = selectedItems();
    if (selection.isEmpty()) return;

    QList<WireItem*> wiresToRemove;
    QList<BlockItem*> blocksToRemove;
    for (QGraphicsItem* item : selection) {
        if (item->type() == WireItem::Type)
            wiresToRemove.append(static_cast<WireItem*>(item));
        else if (item->type() == BlockItem::Type)
            blocksToRemove.append(static_cast<BlockItem*>(item));
    }

    for (WireItem* wire : wiresToRemove)
        model_->disconnect(wire->connectionId().toStdString());
    for (BlockItem* block : blocksToRemove)
        model_->removeBlock(block->blockId().toStdString());

    const int removed = wiresToRemove.size() + blocksToRemove.size();

    rebuild();
    emit modelEdited();
    emit statusMessage(tr("Deleted %n item(s)", "", removed));
}

void DiagramScene::mirrorSelection() {
    if (locked_) return;

    const QList<BlockItem*> blocks = selectedBlocks();
    if (blocks.isEmpty()) return;
    for (BlockItem* block : blocks) {
        block->setMirrored(!block->isMirrored());
    }
    rerouteWires();
    emit modelEdited();
}

void DiagramScene::selectAll() {
    for (QGraphicsItem* item : items()) item->setSelected(true);
}

void DiagramScene::applySignalWidths(const CompiledModel& compiled) {
    for (WireItem* wire : wireItems_) {
        const CompiledBlock* source =
            compiled.find(wire->sourceItem()->blockId().toStdString());
        if (!source ||
            wire->sourcePort() >= static_cast<int>(source->outputSignal.size()))
            continue;
        const int signal = source->outputSignal[wire->sourcePort()];
        if (signal >= 0 && signal < static_cast<int>(compiled.signalWidths.size()))
            wire->setSignalWidth(compiled.signalWidths[signal]);
    }
}

void DiagramScene::refreshValues() {
    for (BlockItem* item : std::as_const(blockItems_)) item->refreshValue();
}

void DiagramScene::clearSignalWidths() {
    for (WireItem* wire : wireItems_) wire->setSignalWidth(0);
}

void DiagramScene::setBlockProblem(const QString& blockId,
                                   const QString& message) {
    if (BlockItem* item = blockItems_.value(blockId)) item->setProblem(message);
}

void DiagramScene::clearBlockProblems() {
    for (BlockItem* item : std::as_const(blockItems_)) item->setProblem({});
}

void DiagramScene::drawBackground(QPainter* painter, const QRectF& rect) {
    const theme::Palette& colors = theme::palette();
    painter->fillRect(rect, colors.canvas);

    const qreal scale = painter->worldTransform().m11();
    if (scale < 0.35) return;

    const qreal step = kGridStep * 2.0;
    const qreal left = std::floor(rect.left() / step) * step;
    const qreal top = std::floor(rect.top() / step) * step;

    QVector<QLineF> fine;
    QVector<QLineF> coarse;
    for (qreal x = left; x < rect.right(); x += step) {
        QLineF line(x, rect.top(), x, rect.bottom());
        (std::fmod(std::abs(x), step * 5.0) < 0.01 ? coarse : fine).append(line);
    }
    for (qreal y = top; y < rect.bottom(); y += step) {
        QLineF line(rect.left(), y, rect.right(), y);
        (std::fmod(std::abs(y), step * 5.0) < 0.01 ? coarse : fine).append(line);
    }

    if (scale >= 0.7) {
        painter->setPen(QPen(colors.grid, 0));
        painter->drawLines(fine);
    }
    painter->setPen(QPen(colors.gridStrong, 0));
    painter->drawLines(coarse);
}

DiagramScene::PortHit DiagramScene::portAt(const QPointF& scenePos) const {
    const QList<QGraphicsItem*> nearby =
        items(QRectF(scenePos - QPointF(14, 14), QSizeF(28, 28)));

    for (QGraphicsItem* item : nearby) {
        if (item->type() != BlockItem::Type) continue;
        auto* block = static_cast<BlockItem*>(item);

        const int output = block->outputPortAt(scenePos);
        if (output >= 0) return {block, output, false};

        const int input = block->inputPortAt(scenePos);
        if (input >= 0) return {block, input, true};
    }
    return {};
}

DiagramScene::PortHit DiagramScene::portNear(const QPointF& scenePos,
                                             bool wantInput) const {
    const qreal margin = BlockItem::portDropReach() + kGridStep;
    const QList<QGraphicsItem*> nearby = items(
        QRectF(scenePos - QPointF(margin, margin), QSizeF(2 * margin, 2 * margin)));

    PortHit best;
    qreal bestDistance = 0.0;
    for (QGraphicsItem* item : nearby) {
        if (item->type() != BlockItem::Type) continue;
        auto* block = static_cast<BlockItem*>(item);

        const int port = block->portNear(scenePos, wantInput);
        if (port < 0) continue;

        const QPointF centre = wantInput ? block->inputScenePos(port)
                                         : block->outputScenePos(port);
        const qreal distance = QLineF(scenePos, centre).length();
        if (!best.valid() || distance < bestDistance) {
            best = {block, port, wantInput};
            bestDistance = distance;
        }
    }
    return best;
}

void DiagramScene::cancelPendingWire() {
    if (!pendingWire_) return;
    removeItem(pendingWire_);
    delete pendingWire_;
    pendingWire_ = nullptr;
    pendingSource_ = {};
}

void DiagramScene::finishPendingWire(const QPointF& scenePos) {
    const PortHit source = pendingSource_;
    cancelPendingWire();
    if (!source.valid()) return;

    // The kind of port wanted is known, so the whole band beside it can be a target.
    const PortHit target = portNear(scenePos, !source.isInput);
    if (!target.valid()) return;

    const PortHit* from = source.isInput ? &target : &source;
    const PortHit* to = source.isInput ? &source : &target;

    if (from->isInput || !to->isInput) {
        emit statusMessage(tr("A wire must connect an output to an input"));
        return;
    }
    if (from->item == to->item) {
        emit statusMessage(tr("A block cannot be wired to itself"));
        return;
    }

    try {
        const Connection& connection = model_->connect(
            from->item->blockId().toStdString(), from->port,
            to->item->blockId().toStdString(), to->port);

        for (int i = wireItems_.size() - 1; i >= 0; --i) {
            WireItem* wire = wireItems_[i];
            if (wire->targetItem() == to->item &&
                wire->targetPort() == to->port) {
                removeItem(wire);
                delete wire;
                wireItems_.removeAt(i);
            }
        }

        addWireItem(connection);
        rerouteWires();
        emit modelEdited();
    } catch (const ModelError& error) {
        emit statusMessage(QString::fromStdString(error.what()));
    }
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (locked_) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const PortHit hit = portAt(event->scenePos());
        if (hit.valid()) {
            pendingSource_ = hit;
            pendingWire_ = new QGraphicsPathItem;
            QPen pen(theme::palette().wirePending, 1.8, Qt::DashLine);
            pen.setCapStyle(Qt::RoundCap);
            pendingWire_->setPen(pen);
            pendingWire_->setZValue(10.0);
            addItem(pendingWire_);
            event->accept();
            return;
        }
        draggingBlocks_ = !selectedBlocks().isEmpty();
    }
    QGraphicsScene::mousePressEvent(event);
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (pendingWire_) {
        const QPointF from = pendingSource_.isInput
                                 ? pendingSource_.item->inputScenePos(
                                       pendingSource_.port)
                                 : pendingSource_.item->outputScenePos(
                                       pendingSource_.port);
        const bool facesLeft = pendingSource_.item->isMirrored();
        pendingWire_->setPath(
            WireItem::route(from, event->scenePos(),
                            pendingSource_.isInput ? !facesLeft : facesLeft,
                            false));
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (pendingWire_) {
        finishPendingWire(event->scenePos());
        event->accept();
        return;
    }

    QGraphicsScene::mouseReleaseEvent(event);

    if (draggingBlocks_) {
        draggingBlocks_ = false;
        bool moved = false;
        for (BlockItem* block : selectedBlocks()) {
            const QPointF snapped = snap(block->pos());
            if (snapped != block->pos()) {
                block->setPos(snapped);
                moved = true;
            }
            block->commitGeometry();
        }
        if (moved || event->button() == Qt::LeftButton) emit modelEdited();
    }
}

void DiagramScene::keyPressEvent(QKeyEvent* event) {
    if (locked_) {
        QGraphicsScene::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelection();
            event->accept();
            return;
        case Qt::Key_Escape:
            cancelPendingWire();
            clearSelection();
            event->accept();
            return;
        default:
            break;
    }

    const QList<BlockItem*> blocks = selectedBlocks();
    if (!blocks.isEmpty()) {
        const qreal amount =
            event->modifiers().testFlag(Qt::AltModifier) ? 1.0 : kGridStep;
        QPointF delta;
        if (event->key() == Qt::Key_Left) delta = QPointF(-amount, 0);
        else if (event->key() == Qt::Key_Right) delta = QPointF(amount, 0);
        else if (event->key() == Qt::Key_Up) delta = QPointF(0, -amount);
        else if (event->key() == Qt::Key_Down) delta = QPointF(0, amount);

        if (!delta.isNull()) {
            for (BlockItem* block : blocks) {
                block->moveBy(delta.x(), delta.y());
                block->commitGeometry();
            }
            emit modelEdited();
            event->accept();
            return;
        }
    }

    QGraphicsScene::keyPressEvent(event);
}

void DiagramScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event) {
    if (locked_) return;

    if (event->mimeData()->hasFormat(kBlockMimeType)) event->acceptProposedAction();
    else QGraphicsScene::dragEnterEvent(event);
}

void DiagramScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event) {
    if (locked_) return;

    if (event->mimeData()->hasFormat(kBlockMimeType)) event->acceptProposedAction();
    else QGraphicsScene::dragMoveEvent(event);
}

void DiagramScene::dropEvent(QGraphicsSceneDragDropEvent* event) {
    if (locked_) return;

    if (!event->mimeData()->hasFormat(kBlockMimeType)) {
        QGraphicsScene::dropEvent(event);
        return;
    }
    const QString typeName =
        QString::fromUtf8(event->mimeData()->data(kBlockMimeType));
    addBlock(typeName, event->scenePos());
    event->acceptProposedAction();
}

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (locked_) {
        QGraphicsScene::mouseDoubleClickEvent(event);
        return;
    }

    for (QGraphicsItem* item : items(event->scenePos())) {
        if (item->type() != WireItem::Type) continue;
        auto* wire = static_cast<WireItem*>(item);
        clearSelection();
        wire->setSelected(true);
        promptSignalName(wire);
        event->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void DiagramScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    if (locked_) {
        event->accept();
        return;
    }

    QGraphicsItem* under = itemAt(event->scenePos(), QTransform());
    if (under && !under->isSelected()) {
        clearSelection();
        under->setSelected(true);
    }

    QMenu menu;
    const QList<BlockItem*> blocks = selectedBlocks();

    if (blocks.size() == 1) {
        QAction* edit = menu.addAction(tr("Edit…"));
        connect(edit, &QAction::triggered, this,
                [this, item = blocks.first()] { emit blockEditRequested(item); });
    }

    if (WireItem* wire = selectedWire()) {
        QAction* rename = menu.addAction(wire->signalName().isEmpty()
                                             ? tr("Name signal…")
                                             : tr("Rename signal…"));
        connect(rename, &QAction::triggered, this,
                [this, wire] { promptSignalName(wire); });

        if (!wire->signalName().isEmpty()) {
            QAction* clear = menu.addAction(tr("Clear signal name"));
            connect(clear, &QAction::triggered, this,
                    [this, wire] { renameSignal(wire, QString()); });
        }
        menu.addSeparator();
    }
    if (!blocks.isEmpty()) {
        QAction* mirror = menu.addAction(tr("Mirror horizontally"));
        connect(mirror, &QAction::triggered, this,
                &DiagramScene::mirrorSelection);

        QAction* group = menu.addAction(tr("Group into subsystem"));
        group->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
        connect(group, &QAction::triggered, this,
                &DiagramScene::groupSelection);

        if (blocks.size() == 1 && isSubsystem(blocks.first()->block())) {
            QAction* ungroup = menu.addAction(tr("Break subsystem open"));
            ungroup->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
            connect(ungroup, &QAction::triggered, this,
                    &DiagramScene::ungroupSelection);
        }
    }

    if (!blocks.isEmpty()) {
        menu.addSeparator();
        QAction* copy = menu.addAction(tr("Copy"));
        copy->setShortcut(QKeySequence::Copy);
        connect(copy, &QAction::triggered, this, &DiagramScene::copySelection);

        QAction* cut = menu.addAction(tr("Cut"));
        cut->setShortcut(QKeySequence::Cut);
        connect(cut, &QAction::triggered, this, &DiagramScene::cutSelection);

        QAction* duplicate = menu.addAction(tr("Duplicate"));
        duplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        connect(duplicate, &QAction::triggered, this,
                &DiagramScene::duplicateSelection);
    }

    if (clipboardHasBlocks()) {
        if (blocks.isEmpty()) menu.addSeparator();
        QAction* paste = menu.addAction(tr("Paste here"));
        paste->setShortcut(QKeySequence::Paste);
        const QPointF where = event->scenePos();
        connect(paste, &QAction::triggered, this,
                [this, where] { pasteAt(where); });
    }
    if (!selectedItems().isEmpty()) {
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Delete"));
        remove->setShortcut(QKeySequence::Delete);
        connect(remove, &QAction::triggered, this,
                &DiagramScene::deleteSelection);
    }

    if (menu.isEmpty()) return;
    menu.exec(event->screenPos());
    event->accept();
}

}
