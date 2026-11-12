#ifndef QUANTBOT_SPICE_BLOOM_POLICY_H
#define QUANTBOT_SPICE_BLOOM_POLICY_H

#include <data.h>

#include <cstdint>
#include <tuple>

// SpiceBloomHarvest (docs/ai-scenario-matrix.md, "Spice bloom harvest"): when a house shoots a
// spice bloom so that it bursts into a field for its own harvesters. The rules are pure;
// QuantBotSpiceBloom.cpp gathers what the house has observed and applies them.
namespace QuantBotSpiceBloomPolicy {

/// Tile::triggerSpiceBloom turns every plain sand tile within this radius into thin spice
/// (Map::createSpiceField(location, 5, ...)); spice, dunes, rock and other blooms stay as they are.
inline constexpr int kSpillRadius = 5;
/// The waste test also counts a ring beyond the spill: spice there is a field the burst would
/// only extend, not one the harvesters lack.
inline constexpr int kWasteMargin = 2;
inline constexpr int kMeasureRadius = kSpillRadius + kWasteMargin;
/// At least half of the measured disc must be explored: an unseen neighbourhood is never
/// assumed empty.
inline constexpr int kKnownPermille = 500;
/// A bloom is worth bursting within this many tiles of one of our harvester dropoffs.
inline constexpr int kDropoffReach = 20;
/// A shooter is sent from at most this far away.
inline constexpr int kShooterReach = 32;
/// A shooter stops at its weapon range, so at least this far from the bloom: a ground unit that
/// stops on a bloom sets it off and dies.
inline constexpr int kMinimumRange = 2;
/// QuantBot takes one unit pass every 50 cycles (AIUPDATEINTERVAL); blooms are evaluated on the
/// pass that falls in the first 50 cycles of every 200, so every fourth pass (3.2 seconds).
inline constexpr uint32_t kEvaluationWindowCycles = 50;
inline constexpr uint32_t kEvaluationPeriodCycles = 200;

/// Known spice (credits) within kMeasureRadius that a level still bursts beside; -1 = never.
constexpr int64_t wasteLimit(int level) { return level == 1 ? 0 : level == 2 ? 2000 : -1; }

/// Would bursting waste the bloom? Level 1 bursts only beside an empty field, level 2 beside
/// at most 2,000 credits of known spice.
constexpr bool wasteful(int level, int64_t knownSpice) {
    const int64_t limit = wasteLimit(level);
    return limit < 0 || knownSpice > limit;
}

/// Has the house explored enough of the measured disc to judge it?
constexpr bool enoughKnown(int exploredTiles, int tiles) {
    return tiles > 0 && int64_t(exploredTiles) * 1000 >= int64_t(tiles) * kKnownPermille;
}

/// Our side of the map: no closer to a known enemy building than to one of ours. A negative
/// distance means none is known; without a building of our own nothing is our side.
constexpr bool ownSide(int ownDistance, int enemyDistance) {
    return ownDistance >= 0 && (enemyDistance < 0 || ownDistance <= enemyDistance);
}

/// The cadence, from the cycle alone so a reloaded game evaluates on the same passes.
constexpr bool evaluationDue(uint32_t cycle) { return cycle % kEvaluationPeriodCycles < kEvaluationWindowCycles; }

/// Projectiles that set a bloom off where they land (Map::damage with terrain effects; Sonic
/// waves never do) and leave nothing behind: Deviator gas, healing, slowing and flame missiles
/// are not used. Rockets scatter and may need another shot; shells and small rockets do not.
constexpr bool burstingBullet(int bulletType) {
    return bulletType == Bullet_ShellSmall || bulletType == Bullet_ShellMedium || bulletType == Bullet_ShellLarge
        || bulletType == Bullet_SmallRocket || bulletType == Bullet_Rocket;
}

/// A shooter candidate. Better is cheaper, then idle before moving, then nearer, then the
/// lower object id, so every peer picks the same unit.
struct Shooter {
    int price = 0;
    bool moving = false;
    int distance = 0;
    uint32_t id = 0;
};
constexpr bool betterShooter(const Shooter& a, const Shooter& b) {
    return std::tie(a.price, a.moving, a.distance, a.id) < std::tie(b.price, b.moving, b.distance, b.id);
}

} // namespace QuantBotSpiceBloomPolicy

#endif // QUANTBOT_SPICE_BLOOM_POLICY_H
