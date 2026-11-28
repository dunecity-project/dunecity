/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// Lessons from Stefan's Metropolis save seewhatidid (CityLessonsPolicy.h): two default-off QuantBot
// switches, each logged so a match can count how often it acted and what it achieved.
//
//   CityLauncherArmy  LAUNCHER_ARMY: house= applied=1 launcher_bps=before->after ornithopter_bps=...
//                     once per stats pass (30 s) while it holds the mix (QuantBot::planUnitMix).
//   LowValuePalaces   LOW_VALUE_PALACE: event=planned (a site and its low lots), placed (a Palace
//                     stands there), effect (the same lots' mean land value 2 minutes later; met=1
//                     when it rose by kMinRise with the Palace standing) or expired (no Palace within
//                     5 minutes of the plan; why: the passes it was saved for, pre-empted, ordered, held).
//                     The next plan does not wait for the effect; a site that expired is not planned
//                     again for 10 minutes.
//   CITY_LOTS         every 3 game minutes, every QuantBot house: R/C lots, low-value lots, mean land value.
//   FrontPalaces      FRONT_PALACE: the same events; its effect is read 5 minutes after placement and
//                     met=1 when the Palace still stands there (Stefan's survived, the AI's did not).
// Nothing here is saved: a loaded game plans again on its first pass.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/CityLessonsPolicy.h>
#include <players/TurretFrontPolicy.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

#include <algorithm>
#include <cstdlib>

namespace {
int envSwitch(const char* name) {
    const char* text = std::getenv(name);
    return text != nullptr && (text[0] == '0' || text[0] == '1') ? text[0] - '0' : -1;
}
} // namespace

// Experiment-only overrides for match arms in one build (0 or 1), read once per process and never a
// match rule. Without them each house follows its own QuantBot Config (play styles).
bool QuantBot::cityLauncherArmyEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_CITY_LAUNCHER_ARMY");
    return forced >= 0 ? forced == 1 : botConfig().force.cityLauncherArmy;
}

bool QuantBot::frontPalacesEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_FRONT_PALACES");
    return forced >= 0 ? forced == 1 : botConfig().force.frontPalaces;
}

bool QuantBot::economyFirstEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_ECONOMY_FIRST");
    return forced >= 0 ? forced == 1 : botConfig().force.economyFirst;
}

int QuantBot::economyFirstUnitPacing() const {
    static const int forced = [] {
        const char* text = std::getenv("DUNECITY_TEST_ECONOMY_UNIT_PACING");
        return text && text[0] >= '0' && text[0] <= '2' && text[1] == '\0' ? text[0] - '0' : -1;
    }();
    return forced >= 0 ? forced : botConfig().force.economyFirstUnitPacing;
}

bool QuantBot::economyFirstTurretsEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_ECONOMY_TURRETS");
    return forced >= 0 ? forced == 1 : botConfig().force.economyFirstTurrets;
}

bool QuantBot::economyLogEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_ECONOMY_LOG");
    return forced == 1 || economyFirstEnabled();
}

