#ifndef EASIEST_POLICY_H
#define EASIEST_POLICY_H

#include <algorithm>
#include <cstdint>

// QuantBot Easiest (class qBotEasiest, Stefan 2026-11-22: "an 'easiest' difficulty which is
// easier than easy"). It is QuantBot Easy - same skills, same Easy settings - with the lower
// ceilings below. Easy and every other difficulty never read these rules
// (docs/campaign-ai-difficulty-matrix.md, "QuantBot Easiest").
namespace EasiestPolicy {

// Custom games (skirmish, custom, multiplayer).
constexpr int kCustomArmyPercent = 60;       ///< Army it builds up to, % of Easy's limit for the map size.
constexpr int kCustomHarvesterPercent = 50;  ///< Harvesters it keeps, % of Easy's ceiling (maps above 64x64).
constexpr int kCustomFirstAttackMin = 10;    ///< No attack leaves its base before this game minute.

// Campaign opponents.
constexpr int kCampaignArmyPercent = 100;    ///< Army it rebuilds to, % of its starting army (Easy: 150).
constexpr int kCampaignArmyFloor = 2000;     ///< Never rebuilds to less than this.
constexpr int kCampaignWavePercent = 50;     ///< One ground wave, % of Easy's (850 of 1,700 late).
constexpr int kCampaignWaveFewerUnits = 1;   ///< Units in one wave: this many fewer than Easy's 3 / 4 / 5.
constexpr int kCampaignOpeningDelaySec = 180;///< Extra wait before its first wave (triggered missions).

/// The army value a custom Easiest house builds up to.
inline int customArmyCeiling(int easyLimit) {
    return int(int64_t(std::max(0, easyLimit)) * kCustomArmyPercent / 100);
}

/// The army value a campaign Easiest opponent replaces its losses up to: the army the mission
/// started it with, at least the floor, never above Easy's limit (1.5 times the starting army).
inline int campaignArmyCeiling(int startingArmy, int easyLimit) {
    const int scaled = int(int64_t(std::max(0, startingArmy)) * kCampaignArmyPercent / 100);
    return std::min(std::max(0, easyLimit), std::max(kCampaignArmyFloor, scaled));
}

/// A wave must stay launchable under the lower ceiling: never wait for more than four fifths of it.
inline int readiness(int required, int ceiling) {
    return ceiling > 0 ? std::min(required, ceiling * 4 / 5) : required;
}

/// One campaign ground wave, never below one cheap unit.
inline int waveValue(int easyValue) {
    return std::max(300, int(int64_t(easyValue) * kCampaignWavePercent / 100));
}

/// Units in one campaign ground wave: fewer than Easy's, never below two.
inline int waveUnits(int easyUnits) {
    return std::max(2, easyUnits - kCampaignWaveFewerUnits);
}

/// Harvesters a custom Easiest house keeps: half Easy's ceiling, rounded up, at least two.
/// 0 (no configured ceiling on this map size) stays 0.
inline int harvesterCeiling(int easyCeiling) {
    return easyCeiling <= 0 ? easyCeiling : std::max(2, (easyCeiling * kCustomHarvesterPercent + 99) / 100);
}

/// No attack leaves a custom Easiest base before its first-attack minute.
inline bool attackHeld(uint32_t cycle, uint32_t firstAttackCycle) { return cycle < firstAttackCycle; }

} // namespace EasiestPolicy

#endif
