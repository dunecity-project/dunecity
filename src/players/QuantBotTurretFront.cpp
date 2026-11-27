/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// Rocket Turrets at the front: four default-off QuantBot switches (TurretFrontPolicy.h).
// (a) FrontTurrets, (b) AdvancingTurrets, (c) TurretCreep, (d) RearTurretRetire.
//
// One scan per house every TurretFrontPolicy::kScanPeriodCycles builds three tile grids from the
// structure list: the Chebyshev distance to the nearest explored enemy building, and box-sum
// tables of own Rocket Turrets, enemy turrets, enemy building tiles and visible enemy units. The
// switches read those grids, so a scan is a few passes over the map, never a search per pair.
// The chosen site waits in turretFrontPlan for a free yard, which re-checks it before ordering.
// Nothing here is saved: a loaded game scans again on its first pass.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/TurretFrontPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <players/FrontBatteryPolicy.h>
#include <players/QuantBotAirDefencePolicy.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CitySimulation.h>
#include <structures/TurretBase.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

namespace {

int envSwitch(const char* name) {
    const char* text = std::getenv(name);
    return text != nullptr && (text[0] == '0' || text[0] == '1') ? text[0] - '0' : -1;
}

bool frontRocketTurret(Uint32 item) { return item == Structure_RocketTurret; }
bool enemyTurret(Uint32 item) { return item == Structure_RocketTurret || item == Structure_GunTurret; }
/// Buildings that mark the base's extent: not zones, walls or slabs (zones fill the city and
/// would make every lot an anchor; walls and slabs are not buildings).
bool anchorItem(Uint32 item) {
    return item != Structure_Wall && item != Structure_Slab1 && item != Structure_Slab4
        && !DuneCity::isCityZoneStructure(item);
}

/// Experiment-only integer overrides of the moving line's shape (DUNECITY_TEST_BELT_GOAL,
/// DUNECITY_TEST_BELT_STANDOFF, DUNECITY_TEST_BELT_BEHIND_ARMY), read once per process; never a
/// match rule. Without them the TurretFrontPolicy constants apply.
int envInt(const char* name, int fallback, int low, int high) {
    const char* text = std::getenv(name);
    if (text == nullptr || *text == '\0') return fallback;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    return end != nullptr && *end == '\0' ? int(std::clamp<long>(value, low, high)) : fallback;
}
int beltGoal() {
    static const int value = envInt("DUNECITY_TEST_BELT_GOAL", TurretFrontPolicy::kBeltGoal, 1, 30);
    return value;
}
int beltStandoff() {
    static const int value = envInt("DUNECITY_TEST_BELT_STANDOFF", TurretFrontPolicy::kBeltEnemyStandoff, 1, 60);
    return value;
}
int beltBehindArmy() {
    static const int value = envInt("DUNECITY_TEST_BELT_BEHIND_ARMY", TurretFrontPolicy::kBehindArmyTiles, 0, 30);
    return value;
}

} // namespace

// Experiment-only overrides for match arms in one build (0 or 1), read once per process and never
// a match rule. Without them each house follows its own QuantBot Config (play styles).
bool QuantBot::frontTurretsEnabled() const {
    static const int override = envSwitch("DUNECITY_TEST_FRONT_TURRETS");
    return override >= 0 ? override == 1 : botConfig().force.frontTurrets;
}
bool QuantBot::advancingTurretsEnabled() const {
    static const int override = envSwitch("DUNECITY_TEST_ADVANCING_TURRETS");
    return override >= 0 ? override == 1 : botConfig().force.advancingTurrets;
}
bool QuantBot::turretCreepEnabled() const {
    static const int override = envSwitch("DUNECITY_TEST_TURRET_CREEP");
    return override >= 0 ? override == 1 : botConfig().force.turretCreep;
}
bool QuantBot::rearTurretRetireEnabled() const {
    static const int override = envSwitch("DUNECITY_TEST_REAR_TURRET_RETIRE");
    return override >= 0 ? override == 1 : botConfig().force.rearTurretRetire;
}
const char* QuantBot::turretFrontRule(TurretFrontMode mode) {
    return mode == TurretFrontMode::Reinforce ? "front_reinforce"
        : mode == TurretFrontMode::Creep ? "turret_creep"
        : mode == TurretFrontMode::Drill ? "turret_drill" : "turret_advance";
}

bool QuantBot::turretDrillEnabled() const {
    static const int override = envSwitch("DUNECITY_TEST_TURRET_DRILL");
    return override >= 0 ? override == 1 : botConfig().force.turretDrill;
}

