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

#ifndef WORFINERY_H
#define WORFINERY_H

#include <ObjectPointer.h>
#include <structures/BuilderBase.h>

class Carryall;
class TrackedUnit;
class UnitBase;

/// Worfinery (Tornie mod) — WOR + Refinery combo that produces Troopers
/// instead of (or in addition to) Harvesters. Spawns infantry via the
/// base game's trooper production path. Stats come from ObjectData.ini.
///
/// Per Tornie spec: this is a single building that combines the visual
/// identity of a WOR with the production role of a Refinery. Its two active
/// frames are expanded into the vanilla Refinery's 10-frame layout and use
/// the same idle, approach, loaded-state, and timing rules.
class Worfinery final : public BuilderBase
{
public:
    explicit Worfinery(House* newOwner);
    explicit Worfinery(InputStream& stream);
    void init();
    virtual ~Worfinery();

    void save(OutputStream& stream) const override;
    bool hasTameWormOffer() const { return tameWormOffer; }

    ObjectInterface* getInterfaceContainer() override;

    bool acceptsHarvesterDropoff() const override { return true; }
    bool isHarvesterDropoffFree() const override { return !extractingSpice; }
    int getHarvesterDropoffBookings() const override { return static_cast<int>(bookings); }
    void bookHarvesterDropoff() override;
    void unbookHarvesterDropoff() override;
    void startHarvesterDropoffAnimation() override;
    bool receiveHarvester(TrackedUnit* unit) override;
    void deployContainedHarvester(Carryall* carryall = nullptr) override;
    UnitBase* getContainedHarvesterUnit() override { return harvester.getUnitPointer(); }
    const UnitBase* getContainedHarvesterUnit() const override { return harvester.getUnitPointer(); }

protected:
    void updateStructureSpecificStuff() override;

private:
    void startAnimate();
    void stopAnimate();
    void assignHarvester(TrackedUnit* unit);
    void deployHarvester(Carryall* carryall = nullptr);

    bool extractingSpice = false;
    ObjectPointer harvester;
    Uint32 bookings = 0;
    bool firstRun = true;
    bool tameWormOffer = false; ///< Stable per-building Chaos offer; never rerolled when loading
};

#endif // WORFINERY_H
