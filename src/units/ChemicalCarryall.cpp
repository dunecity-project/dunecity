/*
 *  This file is part of Dune Legacy.
 */

#include <units/ChemicalCarryall.h>

#include <Bullet.h>
#include <FileClasses/GFXManager.h>
#include <Game.h>
#include <House.h>
#include <ObjectManager.h>
#include <globals.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>

#include <algorithm>
#include <cstdlib>

namespace {
constexpr int CHEMICAL_CARRYALL_HEAL_RANGE = 5;
constexpr int CHEMICAL_HEAL_AMOUNT = 15;
constexpr int CHEMICAL_HEAL_RELOAD = 300;
}

ChemicalCarryall::ChemicalCarryall(House* newOwner)
    : Carryall(newOwner, Unit_ChemicalCarryall)
{
    ensureRuntimeObjectData();
    graphicID = ObjPic_ChemicalCarryall;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    setHealth(getMaxHealth());
    respondable = true;
}

ChemicalCarryall::ChemicalCarryall(InputStream& stream)
    : Carryall(stream, Unit_ChemicalCarryall)
{
    const FixPoint loadedHealth = getHealth();
    ensureRuntimeObjectData();
    graphicID = ObjPic_ChemicalCarryall;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    setHealth(loadedHealth > 0 ? std::min(loadedHealth, FixPoint(getMaxHealth()))
                               : FixPoint(getMaxHealth()));
    respondable = true;
}

ChemicalCarryall::~ChemicalCarryall() = default;

void ChemicalCarryall::setProductionHouseID(int houseID)
{
    Carryall::setProductionHouseID(houseID);
    if(ModManager::instance().isTornieFamilyContentActive()) {
        ensureRuntimeObjectData();
        currentMaxSpeed = currentGame->objectData.data[itemID][getProductionHouseID()].maxspeed;
    }
}

void ChemicalCarryall::ensureRuntimeObjectData()
{
    if(currentGame == nullptr || owner == nullptr) {
        return;
    }

    auto& chemicalData = currentGame->objectData.data[Unit_ChemicalCarryall][getProductionHouseID()];
    if(chemicalData.hitpoints > 0 && chemicalData.maxspeed > 0
       && chemicalData.turnspeed > 0 && chemicalData.buildtime > 0) {
        return;
    }

    // Old saves and stale mod data may not contain appended item ID 66. Start
    // from the normal Carryall so transport and movement remain identical.
    chemicalData = currentGame->objectData.data[Unit_Carryall][getProductionHouseID()];
    chemicalData.enabled = true;
    chemicalData.hitpoints = 100;
    chemicalData.price = 950;
    chemicalData.viewrange = 5;
    chemicalData.buildtime = 76;
    chemicalData.infspawnprop = 0;
    chemicalData.builder = Structure_HighTechFactory;
    chemicalData.techLevel = 1;
    chemicalData.upgradeLevel = 2;
    chemicalData.prerequisiteStructuresSet.reset();
    chemicalData.prerequisiteStructuresSet.set(Structure_IX);
}

void ChemicalCarryall::deploy(const Coord& newLocation)
{
    Carryall::deploy(newLocation);
    respondable = true;
}

bool ChemicalCarryall::isValidHealTarget(const ObjectBase* pObject) const
{
    const auto* pUnit = dynamic_cast<const UnitBase*>(pObject);
    return pUnit != nullptr && pUnit != this && pUnit->getHealth() > 0
        && pUnit->getHealth() < pUnit->getMaxHealth()
        && pUnit->getOwner()->getTeamID() == owner->getTeamID();
}

UnitBase* ChemicalCarryall::findDamagedAlly(int range) const
{
    UnitBase* pBest = nullptr;
    int bestDistance = range + 1;
    for(UnitBase* pUnit : unitList) {
        if(!isValidHealTarget(pUnit)) {
            continue;
        }
        const Coord targetLocation = pUnit->getLocation();
        const int distance = std::max(std::abs(targetLocation.x - location.x), std::abs(targetLocation.y - location.y));
        if(distance < bestDistance) {
            bestDistance = distance;
            pBest = pUnit;
        }
    }
    return pBest;
}

void ChemicalCarryall::fireHealingMissile(UnitBase* pTarget)
{
    if(pTarget == nullptr || healingReloadTimer > 0) {
        return;
    }
    Coord source = getCenterPoint();
    Coord destination = pTarget->getCenterPoint();
    bulletList.push_back(new Bullet(objectID, &source, &destination, Bullet_Heal,
                                    CHEMICAL_HEAL_AMOUNT, pTarget->isAFlyingUnit(), pTarget));
    healingReloadTimer = CHEMICAL_HEAL_RELOAD;
}

void ChemicalCarryall::healCargo()
{
    if(healingReloadTimer > 0) {
        return;
    }
    bool healedCargo = false;
    for(const Uint32 pickedUpUnitID : pickedUpUnitList) {
        UnitBase* pUnit = static_cast<UnitBase*>(currentGame->getObjectManager().getObject(pickedUpUnitID));
        if(pUnit == nullptr) {
            continue;
        }
        if(pUnit->getHealth() < pUnit->getMaxHealth()) {
            pUnit->setHealth(std::min(FixPoint(pUnit->getMaxHealth()), pUnit->getHealth() + FixPoint(CHEMICAL_HEAL_AMOUNT)));
            healedCargo = true;
        }
    }
    if(healedCargo) {
        healingReloadTimer = CHEMICAL_HEAL_RELOAD;
    }
}

void ChemicalCarryall::doAttackObject(Uint32 targetObjectID, bool bForced)
{
    ObjectBase* pObject = currentGame->getObjectManager().getObject(targetObjectID);
    if(isValidHealTarget(pObject)) {
        healingTarget.pointTo(pObject);
        forced = false;
        return;
    }
    Carryall::doAttackObject(targetObjectID, bForced);
}

bool ChemicalCarryall::update()
{
    if(!Carryall::update()) {
        return false;
    }
    if(healingReloadTimer > 0) {
        --healingReloadTimer;
    }
    healCargo();

    UnitBase* pHealTarget = dynamic_cast<UnitBase*>(healingTarget.getObjPointer());
    if(!isValidHealTarget(pHealTarget)) {
        healingTarget.pointTo(nullptr);
        pHealTarget = findDamagedAlly(CHEMICAL_CARRYALL_HEAL_RANGE);
    }
    if(pHealTarget != nullptr) {
        const Coord targetLocation = pHealTarget->getLocation();
        const int distance = std::max(std::abs(targetLocation.x - location.x), std::abs(targetLocation.y - location.y));
        if(distance <= CHEMICAL_CARRYALL_HEAL_RANGE) {
            fireHealingMissile(pHealTarget);
        }
    }
    return true;
}