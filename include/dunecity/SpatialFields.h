/*
 *  SpatialFields.h — pure, engine-free spatial accelerators.
 *
 *  Each type here replaces a pointwise scan with a precomputed field that
 *  answers the SAME predicate or sum. They hold no gameplay state, are never
 *  serialised, and are rebuilt from their inputs at every use, so they cannot
 *  carry information across decisions. Integer arithmetic only: the sums are
 *  commutative and associative over the same addends the scans produced, so a
 *  field query is bit-identical to the loop it replaces, including tie values.
 *
 *  These are shared by the engine and by the tests that pin them, so the tests
 *  exercise the implementation the game actually runs.
 */
#ifndef DUNECITY_SPATIALFIELDS_H
#define DUNECITY_SPATIALFIELDS_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace DuneCity {

/// Chebyshev-falloff weight sum over point sources.
///
/// Replaces `for (src : sources) if (cheb(q,src) < radius+1) sum += (radius+1 -
/// cheb(q,src)) * weight`. Sources outside the map are kept: they legitimately
/// reach in-map cells near an edge. Only in-box cells are stored, and the box
/// covers every in-map cell any source can reach, so a query at any in-map
/// coordinate returns the exact loop total and a query further away returns 0 —
/// which is what the loop returns there too.
class ChebyshevWeightField {
public:
    ChebyshevWeightField(int radius, int weight) : radius_(radius), weight_(weight) {}

    /// Stamps every source. `mapWidth`/`mapHeight` bound the queryable area.
    template <typename CoordLike>
    void build(const std::vector<CoordLike>& sources, int mapWidth, int mapHeight) {
        cells_.clear();
        boxW_ = boxH_ = 0;
        if (sources.empty() || mapWidth <= 0 || mapHeight <= 0 || radius_ < 0) return;

        int minX = sources[0].x, maxX = sources[0].x;
        int minY = sources[0].y, maxY = sources[0].y;
        for (const auto& source : sources) {
            minX = std::min(minX, static_cast<int>(source.x));
            maxX = std::max(maxX, static_cast<int>(source.x));
            minY = std::min(minY, static_cast<int>(source.y));
            maxY = std::max(maxY, static_cast<int>(source.y));
        }
        // Clip to the map: only in-map cells are ever queried, and an off-map
        // source still reaches the in-map part of its own footprint.
        boxX_ = std::max(0, minX - radius_);
        boxY_ = std::max(0, minY - radius_);
        const int boxX1 = std::min(mapWidth  - 1, maxX + radius_);
        const int boxY1 = std::min(mapHeight - 1, maxY + radius_);
        if (boxX1 < boxX_ || boxY1 < boxY_) return;

        boxW_ = boxX1 - boxX_ + 1;
        boxH_ = boxY1 - boxY_ + 1;
        cells_.assign(static_cast<size_t>(boxW_) * static_cast<size_t>(boxH_), 0);
        for (const auto& source : sources) {
            const int sx = static_cast<int>(source.x), sy = static_cast<int>(source.y);
            for (int dy = -radius_; dy <= radius_; ++dy) {
                const int ty = sy + dy;
                if (ty < boxY_ || ty >= boxY_ + boxH_) continue;
                for (int dx = -radius_; dx <= radius_; ++dx) {
                    const int tx = sx + dx;
                    if (tx < boxX_ || tx >= boxX_ + boxW_) continue;
                    const int chebyshev = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
                    cells_[static_cast<size_t>(ty - boxY_) * static_cast<size_t>(boxW_)
                           + static_cast<size_t>(tx - boxX_)]
                        += (radius_ + 1 - chebyshev) * weight_;
                }
            }
        }
    }

    int at(int x, int y) const {
        if (boxW_ == 0 || x < boxX_ || y < boxY_
            || x >= boxX_ + boxW_ || y >= boxY_ + boxH_) return 0;
        return cells_[static_cast<size_t>(y - boxY_) * static_cast<size_t>(boxW_)
                      + static_cast<size_t>(x - boxX_)];
    }

    /// Cells actually stamped — a work counter for telemetry, not a decision.
    size_t storedCells() const { return cells_.size(); }

private:
    int radius_ = 0, weight_ = 0;
    int boxX_ = 0, boxY_ = 0, boxW_ = 0, boxH_ = 0;
    std::vector<int32_t> cells_;
};

/// "Is any marked tile inside this box?" over a boolean tile indicator.
///
/// Replaces a pointwise `(2R+1)^2` probe such as Map::isWithinBuildRange(),
/// which scans a square around one tile and stops at the first owned tile.
/// A summed-area table answers the same existence question in four lookups.
/// Queries clip to the map, exactly as a probe that skips off-map tiles does.
class BoxAnyField {
public:
    /// `marked(x,y)` is evaluated once per tile, in row-major order.
    template <typename Predicate>
    void build(int width, int height, Predicate marked) {
        width_ = std::max(0, width);
        height_ = std::max(0, height);
        stride_ = static_cast<size_t>(width_) + 1;
        sat_.assign(stride_ * (static_cast<size_t>(height_) + 1), 0);
        for (int y = 0; y < height_; ++y) {
            int32_t run = 0;
            for (int x = 0; x < width_; ++x) {
                run += marked(x, y) ? 1 : 0;
                sat_[static_cast<size_t>(y + 1) * stride_ + static_cast<size_t>(x + 1)]
                    = sat_[static_cast<size_t>(y) * stride_ + static_cast<size_t>(x + 1)] + run;
            }
        }
    }

    /// Marked tiles inside the inclusive box, clipped to the map.
    int count(int x0, int y0, int x1, int y1) const {
        x0 = std::max(0, x0); y0 = std::max(0, y0);
        x1 = std::min(width_ - 1, x1); y1 = std::min(height_ - 1, y1);
        if (x0 > x1 || y0 > y1 || sat_.empty()) return 0;
        const size_t a = static_cast<size_t>(y1 + 1) * stride_ + static_cast<size_t>(x1 + 1);
        const size_t b = static_cast<size_t>(y0) * stride_ + static_cast<size_t>(x1 + 1);
        const size_t c = static_cast<size_t>(y1 + 1) * stride_ + static_cast<size_t>(x0);
        const size_t d = static_cast<size_t>(y0) * stride_ + static_cast<size_t>(x0);
        return sat_[a] - sat_[b] - sat_[c] + sat_[d];
    }

    /// Any marked tile within Chebyshev `radius` of (x,y)?
    bool anyWithin(int x, int y, int radius) const {
        return count(x - radius, y - radius, x + radius, y + radius) > 0;
    }

    /// Any marked tile within `radius` of the inclusive footprint box?
    bool anyNearFootprint(int x, int y, int w, int h, int radius) const {
        return count(x - radius, y - radius, x + w - 1 + radius, y + h - 1 + radius) > 0;
    }

    bool empty() const { return sat_.empty(); }

private:
    int width_ = 0, height_ = 0;
    size_t stride_ = 1;
    std::vector<int32_t> sat_;
};

} // namespace DuneCity

#endif // DUNECITY_SPATIALFIELDS_H
