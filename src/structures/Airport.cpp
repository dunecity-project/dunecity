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

void Airport::init() {
    itemID = Structure_Airport;
    owner->incrementStructures(itemID);

    // Stefan, 2026-11-23: "make an airport 4x4 squares not 3x3". A compact Airport stays 3x3
    // and draws the same sprite cell scaled into its tiles (StructureBase::blitToScreen).
    structureSize = compact ? Coord(3, 3) : Coord(4, 4);
    graphicID = ObjPic_Airport;
    // An Airport built during the game has its free plane ready at once (Stefan, 2026-11-23:
    // "when you build an airport you should get one"). One that stands when the game begins, on
    // a scenario map or in a generated city, charges first, and one built before the first
    // charge time has passed is ready at that time, so that no house bombs in the first five
    // minutes of a game (2026-11-24: "early game balance").
    {
        const Sint32 charge = freePlaneCharge();
        const Sint32 played = static_cast<Sint32>(std::min<Uint32>(currentGame->getGameCycleCount(), static_cast<Uint32>(charge)));
        freePlaneTimer = charge - played;
    }
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
        && owner->getNumItems(Unit_AmbientAirplane) < CA::kMaxPlanesPerHouse
        && owner->getCredits() >= CA::kPlanePrice;
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
    if (launchAirplane(true) == nullptr) return false;
    owner->takeCredits(CA::kPlanePrice);
    purchaseCooldown = MILLI2CYCLES(CA::kPurchaseCooldownMs);
    SDL_Log("CITY_PLANE_BOUGHT: house=%d price=%d cycle=%u", owner->getHouseID(), CA::kPlanePrice,
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
    if (freePlaneTimer <= 0 && AmbientAirplane::freePlanes(owner) == 0
        && currentGame->objectData.data[Unit_AmbientAirplane][owner->getHouseID()].enabled) {
        if (launchAirplane(false) != nullptr) {
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

void Airport::updateStructureSpecificStuff() {
    firstAnimFrame = lastAnimFrame = curAnimFrame =
        DuneCity::CitySprites::poweredFrame(currentGame->getGameCycleCount(), owner->hasPower());
    if (purchaseCooldown > 0) --purchaseCooldown;
    if (freePlaneTimer > 0) --freePlaneTimer;
    if (!currentGame->isCitySimEnabled() || getHealth() <= 0) return;
    if (currentGame->getGameCycleCount() % MILLI2CYCLES(5000) == 0) updateCityAircraft();
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