void QuantBot::planEconomyFirst(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    pass.economyFirstHold = 0;
    pass.economyFirstActive = false;
    cityLessons.economyFirstThreatNow = false;
    auto& lessons = cityLessons;
    const auto* sim = currentGame->getCitySimulation();
    const int houseID = getHouse()->getHouseID();
    int livingHouses = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)
        if (const auto* other = currentGame->getHouse(h); other && other->isAlive()) ++livingHouses;
    const bool lowSpice = L::lowSpiceMap(lastCalculatedSpice, livingHouses);
    if (economyFirstEnabled() && pass.citySimEnabled && sim && gameMode == GameMode::Custom
        && !lessons.economyFirstStoodDown && !lowSpice && !lessons.economyFirstRichLogged) {
        // A spice-rich map: Stefan asked for low / no spice maps only.
        lessons.economyFirstRichLogged = true;
        SDL_Log("ECONOMY_FIRST: house=%d event=spice_rich spice=%d houses=%d cycle=%u", houseID, lastCalculatedSpice,
                livingHouses, getGameCycleCount());
    }
    if (economyFirstEnabled() && pass.citySimEnabled && sim && gameMode == GameMode::Custom
        && !lessons.economyFirstStoodDown && lowSpice) {
        const int population = sim->getHouseState(houseID).getTotalPop();
        const int minute = int(getGameCycleCount() / MILLI2CYCLES(60000));
        const auto* house = getHouse();
        const int zonesNow = house->getNumItems(Structure_ZoneResidential) + house->getNumItems(Structure_ZoneCommercial)
            + house->getNumItems(Structure_ZoneIndustrial);
        if (lessons.economyFirstStartZones < 0) lessons.economyFirstStartZones = zonesNow;
        if (!lessons.economyFirstFootprintLogged && minute >= L::kEconomyFirstCheckMinute) {
            lessons.economyFirstFootprintLogged = true;
            // New zones since the first pass (his map started every house with 92).
            const int zones = zonesNow - lessons.economyFirstStartZones;
            const int yards = house->getNumItems(Structure_ConstructionYard);
            SDL_Log("ECONOMY_FIRST: house=%d event=footprint met=%d half_his=%d zones=%d yards=%d mcvs=%d population=%d "
                    "credits=%d cycle=%u", houseID,
                    int(lessons.economyFirstMcvs > 0 && lessons.economyFirstZonesPlaced >= L::kEconomyFirstEffectZones && yards >= 2),
                    int(zones >= L::kEconomyFirstZones && yards >= L::kEconomyFirstCheckYards), zones, yards,
                    lessons.economyFirstMcvs, population, house->getCredits(), getGameCycleCount());
        }
        if (population >= L::kEconomyFirstPopulation) {
            lessons.economyFirstStoodDown = true;
            SDL_Log("ECONOMY_FIRST: house=%d event=stood_down population=%d yards=%d hold_passes=%d threat_passes=%d "
                    "cycle=%u", houseID, population, getHouse()->getNumItems(Structure_ConstructionYard),
                    lessons.economyFirstHoldPasses, lessons.economyFirstThreatPasses, getGameCycleCount());
        } else {
            // Usable room sets build capacity; the lobby yard limit still caps it.
            const int before = pass.cityYardTarget;
            pass.cityYardTarget = std::max(pass.cityYardTarget, L::economyFirstYardsForRoom(availableBaseFootprintsWide));
            if (pass.yardLimit > 0) pass.cityYardTarget = std::min(pass.cityYardTarget, pass.yardLimit);
            // An enemy ground unit near a yard, factory, Starport, Nuclear Plant or Palace releases the hold for
            // the factories: the base is defended. (Near any building, the first smoke runs released it in half
            // the passes: a 92-zone city always has an enemy near some lot.)
            pass.economyFirstActive = true;
            bool threat = false;
            for (const auto& contact : visibleGroundTurretThreats) {
                for (const auto* structure : getStructureList()) {
                    if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
                        || !L::economyFirstCoreAsset(structure->getItemID())) continue;
                    if (blockDistance(contact.at, structure->getClosestPoint(contact.at)) <= L::kEconomyFirstThreatTiles) {
                        threat = true;
                        break;
                    }
                }
                if (threat) break;
            }
            lessons.economyFirstThreatNow = threat;
            if (threat) ++lessons.economyFirstThreatPasses;
            else {
                // What the switch is saving for: the MCVs its yard schedule still lacks, and one Nuclear Plant
                // while power is short; never more than half the cash above the floor (a poor house keeps its
                // ordinary spending, the 12 tests at 54a8a1189).
                const auto& data = currentGame->objectData.data;
                const int missing = std::max(0, pass.cityYardTarget - pass.cityConstructionCapacity);
                const bool powerShort = getHouse()->getProducedPower() - getHouse()->getPowerRequirement()
                    < pass.cityPowerReserve;
                // His opening zones (19 in the first three minutes) while they are not yet ordered.
                const int zoneOrders = lessons.economyFirstZoneOrders[0] + lessons.economyFirstZoneOrders[1]
                    + lessons.economyFirstZoneOrders[2];
                const int openingZones = minute < L::kEconomyFirstOpeningMinutes
                    ? std::max(0, L::kEconomyFirstOpeningZones - zoneOrders) * data[Structure_ZoneResidential][houseID].price : 0;
                const int need = missing * data[Unit_MCV][houseID].price
                    + (powerShort ? data[Structure_NuclearPlant][houseID].price : 0) + openingZones;
                pass.economyFirstHold = std::min(need, L::economyFirstHold(getHouse()->getCredits()));
                ++lessons.economyFirstHoldPasses;
            }
            // His Palaces (3 by minute 3, 6 by 10, 8 by 20, 15 by 31): planned as the city economy project while the
            // house can pay for one, so the project's reserve keeps the price from the factories until it is ordered.
            {
                const auto& data = currentGame->objectData.data;
                const auto& palace = data[Structure_Palace][houseID];
                const bool pending = pass.itemCount[Structure_Palace] != getHouse()->getNumItems(Structure_Palace);
                const bool onlyOne = getGameInitSettings().getGameOptions().onlyOnePalace && pass.itemCount[Structure_Palace] >= 1;
                if (pass.economyProject == NONE_ID && !pending && !onlyOne && palace.enabled
                    && palace.techLevel <= getHouse()->getTechLevel()
                    && pass.itemCount[Structure_Palace] < L::economyFirstPalaces(minute)
                    && getHouse()->getCredits() >= int(palace.price)) {
                    const Coord site = findPalaceDistrictSite(Structure_Palace, ForceConcentrationPolicy::kPalaceOutnumberedMinLots);
                    if (site.isValid()) {
                        pass.economyProject = Structure_Palace;
                        pass.economyProjectSite = site;
                        pass.economyProjectReserve = palace.price;
                        SDL_Log("ECONOMY_FIRST: house=%d event=palace_planned x=%d y=%d palaces=%d target=%d credits=%d cycle=%u",
                                houseID, site.x, site.y, pass.itemCount[Structure_Palace], L::economyFirstPalaces(minute),
                                getHouse()->getCredits(), getGameCycleCount());
                    }
                }
            }
            if (pass.emitStatsLog)
                SDL_Log("ECONOMY_FIRST: house=%d event=pass yard_target=%d was=%d capacity=%d hold=%d threat=%d credits=%d "
                        "population=%d cycle=%u", houseID, pass.cityYardTarget, before, pass.cityConstructionCapacity,
                        pass.economyFirstHold, int(threat), getHouse()->getCredits(), population, getGameCycleCount());
        }
    }
    lessons.economyFirstActiveNow = pass.economyFirstActive;
    if (pass.citySimEnabled && economyLogEnabled()) logEconomyState(pass);
}

