#include <players/DeviationReward.h>

#include <globals.h>

#include <Game.h>
#include <House.h>
#include <ObjectBase.h>
#include <players/AIDecisionLog.h>
#include <units/UnitBase.h>

namespace DeviationReward {

namespace {

bool telemetry() { return AITelemetry::log().enabled(); }

void logEvent(int houseID, const char* event, const AITelemetry::Record& details) {
    if (!telemetry() || currentGame == nullptr) return;
    AITelemetry::log().write(currentGame->getGameCycleCount(), houseID, -1, event, details);
}

/// Common identity of a credited event: enough to reconstruct who earned what from the log.
AITelemetry::Record identity(const Provenance& credit) {
    return AITelemetry::Record().set("episode", credit.episodeID)
        .set("beneficiary", credit.beneficiaryHouse)
        .set("reward_item", credit.rewardItemID)
        .set("source", credit.sourceObjectID)
        .set("source_item", credit.sourceItemID)
        .set("deviator", credit.deviatorObjectID);
}

AITelemetry::Record components(const CombatReward::Totals& reward) {
    return AITelemetry::Record().set("damage_value_milli", reward.damageMilli)
        .set("kill_bonus_milli", reward.killBonusMilli)
        .set("hp_removed_milli", reward.hpRemovedMilli)
        .set("hits", static_cast<int64_t>(reward.hits))
        .set("kills", static_cast<int64_t>(reward.kills));
}

House* houseOf(Sint32 houseID) {
    if (currentGame == nullptr || houseID < 0 || houseID >= NUM_HOUSES) return nullptr;
    return currentGame->getHouse(houseID);
}

} // namespace

const Episode& episodeOf(const ObjectBase* object) {
    static const Episode none{};
    if (object == nullptr || !object->isAUnit()) return none;
    return static_cast<const UnitBase*>(object)->getDeviationEpisode();
}

Provenance liveProvenance(const ObjectBase* source, const House* actingOwner) {
    Provenance result;
    const House* beneficiary = actingOwner != nullptr
        ? actingOwner : (source != nullptr ? source->getOwner() : nullptr);
    if (beneficiary != nullptr) result.beneficiaryHouse = beneficiary->getHouseID();
    // Without a live source there is nothing to attribute: the caller took no snapshot and
    // the acting object is gone, so the event stays out of every learning ledger.
    if (source == nullptr || beneficiary == nullptr) return result;

    result.sourceObjectID = source->getObjectID();
    result.sourceItemID = source->getItemID();
    result.rewardItemID = source->getItemID();

    // Exactly one controller is ever credited, so a unit captured from a captor pays only its
    // present controller and nested captures cannot produce recursive rewards.
    const auto& episode = episodeOf(source);
    if (episode.credits() && episode.controllerHouse == result.beneficiaryHouse) {
        result.rewardItemID = Unit_Deviator;
        result.episodeID = episode.id;
        result.deviatorObjectID = episode.deviatorObjectID;
    }
    return result;
}

House* beneficiaryOf(const Provenance& credit, House* damagerOwner) {
    House* named = houseOf(credit.beneficiaryHouse);
    return named != nullptr ? named : damagerOwner;
}

void recordOutgoing(const Provenance& credit, House& beneficiary,
                    const CombatReward::Totals& reward, const ObjectBase* victim) {
    if (!credit.controlled()) return;
    auto& counters = beneficiary.deviationCounters_();
    if (reward.hits == 0) {
        // Friendly fire, overkill and zero-damage contacts are visible but earn nothing.
        ++counters.excluded;
        return;
    }
    counters.outgoingDamageMilli += reward.damageMilli;
    counters.outgoingKillBonusMilli += reward.killBonusMilli;
    counters.outgoingHpMilli += reward.hpRemovedMilli;
    counters.outgoingHits += reward.hits;
    counters.outgoingKills += reward.kills;
    if (!telemetry()) return;
    auto record = identity(credit).set("component", std::string("outgoing"));
    record.set("reward", components(reward));
    if (victim != nullptr) {
        record.set("target", victim->getObjectID()).set("target_item", victim->getItemID())
            .set("target_house", victim->getOwner() ? victim->getOwner()->getHouseID() : -1)
            .set("target_original_house", victim->getOriginalHouseID());
    }
    logEvent(credit.beneficiaryHouse, "deviation_reward", record);
}

void recordAbsorbed(ObjectBase* victim, int64_t beforeHpMilli, int damage,
                    const House* damagerOwner, Uint32 damagerID) {
    if (victim == nullptr || !victim->isAUnit() || currentGame == nullptr) return;
    auto* unit = static_cast<UnitBase*>(victim);
    const auto& episode = unit->getDeviationEpisode();
    if (!episode.credits()) return;
    House* controller = unit->getOwner();
    // Damage that lands after control returned belongs to nobody's capture.
    if (controller == nullptr || controller->getHouseID() != episode.controllerHouse) return;

    // Healing never reaches handleDamage, environmental damage carries no owner, and own,
    // friendly and self damage is not hostile - all of them fail this test. The object
    // check catches the one case the team check cannot: a shot this unit fired itself
    // before it was captured, which now carries an owner hostile to its new controller.
    const bool hostile = damage > 0 && damagerOwner != nullptr
        && damagerID != unit->getObjectID()
        && damagerOwner->getTeamID() != controller->getTeamID();
    const auto reward = CombatReward::hit(
        currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price,
        int64_t(unit->getMaxHealth())*1000, beforeHpMilli, (unit->getHealth()*1000).lround(),
        hostile, /*unitTarget*/false); // losing a borrowed unit is not a killing blow for its captor
    auto& counters = controller->deviationCounters_();
    if (reward.hits == 0) { ++counters.excluded; return; }

    controller->addCombatReward(Unit_Deviator, reward);
    counters.absorbedDamageMilli += reward.damageMilli;
    counters.absorbedHpMilli += reward.hpRemovedMilli;
    counters.absorbedHits += reward.hits;
    if (!telemetry()) return;
    Provenance credit;
    credit.beneficiaryHouse = controller->getHouseID();
    credit.rewardItemID = Unit_Deviator;
    credit.sourceObjectID = unit->getObjectID();
    credit.sourceItemID = unit->getItemID();
    credit.episodeID = episode.id;
    credit.deviatorObjectID = episode.deviatorObjectID;
    logEvent(credit.beneficiaryHouse, "deviation_reward",
        identity(credit).set("component", std::string("absorbed"))
            .set("reward", components(reward))
            .set("attacker_house", damagerOwner ? damagerOwner->getHouseID() : -1)
            .set("target_original_house", unit->getOriginalHouseID()));
}

void creditCommandedDetonation(const Provenance& armed, const ObjectBase& devastator) {
    if (!armed.controlled() || currentGame == nullptr) return;
    House* beneficiary = houseOf(armed.beneficiaryHouse);
    House* original = houseOf(devastator.getOriginalHouseID());
    if (beneficiary == nullptr || original == nullptr) return;

    auto& counters = beneficiary->deviationCounters_();
    ++counters.detonations;
    const bool hostile = beneficiary->getTeamID() != original->getTeamID();
    const int price = currentGame->objectData
        .data[devastator.getItemID()][devastator.getOriginalHouseID()].price;
    // Only the hit points still present are paid, so damage already booked is not paid twice.
    // The completion is a killing blow on the Devastator's own natural type under its
    // original owner, so it reads exactly the record an ordinary lethal hit would read.
    const auto reward = CombatReward::hit(price, int64_t(devastator.getMaxHealth())*1000,
        (devastator.getHealth()*1000).lround(), 0, hostile, true,
        hostile && devastator.getHealth() > 0
            ? House::killBonusPermilleForVictim(devastator.getItemID(),
                devastator.getOriginalHouseID(), price)
            : CombatReward::kBaselineBonusPermille);
    if (reward.hits == 0) { ++counters.excluded; return; }

    beneficiary->addCombatReward(armed.rewardItemID, reward);
    beneficiary->informHasDamaged(armed.rewardItemID, devastator.getHealth().lround());
    if (reward.kills > 0) beneficiary->informHasKilled(devastator.getItemID());
    counters.terminalDamageMilli += reward.damageMilli;
    counters.terminalKillBonusMilli += reward.killBonusMilli;
    counters.terminalHpMilli += reward.hpRemovedMilli;
    logEvent(armed.beneficiaryHouse, "deviation_detonation",
        identity(armed).set("component", std::string("terminal"))
            .set("reward", components(reward))
            .set("object", devastator.getObjectID())
            .set("remaining_health", devastator.getHealth().lround())
            .set("target_original_house", devastator.getOriginalHouseID()));
}

void recordCapture(const UnitBase& unit, const Episode& episode, bool refresh) {
    House* controller = houseOf(episode.controllerHouse);
    if (controller == nullptr) return;
    auto& counters = controller->deviationCounters_();
    if (refresh) ++counters.refreshes; else ++counters.captures;
    logEvent(episode.controllerHouse, "deviation_capture",
        AITelemetry::Record().set("episode", episode.id)
            .set("object", unit.getObjectID())
            .set("target_item", unit.getItemID())
            .set("target_original_house", unit.getOriginalHouseID())
            .set("controller", episode.controllerHouse)
            .set("deviator", episode.deviatorObjectID)
            .set("rewarded", episode.rewarded)
            .set("refresh", refresh)
            .set("health", unit.getHealth().lround()));
}

void recordRelease(const UnitBase& unit, const Episode& episode) {
    House* controller = houseOf(episode.controllerHouse);
    if (controller == nullptr || !episode.active()) return;
    ++controller->deviationCounters_().releases;
    logEvent(episode.controllerHouse, "deviation_release",
        AITelemetry::Record().set("episode", episode.id)
            .set("object", unit.getObjectID())
            .set("target_item", unit.getItemID())
            .set("controller", episode.controllerHouse)
            .set("cycles", currentGame != nullptr
                ? static_cast<int64_t>(currentGame->getGameCycleCount() - episode.startCycle) : 0));
}

void migrateLegacyLedger(House& house) {
    house.resetLearningLedgerForMeasuredScoring();
    ++house.deviationCounters_().legacyLedgerReset;
}

} // namespace DeviationReward
