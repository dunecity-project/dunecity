/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

// QuantBot ground army: base defence, waves and hunts, posture, assembly and regrouping, colony
// guards, squads and the per-unit controller pass.

#include "QuantBotInternal.h"

#include <FormationOrders.h>

using QuantBotInternal::mobileCombatItem;

namespace QuantBotInternal {
/// Mobile combat units only. Transports, workers, builders, saboteurs, worms and
/// ambient traffic are excluded from every strength, loss and kill figure used by
/// the army posture machine, so a lost harvester or a downed carryall is economic
/// news rather than a reason for the whole house to change posture.
bool mobileCombatItem(Uint32 item) {
    if (!isUnit(item) || isAmbientUnit(item) || isCarryallUnit(item)) return false;
    if (isHarvesterLikeUnit(item)) return false;
    return item != Unit_MCV && item != Unit_Sandworm && item != Unit_Saboteur
        && item != Unit_Frigate;
}
} // namespace QuantBotInternal


void QuantBot::scrambleUnitsAndDefend(const ObjectBase* intruder, bool clearingSpice, const ObjectBase* protectedAsset) {
    AITelemetry::PerformanceScope perfScope("ai.scrambleUnitsAndDefend", getGameCycleCount(), getHouse()->getHouseID());
    if (supportMode || !intruder || intruder->getHealth() <= 0
        || intruder->getOwner()->getTeamID() == getHouse()->getTeamID()) return;
    const Coord contact = intruder->getLocation();
    const auto* victim = intruder->isAUnit() ? static_cast<const UnitBase*>(intruder)->getTarget() : nullptr;
    const bool baseAttack = !clearingSpice && victim && victim->isAStructure()
        && victim->getOwner() == getHouse();
    if (!getMap().tileExists(contact)) return;
    if (isCampaignEnemy() && !campaignLocalContact(intruder)) return;
    // Debounce volleys by local district, independently for air and ground.
    // This state is saved: neither rendering speed nor telemetry affects orders.
    const Uint32 key = ((contact.y / 8) * ((getMap().getSizeX()+7)/8) + contact.x/8) * 2
        + (intruder->isAFlyingUnit() ? 1 : 0);
    const Uint32 now = getGameCycleCount();
    auto previous = defenceResponseCycles.find(key);
    if (previous != defenceResponseCycles.end() && now-previous->second < MILLI2CYCLES(clearingSpice ? 5000 : 2000)) return;
    defenceResponseCycles[key] = now;
    const auto isProtectedAsset=[&](const ObjectBase* object) {
        return object && object->getOwner()==getHouse()
            && (object->isAStructure() || object->getItemID()==Unit_Harvester
                || object->getItemID()==Unit_RebelHarvester);
    };
    if (!clearingSpice && (isProtectedAsset(protectedAsset) || isProtectedAsset(victim))) {
        const auto& config=getQuantBotConfig();
        tryLaunchOrnithopterStrike(config.getSettings(static_cast<int>(difficulty)),config,
            isProtectedAsset(protectedAsset) ? intruder : nullptr,
            isProtectedAsset(protectedAsset) && protectedAsset->isAStructure());
    }
    // Aircraft over one of our buildings are an anti-air problem, not a
    // reinforcement problem. The proportional response below would send
    // whatever is nearest at the aircraft's current tile, which a ground unit
    // may not be able to stand on and which the engine drops as a target the
    // moment it is out of weapon range. Hand the whole contact to the rescue.
    const bool airOnBuilding = !clearingSpice && intruder->isAFlyingUnit()
        && ((isProtectedAsset(protectedAsset) && protectedAsset->isAStructure())
            || (isProtectedAsset(victim) && victim->isAStructure()));
    if (airOnBuilding) {
        defendStructuresFromAircraft();
        return;
    }
    // A building we own - the main base or an outlying colony, including an
    // R/C/I zone - is actually being attacked. In Custom Hard/Brutal the
    // nearest free troops hunt their way to that building
    // (scrambleWholeArmyToDefend), which replaces the district response
    // below. Workers and ordinary contacts keep that established local path.
    //
    // "Actually" is the damage event itself, or an attacker standing in weapon
    // range of the building it has targeted. An idle enemy across the map, an
    // attacker that has stopped, a dead contact and a worker raid are none of
    // those, and none of them scrambles the house.
    const ObjectBase* attackedBuilding = nullptr;
    if (!clearingSpice && intruder->getItemID()!=Unit_Sandworm) {
        if (protectedAsset && protectedAsset->isAStructure()
            && protectedAsset->getOwner()==getHouse() && protectedAsset->getHealth()>0)
            attackedBuilding = protectedAsset;
        else if (attackingOwnedBuilding(intruder)) attackedBuilding = intruder->getTarget();
    }
    if (attackedBuilding && wholeArmyBaseDefence()) {
        scrambleWholeArmyToDefend(intruder,attackedBuilding);
        return;
    }
    // One of our own buildings or workers is being shot at. This is the event
    // the player sees as "nobody came to help": the proportional reinforcement
    // budget below answers a 10,000-credit assault with the nearest handful of
    // troops and silently skips everything that happens to be shooting at
    // something else. For an attack on an asset we own, the nearby troops all
    // respond in this one event instead.
    const bool assetEmergency = !clearingSpice
        && (isProtectedAsset(protectedAsset) || isProtectedAsset(victim));
    auto value = [&](const ObjectBase* object) {
        const int price = currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
        return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
    };
    int threatValue = value(intruder), committed = 0;
    // Sized defence (X, on by default, docs/formations.md "Round 5"): a worker under ground attack
    // counts the enemy within 12 tiles (not 8) and gets 1.5x it, the bundle first, or nobody
    // when everything in reach cannot match the threat.
    const bool sizedWorker = assetEmergency && !baseAttack && !intruder->isAFlyingUnit()
        && forceVariant(kForceSizedDefence);
    const int threatTiles = sizedWorker ? ForceConcentrationPolicy::kPoolThreatTiles : 8;
    constexpr int clearingRadius=18;
    if (clearingSpice) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()->getTeamID()!=getHouse()->getTeamID() && structure->getHealth()>0
            && structure->canAttack() && structure->getItemID()!=Structure_Palace
            && structure->isVisible(getHouse()->getTeamID())
            && blockDistance(contact,structure->getLocation())<=8) threatValue+=value(structure);
    }
    std::vector<SimpleArmyPolicy::Responder> candidates;
    for (const auto* unit : getUnitList()) {
        if (!unit->isActive() || !unit->canAttack()) continue;
        if (unit != intruder && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->isVisible(getHouse()->getTeamID())
            && unit->isAFlyingUnit() == intruder->isAFlyingUnit()
            && blockDistance(contact,unit->getLocation()) <= threatTiles) threatValue += value(unit);
        if (unit->getOwner()!=getHouse() || !unit->isRespondable()
            || !unit->canAttack(intruder) || unit->getAttackMode()==RETREAT
            || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Harvester
            || unit->getItemID()==Unit_Ornithopter) continue; // Air planner prioritizes active base/worker attackers.
        // A human order and a repair run outrank the scramble, and a worker raid
        // cannot cancel an ongoing whole-army base response.
        if (unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::BaseDefence}) != UnitRole::Free) continue;
        if (clearingSpice && blockDistance(contact,unit->getLocation())>clearingRadius) continue;
        const auto* target=unit->getTarget();
        const auto assignment=defenceAssignments.find(unit->getObjectID());
        if (assignment!=defenceAssignments.end()) {
            const auto* assigned=getObject(assignment->second);
            if (assigned && assigned->isAFlyingUnit() && airAttackContinues(unit,assigned)) continue;
        }
        if (assignment!=defenceAssignments.end() && target && target->isAUnit()) {
            const auto* victim=static_cast<const UnitBase*>(target)->getTarget();
            const auto* newVictim=intruder->isAUnit() ? static_cast<const UnitBase*>(intruder)->getTarget() : nullptr;
            if (victim && victim->isAStructure() && victim->getOwner()==getHouse()
                && (!newVictim || !newVictim->isAStructure() || newVictim->getOwner()!=getHouse())) continue;
        }
        const bool hostileTarget=target && target->getHealth()>0
            && target->getOwner()->getTeamID()!=getHouse()->getTeamID();
        // Count troops already fighting or travelling to this contact. They do
        // not need a fresh command on every hit. Do not pull units out of another fight.
        if (hostileTarget && target->isAFlyingUnit()==intruder->isAFlyingUnit()
            && blockDistance(contact,target->getLocation())<=8) { committed+=value(unit); continue; }
        // Being busy elsewhere is a reason not to interrupt an ordinary
        // skirmish, but not a reason to leave our own base or worker to die.
        // Hunt troops reach here with no target of their own most of the time;
        // this is what lets the emergency recall them as well.
        if (!assetEmergency && hostileTarget
            && blockDistance(unit->getLocation(),target->getLocation())<=unit->getWeaponRange()) continue;
        candidates.push_back({unit->getObjectID(),value(unit),blockDistance(contact,unit->getLocation()).lround()});
    }
    auto response=clearingSpice
        ? SimpleArmyPolicy::clearingForce(threatValue,committed,candidates,clearingRadius)
        : SimpleArmyPolicy::reinforcements(threatValue,committed,candidates);
    if (sizedWorker) {
        std::vector<ForceConcentrationPolicy::SizedResponder> responders;
        int64_t reachable = 0;
        for (const auto& candidate : candidates) {
            const bool bundled = standsInBundle(static_cast<const UnitBase*>(getObject(candidate.id)));
            responders.push_back({candidate.id,candidate.value,candidate.distance,bundled});
            if (bundled || candidate.distance <= ForceConcentrationPolicy::kSizedBandTiles) reachable += candidate.value;
        }
        if (!ForceConcentrationPolicy::answerable(threatValue,committed,reachable)) {
            ++forceStats.declinedResponses;
            traceDecision("defence_declined",AITelemetry::Record().set("target",intruder->getObjectID())
                .set("x",contact.x).set("y",contact.y).set("threat_value",threatValue)
                .set("committed_value",committed).set("reachable_value",reachable));
            return;
        }
        response=ForceConcentrationPolicy::sizedPool(ForceConcentrationPolicy::sizedRequired(threatValue),committed,
            responders,ForceConcentrationPolicy::kSizedBandTiles);
        forceStats.sizedResponses+=int(response.size());
    }
    // Troops already beside an attacked base must fight, even if distant
    // responders satisfy the proportional reinforcement budget. An attack on
    // anything we own widens that to a band that grows with the local threat,
    // so an overwhelming assault is met by everything that can realistically
    // get there rather than by three units. Still bounded, still one event, and
    // still nearest-first: nothing here is a new squad or a standing order.
    const int emergencyBand = assetEmergency ? emergencyResponseRadius(threatValue-committed) : 12;
    if ((baseAttack || assetEmergency) && !sizedWorker) for (const auto& candidate : candidates)
        if (candidate.distance <= emergencyBand
            && std::find(response.begin(),response.end(),candidate.id)==response.end())
            response.push_back(candidate.id);
    int dispatched=0;
    // Defence is never slowed to a formation pace (docs/formations.md).
    for (const auto id:response) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        groundSquad.erase(id);
        // Distant non-forced Area Guard targets are discarded by UnitBase.
        // Force transit, then release it inside guard range in checkAllUnits.
        // Persist assignments in the former escort-map save slot (same layout).
        const_cast<UnitBase*>(unit)->setGuardPoint(contact);
        doSetAttackMode(unit,AREAGUARD);
        doAttackObject(unit,intruder,!unit->isInAttackRange(intruder));
        defenceAssignments[id] = intruder->getObjectID();
        ++dispatched;
    }
    if (dispatched) traceDecision("defence_response",AITelemetry::Record().set("target",intruder->getObjectID())
        .set("x",contact.x).set("y",contact.y).set("threat_value",threatValue)
        .set("already_committed_value",committed).set("required_value",SimpleArmyPolicy::responseValue(threatValue))
        .set("reason",clearingSpice ? "clear_spice_launcher" : "under_attack")
        .set("asset_emergency",assetEmergency)
        .set("response_band",emergencyBand)
        .set("candidates",int(candidates.size()))
        .set("dispatched",dispatched));
}

bool QuantBot::autonomousCustomHardBrutal() const {
    return !supportMode && currentGame!=nullptr && gameMode==GameMode::Custom
        && !isCampaignGameType(currentGame->gameType)
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal);
}

bool QuantBot::wholeArmyBaseDefence() const {
    return autonomousCustomHardBrutal();
}

bool QuantBot::attackingOwnedBuilding(const ObjectBase* contact) const {
    // Is this contact, right now, shooting at a building we own? One live
    // hostile attacker, one target that is our structure, and close enough to
    // actually be hitting it. An attacker that died, that stopped, that has
    // moved on to something else or that never had a target is not an attack on
    // our base, and must never scramble the house.
    if (!contact || contact->getHealth()<=0 || !contact->isActive() || !contact->canAttack()) return false;
    if (!contact->getOwner() || contact->getOwner()->getTeamID()==getHouse()->getTeamID()) return false;
    if (contact->getItemID()==Unit_Sandworm) return false;
    const auto* victim=contact->getTarget();
    return victim && victim->isAStructure() && victim->getOwner()==getHouse()
        && victim->getHealth()>0 && (contact->isAUnit()
            ? static_cast<const UnitBase*>(contact)->isInWeaponRange(victim)
            : blockDistance(contact->getLocation(),victim->getClosestPoint(contact->getLocation()))
                <= contact->getWeaponRange());
}

bool QuantBot::baseDefenceAssignment(const UnitBase* unit) const {
    // A scramble records the building the unit was sent to; every other defence
    // assignment records a hostile contact. The upkeep in checkAllUnits() ends
    // the scramble once the unit is neither walking there nor fighting.
    if (!unit || !wholeArmyBaseDefence()) return false;
    const auto assignment=defenceAssignments.find(unit->getObjectID());
    if (assignment==defenceAssignments.end()) return false;
    const auto* assigned=getObject(assignment->second);
    return assigned && assigned->isAStructure() && assigned->getOwner()==getHouse();
}

void QuantBot::scrambleWholeArmyToDefend(const ObjectBase* intruder, const ObjectBase* building) {
    AITelemetry::PerformanceScope perfScope("ai.scrambleWholeArmyToDefend",
        getGameCycleCount(), getHouse()->getHouseID());
    if (!intruder || intruder->getHealth()<=0 || !intruder->getOwner()
        || intruder->getOwner()->getTeamID()==getHouse()->getTeamID()
        || !building || building->getHealth()<=0) return;
    const int myTeam=getHouse()->getTeamID();
    auto distanceToBuilding=[&](Coord from) {
        return blockDistance(from,building->getClosestPoint(from)).lround();
    };
    auto atBuilding=[&](Coord from) {
        return from.isValid() && distanceToBuilding(from)<=kBaseScrambleContactTiles;
    };
    auto value=[&](const ObjectBase* object) {
        const int price=currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
        return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
    };
    // Test-only force candidates (docs/formations.md, "Concentrating force"): P counts the
    // enemy within 12 tiles and sends twice it from up to 40 tiles; H brings the raiding army
    // home when the troops nearby cannot answer; M has distant defenders muster first. X
    // (round 5) counts the enemy within 12 tiles, sends 1.5x it from the bundle first, then
    // the nearest within 40 tiles, and has the distant ones muster.
    const bool sized=forceVariant(kForceSizedDefence);
    const bool pool=forceVariant(kForcePool), recall=forceVariant(kForceRecall), muster=forceVariant(kForceMuster) || sized;
    const int threatTiles=pool || sized ? ForceConcentrationPolicy::kPoolThreatTiles : kBaseScrambleContactTiles;
    int threat=value(intruder), committed=0;
    int held=0, forcedKept=0, alreadyScrambled=0, retuned=0;
    std::vector<SimpleArmyPolicy::Responder> candidates;
    for (const auto* unit : getUnitList()) {
        if (unit->getHealth()<=0 || !unit->isActive() || !unit->getOwner()) continue;
        // The local threat: every visible enemy ground fighter at the building.
        if (unit->getOwner()->getTeamID()!=myTeam) {
            if (unit!=intruder && unit->canAttack() && !unit->isAFlyingUnit()
                && unit->isVisible(myTeam) && distanceToBuilding(unit->getLocation())<=threatTiles) threat+=value(unit);
            continue;
        }
        if (unit->getOwner()!=getHouse() || !unit->isRespondable()) continue;
        // Ground combat troops only. Workers, builders, transports, saboteurs,
        // worms and ambient traffic are not an army, and the aircraft answering
        // this house's air threats belong to the strike/rescue planner.
        if (!unit->isAGroundUnit() || !mobileCombatItem(unit->getItemID())
            || !unit->canAttack() || !unit->canAttack(intruder)) continue;
        // A human order, a trip to the repair yard and a booked carryall pickup
        // stay with their owner.
        if (unitRole(unit, {UnitRole::Human, UnitRole::Repair}) != UnitRole::Free
            || static_cast<const GroundUnit*>(unit)->isAwaitingPickup()) { ++held; continue; }
        const Uint32 id=unit->getObjectID();
        // Mustering for a building: committed to it, never re-ordered here.
        if (!musters.empty()) if (const auto* own=musterOf(id)) {
            if (own->building==building->getObjectID()) committed+=value(unit);
            continue;
        }
        // A scrambled unit is never scrambled again while it is on that job. If
        // that job is this building, it already counts towards the answer.
        if (unitRole(unit, {UnitRole::BaseDefence}) == UnitRole::BaseDefence) {
            ++alreadyScrambled;
            const auto* target=unit->getTarget();
            if (defenceAssignments.at(id)==building->getObjectID()
                || (target && atBuilding(target->getLocation()))) committed+=value(unit);
            continue;
        }
        // An anti-air rescue in progress is the air planner's contact.
        const auto assignment=defenceAssignments.find(id);
        if (assignment!=defenceAssignments.end()) {
            const auto* assigned=getObject(assignment->second);
            if (assigned && assigned->isAFlyingUnit() && airAttackContinues(unit,assigned)) {
                ++held; continue;
            }
        }
        // A unit under a forced order - a launcher's spacing step, a transit -
        // is not moved.
        if (unit->wasForced()) { ++forcedKept; continue; }
        // Already fighting at this building: it keeps its fight as a hunter and
        // counts towards the answer.
        const auto* target=unit->getTarget();
        if (target && target->getHealth()>0 && target->getOwner()
            && target->getOwner()->getTeamID()!=myTeam && !target->isAFlyingUnit()
            && atBuilding(target->getLocation())) {
            if (unit->getAttackMode()!=HUNT) { doSetAttackMode(unit,HUNT); ++retuned; }
            defenceAssignments[id]=building->getObjectID();
            groundSquad.erase(id);
            committed+=value(unit);
            continue;
        }
        candidates.push_back({id,value(unit),distanceToBuilding(unit->getLocation())});
    }
    // "Closest" is bounded: the same threat-scaled band (12-40 tiles) that
    // limits every other emergency response, so one raid cannot pull troops
    // from the far side of the map.
    const int band=pool ? kEmergencyBandMax : emergencyResponseRadius(threat);
    std::vector<Uint32> response;
    int recalled=0;
    if (sized) {
        std::vector<ForceConcentrationPolicy::SizedResponder> responders;
        for (const auto& candidate : candidates) {
            const auto* unit=static_cast<const UnitBase*>(getObject(candidate.id));
            responders.push_back({candidate.id,candidate.value,candidate.distance,standsInBundle(unit)});
        }
        response=ForceConcentrationPolicy::sizedPool(ForceConcentrationPolicy::sizedRequired(threat),committed,
            responders,ForceConcentrationPolicy::kSizedBandTiles);
        forceStats.sizedResponses+=int(response.size());
    } else if (pool || recall || muster) {
        std::vector<ForceConcentrationPolicy::Responder> responders;
        for (const auto& candidate : candidates) responders.push_back({candidate.id,candidate.value,candidate.distance});
        const int64_t required=pool ? ForceConcentrationPolicy::poolRequired(threat) : SimpleArmyPolicy::responseValue(threat);
        response=ForceConcentrationPolicy::pool(required,committed,responders,band,recall);
        for (const auto& candidate : candidates)
            if (candidate.distance>band && std::find(response.begin(),response.end(),candidate.id)!=response.end()) ++recalled;
        forceStats.pooled+=int(response.size());
        forceStats.recalled+=recalled;
    } else {
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),
            [&](const SimpleArmyPolicy::Responder& candidate) { return candidate.distance>band; }),
            candidates.end());
        // The nearest troops, until together with those already in the fight they
        // outweigh the threat at the building.
        response=SimpleArmyPolicy::reinforcements(threat,committed,candidates);
    }
    std::vector<Uint32> musterIds;
    // Defence is never slowed to a formation pace (docs/formations.md): each troop walks
    // to the building's point nearest to itself at its own speed.
    for (const auto id : response) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        const Coord at=building->getClosestPoint(unit->getLocation());
        if (muster && blockDistance(unit->getLocation(),at).lround()>ForceConcentrationPolicy::kMusterFromTiles) {
            musterIds.push_back(id);
            continue;
        }
        doHuntToward(unit,at.x,at.y);
        defenceAssignments[id]=building->getObjectID();
        groundSquad.erase(id);
    }
    if (!musterIds.empty()) startMuster(building,musterIds);
    const Coord contact=intruder->getLocation();
    if (!response.empty() || committed>0) traceDecision("base_scramble",AITelemetry::Record()
        .set("target",intruder->getObjectID()).set("building",building->getObjectID())
        .set("x",contact.x).set("y",contact.y)
        .set("threat_value",threat).set("required_value",SimpleArmyPolicy::responseValue(threat))
        .set("committed_value",committed).set("candidates",int(candidates.size()))
        .set("sent",int(response.size())).set("already_scrambled",alreadyScrambled)
        .set("forced_kept",forcedKept).set("mission_corrected",retuned).set("held",held)
        .set("posture",ArmyPosturePolicy::postureName(armyPosture)));
}

int QuantBot::emergencyResponseRadius(int threatValue) {
    // How far troops are pulled from to answer an attack on something we own.
    // A trike shooting a harvester pulls the district; a siege column at a
    // refinery pulls the army. Deterministic arithmetic on the observed local
    // threat value, clamped at both ends so it is always a bounded local
    // response rather than a map-wide recall.
    return std::clamp(kEmergencyBandMin + std::max(0,threatValue)/500,
                      kEmergencyBandMin, kEmergencyBandMax);
}

int QuantBot::houseAttackThreshold() const {
    // Campaign mode: the difficulty's threshold; Custom: the global one, except Brutal's own.
    const QuantBotConfig& config = getQuantBotConfig();
    const float percent = (gameMode == GameMode::Campaign || difficulty == Difficulty::Brutal)
        ? config.getSettings(static_cast<int>(difficulty)).attackThresholdPercent : config.attackThresholdPercent;
    const FixPoint threshold = FixPoint(static_cast<int>(percent * 100)) / 100;
    const bool vanillaCustom = gameMode == GameMode::Custom && !getHouse()->isPowerRequired();
    return vanillaCustom
        ? DuneCity::vanillaAttackThreshold((militaryValueLimit * threshold).lround(), static_cast<int>(difficulty))
        : (militaryValueLimit * threshold).lround();
}

bool QuantBot::bundleArmyReady() const {
    // The house attack threshold at the configured wave readiness, exactly as attack() applies
    // it (D's outnumbered raise aside): below it the reserve stays on the home rally.
    const int64_t required = ForceConcentrationPolicy::scaledReadiness(houseAttackThreshold(), waveReadinessPercent());
    return militaryUnitValue() >= required;
}


