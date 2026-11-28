/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// QuantBot Zone Garrisons (docs/zone-garrisons.md): the city's Garrisoning and Capturing rule
// grants zone permission and enables all six tactics below; config preferences never bypass OFF.
//  - ZoneGarrison: Soldiers and Troopers into own zones enemies close on, out when quiet, and into
//    empty enemy zones they pass;
//  - ZoneCaptureRaid: free soldiers take empty enemy zones nobody guards;
//  - ZoneCaptureLevy: the Barracks train cheap Soldiers that walk into the nearest enemy zones;
//  - ZoneGarrisonReinforce: Troopers reinforce garrisons enemies close on;
//  - ZoneFrontGarrison: our zones nearest the enemy (the front, however far) get a garrison first;
//  - ZoneCaptureChain: soldiers take the empty enemy zones nearest a zone we hold, block by block.
// A captured zone returns to its owner once its garrison is gone, so soldiers in a captured zone stay.
// A soldier on its way in belongs to this controller alone (Player's garrisonBound refuses every
// other controller's order); this pass ends an order whose zone is gone, held by someone else, or
// that made no progress for kNoProgressSeconds. Every order and its end is a ZONE_GARRISON line of the
// match log (house=<engine house>), which tests/ai/activation-events.json counts per play style.

#include "QuantBotInternal.h"
#include <players/HumanPlayer.h>
#include <structures/ZoneStructure.h>
#include <units/InfantryBase.h>
#include <structures/BuilderBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <dunecity/InfantrySquadPolicy.h>

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
/// A nearby free Trooper answers a threatened shelter before ordinary rally/idle duties.
constexpr int kRushResponderRadius = 25;
constexpr Uint32 kAttackedShelterHold = MILLI2CYCLES(30000);
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
/// ZoneFrontGarrison: our kFrontZones zones nearest a visible enemy building are the front, however
/// far that is; each gets kFrontGarrison soldiers from within kFrontResponderRadius.
constexpr int kFrontZones = 4;
constexpr int kFrontGarrison = 2;
constexpr int kFrontResponderRadius = 20;
/// ZoneCaptureChain: the kChainFrontier empty enemy zones nearest a zone we hold (own or captured),
/// however far, are the next blocks; soldiers within kChainResponderRadius take them. Each zone
/// taken is a zone we hold, so the chain moves on from it.
constexpr int kChainFrontier = 2;
constexpr int kChainResponderRadius = 40;
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

// The axis-aligned bound cannot reject anything within the exact block-distance radius.
// Most visible enemies are on the other side of the map, not near this particular zone.
bool withinZoneRadius(const ZoneStructure* zone, const Coord& at, int radius) {
    const Coord origin = zone->getLocation();
    const Coord size = zone->getStructureSize();
    if (at.x < origin.x - radius || at.y < origin.y - radius
        || at.x >= origin.x + size.x + radius || at.y >= origin.y + size.y + radius) return false;
    return blockDistance(at, zone->getClosestPoint(at)) <= radius;
}

} // namespace

bool QuantBot::spareForZoneGarrison(const UnitBase* unit) const {
    if (unit->getOwner() != getHouse() || !unit->isActive() || !unit->isRespondable()
        || unit->getHealth() * 2 < unit->getMaxHealth() || unit->getAttackMode() == CAPTURE
        || unit->isAttackMoving() || unit->getAttackPos().isValid()
        || unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Escort, UnitRole::ColonyGuard,
                           UnitRole::Defence, UnitRole::Wave}) != UnitRole::Free) return false;
    const auto* target = unit->getTarget();
    return target == nullptr || target->getOwner()->getTeamID() == getHouse()->getTeamID();
}

