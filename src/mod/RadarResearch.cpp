// SPDX-License-Identifier: GPL-2.0-or-later
#include <House.h>
#include <globals.h>
#include <Game.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <units/UnitBase.h>
#include <structures/StructureBase.h>
#include <algorithm>
#include <stdexcept>

bool House::hasRadarResearch(int kind) const {
    return radar::valid(kind) && (radarResearch.completed & (1U << kind)) != 0
        && ModManager::instance().isTornieFamilyContentActive() && !isNatives();
}

bool House::hasRadarSpecialization(HOUSETYPE identity) const {
    return hasRadarResearch(radar::Specialization)
        && isHouseFaction(static_cast<HOUSETYPE>(getFactionID()), identity);
}

bool House::canStartRadarResearch(int kind) const {
    return radar::valid(kind) && currentGame && !isNatives()
        && ModManager::instance().isTornieFamilyContentActive() && hasRadarOn()
        && radarResearch.active < 0 && !hasRadarResearch(kind)
        && currentGame->techLevel >= radar::level(kind)
        && (kind != radar::Specialization || hasIX()) && getCredits() >= radar::cost(kind);
}

bool House::startRadarResearch(int kind) {
    if(!canStartRadarResearch(kind)) return false;
    takeCredits(radar::cost(kind));
    radarResearch.active = kind;
    radarResearch.remaining = MILLI2CYCLES(radar::seconds(kind) * 1000);
    return true;
}

void House::updateRadarResearch() {
    if(radarResearch.active < 0 || !ModManager::instance().isTornieFamilyContentActive() || !hasRadarOn()) return;
    if(--radarResearch.remaining > 0) return;
    const int kind = radarResearch.active;
    radarResearch.active = -1;
    radarResearch.remaining = 0;
    radarResearch.completed |= 1U << kind;
    const bool upgradeAircraft = kind == radar::Specialization && hasRadarSpecialization(HOUSE_WILDSPADE);
    for(auto* unit : unitList) if(unit->getOwner() == this) {
        if(upgradeAircraft && radar::aircraft(unit->getItemID())) {
            // Before this specialization the maximum was exactly the production-house base.
            // Reading it directly preserves damage ratios without a temporary aircraft vector.
            const int previousMaximum = currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].hitpoints;
            if(previousMaximum > 0)
                unit->setHealth(unit->getHealth() / previousMaximum * unit->getMaxHealth());
        }
        unit->refreshRadarResearch();
        if(currentGameMap && currentGameMap->tileExists(unit->getLocation()))
            currentGameMap->viewMap(houseID, unit->getLocation(), unit->getViewRange());
    }
    if(currentGameMap) for(auto* structure : structureList) if(structure->getOwner() == this)
        currentGameMap->viewMap(houseID, structure->getLocation(), structure->getViewRange());
    updateBuildLists();
}

int House::radarUnitPrice(int item, int price) const {
    return hasRadarSpecialization(HOUSE_MERCENARY) && radar::nonHeavyUnit(item)
        ? std::max(1, price * 90 / 100) : price;
}

FixPoint House::radarSpeedFactor(int item) const {
    if((hasRadarSpecialization(HOUSE_ATREIDES) || hasRadarSpecialization(HOUSE_FREMEN))
       && (item == Unit_Trooper || item == Unit_FlameTrooper || item == Unit_Troopers || item == Unit_Troopers5)) return 1.15_fix;
    if(hasRadarSpecialization(HOUSE_ORDOS) && radar::lightVehicle(item)) return 1.15_fix;
    if(hasRadarSpecialization(HOUSE_CUSTOM) && radar::conventionalTank(item)) return 0.9_fix;
    return 1_fix;
}

int House::radarSightBonus(int item) const {
    if(isFlyingUnit(item)) return 0;
    return (hasRadarResearch(radar::ExtendedVision) ? 2 : 0)
        + (hasRadarSpecialization(HOUSE_THARPIQUE) && radar::scoutOrArtillery(item) ? 2 : 0);
}

int House::radarWeaponRangeBonus(int item, int base) const {
    return base > 0 && isUnit(item) && !isInfantryUnit(item) && !isFlyingUnit(item) && !isSandwormUnit(item)
        && hasRadarSpecialization(HOUSE_REBELS) ? 1 : 0;
}

int House::radarAttackDamage(int item, int damage) const {
    return hasRadarSpecialization(HOUSE_HARKONNEN)
        && (item == Unit_Tank || item == Unit_Devastator || item == Structure_Palace) ? damage * 120 / 100 : damage;
}

void House::saveRadarResearch(OutputStream& stream) const {
    stream.writeUint8(radarResearch.completed);
    stream.writeSint32(radarResearch.active);
    stream.writeSint32(radarResearch.remaining);
}

void House::loadRadarResearch(InputStream& stream) {
    radarResearch.completed = stream.readUint8();
    radarResearch.active = stream.readSint32();
    radarResearch.remaining = stream.readSint32();
    if(radarResearch.completed > 7 || radarResearch.active < -1 || radarResearch.active >= radar::Count
       || radarResearch.remaining < 0 || radarResearch.remaining > MILLI2CYCLES(60000)
       || (radarResearch.active < 0 && radarResearch.remaining != 0)
       || (radarResearch.active >= 0 && (radarResearch.remaining == 0
           || (radarResearch.completed & (1U << radarResearch.active)))))
        throw std::runtime_error("Invalid Radar research state");
}
