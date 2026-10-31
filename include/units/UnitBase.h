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

#ifndef UNITBASE_H
#define UNITBASE_H

#include <ObjectBase.h>
#include <units/UnitOrderQueue.h>
#include <FormationOrders.h>

#include <House.h>

#include <list>
#include <cstdint>
#include <limits>

// forward declarations
class Tile;

class UnitBase : public ObjectBase
{
public:
    enum class TargetRequestKind : uint8_t {
        None,
        Refresh,
        Acquire
    };
    explicit UnitBase(House* newOwner);
    explicit UnitBase(InputStream& stream);
    void init();
    virtual ~UnitBase();

    UnitBase(const UnitBase &) = delete;
    UnitBase(UnitBase &&) = delete;
    UnitBase& operator=(const UnitBase &) = delete;
    UnitBase& operator=(UnitBase &&) = delete;

    void save(OutputStream& stream) const override;
    virtual void saveObserverRuntime(OutputStream& stream) const;
    virtual void loadObserverRuntime(InputStream& stream);

    // Factory technology is independent of permanent ownership and deviation.
    int getProductionHouseID() const override;
    virtual void setProductionHouseID(int houseID);
    void refreshRadarResearch();
    void applySlow();
    int getSlowTimer() const { return slowTimer; }
    FixPoint getMovementSpeedFactor() const;
    bool canCaptureStructures() const;

    void blitToScreen() override;

    ObjectInterface* getInterfaceContainer() override;

    virtual void checkPos() = 0;
    virtual void deploy(const Coord& newLocation);
    void cancelDeployment();

    void destroy() override;
    /**
        Hands control of this unit to newOwner until the deviation timer runs out.
        \param  newOwner            the house taking control; the original house ends the deviation
        \param  deviatorObjectID    the Deviator that fired, kept for diagnostics only
    */
    void deviate(House* newOwner, Uint32 deviatorObjectID = NONE_ID);

    void drawSelectionBox() override;
    void drawOtherPlayerSelectionBox() override;

    /**
        This method is called when an unit is ordered by a right click
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    void handleActionClick(int xPos, int yPos) override;
    // Shared by contextual cursor/feedback and the actual right-click command.
    ObjectBase* getActionClickTarget(int xPos, int yPos) const;

    /**
        This method is called when an unit is ordered to attack
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    virtual void handleAttackClick(int xPos, int yPos);
    virtual void handleHealClick(int xPos, int yPos);

    /**
        This method is called when an unit is ordered to move
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    virtual void handleMoveClick(int xPos, int yPos);


    /**
        This method is called when an unit is ordered to be in a new attack mode
        \param  newAttackMode   the new attack mode the unit is put in.
    */
    virtual void handleSetAttackModeClick(ATTACKMODE newAttackMode);


    /**
        This method is called when an unit is ordered to request a carryall drop
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    virtual void handleRequestCarryallDropClick(int xPos, int yPos);

    /**
        This method is called when an unit should move to (xPos,yPos)
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
        \param  bForced true, if the unit should ignore everything else
    */
    virtual void doMove2Pos(int xPos, int yPos, bool bForced);

    /**
        This method is called when an unit should move to coord
        \param  coord   the position on the map
        \param  bForced true, if the unit should ignore everything else
    */
    virtual void doMove2Pos(const Coord& coord, bool bForced);

    /**
        Hunt on the way to (xPos,yPos). The unit walks there with an ordinary,
        unforced move in Hunt mode, and Hunt takes over from the move: any enemy
        that comes within huntTowardRange() on the way is attacked. Once the
        unit is within that range of the destination it is an ordinary hunter.
        Unlike setting Hunt directly, this does not start a map-wide target
        search that would send the unit to the nearest enemy anywhere instead.
        Any later order ends it. If the engine drops the walk because it found
        no path, it is resumed up to kHuntTowardRetries times before the unit
        becomes an ordinary hunter.
    */
    void doHuntToward(int xPos, int yPos);

    /// Still walking a doHuntToward() route?
    bool isHuntingToward() const { return huntTowardGoal.isValid(); }

    /// How close an enemy has to be for a unit hunting on its way to stop for it:
    /// the farther of its weapon range and its view range.
    int huntTowardRange() const;
    static constexpr int kHuntTowardRetries = 3;

