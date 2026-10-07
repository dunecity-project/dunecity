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

#include <units/Carryall.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <Game.h>
#include <SoundPlayer.h>

#include <structures/RepairYard.h>
#include <structures/Refinery.h>
#include <structures/HarvesterDropoff.h>
#include <structures/ConstructionYard.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/GroundUnit.h>
#include <units/CarryallFlight.h>

Carryall::Carryall(House* newOwner, int unitItemID) : AirUnit(newOwner)
{
    Carryall::init(unitItemID);

    setHealth(getMaxHealth());

    owned = true;

    aDropOfferer = false;
    droppedOffCargo = false;
    respondable = false;
}

Carryall::Carryall(InputStream& stream, int unitItemID) : AirUnit(stream)
{
    Carryall::init(unitItemID);

    pickedUpUnitList = stream.readUint32List();
    if(!pickedUpUnitList.empty()) {
        drawnFrame = 1;
    }

    stream.readBools(&owned, &aDropOfferer, &droppedOffCargo);
}

void Carryall::init(int unitItemID)
{
    itemID = unitItemID;
    registerUnit();

    canAttackStuff = false;

    graphicID = ObjPic_Carryall;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    shadowGraphic = pGFXManager->getObjPic(ObjPic_CarryallShadow,getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 2;
}

Carryall::~Carryall()
{
    ;
}

void Carryall::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeUint32List(pickedUpUnitList);

    stream.writeBools(owned, aDropOfferer, droppedOffCargo);
}

bool Carryall::update() {
    // Carryalls do not keep a house alive. Remove survivors on their own
    // update instead of mutating the unit list recursively from House::lose.
    // The normal destruction path releases bookings and disposes of cargo.
    if (owner && !owner->isAlive()) {
        destroy();
        return false;
    }

    if(AirUnit::update() == false) {
        return false;
    }

    // check if this carryall has to be removed because it has just brought something
    // to the map (e.g. new harvester)
    if (active) {
        if(aDropOfferer && droppedOffCargo && (hasCargo() == false)
            && ((getRealX() < -TILESIZE) || (getRealX() > (currentGameMap->getSizeX()+1)*TILESIZE)
                || (getRealY() < -TILESIZE) || (getRealY() > (currentGameMap->getSizeY()+1)*TILESIZE))) {
            
            // CRITICAL: Clear any bookings BEFORE leaving map
            // Harvesters may have booked this delivery carryall while it was flying away
            if(StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer())) {
                dropoff->unbookHarvesterDropoff();
            }
            if(targetFriendly && target.getObjPointer() && target.getObjPointer()->isAGroundUnit()) {
                GroundUnit* ground = static_cast<GroundUnit*>(target.getObjPointer());
                if(ground->getCarrier() == this) {
                    ground->bookCarrier(nullptr);
                }
            }
            // Also scan for any unit that may have booked us
            for(UnitBase* pUnit : unitList) {
                if(pUnit->isAGroundUnit()) {
                    GroundUnit* ground = static_cast<GroundUnit*>(pUnit);
                    if(ground->getCarrier() == this) {
                        ground->bookCarrier(nullptr);
                    }
                }
            }
            
            setVisible(VIS_ALL, false);
            destroy();
            return false;
        }
    }
    return true;
}

void Carryall::deploy(const Coord& newLocation) {
    AirUnit::deploy(newLocation);

    respondable = false;
}