const UnitBase* QuantBot::rushZoneGarrison(const ZoneStructure* zone) {
    const auto& force = botConfig().force;
    if (builderOnly() || !ZoneStructure::garrisonsEnabled()
        || (!wantsGarrisonTactic(force.zoneGarrison) && !wantsGarrisonTactic(force.zoneGarrisonReinforce))
        || zone->getOwner() != getHouse() || zone->getHealth() <= 0 || zone->tooDamagedToHold()
        || !hasOpenSide(getMap(), zone)) return nullptr;
    for (const Uint32 id : zone->getGarrison()) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (unit && unit->getHealth() > 0 && unit->getItemID() == Unit_Trooper) return nullptr;
    }
    const UnitBase* best = nullptr;
    FixPoint bestDistance = FixPoint(kRushResponderRadius + 1);
    int incoming = 0;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || !unit->isActive() || unit->getHealth() <= 0
            || !garrisonSoldier(unit)) continue;
        if (unit->getAttackMode() == CAPTURE && unit->getTarget() == zone) {
            if (unit->getItemID() == Unit_Trooper) return nullptr;
            ++incoming;
            continue;
        }
        if (unit->getItemID() != Unit_Trooper || !unit->canCaptureStructures() || !spareForZoneGarrison(unit)) continue;
        const FixPoint distance = blockDistance(unit->getLocation(), zone->getClosestPoint(unit->getLocation()));
        if (distance <= kRushResponderRadius && distance < bestDistance) { best = unit; bestDistance = distance; }
    }
    if (!best) return nullptr;
    const Uint32 id = best->getObjectID();
    if (zone->getGarrisonSize() + incoming >= DuneCity::ZoneGarrison::kCapacity) {
        // Replace one Soldier, never empty a captured shelter or release a human/allied guest.
        if (incoming || zone->getGarrisonSize() != DuneCity::ZoneGarrison::kCapacity
            || std::any_of(getHouse()->getPlayerList().begin(), getHouse()->getPlayerList().end(),
                [](const auto& player) { return dynamic_cast<const HumanPlayer*>(player.get()) != nullptr; })
            || std::any_of(zone->getGarrison().begin(), zone->getGarrison().end(), [&](Uint32 occupant) {
                const auto* unit = dynamic_cast<const UnitBase*>(getObject(occupant));
                return !unit || unit->getOwner() != getHouse();
            })) return nullptr;
        auto* shelter = const_cast<ZoneStructure*>(zone);
        const int out = shelter->evacuateGarrison(false, DuneCity::ZoneGarrison::kCapacity - 1);
        SDL_Log("ZONE_GARRISON: event=rush_replace house=%d zone=%u deployed=%d retained=%d ai_only=1 cycle=%u",
            getHouse()->getHouseID(), zone->getObjectID(), out, zone->getGarrisonSize(), getGameCycleCount());
        best = dynamic_cast<const UnitBase*>(getObject(id));
        if (!best || !spareForZoneGarrison(best)) return nullptr;
    }
    if (!zone->mayEnterGarrison(best)) return nullptr;
    SDL_Log("ZONE_GARRISON: event=order house=%d why=defend unit=%u item=%d zone=%u zone_house=%d"
            " occupants=%d rush=1 responder_distance10=%d cycle=%u",
        getHouse()->getHouseID(), id, best->getItemID(), zone->getObjectID(), zone->getOwner()->getHouseID(),
        zone->getGarrisonSize(), tenths(bestDistance), getGameCycleCount());
    doCaptureStructure(static_cast<const InfantryBase*>(best), zone);
    if (best->isActive() && best->getAttackMode() != CAPTURE) return nullptr;
    if (best->isActive()) zoneGarrisonProgress_[id] = {getGameCycleCount(), tenths(bestDistance)};
    return best;
}

