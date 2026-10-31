/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tornie Gas Infantry behavior by Tornie_Panther, adapted for DuneCity.
 */
#include <units/GasInfantry.h>
#include <mod/HealingBalance.h>
#include <FileClasses/GFXManager.h>
#include <globals.h>
#include <Game.h>
#include <House.h>
#include <Bullet.h>
#include <SoundPlayer.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <algorithm>
#include <cstdlib>

namespace {
constexpr int GAS_INFANTRY_HEAL_AMOUNT = 15;
constexpr int GAS_INFANTRY_HEAL_RELOAD = 120;
}

GasInfantry::GasInfantry(House* owner) : InfantryBase(owner) {
    init();
    setHealth(getMaxHealth());
}

GasInfantry::GasInfantry(InputStream& stream) : InfantryBase(stream) {
    init();
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_TORNIE546) {
        healingReloadTimer = stream.readSint32();
        healingTarget.load(stream);
        manualHealing = stream.readBool();
    }
}

void GasInfantry::save(OutputStream& stream) const {
    InfantryBase::save(stream);
    stream.writeSint32(healingReloadTimer);
    healingTarget.save(stream);
    stream.writeBool(manualHealing);
}

void GasInfantry::init() {
    itemID = Unit_GasInfantry;
    registerUnit();
    numWeapons = 1;
    bulletType = Bullet_DRocket;
    graphicID = ObjPic_GasInfantry;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 3;
}

bool GasInfantry::canAttack(const ObjectBase* object) const {
    return ModManager::instance().isTornieFamilyContentActive() && object
        && !object->isAStructure() && object->getHealth() > 0
        && object->getOwner()->getTeamID() != owner->getTeamID()
        && object->isVisible(owner->getTeamID()) && !object->isAFlyingUnit();
}

void GasInfantry::playAttackSound() {
    soundPlayer->playSoundAt(Sound_RocketSmall, location);
}

bool GasInfantry::isValidHealTarget(const ObjectBase* object) const {
    const auto* unit = dynamic_cast<const UnitBase*>(object);
    return unit && unit != this && unit->getHealth() > 0
        && unit->getHealth() < unit->getMaxHealth()
        && unit->getOwner()->getTeamID() == owner->getTeamID();
}

UnitBase* GasInfantry::findDamagedAlly() const {
    UnitBase* best = nullptr;
    int bestDistance = getAreaGuardRange() + 1;
    for(UnitBase* unit : unitList) {
        if(!isValidHealTarget(unit)) continue;
        const Coord position = unit->getLocation();
        const int guardDistance = std::max(std::abs(position.x - guardPoint.x), std::abs(position.y - guardPoint.y));
        if(guardDistance > getAreaGuardRange()) continue;
        const int distance = std::max(std::abs(position.x - location.x), std::abs(position.y - location.y));
        if(distance < bestDistance) {
            bestDistance = distance;
            best = unit;
        }
    }
    return best;
}

void GasInfantry::fireHealingMissile(UnitBase* unit) {
    if(!unit || healingReloadTimer > 0) return;
    Coord source = getCenterPoint();
    Coord destination = unit->getCenterPoint();
    bulletList.push_back(new Bullet(objectID, &source, &destination, Bullet_Heal,
        GAS_INFANTRY_HEAL_AMOUNT, unit->isAFlyingUnit(), unit));
    healingReloadTimer = healing::reload(GAS_INFANTRY_HEAL_RELOAD, getProductionHouseID());
}

void GasInfantry::doAttackObject(Uint32 targetObjectID, bool forced) {
    ObjectBase* object = currentGame->getObjectManager().getObject(targetObjectID);
    if(ModManager::instance().isTornieFamilyContentActive() && isValidHealTarget(object)) {
        healingTarget.pointTo(object);
        target.pointTo(nullptr);
        this->forced = false;
        manualHealing = true;
        return;
    }
    healingTarget.pointTo(nullptr);
    manualHealing = false;
    InfantryBase::doAttackObject(targetObjectID, forced);
}

bool GasInfantry::update() {
    if(!InfantryBase::update()) return false;
    if(!ModManager::instance().isTornieFamilyContentActive()) return true;
    if(healingReloadTimer > 0) --healingReloadTimer;
    if(wasForced() || hasATarget()) {
        healingTarget.pointTo(nullptr);
        manualHealing = false;
        return true;
    }
    UnitBase* unit = dynamic_cast<UnitBase*>(healingTarget.getObjPointer());
    if(!isValidHealTarget(unit)) {
        healingTarget.pointTo(nullptr);
        manualHealing = false;
        unit = nullptr;
        if(getAttackMode() == AREAGUARD) {
            unit = findDamagedAlly();
            healingTarget.pointTo(unit);
        }
    }
    if(!unit) return true;
    const Coord position = unit->getLocation();
    const int distance = std::max(std::abs(position.x - location.x), std::abs(position.y - location.y));
    if(distance <= getWeaponRange()) {
        setDestination(location);
        fireHealingMissile(unit);
    } else {
        setDestination(position);
    }
    return true;
}
