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

#ifndef HOUSE_H
#define HOUSE_H

#include <players/CombatReward.h>
#include <players/DeviationReward.h>
namespace AITelemetry { class Record; }
class ObjectData;

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <Definitions.h>
#include <DataTypes.h>
#include <SpiceIncome.h>
#include <AITeamInfo.h>
#include <data.h>
#include <Choam.h>

#include <players/Player.h>

#include <memory>

// forward declarations
class UnitBase;
class StructureBase;
class ObjectBase;
class HumanPlayer;

class House
{
public:
    House(int newHouse, int newCredits, int maxUnits, int maxHarvesters, Uint8 teamID = 0, int quota = 0);
    explicit House(InputStream& stream);
    void init();
    virtual ~House();
    virtual void save(OutputStream& stream) const;

    void addPlayer(std::unique_ptr<Player> newPlayer);
    void configureNetworkPlayers(const std::vector<std::pair<std::string, std::string>>& desired);
    void configureCoopPlayers(const std::vector<std::pair<std::string, std::string>>& desired);

    inline int getHouseID() const { return houseID; }
    inline int getTeamID() const { return teamID; }

    inline bool isAI() const { return ai; }
    // The live value can differ from the controller-list load default after promotion.
    void restoreObserverAI(bool enabled) { ai = enabled; }
    bool isAutoRepairEnabled() const { return autoRepairEnabled; }
    void setAutoRepairEnabled(bool enabled) { autoRepairEnabled = enabled; }
    inline bool isAlive() const { return (teamID == 0) || !(((numStructures - numItem[Structure_Wall]) <= 0) && (((numUnits - numItem[Unit_Carryall] - numItem[Unit_ChemicalCarryall] - numItem[Unit_Harvester] - numItem[Unit_RebelHarvester] - numItem[Unit_Frigate] - numItem[Unit_Sandworm] - numItem[Unit_AmbientAirplane] - numItem[Unit_AmbientHelicopter]) <= 0))); }

    inline bool hasCarryalls() const { return (numItem[Unit_Carryall] + numItem[Unit_ChemicalCarryall] > 0); }
    inline bool hasBarracks() const { return (numItem[Structure_Barracks] > 0); }
    inline bool hasIX() const { return (numItem[Structure_IX] > 0); }
    inline bool hasLightFactory() const { return (numItem[Structure_LightFactory] > 0); }
    inline bool hasHeavyFactory() const { return (numItem[Structure_HeavyFactory] > 0); }
    inline bool hasRefinery() const { return (numItem[Structure_Refinery] + numItem[Structure_Worfinery] > 0); }
    inline bool hasRepairYard() const { return (numItem[Structure_RepairYard] > 0); }
    inline bool hasStarPort() const { return (numItem[Structure_StarPort] > 0); }
    inline bool hasLoveFactory() const { return (numItem[Structure_LoveFactory] > 0); }
    inline bool hasWindTrap() const { return (numItem[Structure_WindTrap] > 0); }
    inline bool hasSandworm() const { return (numItem[Unit_Sandworm] > 0); }
    inline bool hasRadar() const { return (numItem[Structure_Radar] > 0); }

    // Radar always needs its full power allocation, even when other
    // production is allowed to operate without power (vanilla rules).
    inline bool hasRadarOn() const { return hasRadar() && producedPower >= powerRequirement; }
    bool isPowerRequired() const;
    bool hasPower() const;

    inline int getNumStructures() const { return numStructures; };
    inline int getNumUnits() const { return numUnits; };
    inline int getNumItems(int itemID) const { return (isStructure(itemID) || isUnit(itemID)) ? numItem[itemID] : 0; };

    inline int getCapacity() const { return capacity; }

    inline int getProducedPower() const { return producedPower; }
    void setProducedPower(int newPower);
    inline int getPowerRequirement() const { return powerRequirement; }