bool QuantBot::economyFirstMcv(BuildPass& pass, BuilderTurn& turn) {
    const Uint32 producer = turn.pBuilder->getItemID();
    // The Starport keeps buying: its bargains now leave the hold (buildStarPort's bargainReserve).
    if (!pass.economyFirstActive || producer != Structure_HeavyFactory) return false;
    if (pass.cityConstructionCapacity + pass.economyFirstMcvsOrdered >= pass.cityYardTarget) return false;
    const auto* builder = turn.pBuilder;
    // A busy or held factory, or one that cannot add a unit, does its ordinary work: blocking it here starved
    // affordable military production (quantbot_shared-spending_dunecity at 32601bbb0).
    if (builder->isUpgrading() || builder->isOnHold() || builder->getProductionQueueSize() > 0
        || getHouse()->isGroundUnitLimitReached()) return false;
    // The held cash is the MCV's: give it back for this order only.
    const int held = turn.reserve.reserved;
    pass.money += held;
    bool ordered = false;
    const char* what = "mcv";
    // The MCV skips the opening-capital test that kept the AIs at one yard, but not the working runway: production
    // keeps max(1000, the city working reserve) after paying (quantbot_shared-spending_dunecity at 8e05c6eb5).
    const int mcvPrice = currentGame->objectData.data[Unit_MCV][getHouse()->getHouseID()].price;
    if (pass.money - mcvPrice < std::max(1000, pass.cityWorkingReserve)) what = "runway";
    else if (campaignAvailableToBuild(builder, Unit_MCV)) {
        ordered = produceItemWithLogging(pass, turn, Unit_MCV, __LINE__, "economy_first_mcv");
        if (ordered) { ++pass.economyFirstMcvsOrdered; ++cityLessons.economyFirstMcvs; }
    } else if (builder->getCurrentUpgradeLevel() < builder->getMaxUpgradeLevel()
               && getHouse()->getCredits() >= builder->getUpgradeCost()) {
        what = "upgrade";
        ordered = upgradeWithLogging(pass, turn, __LINE__);
    } else what = "unavailable";
    pass.money -= held;
    // Every order, and the first refused MCV of each minute (why the yards did not come).
    const Uint32 minute = getGameCycleCount() / MILLI2CYCLES(60000);
    if (ordered || cityLessons.economyFirstRefusedMinute != minute + 1) {
        if (!ordered) cityLessons.economyFirstRefusedMinute = minute + 1;
        SDL_Log("ECONOMY_FIRST: house=%d event=%s ordered=%d capacity=%d yard_target=%d credits=%d cycle=%u",
            getHouse()->getHouseID(), what, int(ordered), pass.cityConstructionCapacity, pass.cityYardTarget,
            getHouse()->getCredits(), getGameCycleCount());
    }
    // Only an MCV or upgrade actually ordered takes this factory's turn.
    return ordered;
}

