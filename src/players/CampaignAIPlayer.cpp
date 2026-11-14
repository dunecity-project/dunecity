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

#include <players/CampaignAIPlayer.h>
#include <players/OriginalCampaignPolicy.h>
#include <FileClasses/FileManager.h>
#include <players/QuantBotConfig.h>
#include <House.h>
#include <sand.h>
#include <Map.h>
#include <Game.h>
#include <misc/Random.h>

#include <structures/ConstructionYard.h>
#include <structures/Palace.h>
#include <structures/BuilderBase.h>
#include <units/UnitBase.h>
#include <FormationOrders.h>
#include <units/HarvesterHelpers.h>
#include <mod/ModManager.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <climits>

#define AIUPDATEINTERVAL 50

namespace {

template <typename T, std::size_t Size>
using ItemArray = std::array<T, Size>;

// Build priorities from Dune Dynasty unitinfo.c/structureinfo.c (priorityBuild field)
// Higher value = HIGHER priority (opposite of buildtime)
// NOTE: Most structure priorities are 0, but turrets have non-zero values (for rebuild priority)
const ItemArray<int, Num_ItemID> buildPriorityMap = []() {
    ItemArray<int, Num_ItemID> data{};
    // Units (from Dynasty src/table/unitinfo.c)
    data[Unit_Carryall] = 20;
    data[Unit_Ornithopter] = 75;
    data[Unit_Infantry] = 20;
    data[Unit_Troopers] = 50;
    data[Unit_Soldier] = 20;
    data[Unit_Trooper] = 50;
    data[Unit_Saboteur] = 0;
    data[Unit_Launcher] = 100;
    data[Unit_Deviator] = 50;
    data[Unit_Tank] = 80;
    data[Unit_SiegeTank] = 130;
    data[Unit_Devastator] = 175;
    data[Unit_SonicTank] = 80;
    data[Unit_Trike] = 50;
    data[Unit_RaiderTrike] = 55;
    data[Unit_Quad] = 60;
    data[Unit_Harvester] = 10;  // Low (rarely auto-built in Original AI)
    data[Unit_MCV] = 10;        // Low (never auto-built)
    data[Unit_Frigate] = 0;
    data[Unit_Sandworm] = 0;

    // Structures (from Dynasty src/table/structureinfo.c)
    // Most are zero (CY uses FIFO queue), but turrets have priorities for rebuild
    data[Structure_Slab1] = 0;
    data[Structure_Slab4] = 0;
    data[Structure_Palace] = 0;
    data[Structure_LightFactory] = 0;
    data[Structure_HeavyFactory] = 0;
    data[Structure_HighTechFactory] = 0;
    data[Structure_IX] = 0;
    data[Structure_WOR] = 0;
    data[Structure_ConstructionYard] = 0;
    data[Structure_WindTrap] = 0;
    data[Structure_Barracks] = 0;
    data[Structure_StarPort] = 0;
    data[Structure_Refinery] = 0;
    data[Structure_RepairYard] = 0;
    data[Structure_Wall] = 0;
    data[Structure_GunTurret] = 75;      // Dynasty structureinfo.c line 1037
    data[Structure_RocketTurret] = 100;  // Dynasty structureinfo.c line 1103
    data[Structure_Silo] = 0;
    data[Structure_Radar] = 0;
    data[Unit_Troopers5] = data[Unit_Troopers];
    data[Unit_Infantry5] = data[Unit_Infantry];
    data[Unit_FlameQuad] = data[Unit_Quad];
    data[Unit_GasInfantry] = data[Unit_Soldier];
    data[Unit_FlameTrooper] = data[Unit_Trooper];
    data[Unit_EliteOrnithopter] = data[Unit_Ornithopter];
    data[Unit_RaiderQuad] = data[Unit_Quad];
    data[Unit_RangedTank] = data[Unit_Tank];
    data[Unit_SlowTank] = data[Unit_Tank];
    data[Unit_CapacityHarvester] = data[Unit_Harvester];
    data[Unit_FastHarvester] = data[Unit_Harvester];
    data[Unit_TameSandworm] = data[Unit_Tank];
    data[Structure_Slowpost] = data[Structure_GunTurret];

    return data;
}();

// Target priorities for attack selection (kept from old implementation)
const ItemArray<int, Num_ItemID> targetPriorityMap = []() {
    ItemArray<int, Num_ItemID> data{};

    // Units
    data[Unit_Carryall] = 36;
    data[Unit_Ornithopter] = 105;
    data[Unit_Infantry] = 40;
    data[Unit_Troopers] = 100;
    data[Unit_Soldier] = 20;
    data[Unit_Trooper] = 50;
    data[Unit_Saboteur] = 700;
    data[Unit_Launcher] = 250;
    data[Unit_Deviator] = 225;
    data[Unit_Tank] = 180;
    data[Unit_SiegeTank] = 280;
    data[Unit_Devastator] = 355;
    data[Unit_SonicTank] = 190;
    data[Unit_Trike] = 100;
    data[Unit_RaiderTrike] = 115;
    data[Unit_Quad] = 120;
    data[Unit_Harvester] = 160;
    data[Unit_MCV] = 160;
    data[Unit_Frigate] = 0;
    data[Unit_Sandworm] = 0;

    // Structures
    data[Structure_Slab1] = 5;
    data[Structure_Slab4] = 10;
    data[Structure_Palace] = 400;
    data[Structure_LightFactory] = 200;
    data[Structure_HeavyFactory] = 600;
    data[Structure_HighTechFactory] = 200;
    data[Structure_IX] = 100;
    data[Structure_WOR] = 175;
    data[Structure_ConstructionYard] = 300;
    data[Structure_WindTrap] = 300;
    data[Structure_Barracks] = 100;
    data[Structure_StarPort] = 250;
    data[Structure_Refinery] = 300;
    data[Structure_RepairYard] = 600;
    data[Structure_Wall] = 30;
    data[Structure_GunTurret] = 225;
    data[Structure_RocketTurret] = 175;
    data[Structure_Silo] = 150;
    data[Structure_Radar] = 275;
    data[Unit_Troopers5] = data[Unit_Troopers];
    data[Unit_Infantry5] = data[Unit_Infantry];
    data[Unit_FlameQuad] = data[Unit_Quad];
    data[Unit_GasInfantry] = data[Unit_Soldier];
    data[Unit_FlameTrooper] = data[Unit_Trooper];
    data[Unit_EliteOrnithopter] = data[Unit_Ornithopter];
    data[Unit_RaiderQuad] = data[Unit_Quad];
    data[Unit_RangedTank] = data[Unit_Tank];
    data[Unit_SlowTank] = data[Unit_Tank];
    data[Unit_CapacityHarvester] = data[Unit_Harvester];
    data[Unit_FastHarvester] = data[Unit_Harvester];
    data[Unit_TameSandworm] = data[Unit_Tank];
    data[Structure_Doublefinery] = data[Structure_Refinery];
    data[Structure_Slowpost] = data[Structure_GunTurret];

    return data;
}();

inline int getBuildPriority(int itemID) {
    if(itemID < 0 || itemID >= static_cast<int>(buildPriorityMap.size())) {
        return 0;
    }
    return buildPriorityMap[itemID];
}

inline int getTargetPriority(int itemID) {
    if(itemID < 0 || itemID >= static_cast<int>(targetPriorityMap.size())) {
        return 0;
    }
    return targetPriorityMap[itemID];
}

} // namespace

