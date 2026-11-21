// The Park (Micropolis park tool, item 89, save 9888) in a generated DuneCity match, played through
// the real engine: the player's Construction Yard offers and builds it, CMD_PLACE_STRUCTURE places it
// next to the player's lots, it raises their land value through park terrain without developing its
// own block or paving a road ring, it is drawn, saved and reloaded; then a QuantBot with
// [Force Concentration] ParkDistricts plants parks among its own R/C lots.
// Included inside skirmish-probe.inc (runs as SKIRMISH_PROBE_CASE=city-park).

// Its headers are included at the top of skirmish-probe.inc, outside the anonymous namespace.

namespace ParkProbe {

const StructureBase* parkAt(Coord p) {
    const ObjectBase* object = currentGameMap->getTile(p.x, p.y)->getGroundObject();
    return object && object->getItemID() == Structure_Park ? static_cast<const StructureBase*>(object) : nullptr;
}

bool ownLot(const StructureBase* s, const House* house) {
    return s->getOwner() == house && s->getHealth() > 0
        && (s->getItemID() == Structure_ZoneResidential || s->getItemID() == Structure_ZoneCommercial);
}

/// Land value one park at @a site adds, summed over the 2x2 land-value blocks of @a house's R/C lots.
int lotGain(const DuneCity::CitySimulation& sim, const House* house, Coord site) {
    std::set<std::pair<int, int>> blocks;
    for(const StructureBase* s : structureList) {
        if(!ownLot(s, house)) continue;
        for(int dy = 0; dy < s->getStructureSizeY(); ++dy)
            for(int dx = 0; dx < s->getStructureSizeX(); ++dx)
                blocks.insert({(s->getX() + dx) / 2, (s->getY() + dy) / 2});
    }
    int gain = 0;
    for(const auto& [bx, by] : blocks)
        gain += sim.getParkTerrain().marginalGain(site.x, site.y, DuneCity::kParkLandValueBonus, bx * 2, by * 2);
    return gain;
}

/// Open rock or concrete beside @a house's lots, outside any road, where a park adds the most.
Coord chooseSite(const DuneCity::CitySimulation& sim, const House* house, int& bestGain, Coord& bestLot) {
    Coord best = Coord::Invalid();
    bestGain = 0;
    for(const StructureBase* s : structureList) {
        if(!ownLot(s, house)) continue;
        for(int y = s->getY() - 2; y < s->getY() + s->getStructureSizeY() + 2; ++y)
            for(int x = s->getX() - 2; x < s->getX() + s->getStructureSizeX() + 2; ++x) {
                if(!currentGameMap->tileExists(x, y)) continue;
                const Tile* tile = currentGameMap->getTile(x, y);
                if(tile->isRoad() || tile->hasAGroundObject()
                   || !currentGameMap->okayToPlaceStructure(x, y, 1, 1, false, house, false, Structure_Park)) continue;
                const int gain = lotGain(sim, house, Coord(x, y));
                if(gain > bestGain) { bestGain = gain; best = Coord(x, y); bestLot = s->getLocation(); }
            }
    }
    return best;
}

void scan(DuneCity::CitySimulation& sim, const DuneCity::CityMapLayer<uint16_t>& crime) {
    sim.crimeRateMap_ = crime;   // the same previous-scan crime feedback for both readings
    sim.runEffectsScans();
}

void runParkProbe() {
    const Params params = caseParams("city-park");
    skirmishRequire(RandomSkirmish::activateMod(params.mod), "Case mod did not activate");
    const Result result = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, params);
    init.randomSeed = params.seed;
    auto game = startMatch(init);
    checkLoaded(*game, result, params);
    auto* sim = game->getCitySimulation();
    skirmishRequire(sim != nullptr, "City simulation missing in a DuneCity match");
    while(sim->lastProcessedDay_ < 2 || sim->pendingGrowthPhase_) game->updateGameState();

