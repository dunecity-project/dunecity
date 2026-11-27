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
// zones its soldiers pass. ZoneCaptureRaid sends free soldiers to take empty enemy zones nobody
// guards; ZoneCaptureLevy trains cheap Soldiers at the Barracks and walks them into the nearest
// enemy zones. A captured zone returns to its owner once its garrison is gone, so soldiers in a
// captured zone stay there. Stateless: every pass decides from the map and the zones' own
// garrisons, so nothing is saved and every peer makes the same choices.

#include "QuantBotInternal.h"
#include <structures/ZoneStructure.h>
#include <units/InfantryBase.h>
#include <structures/BuilderBase.h>
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
/// ZoneCaptureRaid: a free soldier this close to an unguarded enemy zone goes to take it.
constexpr int kRaidRadius = 24;
/// ZoneCaptureRaid: an enemy zone is unguarded with no enemy unit that can fight or capture
/// within this many tiles, and no enemy turret within kRaidTurretRadius.
constexpr int kRaidGuardRadius = 6;
constexpr int kRaidTurretRadius = 9;
/// ZoneCaptureLevy: Soldiers a house keeps, the credits it keeps after paying one, and how far
/// its levy walks to an enemy zone.
constexpr int kLevySoldiers = 16;
constexpr int kLevyCashReserve = 300;
constexpr int kLevyRadius = 48;

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
    const auto& force = botConfig().force;
    if ((!force.zoneGarrison && !force.zoneCaptureRaid && !force.zoneCaptureLevy)
        || !ZoneStructure::garrisonsEnabled() || builderOnly()
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

    if (force.zoneGarrison) {
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
        // A house a human shares keeps the garrisons its human put there, and a captured zone keeps
        // its garrison: it would go back to its owner the moment they left.
        if (house->isAI()) {
            for (const StructureBase* structure : getStructureList()) {
                if (structure->getOwner() != house) continue;
                auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
                if (zone == nullptr || !zone->isGarrisoned() || zone->getGarrisonHouse() != house
                    || zone->getGarrisonOriginalOwner() >= 0) continue;
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

    if (!force.zoneCaptureRaid && !force.zoneCaptureLevy) return;

    // The enemy zones a soldier could take now: empty, seen, enterable from a free side.
    std::vector<const ZoneStructure*> targets;
    std::vector<Coord> enemyTurrets;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner()->getTeamID() == team) continue;
        if (structure->getItemID() == Structure_GunTurret || structure->getItemID() == Structure_RocketTurret) {
            if (structure->isVisible(team)) enemyTurrets.push_back(structure->getLocation());
            continue;
        }
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        if (zone == nullptr || zone->isGarrisoned() || pending[zone->getObjectID()] > 0
            || !zone->isVisible(team) || zone->tooDamagedToHold() || !hasOpenSide(map, zone)) continue;
        targets.push_back(zone);
    }

    // Each target's nearest free soldier within RADIUS (lower ids on ties), nearest pairs first,
    // one soldier a zone. SOLDIERONLY keeps Troopers in the army (the levy is Soldiers).
    auto dispatch = [&](const std::vector<const ZoneStructure*>& zones, int radius, bool soldierOnly,
                        const char* why) {
        struct Pair { FixPoint distance; Uint32 zone; Uint32 unit; const ZoneStructure* z; const UnitBase* u; };
        std::vector<Pair> pairs;
        for (const ZoneStructure* zone : zones) {
            if (pending[zone->getObjectID()] > 0) continue;
            for (const UnitBase* soldier : soldiers) {
                if ((soldierOnly && soldier->getItemID() != Unit_Soldier) || !available(soldier)
                    || !zone->mayEnterGarrison(soldier)) continue;
                const Coord at = soldier->getLocation();
                const FixPoint distance = blockDistance(at, zone->getClosestPoint(at));
                if (distance <= radius)
                    pairs.push_back({distance, zone->getObjectID(), soldier->getObjectID(), zone, soldier});
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            if (a.zone != b.zone) return a.zone < b.zone;
            return a.unit < b.unit;
        });
        for (const Pair& pair : pairs) {
            if (orders >= kMaxOrdersPerPass) break;
            if (pending[pair.zone] > 0 || !available(pair.u)) continue;
            order(pair.u, pair.z, why);
        }
    };

    // 5. Raid: empty enemy zones with no enemy that can fight or capture near, and no turret.
    if (force.zoneCaptureRaid) {
        std::vector<const ZoneStructure*> unguarded;
        for (const ZoneStructure* zone : targets) {
            bool guarded = false;
            for (const UnitBase* enemy : enemies) {
                const Coord at = enemy->getLocation();
                if (blockDistance(at, zone->getClosestPoint(at)) <= kRaidGuardRadius) { guarded = true; break; }
            }
            for (const Coord& turret : enemyTurrets) {
                if (guarded) break;
                guarded = blockDistance(turret, zone->getClosestPoint(turret)) <= kRaidTurretRadius;
            }
            if (!guarded) unguarded.push_back(zone);
        }
        dispatch(unguarded, kRaidRadius, false, "raid");
    }

    // 6. Levy: keep kLevySoldiers Soldiers in training at the Barracks while enemy zones are there
    // to take, and walk every free Soldier into the nearest one.
    if (force.zoneCaptureLevy && !targets.empty()) {
        int levy = 0;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() == house && unit->getItemID() == Unit_Soldier && unit->getHealth() > 0) ++levy;
        }
        const int price = currentGame->objectData.data[Unit_Soldier][house->getHouseID()].price;
        for (const StructureBase* structure : getStructureList()) {
            if (levy >= kLevySoldiers) break;
            if (structure->getOwner() != house || structure->getItemID() != Structure_Barracks) continue;
            const auto* barracks = static_cast<const BuilderBase*>(structure);
            if (barracks->getProductionQueueSize() > 0 || barracks->isUpgrading()
                || !barracks->isAvailableToBuild(Unit_Soldier)
                || house->getCredits() < price + kLevyCashReserve) continue;
            doProduceItem(barracks, Unit_Soldier);
            ++levy;
            traceDecision("zone_capture_levy_train", AITelemetry::Record().set("barracks", barracks->getObjectID())
                .set("soldiers", levy).set("credits", house->getCredits()));
        }
        dispatch(targets, kLevyRadius, true, "levy");
    }
}
