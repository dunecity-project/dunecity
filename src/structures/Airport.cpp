#include <cstdlib>
#include <structures/Airport.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <dunecity/CitySpritePolicy.h>
#include <structures/Palace.h>
#include <units/UnitBase.h>
#include <units/AmbientAirplane.h>
#include <units/AmbientHelicopter.h>
#include <dunecity/CityAircraftPolicy.h>
#include <players/AIDecisionLog.h>
#include <FileClasses/TextManager.h>
#include <GUI/ObjectInterfaces/AirportInterface.h>
#include <Command.h>
#include <CommandManager.h>

#include <algorithm>
#include <mmath.h>

Airport::Airport(House* newOwner) : StructureBase(newOwner) {
    compact = nextIsCompact;
    Airport::init();
    setHealth(getMaxHealth());
}

Airport::Airport(InputStream& stream) : StructureBase(stream) {
    Airport::init();
    if (currentGame->getLoadedSavegameVersion() >= 9836) {
        const int remaining=stream.readSint32();
        const int pending=stream.readSint32();
        patrol.restore(remaining,pending,getMaxSpawnTimer());
    }
    if (currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_CITY_PLANE_PURCHASE) {
        purchaseCooldown = std::max(0, stream.readSint32());
    }
    // An Airport from a save older than the 4x4 Airport is compact: its tiles in that save hold
    // a 3x3 claim. init() ran before this was known, so the footprint is set again here.
    compact = currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_AIRPORT_4X4 ? stream.readBool() : true;
    structureSize = compact ? Coord(3, 3) : Coord(4, 4);
    // An older save's Airport starts with a full clock, whatever init() made of the cycle count.
    freePlaneTimer = currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_AIRPORT_FREE_PLANE_CLOCK
        ? std::max(0, stream.readSint32()) : freePlaneCharge();
}

int Airport::freePlaneCharge() {
    // Balance experiments only (tests/ai matches on the queue): DUNECITY_TEST_FREE_PLANE_SECONDS
    // sets the charge for a whole process, read once. Never set in a game people play: it is
    // not part of the match rules and two players with different values would fall out of step.
    static const int experiment = [] {
        const char* seconds = std::getenv("DUNECITY_TEST_FREE_PLANE_SECONDS");
        const int value = seconds != nullptr ? std::atoi(seconds) : 0;
        return value > 0 ? MILLI2CYCLES(value * 1000) : 0;
    }();
    if (testFreePlaneCycles > 0) return testFreePlaneCycles;
    return experiment > 0 ? experiment : MILLI2CYCLES(DuneCity::CityAircraft::kFreePlaneMs);
}

bool Airport::freePlanePerAirport() {
    static const bool perAirport = [] {
        const char* text = std::getenv("DUNECITY_TEST_FREE_PLANE_PER_AIRPORT");
        return text != nullptr && std::atoi(text) == 1;
    }();
    return perAirport;
}

bool Airport::planeBuildTime() {
    static const bool enabled = [] {
        const char* text = std::getenv("DUNECITY_TEST_PLANE_BUILD_TIME");
        return text != nullptr && std::atoi(text) == 1;
    }();
    return enabled;
}

int Airport::planeBuildCycles(int houseID) {
    if (!planeBuildTime()) return 0;
    const auto& orni = currentGame->objectData.data[Unit_Ornithopter][houseID];
    if (orni.price <= 0 || orni.buildtime <= 0) return MILLI2CYCLES(60000);
    return static_cast<int>(int64_t(DuneCity::CityAircraft::planePrice()) * orni.buildtime * 15 / orni.price);
}

void Airport::init() {
    itemID = Structure_Airport;
    owner->incrementStructures(itemID);

    // Stefan, 2026-11-23: "make an airport 4x4 squares not 3x3". A compact Airport stays 3x3
    // and draws the same sprite cell scaled into its tiles (StructureBase::blitToScreen).
    structureSize = compact ? Coord(3, 3) : Coord(4, 4);
    graphicID = ObjPic_Airport;
    // An Airport built during the game has its free plane ready at once, also in the first five
    // minutes (Stefan, 2026-11-23: "when you build an airport you should get one"; 2026-11-26:
    // "make a plane spawn when an airport is built, not the first one after 5 mins", which ends
    // the opening hold of 2026-11-24). One that stands when the game begins, placed by the map
    // loader before the first cycle (a scenario map, a generated city, a Fortress base), still
    // charges first, so a fully built start does not open with every house bombing.
    freePlaneTimer = currentGame->getGameCycleCount() == 0 ? freePlaneCharge() : 0;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = DuneCity::CitySprites::specialFrames;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

Airport::~Airport() = default;

ObjectInterface* Airport::getInterfaceContainer() {
    if (mayShowOwnerInterface()) return AirportInterface::create(objectID);
    return DefaultObjectInterface::create(objectID);
}

int Airport::getMaxSpawnTimer() const {
    return Palace::getSpecialWeaponCooldownForHouse(HOUSE_HARKONNEN);
}

void Airport::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(patrol.remainingCycles);
    stream.writeSint32(patrol.pendingAircraft);
    stream.writeSint32(purchaseCooldown);
    stream.writeBool(compact);
    stream.writeSint32(freePlaneTimer);
}

