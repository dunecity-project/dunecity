// Headless measurement fixture (tests/units/run-movement-effects.py); linked with the production game
// objects, never part of the shipped game. Measures what the movement step code does, so two builds
// can be compared (HANDOVER "Unit movement: chained steps"):
//   speed:    cycles per route for each ground unit type, 12 tiles straight and diagonal, over sand and
//             rock, 16 legs per unit from different route phases (movement-speed.csv);
//   crush:    infantry crushed per game minute by tracked vehicles shuttling straight and diagonally
//             over rock and sand through enemy infantry crossing their lanes (movement-crush.csv). Every
//             unit has 100,000 hit points, so no weapon kills anyone and every death is a crush;
//   carryall: a unit that waits 400 cycles for a carryall that never comes, then drives on: how fast it
//             moves afterwards (movement-carryall.csv, one row per cycle of the 300 after the wait).
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace MovementEffects {

inline void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }

inline std::unique_ptr<Game> makeGame(int terrain, bool enemies) {
    std::ifstream mapFile(PROBE_MAP_PATH);
    std::string mapData;
    for (std::string line; std::getline(mapFile, line);)
        if (line.find(",Sandworm,") == std::string::npos) mapData += line + '\n';
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("movement.ini", mapData, true, settings.gameOptions);
    init.randomSeed = 93;
    GameInitSettings::HouseInfo atreides(HOUSE_ATREIDES, 1);
    atreides.addPlayerInfo({"Movement tester", "HumanPlayer"});
    init.addHouseInfo(atreides);
    GameInitSettings::HouseInfo harkonnen(HOUSE_HARKONNEN, enemies ? 2 : 1);
    harkonnen.addPlayerInfo({"Other", "HumanPlayer"});
    init.addHouseInfo(harkonnen);
    game->initGame(init);
    for (int y = 0; y < currentGameMap->getSizeY(); ++y)
        for (int x = 0; x < currentGameMap->getSizeX(); ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            tile->setType(terrain);
            tile->setRoad(false);
        }
    game->initializeGameLoop();
    return game;
}

/// A unit going back and forth between two tiles; the next leg is ordered a varying number of cycles
/// after the last arrival, so the legs start from every route phase.
struct Shuttle {
    Uint32 id = NONE_ID;
    int item = 0;
    bool diagonal = false;
    Coord a, b;
    int legs = 0;
    Uint32 ordered = 0, orderAt = 0;
    bool travelling = false;
};

inline UnitBase* unitOf(Game& game, Uint32 id) { return dynamic_cast<UnitBase*>(game.getObjectManager().getObject(id)); }

/// Advances every shuttle by one cycle's bookkeeping; returns the legs completed this cycle as (shuttle, cycles).
inline std::vector<std::pair<Shuttle*, Uint32>> drive(Game& game, std::vector<Shuttle>& shuttles) {
    std::vector<std::pair<Shuttle*, Uint32>> done;
    const Uint32 now = game.getGameCycleCount();
    for (Shuttle& s : shuttles) {
        UnitBase* u = unitOf(game, s.id);
        if (u == nullptr) continue;
        if (s.travelling) {
            if (!u->moving && u->location == u->destination) {
                done.emplace_back(&s, now - s.ordered);
                s.travelling = false;
                ++s.legs;
                s.orderAt = now + 2 + static_cast<Uint32>(s.legs * 5 % 16);
            }
        } else if (now >= s.orderAt) {
            const Coord to = u->location == s.a ? s.b : s.a;
            u->doMove2Pos(to.x, to.y, true);
            s.ordered = now;
            s.travelling = true;
        }
    }
    return done;
}

inline Shuttle place(House* house, int item, Coord a, Coord b, Uint32 firstOrder) {
    auto* unit = house->createUnit(item, true);
    require(unit != nullptr, "Could not create unit " + std::to_string(item));
    unit->deploy(a);
    require(unit->location == a, "Unit " + std::to_string(item) + " was not deployed on its lane");
    Shuttle s;
    s.id = unit->getObjectID(); s.item = item; s.a = a; s.b = b; s.diagonal = a.x != b.x && a.y != b.y;
    s.orderAt = firstOrder;
    return s;
}

inline const char* terrainName(int terrain) { return terrain == Terrain_Rock ? "rock" : "sand"; }

