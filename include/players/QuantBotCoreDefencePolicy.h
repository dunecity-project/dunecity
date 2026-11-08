#ifndef QUANTBOT_CORE_DEFENCE_POLICY_H
#define QUANTBOT_CORE_DEFENCE_POLICY_H

#include <algorithm>
#include <cstdint>
#include <limits>

// Pure arithmetic of the default-OFF Custom Brutal Vanilla FFA experiments
// VanillaCoreDefence and VanillaArmyCounters (docs/ai-play-styles.md, "Vanilla FFA
// core defence and army counters"). QuantBot reads the live simulation state; nothing
// here is saved, and every constant is a fixed preregistered choice, not a search.
namespace QuantBotCoreDefencePolicy {

/// The only two intensities of VanillaCoreDefenceThreatBps and VanillaArmyCounterBlendBps.
/// Every other configured value is the 5000 level.
constexpr int level(int bps) { return bps == 10000 ? 10000 : 5000; }

/// Shared scope of both experiments, from the original occupied controller rows: ours
/// alone on its team (no allies), at least three hostile controllers, on more than one
/// hostile team (FFA4/FFA6). Never runtime houses, survivors or another policy switch.
constexpr bool tuningScope(bool autonomousBrutalCustom, bool city, bool vanilla, bool spice,
                           int friendlyControllers, int hostileControllers, bool multipleHostileTeams) {
    return autonomousBrutalCustom && !city && vanilla && spice
        && friendlyControllers == 1 && hostileControllers >= 3 && multipleHostileTeams;
}

/// A hostile armed mobile unit threatens a core within its own weapon range plus 24 tiles:
/// between the native emergency band's 12-tile floor and 40-tile cap, so a column is
/// answered while it approaches rather than once the base is already under fire.
constexpr int kApproachTiles = 24;
constexpr bool inThreatBand(int distance, int weaponRange) {
    return distance <= kApproachTiles + std::max(0, weaponRange);
}

/// Response value: the strongest hostile team's live threat (purchase price x health
/// share) at the 50% or 100% level. Zero threat is zero response.
constexpr int response(int threat, int bps) {
    return int(std::min<int64_t>(std::numeric_limits<int>::max(),
        int64_t(std::max(0, threat)) * level(bps) / 10000));
}

/// Emplacements: one per 1,000 response credits (four Rocket Turret or eight Gun Turret
/// prices in vanilla), none without a threat, at most three ground and two anti-air
/// covers per core and sixteen turrets per house.
constexpr int kTurretResponseCredits = 1000;
constexpr int kGroundTurretsPerCore = 3;
constexpr int kAirTurretsPerCore = 2;
constexpr int kTurretTotalCap = 16;
constexpr int turretGoal(int threat, int bps, bool air) {
    const int64_t value = response(threat, bps);
    if (value <= 0) return 0;
    return int(std::min<int64_t>(air ? kAirTurretsPerCore : kGroundTurretsPerCore,
        (value + kTurretResponseCredits - 1) / kTurretResponseCredits));
}

/// Standing guard: idle home ground fighters worth the response value hold Area Guard
/// posts within four tiles of the core. At most a third of our live ground army's value
/// and of its units, never more than eight, and four new posts or moves per unit pass.
constexpr int kGuardRadius = 4;
constexpr int kMaxGuards = 8;
constexpr int kOrdersPerPass = 4;
constexpr int guardGoal(int threat, int bps) { return response(threat, bps); }
constexpr int guardValueLimit(int ownValue) { return std::max(0, ownValue) / 3; }
constexpr int guardCountLimit(int ownUnits) { return std::min(kMaxGuards, std::max(0, ownUnits) / 3); }

} // namespace QuantBotCoreDefencePolicy

#endif