void Carryall::checkPos()
{
    AirUnit::checkPos();

    if (active) {
        if (hasCargo()) {
            if(destination.isValid() && CarryallFlight::distance(
                    destination.x*TILESIZE + TILESIZE/2 - realX,
                    destination.y*TILESIZE + TILESIZE/2 - realY) < TILESIZE/8) {
                // drop up to 3 infantry units at once or one other unit
                int droppedUnits = 0;
                do {
                    Uint32 unitID = pickedUpUnitList.front();
                    UnitBase* pUnit = static_cast<UnitBase*>(currentGame->getObjectManager().getObject(unitID));

                    if(pUnit == nullptr) {
                        // An entry that no longer resolves cannot be flown anywhere. Returning
                        // here left it at the head of the list forever, which kept the aircraft
                        // booked with cargo it could never put down.
                        releaseCargoId(unitID);
                        continue;
                    }

                    if((pUnit != nullptr) && (pUnit->isInfantry() == false) && (droppedUnits > 0)) {
                        // we already dropped infantry and this is no infantry
                        // => do not drop this here
                        break;
                    }

                    deployUnit(unitID);
                    droppedUnits++;

                    if((pUnit != nullptr) && (pUnit->isInfantry() == false)) {
                        // we dropped a non infantry unit
                        // => do not drop another unit
                        break;
                    }
                } while(hasCargo() && (droppedUnits < 3));

                if(pickedUpUnitList.empty() == false) {
                    // find next place to drop
                    for(int i=8;i<18;i++) {
                        int r = currentGame->randomGen.rand(3,i/2);
                        FixPoint angle = 2 * FixPt_PI * currentGame->randomGen.randFixPoint();

                        Coord dropCoord = location + Coord( lround(r*FixPoint::sin(angle)), lround(-r*FixPoint::cos(angle)));
                        if(currentGameMap->tileExists(dropCoord) && currentGameMap->getTile(dropCoord)->hasAGroundObject() == false) {
                            setDestination(dropCoord);
                            break;
                        }
                    }
                } else {
                    setTarget(nullptr);
                    setDestination(guardPoint);
                }
            }
        } else if(isBooked() == false) {
            if(destination.isValid()) {
                if(blockDistance(location, destination) <= 2) {
                    destination.invalidate();
                }
            } else {
                if(blockDistance(location, guardPoint) > 17) {
                    setDestination(guardPoint);
                }
            }
        }
    }

}

void Carryall::deployUnit(Uint32 unitID)
{
    bool found = false;

    for(const Uint32& pickedUpUnitID : pickedUpUnitList) {
        if(pickedUpUnitID == unitID) {
            found = true;
            break;
        }
    }

    if(found == false) {
        return;
    }

    pickedUpUnitList.remove(unitID);

    soundPlayer->playSoundAt(Sound_Drop, location);

    UnitBase* pUnit = static_cast<UnitBase*>(currentGame->getObjectManager().getObject(unitID));

    if(pUnit == nullptr) {
        return;
    }

    if (found) {
        currentMaxSpeed = 0;
        setSpeeds();

        if (currentGameMap->getTile(location)->hasANonInfantryGroundObject()) {
            ObjectBase* object = currentGameMap->getTile(location)->getNonInfantryGroundObject();
            // A deviated Carryall keeps flying its original owner's cargo, so the
            // temporary owner's repair yard or refinery must not accept it. Cargo
            // that is still hostile is simply dropped on the ground instead.
            if (object->getOwner() == getOwner()
                && object->getOwner()->getTeamID() == pUnit->getOwner()->getTeamID()) {
                // Deviated cargo is dropped on the ground as well: the captor does not get to
                // repair a unit it only borrowed, and the bay stays free and unbooked.
                if (object->getItemID() == Structure_RepairYard && pUnit->isEligibleForRepair()) {
                    if (static_cast<RepairYard*>(object)->isFree()) {
                        pUnit->setTarget(object);   // unit books repair yard again
                        pUnit->setGettingRepaired();
                        pUnit = nullptr;
                    } else {
                        // unit is still going to repair yard but was unbooked from repair yard at pickup => book now
                        static_cast<RepairYard*>(object)->book();
                    }
                } else if (getHarvesterDropoff(object) != nullptr && isHarvesterLikeUnit(pUnit->getItemID())) {
                    if (getHarvesterDropoff(object)->isHarvesterDropoffFree()) {
                        pUnit->setTarget(object);
                        harvesterSetReturned(pUnit);
                        pUnit = nullptr;
                        goingToRepairYard = false;
                    }
                }
            }
        }

        if(pUnit != nullptr) {
            pUnit->setAngle(drawnAngle);
            Coord deployPos = currentGameMap->findDeploySpot(pUnit, location, currentGame->randomGen);
            pUnit->setForced(false); // Stop units being forced if they are deployed
            pUnit->deploy(deployPos);
            if(pUnit->getItemID() == Unit_Saboteur) {
                pUnit->doSetAttackMode(HUNT);
            } else if(!isHarvesterLikeUnit(pUnit->getItemID())) {
                pUnit->doSetAttackMode(AREAGUARD);
            } else {
                pUnit->doSetAttackMode(HARVEST);
            }
        }

        if (pickedUpUnitList.empty()) {
            if(!aDropOfferer) {
                setTarget(nullptr);
                setDestination(guardPoint);
            }
            droppedOffCargo = true;
            drawnFrame = 0;

            clearPath();
        }
    }
}

