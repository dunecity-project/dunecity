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
#include <DynastyProjectile.h>
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
/// A 128x128 map for the straight-flight phase: the 51x31 fixture is crossed in
/// a few seconds, too short for legs long enough to show a zig-zag.
std::string gLargeMapData;

std::unique_ptr<Game> newCityGame(int seed, const std::string& mapData = gMapData) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("city-aircraft.ini", mapData, "City aircraft", true, settings.gameOptions);
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
                               ArtSpec{ObjPic_CityAirplane, Unit_AmbientAirplane, 3 * D2_TILESIZE, CA::kAirplaneSheetRows}}) {
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
    record("art", "helicopter=8x1x32 airplane=8x5x48 zoom_levels="
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

        // Hold the aircraft still so the kill is a damage test, not a chase. The
        // airplane crashes with a Death Hand's blast, which the turret will not
        // set off over itself (AmbientAirplane::crashEndangers): the blast centres
        // reach two tiles, so it is shot down four tiles out, inside the turret's
        // view range and clear of the blast.
        game->objectData.data[item][HOUSE_ATREIDES].maxspeed = 0;
        auto* aircraft = dynamic_cast<AirUnit*>(owner->createUnit(item, true));
        cityRequire(aircraft != nullptr, "could not create the city aircraft");
        aircraft->deploy(site + Coord(item == Unit_AmbientAirplane ? 4 : 3, 0));
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
    cityRequire(loadedPlane->drawnFrame == CA::airplaneTakeoffRow(savedTakeoff),
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
/// the flight budget, so the aircraft record is the one 9853 introduced.
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
    cityRequire(currentGame->getLoadedSavegameVersion() == SAVEGAMEVERSION,
                "the departure save is not a current record");
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
                         + " savegame_version=" + std::to_string(SAVEGAMEVERSION));
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// straight flight
// ---------------------------------------------------------------------------

/// Stefan, 2026-10-22: "stop the plane from turning eratically it should move in
/// straight lines mostly not turn left and right all the time".
///
/// Heading changes are read off the engine's own `angle` (eighths of a turn).
/// A change is a cycle on which the heading differs from the cycle before; its
/// sign is left (counter-clockwise) or right. A reversal is a change whose sign
/// is opposite to the previous one: flying to one fixed waypoint, a plane that
/// does not zig-zag only ever turns one way. An oscillation is a reversal less
/// than kOscillationTiles after the previous change - the left-right-left of the
/// zig-zag, as opposed to a new waypoint that happens to lie the other way.
constexpr double kOscillationTiles = 4.0;
/// Leg budget: the start-of-leg correction plus at most two more per leg. The
/// zig-zag turned every five sprite ticks (2.5 tiles), 0.4 per tile.
constexpr double kMaxLegChangesPerTile = 0.05;
constexpr int kMaxLegChanges = 3;
/// Whole flight, take-off to leaving the map, waypoints drawn by the engine.
constexpr double kMaxFlightChangesPerTile = 0.15;
/// One game minute at GAMESPEED_DEFAULT (16 ms per cycle).
constexpr int kCyclesPerGameMinute = 60000 / GAMESPEED_DEFAULT;

struct TurnLog {
    int changes = 0;
    int reversals = 0;
    int oscillations = 0;
    double tiles = 0.0;
    int cycles = 0;
    int lastSign = 0;
    double tilesAtLastChange = -1e9;
    bool primed = false;
    FixPoint lastAngle;

    void observe(const AmbientAirplane* plane, double stepTiles) {
        tiles += stepTiles;
        ++cycles;
        if(!primed) { lastAngle = plane->angle; primed = true; return; }
        if(plane->angle == lastAngle) return;
        FixPoint diff = plane->angle - lastAngle;
        while(diff > 4) diff -= 8;
        while(diff <= -4) diff += 8;
        const int sign = diff > 0 ? 1 : -1;
        ++changes;
        if(lastSign != 0 && sign != lastSign) {
            ++reversals;
            if(tiles - tilesAtLastChange < kOscillationTiles) ++oscillations;
        }
        lastSign = sign;
        tilesAtLastChange = tiles;
        lastAngle = plane->angle;
    }
    void add(const TurnLog& other) {
        changes += other.changes;
        reversals += other.reversals;
        oscillations += other.oscillations;
        tiles += other.tiles;
        cycles += other.cycles;
    }
    double changesPerTile() const { return tiles > 0 ? changes / tiles : 0.0; }
    double changesPerMinute() const {
        return cycles > 0 ? changes * static_cast<double>(kCyclesPerGameMinute) / cycles : 0.0;
    }
};

/// One row per cycle, for tests/units/city-airplane-frame-strip.py: position,
/// heading and the sheet cell the renderer draws (column drawnAngle, row
/// drawnFrame), so a frame strip shows exactly what the player saw.
void traceRow(std::ofstream& out, int leg, int cycle, const AmbientAirplane* p) {
    out << leg << ',' << cycle << ',' << lround(p->realX) << ',' << lround(p->realY) << ','
        << p->angle.toDouble() << ',' << static_cast<int>(p->drawnAngle) << ',' << p->drawnFrame << ','
        << p->getTakeoffFrame() << ',' << p->getDestination().x << ',' << p->getDestination().y << '\n';
}

std::ofstream openTrace(const std::string& name) {
    std::ofstream trace(gCityAircraftOut + "/" + name + "-" + gCityAircraftMod + ".csv");
    trace << "leg,cycle,x,y,angle,drawn_angle,drawn_frame,takeoff_frame,dest_x,dest_y\n";
    return trace;
}

/// 48 legs from the map centre to fixed waypoints 7.5 degrees apart. Each starts
/// on the original eight-way bearing (what a plane that has just picked the
/// waypoint flies), and ends when the plane arrives and draws its next one.
void checkStraightFlightLegs() {
    auto game = newCityGame(1820, gLargeMapData);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    game->updateGameState();

    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    cityRequire(w >= 96 && h >= 96, "the straight-flight map is too small for long legs");
    const Coord centre(w / 2, h / 2);
    const int radius = std::min(w, h) / 2 - 8;
    constexpr int kBearings = 48;

    std::ofstream trace = openTrace("straight-legs");
    TurnLog all;
    int arrived = 0, legsWithReversal = 0, worstLeg = 0, worstLegChanges = 0;
    for(int leg = 0; leg < kBearings; ++leg) {
        const double theta = 2.0 * M_PI * leg / kBearings;
        const Coord target(centre.x + static_cast<int>(std::lround(radius * std::cos(theta))),
                           centre.y - static_cast<int>(std::lround(radius * std::sin(theta))));
        auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
        cityRequire(plane != nullptr, "could not create the straight-leg airplane");
        plane->deploy(centre);
        plane->setVisible(VIS_ALL, true);
        plane->setFlightDestination(target);
        const Coord from = plane->micropolisPosition(), to = plane->micropolisDestination();
        const ANGLETYPE start = CA::directionToAngle(CA::getDir(from.x, from.y, to.x, to.y).direction);
        plane->angle = FixPoint(static_cast<int>(start));
        plane->drawnAngle = static_cast<Sint8>(start);
        const Uint32 planeID = plane->getObjectID();

        TurnLog log;
        long prevX = lround(plane->realX), prevY = lround(plane->realY);
        bool reached = false;
        for(int cycle = 0; cycle < 2000; ++cycle) {
            game->updateGameState();
            auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
            cityRequire(live != nullptr, "a straight-leg airplane vanished over the map");
            if(live->getDestination() != target) { reached = true; break; }
            const long x = lround(live->realX), y = lround(live->realY);
            log.observe(live, std::hypot(static_cast<double>(x - prevX), static_cast<double>(y - prevY)) / TILESIZE);
            prevX = x;
            prevY = y;
            traceRow(trace, leg, cycle, live);
        }
        if(reached) ++arrived;
        if(log.reversals > 0) ++legsWithReversal;
        if(log.changes > worstLegChanges) { worstLegChanges = log.changes; worstLeg = leg; }
        all.add(log);
        SDL_Log("CITY_AIRCRAFT leg=%d bearing_deg=%.1f target=%d,%d tiles=%.1f changes=%d reversals=%d arrived=%d",
                leg, 360.0 * leg / kBearings, target.x, target.y, log.tiles, log.changes, log.reversals, reached);

        if(auto* live = game->getObjectManager().getObject(planeID)) {
            live->setVisible(VIS_ALL, false);
            live->destroy();
        }
    }

    record("straight", "legs=" + std::to_string(kBearings) + " arrived=" + std::to_string(arrived)
                     + " tiles=" + std::to_string(static_cast<long>(all.tiles))
                     + " heading_changes=" + std::to_string(all.changes)
                     + " changes_per_tile=" + std::to_string(all.changesPerTile())
                     + " changes_per_game_minute=" + std::to_string(all.changesPerMinute())
                     + " reversals=" + std::to_string(all.reversals)
                     + " legs_with_reversal=" + std::to_string(legsWithReversal)
                     + " worst_leg=" + std::to_string(worstLeg)
                     + " worst_leg_changes=" + std::to_string(worstLegChanges));
    cityRequire(arrived == kBearings, "a straight-flight leg never reached its waypoint");
    cityRequire(legsWithReversal == 0,
                std::to_string(legsWithReversal) + " legs turned left and right on the way to one waypoint");
    cityRequire(worstLegChanges <= kMaxLegChanges,
                "leg " + std::to_string(worstLeg) + " changed heading " + std::to_string(worstLegChanges)
                + " times on the way to one waypoint");
    cityRequire(all.changesPerTile() <= kMaxLegChangesPerTile,
                "the plane changes heading " + std::to_string(all.changesPerTile())
                + " times per tile flown towards a fixed waypoint");
    game.reset();
    currentGame = nullptr;
}

/// A whole natural flight on the large map: the original take-off, the engine's
/// own random waypoints, the departure. Writes the trace the frame strips use.
void checkStraightNaturalFlight() {
    auto game = newCityGame(1821, gLargeMapData);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    game->updateGameState();

    const Coord runway(currentGameMap->getSizeX() / 4, currentGameMap->getSizeY() / 2);
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the natural-flight airplane");
    plane->deploy(runway);
    plane->setVisible(VIS_ALL, true);
    plane->beginTakeoff();
    const Uint32 planeID = plane->getObjectID();

    std::ofstream trace = openTrace("flight-path");
    TurnLog log;
    std::set<std::pair<int, int>> cells;
    int waypoints = 0;
    Coord lastDestination = plane->getDestination();
    long prevX = lround(plane->realX), prevY = lround(plane->realY);
    for(int cycle = 0; cycle < 26000; ++cycle) {
        game->updateGameState();
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) break;
        traceRow(trace, 0, cycle, live);
        cells.insert({live->drawnAngle, live->drawnFrame});
        if(live->getDestination() != lastDestination) {
            lastDestination = live->getDestination();
            ++waypoints;
        }
        const long x = lround(live->realX), y = lround(live->realY);
        if(live->getTakeoffFrame() == 0)
            log.observe(live, std::hypot(static_cast<double>(x - prevX), static_cast<double>(y - prevY)) / TILESIZE);
        prevX = x;
        prevY = y;
    }
    cityRequire(game->getObjectManager().getObject(planeID) == nullptr,
                "the natural flight did not end within the cycle budget");

    record("straight", "natural_flight tiles=" + std::to_string(static_cast<long>(log.tiles))
                     + " cycles=" + std::to_string(log.cycles)
                     + " waypoints=" + std::to_string(waypoints)
                     + " heading_changes=" + std::to_string(log.changes)
                     + " changes_per_tile=" + std::to_string(log.changesPerTile())
                     + " changes_per_game_minute=" + std::to_string(log.changesPerMinute())
                     + " reversals=" + std::to_string(log.reversals)
                     + " oscillations=" + std::to_string(log.oscillations)
                     + " sheet_cells_drawn=" + std::to_string(cells.size()));
    cityRequire(log.oscillations == 0,
                std::to_string(log.oscillations) + " left-right oscillations in one natural flight");
    cityRequire(log.changesPerTile() <= kMaxFlightChangesPerTile,
                "a natural flight changes heading " + std::to_string(log.changesPerTile()) + " times per tile");
    game.reset();
    currentGame = nullptr;
}

// ---------------------------------------------------------------------------
// crash blast
// ---------------------------------------------------------------------------

/// Atreides witnesses around the crash point. Every one is at least two tiles
/// from it, outside the one-tile splash of the turret rockets that bring the
/// plane down, so what reaches them is the crash blast alone; the outer ring is
/// beyond the Death Hand's reach and has to come through untouched.
struct BlastWitness { int dx, dy, itemID; };
const BlastWitness kBlastWitnesses[] = {
    {2, 0, Unit_Tank},  {0, 2, Unit_Tank},   {-2, 0, Unit_Tank},  {0, -2, Unit_Tank},
    {2, 2, Unit_Trike}, {-2, -2, Unit_Quad}, {3, 0, Unit_Tank},   {0, 3, Unit_Trike},
    {-3, 1, Unit_Quad}, {1, -3, Unit_Tank},  {3, 3, Unit_Trike},  {4, 0, Unit_Tank},
    {0, -4, Unit_Quad}, {-4, -1, Unit_Tank}, {2, -2, Structure_Wall}, {-2, 2, Structure_Wall},
    {6, 0, Unit_Tank},  {0, 6, Unit_Quad},   {0, -6, Structure_Wall},
};

struct BlastOutcome {
    std::vector<int> health;   ///< per witness; 0 once destroyed
    std::vector<int> full;     ///< per witness, as placed
    int shooterUnitKills = 0;
    int shooterStructureKills = 0;
    Coord centre;              ///< world coordinates of the detonation
    int newExplosions = 0;
};

/// One fixture, two ways: a hostile (Harkonnen) rocket turret shoots an
/// Atreides city airplane down at `site`, or - same map, same witnesses, no
/// plane - a Death Hand impact (Bullet_LargeRocket with the Palace's damage,
/// fired by the same turret) lands on `deathHandAt`. The witnesses are the
/// plane owner's, or with `nativeWitnesses` the Natives house's (a sandworm
/// witness gets a sand tile of its own; the rock around keeps it there).
BlastOutcome runBlastFixture(bool shootPlane, const Coord& deathHandAt,
                             const std::vector<BlastWitness>& witnesses
                                 = {std::begin(kBlastWitnesses), std::end(kBlastWitnesses)},
                             bool nativeWitnesses = false) {
    auto game = newCityGame(1830);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    powerHouse(owner);
    powerHouse(enemy);
    game->updateGameState();

    const Coord site(30, 15);
    clearGround(site + Coord(-8, -7), 16, 15);
    auto* turret = dynamic_cast<RocketTurret*>(
        enemy->placeStructure(NONE_ID, Structure_RocketTurret, site.x - 7, site.y, true, true));
    cityRequire(turret != nullptr, "could not place the blast-test turret");
    turret->setVisible(VIS_ALL, true);
    turret->attackMode = STOP;   // shoots only what it is told to

    BlastOutcome outcome;
    std::vector<Uint32> witnessIDs;
    House* const witnessOwner = nativeWitnesses ? game->getOrCreateNativesHouse() : owner;
    for(const BlastWitness& witness : witnesses) {
        const Coord at = site + Coord(witness.dx, witness.dy);
        if(witness.itemID == Unit_Sandworm) currentGameMap->getTile(at.x, at.y)->setType(Terrain_Sand);
        ObjectBase* object = isStructure(witness.itemID)
            ? static_cast<ObjectBase*>(witnessOwner->placeStructure(NONE_ID, witness.itemID, at.x, at.y, true, true))
            : static_cast<ObjectBase*>(witnessOwner->placeUnit(witness.itemID, at.x, at.y, true));
        cityRequire(object != nullptr && object->getOwner() == witnessOwner, "could not place a blast witness");
        object->setVisible(VIS_ALL, true);
        if(auto* unit = dynamic_cast<UnitBase*>(object)) unit->doSetAttackMode(STOP);
        witnessIDs.push_back(object->getObjectID());
        outcome.full.push_back(lround(object->getHealth()));
    }

    const int unitKillsBefore = enemy->getNumDestroyedUnits();
    const int structureKillsBefore = enemy->getNumDestroyedStructures();
    size_t explosionsBefore = 0;
    if(shootPlane) {
        game->objectData.data[Unit_AmbientAirplane][HOUSE_ATREIDES].maxspeed = 0;
        auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
        cityRequire(plane != nullptr, "could not create the plane to shoot down");
        plane->deploy(site);
        plane->currentMaxSpeed = 0;
        plane->setVisible(VIS_ALL, true);
        const Uint32 planeID = plane->getObjectID();
        cityRequire(turret->canAttack(plane), "the hostile turret may not shoot the plane down");
        turret->doAttackObject(plane);
        bool down = false;
        for(int cycle = 0; cycle < 4000 && !down; ++cycle) {
            auto* live = game->getObjectManager().getObject(planeID);
            outcome.centre = live->getCenterPoint();
            explosionsBefore = game->getExplosionList().size();
            game->updateGameState();
            live = game->getObjectManager().getObject(planeID);
            down = live == nullptr || live->getHealth() <= 0;
        }
        cityRequire(down, "the hostile turret could not shoot the plane down");
    } else {
        outcome.centre = deathHandAt;
        explosionsBefore = game->getExplosionList().size();
        Coord at = deathHandAt;
        auto* missile = new Bullet(turret->getObjectID(), &at, &at, Bullet_LargeRocket,
                                   DynastyProjectile::deathHandDamage, false, nullptr);
        bulletList.push_back(missile);
        missile->destroy();   // Bullet::destroy() is the Death Hand impact
    }
    outcome.newExplosions = static_cast<int>(game->getExplosionList().size() - explosionsBefore);
    for(int cycle = 0; cycle < 10; ++cycle) game->updateGameState();

    for(Uint32 id : witnessIDs) {
        const ObjectBase* object = game->getObjectManager().getObject(id);
        outcome.health.push_back(object ? lround(object->getHealth()) : 0);
    }
    outcome.shooterUnitKills = enemy->getNumDestroyedUnits() - unitKillsBefore;
    outcome.shooterStructureKills = enemy->getNumDestroyedStructures() - structureKillsBefore;
    game.reset();
    currentGame = nullptr;
    return outcome;
}

std::string joinHealth(const std::vector<int>& health) {
    std::string text;
    for(size_t i = 0; i < health.size(); ++i) text += (i ? "/" : "") + std::to_string(health[i]);
    return text;
}

void checkShotDownPlaneDetonates() {
    const BlastOutcome crash = runBlastFixture(true, Coord());
    const BlastOutcome deathHand = runBlastFixture(false, crash.centre);
    record("crash", "crash_centre=" + std::to_string(crash.centre.x) + ',' + std::to_string(crash.centre.y)
                  + " crash_health=" + joinHealth(crash.health)
                  + " death_hand_health=" + joinHealth(deathHand.health)
                  + " crash_kills=" + std::to_string(crash.shooterUnitKills) + '+'
                  + std::to_string(crash.shooterStructureKills)
                  + " death_hand_kills=" + std::to_string(deathHand.shooterUnitKills) + '+'
                  + std::to_string(deathHand.shooterStructureKills)
                  + " crash_explosions=" + std::to_string(crash.newExplosions)
                  + " death_hand_explosions=" + std::to_string(deathHand.newExplosions));

    int damaged = 0;
    for(size_t i = 0; i < deathHand.health.size(); ++i) {
        const BlastWitness& witness = kBlastWitnesses[i];
        const std::string where = "witness " + std::to_string(i) + " at offset ("
            + std::to_string(witness.dx) + ',' + std::to_string(witness.dy) + ")";
        cityRequire(crash.health[i] == deathHand.health[i],
                    where + " has " + std::to_string(crash.health[i]) + " HP after the crash but "
                    + std::to_string(deathHand.health[i]) + " after a Death Hand on the same spot");
        if(std::max(std::abs(witness.dx), std::abs(witness.dy)) >= 6)
            cityRequire(deathHand.health[i] == deathHand.full[i],
                        where + " is outside the Death Hand's reach but was damaged");
        else if(deathHand.health[i] < deathHand.full[i])
            ++damaged;
    }
    cityRequire(damaged > 0, "the Death Hand reference damaged nothing; the fixture proves nothing");
    // The shooter is credited exactly as a Death Hand's owner is, plus the plane.
    cityRequire(crash.shooterUnitKills == deathHand.shooterUnitKills + 1,
                "the shooter's unit kills differ from a Death Hand's");
    cityRequire(crash.shooterStructureKills == deathHand.shooterStructureKills,
                "the shooter's structure kills differ from a Death Hand's");
    // Seventeen blast centres, one large explosion each (all inside the map here).
    cityRequire(deathHand.newExplosions == 17, "the Death Hand reference did not explode at 17 centres");
    cityRequire(crash.newExplosions >= deathHand.newExplosions,
                "the crash did not produce the Death Hand's explosions");
}

/// Release 1.0.851 cross-check of the city airplane against the Natives house:
/// a plane shot down over Natives worms and crime troopers hits them exactly as
/// a Death Hand on the same spot does, and everything it destroys is credited
/// to the house that brought the plane down (here Harkonnen, not the Natives
/// and not the plane's owner).
const std::vector<BlastWitness> kNativesBlastWitnesses = {
    {2, 0, Unit_Trooper},   {0, -2, Unit_Trooper},  {-2, 1, Unit_Trooper}, {1, 3, Unit_Trooper},
    {0, 2, Unit_Sandworm},  {-3, -1, Unit_Sandworm},
    {6, 0, Unit_Trooper},   {0, 6, Unit_Sandworm},
};

void checkShotDownPlaneOverNatives() {
    const BlastOutcome crash = runBlastFixture(true, Coord(), kNativesBlastWitnesses, true);
    const BlastOutcome deathHand = runBlastFixture(false, crash.centre, kNativesBlastWitnesses, true);
    record("crash", "natives_crash_health=" + joinHealth(crash.health)
                  + " natives_death_hand_health=" + joinHealth(deathHand.health)
                  + " natives_full=" + joinHealth(crash.full)
                  + " natives_crash_kills=" + std::to_string(crash.shooterUnitKills)
                  + " natives_death_hand_kills=" + std::to_string(deathHand.shooterUnitKills));
    for(size_t i = 0; i < kNativesBlastWitnesses.size(); ++i) {
        const BlastWitness& witness = kNativesBlastWitnesses[i];
        const std::string where = "Natives witness " + std::to_string(i) + " at offset ("
            + std::to_string(witness.dx) + ',' + std::to_string(witness.dy) + ")";
        cityRequire(crash.health[i] == deathHand.health[i],
                    where + " has " + std::to_string(crash.health[i]) + " HP after the crash but "
                    + std::to_string(deathHand.health[i]) + " after a Death Hand on the same spot");
        if(std::max(std::abs(witness.dx), std::abs(witness.dy)) >= 6)
            cityRequire(crash.health[i] == crash.full[i], where + " is outside the blast's reach but was damaged");
        else
            cityRequire(crash.health[i] < crash.full[i], where + " is inside the blast's reach but took no damage");
    }
    cityRequire(deathHand.shooterUnitKills > 0, "the Death Hand reference killed no Natives unit; the fixture proves nothing");
    cityRequire(crash.shooterUnitKills == deathHand.shooterUnitKills + 1,
                "the shooter is not credited with the Natives units the crash destroyed (plus the plane)");
}

/// A plane that simply flies off the edge ends without any blast.
void checkDepartingPlaneDoesNotDetonate() {
    auto game = newCityGame(1831);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    powerHouse(owner);
    game->updateGameState();

    const int w = currentGameMap->getSizeX();
    const Coord start(w - 4, 15);
    clearGround(start + Coord(-4, -4), 8, 9);
    std::vector<Uint32> witnessIDs;
    std::vector<int> fullHealth;
    for(const Coord offset : {Coord(0, 2), Coord(0, -2), Coord(2, 0), Coord(-2, 1), Coord(1, 3)}) {
        auto* unit = owner->placeUnit(Unit_Tank, start.x + offset.x, start.y + offset.y, true);
        cityRequire(unit != nullptr, "could not place a departure witness");
        unit->doSetAttackMode(STOP);
        witnessIDs.push_back(unit->getObjectID());
        fullHealth.push_back(lround(unit->getHealth()));
    }

    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the departing airplane");
    plane->deploy(start);
    plane->setVisible(VIS_ALL, true);
    plane->remainingTicks = 0;
    const Uint32 planeID = plane->getObjectID();
    const int killsBefore = enemy->getNumDestroyedUnits() + owner->getNumDestroyedUnits();
    bool gone = false;
    int explosionsAtExit = -1;
    for(int cycle = 0; cycle < 2000 && !gone; ++cycle) {
        const size_t before = game->getExplosionList().size();
        game->updateGameState();
        gone = game->getObjectManager().getObject(planeID) == nullptr;
        if(gone) explosionsAtExit = static_cast<int>(game->getExplosionList().size() - before);
    }
    cityRequire(gone, "the departing plane never left the map");
    for(int cycle = 0; cycle < 10; ++cycle) game->updateGameState();
    for(size_t i = 0; i < witnessIDs.size(); ++i) {
        const ObjectBase* object = game->getObjectManager().getObject(witnessIDs[i]);
        cityRequire(object != nullptr && lround(object->getHealth()) == fullHealth[i],
                    "a plane leaving the map damaged a unit under its exit path");
    }
    cityRequire(explosionsAtExit == 0, "a plane leaving the map exploded");
    cityRequire(enemy->getNumDestroyedUnits() + owner->getNumDestroyedUnits() == killsBefore,
                "a plane leaving the map was counted as a kill");
    record("crash", "departure_blast=none witnesses_untouched=" + std::to_string(witnessIDs.size()));
    game.reset();
    currentGame = nullptr;
}

/// Anti-air holds its fire while bringing the plane down would set the crash
/// blast off over its own side - where the plane is, and where it will be by
/// the time a rocket reaches it.
void checkAntiAirSparesOwnSide() {
    auto game = newCityGame(1832);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    powerHouse(owner);
    powerHouse(enemy);
    game->updateGameState();

    const Coord turretAt(14, 15);
    clearGround(Coord(10, 4), 22, 24);
    auto* turret = dynamic_cast<RocketTurret*>(
        enemy->placeStructure(NONE_ID, Structure_RocketTurret, turretAt.x, turretAt.y, true, true));
    cityRequire(turret != nullptr, "could not place the anti-air turret");
    turret->setVisible(VIS_ALL, true);
    auto* launcher = enemy->placeUnit(Unit_Launcher, turretAt.x + 6, turretAt.y - 7, true);
    cityRequire(launcher != nullptr, "could not place the anti-air launcher");
    launcher->doSetAttackMode(STOP);
    launcher->setVisible(VIS_ALL, true);

    const auto hover = [&](const Coord& at, ANGLETYPE heading, bool moving) {
        auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
        cityRequire(plane != nullptr, "could not create the anti-air test plane");
        plane->deploy(at);
        plane->setVisible(VIS_ALL, true);
        plane->angle = FixPoint(static_cast<int>(heading));
        plane->drawnAngle = static_cast<Sint8>(heading);
        plane->currentMaxSpeed = moving ? game->objectData.data[Unit_AmbientAirplane][HOUSE_ATREIDES].maxspeed : 0;
        return plane;
    };

    // Clear sky six tiles east of the turret: fair game.
    auto* clear = hover(turretAt + Coord(6, 0), RIGHT, false);
    cityRequire(turret->canAttack(clear), "the turret may not shoot a plane over open ground");
    cityRequire(launcher->canAttack(clear), "the launcher may not shoot a plane over open ground");
    // A Harkonnen wall three tiles east of it is beyond every blast centre: still fair game.
    auto* farWall = enemy->placeStructure(NONE_ID, Structure_Wall, turretAt.x + 9, turretAt.y, true, true);
    cityRequire(farWall != nullptr, "could not place the out-of-reach wall");
    cityRequire(turret->canAttack(clear), "the turret holds fire over a wall the blast cannot reach");
    // A Harkonnen wall two tiles east, under the blast's (128, 0) centre: it would land on its own side.
    auto* wall = enemy->placeStructure(NONE_ID, Structure_Wall, turretAt.x + 8, turretAt.y, true, true);
    cityRequire(wall != nullptr, "could not place the own-side wall");
    cityRequire(!turret->canAttack(clear), "the turret shoots a plane down over its own wall");
    cityRequire(!launcher->canAttack(clear), "the launcher shoots a plane down over its own wall");
    // The plane's owner may still be hit: that is the point of shooting it there.
    auto* ownWall = owner->placeStructure(NONE_ID, Structure_Wall, turretAt.x + 6, turretAt.y + 9, true, true);
    cityRequire(ownWall != nullptr, "could not place the plane owner's wall");
    auto* overOwner = hover(turretAt + Coord(5, 8), RIGHT, false);
    cityRequire(turret->canAttack(overOwner), "the turret spares a plane over its owner's own side");

    // In flight: seven tiles north, flying south straight at the turret, the
    // plane is over the turret by the time a rocket gets there. Flying north it
    // is not.
    auto* inbound = hover(turretAt + Coord(0, -7), DOWN, true);
    cityRequire(!turret->canAttack(inbound), "the turret shoots a plane that is flying onto it");
    auto* outbound = hover(turretAt + Coord(0, -7), UP, true);
    cityRequire(turret->canAttack(outbound), "the turret spares a plane that is flying away");
    record("crash", "anti_air_spares_own_side=1 open_ground=fire wall_3_tiles=fire wall_2_tiles=hold"
                    " owner_side=fire inbound=hold outbound=fire");
    game.reset();
    currentGame = nullptr;
}

/// Release 1.0.851 cross-check: the hold-fire guard never counts Natives worms
/// or crime troopers as anybody's own side. A turret fires over them, a Natives
/// trooper fires over its own house's units, and a team-0 house (the anonymous
/// owner a custom map creates for an unlisted house; it shared team 0 with the
/// Natives before save 9873) fires over them too - while still holding over its own wall.
void checkAntiAirIgnoresNatives() {
    auto game = newCityGame(1833);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    House* natives = game->getOrCreateNativesHouse();
    powerHouse(owner);
    powerHouse(enemy);
    game->updateGameState();

    const Coord turretAt(14, 15);
    clearGround(Coord(10, 4), 22, 24);
    auto* turret = dynamic_cast<RocketTurret*>(
        enemy->placeStructure(NONE_ID, Structure_RocketTurret, turretAt.x, turretAt.y, true, true));
    cityRequire(turret != nullptr, "could not place the anti-air turret");
    turret->setVisible(VIS_ALL, true);

    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    cityRequire(plane != nullptr, "could not create the anti-air test plane");
    plane->deploy(turretAt + Coord(6, 0));
    plane->setVisible(VIS_ALL, true);
    plane->angle = FixPoint(static_cast<int>(RIGHT));
    plane->drawnAngle = static_cast<Sint8>(RIGHT);
    plane->currentMaxSpeed = 0;
    cityRequire(turret->canAttack(plane), "the turret may not shoot a plane over open ground");

    // Natives units under the blast: a trooper under the (128, 0) centre, a worm beside the plane.
    const auto placeNative = [&](int itemID, const Coord& at) {
        if(itemID == Unit_Sandworm) currentGameMap->getTile(at.x, at.y)->setType(Terrain_Sand);
        UnitBase* unit = natives->placeUnit(itemID, at.x, at.y, true);
        cityRequire(unit != nullptr && unit->getOwner() == natives, "could not place a Natives unit");
        unit->doSetAttackMode(STOP);
        unit->setVisible(VIS_ALL, true);
        return unit;
    };
    placeNative(Unit_Trooper, turretAt + Coord(8, 0));
    placeNative(Unit_Sandworm, turretAt + Coord(6, 1));
    cityRequire(AmbientAirplane::crashEndangers(*turret, *plane) == false && turret->canAttack(plane),
                "the turret holds fire over Natives units");

    // A Natives crime trooper does not spare its own house's worms and troopers either.
    auto* nativeShooter = placeNative(Unit_Trooper, turretAt + Coord(4, -4));
    cityRequire(nativeShooter->canAttack(plane), "a Natives trooper holds fire over Natives units");

    // A team-0 house (the Natives' team before 9873) is not their side either.
    enemy->teamID = 0;
    cityRequire(turret->canAttack(plane), "a team-0 house's turret holds fire over Natives units");
    auto* wall = enemy->placeStructure(NONE_ID, Structure_Wall, turretAt.x + 6, turretAt.y - 2, true, true);
    cityRequire(wall != nullptr, "could not place the team-0 house's wall");
    cityRequire(!turret->canAttack(plane), "a team-0 house's turret shoots a plane down over its own wall");
    record("crash", "anti_air_natives=1 turret_over_natives=fire natives_over_natives=fire"
                    " team0_over_natives=fire team0_own_wall=hold");
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
    std::ifstream largeMap(PROBE_LARGE_MAP_PATH);
    gLargeMapData.assign((std::istreambuf_iterator<char>(largeMap)), std::istreambuf_iterator<char>());
    cityRequire(!gLargeMapData.empty(), "large fixture map is empty");

    gCityAircraftCsv.open(gCityAircraftOut + "/" + gCityAircraftMod + ".csv");
    gCityAircraftCsv << "mod,phase,detail\n";

    // CITY_AIRCRAFT_PROBE_PHASES=a,b runs only those phases (diagnosis); unset runs all.
    const char* only = std::getenv("CITY_AIRCRAFT_PROBE_PHASES");
    const std::string selected = only ? std::string(",") + only + "," : std::string();
    const auto phase = [&](const char* name, void (*check)()) {
        if(selected.empty() || selected.find(std::string(",") + name + ",") != std::string::npos) check();
    };
    phase("art", checkOriginalArtLoaded);
    phase("motion", checkOriginalMotion);
    phase("sparse", checkSparseCongestedRoad);
    phase("launch", checkAirportLaunch);
    phase("unpowered", checkUnpoweredAirportLaunchesNothing);
    phase("patrol", checkTrafficPatrolAndReports);
    phase("flight", checkAirplaneFlight);
    phase("waypoints", checkOutsideWaypointHandling);
    phase("departure", checkDepartureThroughEachEdge);
    phase("straight_legs", checkStraightFlightLegs);
    phase("straight_flight", checkStraightNaturalFlight);
    phase("defence", checkEnemyAirDefence);
    phase("crash", checkShotDownPlaneDetonates);
    phase("crash_departure", checkDepartingPlaneDoesNotDetonate);
    phase("anti_air", checkAntiAirSparesOwnSide);
    phase("crash_natives", checkShotDownPlaneOverNatives);
    phase("anti_air_natives", checkAntiAirIgnoresNatives);
    phase("continuation", checkSaveContinuation);
    phase("departure_continuation", checkDepartureSaveContinuation);

    gCityAircraftCsv.close();
    SDL_Log("CITY_AIRCRAFT_PROBE_PASS: %s", gCityAircraftMod.c_str());
    return -1;
}
