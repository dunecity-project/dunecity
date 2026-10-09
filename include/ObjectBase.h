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

#ifndef OBJECTBASE_H
#define OBJECTBASE_H

#include <ObjectPointer.h>

#include <Definitions.h>
#include <DataTypes.h>
#include <SpatialGridHandle.h>
#include <fixmath/FixPoint.h>
#include <misc/SDL2pp.h>
#include <mmath.h>

#include <globals.h>
#include <players/DeviationReward.h>

#include <algorithm>
#include <bitset>

// forward declarations
class House;
class InputStream;
class OutputStream;
class ObjectInterface;
class Coord;
template<class WidgetData> class Container;

#define VIS_ALL -1

/*!
    Class from which all structure and unit classes are derived
*/
class ObjectBase
{
public:

    explicit ObjectBase(House* newOwner);
    explicit ObjectBase(InputStream& stream);
    virtual ~ObjectBase();

    ObjectBase(const ObjectBase &) = delete;
    ObjectBase(ObjectBase &&) = delete;
    ObjectBase& operator=(const ObjectBase &) = delete;
    ObjectBase& operator=(ObjectBase &&) = delete;

    virtual void save(OutputStream& stream) const;

    virtual ObjectInterface* getInterfaceContainer();

    /**
        Whether the sidebar may show this object's full, owner-specific panel (production
        queue, city/power/refinery figures, upgrade and special-weapon state) rather than the
        bare DefaultObjectInterface.

        One predicate for every structure and unit type, so the sixteen getInterfaceContainer()
        overrides cannot drift apart. True for the local player's own objects, in debug, and for
        an observer - which is the whole point of watching: every owner's real numbers, read
        from that owner. Showing a panel grants nothing; GameInterface disables the panel's
        inputs while observing and the detached view identity cannot be authorized for any
        command. Ownership is never transferred to make a panel appear.
    */
    bool mayShowOwnerInterface() const;

    virtual void assignToMap(const Coord& pos) = 0;
    virtual void blitToScreen() = 0;

    virtual void drawSelectionBox() { ; };
    virtual void drawOtherPlayerSelectionBox() { ; };

    virtual void destroy() = 0;

    virtual Coord getClosestCenterPoint(const Coord& objectLocation) const;

    virtual bool canAttack(const ObjectBase* object) const;

    virtual Coord getCenterPoint() const;

    /**
        Applies damage to this object and books the resulting combat reward.
        \param  damage        hit points to remove (negative values only run the side effects)
        \param  damagerID     the object that caused it, may already be gone
        \param  damagerOwner  the house that caused it
        \param  provenance    who to credit, snapshotted when the shot was fired. An unknown
                              provenance means "decide now from the live damager".
    */
    virtual void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                              const DeviationReward::Provenance& provenance = DeviationReward::Provenance());

    /**
        This method is called when an object is ordered by a right click
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    virtual void handleActionClick(int xPos, int yPos) = 0;

    virtual void doRepair() = 0;

    virtual void handleInterfaceEvent(SDL_Event* event);

    virtual void playSelectSound() = 0;
    virtual void playConfirmSound() = 0;

    void removeFromSelectionLists();

    virtual void setDestination(int newX, int newY);
    virtual void setHealth(FixPoint newHealth);

    virtual void setLocation(int xPos, int yPos);

    void setObjectID(int newObjectID);

    virtual void setTarget(const ObjectBase* newTarget);
    void setVisible(int teamID, bool status);

    /**
        Updates this object.
        \return true if this object still exists, false if it was destroyed
    */
    virtual bool update() = 0;

    void unassignFromMap(const Coord& location) const;

    bool isOnScreen() const;
    bool isVisible(int teamID) const;
    bool isVisible() const;
    Uint32 getHealthColor() const;

    /**
        This method returns the closest coordinate of this object to objectLocation. If this is a building
        it returns the coordinate of the tile of the building that is closest to this object.
        \param objectLocation the location of the other object
        \return the coordinate that is closest in tile coordinates
    */
    virtual Coord getClosestPoint(const Coord& objectLocation) const;

    const StructureBase* findClosestTargetStructure() const;
    const UnitBase* findClosestTargetUnit() const;
    const ObjectBase* findClosestTarget() const;
    virtual const ObjectBase* findTarget() const;

    /**
        Target-search workload counters. Process-wide diagnostics only: they are
        never saved, never sent over the network and never read by the
        simulation, so they cannot affect determinism. Probes reset and assert
        on them to prove the ring walk stays bounded, the way
        Map::terrainConnectivityBuilds proves the connectivity cache does.
    */
    struct TargetSearchStats {
        Uint64 searches = 0;         ///< findTarget()/findClosestTarget() entries.
        Uint64 cellsVisited = 0;     ///< Grid cells or map tiles the ring walk inspected.
        Uint64 candidatesTested = 0; ///< Objects that reached the per-candidate filters.
    };
    static TargetSearchStats targetSearchStats;

    /**
        One repair-yard healing step. setHealth() rejects any value above the maximum, so a saved
        occupant with a fractional residual (the stalled game held bays at 199.875 of 200) would
        make every further step a silent no-op and never leave its bay. Clamp the healed value
        here instead of relying on the setter; whole-point healing and its cost are unchanged.
    */
    inline void addHealth() {
        const FixPoint maximum = FixPoint(getMaxHealth());
        if (health < maximum) {
            const FixPoint healed = health + 1;
            setHealth(healed > maximum ? maximum : healed);
        }
    }
    inline void setActive(bool status) { active = status; }
    inline void setForced(bool status) { forced = status; }
    inline void setRespondable(bool status) { respondable = status; }
    inline void setSelected(bool value) { selected = value; }
    inline void setSelectedByOtherPlayer(bool value) { selectedByOtherPlayer = value; }
    inline void setDestination(const Coord& location) { setDestination(location.x, location.y); }
    inline void setLocation(const Coord& location) { setLocation(location.x, location.y); }
    inline bool canAttack() const { return canAttackStuff; }
    virtual bool canHeal() const { return false; }
    inline bool hasATarget() const { return (target); }
    inline bool hasObjectID(Uint32 id) const { return (objectID == id); }
    inline bool isActive() const { return active; }
    inline bool isAFlyingUnit() const { return aFlyingUnit; }
    inline bool isAGroundUnit() const { return aGroundUnit; }
    inline bool isAStructure() const { return aStructure; }
    inline bool isABuilder() const { return aBuilder; }
    inline bool isInfantry() const { return infantry; }
    inline bool isAUnit() const { return aUnit; }
    inline bool isRespondable() const { return respondable; }
    inline bool isByScenario() const { return byScenario; }
    inline bool isSelected() const { return selected; }
    inline bool isSelectedByOtherPlayer() const { return selectedByOtherPlayer; }
    inline bool isBadlyDamaged() const { return badlyDamaged; };
    inline bool wasForced() const { return forced; }
    inline int getItemID() const { return itemID; }
    inline int getX() const { return location.x; }
    inline int getY() const { return location.y; }

    inline FixPoint getHealth() const { return health; }
    int getMaxHealth() const;
    inline Uint32 getObjectID() const { return objectID; }


    int getViewRange() const;
    int getAreaGuardRange() const;
    int getWeaponRange() const;
    int getWeaponReloadTime() const;
    int getInfSpawnProp() const;

    inline FixPoint getRealX() const { return realX; }
    inline FixPoint getRealY() const { return realY; }
    inline const Coord& getLocation() const { return location; }
    inline const Coord& getDestination() const { return destination; }
    inline ObjectBase* getTarget() { return target.getObjPointer(); }
    inline const ObjectBase* getTarget() const { return target.getObjPointer(); }

    inline int getOriginalHouseID() const { return originalHouseID; }
    virtual void setOriginalHouseID(int i) { originalHouseID = i; }
    inline House* getOwner() { return owner; }
    inline const House* getOwner() const { return owner; }

    inline void setOwner(House* no) { owner = no; }
    inline SpatialGridHandle& getGridHandle() noexcept { return gridHandle; }
    inline const SpatialGridHandle& getGridHandle() const noexcept { return gridHandle; }

    static ObjectBase* createObject(int itemID, House* Owner, bool byScenario);
    static ObjectBase* loadObject(InputStream& stream, int itemID, Uint32 objectID);

