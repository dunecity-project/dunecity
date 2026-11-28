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

// Mass Carryall manoeuvres (docs/formations.md, "Mass Carryall manoeuvres"). Four default-OFF
// switches in [Force Concentration], Custom games only:
//   AirliftRearDrop     part of a launched wave is flown to the far side of the enemy base
//   AirliftFlankDrop    the same, to one side of the enemy base
//   AirliftRetreatLift  a wave outmatched where it stands is flown home, most valuable first
//   AirliftRescue       a damaged or cut-off vehicle under enemy fire is flown home
//   AirliftFerry        wave members far behind the wave's front are flown up to it
// Second set (2026-11-23):
//   AirliftReserve          the newest Carryalls are the army's: no harvester or repair work
//   SquadRetreat            a wave outmatched where it stands falls back to the protected rally
//   SquadRetreatAirlift     a withdrawing unit far from its rally takes a free Carryall
//   AirliftHarvesterDrop    free units are flown beside an enemy harvester no enemy weapon covers
//                           (DEFAULT ON in the scope of airliftHarvesterDropActive(); the only one)
//   AirliftPerimeterDrop    the same, beside an enemy building no enemy weapon covers
//   AirliftBaseDefence      a distant defender of an attacked building is flown to the attacker
//   AirliftHarvesterDefence the same for an attacked harvester
// A Carryall carries one unit, so a drop is flown in sorties: each pass orders as many units as
// there are free Carryalls. Every lift is the engine's own Carryall request (a forced move to
// the drop point, then GroundUnit::requestCarryall), so pickup, flight and landing are the code
// a player's drop order runs. A unit is only ever ordered while a Carryall is free to book: one
// without a booking would drive to the drop point by itself.

#include "QuantBotInternal.h"

#include <players/AIDecisionLog.h>
#include <players/QuantBotConfig.h>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <globals.h>
#include <sand.h>
#include <structures/StructureBase.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <units/MCV.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <structures/StarPort.h>
#include <players/QuantBotBuildPass.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

using QuantBotInternal::mobileCombatItem;

namespace {
constexpr int kDropNearest = 8;        ///< Nearest a rear or flank drop is looked for, in tiles from the base centre.
constexpr int kDropFarthest = 32;      ///< Farthest; beyond it the group would arrive later than the wave.
constexpr int kStrikeWaveTiles = 18;   ///< The wave this close to the base: the landed group strikes with it.
constexpr int kMinDropTrip = 30;       ///< A shorter trip is driven: the wave is there as soon.
constexpr int kMinDropGroup = 3;       ///< Fewer units behind a base are fed to it one by one.
constexpr int kMinDropCarryalls = 1;   ///< Free Carryalls a drop needs before it is planned (it shuttles).
constexpr int kRescueHomeTiles = 15;   ///< Nearer home than this a unit is not flown out.
constexpr int kRescueAloneTiles = 6;   ///< No friendly fighter this close: the unit is cut off.
constexpr int kRescueOrdersPerPass = 2;
constexpr int kFerryGap = 25;          ///< Tiles behind the wave's front before a member is flown up.
constexpr int kFerryMinWave = 6;       ///< A smaller wave has no front to speak of.
constexpr int kRetreatLiftTiles = 20;  ///< A shorter withdrawal is driven.
constexpr int kDefenceLiftTiles = 20;  ///< A nearer defender drives to the attacker.
constexpr int kRaidHomeTiles = 30;     ///< A raid takes free units standing this close to the base centre.
constexpr int kRaidNextTiles = 20;     ///< A landed raider's next target is looked for this far around it.
constexpr int kEconomyCarryalls = 2;   ///< Carryalls a reserve always leaves to harvesters and repairs.

int unitPrice(const UnitBase* unit) {
    return currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price;
}

/// A vehicle a Carryall manoeuvre may move: a live, standing fighter that nothing else carries.
bool liftable(const UnitBase* unit, const House* house) {
    if (!unit || unit->getOwner() != house || !unit->isAGroundUnit() || unit->isInfantry()) return false;
    if (!mobileCombatItem(unit->getItemID()) || !unit->isActive() || unit->isPickedUp() || unit->getHealth() <= 0) return false;
    const auto* ground = static_cast<const GroundUnit*>(unit);
    if (ground->isAwaitingPickup() || ground->hasBookedCarrier()) return false;
    const auto* target = unit->getTarget();
    return !(target && target->getItemID() == Structure_RepairYard && target->getOwner() == house);
}
} // namespace

bool QuantBot::airliftActive() const {
    const auto& force = botConfig().force;
    return gameMode == GameMode::Custom
        && (force.airliftRearDrop || force.airliftFlankDrop || force.airliftRetreatLift || force.airliftRescue
            || force.airliftFerry || force.squadRetreatAirlift || airliftHarvesterDropActive()
            || force.airliftPerimeterDrop || force.airliftBaseDefence || force.airliftHarvesterDefence || force.airliftColonists);
}

bool QuantBot::airliftable(const UnitBase* unit) const { return liftable(unit, getHouse()); }

bool QuantBot::airliftHarvesterDropActive() const {
    const auto& force = botConfig().force;
    if (!force.airliftHarvesterDrop || gameMode != GameMode::Custom) return false;
    if (force.airliftHarvesterDropEverywhere) return true;
    // The default's scope is where the raid was measured to win (docs/formations.md, "Mass
    // Carryall manoeuvres", rounds 7 to 9): vanilla Construction Yard starts, 1v1 Hard +0.54 and
    // Brutal +0.46 over 192 games each, level in 4-player free-for-alls. From a Fortress start
    // it was level with negative estimates (-0.19, -0.18, -0.08), and city games from a
    // Construction Yard start were not played, so neither gets it.
    return autonomousCustomArmy() && (difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
        && !isAlliedWithHuman() && currentGame && !currentGame->isCitySimEnabled()
        && initialMilitaryValue >= 0 && initialItemCount[Structure_Refinery] == 0;
}

bool QuantBot::squadRetreatActive() const {
    return botConfig().force.squadRetreat && gameMode == GameMode::Custom;
}

bool QuantBot::airliftReserves(const UnitBase* carrier, const GroundUnit* passenger) const {
    const int reserve = botConfig().force.airliftReserve;
    if (reserve <= 0 || gameMode != GameMode::Custom || !carrier || !passenger) return false;
    // A manoeuvre's own unit may take any Carryall.
    if (airliftBooking == passenger->getObjectID() || airliftJobs.count(passenger->getObjectID())) return false;
    // The newest Carryalls are the army's, as long as two are left to the economy. Derived
    // from the fleet as it stands, so nothing about it needs saving.
    int owned = 0, newer = 0;
    for (const Uint32 id : currentGame->getCarryallCandidateIds()) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getOwner() != getHouse() || unit->getItemID() != Unit_Carryall || unit->getHealth() <= 0) continue;
        ++owned;
        if (id > carrier->getObjectID()) ++newer;
    }
    return newer < std::min(reserve, owned - kEconomyCarryalls);
}

int QuantBot::airliftFreeCarryalls() const {
    int free = 0;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || unit->getItemID() != Unit_Carryall || !unit->isActive()
            || unit->getHealth() <= 0) continue;
        const auto* carryall = static_cast<const Carryall*>(unit);
        if (!carryall->isBooked() && !carryall->isDropOfferer()) ++free;
    }
    return free;
}

bool QuantBot::orderAirlift(const UnitBase* unit, Coord drop, AirliftKind kind, Uint32 objective) {
    if (!unit || !unit->isAGroundUnit() || !getMap().tileExists(drop)) return false;
    if (kind==AirliftKind::Colonist && (!botConfig().force.airliftColonists
        || !getGameInitSettings().getGameOptions().manualCarryallDrops || unit->getItemID()!=Unit_MCV)) return false;
    const auto* ground = static_cast<const GroundUnit*>(unit);
    const ATTACKMODE before = unit->getAttackMode();
    doMove2Pos(unit, drop.x, drop.y, true);
    airliftBooking = unit->getObjectID();   // it may book a reserved Carryall
    const bool booked = doRequestCarryallDrop(ground) && ground->hasBookedCarrier();
    airliftBooking = NONE_ID;
    if (!booked) {
        // No aircraft after all. It must not set off for the drop point on the ground.
        const_cast<GroundUnit*>(ground)->cancelCarryallPickup();
        doMove2Pos(unit, unit->getLocation().x, unit->getLocation().y, false);
        doSetAttackMode(unit, before == CARRYALLREQUESTED ? AREAGUARD : before);
        return false;
    }
    AirliftJob job;
    job.kind = kind;
    job.drop = drop;
    job.ordered = getGameCycleCount();
    job.objective = objective;
    airliftJobs[unit->getObjectID()] = job;
    ++airliftStats.ordered;
    return true;
}

