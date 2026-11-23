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
    /// City Plane Purchase: this Airport may launch a plane for CityAircraft::kPlanePrice now
    /// (rule on, city game, powered, cooled down, the house below its plane limit and able to pay).
    bool canBuyAirplane() const;
    /// Pays and launches; false, and nothing changes, unless canBuyAirplane().
    bool buyAirplane();
    /// The local player's Buy button: CMD_AIRPORT_BUY_AIRPLANE.
    void handleBuyAirplaneClick();
    /// Game cycles until this Airport may sell its next plane.
    int getPurchaseCooldown() const { return purchaseCooldown; }

private:
    DuneCity::AirPatrolCycle patrol{getMaxSpawnTimer()};
    Sint32 purchaseCooldown = 0;   ///< Cycles since-purchase clock QuantBot paces itself by (saved from 9891)
    /// Puts a city airplane on the runway. Null when the unit could not be created.
    class AmbientAirplane* launchAirplane(bool bought);
    void init();
    /// Micropolis doAirport(): launch the city airplane or traffic helicopter.
    void updateCityAircraft();
    void updateStructureSpecificStuff() override;
};

#endif // AIRPORT_H