// =============================================================================
// RebuildQueueEntry implementation
// =============================================================================

CampaignAIPlayer::RebuildQueueEntry::RebuildQueueEntry(InputStream& stream) {
    itemID = stream.readSint32();
    location.x = stream.readSint32();
    location.y = stream.readSint32();
}

void CampaignAIPlayer::RebuildQueueEntry::save(OutputStream& stream) const {
    stream.writeSint32(itemID);
    stream.writeSint32(location.x);
    stream.writeSint32(location.y);
}

// =============================================================================
// CampaignAIPlayer lifecycle
// =============================================================================

CampaignAIPlayer::CampaignAIPlayer(House* associatedHouse, const std::string& playername)
 : Player(associatedHouse, playername) {
    init();
    originalRules = getQuantBotConfig().originalCampaignAI;
}

CampaignAIPlayer::CampaignAIPlayer(InputStream& stream, House* associatedHouse) 
 : Player(stream, associatedHouse) {
    init();
    
    Uint32 numRebuildEntries = stream.readUint32();
    for(Uint32 i = 0; i < numRebuildEntries; i++) {
        rebuildQueue.emplace_back(stream);
    }
    
    // attackTriggered was added in version 9804
    if(currentGame->getLoadedSavegameVersion() >= 9804) {
        attackTriggered = stream.readBool();
    }
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_ORIGINAL_CAMPAIGN_AI) {
        originalRules = stream.readBool();
        scenarioTeamsInitialized = stream.readBool();
        nextTeamTick = stream.readUint32();
        const Uint32 count = stream.readUint32();
        if(count > 1024) throw std::runtime_error("Invalid campaign AI team count");
        for(Uint32 i = 0; i < count; ++i) {
            ScenarioTeam team;
            team.behavior = static_cast<AITeamBehavior>(stream.readSint32());
            team.type = static_cast<AITeamType>(stream.readSint32());
            team.minimum = stream.readSint32();
            team.maximum = stream.readSint32();
            team.staging = stream.readBool();
            team.position.x = stream.readSint32();
            team.position.y = stream.readSint32();
            team.targetID = stream.readUint32();
            team.script.pc = stream.readUint32();
            team.script.delay = stream.readUint16();
            team.script.result = stream.readUint16();
            team.script.sp = stream.readUint8();
            team.script.halted = stream.readBool();
            for(auto& word : team.script.stack) word = stream.readUint16();
            const Uint32 members = stream.readUint32();
            if(team.minimum < 0 || team.maximum < team.minimum || team.maximum > 65535
                || members > static_cast<Uint32>(team.maximum) || team.script.sp > 15
                || team.behavior < AITeamBehavior_Normal || team.behavior > AITeamBehavior_Flee
                || team.type < AITeamType_Foot || team.type > AITeamType_Harvester)
                throw std::runtime_error("Invalid campaign AI team state");
            for(Uint32 j = 0; j < members; ++j) team.members.push_back(stream.readUint32());
            scenarioTeams.push_back(std::move(team));
        }
    }
}

void CampaignAIPlayer::init() {
}

CampaignAIPlayer::~CampaignAIPlayer() = default;

void CampaignAIPlayer::save(OutputStream& stream) const {
    Player::save(stream);
    
    stream.writeUint32(rebuildQueue.size());
    for(const auto& entry : rebuildQueue) {
        entry.save(stream);
    }
    
    stream.writeBool(attackTriggered);
    stream.writeBool(originalRules);
    stream.writeBool(scenarioTeamsInitialized);
    stream.writeUint32(nextTeamTick);
    stream.writeUint32(scenarioTeams.size());
    for(const auto& team : scenarioTeams) {
        stream.writeSint32(team.behavior);
        stream.writeSint32(team.type);
        stream.writeSint32(team.minimum);
        stream.writeSint32(team.maximum);
        stream.writeBool(team.staging);
        stream.writeSint32(team.position.x);
        stream.writeSint32(team.position.y);
        stream.writeUint32(team.targetID);
        stream.writeUint32(team.script.pc);
        stream.writeUint16(team.script.delay);
        stream.writeUint16(team.script.result);
        stream.writeUint8(team.script.sp);
        stream.writeBool(team.script.halted);
        for(const auto word : team.script.stack) stream.writeUint16(word);
        stream.writeUint32(team.members.size());
        for(const Uint32 id : team.members) stream.writeUint32(id);
    }
}

// =============================================================================
// Main update loop
// =============================================================================

