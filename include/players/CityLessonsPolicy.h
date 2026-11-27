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
/// A site whose plan expired is not planned again (within this many tiles) for this long: at 86dcb77ea
/// the same site was planned and expired again (Metropolis 9444, Ordos at (26,59) twice).
constexpr int kExpiredSiteMs = 600000;
constexpr int kExpiredSiteTiles = 2;
/// The placement checks a held lesson Palace failed (logged as blocked= at expiry).
enum BlockCheck : int {
    kBlockOwnUnits = 1, kBlockRoadConnections = 2, kBlockReserved = 4, kBlockGroundAccess = 8,
    kBlockRecentLoss = 16, kBlockDanger = 32, kBlockReactor = 64,
    // What failed kBlockOwnUnits: a structure (or mountain) on the footprint, a unit there that is not ours
    // or cannot leave, or the engine's own placement test (build range, terrain).
    kBlockStructure = 128, kBlockUnit = 256, kBlockEngine = 512
};
/// CITY_LOTS: each house's lots, low-value lots and mean land value are logged this often.
constexpr int kLotsLogMs = 180000;

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

/// EconomyFirst: Stefan's last Air game (2026-11-28, "i did the building for the ai and we obliterated
/// the enemy opponents"; 4P 192x192 Random 1v3 FFA DuneCity T9, seed 817478863, 20,000 credits each).
/// He had 4 Construction Yards by minute 2.2, 6 by 17.4 and 9 by 22.7, and spent little on units in the
/// first ten minutes; the three Brutal AIs each put about 15,000 of their 20,000 into units within two
/// minutes, then stayed below 1,000 credits with one yard for the rest of the game.
/// Yards the house aims for by game minute (his schedule).
inline int economyFirstYards(int minute) { return minute < 15 ? 4 : minute < 20 ? 6 : 9; }
/// While the switch is on and no enemy ground unit is near our buildings, unit producers leave this
/// much cash for the yards (his balance at minute 31 was 22,848 against the AIs' 0-820).
constexpr int kEconomyFirstHold = 10000;
/// An enemy ground unit this close to one of our buildings releases the hold (the base is defended).
constexpr int kEconomyFirstThreatTiles = 12;
/// The switch stands down once the house's city has his minute-31 population.
constexpr int kEconomyFirstPopulation = 6734;
/// The proximate effect, read at minute 10: half the zones he added by then (76 ordered: 33 R, 8 C, 35 I;
/// counted from the first pass) and his four yards.
constexpr int kEconomyFirstCheckMinute = 10, kEconomyFirstZones = 38, kEconomyFirstCheckYards = 4;
/// ECONOMY_STATE: credits, yards, zones, power, population and tax are logged this often.
constexpr int kEconomyLogMs = 60000;

} // namespace CityLessonsPolicy

#endif // CITYLESSONSPOLICY_H
