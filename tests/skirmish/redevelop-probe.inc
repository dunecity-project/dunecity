// The four QuantBot redevelopment switches proven in a generated DuneCity match, played through the
// real engine (Stefan, 2026-11-27: "you need much much more tests to prove you have actually
// implemented a feature"; CLAUDE.md "AI behaviours: prove the switch actually does its job").
// One match is generated and saved; every arm loads that save, sets the QuantBot's switches and
// plays the same minutes, so ON and OFF differ only by the switch:
//   RoadLaneParks        ON orders Parks on spare-lane road tiles, they stand there, every road
//                        path between the tile's road neighbours stays open without the Park, and
//                        the lots they are for gain land value; OFF builds no Park.
//   LotParks             ON clears a cheap own lot for Parks that stand on it and lifts its
//                        neighbours; OFF clears nothing.
//   PollutionRelocation  a polluting Industrial zone is planted among the bot's homes in both arms;
//                        ON builds a new one clear of the homes, then demolishes the polluter, and
//                        those homes end with more land value than OFF, where the polluter stands.
//   IndustrySeparation   ON places Industrial zones, none within 5 tiles of an own R/C lot, and
//                        the bot's homes end with less pollution than OFF.
// Every count comes from QuantBot::getRedevelopmentActivity(), the counters the measured games log
// (SCENARIO_SWITCH_ACTIVITY). Included inside skirmish-probe.inc (SKIRMISH_PROBE_CASE=city-redevelop).

namespace RedevelopProbe {

namespace R = RedevelopmentPolicy;
using ParkProbe::ownLot;

struct Bot { QuantBot* bot = nullptr; House* house = nullptr; };

/// Checks after the setup are collected, so one run reports every switch; the probe fails at the end.
std::vector<std::string> failures;
void expect(bool ok, const std::string& what) {
    if (ok) return;
    SDL_Log("SKIRMISH_PROBE_CHECK_FAIL: %s", what.c_str());
    failures.push_back(what);
}

Bot findBot(Game& game) {
    Bot found;
    for(int h = 0; h < NUM_HOUSES && !found.bot; ++h)
        if(House* house = game.getHouse(h); house && house != pLocalHouse)
            for(const auto& player : house->getPlayerList())
                if(auto* bot = dynamic_cast<QuantBot*>(player.get())) { found = {bot, house}; break; }
    skirmishRequire(found.bot != nullptr, "No QuantBot opponent");
    return found;
}

std::unique_ptr<Game> load(const std::string& save) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(save));
    game->bReplay = false;
    game->initializeGameLoop();
    return game;
}

struct Switches { bool lane = false, lot = false, relocate = false, separate = false; };

void configure(QuantBot* bot, Switches s) {
    auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
    config->force.roadLaneParks = s.lane;
    config->force.lotParks = s.lot;
    config->force.pollutionRelocation = s.relocate;
    config->force.industrySeparation = s.separate;
    config->force.parkDistricts = false;
    bot->configOverride = config;
}

std::vector<Coord> lotOrigins(const House* house, int item = -1) {
    std::vector<Coord> lots;
    for(const StructureBase* s : structureList)
        if(ownLot(s, house) && (item < 0 || s->getItemID() == item)) lots.push_back(s->getLocation());
    return lots;
}

double meanLandValue(const DuneCity::CitySimulation& sim, const std::vector<Coord>& at) {
    if(at.empty()) return 0;
    long sum = 0;
    for(const Coord c : at) sum += sim.getLandValueMap().worldGet(c.x, c.y);
    return double(sum) / at.size();
}

double meanPollution(const DuneCity::CitySimulation& sim, const std::vector<Coord>& at) {
    if(at.empty()) return 0;
    long sum = 0;
    for(const Coord c : at) sum += sim.getPollutionDensityMap().worldGet(c.x, c.y);
    return double(sum) / at.size();
}

/// Nearest own R/C lot to an Industrial footprint at @a site (1 = touching).
int homeDistance(const House* house, Coord site) {
    int nearest = 1000000;
    for(const StructureBase* s : structureList)
        if(ownLot(s, house))
            nearest = std::min(nearest, CityPlacementPolicy::footprintDistance(site.x, site.y, 2, 2,
                s->getX(), s->getY(), s->getStructureSizeX(), s->getStructureSizeY()));
    return nearest;
}