    /**
        Player orders (docs/rts-controls.md). Executed only from Command::executeCommand, so
        every peer applies them on the same cycle. Without \a queued the order replaces the
        unit's queue and starts now; with \a queued (Shift) it is appended and starts when the
        orders before it are complete. A queued order on an empty queue starts at once.
        Move, MoveToObject, AttackObject, AttackMove, Capture and HarvesterReturn only;
        patrol goes through doIssuePatrol.
    */
    void doIssueOrder(const UnitOrder& order, bool queued);

    /// Patrol between where the unit stands and \a point; with \a queued the point extends the route.
    void doIssuePatrol(const Coord& point, bool queued);

    /**
        A classic order (right-click move, attack, capture, harvester return) was just executed
        through its own command: it becomes the front of the queue, so a following Shift order
        runs after it instead of replacing it.
    */
    void recordPlayerOrder(const UnitOrder& order);

    /// Any order that is not queueable (Stop, Hold, a mode change, repair, forced fire...) ends the queue.
    void clearOrderQueue();

    const UnitOrderQueue& getOrderQueue() const { return orderQueue; }

    /// Walking an attack-move or patrol leg, engaging enemies met on the way?
    bool isAttackMoving() const;

    /// Does this unit fight on an attack-move? Workers, transports and Saboteurs just walk.
    bool fightsOnAttackMove() const;

    /// An explicit player order ends HOLD; the unit returns to its default, Area Guard.
    void leaveHoldForOrder();

    /**
        Formation membership (docs/formations.md). A member travels to its formation slot -
        or, without a slot, keeps its own order - at the group's pace, recovering its place at
        full speed or waiting for a member that fell behind as FormationOrders::update()
        decides each cycle. Set only through FormationOrders::join(). Any new order
        from a player or an AI, a carryall pickup, deviation, arrival, contact with an enemy or
        being the last member left ends it. Saved from SAVEGAMEVERSION 9857; the hunt flag
        and the facing from 9859. In a hunting formation the first contact of any member, or a hit on one,
        breaks the whole group into a fight (FormationOrders::breakHunt).
    */
    bool isInFormation() const { return formationGroup != 0; }
    Uint32 getFormationGroup() const { return formationGroup; }
    const Coord& getFormationSlot() const { return formationSlot; }
    FixPoint getFormationSpeedCap() const { return formationSpeedCap; }
    FormationPlanner::March getFormationMarch() const { return formationMarch; }
    bool isFormationHunt() const { return formationGroup != 0 && formationHunt; }
    /// The formation's facing (FormationPlanner::facingCode; 0 = none), saved from 9859.
    Uint8 getFormationFacing() const { return formationGroup != 0 ? formationFacing : 0; }
    /// The tile this unit moves to while isMoving().
    const Coord& getNextSpot() const { return nextSpot; }
    /// Has this unit picked the next tile of its path (and not started the step yet unless isMoving())?
    bool hasNextSpot() const { return nextSpotFound; }
    /// Joins formation groupID heading for slot (invalid: keep the current order), capped
    /// at this unit's own reference speed until FormationOrders::join sets the group's.
    /// 'resume' is how the order towards the slot is given again after a form-up.
    void joinFormation(Uint32 groupID, const Coord& slot, bool hunt = false, Uint8 facing = 0,
                       FormationOrders::Resume resume = FormationOrders::Resume::Move);
    /// Joined this cycle; FormationOrders::update() plans the group's form-up next.
    bool isFormationPending() const { return formationGroup != 0 && formationPending; }
    void clearFormationPending() { formationPending = false; }
    /// Forming up: walking to (or waiting on) the form-up tile before the march (saved from 9860).
    bool isFormationForming() const { return formationGroup != 0 && formationFormUp.isValid(); }
    const Coord& getFormationFormUp() const { return formationFormUp; }
    /// Starts the form-up: a walk to 'tile' (forced like the order it stands in for).
    void startFormUp(const Coord& tile);
    /// Done forming up: on its form-up tile, or its walk there is over (no path, or an
    /// attack-move already walking on), or nothing left to move for.
    bool formUpReady() const;
    /// The group has formed up: the order towards the slot is given again and the march begins.
    void endFormUp();
    /// The group's pace: the slowest member's reference speed and its unit type, whose step
    /// time on each tile every capped member keeps (UnitBase::formationPaceStepLength).
    void setFormationPace(FixPoint cap, Uint32 paceItem) { formationSpeedCap = cap; formationPaceItem = paceItem; }
    Uint32 getFormationPaceItem() const { return formationPaceItem; }
    /// This cycle's march (FormationPlanner::marchState), decided before units move.
    void setFormationMarch(FormationPlanner::March march) { formationMarch = march; }
    void leaveFormation();
    /**
        Hands this unit to newOwner for good: owner and original house both change, the unit
        counts move from the old original house to the new one without booking a loss or a
        build, and the unit leaves its formation and every selection. Orders, target and
        attack mode are kept. Used to give sandworms to the Natives house.
    */
    void transferOwnership(House* newOwner);
    /// The pace this unit sets in a formation: its speed on open sand, undamaged, unloaded, off-road.
    FixPoint getFormationReferenceSpeed() const;
    /// Contact: the enemy this unit is targeting within huntTowardRange(), or nullptr.
    const ObjectBase* formationContact() const;
    /**
        A hunting formation broke on contact with 'enemy' (FormationOrders::breakHunt): a
        fighter without a target attacks it if its attack mode keeps a target that far away,
        and otherwise closes in on it, engaging whatever it meets on the way; an attack-move
        or patrol resumes its own walk from there. A 'hunter' (a member of an AI hunting
        formation, Resume::HuntToward) does the same in Hunt mode and stays in Hunt.
    */
    void engageFormationContact(const ObjectBase* enemy, bool hunter = false);
    /// Is this unit marching in an AI hunting formation (Resume::HuntToward)?
    bool isFormationHuntToward() const {
        return formationGroup != 0 && formationResume == static_cast<Uint8>(FormationOrders::Resume::HuntToward);
    }
    /// How this member's order towards its slot is given (FormationOrders::Resume).
    FormationOrders::Resume getFormationResume() const { return static_cast<FormationOrders::Resume>(formationResume); }
    /// Has this member's march ended: arrived or idle, in contact with an enemy, or unable to
    /// move? A hunting member has also arrived once it stands on its slot.
    bool formationMarchEnded() const;
    /// Tiles still to go to the formation slot, or to the current destination without a slot.
    int formationRemainingTiles() const;

