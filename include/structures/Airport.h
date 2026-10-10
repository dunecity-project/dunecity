#ifndef AIRPORT_H
#define AIRPORT_H

#include <structures/StructureBase.h>
#include <dunecity/AirPatrolCycle.h>

/**
 * Airport — DuneCity economic building.
 *
 * 3x3 footprint, powered Micropolis animation. Provides commercial/transport boost to
 * the city economy. Maps to CityRole::Commercial so it generates jobs
 * and tax revenue, similar to SimCity Classic's airport zone.
 */
class Airport final : public StructureBase
{
public:
    explicit Airport(House* newOwner);
    explicit Airport(InputStream& stream);
    virtual ~Airport();
    void save(OutputStream& stream) const override;
    int getMaxSpawnTimer() const;
    int getSpawnTimer() const { return patrol.remainingCycles; }
    int getPendingAircraft() const { return patrol.pendingAircraft; }
    ObjectInterface* getInterfaceContainer() override;

private:
    DuneCity::AirPatrolCycle patrol{getMaxSpawnTimer()};
    void init();
    /// Micropolis doAirport(): launch the city airplane or traffic helicopter.
    void updateCityAircraft();
    void updateStructureSpecificStuff() override;
};

#endif // AIRPORT_H
