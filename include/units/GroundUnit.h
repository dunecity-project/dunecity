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
 *InputStream
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GROUNDUNIT_H
#define GROUNDUNIT_H

#include <units/UnitBase.h>

class Carryall;

class GroundUnit : public UnitBase
{

public:

    explicit GroundUnit(House* newOwner);
    explicit GroundUnit(InputStream& stream);
    void init();
    virtual ~GroundUnit();

    GroundUnit(const GroundUnit &) = delete;
    GroundUnit(GroundUnit &&) = delete;
    GroundUnit& operator=(const GroundUnit &) = delete;
    GroundUnit& operator=(GroundUnit &&) = delete;

    void save(OutputStream& stream) const override;

    void assignToMap(const Coord& pos) override;

    void playConfirmSound() override;
    void playSelectSound() override;

    void bookCarrier(UnitBase* newCarrier);
    void checkPos() override;

    void doRequestCarryallDrop(int x, int y);

    /**
        Explicit transport intent: the player's carryall-drop order, a repair trip, a harvester
        that wants a lift, and the re-booking retry in checkPos(). Puts the unit into
        CARRYALLREQUESTED whether or not a carrier was free, which is what makes it keep asking,
        and is deliberately never blocked by the automatic-rescue cooldown.
        \return true if a carrier was booked
    */
    bool requestCarryall();

    /**
        The automatic long-stall rescue. Distinguished from requestCarryall() because it must not
        change what the unit is doing when no carrier is free: a unit that cannot be collected
        keeps its own attack mode, keeps navigating and keeps fighting, instead of being parked in
        CARRYALLREQUESTED waiting for a flight that is not coming.
        \return true if a carrier was booked
    */
    bool requestCarryallRescue();

    void cancelCarryallPickup() override;
    void setPickedUp(UnitBase* newCarrier) override;

    using UnitBase::doMove2Pos;
    void doMove2Pos(int xPos, int yPos, bool bForced) override;
    void doMove2Object(const ObjectBase* pTargetObject) override;

    /**
        This method is called when the user clicks on the repair button for this unit
    */
    virtual void handleSendToRepairClick();

    void doRepair() override;

    inline void setAwaitingPickup(bool status) { awaitingPickup = status; }
    inline bool isAwaitingPickup() const { return awaitingPickup; }
    bool hasBookedCarrier() const;
    const UnitBase* getCarrier() const;

    /**
        Returns how fast a unit can move over the specified terrain type.
        \param  terrainType the type to consider
        \return Returns a speed factor. Higher values mean slower.
    */
    FixPoint getTerrainDifficulty(TERRAINTYPE terrainType) const override
    {
        switch(terrainType) {
            case Terrain_Slab:          return 1.0_fix;
            case Terrain_Sand:          return 1.375_fix;
            case Terrain_Rock:          return 1.5625_fix;
            case Terrain_Dunes:         return 1.375_fix;
            case Terrain_Mountain:      return 1.0_fix;
            case Terrain_Spice:         return 1.375_fix;
            case Terrain_ThickSpice:    return 1.375_fix;
            case Terrain_GreenSpice:    return 1.375_fix;
            case Terrain_ThickGreenSpice: return 1.375_fix;
            case Terrain_RedSpice:      return 1.375_fix;
            case Terrain_ThickRedSpice: return 1.375_fix;
            case Terrain_SpiceBloom:    return 1.375_fix;
            case Terrain_GreenSpiceBloom: return 1.375_fix;
            case Terrain_RedSpiceBloom: return 1.375_fix;
            case Terrain_SpecialBloom:  return 1.375_fix;
            default:                    return 1.0_fix;
        }
    }

protected:
    void move() override;
    void navigate() override;

    /**
        The first carrier of ours that could collect this unit right now, or nullptr.

        Scans Game's derived carryall index rather than the whole unit list, and revalidates every
        candidate against the live object: still present, still a carryall, ours, active, alive,
        on the map, unbooked, and a carrier we actually own rather than a delivery flight that is
        about to leave the map and take the booking with it.

        "First" is unitList order, which is the selection the explicit request has always made.
    */
    Carryall* findFreeCarrier() const;

    /// Arms the automatic-rescue throttle, jittered by object id so a stalled army does not
    /// rescan on one cycle. Deterministic: no clock and no randomness.
    void armCarryallRequestThrottle();

    bool    awaitingPickup;     ///< Is this unit waiting for pickup?
    Uint32  bookedCarrier;      ///< What is the carrier if waiting for pickup?
};

#endif // GROUNDUNIT_H