    /**
        This method is called when an unit should move to another unit/structure
        \param  TargetObjectID  the ID of the other unit/structure
    */
    virtual void doMove2Object(Uint32 TargetObjectID);

    /**
        This method is called when an unit should move to another unit/structure
        \param  pTargetObject   the other unit/structure
    */
    virtual void doMove2Object(const ObjectBase* pTargetObject);

    /**
        This method is called when an unit should attack a position
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
        \param  bForced true, if the unit should ignore everything else
    */
    virtual void doAttackPos(int xPos, int yPos, bool bForced);

    /**
        This method is called when an unit should attack to another unit/structure
        \param  pTargetObject   the target unit/structure
        \param  bForced true, if the unit should ignore everything else
    */
    virtual void doAttackObject(const ObjectBase* pTargetObject, bool bForced);

    /**
        This method is called when an unit should attack to another unit/structure
        \param  TargetObjectID  the ID of the other unit/structure
        \param  bForced true, if the unit should ignore everything else
    */
    virtual void doAttackObject(Uint32 TargetObjectID, bool bForced);

    /**
        This method is called when an unit should change it's current attack mode
        \param  newAttackMode   the new attack mode
    */
    void doSetAttackMode(ATTACKMODE newAttackMode);

    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;

    void doRepair() override { }

    /**
        Is this object in a range we are guarding. If yes we shall react.
        \param  object  the object to check
    */
    bool isInGuardRange(const ObjectBase* object) const;

    /**
        Is this object in a range we want to attack. If no, we should stop following it.
        \param  object  the object to check
    */
    bool isInAttackRange(const ObjectBase* object) const;

    /**
        Is this object in a range we can attack.
        \param  object  the object to check
    */
    bool isInWeaponRange(const ObjectBase* object) const;

    void setAngle(int newAngle);
    const std::list<Coord>& getPlannedPath() const { return pathList; }

    void setTarget(const ObjectBase* newTarget) override;

    void setGettingRepaired();

    inline void setGuardPoint(const Coord& newGuardPoint) { setGuardPoint(newGuardPoint.x, newGuardPoint.y); }

    void setGuardPoint(int newX, int newY);

    void setLocation(int xPos, int yPos) override;

    inline void setLocation(const Coord& location) { setLocation(location.x, location.y); }

    inline void setDestination(int newX, int newY) override
    {
        if((destination.x != newX) || (destination.y != newY)) {
            ObjectBase::setDestination(newX, newY);
            clearPath();
        }
    }

    inline void setDestination(const Coord& location) { setDestination(location.x, location.y); }

    virtual void setPickedUp(UnitBase* newCarrier);

