#include <players/AIDecisionLog.h>
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

#include <ObjectBase.h>
#include <players/QuantBot.h>
#include <DynastyProjectile.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <Game.h>
#include <SpatialGrid.h>
#include <House.h>
#include <SoundPlayer.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <players/HumanPlayer.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

#include <SpecialVehicle.h>
#include <mod/ModManager.h>

//structures
#include <structures/Barracks.h>
#include <structures/ConstructionYard.h>
#include <structures/GunTurret.h>
#include <structures/HeavyFactory.h>
#include <structures/HighTechFactory.h>
#include <structures/IX.h>
#include <structures/LightFactory.h>
#include <structures/Palace.h>
#include <structures/Radar.h>
#include <structures/Refinery.h>
#include <structures/RepairYard.h>
#include <structures/RocketTurret.h>
#include <structures/Silo.h>
#include <structures/StarPort.h>
#include <structures/Wall.h>
#include <structures/WindTrap.h>
#include <structures/AdvancedWindTrap.h>
#include <structures/Worfinery.h>
#include <structures/TechCenter.h>
#include <structures/Scoutpost.h>
#include <structures/LoveFactory.h>
#include <structures/ChaosFactory.h>
#include <structures/NuclearPlant.h>
#include <structures/PoliceStation.h>
#include <structures/Stadium.h>
#include <structures/Airport.h>
#include <structures/WOR.h>
#include <structures/ZoneStructure.h>

//units
#include <units/Carryall.h>
#include <units/ChemicalCarryall.h>
#include <units/Devastator.h>
#include <units/Deviator.h>
#include <units/Frigate.h>
#include <units/Harvester.h>
#include <units/RebelHarvester.h>
#include <units/Launcher.h>
#include <units/MCV.h>
#include <units/Ornithopter.h>
#include <units/Quad.h>
#include <units/RaiderTrike.h>
#include <units/Saboteur.h>
#include <units/SandWorm.h>
#include <units/SiegeTank.h>
#include <units/Soldier.h>
#include <units/SonicTank.h>
#include <units/Tank.h>
#include <units/Trike.h>
#include <units/Trooper.h>
#include <units/AmbientAirplane.h>
#include <units/AmbientHelicopter.h>
#include <units/RocketTrike.h>
#include <units/SonicTrike.h>
#include <units/FlameTank.h>
#include <units/EliteLauncher.h>
#include <units/EliteSiegeTank.h>
#include <units/ChemicalSiegeTank.h>

#include <array>
#include <vector>

ObjectBase::ObjectBase(House* newOwner) : originalHouseID(newOwner->getHouseID()), owner(newOwner) {
    ObjectBase::init();

    objectID = NONE_ID;

    health = 0;
    badlyDamaged = false;

    location = Coord::Invalid();
    oldLocation = Coord::Invalid();
    destination = Coord::Invalid();
    realX = 0;
    realY = 0;

    drawnAngle = 0;
    angle = drawnAngle;

    active = false;
    respondable = true;
    byScenario = false;
    selected = false;
    selectedByOtherPlayer = false;

    forced = false;
    setTarget(nullptr);
    targetFriendly = false;
    attackMode = GUARD;

    setVisible(VIS_ALL, false);

    gridHandle.invalidate();
}