void CampaignAIPlayer::update() {
    if(originalRules && getHouse()->isAIActivated()) updateScenarioTeams();
    if((getGameCycleCount() + getHouse()->getHouseID()) % AIUPDATEINTERVAL != 0) {
        return;  // Not our turn this cycle
    }
    if(originalRules) {
        for(const auto* structure : getStructureList())
            if(structure->getOwner() == getHouse() && structure->isABuilder())
                const_cast<BuilderBase*>(static_cast<const BuilderBase*>(structure))->grantOriginalCampaignUpgrades();
    }
    
    // ORIGINAL AI: Only act when AI is activated (ground unit contact)
    if(!getHouse()->isAIActivated()) {
        return;
    }
    
    updateStructures();
    updateUnits();
}

void CampaignAIPlayer::onObjectWasBuilt(const ObjectBase* pObject) {
    if(originalRules && pObject && pObject->isAUnit()) {
        const auto* unit = static_cast<const UnitBase*>(pObject);
        if(originalCombatUnit(unit) && !unit->isByScenario()) doSetAttackMode(unit, HUNT);
    }
}

void CampaignAIPlayer::onDecrementStructures(int itemID, const Coord& location) {
    if (itemID == Structure_Slowpost && ModManager::instance().isTornieFamilyContentActive()) itemID = Structure_Scoutpost;
    if(originalRules && (itemID == Structure_Wall || itemID == Structure_Slab1 || itemID == Structure_Slab4)) return;
    // ORIGINAL AI: Add to 5-slot rebuild queue (structure.c:340-354)
    if(originalRules) {
        const auto empty = std::find_if(rebuildQueue.begin(), rebuildQueue.end(),
            [](const auto& entry) { return entry.itemID == ItemID_Invalid; });
        if(empty != rebuildQueue.end()) { *empty = RebuildQueueEntry(itemID, location); return; }
    }
    if(rebuildQueue.size() < 5) {
        rebuildQueue.emplace_back(itemID, location);
    }
}

void CampaignAIPlayer::onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) {
    // ORIGINAL AI: Trigger full-scale attack when structures are damaged (structure.c:2057-2070)
    if(pObject->isAStructure()) {
        const ObjectBase* pDamager = getObject(damagerID);
        // Only trigger for actual enemies (not allies) - check team IDs to respect alliances
        if(pDamager && pDamager->getOwner() && (pObject->getOwner()->getTeamID() != pDamager->getOwner()->getTeamID())) {
            // Defenders answer any intruder, but crime troopers are no opposing house: they
            // must not start this house's campaign-wide offensive.
            if(!pDamager->getOwner()->isNatives()) triggerFullScaleAttack();
            if(!originalRules) scrambleUnitsAndDefend(pDamager);
        }
        return;
    }
    
    // Handle unit damage
    if(!pObject->isAUnit() || !pObject->isRespondable()) {
        return;
    }
    
    const UnitBase* pUnit = static_cast<const UnitBase*>(pObject);
    if (ModManager::instance().isTornieFamilyContentActive() && pUnit->getAttackMode() == SABOTAGE) return;
    const ObjectBase* pDamager = getObject(damagerID);
    if(!pDamager || !pDamager->getOwner()) {
        return;
    }
    
    // Ignore friendly/allied damage
    if(pDamager->getOwner()->getTeamID() == pUnit->getOwner()->getTeamID()) {
        return;
    }
    // Original damage reaction belongs to the unit's own action script. Do not
    // force guards/team members to chase an attacker and abandon their orders.
    if(originalRules) return;
    
    // Unit has been engaged by an enemy – allow attack waves to launch. A worm or a crime
    // trooper (the Natives) is no enemy house and does not open the offensive.
    if(!originalRules && !pDamager->getOwner()->isNatives()) attackTriggered = true;

    // Guard harvesters aggressively (Original AI behaviour tweak)
    if(!originalRules && (pObject->getItemID() == Unit_Harvester || (ModManager::instance().isTornieFamilyContentActive()
        && isHarvesterLikeUnit(pObject->getItemID())))) {
        scrambleUnitsAndDefend(pDamager);
    }
    
    // The target ID may still be set after the target object was destroyed.
    // Resolve it once so a stale ObjectPointer cannot be dereferenced below.
    const ObjectBase* pCurrentTarget = pUnit->hasATarget() ? pUnit->getTarget() : nullptr;

    // Optional immediate retaliation if capable
    if(pUnit->canAttack(pDamager) && (pCurrentTarget == nullptr || pCurrentTarget->getTarget() != pUnit)) {
        doAttackObject(pUnit, pDamager, true);
    }
}

void CampaignAIPlayer::scrambleUnitsAndDefend(const ObjectBase* pIntruder) {
    if(!pIntruder) {
        return;
    }
    
    for(const UnitBase* pUnit : getUnitList()) {
        if (ModManager::instance().isTornieFamilyContentActive()
            && (pUnit->getAttackMode() == SABOTAGE || isHarvesterLikeUnit(pUnit->getItemID()))) continue;
        if(pUnit->getOwner() != getHouse() || !pUnit->isRespondable() || pUnit->wasForced()) {
            continue;
        }
        
        const Uint32 itemID = pUnit->getItemID();
        switch(itemID) {
            case Unit_Harvester:
            case Unit_MCV:
            case Unit_Carryall:
            case Unit_Frigate:
            case Unit_Saboteur:
            case Unit_Sandworm:
                continue;  // Non-combat or special units stay put
            default:
                break;
        }
        
        const bool isArtillery = (itemID == Unit_Launcher || itemID == Unit_Deviator);
        if(pUnit->hasATarget() && !isArtillery) {
            continue;
        }
        
        // Switch defenders into hunt mode so they sweep intruders instead of scripted attack-move
        doSetAttackMode(pUnit, HUNT);
        doAttackObject(pUnit, pIntruder, false);
    }
}

// =============================================================================
// Structure management (Original AI)
// =============================================================================

