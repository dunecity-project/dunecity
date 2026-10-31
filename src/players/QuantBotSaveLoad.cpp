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

// QuantBot save/load: the stream constructor, save() and the observer runtime block.

#include "QuantBotInternal.h"


QuantBot::QuantBot(InputStream& stream, House* associatedHouse) : Player(stream, associatedHouse) {
	QuantBot::init();

	difficulty = static_cast<Difficulty>(stream.readUint8());
	gameMode = static_cast<GameMode>(stream.readUint8());
	buildTimer = stream.readSint32();
	attackTimer = stream.readSint32();
	retreatTimer = stream.readSint32();

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		initialItemCount[i] = stream.readUint32();
	}
	initialMilitaryValue = stream.readSint32();
	militaryValueLimit = stream.readSint32();
	harvesterLimit = stream.readSint32();
	lastCalculatedSpice = stream.readSint32();
	campaignAIAttackFlag = stream.readBool();

	squadRallyLocation.x = stream.readSint32();
	squadRallyLocation.y = stream.readSint32();
	squadRetreatLocation.x = stream.readSint32();
	squadRetreatLocation.y = stream.readSint32();

	// Need to add in a building array for when people save and load
	// So that it keeps the count of buildings that should be on the map.
	Uint32 NumPlaceLocations = stream.readUint32();
	for (Uint32 i = 0; i < NumPlaceLocations; i++) {
		Sint32 x = stream.readSint32();
		Sint32 y = stream.readSint32();

        placeLocations.emplace_back(x, y);
    }

    try {
        if (currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_AI_ROLE) {
            const Uint8 saved = stream.readUint8();
            role = saved <= static_cast<Uint8>(AIRole::Fighter) ? static_cast<AIRole>(saved) : AIRole::Both;
        } else {
            role = stream.readBool() ? AIRole::Builder : AIRole::Both;
        }
    } catch(const InputStream::eof&) {
        role = AIRole::Both;
    } catch(const InputStream::error&) {
        role = AIRole::Both;
    }

    if (currentGame->getLoadedSavegameVersion() >= 9827) {
        rallySelectedCycle = stream.readUint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9828) {
        nonServiceConstructionOrders = std::min<Uint32>(3, stream.readUint32());
    }
    if (currentGame->getLoadedSavegameVersion() >= 9829) {
        powerDemandSampleCycle = stream.readUint32();
        powerDemandSample = stream.readSint32();
        projectedPowerDemandGrowth = stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9830) {
        groundSquadPhase=stream.readUint32(); groundSquadStarted=stream.readUint32();
        groundSquadNextControl=stream.readUint32(); groundSquadInitialCount=stream.readUint32();
        groundSquadObjective=stream.readUint32(); groundSquadObjectiveCycle=stream.readUint32();
        const auto count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) groundSquad.insert(stream.readUint32());
        auto readMap=[&](auto& values) {
            const auto n=stream.readUint32();
            for (Uint32 i=0;i<n;++i) { const auto id=stream.readUint32(); const auto value=stream.readUint32(); values[id]=value; }
        };
        readMap(manualUnitOrders); readMap(defenceAssignments);
        const auto losses=stream.readUint32();
        for (Uint32 i=0;i<losses;++i) {
            RecentStructureLoss loss;
            loss.location.x=stream.readSint32(); loss.location.y=stream.readSint32();
            loss.size.x=stream.readSint32(); loss.size.y=stream.readSint32();
            loss.cycle=stream.readUint32(); loss.item=stream.readUint32(); recentStructureLosses.push_back(loss);
        }
        performanceHistory.load(stream);
    }
    // The measured Deviator ledger replaces the old flat conversion estimate, and House
    // resets the whole unit-mix learning ledger on the same load. A decayed window computed
    // from the old counters would keep the replaced accounting alive, so it starts empty too.
    if (currentGame->getLoadedSavegameVersion() < 9849) resetLearningForMeasuredScoring();
    if (currentGame->getLoadedSavegameVersion() >= 9831) {
        groundSquadProgressCycle=stream.readUint32();
        groundSquadProgressLocation.x=stream.readSint32();
        groundSquadProgressLocation.y=stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9832) {
        const Uint32 count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) {
            const Uint32 key=stream.readUint32();
            defenceResponseCycles[key]=stream.readUint32();
        }
    }
    if (currentGame->getLoadedSavegameVersion() >= 9838) campaignWave.load(stream);
    if (currentGame->getLoadedSavegameVersion() >= 9839) {
        const auto count=stream.readUint32();
        for(Uint32 i=0;i<count;++i) scriptedAssaults.insert(stream.readUint32());
    }
    if (currentGame->getLoadedSavegameVersion() >= 9842) {
        campaignBaselineCaptured=stream.readBool();
        campaignBaseline.refineries=std::max(0,stream.readSint32());
        campaignBaseline.allowance=std::max(0,stream.readSint32());
        campaignMapHasSpice=stream.readBool();
        campaignSpiceZeroSince=stream.readUint32();
        const auto count=stream.readUint32();
        if(count>Num_ItemID) throw std::runtime_error("Invalid campaign building permissions");
        for(Uint32 i=0;i<count;++i) {
            const auto item=stream.readUint32();
            if(item>=Num_ItemID || !isStructure(item)) throw std::runtime_error("Invalid campaign structure");
            campaignOriginalStructures.insert(item);
        }
    }
    if (currentGame->getLoadedSavegameVersion() >= 9848) {
        openingDispersalUntil=stream.readUint32();
        const auto count=stream.readUint32();
        if(count>kOpeningDispersalLimit) throw std::runtime_error("Invalid opening dispersal state");
        for(Uint32 i=0;i<count;++i) {
            const auto id=stream.readUint32();
            Coord site;
            site.x=stream.readSint32();
            site.y=stream.readSint32();
            openingDispersal[id]=site;
        }
    }
    if (currentGame->getLoadedSavegameVersion() >= 9852) {
        const auto posture=stream.readUint8();
        if(!ArmyPosturePolicy::validPosture(posture)) throw std::runtime_error("Invalid army posture");
        armyPosture=static_cast<ArmyPosturePolicy::Posture>(posture);
        postureSince=stream.readUint32();
        localPressureSince=stream.readUint32();
        localWithdrawSince=stream.readUint32();
        lastMaterialLossCycle=stream.readUint32();
        legacyHuntersAdopted=stream.readBool();
        recallCursor=stream.readUint32();
        protectedRallyCycle=stream.readUint32();
        protectedRally.x=stream.readSint32();
        protectedRally.y=stream.readSint32();
        attrition.load(stream,[]{ throw std::runtime_error("Invalid army attrition ledger"); });
        customWave.initialized=stream.readBool();
        customWave.opening=stream.readUint32();
        customWave.launched=stream.readUint32();
        customWave.lastActive=stream.readUint32();
        customWave.front=stream.readUint32();
        const auto waveCount=stream.readUint32();
        if(waveCount>kCustomWaveLimit) throw std::runtime_error("Invalid custom wave state");
        for(Uint32 i=0;i<waveCount;++i) {
            const auto id=stream.readUint32();
            // NONE_ID is never a member; a stream carrying one is damaged
            // rather than fatal, and a missing object is dropped on the first
            // updateCustomWave() anyway.
            if(id!=NONE_ID) customWave.members.insert(id);
        }
    }
    // SAVEGAMEVERSION 9862: hunt parties (Blob waves). An older save has none; its waves are
    // plain engine hunters and carry on as such.
    if (currentGame->getLoadedSavegameVersion() >= 9862) loadHuntParties(stream);
    // SAVEGAMEVERSION 9863: the reserve bundle (point, centre, 30 s clock) and the launch gate's
    // wait. An older save has none: the bundle is chosen afresh at the first unit pass and a
    // ready wave starts a new wait, as at the start of a game.
    if (currentGame->getLoadedSavegameVersion() >= 9863) loadForceState(stream);
    // SAVEGAMEVERSION 9864: the remembered enemy army and the defence musters. A 9863 save (1.0.844)
    // has neither: the enemy army is learned again from what is seen and no defender is mustering.
    if (currentGame->getLoadedSavegameVersion() >= 9864) loadDefenceMemory(stream);
    else { enemySightings.clear(); musters.clear(); }
    // SAVEGAMEVERSION 9865: the strategy (docs/ai-play-styles.md, "Strategies"). A 9864 save has
    // none: the first update picks it by the same pure function of seed, house and config.
    if (currentGame->getLoadedSavegameVersion() >= 9865) {
        strategyPicked = stream.readBool();
        const Uint8 mix = stream.readUint8();
        const Uint8 tempo = stream.readUint8();
        if (mix >= QuantBotStrategy::kMixCount || tempo >= QuantBotStrategy::kTempoCount)
            throw std::runtime_error("Invalid QuantBot strategy");
        strategy.mix = static_cast<QuantBotStrategy::Mix>(mix);
        strategy.tempo = static_cast<QuantBotStrategy::Tempo>(tempo);
    } else {
        strategyPicked = false;
        strategy = QuantBotStrategy::Choice{};
    }
    // SAVEGAMEVERSION 9866: the starting ratios a Tuned/Bounded strategy resolved to. A 9865 save
    // holds none of those mixes (its mixes take their ratios from QuantBotStrategy::profile).
    // 9868: ten slots, one per special; a 9866/9867 save has the eight group slots, its special
    // share is split between the specials this house can build (UnitMixPolicy::expandGroups).
    strategyPrior = {};
    if (currentGame->getLoadedSavegameVersion() >= 9866) {
        int total = 0;
        if (currentGame->getLoadedSavegameVersion() >= 9868) {
            for (auto& share : strategyPrior) {
                share = stream.readUint16();
                total += share;
            }
        } else {
            UnitMixPolicy::GroupWeights group{};
            for (auto& share : group) {
                share = stream.readUint16();
                total += int(share);
            }
            const auto expanded = UnitMixPolicy::expandGroups(group, fieldableMixSlots());
            for (size_t i = 0; i < strategyPrior.size(); ++i) strategyPrior[i] = int(expanded[i]);
        }
        const bool resolves = strategy.mix == QuantBotStrategy::Mix::Tuned || strategy.mix == QuantBotStrategy::Mix::Bounded;
        if ((total != 0 && total != 10000) || (total != 0 && !resolves))
            throw std::runtime_error("Invalid QuantBot strategy starting mix");
    }
    resolveStrategyProfile();
    // An older save has no posture, so the defaults above stand: offensive, an
    // empty ledger and no tracked wave. The first update() rebuilds the baseline
    // ring from live state, which simply means the attrition window is not
    // covered yet and no strategic trigger can fire until it is.

    // Preserve an older save's initialized opening: fired triggers have already
    // been removed from TriggerManager, so rescanning could delay it forever.
    if (builderOnly()) {
        gameMode = GameMode::Custom;
        attackTimer = std::numeric_limits<Sint32>::max();
    }
}

