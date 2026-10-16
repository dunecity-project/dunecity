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

// QuantBot hunt parties - Blob waves (docs/formations.md, "Blob waves"): a Custom Hard/Brutal
// ground wave gathers at a rally point towards its objective, marches as one blob leg by leg
// (the fast members wait at each leg's end for the rest, so the blob keeps its slowest member's
// pace), releases into engine Hunt on contact, reforms when the fight is quiet, and falls back
// a few tiles to reform when it is locally outmatched (the tactical retreat).
//
// Deterministic: parties are a vector in launch order, members ordered sets, every scan in a
// fixed list, tile or id order, and only integer arithmetic on simulation state decides an order.

#include "QuantBotInternal.h"

#include <FormationOrders.h>

using QuantBotInternal::mobileCombatItem;
using BlobWavePolicy::Phase;

namespace {

/// Squared Euclidean distance in tiles.
int64_t distanceSquared(Coord a, Coord b) {
    const int64_t dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

int tiles(Coord a, Coord b) { return blockDistance(a, b).lround(); }

} // namespace

bool QuantBot::blobMarch() const {
    return autonomousCustomHardBrutal() && waveFormationShape() == QuantBotConfig::kWaveFormationBlob;
}

bool QuantBot::tacticalRetreat() const {
    if (!autonomousCustomHardBrutal()) return false;
    if (tacticalRetreatOverride >= 0) return tacticalRetreatOverride > 0;
    return blobMarch();
}

QuantBot::BlobParty* QuantBot::huntPartyOf(Uint32 id) {
    for (auto& party : blobParties) if (party.members.count(id)) return &party;
    return nullptr;
}

const QuantBot::BlobParty* QuantBot::huntPartyOf(Uint32 id) const {
    for (const auto& party : blobParties) if (party.members.count(id)) return &party;
    return nullptr;
}

const UnitBase* QuantBot::huntPartyAnchor(const BlobParty& party) const {
    // OpenRA's squad leader: the member nearest the members' centre, so a regroup towards it
    // moves the fewest units. Vehicles first, as OpenRA prefers the most restricted
    // locomotor: infantry can stand on mountains that the rest cannot reach.
    std::vector<const UnitBase*> active;
    bool vehicles = false;
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isActive() || unit->getHealth() <= 0) continue;
        active.push_back(unit);
        vehicles = vehicles || !unit->isInfantry();
    }
    if (active.empty()) return nullptr;
    int64_t sx = 0, sy = 0, n = 0;
    for (const auto* unit : active) {
        if (vehicles && unit->isInfantry()) continue;
        sx += unit->getLocation().x; sy += unit->getLocation().y; ++n;
    }
    const UnitBase* best = nullptr;
    int64_t bestDistance = 0;
    for (const auto* unit : active) {
        if (vehicles && unit->isInfantry()) continue;
        // n times the offset from the centroid: exact in integers.
        const int64_t dx = unit->getLocation().x * n - sx, dy = unit->getLocation().y * n - sy;
        const int64_t distance = dx * dx + dy * dy;
        if (!best || distance < bestDistance) { best = unit; bestDistance = distance; }
    }
    return best;
}

Coord QuantBot::huntPartyPoint(Coord desired, const UnitBase* anchor, bool safe) const {
    if (!anchor) return desired;
    const Coord from = anchor->getLocation();
    // Reachable for the party's vehicles: the anchor's terrain region (an infantry anchor on
    // a mountain has none, and then any standable tile will do).
    const int region = getMap().vehicleTerrainRegion(from);
    auto usable = [&](Coord p, bool needSafe) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        if (tile->isMountain() || tile->hasAStructure() || tile->isSpiceBloom()) return false;
        if (region != 0 && getMap().vehicleTerrainRegion(p) != region) return false;
        return !needSafe || dangerAt(p) == 0;
    };
    auto search = [&](Coord centre, int rings, bool needSafe) {
        for (int ring = 0; ring <= rings; ++ring)
            for (int dy = -ring; dy <= ring; ++dy)
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                    const Coord p = centre + Coord(dx, dy);
                    if (usable(p, needSafe)) return p;
                }
        return Coord::Invalid();
    };
    Coord point = search(desired, 8, safe);
    if (point.isInvalid() && safe) point = search(desired, 8, false);
    if (point.isInvalid()) point = search(from, 4, false);
    return point.isValid() ? point : from;
}

