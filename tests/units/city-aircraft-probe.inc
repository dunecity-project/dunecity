// Real-engine fixture for the Micropolis city aircraft.
//
// Everything here runs the production Game::updateGameState loop with real
// Airport, AmbientHelicopter, AmbientAirplane, RocketTurret and Bullet objects
// on a real map. Nothing is approximated and no source text is asserted; each
// phase drives the engine and reads the state it produced.
//
//   launch      Airport::updateCityAircraft() with the original doAirport odds:
//               both aircraft appear, never more than one of each per house,
//               a second airport of the same house does not double the fleet,
//               and an unpowered airport launches nothing.
//   patrol      The helicopter steers onto its own saturated roads, reports
//               heavy traffic at a bounded rate, and reports nothing at all
//               over another player's congested roads.
//   flight      The airplane runs the original take-off frames, steers, keeps
//               the waypoints outside the map that ObjectBase::setDestination
//               rejects, and ends every flight by crossing an edge — through
//               each of the four edges, and after its budget expires over the
//               city — never by disappearing over the map.
//   defence     A hostile rocket turret can target, damage and destroy both,
//               while same-team and ordinary ground weapons cannot; the wreck
//               leaves no tile, unit-count or airport-slot reference behind.
//   continuation A save taken mid-patrol/mid-flight reloads and then replays
//               bit-identically, so the new state is complete and deterministic.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Bullet.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <FileClasses/GFXManager.h>
#include <dunecity/CityAircraftPolicy.h>
#include <dunecity/CitySimulation.h>
#include <structures/Airport.h>
#include <structures/RocketTurret.h>
#include <units/AmbientAirplane.h>
#include <units/AmbientHelicopter.h>
#include <units/UnitBase.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace CA = DuneCity::CityAircraft;

std::string gCityAircraftMod;
std::string gCityAircraftOut;
std::ofstream gCityAircraftCsv;

void cityRequire(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error("city-aircraft probe: " + why);
}

void record(const std::string& phase, const std::string& detail) {
    gCityAircraftCsv << gCityAircraftMod << ',' << phase << ',' << detail << '\n';
    gCityAircraftCsv.flush();
    SDL_Log("CITY_AIRCRAFT %s: %s", phase.c_str(), detail.c_str());
}

std::string gMapData;

std::unique_ptr<Game> newCityGame(int seed) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("city-aircraft.ini", gMapData, "City aircraft", true, settings.gameOptions);
    init.randomSeed = seed;
    GameInitSettings::HouseInfo owner(HOUSE_ATREIDES, 1);
    owner.addPlayerInfo({"City tester", "HumanPlayer"});
    init.addHouseInfo(owner);
    GameInitSettings::HouseInfo enemy(HOUSE_HARKONNEN, 2);
    enemy.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemy);
    game->initGame(init);
    game->initializeGameLoop();
    cityRequire(game->isCitySimEnabled(), "city simulation must be active for this fixture");
    return game;
}

/// Flatten a patch so a 3x3 Airport and the road grid can be placed on it.
void clearGround(const Coord& origin, int width, int height) {
    for(int y = origin.y; y < origin.y + height; ++y)
        for(int x = origin.x; x < origin.x + width; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            Tile* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Rock);
            tile->setRoad(false);
        }
}

void powerHouse(House* house) {
    for(int i = 0; i < 8 && house->getProducedPower() < house->getPowerRequirement() + 100; ++i)
        house->placeStructure(NONE_ID, Structure_WindTrap, 2, 2 + 3 * i, true, true);
    cityRequire(house->hasPower(), "fixture house must be powered");
}

Airport* placeAirport(House* house, const Coord& at) {
    clearGround(at + Coord(-1, -1), 5, 5);
    auto* structure = house->placeStructure(NONE_ID, Structure_Airport, at.x, at.y, true, true);
    cityRequire(structure != nullptr, "could not place the city airport");
    structure->setVisible(VIS_ALL, true);
    return dynamic_cast<Airport*>(structure);
}

/// A block of roads owned by `house`, held at the original saturated traffic
/// density. The density layer is the production one; only the fixture writes
/// to it, exactly as the traffic simulation would.
void paintCongestedRoads(DuneCity::CitySimulation* citySim, House* house,
                         const Coord& origin, int size) {
    const int blockSize = std::max(1, citySim->getTrafficDensityMap().getBlockSize());
    for(int y = origin.y; y < origin.y + size; ++y)
        for(int x = origin.x; x < origin.x + size; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            Tile* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Rock);
            tile->setRoad(true);
            tile->setOwner(house->getHouseID());
            citySim->trafficDensityMap_.set(x / blockSize, y / blockSize,
                                            static_cast<uint8_t>(CA::kHeavyTrafficSteerThreshold));
        }
}

bool tileReferences(Uint32 objectID) {
    for(int y = 0; y < currentGameMap->getSizeY(); ++y)
        for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
            const Tile* tile = currentGameMap->getTile(x, y);
            const auto& air = tile->getAirUnitList();
            if(std::find(air.begin(), air.end(), objectID) != air.end()) return true;
            if(tile->getGroundObject() != nullptr
               && tile->getGroundObject()->getObjectID() == objectID) return true;
        }
    return false;
}

int liveUnitsOfType(int itemID) {
    int count = 0;
    for(const UnitBase* unit : unitList) if(unit->getItemID() == itemID) ++count;
    return count;
}

/// No entry for `objectID` is left anywhere in the spatial grid.
bool spatialGridReferences(Uint32 objectID) {
    auto* grid = currentGame->getSpatialGrid();
    if(grid == nullptr) return false;
    std::vector<SpatialGridEntry> entries;
    for(int y = 0; y < grid->getGridHeight(); ++y)
        for(int x = 0; x < grid->getGridWidth(); ++x) {
            entries.clear();
            if(!grid->collectEntries(Coord(x, y), entries)) continue;
            for(const SpatialGridEntry& entry : entries)
                if(entry.object != nullptr && entry.object->getObjectID() == objectID) return true;
        }
    return false;
}

/// World units from the nearest map boundary; negative once outside.
long edgeSlackWorld(long x, long y) {
    const long w = static_cast<long>(currentGameMap->getSizeX()) * TILESIZE;
    const long h = static_cast<long>(currentGameMap->getSizeY()) * TILESIZE;
    return std::min(std::min(x, w - 1 - x), std::min(y, h - 1 - y));
}

/// AmbientAirplane::update() removes the plane in the same cycle in which its
/// centre crosses the boundary, so the last frame a test can still read it on
/// is up to one cycle of travel short of the edge. One cycle is at most 10.24
/// world units on an axis and 10.86 diagonally (ObjectData MaxSpeed and
/// CityAircraft::headingSpeed), so 12 is the tolerance for "it was at the edge".
constexpr long kOneStepWorldUnits = 12;