/// Whether the road tiles beside @a park still reach one another over road alone (the Park itself
/// not counted), within a 9x9 window: the passage RoadLaneParks promises to keep.
bool roadNeighboursConnected(Coord park) {
    std::vector<Coord> ends;
    for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
        if((dx == 0) == (dy == 0)) continue;   // side neighbours only
        const int x = park.x + dx, y = park.y + dy;
        if(currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->isRoad()) ends.push_back(Coord(x, y));
    }
    if(ends.size() < 2) return true;
    std::set<std::pair<int, int>> seen{{ends[0].x, ends[0].y}};
    std::deque<Coord> open{ends[0]};
    while(!open.empty()) {
        const Coord c = open.front();
        open.pop_front();
        static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
        for(int i = 0; i < 4; ++i) {
            const int x = c.x + DX[i], y = c.y + DY[i];
            if(std::abs(x - park.x) > 4 || std::abs(y - park.y) > 4 || (x == park.x && y == park.y)) continue;
            if(!currentGameMap->tileExists(x, y) || !currentGameMap->getTile(x, y)->isRoad()) continue;
            if(seen.insert({x, y}).second) open.push_back(Coord(x, y));
        }
    }
    for(const Coord e : ends) if(!seen.count({e.x, e.y})) return false;
    return true;
}

struct Order { int kind; Coord site; Coord effectCentre; };

struct Run {
    R::Activity activity;
    std::vector<Order> orders;          ///< Every order the bot followed, as first seen.
    std::vector<Coord> parks;           ///< The bot's Parks at the end.
    std::vector<Coord> industry;        ///< The bot's Industrial zones at the end.
    std::vector<Coord> newIndustry;     ///< Industrial zones that were not standing at the start.
    std::map<std::pair<int, int>, int> landValue, pollution;   ///< At the end, by tile.
    double homeLandValue = 0, homePollution = 0;
    int homes = 0;
    bool plantStanding = false;
    int lotClearsParked = 0;            ///< LotClear orders whose site holds the bot's Park at the end.
    int relocationHomeDistance = 1000000;   ///< Nearest home to a relocated zone standing at its site.
};

/// A free 2x2 site touching the most own R/C lots of @a house where a zone may stand.
Coord polluterSite(const House* house) {
    Coord best = Coord::Invalid();
    int bestLots = 0;
    for(const StructureBase* s : structureList) {
        if(!ownLot(s, house)) continue;
        for(int y = s->getY() - 3; y <= s->getY() + 3; ++y)
            for(int x = s->getX() - 3; x <= s->getX() + 3; ++x) {
                if(!currentGameMap->okayToPlaceStructure(x, y, 2, 2, false, house, false, Structure_ZoneIndustrial)) continue;
                bool clear = true;
                for(int dy = 0; dy < 2 && clear; ++dy) for(int dx = 0; dx < 2 && clear; ++dx) {
                    const Tile* t = currentGameMap->getTile(x + dx, y + dy);
                    clear = !t->isRoad() && !t->hasAGroundObject() && !t->hasCityZone();
                }
                if(!clear) continue;
                int lots = 0;
                for(const StructureBase* l : structureList)
                    lots += ownLot(l, house) && CityPlacementPolicy::footprintDistance(x, y, 2, 2, l->getX(), l->getY(),
                        l->getStructureSizeX(), l->getStructureSizeY()) <= 3;
                if(lots > bestLots) { bestLots = lots; best = Coord(x, y); }
            }
    }
    return best;
}

