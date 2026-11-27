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
                    "pending_passes=%d saving_passes=%d preempted_passes=%d ordered_passes=%d held_passes=%d cycle=%u",
                    tag, houseID, watch.site.x, watch.site.y, int(watch.lots.size()), watch.lvBefore,
                    bot.getHouse()->getCredits(), price, watch.projectPasses, watch.pendingPasses, watch.savingPasses,
                    watch.preemptedPasses, watch.orderedPasses, watch.heldPasses, now);
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
