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

#ifndef CARRYALL_H
#define CARRYALL_H

#include <units/AirUnit.h>

#include <list>

class Carryall : public AirUnit
{
public:
    explicit Carryall(House* newOwner, int unitItemID = Unit_Carryall);
    explicit Carryall(InputStream& stream, int unitItemID = Unit_Carryall);
    void init(int unitItemID);
    virtual ~Carryall();

    void checkPos() override;

    /**
        Updates this carryall.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    void deploy(const Coord& newLocation) override;

    void destroy() override;

    void deployUnit(Uint32 unitID);

    void giveCargo(UnitBase* newUnit);

    void save(OutputStream& stream) const override;

    void setTarget(const ObjectBase* newTarget) override;

    bool hasCargo() const {
        return !pickedUpUnitList.empty();
    }

    /**
        The units this carryall actually carries, in enrolment order.

        This list is the authoritative record of what is inside the aircraft: a passenger's own
        target or carrier booking can be rewritten by anything that happens during a pickup, the
        cargo list cannot. Read-only; Game's containment reconciliation uses it to tell a hidden
        passenger apart from a hidden unit no carrier holds.
    */
    const std::list<Uint32>& getCargoIds() const { return pickedUpUnitList; }

    /**
        Drops a cargo entry without deploying or destroying anything.

        For entries that cannot describe a passenger any more: an id that no longer resolves, or
        one a second carrier also claims. Ordinary transport never uses this - deployUnit() is
        what puts a passenger down. Leaves the carrier unbooked and back on patrol if that was
        its last entry, so phantom cargo cannot keep an aircraft reserved for a flight that can
        never finish.
    */
    void releaseCargoId(Uint32 unitID);

    /**
        Drops one repeated entry for \a unitID, keeping the first.

        A passenger listed twice would be deployed twice and counted twice. Removing every entry
        instead would leave the aircraft holding a unit it no longer lists, so the one real
        ownership claim survives this.
        \return true if a repeated entry was removed
    */
    bool releaseExtraCargoId(Uint32 unitID);

    inline void setOwned(bool b) { owned = b; }

    inline void setDropOfferer(bool status) {
        aDropOfferer = status;
    }

    inline bool isBooked() const { return (target || hasCargo()); }

    /**
        Is this a one-way delivery flight that vanishes once its cargo is down?

        Reinforcement and starting-harvester drops are created with setDropOfferer(true) and
        remove themselves as soon as they are empty and off the map (see Carryall::update). Between
        dropping their cargo and leaving they are unbooked and would otherwise look like an idle
        carrier, so the automatic rescue has to be able to tell them apart from the player's own
        fleet. Read-only; nothing here changes behaviour on its own.
    */
    inline bool isDropOfferer() const { return aDropOfferer; }

    /// False for a carryall that only exists to deliver something and is not part of the owner's
    /// fleet (House::createUnit callers set this alongside setDropOfferer).
    inline bool isOwnedCarrier() const { return owned; }

protected:
    void releaseTarget() override;
    void engageTarget() override;
    void pickupTarget();
    void targeting() override;
    virtual void turn() override;
    void move() override;
    FixPoint getDestinationAngle() const override;
    bool getFlightDestination(FixPoint& x, FixPoint& y) const;

    // unit state/properties
    std::list<Uint32>   pickedUpUnitList;   ///< What units does this carryall carry?

    bool     owned;              ///< Is this carryall owned or is it just here to drop something off

    bool     aDropOfferer;       ///< This carryall just drops some units and vanishes afterwards
    bool     droppedOffCargo;    ///< Is the cargo already dropped off?
};

#endif // CARRYALL_H