void QuantBot::attack(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.attack", getGameCycleCount(), getHouse()->getHouseID());
	if (supportMode) {
		attackTimer = std::numeric_limits<Sint32>::max();
		return;
	}
    if (isCampaignEnemy() && !campaignCanLaunch()) return;

    // Get config for this difficulty
    const QuantBotConfig& config = getQuantBotConfig();
    const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    if (isCampaignEnemy()) attackTimer=0;
    else {
        const bool campaignAlly = isCampaignGameType(currentGame->gameType) && isAlliedWithHuman();
        attackTimer = campaignAlly ? MILLI2CYCLES(60000)
            : SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
                currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());
        traceDecision("attack_schedule",AITelemetry::Record().set("delay_cycles",attackTimer)
            .set("base_cycles",MILLI2CYCLES(campaignAlly ? 60000 : config.attackTimerMs)));
    }

	// Check if this difficulty is allowed to attack at all
	if (!diffSettings.attackEnabled) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "difficulty_disabled"));
		logDebug("Don't attack. Difficulty %d has attackEnabled = false", static_cast<int>(difficulty));
		return;
	}

    if (!isCampaignEnemy()) tryLaunchOrnithopterStrike(diffSettings, config);

	// Main attack loop - check military strength threshold
	// Campaign mode: Use difficulty-specific threshold from config
	// Custom mode: global threshold, except Brutal uses its 25% difficulty threshold.
	float attackThresholdPercent = (gameMode == GameMode::Campaign || difficulty == Difficulty::Brutal)
		? diffSettings.attackThresholdPercent 
		: config.attackThresholdPercent;
	const bool vanillaCustom = gameMode == GameMode::Custom && !getHouse()->isPowerRequired();
	int requiredMilitary = houseAttackThreshold();
    const bool campaign = isCampaignGameType(currentGame->gameType);
    if (campaign) requiredMilitary=campaignRequiredArmy(requiredMilitary);
    // Wave size (docs/formations.md, "Concentrating force"): the house threshold scales with
    // the wave readiness (QuantBotConfig::force.waveReadinessPercent; duel labels L / Q; D
    // raises it to 100% while the house is outnumbered).
    if (const int percent=launchReadinessPercent(); percent!=100)
        requiredMilitary=int(ForceConcentrationPolicy::scaledReadiness(requiredMilitary,percent));
    if (militaryValue < requiredMilitary) {
        // Recheck readiness promptly; do not miss a short-lived strength window.
        if (campaign || (vanillaCustom && difficulty == Difficulty::Brutal))
            attackTimer = std::min(attackTimer, static_cast<int>(MILLI2CYCLES(15000)));
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "army_threshold").set("military", militaryValue).set("limit", militaryValueLimit)
            .set("required_military", requiredMilitary));
		logDebug("Don't attack. Not enough troops: house: %d  dif: %d  mStr: %d  mLim: %d (need %.1f%%)",
			getHouse()->getHouseID(), static_cast<Uint8>(difficulty), militaryValue, militaryValueLimit, attackThresholdPercent * 100.0f);
		return;
	}

	// Campaign attacks must remain possible after losing a repair yard, or on
    // scenarios where it cannot be built. Repair remains useful, not mandatory.
	if (!campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && getHouse()->getNumItems(Structure_RepairYard) == 0 && currentGame->techLevel > 4) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "repair_prerequisite"));
		logDebug("Don't attack. Wait until you have a repair yard.");
		return;
	}

    // Recovery suppresses fresh offensive dispatch. Base defence, repair,
    // production and the anti-air planner above are untouched: this only stops
    // new ground waves being sent out while the army is coming home or
    // rebuilding.
    if (recoveryActive() && ArmyPosturePolicy::suppressesOffensiveDispatch(armyPosture)) {
        attackTimer = std::min(attackTimer, static_cast<Sint32>(MILLI2CYCLES(15000)));
        traceDecision("attack_deferred", AITelemetry::Record().set("reason", "army_recovery")
            .set("posture", ArmyPosturePolicy::postureName(armyPosture))
            .set("posture_age_cycles", int(ArmyPosturePolicy::elapsed(getGameCycleCount(), postureSince))));
        return;
    }

    // Outnumbered dispatch gate. While the observed hostile front is at least
    // the configured multiple of what we can bring, a normal ground offensive
    // waits until healthy deployable military value reaches the configured share
    // of the CONFIGURED militaryValueLimit. Nothing here caps production or unit
    // counts, and the bypass is not permission to ignore an urgent withdrawal:
    // the posture check above runs first.
    if (recoveryActive()) {
        // updateArmyPosture() already surveyed this cycle on the ordinary path.
        // Recompute from live state if anything reaches here out of band, so the
        // gate never judges on a stale observation.
        if (lastSurveyCycle != getGameCycleCount()) {
            lastSurvey = surveyArmy();
            lastSurveyCycle = getGameCycleCount();
        }
        const ArmySurvey& survey = lastSurvey;
        const auto gate = offensiveDispatchGate(survey);
        const auto thresholds = postureThresholds();
        if (gate.defer) {
            attackTimer = std::min(attackTimer, static_cast<Sint32>(MILLI2CYCLES(15000)));
            traceDecision("attack_deferred", AITelemetry::Record().set("reason", "outnumbered_front")
                .set("gate_reason", gate.reason)
                .set("own_effective_power", survey.allied.offensive())
                .set("enemy_effective_power", CombatPowerPolicy::hostileGroundOnly(survey.hostileFront))
                .set("enemy_air_power", survey.hostileFront.air)
                .set("outnumbered_bps", thresholds.outnumberedBps)
                .set("configured_limit", militaryValueLimit)
                .set("bypass_threshold_value",
                     ArmyPosturePolicy::dispatchBypassValue(militaryValueLimit, thresholds))
                .set("bypass_bps", thresholds.dispatchBypassBps)
                .set("healthy_deployable_value", survey.deployableValue)
                .set("healthy_deployable_ground_value", survey.deployableGroundValue)
                .set("healthy_deployable_air_value", survey.deployableAirValue)
                .set("bypass", gate.bypass)
                .set("front_observed", survey.frontObserved));
            return;
        }
        if (gate.outnumbered) traceDecision("attack_outnumbered_bypass", AITelemetry::Record()
            .set("gate_reason", gate.reason)
            .set("own_effective_power", survey.allied.offensive())
            .set("enemy_effective_power", CombatPowerPolicy::hostileGroundOnly(survey.hostileFront))
            .set("configured_limit", militaryValueLimit)
            .set("bypass_threshold_value",
                 ArmyPosturePolicy::dispatchBypassValue(militaryValueLimit, thresholds))
            .set("healthy_deployable_value", survey.deployableValue)
            .set("healthy_deployable_ground_value", survey.deployableGroundValue)
            .set("healthy_deployable_air_value", survey.deployableAirValue));
    }

    // Garrison (G, test-only, docs/formations.md "Round 5"): while the remembered enemy army
    // outnumbers ours, no wave leaves; the reserve holds the protected rally and the enemy
    // attacks into the turrets. The next check after a won defence launches as usual.
    if (garrisoned()) {
        ++forceStats.garrisonHolds;
        attackTimer = std::min(attackTimer, static_cast<Sint32>(MILLI2CYCLES(15000)));
        traceDecision("attack_deferred", AITelemetry::Record().set("reason", "garrison")
            .set("own_value", ownGroundArmyValue()).set("enemy_estimate", enemyArmyEstimate()));
        return;
    }

    launchGroundHunt();
    if (isCampaignEnemy()) tryLaunchOrnithopterStrike(diffSettings, config);
}

bool QuantBot::managesAutonomousLauncherHunt(const UnitBase* unit) const {
    return currentGame && huntingLauncher(unit) && unitRole(unit, {UnitRole::Human}) == UnitRole::Free;
}

bool QuantBot::managesAutonomousRocketHunt(const UnitBase* unit) const {
    // A Trooper answers aircraft with its own SmallRocket over five tiles, not the
    // launcher's Rocket over nine, so the projectile is not the same one. What is the
    // same is the selection problem: the engine releases any flying target beyond
    // weapon range, so a distant Ornithopter stalls a hunting Trooper exactly as it
    // stalls a hunting launcher. They share the candidate filter for that reason and
    // nothing else - the close-range spacing and the tracked wave stay launcher-only.
    return currentGame && unitRole(unit, {UnitRole::Human}) == UnitRole::Free
        && (huntingLauncher(unit)
            || (engineHuntAttack(unit)
                && (unit->getItemID()==Unit_Trooper || unit->getItemID()==Unit_Troopers)));
}

bool QuantBot::engineHuntAttack(const UnitBase* unit) const {
    return unit && unit->getOwner()==getHouse() && unit->isAGroundUnit()
        && unit->getAttackMode()==HUNT && !supportMode && gameMode==GameMode::Custom
        && !isCampaignGameType(currentGame->gameType)
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal);
}

bool QuantBot::huntingLauncher(const UnitBase* unit) const {
    // The true launchers only. Deviators keep their established non-Hunt
    // spacing, and no other hunting attacker is pulled out of contact here.
    return engineHuntAttack(unit)
        && (unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher);
}

int QuantBot::launcherEscapeRange(const UnitBase* launcher, const ObjectBase* threat) const {
    if (!launcher || !threat || !currentGame || !threat->isAUnit()) return 0;
    const auto* hostile = static_cast<const UnitBase*>(threat);
    if (!hostile->isActive() || hostile->getHealth()<=0 || hostile->isAFlyingUnit()) return 0;
    if (!hostile->getOwner() || !launcher->getOwner()
        || hostile->getOwner()->getTeamID()==launcher->getOwner()->getTeamID()) return 0;
    // Observation only: an enemy this house cannot see does not move its units.
    if (!hostile->isVisible(launcher->getOwner()->getTeamID())) return 0;
    // Only an enemy that can actually shoot this launcher is a reason to stop
    // firing and move. A worker or a carryall closing on it is not.
    if (!hostile->canAttack(launcher)) return 0;
    const int weaponRange = currentGame->objectData.data[launcher->getItemID()]
        [getHouse()->getHouseID()].weaponrange;
    if (weaponRange <= 2) return 0;                 // Nothing to kite with.
    const int reach = std::max(1, hostile->getWeaponRange());
    // "Too close" needs both: inside the enemy's own reach and inside the inner
    // part of the launcher's band. A distant enemy therefore never pulls an
    // attacker backwards, including one that has just landed a hit.
    const int closeBand = std::min(reach + 1, weaponRange/2 + 1);
    if (blockDistance(launcher->getLocation(),hostile->getLocation()).lround() > closeBand) return 0;
    // Stand off just outside the enemy's reach and never beyond the launcher's
    // own firing range, so the gap it opens is a gap it can shoot across.
    return std::clamp(reach + 2, closeBand + 1, weaponRange);
}

const ObjectBase* QuantBot::closeThreatWhileHunting(const UnitBase* launcher, const ObjectBase* knownThreat,
                                                    int* desiredRange) const {
    if (!huntingLauncher(launcher) || !launcher->isActive() || !launcher->isRespondable()) return nullptr;
    // Everything that outranks fresh offence keeps outranking it: a human order,
    // a repair run, a carryall pickup and a live defence contact stay with their
    // owner rather than being turned into a dodge. The one exception is a
    // launcher hunting an attack on a building we own: that contact is answered
    // by hunting it, so the launcher keeps its spacing step and resumes firing
    // across the gap instead of dying at point-blank range in its own base.
    const UnitRole role = unitRole(launcher, {UnitRole::Human, UnitRole::Repair,
                                              UnitRole::BaseDefence, UnitRole::LiveDefence});
    if (role == UnitRole::Human || role == UnitRole::Repair || role == UnitRole::LiveDefence) return nullptr;
    if (static_cast<const GroundUnit*>(launcher)->isAwaitingPickup()) return nullptr;
    const ObjectBase* threat = nullptr;
    int range = 0;
    if (knownThreat) {
        // A hit is authoritative about who is shooting, but not about distance.
        range = launcherEscapeRange(launcher,knownThreat);
        if (range) threat = knownThreat;
    } else {
        FixPoint nearest = FixPt_MAX;
        auto consider = [&](const ObjectBase* candidate) {
            const int candidateRange = launcherEscapeRange(launcher,candidate);
            if (!candidateRange) return;
            const FixPoint distance = blockDistance(launcher->getLocation(),candidate->getLocation());
            if (!threat || distance < nearest
                || (distance == nearest && candidate->getObjectID() < threat->getObjectID())) {
                nearest = distance; threat = candidate; range = candidateRange;
            }
        };
        // Nobody outside this radius can qualify: the close band is at most half
        // the launcher's own range plus one. Searching the engine's tile grid
        // over that small box keeps the work per launcher constant, instead of
        // walking the whole unit list once per launcher - which in a match with
        // hundreds of units would make an ordinary pass quadratic.
        const int weaponRange = currentGame->objectData.data[launcher->getItemID()]
            [getHouse()->getHouseID()].weaponrange;
        const int radius = std::max(1, weaponRange/2 + 1);
        const Coord at = launcher->getLocation();
        if (currentGameMap) {
            // Fixed ascending tile order, so every peer examines the same
            // candidates and the distance/id tie-break alone decides.
            for (int y = at.y - radius; y <= at.y + radius; ++y)
                for (int x = at.x - radius; x <= at.x + radius; ++x) {
                    if (!currentGameMap->tileExists(x,y)) continue;
                    const Tile* tile = currentGameMap->getTile(x,y);
                    if (!tile || !tile->hasAGroundObject()) continue;
                    for (const Uint32 id : tile->getNonInfantryGroundObjectList()) consider(getObject(id));
                    for (const Uint32 id : tile->getInfantryList()) consider(getObject(id));
                }
        } else {
            // No grid to search yet: the ordinary list, same tie-break.
            for (const UnitBase* candidate : getUnitList()) consider(candidate);
        }
    }
    if (threat && desiredRange) *desiredRange = range;
    return threat;
}

bool QuantBot::escapeCloseThreatWhileHunting(const UnitBase* launcher, const ObjectBase* knownThreat) {
    int desiredRange = 0;
    const ObjectBase* threat = closeThreatWhileHunting(launcher, knownThreat, &desiredRange);
    if (!threat) return false;
    // The established spacing helper, which issues one forced step and restores
    // Hunt after it. No Area Guard, no new target, no change to the tracked
    // army: this stays a hunter that is opening a firing gap.
    const bool baseHunter=unitRole(launcher, {UnitRole::BaseDefence}) == UnitRole::BaseDefence;
    kiteAwayFromThreat(launcher,threat,desiredRange);
    // The spacing move sets a guard point; the base hunter still has no post.
    if (baseHunter) const_cast<UnitBase*>(launcher)->setGuardPoint(Coord::Invalid());
    return true;
}

int QuantBot::waveFormationShape() const {
    return waveFormationOverride != kNoWaveFormationOverride ? waveFormationOverride : getQuantBotConfig().waveFormation;
}

