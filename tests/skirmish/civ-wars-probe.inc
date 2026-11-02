// Included inside skirmish-probe.inc's fixture namespace; no production test hooks.
void captureCivMap(const std::string& name) {
    const int scale = 4, width = currentGameMap->getSizeX(), height = currentGameMap->getSizeY();
    auto image = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, width * scale, height * scale, 32, SDL_PIXELFORMAT_ARGB8888));
    skirmishRequire(image != nullptr, "Cannot create Civilization Wars overview");
    auto paint = [&](int x, int y, int w, int h, Uint32 colour) {
        SDL_Rect r{x * scale, y * scale, w * scale, h * scale};
        SDL_FillRect(image.get(), &r, colour);
    };
    for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x) {
        const auto* tile = currentGameMap->getTile(x, y);
        Uint8 r = 207, g = 164, b = 88;
        if(tile->isMountain()) { r = 68; g = 53; b = 45; }
        else if(tile->isRock()) { r = 133; g = 105; b = 78; }
        else if(tile->isSpiceBloom()) { r = 175; g = 52; b = 12; }
        else if(tile->isSpice()) { r = 206; g = 106; b = 25; }
        paint(x, y, 1, 1, SDL_MapRGB(image->format, r, g, b));
    }
    for(const auto* structure : structureList) {
        const auto at = structure->getLocation(), size = structure->getStructureSize();
        const auto colour = RGBA2SDL(getHouseColorRGB(structure->getOwner()->getHouseID(), 1));
        paint(at.x, at.y, size.x, size.y, SDL_MapRGB(image->format, colour.r, colour.g, colour.b));
    }
    const auto path = probeOut(name + "-overview.png");
    skirmishRequire(SavePNG(image.get(), path.c_str()) == 0, "Cannot save Civilization Wars overview");
    SDL_Log("SKIRMISH_PROBE: screenshot %s", path.c_str());
}

void runCivMenu() {
    CivWarsMenu menu;
    probeSelect(menu.teamsBox, 3);
    probeSelect(menu.sizeBox, 0);
    probeSelect(menu.wormsBox, 1);
    menu.seedBox.setText("20261102");
    menu.fixedSeedBox.setChecked(true);
    menu.spectateBox.setChecked(true);
    menu.onSpectateChanged();
    skirmishCapture(menu, "civilization-wars-menu");
    GameInitSettings init;
    skirmishRequire(menu.prepareStart(init), "Civilization Wars Start could not prepare a match");
    skirmishRequire(init.getModName() == "CivWars" && init.isCivilizationWars() && init.isLocalSpectator(),
                    "Civilization Wars Start lost its mod, marker or Spectate");
    skirmishRequire(init.getGameOptions().researchLab && init.getGameOptions().sandwormsRespawn
                    && init.getGameOptions().trooperAntiAir, "Civilization Wars Start lost required rules");
    skirmishRequire(init.getHouseInfoList().size() == 6, "3v3 did not prepare six seats");
    CivWarsMenu remembered;
    skirmishRequire(remembered.readParams().teamSize == 3 && remembered.readParams().seed == 20261102u
                    && remembered.readParams().spectate, "Civilization Wars choices were not remembered");
    CivWarsMenu host(true);
    host.seedBox.setText("20261103");
    probeSelect(host.teamsBox, 2);
    host.onCreateMap();
    const auto path = host.getCreatedMap();
    skirmishRequire(!path.empty() && existsFile(path), "Civilization Wars Next did not save a multiplayer map");
    skirmishCapture(host, "civilization-wars-host");
    GameInitSettings network(getBasename(path, true), readCompleteFile(path), "Civ Wars test lobby", false, host.getMatchRules());
    CustomGamePlayers lobby(network, true, true);
    skirmishRequire(lobby.numHouses == 4, "Civilization Wars lobby did not read all four PlayerN starts");
    for(int slot = 0; slot < 4; ++slot)
        skirmishRequire(lobby.houseInfo[slot].teamDropDown.getSelectedEntryIntData() == (slot < 2 ? 1 : 2),
                        "Civilization Wars lobby lost the left/right team assignment");
    skirmishCapture(lobby, "civilization-wars-lobby");
    OMemoryStream output;
    output.open();
    network.save(output);
    IMemoryStream input(output.getData(), output.getDataLength());
    GameInitSettings guest(input);
    skirmishRequire(guest.getFiledata() == network.getFiledata() && guest.getModName() == "CivWars"
                    && guest.isCivilizationWars() && guest.getGameOptions() == network.getGameOptions(),
                    "Civilization Wars host rules/map did not survive the guest wire format");
    std::filesystem::remove(path);
}