    /// Apply a runtime adjustment to the power requirement pool. Use this
    /// for state that isn't captured by the static ObjectData.power field —
    /// e.g. city zones whose draw scales with density.
    inline void adjustPowerRequirement(int delta) { powerRequirement += delta; }

    inline int getBuiltValue() const { return unitBuiltValue + structureBuiltValue; }
    inline int getUnitBuiltValue() const { return unitBuiltValue; }
    inline int getMilitaryValue() const { return militaryValue; }
    inline int getKillValue() const { return killValue; }
    inline int getLossValue() const { return lossValue; }
    inline int getStructureBuiltValue() const { return structureBuiltValue; }
    inline int getNumBuiltUnits() const { return numBuiltUnits; }
    inline int getNumBuiltStructures() const { return numBuiltStructures; }
    inline int getDestroyedValue() const { return destroyedValue; }
    inline int getNumDestroyedUnits() const { return numDestroyedUnits; }
    inline int getNumDestroyedStructures() const { return numDestroyedStructures; }
    inline int getNumBuiltItems(int itemID) const { return numItemBuilt[itemID]; }
    inline int getNumKilledItems(int itemID) const { return numItemKills[itemID]; }
    inline int getNumLostItems(int itemID) const { return numItemLosses[itemID]; }
    inline Sint32 getNumItemDamageInflicted(int itemID) const { return numItemDamageInflicted[itemID]; }
    const CombatReward::Totals& getCombatReward(int itemID) const { return combatRewards[itemID]; }
    void addCombatReward(Uint32 itemID, const CombatReward::Totals& reward);
    /// Scale of the learning bonus for destroying one of this house's units of \a itemID,
    /// read from this house's own measured record with that type. O(1), no scan.
    int killBonusPermilleForOwnType(int itemID, int price) const;
    /**
        The single lookup both killing-blow paths use - the ordinary lethal hit and the
        commanded Devastator completion - so the two can never disagree about one type.
        \a itemID and \a originalHouseID are the victim's natural type and natural owner,
        which is also how its price is keyed: a captured victim is therefore scored by the
        record of the house whose unit it really is, never by its captor's Deviator.
        Falls back to the flat baseline when that house or type cannot be resolved.
    */
    static int killBonusPermilleForVictim(int itemID, Sint32 originalHouseID, int price);
    AITelemetry::Record combatRewardStats(const ObjectData& objectData) const;
    /// Measured Deviator contribution totals. Deterministic simulation state, always maintained.
    const DeviationReward::HouseCounters& getDeviationCounters() const { return deviationCounters; }
    DeviationReward::HouseCounters& deviationCounters_() { return deviationCounters; }
    AITelemetry::Record deviationRewardStats() const;
    /**
        Clears the unit-mix learning ledger - rewards, raw damage and unit losses - so a
        pre-9849 save cannot mix the old flat conversion estimate and the old shooter-type
        attribution with measured contributions. Structure losses and general match
        statistics are preserved. See DeviationReward::migrateLegacyLedger().
    */
    void resetLearningLedgerForMeasuredScoring();
    inline FixPoint getHarvestedSpice() const { return harvestedSpice; }
    inline int getNumVisibleEnemyUnits() const { return numVisibleEnemyUnits; }
    inline int getNumVisibleFriendlyUnits() const { return numVisibleFriendlyUnits; }

    inline int getQuota() const { return quota; };
    int getMaxUnits() const;
    int getMaxHarvesters() const;

    inline void informContactWithEnemy() { bHadContactWithEnemy = true; };
    inline bool hadContactWithEnemy() const { return bHadContactWithEnemy; };
    inline void informDirectContactWithEnemy() { bHadDirectContactWithEnemy = true; };
    inline bool hadDirectContactWithEnemy() const { return bHadDirectContactWithEnemy; };
    