void QuantBot::launchGroundHunt() {
    if (supportMode) return;
    const bool limited = isCampaignEnemy();
    const bool custom = !isCampaignGameType(currentGame->gameType);
    if (limited && !campaignCanLaunch()) return;
    const auto profile = limited ? campaignProfile()
        : CampaignDifficultyPolicy::profile(static_cast<int>(difficulty),currentGame->techLevel);
    // Caps apply to this dispatch. The roster separately tracks all survivors.
    CampaignDifficultyPolicy::Pressure pressure;
    const float ratio = getQuantBotConfig().getSettings(static_cast<int>(difficulty)).attackForceMilitaryValueRatio;
    int percent = std::isfinite(ratio) ? static_cast<int>(std::clamp(ratio,0.0f,1.0f)*100.0f+0.5f) : 0;
    // Campaign roles define commitment even with older saved config defaults.
    // Keep an explicit zero as the opt-out; each house uses its own difficulty.
    if (limited && percent>0) percent=profile.enemyCommitPercent;
    int armyValue=0, committedValue=0, availableValue=0, requiredReady=0;
    // Ground pressure is tracked on its own: ornithopters and other non-ground
    // troops must not inflate a budget that can only ever buy ground units.
    int groundArmyValue=0, groundCommittedUnits=0, groundCommittedValue=0;
    // Custom Hard/Brutal dispatch is a single whole-army launch, by decision of
    // the project owner. No share of the standing army, no local assembly
    // cohort, no colony holdback and no per-unit exclusion for merely having a
    // target or a move order may hold a troop back. Hunters already in the
    // field are not reset; they are tracked with the army that joins them.
    const bool wholeArmy=!limited && !supportMode && gameMode==GameMode::Custom
        && !isCampaignGameType(currentGame->gameType)
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal);
    std::vector<Uint32> existingHunters;
    std::vector<SimpleArmyPolicy::Responder> candidates;
    // Launcher accounting for the dispatch audit. launchGroundHunt() has never
    // excluded launchers by type, so "no launchers in the waves" can only be a
    // live defence claim, a repair run, a human order or an empty wave - and
    // which of those it is was not observable from the ground_hunt record.
    int launcherCandidates=0, launchersHeldByDefence=0, launchersAlreadyHunting=0,
        launcherDispatched=0, launchersUnavailable=0;
    auto launcherItem=[](const UnitBase* unit) {
        return unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher;
    };
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner()==getHouse() && launcherItem(unit)
            && (unit->getHealth()<=0 || !unit->isActive() || !unit->isRespondable()
                || unitRole(unit, {UnitRole::Human}) != UnitRole::Free)) ++launchersUnavailable;
        if (unit->getOwner()!=getHouse() || unit->getHealth()<=0 || !unit->isActive() || !unit->isRespondable()
            || !unit->canAttack() || unit->getItemID()==Unit_Saboteur
            || unitRole(unit, {UnitRole::Human}) != UnitRole::Free
            || unit->getItemID()==Unit_Harvester || unit->getItemID()==Unit_Sandworm) continue;
        // Summoned/scripted troops can have zero purchase price. They still
        // consume combat pressure rather than being effectively free attackers.
        const int price=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
        armyValue+=price;
        if (unit->getAttackMode()==HUNT) committedValue+=price;
        const bool ground=unit->isAGroundUnit();
        if (ground) groundArmyValue+=price;
        if (wholeArmy) {
            // A hunt party's members (Blob waves) are committed while they gather, march, wait
            // for the rear or fall back, whatever attack mode that leaves them in; so are the
            // groups of a split wave walking to or waiting at their release points.
            const bool inParty=huntPartyOf(unit->getObjectID())!=nullptr
                || (!splits.empty() && splitOf(unit->getObjectID()));
            if (ground && (unit->getAttackMode()==HUNT || inParty
                || unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave)) {
                ++groundCommittedUnits; groundCommittedValue+=price;
            }
            // Aircraft keep their own planner, workers and transports cannot
            // attack, repair runs keep their unit, and a troop answering a live
            // emergency on one of our own assets - including an anti-air rescue
            // - stays on that job. Everything else is available.
            if (!ground) continue;
            const UnitRole role=unitRole(unit, {UnitRole::Repair, UnitRole::LiveDefence});
            if (role==UnitRole::Repair) {
                if (launcherItem(unit)) ++launchersUnavailable;
                continue;
            }
            if (role==UnitRole::LiveDefence) {
                if (launcherItem(unit)) ++launchersHeldByDefence;
                continue;
            }
            if (inParty || (!musters.empty() && musterOf(unit->getObjectID()))) continue;
            if (unit->getAttackMode()==HUNT) {
                existingHunters.push_back(unit->getObjectID());
                if (launcherItem(unit)) ++launchersAlreadyHunting;
                continue;
            }
            // An escort stays with its colonist until the colony stands.
            if (unitRole(unit, {UnitRole::Escort}) == UnitRole::Escort) continue;
            candidates.push_back({unit->getObjectID(),price,0});
            if (launcherItem(unit)) ++launcherCandidates;
            availableValue+=price;
            continue;
        }
        if (launcherItem(unit)) {
            if (unitRole(unit, {UnitRole::Defence}) == UnitRole::Defence) ++launchersHeldByDefence;
            else if (unit->getAttackMode()==HUNT) ++launchersAlreadyHunting;
        }
        const bool free=unit->getAttackMode()!=RETREAT && !unit->hasATarget()
            && unitRole(unit, {UnitRole::Repair, UnitRole::Defence, UnitRole::ColonyGuard,
                               UnitRole::Escort}) == UnitRole::Free;
        // Every existing hunter counts, including forced moves and busy targets.
        // Never select it again or silently drop it from the pressure budget.
        // Tracked members also count while a withdrawal or base-defence order
        // temporarily replaces Hunt. Otherwise the next pass could commit the
        // configured share a second time.
        // updateCustomWave() releases a member that has genuinely stopped
        // attacking, so this cannot accumulate phantom commitment.
        const bool trackedMember=recoveryActive() && unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave;
        if (ground && (unit->getAttackMode()==HUNT || trackedMember)) {
            ++groundCommittedUnits; groundCommittedValue+=price;
        }
        if (trackedMember) continue;
        if (!free || (custom && unit->getAttackMode()==HUNT)) continue;
        if (!limited && (!ground || unit->getItemID()==Unit_Saboteur
            || (unit->getAttackMode()==HUNT && !unit->wasForced()))) continue;
        if (limited && (scriptedAssaults.count(unit->getObjectID())
            || campaignWave.members.count(unit->getObjectID()))) continue;
        // Campaign waves recruit aircraft only on the raiding difficulties. The
        // air planner owns every other aircraft, including a defensive one.
        if (limited && unit->getItemID()==Unit_Ornithopter && !autonomousAircraftRaids()) continue;
        candidates.push_back({unit->getObjectID(),price,0});
        if (launcherItem(unit)) ++launcherCandidates;
        availableValue+=price;
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    if (limited) committedValue=campaignPressure().value;
    // Every custom difficulty commits its configured percentage of the current
    // ground army, without any additional unit count or flat value ceiling.
    const int customBudget=SimpleArmyPolicy::attackBudget(groundArmyValue,percent);
    std::vector<Uint32> selected;
    if (limited) {
        const auto& settings=getQuantBotConfig().getSettings(static_cast<int>(difficulty));
        const FixPoint threshold=FixPoint(static_cast<int>(settings.attackThresholdPercent*100))/100;
        requiredReady=campaignRequiredArmy((militaryValueLimit*threshold).lround());
        // Away, injured, repairing and busy troops cannot assemble a new wave.
        // Requiring readiness here prevents repeated checks dripping out reserves.
        if (availableValue<requiredReady) {
            traceDecision("attack_deferred",AITelemetry::Record().set("reason","wave_readiness")
                .set("available_value",availableValue).set("required_military",requiredReady)
                .set("committed_value",committedValue));
            return;
        }
        const int houseUnits=profile.limitedWave ? profile.units : INT32_MAX;
        const int houseValue=profile.limitedWave ? profile.value : INT32_MAX;
        const int budget=std::max(0,std::min(houseValue,
            SimpleArmyPolicy::attackBudget(availableValue,percent)));
        int value=0;
        for (const auto& candidate : candidates) {
            if (static_cast<int>(selected.size())>=houseUnits) break;
            if (candidate.value>budget-value || !CampaignDifficultyPolicy::fits(profile,pressure,candidate.value)) continue;
            selected.push_back(candidate.id); value+=candidate.value;
            ++pressure.units; pressure.value+=candidate.value;
        }
        // Avoid rounding a single ready Hard/Brutal unit down to no attack.
        if (selected.empty() && pressure.units==0 && percent>0 && difficulty>=Difficulty::Hard) {
            const SimpleArmyPolicy::Responder* cheapest=nullptr;
            for (const auto& candidate : candidates)
                if (CampaignDifficultyPolicy::fits(profile,pressure,candidate.value)
                    && (!cheapest || candidate.value<cheapest->value)) cheapest=&candidate;
            if (cheapest) {selected.push_back(cheapest->id);++pressure.units;pressure.value+=cheapest->value;}
        }
    } else if (isCampaignGameType(currentGame->gameType)) {
        // Shared-house helpers retain a modest home reserve, without enemy caps.
        selected=SimpleArmyPolicy::limitedAttack(armyValue,committedValue,100-profile.reservePercent,candidates);
    } else {
        // Custom Hard/Brutal: one whole-army launch. The only pre-dispatch
        // condition is that what is available is itself a main wave rather than
        // a patrol - the house readiness value plus the established six-unit,
        // three-thousand-credit minimum. That is also what stops the drip feed
        // while an army is out: whatever is produced next accumulates at home
        // until it is a full wave again, instead of trickling forward.
        // Easy and Medium keep their established small-wave behaviour exactly.
        if (wholeArmy) {
            const auto& settings=getQuantBotConfig().getSettings(static_cast<int>(difficulty));
            const FixPoint threshold=FixPoint(static_cast<int>(settings.attackThresholdPercent*100))/100;
            requiredReady=(militaryValueLimit*threshold).lround();
            // Wave size: the configured readiness percentage (duel labels L / Q: 150% / 60%; D:
            // 100% while outnumbered).
            if (const int percent=launchReadinessPercent(); percent!=100)
                requiredReady=int(ForceConcentrationPolicy::scaledReadiness(requiredReady,percent));
            int availableUnits=0;
            for (const auto& candidate : candidates) if (candidate.value>0) ++availableUnits;
            // A viable main wave, not a patrol: the established minimum is six
            // units and three thousand credits of value.
            const bool viable=QuantBotBuildPolicy::viableMainWave(availableUnits,availableValue);
            if (availableValue<requiredReady || !viable) {
                // A wave that is no longer ready stops waiting for the bundle: its next
                // readiness starts a fresh 60 s wait rather than inheriting an old one.
                bundleGateSince=0;
                traceDecision("attack_deferred",AITelemetry::Record().set("reason","wave_readiness")
                    .set("available_value",availableValue).set("available_units",availableUnits)
                    .set("required_military",requiredReady).set("viable_main_wave",viable)
                    .set("committed_value",groundCommittedValue)
                    .set("committed_units",groundCommittedUnits)
                    .set("posture",ArmyPosturePolicy::postureName(armyPosture)));
                return;
            }
            // Strength-aware waves (A, test-only, docs/formations.md "Round 5"): a ready wave
            // leaves only when it is worth forceStrengthBps of the remembered enemy army; until
            // then it keeps bundling.
            if (forceVariant(kForceStrengthWaves)) {
                std::vector<Uint32> ids;
                ids.reserve(candidates.size());
                for (const auto& candidate : candidates) ids.push_back(candidate.id);
                const int64_t wave=groundValueOf(ids), enemy=enemyArmyEstimate();
                if (!ForceConcentrationPolicy::strongEnough(wave,enemy,forceStrengthBps)) {
                    ++forceStats.strengthHolds;
                    attackTimer=std::min(attackTimer,static_cast<Sint32>(MILLI2CYCLES(15000)));
                    traceDecision("attack_deferred",AITelemetry::Record().set("reason","wave_strength")
                        .set("wave_value",wave).set("enemy_estimate",enemy).set("ratio_bps",forceStrengthBps));
                    return;
                }
            }
            // Launch from the bundle (W, on by default): a ready wave leaves once most of it
            // stands in the bundle - only while there is one and bundling owns the reserve.
            if (forceVariant(kForceBundle) && forceVariant(kForceBundleGate) && !reserveHeldHome()
                && (bundlePoint.isValid() || bundleCentre.isValid())) {
                std::vector<Uint32> ids;
                ids.reserve(candidates.size());
                for (const auto& candidate : candidates) ids.push_back(candidate.id);
                const int share=bundledShareBps(ids,ForceConcentrationPolicy::bundleRadius(int(ids.size()))+2);
                const Uint32 now=getGameCycleCount();
                if (bundleGateSince==0) bundleGateSince=std::max<Uint32>(1,now);
                const bool late=ArmyPosturePolicy::elapsed(now,bundleGateSince)
                    >=Uint32(MILLI2CYCLES(ForceConcentrationPolicy::kBundleGateMaxMs));
                if (share<ForceConcentrationPolicy::kBundleGateBps && !late) {
                    ++forceStats.gateHolds;
                    attackTimer=std::min(attackTimer,static_cast<Sint32>(MILLI2CYCLES(ForceConcentrationPolicy::kBundleRecheckMs)));
                    traceDecision("attack_deferred",AITelemetry::Record().set("reason","bundle_gate")
                        .set("bundled_bps",share).set("available_value",availableValue));
                    return;
                }
                if (share<ForceConcentrationPolicy::kBundleGateBps) ++forceStats.gateTimeouts;
                bundleGateSince=0;
            }
            // Everything available, wherever it stands. Candidate order is the
            // id order the stable sort above produced, so every peer builds the
            // identical dispatch list.
            selected.reserve(candidates.size());
            for (const auto& candidate : candidates) selected.push_back(candidate.id);
            // Home reserve (R, test-only): while the remembered enemy army exceeds ours, the
            // share it exceeds it by stays home - the units nearest the protected rally.
            if (forceVariant(kForceReserve)) {
                const auto kept=homeReserve(selected);
                if (!kept.empty()) {
                    int keptValue=0, leftUnits=0, leftValue=0;
                    for (const auto& candidate : candidates) {
                        if (std::find(kept.begin(),kept.end(),candidate.id)!=kept.end()) keptValue+=candidate.value;
                        else { ++leftUnits; leftValue+=candidate.value; }
                    }
                    if (!QuantBotBuildPolicy::viableMainWave(leftUnits,leftValue)) {
                        attackTimer=std::min(attackTimer,static_cast<Sint32>(MILLI2CYCLES(15000)));
                        traceDecision("attack_deferred",AITelemetry::Record().set("reason","home_reserve")
                            .set("kept_value",keptValue).set("left_value",leftValue));
                        return;
                    }
                    selected.erase(std::remove_if(selected.begin(),selected.end(),[&](Uint32 id) {
                        return std::find(kept.begin(),kept.end(),id)!=kept.end();
                    }),selected.end());
                    forceStats.reserveKept+=int(kept.size());
                }
            }
        }
        // A custom wave commits the configured share of the ground army, minus
        // everything already out there. Survivors keep their place in the share,
        // so repeated passes reinforce a wave instead of stacking new ones.
        if (!wholeArmy) selected=SimpleArmyPolicy::customAttack(groundArmyValue,groundCommittedValue,
            percent,candidates);
    }
    int count=0,value=0;
    if (limited && !selected.empty()) {
        campaignWave.launched=getGameCycleCount();
        campaignWave.lastActive=getGameCycleCount();
        campaignWave.members.insert(selected.begin(),selected.end());
    }
    // Custom Hard/Brutal tracks which troops it has committed, so a withdrawal
    // can recall them and so they are distinguishable from the reserve held at
    // home. Tracking is accounting only: the attackers themselves hunt, and
    // this controller issues no movement or target orders to them while the
    // house is offensive. The engine's own target acquisition is what an
    // attacking Dune army uses, and overriding it per unit was both micro and
    // the thing that walked a dispatched wave back into its own base.
    const bool trackedCustomWave=(wholeArmy || recoveryActive()) && !limited && !selected.empty();
    const ObjectBase* observedFront=nullptr;
    if (trackedCustomWave) {
        if (recoveryActive() && ArmyPosturePolicy::suppressesOffensiveDispatch(armyPosture)) return;
        // The objective is used as an OBSERVATION anchor for the front strength
        // comparison below, and is recorded for telemetry and the same anchor
        // after a load. Idle committed launchers may rejoin this front after
        // engine Hunt ends; ordinary hunters retain their own target selection.
        const UnitBase* lead=nullptr;
        for (const auto id : selected) {
            const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
            if (unit && (!lead || (lead->isInfantry() && !unit->isInfantry()))) lead=unit;
        }
        observedFront=customWaveObjective(lead);
        const ArmySurvey planned=surveyArmy(observedFront);
        const auto gate=offensiveDispatchGate(planned);
        if (recoveryActive() && gate.defer) {
            attackTimer=std::min(attackTimer,static_cast<Sint32>(MILLI2CYCLES(15000)));
            traceDecision("attack_deferred",AITelemetry::Record().set("reason","outnumbered_planned_front")
                .set("own_effective_power",planned.allied.offensive())
                .set("enemy_effective_power",planned.hostileFront.offensive())
                .set("healthy_deployable_value",planned.deployableValue)
                .set("configured_limit",militaryValueLimit)
                .set("bypass_threshold_value",ArmyPosturePolicy::dispatchBypassValue(militaryValueLimit,postureThresholds()))
                .set("bypass",gate.bypass));
            return;
        }
        customWave.launched=getGameCycleCount();
        customWave.lastActive=getGameCycleCount();
        customWave.members.insert(selected.begin(),selected.end());
        // Troops already hunting join the same tracked army. They keep their
        // engine Hunt - nothing resets a unit that is already fighting - but a
        // withdrawal, the commitment accounting and the sortie clock now see
        // one army rather than a dispatched wave plus untracked stragglers.
        customWave.members.insert(existingHunters.begin(),existingHunters.end());
        customWave.front=observedFront ? observedFront->getObjectID() : NONE_ID;
    }
    // Wave formations (docs/formations.md, QuantBot waves): who goes, when and against
    // what is decided exactly as without them; only how the ground members travel
    // changes. With WaveFormation=None every unit gets the per-unit order below. Blob is not
    // a shape: a Custom Hard/Brutal wave becomes a hunt party (startHuntParty, "Blob waves"),
    // every other wave moves as None.
    const int waveShape=QuantBotConfig::formationShapeOf(waveFormationShape());
    const bool formationWave=waveShape!=QuantBotConfig::kWaveFormationNone;
    const bool partyWave=trackedCustomWave && wholeArmy && huntParties();
    // Test-only two-angle attack (S / T): the ground members split in two groups.
    const bool splitWave=trackedCustomWave && wholeArmy && !partyWave && observedFront
        && (forceVariant(kForceSplit) || forceVariant(kForceFlank));
    std::vector<const UnitBase*> splitUnits;
    std::vector<const UnitBase*> partyUnits;
    // The Hard/Brutal campaign flank detour: a point beside the half-way mark, 8 tiles
    // off the line from 'start' to the objective; invalid when the trip is short or the
    // point cannot be stood on.
    auto flankApproach=[&](Coord start, const ObjectBase* objective, const UnitBase* walker) {
        const Coord end=objective->getLocation();
        const int dx=end.x-start.x, dy=end.y-start.y;
        const int length=std::max({1,std::abs(dx),std::abs(dy)});
        const int side=difficulty==Difficulty::Brutal && getHouse()->getHouseID()%2 ? -1 : 1;
        const Coord approach((start.x+end.x)/2-side*dy*8/length,
                             (start.y+end.y)/2+side*dx*8/length);
        if (length>12 && getMap().tileExists(approach) && walker->canPass(approach.x,approach.y)) return approach;
        return Coord::Invalid();
    };
    std::vector<const UnitBase*> hunters;                      // engine-Hunt ground members
    std::map<Uint32,std::vector<const UnitBase*>> assaults;    // campaign objective -> its attackers
    std::map<Uint32,std::vector<const UnitBase*>> flankers;    // campaign objective -> its flankers
    const ObjectBase* front=nullptr;
    for (auto id : selected) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        doSetAttackMode(unit,GUARD);
        if (limited && unit->isAGroundUnit()) {
            if (!front || difficulty>=Difficulty::Hard) front=campaignObjective(unit,count%2);
            if (count==0) campaignWave.front=front ? front->getObjectID() : NONE_ID;
            if (front) {
                doSetAttackMode(unit,AREAGUARD);
                const bool flank=difficulty>=Difficulty::Hard && count%2;
                if (formationWave) {
                    (flank ? flankers : assaults)[front->getObjectID()].push_back(unit);
                } else {
                    const Coord approach=flank ? flankApproach(unit->getLocation(),front,unit) : Coord::Invalid();
                    if (approach.isValid()) doMove2Pos(unit,approach.x,approach.y,true);
                    else doAttackObject(unit,front,true);
                }
            }
            else if (formationWave) hunters.push_back(unit);
            else doSetAttackMode(unit,HUNT);
        } else if (unit->isAGroundUnit()) {
            if (formationWave) hunters.push_back(unit);
            else if (partyWave) partyUnits.push_back(unit);
            else if (splitWave) splitUnits.push_back(unit);
            else doSetAttackMode(unit,HUNT);
        }
        if (launcherItem(unit)) ++launcherDispatched;
        ++count; value+=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
    }
    int formationMembers=0;
    if (formationWave) {
        // The members that can march in a formation, and their integer centroid.
        auto marching=[](const std::vector<const UnitBase*>& units, Coord& centre) {
            int n=0, x=0, y=0;
            for (const auto* unit : units) if (FormationOrders::eligible(unit)) {
                ++n; x+=unit->getLocation().x; y+=unit->getLocation().y;
            }
            if (n>0) centre=Coord(x/n,y/n);
            return n;
        };
        if (!hunters.empty()) {
            // A Hunt wave hunts its way to the objective the wave was given: the tracked
            // wave's front, else the same observed enemy base choice, else the target engine
            // Hunt itself picks for the lead. The first contact breaks it into engine Hunt.
            Coord centre;
            const int n=marching(hunters,centre);
            const ObjectBase* objective=nullptr;
            if (n>=FormationOrders::kMinAIGroup) {
                const UnitBase* lead=nullptr;
                for (const auto* unit : hunters)
                    if (!lead || (lead->isInfantry() && !unit->isInfantry())) lead=unit;
                objective=observedFront ? observedFront : customWaveObjective(lead);
                if (!objective) objective=lead->findClosestTarget();
            }
            if (objective) {
                const Coord goal=objective->getClosestPoint(centre);
                doFormationHuntToward(hunters,goal.x,goal.y,waveShape);
                formationMembers+=n;
            } else {
                for (const auto* unit : hunters) doSetAttackMode(unit,HUNT);
            }
        }
        for (auto& [id,units] : flankers) {
            // The flank detour is one formation leg from the group's centre; once there the
            // members are idle and campaignControlsUnit() sends them at the objective.
            const auto* objective=getObject(id);
            Coord centre;
            const int n=marching(units,centre);
            const Coord approach=n>=FormationOrders::kMinAIGroup ? flankApproach(centre,objective,units.front())
                                                                  : Coord::Invalid();
            if (approach.isValid()) {
                doFormationMove(units,approach.x,approach.y,true,waveShape);
                formationMembers+=n;
                continue;
            }
            for (const auto* unit : units) {
                const Coord own=flankApproach(unit->getLocation(),objective,unit);
                if (own.isValid()) doMove2Pos(unit,own.x,own.y,true);
                else doAttackObject(unit,objective,true);
            }
        }
        for (auto& [id,units] : assaults) {
            const auto* objective=getObject(id);
            Coord centre;
            const int n=marching(units,centre);
            if (n>=FormationOrders::kMinAIGroup) {
                const Coord goal=objective->getClosestPoint(centre);
                doFormationHuntToward(units,goal.x,goal.y,waveShape);
                formationMembers+=n;
            } else {
                for (const auto* unit : units) doAttackObject(unit,objective,true);
            }
        }
    }
    if (!partyUnits.empty()) startHuntParty(partyUnits,observedFront);
    if (!splitUnits.empty() && !startSplit(splitUnits,observedFront))
        for (const auto* unit : splitUnits) doSetAttackMode(unit,HUNT);
    if (count==0) return; // An empty house checking readiness is not an attack.
    if (limited && profile.limitedWave) {
        attackTimer=MILLI2CYCLES(CampaignDifficultyPolicy::repeatDelayMs(
            static_cast<int>(difficulty),
            getGameInitSettings().getRandomSeed(),getGameCycleCount(),getHouse()->getHouseID()));
        traceDecision("attack_schedule",AITelemetry::Record().set("delay_cycles",attackTimer)
            .set("reason","next_wave_after_dispatch"));
    }
    traceDecision("ground_hunt",AITelemetry::Record().set("members",count).set("value",value)
        .set("wave_formation",QuantBotConfig::waveFormationName(waveFormationShape()))
        .set("hunt_party",int(partyUnits.size()))
        .set("formation_members",formationMembers)
        .set("campaign_limited",limited).set("army_value",armyValue)
        .set("committed_value",custom ? groundCommittedValue : committedValue)
        .set("committed_units",limited ? campaignPressure().units : groundCommittedUnits)
        .set("ground_army_value",groundArmyValue)
        .set("pressure_units",limited ? campaignPressure().units : groundCommittedUnits+count)
        .set("pressure_value",limited ? campaignPressure().value : groundCommittedValue+value)
        // Custom attacks have no unit or value ceiling any more; the fields stay
        // in the record at their existing "no cap" sentinel for audit tooling.
        .set("attack_unit_cap",-1)
        .set("attack_value_cap",-1)
        .set("available_value",availableValue).set("required_ready",requiredReady)
        // A whole-army dispatch has no budget at all: the sentinel says so
        // rather than reporting a share that nothing consults.
        .set("whole_army",wholeArmy)
        // Launcher inclusion, straight from this dispatch rather than inferred
        // from the aggregate member count. launchers_dispatched counts the
        // launchers this wave actually ordered to Hunt; the other three say
        // where the rest went.
        .set("launchers_dispatched",launcherDispatched)
        .set("launchers_available",launcherCandidates)
        .set("launchers_held_by_defence",launchersHeldByDefence)
        .set("launchers_already_hunting",launchersAlreadyHunting)
        .set("launchers_unavailable",launchersUnavailable)
        .set("existing_hunters",int(existingHunters.size()))
        .set("attack_percent",wholeArmy ? 100 : percent)
        .set("attack_budget",wholeArmy ? -1 : (limited ? SimpleArmyPolicy::attackBudget(availableValue,percent) : (custom ? customBudget : SimpleArmyPolicy::attackBudget(armyValue,100-profile.reservePercent))))
        .set("active_units",limited ? campaignPressure().units : count)
        .set("active_value",limited ? campaignPressure().value : value)
        .set("alliance_units",pressure.units).set("alliance_value",pressure.value)
        .set("alliance_unit_cap",profile.limitedWave ? profile.units : -1)
        .set("alliance_value_cap",profile.limitedWave ? profile.value : -1)
        .set("alliance_house_cap",profile.houses));
}

// ===========================================================================
//  Army posture: recovery, cohesion and the outnumbered dispatch gate.
//
//  Restricted to Custom Hard/Brutal. Campaign pacing and roles, human-allied
//  helpers, support mode and the established Easy/Medium small-wave home
//  reserve are deliberately untouched; recoveryActive() is the only gate and
//  every call site consults it.
// ===========================================================================

bool QuantBot::recoveryActive() const {
    if (supportMode || gameMode != GameMode::Custom) return false;
    if (currentGame == nullptr || isCampaignGameType(currentGame->gameType)) return false;
    if (difficulty != Difficulty::Hard && difficulty != Difficulty::Brutal) return false;
    return getQuantBotConfig().recovery.enabled;
}

ArmyPosturePolicy::Thresholds QuantBot::postureThresholds() const {
    const auto& settings = getQuantBotConfig().recovery;
    ArmyPosturePolicy::Thresholds t;
    t.windowCycles = static_cast<Uint32>(std::max(1, MILLI2CYCLES(std::max(1000, settings.attritionWindowMs))));
    t.sampleStrideCycles = std::max<Uint32>(1, t.windowCycles / 8);
    t.lossShareBps = settings.lossShareBps;
    t.tradeShareBps = settings.tradeShareBps;
    t.lossFloor = std::max(0, settings.lossFloorCredits);
    t.localWithdrawBps = settings.localWithdrawBps;
    t.localSevereBps = settings.localSevereBps;
    t.localPersistCycles = static_cast<Uint32>(std::max(0, MILLI2CYCLES(std::max(0, settings.localPersistMs))));
    t.resumeAssembledBps = settings.resumeAssembledBps;
    t.resumeAdvantageBps = settings.resumeAdvantageBps;
    t.stabiliseCycles = static_cast<Uint32>(std::max(0, MILLI2CYCLES(std::max(0, settings.stabiliseMs))));
    t.minWithdrawCycles = static_cast<Uint32>(std::max(0, MILLI2CYCLES(std::max(0, settings.minWithdrawMs))));
    t.maxWithdrawCycles = static_cast<Uint32>(std::max(0, MILLI2CYCLES(std::max(0, settings.maxWithdrawMs))));
    t.maxRecoverCycles = static_cast<Uint32>(std::max(0, MILLI2CYCLES(std::max(0, settings.maxRecoverMs))));
    t.outnumberedBps = settings.outnumberedBps;
    t.dispatchBypassBps = settings.dispatchBypassBps;
    return t;
}

bool QuantBot::activeDefenceAssignment(const UnitBase* unit) const {
    if (!unit) return false;
    const auto assignment=defenceAssignments.find(unit->getObjectID());
    if (assignment==defenceAssignments.end()) return false;
    // A unit scrambled to one of our buildings is on that job until the upkeep
    // in checkAllUnits() releases it; the recall and the ordinary rally must
    // not claim it on the way.
    if (baseDefenceAssignment(unit)) return true;
    const auto* target=getObject(assignment->second);
    if (!target || target->getHealth()<=0 || !target->isActive() || !target->getOwner()
        || target->getOwner()->getTeamID()==getHouse()->getTeamID()) return false;
    // Use the same contact lifetime as the defence scan, including ended air
    // attacks and ground contacts that have left their anchored district.
    return target->isAFlyingUnit() ? airAttackContinues(unit,target)
        : campaignDefensiveContact(unit,target);
}

bool QuantBot::orderableCombatUnit(const UnitBase* unit) const {
    if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0) return false;
    // Inactive means inside a carryall or a repair/refinery bay. Those units are
    // neither deployable strength nor available for an order.
    if (!unit->isActive() || !unit->isRespondable()) return false;
    if (!mobileCombatItem(unit->getItemID()) || !unit->canAttack()) return false;
    return unitRole(unit, {UnitRole::Human}) == UnitRole::Free;
}

