// Real-engine probe for the modern RTS controls (docs/rts-controls.md).
// Included only by tests/units/run-rts-controls-probe.py; never linked into the game.
//
// Real units on a real map, real commands through Command::executeCommand and real game
// cycles through Game::updateGameState, so "arrived", "never moved" and "produced" are
// measured, not assumed:
//   1. an attack-move column fights an enemy standing on its route, then arrives in Area Guard;
//   2. a holding launcher being shot never leaves its tile and still fires back;
//   3. a three-waypoint Shift queue is walked in order;
//   4. repeat production builds three units with nobody touching the factory, pauses at the
//      unit limit without being cancelled, and resumes when room frees up;
//   5. queue, HOLD and repeat flags survive a save and load;
//   6. a SAVEGAMEVERSION 9854 save (committed fixture) loads with empty queues and no repeat;
//   7. a rendered frame shows the queued waypoint lines and a hotkey tooltip (smoke screenshot).
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Command.h>
#include <ScreenBorder.h>
#include <GameInterface.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <mod/ModManager.h>
#include <structures/BuilderBase.h>
#include <units/UnitBase.h>
#include <units/UnitOrderQueue.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

void rtsRequire(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("RTS_CONTROLS_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

std::unique_ptr<Game> rtsLoad(const std::string& path) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    return game;
}

bool rtsHasColor(const std::vector<Uint32>& pixels, int width, const SDL_Rect& area, Uint8 r, Uint8 g, Uint8 b) {
    for(int y = area.y; y < area.y + area.h; ++y)
        for(int x = area.x; x < area.x + area.w; ++x) {
            const Uint32 p = pixels[y*width + x];
            if(((p >> 16) & 0xff) == r && ((p >> 8) & 0xff) == g && (p & 0xff) == b) return true;
        }
    return false;
}

} // namespace

