#ifndef COMBAT_REWARD_H
#define COMBAT_REWARD_H
#include <algorithm>
#include <cstdint>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
namespace CombatReward {
struct Totals {
    int64_t damageMilli = 0, killBonusMilli = 0, conversionMilli = 0, hpRemovedMilli = 0;
    uint64_t hits = 0, kills = 0;
    int64_t total() const { return damageMilli + killBonusMilli + conversionMilli; }
    void save(OutputStream& stream) const {
        stream.writeSint64(damageMilli); stream.writeSint64(killBonusMilli); stream.writeSint64(conversionMilli);
        stream.writeSint64(hpRemovedMilli); stream.writeUint64(hits); stream.writeUint64(kills);
    }
    void load(InputStream& stream) {
        damageMilli=stream.readSint64(); killBonusMilli=stream.readSint64(); conversionMilli=stream.readSint64();
        hpRemovedMilli=stream.readSint64(); hits=stream.readUint64(); kills=stream.readUint64();
    }
};
// The scale of a killing blow, in thousandths of the victim's price. 200 is the
// former flat 20% and remains the answer whenever there is nothing to go on.
constexpr int kBaselineBonusPermille = 200;
constexpr int kMinBonusPermille = 50, kMaxBonusPermille = 600;
constexpr int kKillBonusPolicyVersion = 2, kPerformancePriorUnits = 4;
/**
    How much is learned from destroying one unit of an enemy type, from that type's own
    record with its owner: the damage value it has dealt against the value its losses
    have already cost. Both sides carry four purchase prices of trial prior, so an
    unproven type scores exactly the baseline and equal evidence on both sides does too.
    A type that keeps dying without earning falls towards 5%; an efficient one reaches 60%.

    \a damageMilli   damage value that type has dealt, excluding every kill bonus, so a
                     bonus can never become an input to the next bonus for the same type
    \a lostValueMilli value of that type its owner has already lost
    \a price         that type's price for its owner; a non-positive price has no scale
*/
inline int typeBonusPermille(int64_t damageMilli, int64_t lostValueMilli, int price) {
    if (price <= 0) return kBaselineBonusPermille;
    // Both sides are capped far above any total a match can reach, which is what makes
    // the multiplication below safe on any conforming 64-bit integer without needing a
    // wider type. Capping both sides identically keeps equal evidence at the baseline.
    constexpr int64_t kCap = int64_t(1) << 50;
    const int64_t prior = std::min<int64_t>(kCap, int64_t(price) * kPerformancePriorUnits * 1000);
    const int64_t earned = std::min<int64_t>(kCap, std::max<int64_t>(0, damageMilli)) + prior;
    const int64_t spent = std::min<int64_t>(kCap, std::max<int64_t>(0, lostValueMilli)) + prior;
    return static_cast<int>(std::clamp<int64_t>(int64_t(kBaselineBonusPermille) * earned / spent,
                                                kMinBonusPermille, kMaxBonusPermille));
}
// HP and credit amounts use thousandths. Only actual enemy HP removed earns
// value; repeated damage callbacks on a dead object earn nothing.
inline Totals hit(int price, int64_t maxHpMilli, int64_t beforeHpMilli,
                  int64_t afterHpMilli, bool hostile, bool unitTarget,
                  int bonusPermille = kBaselineBonusPermille) {
    Totals result;
    if (!hostile || price <= 0 || maxHpMilli <= 0 || beforeHpMilli <= 0) return result;
    const auto removed = std::max<int64_t>(0, std::min(beforeHpMilli,maxHpMilli)
        - std::max<int64_t>(0,afterHpMilli));
    if (removed == 0) return result;
    result.damageMilli = int64_t(price)*1000*removed/maxHpMilli;
    result.hpRemovedMilli = removed;
    result.hits = 1;
    if (unitTarget && afterHpMilli <= 0) {
        // The bound holds here as well, so no caller can widen it by passing a scale.
        result.killBonusMilli = int64_t(price)
            * std::clamp(bonusPermille, kMinBonusPermille, kMaxBonusPermille);
        result.kills = 1;
    }
    return result;
}
}
#endif