Coord QuantBot::huntPartySlot(const UnitBase* unit, Coord centre, int radius, std::set<int64_t>& reserved) const {
    if (!unit || centre.isInvalid()) return Coord::Invalid();
    const int r = std::max(1, radius);
    const auto key = [&](Coord p) { return int64_t(p.y) * getMap().getSizeX() + p.x; };
    auto usable = [&](int dx, int dy) {
        const Coord p = centre + Coord(dx, dy);
        if (!getMap().tileExists(p) || tiles(centre, p) > r || reserved.count(key(p))) return false;
        return !getMap().getTile(p)->isSpiceBloom() && unit->canPass(p.x, p.y);
    };
    Coord slot = Coord::Invalid();
    // The established hashed scatter first (as withdrawUnit()), so members spread over the
    // area instead of queueing for one tile; then the nearest free tile.
    if (const auto offset = SimpleArmyPolicy::rallyOffset(unit->getObjectID(), r, usable))
        slot = centre + Coord(offset->first, offset->second);
    for (int ring = 0; ring <= r && slot.isInvalid(); ++ring)
        for (int dy = -ring; dy <= ring && slot.isInvalid(); ++dy)
            for (int dx = -ring; dx <= ring && slot.isInvalid(); ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == ring && usable(dx, dy)) slot = centre + Coord(dx, dy);
    if (slot.isValid()) reserved.insert(key(slot));
    return slot;
}

const ObjectBase* QuantBot::huntPartyObjective(const BlobParty& party, const UnitBase* anchor) const {
    const int myTeam = getHouse()->getTeamID();
    if (const auto* current = getObject(party.objective)) {
        if (current->getHealth() > 0 && current->getOwner() && current->getOwner()->getTeamID() != myTeam
            && current->isVisible(myTeam)) return current;
    }
    if (!anchor) return nullptr;
    // The wave's own objective choice (the observed enemy base the strength survey anchors
    // on), else the target engine Hunt itself would pick: the same as the formation waves.
    if (const auto* objective = customWaveObjective(anchor)) return objective;
    return anchor->findClosestTarget();
}

const ObjectBase* QuantBot::huntPartyContact(const BlobParty& party, const UnitBase* anchor) const {
    if (!anchor) return nullptr;
    const int myTeam = getHouse()->getTeamID();
    struct Reach { Coord at; int range; };
    std::vector<Reach> reach;
    int maxRange = 0, extent = 0;
    const Coord at = anchor->getLocation();
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isActive() || unit->getHealth() <= 0) continue;
        reach.push_back({unit->getLocation(), unit->huntTowardRange()});
        maxRange = std::max(maxRange, unit->huntTowardRange());
        extent = std::max(extent, tiles(at, unit->getLocation()));
    }
    const ObjectBase* best = nullptr;
    int64_t bestDistance = 0;
    // Contact: a visible enemy fighter or building within a member's huntTowardRange() (its
    // weapon or view range) - the distance at which engine Hunt itself stops for an enemy.
    auto consider = [&](const ObjectBase* enemy) {
        if (!enemy->getOwner() || enemy->getOwner()->getTeamID() == myTeam || enemy->getHealth() <= 0
            || !enemy->isVisible(myTeam)) return;
        const Coord near = enemy->getClosestPoint(at);
        if (tiles(at, near) > extent + maxRange) return;
        bool inReach = false;
        for (const auto& member : reach)
            if (tiles(member.at, enemy->getClosestPoint(member.at)) <= member.range) { inReach = true; break; }
        if (!inReach) return;
        const int64_t distance = distanceSquared(at, near);
        // Nearest, then lowest object id: the same answer in any list order.
        if (!best || distance < bestDistance || (distance == bestDistance && enemy->getObjectID() < best->getObjectID())) {
            best = enemy; bestDistance = distance;
        }
    };
    for (const auto* unit : getUnitList())
        if (unit->isActive() && !unit->isAFlyingUnit() && unit->canAttack() && unit->getItemID() != Unit_Sandworm)
            consider(unit);
    for (const auto* structure : getStructureList()) consider(structure);
    return best;
}