void QuantBot::logTurretLedger(Uint32 now) {
    // Every city house, whatever its switches: what its Rocket Turrets cost in upkeep, for the
    // match tables (tests/ai/turret-front-report.py). One line per house per two game minutes.
    const auto* sim = currentGame->getCitySimulation();
    if (sim == nullptr || !sim->isInitialized()) return;
    const Uint32 bucket = now / TurretFrontPolicy::kRetirePeriodCycles;
    if (bucket == turretLedgerBucket) return;
    turretLedgerBucket = bucket;
    const int houseID = getHouse()->getHouseID();
    const int funding = sim->getPoliceFundingPercent(houseID);
    const int rockets = getHouse()->getNumItems(Structure_RocketTurret);
    const long long upkeep = (long long)rockets
        * TurretFrontPolicy::upkeepMilliPerMinute(funding, DuneCity::cyclesPerCityYear());
    SDL_Log("TURRET_LEDGER: house=%d rocket_turrets=%d gun_turrets=%d police_stations=%d funding=%d "
            "turret_upkeep_milli_per_min=%lld credits=%d population=%d cycle=%u",
            houseID, rockets, getHouse()->getNumItems(Structure_GunTurret),
            getHouse()->getNumItems(Structure_PoliceStation), funding, upkeep,
            int(getHouse()->getCredits()), sim->getHouseState(houseID).getTotalPop(), now);
}

void QuantBot::planTurretFront(BuildPass& pass) {
    namespace TF = TurretFrontPolicy;
    turretFrontPlan.active = false;
    turretFrontPlan.ordered = false;
    const Uint32 now = getGameCycleCount();
    if (pass.citySimEnabled) logTurretLedger(now);
    trackTurretFrontOrders(now);
    // The last pass had a site waiting that no yard turn even looked at.
    if (turretFrontPlan.active && !turretFrontPlan.ordered && !turretFrontYardSeen)
        ++turretFrontBlocked[BlockNotReached];
    turretFrontYardSeen = false;
    const bool reinforce = frontTurretsEnabled(), advance = advancingTurretsEnabled();
    const bool creep = turretCreepEnabled(), retire = rearTurretRetireEnabled(), drill = turretDrillEnabled();
    if (!reinforce && !advance && !creep && !retire && !drill) return;
    if (gameMode != GameMode::Custom || difficulty < Difficulty::Hard) return;
    const int houseID = getHouse()->getHouseID();
    const auto& turret = currentGame->objectData.data[Structure_RocketTurret][houseID];
    if (!turret.enabled || turret.techLevel > getHouse()->getTechLevel()) return;

    const bool retireDue = retire && now / TF::kRetirePeriodCycles != turretRetireBucket;
    // Retirement alone needs only its own two-minute review, not the eight-second scan.
    // A drilling house scans again soon after each order, so the mass grows as fast as the yards
    // and the budget allow; otherwise every kScanPeriodCycles.
    const Uint32 period = turretDrilling ? TF::kDrillScanPeriodCycles : TF::kScanPeriodCycles;
    const bool scanDue = (reinforce || advance || creep || drill)
        && (turretFrontScanCycle == std::numeric_limits<Uint32>::max()
            || now < turretFrontScanCycle || now - turretFrontScanCycle >= period);
    if (scanDue || retireDue) {
        turretFrontScanCycle = now;
        scanTurretFront(pass, reinforce, advance, creep, drill, retireDue);
        logTurretFrontState(now);
    }
    if (turretFrontPlan.site.isValid()) {
        turretFrontPlan.active = true;
        turretFrontPlan.reserve = QuantBotAirDefencePolicy::threatReserve(pass.economyReserve);
    }
}

