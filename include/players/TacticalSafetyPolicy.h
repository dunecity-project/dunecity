#ifndef TACTICAL_SAFETY_POLICY_H
#define TACTICAL_SAFETY_POLICY_H
#include <algorithm>
#include <vector>
#include <tuple>
#include <utility>
#include <data.h>
#include <players/CityPlacementPolicy.h>
#include <dunecity/SpatialFields.h>

namespace TacticalSafetyPolicy {
inline bool needsRefineryRefuge(bool returning,bool full,bool hasCargo,bool safeField) {
    return hasCargo && (returning || full || !safeField);
}

inline int harvesterThreatRadius(int item, int range) {
    if (isInfantryUnit(item)) return -1; // Harvesters can crush foot troops.
    return std::max(1,range)+(item==Unit_Launcher ? 3 : 0);
}

inline bool productionFactory(int type) {
    return type == Structure_HeavyFactory || type == Structure_LightFactory
        || type == Structure_HighTechFactory || type == Structure_Barracks || type == Structure_WOR;
}
// Distance beyond known weapon reach (including the construction safety buffer).
// Two deterministic passes cost O(map area), shared by every candidate. Beyond
// twelve clear tiles, normal placement preferences decide rather than map edges.
// `result` is reused (every element is written).
inline void enemyClearanceInto(const std::vector<int>& danger, int w, int h, std::vector<int>& result) {
    constexpr int sufficient = 12;
    result.resize(static_cast<size_t>(w)*h);
    for (size_t i=0; i<result.size(); ++i) result[i] = danger[i] > 0 ? 0 : sufficient;
    DuneCity::chessboardPasses(result, w, h);
}
inline std::vector<int> enemyClearance(const std::vector<int>& danger, int w, int h) {
    std::vector<int> result;
    enemyClearanceInto(danger, w, h, result);
    return result;
}
inline int footprintClearance(const std::vector<int>& clearance, int w, int h,
                              int x, int y, int sx, int sy) {
    if (x<0 || y<0 || x+sx>w || y+sy>h || clearance.size()!=static_cast<size_t>(w*h)) return 0;
    int result=12;
    for (int py=y; py<y+sy; ++py) for (int px=x; px<x+sx; ++px)
        result=std::min(result,clearance[py*w+px]);
    return result;
}
// Avoid known loss sites first, then prefer distance from live threats. This
// ranks legal alternatives; it never bans the only available factory site.
inline auto factorySiteRank(int lossRisk, int clearance, int tier, int score) {
    return std::make_tuple(lossRisk==0,clearance,tier,score);
}
inline bool protectedReactorNeighbour(int type) {
    return type == Structure_NuclearPlant || type == Structure_HeavyFactory
        || type == Structure_ConstructionYard || type == Structure_RepairYard
        || type == Structure_HighTechFactory || type == Structure_IX
        || type == Structure_Palace || type == Structure_StarPort || type == Structure_Refinery;
}
// Four clear tiles between footprints, exceeding the reactor's 3.66-tile radius.
inline bool blastClearance(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
    return CityPlacementPolicy::footprintDistance(x,y,w,h,bx,by,bw,bh) >= 5;
}
// Live fire is never worth trading for blast spacing or a better packing score.
// Among unexposed sites prefer no recent losses, distance beyond weapon reach,
// blast spacing, then the rear of the base. Packing only breaks safety ties.
inline auto reactorSiteRank(int threat, int loss, int enemyClearance, bool blastSpacing, int rear, int score) {
    return std::make_tuple(threat == 0, -threat, loss == 0, enemyClearance, blastSpacing, rear, -loss, score);
}
inline bool reactorPlacementAllowed(int item, bool clearance) {
    return item == Structure_NuclearPlant || clearance;
}
inline int lossStrength(unsigned age, unsigned lifetime) {
    if (!lifetime || age >= lifetime) return 0;
    return 1 + 100 * (lifetime-age) / lifetime;
}
// Skip the first tile when escaping a firing zone. For outbound spice journeys,
// the caller separately checks the vehicle's current tile before using this.
template<class Danger>
int corridorDanger(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int result = 0;
    for (int i = 2; i <= steps; ++i)
        result = std::max(result, danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps));
    return result;
}
// Escape an existing firing zone without entering stronger danger or re-entering
// danger after reaching safety. This is a corridor estimate, not a path proof.
template<class Danger>
bool escapeCorridor(int x, int y, int tx, int ty, Danger danger) {
    const int steps = std::max(std::abs(tx-x),std::abs(ty-y));
    int previous = danger(x,y);
    for (int i=1; i<=steps; ++i) {
        const int next = danger(x+(tx-x)*i/steps,y+(ty-y)*i/steps);
        if (next > previous) return false;
        previous = next;
    }
    return previous == 0;
}

/// The candidate this sequential sweep picks, with far fewer route tests:
///
///     best = INT_MAX; pick = -1;
///     for (k = 0; k < scores.size(); ++k) {
///         if (scores[k] >= best) continue;
///         if (!safe(k)) continue;
///         best = scores[k]; pick = k;
///     }
///
/// That loop keeps the first safe candidate of the lowest score: the lowest
/// (score, position) among the candidates safe() accepts. It tests every
/// candidate that improves on the running best, which, scanned in map order
/// from a far corner, is thousands of failing long corridors before the
/// nearby winner. Here candidates are tested in ascending (score, position)
/// order, in score bands that double in width, until the first safe one, so
/// only candidates at least as good as the winner are tested. safe() must be a
/// pure function of k. Scores are non-negative. \a rejected counts the tests
/// that failed. Returns the position, or -1 when no candidate is safe.
template<class Safe>
int lowestSafeCandidate(const std::vector<int>& scores, Safe safe, int& rejected) {
    rejected = 0;
    if (scores.empty()) return -1;
    const auto [minIt, maxIt] = std::minmax_element(scores.begin(), scores.end());
    const long long highest = *maxIt;
    long long low = static_cast<long long>(*minIt) - 1, width = 16;
    std::vector<std::pair<int,int>> band;
    for (;;) {
        const long long high = std::min(highest, low + width);
        band.clear();
        for (size_t k = 0; k < scores.size(); ++k)
            if (scores[k] > low && scores[k] <= high) band.emplace_back(scores[k], static_cast<int>(k));
        std::sort(band.begin(), band.end());
        for (const auto& [score, k] : band) {
            (void)score;
            if (safe(k)) return k;
            ++rejected;
        }
        if (high >= highest) return -1;
        low = high;
        width *= 2;
    }
}

}
#endif
