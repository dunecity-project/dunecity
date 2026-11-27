/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

// Zone Garrisons (docs/zone-garrisons.md): Soldiers and Troopers sent with the capture order
// occupy Residential, Commercial and Industrial zones. Inside they are hidden like a Carryall's
// cargo and fire out with their own weapons; an enemy zone they walk into becomes their house's.

#include <structures/ZoneStructure.h>

#include <globals.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <Tile.h>
#include <ScreenBorder.h>
#include <Achievements/AchievementEvents.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <players/AIDecisionLog.h>
#include <units/UnitBase.h>

#include <algorithm>
#include <cstdlib>

namespace {

UnitBase* occupantUnit(Uint32 objectID) {
    return dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(objectID));
}

} // namespace

bool ZoneStructure::garrisonsEnabled() {
    return currentGame != nullptr && currentGame->getGameInitSettings().getGameOptions().zoneGarrisons;
}

int ZoneStructure::evacuateHealthPercent() {
    // Threshold experiments only (tests/ai matches on the queue): DUNECITY_TEST_GARRISON_EVACUATE_PERCENT
    // sets it for a whole process, read once (0 = never leave). Never set in a game people play: it
    // is not part of the match rules and two players with different values would fall out of step.
    static const int percent = [] {
        const char* text = std::getenv("DUNECITY_TEST_GARRISON_EVACUATE_PERCENT");
        if(text == nullptr || *text == '\0') return DuneCity::ZoneGarrison::kEvacuateHealthPercent;
        return std::clamp(std::atoi(text), 0, 100);
    }();
    return percent;
}

bool ZoneStructure::tooDamagedToHold() const {
    return DuneCity::ZoneGarrison::belowEvacuateHealth(getHealth().lround(), getMaxHealth(), evacuateHealthPercent());
}

const House* ZoneStructure::getGarrisonHouse() const {
    for(const Uint32 id : garrison_) {
        if(const UnitBase* unit = occupantUnit(id)) return unit->getOwner();
    }
    return nullptr;
}

bool ZoneStructure::mayEnterGarrison(const UnitBase* unit) const {
    if(!garrisonsEnabled() || unit == nullptr || owner == nullptr || getHealth() <= 0
       || !unit->isActive() || unit->getHealth() <= 0 || !unit->canCaptureStructures()
       || getLocation().isInvalid() || tooDamagedToHold()) return false;
    const House* garrisonHouse = getGarrisonHouse();
    const auto entry = DuneCity::ZoneGarrison::entryFor(
        unit->getOwner()->getHouseID(), unit->getOwner()->getTeamID(),
        owner->getHouseID(), owner->getTeamID(), getGarrisonSize(),
        garrisonHouse ? garrisonHouse->getHouseID() : owner->getHouseID());
    return entry == DuneCity::ZoneGarrison::Entry::Enter
        || entry == DuneCity::ZoneGarrison::Entry::EnterAndTake;
}

bool ZoneStructure::heldAgainst(const UnitBase* unit) const {
    if(!garrisonsEnabled() || unit == nullptr || garrison_.empty()) return false;
    const House* garrisonHouse = getGarrisonHouse();
    return garrisonHouse != nullptr && garrisonHouse->getTeamID() != unit->getOwner()->getTeamID();
}

bool ZoneStructure::enterGarrison(UnitBase* unit) {
    if(!mayEnterGarrison(unit)) return false;
    House* house = unit->getOwner();
    const bool taken = house->getTeamID() != owner->getTeamID();
    const int previousHouse = owner->getHouseID();
    if(taken) {
        // An empty zone is never a captured one (it went back when its garrison left), so the
        // house it is taken from is the one it returns to.
        garrisonOriginalOwner_ = previousHouse;
        transferOwnership(house);
    }
    unit->enterGarrison();
    garrison_.push_back(unit->getObjectID());
    garrisonSearchTimer_ = 0;
    if(AITelemetry::log().enabled()) {
        const Coord pos = getLocation();
        AITelemetry::log().write(currentGame->getGameCycleCount(), house->getHouseID(), -1,
            taken ? "zone_garrison_taken" : "zone_garrison_enter",
            AITelemetry::Record().set("zone", objectID).set("item", itemID).set("unit", unit->getObjectID())
                .set("unit_item", unit->getItemID()).set("previous_house", previousHouse)
                .set("occupants", getGarrisonSize()).set("x", pos.x).set("y", pos.y));
    }
    return true;
}