Uint32 QuantBot::economyFirstZone(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    auto& lessons = cityLessons;
    const auto& orders = lessons.economyFirstZoneOrders;
    const int zones = orders[0] + orders[1] + orders[2];
    if (int64_t(zones) * 10000 > int64_t(L::kEconomyFirstZoneShareBps) * lessons.economyFirstYardOrders) return NONE_ID;
    if (getHouse()->getProducedPower() - getHouse()->getPowerRequirement() < 24) return NONE_ID;
    static constexpr Uint32 kZones[3] = {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial};
    const int mixTotal = L::kEconomyFirstZoneMix[0] + L::kEconomyFirstZoneMix[1] + L::kEconomyFirstZoneMix[2];
    // Most behind his mix first, then the next.
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int64_t da = int64_t(L::kEconomyFirstZoneMix[a]) * (zones + 1) - int64_t(orders[a]) * mixTotal;
        const int64_t db = int64_t(L::kEconomyFirstZoneMix[b]) * (zones + 1) - int64_t(orders[b]) * mixTotal;
        return da != db ? da > db : a < b;
    });
    for (const int k : order) {
        const Uint32 zone = kZones[k];
        if (buildingCapitalCost(pass, zone) > pass.money) continue;
        if (findPlaceLocation(zone).isValid()) return zone;
    }
    return NONE_ID;
}

void QuantBot::logEconomyState(const BuildPass& pass) {
    const auto* sim = currentGame->getCitySimulation();
    const Uint32 now = getGameCycleCount();
    auto& lessons = cityLessons;
    if (!sim || (lessons.lastEconomyLog != 0 && now - lessons.lastEconomyLog < MILLI2CYCLES(CityLessonsPolicy::kEconomyLogMs)))
        return;
    lessons.lastEconomyLog = now;
    const int houseID = getHouse()->getHouseID();
    // MCV_LOST: an MCV manageMcv saw that is gone without deploying (deploys erase their track).
    for (auto it = lessons.mcvTracks.begin(); it != lessons.mcvTracks.end();) {
        if (currentGame->getObjectManager().getObject(it->first) != nullptr) { ++it; continue; }
        const auto& tr = it->second;
        int enemyGap = -1;
        for (const auto* structure : getStructureList()) {
            if (!structure->getOwner() || structure->getHealth() <= 0
                || structure->getOwner()->getTeamID() == getHouse()->getTeamID()) continue;
            const int gap = blockDistance(tr.last, structure->getClosestPoint(tr.last)).lround();
            if (enemyGap < 0 || gap < enemyGap) enemyGap = gap;
        }
        SDL_Log("MCV_LOST: house=%d x=%d y=%d start_x=%d start_y=%d travelled=%d target_x=%d target_y=%d to_target=%d "
                "escorts=%d hit_by=%d hit_s=%d age_s=%d enemy_tiles=%d home=%d cycle=%u", houseID, tr.last.x, tr.last.y,
                tr.start.x, tr.start.y, tr.start.isValid() ? blockDistance(tr.start, tr.last).lround() : -1,
                tr.target.isValid() ? tr.target.x : -1, tr.target.isValid() ? tr.target.y : -1,
                tr.target.isValid() ? blockDistance(tr.last, tr.target).lround() : -1, tr.escorts, tr.hitBy,
                tr.hitCycle ? int((now - tr.hitCycle) * GAMESPEED_DEFAULT / 1000) : -1,
                int((now - (tr.born - 1)) * GAMESPEED_DEFAULT / 1000), enemyGap, int(onOwnRockFormation(tr.last)), now);
        it = lessons.mcvTracks.erase(it);
    }
    const auto& state = sim->getHouseState(houseID);
    const int taxPerMinute = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(state.taxBaseEighths,
        sim->getCityTax(houseID), state.avgLandValue));
    const auto* house = getHouse();
    SDL_Log("ECONOMY_STATE: house=%d credits=%d yards=%d mcvs=%d yard_target=%d capacity=%d refinery_bays=%d "
            "yard_refineries=%d zones_r=%d zones_c=%d zones_i=%d nuclear=%d windtraps=%d palaces=%d police=%d "
            "rocket_turrets=%d heavy_factories=%d population=%d tax_per_min=%d power=%d/%d unit_built=%d "
            "structure_built=%d hold=%d spice=%d yards_lost=%d mcvs_lost=%d footprints=%d free_footprints=%d cycle=%u",
            houseID, house->getCredits(),
            house->getNumItems(Structure_ConstructionYard), house->getNumItems(Unit_MCV), pass.cityYardTarget,
            pass.cityConstructionCapacity, getAIRefineryBays(), pass.cityYardRefineries,
            house->getNumItems(Structure_ZoneResidential), house->getNumItems(Structure_ZoneCommercial),
            house->getNumItems(Structure_ZoneIndustrial), house->getNumItems(Structure_NuclearPlant),
            house->getNumItems(Structure_WindTrap), house->getNumItems(Structure_Palace),
            house->getNumItems(Structure_PoliceStation), house->getNumItems(Structure_RocketTurret),
            house->getNumItems(Structure_HeavyFactory), state.getTotalPop(), taxPerMinute,
            house->getProducedPower(), house->getPowerRequirement(), house->getUnitBuiltValue(),
            house->getStructureBuiltValue(), pass.economyFirstHold, lastCalculatedSpice,
            house->getNumLostItems(Structure_ConstructionYard), house->getNumLostItems(Unit_MCV), availableBaseFootprints,
            availableBaseFootprintsWide, now);
}

