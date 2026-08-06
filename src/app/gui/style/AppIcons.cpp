#include "AppIcons.h"

#include "Theme.h"

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <functional>

namespace simupy {
namespace appicons {
namespace {

constexpr qreal kGrid = 24.0;

using DrawFn = std::function<void(QPainter&, const QColor&)>;

class DrawnIconEngine : public QIconEngine {
public:
    DrawnIconEngine(DrawFn draw, QColor tint)
        : draw_(std::move(draw)), tint_(tint) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override {
        QColor colour = tint_;
        if (mode == QIcon::Disabled) colour.setAlphaF(0.35f);
        else if (mode == QIcon::Selected) colour = colour.lighter(115);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / kGrid, rect.height() / kGrid);
        draw_(*painter, colour);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode,
                   QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

    QIconEngine* clone() const override {
        return new DrawnIconEngine(draw_, tint_);
    }

private:
    DrawFn draw_;
    QColor tint_;
};

QPen strokePen(const QColor& colour, qreal width = 1.7) {
    QPen pen(colour, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

void stroke(QPainter& p, const QColor& c, const QPainterPath& path,
            qreal width = 1.7) {
    p.setPen(strokePen(c, width));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void fill(QPainter& p, const QColor& c, const QPainterPath& path) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path);
}

/// Strokes `path` minus `mask`, leaving real transparency rather than a fill.
void strokeBehind(QPainter& p, const QColor& c, const QPainterPath& path,
                  const QPainterPath& mask, qreal width = 1.7) {
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(stroker.createStroke(path).subtracted(mask));
}

QPainterPath grown(const QRectF& rect, qreal margin, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(-margin, -margin, margin, margin),
                        radius, radius);
    return path;
}

QPainterPath polygon(std::initializer_list<QPointF> points) {
    QPainterPath path;
    bool first = true;
    for (const QPointF& point : points) {
        if (first) path.moveTo(point);
        else path.lineTo(point);
        first = false;
    }
    path.closeSubpath();
    return path;
}

QPainterPath polyline(std::initializer_list<QPointF> points) {
    QPainterPath path;
    bool first = true;
    for (const QPointF& point : points) {
        if (first) path.moveTo(point);
        else path.lineTo(point);
        first = false;
    }
    return path;
}

QPainterPath sheet() {
    QPainterPath path;
    path.moveTo(6, 3);
    path.lineTo(14, 3);
    path.lineTo(19, 8);
    path.lineTo(19, 21);
    path.lineTo(6, 21);
    path.closeSubpath();
    path.moveTo(14, 3);
    path.lineTo(14, 8);
    path.lineTo(19, 8);
    return path;
}

QPainterPath lens() {
    QPainterPath path;
    path.addEllipse(QPointF(10.5, 10.5), 6.0, 6.0);
    path.moveTo(15.0, 15.0);
    path.lineTo(20.5, 20.5);
    return path;
}

QHash<QString, QIcon>& cache() {
    static QHash<QString, QIcon> icons;
    return icons;
}

QIcon cached(const QString& key, DrawFn draw, const QColor& tint) {
    QHash<QString, QIcon>& icons = cache();
    const auto found = icons.constFind(key);
    if (found != icons.constEnd()) return *found;

    QIcon icon(new DrawnIconEngine(std::move(draw), tint));
    icons.insert(key, icon);
    return icon;
}

QColor ink() {
    return QGuiApplication::palette().color(QPalette::WindowText);
}

}

void invalidateCache() { cache().clear(); }

QIcon run() {
    return cached(
        QStringLiteral("run"),
        [](QPainter& p, const QColor& c) {
            fill(p, c, polygon({{8, 4.5}, {19.5, 12}, {8, 19.5}}));
        },
        theme::palette().success);
}

QIcon stop() {
    return cached(
        QStringLiteral("stop"),
        [](QPainter& p, const QColor& c) {
            QPainterPath path;
            path.addRoundedRect(QRectF(6.5, 6.5, 11, 11), 1.5, 1.5);
            fill(p, c, path);
        },
        theme::palette().error);
}

QIcon realTime() {
    return cached(
        QStringLiteral("realTime"),
        [](QPainter& p, const QColor& c) {
            QPainterPath face;
            face.addEllipse(QPointF(12, 12.5), 8.5, 8.5);
            stroke(p, c, face, 1.6);
            stroke(p, c, polyline({{12, 6.5}, {12, 12.5}}), 1.7);
            stroke(p, c, polyline({{12, 12.5}, {16.5, 15}}), 1.7);
        },
        ink());
}

QIcon newModel() {
    return cached(
        QStringLiteral("new"),
        [](QPainter& p, const QColor& c) { stroke(p, c, sheet()); }, ink());
}

QIcon open() {
    return cached(
        QStringLiteral("open"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c,
                   polyline({{3, 19}, {3, 6}, {10, 6}, {12, 9}, {20, 9}}));
            stroke(p, c, polyline({{3, 19}, {6.5, 12}, {21, 12}, {17.5, 19}}));
            stroke(p, c, polyline({{3, 19}, {17.5, 19}}));
        },
        ink());
}

QIcon save() {
    return cached(
        QStringLiteral("save"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, polyline({{12, 3}, {12, 14}}));
            stroke(p, c, polyline({{7.5, 10}, {12, 14.5}, {16.5, 10}}));
            stroke(p, c, polyline({{4.5, 17}, {4.5, 20.5}, {19.5, 20.5},
                                   {19.5, 17}}));
        },
        ink());
}