/// Which edge the plane was leaving through on its last live frame, or
/// "inside" when it was nowhere near one — which is the bug this phase exists
/// to catch: a plane that vanishes over the city instead of flying out.
std::string leavingEdge(long x, long y) {
    const long w = static_cast<long>(currentGameMap->getSizeX()) * TILESIZE;
    const long h = static_cast<long>(currentGameMap->getSizeY()) * TILESIZE;
    const long west = x, east = w - 1 - x, north = y, south = h - 1 - y;
    const long nearest = std::min(std::min(west, east), std::min(north, south));
    if(nearest > kOneStepWorldUnits) return "inside";
    if(nearest == west) return "west";
    if(nearest == east) return "east";
    if(nearest == north) return "north";
    return "south";
}

struct FlightEnd {
    bool gone = false;
    long x = 0, y = 0;                  ///< last live centre, world units
    int remainingTicks = 0;
    double travelTiles = 0.0;
    int cycles = 0;
    bool heldOutsideWaypoint = false;   ///< an off-map waypoint actually stuck
    bool heldSentinelWhileCruising = false;
    std::set<int> headings;
};

/// Follow a live plane through the real game loop until it is gone or the cycle
/// budget runs out. Nothing is forced: no realX/realY and no destination is
/// written, so what is measured is the movement the engine produces.
FlightEnd followFlight(Game* game, Uint32 planeID, int cycleBudget) {
    FlightEnd end;
    auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(live != nullptr, "the plane to follow was not alive to start with");
    long prevX = lround(live->realX), prevY = lround(live->realY);
    end.x = prevX;
    end.y = prevY;
    end.remainingTicks = live->getRemainingTicks();

    for(int cycle = 0; cycle < cycleBudget; ++cycle) {
        game->updateGameState();
        ++end.cycles;
        live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) {
            end.gone = true;
            break;
        }

        const long x = lround(live->realX), y = lround(live->realY);
        const double dx = static_cast<double>(x - prevX), dy = static_cast<double>(y - prevY);
        end.travelTiles += std::sqrt(dx * dx + dy * dy) / TILESIZE;
        prevX = x;
        prevY = y;

        const Coord destination = live->getDestination();
        if(destination.isValid()
           && CA::isOutsideMap(destination.x, destination.y, currentGameMap->getSizeX(),
                               currentGameMap->getSizeY()))
            end.heldOutsideWaypoint = true;
        // The outside margin reaches tile -1 on the west and north edges, which
        // is INVALID_POS. A cruising plane must never be left with the engine's
        // "no destination" sentinel, or it stops steering altogether.
        if(destination.isInvalid() && live->getTakeoffFrame() == 0 && cycle > 20)
            end.heldSentinelWhileCruising = true;
        if(live->getTakeoffFrame() == 0) end.headings.insert(live->getAngle());

        end.x = x;
        end.y = y;
        end.remainingTicks = live->getRemainingTicks();
    }
    return end;
}

/// Every flight that ends on its own must end beyond a map edge, with the
/// engine's bookkeeping clean. `doAirplaneSprite` has exactly one ending,
/// sprite.cpp:856 spriteNotInBounds; there is no landing and no timeout.
void requireLeftTheMap(const FlightEnd& end, Uint32 planeID, House* owner,
                       const std::string& what, const std::string& expectedEdge = "") {
    cityRequire(end.gone, what + ": the plane never ended its flight");
    cityRequire(!end.heldSentinelWhileCruising,
                what + ": a cruising plane was left with the invalid-destination sentinel");
    const std::string edge = leavingEdge(end.x, end.y);
    cityRequire(edge != "inside",
                what + ": the plane disappeared " + std::to_string(edgeSlackWorld(end.x, end.y) / TILESIZE)
                     + " tiles inside the map (" + std::to_string(end.x / TILESIZE) + ','
                     + std::to_string(end.y / TILESIZE) + ") instead of crossing an edge");
    if(!expectedEdge.empty())
        cityRequire(edge == expectedEdge,
                    what + ": the plane left through the " + edge + " edge, expected " + expectedEdge);
    cityRequire(!tileReferences(planeID), what + ": a departed plane left a tile reference");
    cityRequire(!spatialGridReferences(planeID), what + ": a departed plane left a spatial-grid entry");
    cityRequire(liveUnitsOfType(Unit_AmbientAirplane) == 0,
                what + ": a departed plane is still in the unit list");
    cityRequire(owner->getNumItems(Unit_AmbientAirplane) == 0,
                what + ": a departed plane was still counted by its house");
}