    /// Is this unit currently inside a transport or a structure bay rather than on the map?
    /// True from setPickedUp() until the unit is deployed again. On its own it does not say
    /// which container holds the unit; Game's containment reconciliation answers that from the
    /// actual carrier cargo lists and structure occupant pointers.
    inline bool isPickedUp() const { return pickedUp; }

    /**
        Updates this unit.
        \return true if this unit still exists, false if it was destroyed
    */
    bool update() override;

    virtual bool canPass(int xPos, int yPos) const;
    /// Route planning: canPass, or the tile holds a marching member of this unit's marching
    /// formation (marchesWith), which moves on at the same pace.
    bool canPathThrough(int xPos, int yPos) const;
    /// Are this unit and 'other' marching in the same formation, 'other' moving on (keeping the
    /// pace, recovering, or holding with a next step to give way on)? Then this unit waits behind
    /// it rather than routing round it - unless this unit fell behind and 'other' keeps the pace.
    bool marchesWith(const UnitBase* other) const;

    virtual bool hasBumpyMovementOnRock() const { return false; }

    /**
        Returns how fast a unit can move over the specified terrain type.
        \param  terrainType the type to consider
        \return Returns a speed factor. Higher values mean slower.
    */
    virtual FixPoint getTerrainDifficulty(TERRAINTYPE terrainType) const { return 1; }

    virtual int getCurrentAttackAngle() const;

    virtual FixPoint getMaxSpeed() const;

    void resolvePendingTargetRequest();
    
    struct PathRequestStats {
        bool pathFound = false;
        bool invalidDestination = false;
        size_t nodesExpanded = 0;
    };
    PathRequestStats resolvePendingPathRequest();

    /// Main-thread precheck for a budgeted search. Clears the pending flag, runs
    /// the HUNT reachability rule and resolves the destination. Returns false when
    /// there is nothing to search, in which case \a stats is already final.
    /// Deliberately does not touch recalculatePathTimer: a search that is only
    /// starting must not leave a countdown that would defer a resumed or
    /// ordinary-load-requeued job.
    bool beginPathRequest(PathRequestStats& stats, Coord& destinationCoord);
    /// Main-thread application of a finished search, including the cooldown the
    /// whole-search path used to set up front.
    void applyPathResult(std::list<Coord> path, const Coord& destinationCoord,
                         Uint32 searchStartRevision, PathRequestStats& stats);
    /// Shared tail of both forms: stuck detection, carryall requests and the
    /// give-up rules that run once a search has produced its answer.
    void finishPathRequest(PathRequestStats& stats);

    /// Everything a running search's result depends on besides terrain: the
    /// endpoints, the blocking-geometry revision, ownership and the target and
    /// repair state that canPass() consults. Deliberately excludes other units'
    /// positions -- those change constantly, movement revalidates passability
    /// per step, and including them would restart every cross-tick search
    /// whenever an unrelated unit moved.
    struct PathInputFingerprint {
        Coord location = Coord::Invalid();
        Coord destination = Coord::Invalid();
        Uint32 pathingRevision = 0;
        Uint32 targetObjectID = NONE_ID;
        Sint32 houseID = -1;
        Sint32 teamID = -1;
        bool targetFriendly = false;
        bool goingToRepairYard = false;
        bool repairYardFree = false;
        // canPass() lets a unit enter its own target only when that object is
        // still there, is a structure, is on our team and is visible to us, and
        // only enters a repair yard while it is free. Retaining the id alone would
        // miss the target being destroyed, changing hands through deviation, or
        // becoming occupied, so each fact canPass actually reads is captured.
        bool targetPresent = false;
        bool targetIsStructure = false;
        bool targetVisibleToUs = false;
        Sint32 targetOwnerTeamID = -1;
        Uint32 targetItemID = NONE_ID;
        // Frozen search parameters: turn speed is read once from objectData for
        // this item and original house, so a change invalidates the plan's costs.
        Uint32 itemID = NONE_ID;
        Sint32 originalHouseID = -1;