ObjectBase::ObjectBase(InputStream& stream) {
    objectID = NONE_ID; // has to be set after loading
    originalHouseID = stream.readUint32();
    owner = currentGame->getHouse(stream.readUint32());

    ObjectBase::init();

    health = stream.readFixPoint();
    badlyDamaged = stream.readBool();

    location.x = stream.readSint32();
    location.y = stream.readSint32();
    oldLocation.x = stream.readSint32();
    oldLocation.y = stream.readSint32();
    destination.x = stream.readSint32();
    destination.y = stream.readSint32();
    realX = stream.readFixPoint();
    realY = stream.readFixPoint();

    angle = stream.readFixPoint();
    drawnAngle = stream.readSint8();

    active = stream.readBool();
    respondable = stream.readBool();
    byScenario = stream.readBool();

    if(!isNetworkGameType(currentGame->getGameInitSettings().getGameType())) {
        selected = stream.readBool();
        selectedByOtherPlayer = stream.readBool();
    } else {
        selected = false;
        selectedByOtherPlayer = false;
    }

    forced = stream.readBool();
    target.load(stream);
    targetFriendly = stream.readBool();
    attackMode = static_cast<ATTACKMODE>(stream.readUint32());

    // The packed save field contains 8 bits. Keep the ninth team-array slot
    // initialized instead of indexing past the old seven-element temporary.
    std::array<bool, NUM_TEAM_SLOTS> b{};

    stream.readBools(&b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
    if(currentGame && currentGame->getLoadedSavegameVersion() >= 9821) {
        stream.readBools(&b[8], &b[9]);
    }

    for (decltype(visible.size()) i = 0; i < visible.size(); ++i)
        visible[i] = b[i];

    gridHandle.invalidate();
}

void ObjectBase::init() {
    itemID = ItemID_Invalid;

    aFlyingUnit = false;
    aGroundUnit = false;
    aStructure = false;
    aUnit = false;
    infantry = false;
    aBuilder = false;

    canAttackStuff = false;

    radius = TILESIZE/2;

    graphicID = -1;
    numImagesX = 0;
    numImagesY = 0;

}

ObjectBase::~ObjectBase() {
    if(gridHandle.owner != nullptr) {
        gridHandle.owner->unregister(gridHandle);
    }
}

void ObjectBase::save(OutputStream& stream) const {
    stream.writeUint32(originalHouseID);
    stream.writeUint32(owner->getHouseID());

    stream.writeFixPoint(health);
    stream.writeBool(badlyDamaged);

    stream.writeSint32(location.x);
    stream.writeSint32(location.y);
    stream.writeSint32(oldLocation.x);
    stream.writeSint32(oldLocation.y);
    stream.writeSint32(destination.x);
    stream.writeSint32(destination.y);
    stream.writeFixPoint(realX);
    stream.writeFixPoint(realY);

    stream.writeFixPoint(angle);
    stream.writeSint8(drawnAngle);

    stream.writeBool(active);
    stream.writeBool(respondable);
    stream.writeBool(byScenario);

    if(!isNetworkGameType(currentGame->getGameInitSettings().getGameType())) {
        stream.writeBool(selected);
        stream.writeBool(selectedByOtherPlayer);
    }

    stream.writeBool(forced);
    target.save(stream);
    stream.writeBool(targetFriendly);
    stream.writeUint32(attackMode);

    stream.writeBools(visible[0], visible[1], visible[2], visible[3], visible[4], visible[5], visible[6], visible[7]);
    stream.writeBools(visible[8], visible[9]);
}


/**
    Returns the center point of this object
    \return the center point in world coordinates
*/
Coord ObjectBase::getCenterPoint() const {
    return Coord(lround(realX), lround(realY));
}

Coord ObjectBase::getClosestCenterPoint(const Coord& objectLocation) const {
    return getCenterPoint();
}


int ObjectBase::getMaxHealth() const {
    return currentGame->objectData.data[itemID][originalHouseID].hitpoints;
}

void ObjectBase::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                              const DeviationReward::Provenance& provenance) {
    // Multi-impact missiles must not award repeated kills for the same dead object.
    if (damage >= 0 && getHealth() <= 0) return;
    // Immortality guard: Human-controlled houses are invulnerable in single-player modes when option is enabled
    // This applies when ANY human player controls the house (not just AI players)
    if(damage > 0) {
        // Game::localHumanImmortalityApplies() carries the option, the game-type restriction
        // and - new - the requirement that pLocalHouse really is played by a registered human.
        // While observing, pLocalHouse is a view anchor onto an AI faction, and making that
        // faction invulnerable would change the match being watched.
        if(currentGame->localHumanImmortalityApplies() && getOwner() == pLocalHouse) {
            // Zero damage so human-controlled units/structures are invulnerable
            // Continue function execution so visibility/music/stats hooks still run
            damage = 0;
        }
    }
    
    const FixPoint healthBefore = getHealth();
    if(damage >= 0) {
        FixPoint newHealth = getHealth();

        newHealth -= damage;

        if(newHealth <= 0) {
            if (getHealth() > 0 && AITelemetry::log().enabled())
                AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "lethal_damage",
                    AITelemetry::Record().set("object", objectID).set("item", itemID)
                        .set("attacker", damagerID).set("attacker_house", damagerOwner ? damagerOwner->getHouseID() : -1)
                        .set("damage", damage).set("x", location.x).set("y", location.y));
            setHealth(0);

            if(damagerOwner != nullptr) {
                damagerOwner->informHasKilled(itemID);
            }
        } else {
            setHealth(newHealth);
        }
    }

    if(getOwner() == pLocalHouse) {
        musicPlayer->changeMusic(MUSIC_ATTACK);
    }

    if (damagerOwner != nullptr && damage != 0) {
        ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
        // Attribution belongs to the moment the shot left the weapon. A shooter that died,
        // reverted to its original house or was recaptured in the meantime no longer decides
        // which learning bucket its shot pays.
        const auto credit = provenance.known() || !provenance.allowsLiveLookup()
            ? provenance : DeviationReward::liveProvenance(pDamager, damagerOwner);
        // The reward goes to the house that owned the shot when it was fired, which is not
        // always the house the engine damages on behalf of: an armed Devastator that reverts
        // mid-fuse explodes under its original owner but still pays its captor. Simulation
        // effects below stay with damagerOwner; only the ledger follows the snapshot.
        House* beneficiary = DeviationReward::beneficiaryOf(credit, damagerOwner);
        if (credit.known() && beneficiary != nullptr) {
            const auto reward = CombatReward::hit(
                currentGame->objectData.data[itemID][originalHouseID].price,
                int64_t(getMaxHealth())*1000, (healthBefore*1000).lround(), (getHealth()*1000).lround(),
                damage > 0 && damagerID != objectID && beneficiary->getTeamID() != getOwner()->getTeamID(), isAUnit());
            // A borrowed unit's damage pays its controller's Deviator and nothing else: the
            // natural type must not be credited a second time for the same hit.
            beneficiary->addCombatReward(credit.rewardItemID, reward);
            DeviationReward::recordOutgoing(credit, *beneficiary, reward, this);
            // Recovery decisions need actual hostile fatalities, irrespective
            // of whether telemetry is enabled. Legacy kill counters also count
            // friendly fire, whereas hit() excludes it and repeated dead hits.
            if (reward.kills > 0)
                for (const auto& player : beneficiary->getPlayerList())
                    player->onHostileUnitKilled(itemID, originalHouseID);
            if (AITelemetry::log().enabled() && reward.hits > 0)
                for (const auto& player : beneficiary->getPlayerList())
                    player->onCombatReward(damagerID, objectID, reward);
            int appliedDamage = damage;
            if (beneficiary == getOwner()) {
                appliedDamage *= -1;
            }
            beneficiary->informHasDamaged(credit.rewardItemID, appliedDamage);
        }
    }

    // Hit points a borrowed unit soaked up for the house controlling it. The control
    // interval is still the one that was current when this hit was dispatched: a timer
    // exhausted by this very hit only reverts ownership on the next update.
    DeviationReward::recordAbsorbed(this, (healthBefore*1000).lround(), damage, damagerOwner, damagerID);

    getOwner()->noteDamageLocation(this, damage, damagerID);
}

