// Included inside skirmish-probe.inc's fixture namespace; no production test hooks.
#include "mountain-scenery-probe.inc"

void captureCivMap(const std::string& name) {
    const int width = currentGameMap->getSizeX(), height = currentGameMap->getSizeY();
    int viewWidth = 0, viewHeight = 0;
    SDL_GetRendererOutputSize(renderer, &viewWidth, &viewHeight);
    const int scale = std::max(1, std::min((viewWidth - 32) / width, (viewHeight - 32) / height));
    const int left = (viewWidth - width * scale) / 2, top = (viewHeight - height * scale) / 2;
    auto image = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, viewWidth, viewHeight, 32, SDL_PIXELFORMAT_RGBA32));
    skirmishRequire(image != nullptr, "Cannot create Civilization Wars overview");
    SDL_FillRect(image.get(), nullptr, SDL_MapRGB(image->format, 24, 22, 20));
    auto paint = [&](int x, int y, int w, int h, Uint32 colour) {
        SDL_Rect r{left + x * scale, top + y * scale, w * scale, h * scale};
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
        auto colour = RGBA2SDL(getHouseColorRGB(structure->getOwner()->getHouseID(), 1));
        switch(structure->getItemID()) {
            case Structure_RocketTurret: colour = SDL_Color{255, 255, 255, 255}; break;
            case Structure_ZoneResidential: colour = SDL_Color{80, 225, 110, 255}; break;
            case Structure_ZoneCommercial: colour = SDL_Color{80, 165, 255, 255}; break;
            case Structure_ZoneIndustrial: colour = SDL_Color{255, 215, 65, 255}; break;
            default: break;
        }
        paint(at.x, at.y, size.x, size.y, SDL_MapRGB(image->format, colour.r, colour.g, colour.b));
    }
    const auto path = probeOut(name + "-overview.png");
    skirmishRequire(SavePNG(image.get(), path.c_str()) == 0, "Cannot save Civilization Wars overview");
    SDL_Log("SKIRMISH_PROBE: screenshot %s", path.c_str());
}

void runCivMenu() {
    const bool savedDamage = settings.gameOptions.originalUnitDamage;
    const bool effectiveDamage = effectiveGameOptions.originalUnitDamage;
    CivWarsMenu menu;
    probeSelect(menu.teamsBox, 3);
    probeSelect(menu.sizeBox, 0);
    probeSelect(menu.wormsBox, CivWarsGenerator::kMaxWormsPerLane);
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
    skirmishRequire(init.getGameOptions().originalUnitDamage
                    && settings.gameOptions.originalUnitDamage == savedDamage,
                    "Civ Wars must enable original unit damage without changing the global preference");
    skirmishRequire(menu.readParams().wormsPerLane == CivWarsGenerator::kMaxWormsPerLane
                    && init.getFiledata().find("WormsPerLane=10") != std::string::npos,
                    "Civ Wars menu did not accept ten worms per lane");
    probeSelect(menu.wormsBox, 0);
    const Point wormAt = menu.getPosition() + menu.windowWidget.getWidgetPosition(&menu.wormsBox);
    const int wormX = wormAt.x + menu.wormsBox.trackEnd();
    menu.handleMouseLeft(wormX, wormAt.y + 5, true);
    menu.handleMouseLeft(wormX, wormAt.y + 5, false);
    skirmishRequire(menu.readParams().wormsPerLane == 10, "Actual worms slider click did not select ten");
    skirmishCapture(menu, "civilization-wars-max-worms");
    skirmishRequire(effectiveGameOptions.originalUnitDamage == effectiveDamage,
                    "Civ Wars menu overwrote other modes' effective rules");
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
    auto wireRules = network.getGameOptions();
    // Legacy setup streams never carried cityEffects; the verified mod is authoritative
    // for city simulation on both peers (Game::initGame/loadSaveGame), not this GUI default.
    wireRules.cityEffects = false;
    skirmishRequire(guest.getFiledata() == network.getFiledata() && guest.getModName() == "CivWars"
                    && guest.isCivilizationWars() && guest.getGameOptions() == wireRules
                    && ModManager::instance().isCityModeActive() && input.getRemainingLength() == 0,
                    "Civilization Wars host rules/map did not survive the guest wire format");
    OMemoryStream repeated;
    repeated.open();
    guest.save(repeated);
    skirmishRequire(std::string(output.getData(), output.getDataLength())
                    == std::string(repeated.getData(), repeated.getDataLength()),
                    "Civilization Wars guest did not preserve the exact transmitted setup bytes");
    std::filesystem::remove(path);
}