void QuantBot::scanTurretFront(BuildPass& pass, bool reinforce, bool advance, bool creep, bool drill, bool retireDue) {
    namespace TF = TurretFrontPolicy;
    AITelemetry::PerformanceScope perfScope("ai.scanTurretFront", getGameCycleCount(), getHouse()->getHouseID());
    turretFrontPlan = TurretFrontPlan();
    turretDrilling = false;
    const Map& map = getMap();
    const int w = map.getSizeX(), h = map.getSizeY();
    if (w <= 0 || h <= 0) return;
    const int houseID = getHouse()->getHouseID();
    const int teamID = getHouse()->getTeamID();
    const int range = std::max(1, currentGame->objectData.data[Structure_RocketTurret][houseID].weaponrange);
    const size_t n = size_t(w) * size_t(h);
    auto index = [w](int x, int y) { return size_t(y) * size_t(w) + size_t(x); };

    std::vector<uint8_t> enemySeed(n, 0);
    std::vector<int32_t> ownTurretW(n, 0), ownBuildingW(n, 0), enemyTurretW(n, 0), enemyTileW(n, 0), contactW(n, 0);
    struct OwnTurret { Uint32 id; Coord at; };
    std::vector<OwnTurret> ownTurrets;
    std::vector<Coord> anchors, keepers;
    bool enemyKnown = false;
    for (const StructureBase* structure : getStructureList()) {
        const House* owner = structure->getOwner();
        if (owner == nullptr || structure->getHealth() <= 0) continue;
        const Uint32 item = structure->getItemID();
        if (item == Structure_Slab1 || item == Structure_Slab4) continue;
        const Coord at = structure->getLocation();
        const Coord size = structure->getStructureSize();
        if (!map.tileExists(at)) continue;
        if (owner == getHouse()) {
            for (int y = at.y; y < at.y + size.y && y < h; ++y)
                for (int x = at.x; x < at.x + size.x && x < w; ++x) ownBuildingW[index(x, y)] = 1;
            if (frontRocketTurret(item)) {
                ownTurretW[index(at.x, at.y)] += 1;
                ownTurrets.push_back({structure->getObjectID(), at});
            } else if (item == Structure_Airport || item == Structure_Palace) {
                for (int y = at.y; y < at.y + size.y && y < h; ++y)
                    for (int x = at.x; x < at.x + size.x && x < w; ++x) keepers.push_back(Coord(x, y));
            }
            if (anchorItem(item)) anchors.push_back(at);
        } else if (owner->getTeamID() != teamID && !owner->isNatives()) {
            if (!map.getTile(at)->isExploredByTeam(teamID)) continue;
            if (item == Structure_Wall) continue;
            enemyKnown = true;
            for (int y = at.y; y < at.y + size.y && y < h; ++y)
                for (int x = at.x; x < at.x + size.x && x < w; ++x) {
                    enemySeed[index(x, y)] = 1;
                    enemyTileW[index(x, y)] = 1;
                }
            if (enemyTurret(item)) enemyTurretW[index(at.x, at.y)] += 1;
        }
    }
    for (const auto& entry : reservedStructures)
        if (entry.second.item == Structure_RocketTurret && map.tileExists(entry.second.location))
            ownTurretW[index(entry.second.location.x, entry.second.location.y)] += 1;
    for (const auto& contact : visibleGroundTurretThreats)
        if (map.tileExists(contact.at)) contactW[index(contact.at.x, contact.at.y)] += contact.value;

    const std::vector<uint16_t> enemyDist = TF::chebyshevDistance(enemySeed, w, h, TF::kDistanceCap);
    const TF::BoxSum ownTurretSum(ownTurretW, w, h), ownBuildingSum(ownBuildingW, w, h);
    const TF::BoxSum enemyTurretSum(enemyTurretW, w, h), enemyTileSum(enemyTileW, w, h), contactSum(contactW, w, h);
    auto distAt = [&](int x, int y) { return int(enemyDist[index(x, y)]); };
    auto nearestOwnTurret = [&](int x, int y) {
        for (int r = 1; r <= 4; ++r) if (ownTurretSum.around(x, y, r) > 0) return r;
        return 5;
    };
    // The legality every turret search here uses (findAirportGuardSite), cheapest checks first.
    auto legal = [&](int x, int y) {
        if (!map.tileExists(x, y) || !TF::spaced(int(ownTurretSum.around(x, y, TF::kMinSpacing - 1)))) return false;
        if (!placeableAmongOwnUnits(x, y, 1, 1, Structure_RocketTurret)) return false;
        if (overlapsReservedStructure(x, y, 1, 1) || !preservesGroundAccess(Structure_RocketTurret, Coord(x, y))) return false;
        if (!cityRoadImpact(map, x, y, 1, 1, Structure_RocketTurret).preservesConnections) return false;
        if (currentGame->isCitySimEnabled() && wouldLandlockNeighbouringZone(map, houseID, x, y, 1, 1)) return false;
        int passable = 0;
        for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
            if ((!ox && !oy) || !map.tileExists(x + ox, y + oy)) continue;
            const Tile* tile = map.getTile(x + ox, y + oy);
            if (!tile->isMountain() && !tile->hasAStructure()) ++passable;
        }
        return FrontBatteryPolicy::keepsCorridor(passable);
    };
    const bool buildUp = !usesOldEnemySpacing();
    // The mass's legality (the drill and the line): no spacing, turrets touch as in Stefan's clumps,
    // but every site keeps kDrillCorridorNeighbours passable neighbours.
    auto massLegal = [&](int x, int y) {
        if (!map.tileExists(x, y) || ownTurretW[index(x, y)] > 0) return false;
        if (!placeableAmongOwnUnits(x, y, 1, 1, Structure_RocketTurret)) return false;
        if (overlapsReservedStructure(x, y, 1, 1) || !preservesGroundAccess(Structure_RocketTurret, Coord(x, y))) return false;
        if (!cityRoadImpact(map, x, y, 1, 1, Structure_RocketTurret).preservesConnections) return false;
        if (currentGame->isCitySimEnabled() && wouldLandlockNeighbouringZone(map, houseID, x, y, 1, 1)) return false;
        int passable = 0;
        for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
            if ((!ox && !oy) || !map.tileExists(x + ox, y + oy)) continue;
            const Tile* tile = map.getTile(x + ox, y + oy);
            if (!tile->isMountain() && !tile->hasAStructure()) ++passable;
        }
        return FrontBatteryPolicy::keepsCorridor(passable, TF::kDrillCorridorNeighbours);
    };

    // (a) FrontTurrets: the attack worth most within reach of an own building, then its cover.
    if (reinforce && !visibleGroundTurretThreats.empty()) {
        int64_t bestValue = 0;
        Coord attack = Coord::Invalid();
        for (const auto& contact : visibleGroundTurretThreats) {
            if (!map.tileExists(contact.at)) continue;
            if (ownBuildingSum.around(contact.at.x, contact.at.y, TF::kAttackReachTiles) == 0) continue;
            const int64_t value = contactSum.around(contact.at.x, contact.at.y, TF::kAttackClusterTiles);
            if (value > bestValue) { bestValue = value; attack = contact.at; }
        }
        if (attack.isValid()) {
            // The attacked building: the own building tile nearest the attack.
            Coord anchor = Coord::Invalid();
            for (int r = 0; r <= TF::kAttackReachTiles && anchor.isInvalid(); ++r)
                for (int y = attack.y - r; y <= attack.y + r && anchor.isInvalid(); ++y)
                    for (int x = attack.x - r; x <= attack.x + r; ++x) {
                        if (std::max(std::abs(x - attack.x), std::abs(y - attack.y)) != r || !map.tileExists(x, y)) continue;
                        if (ownBuildingW[index(x, y)]) { anchor = Coord(x, y); break; }
                    }
            const int cover = anchor.isValid() ? int(ownTurretSum.around(anchor.x, anchor.y, TF::kCoverTiles)) : 0;
            const int goal = TF::reinforceGoal(bestValue);
            turretFrontPlan.attackValue = int(std::min<int64_t>(bestValue, std::numeric_limits<int>::max()));
            if (anchor.isValid() && cover < goal) {
                int bestScore = std::numeric_limits<int>::min();
                Coord best = Coord::Invalid();
                const int reach = TF::kReinforceSiteTiles;
                for (int y = std::max(0, anchor.y - reach); y <= std::min(h - 1, anchor.y + reach); ++y)
                    for (int x = std::max(0, anchor.x - reach); x <= std::min(w - 1, anchor.x + reach); ++x) {
                        const int score = TF::reinforceSiteScore(TF::tileDistance(x, y, attack.x, attack.y),
                            TF::tileDistance(x, y, anchor.x, anchor.y), nearestOwnTurret(x, y), range);
                        if (score <= bestScore || !legal(x, y)) continue;
                        bestScore = score; best = Coord(x, y);
                    }
                if (best.isValid()) {
                    turretFrontPlan.site = best;
                    turretFrontPlan.mode = TurretFrontMode::Reinforce;
                    turretFrontPlan.anchor = anchor;
                    turretFrontPlan.cover = cover;
                    turretFrontPlan.goal = goal;
                }
            }
        }
    }

    // Own anchors by distance to the enemy, nearest first (ties by tile index).
    std::sort(anchors.begin(), anchors.end(), [&](Coord a, Coord b) {
        const int da = distAt(a.x, a.y), db = distAt(b.x, b.y);
        return da != db ? da < db : index(a.x, a.y) < index(b.x, b.y);
    });

    // The base's enemy-facing edge: its own building nearest an enemy building.
    const int edge = anchors.empty() ? TF::kDistanceCap : distAt(anchors.front().x, anchors.front().y);
    turretFrontPlan.edge = edge;
    turretFrontPlan.armyLead = -1;

    // (e) TurretDrill: once the edge is near an enemy base, the legal tile nearest it that at most
    // one enemy turret reaches and no nearer than kDrillMinTiles to an enemy building, touching the
    // mass, from which a turret reaches what it can shoot (Stefan's drill stood a median 11 tiles
    // from enemy buildings, at the edge of the enemy turrets' reach). No count cap: the yard pays
    // from cash beyond the economy's reserve, one order a pass.
    if (drill && buildUp && enemyKnown && turretFrontPlan.site.isInvalid() && !anchors.empty() && TF::drillStarts(edge)) {
        const bool fireOnBuildings = currentGame->getGameInitSettings().getGameOptions().turretsFireOnBuildings;
        std::vector<int32_t> targetW(n, 0);
        for (const auto& contact : visibleGroundTurretThreats)
            if (map.tileExists(contact.at)) targetW[index(contact.at.x, contact.at.y)] += contact.value;
        if (fireOnBuildings)
            for (size_t i = 0; i < n; ++i) if (enemyTileW[i]) targetW[i] += 100;
        const TF::BoxSum targetSum(targetW, w, h);
        int bestScore = std::numeric_limits<int>::min();
        Coord best = Coord::Invalid();
        const int window = BUILDRANGE + 1;
        int examined = 0;
        for (const Coord a : anchors) {
            if (examined >= TF::kDrillAnchors) break;
            ++examined;
            for (int y = std::max(0, a.y - window); y <= std::min(h - 1, a.y + window + 3); ++y)
                for (int x = std::max(0, a.x - window); x <= std::min(w - 1, a.x + window + 3); ++x) {
                    const int d = distAt(x, y);
                    if (!TF::drillSite(d, edge, int(enemyTurretSum.around(x, y, range)))) continue;
                    const int score = TF::drillSiteScore(d, targetSum.around(x, y, range - 1),
                                                         int(ownTurretSum.around(x, y, 1)));
                    if (score <= bestScore || !massLegal(x, y)) continue;
                    bestScore = score; best = Coord(x, y);
                }
        }
        if (best.isValid()) {
            turretFrontPlan.site = best;
            turretFrontPlan.mode = TurretFrontMode::Drill;
            turretFrontPlan.anchor = Coord::Invalid();
            turretDrilling = true;
        }
    }

    // (c) TurretCreep: in range of the enemy's defenders (visible enemy combat units near its
    // buildings), where at most one enemy turret reaches. Turrets fire on units only by default.
    if (creep && buildUp && enemyKnown && turretFrontPlan.site.isInvalid() && !visibleGroundTurretThreats.empty()) {
        std::vector<uint8_t> defenderSeed(n, 0);
        std::vector<int32_t> defenderW(n, 0);
        bool defenders = false;
        for (const auto& contact : visibleGroundTurretThreats) {
            if (!map.tileExists(contact.at) || distAt(contact.at.x, contact.at.y) > TF::kCreepDefenderTiles) continue;
            defenderSeed[index(contact.at.x, contact.at.y)] = 1;
            defenderW[index(contact.at.x, contact.at.y)] += contact.value;
            defenders = true;
        }
        if (defenders) {
            const std::vector<uint16_t> defenderDist = TF::chebyshevDistance(defenderSeed, w, h, TF::kDistanceCap);
            const TF::BoxSum defenderSum(defenderW, w, h);
            // No count cap (Stefan's turrets went in as a mass): the yard pays from cash beyond the
            // economy's reserve, one order a pass.
            int forward = 0;
            for (const auto& own : ownTurrets) forward += int(defenderDist[index(own.at.x, own.at.y)] <= range - 1);
            turretFrontPlan.forward = forward;
            {
                const int window = BUILDRANGE + 1;
                // Anchors whose build window can reach a tile in range of a defender, nearest first.
                std::vector<Coord> near;
                for (const Coord a : anchors)
                    if (int(defenderDist[index(a.x, a.y)]) <= range - 1 + window + 3) near.push_back(a);
                std::sort(near.begin(), near.end(), [&](Coord a, Coord b) {
                    const int da = defenderDist[index(a.x, a.y)], db = defenderDist[index(b.x, b.y)];
                    return da != db ? da < db : index(a.x, a.y) < index(b.x, b.y);
                });
                if (near.size() > size_t(TF::kCreepAnchors)) near.resize(TF::kCreepAnchors);
                int bestScore = std::numeric_limits<int>::min();
                Coord best = Coord::Invalid();
                for (const Coord a : near)
                    for (int y = std::max(0, a.y - window); y <= std::min(h - 1, a.y + window + 3); ++y)
                        for (int x = std::max(0, a.x - window); x <= std::min(w - 1, a.x + window + 3); ++x) {
                            const int turrets = int(enemyTurretSum.around(x, y, range));
                            if (!TF::creepSite(defenderDist[index(x, y)], range, turrets)) continue;
                            const int score = TF::creepSiteScore(defenderSum.around(x, y, range - 1), turrets,
                                                                nearestOwnTurret(x, y));
                            if (score <= bestScore || !legal(x, y)) continue;
                            bestScore = score; best = Coord(x, y);
                        }
                if (best.isValid()) {
                    turretFrontPlan.site = best;
                    turretFrontPlan.mode = TurretFrontMode::Creep;
                    turretFrontPlan.anchor = Coord::Invalid();
                }
            }
        }
    }

    // (b) AdvancingTurrets: a line at the enemy-facing edge, kBeltGoal wide, then a step forward.
    if (advance && buildUp && enemyKnown && turretFrontPlan.site.isInvalid() && !anchors.empty()) {
        const Coord edgeAnchor = anchors.front();
        std::vector<int> lead;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() != getHouse() || !unit->isActive() || unit->getHealth() <= 0
                || unit->isAFlyingUnit() || !QuantBotBuildPolicy::militaryItem(unit->getItemID())
                || !map.tileExists(unit->getLocation())) continue;
            lead.push_back(distAt(unit->getLocation().x, unit->getLocation().y));
        }
        turretFrontPlan.edge = edge;
        turretFrontPlan.armyUnits = int(lead.size());
        // Behind the army while it stands ahead of the edge; otherwise the line moves on its own.
        int behindLimit = 0;
        if (int(lead.size()) >= TF::kArmyLeadRank) {
            std::nth_element(lead.begin(), lead.begin() + (TF::kArmyLeadRank - 1), lead.end());
            const int armyLead = lead[TF::kArmyLeadRank - 1];
            turretFrontPlan.armyLead = armyLead;
            if (TF::armyAhead(armyLead, edge)) behindLimit = armyLead + beltBehindArmy();
        }
        // The line's width: own turrets near the edge's anchor at the edge's depth (one tile slack).
        int belt = 0;
        for (const auto& own : ownTurrets)
            belt += int(TF::tileDistance(own.at.x, own.at.y, edgeAnchor.x, edgeAnchor.y) <= TF::kBeltTiles
                        && distAt(own.at.x, own.at.y) <= edge + 1);
        int bestScore = std::numeric_limits<int>::min();
        Coord best = Coord::Invalid();
        const int window = BUILDRANGE + 1;
        int examined = 0;
        for (const Coord a : anchors) {
            if (distAt(a.x, a.y) > edge + 2 || examined >= TF::kCreepAnchors) break;
            ++examined;
            for (int y = std::max(0, a.y - window); y <= std::min(h - 1, a.y + window + 3); ++y)
                for (int x = std::max(0, a.x - window); x <= std::min(w - 1, a.x + window + 3); ++x) {
                    const int d = distAt(x, y);
                    if (!TF::beltSite(d, edge, belt, beltGoal(), behindLimit, beltStandoff(),
                                      int(enemyTurretSum.around(x, y, TF::kEnemyTurretReachTiles)))) continue;
                    const int score = TF::advanceSiteScore(d, edge, int(ownTurretSum.around(x, y, 1)));
                    if (score <= bestScore || !massLegal(x, y)) continue;
                    bestScore = score; best = Coord(x, y);
                }
        }
        if (best.isValid()) {
            turretFrontPlan.site = best;
            turretFrontPlan.mode = TurretFrontMode::Advance;
            turretFrontPlan.anchor = edgeAnchor;
            turretFrontPlan.cover = belt;
            turretFrontPlan.goal = beltGoal();
        }
    }

    // (d) RearTurretRetire: the deepest rear turrets, a few a review, above the coverage need.
    if (retireDue) {
        turretRetireBucket = getGameCycleCount() / TF::kRetirePeriodCycles;
        const auto& raid = getHouse()->getAirRaid();
        const Uint32 now = getGameCycleCount();
        const bool bombedLately = raid.lastHitCycle != 0 && now >= raid.lastHitCycle
            && now - raid.lastHitCycle < TF::kBombQuietCycles;
        const int keep = RocketTurretPolicy::coverageTurretCap(pass.coverageDemand);
        const int allowance = TF::retireAllowance(int(ownTurrets.size()), keep);
        if (enemyKnown && !bombedLately && allowance > 0) {
            struct Candidate { int dist; Uint32 id; Coord at; };
            std::vector<Candidate> rear;
            for (const auto& own : ownTurrets) {
                const int d = distAt(own.at.x, own.at.y);
                if (!TF::rearTurret(d, edge, contactSum.around(own.at.x, own.at.y, TF::kRearQuietTiles))) continue;
                bool kept = false;
                for (const Coord k : keepers)
                    if (TF::tileDistance(k.x, k.y, own.at.x, own.at.y) <= TF::kKeepNearTiles) { kept = true; break; }
                if (!kept) rear.push_back({d, own.id, own.at});
            }
            std::sort(rear.begin(), rear.end(), [](const Candidate& a, const Candidate& b) {
                return a.dist != b.dist ? a.dist > b.dist : a.id < b.id;
            });
            int retired = 0;
            for (const auto& candidate : rear) {
                if (retired >= allowance) break;
                auto* structure = dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(candidate.id));
                if (structure == nullptr || structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
                const bool sold = structure->canSell();
                const int refund = sold ? structure->getSaleRefund() : 0;
                // One line per turret for the match tables (tests/ai/turret-front-report.py).
                SDL_Log("TURRET_RETIRE: house=%d object=%u x=%d y=%d enemy_dist=%d sold=%d refund=%d standing=%d keep=%d cycle=%u",
                        houseID, candidate.id, candidate.at.x, candidate.at.y, candidate.dist, int(sold), refund,
                        int(ownTurrets.size()) - retired, keep, now);
                traceDecision("turret_retire", AITelemetry::Record().set("object", candidate.id)
                    .set("x", candidate.at.x).set("y", candidate.at.y).set("enemy_dist", candidate.dist)
                    .set("sold", int(sold)).set("refund", refund).set("keep", keep));
                if (sold) structure->doSell(); else structure->demolish();
                ++retired;
            }
            pass.itemCount[Structure_RocketTurret] -= retired;
            turretRetired += retired;
        }
    }
}

