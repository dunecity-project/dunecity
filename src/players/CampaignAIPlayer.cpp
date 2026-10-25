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
#include <algorithm>
#include <array>

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
}

// =============================================================================
// Main update loop
// =============================================================================

void CampaignAIPlayer::update() {
    if((getGameCycleCount() + getHouse()->getHouseID()) % AIUPDATEINTERVAL != 0) {
        return;  // Not our turn this cycle
    }
    
    // ORIGINAL AI: Only act when AI is activated (ground unit contact)
    if(!getHouse()->isAIActivated()) {
        return;
    }
    
    updateStructures();
    updateUnits();
}

void CampaignAIPlayer::onObjectWasBuilt(const ObjectBase* pObject) {
    // Nothing special needed here for Original AI
}

void CampaignAIPlayer::onDecrementStructures(int itemID, const Coord& location) {
    // ORIGINAL AI: Add to 5-slot rebuild queue (structure.c:340-354)
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
            scrambleUnitsAndDefend(pDamager);
        }
        return;
    }
    
    // Handle unit damage
    if(!pObject->isAUnit() || !pObject->isRespondable()) {
        return;
    }
    
    const UnitBase* pUnit = static_cast<const UnitBase*>(pObject);
    const ObjectBase* pDamager = getObject(damagerID);
    if(!pDamager || !pDamager->getOwner()) {
        return;
    }
    
    // Ignore friendly/allied damage
    if(pDamager->getOwner()->getTeamID() == pUnit->getOwner()->getTeamID()) {
        return;
    }
    
    // Unit has been engaged by an enemy – allow attack waves to launch. A worm or a crime
    // trooper (the Natives) is no enemy house and does not open the offensive.
    if(!pDamager->getOwner()->isNatives()) attackTriggered = true;

    // Guard harvesters aggressively (Original AI behaviour tweak)
    if(pObject->getItemID() == Unit_Harvester) {
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
        
        // Repair damaged structures (< 50% health)
        if(pStructure->getHealth() < pStructure->getMaxHealth() / 2) {
            if(!pStructure->isRepairing()) {
                doRepair(pStructure);
            }
            continue;
        }
        
        // Handle builders
        if(pStructure->isABuilder()) {
            const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
            
            // Upgrade if not at max level
            if(pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()) {
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
                                doPlaceStructure(pConstYard, location.x, location.y);
                            } else if(itemID == Structure_Slab1 || itemID == Structure_Slab4) {
                                // Forget about concrete slabs that can't be placed
                                doCancelItem(pConstYard, itemID);
                            } else {
                                // Can't place - cancel and refund
                                doCancelItem(pConstYard, itemID);
                            }
                            
                            rebuildQueue.erase(iter);
                            break;
                        }
                    }
                } else if(!rebuildQueue.empty() && pBuilder->getProductionQueueSize() <= 0) {
                    // Start building from rebuild queue
                    const RebuildQueueEntry& entry = rebuildQueue.front();
                    if(pBuilder->isAvailableToBuild(entry.itemID)) {
                        doSetBuildSpeedLimit(pBuilder, 
                            std::min(1.0_fix, ((getTechLevel()-1) * 20 + 95)/255.0_fix));
                        doProduceItem(pBuilder, entry.itemID);
                    } else {
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
                    Uint32 gameMinutes = currentGame->getGameCycleCount() / (60 * 50);
                    if(gameMinutes < 10) {
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

void CampaignAIPlayer::updateUnits() {
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
    
    // Set all combat units to HUNT mode
    for(const UnitBase* pUnit : getUnitList()) {
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