void QuantBot::sendHuntParty(const BlobParty& party, Coord point, bool forced, bool onlyIdle) {
    const int radius = BlobWavePolicy::blobRadius(int(party.members.size())) + 1;
    std::set<int64_t> reserved;
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isActive() || !unit->isRespondable() || unit->getHealth() <= 0) continue;
        const bool there = tiles(unit->getLocation(), point) <= radius;
        if (onlyIdle) {
            // Lost its walk (blocked, or an engine reaction): send it again.
            if (there || unit->isMoving() || unit->hasPendingPathRequest() || unit->hasATarget()) continue;
        } else if (there && !forced) {
            // Already there: stand. Area Guard answers whatever reaches it (a hit releases the
            // party), but never sets off after the nearest enemy anywhere.
            doSetAttackMode(unit, AREAGUARD);
            doMove2Pos(unit, unit->getLocation().x, unit->getLocation().y, false);
            continue;
        }
        Coord slot = huntPartySlot(unit, point, radius, reserved);
        if (slot.isInvalid()) slot = point;
        // As withdrawUnit(): Area Guard first (Guard would cancel the move), then the move.
        doSetAttackMode(unit, AREAGUARD);
        doMove2Pos(unit, slot.x, slot.y, forced);
    }
}

void QuantBot::startHuntParty(const std::vector<const UnitBase*>& units, const ObjectBase* objective) {
    const Uint32 now = getGameCycleCount();
    BlobParty party;
    party.since = now;
    party.lastContact = now;
    for (const auto* unit : units)
        if (FormationOrders::eligible(unit) && mobileCombatItem(unit->getItemID()))
            party.members.insert(unit->getObjectID());
    auto huntAll = [&] { for (const auto* unit : units) doSetAttackMode(unit, HUNT); };
    if (static_cast<int>(party.members.size()) < BlobWavePolicy::kMinMembers) { huntAll(); return; }
    if (!blobMarch()) {
        // Tactical retreat without the blob: today's plain Hunt wave, tracked as a party so
        // that it can fall back and reform together.
        party.phase = Phase::Fight;
        huntAll();
        blobParties.push_back(party);
        ++blobStats.parties;
        traceDecision("hunt_party", AITelemetry::Record().set("members", int(party.members.size()))
            .set("phase", BlobWavePolicy::phaseName(party.phase)));
        return;
    }
    const UnitBase* anchor = huntPartyAnchor(party);
    if (!objective) objective = huntPartyObjective(party, anchor);
    if (!anchor || !objective) { huntAll(); return; }
    party.objective = objective->getObjectID();
    party.goal = objective->getClosestPoint(anchor->getLocation());
    const auto offset = BlobWavePolicy::gatherOffset(anchor->getLocation().x, anchor->getLocation().y,
                                                     party.goal.x, party.goal.y);
    const Coord rally = huntPartyPoint(anchor->getLocation() + Coord(offset.first, offset.second), anchor, true);
    blobParties.push_back(party);
    ++blobStats.parties;
    startPartyGather(blobParties.back(), Phase::Gather, rally);
    traceDecision("hunt_party", AITelemetry::Record().set("members", int(party.members.size()))
        .set("phase", "gather").set("rally_x", rally.x).set("rally_y", rally.y)
        .set("objective", party.objective).set("goal_x", party.goal.x).set("goal_y", party.goal.y));
}

namespace {

/// The slowest member's travel time (cycles) to \a point, straight-line at its open-sand speed,
/// over the members within \a ignoreBeyond tiles of it (without foot infantry if \a vehicles).
template<class Lookup>
int64_t slowestTravel(const Lookup& lookup, const std::set<Uint32>& members, Coord point, int ignoreBeyond,
                      bool vehicles) {
    int64_t slowest = 0;
    for (const Uint32 id : members) {
        const auto* unit = dynamic_cast<const UnitBase*>(lookup(id));
        if (!unit || !unit->isActive() || (vehicles && unit->isInfantry())) continue;
        const int distance = tiles(unit->getLocation(), point);
        const FixPoint speed = unit->getFormationReferenceSpeed();
        if (distance > ignoreBeyond || speed <= 0) continue;
        slowest = std::max<int64_t>(slowest, distance * (FixPoint(TILESIZE) / speed).lround());
    }
    return slowest;
}

} // namespace

