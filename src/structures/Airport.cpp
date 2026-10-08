#include <structures/Airport.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <dunecity/CitySpritePolicy.h>
#include <structures/Palace.h>
#include <units/UnitBase.h>
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
}

void Airport::updateStructureSpecificStuff() {
    firstAnimFrame = lastAnimFrame = curAnimFrame =
        DuneCity::CitySprites::poweredFrame(currentGame->getGameCycleCount(), owner->hasPower());
    if (!currentGame->isCitySimEnabled() || getHealth() <= 0) return;
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
