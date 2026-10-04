#ifndef ARMY_POSTURE_POLICY_H
#define ARMY_POSTURE_POLICY_H

/*
    House-level army posture for Custom Hard/Brutal QuantBot.

    Three postures, all saved: Offensive (the established behaviour), Withdrawing
    (the tracked wave is recalled to a protected rally while no new offensive
    dispatch happens) and Recovering (the army assembles and rebuilds at home
    before attacking again).

    Everything here is integer and stateless apart from the explicitly saved
    AttritionLedger, so two peers running the same cycle reach the same posture.
    No wall clock, no random source, no container iteration order.

    Deliberate restrictions, from the reviewed design:

    * Entering Withdrawing needs ALL of a material loss floor, a loss share of
      the force that was actually deployed when the window opened, a confirmed
      bad trade, and a corroborating readiness or front signal. A single lost
      unit, one ordinary raid, or one dangerous tile near the base never moves
      the whole house.
    * The attrition window must be fully covered by samples before it can
      trigger anything, so an opening skirmish cannot panic a young house.
    * Dwell times delay ordinary transitions only. A severe local defeat or an
      attack on the production core is an emergency and bypasses them, because a
      cooldown that blocks an urgent retreat is worse than no posture at all.
    * Recovering has a bounded fallback: it resumes even with an unknown front,
      so the house cannot freeze waiting for information it may never get.
*/

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ArmyPosturePolicy {

enum class Posture : uint8_t { Offensive = 0, Withdrawing = 1, Recovering = 2 };

inline const char* postureName(Posture posture) {
    switch (posture) {
        case Posture::Withdrawing: return "withdrawing";
        case Posture::Recovering:  return "recovering";
        default:                   return "offensive";
    }
}

inline bool validPosture(unsigned value) { return value <= unsigned(Posture::Recovering); }

/// Ring of attrition baselines. Each entry records the cumulative own
/// mobile-combat loss cost and the cumulative confirmed hostile mobile-combat
/// kill cost at the moment it was taken, plus the deployable force cost at that
/// moment. Differences against an entry give a windowed rate without storing a
/// per-event list, which is what keeps the work bounded in a 900-unit match.
constexpr size_t kAttritionSamples = 10;
/// A save or checkpoint claiming more than the ring can hold is corrupt.
constexpr uint32_t kAttritionSampleLimit = kAttritionSamples;

struct AttritionSample {
    uint32_t cycle = 0;
    int64_t lostCost = 0;
    int64_t killCost = 0;
    int64_t deployable = 0;
};

struct AttritionLedger {
    /// Cumulative, monotonic, own mobile-combat unit loss cost.
    int64_t lostCost = 0;
    /// Cumulative, monotonic, confirmed hostile mobile-combat kill cost.
    int64_t killCost = 0;
    /// Valid entries, oldest first from `first()`.
    uint32_t count = 0;
    uint32_t next = 0;
    std::array<AttritionSample, kAttritionSamples> samples{};

    void reset() { *this = AttritionLedger(); }

    const AttritionSample& at(uint32_t index) const {
        // Index 0 is the oldest retained sample.
        const uint32_t base = count < kAttritionSamples ? 0u : next;
        return samples[(base + index) % kAttritionSamples];
    }
    const AttritionSample* newest() const {
        if (count == 0) return nullptr;
        return &at(count - 1);
    }

    /// One baseline per stride. Returns true when a sample was actually added.
    bool sample(uint32_t cycle, int64_t deployable, uint32_t strideCycles) {
        const uint32_t stride = std::max<uint32_t>(1, strideCycles);
        if (const AttritionSample* last = newest()) {
            // Unsigned-safe: a loaded save can carry a sample from a later cycle
            // than the one being replayed only if the stream is corrupt, and the
            // comparison below then simply refuses to add until time passes.
            if (cycle < last->cycle || cycle - last->cycle < stride) return false;
        }
        samples[next] = {cycle, lostCost, killCost, std::max<int64_t>(0, deployable)};
        next = (next + 1) % kAttritionSamples;
        if (count < kAttritionSamples) ++count;
        return true;
    }

    /// Newest baseline at least `windowCycles` old. Null while the ring does not
    /// reach that far back, which is what keeps a young house out of the
    /// strategic triggers entirely.
    const AttritionSample* windowStart(uint32_t cycle, uint32_t windowCycles) const {
        const AttritionSample* chosen = nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            const AttritionSample& sample = at(i);
            if (sample.cycle > cycle || cycle - sample.cycle < windowCycles) break;
            chosen = &sample;
        }
        return chosen;
    }

    template<class Stream> void save(Stream& stream) const {
        stream.writeSint64(lostCost);
        stream.writeSint64(killCost);
        stream.writeUint32(count);
        // Serialised oldest-first. The ring cursor is deliberately NOT written:
        // it is implied by that order, and writing it would make a reloaded
        // ledger re-serialise to different bytes than the one it was loaded
        // from, which is exactly the kind of difference a save-parity check
        // exists to catch.
        for (uint32_t i = 0; i < count; ++i) {
            const AttritionSample& sample = at(i);
            stream.writeUint32(sample.cycle);
            stream.writeSint64(sample.lostCost);
            stream.writeSint64(sample.killCost);
            stream.writeSint64(sample.deployable);
        }
    }
    /// Throws through the stream's own error type on an impossible count, the
    /// same way the other QuantBot load gates validate their sizes.
    template<class Stream, class OnInvalid>
    void load(Stream& stream, OnInvalid invalid) {
        reset();
        lostCost = stream.readSint64();
        killCost = stream.readSint64();
        const uint32_t storedCount = stream.readUint32();
        if (storedCount > kAttritionSampleLimit) { invalid(); return; }
        for (uint32_t i = 0; i < storedCount; ++i) {
            AttritionSample sample;
            sample.cycle = stream.readUint32();
            sample.lostCost = stream.readSint64();
            sample.killCost = stream.readSint64();
            sample.deployable = stream.readSint64();
            samples[i] = sample;
        }
        count = storedCount;
        next = storedCount % kAttritionSamples;
        if (lostCost < 0) lostCost = 0;
        if (killCost < 0) killCost = 0;
    }
};

