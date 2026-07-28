#include "BlockItem.h"

#include "app/gui/style/BlockIconRenderer.h"
#include "app/gui/style/Theme.h"
#include "io/CustomBlock.h"
#include "blocks/ControlBlocks.h"

#include <QGraphicsScene>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>

namespace simupy {
namespace {

constexpr qreal kPortLength = 7.0;
constexpr qreal kPortHalfWidth = 4.5;
constexpr qreal kPortGrab = 11.0;
constexpr qreal kDropReach = 16.0;
constexpr qreal kDropInset = 10.0;
constexpr qreal kDropEdge = 6.0;
constexpr qreal kLabelHeight = 16.0;
constexpr qreal kCornerRadius = 4.0;

QString formatNumberList(const std::vector<double>& values, int limit = 4) {
    QStringList parts;
    for (std::size_t i = 0; i < values.size() && i < std::size_t(limit); ++i) {
        QString text = QString::number(values[i], 'g', 4);
        parts << text;
    }
    if (values.size() > std::size_t(limit)) parts << QStringLiteral("…");
    return parts.join(QStringLiteral(" "));
}

QString polynomialText(const std::vector<double>& coefficients,
                       const QString& variable) {
    const int degree = static_cast<int>(coefficients.size()) - 1;
    QString text;
    for (int i = 0; i <= degree; ++i) {
        const double value = coefficients[i];
        if (value == 0.0) continue;
        const int power = degree - i;

        if (!text.isEmpty()) text += value < 0 ? " - " : " + ";
        else if (value < 0) text += "-";

        const double magnitude = std::abs(value);
        if (magnitude != 1.0 || power == 0)
            text += QString::number(magnitude, 'g', 4);
        if (power >= 1) text += variable;
        if (power >= 2) text += "^" + QString::number(power);
    }
    return text.isEmpty() ? QStringLiteral("0") : text;
}

void drawFittedText(QPainter* painter, const QRectF& rect, const QString& text,
                    int maxPointSize, int alignment = Qt::AlignCenter) {
    if (text.isEmpty()) return;
    QFont font = painter->font();
    for (int size = maxPointSize; size >= 5; --size) {
        font.setPointSize(size);
        const QFontMetricsF metrics(font);
        if (metrics.horizontalAdvance(text) <= rect.width() &&
            metrics.height() <= rect.height())
            break;
    }
    painter->setFont(font);
    painter->drawText(rect, alignment, text);
}

QRectF switchFace(const QRectF& body) {
    return QRectF(body.center().x() - body.width() * 0.28,
                  body.center().y() - body.height() * 0.34,
                  body.width() * 0.56, body.height() * 0.5);
}

qreal sliderTrackY(const QRectF& body) {
    return body.center().y() - body.height() * 0.12;
}

QRectF sliderBand(const QRectF& body) {
    const qreal y = sliderTrackY(body);
    return QRectF(body.left(), y - 8.0, body.width(), 16.0);
}

}

BlockItem::BlockItem(Model& model, Block* block, QGraphicsItem* parent)
    : QGraphicsObject(parent), model_(model), block_(block) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCacheMode(DeviceCoordinateCache);

    refresh();
    const BlockGeometry& geometry = model_.geometry(block_->id());
    setPos(geometry.x, geometry.y);
}

void BlockItem::refresh() {
    prepareGeometryChange();

    const BlockGeometry& geometry = model_.geometry(block_->id());
    size_ = QSizeF(geometry.width, geometry.height);
    mirrored_ = geometry.mirrored;
    label_ = QString::fromStdString(block_->name());

    inputs_.clear();
    outputs_.clear();
    try {
        const PortLayout layout = block_->ports();
        for (const std::string& name : layout.inputs)
            inputs_ << QString::fromStdString(name);
        for (const std::string& name : layout.outputs)
            outputs_ << QString::fromStdString(name);
    } catch (const ModelError& error) {
        // A parameter deciding the port count can be unreadable too.
        noteProblem(QString::fromStdString(error.what()));
    }

    const int maxPorts = std::max(inputs_.size(), outputs_.size());
    const qreal minimumHeight = std::max(40.0, 18.0 * maxPorts + 12.0);
    if (size_.height() < minimumHeight) {
        size_.setHeight(minimumHeight);
        model_.geometry(block_->id()).height = minimumHeight;
    }

    update();
    emit geometryChanged();
}