QuantBot::ArmySurvey QuantBot::surveyArmy(const ObjectBase* plannedFront) {
    ArmySurvey survey;
    if (currentGame == nullptr) return survey;
    const auto& data = currentGame->objectData.data;
    const int myTeam = getHouse()->getTeamID();

    // Front anchor: the shared wave objective if we still see it, otherwise the
    // nearest enemy base this house has actually observed. refreshTacticalDanger
    // maintains visibleEnemyBases from isVisible() checks only, on its own
    // two-second cadence, so no extra scan and no omniscience.
    if (const auto* objective = plannedFront ? plannedFront : getObject(customWave.front)) {
        if (objective->getOwner() && objective->getOwner()->getTeamID() != myTeam
            && objective->isVisible(myTeam) && objective->getHealth() > 0)
            survey.frontAnchor = objective->getLocation();
    }
    if (survey.frontAnchor.isInvalid()) {
        const UnitBase* lead=nullptr;
        for (const auto* unit : getUnitList()) {
            if (!orderableCombatUnit(unit) || !unit->isAGroundUnit()
                || unitRole(unit, {UnitRole::Repair}) != UnitRole::Free || unit->hasATarget()) continue;
            if (!lead || (lead->isInfantry() && !unit->isInfantry())) lead=unit;
        }
        if (const auto* objective=customWaveObjective(lead))
            survey.frontAnchor=objective->getLocation();
    }
    if (survey.frontAnchor.isInvalid()) {
        const Coord base=protectedRally.isValid() ? protectedRally : findBaseCentre(getHouse()->getHouseID());
        int closest = std::numeric_limits<int>::max();
        // A visible army is sufficient intelligence even when its base is
        // still hidden. Never require a building sighting to notice that force.
        for (const auto* unit : getUnitList()) {
            if (!unit->getOwner() || unit->getOwner()->getTeamID()==myTeam
                || !unit->isActive() || unit->getHealth()<=0 || !unit->canAttack()
                || !mobileCombatItem(unit->getItemID()) || !unit->isVisible(myTeam)) continue;
            const int d=base.isValid() ? blockDistance(base,unit->getLocation()).lround() : 0;
            if (d<closest) { closest=d; survey.frontAnchor=unit->getLocation(); }
        }
    }
    survey.frontObserved = survey.frontAnchor.isValid();

    // How far from the anchor counts as "this front". Provisional, and only used
    // to scope an observation, never to decide a battle.
    constexpr int kFrontRadius = 32;
    constexpr int kSquadRadius = 14;
    // Local corroboration radius for an attack on a core asset.
    constexpr int kCoreRadius = 12;

    // The assembly point and radius have to be the ones the orders actually
    // use, or "assembled" would be measured against a different area than the
    // one the units are being sent to and the house could never report itself
    // ready. armyAssemblyAnchor() is that single source: the exterior staging
    // slot while offensive, the protected shelter while coming home.
    const Coord assembly = armyAssemblyAnchor();
    const int rallyRadius = armyAssemblyRadius();

    Coord squadCentre = Coord::Invalid();
    int squadCount = 0, squadX = 0, squadY = 0;
    for (const auto id : customWave.members) {
        const auto* member = dynamic_cast<const UnitBase*>(getObject(id));
        if (!orderableCombatUnit(member)) continue;
        squadX += member->getX(); squadY += member->getY(); ++squadCount;
    }
    if (squadCount) squadCentre = Coord(squadX / squadCount, squadY / squadCount);
    survey.waveMembers = squadCount;

    // Core assets of ours that an observed hostile unit is actually shooting at,
    // each with its OWN threat and its own local defence. Collected first so the
    // hostile sweep below can measure the force on them instead of treating one
    // raider in weapon range as a house emergency.
    //
    // Per asset, not summed over the house: a single total lets the garrison of
    // a strong main base cover for a raid on an undefended forward colony, and
    // conversely lets a raid on the main base look overwhelming because a
    // distant colony contributes nothing. Both were wrong in opposite
    // directions. The emergency is local, so the measurement is local.
    struct AttackedCore { Coord at; int64_t threat = 0; int64_t holding = 0; bool wounded = false; };
    std::vector<AttackedCore> attackedCores;
    auto coreEntry = [&](Coord at) -> AttackedCore& {
        for (auto& core : attackedCores) if (core.at == at) return core;
        attackedCores.push_back({at, 0, 0, false});
        return attackedCores.back();
    };

    for (const auto* unit : getUnitList()) {
        if (!unit || !unit->getOwner() || unit->getHealth() <= 0) continue;
        const Uint32 item = unit->getItemID();
        const bool combat = mobileCombatItem(item) && unit->canAttack();
        const bool flying = unit->isAFlyingUnit();
        const bool hostile = unit->getOwner()->getTeamID() != myTeam;
        const int price = data[item][unit->getOriginalHouseID()].price;
        const int64_t power = CombatPowerPolicy::unitPower(price, unit->getHealth().lround(),
                                                           unit->getMaxHealth());
        // Real engine capability, not a weapon-range guess: see
        // CombatPowerPolicy::airCapableItem for the overrides it mirrors.
        const bool canHitAir = CombatPowerPolicy::airCapableItem(int(item));

        if (hostile) {
            // Observation only: active, visible to our team, able to fight, and
            // never a neutral worm or ambient aircraft.
            if (!combat || !unit->isActive() || !unit->isVisible(myTeam)) continue;
            const Coord at = unit->getLocation();
            if (survey.frontObserved && blockDistance(survey.frontAnchor, at).lround() <= kFrontRadius) {
                if (flying) survey.hostileFront.addAir(power, canHitAir);
                else survey.hostileFront.addGround(power, canHitAir);
            }
            if (squadCentre.isValid() && blockDistance(squadCentre, at).lround() <= kSquadRadius) {
                if (flying) survey.squadHostile.addAir(power, canHitAir);
                else survey.squadHostile.addGround(power, canHitAir);
            }
            // An attack on a production/economy core asset is a candidate house
            // emergency, but only a candidate: the raid still has to outweigh
            // what is defending that asset, or seriously have damaged it. One
            // trike shooting a refinery must not recall the whole army.
            const auto* victim = unit->getTarget();
            if (victim && victim->isAStructure() && victim->getOwner() == getHouse()
                && RocketTurretPolicy::coreAsset(victim->getItemID())
                && unit->isInWeaponRange(victim)) {
                auto& core = coreEntry(victim->getLocation());
                core.threat += power;
                if (victim->getHealth() * 2 <= victim->getMaxHealth()) core.wounded = true;
            }
            continue;
        }

        if (!combat) continue;
        const bool allied = unit->getOwner() != getHouse();
        if (allied) {
            // An ally's troops are not ours to order. They count as strength
            // only where they actually are: near the chosen front, or beside our
            // own main cohort. A friendly army sitting at its own base on the
            // far side of the map must not bypass the outnumbered veto.
            if (!unit->isActive()) continue;
            const Coord at = unit->getLocation();
            const bool nearFront = survey.frontObserved
                && blockDistance(survey.frontAnchor, at).lround() <= kFrontRadius;
            const bool nearCohort = squadCentre.isValid()
                && blockDistance(squadCentre, at).lround() <= kSquadRadius;
            if (!nearFront && !nearCohort) continue;
            if (flying) survey.allied.addAir(power, canHitAir);
            else survey.allied.addGround(power, canHitAir);
            if (nearCohort) {
                if (flying) survey.squadSupport.addAir(power, canHitAir);
                else survey.squadSupport.addGround(power, canHitAir);
            }
            continue;
        }
        if (!orderableCombatUnit(unit)) continue;
        // A unit held back for repair is not deployable strength.
        if (unitRole(unit, {UnitRole::Repair}) != UnitRole::Free) continue;
        if (flying) { survey.deployable.addAir(power, canHitAir); survey.allied.addAir(power, canHitAir); }
        else { survey.deployable.addGround(power, canHitAir); survey.allied.addGround(power, canHitAir); }
        // Credit value over the same combat population the configured
        // militaryValueLimit covers: armed aircraft included, actual price, HP
        // weighted. The ground and air parts are also kept apart so the ground
        // power comparisons are never contaminated by an air wing.
        const int64_t value = CombatPowerPolicy::unitValue(price, unit->getHealth().lround(),
                                                           unit->getMaxHealth());
        survey.deployableValue += value;
        if (flying) survey.deployableAirValue += value;
        else survey.deployableGroundValue += value;
        if (!flying) {
            survey.designatedPower += power;
            if (assembly.isValid() && blockDistance(assembly, unit->getLocation()).lround() <= rallyRadius
                && offensiveAssemblySlot(unit->getLocation()))
                survey.assembledPower += power;
        }
        if (unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave) {
            if (flying) survey.squad.addAir(power, canHitAir);
            else {
                survey.squad.addGround(power, canHitAir);
                if (assembly.isValid()
                    && blockDistance(assembly, unit->getLocation()).lround() <= rallyRadius)
                    survey.assembledWavePower += power;
            }
            if (squadCentre.isValid()
                && blockDistance(squadCentre, unit->getLocation()).lround() <= kSquadRadius) {
                if (flying) survey.squadSupport.addAir(power, canHitAir);
                else {
                    survey.squadSupport.addGround(power, canHitAir);
                    survey.localWavePower += power;
                }
            }
        } else if (squadCentre.isValid()
            && blockDistance(squadCentre, unit->getLocation()).lround() <= kSquadRadius) {
            // Our own non-wave troops standing with the squad are part of the
            // strength it can actually call on.
            if (flying) survey.squadSupport.addAir(power, canHitAir);
            else survey.squadSupport.addGround(power, canHitAir);
        }
    }

    // Static defence. Emplacements covering the assembly point hold that ground;
    // emplacements covering an attacked core asset are what decides whether a
    // raid on it is an emergency or an ordinary nuisance. Defensive only: never
    // part of the offensive strength the dispatch gate compares.
    {
        const int radius = std::max(1, data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
            const Uint32 item = structure->getItemID();
            if (item != Structure_RocketTurret && item != Structure_GunTurret) continue;
            const int64_t turret = CombatPowerPolicy::turretPower(
                data[item][getHouse()->getHouseID()].price,
                structure->getHealth().lround(), structure->getMaxHealth());
            const bool answersAir = CombatPowerPolicy::airCapableItem(int(item));
            if (assembly.isValid()
                && blockDistance(structure->getLocation(), assembly).lround() <= radius)
                survey.deployable.addDefence(turret, answersAir);
            if (squadCentre.isValid()
                && blockDistance(structure->getLocation(), squadCentre).lround() <= radius)
                survey.squadSupport.addDefence(turret, answersAir);
            // Cover is credited to every attacked asset it actually reaches,
            // and only to those: a turret at another colony defends nothing
            // here.
            for (auto& core : attackedCores)
                if (blockDistance(structure->getLocation(), core.at).lround() <= radius)
                    core.holding += turret;
        }
    }
    // Our own mobile troops standing near an attacked core asset are the rest of
    // its local holding strength - again per asset, so troops massed at one
    // colony never count as the defence of another.
    if (!attackedCores.empty()) {
        for (const auto* unit : getUnitList()) {
            if (!orderableCombatUnit(unit) || unitRole(unit, {UnitRole::Repair}) != UnitRole::Free) continue;
            const int price = data[unit->getItemID()][unit->getOriginalHouseID()].price;
            const int64_t power = CombatPowerPolicy::unitPower(price,
                unit->getHealth().lround(), unit->getMaxHealth());
            for (auto& core : attackedCores)
                if (blockDistance(unit->getLocation(), core.at).lround() <= kCoreRadius)
                    core.holding += power;
        }
        // The emergency: a material raid on ONE asset that outweighs what is
        // actually holding THAT asset, at half the bar when the asset is already
        // half destroyed. Anything less is handled by the ordinary scramble,
        // which is untouched.
        //
        // "Already half destroyed" is deliberately not an emergency on its own:
        // a damaged building stays damaged, so one raider in range of it would
        // otherwise re-declare the emergency on every single evaluation, and a
        // mature Brutal house could never finish an offensive. That is the
        // observed 1.0.803 regression: house 1 recalled a 137-unit wave for a
        // 859k raid on an asset held by 10.06M of its own defence.
        const int64_t floorPower =
            std::max<int64_t>(1, postureThresholds().lossFloor) * CombatPowerPolicy::kPowerScale;
        // Report the asset that actually decided - the first triggering one, or
        // else the worst local margin - so the telemetry names a real place
        // instead of a house-wide total that describes none of them.
        const AttackedCore* worst = nullptr;
        for (const auto& core : attackedCores) {
            const bool emergency =
                ArmyPosturePolicy::coreEmergency(core.threat, core.holding, core.wounded, floorPower);
            if (emergency && !survey.coreUnderAttack) { survey.coreUnderAttack = true; worst = &core; }
            if (!survey.coreUnderAttack
                && (!worst || core.threat - core.holding > worst->threat - worst->holding)) worst = &core;
        }
        if (worst) {
            survey.coreThreatPower = worst->threat;
            survey.coreHoldingPower = worst->holding;
            survey.coreSeriouslyDamaged = worst->wounded;
            survey.coreAt = worst->at;
        }
    }
    return survey;
}

void QuantBot::updateArmyPosture() {
    if (!recoveryActive()) {
        // A loaded save, a changed difficulty or a helper role must not leave a
        // house frozen in a posture it no longer runs.
        if (armyPosture != ArmyPosturePolicy::Posture::Offensive) {
            armyPosture = ArmyPosturePolicy::Posture::Offensive;
            postureSince = getGameCycleCount();
        }
        customWave.members.clear();
        return;
    }
    AITelemetry::PerformanceScope scope("ai.armyPosture", getGameCycleCount(), getHouse()->getHouseID());
    const Uint32 now = getGameCycleCount();
    const auto thresholds = postureThresholds();
    refreshTacticalDanger();
    // A save from before the posture existed, or a difficulty change into
    // Hard/Brutal, leaves autonomous attackers that no recall would ever touch.
    adoptLegacyHuntersIntoWave();
    updateCustomWave();
    protectedRally = findProtectedRallyLocation();

    ArmySurvey survey = surveyArmy();

    // One baseline per stride. The deployable cost stored with it is what the
    // window share is measured against, so the comparison is always against the
    // force that was actually in the field when the window opened.
    attrition.sample(now, survey.deployableValue, thresholds.sampleStrideCycles);

    ArmyPosturePolicy::Situation situation;
    situation.cycle = now;
    if (const auto* start = attrition.windowStart(now, thresholds.windowCycles)) {
        situation.windowCovered = true;
        situation.windowStartDeployable = start->deployable;
        situation.windowLoss = std::max<int64_t>(0, attrition.lostCost - start->lostCost);
        situation.windowKill = std::max<int64_t>(0, attrition.killCost - start->killCost);
        // Readiness trend from the same samples: no extra state, and it cannot
        // disagree with the window it corroborates.
        situation.readinessWorsening =
            survey.deployableValue * 10 < start->deployable * 9;
    }
    // Quiet test: how long since the last material mobile-combat loss. A resume
    // is measured against this, not against how long the posture has been set.
    situation.quietCycles = lastMaterialLossCycle == std::numeric_limits<Uint32>::max()
        ? std::numeric_limits<Uint32>::max()
        : ArmyPosturePolicy::elapsed(now, lastMaterialLossCycle);
    situation.recentSeriousLoss = situation.windowCovered
        && situation.windowLoss >= std::max<int64_t>(1, thresholds.lossFloor)
        && situation.quietCycles < thresholds.stabiliseCycles;
    // The front comparison is honest about aircraft: an enemy wing inflicts the
    // losses the attrition window measures whether or not we can shoot back.
    const int64_t hostileFront = CombatPowerPolicy::hostileThreatToGround(survey.hostileFront);
    situation.canAnswerAir = CombatPowerPolicy::canAnswerAir(survey.deployable);
    situation.adverseMainFront = survey.frontObserved && hostileFront > 0
        && hostileFront * 10000 >= int64_t(thresholds.localWithdrawBps) * std::max<int64_t>(1, survey.allied.offensive());
    situation.coreUnderAttack = survey.coreUnderAttack;
    situation.assembledPower = survey.assembledPower;
    situation.designatedPower = survey.designatedPower;
    situation.frontFriendly = survey.allied.offensive();
    situation.frontHostile = hostileFront;
    situation.frontObserved = survey.frontObserved;
    // Release readiness for a rebuilt army, recomputed here every evaluation and
    // never stored: the house's own offensive value threshold - the same
    // militaryValueLimit share launchGroundHunt() requires, with the established
    // main-wave credit floor underneath it - plus the established
    // outnumbered/80%-of-configured dispatch gate. A legitimately outnumbered
    // house below that 80% therefore keeps waiting rather than being pushed out.
    const auto& releaseSettings = getQuantBotConfig().getSettings(static_cast<int>(difficulty));
    const FixPoint releasePercent =
        FixPoint(static_cast<int>(releaseSettings.attackThresholdPercent * 100)) / 100;
    const int64_t attackThresholdValue = (militaryValueLimit * releasePercent).lround();
    // 3000 credits is the established viable-main-wave value floor
    // (QuantBotBuildPolicy::viableMainWave), so a configured limit of zero or a
    // tiny threshold cannot release a patrol as if it were an army.
    const int64_t releaseFloor = std::max<int64_t>(3000, attackThresholdValue);
    const auto releaseGate = offensiveDispatchGate(survey);
    situation.readyForOffensive = survey.deployableGroundValue >= releaseFloor && !releaseGate.defer;

    // Local squad verdict. Friendly strength is what is actually beside the
    // squad - its own members, our nearby troops, nearby allies and the static
    // cover that reaches it - rather than every member anywhere on the map.
    const int64_t squadHostile = CombatPowerPolicy::hostileThreatToGround(survey.squadHostile);
    // squadSupport already includes the local wave members, exactly once.
    // Distant members are readiness, not support in this engagement.
    const int64_t squadFriendly = survey.squadSupport.holding();
    const bool materialCohort = survey.localWavePower
        >= std::max<int64_t>(1, thresholds.lossFloor) * CombatPowerPolicy::kPowerScale
        && survey.localWavePower * 5 >= survey.squad.offensive();
    // Pressure starts when the ENEMY reaches the ratio, which is the direction
    // the threshold is expressed in. The previous form had the comparison the
    // wrong way round and so started the clock on a squad that was winning.
    const bool outmatchedNow = materialCohort && squadHostile > 0
        && squadHostile * 10000 >= int64_t(thresholds.localWithdrawBps)
            * std::max<int64_t>(1, squadFriendly);
    if (outmatchedNow) {
        if (localPressureSince == std::numeric_limits<Uint32>::max()) localPressureSince = now;
    } else localPressureSince = std::numeric_limits<Uint32>::max();
    const auto local = ArmyPosturePolicy::localVerdict(squadFriendly, squadHostile,
        localPressureSince == std::numeric_limits<Uint32>::max() ? 0u
            : ArmyPosturePolicy::elapsed(now, localPressureSince), thresholds);
    // A severe local defeat is a house emergency only for a material cohort.
    // A few stragglers or an empty centroid are not the main army. Local defeat
    // can change its posture only when at least one fifth of its tracked power
    // is actually there, as well as meeting the material floor.
    // With the tactical retreat a locally overwhelmed hunt party falls back a few tiles to
    // reform (updateHuntParties) instead of the whole house withdrawing home.
    situation.severeLocalDefeat = local.severe && survey.waveMembers > 0 && materialCohort && !tacticalRetreat();
    // Sustained local withdrawal intent. Once it starts, it holds until the
    // squad is actually home or the house is ready again: otherwise the enemy
    // drifting one tile outside the measuring radius let the same units resume
    // the forced objective they were being pulled off.
    if (local.withdraw && materialCohort && survey.waveMembers > 0) {
        if (localWithdrawSince == std::numeric_limits<Uint32>::max()) localWithdrawSince = now;
    } else if (localWithdrawSince != std::numeric_limits<Uint32>::max()) {
        const bool squadHome = survey.squad.offensive() <= 0
            || (survey.assembledWavePower * 10000
                >= int64_t(thresholds.resumeAssembledBps) * std::max<int64_t>(1, survey.squad.offensive()));
        const bool timedOut = thresholds.maxWithdrawCycles > 0
            && ArmyPosturePolicy::elapsed(now, localWithdrawSince) >= thresholds.maxWithdrawCycles;
        if (squadHome || timedOut) localWithdrawSince = std::numeric_limits<Uint32>::max();
    }
    const bool wasLocalWithdraw = localSquadWithdraw;
    localSquadWithdraw = localWithdrawSince != std::numeric_limits<Uint32>::max();

    // Withdrawals are off by default (QuantBotConfig::RecoverySettings::withdrawalsEnabled):
    // measured on six all-against-Atreides seeds, every withdrawal trigger together cost a
    // lost game and 2-5 minutes per win, and no single trigger alone was the culprit.
    // Fall-back (Y, test-only, docs/formations.md "Round 5") keeps the local withdrawal alone:
    // a wave outmatched where it stands comes back to the protected rally and relaunches.
    if (!getQuantBotConfig().recovery.withdrawalsEnabled) {
        situation.severeLocalDefeat = false;
        situation.coreUnderAttack = false;
        situation.windowCovered = false;
        localSquadWithdraw = localSquadWithdraw && forceVariant(kForceFallBack);
    }
    if (localSquadWithdraw && !wasLocalWithdraw && forceVariant(kForceFallBack)) ++forceStats.fallBacks;
    const auto previous = armyPosture;
    const auto decision = ArmyPosturePolicy::evaluate(armyPosture, postureSince, situation, thresholds);
    if (decision.changed) {
        const bool released = previous == ArmyPosturePolicy::Posture::Recovering
            && decision.posture == ArmyPosturePolicy::Posture::Offensive;
        armyPosture = decision.posture;
        postureSince = now;
        recallCursor = 0;
        if (released) {
            // The army that paid for the last battle is gone; this is a rebuilt
            // one. Start its attrition window and its local pressure clocks from
            // here, or the losses that caused the withdrawal would still be
            // inside the window and would send the fresh army straight home.
            // Cumulative loss and kill totals are deliberately kept, so the
            // house ledger and every future window still work normally.
            attrition.resetBaselines();
            localPressureSince = std::numeric_limits<Uint32>::max();
            localWithdrawSince = std::numeric_limits<Uint32>::max();
            localSquadWithdraw = false;
        }
    }
    lastSurvey = survey;
    lastSurveyCycle = now;

    if (decision.changed || decision.emergency || AITelemetry::log().enabled()) {
        if (decision.changed || decision.emergency)
            traceDecision("army_posture", AITelemetry::Record()
                .set("from", ArmyPosturePolicy::postureName(previous))
                .set("to", ArmyPosturePolicy::postureName(armyPosture))
                .set("reason", decision.reason)
                .set("emergency", decision.emergency)
                .set("window_covered", situation.windowCovered)
                .set("window_loss", situation.windowLoss)
                .set("window_kill", situation.windowKill)
                .set("window_start_deployable", situation.windowStartDeployable)
                .set("readiness_worsening", situation.readinessWorsening)
                .set("adverse_main_front", situation.adverseMainFront)
                .set("core_under_attack", situation.coreUnderAttack)
                .set("severe_local_defeat", situation.severeLocalDefeat)
                .set("deployable_power", survey.deployable.offensive())
                .set("deployable_value", survey.deployableValue)
                .set("deployable_ground_value", survey.deployableGroundValue)
                .set("deployable_air_value", survey.deployableAirValue)
                .set("assembled_power", situation.assembledPower)
                .set("designated_power", situation.designatedPower)
                .set("assembled_wave_power", survey.assembledWavePower)
                .set("front_friendly", situation.frontFriendly)
                .set("front_hostile", situation.frontHostile)
                .set("front_hostile_ground", CombatPowerPolicy::hostileGroundOnly(survey.hostileFront))
                .set("front_hostile_air", survey.hostileFront.air)
                .set("front_observed", situation.frontObserved)
                .set("can_answer_air", situation.canAnswerAir)
                .set("quiet_cycles", int64_t(situation.quietCycles))
                .set("recent_serious_loss", situation.recentSeriousLoss)
                .set("ready_for_offensive", situation.readyForOffensive)
                .set("release_floor_value", releaseFloor)
                .set("release_gate_reason", releaseGate.reason)
                .set("local_withdraw", localSquadWithdraw)
                .set("core_threat_power", survey.coreThreatPower)
                .set("core_holding_power", survey.coreHoldingPower)
                .set("core_seriously_damaged", survey.coreSeriouslyDamaged)
                // Which colony the emergency was actually measured at, so a
                // live log names the place instead of a house-wide total.
                .set("core_x", survey.coreAt.x).set("core_y", survey.coreAt.y)
                .set("wave_members", survey.waveMembers)
                .set("local_wave_power", survey.localWavePower)
                .set("wave_power", survey.squad.offensive())
                .set("rally_x", protectedRally.x).set("rally_y", protectedRally.y)
                .set("staging_x", armyAssemblyAnchor().x)
                .set("staging_y", armyAssemblyAnchor().y));
    }
}