/// Every tunable, in integer basis points or cycles. Provisional values live in
/// QuantBotConfig so they are hashed for multiplayer and editable in the shared recovery settings.
struct Thresholds {
    uint32_t windowCycles = 0;      ///< Attrition window, 90 s by default.
    uint32_t sampleStrideCycles = 0;///< Derived: windowCycles/8, at least 1.
    int lossShareBps = 2000;        ///< Window loss >= 20% of window-start deployable cost.
    int tradeShareBps = 6500;       ///< Confirmed hostile loss < 65% of our own.
    int64_t lossFloor = 0;          ///< Material floor; below it, no strategic reaction.
    int localWithdrawBps = 13000;   ///< Local squad disengage at 1.3x observed enemy.
    int localSevereBps = 20000;     ///< 2.0x is severe: withdraw without dwell.
    uint32_t localPersistCycles = 0;///< Local disadvantage must persist this long.
    int resumeAssembledBps = 8000;  ///< ~80% of the designated wave gathered.
    int resumeAdvantageBps = 12000; ///< Front strength >= 1.2x observed enemy.
    uint32_t stabiliseCycles = 0;   ///< Minimum quiet time before resuming.
    uint32_t minWithdrawCycles = 0; ///< Ordinary dwell in Withdrawing.
    uint32_t maxWithdrawCycles = 0; ///< Bounded fallback out of Withdrawing.
    uint32_t maxRecoverCycles = 0;  ///< Bounded fallback out of Recovering.
    int outnumberedBps = 15000;     ///< >= 1.5x hostile front power is outnumbered.
    int dispatchBypassBps = 8000;   ///< 80% of the CONFIGURED military value limit.
};

