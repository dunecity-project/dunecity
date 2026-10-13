// Headless integration fixture for formation movement (docs/formations.md); linked with the
// production game objects by run-formation-probe.py, never part of the game binary.
//
// Twelve mixed units are moved across open sand through the real player path
// (Game::handleSelectedObjectsActionClick -> commands -> executor -> game loop), once with
// "Move groups in formation" on and once off, from identical starting positions. A save
// taken mid-march is reloaded, and a 9854 unit record is loaded into the current build.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <ScreenBorder.h>
#include <FormationOrders.h>
#include <FileClasses/LoadSavePNG.h>
#include <misc/draw_util.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <units/Tank.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FormationProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FORMATION_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

struct Start { Uint32 item; Coord tile; };

/// Twelve units, scattered over more than the coherent radius on the west side.
const std::vector<Start> kArmy = {
    {Unit_Tank, Coord(5, 8)},      {Unit_Tank, Coord(8, 13)},    {Unit_Tank, Coord(4, 18)},
    {Unit_Tank, Coord(7, 22)},     {Unit_Launcher, Coord(3, 10)}, {Unit_Launcher, Coord(6, 16)},
    {Unit_Launcher, Coord(9, 19)}, {Unit_Launcher, Coord(5, 24)}, {Unit_SiegeTank, Coord(9, 9)},
    {Unit_SiegeTank, Coord(4, 14)}, {Unit_Quad, Coord(10, 12)},  {Unit_Quad, Coord(6, 20)},
};
const Coord kTarget(42, 16);
/// A member has arrived once it is this close to the clicked tile (the same test with and
/// without formations: a formation slot is within this of the target, and without one the
/// group piles up around it).
constexpr int kArrivalTiles = 4;
constexpr Uint32 kTimeoutCycles = 7000;

struct Outcome {
    std::map<Uint32, Uint32> arrival;   ///< cycles from the click
    std::map<Uint32, Coord> ordered;    ///< destination each unit was given
    std::map<Uint32, Coord> final;
    Uint32 spread = 0;
};

double seconds(Uint32 cycles) { return cycles * GAMESPEED_DEFAULT / 1000.0; }

std::vector<UnitBase*> deployArmy(House* owner) {
    std::vector<UnitBase*> army;
    for(const auto& start : kArmy) {
        UnitBase* unit = owner->createUnit(start.item, true);
        require(unit != nullptr, "Could not create a formation fixture unit");
        unit->deploy(start.tile);
        require(unit->getLocation() == start.tile, "A fixture unit did not stand on its start tile");
        unit->angle = RIGHT; unit->drawnAngle = RIGHT;
        army.push_back(unit);
    }
    return army;
}

void clearArmy(std::vector<UnitBase*>& army) {
    for(UnitBase* unit : army) unit->cancelDeployment();
    army.clear();
}

/// Selects the army and right-clicks the target through the real player path.
void order(Game& game, const std::vector<UnitBase*>& army, bool formations) {
    settings.general.formationMoves = formations;
    game.selectedList.clear();
    for(const UnitBase* unit : army) game.selectedList.insert(unit->getObjectID());
    require(game.handleSelectedObjectsActionClick(kTarget.x, kTarget.y), "The right click was not accepted");
}

void saveScreenshot(Game& game, const char* name) {
    screenborder->setNewScreenCenter(Coord(26 * TILESIZE, 16 * TILESIZE));
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    require(image != nullptr, "Could not read back the rendered map");
    SavePNG(image.get(), name);
}

Outcome march(Game& game, const std::vector<UnitBase*>& army, bool formations, const char* screenshot) {
    Outcome outcome;
    const Uint32 clicked = game.getGameCycleCount();
    order(game, army, formations);
    bool captured = false, shot = false;
    while(game.getGameCycleCount() - clicked < kTimeoutCycles) {
        game.updateGameState();
        const Uint32 now = game.getGameCycleCount() - clicked;
        if(!captured) {
            bool all = true;
            for(const UnitBase* unit : army) all = all && unit->getDestination() != unit->getLocation();
            if(all) {
                captured = true;
                for(const UnitBase* unit : army) outcome.ordered[unit->getObjectID()] = unit->getDestination();
            }
        }
        for(const UnitBase* unit : army)
            if(!outcome.arrival.count(unit->getObjectID())
               && FormationPlanner::tileDistance(unit->getLocation(), kTarget) <= kArrivalTiles)
                outcome.arrival[unit->getObjectID()] = now;
        if(screenshot && !shot && now >= MILLI2CYCLES(14000)) { saveScreenshot(game, screenshot); shot = true; }
        if(outcome.arrival.size() == army.size()) {
            // With formations, wait for every member to stand on its own slot: a member can
            // reach the area behind a row that arrived first and still has to walk round it.
            bool settled = true;
            for(const UnitBase* unit : army)
                settled = settled && !unit->isMoving()
                    && (!formations || unit->getLocation() == outcome.ordered.at(unit->getObjectID()));
            if(settled && now > outcome.arrival.rbegin()->second + 120) break;
        }
    }
    require(captured, "The order never reached the units");
    for(const UnitBase* unit : army) outcome.final[unit->getObjectID()] = unit->getLocation();
    Uint32 first = UINT32_MAX, last = 0;
    for(const auto& [id, cycle] : outcome.arrival) { first = std::min(first, cycle); last = std::max(last, cycle); }
    outcome.spread = outcome.arrival.empty() ? 0 : last - first;
    return outcome;
}

