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
        || ( ((isCampaignGameType(currentGame->gameType)) || ((currentGame->gameType == GameType::Skirmish || currentGame->gameType == GameType::SkirmishCoop))) && getOwner()->isAI() && getOwner()->isPowerRequired()
             // The DuneCity campaign plays its Custom city maps' rules: every turret needs power.
             && !currentGame->getGameInitSettings().isDuneCityCampaign()) ) {
        // City Plane Defence: a turret without a target takes an enemy city airplane the cycle
        // it comes into reach, instead of at its next search up to a second later; the plane
        // flies ten tiles in that second. Nothing is looked at while no city airplane exists.
        if(attackMode != STOP && !target && !AmbientAirplane::all().empty()
           && currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence) {
            if(const ObjectBase* plane = cityPlaneInReach()) setTarget(plane);
        }
        TurretBase::updateStructureSpecificStuff();
    }
}

bool RocketTurret::canAttack(const ObjectBase* object) const {
    if((object != nullptr)
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || object->isWildSandworm())
        && (isOrnithopterUnit(object->getItemID()) || object->isVisible(getOwner()->getTeamID()))
        && canMeetCityPlane(object)) {
        return true;
    } else {
        return false;
    }
}

/**
    City Plane Defence: a rocket chasing a city airplane goes off 3 x WeaponRange from this
    turret (Bullet::updateDynastyProjectile), so the turret only takes a plane its rocket can
    meet inside that distance. A plane flying away is left to the turrets ahead of it, and this
    one is free for another target. True for anything that is not a city airplane.
*/
bool RocketTurret::canMeetCityPlane(const ObjectBase* object) const {
    if(object->getItemID() != Unit_AmbientAirplane
       || !currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence) return true;
    return static_cast<const AmbientAirplane*>(object)->rocketCanMeet(
        getCenterPoint(), AmbientAirplane::rocketChaseRange4(owner->getHouseID()));
}

const ObjectBase* RocketTurret::findTarget() const {
    if(attackMode == STOP) return nullptr;
    const auto* best = TurretBase::findTarget();
    const Coord center = getCenterPoint();
    int bestDistance = best ? DynastyProjectile::distance(center*4, best->getCenterPoint()*4) : 0x7fffffff;
    // Script_Structure_FindTarget explicitly sees ornithopters at triple range,
    // even outside unveiled terrain. Other aircraft retain the normal range.
    // City Plane Defence: the city airplane is seen as far as an Ornithopter is.
    const bool cityPlanes = currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence;
    for(const auto* unit : unitList) {
        if(!(isOrnithopterUnit(unit->getItemID()) || (cityPlanes && unit->getItemID() == Unit_AmbientAirplane))
           || unit->getHealth() <= 0 || !unit->isActive() || !canAttack(unit)) continue;
        const int distance = DynastyProjectile::distance(center*4, unit->getCenterPoint()*4);
        const int factor = unit->getItemID() == Unit_AmbientAirplane ? AmbientAirplane::turretReachFactor() : 3;
        if(distance <= getWeaponRange()*factor*TILESIZE*4 && distance < bestDistance) {
            best = unit;
            bestDistance = distance;
        }
    }
    return best;
}

const ObjectBase* RocketTurret::cityPlaneInReach() const {
    const Coord center = getCenterPoint();
    const AmbientAirplane* best = nullptr;
    int bestDistance = getWeaponRange()*AmbientAirplane::turretReachFactor()*TILESIZE*4 + 1;
    for(const AmbientAirplane* plane : AmbientAirplane::all()) {
        if(plane->getHealth() <= 0 || !plane->isActive()) continue;
        const int distance = DynastyProjectile::distance(center*4, plane->getCenterPoint()*4);
        // The registry has no fixed order: equal distances go to the lower object ID.
        if(distance > bestDistance || (distance == bestDistance && best != nullptr
                                       && plane->getObjectID() > best->getObjectID())) continue;
        if(!canAttack(plane)) continue;
        best = plane;
        bestDistance = distance;
    }
    return best;
}

int RocketTurret::rocketDamage() const {
    const int baseDamage = currentGame->objectData.data[itemID][getTechnologyHouseID()].weapondamage;
    return ModManager::instance().isTornieFamilyContentActive() ? owner->radarAttackDamage(itemID, baseDamage) : baseDamage;
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
        // City Plane Defence: the cannon shell flies to where the plane was; the plane always
        // gets the rocket, which flies to where it will be.
        const bool rocketOnly = pObject->getItemID() == Unit_AmbientAirplane
            && currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence;
        const bool closeRange = !rocketOnly && distanceFrom(centerPoint, targetCenterPoint) < 3 * TILESIZE;
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
