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

#include <units/GroundUnit.h>

#include <globals.h>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SoundPlayer.h>

#include <players/HumanPlayer.h>

#include <structures/RepairYard.h>
#include <units/Carryall.h>
#include <units/HarvesterHelpers.h>
#include <units/UnitMovementPolicy.h>

GroundUnit::GroundUnit(House* newOwner) : UnitBase(newOwner) {

    GroundUnit::init();

    awaitingPickup = false;
    bookedCarrier = NONE_ID;
}

GroundUnit::GroundUnit(InputStream& stream) : UnitBase(stream) {

    GroundUnit::init();

    awaitingPickup = stream.readBool();
    bookedCarrier = stream.readUint32();
}

void GroundUnit::init() {
    aGroundUnit = true;
}

GroundUnit::~GroundUnit() = default;

void GroundUnit::save(OutputStream& stream) const {
    UnitBase::save(stream);

    stream.writeBool(awaitingPickup);
    stream.writeUint32(bookedCarrier);
}

void GroundUnit::assignToMap(const Coord& pos) {
    if (currentGameMap->tileExists(pos)) {
        currentGameMap->getTile(pos)->assignNonInfantryGroundObject(getObjectID());
        currentGameMap->viewMap(owner->getHouseID(), pos, getViewRange());
    }
}

void GroundUnit::checkPos() {
    auto* pTile = currentGameMap->getTile(location);
    if(!moving && !justStoppedMoving && !isInfantry()) {
        pTile->setTrack(drawnAngle);
    }

    // Clear stale carryall bookings - harvester waiting for carryall that's gone
    if(awaitingPickup && !hasBookedCarrier()) {
        awaitingPickup = false;
        bookedCarrier = NONE_ID;
    }

    if(justStoppedMoving)
    {
        realX = location.x*TILESIZE + TILESIZE/2;
        realY = location.y*TILESIZE + TILESIZE/2;
        //findTargetTimer = 0;  //allow a scan for new targets now

        if(pTile->isSpiceBloom()) {
            // Trigger the bloom explosion (creates spice)
            pTile->triggerSpiceBloom(getOwner());
            
            // Check if unit should be destroyed by the bloom
            GameType gameType = currentGame->getGameInitSettings().getGameType();
            bool isImmortal = (!isNetworkGameType(gameType)
                              && gameType != GameType::LoadMultiplayer
                              && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer
                              && getOwner() == pLocalHouse);
            
            if(!isImmortal) {
                // Normal behavior: unit is destroyed by spice bloom
                setHealth(0);
                setVisible(VIS_ALL, false);
            }
        } else if(pTile->isSpecialBloom()){
            pTile->triggerSpecialBloom(getOwner());
        }
    }

    /*
        Go to repair yard if low on health
    */
    if(active && (getHealth() < getMaxHealth()/2)
            && !goingToRepairYard
            && owner->hasRepairYard()
            && !pickedUp
            && owner->hasCarryalls()
            && isEligibleForRepair() // stop deviated units from being repaired
            && !isInfantry()
            && !forced ) { // Stefan - Allow units with targets to be picked up for repairs

        doRepair();

    }


    if(goingToRepairYard) {
        if(target.getObjPointer() == nullptr) {
            goingToRepairYard = false;
            awaitingPickup = false;
            bookedCarrier = NONE_ID;

            clearPath();
        } else {
            ObjectBase *pObject = pTile->getGroundObject();

            if( justStoppedMoving
                && (pObject != nullptr)
                && (pObject->getObjectID() == target.getObjectID())
                && (target.getObjPointer()->getItemID() == Structure_RepairYard))
            {
                RepairYard* pRepairYard = static_cast<RepairYard*>(target.getObjPointer());
                if(pRepairYard->isFree()) {
                    setGettingRepaired();
                } else {
                    // the repair yard is already in use by some other unit => move out
                    Coord newDestination = currentGameMap->findDeploySpot(this, target.getObjPointer()->getLocation(), currentGame->randomGen, getLocation(), pRepairYard->getStructureSize());
                    doMove2Pos(newDestination, true);
                }
            }
        }
    }

    // If we are awaiting a pickup try book a carryall if we have one
    if(!pickedUp && attackMode == CARRYALLREQUESTED && bookedCarrier == NONE_ID) {
        if(getOwner()->hasCarryalls() && (target || (destination != location))) {
            // Throttled: this runs on every cycle, and an unthrottled retry here meant every unit
            // waiting on a fully booked fleet rescanned for a free carrier once per cycle. The
            // transport intent itself is unchanged - the unit stays in CARRYALLREQUESTED and keeps
            // asking - only the scan rate is bounded.
            if(carryallRequestCooldown <= 0) {
                requestCarryall();
            }
        } else {
            if(isHarvesterLikeUnit(getItemID())) {
                doSetAttackMode(HARVEST);
            } else {
                doSetAttackMode(GUARD);
            }
        }
    }
}


