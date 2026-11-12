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

// QuantBot force concentration (docs/formations.md, "Concentrating force"): focus fire,
// converging on a fight, and mustering distant defenders before they engage. The pooling and
// recall rules of the base defence live in scrambleWholeArmyToDefend (QuantBotArmy.cpp).
//
// Deterministic: every scan runs in the fixed unit/structure list order, each unit's decision
// depends only on the simulation state (never on another unit's decision this pass), musters
// are a vector in creation order with ordered member sets, and only integer arithmetic decides.

#include "QuantBotInternal.h"
#include <players/QuantBotMapScopePolicy.h>

using QuantBotInternal::mobileCombatItem;
namespace Force = ForceConcentrationPolicy;

namespace {

int tiles(Coord a, Coord b) { return blockDistance(a, b).lround(); }

/// The first point that \a accept takes on the line from \a from along \a step (\a step / \a length
/// tiles per step) at steps \a first..last, searching a ring of at most \a rings tiles around each
/// (rings outward, rows then columns); invalid if none. Integer and in a fixed order.
template <typename Accept>
Coord clearOfOwnBase(Coord from, Coord step, int length, int first, int last, int rings, Accept accept) {
    Coord previous = Coord::Invalid();
    for (int s = first; s <= last; ++s) {
        const Coord centre(from.x + int(int64_t(step.x) * s / length), from.y + int(int64_t(step.y) * s / length));
        if (centre == previous) continue;
        previous = centre;
        for (int ring = 0; ring <= rings; ++ring)
            for (int dy = -ring; dy <= ring; ++dy)
                for (int dx = -ring; dx <= ring; ++dx)
                    if (std::max(std::abs(dx), std::abs(dy)) == ring && accept(centre + Coord(dx, dy)))
                        return centre + Coord(dx, dy);
    }
    return Coord::Invalid();
}

Force::TargetKey targetKeyOf(const ObjectBase* object) {
    Force::TargetKey key;
    key.id = object->getObjectID();
    key.health = object->getHealth().lround();
    if (object->isAStructure()) {
        key.cls = object->getItemID() == Structure_Wall ? Force::Wall
            : object->canAttack() ? Force::ArmedBuilding : Force::Building;
    } else {
        key.cls = object->canAttack() && mobileCombatItem(object->getItemID()) ? Force::ArmedUnit : Force::Unarmed;
    }
    return key;
}

bool armed(const ObjectBase* object) {
    const int cls = targetKeyOf(object).cls;
    return cls == Force::ArmedUnit || cls == Force::ArmedBuilding;
}

Force::ValueKey valueKeyOf(const ObjectBase* object) {
    const auto key = targetKeyOf(object);
    Force::ValueKey value;
    value.cls = key.cls;
    value.health = std::max<int64_t>(1, key.health);
    value.id = key.id;
    if (key.cls == Force::ArmedUnit || key.cls == Force::ArmedBuilding) {
        const auto& data = currentGame->objectData.data[object->getItemID()][object->getProductionHouseID()];
        value.dpf = int64_t(std::max(0, data.weapondamage)) * 1000 / std::max(1, data.weaponreloadtime);
    }
    return value;
}

/// One volley of \a unit: its weapon damage (the per-hit damage the target loses).
int64_t volleyOf(const UnitBase* unit) {
    return std::max(1, currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].weapondamage);
}

} // namespace

int QuantBot::forceBits() const {
    if (forceVariantOverride != kNoForceOverride) return forceVariantOverride;
    const auto& force = botConfig().force;
    const auto& closing = closingForce();
    return (force.focusFire ? kForceFocusFire : 0) | ((force.converge || convergeChokepointNative()) ? kForceConverge : 0)
        | (force.bundle ? kForceBundle : 0) | (force.bundle && force.launchFromBundle ? kForceBundleGate : 0)
        | (force.perimeterTurrets ? kForceThreatTurrets : 0)
        | (force.sizedDefence ? kForceSizedDefence : 0)
        | ((force.openingReadiness || openingOpenMapNative()) ? kForceOpeningReadiness : 0)
        | (force.turretPriority ? kForceTurretPriority : 0) | (closing.pressOn ? kForcePressOn : 0)
        | ((force.finishWeakest || cityBoomSpiceKind() == CityBoomKind::Reactive) ? kForceFinishWeakest : 0)
        | (force.fortifiedWaves ? kForceFortifiedWaves : 0)
        | (force.sonicClearLane ? kForceSonicClearLane : 0) | (force.spendAtCap ? kForceSpendAtCap : 0)
        | (force.siegeStandoff ? kForceSiegeStandoff : 0) | (force.massAssault ? kForceMassAssault : 0)
        | (closing.finishWhenLeading ? kForceFinishLeading : 0);
}

bool QuantBot::vanillaMultiEnemyGame() const {
    if (!autonomousCustomArmy() || currentGame->isCitySimEnabled()
        || currentGame->getGameInitSettings().getModName() != "vanilla") return false;
    int firstEnemy = -1;
    for (const auto& house : currentGame->getGameInitSettings().getHouseInfoList()) {
        if (house.playerInfoList.empty() || house.team == getHouse()->getTeamID()) continue;
        if (firstEnemy < 0) firstEnemy = house.team;
        else if (house.team != firstEnemy) return true;
    }
    return false;
}

bool QuantBot::vanillaFfaTuningGame() const {
    if (!autonomousCustomArmy() || difficulty != Difficulty::Brutal) return false;
    int friendlyControllers = 0, hostileControllers = 0, firstHostileTeam = -1;
    bool multipleHostileTeams = false;
    // These are the original occupied controller rows, not runtime House
    // objects, survivors, guests or a policy-dependent roster.
    for (const auto& house : currentGame->getGameInitSettings().getHouseInfoList()) {
        if (house.playerInfoList.empty()) continue;
        if (house.team == getHouse()->getTeamID()) ++friendlyControllers;
        else {
            ++hostileControllers;
            if (firstHostileTeam < 0) firstHostileTeam = house.team;
            else if (firstHostileTeam != house.team) multipleHostileTeams = true;
        }
    }
    return QuantBotCoreDefencePolicy::tuningScope(true, currentGame->isCitySimEnabled(),
        currentGame->getGameInitSettings().getModName() == "vanilla", lastCalculatedSpice > 0,
        friendlyControllers, hostileControllers, multipleHostileTeams);
}

bool QuantBot::vanillaBoomGame() const {
    if (!autonomousCustomArmy() || currentGame->isCitySimEnabled()
        || currentGame->getGameInitSettings().getModName() != "vanilla" || lastCalculatedSpice <= 0)
        return false;
    int allies = 0, enemies = 0, firstEnemyTeam = -1;
    bool severalEnemyTeams = false;
    for (const auto& house : currentGame->getGameInitSettings().getHouseInfoList()) {
        if (house.playerInfoList.empty()) continue;
        if (house.team == getHouse()->getTeamID()) ++allies;
        else {
            ++enemies;
            if (firstEnemyTeam < 0) firstEnemyTeam = house.team;
            else if (house.team != firstEnemyTeam) severalEnemyTeams = true;
        }
    }
    return QuantBotVanillaPolicy::rosterScope(allies, enemies, severalEnemyTeams)
        && QuantBotVanillaPolicy::boomScope(botConfig().force.vanillaBoomFfaOnly, enemies, severalEnemyTeams);
}

bool QuantBot::vanillaReactiveKnowledge() const {
    return botConfig().force.vanillaReactiveBoom && vanillaBoomGame();
}

QuantBotVanillaPolicy::Plan QuantBot::vanillaBoldPlan(QuantBotVanillaPolicy::Observation& receipt) const {
    using Plan = QuantBotVanillaPolicy::Plan;
    const auto& force = botConfig().force;
    receipt = {};
    if (!(force.vanillaHarvesterBoom || force.vanillaRefineryBoom || force.vanillaGreedyBoom || force.vanillaReactiveBoom)
        || !vanillaBoomGame()) return Plan::None;
    const unsigned minute = getGameCycleCount() / MILLI2CYCLES(60000);
    const Plan kind = force.vanillaHarvesterBoom ? Plan::Harvester : force.vanillaRefineryBoom ? Plan::Refinery
        : force.vanillaGreedyBoom ? Plan::GreedyEconomy : Plan::ReactiveEconomy;
    const bool refineryGreedy = kind == Plan::Refinery && force.vanillaGreedyBoom;
    receipt.stopMinute = kind == Plan::GreedyEconomy || refineryGreedy ? force.vanillaGreedyBoomMinutes
        : kind == Plan::Harvester || kind == Plan::Refinery ? 10 : 0;
    receipt.plan = QuantBotVanillaPolicy::timedPlan(kind, minute, force.vanillaGreedyBoomMinutes, refineryGreedy);
    if (kind == Plan::ReactiveEconomy) {
        vanillaReactiveSurvey(receipt);
        receipt.plan = QuantBotVanillaPolicy::reactiveArmy(receipt.ownArmy, receipt.enemyArmy, receipt.nearArmy,
            receipt.ownEconomy, receipt.leaderEconomy, receipt.armyRatioBps, receipt.economyRatioBps)
            ? Plan::ReactiveArmy : Plan::ReactiveEconomy;
    }
    return receipt.plan;
}

bool QuantBot::citySpiceScope() const {
    if (!autonomousCustomArmy() || fighterOnly() || !currentGame->isCitySimEnabled()
        || getGameInitSettings().getModName() != "dunecity") return false;
    const auto type = getGameInitSettings().getGameType();
    return (type == GameType::CustomGame || type == GameType::CustomMultiplayer)
        && getGameInitSettings().getInitialSpiceTilePermille() > 0;
}

bool QuantBot::cityEconomyExperimentScope() const {
    return citySpiceScope() && !getGameInitSettings().isCivilizationWars();
}

bool QuantBot::citySpiceFfaScope(unsigned minimumControllers) const {
    if (!citySpiceScope()) return false;
    unsigned occupied = 0;
    std::array<int, NUM_HOUSES> teams{};
    for (const auto& row : getGameInitSettings().getHouseInfoList()) {
        if (row.playerInfoList.empty()) continue;
        for (unsigned i = 0; i < occupied; ++i)
            if (teams[i] == row.team) return false;
        if (occupied == teams.size()) return false;
        teams[occupied++] = row.team;
    }
    return occupied >= minimumControllers;
}

const QuantBot::CitySpiceShare& QuantBot::citySpiceShare() const {
    const Uint32 now = getGameCycleCount();
    if (citySpiceShareCycle == now) return citySpiceShareCache;
    citySpiceShareCycle = now;
    auto& share = citySpiceShareCache;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    share.ownSpice = share.reachableSpice = 0;
    share.ownTiles = share.reachableTiles = share.ownAnchors = share.rivals = 0;
    share.owner.assign(static_cast<size_t>(w) * h, 0);
    share.ownDistance.assign(static_cast<size_t>(w) * h, -1);
    const int ownTeam = getHouse()->getTeamID();
    auto live = [](const StructureBase* structure) {
        return structure->getOwner() && !structure->getOwner()->isNatives()
            && structure->getHealth() > 0 && structure->isActive();
    };
    // A house harvests into its drop-offs; only a house without one is
    // anchored at its construction yards (it will site its refinery there).
    std::array<bool, NUM_HOUSES> dropoff{};
    for (const auto* structure : getStructureList())
        if (live(structure) && structure->acceptsHarvesterDropoff())
            dropoff[structure->getOwner()->getHouseID()] = true;
    struct Anchor { int x0, y0, x1, y1, house; bool ours; std::array<int, 4> regions; int regionCount; };
    std::vector<Anchor> anchors;
    for (const auto* structure : getStructureList()) {
        if (!live(structure)) continue;
        const int house = structure->getOwner()->getHouseID();
        if (dropoff[house] ? !structure->acceptsHarvesterDropoff()
                : structure->getItemID() != Structure_ConstructionYard) continue;
        Anchor anchor{structure->getX(), structure->getY(),
            structure->getX() + structure->getStructureSizeX() - 1,
            structure->getY() + structure->getStructureSizeY() - 1,
            house, structure->getOwner()->getTeamID() == ownTeam, {}, 0};
        // Vehicles leave a building onto its perimeter; collect those regions.
        for (int y = anchor.y0 - 1; y <= anchor.y1 + 1; ++y)
            for (int x = anchor.x0 - 1; x <= anchor.x1 + 1; ++x) {
                if ((x >= anchor.x0 && x <= anchor.x1 && y >= anchor.y0 && y <= anchor.y1)
                    || !getMap().tileExists(x, y)) continue;
                const int region = getMap().vehicleTerrainRegion(Coord(x, y));
                if (region == 0 || anchor.regionCount == static_cast<int>(anchor.regions.size())
                    || std::find(anchor.regions.begin(), anchor.regions.begin() + anchor.regionCount, region)
                        != anchor.regions.begin() + anchor.regionCount) continue;
                anchor.regions[anchor.regionCount++] = region;
            }
        if (anchor.regionCount > 0) anchors.push_back(anchor);
    }
    std::array<bool, NUM_HOUSES> rival{};
    for (const auto& anchor : anchors) {
        if (anchor.ours) { ++share.ownAnchors; continue; }
        for (const auto& own : anchors) {
            if (!own.ours) continue;
            for (int i = 0; i < anchor.regionCount && !rival[anchor.house]; ++i)
                for (int j = 0; j < own.regionCount; ++j)
                    if (anchor.regions[i] == own.regions[j]) { rival[anchor.house] = true; break; }
        }
    }
    share.rivals = static_cast<int>(std::count(rival.begin(), rival.end(), true));
    if (share.ownAnchors == 0) return share;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const Tile* tile = getMap().getTile(x, y);
        if (!tile->hasSpice()) continue;
        const int region = getMap().vehicleTerrainRegion(Coord(x, y));
        if (region == 0) continue;
        int own = std::numeric_limits<int>::max(), other = std::numeric_limits<int>::max();
        std::array<int, NUM_HOUSES> rivalDistance;
        rivalDistance.fill(std::numeric_limits<int>::max());
        for (const auto& anchor : anchors) {
            if (std::find(anchor.regions.begin(), anchor.regions.begin() + anchor.regionCount, region)
                == anchor.regions.begin() + anchor.regionCount) continue;
            const int dx = std::max({0, anchor.x0 - x, x - anchor.x1});
            const int dy = std::max({0, anchor.y0 - y, y - anchor.y1});
            const int d = std::max(dx, dy);
            int& best = anchor.ours ? own : rivalDistance[anchor.house];
            best = std::min(best, d);
        }
        other = *std::min_element(rivalDistance.begin(), rivalDistance.end());
        if (own == std::numeric_limits<int>::max()) continue;
        const int64_t amount = tile->getSpice().lround();
        const size_t cell = static_cast<size_t>(y) * w + x;
        share.reachableSpice += amount;
        ++share.reachableTiles;
        share.ownDistance[cell] = static_cast<int16_t>(std::min(own, 32767));
        if (own < other) {
            share.owner[cell] = 1;
            share.ownSpice += amount;
            ++share.ownTiles;
        } else if (own == other) {
            share.owner[cell] = 2;
            const int ties = static_cast<int>(std::count(rivalDistance.begin(), rivalDistance.end(), own));
            share.ownSpice += amount / (1 + ties);
        } else share.owner[cell] = 3;
    }
    return share;
}

