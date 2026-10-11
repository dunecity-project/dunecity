#ifndef MCV_DEPLOY_POLICY_H
#define MCV_DEPLOY_POLICY_H
#include <vector>

// Where an MCV turns into a construction yard on the rock formation the base
// already stands on. Colonising a *different* formation remains
// RockExpansionPolicy's job: nothing here ever leaves the home formation, so
// the map's expansion rules are untouched by local growth.
namespace McvDeployPolicy {

struct Tile {
    bool rock = false;      ///< Buildable rock; the caller excludes mountains.
    bool free = false;      ///< No structure and no ground object except this MCV.
    bool passable = false;  ///< This MCV can drive over the tile.
    bool owned = false;     ///< One of our own structures stands here.
    bool blocked = false;   ///< Reserved, zoned, remembered by another MCV, or unsafe.
};

struct Site {
    int x = -1, y = -1, room = 0, distance = 0;
    bool valid() const { return x >= 0; }
};

// Travel distance decides first. An MCV already standing on a usable footprint
// scores distance 0 and therefore keeps that footprint instead of driving
// somewhere else, which is what makes a chosen site stable across planning
// passes rather than a fresh destination every survey.
inline bool betterSite(const Site& candidate, const Site& best) {
    if(!best.valid()) return true;
    if(candidate.distance != best.distance) return candidate.distance < best.distance;
    if(candidate.room != best.room) return candidate.room > best.room;
    if(candidate.y != best.y) return candidate.y < best.y;
    return candidate.x < best.x;
}

// Linear survey of one window around the MCV: rock formations, drivable reach
// and summed local building space. No nested searches and no per-candidate
// pathfinding, so this stays cheap enough to run per idle MCV per AI pass.
//
// \a start is the MCV's own tile index. \a minRoom is the free rock wanted
// around a site; when no roomy site is reachable the tightest legal one is
// still returned rather than leaving the MCV without a plan.
//
// \a prefer is the site this MCV is already driving at. It is confirmed
// whenever it is still reachable rock of ours, ignoring units standing on it,
// so ordinary traffic crossing a destination never rerolls the trip.
template<class Accept>
inline Site choose(int w, int h, const std::vector<Tile>& tiles, int start, int minRoom, int prefer, Accept accept) {
    const int n = w * h;
    if(n <= 0 || static_cast<int>(tiles.size()) != n || start < 0 || start >= n) return {};

    std::vector<int> component(n, -1), distance(n, -1), queue;
    std::vector<bool> occupied;
    queue.reserve(n);
    // Formations are edge-connected rock: a building needs contiguous ground.
    auto neighbours = [&](int i, auto visit) {
        if(i % w) visit(i - 1);
        if(i % w + 1 < w) visit(i + 1);
        if(i >= w) visit(i - w);
        if(i + w < n) visit(i + w);
    };
    // Driving is eight-directional with no corner rule in the engine's path
    // search, so an MCV can leave between two buildings that touch only at a
    // corner. A four-way flood would strand it beside an open diagonal.
    auto driveNeighbours = [&](int i, auto visit) {
        const int x = i % w, y = i / w;
        for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx)
            if((dx || dy) && x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h) visit(i + dy * w + dx);
    };

    // Rock formations, and which of them already carry one of our structures.
    for(int i = 0; i < n; ++i) if(tiles[i].rock && component[i] < 0) {
        const int id = static_cast<int>(occupied.size());
        occupied.push_back(false);
        queue.clear(); queue.push_back(i); component[i] = id;
        for(size_t j = 0; j < queue.size(); ++j) {
            const int at = queue[j];
            occupied[id] = occupied[id] || tiles[at].owned;
            neighbours(at, [&](int next) {
                if(tiles[next].rock && component[next] < 0) { component[next] = id; queue.push_back(next); }
            });
        }
    }
    bool anyOwned = false;
    for(bool held : occupied) anyOwned = anyOwned || held;

    // Drivable reach from the MCV, so a site is never chosen behind a wall of
    // mountains or buildings that the unit cannot actually round.
    queue.clear();
    if(tiles[start].passable) { distance[start] = 0; queue.push_back(start); }
    for(size_t j = 0; j < queue.size(); ++j) {
        const int at = queue[j];
        driveNeighbours(at, [&](int next) {
            if(tiles[next].passable && distance[next] < 0) { distance[next] = distance[at] + 1; queue.push_back(next); }
        });
    }

    std::vector<int> sum((w + 1) * (h + 1));
    for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x) {
        const int i = y * w + x;
        sum[(y + 1) * (w + 1) + x + 1] = sum[y * (w + 1) + x + 1] + sum[(y + 1) * (w + 1) + x] - sum[y * (w + 1) + x]
            + static_cast<int>(tiles[i].rock && tiles[i].free && !tiles[i].blocked);
    }

    auto room = [&](int x, int y) {
        const int x0 = x > 3 ? x - 3 : 0, x1 = x + 5 < w ? x + 5 : w;
        const int y0 = y > 3 ? y - 3 : 0, y1 = y + 5 < h ? y + 5 : h;
        return sum[y1 * (w + 1) + x1] - sum[y0 * (w + 1) + x1] - sum[y1 * (w + 1) + x0] + sum[y0 * (w + 1) + x0];
    };
    auto home = [&](int c) { return c >= 0 && (anyOwned ? occupied[c] : c == component[start]); };

    // A trip already under way is kept while its target is still reachable
    // rock of ours. Only permanent obstacles retire it.
    if(prefer >= 0 && prefer < n && prefer % w + 1 < w && prefer / w + 1 < h
        && distance[prefer] >= 0 && home(component[prefer])) {
        bool holds = true;
        for(int d : {0, 1, w, w + 1})
            if(!tiles[prefer + d].rock || !tiles[prefer + d].passable || tiles[prefer + d].blocked) holds = false;
        if(holds && accept(prefer % w, prefer / w)) return Site{prefer % w, prefer / w, room(prefer % w, prefer / w), distance[prefer]};
    }

    Site best, tightest;
    for(int y = 0; y + 1 < h; ++y) for(int x = 0; x + 1 < w; ++x) {
        const int i = y * w + x, c = component[i];
        if(c < 0 || distance[i] < 0) continue;
        // The base's own formation only. Before the first structure exists the
        // MCV keeps the formation it is standing on, so an opening MCV still
        // has a plan.
        if(!home(c)) continue;
        bool legal = true;
        for(int d : {0, 1, w, w + 1})
            if(!tiles[i + d].rock || !tiles[i + d].free || tiles[i + d].blocked) legal = false;
        if(!legal) continue;
        const Site candidate{x, y, room(x, y), distance[i]};
        if(candidate.room >= minRoom) { if(betterSite(candidate, best) && accept(x,y)) best = candidate; }
        else if(betterSite(candidate, tightest) && accept(x,y)) tightest = candidate;
    }
    return best.valid() ? best : tightest;
}

inline Site choose(int w, int h, const std::vector<Tile>& tiles, int start, int minRoom = 12, int prefer = -1) {
    return choose(w,h,tiles,start,minRoom,prefer,[](int,int) { return true; });
}

}
#endif