void GroundUnit::playConfirmSound() {
    soundPlayer->playVoice(getRandomOf({Acknowledged,Affirmative}), getOwner()->getHouseID());
}

void GroundUnit::playSelectSound() {
    soundPlayer->playVoice(Reporting, getOwner()->getHouseID());
}

/**
    Request a Carryall to drop at target location
**/

void GroundUnit::doRequestCarryallDrop(int xPos, int yPos) {
    if(getOwner()->hasCarryalls() && !awaitingPickup && currentGameMap->tileExists(xPos, yPos)){
        doMove2Pos(xPos, yPos, true);
        requestCarryall();
    }
}

void GroundUnit::cancelCarryallPickup() {
    if(awaitingPickup || bookedCarrier != NONE_ID) {
        // Release both sides immediately. Otherwise cancelling and rebooking in the same
        // cycle leaves the first aircraft targeting a passenger promised to another aircraft.
        auto* carryall = dynamic_cast<Carryall*>(currentGame->getObjectManager().getObject(bookedCarrier));
        if(carryall != nullptr && carryall->getTarget() == this) {
            carryall->setTarget(nullptr);
        }
        bookCarrier(nullptr);
    }

    const ATTACKMODE next = UnitMovementPolicy::attackModeAfterCancellingPickup(
        attackMode, isHarvesterLikeUnit(getItemID()));
    if(next != attackMode) {
        doSetAttackMode(next);
    }
}

void GroundUnit::releaseReplacementPickup(const UnitBase* keepCarrier) {
    if(bookedCarrier == NONE_ID) {
        return;
    }
    if(keepCarrier != nullptr && bookedCarrier == keepCarrier->getObjectID()) {
        return;  // The collecting aircraft is the booked one; nothing was replaced.
    }

    auto* other = dynamic_cast<Carryall*>(currentGame->getObjectManager().getObject(bookedCarrier));
    if(other != nullptr && other->getTarget() == this) {
        other->setTarget(nullptr);
    }
    bookCarrier(nullptr);
}

void GroundUnit::doMove2Pos(int xPos, int yPos, bool bForced) {
    if(UnitMovementPolicy::shouldCancelPickupOnMove(awaitingPickup, attackMode, bForced)) {
        cancelCarryallPickup();
    }
    UnitBase::doMove2Pos(xPos, yPos, bForced);
}

void GroundUnit::doMove2Object(const ObjectBase* pTargetObject) {
    // doMove2Object is always treated as a forced move (see UnitBase::doMove2Object).
    if(UnitMovementPolicy::shouldCancelPickupOnMove(awaitingPickup, attackMode, true)) {
        cancelCarryallPickup();
    }
    UnitBase::doMove2Object(pTargetObject);
}

Carryall* GroundUnit::findFreeCarrier() const {
    if(currentGame == nullptr) {
        return nullptr;
    }

    // Carriers only, in unitList order, so a fully booked fleet costs a scan of the fleet instead
    // of a scan of every unit in the match. The index is derived and may be a tick stale, which is
    // why every candidate is revalidated here.
    for(const Uint32 carrierId : currentGame->getCarryallCandidateIds()) {
        auto* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(carrierId));
        if(unit == nullptr || !isCarryallUnit(unit->getItemID())) {
            continue;  // Gone, or the id was reused by something else.
        }
        if(unit->getOwner() != owner || !unit->isActive() || unit->getHealth() <= 0) {
            continue;
        }
        if(currentGameMap == nullptr || !currentGameMap->tileExists(unit->getLocation())) {
            continue;  // Already leaving the map.
        }

        auto* carryall = static_cast<Carryall*>(unit);
        if(carryall->isBooked()) {
            continue;
        }
        if(carryall->isDropOfferer() || !carryall->isOwnedCarrier()) {
            continue;  // A delivery flight that removes itself; booking it strands the unit.
        }
        return carryall;
    }

    return nullptr;
}

void GroundUnit::armCarryallRequestThrottle() {
    carryallRequestCooldown = carryallRescueCooldownCycles
        + static_cast<Sint32>(getObjectID() % carryallRescueJitterCycles);
}

bool GroundUnit::requestCarryall() {
    // A repeat of a request this unit is already waiting on is throttled, wherever it comes from:
    // checkPos() and both harvester classes call this every cycle while a lift is wanted, and
    // without this each of those calls was a fresh fleet scan. A genuinely new explicit order is
    // never affected, because every one of them (the player's drop order, doRepair, a forced move
    // to a different refinery) cancels the outstanding pickup first, which leaves CARRYALLREQUESTED
    // before arriving here.
    if(attackMode == CARRYALLREQUESTED && !awaitingPickup && carryallRequestCooldown > 0) {
        return false;
    }

    if (getOwner()->hasCarryalls() && !awaitingPickup)  {
        // This allows a unit to keep requesting a carryall even if one isn't available right now
        doSetAttackMode(CARRYALLREQUESTED);

        // A fleet scan happened, so the throttle is armed either way. Explicit requests never
        // *consult* it - a player order or a repair trip is honoured immediately - but arming it
        // here is what stops checkPos() rescanning the fleet on every cycle for a unit sitting in
        // CARRYALLREQUESTED with nothing free to collect it.
        armCarryallRequestThrottle();

        if(Carryall* carryall = findFreeCarrier()) {
            carryall->setTarget(this);
            carryall->clearPath();
            bookCarrier(carryall);

            //setDestination(&location);    //stop moving, and wait for carryall to arrive

            return true;
        }
    }

    return false;
}

