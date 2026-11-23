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
constexpr int kMinDropCarryalls = 2;   ///< Free Carryalls a drop needs before it is planned.
constexpr int kRescueHomeTiles = 15;   ///< Nearer home than this a unit is not flown out.
constexpr int kRescueAloneTiles = 6;   ///< No friendly fighter this close: the unit is cut off.
constexpr int kRescueOrdersPerPass = 2;

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
        && (force.airliftRearDrop || force.airliftFlankDrop || force.airliftRetreatLift || force.airliftRescue);
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

bool QuantBot::orderAirlift(const UnitBase* unit, Coord drop, AirliftKind kind) {
    if (!unit || !getMap().tileExists(drop)) return false;
    const auto* ground = static_cast<const GroundUnit*>(unit);
    const ATTACKMODE before = unit->getAttackMode();
    doMove2Pos(unit, drop.x, drop.y, true);
    if (!doRequestCarryallDrop(ground) || !ground->hasBookedCarrier()) {
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
        if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isAGroundUnit()) {
            if (job.lifted && !job.landed) ++airliftStats.lostInLift;   // died in the air with its Carryall
            else if (!job.lifted) ++airliftStats.lostWaiting;           // died before the Carryall came
            it = airliftJobs.erase(it);
            continue;
        }
        const auto* ground = static_cast<const GroundUnit*>(unit);
        if (unit->isPickedUp()) {
            if (!job.lifted) { job.lifted = true; ++airliftStats.lifted; }
            ++it;
            continue;
        }
        if (job.lifted && !job.landed && unit->isActive()) {
            job.landed = true;
            ++airliftStats.landed;
            if (job.kind == AirliftKind::Rear || job.kind == AirliftKind::Flank) {
                // The landed part of a drop waits for the rest (updateAirliftDrop strikes).
                if (airliftDrop.firstLanding == 0) airliftDrop.firstLanding = now;
                ++it;
            } else {
                it = airliftJobs.erase(it);
            }
            continue;
        }
        if (job.landed) { ++it; continue; }
        // Not lifted yet. A lost booking (the aircraft was shot down or took other work) is
        // rebooked at once or given up: it never drives on alone. Ninety seconds bound a
        // unit that sits in a repair bay or waits for an aircraft that does not arrive.
        const bool waiting = unit->isActive() && (ground->isAwaitingPickup() || ground->hasBookedCarrier());
        const bool overdue = now - job.ordered > MILLI2CYCLES(90000);
        if (unit->isActive() && (!waiting || overdue)) {
            const bool rebooked = !overdue && airliftFreeCarryalls() > 0 && unit->getAttackMode() == CARRYALLREQUESTED
                && doRequestCarryallDrop(ground) && ground->hasBookedCarrier();
            if (!rebooked) {
                const_cast<GroundUnit*>(ground)->cancelCarryallPickup();
                doMove2Pos(unit, unit->getLocation().x, unit->getLocation().y, false);
                const bool strike = job.kind == AirliftKind::Rear || job.kind == AirliftKind::Flank;
                doSetAttackMode(unit, strike ? HUNT : AREAGUARD);
                ++airliftStats.cancelled;
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

    // The strongest whole vehicles of the wave, at most a third of it: the wave stays the main
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
    const int size = std::min({int(group.size()), std::max(0, force.airliftGroup), waveVehicles / 3});
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
    // One vehicle at a time: under observed enemy fire, away from home, and either below half
    // health without a repair trip of its own or with no friendly fighter near it.
    if (!botConfig().force.airliftRescue) return;
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
        const bool damaged = unit->getHealth() * 2 < unit->getMaxHealth();
        bool alone = true;
        if (!damaged) {
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

void QuantBot::updateAirlift() {
    if (!airliftActive()) {
        // Switched off (a config change between a save and its reload): nothing stays held.
        if (!airliftJobs.empty() || airliftDrop.point.isValid()) {
            airliftJobs.clear();
            airliftDrop = AirliftDrop{};
        }
        return;
    }
    trackAirliftJobs();
    if (getHouse()->getNumItems(Unit_Carryall) <= 0 && airliftJobs.empty() && !airliftDrop.point.isValid()) return;
    updateAirliftRescue();
    updateAirliftRetreat();
    updateAirliftDrop();
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
}

void QuantBot::loadAirlift(InputStream& stream) {
    constexpr Uint32 kMaxEntries = 65536;
    const auto coord = [&]() { Coord v; v.x = stream.readSint32(); v.y = stream.readSint32(); return v; };
    const auto kind = [&]() {
        const Uint8 value = stream.readUint8();
        if (value > static_cast<Uint8>(AirliftKind::Rescue)) throw std::runtime_error("Invalid airlift kind");
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
}