void ObjectBase::handleInterfaceEvent(SDL_Event* event) {
}

bool ObjectBase::mayShowOwnerInterface() const {
    return pLocalHouse == owner || debug || (currentGame != nullptr && currentGame->isObserving());
}

ObjectInterface* ObjectBase::getInterfaceContainer() {
    return DefaultObjectInterface::create(objectID);
}

void ObjectBase::removeFromSelectionLists() {
    currentGame->getSelectedList().erase(getObjectID());
    currentGame->selectionChanged();
    currentGame->getSelectedByOtherPlayerList().erase(getObjectID());
    selected = false;
}

void ObjectBase::setDestination(int newX, int newY) {
    if(currentGameMap->tileExists(newX, newY) || ((newX == INVALID_POS) && (newY == INVALID_POS))) {
        destination.x = newX;
        destination.y = newY;
    }
}

void ObjectBase::setHealth(FixPoint newHealth) {
    if((newHealth >= 0) && (newHealth <= getMaxHealth())) {
        health = newHealth;
        badlyDamaged = health < BADLYDAMAGEDRATIO * getMaxHealth();
    }
}

void ObjectBase::setLocation(int xPos, int yPos) {
    SpatialGrid* spatialGrid = (currentGame != nullptr) ? currentGame->getSpatialGrid() : nullptr;
    const Coord previousLocation = location;

    if((xPos == INVALID_POS) && (yPos == INVALID_POS)) {
        if(spatialGrid != nullptr && gridHandle.isValid()) {
            spatialGrid->unregister(gridHandle);
        }
        location.invalidate();
    } else if (currentGameMap->tileExists(xPos, yPos))  {
        location.x = xPos;
        location.y = yPos;
        realX = location.x*TILESIZE;
        realY = location.y*TILESIZE;

        assignToMap(location);

        if(spatialGrid != nullptr) {
            spatialGrid->move(*this, gridHandle, previousLocation, location);
        }
    }
}

void ObjectBase::setObjectID(int newObjectID) {
    if(newObjectID >= 0) {
        objectID = newObjectID;
    }
}

void ObjectBase::setVisible(int teamID, bool status) {
    if(teamID == VIS_ALL) {
        if (status)
            visible.set();
        else
            visible.reset();
    } else if ((teamID >= 0) && (teamID < NUM_TEAM_SLOTS)) {
        visible[teamID] = status;
    }
}

void ObjectBase::setTarget(const ObjectBase* newTarget) {
    target.pointTo(const_cast<ObjectBase*>(newTarget));

    // ObjectPointer::operator bool() only reports whether an ID is set. Resolve
    // it once because a target destroyed earlier in the cycle can be stale.
    const ObjectBase* pTarget = target.getObjPointer();
    targetFriendly = (pTarget != nullptr)
                     && (pTarget->getOwner()->getTeamID() == owner->getTeamID())
                     && (getItemID() != Unit_Sandworm)
                     && (pTarget->getItemID() != Unit_Sandworm);
}

void ObjectBase::unassignFromMap(const Coord& location) const {
    if(currentGameMap->tileExists(location)) {
        currentGameMap->getTile(location)->unassignObject(getObjectID());
    }
}


bool ObjectBase::canAttack(const ObjectBase* object) const {
    return canAttack()
        && (object != nullptr)
        && (object->isAStructure() || !object->isAFlyingUnit())
        && (((object->getOwner()->getTeamID() != owner->getTeamID()) && object->isVisible(getOwner()->getTeamID())) || (object->getItemID() == Unit_Sandworm));
}

bool ObjectBase::isOnScreen() const {
    const Coord position{ lround(getRealX()), lround(getRealY()) };
    const Coord size{ getWidth(graphic[currentZoomlevel]) / numImagesX, getHeight(graphic[currentZoomlevel]) / numImagesY };

    return screenborder->isInsideScreen(position,size);
}

bool ObjectBase::isVisible(int teamID) const {
    if((teamID >= 0) && (teamID < NUM_TEAM_SLOTS)) {
        return visible[teamID];
    } else {
        return false;
    }
}

bool ObjectBase::isVisible() const {
    return visible.any();
}

Uint32 ObjectBase::getHealthColor() const {
    const FixPoint healthPercent = health/getMaxHealth();

    if(healthPercent >= BADLYDAMAGEDRATIO) {
        return COLOR_LIGHTGREEN;
    } else if(healthPercent >= HEAVILYDAMAGEDRATIO) {
        return COLOR_YELLOW;
    } else {
        return COLOR_RED;
    }
}

ObjectBase::TargetSearchStats ObjectBase::targetSearchStats{};