// ---------------------------------------------------------------------------
// art
// ---------------------------------------------------------------------------
void checkOriginalArtLoaded() {
    auto game = newCityGame(1800);
    auto* owner = game->getHouse(HOUSE_ATREIDES);

    struct ArtSpec { int objPic; int itemID; int cell; int rows; };
    for(const ArtSpec& spec : {ArtSpec{ObjPic_CityHelicopter, Unit_AmbientHelicopter, 2 * D2_TILESIZE, 1},
                               ArtSpec{ObjPic_CityAirplane, Unit_AmbientAirplane, 3 * D2_TILESIZE, 4}}) {
        for(int house = 0; house < NUM_HOUSES; ++house) {
            auto art = pGFXManager->getObjPic(spec.objPic, house);
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                cityRequire(art[zoom] != nullptr, "the original aircraft art did not load");
                int w = 0, h = 0;
                cityRequire(SDL_QueryTexture(art[zoom], nullptr, nullptr, &w, &h) == 0,
                            "the aircraft texture could not be queried");
                cityRequire(w == NUM_ANGLES * spec.cell * (zoom + 1),
                            "the aircraft sheet is not eight original frames wide");
                cityRequire(h == spec.rows * spec.cell * (zoom + 1),
                            "the aircraft sheet has the wrong number of original rows");
            }
        }
        // The unit really indexes that sheet, so every heading is reachable.
        auto* unit = owner->createUnit(spec.itemID, true);
        cityRequire(unit != nullptr, "could not create the aircraft to check its art");
        cityRequire(unit->numImagesX == NUM_ANGLES, "aircraft does not use the eight original headings");
        cityRequire(unit->numImagesY == spec.rows, "aircraft does not use the original row count");
        cityRequire(unit->graphicID == static_cast<int>(spec.objPic), "aircraft is not using the original art");
        unit->cancelDeployment();
    }
    record("art", "helicopter=8x1x32 airplane=8x4x48 zoom_levels="
                + std::to_string(NUM_ZOOMLEVEL) + " houses=" + std::to_string(NUM_HOUSES));
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// launch
// ---------------------------------------------------------------------------
void checkOriginalMotion() {
    auto game = newCityGame(1940);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    // Independently transcribed CDx/CDy tables, reordered to ANGLETYPE.
    const int copterX[8] = {5, 3, 0, -3, -5, -3, 0, 3};
    const int copterY[8] = {0, -3, -5, -3, 0, 3, 5, 3};
    const int planeX[8] = {8, 6, 0, -6, -8, -6, 0, 6};
    const int planeY[8] = {0, -6, -8, -6, 0, 6, 8, 6};
    const double scale = static_cast<double>(TILESIZE) * GAMESPEED_DEFAULT / (16 * 50);
    for(int item : {Unit_AmbientHelicopter, Unit_AmbientAirplane}) {
        for(int heading = 0; heading < 8; ++heading) {
            auto* unit = dynamic_cast<AirUnit*>(owner->createUnit(item, true));
            cityRequire(unit != nullptr, "could not create the original-motion aircraft");
            unit->deploy(Coord(24, 16));
            unit->currentMaxSpeed = game->objectData.data[item][HOUSE_ATREIDES].maxspeed;
            unit->angle = FixPoint(heading);
            unit->drawnAngle = static_cast<Sint8>(heading);
            const FixPoint beforeX = unit->realX, beforeY = unit->realY;
            unit->move();
            const int* dx = item == Unit_AmbientHelicopter ? copterX : planeX;
            const int* dy = item == Unit_AmbientHelicopter ? copterY : planeY;
            SDL_Log("CITY_AIRCRAFT vector item=%d heading=%d actual=(%.6f,%.6f) expected=(%.6f,%.6f) axis_speed=%.6f",
                    item, heading, (unit->realX - beforeX).toDouble(), (unit->realY - beforeY).toDouble(),
                    dx[heading] * scale, dy[heading] * scale, unit->currentMaxSpeed.toDouble());
            // The engine's fixed-point trig has a small cardinal-axis drift.
            // 0.01 world units is 1/6400 tile, below rendered pixel precision.
            cityRequire(std::abs((unit->realX - beforeX).toDouble() - dx[heading] * scale) < 0.01
                     && std::abs((unit->realY - beforeY).toDouble() - dy[heading] * scale) < 0.01,
                        "the aircraft does not follow the original heading step vector");
            unit->cancelDeployment();
        }
    }
    record("motion", "original_vectors=16 diagonal_components_preserved=1");
    game.reset(); currentGame = nullptr;
}

void checkSparseCongestedRoad() {
    auto game = newCityGame(1941);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner); game->updateGameState();
    auto* sim = game->getCitySimulation();
    cityRequire(sim != nullptr, "sparse-road fixture needs city simulation");
    const Coord home(24, 16), road(25, 17);
    paintCongestedRoads(sim, owner, road, 1);
    auto* copter = dynamic_cast<AmbientHelicopter*>(owner->createUnit(Unit_AmbientHelicopter, true));
    cityRequire(copter != nullptr, "could not create the sparse-road helicopter");
    copter->deploy(home); copter->setHome(home); copter->setDestination(home);
    copter->seekHeavyTraffic();
    cityRequire(copter->getDestination() == road,
                "saturated friendly road on opposite coordinate parity was ignored");
    record("boundary", "sparse_owned_road_selected=1");
    game.reset(); currentGame = nullptr;
}

void checkAirportLaunch() {
    auto game = newCityGame(1801);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    placeAirport(owner, Coord(20, 12));
    // A second airport of the same house must not double the fleet: the
    // original's generatePlane/generateCopter are singletons.
    placeAirport(owner, Coord(30, 20));

    int maxPlanes = 0, maxCopters = 0, planeLaunches = 0, copterLaunches = 0;
    std::set<Uint32> seenPlanes, seenCopters;
    for(int cycle = 0; cycle < 30000; ++cycle) {
        game->updateGameState();
        const int planes = owner->getNumItems(Unit_AmbientAirplane);
        const int copters = owner->getNumItems(Unit_AmbientHelicopter);
        maxPlanes = std::max(maxPlanes, planes);
        maxCopters = std::max(maxCopters, copters);
        cityRequire(planes <= 1, "a house kept more than one city airplane");
        cityRequire(copters <= 1, "a house kept more than one city helicopter");
        for(const UnitBase* unit : unitList) {
            if(unit->getOwner() != owner) continue;
            if(unit->getItemID() == Unit_AmbientAirplane && seenPlanes.insert(unit->getObjectID()).second) ++planeLaunches;
            if(unit->getItemID() == Unit_AmbientHelicopter && seenCopters.insert(unit->getObjectID()).second) ++copterLaunches;
        }
        if(planeLaunches >= 2 && copterLaunches >= 1) break;
    }
    cityRequire(planeLaunches >= 1, "the airport never launched a city airplane");
    cityRequire(copterLaunches >= 1, "the airport never launched a city helicopter");
    // Several launches over the run prove the singleton slot is released and
    // reused rather than used once and stuck.
    cityRequire(planeLaunches >= 2, "the airplane slot was never reused after a flight ended");
    cityRequire(maxPlanes <= 1 && maxCopters <= 1, "fleet cap broken");
    record("launch", "planes=" + std::to_string(planeLaunches)
                   + " copters=" + std::to_string(copterLaunches)
                   + " max_planes=" + std::to_string(maxPlanes)
                   + " max_copters=" + std::to_string(maxCopters) + " airports=2");
    game.reset();
    currentGame = nullptr;
}