QuantBot::CityBoomKind QuantBot::cityBoomKind() const {
    const auto& f = botConfig().force;
    if (!currentGame || !(f.cityBoomWorkers || f.cityBoomRefineries || f.cityBoomGreedy
            || f.cityBoomShortTax || f.cityBoomReactive)) return CityBoomKind::Native;
    const Uint32 now = getGameCycleCount();
    if (cityBoomKindCycle == now) return cityBoomKindCache;
    cityBoomKindCycle = now;
    cityBoomKindCache = CityBoomKind::Native;
    if (!citySpiceFfaScope(5)) return cityBoomKindCache;
    cityBoomKindCache = f.cityBoomWorkers ? CityBoomKind::Workers
        : f.cityBoomRefineries ? CityBoomKind::Refineries
        : f.cityBoomGreedy ? CityBoomKind::Greedy
        : f.cityBoomShortTax ? CityBoomKind::ShortTax : CityBoomKind::Reactive;
    return cityBoomKindCache;
}

const QuantBot::CityBoomSurvey& QuantBot::cityBoomSurvey() const {
    if (cityBoomSurveyCycle == getGameCycleCount()) return cityBoomSurveyCache;
    cityBoomSurveyCycle = getGameCycleCount();
    auto& survey = cityBoomSurveyCache;
    survey = {};
    cityBoomBaseSites.clear(); // Retain capacity; no per-survey heap churn.
    if (cityBoomSpiceKind() != CityBoomKind::Reactive) return survey;
    std::array<bool, NUM_HOUSES> enemy{};
    std::array<int64_t, NUM_HOUSES> armies{}, economies{}, reachable{}, local{};
    std::array<int, NUM_HOUSES> distance;
    distance.fill(std::numeric_limits<int>::max());
    const int ownID = getHouse()->getHouseID(), ownTeam = getHouse()->getTeamID();
    // The initial controller roster defines opponents; live Houses supply
    // their actual state, never an enemy count or a fog-dependent estimate.
    for (const auto& row : getGameInitSettings().getHouseInfoList()) {
        if (row.playerInfoList.empty() || row.team == ownTeam) continue;
        for (int h = 0; h < NUM_HOUSES; ++h)
            if (const auto* house = getHouse(h); house && !house->isNatives()
                && house->isAlive() && house->getTeamID() == row.team) enemy[h] = true;
    }
    const auto* sim = currentGame->getCitySimulation();
    const int64_t elapsed = std::max<int64_t>(DuneCity::kCyclesPerMinute, getGameCycleCount());
    auto economicCapital = [&](const House* house) {
        const int h = house->getHouseID();
        const auto& state = sim->getHouseState(h);
        const int tax = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
            state.taxBaseEighths, sim->getCityTax(h), state.avgLandValue));
        const int64_t spice = int64_t(house->getHarvestedSpice().lround()) * DuneCity::kCyclesPerMinute / elapsed;
        const auto& data = currentGame->objectData.data;
        return int64_t(house->getCredits())
            + int64_t(house->getNumItems(Unit_Harvester)) * data[Unit_Harvester][h].price
            + int64_t(house->getNumItems(Structure_Refinery)) * data[Structure_Refinery][h].price
            + 4 * (spice + tax);
    };
    survey.ownEconomy = economicCapital(getHouse());
    for (int h = 0; h < NUM_HOUSES; ++h) if (enemy[h]) {
        economies[h] = economicCapital(getHouse(h));
        survey.leaderEconomy = std::max(survey.leaderEconomy, economies[h]);
    }
    const ObjectBase* anchor = nullptr;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0 || !structure->isActive()) continue;
        switch (structure->getItemID()) {
            case Structure_Wall: case Structure_Slab1: case Structure_Slab4: case Structure_Road: case Structure_PowerLine:
                continue;
            default: break;
        }
        cityBoomBaseSites.push_back(structure);
        if (!anchor || (structure->getItemID() == Structure_ConstructionYard
            && anchor->getItemID() != Structure_ConstructionYard)) anchor = structure;
    }
    if (!anchor) for (const auto* unit : getUnitList())
        if (unit->getOwner() == getHouse() && unit->getHealth() > 0 && unit->isActive()) { anchor = unit; break; }
    if (!anchor) return survey;
    for (const auto* unit : getUnitList()) {
        const auto* owner = unit->getOwner();
        if (!owner || owner->isNatives() || !unit->isActive() || unit->getHealth() <= 0
            || !unit->canAttack() || !mobileCombatItem(unit->getItemID())) continue;
        const int h = owner->getHouseID();
        if (h != ownID && !enemy[h]) continue;
        const int64_t value = CombatPowerPolicy::unitValue(
            currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price,
            unit->getHealth().lround(), unit->getMaxHealth());
        bool reaches = !unit->isAGroundUnit(), near = false;
        for (const auto* base : cityBoomBaseSites) {
            const Coord closest = base->getClosestPoint(unit->getLocation());
            const int dx = closest.x - unit->getX(), dy = closest.y - unit->getY();
            const bool close = std::abs(dx) <= Force::kPoolThreatTiles && std::abs(dy) <= Force::kPoolThreatTiles
                && dx * dx + dy * dy <= Force::kPoolThreatTiles * Force::kPoolThreatTiles;
            if (!reaches || close) {
                // Shared terrain-region cache, not a per-unit path search.
                const bool canReach = groundAttackReachable(unit, base);
                reaches |= canReach;
                near |= close && canReach;
            }
            if (reaches && near) break;
        }
        if (cityBoomBaseSites.empty()) reaches = groundAttackReachable(unit, anchor);
        if (h == ownID) {
            survey.ownArmy += value;
            if (near) survey.ownNearbyArmy += value;
        } else {
            armies[h] += value;
            if (reaches) reachable[h] += value;
            if (near) local[h] += value;
        }
    }
    // Nearby incursions are grouped by opponent, not summed across FFA rivals.
    for (int h = 0; h < NUM_HOUSES; ++h) {
        survey.strongestArmy = std::max(survey.strongestArmy, reachable[h]);
        survey.nearbyArmy = std::max(survey.nearbyArmy, local[h]);
    }
    for (const auto* structure : getStructureList()) {
        if (!structure->getOwner() || structure->getOwner()->isNatives()
            || !enemy[structure->getOwner()->getHouseID()] || structure->getHealth() <= 0 || !structure->isActive()) continue;
        switch (structure->getItemID()) {
            case Structure_Wall: case Structure_Slab1: case Structure_Slab4: case Structure_Road: case Structure_PowerLine:
                continue;
            default: break;
        }
        const int h = structure->getOwner()->getHouseID();
        distance[h] = std::min(distance[h], tiles(anchor->getLocation(), structure->getLocation()));
    }
    std::array<int, 2> neighbours{{-1, -1}};
    for (int h = 0; h < NUM_HOUSES; ++h) {
        if (!enemy[h] || distance[h] == std::numeric_limits<int>::max()) continue;
        if (neighbours[0] < 0 || distance[h] < distance[neighbours[0]]) {
            neighbours[1] = neighbours[0]; neighbours[0] = h;
        } else if (neighbours[1] < 0 || distance[h] < distance[neighbours[1]]) neighbours[1] = h;
    }
    int weakest = -1;
    for (int h : neighbours) if (h >= 0
        && (weakest < 0 || armies[h] + economies[h] < armies[weakest] + economies[weakest]
            || (armies[h] + economies[h] == armies[weakest] + economies[weakest] && h < weakest))) weakest = h;
    if (weakest >= 0) survey.targetTeam = getHouse(weakest)->getTeamID();
    const auto& f = botConfig().force;
    survey.threatened = survey.nearbyArmy > survey.ownNearbyArmy;
    survey.funding = !survey.threatened
        && survey.ownArmy * 10000 >= survey.strongestArmy * f.cityBoomArmyRatioBps
        && survey.ownEconomy * 10000 < survey.leaderEconomy * f.cityBoomEconomyRatioBps;
    return survey;
}


int QuantBot::waveReadinessPercent() const {
    if (!autonomousCustomArmy()) return 100;
    if (forceVariantOverride != kNoForceOverride)
        return (forceVariantOverride & kForceBigWaves) ? 150 : (forceVariantOverride & kForceSmallWaves) ? 60 : 100;
    if (botConfig().force.vanillaGreedyBoom && vanillaBoomGame()
        && getGameCycleCount() / MILLI2CYCLES(60000) < 20) return 150;
    if (botConfig().force.vanillaFfaFullWaves && vanillaMultiEnemyGame()) return 100;
    // A small start buys one full wave before its tempo starts splitting production into raids.
    // This ends only when that wave actually launches, not when its first heavy factory stands.
    if (openingCombatBudgetActive()) return 100;
    // The tempo of the strategy (docs/ai-play-styles.md, "Strategies"); Base keeps the config's.
    const auto* tempo = strategyTempo();
    if (tempo && tempo->readinessPercent > 0) return std::clamp(tempo->readinessPercent, 10, 300);
    return std::clamp(botConfig().force.waveReadinessPercent, 10, 300);
}

int QuantBot::openingReadinessPercent() const {
    const int percent = waveReadinessPercent();
    if (botConfig().force.vanillaGreedyBoom && vanillaBoomGame()
        && getGameCycleCount() / MILLI2CYCLES(60000) < 20) return 150;
    // I: a house that started without an army able to launch a wave (a Construction Yard start,
    // not a Fortress with a starting army) holds its first wave until full readiness: small
    // early waves into a base that is building its defence died there (docs/formations.md,
    // "Scenario tests"). A starting army that can strike keeps the smaller readiness.
    if (!forceVariant(kForceOpeningReadiness)) return percent;
    // A Rush tempo launches its opening at its own readiness.
    if (const auto* tempo = strategyTempo(); tempo && !tempo->openingReadiness) return percent;
    // In a team game the opening is over once the team's army is worth the house threshold: three
    // Hard allies each holding for 28,000 never launched under a Medium team's waves (3v3, 12
    // seeds, docs/ai-scenario-matrix.md "Team games"). Without allies the team is the house.
    const bool teamReady = teamGroundArmyValue() >= houseAttackThreshold();
    return Force::openingReadiness(percent, customWave.launched != 0 || teamReady, initialMilitaryValue,
                                   houseAttackThreshold());
}

int QuantBot::launchReadinessPercent() {
    const int percent = waveReadinessPercent();
    if (percent >= 100) return percent;
    if (openingReadinessPercent() < 100
        && !(forceVariant(kForceStrengthReadiness)
             && (forceVariant(kForceEstimate) ? outnumberedByEstimate() : outnumberedByObservedArmy())))
        return percent;
    ++forceStats.fullReadiness;
    return 100;
}

namespace {

/// A mobile ground fighter (what the army comparisons count), alive and on the map. Only the
/// army and threat aggregations call this, so units of the Natives (crime troopers) are left
/// out here: they are no team's army. Local targeting does not use it and still fights them.
bool groundFighter(const UnitBase* unit) {
    return unit->getOwner() && !unit->getOwner()->isNatives() && unit->getHealth() > 0 && unit->isActive()
        && unit->isAGroundUnit() && unit->canAttack() && mobileCombatItem(unit->getItemID());
}

int64_t healthValue(const UnitBase* unit) {
    return CombatPowerPolicy::unitValue(currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price,
                                        unit->getHealth().lround(), unit->getMaxHealth());
}

} // namespace