int ZoneStructure::evacuateGarrison(bool destroying) {
    if(garrison_.empty()) return 0;
    int deployed = 0;
    std::vector<Uint32> remaining;
    const auto occupants = garrison_;
    garrison_.clear();
    garrisonTargetID_ = NONE_ID;
    for(const Uint32 id : occupants) {
        UnitBase* unit = occupantUnit(id);
        if(unit == nullptr || unit->getHealth() <= 0) continue;
        Coord spot = Coord::Invalid();
        if(currentGameMap != nullptr && getLocation().isValid()) {
            spot = currentGameMap->findDeploySpot(unit, getLocation(), currentGame->randomGen,
                                                  Coord::Invalid(), structureSize);
        }
        if(spot.isValid() && currentGameMap->tileExists(spot)) {
            unit->setGuardPoint(spot);
            unit->setForced(false);
            unit->deploy(spot);
            // Thrown out of the ruin: hurt, never killed (DuneCity::ZoneGarrison::healthAfterEjection).
            if(destroying) unit->setHealth(DuneCity::ZoneGarrison::healthAfterEjection(
                unit->getHealth().lround(), unit->getMaxHealth()));
            ++deployed;
        } else if(destroying) {
            unit->setHealth(0);    // Buried in the ruin; removed on its next update.
        } else {
            remaining.push_back(id);
        }
    }
    garrison_ = std::move(remaining);
    if(AITelemetry::log().enabled() && owner != nullptr) {
        AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "zone_garrison_exit",
            AITelemetry::Record().set("zone", objectID).set("deployed", deployed)
                .set("remaining", getGarrisonSize()).set("destroyed", destroying)
                .set("low_health", !destroying && tooDamagedToHold())
                .set("health", getHealth().lround()).set("max_health", getMaxHealth()));
    }
    if(!destroying && garrison_.empty()) returnCapturedZone();
    return deployed;
}

void ZoneStructure::returnCapturedZone() {
    if(garrisonOriginalOwner_ < 0 || owner == nullptr) return;
    House* original = currentGame->getHouse(garrisonOriginalOwner_);
    garrisonOriginalOwner_ = -1;
    // A house the capture defeated gets nothing back: the zone stays with the house that took it.
    if(original == nullptr || original == owner || !original->isAlive()) return;
    const int holder = owner->getHouseID();
    transferOwnership(original, false);
    if(AITelemetry::log().enabled()) {
        const Coord pos = getLocation();
        AITelemetry::log().write(currentGame->getGameCycleCount(), original->getHouseID(), -1,
            "zone_garrison_returned", AITelemetry::Record().set("zone", objectID).set("item", itemID)
                .set("holder", holder).set("health", getHealth().lround()).set("x", pos.x).set("y", pos.y));
    }
}

void ZoneStructure::transferOwnership(House* newOwner, bool capture) {
    if(newOwner == nullptr || newOwner == owner || owner == nullptr) return;
    House* previous = owner;
    // The zone's live power draw belongs to whoever owns it.
    if(registeredZonePower_ != 0) {
        previous->adjustPowerRequirement(-registeredZonePower_);
        registeredZonePower_ = 0;
    }
    owner = newOwner;
    newOwner->incrementStructures(itemID);
    // Last, so a house that loses its last building here is defeated with the zone already gone.
    previous->decrementStructures(itemID, location, true);
    graphic = pGFXManager->getObjPic(graphicID, newOwner->getHouseID());
    if(currentGameMap != nullptr && location.isValid()) {
        for(int dy = 0; dy < structureSize.y; ++dy) {
            for(int dx = 0; dx < structureSize.x; ++dx) {
                if(Tile* tile = currentGameMap->getTile(location.x + dx, location.y + dy))
                    tile->setOwner(newOwner->getHouseID());
            }
        }
        currentGameMap->viewMap(newOwner->getHouseID(), location, getViewRange());
    }
    refreshZonePowerDraw();
    garrisonTargetID_ = NONE_ID;
    if(capture) AchievementEvents::captured(newOwner, previous);
}

void ZoneStructure::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                                 const DeviationReward::Provenance& provenance) {
    // Cover, not immunity: the soldier at the front takes half of every hit on a held zone.
    if(damage > 0 && getHealth() > 0 && !garrison_.empty()) {
        if(UnitBase* front = occupantUnit(garrison_.front()); front != nullptr && front->getHealth() > 0) {
            front->ObjectBase::handleDamage(DuneCity::ZoneGarrison::occupantDamage(damage),
                                            damagerID, damagerOwner, provenance);
        }
        dropDeadOccupants();
    }
    StructureBase::handleDamage(damage, damagerID, damagerOwner, provenance);
}

