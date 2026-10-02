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

    /**
        Updates this unit.
        \return true if this unit still exists, false if it was destroyed
    */
    bool update() override;

    virtual bool canPass(int xPos, int yPos) const;

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
    bool moveDynastyStep();
    virtual Coord movementEndpoint() const;
    // Clock units are 1/3000 second: a legacy cycle is48, a Dynasty tick50.
    // Persisted so saves and observer checkpoints continue mid-step exactly.
    Uint64 dynastyRouteTick = 0;
    Uint64 dynastyStepStart = 0, dynastyStepEnd = 0;
    FixPoint dynastyStartX = 0, dynastyStartY = 0;
    Coord dynastyEndpoint = Coord::Invalid();


    /**
        When the unit is currently idling this method is called about every 5 seconds.
    */
    virtual void idleAction();

    virtual void setSpeeds();
    FixPoint getTerrainAdjustedSpeed(int cargoPercent = 0) const;

    virtual void targeting();
    void enqueueTargetRequest(TargetRequestKind kind);
    void enqueuePathRequest();

    virtual void turn();
    void turnLeft();
    void turnRight();

    void quitDeviation();

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

    // unit state/properties
    Coord    guardPoint;             ///< The guard point where to return to after the micro-AI hunted some nearby enemy unit
    Coord    attackPos;              ///< The position to attack
    bool     goingToRepairYard;      ///< Are we currently going to a repair yard?
    bool     pickedUp;               ///< Were we picked up by a carryall?
    bool     bFollow;                ///< Do we currently follow some other unit (specified by target)?


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