void QuantBot::releaseAirliftJob(const UnitBase* unit, const AirliftJob& job) {
    if (!unit || !unit->isActive() || unit->isPickedUp()) return;
    const bool strike = job.kind == AirliftKind::Rear || job.kind == AirliftKind::Flank;
    if (strike && airliftDrop.target.isValid()) doHuntToward(unit, airliftDrop.target.x, airliftDrop.target.y);
    else if (strike) doSetAttackMode(unit, HUNT);
    // A unit flown home keeps the Area Guard the Carryall gave it; the next wave takes it along.
}

Coord QuantBot::airliftHome() {
    Coord home = findProtectedRallyLocation();
    if (home.isInvalid() || !getMap().tileExists(home)) home = findBaseCentre(getHouse()->getHouseID());
    return home;
}

void QuantBot::trackAirliftJobs() {
    const Uint32 now = getGameCycleCount();
    for (auto it = airliftJobs.begin(); it != airliftJobs.end();) {
        AirliftJob& job = it->second;
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(it->first));
        if (job.kind==AirliftKind::ColonyTurret) {
            const auto* supplier=dynamic_cast<const BuilderBase*>(getObject(job.objective));
            if (!supplier || supplier->getOwner()!=getHouse() || supplier->getHealth()<=0
                || supplier->getProductionQueueSize()==0) {
                SDL_Log("COLONIST: house=%d event=turret_unavailable mcv=%u cycle=%u", getHouse()->getHouseID(),it->first,now);
                releaseColonistTurret(job);it=airliftJobs.erase(it);
            } else ++it;
            continue;
        }
        if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isAGroundUnit()) {
            if (job.kind==AirliftKind::Colonist || job.kind==AirliftKind::ColonistGround || job.kind==AirliftKind::ColonistPreparing) {
                SDL_Log("COLONIST: house=%d event=lost mcv=%u air=%d x=%d y=%d cycle=%u",getHouse()->getHouseID(),
                    it->first,int(job.lifted),job.drop.x,job.drop.y,now);
                releaseColonistTurret(job);
            }
            if (job.lifted && !job.landed) ++airliftStats.lostInLift;
            else if (!job.lifted && job.kind!=AirliftKind::ColonistGround && job.kind!=AirliftKind::ColonistPreparing) ++airliftStats.lostWaiting;
            it = airliftJobs.erase(it);
            continue;
        }
        if (job.kind==AirliftKind::ColonistGround || job.kind==AirliftKind::ColonistPreparing) {
            // Automatic engine rescue remains possible with manual drops off; count actual transport,
            // not merely this controller's booking, rather than mislabel a carried MCV as driven.
            if (unit->isPickedUp() && !job.lifted) {
                job.lifted=true;
                SDL_Log("COLONIST: house=%d event=automatic_lift mcv=%u drops=%d cycle=%u",getHouse()->getHouseID(),
                    it->first,int(getGameInitSettings().getGameOptions().manualCarryallDrops),now);
            }
            if (job.lifted && !job.landed && unit->isActive() && !unit->isPickedUp()) {
                const bool awayArrival=blockDistance(unit->getLocation(),job.drop)<=3;
                job.lifted=awayArrival;job.landed=awayArrival;
                SDL_Log("COLONIST: house=%d event=automatic_landed mcv=%u away=%d cycle=%u",getHouse()->getHouseID(),it->first,int(awayArrival),now);
            }
            ++it;continue;
        }
        const auto* ground = static_cast<const GroundUnit*>(unit);
        if (unit->isPickedUp()) {
            if (!job.lifted) {
                job.lifted = true; ++airliftStats.lifted;
                if (job.kind==AirliftKind::Colonist)
                    SDL_Log("COLONIST: house=%d event=lifted mcv=%u cycle=%u",getHouse()->getHouseID(),it->first,now);
            }
            ++it;
            continue;
        }
        if (job.lifted && !job.landed && unit->isActive()) {
            job.landed = true;
            ++airliftStats.landed;
            if (job.kind==AirliftKind::Colonist) {
                SDL_Log("COLONIST: house=%d event=landed mcv=%u x=%d y=%d target_x=%d target_y=%d cycle=%u",
                    getHouse()->getHouseID(),it->first,unit->getX(),unit->getY(),job.drop.x,job.drop.y,now);
                job.kind=AirliftKind::ColonistGround;
                const_cast<UnitBase*>(unit)->setForced(false);
                ++it;
            } else if (job.kind == AirliftKind::Rear || job.kind == AirliftKind::Flank) {
                // The landed part of a drop waits for the rest (updateAirliftDrop strikes).
                if (airliftDrop.firstLanding == 0) airliftDrop.firstLanding = now;
                ++it;
            } else if (job.kind == AirliftKind::HarvesterRaid || job.kind == AirliftKind::PerimeterRaid) {
                ++it;   // held while it raids (continueAirliftRaid, below, from the next pass)
            } else {
                // Flown up to the wave: it hunts on with it. Flown home: it keeps its Area Guard.
                if (job.kind == AirliftKind::Ferry) doSetAttackMode(unit, HUNT);
                if (job.kind == AirliftKind::Defence) {
                    // Set down beside the fight: the same order and booking a driven defender gets.
                    const auto* aim = getObject(job.objective);
                    if (aim && aim->getHealth() > 0 && aim->getOwner()
                        && aim->getOwner()->getTeamID() != getHouse()->getTeamID()) {
                        const_cast<UnitBase*>(unit)->setGuardPoint(unit->getLocation());
                        doAttackObject(unit, aim, !unit->isInAttackRange(aim));
                        defenceAssignments[unit->getObjectID()] = aim->getObjectID();
                    }
                }
                it = airliftJobs.erase(it);
            }
            continue;
        }
        if (job.landed) {
            if ((job.kind == AirliftKind::HarvesterRaid || job.kind == AirliftKind::PerimeterRaid)
                && unit->isActive() && !continueAirliftRaid(unit, job)) {
                it = airliftJobs.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        // Not lifted yet. A lost booking (the aircraft was shot down or took other work) is
        // rebooked at once or given up: it never drives on alone. Ninety seconds bound a
        // unit that sits in a repair bay or waits for an aircraft that does not arrive.
        const bool waiting = unit->isActive() && (ground->isAwaitingPickup() || ground->hasBookedCarrier());
        const bool overdue = now - job.ordered > MILLI2CYCLES(90000);
        if (unit->isActive() && (!waiting || overdue)) {
            airliftBooking = unit->getObjectID();
            const bool rebooked = !overdue && airliftFreeCarryalls() > 0 && unit->getAttackMode() == CARRYALLREQUESTED
                && doRequestCarryallDrop(ground) && ground->hasBookedCarrier();
            airliftBooking = NONE_ID;
            if (!rebooked) {
                const_cast<GroundUnit*>(ground)->cancelCarryallPickup();
                doMove2Pos(unit, unit->getLocation().x, unit->getLocation().y, false);
                const bool attacking = job.kind == AirliftKind::Rear || job.kind == AirliftKind::Flank
                    || job.kind == AirliftKind::Ferry;
                doSetAttackMode(unit, attacking ? HUNT : AREAGUARD);
                ++airliftStats.cancelled;
                if (job.kind==AirliftKind::Colonist) {
                    SDL_Log("COLONIST: house=%d event=flight_cancelled mcv=%u cycle=%u",getHouse()->getHouseID(),it->first,now);
                    job.kind=AirliftKind::ColonistGround;job.landed=false;
                    ++it;continue;
                }
                it = airliftJobs.erase(it);
                continue;
            }
        } else if (!unit->isActive() && overdue) {
            it = airliftJobs.erase(it);   // in a repair bay: the yard owns it now
            continue;
        }
        ++it;
    }
}

