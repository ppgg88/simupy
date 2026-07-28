#include "WireRouter.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace simupy {
namespace {

constexpr int kMaxCoordinates = 90;

constexpr qreal kBendPenalty = 45.0;

constexpr qreal kEpsilon = 1e-6;

constexpr qreal kLaneTolerance = 7.0;

constexpr qreal kOverlapWeight = 2.0;

constexpr qreal kCrossPenalty = 30.0;

enum Direction { Horizontal = 0, Vertical = 1 };

struct Grid {
    std::vector<qreal> xs;
    std::vector<qreal> ys;
    std::vector<QRectF> blocked;

    int index(int ix, int iy, int dir) const {
        return (iy * static_cast<int>(xs.size()) + ix) * 2 + dir;
    }

    bool horizontalBlocked(qreal y, qreal x0, qreal x1) const {
        if (x0 > x1) std::swap(x0, x1);
        for (const QRectF& rect : blocked) {
            if (y <= rect.top() + kEpsilon || y >= rect.bottom() - kEpsilon)
                continue;
            if (x1 <= rect.left() + kEpsilon || x0 >= rect.right() - kEpsilon)
                continue;
            return true;
        }
        return false;
    }

    bool verticalBlocked(qreal x, qreal y0, qreal y1) const {
        if (y0 > y1) std::swap(y0, y1);
        for (const QRectF& rect : blocked) {
            if (x <= rect.left() + kEpsilon || x >= rect.right() - kEpsilon)
                continue;
            if (y1 <= rect.top() + kEpsilon || y0 >= rect.bottom() - kEpsilon)
                continue;
            return true;
        }
        return false;
    }
};

void addCoordinate(std::vector<qreal>& values, qreal value) {
    for (qreal existing : values)
        if (std::abs(existing - value) < 0.5) return;
    values.push_back(value);
}

int nearestIndex(const std::vector<qreal>& values, qreal value) {
    int best = 0;
    qreal bestDistance = std::abs(values[0] - value);
    for (std::size_t i = 1; i < values.size(); ++i) {
        const qreal distance = std::abs(values[i] - value);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

QVector<QPointF> simplify(const QVector<QPointF>& points) {
    QVector<QPointF> result;
    for (const QPointF& point : points) {
        if (result.size() >= 2) {
            const QPointF& a = result[result.size() - 2];
            const QPointF& b = result[result.size() - 1];
            const bool collinear =
                (std::abs(a.x() - b.x()) < kEpsilon &&
                 std::abs(b.x() - point.x()) < kEpsilon) ||
                (std::abs(a.y() - b.y()) < kEpsilon &&
                 std::abs(b.y() - point.y()) < kEpsilon);
            if (collinear) {
                result.removeLast();
            }
        }
        if (result.isEmpty() || result.last() != point) result.append(point);
    }
    return result;
}

QPainterPath pathThrough(const QPointF& from, const QVector<QPointF>& points,
                         const QPointF& to) {
    QVector<QPointF> all;
    all.append(from);
    all += points;
    all.append(to);

    const QVector<QPointF> simplified = simplify(all);
    QPainterPath path(simplified.first());
    for (int i = 1; i < simplified.size(); ++i) path.lineTo(simplified[i]);
    return path;
}

}

void WireField::clear() {
    horizontal_.clear();
    vertical_.clear();
    sorted_ = true;
}

void WireField::add(const QPainterPath& path, SignalKey signal) {
    for (int i = 1; i < path.elementCount(); ++i) {
        const QPainterPath::Element a = path.elementAt(i - 1);
        const QPainterPath::Element b = path.elementAt(i);
        if (!b.isLineTo()) continue;

        if (std::abs(a.y - b.y) < 0.5) {
            horizontal_.push_back(
                {a.y, std::min(a.x, b.x), std::max(a.x, b.x), signal});
        } else if (std::abs(a.x - b.x) < 0.5) {
            vertical_.push_back(
                {a.x, std::min(a.y, b.y), std::max(a.y, b.y), signal});
        }
        sorted_ = false;
    }
}

void WireField::ensureSorted() const {
    if (sorted_) return;
    auto byLane = [](const Segment& a, const Segment& b) {
        return a.lane < b.lane;
    };
    auto& horizontal = const_cast<std::vector<Segment>&>(horizontal_);
    auto& vertical = const_cast<std::vector<Segment>&>(vertical_);
    std::sort(horizontal.begin(), horizontal.end(), byLane);
    std::sort(vertical.begin(), vertical.end(), byLane);
    sorted_ = true;
}

void WireField::sidestepLanes(bool horizontal, qreal min, qreal max,
                              std::vector<qreal>& out) const {
    ensureSorted();
    const std::vector<Segment>& lanes = horizontal ? horizontal_ : vertical_;

    constexpr qreal kStep = kLaneTolerance + 3.0;

    auto it = std::lower_bound(
        lanes.begin(), lanes.end(), min,
        [](const Segment& s, qreal v) { return s.lane < v; });
    for (; it != lanes.end() && it->lane <= max; ++it) {
        out.push_back(it->lane - kStep);
        out.push_back(it->lane + kStep);
    }
}

qreal WireField::penalty(bool horizontal, qreal lane, qreal from, qreal to,
                         SignalKey signal) const {
    if (from > to) std::swap(from, to);
    ensureSorted();

    auto lower = [](const std::vector<Segment>& list, qreal value) {
        return std::lower_bound(
            list.begin(), list.end(), value,
            [](const Segment& s, qreal v) { return s.lane < v; });
    };

    qreal cost = 0.0;

    const std::vector<Segment>& alongside = horizontal ? horizontal_ : vertical_;
    for (auto it = lower(alongside, lane - kLaneTolerance);
         it != alongside.end() && it->lane <= lane + kLaneTolerance; ++it) {
        if (it->signal == signal) continue;
        const qreal shared = std::min(to, it->to) - std::max(from, it->from);
        if (shared > 0.0) cost += shared * kOverlapWeight;
    }

    const std::vector<Segment>& across = horizontal ? vertical_ : horizontal_;
    for (auto it = lower(across, from); it != across.end() && it->lane <= to;
         ++it) {
        if (it->signal == signal) continue;
        if (lane < it->from || lane > it->to) continue;
        cost += kCrossPenalty;
    }

    return cost;
}

QPainterPath WireRouter::directRoute(const QPointF& from, const QPointF& to,
                                     bool sourceFacesLeft,
                                     bool targetFacesLeft) {
    const qreal outward = sourceFacesLeft ? -kStub : kStub;
    const qreal inward = targetFacesLeft ? kStub : -kStub;

    const QPointF start = from + QPointF(outward, 0.0);
    const QPointF end = to + QPointF(inward, 0.0);

    QPainterPath path(from);
    path.lineTo(start);

    const bool forward =
        sourceFacesLeft ? start.x() > end.x() : start.x() < end.x();
    if (forward) {
        const qreal middle = (start.x() + end.x()) / 2.0;
        path.lineTo(middle, start.y());
        path.lineTo(middle, end.y());
    } else {
        qreal lane = start.y();
        if (std::abs(start.y() - end.y()) < 1.0) lane = start.y() + 46.0;
        path.lineTo(start.x(), lane);
        path.lineTo(end.x(), lane);
    }

    path.lineTo(end);
    path.lineTo(to);
    return path;
}

QPainterPath WireRouter::route(const QPointF& from, const QPointF& to,
                               bool sourceFacesLeft, bool targetFacesLeft,
                               const QVector<QRectF>& obstacles,
                               const WireField* placed,
                               WireField::SignalKey signal) {
    const qreal outward = sourceFacesLeft ? -kStub : kStub;
    const qreal inward = targetFacesLeft ? kStub : -kStub;
    const QPointF start = from + QPointF(outward, 0.0);
    const QPointF end = to + QPointF(inward, 0.0);

    Grid grid;
    grid.blocked.reserve(obstacles.size());
    for (const QRectF& rect : obstacles) {
        QRectF solid = rect.adjusted(-kMargin, -kMargin, kMargin, kMargin);

        // Give up the clearance ring, never the block: a route must not cut across it.
        if (solid.contains(start) || solid.contains(end)) solid = rect;

        if (solid.contains(start) || solid.contains(end)) continue;

        grid.blocked.push_back(solid);
    }

    if (grid.blocked.empty() && !placed)
        return directRoute(from, to, sourceFacesLeft, targetFacesLeft);

    addCoordinate(grid.xs, start.x());
    addCoordinate(grid.xs, end.x());
    addCoordinate(grid.ys, start.y());
    addCoordinate(grid.ys, end.y());
    addCoordinate(grid.xs, (start.x() + end.x()) / 2.0);

    constexpr qreal kLaneSlack = 1.5;
    for (const QRectF& rect : grid.blocked) {
        addCoordinate(grid.xs, rect.left() - kLaneSlack);
        addCoordinate(grid.xs, rect.right() + kLaneSlack);
        addCoordinate(grid.ys, rect.top() - kLaneSlack);
        addCoordinate(grid.ys, rect.bottom() + kLaneSlack);
    }

    if (placed) {
        constexpr qreal kReach = 140.0;
        std::vector<qreal> lanes;

        placed->sidestepLanes(true,
                              std::min(start.y(), end.y()) - kReach,
                              std::max(start.y(), end.y()) + kReach, lanes);
        for (qreal lane : lanes) addCoordinate(grid.ys, lane);

        lanes.clear();
        placed->sidestepLanes(false,
                              std::min(start.x(), end.x()) - kReach,
                              std::max(start.x(), end.x()) + kReach, lanes);
        for (qreal lane : lanes) addCoordinate(grid.xs, lane);
    }

    if (grid.xs.size() > kMaxCoordinates || grid.ys.size() > kMaxCoordinates)
        return directRoute(from, to, sourceFacesLeft, targetFacesLeft);

    std::sort(grid.xs.begin(), grid.xs.end());
    std::sort(grid.ys.begin(), grid.ys.end());

    const int columns = static_cast<int>(grid.xs.size());
    const int rows = static_cast<int>(grid.ys.size());

    const int startX = nearestIndex(grid.xs, start.x());
    const int startY = nearestIndex(grid.ys, start.y());
    const int endX = nearestIndex(grid.xs, end.x());
    const int endY = nearestIndex(grid.ys, end.y());

    const int nodeCount = columns * rows * 2;
    std::vector<qreal> cost(nodeCount, std::numeric_limits<qreal>::infinity());
    std::vector<int> cameFrom(nodeCount, -1);

    auto heuristic = [&](int ix, int iy) {
        return std::abs(grid.xs[ix] - grid.xs[endX]) +
               std::abs(grid.ys[iy] - grid.ys[endY]);
    };

    using Entry = std::pair<qreal, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    // The wire leaves its port horizontally, so the first run pays no bend.
    const int startNode = grid.index(startX, startY, Horizontal);
    cost[startNode] = 0.0;
    open.push({heuristic(startX, startY), startNode});

    int goal = -1;
    while (!open.empty()) {
        const auto [priority, node] = open.top();
        open.pop();
        if (priority > cost[node] + heuristic(node / 2 % columns,
                                              node / 2 / columns) + kEpsilon)
            continue;

        const int dir = node % 2;
        const int cell = node / 2;
        const int ix = cell % columns;
        const int iy = cell / columns;

        if (ix == endX && iy == endY && dir == Horizontal) {
            goal = node;
            break;
        }

        for (int step = -1; step <= 1; step += 2) {
            const int nx = ix + step;
            if (nx >= 0 && nx < columns &&
                !grid.horizontalBlocked(grid.ys[iy], grid.xs[ix], grid.xs[nx])) {
                const int next = grid.index(nx, iy, Horizontal);
                qreal move = std::abs(grid.xs[nx] - grid.xs[ix]) +
                             (dir == Horizontal ? 0.0 : kBendPenalty);
                if (placed)
                    move += placed->penalty(true, grid.ys[iy], grid.xs[ix],
                                            grid.xs[nx], signal);
                if (cost[node] + move < cost[next] - kEpsilon) {
                    cost[next] = cost[node] + move;
                    cameFrom[next] = node;
                    open.push({cost[next] + heuristic(nx, iy), next});
                }
            }

            const int ny = iy + step;
            if (ny >= 0 && ny < rows &&
                !grid.verticalBlocked(grid.xs[ix], grid.ys[iy], grid.ys[ny])) {
                const int next = grid.index(ix, ny, Vertical);
                qreal move = std::abs(grid.ys[ny] - grid.ys[iy]) +
                             (dir == Vertical ? 0.0 : kBendPenalty);
                if (placed)
                    move += placed->penalty(false, grid.xs[ix], grid.ys[iy],
                                            grid.ys[ny], signal);
                if (cost[node] + move < cost[next] - kEpsilon) {
                    cost[next] = cost[node] + move;
                    cameFrom[next] = node;
                    open.push({cost[next] + heuristic(ix, ny), next});
                }
            }
        }
    }

    if (goal < 0)
        return directRoute(from, to, sourceFacesLeft, targetFacesLeft);

    QVector<QPointF> points;
    for (int node = goal; node >= 0; node = cameFrom[node]) {
        const int cell = node / 2;
        points.prepend(QPointF(grid.xs[cell % columns], grid.ys[cell / columns]));
    }

    if (!points.isEmpty()) {
        points.first() = start;
        points.last() = end;
    }
    return pathThrough(from, points, to);
}

}