Run play(const std::string& save, Switches switches, int minutes, bool plant, Coord* plantSite = nullptr) {
    auto game = load(save);
    Bot b = findBot(*game);
    configure(b.bot, switches);
    auto* sim = game->getCitySimulation();
    Uint32 plantID = NONE_ID;
    if(plant) {
        const Coord site = polluterSite(b.house);
        skirmishRequire(site.isValid(), "No free 2x2 site among the QuantBot's lots for the polluter");
        StructureBase* zone = b.house->placeStructure(NONE_ID, Structure_ZoneIndustrial, site.x, site.y, false, true);
        skirmishRequire(zone != nullptr, "Could not plant the Industrial zone among the bot's lots");
        const int level = DuneCity::getStructureMaxLevel(Structure_ZoneIndustrial);
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
            currentGameMap->getTile(site.x + dx, site.y + dy)->setCityZoneDensity(level);
        plantID = zone->getObjectID();
        if(plantSite) *plantSite = site;
        sim->runEffectsScans();
        SDL_Log("SKIRMISH_PROBE: redevelop planted polluter=(%d,%d) level=%d pollution=%d",
                site.x, site.y, level, sim->getPollutionDensityMap().worldGet(site.x, site.y));
    }
    std::set<Uint32> startIndustry;
    for(const StructureBase* s : structureList)
        if(s->getOwner() == b.house && s->getItemID() == Structure_ZoneIndustrial) startIndustry.insert(s->getObjectID());
    Run run;
    std::set<Uint32> seenOrders;
    const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(minutes * 60 * 1000);
    while(!game->finished && game->getGameCycleCount() < end) {
        game->updateGameState();
        if(game->getGameCycleCount() % MILLI2CYCLES(1000) != 0) continue;
        // The plant keeps its density while the switch decides (city growth would let it decay).
        if(plantID != NONE_ID && game->getObjectManager().getObject(plantID)) {
            const Coord p = game->getObjectManager().getObject(plantID)->getLocation();
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
                currentGameMap->getTile(p.x + dx, p.y + dy)->setCityZoneDensity(DuneCity::getStructureMaxLevel(Structure_ZoneIndustrial));
        }
        for(const auto& o : b.bot->redevelopmentOrders)
            if(seenOrders.insert(o.orderCycle * 8 + o.kind).second) {
                Coord centre = o.site;
                if(o.kind == R::Relocation)
                    if(const auto* old = game->getObjectManager().getObject(o.oldZone)) centre = old->getLocation();
                run.orders.push_back({o.kind, o.site, centre});
            }
    }
    skirmishRequire(!game->finished, "The match ended before the arm's minutes were played");
    run.activity = b.bot->getRedevelopmentActivity();
    run.plantStanding = plantID != NONE_ID && game->getObjectManager().getObject(plantID) != nullptr;
    for(const StructureBase* s : structureList) {
        if(s->getOwner() != b.house) continue;
        if(s->getItemID() == Structure_Park) run.parks.push_back(s->getLocation());
        if(s->getItemID() == Structure_ZoneIndustrial) {
            run.industry.push_back(s->getLocation());
            if(!startIndustry.count(s->getObjectID())) run.newIndustry.push_back(s->getLocation());
        }
    }
    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x) {
        run.landValue[{x, y}] = sim->getLandValueMap().worldGet(x, y);
        run.pollution[{x, y}] = sim->getPollutionDensityMap().worldGet(x, y);
    }
    const auto homes = lotOrigins(b.house);
    run.homes = int(homes.size());
    run.homeLandValue = meanLandValue(*sim, homes);
    run.homePollution = meanPollution(*sim, homes);
    for(const Order& o : run.orders) {
        const bool parked = std::find(run.parks.begin(), run.parks.end(), o.site) != run.parks.end();
        if(o.kind == R::LotClear && parked) ++run.lotClearsParked;
        if(o.kind == R::Relocation && std::find(run.industry.begin(), run.industry.end(), o.site) != run.industry.end()) {
            run.relocationHomeDistance = std::min(run.relocationHomeDistance, homeDistance(b.house, o.site));
            SDL_Log("SKIRMISH_PROBE: redevelop relocated to (%d,%d) from (%d,%d) home_distance=%d", o.site.x, o.site.y,
                    o.effectCentre.x, o.effectCentre.y, homeDistance(b.house, o.site));
        }
    }
    // Each lane Park keeps the road passage it stands on.
    if(switches.lane)
        for(const Order& o : run.orders)
            if(o.kind == R::LanePark && std::find(run.parks.begin(), run.parks.end(), o.site) != run.parks.end())
                expect(roadNeighboursConnected(o.site), "A lane Park cut a road path between its neighbours");
    // IndustrySeparation: no new Industrial zone next to the bot's homes, checked against the end map.
    if(switches.separate)
        for(const Coord c : run.newIndustry)
            SDL_Log("SKIRMISH_PROBE: redevelop separate new_industry=(%d,%d) home_distance=%d", c.x, c.y, homeDistance(b.house, c));
    SDL_Log("SKIRMISH_PROBE: redevelop arm lane=%d lot=%d relocate=%d separate=%d plant=%d parks=%zu industry=%zu "
            "new_industry=%zu homes=%d home_land_value=%.1f home_pollution=%.1f plant_standing=%d activity: %s",
            switches.lane, switches.lot, switches.relocate, switches.separate, plant, run.parks.size(),
            run.industry.size(), run.newIndustry.size(), run.homes, run.homeLandValue, run.homePollution,
            run.plantStanding, run.activity.format().c_str());
    endMatch(game);
    return run;
}