void CampaignAIPlayer::updateStructures() {
    for(const StructureBase* pStructure : getStructureList()) {
        if(pStructure->getOwner() != getHouse()) {
            continue;
        }
        
        // ORIGINAL AI: Palace auto-fire when charged and AI active (structure.c:115)
        if(pStructure->getItemID() == Structure_Palace) {
            const Palace* pPalace = static_cast<const Palace*>(pStructure);
            if(pPalace->isSpecialWeaponReady()) {
                if(!pPalace->usesTargetedSpecialWeapon()) {
                    doSpecialWeapon(pPalace);
                } else {
                    if(originalRules) {
                        // The original palace chooses the first enemy structure, not
                        // the richest house or DuneCity's nuclear reactor heuristic.
                        for(const auto* target : getStructureList()) {
                            if(target->getOwner()->isNatives()
                                || target->getOwner()->getTeamID() == getHouse()->getTeamID()
                                || target->getItemID() == Structure_Wall
                                || target->getItemID() == Structure_Slab1
                                || target->getItemID() == Structure_Slab4) continue;
                            doLaunchDeathhand(pPalace, target->getLocation().x, target->getLocation().y);
                            break;
                        }
                    } else {
                    // Death Hand - target house with most structures
                    const House* pBestHouse = nullptr;
                    for(int i = 0; i < NUM_HOUSES; i++) {
                        const House* pHouse = getHouse(i);
                        // The Natives have no base to strike.
                        if(!pHouse || pHouse->isNatives() || pHouse->getTeamID() == getHouse()->getTeamID()) {
                            continue;
                        }
                        if(!pBestHouse || pHouse->getNumStructures() > pBestHouse->getNumStructures()) {
                            pBestHouse = pHouse;
                        } else if(pBestHouse->getNumStructures() == 0 && 
                                  pHouse->getNumUnits() > pBestHouse->getNumUnits()) {
                            pBestHouse = pHouse;
                        }
                    }
                    
                    if(pBestHouse) {
                        Coord target = pBestHouse->getNumStructures() > 0 
                            ? pBestHouse->getCenterOfMainBase() 
                            : pBestHouse->getStrongestUnitPosition();
                        const Coord reactorTarget = findNuclearMissileTarget();
                        if (reactorTarget.isValid()) target = reactorTarget;
                        doLaunchDeathhand(pPalace, target.x, target.y);
                    }
                    }
                }
            }
        }
        if(originalRules && getHouse()->getCredits() <= 0) continue;
        
        // Repair damaged structures (< 50% health)
        if(pStructure->getHealth() < pStructure->getMaxHealth() / 2) {
            if(!pStructure->isRepairing()) {
                doRepair(pStructure);
            }
            continue;
        }
        
        if (doUpgradeScoutpost(pStructure, getHouse()->getCredits())) continue;
        // Handle builders
        if(pStructure->isABuilder()) {
            const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
            
            // Upgrade if not at max level
            if(!originalRules && pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()) {
                if(!pStructure->isRepairing() && !pBuilder->isUpgrading()) {
                    doUpgrade(pBuilder);
                }
                continue;
            }
            
            // ORIGINAL AI: Construction Yard rebuild queue handling (structure.c:242-305)
            if(pBuilder->getItemID() == Structure_ConstructionYard) {
                if(pBuilder->isWaitingToPlace()) {
                    int itemID = pBuilder->getCurrentProducedItem();
                    
                    // Try to place from rebuild queue at original location
                    for(auto iter = rebuildQueue.begin(); iter != rebuildQueue.end(); ++iter) {
                        if(iter->itemID == itemID) {
                            const auto location = iter->location;
                            Coord itemsize = getStructureSize(itemID);
                            const auto* pConstYard = static_cast<const ConstructionYard*>(pBuilder);
                            
                            if(getMap().okayToPlaceStructure(location.x, location.y, 
                                                            itemsize.x, itemsize.y, false, 
                                                            pConstYard->getOwner())) {
                                const bool placed = doPlaceStructure(pConstYard, location.x, location.y);
                                if(originalRules && !placed) continue;
                            } else if(originalRules) {
                                // A unit temporarily standing on a rebuild site must
                                // not erase that structure from the five-slot plan.
                                continue;
                            } else if(itemID == Structure_Slab1 || itemID == Structure_Slab4) {
                                // Forget about concrete slabs that can't be placed
                                doCancelItem(pConstYard, itemID);
                            } else {
                                // Can't place - cancel and refund
                                doCancelItem(pConstYard, itemID);
                            }
                            
                            if(originalRules) *iter = RebuildQueueEntry();
                            else rebuildQueue.erase(iter);
                            break;
                        }
                    }
                } else if(!rebuildQueue.empty() && pBuilder->getProductionQueueSize() <= 0) {
                    // Start building from rebuild queue
                    const auto selected = originalRules
                        ? std::find_if(rebuildQueue.begin(), rebuildQueue.end(), [&](const auto& candidate) {
                            return pBuilder->isAvailableToBuild(candidate.itemID);
                        }) : rebuildQueue.begin();
                    if(selected == rebuildQueue.end()) continue;
                    const RebuildQueueEntry& entry = *selected;
                    if(pBuilder->isAvailableToBuild(entry.itemID)) {
                        doSetBuildSpeedLimit(pBuilder, 
                            std::min(1.0_fix, ((getTechLevel()-1) * 20 + 95)/255.0_fix));
                        doProduceItem(pBuilder, entry.itemID);
                    } else if(!originalRules) {
                        // Unavailable - dequeue
                        rebuildQueue.erase(rebuildQueue.begin());
                    }
                }
            } else if(pBuilder->getItemID() != Structure_StarPort) {
                // ORIGINAL AI: Factory unit production
                if(pBuilder->getProductionQueueSize() >= 1) {
                    continue;  // Already busy
                }
                
                Uint32 itemID = pickNextToBuild(pBuilder);
                if(itemID != ItemID_Invalid) {
                    doSetBuildSpeedLimit(pBuilder, 
                        std::min(1.0_fix, ((getTechLevel()-1) * 20 + 95)/255.0_fix));
                    doProduceItem(pBuilder, itemID);
                }
            }
        }
    }
}

// =============================================================================
// Original AI build selection (ai.c:349-406)
// =============================================================================

