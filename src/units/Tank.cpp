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

#include <units/Tank.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <ScreenBorder.h>
#include <SoundPlayer.h>
#include <mod/ModManager.h>


Tank::Tank(House* newOwner, int unitType) : TankBase(newOwner) {
    Tank::init(unitType);

    setHealth(getMaxHealth());
}

Tank::Tank(InputStream& stream, int unitType) : TankBase(stream) {
    Tank::init(unitType);
}

void Tank::init(int unitType) {
    itemID = unitType;
    registerUnit();

    numWeapons = 1;
    bulletType = itemID == Unit_SlowTank ? Bullet_Slow : Bullet_ShellMedium;

    graphicID = itemID == Unit_RangedTank ? ObjPic_RangedTankBase
        : itemID == Unit_SlowTank ? ObjPic_SlowTankBase : ObjPic_Tank_Base;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    gunGraphicID = itemID == Unit_RangedTank ? ObjPic_RangedTankTurret
        : itemID == Unit_SlowTank ? ObjPic_SlowTankTurret : ObjPic_Tank_Gun;
    turretGraphic = pGFXManager->getObjPic(gunGraphicID,getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

Tank::~Tank() = default;

bool Tank::canAttack(const ObjectBase* object) const {
    if(ModManager::instance().isTornieFamilyContentActive() && itemID == Unit_SlowTank
       && (!object || object->isAStructure() || object->isAFlyingUnit())) return false;
    return TankBase::canAttack(object);
}


void Tank::blitToScreen() {
    int x = screenborder->world2screenX(realX);
    int y = screenborder->world2screenY(realY);

    const Uint8 dune2rBlend = pGFXManager->getDune2RVisualBlend();
    const bool crossfading = dune2rBlend > 0 && dune2rBlend < SDL_ALPHA_OPAQUE;
    if(crossfading) {
        SDL_Texture* classicBase = graphic[currentZoomlevel];
        SDL_Rect classicBaseSource = calcSpriteSourceRect(classicBase, drawnAngle, numImagesX);
        SDL_Rect classicBaseDest = calcSpriteDrawingRect(
            classicBase, x, y, numImagesX, 1, HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, classicBase, &classicBaseSource, &classicBaseDest);

        SDL_Texture* classicTurret = turretGraphic[currentZoomlevel];
        SDL_Rect classicTurretSource = calcSpriteSourceRect(
            classicTurret, drawnTurretAngle, NUM_ANGLES);
        SDL_Rect classicTurretDest = calcSpriteDrawingRect(
            classicTurret, x, y, NUM_ANGLES, 1, HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, classicTurret, &classicTurretSource, &classicTurretDest);
    }

    // Enhanced unit animations are complete vehicle frames, so a successful
    // draw replaces both the shared classic chassis and turret layers.
    // Dune2R treats the complete tank as a rigid sprite: movement follows the
    // chassis while stationary aiming and combat follow the weapon direction.
    if(drawEnhancedUnitSprite(x, y, drawnTurretAngle, drawnTurretAngle)) {
        if(isBadlyDamaged()) {
            drawSmoke(x, y);
        }
        return;
    }
    if(crossfading) {
        if(isBadlyDamaged()) {
            drawSmoke(x, y);
        }
        return;
    }

    SDL_Texture* pUnitGraphic = graphic[currentZoomlevel];
    SDL_Rect source1 = calcSpriteSourceRect(pUnitGraphic, drawnAngle, numImagesX);
    SDL_Rect dest1 = calcSpriteDrawingRect( pUnitGraphic, x, y, numImagesX, 1, HAlign::Center, VAlign::Center);

    if(!pGFXManager->drawHDObjPic(graphicID, getOwner()->getHouseID(), currentZoomlevel,
                                  drawnAngle, numImagesX, 0, 1, x, y)) {
        SDL_RenderCopy(renderer, pUnitGraphic, &source1, &dest1);
    }

    SDL_Texture* pTurretGraphic = turretGraphic[currentZoomlevel];
    SDL_Rect source2 = calcSpriteSourceRect(pTurretGraphic, drawnTurretAngle, NUM_ANGLES);
    SDL_Rect dest2 = calcSpriteDrawingRect( pTurretGraphic, x, y, NUM_ANGLES, 1, HAlign::Center, VAlign::Center);

    if(!pGFXManager->drawHDObjPic(gunGraphicID, getOwner()->getHouseID(), currentZoomlevel,
                                  drawnTurretAngle, NUM_ANGLES, 0, 1, x, y)) {
        SDL_RenderCopy(renderer, pTurretGraphic, &source2, &dest2);
    }

    if(isBadlyDamaged()) {
        drawSmoke(x, y);
    }
}

void Tank::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        Coord realPos(lround(realX), lround(realY));
        Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2,Explosion_Flames});
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

        if(isVisible(getOwner()->getTeamID()))
            soundPlayer->playSoundAt(Sound_ExplosionMedium,location);
    }

    TankBase::destroy();
}

void Tank::playAttackSound() {
    soundPlayer->playSoundAt(itemID == Unit_SlowTank ? Sound_RocketSmall : Sound_ExplosionSmall, location);
}