void Carryall::releaseCargoId(Uint32 unitID)
{
    const size_t before = pickedUpUnitList.size();
    pickedUpUnitList.remove(unitID);
    if(pickedUpUnitList.size() == before) {
        return;
    }

    if(pickedUpUnitList.empty()) {
        if(!aDropOfferer) {
            setTarget(nullptr);
            setDestination(guardPoint);
        }
        droppedOffCargo = true;
        drawnFrame = 0;

        clearPath();
    }
}

bool Carryall::releaseExtraCargoId(Uint32 unitID)
{
    bool kept = false;
    bool removed = false;
    for(auto it = pickedUpUnitList.begin(); it != pickedUpUnitList.end(); ) {
        if(*it != unitID) {
            ++it;
        } else if(!kept) {
            kept = true;   // The first entry is the actual claim.
            ++it;
        } else {
            it = pickedUpUnitList.erase(it);
            removed = true;
        }
    }
    return removed;
}

void Carryall::destroy()
{
    // Clean up bookings to prevent stranded units/refineries
    if(StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer())) {
        dropoff->unbookHarvesterDropoff();
    }
    if(targetFriendly && target.getObjPointer() && target.getObjPointer()->isAGroundUnit()) {
        GroundUnit* ground = static_cast<GroundUnit*>(target.getObjPointer());
        if(ground->getCarrier() == this) {
            ground->bookCarrier(nullptr);
        }
    }

    // destroy cargo (unless immortal)
    GameType gameType = currentGame->getGameInitSettings().getGameType();
    bool immortalityEnabled = (!isNetworkGameType(gameType)
                              && gameType != GameType::LoadMultiplayer
                              && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer);
    
    for(const Uint32& pickedUpUnitID : pickedUpUnitList) {
        UnitBase* pPickedUpUnit = static_cast<UnitBase*>(currentGame->getObjectManager().getObject(pickedUpUnitID));
        if(pPickedUpUnit != nullptr) {
            bool cargoIsImmortal = immortalityEnabled && (pPickedUpUnit->getOwner() == pLocalHouse);
            if(!cargoIsImmortal) {
                pPickedUpUnit->destroy();
            } else {
                // Drop immortal unit at carryall's location instead of destroying it
                pPickedUpUnit->deploy(location);
            }
        }
    }
    pickedUpUnitList.clear();

    // place wreck
    if(isVisible() && currentGameMap->tileExists(location)) {
        Tile* pTile = currentGameMap->getTile(location);
        pTile->assignDeadUnit(DeadUnit_Carrall, owner->getHouseID(), Coord(lround(realX), lround(realY)));
    }

    AirUnit::destroy();
}

void Carryall::releaseTarget() {
    setTarget(nullptr);

    if(!hasCargo()) {
        setDestination(guardPoint);
    }
}

void Carryall::engageTarget()
{
    ObjectBase* pTarget = target.getObjPointer();
    if(pTarget == nullptr) {
        // the target does not exist anymore
        releaseTarget();
        return;
    }

    if(pTarget->isActive() == false) {
        // the target changed its state to inactive
        releaseTarget();
        return;
    }

    if(pTarget->isAGroundUnit() && !static_cast<GroundUnit*>(pTarget)->isAwaitingPickup()) {
        // the target changed its state to not awaiting pickup anymore
        releaseTarget();
        return;
    }

    if(pTarget->getOwner()->getTeamID() != owner->getTeamID()) {
        // the target changed its owner (e.g. was deviated)
        releaseTarget();
        return;
    }

    Coord targetLocation;
    if(getHarvesterDropoff(pTarget) != nullptr) {
        targetLocation = pTarget->getLocation() + Coord(getHarvesterDropoff(pTarget)->getStructureSizeX() - 1, 0);
    } else if(pTarget->isAUnit()) {
        // For units (like harvesters), use their exact location, not getClosestPoint
        // getClosestPoint recalculates every frame causing circular flight patterns
        targetLocation = pTarget->getLocation();
    } else {
        // For multi-tile structures, use closest point
        targetLocation = pTarget->getClosestPoint(location);
    }

    // Targeting only selects the destination or transfers cargo. All position
    // changes happen once in move(), including the slow final docking phase.
    setDestination(targetLocation);
    FixPoint targetX, targetY;
    getFlightDestination(targetX, targetY);
    targetDistance = CarryallFlight::distance(targetX-realX, targetY-realY);
    if (targetDistance < TILESIZE/8) {
        if(hasCargo()) {
            if(pTarget->isAStructure()) {
                while(pickedUpUnitList.begin() != pickedUpUnitList.end()) {
                    deployUnit(*(pickedUpUnitList.begin()) );
                }

                setTarget(nullptr);
                setDestination(guardPoint);
            }
        } else {
            pickupTarget();
        }
    }
}