void report(const char* label, const Outcome& outcome, const std::vector<UnitBase*>& army) {
    SDL_Log("FORMATION_PROBE: %s arrived=%zu/%zu first=%.2fs last=%.2fs spread=%.2fs", label,
            outcome.arrival.size(), army.size(),
            outcome.arrival.empty() ? -1.0 : seconds(std::min_element(outcome.arrival.begin(), outcome.arrival.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; })->second),
            outcome.arrival.empty() ? -1.0 : seconds(std::max_element(outcome.arrival.begin(), outcome.arrival.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; })->second),
            seconds(outcome.spread));
    for(const UnitBase* unit : army) {
        const Uint32 id = unit->getObjectID();
        SDL_Log("FORMATION_PROBE: %s id=%u item=%u ordered=%d,%d final=%d,%d arrival=%.2fs", label, id,
                unit->getItemID(), outcome.ordered.at(id).x, outcome.ordered.at(id).y,
                outcome.final.at(id).x, outcome.final.at(id).y,
                outcome.arrival.count(id) ? seconds(outcome.arrival.at(id)) : -1.0);
    }
}

} // namespace FormationProbe

int runFormationProbe() {
    using namespace FormationProbe;
    settings.general.playerName = "Formation tester";
    settings.general.showMovementPaths = true;
    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("formation.ini", mapData, "Formations", true, settings.gameOptions);
    init.randomSeed = 4242;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Formation tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    game->initializeGameLoop();
    House* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && pLocalHouse == owner && pLocalPlayer != nullptr, "The local house is not the fixture house");

    // Open sand from the start area to beyond the target, with nothing standing on it.
    for(int y = 2; y <= 28; ++y)
        for(int x = 1; x <= 48; ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            require(!tile->hasAnObject(), "The fixture field is not empty at " + std::to_string(x) + "," + std::to_string(y));
            tile->setType(Terrain_Sand);
            tile->setRoad(false);
        }

    // 1. Formations on: the real right click issues one formation order per unit.
    auto army = deployArmy(owner);
    std::vector<const UnitBase*> constArmy(army.begin(), army.end());
    const auto expected = FormationOrders::planGroup(constArmy, kTarget);
    require(expected.formation && expected.slots.size() == army.size(), "The fixture army got no formation plan");
    const Outcome on = march(*game, army, true, "formation-march.png");
    report("on", on, army);
    for(const UnitBase* unit : army)
        require(on.ordered.at(unit->getObjectID()) == expected.slotFor(unit->getObjectID()),
                "A unit was not sent to the slot the planner chose");
    std::set<std::pair<int, int>> slots;
    for(const auto& [id, slot] : on.ordered) slots.insert({slot.x, slot.y});
    require(slots.size() == army.size(), "Two vehicles were given the same slot");
    // Tanks ahead of launchers (travel is +x), at their destinations.
    int rearmostTank = INT32_MAX, foremostLauncher = INT32_MIN;
    for(const UnitBase* unit : army) {
        const Coord at = on.final.at(unit->getObjectID());
        if(unit->getItemID() == Unit_Tank) rearmostTank = std::min(rearmostTank, at.x);
        if(unit->getItemID() == Unit_Launcher) foremostLauncher = std::max(foremostLauncher, at.x);
    }
    require(rearmostTank > foremostLauncher, "A launcher ended up level with or ahead of a tank");
    require(on.arrival.size() == army.size(), "Not every formation member arrived");
    for(const UnitBase* unit : army) require(!unit->isInFormation(), "A member kept its formation after arriving");
    clearArmy(army);

    // 2. Formations off: exactly the old orders - every unit a forced move to the clicked tile.
    army = deployArmy(owner);
    const Outcome off = march(*game, army, false, nullptr);
    report("off", off, army);
    for(const UnitBase* unit : army) {
        require(off.ordered.at(unit->getObjectID()) == kTarget, "Without formations a unit was not sent to the clicked tile");
        require(!unit->isInFormation(), "Without formations a unit joined one");
    }
    SDL_Log("FORMATION_PROBE_SPREAD: on=%.2fs off=%.2fs arrived_on=%zu arrived_off=%zu",
            seconds(on.spread), seconds(off.spread), on.arrival.size(), off.arrival.size());
    require(on.spread < off.spread, "Formations did not narrow the arrival spread");
    require(on.spread <= MILLI2CYCLES(20000), "The formation arrival spread exceeds 20 seconds");
    clearArmy(army);

    // 3. Mid-march save and reload: membership, slot and pace survive (SAVEGAMEVERSION 9857).
    army = deployArmy(owner);
    order(*game, army, true);
    for(int i = 0; i < 300; ++i) game->updateGameState();
    struct Saved { Uint32 group; Coord slot; FixPoint cap; };
    std::map<Uint32, Saved> before;
    for(const UnitBase* unit : army) {
        require(unit->isInFormation(), "A member left its formation mid-march");
        before[unit->getObjectID()] = {unit->getFormationGroup(), unit->getFormationSlot(), unit->getFormationSpeedCap()};
    }
    const std::string savePath = "formation-midmarch.dls";
    require(game->saveGame(savePath), "Could not save the mid-march game");
    army.clear();
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(savePath));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    require(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The save was not written at the current version");
    for(const auto& [id, saved] : before) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit != nullptr, "A member disappeared across the reload");
        require(unit->getFormationGroup() == saved.group && unit->getFormationSlot() == saved.slot
                && unit->getFormationSpeedCap() == saved.cap, "Formation state changed across the reload");
    }
    Uint32 resumed = 0;
    std::map<Uint32, bool> reached;
    while(resumed < kTimeoutCycles && reached.size() < before.size()) {
        game->updateGameState();
        ++resumed;
        for(const auto& [id, saved] : before) {
            const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
            if(unit && unit->getLocation() == saved.slot && !unit->isMoving()) reached[id] = true;
        }
    }
    require(reached.size() == before.size(), "The reloaded formation did not finish its march");
    SDL_Log("FORMATION_PROBE_SAVE: %zu members restored with group, slot and pace; all reached their slots %.2fs after the reload",
            before.size(), seconds(resumed));

    // 4. A 9854 unit record (no formation fields) loads into this build with no membership.
    {
        Tank* tank = nullptr;
        for(UnitBase* unit : unitList) if(unit->getItemID() == Unit_Tank) { tank = static_cast<Tank*>(unit); break; }
        require(tank != nullptr, "No tank for the old-record fixture");
        // The 9855 order-queue block sits right before the formation record; an empty queue
        // writes a fixed 11 bytes (count, dispatched flag, walk retries, break-off point).
        tank->clearOrderQueue();
        constexpr size_t kEmptyOrderQueueBlock = 1 + 1 + 1 + 4 + 4;
        FormationOrders::join({tank, tank}, FormationPlanner::makeGroupID(1, 0x123456), {Coord(1, 2), Coord(1, 2)});
        OMemoryStream withFormation; withFormation.open(); tank->save(withFormation);
        const std::string current(withFormation.getData(), withFormation.getDataLength());
        tank->leaveFormation();
        OMemoryStream without; without.open(); tank->save(without);
        const std::string plain(without.getData(), without.getDataLength());
        size_t first = 0;
        while(first < current.size() && current[first] == plain[first]) ++first;
        size_t last = current.size();
        while(last > first && current[last - 1] == plain[last - 1]) --last;
        require(last - first <= 20 && first + 20 <= current.size() && first >= kEmptyOrderQueueBlock,
                "The formation record is not where expected");
        // Remove the whole 20-byte record that starts at the group id, and the order queue before it.
        const std::string old9854 = current.substr(0, first - kEmptyOrderQueueBlock) + current.substr(first + 20);
        game->loadedSavegameVersion = 9854;
        {
            IMemoryStream in(old9854.data(), old9854.size());
            Tank restored(in);
            require(in.getRemainingLength() == 0, "A 9854 unit record was not read exactly");
            require(!restored.isInFormation() && restored.getFormationSlot().isInvalid()
                    && restored.getFormationSpeedCap() == 0, "A 9854 unit loaded with formation state");
            require(restored.getLocation() == tank->getLocation(), "A 9854 unit record lost its position");
            restored.setHealth(0);
            unitList.remove(&restored);
        }
        game->loadedSavegameVersion = SAVEGAMEVERSION;
    }
    SDL_Log("FORMATION_PROBE_PASS: on spread %.2fs, off spread %.2fs, tanks ahead of launchers, save round trip, 9854 record",
            seconds(on.spread), seconds(off.spread));
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    return -1;
}
