#pragma once

#include "model/Model.h"
#include "engine/Scheduler.h"

#include <QGraphicsScene>
#include <QHash>
#include <QList>

class QGraphicsPathItem;

namespace simupy {

class BlockItem;
class WireItem;

class DiagramScene : public QGraphicsScene {
    Q_OBJECT

public:
    static const char* kBlockMimeType;

    explicit DiagramScene(Model& model, QObject* parent = nullptr);

    Model& model() { return *model_; }

    void setModel(Model& model);

    void rebuild();

    BlockItem* itemForBlock(const QString& blockId) const;
    BlockItem* addBlock(const QString& typeName, const QPointF& scenePos);

    QList<BlockItem*> selectedBlocks() const;
    void deleteSelection();
    void mirrorSelection();
    void selectAll();

    void copySelection();
    void cutSelection();

    void pasteAt(const QPointF& scenePos);

    void duplicateSelection();

    void groupSelection();

    void ungroupSelection();

    static bool clipboardHasBlocks();

    void renameSignal(WireItem* wire, const QString& name);

    WireItem* selectedWire() const;

    void promptSignalName(WireItem* wire);

    void refreshBlock(const QString& blockId);

    void applySignalWidths(const CompiledModel& compiled);
    void clearSignalWidths();

    /// Repaints the blocks that show a live value, such as Display.
    void refreshValues();

    void setBlockProblem(const QString& blockId, const QString& message);
    void clearBlockProblems();

    void setInteractionLocked(bool locked);
    bool isInteractionLocked() const { return locked_; }

    static constexpr qreal kGridStep = 10.0;

signals:
    void modelEdited();
    void blockEditRequested(BlockItem* item);
    void statusMessage(const QString& message);
    void controlChanged();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dropEvent(QGraphicsSceneDragDropEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    struct PortHit {
        BlockItem* item = nullptr;
        int port = -1;
        bool isInput = false;
        bool valid() const { return item != nullptr && port >= 0; }
    };

    PortHit portAt(const QPointF& scenePos) const;
    PortHit portNear(const QPointF& scenePos, bool wantInput) const;
    void addWireItem(const Connection& connection);
    void rerouteWires();
    void finishPendingWire(const QPointF& scenePos);
    void cancelPendingWire();
    static QPointF snap(const QPointF& point);

    Model* model_;
    QHash<QString, BlockItem*> blockItems_;
    QList<WireItem*> wireItems_;
    QVector<QRectF> obstacles_;

    QGraphicsPathItem* pendingWire_ = nullptr;
    PortHit pendingSource_;
    bool draggingBlocks_ = false;
    bool locked_ = false;
};

}