void BlockItem::setMirrored(bool mirrored) {
    if (mirrored_ == mirrored) return;
    prepareGeometryChange();
    mirrored_ = mirrored;
    model_.geometry(block_->id()).mirrored = mirrored;
    update();
    emit geometryChanged();
}

void BlockItem::noteProblem(const QString& message) {
    // No update() here: this is reached from paint(), which would recurse.
    problem_ = message;
    setToolTip(message);
}

void BlockItem::setProblem(const QString& message) {
    if (problem_ == message) return;
    problem_ = message;
    setToolTip(message);
    update();
}

QRectF BlockItem::bodyRect() const {
    return QRectF(0.0, 0.0, size_.width(), size_.height());
}

QRectF BlockItem::boundingRect() const {
    return bodyRect().adjusted(-kPortLength - 2.0, -2.0, kPortLength + 2.0,
                               kLabelHeight + 2.0);
}

QPainterPath BlockItem::shape() const {
    QPainterPath path;
    path.addRoundedRect(bodyRect(), kCornerRadius, kCornerRadius);
    return path;
}

QPointF BlockItem::inputLocalPos(int index) const {
    if (index < 0 || index >= inputs_.size()) return {};
    const qreal spacing = size_.height() / (inputs_.size() + 1);
    const qreal y = spacing * (index + 1);
    return mirrored_ ? QPointF(size_.width() + kPortLength, y)
                     : QPointF(-kPortLength, y);
}

QPointF BlockItem::outputLocalPos(int index) const {
    if (index < 0 || index >= outputs_.size()) return {};
    const qreal spacing = size_.height() / (outputs_.size() + 1);
    const qreal y = spacing * (index + 1);
    return mirrored_ ? QPointF(-kPortLength, y)
                     : QPointF(size_.width() + kPortLength, y);
}

QPointF BlockItem::inputScenePos(int index) const {
    return mapToScene(inputLocalPos(index));
}

QPointF BlockItem::outputScenePos(int index) const {
    return mapToScene(outputLocalPos(index));
}

int BlockItem::inputPortAt(const QPointF& scenePos) const {
    const QPointF local = mapFromScene(scenePos);
    for (int i = 0; i < inputs_.size(); ++i)
        if (QLineF(local, inputLocalPos(i)).length() <= kPortGrab) return i;
    return -1;
}

int BlockItem::outputPortAt(const QPointF& scenePos) const {
    const QPointF local = mapFromScene(scenePos);
    for (int i = 0; i < outputs_.size(); ++i)
        if (QLineF(local, outputLocalPos(i)).length() <= kPortGrab) return i;
    return -1;
}

QRectF BlockItem::portDropZone(int index, bool input) const {
    const int count = input ? inputs_.size() : outputs_.size();
    if (index < 0 || index >= count) return {};

    const qreal spacing = size_.height() / (count + 1);
    const qreal centre = spacing * (index + 1);
    const qreal top = index == 0 ? -kDropEdge : centre - spacing / 2.0;
    const qreal bottom =
        index == count - 1 ? size_.height() + kDropEdge : centre + spacing / 2.0;

    const bool onLeft = input != mirrored_;
    const qreal outer = onLeft ? -kPortLength - kDropReach
                               : size_.width() + kPortLength + kDropReach;
    const qreal inner = onLeft ? kDropInset : size_.width() - kDropInset;

    return QRectF(QPointF(std::min(outer, inner), top),
                  QPointF(std::max(outer, inner), bottom));
}

int BlockItem::portNear(const QPointF& scenePos, bool input) const {
    const QPointF local = mapFromScene(scenePos);
    const int count = input ? inputs_.size() : outputs_.size();

    for (int i = 0; i < count; ++i)
        if (portDropZone(i, input).contains(local)) return i;
    return -1;
}

qreal BlockItem::portDropReach() { return kPortLength + kDropReach; }

