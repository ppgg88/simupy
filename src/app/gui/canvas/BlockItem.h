#pragma once

#include "model/Model.h"

#include <QGraphicsObject>
#include <QStringList>

namespace simupy {

/// A block drawn on the canvas.
///
/// The item owns no model data: geometry, name and parameters live in the
/// Model, and the item reads them back whenever refresh() is called.
class BlockItem : public QGraphicsObject {
    Q_OBJECT

public:
    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

    BlockItem(Model& model, Block* block, QGraphicsItem* parent = nullptr);

    Block* block() const { return block_; }
    QString blockId() const { return QString::fromStdString(block_->id()); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    QRectF bodyRect() const;

    int inputCount() const { return inputs_.size(); }
    int outputCount() const { return outputs_.size(); }

    QPointF inputScenePos(int index) const;
    QPointF outputScenePos(int index) const;

    int inputPortAt(const QPointF& scenePos) const;
    int outputPortAt(const QPointF& scenePos) const;

    bool isMirrored() const { return mirrored_; }
    void setMirrored(bool mirrored);

    void refresh();

    void commitGeometry();

    void setProblem(const QString& message);
    QString problem() const { return problem_; }

    /// The part of an interactive block that acts as its control — the
    /// slider's track, the switch, the button face. Empty for everything else.
    ///
    /// Deliberately smaller than the block: dragging it operates the control,
    /// dragging anywhere else still moves the block, which is what keeps
    /// direct manipulation from stealing the diagram's basic gesture.
    QRectF controlZone() const;

    /// True while the diagram is read-only. Controls stay live — that is the
    /// point of locking rather than disabling — but the block cannot be moved.
    void setInteractionLocked(bool locked);

signals:
    void geometryChanged();
    void controlChanged();
    void editRequested();

protected:
    bool driveControl(const QPointF& local, bool pressed);

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void paintGlyph(QPainter* painter, const QRectF& body) const;
    void paintPorts(QPainter* painter) const;
    QPointF inputLocalPos(int index) const;
    QPointF outputLocalPos(int index) const;

    Model& model_;
    Block* block_;
    QStringList inputs_;
    QStringList outputs_;
    QString label_;
    QString problem_;
    QSizeF size_{80.0, 60.0};
    bool mirrored_ = false;
    bool hovered_ = false;
    bool locked_ = false;
    bool drivingControl_ = false;
};

}  // namespace simupy