/// Mean end land value over the lots an arm's orders of @a kind were for, in @a on and @a off.
std::pair<double, double> effectAgainst(const Run& on, const Run& off, int kind, int reach,
                                        const std::vector<Coord>& startLots) {
    std::set<std::pair<int, int>> at;
    for(const Order& o : on.orders) {
        if(o.kind != kind) continue;
        for(const Coord l : startLots)
            if(std::max(std::abs(l.x - o.effectCentre.x), std::abs(l.y - o.effectCentre.y)) <= reach
               && !(kind == R::LotClear && l == o.site)) at.insert({l.x, l.y});
    }
    double a = 0, b = 0;
    for(const auto& p : at) { a += on.landValue.at(p); b += off.landValue.at(p); }
    return at.empty() ? std::make_pair(0.0, 0.0) : std::make_pair(a / at.size(), b / at.size());
}

void requireActed(const Run& run, int kind, const char* name) {
    const auto& a = run.activity;
    expect(a.ordered[kind] >= 1, std::string(name) + ": the switch never ordered");
    expect(a.placed[kind] >= 1, std::string(name) + ": no order stood at its chosen site");
    expect(a.lost[kind] * 2 <= a.ordered[kind], std::string(name) + ": most orders were lost or re-sited");
    expect(a.checked[kind] >= 1 && a.raised[kind] * 2 >= a.checked[kind],
                    std::string(name) + ": the lots it was for did not gain land value");
}

void requireIdle(const Run& run, const char* name) {
    for(int k = 0; k < R::KindCount; ++k)
        expect(run.activity.ordered[k] == 0, std::string(name) + ": a redevelopment order with every switch off");
}

