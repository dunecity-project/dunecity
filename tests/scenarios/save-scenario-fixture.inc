// Included only through tools/scenarios/save-scenario-export.inc, inside
// namespace ScenarioExport. No includes of its own: the including file already
// pulls in the engine headers it needs.
//
// Builds a small synthetic world natively and saves it, so the scenario-export
// regression exercises the real save loader, the real writer and the real
// scenario loader without depending on anybody's private save file.
//
// The world deliberately contains everything the exporter has to get right:
//
//   * concrete-only tiles, road-only tiles, and tiles carrying BOTH, which the
//     writer cannot express with one GEN key per position
//   * damaged structures, so the 0..256 health quantization is exercised
//   * three houses on three teams with their own credit balances
//   * an air unit over a building, carried infantry and a damaged wall
//   * two infantry sharing one tile, which must stay legal after the round trip

void runFixture() {
    const std::string mapPath = requiredEnv("SCENARIO_EXPORT_FIXTURE_MAP");
    const std::string savePath = requiredEnv("SCENARIO_EXPORT_SAVE");
    const std::string mod = env("SCENARIO_EXPORT_FIXTURE_MOD", "dunecity");

    if(!ModManager::instance().setActiveMod(mod) || ModManager::instance().getActiveModName() != mod)
        fail("The fixture mod did not activate: " + mod);

    const std::string contents = readFile(mapPath);
    GameInitSettings init(mapPath, contents, true, settings.gameOptions);
    init.randomSeed = 1009202601u;
    const int houses[3] = { HOUSE_HARKONNEN, HOUSE_ATREIDES, HOUSE_ORDOS };
    for(int slot = 0; slot < 3; ++slot) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(houses[slot]), slot + 1);
        // An ordinary custom match has a local human house; without one there is
        // no local player for the engine to anchor on.
        if(slot == 0) info.addPlayerInfo({settings.general.playerName, "HumanPlayer"});
        else info.addPlayerInfo({houseName(houses[slot]), "qBotEasy"});
        init.addHouseInfo(info);
    }

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;

    require(currentGameMap != nullptr, "The fixture map loaded");
    for(const int houseID : houses)
        require(game->house[houseID] != nullptr, "Fixture house " + houseName(houseID) + " exists");

    // Damage one structure per house so health is neither full nor zero.
    int damaged = 0;
    game->getObjectManager().forEachObject([&](Uint32, ObjectBase* object) {
        if(object == nullptr || !object->isAStructure()) return;
        const int owner = object->getOwner() != nullptr ? object->getOwner()->getHouseID() : INVALID;
        for(int slot = 0; slot < 3; ++slot) {
            if(owner != houses[slot]) continue;
            if(damaged > slot) continue;
            // 3/8, 5/8 and 7/8 of maximum: distinct, non-trivial quantizations.
            object->setHealth((object->getMaxHealth() * (3 + 2 * slot)) / 8);
            ++damaged;
            return;
        }
    });
    require(damaged > 0, "At least one fixture structure was damaged");

    // Concrete, road and the overlay of both, on free rock tiles.
    std::vector<Coord> freeRock;
    for(int y = 0; y < currentGameMap->getSizeY() && freeRock.size() < 12; ++y) {
        for(int x = 0; x < currentGameMap->getSizeX() && freeRock.size() < 12; ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            if(tile->getType() != Terrain_Rock || tile->hasAGroundObject()) continue;
            freeRock.push_back(Coord(x, y));
        }
    }
    if(freeRock.size() < 12)
        fail("The fixture map offers only " + std::to_string(freeRock.size())
             + " free rock tiles; 12 are needed. Choose another fixture map.");

    int concreteOnly = 0, roadOnly = 0, overlay = 0;
    for(size_t i = 0; i < freeRock.size(); ++i) {
        Tile* tile = currentGameMap->getTile(freeRock[i].x, freeRock[i].y);
        const int owner = houses[i % 3];
        tile->setOwner(owner);
        switch(i % 3) {
            case 0: tile->setType(Terrain_Slab); ++concreteOnly; break;
            case 1: tile->setRoad(true); ++roadOnly; break;
            default:
                // Reachable in ordinary play: build concrete, then a road on it.
                // placeStructure(Slab1) clears the road flag, placeStructure(Road)
                // leaves the slab, so this is the surviving combination.
                tile->setType(Terrain_Slab);
                tile->setRoad(true);
                ++overlay;
                break;
        }
    }

    // Units load before structures, so air over a building can stay in place.
    int airOverStructure = 0;
    const StructureBase* anchor = nullptr;
    game->getObjectManager().forEachObject([&](Uint32, ObjectBase* object) {
        if(anchor != nullptr || object == nullptr || !object->isAStructure()) return;
        if(object->getOwner() == nullptr || object->getOwner()->getHouseID() != houses[0]) return;
        anchor = static_cast<const StructureBase*>(object);
    });
    if(anchor != nullptr) {
        UnitBase* carryall = game->house[houses[0]]->createUnit(Unit_Carryall, true);
        carryall->deploy(anchor->getLocation());
        UnitBase* passenger = game->house[houses[0]]->createUnit(Unit_Soldier, true);
        passenger->attackMode = RETREAT; // diagnostic-only saved live order
        static_cast<Carryall*>(carryall)->giveCargo(passenger);
        Tile* foundation = currentGameMap->getTile(anchor->getLocation());
        foundation->setType(Terrain_Slab);
        foundation->setOwner(houses[0]);
        ++airOverStructure;
    }

    StructureBase* wall = game->house[houses[2]]->placeStructure(NONE_ID, Structure_Wall,
        freeRock.back().x, freeRock.back().y, true);
    require(wall != nullptr, "A wall was placed for damage preservation");
    wall->setHealth(wall->getMaxHealth() / 2);

    // Two infantry on one tile, to keep the shared-tile rule under test.
    int stackedInfantry = 0;
    for(const auto& candidate : freeRock) {
        Tile* tile = currentGameMap->getTile(candidate.x, candidate.y);
        if(tile->hasAGroundObject() || tile->getType() == Terrain_Mountain) continue;
        for(int i = 0; i < 2; ++i)
            if(game->house[houses[1]]->placeUnit(Unit_Soldier, candidate.x, candidate.y, true) != nullptr)
                ++stackedInfantry;
        if(stackedInfantry >= 2) break;
    }

    require(game->saveGame(savePath), "The fixture world was saved");
    SDL_Log("SCENARIO_EXPORT_FIXTURE: map=%s size=%dx%d damaged=%d concrete_only=%d road_only=%d "
            "overlay=%d air_over_structure=%d stacked_infantry=%d objects=%zu",
            mapPath.c_str(), currentGameMap->getSizeX(), currentGameMap->getSizeY(), damaged,
            concreteOnly, roadOnly, overlay, airOverStructure, stackedInfantry,
            game->getObjectManager().getObjectCount());
    require(overlay > 0, "The fixture contains at least one concrete+road tile");
    teardown(game);
    SDL_Log("SCENARIO_EXPORT_PASS: fixture checks=%d", gChecks);
}
