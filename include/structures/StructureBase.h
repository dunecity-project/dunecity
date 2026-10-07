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

#ifndef STRUCTUREBASE_H
#define STRUCTUREBASE_H

#include <ObjectBase.h>

#include <list>
#include <limits>

class TrackedUnit;
class UnitBase;
class Carryall;

struct StructureSmoke {
    StructureSmoke(const Coord& pos, Uint32 gameCycle)
     : realPos(pos), startGameCycle(gameCycle) {
    }

    explicit StructureSmoke(InputStream& stream) {
        realPos.x = stream.readSint32();
        realPos.y = stream.readSint32();
        startGameCycle = stream.readUint32();
    }

    void save(OutputStream& stream) const {
        stream.writeSint32(realPos.x);
        stream.writeSint32(realPos.y);
        stream.writeUint32(startGameCycle);
    }

    Coord   realPos;
    Uint32  startGameCycle;
};

class StructureBase : public ObjectBase
{
public:
    explicit StructureBase(House* newOwner);
    explicit StructureBase(InputStream& stream);
    virtual ~StructureBase();

    StructureBase(const StructureBase &) = delete;
    StructureBase(StructureBase &&) = delete;
    StructureBase& operator=(const StructureBase &) = delete;
    StructureBase& operator=(StructureBase &&) = delete;

    void save(OutputStream& stream) const override;

    void assignToMap(const Coord& pos) override;
    void blitToScreen() override;

    ObjectInterface* getInterfaceContainer() override;

    void destroy() override;
    virtual void demolish();
    // Roll back a temporary structure that never occupied the map.
    void cancelPlacement() { demolishedByOwner_ = true; }
    void drawSelectionBox() override;
    void drawOtherPlayerSelectionBox() override;
    virtual void drawGatheringPointLine();

    Coord getCenterPoint() const override;
    Coord getClosestCenterPoint(const Coord& objectLocation) const override;
    void setDestination(int newX, int newY) override;
    void setJustPlaced();
    void setFogged(bool bFogged) { fogged = bFogged; }

    void playConfirmSound() override { }
    void playSelectSound() override { }

    /**
        This method is called when a structure is ordered by a right click
        \param  xPos    the x position on the map
        \param  yPos    the y position on the map
    */
    void handleActionClick(int xPos, int yPos) override;

    /**
        This method is called when the user clicks on the repair button for this building
    */
    virtual void handleRepairClick();

    /**
        Set the deploy position of this structure. Units produced in this structure will move directly to
        this position x,y after being deployed.
        \param  x           the x coordinate (in tile coordinates)
        \param  y           the y coordinate (in tile coordinates)
    */
    virtual void doSetDeployPosition(int xPos, int yPos);

    /**
        Start repairing this structure.
    */
    void doRepair() override;

    /**
        Updates this object.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    /**
        Can this structure be captured by infantry units?
        \return true, if this structure can be captured, false otherwise
    */
    virtual bool canBeCaptured() const { return true; }

    // Harvester drop-off interface. Refinery and Worfinery may both
    // store a harvester while extracting its spice progressively.
    virtual bool acceptsHarvesterDropoff() const { return false; }
    virtual bool isHarvesterDropoffFree() const { return false; }
    virtual int getHarvesterDropoffBookings() const { return 0; }
    virtual void bookHarvesterDropoff() { }
    virtual void unbookHarvesterDropoff() { }
    virtual void startHarvesterDropoffAnimation() { }
    // Returns true when the unit is stored inside the structure.
    virtual bool receiveHarvester(TrackedUnit*) { return false; }
    virtual void deployContainedHarvester(Carryall* = nullptr) { }
    virtual UnitBase* getContainedHarvesterUnit() { return nullptr; }
    virtual const UnitBase* getContainedHarvesterUnit() const { return nullptr; }

