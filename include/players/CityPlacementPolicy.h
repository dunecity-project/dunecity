#ifndef CITY_PLACEMENT_POLICY_H
#define CITY_PLACEMENT_POLICY_H
#include <algorithm>
#include <vector>
#include <type_traits>
#include <cstddef>

namespace CityPlacementPolicy {
// Partition candidate origins into a cheap central pass and a disjoint fallback.
// The second pass must include disconnected districts and map-edge outposts.
inline bool inPlacementSearchPass(int x, int y, int cx, int cy, int radius, int pass) {
    const bool central = x >= cx-radius && x <= cx+radius && y >= cy-radius && y <= cy+radius;
    return pass == 0 ? central : !central;
}
// How many origins (x, y0..y1) inPlacementSearchPass() admits, without visiting them.
inline int placementSearchPassCount(int x, int y0, int y1, int cx, int cy, int radius, int pass) {
    if (y1 < y0) return 0;
    const bool centralColumn = x >= cx-radius && x <= cx+radius;
    const int central = centralColumn ? std::max(0, std::min(y1, cy+radius) - std::max(y0, cy-radius) + 1) : 0;
    return pass == 0 ? central : (y1 - y0 + 1) - central;
}

// Match ground-unit movement: neighbouring road tiles may connect diagonally.
constexpr int dx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr int dy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

inline bool overlaps(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
    return x < bx + bw && bx < x + w && y < by + bh && by < y + h;
}

inline bool recentLossBlocks(int x, int y, int w, int h, int lx, int ly, int lw, int lh,
                             unsigned age, unsigned cooldown) {
    return age < cooldown && overlaps(x, y, w, h, lx-1, ly-1, lw+2, lh+2);
}

// Closest-tile Chebyshev distance between full building footprints.
inline int footprintDistance(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
    return std::max({0, bx-(x+w-1), x-(bx+bw-1), by-(y+h-1), y-(by+bh-1)});
}
// Prefer a clean commute band; retain crowded/disconnected sites as fallbacks.
inline int cityPlacementTier(bool withinSupplyReach, int pollutionDistance) {
    return withinSupplyReach ? (pollutionDistance > 5 ? 2 : 1) : 0;
}
inline int pollutionSeparationScore(int distance) {
    if (distance <= 5) return -120 * (6 - std::max(0, distance));
    return distance <= 16 ? 30 : 0;
}
inline int sensitivePlacementTier(bool withinSupplyReach, int pollutionDistance,
                                  int pollution, int traffic) {
    int tier = cityPlacementTier(withinSupplyReach, pollutionDistance);
    if (pollution >= 160) tier -= 2;
    else if (pollution >= 100) --tier;
    if (traffic >= 220) --tier;
    return tier;
}
inline int residentialCommercialEnvironmentScore(int pollution, int value, int adjacentSand, int traffic = 0) {
    const int congestionPenalty = std::max(0, std::clamp(traffic, 0, 255) - 160);
    return std::clamp(value, 0, 250) / 2 - 3 * std::clamp(pollution, 0, 250)
        - congestionPenalty
        + 8 * std::max(0, adjacentSand);
}

inline bool fourZoneBlockFits(int mapWidth, int mapHeight, int blockX, int blockY) {
    // Four tiles of lots plus one road tile on each side.
    return blockX >= 1 && blockY >= 1 && blockX + 4 < mapWidth && blockY + 4 < mapHeight;
}

inline bool fourZoneGridSlot(int x, int y, int baseX, int baseY) {
    const int gx=((x-baseX)%5+5)%5, gy=((y-baseY)%5+5)%5;
    return (gx==0 || gx==2) && (gy==0 || gy==2);
}

// Safety and environmental suitability first; infill breaks ties between comparable sites.
inline bool preferCitySite(bool safe,int sides,int tier,int score,
                           bool bestSafe,int bestSides,int bestTier,int bestScore) {
    if (safe!=bestSafe) return safe;
    if (tier!=bestTier) return tier>bestTier;
    const bool infill=sides>=2,bestInfill=bestSides>=2;
    if (infill!=bestInfill) return infill;
    return score>bestScore;
}

struct RoadImpact { bool preservesConnections = true; int roadsCovered = 0; int redundantRoadsCovered = 0; int junctionBonus = 0; };

template<class IsRoad, class CanPave = std::nullptr_t>
RoadImpact assessRoads(int x, int y, int width, int height, bool turret, IsRoad road, CanPave pave = nullptr) {
    RoadImpact result;
    const int w = width + 2, h = height + 2;
    std::vector<bool> before(w * h), after(w * h);
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) {
        const bool covered = i > 0 && i < w - 1 && j > 0 && j < h - 1;
        before[j*w+i] = road(x+i-1, y+j-1);
        after[j*w+i] = before[j*w+i] && !covered;
        if constexpr (!std::is_same_v<CanPave,std::nullptr_t>)
            if (!covered && pave(x+i-1,y+j-1)) after[j*w+i] = true;
        if (covered && before[j*w+i]) ++result.roadsCovered;
    }
    if (!result.roadsCovered) return result;
    auto survivingRoad=[&](int tx,int ty) {
        return !(tx>=x && tx<x+width && ty>=y && ty<y+height) && road(tx,ty);
    };
    for (int ty=y;ty<y+height;++ty) for (int tx=x;tx<x+width;++tx) if (road(tx,ty)) {
        const bool horizontal=road(tx-1,ty) && road(tx+1,ty)
            && (survivingRoad(tx,ty-1) || survivingRoad(tx,ty+1));
        const bool vertical=road(tx,ty-1) && road(tx,ty+1)
            && (survivingRoad(tx-1,ty) || survivingRoad(tx+1,ty));
        result.redundantRoadsCovered += horizontal || vertical;
    }
    auto components = [&](const std::vector<bool>& cells) {
        std::vector<int> labels(w*h, -1), queue;
        for (int seed = 0; seed < w*h; ++seed) {
            if (!cells[seed] || labels[seed] >= 0) continue;
            queue.clear(); queue.push_back(seed); labels[seed] = seed;
            for (size_t q = 0; q < queue.size(); ++q) {
                const int cx = queue[q] % w, cy = queue[q] / w;
                for (int d = 0; d < (turret ? 8 : 4); ++d) {
                    const int nx = cx + dx[d], ny = cy + dy[d];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    const int n = ny*w+nx;
                    if (cells[n] && labels[n] < 0) { labels[n] = seed; queue.push_back(n); }
                }
            }
        }
        return labels;
    };
    const auto oldLabels = components(before), newLabels = components(after);
    for (int a = 0; a < w*h; ++a) if (after[a] && before[a]) {
        for (int b = a+1; b < w*h; ++b) if (after[b] && before[b]) {
            if (oldLabels[a] == oldLabels[b] && newLabels[a] != newLabels[b])
                result.preservesConnections = false;
        }
    }
    if (turret && width == 1 && height == 1 && result.preservesConnections) {
        bool arms[4]; int count = 0;
        for (int d = 0; d < 4; ++d) { arms[d] = road(x+dx[d], y+dy[d]); count += arms[d]; }
        if (count >= 3) result.junctionBonus = count * 60;
        else if (count == 2 && !(arms[0] && arms[2]) && !(arms[1] && arms[3]))
            result.junctionBonus = 120;
    }
    return result;
}
// The one-tile perimeter may extend outside the map even for a valid building.
// Treat it as unavailable before invoking callbacks backed by Map::getTile.
template<class IsRoad, class CanPave>
RoadImpact assessRoadsOnMap(int mapWidth, int mapHeight, int x, int y, int width, int height,
                            bool turret, IsRoad road, CanPave pave) {
    auto inside = [&](int tx, int ty) {
        return tx >= 0 && ty >= 0 && tx < mapWidth && ty < mapHeight;
    };
    return assessRoads(x, y, width, height, turret,
        [&](int tx, int ty) { return inside(tx, ty) && road(tx, ty); },
        [&](int tx, int ty) { return inside(tx, ty) && pave(tx, ty); });
}
} // namespace CityPlacementPolicy
#endif
