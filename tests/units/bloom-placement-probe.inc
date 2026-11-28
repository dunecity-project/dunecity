// Units a scenario starts with on a spice or special bloom must load alive, visible to every
// team and targetable, with the bloom still intact. A unit that arrives on a bloom later must
// still set it off and die. Before the fix the loader's [UNITS] health revived a unit the bloom
// had just killed, leaving it alive but invisible to everyone (reworked Atreides mission 13).
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <mod/ModManager.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace BloomPlacementProbe {

struct StartUnit {
    const char* name;
    int x, y, house, itemID, iniHealth;
    bool specialBloom;
};

// tests/units/run-bloom-placement-probe.py adds these [UNITS] lines to the fixture map.
constexpr StartUnit startUnits[] = {
    {"enemy tank on spice bloom", 4, 6, HOUSE_HARKONNEN, Unit_Tank, 128, false},
    {"own trike on spice bloom", 29, 6, HOUSE_ATREIDES, Unit_Trike, 256, false},
    {"enemy quad on special bloom", 46, 24, HOUSE_HARKONNEN, Unit_Quad, 192, true},
};
constexpr int arrivalX = 21, arrivalY = 24;   // a spice bloom left free for the arrival check

inline void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error("BLOOM_PLACEMENT_PROBE_FAIL: " + why);
}

inline UnitBase* unitAt(int x, int y, int itemID) {
    for(auto* unit : unitList)
        if(unit->getItemID() == itemID && unit->getLocation() == Coord(x, y)) return unit;
    return nullptr;
}

/// No unit is ever active and alive yet hidden from a team: that unit could never be targeted.
inline void requireNoGhosts(Game& game, const std::string& when) {
    for(const auto* unit : unitList) {
        if(isSandwormUnit(unit->getItemID()) || !unit->isActive() || unit->getHealth() <= 0) continue;
        for(int team : {1, 2})
            require(unit->isVisible(team), when + ": object " + std::to_string(unit->getObjectID())
                    + " item " + std::to_string(unit->getItemID()) + " is alive but invisible to team "
                    + std::to_string(team));
    }
}

} // namespace BloomPlacementProbe

int runBloomPlacementProbe() {
    using namespace BloomPlacementProbe;
    const std::string mod = std::getenv("BLOOM_PLACEMENT_PROBE_MOD");
    require(ModManager::instance().setActiveMod(mod), "missing fixture mod " + mod);
    settings.general.playerName = "Bloom tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    require(!mapData.empty(), "fixture map missing");
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("bloom-placement.ini", mapData, "Bloom placement regression", true, settings.gameOptions);
    init.randomSeed = 67;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Bloom tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    require(pLocalHouse == game->getHouse(HOUSE_ATREIDES), "local house is not the Atreides tester");

    Uint32 startIDs[std::size(startUnits)];
    for(size_t i = 0; i < std::size(startUnits); ++i) {
        const auto& start = startUnits[i];
        auto* unit = unitAt(start.x, start.y, start.itemID);
        require(unit != nullptr, std::string(start.name) + ": not on its tile after loading");
        require(unit->getOwner()->getHouseID() == start.house, std::string(start.name) + ": wrong owner");
        const FixPoint expected = unit->getMaxHealth() * std::min(FixPoint(start.iniHealth) / 256, FixPoint(1));
        require(unit->isActive() && unit->getHealth() == expected,
                std::string(start.name) + ": health " + std::to_string(unit->getHealth().lround())
                + " instead of its [UNITS] health " + std::to_string(expected.lround()));
        for(int team : {1, 2})
            require(unit->isVisible(team), std::string(start.name) + ": invisible to team " + std::to_string(team));
        const Tile* tile = currentGameMap->getTile(start.x, start.y);
        require(start.specialBloom ? tile->isSpecialBloom() : tile->isSpiceBloom(),
                std::string(start.name) + ": the bloom under it went off at load");
        startIDs[i] = unit->getObjectID();
        SDL_Log("BLOOM_PLACEMENT_START: mod=%s unit=\"%s\" object=%u health=%d visible=1 bloom=intact",
                mod.c_str(), start.name, startIDs[i], unit->getHealth().lround());
    }
    requireNoGhosts(*game, "after loading");

    game->initializeGameLoop();
    for(int cycle = 0; cycle < 120; ++cycle) {
        game->updateGameState();
        requireNoGhosts(*game, "cycle " + std::to_string(game->getGameCycleCount()));
    }
    for(size_t i = 0; i < std::size(startUnits); ++i) {
        const auto* object = game->getObjectManager().getObject(startIDs[i]);
        require(object != nullptr && object->getHealth() > 0,
                std::string(startUnits[i].name) + ": died without being attacked");
        for(int team : {1, 2})
            require(object->isVisible(team), std::string(startUnits[i].name) + ": became invisible");
    }

    // A unit that arrives on a bloom still sets it off and is destroyed, never kept as a ghost.
    require(currentGameMap->getTile(arrivalX, arrivalY)->isSpiceBloom(), "arrival bloom missing");
    auto* arrival = game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Quad, false);
    require(arrival != nullptr, "cannot create the arriving quad");
    const Uint32 arrivalID = arrival->getObjectID();
    arrival->deploy(Coord(arrivalX, arrivalY));
    require(!currentGameMap->getTile(arrivalX, arrivalY)->isSpiceBloom(), "the arriving quad did not set off the bloom");
    require(arrival->getHealth() == 0, "the arriving quad survived the bloom");
    int cycles = 0;
    for(; cycles < 10 && game->getObjectManager().getObject(arrivalID) != nullptr; ++cycles) {
        game->updateGameState();
        requireNoGhosts(*game, "after the arrival");
    }
    require(game->getObjectManager().getObject(arrivalID) == nullptr, "the bloom's victim was never removed");

    game.reset();
    currentGame = nullptr;
    SDL_Log("BLOOM_PLACEMENT_PROBE_PASS: mod=%s arrival_removed_after=%d", mod.c_str(), cycles);
    return -1;
}
