// Formation micro-benchmark (docs/formations.md, "Scenario tests"); linked with the production
// game objects by run-formation-bench.py, never part of the game binary.
//
// Two fixed armies on a bench map (open sand, a canyon, obstacles), no bases, no AI, no
// production. Army A (Atreides, the local player) gets a real player attack-move
// (Game::issuePositionOrder) towards army B in one of the five player shapes or with formations
// off; army B (Ordos) area-guards where it stands, or attack-moves towards A at the same moment
// (no formation). Every trial starts from freshly deployed armies at seeded, jittered positions
// that are the same for every shape (paired comparison). Per trial it logs one FORMATION_BENCH
// line: values, losses, survivors, first contact, time to resolve, the members that fired within
// 10 s of A's first shot, A's crossing spread over the bench line (the canyon's mouth), the
// seconds A's members stood blocked on the way, and for the air bench the ornithopters shot
// down and which anti-air members fired at them.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <FormationOrders.h>
#include <units/UnitBase.h>
#include <units/UnitOrderQueue.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace FormationBench {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FORMATION_BENCH_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

std::string env(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream input(text);
    std::string item;
    while(std::getline(input, item, ',')) if(!item.empty()) out.push_back(item);
    return out;
}

struct Composition { std::vector<Uint32> a, b; bool air = false; };

/// The armies (unit counts chosen for near-equal value with vanilla prices; the log carries
/// the exact values).
Composition composition(const std::string& name) {
    auto many = [](Uint32 item, int count) { return std::vector<Uint32>(count, item); };
    auto join = [](std::initializer_list<std::vector<Uint32>> parts) {
        std::vector<Uint32> all;
        for(const auto& part : parts) all.insert(all.end(), part.begin(), part.end());
        return all;
    };
    const auto mixed = join({many(Unit_Tank, 5), many(Unit_Launcher, 3), many(Unit_SiegeTank, 2)});
    if(name == "tanks") return {many(Unit_Tank, 10), many(Unit_Tank, 10)};
    if(name == "mixed") return {mixed, mixed};
    if(name == "sonic") return {join({many(Unit_SonicTank, 3), many(Unit_Tank, 4), many(Unit_Launcher, 2)}), mixed};
    if(name == "light") return {join({many(Unit_Quad, 8), many(Unit_Trike, 5)}), many(Unit_Tank, 8)};
    if(name == "heavy") return {many(Unit_Tank, 8), join({many(Unit_Quad, 8), many(Unit_Trike, 5)})};
    if(name == "big") return {join({many(Unit_Tank, 12), many(Unit_Launcher, 6), many(Unit_SiegeTank, 4)}),
                              join({many(Unit_Tank, 12), many(Unit_Launcher, 6), many(Unit_SiegeTank, 4)})};
    // Air: a marching column with anti-air (launchers, rocket troopers) against hunting 'thopters.
    if(name == "air") return {join({many(Unit_Tank, 4), many(Unit_Launcher, 3), many(Unit_Trooper, 4)}),
                              many(Unit_Ornithopter, 3), true};
    if(name == "air-tanks") return {many(Unit_Tank, 8), many(Unit_Ornithopter, 3), true};
    throw std::runtime_error("Unknown bench composition: " + name);
}

/// -1 = formations off (plain attack-move), -2 = engine Hunt (how AI waves without a shape
/// move: each unit at its own speed to the nearest enemy), else the FormationPlanner shape id.
int shapeId(const std::string& name) {
    if(name == "off") return -1;
    if(name == "hunt") return -2;
    if(name == "box") return int(FormationPlanner::Shape::Box);
    if(name == "pincer") return int(FormationPlanner::Shape::Pincer);
    if(name == "column") return int(FormationPlanner::Shape::Column);
    if(name == "staggered") return int(FormationPlanner::Shape::Staggered);
    if(name == "echelon") return int(FormationPlanner::Shape::Echelon);
    throw std::runtime_error("Unknown bench shape: " + name);
}

double seconds(Uint32 cycles) { return cycles * GAMESPEED_DEFAULT / 1000.0; }

