#pragma once

#include <QGraphicsView>
#include <QPointer>

namespace simupy {

class QuickAddPopup;

/// Canvas viewport: wheel zoom around the cursor, middle-button panning and
/// the usual fit/reset commands.
///
/// Also owns the type-to-insert search: typing a letter over the canvas opens
/// a block search at the cursor, so the common case of adding a block never
/// requires crossing the window to the palette.
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
    /// Where a quick-added block should land: the last cursor position inside
    /// the viewport, or the middle of the view when there has not been one.
    QPoint insertionAnchor() const;

    bool panning_ = false;
    QPoint lastPanPoint_;

    QPointer<QuickAddPopup> quickAdd_;
    QPoint lastCursor_{-1, -1};
};

}  // namespace simupy