void ZoneStructure::dropDeadOccupants() {
    if(garrison_.empty()) return;
    garrison_.erase(std::remove_if(garrison_.begin(), garrison_.end(), [](Uint32 id) {
        const UnitBase* unit = occupantUnit(id);
        return unit == nullptr || unit->getHealth() <= 0;
    }), garrison_.end());
    if(garrison_.empty()) {
        garrisonTargetID_ = NONE_ID;
        // The last soldier inside was killed: a captured zone is held only while occupied.
        if(getHealth() > 0) returnCapturedZone();
    }
}

const ObjectBase* ZoneStructure::findGarrisonTarget(int range) const {
    const ObjectBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    auto consider = [&](const ObjectBase* candidate) {
        if(candidate == nullptr || !candidate->isAUnit() || !candidate->isActive() || candidate->getHealth() <= 0
           || candidate->getOwner()->getTeamID() == owner->getTeamID()
           || !candidate->isVisible(owner->getTeamID())) return;
        const Coord at = candidate->getLocation();
        const FixPoint distance = blockDistance(getClosestPoint(at), at);
        if(distance > range) return;
        bool someoneCan = false;
        for(const Uint32 id : garrison_) {
            const UnitBase* unit = occupantUnit(id);
            if(unit != nullptr && unit->canAttack(candidate) && distance <= unit->getWeaponRange()) {
                someoneCan = true;
                break;
            }
        }
        if(!someoneCan) return;
        // The lower object ID decides a tie, so every peer picks the same target.
        if(distance < bestDistance || (distance == bestDistance && candidate->getObjectID() < best->getObjectID())) {
            best = candidate;
            bestDistance = distance;
        }
    };
    const Coord pos = getLocation();
    if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
        const Coord low = grid->clampToCell(Coord(std::max(0, pos.x - range), std::max(0, pos.y - range)));
        const Coord high = grid->clampToCell(Coord(
            std::min(currentGameMap->getSizeX() - 1, pos.x + structureSize.x - 1 + range),
            std::min(currentGameMap->getSizeY() - 1, pos.y + structureSize.y - 1 + range)));
        std::vector<SpatialGridEntry> entries;
        for(int y = low.y; y <= high.y; ++y) {
            for(int x = low.x; x <= high.x; ++x) {
                const Coord cell(x, y);
                entries.clear();
                if(!grid->isCellCoordValid(cell) || !grid->collectEntries(cell, entries)) continue;
                for(const SpatialGridEntry& entry : entries) {
                    if(entry.object != nullptr
                       && entry.object->getGridHandle().matches(entry.key.objectId, entry.key.generation))
                        consider(entry.object);
                }
            }
        }
    } else {
        for(const UnitBase* unit : unitList) consider(unit);
    }
    return best;
}

void ZoneStructure::updateGarrison() {
    dropDeadOccupants();
    if(garrison_.empty() || owner == nullptr || currentGameMap == nullptr || getLocation().isInvalid()) return;
    // Below the threshold the garrison leaves on its own (any with no free tile around stay and
    // try again next cycle); a captured zone goes back to its original owner as they do.
    if(tooDamagedToHold()) {
        evacuateGarrison();
        if(garrison_.empty()) return;
    }

    bool anyReady = false;
    int range = 0;
    for(const Uint32 id : garrison_) {
        UnitBase* unit = occupantUnit(id);
        unit->tickGarrisonWeapon();
        anyReady = anyReady || unit->garrisonWeaponReady();
        range = std::max(range, unit->getWeaponRange());
    }
    if(!anyReady || range <= 0) return;

    const ObjectBase* target = currentGame->getObjectManager().getObject(garrisonTargetID_);
    if(target != nullptr) {
        const Coord at = target->getLocation();
        if(!target->isActive() || target->getHealth() <= 0 || !target->isAUnit()
           || target->getOwner()->getTeamID() == owner->getTeamID()
           || !target->isVisible(owner->getTeamID())
           || blockDistance(getClosestPoint(at), at) > range) {
            target = nullptr;
        }
    }
    if(target == nullptr) {
        garrisonTargetID_ = NONE_ID;
        if(garrisonSearchTimer_ > 0) {
            --garrisonSearchTimer_;
            return;
        }
        target = findGarrisonTarget(range);
        if(target == nullptr) {
            garrisonSearchTimer_ = DuneCity::ZoneGarrison::kIdleSearchCycles;
            return;
        }
        garrisonTargetID_ = target->getObjectID();
    }

    const Coord fromTile = getClosestPoint(target->getLocation());
    const Coord muzzle(fromTile.x * TILESIZE + TILESIZE / 2, fromTile.y * TILESIZE + TILESIZE / 2);
    const FixPoint distance = blockDistance(fromTile, target->getLocation());
    for(const Uint32 id : garrison_) {
        UnitBase* unit = occupantUnit(id);
        if(unit->garrisonWeaponReady() && unit->canAttack(target) && distance <= unit->getWeaponRange()) {
            unit->fireFromGarrison(target, muzzle, fromTile);
        }
    }
}

