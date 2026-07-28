#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <vector>

namespace simupy {

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

class WireRouter {
public:
    static constexpr qreal kMargin = 14.0;
    static constexpr qreal kStub = 14.0;

    static QPainterPath route(const QPointF& from, const QPointF& to,
                              bool sourceFacesLeft, bool targetFacesLeft,
                              const QVector<QRectF>& obstacles,
                              const WireField* placed = nullptr,
                              WireField::SignalKey signal = 0);

    static QPainterPath directRoute(const QPointF& from, const QPointF& to,
                                    bool sourceFacesLeft,
                                    bool targetFacesLeft);
};

}
