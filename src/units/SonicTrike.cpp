/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <units/SonicTrike.h>

#include <globals.h>

#include <Explosion.h>
#include <FileClasses/GFXManager.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SoundPlayer.h>

SonicTrike::SonicTrike(House* newOwner) : GroundUnit(newOwner) {
    SonicTrike::init();
    setHealth(getMaxHealth());
}

SonicTrike::SonicTrike(InputStream& stream) : GroundUnit(stream) {
    SonicTrike::init();
}

void SonicTrike::init() {
    itemID = Unit_SonicTrike;
    registerUnit();

    numWeapons = 1;
    bulletType = Bullet_SonicTrike;

    graphicID = ObjPic_SonicTrike;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

SonicTrike::~SonicTrike() = default;

void SonicTrike::destroy() {
    if(currentGameMap->tileExists(location) && isVisible()) {
        const Coord realPos(lround(realX), lround(realY));
        currentGame->getExplosionList().push_back(new Explosion(Explosion_SmallUnit, realPos, owner->getHouseID()));

        if(isVisible(getOwner()->getTeamID())) {
            soundPlayer->playSoundAt(Sound_ExplosionSmall, location);
        }
    }

    GroundUnit::destroy();
}

void SonicTrike::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                                 const DeviationReward::Provenance& provenance) {
    ObjectBase* damager = currentGame->getObjectManager().getObject(damagerID);

    if(!damager || damager->getItemID() != Unit_SonicTrike) {
        GroundUnit::handleDamage(damage, damagerID, damagerOwner, provenance);
    }
}

bool SonicTrike::canAttack(const ObjectBase* object) const {
    return object != nullptr && ObjectBase::canAttack(object) && object->getItemID() != Unit_SonicTrike;
}

void SonicTrike::playAttackSound() {
    soundPlayer->playSoundAt(Sound_Sonic, location);
}