        enum Difference : Uint32 {
            StartMoved      = 1u << 0,
            DestinationMoved= 1u << 1,
            GeometryChanged = 1u << 2,
            TargetChanged   = 1u << 3,
            OwnerChanged    = 1u << 4,
            RepairChanged   = 1u << 5,
            ParametersChanged = 1u << 6
        };
        /// Inputs that make a running search answer the wrong question, so it has
        /// to start again. Deliberately excludes GeometryChanged: that counter is
        /// global, it moves whenever anyone anywhere places or loses blocking
        /// geometry, and on a busy city map it was measured bumping about every
        /// 2.4 cycles -- restarting on it meant no cross-tick search ever
        /// finished. A path built across a geometry change is already handled the
        /// way every other path is: once the revision has moved,
        /// isCachedPathStillValid() re-checks canPass over a bounded near-prefix of
        /// the route (kPathValidationProbeCount tiles) rather than all of it, and
        /// UnitBase::update revalidates the next step before taking it.
        static constexpr Uint32 RestartForcingMask =
            StartMoved | DestinationMoved | TargetChanged | OwnerChanged | RepairChanged | ParametersChanged;

        /// Which inputs differ. Reported in telemetry so a restart storm can be
        /// attributed to a field instead of guessed at.
        Uint32 differenceMask(const PathInputFingerprint& o) const {
            Uint32 mask = 0;
            if(location != o.location) mask |= StartMoved;
            if(destination != o.destination) mask |= DestinationMoved;
            if(pathingRevision != o.pathingRevision) mask |= GeometryChanged;
            if(targetObjectID != o.targetObjectID || targetFriendly != o.targetFriendly
               || targetPresent != o.targetPresent || targetIsStructure != o.targetIsStructure
               || targetVisibleToUs != o.targetVisibleToUs || targetOwnerTeamID != o.targetOwnerTeamID
               || targetItemID != o.targetItemID) mask |= TargetChanged;
            if(houseID != o.houseID || teamID != o.teamID) mask |= OwnerChanged;
            if(goingToRepairYard != o.goingToRepairYard || repairYardFree != o.repairYardFree) mask |= RepairChanged;
            if(itemID != o.itemID || originalHouseID != o.originalHouseID) mask |= ParametersChanged;
            return mask;
        }
        bool operator==(const PathInputFingerprint& o) const { return differenceMask(o) == 0; }
        bool operator!=(const PathInputFingerprint& o) const { return !(*this == o); }
    };
    /// Resolves the current destination itself, so the scheduler never needs the
    /// protected target-resolution rules.
    PathInputFingerprint capturePathInputs() const;

    /// The pending flag spans the whole request, not just its time in the queue:
    /// it is set when the request is enqueued and cleared only when the request
    /// completes or is cancelled, which is what stops update() from enqueueing a
    /// second request for a unit whose search is still suspended.
    bool hasPendingPathRequest() const { return pathRequestQueued; }
    void markPathRequestActive() { pathRequestQueued = true; }
    void markPathRequestFinished() { pathRequestQueued = false; }

    inline void clearPath() {
        pathList.clear();
        nextSpotFound = false;
        recalculatePathTimer = 0;
        nextSpotAngle = INVALID;
        noCloserPointCount = 0;
        // pathRequestQueued is deliberately NOT cleared here. Abandoning the route
        // you are walking says nothing about a request that is still in flight, and
        // clearing it let a unit enqueue a second request for a search the
        // scheduler was still running -- update() calls clearPath() on a cache
        // miss or a blocked step, and setDestination() calls it on every new
        // order. The scheduler owns this flag end to end: it clears it when the
        // request completes or is cancelled, and a changed order is picked up
        // because the in-flight plan's inputs no longer match.
        noProgressCount = 0;
        lastDistanceToDestination = -1;
        cachedPathDestination.invalidate();
        cachedPathRevision = 0;
    }

    inline bool isTracked() const { return tracked; }

    inline bool isTurreted() const { return turreted; }

    inline bool isMoving() const { return moving; }

    inline bool wasDeviated() const { return (owner->getHouseID() != originalHouseID); }

    /**
        A temporarily deviated unit is never repaired by the house that converted it: no automatic
        trip, no explicit order, no repair-yard booking and no carryall docking. The single check
        lives here so every repair path shares it, and repairs resume by themselves once the
        deviation timer reverts ownership.
    */
    inline bool isEligibleForRepair() const { return !wasDeviated(); }

    /**
        The control interval this unit is currently part of, used to attribute what it does
        while borrowed to the Deviator of the house controlling it. Inactive when the unit
        answers to its own house.
    */
    inline const DeviationReward::Episode& getDeviationEpisode() const { return deviationEpisode; }

    inline int getAngle() const { return drawnAngle; }

    inline ATTACKMODE getAttackMode() const { return attackMode; }

    inline const Coord& getGuardPoint() const { return guardPoint; }

    virtual void playAttackSound();

    /// Does this unit fire Dynasty's launcher rocket pair instead of the legacy follow-up shot?
    bool usesLauncherRocketPair() const;

