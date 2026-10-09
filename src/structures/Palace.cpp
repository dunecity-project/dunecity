#include <DynastyProjectile.h>
#include <players/AIDecisionLog.h>
/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <structures/Palace.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Bullet.h>
#include <SoundPlayer.h>

#include <players/HumanPlayer.h>
#include <mod/ModManager.h>

#include <units/InfantryBase.h>
#include <units/UnitBase.h>
#include <units/Trooper.h>
#include <units/Saboteur.h>

#include <GUI/ObjectInterfaces/PalaceInterface.h>

#define PALACE_DEATHHAND_WEAPONDAMAGE       DynastyProjectile::deathHandDamage

Palace::Palace(House* newOwner) : StructureBase(newOwner) {
    Palace::init();

    setHealth(getMaxHealth());
    specialWeaponTimer = getMaxSpecialWeaponTimer();

    // TODO: Special weapon is available immediately but AI uses it only after first visual contact
    //specialTimer = 1; // we want the special weapon to be immediately ready
}

Palace::Palace(InputStream& stream) : StructureBase(stream) {
    Palace::init();

    specialWeaponTimer = stream.readSint32();


}

void Palace::init() {
    itemID = Structure_Palace;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 3;

    graphicID = ObjPic_Palace;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;

    canAttackStuff = true;
}

Palace::~Palace() = default;

void Palace::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(specialWeaponTimer);
}

ObjectInterface* Palace::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return PalaceInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

void Palace::handleSpecialClick() {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PALACE_SPECIALWEAPON,objectID));
}

void Palace::handleDeathhandClick(int xPos, int yPos) {
    if (currentGameMap->tileExists(xPos, yPos)) {
        currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PALACE_DEATHHAND,objectID, (Uint32) xPos, (Uint32) yPos));
    }
}

int Palace::getSpecialWeaponCooldownForHouse(int houseID) {
    const auto house = static_cast<HOUSETYPE>(houseID);
    const bool tornie = ModManager::instance().isTornieContentActive();
    if (tornie && getHouseFactionIdentity(house) == HOUSE_REBELS)
        return MILLI2CYCLES(7*60*1000 + 30*1000);
    if (houseID == HOUSE_HARKONNEN || houseID == HOUSE_SARDAUKAR
        || (tornie && isHouseFaction(house, HOUSE_WILDSPADE)))
        return MILLI2CYCLES(10*60*1000);
    return MILLI2CYCLES(5*60*1000);
}

bool Palace::usesTornieMainRebelsCooldown() const {
    return ModManager::instance().isTornieContentActive()
        && getHouseFactionIdentity(static_cast<HOUSETYPE>(originalHouseID)) == HOUSE_REBELS;
}

bool Palace::usesTornieMainRebelsRandomSpecial() const {
    return usesTornieMainRebelsCooldown();
}

Palace::TornieRebelsSpecialWeapon Palace::getTornieMainRebelsSpecialWeapon() const {
    if(!usesTornieMainRebelsRandomSpecial() || specialWeaponTimer >= 0) {
        return TornieRebelsSpecialWeapon::None;
    }

    const Sint32 encodedWeapon = -specialWeaponTimer;
    if(encodedWeapon < static_cast<Sint32>(TornieRebelsSpecialWeapon::Missile)
       || encodedWeapon > static_cast<Sint32>(TornieRebelsSpecialWeapon::Ornithopters)) {
        return TornieRebelsSpecialWeapon::None;
    }

    return static_cast<TornieRebelsSpecialWeapon>(encodedWeapon);
}

bool Palace::usesTargetedSpecialWeapon() const {
    if(usesTornieMainRebelsRandomSpecial()) {
        return getTornieMainRebelsSpecialWeapon() == TornieRebelsSpecialWeapon::Missile;
    }

    const HOUSETYPE palaceHouse = getHouseFallbackHouse(static_cast<HOUSETYPE>(originalHouseID));
    return palaceHouse == HOUSE_HARKONNEN || palaceHouse == HOUSE_SARDAUKAR;
}

void Palace::selectTornieMainRebelsSpecialWeapon() {
    const Sint32 selectedWeapon = currentGame->randomGen.rand(
        static_cast<Sint32>(TornieRebelsSpecialWeapon::Missile),
        static_cast<Sint32>(TornieRebelsSpecialWeapon::Ornithopters));
    specialWeaponTimer = -selectedWeapon;
}

bool Palace::usesGuestWildspadeOrnithopterStrike() const {
    return ModManager::instance().isTornieContentActive()
        && isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_WILDSPADE);
}