Uint32 CampaignAIPlayer::pickNextToBuild(const BuilderBase* pBuilder) {
    if(!pBuilder) return ItemID_Invalid;
    if(originalRules) {
        const int selected = OriginalCampaignPolicy::pickUnit([&](int item) {
            return pBuilder->isAvailableToBuild(item) && shouldBuildItem(pBuilder->getItemID(), item);
        }, [&] { return getRandomGen().rand(0, 255); });
        if(selected != ItemID_Invalid) return selected;
        // A mod can replace every original unit in a factory. Keep that factory
        // productive using its legal combat roster; this extension is inert for
        // the original 18-unit roster and never substitutes harvesters or MCVs.
        int fallback = ItemID_Invalid, priority = 0;
        for(const auto& item : pBuilder->getBuildList()) {
            if(std::find(OriginalCampaignPolicy::unitOrder.begin(), OriginalCampaignPolicy::unitOrder.end(), item.itemID)
                    != OriginalCampaignPolicy::unitOrder.end()
                || !OriginalCampaignPolicy::combatUnit(item.itemID) || isHarvesterLikeUnit(item.itemID)
                || isCarryallUnit(item.itemID) || !pBuilder->isAvailableToBuild(item.itemID)
                || !shouldBuildItem(pBuilder->getItemID(), item.itemID)) continue;
            const int value = getBuildPriority(item.itemID);
            if(getRandomGen().rand(0, 255) % 4 == 0 || fallback == ItemID_Invalid || value > priority) {
                fallback = item.itemID; priority = value;
            }
        }
        return fallback;
    }
    
    // Get buildable items from builder's build list
    const std::list<BuildItem>& buildList = pBuilder->getBuildList();
    if(buildList.empty()) return ItemID_Invalid;

    const int customItem = chooseLowPriorityCustomUnit(pBuilder);
    if(customItem != ItemID_Invalid) {
        return static_cast<Uint32>(customItem);
    }
    
    // Build filtered candidate list (Original AI filters)
    std::vector<Uint32> candidates;
    candidates.reserve(buildList.size());
    
    for(const BuildItem& item : buildList) {
        if(shouldBuildItem(pBuilder->getItemID(), item.itemID)) {
            candidates.push_back(item.itemID);
        }
    }
    
    if(candidates.empty()) return ItemID_Invalid;
    
    // ORIGINAL AI: 25% chance to pick early, else highest priority
    Uint32 selectedItem = ItemID_Invalid;
    int selectedPriority = -1;
    
    for(Uint32 itemID : candidates) {
        // Use Dynasty build priorities (higher value = higher priority)
        int priority = (itemID < Num_ItemID) ? getBuildPriority(itemID) : 0;
        
        // 25% chance to select immediately (deterministic for MP)
        if(((getHouse()->getHouseID() + itemID) % 4) == 0) {
            selectedItem = itemID;
            selectedPriority = priority;
        }
        
        // Otherwise pick if HIGHER priority
        if(selectedItem != ItemID_Invalid && priority <= selectedPriority) {
            continue;
        }
        
        selectedItem = itemID;
        selectedPriority = priority;
    }
    
    return selectedItem;
}

// ORIGINAL AI build filters (ai.c:199-226)
// Returns true if this item should be built by this builder type
bool CampaignAIPlayer::shouldBuildItem(int builderType, Uint32 itemID) const {
    switch(builderType) {
        case Structure_HeavyFactory:
            // NEVER auto-build harvesters (must be manually queued)
            if(itemID == Unit_Harvester) return false;
            // NEVER build MCVs
            if(itemID == Unit_MCV) return false;
            break;
            
        case Structure_HighTechFactory:
            // Max 1 carryall (if one exists, don't build more)
            if(itemID == Unit_Carryall && getHouse()->getNumItems(Unit_Carryall) > 0) {
                return false;
            }
            
            // Ornithopter delay: Don't build within first 10 minutes in skirmish/MP
            if(itemID == Unit_Ornithopter) {
                const GameInitSettings& settings = currentGame->getGameInitSettings();
                if(settings.getGameType() == GameType::Skirmish || 
                   settings.getGameType() == GameType::CustomMultiplayer) {
                    // 10 minutes = 600 seconds = 30000 cycles at 50Hz
                    const bool tooEarly = originalRules
                        ? getGameCycleCount() < MILLI2CYCLES(10 * 60 * 1000)
                        : currentGame->getGameCycleCount() / (60 * 50) < 10;
                    if(tooEarly) {
                        return false;
                    }
                }
            }
            break;
        
        default:
            break;
    }
    
    return true;
}

// =============================================================================
// Unit management (Original AI)
// =============================================================================

bool CampaignAIPlayer::originalCombatUnit(const UnitBase* unit) const {
    return unit && unit->getOwner() == getHouse() && unit->isRespondable()
        && OriginalCampaignPolicy::combatUnit(unit->getItemID())
        && !isHarvesterLikeUnit(unit->getItemID()) && unit->getAttackMode() != SABOTAGE;
}

AITeamType CampaignAIPlayer::movementClass(const UnitBase* unit) {
    if(isHarvesterLikeUnit(unit->getItemID())) return AITeamType_Harvester;
    if(unit->getItemID() == Unit_Sandworm) return AITeamType_Slither;
    if(unit->isAFlyingUnit()) return AITeamType_Winged;
    if(unit->isInfantry()) return AITeamType_Foot;
    return unit->isTracked() ? AITeamType_Tracked : AITeamType_Wheeled;
}