// The runner replaces only the terminal interactive game launcher. The menu's
// Start callback, generation, rules and roster stay production code; this body
// then drives the actual human SDL/command path without injecting income/state.
void runCivResearchGame(const GameInitSettings& init) {
    auto game = startMatch(init);
    skirmishRequire(init.getGameOptions().originalUnitDamage, "Civ Wars lost original unit damage");
    SDL_Log("CIV_HUMAN_RESEARCH: event=setup originalUnitDamage=%d researchLab=%d civWars=%d",
        int(init.getGameOptions().originalUnitDamage), int(init.getGameOptions().researchLab), int(init.isCivilizationWars()));
    skirmishRequire(!game->isObserving() && pLocalHouse && pLocalPlayer
                    && dynamic_cast<HumanPlayer*>(pLocalPlayer), "Civ Wars did not seat the human controller");
    ResearchLab* lab = nullptr;
    for(auto* structure : structureList)
        if(structure->getOwner() == pLocalHouse && structure->getItemID() == Structure_ResearchLab)
            lab = dynamic_cast<ResearchLab*>(structure);
    skirmishRequire(lab, "Human's generated Lab is missing");
    captureCivMap("civ-research-initial");
    std::ofstream(probeOut("civ-research-initial.ini"), std::ios::binary) << init.getFiledata();
    const Coord centre = lab->getCenterPoint();
    screenborder->setNewScreenCenter(centre);
    auto click = [&](int x, int y) {
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        for(auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
            SDL_Event event{};
            event.type = type; event.button.button = SDL_BUTTON_LEFT;
            event.button.x = x; event.button.y = y;
            SDL_PushEvent(&event);
        }
        game->doInput();
    };
    click(screenborder->world2screenX(centre.x), screenborder->world2screenY(centre.y));
    skirmishRequire(game->getSelectedList() == std::set<Uint32>{lab->getObjectID()},
                    "Actual map click did not select the Research Lab");
    auto& gui = game->getGameInterface();
    gui.updateObjectInterface();
    gui.updateObjectInterface();
    auto* panel = dynamic_cast<ResearchLabInterface*>(gui.pObjectContainer);
    skirmishRequire(panel, "Selecting the Lab did not open its research sidebar");
    auto researchPoint = [&]() {
        const Point at = gui.windowWidget.getWidgetPosition(panel)
            + panel->getWidgetPosition(&panel->mainHBox)
            + panel->mainHBox.getWidgetPosition(&panel->box)
            + panel->box.getWidgetPosition(&panel->research);
        return Point(at.x + panel->research.getSize().x / 2, at.y + panel->research.getSize().y / 2);
    };
    auto commands = [&]() {
        int count = 0;
        for(const auto& slot : game->cmdManager.timeslot) for(const auto& command : slot)
            count += command.getCommandID() == CMD_RESEARCH_TECH_LEVEL;
        return count;
    };
    const Point button = researchPoint();
    auto capture = [&](const char* stage) {
        captureGame(*game, std::string("civ-research-") + stage + "-" + std::to_string(getRendererWidth()),
                    lab->getLocation());
    };
    skirmishRequire(button.x >= 0 && button.x < getRendererWidth()
                    && button.y >= 0 && button.y < getRendererHeight(), "Research button lies outside the window");
    capture("zero-credits");
    skirmishRequire(!panel->research.isEnabled() && !pLocalHouse->canStartTechResearch(lab->getObjectID()),
                    "Zero-credit Civ Wars house unexpectedly offers research");
    skirmishRequire(panel->status.getText() == "Needs 600 credits\nTime: 0:50",
                    "Unaffordable research does not explain its credit requirement");
    click(button.x, button.y);
    skirmishRequire(commands() == 0 && !pLocalHouse->isTechResearchActive(),
                    "Unaffordable research click sent a command or spent credits");
    SDL_Log("CIV_HUMAN_RESEARCH: event=unaffordable cycle=%u house=%d credits=%d tech=%d button=%d,%d enabled=%d",
        game->getGameCycleCount(), pLocalHouse->getHouseID(), pLocalHouse->getCredits(), pLocalHouse->getTechLevel(),
        button.x, button.y, int(panel->research.isEnabled()));
    const Uint32 limit = MILLI2CYCLES(480000);
    while(game->getGameCycleCount() < limit && !pLocalHouse->canStartTechResearch(lab->getObjectID()))
        game->updateGameState();
    gui.updateObjectInterface();
    SDL_Log("CIV_HUMAN_RESEARCH: event=ready cycle=%u house=%d credits=%d tech=%d enabled=%d active_lab=%d",
        game->getGameCycleCount(), pLocalHouse->getHouseID(), pLocalHouse->getCredits(), pLocalHouse->getTechLevel(),
        int(panel->research.isEnabled()), int(lab->isActive()));
    capture("ready");
    skirmishRequire(panel->research.isEnabled(), "Normal Civ Wars income did not enable human research");
    const int paid = pLocalHouse->getCredits();
    const int cost = pLocalHouse->getTechResearchCost();
    click(button.x, button.y);
    skirmishRequire(commands() == 1 && !pLocalHouse->isTechResearchActive(),
                    "Research button did not queue exactly one delayed command");
    for(int tick = 0; tick < 20 && !pLocalHouse->isTechResearchActive(); ++tick) game->updateGameState();
    SDL_Log("CIV_HUMAN_RESEARCH: event=command_result active=%d target=%d credits_before=%d cost=%d credits_after=%d",
        int(pLocalHouse->isTechResearchActive()), pLocalHouse->getTechResearchTarget(),
        paid, cost, pLocalHouse->getCredits());
    // The same simulation tick may also collect tax or pay upkeep. Exact research
    // payment is covered by research_lab_probe; here require the GUI command and completion.
    skirmishRequire(pLocalHouse->isTechResearchActive() && pLocalHouse->getTechResearchTarget() == 2,
                    "Human GUI research command did not execute against the selected Lab");
    gui.updateObjectInterface();
    capture("active");
    SDL_Log("CIV_HUMAN_RESEARCH: event=start cycle=%u house=%d credits=%d tech=%d target=%d lab=%u",
        game->getGameCycleCount(), pLocalHouse->getHouseID(), pLocalHouse->getCredits(),
        pLocalHouse->getTechLevel(), pLocalHouse->getTechResearchTarget(), lab->getObjectID());
    while(game->getGameCycleCount() < limit && pLocalHouse->getTechLevel() < 2) game->updateGameState();
    skirmishRequire(pLocalHouse->getTechLevel() == 2, "Human GUI research failed to advance to tech 2");
    gui.updateObjectInterface();
    capture("complete");
    SDL_Log("CIV_HUMAN_RESEARCH: event=complete cycle=%u house=%d credits=%d tech=%d",
        game->getGameCycleCount(), pLocalHouse->getHouseID(), pLocalHouse->getCredits(), pLocalHouse->getTechLevel());
    while(game->getGameCycleCount() < limit) game->updateGameState();
    for(const auto& setup : init.getHouseInfoList()) {
        const auto* house = game->getHouse(setup.houseID);
        SDL_Log("CIV_HUMAN_RESEARCH: event=eight_minutes cycle=%u house=%d credits=%d tech=%d target=%d",
            game->getGameCycleCount(), house->getHouseID(), house->getCredits(),
            house->getTechLevel(), house->getTechResearchTarget());
        skirmishRequire(house->getTechLevel() >= 2, "Human/AI Civ Wars house failed to research in eight minutes");
    }
    endMatch(game);
}