namespace {

// Ordered ring walk. Every search below used to loop the whole (2r+1)^2 square
// and discard the interior with a max(|dx|,|dy|)!=r test, which made a pure
// perimeter traversal cubic in the search radius: a Hunt across a 256x256 map
// is 64 grid rings, so 349504 cell tests to inspect 16129 cells. This visits
// exactly the same cells in exactly the same x-then-y order in O(r) per ring,
// so candidate order - and therefore every tie - is unchanged.
template<typename Visit>
void forEachRingOffset(int ring, Visit visit) {
    if(ring <= 0) {
        visit(0, 0);
        return;
    }

    for(int dx = -ring; dx <= ring; ++dx) {
        if(dx == -ring || dx == ring) {
            // The two end columns are solid: every dy is on the perimeter.
            for(int dy = -ring; dy <= ring; ++dy) {
                visit(dx, dy);
            }
        } else {
            visit(dx, -ring);
            visit(dx, ring);
        }
    }
}

// Workers are never prey for an autonomous aircraft search. Enforced at the
// search, which is the autonomous source: an explicit attack order does not
// come through here, so manual control keeps targeting whatever it likes.
bool isAutonomousTargetVetoed(const ObjectBase& seeker, const ObjectBase& candidate) {
    if(seeker.getItemID() != Unit_Ornithopter) {
        return false;
    }

    const int item = candidate.getItemID();
    return item == Unit_Harvester || item == Unit_RebelHarvester;
}

// 0 = normal, 1 = walls (slightly deprioritized), 2 = carryalls (heavily deprioritized)
int getTargetDeprioritizationLevel(const ObjectBase& candidate) {
    if(isCarryallUnit(candidate.getItemID())) return 2;
    if(candidate.getItemID() == Structure_Wall) return 1;
    return 0;
}

bool isDeprioritizedTarget(const ObjectBase& candidate) {
    return getTargetDeprioritizationLevel(candidate) > 0;
}

bool isTileVisibleToSeeker(const ObjectBase& seeker, const Coord& tileCoord) {
    if(!currentGameMap->tileExists(tileCoord)) {
        return false;
    }

    const Tile* tile = currentGameMap->getTile(tileCoord);
    const int teamId = seeker.getOwner()->getTeamID();

    return tile->isExploredByTeam(teamId) && !tile->isFoggedByTeam(teamId);
}

const UnitBase* managedLauncherHunt(const ObjectBase& seeker) {
    if (seeker.getItemID()!=Unit_Launcher && seeker.getItemID()!=Unit_EliteLauncher) return nullptr;
    const auto* unit=dynamic_cast<const UnitBase*>(&seeker);
    if (!unit || unit->getAttackMode()!=HUNT || !seeker.getOwner()) return nullptr;
    for (const auto& player : seeker.getOwner()->getPlayerList())
        if (const auto* bot=dynamic_cast<const QuantBot*>(player.get()))
            if (bot->managesAutonomousLauncherHunt(unit)) return unit;
    return nullptr;
}

const ObjectBase* findClosestTargetLegacy(const ObjectBase& seeker) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    const auto* launcherHunt = managedLauncherHunt(seeker);
    const int maxRadiusX = std::max(seekerLocation.x, currentGameMap->getSizeX() - 1 - seekerLocation.x);
    const int maxRadiusY = std::max(seekerLocation.y, currentGameMap->getSizeY() - 1 - seekerLocation.y);
    const int maxRadius = std::max(maxRadiusX, maxRadiusY);

    const ObjectBase* bestTarget = nullptr;
    int bestDeprio = 3; // worse than carryall
    FixPoint bestDistance = FixPt_MAX;

    for(int radius = 1; radius <= maxRadius; ++radius) {
        forEachRingOffset(radius, [&](int dx, int dy) {
            const Coord checkCoord(seekerLocation.x + dx, seekerLocation.y + dy);
            ++ObjectBase::targetSearchStats.cellsVisited;
            if(!currentGameMap->tileExists(checkCoord)) {
                return;
            }

            Tile* tile = currentGameMap->getTile(checkCoord);
            if(!tile->hasAnObject()) {
                return;
            }

            ObjectBase* candidate = tile->getObject();
            // A remote aircraft is immediately released by the engine, which
            // resets Hunt's destination and cooldown. Let this AI launcher find
            // reachable ground prey; keep every aircraft it can actually shoot.
            if(launcherHunt && candidate && candidate->isAFlyingUnit()
                && !launcherHunt->isInWeaponRange(candidate)) return;
            if(candidate != nullptr && !isAutonomousTargetVetoed(seeker, *candidate)
                && seeker.canAttack(candidate)
                && currentGameMap->terrainAttackReachable(seeker,*candidate)) {
                ++ObjectBase::targetSearchStats.candidatesTested;
                const int deprio = getTargetDeprioritizationLevel(*candidate);
                const FixPoint dist = blockDistance(seekerLocation, checkCoord);

                // Prefer lower deprioritization; same tier -> prefer closer
                if(deprio < bestDeprio || (deprio == bestDeprio && dist < bestDistance)) {
                    bestTarget = candidate;
                    bestDeprio = deprio;
                    bestDistance = dist;
                }
            }
        });

        // Early exit: if we found a non-deprioritized target this ring, stop
        if(bestTarget != nullptr && bestDeprio == 0) {
            break;
        }
        // If we found a wall but no carryall-free target yet, keep searching one more ring
        // Give buildings a fair chance at being found before giving up
        if(bestTarget != nullptr && bestDeprio <= 1 && radius > 5) {
            break;
        }
        if(bestTarget != nullptr && bestDeprio == 2 && radius > 10) {
            break;
        }
    }

    return bestTarget;
}

const ObjectBase* findTargetLegacy(const ObjectBase& seeker, int checkRange) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    ObjectBase* closestTarget = nullptr;
    auto closestDistance = FixPt_MAX;

    for(int ring = 0; ring <= checkRange; ++ring) {
        forEachRingOffset(ring, [&](int dx, int dy) {
            const Coord checkCoord(seekerLocation.x + dx, seekerLocation.y + dy);
            ++ObjectBase::targetSearchStats.cellsVisited;
            if(!currentGameMap->tileExists(checkCoord)) {
                return;
            }

            const FixPoint distance = blockDistance(seekerLocation, checkCoord);
            if(distance > FixPoint(checkRange)) {
                return;
            }

            Tile* tile = currentGameMap->getTile(checkCoord);
            if(tile->hasAnObject() == false) {
                return;
            }

            if(!isTileVisibleToSeeker(seeker, checkCoord)) {
                return;
            }

            ObjectBase* candidate = tile->getObject();
            if(candidate == nullptr) {
                return;
            }

            if(isAutonomousTargetVetoed(seeker, *candidate)) {
                return;
            }

            if(isDeprioritizedTarget(*candidate) && closestTarget != nullptr) {
                return;
            }

            ++ObjectBase::targetSearchStats.candidatesTested;
            if(seeker.canAttack(candidate) && distance < closestDistance) {
                closestTarget = candidate;
                closestDistance = distance;
            }
        });

        if(closestTarget != nullptr && !isDeprioritizedTarget(*closestTarget)) {
            break;
        }
    }

    return closestTarget;
}

