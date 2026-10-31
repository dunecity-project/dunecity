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

#include <units/FlameTank.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <ScreenBorder.h>
#include <SoundPlayer.h>
#include <mod/ModManager.h>


FlameTank::FlameTank(House* newOwner) : TrackedUnit(newOwner) {
    FlameTank::init();

    setHealth(getMaxHealth());
}

FlameTank::FlameTank(InputStream& stream) : TrackedUnit(stream) {
    FlameTank::init();
}

void FlameTank::init() {
    itemID = Unit_FlameTank;
    registerUnit();

    numWeapons = 1;
    bulletType = Bullet_Flame;

    graphicID = ObjPic_Tank_Base;
    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieFamilyContentActive();
    gunGraphicID = tornieActive ? ObjPic_FlameTankGunTornie : ObjPic_Launcher_Gun;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    turretGraphic = pGFXManager->getObjPic(gunGraphicID, tornieActive ? HOUSE_HARKONNEN : getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

FlameTank::~FlameTank() = default;

void FlameTank::blitToScreen() {
    int x1 = screenborder->world2screenX(realX);
    int y1 = screenborder->world2screenY(realY);

    SDL_Texture* pUnitGraphic = graphic[currentZoomlevel];
    SDL_Rect source1 = calcSpriteSourceRect(pUnitGraphic, drawnAngle, numImagesX);
    SDL_Rect dest1 = calcSpriteDrawingRect(pUnitGraphic, x1, y1, numImagesX, 1, HAlign::Center, VAlign::Center);

    SDL_RenderCopy(renderer, pUnitGraphic, &source1, &dest1);

    const Coord launcherTurretOffset[] =    {   Coord(0, -12),
                                                Coord(0, -8),
                                                Coord(0, -8),
                                                Coord(0, -8),
                                                Coord(0, -12),
                                                Coord(0, -8),
                                                Coord(0, -8),
                                                Coord(0, -8)
                                            };

    SDL_Texture* pTurretGraphic = turretGraphic[currentZoomlevel];
    SDL_Rect source2 = calcSpriteSourceRect(pTurretGraphic, drawnAngle, numImagesX);
    SDL_Rect dest2 = calcSpriteDrawingRect( pTurretGraphic,
                                            screenborder->world2screenX(realX + launcherTurretOffset[drawnAngle].x),
                                            screenborder->world2screenY(realY + launcherTurretOffset[drawnAngle].y),
                                            numImagesX, 1, HAlign::Center, VAlign::Center);

    SDL_RenderCopy(renderer, pTurretGraphic, &source2, &dest2);

    if(isBadlyDamaged()) {
        drawSmoke(x1, y1);
    }
}

void FlameTank::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        Coord realPos(lround(realX), lround(realY));

        // Use the weapon-impact flames only. Explosion_Flames is the burning
        // vehicle-wreck animation and produced three visible carcasses here.
        for(int i = 0; i < 3; i++) {
            Coord flamePos = realPos;
            if(i > 0) {
                flamePos.x += currentGame->randomGen.rand(-TILESIZE/3, TILESIZE/3);
                flamePos.y += currentGame->randomGen.rand(-TILESIZE/3, TILESIZE/3);
            }
            currentGame->getExplosionList().push_back(
                new Explosion(Explosion_FlameImpact, flamePos, owner->getHouseID()));
        }

        if(isVisible(getOwner()->getTeamID())) {
            screenborder->shakeScreen(6);
            soundPlayer->playSoundAt(Sound_ExplosionSmall, location);
        }
    }

    TrackedUnit::destroy();
}

void FlameTank::playAttackSound() {
    soundPlayer->playSoundAt(Sound_Rocket, location);
}