void QuantBot::vanillaReactiveSurvey(QuantBotVanillaPolicy::Observation& receipt, int* firstTeam,
                                   const UnitBase* lead) const {
    // Full simulation truth, not sightings, visibility, telemetry or client-local state. No decision cache.
    struct Team {
        int id{-1}, distance{INT32_MAX};
        int64_t army{}, reachable{}, nearby{}, economy{};
    };
    std::array<Team, NUM_HOUSES> teams{};
    std::array<int, NUM_HOUSES> houseTeam{};
    houseTeam.fill(-1);
    int teamCount = 0;
    const int myTeam = getHouse()->getTeamID();
    const auto& data = currentGame->objectData.data;
    const auto economicSize = [&](const House* house) -> int64_t {
        const int id = house->getHouseID();
        const int worker = std::max(0, data[Unit_Harvester][id].price);
        // A refinery includes a gifted worker; count its bay capital net of that worker, not twice.
        const int bay = std::max(0, data[Structure_Refinery][id].price - worker);
        return int64_t(std::max(0, house->getCredits())) + int64_t(house->getNumHarvesters()) * worker
            + int64_t(house->getNumItems(Structure_Refinery)) * bay;
    };
    receipt.ownEconomy = economicSize(getHouse());
    receipt.armyRatioBps = botConfig().force.vanillaReactiveArmyRatioBps;
    receipt.economyRatioBps = botConfig().force.vanillaReactiveEconomyRatioBps;
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const auto* house = getHouse(h);
        if (!house || house->isNatives() || !house->isAlive() || house->getTeamID() == myTeam) continue;
        int t = 0;
        while (t < teamCount && teams[t].id != house->getTeamID()) ++t;
        if (t == teamCount) teams[teamCount++].id = house->getTeamID();
        houseTeam[h] = t;
        const int64_t economy = economicSize(house);
        teams[t].economy += economy;
        receipt.leaderEconomy = std::max(receipt.leaderEconomy, economy);
    }
    const auto core = [](const StructureBase* structure) {
        if (!structure->getOwner() || structure->getHealth() <= 0) return false;
        switch (structure->getItemID()) {
            case Structure_Wall: case Structure_Slab1: case Structure_Slab4:
            case Structure_Road: case Structure_PowerLine: return false;
            default: return true;
        }
    };
    const ObjectBase* home = nullptr;
    Coord low(INT32_MAX, INT32_MAX), high(-1, -1);
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || !core(structure)) continue;
        if (!home || (structure->getItemID() == Structure_ConstructionYard && home->getItemID() != Structure_ConstructionYard))
            home = structure;
        const Coord at = structure->getLocation(), size = structure->getStructureSize();
        low.x = std::min(low.x, at.x); low.y = std::min(low.y, at.y);
        high.x = std::max(high.x, at.x + size.x - 1); high.y = std::max(high.y, at.y + size.y - 1);
    }
    const bool hasBase = home != nullptr;
    if (!home) for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isActive()) continue;
        if (!home || unit->getItemID() == Unit_MCV) home = unit;
        if (unit->getItemID() == Unit_MCV) break;
    }
    constexpr int nearTiles = 18;
    for (const auto* unit : getUnitList()) {
        if (!unit->getOwner() || unit->getOwner()->isNatives() || unit->getHealth() <= 0 || !unit->isActive()
            || !unit->canAttack() || !mobileCombatItem(unit->getItemID())) continue;
        const int64_t value = healthValue(unit); // Ground and armed air are counted consistently on both sides.
        if (unit->getOwner() == getHouse()) {
            receipt.ownArmy += value;
            if (firstTeam && !lead && unit->isAGroundUnit()) lead = unit;
            continue;
        }
        const int house = unit->getOwner()->getHouseID();
        if (house < 0 || house >= NUM_HOUSES || houseTeam[house] < 0) continue;
        auto& team = teams[houseTeam[house]];
        team.army += value;
        bool reachable = false, near = false;
        if (home && !hasBase) {
            reachable = getMap().terrainAttackReachable(*unit, *home);
            near = tiles(unit->getLocation(), home->getLocation()) <= nearTiles;
        } else if (home) {
            const Coord at = unit->getLocation();
            const bool nearBounds = at.x >= low.x - nearTiles && at.y >= low.y - nearTiles
                && at.x <= high.x + nearTiles && at.y <= high.y + nearTiles;
            for (const auto* structure : getStructureList()) {
                if (structure->getOwner() != getHouse() || !core(structure)) continue;
                if (!reachable) reachable = getMap().terrainAttackReachable(*unit, *structure);
                if (nearBounds && !near) near = tiles(at, structure->getClosestPoint(at)) <= nearTiles;
                if (reachable && (!nearBounds || near)) break;
            }
        }
        if (reachable) team.reachable += value;
        if (reachable && near) team.nearby += value;
    }
    for (int t = 0; t < teamCount; ++t) {
        receipt.enemyArmy = std::max(receipt.enemyArmy, teams[t].reachable);
        receipt.nearArmy = std::max(receipt.nearArmy, teams[t].nearby);
    }
    if (!firstTeam) return;
    *firstTeam = -1;
    if (!home || !lead) return;
    for (const auto* structure : getStructureList()) {
        if (!core(structure)) continue;
        const int house = structure->getOwner()->getHouseID();
        if (house < 0 || house >= NUM_HOUSES || houseTeam[house] < 0
            || !groundAttackReachable(lead, structure)) continue;
        auto& team = teams[houseTeam[house]];
        team.distance = std::min(team.distance, tiles(home->getLocation(), structure->getClosestPoint(home->getLocation())));
    }
    // Neighbours are the two nearest terrain-reachable hostile teams; only then compare true economic/army size.
    std::array<int, 2> neighbours{-1, -1};
    for (int t = 0; t < teamCount; ++t) {
        if (teams[t].distance == INT32_MAX) continue;
        for (int n = 0; n < 2; ++n) {
            const int previous = neighbours[n];
            if (previous < 0 || teams[t].distance < teams[previous].distance
                || (teams[t].distance == teams[previous].distance && teams[t].id < teams[previous].id)) {
                if (n == 0) neighbours[1] = neighbours[0];
                neighbours[n] = t;
                break;
            }
        }
    }
    int weakest = -1;
    for (const int t : neighbours) {
        if (t < 0) continue;
        const int64_t size = teams[t].economy + teams[t].army;
        const int64_t previous = weakest < 0 ? INT64_MAX : teams[weakest].economy + teams[weakest].army;
        if (weakest < 0 || size < previous || (size == previous && teams[t].id < teams[weakest].id)) weakest = t;
    }
    if (weakest >= 0) *firstTeam = teams[weakest].id;
}

bool QuantBot::outnumberedByObservedArmy() const {
    // Against the strongest enemy team in view, not every enemy summed (see enemyThreatEstimate).
    const int myTeam = getHouse()->getTeamID();
    int64_t own = 0;
    std::map<int, int64_t> hostile;
    for (const auto* unit : getUnitList()) {
        if (!groundFighter(unit)) continue;
        const bool mine = unit->getOwner() == getHouse();
        if (!mine && (unit->getOwner()->getTeamID() == myTeam || !unit->isVisible(myTeam))) continue;
        (mine ? own : hostile[unit->getOwner()->getTeamID()]) += healthValue(unit);
    }
    int64_t strongest = 0;
    for (const auto& [team, value] : hostile) strongest = std::max(strongest, value);
    return Force::outnumbered(own, strongest);
}

void QuantBot::updateEnemySightings() {
    const int myTeam = getHouse()->getTeamID();
    const Uint32 now = getGameCycleCount();
    for (const auto* unit : getUnitList())
        if (groundFighter(unit) && unit->getOwner()->getTeamID() != myTeam && unit->isVisible(myTeam))
            enemySightings[unit->getObjectID()] = {healthValue(unit), now};
    // Forget the dead, the captured and the long unseen (id order: the same on every peer).
    const Uint32 memory = Uint32(MILLI2CYCLES(Force::kSightingMemoryMs));
    for (auto it = enemySightings.begin(); it != enemySightings.end();) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(it->first));
        // A unit now owned by the Natives (never sighted as one) is no enemy army either.
        const bool gone = !unit || unit->getHealth() <= 0 || !unit->getOwner() || unit->getOwner()->getTeamID() == myTeam
            || unit->getOwner()->isNatives()
            || ArmyPosturePolicy::elapsed(now, it->second.cycle) > memory;
        it = gone ? enemySightings.erase(it) : std::next(it);
    }
}

int64_t QuantBot::enemyThreatEstimate() const {
    // The remembered army of the strongest single enemy team. In a 1v1 or against one alliance
    // that is every remembered fighter; in a free-for-all the other houses fight each other as
    // much as us, and summing them made every house permanently "outnumbered" (three equal
    // starting armies are 300% of ours), so no house ever launched a wave and the reserve held
    // the perimeter inside its own city (docs/formations.md, "Free-for-all: the army deploys").
    // Grouped by the fighter's current owner: the sightings are in id order and std::map keeps
    // the teams ordered, so every peer gets the same value.
    std::map<int, int64_t> teams;
    for (const auto& [id, sighting] : enemySightings)
        if (const auto* unit = getObject(id); unit && unit->getOwner()) teams[unit->getOwner()->getTeamID()] += sighting.value;
    int64_t strongest = 0;
    for (const auto& [team, value] : teams) strongest = std::max(strongest, value);
    return strongest;
}

int64_t QuantBot::ownGroundArmyValue() const {
    int64_t own = 0;
    for (const auto* unit : getUnitList())
        if (unit->getOwner() == getHouse() && groundFighter(unit)) own += healthValue(unit);
    return own;
}

int64_t QuantBot::teamGroundArmyValue() const {
    const int myTeam = getHouse()->getTeamID();
    int64_t team = 0;
    for (const auto* unit : getUnitList())
        if (groundFighter(unit) && unit->getOwner()->getTeamID() == myTeam) team += healthValue(unit);
    return team;
}

int QuantBot::vultureOpportunity(const std::vector<Uint32>& strikeUnits) {
    const int level = botConfig().force.ffaVultureLevel;
    if (level == 0 || !autonomousCustomArmy()) return -1;
    const auto& init = currentGame->getGameInitSettings();
    const auto type = init.getGameType();
    if (type != GameType::CustomGame && type != GameType::CustomMultiplayer) return -1;
    if (init.getModName() != "vanilla" && init.getModName() != "dunecity") return -1;
    // Scope is the original occupied controller roster, not surviving runtime houses.
    std::array<int, NUM_HOUSES> teams;
    teams.fill(-1);
    int count = 0;
    for (const auto& info : init.getHouseInfoList()) {
        if (info.playerInfoList.empty()) continue;
        const int h = static_cast<int>(info.houseID);
        if (h < 0 || h >= NUM_HOUSES) return -1;
        for (const int team : teams) if (team == info.team) return -1;
        teams[h] = info.team;
        ++count;
    }
    if (count != 4 && count != 6) return -1;
    if (init.getInitialSpiceTilePermille() <= 0) return -1;
    const int mine = getHouse()->getHouseID();
    std::array<int64_t, NUM_HOUSES> army{};
    std::array<std::array<bool, NUM_HOUSES>, NUM_HOUSES> shooting{};
    const int64_t strike = groundValueOf(strikeUnits);
    for (const auto* unit : getUnitList()) {
        if (!groundFighter(unit)) continue;
        const int h = unit->getOwner()->getHouseID();
        if (h < 0 || h >= NUM_HOUSES || teams[h] < 0) continue;
        army[h] += healthValue(unit);
        if (h == mine) continue;
        const auto* target = unit->getTarget();
        if (!target || !target->isAUnit() || !groundFighter(static_cast<const UnitBase*>(target))
            || !unit->isInWeaponRange(target)) continue;
        const int other = target->getOwner()->getHouseID();
        if (other < 0 || other >= NUM_HOUSES || other == mine || other == h || teams[other] < 0) continue;
        shooting[h][other] = true;
    }
    int weakest = -1, opponent = -1;
    bool fighting = false;
    for (int a = 0; a < NUM_HOUSES; ++a)
        for (int b = a + 1; b < NUM_HOUSES; ++b) {
            if (!shooting[a][b] || !shooting[b][a]) continue;
            fighting = true;
            const int loser = army[a] <= army[b] ? a : b;
            if (weakest < 0 || army[loser] < army[weakest]) {
                weakest = loser;
                opponent = loser == a ? b : a;
            }
        }
    if (!fighting) return -1;
    const auto losses = [&](int h) {
        int lost = 0;
        const House* house = getHouse(h);
        if (house) for (int item = ItemID_FirstID; item < Num_ItemID; ++item)
            if (mobileCombatItem(item)) lost += house->getNumLostItems(item);
        return lost;
    };
    const bool attack = Force::vultureStrike(level, strike, army[weakest]);
    traceDecision("ffa_vulture", AITelemetry::Record()
        .set("decision", attack ? "strike" : "hold").set("level", level)
        .set("strike_force", strike).set("target_house", weakest).set("rival_house", opponent)
        .set("target_army", weakest < 0 ? -1 : army[weakest])
        .set("rival_army", opponent < 0 ? -1 : army[opponent])
        .set("target_losses", losses(weakest)).set("rival_losses", losses(opponent)));
    return attack ? teams[weakest] : -2;
}