    /**
        The vehicle stored inside this structure's repair bay, or nullptr.

        Only a RepairYard has one. Together with getContainedHarvesterUnit() and the carriers'
        cargo lists this is what Game's containment reconciliation treats as authoritative: a unit
        hidden off the map belongs to one of these containers or to nothing at all.
    */
    virtual const UnitBase* getContainedRepairUnit() const { return nullptr; }

    bool isRepairing() const { return repairing; }

    Coord getClosestPoint(const Coord& objectLocation) const override;

    inline short getStructureSizeX() const { return structureSize.x; }
    inline short getStructureSizeY() const { return structureSize.y; }
    inline const Coord& getStructureSize() const { return structureSize; }

    inline void addSmoke(const Coord& pos, Uint32 gameCycle) {
        for(auto iter = smoke.begin(); iter != smoke.end(); ++iter) {
            if(iter->realPos == pos) {
                iter->startGameCycle = gameCycle;
                return;
            } else if(iter->realPos.y > pos.y) {
                smoke.insert(iter, StructureSmoke(pos, gameCycle));
            }
        }

        smoke.emplace_back(pos, gameCycle);
    }
    inline size_t getNumSmoke() const { return smoke.size(); }

    /// City sim "occupancy" (0..maxLevel for this structure type). Acts as
    /// the analogue of a zone's tile density for non-zone city-role
    /// structures (Refinery, Silo, Radar, Factories, RepairYard, IX,
    /// HighTech). The city sim floors these at level 1 once the structure
    /// exists (CityEffectsRuntime.cpp) so player-built supply doesn't sit
    /// at zero jobs; higher tiers are still reached via the usual demand
    /// gates. The raw stored value can be 0 (newly built / loaded from an
    /// older save) without affecting jobs.
    uint8_t getCityOccupancy() const { return cityOccupancy_; }
    void setCityOccupancy(uint8_t v) { cityOccupancy_ = v; }

protected:
    bool demolishedByOwner_ = false; // transient removal cause; never survives removal
    /**
        Used for updating things that are specific to that particular structure. Is called from
        StructureBase::update() before the check if this structure is still alive.
    */
    virtual void updateStructureSpecificStuff() { }


    // constant for all structures of the same type
    Coord   structureSize;      ///< The size of this structure in tile coordinates (e.g. (3,2) for a refinery)

    // structure state
    bool    repairing;          ///< currently repairing?

    // Dynasty-aligned degradation (see include/dunecity/StructureDegradation.h).
    // The two mechanisms are independent: power damage always runs, foundation
    // decay only for structures placed on an incomplete foundation.
    int     powerDamageTimer;       ///< cycles until the next power-shortage hitpoint is applied
    int     foundationDecayTimer;   ///< cycles until the next foundation-decay tick
    bool    foundationDegrades;     ///< placed on an incomplete foundation (Dynasty o.flags.s.degrades)

    // TODO: fogging is currently broken (fogged and lastVisibleFrame differ in multiplayer between players; hidden building disappear when being destroyed)
    bool        fogged;             ///< Currently fogged?
    int         lastVisibleFrame;   ///< store picture drawn before fogged

    // drawing information
    int     justPlacedTimer;          ///< When the structure is justed placed, we draw some special graphic
    std::list<StructureSmoke> smoke;  ///< A vector containing all the smoke for this structure

    int     firstAnimFrame;     ///< First frame of the current animation
    int     lastAnimFrame;      ///< Last frame of the current animation
    int     curAnimFrame;       ///< The current frame of the current animation
    int     animationCounter;   ///< When to show the next animation frame?

    // Local Dune2R presentation timing. These values are intentionally not
    // serialized and never participate in simulation or multiplayer state.
    int     enhancedVisualState = -1;
    Uint32  enhancedVisualStateStartMs = 0;
    Uint32  enhancedPlacementStartMs = std::numeric_limits<Uint32>::max();

    uint8_t cityOccupancy_ = 0; ///< City sim level for non-zone city-role structures (0=vacant)

private:
    void init();

    /// Applies power-shortage damage and foundation decay, independently of each other.
    void updateDegradation();
};

#endif //STRUCTUREBASE_H
