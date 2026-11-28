/*
 *  CityEffectsRuntime_test_stub.cpp
 *
 *  Empty replacements for CitySimulation::runEffectsScans / runZoneGrowth
 *  in the unit-test build. The real implementations live in
 *  src/dunecity/CityEffectsRuntime.cpp and link against Map / Tile /
 *  ZoneStructure / House — heavy dependencies the test target deliberately
 *  doesn't include. Tests don't exercise the runtime scans (those are
 *  integration-tier behavior verified in-game), so empty bodies are correct.
 */

#include <dunecity/CitySimulation.h>

namespace DuneCity {

void CitySimulation::runEffectsScans()    {}
void CitySimulation::runZoneGrowth()      {}
void CitySimulation::runDailyBudget()     {}
void CitySimulation::decayGrowthRateMap() {}
void CitySimulation::reconcileLoadedMapState(uint32_t) {}
void CitySimulation::runHospitalCare()    {}

int CitySimulation::getResPop() const { return 0; }
int CitySimulation::getComPop() const { return 0; }
int CitySimulation::getIndPop() const { return 0; }
int16_t CitySimulation::getResValve() const { return 0; }
int16_t CitySimulation::getComValve() const { return 0; }
int16_t CitySimulation::getIndValve() const { return 0; }
void CitySimulation::setPoliceFundingPercent(int) {}

}  // namespace DuneCity