void ZoneStructure::drawGarrisonSandbags() const {
    // Original pixel art, drawn rather than loaded: a ring of stacked sandbags hugging the lot's
    // edge, two staggered courses on every side with a gap in the middle of each side as a firing
    // slit, the garrison house's colour along the front course, and an olive helmet per soldier.
    const int x0 = screenborder->world2screenX(lround(realX));
    const int y0 = screenborder->world2screenY(lround(realY));
    const int w = screenborder->world2screenX(lround(realX) + structureSize.x * TILESIZE) - x0;
    const int h = screenborder->world2screenY(lround(realY) + structureSize.y * TILESIZE) - y0;
    const int bagL = std::max(5, w / 7);        // a bag's long side
    const int bagS = std::max(3, h / 10);       // its short side
    const House* house = getGarrisonHouse();
    const int houseColor = house ? house->getHouseID() : owner->getHouseID();
    const SDL_Color seam = getHouseColorSDL(getHouseVisualHouse(houseColor));

    auto fill = [](int x, int y, int bw, int bh, Uint8 r, Uint8 g, Uint8 b) {
        SDL_Rect rect{x, y, std::max(1, bw), std::max(1, bh)};
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderFillRect(renderer, &rect);
    };
    // One bag: dark outline, sacking body, sunlit face on its top or left side, a tie in the middle.
    auto bag = [&](int x, int y, int bw, int bh, bool horizontal) {
        fill(x, y, bw, bh, 58, 42, 24);
        fill(x + 1, y + 1, bw - 2, bh - 2, 176, 146, 92);
        if(horizontal) {
            fill(x + 1, y + 1, bw - 2, std::max(1, (bh - 2) / 3), 216, 190, 134);
            fill(x + bw / 2, y + 1, 1, bh - 2, 128, 100, 62);
        } else {
            fill(x + 1, y + 1, std::max(1, (bw - 2) / 3), bh - 2, 216, 190, 134);
            fill(x + 1, y + bh / 2, bw - 2, 1, 128, 100, 62);
        }
    };
    // A wall of two staggered courses along one side; the middle bag of the outer course is left
    // out as a firing slit.
    auto wallH = [&](int y, bool outerBelow) {
        for(int course = 0; course < 2; ++course) {
            const int offset = course == 0 ? 0 : bagL / 2;
            const int cy = outerBelow ? y + (1 - course) * (bagS - 1) : y + course * (bagS - 1);
            const int count = (w - offset) / bagL;
            for(int i = 0; i < count; ++i) {
                if(course == 0 && i == count / 2) continue;
                bag(x0 + offset + i * bagL, cy, bagL, bagS, true);
            }
        }
    };
    auto wallV = [&](int x, bool outerRight) {
        for(int course = 0; course < 2; ++course) {
            const int offset = bagS * 2 + (course == 0 ? 0 : bagL / 2);
            const int cx = outerRight ? x + (1 - course) * (bagS - 1) : x + course * (bagS - 1);
            const int count = (h - 2 * offset) / bagL;
            for(int i = 0; i < count; ++i) {
                if(course == 0 && i == count / 2) continue;
                bag(cx, y0 + offset + i * bagL, bagS, bagL, false);
            }
        }
    };
    wallV(x0, false);
    wallV(x0 + w - 2 * bagS + 1, true);
    wallH(y0, false);
    wallH(y0 + h - 2 * bagS + 1, true);
    // The garrison house's colour along the front course, as a pennant line over the bags.
    fill(x0 + bagL, y0 + h - 2 * bagS, w - 2 * bagL, std::max(2, bagS / 2), seam.r, seam.g, seam.b);

    // An olive helmet per soldier inside, along the top wall: how full it is at a glance.
    const int helmet = std::max(3, bagS + 1);
    for(int i = 0; i < getGarrisonSize(); ++i) {
        const int hx = x0 + 2 * bagS + 1 + i * (helmet + 2);
        const int hy = y0 + 2 * bagS;
        fill(hx, hy, helmet, helmet, 20, 20, 14);                       // outline
        fill(hx + 1, hy + 1, helmet - 2, helmet - 2, 92, 104, 52);      // olive drab
        fill(hx + 1, hy + 1, std::max(1, (helmet - 2) / 2), 1, 150, 164, 96);   // highlight
        fill(hx, hy + helmet - 1, helmet, 1, seam.r, seam.g, seam.b);   // house-coloured band
    }
}
