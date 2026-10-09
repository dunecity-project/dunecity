/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <structures/Scoutpost.h>
#include <dunecity/PowerRules.h>

#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <GUI/ObjectInterfaces/WindTrapInterface.h>
#include <globals.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <Command.h>

#include <Bullet.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <units/UnitBase.h>

#include <algorithm>
#include <cstdlib>

Scoutpost::Scoutpost(House* newOwner, int newItemID) : TurretBase(newOwner) {
    Scoutpost::init(newItemID);

    setHealth(getMaxHealth());
}

Scoutpost::Scoutpost(InputStream& stream, int newItemID) : TurretBase(stream) {
    Scoutpost::init(newItemID);
}

void Scoutpost::init(int newItemID) {
    itemID = newItemID;
    owner->incrementStructures(itemID);

    structureSize.x = 1;
    structureSize.y = 1;

    attackSound = Sound_RocketSmall;
    bulletType = itemID == Structure_Chemipost
        ? Bullet_Heal
        : (itemID == Structure_Flamepost ? Bullet_Flame : Bullet_SmallRocket);

    graphicID = itemID == Structure_Chemipost
        ? ObjPic_Chemipost
        : (itemID == Structure_Flamepost ? ObjPic_Flamepost : ObjPic_Scoutpost);
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

Scoutpost::~Scoutpost() {
    // Rejected placement and demolition must also release generated power.
    setHealth(0);
}

bool Scoutpost::canAttack(const ObjectBase* object) const {
    if(itemID == Structure_Chemipost) {
        const auto* unit = dynamic_cast<const UnitBase*>(object);
        return unit != nullptr
            && unit->getHealth() > 0
            && unit->getHealth() < unit->getMaxHealth()
            && unit->getOwner()->getTeamID() == owner->getTeamID()
            && unit->isVisible(owner->getTeamID());
    }

    return object != nullptr
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || object->getItemID() == Unit_Sandworm)
        && object->isVisible(getOwner()->getTeamID());
}

ObjectInterface* Scoutpost::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return WindTrapInterface::create(objectID);
    }

    return DefaultObjectInterface::create(objectID);
}

void Scoutpost::updateStructureSpecificStuff() {
    auto* citySim = currentGame->getCitySimulation();
    if(citySim) {
        citySim->registerPowerSource(location.x, location.y, getProducedPower());
    }

    if(target && target.getObjPointer() != nullptr) {
        if(!canAttack(target.getObjPointer()) || !targetInWeaponRange()) {
            setTarget(nullptr);
            if(findTargetTimer < 25) {
                findTargetTimer = 25 + (objectID % 15);
            }
        } else {
            attack();
        }
    } else if((attackMode != STOP) && (findTargetTimer == 0)) {
        setTarget(itemID == Structure_Chemipost ? findDamagedAlly() : findTarget());
        findTargetTimer = 50 + (objectID % 20);
    }

    if(findTargetTimer > 0) {
        findTargetTimer--;
    }

    if(weaponTimer > 0) {
        weaponTimer--;
    }
}

UnitBase* Scoutpost::findDamagedAlly() const {
    UnitBase* bestTarget = nullptr;
    int bestDistance = getWeaponRange() + 1;
    for(UnitBase* unit : unitList) {
        if(!canAttack(unit)) {
            continue;
        }

        const Coord targetLocation = unit->getLocation();
        const int distance = std::max(std::abs(targetLocation.x - location.x),
                                      std::abs(targetLocation.y - location.y));
        if(distance <= getWeaponRange() && distance < bestDistance) {
            bestDistance = distance;
            bestTarget = unit;
        }
    }
    return bestTarget;
}

void Scoutpost::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    TurretBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int Scoutpost::getProducedPower() const {
    int nominal = abs(currentGame->objectData.data[itemID][originalHouseID].power);
    return DuneCity::generatorOutput(nominal, getHealth(), getMaxHealth(), false);
}
bool Scoutpost::isFlamepostUpgradeEligible() const {
    return itemID == Structure_Scoutpost
        && owner != nullptr
        && isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_KLESHMERSH)
        && ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
}

int Scoutpost::getFlamepostUpgradeCost() const {
    if(currentGame == nullptr) {
        return 0;
    }

    const int configuredPrice = currentGame->objectData.data[Structure_Flamepost][originalHouseID].price;
    return configuredPrice > 0 ? configuredPrice : 0;
}

bool Scoutpost::canUpgradeToFlamepost() const {
    return isFlamepostUpgradeEligible()
        && owner->getNumItems(Structure_IX) > 0
        && owner->getCredits() >= getFlamepostUpgradeCost();
}

void Scoutpost::handleFlamepostUpgradeClick() {
    if(currentGame == nullptr || pLocalPlayer == nullptr) {
        return;
    }

    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_SCOUTPOST_UPGRADE, objectID));
}

void Scoutpost::doUpgradeToFlamepost() {
    if(!canUpgradeToFlamepost()) {
        return;
    }

    owner->takeCredits(getFlamepostUpgradeCost());
    owner->transformStructure(Structure_Scoutpost, Structure_Flamepost);

    itemID = Structure_Flamepost;
    bulletType = Bullet_Flame;
    graphicID = ObjPic_Flamepost;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
}

bool Scoutpost::isChemipostUpgradeEligible() const {
    if(itemID != Structure_Scoutpost || owner == nullptr || currentGame == nullptr
       || !isHouseFaction(static_cast<HOUSETYPE>(originalHouseID), HOUSE_THARPIQUE)
       || currentGame->techLevel < 7
       || !ModManager::instance().isInitialized()) {
        return false;
    }

    return ModManager::instance().isTornieContentActive();
}

int Scoutpost::getChemipostUpgradeCost() const {
    if(currentGame == nullptr) {
        return 0;
    }

    const int configuredPrice = currentGame->objectData.data[Structure_Chemipost][originalHouseID].price;
    return configuredPrice > 0 ? configuredPrice : 0;
}

bool Scoutpost::canUpgradeToChemipost() const {
    return isChemipostUpgradeEligible()
        && owner->getNumItems(Structure_IX) > 0
        && owner->getCredits() >= getChemipostUpgradeCost();
}

void Scoutpost::handleChemipostUpgradeClick() {
    if(currentGame == nullptr || pLocalPlayer == nullptr) {
        return;
    }

    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_SCOUTPOST_CHEMIPOST_UPGRADE, objectID));
}

void Scoutpost::doUpgradeToChemipost() {
    if(!canUpgradeToChemipost()) {
        return;
    }

    owner->takeCredits(getChemipostUpgradeCost());
    owner->transformStructure(Structure_Scoutpost, Structure_Chemipost);

    itemID = Structure_Chemipost;
    bulletType = Bullet_Heal;
    graphicID = ObjPic_Chemipost;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    setTarget(nullptr);
}