    // Original AI activation control (from Dune Dynasty)
    inline void activateAI() { isAIActive = true; };
    inline bool isAIActivated() const { return isAIActive; };
    inline void triggerFullScaleAttack() { doneFullScaleAttack = true; };
    inline bool hasTriggeredFullScaleAttack() const { return doneFullScaleAttack; };

    inline void informVisibleEnemyUnit() {
        numVisibleEnemyUnits++;
    }

    inline void informVisibleFriendlyUnit() {
        numVisibleFriendlyUnits++;
    }

    /**
        This function checks if the limit for ground units is already reached. Infantry units are only counted as 1/3.
        \return true, if the limit is already reached, false if building further ground units is allowed
    */
    inline bool isGroundUnitLimitReached() const {
        const int limit = getMaxUnits();
        if (limit == 0) return false;  // 0 = unlimited units
        int numGroundUnit = numUnits - numItem[Unit_Soldier] - numItem[Unit_Trooper] - numItem[Unit_Carryall] - numItem[Unit_ChemicalCarryall] - numItem[Unit_Ornithopter];
        return (numGroundUnit + (numItem[Unit_Soldier]+2)/3 + (numItem[Unit_Trooper]+2)/3  >= limit);
    };

    /**
        This function checks if the limit for infantry units is already reached. Infantry units are only counted as 1/3.
        \return true, if the limit is already reached, false if building further infantry units is allowed
    */
    inline bool isInfantryUnitLimitReached() const {
        const int limit = getMaxUnits();
        if (limit == 0) return false;  // 0 = unlimited units
        int numGroundUnit = numUnits - numItem[Unit_Soldier] - numItem[Unit_Trooper] - numItem[Unit_Carryall] - numItem[Unit_ChemicalCarryall] - numItem[Unit_Ornithopter];
        return (numGroundUnit + numItem[Unit_Soldier]/3 + numItem[Unit_Trooper]/3  >= limit);
    };

    /**
        This function checks if the limit for air units is already reached.
        \return true, if the limit is already reached, false if building further air units is allowed
    */
    inline bool isAirUnitLimitReached() const {
        const int limit = getMaxUnits();
        if (limit == 0) return false;  // 0 = unlimited units
        return (numItem[Unit_Carryall] + numItem[Unit_ChemicalCarryall] + numItem[Unit_Ornithopter] >= 11*std::max(limit,25)/25);
    }

    /**
        Check the appropriate house limit for a specific unit type.
        \return true if creating itemID would exceed its unit category limit
    */
    bool isUnitLimitReached(int itemID) const;

    /**
        This function checks if the limit for harvesters is already reached.
        \return true, if the limit is already reached, false if building further harvesters is allowed
    */
    inline bool isHarvesterLimitReached() const {
        const int limit = getMaxHarvesters();
        if (limit == 0) return false;  // 0 = unlimited harvesters
        return ((numItem[Unit_Harvester] + numItem[Unit_RebelHarvester]) >= limit);
    }

    inline Choam& getChoam() { return choam; };
    inline const Choam& getChoam() const { return choam; };


    /// What a refinery pays this house per unit of spice a harvester delivers, relative to the
    /// original rate. Chosen per house row in the Custom Game lobby; 1 everywhere else.
    inline Uint32 getSpiceIncomeMultiplier() const { return spiceIncomeMultiplier; }
    /**
        Sets the delivery income factor. Refuses anything outside SpiceIncome's range instead of
        clamping: a factor that silently became 1 would make this house earn at a different rate
        than the peers it is playing against.
        \param  multiplier  the factor to use
    */
    void setSpiceIncomeMultiplier(Uint32 multiplier);