void runCivResearch() {
    CivWarsMenu menu;
    probeSelect(menu.teamsBox, 1);
    probeSelect(menu.sizeBox, 0);
    probeSelect(menu.houseBox, HOUSE_ATREIDES);
    menu.spectateBox.setChecked(false);
    menu.onSpectateChanged();
    menu.seedBox.setText("20261103");
    menu.fixedSeedBox.setChecked(true);
    skirmishCapture(menu, "civ-research-menu");
    const Point at = menu.getPosition() + menu.windowWidget.getWidgetPosition(&menu.startButton);
    menu.handleMouseLeft(at.x + 5, at.y + 5, true);
    menu.handleMouseLeft(at.x + 5, at.y + 5, false);
    skirmishRequire(menu.isQuiting(), "Actual Civ Wars Start button did not finish its headless match");
}

void runCivMatch(const std::string& name) {
    CivWarsGenerator::Params params;
    params.teamSize = name == "civ-1v1" ? 1 : name == "civ-2v2" ? 2 : 3;
    params.house = HOUSE_ATREIDES;
    params.seed = 20261102u + params.teamSize;
    params.size = params.teamSize - 1;
    params.spectate = true;
    params.spectateClass = params.allyClass = params.enemyClass = params.teamSize == 1 ? "qBotEasy" : "qBotMedium";
    if(params.teamSize == 2) params.enemyClass = "qBotHard";
    if(params.teamSize == 3) {
        params.spectateClass = "qBotBrutal";
        params.allyClass = "qBotSupportMedium";
        params.enemyClass = "qBotDefend";
    }
    params.wormsPerLane = CivWarsGenerator::kMaxWormsPerLane;
    skirmishRequire(RandomSkirmish::activateMod("CivWars"), "Cannot activate Civilization Wars");
    const auto catalogue = RandomSkirmish::loadCatalogue(Mode::DuneCity);
    const auto result = CivWarsGenerator::generate(params, catalogue);
    std::ofstream(probeOut(name + ".ini"), std::ios::binary) << result.ini;
    auto init = CivWars::makeInit(result, effectiveGameOptions, params);
    auto game = startMatch(init);
    int viewWidth = 0, viewHeight = 0;
    SDL_GetRendererOutputSize(renderer, &viewWidth, &viewHeight);
    const std::string picture = name + "-" + std::to_string(viewWidth) + "x" + std::to_string(viewHeight);
    skirmishRequire(game->techLevel == 1 && game->isCitySimEnabled() && game->isObserving()
                    && game->getGameInitSettings().isCivilizationWars(), "Civilization Wars match did not load its fixed mode");
    skirmishRequire(init.getGameOptions().originalUnitDamage, "Civ Wars AI game lost original unit damage");
    int worms = 0;
    for(const auto* unit : unitList) worms += unit->getItemID() == Unit_Sandworm;
    skirmishRequire(worms == params.wormsPerLane * 3, "Generated per-lane worms did not load");
    for(const auto& setup : result.houses) {
        const auto* house = game->getHouse(setup.house);
        skirmishRequire(house && house->getTeamID() == setup.team && house->getCredits() == 0
                        && house->getTechLevel() == 1,
                        "Civilization Wars house/team/tech-1/zero-credit setup differs");
        skirmishRequire(house->getProducedPower() > house->getPowerRequirement(), "Starting turrets/lab/city are not powered");
        std::map<int, int> expected, actual;
        for(const auto& placement : result.structures) if(placement.house == setup.house) ++expected[placement.item];
        for(const auto* structure : structureList) if(structure->getOwner() == house) ++actual[structure->getItemID()];
        skirmishRequire(actual == expected, "Loaded Civilization Wars starting buildings differ from the generated kit");
        skirmishRequire(actual[Structure_ResearchLab] == 1 && actual[Structure_Refinery] == 1
                        && actual[Structure_ConstructionYard] == 1 && actual[Structure_PoliceStation] == 1,
                        "Civilization Wars starting kit is incomplete");
        int frontTurrets = 0;
        for(const auto* structure : structureList) if(structure->getOwner() == house && structure->getItemID() == Structure_RocketTurret) {
            const int x = structure->getLocation().x;
            frontTurrets += std::min(x, result.map.getSizeX() - 1 - x) == result.map.getSizeX() / 4 - 1;
        }
        skirmishRequire(actual[Structure_RocketTurret] == 15 && frontTurrets == 10,
                        "Every Civilization Wars base requires fifteen turrets, ten at the front");
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
        SDL_Log("SKIRMISH_PROBE: civ house=%d team=%d power=%d/%d turrets=%d front=%d core-flank=%d windtraps=%d harvester=%d permanent-carryall=%d",
            setup.house, setup.team, house->getProducedPower(), house->getPowerRequirement(),
            actual[Structure_RocketTurret], frontTurrets, actual[Structure_RocketTurret] - frontTurrets,
            actual[Structure_WindTrap], harvesters, carriers);
    }
    captureCivMap(picture + "-loaded");
    const int baseEnd = result.map.getSizeX() / 4, baseY = result.houses.front().start.y + 1;
    captureGame(*game, picture + "-base", Coord(baseEnd - 16, baseY));
    captureGame(*game, picture + "-base-front", Coord(baseEnd - 6, baseY));
    captureGame(*game, picture + "-base-rear", Coord(baseEnd - 21, baseY));
    captureGame(*game, picture + "-lanes", Coord(result.map.getSizeX() / 2, result.map.getSizeY() / 2));
    auto* sim = game->getCitySimulation();
    while(sim->lastProcessedDay_ < 2 || sim->pendingGrowthPhase_) game->updateGameState();
    for(const auto& setup : result.houses) {
        const auto& city = sim->getHouseState(setup.house);
        FixPoint fullUpkeep = 0;
        for(const auto* structure : structureList)
            if(structure->getOwner()->getHouseID() == setup.house)
                fullUpkeep += DuneCity::getPoliceAnnualCost(structure->getItemID());
        const int annualPower = (game->getHouse(setup.house)->getPowerRequirement() + 3) / 4;
        SDL_Log("SKIRMISH_PROBE: civ initial-income house=%d population=%d annual-tax=%d annual-upkeep=%d annual-power=%d paid-upkeep=%d land-value=%d",
            setup.house, city.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier,
            city.budget.getLastTaxRevenue(), fullUpkeep.lround(), annualPower, city.lastPoliceExpense, city.avgLandValue);
        skirmishRequire(fullUpkeep == 325 && FixPoint(city.budget.getLastTaxRevenue()) > fullUpkeep + annualPower,
                        "Civilization Wars starting city does not cover full turret, police and power upkeep");
    }
    // Preserve the reference test's minimum research savings rate, rather than
    // requiring a dearer, longer first study to finish in the old eight minutes.
    constexpr Uint32 referenceWindow = MILLI2CYCLES(480 * 1000);
    constexpr int referenceCost = TechResearch::cost(2, TechResearch::kReferenceArea, TechResearch::Pace::Normal);
    constexpr int referenceCycles = TechResearch::cycles(2, TechResearch::kReferenceArea, TechResearch::Pace::Normal);
    const auto* firstHouse = game->getHouse(result.houses.front().house);
    const int firstCost = firstHouse->getTechResearchCost();
    const int firstCycles = firstHouse->techResearchCycles(2);
    const Uint32 fundingCycles = static_cast<Uint32>(
        (std::uint64_t(referenceWindow - referenceCycles) * firstCost + referenceCost - 1) / referenceCost);
    const Uint32 window = std::max(referenceWindow, fundingCycles + firstCycles);
    SDL_Log("SKIRMISH_SMOKE_BEGIN: Civilization Wars %dv%d window_cycles=%u first_cost=%d first_cycles=%d",
        params.teamSize, params.teamSize, window, firstCost, firstCycles);
    const Uint32 researchEnd = game->getGameCycleCount() + window;
    const Uint32 end = std::max(researchEnd, MILLI2CYCLES(600 * 1000));
    std::array<int, NUM_HOUSES> lastTech{}, lastTarget{};
    while(!game->finished && game->getGameCycleCount() < end) {
        game->updateGameState();
        for(const auto& setup : result.houses) {
            const auto* house = game->getHouse(setup.house);
            const int tech = house->getTechLevel(), target = house->getTechResearchTarget();
            if(tech != lastTech[setup.house] || target != lastTarget[setup.house])
                SDL_Log("CIV_AI_RESEARCH: cycle=%u house=%d class=%s tech=%d target=%d remaining=%d credits=%d",
                    game->getGameCycleCount(), setup.house,
                    (setup.role == Role::Human ? params.spectateClass : setup.playerClass).c_str(),
                    tech, target, house->getTechResearchRemaining(), house->getCredits());
            lastTech[setup.house] = tech;
            lastTarget[setup.house] = target;
        }
        if(game->getGameCycleCount() == researchEnd)
            for(const auto& setup : result.houses)
                skirmishRequire(game->getHouse(setup.house)->getTechLevel() >= 2,
                                "Civ Wars QuantBot missed its original policy-scaled research deadline");
    }
    SDL_Log("SKIRMISH_SMOKE_END: cycle=%u target=%u finished=%d", game->getGameCycleCount(), end, game->finished ? 1 : 0);
    skirmishRequire(!game->finished && game->getGameCycleCount() == end && end >= MILLI2CYCLES(600 * 1000),
                    "Civilization Wars did not reach ten minutes and its policy-scaled research window");
    for(const auto& setup : result.houses) {
        const auto* house = game->getHouse(setup.house);
        skirmishRequire(house && house->isAlive() && house->getCityTaxReceipts() > 0,
                        "Civilization Wars house died or received no city tax income");
        skirmishRequire(house->getTechLevel() >= 2, "Civ Wars QuantBot did not advance technology within its policy-scaled research window");
    }
    captureCivMap(picture + "-played");
    captureGame(*game, picture + "-played-base", result.houses.front().start);
    if(params.teamSize == 1) runCivWarsWormRespawnProbe(game, probeOut("civ-worm-sleep.dls"));
    if(params.teamSize == 1) runMountainSceneryProbe(*game);
    endMatch(game);
}