/// Observed situation for one evaluation. Every field is produced from
/// fog-respecting, team-respecting observation of active units.
struct Situation {
    uint32_t cycle = 0;
    bool windowCovered = false;     ///< The attrition window has a real baseline.
    int64_t windowStartDeployable = 0;
    int64_t windowLoss = 0;
    int64_t windowKill = 0;
    bool readinessWorsening = false;///< Ready-force trend is down.
    bool adverseMainFront = false;  ///< Observed front power is against us.
    bool coreUnderAttack = false;   ///< A production/economy core asset is being hit.
    bool severeLocalDefeat = false; ///< Main squad is overwhelmed right now.
    int64_t assembledPower = 0;     ///< Healthy power already at the rally.
    int64_t designatedPower = 0;    ///< Healthy power that should be there.
    int64_t frontFriendly = 0;
    int64_t frontHostile = 0;
    bool frontObserved = false;     ///< We have actually seen the front.
    /// How long it has actually been quiet: cycles since the last material
    /// mobile-combat loss. Resume is measured against this, not against how long
    /// the posture happens to have been set, so a house that is still bleeding
    /// cannot be called stable merely because its state is old.
    uint32_t quietCycles = 0;
    /// A material loss landed inside the quiet window. Blocks every resume path,
    /// including the bounded fallbacks, so a timeout cannot walk an army back
    /// into the fire it is still taking.
    bool recentSeriousLoss = false;
    /// Whether this force can answer hostile aircraft at all. Reported for
    /// telemetry and for the air composition distinction; never used to pretend
    /// hostile aircraft are harmless.
    bool canAnswerAir = false;
};

struct Decision {
    Posture posture = Posture::Offensive;
    bool changed = false;
    bool emergency = false;
    const char* reason = "hold";
};

/// Material attrition test. All four conditions, never any of them alone.
inline bool materialAttrition(const Situation& s, const Thresholds& t) {
    if (!s.windowCovered) return false;
    if (s.windowLoss < std::max<int64_t>(1, t.lossFloor)) return false;
    if (s.windowStartDeployable <= 0) return false;
    if (s.windowLoss * 10000 < int64_t(t.lossShareBps) * s.windowStartDeployable) return false;
    if (s.windowKill * 10000 >= int64_t(t.tradeShareBps) * s.windowLoss) return false;
    return s.readinessWorsening || s.adverseMainFront;
}

inline bool assembled(const Situation& s, const Thresholds& t) {
    if (s.designatedPower <= 0) return true; // Nothing left to gather.
    return s.assembledPower * 10000 >= int64_t(t.resumeAssembledBps) * s.designatedPower;
}

inline bool frontFavourable(const Situation& s, const Thresholds& t) {
    if (!s.frontObserved) return false;            // Unknown needs scouting.
    if (s.frontHostile <= 0) return true;
    return s.frontFriendly * 10000 >= int64_t(t.resumeAdvantageBps) * s.frontHostile;
}

inline uint32_t elapsed(uint32_t cycle, uint32_t since) {
    return cycle >= since ? cycle - since : 0u;
}

inline Decision evaluate(Posture current, uint32_t since, const Situation& s, const Thresholds& t) {
    Decision decision{current, false, false, "hold"};
    const uint32_t age = elapsed(s.cycle, since);
    // An emergency is never gated by dwell, cooldown or window maturity.
    const bool emergency = s.severeLocalDefeat || s.coreUnderAttack;
    switch (current) {
        case Posture::Offensive: {
            if (emergency) {
                return {Posture::Withdrawing, true, true,
                        s.severeLocalDefeat ? "severe_local_defeat" : "core_under_attack"};
            }
            if (materialAttrition(s, t))
                return {Posture::Withdrawing, true, false, "sustained_main_force_attrition"};
        } break;
        case Posture::Withdrawing: {
            // Reaching the rally ends the withdrawal, but not before the
            // minimum dwell: a wave that happened to be near home already must
            // not flip straight back out on the next evaluation. The bounded
            // fallback still applies, so a trapped remnant cannot hold the
            // house in place for ever.
            if (age >= t.minWithdrawCycles && assembled(s, t))
                return {Posture::Recovering, true, false, "withdrawal_assembled"};
            if (t.maxWithdrawCycles > 0 && age >= t.maxWithdrawCycles)
                return {Posture::Recovering, true, false, "withdrawal_timeout"};
        } break;
        case Posture::Recovering: {
            if (emergency) {
                // Already recovering; stay, but report it so base defence and
                // repair keep their priority and telemetry shows the cause.
                return {Posture::Recovering, false, true,
                        s.severeLocalDefeat ? "severe_local_defeat" : "core_under_attack"};
            }
            // Fresh material losses are the one thing no resume path may
            // ignore. Without this, a long-set posture plus a timeout would
            // march the rebuilt army straight back into fire it is still
            // taking, which is the behaviour this whole posture exists to stop.
            if (s.recentSeriousLoss) break;
            // Stability is measured from the last material loss as well as from
            // the posture change, so "quiet" means actually quiet.
            const bool stable = age >= t.stabiliseCycles && s.quietCycles >= t.stabiliseCycles;
            if (stable && assembled(s, t) && frontFavourable(s, t))
                return {Posture::Offensive, true, false, "recovered_with_front_advantage"};
            if (t.maxRecoverCycles > 0 && age >= t.maxRecoverCycles && assembled(s, t))
                return {Posture::Offensive, true, false, "recovery_timeout_assembled"};
            if (t.maxRecoverCycles > 0 && age >= 2 * t.maxRecoverCycles)
                return {Posture::Offensive, true, false, "recovery_timeout_bounded"};
        } break;
    }
    return decision;
}

