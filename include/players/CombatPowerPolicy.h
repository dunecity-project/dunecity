#ifndef COMBAT_POWER_POLICY_H
#define COMBAT_POWER_POLICY_H

/*
    Shared integer combat-power estimate for QuantBot's posture, squad and
    dispatch decisions.

    This is deliberately a small, honest proxy and not a balance model:

    * Power is the unit's purchase price weighted by remaining hit points, in
      milli units, with a floor so a nearly dead unit is not worth exactly
      nothing while it is still shooting. Fixed-point free, so every peer and
      every reload computes the same number.
    * Ground and air power are kept apart, and anti-air capability is reported
      separately, because "can this force even shoot that force" is a real
      engine property (ObjectBase::canAttack) rather than a guess about damage
      tables. Nothing here invents per-unit combat weights from match scores.
    * Turret cover is counted only where a turret actually covers the point
      being judged, and only as defensive power. It never inflates the
      offensive strength used to decide whether to attack.
    * Inactive units - cargo inside a carryall, an occupant of a repair bay -
      contribute nothing. The caller filters them; these helpers only do
      arithmetic.
*/

#include <algorithm>
#include <cstdint>

#include <data.h>

namespace CombatPowerPolicy {

/// Hit-point floor, in basis points, so a surviving damaged unit still counts.
constexpr int kHealthFloorBps = 2500;
/// Power is carried in milli-price units to keep the HP weighting integral.
constexpr int64_t kPowerScale = 1000;

/// Remaining-strength weight in basis points, clamped to [floor, 10000].
inline int healthBps(int64_t health, int64_t maxHealth) {
    if (maxHealth <= 0) return 10000;
    const int64_t bps = std::clamp<int64_t>(health * 10000 / maxHealth, 0, 10000);
    return int(std::max<int64_t>(kHealthFloorBps, bps));
}

/// Tactical power floor, for a summoned or scripted unit whose purchase price is
/// zero. It still occupies the field, so the tactical comparison floors it the
/// way the established wave accounting does. This floor is deliberately NOT
/// applied to credit value below.
constexpr int kTacticalPriceFloor = 100;

/// One unit's tactical power, used only for strength comparisons.
inline int64_t unitPower(int price, int64_t health, int64_t maxHealth,
                         int priceFloor = kTacticalPriceFloor) {
    const int64_t effective = price > 0 ? price : std::max(0, priceFloor);
    return effective * kPowerScale * healthBps(health, maxHealth) / 10000;
}

/// Credit value, which is what the configured militaryValueLimit is expressed
/// in. The ACTUAL purchase price, with no floor: militaryUnitValue() sums raw
/// prices, so a sixty-credit Soldier has to count sixty here or the share of the
/// configured limit would be measured against a different currency. Only the
/// hit-point weighting is applied, which is what makes it "healthy" value.
inline int64_t unitValue(int price, int64_t health, int64_t maxHealth) {
    const int64_t effective = std::max(0, price);
    return effective * healthBps(health, maxHealth) / 10000;
}

/// Can a unit or structure of this type engage a flying unit at all?
///
/// This mirrors the engine's own canAttack overrides rather than guessing from a
/// weapon range: ObjectBase::canAttack rejects every flying target
/// (src/ObjectBase.cpp:444) and only these types override that rejection -
/// RocketTurret (src/structures/RocketTurret.cpp:72), Launcher
/// (src/units/Launcher.cpp:104), EliteLauncher (src/units/EliteLauncher.cpp:109),
/// Trooper (src/units/Trooper.cpp:55) and Deviator (src/units/Deviator.cpp:121).
/// Ornithopter explicitly excludes flying targets (src/units/Ornithopter.cpp:185),
/// and Soldier, the tanks and the Sonic line all keep the base rejection.
inline bool airCapableItem(int item) {
    return item == Structure_RocketTurret || item == Unit_Launcher
        || item == Unit_EliteLauncher || item == Unit_Deviator
        || item == Unit_Trooper;
}

/// Can a unit of this type attack ground targets? Every combat type can, which
/// is exactly why hostile aircraft cannot be discounted just because we have no
/// anti-air: an Ornithopter still destroys the ground army it strafes.
inline bool groundCapableItem(int item) {
    (void)item;
    return true;
}

/// Static defence contributes to holding a place, never to attacking one.
inline int64_t turretPower(int price, int64_t health, int64_t maxHealth) {
    return unitPower(price, health, maxHealth, 1);
}

struct Force {
    int64_t ground = 0;   ///< Ground combat power.
    int64_t air = 0;      ///< Flying combat power.
    int64_t antiAir = 0;  ///< Subset able to engage flying units.
    int64_t defence = 0;  ///< Static emplacements covering the judged point.
    int units = 0;

    void addGround(int64_t power, bool canHitAir) {
        ground += power; if (canHitAir) antiAir += power; ++units;
    }
    void addAir(int64_t power, bool canHitAir) {
        air += power; if (canHitAir) antiAir += power; ++units;
    }
    void addDefence(int64_t power, bool canHitAir) {
        defence += power; if (canHitAir) antiAir += power;
    }
    /// Everything that can contribute to holding ground here.
    int64_t holding() const { return ground + defence; }
    /// What can actually be sent forward.
    int64_t offensive() const { return ground; }
    int64_t total() const { return ground + air + defence; }
};

/// Hostile power for the GROUND dispatch veto: ground against ground, nothing
/// else. The outnumbered gate asks whether a ground offensive can win the
/// ground fight it is about to start, so an aircraft wing - which a ground wave
/// neither fights nor escapes by staying home - is deliberately out of scope
/// here and is assessed by the recovery triggers below instead.
inline int64_t hostileGroundOnly(const Force& hostile) { return hostile.ground; }

/// Hostile power that is actually hurting our ground force, for the recovery and
/// local-withdrawal triggers. Enemy aircraft are counted whether or not we can
/// shoot back: they inflict the losses the attrition window measures, and
/// pretending they are irrelevant because we lack anti-air is how an army sits
/// still while it is strafed.
inline int64_t hostileThreatToGround(const Force& hostile) {
    return hostile.ground + hostile.air;
}

/// Can this force answer hostile aircraft at all? Reported separately so a
/// decision can say "outmatched and cannot reply" rather than silently folding
/// the two together.
inline bool canAnswerAir(const Force& friendly) { return friendly.antiAir > 0; }

} // namespace CombatPowerPolicy

#endif // COMBAT_POWER_POLICY_H