void checkUnpoweredAirportLaunchesNothing() {
    auto game = newCityGame(1802);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    placeAirport(owner, Coord(20, 12));   // no WindTraps: the airport is dark
    cityRequire(!owner->hasPower(), "fixture airport must be unpowered");
    for(int cycle = 0; cycle < 8000; ++cycle) {
        game->updateGameState();
        cityRequire(owner->getNumItems(Unit_AmbientAirplane) == 0
                 && owner->getNumItems(Unit_AmbientHelicopter) == 0,
                    "an unpowered airport launched city aircraft");
    }
    record("launch", "unpowered_airport_launches=0 cycles=8000");
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// patrol
// ---------------------------------------------------------------------------
void checkTrafficPatrolAndReports() {
    auto game = newCityGame(1803);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    powerHouse(owner);
    game->updateGameState();
    auto* citySim = game->getCitySimulation();
    cityRequire(citySim != nullptr, "city simulation was not created");

    const Coord cityOrigin(18, 10);
    const int citySize = 12;
    paintCongestedRoads(citySim, owner, cityOrigin, citySize);

    auto* copter = dynamic_cast<AmbientHelicopter*>(owner->createUnit(Unit_AmbientHelicopter, true));
    cityRequire(copter != nullptr, "could not create the city helicopter");
    const Coord pad = cityOrigin + Coord(citySize / 2, citySize / 2);
    copter->deploy(pad);
    copter->setHome(pad);
    cityRequire(copter->getHome() == pad, "helicopter did not take its airport as home");
    cityRequire(copter->getPatrolCount() == CA::kHelicopterPatrolCount,
                "helicopter did not start the original 1500-tick patrol");

    const Uint32 copterID = copter->getObjectID();
    const Uint32 startCycle = game->getGameCycleCount();
    int headingChanges = 0;
    int lastAngle = copter->getAngle();
    Uint32 lastReportCycle = 0;
    Uint32 shortestReportGap = 0xffffffffu;
    Uint32 previousReports = 0;
    std::set<std::pair<int, int>> visitedRoads;

    // Hold the city congested, exactly as a city that is actually jammed would
    // be, and let the production patrol run its whole original lifetime.
    while(game->getObjectManager().getObject(copterID) != nullptr) {
        paintCongestedRoads(citySim, owner, cityOrigin, citySize);
        game->updateGameState();
        auto* live = dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(copterID));
        if(live == nullptr) break;
        if(live->getAngle() != lastAngle) { ++headingChanges; lastAngle = live->getAngle(); }
        if(currentGameMap->tileExists(live->getLocation())
           && currentGameMap->getTile(live->getLocation())->isRoad())
            visitedRoads.insert({live->getLocation().x, live->getLocation().y});
        if((game->getGameCycleCount() % 250) == 0) {
            const Coord at = live->getLocation();
            const Coord to = live->getDestination();
            SDL_Log("CITY_AIRCRAFT patrol-trace cycle=%u at=%d,%d dest=%d,%d angle=%d "
                    "patrol=%d cooldown=%d reports=%u density=%d road=%d owner=%d",
                    game->getGameCycleCount(), at.x, at.y, to.x, to.y, live->getAngle(),
                    live->getPatrolCount(), live->getReportCooldown(), live->getReportCount(),
                    currentGameMap->tileExists(at)
                        ? citySim->getTrafficDensityMap().worldGet(at.x, at.y) : -1,
                    currentGameMap->tileExists(at) ? currentGameMap->getTile(at)->isRoad() : -1,
                    currentGameMap->tileExists(at) ? currentGameMap->getTile(at)->getOwner() : -1);
        }
        if(live->getReportCount() != previousReports) {
            const Uint32 now = game->getGameCycleCount();
            if(lastReportCycle != 0) shortestReportGap = std::min(shortestReportGap, now - lastReportCycle);
            lastReportCycle = now;
            previousReports = live->getReportCount();
            cityRequire(live->getReportCooldown() == CA::kReportCooldown,
                        "a report did not arm the original 200-tick cooldown");
        }
        cityRequire(game->getGameCycleCount() - startCycle < 40000u,
                    "the helicopter patrol never ended");
    }

    const Uint32 flown = game->getGameCycleCount() - startCycle;
    const int spriteTicks = static_cast<int>(CA::spriteCycle(game->getGameCycleCount())
                                           - CA::spriteCycle(startCycle));
    cityRequire(previousReports > 0, "the helicopter never reported its city's heavy traffic");
    // Bounded rate: the original arms a 200 sprite-tick cooldown after each
    // report, so a 1500-tick patrol can produce at most eight of them.
    const int allowed = spriteTicks / CA::kReportCooldown + 1;
    cityRequire(static_cast<int>(previousReports) <= allowed,
                "heavy-traffic reports exceeded the original cooldown rate");
    if(previousReports > 1)
        cityRequire(shortestReportGap >= static_cast<Uint32>(CA::kReportCooldown * CA::kMicropolisSpriteIntervalMs
                                                             / GAMESPEED_DEFAULT),
                    "two reports arrived inside one cooldown");
    cityRequire(headingChanges > 0, "the helicopter never changed heading while patrolling");
    cityRequire(visitedRoads.size() > 1, "the helicopter never travelled over its city");
    // The patrol ends by landing, not by exploding or by being forgotten.
    cityRequire(owner->getNumItems(Unit_AmbientHelicopter) == 0, "a landed helicopter was still counted");
    cityRequire(!tileReferences(copterID), "a landed helicopter left a tile reference");

    record("patrol", "reports=" + std::to_string(previousReports)
                   + " allowed=" + std::to_string(allowed)
                   + " sprite_ticks=" + std::to_string(spriteTicks)
                   + " cycles=" + std::to_string(flown)
                   + " heading_changes=" + std::to_string(headingChanges)
                   + " road_tiles_visited=" + std::to_string(visitedRoads.size()));

    // Same city, same congestion, but the roads now belong to the other player:
    // a helicopter must not report another player's traffic.
    paintCongestedRoads(citySim, enemy, cityOrigin, citySize);
    auto* foreign = dynamic_cast<AmbientHelicopter*>(owner->createUnit(Unit_AmbientHelicopter, true));
    cityRequire(foreign != nullptr, "could not create the second city helicopter");
    foreign->deploy(pad);
    foreign->setHome(pad);
    const Uint32 foreignID = foreign->getObjectID();
    for(int cycle = 0; cycle < 4000; ++cycle) {
        paintCongestedRoads(citySim, enemy, cityOrigin, citySize);
        game->updateGameState();
        auto* live = dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(foreignID));
        if(live == nullptr) break;
        cityRequire(live->getReportCount() == 0, "a helicopter reported another player's traffic");
    }
    record("patrol", "foreign_city_reports=0 cycles=4000");
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// flight
// ---------------------------------------------------------------------------
void checkAirplaneFlight() {
    auto game = newCityGame(1804);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    game->updateGameState();

    const Coord runway(12, 15);
    clearGround(runway + Coord(-1, -1), 5, 5);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the city airplane");
    plane->deploy(runway);
    plane->beginTakeoff();
    cityRequire(plane->getTakeoffFrame() == CA::kAirplaneFirstTakeoffFrame,
                "take-off did not start at the original frame 11");

    const Uint32 planeID = plane->getObjectID();
    const FixPoint startX = plane->realX;
    std::vector<int> takeoffFrames{plane->getTakeoffFrame()};
    std::set<int> headings;
    int previousFrame = plane->getTakeoffFrame();
    FixPoint takeoffEndX = startX;
    bool cruising = false;
    bool heldOutsideWaypoint = false;
    bool heldSentinelWhileCruising = false;
    long lastX = lround(plane->realX), lastY = lround(plane->realY);
    int lastTicks = plane->getRemainingTicks();

    // Longer than kAirplaneMaxTicks (6000 sprite ticks = 18750 cycles) on
    // purpose, so a flight whose budget expires is still followed to its real
    // ending instead of being cut off by the loop and called "off map".
    for(int cycle = 0; cycle < 26000; ++cycle) {
        game->updateGameState();
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) break;
        if(live->getTakeoffFrame() != previousFrame) {
            previousFrame = live->getTakeoffFrame();
            if(previousFrame > 0) takeoffFrames.push_back(previousFrame);
            else { cruising = true; takeoffEndX = live->realX; }
        }
        if(cruising) headings.insert(live->getAngle());
        const Coord destination = live->getDestination();
        if(destination.isValid()
           && CA::isOutsideMap(destination.x, destination.y, currentGameMap->getSizeX(),
                               currentGameMap->getSizeY()))
            heldOutsideWaypoint = true;
        if(destination.isInvalid() && live->getTakeoffFrame() == 0 && cycle > 20)
            heldSentinelWhileCruising = true;
        // A plane is not a patrol: it must never adopt the helicopter's home.
        cityRequire(live->getRemainingTicks() <= CA::kAirplaneMaxTicks, "flight budget grew");
        lastX = lround(live->realX);
        lastY = lround(live->realY);
        lastTicks = live->getRemainingTicks();
    }

    cityRequire(takeoffFrames == std::vector<int>({11, 10, 9}),
                "take-off did not run the original 11-10-9 frames in order");
    cityRequire(cruising, "the airplane never finished its take-off run");
    cityRequire(takeoffEndX > startX, "the airplane did not climb out eastwards");
    cityRequire(headings.size() > 1, "the airplane never changed heading after take-off");
    // Whether this particular flight happened to draw an outside waypoint is up
    // to the shared RNG; that the outside draws are kept at all is asserted
    // deterministically in checkOutsideWaypointHandling().
    cityRequire(!heldSentinelWhileCruising,
                "a cruising plane was left with the invalid-destination sentinel");
    FlightEnd natural;
    natural.gone = game->getObjectManager().getObject(planeID) == nullptr;
    natural.x = lastX;
    natural.y = lastY;
    natural.remainingTicks = lastTicks;
    requireLeftTheMap(natural, planeID, owner, "natural flight");

    record("flight", "takeoff_frames=11-10-9 headings=" + std::to_string(headings.size())
                   + " climb_out_world_units=" + std::to_string((takeoffEndX - startX).lround())
                   + " outside_waypoint_held=" + std::to_string(heldOutsideWaypoint ? 1 : 0)
                   + " ended_edge=" + leavingEdge(lastX, lastY)
                   + " end_tile=" + std::to_string(lastX / TILESIZE) + ','
                   + std::to_string(lastY / TILESIZE)
                   + " ticks_left=" + std::to_string(lastTicks));

    // The eastern-edge variant of newSprite(): no take-off run, heading west.
    const Coord edge(currentGameMap->getSizeX() - 4, 15);
    clearGround(edge + Coord(-1, -1), 4, 4);
    auto* westbound = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(westbound != nullptr, "could not create the westbound airplane");
    westbound->deploy(edge);
    westbound->beginWestboundCruise();
    cityRequire(westbound->getTakeoffFrame() == 0, "the westbound plane ran a take-off anyway");
    cityRequire(westbound->getAngle() == LEFT, "the westbound plane did not head west");
    const FixPoint westStart = westbound->realX;
    for(int cycle = 0; cycle < 200; ++cycle) game->updateGameState();
    auto* liveWest = dynamic_cast<AmbientAirplane*>(
        game->getObjectManager().getObject(westbound->getObjectID()));
    cityRequire(liveWest == nullptr || liveWest->realX < westStart,
                "the westbound plane did not fly west");
    record("flight", "eastern_edge_variant=westbound_no_takeoff");
    game.reset();
    currentGame = nullptr;
}

