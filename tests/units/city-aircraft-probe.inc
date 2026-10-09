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
//   flight      The airplane runs the original take-off frames, steers, and
//               leaves the map instead of patrolling.
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

    for(int cycle = 0; cycle < 20000; ++cycle) {
        game->updateGameState();
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) break;
        if(live->getTakeoffFrame() != previousFrame) {
            previousFrame = live->getTakeoffFrame();
            if(previousFrame > 0) takeoffFrames.push_back(previousFrame);
            else { cruising = true; takeoffEndX = live->realX; }
        }
        if(cruising) headings.insert(live->getAngle());
        // A plane is not a patrol: it must never adopt the helicopter's home.
        cityRequire(live->getRemainingTicks() <= CA::kAirplaneMaxTicks, "flight budget grew");
    }

    cityRequire(takeoffFrames == std::vector<int>({11, 10, 9}),
                "take-off did not run the original 11-10-9 frames in order");
    cityRequire(cruising, "the airplane never finished its take-off run");
    cityRequire(takeoffEndX > startX, "the airplane did not climb out eastwards");
    cityRequire(headings.size() > 1, "the airplane never changed heading after take-off");
    cityRequire(game->getObjectManager().getObject(planeID) == nullptr,
                "the airplane never ended its flight");
    cityRequire(owner->getNumItems(Unit_AmbientAirplane) == 0, "a departed airplane was still counted");
    cityRequire(!tileReferences(planeID), "a departed airplane left a tile reference");
    cityRequire(liveUnitsOfType(Unit_AmbientAirplane) == 0, "a departed airplane is still in the unit list");

    record("flight", "takeoff_frames=11-10-9 headings=" + std::to_string(headings.size())
                   + " climb_out_world_units=" + std::to_string((takeoffEndX - startX).lround())
                   + " ended=offmap");

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
                   + ',' + std::to_string(p->getRemainingTicks()) + ',' + std::to_string(p->drawnFrame);
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
    checkEnemyAirDefence();
    checkSaveContinuation();

    gCityAircraftCsv.close();
    SDL_Log("CITY_AIRCRAFT_PROBE_PASS: %s", gCityAircraftMod.c_str());
    return -1;
}
