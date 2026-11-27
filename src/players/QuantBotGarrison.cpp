/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// QuantBot Zone Garrisons (docs/zone-garrisons.md), each behaviour behind its own switch in
// QuantBot Config [Force Concentration], all only with the Zone Garrisons match rule on in a city game:
//  - ZoneGarrison: Soldiers and Troopers into own zones enemies close on, out when quiet, and into
//    empty enemy zones they pass;
//  - ZoneCaptureRaid: free soldiers take empty enemy zones nobody guards;
//  - ZoneCaptureLevy: the Barracks train cheap Soldiers that walk into the nearest enemy zones;
//  - ZoneGarrisonReinforce: Troopers reinforce garrisons enemies close on;
//  - ZoneFrontGarrison: our zones nearest the enemy get a garrison first;
//  - ZoneCaptureChain: from every zone we hold, soldiers take the next enemy zone across the street.
// A captured zone returns to its owner once its garrison is gone, so soldiers in a captured zone stay.
// A soldier on its way in belongs to this controller alone (Player's garrisonBound refuses every
// other controller's order); this pass ends an order whose zone is gone, held by someone else, or
// that made no progress for kNoProgressSeconds. Every order and its end is a ZONE_GARRISON line of the
// match log (house=<engine house>), which tests/ai/activation-events.json counts per play style.

#include "QuantBotInternal.h"
#include <structures/ZoneStructure.h>
#include <units/InfantryBase.h>
#include <structures/BuilderBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>

#include <algorithm>
#include <climits>
#include <map>
#include <vector>

namespace {

/// A visible enemy within this many tiles of a zone of ours threatens it.
constexpr int kThreatRadius = 7;
/// A garrison comes out when no visible enemy is within this many tiles (hysteresis over the threat).
constexpr int kReleaseRadius = 11;
/// Own soldiers this close to a threatened zone answer it.
constexpr int kResponderRadius = 12;
/// An empty enemy zone this close to one of our soldiers is taken.
constexpr int kOffenceRadius = 5;
/// Capture orders a house gives in one pass, all behaviours together.
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
/// ZoneGarrisonReinforce: Troopers this close to a threatened garrison of ours reinforce it.
constexpr int kReinforceRadius = 16;
/// ZoneFrontGarrison: a zone of ours with a visible enemy building this close is at the front; it
/// gets kFrontGarrison soldiers from within kFrontResponderRadius.
constexpr int kFrontRadius = 12;
constexpr int kFrontGarrison = 2;
constexpr int kFrontResponderRadius = 20;
/// ZoneCaptureChain: an empty enemy zone this close to a zone we hold is the next block; soldiers
/// within kChainResponderRadius take it.
constexpr int kChainReach = 5;
constexpr int kChainResponderRadius = 16;
/// A soldier on its way in that has not got closer for this long gives up (Stefan: exempt "until it
/// is inside ... or it has made no progress for a set time").
constexpr int kNoProgressSeconds = 45;

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

int tenths(FixPoint distance) { return (distance * 10).lround(); }

} // namespace