void BlockItem::commitGeometry() {
    BlockGeometry& geometry = model_.geometry(block_->id());
    geometry.x = pos().x();
    geometry.y = pos().y();
    geometry.width = size_.width();
    geometry.height = size_.height();
    geometry.mirrored = mirrored_;
}

void BlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*,
                      QWidget*) {
    const theme::Palette& colors = theme::palette();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF body = bodyRect();
    const bool selected = isSelected();

    QPen pen(selected ? colors.blockStrokeSelected
                      : (problem_.isEmpty() ? colors.blockStroke
                                            : colors.blockError));
    pen.setWidthF(selected ? 2.0 : 1.4);
    painter->setPen(pen);
    painter->setBrush(hovered_ ? colors.blockFillHover : colors.blockFill);
    painter->drawRoundedRect(body, kCornerRadius, kCornerRadius);

    painter->setPen(colors.blockGlyph);
    paintGlyph(painter, body.adjusted(6, 5, -6, -5));

    paintPorts(painter);

    painter->setPen(colors.blockText);
    QFont font = painter->font();
    font.setPointSize(8);
    font.setBold(false);
    painter->setFont(font);
    const QRectF labelRect(-kPortLength, body.bottom() + 1.0,
                           body.width() + 2 * kPortLength, kLabelHeight);
    const QString elided =
        QFontMetricsF(font).elidedText(label_, Qt::ElideRight,
                                       labelRect.width());
    painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);

    if (!problem_.isEmpty()) {
        painter->setBrush(colors.blockError);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(body.right() - 5, body.top() + 5), 4, 4);
    }
}

void BlockItem::paintPorts(QPainter* painter) const {
    const theme::Palette& colors = theme::palette();
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.port);

    const qreal direction = mirrored_ ? -1.0 : 1.0;

    for (int i = 0; i < inputs_.size(); ++i) {
        const QPointF base = inputLocalPos(i);
        const QPointF apex = base + QPointF(kPortLength * direction, 0.0);
        QPolygonF arrow;
        arrow << apex << QPointF(base.x(), base.y() - kPortHalfWidth)
              << QPointF(base.x(), base.y() + kPortHalfWidth);
        painter->drawPolygon(arrow);
    }

    for (int i = 0; i < outputs_.size(); ++i) {
        const QPointF tip = outputLocalPos(i);
        const QPointF root = tip - QPointF(kPortLength * direction, 0.0);
        painter->setPen(QPen(colors.port, 1.5));
        painter->drawLine(root, tip);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(tip, 2.6, 2.6);
    }
}