bool QuantBot::lowValuePalacesEnabled() const {
    static const int forced = envSwitch("DUNECITY_TEST_LOW_VALUE_PALACES");
    return forced >= 0 ? forced == 1 : botConfig().force.lowValuePalaces;
}

void QuantBot::applyCityLauncherArmy(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    const auto before = pass.unitMix;
    const int floor = std::clamp(botConfig().force.cityLauncherArmyBps, L::kLauncherArmyMinBps, L::kLauncherArmyMaxBps);
    pass.unitMix = L::launcherArmy(pass.unitMix, floor);
    ++cityLessons.launcherArmyPasses;
    if (!pass.emitStatsLog) return;
    // What the army is now: Launchers' share of the house's combat vehicle value.
    int64_t vehicles = 0, launchers = 0;
    const auto& data = currentGame->objectData.data;
    const int houseID = getHouse()->getHouseID();
    for (const Uint32 item : kMixItems) {
        const int64_t value = int64_t(getHouse()->getNumItems(item)) * data[item][houseID].price;
        vehicles += value;
        if (item == Unit_Launcher) launchers += value;
    }
    // met=1: Launchers are at least half of the house's combat vehicle value (the activation registry's success).
    SDL_Log("LAUNCHER_ARMY: house=%d applied=1 met=%d passes=%d launcher_bps=%d->%d ornithopter_bps=%d->%d launchers=%d "
            "ornithopters=%d launcher_value=%lld vehicle_value=%lld cycle=%u",
            houseID, int(vehicles > 0 && launchers * 2 >= vehicles), cityLessons.launcherArmyPasses, before[L::kLauncherSlot], pass.unitMix[L::kLauncherSlot],
            before[UnitMixPolicy::kOrnithopterSlot], pass.unitMix[UnitMixPolicy::kOrnithopterSlot],
            getHouse()->getNumItems(Unit_Launcher), getHouse()->getNumItems(Unit_Ornithopter),
            (long long)launchers, (long long)vehicles, getGameCycleCount());
}

int QuantBot::lowValueLotsMean(const std::vector<Coord>& lots, int* standing) const {
    const auto* sim = currentGame->getCitySimulation();
    int n = 0;
    long long sum = 0;
    for (const Coord at : lots) {
        const auto* tile = getMap().tileExists(at.x, at.y) ? getMap().getTile(at.x, at.y) : nullptr;
        const auto* zone = tile ? tile->getNonInfantryGroundObject() : nullptr;
        if (!sim || !zone || !zone->isAStructure() || zone->getOwner() != getHouse()) continue;
        ++n;
        sum += sim->getLandValueMap().worldGet(at.x, at.y);
    }
    if (standing) *standing = n;
    return n ? int(sum / n) : 0;
}

bool QuantBot::ownPalaceAt(Coord site) const {
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && structure->getItemID() == Structure_Palace && structure->getLocation() == site) return true;
    return false;
}

bool QuantBot::nearExpiredLessonSite(Coord pos) const {
    const Uint32 now = getGameCycleCount();
    for (const auto& [site, cycle] : cityLessons.expiredSites)
        if (now - cycle < MILLI2CYCLES(CityLessonsPolicy::kExpiredSiteMs)
            && std::max(std::abs(site.x - pos.x), std::abs(site.y - pos.y)) <= CityLessonsPolicy::kExpiredSiteTiles) return true;
    return false;
}

QuantBot::CityLessonsState::PalaceWatch* QuantBot::lessonWatchAt(Coord site) {
    if (cityLessons.palace.active && cityLessons.palace.site == site) return &cityLessons.palace;
    if (cityLessons.front.active && cityLessons.front.site == site) return &cityLessons.front;
    return nullptr;
}

