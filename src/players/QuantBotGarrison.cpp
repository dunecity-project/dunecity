/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// QuantBot ZoneGarrison (QuantBot Config [Force Concentration] ZoneGarrison; docs/zone-garrisons.md):
// with the Zone Garrisons match rule on, QuantBot puts its Soldiers and Troopers into its own city
// zones while enemies close on them, brings them out once the area is quiet, and takes empty enemy
// zones its soldiers pass. Stateless: every pass decides from the map and the zones' own garrisons,
// so nothing is saved and every peer makes the same choices.

#include "QuantBotInternal.h"
#include <structures/ZoneStructure.h>
#include <units/InfantryBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>

#include <algorithm>
#include <map>
#include <vector>

namespace {

/// A visible enemy within this many tiles of an own zone threatens it.
constexpr int kThreatRadius = 7;
/// A garrison comes out when no visible enemy is within this many tiles (hysteresis over the threat).
constexpr int kReleaseRadius = 11;
/// Own soldiers this close to a threatened zone answer it.
constexpr int kResponderRadius = 12;
/// An empty enemy zone this close to one of our soldiers is taken.
constexpr int kOffenceRadius = 5;
/// Capture orders a house gives in one pass, defence and offence together.
constexpr int kMaxOrdersPerPass = 6;

ZoneStructure* zoneOnTile(const Map& map, int x, int y) {
    if (!map.tileExists(x, y)) return nullptr;
    return dynamic_cast<ZoneStructure*>(map.getTile(x, y)->getGroundObject());
}

/// A soldier can only enter from a tile beside the zone (diagonals too): one in a city block
/// packed on every side never gets there and was ordered at it again every pass.
bool hasOpenSide(const Map& map, const ZoneStructure* zone) {
    const Coord at = zone->getLocation();
    const Coord size = zone->getStructureSize();
    for (int y = at.y - 1; y <= at.y + size.y; ++y) {
        for (int x = at.x - 1; x <= at.x + size.x; ++x) {
            if (x >= at.x && x < at.x + size.x && y >= at.y && y < at.y + size.y) continue;
            if (!map.tileExists(x, y)) continue;
            const Tile* tile = map.getTile(x, y);
            if (!tile->hasANonInfantryGroundObject() && tile->infantryNotFull()) return true;
        }
    }
    return false;
}

bool garrisonSoldier(const UnitBase* unit) {
    return unit->getItemID() == Unit_Soldier || unit->getItemID() == Unit_Trooper;
}

} // namespace

