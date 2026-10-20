#ifndef AIR_STRIKE_POLICY_H
#define AIR_STRIKE_POLICY_H

#include <data.h>
#include <mmath.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace AirStrikePolicy {
inline bool antiAir(int item) {
    return item == Structure_RocketTurret || item == Unit_Launcher
        || item == Unit_EliteLauncher || item == Unit_Deviator;
}

// Mobile launcher cover may be entered with sufficient local aircraft.
// Rocket turrets and Deviators retain absolute cover for ordinary raids.
inline bool mobileLauncher(int item) {
    return item == Unit_Launcher || item == Unit_EliteLauncher;
}

// Every shipped item whose own canAttack() actually reaches an aircraft: the
// rocket turret, rocket launchers, the Deviator and the rocket infantry that
// 1.0.826 produces as air defence (Trooper::canAttack() has no flying-unit
// exclusion). Used only by the strict, no-override cover test below, so the
// established raid pricing for the offensive difficulties is untouched.
inline bool strictAntiAir(int item) {
    return antiAir(item) || item == Unit_Trooper || item == Unit_Troopers;
}

// How far a defender of this item reaches an aircraft. A rocket turret engages
// ornithopters at three times its ground weapon range
// (RocketTurret::findTarget(); config/ObjectData.ini.default documents the
// rule), which the ordinary ground safety radius understates.
inline int safetyRange(int weaponRange);
inline int antiAirSafetyRange(int item, int weaponRange) {
    return safetyRange(item == Structure_RocketTurret ? weaponRange * 3 : weaponRange);
}

// A base building rather than paving or a wall: the single offensive option the
// non-raiding difficulties have is a strike on the enemy base itself.
inline bool opportunisticBaseStructure(int item) {
    return item != Structure_Wall && item != Structure_Slab1 && item != Structure_Slab4;
}

// Healthy local aircraft required per launcher covering the point they want to
// enter. From the shipped unit table: a launcher's 75 damage one-shots a 25 HP
// ornithopter, and four ornithopters deal 180 against its 100 HP, so four kill
// a lone launcher in one pass. Overlapping launchers multiply the requirement.
constexpr int kAircraftPerMobileLauncher = 4;
// Tiles that count as "the same engagement" for both the local wing count and
// the launcher-escort search: a launcher's own safetyRange at its nine-tile
// weapon range, so the wing is measured over exactly the area that can shoot it.
constexpr int kLocalEngagementRadius = 14;

// Whether a wing of `localAircraft` may enter a point covered by
// `coveringLaunchers` mobile launchers. Uncovered points are always permitted;
// the ratio is integer and total-order deterministic.
inline bool localWingPermits(int localAircraft, int coveringLaunchers) {
    return coveringLaunchers <= 0
        || int64_t(localAircraft) >= int64_t(coveringLaunchers) * kAircraftPerMobileLauncher;
}

// Home and harvester defence preempts raids on exposed enemy structures.
// Ordinary visible enemy ground units away from our assets are raid candidates
// too; whether a wing may actually fly at one is decided per aircraft by the
// launcher ratio above, not by this rank.
constexpr int RaidRank = 1;
constexpr int DefenseRank = 2;
// An attacker actually hitting something we own. A remote worker rescue and an
// attack on the base itself are both emergencies, but they are not the same
// emergency: the base outranks the field, so a wing already saving a harvester
// is recalled by an attack on the city and not the other way round. Without
// that separation every aircraft re-chose its target on every volley.
constexpr int UnderAttackRank = 3;
constexpr int BaseUnderAttackRank = 4;
inline int targetRank(bool structure, bool defensiveContact, bool roamingUnitRaidable) {
    return defensiveContact ? DefenseRank
        : (structure || roamingUnitRaidable) ? RaidRank : 0;
}
inline int underAttackRank(bool attackingBase) {
    return attackingBase ? BaseUnderAttackRank : UnderAttackRank;
}
// Ranks that describe a present loss rather than an opportunity. They may be
// approached through anti-air cover and they outrank every raid.
inline bool emergencyRank(int rank) { return rank >= UnderAttackRank; }
// A forced interception stands until its target dies or becomes unreachable;
// only a strictly more urgent class of emergency may replace it. Equal-rank
// score differences are not a reason to abandon a live attack run.
inline bool holdsInterception(int heldRank, int bestRank) {
    return heldRank > 0 && bestRank <= heldRank;
}
inline int safetyRange(int weaponRange) { return weaponRange + 5; }