bool Palace::usesGuestKleshmershFremenCall() const {
    return ModManager::instance().isTornieContentActive()
        && isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_KLESHMERSH);
}

bool Palace::usesLightVehicleCall() const {
    const HOUSETYPE originalHouse = static_cast<HOUSETYPE>(originalHouseID);

    if(ModManager::instance().isTornieContentActive()
       && isHouseFaction(originalHouse, HOUSE_THARPIQUE)) {
        return true;
    }
    if(usesGuestKleshmershFremenCall()) {
        return false;
    }
    if(usesGuestWildspadeOrnithopterStrike()) {
        return false;
    }

    const HOUSETYPE fallbackHouse = getHouseFallbackHouse(originalHouse);
    return fallbackHouse == HOUSE_NEUTRAL || fallbackHouse == HOUSE_REBELS;
}

void Palace::doSpecialWeapon() {
    if(!isSpecialWeaponReady()) {
        return;
    }

    const HOUSETYPE originalHouse = static_cast<HOUSETYPE>(originalHouseID);
    if(usesTornieMainRebelsRandomSpecial()) {
        bool activated = false;
        switch(getTornieMainRebelsSpecialWeapon()) {
            case TornieRebelsSpecialWeapon::Missile:
                // The missile is launched through doLaunchDeathhand after selecting a target.
                return;

            case TornieRebelsSpecialWeapon::Fremen:
                activated = callFremen();
                break;

            case TornieRebelsSpecialWeapon::Saboteur:
                activated = spawnSaboteur();
                break;

            case TornieRebelsSpecialWeapon::LightVehicles:
                activated = callLightVehicles();
                break;

            case TornieRebelsSpecialWeapon::Ornithopters:
                activated = callOrnithopterStrike();
                break;

            case TornieRebelsSpecialWeapon::None:
            default:
                return;
        }

        if(activated) {
            specialWeaponTimer = getMaxSpecialWeaponTimer();
        }
        return;
    }

    if(usesGuestWildspadeOrnithopterStrike()) {
        if(callOrnithopterStrike()) {
            specialWeaponTimer = getMaxSpecialWeaponTimer();
        }
        return;
    }

    if(usesGuestKleshmershFremenCall()) {
        if(callFremen()) {
            specialWeaponTimer = getMaxSpecialWeaponTimer();
        }
        return;
    }

    if(usesLightVehicleCall()) {
        if(callLightVehicles()) {
            specialWeaponTimer = getMaxSpecialWeaponTimer();
        }
        return;
    }

    switch (getHouseFallbackHouse(originalHouse)) {
        case HOUSE_HARKONNEN:
        case HOUSE_SARDAUKAR: {
            // wrong house (see DoLaunchDeathhand)
            return;
        } break;

        case HOUSE_ATREIDES:
        case HOUSE_FREMEN: {
            if(callFremen()) {
                specialWeaponTimer = getMaxSpecialWeaponTimer();
            }
        } break;

        case HOUSE_ORDOS:
        case HOUSE_MERCENARY: {
            if(spawnSaboteur()) {
                specialWeaponTimer = getMaxSpecialWeaponTimer();
            }
        } break;

        case HOUSE_NEUTRAL:
        case HOUSE_REBELS: {
            if(callLightVehicles()) {
                specialWeaponTimer = getMaxSpecialWeaponTimer();
            }
        } break;

        default: {
            SDL_Log("PALACE: Ignoring special weapon for unsupported house %d", originalHouseID);
            return;
        } break;
    }
}

void Palace::doLaunchDeathhand(int x, int y) {
    if(!isSpecialWeaponReady()) {
        return;
    }

    if(!usesTargetedSpecialWeapon()) {
        // This command is valid for Harkonnen/Sardaukar or Tornie's revealed missile.
        return;
    }

    // Dynasty scatters by up to ten tiles, rejects out-of-map scatter, then
    // encodes the chosen tile (so the final destination is its centre).
    int radius = currentGame->randomGen.rand(0, 255);
    while(radius > 160) radius /= 2;
    const int direction = currentGame->randomGen.rand(0, 255);
    Coord centerPoint = getCenterPoint();
    Coord dest(x*TILESIZE+TILESIZE/2, y*TILESIZE+TILESIZE/2);
    const Coord scattered = dest + Coord(
        (DynastyProjectile::k_stepX[direction]*radius/128)*4,
        -(DynastyProjectile::k_stepY[direction]*radius/128)*4);
    if(scattered.x >= 0 && scattered.y >= 0 && scattered.x < currentGameMap->getSizeX()*TILESIZE
       && scattered.y < currentGameMap->getSizeY()*TILESIZE) {
        dest = Coord(scattered.x/TILESIZE*TILESIZE+TILESIZE/2, scattered.y/TILESIZE*TILESIZE+TILESIZE/2);
    }

    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "palace_missile_launched",
        AITelemetry::Record().set("palace", objectID).set("aim_x", x).set("aim_y", y)
            .set("destination_x_pixels", dest.x).set("destination_y_pixels", dest.y));
    bulletList.push_back(new Bullet(objectID, &centerPoint, &dest, Bullet_LargeRocket, PALACE_DEATHHAND_WEAPONDAMAGE, false, nullptr));
    soundPlayer->playSoundAt(Sound_Rocket, getLocation());

    if(getOwner() != pLocalHouse) {
        currentGame->addToNewsTicker(_("@DUNE.ENG|81#Missile is approaching"));
        soundPlayer->playVoice(MissileApproaching, pLocalHouse->getHouseID());
    }

    specialWeaponTimer = getMaxSpecialWeaponTimer();

}