void QuantBot::updateZoneGarrisons() {
    if (!botConfig().force.zoneGarrison || !ZoneStructure::garrisonsEnabled() || builderOnly()
        || currentGame == nullptr || !currentGame->isCitySimEnabled()) return;
    AITelemetry::PerformanceScope perfScope("ai.zoneGarrisons", getGameCycleCount(), getHouse()->getHouseID());
    const House* house = getHouse();
    const int team = house->getTeamID();
    const Map& map = getMap();

    // Our soldiers free to act and the enemies we can see, in unit-list order.
    std::vector<const UnitBase*> soldiers;
    std::vector<const UnitBase*> enemies;
    std::map<Uint32, int> pending;   // zone -> own soldiers already ordered into it
    for (const UnitBase* unit : getUnitList()) {
        if (!unit->isActive() || unit->getHealth() <= 0) continue;
        if (unit->getOwner() == house) {
            if (!garrisonSoldier(unit) || !unit->isRespondable()) continue;
            if (unit->getAttackMode() == CAPTURE) {
                if (const auto* zone = dynamic_cast<const ZoneStructure*>(unit->getTarget())) {
                    ++pending[zone->getObjectID()];
                    continue;
                }
                // Its zone is gone or the order ended without a target: back to the army.
                doSetAttackMode(unit, AREAGUARD);
                continue;
            }
            soldiers.push_back(unit);
        } else if (unit->getOwner()->getTeamID() != team && unit->isAGroundUnit()
                   && unit->getItemID() != Unit_Sandworm && unit->isVisible(team)
                   && (unit->canAttack() || unit->canCaptureStructures())) {
            enemies.push_back(unit);
        }
    }

    // 1. Threat on each of our zones: one point an enemy, three a soldier that can capture.
    std::map<Uint32, int> threat;
    std::map<Uint32, ZoneStructure*> zones;
    std::vector<Uint32> seen;
    for (const UnitBase* enemy : enemies) {
        const Coord at = enemy->getLocation();
        const int points = enemy->isInfantry() && enemy->canCaptureStructures() ? 3 : 1;
        seen.clear();
        for (int y = at.y - kThreatRadius; y <= at.y + kThreatRadius; ++y) {
            for (int x = at.x - kThreatRadius; x <= at.x + kThreatRadius; ++x) {
                ZoneStructure* zone = zoneOnTile(map, x, y);
                if (zone == nullptr || zone->getOwner() != house) continue;
                const Uint32 id = zone->getObjectID();
                if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
                seen.push_back(id);
                if (blockDistance(at, zone->getClosestPoint(at)) > kThreatRadius) continue;
                threat[id] += points;
                zones[id] = zone;
            }
        }
    }

    int orders = 0;
    std::vector<Uint32> used;
    auto order = [&](const UnitBase* soldier, const ZoneStructure* zone, const char* why) {
        doCaptureStructure(static_cast<const InfantryBase*>(soldier), zone);
        used.push_back(soldier->getObjectID());
        ++pending[zone->getObjectID()];
        ++orders;
        traceDecision("zone_garrison_order", AITelemetry::Record().set("why", std::string(why))
            .set("unit", soldier->getObjectID()).set("zone", zone->getObjectID())
            .set("zone_house", zone->getOwner()->getHouseID()).set("occupants", zone->getGarrisonSize()));
    };
    auto available = [&](const UnitBase* soldier) {
        return std::find(used.begin(), used.end(), soldier->getObjectID()) == used.end()
            && soldier->getHealth() * 2 >= soldier->getMaxHealth()
            && unitRole(soldier, {UnitRole::Human, UnitRole::Repair, UnitRole::Escort, UnitRole::ColonyGuard})
                   == UnitRole::Free;
    };

    // 2. Defence: the most threatened zones first, the nearest free soldiers into them.
    std::vector<std::pair<int, Uint32>> ranked;
    for (const auto& [id, points] : threat) ranked.emplace_back(-points, id);
    std::sort(ranked.begin(), ranked.end());
    for (const auto& [negPoints, id] : ranked) {
        if (orders >= kMaxOrdersPerPass) break;
        ZoneStructure* zone = zones[id];
        if (!hasOpenSide(map, zone)) continue;
        const int want = std::min(DuneCity::ZoneGarrison::kCapacity, (-negPoints + 1) / 2);
        int have = zone->getGarrisonSize() + pending[id];
        while (have < want && orders < kMaxOrdersPerPass) {
            const UnitBase* best = nullptr;
            FixPoint bestDistance = FixPt_MAX;
            for (const UnitBase* soldier : soldiers) {
                if (!available(soldier) || !zone->mayEnterGarrison(soldier)) continue;
                const Coord at = soldier->getLocation();
                const FixPoint distance = blockDistance(at, zone->getClosestPoint(at));
                if (distance > kResponderRadius) continue;
                if (distance < bestDistance) { best = soldier; bestDistance = distance; }
            }
            if (best == nullptr) break;
            order(best, zone, "defend");
            ++have;
        }
    }

    // 3. Release: a garrison of ours with no visible enemy near comes out to rejoin the army.
    // A house a human shares keeps the garrisons its human put there.
    if (house->isAI()) {
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr || !zone->isGarrisoned() || zone->getGarrisonHouse() != house) continue;
            bool quiet = true;
            for (const UnitBase* enemy : enemies) {
                const Coord at = enemy->getLocation();
                if (blockDistance(at, zone->getClosestPoint(at)) <= kReleaseRadius) { quiet = false; break; }
            }
            if (!quiet) continue;
            const int out = zone->evacuateGarrison();
            traceDecision("zone_garrison_release", AITelemetry::Record().set("zone", zone->getObjectID())
                .set("deployed", out));
        }
    }

    // 4. Offence: a soldier passing an empty enemy zone takes it (one soldier a zone a pass).
    for (const UnitBase* soldier : soldiers) {
        if (orders >= kMaxOrdersPerPass) break;
        if (!available(soldier)) continue;
        const Coord at = soldier->getLocation();
        const ZoneStructure* best = nullptr;
        FixPoint bestDistance = FixPt_MAX;
        for (int y = at.y - kOffenceRadius; y <= at.y + kOffenceRadius; ++y) {
            for (int x = at.x - kOffenceRadius; x <= at.x + kOffenceRadius; ++x) {
                const ZoneStructure* zone = zoneOnTile(map, x, y);
                if (zone == nullptr || zone->getOwner()->getTeamID() == team || zone->isGarrisoned()
                    || pending[zone->getObjectID()] > 0 || !zone->isVisible(team)
                    || !zone->mayEnterGarrison(soldier) || !hasOpenSide(map, zone)) continue;
                const FixPoint distance = blockDistance(at, zone->getClosestPoint(at));
                if (distance > kOffenceRadius) continue;
                if (distance < bestDistance || (distance == bestDistance && zone->getObjectID() < best->getObjectID())) {
                    best = zone;
                    bestDistance = distance;
                }
            }
        }
        if (best != nullptr) order(soldier, best, "take");
    }
}