QuantBot::AirliftDecline QuantBot::planAirliftDrop() {
    const auto& force = botConfig().force;
    const Uint32 now = getGameCycleCount();
    const auto* front = getObject(customWave.front);
    if (!front || !front->getOwner() || front->getOwner()->isNatives()
        || front->getOwner()->getTeamID() == getHouse()->getTeamID()) return AirliftDecline::Front;
    const Coord origin = findBaseCentre(getHouse()->getHouseID());
    Coord target = findBaseCentre(front->getOwner()->getHouseID());
    if (target.isInvalid()) target = front->getLocation();
    if (origin.isInvalid() || target.isInvalid() || blockDistance(origin, target) < kMinDropTrip) return AirliftDecline::Trip;
    // Measured (smoke batches, 2026-11-23): a house launches one or two waves in thirty minutes
    // and its Carryalls are almost always out with harvesters, so a drop takes every free one.
    const int free = airliftFreeCarryalls();
    if (free < kMinDropCarryalls) return AirliftDecline::Carryalls;

    // The strongest whole vehicles of the wave, at most half of it: the wave stays the main
    // attack. Full health only - a Carryall takes anything damaged to a Repair Yard instead.
    std::vector<const UnitBase*> group;
    int waveVehicles = 0;
    for (const Uint32 id : customWave.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!liftable(unit, getHouse()) || airliftJobs.count(id)) continue;
        ++waveVehicles;
        if (unit->getHealth() >= unit->getMaxHealth()) group.push_back(unit);
    }
    std::sort(group.begin(), group.end(), [](const UnitBase* a, const UnitBase* b) {
        const int pa = unitPrice(a), pb = unitPrice(b);
        return pa != pb ? pa > pb : a->getObjectID() < b->getObjectID();
    });
    const int size = std::min({int(group.size()), std::max(0, force.airliftGroup), waveVehicles / 2});
    if (size < kMinDropGroup) return AirliftDecline::Group;

    // Both on: rear and flank take turns, wave by wave.
    const bool rear = force.airliftRearDrop && (!force.airliftFlankDrop || airliftStats.rearDrops <= airliftStats.flankDrops);
    const int dx = target.x - origin.x, dy = target.y - origin.y;
    const int length = std::max({1, std::abs(dx), std::abs(dy)});
    // The nearest standable tile out of observed enemy reach along a line from the base centre:
    // straight on for the rear, square to the approach for a flank. It must be ground the group
    // can drive from into the base (the front object's terrain region).
    const int region = getMap().vehicleTerrainRegion(group.front()->getLocation());
    const auto standable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        if (tile->isMountain() || tile->hasAStructure() || tile->isSpiceBloom() || tile->hasAGroundObject()) return false;
        if (region != 0 && getMap().vehicleTerrainRegion(p) != region) return false;
        return dangerAt(p) == 0;
    };
    const auto along = [&](int ux, int uy, int& radius) {
        for (radius = kDropNearest; radius <= kDropFarthest; radius += 2) {
            const Coord centre = target + Coord(ux * radius / length, uy * radius / length);
            for (int ring = 0; ring <= 2; ++ring)
                for (int oy = -ring; oy <= ring; ++oy)
                    for (int ox = -ring; ox <= ring; ++ox) {
                        if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                        if (standable(centre + Coord(ox, oy))) return centre + Coord(ox, oy);
                    }
        }
        return Coord::Invalid();
    };
    Coord point = Coord::Invalid();
    int radius = 0;
    if (rear) {
        point = along(dx, dy, radius);
    } else {
        int radiusLeft = 0, radiusRight = 0;
        const Coord left = along(-dy, dx, radiusLeft), right = along(dy, -dx, radiusRight);
        const bool takeLeft = left.isValid() && (right.isInvalid() || radiusLeft < radiusRight
            || (radiusLeft == radiusRight && getHouse()->getHouseID() % 2));
        point = takeLeft ? left : right;
        radius = takeLeft ? radiusLeft : radiusRight;
    }
    // A base on the map's rim has no ground behind it; nothing is flown then.
    if (point.isInvalid()) return AirliftDecline::Point;

    airliftDrop = AirliftDrop{};
    airliftDrop.kind = rear ? AirliftKind::Rear : AirliftKind::Flank;
    airliftDrop.point = point;
    airliftDrop.target = target;
    airliftDrop.wave = customWave.launched;
    airliftDrop.planned = now;
    for (int i = 0; i < size; ++i) airliftDrop.pending.push_back(group[i]->getObjectID());
    ++(rear ? airliftStats.rearDrops : airliftStats.flankDrops);
    traceDecision("airlift_drop", AITelemetry::Record().set("kind", rear ? "rear" : "flank").set("units", size)
        .set("x", point.x).set("y", point.y).set("target_x", target.x).set("target_y", target.y).set("radius", radius)
        .set("free_carryalls", free).set("wave_vehicles", waveVehicles));
    return AirliftDecline::None;
}

void QuantBot::updateAirliftDrop() {
    const auto& force = botConfig().force;
    const Uint32 now = getGameCycleCount();
    if (!airliftDrop.point.isValid()) {
        if (!(force.airliftRearDrop || force.airliftFlankDrop)) return;
        // One plan per launched wave, tried every pass of its first ninety seconds.
        if (customWave.launched == 0 || customWave.launched == airliftDrop.wave || customWave.members.empty()) return;
        if (now - customWave.launched > MILLI2CYCLES(90000)) { airliftDrop.wave = customWave.launched; return; }
        // Every refused attempt is counted by its reason (one attempt a pass, for the reports).
        switch (planAirliftDrop()) {
            case AirliftDecline::None: break;
            case AirliftDecline::Front: ++airliftStats.declinedFront; return;
            case AirliftDecline::Trip: ++airliftStats.declinedTrip; return;
            case AirliftDecline::Carryalls: ++airliftStats.declinedCarryalls; return;
            case AirliftDecline::Group: ++airliftStats.declinedGroup; return;
            case AirliftDecline::Point: ++airliftStats.declinedPoint; return;
        }
    }
    // Sorties: one unit per free Carryall.
    int free = airliftFreeCarryalls();
    while (free > 0 && !airliftDrop.pending.empty()) {
        const Uint32 id = airliftDrop.pending.front();
        airliftDrop.pending.erase(airliftDrop.pending.begin());
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        // Hit since the plan: it stays with the wave.
        if (!liftable(unit, getHouse()) || unit->getHealth() < unit->getMaxHealth()) continue;
        if (orderAirlift(unit, airliftDrop.point, airliftDrop.kind)) --free;
    }
    int flying = 0, landed = 0;
    for (const auto& [id, job] : airliftJobs) {
        if (job.kind != AirliftKind::Rear && job.kind != AirliftKind::Flank) continue;
        ++(job.landed ? landed : flying);
    }
    // The landed group waits out of reach until it is assembled (all down, or 45 s after the
    // first landing) and the ground wave is near the base, then both strike together. It does
    // not wait for the wave longer than 150 s, and a drop is over after five minutes or when
    // its wave is gone.
    const bool complete = airliftDrop.pending.empty() && flying == 0;
    const bool sinceLanding = airliftDrop.firstLanding != 0;
    const bool assembled = complete || (sinceLanding && now - airliftDrop.firstLanding > MILLI2CYCLES(45000));
    const bool longWait = sinceLanding && now - airliftDrop.firstLanding > MILLI2CYCLES(150000);
    const bool expired = now - airliftDrop.planned > MILLI2CYCLES(300000) || customWave.members.empty();
    bool waveNear = false;
    for (const Uint32 id : customWave.members) {
        if (airliftJobs.count(id)) continue;
        const auto* member = dynamic_cast<const UnitBase*>(getObject(id));
        if (member && member->isActive() && blockDistance(member->getLocation(), airliftDrop.target) <= kStrikeWaveTiles) {
            waveNear = true;
            break;
        }
    }
    if (!(expired || (assembled && (waveNear || longWait)))) return;
    for (auto it = airliftJobs.begin(); it != airliftJobs.end();) {
        const AirliftJob job = it->second;
        if ((job.kind != AirliftKind::Rear && job.kind != AirliftKind::Flank) || !job.landed) { ++it; continue; }
        releaseAirliftJob(dynamic_cast<const UnitBase*>(getObject(it->first)), job);
        it = airliftJobs.erase(it);
    }
    if (landed > 0) {
        ++airliftStats.strikes;
        traceDecision("airlift_strike", AITelemetry::Record().set("units", landed).set("still_flying", flying)
            .set("not_lifted", int(airliftDrop.pending.size())).set("complete", complete).set("wave_near", waveNear));
    }
    if (complete || expired) {
        const Uint32 wave = airliftDrop.wave;
        airliftDrop = AirliftDrop{};
        airliftDrop.wave = wave;
    } else {
        airliftDrop.firstLanding = 0;   // the rest strikes as it lands
    }
}

