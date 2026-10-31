// Included only by run-skirmish-probe.py, never by the shipped game. Drives the real Random
// Skirmish screen and the real CustomGame start/load path with an isolated profile.
#include <Menu/RandomSkirmishMenu.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/PlaySetup.h>
#include <Menu/CustomGamePlayers.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <MapEditor/SkirmishGenerator.h>
#include <MapEditor/MapGenerator.h>
#include <players/PlayerFactory.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/LoadSavePNG.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <players/Player.h>
#include <players/HumanPlayer.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityConstants.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <misc/draw_util.h>
#include <misc/fnkdat.h>
#include <misc/string_util.h>
#include <sand.h>

#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>

namespace {

using namespace SkirmishGenerator;

void skirmishRequire(bool ok, const std::string& what) {
    if(!ok) { SDL_Log("SKIRMISH_PROBE_FAIL: %s", what.c_str()); throw std::runtime_error(what); }
}

template<class Box>
void probeSelect(Box& box, int data) {
    for(int i = 0; i < box.getNumEntries(); ++i) if(box.getEntryIntData(i) == data) { box.setSelectedItem(i); return; }
    skirmishRequire(false, "Control has no entry " + std::to_string(data));
}

std::string probeOut(const std::string& name) { return std::string(std::getenv("SKIRMISH_PROBE_OUT")) + "/" + name; }

void skirmishCapture(Window& window, const std::string& name, bool overlay = false) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    window.draw();
    if(overlay) window.drawOverlay();
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888));
    skirmishRequire(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0, "Capture failed");
    const std::string path = probeOut(name + "-" + std::to_string(w) + "x" + std::to_string(h) + ".bmp");
    skirmishRequire(SDL_SaveBMP(surface.get(), path.c_str()) == 0, "Screenshot write failed");
    SDL_Log("SKIRMISH_PROBE: screenshot %s", path.c_str());
}

Params caseParams(const std::string& name) {
    Params p;
    p.house = HOUSE_ATREIDES;
    p.seed = 20261013;
    p.techLevel = 8;
    if(name == "vanilla-1v1") {
        p.mapSize = 64; p.allies = 0; p.enemies = 1; p.mod = "vanilla"; p.mode = Mode::Vanilla;
        p.enemySide = Side{"qBotMedium", BaseSize::Small, 2, 0};
    } else if(name == "allied-2v3") {
        p.mapSize = 128; p.allies = 1; p.enemies = 3; p.enemiesAllied = true; p.mod = "Tornie"; p.mode = Mode::Vanilla;
        p.allySide = Side{"qBotMedium", BaseSize::Large, 4, 0};
        p.enemySide = Side{"qBotMedium", BaseSize::Large, 4, 0};
    } else if(name == "city-192") {
        p.mapSize = 192; p.allies = 1; p.enemies = 1; p.mod = "dunecity"; p.mode = Mode::DuneCity;
        p.allySide = Side{"qBotMedium", BaseSize::Medium, 3, 3};
        p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 3};
    } else if(name == "stefan-128") {
        // Stefan's played configuration (testskirmish.dls, its [RANDOMSKIRMISH] section).
        p.mapSize = 128; p.seed = 488631365; p.house = HOUSE_REBELS; p.allies = 2; p.enemies = 2; p.enemiesAllied = true;
        p.techLevel = 9; p.mod = "dunecity"; p.mode = Mode::DuneCity;
        p.allySide = Side{"qBotMedium", BaseSize::Medium, 3, 4};
        p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 4};
        p.playerSide = Side{"", BaseSize::None, 0, 0};
    } else if(name == "partner-support") {
        // The player's own Medium base, 5,000-credit army and 5,000-people city, with a
        // support AI sharing the house.
        p.mapSize = 128; p.allies = 0; p.enemies = 1; p.mod = "dunecity"; p.mode = Mode::DuneCity;
        p.playerSide = Side{"qBotSupportMedium", BaseSize::Medium, 3, 2};
        p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 2};
    } else if(name == "partner-quantbot") {
        p.mapSize = 96; p.allies = 0; p.enemies = 1; p.mod = "vanilla"; p.mode = Mode::Vanilla;
        p.playerSide = Side{"qBotMedium", BaseSize::Medium, 3, 0};
        p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 0};
    } else {
        skirmishRequire(false, "Unknown probe case " + name);
    }
    return p;
}

std::unique_ptr<Game> startMatch(const GameInitSettings& init) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    return game;
}

