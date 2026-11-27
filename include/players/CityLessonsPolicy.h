#ifndef CITYLESSONSPOLICY_H
#define CITYLESSONSPOLICY_H

// Two default-off QuantBot switches from Stefan's Metropolis save seewhatidid (2026-11-27: "i added
// more palaces to improve land, added rocket turrets as a maginot and built a lot of launchers. and my
// ai is doing better than its opponents"; QuantBotCityLessons.cpp, docs/formations.md "What Stefan did
// on Metropolis").
//
//  CityLauncherArmy  In a city game the vehicle mix puts its Ornithopter share into Launchers and keeps
//                    Launchers at no less than CityLauncherArmyBps of it while a factory can build them.
//                    In every house of the save, and in the 24 AI-vs-AI Metropolis games before it,
//                    Launchers returned the most damage per credit of any unit (2.5 credits of damage
//                    per credit spent; Siege Tank 1.5, Tank 0.8, Ornithopter 0.4).
//  LowValuePalaces   A Palace goes where most of our low-value R/C lots are (land value at most
//                    kLowLandValue), whether or not another Palace already reaches them - Palace land
//                    value stamps add up - and it is saved for, not bought from spare cash only.

#include <players/UnitMixPolicy.h>

#include <algorithm>

#include <cstdint>

namespace CityLessonsPolicy {

/// CityLauncherArmy: the default floor of the Launcher share of the vehicle mix, and its bounds.
constexpr int kLauncherArmyBps = 7000;
constexpr int kLauncherArmyMinBps = 3000, kLauncherArmyMaxBps = 9500;
constexpr std::size_t kLauncherSlot = 2;

/// The vehicle mix with the Ornithopter share moved into Launchers and Launchers floored at floorBps.
/// The total is unchanged (10000 when the input sums to it).
inline UnitMixPolicy::Mix launcherArmy(const UnitMixPolicy::Mix& mix, int floorBps) {
    UnitMixPolicy::Mix result = mix;
    result[kLauncherSlot] += std::max(0, result[UnitMixPolicy::kOrnithopterSlot]);
    result[UnitMixPolicy::kOrnithopterSlot] = 0;
    return UnitMixPolicy::applyFloor(result, kLauncherSlot, floorBps);
}

/// LowValuePalaces: a lot this cheap counts as low value; a site needs this many of them in reach.
constexpr int kLowLandValue = 80;
constexpr int kMinLowLots = 6;
/// The effect is read this long after the Palace stands (two city land-value scans and more).
constexpr int kEffectDelayMs = 120000;
/// A planned site whose Palace has not stood up by then is given up (and logged as expired).
constexpr int kPlanExpiryMs = 300000;
/// The effect counts as reached when the lots rose by this much with the Palace still standing.
constexpr int kMinRise = 10;

/// FrontPalaces: Stefan's placement in seewhatiddid/moremaginot. His 26 Palaces stood on the enemy
/// side of his city, 10-31 tiles from the nearest enemy building, behind his own Rocket Turret clumps,
/// and all survived while his house lost 27 others; he added 19 by minute 72 and 26 by minute 95.
constexpr int kFrontPalaceMinEnemyTiles = 10, kFrontPalaceMaxEnemyTiles = 31;
/// Own Rocket Turrets within kFrontPalaceCoverTiles of the site and nearer the enemy than it.
constexpr int kFrontPalaceTurretsAhead = 3;
constexpr int kFrontPalaceCoverTiles = 10;
/// Own R/C lots in reach (his: 11-35).
constexpr int kFrontPalaceMinLots = 6;
/// At most one new site this often (his pace: 19 by minute 72, 7 more by minute 95).
constexpr int kFrontPalaceIntervalMs = 120000;
/// Its effect - still standing - is read this long after it was placed.
constexpr int kFrontPalaceSurvivalMs = 300000;

/// FrontPalaces: may a Palace centred where the nearest enemy building is enemyTiles away, with
/// turretsAhead own turrets covering it from the enemy side, go there?
inline bool frontPalaceCentre(int enemyTiles, int turretsAhead) {
    return enemyTiles >= kFrontPalaceMinEnemyTiles && enemyTiles <= kFrontPalaceMaxEnemyTiles
        && turretsAhead >= kFrontPalaceTurretsAhead;
}

inline bool lowLot(int landValue) { return landValue <= kLowLandValue; }

} // namespace CityLessonsPolicy

#endif // CITYLESSONSPOLICY_H