void QuantBot::logCityLots() {
    const auto* sim = currentGame->getCitySimulation();
    const Uint32 now = getGameCycleCount();
    if (!sim || (cityLessons.lastLotsLog != 0 && now - cityLessons.lastLotsLog < MILLI2CYCLES(CityLessonsPolicy::kLotsLogMs)))
        return;
    cityLessons.lastLotsLog = now;
    int lots = 0, low = 0;
    long long sum = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || (structure->getItemID() != Structure_ZoneResidential && structure->getItemID() != Structure_ZoneCommercial)) continue;
        const Coord o = structure->getLocation();
        const int lv = sim->getLandValueMap().worldGet(o.x, o.y);
        ++lots;
        low += int(CityLessonsPolicy::lowLot(lv));
        sum += lv;
    }
    SDL_Log("CITY_LOTS: house=%d event=lots lots=%d low_lots=%d lv_now=%d palaces=%d cycle=%u", getHouse()->getHouseID(),
            lots, low, lots ? int(sum / lots) : 0, getHouse()->getNumItems(Structure_Palace), now);
}

/// One Palace watch (LowValuePalaces or FrontPalaces): placed on its site or expired; then the effect of
/// each placed one is read in turn while the next plan goes ahead.
void QuantBot::trackPalaceWatch(CityLessonsState::PalaceWatch& watch, std::vector<CityLessonsState::PalaceWatch>& effects,
                                const char* tag, Uint32 effectDelayMs, bool riseCounts, int& placedCount) {
    QuantBot& bot = *this;
    namespace L = CityLessonsPolicy;
    const Uint32 now = bot.getGameCycleCount();
    const int houseID = bot.getHouse()->getHouseID();
    auto& expired = cityLessons.expiredSites;
    expired.erase(std::remove_if(expired.begin(), expired.end(),
        [&](const auto& e) { return now - e.second >= MILLI2CYCLES(L::kExpiredSiteMs); }), expired.end());
    if (watch.active && !watch.placed) {
        if (ownPalaceAt(watch.site)) {
            watch.placed = true;
            watch.placedCycle = now;
            ++placedCount;
            int standing = 0;
            const int lv = bot.lowValueLotsMean(watch.lots, &standing);
            SDL_Log("%s: house=%d event=placed x=%d y=%d low_lots=%d lv_before=%d lv_now=%d enemy_tiles=%d "
                    "turrets_ahead=%d wait_s=%u cycle=%u", tag, houseID, watch.site.x, watch.site.y, standing,
                    watch.lvBefore, lv, watch.enemyTiles, watch.turretsAhead,
                    (now - watch.plannedCycle) * GAMESPEED_DEFAULT / 1000, now);
            effects.push_back(watch);
            watch = {};
        } else if (now - watch.plannedCycle >= MILLI2CYCLES(L::kPlanExpiryMs)) {
            const int price = currentGame->objectData.data[Structure_Palace][houseID].price;
            SDL_Log("%s: house=%d event=expired x=%d y=%d low_lots=%d lv_before=%d credits=%d price=%d project_passes=%d "
                    "pending_passes=%d saving_passes=%d preempted_passes=%d ordered_passes=%d held_passes=%d blocked=%d cycle=%u",
                    tag, houseID, watch.site.x, watch.site.y, int(watch.lots.size()), watch.lvBefore,
                    bot.getHouse()->getCredits(), price, watch.projectPasses, watch.pendingPasses, watch.savingPasses,
                    watch.preemptedPasses, watch.orderedPasses, watch.heldPasses, watch.blockedChecks, now);
            expired.emplace_back(watch.site, now);
            watch = {};
        }
    }
    for (auto it = effects.begin(); it != effects.end();) {
        if (now - it->placedCycle < MILLI2CYCLES(effectDelayMs)) { ++it; continue; }
        int standing = 0;
        const int lv = bot.lowValueLotsMean(it->lots, &standing);
        const bool stands = ownPalaceAt(it->site);
        // met=1: the Palace still stands and (LowValuePalaces) its lots rose by kMinRise or more.
        const bool met = stands && (!riseCounts || lv - it->lvBefore >= L::kMinRise);
        SDL_Log("%s: house=%d event=effect met=%d x=%d y=%d low_lots=%d lots_standing=%d lv_before=%d lv_after=%d "
                "rise=%d palace_stands=%d cycle=%u", tag, houseID, int(met), it->site.x, it->site.y,
                int(it->lots.size()), standing, it->lvBefore, lv, lv - it->lvBefore, int(stands), now);
        it = effects.erase(it);
    }
}

