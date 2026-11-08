#ifndef QUANTBOT_ARMY_COUNTER_POLICY_H
#define QUANTBOT_ARMY_COUNTER_POLICY_H

#include <OrdinaryDamagePolicy.h>
#include <players/CombatPowerPolicy.h>
#include <players/UnitMixPolicy.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

/*
    VanillaArmyCounters (docs/ai-play-styles.md): re-weight the production mix toward the
    units that answer the live army of the strongest terrain-reachable hostile team.

    An exchange estimate from the configured unit data, not a battle simulator and not a
    table of counter ratios:

    * Weapons are the native unit constructors' projectiles and weapon counts; damage,
      reload, hit points and price are the ObjectData values the engine uses. Ordinary
      ground impacts take OrdinaryDamagePolicy's centre damage under the match's damage
      rules, an aircraft takes a rocket's whole payload (the air layer of Map::damage),
      only the engine's anti-air types (CombatPowerPolicy::airCapableItem) can hit a
      flying target, and a Sonic Tank cannot hurt another Sonic Tank.
    * A candidate's worth against one hostile type is the replacement value it destroys
      per credit and reload cycle, discounted by its survival against the WHOLE hostile
      team: the team's value-weighted rate of destroying that candidate. Armed air and
      ground armour are judged together, so an Ornithopter is safe only from a team
      without anti-air. Per hostile type the worths become shares exactly as the learned
      mix turns scores into weights (UnitMixPolicy::sharpenScores).
    * Not forecast: range, speed, accuracy, splash and terrain. The Sonic wave is valued
      at its configured payload per volley. Deviator gas converts temporarily and removes
      no hit points, so it neither earns Deviators a counter share nor taxes a candidate.
    * Only slots a live factory can build now are re-weighted, and only among
      themselves: every other slot keeps its native share. A hostile type nothing
      buildable can damage keeps the native proportions for its value; with nothing
      answerable the native mix is returned unchanged.
*/
namespace QuantBotArmyCounterPolicy {

using Mix = UnitMixPolicy::Mix;
using Weights = UnitMixPolicy::Weights;
using DamageMode = OrdinaryDamagePolicy::Mode;

/// One unit type's configured combat data (the ObjectData row of its production house).
struct UnitType {
    int item = ItemID_Invalid;
    int price = 0;
    int hitpoints = 0;
    int weaponDamage = 0;
    int reloadCycles = 0;
    bool flying = false;
};

inline bool sameType(const UnitType& a, const UnitType& b) {
    return a.item == b.item && a.price == b.price && a.hitpoints == b.hitpoints
        && a.weaponDamage == b.weaponDamage && a.reloadCycles == b.reloadCycles && a.flying == b.flying;
}

struct Weapon {
    int projectile = -1;
    int shots = 0;
};

/// The native constructors' weapon (bulletType and numWeapons in src/units). Types outside
/// the armed vanilla roster - workers, transports, MCVs, saboteurs, worms - have none.
inline Weapon weapon(int item) {
    switch (item) {
        case Unit_Tank: return {Bullet_ShellMedium, 1};
        case Unit_SiegeTank: case Unit_Devastator: return {Bullet_ShellLarge, 2};
        case Unit_Launcher: return {Bullet_Rocket, 2};
        case Unit_SonicTank: return {Bullet_Sonic, 1};
        case Unit_Deviator: return {Bullet_DRocket, 1};
        case Unit_Ornithopter: case Unit_Trooper: return {Bullet_SmallRocket, 1};
        case Unit_Trike: case Unit_RaiderTrike: case Unit_Quad: return {Bullet_ShellSmall, 2};
        case Unit_Soldier: return {Bullet_ShellSmall, 1};
        default: return {};
    }
}

/// Army for this policy: an armed type of the vanilla roster.
inline bool armyType(int item) { return weapon(item).shots > 0; }

/// Does this type's weapon remove hit points? Deviator gas does not.
inline bool destroys(int item) {
    const Weapon gun = weapon(item);
    return gun.shots > 0 && gun.projectile != Bullet_DRocket;
}

/// Hit points one volley of \a attacker removes from \a target; 0 if it cannot hurt it.
inline int64_t volleyDamage(const UnitType& attacker, const UnitType& target, DamageMode mode) {
    const Weapon gun = weapon(attacker.item);
    if (!destroys(attacker.item) || attacker.weaponDamage <= 0) return 0;
    if (target.flying)
        return CombatPowerPolicy::airCapableItem(attacker.item) ? int64_t(attacker.weaponDamage) * gun.shots : 0;
    if (attacker.item == Unit_SonicTank && target.item == Unit_SonicTank) return 0;
    int payload = attacker.weaponDamage;
    // Beyond two tiles a Trooper fires its rocket at three quarters (UnitBase::attack).
    if (attacker.item == Unit_Trooper) payload -= payload / 4;
    const auto projectile = Uint32(gun.projectile);
    const int impact = OrdinaryDamagePolicy::ordinaryBlast(projectile)
        ? OrdinaryDamagePolicy::centreDamage(mode, projectile, Uint32(attacker.item), payload) : payload;
    return int64_t(impact) * gun.shots;
}

/// About a year of game cycles: the longest kill counted, keeping every product in 64 bits.
constexpr int64_t kMaxCycles = int64_t(1) << 31;

/// Reload cycles \a attacker spends destroying a fresh \a target; 0 if it never can.
inline int64_t defeatCycles(const UnitType& attacker, const UnitType& target, DamageMode mode) {
    const int64_t damage = volleyDamage(attacker, target, mode);
    if (damage <= 0 || attacker.reloadCycles <= 0 || target.hitpoints <= 0) return 0;
    const int64_t volleys = (int64_t(target.hitpoints) + damage - 1) / damage;
    return std::min(kMaxCycles, volleys * attacker.reloadCycles);
}

/// a + b for a, b >= 0, saturating at the largest value.
inline int64_t addSaturated(int64_t a, int64_t b) {
    constexpr int64_t largest = std::numeric_limits<int64_t>::max();
    return a > largest - b ? largest : a + b;
}

/// value * bps / 10000 for value >= 0 and bps in [0, 10000], without overflow.
inline int64_t scaled(int64_t value, int bps) {
    return value / 10000 * bps + value % 10000 * bps / 10000;
}

/// part / whole in basis points for 0 <= part <= whole.
inline int shareBps(int64_t part, int64_t whole) {
    if (part <= 0 || whole <= 0) return 0;
    constexpr int64_t limit = std::numeric_limits<int64_t>::max() / 10000;
    while (whole > limit) { part /= 2; whole /= 2; }
    return int(std::min<int64_t>(10000, part * 10000 / whole));
}

/// UnitMixPolicy::normalize for weights of any size: negatives count as zero, and very large
/// weights are halved together, which keeps their ratio, until the products fit.
inline Mix normalize(Weights weights) {
    constexpr int64_t limit = std::numeric_limits<int64_t>::max() / (10000 * int64_t(UnitMixPolicy::kSlots));
    for (auto& weight : weights) weight = std::max<int64_t>(0, weight);
    while (*std::max_element(weights.begin(), weights.end()) > limit)
        for (auto& weight : weights) weight /= 2;
    return UnitMixPolicy::normalize(weights);
}

struct Team {
    int id = -1;
    int64_t value = 0;
};

/// Live health-weighted army value (CombatPowerPolicy::unitValue) per hostile team.
template<size_t N>
struct Teams {
    std::array<Team, N> entries{};
    size_t size = 0;