const ObjectBase* findTargetViaGrid(const ObjectBase& seeker,
                                    const SpatialGrid& grid,
                                    int checkRange,
                                    bool huntMode, bool* terrainRejected = nullptr) {
    const Coord seekerLocation = seeker.getLocation();
    if(!seekerLocation.isValid()) {
        return nullptr;
    }

    const auto* launcherHunt = huntMode ? managedLauncherHunt(seeker) : nullptr;
    const Coord centerCell = grid.clampToCell(seekerLocation);
    if(!centerCell.isValid()) {
        return nullptr;
    }

    const int cellSize = grid.getCellSize();
    const int gridWidth = grid.getGridWidth();
    const int gridHeight = grid.getGridHeight();

    const int maxRadiusX = std::max(centerCell.x, gridWidth - 1 - centerCell.x);
    const int maxRadiusY = std::max(centerCell.y, gridHeight - 1 - centerCell.y);
    const int maxReachableRadius = std::max(maxRadiusX, maxRadiusY);

    int searchRadius = 0;
    if(huntMode) {
        searchRadius = maxReachableRadius;
    } else {
        searchRadius = std::min((checkRange + cellSize - 1) / cellSize, maxReachableRadius);
    }

    std::vector<SpatialGridEntry> entries;
    entries.reserve(16);

    const ObjectBase* bestTarget = nullptr;
    auto bestDistance = FixPt_MAX;

    for(int ring = 0; ring <= searchRadius; ++ring) {
        bool ringProducedCandidate = false;

        forEachRingOffset(ring, [&](int dx, int dy) {
            Coord cell(centerCell.x + dx, centerCell.y + dy);
            ++ObjectBase::targetSearchStats.cellsVisited;
            if(!grid.isCellCoordValid(cell)) {
                return;
            }

            entries.clear();
            if(!grid.collectEntries(cell, entries)) {
                return;
            }

            for(const SpatialGridEntry& entry : entries) {
                ObjectBase* candidate = entry.object;
                if(candidate == nullptr || candidate == &seeker) {
                    continue;
                }

                const SpatialGridHandle& candidateHandle = candidate->getGridHandle();
                if(!candidateHandle.matches(entry.key.objectId, entry.key.generation)) {
                    continue;
                }

                if(isAutonomousTargetVetoed(seeker, *candidate)) {
                    continue;
                }

                ++ObjectBase::targetSearchStats.candidatesTested;
                if(!seeker.canAttack(candidate)) {
                    continue;
                }
                if(huntMode && !currentGameMap->terrainAttackReachable(seeker,*candidate)) {
                    if(terrainRejected) *terrainRejected=true;
                    continue;
                }
                if(launcherHunt && candidate->isAFlyingUnit()
                    && !launcherHunt->isInWeaponRange(candidate)) continue;

                const Coord candidatePoint = candidate->getClosestPoint(seekerLocation);
                if(!isTileVisibleToSeeker(seeker, candidatePoint)) {
                    continue;
                }

                const FixPoint distance = blockDistance(seekerLocation, candidatePoint);
                if(!huntMode && distance > FixPoint(checkRange)) {
                    continue;
                }

                const bool candidateDeprioritized = isDeprioritizedTarget(*candidate);
                if(candidateDeprioritized && bestTarget != nullptr) {
                    continue;
                }

                const bool bestIsDeprioritized = (bestTarget != nullptr) && isDeprioritizedTarget(*bestTarget);

                if(distance < bestDistance ||
                   (bestIsDeprioritized && !candidateDeprioritized)) {
                    bestTarget = candidate;
                    bestDistance = distance;
                    ringProducedCandidate = true;
                }
            }
        });

        if(bestTarget != nullptr) {
            const FixPoint nextRingLowerBound = FixPoint((ring + 1) * cellSize);
            // Don't break early if best target is deprioritized — keep searching for buildings
            const int bestDeprio = getTargetDeprioritizationLevel(*bestTarget);
            if(bestDeprio == 0 && (bestDistance <= nextRingLowerBound || ring == searchRadius)) {
                break;
            }
            if(bestDeprio > 0 && ring == searchRadius) {
                break;
            }
        } else if(!huntMode) {
            const int ringOuterBound = (ring + 1) * cellSize;
            if(ringOuterBound > checkRange) {
                break;
            }
        }

        if(!ringProducedCandidate && !huntMode) {
            const int ringOuterBound = (ring + 1) * cellSize;
            if(ringOuterBound > checkRange) {
                break;
            }
        }
    }

    return bestTarget;
}

} // namespace

Coord ObjectBase::getClosestPoint(const Coord& point) const {
    return location;
}

const StructureBase* ObjectBase::findClosestTargetStructure() const {
    const StructureBase *pClosestStructure = nullptr;
    auto closestDistance = FixPt_MAX;
    for(const StructureBase* pStructure : structureList) {
        if(canAttack(pStructure)) {
            const auto closestPoint = pStructure->getClosestPoint(getLocation());
            auto structureDistance = blockDistance(getLocation(), closestPoint);

            if(pStructure->getItemID() == Structure_Wall) {
                structureDistance += 20000000; //so that walls are targeted very last
            }

            if(structureDistance < closestDistance) {
                closestDistance = structureDistance;
                pClosestStructure = pStructure;
            }
        }
    }

    return pClosestStructure;
}