namespace {

/// The conventional curved arrow, drawn once and mirrored for redo.
void curvedArrow(QPainter& p, const QColor& c, bool leftward) {
    p.save();
    if (!leftward) {
        p.translate(kGrid, 0);
        p.scale(-1, 1);
    }

    // A shaft pointing left, hooking down to the right: asymmetric, so it does
    // not read as a horseshoe the way a plain half-circle does.
    const QPointF tip(3.5, 8.5);

    QPainterPath hook;
    hook.moveTo(tip);
    hook.lineTo(11, 8.5);
    hook.arcTo(QRectF(4, 8.5, 14, 14), 90, -90);
    stroke(p, c, hook, 2.0);

    stroke(p, c, polyline({{8, 4}, tip, {8, 13}}), 2.0);
    p.restore();
}

}

QIcon undo() {
    return cached(
        QStringLiteral("undo"),
        [](QPainter& p, const QColor& c) { curvedArrow(p, c, true); }, ink());
}

QIcon redo() {
    return cached(
        QStringLiteral("redo"),
        [](QPainter& p, const QColor& c) { curvedArrow(p, c, false); }, ink());
}

QIcon copy() {
    return cached(
        QStringLiteral("copy"),
        [](QPainter& p, const QColor& c) {
            const QRectF frontRect(8, 7, 12, 14);
            QPainterPath back;
            back.addRoundedRect(QRectF(4, 3, 12, 14), 2, 2);
            strokeBehind(p, c, back, grown(frontRect, 1.3, 3), 1.5);

            QPainterPath front;
            front.addRoundedRect(frontRect, 2, 2);
            stroke(p, c, front, 1.5);
        },
        ink());
}

QIcon cut() {
    return cached(
        QStringLiteral("cut"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, polyline({{7, 3}, {16, 16}}), 1.5);
            stroke(p, c, polyline({{17, 3}, {8, 16}}), 1.5);
            QPainterPath blades;
            blades.addEllipse(QPointF(6.5, 18.5), 3.0, 3.0);
            blades.addEllipse(QPointF(17.5, 18.5), 3.0, 3.0);
            stroke(p, c, blades, 1.5);
        },
        ink());
}