/// The waypoints `doAirplaneSprite` draws reach 50 Micropolis units beyond
/// every edge, and the first unit beyond the west or north edge floors to tile
/// -1, which is the engine's INVALID_POS sentinel. The airplane has to keep
/// both; every other unit has to keep the map-tile validation it relies on.
void checkOutsideWaypointHandling() {
    auto game = newCityGame(1806);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    game->updateGameState();

    const Coord runway(12, 15);
    clearGround(runway + Coord(-1, -1), 5, 5);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the waypoint-test airplane");
    plane->deploy(runway);

    // Negative waypoints are kept as given.
    plane->setFlightDestination(Coord(-4, 7));
    cityRequire(plane->getDestination() == Coord(-4, 7), "an outside waypoint was rejected");
    cityRequire(plane->getDestination().isValid(), "an outside waypoint read as no destination");

    // The two sentinel corners: -1 on either axis would make Coord::isInvalid()
    // true, so it is pushed one tile further out instead of being lost.
    plane->setFlightDestination(Coord(-1, 7));
    cityRequire(plane->getDestination().isValid() && plane->getDestination().x < 0
                && plane->getDestination().y == 7,
                "a west-margin waypoint of tile -1 was mistaken for no destination");
    plane->setFlightDestination(Coord(7, -1));
    cityRequire(plane->getDestination().isValid() && plane->getDestination().y < 0
                && plane->getDestination().x == 7,
                "a north-margin waypoint of tile -1 was mistaken for no destination");
    plane->setFlightDestination(Coord(-1, -1));
    cityRequire(plane->getDestination().isValid(),
                "the (-1,-1) corner of the outside margin was mistaken for no destination");
    const Coord beyondEast(currentGameMap->getSizeX() + 3, currentGameMap->getSizeY() + 3);
    plane->setFlightDestination(beyondEast);
    cityRequire(plane->getDestination() == beyondEast, "a waypoint beyond the far edges was rejected");

    // The production draw: doAirplaneSprite's own random waypoints must reach
    // outside the map and survive being stored. Every draw is checked, so a
    // waypoint silently dropped back to the previous one is caught even if the
    // particular draw happens to be interior.
    int outsideDraws = 0, keptDraws = 0;
    for(int draw = 0; draw < 400; ++draw) {
        plane->setFlightDestination(Coord::Invalid());
        plane->pickDestination();
        const Coord drawn = plane->getDestination();
        cityRequire(drawn.isValid(), "a drawn waypoint was lost: the plane has no destination");
        ++keptDraws;
        if(CA::isOutsideMap(drawn.x, drawn.y, currentGameMap->getSizeX(), currentGameMap->getSizeY()))
            ++outsideDraws;
        // The original margin is 50 Micropolis units, just over three tiles.
        const int margin = CA::kAirplaneDestinationMargin / CA::kMicropolisUnitsPerTile + 1;
        cityRequire(drawn.x >= -margin && drawn.y >= -margin
                    && drawn.x <= currentGameMap->getSizeX() + margin
                    && drawn.y <= currentGameMap->getSizeY() + margin,
                    "a drawn waypoint left the original's outside margin");
    }
    cityRequire(keptDraws == 400, "a drawn waypoint was not stored");
    cityRequire(outsideDraws > 0,
                "no drawn waypoint ever reached outside the map, so the outside draws of "
                "doAirplaneSprite are still being rejected");

    // ObjectBase::setDestination must still refuse off-map waypoints for units
    // that have to reach them on the ground.
    const Coord infantryAt(14, 18);
    clearGround(infantryAt, 2, 2);
    auto* soldier = owner->createUnit(Unit_Soldier, true);
    cityRequire(soldier != nullptr, "could not create the ground unit for the validator check");
    soldier->deploy(infantryAt);
    soldier->setDestination(Coord(-4, 7));
    cityRequire(soldier->getDestination() != Coord(-4, 7),
                "ObjectBase::setDestination now accepts off-map waypoints for ground units");
    soldier->setDestination(beyondEast);
    cityRequire(soldier->getDestination() != beyondEast,
                "ObjectBase::setDestination now accepts waypoints beyond the far edges");

    record("flight", "outside_waypoints=kept sentinel_corners=3 ground_validation=unchanged"
                     " draws=400 outside_draws=" + std::to_string(outsideDraws));
    game.reset();
    currentGame = nullptr;
}

