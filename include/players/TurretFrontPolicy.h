#ifndef TURRET_FRONT_POLICY_H
#define TURRET_FRONT_POLICY_H

/*
    Rocket Turrets at the front (QuantBot, four default-off switches; QuantBotTurretFront.cpp).

    Stefan, 2026-11-27: "as you can see rocket turrets have an amazing return. houses should try
    use them offensively and also try increase the amount of them on front lines under attack ...
    a maginot line which moves forward. keep in mind though that the listed return of 7.4x for
    rocket turrets is slightly misleading given the ongoing cost in the budget for them (15% of
    police)".

    The upkeep. A Rocket Turret is charged 15 credits a city year in the police budget
    (DuneCity::getPoliceAnnualCost; a Police Station is 100), scaled by the police funding share
    and paid every cycle. With Slow City Year (a 120-second year) that is 7.5 credits a game minute
    a turret, 15 without it. A turret standing for 30 minutes has cost its price again. Stefan's
    3P 192x192 3waysplit game (1.0.880, 4h39m): counting the turrets each house was given on the
    map (610/336/711) as well as the 858/459/602 it built, and the police upkeep they drew, the
    winner's turrets returned 0.94x of what they cost and the losers' 2.1x; upkeep was 76-78% of
    every house's turret cost. A turret earns only while something is in its range. These switches
    therefore put turrets where the fighting is and stop paying for the ones far behind it.

    (a) FrontTurrets: an own building cluster with visible enemy ground units on it gets Rocket
        Turrets beside it until the local cover matches the attackers' value.
    (b) AdvancingTurrets: while the army stands ahead of the base's edge facing the nearest enemy,
        a belt of turrets is kept at that edge and extended toward the enemy, staying behind the
        army. Each turret extends the build range, so the belt moves forward with the army.
    (c) TurretCreep: turrets placed within range of enemy buildings, where few enemy turrets reach.
    (d) RearTurretRetire: turrets far from every enemy building and contact are sold (demolished
        where selling is off), a few at a time, keeping the base's own coverage need.

    Everything here is integer and geometric: Chebyshev distances on the tile grid and box counts.
*/

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace TurretFrontPolicy {

// --- Tile grids --------------------------------------------------------------------------------

/// Chebyshev distance of every tile to the nearest seed tile (seed = true), capped at \a cap.
/// Two passes of the 8-neighbour chamfer transform give the exact Chebyshev distance.
inline std::vector<uint16_t> chebyshevDistance(const std::vector<uint8_t>& seed, int w, int h, int cap) {
    const uint16_t far = uint16_t(std::clamp(cap, 0, 65535));
    std::vector<uint16_t> d(size_t(w) * size_t(h), far);
    for (size_t i = 0; i < d.size(); ++i) if (seed[i]) d[i] = 0;
    auto relax = [&](int x, int y, int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) return;
        uint16_t& here = d[size_t(y) * w + x];
        const uint16_t there = d[size_t(ny) * w + nx];
        if (there < far && uint16_t(there + 1) < here) here = uint16_t(there + 1);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            relax(x, y, x - 1, y); relax(x, y, x - 1, y - 1); relax(x, y, x, y - 1); relax(x, y, x + 1, y - 1);
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            relax(x, y, x + 1, y); relax(x, y, x + 1, y + 1); relax(x, y, x, y + 1); relax(x, y, x - 1, y + 1);
        }
    return d;
}

/// Summed-area table: the total of a per-tile weight over any Chebyshev box in O(1).
class BoxSum {
public:
    BoxSum() = default;
    BoxSum(const std::vector<int32_t>& weight, int w, int h) : w_(w), h_(h), s_(size_t(w + 1) * size_t(h + 1), 0) {
        for (int y = 0; y < h; ++y) {
            int64_t row = 0;
            for (int x = 0; x < w; ++x) {
                row += weight[size_t(y) * w + x];
                s_[size_t(y + 1) * (w + 1) + x + 1] = s_[size_t(y) * (w + 1) + x + 1] + row;
            }
        }
    }
    /// Sum over tiles within Chebyshev distance \a r of (\a x, \a y), clipped to the map.
    int64_t around(int x, int y, int r) const {
        if (w_ <= 0) return 0;
        const int x0 = std::max(0, x - r), y0 = std::max(0, y - r);
        const int x1 = std::min(w_ - 1, x + r), y1 = std::min(h_ - 1, y + r);
        if (x0 > x1 || y0 > y1) return 0;
        auto at = [&](int xx, int yy) { return s_[size_t(yy) * (w_ + 1) + xx]; };
        return at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) + at(x0, y0);
    }
private:
    int w_ = 0, h_ = 0;
    std::vector<int64_t> s_;
};

