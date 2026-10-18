#ifndef FORCE_CONCENTRATION_POLICY_H
#define FORCE_CONCENTRATION_POLICY_H

/*
    How a QuantBot army concentrates force on attack and defence (docs/formations.md,
    "Concentrating force"): focus fire, converging on a fight, pooling defenders by strength,
    recalling the raiding army to a real base attack, and staging a wave near the enemy.

    Everything here is pure integer arithmetic on values the caller observes from the
    simulation, so two peers deciding on the same cycle reach the same answer. No container
    iteration order, no clock, no random source.
*/

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace ForceConcentrationPolicy {

// ---- Focus fire --------------------------------------------------------------------------------

/// What a target is, best first: an enemy fighter shoots back, a turret shoots back but cannot
/// move, a worker or transport does not shoot, a plain building neither, a wall last.
enum TargetClass : int { Wall = -1, Building = 0, Unarmed = 1, ArmedBuilding = 2, ArmedUnit = 3 };

struct TargetKey {
    int cls = Wall;
    int64_t health = 0;     ///< hit points left
    uint32_t id = 0;
};

/// Is \a a the better target? The highest class, then the fewest hit points left (the one
/// that dies first, which removes a gun soonest - Lanchester), then the lowest object id.
inline bool betterTarget(const TargetKey& a, const TargetKey& b) {
    if (a.cls != b.cls) return a.cls > b.cls;
    if (a.health != b.health) return a.health < b.health;
    return a.id < b.id;
}

/// A unit only gives up a target it can shoot for one with this share of its hit points or
/// fewer (or of a better class), so units do not swing their turrets back and forth.
constexpr int kSwitchHealthBps = 7500;

/// Should a unit firing at \a current switch to \a best (both in its weapon range unless
/// \a currentInRange is false)?
inline bool shouldSwitch(bool currentInRange, const TargetKey& current, const TargetKey& best) {
    if (best.id == current.id) return false;
    if (!currentInRange) return true;
    if (best.cls != current.cls) return best.cls > current.cls;
    return best.health * 10000 <= current.health * kSwitchHealthBps;
}

/// Attack-value focus fire without overkill (Churchill, Saffidine & Buro 2012, "NOK-AV"): the
/// target in reach that removes the most enemy damage per hit point dealt.
struct ValueKey {
    int cls = Wall;
    int64_t dpf = 0;        ///< the target's damage per frame x1000 (0 for the unarmed)
    int64_t health = 0;     ///< hit points left (at least 1)
    uint32_t id = 0;
};

/// Is \a a the better target? The highest class, then the highest damage per hit point
/// (compared exactly by cross-multiplication), then the fewest hit points, then the lowest id.
inline bool betterValue(const ValueKey& a, const ValueKey& b) {
    if (a.cls != b.cls) return a.cls > b.cls;
    const int64_t left = a.dpf * std::max<int64_t>(1, b.health), right = b.dpf * std::max<int64_t>(1, a.health);
    if (left != right) return left > right;
    if (a.health != b.health) return a.health < b.health;
    return a.id < b.id;
}

/// Has a target already been assigned lethal damage (\a assigned, one volley of every unit
/// shooting at it) so that another shooter would be overkill?
inline bool overkilled(int64_t assigned, int64_t health) { return assigned >= std::max<int64_t>(1, health); }

/// Should a unit firing at \a current switch to \a best? Only if it cannot shoot the current
/// one, the others already kill it, or the best is a better class or at least a third better.
inline bool shouldSwitchValue(bool currentInRange, bool currentOverkilled, const ValueKey& current, const ValueKey& best) {
    if (best.id == current.id) return false;
    if (!currentInRange || currentOverkilled) return true;
    if (best.cls != current.cls) return best.cls > current.cls;
    // best.dpf / best.health >= 4/3 * current.dpf / current.health
    return best.dpf * std::max<int64_t>(1, current.health) * 3 >= current.dpf * std::max<int64_t>(1, best.health) * 4
        && best.dpf > 0;
}

// ---- Converging on a fight ---------------------------------------------------------------------

/// A wave member that is not fighting joins a fight of its own army this close (tiles) ...
constexpr int kConvergeTiles = 24;
/// ... unless the target it is already walking to is no further than the fight plus this.
constexpr int kConvergeSlackTiles = 4;
/// A member already hunting towards a point this close to the fight keeps its walk.
constexpr int kConvergeSameGoalTiles = 4;

/// Does a member \a fightDistance tiles from a fight leave its own walk (\a targetDistance
/// tiles to its current target, negative for none) for it?
inline bool converge(int fightDistance, int targetDistance) {
    if (fightDistance < 0 || fightDistance > kConvergeTiles) return false;
    return targetDistance < 0 || targetDistance > fightDistance + kConvergeSlackTiles;
}

// ---- Pooling defenders -------------------------------------------------------------------------

/// The enemy counted against an attacked building: visible enemy fighters this close to it
/// (the scramble's own count uses 8), so attackers still walking in are counted too.
constexpr int kPoolThreatTiles = 12;
/// The defence sends this share of that threat (the scramble's own answer is 125%):
/// overwhelming the attackers locally is what keeps the defenders' losses down (Lanchester).
constexpr int kPoolResponseBps = 20000;

inline int64_t poolRequired(int64_t threat) { return std::max<int64_t>(0, threat) * kPoolResponseBps / 10000; }

struct Responder {
    uint32_t id = 0;
    int64_t value = 0;
    int distance = 0;       ///< tiles to the attacked building
};

/// The defenders to send: nearest first (ties by id) within \a band tiles until, with the
/// \a committed value already there, they reach \a required. With \a recall, when the
/// troops within the band cannot reach it, the nearest troops beyond the band (the raiding
/// army) are added until they do - a real attack on the base brings the army home, a raid
/// the local troops can answer does not.
inline std::vector<uint32_t> pool(int64_t required, int64_t committed, std::vector<Responder> candidates,
                                  int band, bool recall) {
    std::stable_sort(candidates.begin(), candidates.end(), [](const Responder& a, const Responder& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    std::vector<uint32_t> selected;
    int64_t value = std::max<int64_t>(0, committed);
    for (const auto& candidate : candidates) {
        if (value >= required) break;
        if (candidate.value <= 0) continue;
        if (candidate.distance > band && !recall) break;
        selected.push_back(candidate.id);
        value += candidate.value;
    }
    return selected;
}

// ---- Mustering defenders -----------------------------------------------------------------------

/// Defenders further than this from the attacked building muster first ...
constexpr int kMusterFromTiles = 14;
/// ... at a point this far from the building on their side of it, outside enemy reach ...
constexpr int kMusterStandoffTiles = 8;
/// ... and engage together when this share stands there, or after this long at most.
constexpr int kMusterQuorumBps = 7500;
constexpr int kMusterMaxMs = 15000;

// ---- Staging an attack near the enemy ----------------------------------------------------------

/// A staged wave waits this far from its objective (outside turret reach) for the rest ...
constexpr int kStageStandoffTiles = 12;
/// ... at most this long for the slowest member ...
constexpr int kStageMaxMs = 120000;
/// ... and, with the local-superiority rule, attacks only when its strength is this share of
/// the enemy's around the objective, holding at most kStageHoldMs longer for reinforcements.
constexpr int kStageSuperiorityBps = 15000;
constexpr int kStageHoldMs = 60000;
/// The enemy counted around the objective: visible fighters and turrets this close to it.
constexpr int kStageEnemyTiles = 12;

/// Is the party strong enough to attack (\a own and \a enemy in CombatPowerPolicy units)?
inline bool superior(int64_t own, int64_t enemy) {
    return enemy <= 0 || own * 10000 >= enemy * kStageSuperiorityBps;
}

// ---- Wave size ---------------------------------------------------------------------------------

/// Readiness value for a wave, scaled by \a percent (100 = unchanged).
inline int64_t scaledReadiness(int64_t required, int percent) {
    return std::max<int64_t>(0, required) * std::max(0, percent) / 100;
}

/// A house is outnumbered (kForceStrengthReadiness) when the observed enemy army is worth this
/// share of its own or more; its waves then wait for full readiness.
constexpr int kOutnumberedReadinessBps = 12500;
inline bool outnumbered(int64_t own, int64_t hostile) {
    return hostile > 0 && hostile * 10000 >= std::max<int64_t>(0, own) * kOutnumberedReadinessBps;
}

/// Opening readiness (kForceOpeningReadiness): the readiness a house's waves use. Before its first
/// ground wave (\a launched false), a house whose starting army (\a initialArmy) could not launch
/// a wave at \a percent of \a required - a Construction Yard start rather than a Fortress with an
/// army - waits for full readiness (100); otherwise, and from the first wave on, \a percent.
inline int openingReadiness(int percent, bool launched, int64_t initialArmy, int64_t required) {
    if (percent >= 100 || launched) return percent;
    return initialArmy < scaledReadiness(required, percent) ? 100 : percent;
}

// ---- Defence while outnumbered (round 5) -------------------------------------------------------

/// The remembered enemy army forgets a fighter not seen for this long.
constexpr int kSightingMemoryMs = 300000;

/// Does a wave worth \a wave launch against an estimated enemy army of \a enemy, needing
/// \a ratioBps of it (kForceStrengthWaves)? Nothing known is no reason to wait.
inline bool strongEnough(int64_t wave, int64_t enemy, int ratioBps) {
    return enemy <= 0 || std::max<int64_t>(0, wave) * 10000 >= enemy * std::max(0, ratioBps);
}

/// The most of an available wave a house keeps home (kForceReserve) ...
constexpr int kReserveMaxBps = 5000;
/// ... the share by which the enemy estimate exceeds our army, capped at kReserveMaxBps.
inline int reserveShareBps(int64_t own, int64_t enemy) {
    if (enemy <= 0 || enemy <= own) return 0;
    return int(std::min<int64_t>(kReserveMaxBps, (enemy - std::max<int64_t>(0, own)) * 10000 / enemy));
}

/// A sized defence (kForceSizedDefence) sends this share of the threat within kPoolThreatTiles
/// from up to this far ...
constexpr int kSizedResponseBps = 15000;
constexpr int kSizedBandTiles = 40;
inline int64_t sizedRequired(int64_t threat) { return std::max<int64_t>(0, threat) * kSizedResponseBps / 10000; }

struct SizedResponder {
    uint32_t id = 0;
    int64_t value = 0;
    int distance = 0;       ///< tiles to the defended object
    bool bundled = false;   ///< standing in the bundle (the reserve kept for this)
};

/// ... the bundle first, then the nearest (ties by id), until with \a committed they reach
/// \a required; nobody beyond \a band unless bundled.
inline std::vector<uint32_t> sizedPool(int64_t required, int64_t committed, std::vector<SizedResponder> candidates,
                                       int band) {
    std::stable_sort(candidates.begin(), candidates.end(), [](const SizedResponder& a, const SizedResponder& b) {
        if (a.bundled != b.bundled) return a.bundled;
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    std::vector<uint32_t> selected;
    int64_t value = std::max<int64_t>(0, committed);
    for (const auto& candidate : candidates) {
        if (value >= required) break;
        if (candidate.value <= 0 || (candidate.distance > band && !candidate.bundled)) continue;
        selected.push_back(candidate.id);
        value += candidate.value;
    }
    return selected;
}

/// A worker under attack is answered only when \a committed plus everything in reach
/// (\a reachable) at least matches the \a threat: a smaller answer is fed to the enemy piecemeal.
inline bool answerable(int64_t threat, int64_t committed, int64_t reachable) {
    return std::max<int64_t>(0, committed) + std::max<int64_t>(0, reachable) >= std::max<int64_t>(0, threat);
}

// ---- Defending at the base perimeter (round 5) ---------------------------------------------------

/// The base whose perimeter is held: own buildings this close to the base centre ...
constexpr int kPerimeterBaseTiles = 24;
/// ... the army holds this far beyond the outermost one on the enemy side (V) ...
constexpr int kPerimeterStandoffTiles = 3;
/// ... or, where that point still lies among its own buildings or roads (a city spreading beyond
/// kPerimeterBaseTiles), the first standable point at most kPerimeterMaxExtraTiles further out
/// with none within kPerimeterClearTiles (the standoff: a compact base's point is already clear) ...
constexpr int kPerimeterMaxExtraTiles = 24;
constexpr int kPerimeterClearTiles = kPerimeterStandoffTiles;
/// ... and threat turrets (U) stand in the band from kPerimeterInnerTiles inside that outermost
/// building to kPerimeterOuterTiles beyond it, at most kPerimeterHalfWidth tiles to either side of
/// the front line through the base centre.
constexpr int kPerimeterInnerTiles = 4;
constexpr int kPerimeterOuterTiles = 2;
constexpr int kPerimeterHalfWidth = 12;
/// One threat turret per this many credits by which the enemy estimate exceeds our army, at
/// most kPerimeterTurretsMax on the perimeter; credits for units are kept above the price.
constexpr int kPerimeterCreditsPerTurret = 1000;
constexpr int kPerimeterTurretsMax = 8;
constexpr int kPerimeterUnitReserve = 500;

/// Perimeter turrets wanted while the estimate (\a enemy) outnumbers \a own.
inline int perimeterTurretGoal(int64_t own, int64_t enemy) {
    if (!outnumbered(own, enemy)) return 0;
    return int(std::min<int64_t>(kPerimeterTurretsMax, (enemy - std::max<int64_t>(0, own)) / kPerimeterCreditsPerTurret));
}

/// Is an offset (\a along, \a across: tiles along and across the front from the base centre) on
/// the perimeter band of a base \a depth tiles deep?
inline bool onPerimeterBand(int along, int across, int depth) {
    return along >= depth - kPerimeterInnerTiles && along <= depth + kPerimeterOuterTiles
        && std::abs(across) <= kPerimeterHalfWidth;
}

// ---- Late game: finishing the game (docs/formations.md, "Free-for-all: finishing the game") ------

/// H weighs each known enemy turret as this many credits of army ...
constexpr int kKnownTurretValue = 250;
/// ... and its objective counts production and economy (yard, factories, refinery) this much nearer.
constexpr int kFinishPriorityTiles = 20;

/// What H knows of an enemy team: its remembered army, known turrets and known structures.
struct TeamStrength {
    int team = -1;
    int64_t army = 0;
    int turrets = 0;
    int structures = 0;
};

/// Is \a a the weaker team to finish (army + kKnownTurretValue per turret; ties: fewer known
/// structures, then the lower team id)?
inline bool weakerTeam(const TeamStrength& a, const TeamStrength& b) {
    const int64_t sa = a.army + int64_t(kKnownTurretValue) * a.turrets;
    const int64_t sb = b.army + int64_t(kKnownTurretValue) * b.turrets;
    if (sa != sb) return sa < sb;
    if (a.structures != b.structures) return a.structures < b.structures;
    return a.team < b.team;
}

/// K: an objective with at least kFortifiedTurrets known turrets within kFortifiedRadiusTiles is
/// fortified; a wave against it needs kFortifiedLimitPercent of the military limit (or the normal
/// readiness if more), unless the last wave left kFortifiedMaxHoldMs ago.
constexpr int kFortifiedRadiusTiles = 12;
constexpr int kFortifiedTurrets = 8;
constexpr int kFortifiedLimitPercent = 60;
constexpr int kFortifiedMaxHoldMs = 240000;

/// The value a ready wave needs against an objective with \a turrets known turrets nearby.
inline int64_t fortifiedReady(int64_t required, int64_t militaryLimit, int turrets) {
    if (turrets < kFortifiedTurrets) return required;
    return std::max(required, militaryLimit * kFortifiedLimitPercent / 100);
}

// ---- Bundling the idle reserve -----------------------------------------------------------------

/// The reserve gathers this share of the way from its home assembly point to the observed enemy
/// base (the pre-1.0.602 rally was 75% own base + 25% enemy base) ...
constexpr int kBundleForwardPercent = 25;
/// ... out of the house's own base: where that point lies among its own buildings or roads (a
/// city base), it moves on towards the enemy, at most to kBundleMaxForwardPercent of the way, to
/// the first standable point with none within the bundle's clearance - the reserve's radius
/// (bundleRadius), kept between kBundleClearMinTiles and kBundleClearMaxTiles ...
constexpr int kBundleMaxForwardPercent = 50;
constexpr int kBundleClearMinTiles = 4;
constexpr int kBundleClearMaxTiles = 8;
inline int bundleClearance(int radius) { return std::clamp(radius, kBundleClearMinTiles, kBundleClearMaxTiles); }
/// ... with at most this many move orders per unit pass ...
constexpr int kBundleOrdersPerPass = 12;
/// ... and, with the launch gate, a ready wave waits until this share of its value stands in
/// the bundle, at most kBundleGateMaxMs, re-checking every kBundleRecheckMs.
constexpr int kBundleGateBps = 7000;
constexpr int kBundleGateMaxMs = 60000;
constexpr int kBundleRecheckMs = 5000;

/// The smallest radius (at least 3 tiles) that holds \a members at two per squared tile.
inline int bundleRadius(int members) {
    int radius = 3;
    while (radius * radius * 2 < members && radius < 64) ++radius;
    return radius;
}

/// A point \a percent of the way from \a from towards \a to (integer, truncating towards from).
inline int along(int from, int to, int percent) { return from + (to - from) * percent / 100; }

/// Is enough of the wave in the bundle (\a bundled of \a total value) to launch?
inline bool bundled(int64_t bundledValue, int64_t total) {
    return total <= 0 || bundledValue * 10000 >= total * kBundleGateBps;
}

/// Which of two reserve units gets one of the pass's bundle orders first: the one farther from
/// the bundle (it has the longest walk), then the lower object id. A strict order on (distance,
/// id), so the chosen units never depend on the order of the unit list.
inline bool bundleOrderFirst(int distanceA, uint32_t idA, int distanceB, uint32_t idB) {
    return distanceA != distanceB ? distanceA > distanceB : idA < idB;
}

// ---- Splitting a wave: two angles ----------------------------------------------------------------

/// A split wave's groups wait this far from the objective (outside turret reach) ...
constexpr int kSplitStandoffTiles = 14;
/// ... only when the wave stands at least this far from it and has this many members ...
constexpr int kSplitMinTiles = 24;
constexpr int kSplitMinMembers = 8;
/// ... and attack together once this share of each group has arrived, after at most
/// kSplitMaxMs, or as soon as this share of one group is already fighting.
constexpr int kSplitQuorumBps = 7500;
constexpr int kSplitMaxMs = 120000;
constexpr int kSplitFightBps = 2500;
/// The flanking group's share of the wave value (kForceFlank; the halves of kForceSplit: 5000).
constexpr int kFlankShareBps = 3000;
constexpr int kSplitOrdersPerPass = 16;

/// Integer square root (floor).
inline int64_t isqrt(int64_t value) {
    if (value <= 0) return 0;
    int64_t root = 0, bit = int64_t(1) << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= root + bit) { value -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

struct Point { int x = 0, y = 0; };

/// The release point \a standoff tiles from \a objective in the direction of \a from (where the
/// wave comes from) rotated by \a degrees (0, +-45 or +-90; +d and -d are mirror images across
/// the approach line). \a from == \a objective gives \a objective.
inline Point releasePoint(Point objective, Point from, int standoff, int degrees) {
    const int64_t vx = from.x - objective.x, vy = from.y - objective.y;
    const int64_t length = isqrt(vx * vx + vy * vy);
    if (length == 0) return objective;
    // cos/sin x1000 of the supported angles.
    int64_t c = 1000, s = 0;
    if (degrees == 45) { c = 707; s = 707; }
    else if (degrees == -45) { c = 707; s = -707; }
    else if (degrees == 90) { c = 0; s = 1000; }
    else if (degrees == -90) { c = 0; s = -1000; }
    const int64_t rx = vx * c - vy * s, ry = vx * s + vy * c;    // x1000
    return {objective.x + int(rx * standoff / (length * 1000)), objective.y + int(ry * standoff / (length * 1000))};
}

struct SplitMember {
    uint32_t id = 0;
    int64_t value = 0;
    int64_t lean = 0;       ///< distance to group 0's point minus distance to group 1's
};

/// Which group (0 or 1) each member joins, in input order: the members leaning most towards
/// group 0's point (ties by id) fill group 0 until it holds \a shareBps of the value, the rest
/// go to group 1. Both groups get at least one member when there are two or more.
inline std::vector<int> splitGroups(const std::vector<SplitMember>& members, int shareBps) {
    std::vector<size_t> order(members.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return members[a].lean != members[b].lean ? members[a].lean < members[b].lean : members[a].id < members[b].id;
    });
    int64_t total = 0;
    for (const auto& member : members) total += std::max<int64_t>(0, member.value);
    std::vector<int> group(members.size(), 1);
    int64_t value = 0;
    size_t taken = 0;
    for (const size_t index : order) {
        if (taken > 0 && value * 10000 >= total * shareBps) break;
        if (taken + 1 >= members.size()) break;
        group[index] = 0;
        value += std::max<int64_t>(0, members[index].value);
        ++taken;
    }
    return group;
}

/// Have both groups arrived (\a arrived of \a alive each, kSplitQuorumBps)?
inline bool splitReady(const int arrived[2], const int alive[2]) {
    for (int g = 0; g < 2; ++g)
        if (alive[g] > 0 && int64_t(arrived[g]) * 10000 < int64_t(alive[g]) * kSplitQuorumBps) return false;
    return true;
}

/// Is a group (\a fighting of \a alive) in a real fight, so the other must come now?
inline bool splitFighting(int fighting, int alive) {
    return alive > 0 && fighting > 0 && int64_t(fighting) * 10000 >= int64_t(alive) * kSplitFightBps;
}

} // namespace ForceConcentrationPolicy

#endif // FORCE_CONCENTRATION_POLICY_H
