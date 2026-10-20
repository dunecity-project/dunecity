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

// QuantBot harvesters: harvester safety and harvester strike telemetry.

#include "QuantBotInternal.h"

bool QuantBot::manageHarvesterSafety(const Harvester* harvester, SpiceFieldCache* spiceCache) {
    const Uint32 now = getGameCycleCount();
    auto& state = harvesterSafety[harvester->getObjectID()];
    const bool newHarvester = state.lastLocation.isInvalid();
    state.lastLocation = harvester->getLocation();
    if (now < state.nextCheck) return state.controlled;
    state.nextCheck = now + MILLI2CYCLES(2000);
    AITelemetry::PerformanceScope safetyScope("ai.harvesterSafety",
        getGameCycleCount(), getHouse()->getHouseID());
    const Coord origin = harvester->getLocation(), destination = harvester->getDestination();
    auto distance = [](Coord a, Coord b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); };
    const int mapW = getMap().getSizeX(), mapH = getMap().getSizeY();
    // harvesterDanger's size stays a live test: scrambleUnitsAndDefend() below can refresh it.
    auto danger = [&](Coord p) {
        return p.isValid() && p.x < mapW && p.y < mapH
            && harvesterDanger.size() == static_cast<size_t>(mapW*mapH)
            ? harvesterDanger[p.y*mapW+p.x] : 0;
    };
    const bool threatened = danger(origin) > 0;
    // Remember evacuations before the first hit too. Losing sight of an enemy
    // must not immediately make its spice field attractive again.
    auto rememberUnsafe = [&](Coord p) {
        if (!p.isValid() || danger(p)==0) return;
        for (auto& field : unsafeFields) if (distance(p,field.location)<=3) {
            field.cycle=now; return;
        }
        unsafeFields.push_back({p,now});
    };
    rememberUnsafe(origin);
    rememberUnsafe(destination);
    // Recently evacuated fields cool down for two minutes, even under fog.
    auto memoryPenalty = [&](Coord p) {
        int penalty = 0;
        for (const auto& field : unsafeFields)
            if (distance(p,field.location) <= 6 && now-field.cycle < MILLI2CYCLES(120000))
                penalty = std::max(penalty, 12-static_cast<int>((now-field.cycle)/MILLI2CYCLES(10000)));
        return penalty;
    };
    auto routeSafe = [&](Coord end) {
        return TacticalSafetyPolicy::escapeCorridor(origin.x,origin.y,end.x,end.y,
            [&](int x,int y) { return danger(Coord(x,y)); });
    };
    // Ask for help before the first missile lands. Use the cached visible
    // launcher list, and one nearest threat per check; incident debounce and
    // already-committed forces keep many harvesters from recruiting repeatedly.
    const ObjectBase* clearingTarget=nullptr;
    int nearestThreat=std::numeric_limits<int>::max();
    for (Uint32 id:visibleHarvestLaunchers) {
        const auto* enemy=getObject(id);
        if (!enemy || enemy->getHealth()<=0 || !enemy->canAttack(harvester)
            || !enemy->isVisible(getHouse()->getTeamID())
            || enemy->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const int radius=TacticalSafetyPolicy::harvesterThreatRadius(enemy->getItemID(),enemy->getWeaponRange());
        const int fromHarvester=distance(origin,enemy->getLocation());
        const int fromJob=destination.isValid() ? distance(destination,enemy->getLocation()) : fromHarvester;
        if (std::min(fromHarvester,fromJob)>radius || fromHarvester>radius+6) continue;
        if (fromHarvester<nearestThreat) { clearingTarget=enemy; nearestThreat=fromHarvester; }
    }
    if (clearingTarget) scrambleUnitsAndDefend(clearingTarget,true);
    // Leave an established safe unloading trip alone when its next job is safe.
    if (harvester->isReturning() && harvester->getAmountOfSpice()>0) {
        const auto* target=dynamic_cast<const StructureBase*>(harvester->getTarget());
        const Coord job=harvester->getGuardPoint();
        if (target && target->getOwner()==getHouse() && target->getHealth()>0
            && target->acceptsHarvesterDropoff() && danger(target->getClosestPoint(origin))==0
            && routeSafe(target->getClosestPoint(origin))
            && (harvester->getAttackMode()==STOP
                || (job.isValid() && danger(job)==0 && memoryPenalty(job)==0 && routeSafe(job)))) {
            state.controlled=true;
            return true;
        }
    }
    // Do not hold a safe vehicle after enemies leave. Keep its existing safe job.
    if (!threatened && !harvester->isReturning() && destination.isValid()
        && danger(destination)==0 && memoryPenalty(destination)==0
        && harvester->getAttackMode() != STOP && !newHarvester && routeSafe(destination)) {
        state.controlled = false;
        state.retreatUntil = 0;
        return false;
    }
    state.controlled = true;
    std::vector<Coord> peers;
    for (const auto* unit : getUnitList())
        if (unit != harvester && unit->isActive() && unit->getOwner() == getHouse()
            && unit->getItemID() == Unit_Harvester)
        {
            const auto peer = harvesterSafety.find(unit->getObjectID());
            const Coord reserved = peer != harvesterSafety.end() && peer->second.controlled
                ? peer->second.plannedDestination : Coord::Invalid();
            peers.push_back(reserved.isValid() ? reserved
                : unit->getDestination().isValid() ? unit->getDestination() : unit->getLocation());
        }
    // crowdPenalty summed over every peer for every safe-spice candidate, and
    // the candidate sweep below is the whole map. Stamping each peer's 7x7
    // Chebyshev falloff once makes the query O(1) for the same total: the
    // addends are the same integers, added in a different order, and integer
    // addition is commutative, so scores and therefore tie-breaks are
    // bit-identical. Built here, after peers are gathered, so the reservations
    // earlier harvesters just wrote into plannedDestination are included
    // exactly as before; nothing is retained across harvester decisions.
    DuneCity::ChebyshevWeightField crowdField(3, 12);
    crowdField.build(peers, mapW, mapH);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "harvester.crowd_peers",static_cast<int64_t>(peers.size()),-1,false);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "harvester.crowd_cells",static_cast<int64_t>(crowdField.storedCells()),-1,false);
    auto crowdPenalty = [&](Coord candidate) { return crowdField.at(candidate.x,candidate.y); };
    Coord best = Coord::Invalid();
    int bestScore = std::numeric_limits<int>::max();
    int safeFields = 0, rejectedRoutes = 0;
    // Spice membership is the only thing shared between the harvesters of one
    // checkAllUnits() pass; it is a property of the tile, not of a harvester.
    // Everything that genuinely varies per harvester or changes as earlier
    // harvesters are ordered — danger, remembered unsafe fields, passability,
    // routes, peers and their plannedDestination reservations — is still
    // evaluated per candidate below, in the original order. The list is built in
    // the same y-then-x order the scan used, and holds exactly the tiles the
    // scan's hasSpice() test admitted, so the visit order, the remaining
    // predicate order and the safeFields/rejectedRoutes counts are unchanged.
    SpiceFieldCache localSpice;
    SpiceFieldCache& spice = spiceCache ? *spiceCache : localSpice;
    if (!spice.valid) {
        AITelemetry::PerformanceScope spiceScope("ai.harvester.spice_list",
            getGameCycleCount(), getHouse()->getHouseID());
        spice.tiles.clear();
        // Tiles are stored column by column: read them in that order, then list
        // the spice tiles in the scan's y-then-x order.
        std::vector<uint8_t> spiceAt(static_cast<size_t>(mapW)*mapH, 0);
        for (int x = 0; x < mapW; ++x) for (int y = 0; y < mapH; ++y)
            spiceAt[static_cast<size_t>(y)*mapW+x] = getMap().getTile(x,y)->hasSpice();
        for (int y = 0; y < mapH; ++y) for (int x = 0; x < mapW; ++x)
            if (spiceAt[static_cast<size_t>(y)*mapW+x]) spice.tiles.emplace_back(x,y);
        spice.valid = true;
        ++spice.builds;
    } else {
        ++spice.hits;
    }
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "harvester.spice_tiles",static_cast<int64_t>(spice.tiles.size()),-1,false);
    // memoryPenalty(p) > 0 exactly when some field that is still cooling down
    // with a positive penalty lies within 6 tiles: stamp those once instead of
    // walking every remembered field for every spice tile. unsafeFields does not
    // change during the sweep.
    std::vector<uint8_t> remembered(static_cast<size_t>(mapW)*mapH, 0);
    for (const auto& field : unsafeFields) {
        if (now-field.cycle >= MILLI2CYCLES(120000)
            || 12-static_cast<int>((now-field.cycle)/MILLI2CYCLES(10000)) <= 0) continue;
        const Coord at = field.location;
        for (int y = std::max(0,at.y-6); y <= std::min(mapH-1,at.y+6); ++y)
            for (int x = std::max(0,at.x-6); x <= std::min(mapW-1,at.x+6); ++x)
                remembered[static_cast<size_t>(y)*mapW+x] = 1;
    }
    for (const Coord candidate : spice.tiles) {
        if (danger(candidate)>0 || remembered[static_cast<size_t>(candidate.y)*mapW+candidate.x]
            || !harvester->canPass(candidate.x,candidate.y)) continue;
        ++safeFields;
        const int score = distance(origin,candidate)*3 + crowdPenalty(candidate);
        if (score >= bestScore) continue;
        if (!routeSafe(candidate)) { ++rejectedRoutes; continue; }
        bestScore = score; best = candidate;
    }
    const bool foundSpice = best.isValid();
    // A partial load can continue harvesting elsewhere. Only unload a full load,
    // finish a real return trip, or salvage cargo when no safe field exists.
    if (TacticalSafetyPolicy::needsRefineryRefuge(harvester->isReturning(),
            harvester->getAmountOfSpice()>=HARVESTERMAXSPICE,
            harvester->getAmountOfSpice()>0,foundSpice)) {
        const StructureBase* refuge = nullptr;
        int bestRefineryScore = std::numeric_limits<int>::max();
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
                || !structure->acceptsHarvesterDropoff()) continue;
            const Coord entry = structure->getClosestPoint(origin);
            if (danger(entry) > 0 || !routeSafe(entry)) continue;
            const int score = distance(origin,entry) + structure->getHarvesterDropoffBookings()*3;
            if (score < bestRefineryScore) { refuge=structure; bestRefineryScore=score; }
        }
        if (harvester->isReturning()) {
            const auto* target=dynamic_cast<const StructureBase*>(harvester->getTarget());
            if (target && target->getOwner()==getHouse() && target->getHealth()>0
                && target->acceptsHarvesterDropoff() && danger(target->getClosestPoint(origin))==0
                && routeSafe(target->getClosestPoint(origin))) refuge=target;
        }
        if (refuge) {
            state.controlled = true;
            state.plannedDestination = refuge->getLocation();
            // Replace the remembered job before unloading, so deploy/carryall
            // return resumes at the alternate field rather than the old one.
            const Coord job=harvester->getGuardPoint();
            const bool replaceJob=job.isInvalid() || danger(job)>0 || memoryPenalty(job)>0 || !routeSafe(job);
            if (replaceJob) {
                doSetAttackMode(harvester,foundSpice ? HARVEST : STOP);
                if (foundSpice) doMove2Pos(harvester,best.x,best.y,false);
            }
            if (harvester->getTarget() != refuge) {
                doMove2Object(harvester,refuge);
                traceDecision("harvester_safety",AITelemetry::Record().set("object",harvester->getObjectID())
                    .set("action","retreat_refinery").set("refinery",refuge->getObjectID())
                    .set("x",origin.x).set("y",origin.y).set("cargo",harvester->getAmountOfSpice().lround()));
            }
            return true;
        }
    }

    // If no safe spice corridor exists, disperse to the nearest safe open tile.
    // There is deliberately no base-centre attraction.
    if (!foundSpice) {
        for (int y = std::max(0,origin.y-20); y <= std::min(mapH-1,origin.y+20); ++y)
            for (int x = std::max(0,origin.x-20); x <= std::min(mapW-1,origin.x+20); ++x) {
                const Coord candidate(x,y);
                if (danger(candidate)>0 || !harvester->canPass(x,y)) continue;
                const int score = distance(origin,candidate)*3 + crowdPenalty(candidate);
                if (score < bestScore && routeSafe(candidate)) { bestScore=score; best=candidate; }
            }
    }
    state.plannedDestination = best; // reserve immediately, before queued movement commands execute
    if (best.isValid()) {
        const auto mode = foundSpice ? HARVEST : STOP;
        if (best != destination || harvester->getAttackMode() != mode) {
            doSetAttackMode(harvester, mode);
            doMove2Pos(harvester,best.x,best.y,!foundSpice);
        }
    } else doSetAttackMode(harvester, STOP);
    state.retreatUntil = 0;
    // Retrying the same evacuation route is execution noise. Preserve every
    // distinct safe-field decision, which is what later tuning can use.
    const uint64_t safetySignature = (uint64_t(foundSpice) << 32)
        | (uint64_t(best.x & 0xffff) << 16) | uint64_t(best.y & 0xffff);
    if (lastHarvesterSafetyTrace[harvester->getObjectID()] != safetySignature) {
        traceDecision("harvester_safety", AITelemetry::Record().set("object",harvester->getObjectID())
            .set("action",foundSpice ? "redirect_spice" : best.isValid() ? "disperse" : "no_safe_route")
            .set("x",origin.x).set("y",origin.y).set("destination_x",best.x).set("destination_y",best.y)
            .set("danger",danger(origin)).set("old_destination_danger",danger(destination))
            .set("safe_fields",safeFields).set("rejected_routes",rejectedRoutes)
            .set("memory_penalty",best.isValid()?memoryPenalty(best):0)
            .set("crowding_penalty",best.isValid()?crowdPenalty(best):0)
            .set("cargo",harvester->getAmountOfSpice().lround()));
        lastHarvesterSafetyTrace[harvester->getObjectID()] = safetySignature;
    }
    return true;
}