void Palace::updateStructureSpecificStuff() {
    bool becameReady = false;

    if(specialWeaponTimer > 0) {
        --specialWeaponTimer;
        becameReady = specialWeaponTimer <= 0;
    } else if(specialWeaponTimer == 0 && usesTornieMainRebelsRandomSpecial()) {
        // Upgrade an already-ready Palace from an older save to the random Tornie ability.
        becameReady = true;
    }

    if(!becameReady) {
        return;
    }

    if(usesTornieMainRebelsRandomSpecial()) {
        selectTornieMainRebelsSpecialWeapon();
    } else {
        specialWeaponTimer = 0;
    }

    if(getOwner() == pLocalHouse) {
        currentGame->addToNewsTicker(_("Palace is ready"));
    } else if(getOwner()->isAI() && !usesTargetedSpecialWeapon()) {
        doSpecialWeapon();
    }
}

bool Palace::callFremen() {
    if(getOwner()->isUnitLimitReached(Unit_Trooper)) {
        if(getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("Unit limit reached"));
        }
        return false;
    }

    int count = 0;
    int x;
    int y;
    do {
        x = currentGame->randomGen.rand(1, currentGameMap->getSizeX()-2);
        y = currentGame->randomGen.rand(1, currentGameMap->getSizeY()-2);
    } while((currentGameMap->getTile(x-1, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x-1, y)->hasAGroundObject()
            || currentGameMap->getTile(x, y)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y)->hasAGroundObject()
            || currentGameMap->getTile(x-1, y+1)->hasAGroundObject()
            || currentGameMap->getTile(x, y+1)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y+1)->hasAGroundObject())
            && (count++ <= 1000));

    if(count < 1000) {

        int spawned = 0;
        for(int numFremen = 0; numFremen < 15; numFremen++) {
            if(currentGame->randomGen.rand(0, 5) == 0) {
                continue;
            }

            if(getOwner()->isUnitLimitReached(Unit_Trooper)) {
                break;
            }

            Trooper *pFremen = static_cast<Trooper*>(getOwner()->createUnit(Unit_Trooper));
            if(pFremen == nullptr) {
                break;
            }

            int i;
            int j;
            do {
                i = currentGame->randomGen.rand(-1, 1);
                j = currentGame->randomGen.rand(-1, 1);
            } while (!currentGameMap->getTile(x + i, y + j)->infantryNotFull());

            pFremen->deploy(Coord(x + i,y + j));

            pFremen->doSetAttackMode(HUNT);
            pFremen->setRespondable(false);
            ++spawned;

            const StructureBase* closestStructure = pFremen->findClosestTargetStructure();
            if(closestStructure) {
                Coord closestPoint = closestStructure->getClosestPoint(pFremen->getLocation());
                pFremen->setGuardPoint(closestPoint);
                pFremen->setDestination(closestPoint);
            } else {
                const UnitBase* closestUnit = pFremen->findClosestTargetUnit();
                if(closestUnit) {
                    pFremen->setGuardPoint(closestUnit->getLocation());
                    pFremen->setDestination(closestUnit->getLocation());
                }
            }
        }

        return spawned > 0;
    } else {
        if(getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("Unable to spawn Fremen"));
        }

        return false;
    }
}