bool Carryall::getFlightDestination(FixPoint& x, FixPoint& y) const {
    if(const auto* object = target.getObjPointer(); object && object->isAUnit()) {
        x = object->getRealX(); y = object->getRealY();
        return true;
    }
    if((target || hasCargo()) && destination.isValid()) {
        x = destination.x*TILESIZE + TILESIZE/2;
        y = destination.y*TILESIZE + TILESIZE/2;
        return true;
    }
    return false;
}

FixPoint Carryall::getDestinationAngle() const {
    FixPoint x, y;
    if(getFlightDestination(x,y))
        return destinationAngleRad(realX,realY,x,y)*8/(FixPt_PI << 1);
    return AirUnit::getDestinationAngle();
}

void Carryall::move() {
    const FixPoint cruise = currentGame->objectData.data[itemID][originalHouseID].maxspeed;
    FixPoint x, y;
    if(!getFlightDestination(x,y)) {
        currentMaxSpeed = std::min(currentMaxSpeed + 0.2_fix, cruise);
        AirUnit::move();
        return;
    }
    const FixPoint dist = CarryallFlight::distance(x-realX,y-realY);
    if(dist >= TILESIZE/2) {
        FixPoint error = FixPoint::abs(getDestinationAngle()-angle);
        error = std::min(error, FixPoint(NUM_ANGLES)-error);
        currentMaxSpeed = CarryallFlight::approachSpeed(dist,error,cruise);
        AirUnit::move();
        return;
    }

    // Stop forward flight before moving directly toward the pickup/drop site.
    // Dynasty's delayed script has a 0.25 tile/s per-axis docking rate at
    // normal speed. Spread that movement over our updates without a teleport.
    currentMaxSpeed = 0;
    const FixPoint step = CarryallFlight::finalStep(cruise);
    realX += std::clamp(x-realX,-step,step);
    realY += std::clamp(y-realY,-step,step);
    const Coord next(realX.lround()/TILESIZE,realY.lround()/TILESIZE);
    if(next != location) {
        unassignFromMap(location);
        assignToMap(next);
        if(auto* grid = currentGame->getSpatialGrid())
            grid->move(*this,getGridHandle(),location,next);
        location = next;
    }
    checkPos();
}

void Carryall::giveCargo(UnitBase* newUnit)
{
    if(newUnit == nullptr) {
        return;
    }

    pickedUpUnitList.push_back(newUnit->getObjectID());

    newUnit->setPickedUp(this);

    drawnFrame = 1;

    droppedOffCargo = false;
}

