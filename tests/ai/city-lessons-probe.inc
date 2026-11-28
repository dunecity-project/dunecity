// Real-engine activation test of the two QuantBot switches taken from Stefan's Metropolis save
// seewhatidid (CityLessonsPolicy.h, QuantBotCityLessons.cpp). Included only by run-campaign-balance.py
// --city-lessons-probe; never linked into the game. CTest quantbot_city_lessons (headless, nightly).
//
// Stefan, 2026-11-27: "make sure you are actually doing what is asked and not just imagining you are
// doing it". On the bundled 4P Metropolis city map, four QuantBot Brutal houses: house ON has
// CityLauncherArmy and LowValuePalaces on, house OFF has both off (the shipped default), the other two
// play as shipped. The game runs through the real build passes and game cycles, and the test asserts:
//
//  CityLauncherArmy
//   1. ON acts: its first build pass after the switch holds the mix (passes counted) with the Launcher
//      share at least the floor and no Ornithopter share; OFF never acts.
//   2. ON reaches its effect: in the 10 game minutes Launchers are at least half the vehicle value the
//      house builds and it builds no Ornithopter. OFF's own mix is not touched.
//  LowValuePalaces
//   3. ON plans a Palace on a site with at least kMinLowLots own lots of land value <= kLowLandValue
//      (counted in the land-value map, not taken from the plan); OFF plans none.
//   4. ON's Palace stands on that site (same location, not placed elsewhere), and two minutes later
//      the mean land value of those lots has risen by at least kMinRise.
//   5. ON does not plan the same lots again once they are no longer low.
//  FrontPalaces (a third house, FRONT, only this switch on; Stefan's placement)
//   6. FRONT plans a Palace whose centre the probe itself finds 10-31 tiles from the nearest enemy
//      building with at least 3 own Rocket Turrets within 10 tiles that stand nearer the enemy; OFF
//      plans none.
//   7. That Palace stands on its planned site and still stands 5 minutes later (met=1).
#include <players/CityLessonsPolicy.h>
#include <players/QuantBotBuildPass.h>

