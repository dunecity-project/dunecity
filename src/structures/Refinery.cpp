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

#include <structures/Refinery.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <mod/ModManager.h>
#include <SoundPlayer.h>
#include <Map.h>

#include <units/UnitBase.h>
#include <units/GroundUnit.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/Carryall.h>

#include <GUI/ObjectInterfaces/RefineryAndSiloInterface.h>

/* how fast is spice extracted */
#define MAXIMUMHARVESTEREXTRACTSPEED (0.625_fix)

Refinery::Refinery(House* newOwner, int newItemID) : StructureBase(newOwner) {
    Refinery::init(newItemID);

    setHealth(getMaxHealth());

    bookings = 0;

    firstRun = true;

    firstAnimFrame = 2;
    lastAnimFrame = 3;
}

Refinery::Refinery(InputStream& stream, int newItemID) : StructureBase(stream) {
    Refinery::init(newItemID);

    extractingSpice[0] = stream.readBool();
    harvesters[0].load(stream);
    bookings = stream.readUint32();

    if(itemID == Structure_Doublefinery
       && currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_TORNIE546) {
        extractingSpice[1] = stream.readBool();
        harvesters[1].load(stream);
    }

    if(ModManager::instance().isTornieFamilyContentActive()) {
        for(int index = 0; index < getNumBays(); ++index) {
            if(!extractingSpice[index]) harvesters[index].pointTo(NONE_ID);
        }
        refreshAnimation();
    } else if(extractingSpice[0]) {
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

void Refinery::init(int newItemID) {
    itemID = newItemID;
    owner->incrementStructures(itemID);

    const bool doubleBay = getNumBays() == 2;
    structureSize.x = doubleBay ? 5 : 3;
    structureSize.y = 2;

    graphicID = doubleBay ? ObjPic_Doublefinery : ObjPic_Refinery;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 10;
    numImagesY = 1;
}

Refinery::~Refinery() {
    for(int index = 0; index < getNumBays(); ++index) {
        if(extractingSpice[index] && harvesters[index]) {
            if(auto* unit = harvesters[index].getUnitPointer()) unit->destroy();
            harvesters[index].pointTo(NONE_ID);
        }
    }
}

void Refinery::save(OutputStream& stream) const {
    StructureBase::save(stream);

    // The first bay keeps the classic bool/pointer/bookings byte layout.
    stream.writeBool(extractingSpice[0]);
    harvesters[0].save(stream);
    stream.writeUint32(bookings);
    if(SAVEGAMEVERSION >= SAVEGAMEVERSION_TORNIE546 && itemID == Structure_Doublefinery) {
        stream.writeBool(extractingSpice[1]);
        harvesters[1].save(stream);
    }
}

ObjectInterface* Refinery::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return RefineryAndSiloInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

int Refinery::getNumBays() const {
    return itemID == Structure_Doublefinery
        && ModManager::instance().isTornieFamilyContentActive() ? 2 : 1;
}

void Refinery::unBook() {
    if(bookings > 0 || !(ModManager::instance().isTornieFamilyContentActive())) {
        --bookings;
    }
    if(bookings == 0) stopAnimate();
}

int Refinery::getNumOccupiedBays() const {
    return int(extractingSpice[0]) + (getNumBays() == 2 ? int(extractingSpice[1]) : 0);
}

bool Refinery::isFree() const {
    return getNumOccupiedBays() < getNumBays();
}

UnitBase* Refinery::getContainedHarvester() {
    return const_cast<UnitBase*>(static_cast<const Refinery*>(this)->getContainedHarvester());
}

const UnitBase* Refinery::getContainedHarvester() const {
    if(!(ModManager::instance().isTornieFamilyContentActive())) {
        // Legacy refineries retain their historical pointer after deployment.
        return harvesters[0].getUnitPointer();
    }
    for(int index = 0; index < getNumBays(); ++index) {
        if(extractingSpice[index]) {
            if(auto* unit = harvesters[index].getUnitPointer()) return unit;
        }
    }
    return nullptr;
}
UnitBase* Refinery::getContainedHarvester(int bay) {
    return const_cast<UnitBase*>(static_cast<const Refinery*>(this)->getContainedHarvester(bay));
}

const UnitBase* Refinery::getContainedHarvester(int bay) const {
    return bay >= 0 && bay < getNumBays() && extractingSpice[bay]
        ? harvesters[bay].getUnitPointer() : nullptr;
}


std::vector<UnitBase*> Refinery::getContainedHarvesters() {
    std::vector<UnitBase*> result;
    const int numBays = getNumBays();
    for(int index = 0; index < numBays; ++index) {
        if(extractingSpice[index]) {
            if(auto* unit = harvesters[index].getUnitPointer()) {
                if(result.empty()) result.reserve(numBays);
                result.push_back(unit);
            }
        }
    }
    return result;
}

bool Refinery::assignHarvester(TrackedUnit* newHarvester) {
    if(ModManager::instance().isTornieFamilyContentActive()) {
        if(!newHarvester) return false;
        const int numBays = getNumBays();
        for(int index = 0; index < numBays; ++index) {
            if(extractingSpice[index] && harvesters[index].getUnitPointer() == newHarvester) return false;
        }
        for(int index = 0; index < numBays; ++index) {
            if(!extractingSpice[index]) {
                extractingSpice[index] = true;
                harvesters[index].pointTo(newHarvester);
                refreshAnimation();
                return true;
            }
        }
        return false;
    }
    extractingSpice[0] = true;
    harvesters[0].pointTo(newHarvester);
    drawnAngle = 1;
    firstAnimFrame = 8;
    lastAnimFrame = 9;
    curAnimFrame = 8;
    return true;
}

void Refinery::deployHarvester(Carryall* carryall) {
    if(!(ModManager::instance().isTornieFamilyContentActive())) {
        deployBay(0, carryall);
        return;
    }
    const int numBays = getNumBays();
    for(int index = 0; index < numBays; ++index) {
        auto* unit = static_cast<GroundUnit*>(harvesters[index].getUnitPointer());
        // Both aircraft target this structure, but each must collect its own bay.
        if(extractingSpice[index] && unit && (!carryall || unit->getCarrier() == carryall)) {
            deployBay(index, carryall);
            return;
        }
    }
    if(carryall) carryall->setTarget(nullptr);
}

void Refinery::deployBay(int index, Carryall* pCarryall) {
    const bool familyActive = ModManager::instance().isTornieFamilyContentActive();
    UnitBase* pHarvester = harvesters[index].getUnitPointer();
    if(familyActive && !pHarvester) return;
    extractingSpice[index] = false;
    if(familyActive) harvesters[index].pointTo(NONE_ID);
    unBook();
    drawnAngle = 0;

    if(firstRun) {
        if(getOwner() == pLocalHouse) {
            soundPlayer->playVoice(HarvesterDeployed,getOwner()->getHouseID());
        }
    }

    firstRun = false;

    // Preserve the legacy invalid-pointer deployment behavior outside the family.
    if(pHarvester == nullptr) {
        return;
    }

    if((pCarryall != nullptr) && pHarvester->getGuardPoint().isValid()) {
        pCarryall->giveCargo(pHarvester);
        pCarryall->setTarget(nullptr);
        pCarryall->setDestination(pHarvester->getGuardPoint());
    } else {
        Coord deployPos = currentGameMap->findDeploySpot(pHarvester, location, currentGame->randomGen, destination, structureSize);
        pHarvester->deploy(deployPos);
    }

    if(familyActive) {
        refreshAnimation();
    } else if(bookings == 0) {
        stopAnimate();
    } else {
        startAnimate();
    }
}

void Refinery::refreshAnimation() {
    // Object IDs must not be resolved while loading: units can be registered later.
    const bool occupied = extractingSpice[0] || (getNumBays() == 2 && extractingSpice[1]);
    drawnAngle = occupied ? 1 : 0;
    firstAnimFrame = occupied ? 8 : 2;
    lastAnimFrame = occupied ? 9 : (bookings ? 7 : 3);
    if(curAnimFrame < firstAnimFrame || curAnimFrame > lastAnimFrame) curAnimFrame = firstAnimFrame;
}

void Refinery::startAnimate() {
    if(ModManager::instance().isTornieFamilyContentActive()) {
        refreshAnimation();
        justPlacedTimer = 0;
        animationCounter = 0;
    } else if(!extractingSpice[0]) {
        firstAnimFrame = 2;
        lastAnimFrame = 7;
        curAnimFrame = 2;
        justPlacedTimer = 0;
        animationCounter = 0;
    }
}

void Refinery::stopAnimate() {
    if(ModManager::instance().isTornieFamilyContentActive()) {
        refreshAnimation();
        return;
    }
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
}

void Refinery::updateStructureSpecificStuff() {
    const bool familyActive = ModManager::instance().isTornieFamilyContentActive();
    if(familyActive) refreshAnimation();
    const int numBays = getNumBays();
    for(int index = 0; index < numBays; ++index) {
        if(!extractingSpice[index]) continue;
        UnitBase* pHarvester = harvesters[index].getUnitPointer();
        if(pHarvester == nullptr) {
            if(!familyActive) return;
            extractingSpice[index] = false;
            refreshAnimation();
            continue;
        }

        if(harvesterGetAmountOfSpice(pHarvester) > 0) {
            FixPoint extractionSpeed = MAXIMUMHARVESTEREXTRACTSPEED;
            if(numBays == 2) extractionSpeed = extractionSpeed * 115 / 100;

            int scale = floor(5*getHealth()/getMaxHealth());
            if(scale == 0) {
                scale = 1;
            }

            extractionSpeed = (extractionSpeed * scale) / 5;
            // The one place a delivery becomes income. The extraction speed above, and so the
            // cargo actually taken off the harvester this tick, is untouched: only what the
            // house is paid for it scales. An integer multiply of the fixed-point amount is
            // exact, so a load unloaded over many ticks - or interrupted part way - still pays
            // exactly the delivered cargo times the factor.
            FixPoint delivered = harvesterExtractSpice(pHarvester, extractionSpeed);
            delivered *= static_cast<int>(owner->getSpiceIncomeMultiplier());
            owner->addCredits(delivered, true);
        } else {
            GroundUnit* pGroundHarvester = static_cast<GroundUnit*>(pHarvester);
            if((pGroundHarvester->isAwaitingPickup() == false) && (pHarvester->getGuardPoint().isValid())) {
            // find carryall
            Carryall* pCarryall = nullptr;
            if((pHarvester->getGuardPoint().isValid()) && getOwner()->hasCarryalls())   {
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

            if(pCarryall != nullptr) {
                pCarryall->setTarget(this);
                pCarryall->clearPath();
                pGroundHarvester->bookCarrier(pCarryall);
                pHarvester->setTarget(nullptr);
                pHarvester->setDestination(pHarvester->getGuardPoint());
            } else {
                deployBay(index, nullptr);
            }
            } else if(!pGroundHarvester->hasBookedCarrier()) {
                deployBay(index, nullptr);
            }
        }
    }
}