void QuantBot::startPartyGather(BlobParty& party, Phase phase, Coord point) {
    const Uint32 now = getGameCycleCount();
    party.phase = phase;
    party.since = now;
    party.rally = point;
    party.lastArrival = now;
    party.gathered = 0;
    party.outmatchedSince = std::numeric_limits<Uint32>::max();
    // Wait for every member's walk there (the slowest one's), but never longer than the cap.
    const auto lookup = [this](Uint32 id) { return getObject(id); };
    party.legCycles = BlobWavePolicy::travelLimit(slowestTravel(lookup, party.members, point, INT32_MAX,
        (blobVariantOverride & kBlobVehiclePace) != 0),
        MILLI2CYCLES(1000), phase == Phase::Reform ? BlobWavePolicy::kReformMaxMs : BlobWavePolicy::kGatherMaxMs);
    sendHuntParty(party, point, false, false);
}

void QuantBot::startPartyLeg(BlobParty& party, const UnitBase* anchor) {
    const int members = static_cast<int>(party.members.size());
    const int leg = BlobWavePolicy::legTiles(members);
    const Coord at = anchor->getLocation();
    const auto offset = BlobWavePolicy::legOffset(at.x, at.y, party.goal.x, party.goal.y, leg);
    party.rally = huntPartyPoint(at + Coord(offset.first, offset.second), anchor, false);
    party.since = getGameCycleCount();
    // The leg's time limit follows the slowest member's travel time over it, so the blob waits
    // for the slowest member rather than leaving it behind (Beyond All Reason's "move in
    // formation": faster units move at the speed of the slowest). Stragglers are not waited for.
    const int radius = BlobWavePolicy::blobRadius(members) + 1;
    const auto lookup = [this](Uint32 id) { return getObject(id); };
    party.legCycles = BlobWavePolicy::travelLimit(slowestTravel(lookup, party.members, party.rally, 2 * leg + radius,
        (blobVariantOverride & kBlobVehiclePace) != 0),
        MILLI2CYCLES(1000), BlobWavePolicy::kLegMaxMs);
    ++blobStats.legs;
    sendHuntParty(party, party.rally, false, false);
}

void QuantBot::startPartyMarch(BlobParty& party) {
    const UnitBase* anchor = huntPartyAnchor(party);
    const ObjectBase* objective = huntPartyObjective(party, anchor);
    if (!anchor || !objective) { dissolveHuntParty(party, true, "no_objective"); return; }
    party.objective = objective->getObjectID();
    party.goal = objective->getClosestPoint(anchor->getLocation());
    party.phase = Phase::March;
    party.progressCycle = getGameCycleCount();
    party.progressRemaining = tiles(anchor->getLocation(), party.goal);
    party.outmatchedSince = std::numeric_limits<Uint32>::max();
    startPartyLeg(party, anchor);
    traceDecision("hunt_party_march", AITelemetry::Record().set("members", int(party.members.size()))
        .set("objective", party.objective).set("goal_x", party.goal.x).set("goal_y", party.goal.y));
}

void QuantBot::releaseHuntParty(BlobParty& party, Coord contact, const char* reason) {
    const Uint32 now = getGameCycleCount();
    party.phase = Phase::Fight;
    party.since = now;
    party.lastContact = now;
    if (contact.isValid()) party.threat = contact;
    party.outmatchedSince = std::numeric_limits<Uint32>::max();
    ++blobStats.releases;
    // Unblob: every member fights as engine Hunt (as None does). A member already in contact
    // keeps its target; the others hunt their way to the contact, so the whole party arrives
    // at the fight instead of each member setting off after its own nearest enemy.
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isActive() || !unit->isRespondable()) continue;
        if (unit->formationContact() || contact.isInvalid()) {
            if (unit->getAttackMode() != HUNT || unit->wasForced()) doSetAttackMode(unit, HUNT);
        } else doHuntToward(unit, contact.x, contact.y);
    }
    traceDecision("hunt_party_release", AITelemetry::Record().set("reason", reason)
        .set("members", int(party.members.size())).set("x", contact.x).set("y", contact.y));
}