void QuantBot::save(OutputStream& stream) const {
	Player::save(stream);

	stream.writeUint8(static_cast<Uint8>(difficulty));
	stream.writeUint8(static_cast<Uint8>(gameMode));
	stream.writeSint32(buildTimer);
	stream.writeSint32(attackTimer);
	stream.writeSint32(retreatTimer);

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		stream.writeUint32(initialItemCount[i]);
	}
	stream.writeSint32(initialMilitaryValue);
	stream.writeSint32(militaryValueLimit);
	stream.writeSint32(harvesterLimit);
	stream.writeSint32(lastCalculatedSpice);
	stream.writeBool(campaignAIAttackFlag);

	stream.writeSint32(squadRallyLocation.x);
	stream.writeSint32(squadRallyLocation.y);
	stream.writeSint32(squadRetreatLocation.x);
	stream.writeSint32(squadRetreatLocation.y);

	stream.writeUint32(placeLocations.size());
    for (const Coord& placeLocation : placeLocations) {
        stream.writeSint32(placeLocation.x);
        stream.writeSint32(placeLocation.y);
    }

    stream.writeUint8(static_cast<Uint8>(role));
    stream.writeUint32(rallySelectedCycle);
    stream.writeUint32(nonServiceConstructionOrders);
    stream.writeUint32(powerDemandSampleCycle);
    stream.writeSint32(powerDemandSample);
    stream.writeSint32(projectedPowerDemandGrowth);
    stream.writeUint32(groundSquadPhase); stream.writeUint32(groundSquadStarted);
    stream.writeUint32(groundSquadNextControl); stream.writeUint32(groundSquadInitialCount);
    stream.writeUint32(groundSquadObjective); stream.writeUint32(groundSquadObjectiveCycle);
    stream.writeUint32(static_cast<Uint32>(groundSquad.size()));
    for (const auto id:groundSquad) stream.writeUint32(id);
    auto writeMap=[&](const auto& values) {
        stream.writeUint32(static_cast<Uint32>(values.size()));
        for (const auto& entry:values) { stream.writeUint32(entry.first); stream.writeUint32(entry.second); }
    };
    writeMap(manualUnitOrders); writeMap(defenceAssignments);
    stream.writeUint32(static_cast<Uint32>(recentStructureLosses.size()));
    for (const auto& loss:recentStructureLosses) {
        stream.writeSint32(loss.location.x); stream.writeSint32(loss.location.y);
        stream.writeSint32(loss.size.x); stream.writeSint32(loss.size.y);
        stream.writeUint32(loss.cycle); stream.writeUint32(loss.item);
    }
    performanceHistory.save(stream);
    stream.writeUint32(groundSquadProgressCycle);
    stream.writeSint32(groundSquadProgressLocation.x);
    stream.writeSint32(groundSquadProgressLocation.y);
    writeMap(defenceResponseCycles);
    campaignWave.save(stream);
    stream.writeUint32(static_cast<Uint32>(scriptedAssaults.size()));
    for(auto id:scriptedAssaults) stream.writeUint32(id);
    stream.writeBool(campaignBaselineCaptured);
    stream.writeSint32(campaignBaseline.refineries);
    stream.writeSint32(campaignBaseline.allowance);
    stream.writeBool(campaignMapHasSpice);
    stream.writeUint32(campaignSpiceZeroSince);
    stream.writeUint32(static_cast<Uint32>(campaignOriginalStructures.size()));
    for(auto item:campaignOriginalStructures) stream.writeUint32(item);
    // The opening dispersal decides whether a starting unit is left where it
    // was stepped to or regrouped onto the rock the first buildings need, so a
    // save or network checkpoint taken during the opening window must carry
    // it. Serialized in sorted object-ID order: the map is unordered, and a
    // save has to be byte-identical on every client.
    stream.writeUint32(openingDispersalUntil);
    std::vector<Uint32> openingUnits;
    openingUnits.reserve(openingDispersal.size());
    for(const auto& entry:openingDispersal) openingUnits.push_back(entry.first);
    std::sort(openingUnits.begin(),openingUnits.end());
    stream.writeUint32(static_cast<Uint32>(openingUnits.size()));
    for(const auto id:openingUnits) {
        const Coord site=openingDispersal.at(id);
        stream.writeUint32(id);
        stream.writeSint32(site.x);
        stream.writeSint32(site.y);
    }
    // SAVEGAMEVERSION 9852. The posture decides whether a unit is marching home
    // or marching out, and the attrition ledger is cumulative, so neither can be
    // recomputed from the world after a load. The wave member set is a std::set,
    // which iterates in sorted id order, so these bytes are identical on every
    // client.
    stream.writeUint8(static_cast<Uint8>(armyPosture));
    stream.writeUint32(postureSince);
    stream.writeUint32(localPressureSince);
    // A sustained local withdrawal and the quiet clock both decide orders, so a
    // checkpoint mid-withdrawal has to carry them; and the adoption flag has to
    // be carried or a reload would re-adopt every hunter it already tracks.
    stream.writeUint32(localWithdrawSince);
    stream.writeUint32(lastMaterialLossCycle);
    stream.writeBool(legacyHuntersAdopted);
    stream.writeUint32(recallCursor);
    // The throttle is part of the decision, exactly as rallySelectedCycle is:
    // without it a reload would re-run the bounded rally search immediately and
    // could choose a different assembly point than the peer that did not reload.
    stream.writeUint32(protectedRallyCycle);
    stream.writeSint32(protectedRally.x);
    stream.writeSint32(protectedRally.y);
    attrition.save(stream);
    stream.writeBool(customWave.initialized);
    stream.writeUint32(customWave.opening);
    stream.writeUint32(customWave.launched);
    stream.writeUint32(customWave.lastActive);
    stream.writeUint32(customWave.front);
    stream.writeUint32(static_cast<Uint32>(customWave.members.size()));
    for (const auto id : customWave.members) stream.writeUint32(id);
    // SAVEGAMEVERSION 9862. A party's phase, its points and clocks decide every member's next
    // order and cannot be recomputed from the world; members are ordered sets, so the bytes
    // are identical on every client.
    saveHuntParties(stream);
    // SAVEGAMEVERSION 9863. The bundle point is re-chosen only every 30 s and the launch gate
    // waits at most 60 s from its first hold; both clocks decide orders, so a reload carries them.
    saveForceState(stream);
    // SAVEGAMEVERSION 9864. The remembered enemy army (the perimeter hold and turrets read it)
    // and the musters (who engages when) cannot be recomputed from the world.
    saveDefenceMemory(stream);
    // SAVEGAMEVERSION 9865. The strategy was picked under the config the game started with; a
    // reload keeps playing it whatever config is loaded then.
    stream.writeBool(strategyPicked);
    stream.writeUint8(static_cast<Uint8>(strategy.mix));
    stream.writeUint8(static_cast<Uint8>(strategy.tempo));
    // SAVEGAMEVERSION 9866: the resolved Tuned/Bounded starting ratios (all zero otherwise).
    for (const int share : strategyPrior) stream.writeUint16(static_cast<Uint16>(share));
}