const ObjectBase* QuantBot::vultureObjective(const UnitBase* lead, int team) const {
    if (!lead) return nullptr;
    const StructureBase* chosen = nullptr;
    int best = std::numeric_limits<int>::max();
    for (const auto* structure : getStructureList()) {
        if (!structure->getOwner() || structure->getOwner()->getTeamID() != team
            || structure->getHealth() <= 0) continue;
        switch (structure->getItemID()) {
            case Structure_Wall: case Structure_Slab1: case Structure_Slab4:
            case Structure_Road: case Structure_PowerLine: continue;
            default: break;
        }
        int score = tiles(lead->getLocation(), structure->getLocation());
        switch (structure->getItemID()) {
            case Structure_ConstructionYard: case Structure_HeavyFactory:
            case Structure_LightFactory: case Structure_HighTechFactory: case Structure_Refinery:
                score -= Force::kFinishPriorityTiles; break;
            default: break;
        }
        if (score >= best || !groundAttackReachable(lead, structure)) continue;
        best = score;
        chosen = structure;
    }
    return chosen;
}

const ObjectBase* QuantBot::allyWaveObjective() const {
    if (!closingForce().joinAllyWaves || difficulty < Difficulty::Hard || !autonomousCustomArmy()) return nullptr;
    const Uint32 now = getGameCycleCount();
    const Uint32 window = MILLI2CYCLES(kAllyWaveJoinMs);
    // Our own wave that left inside the window is the one being joined, never a reason to join
    // again (two allies would otherwise drip minimum waves after each other).
    if (customWave.launched != 0 && ArmyPosturePolicy::elapsed(now, customWave.launched) <= window) return nullptr;
    const int myTeam = getHouse()->getTeamID();
    // House id order, then player order: the same answer on every peer.
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const House* house = getHouse(h);
        if (!house || house == getHouse() || house->isNatives() || !house->isAlive() || house->getTeamID() != myTeam)
            continue;
        for (const auto& player : house->getPlayerList()) {
            const auto* bot = dynamic_cast<const QuantBot*>(player.get());
            if (!bot || bot->customWave.launched == 0 || ArmyPosturePolicy::elapsed(now, bot->customWave.launched) > window)
                continue;
            const ObjectBase* front = getObject(bot->customWave.front);
            if (front && front->getHealth() > 0 && front->getOwner() && !front->getOwner()->isNatives()
                && front->getOwner()->getTeamID() != myTeam) return front;
        }
    }
    return nullptr;
}

bool QuantBot::teamCoordinationGame() const {
    if (!autonomousCustomArmy()) return false;
    const auto& init = currentGame->getGameInitSettings();
    if (init.getModName() != "vanilla" && init.getModName() != "dunecity") return false;
    int friends = 0, enemies = 0;
    for (const auto& house : init.getHouseInfoList()) {
        if (house.playerInfoList.empty()) continue;
        if (house.team == getHouse()->getTeamID()) ++friends;
        else ++enemies;
    }
    return friends >= 2 && enemies >= 1;
}

const QuantBot* QuantBot::teamAttackCoordinator() const {
    if (!teamCoordinationGame()) return nullptr;
    const QuantBot* coordinator = nullptr;
    int partners = 0;
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const auto* house = getHouse(h);
        if (!house || house->isNatives() || !house->isAlive()
            || house->getTeamID() != getHouse()->getTeamID()) continue;
        for (const auto& player : house->getPlayerList()) {
            const auto* bot = dynamic_cast<const QuantBot*>(player.get());
            if (!bot || !bot->autonomousCustomArmy()) continue;
            ++partners;
            if (!coordinator && bot->botConfig().force.teamSynchroniseAttacks) coordinator = bot;
            break; // One army per occupied house, not per co-controller.
        }
    }
    return partners >= 2 ? coordinator : nullptr;
}

bool QuantBot::teamAttackReady() const {
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const auto* house = getHouse(h);
        if (!house || house == getHouse() || house->isNatives() || !house->isAlive()
            || house->getTeamID() != getHouse()->getTeamID()) continue;
        for (const auto& player : house->getPlayerList()) {
            const auto* bot = dynamic_cast<const QuantBot*>(player.get());
            if (!bot || !bot->autonomousCustomArmy()) continue;
            // A partner already departing this joint wave does not make us wait for its
            // reinforcements. No orders are issued to the other controller here.
            if (bot->customWave.launched != 0
                && ArmyPosturePolicy::elapsed(getGameCycleCount(), bot->customWave.launched)
                    <= Uint32(MILLI2CYCLES(kAllyWaveJoinMs))) break;
            if (!bot->botConfig().getSettings(static_cast<int>(bot->difficulty)).attackEnabled
                || ArmyPosturePolicy::suppressesOffensiveDispatch(bot->armyPosture)) return false;
            // Match native dispatch readiness without incrementing another bot's telemetry
            // counter or treating its older hunters as a fresh force waiting to depart.
            int percent = bot->waveReadinessPercent();
            if (percent < 100 && (bot->openingReadinessPercent() >= 100
                || (bot->forceVariant(kForceStrengthReadiness)
                    && (bot->forceVariant(kForceEstimate)
                        ? Force::outnumbered(bot->teamGroundArmyValue(), bot->enemyThreatEstimate())
                        : bot->outnumberedByObservedArmy())))) percent = 100;
            const bool exhausted = bot->exhaustedArmyEconomy();
            const int64_t required = Force::scaledReadiness(bot->houseAttackThreshold(), percent);
            if (!exhausted && bot->militaryUnitValue() < required) return false;
            int availableUnits = 0;
            int64_t availableValue = 0;
            for (const auto* unit : getUnitList()) {
                if (unit->getOwner() != house || !groundFighter(unit) || !unit->isRespondable()
                    || unit->getAttackMode() == HUNT
                    || bot->unitRole(unit, {UnitRole::Human, UnitRole::Repair,
                        UnitRole::LiveDefence, UnitRole::Escort}) != UnitRole::Free) continue;
                const Uint32 id = unit->getObjectID();
                if (bot->styleRaiders.count(id) || bot->huntPartyOf(id)
                    || (!bot->splits.empty() && bot->splitOf(id))
                    || (!bot->musters.empty() && bot->musterOf(id))) continue;
                ++availableUnits;
                availableValue += std::max(100, currentGame->objectData.data[unit->getItemID()]
                    [unit->getProductionHouseID()].price);
            }
            if (exhausted) {
                if (availableUnits == 0) return false;
            } else {
                const auto& settings = bot->botConfig().getSettings(static_cast<int>(bot->difficulty));
                const FixPoint threshold = FixPoint(static_cast<int>(settings.attackThresholdPercent * 100)) / 100;
                const int64_t freshRequired = Force::scaledReadiness(
                    bot->reachableAttackThreshold((bot->militaryValueLimit * threshold).lround()), percent);
                if (availableUnits < bot->minWaveUnits()
                    || availableValue < std::max<int64_t>(bot->minWaveValue(), freshRequired)) return false;
            }
            if (!exhausted && !bot->fighterOnly() && !bot->lightFirstOpening() && house->getTechLevel() > 4
                && (bot->difficulty != Difficulty::Medium || bot->initialItemCount[Structure_RepairYard] > 0)
                && house->getNumItems(Structure_RepairYard) == 0) return false;
            break;
        }
    }
    return true;
}

const ObjectBase* QuantBot::teamAttackObjective() const {
    // Continue the first partner's just-launched objective so same-cycle losses cannot make
    // the second partner pick a different enemy. The anchor is already saved in customWave.
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const auto* house = getHouse(h);
        if (!house || house == getHouse() || house->isNatives()
            || house->getTeamID() != getHouse()->getTeamID()) continue;
        for (const auto& player : house->getPlayerList()) {
            const auto* bot = dynamic_cast<const QuantBot*>(player.get());
            if (!bot || !bot->customWave.launched
                || ArmyPosturePolicy::elapsed(getGameCycleCount(), bot->customWave.launched)
                    > Uint32(MILLI2CYCLES(kAllyWaveJoinMs))) continue;
            const auto* front = getObject(bot->customWave.front);
            if (front && front->getHealth() > 0 && front->getOwner()
                && front->getOwner()->getTeamID() != getHouse()->getTeamID()) return front;
        }
    }
    std::array<int64_t, NUM_HOUSES> armies{};
    for (const auto* unit : getUnitList())
        if (unit->getOwner() && !unit->getOwner()->isNatives() && unit->isActive()
            && unit->getHealth() > 0 && unit->canAttack() && mobileCombatItem(unit->getItemID()))
            armies[unit->getOwner()->getHouseID()] += healthValue(unit);
    const ObjectBase* objective = nullptr;
    int64_t weakest = std::numeric_limits<int64_t>::max();
    // Deterministic full-knowledge target: weakest living enemy with a building, then house
    // id, then structure id. Both partners derive the same answer regardless of their starts.
    int chosenHouse = NUM_HOUSES;
    for (const auto* building : getStructureList()) {
        const auto* owner = building->getOwner();
        if (!owner || owner->isNatives() || !owner->isAlive() || building->getHealth() <= 0
            || owner->getTeamID() == getHouse()->getTeamID()) continue;
        const int h = owner->getHouseID();
        if (!objective || armies[h] < weakest || (armies[h] == weakest && h < chosenHouse)
            || (h == chosenHouse && building->getObjectID() < objective->getObjectID())) {
            objective = building;
            weakest = armies[h];
            chosenHouse = h;
        }
    }
    return objective;
}

void QuantBot::wakeTeamAttackPartners() {
    // Wake just the allied controllers when the first real wave leaves, not while waiting.
    // attackTimer is existing saved simulation state; no unsaved coordination clock is added.
    for (int h = 0; h < NUM_HOUSES; ++h) {
        auto* house = getHouse(h);
        if (!house || house == getHouse() || house->isNatives() || !house->isAlive()
            || house->getTeamID() != getHouse()->getTeamID()) continue;
        for (const auto& player : house->getPlayerList()) {
            auto* bot = dynamic_cast<QuantBot*>(player.get());
            if (bot && bot->autonomousCustomArmy()
                && (!bot->customWave.launched
                    || ArmyPosturePolicy::elapsed(getGameCycleCount(), bot->customWave.launched)
                        > Uint32(MILLI2CYCLES(kAllyWaveJoinMs))))
                bot->attackTimer = 0;
        }
    }
}

bool QuantBot::outnumberedByEstimate() const {
    const Uint32 now = getGameCycleCount();
    if (estimateCycle != now) {
        estimateCycle = now;
        // Our team against the strongest enemy team: one house of an alliance is not outnumbered
        // by the whole enemy alliance while its allies stand beside it (3v3: own 30,500 against
        // 57,374 held every Hard house at home).
        estimateOutnumbered = Force::outnumbered(teamGroundArmyValue(), enemyThreatEstimate());
    }
    return estimateOutnumbered;
}

int64_t QuantBot::groundValueOf(const std::vector<Uint32>& ids) const {
    int64_t total = 0;
    for (const Uint32 id : ids)
        if (const auto* unit = dynamic_cast<const UnitBase*>(getObject(id)); unit && unit->getHealth() > 0)
            total += healthValue(unit);
    return total;
}

std::vector<Uint32> QuantBot::homeReserve(const std::vector<Uint32>& ids) const {
    std::vector<Uint32> kept;
    int share = Force::reserveShareBps(ownGroundArmyValue(), enemyThreatEstimate());
    if (botConfig().force.vanillaFfaReserve && vanillaMultiEnemyGame()) share = std::max(share, 2500);
    if (share <= 0) return kept;
    const Coord home = protectedRally.isValid() ? protectedRally : findBaseCentre(getHouse()->getHouseID());
    if (home.isInvalid()) return kept;
    struct Entry { Uint32 id; int distance; int64_t value; };
    std::vector<Entry> entries;
    int64_t total = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getHealth() <= 0) continue;
        const int64_t value = healthValue(unit);
        entries.push_back({id, tiles(unit->getLocation(), home), value});
        total += value;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    int64_t value = 0;
    for (const auto& entry : entries) {
        if (value * 10000 >= total * share) break;
        kept.push_back(entry.id);
        value += entry.value;
    }
    return kept;
}

bool QuantBot::standsInBundle(const UnitBase* unit) const {
    if (!unit) return false;
    constexpr int kBundledTiles = 8;
    const Coord at = unit->getLocation();
    return (bundlePoint.isValid() && tiles(at, bundlePoint) <= kBundledTiles)
        || (bundleCentre.isValid() && tiles(at, bundleCentre) <= kBundledTiles);
}

// ---- Defending at the base perimeter (round 5: U and V, on by default) -----------------------