AmbientAirplane* Airport::launchAirplane(bool bought) {
    namespace CA = DuneCity::CityAircraft;
    const Coord centre = getLocation() + Coord(structureSize.x / 2, structureSize.y / 2);
    auto* unit = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane));
    if (unit == nullptr) return nullptr;
    // makeSprite(SPRITE_AIRPLANE, (posX << 4) + 48, (posY << 4) + 12):
    // three tiles east of the airport's centre tile, on the runway. For the 4x4 and the compact
    // 3x3 Airport alike that is the second tile east of the building.
    Coord start = centre + Coord(3, 0);
    if (!currentGameMap->tileExists(start)) start = centre;
    unit->setBought(bought);
    unit->deploy(start);
    unit->setGuardPoint(start);
    unit->doSetAttackMode(STOP);
    // newSprite(): an airport within 20 tiles of the eastern edge starts
    // the plane already westbound instead of running the take-off east.
    if (start.x > currentGameMap->getSizeX() - 20) unit->beginWestboundCruise();
    else                                           unit->beginTakeoff();
    // One line per event for the match tables (tests/ai/city-plane-report.py).
    SDL_Log("CITY_PLANE_LAUNCHED: house=%d object=%u bought=%d cycle=%u", owner->getHouseID(), unit->getObjectID(),
            bought ? 1 : 0, currentGame->getGameCycleCount());
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "city_aircraft_spawned", AITelemetry::Record().set("airport", objectID)
            .set("object", unit->getObjectID()).set("item", Unit_AmbientAirplane)
            .set("x", start.x).set("y", start.y).set("bought", bought ? 1 : 0));
    return unit;
}

bool Airport::canBuyAirplane() const {
    namespace CA = DuneCity::CityAircraft;
    return currentGame->getGameInitSettings().getGameOptions().cityPlanePurchase
        && currentGame->isCitySimEnabled() && getHealth() > 0 && owner->hasPower()
        && currentGame->objectData.data[Unit_AmbientAirplane][owner->getHouseID()].enabled
        && owner->getNumItems(Unit_AmbientAirplane) + buildingPlanes(owner) < CA::kMaxPlanesPerHouse
        && owner->getCredits() >= CA::planePrice()
        && !isBuildingPlane();
}

int Airport::buildingPlanes(const House* house) {
    if (!planeBuildTime()) return 0;
    int building = 0;
    for (const StructureBase* structure : structureList) {
        if (structure->getOwner() == house && structure->getItemID() == Structure_Airport
            && static_cast<const Airport*>(structure)->isBuildingPlane()) ++building;
    }
    return building;
}

/**
    Stefan, 2026-11-23: "allow the ai to spawn planes for 1500 credits". Match rule City Plane
    Purchase: the Airport launches a plane now instead of waiting for its free roll, also while
    the house already has planes in the air, up to CA::kMaxPlanesPerHouse (Stefan, 2026-11-23:
    "i should be able to buy more planes, up to 10"). A player buys as fast as he can pay;
    purchaseCooldown, started here, holds nobody back; the probes read it to see a purchase. The same call serves a
    human's Buy button (CMD_AIRPORT_BUY_AIRPLANE) and QuantBot (QuantBot::buyCityAirplanes).
*/
bool Airport::buyAirplane() {
    namespace CA = DuneCity::CityAircraft;
    if (!canBuyAirplane()) return false;
    if (planeBuildTime()) {
        // Paid now, launched when built (updateStructureSpecificStuff); the clock is the build.
        owner->takeCredits(CA::planePrice());
        purchaseCooldown = std::max(1, planeBuildCycles(owner->getHouseID()));
        SDL_Log("CITY_PLANE_BOUGHT: house=%d price=%d build_cycles=%d airport=%u cycle=%u", owner->getHouseID(),
                CA::planePrice(), purchaseCooldown, objectID, currentGame->getGameCycleCount());
        return true;
    }
    if (launchAirplane(true) == nullptr) return false;
    owner->takeCredits(CA::planePrice());
    purchaseCooldown = MILLI2CYCLES(CA::kPurchaseCooldownMs);
    SDL_Log("CITY_PLANE_BOUGHT: house=%d price=%d cycle=%u", owner->getHouseID(), CA::planePrice(),
            currentGame->getGameCycleCount());
    return true;
}

