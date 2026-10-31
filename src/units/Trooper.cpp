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

#include <units/Trooper.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <mod/ModManager.h>
#include <SoundPlayer.h>
#include <units/AmbientAirplane.h>
#include <Game.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/exceptions.h>

Trooper::Trooper(House* newOwner, int product) : InfantryBase(newOwner) {
    Trooper::init(product);

    setHealth(getMaxHealth());
}

Trooper::Trooper(InputStream& stream, int product) : InfantryBase(stream) {
    Trooper::init(product);
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_TORNIE546) {
        flameDamageRemainder = stream.readUint8();
        if(flameDamageRemainder >= 5) THROW(std::runtime_error, "Invalid flame damage remainder");
    }
}

void Trooper::init(int product) {
    itemID = product;
    registerUnit();

    numWeapons = 1;
    setProductionHouseID(getProductionHouseID());

    graphicID = ObjPic_Trooper;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());

    numImagesX = 4;
    numImagesY = 3;
}

Trooper::~Trooper() = default;

void Trooper::save(OutputStream& stream) const {
    InfantryBase::save(stream);
    stream.writeUint8(flameDamageRemainder);
}

Sint32 Trooper::flameWeaponDamage(Sint32 base) {
    // Preserve fractional fifths without consuming additional synchronized RNG.
    const Sint64 scaled = Sint64(base) * 6 + flameDamageRemainder;
    flameDamageRemainder = static_cast<Uint8>(scaled % 5);
    return static_cast<Sint32>(scaled / 5);
}

void Trooper::setProductionHouseID(int houseID) {
    UnitBase::setProductionHouseID(houseID);
    const bool family = ModManager::instance().isTornieFamilyContentActive();
    const bool kleshmershTrooper = family
        && ModManager::instance().isTornieCustomUnitsAndBuildingsEnabled()
        && isHouseFaction(static_cast<HOUSETYPE>(getProductionHouseID()), HOUSE_KLESHMERSH);
    bulletType = (family && itemID == Unit_FlameTrooper) || kleshmershTrooper
        ? Bullet_Flame : Bullet_SmallRocket;
}

bool Trooper::canAttack(const ObjectBase* object) const {
    if ((object != nullptr)
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || (object->getItemID() == Unit_Sandworm))
        && object->isVisible(getOwner()->getTeamID())
        && !AmbientAirplane::crashEndangers(*this, *object))
    {
        return true;
    }
    else
        return false;
}


void Trooper::playAttackSound() {
    soundPlayer->playSoundAt(
        lastFiredBulletType == Bullet_ShellSmall ? Sound_Gun : Sound_RocketSmall,
        location);
}
