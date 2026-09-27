#ifndef DEVIATION_REWARD_H
#define DEVIATION_REWARD_H

#include <Definitions.h>
#include <data.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <players/CombatReward.h>

#include <cstdint>

// forward declarations
class House;
class ObjectBase;
class UnitBase;

/**
    Measured Deviator contribution accounting.

    A Deviator removes no hit points of its own, so its learning score used to be a flat
    estimate paid the moment a unit changed hands. That estimate was paid for idle captures,
    for friendly hits and for same-controller refreshes, and the damage a borrowed unit then
    dealt was booked under the borrowed unit's own type. This namespace replaces the estimate
    with what the capture actually achieved:

      - outgoing: hostile damage value plus unit-kill bonuses earned while controlled,
      - absorbed: hostile hit points removed from the borrowed unit while controlled,
      - terminal: the remaining value plus one kill bonus for a completed commanded
        Devastator detonation.

    Every event pays the house that held control when the shot left the weapon. The decision
    is snapshotted into a Provenance at fire/arming time so it survives the firing unit's
    death, its reversion to its original owner, or its recapture by a third house.
 */
namespace DeviationReward {

/**
    Ledger contract version.

    1: measured contribution. The flat conversion estimate (CombatReward::Totals::conversionMilli
       and the matching synthetic raw-damage counter) is no longer produced, and saves written
       before SAVEGAMEVERSION 9849 have it cleared on load - see migrateLegacyLedger().
 */
inline constexpr int kLedgerVersion = 1;

/// Episode id reserved for "this unit is under its own house".
inline constexpr Uint32 kNoEpisode = 0;

/**
    One control interval of one captured unit.

    A same-controller refresh continues the interval; a different controller starts a new one.
    Only the current controller is ever credited, so nested captures cannot pay twice.
*/
struct Episode {
    Uint32 id = kNoEpisode;              ///< kNoEpisode while the unit answers to its own house
    Uint32 deviatorObjectID = NONE_ID;   ///< capturing Deviator; diagnostics only, may be dead
    Uint32 startCycle = 0;
    Sint32 controllerHouse = -1;         ///< house that receives the credit
    bool   rewarded = false;             ///< original house was hostile to the controller

    bool active() const { return id != kNoEpisode; }
    /// Only a hostile-origin capture earns Deviator credit; an allied unit stays on its own type.
    bool credits() const { return active() && rewarded; }

    void save(OutputStream& stream) const {
        stream.writeUint32(id); stream.writeUint32(deviatorObjectID);
        stream.writeUint32(startCycle); stream.writeSint32(controllerHouse);
        stream.writeBool(rewarded);
    }
    void load(InputStream& stream) {
        id = stream.readUint32(); deviatorObjectID = stream.readUint32();
        startCycle = stream.readUint32(); controllerHouse = stream.readSint32();
        rewarded = stream.readBool();
    }
};

/**
    Who a single damage event pays, decided when the shot is fired or the fuse is armed.

    Direct damage can look up its live source. Legacy projectiles have no firing-time
    record, so their outgoing reward is suppressed rather than guessed.
*/
struct Provenance {
    Sint32 beneficiaryHouse = -1;      ///< house credited, snapshotted at fire time
    Uint32 rewardItemID = NONE_ID;     ///< learning bucket: Unit_Deviator while controlled
    Uint32 sourceObjectID = NONE_ID;   ///< firing object, diagnostics
    Uint32 sourceItemID = NONE_ID;     ///< its natural type, diagnostics
    Uint32 episodeID = kNoEpisode;
    Uint32 deviatorObjectID = NONE_ID;

    static constexpr Sint32 LegacyUnknownBeneficiary = -2;
    bool allowsLiveLookup() const { return beneficiaryHouse != LegacyUnknownBeneficiary; }
    bool known() const { return rewardItemID != NONE_ID; }
    bool controlled() const { return episodeID != kNoEpisode; }

    void save(OutputStream& stream) const {
        stream.writeSint32(beneficiaryHouse); stream.writeUint32(rewardItemID);
        stream.writeUint32(sourceObjectID); stream.writeUint32(sourceItemID);
        stream.writeUint32(episodeID); stream.writeUint32(deviatorObjectID);
    }
    void load(InputStream& stream) {
        beneficiaryHouse = stream.readSint32(); rewardItemID = stream.readUint32();
        sourceObjectID = stream.readUint32(); sourceItemID = stream.readUint32();
        episodeID = stream.readUint32(); deviatorObjectID = stream.readUint32();
    }
};

/**
    Deterministic per-house totals.

    These live in simulation state and are serialized, so they are identical on every peer and
    do not depend on whether diagnostic logging is enabled.
*/
struct HouseCounters {
    int64_t outgoingDamageMilli = 0, outgoingKillBonusMilli = 0, outgoingHpMilli = 0;
    int64_t absorbedDamageMilli = 0, absorbedHpMilli = 0;
    int64_t terminalDamageMilli = 0, terminalKillBonusMilli = 0, terminalHpMilli = 0;
    uint64_t outgoingHits = 0, outgoingKills = 0, absorbedHits = 0;
    uint64_t captures = 0, refreshes = 0, releases = 0;
    uint64_t detonations = 0, excluded = 0;
    /// Set when a pre-9849 save had its flat conversion estimate cleared on load.
    uint64_t legacyLedgerReset = 0;