    inline FixPoint getStartingCredits() const { return startingCredits; }
    inline FixPoint getStoredCredits() const { return storedCredits; }
    inline FixPoint getCityCredits() const { return cityCredits; }
    inline FixPoint getCityTaxReceipts() const { return cityTaxReceipts; }
    static constexpr int MAX_GAME_CREDITS = 999999;
    /// Cap for the cumulative tax statistic. Far above any reachable mission
    /// total, but small enough that the 32.32 fixed-point total cannot overflow.
    static constexpr int MAX_CITY_TAX_RECEIPTS = 1000000000;
    inline int getCredits() const { return lround(storedCredits+startingCredits+cityCredits); }
    /// Credits the house earned this mission (refined spice plus city taxes).
    /// These share the refinery/silo storage and never exceed getCapacity().
    inline FixPoint getEarnedCredits() const { return storedCredits + cityCredits; }
    /// How much more earned income the house can still store right now.
    FixPoint getEarnedCreditRoom() const;
    void enforceCreditCapacity();
    void addCredits(FixPoint newCredits, bool wasRefined = false);
    void addCityCredits(FixPoint amount);
    void addCityTaxReceipts(FixPoint grossAmount);
    /// How a withdrawal was funded. Starting cash is storage-exempt, so a
    /// refund has to know how much of the payment came from that pool: paying
    /// it back as earned income would either destroy it (no refinery yet) or
    /// launder exempt cash into the capacity-limited earned balance.
    struct CreditPayment {
        FixPoint total = 0;         ///< what was actually withdrawn
        FixPoint fromStarting = 0;  ///< the part taken from exempt starting cash
    };
    /// Withdraw up to \a amount and report which pools paid for it.
    CreditPayment payCredits(FixPoint amount);
    /// Refund \a newCredits, of which \a fromStartingCredits was originally
    /// paid out of starting cash and returns there. The rest is earned income
    /// and stays subject to storage capacity; the total never exceeds the
    /// absolute credit ceiling.
    void returnCredits(FixPoint newCredits, FixPoint fromStartingCredits = 0);
    FixPoint takeCredits(FixPoint amount);

    void printStat() const;

    void updateBuildLists();

    void update();

    void incrementUnits(int itemID, bool addMilitaryValue = true);
    void decrementUnits(int itemID);
    void cancelCreatedUnit(int itemID);
    void incrementStructures(int itemID);
    void decrementStructures(int itemID, const Coord& location, bool recordLoss = true);
    void transformStructure(int oldItemID, int newItemID);

    /**
        An object was hit by something or damaged somehow else.
        \param  pObject     the object that was damaged
        \param  damage      the damage taken
        \param  damagerID   the shooter of the bullet, rocket, etc. if known; NONE_ID otherwise
    */
    void noteDamageLocation(ObjectBase* pObject, int damage, Uint32 damagerID);

    void informWasBuilt(ObjectBase* pObject);
    void informHasKilled(Uint32 itemID);
    void informHasDamaged(Uint32 itemID, Uint32 damage);

    void lose(bool bSilent = false);
    void win();

    void freeHarvester(int xPos, int yPos);
    void freeHarvester(const Coord& coord) { freeHarvester(coord.x, coord.y); };
    StructureBase* placeStructure(Uint32 builderID, int itemID, int xPos, int yPos, bool byScenario = false, bool bForcePlacing = false);
    UnitBase* createUnit(int itemID, bool byScenario = false);
    UnitBase* placeUnit(int itemID, int xPos, int yPos, bool byScenario = false);

    Coord getCenterOfMainBase() const;

    Coord getStrongestUnitPosition() const;

    const std::vector<AITeamInfo> getAITeams() const { return aiteams; };
    void addAITeam(AITeamBehavior aiTeamBehavior, AITeamType aiTeamType, int minUnits, int maxUnits) {
        aiteams.emplace_back(houseID, aiTeamBehavior, aiTeamType, minUnits, maxUnits);
    }

    const std::list<std::unique_ptr<Player> >& getPlayerList() const { return players; };

protected:
    bool autoRepairEnabled = false;
    void decrementHarvesters();

    /**
        Books as much of \a amount as the refinery/silo storage can still hold.
        The rest is destroyed and reported as storage_lost; the caller adds the
        returned amount to whichever earned pool it belongs to.
    */
    FixPoint acceptEarnedCredits(FixPoint amount);
    void enforceCreditCeiling();

