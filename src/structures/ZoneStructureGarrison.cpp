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
#include <FileClasses/TextManager.h>
#include <players/AIDecisionLog.h>
#include <units/UnitBase.h>

#include <algorithm>
#include <cstdlib>

namespace {

UnitBase* occupantUnit(Uint32 objectID) {
    return dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(objectID));
}

/// The house of the first live soldier of OCCUPANTS, or -1.
int getGarrisonHouseID(const std::vector<Uint32>& occupants) {
    for(const Uint32 id : occupants) {
        if(const UnitBase* unit = occupantUnit(id)) return unit->getOwner()->getHouseID();
    }
    return -1;
}

} // namespace

bool ZoneStructure::garrisonsEnabled() {
    return currentGame != nullptr && currentGame->isCitySimEnabled()
        && currentGame->getGameInitSettings().getGameOptions().garrisoningAndCapturing;
}

bool ZoneStructure::garrisonOrderAvailable(const ObjectBase* unit) {
    return unit != nullptr && unit->isAUnit() && garrisonsEnabled()
        && static_cast<const UnitBase*>(unit)->canCaptureStructures();
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
    garrisonHealthRemainders_[garrison_.size()] = {};
    garrison_.push_back(unit->getObjectID());
    garrisonSearchTimer_ = 0;
    SDL_Log("ZONE_GARRISON: event=entered house=%d unit=%u item=%d zone=%u taken=%d previous_house=%d occupants=%d cycle=%u",
            house->getHouseID(), unit->getObjectID(), unit->getItemID(), objectID, taken ? 1 : 0, previousHouse,
            getGarrisonSize(), currentGame->getGameCycleCount());
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
    int hurt = 0;
    const bool lowHealth = !destroying && tooDamagedToHold();
    std::vector<Uint32> remaining;
    const auto occupants = garrison_;
    garrison_.clear();
    garrisonTargetID_ = NONE_ID;
    for(size_t i = 0; i < occupants.size(); ++i) {
        const Uint32 id = occupants[i];
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
            if(destroying) {
                const int before = unit->getHealth().lround();
                unit->setHealth(DuneCity::ZoneGarrison::healthAfterEjection(before, unit->getMaxHealth()));
                hurt += before - unit->getHealth().lround();
            }
            ++deployed;
        } else if(destroying) {
            unit->setHealth(0);    // Buried in the ruin; removed on its next update.
        } else {
            garrisonHealthRemainders_[remaining.size()] = garrisonHealthRemainders_[i];
            remaining.push_back(id);
        }
    }
    garrison_ = std::move(remaining);
    std::fill(garrisonHealthRemainders_.begin() + garrison_.size(), garrisonHealthRemainders_.end(),
              GarrisonHealthRemainders{});
    if(owner != nullptr) {
        SDL_Log("ZONE_GARRISON: event=exit house=%d zone=%u deployed=%d remaining=%d destroyed=%d low_health=%d hurt=%d health=%d max_health=%d shots=%d air_shots=%d cycle=%u",
                getGarrisonHouseID(occupants), objectID, deployed, getGarrisonSize(), destroying ? 1 : 0, lowHealth ? 1 : 0,
                hurt, getHealth().lround(), getMaxHealth(), garrisonShots_, garrisonAirShots_, currentGame->getGameCycleCount());
    }
    if(garrison_.empty()) garrisonShots_ = garrisonAirShots_ = 0;
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
    SDL_Log("ZONE_GARRISON: event=returned house=%d holder=%d zone=%u health=%d cycle=%u",
            original->getHouseID(), holder, objectID, getHealth().lround(), currentGame->getGameCycleCount());
    if(AITelemetry::log().enabled()) {
        const Coord pos = getLocation();
        AITelemetry::log().write(currentGame->getGameCycleCount(), original->getHouseID(), -1,
            "zone_garrison_returned", AITelemetry::Record().set("zone", objectID).set("item", itemID)
                .set("holder", holder).set("health", getHealth().lround()).set("x", pos.x).set("y", pos.y));
    }
}