void QuantBot::updateZoneGarrisons() {
    const auto& force = botConfig().force;
    if ((!wantsGarrisonTactic(force.zoneGarrison) && !wantsGarrisonTactic(force.zoneCaptureRaid)
         && !wantsGarrisonTactic(force.zoneCaptureLevy) && !wantsGarrisonTactic(force.zoneGarrisonReinforce)
         && !wantsGarrisonTactic(force.zoneFrontGarrison) && !wantsGarrisonTactic(force.zoneCaptureChain))
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
        } else if (unit->getOwner()->getTeamID() != team
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
            && spareForZoneGarrison(soldier);
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
    if (wantsGarrisonTactic(force.zoneGarrison) || wantsGarrisonTactic(force.zoneGarrisonReinforce)) {
        // Count each enemy once per mapped own zone, rather than searching 225 tiles around every
        // visible enemy and deduplicating each zone's four tiles. Integer sums and ID ranking agree.
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr) continue;
            const Coord origin = zone->getLocation(), size = zone->getStructureSize();
            int points = 0;
            for (const UnitBase* enemy : enemies) {
                const Coord at = enemy->getLocation();
                if (!withinZoneRadius(zone, at, kThreatRadius)) continue;
                // Retain the old tile-query visibility even for a partially registered footprint.
                // An unplaced zone contributes nothing; a normal zone matches on the first tile.
                bool mapped = false;
                for (int y = std::max(origin.y, at.y - kThreatRadius);
                     y < std::min(origin.y + size.y, at.y + kThreatRadius + 1) && !mapped; ++y) {
                    for (int x = std::max(origin.x, at.x - kThreatRadius);
                         x < std::min(origin.x + size.x, at.x + kThreatRadius + 1); ++x) {
                        if (map.tileExists(x, y) && map.getTile(x, y)->getGroundObject() == zone) {
                            mapped = true;
                            break;
                        }
                    }
                }
                if (mapped) points += enemy->isInfantry() && enemy->canCaptureStructures() ? 3 : 1;
            }
            if (points == 0) continue;
            threat[zone->getObjectID()] = points;
            zones[zone->getObjectID()] = zone;
        }
    }
    for (auto it = zoneGarrisonAttacks_.begin(); it != zoneGarrisonAttacks_.end();) {
        const auto* zone = dynamic_cast<const ZoneStructure*>(getObject(it->first));
        if (!zone || zone->getOwner() != house || zone->getHealth() <= 0
            || now - it->second >= kAttackedShelterHold) it = zoneGarrisonAttacks_.erase(it);
        else {
            threat[it->first] = std::max(threat[it->first], 1);
            zones[it->first] = const_cast<ZoneStructure*>(zone);
            ++it;
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

    // Our zones at the front: the kFrontZones nearest a visible enemy building, nearest first.
    std::vector<std::pair<int, ZoneStructure*>> front;
    if (wantsGarrisonTactic(force.zoneFrontGarrison) && !enemyBuildings.empty()) {
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr || zone->getGarrisonOriginalOwner() >= 0) continue;
            int closest = INT_MAX;
            for (const Coord& at : enemyBuildings) closest = std::min(closest, tenths(blockDistance(at, zone->getClosestPoint(at))));
            front.emplace_back(closest, zone);
        }
        std::sort(front.begin(), front.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second->getObjectID() < b.second->getObjectID();
        });
        if (front.size() > size_t(kFrontZones)) front.resize(kFrontZones);
    }
    auto atTheFront = [&](const ZoneStructure* zone) {
        return std::any_of(front.begin(), front.end(), [&](const auto& item) { return item.second == zone; });
    };

    // One Trooper in every threatened shelter before anyone receives a second responder.
    if (wantsGarrisonTactic(force.zoneGarrison) || wantsGarrisonTactic(force.zoneGarrisonReinforce)) {
        for (const auto& [negPoints, id] : ranked) {
            if (orders >= kMaxOrdersPerPass) break;
            if (const auto* responder = rushZoneGarrison(zones[id])) {
                used.push_back(responder->getObjectID());
                if (responder->isActive()) ++pending[id];
                ++orders;
            }
        }
    }

    if (wantsGarrisonTactic(force.zoneGarrison)) {
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
    if (wantsGarrisonTactic(force.zoneGarrisonReinforce)) {
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
    if (wantsGarrisonTactic(force.zoneGarrison)
        && std::none_of(house->getPlayerList().begin(), house->getPlayerList().end(), [](const auto& player) {
            return dynamic_cast<const HumanPlayer*>(player.get()) != nullptr;
        })) {
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(structure));
            if (zone == nullptr || !zone->isGarrisoned() || zone->getGarrisonHouse() != house
                || zone->getGarrisonOriginalOwner() >= 0 || atTheFront(zone) || zone->tooDamagedToHold()
                || zoneGarrisonAttacks_.count(zone->getObjectID())) continue;
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
    if (wantsGarrisonTactic(force.zoneGarrison)) {
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

    // Chain: the next empty enemy zone across the street from a zone we hold (own or captured),
    // block by block: each zone taken becomes the next starting point.
    if (wantsGarrisonTactic(force.zoneCaptureChain) && !targets.empty()) {
        std::vector<const ZoneStructure*> held;
        for (const StructureBase* structure : getStructureList()) {
            if (structure->getOwner() != house) continue;
            if (const auto* zone = dynamic_cast<const ZoneStructure*>(structure)) held.push_back(zone);
        }
        std::vector<std::pair<int, const ZoneStructure*>> frontier;
        for (const ZoneStructure* zone : targets) {
            if (pending[zone->getObjectID()] > 0) continue;
            const Coord at = zone->getLocation();
            int closest = INT_MAX;
            for (const ZoneStructure* own : held) closest = std::min(closest, tenths(blockDistance(at, own->getClosestPoint(at))));
            if (closest != INT_MAX) frontier.emplace_back(closest, zone);
        }
        std::sort(frontier.begin(), frontier.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second->getObjectID() < b.second->getObjectID();
        });
        std::vector<const ZoneStructure*> next;
        for (const auto& item : frontier) {
            if (next.size() >= size_t(kChainFrontier)) break;
            next.push_back(item.second);
        }
        dispatch(next, kChainResponderRadius, false, "chain");
    }

    // Raid: empty enemy zones with no enemy that can fight or capture near, and no turret.
    if (wantsGarrisonTactic(force.zoneCaptureRaid)) {
        std::vector<const ZoneStructure*> unguarded;
        for (const ZoneStructure* zone : targets) {
            bool guarded = false;
            for (const UnitBase* enemy : enemies) {
                const Coord at = enemy->getLocation();
                if (withinZoneRadius(zone, at, kRaidGuardRadius)) { guarded = true; break; }
            }
            for (const Coord& turret : enemyTurrets) {
                if (guarded) break;
                guarded = withinZoneRadius(zone, turret, kRaidTurretRadius);
            }
            if (!guarded) unguarded.push_back(zone);
        }
        dispatch(unguarded, kRaidRadius, false, "raid");
    }

    // Levy: keep kLevySoldiers Soldiers in training at the Barracks while enemy zones are there
    // to take, and walk every free Soldier into the nearest one.
    if (wantsGarrisonTactic(force.zoneCaptureLevy) && !targets.empty()) {
        int levy = 0;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() == house && unit->getItemID() == Unit_Soldier && unit->getHealth() > 0) ++levy;
        }
        for (const StructureBase* structure : getStructureList()) {
            if (levy >= kLevySoldiers) break;
            if (structure->getOwner() != house || structure->getItemID() != Structure_Barracks) continue;
            const auto* barracks = static_cast<const BuilderBase*>(structure);
            if (barracks->getProductionQueueSize() > 0 || barracks->isUpgrading()
                || !barracks->isAvailableToBuild(Unit_Soldier)) continue;
            // The same strict policy includes the effective city option, even when the Config bit is off.
            const Uint32 item = infantryOrderFor(barracks, Unit_Soldier);
            const int price = purchasePrice(barracks, item);
            if (house->getCredits() < price + kLevyCashReserve) continue;
            doProduceItem(barracks, item);
            const int men = DuneCity::InfantrySquad::orderSize(item);
            levy += men;
            SDL_Log("ZONE_GARRISON: event=levy_train house=%d barracks=%u soldiers=%d price=%d item=%u men=%d credits=%d cycle=%u",
                    houseID, barracks->getObjectID(), levy, price, item, men, house->getCredits(), now);
            traceDecision("zone_capture_levy_train", AITelemetry::Record().set("barracks", barracks->getObjectID())
                .set("soldiers", levy).set("credits", house->getCredits()));
        }
        dispatch(targets, kLevyRadius, true, "levy");
    }
}
