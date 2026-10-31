#include <dunecity/PowerRules.h>
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

#include <structures/RocketTurret.h>

#include <globals.h>
#include <DynastyProjectile.h>
#include <units/UnitBase.h>

#include <Bullet.h>
#include <SoundPlayer.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <units/AmbientAirplane.h>

#include <algorithm>

RocketTurret::RocketTurret(House* newOwner) : TurretBase(newOwner) {
    RocketTurret::init();

    setHealth(getMaxHealth());
}

RocketTurret::RocketTurret(InputStream& stream) : TurretBase(stream) {
    RocketTurret::init();
}

void RocketTurret::init() {
    itemID = Structure_RocketTurret;
    owner->incrementStructures(itemID);

    attackSound = Sound_Rocket;
    // Use Bullet_TurretRocket with Dynasty speed (20.48)
    // Ornithopters run at 19.2, so turret rockets stay faster
    // Added safety detonation timer to Bullet_TurretRocket as backup
    bulletType = Bullet_TurretRocket;

    graphicID = ObjPic_RocketTurret;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 10;
    numImagesY = 1;
    curAnimFrame = firstAnimFrame = lastAnimFrame = ((10-drawnAngle) % 8) + 2;
}

RocketTurret::~RocketTurret() = default;

void RocketTurret::updateStructureSpecificStuff() {
    if( ( DuneCity::rocketTurretPowered(currentGame->getGameInitSettings().getGameOptions().rocketTurretsNeedPower, getOwner()->getProducedPower(), getOwner()->getPowerRequirement()) )
        || ( ((isCampaignGameType(currentGame->gameType)) || ((currentGame->gameType == GameType::Skirmish || currentGame->gameType == GameType::SkirmishCoop))) && getOwner()->isAI() && getOwner()->isPowerRequired()) ) {
        TurretBase::updateStructureSpecificStuff();
    }
}

bool RocketTurret::canAttack(const ObjectBase* object) const {
    if((object != nullptr)
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || object->getItemID() == Unit_Sandworm)
        && (isOrnithopterUnit(object->getItemID()) || object->isVisible(getOwner()->getTeamID()))
        && !AmbientAirplane::crashEndangers(*this, *object)) {
        return true;
    } else {
        return false;
    }
}

const ObjectBase* RocketTurret::findTarget() const {
    if(attackMode == STOP) return nullptr;
    const auto* best = TurretBase::findTarget();
    const Coord center = getCenterPoint();
    int bestDistance = best ? DynastyProjectile::distance(center*4, best->getCenterPoint()*4) : 0x7fffffff;
    // Script_Structure_FindTarget explicitly sees ornithopters at triple range,
    // even outside unveiled terrain. Other aircraft retain the normal range.
    for(const auto* unit : unitList) {
        if(!isOrnithopterUnit(unit->getItemID()) || unit->getHealth() <= 0 || !canAttack(unit)) continue;
        const int distance = DynastyProjectile::distance(center*4, unit->getCenterPoint()*4);
        if(distance <= getWeaponRange()*3*TILESIZE*4 && distance < bestDistance) {
            best = unit;
            bestDistance = distance;
        }
    }
    return best;
}

int RocketTurret::closeCannonReloadTime() const {
    // Translate Dynasty's measured 2.000-2.083 s cannon interval to 2.048 s
    // (128 cycles) in this engine, while a standalone gun turret keeps its
    // 240-cycle reload. Scale the configured Gun-Turret value by 128/240 so a mod
    // that retunes the gun turret still moves this cannon with it.
    const int gunTurretReload = currentGame->objectData.data[Structure_GunTurret][getTechnologyHouseID()].weaponreloadtime;
    return std::max(1, gunTurretReload * closeCannonReloadNumerator / closeCannonReloadDenominator);
}

void RocketTurret::attack() {
    if((weaponTimer == 0) && (target.getObjPointer() != nullptr)) {
        Coord centerPoint = getCenterPoint();
        ObjectBase* pObject = target.getObjPointer();
        Coord targetCenterPoint = pObject->getClosestCenterPoint(location);
        const int technology = getTechnologyHouseID();
        const bool closeRange = distanceFrom(centerPoint, targetCenterPoint) < 3 * TILESIZE;
        const int baseDamage = currentGame->objectData.data[
            closeRange ? Structure_GunTurret : itemID][technology].weapondamage;
        const int damage = ModManager::instance().isTornieFamilyContentActive()
            ? owner->radarAttackDamage(itemID, baseDamage) : baseDamage;

        if(closeRange) {
            // we are just shooting a bullet as a gun turret would do
            // Dynasty also uses its cannon against aircraft inside three tiles.
            {
                bulletList.push_back( new Bullet( objectID, &centerPoint, &targetCenterPoint, Bullet_ShellTurret,
                                                       damage,
                                                       pObject->isAFlyingUnit(),
                                                       pObject ) );

                currentGameMap->viewMap(pObject->getOwner()->getHouseID(), location, 2);
                soundPlayer->playSoundAt(Sound_ExplosionSmall, location);
                weaponTimer = closeCannonReloadTime();
            }
        } else {
            // MULTIPLAYER-SAFE: Track turret rocket firing
            if(isOrnithopterUnit(pObject->getItemID())) {
                currentGame->combatStats.rocketTurretFiresOnOrni++;
                currentGame->combatStats.turretRocketsSpawned++;
            }
            
            bulletList.push_back( new Bullet( objectID, &centerPoint, &targetCenterPoint, bulletType,
                                                   damage,
                                                   pObject->isAFlyingUnit(),
                                                   pObject ) );

            currentGameMap->viewMap(pObject->getOwner()->getHouseID(), location, 2);
            soundPlayer->playSoundAt(attackSound, location);
            weaponTimer = getWeaponReloadTime();
        }
    } else if((weaponTimer != 0) && (target.getObjPointer() != nullptr)) {
        // MULTIPLAYER-SAFE: Track when weapon timer blocks firing
        if(isOrnithopterUnit(target.getObjPointer()->getItemID())) {
            currentGame->combatStats.rocketTurretFireBlocked++;
        }
    }
}