const ObjectBase* CampaignAIPlayer::originalTarget(const UnitBase* unit, bool structuresFirst) const {
    const ObjectBase* bestStructure = nullptr;
    const ObjectBase* bestUnit = nullptr;
    int structurePriority = 0, unitPriority = 0;
    const auto eligible = [&](const ObjectBase* target) {
        return target && !target->getOwner()->isNatives()
            && target->getOwner()->getTeamID() != getHouse()->getTeamID()
            && target->isVisible(getHouse()->getTeamID()) && unit->canAttack(target)
            && target->getLocation().isValid();
    };
    for(const auto* target : getStructureList()) {
        if(!eligible(target) || target->getItemID() == Structure_Wall
            || target->getItemID() == Structure_Slab1 || target->getItemID() == Structure_Slab4) continue;
        const int priority = OriginalCampaignPolicy::targetPriority(getTargetPriority(target->getItemID()),
            blockDistanceApprox(unit->getLocation(), target->getLocation()), true);
        if(priority >= structurePriority && priority > 0) { structurePriority = priority; bestStructure = target; }
    }
    if(structuresFirst && bestStructure) return bestStructure;
    for(const auto* target : getUnitList()) {
        if(!eligible(target)) continue;
        const int combined = getTargetPriority(target->getItemID());
        const int priority = OriginalCampaignPolicy::targetPriority(combined,
            blockDistanceApprox(unit->getLocation(), target->getLocation()), false);
        if(priority > unitPriority) { unitPriority = priority; bestUnit = target; }
    }
    return bestStructure && structurePriority >= unitPriority ? bestStructure : bestUnit;
}

void CampaignAIPlayer::updateOriginalUnits() {
    for(const auto* unit : getUnitList()) {
        if(!originalCombatUnit(unit) || unit->isByScenario() || unit->wasForced()
            || unit->isInFormation() || unit->getLocation().isInvalid()) continue;
        // Produced units hunt independently. Scenario actions and mission-team
        // membership are handled separately, never folded into a generic wave.
        if(unit->getAttackMode() != HUNT) doSetAttackMode(unit, HUNT);
        if(!unit->getTarget()) {
            if(const auto* target = originalTarget(unit, false)) doAttackObject(unit, target, false);
        }
    }
}

