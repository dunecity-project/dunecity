#ifndef CITY_DISTANCE_FIELD_H
#define CITY_DISTANCE_FIELD_H
#include <algorithm>
#include <vector>

// Exact obstacle-free Chebyshev (eight-neighbour, unit cost) distance from a
// map cell or footprint to the nearest added source rectangle; `missing` when
// there is none. Sources and queries are clipped to the map exactly as the
// former map-sized chamfer field clipped them: an off-map cell is `missing`,
// and a footprint is the minimum over its on-map cells.
//
// The field answers each query from the source rectangles themselves instead
// of filling a distance for every map cell. A placement search asks a few dozen
// questions, while a chamfer pass over a 384x384 map touched 147,456 cells per
// field (four fields per search, 0.8 ms each). The rectangle distance is the
// closed form of what the chamfer computed: for a cell, max of the x and y
// gaps to the rectangle; for a footprint, the minimum over its cells, which
// separates into the max of the minimum x gap and the minimum y gap.
// build() buckets the rectangles so a query visits only nearby ones.
class CityDistanceField {
public:
    static constexpr int missing = 1000000;
    CityDistanceField(int width, int height) : width_(std::max(0, width)), height_(std::max(0, height)) {}
    void add(int x, int y, int w=1, int h=1) {
        const Rect r{std::max(0, x), std::max(0, y), std::min(width_, x+w) - 1, std::min(height_, y+h) - 1};
        if (r.x0 > r.x1 || r.y0 > r.y1) return;
        rects_.push_back(r);
        built_ = false;
    }
    void build() {
        bucketsX_ = (width_ + kBucket - 1) / kBucket;
        bucketsY_ = (height_ + kBucket - 1) / kBucket;
        start_.assign(static_cast<size_t>(bucketsX_) * bucketsY_ + 1, 0);
        for (const Rect& r : rects_)
            forBuckets(r, [&](size_t bucket) { ++start_[bucket + 1]; });
        for (size_t i = 1; i < start_.size(); ++i) start_[i] += start_[i - 1];
        members_.assign(start_.back(), 0);
        std::vector<int> fill(start_.begin(), start_.end() - 1);
        for (size_t i = 0; i < rects_.size(); ++i)
            forBuckets(rects_[i], [&](size_t bucket) { members_[fill[bucket]++] = static_cast<int>(i); });
        built_ = true;
    }
    int get(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return missing;
        return nearest(Rect{x, y, x, y});
    }
    int footprint(int x, int y, int w, int h) const {
        const Rect q{std::max(0, x), std::max(0, y), std::min(width_, x+w) - 1, std::min(height_, y+h) - 1};
        if (q.x0 > q.x1 || q.y0 > q.y1) return missing;
        return nearest(q);
    }
private:
    struct Rect { int x0, y0, x1, y1; };   // inclusive, on the map
    static constexpr int kBucket = 16;

    static int gap(int a0, int a1, int b0, int b1) { return std::max({0, b0 - a1, a0 - b1}); }
    static int distance(const Rect& a, const Rect& b) {
        return std::max(gap(a.x0, a.x1, b.x0, b.x1), gap(a.y0, a.y1, b.y0, b.y1));
    }
    template <class Visit> void forBuckets(const Rect& r, Visit visit) const {
        for (int by = r.y0 / kBucket; by <= r.y1 / kBucket; ++by)
            for (int bx = r.x0 / kBucket; bx <= r.x1 / kBucket; ++bx)
                visit(static_cast<size_t>(by) * bucketsX_ + bx);
    }
    int nearest(const Rect& q) const {
        if (rects_.empty()) return missing;
        int best = missing;
        if (!built_) {
            for (const Rect& r : rects_) best = std::min(best, distance(q, r));
            return best;
        }
        const int bx0 = q.x0 / kBucket, bx1 = q.x1 / kBucket, by0 = q.y0 / kBucket, by1 = q.y1 / kBucket;
        auto scanBucket = [&](int bx, int by) {
            if (bx < 0 || by < 0 || bx >= bucketsX_ || by >= bucketsY_) return;
            const size_t bucket = static_cast<size_t>(by) * bucketsX_ + bx;
            for (int i = start_[bucket]; i < start_[bucket + 1]; ++i)
                best = std::min(best, distance(q, rects_[members_[i]]));
        };
        const int rings = std::max(bucketsX_, bucketsY_);
        for (int ring = 0; ring <= rings; ++ring) {
            const int left = bx0 - ring, right = bx1 + ring, top = by0 - ring, bottom = by1 + ring;
            if (ring == 0) {
                for (int by = top; by <= bottom; ++by) for (int bx = left; bx <= right; ++bx) scanBucket(bx, by);
            } else {
                for (int bx = left; bx <= right; ++bx) { scanBucket(bx, top); scanBucket(bx, bottom); }
                for (int by = top + 1; by < bottom; ++by) { scanBucket(left, by); scanBucket(right, by); }
            }
            // Every cell of a bucket in the next ring lies more than ring*kBucket from q.
            if (best <= ring * kBucket) break;
        }
        return best;
    }

    int width_, height_;
    int bucketsX_ = 0, bucketsY_ = 0;
    bool built_ = false;
    std::vector<Rect> rects_;
    std::vector<int> start_, members_;
};
#endif