// Supplemental network checkpoint state: normal disk saves intentionally rebuild these plans.
void QuantBot::saveObserverRuntime(OutputStream& s) const {
    const auto coord=[&](Coord v) { s.writeSint32(v.x); s.writeSint32(v.y); };
    s.writeUint32(lastPoliceBudgetReviewCycle);
    s.writeUint32(ixEligibleSinceCycle);
    s.writeUint32(palaceEligibleSinceCycle);
    s.writeUint32(rockSurveyCycle);
    s.writeUint32(refineryQueueSince);
    s.writeUint32(dangerUpdated);
    s.writeUint32(planningBuilder);
    s.writeUint32(placementCacheExcludedBuilder);
    s.writeUint32(cityReadyYardCount);
    s.writeSint32(availableBaseRock);
    s.writeSint32(availableBaseFootprints);
    s.writeSint32(cityBuildTimer);
    s.writeSint32(ornithopterStrikeTeam.minMembers);
    s.writeBool(planningCityProductionPlots);
    s.writeBool(baseProductionRoomBlocked);
    coord(rockExpansionSite);
    s.writeUint32(idleHarvesterCounters.size()); for(const auto& e : idleHarvesterCounters) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(harvesterMovingCounters.size()); for(const auto& e : harvesterMovingCounters) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(mcvSurveyCycles.size()); for(const auto& e : mcvSurveyCycles) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(roadRedirectRetryCycle.size()); for(const auto& e : roadRedirectRetryCycle) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(mcvExpansionSites.size()); for(const auto& e : mcvExpansionSites) { s.writeUint32(e.first); coord(e.second); }
    s.writeUint32(placementCache.size()); for(const auto& e : placementCache) { s.writeUint32(e.first); coord(e.second); }
    s.writeUint32(tacticalDanger.size()); for(auto v : tacticalDanger) s.writeUint32(v);
    s.writeUint32(harvesterDanger.size()); for(auto v : harvesterDanger) s.writeUint32(v);
    s.writeUint32(lossDanger.size()); for(auto v : lossDanger) s.writeUint32(v);
    s.writeUint32(factoryClearance().size()); for(auto v : factoryClearance()) s.writeUint32(v);
    s.writeUint32(visibleHarvestLaunchers.size()); for(auto v : visibleHarvestLaunchers) s.writeUint32(v);
    s.writeUint32(visibleEnemyBases.size()); for(auto v : visibleEnemyBases) coord(v);
    s.writeUint32(builderPlaceLocations.size()); for(const auto& e : builderPlaceLocations) { s.writeUint32(e.first); s.writeUint32(e.second.size()); for(auto v : e.second) coord(v); }
    s.writeUint32(reservedStructures.size()); for(const auto& e : reservedStructures) { s.writeUint32(e.first); s.writeUint32(e.second.item); coord(e.second.location); }
    s.writeUint32(cityProductionPlots.size()); for(const auto& e : cityProductionPlots) { s.writeUint32(e.item); coord(e.location); }
    s.writeUint32(ornithopterStrikeTeam.targetId); s.writeUint32Set(ornithopterStrikeTeam.memberIds);
    s.writeUint32(harvesterSafety.size()); for(const auto& e : harvesterSafety) { s.writeUint32(e.first); s.writeUint32(e.second.nextCheck); s.writeUint32(e.second.retreatUntil); coord(e.second.lastLocation); coord(e.second.plannedDestination); s.writeBool(e.second.controlled); }
    s.writeUint32(unsafeFields.size()); for(const auto& e : unsafeFields) { coord(e.location); s.writeUint32(e.cycle); }
    s.writeUint32(aircraftTargetAuthority.size());
    for(const auto& e : aircraftTargetAuthority) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(assetAttackerContacts.size());
    for(const auto& e : assetAttackerContacts) { s.writeUint32(e.first); s.writeUint32(e.second.cycle); s.writeBool(e.second.base); }
    // Observational allocation snapshot. A live checkpoint hands the observer the
    // exact scores and shares the producing house last used, so the Stats window
    // shows real values instead of waiting for the next production pass.
    s.writeBool(allocationSnapshot.ready);
    s.writeUint32(allocationSnapshot.cycle);
    s.writeBool(allocationSnapshot.learning);
    s.writeSint32(allocationSnapshot.militaryValue);
    s.writeSint32(allocationSnapshot.antiAirFloorBps);
    s.writeSint32(allocationSnapshot.infantryQuotaBps);
    for(const auto& slot : allocationSnapshot.slots) {
        s.writeUint32(slot.itemID);
        s.writeSint64(slot.score);
        s.writeSint32(slot.targetBps);
        s.writeBool(slot.allocated);
    }
}