void runCityLessonsProbe(Game& game) {
    namespace L = CityLessonsPolicy;
    auto require = [](bool ok, const char* why) {
        if (!ok) { SDL_Log("CITY_LESSONS_PROBE_FAIL: %s", why); throw std::runtime_error(why); }
    };
    require(game.isCitySimEnabled(), "Run the city lessons fixture with the Dune City mod");
    game.gameType = GameType::CustomGame;
    std::vector<QuantBot*> bots;
    for (int h = 0; h < NUM_HOUSES; ++h)
        if (House* house = game.getHouse(h))
            for (const auto& player : house->getPlayerList())
                if (auto* bot = dynamic_cast<QuantBot*>(player.get())) bots.push_back(bot);
    require(bots.size() >= 3, "The fixture needs three QuantBot city houses");
    QuantBot* on = bots[0];
    QuantBot* off = bots[1];
    QuantBot* frontBot = bots[2];
    for (auto* bot : {on, off, frontBot}) {
        bot->gameMode = QuantBot::GameMode::Custom;
        bot->difficulty = QuantBot::Difficulty::Brutal;
        require(!bot->botConfig().force.cityLauncherArmy && !bot->botConfig().force.lowValuePalaces
                && !bot->botConfig().force.frontPalaces,
                "The switches are not off by default");
        require(bot->botConfig().force.palaceDistricts, "PalaceDistricts is not on by default");
    }
    // ON plays its own copy of the configuration with both switches on.
    auto onConfig = std::make_shared<QuantBotConfig>(on->botConfig());
    onConfig->force.cityLauncherArmy = true;
    onConfig->force.lowValuePalaces = true;
    on->configOverride = onConfig;
    auto frontConfig = std::make_shared<QuantBotConfig>(frontBot->botConfig());
    frontConfig->force.frontPalaces = true;
    frontBot->configOverride = frontConfig;
    require(frontBot->frontPalacesEnabled() && !on->frontPalacesEnabled() && !off->frontPalacesEnabled(),
            "FrontPalaces did not reach the bots");
    House* frontHouse = const_cast<House*>(frontBot->getHouse());
    frontHouse->addCredits(20000);
    require(on->cityLauncherArmyEnabled() && on->lowValuePalacesEnabled() && !off->cityLauncherArmyEnabled()
            && !off->lowValuePalacesEnabled(), "The switches did not reach the bots");
    House* onHouse = const_cast<House*>(on->getHouse());
    House* offHouse = const_cast<House*>(off->getHouse());
    SDL_Log("CITY_LESSONS_PROBE: on=%d off=%d", onHouse->getHouseID(), offHouse->getHouseID());

    // Built value per type before the run, to measure what the 14 minutes add.
    auto built = [&](const House* house, Uint32 item) {
        return int64_t(house->getNumBuiltItems(item)) * game.objectData.data[item][house->getHouseID()].price;
    };
    auto vehicleBuilt = [&](const House* house) {
        int64_t sum = 0;
        for (const Uint32 item : QuantBot::kMixItems) sum += built(house, item);
        return sum;
    };
    const int64_t onVehicles0 = vehicleBuilt(onHouse), onLaunchers0 = built(onHouse, Unit_Launcher);
    const int onOrni0 = onHouse->getNumBuiltItems(Unit_Ornithopter);
    const int64_t offVehicles0 = vehicleBuilt(offHouse), offLaunchers0 = built(offHouse, Unit_Launcher);

    // 1. The first game minute through the real update loop (the bots initialise on their first
    //    update): ON holds the mix in its build passes, OFF never does.
    require(on->cityLessons.launcherArmyPasses == 0 && off->cityLessons.launcherArmyPasses == 0, "Passes before the run");
    const Uint32 firstMinute = game.getGameCycleCount() + MILLI2CYCLES(60000);
    while (game.getGameCycleCount() < firstMinute && !game.finished) game.updateGameState();
    SDL_Log("CITY_LESSONS_PROBE: first minute on passes=%d launcher_bps=%d ornithopter_bps=%d off passes=%d launcher_bps=%d ornithopter_bps=%d",
            on->cityLessons.launcherArmyPasses, on->lastUnitMixBps[L::kLauncherSlot], on->lastUnitMixBps[UnitMixPolicy::kOrnithopterSlot],
            off->cityLessons.launcherArmyPasses, off->lastUnitMixBps[L::kLauncherSlot], off->lastUnitMixBps[UnitMixPolicy::kOrnithopterSlot]);
    require(on->cityLessons.launcherArmyPasses > 0, "ON's build passes did not hold the Launcher mix in the first minute");
    require(on->lastUnitMixBps[L::kLauncherSlot] >= L::kLauncherArmyBps,
            "ON's Launcher share is below the CityLauncherArmy floor");
    require(on->lastUnitMixBps[UnitMixPolicy::kOrnithopterSlot] == 0, "ON's mix still has an Ornithopter share");
    require(off->cityLessons.launcherArmyPasses == 0, "OFF's build passes held the Launcher mix");

    // 3-5 are read as the game runs: the first plan, its placement and its effect.
    auto* sim = game.getCitySimulation();
    struct Seen { bool planned = false, placed = false, effect = false; Coord site = Coord::Invalid();
                  std::vector<Coord> lots; int lvBefore = 0, lvAfter = 0; Uint32 placedCycle = 0;
                  bool nextBeforeEffect = false; } seen;
    struct FrontSeen { bool planned = false, placed = false, survived = false; Coord site = Coord::Invalid();
                       Uint32 placedCycle = 0; } front;
    // The probe's own reading of a front site: nearest enemy building and own turrets ahead of it.
    auto frontReading = [&](Coord centre, int* enemyTiles, int* ahead) {
        auto nearestEnemy = [&](Coord c) {
            int best = 9999;
            for (const auto* s : structureList) {
                if (!s->getOwner() || s->getOwner()->getTeamID() == frontHouse->getTeamID() || s->getItemID() == Structure_Wall) continue;
                const Coord o = s->getLocation(), z = s->getStructureSize();
                for (int y = o.y; y < o.y + z.y; ++y) for (int x = o.x; x < o.x + z.x; ++x)
                    best = std::min(best, std::max(std::abs(x - c.x), std::abs(y - c.y)));
            }
            return best;
        };
        *enemyTiles = nearestEnemy(centre);
        *ahead = 0;
        for (const auto* s : structureList)
            if (s->getOwner() == frontHouse && s->getItemID() == Structure_RocketTurret
                && std::max(std::abs(s->getLocation().x - centre.x), std::abs(s->getLocation().y - centre.y)) <= L::kFrontPalaceCoverTiles
                && nearestEnemy(s->getLocation()) < *enemyTiles) ++*ahead;
    };
    const Uint32 end = game.getGameCycleCount() + MILLI2CYCLES(14 * 60000);
    onHouse->addCredits(20000);   // the Palace is saved for; make the saving short, not the choice
    while (game.getGameCycleCount() < end && !game.finished) {
        game.updateGameState();
        const auto& watch = on->cityLessons.palace;
        if (!seen.planned && watch.active) {
            seen.planned = true;
            seen.site = watch.site;
            seen.lots = watch.lots;
            seen.lvBefore = watch.lvBefore;
            // 3. Count the low lots in reach in the land-value map itself.
            const Coord centre = watch.site + Coord(1, 1);
            int low = 0;
            for (const auto* s : structureList) {
                if (s->getOwner() != onHouse || (s->getItemID() != Structure_ZoneResidential
                    && s->getItemID() != Structure_ZoneCommercial)) continue;
                const Coord o = s->getLocation();
                if (std::max(std::abs(o.x - centre.x), std::abs(o.y - centre.y)) <= ForceConcentrationPolicy::kPalaceReachTiles
                    && L::lowLot(sim->getLandValueMap().worldGet(o.x, o.y))) ++low;
            }
            SDL_Log("CITY_LESSONS_PROBE: planned site=(%d,%d) low_lots=%d counted=%d lv_before=%d cycle=%u",
                    watch.site.x, watch.site.y, int(watch.lots.size()), low, watch.lvBefore, game.getGameCycleCount());
            require(low >= L::kMinLowLots, "The planned Palace site has fewer low-value lots than the minimum");
        }
        // The bot reports a placement by moving the watch to palaceEffects (the next plan goes ahead at once).
        auto reportedPlaced = [](const auto& effects, Coord site) {
            return std::any_of(effects.begin(), effects.end(), [&](const auto& e) { return e.site == site; });
        };
        if (seen.planned && !seen.placed && reportedPlaced(on->cityLessons.palaceEffects, seen.site)) {
            seen.placed = true;
            seen.placedCycle = game.getGameCycleCount();
            bool stands = false;
            for (const auto* s : structureList)
                stands |= s->getOwner() == onHouse && s->getItemID() == Structure_Palace && s->getLocation() == seen.site;
            require(stands, "The Palace the bot reports placed is not on the planned site");
        }
        if (seen.placed && !seen.effect && game.getGameCycleCount() >= seen.placedCycle + MILLI2CYCLES(L::kEffectDelayMs)) {
            seen.effect = true;
            seen.lvAfter = on->lowValueLotsMean(seen.lots, nullptr);
            SDL_Log("CITY_LESSONS_PROBE: effect lv_before=%d lv_after=%d", seen.lvBefore, seen.lvAfter);
        }
        if (seen.placed && on->cityLessons.palace.active && !on->cityLessons.palace.placed) {
            // 5. A later plan is not the same site, and while the first effect is pending not the same lots.
            require(on->cityLessons.palace.site != seen.site, "The bot planned the same Palace site again");
            if (!seen.effect) {
                if (!seen.nextBeforeEffect) {
                    seen.nextBeforeEffect = true;
                    SDL_Log("CITY_LESSONS_PROBE: next plan before the first effect site=(%d,%d) cycle=%u",
                            on->cityLessons.palace.site.x, on->cityLessons.palace.site.y, game.getGameCycleCount());
                }
                for (const Coord lot : on->cityLessons.palace.lots)
                    require(std::find(seen.lots.begin(), seen.lots.end(), lot) == seen.lots.end(),
                            "A plan made while the first Palace's effect was pending reuses its lots");
            }
        }
        const auto& fw = frontBot->cityLessons.front;
        if (!front.planned && fw.active) {
            front.planned = true;
            front.site = fw.site;
            int enemyTiles = 0, ahead = 0;
            frontReading(fw.site + Coord(1, 1), &enemyTiles, &ahead);
            SDL_Log("CITY_LESSONS_PROBE: front planned site=(%d,%d) bot enemy_tiles=%d turrets_ahead=%d probe enemy_tiles=%d turrets_ahead=%d lots=%d",
                    fw.site.x, fw.site.y, fw.enemyTiles, fw.turretsAhead, enemyTiles, ahead, int(fw.lots.size()));
            require(enemyTiles >= L::kFrontPalaceMinEnemyTiles && enemyTiles <= L::kFrontPalaceMaxEnemyTiles,
                    "The front Palace site is not 10-31 tiles from the nearest enemy building");
            require(ahead >= L::kFrontPalaceTurretsAhead, "The front Palace site has fewer than 3 own turrets ahead of it");
            require(int(fw.lots.size()) >= L::kFrontPalaceMinLots, "The front Palace site reaches too few own lots");
        }
        if (front.planned && !front.placed && reportedPlaced(frontBot->cityLessons.frontEffects, front.site)) {
            front.placed = true;
            front.placedCycle = game.getGameCycleCount();
            require(frontBot->ownPalaceAt(front.site), "The front Palace the bot reports placed is not on its site");
        }
        if (front.placed && !front.survived && game.getGameCycleCount() >= front.placedCycle + MILLI2CYCLES(L::kFrontPalaceSurvivalMs)) {
            front.survived = frontBot->ownPalaceAt(front.site);
            SDL_Log("CITY_LESSONS_PROBE: front Palace at (%d,%d) standing after 5 minutes: %d", front.site.x, front.site.y,
                    int(front.survived));
            require(front.survived, "The front Palace did not survive 5 minutes");
        }
        require(off->cityLessons.frontPlanned == 0 && on->cityLessons.frontPlanned == 0, "A house without FrontPalaces planned one");
        require(off->cityLessons.palacesPlanned == 0, "OFF planned a low-value Palace");
        require(off->cityLessons.launcherArmyPasses == 0, "OFF held the Launcher mix");
    }
    // 2. What ON built in the 14 minutes.
    const int64_t onVehicles = vehicleBuilt(onHouse) - onVehicles0, onLaunchers = built(onHouse, Unit_Launcher) - onLaunchers0;
    const int onOrni = onHouse->getNumBuiltItems(Unit_Ornithopter) - onOrni0;
    const int64_t offVehicles = vehicleBuilt(offHouse) - offVehicles0, offLaunchers = built(offHouse, Unit_Launcher) - offLaunchers0;
    SDL_Log("CITY_LESSONS_PROBE: built on vehicles=%lld launchers=%lld ornithopters=%d passes=%d off vehicles=%lld launchers=%lld",
            (long long)onVehicles, (long long)onLaunchers, onOrni, on->cityLessons.launcherArmyPasses,
            (long long)offVehicles, (long long)offLaunchers);
    require(on->cityLessons.launcherArmyPasses > 10, "ON held the Launcher mix in too few build passes");
    require(onVehicles > 0, "ON built no vehicles in fourteen minutes");
    require(onLaunchers * 2 >= onVehicles, "Launchers are less than half of the vehicle value ON built");
    require(onOrni == 0, "ON built an Ornithopter with CityLauncherArmy on");
    require(seen.planned, "ON never planned a low-value Palace");
    require(seen.placed, "ON's planned Palace never stood on its site");
    require(seen.effect, "ON's Palace effect was never read");
    require(seen.lvAfter - seen.lvBefore >= L::kMinRise, "The low lots' land value did not rise after the Palace");
    require(front.planned, "FRONT never planned a front Palace");
    require(front.placed, "FRONT's planned Palace never stood on its site");
    require(front.survived, "FRONT's Palace was not checked 5 minutes after placement");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_lessons outcome=passed");
}