const QuantBot::Perimeter& QuantBot::perimeter() const {
    const Uint32 now = getGameCycleCount();
    if (perimeterCycle == now) return perimeterCache;
    perimeterCycle = now;
    perimeterCache = Perimeter{};
    const Coord anchor = findBaseCentre(getHouse()->getHouseID());
    if (anchor.isInvalid()) return perimeterCache;
    const Coord forward = observedFrontDirection(anchor);
    const int length = int(Force::isqrt(int64_t(forward.x) * forward.x + int64_t(forward.y) * forward.y));
    if (length <= 0) return perimeterCache;
    // The outermost own building of this base along the front (structure list order; the
    // maximum does not depend on it).
    int depth = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || tiles(structure->getLocation(), anchor) > Force::kPerimeterBaseTiles) continue;
        const Coord size = getStructureSize(structure->getItemID());
        for (const Coord corner : {structure->getLocation(), structure->getLocation() + size - Coord(1, 1)}) {
            const int along = int((int64_t(corner.x - anchor.x) * forward.x + int64_t(corner.y - anchor.y) * forward.y)
                                  / length);
            depth = std::max(depth, along);
        }
    }
    perimeterCache.anchor = anchor;
    perimeterCache.forward = forward;
    perimeterCache.length = length;
    perimeterCache.depth = depth;
    const int reach = depth + Force::kPerimeterStandoffTiles;
    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom() && !onPlannedSite(p);
    };
    const Coord desired(anchor.x + int(int64_t(forward.x) * reach / length), anchor.y + int(int64_t(forward.y) * reach / length));
    for (int ring = 0; ring <= 6 && perimeterCache.point.isInvalid(); ++ring)
        for (int dy = -ring; dy <= ring && perimeterCache.point.isInvalid(); ++dy)
            for (int dx = -ring; dx <= ring && perimeterCache.point.isInvalid(); ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == ring && usable(desired + Coord(dx, dy)))
                    perimeterCache.point = desired + Coord(dx, dy);
    // A city spreads its lots and roads well beyond kPerimeterBaseTiles, and that point stood in the
    // middle of the house's own streets: then walk on along the front to the first point clear of
    // our buildings and roads (a compact base's point already is, and stays where it was).
    constexpr int clear = Force::kPerimeterClearTiles;
    if (perimeterCache.point.isInvalid() || builtUpDistance(perimeterCache.point, clear) < clear) {
        const Coord out = clearOfOwnBase(anchor, forward, length, reach, reach + Force::kPerimeterMaxExtraTiles, 1,
            [&](Coord p) { return usable(p) && builtUpDistance(p, clear) >= clear; });
        if (out.isValid()) perimeterCache.point = out;
    }
    return perimeterCache;
}

int QuantBot::builtUpDistance(Coord p, int limit) const {
    for (int ring = 0; ring < limit; ++ring)
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                const Coord q = p + Coord(dx, dy);
                if (!getMap().tileExists(q)) continue;
                const auto* tile = getMap().getTile(q);
                if (tile->isRoad()) return ring;
                if (tile->hasAStructure()) {
                    const auto* building = tile->getGroundObject();
                    if (building && building->getOwner() == getHouse()) return ring;
                }
            }
    return limit;
}

int QuantBot::perimeterTurretGoal() const {
    if (!forceVariant(kForceThreatTurrets)) return 0;
    return Force::perimeterTurretGoal(teamGroundArmyValue(), enemyThreatEstimate());
}

int QuantBot::perimeterTurrets() const {
    const Perimeter& p = perimeter();
    if (p.length <= 0) return 0;
    int count = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || structure->getItemID() != Structure_RocketTurret) continue;
        const int dx = structure->getLocation().x - p.anchor.x, dy = structure->getLocation().y - p.anchor.y;
        const int along = int((int64_t(dx) * p.forward.x + int64_t(dy) * p.forward.y) / p.length);
        const int across = int((int64_t(dy) * p.forward.x - int64_t(dx) * p.forward.y) / p.length);
        if (Force::onPerimeterBand(along, across, p.depth)) ++count;
    }
    return count;
}

bool QuantBot::combatTargetTuningActive() const {
    const auto& settings = botConfig().force;
    if (builderOnly() || (!effectiveFocusFireLevel() && !settings.targetPriorityLevel) || !currentGame) return false;
    return currentGame->getGameInitSettings().getInitialSpiceTilePermille() > 0;
}

const QuantBotConfig::ForceSettings& QuantBot::closingForce() const {
    const auto& force = botConfig().force;
    if (!force.closingMountainScope || !currentGame
        || !QuantBotMapScopePolicy::mountainRich(force.closingMountainScope,
                                                 currentGame->getGameInitSettings().getInitialTerrain()))
        return force;
    static const QuantBotConfig native;
    return native.force;
}

bool QuantBot::convergeChokepointNative() const {
    if (!botConfig().force.convergeChokepointScope || !currentGame) return false;
    const auto& init = currentGame->getGameInitSettings();
    int controllers = 0;
    for (const auto& house : init.getHouseInfoList())
        if (!house.playerInfoList.empty()) ++controllers;
    return QuantBotMapScopePolicy::smallMountainDuel(init.getInitialTerrain(), controllers);
}

bool QuantBot::openingOpenMapNative() const {
    if (!botConfig().force.openingOpenMapScope || !currentGame) return false;
    const auto& init = currentGame->getGameInitSettings();
    int controllers = 0;
    for (const auto& house : init.getHouseInfoList())
        if (!house.playerInfoList.empty()) ++controllers;
    return QuantBotMapScopePolicy::openFlatCrowd(init.getInitialTerrain(), controllers);
}

int QuantBot::effectiveFocusFireLevel() const {
    const auto& force = botConfig().force;
    if (!force.focusFireLevel || !force.focusFireMapScope || !currentGame) return force.focusFireLevel;
    const auto& init = currentGame->getGameInitSettings();
    if (force.focusFireMapScope >= 3) { // Levels 3-5: the original roster (5: or map level 1).
        const int sides = QuantBotMapScopePolicy::opposingSides(init.getHouseInfoList(), getHouse()->getTeamID());
        const bool native = force.focusFireMapScope == 5
            ? QuantBotMapScopePolicy::focusFireLevel5(init.getInitialTerrain(), sides)
            : QuantBotMapScopePolicy::manySides(force.focusFireMapScope, sides);
        return native ? 0 : force.focusFireLevel;
    }
    // Original occupied controller rows, not surviving Houses.
    int controllers = 0;
    for (const auto& house : init.getHouseInfoList())
        if (!house.playerInfoList.empty()) ++controllers;
    return QuantBotMapScopePolicy::hugeOrCrowded(force.focusFireMapScope, init.getInitialTerrain(), controllers)
        ? 0 : force.focusFireLevel;
}

bool QuantBot::armyCountersMapExcluded() const {
    const int level = botConfig().force.vanillaArmyCounterMapScope;
    if (!level || !currentGame) return false;
    const auto& init = currentGame->getGameInitSettings();
    if (level >= 3) { // Levels 3-5: the original roster (5: or map level 1).
        const int sides = QuantBotMapScopePolicy::opposingSides(init.getHouseInfoList(), getHouse()->getTeamID());
        return level == 5 ? QuantBotMapScopePolicy::armyCountersLevel5(init.getInitialTerrain(), sides)
                          : QuantBotMapScopePolicy::manySides(level, sides);
    }
    return QuantBotMapScopePolicy::thinOrFragmented(level, init.getInitialTerrain());
}

bool QuantBot::focusFire(const UnitBase* unit, const std::vector<const ObjectBase*>& enemies) {
    const int myTeam = getHouse()->getTeamID();
    const ObjectBase* current = unit->getTarget();
    if (current && (current->getHealth() <= 0 || !current->getOwner() || current->getOwner()->getTeamID() == myTeam))
        current = nullptr;
    const Coord at = unit->getLocation();
    // Tile pre-filter; isInWeaponRange() decides.
    const int reach = unit->getWeaponRange() + 3;
    const ObjectBase* best = nullptr;
    Force::TargetKey bestKey;
    const auto& settings = botConfig().force;
    const bool tuning = combatTargetTuningActive();
    const int focusLevel = tuning ? effectiveFocusFireLevel() : 0;
    const int priorityLevel = tuning ? settings.targetPriorityLevel : 0;
    const auto keyOf = [&](const ObjectBase* object) {
        auto key = targetKeyOf(object);
        if (!tuning) return key;
        const int item = object->getItemID();
        const bool artillery = item == Unit_Launcher || item == Unit_EliteLauncher
            || item == Unit_SiegeTank || item == Unit_Trooper || item == Unit_Troopers;
        key.cls = Force::tunedTargetClass(key.cls, focusLevel, priorityLevel,
            artillery, item == Unit_Harvester || item == Unit_RebelHarvester);
        return key;
    };
    for (const auto* enemy : enemies) {
        if (tiles(at, enemy->getClosestPoint(at)) > reach) continue;
        if (!unit->canAttack(enemy) || !unit->isInWeaponRange(enemy)) continue;
        const auto key = keyOf(enemy);
        if (key.cls == Force::Wall) continue;
        if (!best || Force::betterTarget(key, bestKey)) { best = enemy; bestKey = key; }
    }
    if (!best) return false;
    if (current && !Force::tunedShouldSwitch(unit->isInWeaponRange(current), keyOf(current), bestKey,
                                          focusLevel)) return false;
    doAttackObject(unit, best, false);
    ++forceStats.focusSwitches;
    return true;
}

void QuantBot::focusFireNoOverkill(const std::vector<const UnitBase*>& hunters, const std::vector<const ObjectBase*>& enemies) {
    const int myTeam = getHouse()->getTeamID();
    auto hostile = [&](const ObjectBase* target) {
        return target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam;
    };
    // One volley of everyone already shooting at a target in reach (NOK-AV's assigned damage).
    std::map<Uint32, int64_t> assigned;
    for (const auto* unit : hunters) {
        const auto* target = unit->getTarget();
        if (hostile(target) && unit->isInWeaponRange(target)) assigned[target->getObjectID()] += volleyOf(unit);
    }
    // Object-id order: the same assignment on every peer whatever the unit list order.
    for (const auto* unit : hunters) {
        const ObjectBase* current = hostile(unit->getTarget()) ? unit->getTarget() : nullptr;
        const bool currentInRange = current && unit->isInWeaponRange(current);
        const int64_t mine = currentInRange ? volleyOf(unit) : 0;
        const Coord at = unit->getLocation();
        const int reach = unit->getWeaponRange() + 3;
        const ObjectBase* best = nullptr;
        Force::ValueKey bestKey;
        for (const auto* enemy : enemies) {
            if (enemy == current || tiles(at, enemy->getClosestPoint(at)) > reach) continue;
            if (!unit->canAttack(enemy) || !unit->isInWeaponRange(enemy)) continue;
            const auto key = valueKeyOf(enemy);
            if (key.cls == Force::Wall) continue;
            const auto found = assigned.find(key.id);
            if (found != assigned.end() && Force::overkilled(found->second, key.health)) continue;
            if (!best || Force::betterValue(key, bestKey)) { best = enemy; bestKey = key; }
        }
        if (!best) continue;
        if (current) {
            const auto currentKey = valueKeyOf(current);
            const bool overkill = Force::overkilled(assigned[current->getObjectID()] - mine, currentKey.health);
            if (!Force::shouldSwitchValue(currentInRange, overkill, currentKey, bestKey)) continue;
            assigned[current->getObjectID()] -= mine;
        }
        assigned[best->getObjectID()] += volleyOf(unit);
        doAttackObject(unit, best, false);
        ++forceStats.focusSwitches;
    }
}