void QuantBot::logTurretFrontState(Uint32 now) {
    // Why the switches did or did not act, every two game minutes per house with any of them on:
    // the edge and the army's lead (b), and the orders so far by rule (ordered/placed/engaged/missed).
    const Uint32 bucket = now / TurretFrontPolicy::kRetirePeriodCycles;
    if (bucket == turretStateBucket) return;
    turretStateBucket = bucket;
    const auto& r = turretFrontCounts[size_t(TurretFrontMode::Reinforce)];
    const auto& c = turretFrontCounts[size_t(TurretFrontMode::Creep)];
    const auto& a = turretFrontCounts[size_t(TurretFrontMode::Advance)];
    const auto& d = turretFrontCounts[size_t(TurretFrontMode::Drill)];
    SDL_Log("TURRET_FRONT_STATE: house=%d edge=%d army_lead=%d army_units=%d mode=%d "
            "reinforce=%d/%d/%d/%d creep=%d/%d/%d/%d advance=%d/%d/%d/%d drill=%d/%d/%d/%d retired=%d "
            "blocked=%d/%d/%d/%d/%d/%d cycle=%u",
            getHouse()->getHouseID(), turretFrontPlan.edge, turretFrontPlan.armyLead, turretFrontPlan.armyUnits,
            int(turretFrontPlan.mode), r.ordered, r.placed, r.engaged, r.missed, c.ordered, c.placed, c.engaged, c.missed,
            a.ordered, a.placed, a.engaged, a.missed, d.ordered, d.placed, d.engaged, d.missed, turretRetired,
            turretFrontBlocked[BlockNotReached], turretFrontBlocked[BlockYardBusy], turretFrontBlocked[BlockUnavailable],
            turretFrontBlocked[BlockSiteGone], turretFrontBlocked[BlockPower], turretFrontBlocked[BlockFunds], now);
}