void runCivMatch(const std::string& name) {
    CivWarsGenerator::Params params;
    params.teamSize = name == "civ-1v1" ? 1 : name == "civ-2v2" ? 2 : 3;
    params.house = HOUSE_ATREIDES;
    params.seed = 20261102u + params.teamSize;
    params.size = params.teamSize - 1;
    params.spectate = true;
    params.spectateClass = params.allyClass = params.enemyClass = "qBotMedium";
    skirmishRequire(RandomSkirmish::activateMod("CivWars"), "Cannot activate Civilization Wars");
    const auto catalogue = RandomSkirmish::loadCatalogue(Mode::DuneCity);
    const auto result = CivWarsGenerator::generate(params, catalogue);
    std::ofstream(probeOut(name + ".ini"), std::ios::binary) << result.ini;
    auto init = CivWars::makeInit(result, effectiveGameOptions, params);
    auto game = startMatch(init);
    skirmishRequire(game->techLevel == 1 && game->isCitySimEnabled() && game->isObserving()
                    && game->getGameInitSettings().isCivilizationWars(), "Civilization Wars match did not load its fixed mode");
    int worms = 0;
    for(const auto* unit : unitList) worms += unit->getItemID() == Unit_Sandworm;
    skirmishRequire(worms == params.wormsPerLane * 3, "Generated per-lane worms did not load");
    for(const auto& setup : result.houses) {
        const auto* house = game->getHouse(setup.house);
        skirmishRequire(house && house->getTeamID() == setup.team && house->getCredits() == 0,
                        "Civilization Wars house/team/zero-credit setup differs");
        skirmishRequire(house->getProducedPower() > house->getPowerRequirement(), "Starting turrets/lab/city are not powered");
        std::map<int, int> expected, actual;
        for(const auto& placement : result.structures) if(placement.house == setup.house) ++expected[placement.item];
        for(const auto* structure : structureList) if(structure->getOwner() == house) ++actual[structure->getItemID()];
        skirmishRequire(actual == expected, "Loaded Civilization Wars starting buildings differ from the generated kit");
        skirmishRequire(actual[Structure_ResearchLab] == 1 && actual[Structure_Refinery] == 1
                        && actual[Structure_ConstructionYard] == 1 && actual[Structure_PoliceStation] == 1,
                        "Civilization Wars starting kit is incomplete");
        int harvesters = 0, carriers = 0;
        for(const auto* unit : unitList) if(unit->getOwner() == house) {
            harvesters += unit->getItemID() == Unit_Harvester;
            if(const auto* carrier = dynamic_cast<const Carryall*>(unit)) carriers += carrier->isOwnedCarrier();
        }
        skirmishRequire(harvesters == 1 && carriers == 1,
                        "First Refinery did not supply its normal harvester gift, or permanent Carryall is missing");
        skirmishRequire(house->getPlayerList().size() == 1
                        && dynamic_cast<const QuantBot*>(house->getPlayerList().front().get()),
                        "Civilization Wars smoke does not have QuantBot on every seat");
        SDL_Log("SKIRMISH_PROBE: civ house=%d team=%d power=%d/%d turrets=%d windtraps=%d harvester=%d permanent-carryall=%d",
            setup.house, setup.team, house->getProducedPower(), house->getPowerRequirement(),
            actual[Structure_RocketTurret], actual[Structure_WindTrap], harvesters, carriers);
    }
    captureCivMap(name + "-loaded");
    captureGame(*game, name + "-base", result.houses.front().start);
    captureGame(*game, name + "-lanes", Coord(result.map.getSizeX() / 2, result.map.getSizeY() / 2));
    auto* sim = game->getCitySimulation();
    while(sim->lastProcessedDay_ < 2 || sim->pendingGrowthPhase_) game->updateGameState();
    for(const auto& setup : result.houses) {
        const auto& city = sim->getHouseState(setup.house);
        SDL_Log("SKIRMISH_PROBE: civ initial-income house=%d population=%d annual-tax=%d annual-upkeep=%d land-value=%d",
            setup.house, city.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier,
            city.budget.getLastTaxRevenue(), city.lastPoliceExpense, city.avgLandValue);
        skirmishRequire(city.budget.getLastTaxRevenue() > city.lastPoliceExpense,
                        "Civilization Wars starting city does not cover turret and police upkeep");
    }
    SDL_Log("SKIRMISH_SMOKE_BEGIN: Civilization Wars %dv%d three game minutes", params.teamSize, params.teamSize);
    const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(180 * 1000);
    while(!game->finished && game->getGameCycleCount() < end) game->updateGameState();
    SDL_Log("SKIRMISH_SMOKE_END: cycle=%u target=%u finished=%d", game->getGameCycleCount(), end, game->finished ? 1 : 0);
    skirmishRequire(!game->finished && game->getGameCycleCount() == end, "Civilization Wars did not reach its three-minute limit");
    for(const auto& setup : result.houses) {
        const auto* house = game->getHouse(setup.house);
        skirmishRequire(house && house->isAlive() && house->getCityTaxReceipts() > 0,
                        "Civilization Wars house died or received no city tax income");
    }
    captureCivMap(name + "-played");
    captureGame(*game, name + "-played-base", result.houses.front().start);
    if(params.teamSize == 1) runCivWarsWormRespawnProbe(game, probeOut("civ-worm-sleep.dls"));
    endMatch(game);
}
