#ifndef POLICESTATION_H
#define POLICESTATION_H

#include <structures/StructureBase.h>

/**
 * Police Station — DuneCity city-mode building.
 *
 * 2x2 footprint, single graphic, no animation. Full-strength source of police
 * coverage in city mode: Barracks and WOR no longer reduce crime
 * (getPoliceCoverage(Structure_Barracks) returns 0). Coverage radius
 * and annual upkeep are defined in CityEffects.h; the per-tick stamp
 * happens in runEffectsScans alongside Gun/Rocket Turret partial coverage.
 *
 * Price (500) matches SimCity Classic's TOOL_POLICESTATION cost
 * (MicropolisCore/MicropolisEngine/src/tool.cpp gCostOf[4]).
 */
class PoliceStation final : public StructureBase
{
public:
    explicit PoliceStation(House* newOwner);
    explicit PoliceStation(InputStream& stream);
    virtual ~PoliceStation();

    void save(OutputStream& stream) const override;
    ObjectInterface* getInterfaceContainer() override;
    void handleSpawnClick();
    void doSpawnVehicles();
    /**
        True when the game's effective unit limit leaves no room for any member of the
        patrol. House::getMaxUnits() and the per-category policy behind it are the only
        authority here, so the sidebar status reports the limit the player selected and
        nothing else. A police-local count ceiling is not a unit limit.
    */
    bool isUnitLimitReached() const;
    /**
        True when a QuantBot controller of this house has exhausted its configured
        military-value target. That is an AI army policy, independent of the game's unit
        limit, and must never be reported as one.
    */
    bool isReinforcementBudgetReached() const;
    bool canSpawnVehicles() const { return spawnTimer <= 0 && getHealth() > 0
        && !isUnitLimitReached() && !isReinforcementBudgetReached(); }
    int getMaxSpawnTimer() const;
    int getPercentComplete() const { return spawnTimer * 100 / getMaxSpawnTimer(); }
protected:
    void updateStructureSpecificStuff() override;
private:
    Sint32 spawnTimer = getMaxSpawnTimer();
    void init();
};

#endif // POLICESTATION_H