void QuantBot::updateCustomWave() {
    if (!recoveryActive()) { customWave.members.clear(); return; }
    const Uint32 now = getGameCycleCount();
    // A Custom sortie expires like a campaign one, so a wave that has stopped
    // achieving anything releases its members instead of trickling forward for
    // the rest of the match. Provisional: four minutes of game time.
    const Uint32 sortieCycles = static_cast<Uint32>(MILLI2CYCLES(240000));
    const bool hadWave = !customWave.members.empty();
    const auto* front=getObject(customWave.front);
    const bool usableGroundFront=front && front->getHealth()>0 && front->isActive()
        && !front->isAFlyingUnit() && front->getOwner()
        && front->getOwner()->getTeamID()!=getHouse()->getTeamID()
        && front->isVisible(getHouse()->getTeamID());
    for (auto it = customWave.members.begin(); it != customWave.members.end();) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(*it));
        if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0
            || !mobileCombatItem(unit->getItemID())) { it = customWave.members.erase(it); continue; }
        if (unitRole(unit, {UnitRole::Human, UnitRole::Repair}) != UnitRole::Free) {
            it = customWave.members.erase(it); continue;
        }
        // Engine Hunt can end without a candidate. An idle committed launcher
        // still belongs with this active army when its observed ground front is
        // reachable. This fallback never recruits a new home reserve or replaces
        // combat, a manual order, repair, a retreat or a live defense assignment.
        if (usableGroundFront && wholeArmyBaseDefence() && !huntPartyOf(unit->getObjectID())
            && !ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
            && !localSquadWithdraw && unit->isActive() && unit->isRespondable()
            && (unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher)
            && unit->getAttackMode()==GUARD && !unit->hasATarget()
            && !unit->isMoving() && !unit->wasForced()
            && unitRole(unit, {UnitRole::Defence}) == UnitRole::Free
            && (customWave.launched==0 || now-customWave.launched<sortieCycles)
            && unit->canAttack(front)
            && getMap().tileExists(front->getClosestPoint(unit->getLocation()))
            && getMap().getTile(front->getClosestPoint(unit->getLocation()))->isExploredByTeam(getHouse()->getTeamID())
            && !getMap().getTile(front->getClosestPoint(unit->getLocation()))->isFoggedByTeam(getHouse()->getTeamID())
            && groundAttackReachable(unit,front)) {
            doSetAttackMode(unit,HUNT);
            doAttackObject(unit,front,false);
            traceDecision("launcher_wave_rejoined",AITelemetry::Record()
                .set("unit",unit->getObjectID()).set("front",front->getObjectID()));
        }
        // Membership is commitment accounting, so it has to end when the
        // commitment does. A dispatched attacker hunts; once it is active, no
        // longer hunting, has no target and is not travelling, it has stopped
        // attacking - base defence claimed it, a retreat order was given, or it
        // simply came home - and holding it in the wave would reserve its value
        // against every future dispatch budget for the rest of the match. That
        // phantom commitment is what shrank a 139-unit Brutal army's next wave
        // to 33 units while the rest stood in the base. The house posture, not
        // this bookkeeping, decides whether to send it out again.
        //
        // Withdrawing and recovering keep their members: the recall is driven
        // off exactly this list, and a unit standing at the shelter is still
        // the wave that is being brought home.
        // A hunt party's members gather, wait for the rear and reform standing still on
        // purpose (Blob waves): they stay committed while the party lasts.
        const bool inParty = huntPartyOf(unit->getObjectID()) != nullptr
            || (!splits.empty() && splitOf(unit->getObjectID()));
        if (!ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture) && !inParty
            && unit->isActive() && unit->getAttackMode() != HUNT
            && !unit->hasATarget() && !unit->isMoving() && !unit->wasForced()
            && unitRole(unit, {UnitRole::Defence}) == UnitRole::Free) {
            it = customWave.members.erase(it); continue;
        }
        if (!inParty && unit->isActive() && !unit->hasATarget() && !unit->isMoving()
            && customWave.launched != 0 && now >= customWave.launched
            && now - customWave.launched >= sortieCycles) {
            it = customWave.members.erase(it); continue;
        }
        ++it;
    }
    if (hadWave || !customWave.members.empty()) customWave.lastActive = now;
    // Keep the observed front anchor only while it is still a thing we can see.
    // It anchors the strength comparison and the idle-launcher rejoin fallback.
    if (customWave.front != NONE_ID) {
        const auto* objective = getObject(customWave.front);
        if (!objective || objective->getHealth() <= 0 || !objective->getOwner()
            || objective->getOwner()->getTeamID() == getHouse()->getTeamID()
            || !objective->isVisible(getHouse()->getTeamID())) customWave.front = NONE_ID;
    }
}

bool QuantBot::groundAttackReachable(const UnitBase* unit, const ObjectBase* target) const {
    // Reuse the engine's own answer. Map::terrainAttackReachable is the test the
    // autonomous Hunt selection already uses to reject terrain-disconnected
    // targets, so the wave objective and the individual hunt agree about what is
    // reachable instead of each having its own idea.
    if (!unit || !target) return false;
    if (!unit->isAGroundUnit()) return true;      // Aircraft ignore terrain.
    return getMap().terrainAttackReachable(*unit, *target);
}

const ObjectBase* QuantBot::customWaveObjective(const UnitBase* unit) const {
    // Observed, ground-reachable enemy structures anchor the strength survey.
    // This intelligence never becomes a unit movement or target order.
    const ObjectBase* chosen = nullptr;
    int best = std::numeric_limits<int>::max();
    for (const auto* building : getStructureList()) {
        if (!building->getOwner() || building->getOwner()->getTeamID() == getHouse()->getTeamID()
            || building->getHealth() <= 0 || !building->isVisible(getHouse()->getTeamID())) continue;
        int score = unit ? blockDistance(unit->getLocation(), building->getLocation()).lround()
                         : int(building->getObjectID());
        // Prefer the production and economy that rebuilds the army, as the
        // campaign objective chooser already does for Hard and above.
        if (building->getItemID() == Structure_Refinery
            || building->getItemID() == Structure_HeavyFactory
            || building->getItemID() == Structure_ConstructionYard) score -= 20;
        if (score >= best) continue;
        if (unit && !groundAttackReachable(unit, building)) continue;
        best = score; chosen = building;
    }
    return chosen;
}

void QuantBot::adoptLegacyHuntersIntoWave() {
    // A save written before the posture existed carries Hard/Brutal units that
    // are already hunting on their own. The recall only ever orders tracked
    // members, so without adopting them the old attackers would keep trickling
    // forward for the rest of the match while the new reinforcements correctly
    // held at home - the exact split this feature exists to remove.
    //
    // Deterministic: the unit list is walked in its stable order and membership
    // is a sorted set, so every peer adopts the same ids on the same cycle.
    // Runs once, and only for units this controller may actually order.
    if (legacyHuntersAdopted) return;
    legacyHuntersAdopted = true;
    if (!recoveryActive()) return;
    int adopted = 0;
    for (const auto* unit : getUnitList()) {
        if (!orderableCombatUnit(unit) || !unit->isAGroundUnit()) continue;
        if (unit->getAttackMode() != HUNT) continue;
        if (unitRole(unit, {UnitRole::Repair}) != UnitRole::Free) continue;
        if (customWave.members.insert(unit->getObjectID()).second) ++adopted;
    }
    if (adopted) {
        // They were dispatched before this build existed, so the sortie clock
        // starts now rather than pretending the wave launched at cycle zero.
        if (customWave.launched == 0) customWave.launched = getGameCycleCount();
        customWave.lastActive = getGameCycleCount();
        traceDecision("army_legacy_hunters_adopted",
            AITelemetry::Record().set("adopted", adopted)
                .set("members", int(customWave.members.size())));
    }
}

std::vector<QuantBot::ColonyCluster> QuantBot::colonyClusters() const {
    // Each of this house's core buildings, scored by the core buildings and the
    // living emplacements around it. One bounded pass over the structure list.
    //
    // findBaseCentre() averages every structure, which on a two-colony map puts
    // the anchor in the open sand between the colonies: the one place with no
    // cover, no repair and no production. These clusters are what let both the
    // shelter and the staging choice name an actual colony instead.
    std::vector<ColonyCluster> cores;
    std::vector<std::pair<Coord,int>> cover;   // emplacement, its cover radius
    const auto& data = currentGame->objectData.data;
    const int turretRadius = std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Uint32 item = structure->getItemID();
        if (item == Structure_RocketTurret || item == Structure_GunTurret) {
            cover.emplace_back(structure->getLocation(), turretRadius);
            continue;
        }
        if (!RocketTurretPolicy::coreAsset(item)) continue;
        cores.push_back({structure->getLocation(), structure->getObjectID(), 0, 0, 0});
    }
    // A cluster is "the core buildings within one emplacement range", which is
    // the same neighbourhood the defence planner already reasons about.
    for (auto& candidate : cores) {
        for (const auto& other : cores)
            if (blockDistance(candidate.at, other.at).lround() <= turretRadius * 2) ++candidate.cores;
        for (const auto& emplacement : cover)
            if (blockDistance(candidate.at, emplacement.first).lround() <= emplacement.second)
                ++candidate.cover;
        // Unchanged weights: the established shelter scoring, now expressed over
        // the counted parts so the staging choice can read them separately.
        candidate.score = int64_t(candidate.cores) * 10 + int64_t(candidate.cover) * 25 + 5;
    }
    return cores;
}

Coord QuantBot::shelterAnchor() {
    const auto clusters = colonyClusters();
    if (clusters.empty()) return findBaseCentre(getHouse()->getHouseID());
    // Deterministic: highest score, then lowest object id.
    const ColonyCluster* best = nullptr;
    for (const auto& candidate : clusters)
        if (!best || candidate.score > best->score
            || (candidate.score == best->score && candidate.id < best->id)) best = &candidate;
    return best ? best->at : findBaseCentre(getHouse()->getHouseID());
}

Coord QuantBot::stagingAnchor() const {
    const auto clusters = colonyClusters();
    if (clusters.empty()) return findBaseCentre(getHouse()->getHouseID());
    const ColonyCluster* strongest = nullptr;
    for (const auto& candidate : clusters)
        if (!strongest || candidate.score > strongest->score
            || (candidate.score == strongest->score && candidate.id < strongest->id))
            strongest = &candidate;
    if (!strongest) return findBaseCentre(getHouse()->getHouseID());
    const auto& data = currentGame->objectData.data;
    const int group = 2 * std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    // How far a forward colony may be from the main cluster and still be "the
    // same base area". Without this bound the strongest-cover colony on a
    // sprawling map can be on the far side of it, and the whole reserve spends
    // the match walking there instead of attacking - which is exactly what the
    // observed 1.0.803 house did when its shelter jumped to a corner outpost.
    const int reachBound = std::max(16, 2 * group);
    // Observed enemy positions only - visible bases, then the places our own
    // buildings have actually been lost. No omniscient enemy lookup.
    const Coord ownTowards = observedFrontDirection(strongest->at);
    if (ownTowards.x == 0 && ownTowards.y == 0) return strongest->at;
    const int ownReach = std::max(std::abs(ownTowards.x), std::abs(ownTowards.y));
    const ColonyCluster* best = nullptr;
    int bestReach = ownReach;
    for (const auto& candidate : clusters) {
        // A forward colony is only a staging choice if it is a real colony that
        // can actually hold ground. Massing the army at an uncovered outpost is
        // how an army gets caught in the open, which this must not do.
        if (candidate.cover <= 0 || candidate.cores < 2) continue;
        const int fromMain = blockDistance(strongest->at, candidate.at).lround();
        if (fromMain > reachBound) continue;
        const Coord towards = observedFrontDirection(candidate.at);
        if (towards.x == 0 && towards.y == 0) continue;
        const int reach = std::max(std::abs(towards.x), std::abs(towards.y));
        // Genuinely forward, and still ours rather than the enemy's doorstep.
        if (reach >= bestReach || fromMain >= reach) continue;
        bestReach = reach; best = &candidate;
    }
    return best ? best->at : strongest->at;
}

int QuantBot::colonyFootprintRadius(Coord anchor) const {
    if (anchor.isInvalid()) return 0;
    const auto& data = currentGame->objectData.data;
    // The colony is the buildings grouped with the anchor, on the same
    // neighbourhood the cluster scoring uses. A building belonging to another
    // colony must not inflate this radius and push staging into empty sand.
    const int group = 2 * std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    int reach = 2;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Coord at = structure->getLocation();
        const int distance = blockDistance(anchor, at).lround();
        if (distance > group) continue;
        const Coord size = structure->getStructureSize();
        reach = std::max(reach, distance + std::max(size.x, size.y) - 1);
    }
    // The neighbourhood above bounds the scan. Clipping the actual footprint
    // would classify the outer streets of a large colony as exterior ground.
    return reach;
}

Coord QuantBot::offensiveStagingLocation() const {
    const Uint32 now = getGameCycleCount();
    if (offensiveStagingCycle == now) return offensiveStaging;
    offensiveStagingCycle = now;
    offensiveStaging = Coord::Invalid();
    exteriorStagingAvailable = false;
    const Coord anchor = stagingAnchor();
    offensiveColonyAnchor = anchor;
    if (anchor.isInvalid()) return offensiveStaging;
    const auto& data = currentGame->objectData.data;
    const int turretRadius = std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    std::vector<Coord> turrets;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && (structure->getItemID() == Structure_RocketTurret
                || structure->getItemID() == Structure_GunTurret))
            turrets.push_back(structure->getLocation());

    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        // Roads are the colony's exits. Standing on one blocks the traffic the
        // city simulation and the army both need, so staging never claims them.
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom()
            && !tile->isRoad() && dangerAt(p) == 0;
    };
    const int footprint = colonyFootprintRadius(anchor);
    offensiveColonyFootprint = footprint;
    // Outside the buildings, and only just outside: a fixed narrow band, so the
    // search stays small on a 900-unit match and the result is "a little
    // outside the base" rather than a march. How much ground the gathering
    // force then needs is a separate question, answered by the arrival radius
    // in armyAssemblyRadius() around this point.
    const int inner = footprint + 1;
    const int outer = inner + kStagingBand;
    const Coord forward = observedFrontDirection(anchor);
    const int forwardLen = std::max(1, std::max(std::abs(forward.x), std::abs(forward.y)));
    Coord best = Coord::Invalid();
    int bestScore = std::numeric_limits<int>::min();
    for (int y = std::max(0, anchor.y - outer); y <= std::min(getMap().getSizeY() - 1, anchor.y + outer); ++y)
      for (int x = std::max(0, anchor.x - outer); x <= std::min(getMap().getSizeX() - 1, anchor.x + outer); ++x) {
        const Coord p(x, y);
        const int distance = blockDistance(anchor, p).lround();
        if (distance < inner || distance > outer) continue;
        if (!usable(p)) continue;
        int open = 0;
        for (const Coord d : {Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) open += usable(p + d);
        // Room to stand and to leave. A single pocket between two buildings is
        // not a form-up point; it is the congestion being replaced.
        if (open < 2) continue;
        // Cover is a tiebreaker here, not a magnet: capped, so it can never
        // outweigh being outside the footprint the way an uncapped turret bonus
        // pulled the old shelter scoring into the middle of the base.
        int cover = 0;
        for (const Coord turret : turrets)
            if (blockDistance(turret, p).lround() <= turretRadius) ++cover;
        const int coverScore = std::min(cover, 3) * 20;
        // Facing the observed enemy, measured as the projection of the offset
        // onto the observed direction. Unknown enemy means no preference at all.
        const int projection = (forward.x == 0 && forward.y == 0) ? 0
            : ((p.x - anchor.x) * forward.x + (p.y - anchor.y) * forward.y) / forwardLen;
        // Prefer the near edge of the exterior band on the enemy-facing side;
        // distance must outweigh the reward for walking further forward.
        const int facing = 20 * projection / std::max(1,distance);
        const int value = coverScore + facing + open * 4 - (distance-inner) * 12;
        // Deterministic: best score, then the y-then-x scan order already fixes
        // the winner, so no identifier tiebreak is needed or used.
        if (value > bestScore) { bestScore = value; best = p; }
      }
    // No usable exterior ground: keep the established shelter rather than
    // inventing a point, so this is never worse than the previous behaviour.
    offensiveStaging = best.isValid() ? best : protectedRally;
    exteriorStagingAvailable = best.isValid();
    return offensiveStaging;
}

bool QuantBot::offensiveAssemblySlot(Coord at) const {
    if (!recoveryActive() || ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
        || localSquadWithdraw || garrisoned()) return true;
    offensiveStagingLocation();
    if (!exteriorStagingAvailable) return true; // No exterior: sheltered fallback.
    return getMap().tileExists(at) && !getMap().getTile(at)->isRoad()
        && blockDistance(offensiveColonyAnchor,at).lround()>offensiveColonyFootprint;
}

Coord QuantBot::findProtectedRallyLocation() {
    const Uint32 now = getGameCycleCount();
    // One bounded search per thirty simulation seconds, failures included, the
    // same cadence the established harvest rally uses - except that an anchor
    // which has become unsafe is re-chosen at once. Holding an army on a tile
    // that is now inside observed enemy fire for up to thirty seconds is the one
    // case where the throttle would do real harm.
    const bool currentUnsafe = protectedRally.isValid()
        && (!getMap().tileExists(protectedRally) || dangerAt(protectedRally) != 0
            || getMap().getTile(protectedRally)->isMountain()
            || getMap().getTile(protectedRally)->hasAStructure()
            || getMap().getTile(protectedRally)->isSpiceBloom());
    if (!currentUnsafe && protectedRallyCycle != std::numeric_limits<Uint32>::max()
        && now >= protectedRallyCycle && now - protectedRallyCycle < static_cast<Uint32>(MILLI2CYCLES(30000)))
        return protectedRally;
    protectedRallyCycle = now;
    const Coord base = shelterAnchor();
    if (base.isInvalid()) {
        // No base: fall back to the established rally rather than inventing one.
        return squadRallyLocation;
    }
    const auto& data = currentGame->objectData.data;
    const int turretRadius = std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    std::vector<Coord> turrets;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && (structure->getItemID() == Structure_RocketTurret
                || structure->getItemID() == Structure_GunTurret))
            turrets.push_back(structure->getLocation());

    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom() && dangerAt(p) == 0;
    };
    // The search window has to be able to hold the force that will gather in it.
    const int window = std::max(8, assemblyRadius(assemblyForceSize()));
    auto score = [&](Coord p) {
        int cover = 0;
        for (const Coord turret : turrets)
            if (blockDistance(turret, p).lround() <= turretRadius) ++cover;
        int open = 0;
        for (const Coord d : {Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) open += usable(p + d);
        // Turret cover first, then distance from the observed enemy relative to
        // the base itself (rearScore), then compactness.
        return cover * 100 + rearScore(p, base) + open * 4
            - blockDistance(p, base).lround() * 2;
    };
    // Hysteresis: keep the established anchor while it is still safe and still
    // within a tenth of the best score, so the army is not marched around.
    const bool keepCurrent = protectedRally.isValid() && usable(protectedRally);
    Coord best = Coord::Invalid();
    int bestScore = std::numeric_limits<int>::min();
    for (int dy = -window; dy <= window; ++dy) for (int dx = -window; dx <= window; ++dx) {
        const Coord p = base + Coord(dx, dy);
        if (!usable(p)) continue;
        const int value = score(p);
        if (value > bestScore) { bestScore = value; best = p; }
    }
    if (keepCurrent && (best.isInvalid() || score(protectedRally) * 10 >= bestScore * 9))
        return protectedRally;
    if (best.isValid()) {
        traceDecision("protected_rally", AITelemetry::Record().set("x", best.x).set("y", best.y)
            .set("score", bestScore).set("turrets", int(turrets.size()))
            .set("anchor_x", base.x).set("anchor_y", base.y).set("window", window));
        return best;
    }
    // Every candidate in the window is blocked or dangerous. Look for a tile
    // beside a real owned building instead - findSquadRetreatLocation() returns
    // the closest point OF the structure, which is the structure's own occupied
    // footprint and is not somewhere a unit can stand, so the neighbourhood of
    // that point is searched for an actually usable tile. If there is none, this
    // house has no safe shelter and reports that: callers then issue no
    // withdrawal order rather than marching units onto a building.
    const Coord shelter = findSquadRetreatLocation();
    if (shelter.isValid()) {
        for (int ring = 1; ring <= 4; ++ring)
            for (int dy = -ring; dy <= ring; ++dy) for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                const Coord p = shelter + Coord(dx, dy);
                if (!usable(p)) continue;
                traceDecision("protected_rally", AITelemetry::Record().set("x", p.x).set("y", p.y)
                    .set("fallback", "structure_shelter").set("ring", ring));
                return p;
            }
    }
    // Last resort: the established harvest rally, but only if it is actually
    // standable. Otherwise there is no shelter at all and that is the answer.
    if (squadRallyLocation.isValid() && usable(squadRallyLocation)) {
        traceDecision("protected_rally", AITelemetry::Record()
            .set("x", squadRallyLocation.x).set("y", squadRallyLocation.y)
            .set("fallback", "harvest_rally"));
        return squadRallyLocation;
    }
    traceDecision("protected_rally", AITelemetry::Record().set("fallback", "no_safe_shelter")
        .set("anchor_x", base.x).set("anchor_y", base.y));
    return Coord::Invalid();
}

Coord QuantBot::armyAssemblyAnchor() const {
    // Two different jobs, two different places.
    //
    // While the house is offensive, troops that are not committed form up just
    // outside the staging colony. That is where an army can actually deploy
    // from; gathering them in the middle of their own base produced the
    // observed congestion, with a dense block of idle troops standing between
    // the buildings they were meant to leave.
    //
    // While it is withdrawing or recovering, they come to the protected shelter
    // inside the base, under turret cover and beside the repair yard. A retreat
    // is the one time being deep inside the colony is correct.
    //
    // Easy, Medium, campaign roles and helpers keep the established squad rally.
    if (!recoveryActive()) return squadRallyLocation;
    if (!ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture) && !localSquadWithdraw && !garrisoned()) {
        const Coord staging = offensiveStagingLocation();
        if (staging.isValid()) return staging;
    }
    // Perimeter hold (V, docs/formations.md "Round 5"): while outnumbered the army stands on
    // the enemy-facing base perimeter and meets attackers there (only the garrison holds it: a
    // withdrawal or recovery still comes in to the shelter). In the city simulation too: the
    // footprint rule keeps the holding troops off planned sites and out of the site search
    // ("Planned sites and the army").
    if (garrisoned() && forceVariant(kForcePerimeterHold) && !ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
        && !localSquadWithdraw) {
        const Coord perimeterPoint = perimeter().point;
        if (perimeterPoint.isValid()) return perimeterPoint;
    }
    if (protectedRally.isValid()) return protectedRally;
    return squadRallyLocation;
}

Coord QuantBot::assemblyPoint() { return armyAssemblyAnchor(); }

int QuantBot::assemblyRadius(int members) {
    // The established derivation - two units per squared radius, as
    // checkAllUnits() uses for its rally radius - but with no fixed ceiling.
    //
    // A ceiling is actively harmful here: the resume test asks whether eighty
    // percent of the force is inside this radius, so a radius that cannot
    // physically hold eighty percent of a nine-hundred unit army would make
    // "assembled" unreachable and the house would never leave recovery. The
    // bound is the map instead: a radius wider than the map is meaningless.
    const int wanted = std::max(1, members);
    int radius = 3;
    const int limit = currentGameMap != nullptr
        ? std::max(8, std::max(currentGameMap->getSizeX(), currentGameMap->getSizeY()))
        : 64;
    while (radius * radius * 2 < wanted && radius < limit) ++radius;
    return radius;
}

int QuantBot::assemblyForceSize() const {
    int count=0;
    for (const auto* unit : getUnitList())
        if (orderableCombatUnit(unit) && unit->isAGroundUnit()
            && unitRole(unit, {UnitRole::Repair}) == UnitRole::Free) ++count;
    return count;
}

int QuantBot::armyAssemblyRadius() const {
    const int count=assemblyForceSize();
    int radius=assemblyRadius(count);
    const Coord anchor=armyAssemblyAnchor();
    if (anchor.isInvalid()) return radius;
    const int needed=(int64_t(count)*postureThresholds().resumeAssembledBps+9999)/10000;
    const int limit=2*std::max(getMap().getSizeX(), getMap().getSizeY());
    // Count terrain capacity, including occupied but standable land. A clipped
    // circle at a map edge or a built-up colony may not fit the required share.
    // Geometric growth bounds the scans; each is clipped to the actual map.
    while (radius < limit && int(assemblySlots(anchor,radius).size()) < needed)
        radius=std::min(limit,radius+std::max(1,radius/4));
    return radius;
}