const UnitBase* ObjectBase::findClosestTargetUnit() const {
    const UnitBase *pClosestUnit = nullptr;
    auto closestDistance = FixPt_MAX;
    for(const UnitBase* pUnit : unitList) {
        if(canAttack(pUnit)) {
            const auto closestPoint = pUnit->getClosestPoint(getLocation());
            const auto unitDistance = blockDistance(getLocation(), closestPoint);

            if(unitDistance < closestDistance) {
                closestDistance = unitDistance;
                pClosestUnit = pUnit;
            }
        }
    }

    return pClosestUnit;
}

const ObjectBase* ObjectBase::findClosestTarget() const {
    ++targetSearchStats.searches;
    if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
        bool terrainRejected=false;
        if(const ObjectBase* target = findTargetViaGrid(*this, *grid, 0, true,&terrainRejected)) {
            return target;
        }
        if(terrainRejected) return nullptr; // Do not rescan every tile for the same unreachable enemies.
    }

    return findClosestTargetLegacy(*this);
}

const ObjectBase* ObjectBase::findTarget() const {
    ++targetSearchStats.searches;
    int checkRange = 0;
    bool huntMode = false;

    switch(attackMode) {
        case GUARD: {
            checkRange = getWeaponRange();
        } break;

        case AREAGUARD: {
            // Launchers get extended area guard range due to long weapon range
            checkRange = (getItemID() == Unit_Launcher || getItemID() == Unit_EliteLauncher) ? 12 : 10;
        } break;

        case AMBUSH: {
            checkRange = getViewRange();
        } break;

        case HUNT: {
            huntMode = true;
        } break;

        case STOP:
        default: {
            return nullptr;
        } break;
    }

    if(!huntMode && getItemID() == Unit_Sandworm) {
        checkRange = getViewRange();
    }

    // Special handling for Ornithopter: check if this is an Ornithopter and if so,
    // skip the spatial grid to let Ornithopter::findTarget() handle QuantBot-style scoring
    if(getItemID() != Unit_Ornithopter) {
        if(const SpatialGrid* grid = currentGame->getSpatialGrid()) {
            bool terrainRejected=false;
            if(const ObjectBase* target = findTargetViaGrid(*this, *grid, checkRange, huntMode,&terrainRejected)) {
                return target;
            }
            if(terrainRejected) return nullptr;
        }
    }

    return huntMode ? findClosestTargetLegacy(*this) : findTargetLegacy(*this, checkRange);
}

int ObjectBase::getViewRange() const {
    return currentGame->objectData.data[itemID][originalHouseID].viewrange;
}

int ObjectBase::getAreaGuardRange() const {
    return 2*getWeaponRange();
}

int ObjectBase::getWeaponRange() const {
    return currentGame->objectData.data[itemID][originalHouseID].weaponrange;
}

int ObjectBase::getWeaponReloadTime() const {
    return currentGame->objectData.data[itemID][originalHouseID].weaponreloadtime;
}

int ObjectBase::getInfSpawnProp() const {
    return currentGame->objectData.data[itemID][originalHouseID].infspawnprop;
}

