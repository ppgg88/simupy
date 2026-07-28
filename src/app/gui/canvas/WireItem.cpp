#include "WireItem.h"

#include "BlockItem.h"
#include "app/gui/style/Theme.h"
#include "WireRouter.h"

#include <QPainter>
#include <QPainterPathStroker>

namespace simupy {

WireItem::WireItem(QString connectionId, BlockItem* source, int sourcePort,
                   BlockItem* target, int targetPort)
    : connectionId_(std::move(connectionId)),
      source_(source),
      target_(target),
      sourcePort_(sourcePort),
      targetPort_(targetPort) {
    setFlags(ItemIsSelectable);
    setZValue(-1.0);
    setAcceptHoverEvents(true);
    updatePath();
}

QPainterPath WireItem::route(const QPointF& from, const QPointF& to,
                             bool sourceFacesLeft, bool targetFacesLeft) {
    return WireRouter::directRoute(from, to, sourceFacesLeft, targetFacesLeft);
}

WireField::SignalKey WireItem::signalKey() const {
    return reinterpret_cast<WireField::SignalKey>(source_) * 131u +
           static_cast<WireField::SignalKey>(sourcePort_);
}

void WireItem::updatePath(const QVector<QRectF>& obstacles,
                          WireField* placed) {
    if (!source_ || !target_) return;

    const QPointF from = source_->outputScenePos(sourcePort_);
    const QPointF to = target_->inputScenePos(targetPort_);
    const QPainterPath routed =
        WireRouter::route(from, to, source_->isMirrored(),
                          target_->isMirrored(), obstacles, placed,
                          signalKey());
    setPath(routed);

    if (placed) placed->add(routed, signalKey());
}

void WireItem::setSignalWidth(int width) {
    if (signalWidth_ == width) return;
    signalWidth_ = width;
    update();
}

void WireItem::setSignalName(const QString& name) {
    if (signalName_ == name) return;
    // The label sits outside the stroked path, so the extent has to be re-announced.
    prepareGeometryChange();
    signalName_ = name;
    update();
}

QRectF WireItem::boundingRect() const {
    QRectF bounds = QGraphicsPathItem::boundingRect();
    return bounds.adjusted(-40.0, -18.0, 40.0, 18.0);
}

QPainterPath WireItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(9.0);
    return stroker.createStroke(path());
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*,
                     QWidget*) {
    const theme::Palette& colors = theme::palette();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = isSelected();
    QPen pen(selected ? colors.wireSelected : colors.wire);
    pen.setWidthF(signalWidth_ > 1 ? 2.6 : 1.6);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    if (signalWidth_ > 1) {
        const QPointF label = path().pointAtPercent(0.45);
        QFont font = painter->font();
        font.setPointSize(7);
        painter->setFont(font);
        painter->setPen(colors.wire);
        painter->drawText(QRectF(label.x() - 14, label.y() - 15, 28, 12),
                          Qt::AlignCenter, QString::number(signalWidth_));
    }

    if (!signalName_.isEmpty()) {
        const QPointF anchor = path().pointAtPercent(0.5);
        QFont font = painter->font();
        font.setPointSize(7);
        font.setItalic(true);
        painter->setFont(font);

        const QFontMetricsF metrics(font);
        const qreal width = metrics.horizontalAdvance(signalName_) + 6.0;
        const QRectF plate(anchor.x() - width / 2.0, anchor.y() + 2.0, width,
                           metrics.height() + 1.0);

        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.canvas);
        painter->drawRoundedRect(plate, 2.0, 2.0);

        painter->setPen(selected ? colors.wireSelected : colors.wire);
        painter->drawText(plate, Qt::AlignCenter, signalName_);
    }
}

}