void endMatch(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

/// The game view centred on @a tile, drawn by the real renderer, as a PNG.
void captureGame(Game& game, const std::string& name, Coord tile) {
    screenborder->setNewScreenCenter(Coord(tile.x * TILESIZE + TILESIZE / 2, tile.y * TILESIZE + TILESIZE / 2));
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    skirmishRequire(image != nullptr, "Could not read back the game view");
    const std::string path = probeOut(name + ".png");
    skirmishRequire(SavePNG(image.get(), path.c_str()) == 0, "Could not write " + path);
    SDL_Log("SKIRMISH_PROBE: screenshot %s", path.c_str());
}

#include "skirmish-render.inc"

/// Houses, teams, buildings, armies and power of a freshly loaded generated match.
void checkLoaded(Game& game, const Result& result, const Params& params) {
    skirmishRequire(game.techLevel == params.techLevel, "Tech level did not reach the match");
    skirmishRequire(game.isCitySimEnabled() == (params.mode == Mode::DuneCity), "City mode does not match the chosen game mode");
    int housesInGame = 0;
    for(int h = 0; h < NUM_HOUSES; ++h) housesInGame += game.getHouse(h) != nullptr;
    skirmishRequire(housesInGame == static_cast<int>(result.houses.size()), "House count differs from the generated roster");
    for(const HouseSetup& setup : result.houses) {
        const House* house = game.getHouse(setup.house);
        skirmishRequire(house != nullptr, "Generated house missing in the match");
        skirmishRequire(house->getTeamID() == setup.team, "Team differs for house " + std::to_string(setup.house));
        std::map<int, int> wantStructures, haveStructures, wantUnits, haveUnits;
        for(const Placement& s : result.structures) if(s.house == setup.house) ++wantStructures[s.item];
        for(const Placement& u : result.units) if(u.house == setup.house) ++wantUnits[u.item];
        for(const StructureBase* s : structureList) if(s->getOwner() == house) ++haveStructures[s->getItemID()];
        int armyValue = 0;
        for(const UnitBase* u : unitList) if(u->getOwner() == house && wantUnits.count(u->getItemID())) {
            ++haveUnits[u->getItemID()];
            if(u->getItemID() != Unit_MCV) armyValue += game.objectData.data[u->getItemID()][setup.house].price;
        }
        for(const auto& [item, count] : wantStructures)
            skirmishRequire(haveStructures[item] == count, "House " + std::to_string(setup.house) + " has "
                + std::to_string(haveStructures[item]) + " of item " + std::to_string(item) + ", generated " + std::to_string(count));
        for(const auto& [item, count] : wantUnits)
            skirmishRequire(haveUnits[item] == count, "House " + std::to_string(setup.house) + " has "
                + std::to_string(haveUnits[item]) + " of unit " + std::to_string(item) + ", generated " + std::to_string(count));
        if(setup.armyBudget > 0 && (setup.role != Role::Human || setup.base != BaseSize::None)) {
            skirmishRequire(armyValue <= setup.armyBudget && armyValue * 10 >= setup.armyBudget * 9,
                            "Army value outside 90-100% of its budget");
        }
        if(setup.base != BaseSize::None) {
            // Every generated base, the player's own included, starts at its yard on positive power.
            skirmishRequire(haveStructures[Structure_ConstructionYard] == 1, "Generated base has no Construction Yard");
            skirmishRequire(house->getProducedPower() > house->getPowerRequirement(), "Generated base runs on negative power");
        }
        // The player's house holds the player plus the chosen AI partner, nobody else.
        std::vector<std::string> classes;
        for(const auto& player : house->getPlayerList()) classes.push_back(player->getPlayerclass());
        if(setup.role == Role::Human) {
            std::vector<std::string> expected{HUMANPLAYERCLASS};
            if(!setup.partnerClass.empty()) expected.push_back(setup.partnerClass);
            skirmishRequire(classes == expected, "The player's house holds " + std::to_string(classes.size()) + " players, not the player and the chosen partner");
        } else {
            skirmishRequire(classes.size() == 1, "An AI house holds more than one player");
        }
        std::string roster;
        for(const std::string& c : classes) roster += (roster.empty() ? "" : "+") + c;
        SDL_Log("SKIRMISH_PROBE: house=%d players=%s nuclear=%d windtraps=%d", setup.house, roster.c_str(),
                haveStructures[Structure_NuclearPlant], haveStructures[Structure_WindTrap]);
        int structures = 0;
        for(const auto& [item, count] : haveStructures) structures += count;
        SDL_Log("SKIRMISH_PROBE: house=%d team=%d role=%d class=%s structures=%d army=%d/%d power=%d/%d",
                setup.house, house->getTeamID(), static_cast<int>(setup.role), setup.playerClass.c_str(), structures,
                armyValue, setup.armyBudget, house->getProducedPower(), house->getPowerRequirement());
    }
}

void runMatch(const std::string& name, bool smoke) {
    const Params params = caseParams(name);
    skirmishRequire(RandomSkirmish::modeForMod(params.mod) == params.mode, "Case mode does not match its mod's city flag");
    skirmishRequire(RandomSkirmish::activateMod(params.mod), "Case mod did not activate");
    const Result result = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
    for(const HouseSetup& h : result.houses) skirmishRequire(h.skipped == 0, "Generator could not place everything");
    // The same seed and choices produce the same map through the real catalogue as well.
    skirmishRequire(SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode)).ini == result.ini,
                    "Generation is not deterministic against the live ObjectData");
    std::ofstream(probeOut(name + ".ini"), std::ios::binary) << result.ini;   // reviewable artifact
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, params);
    init.randomSeed = params.seed;    // Test-only access, provided by -fno-access-control.
    auto game = startMatch(init);
    checkLoaded(*game, result, params);
    renderPicture(pictureOfGame(), "loaded-" + name);   // the whole map as the engine loaded it
    if(name == "partner-support") {
        // The player's own generated base and zoned city, drawn by the game itself.
        for(const Placement& s : result.structures)
            if(s.house == result.houses[0].house && s.item == Structure_ConstructionYard)
                captureGame(*game, "ingame-player-base", Coord(s.x + 1, s.y + 1));
        int zones = 0, sx = 0, sy = 0;
        for(const Placement& s : result.structures)
            if(s.house == result.houses[0].house && DuneCity::isCityZoneStructure(s.item)) { ++zones; sx += s.x; sy += s.y; }
        if(zones > 0) captureGame(*game, "ingame-player-city", Coord(sx / zones, sy / zones));
    }

    if(params.mode == Mode::DuneCity) {
        // Two complete city days: the first scan tallies the placed lots, the second has
        // already applied one round of growth and decline to them.
        auto* sim = game->getCitySimulation();
        skirmishRequire(sim != nullptr, "City simulation missing in a DuneCity match");
        while(sim->lastProcessedDay_ < 2 || sim->pendingGrowthPhase_) game->updateGameState();
        for(const HouseSetup& h : result.houses) {
            if(h.cityTarget <= 0) continue;
            const int measured = sim->getHouseState(h.house).getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier;
            SDL_Log("SKIRMISH_PROBE: city house=%d target=%d estimate=%d measured=%d day=%u", h.house, h.cityTarget,
                    h.cityEstimate, measured, sim->lastProcessedDay_);
            skirmishRequire(std::abs(measured - h.cityTarget) * 4 <= h.cityTarget, "Measured city population outside 25% of target");
        }
    }

    if(smoke) {
        SDL_Log("SKIRMISH_SMOKE_BEGIN: %s", result.name.c_str());
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(60 * 1000);
        while(!game->finished && game->getGameCycleCount() < end) game->updateGameState();
        SDL_Log("SKIRMISH_SMOKE_END: cycle=%u finished=%d", game->getGameCycleCount(), game->finished ? 1 : 0);
        renderPicture(pictureOfGame(), "played-" + name);
        skirmishRequire(!game->finished, "Generated match ended inside its first minute");
        for(const HouseSetup& h : result.houses) {
            const House* house = game->getHouse(h.house);
            skirmishRequire(house != nullptr && house->isAlive(), "A house died in the first minute");
            if(game->getCitySimulation() && h.cityTarget > 0)
                SDL_Log("SKIRMISH_PROBE: city after one minute house=%d population=%d", h.house,
                        game->getCitySimulation()->getHouseState(h.house).getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier);
        }
        // The match is an ordinary custom game: it saves and loads like one.
        const std::string save = probeOut(name + ".dls");
        skirmishRequire(game->saveGame(save), "Could not save the generated match");
        std::map<int, int> before;
        for(const StructureBase* s : structureList) ++before[s->getOwner()->getHouseID()];
        endMatch(game);
        auto reloaded = std::make_unique<Game>();
        currentGame = reloaded.get();
        reloaded->bReplay = true;
        reloaded->initGame(GameInitSettings(save));
        reloaded->bReplay = false;
        reloaded->initializeGameLoop();
        std::map<int, int> after;
        for(const StructureBase* s : structureList) ++after[s->getOwner()->getHouseID()];
        skirmishRequire(before == after, "Structures differ after save and load");
        for(const HouseSetup& h : result.houses)
            skirmishRequire(reloaded->getHouse(h.house) && reloaded->getHouse(h.house)->getTeamID() == h.team, "Teams differ after load");
        SDL_Log("SKIRMISH_PROBE: save/load round trip kept %zu houses", after.size());
        endMatch(reloaded);
        return;
    }
    endMatch(game);
}