void BlockItem::paintGlyph(QPainter* painter, const QRectF& body) const {
    const theme::Palette& colors = theme::palette();
    std::string type = block_->typeName();
    const Parameters& params = block_->params();

    if (const CustomBlockDef* def =
            LibraryManager::instance().definition(type)) {
        if (!def->icon.empty()) {
            painter->save();
            icons::paint(painter, body, def->icon, colors.blockGlyph);
            painter->restore();
            return;
        }
        type = def->kind == CustomBlockKind::Python ? "PythonFunction"
                                                    : "Subsystem";
    }

    painter->save();
    if (mirrored_) {
        painter->translate(body.center());
        painter->scale(-1.0, 1.0);
        painter->translate(-body.center());
    }

    auto strokePath = [&](const QPainterPath& path, qreal width = 1.6) {
        painter->setPen(QPen(colors.blockGlyph, width));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };

    if (type == "Gain") {
        QPainterPath triangle;
        triangle.moveTo(body.left(), body.top());
        triangle.lineTo(body.right(), body.center().y());
        triangle.lineTo(body.left(), body.bottom());
        triangle.closeSubpath();
        strokePath(triangle);

        painter->restore();
        painter->save();
        const QRectF textRect = body.adjusted(2, 0, -body.width() * 0.4, 0);
        drawFittedText(painter, textRect,
                       formatNumberList(params.vectorOr("gain", {1.0}), 2), 10);
        painter->restore();
        return;
    }

    if (type == "Sum") {
        painter->setPen(QPen(colors.blockGlyph, 1.4));
        painter->drawEllipse(body.center(),
                             std::min(body.width(), body.height()) * 0.36,
                             std::min(body.width(), body.height()) * 0.36);
        painter->restore();

        const QString signs =
            QString::fromStdString(params.textOr("signs", "++"));
        QFont font = painter->font();
        font.setPointSize(9);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(colors.blockGlyph);
        for (int i = 0; i < signs.size() && i < inputs_.size(); ++i) {
            const QPointF port = inputLocalPos(i);
            const qreal inset = mirrored_ ? -16.0 : 10.0;
            painter->drawText(
                QRectF(port.x() + inset - 6, port.y() - 8, 12, 16),
                Qt::AlignCenter, signs.mid(i, 1));
        }
        return;
    }

    if (type == "Integrator") {
        painter->restore();
        painter->save();
        QFont font = painter->font();
        font.setPointSize(14);
        painter->setFont(font);
        painter->setPen(colors.blockGlyph);
        painter->drawText(body, Qt::AlignCenter, QStringLiteral("1⁄s"));
        painter->restore();
        return;
    }

    if (type == "Derivative") {
        painter->restore();
        painter->save();
        drawFittedText(painter, body, QStringLiteral("du⁄dt"), 12);
        painter->restore();
        return;
    }

    if (type == "TransferFcn" || type == "DiscreteTransferFcn") {
        const QString variable =
            type == "TransferFcn" ? QStringLiteral("s") : QStringLiteral("z");
        const QString numerator =
            polynomialText(params.vectorOr("numerator", {1.0}), variable);
        const QString denominator =
            polynomialText(params.vectorOr("denominator", {1.0, 1.0}), variable);

        painter->restore();
        painter->save();
        const qreal half = body.height() / 2.0;
        drawFittedText(painter,
                       QRectF(body.left(), body.top(), body.width(), half - 1),
                       numerator, 9);
        drawFittedText(painter,
                       QRectF(body.left(), body.center().y() + 1, body.width(),
                              half - 1),
                       denominator, 9);
        painter->setPen(QPen(colors.blockGlyph, 1.2));
        painter->drawLine(QPointF(body.left() + 2, body.center().y()),
                          QPointF(body.right() - 2, body.center().y()));
        painter->restore();
        return;
    }

    if (type == "Constant") {
        painter->restore();
        painter->save();
        drawFittedText(painter, body,
                       formatNumberList(params.vectorOr("value", {1.0})), 12);
        painter->restore();
        return;
    }

    if (type == "Step") {
        QPainterPath path;
        path.moveTo(body.left(), body.bottom() - 2);
        path.lineTo(body.center().x(), body.bottom() - 2);
        path.lineTo(body.center().x(), body.top() + 2);
        path.lineTo(body.right(), body.top() + 2);
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "Ramp") {
        QPainterPath path;
        path.moveTo(body.left(), body.bottom() - 2);
        path.lineTo(body.center().x() - body.width() * 0.1, body.bottom() - 2);
        path.lineTo(body.right(), body.top() + 2);
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "Sine" || type == "Chirp") {
        QPainterPath path;
        const int steps = 48;
        const qreal cycles = type == "Sine" ? 1.6 : 2.6;
        for (int i = 0; i <= steps; ++i) {
            const qreal fraction = qreal(i) / steps;
            const qreal phase = type == "Sine"
                                    ? fraction
                                    : fraction * fraction;
            const qreal x = body.left() + fraction * body.width();
            const qreal y = body.center().y() -
                            std::sin(phase * cycles * 2.0 * kPi) *
                                body.height() * 0.36;
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "Pulse") {
        QPainterPath path;
        const qreal top = body.top() + 2;
        const qreal bottom = body.bottom() - 2;
        const qreal quarter = body.width() / 4.0;
        path.moveTo(body.left(), bottom);
        path.lineTo(body.left() + quarter * 0.6, bottom);
        path.lineTo(body.left() + quarter * 0.6, top);
        path.lineTo(body.left() + quarter * 1.8, top);
        path.lineTo(body.left() + quarter * 1.8, bottom);
        path.lineTo(body.left() + quarter * 2.9, bottom);
        path.lineTo(body.left() + quarter * 2.9, top);
        path.lineTo(body.right(), top);
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "Clock") {
        painter->setPen(QPen(colors.blockGlyph, 1.4));
        const qreal radius = std::min(body.width(), body.height()) * 0.36;
        painter->drawEllipse(body.center(), radius, radius);
        painter->drawLine(body.center(),
                          body.center() + QPointF(0, -radius * 0.7));
        painter->drawLine(body.center(),
                          body.center() + QPointF(radius * 0.55, 0));
        painter->restore();
        return;
    }

    if (type == "Saturation") {
        QPainterPath path;
        path.moveTo(body.left(), body.bottom() - 2);
        path.lineTo(body.left() + body.width() * 0.3, body.bottom() - 2);
        path.lineTo(body.right() - body.width() * 0.3, body.top() + 2);
        path.lineTo(body.right(), body.top() + 2);
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "DeadZone") {
        QPainterPath path;
        path.moveTo(body.left(), body.center().y() - body.height() * 0.3);
        path.lineTo(body.center().x() - body.width() * 0.15, body.center().y());
        path.lineTo(body.center().x() + body.width() * 0.15, body.center().y());
        path.lineTo(body.right(), body.center().y() + body.height() * 0.3);
        strokePath(path);
        painter->restore();
        return;
    }

    if (type == "Scope") {
        painter->setPen(QPen(colors.blockGlyph, 1.3));
        painter->setBrush(Qt::NoBrush);
        const QRectF screen = body.adjusted(2, 2, -2, -2);
        painter->drawRoundedRect(screen, 2, 2);
        QPainterPath trace;
        const int steps = 24;
        for (int i = 0; i <= steps; ++i) {
            const qreal fraction = qreal(i) / steps;
            const qreal x = screen.left() + 3 + fraction * (screen.width() - 6);
            const qreal y = screen.center().y() -
                            std::sin(fraction * 2.4 * kPi) *
                                screen.height() * 0.28;
            if (i == 0) trace.moveTo(x, y);
            else trace.lineTo(x, y);
        }
        painter->setPen(QPen(colors.accent, 1.4));
        painter->drawPath(trace);
        painter->restore();
        return;
    }

    if (type == "Mux" || type == "Demux") {
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.blockGlyph);
        painter->drawRoundedRect(body.adjusted(body.width() * 0.28, 0,
                                               -body.width() * 0.28, 0),
                                 2, 2);
        painter->restore();
        return;
    }

    if (type == "Switch" || type == "ManualSwitch") {
        painter->setPen(QPen(colors.blockGlyph, 1.5));
        const QPointF pivot(body.left() + 3, body.center().y());
        painter->drawLine(pivot, QPointF(body.right() - 3, body.top() + 4));
        painter->setPen(QPen(colors.blockGlyph, 1.0, Qt::DotLine));
        painter->drawLine(pivot, QPointF(body.right() - 3, body.bottom() - 4));
        painter->restore();
        return;
    }

    if (type == "UnitDelay" || type == "IntegerDelay") {
        painter->restore();
        painter->save();
        const QString text =
            type == "UnitDelay"
                ? QStringLiteral("1⁄z")
                : QStringLiteral("z⁻ⁿ");
        drawFittedText(painter, body, text, 14);
        painter->restore();
        return;
    }

    if (type == "Subsystem") {
        painter->setPen(QPen(colors.blockGlyph, 1.2));
        painter->setBrush(Qt::NoBrush);
        const QRectF outer = body.adjusted(body.width() * 0.14,
                                           body.height() * 0.18,
                                           -body.width() * 0.14,
                                           -body.height() * 0.30);
        painter->drawRoundedRect(outer, 2, 2);

        const qreal cell = outer.height() * 0.34;
        painter->setBrush(colors.blockGlyph);
        painter->setPen(Qt::NoPen);
        painter->drawRect(QRectF(outer.left() + 4, outer.center().y() - cell / 2,
                                 cell * 0.9, cell));
        painter->drawRect(QRectF(outer.right() - 4 - cell * 0.9,
                                 outer.center().y() - cell / 2, cell * 0.9,
                                 cell));
        painter->setPen(QPen(colors.blockGlyph, 1.1));
        painter->drawLine(
            QPointF(outer.left() + 4 + cell * 0.9, outer.center().y()),
            QPointF(outer.right() - 4 - cell * 0.9, outer.center().y()));

        painter->restore();
        painter->save();
        drawFittedText(painter,
                       QRectF(body.left(), body.bottom() - body.height() * 0.28,
                              body.width(), body.height() * 0.28),
                       QStringLiteral("%1 in · %2 out")
                           .arg(inputs_.size())
                           .arg(outputs_.size()),
                       7);
        painter->restore();
        return;
    }

    if (type == "Slider") {
        const auto* interactive = dynamic_cast<const InteractiveBlock*>(block_);
        const ControlDescriptor d =
            interactive ? interactive->descriptor() : ControlDescriptor{};
        const double minimum = d.minimum;
        const double maximum = d.maximum;
        const double value =
            interactive ? interactive->liveValue() : params.realOr("value", 0.0);
        const double fraction =
            maximum > minimum
                ? std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0)
                : 0.0;

        const qreal trackY = sliderTrackY(body);
        painter->setPen(QPen(colors.blockGlyph, 1.6));
        painter->drawLine(QPointF(body.left() + 3, trackY),
                          QPointF(body.right() - 3, trackY));

        const qreal x = body.left() + 3 + fraction * (body.width() - 6);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.accent);
        painter->drawRoundedRect(QRectF(x - 2.5, trackY - 6, 5, 12), 2, 2);

        painter->restore();
        painter->save();
        drawFittedText(painter,
                       QRectF(body.left(), body.center().y() + body.height() * 0.08,
                              body.width(), body.height() * 0.4),
                       formatNumberList({value}, 1), 8);
        painter->restore();
        return;
    }

    if (type == "Toggle" || type == "PushButton") {
        const auto* interactive = dynamic_cast<const InteractiveBlock*>(block_);
        const bool latched =
            interactive &&
            interactive->liveValue() == interactive->descriptor().onValue;

        const QRectF face = switchFace(body);
        const qreal radius = type == "Toggle" ? face.height() * 0.5 : 3.0;

        if (type == "PushButton") {
            painter->setPen(QPen(colors.blockGlyph, 1.2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(face.translated(0, 3), radius, radius);
        }

        QPainterPath body_;
        body_.addRoundedRect(face, radius, radius);
        painter->setPen(QPen(colors.blockGlyph, 1.5));
        painter->setBrush(latched ? colors.accent : colors.blockFill);
        painter->drawPath(body_);

        if (type == "Toggle") {
            const qreal knob = face.height() * 0.32;
            const qreal cx = latched ? face.right() - knob - 2
                                     : face.left() + knob + 2;
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.blockGlyph);
            painter->drawEllipse(QPointF(cx, face.center().y()), knob, knob);
        }

        painter->restore();
        painter->save();
        drawFittedText(painter,
                       QRectF(body.left(), body.bottom() - body.height() * 0.3,
                              body.width(), body.height() * 0.3),
                       type == "Toggle" ? QStringLiteral("toggle")
                                        : QStringLiteral("push"),
                       7);
        painter->restore();
        return;
    }

    if (type == "Goto" || type == "From") {
        painter->restore();
        painter->save();

        QString tag = QString::fromStdString(params.textOr("tag", ""));
        if (tag.isEmpty()) tag = QStringLiteral("?");
        const bool global = params.textOr("scope", "local") == "global";

        painter->setPen(colors.accent);
        drawFittedText(painter, body.adjusted(0, 0, 0, -body.height() * 0.28),
                       QStringLiteral("[%1]").arg(tag), 12);

        if (type == "Goto" && global) {
            painter->setPen(colors.blockText);
            drawFittedText(painter,
                           QRectF(body.left(), body.bottom() - body.height() * 0.28,
                                  body.width(), body.height() * 0.28),
                           QStringLiteral("global"), 7);
        }
        painter->restore();
        return;
    }

    if (type == "Inport" || type == "Outport") {
        painter->restore();
        painter->save();
        drawFittedText(painter, body,
                       QString::number(params.integerOr("portNumber", 1)), 14);
        painter->restore();
        return;
    }

    if (type == "PythonFunction") {
        painter->restore();
        painter->save();
        painter->setPen(colors.accent);
        QFont font = painter->font();
        font.setPointSize(11);
        font.setBold(true);
        painter->setFont(font);
        painter->drawText(QRectF(body.left(), body.top(), body.width(),
                                 body.height() * 0.55),
                          Qt::AlignCenter, QStringLiteral("Python"));

        painter->setPen(colors.blockText);
        QString className =
            QString::fromStdString(params.textOr("className", ""));
        if (className.isEmpty()) className = QStringLiteral("‹auto›");
        drawFittedText(painter,
                       QRectF(body.left(), body.center().y(), body.width(),
                              body.height() * 0.45),
                       className, 8);
        painter->restore();
        return;
    }

    painter->restore();
    painter->save();
    drawFittedText(painter, body, QString::fromStdString(block_->typeName()),
                   9);
    painter->restore();
}

