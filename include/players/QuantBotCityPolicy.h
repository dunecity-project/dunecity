/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QUANTBOTCITYPOLICY_H
#define QUANTBOTCITYPOLICY_H

#include <algorithm>
#include <cstdint>

/// Pure decision table for the CUSTOM-GAME city size of one AI house.
///
/// Human players are never consulted here, and neither is the campaign (the
/// campaign keeps its own, stricter gates in QuantBotCityCampaignPolicy, which
/// includes the campaign helper running in the internal Custom game mode).
/// Nothing here grants anything: every value is a ceiling. The limit is a
/// DISPLAYED population, i.e. the number a player reads off the city UI, which
/// is the internal simulation population multiplied by kPopDisplayMultiplier.
///
/// The bot keeps choosing its own R/C/I mix: the zone ceiling is one shared
/// allowance for residential+commercial+industrial, placed plus queued.
namespace QuantBotCityPolicy {

/// Mirrors QuantBot::Difficulty so the table stays testable without the engine.
enum Difficulty { Easy = 0, Medium = 1, Hard = 2, Brutal = 3, Defend = 4 };

/// No ceiling at all (Brutal and Defend).
constexpr int kUnlimited = -1;

/// Must match DuneCity::CitySimulation::kPopDisplayMultiplier.
constexpr int kPopDisplayMultiplier = 20;

/// Map tile-area band edges (width*height), inclusive upper bounds.
constexpr int kSmallMapArea = 1024;   ///< up to 32x32
constexpr int kMediumMapArea = 4096;  ///< up to 64x64
constexpr int kLargeMapArea = 16384;  ///< up to 128x128; anything larger is "huge"

/// Anti-sprawl anchor: the accepted Easy 128x128 city is 48 zones at a 20000
/// displayed population. Every other cell of the matrix scales from that same
/// ratio, so a larger allowed population always buys proportionally more land.
constexpr int kZoneCapReferencePopulation = 20000;
constexpr int kZoneCapReferenceZones = 48;

struct Limits {
    int displayPopulationLimit = kUnlimited; ///< displayed (UI) population ceiling
    int sharedZoneCap = kUnlimited;          ///< TOTAL R+C+I, queued included
};

/// Tile-area band index 0..3 (small, medium, large, huge).
inline int areaBand(int mapArea) {
    if (mapArea <= kSmallMapArea) return 0;
    if (mapArea <= kMediumMapArea) return 1;
    if (mapArea <= kLargeMapArea) return 2;
    return 3;
}

/// The city UI shows the internal simulation population multiplied out.
inline int displayPopulation(int internalPopulation) {
    return std::max(0, internalPopulation) * kPopDisplayMultiplier;
}

/// Brutal and Defend have no city size ceiling of any kind.
inline bool unlimited(int difficulty) {
    return difficulty != Easy && difficulty != Medium && difficulty != Hard;
}

/// The approved displayed-population matrix. kUnlimited where uncapped.
/// @a hardMatchesLargest (QuantBotConfig force.hardCityMatch, on by default since the
/// 2026-11-01 change) is Hard's 20k/40k/60k/90k row; off keeps the former
/// 20k/40k/80k/120k row. Either way this is the base ceiling before
/// withLargestRival().
inline int populationLimit(int difficulty, int mapArea, bool hardMatchesLargest) {
    if (unlimited(difficulty)) return kUnlimited;
    static constexpr int kMatrix[3][4] = {
        {  5000,  10000,  20000,  30000 }, // Easy
        { 10000,  20000,  40000,  60000 }, // Medium
        { 20000,  40000,  80000, 120000 }, // Hard (switch off)
    };
    static constexpr int kHardMatchingLargest[4] = { 20000, 40000, 60000, 90000 };
    const int band = areaBand(mapArea);
    return difficulty == Hard && hardMatchesLargest ? kHardMatchingLargest[band] : kMatrix[difficulty][band];
}

/// Hard (with force.hardCityMatch) may grow past its base ceiling while another house's
/// city is bigger, up to that house's displayed population: the ceiling is the larger of
/// the two. It never shrinks below the base, and an uncapped base stays uncapped.
inline int withLargestRival(int baseLimit, int largestOtherDisplayPopulation) {
    if (baseLimit == kUnlimited) return kUnlimited;
    return std::max(baseLimit, largestOtherDisplayPopulation);
}

/// Shared R+C+I ceiling for a displayed-population ceiling, rounded up so a
/// small map still gets a workable city. Uncapped wherever population is.
inline int zoneLimit(int displayPopulationLimit) {
    if (displayPopulationLimit == kUnlimited) return kUnlimited;
    return static_cast<int>((static_cast<int64_t>(displayPopulationLimit) * kZoneCapReferenceZones
                             + kZoneCapReferencePopulation - 1) / kZoneCapReferencePopulation);
}

inline Limits limits(int displayPopulationLimit) {
    return Limits{displayPopulationLimit, zoneLimit(displayPopulationLimit)};
}

/// This policy is a custom-game rule only. Campaign games (including the
/// helper that runs the campaign in the internal Custom game mode) and
/// non-city games keep their existing behaviour untouched.
inline bool appliesToGame(bool citySimEnabled, bool campaignGameType) {
    return citySimEnabled && !campaignGameType;
}

/// Land ceiling: placed plus queued zones share one allowance.
inline bool allowsZone(int zonesIncludingQueued, int sharedZoneCap) {
    return sharedZoneCap == kUnlimited || zonesIncludingQueued < sharedZoneCap;
}

/// Population ceiling. @a pendingDisplayPop is the population the already
/// committed city still has to take on (queued zones plus zones that have not
/// reached their initial occupancy yet), so a burst of orders in one pass
/// cannot overshoot the ceiling while every single order still looks legal.
/// Upgrades, maintenance, power, roads and services are not population and are
/// never gated here.
inline bool allowsPopulation(int currentDisplayPop, int pendingDisplayPop,
                             int nextZoneDisplayPop, int displayPopulationLimit) {
    if (displayPopulationLimit == kUnlimited) return true;
    return static_cast<int64_t>(std::max(0, currentDisplayPop)) + std::max(0, pendingDisplayPop)
         + std::max(0, nextZoneDisplayPop) <= displayPopulationLimit;
}

/// One more zone is admitted only when it fits both ceilings.
inline bool admitsZone(const Limits& ceilings, int zonesIncludingQueued,
                       int currentDisplayPop, int pendingDisplayPop, int nextZoneDisplayPop) {
    return allowsZone(zonesIncludingQueued, ceilings.sharedZoneCap)
        && allowsPopulation(currentDisplayPop, pendingDisplayPop, nextZoneDisplayPop,
                            ceilings.displayPopulationLimit);
}

} // namespace QuantBotCityPolicy

#endif // QUANTBOTCITYPOLICY_H