void QuantBot::updateAirliftRetreat() {
    // The wave is outmatched where it stands (the posture machine's sustained local verdict,
    // read before the default-off withdrawal switch masks it). What the fleet can carry is
    // flown home, the most valuable first; the rest keeps fighting, as it does without this.
    if (!botConfig().force.airliftRetreatLift) return;
    if (!airliftLocalDefeat) { airliftRetreating = false; return; }
    ++airliftStats.defeatPasses;
    int free = airliftFreeCarryalls();
    if (free <= 0) { ++airliftStats.retreatNoCarryall; return; }
    std::vector<const UnitBase*> fighters;
    for (const Uint32 id : customWave.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!liftable(unit, getHouse()) || airliftJobs.count(id) || dangerAt(unit->getLocation()) == 0) continue;
        fighters.push_back(unit);
    }
    if (fighters.empty()) { ++airliftStats.retreatNoFighter; return; }
    const Coord home = airliftHome();
    if (home.isInvalid()) return;
    std::sort(fighters.begin(), fighters.end(), [](const UnitBase* a, const UnitBase* b) {
        const int pa = unitPrice(a), pb = unitPrice(b);
        return pa != pb ? pa > pb : a->getObjectID() < b->getObjectID();
    });
    int ordered = 0;
    for (const auto* unit : fighters) {
        if (free <= 0) break;
        if (blockDistance(unit->getLocation(), home) < MIN_CARRYALL_LIFT_DISTANCE) continue;
        if (!orderAirlift(unit, home, AirliftKind::Retreat)) continue;
        customWave.members.erase(unit->getObjectID());   // it is out of this wave
        --free;
        ++ordered;
    }
    if (ordered > 0) {
        airliftStats.retreatUnits += ordered;
        if (!airliftRetreating) {
            airliftRetreating = true;
            ++airliftStats.retreatLifts;
            traceDecision("airlift_retreat", AITelemetry::Record().set("units", ordered).set("in_contact", int(fighters.size()))
                .set("home_x", home.x).set("home_y", home.y));
        }
    }
}

void QuantBot::updateAirliftRescue() {
    // One vehicle at a time: under observed enemy fire, away from home, and either below
    // AirliftRescueHealth percent without a repair trip of its own or (AirliftRescueCutOff) with
    // no friendly fighter near it.
    const auto& force = botConfig().force;
    if (!force.airliftRescue) return;
    int free = airliftFreeCarryalls();
    if (free <= 0) return;
    const Coord base = findBaseCentre(getHouse()->getHouseID());
    if (base.isInvalid()) return;
    Coord home = Coord::Invalid();
    int orders = 0;
    for (const auto* unit : getUnitList()) {
        if (free <= 0 || orders >= kRescueOrdersPerPass) break;
        if (!liftable(unit, getHouse()) || airliftJobs.count(unit->getObjectID())) continue;
        const Coord at = unit->getLocation();
        if (dangerAt(at) == 0 || blockDistance(at, base) < kRescueHomeTiles) continue;
        const bool damaged = force.airliftRescueHealth > 0
            && unit->getHealth() * 100 < FixPoint(unit->getMaxHealth()) * force.airliftRescueHealth;
        bool alone = force.airliftRescueCutOff;
        if (!damaged && alone) {
            for (const auto* other : getUnitList()) {
                if (other == unit || other->getOwner() != getHouse() || !mobileCombatItem(other->getItemID())
                    || !other->isActive() || other->isPickedUp()) continue;
                if (blockDistance(at, other->getLocation()) <= kRescueAloneTiles) { alone = false; break; }
            }
        }
        if (!damaged && !alone) continue;
        if (home.isInvalid()) home = airliftHome();
        if (home.isInvalid()) return;
        if (blockDistance(at, home) < MIN_CARRYALL_LIFT_DISTANCE) continue;
        if (!orderAirlift(unit, home, AirliftKind::Rescue)) continue;
        customWave.members.erase(unit->getObjectID());
        --free;
        ++orders;
        ++airliftStats.rescues;
        ++(damaged ? airliftStats.rescuesDamaged : airliftStats.rescuesAlone);
        traceDecision("airlift_rescue", AITelemetry::Record().set("unit", int(unit->getObjectID()))
            .set("item", int(unit->getItemID())).set("damaged", damaged).set("x", at.x).set("y", at.y));
    }
}

void QuantBot::updateAirliftFerry() {
    // Concentration instead of a split: the wave's front is its third nearest the enemy base.
    // A whole member 25 tiles or more farther back, and not in a fight of its own, is flown to
    // the nearest ground out of enemy reach beside that front and hunts on from there.
    if (!botConfig().force.airliftFerry) return;
    if (customWave.launched == 0 || int(customWave.members.size()) < kFerryMinWave) return;
    int free = airliftFreeCarryalls();
    if (free <= 0) return;
    const auto* front = getObject(customWave.front);
    if (!front || !front->getOwner() || front->getOwner()->isNatives()
        || front->getOwner()->getTeamID() == getHouse()->getTeamID()) return;
    Coord target = findBaseCentre(front->getOwner()->getHouseID());
    if (target.isInvalid()) target = front->getLocation();
    if (target.isInvalid()) return;
    struct Member { const UnitBase* unit; int distance; };
    std::vector<Member> members;
    for (const Uint32 id : customWave.members) {
        if (airliftJobs.count(id)) continue;
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isAGroundUnit() || !unit->isActive() || unit->isPickedUp()) continue;
        members.push_back({unit, blockDistance(unit->getLocation(), target).lround()});
    }
    if (int(members.size()) < kFerryMinWave) return;
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.unit->getObjectID() < b.unit->getObjectID();
    });
    const int van = std::max(2, int(members.size()) / 3);
    int x = 0, y = 0;
    for (int i = 0; i < van; ++i) { x += members[i].unit->getLocation().x; y += members[i].unit->getLocation().y; }
    const int vanDistance = members[van - 1].distance;
    const Coord point = huntPartyPoint(Coord(x / van, y / van), members.front().unit, true);
    if (point.isInvalid() || !getMap().tileExists(point) || dangerAt(point) != 0) return;
    for (auto it = members.rbegin(); it != members.rend() && free > 0; ++it) {
        if (it->distance < vanDistance + kFerryGap) break;   // sorted: nobody nearer is behind either
        const auto* unit = it->unit;
        // Whole units only (a Carryall takes a damaged one to a Repair Yard instead), and none
        // that is fighting where it stands.
        if (!liftable(unit, getHouse()) || unit->getHealth() < unit->getMaxHealth()) continue;
        if (dangerAt(unit->getLocation()) != 0 || blockDistance(unit->getLocation(), point) < kFerryGap) continue;
        if (!orderAirlift(unit, point, AirliftKind::Ferry)) continue;
        --free;
        ++airliftStats.ferried;
    }
}

bool QuantBot::airliftRetreatByAir(const UnitBase* unit, Coord destination) {
    if (gameMode != GameMode::Custom || !botConfig().force.squadRetreatAirlift) return false;
    if (!airliftable(unit) || airliftJobs.count(unit->getObjectID())
        || blockDistance(unit->getLocation(), destination) < kRetreatLiftTiles) return false;
    if (airliftFreeCarryalls() <= 0) return false;
    if (!orderAirlift(unit, destination, AirliftKind::Retreat)) return false;
    ++airliftStats.retreatUnits;
    return true;
}