void Airport::handleBuyAirplaneClick() {
    if (currentGame && pLocalPlayer && currentGame->canControlHouse(owner))
        currentGame->getCommandManager().addCommand(
            Command(pLocalPlayer->getPlayerID(), CMD_AIRPORT_BUY_AIRPLANE, objectID));
}

/**
 * Micropolis simulate.cpp:1651 doAirport(): "Generate a airplane or
 * helicopter every now and then."
 *
 *     if (getRandom(5) == 0)  { generatePlane(pos);  return; }
 *     if (getRandom(12) == 0) { generateCopter(pos); }
 *
 * generatePlane/generateCopter return immediately when that sprite already
 * exists, so the original city has at most one of each at any time. Here the
 * singleton is per owning house instead of per map, so two cities each keep
 * their own pair and a second airport cannot double the fleet. Only an
 * operational airport launches: alive, powered, and in city-simulation mode.
 *
 * Cadence is DuneCity's, not the original's: the roll runs on a five-second
 * structure tick, rather than on Micropolis's own simulation pass, which has
 * no counterpart here.
 */
void Airport::updateCityAircraft() {
    if (!owner->hasPower()) return;

    const Coord origin = getLocation();
    const Coord centre = origin + Coord(structureSize.x / 2, structureSize.y / 2);
    if (!currentGameMap->tileExists(centre)) return;

    namespace CA = DuneCity::CityAircraft;

    // The free plane (Stefan, 2026-11-23: "the normal plane that respawns should be free";
    // "delay the time you get a free plane to be the time it takes to get a saboteur"). It is
    // counted apart from bought planes and launches when this Airport's clock has run out and
    // the house has no free plane in the air. The launch restarts the clock of every Airport of
    // the house, so a second Airport does not shorten the wait. Micropolis rolled one in six
    // here (generatePlane()); that roll is gone, and with it one draw from the game's generator.
    // Under the experiment DUNECITY_TEST_FREE_PLANE_PER_AIRPORT (freePlanePerAirport) each Airport
    // has its own: one free plane in the air per Airport, and only the launching Airport's clock
    // restarts.
    const bool perAirport = freePlanePerAirport();
    const int freeAllowed = perAirport ? owner->getNumItems(Structure_Airport) : 1;
    if (freePlaneTimer <= 0 && AmbientAirplane::freePlanes(owner) < freeAllowed
        && currentGame->objectData.data[Unit_AmbientAirplane][owner->getHouseID()].enabled) {
        if (launchAirplane(false) != nullptr) {
            if (perAirport) {
                freePlaneTimer = freePlaneCharge();
                return;
            }
            for (StructureBase* structure : structureList) {
                if (structure->getOwner() == owner && structure->getItemID() == Structure_Airport) {
                    static_cast<Airport*>(structure)->freePlaneTimer = freePlaneCharge();
                }
            }
        }
        return;
    }

    if (currentGame->randomGen.rand(0, CA::kHelicopterRandomMax) != 0) return;
    if (owner->getNumItems(Unit_AmbientHelicopter) > 0) return;      // generateCopter(): already flying
    if (!currentGame->objectData.data[Unit_AmbientHelicopter][owner->getHouseID()].enabled) return;
    auto* unit = dynamic_cast<AmbientHelicopter*>(owner->createUnit(Unit_AmbientHelicopter));
    if (unit == nullptr) return;
    // makeSprite(SPRITE_HELICOPTER, posX << 4, (posY << 4) + 30): on the pad.
    unit->deploy(centre);
    unit->setGuardPoint(centre);
    unit->doSetAttackMode(STOP);
    unit->setHome(centre);
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "city_aircraft_spawned", AITelemetry::Record().set("airport", objectID)
            .set("object", unit->getObjectID()).set("item", Unit_AmbientHelicopter)
            .set("x", centre.x).set("y", centre.y));
}