/// A flight must end by crossing a boundary, through whichever edge it is
/// nearest, and the expiry of DuneCity's flight budget must start that
/// departure rather than delete the plane over the city. No coordinate is
/// written here: the planes are deployed in bounds and fly out themselves.
void checkDepartureThroughEachEdge() {
    struct Case { const char* edge; Coord at; };
    auto game = newCityGame(1807);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    game->updateGameState();

    const int w = currentGameMap->getSizeX();
    const int h = currentGameMap->getSizeY();
    for(const Case& testCase : {Case{"west", Coord(4, h / 2)},
                                Case{"east", Coord(w - 5, h / 2)},
                                Case{"north", Coord(w / 2, 2)},
                                Case{"south", Coord(w / 2, h - 3)}}) {
        clearGround(testCase.at + Coord(-1, -1), 3, 3);
        auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
        cityRequire(plane != nullptr, "could not create the departing airplane");
        plane->deploy(testCase.at);
        const Uint32 planeID = plane->getObjectID();
        // Spend the budget: the flight is over, so it has to leave.
        plane->remainingTicks = 0;

        const FlightEnd end = followFlight(game.get(), planeID, 4000);
        const std::string what = std::string("departure through the ") + testCase.edge + " edge";
        requireLeftTheMap(end, planeID, owner, what, testCase.edge);
        cityRequire(end.travelTiles > 1.0, what + ": the plane did not actually travel");
        record("flight", std::string("departure_edge=") + testCase.edge
                       + " travel_tiles=" + std::to_string(static_cast<long>(end.travelTiles))
                       + " cycles=" + std::to_string(end.cycles)
                       + " end_tile=" + std::to_string(end.x / TILESIZE) + ','
                       + std::to_string(end.y / TILESIZE));
    }

    // Expiry over the city: a real take-off, a cruise, then the budget runs out
    // far from every edge. The plane must fly out, not disappear where it is.
    const Coord runway(w / 2, h / 2);
    clearGround(runway + Coord(-1, -1), 5, 5);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the expiring airplane");
    plane->deploy(runway);
    plane->beginTakeoff();
    const Uint32 planeID = plane->getObjectID();
    for(int cycle = 0; cycle < 200; ++cycle) game->updateGameState();
    auto* cruising = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(cruising != nullptr, "the expiring airplane did not survive its take-off");
    cityRequire(cruising->getTakeoffFrame() == 0, "the expiring airplane never reached cruise");
    const long expiryX = lround(cruising->realX), expiryY = lround(cruising->realY);
    cityRequire(edgeSlackWorld(expiryX, expiryY) > 4 * TILESIZE,
                "the expiry case has to start over the city, not at an edge");
    cruising->remainingTicks = 0;

    const FlightEnd expired = followFlight(game.get(), planeID, 8000);
    requireLeftTheMap(expired, planeID, owner, "budget expiry over the city");
    cityRequire(expired.travelTiles > 4.0,
                "budget expiry over the city: the plane vanished instead of flying out");
    record("flight", "expiry_start_tile=" + std::to_string(expiryX / TILESIZE) + ','
                   + std::to_string(expiryY / TILESIZE)
                   + " expiry_edge=" + leavingEdge(expired.x, expired.y)
                   + " travel_tiles=" + std::to_string(static_cast<long>(expired.travelTiles))
                   + " end_tile=" + std::to_string(expired.x / TILESIZE) + ','
                   + std::to_string(expired.y / TILESIZE));
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// defence
// ---------------------------------------------------------------------------
void checkEnemyAirDefence() {
    for(int item : {Unit_AmbientHelicopter, Unit_AmbientAirplane}) {
        auto game = newCityGame(1805 + item);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* enemy = game->getHouse(HOUSE_HARKONNEN);
        powerHouse(owner);
        powerHouse(enemy);
        game->updateGameState();

        const Coord site(24, 16);
        clearGround(site + Coord(-2, -2), 8, 8);
        auto* hostile = dynamic_cast<RocketTurret*>(
            enemy->placeStructure(NONE_ID, Structure_RocketTurret, site.x, site.y, true, true));
        cityRequire(hostile != nullptr, "could not place the hostile rocket turret");
        hostile->setVisible(VIS_ALL, true);
        // The same-team turret and the ground-weapon control sit far outside
        // everyone's weapon range: they are here to be asked what they may
        // target, not to join the fight and shoot the hostile turret down
        // before it can finish its job.
        const Coord control(4, 26);
        clearGround(control + Coord(-1, -1), 6, 4);
        auto* allied = dynamic_cast<RocketTurret*>(
            owner->placeStructure(NONE_ID, Structure_RocketTurret, control.x, control.y, true, true));
        cityRequire(allied != nullptr, "could not place the same-team rocket turret");
        allied->setVisible(VIS_ALL, true);
        auto* tank = owner->placeUnit(Unit_Tank, control.x + 3, control.y, true);
        cityRequire(tank != nullptr, "could not place the ground-weapon control");
        tank->doSetAttackMode(STOP);

        // Hold the aircraft still so the kill is a damage test, not a chase.
        game->objectData.data[item][HOUSE_ATREIDES].maxspeed = 0;
        auto* aircraft = dynamic_cast<AirUnit*>(owner->createUnit(item, true));
        cityRequire(aircraft != nullptr, "could not create the city aircraft");
        aircraft->deploy(site + Coord(3, 0));
        aircraft->currentMaxSpeed = 0;
        aircraft->setVisible(VIS_ALL, true);
        const Uint32 aircraftID = aircraft->getObjectID();
        const int startHP = lround(aircraft->getHealth());

        cityRequire(hostile->canAttack(aircraft), "hostile anti-air cannot target the city aircraft");
        cityRequire(!allied->canAttack(aircraft), "a same-team turret targets its own city aircraft");
        cityRequire(!tank->canAttack(aircraft), "an ordinary ground weapon can hit the city aircraft");
        cityRequire(!aircraft->canAttack(hostile), "a civilian city aircraft can attack");
        cityRequire(!aircraft->canAttackStuff, "a civilian city aircraft carries a weapon");

        const Uint32 hostileID = hostile->getObjectID();
        int ticks = 0, hostileShots = 0;
        size_t previousBullets = bulletList.size();
        for(; ticks < 6000; ++ticks) {
            game->updateGameState();
            cityRequire(game->getObjectManager().getObject(hostileID) != nullptr,
                        "the hostile anti-air was destroyed during the encounter");
            if(bulletList.size() > previousBullets) hostileShots += static_cast<int>(bulletList.size() - previousBullets);
            previousBullets = bulletList.size();
            if((ticks % 1000) == 0) {
                auto* live = dynamic_cast<AirUnit*>(game->getObjectManager().getObject(aircraftID));
                SDL_Log("CITY_AIRCRAFT defence-trace item=%d tick=%d alive=%d hp=%ld at=%d,%d "
                        "turret_target=%d turret_timer=%d shots=%d canattack=%d visible_enemy=%d "
                        "active=%d pickedup=%d respondable=%d",
                        item, ticks, live != nullptr,
                        live ? lround(live->getHealth()) : -1,
                        live ? live->getLocation().x : -1, live ? live->getLocation().y : -1,
                        hostile->getTarget() != nullptr,
                        hostile->weaponTimer, hostileShots,
                        live ? hostile->canAttack(live) : -1,
                        live ? live->isVisible(enemy->getTeamID()) : -1,
                        live ? live->isActive() : -1,
                        live ? live->pickedUp : -1,
                        live ? live->isRespondable() : -1);
            }
            if(game->getObjectManager().getObject(aircraftID) == nullptr) break;
        }
        cityRequire(game->getObjectManager().getObject(aircraftID) == nullptr,
                    "hostile anti-air could not destroy the city aircraft");
        cityRequire(hostileShots > 0, "the aircraft died without being shot at");

        // Clean removal: no tile, no unit list entry, no house count, and the
        // airport slot is free for the next launch.
        cityRequire(!tileReferences(aircraftID), "a destroyed city aircraft left a tile reference");
        cityRequire(liveUnitsOfType(item) == 0, "a destroyed city aircraft is still in the unit list");
        cityRequire(owner->getNumItems(item) == 0, "a destroyed city aircraft is still counted");
        for(int cycle = 0; cycle < 200; ++cycle) game->updateGameState();
        cityRequire(owner->getNumItems(item) == 0, "the dead aircraft count came back");

        record("defence", std::string("item=") + std::to_string(item)
                        + " start_hp=" + std::to_string(startHP)
                        + " shots=" + std::to_string(hostileShots)
                        + " killed_after_cycles=" + std::to_string(ticks)
                        + " same_team_targets=0 ground_weapon_targets=0 leaks=0");
        game.reset();
        currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------
// continuation
// ---------------------------------------------------------------------------
void checkSaveContinuation() {
    auto game = newCityGame(1807);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    game->updateGameState();
    auto* citySim = game->getCitySimulation();
    cityRequire(citySim != nullptr, "city simulation was not created");

    const Coord cityOrigin(18, 10);
    paintCongestedRoads(citySim, owner, cityOrigin, 12);
    const Coord pad = cityOrigin + Coord(6, 6);

    auto* copter = dynamic_cast<AmbientHelicopter*>(owner->createUnit(Unit_AmbientHelicopter, true));
    cityRequire(copter != nullptr, "could not create the helicopter to save");
    copter->deploy(pad);
    copter->setHome(pad);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the airplane to save");
    plane->deploy(Coord(12, 20));
    plane->beginTakeoff();

    const Uint32 copterID = copter->getObjectID();
    const Uint32 planeID = plane->getObjectID();
    // Short enough that the airplane is still inside its three-frame take-off
    // run, so the saved frame is real state and not a settled zero.
    for(int cycle = 0; cycle < 30; ++cycle) game->updateGameState();

    auto* midCopter = dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(copterID));
    auto* midPlane = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(midCopter != nullptr && midPlane != nullptr, "the saved aircraft did not survive the opening cycles");
    const Coord savedHome = midCopter->getHome();
    const Sint32 savedPatrol = midCopter->getPatrolCount();
    const Sint32 savedCooldown = midCopter->getReportCooldown();
    const Uint32 savedReports = midCopter->getReportCount();
    const Sint32 savedTakeoff = midPlane->getTakeoffFrame();
    const Sint32 savedBudget = midPlane->getRemainingTicks();
    cityRequire(savedPatrol < CA::kHelicopterPatrolCount, "the patrol count never advanced before saving");
    cityRequire(savedTakeoff > 0 && savedTakeoff <= CA::kAirplaneFirstTakeoffFrame,
                "the airplane was not mid-take-off when the save was taken");

    const std::string path = gCityAircraftOut + "/city-aircraft-" + gCityAircraftMod + ".dls";
    cityRequire(game->saveGame(path), "could not write the city-aircraft save");

    // The exact state the running game produces next, to compare against.
    auto trace = [&](const AmbientHelicopter* h, const AmbientAirplane* p) {
        std::string out;
        if(h) out += "h " + std::to_string(h->realX.getRawValue()) + ',' + std::to_string(h->realY.getRawValue())
                   + ',' + std::to_string(h->getAngle()) + ',' + std::to_string(h->getPatrolCount())
                   + ',' + std::to_string(h->getReportCooldown()) + ',' + std::to_string(h->getReportCount());
        else out += "h gone";
        if(p) out += " p " + std::to_string(p->realX.getRawValue()) + ',' + std::to_string(p->realY.getRawValue())
                   + ',' + std::to_string(p->getAngle()) + ',' + std::to_string(p->getTakeoffFrame())
                   + ',' + std::to_string(p->getRemainingTicks()) + ',' + std::to_string(p->drawnFrame)
                   // The waypoint is part of the flight now that it can be
                   // outside the map: a reload that dropped it would fly away.
                   + ',' + std::to_string(p->getDestination().x)
                   + ',' + std::to_string(p->getDestination().y);
        else out += " p gone";
        return out;
    };

    std::vector<std::string> expected;
    for(int i = 0; i < 300; ++i) {
        paintCongestedRoads(citySim, owner, cityOrigin, 12);
        game->updateGameState();
        expected.push_back(trace(
            dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(copterID)),
            dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID))));
    }
    game.reset();
    currentGame = nullptr;

    game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    auto* loadedCopter = dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(copterID));
    auto* loadedPlane = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(loadedCopter != nullptr, "the saved helicopter did not come back");
    cityRequire(loadedPlane != nullptr, "the saved airplane did not come back");
    cityRequire(loadedCopter->getHome() == savedHome, "the helicopter lost its home pad");
    cityRequire(loadedCopter->getPatrolCount() == savedPatrol, "the helicopter lost its patrol count");
    cityRequire(loadedCopter->getReportCooldown() == savedCooldown, "the helicopter lost its report cooldown");
    cityRequire(loadedCopter->getReportCount() == savedReports, "the helicopter lost its report tally");
    cityRequire(loadedPlane->getTakeoffFrame() == savedTakeoff, "the airplane lost its take-off frame");
    cityRequire(loadedPlane->drawnFrame == savedTakeoff - 8,
                "the airplane lost its take-off artwork row immediately after loading");
    cityRequire(loadedPlane->getRemainingTicks() == savedBudget, "the airplane lost its flight budget");

    auto* loadedCitySim = game->getCitySimulation();
    cityRequire(loadedCitySim != nullptr, "the loaded game lost its city simulation");
    auto* loadedOwner = game->getHouse(HOUSE_ATREIDES);
    for(int i = 0; i < 300; ++i) {
        paintCongestedRoads(loadedCitySim, loadedOwner, cityOrigin, 12);
        game->updateGameState();
        const std::string got = trace(
            dynamic_cast<AmbientHelicopter*>(game->getObjectManager().getObject(copterID)),
            dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID)));
        if(got != expected[i])
            throw std::runtime_error("city-aircraft probe: save continuation differs at frame "
                                     + std::to_string(i) + ": got [" + got + "] expected ["
                                     + expected[i] + "]");
    }
    record("continuation", "exact_frames=300 patrol=" + std::to_string(savedPatrol)
                         + " takeoff=" + std::to_string(savedTakeoff));
    game.reset();
    currentGame = nullptr;
}

