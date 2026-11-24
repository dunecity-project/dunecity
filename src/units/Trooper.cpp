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

#include <algorithm>
#include <vector>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <mod/ModManager.h>
#include <SoundPlayer.h>
#include <Game.h>
#include <Map.h>
#include <SpatialGrid.h>
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
        && object->isVisible(getOwner()->getTeamID()))
    {
        return true;
    }
    else
        return false;
}

bool Trooper::antiAirRuleActive() {
    return currentGame->getGameInitSettings().getGameOptions().trooperAntiAir
        && ModManager::instance().isDuneCityContentActive();
}

bool Trooper::ornithopterReachActive() const {
    return bulletType == Bullet_SmallRocket && antiAirRuleActive()
        && currentGame->getGameInitSettings().getGameOptions().trooperOrnithopterReach;
}

// The Ornithopter reach (match rule trooperOrnithopterReach, part of Trooper Anti-Air): a rocket
// Trooper answers an Ornithopter anywhere in its weapon range. An Ornithopter crosses that range
// at about eight tiles a second, so three engine rules kept the answer to moments when it was
// nearly overhead:
//  - the target search only takes what stands on ground the team sees, and a Trooper sees one
//    tile (targetsWithoutGroundSight);
//  - a Trooper without a target searches once a second, and on Guard only inside its weapon
//    range, while an Ornithopter strafing something nearby is inside it for a fifth of a second
//    per pass (targeting);
//  - it had to finish turning, up to 0.7 s, before it fired (firesAtAircraftUnfaced).
// Other aircraft, every ground target, a Trooper that is moving on a plain move order or waits
// in Ambush, and a flame-armed Trooper keep the old rules.
bool Trooper::targetsWithoutGroundSight(const ObjectBase& candidate) const {
    // As for the rocket turret (RocketTurret::findTarget), an Ornithopter in the sky needs no
    // unveiled ground below it.
    return isOrnithopterUnit(candidate.getItemID()) && ornithopterReachActive() && isInWeaponRange(&candidate);
}

bool Trooper::firesAtAircraftUnfaced(const ObjectBase& aircraft) const {
    return isOrnithopterUnit(aircraft.getItemID()) && ornithopterReachActive();
}

const ObjectBase* Trooper::ornithopterInRange() const {
    // In most cycles of most matches no enemy flies one, and nothing more is done.
    bool enemyFlies = false;
    for(int h = 0; h < NUM_HOUSES && !enemyFlies; ++h) {
        const House* house = currentGame->getHouse(h);
        enemyFlies = house != nullptr && house->getTeamID() != owner->getTeamID()
            && house->getNumItems(Unit_Ornithopter) + house->getNumItems(Unit_EliteOrnithopter) > 0;
    }
    if(!enemyFlies) return nullptr;

    const ObjectBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    auto consider = [&](const ObjectBase* candidate) {
        if(candidate == nullptr || !isOrnithopterUnit(candidate->getItemID()) || candidate->getHealth() <= 0
           || !canAttack(candidate) || !isInWeaponRange(candidate) || !isInAttackRange(candidate)) return;
        const FixPoint distance = blockDistance(location, candidate->getLocation());
        // The lower object ID decides a tie, so every peer picks the same aircraft.
        if(distance < bestDistance
           || (distance == bestDistance && candidate->getObjectID() < best->getObjectID())) {
            best = candidate;
            bestDistance = distance;
        }
    };
    if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
        const int range = getWeaponRange();
        const Coord low = grid->clampToCell(Coord(std::max(0, location.x - range), std::max(0, location.y - range)));
        const Coord high = grid->clampToCell(Coord(std::min(currentGameMap->getSizeX() - 1, location.x + range),
                                                   std::min(currentGameMap->getSizeY() - 1, location.y + range)));
        std::vector<SpatialGridEntry> entries;
        for(int y = low.y; y <= high.y; ++y) {
            for(int x = low.x; x <= high.x; ++x) {
                const Coord cell(x, y);
                entries.clear();
                if(!grid->isCellCoordValid(cell) || !grid->collectEntries(cell, entries)) continue;
                for(const SpatialGridEntry& entry : entries) {
                    if(entry.object != nullptr
                       && entry.object->getGridHandle().matches(entry.key.objectId, entry.key.generation))
                        consider(entry.object);
                }
            }
        }
    } else {
        for(const UnitBase* unit : unitList) consider(unit);
    }
    return best;
}

void Trooper::targeting() {
    // Between its one-a-second searches an idle Trooper still notices an Ornithopter the moment
    // it comes into range. A Trooper with a target or an order to fire at ground keeps it.
    const bool mayFight = attackMode == GUARD || attackMode == AREAGUARD || attackMode == HUNT || attackMode == HOLD;
    if(mayFight && !target && !attackPos && !forced && ornithopterReachActive()
       && ((!moving && !justStoppedMoving) || isAttackMoving())) {
        if(const ObjectBase* aircraft = ornithopterInRange()) {
            // As the engine's own attack-move acquisition does: remember where the walk left off.
            if(isAttackMoving()) attackMoveBreakOff = location;
            doAttackObject(aircraft, false);
        }
    }
    InfantryBase::targeting();
}


void Trooper::playAttackSound() {
    soundPlayer->playSoundAt(
        lastFiredBulletType == Bullet_ShellSmall ? Sound_Gun : Sound_RocketSmall,
        location);
}