    /// May the second weapon fire right now? Launchers need strictly more than half health.
    bool canFireSecondaryWeapon() const;

    /// Cycles between the two rockets of a launcher pair: 18 = 0.288 s, matching
    /// Dynasty's 5-6 movement ticks. See docs/weapon-reload-comparison.md.
    static constexpr Sint32 launcherBurstGapCycles = 18;

    /// Every other two-weapon unit keeps its legacy follow-up shot delay.
    static constexpr Sint32 defaultSecondaryWeaponCycles = 15;

    /**
        Long-stall carryall rescue.

        Thirty seconds of simulation time on the same tile while the unit still wants to be
        somewhere else. Measured in game cycles, never in wall-clock time, so every peer and
        every observer reaches the threshold on the same cycle. Deliberately independent of
        pathfinding: a unit waiting behind a long request queue, or one whose cached route is
        cleared on every blocked step, accumulates stall time exactly like a unit whose searches
        all fail. clearPath() and a pending search do not touch this clock.
    */
    static constexpr Sint32 longStallRescueCycles = MILLI2CYCLES(30000);

    /**
        Minimum cycles between two automatic rescue attempts for one unit, before the
        per-unit jitter below. A failed attempt does not restart the thirty-second clock and
        does not change the unit's mode: it simply retries after this cooldown.
    */
    static constexpr Sint32 carryallRescueCooldownCycles = MILLI2CYCLES(5000);

    /// Spreads simultaneous retries of a stalled army across cycles. Derived from the object
    /// id, so it is identical on every peer.
    static constexpr Sint32 carryallRescueJitterCycles = 64;

    /// How long this unit has been on the same tile while still wanting to move. Zero while it
    /// is making progress or has no movement intent.
    Uint32 getStalledCycleCount() const;

    /// True once the long-stall threshold has been reached. Diagnostic/test accessor.
    bool isLongStalled() const;

    /**
        An explicit order from a real player restarts the stall clock even when it repeats the
        previous coordinates. Autonomous AI route re-issues deliberately do not call this: an
        identical order re-sent every cycle must not suppress stall detection for a unit that
        never actually moves.
    */
    void noteExplicitPlayerOrder();

    /// Releases an outstanding carryall pickup. Only ground units can be carried, so the base
    /// implementation does nothing; GroundUnit cancels its booking.
    virtual void cancelCarryallPickup() {}

    /**
        Render only (Game::drawScreen; docs/unit-route-comparison.md, "Chained steps"): under the
        default movement rule (GameOptionsClass::continuousGroundSteps off, the 1.0.852 simulation),
        while it exists every Dynasty ground unit on a step that kept a stale justStoppedMoving has
        realX/realY set to where its step puts it; the destructor puts the simulation's values
        back. Such a step was chained onto the previous one on the cycle that ended it, and
        GroundUnit::checkPos holds the unit on its tile centre for the whole step, so drawn as
        simulated it stands still for half a step and then jumps a whole tile. Nothing runs a cycle
        while the scope exists, so the simulation and its digest are unchanged. With the rule on the
        simulation clears the flag and the scope does nothing.
    */
    class DrawnStepScope {
    public:
        DrawnStepScope();
        ~DrawnStepScope();
        DrawnStepScope(const DrawnStepScope&) = delete;
        DrawnStepScope& operator=(const DrawnStepScope&) = delete;
    };

    friend struct TornieUnitStateFixture;
protected:
    // Counts belong to the original house even while a unit is deviated.
    void registerUnit();
    bool restoredFromSave = false;


    void updateVisibleUnits();

    virtual bool attack();

    virtual void releaseTarget();
    virtual void engageTarget();
    virtual void move();

    virtual void bumpyMovementOnRock(FixPoint fromDistanceX, FixPoint fromDistanceY, FixPoint toDistanceX, FixPoint toDistanceY);

    virtual void navigate();
    bool usesDynastyGroundTiming() const;
    bool turnDynastyBody(int wantedAngle);
    bool beginDynastyStep(Uint64 routeTick);
    /// The time the formation's pace unit takes for this step (now-units), or 0 without one or
    /// while this member recovers its place: the floor of a capped member's step time.
    Uint64 formationPaceStepLength(Uint64 routeTick, TERRAINTYPE terrain, bool road, bool diagonal) const;
    bool moveDynastyStep();
    virtual Coord movementEndpoint() const;
    // Clock units are 1/3000 second: a legacy cycle is48, a Dynasty tick50.
    // Persisted so saves and observer checkpoints continue mid-step exactly.
    Uint64 dynastyRouteTick = 0;
    Uint64 dynastyStepStart = 0, dynastyStepEnd = 0;
    // Family speed effects advance the existing step clock without relocating its origin.
    FixPoint dynastyStepTimeOffset = 0;
    FixPoint dynastyStartX = 0, dynastyStartY = 0;
    Coord dynastyEndpoint = Coord::Invalid();