/// A plane following a waypoint outside the map, in its departure phase, has to
/// come back from a save on exactly that route. Nothing new is written: the
/// waypoint lives in the inherited destination fields and the phase is read off
/// the flight budget, so the record is still the 9853 one.
void checkDepartureSaveContinuation() {
    auto game = newCityGame(1809);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    powerHouse(owner);
    game->updateGameState();

    const Coord runway(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2);
    clearGround(runway + Coord(-1, -1), 5, 5);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the departing airplane to save");
    plane->deploy(runway);
    plane->beginTakeoff();
    const Uint32 planeID = plane->getObjectID();
    for(int cycle = 0; cycle < 200; ++cycle) game->updateGameState();

    auto* cruising = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(cruising != nullptr, "the airplane to save did not survive its take-off");
    cruising->remainingTicks = 0;
    // A few more cycles: the flight logic only runs on the original's sprite
    // ticks (one every 3.125 game cycles), so beginDeparture() needs one of
    // those to choose and store the outward route.
    for(int cycle = 0; cycle < 8; ++cycle) game->updateGameState();
    cruising = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(cruising != nullptr, "the departing airplane was removed over the city");
    const Coord savedWaypoint = cruising->getDestination();
    const Sint32 savedBudget = cruising->getRemainingTicks();
    cityRequire(savedBudget == 0, "the saved airplane was not in its departure phase");
    cityRequire(savedWaypoint.isValid()
                && CA::isOutsideMap(savedWaypoint.x, savedWaypoint.y, currentGameMap->getSizeX(),
                                    currentGameMap->getSizeY()),
                "the departing airplane was not following a waypoint outside the map");

    const std::string path = gCityAircraftOut + "/city-aircraft-departure-" + gCityAircraftMod + ".dls";
    cityRequire(game->saveGame(path), "could not write the departure save");

    auto trace = [&](const AmbientAirplane* p) {
        if(p == nullptr) return std::string("gone");
        return std::to_string(p->realX.getRawValue()) + ',' + std::to_string(p->realY.getRawValue())
             + ',' + std::to_string(p->getAngle()) + ',' + std::to_string(p->getRemainingTicks())
             + ',' + std::to_string(p->getDestination().x) + ','
             + std::to_string(p->getDestination().y);
    };

    std::vector<std::string> expected;
    for(int i = 0; i < 300; ++i) {
        game->updateGameState();
        expected.push_back(trace(
            dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID))));
    }
    cityRequire(expected.back() == "gone",
                "the departing airplane did not leave the map within 300 frames");
    game.reset();
    currentGame = nullptr;

    game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    cityRequire(currentGame->getLoadedSavegameVersion() == 9853,
                "the departure save is not a 9853 record");
    auto* loaded = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    cityRequire(loaded != nullptr, "the departing airplane did not come back");
    cityRequire(loaded->getRemainingTicks() == savedBudget, "the departing airplane lost its budget");
    cityRequire(loaded->getDestination() == savedWaypoint,
                "the reloaded airplane lost its waypoint outside the map");

    for(int i = 0; i < 300; ++i) {
        game->updateGameState();
        const std::string got = trace(
            dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID)));
        if(got != expected[i])
            throw std::runtime_error("city-aircraft probe: departure save continuation differs at frame "
                                     + std::to_string(i) + ": got [" + got + "] expected ["
                                     + expected[i] + "]");
    }
    record("continuation", "departure_exact_frames=300 waypoint="
                         + std::to_string(savedWaypoint.x) + ',' + std::to_string(savedWaypoint.y)
                         + " savegame_version=9853");
    game.reset();
    currentGame = nullptr;
}

} // namespace

