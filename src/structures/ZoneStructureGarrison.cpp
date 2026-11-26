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

namespace {

UnitBase* occupantUnit(Uint32 objectID) {
    return dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(objectID));
}

} // namespace

bool ZoneStructure::garrisonsEnabled() {
    return currentGame != nullptr && currentGame->getGameInitSettings().getGameOptions().zoneGarrisons;
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
       || getLocation().isInvalid()) return false;
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
    if(taken) transferOwnership(house);
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
                .set("remaining", getGarrisonSize()).set("destroyed", destroying));
    }
    return deployed;
}

void ZoneStructure::transferOwnership(House* newOwner) {
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
    AchievementEvents::captured(newOwner, previous);
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
    garrison_.erase(std::remove_if(garrison_.begin(), garrison_.end(), [](Uint32 id) {
        const UnitBase* unit = occupantUnit(id);
        return unit == nullptr || unit->getHealth() <= 0;
    }), garrison_.end());
    if(garrison_.empty()) garrisonTargetID_ = NONE_ID;
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
    // edge, tinted toward the garrison's house colour along the top seam so a held zone reads at
    // a glance. Bags are laid in staggered courses, two high, with a dark outline and a light
    // top face; gaps at the middle of each side are the firing slits.
    const int zoom = currentZoomlevel + 1;
    const int x0 = screenborder->world2screenX(lround(realX));
    const int y0 = screenborder->world2screenY(lround(realY));
    const int scaleW = screenborder->world2screenX(lround(realX) + structureSize.x * TILESIZE) - x0;
    const int scaleH = screenborder->world2screenY(lround(realY) + structureSize.y * TILESIZE) - y0;
    const int bagW = std::max(3, scaleW / 9);
    const int bagH = std::max(2, scaleH / 14);
    const House* house = getGarrisonHouse();
    const int houseColor = house ? house->getHouseID() : owner->getHouseID();
    const SDL_Color seam = getHouseColorSDL(getHouseVisualHouse(houseColor));

    auto bag = [&](int x, int y, int bw, int bh) {
        SDL_Rect body{x, y, bw, bh};
        SDL_SetRenderDrawColor(renderer, 182, 152, 98, 255);    // sacking
        SDL_RenderFillRect(renderer, &body);
        SDL_Rect top{x + 1, y, std::max(1, bw - 2), std::max(1, bh / 3)};
        SDL_SetRenderDrawColor(renderer, 214, 188, 132, 255);   // sunlit top
        SDL_RenderFillRect(renderer, &top);
        SDL_Rect tie{x + bw / 2, y + 1, std::max(1, bw / 8), std::max(1, bh - 2)};
        SDL_SetRenderDrawColor(renderer, 140, 112, 70, 255);    // stitched seam
        SDL_RenderFillRect(renderer, &tie);
        SDL_SetRenderDrawColor(renderer, 62, 46, 28, 255);      // outline
        SDL_RenderDrawRect(renderer, &body);
    };
    // Two staggered courses along one edge, leaving the middle bag out as a firing slit.
    auto wallH = [&](int y) {
        const int count = std::max(4, scaleW / bagW);
        for(int course = 0; course < 2; ++course) {
            const int offset = course == 0 ? 0 : bagW / 2;
            for(int i = 0; i < count; ++i) {
                if(i == count / 2) continue;
                const int x = x0 + offset + i * bagW;
                if(x + bagW > x0 + scaleW) break;
                bag(x, y - course * (bagH - 1), bagW, bagH);
            }
        }
    };
    auto wallV = [&](int x) {
        const int count = std::max(4, scaleH / bagW);
        for(int i = 0; i < count; ++i) {
            if(i == count / 2) continue;
            const int y = y0 + i * bagW;
            if(y + bagW > y0 + scaleH) break;
            bag(x, y, bagH + 1, bagW);
        }
    };
    wallV(x0);
    wallV(x0 + scaleW - bagH - 1);
    wallH(y0 + scaleH - bagH);
    wallH(y0 + bagH);
    // The house's colour along the front course, as a pennant line over the sandbags.
    SDL_SetRenderDrawColor(renderer, seam.r, seam.g, seam.b, 255);
    SDL_Rect pennant{x0 + bagW, y0 + scaleH - bagH - 1, std::max(1, scaleW - 2 * bagW), std::max(1, zoom / 2 + 1)};
    SDL_RenderFillRect(renderer, &pennant);

    // One small helmet per soldier inside, in the top-left corner: how full it is at a glance.
    for(int i = 0; i < getGarrisonSize(); ++i) {
        SDL_Rect helmet{x0 + bagH + 2 + i * (bagW / 2 + 2), y0 + 2 * bagH + 2, std::max(2, bagW / 2), std::max(2, bagH)};
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(renderer, &helmet);
        SDL_Rect band{helmet.x, helmet.y + helmet.h - 1, helmet.w, 1};
        SDL_SetRenderDrawColor(renderer, seam.r, seam.g, seam.b, 255);
        SDL_RenderFillRect(renderer, &band);
    }
}