bool QuantBot::airliftDefender(const UnitBase* unit, const ObjectBase* aim, bool worker) {
    const auto& force = botConfig().force;
    if (gameMode != GameMode::Custom || !(worker ? force.airliftHarvesterDefence : force.airliftBaseDefence)) return false;
    if (!aim || aim->isAFlyingUnit() || !getMap().tileExists(aim->getLocation())) return false;
    // Whole units only: a Carryall takes a damaged one to a Repair Yard instead.
    if (!airliftable(unit) || airliftJobs.count(unit->getObjectID()) || unit->getHealth() < unit->getMaxHealth()) return false;
    const Coord contact = aim->getLocation();
    if (blockDistance(unit->getLocation(), contact) < kDefenceLiftTiles || airliftFreeCarryalls() <= 0) return false;
    // Set down four tiles short of the attacker, on the defender's side of it.
    const int dx = unit->getLocation().x - contact.x, dy = unit->getLocation().y - contact.y;
    const int length = std::max({1, std::abs(dx), std::abs(dy)});
    const Coord centre = contact + Coord(dx * 4 / length, dy * 4 / length);
    Coord point = Coord::Invalid();
    for (int ring = 0; ring <= 3 && point.isInvalid(); ++ring)
        for (int oy = -ring; oy <= ring && point.isInvalid(); ++oy)
            for (int ox = -ring; ox <= ring && point.isInvalid(); ++ox) {
                if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                const Coord p = centre + Coord(ox, oy);
                if (!getMap().tileExists(p)) continue;
                const auto* tile = getMap().getTile(p);
                if (tile->isMountain() || tile->hasAStructure() || tile->isSpiceBloom() || tile->hasAGroundObject()) continue;
                point = p;
            }
    if (point.isInvalid() || !orderAirlift(unit, point, AirliftKind::Defence, aim->getObjectID())) return false;
    ++(worker ? airliftStats.harvesterDefenceLifts : airliftStats.baseDefenceLifts);
    return true;
}

const ObjectBase* QuantBot::airliftRaidTarget(AirliftKind kind, Coord near, int maxTiles) const {
    // An enemy harvester, or the dearest enemy building, that no observed enemy weapon covers
    // (the tactical danger map is 0 on it). True positions, as the ground harvester raid uses.
    const int team = getHouse()->getTeamID();
    const ObjectBase* best = nullptr;
    int bestPrice = -1, bestDistance = 0;
    const auto consider = [&](const ObjectBase* object, int price, Coord at) {
        const int distance = blockDistance(near, at).lround();
        if (maxTiles > 0 && distance > maxTiles) return;
        if (best && (price < bestPrice || (price == bestPrice && (distance > bestDistance
            || (distance == bestDistance && object->getObjectID() > best->getObjectID()))))) return;
        best = object; bestPrice = price; bestDistance = distance;
    };
    const auto hostile = [&](const ObjectBase* object) {
        return object->getOwner() && !object->getOwner()->isNatives() && object->getOwner()->isAlive()
            && object->getOwner()->getTeamID() != team && object->getHealth() > 0;
    };
    if (kind == AirliftKind::HarvesterRaid) {
        for (const auto* unit : getUnitList()) {
            if (unit->getItemID() != Unit_Harvester || !hostile(unit) || !unit->isActive() || unit->isPickedUp()) continue;
            if (!getMap().tileExists(unit->getLocation()) || dangerAt(unit->getLocation()) != 0) continue;
            consider(unit, 0, unit->getLocation());   // every harvester is worth the same: the nearest
        }
    } else {
        for (const auto* structure : getStructureList()) {
            if (!hostile(structure) || structure->canAttack() || structure->getItemID() == Structure_Wall) continue;
            if (dangerAt(structure->getLocation(), structure->getStructureSize()) != 0) continue;
            const int price = currentGame->objectData.data[structure->getItemID()][structure->getProductionHouseID()].price;
            consider(structure, price, structure->getClosestPoint(near));
        }
    }
    return best;
}

bool QuantBot::planAirliftRaid() {
    const auto& force = botConfig().force;
    const Uint32 now = getGameCycleCount();
    if (now < airliftRaidNext) return false;
    airliftRaidNext = now + MILLI2CYCLES(5000);
    if (airliftFreeCarryalls() <= 0) { ++airliftStats.raidNoCarryall; return false; }
    const Coord base = findBaseCentre(getHouse()->getHouseID());
    if (base.isInvalid()) return false;
    // Both on: harvester and perimeter raids take turns.
    const bool harvesters = airliftHarvesterDropActive()
        && (!force.airliftPerimeterDrop || airliftStats.harvesterRaids <= airliftStats.perimeterRaids);
    const AirliftKind kind = harvesters ? AirliftKind::HarvesterRaid : AirliftKind::PerimeterRaid;
    // Free, whole vehicles standing at home: nobody is taken out of a wave or a defence.
    std::vector<const UnitBase*> group;
    for (const auto* unit : getUnitList()) {
        if (!airliftable(unit) || !orderableCombatUnit(unit) || airliftJobs.count(unit->getObjectID())) continue;
        if (unit->getHealth() < unit->getMaxHealth() || unit->getAttackMode() == HUNT) continue;
        if (unitRole(unit, {UnitRole::Repair, UnitRole::BaseDefence, UnitRole::LiveDefence, UnitRole::Defence,
                            UnitRole::Wave, UnitRole::Escort, UnitRole::ColonyGuard}) != UnitRole::Free) continue;
        if (blockDistance(unit->getLocation(), base) > kRaidHomeTiles) continue;
        group.push_back(unit);
    }
    if (int(group.size()) < 2) { ++airliftStats.raidNoGroup; return false; }
    std::sort(group.begin(), group.end(), [](const UnitBase* a, const UnitBase* b) {
        const int pa = unitPrice(a), pb = unitPrice(b);
        return pa != pb ? pa > pb : a->getObjectID() < b->getObjectID();
    });
    const ObjectBase* target = airliftRaidTarget(kind, base, 0);
    const Coord at = target ? target->getClosestPoint(base) : Coord::Invalid();
    Coord point = Coord::Invalid();
    if (target && blockDistance(base, at) >= kMinDropTrip)
        for (int ring = 1; ring <= 4 && point.isInvalid(); ++ring)
            for (int oy = -ring; oy <= ring && point.isInvalid(); ++oy)
                for (int ox = -ring; ox <= ring && point.isInvalid(); ++ox) {
                    if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                    const Coord p = at + Coord(ox, oy);
                    if (!getMap().tileExists(p) || dangerAt(p) != 0) continue;
                    const auto* tile = getMap().getTile(p);
                    if (tile->isMountain() || tile->hasAStructure() || tile->isSpiceBloom() || tile->hasAGroundObject()) continue;
                    point = p;
                }
    if (point.isInvalid()) {
        // Nothing uncovered within reach of a landing: look again in twenty seconds.
        ++airliftStats.raidNoTarget;
        airliftRaidNext = now + MILLI2CYCLES(20000);
        return false;
    }
    airliftRaid = AirliftDrop{};
    airliftRaid.kind = kind;
    airliftRaid.point = point;
    airliftRaid.target = at;
    airliftRaid.objective = target->getObjectID();
    airliftRaid.planned = now;
    const int size = std::min(int(group.size()), force.airliftRaidGroup);
    for (int i = 0; i < size; ++i) airliftRaid.pending.push_back(group[i]->getObjectID());
    ++(harvesters ? airliftStats.harvesterRaids : airliftStats.perimeterRaids);
    traceDecision("airlift_raid", AITelemetry::Record().set("kind", harvesters ? "harvester" : "perimeter")
        .set("units", size).set("target", int(target->getObjectID())).set("target_item", int(target->getItemID()))
        .set("x", point.x).set("y", point.y));
    return true;
}

