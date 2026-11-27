#ifndef REDEVELOPMENT_POLICY_H
#define REDEVELOPMENT_POLICY_H
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
namespace RedevelopmentPolicy {
// R's demand ceiling is2000; C/I1500. Compare equal fractions equally.
inline int displacementCost(int density, int landValue, int demand, int demandCeiling) {
    const int normalized = std::clamp(demand*1000/std::max(1,demandCeiling),-1000,1000);
    return (normalized+1000)*20 + (density>0 ? 10000:0) + density*1000
        + std::max(0,landValue);
}

// --- City redevelopment for land value (QuantBot [Force Concentration] RoadLaneParks, LotParks,
// PollutionRelocation, IndustrySeparation; default off; docs/formations.md "Redevelopment for land value").

/// LotParks: an own R/C lot this cheap (land value at most kLotParkMaxValue, density at most
/// kLotParkMaxDensity) may be cleared for parks.
constexpr int kLotParkMaxValue = 64;
constexpr int kLotParkMaxDensity = 1;
/// Park terrain the cleared lot ends up holding: one Park per tile of the 2x2 footprint.
constexpr int kLotParkSources = 4;
/// Land value the lot's parks must add, summed over the blocks of the house's other R/C lots.
constexpr int kLotParkMinGain = 24;

/// PollutionRelocation: an Industrial zone is rebuilt elsewhere when its pollution costs our R/C
/// lots at least this much land value (pollution is subtracted from land value one for one), and
/// the replacement site costs at most half as much.
constexpr int kRelocateMinHarm = 40;
/// How often a house searches for one Industrial zone to relocate, and for a park site. A search
/// is due once its period has passed since the last one and runs on the next idle yard pass
/// (2026-11-27: fixed windows of 2 s in 120 s missed the idle yard and fired once in 30 minutes).
constexpr int kRelocatePeriodMs = 30000;
constexpr int kParkSearchPeriodMs = 8000;

/// IndustrySeparation (and every PollutionRelocation site): a new Industrial zone keeps more than
/// this many tiles (footprint to footprint, Chebyshev; 1 = touching) from every own R/C lot, the
/// reach of an Industrial zone's pollution stamp (CityEffects kPollutionRadius).
constexpr int kIndustrySeparationTiles = 5;
inline bool industryTooCloseToHomes(int footprintDistance) { return footprintDistance <= kIndustrySeparationTiles; }

/// Lots whose land value a redevelopment is meant to raise: own R/C lots within this many tiles
/// (Chebyshev, origin to origin) of a new Park (two park-terrain blocks), or of a relocated
/// Industrial zone (the stamp plus the two smoothing passes, a block each).
constexpr int kParkEffectReach = 6;
constexpr int kRelocationEffectReach = 9;
/// An order not standing at its chosen site this long after it was given counts as lost
/// (cancelled, released for funds, or placed elsewhere by another rule).
constexpr int kOrderTimeoutMs = 180000;
/// The proximate effect is read this long after the building stands (several city days, so the
/// land-value scan has run on it).
constexpr int kEffectDelayMs = 30000;

/// What each redevelopment switch did in a game, per house: orders given, buildings standing at
/// the chosen site, orders lost, effect reads, and reads where the affected lots' mean land value
/// rose. Logged as SCENARIO_SWITCH_ACTIVITY by scenario-match.inc (houses.csv switch_activity).
enum Kind { LanePark, LotClear, LotFill, Relocation, KindCount };
inline const char* kindName(int k) {
    static const char* names[KindCount] = {"lane_park", "lot_clear", "lot_fill", "relocation"};
    return k >= 0 && k < KindCount ? names[k] : "?";
}
struct Activity {
    int ordered[KindCount]{}, placed[KindCount]{}, lost[KindCount]{}, checked[KindCount]{}, raised[KindCount]{};
    /// Land value summed over the effect reads: affected lots' mean before and after, times 10.
    long before10[KindCount]{}, after10[KindCount]{};
    /// Own Industrial zones placed, and those within kIndustrySeparationTiles of an own R/C lot.
    int industryPlaced = 0, industryNearHomes = 0;
    /// Why searches did or did not produce an order (gate blocks, searches, rejections), by name.
    std::map<std::string, int> reasons;
    void note(const char* reason, int n = 1) { reasons[reason] += n; }
    std::string format() const {
        std::string out;
        for (int k = 0; k < KindCount; ++k) {
            const std::string n = kindName(k);
            out += n + "_ordered=" + std::to_string(ordered[k]) + " " + n + "_placed=" + std::to_string(placed[k])
                + " " + n + "_lost=" + std::to_string(lost[k]) + " " + n + "_checked=" + std::to_string(checked[k])
                + " " + n + "_raised=" + std::to_string(raised[k]) + " " + n + "_before10=" + std::to_string(before10[k])
                + " " + n + "_after10=" + std::to_string(after10[k]) + " ";
        }
        out += "industry_placed=" + std::to_string(industryPlaced) + " industry_near_homes="
            + std::to_string(industryNearHomes);
        for (const auto& [reason, n] : reasons) out += " why_" + reason + "=" + std::to_string(n);
        return out;
    }
};

/// Land value one polluter of \a emission costs a lot whose centre is \a distance tiles from its
/// centre (CityEffects falloff: linear to zero at \a radius).
inline int pollutionHarm(int emission, int distance, int radius) {
    if (distance >= radius || radius <= 0 || emission <= 0) return 0;
    return emission * (radius - std::max(0, distance)) / radius;
}

/// Whether a relocation pays: enough harm now, and the new site at most half of it.
inline bool relocationPays(int harm, int newHarm) {
    return harm >= kRelocateMinHarm && newHarm * 2 <= harm;
}

/// Whether blocking a tile keeps every path that crossed it. \a ring holds whether each of its eight
/// neighbours is open, clockwise from north (N, NE, E, SE, S, SW, W, NW); consecutive ring tiles are
/// side neighbours. A 4-connected path through the tile enters and leaves by two side neighbours, so
/// the block is safe when every open side neighbour lies in one run of open ring tiles. Local and
/// exact for 4-connected movement; conservative for units, which may also move diagonally.
inline bool ringKeepsPassage(const bool ring[8]) {
    int start = -1;
    for (int i = 0; i < 8; ++i) if (!ring[i]) { start = i; break; }
    if (start < 0) return true;              // all open: one ring
    int runsWithSide = 0;
    bool inRun = false, runHasSide = false;
    for (int k = 1; k <= 8; ++k) {
        const int i = (start + k) % 8;
        if (ring[i]) {
            if (!inRun) { inRun = true; runHasSide = false; }
            runHasSide = runHasSide || i % 2 == 0;
        } else if (inRun) {
            runsWithSide += runHasSide;
            inRun = false;
        }
    }
    if (inRun) runsWithSide += runHasSide;
    return runsWithSide <= 1;
}
}
#endif