void Carryall::pickupTarget()
{
    currentMaxSpeed = 0;
    setSpeeds();

    ObjectBase* pTarget = target.getObjPointer();
    if(pTarget == nullptr) {
        releaseTarget();
        return;
    }

    if(pTarget->isAGroundUnit()) {
        GroundUnit* pGroundUnitTarget = static_cast<GroundUnit*>(pTarget);

        if(pTarget->getHealth() <= 0) {
            // unit died just in the moment we tried to pick it up => carryall also crushes
            // Check if carryall itself is immortal
            GameType gameType = currentGame->getGameInitSettings().getGameType();
            bool isImmortal = (!isNetworkGameType(gameType)
                              && gameType != GameType::LoadMultiplayer
                              && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer
                              && getOwner() == pLocalHouse);
            
            if(!isImmortal) {
                setHealth(0);
            }
            return;
        }

        if (  pGroundUnitTarget->hasATarget()
            || ( pGroundUnitTarget->getDestination() != pGroundUnitTarget->getLocation())
            || pGroundUnitTarget->isBadlyDamaged()
            || pGroundUnitTarget->isAwaitingPickup()) {

            // The passenger is identified once, before anything can run. doRepair() below issues
            // a forced move, which cancels the booking that brought this aircraft here and with
            // it our own target; reading target again afterwards enrolled NONE_ID and left the
            // passenger hidden inside nothing.
            const Uint32 passengerID = pGroundUnitTarget->getObjectID();

            if(pGroundUnitTarget->isBadlyDamaged() || (pGroundUnitTarget->hasATarget() == false && !isHarvesterLikeUnit(pGroundUnitTarget->getItemID())))   {
                pGroundUnitTarget->doRepair();
            }

            // The repair trip asks for a carryall again, and that request can land on a different
            // aircraft. Exactly one carrier owns a passenger: this one is about to take it, so any
            // replacement booking is released now, while the unit is still something the other
            // aircraft's own checks can see.
            pGroundUnitTarget->releaseReplacementPickup(this);

            ObjectBase* newTarget = pGroundUnitTarget->hasATarget() ? pGroundUnitTarget->getTarget() : nullptr;

            pickedUpUnitList.push_back(passengerID);
            pGroundUnitTarget->setPickedUp(this);

            drawnFrame = 1;

            if(newTarget && getHarvesterDropoff(newTarget) != nullptr) {
                pGroundUnitTarget->setGuardPoint(pGroundUnitTarget->getLocation());
                setTarget(newTarget);
                ObjectBase* pNewTarget = target.getObjPointer();
                if(pNewTarget != nullptr) {
                    setDestination(pNewTarget->getLocation() + Coord(getHarvesterDropoff(pNewTarget)->getStructureSizeX() - 1, 0));
                } else {
                    releaseTarget();
                }
            } else if(newTarget && (newTarget->getItemID() == Structure_RepairYard)) {
                pGroundUnitTarget->setGuardPoint(pGroundUnitTarget->getLocation());
                setTarget(newTarget);
                ObjectBase* pNewTarget = target.getObjPointer();
                if(pNewTarget != nullptr) {
                    setDestination(pNewTarget->getClosestPoint(location));
                } else {
                    releaseTarget();
                }
            } else if (pGroundUnitTarget->getDestination().isValid()) {
                setDestination(pGroundUnitTarget->getDestination());
            }

            clearPath();

        } else {
            pGroundUnitTarget->setAwaitingPickup(false);
            if(pGroundUnitTarget->getAttackMode() == CARRYALLREQUESTED) {
                pGroundUnitTarget->doSetAttackMode(STOP);
            }
            releaseTarget();
        }
    } else {
        // get unit from structure
        if(getHarvesterDropoff(pTarget) != nullptr) {
            // get a harvester stored by a compatible drop-off structure
            getHarvesterDropoff(pTarget)->deployContainedHarvester(this);
        } else if(pTarget->getItemID() == Structure_RepairYard) {
            // get repaired unit
            static_cast<RepairYard*>(pTarget)->deployRepairUnit(this);
        }
    }
}

void Carryall::setTarget(const ObjectBase* newTarget) {
    ObjectBase* oldTarget = target.getObjPointer();
    if(oldTarget != nullptr
        && targetFriendly
        && oldTarget->isAGroundUnit()
        && (static_cast<GroundUnit*>(oldTarget)->getCarrier() == this))
    {
        static_cast<GroundUnit*>(oldTarget)->bookCarrier(nullptr);
    }

    if(StructureBase* oldDropoff = getHarvesterDropoff(oldTarget)) {
        oldDropoff->unbookHarvesterDropoff();
    }

    UnitBase::setTarget(newTarget);

    ObjectBase* pTarget = target.getObjPointer();
    if(StructureBase* newDropoff = getHarvesterDropoff(pTarget))
    {
        newDropoff->bookHarvesterDropoff();
    }

    if(pTarget != nullptr && targetFriendly && pTarget->isAGroundUnit()) {
        static_cast<GroundUnit*>(pTarget)->setAwaitingPickup(true);
    }
}

void Carryall::targeting() {
    if(target) {
        engageTarget();
    }
}

void Carryall::turn() {
    if (active && aDropOfferer && droppedOffCargo && (hasCargo() == false)
        && ((getRealX() < TILESIZE/2) || (getRealX() > currentGameMap->getSizeX()*TILESIZE - TILESIZE/2)
            || (getRealY() < TILESIZE/2) || (getRealY() > currentGameMap->getSizeY()*TILESIZE - TILESIZE/2))) {
        // already partially outside the map => do not turn
        return;
    }

    AirUnit::turn();
}