/// A bundled city map written before the zone-level field: every lot still starts empty.
void runLegacyMap() {
    skirmishRequire(RandomSkirmish::activateMod("dunecity"), "dunecity mod did not activate");
    const std::string path = getDuneLegacyDataDir() + "/maps/multiplayer/4P - 192x192 - Metropolis.ini";
    const std::string fallback = std::string(std::getenv("SKIRMISH_SOURCE_DIR")) + "/data/maps/multiplayer/4P - 192x192 - Metropolis.ini";
    const std::string mapPath = existsFile(path) ? path : fallback;
    GameInitSettings init(getBasename(mapPath, true), readCompleteFile(mapPath), false, effectiveGameOptions);
    for(int i = 0; i < 4; ++i) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(i), i + 1);
        info.addPlayerInfo(i == 0 ? GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS)
                                  : GameInitSettings::PlayerInfo(getHouseNameByNumber(static_cast<HOUSETYPE>(i)), "qBotEasy"));
        init.addHouseInfo(info);
    }
    init.randomSeed = 7;
    auto game = startMatch(init);
    int zones = 0;
    for(const StructureBase* s : structureList) {
        const auto* zone = dynamic_cast<const ZoneStructure*>(s);
        if(!zone) continue;
        ++zones;
        const Coord pos = zone->getLocation();
        skirmishRequire(currentGameMap->getTile(pos.x, pos.y)->getCityZoneDensity() == 0, "A legacy zone did not start empty");
        skirmishRequire(zone->getResidentialPopulation() == 0, "A legacy residential lot started populated");
    }
    skirmishRequire(zones > 0, "Legacy map placed no zones");
    SDL_Log("SKIRMISH_PROBE: legacy map %zu structures, %d zones all at level 0", structureList.size(), zones);
    endMatch(game);
}