void QuantBot::concentrateForces() {
    const bool tactical = combatTargetTuningActive();
    const bool noOverkill = forceVariant(kForceNoOverkill);
    const bool focus = forceVariant(kForceFocusFire) && !noOverkill, convergeOn = forceVariant(kForceConverge);
    const bool turretsFirst = forceVariant(kForceTurretPriority), sonicLanes = forceVariant(kForceSonicClearLane);
    if (forceVariant(kForceMuster) || !musters.empty()) updateMusters();
    if (!splits.empty()) updateSplits();
    // O (siege standoff) first: what it orders or holds takes no other targeting rule this pass.
    std::set<Uint32> sieging;
    if (forceVariant(kForceSiegeStandoff)) siegeStandoff(sieging);
    else siegeHeld.clear();
    if (!tactical && !focus && !convergeOn && !noOverkill && !turretsFirst && !sonicLanes) return;
    AITelemetry::PerformanceScope perfScope("ai.concentrateForces", getGameCycleCount(), getHouse()->getHouseID());
    const int myTeam = getHouse()->getTeamID();
    // Visible enemy ground units and buildings, in list order.
    std::vector<const ObjectBase*> enemies;
    for (const auto* unit : getUnitList())
        if (unit->getOwner() && unit->getOwner()->getTeamID() != myTeam && unit->getHealth() > 0 && unit->isActive()
            && !unit->isAFlyingUnit() && unit->getItemID() != Unit_Sandworm && unit->isVisible(myTeam))
            enemies.push_back(unit);
    for (const auto* structure : getStructureList())
        if (structure->getOwner() && structure->getOwner()->getTeamID() != myTeam && structure->getHealth() > 0
            && structure->isVisible(myTeam))
            enemies.push_back(structure);
    if (enemies.empty()) return;
    // Their turrets (armed buildings), for T.
    std::vector<const ObjectBase*> turrets;
    if (turretsFirst)
        for (const auto* enemy : enemies)
            if (enemy->isAStructure() && enemy->canAttack()) turrets.push_back(enemy);
    // Our team's ground units a sonic wave would hurt (every one but the sonic tanks), for S.
    std::vector<const UnitBase*> friends;
    if (sonicLanes)
        for (const auto* unit : getUnitList())
            if (unit->getOwner() && unit->getOwner()->getTeamID() == myTeam && unit->getHealth() > 0 && unit->isActive()
                && unit->isAGroundUnit() && unit->getItemID() != Unit_SonicTank)
                friends.push_back(unit);
    // Our hunters: the wave members and the base defenders (engine Hunt, not forced).
    auto hunter = [&](const UnitBase* unit) {
        return unit->getOwner() == getHouse() && unit->getHealth() > 0 && unit->isActive() && unit->isRespondable()
            && unit->isAGroundUnit() && unit->canAttack() && mobileCombatItem(unit->getItemID())
            && unit->getItemID() != Unit_Deviator && unit->getAttackMode() == HUNT && !unit->wasForced()
            && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free;
    };
    // The fights: where a hunter of ours is shooting at an armed enemy in its reach. Taken
    // before any order of this pass, so no unit's decision depends on another's.
    std::vector<Coord> fights;
    if (convergeOn) {
        for (const auto* unit : getUnitList()) {
            if (!hunter(unit)) continue;
            const auto* target = unit->getTarget();
            if (target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam
                && armed(target) && unit->isInWeaponRange(target)) fights.push_back(target->getLocation());
        }
    }
    // A hunting launcher with an enemy already inside its close band belongs to the escape in
    // the unit pass (escapeCloseThreatWhileHunting): an attack or hunt-toward order here would
    // clear its spacing step. Once it stands off again it is an ordinary hunter and takes the
    // focus target like everyone else.
    auto escaping = [&](const UnitBase* unit) { return closeThreatWhileHunting(unit) != nullptr; };
    if (noOverkill) {
        std::vector<const UnitBase*> hunters;
        for (const auto* unit : getUnitList()) if (hunter(unit) && !escaping(unit)) hunters.push_back(unit);
        std::sort(hunters.begin(), hunters.end(),
            [](const UnitBase* a, const UnitBase* b) { return a->getObjectID() < b->getObjectID(); });
        focusFireNoOverkill(hunters, enemies);
    }
    for (const auto* unit : getUnitList()) {
        // The experiment extends the existing focus policy to campaign/support and
        // guarding ground fighters. Never take manual, repair, retreat or STOP units.
        const bool tacticalFighter = tactical && unit->getOwner() == getHouse()
            && unit->getHealth() > 0 && unit->isActive() && unit->isRespondable()
            && unit->isAGroundUnit() && unit->canAttack() && mobileCombatItem(unit->getItemID())
            && unit->getItemID() != Unit_Deviator && !unit->wasForced()
            && (unit->getAttackMode() == HUNT || unit->getAttackMode() == GUARD || unit->getAttackMode() == AREAGUARD)
            && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free;
        const bool nativeHunter = hunter(unit);
        if ((!tacticalFighter && !nativeHunter) || escaping(unit) || sieging.count(unit->getObjectID())) continue;
        if (sonicLanes && unit->getItemID() == Unit_SonicTank && sonicClearTarget(unit, enemies, friends)) continue;
        if (!turrets.empty() && unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave && attackTurretInReach(unit, turrets))
            continue;
        if ((focus || tacticalFighter) && focusFire(unit, enemies)) continue;
        if (!nativeHunter) continue;
        if (fights.empty() || unitRole(unit, {UnitRole::Wave}) != UnitRole::Wave || unit->formationContact()) continue;
        // Not fighting: the nearest fight of our army (ties to the first in list order, which
        // is the same on every peer).
        const Coord at = unit->getLocation();
        int fightDistance = -1;
        Coord fight = Coord::Invalid();
        for (const Coord& point : fights) {
            const int distance = tiles(at, point);
            if (fightDistance < 0 || distance < fightDistance) { fightDistance = distance; fight = point; }
        }
        const auto* target = unit->getTarget();
        const int targetDistance = target && target->getHealth() > 0 ? tiles(at, target->getClosestPoint(at)) : -1;
        if (!Force::converge(fightDistance, targetDistance)) continue;
        if (unit->isHuntingToward() && tiles(unit->getDestination(), fight) <= Force::kConvergeSameGoalTiles) continue;
        doHuntToward(unit, fight.x, fight.y);
        ++forceStats.converges;
    }
}

bool QuantBot::attackTurretInReach(const UnitBase* unit, const std::vector<const ObjectBase*>& turrets) {
    // Units first (focus fire): a hunter already shooting an armed unit in its reach keeps it, and
    // one already on a turret keeps that turret.
    const int myTeam = getHouse()->getTeamID();
    const ObjectBase* current = unit->getTarget();
    if (current && current->getHealth() > 0 && current->getOwner() && current->getOwner()->getTeamID() != myTeam
        && armed(current) && (current->isAStructure() || unit->isInWeaponRange(current))) return false;
    // The nearest turret that has this unit in its reach, or that the unit reaches (ties: list order).
    const Coord at = unit->getLocation();
    const ObjectBase* best = nullptr;
    int bestDistance = 0;
    for (const auto* turret : turrets) {
        const int distance = tiles(at, turret->getClosestPoint(at));
        if (distance > std::max(turret->getWeaponRange() + 1, unit->getWeaponRange())) continue;
        if (best && distance >= bestDistance) continue;
        if (!unit->canAttack(turret)) continue;
        best = turret; bestDistance = distance;
    }
    if (!best) return false;
    doAttackObject(unit, best, false);
    ++forceStats.turretTargets;
    return true;
}

bool QuantBot::sonicClearTarget(const UnitBase* unit, const std::vector<const ObjectBase*>& enemies,
                                const std::vector<const UnitBase*>& friends) {
    const Coord at = unit->getLocation();
    // Our units the wave could reach at all (list order).
    std::vector<Coord> nearbyFriends;
    for (const auto* other : friends)
        if (other != unit && tiles(at, other->getLocation()) <= Force::kSonicReachTiles + 1) nearbyFriends.push_back(other->getLocation());
    auto clear = [&](const ObjectBase* target) {
        const Coord aim = target->getClosestPoint(at);
        for (const Coord& other : nearbyFriends)
            if (Force::inSonicPath(aim.x - at.x, aim.y - at.y, other.x - at.x, other.y - at.y)) return false;
        return true;
    };
    const int myTeam = getHouse()->getTeamID();
    const ObjectBase* current = unit->getTarget();
    if (current && current->getHealth() > 0 && current->getOwner() && current->getOwner()->getTeamID() != myTeam
        && unit->isInWeaponRange(current) && clear(current)) return true;
    // The nearest enemy in reach with a clear lane (ties: list order).
    const ObjectBase* best = nullptr;
    int bestDistance = 0;
    for (const auto* enemy : enemies) {
        if (enemy == current || !unit->isInWeaponRange(enemy) || !unit->canAttack(enemy)) continue;
        const int distance = tiles(at, enemy->getClosestPoint(at));
        if (best && distance >= bestDistance) continue;
        if (!clear(enemy)) continue;
        best = enemy; bestDistance = distance;
    }
    if (!best) return false;
    doAttackObject(unit, best, false);
    ++forceStats.sonicRetargets;
    return true;
}

void QuantBot::siegeStandoff(std::set<Uint32>& handled) {
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    // Known enemy turrets (explored, not necessarily in sight: buildings do not move), list order.
    std::vector<const StructureBase*> turrets;
    for (const auto* structure : getStructureList())
        if (structure->canAttack() && knownEnemyStructure(structure)) turrets.push_back(structure);
    // The nearest of them to \a at within \a reach(turret) tiles, or nullptr (ties: list order).
    auto nearestTurret = [&](Coord at, auto reach) {
        const StructureBase* best = nullptr;
        int bestDistance = 0;
        for (const auto* turret : turrets) {
            const int distance = tiles(at, turret->getClosestPoint(at));
            if (distance > reach(turret) || (best && distance >= bestDistance)) continue;
            best = turret; bestDistance = distance;
        }
        return best;
    };
    auto waveMember = [&](const UnitBase* unit) {
        return unit->getOwner() == getHouse() && unit->getHealth() > 0 && unit->isActive() && unit->isRespondable()
            && unit->isAGroundUnit() && unit->canAttack() && mobileCombatItem(unit->getItemID())
            && unit->getItemID() != Unit_Deviator && !unit->wasForced()
            && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free
            && unitRole(unit, {UnitRole::Wave}) == UnitRole::Wave;
    };
    // Shooting an armed enemy unit in its reach: such a unit keeps its fight.
    auto fighting = [&](const UnitBase* unit) {
        const ObjectBase* target = unit->getTarget();
        return target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam
            && !target->isAStructure() && armed(target) && unit->isInWeaponRange(target);
    };
    // 1. Launchers shell the nearest known turret within their reach plus kSiegeReachTiles.
    std::vector<Coord> launchers;
    if (!turrets.empty())
        for (const auto* unit : getUnitList()) {
            if (unit->getItemID() != Unit_Launcher || !waveMember(unit) || unit->getAttackMode() != HUNT) continue;
            if (closeThreatWhileHunting(unit) != nullptr || fighting(unit)) continue;
            const int range = unit->getWeaponRange();
            const auto* turret = nearestTurret(unit->getLocation(),
                [&](const StructureBase*) { return range + Force::kSiegeReachTiles; });
            if (!turret || !unit->canAttack(turret)) continue;
            launchers.push_back(unit->getLocation());
            handled.insert(unit->getObjectID());
            if (unit->getTarget() == turret) continue;
            doAttackObject(unit, turret, false);
            ++forceStats.siegeOrders;
        }
    auto launchersNear = [&](Coord at) {
        int n = 0;
        for (const Coord& launcher : launchers) if (tiles(at, launcher) <= Force::kSiegeEscortTiles) ++n;
        return n;
    };
    auto holdReach = [](const StructureBase* turret) { return turret->getWeaponRange() + Force::kSiegeHoldMarginTiles; };
    // 2. Held escorts: released (back to engine Hunt) when the hold is over.
    for (auto it = siegeHeld.begin(); it != siegeHeld.end();) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(it->first));
        if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isActive()) {
            it = siegeHeld.erase(it);
            continue;
        }
        const bool over = ArmyPosturePolicy::elapsed(now, it->second) >= Uint32(MILLI2CYCLES(Force::kSiegeMaxHoldMs))
            || launchersNear(unit->getLocation()) < Force::kSiegeMinLaunchers
            || !nearestTurret(unit->getLocation(), [&](const StructureBase* t) { return holdReach(t) + 1; });
        if (over) {
            if (unit->isRespondable() && unit->getAttackMode() == GUARD) doSetAttackMode(unit, HUNT);
            it = siegeHeld.erase(it);
            continue;
        }
        handled.insert(it->first);
        ++it;
    }
    if (launchers.size() < size_t(Force::kSiegeMinLaunchers)) return;
    // 3. Hunting wave members about to walk into a turret's reach while launchers are near them
    //    guard a point kSiegeHoldMarginTiles outside that reach, on their side of the turret.
    for (const auto* unit : getUnitList()) {
        if (unit->getItemID() == Unit_Launcher || !waveMember(unit) || unit->getAttackMode() != HUNT) continue;
        if (handled.count(unit->getObjectID()) || siegeHeld.count(unit->getObjectID()) || fighting(unit)) continue;
        const Coord at = unit->getLocation();
        const auto* turret = nearestTurret(at, holdReach);
        if (!turret || launchersNear(at) < Force::kSiegeMinLaunchers) continue;
        const Coord edge = turret->getClosestPoint(at);
        const auto offset = BlobWavePolicy::scaled(at.x - edge.x, at.y - edge.y, holdReach(turret));
        Coord point = edge + Coord(offset.first, offset.second);
        if (!getMap().tileExists(point) || !unit->canPass(point.x, point.y)) point = at;
        doSetAttackMode(unit, GUARD);
        doMove2Pos(unit, point.x, point.y, false);
        siegeHeld.emplace(unit->getObjectID(), std::max<Uint32>(1, now));
        handled.insert(unit->getObjectID());
        ++forceStats.siegeHolds;
    }
}

bool QuantBot::knownEnemyStructure(const StructureBase* structure) const {
    if (!structure->getOwner() || structure->getOwner()->getTeamID() == getHouse()->getTeamID()
        || structure->getHealth() <= 0) return false;
    switch (structure->getItemID()) {
        case Structure_Wall: case Structure_Slab1: case Structure_Slab4: case Structure_Road: case Structure_PowerLine:
            return false;
        default: break;
    }
    const Coord at = structure->getLocation();
    return getMap().tileExists(at) && (vanillaReactiveKnowledge()
        || cityBoomSpiceKind() == CityBoomKind::Reactive
        || getMap().getTile(at)->isExploredByTeam(getHouse()->getTeamID()));
}