inline void speed(const std::string& out) {
    std::ofstream csv(out + "/movement-speed.csv");
    csv << "terrain,diagonal,item,leg,cycles\n";
    const int items[] = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Devastator, Unit_SonicTank, Unit_Deviator,
                         Unit_Quad, Unit_Trike, Unit_RaiderTrike, Unit_Harvester, Unit_MCV, Unit_Soldier, Unit_Trooper,
                         Unit_Saboteur};
    constexpr int kLegs = 16;
    for (int terrain : {Terrain_Sand, Terrain_Rock})
        for (bool diagonal : {false, true}) {
            auto game = makeGame(terrain, false);
            House* house = game->getHouse(HOUSE_ATREIDES);
            std::vector<Shuttle> shuttles;
            for (int k = 0; k < static_cast<int>(std::size(items)); ++k) {
                Coord a, b;
                if (!diagonal) { a = Coord(2, 1 + 2 * k); b = Coord(14, 1 + 2 * k); }
                else { a = Coord(2 + 3 * (k % 7), k < 7 ? 1 : 16); b = a + Coord(12, 12); }
                shuttles.push_back(place(house, items[k], a, b, game->getGameCycleCount() + 1 + k));
            }
            while (game->getGameCycleCount() < 200000) {
                game->updateGameState();
                for (const auto& [s, cycles] : drive(*game, shuttles))
                    csv << terrainName(terrain) << ',' << int(diagonal) << ',' << s->item << ',' << s->legs << ',' << cycles << '\n';
                if (std::all_of(shuttles.begin(), shuttles.end(), [](const Shuttle& s) { return s.legs >= kLegs; })) break;
            }
            for (const Shuttle& s : shuttles)
                require(s.legs >= kLegs, "Speed: unit " + std::to_string(s.item) + " did not finish its legs");
            game.reset();
            currentGame = nullptr;
        }
}

inline void crush(const std::string& out) {
    std::ofstream csv(out + "/movement-crush.csv");
    csv << "terrain,minutes,crusher_item,lane,kills\n";
    const Uint32 kMinutes = 10;
    const int crushers[] = {Unit_Tank, Unit_SiegeTank, Unit_Devastator, Unit_Harvester, Unit_Launcher, Unit_SonicTank,
                            Unit_Deviator};
    for (int terrain : {Terrain_Rock, Terrain_Sand}) {
        auto game = makeGame(terrain, true);
        for (int item = 0; item < Num_ItemID; ++item)
            for (int h = 0; h < NUM_HOUSES; ++h) game->objectData.data[item][h].hitpoints = 100000;
        House* atreides = game->getHouse(HOUSE_ATREIDES);
        House* harkonnen = game->getHouse(HOUSE_HARKONNEN);
        const Uint32 start = game->getGameCycleCount();
        std::vector<Shuttle> vehicles, infantry;
        // Straight lanes on the left (rows 3, 7, ... 27, x 2-22), diagonals on the right.
        for (int k = 0; k < 7; ++k)
            vehicles.push_back(place(atreides, crushers[k], Coord(2, 3 + 4 * k), Coord(22, 3 + 4 * k), start + 1 + k));
        for (int k = 0; k < 4; ++k)
            vehicles.push_back(place(atreides, crushers[k * 2 % 7], Coord(26, 1 + 4 * k), Coord(38, 13 + 4 * k), start + 1 + k));
        // Infantry crossing them: columns on the left, rows on the right.
        std::vector<std::pair<Coord, Coord>> walks;
        for (int x = 4; x <= 21; x += 3) walks.emplace_back(Coord(x, 1), Coord(x, 29));
        for (int y = 4; y <= 28; y += 3) walks.emplace_back(Coord(25, y), Coord(49, y));
        auto spawn = [&](size_t walk) {
            const int item = walk % 2 ? Unit_Trooper : Unit_Soldier;
            return place(harkonnen, item, walks[walk].first, walks[walk].second, game->getGameCycleCount() + 1 + walk % 7);
        };
        std::vector<size_t> walkOf;
        for (size_t w = 0; w < walks.size(); ++w) { infantry.push_back(spawn(w)); walkOf.push_back(w); }
        std::map<std::pair<int, int>, int> kills;   // (crusher item, diagonal lane) -> kills
        std::map<Uint32, Uint32> respawnAt;           // walk -> cycle
        std::unordered_map<Uint32, Coord> seen;
        const Uint32 end = start + kMinutes * MILLI2CYCLES(60000);
        while (game->getGameCycleCount() < end) {
            game->updateGameState();
            drive(*game, vehicles);
            for (size_t i = 0; i < infantry.size(); ++i) {
                Shuttle& s = infantry[i];
                UnitBase* u = unitOf(*game, s.id);
                if (u == nullptr) {
                    if (s.id == NONE_ID) continue;
                    // The vehicle that crushed it stands on its last tile or, if it stepped this cycle, next to it.
                    const Coord at = seen[s.id];
                    const UnitBase* crusher = nullptr;
                    for (int dy = -1; dy <= 1 && crusher == nullptr; ++dy)
                        for (int dx = -1; dx <= 1 && crusher == nullptr; ++dx) {
                            const Coord c = at + Coord(dx, dy);
                            if (!currentGameMap->tileExists(c)) continue;
                            const auto* v = dynamic_cast<const UnitBase*>(currentGameMap->getTile(c)->getNonInfantryGroundObject());
                            if (v != nullptr && v->isTracked() && v->getLocation() == c) crusher = v;
                        }
                    require(crusher != nullptr, "Crush: an infantry unit died with no vehicle on or next to its tile");
                    const bool diagonalLane = crusher->getLocation().x >= 24;
                    ++kills[{static_cast<int>(crusher->getItemID()), int(diagonalLane)}];
                    respawnAt[walkOf[i]] = game->getGameCycleCount() + 30;
                    s.id = NONE_ID;
                    continue;
                }
                seen[s.id] = u->getLocation();
            }
            for (auto it = respawnAt.begin(); it != respawnAt.end();) {
                const size_t w = it->first;
                const Tile* home = currentGameMap->getTile(walks[w].first);
                if (game->getGameCycleCount() >= it->second && !home->hasAGroundObject()) {
                    for (size_t i = 0; i < infantry.size(); ++i)
                        if (walkOf[i] == w) infantry[i] = spawn(w);
                    it = respawnAt.erase(it);
                } else ++it;
            }
            drive(*game, infantry);
        }
        int total = 0;
        for (const auto& [key, n] : kills) {
            csv << terrainName(terrain) << ',' << kMinutes << ',' << key.first << ',' << (key.second ? "diagonal" : "straight")
                << ',' << n << '\n';
            total += n;
        }
        SDL_Log("MOVEMENT_EFFECTS_CRUSH: terrain=%s minutes=%u kills=%d per_minute=%.2f", terrainName(terrain), kMinutes,
                total, total / static_cast<double>(kMinutes));
        game.reset();
        currentGame = nullptr;
    }
}

