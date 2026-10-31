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

#include <units/TrackedUnit.h>

#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>

#include <structures/RepairYard.h>
#include <structures/Refinery.h>
#include <structures/HarvesterDropoff.h>

#include <globals.h>

#include <House.h>
#include <Map.h>


TrackedUnit::TrackedUnit(House* newOwner) : GroundUnit(newOwner)
{
    TrackedUnit::init();
}

TrackedUnit::TrackedUnit(InputStream& stream) : GroundUnit(stream)
{
    TrackedUnit::init();
}

void TrackedUnit::init()
{
    tracked = true;
}

TrackedUnit::~TrackedUnit() = default;

void TrackedUnit::save(OutputStream& stream) const
{
    GroundUnit::save(stream);
}

void TrackedUnit::checkPos()
{
    GroundUnit::checkPos();

    if(active && justStoppedMoving) {
        auto* tile = currentGameMap->getTile(location.x, location.y);
        if(ModManager::instance().isTornieFamilyContentActive()) tile->squash(this);
        else tile->squash();
    }
}

bool TrackedUnit::canPass(int xPos, int yPos) const
{
    if(!currentGameMap->tileExists(xPos, yPos)) {
        return false;
    }

    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile->isMountain()) {
        return false;
    }

    if(pTile->hasAGroundObject()) {
        ObjectBase *pObject = pTile->getGroundObject();

        // Never block on our own tile – starting tile must stay passable
        if(pObject != nullptr && pObject->getObjectID() == getObjectID()) {
            return true;
        }

        if( (pObject != nullptr)
            && (pObject->getObjectID() == target.getObjectID())
            && targetFriendly
            && pObject->isAStructure()
            && (pObject->getOwner()->getTeamID() == owner->getTeamID())
            && pObject->isVisible(getOwner()->getTeamID()))
        {
            // are we entering a repair yard?
            if(goingToRepairYard && (pObject->getItemID() == Structure_RepairYard)) {
                return static_cast<const RepairYard*>(pObject)->isFree();
            } else if(isHarvesterLikeUnit(getItemID())) {
                return (harvesterIsReturning(this) && getHarvesterDropoff(pObject) != nullptr && getHarvesterDropoff(pObject)->isHarvesterDropoffFree());
            } else {
                return false;
            }
        } else {
            if (!pTile->hasANonInfantryGroundObject() && (pTile->getInfantryTeam() != getOwner()->getTeamID())) {
                // possibly squashing this unit
                return true;
            } else {
                return false;
            }
        }
    }

    return true;
}
