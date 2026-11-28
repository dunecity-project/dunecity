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
    return DuneCity::ZoneGarrison::kEvacuateHealthPercent;
}

bool ZoneStructure::tooDamagedToHold() const {
    return evacuateHealthPercent() > 0
        && getHealth() * 100 < FixPoint(getMaxHealth()) * evacuateHealthPercent();
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
    if(unit->getGarrisonRelocationTarget() == objectID || unit->getOwner() == owner)
        return mayReceiveGarrisonRelocation(unit);
    if(!hasGarrisonSpaceFor(unit)) return false;
    const House* garrisonHouse = getGarrisonHouse();
    if(isGarrisoned() && garrisonHouse != unit->getOwner()) return false;
    const auto entry = DuneCity::ZoneGarrison::entryFor(
        unit->getOwner()->getHouseID(), unit->getOwner()->getTeamID(),
        owner->getHouseID(), owner->getTeamID(), getGarrisonSize(),
        garrisonHouse ? garrisonHouse->getHouseID() : owner->getHouseID());
    return entry == DuneCity::ZoneGarrison::Entry::Enter
        || entry == DuneCity::ZoneGarrison::Entry::EnterAndTake;
}


bool ZoneStructure::hasGarrisonSpaceFor(const UnitBase* unit) const {
    int occupied = getGarrisonSize();
    if(occupied >= DuneCity::ZoneGarrison::kCapacity) return false;
    for(const UnitBase* incoming : unitList) {
        if(incoming != unit && incoming->isActive() && incoming->getHealth() > 0
           && incoming->getGarrisonRelocationTarget() == objectID
           && incoming->getOwner()->getTeamID() == owner->getTeamID()) {
            if(++occupied >= DuneCity::ZoneGarrison::kCapacity) return false;
        }
    }
    return true;
}

bool ZoneStructure::mayReceiveGarrisonRelocation(const UnitBase* unit) const {
    if(unit == nullptr || owner == nullptr || !isActive() || getHealth() <= 0
       || getLocation().isInvalid() || tooDamagedToHold()
       || owner->getTeamID() != unit->getOwner()->getTeamID()
       || !hasGarrisonSpaceFor(unit)) return false;
    for(const Uint32 id : garrison_) {
        const UnitBase* occupant = occupantUnit(id);
        if(occupant && occupant->getHealth() > 0
           && occupant->getOwner()->getTeamID() != unit->getOwner()->getTeamID()) return false;
    }
    return true;
}