void QuantBot::updateHarvesterStrikeTelemetry(bool final) {
    AITelemetry::PerformanceScope perfScope("ai.updateHarvesterStrikeTelemetry", getGameCycleCount(), getHouse()->getHouseID());
    if (!AITelemetry::log().enabled()) { harvesterStrikeTraces.clear(); return; }
    const Uint32 now=getGameCycleCount();
    for (auto it=harvesterStrikeTraces.begin(); it!=harvesterStrikeTraces.end();) {
        auto& raid=*it;
        const auto* target=currentGame->getObjectManager().getObject(raid.target);
        const bool visible=target && target->isVisible(getHouse()->getTeamID());
        if (visible) raid.lastVisible=now;
        // A strike may outlive the initial forced attack command. Only abandon
        // a visible harvester when it moves under meaningful protection; a
        // harmless command expiry must reissue the same target instead.
        int targetDefence = 0;
        bool protectedByTurret = false;
        if (visible && target && target->isActive()) {
            for (const auto* building : getStructureList()) {
                if (building->getOwner()->getTeamID() != target->getOwner()->getTeamID()
                    || !building->isVisible(getHouse()->getTeamID()) || !building->canAttack()) continue;
                if (blockDistance(target->getLocation(), building->getLocation())
                    <= building->getWeaponRange() + 3) {
                    protectedByTurret = true;
                    break;
                }
            }
            if (!protectedByTurret) for (const auto* guard : getUnitList()) {
                if (!guard->isActive() || !guard->canAttack() || !guard->isVisible(getHouse()->getTeamID())
                    || guard->getOwner()->getTeamID() != target->getOwner()->getTeamID()) continue;
                if (blockDistance(target->getLocation(), guard->getLocation())
                    <= std::max(6, guard->getWeaponRange() + 2))
                    targetDefence += currentGame->objectData.data[guard->getItemID()][guard->getOriginalHouseID()].price;
            }
        }
        const bool targetDefended = protectedByTurret || targetDefence > 2000;
        int alive=0, lost=0, captured=0, lostValue=0;
        bool engaged=false, stillAssigned=false;
        int64_t damage=0, bonus=0;
        AITelemetry::Record members;
        for (const auto& member : raid.members) {
            const auto* unit=dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(member.id));
            const bool dead=!unit || unit->getHealth()<=0;
            const bool changedOwner=unit && !dead && unit->getOwner()!=getHouse();
            if (dead) { ++lost; lostValue+=member.price; }
            else if (changedOwner) ++captured;
            else {
                ++alive;
                const auto* currentTarget=unit->getTarget();
                stillAssigned |= currentTarget && currentTarget->getObjectID()==raid.target;
                engaged |= currentTarget && unit->isActive()
                    && blockDistance(unit->getLocation(),currentTarget->getLocation())<=unit->getWeaponRange();
            }
            damage+=member.reward.damageMilli; bonus+=member.reward.killBonusMilli;
            members.set(std::to_string(member.id),AITelemetry::Record().set("item",member.item)
                .set("lost",dead).set("captured",changedOwner).set("damage_value_milli",member.reward.damageMilli)
                .set("kill_bonus_milli",member.reward.killBonusMilli).set("reward_milli",member.reward.total()));
        }
        int retargeted = 0;
        // Observation only: squad orders must not depend on telemetry being enabled.
        (engaged ? raid.engagementCycles : raid.transitCycles) += now-raid.sampled;
        raid.sampled=now;
        const char* outcome=nullptr;
        if (raid.targetKilledByStrike) outcome="target_killed_by_strike";
        else if (!target || target->getHealth()<=0) outcome="target_removed";
        else if (target->getOwner()->getTeamID()==getHouse()->getTeamID()) outcome="target_captured";
        else if (alive==0) outcome="force_lost_or_captured";
        else if (!visible && now-raid.lastVisible>=MILLI2CYCLES(15000)) outcome="target_lost_contact";
        else if (visible && !target->isActive()) outcome="target_transport_or_inactive";
        else if (targetDefended) outcome="target_entered_defended_area";
        else if (!stillAssigned && now-raid.start>=MILLI2CYCLES(5000)) outcome="attackers_unavailable_target_survived";
        else if (now-raid.start>=MILLI2CYCLES(120000)) outcome="timeout_target_survived";
        if (!outcome && final) outcome="game_ended_while_active";
        if (outcome || now-raid.logged>=MILLI2CYCLES(5000)) {
            traceDecision(outcome ? "harvester_strike_outcome" : "harvester_strike_progress",AITelemetry::Record()
                .set("raid_id",raid.id).set("target",raid.target).set("outcome",outcome ? outcome : "active")
                .set("target_visible",visible).set("target_present",target!=nullptr)
                .set("elapsed_cycles",now-raid.start).set("transit_cycles",raid.transitCycles)
                .set("engagement_cycles",raid.engagementCycles).set("survivors",alive).set("losses",lost)
                .set("captured",captured).set("lost_value",lostValue).set("damage_value_milli",damage)
                .set("kill_bonus_milli",bonus).set("reward_milli",damage+bonus).set("members",members)
                .set("target_defence",targetDefence).set("target_turret_covered",protectedByTurret)
                .set("retargeted_members",retargeted));
            raid.logged=now;
        }
        if (outcome) it=harvesterStrikeTraces.erase(it); else ++it;
    }
}