int QuantBot::finishTargetTeam(const UnitBase* lead) const {
    if (customWave.launched == 0 && vanillaReactiveKnowledge()) {
        QuantBotVanillaPolicy::Observation receipt{};
        int firstTeam = -1;
        vanillaReactiveSurvey(receipt, &firstTeam, lead);
        return firstTeam;
    }
    if (cityBoomSpiceKind() == CityBoomKind::Reactive) return cityBoomSurvey().targetTeam;
    // Ordered by team id (std::map), scanned in list order: the same choice on every peer.
    std::map<int, Force::TeamStrength> teams;
    const bool nearby = botConfig().force.vanillaFfaNearbyTarget && vanillaMultiEnemyGame();
    const Coord home = nearby ? findBaseCentre(getHouse()->getHouseID()) : Coord::Invalid();
    for (const auto* structure : getStructureList()) {
        if (!knownEnemyStructure(structure)) continue;
        auto& team = teams[structure->getOwner()->getTeamID()];
        team.team = structure->getOwner()->getTeamID();
        ++team.structures;
        if (structure->canAttack()) ++team.turrets;
        if (home.isValid()) {
            const int distance = tiles(home, structure->getLocation());
            team.distance = team.structures == 1 ? distance : std::min(team.distance, distance);
        }
    }
    for (const auto& [id, sighting] : enemySightings)
        if (const auto* unit = getObject(id); unit && unit->getOwner()) {
            const auto found = teams.find(unit->getOwner()->getTeamID());
            if (found != teams.end()) found->second.army += sighting.value;
        }
    const Force::TeamStrength* weakest = nullptr;
    for (const auto& [id, team] : teams) {
        const bool prefer = weakest
            && (home.isValid() ? Force::nearerOpportunity(team, *weakest) : Force::weakerTeam(team, *weakest));
        if (!weakest || prefer) weakest = &team;
    }
    return weakest ? weakest->team : -1;
}

const ObjectBase* QuantBot::finishObjective(const UnitBase* lead, int team) const {
    if (!lead || team < 0) return nullptr;
    const ObjectBase* chosen = nullptr;
    int best = std::numeric_limits<int>::max();
    for (const auto* structure : getStructureList()) {
        if (!knownEnemyStructure(structure) || structure->getOwner()->getTeamID() != team) continue;
        int score = tiles(lead->getLocation(), structure->getLocation());
        switch (structure->getItemID()) {
            case Structure_ConstructionYard: case Structure_HeavyFactory: case Structure_LightFactory:
            case Structure_HighTechFactory: case Structure_Refinery:
                score -= Force::kFinishPriorityTiles; break;
            default: break;
        }
        if (score >= best || !groundAttackReachable(lead, structure)) continue;
        best = score; chosen = structure;
    }
    return chosen;
}

const StructureBase* QuantBot::pressOnTarget(const UnitBase* unit, const std::vector<const StructureBase*>& known,
                                             int team) const {
    const Coord at = unit->getLocation();
    const StructureBase* best = nullptr;
    int bestDistance = 0;
    for (const auto* structure : known) {
        if (team >= 0 && structure->getOwner()->getTeamID() != team) continue;
        const int distance = tiles(at, structure->getClosestPoint(at));
        if (best && distance >= bestDistance) continue;
        if (!groundAttackReachable(unit, structure)) continue;
        best = structure; bestDistance = distance;
    }
    return best;
}

int QuantBot::knownTurretsNear(const ObjectBase* objective) const {
    if (!objective) return 0;
    const Coord at = objective->getLocation();
    int count = 0;
    for (const auto* structure : getStructureList())
        if (structure->canAttack() && knownEnemyStructure(structure)
            && tiles(at, structure->getLocation()) <= Force::kFortifiedRadiusTiles) ++count;
    return count;
}

const QuantBot::Muster* QuantBot::musterOf(Uint32 id) const {
    for (const auto& muster : musters) if (muster.members.count(id)) return &muster;
    return nullptr;
}

void QuantBot::startMuster(const ObjectBase* building, const std::vector<Uint32>& ids) {
    if (!building || ids.empty()) return;
    Muster* muster = nullptr;
    for (auto& existing : musters) if (existing.building == building->getObjectID()) muster = &existing;
    const UnitBase* anchor = nullptr;
    int64_t sx = 0, sy = 0, n = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        sx += unit->getLocation().x; sy += unit->getLocation().y; ++n;
        if (!anchor || (anchor->isInfantry() && !unit->isInfantry())) anchor = unit;
    }
    if (!anchor) return;
    if (!muster) {
        // On the defenders' side of the building, kMusterStandoffTiles from it, out of reach.
        const Coord from(int(sx / n), int(sy / n));
        const Coord edge = building->getClosestPoint(from);
        const auto offset = BlobWavePolicy::scaled(from.x - edge.x, from.y - edge.y, Force::kMusterStandoffTiles);
        Muster created;
        created.building = building->getObjectID();
        created.since = getGameCycleCount();
        created.point = huntPartyPoint(edge + Coord(offset.first, offset.second), anchor, true);
        musters.push_back(created);
        muster = &musters.back();
        ++forceStats.musters;
    }
    const int radius = BlobWavePolicy::blobRadius(int(muster->members.size() + ids.size())) + 1;
    std::set<int64_t> reserved;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        muster->members.insert(id);
        groundSquad.erase(id);
        Coord slot = huntPartySlot(unit, muster->point, radius, reserved);
        if (slot.isInvalid()) slot = muster->point;
        doSetAttackMode(unit, AREAGUARD);
        doMove2Pos(unit, slot.x, slot.y, false);
    }
    traceDecision("defence_muster", AITelemetry::Record().set("building", building->getObjectID())
        .set("x", muster->point.x).set("y", muster->point.y).set("members", int(muster->members.size())));
}

void QuantBot::updateMusters() {
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    for (auto& muster : musters) {
        for (auto it = muster.members.begin(); it != muster.members.end();) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(*it));
            const bool keep = unit && unit->getOwner() == getHouse() && unit->getHealth() > 0
                && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free;
            it = keep ? std::next(it) : muster.members.erase(it);
        }
        const auto* building = getObject(muster.building);
        // Is the attack still on? Any visible enemy fighter near the building.
        bool contested = false;
        if (building && building->getHealth() > 0)
            for (const auto* enemy : getUnitList())
                if (enemy->getOwner() && enemy->getOwner()->getTeamID() != myTeam && enemy->getHealth() > 0
                    && enemy->isActive() && enemy->canAttack() && !enemy->isAFlyingUnit() && enemy->isVisible(myTeam)
                    && tiles(enemy->getLocation(), building->getClosestPoint(enemy->getLocation())) <= Force::kPoolThreatTiles) {
                    contested = true; break;
                }
        const int members = int(muster.members.size());
        const int radius = BlobWavePolicy::blobRadius(members) + 1;
        int there = 0;
        bool contact = false;
        for (const Uint32 id : muster.members) {
            const auto* unit = static_cast<const UnitBase*>(getObject(id));
            if (tiles(unit->getLocation(), muster.point) <= radius) ++there;
            contact = contact || unit->formationContact() != nullptr;
        }
        const Uint32 elapsed = ArmyPosturePolicy::elapsed(now, muster.since);
        const bool quorum = int64_t(there) * 10000 >= int64_t(Force::kMusterQuorumBps) * std::max(1, members);
        if (members > 0 && contested && !quorum && !contact && elapsed < Uint32(MILLI2CYCLES(Force::kMusterMaxMs))) {
            // Re-send whoever lost its walk.
            std::set<int64_t> reserved;
            for (const Uint32 id : muster.members) {
                const auto* unit = static_cast<const UnitBase*>(getObject(id));
                if (!unit->isActive() || !unit->isRespondable() || tiles(unit->getLocation(), muster.point) <= radius
                    || unit->isMoving() || unit->hasPendingPathRequest() || unit->hasATarget()) continue;
                Coord slot = huntPartySlot(unit, muster.point, radius, reserved);
                if (slot.isInvalid()) slot = muster.point;
                doSetAttackMode(unit, AREAGUARD);
                doMove2Pos(unit, slot.x, slot.y, false);
            }
            continue;
        }
        // Engage together: every member hunts its way to the building, as a scrambled defender.
        for (const Uint32 id : muster.members) {
            const auto* unit = static_cast<const UnitBase*>(getObject(id));
            if (!unit->isActive() || !unit->isRespondable()) continue;
            if (contested && building) {
                const Coord to = building->getClosestPoint(unit->getLocation());
                doHuntToward(unit, to.x, to.y);
                defenceAssignments[id] = muster.building;
            } else doSetAttackMode(unit, HUNT);
        }
        ++forceStats.musterReleases;
        traceDecision("defence_muster_release", AITelemetry::Record().set("building", muster.building)
            .set("members", members).set("there", there).set("contact", contact).set("contested", contested)
            .set("elapsed_cycles", int(elapsed)));
        muster.members.clear();
    }
    musters.erase(std::remove_if(musters.begin(), musters.end(),
        [](const Muster& muster) { return muster.members.empty(); }), musters.end());
}

// ---- Bundling the idle reserve (kForceBundle, on by default) ---------------------------------
//
// Before 1.0.602 the idle army stood at a forward rally (75% own base + 25% enemy base) or at
// its own centre of mass, so a wave left as one body. Bundling brings that back with the
// observed enemy base only, a bounded tile search every 30 s and at most kBundleOrdersPerPass
// orders per pass, farthest first; bundled troops keep no role, so base defence takes them like
// any other. The point, its clock and the launch gate's wait are saved (9863).

Coord QuantBot::forwardBundlePoint(int clearance) {
    const Uint32 now = getGameCycleCount();
    const bool unsafe = bundlePoint.isValid() && (!getMap().tileExists(bundlePoint) || dangerAt(bundlePoint) != 0);
    if (!unsafe && bundleCycle != std::numeric_limits<Uint32>::max() && now >= bundleCycle
        && now - bundleCycle < Uint32(MILLI2CYCLES(30000))) return bundlePoint;
    bundleCycle = now;
    const Coord home = armyAssemblyAnchor();
    if (home.isInvalid()) { bundlePoint = Coord::Invalid(); return bundlePoint; }
    const Coord towards = observedFrontDirection(home);
    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom() && dangerAt(p) == 0
            && !onPlannedSite(p);
    };
    // A quarter of the way (the nearest usable tile to it) ...
    const Coord desired(Force::along(home.x, home.x + towards.x, Force::kBundleForwardPercent),
                        Force::along(home.y, home.y + towards.y, Force::kBundleForwardPercent));
    Coord quarter = Coord::Invalid();
    for (int ring = 0; ring <= 8 && quarter.isInvalid(); ++ring)
        for (int dy = -ring; dy <= ring && quarter.isInvalid(); ++dy)
            for (int dx = -ring; dx <= ring && quarter.isInvalid(); ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == ring && usable(desired + Coord(dx, dy)))
                    quarter = desired + Coord(dx, dy);
    // ... unless that is among our own lots and roads (a city base, where the reserve stood packed
    // in its own streets): then the first point further towards the enemy, at most half way, that
    // the whole bundle fits beside without entering the base. Where none is, the quarter point.
    int forwardPercent = Force::kBundleForwardPercent;
    bundlePoint = quarter;
    if (quarter.isInvalid() || builtUpDistance(quarter, clearance) < clearance) {
        const Coord clear = clearOfOwnBase(home, towards, 100, Force::kBundleForwardPercent,
            Force::kBundleMaxForwardPercent, 2, [&](Coord p) { return usable(p) && builtUpDistance(p, clearance) >= clearance; });
        const int64_t length2 = int64_t(towards.x) * towards.x + int64_t(towards.y) * towards.y;
        if (clear.isValid() && length2 > 0) {
            bundlePoint = clear;
            forwardPercent = int((int64_t(clear.x - home.x) * towards.x + int64_t(clear.y - home.y) * towards.y) * 100 / length2);
        }
    }
    if (bundlePoint.isInvalid()) bundlePoint = home;
    traceDecision("force_bundle_point", AITelemetry::Record().set("x", bundlePoint.x).set("y", bundlePoint.y)
        .set("home_x", home.x).set("home_y", home.y).set("forward_x", towards.x).set("forward_y", towards.y)
        .set("forward_percent", forwardPercent).set("clearance", clearance)
        .set("built_up_tiles", builtUpDistance(bundlePoint, clearance)));
    return bundlePoint;
}

bool QuantBot::bundleUnit(const UnitBase* unit, int radius, std::vector<BundleWalker>& walkers) {
    if (!unit || !unit->isRespondable() || unit->hasATarget() || unit->wasForced()
        || unit->getAttackMode() == HUNT || unit->getAttackMode() == RETREAT || holdsOpeningPosition(unit)
        || unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence, UnitRole::ColonyGuard,
                           UnitRole::Escort, UnitRole::Wave}) != UnitRole::Free) return false;
    // A withdrawal or recovery brings reinforcements to the protected shelter: the home rally
    // (moveToOptimalSquadPosition) decides then, exactly as without bundling.
    if (reserveHeldHome()) return false;
    const Coord from = unit->getLocation();
    Coord at = bundlePoint;
    // The pre-1.0.602 rule: the closer of the forward rally and the army's centre of mass.
    if (bundleCentre.isValid() && (at.isInvalid() || tiles(from, bundleCentre) < tiles(from, at))) at = bundleCentre;
    if (at.isInvalid()) return false;
    if (unit->isMoving()) return true;
    const_cast<UnitBase*>(unit)->setGuardPoint(at);
    if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
    if (tiles(from, at) <= radius || unit->hasPendingPathRequest()) return true;
    walkers.push_back({unit->getObjectID(), tiles(from, at), at});
    return true;
}