    House* house = game->getHouse(result.houses[0].house);
    skirmishRequire(house != nullptr && house == pLocalHouse, "The player's house is not the local human");
    const int houseID = house->getHouseID();
    const auto& stats = game->objectData.data[Structure_Park][houseID];
    skirmishRequire(stats.enabled && stats.price == 10 && stats.power == 0 && stats.buildtime == 8,
                    "Park ObjectData is not the Micropolis tool (enabled, 10 credits, no power)");
    ConstructionYard* yard = nullptr;
    for(StructureBase* s : structureList)
        if(s->getOwner() == house && s->getItemID() == Structure_ConstructionYard) yard = static_cast<ConstructionYard*>(s);
    skirmishRequire(yard != nullptr, "The player has no Construction Yard");
    skirmishRequire(std::any_of(yard->getBuildList().begin(), yard->getBuildList().end(),
                                [](const BuildItem& item) { return item.itemID == Structure_Park; }),
                    "The Construction Yard does not offer a Park in city mode");

    // Build it like a player: order, wait, place through the network command.
    yard->doProduceItem(Structure_Park);
    const Uint32 orderDeadline = game->getGameCycleCount() + MILLI2CYCLES(30 * 1000);
    while(!yard->isWaitingToPlace() && game->getGameCycleCount() < orderDeadline) game->updateGameState();
    skirmishRequire(yard->isWaitingToPlace(), "The Park did not finish building in 30 seconds");