void CampaignAIPlayer::updateScenarioTeams() {
    if(!scenarioTeamsInitialized && getHouse()->getAITeams().empty()) {
        scenarioTeamsInitialized = true;
        return;
    }
    if(scenarioTeamsInitialized && scenarioTeams.empty()) return;
    const Uint32 tick = static_cast<Uint32>(static_cast<Uint64>(getGameCycleCount()) * GAMESPEED_DEFAULT * 60 / 1000);
    if(tick < nextTeamTick) return;
    nextTeamTick = tick + (getRandomGen().rand(0, 255) & 7) + 5;
    if(teamProgram.words.empty()) {
        auto file = pFileManager->openFileFromNamedPak("TEAM.EMC", "DUNE.PAK");
        if(!file) throw std::runtime_error("Original Campaign AI requires TEAM.EMC in DUNE.PAK");
        const auto size = SDL_RWsize(file.get());
        if(size < 12 || size > 1024 * 1024) throw std::runtime_error("Invalid original TEAM.EMC size");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if(SDL_RWread(file.get(), bytes.data(), 1, bytes.size()) != bytes.size())
            throw std::runtime_error("Cannot read original TEAM.EMC");
        teamProgram = OriginalTeamScript::Program::parse(bytes);
    }
    const auto action = [](AITeamBehavior behavior) -> unsigned {
        switch(behavior) {
            case AITeamBehavior_Staging: return 1;
            case AITeamBehavior_Flee: return 2;
            case AITeamBehavior_Kamikaze: return 3;
            case AITeamBehavior_Guard: return 4;
            default: return 0;
        }
    };
    if(!scenarioTeamsInitialized) {
        for(const auto& definition : getHouse()->getAITeams()) {
            ScenarioTeam team;
            team.behavior = definition.aiTeamBehavior;
            team.type = definition.aiTeamType;
            team.minimum = std::clamp(definition.minUnits, 0, 65535);
            team.maximum = std::clamp(definition.maxUnits, team.minimum, 65535);
            team.staging = definition.aiTeamBehavior == AITeamBehavior_Staging;
            teamProgram.load(team.script, action(team.behavior));
            scenarioTeams.push_back(std::move(team));
        }
        scenarioTeamsInitialized = true;
    }
    std::set<Uint32> assigned;
    for(auto& team : scenarioTeams) {
        team.members.erase(std::remove_if(team.members.begin(), team.members.end(), [&](Uint32 id) {
            const auto* object = getObject(id);
            return !object || !object->isAUnit() || object->getOwner() != getHouse()
                || !object->isByScenario() || !assigned.insert(id).second;
        }), team.members.end());
        const auto* target = getObject(team.targetID);
        if(!target || target->getOwner()->isNatives()
            || target->getOwner()->getTeamID() == getHouse()->getTeamID()) team.targetID = NONE_ID;
    }
    for(auto& team : scenarioTeams) {
        // Run exactly one instruction, or decrement one delay count, per
        // original 5..12-tick scheduler turn. TEAM.EMC owns the cadence.
        const bool running = teamProgram.step(team.script, [&](unsigned function, OriginalTeamScript::State& vm) -> Uint16 {
            switch(function) {
                case 0: vm.delay = vm.peek() / 5; return vm.delay;
                case 1: case 11: case 14: return 0; // Text/no-op have no simulation effect.
                case 2: return static_cast<Uint16>(team.members.size());
                case 3: {
                    if(static_cast<int>(team.members.size()) >= team.maximum) return 0;
                    const UnitBase* nearest = nullptr;
                    ScenarioTeam* donor = nullptr;
                    int bestDistance = INT_MAX;
                    const auto eligible = [&](const UnitBase* unit) {
                        return unit && unit->getOwner() == getHouse() && unit->isByScenario()
                            && unit->isRespondable() && unit->getItemID() != Unit_Saboteur
                            && !unit->wasForced() && unit->getLocation().isValid() && movementClass(unit) == team.type;
                    };
                    for(const auto* unit : getUnitList()) {
                        if(!eligible(unit) || assigned.count(unit->getObjectID())) continue;
                        const int distance = blockDistanceApprox(team.position, unit->getLocation());
                        if(distance < bestDistance) { nearest = unit; bestDistance = distance; }
                    }
                    if(!nearest) for(auto& other : scenarioTeams) {
                        if(&other == &team || static_cast<int>(other.members.size()) > other.minimum) continue;
                        for(Uint32 id : other.members) {
                            const auto* unit = static_cast<const UnitBase*>(getObject(id));
                            if(!eligible(unit)) continue;
                            const int distance = blockDistanceApprox(team.position, unit->getLocation());
                            if(distance < bestDistance) { nearest = unit; bestDistance = distance; donor = &other; }
                        }
                    }
                    if(!nearest) return 0;
                    const auto id = nearest->getObjectID();
                    if(donor) donor->members.erase(std::remove(donor->members.begin(), donor->members.end(), id), donor->members.end());
                    team.members.push_back(id); assigned.insert(id);
                    return static_cast<Uint16>(team.maximum - team.members.size());
                }
                case 4: {
                    Sint64 x = 0, y = 0; int count = 0;
                    for(Uint32 id : team.members) {
                        const auto* unit = getObject(id);
                        if(!unit || unit->getLocation().isInvalid()) continue;
                        x += unit->getLocation().x; y += unit->getLocation().y; ++count;
                    }
                    if(!count) return 0;
                    team.position = Coord(static_cast<int>(x / count), static_cast<int>(y / count));
                    int distance = 0;
                    for(Uint32 id : team.members) {
                        const auto* unit = getObject(id);
                        if(unit && unit->getLocation().isValid()) distance += blockDistanceApprox(team.position, unit->getLocation());
                    }
                    return static_cast<Uint16>(distance / count);
                }
                case 5: {
                    const int distance = vm.peek(); Uint16 moving = 0;
                    for(Uint32 id : team.members) {
                        const auto* unit = static_cast<const UnitBase*>(getObject(id));
                        if(!unit || !unit->isRespondable() || unit->wasForced()) continue;
                        const auto destination = unit->getDestination();
                        const int toUnit = blockDistanceApprox(unit->getLocation(), team.position);
                        const int toDestination = destination.isValid() ? blockDistanceApprox(unit->getLocation(), destination) : 64;
                        const int teamToDestination = destination.isValid() ? blockDistanceApprox(team.position, destination) : 64;
                        if((toDestination < teamToDestination && distance + 2 < toUnit)
                            || (toDestination >= teamToDestination && toUnit > distance)) {
                            // DuneCity uses tile-centre path destinations. Spread nearby
                            // rally positions deterministically rather than one blocked tile.
                            const int x = std::clamp(team.position.x + getRandomGen().rand(-distance, distance), 0, getMap().getSizeX()-1);
                            const int y = std::clamp(team.position.y + getRandomGen().rand(-distance, distance), 0, getMap().getSizeY()-1);
                            doMove2Pos(unit, x, y, false); ++moving;
                        } else doSetAttackMode(unit, GUARD);
                    }
                    return moving;
                }
                case 6:
                    for(Uint32 id : team.members) {
                        const auto* unit = static_cast<const UnitBase*>(getObject(id));
                        if(!unit || !unit->isRespondable() || unit->getLocation().isInvalid()) continue;
                        if(const auto* target = originalTarget(unit, !team.staging && team.behavior == AITeamBehavior_Kamikaze)) {
                            team.targetID = target->getObjectID(); return 1;
                        }
                    }
                    return 0;
                case 7: {
                    const auto* target = getObject(team.targetID);
                    if(!target) return 0;
                    for(Uint32 id : team.members) {
                        const auto* unit = static_cast<const UnitBase*>(getObject(id));
                        if(!unit || !unit->isRespondable() || unit->wasForced() || !unit->canAttack(target)) continue;
                        if(unit->getTarget() == target && (unit->isMoving()
                            || blockDistanceApprox(unit->getLocation(), target->getLocation()) >= unit->getWeaponRange())) continue;
                        // Dune II ACTION_ATTACK pursues beyond a guard point.
                        // HUNT supplies that movement here; the team target is
                        // retained by targeting() without marking a player order.
                        doSetAttackMode(unit, HUNT);
                        doAttackObject(unit, target, false);
                    }
                    return 0;
                }
                case 8: {
                    const unsigned next = vm.peek();
                    const unsigned current = team.staging ? 1 : action(team.behavior);
                    if(next != current) {
                        if(next != 1 && next != action(team.behavior)) throw std::runtime_error("Unsupported campaign team action transition");
                        team.staging = next == 1; teamProgram.load(vm, next);
                    }
                    return 0;
                }
                case 9:
                    if(team.staging && team.behavior != AITeamBehavior_Staging) {
                        team.staging = false; teamProgram.load(vm, action(team.behavior));
                    }
                    return 0;
                case 10: vm.delay = getRandomGen().rand(0, 255) * vm.peek() / 256 / 5; return vm.delay;
                case 12: return static_cast<Uint16>(team.minimum);
                case 13: return team.targetID == NONE_ID ? 0 : 1;
                default: throw std::runtime_error("Unsupported campaign team callback");
            }
        });
        if(!running) break; // Original script error aborts this scheduler turn.
    }
}

bool CampaignAIPlayer::keepsScenarioTeamTarget(const UnitBase* unit) const {
    if(!originalRules || !unit || unit->getOwner() != getHouse() || !unit->isByScenario()
        || unit->wasForced() || unit->getAttackMode() != HUNT || !unit->getTarget()) return false;
    for(const auto& team : scenarioTeams)
        if(team.targetID == unit->getTarget()->getObjectID()
            && std::find(team.members.begin(), team.members.end(), unit->getObjectID()) != team.members.end()) return true;
    return false;
}