void QuantBot::startPartyFallback(BlobParty& party) {
    const UnitBase* anchor = huntPartyAnchor(party);
    if (!anchor) return;
    const Coord at = anchor->getLocation();
    const Coord home = protectedRally.isValid() ? protectedRally : findBaseCentre(getHouse()->getHouseID());
    const auto offset = BlobWavePolicy::fallbackOffset(at.x, at.y, party.threat.x, party.threat.y,
        party.threat.isValid(), home.x, home.y, home.isValid(), BlobWavePolicy::kFallbackTiles);
    const Coord point = huntPartyPoint(at + Coord(offset.first, offset.second), anchor, true);
    party.phase = Phase::Fallback;
    party.since = getGameCycleCount();
    party.rally = point;
    party.outmatchedSince = std::numeric_limits<Uint32>::max();
    ++party.fallbacks;
    ++blobStats.fallbacks;
    // A forced move, as withdrawUnit(): it disengages, and the engine clears it on arrival so
    // the unit defends itself at the fall-back point.
    sendHuntParty(party, point, true, false);
    traceDecision("hunt_party_fallback", AITelemetry::Record().set("members", int(party.members.size()))
        .set("from_x", at.x).set("from_y", at.y).set("x", point.x).set("y", point.y)
        .set("threat_x", party.threat.x).set("threat_y", party.threat.y).set("count", int(party.fallbacks)));
}

void QuantBot::dissolveHuntParty(BlobParty& party, bool hunt, const char* reason) {
    if (hunt) {
        for (const Uint32 id : party.members) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
            if (!unit || unit->getOwner() != getHouse() || !unit->isActive() || unit->getHealth() <= 0) continue;
            if (unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence}) != UnitRole::Free) continue;
            if (unit->getAttackMode() != HUNT || unit->wasForced()) doSetAttackMode(unit, HUNT);
        }
    }
    ++blobStats.dissolved;
    traceDecision("hunt_party_end", AITelemetry::Record().set("reason", reason)
        .set("phase", BlobWavePolicy::phaseName(party.phase)).set("members", int(party.members.size())));
    party.members.clear();
}

void QuantBot::huntPartyMemberHit(const UnitBase* unit, const ObjectBase* damager) {
    if (!unit || !damager) return;
    BlobParty* party = huntPartyOf(unit->getObjectID());
    if (!party) return;
    const Coord contact = damager->getClosestPoint(unit->getLocation());
    party->lastContact = getGameCycleCount();
    party->threat = contact;
    if (party->phase == Phase::Gather || party->phase == Phase::March || party->phase == Phase::Reform) {
        if ((blobVariantOverride & kBlobRealFight) && !huntPartyRealFight(*party, damager)) {
            // A lone raider: the member that was hit answers it, the blob marches on.
            if (unit->canAttack(damager) && unit->getTarget() != damager) doAttackObject(unit, damager, false);
            return;
        }
        releaseHuntParty(*party, contact, "hit");
    }
}

bool QuantBot::huntPartyRealFight(const BlobParty& party, const ObjectBase* contact) const {
    if (!contact || contact->isAStructure()) return true;     // a base, a turret: the real thing
    const int myTeam = getHouse()->getTeamID();
    const auto& data = currentGame->objectData.data;
    const Coord at = contact->getLocation();
    int64_t hostile = 0, own = 0;
    for (const auto* unit : getUnitList()) {
        if (!unit->getOwner() || unit->getOwner()->getTeamID() == myTeam || unit->getHealth() <= 0 || !unit->isActive()
            || !unit->canAttack() || !mobileCombatItem(unit->getItemID()) || !unit->isVisible(myTeam)
            || tiles(at, unit->getLocation()) > BlobWavePolicy::kLocalRadiusTiles) continue;
        hostile += CombatPowerPolicy::unitPower(data[unit->getItemID()][unit->getOriginalHouseID()].price,
            unit->getHealth().lround(), unit->getMaxHealth());
    }
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getHealth() <= 0) continue;
        own += CombatPowerPolicy::unitPower(data[unit->getItemID()][unit->getOriginalHouseID()].price,
            unit->getHealth().lround(), unit->getMaxHealth());
    }
    // A fight is real when the enemy there is at least a quarter of the party.
    return hostile * 4 >= own;
}

