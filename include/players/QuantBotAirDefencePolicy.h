#ifndef QUANTBOT_AIR_DEFENCE_POLICY_H
#define QUANTBOT_AIR_DEFENCE_POLICY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

// AirRaidTurrets ([Force Concentration], on by default). Stefan, 2026-11-23: "also make the ai
// respond when being bombed by planes by having more turrets dispersed".
//
// A city airplane crosses the whole city on a bombing run, so Rocket Turrets at the core or on
// the edge fire at it for only part of the way. While a house's buildings are being bombed, its
// QuantBot adds Rocket Turrets spread through the city, one at a time, each beside a building
// that has none near it, starting where the bombs fell.
//
// This header holds the house's record of the raid (House keeps it and saves it) and the
// arithmetic of the answer. QuantBot reads the map and places the order
// (QuantBot::planAirRaidDefence, chooseYardAirRaidDefence, findAirRaidTurretSite). Everything
// here is pure, so the unit tests include it without the game.
namespace QuantBotAirDefencePolicy {

/// Game cycles in a game minute. The same number as DuneCity::kCyclesPerMinute
/// (QuantBotBuildYard.cpp asserts it); kept here so this header needs no city header.
constexpr uint32_t kCyclesPerMinute = 3750;

// --- When -------------------------------------------------------------------------------

/// A house counts as bombed for three game minutes after the last bomb on one of its buildings.
/// That bridges the pauses between the runs of one raid and leaves time to place a few
/// turrets; one stray stick does not keep the house building for the rest of the game.
constexpr uint32_t kBombedWindowCycles = 3 * kCyclesPerMinute;

/// \a lastHitCycle is 0 for a house that was never bombed.
constexpr bool bombedRecently(uint32_t lastHitCycle, uint32_t now) {
    return lastHitCycle != 0 && (now <= lastHitCycle || now - lastHitCycle <= kBombedWindowCycles);
}

// --- How dense --------------------------------------------------------------------------

/// Every building of the city should have an own Rocket Turret within eight tiles of its
/// footprint (Chebyshev tiles). A Rocket Turret reaches a city airplane at 16 tiles (twice
/// its WeaponRange of 8, CityAircraft::kTurretPlaneReachFactor), so a plane over a covered
/// building is at most half that reach from one turret, and the turrets of the buildings
/// around it are inside the reach too, so several fire at it.
constexpr int kCoverTiles = 8;
/// A new turret keeps at least six tiles (Chebyshev) from every own Rocket Turret, standing or
/// planned, so the turrets do not clump.
constexpr int kMinSpacingTiles = 6;
/// A site is at most three tiles from the footprint of the building it is chosen for: inside
/// the city, and close enough that the new turret covers that building.
constexpr int kSiteReachTiles = 3;
// A building is uncovered when every turret is at least kCoverTiles + 1 tiles from it, so every
// site within kSiteReachTiles of it already keeps the spacing. The two rules cannot leave an
// uncovered building without a candidate.
static_assert(kCoverTiles + 1 - kSiteReachTiles >= kMinSpacingTiles, "spacing would rule out every site of an uncovered building");

/// A building within ten tiles of a recent bomb hit is in that hit's neighbourhood. Those
/// buildings get their turret before the rest of the city.
constexpr int kHitNeighbourhoodTiles = 10;
/// Beyond two cover radii from the nearest turret, more distance is no better.
constexpr int kSpreadCapTiles = 2 * kCoverTiles;
/// One search tries the eight best uncovered buildings, so a few walled-in ones do not block
/// the rest, and the tiles it tests stay bounded (at most a 10x10 window per building).
constexpr int kTargetsTried = 8;

constexpr int kNoTurret = std::numeric_limits<int>::max();   ///< nearest-turret distance of a house without one
constexpr int kNoHit = std::numeric_limits<int>::max();      ///< hit distance when no recent hit is on record

constexpr int absolute(int value) { return value < 0 ? -value : value; }
/// Chebyshev tiles between two tiles.
constexpr int tileDistance(int ax, int ay, int bx, int by) {
    return std::max(absolute(ax - bx), absolute(ay - by));
}
/// Chebyshev tiles from tile (\a px, \a py) to the footprint at (\a x, \a y) of \a w by \a h tiles; 0 inside it.
constexpr int footprintDistance(int px, int py, int x, int y, int w, int h) {
    const int dx = px < x ? x - px : px > x + w - 1 ? px - (x + w - 1) : 0;
    const int dy = py < y ? y - py : py > y + h - 1 ? py - (y + h - 1) : 0;
    return std::max(dx, dy);
}
/// A building is covered when its nearest own Rocket Turret is within kCoverTiles of its footprint.
constexpr bool covered(int nearestTurret) { return nearestTurret <= kCoverTiles; }
/// A site keeps the spacing when its nearest own Rocket Turret is at least kMinSpacingTiles away.
constexpr bool spacedEnough(int nearestTurret) { return nearestTurret >= kMinSpacingTiles; }

/// Which uncovered building gets the next turret; the highest rank wins. First the buildings in
/// the neighbourhood of a recent hit, then the one farthest from every own Rocket Turret, then
/// the one nearest the hit. \a hitDistance is kNoHit without a recent hit.
constexpr int targetRank(int hitDistance, int nearestTurret) {
    const bool nearHit = hitDistance <= kHitNeighbourhoodTiles;
    return (nearHit ? 100000 : 0) + std::min(nearestTurret, kSpreadCapTiles) * 100 - (nearHit ? hitDistance : 0);
}
/// Which legal tile beside that building; the highest score wins. Farthest from the own Rocket
/// Turrets first, then nearest the hit, then nearest the building.
constexpr int siteScore(int nearestTurret, int hitDistance, int buildingDistance) {
    return std::min(nearestTurret, kSpreadCapTiles) * 1000
        - (hitDistance == kNoHit ? 0 : std::min(hitDistance, 99) * 10) - buildingDistance;
}

// --- How many, how fast, from what money ------------------------------------------------

/// At most two of these turrets are ordered in a game minute.
constexpr int kMaxTurretsPerMinute = 2;
constexpr uint32_t kOrderIntervalCycles = kCyclesPerMinute / kMaxTurretsPerMinute;
/// At most eight are ordered against one raid. A raid ends when no bomb has landed for the window.
constexpr int kMaxTurretsPerRaid = 8;
/// No order while the house has 24 Rocket Turrets, standing and queued, whatever their reason.
constexpr int kMaxRocketTurrets = 24;
/// Cash the house keeps, on top of the build pass's largest reserve, after paying for a turret.
/// Two Rocket Turret prices: the turret is bought from spare cash and never saved for.
constexpr int kCashFloor = 500;

/// What must remain after paying: the largest reserve this build pass keeps (the same four that
/// QuantBot::buyCityAirplanes respects) plus kCashFloor.
constexpr int cashReserve(int economyReserve, int strategicReserve, int civicReserve, int cityWorkingReserve) {
    return std::max({economyReserve, strategicReserve, civicReserve, cityWorkingReserve, 0}) + kCashFloor;
}
/// \a cost is the turret with its foundation, \a reserve from cashReserve().
constexpr bool affordable(int money, int cost, int reserve) {
    return int64_t(money) >= int64_t(cost) + reserve;
}

// --- The house's record -----------------------------------------------------------------

/// The record keeps the eight most recent hit neighbourhoods. A bomb within four tiles of a
/// kept one refreshes it, so one stick of bombs does not use up the whole list.
constexpr int kMaxHits = 8;
constexpr int kHitMergeTiles = 4;

struct Hit {
    int16_t x = 0, y = 0;   ///< tile the bomb landed on
    uint32_t cycle = 0;     ///< game cycle it landed
};

/// What a house knows about being bombed by city airplanes, and what its QuantBot has ordered
/// in answer. Written by the bomb's impact (Bullet.cpp) and by QuantBot when it orders a turret.
/// Saved with the House from SAVEGAMEVERSION_AIR_RAID_RECORD.
struct RaidRecord {
    uint32_t lastHitCycle = 0;      ///< cycle of the last bomb on one of the house's buildings; 0 = never
    uint32_t lastTurretCycle = 0;   ///< cycle the last turret was ordered against a raid; 0 = never
    uint8_t turretsOrdered = 0;     ///< turrets ordered against the current raid
    uint8_t hitCount = 0;           ///< entries of hits in use
    std::array<Hit, kMaxHits> hits{};