ObjectBase* ObjectBase::createObject(int itemID, House* Owner, bool byScenario) {
    if(isTornieExclusiveItem(itemID)
       && !ModManager::instance().isTornieContentActive()) {
        SDL_Log("ObjectBase::createObject(): rejected Tornie-only ItemID %d outside Tornie", itemID);
        return nullptr;
    }

    ObjectBase* newObject = nullptr;
    switch(itemID) {
        case Structure_Barracks:            newObject = new Barracks(Owner); break;
        case Structure_ConstructionYard:    newObject = new ConstructionYard(Owner); break;
        case Structure_GunTurret:           newObject = new GunTurret(Owner); break;
        case Structure_HeavyFactory:        newObject = new HeavyFactory(Owner); break;
        case Structure_HighTechFactory:     newObject = new HighTechFactory(Owner); break;
        case Structure_IX:                  newObject = new IX(Owner); break;
        case Structure_LightFactory:        newObject = new LightFactory(Owner); break;
        case Structure_Palace:              newObject = new Palace(Owner); break;
        case Structure_Radar:               newObject = new Radar(Owner); break;
        case Structure_Refinery:            newObject = new Refinery(Owner); break;
        case Structure_RepairYard:          newObject = new RepairYard(Owner); break;
        case Structure_RocketTurret:        newObject = new RocketTurret(Owner); break;
        case Structure_Silo:                newObject = new Silo(Owner); break;
        case Structure_StarPort:            newObject = new StarPort(Owner); break;
        case Structure_Wall:                newObject = new Wall(Owner); break;
        case Structure_WindTrap:            newObject = new WindTrap(Owner); break;
        case Structure_AdvancedWindTrap:    newObject = new AdvancedWindTrap(Owner); break;
        case Structure_AdvancedWindTrapMK2: newObject = new AdvancedWindTrap(Owner, Structure_AdvancedWindTrapMK2); break;
        case Structure_AdvancedWindTrapMK3: newObject = new AdvancedWindTrap(Owner, Structure_AdvancedWindTrapMK3); break;
        case Structure_Worfinery:           newObject = new Worfinery(Owner); break;
        case Structure_TechCenter:          newObject = new TechCenter(Owner); break;
        case Structure_Scoutpost:           newObject = new Scoutpost(Owner); break;
        case Structure_Flamepost:           newObject = new Scoutpost(Owner, Structure_Flamepost); break;
        case Structure_Chemipost:           newObject = new Scoutpost(Owner, Structure_Chemipost); break;
        case Structure_LoveFactory:         newObject = new LoveFactory(Owner); break;
        case Structure_ChaosFactory:        newObject = new ChaosFactory(Owner); break;
        case Structure_WOR:                 newObject = new WOR(Owner); break;
        case Structure_NuclearPlant:        newObject = new NuclearPlant(Owner); break;
        case Structure_PoliceStation:       newObject = new PoliceStation(Owner); break;
        case Structure_ZoneResidential:     newObject = new ResidentialZone(Owner); break;
        case Structure_ZoneCommercial:      newObject = new CommercialZone(Owner); break;
        case Structure_ZoneIndustrial:      newObject = new IndustrialZone(Owner); break;
        case Structure_Stadium:             newObject = new Stadium(Owner); break;
        case Structure_Airport:             newObject = new Airport(Owner); break;

        case Unit_Carryall:                 newObject = new Carryall(Owner); break;
        case Unit_ChemicalCarryall:         newObject = new ChemicalCarryall(Owner); break;
        case Unit_Devastator:               newObject = new Devastator(Owner); break;
        case Unit_Deviator:                 newObject = new Deviator(Owner); break;
        case Unit_Frigate:                  newObject = new Frigate(Owner); break;
        case Unit_Harvester:                newObject = new Harvester(Owner); break;
        case Unit_RebelHarvester:           newObject = new RebelHarvester(Owner); break;
        case Unit_Soldier:                  newObject = new Soldier(Owner); break;
        case Unit_Launcher:                 newObject = new Launcher(Owner); break;
        case Unit_MCV:                      newObject = new MCV(Owner); break;
        case Unit_Ornithopter:              newObject = new Ornithopter(Owner); break;
        case Unit_Quad:                     newObject = new Quad(Owner); break;
        case Unit_Saboteur:                 newObject = new Saboteur(Owner); break;
        case Unit_Sandworm:                 newObject = new Sandworm(Owner); break;
        case Unit_SiegeTank:                newObject = new SiegeTank(Owner); break;
        case Unit_SonicTank:                newObject = new SonicTank(Owner); break;
        case Unit_Tank:                     newObject = new Tank(Owner); break;
        case Unit_Trike:                    newObject = new Trike(Owner); break;
        case Unit_RaiderTrike:              newObject = new RaiderTrike(Owner); break;
        case Unit_Trooper:                  newObject = new Trooper(Owner); break;
        case Unit_AmbientAirplane:          newObject = new AmbientAirplane(Owner); break;
        case Unit_AmbientHelicopter:        newObject = new AmbientHelicopter(Owner); break;
        case Unit_RocketTrike:              newObject = new RocketTrike(Owner); break;
        case Unit_SonicTrike:               newObject = new SonicTrike(Owner); break;
        case Unit_FlameTank:                newObject = new FlameTank(Owner); break;
        case Unit_EliteLauncher:            newObject = new EliteLauncher(Owner); break;
        case Unit_EliteSiegeTank:           newObject = new EliteSiegeTank(Owner); break;
        case Unit_ChemicalSiegeTank:        newObject = new ChemicalSiegeTank(Owner); break;
        case Unit_Special: {
            const bool modInitialized = ModManager::instance().isInitialized();
            const bool tornieActive = modInitialized && ModManager::instance().isTornieContentActive();
            const int houseID = Owner->getHouseID();
            const auto objectDataIxCandidates = discoverHouseSpecialVehicleCandidates([&](int candidate) {
                const auto& data = currentGame->objectData.data[candidate][houseID];
                return HouseSpecialVehicleCandidateData{
                    data.enabled,
                    data.builder,
                    data.prerequisiteStructuresSet[Structure_IX]
                };
            });

            const auto pool = resolveSpecialVehiclePoolForHouse(
                houseID, tornieActive, objectDataIxCandidates);
            std::vector<int> enabledPool;
            for(const int candidate : pool) {
                if(isSpecialVehicleSelectionCandidate(candidate) && currentGame->objectData.data[candidate][houseID].enabled) {
                    enabledPool.push_back(candidate);
                }
            }

            if(!enabledPool.empty()) {
                const int selected = enabledPool.size() == 1
                    ? enabledPool.front()
                    : enabledPool[currentGame->randomGen.rand(
                          static_cast<Sint32>(0), static_cast<Sint32>(enabledPool.size() - 1))];
                switch(selected) {
                    case Unit_Devastator:      newObject = new Devastator(Owner); break;
                    case Unit_Deviator:        newObject = new Deviator(Owner); break;
                    case Unit_SonicTank:       newObject = new SonicTank(Owner); break;
                    case Unit_FlameTank:       newObject = new FlameTank(Owner); break;
                    case Unit_EliteLauncher:   newObject = new EliteLauncher(Owner); break;
                    case Unit_EliteSiegeTank:  newObject = new EliteSiegeTank(Owner); break;
                    case Unit_ChemicalSiegeTank: newObject = new ChemicalSiegeTank(Owner); break;
                    default: break;
                }
            }
        } break;

        default:                            newObject = nullptr;
                                            SDL_Log("ObjectBase::createObject(): %d is no valid ItemID!",itemID);
                                            break;
    }

    if(newObject == nullptr) {
        return nullptr;
    }

    newObject->byScenario = byScenario;

    Uint32 objectID = currentGame->getObjectManager().addObject(newObject);
    newObject->setObjectID(objectID);

    return newObject;
}

