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

#ifndef SABOTEUR_H
#define SABOTEUR_H

#include <units/InfantryBase.h>

class StructureBase;

class Saboteur final : public InfantryBase
{
public:
    explicit Saboteur(House* newOwner);
    explicit Saboteur(InputStream& stream);
    void init();
    virtual ~Saboteur();

    void checkPos() override;

    /**
        Updates this saboteur.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    void deploy(const Coord& newLocation) override;
    bool canAttack(const ObjectBase* object) const override;

    /**
        A Saboteur's own search. Hunting, it takes the enemy building with the highest
        sabotageValue() / distance anywhere its house has seen, as Dune II's Saboteur does
        (bestBuilding()), unless an enemy vehicle within kVehicleReach tiles scores higher
        (vehicleValue() / distance). With no such building, or outside Hunt, it takes the
        nearest thing it may blow up: a vehicle (a ground unit that is neither infantry nor a sandworm, so
        harvesters and MCVs count and aircraft do not), walls and parks last. The pick is
        remembered so that only it is replaced on the way. An explicit order still sends it at
        anything canAttack() accepts.
        \return the target to sabotage, or nullptr
    */
    const ObjectBase* findTarget() const override;

    /// Dune II's worth of a building of this type to a Saboteur (priorityBuild + priorityTarget);
    /// 0 for walls and parks, which only the general search takes.
    static int sabotageValue(int itemID);

    /// Dune II's worth of a vehicle as a target (priorityBuild + priorityTarget), on the same
    /// scale as sabotageValue().
    static int vehicleValue(int itemID);

    void destroy() override;

    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;

    /// The match rule that gives Saboteurs their own hunt (GameOptionsClass::saboteurHunt).
    static bool keepsOwnHunt();

    /**
        An AI player's army order addressed to this Saboteur (Player::do*; a human's orders are
        commands and never come this way). With the match rule on, a Saboteur that is hunting
        keeps its hunt until it has blown something up: the order is refused. Every order is
        counted and the first few are logged with the army code that gave them.
        \param  order   the kind of order, e.g. "move" or "attack_mode"
        \param  caller  the function that gave it
        \return true if the order must not be applied
    */
    bool armyOrder(const char* order, const char* caller);

    /// Called by the Palace that released it: the match log counts from here.
    void markReleased();

protected:
    /// With the match rule on, a Saboteur that is destroyed without having set its charge off
    /// falls like any foot soldier; with the rule off it blows up however it ends, as before.
    bool fallsWhenDestroyed() const override;

    /// A Saboteur that set its charge off is spent, not lost: it counts as a death in the
    /// house's statistics only when it was destroyed without having sabotaged anything.
    bool lostWhenDestroyed() const override { return !detonated; }

private:
    void reconsiderOwnPick();
    const StructureBase* bestBuilding() const;
    const ObjectBase* bestVehicleNearby() const;
    FixPoint sabotageScore(const ObjectBase* object) const;

    static constexpr int kVehicleReach = 5;       ///< tiles within which a vehicle competes with buildings
    static constexpr int kReconsiderCycles = 16;  ///< how often its own pick is weighed again
    void showTargetToOwner(const Coord& targetTile);

    bool detonated = false;     ///< Set as it blows up its target; never saved, it is gone that cycle.
    /// The target its own search last chose; a different target is its owner's order. Never
    /// saved: after a load the target it holds counts as an order until it picks again.
    mutable Uint32 ownPick = NONE_ID;

    // Match-log bookkeeping (SABOTEUR_* lines). Never saved: a loaded Saboteur counts from zero.
    Uint32 releasedCycle = 0;
    Sint32 loggedMode = HUNT;   ///< attack mode last written to the match log
    Uint16 modeChanges = 0;
    Uint32 loggedTarget = NONE_ID;  ///< target last written to the match log
    Uint16 targetChanges = 0;
    Uint16 armyOrders = 0;      ///< AI army orders addressed to it
    Uint16 armyOverwrites = 0;  ///< of those, applied while it had a target or was walking
    Uint16 armyRefused = 0;     ///< of those, refused
    Sint32 lastDamagerItem = -1;
    Sint32 lastDamagerHouse = -1;
    const char* lastDamagerKind = "none";
};

#endif // SABOTEUR_H
