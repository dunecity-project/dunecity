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

#include <structures/Worfinery.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>

#include <units/TrackedUnit.h>
#include <units/UnitBase.h>
#include <units/GroundUnit.h>
#include <units/Carryall.h>
#include <units/HarvesterHelpers.h>

#define MAXIMUMHARVESTEREXTRACTSPEED (0.625_fix)

Worfinery::Worfinery(House* newOwner) : BuilderBase(newOwner) {
    Worfinery::init();

    setHealth(getMaxHealth());
}

Worfinery::Worfinery(InputStream& stream) : BuilderBase(stream) {
    Worfinery::init();

    if(currentGame != nullptr && currentGame->getLoadedSavegameVersion() >= 9822) {
        extractingSpice = stream.readBool();
        harvester.load(stream);
        bookings = stream.readUint32();
    }

    if(extractingSpice) {
        drawnAngle = 1;
        firstAnimFrame = 8;
        lastAnimFrame = 9;
        curAnimFrame = 8;
    } else if(bookings == 0) {
        stopAnimate();
    } else {
        startAnimate();
    }
    firstRun = false;
}

void Worfinery::init() {
    itemID = Structure_Worfinery;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 2;

    graphicID = ObjPic_Worfinery;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    numImagesX = 10;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
    animationCounter = 0;
}

Worfinery::~Worfinery() {
    if(extractingSpice && harvester) {
        if(harvester.getUnitPointer() != nullptr) {
            harvester.getUnitPointer()->destroy();
        }
        harvester.pointTo(NONE_ID);
    }
}

bool Worfinery::receiveHarvester(TrackedUnit* unit) {
    if(unit == nullptr || !isHarvesterLikeUnit(unit->getItemID()) || extractingSpice) {
        return false;
    }

    assignHarvester(unit);
    return true;
}

void Worfinery::save(OutputStream& stream) const {
    BuilderBase::save(stream);

    stream.writeBool(extractingSpice);
    harvester.save(stream);
    stream.writeUint32(bookings);
}

ObjectInterface* Worfinery::getInterfaceContainer() {
    // Reuse the Refinery interface — same layout (storage list + production
    // button) makes sense for a building that produces Troopers instead
    // of Harvesters.
    return BuilderBase::getInterfaceContainer();
}

void Worfinery::updateStructureSpecificStuff() {
    if(!extractingSpice) {
        return;
    }

    UnitBase* unit = harvester.getUnitPointer();
    if(unit == nullptr) {
        extractingSpice = false;
        harvester.pointTo(NONE_ID);
        drawnAngle = 0;
        if(bookings == 0) {
            stopAnimate();
        } else {
            startAnimate();
        }
        return;    }

    if(harvesterGetAmountOfSpice(unit) > 0) {
        FixPoint extractionSpeed = MAXIMUMHARVESTEREXTRACTSPEED;
        int healthScale = floor(5 * getHealth() / getMaxHealth());
        if(healthScale == 0) {
            healthScale = 1;
        }

        extractionSpeed = (extractionSpeed * healthScale) / 5;
        // Same contract as Refinery::updateStructureSpecificStuff: scale the payment, never
        // the extraction. Tornie's Harvestank/Worfinery pair uses the same house factor.
        FixPoint delivered = harvesterExtractSpice(unit, extractionSpeed);
        delivered *= static_cast<int>(owner->getSpiceIncomeMultiplier());
        owner->addCredits(delivered, true);
        return;
    }

    GroundUnit* groundHarvester = static_cast<GroundUnit*>(unit);
    if(!groundHarvester->isAwaitingPickup() && unit->getGuardPoint().isValid()) {
        Carryall* carryall = nullptr;
        if(getOwner()->hasCarryalls()) {
            for(UnitBase* candidate : unitList) {
                if(candidate->getOwner() == owner && candidate->getItemID() == Unit_Carryall) {
                    Carryall* candidateCarryall = static_cast<Carryall*>(candidate);
                    if(!candidateCarryall->isBooked()) {
                        carryall = candidateCarryall;
                        break;
                    }
                }
            }
        }

        if(carryall != nullptr) {
            carryall->setTarget(this);
            carryall->clearPath();
            groundHarvester->bookCarrier(carryall);
            unit->setTarget(nullptr);
            unit->setDestination(unit->getGuardPoint());
        } else {
            deployHarvester();
        }
    } else if(!groundHarvester->hasBookedCarrier()) {
        deployHarvester();
    }
}

void Worfinery::bookHarvesterDropoff() {
    ++bookings;
    startAnimate();
}

void Worfinery::startHarvesterDropoffAnimation() {
    startAnimate();
}

void Worfinery::unbookHarvesterDropoff() {
    if(bookings > 0) {
        --bookings;
    }

    if(bookings == 0 && !extractingSpice) {
        stopAnimate();
    }
}

void Worfinery::startAnimate() {
    if(!extractingSpice) {
        firstAnimFrame = 2;
        lastAnimFrame = 7;
        curAnimFrame = 2;
        justPlacedTimer = 0;
        animationCounter = 0;
    }
}

void Worfinery::stopAnimate() {
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
}
void Worfinery::assignHarvester(TrackedUnit* unit) {
    extractingSpice = true;
    harvester.pointTo(unit);
    drawnAngle = 1;
    firstAnimFrame = 8;
    lastAnimFrame = 9;
    curAnimFrame = 8;
}

void Worfinery::deployContainedHarvester(Carryall* carryall) {
    deployHarvester(carryall);
}

void Worfinery::deployHarvester(Carryall* carryall) {
    unbookHarvesterDropoff();
    drawnAngle = 0;
    extractingSpice = false;

    if(bookings == 0) {
        stopAnimate();
    } else {
        startAnimate();
    }
    if(firstRun && getOwner() == pLocalHouse) {
        soundPlayer->playVoice(HarvesterDeployed, getOwner()->getHouseID());
    }
    firstRun = false;

    UnitBase* unit = harvester.getUnitPointer();
    harvester.pointTo(NONE_ID);
    if(unit == nullptr) {
        return;
    }

    if(carryall != nullptr && unit->getGuardPoint().isValid()) {
        carryall->giveCargo(unit);
        carryall->setTarget(nullptr);
        carryall->setDestination(unit->getGuardPoint());
    } else {
        Coord deployPos = currentGameMap->findDeploySpot(
            unit, location, currentGame->randomGen, destination, structureSize);
        unit->deploy(deployPos);
    }
}