void QuantBot::trackLowValuePalaces() {
    trackPalaceWatch(cityLessons.palace, cityLessons.palaceEffects, "LOW_VALUE_PALACE", CityLessonsPolicy::kEffectDelayMs,
                     true, cityLessons.palacesPlaced);
}

void QuantBot::trackFrontPalaces() {
    trackPalaceWatch(cityLessons.front, cityLessons.frontEffects, "FRONT_PALACE", CityLessonsPolicy::kFrontPalaceSurvivalMs,
                     false, cityLessons.frontPlaced);
}

bool QuantBot::planFrontPalace(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    auto& watch = cityLessons.front;
    const int price = currentGame->objectData.data[Structure_Palace][getHouse()->getHouseID()].price;
    if (watch.active) {
        if (watch.placed) return false;
        ++watch.projectPasses;
        pass.economyProject = Structure_Palace;
        pass.economyProjectSite = watch.site;
        pass.economyProjectSpareOnly = false;
        pass.economyProjectReserve = price;
        return true;
    }
    const Uint32 now = getGameCycleCount();
    if (cityLessons.lastFrontPlan != 0 && now - cityLessons.lastFrontPlan < MILLI2CYCLES(L::kFrontPalaceIntervalMs))
        return false;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || sim->getHouseState(getHouse()->getHouseID()).avgLandValue <= 0) return false;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    // Distance to the nearest enemy building (Chebyshev, capped beyond the band).
    std::vector<uint8_t> seed(size_t(w) * h, 0);
    std::vector<Coord> turrets;
    for (const auto* structure : getStructureList()) {
        if (structure->getHealth() <= 0) continue;
        const House* owner = structure->getOwner();
        if (owner == getHouse()) {
            if (structure->getItemID() == Structure_RocketTurret) turrets.push_back(structure->getLocation());
            continue;
        }
        if (!owner || owner->getTeamID() == getHouse()->getTeamID() || structure->getItemID() == Structure_Wall) continue;
        const Coord o = structure->getLocation(), s = structure->getStructureSize();
        for (int y = o.y; y < o.y + s.y; ++y) for (int x = o.x; x < o.x + s.x; ++x)
            if (x >= 0 && y >= 0 && x < w && y < h) seed[size_t(y) * w + x] = 1;
    }
    // Why no front site this time, once per interval (rs192 at 86dcb77ea: planned in 11 of 24 house-games).
    auto noSite = [&](const char* reason, int allowedTiles) {
        cityLessons.lastFrontPlan = now;
        SDL_Log("FRONT_PALACE: house=%d event=no_site reason=%s turrets=%d allowed_tiles=%d credits=%d cycle=%u",
                    getHouse()->getHouseID(), reason, int(turrets.size()), allowedTiles, getHouse()->getCredits(), now);
        return false;
    };
    if (turrets.size() < size_t(L::kFrontPalaceTurretsAhead)) return noSite("turrets", 0);
    const auto enemy = TurretFrontPolicy::chebyshevDistance(seed, w, h, L::kFrontPalaceMaxEnemyTiles + 1);
    auto enemyAt = [&](Coord c) { return int(enemy[size_t(c.y) * w + c.x]); };
    std::vector<int> turretEnemy;
    turretEnemy.reserve(turrets.size());
    for (const Coord t : turrets) turretEnemy.push_back(enemyAt(t));
    std::vector<uint8_t> allowed(size_t(w) * h, 0);
    int allowedTiles = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int d = int(enemy[size_t(y) * w + x]);
        if (d < L::kFrontPalaceMinEnemyTiles || d > L::kFrontPalaceMaxEnemyTiles) continue;
        int ahead = 0;
        for (size_t i = 0; i < turrets.size() && ahead < L::kFrontPalaceTurretsAhead; ++i)
            ahead += TurretFrontPolicy::tileDistance(x, y, turrets[i].x, turrets[i].y) <= L::kFrontPalaceCoverTiles
                && turretEnemy[i] < d;
        if (L::frontPalaceCentre(d, ahead)) { allowed[size_t(y) * w + x] = 1; ++allowedTiles; }
    }
    if (allowedTiles == 0) return noSite("band", 0);
    const Coord site = findPalaceDistrictSite(Structure_Palace, L::kFrontPalaceMinLots, false, &allowed);
    if (site.isInvalid()) return noSite("site", allowedTiles);
    cityLessons.lastFrontPlan = now;
    const Coord centre = site + Coord(1, 1);
    std::vector<Coord> lots;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && (structure->getItemID() == Structure_ZoneResidential || structure->getItemID() == Structure_ZoneCommercial)
            && TurretFrontPolicy::tileDistance(structure->getLocation().x, structure->getLocation().y, centre.x, centre.y)
                <= ForceConcentrationPolicy::kPalaceReachTiles) lots.push_back(structure->getLocation());
    int ahead = 0;
    for (size_t i = 0; i < turrets.size(); ++i)
        ahead += TurretFrontPolicy::tileDistance(centre.x, centre.y, turrets[i].x, turrets[i].y) <= L::kFrontPalaceCoverTiles
            && turretEnemy[i] < enemyAt(centre);
    watch.active = true;
    watch.placed = false;
    watch.site = site;
    watch.lots = lots;
    watch.plannedCycle = now;
    watch.lvBefore = lowValueLotsMean(lots, nullptr);
    watch.enemyTiles = enemyAt(centre);
    watch.turretsAhead = ahead;
    ++cityLessons.frontPlanned;
    pass.economyProject = Structure_Palace;
    pass.economyProjectSite = site;
    pass.economyProjectSpareOnly = false;
    pass.economyProjectReserve = price;
    SDL_Log("FRONT_PALACE: house=%d event=planned x=%d y=%d low_lots=%d lv_before=%d enemy_tiles=%d turrets_ahead=%d "
            "palaces=%d credits=%d cycle=%u", getHouse()->getHouseID(), site.x, site.y, int(lots.size()), watch.lvBefore,
            watch.enemyTiles, ahead, getHouse()->getNumItems(Structure_Palace), getHouse()->getCredits(), now);
    return true;
}

