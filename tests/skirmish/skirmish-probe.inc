// Included only by run-skirmish-probe.py, never by the shipped game. Drives the real Random
// Skirmish screen and the real CustomGame start/load path with an isolated profile.
#include <Menu/RandomSkirmishMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <MapEditor/MapGenerator.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/LoadSavePNG.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <players/Player.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityConstants.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/draw_util.h>
#include <misc/fnkdat.h>
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
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions);
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
        // Allies 3 leave room for houseCount - 4 enemies, never more than five.
        probeSelect(menu.alliesBox, 3);
        menu.onPlayersChanged();
        skirmishRequire(menu.enemiesBox.getNumEntries() == std::min(kMaxEnemies, menu.houseCount - 4), "Enemy count not clamped to the house cap");
        probeSelect(menu.alliesBox, 1);
        menu.onPlayersChanged();
        probeSelect(menu.enemiesBox, 3);
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

}

int runSkirmishProbe() {
    settings.general.playerName = "Skirmish tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    const std::string kind = std::getenv("SKIRMISH_PROBE_CASE") ? std::getenv("SKIRMISH_PROBE_CASE") : "";
    if(kind == "menu") runMenu();
    else if(kind == "legacy-map") runLegacyMap();
    else if(kind == "render") runRender();
    else runMatch(kind, std::getenv("SKIRMISH_PROBE_SMOKE") != nullptr);
    SDL_Log("SKIRMISH_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