QIcon paste() {
    return cached(
        QStringLiteral("paste"),
        [](QPainter& p, const QColor& c) {
            QPainterPath board;
            board.addRoundedRect(QRectF(4.5, 4, 15, 17), 2, 2);
            stroke(p, c, board, 1.5);
            QPainterPath clip;
            clip.addRoundedRect(QRectF(9, 2, 6, 4), 1, 1);
            fill(p, c, clip);
            stroke(p, c, polyline({{8.5, 11}, {15.5, 11}}), 1.4);
            stroke(p, c, polyline({{8.5, 15}, {15.5, 15}}), 1.4);
        },
        ink());
}

QIcon duplicate() {
    return cached(
        QStringLiteral("duplicate"),
        [](QPainter& p, const QColor& c) {
            const QRectF frontRect(9, 9, 11, 11);
            QPainterPath back;
            back.addRoundedRect(QRectF(4, 4, 11, 11), 2, 2);
            strokeBehind(p, c, back, grown(frontRect, 1.3, 3), 1.5);

            QPainterPath front;
            front.addRoundedRect(frontRect, 2, 2);
            stroke(p, c, front, 1.5);
            stroke(p, c, polyline({{14.5, 11.5}, {14.5, 17.5}}), 1.5);
            stroke(p, c, polyline({{11.5, 14.5}, {17.5, 14.5}}), 1.5);
        },
        ink());
}

QIcon remove() {
    return cached(
        QStringLiteral("remove"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, polyline({{4, 6.5}, {20, 6.5}}));
            stroke(p, c, polyline({{9.5, 6.5}, {9.5, 4}, {14.5, 4}, {14.5, 6.5}}));
            stroke(p, c,
                   polyline({{6.5, 6.5}, {7.5, 20.5}, {16.5, 20.5}, {17.5, 6.5}}));
            stroke(p, c, polyline({{10.5, 10}, {10.5, 17}}), 1.4);
            stroke(p, c, polyline({{13.5, 10}, {13.5, 17}}), 1.4);
        },
        ink());
}

QIcon mirror() {
    return cached(
        QStringLiteral("mirror"),
        [](QPainter& p, const QColor& c) {
            QPen dashed = strokePen(c, 1.4);
            dashed.setStyle(Qt::DashLine);
            p.setPen(dashed);
            p.drawLine(QPointF(12, 3), QPointF(12, 21));

            fill(p, c, polygon({{9.5, 7}, {9.5, 17}, {3.5, 12}}));
            stroke(p, c, polygon({{14.5, 7}, {14.5, 17}, {20.5, 12}}), 1.5);
        },
        ink());
}

QIcon addBlock() {
    return cached(
        QStringLiteral("addBlock"),
        [](QPainter& p, const QColor& c) {
            QPainterPath body;
            body.addRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
            stroke(p, c, body, 1.6);
            stroke(p, c, polyline({{12, 8.5}, {12, 15.5}}), 1.9);
            stroke(p, c, polyline({{8.5, 12}, {15.5, 12}}), 1.9);
        },
        theme::palette().accent);
}

QIcon nameSignal() {
    return cached(
        QStringLiteral("nameSignal"),
        [](QPainter& p, const QColor& c) {
            const QRectF label(5, 4, 14, 10);
            QPainterPath wire = polyline({{2, 19}, {22, 19}});
            stroke(p, c, wire, 1.7);
            stroke(p, c, polyline({{12, 14}, {12, 19}}), 1.4);

            QPainterPath box;
            box.addRoundedRect(label, 2, 2);
            stroke(p, c, box, 1.5);
            stroke(p, c, polyline({{8, 7.5}, {16, 7.5}}), 1.3);
            stroke(p, c, polyline({{8, 10.5}, {13, 10.5}}), 1.3);
        },
        ink());
}

QIcon enterSubsystem() {
    return cached(
        QStringLiteral("enter"),
        [](QPainter& p, const QColor& c) {
            QPainterPath box;
            box.addRoundedRect(QRectF(3.5, 3.5, 17, 17), 2.5, 2.5);
            stroke(p, c, box, 1.5);
            stroke(p, c, polyline({{12, 7}, {12, 16}}), 1.8);
            stroke(p, c, polyline({{8, 12}, {12, 16.5}, {16, 12}}), 1.8);
        },
        ink());
}