void ZoneStructure::transferOwnership(House* newOwner, bool capture) {
    if(newOwner == nullptr || newOwner == owner || owner == nullptr) return;
    if(capture && currentGame->isCitySimEnabled() && !garrisonsEnabled()) return;
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
    const FixPoint before = getHealth();
    StructureBase::handleDamage(damage, damagerID, damagerOwner, provenance);
    const FixPoint lost = before - getHealth();
    if(lost <= 0 || garrison_.empty()) return;
    // Each occupant shares only the HP the building actually lost. The normal
    // integer damage path retains kill attribution, rewards and unit destruction.
    for(size_t i = 0; i < garrison_.size(); ++i) {
        if(UnitBase* unit = occupantUnit(garrison_[i]); unit && unit->getHealth() > 0) {
            const int share = DuneCity::ZoneGarrison::occupantDamage(lost, garrisonHealthRemainders_[i].damage);
            if(share > 0) unit->ObjectBase::handleDamage(share, damagerID, damagerOwner, provenance);
        }
    }
    dropDeadOccupants();
}

void ZoneStructure::dropDeadOccupants() {
    if(garrison_.empty()) return;
    size_t live = 0;
    int killed = 0;
    for(size_t i = 0; i < garrison_.size(); ++i) {
        const Uint32 id = garrison_[i];
        const UnitBase* unit = occupantUnit(id);
        if(unit && unit->getHealth() > 0) {
            garrison_[live] = id;
            garrisonHealthRemainders_[live++] = garrisonHealthRemainders_[i];
        } else {
            ++killed;
            SDL_Log("ZONE_GARRISON: event=casualty house=%d unit=%u item=%d zone=%u health=%d max_health=%d missing=%d cycle=%u",
                    owner ? owner->getHouseID() : -1, id, unit ? unit->getItemID() : -1, objectID,
                    getHealth().lround(), getMaxHealth(), unit ? 0 : 1, currentGame->getGameCycleCount());
        }
    }
    garrison_.resize(live);
    std::fill(garrisonHealthRemainders_.begin() + live, garrisonHealthRemainders_.end(), GarrisonHealthRemainders{});
    if(killed > 0 && owner == pLocalHouse)
        currentGame->addUrgentMessageToNewsTicker(_("Garrison casualties") + ": " + std::to_string(killed));
    if(garrison_.empty()) {
        garrisonTargetID_ = NONE_ID;
        SDL_Log("ZONE_GARRISON: event=killed_out house=%d zone=%u health=%d max_health=%d shots=%d air_shots=%d cycle=%u",
                owner != nullptr ? owner->getHouseID() : -1, objectID, getHealth().lround(), getMaxHealth(),
                garrisonShots_, garrisonAirShots_, currentGame->getGameCycleCount());
        garrisonShots_ = garrisonAirShots_ = 0;
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
    // A zone at 0 waits for destroy(), which throws its soldiers out of the ruin hurt.
    if(getHealth() <= 0) return;
    if(tooDamagedToHold()) {
        evacuateGarrison();
        if(garrison_.empty()) return;
    }

    bool anyReady = false;
    int range = 0;
    const uint64_t healCycles = static_cast<uint64_t>(currentGame->objectData.garrisonHealTime) * 1000 / GAMESPEED_DEFAULT;
    for(size_t i = 0; i < garrison_.size(); ++i) {
        UnitBase* unit = occupantUnit(garrison_[i]);
        if(unit->getHealth() < unit->getMaxHealth()) {
            const FixPoint step = DuneCity::ZoneGarrison::healingStep(
                unit->getMaxHealth(), healCycles, garrisonHealthRemainders_[i].healing);
            unit->setHealth(std::min(FixPoint(unit->getMaxHealth()), unit->getHealth() + step));
        }
        if(unit->getHealth() >= unit->getMaxHealth()) garrisonHealthRemainders_[i].healing = 0;
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
        if(unit->garrisonWeaponReady() && unit->canAttack(target) && distance <= unit->getWeaponRange()
           && unit->fireFromGarrison(target, muzzle, fromTile) && (++garrisonShots_, target->isAFlyingUnit())) {
            ++garrisonAirShots_;
            SDL_Log("ZONE_GARRISON: event=fired_air house=%d unit=%u item=%d zone=%u target=%u target_item=%d cycle=%u",
                    unit->getOwner()->getHouseID(), unit->getObjectID(), unit->getItemID(), objectID,
                    target->getObjectID(), target->getItemID(), currentGame->getGameCycleCount());
        }
    }
}

void ZoneStructure::drawGarrisonSandbags() const {
    // Original pixel art, drawn rather than loaded: a ring of stacked sandbags hugging the lot's
    // edge, two staggered courses on every side with a gap in the middle of each side as a firing
    // slit and a helmet per soldier. Every surface uses the occupying house's
    // remapped palette ramp, as unit and building owner colours do.
    const int x0 = screenborder->world2screenX(lround(realX));
    const int y0 = screenborder->world2screenY(lround(realY));
    const int w = screenborder->world2screenX(lround(realX) + structureSize.x * TILESIZE) - x0;
    const int h = screenborder->world2screenY(lround(realY) + structureSize.y * TILESIZE) - y0;
    const int bagL = std::max(5, w / 7);        // a bag's long side
    const int bagS = std::max(3, h / 10);       // its short side
    const House* house = getGarrisonHouse();
    const int houseColor = house ? house->getHouseID() : owner->getHouseID();
    const int colorSlot = getHouseVisualHouse(houseColor);
    const SDL_Color highlight = getHouseColorSDL(colorSlot, 0);
    const SDL_Color body = getHouseColorSDL(colorSlot, 3);
    const SDL_Color tie = getHouseColorSDL(colorSlot, 5);
    const SDL_Color outline = getHouseColorSDL(colorSlot, 7);

    auto fill = [](int x, int y, int bw, int bh, Uint8 r, Uint8 g, Uint8 b) {
        SDL_Rect rect{x, y, std::max(1, bw), std::max(1, bh)};
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderFillRect(renderer, &rect);
    };
    // One bag: dark outline, sacking body, sunlit face on its top or left side, a tie in the middle.
    auto bag = [&](int x, int y, int bw, int bh, bool horizontal) {
        fill(x, y, bw, bh, outline.r, outline.g, outline.b);
        fill(x + 1, y + 1, bw - 2, bh - 2, body.r, body.g, body.b);
        if(horizontal) {
            fill(x + 1, y + 1, bw - 2, std::max(1, (bh - 2) / 3), highlight.r, highlight.g, highlight.b);
            fill(x + bw / 2, y + 1, 1, bh - 2, tie.r, tie.g, tie.b);
        } else {
            fill(x + 1, y + 1, std::max(1, (bw - 2) / 3), bh - 2, highlight.r, highlight.g, highlight.b);
            fill(x + 1, y + bh / 2, bw - 2, 1, tie.r, tie.g, tie.b);
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
    fill(x0 + bagL, y0 + h - 2 * bagS, w - 2 * bagL, std::max(2, bagS / 2), body.r, body.g, body.b);

    // A house-coloured helmet per soldier inside, along the top wall.
    const int helmet = std::max(1, std::min(bagS + 1, (w - 4) / DuneCity::ZoneGarrison::kCapacity - 2));
    const int firstHelmet = x0 + (w - getGarrisonSize() * (helmet + 2) + 2) / 2;
    for(int i = 0; i < getGarrisonSize(); ++i) {
        const int hx = firstHelmet + i * (helmet + 2);
        const int hy = y0 + 2 * bagS;
        fill(hx, hy, helmet, helmet, outline.r, outline.g, outline.b);
        fill(hx + 1, hy + 1, helmet - 2, helmet - 2, body.r, body.g, body.b);
        fill(hx + 1, hy + 1, std::max(1, (helmet - 2) / 2), 1, highlight.r, highlight.g, highlight.b);
        fill(hx, hy + helmet - 1, helmet, 1, tie.r, tie.g, tie.b);
    }
}
