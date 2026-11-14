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

#ifndef CAMPAIGNAIPLAYER_H
#define CAMPAIGNAIPLAYER_H

#include <players/Player.h>
#include <DataTypes.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <players/OriginalTeamScript.h>

#include <vector>

// Forward declarations
class House;
class ObjectBase;
class BuilderBase;
class StructureBase;
class UnitBase;

/**
 * Campaign AI Player - original Dune II rules with legacy save compatibility.
 * 
 * Key features:
 * - Ground-only contact activation (mutual activation of both houses)
 * - 5-slot rebuild queue with original position tracking
 * - Original AI build filters (no harvester auto-build, carryall max 1, ornithopter 10min delay)
 * - 25% randomization + priority-based build selection
 * - Palace auto-fire when AI active
 * - Original TEAM.EMC execution for authored scenario teams
 * - Independently hunting produced combat units
 * - Original ineffective full-scale attack flag, without added base scrambles
 */
class CampaignAIPlayer : public Player {
public:
    CampaignAIPlayer(House* associatedHouse, const std::string& playername);
    CampaignAIPlayer(InputStream& stream, House* associatedHouse);
    void init();
    virtual ~CampaignAIPlayer();

    virtual void save(OutputStream& stream) const;

    virtual void update();

    virtual void onObjectWasBuilt(const ObjectBase* pObject);
    virtual void onDecrementStructures(int itemID, const Coord& location);
    virtual void onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID);
    bool usesOriginalRules() const { return originalRules; }
    bool keepsScenarioTeamTarget(const UnitBase* unit) const;

private:
    /**
     * Rebuild queue entry - stores type and original position (from Dune Dynasty)
     */
    struct RebuildQueueEntry {
        int itemID;
        Coord location;
        
        RebuildQueueEntry() : itemID(ItemID_Invalid), location(-1, -1) {}
        RebuildQueueEntry(int id, const Coord& loc) : itemID(id), location(loc) {}
        RebuildQueueEntry(InputStream& stream);
        void save(OutputStream& stream) const;
    };

    void updateStructures();
    void updateUnits();
    
    /**
     * Pick next item to build (Original AI selection algorithm - ai.c:349-406)
     * @param pBuilder The factory/construction yard
     * @return ItemID to build, or ItemID_Invalid if nothing available
     */
    Uint32 pickNextToBuild(const BuilderBase* pBuilder);
    
    /**
     * Check if AI should build this item (Original AI filters - ai.c:199-226)
     * @param builderType Factory type
     * @param itemID Item to check
     * @return True if item should be auto-built
     */
    bool shouldBuildItem(int builderType, Uint32 itemID) const;
    
    /**
     * Calculate target priority for attack selection
     * @param pUnit Attacking unit
     * @param pObject Target object
     * @return Priority value (higher = better target)
     */
    int calculateTargetPriority(const UnitBase* pUnit, const ObjectBase* pObject);
    
    /**
     * Trigger full-scale attack (all combat units hunt) - structure.c:2057-2070
     */
    void triggerFullScaleAttack();

    /**
     * Scramble available combat units to defend the base/intruder location.
     */
    void scrambleUnitsAndDefend(const ObjectBase* pIntruder);

    // Captured when a game is created and saved. Old saves keep the old controller.
    bool originalRules = false;
    struct ScenarioTeam {
        AITeamBehavior behavior = AITeamBehavior_Normal;
        AITeamType type = AITeamType_Foot;
        int minimum = 0;
        int maximum = 0;
        bool staging = false;
        Coord position{0, 0};
        Uint32 targetID = NONE_ID;
        OriginalTeamScript::State script;
        std::vector<Uint32> members;
    };
    bool scenarioTeamsInitialized = false;
    std::vector<ScenarioTeam> scenarioTeams;
    OriginalTeamScript::Program teamProgram;
    Uint32 nextTeamTick = 0; // Original 60 Hz clock, independent of the 50-cycle build pass.
    void updateOriginalUnits();
    void updateScenarioTeams();
    const ObjectBase* originalTarget(const UnitBase* unit, bool structuresFirst) const;
    bool originalCombatUnit(const UnitBase* unit) const;
    static AITeamType movementClass(const UnitBase* unit);

    std::vector<RebuildQueueEntry> rebuildQueue;  // Max 5 entries (Original AI limit)

    // Pre-9886 approximation retained only for old saves or OriginalCampaignAI=0.
    struct AttackTeam {
        Uint32 minSize = 8;                 // Launch threshold
        Uint32 cooldownCycles = MILLI2CYCLES(12000); // 12s between launches
        Uint32 nextLaunchCycle = 0;
        std::vector<Uint32> memberIds;      // Object IDs of staged units
    };
    AttackTeam attackTeam;
    
    // Attack trigger: waves only launch after AI has been engaged at least once
    // SAVE COMPATIBILITY NOTE: Added in SAVEGAMEVERSION 9804
    bool attackTriggered = false;
};

#endif // CAMPAIGNAIPLAYER_H