void QuantBot::updateAirliftRaid() {
    const auto& force = botConfig().force;
    if (!(airliftHarvesterDropActive() || force.airliftPerimeterDrop)) return;
    const Uint32 now = getGameCycleCount();
    if (!airliftRaid.point.isValid() && !planAirliftRaid()) return;
    const auto* target = getObject(airliftRaid.objective);
    const bool live = target && target->getHealth() > 0;
    int free = airliftFreeCarryalls();
    while (live && free > 0 && !airliftRaid.pending.empty()) {
        const Uint32 id = airliftRaid.pending.front();
        airliftRaid.pending.erase(airliftRaid.pending.begin());
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!airliftable(unit) || unit->getHealth() < unit->getMaxHealth()) continue;
        if (orderAirlift(unit, airliftRaid.point, airliftRaid.kind, airliftRaid.objective)) {
            --free;
            ++airliftStats.raidUnits;
        }
    }
    // The plan is spent once everyone is ordered, its target is gone or two minutes passed; the
    // raiders already out carry on by themselves. The next raid is planned a minute later.
    if (airliftRaid.pending.empty() || !live || now - airliftRaid.planned > MILLI2CYCLES(120000)) {
        airliftRaid = AirliftDrop{};
        airliftRaidNext = now + MILLI2CYCLES(60000);
    }
}

bool QuantBot::continueAirliftRaid(const UnitBase* unit, AirliftJob& job) {
    const Uint32 now = getGameCycleCount();
    const bool fit = unit->getHealth() * 100 >= FixPoint(unit->getMaxHealth()) * 40
        && now - job.ordered < MILLI2CYCLES(180000);
    const auto* target = getObject(job.objective);
    const bool live = target && target->getHealth() > 0 && target->getOwner()
        && target->getOwner()->getTeamID() != getHouse()->getTeamID()
        && (!target->isAUnit() || (target->isActive() && !static_cast<const UnitBase*>(target)->isPickedUp()));
    if (fit && live) {
        // The engine lets go of a forced target that drives out of reach; a raider does not.
        if (unit->getTarget() != target) doAttackObject(unit, target, true);
        return true;
    }
    if (fit) {
        if (const auto* next = airliftRaidTarget(job.kind, unit->getLocation(), kRaidNextTiles)) {
            job.objective = next->getObjectID();
            doAttackObject(unit, next, true);
            ++airliftStats.raidRetargets;
            return true;
        }
    }
    // Done, hurt, or out for three minutes: home, by Carryall when one is free.
    const Coord home = airliftHome();
    if (home.isInvalid()) { doSetAttackMode(unit, AREAGUARD); return false; }
    const auto* ground = static_cast<const GroundUnit*>(unit);
    doSetAttackMode(unit, AREAGUARD);
    doMove2Pos(unit, home.x, home.y, true);
    if (airliftable(unit) && airliftFreeCarryalls() > 0) {
        airliftBooking = unit->getObjectID();
        const bool booked = doRequestCarryallDrop(ground) && ground->hasBookedCarrier();
        airliftBooking = NONE_ID;
        if (booked) {
            // The same job, now a flight home that trackAirliftJobs follows to its landing.
            job.kind = AirliftKind::Retreat;
            job.drop = home;
            job.ordered = now;
            job.lifted = job.landed = false;
            job.objective = NONE_ID;
            ++airliftStats.ordered;
            return true;
        }
        // No aircraft: the Carryall request must not leave it waiting; it drives.
        const_cast<GroundUnit*>(ground)->cancelCarryallPickup();
        doSetAttackMode(unit, AREAGUARD);
        doMove2Pos(unit, home.x, home.y, true);
    }
    return false;
}

bool QuantBot::airliftHolds(Uint32 id) const {
    const auto it=airliftJobs.find(id);
    return it!=airliftJobs.end() && it->second.kind!=AirliftKind::ColonistGround
        && it->second.kind!=AirliftKind::ColonyTurret && it->second.kind!=AirliftKind::ColonistPreparing;
}



Coord QuantBot::colonistTurretSite(Coord yard,Uint32 supplier) const {
    Coord best=Coord::Invalid();int bestEnemy=std::numeric_limits<int>::max(),bestGap=std::numeric_limits<int>::max();
    for (int y=yard.y-2;y<=yard.y+3;++y) for (int x=yard.x-2;x<=yard.x+3;++x) {
        const Coord at(x,y);
        if (!getMap().tileExists(at) || (x>=yard.x && x<yard.x+2 && y>=yard.y && y<yard.y+2)) continue;
        const auto* tile=getMap().getTile(at);
        if (!tile->isRock() || tile->isMountain() || tile->hasAGroundObject() || tile->hasCityZone()) continue;
        bool reserved=false;
        for (const auto& entry:reservedStructures)
            if (entry.first!=supplier && entry.second.location==at) {reserved=true;break;}
        if (reserved) continue;
        int enemyGap=std::numeric_limits<int>::max();
        for (const auto* enemy:getStructureList())
            if (enemy->getOwner() && enemy->getOwner()->getTeamID()!=getHouse()->getTeamID() && enemy->getHealth()>0)
                enemyGap=std::min(enemyGap,blockDistance(at,enemy->getClosestPoint(at)).lround());
        const int gap=blockDistance(at,yard).lround();
        if (best.isInvalid() || enemyGap<bestEnemy || (enemyGap==bestEnemy && gap<bestGap)) {
            best=at;bestEnemy=enemyGap;bestGap=gap;
        }
    }
    return best;
}

void QuantBot::releaseColonistTurret(const AirliftJob& job) {
    if (job.kind==AirliftKind::ColonistPreparing) return; // Its ordinary home work is not colony material.
    const auto reservation=reservedStructures.find(job.objective);
    if (reservation==reservedStructures.end() || reservation->second.item!=Structure_RocketTurret
        || blockDistance(reservation->second.location,job.drop)>4) return;
    if (const auto held=heldWithoutSiteSince.find(job.objective);held!=heldWithoutSiteSince.end())
        SDL_Log("COLONIST: house=%d event=turret_released supplier=%u wait_s=%d cycle=%u",
            getHouse()->getHouseID(),job.objective,
            int((getGameCycleCount()-held->second)*GAMESPEED_DEFAULT/1000),getGameCycleCount());
    if (const auto* builder=dynamic_cast<const BuilderBase*>(getObject(job.objective));builder && builder->getOwner()==getHouse())
        doCancelItem(builder,Structure_RocketTurret);
    reservedStructures.erase(reservation);
    heldWithoutSiteSince.erase(job.objective);
}

bool QuantBot::prepareColonistTrip(const MCV* mcv,Coord site) {
    if (!botConfig().force.airliftColonists || gameMode!=GameMode::Custom || !currentGame->isCitySimEnabled()
        || !mcv || !site.isValid()) return false;
    const Uint32 id=mcv->getObjectID(),now=getGameCycleCount();
    auto found=airliftJobs.find(id);
    if (found==airliftJobs.end() && onOwnRockFormation(site)) return false;
    if (found!=airliftJobs.end() && found->second.kind==AirliftKind::Colonist) return true;
    if (found==airliftJobs.end()) {
        AirliftJob job;job.kind=AirliftKind::ColonistPreparing;job.drop=site;job.ordered=now;
        found=airliftJobs.emplace(id,job).first;
        SDL_Log("COLONIST: house=%d event=trip mcv=%u target_x=%d target_y=%d drops=%d cycle=%u",getHouse()->getHouseID(),
            id,site.x,site.y,int(getGameInitSettings().getGameOptions().manualCarryallDrops),now);
    }
    auto& job=found->second;
    if (job.kind!=AirliftKind::ColonistGround && job.kind!=AirliftKind::ColonistPreparing) return false;
    if (job.drop!=site) {releaseColonistTurret(job);job.drop=site;job.objective=NONE_ID;job.landed=false;}
    // An aircraft that landed slightly off the footprint finishes its short local drive normally.
    if (job.lifted && job.landed) return false;
    const auto* supplier=dynamic_cast<const BuilderBase*>(getObject(job.objective));
    if (!supplier || supplier->getOwner()!=getHouse() || supplier->getHealth()<=0) {
        releaseColonistTurret(job);
        job.objective=NONE_ID;
        for (const auto* structure:getStructureList()) {
            const auto* yard=dynamic_cast<const BuilderBase*>(structure);
            if (!yard || yard->getOwner()!=getHouse() || yard->getHealth()<=0 || yard->getItemID()!=Structure_ConstructionYard
                || yard->isUpgrading() || yard->isOnHold()
                || !campaignAvailableToBuild(yard,Structure_RocketTurret)) continue;
            bool taken=false;
            for (const auto& other:airliftJobs) if (other.first!=id && other.second.objective==yard->getObjectID()
                && other.second.kind>=AirliftKind::Colonist) {taken=true;break;}
            if (!taken) {supplier=yard;job.objective=yard->getObjectID();break;}
        }
    }
    const auto reserved=reservedStructures.find(job.objective);
    if (job.kind==AirliftKind::ColonistPreparing || !supplier || reserved==reservedStructures.end() || reserved->second.item!=Structure_RocketTurret
        || blockDistance(reserved->second.location,job.drop)>4
        || !supplier->isWaitingToPlace() || supplier->getCurrentProducedItem()!=Structure_RocketTurret) return true;
    if (getGameInitSettings().getGameOptions().manualCarryallDrops && !job.lifted) {
        const Uint32 supplierID=job.objective;
        if (airliftFreeCarryalls()>0 && orderAirlift(mcv,site,AirliftKind::Colonist,supplierID)) {
            SDL_Log("COLONIST: house=%d event=flight_order mcv=%u supplier=%u target_x=%d target_y=%d cycle=%u",
                getHouse()->getHouseID(),id,supplierID,site.x,site.y,now);
        }
        return true; // Never silently drive an air arm when no aircraft is available.
    }
    return false;
}

