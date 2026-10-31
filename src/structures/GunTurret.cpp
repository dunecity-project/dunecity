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

#include <structures/GunTurret.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <House.h>

GunTurret::GunTurret(House* newOwner, int unitType) : TurretBase(newOwner) {
    GunTurret::init(unitType);

    setHealth(getMaxHealth());
}

GunTurret::GunTurret(InputStream& stream, int unitType) : TurretBase(stream) {
    GunTurret::init(unitType);
}

void GunTurret::init(int unitType) {
    itemID = unitType;
    owner->incrementStructures(itemID);

    attackSound = itemID == Structure_Slowpost ? Sound_RocketSmall : Sound_ExplosionSmall;
    bulletType = itemID == Structure_Slowpost ? Bullet_Slow : Bullet_ShellTurret;

    graphicID = itemID == Structure_Slowpost ? ObjPic_Slowpost : ObjPic_GunTurret;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 10;
    numImagesY = 1;
    curAnimFrame = firstAnimFrame = lastAnimFrame = ((10-drawnAngle) % 8) + 2;
}

GunTurret::~GunTurret() = default;