void CampaignAIPlayer::updateUnits() {
    if(originalRules) {
        updateOriginalUnits();
        return;
    }
    // Simple staging: gather idle combat units and launch waves only when we have enough
    std::vector<const UnitBase*> stagingUnits;
    stagingUnits.reserve(getUnitList().size());
    
    Uint32 now = getGameCycleCount();
    
    // Do not launch waves until we've been engaged (unit damaged by enemy)
    if(!attackTriggered && !getHouse()->hasTriggeredFullScaleAttack()) {
        return;
    }
    
    // Clear and rebuild the attack team each update (avoids O(N²) deduplication)
    attackTeam.memberIds.clear();
    
    // Collect idle combat units for attack team staging; a wave still marching on its target
    // in formation is not idle.
    for(const UnitBase* pUnit : getUnitList()) {
        if (ModManager::instance().isTornieFamilyContentActive()
            && (pUnit->getAttackMode() == SABOTAGE || isHarvesterLikeUnit(pUnit->getItemID()))) continue;
        if(pUnit->getOwner() != getHouse() || 
           pUnit->wasForced() || 
           !pUnit->isRespondable() || 
           pUnit->isByScenario() || 
           pUnit->hasATarget() ||
           pUnit->isInFormation() || pUnit->isHuntingToward()) {
            continue;
        }
        
        // Skip non-combat units
        if(pUnit->getItemID() == Unit_Harvester || 
           pUnit->getItemID() == Unit_MCV || 
           pUnit->getItemID() == Unit_Carryall || 
           pUnit->getItemID() == Unit_Frigate ||
           pUnit->getItemID() == Unit_Sandworm) {
            continue;
        }
        
        attackTeam.memberIds.push_back(pUnit->getObjectID());
    }
    
    // Launch only if we have enough units and cooldown has passed
    if(static_cast<int>(attackTeam.memberIds.size()) < static_cast<int>(attackTeam.minSize)) {
        return;
    }
    if(now < attackTeam.nextLaunchCycle) {
        return;
    }
    
    // Pick best target using all staged units
    const ObjectBase* pBestCandidate = nullptr;
    int bestPriority = -1;
    
    auto considerTarget = [&](const ObjectBase* target) {
        int priority = -1;
        for(Uint32 id : attackTeam.memberIds) {
            const UnitBase* pUnit = static_cast<const UnitBase*>(getObject(id));
            if(!pUnit || !pUnit->canAttack(target)) continue;
            priority = std::max(priority, calculateTargetPriority(pUnit, target));
        }
        if(priority > bestPriority) {
            bestPriority = priority;
            pBestCandidate = target;
        }
    };
    
    for(const StructureBase* pCandidate : getStructureList()) {
        if(!pCandidate || pCandidate->getOwner() == getHouse()) continue;
        considerTarget(pCandidate);
    }
    for(const UnitBase* pCandidate : getUnitList()) {
        // Waves go for an opposing house, never a worm or a crime trooper (the Natives).
        if(!pCandidate || pCandidate->getOwner() == getHouse() || pCandidate->getOwner()->isNatives()) continue;
        considerTarget(pCandidate);
    }
    
    if(!pBestCandidate) {
        return;
    }
    
    // Launch the wave. In a configured shape (WaveFormation, docs/formations.md) the ground
    // units march on the target together at the slowest one's pace and break into a fight at
    // the first contact; they arrive in Hunt. Aircraft, a wave too small to form up, and every
    // unit with WaveFormation=None (the default) set HUNT and attack the target as before.
    std::vector<const UnitBase*> wave, ground;
    Sint64 sumX = 0, sumY = 0;
    for(Uint32 id : attackTeam.memberIds) {
        const UnitBase* pUnit = static_cast<const UnitBase*>(getObject(id));
        if(!pUnit) continue;
        wave.push_back(pUnit);
        if(FormationOrders::eligible(pUnit)) {
            ground.push_back(pUnit);
            sumX += pUnit->getLocation().x;
            sumY += pUnit->getLocation().y;
        }
    }
    const bool formed = aiFormationShape() != QuantBotConfig::kWaveFormationNone
        && static_cast<int>(ground.size()) >= FormationOrders::kMinAIGroup;
    for(const UnitBase* pUnit : wave) {
        if(formed && FormationOrders::eligible(pUnit)) continue;
        doSetAttackMode(pUnit, HUNT);
        doAttackObject(pUnit, pBestCandidate, true);
    }
    if(formed) {
        const Coord centre(static_cast<int>(sumX / static_cast<Sint64>(ground.size())),
                           static_cast<int>(sumY / static_cast<Sint64>(ground.size())));
        const Coord goal = pBestCandidate->getClosestPoint(centre);
        doFormationHuntToward(ground, goal.x, goal.y);
    }
    
    // Team will be cleared and rebuilt on next update
    attackTeam.nextLaunchCycle = now + attackTeam.cooldownCycles;
}

int CampaignAIPlayer::calculateTargetPriority(const UnitBase* pUnit, const ObjectBase* pObject) {
    if(pUnit->getLocation().isInvalid() || pObject->getLocation().isInvalid()) {
        return 0;
    }
    
    int basePriority = getTargetPriority(pObject->getItemID());
    int distance = blockDistanceApprox(pUnit->getLocation(), pObject->getLocation());
    
    return (distance > 0) ? ((basePriority / distance) + 1) : basePriority;
}

// =============================================================================
// Full-scale attack (Original AI) - structure.c:2057-2070
// =============================================================================

void CampaignAIPlayer::triggerFullScaleAttack() {
    const House* pHouse = getHouse();
    
    // Only trigger once
    if(pHouse->hasTriggeredFullScaleAttack()) {
        return;
    }
    
    // Mark house as having done full-scale attack (need non-const access)
    const_cast<House*>(pHouse)->triggerFullScaleAttack();
    // Dune II's original Carryall-only search makes the mass-attack body a
    // no-op. Dynasty changes that only in its enhanced mode.
    if(originalRules) return;
    
    // Set all combat units to HUNT mode
    for(const UnitBase* pUnit : getUnitList()) {
        if (ModManager::instance().isTornieFamilyContentActive()
            && (pUnit->getAttackMode() == SABOTAGE || isHarvesterLikeUnit(pUnit->getItemID()))) continue;
        if(pUnit->getOwner() != pHouse) continue;
        
        // Skip non-combat units
        if(pUnit->getItemID() == Unit_Carryall || 
           pUnit->getItemID() == Unit_Harvester || 
           pUnit->getItemID() == Unit_MCV || 
           pUnit->getItemID() == Unit_Saboteur) {
            continue;
        }
        
        // Set to hunt/attack mode
        if(!pUnit->hasATarget()) {
            doSetAttackMode(pUnit, HUNT);
        }
    }
}