bool QuantBot::buildColonistTransport(BuildPass& pass,BuilderTurn& turn) {
    if (!botConfig().force.airliftColonists || gameMode!=GameMode::Custom
        || !getGameInitSettings().getGameOptions().manualCarryallDrops || pass.itemCount[Unit_Carryall]>0
        || (turn.pBuilder->getItemID()!=Structure_HighTechFactory && turn.pBuilder->getItemID()!=Structure_StarPort)
        || turn.pBuilder->getProductionQueueSize()>0
        || turn.pBuilder->isUpgrading() || turn.pBuilder->isOnHold()) return false;
    for (const auto& entry:airliftJobs) if (entry.second.kind==AirliftKind::ColonistGround || entry.second.kind==AirliftKind::ColonistPreparing) {
        const bool port=turn.pBuilder->getItemID()==Structure_StarPort;
        if (port && (!static_cast<const StarPort*>(turn.pBuilder)->okToOrder()
            || getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0)) return false;
        if (campaignAvailableToBuild(turn.pBuilder,Unit_Carryall)
            && produceItemWithLogging(pass,turn,Unit_Carryall,__LINE__,"colonist_transport")) {
            ++pass.itemCount[Unit_Carryall];
            if (port) doPlaceOrder(static_cast<const StarPort*>(turn.pBuilder));
            return true;
        }
        break;
    }
    return false;
}

bool QuantBot::buildColonistTurret(BuildPass& pass,BuilderTurn& turn) {
    if (!botConfig().force.airliftColonists || gameMode!=GameMode::Custom
        || turn.pBuilder->getItemID()!=Structure_ConstructionYard) return false;
    for (auto it=airliftJobs.begin();it!=airliftJobs.end();++it) {
        auto& job=it->second;
        if (job.kind<AirliftKind::Colonist || job.objective!=turn.pBuilder->getObjectID()) continue;
        const Uint32 now=getGameCycleCount();
        // Reserve the yard's next empty turn; ordinary paid home work finishes first.
        if (job.kind==AirliftKind::ColonistPreparing && turn.pBuilder->getProductionQueueSize()>0) return false;
        // The assignment and supplier ID survive saves in the existing AirliftJob stream.
        // Rebuild the transient site reservation for that supplier's paid material after reload.
        if (job.kind!=AirliftKind::ColonistPreparing && !reservedStructures.count(job.objective) && turn.pBuilder->getProductionQueueSize()>0
            && turn.pBuilder->getCurrentProducedItem()==Structure_RocketTurret) {
            const Coord at=colonistTurretSite(job.drop,job.objective);
            if (at.isValid()) reservedStructures[job.objective]={Structure_RocketTurret,at};
        }
        const auto ownMaterial=reservedStructures.find(job.objective);
        if (turn.pBuilder->getProductionQueueSize()>0
            && (ownMaterial==reservedStructures.end() || ownMaterial->second.item!=Structure_RocketTurret
                || blockDistance(ownMaterial->second.location,job.drop)>4)) {
            // A home emergency used the uncommitted yard first. Its material is never stolen by this mission.
            job.objective=NONE_ID;return false;
        }
        if (!turn.pBuilder->isWaitingToPlace()) {
            if (turn.pBuilder->getProductionQueueSize()>0) return true;
            if (!hasPowerBufferForTurret(pass)) return false;
            const Coord at=colonistTurretSite(job.drop,job.objective);
            if (!at.isValid()) return true;
            if (produceItemWithLogging(pass,turn,Structure_RocketTurret,__LINE__,"colonist_turret")) {
                ++pass.itemCount[Structure_RocketTurret];reservedStructures[job.objective]={Structure_RocketTurret,at};
                heldWithoutSiteSince.erase(job.objective);
                job.kind=AirliftKind::ColonistGround;
                SDL_Log("COLONIST: house=%d event=turret_queued mcv=%u supplier=%u x=%d y=%d cycle=%u",getHouse()->getHouseID(),
                    it->first,job.objective,at.x,at.y,now);
            }
            return true;
        }
        if (turn.pBuilder->getCurrentProducedItem()!=Structure_RocketTurret) {job.objective=NONE_ID;return false;}
        if (job.kind!=AirliftKind::ColonyTurret) {
            if (heldWithoutSiteSince.emplace(job.objective,now).second)
                SDL_Log("COLONIST: house=%d event=turret_ready mcv=%u supplier=%u cycle=%u",getHouse()->getHouseID(),it->first,job.objective,now);
            return true;
        }
        bool yardStanding=false;
        for (const auto* yard:getStructureList())
            if (yard->getOwner()==getHouse() && yard->getItemID()==Structure_ConstructionYard
                && yard->getHealth()>0 && yard->getLocation()==job.drop) {yardStanding=true;break;}
        if (!yardStanding) {
            SDL_Log("COLONIST: house=%d event=colony_lost mcv=%u cycle=%u",getHouse()->getHouseID(),it->first,now);
            releaseColonistTurret(job);airliftJobs.erase(it);return true;
        }
        if (getGameInitSettings().getGameOptions().rocketTurretsNeedPower
            && (!getHouse()->hasPower() || !hasPowerBufferForTurret(pass))) return true;
        Coord at=Coord::Invalid();
        if (const auto reserved=reservedStructures.find(job.objective);reserved!=reservedStructures.end()) at=reserved->second.location;
        if (!at.isValid() || !getMap().okayToPlaceStructure(at.x,at.y,1,1,false,getHouse(),false,Structure_RocketTurret))
            at=colonistTurretSite(job.drop,job.objective);
        if (!at.isValid() || !getMap().okayToPlaceStructure(at.x,at.y,1,1,false,getHouse(),false,Structure_RocketTurret)) return true;
        const auto held=heldWithoutSiteSince.find(job.objective);
        const int holdingSeconds=held==heldWithoutSiteSince.end() ? 0 : int((now-held->second)*GAMESPEED_DEFAULT/1000);
        if (doPlaceStructure(static_cast<const ConstructionYard*>(turn.pBuilder),at.x,at.y)) {
            int enemyGap=-1,yardEnemyGap=-1;
            for (const auto* enemy:getStructureList()) if (enemy->getOwner() && enemy->getOwner()->getTeamID()!=getHouse()->getTeamID()
                && enemy->getHealth()>0) {
                const int gap=blockDistance(at,enemy->getClosestPoint(at)).lround();
                if (enemyGap<0 || gap<enemyGap) enemyGap=gap;
                const int yardGap=blockDistance(job.drop,enemy->getClosestPoint(job.drop)).lround();
                if (yardEnemyGap<0 || yardGap<yardEnemyGap) yardEnemyGap=yardGap;
            }
            SDL_Log("COLONIST: house=%d event=turret_placed mcv=%u supplier=%u wait_s=%d air=%d x=%d y=%d target_x=%d target_y=%d yard_tiles=%d "
                "enemy_tiles=%d yard_enemy_tiles=%d delay_s=%d power=%d met=%d cycle=%u",getHouse()->getHouseID(),it->first,job.objective,holdingSeconds,int(job.lifted),at.x,at.y,
                job.drop.x,job.drop.y,blockDistance(at,job.drop).lround(),enemyGap,yardEnemyGap,
                int((now-job.ordered)*GAMESPEED_DEFAULT/1000),int(getHouse()->hasPower()),
                int(now-job.ordered<=MILLI2CYCLES(10000)),now);
            reservedStructures.erase(job.objective);heldWithoutSiteSince.erase(job.objective);airliftJobs.erase(it);clearPlacementCache();
        }
        return true;
    }
    return false;
}