std::vector<Coord> QuantBot::assemblySlots(Coord anchor, int radius) const {
    std::vector<Coord> slots;
    if (anchor.isInvalid()) return slots;
    const int r = std::max(1, radius);
    slots.reserve(size_t(2 * r + 1) * size_t(2 * r + 1));
    // Deterministic y-then-x order, so every peer builds the identical list.
    for (int y=std::max(0,anchor.y-r); y<=std::min(getMap().getSizeY()-1,anchor.y+r); ++y)
      for (int x=std::max(0,anchor.x-r); x<=std::min(getMap().getSizeX()-1,anchor.x+r); ++x) {
        const Coord p(x,y);
        if (!getMap().tileExists(p)) continue;
        // The same combat distance metric the arrival and "already walking
        // home" tests use. A box corner is further away than the radius under
        // that metric, so a unit sent there would never count as home and would
        // be reordered on every pass.
        if (blockDistance(anchor, p).lround() > r) continue;
        const auto* tile = getMap().getTile(p);
        if (tile->isMountain() || tile->hasAStructure() || tile->isSpiceBloom() || onPlannedSite(p)) continue;
        if (!offensiveAssemblySlot(p)) continue;
        if (dangerAt(p) != 0) continue;
        slots.push_back(p);
    }
    return slots;
}

bool QuantBot::withdrawUnit(const UnitBase* unit, Coord anchor, int radius,
                            const std::vector<Coord>* slots, std::set<int64_t>* reserved) {
    if (!unit || anchor.isInvalid()) return false;
    const int arrival = std::max(1, radius);
    // Already home, or already walking to somewhere inside the assembly area.
    if (blockDistance(unit->getLocation(), anchor).lround() <= arrival) {
        if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
        return false;
    }
    if (unit->wasForced() && unit->getDestination().isValid()
        && blockDistance(unit->getDestination(), anchor).lround() <= arrival) return false;
    // Spread deterministically around the anchor rather than marching a whole
    // army onto one tile: first the cheap hashed scatter the established regroup
    // uses, then a bounded walk of the slot list this pass already scanned. A
    // unit with nowhere safe to stand receives no order at all.
    Coord destination = Coord::Invalid();
    const auto key=[&](Coord p) { return int64_t(p.y)*getMap().getSizeX()+p.x; };
    const auto usable = [&](int dx, int dy) {
        const Coord p = anchor + Coord(dx, dy);
        return getMap().tileExists(p) && blockDistance(anchor, p).lround() <= arrival
            && (!reserved || !reserved->count(key(p)))
            && !getMap().getTile(p)->isSpiceBloom() && !onPlannedSite(p)
            && unit->canPass(p.x, p.y) && dangerAt(p) == 0;
    };
    // The hashed scatter comes FIRST, and the bare anchor is only a last
    // resort. Taking the anchor whenever it happened to be standable sent every
    // recalled unit to the identical tile, which is not an assembly, it is a
    // queue: the engine then shuffles them around one square at a time and the
    // arrival test never settles.
    if (const auto offset = SimpleArmyPolicy::rallyOffset(unit->getObjectID(), radius, usable))
        destination = anchor + Coord(offset->first, offset->second);
    if (destination.isInvalid() && slots != nullptr && !slots->empty()) {
        // Walk the shared list from this unit's own offset, so a large army
        // distributes over the available ground instead of competing for the
        // same eight hashed tiles. Bounded by the list itself - at most
        // (2*radius+1)^2 tiles, already scanned this pass - and only reached by
        // a unit the cheap scatter could not place. Infantry and vehicles have
        // different terrain rules, so passability is per unit even though the
        // candidate list is shared.
        const size_t count = slots->size();
        const size_t start = size_t(unit->getObjectID() % count);
        for (size_t probe = 0; probe < count; ++probe) {
            const Coord p = (*slots)[(start + probe) % count];
            if (usable(p.x-anchor.x,p.y-anchor.y)) { destination = p; break; }
        }
    }
    // A lone unit with no scattered slot available still goes home.
    if (destination.isInvalid() && usable(0, 0)) destination = anchor;
    if (destination.isInvalid()) return false;
    // AREAGUARD first: doSetAttackMode(GUARD) would cancel the movement
    // (UnitBase.cpp:1364), and AREAGUARD does not. The forced move then clears
    // the current target (UnitBase.cpp:1251) and suppresses target acquisition
    // while travelling (UnitBase.cpp:1756), and the engine clears forced on
    // arrival (UnitBase.cpp:903,962), which is what re-enables area defence at
    // the rally. RETREAT is never used: it returns false from
    // isInGuardRange/isInAttackRange (UnitBase.cpp:1446,1492), so a retreating
    // army cannot shoot back at all.
    doSetAttackMode(unit, AREAGUARD);
    doMove2Pos(unit, destination.x, destination.y, true);
    if (reserved) reserved->insert(key(destination));
    return true;
}

void QuantBot::applyArmyPosture(int& orderBudget) {
    if (!recoveryActive()) return;
    // The house posture recalls everything; a locally outmatched squad pulls
    // itself back even while the house is still offensive.
    // With the tactical retreat a local disadvantage is a hunt party's short fall-back
    // (updateHuntParties), not a recall home; the house posture still recalls everything.
    if (!ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture) && (!localSquadWithdraw || tacticalRetreat())) return;
    const Coord rally = protectedRally;
    if (rally.isInvalid()) return;
    if (orderBudget <= 0) return;
    // Replacing an existing request adds no queue entry. During congestion,
    // allow those replacements plus two new requests: stopping every recall
    // behind a busy global queue leaves the losing units chasing indefinitely.
    const bool congested=currentGame->getPathRequestQueueSize()>150;
    int freshRemaining=congested ? std::min(2,orderBudget) : orderBudget;
    // Candidates are the tracked wave members only, so this is one pass over a
    // subset of our own units - never a global scan per unit. std::set iteration
    // is sorted by id, so the order is identical on every peer.
    const int radius = armyAssemblyRadius();
    struct Recall { Uint32 id; int danger; };
    std::vector<Recall> pending;
    pending.reserve(customWave.members.size());
    for (const auto id : customWave.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!orderableCombatUnit(unit)) continue;
        // Already home.
        if (blockDistance(unit->getLocation(), rally).lround() <= radius) continue;
        // Already walking home: leave the order alone rather than resubmitting a
        // different slot on every pass. This covers both a forced withdrawal
        // order and a unit that is simply already moving into the assembly area,
        // so a queued or in-flight path request is never duplicated.
        const Coord destination = unit->getDestination();
        const bool headingHome = destination.isValid()
            && blockDistance(destination, rally).lround() <= radius
            && (unit->wasForced() || unit->isMoving());
        if (headingHome) continue;
        // Emergency base defence outranks the withdrawal and keeps its unit.
        if (unitRole(unit, {UnitRole::Defence}) == UnitRole::Defence) continue;
        pending.push_back({id, dangerAt(unit->getLocation())});
    }
    if (pending.empty()) return;
    // One bounded scan of the assembly area per pass, shared by every unit this
    // pass orders home.
    const std::vector<Coord> slots = assemblySlots(rally, radius);
    std::set<int64_t> reserved;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive()) continue;
        const Coord target=unit->getDestination();
        if ((unit->isMoving() || unit->wasForced()) && target.isValid()
            && blockDistance(target,rally).lround()<=radius)
            reserved.insert(int64_t(target.y)*getMap().getSizeX()+target.x);
    }
    int issued = 0;
    std::vector<const UnitBase*> recalled;
    // Half the budget goes to the most endangered members first; the rest is a
    // round-robin from the saved cursor, which is what guarantees that every
    // member eventually gets its order even in a nine-hundred unit match.
    const int dangerBudget = std::max(1, orderBudget / 2);
    std::vector<Recall> byDanger = pending;
    std::stable_sort(byDanger.begin(), byDanger.end(), [](const Recall& a, const Recall& b) {
        return a.danger != b.danger ? a.danger > b.danger : a.id < b.id;
    });
    std::set<Uint32> done;
    for (const auto& candidate : byDanger) {
        if (issued >= dangerBudget || orderBudget <= 0) break;
        if (candidate.danger <= 0) break;
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(candidate.id));
        const bool fresh=!unit->hasPendingPathRequest();
        if (fresh && freshRemaining<=0) continue;
        if (withdrawUnit(unit, rally, radius, &slots, &reserved)) {
            ++issued; --orderBudget; done.insert(candidate.id);
            recalled.push_back(unit);
            if (fresh) --freshRemaining;
        }
    }
    const size_t count = pending.size();
    size_t start = count ? size_t(recallCursor % count) : 0;
    for (size_t step = 0; step < count && orderBudget > 0; ++step) {
        const auto& candidate = pending[(start + step) % count];
        recallCursor = Uint32((start + step + 1) % count);
        if (done.count(candidate.id)) continue;
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(candidate.id));
        const bool fresh=!unit->hasPendingPathRequest();
        if (fresh && freshRemaining<=0) continue;
        if (withdrawUnit(unit, rally, radius, &slots, &reserved)) {
            ++issued; --orderBudget;
            recalled.push_back(unit);
            if (fresh) --freshRemaining;
        }
    }
    // Gathering the army in the configured wave shape (docs/formations.md; WaveFormation=None,
    // the default, keeps withdrawUnit()'s plain moves and no pace): the troops recalled together
    // this pass take the planner's slots around the rally, facing the way they march home,
    // nothing in a sonic lane, wherever that slot is safe assembly ground by withdrawUnit()'s
    // rules and no other unit's destination; anyone else keeps the slot withdrawUnit() chose.
    // Ids in order, so every peer agrees. They come home together at the slowest one's pace.
    // Who is recalled is unchanged.
    const int recallShape = QuantBotConfig::formationShapeOf(waveFormationShape());
    const bool recallInFormation = recallShape != QuantBotConfig::kWaveFormationNone;
    if (recallInFormation && static_cast<int>(recalled.size()) >= FormationOrders::kMinAIGroup) {
        const auto plan = FormationOrders::planGroup(recalled, rally, FormationPlanner::shapeFromId(recallShape));
        const auto tileKey = [&](Coord p) { return int64_t(p.y)*getMap().getSizeX()+p.x; };
        std::map<int64_t, int> taken;    // the recalled units' destinations, as they stand
        for (const auto* unit : recalled) ++taken[tileKey(unit->getDestination())];
        for (const auto& slot : plan.slots) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(slot.objectID));
            const Coord current = unit ? unit->getDestination() : Coord::Invalid();
            const Coord p = slot.tile;
            if (!unit || current.isInvalid() || p == current || !getMap().tileExists(p)) continue;
            const int64_t k = tileKey(p);
            if (taken.count(k) || reserved.count(k)
                || blockDistance(rally, p).lround() > std::max(1, radius)
                || getMap().getTile(p)->isSpiceBloom() || onPlannedSite(p) || !unit->canPass(p.x, p.y) || dangerAt(p) != 0) continue;
            doMove2Pos(unit, p.x, p.y, true);
            if (--taken[tileKey(current)] == 0) taken.erase(tileKey(current));
            ++taken[k];
        }
    }
    if (recallInFormation) doFormUp(recalled);
    if (issued) traceDecision("army_recall", AITelemetry::Record()
        .set("posture", ArmyPosturePolicy::postureName(armyPosture))
        .set("issued", issued).set("pending", int(count))
        .set("cursor", int(recallCursor)).set("radius", radius)
        .set("local_withdraw", localSquadWithdraw)
        .set("x", rally.x).set("y", rally.y));
}

const std::map<Uint32,Coord>& QuantBot::colonyGuardPosts() const {
    const Uint32 now = getGameCycleCount();
    if (colonyGuardCycle == now) return colonyGuardSet;
    colonyGuardCycle = now;
    colonyGuardSet.clear();
    if (!recoveryActive() || currentGame == nullptr) return colonyGuardSet;
    const auto& data = currentGame->objectData.data;
    const int turretRadius = std::max(1,
        data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange - 1);
    const int group = 2 * turretRadius;

    // One representative per colony: the strongest core building that is not
    // already inside an accepted colony. Deterministic - the candidates are
    // sorted by score then object id, and the structure list order never
    // reaches the result.
    auto clusters = colonyClusters();
    if (clusters.size() < 2) return colonyGuardSet;   // Single colony: the ordinary assembly covers it.
    std::stable_sort(clusters.begin(), clusters.end(),
        [](const ColonyCluster& a, const ColonyCluster& b) {
            return a.score != b.score ? a.score > b.score : a.id < b.id;
        });
    std::vector<ColonyCluster> colonies;
    for (const auto& candidate : clusters) {
        bool merged = false;
        for (const auto& accepted : colonies)
            if (blockDistance(accepted.at, candidate.at).lround() <= group) { merged = true; break; }
        if (!merged) colonies.push_back(candidate);
    }
    if (colonies.size() < 2) return colonyGuardSet;

    // The colony the army is already forming up at needs no separate garrison;
    // it has the whole reserve standing beside it.
    const Coord assembly = armyAssemblyAnchor();
    // One pass over the units: observed hostile combat power near each colony,
    // and our own mobile power already holding it.
    const int myTeam = getHouse()->getTeamID();
    const int watch = group + 4;
    std::vector<int64_t> hostile(colonies.size(), 0), holding(colonies.size(), 0);
    int orderableGround = 0;
    for (const auto* unit : getUnitList()) {
        if (!unit || !unit->getOwner() || unit->getHealth() <= 0 || !unit->isActive()) continue;
        const Uint32 item = unit->getItemID();
        if (!mobileCombatItem(item) || !unit->canAttack()) continue;
        const int price = data[item][unit->getOriginalHouseID()].price;
        const int64_t power = CombatPowerPolicy::unitPower(price,
            unit->getHealth().lround(), unit->getMaxHealth());
        const bool enemy = unit->getOwner()->getTeamID() != myTeam;
        if (enemy && !unit->isVisible(myTeam)) continue;       // Observed enemies only.
        const bool mine = orderableCombatUnit(unit) && unitRole(unit, {UnitRole::Repair}) == UnitRole::Free;
        if (mine && unit->isAGroundUnit()) ++orderableGround;
        if (!enemy && !mine) continue;
        for (size_t i = 0; i < colonies.size(); ++i) {
            if (blockDistance(colonies[i].at, unit->getLocation()).lround() > watch) continue;
            if (enemy) hostile[i] += power; else holding[i] += power;
        }
    }
    // Offence is never starved for a garrison: the reserve is a fixed handful,
    // and only a house that can spare several times that commits one.
    if (orderableGround <= kColonyGuardMax * 3) return colonyGuardSet;

    // The worst threatened colony takes priority. Otherwise keep a small post
    // at a real forward colony nearer the observed enemy than the assembly
    // colony; waiting until it is already being shot at leaves it undefended.
    const Coord assemblyTowards=observedFrontDirection(assembly);
    const int assemblyReach=std::max(std::abs(assemblyTowards.x),std::abs(assemblyTowards.y));
    int chosen = -1;
    int64_t worst = 0;
    for (size_t i = 0; i < colonies.size(); ++i) {
        if (blockDistance(colonies[i].at, assembly).lround() <= watch) continue;
        const Coord towards=observedFrontDirection(colonies[i].at);
        const int reach=std::max(std::abs(towards.x),std::abs(towards.y));
        const bool forward=assemblyReach>0 && reach>0 && colonies[i].cores>=2
            && reach+group<assemblyReach;
        const int64_t shortfall = std::max<int64_t>(forward ? 1 : 0,hostile[i] - holding[i]);
        if (shortfall <= 0) continue;
        if (chosen < 0 || shortfall > worst
            || (shortfall == worst && colonies[i].id < colonies[size_t(chosen)].id)) {
            chosen = int(i); worst = shortfall;
        }
    }
    if (chosen < 0) return colonyGuardSet;
    const Coord post = colonies[size_t(chosen)].at;

    // Nearest eligible reserve units, never the troops already committed to an
    // offensive and never anything under a human or emergency-defence order.
    struct Pick { Uint32 id; int distance; };
    std::vector<Pick> picks;
    for (const auto* unit : getUnitList()) {
        if (!orderableCombatUnit(unit) || !unit->isAGroundUnit()) continue;
        // Never a repair run, a defender, a wave member or a colonist's escort.
        if (unitRole(unit, {UnitRole::Repair, UnitRole::Defence, UnitRole::Wave,
                            UnitRole::Escort}) != UnitRole::Free) continue;
        if (unit->getAttackMode() == HUNT) continue;   // Already attacking: leave it attacking.
        picks.push_back({unit->getObjectID(), blockDistance(post, unit->getLocation()).lround()});
    }
    std::stable_sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    const size_t wanted = std::min<size_t>(kColonyGuardMax, picks.size());
    for (size_t i = 0; i < wanted; ++i) colonyGuardSet[picks[i].id] = post;
    return colonyGuardSet;
}

void QuantBot::assignColonyGuards(int& orderBudget) {
    if (!recoveryActive() || orderBudget <= 0) return;
    // A house that is pulling everything home is already answering the threat
    // with the whole army; a second, smaller answer would just fight the recall.
    if (ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture) || localSquadWithdraw) return;
    const auto& posts = colonyGuardPosts();
    if (posts.empty()) return;
    int issued = 0;
    Coord reported = Coord::Invalid();
    for (const auto& entry : posts) {
        if (orderBudget <= 0) break;
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(entry.first));
        // The cached chooser is advisory. Revalidate authority at execution,
        // since a defence assignment or dispatch can occur in this same cycle.
        if (!orderableCombatUnit(unit) || !unit->isAGroundUnit()
            || unitRole(unit, {UnitRole::Repair, UnitRole::Defence, UnitRole::Wave}) != UnitRole::Free
            || unit->getAttackMode()==HUNT) continue;
        reported = entry.second;
        // Same order shape the withdrawal uses: Area Guard so the unit can
        // still shoot, plus a committed move. Already-there units are left
        // alone by withdrawUnit itself.
        const int radius = std::max(3, assemblyRadius(kColonyGuardMax));
        if (withdrawUnit(unit, entry.second, radius)) { ++issued; --orderBudget; }
    }
    if (issued) traceDecision("colony_guard", AITelemetry::Record()
        .set("x", reported.x).set("y", reported.y)
        .set("assigned", int(posts.size())).set("issued", issued)
        .set("reserve_cap", kColonyGuardMax));
}

ArmyPosturePolicy::DispatchGate QuantBot::offensiveDispatchGate(const ArmySurvey& survey) const {
    const auto thresholds = postureThresholds();
    // Strictly ground against ground. The veto asks whether a ground wave can
    // win the ground fight it is about to start; an enemy air wing is a danger
    // the recovery triggers assess, not something staying home avoids, so it is
    // deliberately out of this comparison.
    const int64_t hostile = CombatPowerPolicy::hostileGroundOnly(survey.hostileFront);
    // The denominator is the CONFIGURED militaryValueLimit, never the Brutal
    // rolling override budget: that budget is owned value plus headroom, so 80%
    // of it would be reachable at any army size and the gate would be a no-op.
    // Production is not affected by any of this; an explicit unit override of
    // zero stays unlimited and building past the limit continues as before.
    return ArmyPosturePolicy::dispatchGate(survey.allied.offensive(), hostile,
        militaryValueLimit, survey.deployableValue, thresholds);
}

int QuantBot::frontBatteryGoal(int coverageCap, int refineries, int heavyFactories, int repairYards) const {
    if (!recoveryActive() || !getQuantBotConfig().recovery.frontBatteriesEnabled) return 0;
    return FrontBatteryPolicy::batteryGoal(coverageCap,
        FrontBatteryPolicy::economyReady(refineries, heavyFactories, repairYards));
}

Coord QuantBot::observedFrontDirection(Coord anchor) const {
    // Visible enemy bases first; then the direction our buildings have actually
    // been lost in. Never the harvest rally point, which is derived from where
    // the workers happen to be, and never another house's item list.
    Coord towards = Coord::Invalid();
    int closest = std::numeric_limits<int>::max();
    for (const Coord enemy : visibleEnemyBases) {
        const int d = blockDistance(anchor, enemy).lround();
        if (d < closest) { closest = d; towards = enemy; }
    }
    if (towards.isInvalid() && !recentStructureLosses.empty()) {
        int64_t x = 0, y = 0, n = 0;
        for (const auto& loss : recentStructureLosses) { x += loss.location.x; y += loss.location.y; ++n; }
        if (n) towards = Coord(int(x / n), int(y / n));
    }
    if (towards.isInvalid()) return Coord(0, 0);
    return towards - anchor;
}

void QuantBot::releaseLegacyGroundSquad() {
    if (!groundSquadPhase && groundSquad.empty()) return;
    for (const auto id:groundSquad) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getOwner()!=getHouse() || unitRole(unit, {UnitRole::Human}) != UnitRole::Free
            || unit->getItemID()==Unit_Saboteur) continue;
        doSetAttackMode(unit,GUARD);
        doSetAttackMode(unit,groundSquadPhase==2 ? HUNT : AREAGUARD);
    }
    groundSquad.clear(); groundSquadPhase=0; groundSquadObjective=NONE_ID;
    squadRallyLocation=Coord::Invalid();
    rallySelectedCycle=std::numeric_limits<Uint32>::max();
}

Coord QuantBot::findSquadRallyLocation() {
    const Uint32 now=getGameCycleCount();
    // One bounded search per 30 simulation seconds, including failed searches.
    if (rallySelectedCycle!=std::numeric_limits<Uint32>::max()
        && now-rallySelectedCycle<MILLI2CYCLES(30000)) return squadRallyLocation;
    rallySelectedCycle=now;
    int x=0,y=0,count=0;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || unit->getItemID()!=Unit_Harvester) continue;
        const auto* harvester=static_cast<const Harvester*>(unit);
        if (harvester->isReturning() || !harvester->isHarvesting()) continue;
        x+=unit->getX(); y+=unit->getY(); ++count;
    }
    Coord centre=count ? Coord(x/count,y/count) : findBaseCentre(getHouse()->getHouseID());
    if (centre.isInvalid()) return Coord::Invalid();
    const UnitBase* closest=nullptr;
    int distance=std::numeric_limits<int>::max();
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->canAttack() || unit->isAFlyingUnit()
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !unit->isVisible(getHouse()->getTeamID())) continue;
        const int d=blockDistance(centre,unit->getLocation()).lround();
        if (d<distance) { distance=d; closest=unit; }
    }
    // Stand on the enemy-facing side of the working harvesters, without selecting
    // an enemy base as a compulsory destination for every unit.
    if (closest) {
        const Coord delta=closest->getLocation()-centre;
        const int scale=std::max(1,std::max(std::abs(delta.x),std::abs(delta.y)));
        centre+=Coord(delta.x*3/scale,delta.y*3/scale);
    }
    auto usable=[&](Coord p) { return getMap().tileExists(p) && !getMap().getTile(p)->isMountain()
        && !getMap().getTile(p)->hasAStructure() && dangerAt(p)==0; };
    if (squadRallyLocation.isValid() && blockDistance(centre,squadRallyLocation)<=5 && usable(squadRallyLocation))
        return squadRallyLocation;
    Coord best=Coord::Invalid(); int bestScore=std::numeric_limits<int>::max();
    for (int dy=-8;dy<=8;++dy) for (int dx=-8;dx<=8;++dx) {
        const Coord p=centre+Coord(dx,dy);
        if (!usable(p)) continue;
        int open=0;
        for (const Coord d:{Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) open+=usable(p+d);
        const int score=(std::abs(dx)+std::abs(dy))*2+(4-open)*4;
        if (score<bestScore) { bestScore=score; best=p; }
    }
    if (best.isValid()) traceDecision("harvest_army_rally",AITelemetry::Record().set("x",best.x).set("y",best.y)
        .set("working_harvesters",count));
    return best;
}

