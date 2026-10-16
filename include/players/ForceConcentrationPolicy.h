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

} // namespace ForceConcentrationPolicy

#endif // FORCE_CONCENTRATION_POLICY_H