void runMenu() {
    {
        RandomSkirmishMenu menu;
        skirmishRequire(menu.houseBox.getNumEntries() == menu.houseCount + 1, "House choice is not Random + every custom-game house");
        skirmishRequire(menu.houseCount >= 8, "Fewer houses than a vanilla custom game fields");
        // Allies 0-6 and enemies 1-7, never more than 7 together: moving one past the total pushes
        // the other down, through the real slider input.
        const int others = maxOtherHouses(menu.houseCount);
        skirmishRequire(others == kMaxOtherHouses, "The engine cannot field the player plus seven houses");
        skirmishRequire(menu.alliesBox.getEntryIntData(menu.alliesBox.getNumEntries() - 1) == kMaxAllies
                        && menu.enemiesBox.getEntryIntData(menu.enemiesBox.getNumEntries() - 1) == kMaxEnemies,
                        "Ally/enemy slider ranges are not 0-6 and 1-7");
        probeSelect(menu.alliesBox, 2);
        probeSelect(menu.enemiesBox, 4);
        menu.alliesBox.handleMouseWheel(1, 1, true); menu.alliesBox.handleMouseWheel(1, 1, true);   // 2 -> 4 allies
        skirmishRequire(menu.alliesBox.getSelectedEntryIntData() == 4 && menu.enemiesBox.getSelectedEntryIntData() == 3,
                        "Raising allies past the total did not lower enemies");
        for(int i = 0; i < 8; ++i) menu.enemiesBox.handleMouseWheel(1, 1, true);                    // enemies to 7
        skirmishRequire(menu.enemiesBox.getSelectedEntryIntData() == 7 && menu.alliesBox.getSelectedEntryIntData() == 0,
                        "Raising enemies to 7 did not lower allies to 0");
        for(int i = 0; i < 8; ++i) menu.alliesBox.handleMouseWheel(1, 1, true);                     // allies to 6
        skirmishRequire(menu.alliesBox.getSelectedEntryIntData() == 6 && menu.enemiesBox.getSelectedEntryIntData() == 1,
                        "Raising allies to 6 did not leave exactly one enemy");
        SDL_Log("SKIRMISH_PROBE: allies 0-6, enemies 1-7, together at most 7");
        probeSelect(menu.alliesBox, 1);
        probeSelect(menu.enemiesBox, 3);
        menu.onPlayersChanged(false);
        // Sliders run low (left) to high (right): a click at the left end picks the smallest map,
        // dragging to the right end the largest, and the wheel steps one size at a time.
        {
            Slider& size = menu.sizeBox;
            const int midY = size.getSize().y / 2;
            int changes = 0;
            size.setOnSelectionChange([&](bool interactive) { if(interactive) ++changes; });
            probeSelect(size, 128);
            size.handleMouseLeft(1, midY, true); size.handleMouseLeft(1, midY, false);
            skirmishRequire(size.getSelectedEntryIntData() == kMapSizes.front(), "Left end of the size slider is not the smallest map");
            size.handleMouseLeft(1, midY, true);
            size.handleMouseMovement(size.getSize().x - 1, midY, false);
            size.handleMouseLeft(size.getSize().x - 1, midY, false);
            skirmishRequire(size.getSelectedEntryIntData() == kMapSizes.back(), "Dragging to the end of the track is not the largest map");
            size.handleMouseWheel(1, midY, false);
            skirmishRequire(size.getSelectedEntryIntData() == kMapSizes[kMapSizes.size() - 2], "Wheel down did not step one size smaller");
            skirmishRequire(changes == 3, "The slider did not report each player change");
            size.setOnSelectionChange(nullptr);
            SDL_Log("SKIRMISH_PROBE: sliders go low to high (click, drag, wheel)");
        }
        probeSelect(menu.sizeBox, 96);
        // Game rules: the button opens the same window as Custom Game, and what it returns is
        // what the match starts with.
        {
            skirmishRequire(!menu.rulesChosen, "Rules counted as chosen before the window was used");
            menu.onGameRules();
            auto* rulesWindow = dynamic_cast<GameOptionsWindow*>(menu.pChildWindow);
            skirmishRequire(rulesWindow != nullptr, "Game rules button did not open the rules window");
            const bool fog = rulesWindow->fogOfWarCheckbox.isChecked();
            const bool explored = rulesWindow->startWithExploredMapCheckbox.isChecked();
            rulesWindow->fogOfWarCheckbox.setChecked(!fog);
            rulesWindow->startWithExploredMapCheckbox.setChecked(!explored);
            rulesWindow->rememberDefaults.setChecked(false);
            rulesWindow->onOK();
            menu.processChildWindowOpenCloses();
            skirmishRequire(menu.rulesChosen && menu.matchRules().fogOfWar == !fog
                            && menu.matchRules().startWithExploredMap == !explored,
                            "The match does not take the rules chosen in the window");
            SDL_Log("SKIRMISH_PROBE: game rules window sets the match rules (fog %d -> %d, explored %d -> %d)",
                    int(fog), int(!fog), int(explored), int(!explored));
        }
        // The Mod box lists every installed mod; City rows appear only for a city-sim mod.
        auto chooseMod = [&](const std::string& name) {
            for(size_t i = 0; i < menu.availableMods.size(); ++i)
                if(menu.availableMods[i].name == name) { menu.modBox.setSelectedItem(static_cast<int>(i)); menu.onModChanged(); return; }
            skirmishRequire(false, "Mod box does not list " + name);
        };
        skirmishRequire(menu.modBox.getNumEntries() == static_cast<int>(ModManager::instance().listModChoices().size()),
                        "Mod box does not list every installed mod");
        for(const char* name : {"vanilla", "Tornie"}) {
            chooseMod(name);
            skirmishRequire(!menu.enemyCityBox.isVisible() && !menu.allyCityBox.isVisible() && !menu.enemyCityCaption->isVisible(),
                            std::string("City rows shown for non-city mod ") + name);
            if(std::string(name) == "vanilla") skirmishCapture(menu, "random-skirmish-vanilla");
        }
        chooseMod("dunecity");
        skirmishRequire(menu.enemyCityBox.isVisible() && menu.enemyCityCaption->isVisible() && menu.enemyCityBox.isEnabled(),
                        "City rows hidden for the DuneCity mod");
        probeSelect(menu.enemyCityBox, 2);
        probeSelect(menu.techBox, 7);
        menu.seedBox.setText("123456789");
        menu.fixedSeedBox.setChecked(true);
        skirmishCapture(menu, "random-skirmish");
        menu.remember(menu.readParams());

        menu.onSaveMap();
        skirmishCapture(menu, "random-skirmish-saved", true);
        char dir[FILENAME_MAX];
        fnkdat("maps/multiplayer/", dir, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
        const auto files = getFileNamesList(dir, "ini", true);
        skirmishRequire(files.size() == 1, "Save map did not write exactly one map");
        INIFile saved(std::string(dir) + files.front());
        skirmishRequire(saved.getIntValue("BASIC", "TechLevel") == 7 && saved.getIntValue("MAP", "SizeX") == 96,
                        "Saved map does not carry the chosen size and tech level");
        skirmishRequire(saved.getStringValue("BASIC", "Mod") == "dunecity", "Saved map is not marked as a DuneCity map");
        SDL_Log("SKIRMISH_PROBE: saved %s", files.front().c_str());
    }
    {
        // Last-used parameters come back on the next visit, seed included when kept.
        RandomSkirmishMenu again;
        const Params p = again.readParams();
        skirmishRequire(p.mapSize == 96 && p.allies == 1 && p.enemies == 3 && p.mod == "dunecity" && p.mode == Mode::DuneCity
                        && p.enemySide.city == 2 && p.techLevel == 7 && p.seed == 123456789u && p.fixedSeed,
                        "Last-used parameters were not restored");
        skirmishCapture(again, "random-skirmish-restored");
    }
}

/**
    Spectate, through the real Random Skirmish screen and its Start step (prepareStart is everything
    Start does before the blocking game loop): clicking Spectate enables the house-AI choice; the
    start is a local-spectator one with no human anywhere, the player's house played by the chosen
    AI plus its partner, every house AI; the match runs as an observed game. The choice is
    remembered, and clicking Spectate off gives the player's seat back to the human.
*/
void runSpectate() {
    const auto click = [](Button& button) {
        button.handleMouseLeft(1, 1, true);
        button.handleMouseLeft(1, 1, false);
    };
    const auto classesOf = [](const House* house) {
        std::vector<std::string> classes;
        for(const auto& player : house->getPlayerList()) classes.push_back(player->getPlayerclass());
        return classes;
    };
    GameInitSettings watched;
    {
        RandomSkirmishMenu menu;
        skirmishRequire(menu.spectateBox.getParent() != nullptr && menu.spectateBox.isVisible() && !menu.spectateBox.isChecked(),
                        "Spectate is not offered, or starts on, in a fresh profile");
        skirmishRequire(!menu.spectateAIBox.isVisible() && !menu.spectateAI.aiBox.isEnabled() && !menu.spectateAICaption->isVisible(),
                        "The house AI row is shown while not spectating");
        skirmishRequire(!menu.spectateBox.getTooltipText().empty(), "Spectate has no tooltip");
        for(size_t i = 0; i < menu.availableMods.size(); ++i)
            if(menu.availableMods[i].name == "dunecity") { menu.modBox.setSelectedItem(static_cast<int>(i)); menu.onModChanged(); }
        probeSelect(menu.sizeBox, 96);
        probeSelect(menu.houseBox, HOUSE_ORDOS);
        probeSelect(menu.alliesBox, 1);
        probeSelect(menu.enemiesBox, 2);
        menu.onPlayersChanged(false);
        // The partner may build, fight, or both; this match keeps the Builder.
        for(const char* partnerClass : {"qBotFighterMedium", "qBotMedium", "qBotSupportMedium"})
            skirmishRequire(menu.partnerAI.selectPlayerClass(partnerClass) && menu.partnerAI.getPlayerClass() == partnerClass,
                            std::string("The AI partner choice does not offer ") + partnerClass);
        probeSelect(menu.playerBaseBox, static_cast<int>(BaseSize::Small));
        probeSelect(menu.playerCityBox, 1);
        menu.seedBox.setText("20261018");
        menu.fixedSeedBox.setChecked(true);
        skirmishCapture(menu, "random-skirmish-spectate-off");
        click(menu.spectateBox);
        skirmishRequire(menu.spectateBox.isChecked() && menu.spectateAIBox.isVisible() && menu.spectateAI.aiBox.isEnabled()
                        && menu.spectateAICaption->isVisible(), "Clicking Spectate did not show the house AI choice");
        skirmishRequire(menu.playerHeader->getText() == _("Your house starts with"), "The player's start header does not follow Spectate");
        // The player's start rows stay live: the watched house still starts with them.
        skirmishRequire(menu.partnerAI.aiBox.isEnabled() && menu.playerBaseBox.isEnabled() && menu.playerArmyBox.isEnabled(),
                        "Spectate disabled the watched house's start rows");
        // The house AI is a house of its own: every AI and difficulty plays Builder + Fighter.
        for(int i = 0; i < menu.spectateAI.aiBox.getNumEntries(); ++i) {
            menu.spectateAI.aiBox.setSelectedItem(i);
            for(int j = 0; j < menu.spectateAI.difficultyBox.getNumEntries(); ++j) {
                menu.spectateAI.difficultyBox.setSelectedItem(j);
                const auto* data = PlayerFactory::getByPlayerClass(menu.spectateAI.getPlayerClass());
                skirmishRequire(data != nullptr && data->getPlayerClass() != HUMANPLAYERCLASS && data->getRole() == AIRole::Both,
                                "The house AI choice offers " + menu.spectateAI.getPlayerClass());
            }
        }
        skirmishRequire(!menu.spectateAI.selectPlayerClass("qBotSupportMedium") && !menu.spectateAI.selectPlayerClass("qBotFighterMedium"),
                        "The house AI choice offers a Builder or Fighter");
        skirmishRequire(menu.spectateAI.selectPlayerClass("qBotHard"), "The house AI choice does not offer qBotHard");
        skirmishCapture(menu, "random-skirmish-spectate");
        skirmishRequire(menu.prepareStart(watched), "Start could not prepare the spectated match");
    }
    skirmishRequire(watched.isLocalSpectator(), "The spectated start is not a local-spectator start");
    for(const auto& info : watched.getHouseInfoList())
        for(const auto& player : info.playerInfoList)
            skirmishRequire(player.playerClass != HUMANPLAYERCLASS, "A spectated start seats a human");

    // The observed match: every house AI, the player's house the chosen AI plus its partner.
    {
        auto game = startMatch(watched);
        skirmishRequire(game->isObserving() && game->localObserver, "The spectated match is not observed");
        skirmishRequire(pLocalPlayer != nullptr && pLocalPlayer->getPlayerID() == Player::OBSERVER_PLAYER_ID,
                        "The local view is not the unregistered observer");
        int houses = 0;
        for(int h = 0; h < NUM_HOUSES; ++h) {
            const House* house = game->getHouse(h);
            if(house == nullptr) continue;
            ++houses;
            for(const auto& player : house->getPlayerList())
                skirmishRequire(dynamic_cast<const HumanPlayer*>(player.get()) == nullptr, "A house has a human player while spectating");
        }
        skirmishRequire(houses == 4, "The spectated match does not hold the player's house, one ally and two enemies");
        skirmishRequire(classesOf(game->getHouse(HOUSE_ORDOS)) == std::vector<std::string>{"qBotHard", "qBotSupportMedium"},
                        "The watched house is not played by the chosen AI and its partner");
        const StructureBase* yard = nullptr;
        for(const StructureBase* s : structureList)
            if(s->getOwner()->getHouseID() == HOUSE_ORDOS && s->getItemID() == Structure_ConstructionYard) yard = s;
        skirmishRequire(yard != nullptr, "The watched house did not get its configured base");
        SDL_Log("SKIRMISH_PROBE: spectated match loaded, %d houses, all AI, watched house Ordos qBotHard+qBotSupportMedium", houses);
        SDL_Log("SKIRMISH_SMOKE_BEGIN: spectated");
        const Uint32 cycles = MILLI2CYCLES(60 * 1000);
        while(!game->finished && game->getGameCycleCount() < cycles) game->updateGameState();
        SDL_Log("SKIRMISH_SMOKE_END: cycle=%u finished=%d", game->getGameCycleCount(), game->finished ? 1 : 0);
        skirmishRequire(!game->finished && game->getGameCycleCount() >= cycles, "The spectated match did not run its minute");
        skirmishRequire(game->isObserving() && pLocalPlayer->getPlayerID() == Player::OBSERVER_PLAYER_ID, "Observation ended while playing");
        captureGame(*game, "ingame-spectator", Coord(yard->getLocation().x + 1, yard->getLocation().y + 1));
        SDL_Log("SKIRMISH_PROBE: spectated match ran %u cycles", game->getGameCycleCount());
        endMatch(game);
    }

    // Remembered; clicking Spectate off gives the seat back to the human.
    GameInitSettings played;
    {
        RandomSkirmishMenu again;
        skirmishRequire(again.spectateBox.isChecked() && again.spectateAIBox.isVisible() && again.spectateAI.aiBox.isEnabled()
                        && again.spectateAI.getPlayerClass() == "qBotHard",
                        "Spectate and its house AI were not remembered");
        click(again.spectateBox);
        skirmishRequire(!again.spectateBox.isChecked() && !again.spectateAIBox.isVisible() && !again.spectateAICaption->isVisible()
                        && again.playerHeader->getText() == _("You start with"), "Clicking Spectate off did not restore the player's rows");
        skirmishRequire(again.prepareStart(played), "Start could not prepare the played match");
    }
    skirmishRequire(!played.isLocalSpectator(), "Turning Spectate off still starts a spectator match");
    {
        auto game = startMatch(played);
        skirmishRequire(!game->isObserving(), "The played match is observed");
        const House* own = game->getHouse(HOUSE_ORDOS);
        skirmishRequire(own != nullptr && classesOf(own) == std::vector<std::string>{HUMANPLAYERCLASS, "qBotSupportMedium"},
                        "The player's house does not hold the human and the partner again");
        skirmishRequire(dynamic_cast<HumanPlayer*>(pLocalPlayer) != nullptr && pLocalPlayer->getHouse() == own
                        && pLocalPlayer->getPlayername() == settings.general.playerName, "The human does not control the player's house");
        for(int i = 0; i < 100; ++i) game->updateGameState();
        SDL_Log("SKIRMISH_PROBE: Spectate off seats %s in Ordos with its partner", settings.general.playerName.c_str());
        endMatch(game);
    }
    {
        RandomSkirmishMenu third;
        skirmishRequire(!third.spectateBox.isChecked(), "Spectate off was not remembered");
    }
}

/**
    Multiplayer Random map, host side and a second peer, against the real menus and engine:
    Custom Game (LAN host) offers Random map...; its screen has no allies/enemies/AI rows and
    mirrors starts by default; Next saves an open-start map to MP User Maps; the host's map
    list selects it; the lobby built from it (as Next builds it) seats the host, a joining guest
    and two AI on the four starts with teams; the guest receives the host's game info and roster
    in the wire format; both peers start the match, every house sits on its own start with the
    start's exact contents, and after a minute both peers report the same state digest.
*/
void runMultiplayerLobby() {
    const std::string hostName = settings.general.playerName, guestName = "Guest tester";
    skirmishRequire(RandomSkirmish::activateMod("dunecity"), "dunecity mod did not activate");
    {
        CustomGameMenu lan(true, true);
        skirmishRequire(lan.randomMapButton.isVisible() && lan.randomMapButton.isEnabled(), "LAN host has no Random map button");
        CustomGameMenu single(false);
        skirmishRequire(!single.randomMapButton.isVisible(), "Single-player Custom Game shows the Random map button");
    }

    // The host's Random map step.
    std::string path;
    Params params;
    {
        RandomSkirmishMenu menu(std::string("dunecity"));
        for(Widget* hidden : std::initializer_list<Widget*>{&menu.alliesBox, &menu.enemiesBox, &menu.enemiesAlliedBox, &menu.houseBox,
                                                            &menu.partnerBox, &menu.allyAIBox, &menu.enemyAIBox, &menu.modBox,
                                                            &menu.partnerAI.roleBox, &menu.spectateBox, &menu.spectateAIBox,
                                                            &menu.rulesButton, &menu.saveButton, &menu.startButton})
            skirmishRequire(hidden->getParent() == nullptr, "The multiplayer screen shows a single-player control");
        skirmishRequire(menu.symmetricBox.isChecked(), "Mirrored starts are not on by default for a hosted map");
        skirmishRequire(menu.startsBox.getEntryIntData(0) == kMinStarts
                        && menu.startsBox.getEntryIntData(menu.startsBox.getNumEntries() - 1) == maxStarts(menu.houseCount),
                        "Start positions do not run from 2 to the house cap");
        skirmishRequire(maxStarts(menu.houseCount) == kMaxStarts, "The engine cannot field eight starts");
        skirmishRequire(menu.startCityBox.isVisible() && menu.startCityCaption->isVisible(), "City row hidden for the DuneCity host mod");
        skirmishRequire(!menu.hostModLabel.getText().empty(), "The host's mod is not shown");
        probeSelect(menu.sizeBox, 128);
        probeSelect(menu.startsBox, 4);
        probeSelect(menu.startBaseBox, static_cast<int>(BaseSize::Medium));
        probeSelect(menu.startArmyBox, kArmyRandom);
        probeSelect(menu.startCityBox, 2);
        probeSelect(menu.creditsBox, 5000);
        probeSelect(menu.techBox, 9);
        menu.seedBox.setText("20261015");
        menu.fixedSeedBox.setChecked(true);
        skirmishCapture(menu, "mp-random-map");
        params = menu.readParams();
        skirmishRequire(params.multiplayer && params.starts == 4 && params.mod == "dunecity" && params.mode == Mode::DuneCity
                        && params.symmetric && params.startSide.army == kArmyRandom, "The screen does not read back its choices");
        menu.onCreateMap();
        path = menu.getCreatedMap();
        skirmishRequire(!path.empty() && existsFile(path), "Next did not save the map");
    }
    const Result result = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
    skirmishRequire(readCompleteFile(path) == result.ini, "The saved map is not the generated one");
    for(const HouseSetup& h : result.houses) {
        skirmishRequire(h.skipped == 0 && h.armyBudget == result.houses[0].armyBudget && h.cityTarget == result.houses[0].cityTarget,
                        "Starts differ in the generated plan");
    }
    SDL_Log("SKIRMISH_PROBE: multiplayer map %s army=%d city=%d", getBasename(path).c_str(), result.houses[0].armyBudget,
            result.houses[0].cityTarget);

    // The host's map choice selects it, as Next would see it.
    CustomGameMenu host(true, true);
    skirmishRequire(host.selectUserMap(path) && strToLower(host.getSelectedMapPath()) == strToLower(path),
                    "The host's map list did not select the generated map");
    skirmishCapture(host, "mp-map-chosen");
    {
        // Custom Game's Offline/Online chooser (Play Online hosting) takes the new file into its
        // fixed map list, and its Next hands that map to the players step.
        CustomPlaySetup setup;
        setup.rules = effectiveGameOptions;
        setup.mods = ModManager::instance().listModChoices();
        for(size_t i = 0; i < setup.mods.size(); ++i) if(setup.mods[i].name == "dunecity") setup.mod = static_cast<int>(i);
        setup.online = true;
        CustomGameMenu browser(false, false, &setup);
        skirmishRequire(browser.randomMapButton.isVisible() && browser.randomMapButton.isEnabled(), "Custom Game setup has no Random map button");
        skirmishRequire(browser.selectUserMap(path), "The setup map list did not select the generated map");
        browser.onNext();
        skirmishRequire(setup.map >= 0 && setup.map < static_cast<int>(setup.maps.size()) && strToLower(setup.maps[setup.map]) == strToLower(path),
                        "The setup's players step would not get the generated map");
        SDL_Log("SKIRMISH_PROBE: online setup continues with %s", getBasename(setup.maps[setup.map]).c_str());
    }

    // The lobby exactly as CustomGameMenu::onNext opens it for a LAN host.
    GameInitSettings hostInit(getBasename(path, true), readCompleteFile(path), hostName + "'s Game", false, host.currentGameOptions);
    CustomGamePlayers lobby(hostInit, true, true);
    skirmishRequire(lobby.numHouses == 4, "The lobby does not offer the four starts");
    // A seated human's seat player is 0 (PLAYER_HUMAN, private to CustomGamePlayers.cpp).
    skirmishRequire(lobby.getSeatPlayer(0) == 0 && lobby.getSeatName(0) == hostName, "The host is not seated");
    // A guest joins: the host seats it on the first open start and sends it the game and roster.
    OMemoryStream joinPacket;
    joinPacket.open();
    lobby.gameInitSettings.save(joinPacket);
    // getChangeEventListForNewPlayer() seats a joiner on the first open start (row 1) with
    // setPlayer2Slot() and then broadcasts; there is no socket here, so seat it the same way.
    lobby.setPlayer2Slot(guestName, 2);
    skirmishRequire(lobby.getSeatName(2) == guestName, "The guest was not seated on a start");
    // The other starts get AI; houses and teams are set here: two teams of two.
    const int ai = PlayerFactory::getIndexByPlayerClass("qBotEasy");
    const int houses[] = {HOUSE_ATREIDES, HOUSE_HARKONNEN, HOUSE_INVALID, HOUSE_ORDOS};
    const int teams[] = {1, 2, 1, 2};
    for(int row = 0; row < 4; ++row) {
        auto& info = lobby.houseInfo[row];
        if(row >= 2) {
            // The AI box's QuantBot with its first difficulty and role, as a user picks it.
            skirmishRequire(lobby.selectSeatPlayer(2 * row, ai), "The lobby does not offer QuantBot Easy");
            lobby.onChangePlayerDropDownBoxes(true, 2 * row);
        }
        probeSelect(info.houseDropDown, houses[row]);
        lobby.onChangeHousesDropDownBoxes(true, row);
        probeSelect(info.teamDropDown, teams[row]);
        lobby.onChangeTeamDropDownBoxes(true, row);
    }
    lobby.update();
    skirmishCapture(lobby, "mp-lobby");
    lobby.addAllPlayersToGameInitSettings();
    const GameInitSettings hostStart = lobby.gameInitSettings;
    skirmishRequire(hostStart.getHouseInfoList().size() == 4, "The lobby did not start four houses");

    // The guest's side: its game info comes off the wire, its roster from the host's change events.
    settings.general.playerName = guestName;
    GameInitSettings guestStart;
    {
        IMemoryStream wire(joinPacket.getData(), joinPacket.getDataLength());
        GameInitSettings received(wire);
        skirmishRequire(received.getFiledata() == hostInit.getFiledata(), "The map did not reach the guest unchanged");
        CustomGamePlayers guestLobby(received, false, true);
        guestLobby.onReceiveChangeEventList(lobby.getChangeEventList());
        skirmishRequire(guestLobby.numHouses == 4, "The guest's lobby does not show the four starts");
        guestLobby.addAllPlayersToGameInitSettings();
        guestStart = guestLobby.gameInitSettings;
    }
    settings.general.playerName = hostName;
    auto bytes = [](const GameInitSettings& init) {
        OMemoryStream stream; stream.open(); init.save(stream);
        return std::string(stream.getData(), stream.getDataLength());
    };
    skirmishRequire(bytes(guestStart) == bytes(hostStart), "Host and guest would start different games");
    SDL_Log("SKIRMISH_PROBE: guest received %zu map bytes and the same %zu-byte start settings",
            guestStart.getFiledata().size(), bytes(guestStart).size());

    // Both peers play the first minute; every house is on its own start with that start's contents.
    const Uint32 minute = MILLI2CYCLES(60 * 1000);
    std::string digests[2];
    for(int peer = 0; peer < 2; ++peer) {
        settings.general.playerName = peer == 0 ? hostName : guestName;
        auto game = startMatch(peer == 0 ? hostStart : guestStart);
        skirmishRequire(game->isCitySimEnabled(), "City mode did not reach the match");
        skirmishRequire(pLocalHouse != nullptr && pLocalHouse->getHouseID() == (peer == 0 ? HOUSE_ATREIDES : HOUSE_HARKONNEN),
                        "A peer does not control the house it chose");
        std::set<int> startsTaken;
        int housesInGame = 0;
        for(int h = 0; h < NUM_HOUSES; ++h) {
            const House* house = game->getHouse(h);
            if(!house) continue;
            ++housesInGame;
            const StructureBase* yard = nullptr;
            for(const StructureBase* s : structureList) if(s->getOwner() == house && s->getItemID() == Structure_ConstructionYard) yard = s;
            skirmishRequire(yard != nullptr, "A house has no Construction Yard");
            int start = -1;
            for(const HouseSetup& setup : result.houses)
                if(setup.plateau.contains(yard->getLocation().x, yard->getLocation().y)) start = setup.house;
            skirmishRequire(start >= 0 && startsTaken.insert(start).second, "A house is not on a start of its own");
            std::map<int, int> wantStructures, haveStructures, wantUnits, haveUnits;
            for(const Placement& s : result.structures) if(s.house == start) ++wantStructures[s.item];
            for(const Placement& u : result.units) if(u.house == start) ++wantUnits[u.item];
            for(const StructureBase* s : structureList) if(s->getOwner() == house) ++haveStructures[s->getItemID()];
            for(const UnitBase* u : unitList) if(u->getOwner() == house && wantUnits.count(u->getItemID())) ++haveUnits[u->getItemID()];
            skirmishRequire(haveStructures == wantStructures && haveUnits == wantUnits, "A house does not hold its start's contents");
            // Teams and controllers are the lobby's.
            const auto& players = house->getPlayerList();
            skirmishRequire(players.size() == 1, "A start holds more than one controller");
            const std::string controller = players.front()->getPlayerclass(), name = players.front()->getPlayername();
            const int expectedTeam = h == HOUSE_ATREIDES ? 1 : h == HOUSE_HARKONNEN ? 2 : h == HOUSE_ORDOS ? 2 : 1;
            skirmishRequire(house->getTeamID() == expectedTeam, "Team differs from the lobby for house " + std::to_string(h));
            skirmishRequire(h == HOUSE_ATREIDES ? (controller == HUMANPLAYERCLASS && name == hostName)
                            : h == HOUSE_HARKONNEN ? (controller == HUMANPLAYERCLASS && name == guestName)
                            : controller == "qBotEasy", "Controller differs from the lobby for house " + std::to_string(h));
            int structures = 0, units = 0;
            for(const auto& entry : haveStructures) structures += entry.second;
            for(const auto& entry : haveUnits) units += entry.second;
            if(peer == 0)
                SDL_Log("SKIRMISH_PROBE: house=%d start=Player%d team=%d controller=%s structures=%d units=%d credits=%d",
                        h, start + 1, house->getTeamID(), name.c_str(), structures, units, house->getCredits());
        }
        skirmishRequire(housesInGame == 4, "The match does not hold four houses");
        if(peer == 0) renderPicture(pictureOfGame(), "loaded-mp-lobby");
        if(peer == 0) SDL_Log("SKIRMISH_SMOKE_BEGIN: multiplayer host");
        while(!game->finished && game->getGameCycleCount() < minute) game->updateGameState();
        if(peer == 0) SDL_Log("SKIRMISH_SMOKE_END: cycle=%u", game->getGameCycleCount());
        skirmishRequire(!game->finished, "The match ended inside its first minute");
        digests[peer] = GameStateDigest::describe(game->computeStateDigest());
        SDL_Log("SKIRMISH_PROBE: %s digest at cycle %u: %s", peer == 0 ? "host" : "guest", game->getGameCycleCount(), digests[peer].c_str());
        endMatch(game);
    }
    settings.general.playerName = hostName;
    skirmishRequire(digests[0] == digests[1], "Host and guest disagree about the game state");
}

}

int runSkirmishProbe() {
    settings.general.playerName = "Skirmish tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    const std::string kind = std::getenv("SKIRMISH_PROBE_CASE") ? std::getenv("SKIRMISH_PROBE_CASE") : "";
    if(kind == "menu") runMenu();
    else if(kind == "mp-lobby") runMultiplayerLobby();
    else if(kind == "legacy-map") runLegacyMap();
    else if(kind == "spectate") runSpectate();
    else if(kind == "render") runRender();
    else runMatch(kind, std::getenv("SKIRMISH_PROBE_SMOKE") != nullptr);
    SDL_Log("SKIRMISH_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