bool QuantBot::planLowValuePalace(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    auto& watch = cityLessons.palace;
    const int price = currentGame->objectData.data[Structure_Palace][getHouse()->getHouseID()].price;
    // One site at a time: a planned site stays the project (saved for) until its Palace stands or the
    // plan expires; its effect is read later (palaceEffects) while the next plan goes ahead.
    if (watch.active) {
        if (watch.placed) return false;
        ++watch.projectPasses;
        pass.economyProject = Structure_Palace;
        pass.economyProjectSite = watch.site;
        pass.economyProjectSpareOnly = false;
        pass.economyProjectReserve = price;
        return true;
    }
    // Before the first land-value scan every lot reads 0: plan only once the city has values.
    if (const auto* s = currentGame->getCitySimulation(); !s || s->getHouseState(getHouse()->getHouseID()).avgLandValue <= 0)
        return false;
    const Coord site = findPalaceDistrictSite(Structure_Palace, L::kMinLowLots, true);
    if (site.isInvalid()) return false;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim) return false;
    // The low lots the Palace will reach (its stamp centre is its location + (1,1)).
    const Coord centre = site + Coord(1, 1);
    std::vector<Coord> lots;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || (structure->getItemID() != Structure_ZoneResidential && structure->getItemID() != Structure_ZoneCommercial)) continue;
        const Coord o = structure->getLocation();
        bool pendingLift = false;   // a placed lesson Palace whose effect is not read yet covers it
        for (const auto& placed : cityLessons.palaceEffects)
            pendingLift = pendingLift || std::find(placed.lots.begin(), placed.lots.end(), o) != placed.lots.end();
        if (!pendingLift && std::max(std::abs(o.x - centre.x), std::abs(o.y - centre.y)) <= ForceConcentrationPolicy::kPalaceReachTiles
            && L::lowLot(sim->getLandValueMap().worldGet(o.x, o.y))) lots.push_back(o);
    }
    if (int(lots.size()) < L::kMinLowLots) return false;
    watch.active = true;
    watch.placed = false;
    watch.site = site;
    watch.lots = lots;
    watch.plannedCycle = getGameCycleCount();
    watch.lvBefore = lowValueLotsMean(lots, nullptr);
    ++cityLessons.palacesPlanned;
    pass.economyProject = Structure_Palace;
    pass.economyProjectSite = site;
    pass.economyProjectSpareOnly = false;
    pass.economyProjectReserve = price;
    SDL_Log("LOW_VALUE_PALACE: house=%d event=planned x=%d y=%d low_lots=%d lv_before=%d palaces=%d credits=%d cycle=%u",
            getHouse()->getHouseID(), site.x, site.y, int(lots.size()), watch.lvBefore,
            getHouse()->getNumItems(Structure_Palace), getHouse()->getCredits(), getGameCycleCount());
    return true;
}