void QuantBot::orderBundleWalkers(std::vector<BundleWalker>& walkers, int radius) {
    if (walkers.empty() || currentGame->getPathRequestQueueSize() > 150) return;
    std::sort(walkers.begin(), walkers.end(), [](const BundleWalker& a, const BundleWalker& b) {
        return Force::bundleOrderFirst(a.distance, a.id, b.distance, b.id);
    });
    const size_t orders = std::min<size_t>(walkers.size(), Force::kBundleOrdersPerPass);
    std::set<int64_t> reserved;
    for (size_t i = 0; i < orders; ++i) {
        const auto* unit = static_cast<const UnitBase*>(getObject(walkers[i].id));
        Coord slot = huntPartySlot(unit, walkers[i].at, radius, reserved);
        if (slot.isInvalid()) slot = walkers[i].at;
        doMove2Pos(unit, slot.x, slot.y, false);
        ++forceStats.bundleOrders;
    }
}

void QuantBot::saveForceState(OutputStream& stream) const {
    stream.writeSint32(bundlePoint.x);
    stream.writeSint32(bundlePoint.y);
    stream.writeSint32(bundleCentre.x);
    stream.writeSint32(bundleCentre.y);
    stream.writeUint32(bundleCycle);
    stream.writeUint32(bundleGateSince);
}

void QuantBot::saveDefenceMemory(OutputStream& stream) const {
    // The remembered enemy army (id order) and the musters (creation order).
    stream.writeUint32(Uint32(enemySightings.size()));
    for (const auto& [id, sighting] : enemySightings) {
        stream.writeUint32(id);
        stream.writeSint64(sighting.value);
        stream.writeUint32(sighting.cycle);
    }
    stream.writeUint32(Uint32(musters.size()));
    for (const auto& muster : musters) {
        stream.writeUint32(muster.building);
        stream.writeSint32(muster.point.x);
        stream.writeSint32(muster.point.y);
        stream.writeUint32(muster.since);
        stream.writeUint32(Uint32(muster.members.size()));
        for (const Uint32 id : muster.members) stream.writeUint32(id);
    }
}

void QuantBot::loadForceState(InputStream& stream) {
    bundlePoint.x = stream.readSint32();
    bundlePoint.y = stream.readSint32();
    bundleCentre.x = stream.readSint32();
    bundleCentre.y = stream.readSint32();
    bundleCycle = stream.readUint32();
    bundleGateSince = stream.readUint32();
    const auto valid = [&](Coord p) { return p.isInvalid() || getMap().tileExists(p); };
    if (!valid(bundlePoint) || !valid(bundleCentre)) throw std::runtime_error("Invalid force bundle state");
    estimateCycle = perimeterCycle = std::numeric_limits<Uint32>::max();
}

void QuantBot::loadDefenceMemory(InputStream& stream) {
    constexpr Uint32 kMaxEntries = 65536;
    enemySightings.clear();
    const Uint32 sightings = stream.readUint32();
    if (sightings > kMaxEntries) throw std::runtime_error("Invalid force sightings");
    for (Uint32 i = 0; i < sightings; ++i) {
        const Uint32 id = stream.readUint32();
        Sighting sighting;
        sighting.value = stream.readSint64();
        sighting.cycle = stream.readUint32();
        if (sighting.value < 0 || !enemySightings.emplace(id, sighting).second)
            throw std::runtime_error("Invalid force sighting");
    }
    musters.clear();
    const Uint32 count = stream.readUint32();
    if (count > kMaxEntries) throw std::runtime_error("Invalid force musters");
    for (Uint32 i = 0; i < count; ++i) {
        Muster muster;
        muster.building = stream.readUint32();
        muster.point.x = stream.readSint32();
        muster.point.y = stream.readSint32();
        muster.since = stream.readUint32();
        const Uint32 members = stream.readUint32();
        if (members > kMaxEntries || !getMap().tileExists(muster.point)) throw std::runtime_error("Invalid force muster");
        for (Uint32 m = 0; m < members; ++m) muster.members.insert(stream.readUint32());
        musters.push_back(std::move(muster));
    }
    estimateCycle = perimeterCycle = std::numeric_limits<Uint32>::max();
}

int QuantBot::bundledShareBps(const std::vector<Uint32>& ids, int radius) {
    int64_t total = 0, bundledValue = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getHealth() <= 0) continue;
        const int64_t price = std::max(100, currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price);
        total += price;
        const Coord at = unit->getLocation();
        if ((bundlePoint.isValid() && tiles(at, bundlePoint) <= radius)
            || (bundleCentre.isValid() && tiles(at, bundleCentre) <= radius)) bundledValue += price;
    }
    return total > 0 ? int(bundledValue * 10000 / total) : 10000;
}

// ---- Two-angle attack (kForceSplit / kForceFlank) --------------------------------------------
//
// The wave splits into two groups that walk (Area Guard, so they answer what they meet) to
// release points kSplitStandoffTiles from the objective, on directions 90 degrees apart (S:
// +-45 degrees off the approach; T: the main force head-on, the flank at 90 degrees). The first
// group there waits; both then hunt towards the objective at once. A group caught in a real fight
// on the way brings the other straight to it, and kSplitMaxMs bounds the wait.

const QuantBot::Split* QuantBot::splitOf(Uint32 id) const {
    for (const auto& split : splits)
        if (split.members[0].count(id) || split.members[1].count(id)) return &split;
    return nullptr;
}

bool QuantBot::startSplit(const std::vector<const UnitBase*>& units, const ObjectBase* objective) {
    if (!objective) return false;
    std::vector<const UnitBase*> walkers;
    int64_t sx = 0, sy = 0;
    const UnitBase* anchor = nullptr;
    for (const auto* unit : units) {
        if (!unit || !unit->isActive() || !unit->isAGroundUnit()) continue;
        walkers.push_back(unit);
        sx += unit->getLocation().x; sy += unit->getLocation().y;
        if (!anchor || (anchor->isInfantry() && !unit->isInfantry())) anchor = unit;
    }
    if (int(walkers.size()) < Force::kSplitMinMembers) return false;
    const Coord centre(int(sx / int64_t(walkers.size())), int(sy / int64_t(walkers.size())));
    const Coord target = objective->getClosestPoint(centre);
    if (tiles(centre, target) < Force::kSplitMinTiles) return false;
    auto release = [&](int degrees) {
        const auto p = Force::releasePoint({target.x, target.y}, {centre.x, centre.y}, Force::kSplitStandoffTiles, degrees);
        const Coord desired(std::clamp(p.x, 0, getMap().getSizeX() - 1), std::clamp(p.y, 0, getMap().getSizeY() - 1));
        return huntPartyPoint(desired, anchor, true);
    };
    const bool flank = forceVariant(kForceFlank) && !forceVariant(kForceSplit);
    Split split;
    if (flank) {
        // Group 0 the flank (the side whose release point is nearer), group 1 the main force.
        const Coord left = release(90), right = release(-90);
        split.point[0] = tiles(centre, right) < tiles(centre, left) ? right : left;
        split.point[1] = release(0);
    } else {
        split.point[0] = release(45);
        split.point[1] = release(-45);
    }
    if (split.point[0] == split.point[1] || tiles(split.point[0], split.point[1]) < Force::kSplitStandoffTiles / 2)
        return false;
    std::vector<Force::SplitMember> members;
    for (const auto* unit : walkers)
        members.push_back({unit->getObjectID(),
            std::max(100, currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price),
            tiles(unit->getLocation(), split.point[0]) - tiles(unit->getLocation(), split.point[1])});
    const auto groups = Force::splitGroups(members, flank ? Force::kFlankShareBps : 5000);
    for (size_t i = 0; i < walkers.size(); ++i) split.members[groups[i]].insert(walkers[i]->getObjectID());
    split.since = getGameCycleCount();
    split.objective = objective->getObjectID();
    split.target = target;
    for (int g = 0; g < 2; ++g) {
        const int radius = Force::bundleRadius(int(split.members[g].size()));
        std::set<int64_t> reserved;
        for (const auto* unit : walkers) {
            if (!split.members[g].count(unit->getObjectID())) continue;
            Coord slot = huntPartySlot(unit, split.point[g], radius, reserved);
            if (slot.isInvalid()) slot = split.point[g];
            doSetAttackMode(unit, AREAGUARD);
            const_cast<UnitBase*>(unit)->setGuardPoint(slot);
            doMove2Pos(unit, slot.x, slot.y, false);
        }
    }
    ++forceStats.splits;
    traceDecision("force_split", AITelemetry::Record().set("objective", split.objective)
        .set("target_x", target.x).set("target_y", target.y).set("flank", flank)
        .set("x0", split.point[0].x).set("y0", split.point[0].y).set("n0", int(split.members[0].size()))
        .set("x1", split.point[1].x).set("y1", split.point[1].y).set("n1", int(split.members[1].size())));
    splits.push_back(std::move(split));
    return true;
}

void QuantBot::updateSplits() {
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    int orders = Force::kSplitOrdersPerPass;
    for (auto it = splits.begin(); it != splits.end();) {
        Split& split = *it;
        int alive[2] = {0, 0}, arrived[2] = {0, 0}, fighting[2] = {0, 0};
        Coord fight[2] = {Coord::Invalid(), Coord::Invalid()};
        for (int g = 0; g < 2; ++g) {
            const int radius = Force::bundleRadius(int(split.members[g].size())) + 3;
            for (auto m = split.members[g].begin(); m != split.members[g].end();) {
                const auto* unit = dynamic_cast<const UnitBase*>(getObject(*m));
                // Defence, repair or a human order takes a member; so does anything that made it hunt.
                const bool keep = unit && unit->getOwner() == getHouse() && unit->getHealth() > 0
                    && unit->getAttackMode() != HUNT
                    && unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence}) == UnitRole::Free;
                if (!keep) { m = split.members[g].erase(m); continue; }
                ++alive[g];
                if (tiles(unit->getLocation(), split.point[g]) <= radius) ++arrived[g];
                const auto* target = unit->getTarget();
                if (target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam
                    && armed(target) && unit->isInWeaponRange(target)) {
                    ++fighting[g];
                    if (fight[g].isInvalid()) fight[g] = target->getLocation();
                }
                ++m;
            }
        }
        const auto* objective = getObject(split.objective);
        const bool objectiveGone = !objective || objective->getHealth() <= 0 || !objective->getOwner()
            || objective->getOwner()->getTeamID() == myTeam;
        const int fightingGroup = Force::splitFighting(fighting[0], alive[0]) ? 0
            : Force::splitFighting(fighting[1], alive[1]) ? 1 : -1;
        const bool timeout = ArmyPosturePolicy::elapsed(now, split.since) >= Uint32(MILLI2CYCLES(Force::kSplitMaxMs));
        const bool ready = Force::splitReady(arrived, alive);
        if (alive[0] == 0 || alive[1] == 0 || objectiveGone || fightingGroup >= 0 || timeout || ready) {
            // Attack together: towards the objective, or towards a group's fight.
            const Coord goal = fightingGroup >= 0 ? fight[fightingGroup] : split.target;
            for (int g = 0; g < 2; ++g)
                for (const Uint32 id : split.members[g]) {
                    const auto* unit = static_cast<const UnitBase*>(getObject(id));
                    if (!unit->isActive() || !unit->isRespondable()) continue;
                    if (unit->hasATarget() || objectiveGone) doSetAttackMode(unit, HUNT);
                    else doHuntToward(unit, goal.x, goal.y);
                }
            if (timeout && !ready && fightingGroup < 0) ++forceStats.splitTimeouts;
            ++forceStats.splitReleases;
            traceDecision("force_split_release", AITelemetry::Record().set("objective", split.objective)
                .set("alive0", alive[0]).set("arrived0", arrived[0]).set("fighting0", fighting[0])
                .set("alive1", alive[1]).set("arrived1", arrived[1]).set("fighting1", fighting[1])
                .set("ready", ready).set("timeout", timeout).set("objective_gone", objectiveGone)
                .set("elapsed_cycles", int(ArmyPosturePolicy::elapsed(now, split.since))));
            it = splits.erase(it);
            continue;
        }
        // Still walking or waiting: re-send whoever stands idle away from its point.
        for (int g = 0; g < 2 && orders > 0; ++g) {
            const int radius = Force::bundleRadius(int(split.members[g].size()));
            std::set<int64_t> reserved;
            for (const Uint32 id : split.members[g]) {
                if (orders <= 0) break;
                const auto* unit = static_cast<const UnitBase*>(getObject(id));
                if (!unit->isActive() || !unit->isRespondable() || unit->isMoving() || unit->hasPendingPathRequest()
                    || unit->hasATarget() || tiles(unit->getLocation(), split.point[g]) <= radius + 3) continue;
                Coord slot = huntPartySlot(unit, split.point[g], radius, reserved);
                if (slot.isInvalid()) slot = split.point[g];
                if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
                const_cast<UnitBase*>(unit)->setGuardPoint(slot);
                doMove2Pos(unit, slot.x, slot.y, false);
                --orders;
            }
        }
        ++it;
    }
}