QRectF BlockItem::controlZone() const {
    if (!dynamic_cast<const InteractiveBlock*>(block_)) return {};

    const QRectF glyph = bodyRect().adjusted(6, 5, -6, -5);
    const std::string& type = block_->typeName();
    if (type == "Slider") return sliderBand(glyph);
    if (type == "Toggle" || type == "PushButton") return switchFace(glyph);
    return {};
}

void BlockItem::setInteractionLocked(bool locked) {
    if (locked_ == locked) return;
    locked_ = locked;
    setFlag(ItemIsMovable, !locked);
}

bool BlockItem::driveControl(const QPointF& local, bool pressed) {
    auto* interactive = dynamic_cast<InteractiveBlock*>(block_);
    if (!interactive) return false;

    const QRectF zone = controlZone();
    if (zone.isEmpty()) return false;

    const ControlDescriptor descriptor = interactive->descriptor();

    switch (descriptor.kind) {
        case ControlDescriptor::Kind::Slider: {
            const double fraction =
                std::clamp((local.x() - zone.left()) / zone.width(), 0.0, 1.0);
            double value =
                descriptor.minimum +
                fraction * (descriptor.maximum - descriptor.minimum);
            if (descriptor.step > 0.0)
                value = descriptor.minimum +
                        std::round((value - descriptor.minimum) /
                                   descriptor.step) *
                            descriptor.step;
            interactive->setLiveValue(
                std::clamp(value, descriptor.minimum, descriptor.maximum));
            break;
        }

        case ControlDescriptor::Kind::Toggle:
            if (!pressed) return true;
            interactive->setLiveValue(
                interactive->liveValue() == descriptor.onValue
                    ? descriptor.offValue
                    : descriptor.onValue);
            break;

        case ControlDescriptor::Kind::Button:
            interactive->setLiveValue(pressed ? descriptor.onValue
                                              : descriptor.offValue);
            break;
    }

    update();
    emit controlChanged();
    return true;
}

void BlockItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        controlZone().contains(event->pos())) {
        // Not forwarded to the base class, or the press would start a block drag.
        drivingControl_ = true;
        setSelected(true);
        driveControl(event->pos(), true);
        // Not forwarded to the base class, or the press would start a block drag.
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

void BlockItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (drivingControl_) {
        driveControl(event->pos(), true);
        event->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(event);
}

void BlockItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (drivingControl_) {
        drivingControl_ = false;
        driveControl(event->pos(), false);
        event->accept();
        return;
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

QVariant BlockItem::itemChange(GraphicsItemChange change,
                               const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        commitGeometry();
        emit geometryChanged();
    } else if (change == ItemSelectedHasChanged) {
        update();
    }
    return QGraphicsObject::itemChange(change, value);
}

void BlockItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    emit editRequested();
    event->accept();
}

void BlockItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void BlockItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    unsetCursor();
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

void BlockItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    const QRectF zone = controlZone();
    if (!zone.isEmpty() && zone.contains(event->pos()))
        setCursor(QCursor(block_->typeName() == "Slider"
                              ? Qt::SizeHorCursor
                              : Qt::PointingHandCursor));
    else
        unsetCursor();
    QGraphicsObject::hoverMoveEvent(event);
}

}