void ZoneStructure::relocateGarrison() {
    const int holderHouse = owner->getHouseID();
    const int64_t healthRaw = getHealth().getRawValue();
    const int maxHealth = getMaxHealth();
    const int occupants = getGarrisonSize();
    int moving = 0;
    const int width = currentGameMap->getSizeX();
    const size_t tileCount = static_cast<size_t>(width) * currentGameMap->getSizeY();
    std::vector<int> exitOrigins;
    std::vector<Coord> frontier;
    static const std::array<Coord, 8> neighbours = {{
        Coord(-1, -1), Coord(0, -1), Coord(1, -1), Coord(-1, 0),
        Coord(1, 0), Coord(-1, 1), Coord(0, 1), Coord(1, 1)
    }};

    for(size_t i = 0; i < garrison_.size();) {
        UnitBase* unit = occupantUnit(garrison_[i]);
        bool hasCandidate = false;
        for(StructureBase* structure : structureList) {
            const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
            if(zone && zone != this && zone->mayReceiveGarrisonRelocation(unit)) {
                hasCandidate = true;
                break;
            }
        }
        if(!hasCandidate) {
            ++i;
            continue;
        }

        // A multi-source flood follows the same eight-neighbour canPathThrough policy as A*.
        // Retain the source exit for each reachable tile: deploying on a random other side
        // of the building could strand an otherwise reachable relocation.
        exitOrigins.assign(tileCount, -1);
        frontier.clear();
        if(frontier.capacity() < tileCount) frontier.reserve(tileCount);
        for(int y = location.y - 1; y <= location.y + structureSize.y; ++y) {
            for(int x = location.x - 1; x <= location.x + structureSize.x; ++x) {
                if(x >= location.x && x < location.x + structureSize.x
                   && y >= location.y && y < location.y + structureSize.y) continue;
                if(!unit->canPathThrough(x, y)) continue;
                const int index = y * width + x;
                exitOrigins[index] = index;
                frontier.emplace_back(x, y);
            }
        }
        for(size_t head = 0; head < frontier.size(); ++head) {
            const Coord at = frontier[head];
            for(const Coord& offset : neighbours) {
                const Coord next = at + offset;
                if(!currentGameMap->tileExists(next)) continue;
                const int index = next.y * width + next.x;
                if(exitOrigins[index] >= 0 || !unit->canPathThrough(next.x, next.y)) continue;
                exitOrigins[index] = exitOrigins[at.y * width + at.x];
                frontier.push_back(next);
            }
        }

        ZoneStructure* best = nullptr;
        FixPoint bestDistance = FixPt_MAX;
        int bestExit = -1;
        for(StructureBase* structure : structureList) {
            auto* zone = dynamic_cast<ZoneStructure*>(structure);
            if(!zone || zone == this || !zone->mayReceiveGarrisonRelocation(unit)) continue;
            const Coord targetAt = zone->getLocation();
            const FixPoint distance = blockDistance(getClosestPoint(targetAt), zone->getClosestPoint(location));
            if(distance > bestDistance || (distance == bestDistance && best
               && zone->getObjectID() > best->getObjectID())) continue;
            int exit = -1;
            for(int y = targetAt.y - 1; y <= targetAt.y + zone->structureSize.y && exit < 0; ++y) {
                for(int x = targetAt.x - 1; x <= targetAt.x + zone->structureSize.x; ++x) {
                    const Coord approach(x, y);
                    if(!currentGameMap->tileExists(approach)) continue;
                    if(x >= targetAt.x && x < targetAt.x + zone->structureSize.x
                       && y >= targetAt.y && y < targetAt.y + zone->structureSize.y) continue;
                    exit = exitOrigins[y * width + x];
                    if(exit >= 0) break;
                }
            }
            if(exit < 0) continue;
            best = zone;
            bestDistance = distance;
            bestExit = exit;
        }
        if(best == nullptr) {
            ++i;
            continue;
        }

        // Remove this occupant before deployment hazards can run. Unmoved occupants retain
        // their healing fraction and remain hidden; every living departure books its own room.
        for(size_t j = i + 1; j < garrison_.size(); ++j)
            garrisonHealthRemainders_[j - 1] = garrisonHealthRemainders_[j];
        garrison_.erase(garrison_.begin() + i);
        garrisonHealthRemainders_[garrison_.size()] = {};
        garrisonTargetID_ = NONE_ID;
        const Coord spot(bestExit % width, bestExit / width);
        unit->setGuardPoint(spot);
        unit->setForced(false);
        unit->deploy(spot);
        if(unit->isActive() && unit->getHealth() > 0) {
            unit->doGarrisonRelocation(best);
            if(unit->getGarrisonRelocationTarget() == best->getObjectID()) {
                ++moving;
                SDL_Log("ZONE_GARRISON: event=relocation_departure house=%d holder_house=%d unit=%u source=%u destination=%u cycle=%u",
                        unit->getOwner()->getHouseID(), holderHouse, unit->getObjectID(), objectID,
                        best->getObjectID(), currentGame->getGameCycleCount());
            }
        }
    }
    SDL_Log("ZONE_GARRISON: event=relocation_attempt house=%d source=%u health_raw=%lld max_health=%d occupants=%d moving=%d stayed=%d cycle=%u",
            holderHouse, objectID, static_cast<long long>(healthRaw), maxHealth, occupants, moving,
            getGarrisonSize(), currentGame->getGameCycleCount());
    if(garrison_.empty()) {
        garrisonShots_ = garrisonAirShots_ = 0;
        if(getHealth() > 0) returnCapturedZone();
    }
}

bool ZoneStructure::heldAgainst(const UnitBase* unit) const {
    if(unit == nullptr) return false;
    const House* actor = unit->getOwner();
    for(const Uint32 id : garrison_) {
        const UnitBase* occupant = occupantUnit(id);
        if(occupant == nullptr) continue;
        const House* crew = occupant->getOwner();
        // An owner's reinforcement of its friendly allied crew is not an ownership capture.
        if(crew != actor && (owner != actor || crew->getTeamID() != actor->getTeamID())) return true;
    }
    return false;
}