    /// Tells the local player that storage is full, at most once every 5 seconds.
    void warnStorageFull();

    std::list<std::unique_ptr<Player> > players;        ///< List of associated players that control this house

    bool    ai;             ///< Is this an ai player?

    Uint8   houseID;        ///< The house number
    Uint8   teamID;         ///< The team number

    int numStructures;          ///< How many structures does this player have?
    int numUnits;               ///< How many units does this player have?
    int numItem[Num_ItemID];    ///< This array contains the number of structures/units of a certain type this player has
    int numItemBuilt[Num_ItemID];  /// Number of items built by player
    int numItemKills[Num_ItemID]; /// Number of items killed by player
    int numItemLosses [Num_ItemID]; /// Number of items lost by player
    CombatReward::Totals combatRewards[Num_ItemID];
    Sint32 numItemDamageInflicted[Num_ItemID]; /// Amount of damage inflicted by a specific unit type owned by the player
    DeviationReward::HouseCounters deviationCounters; ///< measured Deviator contribution (SAVEGAMEVERSION 9849)

    int capacity;             ///< Total spice capacity
    int producedPower;        ///< Power prodoced by this player
    int powerRequirement;     ///< How much power does this player use?

    FixPoint storedCredits;   ///< current number of credits that are stored in refineries/silos
    FixPoint startingCredits; ///< number of starting credits this player still has
    FixPoint cityCredits;     ///< spendable city tax income, excluded from the harvested-spice quota
    FixPoint cityTaxReceipts; ///< cumulative gross city tax collected this mission (statistic only: never spent, never reduced by police costs or credit caps)
    /// Delivery income factor for this house (SAVEGAMEVERSION 9851). Part of the simulation:
    /// it is mixed into the state digest and read at every refinery payout.
    Uint32 spiceIncomeMultiplier;
    /// Next game cycle at which the "storage is full" ticker may repeat.
    /// Presentation only; it never influences the simulation.
    Uint32 nextStorageWarningCycle;
    int oldCredits;           ///< amount of credits in the last game cycle (used for playing the credits tick sound)

    int maxUnits;             ///< maximum number of units this house is allowed to build
    int maxHarvesters;        ///< maximum number of harvesters this house is allowed to build
    int quota;                ///< number of credits to win

    Choam   choam;            ///< the things that are deliverable at the starport

    std::vector<AITeamInfo> aiteams;    ///< the ai teams that were loaded from the map

    int powerUsageTimer;      ///< every N ticks you have to pay for your power usage

    bool bHadContactWithEnemy;      ///< did this house already have contact with an enemy (= tiles with enemy units were explored by this house or allied houses)
    bool bHadDirectContactWithEnemy;///< did this house already have direct contact with an enemy (= tiles with enemy units were explored by this house)
    
        // Original AI activation flags (from Dune Dynasty)
        // SAVE COMPATIBILITY NOTE: Adding these flags requires SAVEGAMEVERSION bump (9803).
        // Old saves will fail to load with clear error message. This is intentional for major AI replacement.
        bool isAIActive;                ///< AI is "awake" - activated when ground units make contact (mutual activation)
        bool doneFullScaleAttack;       ///< One-time all-in assault flag - prevents repeated full-scale attacks

    int numVisibleEnemyUnits;   ///< the number of enemy units visible; will be reset to 0 each cycle
    int numVisibleFriendlyUnits;///< the number of visible units from the same team; will be reset to 0 each cycle

    // statistic
    int unitBuiltValue;
    int structureBuiltValue;
    int militaryValue;
    int killValue;
    int lossValue;
    int numBuiltUnits;
    int numBuiltStructures;
    int destroyedValue;
    int numDestroyedUnits;
    int numDestroyedStructures;
    FixPoint harvestedSpice;
};

#endif // HOUSE_H