    int expectedGain = 0;
    Coord lot = Coord::Invalid();
    const Coord site = chooseSite(*sim, house, expectedGain, lot);
    skirmishRequire(site.isValid() && expectedGain > 0, "No open tile beside the player's lots raises their land value");
    const int lotBlockBefore = sim->getLandValueMap().worldGet(lot.x, lot.y);
    const int lotTerrainBefore = sim->getParkTerrain().landValueContribution(lot.x, lot.y);
    const int structuresBefore = house->getNumStructures();
    bool roadsBefore[3][3];
    for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx)
        roadsBefore[dy + 1][dx + 1] = currentGameMap->tileExists(site.x + dx, site.y + dy)
            && currentGameMap->getTile(site.x + dx, site.y + dy)->isRoad();
    const auto crime = sim->crimeRateMap_;
    scan(*sim, crime);
    const int lotBefore = sim->getLandValueMap().worldGet(lot.x, lot.y);

    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,
        yard->getObjectID(), static_cast<Uint32>(site.x), static_cast<Uint32>(site.y)));
    const Uint32 placeDeadline = game->getGameCycleCount() + MILLI2CYCLES(5 * 1000);
    while(!parkAt(site) && game->getGameCycleCount() < placeDeadline) game->updateGameState();
    const StructureBase* park = parkAt(site);
    skirmishRequire(park != nullptr, "CMD_PLACE_STRUCTURE did not place the Park");
    skirmishRequire(park->getOwner() == house && park->getStructureSizeX() == 1 && park->getStructureSizeY() == 1,
                    "The Park is not a 1x1 structure of the player's house");
    skirmishRequire(park->getHealth() == park->getMaxHealth(), "The Park lost health on bare ground (it needs no concrete)");
    skirmishRequire(house->getNumItems(Structure_Park) == 1 && house->getNumStructures() == structuresBefore + 1,
                    "The house does not count its Park");
    for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
        if(dx == 0 && dy == 0) continue;
        if(!currentGameMap->tileExists(site.x + dx, site.y + dy)) continue;
        skirmishRequire(currentGameMap->getTile(site.x + dx, site.y + dy)->isRoad() == roadsBefore[dy + 1][dx + 1],
                        "Placing a Park paved a road ring (Micropolis parks have no frontage)");
    }

    scan(*sim, crime);
    const int lotAfter = sim->getLandValueMap().worldGet(lot.x, lot.y);
    const int lotTerrainAfter = sim->getParkTerrain().landValueContribution(lot.x, lot.y);
    skirmishRequire(lotTerrainAfter > lotTerrainBefore, "The Park added no park terrain to the neighbouring lot");
    skirmishRequire(lotAfter > lotBefore, "The Park did not raise the neighbouring lot's land value");
    // A Micropolis park does not develop its own block: a block holding only the park stays 0.
    bool parkBlockOnlyPark = true;
    const int bx = site.x / 2 * 2, by = site.y / 2 * 2;
    for(int y = by; y < by + 2; ++y) for(int x = bx; x < bx + 2; ++x) {
        if(!currentGameMap->tileExists(x, y) || (x == site.x && y == site.y)) continue;
        const Tile* tile = currentGameMap->getTile(x, y);
        if(tile->isRoad() || tile->hasAStructure()) parkBlockOnlyPark = false;
    }
    if(parkBlockOnlyPark)
        skirmishRequire(sim->getLandValueMap().worldGet(site.x, site.y) == 0, "A park-only block was developed");
    SDL_Log("SKIRMISH_PROBE: park placed=(%d,%d) lot=(%d,%d) expected_gain=%d lot_value %d->%d (live %d) terrain %d->%d park_only_block=%d",
            site.x, site.y, lot.x, lot.y, expectedGain, lotBefore, lotAfter, lotBlockBefore, lotTerrainBefore,
            lotTerrainAfter, parkBlockOnlyPark ? 1 : 0);
    captureGame(*game, "ingame-park", site);

    // Saved and loaded like any structure; the Park row and its terrain survive.
    const FixPoint health = park->getHealth();
    const std::string save = probeOut("city-park.dls");
    skirmishRequire(game->saveGame(save), "Could not save the match with a Park");
    endMatch(game);
    auto reloaded = std::make_unique<Game>();
    currentGame = reloaded.get();
    reloaded->bReplay = true;
    reloaded->initGame(GameInitSettings(save));
    reloaded->bReplay = false;
    reloaded->initializeGameLoop();
    const StructureBase* loadedPark = parkAt(site);
    skirmishRequire(loadedPark != nullptr && loadedPark->getOwner()->getHouseID() == houseID
                    && loadedPark->getHealth() == health, "The Park did not survive save and load");
    skirmishRequire(reloaded->getHouse(houseID)->getNumItems(Structure_Park) == 1, "The loaded house lost its Park count");
    skirmishRequire(reloaded->objectData.data[Structure_Park][houseID].price == 10
                    && reloaded->objectData.data[Structure_Park][houseID].enabled, "The Park row did not survive the save");
    auto* loadedSim = reloaded->getCitySimulation();
    loadedSim->runEffectsScans();
    skirmishRequire(loadedSim->getParkTerrain().landValueContribution(lot.x, lot.y) == lotTerrainAfter,
                    "The loaded game rebuilt different park terrain");
    SDL_Log("SKIRMISH_PROBE: park save/load kept (%d,%d) health=%d", site.x, site.y, health.lround());

    // QuantBot with ParkDistricts plants parks among its own R/C lots.
    QuantBot* bot = nullptr;
    const House* botHouse = nullptr;
    for(int h = 0; h < NUM_HOUSES && !bot; ++h)
        if(const House* other = reloaded->getHouse(h); other && other != reloaded->getHouse(houseID))
            for(const auto& player : other->getPlayerList())
                if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) { bot = candidate; botHouse = other; break; }
    skirmishRequire(bot != nullptr, "No QuantBot opponent");
    auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
    config->force.parkDistricts = true;
    bot->configOverride = config;
    const Uint32 aiDeadline = reloaded->getGameCycleCount() + MILLI2CYCLES(4 * 60 * 1000);
    while(!reloaded->finished && reloaded->getGameCycleCount() < aiDeadline
          && botHouse->getNumItems(Structure_Park) < 3) reloaded->updateGameState();
    int lots = 0, parks = 0, nearLots = 0;
    Coord firstPark = Coord::Invalid();
    for(const StructureBase* s : structureList) lots += ownLot(s, botHouse);
    for(const StructureBase* s : structureList) {
        if(s->getOwner() != botHouse || s->getItemID() != Structure_Park) continue;
        ++parks;
        if(firstPark.isInvalid()) firstPark = s->getLocation();
        bool near = false;
        for(const StructureBase* l : structureList)
            near = near || (ownLot(l, botHouse)
                && std::max(std::abs(l->getX() - s->getX()), std::abs(l->getY() - s->getY())) <= ParkDistrictPolicy::kLotReach + 1);
        nearLots += near;
    }
    SDL_Log("SKIRMISH_PROBE: park districts house=%d lots=%d parks=%d near_lots=%d", botHouse->getHouseID(), lots, parks, nearLots);
    skirmishRequire(parks >= 1, "QuantBot with ParkDistricts planted no park in four minutes");
    skirmishRequire(nearLots == parks, "A QuantBot park stands away from its R/C lots");
    skirmishRequire(parks <= ParkDistrictPolicy::parkCap(lots), "QuantBot planted more parks than its lots allow");
    captureGame(*reloaded, "ingame-ai-park", firstPark);
    endMatch(reloaded);
}

} // namespace ParkProbe
