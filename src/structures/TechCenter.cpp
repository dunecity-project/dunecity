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

#include <structures/TechCenter.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <Command.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <SpecialVehicle.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>

#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <GUI/ObjectInterfaces/TechCenterInterface.h>

#include <units/HarvesterHelpers.h>

#include <mod/ModManager.h>

#include <algorithm>
#include <vector>

namespace {

bool isEnabledTechCenterVehicle(int itemID, int house) {
    if(currentGame == nullptr || !isSpecialVehicleSelectionCandidate(itemID)) {
        return false;
    }

    const auto& data = currentGame->objectData.data[itemID][house];
    if(!data.enabled) {
        return false;
    }

    if(data.techLevel >= 0 && currentGame->techLevel < data.techLevel) {
        return false;
    }

    return true;
}

bool isTechCenterSpawnCandidate(int itemID, int house) {
    return isEnabledTechCenterVehicle(itemID, house)
        && currentGame->objectData.data[itemID][house].prerequisiteStructuresSet[Structure_IX];
}

} // namespace


TechCenter::TechCenter(House* newOwner) : StructureBase(newOwner) {
    TechCenter::init();

    setHealth(getMaxHealth());

    spawnTimer = 0;
}

TechCenter::TechCenter(InputStream& stream) : StructureBase(stream) {
    TechCenter::init();

    spawnTimer = stream.readSint32();
}

void TechCenter::init() {
    itemID = Structure_TechCenter;
    owner->incrementStructures(itemID);

    structureSize.x = 3;
    structureSize.y = 2;

    graphicID = ObjPic_TechCenter;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());

    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

TechCenter::~TechCenter() = default;

void TechCenter::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(spawnTimer);
}

ObjectInterface* TechCenter::getInterfaceContainer() {
    // Same interface shape as Palace — the bottom bar shows the
    // "production" progress (spawn timer) in place of the special
    // weapon readiness meter.
    if(mayShowOwnerInterface()) {
        return TechCenterInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

bool TechCenter::canSpawnVehicles() const {
    return isSpawnReady() && houseHasIxUnlocked();
}

void TechCenter::handleSpawnClick() {
    if(currentGame == nullptr || pLocalPlayer == nullptr) {
        return;
    }

    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_TECHCENTER_SPAWN, objectID));
}

void TechCenter::doSpawnVehicles() {
    if(!canSpawnVehicles()) {
        return;
    }

    const int spawnCount = currentGame->randomGen.rand(1, 3);
    if(spawnRandomVehicles(spawnCount) > 0) {
        spawnTimer = getMaxSpawnTimer();
    }
}

bool TechCenter::houseHasIxUnlocked() const {
    // The Tech Center itself provides the IX-style vehicle spawn. It should
    // not require a separate House IX building, otherwise maps that grant or
    // build a Tech Center still hide the command button.
    return currentGame != nullptr && getOwner() != nullptr && currentGame->techLevel >= 9;
}

int TechCenter::spawnRandomVehicles(int count) {
    // Keep Tech Center, Unit_Special maps and factory unlocks on the same
    // active-mod ObjectData table.
    const bool modInitialized = ModManager::instance().isInitialized();
    const bool tornieActive = modInitialized && ModManager::instance().isTornieContentActive();
    const auto objectDataIxCandidates = discoverHouseSpecialVehicleCandidates([&](int candidate) {
        const auto& data = currentGame->objectData.data[candidate][originalHouseID];
        return HouseSpecialVehicleCandidateData{
            data.enabled,
            data.builder,
            data.prerequisiteStructuresSet[Structure_IX]
        };
    });

    const auto specialVehiclePool = resolveSpecialVehiclePoolForHouse(
        originalHouseID, tornieActive, objectDataIxCandidates);
    const bool useGenericCustomFallback =
        (isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_CUSTOM)
         || isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_THARPIQUE))
        && objectDataIxCandidates.empty();
    std::vector<int> vehiclePool;
    for(const auto candidate : specialVehiclePool) {
        const bool candidateEnabled = useGenericCustomFallback
            ? isEnabledTechCenterVehicle(candidate, originalHouseID)
            : isTechCenterSpawnCandidate(candidate, originalHouseID);
        if(candidateEnabled) {
            vehiclePool.push_back(candidate);
        }
    }

    const HOUSETYPE factionIdentity = getHouseFactionIdentity(static_cast<HOUSETYPE>(originalHouseID));
    const bool guestNamedHouse = factionIdentity == HOUSE_WILDSPADE
        || factionIdentity == HOUSE_KLESHMERSH;

    if(vehiclePool.empty() && !guestNamedHouse) {
        for(const auto fallback : { Unit_Trike, Unit_Quad }) {
            const auto& data = currentGame->objectData.data[fallback][originalHouseID];
            if(data.enabled && data.builder != ItemID_Invalid) {
                vehiclePool.push_back(fallback);
            }
        }
    }

    if(vehiclePool.empty()) {
        return 0;
    }

    int spawned = 0;
    for(int i = 0; i < count; i++) {
        const int idx = currentGame->randomGen.rand(0, static_cast<int>(vehiclePool.size()) - 1);
        const int itemID = vehiclePool[idx];

        UnitBase* newUnit = getOwner()->createUnit(itemID);
        if(newUnit == nullptr) {
            continue;
        }

        // Place around the Tech Center footprint (3x3 Palace-size).
        // findDeploySpot picks the nearest empty tile in the 4-tile ring.
        Coord deployPos = currentGameMap->findDeploySpot(newUnit, getLocation(), currentGame->randomGen,
                                                         getLocation(), getStructureSize());
        if(deployPos.isInvalid()) {
            newUnit->cancelDeployment();
            continue;
        }

        newUnit->deploy(deployPos);
        newUnit->setGuardPoint(deployPos);
        newUnit->doSetAttackMode(HUNT);

        spawned++;
    }
    return spawned;
}

void TechCenter::updateStructureSpecificStuff() {
    if(spawnTimer > 0) {
        spawnTimer--;
        if(spawnTimer <= 0) {
            spawnTimer = 0;

            if(getOwner() == pLocalHouse) {
                currentGame->addToNewsTicker(_("Tech Center is ready"));
            } else if(getOwner()->isAI()) {
                doSpawnVehicles();
            }
        }
    } else if(getOwner()->isAI()) {
        doSpawnVehicles();
    }
}