    /**
        When the unit is currently idling this method is called about every 5 seconds.
    */
    virtual void idleAction();

    virtual void setSpeeds();
    /// A realized movement speed, limited to the formation's speed cap while that applies.
    FixPoint applyFormationSpeedCap(FixPoint speed) const;
    FixPoint getTerrainAdjustedSpeed(int cargoPercent = 0) const;

    virtual void targeting();
    void enqueueTargetRequest(TargetRequestKind kind);
    void enqueuePathRequest();

    virtual void turn();
    void turnLeft();
    void turnRight();

    void quitDeviation();

    /**
        Stamps the stall clock whenever the unit actually got somewhere or was given a different
        job. Polled once per update instead of hooked into every order path, so a player command,
        an AI order, a deviation revert and an internal retarget are all covered without any
        caller having to remember to call it.

        Intent is compared as "which target" first and "which destination" only when there is no
        target: engageTarget() rewrites destination every time a chased target moves, and that is
        the same job, not a new one. A changed target id is a new job.
    */
    void updateStallTracking();

    /// Does this unit still want to be somewhere other than where it is?
    bool hasMovementIntent() const;

    /// Restamps the stall clock at the current cycle and tile.
    void stampStallProgress();

    /// Asks for a carryall once the long-stall threshold is reached, subject to the throttle.
    void tryLongStallCarryallRescue();

    bool SearchPathWithAStar(size_t& nodesExpanded, bool& invalidDestination);
    bool isCachedPathStillValid();
    void updateCachedPathMetadata(const Coord& destinationCoord);
    /// Records an explicit observed revision, for a route planned across updates.
    void updateCachedPathMetadata(const Coord& destinationCoord, Uint32 revision);
    Coord resolvePathDestination() const;

    void drawSmoke(int x, int y) const;
    bool drawEnhancedUnitSprite(int x, int y, int idleDirection = -1,
                                int combatDirection = -1);

    // constant for all units of the same type
    bool     tracked;                ///< Does this unit have tracks?
    bool     turreted;               ///< Does this unit have a turret?
    int      numWeapons;             ///< How many weapons do we have?
    int      bulletType;             ///< Type of bullet to shot with
    int      lastFiredBulletType;    ///< Bullet actually fired, including distance-based weapon changes
    int      slowTimer = 0;

    // unit state/properties
    int      productionHouseID = HOUSE_INVALID;
    Coord    guardPoint;             ///< The guard point where to return to after the micro-AI hunted some nearby enemy unit
    Coord    attackPos;              ///< The position to attack
    bool     goingToRepairYard;      ///< Are we currently going to a repair yard?
    bool     pickedUp;               ///< Were we picked up by a carryall?
    bool     bFollow;                ///< Do we currently follow some other unit (specified by target)?
    Coord    huntTowardGoal;         ///< Where a doHuntToward() walk goes; invalid when none (SAVEGAMEVERSION 9854)
    Uint8    huntTowardRetries;      ///< Times that walk was resumed after the engine dropped it

    // Player order queue (SAVEGAMEVERSION 9855, docs/rts-controls.md). The front entry is the
    // order being executed. Kept together and saved as one block after huntToward.
    UnitOrderQueue orderQueue;                             ///< Shift-queued, attack-move and patrol orders
    bool     orderDispatched = false;                      ///< The front order has been handed to the engine
    Uint8    orderWalkRetries = 0;                         ///< Times a dropped attack-move walk was resumed
    Coord    attackMoveBreakOff = Coord::Invalid();        ///< Where an attack-move left its walk to fight