ObjectBase* ObjectBase::loadObject(InputStream& stream, int itemID, Uint32 objectID) {
    if(isTornieExclusiveItem(itemID)
       && !ModManager::instance().isTornieContentActive()) {
        SDL_Log("ObjectBase::loadObject(): rejected Tornie-only ItemID %d outside Tornie", itemID);
        return nullptr;
    }
    ObjectBase* newObject = nullptr;
    switch(itemID) {
        case Structure_Barracks:            newObject = new Barracks(stream); break;
        case Structure_ConstructionYard:    newObject = new ConstructionYard(stream); break;
        case Structure_GunTurret:           newObject = new GunTurret(stream); break;
        case Structure_HeavyFactory:        newObject = new HeavyFactory(stream); break;
        case Structure_HighTechFactory:     newObject = new HighTechFactory(stream); break;
        case Structure_IX:                  newObject = new IX(stream); break;
        case Structure_LightFactory:        newObject = new LightFactory(stream); break;
        case Structure_Palace:              newObject = new Palace(stream); break;
        case Structure_Radar:               newObject = new Radar(stream); break;
        case Structure_Refinery:            newObject = new Refinery(stream); break;
        case Structure_RepairYard:          newObject = new RepairYard(stream); break;
        case Structure_RocketTurret:        newObject = new RocketTurret(stream); break;
        case Structure_Silo:                newObject = new Silo(stream); break;
        case Structure_StarPort:            newObject = new StarPort(stream); break;
        case Structure_Wall:                newObject = new Wall(stream); break;
        case Structure_WindTrap:            newObject = new WindTrap(stream); break;
        case Structure_AdvancedWindTrap:    newObject = new AdvancedWindTrap(stream); break;
        case Structure_AdvancedWindTrapMK2: newObject = new AdvancedWindTrap(stream, Structure_AdvancedWindTrapMK2); break;
        case Structure_AdvancedWindTrapMK3: newObject = new AdvancedWindTrap(stream, Structure_AdvancedWindTrapMK3); break;
        case Structure_Worfinery:           newObject = new Worfinery(stream); break;
        case Structure_TechCenter:          newObject = new TechCenter(stream); break;
        case Structure_Scoutpost:           newObject = new Scoutpost(stream); break;
        case Structure_Flamepost:           newObject = new Scoutpost(stream, Structure_Flamepost); break;
        case Structure_Chemipost:           newObject = new Scoutpost(stream, Structure_Chemipost); break;
        case Structure_LoveFactory:         newObject = new LoveFactory(stream); break;
        case Structure_ChaosFactory:        newObject = new ChaosFactory(stream); break;
        case Structure_WOR:                 newObject = new WOR(stream); break;
        case Structure_NuclearPlant:        newObject = new NuclearPlant(stream); break;
        case Structure_PoliceStation:       newObject = new PoliceStation(stream); break;
        case Structure_ZoneResidential:     newObject = new ResidentialZone(stream); break;
        case Structure_ZoneCommercial:      newObject = new CommercialZone(stream); break;
        case Structure_ZoneIndustrial:      newObject = new IndustrialZone(stream); break;
        case Structure_Stadium:             newObject = new Stadium(stream); break;
        case Structure_Airport:             newObject = new Airport(stream); break;

        case Unit_Carryall:                 newObject = new Carryall(stream); break;
        case Unit_ChemicalCarryall:         newObject = new ChemicalCarryall(stream); break;
        case Unit_Devastator:               newObject = new Devastator(stream); break;
        case Unit_Deviator:                 newObject = new Deviator(stream); break;
        case Unit_Frigate:                  newObject = new Frigate(stream); break;
        case Unit_Harvester:                newObject = new Harvester(stream); break;
        case Unit_RebelHarvester:           newObject = new RebelHarvester(stream); break;
        case Unit_Soldier:                  newObject = new Soldier(stream); break;
        case Unit_Launcher:                 newObject = new Launcher(stream); break;
        case Unit_MCV:                      newObject = new MCV(stream); break;
        case Unit_Ornithopter:              newObject = new Ornithopter(stream); break;
        case Unit_Quad:                     newObject = new Quad(stream); break;
        case Unit_Saboteur:                 newObject = new Saboteur(stream); break;
        case Unit_Sandworm:                 newObject = new Sandworm(stream); break;
        case Unit_SiegeTank:                newObject = new SiegeTank(stream); break;
        case Unit_SonicTank:                newObject = new SonicTank(stream); break;
        case Unit_Tank:                     newObject = new Tank(stream); break;
        case Unit_Trike:                    newObject = new Trike(stream); break;
        case Unit_RaiderTrike:              newObject = new RaiderTrike(stream); break;
        case Unit_Trooper:                  newObject = new Trooper(stream); break;
        case Unit_AmbientAirplane:          newObject = new AmbientAirplane(stream); break;
        case Unit_AmbientHelicopter:        newObject = new AmbientHelicopter(stream); break;
        case Unit_RocketTrike:              newObject = new RocketTrike(stream); break;
        case Unit_SonicTrike:               newObject = new SonicTrike(stream); break;
        case Unit_FlameTank:                newObject = new FlameTank(stream); break;
        case Unit_EliteLauncher:            newObject = new EliteLauncher(stream); break;
        case Unit_EliteSiegeTank:           newObject = new EliteSiegeTank(stream); break;
        case Unit_ChemicalSiegeTank:        newObject = new ChemicalSiegeTank(stream); break;

        default:                            newObject = nullptr;
                                            SDL_Log("ObjectBase::loadObject(): %d is no valid ItemID!",itemID);
                                            break;
    }

    if(newObject == nullptr) {
        return nullptr;
    }

    newObject->setObjectID(objectID);

    return newObject;
}

bool ObjectBase::targetInWeaponRange() const {
    const auto* object = target.getObjPointer();
    if(!object) return false;
    if(itemID == Structure_RocketTurret && object->getItemID() == Unit_Ornithopter)
        return DynastyProjectile::distance(getCenterPoint()*4, object->getCenterPoint()*4)
            <= getWeaponRange()*3*TILESIZE*4;
    Coord coord = (target.getObjPointer())->getClosestPoint(location);
    FixPoint dist = blockDistance(location,coord);

    return ( dist <= currentGame->objectData.data[itemID][originalHouseID].weaponrange);
}