    /// Counts one reachable hostile unit. A non-army type or an empty value changes nothing.
    bool add(int team, int item, int64_t value) {
        if (!armyType(item) || value <= 0) return false;
        for (size_t i = 0; i < size; ++i)
            if (entries[i].id == team) {
                entries[i].value = addSaturated(entries[i].value, value);
                return true;
            }
        if (size == N) return false;
        entries[size++] = {team, value};
        return true;
    }

    /// The team with the most value; equal value goes to the lower team id. -1 if none.
    int strongest() const {
        const Team* best = nullptr;
        for (size_t i = 0; i < size; ++i)
            if (!best || entries[i].value > best->value
                || (entries[i].value == best->value && entries[i].id < best->id)) best = &entries[i];
        return best ? best->id : -1;
    }
};

struct Entry {
    UnitType type{};
    int64_t value = 0;
};

/// One team's live army by unit type.
template<size_t N>
struct Composition {
    std::array<Entry, N> entries{};
    size_t size = 0;
    int64_t total = 0;
    int64_t overflow = 0; ///< Value of types beyond the table; it keeps the native mix.

    bool add(const UnitType& type, int64_t value) {
        if (!armyType(type.item) || value <= 0) return false;
        total = addSaturated(total, value);
        for (size_t i = 0; i < size; ++i)
            if (sameType(entries[i].type, type)) {
                entries[i].value = addSaturated(entries[i].value, value);
                return true;
            }
        if (size == N) overflow = addSaturated(overflow, value);
        else entries[size++] = {type, value};
        return true;
    }
};

/// A mix slot's unit, and whether a live factory of this house can build it now.
struct Candidate {
    UnitType type{};
    bool available = false;
};
using Candidates = std::array<Candidate, UnitMixPolicy::kSlots>;

/// The re-weighted slots: buildable now, priced, and armed with hit-point damage.
inline bool judged(const Candidate& candidate) {
    return candidate.available && destroys(candidate.type.item)
        && candidate.type.price > 0 && candidate.type.hitpoints > 0;
}

struct Target {
    Weights value{};        ///< Hostile value assigned to each slot as its answer.
    int64_t unanswered = 0; ///< Hostile value no judged slot can damage.
};

/// Fixed-point scales: kill rates per cycle, and worth per credit and cycle.
constexpr int64_t kRateScale = int64_t(1) << 30;
constexpr int64_t kWorthScale = int64_t(1) << 50;

/// Which slots answer \a force, as hostile value per slot.
template<size_t N>
inline Target counterTarget(const Composition<N>& force, const Candidates& candidates, DamageMode mode) {
    Target target;
    target.unanswered = force.overflow;
    if (force.total <= 0) return target;
    // The whole team's rate of destroying each candidate, weighted by the live value share
    // of every type in it. A weighted rate never exceeds the fastest one, kRateScale; the
    // bound also holds when saturated values make the shares overlap.
    std::array<int64_t, UnitMixPolicy::kSlots> threat{};
    for (size_t c = 0; c < candidates.size(); ++c) {
        if (!judged(candidates[c])) continue;
        for (size_t e = 0; e < force.size; ++e) {
            const int64_t cycles = defeatCycles(force.entries[e].type, candidates[c].type, mode);
            if (cycles > 0)
                threat[c] += int64_t(shareBps(force.entries[e].value, force.total)) * (kRateScale / cycles) / 10000;
        }
        threat[c] = std::min(threat[c], kRateScale);
    }
    for (size_t e = 0; e < force.size; ++e) {
        const Entry& hostile = force.entries[e];
        Weights worth{};
        for (size_t c = 0; c < candidates.size(); ++c) {
            if (!judged(candidates[c])) continue;
            const int64_t cycles = defeatCycles(candidates[c].type, hostile.type, mode);
            if (cycles == 0) continue;
            // Survival: the candidate's kill time against the team's expected reply time.
            const int survival = threat[c] > 0
                ? int(kRateScale * 10000 / (kRateScale + cycles * threat[c])) : 10000;
            worth[c] = scaled(kWorthScale / (int64_t(candidates[c].type.price) * cycles), survival);
        }
        if (*std::max_element(worth.begin(), worth.end()) <= 0) {
            target.unanswered = addSaturated(target.unanswered, hostile.value);
            continue;
        }
        const Mix plain = normalize(worth);
        Weights scores{};
        for (size_t c = 0; c < plain.size(); ++c) scores[c] = plain[c];
        const Mix shares = UnitMixPolicy::normalize(UnitMixPolicy::sharpenScores(scores));
        for (size_t c = 0; c < shares.size(); ++c)
            target.value[c] = addSaturated(target.value[c], scaled(hostile.value, shares[c]));
    }
    return target;
}

/// \a native with its judged slots moved \a blendBps of the way to \a target; every other
/// slot keeps its native share, and the judged slots keep their combined native share.
inline Mix blend(const Mix& native, const Candidates& candidates, const Target& target, int blendBps) {
    const int strength = std::clamp(blendBps, 0, 10000);
    if (strength == 0) return native;
    int64_t group = 0;
    bool answered = false;
    for (size_t i = 0; i < native.size(); ++i) {
        if (!judged(candidates[i])) continue;
        group += std::max(0, native[i]);
        answered = answered || target.value[i] > 0;
    }
    if (!answered || group <= 0) return native;
    Weights answer{};
    for (size_t i = 0; i < native.size(); ++i)
        if (judged(candidates[i]))
            answer[i] = addSaturated(std::max<int64_t>(0, target.value[i]),
                scaled(std::max<int64_t>(0, target.unanswered), shareBps(std::max(0, native[i]), group)));
    const Mix counter = normalize(answer);
    Weights combined{};
    for (size_t i = 0; i < native.size(); ++i) {
        const int64_t own = std::max(0, native[i]);
        combined[i] = judged(candidates[i])
            ? own * (10000 - strength) * 10000 + int64_t(counter[i]) * group * strength
            : own * 10000 * 10000;
    }
    return normalize(combined);
}

} // namespace QuantBotArmyCounterPolicy

#endif // QUANTBOT_ARMY_COUNTER_POLICY_H
