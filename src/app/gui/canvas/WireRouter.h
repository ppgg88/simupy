#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <vector>

namespace simupy {

/// The wires already placed, so the next one can be steered off their lanes.
///
/// Two things make a diagram hard to read, and they cost differently. Sharing
/// a lane is the worse one — two wires drawn on top of each other cannot be
/// told apart — and is charged per unit of shared length. Crossing at a right
/// angle is far milder but not free, so it costs a flat amount per crossing:
/// enough to prefer the route with fewer of them, not enough to send a wire
/// wandering.
///
/// Wires carrying the *same* signal are exempt from both. They leave one port
/// together, and reading them as a single branching line is exactly right.
class WireField {
public:
    using SignalKey = quintptr;

    void clear();

    void add(const QPainterPath& path, SignalKey signal);

    qreal penalty(bool horizontal, qreal lane, qreal from, qreal to,
                  SignalKey signal) const;

    void sidestepLanes(bool horizontal, qreal min, qreal max,
                       std::vector<qreal>& out) const;

private:
    struct Segment {
        qreal lane = 0.0;
        qreal from = 0.0;
        qreal to = 0.0;
        SignalKey signal = 0;
    };

    void ensureSorted() const;

    std::vector<Segment> horizontal_;
    std::vector<Segment> vertical_;
    mutable bool sorted_ = true;
};

/// Routes a wire as an A* search: blocks are walls, other wires are costs.
class WireRouter {
public:
    static constexpr qreal kMargin = 14.0;
    static constexpr qreal kStub = 14.0;

    /// Routes from an output port to an input port.
    ///
    /// `sourceFacesLeft` and `targetFacesLeft` say which way the ports point,
    /// so a mirrored block still gets a wire leaving on the correct side.
    /// Obstacles containing either endpoint are ignored: a wire is allowed to
    /// hug the blocks it belongs to.
    ///
    /// `placed`, when given, holds the wires routed before this one; the
    /// search then prefers a free lane over sharing theirs.
    ///
    /// Falls back to a plain two-bend route when no path exists or the
    /// diagram is large enough that searching would cost more than it is
    /// worth.
    static QPainterPath route(const QPointF& from, const QPointF& to,
                              bool sourceFacesLeft, bool targetFacesLeft,
                              const QVector<QRectF>& obstacles,
                              const WireField* placed = nullptr,
                              WireField::SignalKey signal = 0);

    /// The obstacle-free route, also used for the rubber-band wire the user
    /// drags before a connection exists.
    static QPainterPath directRoute(const QPointF& from, const QPointF& to,
                                    bool sourceFacesLeft,
                                    bool targetFacesLeft);
};

}  // namespace simupy