    /// Runs the order queue once per update: completes the front order and starts the next one.
    void updateOrderQueue();
    /// Hands the front order to the engine, skipping orders whose target is gone.
    void dispatchFrontOrder();
    /// Can the front order still be carried out (target alive, unit able)?
    bool isOrderExecutable(const UnitOrder& order) const;
    /// Has the engine run out of movement and targets for this unit?
    bool isIdleForOrders() const;
    /// The front order has finished: start the next one.
    void finishFrontOrder();
    // Formation membership, contiguous and saved together (SAVEGAMEVERSION 9857).
    Uint32   formationGroup = 0;                 ///< FormationPlanner::makeGroupID(); 0 = none
    Coord    formationSlot = Coord::Invalid();   ///< Destination in the formation; invalid = own order
    FixPoint formationSpeedCap = 0;              ///< The group's pace: slowest member's reference speed
    bool     formationHunt = false;              ///< A hunting formation: contact breaks the group (SAVEGAMEVERSION 9859)
    Uint8    formationFacing = 0;                ///< FormationPlanner::facingCode of the formation (SAVEGAMEVERSION 9859)
    Coord    formationFormUp = Coord::Invalid(); ///< Form-up tile while forming up; invalid = marching (SAVEGAMEVERSION 9860)
    Uint8    formationResume = 0;                ///< FormationOrders::Resume: the order towards the slot (SAVEGAMEVERSION 9860)
    Uint32   formationPaceItem = ItemID_Invalid; ///< The pace unit's type: the slowest member (SAVEGAMEVERSION 9860)
    FormationPlanner::March formationMarch = FormationPlanner::March::Pace;  ///< Transient: this cycle's march, recomputed every cycle
    bool     formationPending = false;           ///< Transient: joined this cycle, form-up not planned yet (never across a save)


    bool     moving;                 ///< Are we currently moving?
    bool     turning;                ///< Are we currently turning?
    bool     justStoppedMoving;      ///< Do we have just stopped moving?
    FixPoint xSpeed;                 ///< Speed in x direction
    FixPoint ySpeed;                 ///< Speed in y direction
    FixPoint bumpyOffsetX;           ///< The bumpy offset in x direction which is already included in realX
    FixPoint bumpyOffsetY;           ///< The bumpy offset in y direction which is already included in realY

    FixPoint targetDistance;         ///< Distance to the destination
    Sint8    targetAngle;            ///< Angle to the destination

    // path finding
    Uint8    noCloserPointCount;     ///< How often have we tried to dinf a path?
    bool     nextSpotFound;          ///< Is the next spot to move to already found?
    Sint8    nextSpotAngle;          ///< The angle to get to the next spot
    Sint32   recalculatePathTimer;   ///< This timer is for recalculating the best path after x ticks
    Coord    nextSpot;               ///< The next spot to move to
    std::list<Coord> pathList;       ///< The path to the destination found so far
    TargetRequestKind pendingTargetRequest = TargetRequestKind::None;
    bool pathRequestQueued = false;
    Coord    cachedPathDestination = Coord::Invalid(); ///< Destination associated with the current cached path
    Uint32   cachedPathRevision = 0;                   ///< Map revision used to validate the cached path
    
    // Stuck detection (transient - not saved)
    FixPoint lastDistanceToDestination = -1;  ///< Distance to destination on last pathfinding attempt
    Uint8    noProgressCount = 0;             ///< Attempts without getting closer
    Sint32   carryallRequestCooldown = 0;     ///< Cooldown timer to prevent spam requests

    // Long-stall rescue tracking. Transient on an ordinary save, exactly like the pathfinding
    // continuation state above: an ordinary load grants every unit a fresh thirty-second grace
    // period rather than changing the save format. Carried in the observer runtime stream
    // (version 7) so a spectator continues the same clocks the host is already running.
    Uint32   lastProgressCycle = 0;                    ///< Cycle of the last tile change or new job
    Coord    stallAnchor = Coord::Invalid();           ///< Tile observed when the clock was stamped
    Uint32   stallIntentTarget = NONE_ID;              ///< Target that defined the current job
    Coord    stallIntentDestination = Coord::Invalid(); ///< Destination that defined the current job

    Sint32  findTargetTimer;         ///< When to look for the next target?
    Sint32  primaryWeaponTimer;      ///< When can the primary weapon shot again?
    Sint32  secondaryWeaponTimer;    ///< When can the secondary weapon shot again?

    // deviation
    Sint32          deviationTimer;  ///< When to revert back to the original owner?
    /// Who is credited for what this unit does while borrowed (SAVEGAMEVERSION 9849).
    DeviationReward::Episode deviationEpisode;

    // drawing information
    int drawnFrame;                  ///< Which row in the picture should be drawn
    Uint32 enhancedCombatAnimationStartMs = std::numeric_limits<Uint32>::max();
    int enhancedRenderInteractionKey = -1;
    Uint32 enhancedRenderInteractionSequence = 0;
    bool enhancedRenderInteractionUsesFull = true;
};

#endif //UNITBASE_H