Coord QuantBot::openingAnchor() {
    const Coord base=findBaseCentre(getHouse()->getHouseID());
    if (base.isValid()) return base;
    // No yard yet: the MCV stands where the base is about to be built.
    const UnitBase* mcv=nullptr;
    for (const auto* unit:getUnitList())
        if (unit->getOwner()==getHouse() && unit->isActive() && unit->getItemID()==Unit_MCV
            && (!mcv || unit->getObjectID()<mcv->getObjectID())) mcv=unit;
    return mcv ? mcv->getLocation() : Coord::Invalid();
}

Coord QuantBot::openingForwardOffset(Coord anchor) const {
    Coord towards=Coord::Invalid();
    int closest=std::numeric_limits<int>::max();
    auto consider=[&](const ObjectBase* object) {
        if (!object || !object->getOwner() || !object->isActive()
            || object->getItemID()==Unit_Sandworm
            || object->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !object->isVisible(getHouse()->getTeamID()) || object->getLocation().isInvalid()) return;
        const int d=blockDistance(anchor,object->getLocation()).lround();
        if (d<closest) { closest=d; towards=object->getLocation(); }
    };
    for (const auto* unit:getUnitList()) consider(unit);
    for (const auto* structure:getStructureList()) consider(structure);
    // Nothing hostile is visible at game start. The middle of the map is the
    // direction opponents lie in and needs no knowledge we have not earned.
    if (towards.isInvalid()) towards=Coord(getMap().getSizeX()/2,getMap().getSizeY()/2);
    return towards-anchor;
}

/**
    The combat units a custom game starts with stand on the home rock, which is
    the only ground the opening build-out can use. Step each of them a short way
    towards the enemy onto free sand, so the yard keeps its building space.

    This is an opening decision only: it never moves workers, transports or
    saboteurs, never starts an assault, and leaves alone any unit that is
    already off the rock, under a human order, fighting or hunting. A unit with
    no safe sand within reach receives no order here at all: the opening rally
    sweep that used to move everything regardless has been withdrawn, so
    "nowhere to step" now means the unit is left exactly where it stands.
*/
void QuantBot::applyOpeningSpaceDispersal() {
    openingDispersal.clear();
    openingDispersalUntil=0;
    // Campaign missions keep their scripted opening, and a helper bot never
    // reorders the units its human is commanding.
    if (supportMode || gameMode!=GameMode::Custom
        || (currentGame && isCampaignGameType(currentGame->gameType))) return;
    const Coord anchor=openingAnchor();
    if (anchor.isInvalid()) return;
    refreshTacticalDanger();
    const Coord forward=openingForwardOffset(anchor);
    const bool hasForward=forward.x!=0 || forward.y!=0;
    const int scale=std::max(1,std::max(std::abs(forward.x),std::abs(forward.y)));

    // Worms are a hazard candidates keep clear of, but only the ones this
    // house can actually see: a site chosen from a worm nobody has spotted is
    // knowledge the AI has not earned.
    std::vector<Coord> worms;
    for (const auto* unit:getUnitList())
        if (unit->isActive() && unit->getItemID()==Unit_Sandworm
            && unit->isVisible(getHouse()->getTeamID())) worms.push_back(unit->getLocation());

    std::vector<const UnitBase*> movers;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || !unit->isRespondable()
            || !unit->isAGroundUnit() || unit->isAFlyingUnit() || !unit->canAttack()) continue;
        const Uint32 item=unit->getItemID();
        if (item==Unit_MCV || item==Unit_Harvester || item==Unit_Carryall || item==Unit_Frigate
            || item==Unit_Saboteur || item==Unit_Sandworm) continue;
        if (unitRole(unit, {UnitRole::Human}) != UnitRole::Free || unit->wasForced() || unit->hasATarget()
            || unit->getAttackMode()==HUNT || unit->getAttackMode()==RETREAT) continue;
        if (!getMap().tileExists(unit->getLocation())) continue;
        // A unit already standing off the rock costs the base nothing.
        if (!getMap().getTile(unit->getLocation())->isRock()) continue;
        movers.push_back(unit);
    }
    if (movers.empty()) return;
    std::sort(movers.begin(),movers.end(),
        [](const UnitBase* a,const UnitBase* b) { return a->getObjectID()<b->getObjectID(); });

    // Ground actually connected to where the units stand. Traffic is ignored
    // because it moves; mountains and buildings are not.
    //
    // One shared flood fill would only prove that *some* mover can reach a
    // candidate. Label each connected piece of the window separately instead,
    // so a unit walled off behind its own neighbours is not handed a site on
    // the far side of that wall.
    const int window=kOpeningStepMax+4, span=window*2+1;
    const int x0=anchor.x-window, y0=anchor.y-window;
    auto inWindow=[&](Coord p) { return p.x>=x0 && p.y>=y0 && p.x<x0+span && p.y<y0+span; };
    auto slot=[&](Coord p) { return (p.y-y0)*span+(p.x-x0); };
    std::vector<int> component(static_cast<size_t>(span)*span,0);
    int components=0;
    std::vector<Coord> frontier;
    for (const auto* unit:movers) {
        const Coord from=unit->getLocation();
        if (!inWindow(from) || component[slot(from)]) continue;
        const int label=++components;
        component[slot(from)]=label;
        frontier.assign(1,from);
        for (size_t head=0;head<frontier.size();++head) {
            const Coord at=frontier[head];
            for (const Coord step:{Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) {
                const Coord p=at+step;
                if (!inWindow(p) || component[slot(p)] || !getMap().tileExists(p)) continue;
                const auto* tile=getMap().getTile(p);
                if (tile->isMountain() || tile->hasAStructure()) continue;
                component[slot(p)]=label; frontier.push_back(p);
            }
        }
    }

    struct OpeningSite { Coord location; int score; };
    std::vector<OpeningSite> sites;
    for (int dy=-kOpeningStepMax;dy<=kOpeningStepMax;++dy)
        for (int dx=-kOpeningStepMax;dx<=kOpeningStepMax;++dx) {
            const int step=std::max(std::abs(dx),std::abs(dy));
            if (step<kOpeningStepMin) continue;
            const Coord p=anchor+Coord(dx,dy);
            if (!getMap().tileExists(p) || !inWindow(p) || !component[slot(p)]) continue;
            const auto* tile=getMap().getTile(p);
            // Free open ground only: rock is what is being freed, spice belongs
            // to the harvesters and a bloom kills whoever parks on it.
            if (tile->isRock() || tile->isSpice() || tile->isSpiceBloom() || tile->isRoad()
                || tile->hasAnObject()) continue;
            if (dangerAt(p)>0) continue;
            bool wormNear=false;
            for (const Coord worm:worms)
                if (blockDistance(p,worm).lround()<=kOpeningWormClearance) wormNear=true;
            if (wormNear) continue;
            const int ahead=hasForward ? (dx*forward.x+dy*forward.y)*4/scale : 0;
            if (hasForward && ahead<=0) continue;   // Towards the enemy, never back into the base.
            sites.push_back({p,step*3-ahead});
        }

    std::set<int> taken;
    int placed=0;
    for (const auto* unit:movers) {
        const Coord from=unit->getLocation();
        // Only ground this unit itself is standing on a connected piece of.
        const int reachable=inWindow(from) ? component[slot(from)] : 0;
        if (!reachable) continue;
        const OpeningSite* best=nullptr;
        int bestScore=std::numeric_limits<int>::max();
        for (const auto& site:sites) {
            if (component[slot(site.location)]!=reachable) continue;
            if (taken.count(slot(site.location)) || !unit->canPass(site.location.x,site.location.y)) continue;
            const int walk=blockDistance(from,site.location).lround();
            if (walk>kOpeningStepMax+kOpeningStepMin) continue;
            const int score=site.score*2+walk;
            const bool earlier=best && (site.location.y<best->location.y
                || (site.location.y==best->location.y && site.location.x<best->location.x));
            if (score<bestScore || (score==bestScore && earlier)) { bestScore=score; best=&site; }
        }
        if (!best) continue;   // No safe sand within reach: this unit keeps its position.
        doMove2Pos(unit,best->location.x,best->location.y,true);
        openingDispersal[unit->getObjectID()]=best->location;
        taken.insert(slot(best->location));
        ++placed;
        traceDecision("opening_space_step",AITelemetry::Record().set("unit",unit->getObjectID())
            .set("item",unit->getItemID()).set("from_x",from.x).set("from_y",from.y)
            .set("x",best->location.x).set("y",best->location.y)
            .set("steps",blockDistance(from,best->location).lround()));
    }
    if (placed>0) openingDispersalUntil=getGameCycleCount()+MILLI2CYCLES(kOpeningHoldMs);
    traceDecision("opening_space_dispersal",AITelemetry::Record().set("anchor_x",anchor.x)
        .set("anchor_y",anchor.y).set("forward_x",forward.x).set("forward_y",forward.y)
        .set("candidates",static_cast<int>(sites.size()))
        .set("units",static_cast<int>(movers.size())).set("moved",placed)
        .set("without_site",static_cast<int>(movers.size())-placed));
}

bool QuantBot::holdsOpeningPosition(const UnitBase* unit) const {
    if (openingDispersal.empty() || getGameCycleCount()>=openingDispersalUntil) return false;
    return unit && openingDispersal.count(unit->getObjectID())>0;
}

Coord QuantBot::findSquadRetreatLocation() {
	Coord newSquadRetreatLocation = Coord::Invalid();

	FixPoint closestDistance = FixPt_MAX;
	for (const StructureBase* pStructure : getStructureList()) {
		// if it is our building, check to see if it is closer to the squad rally point then we are
		if (pStructure->getOwner()->getHouseID() == getHouse()->getHouseID()) {
			Coord closestStructurePoint = pStructure->getClosestPoint(squadRallyLocation);
			FixPoint structureDistance = blockDistance(squadRallyLocation, closestStructurePoint);

			if (structureDistance < closestDistance) {
				closestDistance = structureDistance;
				newSquadRetreatLocation = closestStructurePoint;
			}
		}
	}

	return newSquadRetreatLocation;
}
const UnitBase* QuantBot::findLightRaiderTarget(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    // Do not send raiders across the whole map looking for an ideal target.
    // Their normal hunt logic handles that; this only exploits visible prey in
    // a local combat pocket.
    constexpr int searchRadius = 12;
    const UnitBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive() || !candidate->isVisible(getHouse()->getTeamID())
            || !QuantBotBuildPolicy::isLightRaiderPreferredTarget(candidate->getItemID())
            || !raider->canAttack(candidate)) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= searchRadius && distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

const UnitBase* QuantBot::findThreateningTank(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    const UnitBase* threat = nullptr;
    FixPoint threatDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive()
            || !QuantBotBuildPolicy::isArmoredTank(candidate->getItemID())
            || candidate->getOwner()->getTeamID() == getHouse()->getTeamID()
            || candidate->getTarget() != raider) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= candidate->getWeaponRange() && distance < threatDistance) {
            threat = candidate;
            threatDistance = distance;
        }
    }
    return threat;
}


Coord QuantBot::findSquadCenter(int houseID, bool preferHunting) {
    int count=0, x=0, y=0, huntingCount=0, huntingX=0, huntingY=0, homeCount=0, homeX=0, homeY=0;
    for (const auto* unit : getUnitList()) {
        if (!unit || !unit->getOwner() || unit->getOwner()->getHouseID()!=houseID
            || !unit->isActive() || !unit->isRespondable() || !unit->isAGroundUnit()
            || !unit->canAttack() || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Sandworm
            || unit->getAttackMode()==RETREAT || unitRole(unit, {UnitRole::Repair}) != UnitRole::Free) continue;
        ++count; x+=unit->getX(); y+=unit->getY();
        if (unit->getAttackMode()==HUNT) {
            ++huntingCount; huntingX+=unit->getX(); huntingY+=unit->getY();
        } else {
            ++homeCount; homeX+=unit->getX(); homeY+=unit->getY();
        }
    }
    // Reinforcements and home guards must not drag an attacking army backwards.
    if (preferHunting && huntingCount) return Coord(huntingX/huntingCount,huntingY/huntingCount);
    // The troops that stayed behind are the anchor for everyone still at home.
    if (!preferHunting) return homeCount ? Coord(homeX/homeCount,homeY/homeCount) : Coord::Invalid();
    return count ? Coord(x/count,y/count) : Coord::Invalid();
}


/**
 * Kite away from a threat while moving towards squad center.
 * Calculates a retreat position that maintains weapon range from the threat
 * while moving closer to the squad center.
 * 
 * @param pUnit The unit to move (must be non-null and respondable)
 * @param pThreat The threatening unit to kite away from (must be non-null)
 * @param desiredRange The desired distance to maintain from threat (typically weapon range)
 */
void QuantBot::kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange) {
	// Safety checks
	if (!pUnit || !pThreat || !pUnit->isRespondable() || !currentGameMap) {
		return;
	}

	// Don't kite if pathfinding is overloaded
	if (currentGame && currentGame->isPathQueueStressed()) {
		return;
	}

	// CRITICAL: Prevent command spam - only issue kite commands if unit is not currently moving
	// or if destination is significantly different (>2 tiles)
	Coord unitLocation = pUnit->getLocation();
	Coord unitDestination = pUnit->getDestination();

	if (pUnit->isMoving() && unitDestination.isValid() && unitDestination != unitLocation) {
		// Unit is already moving - check if it's moving away from the threat
		Coord threatLocation = pThreat->getLocation();
		FixPoint distDestToThreat = blockDistance(unitDestination, threatLocation);
		FixPoint distCurrentToThreat = blockDistance(unitLocation, threatLocation);

		// If already moving away from threat, don't interrupt
		if (distDestToThreat > distCurrentToThreat) {
			return;
		}
	}

	Coord threatLocation = pThreat->getLocation();

	// Calculate current distance to threat
	FixPoint distToThreat = blockDistance(unitLocation, threatLocation);

	// If already at or beyond desired range, no need to kite
	if (distToThreat >= desiredRange) {
		return;
	}

	// Short combat spacing follows the fighting army, never the home rally. A
	// unit that was not sent on the wave backs onto the body it belongs to.
	Coord squadCenter = findSquadCenter(getHouse()->getHouseID(),
        isCampaignGameType(currentGame->gameType) || gameMode!=GameMode::Custom
        || difficulty>Difficulty::Medium || pUnit->getAttackMode()==HUNT);

	// If no squad center, just move directly away from threat
	if (!squadCenter.isValid()) {
		squadCenter = unitLocation;
	}

	// Calculate direction vectors
	FixPoint dx_threat = unitLocation.x - threatLocation.x;
	FixPoint dy_threat = unitLocation.y - threatLocation.y;
	FixPoint dx_squad = squadCenter.x - unitLocation.x;
	FixPoint dy_squad = squadCenter.y - unitLocation.y;

	// Normalize threat direction (away from threat)
	FixPoint threatDist = FixPoint::sqrt(dx_threat * dx_threat + dy_threat * dy_threat);
	if (threatDist < 0.1_fix) {
		threatDist = 0.1_fix;  // Avoid division by zero
	}
	FixPoint nx_away = dx_threat / threatDist;
	FixPoint ny_away = dy_threat / threatDist;

	// Normalize squad direction (towards squad)
	FixPoint squadDist = FixPoint::sqrt(dx_squad * dx_squad + dy_squad * dy_squad);
	if (squadDist < 0.1_fix) {
		squadDist = 0.1_fix;
	}
	FixPoint nx_squad = dx_squad / squadDist;
	FixPoint ny_squad = dy_squad / squadDist;

	// Blend: 70% away from threat, 30% towards squad
	// This prioritizes safety while still moving towards friendlies
	FixPoint blend_x = nx_away * 0.7_fix + nx_squad * 0.3_fix;
	FixPoint blend_y = ny_away * 0.7_fix + ny_squad * 0.3_fix;

	// Normalize blended direction
	FixPoint blendDist = FixPoint::sqrt(blend_x * blend_x + blend_y * blend_y);
	if (blendDist < 0.1_fix) {
		blendDist = 0.1_fix;
	}
	blend_x /= blendDist;
	blend_y /= blendDist;

	// Calculate retreat distance proportional to threat proximity
	// Closer threats = longer retreat to reach weapon range edge
	FixPoint retreatDistance = std::min(FixPoint(2), desiredRange - distToThreat);
	if (retreatDistance < 1) {
		retreatDistance = 1;  // Minimum 1-tile retreat
	}

	// Calculate target position
	int targetX = lround(unitLocation.x + blend_x * retreatDistance);
	int targetY = lround(unitLocation.y + blend_y * retreatDistance);

	// Clamp to map boundaries with 1-tile safety margin
	int mapWidth = currentGameMap->getSizeX();
	int mapHeight = currentGameMap->getSizeY();
	targetX = std::max(1, std::min(mapWidth - 2, targetX));
	targetY = std::max(1, std::min(mapHeight - 2, targetY));

	// Issue move command (forced so unit actually retreats instead of immediately canceling to attack)
    const auto attackMode = pUnit->getAttackMode();
    doMove2Pos(pUnit, targetX, targetY, true);
    // Moving a Hunt unit implicitly changes it to Guard. Restore the mission
    // after the short dodge so it resumes attacking, not regrouping at home.
    if (attackMode == HUNT) doSetAttackMode(pUnit, HUNT);
    // A unit can reissue this exact retreat each AI tick. One record per
    // unit/threat encounter is enough to explain the tactical decision.
    const uint64_t kiteSignature = (uint64_t(pThreat->getObjectID()) << 16)
        | static_cast<uint64_t>(desiredRange & 0xffff);
    if (lastKiteTrace[pUnit->getObjectID()] != kiteSignature) {
        traceDecision("combat_kite", AITelemetry::Record().set("unit", pUnit->getObjectID())
            .set("target", pThreat->getObjectID()).set("distance", distToThreat.lround())
            .set("desired_range", desiredRange).set("x", targetX).set("y", targetY));
        lastKiteTrace[pUnit->getObjectID()] = kiteSignature;
    }
}

/**
 * Move a unit to the optimal squad position.
 * Chooses between actual squad center and squad rally point based on which is closer.
 * Only moves if the unit is outside the radius of both positions.
 * 
 * @param pUnit The unit to potentially move
 * @param squadRadius The acceptable radius around either position (unit won't move if within this radius)
 */
void QuantBot::moveToOptimalSquadPosition(const UnitBase* unit, FixPoint radius, int* orderBudget) {
    if (!unit || unit->getItemID()==Unit_Saboteur || !unit->isRespondable() || unit->hasATarget()
        || unitRole(unit, {UnitRole::Human, UnitRole::Defence}) != UnitRole::Free
        || unit->wasForced() || unit->isMoving() || unit->getAttackMode()==HUNT) return;
    // A unit that has just been stepped off the home rock stays off it for the
    // opening window, instead of being regrouped straight back onto the ground
    // the first buildings need.
    if (holdsOpeningPosition(unit)) return;
    // A committed attacker is never regrouped. Dispatched Custom Hard/Brutal
    // troops hunt, and the early return above already leaves every hunting unit
    // alone; a tracked member on Area Guard after a defence order or from an
    // older save is excluded here as well, because walking it
    // back to the house assembly point is exactly how a dispatched wave ended
    // up standing in its own base instead of attacking.
    const bool trackedMember = recoveryActive()
        && unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave;
    if (trackedMember && !ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
        && !localSquadWithdraw) return;
    // A unit holding a threatened colony keeps holding it, and an escort
    // follows its colonist, not the house assembly. The guard set is derived
    // from current world state, so this is stable without remembering an
    // assignment across passes.
    if (unitRole(unit, {UnitRole::ColonyGuard, UnitRole::Escort}) != UnitRole::Free) return;
    // Easy and Medium keep their reserve at home, as they always have.
    //
    // Custom Hard/Brutal anchors the uncommitted reserve: a freshly built tank
    // walking alone to the surviving attack centroid is the drip-feed this
    // replaces. Where "home" is depends on the posture - outside the staging
    // colony while offensive, the protected shelter while coming home.
    const bool recoveryHold = recoveryActive();
    const bool homeAnchored = recoveryHold || (!isCampaignGameType(currentGame->gameType)
        && gameMode==GameMode::Custom && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium));
    const Coord home = assemblyPoint();
    Coord regroup = (unit->getAttackMode()==RETREAT || homeAnchored)
        ? home : findSquadCenter(getHouse()->getHouseID());
    if (regroup.isInvalid()) regroup = homeAnchored
        ? findSquadCenter(getHouse()->getHouseID(),false) : home;
    if (regroup.isInvalid()) return;
    const_cast<UnitBase*>(unit)->setGuardPoint(regroup);
    if (unit->getAttackMode()!=RETREAT && unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
    if (blockDistance(unit->getLocation(),regroup)<=radius
        && (!recoveryHold || offensiveAssemblySlot(unit->getLocation()))) return;
    // A queued path can exist before isMoving becomes true. Leave its destination
    // alone as well, instead of submitting a different slot on the next AI tick.
    const Coord destination=unit->getDestination();
    if (destination.isValid() && destination!=unit->getLocation()
        && blockDistance(destination,regroup)<=radius*2
        && (!recoveryHold || offensiveAssemblySlot(destination))) return;
    int reactiveBudget=1;
    if (!orderBudget) orderBudget=&reactiveBudget;
    if (*orderBudget<=0 || currentGame->getPathRequestQueueSize()>150) return;
    const auto offset=SimpleArmyPolicy::rallyOffset(unit->getObjectID(),radius.lround(),[&](int x,int y) {
        const Coord p=regroup+Coord(x,y);
        return getMap().tileExists(p) && unit->canPass(p.x,p.y) && dangerAt(p)==0 && !onPlannedSite(p)
            && (!recoveryHold || offensiveAssemblySlot(p));
    });
    if (!offset) return;
    const Coord p=regroup+Coord(offset->first,offset->second);
    doMove2Pos(unit,p.x,p.y,false);
    --*orderBudget;
}

/**
	Set a rally / retreat location for all our military units.
	This should be near our base but within it
	The retreat mode causes all our military units to move
	to this squad rally location

*/
void QuantBot::retreatAllUnits() {

	// Set the new squad rally location
	squadRallyLocation = findSquadRallyLocation();
	squadRetreatLocation = findSquadRetreatLocation();

	// turning this off fow now
	//retreatTimer = MILLI2CYCLES(90000);

	// If no base exists yet, there is no retreat location
	if (squadRallyLocation.isValid() && squadRetreatLocation.isValid()) {
		for (const UnitBase* pUnit : getUnitList()) {
			if (pUnit->getOwner() == getHouse()
				&& pUnit->getItemID() != Unit_Carryall
				&& pUnit->getItemID() != Unit_Sandworm
				&& pUnit->getItemID() != Unit_Harvester
				&& pUnit->getItemID() != Unit_MCV
				&& pUnit->getItemID() != Unit_Frigate
                        && pUnit->getItemID() != Unit_Saboteur) {

				doSetAttackMode(pUnit, RETREAT);
			}
		}
	}
}