void QuantBot::loadObserverRuntime(InputStream& s) {
    const auto count=[&]() { auto n=s.readUint32(); if(n>262144) throw std::runtime_error("Oversized spectator AI state"); return n; };
    const auto coord=[&]() { Coord v; v.x=s.readSint32(); v.y=s.readSint32(); return v; };
    lastPoliceBudgetReviewCycle=s.readUint32();
    ixEligibleSinceCycle=s.readUint32();
    palaceEligibleSinceCycle=s.readUint32();
    rockSurveyCycle=s.readUint32();
    refineryQueueSince=s.readUint32();
    dangerUpdated=s.readUint32();
    planningBuilder=s.readUint32();
    placementCacheExcludedBuilder=s.readUint32();
    cityReadyYardCount=s.readUint32();
    availableBaseRock=s.readSint32();
    availableBaseFootprints=s.readSint32();
    cityBuildTimer=s.readSint32();
    ornithopterStrikeTeam.minMembers=s.readSint32();
    planningCityProductionPlots=s.readBool();
    baseProductionRoomBlocked=s.readBool();
    rockExpansionSite=coord();
    idleHarvesterCounters.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); idleHarvesterCounters[id]=s.readUint32(); }
    harvesterMovingCounters.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); harvesterMovingCounters[id]=s.readUint32(); }
    mcvSurveyCycles.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); mcvSurveyCycles[id]=s.readUint32(); }
    roadRedirectRetryCycle.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); roadRedirectRetryCycle[id]=s.readUint32(); }
    mcvExpansionSites.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); mcvExpansionSites[id]=coord(); }
    placementCache.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); placementCache[id]=coord(); }
    tacticalDanger.clear(); for(Uint32 n=count(); n; --n) tacticalDanger.push_back(s.readUint32());
    harvesterDanger.clear(); for(Uint32 n=count(); n; --n) harvesterDanger.push_back(s.readUint32());
    lossDanger.clear(); for(Uint32 n=count(); n; --n) lossDanger.push_back(s.readUint32());
    factoryEnemyClearance.clear(); factoryEnemyClearanceStale=false; for(Uint32 n=count(); n; --n) factoryEnemyClearance.push_back(s.readUint32());
    visibleHarvestLaunchers.clear(); for(Uint32 n=count(); n; --n) visibleHarvestLaunchers.push_back(s.readUint32());
    visibleEnemyBases.clear(); for(Uint32 n=count(); n; --n) visibleEnemyBases.push_back(coord());
    builderPlaceLocations.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& list=builderPlaceLocations[id]; for(Uint32 m=count(); m; --m) list.push_back(coord()); }
    reservedStructures.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& e=reservedStructures[id]; e.item=s.readUint32(); e.location=coord(); }
    cityProductionPlots.clear(); for(Uint32 n=count(); n; --n) { PlannedStructure e; e.item=s.readUint32(); e.location=coord(); cityProductionPlots.push_back(e); }
    ornithopterStrikeTeam.targetId=s.readUint32(); ornithopterStrikeTeam.memberIds.clear(); for(Uint32 n=count(); n; --n) ornithopterStrikeTeam.memberIds.insert(s.readUint32());
    harvesterSafety.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& e=harvesterSafety[id]; e.nextCheck=s.readUint32(); e.retreatUntil=s.readUint32(); e.lastLocation=coord(); e.plannedDestination=coord(); e.controlled=s.readBool(); }
    unsafeFields.clear(); for(Uint32 n=count(); n; --n) { UnsafeField e; e.location=coord(); e.cycle=s.readUint32(); unsafeFields.push_back(e); }
    aircraftTargetAuthority.clear();
    for(Uint32 n=count(); n; --n) { const auto id=s.readUint32(); aircraftTargetAuthority[id]=s.readUint32(); }
    assetAttackerContacts.clear();
    for(Uint32 n=count(); n; --n) { const auto id=s.readUint32(); AssetContact e; e.cycle=s.readUint32(); e.base=s.readBool(); assetAttackerContacts[id]=e; }
    allocationSnapshot.ready=s.readBool();
    allocationSnapshot.cycle=s.readUint32();
    allocationSnapshot.learning=s.readBool();
    allocationSnapshot.militaryValue=s.readSint32();
    allocationSnapshot.antiAirFloorBps=s.readSint32();
    allocationSnapshot.infantryQuotaBps=s.readSint32();
    for(auto& slot : allocationSnapshot.slots) {
        slot.itemID=s.readUint32();
        slot.score=s.readSint64();
        slot.targetBps=s.readSint32();
        slot.allocated=s.readBool();
    }
}