int runCityAircraftProbe() {
    gCityAircraftMod = std::getenv("CITY_AIRCRAFT_PROBE_MOD");
    gCityAircraftOut = std::getenv("CITY_AIRCRAFT_PROBE_OUT");
    cityRequire(ModManager::instance().setActiveMod(gCityAircraftMod), "missing city-aircraft mod");
    settings.general.playerName = "City tester";
    std::ifstream map(PROBE_MAP_PATH);
    gMapData.assign((std::istreambuf_iterator<char>(map)), std::istreambuf_iterator<char>());
    cityRequire(!gMapData.empty(), "fixture map is empty");

    gCityAircraftCsv.open(gCityAircraftOut + "/" + gCityAircraftMod + ".csv");
    gCityAircraftCsv << "mod,phase,detail\n";

    checkOriginalArtLoaded();
    checkOriginalMotion();
    checkSparseCongestedRoad();
    checkAirportLaunch();
    checkUnpoweredAirportLaunchesNothing();
    checkTrafficPatrolAndReports();
    checkAirplaneFlight();
    checkOutsideWaypointHandling();
    checkDepartureThroughEachEdge();
    checkEnemyAirDefence();
    checkSaveContinuation();
    checkDepartureSaveContinuation();

    gCityAircraftCsv.close();
    SDL_Log("CITY_AIRCRAFT_PROBE_PASS: %s", gCityAircraftMod.c_str());
    return -1;
}
