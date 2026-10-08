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

#include <structures/RepairYard.h>
#include <structures/RepairYardJob.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Map.h>
#include <SoundPlayer.h>

#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>

#include <GUI/ObjectInterfaces/RepairYardInterface.h>

RepairYard::RepairYard(House* newOwner) : StructureBase(newOwner) {
    RepairYard::init();

    setHealth(getMaxHealth());
    bookings = 0;
    repairingAUnit = false;
}

RepairYard::RepairYard(InputStream& stream) : StructureBase(stream) {
    RepairYard::init();

    repairingAUnit = stream.readBool();
    repairUnit.load(stream);
    bookings = stream.readUint32();
}

void RepairYard::init() {
    itemID = Structure_RepairYard;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 2;

    graphicID = ObjPic_RepairYard;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 10;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
}

RepairYard::~RepairYard() {
    auto* unit = resolveRepairUnit();
    clearRepairJob();
    if (unit) unit->destroy();
}

GroundUnit* RepairYard::resolveRepairUnit() {
    return RepairYardJob::resolve(repairingAUnit,
        [&] { return dynamic_cast<GroundUnit*>(repairUnit.getObjPointer()); },
        [&] { clearRepairJob(); });
}

void RepairYard::clearRepairJob() {
    RepairYardJob::finish(repairingAUnit, bookings);
    repairUnit.pointTo(NONE_ID);
    firstAnimFrame = curAnimFrame = 2;
    lastAnimFrame = 3;
}

void RepairYard::save(OutputStream& stream) const {
    StructureBase::save(stream);

    stream.writeBool(repairingAUnit);
    repairUnit.save(stream);
    stream.writeUint32(bookings);
}


ObjectInterface* RepairYard::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return RepairYardInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

void RepairYard::deployRepairUnit(Carryall* pCarryall) {
    UnitBase* pRepairUnit = resolveRepairUnit();
    clearRepairJob();
    if (!pRepairUnit) {
        if (pCarryall) pCarryall->setTarget(nullptr);
        return;
    }
    if(pCarryall != nullptr) {
        pCarryall->giveCargo(pRepairUnit);
        pCarryall->setTarget(nullptr);
        pCarryall->setDestination(pRepairUnit->getGuardPoint());
    } else {
        Coord deployPos = currentGameMap->findDeploySpot(pRepairUnit, location, currentGame->randomGen, destination, structureSize);

        pRepairUnit->setForced(false);
        pRepairUnit->doSetAttackMode(isHarvesterLikeUnit(pRepairUnit->getItemID()) ? HARVEST : GUARD);
        pRepairUnit->deploy(deployPos);
        pRepairUnit->setTarget(nullptr);
        pRepairUnit->setDestination(pRepairUnit->getLocation());
    }

    if(getOwner() == pLocalHouse) {
        soundPlayer->playVoice(VehicleRepaired,getOwner()->getHouseID());
    }
}

void RepairYard::updateStructureSpecificStuff() {
    GroundUnit* pRepairUnit = resolveRepairUnit();
    if(repairingAUnit) {
        if(curAnimFrame < 6) {
            firstAnimFrame = 6;
            lastAnimFrame = 9;
            curAnimFrame = 6;
        }
    } else {
        if(curAnimFrame > 3) {
            firstAnimFrame = 2;
            lastAnimFrame = 3;
            curAnimFrame = 2;
        }
    }

    if (pRepairUnit) {

        if (pRepairUnit->getHealth() * 100 / pRepairUnit->getMaxHealth() < 100) {
            if (owner->takeCredits(UNIT_REPAIRCOST) > 0) {
                pRepairUnit->addHealth();
            }
        } else {
            // Unit is fully repaired - handle deployment
            bool needsCarryall = blockDistance(location, pRepairUnit->getGuardPoint()) >= MIN_CARRYALL_LIFT_DISTANCE 
                               && currentGame->getGameInitSettings().getGameOptions().manualCarryallDrops;
            
            if (pRepairUnit->hasBookedCarrier()) {
                // Already has a carryall coming - wait for it
                return;
            }
            
            if (needsCarryall && !pRepairUnit->isAwaitingPickup()) {
                // Try to find an available carryall
                Carryall* pCarryall = nullptr;
                if (pRepairUnit->getGuardPoint().isValid() && getOwner()->hasCarryalls()) {
                    for(UnitBase* pUnit : unitList) {
                        if ((pUnit->getOwner() == owner) && (pUnit->getItemID() == Unit_Carryall)) {
                            Carryall* pTmpCarryall = static_cast<Carryall*>(pUnit);
                            if (!pTmpCarryall->isBooked()) {
                                pCarryall = pTmpCarryall;
                                break;
                            }
                        }
                    }
                }

                if (pCarryall != nullptr) {
                    // Book the carryall and set it up
                    pCarryall->setTarget(this);
                    pCarryall->clearPath();
                    static_cast<GroundUnit*>(pRepairUnit)->bookCarrier(pCarryall);
                    pRepairUnit->setTarget(nullptr);
                    pRepairUnit->setDestination(pRepairUnit->getGuardPoint());
                    return;
                }
            }
            
            // If we get here, either we don't need a carryall or couldn't find one
            // Safe to deploy normally
            deployRepairUnit();
        }
    }
}