bool ZoneStructure::enterGarrison(UnitBase* unit) {
    if(!mayEnterGarrison(unit)) return false;
    // Automatic friendly relocation shares shelter, never ownership. Manual ally entry
    // still follows entryFor() and is refused.
    House* house = unit->getOwner();
    const bool taken = house->getTeamID() != owner->getTeamID();
    const int previousHouse = owner->getHouseID();
    const bool relocating = unit->getGarrisonRelocationTarget() == objectID;
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
    if(relocating) {
        SDL_Log("ZONE_GARRISON: event=relocation_arrival house=%d unit=%u destination=%u occupants=%d cycle=%u",
                house->getHouseID(), unit->getObjectID(), objectID, getGarrisonSize(),
                currentGame->getGameCycleCount());
    }
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

int ZoneStructure::evacuateGarrison(bool destroying, int keep) {
    if(garrison_.empty()) return 0;
    int deployed = 0;
    int hurt = 0;
    int killed = 0;
    int localKilled = 0;
    const bool lowHealth = !destroying && tooDamagedToHold();
    const int houseID = getGarrisonHouseID(garrison_);
    const size_t count = garrison_.size();
    std::array<Uint32, DuneCity::ZoneGarrison::kCapacity> occupants;
    int initialLive = 0;
    for(size_t i = 0; i < count; ++i) {
        occupants[i] = garrison_[i];
        const UnitBase* unit = occupantUnit(occupants[i]);
        if(unit && unit->getHealth() > 0) ++initialLive;
    }
    std::array<bool, DuneCity::ZoneGarrison::kCapacity> retained{};
    if(!destroying && keep > 0) {
        // A Soldier courier relieves rear Troopers without dropping captured ownership.
        for(const bool preferSoldier : {true, false}) {
            for(size_t i = 0; i < count && keep > 0; ++i) {
                const UnitBase* unit = occupantUnit(occupants[i]);
                if(unit == nullptr || unit->getHealth() <= 0
                   || (unit->getItemID() == Unit_Soldier) != preferSoldier) continue;
                retained[i] = true;
                --keep;
            }
        }
    }
    garrison_.clear();
    garrisonTargetID_ = NONE_ID;
    // A collapsed footprint is available even when every surrounding tile is blocked.
    // Free it before deployment so an ejected casualty follows the ordinary on-map death path.
    if(destroying && currentGameMap != nullptr && getLocation().isValid()) {
        for(int y = 0; y < structureSize.y; ++y)
            for(int x = 0; x < structureSize.x; ++x)
                if(Tile* tile = currentGameMap->getTile(location.x + x, location.y + y))
                    tile->unassignObject(objectID);
    }
    for(size_t i = 0; i < count; ++i) {
        const Uint32 id = occupants[i];
        UnitBase* unit = occupantUnit(id);
        if(unit == nullptr) continue;
        if(retained[i]) {
            garrisonHealthRemainders_[garrison_.size()] = garrisonHealthRemainders_[i];
            garrison_.push_back(id);
            continue;
        }
        Coord spot = Coord::Invalid();
        if(currentGameMap != nullptr && getLocation().isValid()) {
            spot = currentGameMap->findDeploySpot(unit, getLocation(), currentGame->randomGen,
                                                  Coord::Invalid(), structureSize);
            if(destroying && spot.isInvalid() && currentGameMap->tileExists(getLocation()))
                spot = getLocation();
        }
        if(spot.isValid() && currentGameMap->tileExists(spot)) {
            unit->setGuardPoint(spot);
            unit->setForced(false);
            unit->deploy(spot);
            const bool hazard = unit->getHealth() <= 0;
            if(destroying && !hazard) {
                const FixPoint before = unit->getHealth();
                const int damage = static_cast<int>((int64_t{unit->getMaxHealth()}
                    * currentGame->objectData.garrisonCollapseDamagePercent + 99) / 100);
                unit->handleDamage(damage, NONE_ID, nullptr);
                hurt += (before - unit->getHealth()).lround();
            }
            if(unit->getHealth() > 0) {
                ++deployed;
            } else {
                // deploy() hides bloom victims. Reveal the corpse, never restore its health,
                // so the unit's normal destruction creates the visible falling-infantry animation.
                unit->setVisible(VIS_ALL, true);
                ++killed;
                if(unit->getOwner() == pLocalHouse) ++localKilled;
                SDL_Log("ZONE_GARRISON: event=casualty house=%d zone=%u unit=%u cause=%s cycle=%u",
                        unit->getOwner()->getHouseID(), objectID, id,
                        hazard ? "ejection_hazard" : "collapse", currentGame->getGameCycleCount());
            }
        } else {
            garrisonHealthRemainders_[garrison_.size()] = garrisonHealthRemainders_[i];
            garrison_.push_back(id);
        }
    }
    std::fill(garrisonHealthRemainders_.begin() + garrison_.size(), garrisonHealthRemainders_.end(),
              GarrisonHealthRemainders{});
    if(localKilled > 0)
        currentGame->addUrgentMessageToNewsTicker(_("Garrison casualties") + ": " + std::to_string(localKilled));
    if(owner != nullptr) {
        SDL_Log("ZONE_GARRISON: event=exit house=%d zone=%u occupants=%d deployed=%d remaining=%d destroyed=%d low_health=%d hurt=%d killed=%d health=%d max_health=%d shots=%d air_shots=%d cycle=%u",
                houseID, objectID, initialLive, deployed, getGarrisonSize(), destroying ? 1 : 0, lowHealth ? 1 : 0,
                hurt, killed, getHealth().lround(), getMaxHealth(), garrisonShots_, garrisonAirShots_, currentGame->getGameCycleCount());
    }
    if(garrison_.empty()) garrisonShots_ = garrisonAirShots_ = 0;
    if(AITelemetry::log().enabled() && owner != nullptr) {
        AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "zone_garrison_exit",
            AITelemetry::Record().set("zone", objectID).set("occupants", initialLive).set("deployed", deployed)
                .set("remaining", getGarrisonSize()).set("destroyed", destroying).set("killed", killed)
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
    if(capture) {
        if(currentGame->isCitySimEnabled() && !garrisonsEnabled()) return;
        for(const Uint32 id : garrison_)
            if(const UnitBase* occupant = occupantUnit(id); occupant && occupant->getOwner() != newOwner) return;
    }
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

void ZoneStructure::dropDeadOccupants() {
    if(garrison_.empty()) return;
    size_t live = 0;
    int localKilled = 0;
    for(size_t i = 0; i < garrison_.size(); ++i) {
        const Uint32 id = garrison_[i];
        UnitBase* unit = occupantUnit(id);
        if(unit && unit->getHealth() > 0) {
            garrison_[live] = id;
            garrisonHealthRemainders_[live++] = garrisonHealthRemainders_[i];
        } else {
            if(unit != nullptr) {
                if(unit->getOwner() == pLocalHouse) ++localKilled;
                if(currentGameMap != nullptr && getLocation().isValid()) {
                    const Coord spot = currentGameMap->findDeploySpot(
                        unit, getLocation(), currentGame->randomGen, Coord::Invalid(), structureSize);
                    if(spot.isValid() && currentGameMap->tileExists(spot)) {
                        unit->setGuardPoint(spot);
                        unit->deploy(spot);
                        unit->setVisible(VIS_ALL, true);
                    }
                }
            }
            SDL_Log("ZONE_GARRISON: event=casualty house=%d unit=%u item=%d zone=%u health=%d max_health=%d missing=%d cycle=%u",
                    unit ? unit->getOwner()->getHouseID() : -1, id, unit ? unit->getItemID() : -1, objectID,
                    getHealth().lround(), getMaxHealth(), unit ? 0 : 1, currentGame->getGameCycleCount());
        }
    }
    garrison_.resize(live);
    std::fill(garrisonHealthRemainders_.begin() + live, garrisonHealthRemainders_.end(), GarrisonHealthRemainders{});
    if(localKilled > 0)
        currentGame->addUrgentMessageToNewsTicker(_("Garrison casualties") + ": " + std::to_string(localKilled));
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
    const bool belowThreshold = tooDamagedToHold();
    if(!belowThreshold) garrisonRelocationAttempted_ = false;
    if(garrison_.empty() || owner == nullptr || currentGameMap == nullptr || getLocation().isInvalid()) return;
    // Destruction owns zero-health ejection. A failed relocation is not retried until recovery.
    if(getHealth() <= 0) return;
    if(belowThreshold && !garrisonRelocationAttempted_) {
        garrisonRelocationAttempted_ = true;
        relocateGarrison();
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
    Coord muzzle(fromTile.x * TILESIZE + TILESIZE / 2, fromTile.y * TILESIZE + TILESIZE / 2);
    // A gun moves only 20 pixels before testing structure collisions, less
    // than half a tile: launching at the tile centre hits our own cover.
    // Start just beyond its edge along the firing ray, including diagonals.
    const Coord aim = target->getClosestCenterPoint(fromTile);
    const int dx = aim.x - muzzle.x, dy = aim.y - muzzle.y;
    const int extent = std::max(std::abs(dx), std::abs(dy));
    if(extent > 0) {
        const int offset = std::min(TILESIZE / 2 + 1, extent);
        muzzle.x += dx * offset / extent;
        muzzle.y += dy * offset / extent;
    }
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