void QuantBot::updateZoneGarrisons() {
    const auto& force = botConfig().force;
    if ((!force.zoneGarrison && !force.zoneCaptureRaid && !force.zoneCaptureLevy && !force.zoneGarrisonReinforce
         && !force.zoneFrontGarrison && !force.zoneCaptureChain)
        || !ZoneStructure::garrisonsEnabled() || builderOnly()
        || currentGame == nullptr || !currentGame->isCitySimEnabled()) return;
    AITelemetry::PerformanceScope perfScope("ai.zoneGarrisons", getGameCycleCount(), getHouse()->getHouseID());
    const House* house = getHouse();
    const int houseID = house->getHouseID();
    const int team = house->getTeamID();
    const Map& map = getMap();
    const Uint32 now = getGameCycleCount();

    auto logLine = [&](const char* event, const UnitBase* soldier, const ZoneStructure* zone, const char* why) {
        SDL_Log("ZONE_GARRISON: event=%s house=%d why=%s unit=%u item=%d zone=%u zone_house=%d occupants=%d cycle=%u",
                event, houseID, why, soldier->getObjectID(), soldier->getItemID(),
                zone != nullptr ? zone->getObjectID() : 0, zone != nullptr ? zone->getOwner()->getHouseID() : -1,
                zone != nullptr ? zone->getGarrisonSize() : 0, now);
    };

    // Our soldiers free to act and the enemies we can see, in unit-list order. A soldier on its way
    // into a zone keeps its order unless the zone is gone or held by another house, or it has not got
    // closer for kNoProgressSeconds; then it goes back to the army.
    std::vector<const UnitBase*> soldiers;
    std::vector<const UnitBase*> enemies;
    std::map<Uint32, int> pending;   // zone -> own soldiers already ordered into it
    std::vector<Uint32> onTheirWay;
    for (const UnitBase* unit : getUnitList()) {
        if (!unit->isActive() || unit->getHealth() <= 0) continue;
        if (unit->getOwner() == house) {
            if (!garrisonSoldier(unit) || !unit->isRespondable()) continue;
            if (unit->getAttackMode() == CAPTURE) {
                const auto* zone = dynamic_cast<const ZoneStructure*>(unit->getTarget());
                const House* holder = zone != nullptr ? zone->getGarrisonHouse() : nullptr;
                const char* end = zone == nullptr ? "no_zone"
                    : holder != nullptr && holder->getTeamID() != team ? "held_by_other" : nullptr;
                if (end == nullptr) {
                    const int distance = tenths(blockDistance(unit->getLocation(), zone->getClosestPoint(unit->getLocation())));
                    auto [it, fresh] = zoneGarrisonProgress_.try_emplace(unit->getObjectID(), now, distance);
                    if (fresh || distance < it->second.second) it->second = {now, distance};
                    else if (now - it->second.first >= Uint32(MILLI2CYCLES(kNoProgressSeconds * 1000))) end = "no_progress";
                }
                if (end == nullptr) {
                    ++pending[zone->getObjectID()];
                    onTheirWay.push_back(unit->getObjectID());
                    continue;
                }
                logLine("order_ended", unit, zone, end);
                zoneGarrisonProgress_.erase(unit->getObjectID());
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
    // Clocks of soldiers no longer on their way (inside, dead, or re-ordered) are dropped.
    for (auto it = zoneGarrisonProgress_.begin(); it != zoneGarrisonProgress_.end();) {
        if (std::find(onTheirWay.begin(), onTheirWay.end(), it->first) == onTheirWay.end()) it = zoneGarrisonProgress_.erase(it);
        else ++it;
    }

    int orders = 0;
    std::vector<Uint32> used;
    auto order = [&](const UnitBase* soldier, const ZoneStructure* zone, const char* why) {
        logLine("order", soldier, zone, why);
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
    // The nearest available soldier (Troopers only with TROOPERS) within RADIUS that may enter ZONE.
    auto nearest = [&](const ZoneStructure* zone, int radius, bool troopers) -> const UnitBase* {
        const UnitBase* best = nullptr;
        FixPoint bestDistance = FixPt_MAX;
        for (const UnitBase* soldier : soldiers) {
            if ((troopers && soldier->getItemID() != Unit_Trooper) || !available(soldier)
                || !zone->mayEnterGarrison(soldier)) continue;
            const Coord at = soldier->getLocation();
            const FixPoint distance = blockDistance(at, zone->getClosestPoint(at));
            if (distance > radius) continue;
            if (distance < bestDistance) { best = soldier; bestDistance = distance; }
        }
        return best;
    };

    // Threat on each zone we hold (own or captured): one point an enemy, three a soldier that can capture.
    std::map<Uint32, int> threat;
    std::map<Uint32, ZoneStructure*> zones;
    if (force.zoneGarrison || force.zoneGarrisonReinforce) {
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
    }
    std::vector<std::pair<int, Uint32>> ranked;
    for (const auto& [id, points] : threat) ranked.emplace_back(-points, id);
    std::sort(ranked.begin(), ranked.end());

    // Enemy buildings we can see: the front, the turrets a raid keeps clear of, and the empty enemy
    // zones a soldier could take now.
    std::vector<Coord> enemyBuildings;
    std::vector<Coord> enemyTurrets;
    std::vector<const ZoneStructure*> targets;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner()->getTeamID() == team || !structure->isVisible(team)) continue;
        enemyBuildings.push_back(structure->getLocation());
        if (structure->getItemID() == Structure_GunTurret || structure->getItemID() == Structure_RocketTurret) {
            enemyTurrets.push_back(structure->getLocation());
            continue;
        }
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        if (zone == nullptr || zone->isGarrisoned() || zone->tooDamagedToHold() || !hasOpenSide(map, zone)) continue;
        targets.push_back(zone);
    }

    // Our zones at the front: a visible enemy building within kFrontRadius, nearest first.
    std::vector<std::pair<int, ZoneStructure*>> front;
    if (force.zoneFrontGarrison) {
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr || zone->getGarrisonOriginalOwner() >= 0) continue;
            int closest = INT_MAX;
            for (const Coord& at : enemyBuildings) closest = std::min(closest, tenths(blockDistance(at, zone->getClosestPoint(at))));
            if (closest <= kFrontRadius * 10) front.emplace_back(closest, zone);
        }
        std::sort(front.begin(), front.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second->getObjectID() < b.second->getObjectID();
        });
    }
    auto atTheFront = [&](const ZoneStructure* zone) {
        return std::any_of(front.begin(), front.end(), [&](const auto& item) { return item.second == zone; });
    };

    if (force.zoneGarrison) {
        // Defence: the most threatened zones first, the nearest free soldiers into them.
        for (const auto& [negPoints, id] : ranked) {
            if (orders >= kMaxOrdersPerPass) break;
            ZoneStructure* zone = zones[id];
            if (!hasOpenSide(map, zone)) continue;
            const int want = std::min(DuneCity::ZoneGarrison::kCapacity, (-negPoints + 1) / 2);
            int have = zone->getGarrisonSize() + pending[id];
            while (have < want && orders < kMaxOrdersPerPass) {
                const UnitBase* best = nearest(zone, kResponderRadius, false);
                if (best == nullptr) break;
                order(best, zone, "defend");
                ++have;
            }
        }
    }

    // Reinforce: Troopers fill garrisons of ours enemies close on, up to the zone's four.
    if (force.zoneGarrisonReinforce) {
        for (const auto& [negPoints, id] : ranked) {
            if (orders >= kMaxOrdersPerPass) break;
            ZoneStructure* zone = zones[id];
            if (!zone->isGarrisoned() || zone->getGarrisonHouse() != house || !hasOpenSide(map, zone)) continue;
            int have = zone->getGarrisonSize() + pending[id];
            while (have < DuneCity::ZoneGarrison::kCapacity && orders < kMaxOrdersPerPass) {
                const UnitBase* best = nearest(zone, kReinforceRadius, true);
                if (best == nullptr) break;
                order(best, zone, "reinforce");
                ++have;
            }
        }
    }

    // Front: our zones nearest the enemy get kFrontGarrison soldiers before anyone attacks them.
    for (const auto& [distance, zone] : front) {
        if (orders >= kMaxOrdersPerPass) break;
        if (!hasOpenSide(map, zone)) continue;
        int have = zone->getGarrisonSize() + pending[zone->getObjectID()];
        while (have < kFrontGarrison && orders < kMaxOrdersPerPass) {
            const UnitBase* best = nearest(zone, kFrontResponderRadius, false);
            if (best == nullptr) break;
            order(best, zone, "front");
            ++have;
        }
    }

    // Release: a garrison of ours in an own zone with no visible enemy near comes out to rejoin the
    // army. A house a human shares keeps the garrisons its human put there; a captured zone keeps its
    // garrison (it would go back the moment they left); a front zone keeps its garrison.
    if (force.zoneGarrison && house->isAI()) {
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr || !zone->isGarrisoned() || zone->getGarrisonHouse() != house
                || zone->getGarrisonOriginalOwner() >= 0 || atTheFront(zone)) continue;
            bool quiet = true;
            for (const UnitBase* enemy : enemies) {
                const Coord at = enemy->getLocation();
                if (blockDistance(at, zone->getClosestPoint(at)) <= kReleaseRadius) { quiet = false; break; }
            }
            if (!quiet) continue;
            const int out = zone->evacuateGarrison();
            SDL_Log("ZONE_GARRISON: event=release house=%d zone=%u deployed=%d cycle=%u", houseID, zone->getObjectID(), out, now);
            traceDecision("zone_garrison_release", AITelemetry::Record().set("zone", zone->getObjectID())
                .set("deployed", out));
        }
    }

    // Offence on the way: a soldier passing an empty enemy zone takes it (one soldier a zone a pass).
    if (force.zoneGarrison) {
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

    // Each target's nearest free soldier within RADIUS (lower ids on ties), nearest pairs first,
    // one soldier a zone. SOLDIERONLY keeps Troopers in the army (the levy is Soldiers).
    auto dispatch = [&](const std::vector<const ZoneStructure*>& candidates, int radius, bool soldierOnly,
                        const char* why) {
        struct Pair { FixPoint distance; Uint32 zone; Uint32 unit; const ZoneStructure* z; const UnitBase* u; };
        std::vector<Pair> pairs;
        for (const ZoneStructure* zone : candidates) {
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

    // Chain: the next empty enemy zone across the street from a zone we hold, block by block.
    if (force.zoneCaptureChain && !targets.empty()) {
        std::vector<const ZoneStructure*> held;
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
            if (zone != nullptr && zone->isGarrisoned() && zone->getGarrisonHouse() == house) held.push_back(zone);
        }
        std::vector<const ZoneStructure*> next;
        for (const ZoneStructure* zone : targets) {
            const Coord at = zone->getLocation();
            if (std::any_of(held.begin(), held.end(), [&](const ZoneStructure* own) {
                    return blockDistance(at, own->getClosestPoint(at)) <= kChainReach; })) next.push_back(zone);
        }
        dispatch(next, kChainResponderRadius, false, "chain");
    }

    // Raid: empty enemy zones with no enemy that can fight or capture near, and no turret.
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

    // Levy: keep kLevySoldiers Soldiers in training at the Barracks while enemy zones are there
    // to take, and walk every free Soldier into the nearest one.
    if (force.zoneCaptureLevy && !targets.empty()) {
        int levy = 0;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() == house && unit->getItemID() == Unit_Soldier && unit->getHealth() > 0) ++levy;
        }
        const int price = currentGame->objectData.data[Unit_Soldier][houseID].price;
        for (const StructureBase* structure : getStructureList()) {
            if (levy >= kLevySoldiers) break;
            if (structure->getOwner() != house || structure->getItemID() != Structure_Barracks) continue;
            const auto* barracks = static_cast<const BuilderBase*>(structure);
            if (barracks->getProductionQueueSize() > 0 || barracks->isUpgrading()
                || !barracks->isAvailableToBuild(Unit_Soldier)
                || house->getCredits() < price + kLevyCashReserve) continue;
            doProduceItem(barracks, Unit_Soldier);
            ++levy;
            SDL_Log("ZONE_GARRISON: event=levy_train house=%d barracks=%u soldiers=%d price=%d credits=%d cycle=%u",
                    houseID, barracks->getObjectID(), levy, price, house->getCredits(), now);
            traceDecision("zone_capture_levy_train", AITelemetry::Record().set("barracks", barracks->getObjectID())
                .set("soldiers", levy).set("credits", house->getCredits()));
        }
        dispatch(targets, kLevyRadius, true, "levy");
    }
}