/**
	In dune it is best to mass military units in one location.
	This function determines a squad leader by finding the unit with the most central location
	Amongst all of a players units.

	Rocket launchers and Ornithopters are excluded from having this role as on the
	battle field these units should always have other supporting units to work with

*/
    void QuantBot::checkAllUnits() {
    AITelemetry::PerformanceScope perfScope("ai.checkAllUnits", getGameCycleCount(), getHouse()->getHouseID());
        // Safety check: if our house is null (e.g., during game cleanup), don't check units
        if (getHouse() == nullptr) {
            return;
        }
        // Local to this invocation, built lazily by the first harvester that
        // actually reaches the candidate sweep, so the 22.6k calls that return
        // early add no work. Dies with this function; nothing persists.
        SpiceFieldCache spiceCache;

        refreshTacticalDanger();
        releaseLegacyGroundSquad();
        // The opening window is bounded: after it, ordinary regrouping owns
        // these units again.
        if (!openingDispersal.empty() && getGameCycleCount()>=openingDispersalUntil) openingDispersal.clear();
        if (!supportMode) squadRallyLocation = findSquadRallyLocation();
        const QuantBotConfig& config = getQuantBotConfig();
        const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
        // Recheck active base attacks before regrouping, including units already
        // moving away. Damage callbacks alone miss newly arrived defenders.
        for (const auto* intruder : getUnitList()) {
            if (!intruder->isActive() || !intruder->isVisible(getHouse()->getTeamID())
                || intruder->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
            // Aircraft are handled by the rescue below instead, which does not
            // wait for them to close to weapon range first.
            if (intruder->isAFlyingUnit()) continue;
            const auto* victim=intruder->getTarget();
            // Workers count as well as buildings. A harvester being shot at
            // only reaches the damage callback while it is actually being hit;
            // this recheck answers an attacker that is still standing over it.
            const bool ownAsset=victim && victim->getOwner()==getHouse()
                && (victim->isAStructure() || victim->getItemID()==Unit_Harvester
                    || victim->getItemID()==Unit_RebelHarvester);
            // The contact's own target is what scrambleUnitsAndDefend reads, so
            // this stays the same call it has always been: no extra protected
            // asset, and therefore no change to the air-strike planner.
            if (ownAsset && intruder->isInWeaponRange(victim)) scrambleUnitsAndDefend(intruder);
        }
        // A forward building can also be shelled by an enemy turret. Recheck
        // those real targets on the same cadence as mobile attackers.
        if (wholeArmyBaseDefence()) for (const auto* intruder:getStructureList())
            if (intruder->isVisible(getHouse()->getTeamID()) && attackingOwnedBuilding(intruder))
                scrambleUnitsAndDefend(intruder);
        // Aircraft on any building we own, main base or outlying colony, on the
        // same cadence and before regrouping can claim the launchers again.
        defendStructuresFromAircraft();
        // Defence is sized on contact. No fixed reserve owns troops or prevents
        // the main body helping when a city/harvester is under attack.
        // Per building, once per pass: is any visible enemy fighter still at it?
        std::map<Uint32,bool> buildingContested;
        auto contested=[&](const ObjectBase* building) {
            const auto found=buildingContested.find(building->getObjectID());
            if (found!=buildingContested.end()) return found->second;
            const int myTeam=getHouse()->getTeamID();
            bool any=false;
            for (const auto* enemy : getUnitList()) {
                if (!enemy->getOwner() || enemy->getOwner()->getTeamID()==myTeam || enemy->getHealth()<=0
                    || !enemy->isActive() || !enemy->canAttack() || enemy->isAFlyingUnit()
                    || !enemy->isVisible(myTeam)) continue;
                if (blockDistance(enemy->getLocation(),building->getClosestPoint(enemy->getLocation())).lround()
                    <= kBaseScrambleContactTiles) { any=true; break; }
            }
            buildingContested[building->getObjectID()]=any;
            return any;
        };
        for (auto it=defenceAssignments.begin();it!=defenceAssignments.end();) {
            const auto* unit=dynamic_cast<const UnitBase*>(getObject(it->first));
            const auto* target=getObject(it->second);
            // A flying contact is answered by the anti-air rescue and ends with
            // it: the engine releases any flying target beyond weapon range, so
            // the forced-transit handling below would only park the defender.
            const bool airborne=unit && target && !unit->isAFlyingUnit()
                && target->isAUnit() && target->isAFlyingUnit();
            if (unit && unit->getItemID()==Unit_Saboteur) {
                it=defenceAssignments.erase(it);
                continue;
            }
            // Scrambled to one of our buildings (scrambleWholeArmyToDefend). The
            // unit hunts; nothing here re-issues an order. It stays scrambled -
            // and so cannot be scrambled to another building - while it walks
            // there, during a launcher's spacing step, while it fights an enemy
            // it has met, and while enemies remain at that building. After that
            // it is an ordinary hunter, free to answer the next attack.
            if (target && target->isAStructure() && target->getOwner()==getHouse()) {
                const auto* fighting=unit ? unit->getTarget() : nullptr;
                const bool onJob=unit && unit->getOwner()==getHouse() && unit->getHealth()>0
                    && wholeArmyBaseDefence() && unit->getAttackMode()==HUNT
                    && target->getHealth()>0
                    && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free
                    && (unit->isHuntingToward() || unit->wasForced()
                        || (fighting && fighting->getHealth()>0
                            && blockDistance(unit->getLocation(),fighting->getClosestPoint(unit->getLocation())).lround()
                                <= unit->huntTowardRange())
                        || contested(target));
                if (onJob) ++it;
                else it=defenceAssignments.erase(it);
                continue;
            }
            if (!unit || unit->getOwner()!=getHouse() || unitRole(unit, {UnitRole::Human}) != UnitRole::Free
                || unit->getItemID()==Unit_Ornithopter
                || !target || target->getHealth()<=0 || !target->isActive()
                || target->getOwner()->getTeamID()==getHouse()->getTeamID()
                || unitRole(unit, {UnitRole::Repair}) != UnitRole::Free || unit->getAttackMode()==RETREAT
                || (airborne ? !airAttackContinues(unit,target)
                    : unit->getAttackMode()==HUNT || !campaignDefensiveContact(unit,target))) {
                // A Hunt unit holding a hostile contact is a scramble recorded by
                // an earlier version; it is released to ordinary Hunt as well.
                if (unit && unit->getOwner()==getHouse() && unitRole(unit, {UnitRole::Human}) == UnitRole::Free
                    && unit->getAttackMode()==AREAGUARD) {
                    if (airborne && unitRole(unit, {UnitRole::Repair}) == UnitRole::Free
                        && unit->getAttackMode()==AREAGUARD)
                        doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    // The emergency is over. Dropping the forced order hands
                    // target selection back to the engine.
                    else const_cast<UnitBase*>(unit)->setForced(false);
                    // A temporary post borrows an already-dispatched launcher.
                    // Restore that mission when the contact ends; newly produced
                    // home reserves still wait for the normal readiness gate.
                    if (wholeArmyBaseDefence() && unit->isActive() && unit->getHealth() > 0
                        && (unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher)
                        && unit->getAttackMode()==AREAGUARD
                        && unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave
                        && !ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
                        && !localSquadWithdraw && unitRole(unit, {UnitRole::Repair}) == UnitRole::Free) {
                        doSetAttackMode(unit,HUNT);
                        traceDecision("launcher_wave_resumed",AITelemetry::Record()
                            .set("unit",unit->getObjectID()).set("ended_contact",it->second));
                    }
                }
                it=defenceAssignments.erase(it);
            } else if (airborne) {
                // Fire whenever the aircraft comes inside weapon range, and
                // otherwise leave the approach chosen for this rescue running.
                if (unit->isInWeaponRange(target) && unit->getTarget()!=target)
                    doAttackObject(unit,target,true);
                ++it;
            } else {
                // Keep the original contact during travel. On arrival restore
                // ordinary target selection and kiting within this district.
                if (unit->isInAttackRange(target)) {
                    const_cast<UnitBase*>(unit)->setForced(false);
                    // Retain the anchored self-defense permission until the
                    // attacker dies or leaves; wave enforcement runs each tick.
                    if (unit->getTarget()!=target) doAttackObject(unit,target,false);
                    ++it;
                } else {
                    if (unit->getTarget()!=target || !unit->wasForced())
                        doAttackObject(unit,target,true);
                    ++it;
                }
            }
        }
        // Recall the tracked wave before ordinary regrouping claims its members.
        // Its own budget, separate from the four reactive rally orders, because a
        // withdrawal that moves four units per pass cannot bring a large army
        // home: the configured budget plus the fair cursor is what bounds this.
        if (recoveryActive()) {
            int recallBudget = std::max(0, getQuantBotConfig().recovery.recallOrdersPerPass);
            applyArmyPosture(recallBudget);
            // A threatened colony gets its finite reserve from what the recall
            // did not need, so colony defence can never outbid a withdrawal and
            // the two together stay inside one configured order budget.
            assignColonyGuards(recallBudget);
        }
        // Hunt parties (Blob waves) after the recall: a recall dissolves them.
        updateHuntParties();
        // Force concentration (test-only candidates, docs/formations.md "Concentrating force").
        concentrateForces();
        int rallyOrdersRemaining=4;
        int combatCount=0;
        for (const auto* unit:getUnitList()) if (unit->getOwner()==getHouse() && unit->isActive()
            && unit->isAGroundUnit() && unit->canAttack()) ++combatCount;
        int rallyRadius=3;
        if (recoveryActive()) rallyRadius=armyAssemblyRadius();
        else while (rallyRadius*rallyRadius*2<std::max(1,combatCount)) ++rallyRadius;
        for (auto it=defenceResponseCycles.begin();it!=defenceResponseCycles.end();)
            if (getGameCycleCount()-it->second>MILLI2CYCLES(30000)) it=defenceResponseCycles.erase(it); else ++it;
        // Use rally location instead of squad center to avoid constant destination changes
        Coord squadCenterLocation = squadRallyLocation;
        if(!supportMode) {
            tryLaunchOrnithopterStrike(diffSettings, config);
        }

        // Bundling (B, docs/formations.md "Concentrating force"; on by default): once the house
        // could attack, the idle reserve gathers at the forward rally (with O: or the army's
        // centre of mass, whichever is closer); below that it keeps to the home rally.
        const bool bundling=forceVariant(kForceBundle) && bundleArmyReady();
        int bundleTiles=0;
        std::vector<BundleWalker> bundleWalkers;
        if (bundling) {
            forwardBundlePoint();
            int64_t sx=0, sy=0, n=0, reserve=0;
            for (const auto* unit:getUnitList()) {
                if (unit->getOwner()!=getHouse() || !unit->isActive() || !unit->isAGroundUnit() || !unit->canAttack()
                    || !QuantBotInternal::mobileCombatItem(unit->getItemID())) continue;
                sx+=unit->getLocation().x; sy+=unit->getLocation().y; ++n;
                if (unit->getAttackMode()!=HUNT) ++reserve;
            }
            bundleCentre=forceVariant(kForceBundleOld) && n>0 ? Coord(int(sx/n),int(sy/n)) : Coord::Invalid();
            bundleTiles=ForceConcentrationPolicy::bundleRadius(int(reserve));
        }
        for (const UnitBase* pUnit : getUnitList()) {
            // Safety check: skip null units (can happen during unit destruction)
            if (pUnit == nullptr) {
                continue;
            }

            // Palace saboteurs already hunt autonomously. No tactical or army
            // controller may replace their orders, including forced orders.
            if (pUnit->getItemID()==Unit_Saboteur) continue;
            if (pUnit->getOwner()==getHouse() && unitRole(pUnit, {UnitRole::Human}) != UnitRole::Free) continue;
            // A captured Devastator is worth exactly one thing, so arm it at the first scan
            // that sees it - ahead of kiting, campaign holds, rally, escort and every other
            // role early return that would otherwise keep deferring the order. Support mode
            // and campaign waves manage their own units the same way. Human-controlled units
            // were released on the line above and are never armed automatically.
            if (pUnit->getOwner() == getHouse() && pUnit->isActive()
                && pUnit->getItemID() == Unit_Devastator
                && pUnit->getDeviationEpisode().credits()) {
                const auto* pDevastator = static_cast<const Devastator*>(pUnit);
                // Re-ordering never restarts the running fuse; skip the order once it is lit.
                if (!pDevastator->isArmedToDevastate()) {
                    doStartDevastate(pDevastator);
                    doSetAttackMode(pDevastator, HUNT);
                    traceDecision("captured_devastator_armed", AITelemetry::Record()
                        .set("unit", pUnit->getObjectID())
                        .set("original_house", pUnit->getOriginalHouseID())
                        .set("support_mode", supportMode)
                        .set("health", pUnit->getHealth().lround()));
                }
                continue;
            }
            // One exception for a hunting launcher: an enemy that is already
            // too close is backed away from, by one short step, so the launcher
            // fights at its own range again. Hunt, tracked membership and engine
            // target acquisition are all kept.
            if (escapeCloseThreatWhileHunting(pUnit)) continue;
            // Dispatched Custom attackers belong to engine Hunt. Ordinary
            // scans cannot kite them, assign prey, crush targets or regroup them.
            // Repair and emergency defence were handled before this point.
            if (engineHuntAttack(pUnit) && unitRole(pUnit, {UnitRole::Repair}) == UnitRole::Free) continue;
            // A hunt party's members belong to their party (gathering, waiting for the rear,
            // reforming or falling back): no rally, kiting or prey assignment re-orders them.
            if (pUnit->getOwner() == getHouse() && huntPartyOf(pUnit->getObjectID())
                && unitRole(pUnit, {UnitRole::Repair}) == UnitRole::Free) continue;
            // A defender mustering for a base attack belongs to its muster, a split wave's
            // group to its split.
            if (pUnit->getOwner() == getHouse() && forceHeld(pUnit->getObjectID())) continue;
            // Combat spacing applies to defenders and escorts too, before their
            // strategic-role early return. Guard orders already leave targets alone.
            if (!supportMode && pUnit->getOwner() == getHouse()
                && (pUnit->getItemID() == Unit_Launcher || pUnit->getItemID() == Unit_Deviator)) {
                const auto* target = pUnit->getTarget();
                const int range = currentGame->objectData.data[pUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
                if (target && target->canAttack(pUnit) && QuantBotBuildPolicy::needsKiting(
                        blockDistance(pUnit->getLocation(), target->getLocation()).lround(), range,
                        difficulty == Difficulty::Easy, target->isAUnit() && !static_cast<const UnitBase*>(target)->isAFlyingUnit())) {
                    kiteAwayFromThreat(pUnit, target, range);
                    continue;
                }
            }

            // Colonist escorts stay beside their MCV, ahead of rally, raids,
            // prey assignment and every other role below.
            if (!supportMode && escortColonist(pUnit)) continue;

            if (campaignControlsUnit(pUnit)) continue;

            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive()
                && QuantBotBuildPolicy::isLightRaider(pUnit->getItemID())) {
                if (const UnitBase* tank = findThreateningTank(pUnit)) {
                    // A short dodge does not release an offensive slot. Keep
                    // custom hunters in their wave just like artillery kiting.
                    if (isCampaignGameType(currentGame->gameType) || pUnit->getAttackMode()!=HUNT)
                        doSetAttackMode(pUnit, AREAGUARD);
                    kiteAwayFromThreat(pUnit, tank, tank->getWeaponRange() + 2);
                    traceDecision("light_raider_evade", AITelemetry::Record().set("unit", pUnit->getObjectID())
                        .set("threat", tank->getObjectID()).set("reason", "tank_targeting_in_range")
                        .set("distance", blockDistance(pUnit->getLocation(), tank->getLocation()).lround())
                        .set("desired_range", tank->getWeaponRange() + 2));
                    continue;
                }
                if (!pUnit->wasForced() && pUnit->getAttackMode() == HUNT
                    && !(isCampaignEnemy() && difficulty<=Difficulty::Medium)) {
                    if (const UnitBase* prey = findLightRaiderTarget(pUnit);
                        prey && prey != pUnit->getTarget()) {
                        doAttackObject(pUnit, prey, false);
                        traceDecision("light_raider_target", AITelemetry::Record().set("unit", pUnit->getObjectID())
                            .set("target", prey->getObjectID()).set("target_item", prey->getItemID())
                            .set("distance", blockDistance(pUnit->getLocation(), prey->getLocation()).lround()));
                    }
                }
            }

            if (bundling && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive() && pUnit->canAttack()
                && pUnit->getItemID() != Unit_Harvester && pUnit->getItemID() != Unit_Saboteur
                && unitRole(pUnit, {UnitRole::Repair}) == UnitRole::Free && !pUnit->hasATarget() && !pUnit->wasForced()
                && pUnit->getAttackMode() != HUNT && pUnit->getAttackMode() != RETREAT
                && bundleUnit(pUnit,bundleTiles,bundleWalkers)) continue;
            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive() && pUnit->canAttack()
                && pUnit->getItemID() != Unit_Harvester && pUnit->getItemID() != Unit_Saboteur
                && unitRole(pUnit, {UnitRole::Repair}) == UnitRole::Free && !pUnit->hasATarget() && !pUnit->wasForced()
                && pUnit->getAttackMode() != HUNT && pUnit->getAttackMode() != RETREAT
                && squadRallyLocation.isValid()) {
                moveToOptimalSquadPosition(pUnit,rallyRadius,&rallyOrdersRemaining);
                continue;
            }

            // Safety check: skip units with invalid owner
            if (pUnit->getOwner() == nullptr) {
                continue;
            }

		if (pUnit->getOwner() == getHouse()) {
                switch (pUnit->getItemID()) {
                case Unit_MCV: {
                    if (!campaignPermitsStructure(Structure_ConstructionYard)) break;
                    const MCV* pMCV = static_cast<const MCV*>(pUnit);
                    if (pMCV != nullptr) manageMcv(pMCV);
                    // doDeploy() inside manageMcv() places a construction yard
                    // immediately, which can cover spice. Conservatively drop the
                    // shared membership list rather than reason about the
                    // footprint; deploys are rare and the rebuild is one scan.
                    if (spiceCache.valid) { spiceCache.valid = false; ++spiceCache.invalidations; }
                } break;

                case Unit_Harvester: {
                    const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                    if(pHarvester != nullptr && pHarvester->isActive()) {
                        if (manageHarvesterSafety(pHarvester,&spiceCache)) break;
                        // Existing check for early return with half spice
						if(getHouse()->getNumItems(Structure_Refinery) < 4
							&& getHouse()->getCredits() < 1000
							&& pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE/2) {
                            doReturn(pHarvester);
                        }

                        // Check if harvester is stuck: not moving for extended period
                        // (Regardless of what it THINKS it's doing - harvesting/returning/idle)
                        bool isMoving = pHarvester->isMoving();

                        if(!isMoving) {
                            // Harvester is not moving - increment stuck counter
                            idleHarvesterCounters[pHarvester->getObjectID()]++;
                            harvesterMovingCounters[pHarvester->getObjectID()] = 0; // Reset moving counter

                            // 10 seconds at 60 fps = 600 game cycles
                            if(idleHarvesterCounters[pHarvester->getObjectID()] >= 600) {
                                // Harvester has been stuck for 10 seconds - take action based on spice level
                                FixPoint spiceAmount = pHarvester->getAmountOfSpice();

                                // If harvester has significant spice (>300 or >40% full), tell it to return
                                if(spiceAmount > 300 || spiceAmount > (HARVESTERMAXSPICE * 2) / 5) {
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - forcing RETURN", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doReturn(pHarvester);
                                } else {
                                    // Low/no spice - reset to harvest mode
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - resetting to HARVEST", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doSetAttackMode(pHarvester, HARVEST);
                                }
                                idleHarvesterCounters[pHarvester->getObjectID()] = 0; // Reset counter
                            }
                        } else {
                            // Harvester is moving - increment moving counter
                            harvesterMovingCounters[pHarvester->getObjectID()]++;

                            // Only reset stuck counter if continuously moving for 30+ cycles (0.5 seconds)
                            // This ignores brief jitter/animation frames
                            if(harvesterMovingCounters[pHarvester->getObjectID()] >= 30) {
                                if(idleHarvesterCounters[pHarvester->getObjectID()] > 0) {
                                    idleHarvesterCounters[pHarvester->getObjectID()] = 0;
                                }
                            }
                        }
                    }
                } break;

                case Unit_Carryall: {
                } break;

                case Unit_Frigate: {
                } break;

                case Unit_Sandworm: {
                } break;

                case Unit_Ornithopter: {
                    // Safe strike/defence planner owns targeting and patrol locations.
                } break;

                default: {
                    if (supportMode) {
                        break;
                    }

                    int squadRadius = lround(FixPoint::sqrt(getHouse()->getNumUnits()
                        - getHouse()->getNumItems(Unit_Harvester)
                        - getHouse()->getNumItems(Unit_Carryall)
                        - getHouse()->getNumItems(Unit_Ornithopter)
                        - getHouse()->getNumItems(Unit_Sandworm)
                        - getHouse()->getNumItems(Unit_MCV))) + 1;

                    // Safety check: ensure owner is valid before comparing
                    if (pUnit->getOwner() != nullptr && pUnit->getOwner()->getHouseID() != pUnit->getOriginalHouseID()) {
                        // Captured Devastators are armed centrally above, before any role or
                        // rally early return can divert them, so they never reach this switch.
                        if (pUnit->getItemID() == Unit_Harvester) {
                            const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                            if (pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE / 5) {
                                doReturn(pHarvester);
                            }
                            else {
                                    doMove2Pos(pUnit, squadCenterLocation.x, squadCenterLocation.y, true);
                            }
                        }
                        else {
                            // Send deviated unit to squad centre with tight radius (force movement)
                            if (pUnit->getAttackMode() != AREAGUARD) {
                                doSetAttackMode(pUnit, AREAGUARD);
                            }

                            // Use small radius (2 tiles) to ensure deviated units actually move to squad
                            moveToOptimalSquadPosition(pUnit, 2,&rallyOrdersRemaining);
                        }
                    }
                    else if (pUnit->getItemID() != Unit_Ornithopter && pUnit->getItemID() != Unit_Saboteur && pUnit->getAttackMode() != HUNT && !pUnit->hasATarget() && !pUnit->wasForced()) {
                        if (pUnit->getAttackMode() == AREAGUARD && squadCenterLocation.isValid() && (gameMode != GameMode::Campaign)) {
							if (!pUnit->hasATarget()) {
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius,&rallyOrdersRemaining);
                            }
                        }
                        else if (pUnit->getAttackMode() == RETREAT) {
                            if (!pUnit->wasForced()) {
                                if (pUnit->getHealth() < pUnit->getMaxHealth()) {
                                    doRepair(pUnit);
                                }
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius + 2,&rallyOrdersRemaining);
                            }

                            // Check if we've reached the retreat position. An
                            // attack centroid is not a place to end a retreat.
                            Coord actualSquadCenter = findSquadCenter(getHouse()->getHouseID(),
                                isCampaignGameType(currentGame->gameType) || gameMode!=GameMode::Custom
                                || difficulty>Difficulty::Medium);
                            FixPoint distToSquadCenter = actualSquadCenter.isValid() ? 
                                blockDistance(pUnit->getLocation(), actualSquadCenter) : FixPt_MAX;
                            FixPoint distToRallyPoint = squadRallyLocation.isValid() ? 
                                blockDistance(pUnit->getLocation(), squadRallyLocation) : FixPt_MAX;

                            // If within radius of either, we've finished retreating
                            if (distToSquadCenter <= squadRadius + 2 || distToRallyPoint <= squadRadius + 2) {
                                // We have finished retreating back to the rally point
                                doSetAttackMode(pUnit, AREAGUARD);
                            }
                        }
                        else if (pUnit->getAttackMode() == GUARD
                            && ((pUnit->getDestination() != squadRallyLocation) || (blockDistance(pUnit->getLocation(), squadRallyLocation) <= squadRadius))) {
                            // A newly deployed unit has reached the rally point, or has been diverted => Change it to area guard
                            logDebug("UNIT GUARD->AREAGUARD: %s at (%d,%d)", 
                                getItemNameByID(pUnit->getItemID()).c_str(), 
                                pUnit->getLocation().x, pUnit->getLocation().y);
                            doSetAttackMode(pUnit, AREAGUARD);
                        }
                    }
                } break;
            }
        }
    }
    // The pass's bundle orders, farthest first: the same units whatever the list order.
    if (bundling) orderBundleWalkers(bundleWalkers,bundleTiles);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.spice_list.builds",static_cast<int64_t>(spiceCache.builds),-1,false);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.spice_list.hits",static_cast<int64_t>(spiceCache.hits),-1,false);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.spice_list.invalidations",static_cast<int64_t>(spiceCache.invalidations),-1,false);
}