void QuantBot::trackTurretFrontOrders(Uint32 now) {
    // Each front order is followed until its turret stands at the site (placed) and has a unit as
    // its target (engaged); an order not standing after kPlaceTimeoutCycles is missed. Logging and
    // counts only: nothing here changes the game.
    namespace TF = TurretFrontPolicy;
    if (turretFrontOrders.empty()) return;
    const Map& map = getMap();
    const int houseID = getHouse()->getHouseID();
    for (auto it = turretFrontOrders.begin(); it != turretFrontOrders.end();) {
        TurretFrontOrder& order = *it;
        auto& counts = turretFrontCounts[size_t(order.mode)];
        if (order.turret == NONE_ID) {
            const ObjectBase* object = map.tileExists(order.site) ? map.getTile(order.site)->getGroundObject() : nullptr;
            if (object != nullptr && object->isAStructure() && object->getItemID() == Structure_RocketTurret
                && object->getOwner() == getHouse()) {
                order.turret = object->getObjectID();
                ++counts.placed;
                SDL_Log("TURRET_FRONT_PLACED: house=%d rule=%s x=%d y=%d object=%u delay=%u cycle=%u", houseID,
                        turretFrontRule(order.mode), order.site.x, order.site.y, order.turret, now - order.cycle, now);
            } else if (now < order.cycle || now - order.cycle > TF::kPlaceTimeoutCycles) {
                ++counts.missed;
                SDL_Log("TURRET_FRONT_MISSED: house=%d rule=%s x=%d y=%d cycle=%u", houseID,
                        turretFrontRule(order.mode), order.site.x, order.site.y, now);
                it = turretFrontOrders.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        const auto* turret = dynamic_cast<const TurretBase*>(currentGame->getObjectManager().getObject(order.turret));
        if (turret == nullptr || turret->getHealth() <= 0) { it = turretFrontOrders.erase(it); continue; }
        const ObjectBase* target = turret->getTarget();
        if (target != nullptr && !target->isAStructure()) {
            ++counts.engaged;
            SDL_Log("TURRET_FRONT_ENGAGED: house=%d rule=%s x=%d y=%d object=%u target=%u delay=%u cycle=%u", houseID,
                    turretFrontRule(order.mode), order.site.x, order.site.y, order.turret, target->getObjectID(),
                    now - order.cycle, now);
            it = turretFrontOrders.erase(it);
            continue;
        }
        ++it;
    }
}

void QuantBot::chooseYardTurretFront(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    namespace Air = QuantBotAirDefencePolicy;
    // One order per pass across all yards, from cash beyond the economy's reserve; the yard never
    // saves for it. The site waits from the last scan and is checked again here.
    if (!turretFrontPlan.active || turretFrontPlan.ordered) return;
    turretFrontYardSeen = true;
    if (yardTurn.itemID != NONE_ID || yardTurn.skipRemainingStructureLogic) { ++turretFrontBlocked[BlockYardBusy]; return; }
    if (!campaignAvailableToBuild(turn.pBuilder, Structure_RocketTurret)) { ++turretFrontBlocked[BlockUnavailable]; return; }
    const Coord site = turretFrontPlan.site;
    if (!placeableAmongOwnUnits(site.x, site.y, 1, 1, Structure_RocketTurret) || overlapsReservedStructure(site.x, site.y, 1, 1)) {
        ++turretFrontBlocked[BlockSiteGone];
        turretFrontPlan.site = Coord::Invalid();
        turretFrontPlan.active = false;
        return;
    }
    if (!hasPowerBufferForTurret(pass)) {
        ++turretFrontBlocked[BlockPower];
        // As the Airport ring: no turret the house cannot power, one Wind Trap first.
        if (!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder, Structure_WindTrap)
            && Air::affordable(pass.money, buildingCapitalCost(pass, Structure_WindTrap), turretFrontPlan.reserve)
            && findPlaceLocation(Structure_WindTrap).isValid()) {
            turretFrontPlan.ordered = true;
            yardTurn.itemID = Structure_WindTrap;
            yardTurn.structureRule = "turret_front_power";
            yardTurn.skipRemainingStructureLogic = true;
        }
        return;
    }
    if (!getHouse()->hasPower()) { ++turretFrontBlocked[BlockPower]; return; }
    int cost = purchasePrice(turn.pBuilder, Structure_RocketTurret);
    for (const auto& slab : foundationOrders(turn.pBuilder, Structure_RocketTurret, site))
        cost += purchasePrice(turn.pBuilder, slab.item);
    if (!Air::affordable(pass.money, cost, turretFrontPlan.reserve)) { ++turretFrontBlocked[BlockFunds]; return; }
    const char* rule = turretFrontRule(turretFrontPlan.mode);
    turretFrontPlan.ordered = true;
    yardTurn.itemID = Structure_RocketTurret;
    yardTurn.crimeServiceSite = site;
    yardTurn.structureRule = rule;
    yardTurn.skipRemainingStructureLogic = true;
    const Uint32 now = getGameCycleCount();
    traceDecision("turret_front", AITelemetry::Record().set("rule", rule).set("x", site.x).set("y", site.y)
        .set("cost", cost).set("cover", turretFrontPlan.cover).set("goal", turretFrontPlan.goal)
        .set("attack_value", turretFrontPlan.attackValue).set("forward", turretFrontPlan.forward)
        .set("edge", turretFrontPlan.edge).set("army_lead", turretFrontPlan.armyLead));
    // One line per order for the match tables (tests/ai/turret-front-report.py).
    SDL_Log("TURRET_FRONT: house=%d rule=%s x=%d y=%d cost=%d cover=%d goal=%d attack_value=%d forward=%d edge=%d army_lead=%d cycle=%u",
            getHouse()->getHouseID(), rule, site.x, site.y, cost, turretFrontPlan.cover, turretFrontPlan.goal,
            turretFrontPlan.attackValue, turretFrontPlan.forward, turretFrontPlan.edge, turretFrontPlan.armyLead, now);
    ++turretFrontCounts[size_t(turretFrontPlan.mode)].ordered;
    if (turretFrontOrders.size() >= TurretFrontPolicy::kTrackedOrders) turretFrontOrders.erase(turretFrontOrders.begin());
    turretFrontOrders.push_back({site, turretFrontPlan.mode, now});
    // The next scan picks the next site; this one is now the yard's reservation.
    turretFrontPlan.site = Coord::Invalid();
}