bool Palace::spawnSaboteur() {
    if(getOwner()->isUnitLimitReached(Unit_Saboteur)) {
        if(getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("Unit limit reached"));
        }
        return false;
    }

    Saboteur* saboteur = static_cast<Saboteur*>(getOwner()->createUnit(Unit_Saboteur));
    if(saboteur == nullptr) {
        return false;
    }

    Coord spot = currentGameMap->findDeploySpot(saboteur, getLocation(), currentGame->randomGen, getDestination(), getStructureSize());
    if(spot.isInvalid() || !currentGameMap->tileExists(spot)) {
        saboteur->cancelDeployment();
        if(getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("Unable to spawn Saboteur"));
        }
        return false;
    }

    saboteur->deploy(spot);

    if(getOwner()->isAI()) {
        SDL_Log("PALACE: Spawning AI saboteur at (%d,%d), setting to HUNT mode", spot.x, spot.y);
        saboteur->doSetAttackMode(HUNT);
        SDL_Log("PALACE: Saboteur attack mode after setting: %d", saboteur->getAttackMode());
        currentGame->addToNewsTicker(_("@DUNE.ENG|79#Saboteur is approaching"));
        soundPlayer->playVoice(SaboteurApproaching, pLocalHouse->getHouseID());
    }

    return true;
}

bool Palace::callLightVehicles() {
    int count = 0;
    int x;
    int y;
    do {
        x = currentGame->randomGen.rand(1, currentGameMap->getSizeX()-2);
        y = currentGame->randomGen.rand(1, currentGameMap->getSizeY()-2);
    } while((currentGameMap->getTile(x-1, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y-1)->hasAGroundObject()
            || currentGameMap->getTile(x-1, y)->hasAGroundObject()
            || currentGameMap->getTile(x, y)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y)->hasAGroundObject()
            || currentGameMap->getTile(x-1, y+1)->hasAGroundObject()
            || currentGameMap->getTile(x, y+1)->hasAGroundObject()
            || currentGameMap->getTile(x+1, y+1)->hasAGroundObject())
            && (count++ <= 1000));

    if(count >= 1000) {
        if(getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("Unable to spawn vehicles"));
        }
        return false;
    }

    const int trikeCount = currentGame->randomGen.rand(1, 3);
    const int quadCount = currentGame->randomGen.rand(0, 2);
    int spawned = 0;
    bool unitLimitReached = false;

    const auto spawnVehicle = [&](int itemID) {
        if(getOwner()->isUnitLimitReached(itemID)) {
            unitLimitReached = true;
            return;
        }

        UnitBase* newUnit = getOwner()->createUnit(itemID);
        if(newUnit == nullptr) {
            return;
        }

        for(int tries = 0; tries < 30; ++tries) {
            const int i = currentGame->randomGen.rand(-1, 1);
            const int j = currentGame->randomGen.rand(-1, 1);
            const Coord deployPos(x + i, y + j);

            if(currentGameMap->getTile(deployPos)->hasAGroundObject()) {
                continue;
            }
            if(!newUnit->canPass(deployPos.x, deployPos.y)) {
                continue;
            }

            newUnit->deploy(deployPos);
            newUnit->setGuardPoint(deployPos);
            newUnit->doSetAttackMode(HUNT);
            spawned++;
            return;
        }

        newUnit->cancelDeployment();
    };

    for(int i = 0; i < trikeCount; ++i) {
        spawnVehicle(Unit_Trike);
    }
    for(int i = 0; i < quadCount; ++i) {
        spawnVehicle(Unit_Quad);
    }

    if(spawned <= 0 && getOwner() == pLocalHouse) {
        currentGame->addToNewsTicker(unitLimitReached ? _("Unit limit reached") : _("Unable to spawn vehicles"));
    }

    return spawned > 0;
}
bool Palace::callOrnithopterStrike() {
    int spawned = 0;
    bool unitLimitReached = false;

    for(int i = 0; i < 3; ++i) {
        if(getOwner()->isUnitLimitReached(Unit_Ornithopter)) {
            unitLimitReached = true;
            break;
        }

        UnitBase* ornithopter = getOwner()->createUnit(Unit_Ornithopter);
        if(ornithopter == nullptr) {
            continue;
        }

        const Coord deployPos = currentGameMap->findDeploySpot(
            ornithopter, getLocation(), currentGame->randomGen, getDestination(), getStructureSize());
        if(!deployPos.isValid()) {
            ornithopter->cancelDeployment();
            continue;
        }

        ornithopter->deploy(deployPos);
        ornithopter->setGuardPoint(deployPos);
        ornithopter->doSetAttackMode(HUNT);
        ++spawned;
    }

    if(getOwner() == pLocalHouse) {
        if(spawned == 3) {
            currentGame->addToNewsTicker(_("Three Ornithopters deployed in hunt mode"));
        } else if(spawned == 0 && unitLimitReached) {
            currentGame->addToNewsTicker(_("Unit limit reached"));
        } else {
            currentGame->addToNewsTicker(_("Unable to deploy all three Ornithopters"));
        }
    }

    return spawned > 0;
}
