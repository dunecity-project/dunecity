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

#ifndef REFINERY_H
#define REFINERY_H

#include <structures/StructureBase.h>
#include <ObjectPointer.h>
#include <array>
#include <vector>

// forward declarations
class Harvester;
class TrackedUnit;
class UnitBase;
class Carryall;

class Refinery final : public StructureBase
{
public:
    explicit Refinery(House* newOwner, int newItemID = Structure_Refinery);
    explicit Refinery(InputStream& stream, int newItemID = Structure_Refinery);
    void init(int newItemID);
    virtual ~Refinery();

    void save(OutputStream& stream) const override;

    ObjectInterface* getInterfaceContainer() override;

    bool assignHarvester(TrackedUnit* newHarvester);
    void deployHarvester(Carryall* pCarryall = nullptr);
    void startAnimate();
    void stopAnimate();

    inline void book() {
        bookings++;
        startAnimate();
    }
    void unBook();
    bool isFree() const;
    int getNumBays() const;
    int getNumOccupiedBays() const;
    inline int getNumBookings() const { return bookings; }  //number of units goings there
    UnitBase* getContainedHarvester();
    const UnitBase* getContainedHarvester() const;
    UnitBase* getContainedHarvester(int bay);
    const UnitBase* getContainedHarvester(int bay) const;
    std::vector<UnitBase*> getContainedHarvesters();
    inline const Harvester* getHarvester() const { return reinterpret_cast<const Harvester*>(getContainedHarvester()); }
    inline Harvester* getHarvester() { return reinterpret_cast<Harvester*>(getContainedHarvester()); }

    bool acceptsHarvesterDropoff() const override { return true; }
    bool isHarvesterDropoffFree() const override { return isFree(); }
    int getHarvesterDropoffBookings() const override { return getNumBookings(); }
    void bookHarvesterDropoff() override { book(); }
    void unbookHarvesterDropoff() override { unBook(); }
    void startHarvesterDropoffAnimation() override { startAnimate(); }
    bool receiveHarvester(TrackedUnit* unit) override { return assignHarvester(unit); }
    void deployContainedHarvester(Carryall* carryall = nullptr) override { deployHarvester(carryall); }
    UnitBase* getContainedHarvesterUnit() override { return getContainedHarvester(); }
    const UnitBase* getContainedHarvesterUnit() const override { return getContainedHarvester(); }

protected:
    /**
        Used for updating things that are specific to that particular structure. Is called from
        StructureBase::update() before the check if this structure is still alive.
    */
    void updateStructureSpecificStuff() override;

private:

    void deployBay(int index, Carryall* carryall);
    void refreshAnimation();

    std::array<bool, 2> extractingSpice{}; ///< Each unloading bay runs independently.
    std::array<ObjectPointer, 2> harvesters;
    Uint32          bookings;           ///< How many bookings?

    bool    firstRun;       ///< On first deploy of a harvester we tell it to the user
};

#endif // REFINERY_H