void QuantBot::huntPartySkirmish(const BlobParty& party, const ObjectBase* contact) {
    // The three members nearest the scout shoot it (id order breaks ties); the rest march on.
    std::vector<std::pair<int64_t, const UnitBase*>> nearest;
    for (const Uint32 id : party.members) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || !unit->isActive() || !unit->isRespondable() || !unit->canAttack(contact)) continue;
        nearest.push_back({distanceSquared(unit->getLocation(), contact->getClosestPoint(unit->getLocation())), unit});
    }
    std::stable_sort(nearest.begin(), nearest.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < nearest.size() && i < 3; ++i)
        if (nearest[i].second->getTarget() != contact) doAttackObject(nearest[i].second, contact, false);
}

void QuantBot::updateHuntParties() {
    if (blobParties.empty()) return;
    AITelemetry::PerformanceScope perfScope("ai.huntParties", getGameCycleCount(), getHouse()->getHouseID());
    if (!huntParties()) {
        for (auto& party : blobParties) dissolveHuntParty(party, true, "disabled");
        blobParties.clear();
        return;
    }
    // The house posture outranks a party: a strategic withdrawal (and, without the tactical
    // retreat, a local one) recalls the tracked wave home through applyArmyPosture().
    if (recoveryActive() && (ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture)
                             || (localSquadWithdraw && !tacticalRetreat()))) {
        for (auto& party : blobParties) dissolveHuntParty(party, false, "recall");
        blobParties.clear();
        return;
    }
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    const auto& data = currentGame->objectData.data;
    for (auto& party : blobParties) {
        // Members leave when they die, change owner or are claimed by a human order, a repair
        // run or a defence assignment (those outrank the offensive, as for every wave).
        for (auto it = party.members.begin(); it != party.members.end();) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(*it));
            const bool keep = unit && unit->getOwner() == getHouse() && unit->getHealth() > 0
                && unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence}) == UnitRole::Free;
            it = keep ? std::next(it) : party.members.erase(it);
        }
        std::vector<const UnitBase*> active;
        for (const Uint32 id : party.members) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
            if (unit->isActive() && unit->isRespondable()) active.push_back(unit);
        }
        const int members = static_cast<int>(party.members.size());
        if (members < BlobWavePolicy::kMinMembers || active.empty()) {
            dissolveHuntParty(party, true, "too_few");
            continue;
        }
        const UnitBase* anchor = huntPartyAnchor(party);
        if (!anchor) { dissolveHuntParty(party, true, "no_anchor"); continue; }
        const int radius = BlobWavePolicy::blobRadius(members) + 1;
        const Uint32 elapsed = ArmyPosturePolicy::elapsed(now, party.since);
        // Contact while gathering, marching or reforming unblobs the party; during a fight it
        // keeps the fight going. A fall-back ignores it: the fall-back is the answer to it.
        const ObjectBase* contact = party.phase == Phase::Fallback ? nullptr : huntPartyContact(party, anchor);
        const Coord contactAt = contact ? contact->getClosestPoint(anchor->getLocation()) : Coord::Invalid();

        switch (party.phase) {
            case Phase::Gather:
            case Phase::Reform: {
                if (contact && (!(blobVariantOverride & kBlobRealFight) || huntPartyRealFight(party, contact))) {
                    releaseHuntParty(party, contactAt, "contact"); break;
                }
                if (contact) huntPartySkirmish(party, contact);
                // Vehicle pace: foot infantry neither holds the gather nor counts towards it.
                const bool vehiclePace = (blobVariantOverride & kBlobVehiclePace) != 0;
                const int quorum = vehiclePace ? 7500 : BlobWavePolicy::kGatherQuorumBps;
                int paced = 0;
                int gathered = 0;
                bool awayMoving = false;
                for (const auto* unit : active) {
                    if (vehiclePace && unit->isInfantry()) continue;
                    ++paced;
                    if (tiles(unit->getLocation(), party.rally) <= radius) ++gathered;
                    else awayMoving = awayMoving || unit->isMoving() || unit->hasPendingPathRequest();
                }
                if (paced == 0) paced = members;
                if (Uint32(gathered) > party.gathered) { party.gathered = gathered; party.lastArrival = now; }
                const bool reform = party.phase == Phase::Reform;
                const Uint32 maxCycles = party.legCycles;
                if (!BlobWavePolicy::gatherComplete(paced, gathered, elapsed,
                        ArmyPosturePolicy::elapsed(now, party.lastArrival), maxCycles,
                        MILLI2CYCLES(BlobWavePolicy::kGatherStallMs), awayMoving, quorum)) {
                    sendHuntParty(party, party.rally, false, true);
                    break;
                }
                if (elapsed >= maxCycles && int64_t(gathered) * 10000 < int64_t(quorum) * paced)
                    ++blobStats.gatherTimeouts;
                if (reform) ++blobStats.reforms; else ++blobStats.gathered;
                traceDecision("hunt_party_formed", AITelemetry::Record().set("phase", BlobWavePolicy::phaseName(party.phase))
                    .set("members", members).set("gathered", gathered).set("elapsed_cycles", int(elapsed)));
                if (blobMarch()) startPartyMarch(party);
                else releaseHuntParty(party, Coord::Invalid(), "reformed");
            } break;

            case Phase::March: {
                if (contact && (!(blobVariantOverride & kBlobRealFight) || huntPartyRealFight(party, contact))) {
                    releaseHuntParty(party, contactAt, "contact"); break;
                }
                if (contact) huntPartySkirmish(party, contact);
                // A lost objective is replaced; with nothing left to attack the party hunts.
                const ObjectBase* objective = huntPartyObjective(party, anchor);
                if (!objective) { dissolveHuntParty(party, true, "no_objective"); break; }
                if (objective->getObjectID() != party.objective) {
                    party.objective = objective->getObjectID();
                    party.goal = objective->getClosestPoint(anchor->getLocation());
                    party.progressRemaining = std::numeric_limits<Sint32>::max();
                }
                const int leg = BlobWavePolicy::legTiles(members);
                const int anchorRemaining = tiles(anchor->getLocation(), party.goal);
                // Within half a leg of the objective without contact: hunt it from here.
                if (anchorRemaining <= leg / 2) { releaseHuntParty(party, party.goal, "arrived"); break; }
                if (anchorRemaining < party.progressRemaining) {
                    party.progressRemaining = anchorRemaining;
                    party.progressCycle = now;
                } else if (ArmyPosturePolicy::elapsed(now, party.progressCycle) >= Uint32(MILLI2CYCLES(BlobWavePolicy::kMarchStallMs))) {
                    ++blobStats.stalls;
                    releaseHuntParty(party, party.goal, "stalled");
                    break;
                }
                // The leg: the fast members reach its end first and wait there in Area Guard
                // until the blob is there (or the slowest member's time is up); stragglers far
                // behind are sent along but not waited for.
                int counted = 0, arrived = 0;
                const bool vehiclePace = (blobVariantOverride & kBlobVehiclePace) != 0;
                for (const auto* unit : active) {
                    if (vehiclePace && unit->isInfantry()) continue;
                    const int distance = tiles(unit->getLocation(), party.rally);
                    if (BlobWavePolicy::straggler(distance, leg, radius)) continue;
                    ++counted;
                    if (distance <= radius) ++arrived;
                }
                if (BlobWavePolicy::legComplete(counted, arrived, elapsed, party.legCycles,
                        vehiclePace ? 7500 : BlobWavePolicy::kLegQuorumBps)) startPartyLeg(party, anchor);
                else sendHuntParty(party, party.rally, false, true);
            } break;

            case Phase::Fight: {
                if (contact) { party.lastContact = now; party.threat = contactAt; }
                if (tacticalRetreat() && party.fallbacks < BlobWavePolicy::kMaxFallbacks) {
                    // Local strength around the anchor: our ground combat units against the
                    // visible enemy combat units (aircraft included, as the posture's local
                    // verdict counts them).
                    const Coord at = anchor->getLocation();
                    int64_t friendly = 0, hostile = 0;
                    for (const auto* unit : getUnitList()) {
                        if (!unit->getOwner() || unit->getHealth() <= 0 || !unit->isActive() || !unit->canAttack()
                            || !mobileCombatItem(unit->getItemID())) continue;
                        if (tiles(at, unit->getLocation()) > BlobWavePolicy::kLocalRadiusTiles) continue;
                        const int64_t power = CombatPowerPolicy::unitPower(data[unit->getItemID()][unit->getOriginalHouseID()].price,
                            unit->getHealth().lround(), unit->getMaxHealth());
                        if (unit->getOwner()->getTeamID() == myTeam) { if (!unit->isAFlyingUnit()) friendly += power; }
                        else if (unit->isVisible(myTeam)) hostile += power;
                    }
                    if (BlobWavePolicy::outmatched(friendly, hostile, BlobWavePolicy::kOutmatchBps)) {
                        if (party.outmatchedSince == std::numeric_limits<Uint32>::max()) party.outmatchedSince = now;
                        if (BlobWavePolicy::outmatched(friendly, hostile, BlobWavePolicy::kSevereBps)
                            || ArmyPosturePolicy::elapsed(now, party.outmatchedSince) >= Uint32(MILLI2CYCLES(BlobWavePolicy::kOutmatchPersistMs))) {
                            startPartyFallback(party);
                            break;
                        }
                    } else party.outmatchedSince = std::numeric_limits<Uint32>::max();
                }
                // Quiet: reform the blob where it stands and carry on to the next objective.
                if (blobMarch() && ArmyPosturePolicy::elapsed(now, party.lastContact) >= Uint32(MILLI2CYCLES(BlobWavePolicy::kQuietMs)))
                    startPartyGather(party, Phase::Reform, huntPartyPoint(anchor->getLocation(), anchor, true));
            } break;

            case Phase::Fallback: {
                int arrived = 0;
                for (const auto* unit : active) if (tiles(unit->getLocation(), party.rally) <= radius) ++arrived;
                if (!BlobWavePolicy::fallbackComplete(members, arrived, elapsed, MILLI2CYCLES(BlobWavePolicy::kFallbackMaxMs))) {
                    sendHuntParty(party, party.rally, true, true);
                    break;
                }
                ++blobStats.fallbackReturns;
                party.lastContact = now;
                // Reform at the fall-back point, then march (or, without the blob, hunt) again.
                startPartyGather(party, Phase::Reform, party.rally);
            } break;
        }
    }
    blobParties.erase(std::remove_if(blobParties.begin(), blobParties.end(),
        [](const BlobParty& party) { return party.members.empty(); }), blobParties.end());
}

