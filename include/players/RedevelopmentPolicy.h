#ifndef REDEVELOPMENT_POLICY_H
#define REDEVELOPMENT_POLICY_H
#include <algorithm>
#include <cstdlib>
namespace RedevelopmentPolicy {
// R's demand ceiling is2000; C/I1500. Compare equal fractions equally.
inline int displacementCost(int density, int landValue, int demand, int demandCeiling) {
    const int normalized = std::clamp(demand*1000/std::max(1,demandCeiling),-1000,1000);
    return (normalized+1000)*20 + (density>0 ? 10000:0) + density*1000
        + std::max(0,landValue);
}

// --- City redevelopment for land value (QuantBot [Force Concentration] RoadLaneParks, LotParks,
// PollutionRelocation; default off; docs/formations.md "Redevelopment for land value").

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
/// How often a house may relocate one Industrial zone: one search window per period.
constexpr int kRelocatePeriodMs = 120000;
constexpr int kRelocateWindowMs = 2000;
/// Spare-lane and lot-park searches run in a short window each period, not on every yard pass.
constexpr int kParkSearchPeriodMs = 8000;
constexpr int kParkSearchWindowMs = 1000;

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