int runRtsControlsProbe() {
    const std::string out = std::getenv("RTS_PROBE_OUT");
    const std::string smoke = std::getenv("RTS_PROBE_SMOKE") ? std::getenv("RTS_PROBE_SMOKE") : out;
    rtsRequire(ModManager::instance().setActiveMod("vanilla"), "Missing vanilla mod");
    settings.general.playerName = "Legacy";

    // ---- 6. The 9854 fixture loads with empty queues and no repeating items -----------------
    {
        auto legacy = rtsLoad(RTS_FIXTURE_PATH);
        rtsRequire(legacy->getLoadedSavegameVersion() == 9854, "The fixture is not a 9854 save");
        for(const UnitBase* unit : unitList)
            rtsRequire(unit->getOrderQueue().empty(), "A 9854 unit loaded with queued orders");
        auto* factory = dynamic_cast<BuilderBase*>(legacy->getObjectManager().getObject(5));
        rtsRequire(factory != nullptr && factory->repeatItems.empty() && factory->getProductionQueueSize() == 2,
                   "The 9854 factory lost its queue or gained repeat items");
        auto* launcher = dynamic_cast<UnitBase*>(legacy->getObjectManager().getObject(9));
        rtsRequire(launcher != nullptr && launcher->getAttackMode() == AREAGUARD, "The 9854 launcher changed mode");
        std::vector<Coord> before;
        for(Uint32 id : {6u, 7u, 8u}) {
            auto* mover = dynamic_cast<UnitBase*>(legacy->getObjectManager().getObject(id));
            rtsRequire(mover != nullptr, "A 9854 mover is missing");
            before.push_back(mover->getLocation());
        }
        for(int i = 0; i < 300; ++i) legacy->updateGameState();
        int moved = 0;
        for(size_t i = 0; i < 3; ++i) {
            auto* mover = dynamic_cast<UnitBase*>(legacy->getObjectManager().getObject(6 + i));
            if(mover != nullptr && mover->getLocation() != before[i]) ++moved;
        }
        rtsRequire(moved > 0, "The 9854 movers did not continue their saved moves");
        const std::string resaved = out + "/legacy-resaved.dls";
        rtsRequire(legacy->saveGame(resaved), "Could not re-save the 9854 fixture");
        legacy.reset(); currentGame = nullptr;
        auto again = rtsLoad(resaved);
        rtsRequire(again->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The re-saved fixture is not current");
        again.reset(); currentGame = nullptr;
        SDL_Log("RTS_CONTROLS_LEGACY_PASS: 9854 save loaded, %d movers continued, re-saved as %d", moved, SAVEGAMEVERSION);
    }

    // ---- Fixture match --------------------------------------------------------------------
    settings.general.playerName = "Controls tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("rts.ini", mapData, "RTS controls", true, settings.gameOptions);
    init.randomSeed = 71;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Controls tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    game->initializeGameLoop();
    House* owner = game->getHouse(HOUSE_ATREIDES);
    House* enemy = game->getHouse(HOUSE_HARKONNEN);
    const Player* tester = game->getPlayerByName("Controls tester");
    rtsRequire(owner && enemy && tester, "Fixture houses missing");
    const Uint8 pid = tester->getPlayerID();
    owner->maxUnits = 0;

    const int W = 32, H = 22;
    Coord O = Coord::Invalid();
    for(int y = 2; y + H < currentGameMap->getSizeY() - 2 && O.isInvalid(); ++y)
        for(int x = 2; x + W < currentGameMap->getSizeX() - 2 && O.isInvalid(); ++x) {
            bool clear = true;
            for(int dy = 0; dy < H && clear; ++dy) for(int dx = 0; dx < W && clear; ++dx)
                if(currentGameMap->getTile(x+dx, y+dy)->hasAnObject()) clear = false;
            if(clear) O = Coord(x, y);
        }
    rtsRequire(O.isValid(), "No free area for the controls fixture");
    for(int dy = 0; dy < H; ++dy) for(int dx = 0; dx < W; ++dx) {
        auto* tile = currentGameMap->getTile(O.x+dx, O.y+dy);
        tile->setType(Terrain_Sand);
        tile->setExplored(owner->getHouseID(), game->getGameCycleCount());
    }
    auto spawn = [&](House* house, int item, Coord at) {
        auto* unit = house->createUnit(item, true);
        rtsRequire(unit != nullptr, "Unit creation failed");
        unit->deploy(O + at);
        unit->setVisible(owner->getTeamID(), true);
        unit->setVisible(enemy->getTeamID(), true);
        return unit->getObjectID();
    };
    auto unitOf = [&](Uint32 id) { return dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id)); };
    auto step = [&](int cycles) { for(int i = 0; i < cycles; ++i) game->updateGameState(); };
    auto retire = [&](Uint32 id) { if(auto* u = unitOf(id)) u->setHealth(0); step(2); };
    using UnitOrderPolicy::tileDistance;

    // ---- 1. Attack-move column ------------------------------------------------------------
    {
        const Coord goal = O + Coord(26, 6);
        std::vector<Uint32> column = {spawn(owner, Unit_Tank, Coord(1,5)), spawn(owner, Unit_Tank, Coord(1,6)),
                                      spawn(owner, Unit_Tank, Coord(1,7))};
        const Uint32 foe = spawn(enemy, Unit_Trike, Coord(13, 6));
        unitOf(foe)->doSetAttackMode(GUARD);
        for(Uint32 id : column) {
            unitOf(id)->doSetAttackMode(GUARD);
            Command(pid, CMD_UNIT_ATTACKMOVE, id, goal.x, goal.y, 0).executeCommand();
            rtsRequire(unitOf(id)->isAttackMoving(), "A column tank did not start an attack-move");
        }
        bool targeted = false, hitOnRoute = false, foeDeadOnRoute = false;
        int cycles = 0;
        for(; cycles < 6000; ++cycles) {
            step(1);
            bool walking = false;
            for(Uint32 id : column) {
                const UnitBase* tank = unitOf(id);
                rtsRequire(tank != nullptr, "A column tank died");
                walking = walking || !tank->getOrderQueue().empty();
                if(tank->getTarget() != nullptr && tank->getTarget()->getObjectID() == foe) targeted = true;
            }
            const ObjectBase* target = game->getObjectManager().getObject(foe);
            if(walking && (target == nullptr || target->getHealth() < target->getMaxHealth())) hitOnRoute = true;
            if(walking && target == nullptr) foeDeadOnRoute = true;
            if(!walking && target == nullptr) break;
        }
        SDL_Log("RTS_CONTROLS_DIAG: attack-move cycles=%d targeted=%d hit=%d killed=%d", cycles,
                int(targeted), int(hitOnRoute), int(foeDeadOnRoute));
        rtsRequire(targeted && hitOnRoute && foeDeadOnRoute, "The column did not fight the enemy on its route");
        for(Uint32 id : column) {
            const UnitBase* tank = unitOf(id);
            SDL_Log("RTS_CONTROLS_DIAG: column tank %u at (%d,%d) goal (%d,%d) mode=%d", id,
                    tank->getLocation().x, tank->getLocation().y, goal.x, goal.y, int(tank->getAttackMode()));
            rtsRequire(tank->getOrderQueue().empty() && tank->getAttackMode() == AREAGUARD
                       && tileDistance(tank->getLocation(), goal) <= UnitOrderPolicy::kArrivalRadiusTiles,
                       "A column tank did not arrive at the goal in Area Guard");
        }
        for(Uint32 id : column) retire(id);
        SDL_Log("RTS_CONTROLS_ATTACKMOVE_PASS: three tanks fought the trike on route and arrived in Area Guard after %d cycles", cycles);
    }

    // ---- 2. Holding launcher under fire ---------------------------------------------------
    {
        const Uint32 launcherID = spawn(owner, Unit_Launcher, Coord(4, 12));
        Command(pid, CMD_UNIT_SETMODE, launcherID, HOLD).executeCommand();
        const Coord start = unitOf(launcherID)->getLocation();
        const Uint32 attacker = spawn(enemy, Unit_Tank, Coord(11, 12));
        unitOf(attacker)->doAttackObject(unitOf(launcherID), true);
        bool shot = false, fired = false;
        for(int i = 0; i < 900; ++i) {
            step(1);
            UnitBase* launcher = unitOf(launcherID);
            rtsRequire(launcher != nullptr, "The holding launcher died");
            rtsRequire(launcher->getLocation() == start && launcher->getAttackMode() == HOLD,
                       "The holding launcher moved or left HOLD");
            rtsRequire(launcher->getDestination().isInvalid() || launcher->getDestination() == start,
                       "The holding launcher was given somewhere to go");
            if(launcher->getHealth() < launcher->getMaxHealth()) {
                shot = true;
                launcher->setHealth(launcher->getMaxHealth());   // keep the measurement going
            }
            const ObjectBase* tank = game->getObjectManager().getObject(attacker);
            if(tank == nullptr || tank->getHealth() < tank->getMaxHealth()) fired = true;
            if(tank != nullptr) const_cast<ObjectBase*>(tank)->setHealth(tank->getMaxHealth());
        }
        rtsRequire(shot, "The holding launcher was never shot");
        rtsRequire(fired, "The holding launcher never fired back");
        retire(attacker); retire(launcherID);
        SDL_Log("RTS_CONTROLS_HOLD_PASS: the launcher stayed on its tile in HOLD for 900 cycles under fire and hit back");
    }

    // ---- 3. Three-waypoint Shift queue ----------------------------------------------------
    {
        const Uint32 id = spawn(owner, Unit_Tank, Coord(2, 17));
        const Coord w1 = O + Coord(10, 17), w2 = O + Coord(10, 20), w3 = O + Coord(17, 19);
        Command(pid, CMD_UNIT_MOVE2POS, id, w1.x, w1.y, 1).executeCommand();
        Command(pid, CMD_UNIT_QUEUEORDER, id, static_cast<Uint32>(UnitOrderType::Move), w2.x, w2.y).executeCommand();
        Command(pid, CMD_UNIT_QUEUEORDER, id, static_cast<Uint32>(UnitOrderType::Move), w3.x, w3.y).executeCommand();
        rtsRequire(unitOf(id)->getOrderQueue().size() == 3, "The Shift queue does not hold three waypoints");
        std::vector<int> visits;
        for(int i = 0; i < 5000 && !(visits.size() == 3 && unitOf(id)->getOrderQueue().empty()); ++i) {
            step(1);
            const Coord at = unitOf(id)->getLocation();
            const Coord points[] = {w1, w2, w3};
            for(int k = 0; k < 3; ++k)
                if(at == points[k] && (visits.empty() || visits.back() != k + 1)) visits.push_back(k + 1);
        }
        rtsRequire(visits == std::vector<int>({1, 2, 3}), "The waypoints were not reached in order");
        rtsRequire(unitOf(id)->getOrderQueue().empty() && unitOf(id)->getLocation() == w3,
                   "The queue did not finish at the last waypoint");
        retire(id);
        SDL_Log("RTS_CONTROLS_QUEUE_PASS: three Shift-queued waypoints reached in order 1,2,3");
    }

    // ---- 4. Repeat production -------------------------------------------------------------
    Uint32 factoryID = NONE_ID, repeated = ItemID_Invalid;
    {
        auto rules = game->gameInitSettings.getGameOptions();
        rules.instantBuild = true;
        game->gameInitSettings.setGameOptions(rules);
        owner->placeStructure(NONE_ID, Structure_WindTrap, O.x + 20, O.y + 14, true, true);
        owner->placeStructure(NONE_ID, Structure_Radar, O.x + 23, O.y + 14, true, true);
        auto* factory = dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID, Structure_HeavyFactory, O.x + 26, O.y + 14, true, true));
        rtsRequire(factory != nullptr, "Heavy factory placement failed");
        factoryID = factory->getObjectID();
        factory->updateBuildList();
        for(const BuildItem& item : factory->getBuildList())
            if(repeated == ItemID_Invalid && factory->supportsRepeat(item.itemID)) repeated = item.itemID;
        rtsRequire(repeated != ItemID_Invalid, "The heavy factory offers nothing to repeat");
        owner->returnCredits(FixPoint(50000), FixPoint(50000));
        const int before = owner->getNumItems(repeated);
        Command(pid, CMD_BUILDER_SETREPEAT, factoryID, repeated, 1).executeCommand();
        int cycles = 0;
        for(; cycles < 6000 && owner->getNumItems(repeated) < before + 3; ++cycles) step(1);
        rtsRequire(owner->getNumItems(repeated) >= before + 3, "Repeat production did not build three units unattended");
        rtsRequire(factory->isRepeating(repeated) && factory->getProductionQueueSize() >= 1, "Repeat stopped by itself");

        owner->maxUnits = 1;   // the ground unit limit is now reached
        // A copy that was already paid for still rolls out (existing engine rule: the limit
        // stops production progress, not delivery); after that nothing more may appear.
        step(150);
        const int atLimit = owner->getNumItems(repeated);
        step(600);
        rtsRequire(owner->getNumItems(repeated) == atLimit, "Production ignored the unit limit");
        rtsRequire(factory->isRepeating(repeated) && factory->getProductionQueueSize() >= 1,
                   "The unit limit cancelled the repeated item instead of pausing it");
        owner->maxUnits = 0;
        int resume = 0;
        for(; resume < 3000 && owner->getNumItems(repeated) == atLimit; ++resume) step(1);
        rtsRequire(owner->getNumItems(repeated) > atLimit, "Repeat production did not resume after the limit freed up");
        rules.instantBuild = false;
        game->gameInitSettings.setGameOptions(rules);
        SDL_Log("RTS_CONTROLS_REPEAT_PASS: item %u built %d times unattended in %d cycles, paused at the unit limit, resumed after %d cycles",
                repeated, atLimit - before, cycles, resume);
    }

    // ---- 5. Save round trip: queue, patrol, HOLD and repeat flags --------------------------
    const Uint32 queued = spawn(owner, Unit_Tank, Coord(2, 2));
    const Uint32 patroller = spawn(owner, Unit_Tank, Coord(2, 9));
    const Uint32 holder = spawn(owner, Unit_Launcher, Coord(6, 3));
    Command(pid, CMD_UNIT_MOVE2POS, queued, O.x + 12, O.y + 2, 1).executeCommand();
    Command(pid, CMD_UNIT_QUEUEORDER, queued, static_cast<Uint32>(UnitOrderType::Move), O.x + 14, O.y + 8).executeCommand();
    Command(pid, CMD_UNIT_ATTACKMOVE, queued, O.x + 22, O.y + 3, 1).executeCommand();
    Command(pid, CMD_UNIT_PATROL, patroller, O.x + 12, O.y + 11, 0).executeCommand();
    Command(pid, CMD_UNIT_PATROL, patroller, O.x + 18, O.y + 8, 1).executeCommand();
    Command(pid, CMD_UNIT_SETMODE, holder, HOLD).executeCommand();
    step(20);
    const UnitOrderQueue savedQueue = unitOf(queued)->getOrderQueue();
    const UnitOrderQueue savedPatrol = unitOf(patroller)->getOrderQueue();
    rtsRequire(savedQueue.size() == 3 && savedPatrol.size() == 3 && savedPatrol.hasPatrol(), "Round-trip fixture orders missing");
    const std::string path = out + "/rts-controls-roundtrip.dls";
    rtsRequire(game->saveGame(path), "Could not save the round-trip fixture");
    game.reset(); currentGame = nullptr;
    game = rtsLoad(path);
    rtsRequire(unitOf(queued) && unitOf(queued)->getOrderQueue() == savedQueue, "The order queue changed across save and load");
    rtsRequire(unitOf(patroller) && unitOf(patroller)->getOrderQueue() == savedPatrol, "The patrol route changed across save and load");
    rtsRequire(unitOf(holder) && unitOf(holder)->getAttackMode() == HOLD, "HOLD was lost across save and load");
    auto* factory = dynamic_cast<BuilderBase*>(game->getObjectManager().getObject(factoryID));
    rtsRequire(factory && factory->isRepeating(repeated), "The repeat flag was lost across save and load");
    step(60);
    SDL_Log("RTS_CONTROLS_SAVE_PASS: queue, patrol route, HOLD and repeat flag restored");

    // ---- 7. Smoke frame: waypoint lines and a hotkey tooltip -------------------------------
    {
        game->unselectAll(game->selectedList);
        game->selectedList = {queued, patroller};
        for(Uint32 id : game->selectedList) unitOf(id)->setSelected(true);
        game->selectionChanged();
        screenborder->setNewScreenCenter(Coord((O.x + 12) * TILESIZE, (O.y + 6) * TILESIZE));
        game->pInterface->updateObjectInterface();
        auto* panel = dynamic_cast<MultiUnitInterface*>(game->pInterface->pObjectContainer);
        rtsRequire(panel != nullptr, "No group panel for the smoke frame");
        rtsRequire(panel->attackButton.getTooltipText().rfind("Attack Move (A)", 0) == 0
                   && panel->patrolButton.getTooltipText().rfind("Patrol (Q)", 0) == 0
                   && panel->holdButton.getTooltipText().rfind("Hold (H)", 0) == 0,
                   "Unit command tooltips do not name their hotkeys");
        auto capture = [&](const std::string& name, bool tooltip) {
            panel->patrolButton.bHover = tooltip;
            panel->patrolButton.tooltipLastMouseMotion = SDL_GetTicks() - 2000;
            drawnMouseX = getRendererWidth() - 180;
            drawnMouseY = getRendererHeight() / 2;
            game->drawScreen();
            int w, h; SDL_GetRendererOutputSize(renderer, &w, &h);
            sdl2::surface_ptr image{SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)};
            rtsRequire(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, image->pixels, image->pitch) == 0,
                       "Frame capture failed");
            // SavePNG writes the surface bytes as RGBA, so hand it that layout.
            sdl2::surface_ptr png{SDL_ConvertSurfaceFormat(image.get(), SDL_PIXELFORMAT_ABGR8888, 0)};
            rtsRequire(png != nullptr, "Frame conversion failed");
            SavePNG(png.get(), (out + "/" + name).c_str());
            SavePNG(png.get(), (smoke + "/" + name).c_str());
            SDL_RenderPresent(renderer);
            const auto* data = static_cast<const Uint32*>(image->pixels);
            return std::make_pair(std::vector<Uint32>(data, data + w*h), w);
        };
        const auto frame = capture("rts-controls-waypoints.png", false);
        const SDL_Rect field{0, game->topBarPos.h, game->sideBarPos.x, getRendererHeight() - game->topBarPos.h};
        rtsRequire(rtsHasColor(frame.first, frame.second, field, 90, 220, 90), "No green move waypoint line on screen");
        rtsRequire(rtsHasColor(frame.first, frame.second, field, 235, 70, 60), "No red attack-move waypoint line on screen");
        rtsRequire(rtsHasColor(frame.first, frame.second, field, 90, 150, 255), "No blue patrol waypoint line on screen");
        capture("rts-controls-tooltip.png", true);
        SDL_Log("RTS_CONTROLS_SMOKE_PASS: waypoint lines (move, attack-move, patrol) and Patrol (Q) tooltip captured in %s", smoke.c_str());
    }
    game.reset(); currentGame = nullptr;
    SDL_Log("RTS_CONTROLS_PROBE_PASS: attack-move, HOLD, Shift queue, repeat production, save round trip, 9854 load, smoke frame");
    return -1;
}
