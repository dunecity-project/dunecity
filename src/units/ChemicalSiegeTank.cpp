/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <units/ChemicalSiegeTank.h>


#include <Bullet.h>
#include <Game.h>
#include <House.h>
#include <ObjectManager.h>
#include <globals.h>

#include <algorithm>
#include <cstdlib>
#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <ScreenBorder.h>
#include <SoundPlayer.h>
#include <mod/HealingBalance.h>

ChemicalSiegeTank::ChemicalSiegeTank(House* newOwner) : TankBase(newOwner) {
    ChemicalSiegeTank::init();
    setHealth(getMaxHealth());
}

ChemicalSiegeTank::ChemicalSiegeTank(InputStream& stream) : TankBase(stream) {
    ChemicalSiegeTank::init();
}

void ChemicalSiegeTank::init() {
    itemID = Unit_ChemicalSiegeTank;
    registerUnit();
    numWeapons = 2;
    bulletType = Bullet_ShellLarge;
    graphicID = ObjPic_Siegetank_Base;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    gunGraphicID = ObjPic_ChemicalSiegeTankGunTornie;
    turretGraphic = pGFXManager->getObjPic(gunGraphicID, getOwner()->getHouseID());
    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

ChemicalSiegeTank::~ChemicalSiegeTank() = default;

void ChemicalSiegeTank::blitToScreen() {
    const int x1 = screenborder->world2screenX(realX);
    const int y1 = screenborder->world2screenY(realY);
    SDL_Texture* pUnitGraphic = graphic[currentZoomlevel];
    SDL_Rect source1 = calcSpriteSourceRect(pUnitGraphic, drawnAngle, numImagesX);
    SDL_Rect dest1 = calcSpriteDrawingRect(pUnitGraphic, x1, y1, numImagesX, 1, HAlign::Center, VAlign::Center);
    SDL_RenderCopy(renderer, pUnitGraphic, &source1, &dest1);

    const Coord siegeTankTurretOffset[] = {
        Coord(8, -12), Coord(0, -20), Coord(0, -20), Coord(-4, -20),
        Coord(-8, -12), Coord(-8, -4), Coord(-4, -12), Coord(8, -4)
    };
    SDL_Texture* pTurretGraphic = turretGraphic[currentZoomlevel];
    SDL_Rect source2 = calcSpriteSourceRect(pTurretGraphic, drawnTurretAngle, NUM_ANGLES);
    SDL_Rect dest2 = calcSpriteDrawingRect(
        pTurretGraphic,
        screenborder->world2screenX(realX + siegeTankTurretOffset[drawnTurretAngle].x),
        screenborder->world2screenY(realY + siegeTankTurretOffset[drawnTurretAngle].y),
        NUM_ANGLES, 1, HAlign::Center, VAlign::Center);
    SDL_RenderCopy(renderer, pTurretGraphic, &source2, &dest2);
    if(isBadlyDamaged()) drawSmoke(x1, y1);
}

void ChemicalSiegeTank::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        const Coord realPos(lround(realX), lround(realY));
        const Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2});
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));
        if(isVisible(getOwner()->getTeamID())) {
            screenborder->shakeScreen(18);
            soundPlayer->playSoundAt(Sound_ExplosionLarge, location);
        }
    }
    TankBase::destroy();
}

void ChemicalSiegeTank::playAttackSound() {
    soundPlayer->playSoundAt(Sound_ExplosionSmall, location);
}

namespace {
constexpr int CHEMICAL_SIEGE_HEAL_AMOUNT = 15;
constexpr int CHEMICAL_SIEGE_HEAL_RELOAD = 60;
}

bool ChemicalSiegeTank::isValidHealTarget(const ObjectBase* pObject) const
{
    const auto* pUnit = dynamic_cast<const UnitBase*>(pObject);
    return pUnit != nullptr && pUnit != this && pUnit->getHealth() > 0
        && pUnit->getHealth() < pUnit->getMaxHealth()
        && pUnit->getOwner()->getTeamID() == owner->getTeamID();
}

UnitBase* ChemicalSiegeTank::findDamagedAlly() const
{
    UnitBase* pBest = nullptr;
    int bestDistance = getAreaGuardRange() + 1;
    for(UnitBase* pUnit : unitList) {
        if(!isValidHealTarget(pUnit)) {
            continue;
        }
        const Coord targetLocation = pUnit->getLocation();
        const int guardDistance = std::max(std::abs(targetLocation.x - guardPoint.x), std::abs(targetLocation.y - guardPoint.y));
        if(guardDistance > getAreaGuardRange()) {
            continue;
        }
        const int distance = std::max(std::abs(targetLocation.x - location.x), std::abs(targetLocation.y - location.y));
        if(distance < bestDistance) {
            bestDistance = distance;
            pBest = pUnit;
        }
    }
    return pBest;
}

void ChemicalSiegeTank::fireHealingMissile(UnitBase* pTarget)
{
    if(pTarget == nullptr || healingReloadTimer > 0) {
        return;
    }
    Coord source = getCenterPoint();
    Coord destination = pTarget->getCenterPoint();
    bulletList.push_back(new Bullet(objectID, &source, &destination, Bullet_Heal,
                                    CHEMICAL_SIEGE_HEAL_AMOUNT, pTarget->isAFlyingUnit(), pTarget));
    const int baseReload = ModManager::instance().isTornieFamilyContentActive()
        ? 120 : CHEMICAL_SIEGE_HEAL_RELOAD;
    healingReloadTimer = healing::reload(baseReload, getProductionHouseID());
}

void ChemicalSiegeTank::doAttackObject(Uint32 targetObjectID, bool bForced)
{
    ObjectBase* pObject = currentGame->getObjectManager().getObject(targetObjectID);
    if(isValidHealTarget(pObject)) {
        healingTarget.pointTo(pObject);
        target.pointTo(nullptr);
        forced = false;
        manualHealing = true;
        return;
    }
    healingTarget.pointTo(nullptr);
    manualHealing = false;
    TankBase::doAttackObject(targetObjectID, bForced);
}

bool ChemicalSiegeTank::update()
{
    if(!TankBase::update()) {
        return false;
    }
    if(healingReloadTimer > 0) {
        --healingReloadTimer;
    }
    if(wasForced() || hasATarget()) {
        healingTarget.pointTo(nullptr);
        manualHealing = false;
        return true;
    }

    UnitBase* pHealTarget = dynamic_cast<UnitBase*>(healingTarget.getObjPointer());
    if(!isValidHealTarget(pHealTarget)) {
        healingTarget.pointTo(nullptr);
        manualHealing = false;
        if(getAttackMode() == AREAGUARD) {
            pHealTarget = findDamagedAlly();
            healingTarget.pointTo(pHealTarget);
        }
    }
    if(pHealTarget == nullptr) {
        return true;
    }

    const Coord targetLocation = pHealTarget->getLocation();
    const int distance = std::max(std::abs(targetLocation.x - location.x), std::abs(targetLocation.y - location.y));
    if(distance <= getWeaponRange()) {
        setDestination(location);
        fireHealingMissile(pHealTarget);
    } else {
        setDestination(targetLocation);
    }
    return true;
}
