/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tornie Flame Quad behavior by Tornie_Panther, adapted for DuneCity.
 */
#include <units/FlameQuad.h>

#include <globals.h>
#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <SoundPlayer.h>

FlameQuad::FlameQuad(House* newOwner) : GroundUnit(newOwner) {
    FlameQuad::init();
    setHealth(getMaxHealth());
}

FlameQuad::FlameQuad(InputStream& stream) : GroundUnit(stream) {
    FlameQuad::init();
}

void FlameQuad::init() {
    itemID = Unit_FlameQuad;
    registerUnit();
    numWeapons = 2;
    bulletType = Bullet_Flame;
    graphicID = ObjPic_FlameQuad;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

FlameQuad::~FlameQuad() = default;

void FlameQuad::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        const Coord realPos(lround(realX), lround(realY));
        currentGame->getExplosionList().push_back(new Explosion(Explosion_SmallUnit, realPos, owner->getHouseID()));
        if(isVisible(getOwner()->getTeamID())) soundPlayer->playSoundAt(Sound_ExplosionSmall, location);
    }
    GroundUnit::destroy();
}

void FlameQuad::playAttackSound() {
    soundPlayer->playSoundAt(Sound_RocketSmall, location);
}