    /// A bomb landed on one of the house's buildings at tile (\a x, \a y).
    void noteHit(int x, int y, uint32_t cycle) {
        cycle = std::max<uint32_t>(1, cycle);
        // The first bomb after a quiet window opens a new raid with its own turret allowance.
        if (!bombedRecently(lastHitCycle, cycle)) turretsOrdered = 0;
        lastHitCycle = cycle;
        int slot = -1;
        for (int i = 0; i < hitCount && slot < 0; ++i)
            if (tileDistance(hits[i].x, hits[i].y, x, y) <= kHitMergeTiles) slot = i;
        if (slot < 0 && hitCount < kMaxHits) slot = hitCount++;
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < kMaxHits; ++i)
                if (hits[i].cycle < hits[slot].cycle) slot = i;
        }
        const int limit = std::numeric_limits<int16_t>::max();
        hits[slot] = Hit{int16_t(std::clamp(x, 0, limit)), int16_t(std::clamp(y, 0, limit)), cycle};
    }

    /// QuantBot ordered a Rocket Turret against the raid.
    void noteTurretOrdered(uint32_t cycle) {
        lastTurretCycle = std::max<uint32_t>(1, cycle);
        if (turretsOrdered < std::numeric_limits<uint8_t>::max()) ++turretsOrdered;
    }
};

/// Why no turret may be ordered now; None when one may.
enum class Hold { None, NotBombed, HouseCap, RaidCap, Pace };

/// \a rocketTurrets counts the house's Rocket Turrets, standing and queued.
constexpr Hold orderHold(const RaidRecord& raid, uint32_t now, int rocketTurrets) {
    if (!bombedRecently(raid.lastHitCycle, now)) return Hold::NotBombed;
    if (rocketTurrets >= kMaxRocketTurrets) return Hold::HouseCap;
    if (raid.turretsOrdered >= kMaxTurretsPerRaid) return Hold::RaidCap;
    if (raid.lastTurretCycle != 0 && now >= raid.lastTurretCycle
        && now - raid.lastTurretCycle < kOrderIntervalCycles) return Hold::Pace;
    return Hold::None;
}

constexpr const char* holdName(Hold hold) {
    return hold == Hold::NotBombed ? "not_bombed" : hold == Hold::HouseCap ? "house_cap"
        : hold == Hold::RaidCap ? "raid_cap" : hold == Hold::Pace ? "pace" : "none";
}

} // namespace QuantBotAirDefencePolicy

#endif
