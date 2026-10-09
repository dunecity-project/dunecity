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

Airport::Airport(House* newOwner) : StructureBase(newOwner) {
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
}

void Airport::init() {
    itemID = Structure_Airport;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;
    graphicID = ObjPic_Airport;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = DuneCity::CitySprites::specialFrames;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

Airport::~Airport() = default;

ObjectInterface* Airport::getInterfaceContainer() {
    if (owner==pLocalHouse || debug) return AirportInterface::create(objectID);
    return DefaultObjectInterface::create(objectID);
}

int Airport::getMaxSpawnTimer() const {
    return Palace::getSpecialWeaponCooldownForHouse(HOUSE_HARKONNEN);
}

void Airport::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(patrol.remainingCycles);
    stream.writeSint32(patrol.pendingAircraft);
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
 * Cadence is DuneCity's, not the original's: the roll runs on the same
 * five-second structure tick as the ornithopter patrol below, rather than on
 * Micropolis's own simulation pass, which has no counterpart here.
 */
void Airport::updateCityAircraft() {
    if (!owner->hasPower()) return;

    const Coord origin = getLocation();
    const Coord centre = origin + Coord(structureSize.x / 2, structureSize.y / 2);
    if (!currentGameMap->tileExists(centre)) return;

    namespace CA = DuneCity::CityAircraft;

    if (currentGame->randomGen.rand(0, CA::kAirplaneRandomMax) == 0) {
        if (owner->getNumItems(Unit_AmbientAirplane) > 0) return;   // generatePlane(): already flying
        if (!currentGame->objectData.data[Unit_AmbientAirplane][owner->getHouseID()].enabled) return;
        auto* unit = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane));
        if (unit == nullptr) return;
        // makeSprite(SPRITE_AIRPLANE, (posX << 4) + 48, (posY << 4) + 12):
        // three tiles east of the airport tile, on the runway.
        Coord start = centre + Coord(3, 0);
        if (!currentGameMap->tileExists(start)) start = centre;
        unit->deploy(start);
        unit->setGuardPoint(start);
        unit->doSetAttackMode(STOP);
        // newSprite(): an airport within 20 tiles of the eastern edge starts
        // the plane already westbound instead of running the take-off east.
        if (start.x > currentGameMap->getSizeX() - 20) unit->beginWestboundCruise();
        else                                           unit->beginTakeoff();
        AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
            "city_aircraft_spawned", AITelemetry::Record().set("airport", objectID)
                .set("object", unit->getObjectID()).set("item", Unit_AmbientAirplane)
                .set("x", start.x).set("y", start.y));
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
    if (!currentGame->isCitySimEnabled() || getHealth() <= 0) return;
    if (currentGame->getGameCycleCount() % MILLI2CYCLES(5000) == 0) updateCityAircraft();
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