// Row half-widths of the combat-distance disc of radius `range`: element |dy|
// is the largest |dx| with blockDistance((0,0),(dx,dy)) <= range (never below
// zero, since blockDistance((0,0),(0,dy)) == dy). blockDistance depends only
// on |dx| and |dy|, is symmetric in them, and is nondecreasing in each: both
// branches add a nonnegative fixed-point multiple of one difference to the
// other, and crossing from diffX == diffY to diffX == diffY + 1 adds exactly
// one. So every row of the disc is the single run [-span, span] and the spans
// never widen as |dy| grows, which lets one shrinking pointer evaluate the
// very predicate the per-tile scan used O(range) times instead of (2r+1)^2.
inline std::vector<int> coverageSpans(int range) {
    std::vector<int> spans;
    if (range<0) return spans;
    spans.resize(static_cast<size_t>(range)+1);
    int dx=range;
    for (int dy=0;dy<=range;++dy) {
        while (dx>=0 && !(blockDistance(Coord(0,0),Coord(dx,dy))<=range)) --dx;
        spans[dy]=dx;
    }
    return spans;
}

// One shared coverage map per tactical pass; no per-target scan of all weapons.
// Use the combat distance metric, including diagonal range, plus manoeuvre room.
//
// Two layers, both built once per pass: absolute cover (rocket turrets,
// Deviators) and a per-tile count of the mobile launchers covering that tile.
// The count is what makes the ratio honest along a whole route: a wing of four
// may cross ground one launcher covers, but not the overlap of two, and it is
// the overlap at each point that decides, not the one at the endpoint.
class Coverage {
public:
    Coverage(int width, int height)
        : width_(width), height_(height), blocked_(width*height,false),
          launchers_(static_cast<size_t>(width)*height,0) {}
    void add(Coord centre, int range) {
        forEachCovered(centre,range,[this](size_t index) { blocked_[index]=true; });
    }
    /// Mobile rocket cover. Counted, not latched, so overlaps raise the price.
    void addMobileLauncher(Coord centre, int range) {
        forEachCovered(centre,range,[this](size_t index) {
            auto& count=launchers_[index];
            if (count<std::numeric_limits<int>::max()) ++count;
        });
    }
    /// Strict layer: every observed anti-air weapon at its real reach against
    /// aircraft, latched rather than counted. Built only for the difficulties
    /// that are not allowed to price their way into cover, so the offensive
    /// layers above keep their exact established behaviour.
    void enableStrict() {
        strict_.assign(static_cast<size_t>(width_)*height_,false);
        strictEnabled_=true;
    }
    bool strictEnabled() const { return strictEnabled_; }
    void addStrict(Coord centre, int range) {
        if(!strictEnabled_) return;
        forEachCovered(centre,range,[this](size_t index) { strict_[index]=true; });
    }
    bool strictSafe(Coord point) const {
        if(!strictEnabled_) return false;
        if (point.x<0 || point.y<0 || point.x>=width_ || point.y>=height_) return false;
        return !strict_[static_cast<size_t>(point.y)*width_+point.x];
    }
    bool strictClearFootprint(Coord origin, Coord size) const {
        for (int y=0;y<size.y;++y) for(int x=0;x<size.x;++x)
            if (!strictSafe(Coord(origin.x+x,origin.y+y))) return false;
        return true;
    }
    bool strictClearApproach(Coord from, Coord to) const {
        const int steps=std::max({1,std::abs(to.x-from.x),std::abs(to.y-from.y)});
        for(int step=0;step<=steps;++step)
            if (!strictSafe(Coord(from.x+(to.x-from.x)*step/steps,
                                  from.y+(to.y-from.y)*step/steps))) return false;
        return true;
    }
    int mobileLaunchersAt(Coord point) const {
        if (point.x<0 || point.y<0 || point.x>=width_ || point.y>=height_) return 0;
        return launchers_[static_cast<size_t>(point.y)*width_+point.x];
    }
    /// Safe for a wing of `localAircraft`. Zero is the conservative reading used
    /// by withdrawal and escape: any known cover at all is unsafe.
    bool safe(Coord point, int localAircraft) const {
        if (point.x<0 || point.y<0 || point.x>=width_ || point.y>=height_) return false;
        const size_t index=static_cast<size_t>(point.y)*width_+point.x;
        if (blocked_[index]) return false;
        return localWingPermits(localAircraft,launchers_[index]);
    }
    bool safe(Coord point) const { return safe(point,0); }
    bool clearFootprint(Coord origin, Coord size, int localAircraft = 0) const {
        for (int y=0;y<size.y;++y) for(int x=0;x<size.x;++x)
            if (!safe(Coord(origin.x+x,origin.y+y),localAircraft)) return false;
        return true;
    }
    bool clearApproach(Coord from, Coord to, int localAircraft = 0) const {
        const int steps=std::max({1,std::abs(to.x-from.x),std::abs(to.y-from.y)});
        for(int step=0;step<=steps;++step)
            if (!safe(Coord(from.x+(to.x-from.x)*step/steps,
                            from.y+(to.y-from.y)*step/steps),localAircraft)) return false;
        return true;
    }
    // Escape may start inside newly arrived AA coverage, but cannot re-enter it.
    bool clearWithdrawal(Coord from, Coord to) const {
        if (!safe(to)) return false;
        bool reachedSafety = safe(from);
        const int steps=std::max({1,std::abs(to.x-from.x),std::abs(to.y-from.y)});
        for(int step=0;step<=steps;++step) {
            const bool clear=safe(Coord(from.x+(to.x-from.x)*step/steps,
                from.y+(to.y-from.y)*step/steps));
            if(reachedSafety && !clear) return false;
            reachedSafety = reachedSafety || clear;
        }
        return true;
    }
    Coord escape(Coord from) const {
        // Closest safe straight exit; deterministic and used only during danger.
        // The answer is the minimum Manhattan distance below width+height,
        // ties going to the first point in raster order (y, then x). Walking
        // the diamonds outward and each diamond row by row, left point before
        // right, visits candidates in exactly that order, so the first pass
        // is the raster scan's choice without testing the rest of the map.
        Coord best; best.invalidate();
        if (width_<=0 || height_<=0) return best;
        const int nearX=std::max({0,-from.x,from.x-(width_-1)});
        const int nearY=std::max({0,-from.y,from.y-(height_-1)});
        const int farX=std::max(std::abs(from.x),std::abs(from.x-(width_-1)));
        const int farY=std::max(std::abs(from.y),std::abs(from.y-(height_-1)));
        const int last=std::min(farX+farY,width_+height_-1);
        for (int d=nearX+nearY;d<=last;++d) {
            for (int y=std::max(0,from.y-d);y<=std::min(height_-1,from.y+d);++y) {
                const int reach=d-std::abs(y-from.y);
                for (const int x : {from.x-reach,from.x+reach}) {
                    if (x<0 || x>=width_) continue;
                    const Coord point(x,y);
                    if (safe(point) && clearWithdrawal(from,point)) return point;
                    if (reach==0) break;
                }
            }
        }
        return best;
    }
private:
    // Visits the index of every map tile within blockDistance `range` of
    // `centre`, the same tile set the per-tile predicate scan produced.
    template<typename Visit>
    void forEachCovered(Coord centre, int range, Visit visit) {
        if (range<0) return;
        if (static_cast<size_t>(range)>=spansByRange_.size()) spansByRange_.resize(static_cast<size_t>(range)+1);
        auto& spans=spansByRange_[range];
        if (spans.empty()) spans=coverageSpans(range);
        for (int y=std::max(0,centre.y-range);y<=std::min(height_-1,centre.y+range);++y) {
            const int span=spans[std::abs(y-centre.y)];
            const size_t row=static_cast<size_t>(y)*width_;
            for (int x=std::max(0,centre.x-span);x<=std::min(width_-1,centre.x+span);++x)
                visit(row+x);
        }
    }
    int width_,height_;
    std::vector<bool> blocked_;        ///< Absolute cover: rocket turrets, Deviators.
    std::vector<int> launchers_;       ///< How many mobile launchers cover each tile.
    std::vector<bool> strict_;         ///< Any observed anti-air at its anti-air reach.
    std::vector<std::vector<int>> spansByRange_; ///< coverageSpans(range), built on first use.
    bool strictEnabled_ = false;
};
} // namespace AirStrikePolicy
#endif