bool GroundUnit::requestCarryallRescue() {
    if(carryallRequestCooldown > 0 || awaitingPickup || hasBookedCarrier() || !getOwner()->hasCarryalls()) {
        return false;
    }

    // Armed before the result is known: a failed rescue costs one fleet scan and then waits out
    // the cooldown. It deliberately does not restart the thirty-second stall clock, so the unit
    // stays a candidate and retries once per cooldown rather than once per thirty seconds.
    armCarryallRequestThrottle();

    Carryall* carryall = findFreeCarrier();
    if(carryall == nullptr) {
        // Nothing free. The unit keeps its own attack mode and keeps navigating and fighting; it is
        // deliberately not moved into CARRYALLREQUESTED, which would freeze it waiting for a flight
        // nobody has promised.
        return false;
    }

    carryall->setTarget(this);
    carryall->clearPath();
    bookCarrier(carryall);
    // Only now that a carrier is actually on its way does this become a transport wait, which is
    // the state Carryall::pickupTarget() and checkPos() already understand.
    doSetAttackMode(CARRYALLREQUESTED);
    return true;
}

void GroundUnit::setPickedUp(UnitBase* newCarrier) {
    UnitBase::setPickedUp(newCarrier);
    awaitingPickup = false;
    bookedCarrier = NONE_ID;

    clearPath(); // Stefan: I don't think this is right
                 // but there is definitely something to it
                 // <try removing this to keep tanks moving even when a carryall is coming>
}

void GroundUnit::bookCarrier(UnitBase* newCarrier) {
    if(newCarrier == nullptr) {
        bookedCarrier = NONE_ID;
        awaitingPickup = false;
    } else {
        bookedCarrier = newCarrier->getObjectID();
        awaitingPickup = true;
    }
}

bool GroundUnit::hasBookedCarrier() const {
    if(bookedCarrier == NONE_ID) {
        return false;
    } else {
        return (currentGame->getObjectManager().getObject(bookedCarrier) != nullptr);
    }
}

const UnitBase* GroundUnit::getCarrier() const {
    return static_cast<UnitBase*>(currentGame->getObjectManager().getObject(bookedCarrier));
}

void GroundUnit::move() {
    if(!moving && !justStoppedMoving && (((currentGame->getGameCycleCount() + getObjectID()) % 512) == 0)) {
        currentGameMap->viewMap(owner->getHouseID(), location, getViewRange());
    }

    UnitBase::move();
}

void GroundUnit::navigate() {
    // Lets keep units moving even if they are awaiting a pickup
    // Could potentially make this distance based depending on how
    // far away the booked carrier is
    if(!awaitingPickup) {
        UnitBase::navigate();
    }
}

void GroundUnit::handleSendToRepairClick() {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_SENDTOREPAIR,objectID));
}

void GroundUnit::doRepair() {
    if(!isEligibleForRepair()) {
        // Central block for every explicit repair order: the sidebar/hotkey command
        // (CMD_UNIT_SENDTOREPAIR), every bot that asks a damaged unit to withdraw, and the
        // carryall that repairs badly damaged cargo on pickup all arrive here.
        return;
    }

    if(getHealth() < getMaxHealth()) {
        //find a repair yard to return to

        FixPoint closestLeastBookedRepairYardDistance = 1000000;
        RepairYard* pBestRepairYard = nullptr;

        for(StructureBase* pStructure : structureList) {
            if ((pStructure->getItemID() == Structure_RepairYard) && (pStructure->getOwner() == owner)) {
                RepairYard* pRepairYard = static_cast<RepairYard*>(pStructure);

                if(pRepairYard->getNumBookings() == 0) {
                    FixPoint tempDistance = blockDistance(location, pRepairYard->getClosestPoint(location));
                    if(tempDistance < closestLeastBookedRepairYardDistance) {
                        closestLeastBookedRepairYardDistance = tempDistance;
                        pBestRepairYard = pRepairYard;
                    }
                }
            }
        }

        if(pBestRepairYard) {
            // doMove2Object first: GroundUnit::doMove2Object cancels any pending
            // pickup booking, so requesting the carryall afterwards keeps the booking.
            doMove2Object(pBestRepairYard);
            requestCarryall();
        }
    }
}