void runRedevelopProbe() {
    const Params params = caseParams("city-redevelop");
    skirmishRequire(RandomSkirmish::activateMod(params.mod), "Case mod did not activate");
    const Result result = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, params);
    init.randomSeed = params.seed;
    std::string save = probeOut("city-redevelop.dls");
    std::vector<Coord> startLots;
    {
        auto game = startMatch(init);
        checkLoaded(*game, result, params);
        auto* sim = game->getCitySimulation();
        skirmishRequire(sim != nullptr, "City simulation missing in a DuneCity match");
        // Let the city settle so land value and pollution are live readings.
        const Uint32 settle = game->getGameCycleCount() + MILLI2CYCLES(60 * 1000);
        while(game->getGameCycleCount() < settle || sim->pendingGrowthPhase_) game->updateGameState();
        Bot b = findBot(*game);
        startLots = lotOrigins(b.house);
        int cheap = 0, squares = 0, industry = 0;
        for(const Coord l : startLots)
            cheap += sim->getLandValueMap().worldGet(l.x, l.y) <= R::kLotParkMaxValue
                && currentGameMap->getTile(l.x, l.y)->getCityZoneDensity() <= R::kLotParkMaxDensity;
        for(const StructureBase* s : structureList) industry += s->getOwner() == b.house && s->getItemID() == Structure_ZoneIndustrial;
        const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
        for(int y = 0; y + 1 < h; ++y) for(int x = 0; x + 1 < w; ++x) {
            bool all = true;
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) all = all && currentGameMap->getTile(x + dx, y + dy)->isRoad();
            squares += all;
        }
        SDL_Log("SKIRMISH_PROBE: redevelop start house=%d lots=%zu cheap_lots=%d industry=%d road_squares=%d "
                "land_value=%.1f pollution=%.1f credits=%d", b.house->getHouseID(), startLots.size(), cheap, industry,
                squares, meanLandValue(*sim, startLots), meanPollution(*sim, startLots), b.house->getCredits());
        skirmishRequire(startLots.size() >= 8, "The QuantBot has too few R/C lots for a redevelopment test");
        skirmishRequire(game->saveGame(save), "Could not save the redevelopment start");
        endMatch(game);
    }
    constexpr int kMinutes = 6;

    // Every switch off: the baseline every ON arm is read against, and proof nothing fires.
    const Run off = play(save, {}, kMinutes, false);
    requireIdle(off, "all off");
    expect(off.parks.empty(), "The bot built a Park with every park switch off");

    // RoadLaneParks.
    Switches lane; lane.lane = true;
    const Run laneOn = play(save, lane, kMinutes, false);
    requireActed(laneOn, R::LanePark, "RoadLaneParks");
    const auto laneEffect = effectAgainst(laneOn, off, R::LanePark, R::kParkEffectReach, startLots);
    SDL_Log("SKIRMISH_PROBE: redevelop lane effect on=%.1f off=%.1f", laneEffect.first, laneEffect.second);
    expect(laneEffect.first > laneEffect.second, "RoadLaneParks: its lots end with no more land value than OFF");

    // LotParks.
    Switches lot; lot.lot = true;
    const Run lotOn = play(save, lot, kMinutes, false);
    requireActed(lotOn, R::LotClear, "LotParks (clear)");
    expect(lotOn.lotClearsParked >= 1, "LotParks: no cleared lot holds its Park");
    const auto lotEffect = effectAgainst(lotOn, off, R::LotClear, R::kParkEffectReach, startLots);
    SDL_Log("SKIRMISH_PROBE: redevelop lot effect on=%.1f off=%.1f", lotEffect.first, lotEffect.second);
    expect(lotEffect.first > lotEffect.second, "LotParks: the cleared lot's neighbours end with no more land value than OFF");

    // PollutionRelocation, with the same polluter planted in both arms.
    Coord plantSite = Coord::Invalid();
    const Run relocOff = play(save, {}, kMinutes, true, &plantSite);
    requireIdle(relocOff, "relocation off");
    expect(relocOff.plantStanding, "With PollutionRelocation off the planted polluter went away");
    Switches relocate; relocate.relocate = true;
    const Run relocOn = play(save, relocate, kMinutes, true);
    requireActed(relocOn, R::Relocation, "PollutionRelocation");
    expect(!relocOn.plantStanding, "PollutionRelocation left the planted polluter standing");
    expect(R::industryTooCloseToHomes(relocOn.relocationHomeDistance) == false,
                    "PollutionRelocation: the replacement zone stands within 5 tiles of a home");
    std::vector<Coord> plantLots;
    for(const Coord l : startLots)
        if(std::max(std::abs(l.x - plantSite.x), std::abs(l.y - plantSite.y)) <= R::kRelocationEffectReach) plantLots.push_back(l);
    double onValue = 0, offValue = 0, onPollution = 0, offPollution = 0;
    for(const Coord l : plantLots) {
        onValue += relocOn.landValue.at({l.x, l.y}); offValue += relocOff.landValue.at({l.x, l.y});
        onPollution += relocOn.pollution.at({l.x, l.y}); offPollution += relocOff.pollution.at({l.x, l.y});
    }
    SDL_Log("SKIRMISH_PROBE: redevelop relocation lots=%zu land_value on=%.1f off=%.1f pollution on=%.1f off=%.1f",
            plantLots.size(), onValue / std::max<size_t>(1, plantLots.size()), offValue / std::max<size_t>(1, plantLots.size()),
            onPollution / std::max<size_t>(1, plantLots.size()), offPollution / std::max<size_t>(1, plantLots.size()));
    expect(!plantLots.empty() && onPollution < offPollution, "PollutionRelocation: the polluter's neighbours are no cleaner than OFF");
    expect(onValue > offValue, "PollutionRelocation: the polluter's neighbours end with no more land value than OFF");

    // IndustrySeparation.
    Switches separate; separate.separate = true;
    const Run sepOn = play(save, separate, kMinutes, false);
    requireIdle(sepOn, "IndustrySeparation (no redevelopment orders)");
    expect(sepOn.activity.industryPlaced >= 1, "IndustrySeparation: the bot placed no Industrial zone to test");
    expect(sepOn.activity.industryNearHomes == 0, "IndustrySeparation: an Industrial zone landed within 5 tiles of a home");
    SDL_Log("SKIRMISH_PROBE: redevelop separation industry on=%d (near %d) off=%d (near %d) home_pollution on=%.1f off=%.1f "
            "home_land_value on=%.1f off=%.1f", sepOn.activity.industryPlaced, sepOn.activity.industryNearHomes,
            off.activity.industryPlaced, off.activity.industryNearHomes, sepOn.homePollution, off.homePollution,
            sepOn.homeLandValue, off.homeLandValue);
    expect(off.activity.industryNearHomes >= 1, "IndustrySeparation: OFF placed no Industrial zone near a home, so the test proves nothing");
    expect(sepOn.homePollution < off.homePollution, "IndustrySeparation: the homes are no cleaner than OFF");
    expect(sepOn.homeLandValue > off.homeLandValue, "IndustrySeparation: the homes end with no more land value than OFF");
    std::string all;
    for (const auto& f : failures) all += (all.empty() ? "" : "; ") + f;
    skirmishRequire(failures.empty(), all);
}

} // namespace RedevelopProbe