protected:
    bool targetInWeaponRange() const;

    // constant for all objects of the same type
    Uint32   itemID;                 ///< The ItemID of this object.
    int      radius;                 ///< The radius of this object

    bool     aStructure;             ///< Is this a structure?
    bool     aBuilder;               ///< Is this a builder?

    bool     aUnit;                  ///< Is this a unit?
    bool     aFlyingUnit;            ///< Is this a flying unit?
    bool     aGroundUnit;            ///< Is this a ground unit?
    bool     infantry;               ///< Is this an infantry unit?

    bool     canAttackStuff;         ///< Can this unit/structure attack?

    // object state/properties
    Uint32   objectID;               ///< The unique object ID of this object
    int      originalHouseID;        ///< for takeover/deviation, we still want to keep track of what the original house was
    House    *owner;                 ///< The owner of this object

    Coord    location;               ///< The current position of this object in tile coordinates
    Coord    oldLocation;            ///< The previous position of this object in tile coordinates (used when moving from one tile to the next tile)
    Coord    destination;            ///< The destination tile
    FixPoint realX;                  ///< The x-coordinate of this object in world coordinates
    FixPoint realY;                  ///< The y-coordinate of this object in world coordinates

    FixPoint angle;                  ///< The current angle of this unit/structure (8 = 360°)
    Sint8    drawnAngle;             ///< The angle this unit/structure is drawn with. (e.g. 0 to 7)

    bool     active;                 ///< Is this unit/structure active?
    bool     respondable;            ///< Is this unit/structure respondable to commands?
    bool     byScenario;             ///< Did this unit/structure either already exist at the start of the map or is a reinforcement?
    bool     selected;               ///< Is this object currently selected?
    bool     selectedByOtherPlayer;  ///< This is only used in multiplayer games where two players control one house

    bool          forced;            ///< Is this unit/structure forced to do what it currently does or did the micro-AI decide to do that?
    bool          targetFriendly;    ///< Is the current target a friendly unit/structure to follow/move to instead to attack?
    ObjectPointer target;            ///< The target to attack or move to
    ATTACKMODE    attackMode;        ///< The attack mode of this unit/structure

    std::bitset<NUM_TEAM_SLOTS> visible;  ///< To which teams is this unit visible?

    // drawing information
    bool     badlyDamaged;           ///< Is the health below 50%?

    zoomable_texture graphic{};      ///< The graphic for this object
    int         graphicID = -1;      ///< The id of the graphic (needed if we want to reload the graphic, e.g. when a unit is deviated)
    int         numImagesX = 0;      ///< The number of images in x direction
    int         numImagesY = 0;      ///< The number of images in y direction

private:
    FixPoint health;                 ///< The health of this object
    void init();
    SpatialGridHandle gridHandle;
};



#endif // OBJECTBASE_H