void Airport::logCover() const {
    namespace CA = DuneCity::CityAircraft;
    // Diagnostics only: one line a game minute per Airport for the match tables
    // (tests/ai/airport-turrets-report.py). ring: Rocket Turrets of the owner's team within
    // kAirportRingTiles of the footprint; reach: those that reach a plane over the Airport's
    // centre (RocketTurret::cityPlaneInReach's distance rule); powered: they can fire (Rocket
    // Turrets need power in this game, or the owner has it).
    const Coord origin = getLocation();
    const Coord centre = getCenterPoint();
    int ring = 0, reach = 0, gun = 0;
    for (const StructureBase* structure : structureList) {
        if (structure->getHealth() <= 0 || structure->getOwner()->getTeamID() != owner->getTeamID()) continue;
        const Coord at = structure->getLocation();
        const int dx = at.x < origin.x ? origin.x - at.x : at.x > origin.x + structureSize.x - 1 ? at.x - (origin.x + structureSize.x - 1) : 0;
        const int dy = at.y < origin.y ? origin.y - at.y : at.y > origin.y + structureSize.y - 1 ? at.y - (origin.y + structureSize.y - 1) : 0;
        const bool inRing = std::max(dx, dy) <= CA::kAirportRingTiles;
        if (structure->getItemID() == Structure_GunTurret) { gun += inRing; continue; }
        if (structure->getItemID() != Structure_RocketTurret) continue;
        ring += inRing;
        const int range = currentGame->objectData.data[Structure_RocketTurret][structure->getOriginalHouseID()].weaponrange
            * AmbientAirplane::turretReachFactor() * TILESIZE;
        reach += distanceFrom(structure->getCenterPoint(), centre) <= range;
    }
    const bool powered = !currentGame->getGameInitSettings().getGameOptions().rocketTurretsNeedPower || owner->hasPower();
    SDL_Log("CITY_AIRPORT_COVER: house=%d airport=%u x=%d y=%d ring=%d reach=%d gun_turrets=%d powered=%d health=%d cycle=%u",
            owner->getHouseID(), objectID, origin.x, origin.y, ring, reach, gun, powered ? 1 : 0,
            static_cast<int>(lround(getHealth())), currentGame->getGameCycleCount());
}

void Airport::updateStructureSpecificStuff() {
    firstAnimFrame = lastAnimFrame = curAnimFrame =
        DuneCity::CitySprites::poweredFrame(currentGame->getGameCycleCount(), owner->hasPower());
    if (purchaseCooldown > 0 && --purchaseCooldown == 0 && planeBuildTime() && getHealth() > 0
        && currentGame->isCitySimEnabled()) {
        // DUNECITY_TEST_PLANE_BUILD_TIME: the bought plane is built.
        launchAirplane(true);
    }
    if (freePlaneTimer > 0) --freePlaneTimer;
    if (!currentGame->isCitySimEnabled() || getHealth() <= 0) return;
    if (currentGame->getGameCycleCount() % MILLI2CYCLES(5000) == 0) updateCityAircraft();
    if (currentGame->getGameCycleCount() % MILLI2CYCLES(60000) == 0) logCover();
    // Stefan, 2026-11-22: "remove ornithopter spawn from airports". The patrol only runs under
    // the match rule Airport Ornithopter Patrol (default off; on is the Airport up to 1.0.876).
    // Its timer and pair counter stay in the save and stand still while the rule is off.
    if (!currentGame->getGameInitSettings().getGameOptions().airportOrnithopterPatrol) return;
    patrol.tick();
    if (!patrol.ready() || !owner->hasPower()
        || currentGame->getGameCycleCount() % MILLI2CYCLES(5000) != 0) return;
    int spawned=0;
    const int pending=patrol.pendingAircraft;
    for (int i=0;i<pending;++i) {
        if (owner->isUnitLimitReached(Unit_Ornithopter)
            || !currentGame->objectData.data[Unit_Ornithopter][owner->getHouseID()].enabled) break;
        auto* unit=owner->createUnit(Unit_Ornithopter);
        if (!unit) break;
        Coord spot=Coord::Invalid();
        const Coord origin=getLocation();
        // Deterministic local deployment; no Hunt order or map-wide scan.
        for (int y=origin.y-1;y<=origin.y+structureSize.y && spot.isInvalid();++y)
            for (int x=origin.x-1;x<=origin.x+structureSize.x;++x)
                if (currentGameMap->tileExists(x,y)
                    && !currentGameMap->getTile(x,y)->hasAnAirUnit()) { spot=Coord(x,y); break; }
        if (spot.isInvalid()) { unit->cancelDeployment(); break; }
        unit->deploy(spot);
        unit->setGuardPoint(spot);
        unit->doSetAttackMode(owner->isAI() ? STOP : GUARD);
        patrol.deployed(getMaxSpawnTimer());
        ++spawned;
        SDL_Log("CITY_PLANE_PATROL: house=%d object=%u cycle=%u", owner->getHouseID(), unit->getObjectID(),
                currentGame->getGameCycleCount());
        AITelemetry::log().write(currentGame->getGameCycleCount(),owner->getHouseID(),-1,
            "airport_unit_spawned",AITelemetry::Record().set("airport",objectID)
                .set("object",unit->getObjectID()).set("item",Unit_Ornithopter).set("x",spot.x).set("y",spot.y));
    }
    if (spawned > 0) {
        AITelemetry::log().write(currentGame->getGameCycleCount(),owner->getHouseID(),-1,
            "airport_reinforcements",AITelemetry::Record().set("airport",objectID)
                .set("spawned",spawned).set("pending",patrol.ready()?patrol.pendingAircraft:0)
                .set("cooldown_cycles",patrol.remainingCycles));
        if (owner==pLocalHouse) currentGame->addToNewsTicker(_("Airport ornithopter reinforcements deployed"));
    }
}