/// True while this posture must not start a fresh offensive dispatch.
inline bool suppressesOffensiveDispatch(Posture posture) {
    return posture != Posture::Offensive;
}
/// True while fresh production and idle troops are held at the protected rally.
inline bool holdsReinforcementsAtHome(Posture posture) {
    return posture != Posture::Offensive;
}

/// Local squad disengage. Persistence is the caller's: it passes how long the
/// disadvantage has already held. A severe disadvantage needs no persistence.
struct LocalVerdict { bool withdraw = false; bool severe = false; };
inline LocalVerdict localVerdict(int64_t friendlyPower, int64_t hostilePower,
                                 uint32_t persistedCycles, const Thresholds& t) {
    if (hostilePower <= 0) return {};
    if (friendlyPower <= 0) return {true, true};
    const bool severe = hostilePower * 10000 >= int64_t(t.localSevereBps) * friendlyPower;
    if (severe) return {true, true};
    const bool adverse = hostilePower * 10000 >= int64_t(t.localWithdrawBps) * friendlyPower;
    return {adverse && persistedCycles >= t.localPersistCycles, false};
}

/// Outnumbered offensive dispatch gate (the approved new user requirement).
///
/// `configuredLimit` must be the CONFIGURED militaryValueLimit, never Brutal's
/// rolling override budget: that budget is owned value plus headroom, so it
/// grows with the army and 80% of it would be reachable at any size.
///
/// Nonpositive configured limits are handled explicitly rather than falling out
/// of the arithmetic: there is no meaningful 80% reference, so the bypass is
/// refused. The house is not deadlocked by that, because the deferral only
/// applies while the observed hostile front really is at least
/// `outnumberedBps` of our own power.
struct DispatchGate {
    bool outnumbered = false;
    bool bypass = false;
    bool defer = false;
    const char* reason = "clear";
};
inline DispatchGate dispatchGate(int64_t ownFrontPower, int64_t hostileFrontPower,
                                 int configuredLimit, int64_t healthyDeployableValue,
                                 const Thresholds& t) {
    DispatchGate gate;
    if (hostileFrontPower > 0) {
        gate.outnumbered = ownFrontPower <= 0
            || hostileFrontPower * 10000 >= int64_t(t.outnumberedBps) * ownFrontPower;
    }
    if (configuredLimit > 0) {
        const int64_t threshold =
            (int64_t(configuredLimit) * std::clamp(t.dispatchBypassBps, 0, 10000) + 9999) / 10000;
        gate.bypass = healthyDeployableValue >= threshold;
    }
    if (!gate.outnumbered) { gate.reason = "not_outnumbered"; return gate; }
    if (gate.bypass) { gate.reason = "value_threshold_reached"; return gate; }
    gate.defer = true;
    gate.reason = configuredLimit > 0 ? "outnumbered_below_value_threshold"
                                      : "outnumbered_no_configured_limit";
    return gate;
}

/// Rounded-up value the bypass needs, for telemetry and tests.
inline int64_t dispatchBypassValue(int configuredLimit, const Thresholds& t) {
    if (configuredLimit <= 0) return -1;
    return (int64_t(configuredLimit) * std::clamp(t.dispatchBypassBps, 0, 10000) + 9999) / 10000;
}

} // namespace ArmyPosturePolicy

#endif // ARMY_POSTURE_POLICY_H
