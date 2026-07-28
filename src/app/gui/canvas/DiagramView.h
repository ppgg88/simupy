#pragma once

#include <QGraphicsView>
#include <QPointer>

namespace simupy {

class QuickAddPopup;

class DiagramView : public QGraphicsView {
    Q_OBJECT

public:
    explicit DiagramView(QWidget* parent = nullptr);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void zoomToFit();

    qreal zoomFactor() const;

    void openQuickAdd(const QString& initialText = {});

    QPointF insertionScenePos() const;

signals:
    void zoomChanged(qreal factor);
    void cursorMoved(const QPointF& scenePos);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void applyZoom(qreal factor, const QPoint& anchor);
    QPoint insertionAnchor() const;

    bool panning_ = false;
    QPoint lastPanPoint_;

    QPointer<QuickAddPopup> quickAdd_;
    QPoint lastCursor_{-1, -1};
};

}