void QuantBot::saveHuntParties(OutputStream& stream) const {
    auto coord = [&](Coord c) { stream.writeSint32(c.x); stream.writeSint32(c.y); };
    stream.writeUint32(static_cast<Uint32>(blobParties.size()));
    for (const auto& party : blobParties) {
        stream.writeUint8(static_cast<Uint8>(party.phase));
        stream.writeUint32(party.since);
        stream.writeUint32(party.objective);
        coord(party.goal);
        coord(party.rally);
        coord(party.threat);
        stream.writeUint32(party.lastContact);
        stream.writeUint32(party.lastArrival);
        stream.writeUint32(party.gathered);
        stream.writeUint32(party.legCycles);
        stream.writeUint32(party.outmatchedSince);
        stream.writeUint32(party.progressCycle);
        stream.writeSint32(party.progressRemaining);
        stream.writeUint8(party.fallbacks);
        stream.writeUint32Set(party.members);
    }
}

void QuantBot::loadHuntParties(InputStream& stream) {
    auto coord = [&]() { Coord c; c.x = stream.readSint32(); c.y = stream.readSint32(); return c; };
    blobParties.clear();
    const Uint32 count = stream.readUint32();
    if (count > kBlobPartyLimit) throw std::runtime_error("Invalid hunt party state");
    for (Uint32 i = 0; i < count; ++i) {
        BlobParty party;
        const Uint8 phase = stream.readUint8();
        if (!BlobWavePolicy::validPhase(phase)) throw std::runtime_error("Invalid hunt party phase");
        party.phase = static_cast<Phase>(phase);
        party.since = stream.readUint32();
        party.objective = stream.readUint32();
        party.goal = coord();
        party.rally = coord();
        party.threat = coord();
        party.lastContact = stream.readUint32();
        party.lastArrival = stream.readUint32();
        party.gathered = stream.readUint32();
        party.legCycles = stream.readUint32();
        party.outmatchedSince = stream.readUint32();
        party.progressCycle = stream.readUint32();
        party.progressRemaining = stream.readSint32();
        party.fallbacks = stream.readUint8();
        party.members = stream.readUint32Set();
        if (party.members.size() > kCustomWaveLimit) throw std::runtime_error("Invalid hunt party members");
        blobParties.push_back(std::move(party));
    }
}
