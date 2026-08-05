#include "DiagramView.h"

#include "BlockItem.h"
#include "DiagramScene.h"
#include "QuickAddPopup.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace simupy {
namespace {

constexpr qreal kMinZoom = 0.15;
constexpr qreal kMaxZoom = 6.0;
constexpr qreal kZoomStep = 1.15;

}

DiagramView::DiagramView(QWidget* parent) : QGraphicsView(parent) {
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAcceptDrops(true);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
}

qreal DiagramView::zoomFactor() const { return transform().m11(); }

void DiagramView::applyZoom(qreal factor, const QPoint& anchor) {
    const qreal current = zoomFactor();
    qreal target = current * factor;
    target = std::clamp(target, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(target, current)) return;

    const QPointF before = mapToScene(anchor);
    scale(target / current, target / current);
    const QPointF after = mapToScene(anchor);
    translate(after.x() - before.x(), after.y() - before.y());

    emit zoomChanged(zoomFactor());
}

void DiagramView::zoomIn() {
    applyZoom(kZoomStep, viewport()->rect().center());
}

void DiagramView::zoomOut() {
    applyZoom(1.0 / kZoomStep, viewport()->rect().center());
}

void DiagramView::resetZoom() {
    resetTransform();
    emit zoomChanged(zoomFactor());
}

void DiagramView::zoomToFit() {
    if (!scene()) return;
    const QRectF bounds = scene()->itemsBoundingRect();
    if (bounds.isEmpty()) {
        resetZoom();
        return;
    }
    fitInView(bounds.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);

    if (zoomFactor() > 1.6) resetTransform();
    emit zoomChanged(zoomFactor());
}

void DiagramView::wheelEvent(QWheelEvent* event) {
    const bool zoomGesture = event->modifiers().testFlag(Qt::ControlModifier) ||
                             event->modifiers().testFlag(Qt::MetaModifier);
    if (zoomGesture || event->angleDelta().x() == 0) {
        const int delta = event->angleDelta().y();
        if (delta != 0 && zoomGesture) {
            applyZoom(delta > 0 ? kZoomStep : 1.0 / kZoomStep,
                      event->position().toPoint());
            event->accept();
            return;
        }
    }
    QGraphicsView::wheelEvent(event);
}

void DiagramView::mousePressEvent(QMouseEvent* event) {
    const bool panGesture =
        event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers().testFlag(Qt::ShiftModifier));

    lastCursor_ = event->pos();

    if (panGesture) {
        panning_ = true;
        lastPanPoint_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

QPoint DiagramView::insertionAnchor() const {
    return viewport()->rect().contains(lastCursor_)
               ? lastCursor_
               : viewport()->rect().center();
}

QPointF DiagramView::insertionScenePos() const {
    return mapToScene(insertionAnchor());
}

void DiagramView::openQuickAdd(const QString& initialText) {
    auto* diagram = qobject_cast<DiagramScene*>(scene());
    if (!diagram) return;

    if (!quickAdd_) {
        quickAdd_ = new QuickAddPopup(this);
        connect(quickAdd_, &QuickAddPopup::blockChosen, this,
                [this](const QString& typeName) {
                    auto* target = qobject_cast<DiagramScene*>(scene());
                    if (!target) return;

                    BlockItem* added =
                        target->addBlock(typeName, mapToScene(insertionAnchor()));
                    if (!added) return;

                    target->clearSelection();
                    added->setSelected(true);
                    setFocus(Qt::OtherFocusReason);
                });
    }

    quickAdd_->start(viewport()->mapToGlobal(insertionAnchor()), initialText);
}

void DiagramView::keyPressEvent(QKeyEvent* event) {
    const QString text = event->text();
    const bool plainTyping =
        !event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::AltModifier) &&
        !event->modifiers().testFlag(Qt::MetaModifier) && text.size() == 1 &&
        text.at(0).isPrint() && !text.at(0).isSpace();

    if (plainTyping) {
        openQuickAdd(text);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void DiagramView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPoint delta = event->pos() - lastPanPoint_;
        lastPanPoint_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                        delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    lastCursor_ = event->pos();
    emit cursorMoved(mapToScene(event->pos()));
    QGraphicsView::mouseMoveEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        panning_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void DiagramView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);
}

}