inline void carryall(const std::string& out) {
    std::ofstream csv(out + "/movement-carryall.csv");
    csv << "item,terrain,after,x,y,moved,step_speed,tiles\n";
    for (int terrain : {Terrain_Sand, Terrain_Rock}) {
        auto game = makeGame(terrain, false);
        House* house = game->getHouse(HOUSE_ATREIDES);
        auto* carrier = house->createUnit(Unit_Carryall, true);   // booked, never deployed: it never comes
        require(carrier != nullptr, "Could not create the carryall");
        const int items[] = {Unit_Tank, Unit_Harvester, Unit_Soldier, Unit_Quad};
        std::vector<Shuttle> shuttles;
        for (int k = 0; k < static_cast<int>(std::size(items)); ++k)
            shuttles.push_back(place(house, items[k], Coord(2, 2 + 6 * k), Coord(40, 2 + 6 * k), game->getGameCycleCount() + 1));
        constexpr Uint32 kWaitStart = 60, kWait = 400, kAfter = 300;
        const Uint32 start = game->getGameCycleCount();
        std::map<Uint32, std::pair<double, double>> last;
        std::map<Uint32, Coord> from;
        while (game->getGameCycleCount() < start + kWaitStart + kWait + kAfter) {
            const Uint32 rel = game->getGameCycleCount() - start;
            for (Shuttle& s : shuttles) {
                auto* u = dynamic_cast<GroundUnit*>(unitOf(*game, s.id));
                if (rel == kWaitStart) { u->awaitingPickup = true; u->bookedCarrier = carrier->getObjectID(); }
                if (rel == kWaitStart + kWait) { u->cancelCarryallPickup(); from[s.id] = u->location; }
                if (rel > kWaitStart + kWait) {
                    const double x = u->realX.toDouble(), y = u->realY.toDouble();
                    const double moved = std::hypot(x - last[s.id].first, y - last[s.id].second);
                    double stepSpeed = 0;
                    if (u->moving && u->dynastyStepEnd > u->dynastyStepStart)
                        stepSpeed = std::hypot(u->dynastyEndpoint.x - u->dynastyStartX.toDouble(), u->dynastyEndpoint.y - u->dynastyStartY.toDouble())
                                    * (GAMESPEED_DEFAULT * 3) / static_cast<double>(u->dynastyStepEnd - u->dynastyStepStart);
                    csv << s.item << ',' << terrainName(terrain) << ',' << rel - kWaitStart - kWait << ',' << x << ',' << y << ','
                        << moved << ',' << stepSpeed << ',' << std::abs(u->location.x - from[s.id].x) << '\n';
                }
                last[s.id] = {u->realX.toDouble(), u->realY.toDouble()};
            }
            game->updateGameState();
            // Only the first leg: the order is given once and never repeated.
            for (Shuttle& s : shuttles) if (!s.travelling && game->getGameCycleCount() >= s.orderAt && s.legs == 0) {
                unitOf(*game, s.id)->doMove2Pos(s.b.x, s.b.y, true); s.travelling = true;
            }
        }
        game.reset();
        currentGame = nullptr;
    }
}

} // namespace MovementEffects

int runMovementEffects() {
    using namespace MovementEffects;
    const std::string out = std::getenv("MOVEMENT_EFFECTS_OUT");
    const std::string phase = std::getenv("MOVEMENT_EFFECTS_PHASE");
    require(ModManager::instance().setActiveMod("dunecity"), "Missing dunecity mod");
    settings.general.playerName = "Movement tester";
    if (phase == "speed") speed(out);
    else if (phase == "crush") crush(out);
    else if (phase == "carryall") carryall(out);
    else throw std::runtime_error("Unknown phase " + phase);
    SDL_Log("MOVEMENT_EFFECTS_DONE: phase=%s", phase.c_str());
    return -1;
}
