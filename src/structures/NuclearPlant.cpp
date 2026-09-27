#include <structures/NuclearPlant.h>
#include <dunecity/PowerRules.h>
#include <dunecity/CitySpritePolicy.h>

#include <globals.h>
#include <dunecity/NuclearBlastPolicy.h>
#include <players/AIDecisionLog.h>
#include <Map.h>
#include <Tile.h>
#include <Explosion.h>
#include <SoundPlayer.h>
#include <units/UnitBase.h>
#include <ScreenBorder.h>
#include <algorithm>
#include <set>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <GUI/ObjectInterfaces/WindTrapInterface.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

NuclearPlant::NuclearPlant(House* newOwner) : StructureBase(newOwner) {
    NuclearPlant::init();

    setHealth(getMaxHealth());
}

NuclearPlant::NuclearPlant(InputStream& stream) : StructureBase(stream) {
    NuclearPlant::init();
}

void NuclearPlant::init() {
    itemID = Structure_NuclearPlant;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;
    graphicID = ObjPic_NuclearPlant;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = DuneCity::CitySprites::specialFrames;
    numImagesY = 1;
    firstAnimFrame = lastAnimFrame = curAnimFrame = 0;
}

NuclearPlant::~NuclearPlant() {
    // Also reached by rejected placement and owner demolition. Remove the
    // remaining contribution without explosions; lethal damage already set it to zero.
    setHealth(0);
}

ObjectInterface* NuclearPlant::getInterfaceContainer() {
    if (pLocalHouse == owner || debug) return WindTrapInterface::create(objectID);
    return DefaultObjectInterface::create(objectID);
}

bool NuclearPlant::update() {
    firstAnimFrame = lastAnimFrame = curAnimFrame =
        DuneCity::CitySprites::poweredFrame(currentGame->getGameCycleCount(), getHealth() > 0);
    return StructureBase::update();
}

void NuclearPlant::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    StructureBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int NuclearPlant::getProducedPower() const {
    const int nominal = abs(currentGame->objectData.data[itemID][originalHouseID].power);
    return DuneCity::generatorOutput(nominal, getHealth(), getMaxHealth(), currentGame->isCitySimEnabled());
}

void NuclearPlant::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                                   const DeviationReward::Provenance& provenance) {
    const bool wasAlive = getHealth() > 0;
    StructureBase::handleDamage(damage, damagerID, damagerOwner, provenance);
    if (wasAlive && getHealth() <= 0) {
        detonationCreditOwner = damagerOwner;
        detonationTrigger = damagerID;
    }
}

void NuclearPlant::destroy() {
    setHealth(0); // Remove any remaining output when destroyed directly as well as by damage.
    const Coord center = getCenterPoint();
    const Uint32 source = getObjectID();
    House* blastOwner = detonationCreditOwner ? detonationCreditOwner : owner;
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "nuclear_detonation",
        AITelemetry::Record().set("object", source).set("x", center.x).set("y", center.y)
            .set("radius_squared_pixels", DuneCity::NuclearBlastPolicy::radiusSquared)
            .set("damage", DuneCity::NuclearBlastPolicy::plantBlastDamage)
            .set("trigger_attacker", detonationTrigger).set("credit_house", blastOwner->getHouseID()));

    // Snapshot identities before damage callbacks; deaths are resolved by each
    // object's normal update, including further reactor detonations.
    std::set<Uint32> targets;
    for (const auto* structure : structureList) targets.insert(structure->getObjectID());
    for (const auto* unit : unitList)
        if (DuneCity::NuclearBlastPolicy::exposedUnit(unit->isActive(), unit->isAFlyingUnit()))
            targets.insert(unit->getObjectID());
    for (Uint32 id : targets) {
        auto* target = currentGame->getObjectManager().getObject(id);
        if (!target || id == source || target->getHealth() <= 0) continue;
        Coord closest = target->getCenterPoint();
        if (const auto* structure = dynamic_cast<const StructureBase*>(target)) {
            const Coord start = structure->getLocation() * TILESIZE;
            const Coord end = start + structure->getStructureSize() * TILESIZE;
            closest = Coord(std::clamp(center.x, start.x, end.x - 1), std::clamp(center.y, start.y, end.y - 1));
        }
        if (DuneCity::NuclearBlastPolicy::contains(closest.x - center.x, closest.y - center.y))
            target->handleDamage(DuneCity::NuclearBlastPolicy::plantBlastDamage, source, blastOwner);
    }

    for (int dx = -DuneCity::NuclearBlastPolicy::searchTiles; dx <= DuneCity::NuclearBlastPolicy::searchTiles; ++dx) {
        for (int dy = -DuneCity::NuclearBlastPolicy::searchTiles; dy <= DuneCity::NuclearBlastPolicy::searchTiles; ++dy) {
            if (!DuneCity::NuclearBlastPolicy::contains(dx * TILESIZE, dy * TILESIZE)) continue;
            const Coord pos = center + Coord(dx * TILESIZE, dy * TILESIZE);
            const Coord tilePos(pos.x / TILESIZE, pos.y / TILESIZE);
            if (pos.x < 0 || pos.y < 0 || !currentGameMap->tileExists(tilePos.x, tilePos.y)) continue;
            auto* tile = currentGameMap->getTile(tilePos.x, tilePos.y);
            if (tile->isRoad()) { tile->setRoad(false); tile->setDestroyedStructureTile(Destroyed1x1Structure); }
            if (tile->isConcrete()) tile->setType(Terrain_Rock);
            if (tile->isRock()) tile->addDamage(Tile::Terrain_RockDamage, Tile::RockDamage2, pos);
            else if (tile->isSand() || tile->isSpice()) tile->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage3, pos);
            currentGame->getExplosionList().push_back(new Explosion(((dx + dy) % 2) ? Explosion_Large1 : Explosion_Large2, pos, owner->getHouseID()));
        }
    }
    soundPlayer->playSoundAt(Sound_ExplosionLarge, getLocation());
    screenborder->shakeScreen(22);
    StructureBase::destroy(); // Deletes this; never detonate from the destructor/save teardown.
}
