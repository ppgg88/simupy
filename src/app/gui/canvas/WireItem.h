#pragma once

#include "WireRouter.h"

#include <QGraphicsPathItem>
#include <QString>

namespace simupy {

class BlockItem;

class WireItem : public QGraphicsPathItem {
public:
    enum { Type = QGraphicsItem::UserType + 2 };
    int type() const override { return Type; }

    WireItem(QString connectionId, BlockItem* source, int sourcePort,
             BlockItem* target, int targetPort);

    const QString& connectionId() const { return connectionId_; }
    BlockItem* sourceItem() const { return source_; }
    BlockItem* targetItem() const { return target_; }
    int sourcePort() const { return sourcePort_; }
    int targetPort() const { return targetPort_; }

    /// Recomputes the route around `obstacles`, avoiding the lanes already
    /// taken in `placed` and adding itself to them. Called whenever either
    /// end moves; with no obstacles and no field this is the plain two-bend
    /// route.
    void updatePath(const QVector<QRectF>& obstacles = {},
                    WireField* placed = nullptr);

    WireField::SignalKey signalKey() const;

    void setSignalWidth(int width);

    void setSignalName(const QString& name);
    const QString& signalName() const { return signalName_; }

    QPainterPath shape() const override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    static QPainterPath route(const QPointF& from, const QPointF& to,
                              bool sourceFacesLeft, bool targetFacesLeft);

private:
    QString connectionId_;
    BlockItem* source_;
    BlockItem* target_;
    int sourcePort_;
    int targetPort_;
    int signalWidth_ = 0;
    QString signalName_;
};

}  // namespace simupy