    int64_t totalMilli() const {
        return outgoingDamageMilli + outgoingKillBonusMilli + absorbedDamageMilli
             + terminalDamageMilli + terminalKillBonusMilli;
    }

    void save(OutputStream& stream) const {
        for (const auto value : {outgoingDamageMilli, outgoingKillBonusMilli, outgoingHpMilli,
                                 absorbedDamageMilli, absorbedHpMilli, terminalDamageMilli,
                                 terminalKillBonusMilli, terminalHpMilli})
            stream.writeSint64(value);
        for (const auto value : {outgoingHits, outgoingKills, absorbedHits, captures, refreshes,
                                 releases, detonations, excluded, legacyLedgerReset})
            stream.writeUint64(value);
    }
    void load(InputStream& stream) {
        for (auto* value : {&outgoingDamageMilli, &outgoingKillBonusMilli, &outgoingHpMilli,
                            &absorbedDamageMilli, &absorbedHpMilli, &terminalDamageMilli,
                            &terminalKillBonusMilli, &terminalHpMilli})
            *value = stream.readSint64();
        for (auto* value : {&outgoingHits, &outgoingKills, &absorbedHits, &captures, &refreshes,
                            &releases, &detonations, &excluded, &legacyLedgerReset})
            *value = stream.readUint64();
    }
};

/// The control interval of an object, or an inactive Episode for anything that is not a unit.
const Episode& episodeOf(const ObjectBase* object);

/// Attribution of an object acting right now. Used when no snapshot was taken.
Provenance liveProvenance(const ObjectBase* source, const House* actingOwner);

/**
    The house a snapshotted credit pays. Falls back to the house the engine is damaging on
    behalf of when the snapshot names no beneficiary.
*/
House* beneficiaryOf(const Provenance& credit, House* damagerOwner);

/**
    Books an outgoing damage reward and records the deviation telemetry event.
    The reward itself is added by the caller; this only maintains the measured counters.
*/
void recordOutgoing(const Provenance& credit, House& beneficiary,
                    const CombatReward::Totals& reward, const ObjectBase* victim);

/**
    Credits hit points a borrowed unit absorbed for its controller.

    Only hostile incoming damage during a hostile-origin control interval counts. Friendly,
    own, self, environmental and healing damage, overkill, damage after control returned and
    allied-origin captures earn nothing, and the borrowed unit's death is not a kill bonus.

    \param  damagerID  the attacking object. A unit that fired before being captured can be
                       caught by its own shot, whose stored owner is now hostile to it; that
                       is still self damage and earns nothing.
*/
void recordAbsorbed(ObjectBase* victim, int64_t beforeHpMilli, int damage,
                    const House* damagerOwner, Uint32 damagerID);

/**
    Books the once-only terminal reward of a completed commanded Devastator detonation:
    the value of the hit points still present plus one unit-kill bonus.
    Combat deaths and fuses interrupted by an enemy never reach here.
*/
void creditCommandedDetonation(const Provenance& armed, const ObjectBase& devastator);

/// Logs a capture, refresh or release of control. Counters stay deterministic.
void recordCapture(const UnitBase& unit, const Episode& episode, bool refresh);
void recordRelease(const UnitBase& unit, const Episode& episode);

/**
    Legacy-save migration.

    A save written before SAVEGAMEVERSION 9849 carries the flat conversion estimate and books
    every borrowed unit's damage under the type that fired it. Neither can be rescored, and
    keeping either one would mix two incompatible accountings - measured rewards from this
    session over a whole match of old losses, plus captures paid for twice.

    The unit-mix learning ledger is therefore reset as a whole on load: combat rewards, raw
    damage counters and unit-loss counts, together with QuantBot's cached performance window.
    Structure losses stay, because build prerequisites read them as "we owned one and lost it",
    and general match statistics stay because they are not learning input. Measured coverage
    starts at the load, and units that are already deviated are bootstrapped into a fresh
    control interval so the new policy applies immediately rather than after the next capture.
*/
void migrateLegacyLedger(House& house);

} // namespace DeviationReward

#endif // DEVIATION_REWARD_H
