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
#include <data.h>

#include <algorithm>
#include <limits>

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
/// Yards the house aims for by game minute (his schedule; kept for the record and the unit test).
inline int economyFirstYards(int minute) { return minute < 15 ? 4 : minute < 20 ? 6 : 9; }
/// The yards the switch aims for from the room at home (Stefan, 2026-11-28: extra yards are build capacity while
/// there is room to build): one per kEconomyFirstFootprintsPerYard free 2x2 footprints on the home rock in build
/// range, from 1 up to his 9. A house with no room left adds no yard at home; its MCVs settle new rock.
constexpr int kEconomyFirstFootprintsPerYard = 10, kEconomyFirstMaxYards = 9;
/// His opening (orders in his Air game): Palaces 3 by minute 3, 6 by 10, 8 by 20, 15 by 31; 19 zones in the first
/// three minutes. The switch plans the Palaces while it can pay for one and holds the zones' price in the opening.
inline int economyFirstPalaces(int minute) { return minute < 3 ? 3 : minute < 10 ? 6 : minute < 20 ? 8 : 15; }
constexpr int kEconomyFirstOpeningMinutes = 3, kEconomyFirstOpeningZones = 19;
/// Factory-value orders excluding MCVs: his 3,850 by minute 3 and 8,050 by 10; no pacing after 10.
inline int economyFirstUnitAllowance(std::uint32_t cycle, std::uint32_t perMinute) {
    if (cycle >= 10 * perMinute) return std::numeric_limits<int>::max();
    if (cycle <= 3 * perMinute) return int(int64_t(3850) * cycle / (3 * perMinute));
    return 3850 + int(int64_t(4200) * (cycle - 3 * perMinute) / (7 * perMinute));
}
/// New Rocket Turret orders on his schedule, interpolated between his measured marks.
inline int economyFirstTurretTarget(std::uint32_t cycle, std::uint32_t perMinute) {
    if (cycle < 3 * perMinute) return 0;
    if (cycle < 10 * perMinute) return int((int64_t(29) * (cycle - 3 * perMinute) + 7 * perMinute - 1) / (7 * perMinute));
    if (cycle < 20 * perMinute) return 29 + int((int64_t(21) * (cycle - 10 * perMinute) + 10 * perMinute - 1) / (10 * perMinute));
    if (cycle < 31 * perMinute) return 50 + int((int64_t(27) * (cycle - 20 * perMinute) + 11 * perMinute - 1) / (11 * perMinute));
    return 77;
}
inline int economyFirstYardsForRoom(int freeFootprints) {
    return std::clamp(1 + std::max(0, freeFootprints) / kEconomyFirstFootprintsPerYard, 1, kEconomyFirstMaxYards);
}
/// While the switch is on and no enemy ground unit is near our buildings, unit producers leave half the cash
/// above kEconomyFirstHoldFloor for the yards, at most kEconomyFirstHold (his balance at minute 31 was 22,848
/// against the AIs' 0-820). A flat 10,000 starved every affordable unit, import and factory order in a poor
/// house (12 tests at 54a8a1189); scaled, a poor house keeps its ordinary spending.
constexpr int kEconomyFirstHold = 10000;
constexpr int kEconomyFirstHoldFloor = 2000;
inline int economyFirstHold(int credits) {
    return std::clamp((credits - kEconomyFirstHoldFloor) / 2, 0, kEconomyFirstHold);
}
/// An enemy ground unit this close to one of these releases the hold for the factories (the base is defended).
inline bool economyFirstCoreAsset(int item) {
    return item == Structure_ConstructionYard || item == Structure_HeavyFactory || item == Structure_HighTechFactory
        || item == Structure_LightFactory || item == Structure_Barracks || item == Structure_WOR
        || item == Structure_StarPort || item == Structure_NuclearPlant || item == Structure_Palace;
}
constexpr int kEconomyFirstThreatTiles = 12;
/// The switch stands down once the house's city has his minute-31 population.
constexpr int kEconomyFirstPopulation = 6734;
/// His zone orders by minute 31 (134 R, 93 C, 101 I) and zones' share of his yard orders in the first ten
/// minutes (76 zones of 113 orders: about 70%).
constexpr int kEconomyFirstZoneMix[3] = {134, 93, 101};
constexpr int kEconomyFirstZoneShareBps = 7000;
/// Stefan's scope (2026-11-28: "the ai needs to escape the poverty trap on low / no spice maps"): the switch
/// acts only while the spice left on the map is below this much per living house (one starting purse of
/// his game; his map had none). On a spice-rich map it does nothing.
constexpr int kEconomyFirstSpicePerHouse = 20000;
inline bool lowSpiceMap(int mapSpice, int livingHouses) {
    return mapSpice < kEconomyFirstSpicePerHouse * std::max(1, livingHouses);
}
/// Read at minute 10. The proximate effect (activation): an MCV ordered and at least kEconomyFirstEffectZones zones
/// added since the first pass. His footprint, reported beside it: half the zones he added by then (76 ordered:
/// 33 R, 8 C, 35 I) and his four yards.
constexpr int kEconomyFirstCheckMinute = 10, kEconomyFirstEffectZones = 10;
constexpr int kEconomyFirstZones = 38, kEconomyFirstCheckYards = 4;
/// ECONOMY_STATE: credits, yards, zones, power, population and tax are logged this often.
constexpr int kEconomyLogMs = 60000;

} // namespace CityLessonsPolicy

#endif // CITYLESSONSPOLICY_H