int priceOf(const UnitBase* unit) {
    return std::max(0, currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
}

const UnitBase* live(Game& game, Uint32 id) {
    const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
    return (unit != nullptr && unit->getHealth() > 0) ? unit : nullptr;
}

void clearAll() {
    std::vector<UnitBase*> units;
    for(UnitBase* unit : unitList) if(unit->isActive()) units.push_back(unit);
    for(UnitBase* unit : units) unit->cancelDeployment();
}

/// Free, standable tiles of the deployment box in a seeded order.
std::vector<Coord> spots(Coord first, Coord last, std::mt19937& rng, bool air) {
    std::vector<Coord> tiles;
    for(int y = first.y; y <= last.y; ++y)
        for(int x = first.x; x <= last.x; ++x) {
            const Tile* tile = currentGameMap->getTile(x, y);
            if(tile->isMountain() || tile->hasAGroundObject() || (air && tile->hasAnAirUnit())) continue;
            tiles.emplace_back(x, y);
        }
    std::shuffle(tiles.begin(), tiles.end(), rng);
    return tiles;
}

std::vector<Uint32> deploy(House* owner, const std::vector<Uint32>& items, Coord first, Coord last, std::mt19937& rng,
                           int facing) {
    std::vector<Uint32> ids;
    bool air = false;
    for(Uint32 item : items) air = air || item == Unit_Ornithopter;
    auto free = spots(first, last, rng, air);
    require(free.size() >= items.size(), "Not enough free tiles to deploy a bench army");
    for(size_t i = 0; i < items.size(); ++i) {
        UnitBase* unit = owner->createUnit(items[i], true);
        require(unit != nullptr, "Could not create a bench unit");
        unit->deploy(free[i]);
        unit->angle = facing; unit->drawnAngle = facing;
        unit->doSetAttackMode(AREAGUARD);
        ids.push_back(unit->getObjectID());
    }
    return ids;
}

Coord centroid(Game& game, const std::vector<Uint32>& ids) {
    int x = 0, y = 0, n = 0;
    for(Uint32 id : ids) if(const auto* unit = live(game, id)) { x += unit->getLocation().x; y += unit->getLocation().y; ++n; }
    return n ? Coord(x / n, y / n) : Coord::Invalid();
}

struct Side {
    std::vector<Uint32> ids;
    std::map<Uint32, int> value;
    std::map<Uint32, Sint32> timer;     ///< last primary weapon timer
    std::map<Uint32, Uint32> fired;     ///< first cycle it fired
    std::set<Uint32> firedAtAir;        ///< members that fired with a flying target
    int start = 0;
    int lost = 0;
    int alive(Game& game) const { int n = 0; for(Uint32 id : ids) n += live(game, id) ? 1 : 0; return n; }
    int aliveValue(Game& game) const { int v = 0; for(Uint32 id : ids) if(live(game, id)) v += value.at(id); return v; }
};

void track(Game& game, Side& side, Uint32 now) {
    for(Uint32 id : side.ids) {
        const auto* unit = live(game, id);
        if(!unit) continue;
        const Sint32 previous = side.timer.count(id) ? side.timer[id] : unit->primaryWeaponTimer;
        if(unit->primaryWeaponTimer > previous) {
            if(!side.fired.count(id)) side.fired[id] = now;
            const auto* target = unit->getTarget();
            if(target && target->isAFlyingUnit()) side.firedAtAir.insert(id);
        }
        side.timer[id] = unit->primaryWeaponTimer;
    }
}

int run() {
    settings.general.playerName = "Formation bench";
    const std::string mapPath = env("BENCH_MAP", "");
    std::ifstream mapFile(mapPath);
    require(bool(mapFile), "Cannot read the bench map " + mapPath);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    const std::string terrain = env("BENCH_TERRAIN", "open");
    const int trials = std::atoi(env("BENCH_TRIALS", "8").c_str());
    const auto comps = split(env("BENCH_COMPS", "tanks"));
    const auto shapes = split(env("BENCH_SHAPES", "off,box,pincer,column,staggered,echelon"));
    const auto modes = split(env("BENCH_MODES", "hold"));
    const int lineX = std::atoi(env("BENCH_LINE_X", "0").c_str());
    const Uint32 limit = MILLI2CYCLES(std::atoi(env("BENCH_LIMIT_S", "300").c_str()) * 1000);
    // Diagnostics: every BENCH_TRACE_S game seconds, one FORMATION_BENCH_TRACE line per member of A.
    const Uint32 traceEvery = MILLI2CYCLES(std::atoi(env("BENCH_TRACE_S", "0").c_str()) * 1000);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("bench.ini", mapData, "Formation bench", true, settings.gameOptions);
    init.randomSeed = 20261017;
    GameInitSettings::HouseInfo a(HOUSE_ATREIDES, 1);
    a.addPlayerInfo({"Formation bench", "HumanPlayer"});
    init.addHouseInfo(a);
    GameInitSettings::HouseInfo b(HOUSE_ORDOS, 2);
    b.addPlayerInfo({"Opponent", "HumanPlayer"});
    init.addHouseInfo(b);
    game->initGame(init);
    game->initializeGameLoop();
    House* houseA = game->getHouse(HOUSE_ATREIDES);
    House* houseB = game->getHouse(HOUSE_ORDOS);
    require(houseA && houseB && pLocalHouse == houseA, "The bench houses are not set up");
    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    const int midY = h / 2;

    for(size_t c = 0; c < comps.size(); ++c) {
        const Composition comp = composition(comps[c]);
        for(const std::string& mode : modes) {
            for(int trial = 0; trial < trials; ++trial) {
                for(const std::string& shape : shapes) {
                    clearAll();
                    for(int i = 0; i < 30; ++i) game->updateGameState();
                    // The same positions for every shape of this trial.
                    std::mt19937 rng(static_cast<Uint32>(7919 * (trial + 1) + 104729 * (c + 1)));
                    Side sa, sb;
                    sa.ids = deploy(houseA, comp.a, Coord(4, midY - 9), Coord(18, midY + 9), rng, RIGHT);
                    if(comp.air) sb.ids = deploy(houseB, comp.b, Coord(w - 6, midY - 12), Coord(w - 2, midY + 12), rng, LEFT);
                    else sb.ids = deploy(houseB, comp.b, Coord(w - 22, midY - 7), Coord(w - 10, midY + 7), rng, LEFT);
                    for(auto* side : {&sa, &sb})
                        for(Uint32 id : side->ids) {
                            side->value[id] = priceOf(live(*game, id));
                            side->start += side->value[id];
                        }
                    for(int i = 0; i < 5; ++i) game->updateGameState();
                    // A's attack-move: through B's position (air: across the map to the far side).
                    const Coord goal = comp.air ? Coord(w - 8, midY) : centroid(*game, sb.ids);
                    std::vector<UnitBase*> group;
                    for(Uint32 id : sa.ids) group.push_back(const_cast<UnitBase*>(live(*game, id)));
                    const int sid = shapeId(shape);
                    settings.general.formationMoves = sid >= 0;
                    settings.general.formationShape = sid >= 0 ? sid : 0;
                    if(sid == -2) {
                        for(UnitBase* unit : group) unit->doSetAttackMode(HUNT);
                    } else {
                        require(game->issuePositionOrder(Game::PositionOrder::AttackMove, group, goal, false) != nullptr,
                                "The bench attack-move was not issued");
                    }
                    if(comp.air) {
                        for(Uint32 id : sb.ids) const_cast<UnitBase*>(live(*game, id))->doSetAttackMode(HUNT);
                    } else if(mode == "attack") {
                        const Coord at = centroid(*game, sa.ids);
                        for(Uint32 id : sb.ids)
                            const_cast<UnitBase*>(live(*game, id))->doIssueOrder(UnitOrder::at(UnitOrderType::AttackMove, at), false);
                    }
                    const Uint32 start = game->getGameCycleCount();
                    std::map<Uint32, Uint32> crossed;
                    std::map<Uint32, Coord> lastTile;
                    Uint32 stuck = 0, contact = 0, firstA = 0, resolved = 0;
                    Uint32 now = 0;
                    // The march: from the cycle no member of A forms up any more (at once without a
                    // formation) to contact, A's mean x progress per second against the group's pace
                    // (the slowest member's speed on sand).
                    Uint32 formed = UINT32_MAX, marchEnd = 0;
                    double marchX0 = 0, marchX1 = 0, paceTps = 0;
                    auto meanX = [&]() {
                        double sum = 0; int n = 0;
                        for(Uint32 id : sa.ids)
                            if(const auto* unit = live(*game, id)) { sum += unit->getLocation().x; ++n; }
                        return n ? sum / n : 0.0;
                    };
                    for(Uint32 id : sa.ids) {
                        const auto* unit = live(*game, id);
                        if(!unit) continue;
                        const double tps = unit->getFormationReferenceSpeed().toDouble() * MILLI2CYCLES(1000) / TILESIZE;
                        paceTps = paceTps == 0 ? tps : std::min(paceTps, tps);
                    }
                    while(now < limit) {
                        game->updateGameState();
                        now = game->getGameCycleCount() - start;
                        track(*game, sa, now);
                        track(*game, sb, now);
                        // Everything in a side's fired map before this cycle was empty fired now.
                        if(!firstA && !sa.fired.empty()) firstA = now;
                        if(!contact && (!sa.fired.empty() || !sb.fired.empty())) contact = now;
                        if(formed == UINT32_MAX && !contact) {
                            bool forming = false;
                            for(Uint32 id : sa.ids)
                                if(const auto* unit = live(*game, id)) forming = forming || unit->isFormationForming();
                            if(!forming) { formed = now; marchX0 = meanX(); }
                        }
                        if(formed != UINT32_MAX && !marchEnd && (contact || now + 1 >= limit)) { marchEnd = now; marchX1 = meanX(); }
                        for(Uint32 id : sa.ids) {
                            const auto* unit = live(*game, id);
                            if(!unit) continue;
                            if(lineX > 0 && !crossed.count(id) && unit->getLocation().x >= lineX) crossed[id] = now;
                        }
                        // Blocked on the way: every half second before A's first shot, a member that is
                        // not forming up, stands on the same tile as half a second ago, has no target
                        // and is not within 4 tiles of its goal.
                        if(now % 32 == 0) {
                            for(Uint32 id : sa.ids) {
                                const auto* unit = live(*game, id);
                                if(!unit) continue;
                                const Coord at = unit->getLocation();
                                if(now > MILLI2CYCLES(3000) && !firstA && lastTile.count(id) && lastTile[id] == at
                                   && !unit->isFormationForming()
                                   && FormationPlanner::tileDistance(at, goal) > 4 && !unit->hasATarget())
                                    stuck += 32;
                                lastTile[id] = at;
                            }
                        }
                        if(traceEvery && now % traceEvery == 0) {
                            for(Uint32 id : sa.ids) {
                                const auto* unit = live(*game, id);
                                if(!unit) continue;
                                SDL_Log("FORMATION_BENCH_TRACE: t=%.0f id=%u item=%u at=%d,%d slot=%d,%d dest=%d,%d group=%u "
                                        "forming=%d march=%d remaining=%d moving=%d target=%d next=%d,%d path=%d queued=%d",
                                        seconds(now), id, unit->getItemID(), unit->getLocation().x, unit->getLocation().y,
                                        unit->getFormationSlot().x, unit->getFormationSlot().y, unit->getDestination().x,
                                        unit->getDestination().y, unit->getFormationGroup(), int(unit->isFormationForming()),
                                        int(unit->getFormationMarch()), unit->isInFormation() ? unit->formationRemainingTiles() : -1,
                                        int(unit->isMoving()), int(unit->hasATarget()),
                                        unit->hasNextSpot() ? unit->getNextSpot().x : -1, unit->hasNextSpot() ? unit->getNextSpot().y : -1,
                                        int(unit->pathList.size()), int(unit->pathRequestQueued));
                            }
                        }
                        if(sa.alive(*game) == 0 || sb.alive(*game) == 0) { resolved = now; break; }
                    }
                    int aFired10 = 0, bFired10 = 0;
                    for(const auto& [id, at] : sa.fired) aFired10 += (firstA && at <= firstA + MILLI2CYCLES(10000)) ? 1 : 0;
                    Uint32 firstB = 0;
                    for(const auto& [id, at] : sb.fired) firstB = firstB ? std::min(firstB, at) : at;
                    for(const auto& [id, at] : sb.fired) bFired10 += (at <= firstB + MILLI2CYCLES(10000)) ? 1 : 0;
                    Uint32 crossFirst = UINT32_MAX, crossLast = 0;
                    for(const auto& [id, at] : crossed) { crossFirst = std::min(crossFirst, at); crossLast = std::max(crossLast, at); }
                    int aaUnits = 0;
                    for(Uint32 item : comp.a) aaUnits += (item == Unit_Launcher || item == Unit_Trooper) ? 1 : 0;
                    const int aLeft = sa.aliveValue(*game), bLeft = sb.aliveValue(*game);
                    SDL_Log("FORMATION_BENCH: terrain=%s comp=%s mode=%s trial=%d shape=%s a_units=%zu b_units=%zu a_value=%d "
                            "b_value=%d a_lost=%d b_lost=%d a_survivors=%d b_survivors=%d contact_s=%.1f resolve_s=%.1f "
                            "a_fired=%zu a_fired10=%d b_fired=%zu b_fired10=%d cross_n=%zu cross_spread_s=%.1f "
                            "cross_last_s=%.1f stuck_s=%.1f aa_units=%d aa_fired_air=%zu formed_s=%.1f march_tps=%.3f pace_tps=%.3f",
                            terrain.c_str(), comps[c].c_str(), mode.c_str(), trial, shape.c_str(), sa.ids.size(), sb.ids.size(),
                            sa.start, sb.start, sa.start - aLeft, sb.start - bLeft, sa.alive(*game), sb.alive(*game),
                            contact ? seconds(contact) : -1.0, resolved ? seconds(resolved) : -1.0, sa.fired.size(), aFired10,
                            sb.fired.size(), bFired10, crossed.size(),
                            crossed.empty() ? -1.0 : seconds(crossLast - crossFirst), crossed.empty() ? -1.0 : seconds(crossLast),
                            seconds(stuck), aaUnits, sa.firedAtAir.size(), formed == UINT32_MAX ? -1.0 : seconds(formed),
                            marchEnd > formed && formed != UINT32_MAX ? (marchX1 - marchX0) / seconds(marchEnd - formed) : -1.0, paceTps);
                }
            }
        }
    }
    clearAll();
    SDL_Log("FORMATION_BENCH_DONE");
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    return MENU_QUIT_DEFAULT;
}

} // namespace FormationBench

int runFormationBench() { return FormationBench::run(); }