QIcon leaveSubsystem() {
    return cached(
        QStringLiteral("leave"),
        [](QPainter& p, const QColor& c) {
            QPainterPath box;
            box.addRoundedRect(QRectF(3.5, 3.5, 17, 17), 2.5, 2.5);
            stroke(p, c, box, 1.5);
            stroke(p, c, polyline({{12, 17}, {12, 8}}), 1.8);
            stroke(p, c, polyline({{8, 12}, {12, 7.5}, {16, 12}}), 1.8);
        },
        ink());
}

QIcon zoomIn() {
    return cached(
        QStringLiteral("zoomIn"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, lens(), 1.6);
            stroke(p, c, polyline({{7.5, 10.5}, {13.5, 10.5}}), 1.6);
            stroke(p, c, polyline({{10.5, 7.5}, {10.5, 13.5}}), 1.6);
        },
        ink());
}

QIcon zoomOut() {
    return cached(
        QStringLiteral("zoomOut"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, lens(), 1.6);
            stroke(p, c, polyline({{7.5, 10.5}, {13.5, 10.5}}), 1.6);
        },
        ink());
}

QIcon zoomFit() {
    return cached(
        QStringLiteral("zoomFit"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c, polyline({{3.5, 8.5}, {3.5, 3.5}, {8.5, 3.5}}), 1.7);
            stroke(p, c, polyline({{15.5, 3.5}, {20.5, 3.5}, {20.5, 8.5}}), 1.7);
            stroke(p, c, polyline({{20.5, 15.5}, {20.5, 20.5}, {15.5, 20.5}}), 1.7);
            stroke(p, c, polyline({{8.5, 20.5}, {3.5, 20.5}, {3.5, 15.5}}), 1.7);
            QPainterPath body;
            body.addRoundedRect(QRectF(8.5, 9.5, 7, 5), 1, 1);
            stroke(p, c, body, 1.4);
        },
        ink());
}

QIcon library() {
    return cached(
        QStringLiteral("library"),
        [](QPainter& p, const QColor& c) {
            stroke(p, c,
                   polygon({{12, 2.5}, {21.5, 7}, {12, 11.5}, {2.5, 7}}), 1.5);
            stroke(p, c, polyline({{2.5, 12}, {12, 16.5}, {21.5, 12}}), 1.5);
            stroke(p, c, polyline({{2.5, 16.5}, {12, 21}, {21.5, 16.5}}), 1.5);
        },
        ink());
}

QIcon saveAsBlock() {
    return cached(
        QStringLiteral("saveAsBlock"),
        [](QPainter& p, const QColor& c) {
            QPainterPath body;
            body.addRoundedRect(QRectF(7, 2.5, 10, 8), 2, 2);
            stroke(p, c, body, 1.5);

            stroke(p, c, polyline({{12, 10.5}, {12, 16.5}}), 1.7);
            stroke(p, c, polyline({{8.5, 13}, {12, 16.8}, {15.5, 13}}), 1.7);
            stroke(p, c, polyline({{4.5, 19}, {4.5, 21.5}, {19.5, 21.5},
                                   {19.5, 19}}), 1.6);
        },
        theme::palette().accent);
}

QIcon settings() {
    return cached(
        QStringLiteral("settings"),
        [](QPainter& p, const QColor& c) {
            QPainterPath knobs;
            knobs.addEllipse(QPointF(9, 7), 2.7, 2.7);
            knobs.addEllipse(QPointF(15, 17), 2.7, 2.7);

            QPainterPath rails;
            rails.moveTo(3.5, 7);
            rails.lineTo(20.5, 7);
            rails.moveTo(3.5, 17);
            rails.lineTo(20.5, 17);
            strokeBehind(p, c, rails, knobs, 1.6);
            stroke(p, c, knobs, 1.6);
        },
        ink());
}

}
}
