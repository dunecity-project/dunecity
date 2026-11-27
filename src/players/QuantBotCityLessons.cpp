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
//                     stands there), effect (the same lots' mean land value 2 minutes later) or
//                     expired (no Palace within 5 minutes of the plan).
// Nothing here is saved: a loaded game plans again on its first pass.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/CityLessonsPolicy.h>
#include <dunecity/CitySimulation.h>

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

void QuantBot::trackLowValuePalaces() {
    namespace L = CityLessonsPolicy;
    auto& watch = cityLessons.palace;
    if (!watch.active) return;
    const Uint32 now = getGameCycleCount();
    const int houseID = getHouse()->getHouseID();
    if (!watch.placed) {
        for (const auto* structure : getStructureList())
            if (structure->getOwner() == getHouse() && structure->getHealth() > 0
                && structure->getItemID() == Structure_Palace && structure->getLocation() == watch.site) {
                watch.placed = true;
                watch.placedCycle = now;
                ++cityLessons.palacesPlaced;
                int standing = 0;
                const int lv = lowValueLotsMean(watch.lots, &standing);
                SDL_Log("LOW_VALUE_PALACE: house=%d event=placed x=%d y=%d low_lots=%d lv_before=%d lv_now=%d "
                        "wait_s=%u cycle=%u", houseID, watch.site.x, watch.site.y, standing, watch.lvBefore, lv,
                        (now - watch.plannedCycle) * GAMESPEED_DEFAULT / 1000, now);
                break;
            }
        if (!watch.placed && now - watch.plannedCycle >= MILLI2CYCLES(L::kPlanExpiryMs)) {
            SDL_Log("LOW_VALUE_PALACE: house=%d event=expired x=%d y=%d low_lots=%d lv_before=%d cycle=%u",
                    houseID, watch.site.x, watch.site.y, int(watch.lots.size()), watch.lvBefore, now);
            watch = {};
        }
        return;
    }
    if (now - watch.placedCycle < MILLI2CYCLES(L::kEffectDelayMs)) return;
    int standing = 0;
    const int lv = lowValueLotsMean(watch.lots, &standing);
    bool palaceStands = false;
    for (const auto* structure : getStructureList())
        palaceStands |= structure->getOwner() == getHouse() && structure->getHealth() > 0
            && structure->getItemID() == Structure_Palace && structure->getLocation() == watch.site;
    // met=1: the Palace still stands and the lots rose by kMinRise or more (the activation registry's success).
    SDL_Log("LOW_VALUE_PALACE: house=%d event=effect met=%d x=%d y=%d low_lots=%d lots_standing=%d lv_before=%d lv_after=%d "
            "rise=%d palace_stands=%d cycle=%u", houseID, int(palaceStands && lv - watch.lvBefore >= L::kMinRise),
            watch.site.x, watch.site.y, int(watch.lots.size()), standing, watch.lvBefore, lv, lv - watch.lvBefore,
            int(palaceStands), now);
    watch = {};
}

bool QuantBot::planLowValuePalace(BuildPass& pass) {
    namespace L = CityLessonsPolicy;
    auto& watch = cityLessons.palace;
    const int price = currentGame->objectData.data[Structure_Palace][getHouse()->getHouseID()].price;
    // One site at a time, until its effect is read: a planned site stays the project (saved for)
    // until its Palace stands or the plan expires.
    if (watch.active) {
        if (watch.placed) return false;
        pass.economyProject = Structure_Palace;
        pass.economyProjectSite = watch.site;
        pass.economyProjectSpareOnly = false;
        pass.economyProjectReserve = price;
        return true;
    }
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
        if (std::max(std::abs(o.x - centre.x), std::abs(o.y - centre.y)) <= ForceConcentrationPolicy::kPalaceReachTiles
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