void QuantBot::colonistDeployed(Uint32 id,Coord site) {
    auto it=airliftJobs.find(id);
    if (it==airliftJobs.end() || it->second.kind!=AirliftKind::ColonistGround) return;
    auto& job=it->second;
    const auto* supplier=dynamic_cast<const BuilderBase*>(getObject(job.objective));
    const bool ready=supplier && supplier->getOwner()==getHouse() && supplier->isWaitingToPlace()
        && supplier->getCurrentProducedItem()==Structure_RocketTurret;
    SDL_Log("COLONIST: house=%d event=deployed mcv=%u air=%d ready=%d target_x=%d target_y=%d cycle=%u",getHouse()->getHouseID(),
        id,int(job.lifted),int(ready),site.x,site.y,getGameCycleCount());
    job.kind=AirliftKind::ColonyTurret;job.drop=site;job.ordered=getGameCycleCount();
}

void QuantBot::updateAirlift() {
    if (!botConfig().force.airliftColonists && !airliftJobs.empty()) {
        for (auto it=airliftJobs.begin();it!=airliftJobs.end();) {
            if (it->second.kind<AirliftKind::Colonist) { ++it;continue; }
            releaseColonistTurret(it->second);
            if (const auto* unit=dynamic_cast<const GroundUnit*>(getObject(it->first));
                unit && unit->getOwner()==getHouse() && !unit->isPickedUp())
                const_cast<GroundUnit*>(unit)->cancelCarryallPickup();
            it=airliftJobs.erase(it);
        }
    }
    if (!airliftActive()) {
        // Switched off (a config change between a save and its reload): nothing stays held.
        if (!airliftJobs.empty() || airliftDrop.point.isValid() || airliftRaid.point.isValid()) {
            airliftJobs.clear();
            airliftDrop = AirliftDrop{};
            airliftRaid = AirliftDrop{};
        }
        return;
    }
    trackAirliftJobs();
    if (getHouse()->getNumItems(Unit_Carryall) <= 0 && airliftJobs.empty() && !airliftDrop.point.isValid()
        && !airliftRaid.point.isValid()) return;
    updateAirliftRescue();
    updateAirliftRetreat();
    updateAirliftDrop();
    updateAirliftFerry();
    updateAirliftRaid();
}

void QuantBot::saveAirlift(OutputStream& stream) const {
    const auto coord = [&](Coord v) { stream.writeSint32(v.x); stream.writeSint32(v.y); };
    stream.writeUint32(Uint32(airliftJobs.size()));
    for (const auto& [id, job] : airliftJobs) {
        stream.writeUint32(id);
        stream.writeUint8(static_cast<Uint8>(job.kind));
        coord(job.drop);
        stream.writeUint32(job.ordered);
        stream.writeBool(job.lifted);
        stream.writeBool(job.landed);
    }
    stream.writeUint8(static_cast<Uint8>(airliftDrop.kind));
    coord(airliftDrop.point);
    coord(airliftDrop.target);
    stream.writeUint32(airliftDrop.wave);
    stream.writeUint32(airliftDrop.planned);
    stream.writeUint32(airliftDrop.firstLanding);
    stream.writeUint32(Uint32(airliftDrop.pending.size()));
    for (const Uint32 id : airliftDrop.pending) stream.writeUint32(id);
    stream.writeBool(airliftLocalDefeat);
    stream.writeBool(airliftRetreating);
    // The fired counts decide whose turn it is when rear and flank are both on.
    stream.writeSint32(airliftStats.rearDrops);
    stream.writeSint32(airliftStats.flankDrops);
    // SAVEGAMEVERSION_AIRLIFT_RAIDS: the jobs' objectives (in job order), then the raid.
    for (const auto& [id, job] : airliftJobs) stream.writeUint32(job.objective);
    stream.writeUint8(static_cast<Uint8>(airliftRaid.kind));
    coord(airliftRaid.point);
    coord(airliftRaid.target);
    stream.writeUint32(airliftRaid.objective);
    stream.writeUint32(airliftRaid.planned);
    stream.writeUint32(Uint32(airliftRaid.pending.size()));
    for (const Uint32 id : airliftRaid.pending) stream.writeUint32(id);
    stream.writeUint32(airliftRaidNext);
    stream.writeSint32(airliftStats.harvesterRaids);
    stream.writeSint32(airliftStats.perimeterRaids);
}

void QuantBot::loadAirlift(InputStream& stream) {
    constexpr Uint32 kMaxEntries = 65536;
    const auto coord = [&]() { Coord v; v.x = stream.readSint32(); v.y = stream.readSint32(); return v; };
    const auto kind = [&]() {
        const Uint8 value = stream.readUint8();
        if (value > static_cast<Uint8>(AirliftKind::ColonistPreparing)) throw std::runtime_error("Invalid airlift kind");
        return static_cast<AirliftKind>(value);
    };
    const auto valid = [&](Coord p) { return p.isInvalid() || getMap().tileExists(p); };
    airliftJobs.clear();
    const Uint32 jobs = stream.readUint32();
    if (jobs > kMaxEntries) throw std::runtime_error("Invalid airlift jobs");
    for (Uint32 i = 0; i < jobs; ++i) {
        const Uint32 id = stream.readUint32();
        AirliftJob job;
        job.kind = kind();
        job.drop = coord();
        job.ordered = stream.readUint32();
        job.lifted = stream.readBool();
        job.landed = stream.readBool();
        if (!valid(job.drop) || !airliftJobs.emplace(id, job).second) throw std::runtime_error("Invalid airlift job");
    }
    airliftDrop = AirliftDrop{};
    airliftDrop.kind = kind();
    airliftDrop.point = coord();
    airliftDrop.target = coord();
    airliftDrop.wave = stream.readUint32();
    airliftDrop.planned = stream.readUint32();
    airliftDrop.firstLanding = stream.readUint32();
    const Uint32 pending = stream.readUint32();
    if (pending > kMaxEntries || !valid(airliftDrop.point) || !valid(airliftDrop.target))
        throw std::runtime_error("Invalid airlift drop");
    for (Uint32 i = 0; i < pending; ++i) airliftDrop.pending.push_back(stream.readUint32());
    airliftLocalDefeat = stream.readBool();
    airliftRetreating = stream.readBool();
    airliftStats.rearDrops = stream.readSint32();
    airliftStats.flankDrops = stream.readSint32();
    airliftRaid = AirliftDrop{};
    airliftRaidNext = 0;
    if (currentGame->getLoadedSavegameVersion() < SAVEGAMEVERSION_AIRLIFT_RAIDS) return;
    for (auto& [id, job] : airliftJobs) job.objective = stream.readUint32();
    airliftRaid.kind = kind();
    airliftRaid.point = coord();
    airliftRaid.target = coord();
    airliftRaid.objective = stream.readUint32();
    airliftRaid.planned = stream.readUint32();
    const Uint32 raiders = stream.readUint32();
    if (raiders > kMaxEntries || !valid(airliftRaid.point) || !valid(airliftRaid.target))
        throw std::runtime_error("Invalid airlift raid");
    for (Uint32 i = 0; i < raiders; ++i) airliftRaid.pending.push_back(stream.readUint32());
    airliftRaidNext = stream.readUint32();
    airliftStats.harvesterRaids = stream.readSint32();
    airliftStats.perimeterRaids = stream.readSint32();
}