inline int tileDistance(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

// --- Shared ------------------------------------------------------------------------------------

/// The frontier scan and site search run at most this often per house (8 s at normal speed).
constexpr uint32_t kScanPeriodCycles = 512;
/// Distances are not tracked beyond this many tiles.
constexpr int kDistanceCap = 255;
/// New front turrets keep this Chebyshev gap from every own Rocket Turret (one splash, one
/// Death Hand or Devastator takes at most one of them), as FrontBatteryPolicy::kMinSpacing.
constexpr int kMinSpacing = 2;
inline bool spaced(int ownTurretsWithinOne) { return ownTurretsWithinOne == 0; }

// --- (a) FrontTurrets --------------------------------------------------------------------------

/// Visible enemy ground units within this many tiles of each other form one attack.
constexpr int kAttackClusterTiles = 6;
/// An attack this close to an own building is on the front.
constexpr int kAttackReachTiles = 8;
/// Own Rocket Turrets within this many tiles of the attacked building count as its cover.
constexpr int kCoverTiles = 7;
/// New turrets go within this many tiles of the attacked building.
constexpr int kReinforceSiteTiles = 5;
/// Cover wanted against an attack worth \a value credits: two turrets, one more for every 400
/// credits of attacking units, at most ten. A turret is 250 credits and 200 hit points; a Launcher
/// (450) out-ranges it by one tile, so cover is bought in numbers rather than one per attacker.
inline int reinforceGoal(int64_t value) {
    if (value <= 0) return 0;
    return int(std::clamp<int64_t>(2 + value / 400, 2, 10));
}
/// Site score for a reinforcement: in range of the attack first (it fires on arrival), then at
/// the standoff one tile inside the range (not in the attackers' midst), then nearest the
/// attacked building, then farther from other turrets.
inline int reinforceSiteScore(int toAttack, int toAnchor, int nearestOwnTurret, int range) {
    const int standoff = std::max(1, range - 2);
    int score = toAttack <= range - 1 ? 1000 : 0;
    score -= std::abs(toAttack - standoff) * 20;
    score -= toAnchor * 10;
    score += std::min(nearestOwnTurret, 4) * 5;
    return score;
}

// --- (b) AdvancingTurrets ----------------------------------------------------------------------

/// Turrets within this many tiles of the base's enemy-facing edge make the belt.
constexpr int kBeltTiles = 6;
/// The belt wanted at the edge.
constexpr int kBeltGoal = 6;
/// The army counts as ahead of the edge when its lead stands this many tiles nearer the enemy.
constexpr int kArmyLeadTiles = 4;
/// A belt turret stays at least this many tiles behind the army's lead.
constexpr int kBehindArmyTiles = 3;
/// The army's lead is the k-th own combat unit nearest the enemy (one scout is not a front).
constexpr int kArmyLeadRank = 5;
/// The belt never stands closer to an enemy building than this (the creep switch goes closer).
constexpr int kBeltEnemyStandoff = 10;
inline bool armyAhead(int armyLead, int edge) { return armyLead + kArmyLeadTiles <= edge; }
inline bool behindArmy(int site, int armyLead) { return site >= armyLead + kBehindArmyTiles; }
/// Site score for the belt: tiles gained toward the enemy first, then spacing.
inline int advanceSiteScore(int site, int edge, int nearestOwnTurret) {
    return (edge - site) * 100 + std::min(nearestOwnTurret, 4) * 5;
}

// --- (c) TurretCreep ---------------------------------------------------------------------------

/// Own buildings this close to an enemy building anchor the creep search.
constexpr int kCreepAnchorTiles = 12;
/// A creep site may be within reach of at most this many enemy turrets (rocket or gun).
constexpr int kCreepEnemyTurretTolerance = 1;
/// At most this many own Rocket Turrets stand in range of enemy buildings at once.
constexpr int kCreepCap = 12;
/// Anchors examined per search, nearest the enemy first.
constexpr int kCreepAnchors = 16;
/// Site score for creep: enemy building tiles in range first, enemy turrets reaching it second,
/// spacing last.
inline int creepSiteScore(int64_t enemyTilesInRange, int enemyTurretsInReach, int nearestOwnTurret) {
    return int(std::min<int64_t>(enemyTilesInRange, 200)) * 10 - enemyTurretsInReach * 300
        + std::min(nearestOwnTurret, 4) * 5;
}

// --- (d) RearTurretRetire ----------------------------------------------------------------------

/// The retirement review runs once per two game minutes.
constexpr uint32_t kRetirePeriodCycles = 7500;
/// A turret this far from every enemy building is in the rear...
constexpr int kRearEnemyTiles = 28;
/// ...when no visible enemy unit is within this many tiles of it either.
constexpr int kRearQuietTiles = 16;
/// Turrets within this many tiles of an own Airport or Palace are its air cover and stay.
constexpr int kKeepNearTiles = 5;
/// At most this many turrets are retired a review: 2 a minute, 120 an hour.
constexpr int kRetirePerReview = 4;
/// A city bombed within this many cycles keeps every turret (its anti-air).
constexpr uint32_t kBombQuietCycles = 3 * 3750;
/// The house always keeps at least its own coverage need, and never fewer than this.
constexpr int kKeepFloor = 12;
inline bool rearTurret(int enemyDistance, int64_t contactsNear) {
    return enemyDistance >= kRearEnemyTiles && contactsNear == 0;
}
inline int retireAllowance(int standing, int keep) {
    return std::clamp(standing - std::max(keep, kKeepFloor), 0, kRetirePerReview);
}

// --- Upkeep ------------------------------------------------------------------------------------

/// Police upkeep of one Rocket Turret in thousandths of a credit per game minute: 15 credits a
/// city year at \a fundingPercent of the police budget, \a cyclesPerYear cycles a year.
inline int64_t upkeepMilliPerMinute(int fundingPercent, uint32_t cyclesPerYear, uint32_t cyclesPerMinute = 3750) {
    if (cyclesPerYear == 0) return 0;
    return int64_t(15000) * std::max(0, fundingPercent) / 100 * cyclesPerMinute / cyclesPerYear;
}

} // namespace TurretFrontPolicy

#endif
