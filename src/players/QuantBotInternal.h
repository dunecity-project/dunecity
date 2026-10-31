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

// Private to the QuantBot*.cpp translation units, which implement one class
// split by subsystem. Not part of the player interface; include it only there.

#ifndef QUANTBOTINTERNAL_H
#define QUANTBOTINTERNAL_H

#include <players/RockExpansionPolicy.h>
#include <players/McvDeployPolicy.h>
#include <players/QuantBotColonisationPolicy.h>
#include <dunecity/CityStructurePopulation.h>
#include <dunecity/ZonePower.h>
#include <players/LocalPointIndex.h>
#include <players/CityDistanceField.h>
#include <players/CityRoadRepairPolicy.h>
#include <players/UnitMixPolicy.h>
#include <players/CityServiceInvestmentPolicy.h>
#include <dunecity/PoliceCoveragePolicy.h>
#include <dunecity/VanillaEconomy.h>
#include <structures/AdvancedWindTrap.h>
#include <structures/Scoutpost.h>
#include <structures/ZoneStructure.h>
#include <players/RedevelopmentPolicy.h>
#include <structures/NuclearPlant.h>
#include <structures/WindTrap.h>
#include <players/TacticalSafetyPolicy.h>

#include <players/QuantBot.h>
#include <players/HumanPlayer.h>
#include <players/SimpleArmyPolicy.h>
#include <players/QuantBotConfig.h>
#include <cmath>

#include <Game.h>
#include <GameInitSettings.h>
#include <Map.h>
#include <sand.h>
#include <House.h>

#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <structures/ConstructionYard.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotFoundationPolicy.h>
#include <players/QuantBotPowerInvestmentPolicy.h>
#include <players/QuantBotCityPolicy.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/QuantBotSpendingPolicy.h>
#include <players/CityPlacementPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <players/AirStrikePolicy.h>
#include <structures/RepairYard.h>
#include <structures/Refinery.h>
#include <structures/Palace.h>
#include <units/UnitBase.h>
#include <units/GroundUnit.h>
#include <units/AirUnit.h>
#include <units/MCV.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/Saboteur.h>
#include <units/Devastator.h>

#include <vector>
#include <limits>
#include <units/Carryall.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>
#include <dunecity/SpatialFields.h>
#include <players/QuantBotSchedulePolicy.h>
#include <dunecity/TrafficSimulation.h>
#include <Command.h>
#include <CommandManager.h>
#include <mod/ModManager.h>

#include <algorithm>
#include <set>

// Helpers shared by more than one QuantBot subsystem file.
namespace QuantBotInternal {
inline bool ornithopterItem(Uint32 item) {
    return item == Unit_Ornithopter
        || (item == Unit_EliteOrnithopter && ModManager::instance().isTornieFamilyContentActive());
}
inline bool harvesterItem(Uint32 item) {
    return item == Unit_Harvester || ((item == Unit_CapacityHarvester || item == Unit_FastHarvester)
        && ModManager::instance().isTornieFamilyContentActive());
}
// Mobile combat units only; defined in QuantBotArmy.cpp.
bool mobileCombatItem(Uint32 item);
// Desired-overlap tier for city defence; defined in QuantBotPlacement.cpp.
int rocketCoverageTier(QuantBot::Difficulty difficulty);
// Road impact of a city lot; defined in QuantBotPlacement.cpp.
CityPlacementPolicy::RoadImpact cityRoadImpact(const Map& map, int x, int y, int w, int h, Uint32 item);
// Would a lot take away the last open side of a neighbouring zone? Defined in
// QuantBotPlacement.cpp.
bool wouldLandlockNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h,
                                   const std::vector<Uint32>* clearedZones = nullptr);
} // namespace QuantBotInternal

#endif // QUANTBOTINTERNAL_H
