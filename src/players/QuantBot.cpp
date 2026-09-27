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
#include <units/Saboteur.h>
#include <units/Devastator.h>

#include <vector>
#include <limits>
#include <units/Carryall.h>

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>
#include <dunecity/TrafficSimulation.h>
#include <Command.h>
#include <CommandManager.h>

#include <algorithm>
#include <set>

#define AIUPDATEINTERVAL 50



 /**
  TODO

  New list from Dec 2016
  - Some harvesters getting 'stuck' by base when 100% full
  - rocket launchers are firing on units too close again...
  - unit rally points need to be adjusted for unit producers
  - add in writing of game log to a repository

  - fix game performance when toomany units


  New list from May 2016
  - units should move at start
  - fix single player campaign crash
  - fix unit allocation bug - atredes only building light tanks


  == Building Placement ==


  ia) build concrete when no placement locations are available == in progress, bugs exist ==
  iii) increase favourability of being near other buildings == 50% done ==

  1. Refinerys near spice == tried but failed ==
  4. Repair yards factories, & Turrets near enemy == 50% done ==
  5. All buildings away from enemy other that silos and turrets


  == buildings ==
  i) stop repair when just on yellow (at 50%) == 50% done, still broken for some buildings as goes into yellow health ==
  ii) silo build broken == fixed ==


  building algo still leaving gaps
  increase alignment score when sides match

  == Units ==
  ii) units that get stuck in buildings should be transported to squadcenter =%80=
  vii) fix attack timer =%80=
  viii) when attack timer exceeds a certain value then all fing units are set to area guard

  2) harvester return distance bug been introduced.= in progress ==

  3) carryalls sit over units hovering bug introduced.... fix scramble units and defend + manual carryall = 50% =

  4) theres a bug in on increment and decrement units...

  5) turn off force move to rally point after attacked = 50% =
  6) reduce turret building when lacking a military = 50% =

  7) remove turrets from nuke target calculation =50%=
  8) adjust turret placement algo to include points for proximitry to base centre =50%=



  1. Harvesters deploy away from enemy
  5. fix gun turret & gun for rocket turret

  x. Improve squad management

  == New work ==
  1. Add them with some logic =50%=
  2. fix force ratio optimisation algorithm,
  need to make it based off kill / death ratio instead of just losses =50%=
  3. create a retreate mechanism = 50% = still need to add retreat timer, say 1 retreat per minute, max
  - fix rally point and ybut deploy logic


  2. Make carryalls and ornithopers easier to hit

  ====> FIX WORM CRASH GAME BUG

  **/



QuantBot::QuantBot(House* associatedHouse, const std::string& playername, Difficulty difficulty, bool supportModeEnabled)
	: Player(associatedHouse, playername), difficulty(difficulty), supportMode(supportModeEnabled) {

	// MULTIPLAYER FIX: Use deterministic stagger based on house ID instead of random
	// This prevents desync issues in multiplayer games
	buildTimer = (getHouse()->getHouseID() % 4) * 50;  // 0-150 cycles stagger

    const QuantBotConfig& config = getQuantBotConfig();

    attackTimer = SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
        currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());

    retreatTimer = MILLI2CYCLES(60000); //turning off

	// Different AI logic for Campaign. Assumption is if player is loading they are playing a campaign game
	if ((isCampaignGameType(currentGame->gameType)) || (currentGame->gameType == GameType::LoadSavegame) || ((currentGame->gameType == GameType::Skirmish || currentGame->gameType == GameType::SkirmishCoop))) {
		gameMode = GameMode::Campaign;
	}
	else {
		gameMode = GameMode::Custom;
	}

	if (gameMode == GameMode::Campaign) {
		// Wait a while if it is a campaign game

		switch (currentGame->techLevel) {
		case 6: {
			attackTimer = MILLI2CYCLES(540000);
		}break;

		case 7: {
			attackTimer = MILLI2CYCLES(600000);
		}break;

		case 8: {
			attackTimer = MILLI2CYCLES(720000);
		}break;

		default: {
			attackTimer = MILLI2CYCLES(480000);
		}

		}
	}

	if (supportMode) {
		gameMode = GameMode::Custom;
		attackTimer = std::numeric_limits<Sint32>::max();
	}
}

std::string QuantBot::getDifficultyName() const {
    switch (difficulty) {
        case Difficulty::Easy: return "easy";
        case Difficulty::Medium: return "medium";
        case Difficulty::Hard: return "hard";
        case Difficulty::Brutal: return "brutal";
        case Difficulty::Defend: return "defend";
    }
    return "unknown";
}


QuantBot::QuantBot(InputStream& stream, House* associatedHouse) : Player(stream, associatedHouse) {
	QuantBot::init();

	difficulty = static_cast<Difficulty>(stream.readUint8());
	gameMode = static_cast<GameMode>(stream.readUint8());
	buildTimer = stream.readSint32();
	attackTimer = stream.readSint32();
	retreatTimer = stream.readSint32();

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		initialItemCount[i] = stream.readUint32();
	}
	initialMilitaryValue = stream.readSint32();
	militaryValueLimit = stream.readSint32();
	harvesterLimit = stream.readSint32();
	lastCalculatedSpice = stream.readSint32();
	campaignAIAttackFlag = stream.readBool();

	squadRallyLocation.x = stream.readSint32();
	squadRallyLocation.y = stream.readSint32();
	squadRetreatLocation.x = stream.readSint32();
	squadRetreatLocation.y = stream.readSint32();

	// Need to add in a building array for when people save and load
	// So that it keeps the count of buildings that should be on the map.
	Uint32 NumPlaceLocations = stream.readUint32();
	for (Uint32 i = 0; i < NumPlaceLocations; i++) {
		Sint32 x = stream.readSint32();
		Sint32 y = stream.readSint32();

        placeLocations.emplace_back(x, y);
    }

    try {
        supportMode = stream.readBool();
    } catch(const InputStream::eof&) {
        supportMode = false;
    } catch(const InputStream::error&) {
        supportMode = false;
    }

    if (currentGame->getLoadedSavegameVersion() >= 9827) {
        rallySelectedCycle = stream.readUint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9828) {
        nonServiceConstructionOrders = std::min<Uint32>(3, stream.readUint32());
    }
    if (currentGame->getLoadedSavegameVersion() >= 9829) {
        powerDemandSampleCycle = stream.readUint32();
        powerDemandSample = stream.readSint32();
        projectedPowerDemandGrowth = stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9830) {
        groundSquadPhase=stream.readUint32(); groundSquadStarted=stream.readUint32();
        groundSquadNextControl=stream.readUint32(); groundSquadInitialCount=stream.readUint32();
        groundSquadObjective=stream.readUint32(); groundSquadObjectiveCycle=stream.readUint32();
        const auto count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) groundSquad.insert(stream.readUint32());
        auto readMap=[&](auto& values) {
            const auto n=stream.readUint32();
            for (Uint32 i=0;i<n;++i) { const auto id=stream.readUint32(); const auto value=stream.readUint32(); values[id]=value; }
        };
        readMap(manualUnitOrders); readMap(defenceAssignments);
        const auto losses=stream.readUint32();
        for (Uint32 i=0;i<losses;++i) {
            RecentStructureLoss loss;
            loss.location.x=stream.readSint32(); loss.location.y=stream.readSint32();
            loss.size.x=stream.readSint32(); loss.size.y=stream.readSint32();
            loss.cycle=stream.readUint32(); loss.item=stream.readUint32(); recentStructureLosses.push_back(loss);
        }
        performanceHistory.load(stream);
    }
    // The measured Deviator ledger replaces the old flat conversion estimate, and House
    // resets the whole unit-mix learning ledger on the same load. A decayed window computed
    // from the old counters would keep the replaced accounting alive, so it starts empty too.
    if (currentGame->getLoadedSavegameVersion() < 9849) resetLearningForMeasuredScoring();
    if (currentGame->getLoadedSavegameVersion() >= 9831) {
        groundSquadProgressCycle=stream.readUint32();
        groundSquadProgressLocation.x=stream.readSint32();
        groundSquadProgressLocation.y=stream.readSint32();
    }
    if (currentGame->getLoadedSavegameVersion() >= 9832) {
        const Uint32 count=stream.readUint32();
        for (Uint32 i=0;i<count;++i) {
            const Uint32 key=stream.readUint32();
            defenceResponseCycles[key]=stream.readUint32();
        }
    }
    if (currentGame->getLoadedSavegameVersion() >= 9838) campaignWave.load(stream);
    if (currentGame->getLoadedSavegameVersion() >= 9839) {
        const auto count=stream.readUint32();
        for(Uint32 i=0;i<count;++i) scriptedAssaults.insert(stream.readUint32());
    }
    if (currentGame->getLoadedSavegameVersion() >= 9842) {
        campaignBaselineCaptured=stream.readBool();
        campaignBaseline.refineries=std::max(0,stream.readSint32());
        campaignBaseline.allowance=std::max(0,stream.readSint32());
        campaignMapHasSpice=stream.readBool();
        campaignSpiceZeroSince=stream.readUint32();
        const auto count=stream.readUint32();
        if(count>Num_ItemID) throw std::runtime_error("Invalid campaign building permissions");
        for(Uint32 i=0;i<count;++i) {
            const auto item=stream.readUint32();
            if(item>=Num_ItemID || !isStructure(item)) throw std::runtime_error("Invalid campaign structure");
            campaignOriginalStructures.insert(item);
        }
    }
    if (currentGame->getLoadedSavegameVersion() >= 9848) {
        openingDispersalUntil=stream.readUint32();
        const auto count=stream.readUint32();
        if(count>kOpeningDispersalLimit) throw std::runtime_error("Invalid opening dispersal state");
        for(Uint32 i=0;i<count;++i) {
            const auto id=stream.readUint32();
            Coord site;
            site.x=stream.readSint32();
            site.y=stream.readSint32();
            openingDispersal[id]=site;
        }
    }
    // Preserve an older save's initialized opening: fired triggers have already
    // been removed from TriggerManager, so rescanning could delay it forever.
    if (supportMode) {
        gameMode = GameMode::Custom;
        attackTimer = std::numeric_limits<Sint32>::max();
    }
}


void QuantBot::init() {
	// Load QuantBot configuration from file on first init
	// This will create the config file with defaults if it doesn't exist
	getQuantBotConfig();

	// Clear idle harvester counters (important for loading saved games)
    idleHarvesterCounters.clear();
    harvesterMovingCounters.clear();

	SDL_Log("QuantBot initialized with external configuration");
}


QuantBot::~QuantBot() = default;

void QuantBot::save(OutputStream& stream) const {
	Player::save(stream);

	stream.writeUint8(static_cast<Uint8>(difficulty));
	stream.writeUint8(static_cast<Uint8>(gameMode));
	stream.writeSint32(buildTimer);
	stream.writeSint32(attackTimer);
	stream.writeSint32(retreatTimer);

	for (Uint32 i = ItemID_FirstID; i <= Structure_LastID; i++) {
		stream.writeUint32(initialItemCount[i]);
	}
	stream.writeSint32(initialMilitaryValue);
	stream.writeSint32(militaryValueLimit);
	stream.writeSint32(harvesterLimit);
	stream.writeSint32(lastCalculatedSpice);
	stream.writeBool(campaignAIAttackFlag);

	stream.writeSint32(squadRallyLocation.x);
	stream.writeSint32(squadRallyLocation.y);
	stream.writeSint32(squadRetreatLocation.x);
	stream.writeSint32(squadRetreatLocation.y);

	stream.writeUint32(placeLocations.size());
    for (const Coord& placeLocation : placeLocations) {
        stream.writeSint32(placeLocation.x);
        stream.writeSint32(placeLocation.y);
    }

    stream.writeBool(supportMode);
    stream.writeUint32(rallySelectedCycle);
    stream.writeUint32(nonServiceConstructionOrders);
    stream.writeUint32(powerDemandSampleCycle);
    stream.writeSint32(powerDemandSample);
    stream.writeSint32(projectedPowerDemandGrowth);
    stream.writeUint32(groundSquadPhase); stream.writeUint32(groundSquadStarted);
    stream.writeUint32(groundSquadNextControl); stream.writeUint32(groundSquadInitialCount);
    stream.writeUint32(groundSquadObjective); stream.writeUint32(groundSquadObjectiveCycle);
    stream.writeUint32(static_cast<Uint32>(groundSquad.size()));
    for (const auto id:groundSquad) stream.writeUint32(id);
    auto writeMap=[&](const auto& values) {
        stream.writeUint32(static_cast<Uint32>(values.size()));
        for (const auto& entry:values) { stream.writeUint32(entry.first); stream.writeUint32(entry.second); }
    };
    writeMap(manualUnitOrders); writeMap(defenceAssignments);
    stream.writeUint32(static_cast<Uint32>(recentStructureLosses.size()));
    for (const auto& loss:recentStructureLosses) {
        stream.writeSint32(loss.location.x); stream.writeSint32(loss.location.y);
        stream.writeSint32(loss.size.x); stream.writeSint32(loss.size.y);
        stream.writeUint32(loss.cycle); stream.writeUint32(loss.item);
    }
    performanceHistory.save(stream);
    stream.writeUint32(groundSquadProgressCycle);
    stream.writeSint32(groundSquadProgressLocation.x);
    stream.writeSint32(groundSquadProgressLocation.y);
    writeMap(defenceResponseCycles);
    campaignWave.save(stream);
    stream.writeUint32(static_cast<Uint32>(scriptedAssaults.size()));
    for(auto id:scriptedAssaults) stream.writeUint32(id);
    stream.writeBool(campaignBaselineCaptured);
    stream.writeSint32(campaignBaseline.refineries);
    stream.writeSint32(campaignBaseline.allowance);
    stream.writeBool(campaignMapHasSpice);
    stream.writeUint32(campaignSpiceZeroSince);
    stream.writeUint32(static_cast<Uint32>(campaignOriginalStructures.size()));
    for(auto item:campaignOriginalStructures) stream.writeUint32(item);
    // The opening dispersal decides whether a starting unit is left where it
    // was stepped to or regrouped onto the rock the first buildings need, so a
    // save or network checkpoint taken during the opening window must carry
    // it. Serialized in sorted object-ID order: the map is unordered, and a
    // save has to be byte-identical on every client.
    stream.writeUint32(openingDispersalUntil);
    std::vector<Uint32> openingUnits;
    openingUnits.reserve(openingDispersal.size());
    for(const auto& entry:openingDispersal) openingUnits.push_back(entry.first);
    std::sort(openingUnits.begin(),openingUnits.end());
    stream.writeUint32(static_cast<Uint32>(openingUnits.size()));
    for(const auto id:openingUnits) {
        const Coord site=openingDispersal.at(id);
        stream.writeUint32(id);
        stream.writeSint32(site.x);
        stream.writeSint32(site.y);
    }
}


bool QuantBot::permitsPoliceReinforcement(int unitValue) const {
    if (difficulty != Difficulty::Brutal) return true;
    if (!currentGame || militaryValueLimit <= 0) return false;
    int value = 0;
    // Match the production allocator's military valuation, including new batch members.
    for (Uint32 item = Unit_FirstID; item <= Unit_LastID; ++item) {
        if (item != Unit_Carryall && item != Unit_Harvester && item != Unit_MCV && item != Unit_Sandworm)
            value += getHouse()->getNumItems(item)
                * currentGame->objectData.data[item][getHouse()->getHouseID()].price;
    }
    return value < militaryValueLimit && value + unitValue <= militaryValueLimit;
}

void QuantBot::update() {
	// Safety check: if our house is null (e.g., during game cleanup), don't update
	if (getHouse() == nullptr) {
		return;
	}

	if (!supportMode && getPlayerclass().rfind("qBotSupport", 0) == 0) {
		supportMode = true;
		gameMode = GameMode::Custom;
		attackTimer = std::numeric_limits<Sint32>::max();
	}

    // Both co-controllers and separate human-allied houses develop an economy;
    // only opponents use the campaign enemy rebuild/pressure restrictions.
    if (isAlliedWithHuman() && gameMode == GameMode::Campaign) {
        gameMode = GameMode::Custom;
        initialMilitaryValue = -1;
        const auto& config = getQuantBotConfig();
        attackTimer = supportMode ? std::numeric_limits<Sint32>::max()
            : isCampaignGameType(currentGame->gameType) ? 0
            : SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
                currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());
        logDebug("Human-allied house: using economy development instead of campaign enemy rebuild limits");
    }

    // Allies have no campaign opening grace. Discard a legacy opening timer
    // on load, but preserve the normal 60-second break between their attacks.
    if (!supportMode && difficulty != Difficulty::Defend && isAlliedWithHuman()
        && isCampaignGameType(currentGame->gameType) && attackTimer > MILLI2CYCLES(60000))
        attackTimer = 0;

    if (campaignCityEconomy() && !campaignBaselineCaptured && initialMilitaryValue>=0) noteCampaignOriginalState(true);

	if (initialMilitaryValue < 0) {
		// Run once after objects exist, including a new partner added to a
        // mid-mission save. Existing saved bots retain their initialized state.

		// First count all the objects we have
		for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
			initialItemCount[i] = getHouse()->getNumItems(i);
			logDebug("Initial: Item: %d  Count: %d", i, initialItemCount[i]);
		}

        if (campaignCityEconomy()) noteCampaignOriginalState();

		// Allow campaign controllers a repair-yard target, except Medium:
        // Medium preserves only the count actually present at mission start.
		// Note: supportMode sets gameMode to Custom, so check currentGame->gameType instead
		if (difficulty!=Difficulty::Medium && (initialItemCount[Structure_RepairYard] == 0) && currentGame && isCampaignGameType(currentGame->gameType) && currentGame->techLevel > 4) {
			initialItemCount[Structure_RepairYard] = 1;
			if (initialItemCount[Structure_Radar] == 0) {
				initialItemCount[Structure_Radar] = 1;
			}

			if (initialItemCount[Structure_LightFactory] == 0) {
				initialItemCount[Structure_LightFactory] = 1;
			}

			logDebug("Allow Campaign AI one Repair Yard (support: %s)", supportMode ? "yes" : "no");
		}

		// Calculate the total military value of the player
		initialMilitaryValue = 0;
		if (currentGame) {
			for (Uint32 i = Unit_FirstID; i <= Unit_LastID; i++) {
				if (i != Unit_Carryall
					&& i != Unit_Harvester
					&& i != Unit_MCV
					&& i != Unit_Sandworm) {
					// Used for campaign mode.
					initialMilitaryValue += initialItemCount[i] * currentGame->objectData.data[i][getHouse()->getHouseID()].price;
				}
			}
		}



	// Get config for this difficulty
	const QuantBotConfig& config = getQuantBotConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

	// Log which config this QuantBot is using
	logDebug("=== QuantBot [%s - %s] Initialization ===", 
		getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())).c_str(),
		gameMode == GameMode::Campaign ? "Campaign" : "Custom");

	switch (gameMode) {
	case GameMode::Campaign: {
		// Use config values for campaign mode
		harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
		militaryValueLimit = lround(initialMilitaryValue * diffSettings.militaryValueMultiplier);

		logDebug("  Difficulty: %s", 
			difficulty == Difficulty::Defend ? "Defend" :
			difficulty == Difficulty::Easy ? "Easy" :
			difficulty == Difficulty::Medium ? "Medium" :
			difficulty == Difficulty::Hard ? "Hard" : "Brutal");
		logDebug("  Mission: %d", currentGame ? currentGame->getGameInitSettings().getMission() : 0);
		logDebug("  Initial Military Value: %d", initialMilitaryValue);
		logDebug("  Initial Refineries: %d", initialItemCount[Structure_Refinery]);
		logDebug("  Config: HarvesterMult=%d, MilitaryMult=%.1fx",
			diffSettings.harvesterLimitPerRefineryMultiplier,
			diffSettings.militaryValueMultiplier);

		// Special case for late missions (mission 21+)
		if (currentGame && currentGame->getGameInitSettings().getMission() >= 21) {
			if (difficulty == Difficulty::Easy && militaryValueLimit < 2000) {
				militaryValueLimit = 2000;
				logDebug("  Mission 21+ override: MilitaryValueLimit = 2000");
			}
			else if (difficulty == Difficulty::Medium && militaryValueLimit < 4000) {
				militaryValueLimit = 4000;
				logDebug("  Mission 21+ override: MilitaryValueLimit = 4000");
			}
			else if (difficulty == Difficulty::Hard) {
				initialItemCount[Structure_Refinery] = 2;
				militaryValueLimit = 10000;
				harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
				logDebug("  Mission 21+ override: Refineries=2, MilitaryValueLimit=10000");
			}
		}

		// Refinery top-up: Ensure AI has at least the minimum refineries for difficulty
		if (diffSettings.refineryMinimum > 0 && initialItemCount[Structure_Refinery] < diffSettings.refineryMinimum) {
			int refineriesToAdd = diffSettings.refineryMinimum - initialItemCount[Structure_Refinery];
			initialItemCount[Structure_Refinery] = diffSettings.refineryMinimum;
			harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialItemCount[Structure_Refinery];
			logDebug("  Refinery top-up: Had %d, topped up to %d (granted %d refineries)", 
				initialItemCount[Structure_Refinery] - refineriesToAdd, 
				diffSettings.refineryMinimum,
				refineriesToAdd);
		} else if (diffSettings.refineryMinimum > 0) {
			logDebug("  Refinery check: Has %d (minimum %d already met, no top-up needed)", 
				initialItemCount[Structure_Refinery], diffSettings.refineryMinimum);
		}

		// Apply game options harvester override if set and lower than calculated limit
		int harvesterOverride = currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
		if (harvesterOverride >= 0 && harvesterOverride < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterOverride);
			harvesterLimit = harvesterOverride;
		}

		logDebug("  FINAL: HarvesterLimit=%d, MilitaryValueLimit=%d", 
			harvesterLimit, militaryValueLimit);

		// Set initial unit position and group units at squad rally point (Hard and Brutal only)
		if (difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal) {
			squadRallyLocation = findSquadRallyLocation();

			// Move all military units to the squad rally location at game start
			if (squadRallyLocation.isValid()) {
				logDebug("  Moving all units to squad rally point: (%d, %d)", 
					squadRallyLocation.x, squadRallyLocation.y);

				int unitsMoved = 0;
				for (const UnitBase* pUnit : getUnitList()) {
					if (pUnit->getOwner() == getHouse()
						&& pUnit->getItemID() != Unit_Carryall
						&& pUnit->getItemID() != Unit_Sandworm
						&& pUnit->getItemID() != Unit_Harvester
						&& pUnit->getItemID() != Unit_MCV
						&& pUnit->getItemID() != Unit_Frigate
                        && pUnit->getItemID() != Unit_Saboteur) {

						doMove2Pos(pUnit, squadRallyLocation.x, squadRallyLocation.y, true);
						unitsMoved++;
					}
				}

				logDebug("  Moved %d units to rally point", unitsMoved);
			}
		}

	} break;

	case GameMode::Custom: {
		// Free the home rock: the starting combat units step a little way
		// towards the enemy onto sand, so the opening build-out has room.
		//
		// This replaces the old opening rally sweep outright. That sweep gave
		// every unit one forced move to a single tile, including units under a
		// human order, units already carrying a forced order of their own and
		// the units this pass deliberately leaves standing off the rock. A unit
		// with no safe opening tile now simply receives no order at all.
		applyOpeningSpaceDispersal();

		// The rally location is still where later regrouping happens.
		squadRallyLocation = findSquadRallyLocation();

		// Set harvester/military limits based on map size and difficulty from config
		int mapsize = 4096; // Default fallback size
		if (currentGameMap) {
			mapsize = currentGameMap->getSizeX() * currentGameMap->getSizeY();
		}

		logDebug("  Difficulty: %s", 
			difficulty == Difficulty::Defend ? "Defend" :
			difficulty == Difficulty::Easy ? "Easy" :
			difficulty == Difficulty::Medium ? "Medium" :
			difficulty == Difficulty::Hard ? "Hard" : "Brutal");
		logDebug("  Map Size: %dx%d = %d tiles",
			currentGameMap ? currentGameMap->getSizeX() : 64,
			currentGameMap ? currentGameMap->getSizeY() : 64,
			mapsize);

		// Use config values based on map size
		if (mapsize <= 1024) {
			// Small map (32x32)
			harvesterLimit = diffSettings.harvesterLimitCustomSmallMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomSmallMap;
			logDebug("  Map Category: Small (32x32)");
		} else if (mapsize <= 4096) {
			// Medium map (62x62, 64x64)
			harvesterLimit = diffSettings.harvesterLimitCustomMediumMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomMediumMap;
			logDebug("  Map Category: Medium (64x64)");
		} else if (mapsize <= 16384) {
			// Large map (up to 128x128)
			harvesterLimit = diffSettings.harvesterLimitCustomLargeMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomLargeMap;
			logDebug("  Map Category: Large (up to 128x128)");
		} else {
			// Huge maps (> 128x128) - use config values
			harvesterLimit = diffSettings.harvesterLimitCustomHugeMap;
			militaryValueLimit = diffSettings.militaryValueLimitCustomHugeMap;
			logDebug("  Map Category: Huge (> 128x128)");
		}

		logDebug("  Config Values - Small(H:%d,M:%d) Med(H:%d,M:%d) Large(H:%d,M:%d)",
			diffSettings.harvesterLimitCustomSmallMap, diffSettings.militaryValueLimitCustomSmallMap,
			diffSettings.harvesterLimitCustomMediumMap, diffSettings.militaryValueLimitCustomMediumMap,
			diffSettings.harvesterLimitCustomLargeMap, diffSettings.militaryValueLimitCustomLargeMap);

		// Apply game options harvester override if set and lower than calculated limit
		int harvesterOverride = currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
		if (harvesterOverride >= 0 && harvesterOverride < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterOverride);
			harvesterLimit = harvesterOverride;
		}

		logDebug("  FINAL: HarvesterLimit=%d, MilitaryValueLimit=%d", 
			harvesterLimit, militaryValueLimit);

		// what is this useful for? Reseting limits or something
		/*
		if ((currentGameMap->getSizeX() * currentGameMap->getSizeY() / 480) < harvesterLimit && difficulty != Difficulty::Brutal) {
			harvesterLimit = currentGameMap->getSizeX() * currentGameMap->getSizeY() / 480;
			logDebug("Reset harvesterLimit: %d = mapX: %d * mapY: %d / 480", harvesterLimit, currentGameMap->getSizeX(), currentGameMap->getSizeY());
		}*/

	} break;

		}

		// Calculate total spice remaining on map and adjust harvester limit for both modes
		lastCalculatedSpice = 0;
        campaignMapHasSpice = false;
		if (currentGameMap) {
			const int mapSizeX = currentGameMap->getSizeX();
			const int mapSizeY = currentGameMap->getSizeY();

			for (int x = 0; x < mapSizeX; x++) {
				for (int y = 0; y < mapSizeY; y++) {
					if (currentGameMap->tileExists(x, y)) {
						Tile* pTile = currentGameMap->getTile(x, y);
						if (pTile && pTile->hasSpice()) {
                            campaignMapHasSpice = true;
							lastCalculatedSpice += pTile->getSpice().lround();
						}
					}
				}
			}
		}

		// Apply spice-based harvester limit only for Custom mode
		if (gameMode == GameMode::Custom) {
			// Don't build more harvesters if total spice < 2000 * harvester count
			int maxHarvestersForSpice = lastCalculatedSpice / 2000;
			if (maxHarvestersForSpice < harvesterLimit) {
				harvesterLimit = std::max(1, maxHarvestersForSpice); // Always allow at least 1 harvester
				logDebug("Harvester limit reduced due to low spice: %d (spice: %d)", harvesterLimit, lastCalculatedSpice);
			}
		}

		logDebug("Initial spice calculation: %d spice remaining on map", lastCalculatedSpice);
        if (campaignCityEconomy()) campaignBaseline.allowance=std::min(campaignBaseline.allowance,std::max(1,lastCalculatedSpice/2000));

	}

	// Recalculate spice periodically (not every cycle — full map scan is O(N) on 65K+ tiles).
	// Stagger by house ID so multiple AI players don't spike on the same frame.
	if ((getGameCycleCount() + getHouse()->getHouseID() * 100) % 500 == 0) {
		lastCalculatedSpice = 0;
        campaignMapHasSpice = false;
		if (currentGameMap) {
			const int mapSizeX = currentGameMap->getSizeX();
			const int mapSizeY = currentGameMap->getSizeY();

			for (int x = 0; x < mapSizeX; x++) {
				for (int y = 0; y < mapSizeY; y++) {
					if (currentGameMap->tileExists(x, y)) {
						Tile* pTile = currentGameMap->getTile(x, y);
						if (pTile && pTile->hasSpice()) {
                            campaignMapHasSpice = true;
							lastCalculatedSpice += pTile->getSpice().lround();
						}
					}
				}
			}
		}
	}

    if (campaignCityEconomy()) {
        if (campaignMapHasSpice) campaignSpiceZeroSince=std::numeric_limits<Uint32>::max();
        else if (campaignSpiceZeroSince==std::numeric_limits<Uint32>::max()) campaignSpiceZeroSince=getGameCycleCount();
    }

	// Continuously adjust harvester limit based on remaining spice (both Campaign and Custom modes)
	// This runs every cycle to dynamically reduce harvester targets as spice depletes
	const QuantBotConfig& config = getQuantBotConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    // Default/zero means no engine ceiling. Start with the remaining resource
    // budget, then let the house's economy/throughput planner choose its fleet.
    const int harvesterOverride = getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
    int baseHarvesterLimit = std::max(1,lastCalculatedSpice / 2000);
    if (harvesterOverride > 0) baseHarvesterLimit = std::min(baseHarvesterLimit,harvesterOverride);

    if (isCampaignEnemy() && difficulty!=Difficulty::Brutal) {
        // Game Options supplies an engine ceiling, not permission for an Easy
        // campaign opponent to expand to a skirmish-sized harvester fleet.
        baseHarvesterLimit=std::max(0,diffSettings.harvesterLimitPerRefineryMultiplier
            * initialItemCount[Structure_Refinery]);
        if (harvesterOverride>0) baseHarvesterLimit=std::min(baseHarvesterLimit,harvesterOverride);
    }

    if (const int alliedLimit = campaignAllyHarvesterLimit(); alliedLimit > 0) baseHarvesterLimit = alliedLimit;
    // The explicit Game Options ceiling also constrains each AI target.
    if (getHouse()->getMaxHarvesters() > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, getHouse()->getMaxHarvesters());

    // Only opposing Brutal houses receive the seven-worker difficulty ceiling.
    if (const int ceiling = harvesterCountCeiling(); ceiling > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, ceiling);
	// Apply spice-based reduction for all modes and difficulties
	int maxHarvestersForSpice = lastCalculatedSpice / 2000;
	int oldLimit = harvesterLimit;
	harvesterLimit = std::min(baseHarvesterLimit, std::max(1, maxHarvestersForSpice));
    if (campaignCityEconomy()) harvesterLimit=std::min(harvesterLimit,campaignHarvesterTarget());

	// Log when the limit changes
	if (oldLimit != harvesterLimit) {
		logDebug("Harvester limit adjusted: %d -> %d (spice: %d, base: %d, mode: %s, diff: %d)", 
			oldLimit, harvesterLimit, lastCalculatedSpice, baseHarvesterLimit, 
			(gameMode == GameMode::Campaign) ? "Campaign" : "Custom", static_cast<int>(difficulty));
	}

	if ((getGameCycleCount() + getHouse()->getHouseID()) % AIUPDATEINTERVAL != 0) {
		// we are not updating this AI player this cycle
		return;
	}

    updateHarvesterStrikeTelemetry();
	// Calculate the total military value of the player
	int militaryValue = 0;
	if (currentGame) {
		for (Uint32 i = Unit_FirstID; i <= Unit_LastID; i++) {
			if (i != Unit_Carryall
				&& i != Unit_Harvester
				&& i != Unit_MCV
				&& i != Unit_Sandworm) {
					militaryValue += getHouse()->getNumItems(i) * currentGame->objectData.data[i][getHouse()->getHouseID()].price;
			}
		}
	}

	// Log military stats every 30 seconds (game time)
	// MULTIPLAYER FIX: Use game cycles instead of SDL_GetTicks() to ensure
	// all clients execute this logging at the same game cycle
	static Uint32 lastMilitaryLogCycle = 0;
	const Uint32 currentCycle = getGameCycleCount();
	const Uint32 LOG_INTERVAL = MILLI2CYCLES(30000); // 30 seconds in game cycles

	if(lastMilitaryLogCycle == 0) {
		lastMilitaryLogCycle = currentCycle;
	} else if(currentCycle - lastMilitaryLogCycle >= LOG_INTERVAL) {
		SDL_Log("[QuantBot %s] ========== MILITARY STATUS ==========", getHouse()->getHouseID() == HOUSETYPE::HOUSE_HARKONNEN ? "Harkonnen" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_ATREIDES ? "Atreides" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_ORDOS ? "Ordos" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_FREMEN ? "Fremen" : 
				getHouse()->getHouseID() == HOUSETYPE::HOUSE_SARDAUKAR ? "Sardaukar" : "Mercenary");
		SDL_Log("[QuantBot] Military Value: %d (Initial: %d)", militaryValue, initialMilitaryValue);

		// Count units by type
		int infantry = getHouse()->getNumItems(Unit_Soldier) + getHouse()->getNumItems(Unit_Trooper) + getHouse()->getNumItems(Unit_Saboteur);
		int lightVehicles = getHouse()->getNumItems(Unit_Trike) + getHouse()->getNumItems(Unit_RaiderTrike) + getHouse()->getNumItems(Unit_Quad);
		int tanks = getHouse()->getNumItems(Unit_Tank) + getHouse()->getNumItems(Unit_SiegeTank) + getHouse()->getNumItems(Unit_Devastator) + getHouse()->getNumItems(Unit_SonicTank);
		int special = getHouse()->getNumItems(Unit_Launcher) + getHouse()->getNumItems(Unit_Deviator);
		int air = getHouse()->getNumItems(Unit_Ornithopter);

		int totalMilitary = infantry + lightVehicles + tanks + special + air;
		if(totalMilitary > 0) {
			SDL_Log("[QuantBot] Troop Composition: Infantry=%d (%.0f%%), Light=%d (%.0f%%), Tanks=%d (%.0f%%), Special=%d (%.0f%%), Air=%d (%.0f%%)",
					infantry, infantry * 100.0 / totalMilitary,
					lightVehicles, lightVehicles * 100.0 / totalMilitary,
					tanks, tanks * 100.0 / totalMilitary,
					special, special * 100.0 / totalMilitary,
					air, air * 100.0 / totalMilitary);
		}
		SDL_Log("[QuantBot] =====================================");
		lastMilitaryLogCycle = currentCycle;
	}

    updateCampaignWave();
	checkAllUnits();

	if (buildTimer <= 0) {
		build(militaryValue);
	}
	else {
		buildTimer -= AIUPDATEINTERVAL;
	}

	if (!supportMode) {
		if (attackTimer <= 0) {
			attack(militaryValue);
		} else {
			attackTimer -= AIUPDATEINTERVAL;
		}
	} else {
		attackTimer = std::numeric_limits<Sint32>::max();
	}

	if (cityBuildTimer <= 0) {
		manageCityBuilding();
		cityBuildTimer = AIUPDATEINTERVAL * 10;
	} else {
		cityBuildTimer -= AIUPDATEINTERVAL;
	}
}


uint64_t QuantBot::traceDecision(const std::string& event, AITelemetry::Record details) const {
    if (!AITelemetry::log().enabled()) return 0;
    details.set("state_id", telemetryState);
    return AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), event, details);
}

void QuantBot::onObjectWasBuilt(const ObjectBase* pObject) {
    if (pObject) traceDecision("object_built", AITelemetry::Record().set("item", pObject->getItemID())
        .set("object", pObject->getObjectID()).set("x", pObject->getLocation().x).set("y", pObject->getLocation().y));
}


void QuantBot::onDecrementStructures(int itemID, const Coord& location) {
    if (currentGame
            && itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall)
        recentStructureLosses.push_back({location, getStructureSize(itemID), getGameCycleCount(), static_cast<Uint32>(itemID)});
    dangerUpdated = std::numeric_limits<Uint32>::max();
    traceDecision("structure_lost", AITelemetry::Record().set("item", itemID).set("x", location.x).set("y", location.y));
}


/// When we take losses we should hold off from attacking for longer...
void QuantBot::onDecrementUnits(int itemID) {
    traceDecision("unit_lost", AITelemetry::Record().set("item", itemID));
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer += MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 30 / (static_cast<Uint8>(difficulty) + 1));
		//logDebug("loss ");
			retreatTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 20);
	}
}


/// When we get kills we should re-attack sooner...
void QuantBot::onIncrementUnitKills(int itemID) {
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 15);
		//logDebug("kill ");
	}
}

void QuantBot::onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) {
	const ObjectBase* pDamager = getObject(damagerID);

	if (pDamager == nullptr || pDamager->getOwner() == getHouse() || pObject->getItemID() == Unit_Sandworm || pObject->getItemID() == Unit_Saboteur) {
		return;
	}

    // If the human has attacked us then its time to start fighting back... unless its an attack on a special unit
    // Don't trigger with fremen or saboteur
    bool bPossiblyOwnFremen = (pObject->getOwner()->getHouseID() == HOUSE_ATREIDES) && (pObject->getItemID() == Unit_Trooper) && (currentGame->techLevel > 7);
    if(gameMode == GameMode::Campaign && !pDamager->getOwner()->isAI() && !campaignAIAttackFlag && !bPossiblyOwnFremen && (pObject->getItemID() != Unit_Saboteur)) {
        campaignAIAttackFlag = true;
    }
    if (pObject->isAStructure()) {
        doRepair(pObject);
        // no point scrambling to defend a missile
        if(pDamager->getItemID() != Structure_Palace) {
            scrambleUnitsAndDefend(pDamager,false,pObject);
        }

	}
	else if (!supportMode && pObject->isAGroundUnit()) {
		const GroundUnit* pGroundUnit = static_cast<const GroundUnit*>(pObject);

		if (pGroundUnit->isAwaitingPickup()) {
			return;
		}
		// Stop him dead in his tracks if he's going to rally point
		if (!humanControls(pGroundUnit) && pGroundUnit->wasForced() && (pGroundUnit->getItemID() != Unit_Harvester)) {
			doMove2Pos(pGroundUnit,
				pGroundUnit->getLocation().x,
				pGroundUnit->getLocation().y,
				false);
		}

        if (isCampaignEnemy() && damage>0 && campaignCombatUnit(pGroundUnit)
            && pGroundUnit->canAttack(pDamager) && !reserveDamagedUnitForRepair(pGroundUnit)
            && pGroundUnit->getAttackMode()!=RETREAT) {
            // GUARD only searches its own weapon range, so a tank otherwise
            // remains idle while an outranging launcher kills it. Retaliation
            // is defense, independent of opening grace or offensive wave slots.
            if (!pGroundUnit->hasATarget() || !pGroundUnit->isInWeaponRange(pGroundUnit->getTarget())) {
                const_cast<GroundUnit*>(pGroundUnit)->setGuardPoint(pGroundUnit->getLocation());
                doSetAttackMode(pGroundUnit,AREAGUARD);
                doAttackObject(pGroundUnit,pDamager,!pGroundUnit->isInAttackRange(pDamager));
                defenceAssignments[pGroundUnit->getObjectID()]=pDamager->getObjectID();
                traceDecision("campaign_retaliation",AITelemetry::Record()
                    .set("unit",pGroundUnit->getObjectID()).set("target",damagerID));
            }
            scrambleUnitsAndDefend(pDamager);
        }

		if (pGroundUnit->getItemID() == Unit_Harvester) {
			// Always keep Harvesters away from harm
			// Defend the harvester!
			const Harvester* pHarvester = static_cast<const Harvester*>(pGroundUnit);
			if (pHarvester->isActive()) {
				scrambleUnitsAndDefend(pDamager,false,pObject);
                auto& safety = harvesterSafety[pHarvester->getObjectID()];
                safety.nextCheck = 0;
                safety.retreatUntil = getGameCycleCount() + MILLI2CYCLES(30000);
                bool recorded = false;
                for (auto& field : unsafeFields)
                    if (blockDistance(field.location,pHarvester->getLocation()) <= 3) {
                        field.cycle = getGameCycleCount(); recorded = true; break;
                    }
                if (!recorded) unsafeFields.push_back({pHarvester->getLocation(),getGameCycleCount()});
                dangerUpdated = std::numeric_limits<Uint32>::max();
                refreshTacticalDanger();
                manageHarvesterSafety(pHarvester);
			}
		}
		else if ((pGroundUnit->getItemID() == Unit_Launcher
			|| pGroundUnit->getItemID() == Unit_Deviator)
			&& !supportMode) {
			// Keep Launchers/Deviators away from harm when taking damage (not in support mode)
			doSetAttackMode(pGroundUnit, AREAGUARD);
			int weaponRange = currentGame->objectData.data[pGroundUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
			kiteAwayFromThreat(pGroundUnit, pDamager, weaponRange);

		}
		else if (QuantBotBuildPolicy::isLightRaider(pGroundUnit->getItemID())
			&& QuantBotBuildPolicy::isArmoredTank(pDamager->getItemID())) {
			// A hit is authoritative even if targeting changed between AI updates.
			// Retreat beyond the tank's own weapon range, not merely to the squad.
			doSetAttackMode(pGroundUnit, AREAGUARD);
			kiteAwayFromThreat(pGroundUnit, pDamager, pDamager->getWeaponRange() + 2);
			traceDecision("light_raider_evade", AITelemetry::Record().set("unit", pGroundUnit->getObjectID())
				.set("threat", pDamager->getObjectID()).set("reason", "tank_hit")
				.set("desired_range", pDamager->getWeaponRange() + 2));
		}

		// If unit is below 80% then rotate them
		// If the unit is at 60% health or less and is not being forced to move anywhere
		// only do these acitons for vehicles and not when fighting turrets
		// repair them, if they are eligible to be repaired
		if (difficulty != Difficulty::Easy || (isCampaignGameType(currentGame->gameType) && gameMode==GameMode::Custom)) {
			if (pGroundUnit->getHealth() / pGroundUnit->getMaxHealth() < 0.80_fix
				&& !pGroundUnit->isInfantry()
				&& pGroundUnit->isVisible()
				&& (pDamager->getItemID() != Structure_GunTurret
					&& pDamager->getItemID() != Structure_RocketTurret)
				) {


				// If unit isn't an infrantry then heal it once it is below 2/3 health if not an easy or medium campaign
				if (getHouse()->hasRepairYard()
					&& pGroundUnit->getHealth() / pGroundUnit->getMaxHealth() < 0.6_fix

					// Medium can use an authored or player-built yard; Easy keeps engine auto-repair.
					&& !(gameMode == GameMode::Campaign && difficulty == Difficulty::Easy)
					// A deviated unit cannot be repaired, so send it to reposition instead of
					// spending the pass on a repair order the engine will refuse.
					&& pGroundUnit->isEligibleForRepair()
					) {
					doRepair(pGroundUnit);
				}

				// Rotate unit backwards if it is taking damage if it is softer
				else if (pGroundUnit->getItemID() != Unit_Devastator 
						&& pGroundUnit->getItemID() != Unit_SiegeTank) {
					doSetAttackMode(pGroundUnit, AREAGUARD);
					moveToOptimalSquadPosition(pGroundUnit, 6);  // 6 tile radius
				}



			}
		}
	}
}

Coord QuantBot::findRockExpansionSite(const MCV* mcv, bool needsLocalSpace) {
    const bool defenceReady=expansionDefenceReady(needsLocalSpace);
    // A per-MCV query has nothing to measure, so the shut gate still costs
    // nothing. The base survey runs first and reports how much room the base
    // has left even while colonisation itself is blocked: that measurement is
    // what tells production the city has nowhere left to build.
    if(!defenceReady && mcv) return Coord::Invalid();
    const int w=getMap().getSizeX(),h=getMap().getSizeY();
    std::vector<RockExpansionPolicy::Tile> tiles(w*h);
    std::vector<char> buildable(static_cast<size_t>(w)*h,0);
    std::vector<int> starts,enemies,reserved;
    int freeBase=0;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto* tile=getMap().getTile(x,y);
        const auto* ground=tile->getNonInfantryGroundObject();
        auto& out=tiles[y*w+x];
        out.rock=tile->isRock()&&!tile->isMountain();
        out.free=!tile->hasAStructure() && (!tile->hasAGroundObject() || ground==mcv);
        out.walkable=!tile->isMountain()&&!tile->hasAStructure();
        out.owned=tile->hasAStructure()&&tile->getOwner()==getHouse()->getHouseID();
        // A site that already swallowed repeated construction yards is not
        // expanded onto again just because the short-term loss block expired:
        // redeploying there without cover is what turned single losses into a
        // chain of replacements.
        out.unsafe=dangerAt(Coord(x,y),Coord(1,1))>0||nearRecentStructureLoss(x,y,1,1)
            ||lostYardsNear(x,y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit;
        const bool inBuildRange=out.rock&&getMap().isWithinBuildRange(x,y,getHouse());
        if(inBuildRange&&out.free) {
            ++freeBase;
            if(!mcv)starts.push_back(y*w+x);
        }
        // Room for a future building, which is not the same as a free tile:
        // zoned and reserved ground is spoken for, and a unit parked on rock
        // is traffic, not a reason to call the base full.
        if(!mcv) buildable[y*w+x]=inBuildRange&&!out.unsafe&&!tile->hasAStructure()&&!tile->hasCityZone()
            &&!overlapsReservedStructure(x,y,1,1);
    }
    if(!mcv) {
        availableBaseRock=freeBase;
        availableBaseFootprints=QuantBotColonisationPolicy::freeFootprints(w,h,buildable,2,2,
            QuantBotColonisationPolicy::kCrampedFootprints);
    }
    // Re-evaluate after measuring this survey, not the previous base layout.
    if(!expansionDefenceReady(needsLocalSpace)) return Coord::Invalid();
    if(mcv && mcv->getLocation().isValid()) starts.push_back(mcv->getY()*w+mcv->getX());
    if(!mcv && starts.empty()) {
        // A base with no free rock left still stands somewhere. Start the route
        // survey from the open ground around its buildings: the city that most
        // needs to settle elsewhere is exactly the one with nothing left to
        // start from, and it must not be the one that finds nowhere to go.
        for(const auto* structure:getStructureList()) {
            if(structure->getOwner()!=getHouse()) continue;
            const Coord at=structure->getLocation(),size=structure->getStructureSize();
            for(int x=at.x-1;x<=at.x+size.x;++x) for(int y=at.y-1;y<=at.y+size.y;++y)
                if(getMap().tileExists(x,y)&&tiles[y*w+x].walkable) starts.push_back(y*w+x);
        }
    }
    auto enemy=[&](const ObjectBase* object) {
        if(object->getOwner() && object->getOwner()->getTeamID()!=getHouse()->getTeamID()
            && object->isVisible(getHouse()->getTeamID())&&object->getLocation().isValid())
            enemies.push_back(object->getY()*w+object->getX());
    };
    for(const auto* structure:getStructureList())enemy(structure);
    for(const auto* unit:getUnitList()) {
        enemy(unit);
        if(unit->getOwner()==getHouse()&&unit->getItemID()==Unit_MCV&&unit!=mcv) {
            const auto it=mcvExpansionSites.find(unit->getObjectID());
            if(it!=mcvExpansionSites.end()&&getMap().tileExists(it->second.x,it->second.y))reserved.push_back(it->second.y*w+it->second.x);
        }
    }
    const StructureBase* mainYard=nullptr;
    for(const auto* structure:getStructureList())
        if(structure->getOwner()==getHouse() && structure->getItemID()==Structure_ConstructionYard
            && structure->isActive() && structure->getHealth()>0
            && (!mainYard || structure->getObjectID()<mainYard->getObjectID())) mainYard=structure;
    const int mainBase=mainYard ? mainYard->getY()*w+mainYard->getX() : -1;
    const auto result=RockExpansionPolicy::choose(w,h,tiles,starts,enemies,reserved,mainBase);
    if(!result.valid())return Coord::Invalid();
    const Coord site(result.x,result.y);
    if(!overlapsReservedStructure(site.x,site.y,2,2)&&preservesGroundAccess(Structure_ConstructionYard,site)) {
        traceDecision("rock_expansion_site",AITelemetry::Record().set("mcv",mcv?mcv->getObjectID():NONE_ID)
            .set("x",site.x).set("y",site.y).set("free_base_rock",freeBase)
            .set("local_free_rock",result.room).set("enemy_clearance",result.clearance).set("route_tiles",result.distance)
            .set("main_base_x",mainYard?mainYard->getX():-1).set("main_base_y",mainYard?mainYard->getY():-1)
            .set("base_distance",result.baseDistance).set("selection_rule","nearest_main_base_safe_rock")
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked));
        return site;
    }
    return Coord::Invalid();
}

bool QuantBot::baseBuiltOut() const {
    QuantBotColonisationPolicy::Demand demand;
    demand.productionRoomBlocked=baseProductionRoomBlocked;
    demand.freeFootprints=availableBaseFootprints;
    return QuantBotColonisationPolicy::builtOut(demand);
}

bool QuantBot::colonisationMcvDue(int mcvsIncludingQueued, int yardLimit) const {
    QuantBotColonisationPolicy::Demand demand;
    demand.citySim=currentGame->isCitySimEnabled();
    demand.customGame=gameMode==GameMode::Custom;
    // Campaign missions keep the base the script gave them, whatever mode the
    // helper bot runs in. Colonisation is a custom-game rule only.
    demand.campaignGame=isCampaignGameType(currentGame->gameType);
    demand.supportMode=supportMode;
    demand.siteAvailable=rockExpansionSite.isValid();
    demand.productionRoomBlocked=baseProductionRoomBlocked;
    demand.freeFootprints=availableBaseFootprints;
    demand.yards=getHouse()->getNumItems(Structure_ConstructionYard);
    demand.mcvsIncludingQueued=mcvsIncludingQueued;
    demand.yardLimit=yardLimit;
    return QuantBotColonisationPolicy::due(demand);
}

bool QuantBot::colonyMissionDue() const {
    // The MCV in hand may have been bought for anything; what decides its job
    // is the base it is standing in. While the base still has room, the fast
    // local deployment path keeps every MCV, which is what the production
    // yards depend on. Only a built-out base with a surveyed destination
    // sends one across the map — and only under exactly the rules that would
    // have bought a colonist in the first place, so campaigns, helpers and
    // capped games keep the behaviour they had.
    return colonisationMcvDue(0,getGameInitSettings().getGameOptions().maximumNumberOfConstructionYardsOverride);
}

// The opening search, for an MCV that is not growing an existing city base:
// city growth and colonisation are chosen by manageMcv instead.
Coord QuantBot::findMcvPlaceLocation(const MCV* pMCV) {
    AITelemetry::PerformanceScope perfScope("ai.findMcvPlaceLocation", getGameCycleCount(), getHouse()->getHouseID());
	// Always search for best location near the MCV's current position
	// This works for both first MCV and expansion MCVs.
	//
	// Perf bound: the distance penalty (-10 per tile) makes any spot more
	// than ~30 tiles away strictly worse than a closer candidate, so a full
	// map scan is pointless. Cap the outer search to a window around the
	// MCV and the inner rock-count to a small radius — the inner is just an
	// "is there room here" heuristic, not a precise survey. Without these
	// caps this function is O(W*H*innerR^2) ~= 23M ops per call on a 192^2
	// map, and gets called per undeployed MCV per AI tick.
	constexpr int kOuterRadius = 25;
	constexpr int kInnerRadius = 6;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();
	Coord mcvLocation = pMCV->getLocation();

	const int mapW = getMap().getSizeX();
	const int mapH = getMap().getSizeY();
	const int xLo = std::max(1, mcvLocation.x - kOuterRadius);
	const int xHi = std::min(mapW - 2, mcvLocation.x + kOuterRadius);
	const int yLo = std::max(1, mcvLocation.y - kOuterRadius);
	const int yHi = std::min(mapH - 2, mcvLocation.y + kOuterRadius);

	for (int placeLocationX = xLo; placeLocationX <= xHi; placeLocationX++) {
		for (int placeLocationY = yLo; placeLocationY <= yHi; placeLocationY++) {
			Coord placeLocation(placeLocationX, placeLocationY);

			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, 2, 2, false, nullptr)
                && !overlapsReservedStructure(placeLocationX,placeLocationY,2,2)
                && preservesGroundAccess(Structure_ConstructionYard,placeLocation)) {
				int locationScore = 0;

				// Calculate distance penalty (closer is better)
				int distance = lround(blockDistance(mcvLocation, placeLocation));
				locationScore -= distance * 10;  // Strong penalty for distance - MCVs should deploy near where they spawn

				// Calculate available rock in the area (more buildable space is better)
				int availableRock = 0;

				for (int x = placeLocationX - kInnerRadius; x <= placeLocationX + kInnerRadius; x++) {
					for (int y = placeLocationY - kInnerRadius; y <= placeLocationY + kInnerRadius; y++) {
						if (getMap().tileExists(x, y)) {
							const Tile* pTile = getMap().getTile(x, y);
							// Count rock tiles that aren't mountains (buildable with concrete)
							if (pTile->isRock() && !pTile->isMountain() && !pTile->hasAGroundObject()) {
								availableRock++;
							}
						}
					}
				}

				// Score based on available rock
				// A 2x2 building needs 4 tiles, so 6 buildings = 24 tiles minimum
				// But we want more space for growth
				int buildingSites = availableRock / 4;  // Rough estimate of potential building count

				if (buildingSites >= 6) {
					// Location has room for 6+ buildings, give good base score
					locationScore += 200;
					// Additional bonus for even more space (diminishing returns)
					locationScore += (buildingSites - 6) * 5;
				} else {
					// Not enough space - heavy penalty
					locationScore += buildingSites * 15;  // Still give some credit
					locationScore -= 100;  // But penalize insufficient space heavily
				}

				// Bonus for being somewhat central but not too far
				// Prefer locations that aren't at extreme corners
				int distanceFromCenter = lround(blockDistance(placeLocation, 
					Coord(getMap().getSizeX() / 2, getMap().getSizeY() / 2)));
				int mapRadius = (getMap().getSizeX() + getMap().getSizeY()) / 4;

				if (distanceFromCenter < mapRadius / 2) {
					locationScore += 20;  // Bonus for being near map center
				}

				// Pick best location
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = placeLocation;
				}
			}
		}
	}

	if (bestLocation.isValid()) {
		logDebug("MCV deployment location found at (%d, %d) with score %d", 
			bestLocation.x, bestLocation.y, bestLocationScore);
	}

	return bestLocation;
}

std::vector<Coord> QuantBot::otherMcvSites(const MCV* mcv) const {
    // Read through the live unit list so a destroyed MCV cannot keep a tile
    // reserved, exactly as the rock expansion survey does.
    std::vector<Coord> sites;
    for(const auto* unit:getUnitList()) {
        if(unit->getOwner()!=getHouse()||unit->getItemID()!=Unit_MCV||unit==mcv) continue;
        const auto it=mcvExpansionSites.find(unit->getObjectID());
        if(it!=mcvExpansionSites.end()&&it->second.isValid()) sites.push_back(it->second);
    }
    return sites;
}

bool QuantBot::onOwnRockFormation(Coord site) const {
    if(site.isInvalid()) return false;
    const auto& map=getMap();
    const int houseID=getHouse()->getHouseID();
    const int w=map.getSizeX(),h=map.getSizeY();
    if(!map.tileExists(site.x,site.y)) return false;
    // Build range can bridge sand: ownership requires connected buildable rock.
    std::vector<bool> seen(static_cast<size_t>(w)*h,false);
    std::vector<int> queue{site.y*w+site.x};
    seen[queue.front()]=true;
    for(size_t at=0;at<queue.size();++at) {
        const int index=queue[at],x=index%w,y=index/w;
        const auto* tile=map.getTile(x,y);
        if(!tile->isRock()||tile->isMountain()) continue;
        if(tile->hasAStructure()&&tile->getOwner()==houseID) return true;
        const int steps[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
        for(const auto& step:steps) {
            const int nx=x+step[0],ny=y+step[1];
            if(!map.tileExists(nx,ny)||seen[ny*w+nx]) continue;
            const auto* next=map.getTile(nx,ny);
            if(!next->isRock()||next->isMountain()) continue;
            seen[ny*w+nx]=true;queue.push_back(ny*w+nx);
        }
    }
    return false;
}

bool QuantBot::mcvSiteUsable(const MCV* pMCV, Coord site) const {
    if(site.isInvalid()) return false;
    for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
        if(!getMap().tileExists(site.x+dx,site.y+dy)) return false;
        const auto* tile=getMap().getTile(site.x+dx,site.y+dy);
        // Permanent obstacles only. A unit crossing the site is transient and
        // must not throw away a destination the MCV is still driving towards.
        if(!tile->isRock()||tile->isMountain()||tile->hasCityZone()||tile->hasAStructure()) return false;
    }
    if(overlapsReservedStructure(site.x,site.y,2,2)) return false;
    for(const auto& taken:otherMcvSites(pMCV))
        if(CityPlacementPolicy::overlaps(site.x,site.y,2,2,taken.x,taken.y,2,2)) return false;
    return true;
}

Coord QuantBot::findLocalDeploySite(const MCV* pMCV, Coord current) {
    AITelemetry::PerformanceScope perfScope("ai.findLocalDeploySite", getGameCycleCount(), getHouse()->getHouseID());
    if(pMCV==nullptr||pMCV->getLocation().isInvalid()) return Coord::Invalid();
    const auto& map=getMap();
    const Coord centre=pMCV->getLocation();
    const int xLo=std::max(0,centre.x-kMcvLocalRadius),xHi=std::min(map.getSizeX()-1,centre.x+kMcvLocalRadius);
    const int yLo=std::max(0,centre.y-kMcvLocalRadius),yHi=std::min(map.getSizeY()-1,centre.y+kMcvLocalRadius);
    const int w=xHi-xLo+1,h=yHi-yLo+1;
    if(w<2||h<2) return Coord::Invalid();
    const int houseID=getHouse()->getHouseID();
    std::vector<McvDeployPolicy::Tile> tiles(static_cast<size_t>(w)*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto* tile=map.getTile(xLo+x,yLo+y);
        const auto* ground=tile->getNonInfantryGroundObject();
        auto& out=tiles[y*w+x];
        out.rock=tile->isRock()&&!tile->isMountain();
        out.free=!tile->hasAStructure()&&(!tile->hasAGroundObject()||ground==pMCV);
        out.passable=!tile->isMountain()&&!tile->hasAStructure();
        out.owned=tile->hasAStructure()&&tile->getOwner()==houseID;
        out.blocked=tile->hasCityZone()||overlapsReservedStructure(xLo+x,yLo+y,1,1)
            ||dangerAt(Coord(xLo+x,yLo+y))>0
            ||lostYardsNear(xLo+x,yLo+y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit;
    }
    // One yard per remembered site: two MCVs never drive at the same tile.
    for(const auto& taken:otherMcvSites(pMCV))
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const int x=taken.x+dx-xLo,y=taken.y+dy-yLo;
            if(x>=0&&x<w&&y>=0&&y<h) tiles[y*w+x].blocked=true;
        }
    const int start=(centre.y-yLo)*w+(centre.x-xLo);
    int prefer=-1;
    if(current.isValid()&&current.x>=xLo&&current.x<=xHi&&current.y>=yLo&&current.y<=yHi)
        prefer=(current.y-yLo)*w+(current.x-xLo);
    // Ownership can lie outside this local window on the same formation.
    if(onOwnRockFormation(centre)) tiles[start].owned=true;
    if(std::none_of(tiles.begin(),tiles.end(),[](const auto& tile) { return tile.owned; }))
        return Coord::Invalid();
    const auto site=McvDeployPolicy::choose(w,h,tiles,start,kMcvDeployRoom,prefer,
        [&](int x,int y) {
            const Coord chosen(xLo+x,yLo+y);
            return preservesGroundAccess(Structure_ConstructionYard,chosen);
        });
    return site.valid() ? Coord(xLo+site.x,yLo+site.y) : Coord::Invalid();
}

bool QuantBot::mcvMayDeployHere(const MCV* pMCV, bool expansion) {
    const Coord at=pMCV->getLocation();
    if(!pMCV->canDeploy()||overlapsReservedStructure(at.x,at.y,2,2)
        || !preservesGroundAccess(Structure_ConstructionYard,at)) return false;
    // An opening yard keeps the original rule: a legal footprint is enough.
    if(!expansion) return true;
    if(dangerAt(at,Coord(2,2))>0
        || lostYardsNear(at.x,at.y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit) return false;
    // Growth on the rock the base already holds is ordinary building work. It
    // is covered by the base's own defences, so it must not wait for the
    // outlying-colony checklist (repair yard, high tech factory and three
    // rocket turrets over every expansion yard) the way a new formation does.
    if(onOwnRockFormation(at)) return true;
    // A mission that was already approved deploys where it was sent. The core
    // prerequisite is waived for exactly the site this MCV was given and for no
    // other tile, so a colonist that arrives after the base regains a little
    // room is not turned away from the formation it just crossed the map for.
    const auto assigned=mcvExpansionSites.find(pMCV->getObjectID());
    const bool approvedSite=assigned!=mcvExpansionSites.end()&&assigned->second==at;
    return expansionDefenceReady(approvedSite)&&!nearRecentStructureLoss(at.x,at.y,2,2);
}

void QuantBot::manageMcv(const MCV* pMCV) {
    if(planningBuilder!=NONE_ID) {
        planningBuilder=NONE_ID;
        clearPlacementCache();
    }
    const Uint32 id=pMCV->getObjectID();
    // Progress refreshes the retry timer; traffic gets a grace period after arrival.
    if(pMCV->isMoving()) {
        mcvSurveyCycles[id]=getGameCycleCount();
        return;
    }
    if(pMCV->wasForced()) return;
    const Coord location=pMCV->getLocation();
    const bool expansion=currentGame->isCitySimEnabled()&&getHouse()->getNumItems(Structure_ConstructionYard)>0;
    const Uint32 cycle=getGameCycleCount();
    const auto surveyed=mcvSurveyCycles.find(id);
    const bool stale=surveyed==mcvSurveyCycles.end()||cycle-surveyed->second>=MILLI2CYCLES(5000);

    auto remembered=[&]() {
        const auto it=mcvExpansionSites.find(id);
        return it!=mcvExpansionSites.end() ? it->second : Coord::Invalid();
    };
    Coord target=expansion ? remembered() : Coord::Invalid();
    const char* choice="remembered";
    bool surveyedNow=false;
    // After five seconds without movement, a blocked destination can be
    // replaced. Progress and brief traffic keep the existing assignment.
    const bool deployableHere=mcvMayDeployHere(pMCV,expansion);
    if(expansion&&(stale||(target.isValid()&&!mcvSiteUsable(pMCV,target)))) {
        mcvSurveyCycles[id]=cycle;
        surveyedNow=true;
        // Grow on the formation the base already stands on first; only when it
        // has no room left does colonising a new one apply, under its own
        // gates. A stalled trip can use the nearest clear footprint; moving
        // MCVs retain their assignments above.
        //
        // A base that is out of room reverses the order: the last cramped
        // corner of home rock is not worth another yard when the city needs a
        // new formation, so the colony is offered first and the local search
        // stays as the fallback. Everywhere else the local path is untouched,
        // so extra production yards still deploy on the spot.
        const bool colonise=colonyMissionDue();
        // A colony trip already under way is kept while its site is still a
        // legal footprint. The survey re-runs every five seconds and ranks
        // formations by distance from the main base, so without this an MCV
        // could be sent between two equally good formations for ever.
        const bool onTrip=colonise&&target.isValid()&&mcvSiteUsable(pMCV,target)
            &&!onOwnRockFormation(target);
        Coord site=!colonise ? Coord::Invalid() : onTrip ? target : findRockExpansionSite(pMCV);
        choice=site.isValid() ? "colony" : "local";
        if(!site.isValid()) {
            site=findLocalDeploySite(pMCV,(deployableHere||stale) ? Coord::Invalid() : target);
            if(!site.isValid()) {
                choice="expansion";
                if(!colonise) site=findRockExpansionSite(pMCV);
            }
            if(!site.isValid()) {
                // The local search has just failed for this MCV: there is no
                // usable yard footprint on the rock the base stands on, whatever
                // the base survey last measured. A core building that could only
                // ever be built at home must not keep the colonist parked, so
                // the remote survey runs once more with that prerequisite
                // waived. The delivered colonist in the reported match waited a
                // minute here because the base briefly reported room again.
                //
                // A trip already under way to a legal remote footprint is kept
                // rather than re-ranked: the survey runs every five seconds and
                // orders formations by distance, so without this an MCV could
                // be sent between two equally good ones for ever.
                site=(target.isValid()&&mcvSiteUsable(pMCV,target)&&!onOwnRockFormation(target))
                    ? target : findRockExpansionSite(pMCV,true);
                if(site.isValid()) choice="expansion_no_local";
            }
        }
        if(site.isValid()) {
            target=site;
            mcvExpansionSites[id]=site;
        } else if(!mcvSiteUsable(pMCV,target)) {
            target=Coord::Invalid();
            mcvExpansionSites.erase(id);
            choice="none";
        }
    }

    const bool atTarget=target.isValid()&&target==location;
    const bool ready=(!expansion||atTarget)&&deployableHere;
    // Read what the trace needs while the MCV still exists: a successful
    // deployment destroys it. The formation check is only worth its flood fill
    // when a stalled MCV is actually being reported.
    const uint64_t signature=(static_cast<uint64_t>(target.isValid()?target.x+1:0)<<40)
        ^(static_cast<uint64_t>(target.isValid()?target.y+1:0)<<24)
        ^(static_cast<uint64_t>(ready?1:0)<<16)^(static_cast<uint64_t>(atTarget?1:0)<<8)
        ^static_cast<uint64_t>(expansion?1:0);
    const bool report=ready||lastMcvTrace[id]!=signature;
    const bool couldDeploy=pMCV->canDeploy();
    const bool ownFormation=report&&expansion&&onOwnRockFormation(location);
    const bool deployed=ready&&doDeploy(pMCV);
    if(deployed) {
        mcvExpansionSites.erase(id);
        mcvSurveyCycles.erase(id);
        rockSurveyCycle=std::numeric_limits<Uint32>::max();
        clearPlacementCache();
    } else if(!expansion&&stale) {
        mcvSurveyCycles[id]=cycle;
        const Coord pos=findMcvPlaceLocation(pMCV);
        if(pos.isValid()) doMove2Pos(pMCV,pos.x,pos.y,true);
    } else if(target.isValid()&&!atTarget&&surveyedNow) {
        // Reissue only at the retry cadence or after a permanent obstruction.
        doMove2Pos(pMCV,target.x,target.y,true);
    }

    if(report) {
        lastMcvTrace[id]=signature;
        traceDecision("mcv_deployment",AITelemetry::Record().set("mcv",id)
            .set("x",location.x).set("y",location.y)
            .set("target_x",target.isValid()?target.x:-1).set("target_y",target.isValid()?target.y:-1)
            .set("site",choice).set("expansion",expansion).set("deployed",deployed)
            .set("at_target",atTarget).set("can_deploy",couldDeploy)
            .set("own_formation",ownFormation)
            .set("colony_mission",colonyMissionDue())
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked)
            .set("danger",dangerAt(location,Coord(2,2)))
            .set("defence_ready",expansionDefenceReady())
            .set("yards",getHouse()->getNumItems(Structure_ConstructionYard)));
    }
    if(deployed) lastMcvTrace.erase(id);
}

namespace {

CityPlacementPolicy::RoadImpact cityRoadImpact(const Map& map, int x, int y, int w, int h, Uint32 item) {
    if (!currentGame || !currentGame->isCitySimEnabled() || item == Structure_Slab1
        || item == Structure_Slab4 || item == Structure_Road) return {};
    return CityPlacementPolicy::assessRoadsOnMap(map.getSizeX(), map.getSizeY(), x, y, w, h,
        item == Structure_RocketTurret,
        [&](int tx, int ty) { return map.tileExists(tx, ty) && map.getTile(tx, ty)->isRoadConnection(); },
        [&](int tx, int ty) {
            const auto* tile=map.getTile(tx,ty);
            return tile && !tile->hasAGroundObject() && DuneCity::isCityBuildableTerrain(tile->getType());
        });
}

// A structure on this tile that is one of our city zones, or nullptr.
const StructureBase* ownZoneAt(const Map& map, int houseID, int x, int y) {
	if (!map.tileExists(x, y)) return nullptr;
	const ObjectBase* pObject = map.getTile(x, y)->getNonInfantryGroundObject();
	if (pObject == nullptr || !pObject->isAStructure()) return nullptr;
	const auto* pStructure = static_cast<const StructureBase*>(pObject);
	if (!DuneCity::isCityZoneStructure(pStructure->getItemID())) return nullptr;
	if (pStructure->getOwner() == nullptr || pStructure->getOwner()->getHouseID() != houseID) return nullptr;
	return pStructure;
}

// Count nearby R/C/I on each side, including across a single road tile.
// Two or more occupied sides identify an infill gap, independently of zone type.
int residentialInfillSides(const Map& map,int house,int x,int y,int w,int h) {
    int sides=0;
    for (int side=0;side<4;++side) {
        bool found=false;
        for (int gap=1;gap<=2 && !found;++gap) {
            const int length=side<2 ? w : h;
            for (int offset=0;offset<length;++offset) {
                const int tx=side<2 ? x+offset : (side==2 ? x-gap : x+w-1+gap);
                const int ty=side>=2 ? y+offset : (side==0 ? y-gap : y+h-1+gap);
                found |= ownZoneAt(map,house,tx,ty)!=nullptr;
            }
        }
        sides+=found;
    }
    return sides;
}

// Prefer filling a four-zone block, but only where its perimeter can carry roads.
int fourZoneBlockBonus(const Map& map, int house, int x, int y) {
    int best=0;
    for (int oy : {0,2}) for (int ox : {0,2}) {
        const int bx=x-ox, by=y-oy;
        if (!CityPlacementPolicy::fourZoneBlockFits(map.getSizeX(),map.getSizeY(),bx,by)) continue;
        bool valid=true; int neighbours=0;
        for (int sy=0; sy<4 && valid; sy+=2) for (int sx=0; sx<4 && valid; sx+=2) {
            const int zx=bx+sx, zy=by+sy;
            if (zx==x && zy==y) continue;
            const auto* zone=ownZoneAt(map,house,zx,zy);
            if (zone && zone->getLocation()==Coord(zx,zy)) { ++neighbours; continue; }
            for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
                const auto* tile=map.getTile(zx+dx,zy+dy);
                if (!tile || tile->hasAStructure() || !DuneCity::isCityZoneTerrain(tile->getType())) valid=false;
            }
        }
        for (int dy=-1; dy<=4 && valid; ++dy) for (int dx=-1; dx<=4 && valid; ++dx) {
            if (dx>=0 && dx<4 && dy>=0 && dy<4) continue;
            const auto* tile=map.getTile(bx+dx,by+dy);
            if (!tile || (!tile->isRoadConnection() && (tile->hasAStructure()
                || !DuneCity::isCityBuildableTerrain(tile->getType())))) valid=false;
        }
        if (valid) best=std::max(best,neighbours*90);
    }
    return best;
}

// True when the tile could carry a road or already does, ignoring tiles that
// the candidate footprint (x, y, w, h) is about to cover.
bool tileKeepsFrontage(const Map& map, int tx, int ty, int x, int y, int w, int h) {
	if (!map.tileExists(tx, ty)) return false;
	if (tx >= x && tx < x + w && ty >= y && ty < y + h) return false;
	const Tile* t = map.getTile(tx, ty);
	if (t->isRoad()) return true;
	return !t->hasAStructure() && !t->hasCityZone() && !t->isMountain()
		&& !t->hasAGroundObject() && DuneCity::isCityZoneTerrain(t->getType());
}

// Would a lot at (x, y, w, h) take away the last open side of a neighbouring
// zone? Every lot must keep a side where a road can run.
bool wouldLandlockNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h) {
	std::set<Uint32> checked;
	auto sealsNeighbour = [&](int nx, int ny) {
		const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
		if (pZone == nullptr || !checked.insert(pZone->getObjectID()).second) return false;
		const int zx = pZone->getX(), zy = pZone->getY();
		const int zw = pZone->getStructureSizeX(), zh = pZone->getStructureSizeY();
		for (int i = zx; i < zx + zw; i++) {
			if (tileKeepsFrontage(map, i, zy - 1, x, y, w, h)) return false;
			if (tileKeepsFrontage(map, i, zy + zh, x, y, w, h)) return false;
		}
		for (int j = zy; j < zy + zh; j++) {
			if (tileKeepsFrontage(map, zx - 1, j, x, y, w, h)) return false;
			if (tileKeepsFrontage(map, zx + zw, j, x, y, w, h)) return false;
		}
		return true;
	};
	for (int i = x; i < x + w; i++) {
		if (sealsNeighbour(i, y - 1) || sealsNeighbour(i, y + h)) return true;
	}
	for (int j = y; j < y + h; j++) {
		if (sealsNeighbour(x - 1, j) || sealsNeighbour(x + w, j)) return true;
	}
	return false;
}

// Is there one of our zones directly beside this lot, or one road tile away,
// sharing its row or column? That is the "next to each other or one away"
// pattern that keeps a road path along every row of lots.
bool alignedWithNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h) {
	const int offsets[4][2] = { { w, 0 }, { w + 1, 0 }, { 0, h }, { 0, h + 1 } };
	for (const auto& offset : offsets) {
		for (int sign = -1; sign <= 1; sign += 2) {
			const int nx = x + sign * offset[0];
			const int ny = y + sign * offset[1];
			const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
			if (pZone != nullptr && pZone->getX() == nx && pZone->getY() == ny) return true;
		}
	}
	return false;
}

} // namespace

void QuantBot::refreshTacticalDanger() {
    AITelemetry::PerformanceScope perfScope("ai.refreshTacticalDanger", getGameCycleCount(), getHouse()->getHouseID());
    const Uint32 now = getGameCycleCount();
    if (dangerUpdated != std::numeric_limits<Uint32>::max()
        && now - dangerUpdated < MILLI2CYCLES(2000)) return;
    dangerUpdated = now;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    tacticalDanger.assign(w*h, 0);
    harvesterDanger.assign(w*h, 0);
    lossDanger.assign(w*h, 0);
    visibleEnemyBases.clear();
    visibleHarvestLaunchers.clear();
    const bool emitSafety = AITelemetry::log().enabled()
        && (lastSafetyTrace == std::numeric_limits<Uint32>::max() || now-lastSafetyTrace >= MILLI2CYCLES(30000));
    AITelemetry::Record threats;
    auto stamp = [&](std::vector<int>& grid, Coord p, int radius, int strength) {
        if (p.isInvalid()) return;
        for (int y = std::max(0, p.y-radius); y <= std::min(h-1, p.y+radius); ++y)
            for (int x = std::max(0, p.x-radius); x <= std::min(w-1, p.x+radius); ++x)
                grid[y*w+x] = std::min(10000, grid[y*w+x] + strength);
    };
    auto observe = [&](const ObjectBase* object) {
        if (!object || !object->getOwner() || object->getHealth() <= 0
            || object->getOwner()->getTeamID() == getHouse()->getTeamID()
            || !object->isVisible(getHouse()->getTeamID()) || object->getLocation().isInvalid()) return;
        if (object->isAStructure()) visibleEnemyBases.push_back(object->getLocation());
        // Palace missiles are addressed by spacing, not a permanent map-wide veto.
        if (!object->canAttack() || object->getItemID() == Structure_Palace) return;
        Coord p = object->getLocation();
        if (const auto* structure = dynamic_cast<const StructureBase*>(object))
            p += Coord(structure->getStructureSizeX()/2, structure->getStructureSizeY()/2);
        const int radius = std::max(1, object->getWeaponRange()) + 2;
        stamp(tacticalDanger, p, radius, 100);
        // Launchers get an early-warning margin: a harvester must turn before missiles arrive.
        // Tracked harvesters can crush foot troops; do not treat them like tanks.
        const int harvestRadius = TacticalSafetyPolicy::harvesterThreatRadius(object->getItemID(),object->getWeaponRange());
        if (object->getItemID()==Unit_Launcher) visibleHarvestLaunchers.push_back(object->getObjectID());
        for (int y = std::max(0,p.y-harvestRadius); y <= std::min(h-1,p.y+harvestRadius); ++y)
            for (int x = std::max(0,p.x-harvestRadius); x <= std::min(w-1,p.x+harvestRadius); ++x)
                if ((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y) <= harvestRadius*harvestRadius)
                    harvesterDanger[y*w+x] += 100;
        if (emitSafety) threats.set(std::to_string(object->getObjectID()), AITelemetry::Record()
            .set("item",object->getItemID()).set("x",p.x).set("y",p.y).set("radius_tiles",radius)
            .set("harvester_radius_tiles",harvestRadius));
    };
    for (const auto* unit : getUnitList()) if (unit->isActive()) observe(unit);
    for (const auto* structure : getStructureList()) observe(structure);
    factoryEnemyClearance = TacticalSafetyPolicy::enemyClearance(tacticalDanger,w,h);
    for (const auto& loss : recentStructureLosses) {
        const Uint32 age = now - loss.cycle;
        const Uint32 lifetime=loss.item==Structure_HeavyFactory ? MILLI2CYCLES(900000) : MILLI2CYCLES(300000);
        if (age >= lifetime) continue;
        const int strength = TacticalSafetyPolicy::lossStrength(age,lifetime);
        for (int y = 0; y < loss.size.y; ++y) for (int x = 0; x < loss.size.x; ++x)
            stamp(lossDanger, loss.location + Coord(x,y), 3, strength);
    }
    unsafeFields.erase(std::remove_if(unsafeFields.begin(), unsafeFields.end(),
        [&](const auto& field) { return now-field.cycle >= MILLI2CYCLES(120000); }), unsafeFields.end());
    for (auto it = harvesterSafety.begin(); it != harvesterSafety.end();) {
        if (!currentGame->getObjectManager().getObject(it->first)) {
            if (it->second.lastLocation.isValid()) unsafeFields.push_back({it->second.lastLocation, now});
            it = harvesterSafety.erase(it);
        } else ++it;
    }
    if (emitSafety) {
        lastSafetyTrace = now;
        traceDecision("tactical_safety_snapshot", AITelemetry::Record().set("visible_threats",threats)
            .set("recent_structure_losses",recentStructureLosses.size()).set("unsafe_fields",unsafeFields.size()));
    }
}

int QuantBot::dangerAt(Coord pos, Coord size, bool losses) const {
    const auto& grid = losses ? lossDanger : tacticalDanger;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    if (pos.isInvalid() || grid.size() != static_cast<size_t>(w*h)) return 0;
    int danger = 0;
    for (int y = std::max(0,pos.y); y < std::min(h,pos.y+size.y); ++y)
        for (int x = std::max(0,pos.x); x < std::min(w,pos.x+size.x); ++x)
            danger = std::max(danger, grid[y*w+x]);
    return danger;
}

bool QuantBot::reactorClearance(Uint32 item, Coord pos) const {
    const auto size = getStructureSize(item);
    const auto critical = TacticalSafetyPolicy::protectedReactorNeighbour;
    if (!critical(item)) return true;
    auto clears = [&](Uint32 other, Coord location, Coord otherSize) {
        if (!((item == Structure_NuclearPlant && critical(other))
            || (other == Structure_NuclearPlant && critical(item)))) return true;
        return TacticalSafetyPolicy::blastClearance(pos.x,pos.y,size.x,size.y,
            location.x,location.y,otherSize.x,otherSize.y);
    };
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && !clears(structure->getItemID(), structure->getLocation(), structure->getStructureSize())) return false;
    for (const auto& entry : reservedStructures)
        if (entry.first != planningBuilder && !clears(entry.second.item, entry.second.location, getStructureSize(entry.second.item))) return false;
    return true;
}

int QuantBot::rearScore(Coord pos, Coord base) const {
    if (visibleEnemyBases.empty()) return 0;
    int fromSite = 100000, fromBase = 100000;
    for (Coord enemy : visibleEnemyBases) {
        fromSite = std::min(fromSite, std::max(std::abs(pos.x-enemy.x), std::abs(pos.y-enemy.y)));
        fromBase = std::min(fromBase, std::max(std::abs(base.x-enemy.x), std::abs(base.y-enemy.y)));
    }
    return std::clamp(fromSite-fromBase, -20, 20) * 40;
}

int QuantBot::recentFactoryLossCount() const {
    return std::count_if(recentStructureLosses.begin(), recentStructureLosses.end(), [&](const auto& loss) {
        return loss.item == Structure_HeavyFactory && getGameCycleCount()-loss.cycle < MILLI2CYCLES(120000);
    });
}

bool QuantBot::manageHarvesterSafety(const Harvester* harvester) {
    const Uint32 now = getGameCycleCount();
    auto& state = harvesterSafety[harvester->getObjectID()];
    const bool newHarvester = state.lastLocation.isInvalid();
    state.lastLocation = harvester->getLocation();
    if (now < state.nextCheck) return state.controlled;
    state.nextCheck = now + MILLI2CYCLES(2000);
    const Coord origin = harvester->getLocation(), destination = harvester->getDestination();
    auto distance = [](Coord a, Coord b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); };
    auto danger = [&](Coord p) {
        return p.isValid() && p.x < getMap().getSizeX() && p.y < getMap().getSizeY()
            && harvesterDanger.size() == static_cast<size_t>(getMap().getSizeX()*getMap().getSizeY())
            ? harvesterDanger[p.y*getMap().getSizeX()+p.x] : 0;
    };
    const bool threatened = danger(origin) > 0;
    // Remember evacuations before the first hit too. Losing sight of an enemy
    // must not immediately make its spice field attractive again.
    auto rememberUnsafe = [&](Coord p) {
        if (!p.isValid() || danger(p)==0) return;
        for (auto& field : unsafeFields) if (distance(p,field.location)<=3) {
            field.cycle=now; return;
        }
        unsafeFields.push_back({p,now});
    };
    rememberUnsafe(origin);
    rememberUnsafe(destination);
    // Recently evacuated fields cool down for two minutes, even under fog.
    auto memoryPenalty = [&](Coord p) {
        int penalty = 0;
        for (const auto& field : unsafeFields)
            if (distance(p,field.location) <= 6 && now-field.cycle < MILLI2CYCLES(120000))
                penalty = std::max(penalty, 12-static_cast<int>((now-field.cycle)/MILLI2CYCLES(10000)));
        return penalty;
    };
    auto routeSafe = [&](Coord end) {
        return TacticalSafetyPolicy::escapeCorridor(origin.x,origin.y,end.x,end.y,
            [&](int x,int y) { return danger(Coord(x,y)); });
    };
    // Ask for help before the first missile lands. Use the cached visible
    // launcher list, and one nearest threat per check; incident debounce and
    // already-committed forces keep many harvesters from recruiting repeatedly.
    const ObjectBase* clearingTarget=nullptr;
    int nearestThreat=std::numeric_limits<int>::max();
    for (Uint32 id:visibleHarvestLaunchers) {
        const auto* enemy=getObject(id);
        if (!enemy || enemy->getHealth()<=0 || !enemy->canAttack(harvester)
            || !enemy->isVisible(getHouse()->getTeamID())
            || enemy->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const int radius=TacticalSafetyPolicy::harvesterThreatRadius(enemy->getItemID(),enemy->getWeaponRange());
        const int fromHarvester=distance(origin,enemy->getLocation());
        const int fromJob=destination.isValid() ? distance(destination,enemy->getLocation()) : fromHarvester;
        if (std::min(fromHarvester,fromJob)>radius || fromHarvester>radius+6) continue;
        if (fromHarvester<nearestThreat) { clearingTarget=enemy; nearestThreat=fromHarvester; }
    }
    if (clearingTarget) scrambleUnitsAndDefend(clearingTarget,true);
    // Leave an established safe unloading trip alone when its next job is safe.
    if (harvester->isReturning() && harvester->getAmountOfSpice()>0) {
        const auto* target=dynamic_cast<const StructureBase*>(harvester->getTarget());
        const Coord job=harvester->getGuardPoint();
        if (target && target->getOwner()==getHouse() && target->getHealth()>0
            && target->acceptsHarvesterDropoff() && danger(target->getClosestPoint(origin))==0
            && routeSafe(target->getClosestPoint(origin))
            && (harvester->getAttackMode()==STOP
                || (job.isValid() && danger(job)==0 && memoryPenalty(job)==0 && routeSafe(job)))) {
            state.controlled=true;
            return true;
        }
    }
    // Do not hold a safe vehicle after enemies leave. Keep its existing safe job.
    if (!threatened && !harvester->isReturning() && destination.isValid()
        && danger(destination)==0 && memoryPenalty(destination)==0
        && harvester->getAttackMode() != STOP && !newHarvester && routeSafe(destination)) {
        state.controlled = false;
        state.retreatUntil = 0;
        return false;
    }
    state.controlled = true;
    std::vector<Coord> peers;
    for (const auto* unit : getUnitList())
        if (unit != harvester && unit->isActive() && unit->getOwner() == getHouse()
            && unit->getItemID() == Unit_Harvester)
        {
            const auto peer = harvesterSafety.find(unit->getObjectID());
            const Coord reserved = peer != harvesterSafety.end() && peer->second.controlled
                ? peer->second.plannedDestination : Coord::Invalid();
            peers.push_back(reserved.isValid() ? reserved
                : unit->getDestination().isValid() ? unit->getDestination() : unit->getLocation());
        }
    auto crowdPenalty = [&](Coord candidate) {
        int result = 0;
        for (Coord peer : peers) if (distance(candidate,peer)<4) result += (4-distance(candidate,peer))*12;
        return result;
    };
    Coord best = Coord::Invalid();
    int bestScore = std::numeric_limits<int>::max();
    int safeFields = 0, rejectedRoutes = 0;
    for (int y = 0; y < getMap().getSizeY(); ++y) for (int x = 0; x < getMap().getSizeX(); ++x) {
        const Coord candidate(x,y);
        if (!getMap().getTile(x,y)->hasSpice() || danger(candidate)>0
            || memoryPenalty(candidate)>0 || !harvester->canPass(x,y)) continue;
        ++safeFields;
        const int score = distance(origin,candidate)*3 + crowdPenalty(candidate);
        if (score >= bestScore) continue;
        if (!routeSafe(candidate)) { ++rejectedRoutes; continue; }
        bestScore = score; best = candidate;
    }
    const bool foundSpice = best.isValid();
    // A partial load can continue harvesting elsewhere. Only unload a full load,
    // finish a real return trip, or salvage cargo when no safe field exists.
    if (TacticalSafetyPolicy::needsRefineryRefuge(harvester->isReturning(),
            harvester->getAmountOfSpice()>=HARVESTERMAXSPICE,
            harvester->getAmountOfSpice()>0,foundSpice)) {
        const StructureBase* refuge = nullptr;
        int bestRefineryScore = std::numeric_limits<int>::max();
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
                || !structure->acceptsHarvesterDropoff()) continue;
            const Coord entry = structure->getClosestPoint(origin);
            if (danger(entry) > 0 || !routeSafe(entry)) continue;
            const int score = distance(origin,entry) + structure->getHarvesterDropoffBookings()*3;
            if (score < bestRefineryScore) { refuge=structure; bestRefineryScore=score; }
        }
        if (harvester->isReturning()) {
            const auto* target=dynamic_cast<const StructureBase*>(harvester->getTarget());
            if (target && target->getOwner()==getHouse() && target->getHealth()>0
                && target->acceptsHarvesterDropoff() && danger(target->getClosestPoint(origin))==0
                && routeSafe(target->getClosestPoint(origin))) refuge=target;
        }
        if (refuge) {
            state.controlled = true;
            state.plannedDestination = refuge->getLocation();
            // Replace the remembered job before unloading, so deploy/carryall
            // return resumes at the alternate field rather than the old one.
            const Coord job=harvester->getGuardPoint();
            const bool replaceJob=job.isInvalid() || danger(job)>0 || memoryPenalty(job)>0 || !routeSafe(job);
            if (replaceJob) {
                doSetAttackMode(harvester,foundSpice ? HARVEST : STOP);
                if (foundSpice) doMove2Pos(harvester,best.x,best.y,false);
            }
            if (harvester->getTarget() != refuge) {
                doMove2Object(harvester,refuge);
                traceDecision("harvester_safety",AITelemetry::Record().set("object",harvester->getObjectID())
                    .set("action","retreat_refinery").set("refinery",refuge->getObjectID())
                    .set("x",origin.x).set("y",origin.y).set("cargo",harvester->getAmountOfSpice().lround()));
            }
            return true;
        }
    }

    // If no safe spice corridor exists, disperse to the nearest safe open tile.
    // There is deliberately no base-centre attraction.
    if (!foundSpice) {
        for (int y = std::max(0,origin.y-20); y <= std::min(getMap().getSizeY()-1,origin.y+20); ++y)
            for (int x = std::max(0,origin.x-20); x <= std::min(getMap().getSizeX()-1,origin.x+20); ++x) {
                const Coord candidate(x,y);
                if (danger(candidate)>0 || !harvester->canPass(x,y)) continue;
                const int score = distance(origin,candidate)*3 + crowdPenalty(candidate);
                if (score < bestScore && routeSafe(candidate)) { bestScore=score; best=candidate; }
            }
    }
    state.plannedDestination = best; // reserve immediately, before queued movement commands execute
    if (best.isValid()) {
        const auto mode = foundSpice ? HARVEST : STOP;
        if (best != destination || harvester->getAttackMode() != mode) {
            doSetAttackMode(harvester, mode);
            doMove2Pos(harvester,best.x,best.y,!foundSpice);
        }
    } else doSetAttackMode(harvester, STOP);
    state.retreatUntil = 0;
    // Retrying the same evacuation route is execution noise. Preserve every
    // distinct safe-field decision, which is what later tuning can use.
    const uint64_t safetySignature = (uint64_t(foundSpice) << 32)
        | (uint64_t(best.x & 0xffff) << 16) | uint64_t(best.y & 0xffff);
    if (lastHarvesterSafetyTrace[harvester->getObjectID()] != safetySignature) {
        traceDecision("harvester_safety", AITelemetry::Record().set("object",harvester->getObjectID())
            .set("action",foundSpice ? "redirect_spice" : best.isValid() ? "disperse" : "no_safe_route")
            .set("x",origin.x).set("y",origin.y).set("destination_x",best.x).set("destination_y",best.y)
            .set("danger",danger(origin)).set("old_destination_danger",danger(destination))
            .set("safe_fields",safeFields).set("rejected_routes",rejectedRoutes)
            .set("memory_penalty",best.isValid()?memoryPenalty(best):0)
            .set("crowding_penalty",best.isValid()?crowdPenalty(best):0)
            .set("cargo",harvester->getAmountOfSpice().lround()));
        lastHarvesterSafetyTrace[harvester->getObjectID()] = safetySignature;
    }
    return true;
}

Uint32 QuantBot::mainConstructionYardID() const {
    // The oldest surviving yard anchors the main base and already sits inside
    // its defence.
    Uint32 mainYard = NONE_ID;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && structure->getItemID() == Structure_ConstructionYard
            && (mainYard == NONE_ID || structure->getObjectID() < mainYard))
            mainYard = structure->getObjectID();
    return mainYard;
}

int QuantBot::expansionTurretsMissing(const StructureBase* yard, bool planned) const {
    if (!yard || yard->getOwner()!=getHouse() || yard->getHealth()<=0
        || !isExpansionYard(yard->getItemID(),yard->getObjectID(),mainConstructionYardID())) return 0;
    const int radius=std::max(1,currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange-1);
    int coverage=0;
    for (const auto* turret:getStructureList())
        if (turret->getOwner()==getHouse() && turret->getHealth()>0 && turret->getItemID()==Structure_RocketTurret
            && RocketTurretPolicy::coversBuilding(turret->getLocation(),yard->getLocation(),yard->getStructureSize(),radius)) ++coverage;
    if (planned) for (const auto& entry:reservedStructures)
        if (entry.second.item==Structure_RocketTurret
            && RocketTurretPolicy::coversBuilding(entry.second.location,yard->getLocation(),yard->getStructureSize(),radius)) ++coverage;
    return std::max(0,3-coverage);
}

bool QuantBot::expansionDefenceReady(bool needsLocalSpace) const {
    if (gameMode != GameMode::Custom || !currentGame->isCitySimEnabled() || supportMode
        || getHouse()->getNumItems(Structure_ConstructionYard) == 0) return true;
    const auto& data = currentGame->objectData.data;
    const int house = getHouse()->getHouseID();
    // A base with nowhere left to put a production building cannot finish its
    // core where it stands: the missing factory is the very thing the colony
    // has to make room for. Waive that prerequisite only once the survey has
    // measured the base as built out, so the gate cannot close on itself.
    // Turret cover for existing expansions below is unaffected.
    //
    // A caller that has just failed to find any site at home knows the same
    // thing first-hand, and more recently than the survey: the reported
    // colonist stood idle for a minute because the base measurement briefly
    // reported room again while its rock held no legal footprint at all.
    const bool builtOut = needsLocalSpace
        || (rockSurveyCycle != std::numeric_limits<Uint32>::max() && baseBuiltOut());
    // Complete the core first, then secure each expansion before committing
    // another MCV to an outlying site. Recovery of the only yard is exempt.
    if (!builtOut)
        for (Uint32 item : {Structure_HeavyFactory, Structure_HighTechFactory, Structure_RepairYard})
            if (data[item][house].enabled && data[item][house].techLevel <= currentGame->techLevel
                && getHouse()->getNumItems(item) == 0) return false;
    if (!data[Structure_RocketTurret][house].enabled
        || data[Structure_RocketTurret][house].techLevel > currentGame->techLevel) return true;
    for (const auto* yard:getStructureList())
        if (expansionTurretsMissing(yard,false)>0) return false;
    return true;
}

bool QuantBot::isExpansionYard(Uint32 item, Uint32 objectID, Uint32 mainYardID) {
    // Every yard other than the anchor — and every yard still only planned —
    // belongs to an expansion that has to be covered from scratch.
    return item == Structure_ConstructionYard && mainYardID != NONE_ID && objectID != mainYardID;
}

int QuantBot::lostYardsNear(int x, int y, int radius) const {
    // Yard losses stay in the retained history far longer than the placement
    // block, so a site that keeps eating construction yards stays recognisable
    // after the short-term block expires.
    int lost = 0;
    for (const auto& loss : recentStructureLosses)
        if (loss.item == Structure_ConstructionYard
            && std::max(std::abs(loss.location.x-x),std::abs(loss.location.y-y)) <= radius) ++lost;
    return lost;
}

bool QuantBot::nearRecentStructureLoss(int x, int y, int width, int height) const {
    for (const auto& loss : recentStructureLosses)
        if (CityPlacementPolicy::recentLossBlocks(x, y, width, height,
                loss.location.x, loss.location.y, loss.size.x, loss.size.y,
                getGameCycleCount() - loss.cycle, MILLI2CYCLES(60000))) return true;
    return false;
}

bool QuantBot::overlapsReservedStructure(int x, int y, int width, int height) const {
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder) continue;
        const auto& plan = entry.second;
        const auto size = getStructureSize(plan.item);
        if (CityPlacementPolicy::overlaps(x, y, width, height,
                plan.location.x, plan.location.y, size.x, size.y)) return true;
    }
    return false;
}

namespace {
bool needsGroundExit(Uint32 item) {
    return item == Structure_HeavyFactory || item == Structure_LightFactory
        || item == Structure_Refinery || item == Structure_RepairYard
        || item == Structure_Barracks || item == Structure_WOR
        || item == Structure_StarPort || item == Structure_PoliceStation;
}
bool blocksGroundAccess(Uint32 item) {
    return item != Structure_Road && item != Structure_Slab1 && item != Structure_Slab4;
}
// Check the real build-list gates while allowing only this missing prerequisite.
bool prerequisiteBlocksBuild(const BuilderBase* builder, Uint32 goal, Uint32 prerequisite) {
    if (!builder || !builder->getOwner() || !currentGame || goal >= Num_ItemID) return false;
    const auto& data = currentGame->objectData.data[goal][builder->getOriginalHouseID()];
    if (prerequisite >= data.prerequisiteStructuresSet.size()
        || !data.enabled || data.builder != static_cast<int>(builder->getItemID())
        || data.techLevel > currentGame->techLevel
        || data.upgradeLevel > builder->getCurrentUpgradeLevel()
        || !data.prerequisiteStructuresSet[prerequisite]
        || builder->getOwner()->getNumItems(prerequisite) > 0) return false;
    for (int item = ItemID_FirstID; item < std::min<int>(Num_ItemID, data.prerequisiteStructuresSet.size()); ++item) {
        if (item != static_cast<int>(prerequisite) && isStructure(item)
            && data.prerequisiteStructuresSet[item] && builder->getOwner()->getNumItems(item) <= 0) return false;
    }
    return true;
}
}

void QuantBot::clearPlacementCache(bool geometryChanged, bool reuseForBuilder) {
    // Within a build pass, unreserved yards see the same map and reservation
    // set until an order changes geometry. Reuse successful AND failed searches.
    // A yard with its own reservation excludes that reservation, so its key
    // differs. All order/geometry changes keep the original invalidation.
    const Uint32 excluded=reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    if (geometryChanged || !reuseForBuilder || placementCacheExcludedBuilder!=excluded)
        placementCache.clear();
    placementCacheExcludedBuilder=excluded;
    if (geometryChanged) {
        cityServiceSearch.invalidate();
        cityTurretSearch.invalidate();
    }
}

bool QuantBot::preservesGroundAccess(Uint32 item, Coord pos) {
    if (!blocksGroundAccess(item)) return true;
    if (!pos.isValid()) return false;
    const auto size=getStructureSize(item);
    auto passable=[&](int x,int y) {
        if (!getMap().tileExists(x,y)) return false;
        const auto* tile=getMap().getTile(x,y);
        if (tile->isMountain() || tile->hasAStructure()) return false;
        for (const auto& entry:reservedStructures) {
            if (entry.first==planningBuilder || !blocksGroundAccess(entry.second.item)) continue;
            const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
            if (x>=p.x && x<p.x+extent.x && y>=p.y && y<p.y+extent.y) return false;
        }
        return true;
    };
    if (!GroundAccessPolicy::allows({pos.x,pos.y,size.x,size.y},needsGroundExit(item),passable,
            item==Structure_RocketTurret)) return false;
    auto keepsExit=[&](Coord p,Coord extent) {
        for (int ey=p.y-1;ey<=p.y+extent.y;++ey) for (int ex=p.x-1;ex<=p.x+extent.x;++ex) {
            if (ex>=p.x && ex<p.x+extent.x && ey>=p.y && ey<p.y+extent.y) continue;
            if (ex>=pos.x && ex<pos.x+size.x && ey>=pos.y && ey<pos.y+size.y) continue;
            if (passable(ex,ey)) return true;
        }
        return false;
    };
    // Check only producers touching this placement, so its last deployment
    // opening cannot be covered. No scan of all factories or moving units.
    std::set<Uint32> checked;
    for (int y=pos.y-1;y<=pos.y+size.y;++y) for (int x=pos.x-1;x<=pos.x+size.x;++x) {
        if (x>=pos.x && x<pos.x+size.x && y>=pos.y && y<pos.y+size.y) continue;
        if (!getMap().tileExists(x,y)) continue;
        const auto* object=getMap().getTile(x,y)->getNonInfantryGroundObject();
        if (!object || !object->isAStructure() || !needsGroundExit(object->getItemID())
            || !checked.insert(object->getObjectID()).second) continue;
        if (!keepsExit(object->getLocation(),getStructureSize(object->getItemID()))) return false;
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder || !needsGroundExit(entry.second.item)) continue;
        const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
        if (CityPlacementPolicy::overlaps(pos.x-1,pos.y-1,size.x+2,size.y+2,p.x,p.y,extent.x,extent.y)
            && !keepsExit(p,extent)) return false;
    }
    return true;
}

bool QuantBot::redevelopmentZones(Uint32 item, Coord pos, std::vector<Uint32>& zones) const {
    zones.clear();
    auto* sim = currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    if (!sim || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return false;
    const Coord size = getStructureSize(item);
    bool range = false;
    for (int y=pos.y; y<pos.y+size.y; ++y) for (int x=pos.x; x<pos.x+size.x; ++x) {
        const auto* tile = getMap().getTile(x,y);
        if (!tile || !tile->isRock()) return false;
        range = range || getMap().isWithinBuildRange(x,y,getHouse());
        const auto* object = tile->getNonInfantryGroundObject();
        if (!object) { if (tile->isBlocked() || tile->hasCityZone()) return false; continue; }
        const auto* zone = dynamic_cast<const ZoneStructure*>(object);
        if (!zone || zone->getOwner() != getHouse()) return false;
        const Coord z = zone->getLocation();
        if (sim->getLandValueMap().worldGet(z.x,z.y) > 64
            || zone->getCivicOverlay() != ZoneStructure::CivicOverlay::None) return false;
        if (std::find(zones.begin(),zones.end(),zone->getObjectID()) == zones.end()) zones.push_back(zone->getObjectID());
    }
    return range && !zones.empty() && zones.size() <= 4;
}

Coord QuantBot::findRedevelopmentSite(Uint32 item) {
    AITelemetry::PerformanceScope perfScope("ai.findRedevelopmentSite", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame->isCitySimEnabled()
        || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return Coord::Invalid();
    const Coord size=getStructureSize(item), base=findBaseCentre(getHouse()->getHouseID());
    Coord best=Coord::Invalid();
    int bestScore=std::numeric_limits<int>::max();
    auto bestFactoryRank=TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    auto bestReactorRank=TacticalSafetyPolicy::reactorSiteRank(100000,100000,-1,false,0,0);
    auto* sim=currentGame->getCitySimulation();
    if (!sim) return best;
    for (int y=std::max(0,base.y-50); y<=std::min(getMap().getSizeY()-size.y,base.y+50); ++y)
        for (int x=std::max(0,base.x-50); x<=std::min(getMap().getSizeX()-size.x,base.x+50); ++x) {
            const Coord pos(x,y);
            std::vector<Uint32> zones;
            if (!redevelopmentZones(item,pos,zones) || overlapsReservedStructure(x,y,size.x,size.y)
                || nearRecentStructureLoss(x,y,size.x,size.y) || dangerAt(pos,size)>0
                || !reactorClearance(item,pos) || !preservesGroundAccess(item,pos) || !cityRoadImpact(getMap(),x,y,size.x,size.y,item).preservesConnections) continue;
            int score=static_cast<int>(zones.size())*100;
            for (Uint32 id : zones) {
                const auto* zone=static_cast<const ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                const Coord z=zone->getLocation();
                const int density=std::max(int(getMap().getTile(z.x,z.y)->getCityZoneDensity()),
                    zone->getResidentialPopulation() > 0 ? 1 : 0);
                const auto& state=sim->getHouseState(getHouse()->getHouseID());
                const bool residential=zone->getItemID()==Structure_ZoneResidential;
                const int demand=residential ? state.resValve : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                score += RedevelopmentPolicy::displacementCost(density,sim->getLandValueMap().worldGet(z.x,z.y),demand,residential?2000:1500);
            }
            score += blockDistance(pos,base).lround()-rearScore(pos,base);
            const auto rank=TacticalSafetyPolicy::factorySiteRank(dangerAt(pos,size,true),
                TacticalSafetyPolicy::footprintClearance(factoryEnemyClearance,getMap().getSizeX(),getMap().getSizeY(),
                    x,y,size.x,size.y),0,-score);
            const auto reactorRank=TacticalSafetyPolicy::reactorSiteRank(0,dangerAt(pos,size,true),
                TacticalSafetyPolicy::footprintClearance(factoryEnemyClearance,getMap().getSizeX(),getMap().getSizeY(),
                    x,y,size.x,size.y),true,rearScore(pos,base),-score);
            if (item==Structure_NuclearPlant ? reactorRank>bestReactorRank
                : TacticalSafetyPolicy::productionFactory(item) ? rank>bestFactoryRank : score<bestScore) {
                bestReactorRank=reactorRank; bestFactoryRank=rank; bestScore=score; best=pos;
            }
        }
    return best;
}

Coord QuantBot::findPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    refreshTacticalDanger();
    int accessRejected = 0, pollutionRejected = 0, reservedRejected = 0, roadRejected = 0, neighbourRejected = 0;
    int productionPlotRejected = 0;
    int searchPassUsed = 0;
	// Check per-build-cycle cache first
	auto cacheIt = placementCache.find(itemID);
	if (cacheIt != placementCache.end()) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"ai.placement_cache_hit",1,itemID,false);
		return cacheIt->second;
	}
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"ai.placement_search",1,itemID,false);

	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	int bestLocationScore = std::numeric_limits<int>::min();
	Coord bestLocation = Coord::Invalid();
    int bestSiteTier = -1;
    int bestInfill=0;
    bool bestSafe=false;
    const bool factoryPlacement = TacticalSafetyPolicy::productionFactory(itemID);
    auto bestFactoryRank = TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    AITelemetry::Record bestQuality;
    auto bestReactorRank = TacticalSafetyPolicy::reactorSiteRank(100000,100000,-1,false,0,std::numeric_limits<int>::min());
    int candidates = 0, threatRejected = 0, blastRejected = 0, lossRejected = 0;

	bool itemIsBuilder = (itemID == Structure_HeavyFactory
		|| itemID == Structure_RepairYard
		|| itemID == Structure_LightFactory
		|| itemID == Structure_WOR
		|| itemID == Structure_Barracks
		|| itemID == Structure_StarPort);

	// City zones follow road-frontage rules instead of the compact-base
	// scoring: they sit next to each other or one road tile apart, and never
	// pack into blocks that landlock the inner lots.
	const bool cityZonePlacement = currentGame && currentGame->isCitySimEnabled()
		&& DuneCity::isCityZoneStructure(itemID);
	const int houseID = getHouse()->getHouseID();

	// Bound search to radius around base center instead of scanning entire map
	int searchRadius = 50;
	int mapW = getMap().getSizeX();
	int mapH = getMap().getSizeY();
	int startX = std::max(0, baseCenter.x - searchRadius);
	int startY = std::max(0, baseCenter.y - searchRadius);
	int endX = std::min(mapW - newSizeX, baseCenter.x + searchRadius);
	int endY = std::min(mapH - newSizeY, baseCenter.y + searchRadius);

    // Snapshot only the zones used by proximity scoring. A bounded index
    // replaces the full structure-list walk for every candidate origin.
    struct ZoneNeighbour { Coord location; int item; };
    std::vector<ZoneNeighbour> zoneNeighbours;
    LocalPointIndex zoneIndex(mapW,mapH);
    if (DuneCity::isCityZoneStructure(itemID)) {
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || !DuneCity::isCityZoneStructure(structure->getItemID())) continue;
            const auto location=structure->getLocation();
            zoneIndex.add(location.x,location.y,zoneNeighbours.size());
            zoneNeighbours.push_back({location,structure->getItemID()});
        }
    }

    // Resolve nearest origins and full-footprint pollution separation once,
    // instead of scanning every neighbour again at every candidate. These
    // fields include the same live buildings and other yards' reservations.
    // Build lazily: crowded maps often reject every site before scoring.
    auto* citySim = currentGame && currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    const auto newRole = DuneCity::getStructureCityRole(itemID);
    const bool newSensitive = newRole == DuneCity::CityRole::Residential || newRole == DuneCity::CityRole::Commercial;
    const bool newPolluter = DuneCity::getPollutionEmission(itemID, DuneCity::getStructureMaxLevel(itemID)) > 0;
    const bool needCityDistances = citySim && (newSensitive || newPolluter);
    bool cityDistancesReady=false;
    CityDistanceField nearestResidential(0,0),nearestCommercial(0,0),
        nearestIndustrial(0,0),pollutionSeparation(0,0);
    auto addNeighbour = [&](Uint32 item, Coord location, Coord size) {
        const auto role = DuneCity::getStructureCityRole(item);
        if (role == DuneCity::CityRole::Residential) nearestResidential.add(location.x,location.y);
        if (role == DuneCity::CityRole::Commercial) nearestCommercial.add(location.x,location.y);
        if (role == DuneCity::CityRole::Industrial) nearestIndustrial.add(location.x,location.y);
        const bool pollutes=DuneCity::getPollutionEmission(item,DuneCity::getStructureMaxLevel(item))>0;
        const bool sensitive=role==DuneCity::CityRole::Residential || role==DuneCity::CityRole::Commercial;
        if ((newSensitive && pollutes) || (newPolluter && sensitive))
            pollutionSeparation.add(location.x,location.y,size.x,size.y);
    };
    auto prepareCityDistances = [&] {
        if (cityDistancesReady) return;
        cityDistancesReady=true;
        nearestResidential=CityDistanceField(mapW,mapH); nearestCommercial=CityDistanceField(mapW,mapH);
        nearestIndustrial=CityDistanceField(mapW,mapH); pollutionSeparation=CityDistanceField(mapW,mapH);
        for (const auto* structure : getStructureList())
            if (structure->getOwner() == getHouse() && structure->getHealth() > 0)
                addNeighbour(structure->getItemID(), structure->getLocation(), structure->getStructureSize());
        for (const auto& entry : reservedStructures)
            if (entry.first != planningBuilder)
                addNeighbour(entry.second.item, entry.second.location, getStructureSize(entry.second.item));
        nearestResidential.build(); nearestCommercial.build(); nearestIndustrial.build(); pollutionSeparation.build();
    };

	// Pre-collect spice tile positions for refinery placement (avoids O(N^2) inner loop)
	std::vector<Coord> spiceTiles;
	if (itemID == Structure_Refinery) {
		// Only scan spice in a wider area around the base (no need for full map)
		int spiceRadius = searchRadius + 30;
		int spStartX = std::max(0, baseCenter.x - spiceRadius);
		int spStartY = std::max(0, baseCenter.y - spiceRadius);
		int spEndX = std::min(mapW - 1, baseCenter.x + spiceRadius);
		int spEndY = std::min(mapH - 1, baseCenter.y + spiceRadius);
		for (int sx = spStartX; sx <= spEndX; sx++) {
			for (int sy = spStartY; sy <= spEndY; sy++) {
				if (getMap().tileExists(sx, sy) && getMap().getTile(sx, sy)->hasSpice()) {
					spiceTiles.emplace_back(sx, sy);
				}
			}
		}
	}

    // A cramped city keeps legal ground that no longer suits the road grid.
    // Roads are a layout preference; production capacity is not. Factories,
    // builders and repair yards may take such a site once the ordinary search
    // has failed, so the base is never called full while it can still host the
    // building an engine placement check accepts.
    const bool roadingFallbackItem = currentGame->isCitySimEnabled() && (itemIsBuilder || factoryPlacement);
    bool roadingRelaxed = false;

    // Keep the fast ordinary search, but never treat its averaged base centre
    // as the limit of a spread-out city's buildable territory.
    for (int searchPass=0; searchPass<3; ++searchPass) {
        if (searchPass==1) {
            if (bestLocation.isValid() && itemID != Structure_NuclearPlant) break;
            startX=0; startY=0; endX=mapW-newSizeX; endY=mapH-newSizeY;
        }
        if (searchPass==2 && (bestLocation.isValid() || !roadingFallbackItem)) break;
        // Only the road-layout preferences are dropped. Engine placement, ground
        // egress, reservations, blast clearance and threat rules still decide.
        const bool relaxRoading = searchPass==2;
        searchPassUsed=searchPass;
	for (int placeLocationX = startX; placeLocationX <= endX; placeLocationX++) {
		for (int placeLocationY = startY; placeLocationY <= endY; placeLocationY++) {
			if (!relaxRoading && !CityPlacementPolicy::inPlacementSearchPass(placeLocationX,placeLocationY,
                baseCenter.x,baseCenter.y,searchRadius,searchPass)) continue;
            // First check if this location is valid for building
			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, newSizeX, newSizeY,
				false, (itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {

                // A zone may spill onto sand, but slabs cannot. When foundations
                // are enabled, choose a footprint that can actually be prepared.
                if (cityZonePlacement && getGameInitSettings().getGameOptions().concreteRequired
                    && !getMap().okayToPlaceStructure(placeLocationX,placeLocationY,newSizeX,newSizeY,false,getHouse())) continue;
                ++candidates;
                if (overlapsReservedStructure(placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++reservedRejected; continue; }
                if ((cityZonePlacement || planningCityProductionPlots)
                    && std::any_of(cityProductionPlots.begin(),cityProductionPlots.end(),[&](const auto& plot) {
                        const auto size=getStructureSize(plot.item);
                        // Leave access around the future factory/repair footprint.
                        return CityPlacementPolicy::overlaps(placeLocationX,placeLocationY,newSizeX,newSizeY,
                            plot.location.x-1,plot.location.y-1,size.x+2,size.y+2);
                    })) { ++productionPlotRejected; continue; }
                // Use the same origin sample and role-specific gate as zone growth.
                // Industry tolerates pollution; R/C must not become vacant dead lots.
                if (citySim && cityZonePlacement && DuneCity::isPollutionBlockingGrowth(
                    citySim->getPollutionDensityMap().worldGet(placeLocationX, placeLocationY), newRole, 1)) {
                    ++pollutionRejected;
                    continue;
                }
                if (!preservesGroundAccess(itemID,Coord(placeLocationX,placeLocationY))) { ++accessRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall
                    && itemID != Structure_NuclearPlant
                    && nearRecentStructureLoss(placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++lossRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall) {
                    if (dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY)) > 0) { ++threatRejected; continue; }
                    if (!TacticalSafetyPolicy::reactorPlacementAllowed(itemID, reactorClearance(itemID, Coord(placeLocationX, placeLocationY)))) { ++blastRejected; continue; }
                }
                const auto roads = cityRoadImpact(getMap(), placeLocationX, placeLocationY, newSizeX, newSizeY, itemID);
                if (!relaxRoading && !roads.preservesConnections) { ++roadRejected; continue; }
                if (!relaxRoading && currentGame && currentGame->isCitySimEnabled()
                    && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++neighbourRejected; continue; }
				int locationScore = 0;
                const int blockBonus = cityZonePlacement ? fourZoneBlockBonus(getMap(),houseID,placeLocationX,placeLocationY) : 0;
                locationScore += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				int placeLocationEndX = placeLocationX + newSizeX;
				int placeLocationEndY = placeLocationY + newSizeY;

		// Big bonus if building is directly at the map edge
		bool atMapEdge = (placeLocationX == 0 || placeLocationX + newSizeX >= getMap().getSizeX() ||
		                  placeLocationY == 0 || placeLocationY + newSizeY >= getMap().getSizeY());
		if (atMapEdge) {
			locationScore += 12;  // Bonus for edge placement
		}

			// Count adjacent friendly structures and track unique buildings per side
			int adjacentFriendlyStructureTiles = 0;
			int oneTileGapFriendlyStructureTiles = 0;
			std::set<Uint32> northSideBuildings;  // Buildings touching north side
			std::set<Uint32> southSideBuildings;  // Buildings touching south side
			std::set<Uint32> eastSideBuildings;   // Buildings touching east side
			std::set<Uint32> westSideBuildings;   // Buildings touching west side

			// Evaluate surrounding tiles
			for (int i = placeLocationX - 1; i <= placeLocationEndX; i++) {
				for (int j = placeLocationY - 1; j <= placeLocationEndY; j++) {
					if (getMap().tileExists(i, j) && (getMap().getSizeX() > i) && (0 <= i) && (getMap().getSizeY() > j) && (0 <= j)) {
					if (getMap().getTile(i, j)->hasAStructure()) {
						// Favor being near our buildings, avoid enemy buildings
						if (getMap().getTile(i, j)->getOwner() == getHouse()->getHouseID()) {
							adjacentFriendlyStructureTiles++;
							locationScore += cityZonePlacement ? 0 : 10;  // compact bases only; lots keep frontage instead

							// Track which side this building is on and which building it is
							const ObjectBase* pObject = getMap().getTile(i, j)->getObject();
							if (pObject) {
								Uint32 buildingID = pObject->getObjectID();

								// North side (j == placeLocationY - 1)
								if (j == placeLocationY - 1 && i >= placeLocationX && i < placeLocationEndX) {
									northSideBuildings.insert(buildingID);
								}
								// South side (j == placeLocationEndY)
								if (j == placeLocationEndY && i >= placeLocationX && i < placeLocationEndX) {
									southSideBuildings.insert(buildingID);
								}
								// West side (i == placeLocationX - 1)
								if (i == placeLocationX - 1 && j >= placeLocationY && j < placeLocationEndY) {
									westSideBuildings.insert(buildingID);
								}
								// East side (i == placeLocationEndX)
								if (i == placeLocationEndX && j >= placeLocationY && j < placeLocationEndY) {
									eastSideBuildings.insert(buildingID);
								}
							}
						}
						else {
							locationScore -= 10;
						}
					}
					else if (!getMap().getTile(i, j)->isRock() && !getMap().getTile(i, j)->hasPreparedFoundation()) {
						// Favor non-rock tiles (open buildable terrain)
						locationScore += 4;
					}
				else if (getMap().getTile(i, j)->hasAGroundObject()) {
					if (getMap().getTile(i, j)->getOwner() != getHouse()->getHouseID()) {
						// Avoid building next to enemy units
						locationScore -= 100;
					}
					// No penalty for own units
				}
					}
		// Don't penalize tiles outside map - edge placement should be encouraged
			}
		}

		// A second perimeter identifies structures separated by exactly one tile.
		// This lets some bases form lanes and courtyards without using randomness,
		// which would risk multiplayer lockstep divergence.
		for (int i = placeLocationX - 2; i <= placeLocationEndX + 1; i++) {
			for (int j = placeLocationY - 2; j <= placeLocationEndY + 1; j++) {
				const bool onOuterRing = (i == placeLocationX - 2 || i == placeLocationEndX + 1
					|| j == placeLocationY - 2 || j == placeLocationEndY + 1);
				if (!onOuterRing || !getMap().tileExists(i, j)) {
					continue;
				}
				const Tile* tile = getMap().getTile(i, j);
				if (tile->hasAStructure() && tile->getOwner() == getHouse()->getHouseID()) {
					oneTileGapFriendlyStructureTiles++;
				}
			}
		}

	// Penalty if any single side is touching multiple different buildings (gap-filling)
	int sidesWithMultipleBuildings = 0;
	if (northSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (southSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (eastSideBuildings.size() > 1) sidesWithMultipleBuildings++;
	if (westSideBuildings.size() > 1) sidesWithMultipleBuildings++;

	if (sidesWithMultipleBuildings > 0) {
		// BAD: At least one side is touching multiple buildings (gap-filling)
		// Penalty should be smaller than benefit of adjacency to discourage but not completely prohibit
		locationScore -= 10 * sidesWithMultipleBuildings;
	}

	// Deterministically vary ordinary base spacing. One third of placements
	// prefer a one-cell lane, one third remain compact, and one third are
	// neutral. Defensive pieces, slabs, and refineries retain their specialist
	// placement behavior.
	const bool supportsVariedSpacing = !(currentGame && currentGame->isCitySimEnabled())
		&& getHouse()->getNumStructures() >= 3
		&& itemID != Structure_GunTurret
		&& itemID != Structure_RocketTurret
		&& itemID != Structure_Wall
		&& itemID != Structure_Slab1
		&& itemID != Structure_Slab4
		&& itemID != Structure_Refinery;
	if (supportsVariedSpacing) {
		const int spacingStyle = (getHouse()->getHouseID() * 37 + itemID * 17
			+ getHouse()->getNumStructures()) % 3;
		if (spacingStyle == 0) {
			locationScore -= adjacentFriendlyStructureTiles * 16;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 8) * 6;
		} else if (spacingStyle == 2) {
			locationScore -= adjacentFriendlyStructureTiles * 5;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 4) * 2;
		}
	}

	// Bonus for building on concrete tiles
	for (int i = placeLocationX; i < placeLocationEndX; i++) {
		for (int j = placeLocationY; j < placeLocationEndY; j++) {
			if (getMap().tileExists(i, j) && getMap().getTile(i, j)->hasPreparedFoundation()) {
				locationScore += 2;  // Small bonus - concrete protects from damage
			}
		}
	}

		// Building-specific positioning
		if (itemIsBuilder || factoryPlacement || itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
            if (!factoryPlacement)
			    locationScore -= lround(blockDistance(squadRallyLocation, Coord(placeLocationX, placeLocationY)));
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else if (itemID == Structure_Refinery) {
			// Refineries prefer being close to spice deposits
			int closestSpiceDistance = 10000;
			for (const auto& spiceCoord : spiceTiles) {
				int spiceDistance = lround(blockDistance(Coord(placeLocationX, placeLocationY), spiceCoord));
				if (spiceDistance < closestSpiceDistance) {
					closestSpiceDistance = spiceDistance;
				}
			}
			if (closestSpiceDistance < 10000) {
				locationScore += 50 - closestSpiceDistance * 2; // Strong bonus for being closer to spice
			}

			// Bonus for adjacent sand tiles (harvester access)
			// Double bonus if the sand has spice
			Coord structureSize = getStructureSize(itemID);
			for (int adjX = placeLocationX - 1; adjX <= placeLocationX + structureSize.x; adjX++) {
				for (int adjY = placeLocationY - 1; adjY <= placeLocationY + structureSize.y; adjY++) {
					// Skip tiles inside the structure footprint
					if (adjX >= placeLocationX && adjX < placeLocationX + structureSize.x &&
						adjY >= placeLocationY && adjY < placeLocationY + structureSize.y) {
						continue;
					}
					if (getMap().tileExists(adjX, adjY)) {
						const Tile* pTile = getMap().getTile(adjX, adjY);
						if (pTile->isSand()) {
							if (pTile->hasSpice()) {
								locationScore += 6; // Double bonus for sand with spice
							} else {
								locationScore += 3; // Base bonus for sand
							}
						}
					}
				}
			}

			// Also apply base center distance penalty (but weaker than spice bonus)
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else {
			// For other buildings, apply base center distance penalty
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		}

		// === CITY MODE: grid alignment + road spacing + zone-type scoring ===
		if (currentGame && currentGame->isCitySimEnabled()) {

			// Grid alignment: snap to a 3-cell grid (2-tile footprint +
			// 1-tile road gap) anchored on the base centre. Positions
			// that land on grid intersections get a massive bonus so the
			// AI naturally builds in neat rows with roads between.
			int gridOffsetX = ((placeLocationX - baseCenter.x) % 3 + 3) % 3;
			int gridOffsetY = ((placeLocationY - baseCenter.y) % 3 + 3) % 3;
            if (cityZonePlacement) {
                locationScore += blockBonus;
                if (CityPlacementPolicy::fourZoneGridSlot(placeLocationX,placeLocationY,baseCenter.x,baseCenter.y)) locationScore += 60;
            } else if (gridOffsetX == 0 && gridOffsetY == 0) {
				locationScore += 40;  // grid alignment bonus
			} else if (cityZonePlacement && alignedWithNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				locationScore += 50;  // continues a row of lots: touching or one road tile apart
			} else {
				locationScore -= 40;  // off-grid penalty
			}

			// Road-spacing: check 4 sides for road / open / structure.
			int sidesWithRoad = 0;
			int sidesWithOpen = 0;
			int sidesTouchingStructure = 0;

			struct SideCheck { int startI, startJ, endI, endJ; };
			SideCheck sides[4] = {
				{ placeLocationX, placeLocationY - 1, placeLocationEndX, placeLocationY },
				{ placeLocationX, placeLocationEndY, placeLocationEndX, placeLocationEndY + 1 },
				{ placeLocationX - 1, placeLocationY, placeLocationX, placeLocationEndY },
				{ placeLocationEndX, placeLocationY, placeLocationEndX + 1, placeLocationEndY }
			};

			for (const auto& side : sides) {
				bool sideHasRoad = false, sideHasOpen = false, sideTouchesStruct = false;
				for (int si = side.startI; si < side.endI; si++) {
					for (int sj = side.startJ; sj < side.endJ; sj++) {
						if (!getMap().tileExists(si, sj)) continue;
						const Tile* t = getMap().getTile(si, sj);
						if (t->isRoad()) sideHasRoad = true;
						else if (t->hasAStructure() || t->hasCityZone()) sideTouchesStruct = true;
						else if (t->isRock() && !t->isMountain() && !t->hasAGroundObject()) sideHasOpen = true;
					}
				}
				if (sideHasRoad) sidesWithRoad++;
				if (sideHasOpen) sidesWithOpen++;
				if (sideTouchesStruct) sidesTouchingStructure++;
			}

			// Road or open rock frontage is what a well-laid-out city wants. The
			// relaxed pass keeps only the access the engine enforces, which
			// preservesGroundAccess() has already checked on this footprint.
			if (!relaxRoading && sidesWithRoad == 0 && sidesWithOpen == 0) {
				continue;  // landlocked — skip
			}
			if (cityZonePlacement && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				continue;  // would take a neighbour's last road frontage
			}
			locationScore += sidesWithRoad * 25;
			locationScore += sidesWithOpen * 5;
			locationScore -= sidesTouchingStructure * (cityZonePlacement && blockBonus>0 ? 0 : 30);

			// Zone-type proximity scoring:
			// R/C avoid industrial pollution (radius 5) but want it
			// within supply range (16). I clusters with itself and
			// wants residential nearby for workers.
			bool isResidential = (itemID == Structure_ZoneResidential);
			bool isCommercial  = (itemID == Structure_ZoneCommercial);
			bool isIndustrial  = (itemID == Structure_ZoneIndustrial);

			if (isResidential || isCommercial || isIndustrial) {
				// Favour lots that use sand so rock stays free for Dune
				// structures. The placement check already guarantees at
				// least one rock tile under the lot.
				for (int px = placeLocationX; px < placeLocationEndX; px++) {
					for (int py = placeLocationY; py < placeLocationEndY; py++) {
						if (getMap().tileExists(px, py)) {
							const Tile* t = getMap().getTile(px, py);
							if (t->isSand() || t->isDunes()) locationScore += 6;
						}
					}
				}

                int closestIndDist = 100, closestResDist = 100, closestComDist = 100;
                int nearbyRes = 0, nearbyCom = 0, nearbyInd = 0;
                zoneIndex.visit(placeLocationX,placeLocationY,16,[&](size_t index) {
                    const auto& zone=zoneNeighbours[index];
                    const int dist=lround(blockDistance(Coord(placeLocationX,placeLocationY),zone.location));
                    if (dist>16) return;
                    if (zone.item==Structure_ZoneIndustrial) { ++nearbyInd; closestIndDist=std::min(closestIndDist,dist); }
                    if (zone.item==Structure_ZoneResidential) { ++nearbyRes; closestResDist=std::min(closestResDist,dist); }
                    if (zone.item==Structure_ZoneCommercial) { ++nearbyCom; closestComDist=std::min(closestComDist,dist); }
                });

				if (isResidential || isCommercial) {
					auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;

					// Penalty if within pollution radius of industrial
					if (closestIndDist <= 5) {
						locationScore -= 50;
					}
					// Bonus if industrial is reachable but outside pollution
					else if (closestIndDist <= 16) {
						locationScore += 20;
					}

					// Pollution density penalty (city sim layer)
					if (citySim) {
						const auto& polMap = citySim->getPollutionDensityMap();
						int totalPollution = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalPollution += polMap.worldGet(px, py);
							}
						}
						locationScore -= totalPollution / 10;
					}

					// Crime rate penalty (city sim layer)
					if (citySim) {
						const auto& crimeMap = citySim->getCrimeRateMap();
						int totalCrime = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalCrime += crimeMap.worldGet(px, py);
							}
						}
						locationScore -= totalCrime / 5;
					}

					// Sand adjacency bonus — higher land value near sand/desert
					Coord zoneSize = getStructureSize(itemID);
					int sandBonus = 0;
					for (int adjX = placeLocationX - 1; adjX <= placeLocationX + zoneSize.x; adjX++) {
						for (int adjY = placeLocationY - 1; adjY <= placeLocationY + zoneSize.y; adjY++) {
							if (adjX >= placeLocationX && adjX < placeLocationX + zoneSize.x &&
								adjY >= placeLocationY && adjY < placeLocationY + zoneSize.y)
								continue;
							if (getMap().tileExists(adjX, adjY) && getMap().getTile(adjX, adjY)->isSand())
								sandBonus += 5;
						}
					}
					locationScore += sandBonus;

					if (isResidential) {
						// Employment access: bonus if C or I zones reachable
						if (nearbyCom > 0 || nearbyInd > 0) locationScore += 20;
						// Extra for having both (mixed economy nearby)
						if (nearbyCom > 0 && nearbyInd > 0) locationScore += 10;
						// R ↔ C synergy
						if (nearbyCom > 0) locationScore += 15;
					}

					if (isCommercial) {
						// Commercial wants both R (customers) and I (supply) nearby
						if (nearbyRes > 0) locationScore += 20;
						if (nearbyInd > 0) locationScore += 15;
						// Strong bonus for being between R and I
						if (nearbyRes > 0 && nearbyInd > 0) locationScore += 15;
					}
				}

				if (isIndustrial) {
					// I clusters with other I (pollution doesn't affect I)
					locationScore += nearbyInd * 10;

					// I should stay away from R/C to avoid polluting them
					// but within commute distance (6-16 tiles = sweet spot)
                    // All following distance decisions use only the 6/16 thresholds;
                    // a missing neighbour within 16 has the same score as any
                    // more distant neighbour, so reuse the bounded query above.

					// Sweet spot: outside pollution radius but within commute
					if (closestResDist >= 6 && closestResDist <= 16) {
						locationScore += 25;
					} else if (closestResDist < 6) {
						locationScore -= 30;  // too close — will pollute residential
					} else if (closestResDist > 16) {
						locationScore -= 10;  // too far — no workers
					}

					if (closestComDist >= 6 && closestComDist <= 16) {
						locationScore += 15;
					} else if (closestComDist < 6) {
						locationScore -= 20;  // too close to commercial
					}
				}
			}
		}

                int siteTier = 0;
                AITelemetry::Record quality;
                quality.set("four_zone_block_bonus",blockBonus);
                if (needCityDistances) {
                    prepareCityDistances();
                    const int separation=pollutionSeparation.footprint(placeLocationX,placeLocationY,newSizeX,newSizeY);
                    const int nearestR=nearestResidential.get(placeLocationX,placeLocationY);
                    const int nearestC=nearestCommercial.get(placeLocationX,placeLocationY);
                    const int nearestI=nearestIndustrial.get(placeLocationX,placeLocationY);
                    auto inReachOrMissing = [](int distance) { return distance == 1000000 || distance <= DuneCity::kSupplyRadius; };
                    const bool withinSupply = newRole == DuneCity::CityRole::Residential
                        ? inReachOrMissing(std::min(nearestC, nearestI))
                        : newRole == DuneCity::CityRole::Commercial
                            ? inReachOrMissing(nearestR) && inReachOrMissing(nearestI)
                            : inReachOrMissing(nearestR);
                    siteTier = CityPlacementPolicy::cityPlacementTier(withinSupply, separation);
                    quality.set("supply_reachable", withinSupply).set("pollution_buffer_tiles", separation == 1000000 ? -1 : separation)
                        .set("nearest_res_origin", nearestR == 1000000 ? -1 : nearestR)
                        .set("nearest_com_origin", nearestC == 1000000 ? -1 : nearestC)
                        .set("nearest_ind_origin", nearestI == 1000000 ? -1 : nearestI);
                    locationScore += CityPlacementPolicy::pollutionSeparationScore(separation);
                    if (newSensitive) {
                        int pollution = 0, value = 0, traffic = 0, sand = 0;
                        for (int x = placeLocationX; x < placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY; y < placeLocationY+newSizeY; ++y) {
                                pollution += citySim->getPollutionDensityMap().worldGet(x, y);
                                value += citySim->getLandValueMap().worldGet(x, y);
                                traffic += citySim->getTrafficDensityMap().worldGet(x, y);
                            }
                        for (int x = placeLocationX-1; x <= placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY-1; y <= placeLocationY+newSizeY; ++y) {
                                if (x >= placeLocationX && x < placeLocationX+newSizeX
                                    && y >= placeLocationY && y < placeLocationY+newSizeY) continue;
                                if (!getMap().tileExists(x,y)) continue;
                                const auto* tile = getMap().getTile(x,y);
                                if (!tile->hasAStructure() && (tile->isSand() || tile->isDunes())) ++sand;
                            }
                        const int meanPollution = pollution/(newSizeX*newSizeY);
                        const int meanTraffic = traffic/(newSizeX*newSizeY);
                        siteTier = CityPlacementPolicy::sensitivePlacementTier(withinSupply, separation,
                            meanPollution, meanTraffic);
                        quality.set("mean_pollution", meanPollution).set("mean_traffic", meanTraffic)
                            .set("mean_land_value", value/(newSizeX*newSizeY)).set("adjacent_sand", sand);
                        locationScore += CityPlacementPolicy::residentialCommercialEnvironmentScore(
                            meanPollution, value/(newSizeX*newSizeY), sand, meanTraffic);
                    }
                }

                const int infill = cityZonePlacement && itemID==Structure_ZoneResidential
                    ? residentialInfillSides(getMap(),houseID,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                quality.set("residential_infill_sides",infill);
                const int lossRisk = dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY), true);
                // Safety outranks pollution/grid preferences; losses decay over five minutes.
                siteTier += lossRisk == 0 ? 6 : 0;
                locationScore -= lossRisk * 5;
                const int rear = (itemID == Structure_NuclearPlant || factoryPlacement) ? rearScore(Coord(placeLocationX, placeLocationY), baseCenter) : 0;
                locationScore += rear;
                const int enemyClearance = (factoryPlacement || itemID == Structure_NuclearPlant) ? TacticalSafetyPolicy::footprintClearance(
                    factoryEnemyClearance,mapW,mapH,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                const auto factoryRank = TacticalSafetyPolicy::factorySiteRank(lossRisk,enemyClearance,siteTier,locationScore);
                const bool clearsReactor = itemID != Structure_NuclearPlant || reactorClearance(itemID,Coord(placeLocationX,placeLocationY));
                const int fireRisk = itemID == Structure_NuclearPlant ? dangerAt(Coord(placeLocationX,placeLocationY),Coord(newSizeX,newSizeY)) : 0;
                const auto reactorRank = TacticalSafetyPolicy::reactorSiteRank(fireRisk,lossRisk,enemyClearance,clearsReactor,rear,locationScore);
                quality.set("enemy_clearance_tiles", enemyClearance)
                    .set("recent_loss_risk", lossRisk).set("enemy_fire_risk", fireRisk)
                    .set("rear_score", rear).set("reactor_clearance", clearsReactor);

				// Pick this location if it has the best score
				if (itemID == Structure_NuclearPlant ? reactorRank > bestReactorRank
                    : factoryPlacement ? factoryRank > bestFactoryRank
                    : CityPlacementPolicy::preferCitySite(lossRisk==0,infill,siteTier,locationScore,
                        bestSafe,bestInfill,bestSiteTier,bestLocationScore)) {
                    bestReactorRank = reactorRank;
                    bestFactoryRank = factoryRank;
                    bestSiteTier = siteTier;
                    bestSafe=lossRisk==0; bestInfill=infill;
                    roadingRelaxed = relaxRoading;
                    bestQuality = quality.set("tier", siteTier).set("score", locationScore);
					bestLocationScore = locationScore;
					bestLocation = Coord(placeLocationX, placeLocationY);
				}
			}
		}
	}

    } // search passes
    if (bestLocation.isInvalid()) {
        bestLocation = findRedevelopmentSite(itemID);
        if (bestLocation.isValid()) { bestQuality.set("redevelopment",1); roadingRelaxed=false; }
    }
	placementCache[itemID] = bestLocation;
    bestQuality.set("roading_relaxed",roadingRelaxed)
        .set("legal_candidates",candidates).set("threat_rejections",threatRejected)
        .set("blast_rejections",blastRejected).set("recent_loss_rejections",lossRejected)
        .set("search_pass",searchPassUsed).set("search_center_x",baseCenter.x).set("search_center_y",baseCenter.y)
        .set("production_plot_rejections",productionPlotRejected)
        .set("reserved_rejections",reservedRejected).set("road_rejections",roadRejected).set("neighbour_rejections",neighbourRejected)
        .set("ground_access_rejections",accessRejected).set("pollution_rejections",pollutionRejected);
    placementScoreDetails[itemID] = bestQuality;
	return bestLocation;
}

Coord QuantBot::findSlabPlaceLocation(Uint32 itemID) {
	int slabSizeX = getStructureSize(itemID).x;
	int slabSizeY = getStructureSize(itemID).y;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();

	// Check all map tiles for valid slab placement
	for (int x = 0; x <= getMap().getSizeX() - slabSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - slabSizeY; y++) {
			// Check if this location is valid for slab placement
			if (getMap().okayToPlaceStructure(x, y, slabSizeX, slabSizeY, false, getHouse())) {

				int locationScore = 0;
				bool hasExistingSlab = false;

				// Check if any of the slab tiles already have concrete
				for (int i = x; i < x + slabSizeX; i++) {
					for (int j = y; j < y + slabSizeY; j++) {
						if (getMap().getTile(i, j)->hasPreparedFoundation()) {
							hasExistingSlab = true;
							break;
						}
					}
					if (hasExistingSlab) break;
				}

				// Skip if already has concrete - we don't want to place over existing slabs
				if (hasExistingSlab) {
					continue;
				}

			// Count adjacent tiles - favor building next to existing buildings or concrete
			int adjacentStructureTiles = 0;
			int adjacentConcreteTiles = 0;
			int adjacentRockTiles = 0;

			for (int i = x - 1; i <= x + slabSizeX; i++) {
				for (int j = y - 1; j <= y + slabSizeY; j++) {
					if (getMap().tileExists(i, j)) {
						const Tile* pTile = getMap().getTile(i, j);

						// Check if this is directly adjacent (edge-touching, not diagonal)
						bool isDirectlyAdjacent = ((i == x - 1 || i == x + slabSizeX) && j >= y && j < y + slabSizeY) ||
						                          ((j == y - 1 || j == y + slabSizeY) && i >= x && i < x + slabSizeX);

						if (isDirectlyAdjacent) {
							// Count structures that are directly adjacent (highest priority)
							if (pTile->hasAStructure() && pTile->getOwner() == getHouse()->getHouseID()) {
								adjacentStructureTiles++;
							}
							// Count concrete tiles that are directly adjacent (second priority)
							else if (pTile->hasPreparedFoundation()) {
							adjacentConcreteTiles++;
						}
							// Count rock tiles that are directly adjacent (room to expand)
							else if (pTile->isRock() && !pTile->isMountain()) {
							adjacentRockTiles++;
							}
						}
					}
				}
			}

		// SCORING: Favor building next to existing buildings or concrete
		// 1. Highest priority: directly adjacent to our structures
		locationScore += adjacentStructureTiles * 10;

		// 2. Second priority: directly adjacent to existing concrete
		locationScore += adjacentConcreteTiles * 5;

		// 3. Bonus for adjacent rock (room to expand)
		locationScore += adjacentRockTiles * 2;


				// Pick this location if it has the best score
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}

Coord QuantBot::findTurretPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	// Use squad rally location (enemy direction) as approximation of threat
	Coord enemyDirection = squadRallyLocation.isValid() ? squadRallyLocation : Coord::Invalid();

	// If no squad rally, find closest enemy structure
	if (!enemyDirection.isValid()) {
		FixPoint closestEnemyDistance = FixPt_MAX;
		for (const StructureBase* pStructure : getStructureList()) {
			if (pStructure && pStructure->getOwner() && pStructure->getOwner()->getTeamID() != getHouse()->getTeamID()) {
				FixPoint distance = blockDistance(baseCenter, pStructure->getLocation());
				if (distance < closestEnemyDistance) {
					closestEnemyDistance = distance;
					enemyDirection = pStructure->getLocation();
				}
			}
		}
	}

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(x, y, newSizeX, newSizeY, false,
				(itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {

                if (overlapsReservedStructure(x, y, newSizeX, newSizeY)
                    || !preservesGroundAccess(itemID,Coord(x,y))) continue;
                const auto roads = cityRoadImpact(getMap(), x, y, newSizeX, newSizeY, itemID);
                if (!roads.preservesConnections) continue;
				FixPoint score = 0;
                score += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				Coord candidatePos(x, y);

				// 1. Favor being CLOSE to base center (integrated into base, not perimeter)
				FixPoint distanceFromBase = blockDistance(candidatePos, baseCenter);
				score -= distanceFromBase * 2; // Penalty for being far from center

				// 2. Strong bonus for adjacency to own buildings
				int adjacentOwnBuildings = 0;
				for (int dx = -1; dx <= newSizeX; dx++) {
					for (int dy = -1; dy <= newSizeY; dy++) {
						// Check tiles around the structure
						if ((dx == -1 || dx == newSizeX || dy == -1 || dy == newSizeY) && 
							getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (pTile->hasAStructure()) {
								const StructureBase* pStructure = dynamic_cast<const StructureBase*>(pTile->getObject());
								if (pStructure && pStructure->getOwner() == getHouse()) {
									adjacentOwnBuildings++;
								}
							}
						}
					}
				}
				score += adjacentOwnBuildings * 15; // Strong bonus for being next to own buildings

				// 3. Favor the side of the base closest to the enemy
				// We want turrets between our base and the enemy
				if (enemyDirection.isValid() && baseCenter.isValid()) {
					// Calculate vector from base to enemy
					int baseToEnemyX = enemyDirection.x - baseCenter.x;
					int baseToEnemyY = enemyDirection.y - baseCenter.y;

					// Calculate vector from base to candidate position
					int baseToCandidateX = candidatePos.x - baseCenter.x;
					int baseToCandidateY = candidatePos.y - baseCenter.y;

					// Dot product: positive if candidate is on the enemy side of base
					int dotProduct = baseToEnemyX * baseToCandidateX + baseToEnemyY * baseToCandidateY;
					if (dotProduct > 0) {
						score += dotProduct / 10; // Bonus for being on enemy-facing side
					}
				}

				// 4. Slight preference for sand over rock (buildable terrain)
				int sandTiles = 0;
				for (int dx = 0; dx < newSizeX; dx++) {
					for (int dy = 0; dy < newSizeY; dy++) {
						if (getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (!pTile->isRock()) {
								sandTiles++;
							}
						}
					}
				}
				score += sandTiles * 2; // Minor bonus for sand

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}

bool QuantBot::selectCityServiceInvestment(const BuilderBase* builder, int money, bool emergency,
                                         Uint32& selectedItem, Coord& selectedSite, bool landValueOnly, Uint32 requiredItem) {
    AITelemetry::PerformanceScope perfScope("ai.selectCityServiceInvestment", getGameCycleCount(), getHouse()->getHouseID());
    using CityServiceInvestmentPolicy::Value;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || !sim->isInitialized()) return false;
    const int house = getHouse()->getHouseID(), w = getMap().getSizeX(), h = getMap().getSizeY();
    const auto& prices = currentGame->objectData.data;
    unsigned available = 0;
    const std::array<Uint32,2> serviceItems{Structure_PoliceStation,Structure_RocketTurret};
    bool coreReady=true;
    if (gameMode==GameMode::Custom && !supportMode)
    for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard})
        if (prices[item][house].enabled && prices[item][house].techLevel<=currentGame->techLevel
            && getHouse()->getNumItems(item)==0) coreReady=false;
    const bool rocketOpeningReady=coreReady || getHouse()->getNumItems(Structure_RocketTurret)<2;
    for (unsigned i=0;i<serviceItems.size();++i)
        if ((serviceItems[i]!=Structure_RocketTurret || rocketOpeningReady)
            && (requiredItem == NONE_ID || requiredItem == serviceItems[i])
            && campaignAvailableToBuild(builder,serviceItems[i]) && money >= prices[serviceItems[i]][house].price)
            available |= 1u << i;
    if (!available || (landValueOnly && !(available & 2))) return false;
    const unsigned mode = landValueOnly ? 2 : emergency ? 1 : 0;
    // A yard with its own reservation excludes that plan from marginal gains.
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto selectResult = [&](const CityServiceResults& results) {
        const CityServiceSite* best = nullptr;
        for (unsigned i=0;i<serviceItems.size();++i) {
            const auto& candidate = results[mode][i];
            if (!(available & (1u<<i)) || candidate.site.isInvalid()) continue;
            // Routine coverage belongs to multipurpose turrets. Buy the
            // stronger station when occupied buildings face dangerous crime.
            if (serviceItems[i]==Structure_PoliceStation) {
                if (candidate.value.dangerousRelief<32) continue;
                const auto& hs=sim->getHouseState(house);
                const bool stationCommitted=getHouse()->getNumItems(Structure_PoliceStation)>0
                    || std::any_of(reservedStructures.begin(),reservedStructures.end(),[](const auto& plan) {
                        return plan.second.item==Structure_PoliceStation;
                    });
                // A lower service budget must not trigger a construction loop
                // that buys replacement coverage with still more permanent bills.
                // Grow income before adding another station to an underfunded city.
                if (gameMode==GameMode::Custom && !supportMode && stationCommitted
                    && hs.policeFundingPercent<100) continue;
                const int annualTax=DuneCity::computeAnnualTaxRevenue(hs.taxBaseEighths,sim->getCityTax(),hs.avgLandValue);
                const int annualPower=getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
                int committedCost=hs.nominalPoliceCost;
                for (const auto& plan:reservedStructures)
                    if (plan.first!=planningBuilder)
                        committedCost+=DuneCity::getPoliceAnnualCost(plan.second.item).lround();
                const bool overBudget=gameMode==GameMode::Custom && !supportMode
                    && committedCost+DuneCity::getPoliceAnnualCost(Structure_PoliceStation).lround()
                    > CityServiceInvestmentPolicy::policingAllowance(annualTax,annualPower,
                        CityServiceInvestmentPolicy::policingBudgetPercent(getHouse()->getCredits(),annualTax,annualPower,committedCost));
                // Prefer cheaper useful relief before accepting another permanent
                // station bill. Accept an otherwise unaffordable full-strength
                // station only for danger a turret cannot treat; its actual
                // funding still remains under the recurring budget limit.
                const auto& rocket=results[mode][1];
                const bool rocketTreatsDanger=(available&2) && rocket.site.isValid()
                    && rocket.value.dangerousRelief>=32;
                if (overBudget && (!emergency || rocketTreatsDanger)) continue;
            }
            if (!best || candidate.value.betterThan(best->value)) {
                best = &candidate;
                selectedItem = serviceItems[i];
            }
        }
        if (!best) return false;
        selectedSite = best->site;
        const auto& value = best->value;
        traceDecision("city_service_investment", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",selectedItem).set("x",selectedSite.x).set("y",selectedSite.y).set("emergency",emergency)
            .set("crime_reduction",value.crime).set("annual_tax_gain",value.tax)
            .set("crime_utility",value.crimeUtility).set("dangerous_relief",value.dangerousRelief)
            .set("pre_outbreak_relief",value.preOutbreakRelief)
            .set("estimated_growth_tax",value.growthTax).set("threat_defense_value",value.defense)
            .set("build_cost",value.buildCost).set("annual_upkeep",value.upkeep)
            .set("power_cost",value.powerCost).set("placement_overlap_penalty",value.overlapPenalty)
            .set("horizon_years",1));
        return true;
    };
    if (const auto* cached = cityServiceSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.cache_hit",1,-1,false);
        return selectResult(*cached);
    }
    if (!cityServiceSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.search_deferred",1,-1,false);
        return false;
    }
    auto& results = cityServiceSearch.result();
    CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),house);
    // Compare the whole candidate neighbourhood before spending. A rotating
    // map stripe both chose fringe sites and forgot savings on the next pass.
    scan.begin = 0; scan.end = w*h;

    auto plannedTerrain = sim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }
    const auto& state = sim->getHouseState(house);
    const auto& data = currentGame->objectData.data;
    const bool powered = getHouse()->getProducedPower() >= getHouse()->getPowerRequirement();
    struct Property {
        Coord p;
        int item, value, crime, baseCrime, coverage, population, nextPopulation, demand, pollution, threat;
    };
    std::vector<Property> properties;
    std::vector<const UnitBase*> threats;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->getHealth() > 0 && unit->isVisible(getHouse()->getTeamID())
            && data[unit->getItemID()][unit->getOwner()->getHouseID()].weapondamage > 0)
            threats.push_back(unit);
    }
    std::vector<Coord> turretSites, stationSites;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        if (structure->getItemID()==Structure_RocketTurret) turretSites.push_back(structure->getLocation());
        if (structure->getItemID()==Structure_PoliceStation && structure->getHealth()>0)
            stationSites.push_back(structure->getLocation());
    }
    for (const auto& entry:reservedStructures)
        if (entry.first!=planningBuilder && entry.second.item==Structure_PoliceStation)
            stationSites.push_back(entry.second.location);
    DuneCity::CityMapLayer<int32_t> plannedPolice;
    plannedPolice.init(w,h,DuneCity::kPoliceMapBlockSize);
    auto addSource = [&](int item,Coord p,Coord size,int funding,bool hasPower) {
        const int strength=DuneCity::getPoliceCoverage(item);
        if (strength<=0) return;
        const auto source=DuneCity::policeSource(getMap(),p.x,p.y,size.x,size.y,strength,funding,hasPower);
        DuneCity::addPoliceCoverage(plannedPolice,w,h,source.x,source.y,source.strength);
    };
    for (const auto* structure:getStructureList()) {
        const auto* owner=structure->getOwner();
        if (!owner || structure->getHealth()<=0) continue;
        addSource(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
            sim->getHouseState(owner->getHouseID()).policeFundingPercent,
            owner->getProducedPower()>=owner->getPowerRequirement());
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder) continue;
        const auto& plan=entry.second;
        addSource(plan.item,plan.location,getStructureSize(plan.item),state.policeFundingPercent,powered);
    }
    DuneCity::smoothPoliceCoverage(plannedPolice,w,h);
    int totalTaxBaseEighths = 0, sampleCount = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Coord p = structure->getLocation();
        if (!getMap().tileExists(p.x, p.y)) continue;
        const int item = structure->getItemID();
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        const int level = zone ? getMap().getTile(p.x,p.y)->getCityZoneDensity() : structure->getCityOccupancy();
        const int pop = DuneCity::getStructurePopulation(structure, level);
        totalTaxBaseEighths += DuneCity::getStructureTaxBaseEighths(structure, level);
        const int value = sim->getLandValueMap().worldGet(p.x,p.y);
        if (value > 0) ++sampleCount;
        int coverage = plannedPolice.worldGet(p.x,p.y);
        const int landBlockSize = sim->getLandValueMap().getBlockSize();
        int plannedValue = plannedTerrain.landValueContribution(p.x,p.y,landBlockSize)
            - sim->getParkTerrain().landValueContribution(p.x,p.y,landBlockSize);
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const auto& plan = entry.second;
            if (!DuneCity::usesParkTerrain(plan.item))
                plannedValue += CityServiceInvestmentPolicy::parkContribution(plan.item,
                    plan.location.x,plan.location.y,p.x,p.y,landBlockSize,plannedTerrain);
        }
        const int base = sim->getCrimeBeforePoliceMap().worldGet(p.x,p.y);
        const int crime = std::clamp(base - coverage, 0, 250);
        int threat = 0;
        if (RocketTurretPolicy::defenseWeight(item)) {
            for (const auto* unit : threats) {
                const Coord u = unit->getLocation();
                const int distance = std::max(std::abs(p.x-u.x),std::abs(p.y-u.y));
                if (distance <= 12) threat += data[unit->getItemID()][unit->getOwner()->getHouseID()].price * (13-distance)/13;
            }
            threat = std::min(threat, data[item][house].price) * RocketTurretPolicy::defenseWeight(item) / 4;
            for (const Coord t : turretSites) {
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
            for (const auto& entry : reservedStructures) {
                if (entry.first == planningBuilder || entry.second.item != Structure_RocketTurret) continue;
                const Coord t = entry.second.location;
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
        }
        const int demand = item == Structure_ZoneResidential ? state.resValve
            : item == Structure_ZoneCommercial ? state.comValve : item == Structure_ZoneIndustrial ? state.indValve : 0;
        const int nextPop = zone && item == Structure_ZoneResidential
            ? DuneCity::ResidentialPopulation::grow(pop,sim->getPopulationDensityMap().worldGet(p.x,p.y))
            : zone && level < DuneCity::getStructureMaxLevel(item) ? DuneCity::getZonePopulation(item,level+1) : pop;
        properties.push_back({p,item,std::min(250,value+plannedValue),crime,base,coverage,pop,nextPop,demand,
            sim->getPollutionDensityMap().worldGet(p.x,p.y),threat});
    }
    AITelemetry::log().performance(getGameCycleCount(),house,"service.properties",properties.size(),-1,false);
    LocalPointIndex propertyIndex(w,h);
    for (size_t i=0;i<properties.size();++i) propertyIndex.add(properties[i].p.x,properties[i].p.y,i);
    for (unsigned itemIndex=0; itemIndex<serviceItems.size(); ++itemIndex) {
        const Uint32 item = serviceItems[itemIndex];
        // Search both service types once, independent of the first yard's
        // upgrades/reserve. Each caller applies its own affordability and tech
        // checks when selecting. Otherwise an early low-tech yard can starve
        // every later upgraded yard's rocket search.
        if (!data[item][house].enabled || data[item][house].techLevel > currentGame->techLevel) continue;
        Value itemBest;
        Coord itemSite = Coord::Invalid();
        const Coord size = getStructureSize(item);
        std::vector<bool> candidates(w*h, false);
        for (const auto& p : properties) {
            if (p.crime < 60 && (item != Structure_RocketTurret || (p.value >= 250 && p.threat == 0))) continue;
            for (int y=std::max(scan.begin/w,p.p.y-23); y<=std::min({h-size.y,p.p.y+23,(scan.end-1)/w}); ++y)
                for (int x=std::max(0,p.p.x-23); x<=std::min(w-size.x,p.p.x+23); ++x) candidates[y*w+x]=true;
        }
        AITelemetry::PerformanceScope itemScope("ai.service_site_search",getGameCycleCount(),house,item);
        int scoredSites = 0;
        for (int cell=scan.begin;cell<scan.end;++cell) {
            const int x=cell%w, y=cell/w;
            if (x>w-size.x || y>h-size.y) continue;
            if (!candidates[y*w+x] || overlapsReservedStructure(x,y,size.x,size.y)
                || !getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,item)
                || !preservesGroundAccess(item,Coord(x,y))) continue;
            const auto road = cityRoadImpact(getMap(),x,y,size.x,size.y,item);
            if (!road.preservesConnections || (item == Structure_RocketTurret && road.junctionBonus <= 0)
                || wouldLandlockNeighbouringZone(getMap(),house,x,y,size.x,size.y)) continue;
            ++scoredSites;
            Value value;
            value.buildCost = data[item][house].price;
            // Placement-only cost discourages another station beside one already
            // built/planned. It does not change stacking in the simulation.
            if (item == Structure_PoliceStation) {
                for (const Coord p : stationSites)
                    value.overlapPenalty += CityServiceInvestmentPolicy::stationOverlapCost(value.buildCost,
                        std::max(std::abs(x-p.x),std::abs(y-p.y)));
            }
            value.upkeep = (DuneCity::getPoliceAnnualCost(item)*state.policeFundingPercent/100).lround();
            const int power = std::max(0,data[item][house].power);
            const int spare = std::max(0,getHouse()->getProducedPower()-getHouse()->getPowerRequirement());
            // Power upkeep (50-second budget year / 15-second power bill) plus
            // the share of a windtrap needed when spare generation is insufficient.
            value.powerCost = power*50/(32*15) + std::max(0,power-spare)*data[Structure_WindTrap][house].price
                / std::max(1,-data[Structure_WindTrap][house].power);
            const auto source = DuneCity::policeSource(getMap(),x,y,size.x,size.y,
                DuneCity::getPoliceCoverage(item),state.policeFundingPercent,powered);
            int valueGainSum = 0, neighbourhoodGainSum = 0, growthTax = 0;
            propertyIndex.visit(x,y,23,[&](size_t propertyID) {
                const auto& p=properties[propertyID];
                const int distance = std::max(std::abs(x-p.p.x),std::abs(y-p.p.y));
                const int added = DuneCity::policeCoverageAt(source.x,source.y,p.p.x,p.p.y,2,source.strength,w,h);
                const int reduction = DuneCity::marginalCrimeReduction(p.baseCrime,p.coverage,added);
                if (p.population > 0) {
                    value.crime += reduction;
                    value.crimeUtility += CityServiceInvestmentPolicy::underservedUtility(
                        CityServiceInvestmentPolicy::crimeHarm(p.crime,p.item,p.population)
                        - CityServiceInvestmentPolicy::crimeHarm(p.crime-reduction,p.item,p.population),p.coverage);
                    value.dangerousRelief += CityServiceInvestmentPolicy::reliefAboveBand(
                        p.crime,reduction,CityServiceInvestmentPolicy::dangerousBand);
                    value.preOutbreakRelief += CityServiceInvestmentPolicy::reliefAboveBand(
                        p.crime,reduction,CityServiceInvestmentPolicy::preOutbreakBand);
                }
                const int park = CityServiceInvestmentPolicy::parkContribution(item,x,y,p.p.x,p.p.y,
                    sim->getLandValueMap().getBlockSize(),plannedTerrain);
                const int restored = p.crime > 190 && p.crime-reduction <= 190 ? 20 : 0;
                const int gain = std::min(250-p.value,park+restored);
                if (p.value > 0) valueGainSum += gain;
                const auto role = DuneCity::getStructureCityRole(p.item);
                if (p.value > 0 && (role == DuneCity::CityRole::Residential || role == DuneCity::CityRole::Commercial))
                    neighbourhoodGainSum += gain;
                // Conservative forecast: at most a quarter of one demanded growth
                // level, only where improved value and acceptable pollution allow it.
                if (powered && p.demand > 0 && p.population > 0 && p.nextPopulation > p.population
                    && (p.item == Structure_ZoneResidential || p.item == Structure_ZoneCommercial)
                    && p.pollution < 128 && gain > 0)
                    growthTax += DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(p.item,p.nextPopulation-p.population,0),
                        sim->getCityTax(),std::max(1,state.avgLandValue)) * std::min(gain,64) / (4*64);
                if (powered && item == Structure_RocketTurret && distance <= data[item][house].weaponrange)
                    value.defense += p.threat;
            });
            value.tax = CityServiceInvestmentPolicy::annualTaxGain(totalTaxBaseEighths,sim->getCityTax(),valueGainSum,sampleCount);
            value.growthTax = growthTax;
            if (item == Structure_RocketTurret) {
                if (value.crime <= 0) continue; // Civic turrets must reduce actual crime.
                value.neighbourhoodTax = CityServiceInvestmentPolicy::annualTaxGain(
                    totalTaxBaseEighths,sim->getCityTax(),neighbourhoodGainSum,sampleCount);
            }
            for (unsigned resultMode=0;resultMode<results.size();++resultMode) {
                if (!value.useful(resultMode == 1)) continue;
                if (resultMode == 2 && (item != Structure_RocketTurret || !value.landValueTurretEligible())) continue;
                auto& result = results[resultMode][itemIndex];
                if (result.site.isInvalid() || (item == Structure_PoliceStation
                    ? value.betterPoliceSiteThan(result.value) : value.betterThan(result.value)))
                    result = {Coord(x,y),value};
            }
            itemSite = results[mode][itemIndex].site;
            itemBest = results[mode][itemIndex].value;
        }
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scored_sites",scoredSites,item,false);
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scanned_tiles",scan.end-scan.begin,item,false);
        traceDecision("city_service_candidate", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",item).set("eligible",itemSite.isValid()).set("x",itemSite.x).set("y",itemSite.y)
            .set("crime_reduction",itemBest.crime).set("annual_tax_gain",itemBest.tax)
            .set("crime_utility",itemBest.crimeUtility).set("dangerous_relief",itemBest.dangerousRelief)
            .set("pre_outbreak_relief",itemBest.preOutbreakRelief)
            .set("res_com_tax_gain",itemBest.neighbourhoodTax)
            .set("estimated_growth_tax",itemBest.growthTax).set("threat_defense_value",itemBest.defense)
            .set("build_cost",itemBest.buildCost).set("annual_upkeep",itemBest.upkeep)
            .set("power_cost",itemBest.powerCost).set("placement_overlap_penalty",itemBest.overlapPenalty)
            .set("emergency",emergency));
    }
    return selectResult(results);
}

// Desired-overlap tier for city defence: 0 Easy/Defend, 1 Medium, 2 Hard,
// 3 Brutal. Kept next to the only two users so the mapping stays single.
static int rocketCoverageTier(QuantBot::Difficulty difficulty) {
    using Difficulty = QuantBot::Difficulty;
    return difficulty == Difficulty::Brutal ? 3 : difficulty == Difficulty::Hard ? 2
        : difficulty == Difficulty::Medium ? 1 : 0;
}

Coord QuantBot::findCityTurretPlaceLocation(Uint32 itemID, int* defenseScore, int* amenityScore,
                                            int* crimeBenefit, int* crimeHotspot) {
    AITelemetry::PerformanceScope perfScope("ai.findCityTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    if (defenseScore) *defenseScore = 0;
    if (amenityScore) *amenityScore = 0;
    if (crimeBenefit) *crimeBenefit = 0;
    if (crimeHotspot) *crimeHotspot = 0;
    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (!citySim || itemID != Structure_RocketTurret) return Coord::Invalid();
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto resultSite = [&](const CityTurretResult& result) {
        if (defenseScore) *defenseScore = result.defense;
        if (amenityScore) *amenityScore = result.amenity;
        if (crimeBenefit) *crimeBenefit = result.crime;
        if (crimeHotspot) *crimeHotspot = result.hotspot;
        return result.site;
    };
    if (const auto* cached = cityTurretSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.cache_hit",1,itemID,false);
        return resultSite(*cached);
    }
    if (!cityTurretSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.search_deferred",1,itemID,false);
        return Coord::Invalid();
    }
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),getHouse()->getHouseID(),
        key == NONE_ID ? 1 : cityReadyYardCount);
    scan.begin = 0; scan.end = w*h; // Compare all owned districts before buying coverage.
    auto plannedTerrain = citySim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }


    // Existing and planned turrets count as coverage, so additional yards do
    // not buy the same protection/amenity repeatedly.
    std::vector<Coord> turrets;
    struct Target { Coord position; Coord origin; Coord size; int defense; int demand; int value;
                    int firstCover; bool expansionYard; int coverage; bool covered; bool builderExpansion; };
    std::vector<Target> targets;
    const int defenseRadius = std::max(1,
        currentGame->objectData.data[itemID][getHouse()->getHouseID()].weaponrange - 1);
    // One turret in range is not cover for the assets the city cannot lose.
    // Demand the difficulty's overlap for those and keep looking for sites
    // until it is met, so a single raid cannot open the whole base.
    const int tier = rocketCoverageTier(difficulty);
    const Uint32 mainYard = mainConstructionYardID();
    auto addTarget = [&](Uint32 item, Coord pos, Coord size, Uint32 objectID) {
        const int weight = RocketTurretPolicy::assetPriority(item);
        const bool amenity = item == Structure_ZoneResidential || item == Structure_ZoneCommercial;
        if (!weight && !amenity) return;
        // Use the same structure origin that receives city land-value scans.
        const Coord point = weight ? Coord(pos.x + size.x/2, pos.y + size.y/2) : pos;
        const bool expansion = isExpansionYard(item, objectID, mainYard);
        targets.push_back({point, pos, size, weight,
            weight ? RocketTurretPolicy::desiredCoverage(item, tier, expansion) : 1,
            citySim->getLandValueMap().worldGet(point.x, point.y),
            weight ? RocketTurretPolicy::firstCoverPriority(item, expansion) : 0, expansion, 0, false, expansion && objectID==planningBuilder});
    };
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (structure->getItemID() == Structure_RocketTurret) turrets.push_back(structure->getLocation());
        else addTarget(structure->getItemID(), structure->getLocation(), structure->getStructureSize(),
                       structure->getObjectID());
    }
    for (const auto& entry : reservedStructures) {
        const auto& plan = entry.second;
        if (plan.item == Structure_RocketTurret) {
            if (entry.first != planningBuilder) turrets.push_back(plan.location);
        }
        else addTarget(plan.item, plan.location, getStructureSize(plan.item), NONE_ID);
    }
    for (auto& target : targets) {
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (const auto& turret : turrets) {
            if (target.defense
                ? RocketTurretPolicy::coversBuilding(turret,target.origin,target.size,radius)
                : plannedTerrain.marginalGain(turret.x,turret.y,DuneCity::kParkLandValueBonus,
                    target.position.x,target.position.y)>0) {
                if (++target.coverage >= target.demand) { target.covered = true; break; }
            }
        }
    }
    std::vector<bool> candidates(w*h, false);
    for (const auto& target : targets) {
        if (target.covered || (!target.defense && target.value >= DuneCity::kMaxLandValue)) continue;
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (int y = std::max(scan.begin/w, target.position.y-radius); y <= std::min({h-1, target.position.y+radius,(scan.end-1)/w}); ++y)
            for (int x = std::max(0, target.position.x-radius); x <= std::min(w-1, target.position.x+radius); ++x)
                candidates[y*w+x] = true;
    }
    // City crime is a third legitimate turret role. It is deliberately
    // secondary to critical asset defence, but lets the 15% service effect
    // fill road intersections in districts that police have not covered.
    struct CrimeTarget { Coord position; int crime; int plannedCoverage = 0; };
    std::vector<CrimeTarget> crimeTargets;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (DuneCity::getStructureCityRole(structure->getItemID()) == DuneCity::CityRole::None) continue;
        const Coord p = structure->getLocation();
        const int crime = citySim->getCrimeRateMap().worldGet(p.x, p.y);
        if (crime >= 80) {
            crimeTargets.push_back({p, crime});
            for (int y = std::max(scan.begin/w, p.y-DuneCity::kPoliceRadius); y <= std::min({h-1, p.y+DuneCity::kPoliceRadius,(scan.end-1)/w}); ++y)
                for (int x = std::max(0, p.x-DuneCity::kPoliceRadius); x <= std::min(w-1, p.x+DuneCity::kPoliceRadius); ++x)
                    candidates[y*w+x] = true;
        }
    }
    auto plannedCoverageAt = [&](Coord point) {
        int coverage = 0;
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const int strength = DuneCity::getPoliceCoverage(entry.second.item);
            if (strength <= 0) continue;
            const Coord size = getStructureSize(entry.second.item);
            const auto source = DuneCity::policeSource(getMap(), entry.second.location.x,
                entry.second.location.y, size.x, size.y, strength,
                citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
                getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
            coverage += DuneCity::policeCoverageAt(source.x,source.y,point.x,point.y,2,
                source.strength,w,h);
        }
        return coverage;
    };
    for (auto& target : crimeTargets) target.plannedCoverage = plannedCoverageAt(target.position);
    Coord best = Coord::Invalid();
    RocketTurretPolicy::Score bestScore;
    int bestCrimeBenefit = 0, bestCrimeHotspot = 0;
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.scanned_tiles",scan.end-scan.begin,itemID,false);
    for (int cell=scan.begin;cell<scan.end;++cell) {
        const int x=cell%w, y=cell/w;
        if (!candidates[y*w+x] || overlapsReservedStructure(x, y, 1, 1)) continue;
        if (!getMap().okayToPlaceStructure(x, y, 1, 1, false, getHouse(), false, itemID)) continue;
        if (!preservesGroundAccess(itemID,Coord(x,y))) continue;
        const auto roads = cityRoadImpact(getMap(), x, y, 1, 1, itemID);
        if (!roads.preservesConnections
            || wouldLandlockNeighbouringZone(getMap(), getHouse()->getHouseID(), x, y, 1, 1)) continue;
        RocketTurretPolicy::Score score;
        score.junction = roads.junctionBonus;
        int candidateCrimeBenefit = 0, candidateCrimeHotspot = 0;
        for (const auto& target : targets) {
            if (target.covered) continue;
            const int distance = std::max(std::abs(x-target.position.x), std::abs(y-target.position.y));
            if (target.defense && RocketTurretPolicy::coversBuilding(Coord(x,y),target.origin,target.size,defenseRadius)) {
                // Nothing covers this asset yet: its first turret ranks above
                // any amount of additional overlap elsewhere. An expansion
                // yard keeps that priority, at a lower weight, until all three
                // of its demanded turrets stand — one emplacement does not
                // survive a focused wing.
                if (target.builderExpansion) score.expansion=1;
                if (target.coverage == 0) score.critical += target.firstCover;
                else if (target.expansionYard) score.critical += 1;
                score.defense += target.defense;
                score.proximity += target.defense * (defenseRadius-distance);
            } else if (!target.defense) {
                score.amenity += RocketTurretPolicy::amenityBenefit(target.value, false,
                    plannedTerrain.marginalGain(x,y,DuneCity::getParkLandValueBonus(itemID),
                        target.position.x,target.position.y));
            }
        }
        const auto source = DuneCity::policeSource(getMap(), x, y, 1, 1, DuneCity::getPoliceCoverage(itemID),
            citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
            getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
        for (const auto& target : crimeTargets) {
            const int added = DuneCity::policeCoverageAt(source.x,source.y,target.position.x,target.position.y,
                2,source.strength,w,h);
            const int reduction = DuneCity::marginalCrimeReduction(
                citySim->getCrimeBeforePoliceMap().worldGet(target.position.x, target.position.y),
                citySim->getPoliceCoverageMap().worldGet(target.position.x, target.position.y)
                    + target.plannedCoverage, added);
            candidateCrimeBenefit += reduction;
            if (reduction > 0) candidateCrimeHotspot = std::max(candidateCrimeHotspot, target.crime);
        }
        // Amenity normally ranges in the tens. A turret must reduce crime in
        // several blocks before it competes with a demanded tax-base zone.
        score.amenity += candidateCrimeBenefit / 8;
        if (score.useful() && (!best.isValid() || score.betterThan(bestScore))) {
            best = Coord(x, y); bestScore = score;
            bestCrimeBenefit = candidateCrimeBenefit;
            bestCrimeHotspot = candidateCrimeHotspot;
        }
    }
    if (defenseScore) *defenseScore = bestScore.defense;
    if (amenityScore) *amenityScore = bestScore.amenity;
    if (crimeBenefit) *crimeBenefit = bestCrimeBenefit;
    if (crimeHotspot) *crimeHotspot = bestCrimeHotspot;
    cityTurretSearch.result() = {best,bestScore.defense,bestScore.amenity,bestCrimeBenefit,bestCrimeHotspot};
    return best;
}

Coord QuantBot::findEffectiveTurretPlaceLocation(Uint32 itemID) {
    if (currentGame && currentGame->isCitySimEnabled() && itemID == Structure_RocketTurret)
        return findCityTurretPlaceLocation(itemID);
    return findTurretPlaceLocation(itemID);
}

Coord QuantBot::findPlaceLocationSimple(Uint32 itemID) {
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building
			if (getMap().okayToPlaceStructure(x, y, newSizeX, newSizeY, false,
				(itemID == Structure_ConstructionYard) ? nullptr : getHouse(), false, itemID)) {
                if (DuneCity::isCityZoneStructure(itemID) && getGameInitSettings().getGameOptions().concreteRequired
                    && !getMap().okayToPlaceStructure(x,y,newSizeX,newSizeY,false,getHouse())) continue;
                if (!preservesGroundAccess(itemID,Coord(x,y))) continue;

				FixPoint score = 0;

				// Base scoring - favor being close to existing buildings
				FixPoint closestOwnBuildingDistance = FixPt_MAX;
				for (const StructureBase* pStructure : getStructureList()) {
					if (pStructure->getOwner() == getHouse()) {
						FixPoint distance = blockDistance(Coord(x, y), Coord(pStructure->getX(), pStructure->getY()));
						if (distance < closestOwnBuildingDistance) {
							closestOwnBuildingDistance = distance;
						}
					}
				}
				if (closestOwnBuildingDistance < FixPt_MAX) {
					score += 50 - closestOwnBuildingDistance; // Bonus for being close to our buildings
				}

				// Building-specific placement preferences
				if (itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
					// Turrets prefer map edges for defensive positioning
					int distanceToEdge = std::min({x, y, getMap().getSizeX() - 1 - x, getMap().getSizeY() - 1 - y});
					score += (10 - distanceToEdge) * 5; // Higher score for being closer to edges

					// Rocket turrets also prefer being close to squad rally point
					if (itemID == Structure_RocketTurret) {
						FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
						score += 30 - distanceToRally * 2; // Bonus for being close to rally point
					}
				}
				else if (itemID == Structure_Refinery) {
					// Refineries prefer being close to spice deposits
					FixPoint closestSpiceDistance = FixPt_MAX;
					for (int spiceX = 0; spiceX < getMap().getSizeX(); spiceX++) {
						for (int spiceY = 0; spiceY < getMap().getSizeY(); spiceY++) {
							if (getMap().tileExists(spiceX, spiceY) && getMap().getTile(spiceX, spiceY)->hasSpice()) {
								FixPoint spiceDistance = blockDistance(Coord(x, y), Coord(spiceX, spiceY));
								if (spiceDistance < closestSpiceDistance) {
									closestSpiceDistance = spiceDistance;
								}
							}
						}
					}
					if (closestSpiceDistance < FixPt_MAX) {
						score += 50 - closestSpiceDistance * 2; // Higher bonus for being closer to spice
					}
				}
				else if (itemID == Structure_HeavyFactory || itemID == Structure_LightFactory || 
						 itemID == Structure_WOR || itemID == Structure_Barracks || itemID == Structure_StarPort) {
					// Production buildings prefer being close to rally point and base center
					FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
					FixPoint distanceToBase = blockDistance(findBaseCentre(getHouse()->getHouseID()), Coord(x, y));
					score += 20 - distanceToRally / 2; // Bonus for being close to rally point
					score += 20 - distanceToBase; // Bonus for being close to base center
				}

				// Favor map edges in general for defensive positioning
				if (x == 0 || x == getMap().getSizeX() - newSizeX || y == 0 || y == getMap().getSizeY() - newSizeY) {
					score += 10;
				}

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}


bool QuantBot::canAddRepairYard(int includingQueued) const {
    return (currentGame && currentGame->isCitySimEnabled())
        || difficulty != Difficulty::Medium
        || includingQueued < initialItemCount[Structure_RepairYard];
}

void QuantBot::build(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.build", getGameCycleCount(), getHouse()->getHouseID());
    AITelemetry::PerformanceScope phaseScope("ai.build.evaluate",getGameCycleCount(),getHouse()->getHouseID());
    refreshTacticalDanger();
    planningBuilder = NONE_ID;
    recentStructureLosses.erase(std::remove_if(recentStructureLosses.begin(), recentStructureLosses.end(),
        [&](const auto& loss) { return getGameCycleCount() - loss.cycle >= MILLI2CYCLES(900000); }), recentStructureLosses.end());
    cityProductionPlots.clear();planningCityProductionPlots=false;
    clearPlacementCache();
    for (auto it = roadRedirectRetryCycle.begin(); it != roadRedirectRetryCycle.end();) {
        if (getGameCycleCount() >= it->second) it = roadRedirectRetryCycle.erase(it);
        else ++it;
    }
    cityServiceSearch.reset();
    cityTurretSearch.reset();
    for (auto it = reservedStructures.begin(); it != reservedStructures.end();) {
        const auto* builder = dynamic_cast<const BuilderBase*>(currentGame->getObjectManager().getObject(it->first));
        if (!builder || builder->getOwner() != getHouse() || builder->getProductionQueueSize() == 0)
            it = reservedStructures.erase(it);
        else ++it;
    }

	int houseID = getHouse()->getHouseID();
	auto& data = currentGame->objectData.data;

	int itemCount[Num_ItemID];
	for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
		itemCount[i] = getHouse()->getNumItems(i);
	}

	int activeHeavyFactoryCount = 0;
    std::vector<const BuilderBase*> harvesterFactories;
    bool carryallBuildAvailable = false;
    bool mcvBuildAvailable = false;
    bool nuclearBuildAvailable = false;
    int activeLightFactoryCount = 0;
	int activeHighTechFactoryCount = 0;
    int ornithopterFactoryCount = 0;
    int ornithopterCapableFactoryCount = 0;
	int activeRepairYardCount = 0;
    int queuedProductionCost = 0;
    int queuedMilitaryValue = 0;
    int mcvUpgradesInProgress = 0;

	// Let's try just running this once...
	if (squadRallyLocation.isInvalid()) {
		squadRallyLocation = findSquadRallyLocation();
		squadRetreatLocation = findSquadRetreatLocation();
		if (gameMode != GameMode::Campaign) {
			retreatAllUnits();
		}
	}

	// Next add in the objects we are building
	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() == getHouse()) {
			if (pStructure->isABuilder()) {
				const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                if (pBuilder->getItemID() == Structure_ConstructionYard && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)) nuclearBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall)) carryallBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                    && campaignAvailableToBuild(pBuilder,Unit_Ornithopter)) ++ornithopterCapableFactoryCount;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)) harvesterFactories.push_back(pBuilder);
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_MCV)) mcvBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->isUpgrading()
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)) ++mcvUpgradesInProgress;
                if (pBuilder->isUpgrading())
                    queuedProductionCost += std::max(0,pBuilder->getUpgradeCost() - pBuilder->getUpgradeProgress().lround());
				if (pBuilder->getProductionQueueSize() > 0) {
                    int builderQueuedCost = 0;
					for (const auto& queued : pBuilder->getBuildList()) {
						itemCount[queued.itemID] += queued.num;
                        builderQueuedCost += queued.num * queued.price;
                        if (QuantBotBuildPolicy::militaryItem(queued.itemID))
                            queuedMilitaryValue += queued.num * data[queued.itemID][houseID].price;
					}
                    // Starport cargo is paid for when ordered. It still counts
                    // toward the fleet, but must not reserve the same cash again.
                    if (pBuilder->getItemID()!=Structure_StarPort)
                        queuedProductionCost += std::max(0, builderQueuedCost - pBuilder->getProductionProgress().lround());
                    if (pBuilder->getItemID() == Structure_HeavyFactory) {
						activeHeavyFactoryCount++;
					}
					else if (pBuilder->getItemID() == Structure_LightFactory) {
                        ++activeLightFactoryCount;
                    }
                    else if (pBuilder->getItemID() == Structure_HighTechFactory) {
						activeHighTechFactoryCount++;
                        if (pBuilder->getCurrentProducedItem() == Unit_Ornithopter
                            && !pBuilder->isOnHold() && !pBuilder->isUpgrading()) ++ornithopterFactoryCount;
					}
				}
			}
			else if (pStructure->getItemID() == Structure_RepairYard) {
				const RepairYard* pRepairYard = static_cast<const RepairYard*>(pStructure);
				if (!pRepairYard->isFree()) {
					activeRepairYardCount++;
				}

			}

			// Unit deployment position - disabled, just deploy units normally
			// Production buildings will deploy units at their default position
		}


	}

	int money = getHouse()->getCredits();
    militaryValue += queuedMilitaryValue;
	const bool citySimEnabled = currentGame && currentGame->isCitySimEnabled();
    if (citySimEnabled && itemCount[Structure_ConstructionYard]>0) {
        planningCityProductionPlots=true;
        for (Uint32 item:{Structure_HeavyFactory,Structure_RepairYard}) {
            if (!data[item][houseID].enabled || data[item][houseID].techLevel>currentGame->techLevel) continue;
            clearPlacementCache(false);
            const Coord site=findPlaceLocation(item);
            if (site.isValid()) cityProductionPlots.push_back({item,site});
        }
        planningCityProductionPlots=false;clearPlacementCache(false);
        if (AITelemetry::log().enabled()) {
            AITelemetry::Record plots;
            for (const auto& plot:cityProductionPlots) plots.set(std::to_string(plot.item),
                AITelemetry::Record().set("x",plot.location.x).set("y",plot.location.y));
            traceDecision("city_production_plots",AITelemetry::Record().set("plots",plots));
        }
    }
    // INVALID is an absent/disabled market entry; zero is merely sold out and
    // will restock. Check the catalogue, not whether a factory already exists.
    const bool starportMarketAvailable = [&] {
        for (int item=Unit_FirstID;item<=Unit_LastID;++item)
            if (data[item][houseID].enabled && getHouse()->getChoam().getNumAvailable(item)>=0) return true;
        return false;
    }();
    // A queued refinery supplies a free worker when placed. Reserve that
    // worker alongside factory production and paid imports in every game mode.
    const int engineHarvesterLimit = getHouse()->getMaxHarvesters();
    const int actualHarvesters = getHouse()->getNumItems(Unit_Harvester)
        + getHouse()->getNumItems(Unit_RebelHarvester);
    const int pendingRefineries = std::max(0,itemCount[Structure_Refinery]
        - getHouse()->getNumItems(Structure_Refinery));
    itemCount[Unit_Harvester] += engineHarvesterLimit > 0
        ? std::min(pendingRefineries, std::max(0,engineHarvesterLimit
            - itemCount[Unit_Harvester] - itemCount[Unit_RebelHarvester]))
        : pendingRefineries;
    int combatVehicles=0, busyTransports=0, busyRepairBays=0;
    std::set<Uint32> pickupQueue, repairQueue;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || unit->getHealth()<=0) continue;
        if (unit->getItemID()==Unit_Carryall && static_cast<const Carryall*>(unit)->isBooked()) ++busyTransports;
        if (!unit->isAGroundUnit() || unit->isInfantry() || unit->getItemID()==Unit_Saboteur) continue;
        const auto* ground=static_cast<const GroundUnit*>(unit);
        if (unit->canAttack() && unit->getItemID()!=Unit_Sandworm) ++combatVehicles;
        if (unit->getAttackMode()==CARRYALLREQUESTED && !ground->hasBookedCarrier()) pickupQueue.insert(unit->getObjectID());
        const auto* target=unit->getTarget();
        // A borrowed unit is never admitted to our bays, so it is not repair demand and must not
        // reserve a bay or justify building another one.
        if (ground->isEligibleForRepair()
            && (unit->isBadlyDamaged() || (target && target->getItemID()==Structure_RepairYard
            && target->getOwner()==getHouse() && unit->getHealth()<unit->getMaxHealth()))) repairQueue.insert(unit->getObjectID());
    }
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        const GroundUnit* returnUnit=nullptr;
        if (structure->getItemID()==Structure_RepairYard) {
            const auto* unit=static_cast<const RepairYard*>(structure)->getRepairUnit();
            if (unit && unit->getHealth()<unit->getMaxHealth()) {
                ++busyRepairBays;repairQueue.insert(unit->getObjectID());
            } else returnUnit=dynamic_cast<const GroundUnit*>(unit);
        } else if (structure->getItemID()==Structure_Refinery) {
            const auto* unit=dynamic_cast<const Harvester*>(static_cast<const Refinery*>(structure)->getContainedHarvester());
            if (unit && unit->getAmountOfSpice()<=0) returnUnit=unit;
        }
        // A finished job waiting for its return flight is transport pressure,
        // not a reason to buy another repair/unloading bay.
        if (returnUnit && returnUnit->getGuardPoint().isValid() && !returnUnit->hasBookedCarrier()
            && blockDistance(structure->getLocation(),returnUnit->getGuardPoint())>=MIN_CARRYALL_LIFT_DISTANCE)
            pickupQueue.insert(returnUnit->getObjectID());
    }
    const int waitingRepairVehicles=std::max(0,int(repairQueue.size())-busyRepairBays);
    const int repairBaseline=QuantBotBuildPolicy::baselineRepairYards(combatVehicles,actualHarvesters);
    const int repairTarget=QuantBotBuildPolicy::supportQueueTarget(repairBaseline,
        getHouse()->getNumItems(Structure_RepairYard),itemCount[Structure_RepairYard],busyRepairBays,waitingRepairVehicles);
    const bool powerRules = getHouse()->isPowerRequired();
    const bool turretPowerRequired = getGameInitSettings().getGameOptions().rocketTurretsNeedPower;
    const bool vanillaEconomy = !citySimEnabled && !powerRules;
    const bool campaignEconomyPush = vanillaEconomy && isCampaignGameType(currentGame->gameType)
        && !supportMode && !isCampaignEnemy() && difficulty>=Difficulty::Hard;
    // Buildings pay gradually. Previously the same unspent cash funded more
    // orders every pass even though it was already committed to existing queues.
    money = std::max(0, money - queuedProductionCost);
    const int economyReserve = vanillaEconomy ? std::max(2000,
        data[Structure_Refinery][houseID].price + 2 * data[Unit_Harvester][houseID].price) : 0;

	// Per-house city stats — CitySimulation now tracks these per player.
	int ownResPop = 0, ownComPop = 0, ownIndPop = 0, ownTotalPop = 0, ownTaxBaseEighths = 0;
	int ownAvgLandValue = 0;
	int16_t ownResValve = 0, ownComValve = 0, ownIndValve = 0;

	if (citySimEnabled) {
		auto* citySim = currentGame->getCitySimulation();
		if (citySim) {
			const auto& hs = citySim->getHouseState(getHouse()->getHouseID());
			ownResPop = hs.resPop;
			ownComPop = hs.comPop;
			ownIndPop = hs.indPop;
			ownTotalPop = hs.getTotalPop();
            ownTaxBaseEighths = hs.taxBaseEighths;
			ownAvgLandValue = hs.avgLandValue;
			ownResValve = hs.resValve;
			ownComValve = hs.comValve;
			ownIndValve = hs.indValve;
		}
	}

    if (citySimEnabled && !supportMode && gameMode == GameMode::Custom) {
        auto* sim = currentGame->getCitySimulation();
        const auto& hs = sim->getHouseState(houseID);
        const int recentLosses = std::count_if(recentStructureLosses.begin(),recentStructureLosses.end(),[&](const auto& loss) {
            return getGameCycleCount()-loss.cycle < MILLI2CYCLES(180000);
        });
        const int taxIncome = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,sim->getCityTax(),ownAvgLandValue);
        const int powerCost = getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
        // Include queued services, so their completion cannot silently push
        // recurring costs above the limit between periodic city scans.
        int committedPoliceMilli=0;
        for (Uint32 item:{Structure_PoliceStation,Structure_GunTurret,Structure_RocketTurret})
            committedPoliceMilli+=itemCount[item]*(DuneCity::getPoliceAnnualCost(item)*1000).lround();
        const int committedPoliceCost=(committedPoliceMilli+999)/1000;
        const int budgetPercent=CityServiceInvestmentPolicy::policingBudgetPercent(money,taxIncome,powerCost,committedPoliceCost);
        const int affordable=CityServiceInvestmentPolicy::affordablePoliceFunding(taxIncome,powerCost,committedPoliceCost,budgetPercent);
        // Necessary cuts keep the strict 33%/50% recurring limit and apply
        // immediately; increases wait out the review cadence and deadband, so
        // the service stops oscillating every build pass.
        const int funding=CityServiceInvestmentPolicy::smoothedPoliceFunding(hs.policeFundingPercent,
            affordable,getGameCycleCount(),lastPoliceBudgetReviewCycle);
        if (funding != hs.policeFundingPercent) {
            const int previousFunding=hs.policeFundingPercent;
            lastPoliceBudgetReviewCycle=getGameCycleCount();
            // AI updates already run deterministically on every peer, after
            // this cycle's commands have executed. A same-cycle queued budget
            // command is never consumed; apply it like other AI economic orders.
            sim->setPoliceFundingPercent(houseID,funding);
            traceDecision("city_police_budget",AITelemetry::Record().set("previous",previousFunding)
                .set("funding",funding).set("tax_income",taxIncome).set("power_cost",powerCost)
                .set("nominal_cost",hs.nominalPoliceCost).set("cash",money).set("recent_losses",recentLosses)
                .set("budget_percent",budgetPercent).set("committed_police_cost",committedPoliceCost)
                .set("reason",funding < hs.policeFundingPercent ? "police_budget_limit" : "police_budget_headroom"));
        }
    }

	// Strategic structures must eventually outrank repeatable choices such as
	// factories, zones, and reactive turrets. These timers stay runtime-only so
	// loading an older save starts a fresh bounded wait without changing save data.
	const Uint32 currentBuildCycle = getGameCycleCount();
    // Forecast two minutes from the last thirty seconds of actual demand.
    // Sample on simulation cycles and save it so peers/reloads agree. Falling
    // or flat demand removes the measured trend; latent zone load is reserved below.
    if (citySimEnabled) {
        const int required = getHouse()->getPowerRequirement();
        if (powerDemandSampleCycle == 0) {
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        } else if (currentBuildCycle-powerDemandSampleCycle >= MILLI2CYCLES(30000)) {
            projectedPowerDemandGrowth = QuantBotBuildPolicy::projectedPowerGrowth(
                powerDemandSample,required,currentBuildCycle-powerDemandSampleCycle,MILLI2CYCLES(120000));
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        }
    }
	const Uint32 noEligibilityCycle = std::numeric_limits<Uint32>::max();
	bool anyConstructionYardCanBuildIX = false;
    bool anyConstructionYardCanBuildRefinery = false;
	bool anyConstructionYardCanBuildPalace = false;
	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() != getHouse()
			|| pStructure->getItemID() != Structure_ConstructionYard
			|| !pStructure->isABuilder()) {
			continue;
		}

		const BuilderBase* pConstructionYard = static_cast<const BuilderBase*>(pStructure);
		anyConstructionYardCanBuildIX |= pConstructionYard->isAvailableToBuild(Structure_IX);
        anyConstructionYardCanBuildRefinery |= pConstructionYard->isAvailableToBuild(Structure_Refinery);
		anyConstructionYardCanBuildPalace |= pConstructionYard->isAvailableToBuild(Structure_Palace);
	}

	const bool customStrategicPlanning = gameMode == GameMode::Custom && !supportMode;
	const bool stablePower = getHouse()->hasPower();
	// itemCount already includes every queued and unplaced palace from all
	// builders, so parallel construction yards cannot exceed the target.
	const int palaceTarget = QuantBotBuildPolicy::palaceTarget(
		getGameInitSettings().getGameOptions().onlyOnePalace, citySimEnabled,
		ownTotalPop * DuneCity::CitySimulation::kPopDisplayMultiplier,
		static_cast<int>(difficulty));
	const bool palaceAllowedNow = itemCount[Structure_Palace] < palaceTarget;
	const bool ixEligible = customStrategicPlanning
		&& itemCount[Structure_IX] == 0
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_HighTechFactory] > 0
		&& itemCount[Structure_RepairYard] > 0
		&& anyConstructionYardCanBuildIX;
	const bool palaceEligible = customStrategicPlanning
		&& palaceAllowedNow
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_LightFactory] > 0
		&& anyConstructionYardCanBuildPalace;

	auto updateEligibilityTimer = [&](bool eligible, Uint32& eligibleSinceCycle) {
		if (!eligible) {
			eligibleSinceCycle = noEligibilityCycle;
		} else if (eligibleSinceCycle == noEligibilityCycle) {
			eligibleSinceCycle = currentBuildCycle;
		}
	};
	updateEligibilityTimer(ixEligible, ixEligibleSinceCycle);
	updateEligibilityTimer(palaceEligible, palaceEligibleSinceCycle);

	Uint32 ixWaitMs = 75000;
	Uint32 palaceWaitMs = 120000;
	switch (difficulty) {
		case Difficulty::Medium:
			ixWaitMs = 50000;
			palaceWaitMs = 90000;
			break;
		case Difficulty::Hard:
			ixWaitMs = 35000;
			palaceWaitMs = 60000;
			break;
		case Difficulty::Brutal:
			ixWaitMs = 20000;
			palaceWaitMs = 45000;
			break;
		case Difficulty::Defend:
		case Difficulty::Easy:
			break;
	}

	const bool ixOverdue = ixEligible
		&& currentBuildCycle - ixEligibleSinceCycle >= MILLI2CYCLES(ixWaitMs);
	const bool palaceOverdue = palaceEligible
		&& currentBuildCycle - palaceEligibleSinceCycle >= MILLI2CYCLES(palaceWaitMs);
	Uint32 strategicReserveItem = NONE_ID;
	if (stablePower && ixOverdue) {
		strategicReserveItem = Structure_IX;
	} else if (stablePower && palaceOverdue) {
		strategicReserveItem = Structure_Palace;
	}
	// Reserve only the cost of a feasible order. An unplaceable structure must
	// not freeze production indefinitely, and excess funds remain spendable.
	if (strategicReserveItem != NONE_ID && !findPlaceLocation(strategicReserveItem).isValid()) {
		strategicReserveItem = NONE_ID;
	}
	int strategicReserveCost = strategicReserveItem == NONE_ID ? 0
		: data[strategicReserveItem][houseID].price;

    int spiceCompetitors = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)
        if (getHouse(h) && getHouse(h)->getNumStructures() > 0) ++spiceCompetitors;
    const int spiceShare = lastCalculatedSpice / std::max(1, spiceCompetitors);
    const int mapSpiceHarvesterTarget = vanillaEconomy
        ? DuneCity::vanillaHarvesterTarget(lastCalculatedSpice, campaignEconomyPush ? 1 : spiceCompetitors, harvesterLimit)
        : QuantBotBuildPolicy::desiredSpiceHarvesters(lastCalculatedSpice, spiceCompetitors, harvesterLimit);
    const int spiceHarvesterTarget = citySimEnabled ? mapSpiceHarvesterTarget
        : QuantBotBuildPolicy::refineryThroughputHarvesterTarget(
            spiceShare,mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery),harvesterLimit);
    const int fundedHarvesterTarget = citySimEnabled
        ? CityEconomyInvestmentPolicy::factoryHarvesterTarget(spiceHarvesterTarget,harvesterLimit)
        : QuantBotBuildPolicy::fundedSpiceHarvesters(mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery));
    int busyRefineries=0,freeRefineries=0,unbookedRefineries=0,waitingHarvesters=0,waitingCargo=0,blockedRefineryReturns=0;
    int fieldReturnersBlocked=0;
    auto refineryWaitingForTransport=[](const StructureBase* structure) {
        const auto* refinery=dynamic_cast<const Refinery*>(structure);
        const auto* worker=refinery ? dynamic_cast<const Harvester*>(refinery->getContainedHarvester()) : nullptr;
        return worker && worker->getAmountOfSpice()<=0;
    };
    AITelemetry::Record refineryQueues;
    {
        for(const auto* structure:getStructureList()) if(structure->getOwner()==getHouse()&&structure->acceptsHarvesterDropoff()) {
            if(structure->isHarvesterDropoffFree())++freeRefineries;else ++busyRefineries;
            if(structure->isHarvesterDropoffFree() && structure->getHarvesterDropoffBookings()==0)++unbookedRefineries;
            blockedRefineryReturns+=refineryWaitingForTransport(structure);
            refineryQueues.set(std::to_string(structure->getObjectID()),AITelemetry::Record()
                .set("x",structure->getX()).set("y",structure->getY()).set("busy",!structure->isHarvesterDropoffFree())
                .set("waiting_for_return_transport",refineryWaitingForTransport(structure))
                .set("bookings",structure->getHarvesterDropoffBookings()));
        }
        for(const auto* unit:getUnitList()) if(unit->getOwner()==getHouse()) {
            const auto* harvester=dynamic_cast<const Harvester*>(unit);
            const auto* target=harvester?dynamic_cast<const StructureBase*>(harvester->getTarget()):nullptr;
            if(harvester&&harvester->isActive()&&harvester->isReturning()&&harvester->getAmountOfSpice()>0
                && target&&target->getOwner()==getHouse()&&target->acceptsHarvesterDropoff()&&!target->isHarvesterDropoffFree()
                && !refineryWaitingForTransport(target)) {
                ++waitingHarvesters;
                fieldReturnersBlocked+=blockDistance(harvester->getLocation(),target->getClosestPoint(harvester->getLocation()))
                    >=MIN_CARRYALL_LIFT_DISTANCE;
                waitingCargo+=std::min(int(HARVESTERMAXSPICE),harvester->getAmountOfSpice().lround());
            }
        }
    }
    if(waitingHarvesters<unbookedRefineries+2)refineryQueueSince=std::numeric_limits<Uint32>::max();
    else if(refineryQueueSince==std::numeric_limits<Uint32>::max())refineryQueueSince=getGameCycleCount();
    const bool unloadingBacklog=CityEconomyInvestmentPolicy::unloadingQueueNeedsBay(waitingHarvesters,unbookedRefineries,
        itemCount[Structure_Refinery]-getHouse()->getNumItems(Structure_Refinery),
        refineryQueueSince!=std::numeric_limits<Uint32>::max()&&getGameCycleCount()-refineryQueueSince>=MILLI2CYCLES(10000));
    if(citySimEnabled&&(rockSurveyCycle==std::numeric_limits<Uint32>::max()
        || getGameCycleCount()-rockSurveyCycle>=MILLI2CYCLES(15000))) {
        // Free rock is not the same as somewhere to build. The placement search
        // applies the rules a factory actually has to satisfy — access, safety,
        // and roads it may compromise on before giving up — so its verdict is
        // what "no room left" means here. It runs at the survey cadence, not
        // once per pass, and before the site survey: whether the base is built
        // out is what tells colonisation the missing core cannot be finished
        // here, so it must be measured first rather than read from last survey.
        baseProductionRoomBlocked=!findPlaceLocation(Structure_HeavyFactory).isValid();
        rockSurveyCycle=getGameCycleCount();
        rockExpansionSite=findRockExpansionSite();
    }
    const bool rockExpansionNeeded=citySimEnabled&&rockExpansionSite.isValid()
        && (availableBaseRock<48 || (unloadingBacklog&&!findPlaceLocation(Structure_Refinery).isValid()));
    const int cityWorkingReserve = data[Unit_Tank][houseID].price
        + (itemCount[Unit_Harvester] < fundedHarvesterTarget ? data[Unit_Harvester][houseID].price : 0)
        + (!citySimEnabled && itemCount[Structure_Refinery] < QuantBotBuildPolicy::desiredSpiceRefineries(
            mapSpiceHarvesterTarget,itemCount[Unit_Harvester]) ? data[Structure_Refinery][houseID].price : 0);
    bool orderedSpiceHarvester = false;
    const bool transportTechAvailable = gameMode == GameMode::Custom && lastCalculatedSpice > 0
        && data[Unit_Carryall][houseID].enabled && data[Structure_HighTechFactory][houseID].enabled
        && currentGame->techLevel >= data[Unit_Carryall][houseID].techLevel
        && currentGame->techLevel >= data[Structure_HighTechFactory][houseID].techLevel
        && !getHouse()->isAirUnitLimitReached();
    const bool brutalCityEconomy = citySimEnabled && gameMode == GameMode::Custom
        && difficulty == Difficulty::Brutal;
    auto openingFleetIncomplete = [&]() {
        return citySimEnabled && gameMode == GameMode::Custom
            && !getHouse()->isGroundUnitLimitReached()
            && CityEconomyInvestmentPolicy::openingWorkersNeeded(itemCount[Unit_Harvester],fundedHarvesterTarget,brutalCityEconomy);
    };
    auto openingWorkersNeeded = [&]() {
        return openingFleetIncomplete() && !harvesterFactories.empty();
    };
    // Keep money for the missing workers when market stock or a delivery is
    // temporarily unavailable. Count queued workers, so the reserve releases
    // as soon as the fleet is funded rather than waiting for it to arrive.
    auto harvesterInvestmentReserve = [&]() {
        if (!isCampaignGameType(currentGame->gameType) || !isAlliedWithHuman()
            || gameMode != GameMode::Custom || supportMode || !(vanillaEconomy || citySimEnabled)
            || lastCalculatedSpice <= 0 || getHouse()->isGroundUnitLimitReached()
            || getHouse()->getNumItems(Structure_Refinery) == 0
            || (harvesterFactories.empty() && getHouse()->getNumItems(Structure_StarPort) == 0)) return 0;
        int target = vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget;
        const auto& market = getHouse()->getChoam();
        if (getHouse()->getNumItems(Structure_StarPort) > 0 && isAlliedWithHuman()
            && market.getPrice(Unit_Harvester) > 0
            && market.getPrice(Unit_Harvester) < data[Unit_Harvester][houseID].price
            && getHouse()->getMaxHarvesters() > 0) target = getHouse()->getMaxHarvesters();
        const int overrideLimit = getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
        if (overrideLimit > 0) target = std::min(target, overrideLimit);
        return std::max(0, target-itemCount[Unit_Harvester]) * data[Unit_Harvester][houseID].price;
    };
    auto needsFirstTransport = [&]() {
        // Preserve early carryalls while Brutal continues growing its fleet.
        if (brutalCityEconomy ? CityEconomyInvestmentPolicy::openingWorkersNeeded(itemCount[Unit_Harvester],fundedHarvesterTarget)
                              : openingWorkersNeeded()) return false;
        return QuantBotBuildPolicy::firstTransportNeeded(transportTechAvailable,
            itemCount[Structure_HeavyFactory],getHouse()->getNumItems(Unit_Harvester),
            itemCount[Unit_Carryall]);
    };

    const auto windSize = getStructureSize(Structure_WindTrap);
    int generationCostPerThousand = 1000 * (data[Structure_WindTrap][houseID].price
        + windSize.x*windSize.y*data[Structure_Slab1][houseID].price)
        / std::max(1,-data[Structure_WindTrap][houseID].power);
    int largestGenerator = 0;
    int currentZonePower = 0, matureZonePower = 0, committedPowerDemand = 0;
    std::array<int,3> developingZones{};
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const int power = data[structure->getItemID()][structure->getOriginalHouseID()].power;
        if (power < 0) {
            largestGenerator = std::max(largestGenerator, -power);
            const auto size = getStructureSize(structure->getItemID());
            const int generationCost = data[structure->getItemID()][structure->getOriginalHouseID()].price
                + size.x*size.y*data[Structure_Slab1][houseID].price;
            generationCostPerThousand = std::min(generationCostPerThousand,1000*generationCost/-power);
        }
        if (citySimEnabled && DuneCity::isCityZoneStructure(structure->getItemID())) {
            const auto* zone = static_cast<const ZoneStructure*>(structure);
            currentZonePower += zone->getZonePowerDraw();
            const Coord pos = zone->getLocation();
            if (getMap().tileExists(pos.x,pos.y)
                && DuneCity::getStructurePopulation(zone,getMap().getTile(pos.x,pos.y)->getCityZoneDensity())
                    < DuneCity::getZonePopulation(zone->getItemID(),1))
                ++developingZones[zone->getItemID()-Structure_ZoneResidential];
            matureZonePower += DuneCity::getZonePower(structure->getItemID(), 3);
        }
    }
    if (citySimEnabled) for (Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
        committedPowerDemand += std::max(0,itemCount[item]-getHouse()->getNumItems(item))
            * (DuneCity::isCityZoneStructure(item) ? DuneCity::getZonePower(item,3) : std::max(0,data[item][houseID].power));
    const int zoneGrowthHeadroom = QuantBotBuildPolicy::cityGrowthPowerHeadroom(
        currentZonePower,matureZonePower,projectedPowerDemandGrowth,committedPowerDemand);
    const int cityPowerReserve = citySimEnabled ? QuantBotBuildPolicy::cityPowerReserve(
        getHouse()->getPowerRequirement(), largestGenerator) + zoneGrowthHeadroom : 0;
    const bool nuclearPlan = citySimEnabled && !needsFirstTransport()
        && itemCount[Structure_HeavyFactory] > 0
        && itemCount[Structure_NuclearPlant] == getHouse()->getNumItems(Structure_NuclearPlant)
        && itemCount[Structure_WindTrap] == getHouse()->getNumItems(Structure_WindTrap)
        && QuantBotBuildPolicy::planNuclearInvestment(getHouse()->getPowerRequirement(),
            getHouse()->getProducedPower(),cityPowerReserve,std::max(1,-data[Structure_WindTrap][houseID].power))
        && nuclearBuildAvailable && findPlaceLocation(Structure_NuclearPlant).isValid();
    int cityYardTarget = citySimEnabled ? QuantBotBuildPolicy::cityConstructionYardTarget(
        money, ownResValve, ownComValve, ownIndValve) : 0;
    if(rockExpansionNeeded)cityYardTarget=std::max(cityYardTarget,getHouse()->getNumItems(Structure_ConstructionYard)+1);
    const int yardLimit = getGameInitSettings().getGameOptions().maximumNumberOfConstructionYardsOverride;
    if(yardLimit > 0) cityYardTarget = std::min(cityYardTarget, yardLimit);
    const int cityConstructionCapacity = itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV];
    // Settling free rock is a space decision, not a capacity one: it is due
    // when the city has nowhere left to build and somewhere safe to go, and it
    // is the only thing that keeps a built-out city growing. It runs beside
    // the yard target rather than through it, so production capacity is still
    // governed by that target alone.
    const bool colonisationNeeded = colonisationMcvDue(itemCount[Unit_MCV],yardLimit);

    // Custom-game city size ceiling for this bot, including shared-house
    // helpers. Campaign games keep their own separate gates.
    const int populationCeiling = getCityPopulationLimit(getMap().getSizeX()*getMap().getSizeY());
    const auto cityGrowthLimits = populationCeiling > 0
        ? QuantBotCityPolicy::limits(static_cast<int>(difficulty), getMap().getSizeX()*getMap().getSizeY())
        : QuantBotCityPolicy::Limits{};
    int cityZonesIncludingQueued = itemCount[Structure_ZoneResidential]
        + itemCount[Structure_ZoneCommercial] + itemCount[Structure_ZoneIndustrial];
    auto initialCityPopulation = [](Uint32 item) {
        return isStructure(item) ? DuneCity::getZonePopulation(item,1)
            + (item==Structure_Palace ? DuneCity::getPalaceCommercialPopulation(1) : 0) : 0;
    };
    int cityPendingPopulation = 0;
    int currentCityPopulation = 0;
    if(populationCeiling > 0) {
        for(Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
            cityPendingPopulation += std::max(0,itemCount[item]-getHouse()->getNumItems(item))*initialCityPopulation(item);
        for(const auto* structure : getStructureList()) {
            if(structure->getOwner()!=getHouse()) continue;
            const auto item=structure->getItemID();
            const Coord pos=structure->getLocation();
            if(!getMap().tileExists(pos.x,pos.y)) continue;
            const int level=DuneCity::isCityZoneStructure(item)
                ? getMap().getTile(pos.x,pos.y)->getCityZoneDensity()
                : DuneCity::effectiveCityLevel(item,std::max(1,int(structure->getCityOccupancy())));
            const int population=DuneCity::getStructurePopulation(structure,level)
                + (item==Structure_Palace ? DuneCity::getPalaceCommercialPopulation(level) : 0);
            currentCityPopulation += population;
            cityPendingPopulation += std::max(0,initialCityPopulation(item)-population);
        }
    }
    const int cityDisplayPopulation = QuantBotCityPolicy::displayPopulation(currentCityPopulation);
    int cityPendingDisplayPopulation = QuantBotCityPolicy::displayPopulation(cityPendingPopulation);
    auto cityAdmitsStructure = [&](Uint32 item) {
        if(populationCeiling <= 0 || !isStructure(item)) return true;
        if(DuneCity::isCityZoneStructure(item)
           && !QuantBotCityPolicy::allowsZone(cityZonesIncludingQueued,cityGrowthLimits.sharedZoneCap)) return false;
        const int added=QuantBotCityPolicy::displayPopulation(initialCityPopulation(item));
        // Zero-population services, power and roads remain available even in an oversized save.
        return added==0 || QuantBotCityPolicy::allowsPopulation(cityDisplayPopulation,
            cityPendingDisplayPopulation,added,populationCeiling);
    };

    auto decisionState = [&]() {
        return AITelemetry::Record().set("credits", getHouse()->getCredits()).set("spendable", money)
            .set("military", militaryValue).set("military_limit", militaryValueLimit)
            .set("unit_limit_reached", getHouse()->isGroundUnitLimitReached()).set("max_units", getHouse()->getMaxUnits())
            .set("power_rules_enabled", powerRules).set("rocket_turrets_need_power", turretPowerRequired).set("city_effects_enabled", citySimEnabled)
            .set("waiting_to_unload",waitingHarvesters).set("unloading_backlog",unloadingBacklog)
            .set("free_refineries",freeRefineries).set("busy_refineries",busyRefineries)
            .set("free_base_rock",availableBaseRock).set("rock_expansion_needed",rockExpansionNeeded)
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked)
            .set("base_built_out",baseBuiltOut())
            .set("colonisation_due",colonisationNeeded)
            .set("economy_reserve", economyReserve).set("queued_production_cost", queuedProductionCost).set("queued_military_value", queuedMilitaryValue)
            .set("power_produced", getHouse()->getProducedPower()).set("power_required", getHouse()->getPowerRequirement())
            .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
            .set("zone_growth_headroom",zoneGrowthHeadroom).set("committed_power_demand",committedPowerDemand)
            .set("nuclear_investment_due",nuclearPlan)
            .set("city_power_reserve_target", cityPowerReserve).set("largest_generator_nominal", largestGenerator)
            .set("res_count", itemCount[Structure_ZoneResidential]).set("com_count", itemCount[Structure_ZoneCommercial])
            .set("ind_count", itemCount[Structure_ZoneIndustrial])
            .set("construction_yards", itemCount[Structure_ConstructionYard])
            .set("mcvs_including_queued", itemCount[Unit_MCV])
            .set("city_yard_target", cityYardTarget)
            .set("city_mcv_cash", citySimEnabled ? money : 0)
            .set("city_mcv_working_reserve", citySimEnabled ? std::max(1000, cityWorkingReserve) : 0)
            .set("city_mcv_shortfall", citySimEnabled ? std::max(0, cityYardTarget - cityConstructionCapacity) : 0)
            .set("vanilla_yard_target", vanillaEconomy ? DuneCity::vanillaYardTarget(
                money, getHouse()->getNumItems(Unit_Harvester)) : 0)
            .set("mcv_upgrade_in_progress", mcvUpgradesInProgress > 0)
            .set("mcv_upgrades_in_progress", mcvUpgradesInProgress)
            .set("mcv_shortfall", vanillaEconomy ? DuneCity::vanillaMcvShortfall(
                money, getHouse()->getNumItems(Unit_Harvester), itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]) : 0)
            .set("construction_plans_first", citySimEnabled)
            .set("res_demand", ownResValve).set("com_demand", ownComValve).set("ind_demand", ownIndValve)
            .set("res_pop", ownResPop).set("com_pop", ownComPop).set("ind_pop", ownIndPop)
            .set("land_value", ownAvgLandValue).set("spice_remaining", lastCalculatedSpice)
            .set("spice_share", spiceShare).set("map_harvester_target", mapSpiceHarvesterTarget)
            .set("harvester_target", spiceHarvesterTarget).set("refinery_harvester_target",
                QuantBotBuildPolicy::refineryThroughputHarvesterTarget(spiceShare, 0,
                    getHouse()->getNumItems(Structure_Refinery), harvesterLimit))
            .set("repair_baseline",repairBaseline).set("repair_target",repairTarget)
            .set("combat_vehicles",combatVehicles).set("repair_waiting",waitingRepairVehicles)
            .set("harvester_investment_reserve", harvesterInvestmentReserve())
            .set("harvester_ai_limit", harvesterLimit).set("campaign_economy_push",campaignEconomyPush)
            .set("campaign_city_policy",campaignCityEconomy()).set("campaign_city_baseline",campaignBaseline.allowance)
            .set("campaign_city_zone_cap",campaignCityLimits().sharedZoneCap).set("campaign_city_post_spice",campaignPostSpice())
            .set("campaign_city_income_goal",QuantBotCityCampaignPolicy::totalIncomeGoalPerMinute(campaignBaseline))
            .set("campaign_city_income_forecast",campaignIncomeForecastPerMinute())
            .set("city_population_limit",cityGrowthLimits.displayPopulationLimit)
            .set("city_population_display",cityDisplayPopulation)
            .set("city_population_pending",cityPendingDisplayPopulation)
            .set("city_zone_cap",cityGrowthLimits.sharedZoneCap)
            .set("city_zones_including_queued",cityZonesIncludingQueued)
            .set("harvester_engine_limit", getHouse()->getMaxHarvesters())
            .set("funded_harvester_target", vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
            .set("storage_capacity", getHouse()->getCapacity())
            .set("nuclear_pending", itemCount[Structure_NuclearPlant] - getHouse()->getNumItems(Structure_NuclearPlant))
            .set("windtrap_pending", itemCount[Structure_WindTrap] - getHouse()->getNumItems(Structure_WindTrap))
            .set("stadium_committed", itemCount[Structure_Stadium]).set("airport_committed", itemCount[Structure_Airport])
            .set("strategic_item", strategicReserveItem).set("strategic_reserve", strategicReserveCost);
    };

    const bool campaignPowerBudget = isCampaignGameType(currentGame->gameType)
        && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium)
        && !citySimEnabled && !vanillaEconomy;
    auto campaignPowerNeeded = [&](int nextDemand) {
        int demand=0;
        for (Uint32 item=Structure_FirstID;item<=Structure_LastID;++item)
            demand+=std::max(0,itemCount[item]-getHouse()->getNumItems(item))*std::max(0,data[item][houseID].power);
        return campaignPowerBudget && CampaignDifficultyPolicy::needsWindtrap(
            getHouse()->getProducedPower(),getHouse()->getPowerRequirement(),demand,nextDemand,
            itemCount[Structure_WindTrap]>getHouse()->getNumItems(Structure_WindTrap)
            || itemCount[Structure_NuclearPlant]>getHouse()->getNumItems(Structure_NuclearPlant));
    };

    auto powerGenerationPending = [&]() {
        return itemCount[Structure_NuclearPlant] > getHouse()->getNumItems(Structure_NuclearPlant)
            || itemCount[Structure_WindTrap] > getHouse()->getNumItems(Structure_WindTrap);
    };

	auto chooseCityZone = [&](const BuilderBase* builder, bool bootstrap, bool missingOnly = false) {
        if (!campaignAllowsZone(itemCount[Structure_ZoneResidential]+itemCount[Structure_ZoneCommercial]+itemCount[Structure_ZoneIndustrial])) return Uint32(NONE_ID);

		auto ranked = QuantBotBuildPolicy::rankZones(
			itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
			itemCount[Structure_ZoneIndustrial], ownResValve, ownComValve, ownIndValve, bootstrap);
        // The site scorer still rewards residential infill. It must not
        // override the chosen zone type and suppress stronger jobs demand.
        Uint32 selected = NONE_ID;
        AITelemetry::Record candidates;
        for (Uint32 candidate : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
            const int demand = candidate == Structure_ZoneResidential ? ownResValve
                : candidate == Structure_ZoneCommercial ? ownComValve : ownIndValve;
            int rank = -1;
            for (int i = 0; i < 3; ++i) if (ranked[i] == candidate) rank = i;
            candidates.set(std::to_string(candidate), AITelemetry::Record().set("rank", rank)
                .set("demand", demand).set("normalized_demand", QuantBotBuildPolicy::normalizedZoneDemand(candidate, demand))
                .set("count_including_queued", itemCount[candidate]));
        }
        AITelemetry::Record evaluated;
        for (Uint32 candidate : ranked) {
            if (candidate == NONE_ID || (missingOnly && itemCount[candidate] > 0)) continue;
            // Demand and site suitability select the tax candidate; the economic
            // comparison below decides whether more spice capacity is better.
            const char* reason = "lower_rank_not_evaluated";
            if (selected == NONE_ID) {
                if (!cityAdmitsStructure(candidate)) reason = "city_limit";
                else if (!campaignAvailableToBuild(builder,candidate)) reason = "unavailable";
                else if (!findPlaceLocation(candidate).isValid()) reason = "no_site";
                else { selected = candidate; reason = "selected"; }
            }
            evaluated.set(std::to_string(candidate), reason);
        }
        // No-site retries are sampled; successful choices are always recorded.
        if (AITelemetry::log().enabled() && (selected != NONE_ID || !lastZoneTraceCycle.count(builder->getObjectID())
                || getGameCycleCount() - lastZoneTraceCycle[builder->getObjectID()] >= MILLI2CYCLES(30000))) {
            lastZoneTraceCycle[builder->getObjectID()] = getGameCycleCount();
            zoneDecisionIds[builder->getObjectID()] = traceDecision("zone_evaluation", AITelemetry::Record()
                .set("builder", builder->getObjectID()).set("bootstrap", bootstrap)
                .set("rule", "normalized_demand_band_then_committed_balance").set("selected", selected)
                .set("expansion_policy", "demand_led_tax_candidate")
                .set("result", selected != NONE_ID ? "selected"
                    : ranked[0] == NONE_ID ? "no_positive_demand" : "no_available_site_or_building")
                .set("state", decisionState()).set("candidates", candidates).set("evaluated", evaluated));
        }
        return selected;
	};

    auto canBuildMilitaryVehicle = [&](const BuilderBase* factory) {
        if (getHouse()->isGroundUnitLimitReached()) return false;
        for (Uint32 item : {Unit_Tank,Unit_SiegeTank,Unit_Launcher,Unit_Devastator,Unit_SonicTank,Unit_Deviator})
            if (factory->isAvailableToBuild(item) && data[item][houseID].price <= money
                && militaryValue + data[item][houseID].price <= militaryValueLimit) return true;
        return false;
    };

    auto demandedCivicForYard = [&](const BuilderBase* yard) {
        if (!citySimEnabled) return Uint32(NONE_ID);
        const auto blocked = currentGame->getCitySimulation()->getHouseState(houseID).civicDemandBlocked;
        auto available = [&](Uint32 item) {
            return yard->isAvailableToBuild(item) && findPlaceLocation(item).isValid();
        };
        return Uint32(CityEconomyInvestmentPolicy::demandedCivic(blocked,
            itemCount[Structure_Stadium], (blocked & DuneCity::NeedStadium) && available(Structure_Stadium),
            itemCount[Structure_Airport], (blocked & DuneCity::NeedAirport) && available(Structure_Airport)));
    };
    int civicReserveCost = 0;
    if (citySimEnabled) for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_ConstructionYard) continue;
        const auto civic = demandedCivicForYard(static_cast<const BuilderBase*>(structure));
        if (civic != NONE_ID) civicReserveCost = std::max(civicReserveCost,int(data[civic][houseID].price));
    }

    // Estimate travel from existing refineries, with a bounded local spice scan.
    // Missing nearby fields retain a conservative long trip instead of inventing
    // local resources. Future receipts are always capped by the remaining share.
    int refineryTripCycles = -1, walkingTripCycles = -1, refineryFieldRisk = 0;
    auto forecastSpiceTrip = [&](Coord site) {
        if (refineryTripCycles >= 0 || site.isInvalid()) return;
        int distance = 65;
        Coord field = Coord::Invalid();
        for (int dy=-32;dy<=32;dy+=2) for (int dx=-32;dx<=32;dx+=2) {
            const Coord p(site.x+dx,site.y+dy);
            if (!getMap().tileExists(p.x,p.y) || !getMap().getTile(p.x,p.y)->hasSpice()) continue;
            const int d=std::abs(dx)+std::abs(dy);
            if (d<distance) {distance=d;field=p;}
        }
        refineryFieldRisk=dangerAt(site,getStructureSize(Structure_Refinery))
            + (field.isValid() ? dangerAt(field,Coord(1,1)) : 0);
        const FixPoint speed=data[Unit_Harvester][houseID].maxspeed*0.75_fix;
        walkingTripCycles=speed>0 ? (FixPoint(2*distance*TILESIZE)/speed).lround()
            : CityEconomyInvestmentPolicy::horizonCycles;
        refineryTripCycles=walkingTripCycles;
        const int workers=std::max(1,actualHarvesters);
        const int transported=std::min(workers,getHouse()->getNumItems(Unit_Carryall)*5);
        const FixPoint airSpeed=data[Unit_Carryall][houseID].maxspeed*0.75_fix;
        if (transported>0 && airSpeed>0 && distance>=MIN_CARRYALL_LIFT_DISTANCE) {
            // Two flights plus a conservative pickup/landing allowance. Credit
            // only the share supported by existing aircraft, never queued ones.
            const int flown=(FixPoint(2*distance*TILESIZE)/airSpeed).lround()+MILLI2CYCLES(12000);
            refineryTripCycles=int((int64_t(walkingTripCycles)*(workers-transported)
                + int64_t(std::min(walkingTripCycles,flown))*transported)/workers);
        }
    };
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->acceptsHarvesterDropoff()) {
            forecastSpiceTrip(structure->getLocation()); break;
        }
    const int fillCycles=(FixPoint(HARVESTERMAXSPICE)/HARVESTSPEED).lround();
    const int unloadCycles=HARVESTERMAXSPICE*8/5;
    auto workerAnnualIncome = [&]() {
        return HARVESTERMAXSPICE*int(DuneCity::kCyclesPerCityYear)
            / std::max(1,fillCycles+unloadCycles+std::max(0,refineryTripCycles));
    };
    const int bayAnnualIncome=int(DuneCity::kCyclesPerCityYear)*15/32;
    auto factoryPrefersHarvester = [&](const BuilderBase* factory) {
        if (harvesterInvestmentReserve() > 0) return true;
        const int workers=itemCount[Unit_Harvester],refs=itemCount[Structure_Refinery];
        if (QuantBotSpendingPolicy::marginalSpice(spiceShare,
            std::min(workers*workerAnnualIncome(),refs*bayAnnualIncome),
            std::min((workers+1)*workerAnnualIncome(),refs*bayAnnualIncome),
            data[Unit_Harvester][houseID].buildtime*15,CityEconomyInvestmentPolicy::horizonCycles,
            DuneCity::kCyclesPerCityYear)<=data[Unit_Harvester][houseID].price) return false;
        return CityEconomyInvestmentPolicy::preferFactoryHarvester(itemCount[Unit_Harvester],
            citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget,militaryValue,militaryValueLimit,
            data[Unit_Harvester][houseID].price,canBuildMilitaryVehicle(factory),
            citySimEnabled && gameMode == GameMode::Custom,brutalCityEconomy);
    };

    CityEconomyInvestmentPolicy::Investment lastEconomyInvestment;
    bool cityRefineryCatchup = false, cityRefineryOpening = false;
    auto chooseCityEconomy = [&](const BuilderBase* builder, bool opening) {
        using namespace CityEconomyInvestmentPolicy;
        lastEconomyInvestment = {};
        cityRefineryCatchup = false;
        cityRefineryOpening = false;
        const Uint32 zone = chooseCityZone(builder, opening);
        const auto* sim = currentGame->getCitySimulation();
        if (!sim) return zone;
        const int tax = sim->getCityTax();
        const int land = ownAvgLandValue>0 ? ownAvgLandValue : 128;
        const int workers = itemCount[Unit_Harvester], refs = itemCount[Structure_Refinery];
        const bool expandingOpening = CityEconomyInvestmentPolicy::openingWorkersNeeded(workers,fundedHarvesterTarget,brutalCityEconomy)
            && brutalCityEconomy;
        const bool openingRefinery = openingRefineryInvestment(brutalCityEconomy,workers,fundedHarvesterTarget,refs);
        const bool wantedWorker = workers < std::min(spiceHarvesterTarget,harvesterLimit);
        bool capacityNeeded = unloadingBacklog, refineryUseful = false;
        int workerIncome = 0, bayIncome = 0;
        bool hedge = zone == Structure_ZoneResidential && itemCount[zone] == 0;
        Investment residential, refinery;
        auto setupInvestment = [&](Uint32 item, Coord site, int power) {
            const Coord size = getStructureSize(item);
            int foundation = 0;
            // Only a game that requires concrete pays for the missing tiles.
            if (getGameInitSettings().getGameOptions().concreteRequired)
                for (int y=site.y;y<site.y+size.y;++y) for (int x=site.x;x<site.x+size.x;++x) {
                    if (!getMap().tileExists(x,y)) continue;
                    const auto* tile = getMap().getTile(x,y);
                    foundation += !tile->hasPreparedFoundation();
                }
            // Amortize available generation (including foundation) even with spare power.
            Investment result;
            result.cost = data[item][houseID].price + foundation*data[Structure_Slab1][houseID].price
                + (power*generationCostPerThousand+999)/1000;
            // Roads have no recurring cost; allow for separate power upkeep.
            result.annualUpkeep = power/8;
            return result;
        };
        if (zone != NONE_ID) {
            const Coord site = findPlaceLocation(zone);
            const bool industry = zone == Structure_ZoneIndustrial;
            const int pollution = sim->getPollutionDensityMap().worldGet(site.x,site.y);
            const int crime = sim->getCrimeRateMap().worldGet(site.x,site.y);
            const int localValue = sim->getLandValueMap().worldGet(site.x,site.y);
            // Forecast low density on ordinary land, medium on good clean land.
            // Do not value a newly zoned plot as an instant high-density tower.
            const int level = localValue>=128 && pollution<=DuneCity::kPollutionGrowthThreshold && crime<128 ? 2 : 1;
            const int population = DuneCity::getZonePopulation(zone,level);
            const int power = DuneCity::getZonePower(zone,level);
            const int unfinished = developingZones[zone-Structure_ZoneResidential]
                + std::max(0,itemCount[zone]-getHouse()->getNumItems(zone));
            const int demand = zone == Structure_ZoneResidential ? ownResValve : industry ? ownIndValve : ownComValve;
            residential = setupInvestment(zone,site,power);
            residential.annualIncome = DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(zone,population,level),tax,land);
            if (zone != Structure_ZoneResidential) {
                // One C/I job supports eight residents. Credit only half the
                // tax of existing/pending housing currently short of jobs.
                const int waitingR = developingZones[0] + std::max(0,itemCount[Structure_ZoneResidential]
                    - getHouse()->getNumItems(Structure_ZoneResidential));
                const int shortage = std::max(0,ownResPop+waitingR*16-(ownComPop+ownIndPop)*8);
                residential.annualIncome += DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(
                    Structure_ZoneResidential,std::min(shortage,population*8),0),tax,land)/2;
            }
            residential.delayCycles = DuneCity::getCityBuildTime(zone,data[zone][houseID].buildtime)*15
                + DuneCity::kCyclesPerCityYear; // construction plus ~60 s growth/foundation allowance
            residential.confidence = zoneConfidence(demand,zone==Structure_ZoneResidential ? 2000 : 1500,
                industry ? 0 : pollution,crime,unfinished);
        }
        Coord refinerySite = Coord::Invalid();
        if ((spiceShare>0 || unloadingBacklog) && campaignAvailableToBuild(builder,Structure_Refinery))
            refinerySite = findPlaceLocation(Structure_Refinery);
        Coord forecastSite=refinerySite;
        if(forecastSite.isInvalid()) for(const auto* structure:getStructureList())
            if(structure->getOwner()==getHouse()&&structure->getItemID()==Structure_Refinery){forecastSite=structure->getLocation();break;}
        if (forecastSite.isValid()) {
            forecastSpiceTrip(forecastSite);
            const int roundTrip=fillCycles+unloadCycles+std::max(0,refineryTripCycles);
            workerIncome=workerAnnualIncome();
            bayIncome=bayAnnualIncome;
            capacityNeeded = unloadingBacklog || processingCapacityNeeded(refs,workers,workerIncome,bayIncome);
            if(refinerySite.isValid()) {
            refineryUseful = considerRefinery(capacityNeeded,wantedWorker,!harvesterFactories.empty(),workers < 2 || openingRefinery);
            const bool freeWorker = workers < harvesterLimit;
            const int power = std::max(0,data[Structure_Refinery][houseID].power);
            refinery = setupInvestment(Structure_Refinery,refinerySite,power);
            refinery.annualIncome = marginalSpiceIncome(workers,refs,freeWorker,workerIncome,bayIncome);
            const int refineryBuildCycles = data[Structure_Refinery][houseID].buildtime*15;
            refinery.delayCycles = refineryBuildCycles + roundTrip;
            refinery.projectedProceeds = refineryProceeds(workers,refs,freeWorker,workerIncome,bayIncome,
                refineryBuildCycles,roundTrip,unloadCycles,HARVESTERMAXSPICE,refinery.annualUpkeep);
            const int before=std::min(workers*workerIncome,refs*bayIncome);
            const int after=std::min((workers+int(freeWorker))*workerIncome,(refs+1)*bayIncome);
            const int finiteReturn=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                refineryBuildCycles,horizonCycles,DuneCity::kCyclesPerCityYear);
            refinery.projectedProceeds=std::min(refinery.projectedProceeds,
                std::max(0,finiteReturn-refinery.annualUpkeep*4));
            if (refineryFieldRisk>0) refinery.confidence/=2;
            if(unloadingBacklog) {
                refinery.projectedProceeds=std::max(refinery.projectedProceeds,
                    std::min(waitingCargo,2*int(HARVESTERMAXSPICE))-refinery.annualUpkeep*4);
                refinery.confidence=1000; // Already harvested cargo, no remaining-spice risk.
            }
            }
        }
        const int taxIncome = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,tax,land);
        int developingIncome = 0;
        for (Uint32 kind : {Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
            const int pending = developingZones[kind-Structure_ZoneResidential]
                + std::max(0,itemCount[kind]-getHouse()->getNumItems(kind));
            const int demand = kind==Structure_ZoneResidential ? ownResValve
                : kind==Structure_ZoneCommercial ? ownComValve : ownIndValve;
            if (demand > 0) developingIncome += pending * DuneCity::computeAnnualTaxRevenue(
                DuneCity::taxablePopulationEighths(kind,DuneCity::getZonePopulation(kind,1),1),tax,land)/2;
        }
        const int fleetIncome = workers * workerIncome;
        // Positive demand and marginal return decide growth, independently of
        // the current tax/spice ratio. Only the very first housing seed is special.
        Uint32 selected = preferRefinery(refinery,residential,refineryUseful,hedge,capacityNeeded) ? Structure_Refinery : zone;
        cityRefineryCatchup = selected == Structure_Refinery && capacityNeeded;
        cityRefineryOpening = selected == Structure_Refinery && openingRefinery;
        if (selected == zone && zone!=NONE_ID && residential.confidence==0) selected=NONE_ID;
        lastEconomyInvestment=selected==Structure_Refinery ? refinery : selected==zone ? residential : Investment{};
        // Save for a winning refinery instead of spending its money on another
        // cheap lot every pass. The caller preserves urgent non-economic work.
        const bool funded = selected!=NONE_ID && money>=data[selected][houseID].price;
        if (AITelemetry::log().enabled()) {
            lastEconomyTraceCycle[builder->getObjectID()]=getGameCycleCount();
            auto describe = [](const Investment& i) { return AITelemetry::Record().set("cost",i.cost)
                .set("annual_income",i.annualIncome).set("annual_upkeep",i.annualUpkeep)
                .set("delay_cycles",i.delayCycles).set("confidence_per_mille",i.confidence).set("proceeds",i.proceeds()); };
            traceDecision("city_economy_comparison",AITelemetry::Record().set("builder",builder->getObjectID())
                .set("selected",selected).set("zone",zone).set("hedge",hedge).set("funded",funded)
                .set("capacity_needed",capacityNeeded).set("refinery_useful",refineryUseful)
                .set("unloading_backlog",unloadingBacklog).set("waiting_to_unload",waitingHarvesters).set("waiting_cargo",waitingCargo)
                .set("refinery_site_valid",refinerySite.isValid())
                .set("refinery_available",campaignAvailableToBuild(builder,Structure_Refinery))
                .set("refinery_placement",placementScoreDetails[Structure_Refinery])
                .set("brutal_opening",expandingOpening).set("opening_refinery",openingRefinery)
                .set("wanted_included_worker",wantedWorker)
                .set("worker_income",workerIncome).set("bay_capacity",bayIncome)
                .set("generation_cost_per_thousand",generationCostPerThousand)
                .set("workers",workers).set("refineries",refs)
                .set("factory_can_supply",!harvesterFactories.empty())
                .set("tax_income",taxIncome).set("developing_tax_income",developingIncome)
                .set("forecast_fleet_income",fleetIncome)
                .set("sustainable_workers",spiceHarvesterTarget).set("horizon_cycles",horizonCycles)
                .set("spice_share",spiceShare).set("trip_cycles",refineryTripCycles).set("field_risk",refineryFieldRisk)
                .set("tax_candidate",describe(residential)).set("refinery_candidate",describe(refinery)));
        }
        return selected;
    };

	bool emitStatsLog = false;

    if (!supportMode && (militaryValue > 0 || getHouse()->getNumStructures() > 0)) {
        const Uint32 currentCycle = getGameCycleCount();
        if(currentCycle - lastStatsLogCycle >= MILLI2CYCLES(30000)) {
			emitStatsLog = true;
            if (gameMode == GameMode::Custom) {
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d  built: %d  kill: %d  loss: %d remaining spice: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, getHouse()->getUnitBuiltValue(),
                    getHouse()->getKillValue(), getHouse()->getLossValue(), lastCalculatedSpice, getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            } else {
                // Campaign mode - include initial military value and multiplier
                const QuantBotConfig& config = getQuantBotConfig();
                const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d (init: %d, mult: %.1fx)  built: %d  kill: %d  loss: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, initialMilitaryValue, diffSettings.militaryValueMultiplier,
                    getHouse()->getUnitBuiltValue(), getHouse()->getKillValue(), getHouse()->getLossValue(), getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            }
            lastStatsLogCycle = currentCycle;
        }
    }


    if (AITelemetry::log().enabled() && (telemetryState == 0
            || getGameCycleCount() - lastTelemetrySnapshotCycle >= MILLI2CYCLES(30000))) {
        lastTelemetrySnapshotCycle = getGameCycleCount();
        AITelemetry::Record actual, queued;
        for (int i = ItemID_FirstID; i <= ItemID_LastID; ++i) {
            const int count = getHouse()->getNumItems(i);
            if (count) actual.set(std::to_string(i), count);
            if (itemCount[i] != count) queued.set(std::to_string(i), itemCount[i] - count);
        }
        AITelemetry::Record harvesters;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() != getHouse()) continue;
            if (const auto* harvester = dynamic_cast<const Harvester*>(unit)) {
                const auto pos = unit->getLocation(), dest = unit->getDestination();
                harvesters.set(std::to_string(unit->getObjectID()), AITelemetry::Record()
                    .set("x", pos.x).set("y", pos.y).set("destination_x", dest.x).set("destination_y", dest.y)
                    .set("active", unit->isActive()).set("harvesting", harvester->isHarvesting())
                    .set("returning", harvester->isReturning()).set("cargo", harvester->getAmountOfSpice().lround())
                    .set("health", unit->getHealth().lround()).set("mode", unit->getAttackMode()));
            }
        }
        AITelemetry::Record cityHealth;
        if (citySimEnabled && currentGame->getCitySimulation()) {
            auto* sim = currentGame->getCitySimulation();
            const auto& hs = sim->getHouseState(getHouse()->getHouseID());
            int samples = 0, pollution = 0, crime = 0, traffic = 0;
            int resCrime = 0, comCrime = 0, extremeCrime = 0, slowPollution = 0, blockedPollution = 0;
            // Keep telemetry in the same named bands shown to players and
            // defined by Micropolis's crime overlay.
            int crimeBands[4] = {};
            for (const StructureBase* structure : getStructureList()) {
                if (structure->getOwner() != getHouse()) continue;
                const Uint32 type = structure->getItemID();
                if (type < Structure_ZoneResidential || type > Structure_ZoneIndustrial) continue;
                const Coord p = structure->getLocation();
                ++samples;
                pollution += sim->getPollutionDensityMap().worldGet(p.x, p.y);
                const int localCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                const int localPollution = sim->getPollutionDensityMap().worldGet(p.x, p.y);
                crime += localCrime;
                ++crimeBands[localCrime < 64 ? 0 : localCrime < 128 ? 1 : localCrime < 192 ? 2 : 3];
                resCrime += type == Structure_ZoneResidential && localCrime > 100;
                comCrime += type == Structure_ZoneCommercial && localCrime > 80;
                extremeCrime += localCrime > 190;
                if (type != Structure_ZoneIndustrial) {
                    slowPollution += localPollution >= 80;
                    blockedPollution += localPollution >= 160;
                }
                traffic += sim->getTrafficDensityMap().worldGet(p.x, p.y);
            }
            cityHealth.set("unemployment", hs.unemploymentRate).set("tax_rate", sim->getCityTax())
                .set("last_tax_revenue", hs.budget.getLastTaxRevenue()).set("police_expense", hs.lastPoliceExpense)
                .set("tax_base_eighths", hs.taxBaseEighths)
                .set("road_expense", 0)
                .set("zone_origin_samples", samples).set("pollution_mean", samples ? pollution / samples : 0)
                .set("crime_mean", samples ? crime / samples : 0).set("traffic_mean", samples ? traffic / samples : 0)
                .set("res_crime_above100",resCrime).set("com_crime_above80",comCrime).set("crime_above190",extremeCrime)
                .set("res_com_pollution_atleast80",slowPollution).set("res_com_pollution_atleast160",blockedPollution)
                .set("crime_bands", AITelemetry::Record().set("safe_0_63",crimeBands[0])
                    .set("light_64_127",crimeBands[1]).set("moderate_128_191",crimeBands[2])
                    .set("dangerous_192_250",crimeBands[3]));
            if (telemetryState == 0 || getGameCycleCount()-lastCityBuildingSnapshotCycle >= MILLI2CYCLES(120000)) {
                lastCityBuildingSnapshotCycle = getGameCycleCount();
                AITelemetry::Record buildings;
                int count = 0;
                for (const auto* structure : getStructureList()) {
                    if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
                    const auto item = structure->getItemID();
                    const auto pos = structure->getLocation(), size = structure->getStructureSize();
                    const auto* tile = getMap().getTile(pos.x,pos.y);
                    if (!tile) continue;
                    const auto role = DuneCity::getStructureCityRole(item);
                    const int level = DuneCity::isCityZoneStructure(item) ? tile->getCityZoneDensity()
                        : role == DuneCity::CityRole::None ? 0 : std::max(1,int(structure->getCityOccupancy()));
                    int generation = 0;
                    if (const auto* reactor = dynamic_cast<const NuclearPlant*>(structure)) generation = reactor->getProducedPower();
                    else if (const auto* windtrap = dynamic_cast<const WindTrap*>(structure)) generation = windtrap->getProducedPower();
                    buildings.set(std::to_string(structure->getObjectID()), AITelemetry::Record()
                        .set("item",item).set("x",pos.x).set("y",pos.y).set("width",size.x).set("height",size.y)
                        .set("health",structure->getHealth().lround()).set("max_health",structure->getMaxHealth())
                        .set("role",static_cast<int>(role)).set("level",level).set("max_level",DuneCity::getStructureMaxLevel(item))
                        .set("population",DuneCity::getStructurePopulation(structure,level))
                        .set("res_supply",DuneCity::getStructureResidentialSupply(structure,level))
                        .set("com_supply",DuneCity::getCommercialSupply(item,level)).set("ind_supply",DuneCity::getIndustrialSupply(item,level))
                        .set("palace_com_population",item == Structure_Palace ? DuneCity::getPalaceCommercialPopulation(level) : 0)
                        .set("land_value",sim->getLandValueMap().worldGet(pos.x,pos.y))
                        .set("population_density",sim->getPopulationDensityMap().worldGet(pos.x,pos.y))
                        .set("pollution",sim->getPollutionDensityMap().worldGet(pos.x,pos.y))
                        .set("crime",sim->getCrimeRateMap().worldGet(pos.x,pos.y))
                        .set("crime_before_police",sim->getCrimeBeforePoliceMap().worldGet(pos.x,pos.y))
                        .set("police_coverage",sim->getPoliceCoverageMap().worldGet(pos.x,pos.y))
                        .set("traffic_density",sim->getTrafficDensityMap().worldGet(pos.x,pos.y))
                        .set("growth_rate",sim->getGrowthRateMap().worldGet(pos.x,pos.y))
                        .set("pollution_emission",DuneCity::getPollutionEmission(item,level))
                        .set("land_value_bonus",DuneCity::getParkLandValueBonus(item))
                        .set("police_strength",DuneCity::getPoliceCoverage(item)).set("police_cost",DuneCity::getPoliceAnnualCost(item).lround()).set("police_cost_milli",(DuneCity::getPoliceAnnualCost(item)*1000).lround())
                        .set("power_nominal",currentGame->objectData.data[item][structure->getOriginalHouseID()].power)
                        .set("power_generated",generation));
                    ++count;
                }
                traceDecision("city_building_snapshot", AITelemetry::Record().set("buildings",buildings).set("count",count)
                    .set("police_funding",hs.policeFundingPercent).set("state",decisionState()));
            }
        }

        int actualGeneratorPower = 0;
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse()) continue;
            if (const auto* wind = dynamic_cast<const WindTrap*>(structure)) actualGeneratorPower += wind->getProducedPower();
            else if (const auto* nuclear = dynamic_cast<const NuclearPlant*>(structure)) actualGeneratorPower += nuclear->getProducedPower();
            else if (const auto* advanced = dynamic_cast<const AdvancedWindTrap*>(structure)) actualGeneratorPower += advanced->getProducedPower();
            else if (const auto* scout = dynamic_cast<const Scoutpost*>(structure)) actualGeneratorPower += scout->getProducedPower();
        }
        // Observer-only comparison data, never fed back into AI decisions.
        AITelemetry::Record opponents;
        for (int h = 0; h < NUM_HOUSES; ++h) {
            const auto* house = currentGame->getHouse(h);
            if (!house) continue;
            int survivingMilitary = 0;
            for (Uint32 type=Unit_FirstID; type<=Unit_LastID; ++type)
                if (type != Unit_Carryall && type != Unit_Harvester && type != Unit_MCV && type != Unit_Sandworm)
                    survivingMilitary += house->getNumItems(type) * currentGame->objectData.data[type][h].price;
            opponents.set(std::to_string(h), AITelemetry::Record()
                .set("team", house->getTeamID()).set("alive", house->isAlive())
                .set("credits", house->getCredits()).set("military", survivingMilitary).set("military_basis", "current_unit_counts")
                .set("harvesters", house->getNumItems(Unit_Harvester))
                .set("refineries", house->getNumItems(Structure_Refinery))
                .set("heavy_factories", house->getNumItems(Structure_HeavyFactory))
                .set("construction_yards", house->getNumItems(Structure_ConstructionYard))
                .set("economy_totals", AITelemetry::log().economyTotals(h)).set("combat_rewards", house->combatRewardStats(currentGame->objectData))
                .set("deviation_rewards", house->deviationRewardStats()));
        }
        telemetryState = AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), "state_snapshot",
            AITelemetry::Record().set("ai", "QuantBot").set("name", getPlayername())
                .set("difficulty", static_cast<int>(difficulty)).set("support", supportMode)
                .set("house_name", getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())))
                .set("team", getHouse()->getTeamID())
                .set("state", decisionState()).set("city_health", cityHealth).set("actual", actual).set("queued", queued)
                .set("harvesters", harvesters).set("refinery_queues",refineryQueues).set("house_comparison", opponents)
                .set("power_accounting", AITelemetry::Record().set("generators", actualGeneratorPower)
                    .set("reported", getHouse()->getProducedPower())
                    .set("difference", getHouse()->getProducedPower() - actualGeneratorPower))
                .set("economy_totals", AITelemetry::log().economyTotals(houseID))
                .set("economy", AITelemetry::Record()
                    .set("harvested_spice_total", getHouse()->getHarvestedSpice().lround())
                    .set("stored_spice_credits", getHouse()->getStoredCredits().lround())
                    .set("city_credit_balance", getHouse()->getCityCredits().lround())
                    .set("starting_credit_balance", getHouse()->getStartingCredits().lround()))
                .set("heavy_busy", activeHeavyFactoryCount).set("light_busy",activeLightFactoryCount).set("repair_busy", activeRepairYardCount)
                .set("built_value", getHouse()->getUnitBuiltValue()).set("kill_value", getHouse()->getKillValue())
                .set("loss_value", getHouse()->getLossValue()));
    }

    // Infantry retains its separate difficulty quota. Vehicle openings use
    // the current technology and actual production availability below.
	int infantryPercent = 10;
	switch (difficulty) {
		case Difficulty::Defend:
		case Difficulty::Easy:
			infantryPercent = 18;
			break;
		case Difficulty::Medium:
			infantryPercent = 15;
			break;
		case Difficulty::Hard:
			infantryPercent = 12;
			break;
		case Difficulty::Brutal:
			break;
	}

    constexpr std::array<Uint32,8> mixItems = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
        Unit_SonicTank, Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};
    UnitMixPolicy::Weights damage{}, rewardMilli{}, killBonusMilli{}, lostValue{}, scores{}, defaults{}, prices{}, lossMilli{};
    const auto& ratios = getQuantBotConfig().getRatios(houseID);
    const std::array<int,5> configured = {static_cast<int>(ratios.tank*10000),
        static_cast<int>(ratios.siegeTank*10000), static_cast<int>(ratios.launcher*10000),
        static_cast<int>(ratios.special*10000), static_cast<int>(ratios.ornithopter*10000)};
    std::array<bool,8> available{};
    for (const auto* structure : getStructureList()) {
        const auto* builder = dynamic_cast<const BuilderBase*>(structure);
        if (!builder || builder->getOwner() != getHouse()
            || (builder->getItemID() != Structure_LightFactory && builder->getItemID() != Structure_HeavyFactory
                && builder->getItemID() != Structure_HighTechFactory)) continue;
        for (size_t i=0; i<8; ++i) {
            if (i == 3) {
                for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    available[i] |= campaignAvailableToBuild(builder,special);
            } else available[i] |= campaignAvailableToBuild(builder,mixItems[i]);
        }
    }
    // Losing a producer is a rebuilding need, not evidence that its units
    // should disappear from the strategic army mix. Use saved house history
    // so this also survives loading a game after the factory was destroyed.
    // Actual production still uses each factory's normal build/upgrade gates.
    if (getHouse()->getNumLostItems(Structure_HeavyFactory) > 0) {
        auto recoverableHeavyUnit = [&](Uint32 unit) {
            const auto& spec = data[unit][houseID];
            if (!spec.enabled || spec.price <= 0 || spec.techLevel > currentGame->techLevel
                || spec.builder != Structure_HeavyFactory) return false;
            for (int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
                if (spec.prerequisiteStructuresSet[prerequisite]
                    && getHouse()->getNumItems(prerequisite) == 0
                    && getHouse()->getNumLostItems(prerequisite) == 0) return false;
            return true;
        };
        for (size_t i = 0; i < 4; ++i) {
            if (i == 3) {
                for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    available[i] |= recoverableHeavyUnit(special);
            } else available[i] |= recoverableHeavyUnit(mixItems[i]);
        }
    }
    if (getHouse()->isAirUnitLimitReached()) available[4]=false;
    UnitMixPolicy::Weights configuredWeights{};
    for (size_t i=0; i<5; ++i) configuredWeights[i] = configured[i];
    const auto openingMix = UnitMixPolicy::openingMix(currentGame->techLevel, configuredWeights, available);
    for (size_t i=0; i<8; ++i) defaults[i] = openingMix[i];
    int64_t totalDamage = 0, totalRewardMilli = 0, totalLostValue = 0;
    for (size_t i=0; i<8; ++i) {
        const Uint32 item = mixItems[i];
        int priorPrice = std::max(1, data[item][houseID].price);
        damage[i] = std::max(0, getHouse()->getNumItemDamageInflicted(item));
        rewardMilli[i] = getHouse()->getCombatReward(item).total();
        killBonusMilli[i] = getHouse()->getCombatReward(item).killBonusMilli;
        lostValue[i] = int64_t(getHouse()->getNumLostItems(item)) * data[item][houseID].price;
        bool enabled = data[item][houseID].enabled && data[item][houseID].price > 0
            && data[item][houseID].techLevel <= currentGame->techLevel;
        if (i == 3) {
            damage[i] = rewardMilli[i] = killBonusMilli[i] = lostValue[i] = 0;
            enabled = false;
            priorPrice = 700; // Preserve the special-unit group's existing prior.
            for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator}) {
                damage[i] += getHouse()->getNumItemDamageInflicted(special);
                rewardMilli[i] += getHouse()->getCombatReward(special).total();
                killBonusMilli[i] += getHouse()->getCombatReward(special).killBonusMilli;
                lostValue[i] += int64_t(getHouse()->getNumLostItems(special)) * data[special][houseID].price;
                enabled |= data[special][houseID].enabled && data[special][houseID].price > 0
                    && data[special][houseID].techLevel <= currentGame->techLevel;
            }
            damage[i] = std::max<int64_t>(0, damage[i]);
        }
        totalDamage += damage[i];
        totalRewardMilli += rewardMilli[i];
        totalLostValue += lostValue[i];
        prices[i] = priorPrice * 1000;
        lossMilli[i] = lostValue[i] * 1000;
        if (!enabled || !available[i]) { available[i] = false; continue; }
        scores[i] = UnitMixPolicy::performanceScore(rewardMilli[i], lostValue[i]*1000, priorPrice*1000);
    }
    const bool learningUnitMix = totalRewardMilli >= 3000000;
    performanceHistory.update(getGameCycleCount(),rewardMilli,lossMilli);
    if (learningUnitMix) {
        for (size_t i=0;i<8;++i) scores[i]=available[i]
            ? UnitMixPolicy::performanceScore(performanceHistory.reward[i],performanceHistory.loss[i],prices[i]) : 0;
        scores=UnitMixPolicy::exploredScores(scores,performanceHistory.reward,performanceHistory.loss,prices,available);
    }
    const auto allocationWeights = UnitMixPolicy::sharpenScores(scores);
    const auto rawMix = UnitMixPolicy::normalize(allocationWeights);
    const int performanceConfidenceBps = UnitMixPolicy::evidenceConfidenceBps(totalLostValue, militaryValue);
    const auto unitMix = UnitMixPolicy::allocate(scores, defaults, learningUnitMix, vanillaEconomy,
                                                  totalLostValue, militaryValue);
    lastUnitMixBps = unitMix;
    const int rawOrnithopterBps = rawMix[4];
    const FixPoint tankPercent = FixPoint(unitMix[0])/10000;
    const FixPoint siegePercent = FixPoint(unitMix[1])/10000;
    const FixPoint launcherPercent = FixPoint(unitMix[2])/10000;
    const FixPoint specialPercent = FixPoint(unitMix[3])/10000;
    const FixPoint ornithopterPercent = FixPoint(unitMix[4])/10000;
    const int lightVehicleBps = unitMix[5] + unitMix[6] + unitMix[7];
    const int lightVehiclePercent = lightVehicleBps / 100;

	int lightVehicleValue = data[Unit_Trike][houseID].price * itemCount[Unit_Trike]
		+ data[Unit_RaiderTrike][houseID].price * itemCount[Unit_RaiderTrike]
		+ data[Unit_Quad][houseID].price * itemCount[Unit_Quad];
	int infantryValue = data[Unit_Soldier][houseID].price * itemCount[Unit_Soldier]
		+ data[Unit_Infantry][houseID].price * itemCount[Unit_Infantry]
		+ data[Unit_Trooper][houseID].price * itemCount[Unit_Trooper]
		+ data[Unit_Troopers][houseID].price * itemCount[Unit_Troopers];
	int lightVehicleCount = itemCount[Unit_Trike] + itemCount[Unit_RaiderTrike] + itemCount[Unit_Quad];
	int infantryCount = itemCount[Unit_Soldier] + itemCount[Unit_Infantry]
		+ itemCount[Unit_Trooper] + itemCount[Unit_Troopers];

    const int productionCash = std::max(0, money
        - std::max(strategicReserveCost, economyReserve) - (citySimEnabled ? cityWorkingReserve : 0));
    const int fundedArmyValue = QuantBotBuildPolicy::fundedArmyTarget(militaryValue, militaryValueLimit, productionCash);
    const int vehiclePlanValue = std::max(0, fundedArmyValue - infantryValue);
    if (emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("unit_mix", AITelemetry::Record().set("basis", !learningUnitMix ? "configured" : vanillaEconomy ? "lifetime_evidence_weighted_value_per_loss" : "lifetime_value_per_loss")
            .set("tank_bps", (tankPercent * 10000).lround()).set("siege_bps", (siegePercent * 10000).lround())
            .set("special_bps", (specialPercent * 10000).lround()).set("launcher_bps", (launcherPercent * 10000).lround())
            .set("ornithopter_bps", (ornithopterPercent * 10000).lround()).set("raw_ornithopter_bps", rawOrnithopterBps)
            .set("light_vehicle_percent", lightVehiclePercent).set("light_vehicle_bps", lightVehicleBps)
            .set("trike_bps", unitMix[5]).set("raider_trike_bps", unitMix[6]).set("quad_bps", unitMix[7])
            .set("allocation_types", 8).set("tech_level", currentGame->techLevel)
            .set("opening_light_bps", openingMix[5]+openingMix[6]+openingMix[7]).set("total_damage", totalDamage)
            .set("total_reward_milli", totalRewardMilli).set("total_lost_value", totalLostValue)
            .set("performance_exponent_milli",1500).set("performance_confidence_bps", performanceConfidenceBps).set("funded_army_value",fundedArmyValue)
            .set("vehicle_plan_value",vehiclePlanValue).set("queued_military_value",queuedMilitaryValue).set("mix_inputs", [&]() {
                AITelemetry::Record inputs;
                for (size_t i=0; i<8; ++i) inputs.set(i == 3 ? "special" : std::to_string(mixItems[i]),
                    AITelemetry::Record().set("damage", damage[i]).set("reward_milli", rewardMilli[i]).set("kill_bonus_milli", killBonusMilli[i])
                        .set("lost_value", lostValue[i]).set("score", scores[i]).set("allocation_weight",allocationWeights[i])
                        .set("lifetime_reward_milli",performanceHistory.reward[i]).set("lifetime_loss_milli",performanceHistory.loss[i])
                        .set("available", available[i]).set("opening_bps", openingMix[i]).set("target_bps", unitMix[i]));
                return inputs;
            }()).set("infantry_percent", infantryPercent));
    }

	// lets analyse damage inflicted

	if (emitStatsLog) {
		logDebug("Adaptive unit mix: damage=%lld light=%d bps", static_cast<long long>(totalDamage), lightVehicleBps);

		logDebug("  Tank: %d/%d %f Siege: %d/%d %f Special: %d/%d %f Launch: %d/%d %f Orni: %d/%d %f",
			getHouse()->getNumItemDamageInflicted(Unit_Tank), getHouse()->getNumLostItems(Unit_Tank) * 300, tankPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SiegeTank), getHouse()->getNumLostItems(Unit_SiegeTank) * 600, siegePercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SonicTank) + getHouse()->getNumItemDamageInflicted(Unit_Devastator) + getHouse()->getNumItemDamageInflicted(Unit_Deviator),
			getHouse()->getNumLostItems(Unit_SonicTank) * 600 + getHouse()->getNumLostItems(Unit_Devastator) * 800 + getHouse()->getNumLostItems(Unit_Deviator) * 750,
			specialPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Launcher), getHouse()->getNumLostItems(Unit_Launcher) * 450, launcherPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Ornithopter), getHouse()->getNumLostItems(Unit_Ornithopter) * data[Unit_Ornithopter][houseID].price, ornithopterPercent.toDouble()
		);
	}

	// End of adaptive unit prioritisation algorithm

	// Track unique structures ordered this tick to prevent multiple CYs
	// from building the same thing. Zones and turrets are excluded (want multiples).
	std::set<Uint32> orderedThisTick;
    bool roadMaintenanceAttempted = false;

    // One cross-queue capital comparison. Protect only its next purchase; all
    // independent builders may use the remainder. Recovery/prerequisite rules
    // still run, but optional growth cannot consume another queue's commitment.
    auto purchasePrice = [](const BuilderBase* builder, Uint32 item) {
        for (const auto& offer : builder->getBuildList())
            if (offer.itemID == item) return int(offer.price);
        return 0;
    };
    bool defendingEconomy=false;
    // Concrete is no longer an investment decision. Every ordinary building is
    // prepared in full before it is ordered, in every mode and at every
    // difficulty, because half health is a permanent loss of production,
    // generation, land value or armour. Walls, roads and the slabs themselves
    // are tile states with no placement penalty and stay exempt, and the yard
    // an MCV deploys is never queued, so it keeps its own exception.
    // An incomplete plan defers the building; it never places it bare.
    auto foundationPlan = [&](const BuilderBase* builder, Uint32 item, Coord site,
                              const std::vector<Uint32>& clearedZones = std::vector<Uint32>{}) {
        QuantBotFoundationPolicy::Plan plan;
        if (!getGameInitSettings().getGameOptions().concreteRequired
            || !QuantBotBuildPolicy::foundationRequiredForItem(item) || !site.isValid()) {
            plan.complete=true;
            return plan;
        }
        const Coord size=getStructureSize(item);
        if (size.x<=0 || size.y<=0) return plan;
        // A builder with no slab in its list can still take a footprint that
        // roads or existing concrete already prepare; anything else is deferred
        // rather than placed bare.
        return QuantBotFoundationPolicy::planFoundation(site.x,site.y,size.x,size.y,
            campaignAvailableToBuild(builder,Structure_Slab4),
            campaignAvailableToBuild(builder,Structure_Slab1),
            [&](int x,int y) {
                QuantBotFoundationPolicy::TileState state;
                if (!getMap().tileExists(x,y)) return state;
                const Tile* tile=getMap().getTile(x,y);
                state.exists=true;
                state.prepared=tile->hasPreparedFoundation();
                state.road=tile->isRoad();
                const auto* occupant=tile->getNonInfantryGroundObject();
                const bool cleared=occupant && std::find(clearedZones.begin(),clearedZones.end(),
                    occupant->getObjectID())!=clearedZones.end();
                state.paveable=tile->isRock() && !tile->isMountain()
                    && (!tile->isBlocked() || cleared) && (!tile->hasCityZone() || cleared);
                state.inBuildRange=getMap().isWithinBuildRange(x,y,getHouse());
                return state;
            });
    };
    auto foundationOrders = [&](const BuilderBase* builder, Uint32 item, Coord site) {
        return foundationPlan(builder,item,site).orders;
    };
    // The buildings this bot keeps at full health regardless of wealth. Used
    // both by the repair pass below and by the generation investment, which
    // prices exactly the repairs a closed power deficit would stop paying for.
    // StructureBase charges repairs from the original owner's price table, so
    // captured buildings and mod overrides are classified the way the engine
    // actually bills them.
    auto repairPrice = [&](const StructureBase* structure) {
        return data[structure->getItemID()][structure->getOriginalHouseID()].price;
    };
    auto repairPolicyMaintains = [&](const StructureBase* structure) {
        return QuantBotBuildPolicy::repairMaintainsValue(structure->getItemID(),citySimEnabled,
            structure->getMaxHealth(),repairPrice(structure));
    };
    struct CapitalCandidate {
        Uint32 builder=NONE_ID, item=NONE_ID;
        int price=0, cost=0, proceeds=0, score=0, delay=0, capacity=0, foundationCost=0;
        const char* kind="economy";
        const char* reason="unavailable";
        Coord site=Coord::Invalid();
    };
    std::vector<CapitalCandidate> capitalCandidates;
    AITelemetry::Record capitalProducers, capitalUnitOptions;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isVisible(getHouse()->getTeamID())
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const auto* victim=unit->getTarget();
        if (victim && victim->getOwner()==getHouse() && unit->isInWeaponRange(victim)
            && (victim->isAStructure() || victim->getItemID()==Unit_Harvester
                || victim->getItemID()==Unit_RebelHarvester)) defendingEconomy=true;
    }
    const int forecastTax=citySimEnabled ? DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,
        currentGame->getCitySimulation()->getCityTax(),ownAvgLandValue) : 0;
    const int forecastUpkeep=citySimEnabled ? getHouse()->getPowerRequirement()/8
        + currentGame->getCitySimulation()->getHouseState(houseID).lastPoliceExpense : 0;
    // Future workers/bays count toward targets and commitments, but their
    // production is not income until delivered. Reforecast every planning pass.
    const int fleetRate=std::min(actualHarvesters*workerAnnualIncome(),
        getHouse()->getNumItems(Structure_Refinery)*bayAnnualIncome);
    const int forecastSpice=std::min(spiceShare,fleetRate*QuantBotSpendingPolicy::horizonMinutes);
    const int forecastNetIncome=forecastSpice+(forecastTax-forecastUpkeep)*QuantBotSpendingPolicy::horizonMinutes;
    const int forecastFunding=std::max(0,money+forecastNetIncome);
    int existingMilitaryCapacity=0, constructionCapacity=0, readyYards=0;
    int continuedMilitaryCost=0, continuedConstructionCost=0;
    int activeProductionBurn=0;
    std::map<Uint32,int> laneCapacity;
    std::vector<const BuilderBase*> capitalBuilders;
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isABuilder()) continue;
        const auto* builder=static_cast<const BuilderBase*>(structure);
        capitalBuilders.push_back(builder);
        if (builder->getItemID()==Structure_ConstructionYard && !builder->isUpgrading()
            && !builder->isOnHold() && builder->getProductionQueueSize()==0) ++readyYards;
        int capacity=0, cityCapacity=0, unpaid=0, currentBurn=0;
        const bool port=builder->getItemID()==Structure_StarPort;
        const bool yard=builder->getItemID()==Structure_ConstructionYard;
        const FixPoint speed=std::min(builder->getHealth()/builder->getMaxHealth(),builder->getBuildSpeedLimit());
        for (const auto& offer:builder->getBuildList()) {
            const Uint32 item=offer.itemID;
            if (!port) unpaid+=offer.num*int(offer.price);
            int buildtime=data[item][builder->getOriginalHouseID()].buildtime;
            if (citySimEnabled) buildtime=DuneCity::getCityBuildTime(item,buildtime);
            if (port || buildtime<=0) continue;
            const int cost=(FixPoint(offer.price)*speed*CityEconomyInvestmentPolicy::horizonCycles/(buildtime*15)).lround();
            if (item==builder->getCurrentProducedItem() && !builder->isOnHold()
                && !builder->isUpgrading() && !builder->isWaitingToPlace()
                && !getHouse()->isUnitLimitReached(item)) currentBurn=cost/QuantBotSpendingPolicy::horizonMinutes;
            if (!campaignAvailableToBuild(builder,item)) continue;
            // MCVs are occasional capital purchases, not a permanent production
            // mix. Their real queue cost is already reserved. Assuming every
            // heavy factory continuously builds MCVs grossly inflates burn.
            if (QuantBotBuildPolicy::militaryItem(item)
                || (item==Unit_Harvester && lastCalculatedSpice>0
                    && itemCount[item]<(citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget)))
                capacity=std::max(capacity,cost);
            if (yard && (citySimEnabled
                    ? (item==Structure_ZoneResidential && ownResValve>0)
                        || (item==Structure_ZoneCommercial && ownComValve>0)
                        || (item==Structure_ZoneIndustrial && ownIndValve>0)
                        || (item==Structure_Refinery && unloadingBacklog)
                    : item==Structure_HeavyFactory || (item==Structure_Refinery && lastCalculatedSpice>0)))
                cityCapacity=std::max(cityCapacity,cost);
        }
        unpaid=port ? 0 : std::max(0,unpaid-builder->getProductionProgress().lround());
        activeProductionBurn+=currentBurn;
        laneCapacity[builder->getItemID()]=std::max(laneCapacity[builder->getItemID()],capacity);
        // Include idle and held lines: more factories cannot fix cash starvation.
        existingMilitaryCapacity+=capacity;
        constructionCapacity+=cityCapacity;
        continuedMilitaryCost+=std::max(0,capacity-unpaid);
        continuedConstructionCost+=std::max(0,cityCapacity-unpaid);
        if (AITelemetry::log().enabled()) capitalProducers.set(std::to_string(builder->getObjectID()),
            AITelemetry::Record().set("item",builder->getItemID()).set("queue",builder->getProductionQueueSize())
                .set("upgrading",builder->isUpgrading()).set("hold",builder->isOnHold())
                .set("current_item",builder->getCurrentProducedItem()).set("unpaid",unpaid)
                .set("active_burn_per_minute",currentBurn).set("unit_capacity_cost",capacity)
                .set("construction_capacity_cost",cityCapacity));
    }
    const int cashBuffer=std::max(1000,citySimEnabled ? cityWorkingReserve : economyReserve);
    const auto cashFlow=QuantBotSpendingPolicy::cashFlow(getHouse()->getCredits(),money,forecastNetIncome,
        continuedMilitaryCost+continuedConstructionCost,existingMilitaryCapacity+constructionCapacity,cashBuffer);
    // Cash-funded vanilla openings use the established parallel MCV/factory
    // policy. Use actual runway, not a fixed wealth cutoff: as the base grows,
    // the same cash must cover more production as well as its economy pipeline.
    const bool sharedSpending=!supportMode && (!vanillaEconomy || !cashFlow.fundsParallelProduction);
    const int militaryFunding=std::max(0,forecastFunding-continuedConstructionCost
        + existingMilitaryCapacity-continuedMilitaryCost);
    std::sort(capitalBuilders.begin(),capitalBuilders.end(),[](const auto* a,const auto* b) {
        return a->getObjectID()<b->getObjectID();
    });
    // One construction slot keeps growing the tax base. Recompute it from real
    // queues, so losing a yard or loading a save cannot strand the assignment.
    int cityYards=0, cityGrowthYardsBusy=0;
    if (citySimEnabled) for (const auto* builder:capitalBuilders) {
        if (builder->getItemID()!=Structure_ConstructionYard) continue;
        ++cityYards;
        const auto reserved=reservedStructures.find(builder->getObjectID());
        if (!builder->isOnHold() && !builder->isUpgrading() && builder->getProductionQueueSize()>0
            && (DuneCity::isCityZoneStructure(builder->getCurrentProducedItem())
                || (reserved!=reservedStructures.end() && DuneCity::isCityZoneStructure(reserved->second.item))))
            ++cityGrowthYardsBusy;
    }
    auto affordableCityZone = [&](const BuilderBase* builder, int budget) {
        if (!citySimEnabled || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24)
            return Uint32(NONE_ID);
        const Uint32 zone=chooseCityZone(builder,false);
        return zone!=NONE_ID && purchasePrice(builder,zone)<=budget ? zone : Uint32(NONE_ID);
    };
    auto buildingCapitalCost = [&](Uint32 item) {
        const Coord size=getStructureSize(item);
        const int power=citySimEnabled || powerRules ? std::max(0,data[item][houseID].power) : 0;
        // Without concrete there is no foundation to budget for.
        const int foundation=getGameInitSettings().getGameOptions().concreteRequired
            ? size.x*size.y*data[Structure_Slab1][houseID].price : 0;
        return data[item][houseID].price + foundation
            + (power*generationCostPerThousand+999)/1000;
    };
    // A large starting grant can pay for the first heavy line AND keep it
    // operating. In that case construction throughput, not income, is the
    // opening constraint. Budget its missing prerequisites and four minutes of
    // production before allowing it ahead of the ordinary refinery/port path.
    int firstFactoryCapital=0, firstFactoryProduction=0;
    bool firstFactoryLegal=true;
    std::set<Uint32> firstFactoryPrerequisites;
    std::vector<Uint32> factorySteps{Structure_HeavyFactory};
    while (!factorySteps.empty()) {
        const auto item=factorySteps.back();factorySteps.pop_back();
        if (!firstFactoryPrerequisites.insert(item).second || itemCount[item]>0) continue;
        firstFactoryLegal &= data[item][houseID].enabled && data[item][houseID].techLevel<=currentGame->techLevel;
        firstFactoryCapital+=buildingCapitalCost(item);
        for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
            if (data[item][houseID].prerequisiteStructuresSet[prerequisite]) factorySteps.push_back(prerequisite);
    }
    for (int item=Unit_FirstID;item<=Unit_LastID;++item) {
        const auto& info=data[item][houseID];
        if (!info.enabled || info.builder!=Structure_HeavyFactory || info.techLevel>currentGame->techLevel
            || info.upgradeLevel>0 || info.buildtime<=0 || item==Unit_MCV) continue;
        const int time=citySimEnabled ? DuneCity::getCityBuildTime(item,info.buildtime) : info.buildtime;
        firstFactoryProduction=std::max(firstFactoryProduction,
            int(int64_t(info.price)*CityEconomyInvestmentPolicy::horizonCycles/std::max(1,time*15)));
    }
    const bool fundedFactoryOpening=gameMode==GameMode::Custom && itemCount[Structure_HeavyFactory]==0
        && firstFactoryLegal && firstFactoryProduction>0 && militaryValue<militaryValueLimit
        && money>=firstFactoryCapital+cashBuffer
        && cashFlow.projectedCash>=firstFactoryCapital+firstFactoryProduction+cashBuffer;
    // When cash covers another line AND four minutes of its operation, use
    // the established parallel factory/repair build order. City demand is not
    // a prerequisite for production: factories also supply the city's MCVs.
    const int nextHeavyRunway=buildingCapitalCost(Structure_HeavyFactory)
        + std::max(firstFactoryProduction,laneCapacity[Structure_HeavyFactory])+cashBuffer;
    const bool fundedCityProduction=citySimEnabled && gameMode==GameMode::Custom
        && cashFlow.fundsParallelProduction && money>=nextHeavyRunway
        && cashFlow.projectedCash>=nextHeavyRunway && militaryValue<militaryValueLimit;
    const int desiredWorkers=citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget;
    const int transportBaseline=QuantBotBuildPolicy::carryallTarget(itemCount[Unit_Harvester],combatVehicles,
        getHouse()->getNumItems(Structure_RepairYard));
    const int transportTarget=QuantBotBuildPolicy::supportQueueTarget(transportBaseline,
        getHouse()->getNumItems(Unit_Carryall),itemCount[Unit_Carryall],busyTransports,int(pickupQueue.size()));
    // Coverage the core still lacks, counting existing and planned turrets.
    // This is a spatial demand, not a count goal: it stops by itself once the
    // yards, refineries and factories have the difficulty's overlap, so it can
    // drive defence proactively without turning into turret spam. Coverage
    // starts once the first refinery pays for it, not after the full fleet.
    const int coverageTier=rocketCoverageTier(difficulty);
    // Enemy aircraft that have actually reached our territory. A wing counted
    // from the enemy's item list is a plan the base can prepare for between
    // other construction; aircraft over our own buildings are a present loss.
    bool airEngaged=false;
    for (const auto* aircraft:getUnitList()) {
        if (airEngaged) break;
        if (!aircraft->isAFlyingUnit() || !aircraft->isActive() || aircraft->getHealth()<=0
            || !aircraft->canAttack() || !aircraft->getOwner()
            || aircraft->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !aircraft->isVisible(getHouse()->getTeamID())) continue;
        const int reach=std::max(1,aircraft->getWeaponRange())+8;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && blockDistance(aircraft->getLocation(),
                    structure->getClosestPoint(aircraft->getLocation()))<=reach) { airEngaged=true; break; }
    }
    // Core assets that no turret covers at all, as opposed to those short of
    // the difficulty's overlap. First cover is defence; further overlap is a
    // plan that has to share the yards with the city it protects.
    int uncoveredCoreAssets=0;
    auto coreCoverageShortfall=[&]() {
        if (!citySimEnabled || itemCount[Structure_Refinery]==0) return 0;
        const int radius=std::max(1,data[Structure_RocketTurret][houseID].weaponrange-1);
        std::vector<Coord> turrets;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && structure->getItemID()==Structure_RocketTurret) turrets.push_back(structure->getLocation());
        for (const auto& entry:reservedStructures)
            if (entry.second.item==Structure_RocketTurret) turrets.push_back(entry.second.location);
        int shortfall=0;
        uncoveredCoreAssets=0;
        const Uint32 mainYard=mainConstructionYardID();
        auto account=[&](Uint32 item,Coord origin,Coord size,Uint32 objectID) {
            if (!RocketTurretPolicy::coreAsset(item)) return;
            const int demand=RocketTurretPolicy::desiredCoverage(item,coverageTier,
                isExpansionYard(item,objectID,mainYard));
            int covered=0;
            for (const Coord turret:turrets)
                if (RocketTurretPolicy::coversBuilding(turret,origin,size,radius)) ++covered;
            if (demand>0 && covered==0) ++uncoveredCoreAssets;
            shortfall+=std::max(0,demand-covered);
        };
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0)
                account(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
                    structure->getObjectID());
        for (const auto& entry:reservedStructures)
            account(entry.second.item,entry.second.location,getStructureSize(entry.second.item),NONE_ID);
        return shortfall;
    };
    // Prime the first-cover count for the rules that consult it before any
    // shortfall is evaluated; later calls refresh it against new reservations.
    coreCoverageShortfall();
    // Cover the base actually demands, used both as the interim emplacement
    // ceiling and to bound proactive coverage below.
    int coverageDemand=0;
    const Uint32 anchorYard=mainConstructionYardID();
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0)
            coverageDemand+=RocketTurretPolicy::desiredCoverage(structure->getItemID(),coverageTier,
                isExpansionYard(structure->getItemID(),structure->getObjectID(),anchorYard));
    // Spread core assets can demand an overlap that no legal site reaches, so
    // the shortfall alone never returns to zero. Peaceful pursuit of it then
    // owns every construction slot and its savings for the rest of the game,
    // which is what stopped city growth. Bound the proactive claim by the
    // emplacements the base's own demand justifies; observed aircraft keep the
    // unbounded shortfall, because that is a present loss rather than a plan.
    // Both custom-game economies finish the production core before repeatedly
    // adding cover against aircraft seen elsewhere on the map.
    Uint32 missingCoreInfrastructure=NONE_ID;
    bool coreInfrastructureReady=true;
    if (gameMode==GameMode::Custom && !supportMode)
    for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
        if (!data[item][houseID].enabled || data[item][houseID].techLevel>currentGame->techLevel) continue;
        if (getHouse()->getNumItems(item)==0) coreInfrastructureReady=false;
        if (missingCoreInfrastructure==NONE_ID && itemCount[item]==0) missingCoreInfrastructure=item;
    }
    // Storage relief keeps its established city-only core condition: an
    // overflowing bank is lost income now, whatever tech is still missing.
    const bool cityCoreInfrastructureReady=!citySimEnabled || coreInfrastructureReady;
    // Earned spice and city income share storage. Make room before optional
    // growth can monopolize the yards, and stop once storage reaches the global
    // credit ceiling. Count queued capacity so parallel yards do not spam silos.
    auto storageExpansionNeeded = [&]() {
        const int capacity = getHouse()->getCapacity();
        // A bay already ordered brings its own storage, so count that pending
        // capacity instead of cancelling every silo while refineries expand.
        const int pendingCapacity=std::max(0,itemCount[Structure_Refinery]
            -getHouse()->getNumItems(Structure_Refinery))*data[Structure_Refinery][houseID].capacity;
        const int projectedCapacity=capacity+pendingCapacity;
        return getHouse()->getNumItems(Structure_HeavyFactory)>0 && cityCoreInfrastructureReady
            && capacity > 0 && projectedCapacity < 999999
            && getHouse()->getEarnedCredits() >= projectedCapacity * 0.80_fix
            && itemCount[Structure_Silo] == getHouse()->getNumItems(Structure_Silo);
    };
    // IX unlocks the units and upgrades that make further emplacements worth
    // owning, so a non-city base that can already order it finishes that step
    // before repeating optional cover. City coverage keeps its own established
    // core rule; this adds no new condition there.
    const bool advancedTechPending=!citySimEnabled && gameMode==GameMode::Custom && !supportMode
        && itemCount[Structure_HeavyFactory]>0 && itemCount[Structure_IX]==0
        && anyConstructionYardCanBuildIX && findPlaceLocation(Structure_IX).isValid();
    // Keep unloading capacity ahead of optional additional cover as the worker
    // fleet grows. Existing/pending bays count, and an unplaceable refinery
    // cannot reserve the construction queue. The first two rockets still fit.
    const bool refiningCapacityPending=!citySimEnabled && customStrategicPlanning
        && lastCalculatedSpice>=500 && anyConstructionYardCanBuildRefinery
        && itemCount[Structure_Refinery]<(vanillaEconomy
            ? QuantBotBuildPolicy::desiredSpiceRefineries(spiceHarvesterTarget,itemCount[Unit_Harvester])
            : itemCount[Unit_Harvester]/3)
        && findPlaceLocation(Structure_Refinery).isValid();
    const bool routineRocketsAllowed=(coreInfrastructureReady && !advancedTechPending && !refiningCapacityPending)
        || itemCount[Structure_RocketTurret]<2;
    // Coverage worth buying this pass. The unbounded shortfall belongs to
    // aircraft that have actually reached the base — owning ornithopters
    // somewhere on the map kept a 1v4 chasing an overlap it could never
    // complete — and every claim is interleaved with construction unless a
    // core asset still has no cover at all.
    auto proactiveCoverageShortfall=[&]() {
        if (!routineRocketsAllowed) return 0;
        const int shortfall=coreCoverageShortfall();
        if (!RocketTurretPolicy::proactiveCoverageTurn(airEngaged,uncoveredCoreAssets,
            nonServiceConstructionOrders)) return 0;
        if (airEngaged) return shortfall;
        return itemCount[Structure_RocketTurret]>=RocketTurretPolicy::coverageTurretCap(coverageDemand)
            ? 0 : shortfall;
    };
    // A proactive goal keeps its savings only while the forecast cannot also
    // fund a city plot. Growth pays the bills that reach the saved price, so a
    // hold that outlives the forecast starves the economy it is defending.
    // Urgent crime keeps its unconditional reserve.
    const int cityPlotPrice=data[Structure_ZoneResidential][houseID].price;
    auto proactiveSavingHold=[&](int price) {
        // Establish a small R/C/I district, then interleave further plots with
        // services. Unlimited cheap fallback lots otherwise prevent even the
        // first turret upgrade or supplier from ever reaching its price.
        const int services=itemCount[Structure_RocketTurret]+itemCount[Structure_PoliceStation];
        const bool growthTurn=cityZonesIncludingQueued < 3+2*services;
        return !growthTurn || cashFlow.projectedCash < price+cityPlotPrice;
    };
    // Reserve the turret's draw and one small city plot, rather than two whole
    // spare windtraps. Normal power planning separately budgets maturation.
    // Outside the city the reserve is the emplacement's own draw: the flat 225
    // was more than two spare windtraps of headroom per turret, and a base
    // countering aircraft built thirteen generators it never needed.
    const int rocketPowerBuffer=citySimEnabled ? std::max(0,data[Structure_RocketTurret][houseID].power)
        + std::max({DuneCity::getZonePower(Structure_ZoneResidential,1),
                    DuneCity::getZonePower(Structure_ZoneCommercial,1),
                    DuneCity::getZonePower(Structure_ZoneIndustrial,1)})
        : std::max(0,data[Structure_RocketTurret][houseID].power);
    const bool openingSupplierDue = openingFleetIncomplete()
        && itemCount[Structure_Refinery]>=3 && itemCount[Structure_RocketTurret]>=2
        && itemCount[Structure_StarPort]==0 && itemCount[Structure_HeavyFactory]==0;
    int moderateCrimeProperties=0, dangerousCrimeProperties=0;
    if (citySimEnabled) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0
            || DuneCity::getStructureCityRole(structure->getItemID())==DuneCity::CityRole::None) continue;
        const auto p=structure->getLocation();
        const int crime=currentGame->getCitySimulation()->getCrimeRateMap().worldGet(p.x,p.y);
        moderateCrimeProperties+=crime>=128;
        dangerousCrimeProperties+=crime>=192;
    }
    if (sharedSpending) for (const auto* builder:capitalBuilders) {
        const Uint32 building=builder->getItemID();
        const bool port=building==Structure_StarPort;
        const bool ready=!builder->isUpgrading() && !builder->isOnHold()
            && builder->getProductionQueueSize()==0
            && (!port || static_cast<const StarPort*>(builder)->okToOrder());
        if (building==Structure_ConstructionYard) {
            if (!ready || gameMode!=GameMode::Custom) continue;
            planningBuilder=builder->getObjectID(); clearPlacementCache(false,true);
            // An expansion must unlock and fund its three turrets before its
            // own queue becomes a cheap-zoning lane. Other secured yards can
            // continue growing the economy alongside it.
            if (citySimEnabled && coreInfrastructureReady && expansionTurretsMissing(builder)>0
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel) {
                CapitalCandidate defence;
                defence.builder=builder->getObjectID();defence.item=Structure_RocketTurret;
                defence.kind="expansion_defence";defence.reason="secure_expansion";defence.score=5500;
                if (builder->getCurrentUpgradeLevel()<data[Structure_RocketTurret][houseID].upgradeLevel
                    && builder->getMaxUpgradeLevel()>=data[Structure_RocketTurret][houseID].upgradeLevel) {
                    defence.kind="expansion_upgrade";
                    defence.price=defence.cost=builder->getUpgradeCost();
                    if (defence.price>0) capitalCandidates.push_back(defence);
                } else if (campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                    if (turretPowerRequired && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<rocketPowerBuffer)
                        defence.item=Structure_WindTrap;
                    if (campaignAvailableToBuild(builder,defence.item)) {
                        defence.site=defence.item==Structure_RocketTurret ? findCityTurretPlaceLocation(defence.item)
                            : findPlaceLocation(defence.item);
                        defence.price=data[defence.item][houseID].price;defence.cost=buildingCapitalCost(defence.item);
                        if (defence.site.isValid()) capitalCandidates.push_back(defence);
                    }
                }
            }
            CapitalCandidate economy;
            economy.builder=builder->getObjectID();
            if (citySimEnabled) {
                economy.item=chooseCityEconomy(builder,false);
                economy.cost=lastEconomyInvestment.cost;
                economy.proceeds=lastEconomyInvestment.proceeds();
                economy.delay=lastEconomyInvestment.delayCycles;
            } else if (campaignAvailableToBuild(builder,Structure_Refinery)
                && findPlaceLocation(Structure_Refinery).isValid()) {
                forecastSpiceTrip(findPlaceLocation(Structure_Refinery));
                const int workers=itemCount[Unit_Harvester], refs=itemCount[Structure_Refinery];
                const bool freeWorker=workers<desiredWorkers;
                const int before=std::min(workers*workerAnnualIncome(),refs*bayAnnualIncome);
                const int after=std::min((workers+int(freeWorker))*workerAnnualIncome(),(refs+1)*bayAnnualIncome);
                economy.item=Structure_Refinery;
                economy.cost=buildingCapitalCost(economy.item);
                economy.delay=data[economy.item][houseID].buildtime*15;
                economy.proceeds=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                    economy.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerCityYear);
                if (refineryFieldRisk>0) economy.proceeds/=2;
                const bool useful=CityEconomyInvestmentPolicy::considerRefinery(unloadingBacklog
                    || CityEconomyInvestmentPolicy::processingCapacityNeeded(refs,workers,workerAnnualIncome(),bayAnnualIncome),
                    freeWorker,!harvesterFactories.empty(),workers<2);
                if (!useful) economy.proceeds=0;
                if (unloadingBacklog) economy.proceeds=std::max(economy.proceeds,std::min(waitingCargo,2*int(HARVESTERMAXSPICE)));
            }
            if (economy.item!=NONE_ID) {
                economy.price=data[economy.item][houseID].price;
                economy.score=QuantBotSpendingPolicy::economyScore(economy.proceeds,economy.cost);
                economy.reason=economy.score>0 ? "marginal_income" : "no_marginal_income";
                capitalCandidates.push_back(economy);
            }
            // Services must be allowed to save their price, not wait for money
            // left after every factory has placed another order. Use the existing
            // marginal coverage search; pending services are included in it.
            if (citySimEnabled && getHouse()->hasPower()) {
                CapitalCandidate service;
                const int coreShortfall=proactiveCoverageShortfall();
                service.builder=builder->getObjectID();service.kind="civic";
                if (moderateCrimeProperties>0 && selectCityServiceInvestment(builder,
                    std::max(data[Structure_PoliceStation][houseID].price,data[Structure_RocketTurret][houseID].price),
                    true,service.item,service.site)) {
                    service.score=dangerousCrimeProperties>0 ? 6000 : 2500;
                    service.reason=dangerousCrimeProperties>0 ? "crime_prevention" : "crime_maintenance";
                } else if ((airEngaged || coreShortfall>0
                        || (!openingWorkersNeeded() && itemCount[Structure_Refinery]>0))
                    && (airEngaged || uncoveredCoreAssets>0
                        || nonServiceConstructionOrders>=3 || itemCount[Structure_RocketTurret]==0)
                    && (!openingSupplierDue || airEngaged)
                    && routineRocketsAllowed
                    && campaignAvailableToBuild(builder,Structure_RocketTurret)
                    && (!turretPowerRequired || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=rocketPowerBuffer)) {
                    int uncovered=0;
                    service.site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncovered);
                    if (service.site.isValid() && uncovered>0) {
                        // Aircraft already in play outrank peaceful coverage, and
                        // an uncovered yard/refinery/factory outranks covering an
                        // ordinary building, but both stay below crime prevention.
                        service.item=Structure_RocketTurret;
                        service.score=airEngaged ? 3000 : uncoveredCoreAssets>0 ? 2600 : 2000;
                        service.reason=airEngaged ? "air_coverage"
                            : uncoveredCoreAssets>0 ? "core_coverage" : "uncovered_base";
                    }
                }
                if (service.item!=NONE_ID && service.site.isValid()) {
                    service.price=data[service.item][houseID].price;service.cost=buildingCapitalCost(service.item);
                    capitalCandidates.push_back(service);
                }
            }
            if (itemCount[Structure_RepairYard]>0 && itemCount[Structure_RepairYard]<repairTarget
                && canAddRepairYard(itemCount[Structure_RepairYard]) && !openingWorkersNeeded()
                && campaignAvailableToBuild(builder,Structure_RepairYard)) {
                CapitalCandidate repair;
                repair.builder=builder->getObjectID();repair.item=Structure_RepairYard;repair.kind="repair";
                repair.price=data[repair.item][houseID].price;repair.cost=buildingCapitalCost(repair.item);
                repair.site=findPlaceLocation(repair.item);
                if (repair.site.isValid()) {
                    repair.score=waitingRepairVehicles>=2 ? 3500 : 1800;
                    repair.reason=waitingRepairVehicles>=2 ? "repair_queue" : "repair_fleet_ratio";
                }
                capitalCandidates.push_back(repair);
            }
            // A sold-out Starport cannot meet a growing transport target.
            // Fund the first local supplier before optional factory expansion
            // or repeat zoning; pending High Tech factories already cover it.
            if (itemCount[Unit_Carryall]<transportTarget && itemCount[Structure_HighTechFactory]==0
                && itemCount[Structure_HeavyFactory]>0 && !openingWorkersNeeded()
                && (getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0 || itemCount[Structure_StarPort]==0)
                && ((actualHarvesters>0 && lastCalculatedSpice>0)
                    || (combatVehicles>0 && getHouse()->getNumItems(Structure_RepairYard)>0))
                && !getHouse()->isAirUnitLimitReached()
                && campaignAvailableToBuild(builder,Structure_HighTechFactory)) {
                CapitalCandidate supplier;
                supplier.builder=builder->getObjectID();supplier.item=Structure_HighTechFactory;
                supplier.kind="transport_production";supplier.price=data[supplier.item][houseID].price;
                supplier.cost=buildingCapitalCost(supplier.item);supplier.site=findPlaceLocation(supplier.item);
                if (supplier.site.isValid()) {
                    supplier.score=itemCount[Unit_Carryall]==0 ? 5500 : 4500;
                    supplier.reason="transport_imports_unavailable";
                    capitalCandidates.push_back(supplier);
                }
            }
            for (Uint32 factory:{Structure_LightFactory,Structure_HeavyFactory,Structure_HighTechFactory}) {
                CapitalCandidate capacity;
                capacity.builder=builder->getObjectID();capacity.item=factory;capacity.kind="production";
                capacity.price=data[factory][houseID].price;capacity.cost=buildingCapitalCost(factory);
                const int actual=getHouse()->getNumItems(factory);
                const int busy=factory==Structure_LightFactory ? activeLightFactoryCount
                    : factory==Structure_HeavyFactory ? activeHeavyFactoryCount : activeHighTechFactoryCount;
                capacity.delay=data[factory][houseID].buildtime*15;
                const int added=int(int64_t(laneCapacity[factory])
                    * std::max(0,CityEconomyInvestmentPolicy::horizonCycles-capacity.delay)
                    / CityEconomyInvestmentPolicy::horizonCycles);
                capacity.capacity=QuantBotSpendingPolicy::additionalProduction(militaryFunding,existingMilitaryCapacity,
                    added,std::max(0,militaryValueLimit-militaryValue),capacity.cost);
                if (actual==0) capacity.reason="bootstrap_managed_separately";
                else if (factory==Structure_LightFactory && int64_t(lightVehicleValue)*10000
                    >= int64_t(vehiclePlanValue)*lightVehicleBps) capacity.reason="light_mix_satisfied";
                else if (itemCount[factory]>actual) capacity.reason="factory_pending";
                else if (busy<actual) capacity.reason="existing_line_available";
                else if (!campaignAvailableToBuild(builder,factory)) capacity.reason="tech_unavailable";
                else if (getHouse()->isUnitLimitReached(factory==Structure_HighTechFactory ? Unit_Ornithopter : Unit_Tank)) capacity.reason="unit_limit";
                else if (capacity.capacity<=0) capacity.reason="income_or_army_bottleneck";
                else if (!findPlaceLocation(factory).isValid()) capacity.reason="no_site";
                else {
                    capacity.score=QuantBotSpendingPolicy::productionScore(capacity.capacity,capacity.cost,militaryValue,militaryValueLimit);
                    capacity.reason="funded_production_bottleneck";
                }
                capitalCandidates.push_back(capacity);
            }
            continue;
        }
        // Record unavailable worker lanes too: a zero score explains whether
        // stock, time, remaining spice or the fleet limit stopped investment.
        if (building==Structure_HeavyFactory || port) {
            CapitalCandidate worker;
            worker.builder=builder->getObjectID();worker.item=Unit_Harvester;
            worker.price=purchasePrice(builder,Unit_Harvester);
            worker.cost=worker.price;
            worker.delay=port ? MILLI2CYCLES(30000) : data[Unit_Harvester][houseID].buildtime*15;
            const int before=std::min(itemCount[Unit_Harvester]*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome);
            const int after=std::min((itemCount[Unit_Harvester]+1)*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome);
            worker.proceeds=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                worker.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerCityYear);
            if (refineryFieldRisk>0) worker.proceeds/=2;
            if (!ready) worker.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Harvester)) worker.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Harvester)<=0) worker.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached() || (engineHarvesterLimit>0
                && itemCount[Unit_Harvester]>=engineHarvesterLimit)) worker.reason="unit_limit";
            else if (itemCount[Unit_Harvester]>=desiredWorkers && harvesterInvestmentReserve()==0) worker.reason="fleet_target";
            else if (worker.proceeds<=worker.cost && harvesterInvestmentReserve()==0) worker.reason="spice_or_bay_bottleneck";
            else if (!port && harvesterInvestmentReserve()==0 && !factoryPrefersHarvester(builder)) worker.reason="army_balance";
            else {
                worker.score=QuantBotSpendingPolicy::economyScore(worker.proceeds,worker.cost);
                if ((harvesterInvestmentReserve()>0 || openingFleetIncomplete()) && !defendingEconomy)
                    worker.score=std::max(worker.score,5000);
                worker.reason="marginal_income";
            }
            capitalCandidates.push_back(worker);
        }
        if (port || building==Structure_HighTechFactory) {
            // Establish transport for the working spice fleet before optional
            // buildings or filling every remaining worker slot. This is a
            // capacity requirement. Count queued/paid/in-flight transports.
            // Additional carryalls serve both workers and vehicle repair traffic.
            CapitalCandidate transport;
            transport.builder=builder->getObjectID();transport.item=Unit_Carryall;
            transport.kind="transport";
            transport.price=purchasePrice(builder,Unit_Carryall);transport.cost=transport.price;
            transport.delay=port ? MILLI2CYCLES(30000) : data[Unit_Carryall][houseID].buildtime*15;
            if (itemCount[Unit_Carryall]>=transportTarget) transport.reason="transport_target_met";
            else if ((getHouse()->getNumItems(Unit_Harvester)==0 || getHouse()->getNumItems(Structure_Refinery)==0)
                && (militaryValue==0 || itemCount[Structure_RepairYard]==0))
                transport.reason="no_working_spice_fleet";
            else if (lastCalculatedSpice<=0 && (militaryValue==0 || itemCount[Structure_RepairYard]==0)) transport.reason="spice_depleted";
            else if (!ready) transport.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Carryall) || transport.price<=0) transport.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0) transport.reason="sold_out";
            else if (getHouse()->isAirUnitLimitReached()) transport.reason="unit_limit";
            else {
                transport.score=itemCount[Unit_Carryall]==0 ? 5500
                    : itemCount[Unit_Carryall]*2<transportTarget ? 4500 : 2000;
                transport.reason=itemCount[Unit_Carryall]==0 ? "first_transport_productivity" : "transport_capacity_shortfall";
            }
            capitalCandidates.push_back(transport);
        }
        // Give an imported colonist the same shared cash protection as a
        // factory-built one; otherwise repeated bargains consume its savings.
        if (port && colonisationNeeded && !mcvBuildAvailable && itemCount[Unit_MCV]==0) {
            CapitalCandidate colonist;
            colonist.builder=builder->getObjectID();colonist.item=Unit_MCV;
            colonist.kind="construction";colonist.price=purchasePrice(builder,Unit_MCV);
            colonist.cost=colonist.price;colonist.delay=MILLI2CYCLES(30000);
            if (!ready) colonist.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_MCV) || colonist.price<=0)
                colonist.reason="tech_unavailable";
            else if (getHouse()->getChoam().getNumAvailable(Unit_MCV)<=0) colonist.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached()) colonist.reason="unit_limit";
            else {
                colonist.score=5000;
                colonist.reason="colonisation_without_factory";
            }
            capitalCandidates.push_back(colonist);
        }
        if (ready && building==Structure_HeavyFactory && campaignAvailableToBuild(builder,Unit_MCV)
            && !getHouse()->isGroundUnitLimitReached() && itemCount[Unit_MCV]==0
            && gameMode==GameMode::Custom && !openingWorkersNeeded()) {
            CapitalCandidate mcv;
            mcv.builder=builder->getObjectID();mcv.item=Unit_MCV;mcv.kind="construction";
            mcv.price=data[Unit_MCV][houseID].price;mcv.cost=mcv.price;
            mcv.delay=data[Unit_MCV][houseID].buildtime*15+MILLI2CYCLES(60000);
            const bool cityNeed=citySimEnabled && cityConstructionCapacity<cityYardTarget
                && (rockExpansionNeeded || availableBaseRock>=48)
                && (rockExpansionNeeded || ownResValve>0 || ownComValve>0 || ownIndValve>0);
            // Colonisation is not production capacity, so it does not wait for
            // the yard target. A city that has built out its own rock cannot
            // grow at all until someone settles the next formation, however
            // many yards it already runs.
            const bool colonyNeed=colonisationNeeded;
            const bool vanillaNeed=!citySimEnabled && DuneCity::prioritizeVanillaMcv(money,
                actualHarvesters,itemCount[Structure_ConstructionYard],itemCount[Unit_MCV],mcv.price);
            if (!(cityNeed || colonyNeed || vanillaNeed)) mcv.reason="construction_capacity_available";
            else if (forecastFunding<mcv.cost+std::max(1000,cityWorkingReserve)) mcv.reason="income_bottleneck";
            else {
                mcv.capacity=forecastFunding-mcv.cost-cityWorkingReserve;
                // Establish demanded city throughput before filling the entire
                // transport ratio. The first Carryall still ranks above this.
                mcv.score=(cityNeed || colonyNeed) ? 5000 : 4500;
                mcv.reason=colonyNeed&&!cityNeed ? "base_rock_exhausted"
                    : rockExpansionNeeded ? "available_rock_exhausted" : "funded_construction_bottleneck";
            }
            capitalCandidates.push_back(mcv);
        }
        if (!ready) continue;
        CapitalCandidate soldier;
        soldier.builder=builder->getObjectID();soldier.kind="military";
        int64_t bestDeficit=-1;
        AITelemetry::Record unitOptions;
        for (const auto& offer:builder->getBuildList()) {
            const Uint32 item=offer.itemID;
            if (!QuantBotBuildPolicy::militaryItem(item)) continue;
            const int value=data[item][houseID].price;
            const int price=int(offer.price);
            int bps=0;
            for (size_t i=0;i<mixItems.size();++i) if (mixItems[i]==item) bps=unitMix[i];
            if (item==Unit_SonicTank || item==Unit_Deviator || item==Unit_Devastator) bps=unitMix[3];
            if (item==Unit_Soldier || item==Unit_Infantry || item==Unit_Trooper || item==Unit_Troopers) bps=infantryPercent*100;
            const int64_t deficit=int64_t(vehiclePlanValue)*bps-int64_t(itemCount[item])*value*10000;
            int score=QuantBotSpendingPolicy::militaryScore(price,value,militaryValue,militaryValueLimit,defendingEconomy);
            const char* reason="eligible";
            if (!campaignAvailableToBuild(builder,item)) reason="unavailable";
            else if (getHouse()->isUnitLimitReached(item)) reason="unit_limit";
            else if (port && getHouse()->getChoam().getNumAvailable(item)<=0) reason="sold_out";
            else if (port && price >= value) reason="not_discounted";
            else if (!port && (bps<=0 || deficit<=0)) reason="mix_satisfied";
            else if (item==Unit_Ornithopter && int64_t(itemCount[item]+1)*value*10000
                > int64_t(militaryValue+value)*2000) reason="air_composition_cap";
            else if (score<=0) reason="military_limit";
            if (std::string(reason)!="eligible") score=0;
            if (AITelemetry::log().enabled()) unitOptions.set(std::to_string(item),AITelemetry::Record()
                .set("price",price).set("military_value",value).set("committed",itemCount[item])
                .set("target_bps",bps).set("deficit_scaled",deficit).set("score",score).set("reason",reason));
            if (score>soldier.score || (score>0 && score==soldier.score && deficit>bestDeficit)) {
                soldier.item=item;soldier.price=price;soldier.cost=price;soldier.score=score;
                soldier.proceeds=value;soldier.delay=port ? MILLI2CYCLES(30000) : data[item][houseID].buildtime*15;
                soldier.reason=defendingEconomy ? "active_defence" : "military_shortfall";bestDeficit=deficit;
            }
        }
        if (soldier.item!=NONE_ID) capitalCandidates.push_back(soldier);
        if (AITelemetry::log().enabled()) capitalUnitOptions.set(std::to_string(builder->getObjectID()),unitOptions);
    }
    for (auto& candidate:capitalCandidates) if (isStructure(candidate.item) && candidate.score>0
        && candidate.kind!=std::string("expansion_upgrade")) {
        const auto* builder=dynamic_cast<const BuilderBase*>(getObject(candidate.builder));
        if (!builder) continue;
        planningBuilder=candidate.builder;clearPlacementCache(false,true);
        const Coord site=candidate.site.isValid() ? candidate.site : findPlaceLocation(candidate.item);
        std::vector<Uint32> removed;
        if (site.isValid()) {
            redevelopmentZones(candidate.item,site,removed);
            for (const auto& foundation:foundationPlan(builder,candidate.item,site,removed).orders)
                candidate.foundationCost+=purchasePrice(builder,foundation.item);
        }
    }
    planningBuilder=NONE_ID; clearPlacementCache(false,true);
    int capitalChoice=-1;
    for (size_t i=0;i<capitalCandidates.size();++i)
        if (capitalCandidates[i].score>0 && (capitalChoice<0
            || capitalCandidates[i].score>capitalCandidates[capitalChoice].score)) capitalChoice=int(i);
    // Keep a demanded tax-producing lot funded alongside the army. Otherwise
    // saving for the next launcher repeatedly withholds even the last100 credits
    // from idle yards, despite positive demand and legal space. Reserve one lot,
    // release it on acceptance, and let independent factories use the remainder.
    // A selected safety investment owns its cash and yard until accepted.
    // Cheap zoning must not repeatedly consume the savings for that service.
    // Peaceful core coverage is deliberately absent: it is a plan, not a loss
    // in progress, and its shortfall can persist indefinitely. Letting it own
    // the yard and its savings suppressed the dedicated growth allocation and
    // the zoning fallback on every pass, so the city never developed at all.
    const bool protectionCapital = capitalChoice >= 0
        && (capitalCandidates[capitalChoice].reason == std::string("crime_prevention")
            || capitalCandidates[capitalChoice].reason == std::string("air_coverage")
            || capitalCandidates[capitalChoice].reason == std::string("secure_expansion"));
    bool cityGrowthProtected=false;
    // With multiple yards, assign one to growth alongside routine investment.
    // Crime protection keeps the winning allocation; growth resumes after its
    // order is accepted. A lone opening yard retains its tech path.
    Uint32 dedicatedCityYard=NONE_ID;
    if (sharedSpending && citySimEnabled && cityYards>1 && !protectionCapital) {
        for (const auto* builder:capitalBuilders) {
            if (builder->getItemID()!=Structure_ConstructionYard || builder->isUpgrading()
                || builder->isOnHold()) continue;
            // Prefer the same oldest usable yard on every pass, including
            // while its existing job finishes. Other yards' incidental zoning
            // must not divert this dedicated queue back into service spending.
            if (builder->getProductionQueueSize()>0) {
                dedicatedCityYard=builder->getObjectID();
                break;
            }
            planningBuilder=builder->getObjectID();clearPlacementCache(false,true);
            const Uint32 zone=affordableCityZone(builder,money);
            if (zone==NONE_ID) continue;
            dedicatedCityYard=builder->getObjectID();
            CapitalCandidate growth;
            growth.builder=builder->getObjectID();growth.item=zone;
            growth.price=purchasePrice(builder,zone);growth.cost=buildingCapitalCost(zone);
            growth.kind="city_growth";growth.reason="dedicated_city_growth";
            growth.score=1; // A construction allocation, not a fabricated return forecast.
            capitalChoice=int(capitalCandidates.size());capitalCandidates.push_back(growth);
            cityGrowthProtected=true;
            break;
        }
        planningBuilder=NONE_ID;clearPlacementCache(false,true);
    }
    if (!cityGrowthProtected && citySimEnabled
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=24
        && capitalChoice>=0 && capitalCandidates[capitalChoice].kind==std::string("military")) {
        int growth=-1;
        for (size_t i=0;i<capitalCandidates.size();++i)
            if (DuneCity::isCityZoneStructure(capitalCandidates[i].item) && capitalCandidates[i].score>0
                && (growth<0 || capitalCandidates[i].score>capitalCandidates[growth].score)) growth=int(i);
        if (growth>=0) {capitalChoice=growth;cityGrowthProtected=true;}
    }
    bool capitalConsumed=false;
    int capitalOrderedCost=0;
    AITelemetry::Record capitalOrders;
    const int capitalInitialCount=capitalChoice>=0 ? itemCount[capitalCandidates[capitalChoice].item] : 0;
    auto capitalPending = [&]() {
        return capitalChoice>=0 && !capitalConsumed
            && itemCount[capitalCandidates[capitalChoice].item]==capitalInitialCount;
    };
    auto capitalReserve = [&](Uint32 builder) {
        return capitalPending() ? QuantBotSpendingPolicy::reserveForOther(INT_MAX,
            capitalCandidates[capitalChoice].price+capitalCandidates[capitalChoice].foundationCost,capitalCandidates[capitalChoice].builder==builder,false,
            !getHouse()->hasPower() || itemCount[Structure_ConstructionYard]==0) : 0;
    };
    uint64_t capitalDecision=0;
    if (!supportMode && AITelemetry::log().enabled()) {
        AITelemetry::Record options;
        for (size_t i=0;i<capitalCandidates.size();++i) {
            const auto& c=capitalCandidates[i];
            options.set(std::to_string(i),AITelemetry::Record().set("builder",c.builder).set("item",c.item)
                .set("kind",c.kind).set("price",c.price).set("total_cost_with_power",c.cost)
                .set("proceeds_or_military_value",c.proceeds).set("additional_funded_capacity",c.capacity)
                .set("foundation_cost",c.foundationCost).set("score",c.score).set("delay_cycles",c.delay)
                .set("reason",c.reason).set("affordable",money>=c.price+c.foundationCost));
        }
        capitalDecision=traceDecision("capital_plan",AITelemetry::Record().set("policy_version",2)
            .set("mode",citySimEnabled ? "dunecity" : "vanilla").set("campaign",isCampaignGameType(currentGame->gameType))
            .set("campaign_helper_reserve",harvesterInvestmentReserve()).set("defending",defendingEconomy)
            .set("cash",getHouse()->getCredits()).set("committed_cost",queuedProductionCost).set("spendable",money)
            .set("horizon_cycles",CityEconomyInvestmentPolicy::horizonCycles).set("forecast_tax_per_minute",forecastTax)
            .set("forecast_upkeep_per_minute",forecastUpkeep).set("forecast_spice_receipts",forecastSpice)
            .set("forecast_net_income",forecastNetIncome).set("forecast_funding",forecastFunding)
            .set("active_production_burn_per_minute",activeProductionBurn)
            .set("construction_capacity_cost",constructionCapacity).set("continued_construction_cost",continuedConstructionCost)
            .set("continued_unit_cost",continuedMilitaryCost).set("military_funding_after_construction",militaryFunding)
            .set("projected_cash",cashFlow.projectedCash).set("cash_buffer",cashBuffer)
            .set("net_burn_per_minute",cashFlow.netBurnPerMinute).set("cash_runway_seconds",cashFlow.runwaySeconds)
            .set("funds_parallel_production",cashFlow.fundsParallelProduction).set("shared_priority",sharedSpending)
            .set("starport_market_available",starportMarketAvailable).set("funded_factory_opening",fundedFactoryOpening)
            .set("funded_city_production",fundedCityProduction).set("next_heavy_runway",nextHeavyRunway)
            .set("city_growth_protected",cityGrowthProtected)
            .set("city_growth_yards_busy",cityGrowthYardsBusy)
            .set("city_growth_dedicated_yard",dedicatedCityYard)
            .set("city_growth_builder",cityGrowthProtected ? capitalCandidates[capitalChoice].builder : NONE_ID)
            .set("first_factory_capital",firstFactoryCapital).set("first_factory_production_cost",firstFactoryProduction)
            .set("existing_military_capacity",existingMilitaryCapacity).set("military_value",militaryValue)
            .set("military_target",militaryValueLimit).set("queued_military_value",queuedMilitaryValue)
            .set("workers",itemCount[Unit_Harvester]).set("worker_target",desiredWorkers)
            .set("carryalls",getHouse()->getNumItems(Unit_Carryall)).set("carryalls_committed",itemCount[Unit_Carryall])
            .set("carryall_target",transportTarget).set("funded_army_target",vehiclePlanValue)
            .set("carryall_baseline",transportBaseline).set("carryalls_busy",busyTransports)
            .set("pickup_waiting",int(pickupQueue.size())).set("combat_vehicles",combatVehicles)
            .set("repair_baseline",repairBaseline).set("repair_target",repairTarget)
            .set("repair_bays",getHouse()->getNumItems(Structure_RepairYard)).set("repair_bays_busy",busyRepairBays)
            .set("repair_waiting",waitingRepairVehicles).set("repair_bays_committed",itemCount[Structure_RepairYard])
            .set("refinery_capacity_target",(itemCount[Unit_Harvester]*workerAnnualIncome()+bayAnnualIncome-1)/std::max(1,bayAnnualIncome))
            .set("refineries_committed",itemCount[Structure_Refinery]).set("refinery_waiting",waitingHarvesters)
            .set("refinery_unbooked_bays",unbookedRefineries).set("refinery_field_returners_blocked",fieldReturnersBlocked)
            .set("refinery_returns_waiting",blockedRefineryReturns)
            .set("refinery_queue_persistent",unloadingBacklog)
            .set("moderate_crime_properties",moderateCrimeProperties).set("dangerous_crime_properties",dangerousCrimeProperties)
            .set("spice_share",spiceShare).set("worker_income",workerAnnualIncome()).set("bay_income",bayAnnualIncome)
            .set("trip_cycles",refineryTripCycles).set("walking_trip_cycles",walkingTripCycles).set("field_risk",refineryFieldRisk)
            .set("ready_yards",readyYards).set("power_required",getHouse()->getPowerRequirement())
            .set("power_produced",getHouse()->getProducedPower()).set("producers",capitalProducers)
            .set("unit_options",capitalUnitOptions).set("candidates",options).set("selected",capitalChoice)
            .set("reason",cityGrowthProtected ? "protect_demanded_city_growth" : !sharedSpending ? "funded_parallel_production" : capitalChoice<0
                ? "no_useful_available_purchase" : "highest_marginal_priority"));
    }

    phaseScope.next("ai.build.orders");
    // Give city construction first access to this pass's planning budget.
    // Air gets its allocation before ground production, then light precedes heavy overflow.
    // Stable ordering keeps peers deterministic.
    // Keep IDs, not pointers: an earlier yard may demolish a later zone.
    std::vector<std::pair<int, Uint32>> planningOrder;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse()) {
            int priority=QuantBotBuildPolicy::productionPlanningPriority(citySimEnabled,
                structure->getItemID(), structure->getItemID() == Structure_ConstructionYard
                    && static_cast<const ConstructionYard*>(structure)->isWaitingToPlace(), needsFirstTransport());
            // Compound the helper's income before optional construction/army
            // spending. The port still checks funds, stock and existing orders.
            if (campaignEconomyPush && itemCount[Unit_Harvester]<spiceHarvesterTarget) {
                if (structure->getItemID()==Structure_StarPort) priority=6;
                else if (structure->getItemID()==Structure_HeavyFactory) priority=5;
            }
            if (capitalChoice>=0 && capitalCandidates[capitalChoice].item==Unit_Carryall
                && capitalCandidates[capitalChoice].builder==structure->getObjectID()
                && getHouse()->hasPower()) priority=7;
            planningOrder.emplace_back(priority,structure->getObjectID());
        }
    std::stable_sort(planningOrder.begin(), planningOrder.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    cityReadyYardCount = std::max(1,int(std::count_if(planningOrder.begin(),planningOrder.end(),
        [](const auto& row){return row.first == 3;})));
    if (citySimEnabled) CityPlanningPolicy::rotateYards(planningOrder,getGameCycleCount());
	for (const auto& entry : planningOrder) {
        const auto* pStructure = dynamic_cast<const StructureBase*>(getObject(entry.second));
		if (pStructure && pStructure->getOwner() == getHouse()) {
            // Condition is production, generation, land value and armour, so
            // anything whose output follows its health — every factory, the
            // yard itself, refineries, the whole windtrap family and the city
            // reactor — is repaired as soon as the engine can be paid at all,
            // as is everything the engine's integer formula repairs for free.
            // City mode extends that to the entire colony, walls included,
            // because a damaged footprint lowers its own land value. The rest
            // waits for a comfortable treasury, and survival and engaged
            // emplacements keep their emergency repair on top of all of it.
            if (!pStructure->isRepairing() && pStructure->getHealth() > 0
                && pStructure->getHealth() < pStructure->getMaxHealth()
                && QuantBotBuildPolicy::canAffordRepairTick(getHouse()->getCredits())) {
                const Uint32 repairItem = pStructure->getItemID();
                const bool maintained = repairPolicyMaintains(pStructure);
                const bool survival = pStructure->getHealth() < pStructure->getMaxHealth() * 0.40_fix
                    && money > 1000;
                const bool engaged = QuantBotBuildPolicy::defensiveEmplacement(repairItem)
                    && pStructure->hasATarget();
                const bool wealthy = QuantBotBuildPolicy::repairWhenWealthy(money);
                if (maintained || survival || engaged || wealthy) {
                    doRepair(pStructure);
                    if (AITelemetry::log().enabled())
                        traceDecision("structure_repair", AITelemetry::Record()
                            .set("object", pStructure->getObjectID()).set("item", repairItem)
                            .set("health", pStructure->getHealth().lround())
                            .set("max_health", pStructure->getMaxHealth())
                            .set("credits", getHouse()->getCredits()).set("spendable", money)
                            .set("reason", maintained ? "condition_is_output"
                                : survival ? "survival" : engaged ? "engaged_defence" : "wealth"));
                }
            }

			// Special weapon launch logic (not for support AI)
			if (pStructure->getItemID() == Structure_Palace && !supportMode) {

				const Palace* pPalace = static_cast<const Palace*>(pStructure);
				if (pPalace->isSpecialWeaponReady()) {

					if (houseID != HOUSE_HARKONNEN && houseID != HOUSE_SARDAUKAR) {
						doSpecialWeapon(pPalace);
                        placementCache.clear(); // Palace summons can occupy previously free sites.
					}
					else {
						int enemyHouseID = -1;
						int enemyHouseBuildingCount = 0;

						for (int i = 0; i < NUM_HOUSES; i++) {
							if (getHouse(i) != nullptr) {
								if (getHouse(i)->getTeamID() != getHouse()->getTeamID() && getHouse(i)->getNumStructures() > enemyHouseBuildingCount) {
									enemyHouseBuildingCount = getHouse(i)->getNumStructures();
									enemyHouseID = i;
								}
							}
						}

					if ((enemyHouseID != -1) && (houseID == HOUSE_HARKONNEN || houseID == HOUSE_SARDAUKAR)) {
						Coord target = findBestDeathHandTarget(enemyHouseID);
						if (target.isValid()) {
							doLaunchDeathhand(pPalace, target.x, target.y);
						}
					}
					}
				}
			}

			if (pStructure->isABuilder()) {
				const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                // Retire old-save orders that introduce an unauthored RTS type.
                // Copy the list because cancellation mutates its queue counts.
                if (campaignCityEconomy()) {
                    const auto queued = pBuilder->getBuildList();
                    for (const auto& order : queued)
                        if (!campaignPermitsStructure(order.itemID))
                            for (int n = 0; n < order.num; ++n) doCancelItem(pBuilder, order.itemID);
                }

                planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false,true);

				// Log all builder status for campaign AIs (not just CY)
				if (gameMode == GameMode::Campaign && !supportMode && pStructure->getItemID() != Structure_ConstructionYard) {
					logDebug("PRODUCTION: %s - Upgrading:%d Queue:%d Credits:%d", 
						getItemNameByID(pStructure->getItemID()).c_str(),
						pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(), money);
				}

                if (emitStatsLog && AITelemetry::log().enabled()) {
                    traceDecision("producer_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("item", pBuilder->getItemID()).set("queue", pBuilder->getProductionQueueSize())
                        .set("current_item", pBuilder->getCurrentProducedItem()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("waiting_to_place", pBuilder->isWaitingToPlace())
                        .set("health", pBuilder->getHealth().lround()).set("max_health", pBuilder->getMaxHealth())
                        .set("progress_credits", pBuilder->getProductionProgress().lround())
                        .set("unit_limit_blocked", pBuilder->isUnitLimitReached(pBuilder->getCurrentProducedItem()))
                        .set("military_limit_blocked", militaryValue >= militaryValueLimit)
                        .set("power_deficit", !getHouse()->hasPower())
                        .set("x", pBuilder->getLocation().x).set("y", pBuilder->getLocation().y)
                        .set("credits", getHouse()->getCredits()));
                }
				// Correlate only decisions made for this builder in this planning pass.
                zoneDecisionIds.erase(pBuilder->getObjectID());

                // Record actual queue acceptance for unit and structure production.
				auto produceItemWithLogging = [&](Uint32 itemID, int sourceLine, const char* rule = "unit_mix_or_prerequisite") {
                    if(!cityAdmitsStructure(itemID)) return false;
                    if (itemID==Structure_RepairYard && !canAddRepairYard(itemCount[Structure_RepairYard])) return false;
                    // Also cover campaign rebuild orders, which bypass the strategic planner.
                    if (itemID==Structure_Palace && itemCount[Structure_Palace] >=
                        QuantBotBuildPolicy::difficultyPalaceCap(static_cast<int>(difficulty))) return false;
                    if(itemID == Unit_MCV && yardLimit > 0
                       && itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] >= yardLimit) return false;
                    const int quotedPrice=purchasePrice(pBuilder,itemID);
                    const bool emergencyGenerator=!getHouse()->hasPower()
                        && (itemID==Structure_WindTrap || itemID==Structure_NuclearPlant);
                    if (!emergencyGenerator && money<quotedPrice) {
                        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                            .set("builder",pBuilder->getObjectID()).set("item",itemID).set("rule",rule)
                            .set("price",quotedPrice).set("spendable",money).set("reason","shared_cash_budget"));
                        return false;
                    }

					if (gameMode == GameMode::Campaign && !supportMode && currentGame) {
						std::string itemName = getItemNameByID(itemID);
						logDebug("Queuing %s (ID:%d)", itemName.c_str(), itemID);
					}
                    Coord nuclearSite=Coord::Invalid();
                    if (itemID==Structure_NuclearPlant) {
                        // Campaign rebuild/power orders bypass the general site
                        // reservation path. Recheck and reserve at queue acceptance.
                        placementCache.erase(itemID);
                        nuclearSite=findPlaceLocation(itemID);
                        if (!nuclearSite.isValid()) {
                            traceDecision("construction_rejected",AITelemetry::Record()
                                .set("builder",planningBuilder).set("item",itemID).set("reason","no_nuclear_site"));
                            return false;
                        }
                    }
					const int before = pBuilder->getProductionQueueSize();
                    const auto preOrder = AITelemetry::log().enabled() ? decisionState() : AITelemetry::Record();
					doProduceItem(pBuilder, itemID);
					const bool accepted = pBuilder->getProductionQueueSize() > before;
                    if (accepted) {
                        cityPendingDisplayPopulation += QuantBotCityPolicy::displayPopulation(initialCityPopulation(itemID));
                        if(DuneCity::isCityZoneStructure(itemID)) ++cityZonesIncludingQueued;
                        money-=quotedPrice;
                        capitalOrderedCost+=quotedPrice;
                        if (AITelemetry::log().enabled()) capitalOrders.set(std::to_string(pBuilder->getObjectID())+":"+std::to_string(before),
                            AITelemetry::Record().set("item",itemID).set("price",quotedPrice).set("rule",rule));
                    }
                    if (accepted && capitalPending() && (itemID==capitalCandidates[capitalChoice].item
                        || (cityGrowthProtected && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID()
                            && DuneCity::isCityZoneStructure(itemID))))
                        capitalConsumed=true;
                    if (accepted && nuclearSite.isValid()) {
                        reservedStructures[planningBuilder]={itemID,nuclearSite};
                        auto& sites=builderPlaceLocations[planningBuilder];
                        if (sites.empty()) sites.push_back(nuclearSite);
                        clearPlacementCache();
                        traceDecision("site_reserved",AITelemetry::Record().set("builder",planningBuilder)
                            .set("item",itemID).set("x",nuclearSite.x).set("y",nuclearSite.y));
                    }
                    if (AITelemetry::log().enabled()) traceDecision("production_order", AITelemetry::Record().set("capital_plan",capitalDecision)
                        .set("builder", pBuilder->getObjectID()).set("builder_item", pBuilder->getItemID())
                        .set("item", itemID).set("item_name", getItemNameByID(itemID)).set("accepted", accepted)
                        .set("source_line", sourceLine).set("rule", rule).set("queue_before", before).set("queue_after", pBuilder->getProductionQueueSize())
                        .set("quoted_price", quotedPrice)
                        .set("zone_decision", itemID >= Structure_ZoneResidential && itemID <= Structure_ZoneIndustrial ? zoneDecisionIds[pBuilder->getObjectID()] : 0)
                        .set("state", preOrder));
					if (!accepted && emitStatsLog) {
						logDebug("PRODUCTION: builder=%u rejected item=%u credits=%d", pBuilder->getObjectID(), itemID, money);
					}
					return accepted;
				};

                // Recover construction on every difficulty before withholding
                // optional economy/civic/capital reserves. Count paid cargo and
                // factory queues as MCVs so another port cannot duplicate it.
                // A sold-out catalogue entry can restock; save rather than spend
                // the recovery cash on workers or military in the meantime.
                const bool starportRecovery = itemCount[Structure_ConstructionYard] == 0
                    && itemCount[Unit_MCV] == 0 && itemCount[Structure_StarPort] > 0
                    && data[Unit_MCV][houseID].enabled
                    && getHouse()->getChoam().getNumAvailable(Unit_MCV) >= 0;
                if (starportRecovery) {
                    if (pBuilder->getItemID() == Structure_StarPort) {
                        const auto* port = static_cast<const StarPort*>(pBuilder);
                        if (port->okToOrder() && port->isAvailableToBuild(Unit_MCV)
                            && getHouse()->getChoam().getNumAvailable(Unit_MCV) > 0) {
                            // Existing factory queues pay gradually. Recovery may
                            // use that still-unspent cash, but never actual money
                            // already charged by another order in this pass.
                            const int heldCash = std::max(0, getHouse()->getCredits() - money);
                            money += heldCash;
                            const bool accepted = produceItemWithLogging(Unit_MCV, __LINE__, "starport_construction_recovery");
                            money -= heldCash;
                            if (accepted) {
                                ++itemCount[Unit_MCV];
                                doPlaceOrder(port);
                            }
                        }
                    }
                    continue;
                }

                auto upgradeWithLogging = [&](int sourceLine) {
                    const int price=pBuilder->getUpgradeCost();
                    const bool affordable=money>=price;
                    const bool accepted=affordable && doUpgrade(pBuilder);
                    if (accepted) { money-=price; capitalOrderedCost+=price; }
                    traceDecision("capital_upgrade",AITelemetry::Record().set("capital_plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("price",price).set("accepted",accepted)
                        .set("reason",!affordable ? "shared_cash_budget" : accepted ? "technology_unlock" : "unavailable")
                        .set("source_line",sourceLine));
                    return accepted;
                };

                // Campaign reconstruction and power orders do not pass through
                // the standard construction selection below. Give them the same
                // founded queue and per-yard site reservation.
                auto queueFoundedStructure = [&](Uint32 item, int sourceLine,
                                                  const char* rule="campaign_rebuild") {
                    if (pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !campaignAvailableToBuild(pBuilder,item) || !cityAdmitsStructure(item)) return false;
                    planningBuilder=pBuilder->getObjectID();
                    clearPlacementCache(false,true);
                    const Coord site=item==Structure_RocketTurret || item==Structure_GunTurret
                        ? findEffectiveTurretPlaceLocation(item) : findPlaceLocation(item);
                    if (!site.isValid()) return false;
                    // Legacy campaign reconstruction and power orders share the
                    // one foundation rule: no site it cannot prepare in full.
                    const auto plan=foundationPlan(pBuilder,item,site);
                    if(!plan.complete) {
                        traceDecision("construction_rejected",AITelemetry::Record()
                            .set("builder",planningBuilder).set("item",item).set("rule",rule)
                            .set("reason","foundation_incomplete"));
                        return false;
                    }
                    int total=purchasePrice(pBuilder,item);
                    for(const auto& foundation:plan.orders)total+=purchasePrice(pBuilder,foundation.item);
                    if(money<total) return false;
                    auto& places=builderPlaceLocations[planningBuilder];
                    places.clear();
                    // Concrete this building will not stand on is wasted cash.
                    // If any part of the sequence is refused, give it all back.
                    auto abandonFoundation=[&](size_t laid) {
                        for(size_t i=0;i<laid;++i) doCancelItem(pBuilder,plan.orders[i].item);
                        places.clear();
                        return false;
                    };
                    for(size_t i=0;i<plan.orders.size();++i) {
                        if(!produceItemWithLogging(plan.orders[i].item,sourceLine,"building_foundation"))
                            return abandonFoundation(i);
                        places.push_back(Coord(plan.orders[i].x,plan.orders[i].y));
                    }
                    const auto priorSites=places.size();
                    if(!produceItemWithLogging(item,sourceLine,rule))
                        return abandonFoundation(plan.orders.size());
                    if(places.size()==priorSites)places.push_back(site); // Nuclear may reserve in the acceptance wrapper.
                    reservedStructures[planningBuilder]={item,site};
                    clearPlacementCache();
                    return true;
                };

                // Bulk concrete comes before anything optional this yard could
                // buy, in every mode and at every difficulty, as soon as the
                // technology allows it. Every ordinary building now waits for a
                // full foundation, and the 2x2 slab is one order where Slab1
                // needs four — so income, reserve, power and defence
                // preconditions were all gating the unlock behind the buildings
                // it makes cheaper. Only the cost of the upgrade itself remains,
                // and a damaged yard repairs first so it can accept it. A yard
                // that cannot reach the level, or a tree without Slab4, simply
                // keeps founding with single slabs.
                const bool bulkSlabReachable =
                    data[Structure_Slab4][houseID].enabled
                    && data[Structure_Slab4][houseID].techLevel <= currentGame->techLevel
                    && data[Structure_Slab4][houseID].builder == int(Structure_ConstructionYard)
                    && campaignPermitsStructure(Structure_Slab4);
                auto upgradeForConcrete = [&]() {
                    if (pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !QuantBotBuildPolicy::bulkSlabUpgradePending(
                            getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                            pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                            pBuilder->getMaxUpgradeLevel())) return false;
                    // The upgrade is owed and reachable, so this yard spends the
                    // pass on it and on nothing else — waiting for the credits or
                    // for another yard's turn is still the upgrade's pass, not a
                    // licence to buy something cheaper first.
                    const bool anotherUpgrading = std::any_of(capitalBuilders.begin(), capitalBuilders.end(),
                        [&](const auto* other) {
                            return other != pBuilder && other->getItemID() == Structure_ConstructionYard
                                && other->isUpgrading();
                        });
                    const int upgradeCost = pBuilder->getUpgradeCost();
                    auto hold = [&](const char* reason) {
                        traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
                            .set("builder", pBuilder->getObjectID()).set("accepted", false)
                            .set("cost", upgradeCost).set("spendable", money)
                            .set("level", pBuilder->getCurrentUpgradeLevel())
                            .set("reason", reason));
                    };
                    if (!QuantBotBuildPolicy::upgradeYardForBulkSlab(
                            getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                            pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                            pBuilder->getMaxUpgradeLevel(), money, upgradeCost, anotherUpgrading)) {
                        if (anotherUpgrading) { hold("another_yard_unlocking_bulk_slab"); return true; }
                        hold("saving_for_bulk_slab");
                        return true;
                    }
					if (pBuilder->getHealth() < pBuilder->getMaxHealth()) {
						if (!pBuilder->isRepairing()) doRepair(pBuilder);
						hold("repair_before_bulk_slab");
						return true; // The affordable upgrade owns this pass.
					}
					const bool accepted = upgradeWithLogging(__LINE__);
					traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
						.set("builder", pBuilder->getObjectID()).set("accepted", accepted)
						.set("cost", upgradeCost).set("spendable", money)
						.set("level", pBuilder->getCurrentUpgradeLevel())
						.set("reason", "unlock_bulk_slab"));
                    return accepted;
                };
                if (upgradeForConcrete()) continue;

                auto queueCampaignWindtrap = [&](int nextDemand) {
                    if (!campaignPowerNeeded(nextDemand) || pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !campaignAvailableToBuild(pBuilder,Structure_WindTrap)) return false;
                    const Coord site=findPlaceLocation(Structure_WindTrap);
                    if (!site.isValid() || money < data[Structure_WindTrap][houseID].price) return false;
                    if (!queueFoundedStructure(Structure_WindTrap,__LINE__,"campaign_required_power")) return false;
                    ++itemCount[Structure_WindTrap];
                    traceDecision("campaign_power",AITelemetry::Record().set("produced",getHouse()->getProducedPower())
                        .set("required",getHouse()->getPowerRequirement()).set("next_demand",nextDemand));
                    return true;
                };
                if (queueCampaignWindtrap(0)) continue;

                // Generation outside the city simulation is an investment, not
                // a function. Vanilla's House::hasPower() answers true whatever
                // the meters say, so this never asks it: it compares the raw
                // requirement, plus the demand of everything already on order,
                // with raw production plus the output that repairs in progress
                // and queued generators will deliver, keeps a reserve on top of
                // that, and puts the remaining package through the documented
                // economics in QuantBotPowerInvestmentPolicy. A base running at
                // 1800 against 1840 is one order away from another shortage, so
                // being exactly powered is not the target. City power rules, the
                // first prerequisite windtrap and the turret-power option are all
                // untouched and still run ahead of this. Custom and legacy
                // campaign games at every difficulty share the one gate.
                auto economicGenerator = [&]() -> Uint32 {
                    if (!vanillaEconomy || supportMode || defendingEconomy
                        || pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0) return NONE_ID;
                    // Income and whatever production core this tree actually
                    // offers come first; absent technology is not a blocker.
                    auto coreEstablished=[&](Uint32 item) {
                        return !data[item][houseID].enabled
                            || data[item][houseID].techLevel>currentGame->techLevel
                            || getHouse()->getNumItems(item)>0;
                    };
                    if (getHouse()->getNumItems(Structure_Refinery)==0 || getHouse()->getNumItems(Unit_Harvester)==0
                        || !coreEstablished(Structure_LightFactory)
                        || !coreEstablished(Structure_HeavyFactory)) return NONE_ID;
                    // Capacity already paid for: the health a damaged generator
                    // regains under the repair policy, and every queued
                    // generator anywhere in the base. Demand already ordered
                    // counts on the other side of the ledger.
                    QuantBotPowerInvestmentPolicy::PowerTarget target;
                    target.demand=getHouse()->getPowerRequirement();
                    target.produced=getHouse()->getProducedPower();
                    for (const auto* structure:getStructureList()) {
                        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0) continue;
                        const int nominal=-data[structure->getItemID()][structure->getOriginalHouseID()].power;
                        if (nominal<=0 || structure->getMaxHealth()<=0) continue;
                        const int current=(structure->getHealth()*nominal/structure->getMaxHealth()).floor();
                        target.restorable+=std::max(0,nominal-std::min(nominal,current));
                    }
                    for (int item=Structure_FirstID;item<=Structure_LastID;++item) {
                        const int ordered=std::max(0,itemCount[item]-getHouse()->getNumItems(item));
                        if (ordered<=0) continue;
                        const int nominal=-data[item][houseID].power;
                        if (nominal>0) target.pendingOutput+=ordered*nominal;
                        else target.queuedDemand+=ordered*std::max(0,data[item][houseID].power);
                    }
                    // Every generator this yard could actually place, so the
                    // reserve is sized on a real building rather than a guess.
                    // The plain windtrap is the standard unit where the tree has
                    // one; otherwise the smallest available generator is, which
                    // keeps the figure the same for every peer.
                    struct Candidate { Uint32 item; int output; int unitCapital; };
                    std::vector<Candidate> available;
                    for (Uint32 generator:{Uint32(Structure_WindTrap),Uint32(Structure_AdvancedWindTrap),
                            Uint32(Structure_AdvancedWindTrapMK2),Uint32(Structure_AdvancedWindTrapMK3),
                            Uint32(Structure_NuclearPlant)}) {
                        const int output=-data[generator][houseID].power;
                        if (output<=0 || !campaignAvailableToBuild(pBuilder,generator)
                            || !findPlaceLocation(generator).isValid()) continue;
                        available.push_back({generator,output,buildingCapitalCost(generator)});
                    }
                    if (available.empty()) return NONE_ID;
                    for (const auto& candidate:available) {
                        if (candidate.item==Structure_WindTrap) { target.standardOutput=candidate.output; break; }
                        target.standardOutput=target.standardOutput==0
                            ? candidate.output : std::min(target.standardOutput,candidate.output);
                    }
                    // One package at a time, but a pending generator that cannot
                    // reach the target no longer blocks the rest of it: its
                    // output is already counted, so the next pass simply orders
                    // what is still missing.
                    const int deficit=QuantBotPowerInvestmentPolicy::bufferedShortfall(target);
                    const int shortage=QuantBotPowerInvestmentPolicy::operatingShortfall(target);
                    if (deficit<=0) return NONE_ID;
                    // Exactly the repairs a closed deficit stops paying for,
                    // at the engine's own integer per-hitpoint charge.
                    int repairMilli=0;
                    for (const auto* structure:getStructureList())
                        if (structure->getOwner()==getHouse() && structure->getHealth()>0
                            && structure->getItemID()!=Structure_Wall
                            && (repairPolicyMaintains(structure)
                                || QuantBotBuildPolicy::repairWhenWealthy(money)))
                            repairMilli+=QuantBotBuildPolicy::repairCreditsPerHitpointMilli(
                                structure->getMaxHealth(),repairPrice(structure));
                    // Cheapest complete package for a given size, ties broken by
                    // the lowest item id so every peer picks the same building.
                    auto cheapestFor=[&](int size) {
                        QuantBotPowerInvestmentPolicy::GeneratorChoice best;
                        for (const auto& generator:available) {
                            QuantBotPowerInvestmentPolicy::GeneratorChoice candidate;
                            candidate.item=generator.item;
                            candidate.output=generator.output;
                            candidate.unitCapital=generator.unitCapital;
                            candidate.packageCapital=QuantBotPowerInvestmentPolicy::generatorsNeeded(
                                size,generator.output)*candidate.unitCapital;
                            if (QuantBotPowerInvestmentPolicy::preferGenerator(candidate,best)) best=candidate;
                        }
                        return best;
                    };
                    // Forecast recurring income from the current fleet and field;
                    // starting cash is not income, and core queues keep their reserve.
                    auto priced=[&](const QuantBotPowerInvestmentPolicy::GeneratorChoice& choice,int size) {
                        QuantBotPowerInvestmentPolicy::Investment investment;
                        investment.deficit=size;
                        investment.generatorOutput=choice.output;
                        investment.unitCapital=choice.unitCapital;
                        investment.spendable=QuantBotBuildPolicy::spendableCredits(money,economyReserve);
                        investment.netIncome=forecastNetIncome;
                        investment.incomeMinutes=QuantBotSpendingPolicy::horizonMinutes;
                        investment.repairMilliPerHitpoint=repairMilli;
                        return investment;
                    };
                    const auto best=cheapestFor(deficit);
                    if (best.item==NONE_ID) return NONE_ID;
                    const auto investment=priced(best,deficit);
                    // The buffered package is the offer; the bare shortage, with
                    // its own cheapest generator, is the fallback the same gates
                    // judge if the buffered one is refused.
                    const auto relief=cheapestFor(shortage);
                    const auto decision=QuantBotPowerInvestmentPolicy::decide(
                        investment,priced(relief,shortage));
                    const auto verdict=decision.verdict;
                    const bool buy=QuantBotPowerInvestmentPolicy::buys(verdict);
                    const auto& chosen=decision.trimmedToShortage ? relief : best;
                    traceDecision("power_investment",AITelemetry::Record()
                        .set("builder",pBuilder->getObjectID()).set("item",chosen.item)
                        .set("raw_required",getHouse()->getPowerRequirement())
                        .set("raw_produced",getHouse()->getProducedPower())
                        .set("queued_demand",target.queuedDemand)
                        .set("anticipated_demand",QuantBotPowerInvestmentPolicy::anticipatedDemand(target))
                        .set("repair_restored_output",target.restorable)
                        .set("pending_output",target.pendingOutput)
                        .set("effective_capacity",QuantBotPowerInvestmentPolicy::effectiveCapacity(target))
                        .set("standard_generator_output",target.standardOutput)
                        .set("buffer_reserve",QuantBotPowerInvestmentPolicy::bufferReserve(target))
                        .set("buffered_target",QuantBotPowerInvestmentPolicy::bufferedTarget(target))
                        .set("operating_shortage",shortage)
                        .set("buffered_shortfall",deficit)
                        .set("deficit",decision.deficit).set("generator_output",chosen.output)
                        .set("unit_capital",chosen.unitCapital)
                        .set("generators_needed",QuantBotPowerInvestmentPolicy::generatorsNeeded(decision.deficit,chosen.output))
                        .set("package_capital",decision.capital)
                        .set("buffered_package_capital",QuantBotPowerInvestmentPolicy::packageCapital(investment))
                        .set("trimmed_to_shortage",decision.trimmedToShortage)
                        .set("spendable_after_reserve",investment.spendable)
                        .set("economy_reserve",economyReserve)
                        .set("forecast_net_income",forecastNetIncome)
                        .set("forecast_minutes",QuantBotSpendingPolicy::horizonMinutes)
                        .set("maintained_repair_milli_per_hitpoint",repairMilli)
                        .set("avoided_repairs_per_minute",QuantBotPowerInvestmentPolicy::avoidedRepairPerMinute(investment))
                        .set("accepted",buy)
                        .set("reason",QuantBotPowerInvestmentPolicy::describe(verdict)));
                    return buy ? chosen.item : Uint32(NONE_ID);
                };

                // Hard/Brutal establish repair capacity; Medium only replaces an authored
                // yard before optional expansion, including its missing prerequisites.
                // Count queued structures and preserve normal costs/placement rules.
                if (isCampaignGameType(currentGame->gameType)
                    && !(cityGrowthProtected && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID())
                    && !(capitalPending() && capitalCandidates[capitalChoice].item==Unit_Carryall)
                    && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal
                        || (difficulty==Difficulty::Medium && initialItemCount[Structure_RepairYard]>0))
                    && data[Structure_RepairYard][houseID].enabled
                    && currentGame->techLevel>=data[Structure_RepairYard][houseID].techLevel
                    && pBuilder->getItemID()==Structure_ConstructionYard
                    && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize()==0
                    && itemCount[Structure_RepairYard]==0
                    && getHouse()->getNumItems(Structure_Refinery)>0
                    && getHouse()->getNumItems(Unit_Harvester)>0
                    && ((gameMode == GameMode::Campaign && !citySimEnabled && difficulty != Difficulty::Brutal)
                        || !starportMarketAvailable || !data[Structure_StarPort][houseID].enabled
                        || currentGame->techLevel < data[Structure_StarPort][houseID].techLevel
                        || getHouse()->getNumItems(Structure_StarPort)>0)
                    && (getHouse()->hasHeavyFactory() || getHouse()->getNumItems(Structure_StarPort)>0)) {
                    Uint32 repairStep=Structure_RepairYard;
                    if (!campaignAvailableToBuild(pBuilder,repairStep)) {
                        repairStep=NONE_ID;
                        for (int i=Structure_FirstID;i<=Structure_LastID;++i)
                            if (data[Structure_RepairYard][houseID].prerequisiteStructuresSet[i]
                                && itemCount[i]==0 && campaignAvailableToBuild(pBuilder,i)) {repairStep=i;break;}
                    }
                    if (repairStep!=NONE_ID && money>=data[repairStep][houseID].price+300) {
                        const Coord site=findPlaceLocation(repairStep);
                        if (site.isValid() && queueFoundedStructure(repairStep,__LINE__,"campaign_repair_capacity")) {
                            ++itemCount[repairStep];
                            continue;
                        }
                    }
                }

				// Restore the reserved portion after this builder's decisions, keeping
				// charges made by the acceptance wrapper for subsequent builders.
				struct RestoreReservedCredits {
					int& money;
					int reserved;
					~RestoreReservedCredits() { money += reserved; }
                } reserve{money, 0};
                // The factory that has to supply the next yard: either because
                // the rock ran out under the old exhaustion rule, or because
                // the city is built out and has somewhere to settle. Both save
                // for that MCV instead of spending the same cash on units.
                const bool expansionProducer=(rockExpansionNeeded||colonisationNeeded)&&itemCount[Unit_MCV]==0
                    && pBuilder->getItemID()==Structure_HeavyFactory;
                const bool openingWorker = openingWorkersNeeded() && getHouse()->hasPower();
                const bool workerProducer = pBuilder->getItemID() == Structure_HeavyFactory
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)
                    && (openingWorker || (brutalCityEconomy && factoryPrefersHarvester(pBuilder)));
                const bool firstCarryall = needsFirstTransport() && carryallBuildAvailable && getHouse()->hasPower();
                const bool transportProducer = firstCarryall && pBuilder->getItemID() == Structure_HighTechFactory
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall);
                int protectedCash = pBuilder->getItemID() == Structure_ConstructionYard || transportProducer || workerProducer || expansionProducer
                    ? 0 : std::max({strategicReserveCost,economyReserve,civicReserveCost});
                // A cramped start still needs power, income and a factory before
                // it can expand. Don't protect cash for an MCV we cannot build.
                if ((rockExpansionNeeded||colonisationNeeded) && mcvBuildAvailable && itemCount[Unit_MCV]==0 && !expansionProducer)
                    protectedCash=std::max(protectedCash,int(data[Unit_MCV][houseID].price));
                if (!openingWorker && !expansionProducer && pBuilder->getItemID() != Structure_ConstructionYard)
                    protectedCash = std::max(protectedCash,civicReserveCost);
                if (openingWorker && !workerProducer)
                    protectedCash = std::max(protectedCash,data[Unit_Harvester][houseID].price);
                if (firstCarryall && !transportProducer)
                    protectedCash = std::max(protectedCash,data[Unit_Carryall][houseID].price);
                if (nuclearPlan && getHouse()->hasPower() && !powerGenerationPending()
                    && pBuilder->getItemID() != Structure_ConstructionYard && !transportProducer && !workerProducer && !expansionProducer)
                    protectedCash = std::max(protectedCash,data[Structure_NuclearPlant][houseID].price);
                if (pBuilder->getItemID() != Structure_ConstructionYard && !expansionProducer && !transportProducer)
                    protectedCash = std::max(protectedCash,harvesterInvestmentReserve());
                const bool capitalSupplier=capitalPending() && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID();
                if (capitalSupplier && (cityGrowthProtected || protectionCapital || defendingEconomy || harvesterInvestmentReserve()==0
                    || capitalCandidates[capitalChoice].item==Unit_Harvester
                    || capitalCandidates[capitalChoice].item==Unit_Carryall
                    || capitalCandidates[capitalChoice].item==Unit_MCV)) protectedCash=0;
                protectedCash=std::max(protectedCash,capitalReserve(pBuilder->getObjectID()));
                // Saving for optional capital must not stop the yard from
                // relieving the storage ceiling that prevents further saving.
                if (pBuilder->getItemID()==Structure_ConstructionYard && storageExpansionNeeded()
                    && campaignAvailableToBuild(pBuilder,Structure_Silo)) protectedCash=0;
                reserve.reserved = money - QuantBotBuildPolicy::spendableCredits(money,protectedCash);
				money -= reserve.reserved;

                if (capitalSupplier && capitalPending() && capitalCandidates[capitalChoice].item==Unit_Carryall
                    && money<capitalCandidates[capitalChoice].price) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("item",Unit_Carryall)
                        .set("price",capitalCandidates[capitalChoice].price).set("spendable",money)
                        .set("reason",itemCount[Unit_Carryall]==0 ? "saving_first_transport" : "saving_transport_capacity"));
                    continue;
                }
                const CapitalCandidate* localUnitChoice=nullptr;
                // A rich opening can fund its MCV now. Do not repeatedly buy
                // workers before the factory gets to unlock that construction.
                const bool unlockingVanillaMcv=vanillaEconomy && gameMode==GameMode::Custom
                    && pBuilder->getItemID()==Structure_HeavyFactory
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)
                    && pBuilder->getCurrentUpgradeLevel()<pBuilder->getMaxUpgradeLevel()
                    && DuneCity::prioritizeVanillaMcv(money,getHouse()->getNumItems(Unit_Harvester),
                        itemCount[Structure_ConstructionYard],itemCount[Unit_MCV],data[Unit_MCV][houseID].price);
                const bool parallelHeavyProduction=fundedCityProduction
                    && pBuilder->getItemID()==Structure_HeavyFactory;
                if (!transportProducer && !expansionProducer && !unlockingVanillaMcv && !parallelHeavyProduction)
                    for (const auto& candidate:capitalCandidates)
                        if (candidate.builder==pBuilder->getObjectID() && isUnit(candidate.item)
                            && (candidate.item!=Unit_Carryall || itemCount[Unit_Carryall]<transportTarget)
                            && !QuantBotBuildPolicy::militaryItem(candidate.item)
                            && (!capitalSupplier || candidate.item==capitalCandidates[capitalChoice].item)
                            && candidate.score>0 && candidate.price<=money
                            && (!localUnitChoice || candidate.score>localUnitChoice->score)) localUnitChoice=&candidate;
                if (localUnitChoice && pBuilder->getProductionQueueSize()==0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()) {
                    const auto choice=*localUnitChoice;
                    if (produceItemWithLogging(choice.item,__LINE__,capitalSupplier ? "shared_capital_priority" : "parallel_capital_purchase")) {
                        ++itemCount[choice.item];
                        if (QuantBotBuildPolicy::militaryItem(choice.item)) militaryValue+=data[choice.item][houseID].price;
                        if (choice.item==Unit_Harvester) orderedSpiceHarvester=true;
                        if (pBuilder->getItemID()!=Structure_StarPort) continue;
                        // Imports can buy further workers and bargains in the same shipment.
                    }
                } else if (capitalSupplier && isUnit(capitalCandidates[capitalChoice].item)
                    && !QuantBotBuildPolicy::militaryItem(capitalCandidates[capitalChoice].item)
                    && pBuilder->getItemID()!=Structure_StarPort && !transportProducer && !expansionProducer
                    && !unlockingVanillaMcv && !parallelHeavyProduction) continue;

				if (pBuilder->getItemID() != Structure_StarPort && !transportProducer && !workerProducer && !expansionProducer && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize() < 1
					&& money > 1500) {
					const int customItem = chooseLowPriorityCustomUnit(pBuilder);
					if (customItem != ItemID_Invalid) {
						if (produceItemWithLogging(customItem, __LINE__)) itemCount[customItem]++;

						militaryValue += data[customItem][houseID].price;
						continue;
					}
				}

				switch (pStructure->getItemID()) {

			case Structure_LightFactory: {
				if (!pBuilder->isUpgrading()
					&& money > (citySimEnabled ? 500 : 700)
					&& pBuilder->getProductionQueueSize() < 1
					&& pBuilder->getBuildListSize() > 0
					&& militaryValue < militaryValueLimit
					&& ((citySimEnabled && itemCount[Structure_HeavyFactory] == 0)
						|| ((!learningUnitMix && lightVehicleCount < 2)
                            || int64_t(lightVehicleValue) * 10000 < int64_t(vehiclePlanValue) * lightVehicleBps))) {

					if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel() && getHouse()->getCredits() > 1500) {
						upgradeWithLogging(__LINE__);
					}
					else if (!getHouse()->isGroundUnitLimitReached()) {
                        Uint32 itemID = NONE_ID;
                        int64_t bestDeficit = 0;
                        for (size_t i=5; i<8; ++i) {
                            const Uint32 candidate = mixItems[i];
                            if (!campaignAvailableToBuild(pBuilder,candidate)) continue;
                            const auto deficit = UnitMixPolicy::deficit(unitMix[i], vehiclePlanValue,
                                itemCount[candidate], data[candidate][houseID].price);
                            if (deficit > bestDeficit) { bestDeficit = deficit; itemID = candidate; }
                        }
                        // Bootstrap a small sample before combat learning begins.
                        if (itemID == NONE_ID && !learningUnitMix && lightVehicleCount < 2)
                            for (size_t i=5; i<8; ++i)
                                if (campaignAvailableToBuild(pBuilder,mixItems[i])
                                    && (itemID == NONE_ID || itemCount[mixItems[i]] < itemCount[itemID])) itemID = mixItems[i];
                        if (itemID != NONE_ID && militaryValue + data[itemID][houseID].price <= militaryValueLimit
                            && produceItemWithLogging(itemID, __LINE__, "adaptive_light_mix")) {
                            ++itemCount[itemID];
                            ++lightVehicleCount;
                            lightVehicleValue += data[itemID][houseID].price;
                            militaryValue += data[itemID][houseID].price;

                        }
					}
				}
			} break;

		case Structure_WOR: {
			if (!citySimEnabled
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 450
				&& militaryValue < militaryValueLimit
				&& !getHouse()->isGroundUnitLimitReached()
				&& (infantryCount < 3
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				for (Uint32 candidate : {Unit_Trooper, Unit_Troopers}) {
					if (campaignAvailableToBuild(pBuilder,candidate)
						&& (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID])) {
						itemID = candidate;
					}
				}
				if (itemID != NONE_ID && militaryValue+data[itemID][houseID].price<=militaryValueLimit
                    && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;

				}
			}
		} break;

		case Structure_Barracks: {
			if (!citySimEnabled
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 300
				&& militaryValue < militaryValueLimit
				&& !getHouse()->isGroundUnitLimitReached()
				&& (infantryCount < 3
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				for (Uint32 candidate : {Unit_Soldier, Unit_Infantry}) {
					if (campaignAvailableToBuild(pBuilder,candidate)
						&& (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID])) {
						itemID = candidate;
					}
				}
				if (itemID != NONE_ID && militaryValue+data[itemID][houseID].price<=militaryValueLimit
                    && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;

				}
			}
		} break;

                case Structure_HighTechFactory: {
                    const int ornithopterPrice = data[Unit_Ornithopter][houseID].price;
                    const int carryallPrice = data[Unit_Carryall][houseID].price;
                    const int ornithopterValue = ornithopterPrice * itemCount[Unit_Ornithopter];
                    QuantBotBuildPolicy::AirProductionState air;
                    air.busy = pBuilder->getProductionQueueSize() > 0;
                    air.upgrading = pBuilder->isUpgrading();
                    air.airLimit = getHouse()->isAirUnitLimitReached();
                    air.ornithopterAvailable = campaignAvailableToBuild(pBuilder,Unit_Ornithopter);
                    air.carryallAvailable = campaignAvailableToBuild(pBuilder,Unit_Carryall);
                    air.canUpgrade = pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel();
                    air.spendable = money; // Economy/strategic reserves were removed above.
                    air.ornithopterPrice = ornithopterPrice;
                    air.carryallPrice = carryallPrice;
                    air.carryalls = itemCount[Unit_Carryall];
                    air.carryallTarget = transportTarget;
                    air.armyValue = militaryValue;
                    air.armyLimit = militaryValueLimit;
                    air.vehiclePlanValue = vehiclePlanValue;
                    air.airCommittedValue = ornithopterValue;
                    air.airTargetBps = unitMix[4];
                    const auto decision = QuantBotBuildPolicy::chooseAirProduction(air);
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        traceDecision("air_production_decision", AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("reason",decision.reason)
                            .set("credits",getHouse()->getCredits()).set("planning_spendable",money)
                            .set("economy_reserve",economyReserve).set("strategic_reserve",strategicReserveCost)
                            .set("cash_threshold",ornithopterPrice).set("ornithopter_price",ornithopterPrice)
                            .set("air_target_value",(vehiclePlanValue*ornithopterPercent).lround())
                            .set("air_committed_value",ornithopterValue).set("air_target_bps",unitMix[4])
                            .set("military_value",militaryValue).set("military_limit",militaryValueLimit)
                            .set("ornithopter_available",air.ornithopterAvailable)
                            .set("carryall_target",transportTarget).set("carryalls_committed",itemCount[Unit_Carryall])
                            .set("queue",pBuilder->getProductionQueueSize()).set("current_item",pBuilder->getCurrentProducedItem())
                            .set("upgrading",pBuilder->isUpgrading()).set("hold",pBuilder->isOnHold()));
                    }
                    using AirOrder = QuantBotBuildPolicy::AirOrder;
                    if (decision.order == AirOrder::Upgrade) {
                        if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) upgradeWithLogging(__LINE__);
                        else doRepair(pBuilder);
                    } else if (decision.order == AirOrder::Ornithopter || decision.order == AirOrder::Carryall) {
                        const Uint32 item = decision.order == AirOrder::Ornithopter ? Unit_Ornithopter : Unit_Carryall;
                        if (produceItemWithLogging(item, __LINE__, decision.reason)) {
                            ++itemCount[item];

                            if (item == Unit_Ornithopter) militaryValue += ornithopterPrice;
                        }
                    }
                } break;

				case Structure_HeavyFactory: {
					// Log HF status when idle with money (Custom mode diagnostics)
					if (gameMode == GameMode::Custom && emitStatsLog) {
						logDebug("HF=%u: upgrading=%d queue=%d buildList=%d upgLv=%d/%d unitLimit=%d spendable=%d hold=%d military=%d/%d reserve=%d",
							pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
							pBuilder->getBuildListSize(),
							pBuilder->getCurrentUpgradeLevel(), pBuilder->getMaxUpgradeLevel(),
							getHouse()->isGroundUnitLimitReached(), money, pBuilder->isOnHold(),
							militaryValue, militaryValueLimit, strategicReserveCost);
					}
                    const int cityMcvCash = money;
                    if(expansionProducer && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize()==0
                        && money < (campaignAvailableToBuild(pBuilder,Unit_MCV)
                            ? data[Unit_MCV][houseID].price : pBuilder->getUpgradeCost())) break;
                    const bool prioritizeCityMcv = citySimEnabled && gameMode == GameMode::Custom
                        && (fundedCityProduction || !openingWorkersNeeded() || rockExpansionNeeded)
                        && !getHouse()->isGroundUnitLimitReached()
                        && (expansionProducer || QuantBotBuildPolicy::canFundCityYard(cityMcvCash, data[Unit_MCV][houseID].price,
                            itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV], cityYardTarget, cityWorkingReserve));
                    const bool prioritizeMcv = prioritizeCityMcv || (vanillaEconomy && gameMode == GameMode::Custom
                        && !getHouse()->isGroundUnitLimitReached()
                        && DuneCity::prioritizeVanillaMcv(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV], data[Unit_MCV][houseID].price));
                    // A built-out city needs one yard it cannot express as a
                    // capacity shortfall, so the factory may still unlock MCV
                    // production for it.
                    const int mcvShortfall = citySimEnabled
                        ? std::max(colonisationNeeded ? 1 : 0,
                            cityYardTarget - itemCount[Structure_ConstructionYard] - itemCount[Unit_MCV])
                        : DuneCity::vanillaMcvShortfall(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]);
                    if (emitStatsLog && AITelemetry::log().enabled())
                        traceDecision("factory_economy_priority",AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("harvesters_committed",itemCount[Unit_Harvester])
                            .set("spice_target",citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget)
                            .set("army_value",militaryValue).set("army_target",militaryValueLimit)
                            .set("military_available",canBuildMilitaryVehicle(pBuilder))
                            .set("prefer_harvester",factoryPrefersHarvester(pBuilder))
                            .set("opening_workers_needed",openingWorkersNeeded()).set("spendable",money)
                            .set("protected_cash",reserve.reserved).set("queued_cost",queuedProductionCost));
					// only if the factory isn't busy
					if ((pBuilder->isUpgrading() == false) && (pBuilder->getProductionQueueSize() < 1) && (pBuilder->getBuildListSize() > 0)) {
						// we need a construction yard. Build an MCV if we don't have a starport
						if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
							&& itemCount[Unit_MCV] + itemCount[Structure_ConstructionYard] + itemCount[Structure_StarPort] < 1
							&& campaignAvailableToBuild(pBuilder,Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()) {
							if (produceItemWithLogging(Unit_MCV, __LINE__)) itemCount[Unit_MCV]++;
						}
                        else if (prioritizeMcv && campaignAvailableToBuild(pBuilder,Unit_MCV)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__,
                                    citySimEnabled ? "city_cash_construction_capacity" : "cash_construction_capacity")) {
                                ++itemCount[Unit_MCV];
                            }
                        }
                        else if (prioritizeMcv && mcvUpgradesInProgress < mcvShortfall
                            && !campaignAvailableToBuild(pBuilder,Unit_MCV)
                            && pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
                            && (citySimEnabled ? cityMcvCash : money) >= pBuilder->getUpgradeCost()
                                + (expansionProducer ? 0 : citySimEnabled ? std::max(1000, cityWorkingReserve) : 1000)) {
                            if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
                                const bool accepted = upgradeWithLogging(__LINE__);
                                if (accepted) {
                                    ++mcvUpgradesInProgress;
                                }
                                traceDecision("mcv_unlock", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                                    .set("accepted", accepted).set("rule", citySimEnabled
                                        ? "city_cash_construction_capacity" : "cash_construction_capacity"));
                            } else if (!pBuilder->isRepairing()) {
                                doRepair(pBuilder);
                            }
                        }
						else if ((citySimEnabled || vanillaEconomy) && !orderedSpiceHarvester
                            && factoryPrefersHarvester(pBuilder)
                            && itemCount[Unit_Harvester] < (vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
                            && campaignAvailableToBuild(pBuilder,Unit_Harvester) && !getHouse()->isGroundUnitLimitReached()
                            && money + ((vanillaEconomy || harvesterInvestmentReserve() > 0) ? reserve.reserved : 0) >= data[Unit_Harvester][houseID].price
                                + ((campaignEconomyPush || harvesterInvestmentReserve() > 0) ? 0 : vanillaEconomy ? 1000 : (openingWorkersNeeded() || brutalCityEconomy) ? 0 : data[Unit_Tank][houseID].price)) {
                            const int workerCash=(vanillaEconomy || harvesterInvestmentReserve()>0)
                                ? std::max(0,reserve.reserved-capitalReserve(pBuilder->getObjectID())) : 0;
                            money+=workerCash;
                            const bool accepted=produceItemWithLogging(Unit_Harvester,__LINE__,"spice_economy");
                            money-=workerCash;
                            if (accepted) {
                                ++itemCount[Unit_Harvester];
                                orderedSpiceHarvester = true;
                            }
                        }
						else if ((money > 10000) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (gameMode == GameMode::Custom && !vanillaEconomy && !citySimEnabled
							&& campaignAvailableToBuild(pBuilder,Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] < std::min(8, money / 4000)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__, "construction_capacity")) {
                                itemCount[Unit_MCV]++;
                            }
						}
						else if (gameMode == GameMode::Custom
							&& !vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
							&& campaignAvailableToBuild(pBuilder,Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Unit_Harvester] < militaryValue / 1000
							&& itemCount[Unit_Harvester] < harvesterLimit && lastCalculatedSpice > 0) {
							// In case we get given lots of money, it will eventually run out so we need to be prepared
							// Classic-mode harvester expansion; city spice investment is handled above.
							if (produceItemWithLogging(Unit_Harvester, __LINE__)) itemCount[Unit_Harvester]++;
						}
						else if (!vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
                            && lastCalculatedSpice > 0
							&& itemCount[Unit_Harvester] < harvesterLimit
							&& campaignAvailableToBuild(pBuilder,Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& (money < 2000 || gameMode == GameMode::Campaign)) {
							// Classic-mode recovery; city spice investment is handled above.
							if (produceItemWithLogging(Unit_Harvester, __LINE__)) itemCount[Unit_Harvester]++;
						}
						else if ((money > 500) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							// Upgrade before military — unlocks MCV(1), Launcher(2), SiegeTank(3)
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (money > (citySimEnabled || vanillaEconomy ? 500 : 2000)
							&& militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached()) {
                            const std::array<Uint32,6> types = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                                Unit_Devastator, Unit_SonicTank, Unit_Deviator};
                            const int specialValue = data[Unit_Devastator][houseID].price * itemCount[Unit_Devastator]
                                + data[Unit_SonicTank][houseID].price * itemCount[Unit_SonicTank]
                                + data[Unit_Deviator][houseID].price * itemCount[Unit_Deviator];
                            const std::array<FixPoint,6> targets = {tankPercent,siegePercent,launcherPercent,
                                specialPercent,specialPercent,specialPercent};
                            std::array<QuantBotBuildPolicy::AllocationCandidate,6> candidates;
                            AITelemetry::Record choices;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item = types[i];
                                candidates[i] = {data[item][houseID].price,
                                    i>=3 ? specialValue : data[item][houseID].price * itemCount[item],
                                    (targets[i]*10000).lround(), campaignAvailableToBuild(pBuilder,item)};
                            }
                            const int normalHorizon = vehiclePlanValue;
                            const int expansionHorizon = vehiclePlanValue;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item=types[i];
                                const auto& c = candidates[i];
                                choices.set(std::to_string(item), AITelemetry::Record().set("price",c.price)
                                    .set("committed_value",c.committedValue).set("target_bps",c.targetBps)
                                    .set("available",c.available).set("affordable",c.price<=money)
                                    .set("deficit_scaled",int64_t(normalHorizon)*c.targetBps-int64_t(c.committedValue)*10000)
                                    .set("expansion_deficit_scaled",int64_t(expansionHorizon)*c.targetBps-int64_t(c.committedValue)*10000));
                            }
                            int selected = QuantBotBuildPolicy::fundedDeficit(
                                candidates,militaryValue,money,militaryValueLimit,vehiclePlanValue);
                            const bool expansionFallback = selected < 0;
                            if (expansionFallback) selected = QuantBotBuildPolicy::capacityFill(
                                candidates,militaryValue,money,militaryValueLimit);
                            // Candidate tables are useful when the choice changes, but logging the
                            // identical no-deficit decision for every idle factory rapidly exhausts
                            // the match capture. Keep a periodic heartbeat for diagnosis.
                            const Uint32 allocationCycle = getGameCycleCount();
                            const uint64_t allocationSignature = (uint64_t(selected + 1) << 1)
                                | static_cast<uint64_t>(expansionFallback);
                            const auto traceIt = lastHeavyAllocationTrace.find(pBuilder->getObjectID());
                            const bool allocationChanged = traceIt == lastHeavyAllocationTrace.end()
                                || traceIt->second.first != allocationSignature
                                || allocationCycle - traceIt->second.second >= MILLI2CYCLES(30000);
                            if (allocationChanged) {
                                traceDecision("heavy_allocation_decision",AITelemetry::Record()
                                    .set("builder",pBuilder->getObjectID()).set("army_value",militaryValue)
                                    .set("army_limit",militaryValueLimit).set("horizon_value",normalHorizon)
                                    .set("expansion_horizon",expansionHorizon).set("expansion_fallback",expansionFallback)
                                    .set("spendable",money).set("candidates",choices)
                                    .set("selected",selected<0 ? NONE_ID : types[selected])
                                    .set("reason",selected<0 ? "no_affordable_capacity"
                                        : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit"));
                                lastHeavyAllocationTrace[pBuilder->getObjectID()] = {allocationSignature, allocationCycle};
                            }
                            if (selected>=0 && produceItemWithLogging(types[selected],__LINE__,
                                expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit")) {
                                ++itemCount[types[selected]];

                                militaryValue += candidates[selected].price;
                            }
						}
					}

				} break;

				case Structure_StarPort: {
					const StarPort* pStarPort = static_cast<const StarPort*>(pBuilder);
					if (pStarPort->okToOrder()) {
						const Choam& choam = getHouse()->getChoam();

                        // Economic imports may use the cash held for the economy,
                        // just as factory-built harvesters do. Market discounts are
                        // irrelevant to needed workers and the first transport.
                        int workerTarget = citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget;
                        // A bargain worker is a cheap economy upgrade for a human
                        // ally. Fill the permitted fleet instead of stopping at the
                        // normal-price, remaining-spice planning target.
                        const bool bargainWorkers = isCampaignGameType(currentGame->gameType)
                            && isAlliedWithHuman() && lastCalculatedSpice > 0
                            && purchasePrice(pBuilder,Unit_Harvester) > 0
                            && purchasePrice(pBuilder,Unit_Harvester) < data[Unit_Harvester][houseID].price;
                        if (bargainWorkers) {
                            if (getHouse()->getMaxHarvesters() > 0)
                                workerTarget = getHouse()->getMaxHarvesters();
                            const int overrideLimit = getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride;
                            if (overrideLimit > 0) workerTarget = std::min(workerTarget, overrideLimit);
                        }
                        // The shared reservation protects the next construction,
                        // transport or combat order. Spend the remainder on useful
                        // workers without applying a second arbitrary half-cash cap.
                        auto buyEconomicImport = [&](Uint32 item, const char* rule) {
                            const int price = purchasePrice(pBuilder,item);
                            const int cash = money + reserve.reserved - capitalReserve(pBuilder->getObjectID());
                            if (item==Unit_Harvester && !bargainWorkers && harvesterInvestmentReserve()==0
                                && QuantBotSpendingPolicy::marginalSpice(spiceShare,
                                    std::min(itemCount[item]*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome),
                                    std::min((itemCount[item]+1)*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome),
                                    MILLI2CYCLES(30000),CityEconomyInvestmentPolicy::horizonCycles,
                                    DuneCity::kCyclesPerCityYear)<=price) return false;
                            if (item == Unit_Harvester && getHouse()->getMaxHarvesters() > 0
                                && itemCount[item] >= getHouse()->getMaxHarvesters()) return false;
                            if (price <= 0 || cash < price || choam.getNumAvailable(item) <= 0
                                || !pStarPort->isAvailableToBuild(item)) return false;
                            traceDecision("starport_economy_purchase", AITelemetry::Record()
                                .set("item", item).set("market_price", price)
                                .set("normal_price", data[item][houseID].price)
                                .set("spendable", money).set("economic_cash", cash)
                                .set("reserved_cash", reserve.reserved).set("worker_target", workerTarget));
                            const int availableReserve=std::max(0,reserve.reserved-capitalReserve(pBuilder->getObjectID()));
                            money+=availableReserve;
                            const bool accepted=produceItemWithLogging(item,__LINE__,rule);
                            money-=availableReserve;
                            if (!accepted) return false;
                            ++itemCount[item];
                            // The scoped reserve is restored after this builder;
                            // deducting here charges this order exactly once.

                            return true;
                        };

                        // The priority purchase above imports the colonist. If
                        // it is still unaffordable, preserve this port's savings.
                        if (capitalSupplier && capitalPending()
                            && capitalCandidates[capitalChoice].item==Unit_MCV) break;
                        if (itemCount[Unit_Carryall] == 0)
                            buyEconomicImport(Unit_Carryall, "first_economic_transport");
                        while (itemCount[Unit_Harvester] < workerTarget) {
                            if (!buyEconomicImport(Unit_Harvester, "starport_spice_economy")) break;
                        }
                        while (harvesterInvestmentReserve() == 0 && itemCount[Unit_Carryall] < transportTarget)
                            if (!buyEconomicImport(Unit_Carryall, "starport_transport_capacity")) break;

                        // Imports are opportunistic: every discounted combat type
                        // is eligible, regardless of the factory composition targets.
                        // Buy the best percentage discount first, with stable item-ID
                        // tie breaking. Keep the military-value budget and normal caps.
                        std::vector<Uint32> bargains;
                        for (const auto& offer : pStarPort->getBuildList()) {
                            const Uint32 unit = offer.itemID;
                            if (!isUnit(unit) || unit == Unit_Harvester || unit == Unit_Carryall
                                || unit == Unit_MCV || data[unit][houseID].price <= 0
                                || purchasePrice(pBuilder,unit) <= 0 || purchasePrice(pBuilder,unit) >= data[unit][houseID].price) continue;
                            bargains.push_back(unit);
                        }
                        std::sort(bargains.begin(), bargains.end(), [&](Uint32 a, Uint32 b) {
                            const int64_t left = int64_t(purchasePrice(pBuilder,a)) * data[b][houseID].price;
                            const int64_t right = int64_t(purchasePrice(pBuilder,b)) * data[a][houseID].price;
                            return left != right ? left < right : a < b;
                        });
                        for (Uint32 unit : bargains) {
                            const int price = purchasePrice(pBuilder,unit);
                            const int value = data[unit][houseID].price;
                            while (money + reserve.reserved - std::max(harvesterInvestmentReserve(),capitalReserve(pBuilder->getObjectID())) >= price && choam.getNumAvailable(unit) > 0
                                && militaryValue + value <= militaryValueLimit
                                && !getHouse()->isUnitLimitReached(unit)) {
                                const int availableReserve=std::max(0,reserve.reserved
                                    - std::max(harvesterInvestmentReserve(),capitalReserve(pBuilder->getObjectID())));
                                money+=availableReserve;
                                const bool accepted=produceItemWithLogging(unit,__LINE__,"starport_bargain");
                                money-=availableReserve;
                                if (!accepted) break;
                                ++itemCount[unit];

                                militaryValue += value;
                            }
                        }

						doPlaceOrder(pStarPort);
					}

				} break;

				case Structure_ConstructionYard: {

				const ConstructionYard* pConstYard = static_cast<const ConstructionYard*>(pBuilder);

                // Compare the funded next-wave mix with fielded AND queued units.
                // Cash targets must not hide a heavy-unit production bottleneck.
                const int fundedArmy = vehiclePlanValue;
                int heavyValue = 0;
                for (const auto unit : {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                        Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    heavyValue += itemCount[unit] * data[unit][houseID].price;
                const int heavyDeficit = std::max(0,
                    (fundedArmy * (tankPercent + siegePercent + launcherPercent + specialPercent)).lround() - heavyValue);
                const int lightDeficit = std::max<int64_t>(0,int64_t(fundedArmy)*lightVehicleBps/10000-lightVehicleValue);
                const bool lightBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_LightFactory),
                        itemCount[Structure_LightFactory],activeLightFactoryCount,lightDeficit,
                        money,economyReserve,data[Structure_LightFactory][houseID].price);
                const int airDeficit = std::max(0, (fundedArmy * ornithopterPercent).lround()
                    - itemCount[Unit_Ornithopter] * data[Unit_Ornithopter][houseID].price);
                const bool heavyBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_HeavyFactory),
                        itemCount[Structure_HeavyFactory], activeHeavyFactoryCount, heavyDeficit,
                        money, economyReserve, data[Structure_HeavyFactory][houseID].price);
                const bool airBacklog = QuantBotBuildPolicy::needsAirProductionLane(
                    getHouse()->getNumItems(Structure_HighTechFactory), itemCount[Structure_HighTechFactory],
                    activeHighTechFactoryCount, ornithopterCapableFactoryCount, airDeficit,
                    money, economyReserve, data[Structure_HighTechFactory][houseID].price,
                    data[Unit_Ornithopter][houseID].price, militaryValueLimit-militaryValue,
                    getHouse()->isAirUnitLimitReached());

				// Each yard owns its concrete/structure placement sequence. Sharing
				// a single FIFO lets the faster yard consume the other's locations.
				planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false,true);
                auto& placeLocations = builderPlaceLocations[pBuilder->getObjectID()];
				if (pBuilder->getProductionQueueSize() == 0) placeLocations.clear();
				if (emitStatsLog) {
					logDebug("PRODUCTION: CY=%u upgrading=%d queue=%d hold=%d credits=%d buildList=%d R/C/I=%d/%d/%d reserve=%u/%d",
						pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
						pBuilder->isOnHold(), money, pBuilder->getBuildListSize(),
						itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
						itemCount[Structure_ZoneIndustrial], strategicReserveItem, strategicReserveCost);
					auto* balanceCity = currentGame ? currentGame->getCitySimulation() : nullptr;
					const int taxIncome = citySimEnabled
						? DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths, balanceCity ? balanceCity->getCityTax() : 7, ownAvgLandValue) / 60 : 0;
					int factoryTarget = QuantBotBuildPolicy::desiredHeavyFactories(citySimEnabled, taxIncome, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());
                    if (vanillaEconomy) factoryTarget = DuneCity::vanillaFactoryTarget(factoryTarget, getHouse()->getNumItems(Unit_Harvester), money);
					const int tech = currentGame ? currentGame->techLevel : 8;
					const bool policyPrerequisites = citySimEnabled || vanillaEconomy || tech <= 4
						|| (itemCount[Structure_RepairYard] > 0 && (tech <= 6 || itemCount[Structure_IX] > 0));
					const char* factoryReason = !campaignAvailableToBuild(pBuilder,Structure_HeavyFactory) ? "unavailable"
						: money <= std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) ? "cash-reserve"
						: militaryValue >= militaryValueLimit ? "military-limit"
						: getHouse()->isGroundUnitLimitReached() ? "unit-limit"
						: heavyBacklog && itemCount[Structure_HeavyFactory] < 24 ? "funded-unit-backlog"
						: itemCount[Structure_HeavyFactory] >= factoryTarget ? "target-met"
						: !policyPrerequisites ? "tech-policy" : "expansion-due";
                    traceDecision("builder_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("queue", pBuilder->getProductionQueueSize()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("heavy_target", factoryTarget)
                        .set("heavy_reason", factoryReason).set("heavy_busy", activeHeavyFactoryCount)
                        .set("heavy_deficit", heavyDeficit).set("air_deficit", airDeficit)
                        .set("heavy_backlog", heavyBacklog).set("air_backlog", airBacklog)
                        .set("light_backlog",lightBacklog).set("light_busy",activeLightFactoryCount).set("light_deficit",lightDeficit)
                        .set("high_tech_busy", activeHighTechFactoryCount)
                        .set("high_tech_air_capable", ornithopterCapableFactoryCount)
                        .set("air_army_room", militaryValueLimit-militaryValue)
                        .set("high_tech_building_ornithopters", ornithopterFactoryCount)
                        .set("heavy_economy_target", vanillaEconomy ? std::max(1, getHouse()->getNumItems(Unit_Harvester) / 3) : 0)
                        .set("heavy_cash_target", vanillaEconomy ? 1 + std::max(0, money - 10000) / 4000 : 0)
                        .set("heavy_opening_target", vanillaEconomy ? std::min(factoryTarget, 2 * std::clamp(itemCount[Structure_ConstructionYard], 1, 8)) : 0)
                        .set("heavy_losses_2min", recentFactoryLossCount())
                        .set("repair_busy", activeRepairYardCount).set("repair_target",repairTarget)
                        .set("state", decisionState()));
					logDebug("BUILD-BALANCE: CY=%u HF=%d queued=%d busy=%d target=%d reason=%s RY=%d queued=%d busy=%d cap=%d taxPerSec=%d power=%d/%d",
						pBuilder->getObjectID(), getHouse()->getNumItems(Structure_HeavyFactory),
						itemCount[Structure_HeavyFactory] - getHouse()->getNumItems(Structure_HeavyFactory),
						activeHeavyFactoryCount, factoryTarget, factoryReason,
						getHouse()->getNumItems(Structure_RepairYard),
						itemCount[Structure_RepairYard] - getHouse()->getNumItems(Structure_RepairYard),
						activeRepairYardCount, repairTarget,
						taxIncome, getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
				}

					if (!pBuilder->isUpgrading() && getHouse()->getCredits() >= 100 && (pBuilder->getProductionQueueSize() < 1) && pBuilder->getBuildListSize()) {

						// Campaign Build order, iterate through the buildings, if the number that exist
						// is less than the number that should exist, then build the one that is missing

                        // City campaigns need the same economy/civic planner as
                        // city custom games. The classic rebuild list puts zones
                        // behind upgrades and defences and can starve them forever.
						if (!citySimEnabled && gameMode == GameMode::Campaign && difficulty != Difficulty::Brutal) {
							//logDebug("GameMode Campaign.. ");

                            if (storageExpansionNeeded() && getHouse()->hasPower()
                                && campaignAvailableToBuild(pBuilder,Structure_Silo)
                                && findPlaceLocation(Structure_Silo).isValid()
                                && queueFoundedStructure(Structure_Silo,__LINE__,"spice_storage_priority")) {
                                ++itemCount[Structure_Silo];
                                break;
                            }

						for (int i = Structure_FirstID; i <= Structure_LastID; i++) {
							if (itemCount[i] < initialItemCount[i]
								&& campaignAvailableToBuild(pBuilder,i)
								&& findPlaceLocation(i).isValid()
								&& !pBuilder->isUpgrading()
								&& pBuilder->getProductionQueueSize() < 1) {

                                if (queueCampaignWindtrap(std::max(0,data[i][houseID].power))) break;
								logDebug("***CampAI Build itemID: %o structure count: %o, initial count: %o", i, itemCount[i], initialItemCount[i]);
								if (queueFoundedStructure(i, __LINE__)) itemCount[i]++;  // Increment immediately to prevent multiple CYs from building same item
							}
						}

							// If Campaign AI can't build military, let it build up its cash reserves and defenses

							if (pStructure->getHealth() < pStructure->getMaxHealth()) {
								doRepair(pBuilder);
								int health = pStructure->getHealth().lround();
								int maxHealth = pStructure->getMaxHealth();
								logDebug("PRODUCTION: Repairing CY, health: %d/%d", health, maxHealth);
							}
							else if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
								&& !pBuilder->isUpgrading()
								&& itemCount[Unit_Harvester] >= harvesterLimit
								&& money > 1500) {  // Don't upgrade if low on money (need money for structures/units)

								upgradeWithLogging(__LINE__);
								logDebug("PRODUCTION: Upgrading CY to level %d, credits: %d", pBuilder->getCurrentUpgradeLevel() + 1, money);
							}
							else if ((!getHouse()->hasPower())
								&& pBuilder->getProductionQueueSize() == 0) {
								// Prefer nuclear plant over windtrap
								if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
									&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
                                    if (queueFoundedStructure(Structure_NuclearPlant, __LINE__))
                                        itemCount[Structure_NuclearPlant]++;
									logDebug("***CampAI Build Nuclear Plant: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
									&& findPlaceLocation(Structure_WindTrap).isValid()) {
									if (queueFoundedStructure(Structure_WindTrap, __LINE__)) itemCount[Structure_WindTrap]++;
									logDebug("***CampAI Build windtrap: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								}
							}
							// Legacy campaign reconstruction shares the economic
							// generation gate: Vanilla's hasPower() above never
							// reports the raw shortage that degrades the base.
							else if (const Uint32 generator = economicGenerator(); generator != NONE_ID) {
								if (queueFoundedStructure(generator, __LINE__, "power_investment"))
									++itemCount[generator];
							}
							else if (money > 3000
								&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
								&& pBuilder->getProductionQueueSize() == 0
								&& (itemCount[Structure_RocketTurret] <
									(itemCount[Structure_Silo] + itemCount[Structure_Refinery]) * 2)
								&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {

								if (queueFoundedStructure(Structure_RocketTurret, __LINE__)) itemCount[Structure_RocketTurret]++;

								logDebug("***CampAI Build A new Rocket turret increasing count to: %d", itemCount[Structure_RocketTurret]);
							}
							// City zone structures for campaign AI — pick by live demand AND ratio.
							else if (currentGame && currentGame->isCitySimEnabled()
								&& money > 200
								&& pBuilder->getProductionQueueSize() == 0
								&& itemCount[Structure_WindTrap] > 0) {
								const int resCount = itemCount[Structure_ZoneResidential];
								const int comCount = itemCount[Structure_ZoneCommercial];
								const int indCount = itemCount[Structure_ZoneIndustrial];
								const Uint32 zoneID = chooseCityZone(pBuilder, false);

								if (zoneID != NONE_ID && campaignAvailableToBuild(pBuilder,zoneID)
									&& findPlaceLocation(zoneID).isValid()) {
									if (queueFoundedStructure(zoneID, __LINE__)) itemCount[zoneID]++;
									logDebug("***CampAI CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d)",
										getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
										ownResValve, ownComValve, ownIndValve);
								}
							}

							// MULTIPLAYER FIX: Use deterministic timer instead of random
							buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
						}
						else {
								// custom AI starts here:

								Uint32 itemID = NONE_ID;
                const char* structureRule = "no_eligible_structure";
                Coord crimeServiceSite = Coord::Invalid();
                // Set when this yard is short of a demanded service's price and
                // the forecast can still reach it. Cheap zoning must not spend
                // that budget, or the service is never ordered at all.
                bool serviceSavingHold = false;
								bool skipRemainingStructureLogic = false;

                if (storageExpansionNeeded() && getHouse()->hasPower()
                    && campaignAvailableToBuild(pBuilder,Structure_Silo)
                    && money >= buildingCapitalCost(Structure_Silo)
                    && findPlaceLocation(Structure_Silo).isValid()) {
                    itemID=Structure_Silo;
                    structureRule="spice_storage_priority";
                    skipRemainingStructureLogic=true;
                }

                // Honour the growth allocation in the actual yard decision,
                // not just the shared cash reserve. Previously this yard could
                // immediately spend the protected plot's budget on a service.
                if (itemID==NONE_ID && cityGrowthProtected && capitalPending()
                    && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID()) {
                    itemID=affordableCityZone(pBuilder,money);
                    if (itemID!=NONE_ID) structureRule="dedicated_city_growth";
                }

				// Skip build order if something is already queued
								if (pBuilder->getProductionQueueSize() > 0) {
									skipRemainingStructureLogic = true;
								}

							// Count enemy ornithopters - use MAXIMUM from a single enemy house, not sum
								int maxEnemyOrnithopters = 0;
								int totalEnemyOrnithopters = 0;
								if (currentGame) {
								for (int i = 0; i < NUM_HOUSES; i++) {
									const House* pHouse = currentGame->getHouse(i);
									if (pHouse && pHouse->getTeamID() != getHouse()->getTeamID()) {
										int houseOrnis = pHouse->getNumItems(Unit_Ornithopter);
										totalEnemyOrnithopters += houseOrnis;
										if (houseOrnis > maxEnemyOrnithopters) {
											maxEnemyOrnithopters = houseOrnis;
										}
									}
									}
								}
								const int requiredTurrets = std::max(maxEnemyOrnithopters * 2, totalEnemyOrnithopters);
								const bool ixExpected = data[Structure_IX][houseID].enabled
									&& data[Structure_IX][houseID].techLevel <= currentGame->techLevel;
								const bool palaceExpected = data[Structure_Palace][houseID].enabled
									&& data[Structure_Palace][houseID].techLevel <= currentGame->techLevel;
								const bool strategicInfrastructureIncomplete = customStrategicPlanning
									&& ((ixExpected && itemCount[Structure_IX] == 0)
										|| (palaceExpected && itemCount[Structure_Palace] == 0));
								// The interim cap left a large base with two emplacements
								// while optional tech finished, which cannot span it.
								// Scale it with the cover the base actually demands —
								// which now grows with difficulty — while the enemy
								// wing still caps the goal itself.
								const int activeRocketTurretGoal = strategicInfrastructureIncomplete
									? std::min(requiredTurrets, RocketTurretPolicy::coverageTurretCap(coverageDemand))
									: requiredTurrets;

								// Power buffer check for the next turret and city growth.
								// Only applies if rocketTurretsNeedPower is enabled
								auto hasPowerBufferForTurret = [&]() {
									if (!turretPowerRequired) {
										return true; // No power requirement, always allow
									}
									int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
									// Keep the city-specific headroom after powering the turret.
									return powerExcess >= rocketPowerBuffer;
								};

								// CRITICAL: Counter enemy ornithopters ASAP (prep prerequisites if needed)
								// An enemy wing seen elsewhere on the map is a plan, not a
								// present loss: it buys the baseline emplacements and its own
								// prerequisites, then waits behind the production core.
								if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& maxEnemyOrnithopters > 0
                                    && routineRocketsAllowed
									&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal) {
								bool hasWindtrap = itemCount[Structure_WindTrap] > 0;
								bool hasRadar = itemCount[Structure_Radar] > 0;

							if (pBuilder->getCurrentUpgradeLevel() < 2) {
							if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
								doRepair(pBuilder);
											logDebug("COUNTER-ORNITHOPTER: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
										} else if (!pBuilder->isUpgrading() && pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
											logDebug("COUNTER-ORNITHOPTER: Upgrading CY (level %d -> %d)", pBuilder->getCurrentUpgradeLevel(), pBuilder->getCurrentUpgradeLevel() + 1);
							}
									} else if (!hasWindtrap && (!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
										logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasRadar && campaignAvailableToBuild(pBuilder,Structure_Radar) && getHouse()->hasPower()) {
						itemID = Structure_Radar; structureRule = "radar_prerequisite";
										logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasPowerBufferForTurret()) {
										int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
										if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
											&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
											itemID = Structure_NuclearPlant; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
										} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
											&& findPlaceLocation(Structure_WindTrap).isValid()) {
											itemID = Structure_WindTrap; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
										}
									} else if (campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
							&& hasPowerBufferForTurret()
							            && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
							itemID = Structure_RocketTurret; structureRule = "rocket_defense";
										logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", maxEnemyOrnithopters, activeRocketTurretGoal);
									}
								}

								// The bulk-concrete upgrade already ran ahead of this
								// whole selection, for every mode and difficulty.

								// Essential infrastructure - Build Order:
								// 1. WindTrap (if 0)
								// 2. Refinery (if 0)
								// 3. Refinery (ratio with harvesters)
								// 4. Refinery (< 4, money < 2000)
				// 5. Starport when the map offers useful imports
				// 6. Radar
				// 7. Light Factory
				// 8. Repair Yard (if starport or heavy factory exists)
				// 8b. 2 Rocket Turrets (if starport or heavy factory exists)
				// 8c. Counter ornithopters (turrets < 2x max enemy ornis)
				// 9. Heavy Factory (money > 500)
				// 10. High Tech Factory (if no carryalls in CHOAM or no starport)

				// 1. WindTrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_WindTrap] == 0 
					&& (!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
					}
				// 1b. Power Deficit Recovery - prefer nuclear plant, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& (!getHouse()->hasPower() || (turretPowerRequired
                        && itemCount[Structure_RocketTurret] > 0
                        && getHouse()->getProducedPower() < getHouse()->getPowerRequirement()))) {
					int powerDeficit = getHouse()->getPowerRequirement() - getHouse()->getProducedPower();
					if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
						&& money >= data[Structure_NuclearPlant][houseID].price
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
					} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
					}
				}
                // 1c. Economic generation outside the city simulation. Ahead of
                // every optional purchase below, behind the first windtrap and
                // the functional recovery above, and only when the documented
                // gate accepts the whole package.
                if (itemID == NONE_ID && !skipRemainingStructureLogic) {
                    const Uint32 generator = economicGenerator();
                    if (generator != NONE_ID) {
                        itemID = generator; structureRule = "power_investment";
                        skipRemainingStructureLogic = true;
                    }
                }
                // Honour the winning service before optional power headroom,
                // opening tech, civic growth and production expansion. Actual
                // blackout recovery above remains necessary to operate it.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && protectionCapital && capitalPending()
                    && capitalCandidates[capitalChoice].builder == pBuilder->getObjectID()) {
                    const auto& service = capitalCandidates[capitalChoice];
                    if (service.kind==std::string("expansion_upgrade")) {
                        skipRemainingStructureLogic=true;serviceSavingHold=true;
                        structureRule="save_expansion_upgrade";
                        if (money>=service.price && pBuilder->getHealth()>=pBuilder->getMaxHealth()
                            && upgradeWithLogging(__LINE__)) {
                            capitalConsumed=true;structureRule="expansion_turret_upgrade";
                        }
                    } else if (campaignAvailableToBuild(pBuilder, service.item) && service.site.isValid()) {
                        itemID = money >= service.price + service.foundationCost ? service.item : NONE_ID;
                        crimeServiceSite = service.site;
                        structureRule = itemID == NONE_ID ? "save_city_protection" : "city_protection";
                        skipRemainingStructureLogic = true;
                    }
                }
                const bool higherPriorityCapital = capitalPending()
                    && capitalCandidates[capitalChoice].builder == pBuilder->getObjectID()
                    && capitalCandidates[capitalChoice].score > 2600;
                // Seed each demanded part of the tax economy once the spice
                // opening is running. Optional upgrades must not monopolize a
                // single yard before residents and jobs can start developing.
                // Active air defence, dangerous crime and blackouts stay above.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital && cityYards == 1 && itemCount[Structure_Refinery] >= 2) {
                    const Uint32 seed = chooseCityZone(pBuilder, true, true);
                    if (seed != NONE_ID) {
                        if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() >= 24
                            && money >= buildingCapitalCost(seed)) {
                            itemID = seed;
                            structureRule = "opening_city_seed";
                        } else if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() < 24
                            && !powerGenerationPending()
                            && campaignAvailableToBuild(pBuilder,Structure_WindTrap)
                            && money >= buildingCapitalCost(Structure_WindTrap)
                            && findPlaceLocation(Structure_WindTrap).isValid()) {
                            itemID = Structure_WindTrap;
                            structureRule = "opening_city_power";
                        }
                    }
                }
                // On a rich field, keep compounding a profitable refinery's
                // included worker beyond four bays before optional construction.
                // One housing seed and the first two rockets retain their slots.
                if (itemID==NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital && itemCount[Structure_RocketTurret]>=2
                    && openingFleetIncomplete() && itemCount[Structure_ZoneResidential]>0) {
                    const Uint32 investment=chooseCityEconomy(pBuilder,false);
                    if (investment==Structure_Refinery) {
                        itemID=money>=buildingCapitalCost(investment) ? investment : NONE_ID;
                        structureRule=itemID==NONE_ID ? "save_profitable_spice" : "profitable_spice_expansion";
                        skipRemainingStructureLogic=true;
                        serviceSavingHold=itemID==NONE_ID;
                    }
                }
                // Protect the income/rebuild core before optional tech. Walk the
                // actual mod prerequisites and save their cost, rather than
                // waiting for enemy aircraft to reveal that the yard is unready.
                const bool completingCore=itemCount[Structure_RocketTurret]>=2 && missingCoreInfrastructure!=NONE_ID;
                const bool openingWorkerSupplier = openingSupplierDue || completingCore;
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital
                    && ((openingWorkerSupplier && itemCount[Structure_RocketTurret] >= 2)
                        || ((itemCount[Structure_Refinery] >= 3 || airEngaged)
                            && proactiveCoverageShortfall() > 0
                            && data[Structure_RocketTurret][houseID].enabled
                            && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel))) {
                    // Once basic defence exists, unlock the producer that can
                    // fill the opening fleet. Starport upgrades otherwise wait
                    // behind cheap lots even on a rich spice field.
                    const bool supplyingWorkers = openingWorkerSupplier && itemCount[Structure_RocketTurret] >= 2;
                    Uint32 step = completingCore ? missingCoreInfrastructure : supplyingWorkers
                        ? (itemCount[Structure_Refinery]<4 ? Structure_Refinery
                            : starportMarketAvailable ? Structure_StarPort : Structure_HeavyFactory)
                        : Structure_RocketTurret;
                    if (!supplyingWorkers && !hasPowerBufferForTurret()) step = Structure_WindTrap;
                    if (completingCore && data[step][houseID].power>
                        getHouse()->getProducedPower()-getHouse()->getPowerRequirement()) step=Structure_WindTrap;
                    for (int depth=0; depth<Structure_LastID && step!=NONE_ID; ++depth) {
                        if (!data[step][houseID].enabled || data[step][houseID].techLevel>currentGame->techLevel) break;
                        if (step!=Structure_RocketTurret && itemCount[step]>getHouse()->getNumItems(step)) break;
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            int coverage=0;
                            const Coord site=step==Structure_RocketTurret
                                ? findCityTurretPlaceLocation(step,&coverage) : findPlaceLocation(step);
                            if (site.isValid() && (step!=Structure_RocketTurret || coverage>0)) {
                                const int cost=buildingCapitalCost(step);
                                if (money>=cost || cashFlow.projectedCash>=cost) {
                                    itemID=money>=cost ? step : NONE_ID;
                                    if (itemID!=NONE_ID) crimeServiceSite=site;
                                    serviceSavingHold=itemID==NONE_ID && proactiveSavingHold(cost);
                                    skipRemainingStructureLogic=true;
                                    structureRule=supplyingWorkers ? (itemID==NONE_ID ? "save_opening_supplier" : "opening_supplier")
                                        : itemID==NONE_ID ? "save_core_defence" : "core_defence";
                                }
                            }
                            break;
                        }
                        if (pBuilder->getCurrentUpgradeLevel()<data[step][houseID].upgradeLevel
                            && pBuilder->getMaxUpgradeLevel()>=data[step][houseID].upgradeLevel) {
                            const int cost=pBuilder->getUpgradeCost();
                            if (cost>0 && (money>=cost || cashFlow.projectedCash>=cost)) {
                                serviceSavingHold=proactiveSavingHold(cost);skipRemainingStructureLogic=true;
                                structureRule=supplyingWorkers ? "unlock_opening_supplier" : "unlock_core_defence";
                                if (money>=cost && !pBuilder->isUpgrading()
                                    && pBuilder->getHealth()>=pBuilder->getMaxHealth()) upgradeWithLogging(__LINE__);
                            }
                            break;
                        }
                        Uint32 missing=NONE_ID;
                        for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
                            if (data[step][houseID].prerequisiteStructuresSet[prerequisite]
                                && getHouse()->getNumItems(prerequisite)==0) {missing=prerequisite;break;}
                        step=missing;
                    }
                }
                // 1c. Cover zone maturation/recovery and queued consumers as
                // well as the normal reserve. Keep one generator in flight.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
                    && itemCount[Structure_Refinery] >= QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget)) {
					const int produced  = getHouse()->getProducedPower();
					const int required  = getHouse()->getPowerRequirement();
					const int buffer    = produced - required;
					const int targetBuffer = cityPowerReserve;
					if (required > 0 && buffer < targetBuffer) {
						// Prefer an affordable nuclear plant for capacity; the shared
						// investment policy below retains cheap wind for small starts.
						if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
							&& money >= data[Structure_NuclearPlant][houseID].price
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
									 buffer, targetBuffer, required);
						} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d)",
									 buffer, targetBuffer);
						}
					}
				}
                // Accumulate reactor funds before optional orders consume them.
                // An actual blackout can still buy an immediately affordable windtrap.
                if (nuclearPlan && !(protectionCapital && capitalPending())
                    && getHouse()->hasPower() && !powerGenerationPending()
                    && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)
                    && (itemID == NONE_ID || itemID == Structure_WindTrap || itemID == Structure_NuclearPlant)
                    && findPlaceLocation(Structure_NuclearPlant).isValid()) {
                    skipRemainingStructureLogic = true;
                    itemID = money >= data[Structure_NuclearPlant][houseID].price ? Structure_NuclearPlant : NONE_ID;
                    structureRule = itemID == NONE_ID ? "save_nuclear_growth" : "nuclear_growth_investment";
                    if (emitStatsLog) traceDecision("nuclear_investment",AITelemetry::Record()
                        .set("funded",itemID != NONE_ID).set("cash",money)
                        .set("price",data[Structure_NuclearPlant][houseID].price).set("reserve",cityPowerReserve));
                }
                // Restore a lost heavy production line before optional growth.
                // Walk missing prerequisites (e.g. the light factory lost in the
                // same attack); pending buildings prevent duplicate orders.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && itemCount[Structure_HeavyFactory] == 0
                    && getHouse()->getNumLostItems(Structure_HeavyFactory) > 0
                    && data[Structure_HeavyFactory][houseID].enabled
                    && data[Structure_HeavyFactory][houseID].techLevel <= currentGame->techLevel) {
                    Uint32 step = Structure_HeavyFactory;
                    for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
                        if (itemCount[step] > 0) {
                            skipRemainingStructureLogic = true;
                            structureRule = "heavy_recovery_pending";
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (findPlaceLocation(step).isValid()) {
                                skipRemainingStructureLogic = true;
                                structureRule = "save_heavy_recovery";
                                if (money >= data[step][houseID].price) {
                                    itemID = step;
                                    structureRule = "heavy_production_recovery";
                                }
                            }
                            break;
                        }
                        Uint32 prerequisite = NONE_ID;
                        for (int i = Structure_FirstID; i <= Structure_LastID; ++i)
                            if (data[step][houseID].prerequisiteStructuresSet[i]
                                && getHouse()->getNumItems(i) == 0) { prerequisite = i; break; }
                        step = prerequisite;
                    }
                }
                if (itemID==NONE_ID && !skipRemainingStructureLogic && fundedFactoryOpening) {
                    Uint32 step=Structure_HeavyFactory;
                    for (int depth=0;step!=NONE_ID && depth<Structure_LastID;++depth) {
                        if (itemCount[step]>0) {
                            // A pending prerequisite will complete normally; let
                            // other yards use their own slots for useful growth.
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (money>=data[step][houseID].price && findPlaceLocation(step).isValid()) {
                                itemID=step;structureRule="funded_factory_opening";
                                skipRemainingStructureLogic=true;
                            }
                            break;
                        }
                        Uint32 prerequisite=NONE_ID;
                        for (int i=Structure_FirstID;i<=Structure_LastID;++i)
                            if (data[step][houseID].prerequisiteStructuresSet[i]
                                && getHouse()->getNumItems(i)==0) {prerequisite=i;break;}
                        step=prerequisite;
                    }
                }
                // Starport opening: four income-producing refineries, the port,
                // then its repair support before optional factories. Small/spice-poor
                // maps use fewer workers; missions that lock the port keep their
                // normal early-tech progression. Authored campaign rebuild lists
                // are handled above and do not acquire extra opening structures.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && starportMarketAvailable && !fundedFactoryOpening
                    && data[Structure_StarPort][houseID].enabled
                    && currentGame->techLevel >= data[Structure_StarPort][houseID].techLevel) {
                    Uint32 step = NONE_ID;
                    if (getHouse()->getNumItems(Structure_StarPort) == 0) {
                        const int refineryGoal = std::min(4, mapSpiceHarvesterTarget);
                        step = itemCount[Structure_Refinery] < refineryGoal
                            ? Structure_Refinery : Structure_StarPort;
                    } else if (getHouse()->getNumItems(Structure_RepairYard) == 0
                        && canAddRepairYard(itemCount[Structure_RepairYard])
                        && data[Structure_RepairYard][houseID].enabled
                        && currentGame->techLevel >= data[Structure_RepairYard][houseID].techLevel) {
                        step = Structure_RepairYard;
                    }
                    // Walk missing prerequisites, stopping at a pending build.
                    // This uses each mod's actual tech tree, without adding heavy
                    // or high-tech factories unless that tree requires them.
                    for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
                        if (itemCount[step] > 0 && step != Structure_Refinery) {
                            skipRemainingStructureLogic = true;
                            structureRule = "starport_opening_pending";
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (findPlaceLocation(step).isValid()) {
                                skipRemainingStructureLogic = true;
                                structureRule = "starport_opening";
                                if (money >= data[step][houseID].price) itemID = step;
                                // Keep the next worker supplier's savings intact.
                                // Cheap fallback zoning otherwise spends them
                                // every pass and postpones the opening fleet.
                                serviceSavingHold = itemID == NONE_ID && openingFleetIncomplete()
                                    && getHouse()->getNumItems(Structure_StarPort) == 0
                                    && proactiveSavingHold(data[step][houseID].price);
                            }
                            break;
                        }
                        Uint32 prerequisite = NONE_ID;
                        for (int i = Structure_FirstID; i <= Structure_LastID; ++i) {
                            if (data[step][houseID].prerequisiteStructuresSet[i]
                                && getHouse()->getNumItems(i) == 0) { prerequisite = i; break; }
                        }
                        step = prerequisite;
                    }
                }
				// Low-spice economy: skip additional refineries, pivot to R/I/C zones
				const bool lowSpiceEconomy = (lastCalculatedSpice < 500);
				const bool isCitySim = (currentGame && currentGame->isCitySimEnabled());

                // The first refinery supplies income and unlocks the tech tree.
                // Further processing capacity competes with demanded tax growth.
                const int openingRefineries = QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget);
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_Refinery)
                    && findPlaceLocation(Structure_Refinery).isValid()) {
                    skipRemainingStructureLogic = true;
                    structureRule = "city_saving_for_spice_opening";
                    if (money >= data[Structure_Refinery][houseID].price) {
                        itemID = Structure_Refinery;
                        structureRule = "city_spice_opening";
                    }
                    if (emitStatsLog) traceDecision("city_spice_opening", AITelemetry::Record()
                        .set("target",openingRefineries).set("refineries",itemCount[Structure_Refinery])
                        .set("spice_share",spiceShare).set("credits",money)
                        .set("price",data[Structure_Refinery][houseID].price).set("funded",itemID != NONE_ID));
                }

                // One demanded residential plot hedges spice income immediately.
                // After that, buy the better return until the small opening
                // economy exists; don't force a commercial/industrial seed.
                const int openingZones = itemCount[Structure_ZoneResidential]
                    + itemCount[Structure_ZoneCommercial] + itemCount[Structure_ZoneIndustrial];
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && ((itemCount[Structure_ZoneResidential] == 0 && ownResValve>0)
                        || ((brutalCityEconomy || openingZones<6) && itemCount[Structure_Refinery]<openingRefineries))) {
                    const Uint32 investment = chooseCityEconomy(pBuilder,true);
                    if (investment != NONE_ID && (!brutalCityEconomy || investment == Structure_Refinery
                        || itemCount[Structure_ZoneResidential] == 0)) {
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_opening_investment";
                        if (money>=data[investment][houseID].price) {
                            itemID=investment;
                            structureRule="city_opening_investment";
                        }
                    }
                }

                // After the seed economy, save for the first vehicle factory and
                // its available prerequisite. Cheap zoning must not spend that
                // money afresh on every tick. Unavailable/unplaceable tech does
                // not reserve cash, and essential power above still takes priority.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && (itemCount[Structure_ZoneResidential] > 0 || ownResValve<=0)) {
                    for (Uint32 candidate : {Structure_HeavyFactory, Structure_Radar, Structure_LightFactory}) {
                        if (itemCount[candidate] > 0 || !campaignAvailableToBuild(pBuilder,candidate)
                            || !findPlaceLocation(candidate).isValid()) continue;
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_vehicle_production";
                        if (money >= data[candidate][houseID].price) {
                            itemID = candidate;
                            structureRule = "city_vehicle_production_bootstrap";
                        }
                        if (emitStatsLog) traceDecision("city_bootstrap_reserve", AITelemetry::Record()
                            .set("item",candidate).set("price",data[candidate][houseID].price)
                            .set("credits",money).set("funded",itemID != NONE_ID));
                        break;
                    }
                }

                // Cash already harvested but trapped at unloading bays takes
                // priority over optional civic/defence/zoning investments.
                if(itemID==NONE_ID&&!skipRemainingStructureLogic&&unloadingBacklog) {
                    const Uint32 investment=isCitySim ? chooseCityEconomy(pBuilder,false) : Uint32(Structure_Refinery);
                    if(investment==Structure_Refinery && campaignAvailableToBuild(pBuilder,investment)
                        && findPlaceLocation(investment).isValid()) {
                        skipRemainingStructureLogic=true;
                        itemID=money>=data[investment][houseID].price?investment:NONE_ID;
                        structureRule="city_refinery_capacity";
                    }
                }

                // An announced growth cap is a funded investment, before
                // optional tech, extra production, services and additional zoning.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim) {
                    const Uint32 civic = demandedCivicForYard(pBuilder);
                    if (civic != NONE_ID) {
                        skipRemainingStructureLogic = true;
                        itemID = money >= data[civic][houseID].price ? civic : NONE_ID;
                        structureRule = itemID == NONE_ID ? "save_demanded_civic"
                            : civic == Structure_Stadium ? "residential_civic" : "commercial_civic";
                        if (emitStatsLog) traceDecision("city_civic_investment",AITelemetry::Record()
                            .set("item",civic).set("funded",itemID != NONE_ID).set("cash",money)
                            .set("price",data[civic][houseID].price));
                    }
                }

                // Proactively cover the whole city, not just the first two
                // factories. Planned turrets count, so parallel yards fill gaps.
                // Preserve the opening worker investment and interleave peaceful
                // coverage with growth; observed enemy aircraft make it urgent.
                // Observed aircraft are a present loss, not a growth trade-off:
                // do not make that coverage wait for the opening worker fleet,
                // a heavy factory or a spare zone's price. Peaceful coverage
                // keeps interleaving with growth on the established terms.
                // A wing counted from the enemy's item list is a plan; aircraft
                // that have reached our buildings are the present loss.
                const bool airThreatSeen = airEngaged;
                // An uncovered yard, refinery or factory is the same kind of
                // present loss as an enemy wing: it does not wait for the
                // opening worker fleet, a heavy factory or a spare zone's price.
                // Deepening the overlap on assets already covered does.
                const int coreShortfall = isCitySim ? proactiveCoverageShortfall() : 0;
                const bool coreDefenceDue = airThreatSeen
                    || (coreShortfall > 0 && uncoveredCoreAssets > 0);
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && !higherPriorityCapital
                    && (coreDefenceDue
                        || (!openingWorkersNeeded() && itemCount[Structure_HeavyFactory] > 0
                            && nonServiceConstructionOrders >= 3))
                    && campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
                    && hasPowerBufferForTurret()
                    && money >= data[Structure_RocketTurret][houseID].price
                        + (coreDefenceDue ? 0 : data[Structure_ZoneResidential][houseID].price)) {
                    int uncoveredWeight=0;
                    const Coord site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncoveredWeight);
                    if(site.isValid() && uncoveredWeight>0) {
                        itemID=Structure_RocketTurret;
                        crimeServiceSite=site;
                        structureRule="base_air_coverage";
                        skipRemainingStructureLogic=true; // Do not replace this funded coverage slot with optional tech.
                        traceDecision("base_air_coverage",AITelemetry::Record()
                            .set("x",site.x).set("y",site.y).set("uncovered_building_weight",uncoveredWeight)
                            .set("core_coverage_shortfall",coreShortfall).set("coverage_tier",coverageTier)
                            .set("coverage_turret_cap",RocketTurretPolicy::coverageTurretCap(coverageDemand))
                            .set("uncovered_core_assets",uncoveredCoreAssets).set("air_engaged",airEngaged)
                            .set("enemy_aircraft",maxEnemyOrnithopters));
                    }
                }

                // Unlock the first transport before extra ground production.
                // Save the actual factory price; high cash thresholds delayed this
                // until 3-5 heavy factories in 638. Never reserve an impossible site.
                const bool firstTransport = needsFirstTransport();
                const bool firstHighTechSite = firstTransport && itemCount[Structure_HighTechFactory] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
                    && findPlaceLocation(Structure_HighTechFactory).isValid();
                const bool holdExtraHeavy = !fundedCityProduction && (openingWorkersNeeded() || (firstTransport
                    && (firstHighTechSite || carryallBuildAvailable || itemCount[Structure_HighTechFactory]
                        > getHouse()->getNumItems(Structure_HighTechFactory))));
                if (itemID == NONE_ID && !skipRemainingStructureLogic && firstHighTechSite) {
                    skipRemainingStructureLogic = true;
                    structureRule = "save_first_transport_factory";
                    if (money >= data[Structure_HighTechFactory][houseID].price) {
                        itemID = Structure_HighTechFactory;
                        structureRule = "first_transport_factory";
                    }
                }

                // Ongoing economic expansion uses the same comparison after
                // essential services and military infrastructure below.
                // Occupied zones are exactly the buildings that supply rebels to
                // an outbreak, so they are the right denominator. Count the
                // moderate band as well: that is the population still climbing
                // towards the dangerous band and the last chance to prevent it.
                int developedZones = 0, dangerousZones = 0, moderateZones = 0;
                if (isCitySim) {
                    const auto* sim = currentGame->getCitySimulation();
                    if (sim && sim->isInitialized()) {
                        for (const auto* structure : getStructureList()) {
                            const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
                            if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) continue;
                            const Coord p = zone->getLocation();
                            if (!getMap().tileExists(p.x, p.y)
                                || DuneCity::getStructurePopulation(zone,getMap().getTile(p.x,p.y)->getCityZoneDensity()) == 0) continue;
                            ++developedZones;
                            const int zoneCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                            if (zoneCrime >= DuneCity::kCrimeDangerousThreshold) ++dangerousZones;
                            else if (zoneCrime >= 128) ++moderateZones;
                        }
                    }
                }
                const unsigned serviceInterval = QuantBotBuildPolicy::crimeServiceOrderInterval(
                    developedZones, dangerousZones, moderateZones);
                bool serviceEvaluated = false;
                // The scorer used to refuse to even look while the till held
                // less than a station, so a demanded service disappeared from
                // the pass and the yard spent the same cash on a cheap plot —
                // the city then never reached the price and crime ran to an
                // outbreak. Evaluate at the service price, then keep the budget
                // when the forecast can actually reach it inside the horizon.
                const int servicePriceFloor = std::max(data[Structure_PoliceStation][houseID].price,
                    data[Structure_RocketTurret][houseID].price);
                auto selectCrimeService = [&](const char* rule) {
                    if (itemID != NONE_ID || skipRemainingStructureLogic || !isCitySim || serviceEvaluated) return;
                    serviceEvaluated = true;
                    const bool selected = selectCityServiceInvestment(pBuilder,
                        std::max(money, servicePriceFloor),
                        serviceInterval > 0, itemID, crimeServiceSite);
                    const int price = selected ? data[itemID][houseID].price : 0;
                    const bool shortOfPrice = selected && money < price;
                    if (shortOfPrice) {
                        serviceSavingHold = dangerousZones>0 && cashFlow.projectedCash >= price;
                        itemID = NONE_ID;
                        crimeServiceSite = Coord::Invalid();
                        structureRule = serviceSavingHold ? "save_city_service" : structureRule;
                        skipRemainingStructureLogic = serviceSavingHold;
                    } else if (selected) structureRule = rule;
                    traceDecision("crime_service_reservation", AITelemetry::Record()
                        .set("rule",rule).set("interval",int(serviceInterval))
                        .set("developed_zones",developedZones).set("dangerous_zones",dangerousZones)
                        .set("moderate_zones",moderateZones)
                        .set("non_service_orders",int(nonServiceConstructionOrders))
                        .set("selected",selected).set("item",int(itemID)).set("price",price)
                        .set("spendable",money).set("projected_cash",cashFlow.projectedCash)
                        .set("saving",serviceSavingHold));
                };
                // A turret that repays its entire cost through land-value tax
                // alone earns an early investment slot when it also reduces
                // existing crime and improves residential/commercial value.
                // Keep cash for harvesters/refineries and a combat unit; cap the
                // frequency so it cannot monopolise the construction yards.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_HeavyFactory] > 0
                    && serviceInterval != 2 // Widespread dangerous crime still gets the strongest service comparison first.
                    && QuantBotBuildPolicy::crimeServiceOrderDue(3, nonServiceConstructionOrders)
                    && selectCityServiceInvestment(pBuilder,
                        std::max(0,money-cityWorkingReserve), false,
                        itemID, crimeServiceSite, true)) {
                    structureRule = "turret_land_value_investment";
                }
                if (QuantBotBuildPolicy::crimeServiceOrderDue(serviceInterval, nonServiceConstructionOrders))
                    selectCrimeService("city_crime_reserved_order");
				// 2. Refinery (if 0) — non-city-sim path; city sim handles refineries above.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& itemCount[Structure_Refinery] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Refinery)) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
				}

                // Establish repairs before more factories/tech consume the opening
                // grant. Count queued yards to avoid duplicate orders from parallel CYs.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && gameMode == GameMode::Custom && canAddRepairYard(itemCount[Structure_RepairYard])
                    && !openingWorkersNeeded()
                    && itemCount[Structure_RepairYard] < repairBaseline
                    && getHouse()->getNumItems(Structure_Refinery) > 0
                    && money >= data[Structure_RepairYard][houseID].price + 1000
                    && campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && findPlaceLocation(Structure_RepairYard).isValid()) {
                    itemID = Structure_RepairYard;
                    structureRule = "early_repair_capacity";
                }


				// 3. Refinery (ratio: 1 refinery per 3 harvesters) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& !lowSpiceEconomy
					&& itemCount[Structure_Refinery] < (vanillaEconomy
                        ? QuantBotBuildPolicy::desiredSpiceRefineries(spiceHarvesterTarget, itemCount[Unit_Harvester])
                        : itemCount[Unit_Harvester] / 3)
			&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
			&& !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
					}
				// 4. Refinery (< 4, money < 2000) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
				&& !isCitySim
				&& !lowSpiceEconomy
				&& gameMode != GameMode::Campaign
				&& itemCount[Structure_Refinery] < 4
				&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
					&& money < 2000) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
				}
				// City income gate: in city sim mode, defer military infrastructure
				// (StarPort/Radar/LightFactory) until the city has at least the
				// seed economy in place. Uses zone *count*, not population —
				// zones stay at density 0 (and contribute 0 pop) until growth
				// conditions kick in, so a pop-based gate locks military out
				// indefinitely if the AI hasn't laid roads/supply yet.
				constexpr int kCityIncomeReadyZones = 3;
				const int kCityZoneCount = itemCount[Structure_ZoneResidential]
					+ itemCount[Structure_ZoneCommercial]
					+ itemCount[Structure_ZoneIndustrial];
				const bool cityIncomeReady = !isCitySim || kCityZoneCount >= kCityIncomeReadyZones || money > 3000;

                // One light line is enough until the first heavy line exists.
                // Do not mistake an opening's temporary all-light mix for a need
                // to build several light factories before unlocking tanks.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && itemCount[Structure_LightFactory] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
                    && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                    skipRemainingStructureLogic = true;
                    structureRule = "save_first_heavy";
                    if (money >= data[Structure_HeavyFactory][houseID].price) {
                        itemID = Structure_HeavyFactory;
                        structureRule = "first_heavy_before_expansion";
                    }
                }
                // The high-tech factory and IX unlock the units the extra lanes
                // would build, so a wealthy base finishes them before repeating
                // optional factory expansion. Only tech this yard (or another)
                // can actually order counts, so a locked tree never stalls
                // production, and the first factory is never delayed.
                const bool techProgressionPending = customStrategicPlanning
                    && itemCount[Structure_HeavyFactory] > 0
                    && ((itemCount[Structure_HighTechFactory] == 0
                            && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory))
                        || (itemCount[Structure_IX] == 0 && anyConstructionYardCanBuildIX));
                // Wealthy vanilla openings need parallel MCV/troop production
                // before optional infrastructure. First/expanding refineries above
                // keep their priority; queued factories count towards this target.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && vanillaEconomy && gameMode == GameMode::Custom && !holdExtraHeavy
                    && !techProgressionPending
                    && itemCount[Structure_Refinery] > 0
                    && militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached()
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)) {
                    const int target = DuneCity::vanillaFactoryTarget(
                        QuantBotBuildPolicy::desiredHeavyFactories(false, 0, money,
                            getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount()),
                        getHouse()->getNumItems(Unit_Harvester), money);
                    if (DuneCity::prioritizeVanillaFactory(money, itemCount[Structure_HeavyFactory],
                            itemCount[Structure_ConstructionYard], target)
                        && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                        itemID = Structure_HeavyFactory; structureRule = "cash_factory_expansion";
                    }
                }
                // Unlock one defensive yard before repeat zoning can occupy every
                // construction slot. Other yards keep growing the city in parallel.
                // The upgrade is the prerequisite for any emplacement at all, so
                // demanded core coverage buys it once the first refinery pays the
                // bills rather than after the whole opening worker fleet.
                const int rocketUpgrade=data[Structure_RocketTurret][houseID].upgradeLevel;
                if (itemID==NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital
                    && (coreShortfall>0
                        || (!openingWorkersNeeded() && itemCount[Structure_HeavyFactory]>0
                            && itemCount[Structure_RocketTurret]<2))
                    && data[Structure_RocketTurret][houseID].enabled
                    && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel
                    && pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
                    && pBuilder->getMaxUpgradeLevel()>=rocketUpgrade
                    && money>=pBuilder->getUpgradeCost()+data[Structure_ZoneResidential][houseID].price
                    && std::none_of(capitalBuilders.begin(),capitalBuilders.end(),[&](const auto* other) {
                        return other!=pBuilder && other->getItemID()==Structure_ConstructionYard
                            && (other->isUpgrading() || other->getCurrentUpgradeLevel()>=rocketUpgrade);
                    })) {
                    if (pBuilder->getHealth()<pBuilder->getMaxHealth()) doRepair(pBuilder);
                    else if (upgradeWithLogging(__LINE__)) {
                        skipRemainingStructureLogic=true;
                        traceDecision("city_yard_upgrade",AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("accepted",true)
                            .set("reason","unlock_base_defence"));
                    }
                }
                // Demanded core coverage also saves for its own prerequisite:
                // the emplacement, or the yard upgrade that unlocks it. Bounded
                // by the forecast, so a city that cannot reach the price keeps
                // growing instead of idling.
                if (isCitySim && coreShortfall>0 && !serviceSavingHold) {
                    const int upgradeStep=pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
                        && pBuilder->getMaxUpgradeLevel()>=rocketUpgrade ? pBuilder->getUpgradeCost() : 0;
                    const int wanted=upgradeStep>0 ? upgradeStep : data[Structure_RocketTurret][houseID].price;
                    if (money<wanted && cashFlow.projectedCash>=wanted
                        && proactiveSavingHold(wanted)
                        && data[Structure_RocketTurret][houseID].enabled
                        && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel)
                        serviceSavingHold=true;
                }
                if (itemID==NONE_ID && !skipRemainingStructureLogic && sharedSpending && !fundedCityProduction) {
                    const CapitalCandidate* choice=nullptr;
                    for (const auto& candidate:capitalCandidates)
                        if (candidate.builder==pBuilder->getObjectID() && candidate.score>0
                            && (!choice || candidate.score>choice->score)) choice=&candidate;
                    if (choice) {
                        Uint32 candidate=choice->item;
                        // Re-evaluate after earlier yards have committed lots/bays.
                        if (citySimEnabled && std::string(choice->kind)=="economy") candidate=chooseCityEconomy(pBuilder,false);
                        if (candidate!=NONE_ID && (!orderedThisTick.count(candidate)
                            || DuneCity::isCityZoneStructure(candidate))) {
                            itemID=money>=data[candidate][houseID].price ? candidate : NONE_ID;
                            if (itemID!=NONE_ID && choice->site.isValid()) crimeServiceSite=choice->site;
                            // A civic allocation that the till cannot cover yet
                            // keeps its budget; the fallback below used to spend
                            // it on a plot every pass, so the price never came.
                            if (itemID==NONE_ID && std::string(choice->kind)=="civic"
                                && cashFlow.projectedCash>=data[candidate][houseID].price
                                && (choice->reason==std::string("crime_prevention")
                                    || proactiveSavingHold(data[candidate][houseID].price)))
                                serviceSavingHold=true;
                            structureRule=itemID==NONE_ID ? "save_shared_capital" : choice->reason==std::string("crime_prevention")
                                ? "city_crime_prevention" : (choice->reason==std::string("uncovered_base")
                                    || choice->reason==std::string("core_coverage")
                                    || choice->reason==std::string("air_coverage"))
                                ? "base_air_coverage" : "shared_capital_investment";
                            skipRemainingStructureLogic=true;
                        }
                    }
                }
                if (itemID==NONE_ID && !skipRemainingStructureLogic && fundedCityProduction
                    && itemCount[Structure_RepairYard]<repairTarget
                    && canAddRepairYard(itemCount[Structure_RepairYard])
                    && campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && money>=buildingCapitalCost(Structure_RepairYard)+cashBuffer
                    && findPlaceLocation(Structure_RepairYard).isValid()) {
                    itemID=Structure_RepairYard;structureRule="funded_repair_capacity";
                }
                // Fund combat-air capacity before optional ground-factory expansion.
                // Pending factories prevent duplicate lanes across parallel yards.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && airBacklog
                    && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
                    && findPlaceLocation(Structure_HighTechFactory).isValid()) {
                    itemID = Structure_HighTechFactory; structureRule = "air_unit_backlog";
                }
                // Expand saturated light production before optional heavy capacity.
                // Pending factories count, so parallel yards add one lane at a time.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && lightBacklog
                    && (itemCount[Structure_HeavyFactory] > 0
                        || !data[Structure_HeavyFactory][houseID].enabled
                        || data[Structure_HeavyFactory][houseID].techLevel > currentGame->techLevel)
                    && campaignAvailableToBuild(pBuilder,Structure_LightFactory)
                    && findPlaceLocation(Structure_LightFactory).isValid()) {
                    itemID = Structure_LightFactory; structureRule = "light_unit_backlog";
                }
                if (itemID == NONE_ID && !skipRemainingStructureLogic && !holdExtraHeavy && heavyBacklog
                    && itemCount[Structure_HeavyFactory] < 24
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
                    && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                    itemID = Structure_HeavyFactory; structureRule = "heavy_unit_backlog";
                }
				// 5. Starport only if this map offers enabled imports (sold-out entries restock).
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_StarPort] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_StarPort)
					&& findPlaceLocation(Structure_StarPort).isValid()
					&& (gameMode != GameMode::Campaign || money > 1000)
                    && starportMarketAvailable) {
					itemID = Structure_StarPort; structureRule = "starport_supply";
				}
				// 6. Radar
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_Radar] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Radar)
					&& money > 500) {
					itemID = Structure_Radar; structureRule = "radar_prerequisite";
				}
				// 7. Light Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_LightFactory] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_LightFactory)
					&& money > 500) {
					itemID = Structure_LightFactory; structureRule = "light_production";
				}
				// Custom vanilla invests in vehicle production; retain infantry
				// infrastructure for campaign and other-mod build policies.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& itemCount[Structure_Barracks] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Barracks)
					&& money > 400) {
					itemID = Structure_Barracks; structureRule = "infantry_production";
				}
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& itemCount[Structure_WOR] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_WOR)
					&& money > 600) {
					itemID = Structure_WOR; structureRule = "infantry_production";
				}
				// 8. Repair Yard (only if starport or heavy factory exists)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& canAddRepairYard(itemCount[Structure_RepairYard]) && itemCount[Structure_RepairYard] == 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && (!isCitySim || (!openingWorkersNeeded()
                        && money >= data[Structure_RepairYard][houseID].price
                            + data[Structure_ZoneResidential][houseID].price))) {
					itemID = Structure_RepairYard; structureRule = "repair_capacity";
					logDebug("Build Repair Yard... money: %d", money);
				}
				// 8a. Upgrade CY to level 2 for rocket turrets
				//     City sim: upgrade early (no repair yard needed) so turrets protect the colony
				//     Non-city: requires repair yard + starport/heavy factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& pBuilder->getCurrentUpgradeLevel() < 2
                    && (!isCitySim || (!openingWorkersNeeded()
                        && money >= pBuilder->getUpgradeCost() + data[Structure_ZoneResidential][houseID].price))
					&& !pBuilder->isUpgrading()
					&& ((currentGame && currentGame->isCitySimEnabled() && money > 500)
						|| (itemCount[Structure_RepairYard] > 0
							&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
							&& itemCount[Structure_RocketTurret] < 2
                    && (!vanillaEconomy || money > economyReserve + data[Structure_RocketTurret][houseID].price)))) {
					if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
						doRepair(pBuilder);
						logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
					} else if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
                        const int upgradeCost = pBuilder->getUpgradeCost();
                        const bool accepted = upgradeWithLogging(__LINE__);
                        if (accepted) {  skipRemainingStructureLogic = true; }
                        traceDecision("city_yard_upgrade", AITelemetry::Record().set("builder",pBuilder->getObjectID())
                            .set("accepted",accepted).set("cost",upgradeCost).set("spendable",money));
						logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", pBuilder->getCurrentUpgradeLevel() + 1);
					}
				}
                // Windtrap damage does not reduce output; repair for survival
                // uses the ordinary building-repair policy, not power recovery.
				// 8b-pre. Build power for turret buffer — prefer nuclear, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getGameInitSettings().getGameOptions().rocketTurretsNeedPower
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& !hasPowerBufferForTurret()) {
					int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: %d)", powerExcess, rocketPowerBuffer);
					} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("TURRET-POWER: Windtrap for turret buffer (excess: %d, need: %d)", powerExcess, rocketPowerBuffer);
					}
				}
				// 8b. Two baseline rocket turrets after repair yard (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& itemCount[Structure_RocketTurret] < 2
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", itemCount[Structure_RocketTurret] + 1);
				}
				// 8c. Counter enemy ornithopters (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& maxEnemyOrnithopters > 0
                                    && routineRocketsAllowed
					&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
				}
				// 9. Heavy Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HeavyFactory] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
					&& money > 500) {
					itemID = Structure_HeavyFactory; structureRule = "heavy_production";
					logDebug("Build first Heavy Factory... money: %d", money);
				}
				// 10. High Tech Factory (first one - after heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HighTechFactory] == 0
                    && !openingWorkersNeeded()
					&& itemCount[Structure_HeavyFactory] > 0
					&& campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
					&& money > 1000) {
					itemID = Structure_HighTechFactory; structureRule = "air_production";
					logDebug("Build first High Tech Factory... money: %d", money);
				}
                // Buy a prerequisite port at the tech goal's priority, even
                // without imports. Count queued ports across all yards.
				auto starportUnlocks = [&](Uint32 goal) {
					return itemCount[Structure_StarPort] == 0
						&& !orderedThisTick.count(Structure_StarPort)
						&& campaignPermitsStructure(goal)
						&& prerequisiteBlocksBuild(pBuilder,goal,Structure_StarPort)
						&& campaignAvailableToBuild(pBuilder,Structure_StarPort)
						&& money >= data[Structure_StarPort][houseID].price
						&& findPlaceLocation(Structure_StarPort).isValid();
				};
				// 11. House IX (after essential production buildings)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_HighTechFactory] > 0
					&& itemCount[Structure_RepairYard] > 0
					&& campaignAvailableToBuild(pBuilder,Structure_IX)
					&& money > 1000) {
					itemID = Structure_IX; structureRule = "advanced_tech";
					logDebug("Build IX... money: %d", money);
				}
				if (ixOverdue && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& stablePower
					&& money > 1000
					&& campaignAvailableToBuild(pBuilder,Structure_IX)
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_IX; structureRule = "advanced_tech";
				}
                // Missing prerequisites never start the IX overdue timer.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_HighTechFactory] > 0
					&& itemCount[Structure_RepairYard] > 0
					&& money > 1000
					&& starportUnlocks(Structure_IX)) {
					itemID = Structure_StarPort; structureRule = "starport_prerequisite";
					logDebug("Build Starport as House IX prerequisite... money: %d", money);
				}
				// 12. Additional Heavy Factories (expansion).
				//     Income supports steady expansion; large cash surpluses fund
				//     extra tank capacity, bounded while military demand remains.
				//     Non-city uses the existing money/4000 target, also bounded.
				//     City mode uses actual build availability; classic prerequisites remain.
				//     Requirements outside city mode are progressive based on tech level:
				//     Tech 4: No prerequisites (just money and need)
				//     Tech 5-6: Require Repair Yard
				//     Tech 7+: Require Repair Yard + IX
				if (itemID == NONE_ID && !skipRemainingStructureLogic
								&& !holdExtraHeavy && money > std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)) {

								int creditsPerSec = 0;
								if (isCitySim) {
									auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
									int tax = citySim ? citySim->getCityTax() : 7;
									int32_t annual = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths, tax, ownAvgLandValue);
									creditsPerSec = annual / 60;
								}
								int desiredHFs = QuantBotBuildPolicy::desiredHeavyFactories(isCitySim, creditsPerSec, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());

                                if (vanillaEconomy) desiredHFs = DuneCity::vanillaFactoryTarget(desiredHFs, getHouse()->getNumItems(Unit_Harvester), money);
								const bool needMore = itemCount[Structure_HeavyFactory] < desiredHFs
									&& militaryValue < militaryValueLimit && !getHouse()->isGroundUnitLimitReached();

								if (needMore) {
									int techLevel = currentGame ? currentGame->techLevel : 8;
									bool prerequisitesMet = false;

									if (isCitySim || vanillaEconomy || techLevel <= 4) {
										// City and vanilla production use the actual tech tree, not an extra IX policy gate.
										prerequisitesMet = true;
									}
									else if (techLevel <= 6) {
										// Tech 5-6: Require Repair Yard
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1);
									}
									else {
										// Tech 7+: Require both Repair Yard and IX
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1 && itemCount[Structure_IX] >= 1);
									}

									if (prerequisitesMet) {
										itemID = Structure_HeavyFactory; structureRule = "heavy_production";
										logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  desired: %d  tech: %d",
											activeHeavyFactoryCount, getHouse()->getNumItems(Structure_HeavyFactory), money, desiredHFs, techLevel);
									}
								}
							}
				// 13. Vanilla refinery ratio. City mode compares capacity and tax returns.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
						&& !lowSpiceEconomy
						&& ((itemCount[Structure_Refinery] * 3.5_fix < itemCount[Unit_Harvester])
					|| (currentGame && currentGame->techLevel < 4))
						&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
						&& !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
					}
				// 14. Expand repair only when existing capacity is busy and production supports it.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
							&& canAddRepairYard(itemCount[Structure_RepairYard]) && campaignAvailableToBuild(pBuilder,Structure_RepairYard) && money > std::max(2000, economyReserve + data[Structure_RepairYard][houseID].price)
							&& itemCount[Structure_RepairYard]<repairTarget) {
							itemID = Structure_RepairYard; structureRule = "repair_capacity";
							logDebug("Build Repair Yard: have=%d busy=%d cap=%d military=%d", itemCount[Structure_RepairYard], activeRepairYardCount,
								repairTarget, militaryValue);
						}
                selectCrimeService("city_service_investment");
                // Civic turrets use the shared investment comparison above.
				// 17b. Palace (after military infrastructure)
				//       City sim: 1 palace per 30000 population
				{
				const bool palaceAllowed = itemCount[Structure_Palace] < palaceTarget
					&& !orderedThisTick.count(Structure_Palace);
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > 5000
									&& campaignAvailableToBuild(pBuilder,Structure_Palace)
									&& palaceAllowed
									&& itemCount[Structure_HeavyFactory] > 0
									&& itemCount[Structure_LightFactory] > 0) {
								itemID = Structure_Palace; structureRule = "palace_strategy";
							}
				if (palaceOverdue && !skipRemainingStructureLogic
					&& stablePower
					&& money > 5000
					&& campaignAvailableToBuild(pBuilder,Structure_Palace)
					&& palaceAllowed
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_LightFactory] > 0
					&& itemID != Structure_Palace
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_Palace; structureRule = "palace_strategy";
				}
				// Same demand as the palace rules above, for a map whose Palace
				// lists a Starport this base does not own.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& money > 5000
					&& palaceAllowed
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_LightFactory] > 0
					&& starportUnlocks(Structure_Palace)) {
					itemID = Structure_StarPort; structureRule = "starport_prerequisite";
					logDebug("Build Starport as Palace prerequisite... money: %d", money);
				}
				}
				// Round out vanilla bases with regular turrets and short wall lines.
				// Fixed count targets keep this deterministic and prevent defence spam.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
					&& itemCount[Structure_HeavyFactory] > 0 && money > std::max(1000, economyReserve + 1000)
                    && (!vanillaEconomy || itemCount[Unit_Harvester] >= spiceHarvesterTarget)) {
					const int productionBuildings = itemCount[Structure_LightFactory]
						+ itemCount[Structure_HeavyFactory] + itemCount[Structure_Barracks]
						+ itemCount[Structure_WOR];
                    const bool rocketTech = data[Structure_RocketTurret][houseID].enabled
                        && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel;
                    const Uint32 defenceTurret = rocketTech ? Structure_RocketTurret : Structure_GunTurret;
                    const int desiredDefenceTurrets = 1 + productionBuildings / 3;
                    const int desiredWalls = 2 + (itemCount[Structure_GunTurret]
                        + itemCount[Structure_RocketTurret]) * 2;
                    if (itemCount[defenceTurret] < desiredDefenceTurrets
                        && campaignAvailableToBuild(pBuilder,defenceTurret)
                        && (!rocketTech || hasPowerBufferForTurret())
                        && findEffectiveTurretPlaceLocation(defenceTurret).isValid()) {
                        itemID = defenceTurret;
                        structureRule = rocketTech ? "rocket_defense" : "ground_defense";
					} else if (itemCount[Structure_Wall] < desiredWalls
						&& campaignAvailableToBuild(pBuilder,Structure_Wall)
						&& findPlaceLocation(Structure_Wall).isValid()) {
						itemID = Structure_Wall; structureRule = "ground_defense";
					}
				}
				// 18. City zone structures (when city sim is active)
				// Zones are 2x2 structures built via the CY; runZoneGrowth()
				// requires an actual structure object, so tile-flag placement
				// (CMD_CITY_PLACE_ZONE without a structure) does not work.

				// Rank zones by live demand and the R/I/C balance. Try the next
				// candidate if the preferred zone has no available building site.
				// In city sim, zones are the economic base — only windtrap is
				// required so the AI doesn't gate growth behind military
				// infrastructure that itself requires population (e.g. Starport
				// now needs 10000 pop). Outside city sim there's no zone path
				// here at all.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 200
					&& itemCount[Structure_WindTrap] > 0) {
					// Zones consume power as they grow. Before placing one,
					// ensure we have surplus power. If not, build a nuclear
					// plant (or windtrap fallback) first.
					constexpr int kZonePowerHeadroom = 24;  // worst case: industrial L3
					const int powerSurplus = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if (powerSurplus < kZonePowerHeadroom) {
						if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						}
					} else {
						const int resCount = itemCount[Structure_ZoneResidential];
						const int comCount = itemCount[Structure_ZoneCommercial];
						const int indCount = itemCount[Structure_ZoneIndustrial];
						const Uint32 zoneID = chooseCityEconomy(pBuilder, false);

						if (zoneID != NONE_ID && campaignAvailableToBuild(pBuilder,zoneID)
							&& findPlaceLocation(zoneID).isValid()) {
							itemID = zoneID; structureRule = "city_economy";
							logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d surplus=%d)",
								getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
								ownResValve, ownComValve, ownIndValve, powerSurplus);
						}
					}
				}

                // All city power branches share the same investment decision.
                // Include committed consumers, but never change vanilla policy.
                if (isCitySim && (itemID == Structure_NuclearPlant || itemID == Structure_WindTrap)
                    && itemCount[Structure_WindTrap] > 0) {
                    const int need = std::max(1,getHouse()->getPowerRequirement()
                        + cityPowerReserve - getHouse()->getProducedPower());
                    const Coord windSite = campaignAvailableToBuild(pBuilder,Structure_WindTrap)
                        ? findPlaceLocation(Structure_WindTrap) : Coord::Invalid();
                    const Coord nuclearSite = campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)
                        ? findPlaceLocation(Structure_NuclearPlant) : Coord::Invalid();
                    const int windOutput = std::max(1,-data[Structure_WindTrap][houseID].power);
                    const int windNeeded = (need+windOutput-1)/windOutput;
                    const int cash = std::max(0,money);
                    // Once the first windtrap establishes power, prefer a reactor
                    // whenever its real footprint and price are feasible. Extra
                    // windtrap room is no longer a reason to choose less capacity.
                    const bool nuclear = nuclearSite.isValid()
                        && cash >= data[Structure_NuclearPlant][houseID].price;
                    itemID = nuclear ? Structure_NuclearPlant : windSite.isValid() ? Structure_WindTrap : NONE_ID;
                    if (itemID == NONE_ID) skipRemainingStructureLogic = true;
                    traceDecision("city_generator_choice", AITelemetry::Record().set("item",itemID)
                        .set("need",need).set("committed_demand",committedPowerDemand).set("cash",cash)
                        .set("forecast_growth",projectedPowerDemandGrowth).set("forecast_seconds",120)
                        .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
                        .set("zone_growth_headroom",zoneGrowthHeadroom).set("power_shortage",!getHouse()->hasPower())
                        .set("nuclear_available",campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
                        .set("nuclear_placement",placementScoreDetails[Structure_NuclearPlant])
                        .set("nuclear_site",nuclearSite.isValid()).set("nuclear_price",data[Structure_NuclearPlant][houseID].price)
                        .set("wind_needed",windNeeded).set("wind_site",windSite.isValid()).set("nuclear",nuclear)
                        .set("industrial_demand",ownIndValve));
                }
            if (itemID!=NONE_ID && itemID!=Structure_WindTrap
                && campaignPowerNeeded(std::max(0,data[itemID][houseID].power))
                && campaignAvailableToBuild(pBuilder,Structure_WindTrap)) {
                itemID=Structure_WindTrap; structureRule="campaign_planned_power";
                crimeServiceSite=Coord::Invalid();
            }
            if (sharedSpending && !fundedCityProduction
                && (itemID==Structure_LightFactory || itemID==Structure_HeavyFactory || itemID==Structure_HighTechFactory)
                && getHouse()->getNumItems(itemID)>0) {
                const bool fundedLane=std::any_of(capitalCandidates.begin(),capitalCandidates.end(),[&](const auto& c) {
                    return c.builder==pBuilder->getObjectID() && c.item==itemID && c.score>0;
                });
                if (!fundedLane) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("item",itemID).set("rule",structureRule)
                        .set("reason","no_funded_production_bottleneck"));
                    itemID=NONE_ID;
                }
            }
			// Dedup: skip if another CY already ordered this unique structure
			// this tick. Zones and turrets are allowed in multiples.
			if (itemID != NONE_ID) {
				bool isMultiBuild = (itemID == Structure_ZoneResidential
					|| itemID == Structure_ZoneCommercial
					|| itemID == Structure_ZoneIndustrial
					|| itemID == Structure_RocketTurret
					|| itemID == Structure_GunTurret
					|| itemID == Structure_PoliceStation
					|| itemID == Structure_Wall
					|| itemID == Structure_Slab1);
				if (!isMultiBuild && orderedThisTick.count(itemID)) {
					logDebug("DEDUP: Skipping %s — already ordered by another CY this tick",
						getItemNameByID(itemID).c_str());
					if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "deduplicated"));
				itemID = NONE_ID;
				crimeServiceSite = Coord::Invalid();
				}
			}

			// Retry useful economic work when another yard claimed the strategic
			// order, or a chosen structure has no feasible footprint. Never
			// default to residential regardless of demand or power.
			if (itemID != NONE_ID && itemID != Structure_RocketTurret
				&& itemID != Structure_GunTurret && itemID != Structure_PoliceStation
                && !findPlaceLocation(itemID).isValid()) {
				if (emitStatsLog) logDebug("PRODUCTION: CY=%u no site for item=%u", pBuilder->getObjectID(), itemID);
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "no_site"));
				itemID = NONE_ID;
			}
            if (isCitySim && itemID != NONE_ID
                && (structureRule == std::string("city_economy") || structureRule == std::string("city_economy_fallback"))
                && money < data[itemID][houseID].price) {
                itemID=NONE_ID;
                skipRemainingStructureLogic=true;
            }
            if ((vanillaEconomy || isCitySim) && itemID != NONE_ID && money < data[itemID][houseID].price
                && !(isCitySim && !getHouse()->hasPower()
                    && (itemID == Structure_WindTrap || itemID == Structure_NuclearPlant))) {
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "committed_cash")
                    .set("spendable", money).set("queued_production_cost", queuedProductionCost));
                itemID = NONE_ID;
            }
            // Paid imports must still be delivered. If they fill the last slots,
            // wait before adding a refinery that could spawn another worker first.
            // Once the fleet has arrived, capacity-only refineries are safe again.
            if (itemID==Structure_Refinery && engineHarvesterLimit>0
                && actualHarvesters<engineHarvesterLimit
                && itemCount[Unit_Harvester]+itemCount[Unit_RebelHarvester]>=engineHarvesterLimit) {
                traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("item",itemID)
                    .set("rule",structureRule).set("reason","pending_worker_capacity"));
                itemID=NONE_ID;
            }
            if (isCitySim && DuneCity::isCityZoneStructure(itemID)
                && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24) {
                traceDecision("construction_rejected",AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("item",itemID)
                    .set("rule",structureRule).set("reason","city_growth_power_headroom"));
                itemID=NONE_ID;
                skipRemainingStructureLogic=true;
            }
            if (itemID==Structure_RocketTurret && !routineRocketsAllowed) {
                itemID=NONE_ID;
                crimeServiceSite=Coord::Invalid();
                structureRule="core_infrastructure_before_more_rockets";
            }
			if (emitStatsLog) logDebug("BUILD-CHOICE: CY=%u item=%u credits=%d skip=%d",
				pBuilder->getObjectID(), itemID, money, skipRemainingStructureLogic);
            // A later power/tech override cannot reuse a service's 1x1 site.
            if (itemID != Structure_RocketTurret && itemID != Structure_PoliceStation)
                crimeServiceSite = Coord::Invalid();
			Coord selectedPlaceLocation = Coord::Invalid();
			if (itemID != NONE_ID && campaignAvailableToBuild(pBuilder,itemID)) {
                if (crimeServiceSite.isValid()) selectedPlaceLocation = crimeServiceSite;
                else if (itemID == Structure_PoliceStation) {
                    // Legacy demand/emergency rules use the same bounded service
                    // scorer; no alternate full-map search bypasses its overlap costs.
                    Uint32 service = NONE_ID;
                    selectCityServiceInvestment(pBuilder,money,true,service,selectedPlaceLocation,false,itemID);
                } else selectedPlaceLocation = (itemID == Structure_RocketTurret || itemID == Structure_GunTurret)
                    ? findEffectiveTurretPlaceLocation(itemID) : findPlaceLocation(itemID);
			}

            // An idle yard does not wait for a dearer project while it can
            // afford a demanded plot. This also covers deduplication, failed
            // placement and saving rules that set skipRemainingStructureLogic.
            int selectedCost=itemID!=NONE_ID ? purchasePrice(pBuilder,itemID) : 0;
            if (selectedPlaceLocation.isValid())
                for (const auto& foundation:foundationOrders(pBuilder,itemID,selectedPlaceLocation))
                    selectedCost+=purchasePrice(pBuilder,foundation.item);
            if (isCitySim && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                && pBuilder->getProductionQueueSize()==0
                && !(protectionCapital && capitalPending())
                && !serviceSavingHold
                && (!selectedPlaceLocation.isValid() || money<selectedCost)) {
                const Uint32 zone=affordableCityZone(pBuilder,money);
                if (zone!=NONE_ID) {
                    itemID=zone;selectedPlaceLocation=findPlaceLocation(zone);
                    crimeServiceSite=Coord::Invalid();structureRule="idle_city_growth";
                    skipRemainingStructureLogic=false;
                }
            }
            if (serviceSavingHold && itemID==NONE_ID)
                traceDecision("city_service_saving",AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("rule",structureRule)
                    .set("spendable",money).set("projected_cash",cashFlow.projectedCash));

            if (AITelemetry::log().enabled() && (itemID != NONE_ID || emitStatsLog)) {
                AITelemetry::Record site;
                site.set("valid", selectedPlaceLocation.isValid()).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y);
                if (selectedPlaceLocation.isValid() && currentGame->getCitySimulation()) {
                    auto* sim = currentGame->getCitySimulation();
                    const int x = selectedPlaceLocation.x, y = selectedPlaceLocation.y;
                    const auto size = getStructureSize(itemID);
                    const auto roads = cityRoadImpact(getMap(), x, y, size.x, size.y, itemID);
                    if (placementCache.count(itemID) && placementCache[itemID] == selectedPlaceLocation)
                        site.set("placement_quality", placementScoreDetails[itemID]);
                    site.set("road_connections_preserved", roads.preservesConnections)
                        .set("roads_covered", roads.roadsCovered).set("redundant_roads_reused",roads.redundantRoadsCovered).set("junction_bonus", roads.junctionBonus);
                    site.set("pollution", sim->getPollutionDensityMap().worldGet(x, y))
                        .set("crime", sim->getCrimeRateMap().worldGet(x, y))
                        .set("traffic", sim->getTrafficDensityMap().worldGet(x, y))
                        .set("land_value", sim->getLandValueMap().worldGet(x, y));
                }
                traceDecision("construction_selection", AITelemetry::Record().set("capital_plan",capitalDecision).set("builder", pBuilder->getObjectID())
                    .set("item", itemID).set("rule", structureRule).set("skip", skipRemainingStructureLogic)
                    .set("state", decisionState()).set("site", site));
            }

			if (selectedPlaceLocation.isValid()) {
                // Plan redevelopment against the ground after its zones are
                // removed. Buildings consumed their old foundations at placement,
                // so replacement buildings need newly queued slabs as well.
				std::vector<Uint32> zonesToRemove;
                const bool redevelop = redevelopmentZones(itemID, selectedPlaceLocation, zonesToRemove);
                const auto plan = foundationPlan(pBuilder,itemID,selectedPlaceLocation,zonesToRemove);
                if (!plan.complete) {
                    traceDecision("construction_rejected",AITelemetry::Record()
                        .set("builder",planningBuilder).set("item",itemID).set("rule",structureRule)
                        .set("reason","foundation_incomplete")
                        .set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y));
                    continue;
                }
                const auto& foundations=plan.orders;
                int foundationCost=0;
                for (const auto& foundation:foundations) foundationCost+=purchasePrice(pBuilder,foundation.item);
                if (!foundations.empty() && money < purchasePrice(pBuilder,itemID)+foundationCost) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",planningBuilder).set("item",itemID).set("reason","foundation_budget")
                        .set("price",purchasePrice(pBuilder,itemID)).set("foundation_cost",foundationCost).set("spendable",money));
                    continue;
                }
                size_t foundationsQueued=0;
                auto cancelFoundations=[&]() {
                    for (size_t i=0;i<foundationsQueued;++i)
                        doCancelItem(pBuilder,foundations[i].item);
                    placeLocations.clear();
                };
                for (const auto& foundation : foundations) {
                    if (!produceItemWithLogging(foundation.item,__LINE__,"building_foundation")) break;
                    placeLocations.push_back(Coord(foundation.x,foundation.y));
                    ++foundationsQueued;
                }
                if (foundationsQueued!=foundations.size()) {
                    cancelFoundations();
                    continue;
                }
                if (!foundations.empty()) placeLocations.push_back(selectedPlaceLocation);

				if (produceItemWithLogging(itemID, __LINE__, structureRule)) {
                    if (isCitySim && itemID != Structure_Road && itemID != Structure_Slab1
                        && itemID != Structure_Slab4) {
                        if (crimeServiceSite.isValid()) nonServiceConstructionOrders = 0;
                        else nonServiceConstructionOrders = std::min<Uint32>(6, nonServiceConstructionOrders + 1);
                    }
                    if (redevelop) {
                        AITelemetry::Record removed;
                        for (Uint32 id : zonesToRemove) {
                            auto* zone=dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                            if (!zone || zone->getOwner()!=getHouse()) continue;
                            const Coord z=zone->getLocation();
                            const auto* sim=currentGame->getCitySimulation();
                            const auto& state=sim->getHouseState(getHouse()->getHouseID());
                            const int demand=zone->getItemID()==Structure_ZoneResidential ? state.resValve
                                : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                            removed.set(std::to_string(id),AITelemetry::Record().set("item",zone->getItemID())
                                .set("demand",demand).set("land_value",sim->getLandValueMap().worldGet(z.x,z.y))
                                .set("density",getMap().getTile(z.x,z.y)->getCityZoneDensity()));
                            itemCount[zone->getItemID()]--;
                            zone->demolish();
                        }
                        traceDecision("redevelopment_committed", AITelemetry::Record().set("builder",planningBuilder)
                            .set("item",itemID).set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y)
                            .set("removed_zones",removed));
                    }
                    if (itemID==Structure_Refinery && (engineHarvesterLimit==0
                        || itemCount[Unit_Harvester]+itemCount[Unit_RebelHarvester]<engineHarvesterLimit))
                        ++itemCount[Unit_Harvester];
					reservedStructures[planningBuilder] = {itemID, selectedPlaceLocation};
                    if (placeLocations.empty()) placeLocations.push_back(selectedPlaceLocation);
                    traceDecision("site_reserved", AITelemetry::Record().set("builder", planningBuilder)
                        .set("item", itemID).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y));
                    orderedThisTick.insert(itemID);
                    itemCount[itemID]++;

					if (itemID == strategicReserveItem) strategicReserveCost = 0;
					clearPlacementCache();
				} else {
					cancelFoundations();
				}
			}
			else if (itemID != NONE_ID && campaignAvailableToBuild(pBuilder,itemID)) {
				// Extend the buildable area with concrete for anything QuantBot
				// would have founded anyway; the same policy decides both, so a
				// building never waits for ground it would not have prepared.
				bool needsConcreteExpansion = getGameInitSettings().getGameOptions().concreteRequired
					&& QuantBotBuildPolicy::foundationRequiredForItem(itemID);

				if (needsConcreteExpansion && campaignAvailableToBuild(pBuilder,Structure_Slab1)) {
					Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
					if (slabLocation.isValid()) {
						produceItemWithLogging(Structure_Slab1,__LINE__,"concrete_expansion");
						logDebug("Building concrete slab to expand buildable area for itemID %d at (%d,%d)", itemID, slabLocation.x, slabLocation.y);
					} else {
					// Cannot place slab - silenced (too spammy)
					}
				} else {
					logDebug("Cannot build itemID %d: no place to build and slabs not available", itemID);
				}
			}
		else if (itemID != NONE_ID && !campaignAvailableToBuild(pBuilder,itemID)) {
			logDebug("Cannot build itemID %d: not available (prerequisites not met)", itemID);
		}
		else if (itemID == NONE_ID && !skipRemainingStructureLogic && emitStatsLog) {
			logDebug("No structure selected to build (money: %d, skipRemaining: %d)", money, skipRemainingStructureLogic);
		}

        // Only use spare yard capacity, after all strategic/city choices.
        if (isCitySim && itemID == NONE_ID && !skipRemainingStructureLogic
            && !roadMaintenanceAttempted && !pBuilder->isUpgrading()
            && pBuilder->getProductionQueueSize() == 0 && campaignAvailableToBuild(pBuilder,Structure_Road)) {
            roadMaintenanceAttempted = true;
            const int roadPrice = std::max(1,data[Structure_Road][houseID].price);
            const int repaired = queueCityRoadRepairs(pBuilder,std::min(8,std::max(0,money-economyReserve)/roadPrice));
            money -= repaired*roadPrice;
            capitalOrderedCost += repaired*roadPrice;
            if (repaired) traceDecision("capital_road_batch",AITelemetry::Record().set("capital_plan",capitalDecision)
                .set("builder",planningBuilder).set("count",repaired).set("price",roadPrice).set("cost",repaired*roadPrice));
        }

		// City yards use the demand-ranked fallback before placement above.
		// Outside city mode an otherwise idle yard can extend concrete, but
		// only where concrete means something.
		if (!isCitySim && getGameInitSettings().getGameOptions().concreteRequired
			&& !skipRemainingStructureLogic && money > 500 && pBuilder->getProductionQueueSize() < 1
			&& itemID == NONE_ID && campaignAvailableToBuild(pBuilder,Structure_Slab1)) {
			Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
			if (slabLocation.isValid()) produceItemWithLogging(Structure_Slab1,__LINE__,"concrete_expansion");
		}

						}
					}

				if (pBuilder->isWaitingToPlace()) {
					Uint32 itemToBePlaced = pBuilder->getCurrentProducedItem();
					logDebug("PRODUCTION: CY waiting to place itemID: %d, credits: %d, queued locations: %zu", itemToBePlaced, money, placeLocations.size());
					Coord location;
					bool placementIssueHandled = false;
                    auto tracePlacementIssue = [&](const char* event, const char* reason, const Coord& site) {
                        if (!AITelemetry::log().enabled()) return;
                        AITelemetry::Record tiles;
                        const Coord size = getStructureSize(itemToBePlaced);
                        if (site.isValid()) for (int dx = 0; dx < size.x; ++dx) for (int dy = 0; dy < size.y; ++dy) {
                            const int x = site.x + dx, y = site.y + dy;
                            const Tile* tile = getMap().tileExists(x,y) ? getMap().getTile(x, y) : nullptr;
                            AITelemetry::Record state;
                            state.set("x", x).set("y", y).set("exists", tile != nullptr);
                            if (tile) {
                                state.set("terrain", tile->getType()).set("blocked", tile->isBlocked())
                                    .set("zone", static_cast<int>(tile->getCityZoneType())).set("road", tile->isRoad())
                                    .set("in_build_range", getMap().isWithinBuildRange(x, y, getHouse()));
                                if (const auto* object = tile->getGroundObject())
                                    state.set("occupant", object->getObjectID()).set("occupant_item", object->getItemID())
                                        .set("occupant_structure", object->isAStructure());
                            }
                            tiles.set(std::to_string(dx) + "," + std::to_string(dy), state);
                        }
                        traceDecision(event, AITelemetry::Record().set("builder", pConstYard->getObjectID())
                            .set("item", itemToBePlaced).set("reason", reason).set("x", site.x).set("y", site.y)
                            .set("reserved_overlap", site.isValid() && overlapsReservedStructure(site.x, site.y, size.x, size.y))
                            .set("recent_loss_nearby", site.isValid() && nearRecentStructureLoss(site.x, site.y, size.x, size.y))
                            .set("enemy_fire_risk", dangerAt(site,size)).set("recent_loss_risk", dangerAt(site,size,true))
                            .set("reactor_clearance", site.isValid() && reactorClearance(itemToBePlaced,site))
                            .set("placement_quality",placementScoreDetails[itemToBePlaced]).set("planned_locations_remaining", placeLocations.size()).set("tiles", tiles));
                    };


                    // House::placeStructure paves whatever tile it is given and
                    // clears its road flag, so the only guard a slab has is this
                    // one. Rock only, nothing standing on it, no road erased.
                    const bool placingSlab = itemToBePlaced == Structure_Slab1
                        || itemToBePlaced == Structure_Slab4;
                    auto plannedSlabLegal = [&](Uint32 slab, const Coord& site) {
                        const Coord span = getStructureSize(slab);
                        if (!site.isValid()) return false;
                        for (int dx = 0; dx < span.x; ++dx) for (int dy = 0; dy < span.y; ++dy) {
                            const int x = site.x + dx, y = site.y + dy;
                            if (!getMap().tileExists(x, y)) return false;
                            const Tile* tile = getMap().getTile(x, y);
                            if (tile->isRoad() || !tile->isRock() || tile->isMountain()
                                || tile->isBlocked() || tile->hasCityZone()) return false;
                        }
                        return getMap().okayToPlaceStructure(site.x, site.y, span.x, span.y,
                            false, getHouse(), false, slab);
                    };

					// Check if we have a pre-stored location (from concrete pre-placement)
					if (!placeLocations.empty()) {
						location = placeLocations.front();
						Coord itemsize = getStructureSize(itemToBePlaced);

						// Verify the location is still valid
						if ((!placingSlab || plannedSlabLegal(itemToBePlaced, location))
                            && getMap().okayToPlaceStructure(location.x, location.y, itemsize.x, itemsize.y, false, getHouse(), false, itemToBePlaced)
                            && (itemToBePlaced != Structure_Road || !getMap().getTile(location.x,location.y)->isRoadConnection())
                            && cityRoadImpact(getMap(), location.x, location.y, itemsize.x, itemsize.y, itemToBePlaced).preservesConnections
                            && !overlapsReservedStructure(location.x, location.y, itemsize.x, itemsize.y)
                            && preservesGroundAccess(itemToBePlaced,location)
                            && (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret
                                || itemToBePlaced == Structure_Wall || itemToBePlaced == Structure_Road
                                || itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4
                                || (!nearRecentStructureLoss(location.x, location.y, itemsize.x, itemsize.y)
                                    && dangerAt(location,itemsize) == 0 && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,location))))) {
							placeLocations.pop_front();
							logDebug("PRODUCTION: Using pre-stored location (%d,%d) for itemID: %d", location.x, location.y, itemToBePlaced);
						} else if (itemToBePlaced == Structure_Road) {
                            const Coord oldSite = location;
                            location = Coord::Invalid();
                            // Rate-limit unsuccessful searches by yard; a finished
                            // road remains ready and can be used when a gap opens.
                            if (!roadMaintenanceAttempted) {
                                roadMaintenanceAttempted = true;
                                location = findFinishedRoadSite(pBuilder);
                            }
                            if (location.isValid()) {
                                placeLocations.pop_front();
                                tracePlacementIssue("placement_replan","road_redirected_to_gap",location);
                            } else if (emitStatsLog) {
                                tracePlacementIssue("placement_deferred","road_waiting_for_useful_gap",oldSite);
                            }
                            placementIssueHandled = true;
                        } else if (placingSlab) {
                            // A foundation slab is never dropped on its own: that
                            // is exactly how a building ends up on bare ground.
                            // Re-plan the reserved building's foundation against
                            // the map as it is now and use the first order of this
                            // kind. Concrete the ground no longer needs is
                            // refunded while the reserved building keeps its
                            // queue position and can use incomplete foundations.
                            const Coord oldSite = location;
                            location = Coord::Invalid();
                            const auto reserved = reservedStructures.find(planningBuilder);
                            QuantBotFoundationPolicy::Plan replan;
                            replan.complete = true;
                            if (reserved != reservedStructures.end())
                                replan = foundationPlan(pBuilder, reserved->second.item, reserved->second.location);
                            for (const auto& order : replan.orders)
                                if (order.item == itemToBePlaced
                                    && plannedSlabLegal(order.item, Coord(order.x, order.y))) {
                                    location = Coord(order.x, order.y);
                                    break;
                                }
                            if (location.isValid()) {
                                placeLocations.pop_front();
                                tracePlacementIssue("placement_replan", "foundation_replanned", location);
                            } else if (replan.complete && replan.orders.empty()) {
                                tracePlacementIssue("placement_cancel", "foundation_already_prepared", oldSite);
                                doCancelItem(pConstYard, itemToBePlaced);
                                placeLocations.pop_front();
                            } else {
                                // The ground can no longer take this slab. Refund
                                // the concrete only: the building behind it is
                                // finished or paid for and keeps both its queue
                                // place and its planned site. Placing it on a
                                // partly bare footprint incurs normal foundation
                                // damage instead of wasting its construction time.
                                tracePlacementIssue("placement_cancel", "foundation_unavailable", oldSite);
                                doCancelItem(pConstYard, itemToBePlaced);
                                placeLocations.pop_front();
                                clearPlacementCache();
                            }
							placementIssueHandled = true;
						} else {
                            // Try another legal site if this one was blocked. The
                            // final foundation check below still applies to that site.
                            const Coord oldSite = location;
                            placeLocations.pop_front();
                            placementCache.erase(itemToBePlaced);
                            location = (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret)
                                ? findEffectiveTurretPlaceLocation(itemToBePlaced) : findPlaceLocation(itemToBePlaced);
                            if (location.isInvalid()) {
                                placeLocations.push_front(oldSite);
                                if (emitStatsLog) tracePlacementIssue("placement_deferred", "planned_site_blocked", oldSite);
                            } else {
                                tracePlacementIssue("placement_replan", "planned_site_blocked", oldSite);
                            }
                            placementIssueHandled = true; // Suppress the no-dynamic-site branch below.
                        }
					} else {
						// No pre-stored location, find one dynamically
					if (itemToBePlaced == Structure_Road) {
                            location = Coord::Invalid();
                            if (!roadMaintenanceAttempted) {
                                roadMaintenanceAttempted = true;
                                location = findFinishedRoadSite(pBuilder);
                            }
                        } else if (placingSlab) {
						// For concrete slabs, use specialized slab placement method.
						// It is still held to the same ground rule as a planned one.
						location = findSlabPlaceLocation(itemToBePlaced);
						if (!plannedSlabLegal(itemToBePlaced, location)) location = Coord::Invalid();
					} else if (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret) {
						// For turrets, try city placement first (near crime hotspots),
						// falling back to normal perimeter placement
						location = findEffectiveTurretPlaceLocation(itemToBePlaced);
					} else {
						// For other structures, use normal method that favors adjacency
						location = findPlaceLocation(itemToBePlaced);
						}
					}

                        // Only windtraps may use an exposed emergency power site.
                        // A reactor under known fire risks another blackout and
                        // collateral damage; normal placement must find it safety.
                        if (location.isInvalid() && itemToBePlaced == Structure_WindTrap) {
                            const Coord size = getStructureSize(itemToBePlaced);
                            int bestRisk = std::numeric_limits<int>::max();
                            int bestDistance = std::numeric_limits<int>::max();
                            for (int x=0; x<=getMap().getSizeX()-size.x; ++x) {
                                for (int y=0; y<=getMap().getSizeY()-size.y; ++y) {
                                    const Coord site(x,y);
                                    if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,itemToBePlaced)
                                        || overlapsReservedStructure(x,y,size.x,size.y)
                                        || !preservesGroundAccess(itemToBePlaced,site)
                                        || !TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,site))
                                        || !cityRoadImpact(getMap(),x,y,size.x,size.y,itemToBePlaced).preservesConnections
                                        || (currentGame->isCitySimEnabled()
                                            && wouldLandlockNeighbouringZone(getMap(),houseID,x,y,size.x,size.y))) continue;
                                    const int risk = dangerAt(site,size)
                                        + (nearRecentStructureLoss(x,y,size.x,size.y) ? 1000 : 0);
                                    const Coord yard = pConstYard->getLocation();
                                    const int distance = std::abs(x-yard.x)+std::abs(y-yard.y);
                                    if (risk < bestRisk || (risk == bestRisk && distance < bestDistance)) {
                                        bestRisk = risk; bestDistance = distance; location = site;
                                    }
                                }
                            }
                            if (location.isValid()) {
                                placeLocations.clear();
                                traceDecision("placement_power_recovery", AITelemetry::Record()
                                    .set("builder",planningBuilder).set("item",itemToBePlaced)
                                    .set("x",location.x).set("y",location.y).set("risk",bestRisk));
                            }
                        }
                        if (location.isInvalid() && itemToBePlaced==Structure_NuclearPlant) {
                            const Coord size=getStructureSize(itemToBePlaced);
                            bool potentialSite=false;
                            for (int y=0;y<=getMap().getSizeY()-size.y && !potentialSite;++y)
                                for (int x=0;x<=getMap().getSizeX()-size.x && !potentialSite;++x) {
                                    if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),true,itemToBePlaced)
                                        || overlapsReservedStructure(x,y,size.x,size.y)
                                        || dangerAt(Coord(x,y),size)>0) continue;
                                    bool structure=false;
                                    for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx) {
                                        const auto* object=getMap().getTile(x+dx,y+dy)->getGroundObject();
                                        structure |= object && object->isAStructure();
                                    }
                                    potentialSite=!structure;
                                }
                            if (!potentialSite) {
                                // Refund through the ordinary production API. The
                                // next planning pass can buy a smaller windtrap.
                                tracePlacementIssue("placement_cancel","no_safe_nuclear_footprint",Coord::Invalid());
                                doCancelItem(pConstYard,itemToBePlaced);
                                placeLocations.clear();
                                reservedStructures.erase(planningBuilder);
                                --itemCount[itemToBePlaced];
                                clearPlacementCache();
                                placementIssueHandled=true;
                            }
                        }
                        if (location.isValid() && !preservesGroundAccess(itemToBePlaced,location)) {
                            tracePlacementIssue("placement_deferred", "ground_exit_blocked", location);
                            location=Coord::Invalid();
                            placementIssueHandled=true;
                        }
                        // Last gate before the ground is committed. Foundations
                        // are still planned and ordered before every building,
                        // but once a building is finished, cancelling it because
                        // its slabs were lost throws away the whole building and
                        // the yard time that made it: forty-eight were refunded
                        // this way in one match, three of them inside the first
                        // ten minutes. Prefer an equally good prepared footprint
                        // nearby, and otherwise place it where it was planned and
                        // let the engine apply its own foundation damage.
                        if (location.isValid() && !placingSlab
                            && getGameInitSettings().getGameOptions().concreteRequired
                            && QuantBotBuildPolicy::foundationRequiredForItem(itemToBePlaced)) {
                            const Coord span=getStructureSize(itemToBePlaced);
                            auto footprintPrepared=[&](const Coord& site) {
                                for (int dx=0;dx<span.x;++dx)
                                    for (int dy=0;dy<span.y;++dy) {
                                        const int x=site.x+dx, y=site.y+dy;
                                        if (!getMap().tileExists(x,y)
                                            || !getMap().getTile(x,y)->hasPreparedFoundation()) return false;
                                    }
                                return true;
                            };
                            if (!footprintPrepared(location)) {
                                // Same legality the planned site had to satisfy:
                                // real engine placement, no reservation overlap,
                                // ground access, roads and neighbouring lots.
                                auto usableSite=[&](const Coord& site) {
                                    return footprintPrepared(site)
                                        && getMap().okayToPlaceStructure(site.x,site.y,span.x,span.y,
                                            false,getHouse(),false,itemToBePlaced)
                                        && !overlapsReservedStructure(site.x,site.y,span.x,span.y)
                                        && preservesGroundAccess(itemToBePlaced,site)
                                        && (itemToBePlaced==Structure_RocketTurret || itemToBePlaced==Structure_GunTurret
                                            || (!nearRecentStructureLoss(site.x,site.y,span.x,span.y)
                                                && dangerAt(site,span)==0
                                                && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,
                                                    reactorClearance(itemToBePlaced,site))))
                                        && cityRoadImpact(getMap(),site.x,site.y,span.x,span.y,itemToBePlaced).preservesConnections
                                        && !(currentGame->isCitySimEnabled()
                                            && wouldLandlockNeighbouringZone(getMap(),houseID,site.x,site.y,span.x,span.y));
                                };
                                constexpr int preparedSearchRadius=8;
                                Coord prepared=Coord::Invalid();
                                int bestDistance=std::numeric_limits<int>::max();
                                for (int y=location.y-preparedSearchRadius;y<=location.y+preparedSearchRadius;++y)
                                    for (int x=location.x-preparedSearchRadius;x<=location.x+preparedSearchRadius;++x) {
                                        const Coord site(x,y);
                                        const int distance=std::abs(x-location.x)+std::abs(y-location.y);
                                        if (distance>=bestDistance || !usableSite(site)) continue;
                                        bestDistance=distance; prepared=site;
                                    }
                                if (prepared.isValid()) {
                                    tracePlacementIssue("placement_replan","prepared_footprint_preferred",prepared);
                                    location=prepared;
                                } else {
                                    // Never a cancellation, and never a perpetual
                                    // block: the building goes up on the ground
                                    // it was planned for.
                                    tracePlacementIssue("placement_unprepared","footprint_not_prepared",location);
                                }
                            }
                        }
						if (location.isValid()) {
							traceDecision("placement_request", AITelemetry::Record().set("builder", pConstYard->getObjectID())
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y));
                            const bool placed = doPlaceStructure(pConstYard, location.x, location.y);
                            // A road retry keeps its place ahead of the remaining
                            // queue; do not accidentally consume the next plan.
                            if (!placed && itemToBePlaced == Structure_Road) placeLocations.push_front(location);
                            traceDecision("placement_result", AITelemetry::Record().set("builder", planningBuilder)
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y).set("success", placed));
                            if (itemToBePlaced != Structure_Slab1 && itemToBePlaced != Structure_Slab4) {
                                if (placed) reservedStructures.erase(planningBuilder);
                                else { reservedStructures[planningBuilder] = {itemToBePlaced, location};
                                    if (placeLocations.empty()) placeLocations.push_front(location); }
                            }
                            clearPlacementCache();
							logDebug("PRODUCTION: Placed structure itemID: %d at (%d,%d)", itemToBePlaced, location.x, location.y);
						}
						else if (!placementIssueHandled) {
							if (emitStatsLog) logDebug("PRODUCTION: Holding finished item %d until a legal site is available; search=%s",
                                itemToBePlaced, placementScoreDetails[itemToBePlaced].json().c_str());
							if (emitStatsLog) tracePlacementIssue("placement_deferred", "no_dynamic_site", location);
						}
					}
                    // Pre-plan queue=0 often means a yard is about to receive
                    // an order, not that it stayed idle. Record the outcome too.
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        const char* result = pBuilder->isUpgrading() ? "upgrading"
                            : pBuilder->getProductionQueueSize() > 0 ? "queued"
                            : getHouse()->getCredits() <= 100 ? "insufficient_cash"
                            : pBuilder->getBuildListSize() == 0 ? "no_build_options"
                            : "empty_after_planning";
                        traceDecision("yard_planning_result", AITelemetry::Record()
                            .set("builder", pBuilder->getObjectID()).set("result", result)
                            .set("queue", pBuilder->getProductionQueueSize())
                            .set("upgrading", pBuilder->isUpgrading()).set("hold", pBuilder->isOnHold())
                            .set("state", decisionState()));
                    }
				} break;
				}
			}
		}
	}

    if (!supportMode && AITelemetry::log().enabled()) {
        traceDecision("capital_outcome",AITelemetry::Record().set("plan",capitalDecision)
            .set("priority_ordered",capitalConsumed).set("priority_pending",capitalPending())
            .set("orders",capitalOrders).set("ordered_cost",capitalOrderedCost).set("remaining_planning_cash",money));
    }
	// MULTIPLAYER FIX: Use deterministic timer instead of random
	buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
}


void QuantBot::scrambleUnitsAndDefend(const ObjectBase* intruder, bool clearingSpice, const ObjectBase* protectedAsset) {
    AITelemetry::PerformanceScope perfScope("ai.scrambleUnitsAndDefend", getGameCycleCount(), getHouse()->getHouseID());
    if (supportMode || !intruder || intruder->getHealth() <= 0
        || intruder->getOwner()->getTeamID() == getHouse()->getTeamID()) return;
    const Coord contact = intruder->getLocation();
    const auto* victim = intruder->isAUnit() ? static_cast<const UnitBase*>(intruder)->getTarget() : nullptr;
    const bool baseAttack = !clearingSpice && victim && victim->isAStructure()
        && victim->getOwner() == getHouse();
    if (!getMap().tileExists(contact)) return;
    if (isCampaignEnemy() && !campaignLocalContact(intruder)) return;
    // Debounce volleys by local district, independently for air and ground.
    // This state is saved: neither rendering speed nor telemetry affects orders.
    const Uint32 key = ((contact.y / 8) * ((getMap().getSizeX()+7)/8) + contact.x/8) * 2
        + (intruder->isAFlyingUnit() ? 1 : 0);
    const Uint32 now = getGameCycleCount();
    auto previous = defenceResponseCycles.find(key);
    if (previous != defenceResponseCycles.end() && now-previous->second < MILLI2CYCLES(clearingSpice ? 5000 : 2000)) return;
    defenceResponseCycles[key] = now;
    const auto isProtectedAsset=[&](const ObjectBase* object) {
        return object && object->getOwner()==getHouse()
            && (object->isAStructure() || object->getItemID()==Unit_Harvester
                || object->getItemID()==Unit_RebelHarvester);
    };
    if (!clearingSpice && (isProtectedAsset(protectedAsset) || isProtectedAsset(victim))) {
        const auto& config=getQuantBotConfig();
        tryLaunchOrnithopterStrike(config.getSettings(static_cast<int>(difficulty)),config,
            isProtectedAsset(protectedAsset) ? intruder : nullptr,
            isProtectedAsset(protectedAsset) && protectedAsset->isAStructure());
    }
    // Aircraft over one of our buildings are an anti-air problem, not a
    // reinforcement problem. The proportional response below would send
    // whatever is nearest at the aircraft's current tile, which a ground unit
    // may not be able to stand on and which the engine drops as a target the
    // moment it is out of weapon range. Hand the whole contact to the rescue.
    const bool airOnBuilding = !clearingSpice && intruder->isAFlyingUnit()
        && ((isProtectedAsset(protectedAsset) && protectedAsset->isAStructure())
            || (isProtectedAsset(victim) && victim->isAStructure()));
    if (airOnBuilding) {
        defendStructuresFromAircraft();
        return;
    }
    auto value = [&](const ObjectBase* object) {
        const int price = currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
        return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
    };
    int threatValue = value(intruder), committed = 0;
    constexpr int clearingRadius=18;
    if (clearingSpice) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()->getTeamID()!=getHouse()->getTeamID() && structure->getHealth()>0
            && structure->canAttack() && structure->getItemID()!=Structure_Palace
            && structure->isVisible(getHouse()->getTeamID())
            && blockDistance(contact,structure->getLocation())<=8) threatValue+=value(structure);
    }
    std::vector<SimpleArmyPolicy::Responder> candidates;
    for (const auto* unit : getUnitList()) {
        if (!unit->isActive() || !unit->canAttack()) continue;
        if (unit != intruder && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->isVisible(getHouse()->getTeamID())
            && unit->isAFlyingUnit() == intruder->isAFlyingUnit()
            && blockDistance(contact,unit->getLocation()) <= 8) threatValue += value(unit);
        if (unit->getOwner()!=getHouse() || !unit->isRespondable() || humanControls(unit)
            || !unit->canAttack(intruder) || reserveDamagedUnitForRepair(unit) || unit->getAttackMode()==RETREAT
            || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Harvester
            || unit->getItemID()==Unit_Ornithopter) continue; // Air planner prioritizes active base/worker attackers.
        if (clearingSpice && blockDistance(contact,unit->getLocation())>clearingRadius) continue;
        const auto* target=unit->getTarget();
        const auto assignment=defenceAssignments.find(unit->getObjectID());
        if (assignment!=defenceAssignments.end()) {
            const auto* assigned=getObject(assignment->second);
            if (assigned && assigned->isAFlyingUnit() && airAttackContinues(unit,assigned)) continue;
        }
        if (assignment!=defenceAssignments.end() && target && target->isAUnit()) {
            const auto* victim=static_cast<const UnitBase*>(target)->getTarget();
            const auto* newVictim=intruder->isAUnit() ? static_cast<const UnitBase*>(intruder)->getTarget() : nullptr;
            if (victim && victim->isAStructure() && victim->getOwner()==getHouse()
                && (!newVictim || !newVictim->isAStructure() || newVictim->getOwner()!=getHouse())) continue;
        }
        const bool hostileTarget=target && target->getHealth()>0
            && target->getOwner()->getTeamID()!=getHouse()->getTeamID();
        // Count troops already fighting or travelling to this contact. They do
        // not need a fresh command on every hit. Do not pull units out of another fight.
        if (hostileTarget && target->isAFlyingUnit()==intruder->isAFlyingUnit()
            && blockDistance(contact,target->getLocation())<=8) { committed+=value(unit); continue; }
        if (hostileTarget && blockDistance(unit->getLocation(),target->getLocation())<=unit->getWeaponRange()) continue;
        candidates.push_back({unit->getObjectID(),value(unit),blockDistance(contact,unit->getLocation()).lround()});
    }
    auto response=clearingSpice
        ? SimpleArmyPolicy::clearingForce(threatValue,committed,candidates,clearingRadius)
        : SimpleArmyPolicy::reinforcements(threatValue,committed,candidates);
    // Troops already beside an attacked base must fight, even if distant
    // responders satisfy the proportional reinforcement budget.
    if (baseAttack) for (const auto& candidate : candidates)
        if (candidate.distance <= 12 && std::find(response.begin(),response.end(),candidate.id)==response.end())
            response.push_back(candidate.id);
    int dispatched=0;
    for (const auto id:response) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        groundSquad.erase(id);
        // Distant non-forced Area Guard targets are discarded by UnitBase.
        // Force transit, then release it inside guard range in checkAllUnits.
        // Persist assignments in the former escort-map save slot (same layout).
        const_cast<UnitBase*>(unit)->setGuardPoint(contact);
        doSetAttackMode(unit,AREAGUARD);
        doAttackObject(unit,intruder,!unit->isInAttackRange(intruder));
        defenceAssignments[id] = intruder->getObjectID();
        ++dispatched;
    }
    if (dispatched) traceDecision("defence_response",AITelemetry::Record().set("target",intruder->getObjectID())
        .set("x",contact.x).set("y",contact.y).set("threat_value",threatValue)
        .set("already_committed_value",committed).set("required_value",SimpleArmyPolicy::responseValue(threatValue))
        .set("reason",clearingSpice ? "clear_spice_launcher" : "under_attack")
        .set("dispatched",dispatched));
}

bool QuantBot::availableAirDefender(const UnitBase* unit, const UnitBase* aircraft) const {
    if (!unit || !aircraft || unit->getOwner()!=getHouse() || !unit->isActive()
        || !unit->isRespondable() || unit->isAFlyingUnit()) return false;
    const Uint32 item=unit->getItemID();
    if (item==Unit_Saboteur || item==Unit_Harvester || item==Unit_RebelHarvester
        || item==Unit_MCV || item==Unit_Carryall || item==Unit_Frigate
        || item==Unit_Sandworm) return false;
    // canAttack settles which weapons reach the sky: launchers and rocket
    // infantry do, tanks and soldiers do not, and a mod may draw the line
    // somewhere else again. Nothing here enumerates unit types itself.
    if (!unit->canAttack(aircraft)) return false;
    // A human order, an explicit retreat and a trip to the repair yard all
    // outrank the rescue. An ordinary attack, raid or rally order does not:
    // interrupting those is the whole point.
    return !humanControls(unit) && !reserveDamagedUnitForRepair(unit)
        && unit->getAttackMode()!=RETREAT;
}

Coord QuantBot::findAntiAirFiringPosition(const UnitBase* unit, const StructureBase* victim) const {
    if (!unit || !victim) return Coord::Invalid();
    const auto& map=getMap();
    const Coord here=unit->getLocation(), at=victim->getLocation(), size=victim->getStructureSize();
    if (!map.tileExists(here)) return Coord::Invalid();
    // Defend the building's vicinity, not the aircraft's moving position.
    const int reach=std::max(1,std::min(2,unit->getWeaponRange()-1));
    auto covers=[&](Coord p) {
        const int dx=std::max({at.x-p.x,p.x-(at.x+size.x-1),0});
        const int dy=std::max({at.y-p.y,p.y-(at.y+size.y-1),0});
        return std::max(dx,dy)<=reach;
    };
    if (covers(here)) return here;
    // Prove reachability from this defender, not merely connectivity around
    // the building. Ignore moving traffic, but include permanent obstructions.
    // Bound the search to the journey and a detour margin; eight neighbours
    // match the engine's ground pathfinder.
    constexpr int detour=12;
    const int x0=std::max(0,std::min(here.x,at.x)-detour);
    const int y0=std::max(0,std::min(here.y,at.y)-detour);
    const int x1=std::min(map.getSizeX()-1,std::max(here.x,at.x+size.x)+detour);
    const int y1=std::min(map.getSizeY()-1,std::max(here.y,at.y+size.y)+detour);
    const int width=x1-x0+1;
    std::vector<char> reached(static_cast<size_t>(width)*(y1-y0+1),0);
    auto slot=[&](Coord p) { return (p.y-y0)*width+p.x-x0; };
    std::vector<Coord> frontier{here};
    reached[slot(here)]=1;
    for(size_t head=0;head<frontier.size();++head) {
        const Coord from=frontier[head];
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            const Coord next=from+Coord(dx,dy);
            if(next.x<x0 || next.y<y0 || next.x>x1 || next.y>y1 || reached[slot(next)]) continue;
            const auto* tile=map.getTile(next);
            if(tile->hasAStructure() || (tile->isMountain() && !unit->isInfantry())) continue;
            reached[slot(next)]=1;frontier.push_back(next);
        }
    }
    Coord best=Coord::Invalid();
    int bestWalk=std::numeric_limits<int>::max();
    for(int y=std::max(y0,at.y-reach);y<=std::min(y1,at.y+size.y-1+reach);++y)
        for(int x=std::max(x0,at.x-reach);x<=std::min(x1,at.x+size.x-1+reach);++x) {
            const Coord post(x,y);
            if(!reached[slot(post)] || !covers(post) || !unit->canPass(x,y)) continue;
            const int walk=blockDistance(here,post).lround();
            if(walk<bestWalk) { bestWalk=walk;best=post; }
        }
    return best;
}

bool QuantBot::airAttackContinues(const UnitBase* defender, const ObjectBase* aircraft) const {
    if (!defender || !aircraft || aircraft->getHealth()<=0 || !aircraft->isActive()
        || !aircraft->getOwner() || aircraft->getOwner()->getTeamID()==getHouse()->getTeamID()
        || !aircraft->isVisible(getHouse()->getTeamID())) return false;
    const auto* victim=aircraft->isAUnit()
        ? static_cast<const UnitBase*>(aircraft)->getTarget() : nullptr;
    if (victim && victim->getHealth()>0 && victim->isActive() && victim->getOwner()==getHouse()) return true;
    // The attack is over. Keep the defender only while it can still shoot the
    // aircraft from where it stands: nothing chases one that has broken off.
    return defender->isInWeaponRange(aircraft);
}

/**
    Aircraft attacking a building we own, wherever that building stands.

    The reported match lost two outlying construction yards and the colonies
    around them to ornithopters while launchers stayed on ground skirmishes and
    rally points. The generic reinforcement response could not fix it: it skips
    units already firing at something, and what it does order is a forced attack
    on the aircraft's current tile — which the engine drops the moment the
    aircraft is out of weapon range (UnitBase::engageTarget releases any flying
    target beyond range, forced or not), leaving the launcher parked wherever it
    happened to be standing.

    So this owns the whole contact instead. It reads the attack from the
    aircraft's target rather than waiting for damage, gives each attacked
    building a bounded number of anti-air responders, has an in-range defender
    fire instead of taking another move order, and sends the rest to a ground
    tile from which the airspace over the building is covered. Assignments live
    in the saved defenceAssignments map, so they survive save/load and are
    respected by regrouping; nothing new is stored.
*/
void QuantBot::defendStructuresFromAircraft() {
    if (supportMode || getHouse()==nullptr) return;
    AITelemetry::PerformanceScope perfScope("ai.air_rescue",getGameCycleCount(),getHouse()->getHouseID());
    const int myTeam=getHouse()->getTeamID();
    struct AirAttack { const UnitBase* aircraft; const StructureBase* victim; };
    std::vector<AirAttack> attacks;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isAFlyingUnit() || unit->getHealth()<=0) continue;
        if (!unit->getOwner() || unit->getOwner()->getTeamID()==myTeam) continue;
        if (!unit->isVisible(myTeam)) continue;
        // The attack itself, not the damage it has already done: waiting for a
        // damage callback is what let the colonies burn down.
        const auto* target=unit->getTarget();
        if (!target || !target->isAStructure() || target->getOwner()!=getHouse()
            || target->getHealth()<=0 || !target->isActive()) continue;
        if (isCampaignEnemy() && !campaignLocalContact(unit)) continue;
        attacks.push_back({unit,static_cast<const StructureBase*>(target)});
    }
    if (attacks.empty()) return;
    std::sort(attacks.begin(),attacks.end(),[](const AirAttack& a,const AirAttack& b) {
        if (a.victim->getObjectID()!=b.victim->getObjectID())
            return a.victim->getObjectID()<b.victim->getObjectID();
        return a.aircraft->getObjectID()<b.aircraft->getObjectID();
    });
    struct Candidate { const UnitBase* unit; int distance, busy; Uint32 id; };
    std::set<Uint32> committed;
    for (const auto& attack:attacks) {
        const Uint32 aircraftID=attack.aircraft->getObjectID();
        const Coord contact=attack.victim->getLocation();
        std::vector<const UnitBase*> responders;
        std::vector<Candidate> candidates;
        std::map<Uint32,Coord> posts;
        for (const auto* unit:getUnitList()) {
            const Uint32 id=unit->getObjectID();
            if (committed.count(id) || !availableAirDefender(unit,attack.aircraft)) continue;
            const auto assigned=defenceAssignments.find(id);
            if (assigned!=defenceAssignments.end()) {
                // Already on this aircraft: keep it there. A rescue that is
                // re-chosen every pass never arrives anywhere.
                // Answering another live air attack: leave that one alone.
                const auto* other=getObject(assigned->second);
                if (assigned->second!=aircraftID && other && other->isAUnit() && other->isAFlyingUnit()
                    && airAttackContinues(unit,other)) continue;
            }
            const int distance=blockDistance(unit->getLocation(),contact).lround();
            const bool continuing=assigned!=defenceAssignments.end() && assigned->second==aircraftID;
            if (distance>kAirRescueRadius && !continuing) continue;
            if(continuing) {
                const Coord post=unit->isInWeaponRange(attack.aircraft)
                    ? unit->getLocation() : findAntiAirFiringPosition(unit,attack.victim);
                if(post.isInvalid()) { defenceAssignments.erase(assigned);continue; }
                posts[id]=post;responders.push_back(unit);continue;
            }
            candidates.push_back({unit,distance,assigned!=defenceAssignments.end() ? 1 : 0,id});
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
            const auto launcher=[](const UnitBase* unit) {
                return unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher;
            };
            // Launchers provide the mobile air cover; nearby rocket infantry
            // are a fallback when no launcher is available.
            if(launcher(a.unit)!=launcher(b.unit)) return launcher(a.unit);
            if (a.distance!=b.distance) return a.distance<b.distance;
            if (a.busy!=b.busy) return a.busy<b.busy;   // Free troops before committed ones.
            return a.id<b.id;
        });
        for (const auto& candidate:candidates) {
            if (static_cast<int>(responders.size())>=kAirRescueDefenders) break;
            // Only route the nearest preferred candidates we actually need.
            const Coord post=candidate.unit->isInWeaponRange(attack.aircraft)
                ? candidate.unit->getLocation() : findAntiAirFiringPosition(candidate.unit,attack.victim);
            if(post.isInvalid()) continue;
            posts[candidate.id]=post;responders.push_back(candidate.unit);
        }
        int firing=0, approaching=0, stranded=0;
        for (const auto* unit:responders) {
            const Uint32 id=unit->getObjectID();
            committed.insert(id);
            defenceAssignments[id]=aircraftID;
            groundSquad.erase(id);
            if (unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
            if (unit->isInWeaponRange(attack.aircraft)) {
                // Useful fire outranks another move order: a launcher that can
                // already shoot is not marched at a tile the aircraft happens
                // to be flying over.
                if (unit->getTarget()!=attack.aircraft) doAttackObject(unit,attack.aircraft,true);
                ++firing;
                continue;
            }
            const Coord post=posts.at(id);
            // No reachable ground covers this building for this unit. Ordering
            // it at the aircraft would be ordering it onto thin air.
            if (post.isInvalid()) { ++stranded; continue; }
            if (unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
            const_cast<UnitBase*>(unit)->setGuardPoint(post);
            // Commit transit so incidental ground targets cannot arrest the
            // rescue. The next air pass switches to fire as soon as it can.
            if (unit->getLocation()!=post && (unit->getDestination()!=post
                || !unit->wasForced() || unit->hasATarget()))
                doMove2Pos(unit,post.x,post.y,true);
            ++approaching;
        }
        if (firing || approaching || stranded)
            traceDecision("air_rescue",AITelemetry::Record().set("aircraft",aircraftID)
                .set("victim",attack.victim->getObjectID()).set("item",attack.victim->getItemID())
                .set("x",contact.x).set("y",contact.y).set("firing",firing)
                .set("approaching",approaching).set("no_firing_position",stranded)
                .set("defenders",static_cast<int>(responders.size())));
    }
}

bool QuantBot::tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                          const QuantBotConfig& config, const ObjectBase* emergencyAttacker,
                                          bool emergencyOnBase) {
    const int myTeam=getHouse()->getTeamID();
    const Map& map=getMap();
    std::vector<const UnitBase*> aircraft;
    for(const auto* unit:getUnitList())
        if(unit->getOwner()==getHouse() && unit->getItemID()==Unit_Ornithopter
            && unit->isActive() && unit->isRespondable() && !humanControls(unit)) aircraft.push_back(unit);
    ornithopterStrikeTeam.reset(); // Old saved mass-Hunt missions no longer grant permission to attack.
    if(aircraft.empty()) return false;
    AITelemetry::PerformanceScope perfScope("ai.ornithopter_safe_strikes",getGameCycleCount(),getHouse()->getHouseID());

    AirStrikePolicy::Coverage coverage(map.getSizeX(),map.getSizeY());
    int visibleAntiAir=0;
    auto addDefender=[&](const ObjectBase* defender) {
        if(!defender || !defender->isActive() || defender->getHealth()<=0
            || !defender->getOwner() || defender->getOwner()->getTeamID()==myTeam
            || !defender->isVisible(myTeam) || !AirStrikePolicy::antiAir(defender->getItemID())) return;
        // A temporary power outage does not make a turret district a safe sortie.
        coverage.add(defender->getLocation(),AirStrikePolicy::safetyRange(defender->getWeaponRange()));
        ++visibleAntiAir;
    };
    for(const auto* structure:getStructureList()) addDefender(structure);
    for(const auto* unit:getUnitList()) addDefender(unit);

    std::vector<const ObjectBase*> defendedAssets;
    for(const auto* structure:getStructureList())
        if(structure->getOwner()==getHouse() && structure->isActive() && structure->getHealth()>0)
            defendedAssets.push_back(structure);
    for(const auto* worker:getUnitList())
        if(worker->getOwner()==getHouse() && worker->isActive() && worker->getHealth()>0
            && (worker->getItemID()==Unit_Harvester || worker->getItemID()==Unit_RebelHarvester))
            defendedAssets.push_back(worker);
    LocalPointIndex defendedIndex(map.getSizeX(),map.getSizeY());
    int largestAsset=1;
    for(size_t i=0;i<defendedAssets.size();++i) {
        const auto* asset=defendedAssets[i];
        defendedIndex.add(asset->getLocation().x,asset->getLocation().y,i);
        if(asset->isAStructure()) {
            const Coord size=static_cast<const StructureBase*>(asset)->getStructureSize();
            largestAsset=std::max({largestAsset,size.x,size.y});
        }
    }
    struct Candidate { const ObjectBase* object; int weight; int rank; };
    std::vector<Candidate> candidates;
    auto addCandidate=[&](const ObjectBase* object,const QuantBotConfig::TargetPriority& priority) {
        if(!object || !object->isActive() || object->getHealth()<=0 || !object->getOwner()
            || object->getOwner()->getTeamID()==myTeam || !object->isVisible(myTeam)
            || object->isAFlyingUnit()) return;
        const Coord size=object->isAStructure()
            ? static_cast<const StructureBase*>(object)->getStructureSize() : Coord(1,1);
        const auto* victim=object->getTarget();
        const bool engagingAsset=victim && victim->isActive()
            && victim->getHealth()>0 && victim->getOwner()==getHouse()
            && (victim->isAStructure() || victim->getItemID()==Unit_Harvester
                || victim->getItemID()==Unit_RebelHarvester)
            && object->canAttack(victim)
            && blockDistance(object->getLocation(),victim->getClosestPoint(object->getLocation()))
                <= object->getWeaponRange()+3;
        const bool underAttack=object==emergencyAttacker || engagingAsset;
        // A building of ours is the base; a worker in the field is not. The
        // damage callback reports which one it was for its own attacker.
        const bool attackingBase=engagingAsset ? victim->isAStructure()
            : (object==emergencyAttacker && emergencyOnBase);
        // Avoid AA during raids and speculative patrols. An actual attack on
        // our economy/base is different: recall the aircraft and kill the
        // attacker, including launchers, rather than declaring rescue unsafe.
        if(!underAttack && (AirStrikePolicy::antiAir(object->getItemID())
            || !coverage.clearFootprint(object->getLocation(),size))) return;
        // All undefended buildings are eligible, including zones absent from the
        // combat priority table; retain configured priorities for ranking.
        bool defensiveContact=false;
        if(!object->isAStructure() && object->canAttack()) {
            defendedIndex.visit(object->getLocation().x,object->getLocation().y,
                object->getWeaponRange()+3+largestAsset,[&](size_t i) {
                    if(!defensiveContact && blockDistance(object->getLocation(),
                        defendedAssets[i]->getClosestPoint(object->getLocation())) <= object->getWeaponRange()+3)
                        defensiveContact=true;
                });
        }
        const int rank=underAttack ? AirStrikePolicy::underAttackRank(attackingBase)
            : AirStrikePolicy::targetRank(object->isAStructure(),defensiveContact);
        if (rank==AirStrikePolicy::RaidRank && !diffSettings.ornithopterAttackEnabled) return;
        if (isCampaignEnemy() && rank==AirStrikePolicy::DefenseRank && !campaignLocalContact(object)) return;
        if(rank>0) candidates.push_back({object,std::max(1,priority.build+priority.target),rank});
    };
    for(const auto* structure:getStructureList())
        addCandidate(structure,config.getStructurePriority(structure->getItemID()));
    // Defensive interception is available even when offensive raids are disabled.
    for(const auto* unit:getUnitList())
        addCandidate(unit,config.getUnitPriority(unit->getItemID()));

    bool issued=false;
    for(const auto* unit:aircraft) {
        const ObjectBase* target=nullptr;
        double bestScore=-1;
        int bestRank=0;
        // The interception this aircraft is already flying, if it is still a
        // legal target of the same search. It wins every equal-or-lower ranked
        // comparison below, so a wing finishes its attack runs instead of
        // re-choosing between equivalent targets on every damage callback.
        const ObjectBase* held=nullptr;
        int heldRank=0;
        if(!reserveDamagedUnitForRepair(unit) && unit->getAttackMode()!=RETREAT) {
            for(const auto& candidate:candidates) {
                if (isCampaignEnemy() && candidate.rank==AirStrikePolicy::RaidRank
                    && !campaignWave.members.count(unit->getObjectID())) continue;
                if(!unit->canAttack(candidate.object)) continue;
                const Coord endpoint=candidate.object->getClosestPoint(unit->getLocation());
                double score=double(candidate.weight)/(blockDistance(unit->getLocation(),endpoint).toDouble()+1);
                if(!AirStrikePolicy::emergencyRank(candidate.rank)
                    && !coverage.clearApproach(unit->getLocation(),endpoint)) continue;
                if(candidate.object==unit->getTarget() && unit->wasForced()
                    && candidate.rank>heldRank) { held=candidate.object; heldRank=candidate.rank; }
                if(candidate.rank<bestRank || (candidate.rank==bestRank && score<=bestScore)) continue;
                bestRank=candidate.rank;
                bestScore=score;
                target=candidate.object;
            }
            if(AirStrikePolicy::holdsInterception(heldRank,bestRank)) {
                target=held;
                bestRank=heldRank;
            }
        }
        const bool modeChanged=unit->getAttackMode()!=STOP;
        const bool hadTarget=unit->hasATarget();
        if(target) {
            const bool orderNeeded=modeChanged || unit->getTarget()!=target || !unit->wasForced();
            // STOP suppresses autonomous target acquisition; the explicit forced
            // attack still flies and fires. No follow-on Hunt after target death.
            if(modeChanged) doSetAttackMode(unit,STOP);
            if(orderNeeded) {
                doAttackObject(unit,target,true);
                issued=true;
                traceDecision("ornithopter_safe_strike",AITelemetry::Record().set("unit",unit->getObjectID())
                    .set("target",target->getObjectID()).set("target_item",target->getItemID())
                    .set("visible_anti_air",visibleAntiAir).set("safety_margin_tiles",5)
                    .set("held_target",held==target)
                    .set("reason",bestRank==AirStrikePolicy::BaseUnderAttackRank ? "intercept_base_attacker"
                        : bestRank==AirStrikePolicy::UnderAttackRank ? "intercept_active_attacker"
                        : bestRank==AirStrikePolicy::RaidRank ? "exposed_building" : "defend_base_or_harvester"));
            }
        } else {
            if(modeChanged || hadTarget) { doSetAttackMode(unit,STOP); issued=true; }
            // Return along a clear corridor to a real owned building, not a base
            // centroid that can sit in enemy fire. Escape newly arrived AA first.
            Coord home; home.invalidate();
            int bestDistance=std::numeric_limits<int>::max();
            for(const auto* asset:defendedAssets) {
                if(!asset->isAStructure()) continue;
                const Coord point=asset->getLocation();
                const int distance=(blockDistance(unit->getLocation(),point)*100).lround();
                if(distance<bestDistance && coverage.clearApproach(unit->getLocation(),point)) {
                    home=point; bestDistance=distance;
                }
            }
            if(home.isInvalid() && !coverage.safe(unit->getLocation())) home=coverage.escape(unit->getLocation());
            if(home.isValid()) {
                const_cast<UnitBase*>(unit)->setGuardPoint(home.x,home.y);
                const bool unsafeDestination=unit->getDestination().isValid()
                    && !coverage.clearApproach(unit->getLocation(),unit->getDestination());
                if(modeChanged || hadTarget || unsafeDestination
                    || blockDistance(unit->getLocation(),home)>3) {
                    if(unit->getDestination()!=home || hadTarget || modeChanged) {
                        doMove2Pos(unit,home.x,home.y,true);
                        issued=true;
                    }
                }
            }
            if(modeChanged || hadTarget) traceDecision("ornithopter_hold",AITelemetry::Record()
                .set("unit",unit->getObjectID()).set("visible_anti_air",visibleAntiAir)
                .set("reason","no_safe_target_or_approach"));
        }
    }
    return issued;
}


void QuantBot::attack(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.attack", getGameCycleCount(), getHouse()->getHouseID());
	if (supportMode) {
		attackTimer = std::numeric_limits<Sint32>::max();
		return;
	}
    if (isCampaignEnemy() && !campaignCanLaunch()) return;

    // Get config for this difficulty
    const QuantBotConfig& config = getQuantBotConfig();
    const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    if (isCampaignEnemy()) attackTimer=0;
    else {
        const bool campaignAlly = isCampaignGameType(currentGame->gameType) && isAlliedWithHuman();
        attackTimer = campaignAlly ? MILLI2CYCLES(60000)
            : SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
                currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());
        traceDecision("attack_schedule",AITelemetry::Record().set("delay_cycles",attackTimer)
            .set("base_cycles",MILLI2CYCLES(campaignAlly ? 60000 : config.attackTimerMs)));
    }

	// Check if this difficulty is allowed to attack at all
	if (!diffSettings.attackEnabled) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "difficulty_disabled"));
		logDebug("Don't attack. Difficulty %d has attackEnabled = false", static_cast<int>(difficulty));
		return;
	}

    if (!isCampaignEnemy()) tryLaunchOrnithopterStrike(diffSettings, config);

	// Main attack loop - check military strength threshold
	// Campaign mode: Use difficulty-specific threshold from config
	// Custom mode: global threshold, except Brutal uses its 25% difficulty threshold.
	float attackThresholdPercent = (gameMode == GameMode::Campaign || difficulty == Difficulty::Brutal)
		? diffSettings.attackThresholdPercent 
		: config.attackThresholdPercent;

	FixPoint attackThreshold = FixPoint(static_cast<int>(attackThresholdPercent * 100)) / 100;
	const bool vanillaCustom = gameMode == GameMode::Custom && !getHouse()->isPowerRequired();
	int requiredMilitary = vanillaCustom
        ? DuneCity::vanillaAttackThreshold((militaryValueLimit * attackThreshold).lround(), static_cast<int>(difficulty))
        : (militaryValueLimit * attackThreshold).lround();
    const bool campaign = isCampaignGameType(currentGame->gameType);
    if (campaign) requiredMilitary=campaignRequiredArmy(requiredMilitary);
    if (militaryValue < requiredMilitary) {
        // Recheck readiness promptly; do not miss a short-lived strength window.
        if (campaign || (vanillaCustom && difficulty == Difficulty::Brutal))
            attackTimer = std::min(attackTimer, static_cast<int>(MILLI2CYCLES(15000)));
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "army_threshold").set("military", militaryValue).set("limit", militaryValueLimit)
            .set("required_military", requiredMilitary));
		logDebug("Don't attack. Not enough troops: house: %d  dif: %d  mStr: %d  mLim: %d (need %.1f%%)",
			getHouse()->getHouseID(), static_cast<Uint8>(difficulty), militaryValue, militaryValueLimit, attackThresholdPercent * 100.0f);
		return;
	}

	// Campaign attacks must remain possible after losing a repair yard, or on
    // scenarios where it cannot be built. Repair remains useful, not mandatory.
	if (!campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && getHouse()->getNumItems(Structure_RepairYard) == 0 && currentGame->techLevel > 4) {
		traceDecision("attack_deferred", AITelemetry::Record().set("reason", "repair_prerequisite"));
		logDebug("Don't attack. Wait until you have a repair yard.");
		return;
	}

    launchGroundHunt();
    if (isCampaignEnemy()) tryLaunchOrnithopterStrike(diffSettings, config);
}

void QuantBot::onHumanUnitOrder(Uint32 id) {
    manualUnitOrders[id] = getGameCycleCount();
    groundSquad.erase(id);
    defenceAssignments.erase(id);
}

bool QuantBot::humanControls(const UnitBase* unit) const {
    const auto it = manualUnitOrders.find(unit->getObjectID());
    return it != manualUnitOrders.end() && (getGameCycleCount()-it->second < MILLI2CYCLES(120000)
        || unit->wasForced() || unit->isMoving() || unit->hasATarget());
}

void QuantBot::launchGroundHunt() {
    if (supportMode) return;
    const bool limited = isCampaignEnemy();
    const bool custom = !isCampaignGameType(currentGame->gameType);
    if (limited && !campaignCanLaunch()) return;
    const auto profile = limited ? campaignProfile()
        : CampaignDifficultyPolicy::profile(static_cast<int>(difficulty),currentGame->techLevel);
    // Caps apply to this dispatch. The roster separately tracks all survivors.
    CampaignDifficultyPolicy::Pressure pressure;
    const float ratio = getQuantBotConfig().getSettings(static_cast<int>(difficulty)).attackForceMilitaryValueRatio;
    int percent = std::isfinite(ratio) ? static_cast<int>(std::clamp(ratio,0.0f,1.0f)*100.0f+0.5f) : 0;
    // Campaign roles define commitment even with older saved config defaults.
    // Keep an explicit zero as the opt-out; each house uses its own difficulty.
    if (limited && percent>0) percent=profile.enemyCommitPercent;
    int armyValue=0, committedValue=0, availableValue=0, requiredReady=0;
    // Ground pressure is tracked on its own: ornithopters and other non-ground
    // troops must not inflate a budget that can only ever buy ground units.
    int groundArmyValue=0, groundCommittedUnits=0, groundCommittedValue=0;
    std::vector<SimpleArmyPolicy::Responder> candidates;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner()!=getHouse() || unit->getHealth()<=0 || !unit->isActive() || !unit->isRespondable()
            || !unit->canAttack() || unit->getItemID()==Unit_Saboteur || humanControls(unit)
            || unit->getItemID()==Unit_Harvester || unit->getItemID()==Unit_Sandworm) continue;
        // Summoned/scripted troops can have zero purchase price. They still
        // consume combat pressure rather than being effectively free attackers.
        const int price=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
        armyValue+=price;
        if (unit->getAttackMode()==HUNT) committedValue+=price;
        const bool ground=unit->isAGroundUnit();
        if (ground) groundArmyValue+=price;
        const bool free=!reserveDamagedUnitForRepair(unit) && unit->getAttackMode()!=RETREAT
            && !unit->hasATarget() && !defenceAssignments.count(unit->getObjectID());
        // Every existing hunter counts, including forced moves and busy targets.
        // Never select it again or silently drop it from the pressure budget.
        if (ground && unit->getAttackMode()==HUNT) {
            ++groundCommittedUnits; groundCommittedValue+=price;
        }
        if (!free || (custom && unit->getAttackMode()==HUNT)) continue;
        if (!limited && (!ground || unit->getItemID()==Unit_Saboteur
            || (unit->getAttackMode()==HUNT && !unit->wasForced()))) continue;
        if (limited && (scriptedAssaults.count(unit->getObjectID())
            || campaignWave.members.count(unit->getObjectID()))) continue;
        if (limited && unit->getItemID()==Unit_Ornithopter
            && !getQuantBotConfig().getSettings(static_cast<int>(difficulty)).ornithopterAttackEnabled) continue;
        candidates.push_back({unit->getObjectID(),price,0});
        availableValue+=price;
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    if (limited) committedValue=campaignPressure().value;
    // Every custom difficulty commits its configured percentage of the current
    // ground army, without any additional unit count or flat value ceiling.
    const int customBudget=SimpleArmyPolicy::attackBudget(groundArmyValue,percent);
    std::vector<Uint32> selected;
    if (limited) {
        const auto& settings=getQuantBotConfig().getSettings(static_cast<int>(difficulty));
        const FixPoint threshold=FixPoint(static_cast<int>(settings.attackThresholdPercent*100))/100;
        requiredReady=campaignRequiredArmy((militaryValueLimit*threshold).lround());
        // Away, injured, repairing and busy troops cannot assemble a new wave.
        // Requiring readiness here prevents repeated checks dripping out reserves.
        if (availableValue<requiredReady) {
            traceDecision("attack_deferred",AITelemetry::Record().set("reason","wave_readiness")
                .set("available_value",availableValue).set("required_military",requiredReady)
                .set("committed_value",committedValue));
            return;
        }
        const int houseUnits=profile.limitedWave ? profile.units : INT32_MAX;
        const int houseValue=profile.limitedWave ? profile.value : INT32_MAX;
        const int budget=std::max(0,std::min(houseValue,
            SimpleArmyPolicy::attackBudget(availableValue,percent)));
        int value=0;
        for (const auto& candidate : candidates) {
            if (static_cast<int>(selected.size())>=houseUnits) break;
            if (candidate.value>budget-value || !CampaignDifficultyPolicy::fits(profile,pressure,candidate.value)) continue;
            selected.push_back(candidate.id); value+=candidate.value;
            ++pressure.units; pressure.value+=candidate.value;
        }
        // Avoid rounding a single ready Hard/Brutal unit down to no attack.
        if (selected.empty() && pressure.units==0 && percent>0 && difficulty>=Difficulty::Hard) {
            const SimpleArmyPolicy::Responder* cheapest=nullptr;
            for (const auto& candidate : candidates)
                if (CampaignDifficultyPolicy::fits(profile,pressure,candidate.value)
                    && (!cheapest || candidate.value<cheapest->value)) cheapest=&candidate;
            if (cheapest) {selected.push_back(cheapest->id);++pressure.units;pressure.value+=cheapest->value;}
        }
    } else if (isCampaignGameType(currentGame->gameType)) {
        // Shared-house helpers retain a modest home reserve, without enemy caps.
        selected=SimpleArmyPolicy::limitedAttack(armyValue,committedValue,100-profile.reservePercent,candidates);
    } else {
        // A custom wave commits the configured share of the ground army, minus
        // everything already out there. Survivors keep their place in the share,
        // so repeated passes reinforce a wave instead of stacking new ones.
        selected=SimpleArmyPolicy::customAttack(groundArmyValue,groundCommittedValue,
            percent,candidates);
    }
    int count=0,value=0;
    if (limited && !selected.empty()) {
        campaignWave.launched=getGameCycleCount();
        campaignWave.lastActive=getGameCycleCount();
        campaignWave.members.insert(selected.begin(),selected.end());
    }
    const ObjectBase* front=nullptr;
    for (auto id : selected) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        doSetAttackMode(unit,GUARD);
        if (limited && unit->isAGroundUnit()) {
            if (!front || difficulty>=Difficulty::Hard) front=campaignObjective(unit,count%2);
            if (count==0) campaignWave.front=front ? front->getObjectID() : NONE_ID;
            if (front) {
                doSetAttackMode(unit,AREAGUARD);
                bool flanking=false;
                if (difficulty>=Difficulty::Hard && count%2) {
                    const Coord start=unit->getLocation(), end=front->getLocation();
                    const int dx=end.x-start.x, dy=end.y-start.y;
                    const int length=std::max({1,std::abs(dx),std::abs(dy)});
                    const int side=difficulty==Difficulty::Brutal && getHouse()->getHouseID()%2 ? -1 : 1;
                    const Coord approach((start.x+end.x)/2-side*dy*8/length,
                                         (start.y+end.y)/2+side*dx*8/length);
                    if (length>12 && getMap().tileExists(approach) && unit->canPass(approach.x,approach.y)) {
                        doMove2Pos(unit,approach.x,approach.y,true);flanking=true;
                    }
                }
                if (!flanking) doAttackObject(unit,front,true);
            }
            else doSetAttackMode(unit,HUNT);
        } else if (unit->isAGroundUnit()) doSetAttackMode(unit,HUNT);
        ++count; value+=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
    }
    if (count==0) return; // An empty house checking readiness is not an attack.
    if (limited && profile.limitedWave) {
        attackTimer=MILLI2CYCLES(CampaignDifficultyPolicy::repeatDelayMs(
            static_cast<int>(difficulty),
            getGameInitSettings().getRandomSeed(),getGameCycleCount(),getHouse()->getHouseID()));
        traceDecision("attack_schedule",AITelemetry::Record().set("delay_cycles",attackTimer)
            .set("reason","next_wave_after_dispatch"));
    }
    traceDecision("ground_hunt",AITelemetry::Record().set("members",count).set("value",value)
        .set("campaign_limited",limited).set("army_value",armyValue)
        .set("committed_value",custom ? groundCommittedValue : committedValue)
        .set("committed_units",limited ? campaignPressure().units : groundCommittedUnits)
        .set("ground_army_value",groundArmyValue)
        .set("pressure_units",limited ? campaignPressure().units : groundCommittedUnits+count)
        .set("pressure_value",limited ? campaignPressure().value : groundCommittedValue+value)
        // Custom attacks have no unit or value ceiling any more; the fields stay
        // in the record at their existing "no cap" sentinel for audit tooling.
        .set("attack_unit_cap",-1)
        .set("attack_value_cap",-1)
        .set("available_value",availableValue).set("required_ready",requiredReady)
        .set("attack_percent",percent).set("attack_budget",limited ? SimpleArmyPolicy::attackBudget(availableValue,percent) : (custom ? customBudget : SimpleArmyPolicy::attackBudget(armyValue,100-profile.reservePercent)))
        .set("active_units",limited ? campaignPressure().units : count)
        .set("active_value",limited ? campaignPressure().value : value)
        .set("alliance_units",pressure.units).set("alliance_value",pressure.value)
        .set("alliance_unit_cap",profile.limitedWave ? profile.units : -1)
        .set("alliance_value_cap",profile.limitedWave ? profile.value : -1)
        .set("alliance_house_cap",profile.houses));
}

void QuantBot::releaseLegacyGroundSquad() {
    if (!groundSquadPhase && groundSquad.empty()) return;
    for (const auto id:groundSquad) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getOwner()!=getHouse() || humanControls(unit) || unit->getItemID()==Unit_Saboteur) continue;
        doSetAttackMode(unit,GUARD);
        doSetAttackMode(unit,groundSquadPhase==2 ? HUNT : AREAGUARD);
    }
    groundSquad.clear(); groundSquadPhase=0; groundSquadObjective=NONE_ID;
    squadRallyLocation=Coord::Invalid();
    rallySelectedCycle=std::numeric_limits<Uint32>::max();
}

void QuantBot::resetLearningForMeasuredScoring() {
    performanceHistory = UnitMixPolicy::PerformanceHistory();
}

void QuantBot::onCombatReward(Uint32 attacker, Uint32 target, const CombatReward::Totals& reward) {
    for (auto& raid : harvesterStrikeTraces) for (auto& member : raid.members) if (member.id==attacker) {
        member.reward.damageMilli += reward.damageMilli;
        member.reward.killBonusMilli += reward.killBonusMilli;
        member.reward.hpRemovedMilli += reward.hpRemovedMilli;
        member.reward.hits += reward.hits;
        member.reward.kills += reward.kills;
        if (raid.target==target && reward.kills>0) raid.targetKilledByStrike=true;
        return; // Membership belongs to one observed strike at a time.
    }
}

void QuantBot::finishTelemetry() { updateHarvesterStrikeTelemetry(true); }

void QuantBot::updateHarvesterStrikeTelemetry(bool final) {
    AITelemetry::PerformanceScope perfScope("ai.updateHarvesterStrikeTelemetry", getGameCycleCount(), getHouse()->getHouseID());
    if (!AITelemetry::log().enabled()) { harvesterStrikeTraces.clear(); return; }
    const Uint32 now=getGameCycleCount();
    for (auto it=harvesterStrikeTraces.begin(); it!=harvesterStrikeTraces.end();) {
        auto& raid=*it;
        const auto* target=currentGame->getObjectManager().getObject(raid.target);
        const bool visible=target && target->isVisible(getHouse()->getTeamID());
        if (visible) raid.lastVisible=now;
        // A strike may outlive the initial forced attack command. Only abandon
        // a visible harvester when it moves under meaningful protection; a
        // harmless command expiry must reissue the same target instead.
        int targetDefence = 0;
        bool protectedByTurret = false;
        if (visible && target && target->isActive()) {
            for (const auto* building : getStructureList()) {
                if (building->getOwner()->getTeamID() != target->getOwner()->getTeamID()
                    || !building->isVisible(getHouse()->getTeamID()) || !building->canAttack()) continue;
                if (blockDistance(target->getLocation(), building->getLocation())
                    <= building->getWeaponRange() + 3) {
                    protectedByTurret = true;
                    break;
                }
            }
            if (!protectedByTurret) for (const auto* guard : getUnitList()) {
                if (!guard->isActive() || !guard->canAttack() || !guard->isVisible(getHouse()->getTeamID())
                    || guard->getOwner()->getTeamID() != target->getOwner()->getTeamID()) continue;
                if (blockDistance(target->getLocation(), guard->getLocation())
                    <= std::max(6, guard->getWeaponRange() + 2))
                    targetDefence += currentGame->objectData.data[guard->getItemID()][guard->getOriginalHouseID()].price;
            }
        }
        const bool targetDefended = protectedByTurret || targetDefence > 2000;
        int alive=0, lost=0, captured=0, lostValue=0;
        bool engaged=false, stillAssigned=false;
        int64_t damage=0, bonus=0;
        AITelemetry::Record members;
        for (const auto& member : raid.members) {
            const auto* unit=dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(member.id));
            const bool dead=!unit || unit->getHealth()<=0;
            const bool changedOwner=unit && !dead && unit->getOwner()!=getHouse();
            if (dead) { ++lost; lostValue+=member.price; }
            else if (changedOwner) ++captured;
            else {
                ++alive;
                const auto* currentTarget=unit->getTarget();
                stillAssigned |= currentTarget && currentTarget->getObjectID()==raid.target;
                engaged |= currentTarget && unit->isActive()
                    && blockDistance(unit->getLocation(),currentTarget->getLocation())<=unit->getWeaponRange();
            }
            damage+=member.reward.damageMilli; bonus+=member.reward.killBonusMilli;
            members.set(std::to_string(member.id),AITelemetry::Record().set("item",member.item)
                .set("lost",dead).set("captured",changedOwner).set("damage_value_milli",member.reward.damageMilli)
                .set("kill_bonus_milli",member.reward.killBonusMilli).set("reward_milli",member.reward.total()));
        }
        int retargeted = 0;
        // Observation only: squad orders must not depend on telemetry being enabled.
        (engaged ? raid.engagementCycles : raid.transitCycles) += now-raid.sampled;
        raid.sampled=now;
        const char* outcome=nullptr;
        if (raid.targetKilledByStrike) outcome="target_killed_by_strike";
        else if (!target || target->getHealth()<=0) outcome="target_removed";
        else if (target->getOwner()->getTeamID()==getHouse()->getTeamID()) outcome="target_captured";
        else if (alive==0) outcome="force_lost_or_captured";
        else if (!visible && now-raid.lastVisible>=MILLI2CYCLES(15000)) outcome="target_lost_contact";
        else if (visible && !target->isActive()) outcome="target_transport_or_inactive";
        else if (targetDefended) outcome="target_entered_defended_area";
        else if (!stillAssigned && now-raid.start>=MILLI2CYCLES(5000)) outcome="attackers_unavailable_target_survived";
        else if (now-raid.start>=MILLI2CYCLES(120000)) outcome="timeout_target_survived";
        if (!outcome && final) outcome="game_ended_while_active";
        if (outcome || now-raid.logged>=MILLI2CYCLES(5000)) {
            traceDecision(outcome ? "harvester_strike_outcome" : "harvester_strike_progress",AITelemetry::Record()
                .set("raid_id",raid.id).set("target",raid.target).set("outcome",outcome ? outcome : "active")
                .set("target_visible",visible).set("target_present",target!=nullptr)
                .set("elapsed_cycles",now-raid.start).set("transit_cycles",raid.transitCycles)
                .set("engagement_cycles",raid.engagementCycles).set("survivors",alive).set("losses",lost)
                .set("captured",captured).set("lost_value",lostValue).set("damage_value_milli",damage)
                .set("kill_bonus_milli",bonus).set("reward_milli",damage+bonus).set("members",members)
                .set("target_defence",targetDefence).set("target_turret_covered",protectedByTurret)
                .set("retargeted_members",retargeted));
            raid.logged=now;
        }
        if (outcome) it=harvesterStrikeTraces.erase(it); else ++it;
    }
}

Coord QuantBot::findSquadRallyLocation() {
    const Uint32 now=getGameCycleCount();
    // One bounded search per 30 simulation seconds, including failed searches.
    if (rallySelectedCycle!=std::numeric_limits<Uint32>::max()
        && now-rallySelectedCycle<MILLI2CYCLES(30000)) return squadRallyLocation;
    rallySelectedCycle=now;
    int x=0,y=0,count=0;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || unit->getItemID()!=Unit_Harvester) continue;
        const auto* harvester=static_cast<const Harvester*>(unit);
        if (harvester->isReturning() || !harvester->isHarvesting()) continue;
        x+=unit->getX(); y+=unit->getY(); ++count;
    }
    Coord centre=count ? Coord(x/count,y/count) : findBaseCentre(getHouse()->getHouseID());
    if (centre.isInvalid()) return Coord::Invalid();
    const UnitBase* closest=nullptr;
    int distance=std::numeric_limits<int>::max();
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->canAttack() || unit->isAFlyingUnit()
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !unit->isVisible(getHouse()->getTeamID())) continue;
        const int d=blockDistance(centre,unit->getLocation()).lround();
        if (d<distance) { distance=d; closest=unit; }
    }
    // Stand on the enemy-facing side of the working harvesters, without selecting
    // an enemy base as a compulsory destination for every unit.
    if (closest) {
        const Coord delta=closest->getLocation()-centre;
        const int scale=std::max(1,std::max(std::abs(delta.x),std::abs(delta.y)));
        centre+=Coord(delta.x*3/scale,delta.y*3/scale);
    }
    auto usable=[&](Coord p) { return getMap().tileExists(p) && !getMap().getTile(p)->isMountain()
        && !getMap().getTile(p)->hasAStructure() && dangerAt(p)==0; };
    if (squadRallyLocation.isValid() && blockDistance(centre,squadRallyLocation)<=5 && usable(squadRallyLocation))
        return squadRallyLocation;
    Coord best=Coord::Invalid(); int bestScore=std::numeric_limits<int>::max();
    for (int dy=-8;dy<=8;++dy) for (int dx=-8;dx<=8;++dx) {
        const Coord p=centre+Coord(dx,dy);
        if (!usable(p)) continue;
        int open=0;
        for (const Coord d:{Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) open+=usable(p+d);
        const int score=(std::abs(dx)+std::abs(dy))*2+(4-open)*4;
        if (score<bestScore) { bestScore=score; best=p; }
    }
    if (best.isValid()) traceDecision("harvest_army_rally",AITelemetry::Record().set("x",best.x).set("y",best.y)
        .set("working_harvesters",count));
    return best;
}

Coord QuantBot::openingAnchor() {
    const Coord base=findBaseCentre(getHouse()->getHouseID());
    if (base.isValid()) return base;
    // No yard yet: the MCV stands where the base is about to be built.
    const UnitBase* mcv=nullptr;
    for (const auto* unit:getUnitList())
        if (unit->getOwner()==getHouse() && unit->isActive() && unit->getItemID()==Unit_MCV
            && (!mcv || unit->getObjectID()<mcv->getObjectID())) mcv=unit;
    return mcv ? mcv->getLocation() : Coord::Invalid();
}

Coord QuantBot::openingForwardOffset(Coord anchor) const {
    Coord towards=Coord::Invalid();
    int closest=std::numeric_limits<int>::max();
    auto consider=[&](const ObjectBase* object) {
        if (!object || !object->getOwner() || !object->isActive()
            || object->getItemID()==Unit_Sandworm
            || object->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !object->isVisible(getHouse()->getTeamID()) || object->getLocation().isInvalid()) return;
        const int d=blockDistance(anchor,object->getLocation()).lround();
        if (d<closest) { closest=d; towards=object->getLocation(); }
    };
    for (const auto* unit:getUnitList()) consider(unit);
    for (const auto* structure:getStructureList()) consider(structure);
    // Nothing hostile is visible at game start. The middle of the map is the
    // direction opponents lie in and needs no knowledge we have not earned.
    if (towards.isInvalid()) towards=Coord(getMap().getSizeX()/2,getMap().getSizeY()/2);
    return towards-anchor;
}

/**
    The combat units a custom game starts with stand on the home rock, which is
    the only ground the opening build-out can use. Step each of them a short way
    towards the enemy onto free sand, so the yard keeps its building space.

    This is an opening decision only: it never moves workers, transports or
    saboteurs, never starts an assault, and leaves alone any unit that is
    already off the rock, under a human order, fighting or hunting. A unit with
    no safe sand within reach receives no order here at all: the opening rally
    sweep that used to move everything regardless has been withdrawn, so
    "nowhere to step" now means the unit is left exactly where it stands.
*/
void QuantBot::applyOpeningSpaceDispersal() {
    openingDispersal.clear();
    openingDispersalUntil=0;
    // Campaign missions keep their scripted opening, and a helper bot never
    // reorders the units its human is commanding.
    if (supportMode || gameMode!=GameMode::Custom
        || (currentGame && isCampaignGameType(currentGame->gameType))) return;
    const Coord anchor=openingAnchor();
    if (anchor.isInvalid()) return;
    refreshTacticalDanger();
    const Coord forward=openingForwardOffset(anchor);
    const bool hasForward=forward.x!=0 || forward.y!=0;
    const int scale=std::max(1,std::max(std::abs(forward.x),std::abs(forward.y)));

    // Worms are a hazard candidates keep clear of, but only the ones this
    // house can actually see: a site chosen from a worm nobody has spotted is
    // knowledge the AI has not earned.
    std::vector<Coord> worms;
    for (const auto* unit:getUnitList())
        if (unit->isActive() && unit->getItemID()==Unit_Sandworm
            && unit->isVisible(getHouse()->getTeamID())) worms.push_back(unit->getLocation());

    std::vector<const UnitBase*> movers;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || !unit->isActive() || !unit->isRespondable()
            || !unit->isAGroundUnit() || unit->isAFlyingUnit() || !unit->canAttack()) continue;
        const Uint32 item=unit->getItemID();
        if (item==Unit_MCV || item==Unit_Harvester || item==Unit_Carryall || item==Unit_Frigate
            || item==Unit_Saboteur || item==Unit_Sandworm) continue;
        if (humanControls(unit) || unit->wasForced() || unit->hasATarget()
            || unit->getAttackMode()==HUNT || unit->getAttackMode()==RETREAT) continue;
        if (!getMap().tileExists(unit->getLocation())) continue;
        // A unit already standing off the rock costs the base nothing.
        if (!getMap().getTile(unit->getLocation())->isRock()) continue;
        movers.push_back(unit);
    }
    if (movers.empty()) return;
    std::sort(movers.begin(),movers.end(),
        [](const UnitBase* a,const UnitBase* b) { return a->getObjectID()<b->getObjectID(); });

    // Ground actually connected to where the units stand. Traffic is ignored
    // because it moves; mountains and buildings are not.
    //
    // One shared flood fill would only prove that *some* mover can reach a
    // candidate. Label each connected piece of the window separately instead,
    // so a unit walled off behind its own neighbours is not handed a site on
    // the far side of that wall.
    const int window=kOpeningStepMax+4, span=window*2+1;
    const int x0=anchor.x-window, y0=anchor.y-window;
    auto inWindow=[&](Coord p) { return p.x>=x0 && p.y>=y0 && p.x<x0+span && p.y<y0+span; };
    auto slot=[&](Coord p) { return (p.y-y0)*span+(p.x-x0); };
    std::vector<int> component(static_cast<size_t>(span)*span,0);
    int components=0;
    std::vector<Coord> frontier;
    for (const auto* unit:movers) {
        const Coord from=unit->getLocation();
        if (!inWindow(from) || component[slot(from)]) continue;
        const int label=++components;
        component[slot(from)]=label;
        frontier.assign(1,from);
        for (size_t head=0;head<frontier.size();++head) {
            const Coord at=frontier[head];
            for (const Coord step:{Coord(0,-1),Coord(1,0),Coord(0,1),Coord(-1,0)}) {
                const Coord p=at+step;
                if (!inWindow(p) || component[slot(p)] || !getMap().tileExists(p)) continue;
                const auto* tile=getMap().getTile(p);
                if (tile->isMountain() || tile->hasAStructure()) continue;
                component[slot(p)]=label; frontier.push_back(p);
            }
        }
    }

    struct OpeningSite { Coord location; int score; };
    std::vector<OpeningSite> sites;
    for (int dy=-kOpeningStepMax;dy<=kOpeningStepMax;++dy)
        for (int dx=-kOpeningStepMax;dx<=kOpeningStepMax;++dx) {
            const int step=std::max(std::abs(dx),std::abs(dy));
            if (step<kOpeningStepMin) continue;
            const Coord p=anchor+Coord(dx,dy);
            if (!getMap().tileExists(p) || !inWindow(p) || !component[slot(p)]) continue;
            const auto* tile=getMap().getTile(p);
            // Free open ground only: rock is what is being freed, spice belongs
            // to the harvesters and a bloom kills whoever parks on it.
            if (tile->isRock() || tile->isSpice() || tile->isSpiceBloom() || tile->isRoad()
                || tile->hasAnObject()) continue;
            if (dangerAt(p)>0) continue;
            bool wormNear=false;
            for (const Coord worm:worms)
                if (blockDistance(p,worm).lround()<=kOpeningWormClearance) wormNear=true;
            if (wormNear) continue;
            const int ahead=hasForward ? (dx*forward.x+dy*forward.y)*4/scale : 0;
            if (hasForward && ahead<=0) continue;   // Towards the enemy, never back into the base.
            sites.push_back({p,step*3-ahead});
        }

    std::set<int> taken;
    int placed=0;
    for (const auto* unit:movers) {
        const Coord from=unit->getLocation();
        // Only ground this unit itself is standing on a connected piece of.
        const int reachable=inWindow(from) ? component[slot(from)] : 0;
        if (!reachable) continue;
        const OpeningSite* best=nullptr;
        int bestScore=std::numeric_limits<int>::max();
        for (const auto& site:sites) {
            if (component[slot(site.location)]!=reachable) continue;
            if (taken.count(slot(site.location)) || !unit->canPass(site.location.x,site.location.y)) continue;
            const int walk=blockDistance(from,site.location).lround();
            if (walk>kOpeningStepMax+kOpeningStepMin) continue;
            const int score=site.score*2+walk;
            const bool earlier=best && (site.location.y<best->location.y
                || (site.location.y==best->location.y && site.location.x<best->location.x));
            if (score<bestScore || (score==bestScore && earlier)) { bestScore=score; best=&site; }
        }
        if (!best) continue;   // No safe sand within reach: this unit keeps its position.
        doMove2Pos(unit,best->location.x,best->location.y,true);
        openingDispersal[unit->getObjectID()]=best->location;
        taken.insert(slot(best->location));
        ++placed;
        traceDecision("opening_space_step",AITelemetry::Record().set("unit",unit->getObjectID())
            .set("item",unit->getItemID()).set("from_x",from.x).set("from_y",from.y)
            .set("x",best->location.x).set("y",best->location.y)
            .set("steps",blockDistance(from,best->location).lround()));
    }
    if (placed>0) openingDispersalUntil=getGameCycleCount()+MILLI2CYCLES(kOpeningHoldMs);
    traceDecision("opening_space_dispersal",AITelemetry::Record().set("anchor_x",anchor.x)
        .set("anchor_y",anchor.y).set("forward_x",forward.x).set("forward_y",forward.y)
        .set("candidates",static_cast<int>(sites.size()))
        .set("units",static_cast<int>(movers.size())).set("moved",placed)
        .set("without_site",static_cast<int>(movers.size())-placed));
}

bool QuantBot::holdsOpeningPosition(const UnitBase* unit) const {
    if (openingDispersal.empty() || getGameCycleCount()>=openingDispersalUntil) return false;
    return unit && openingDispersal.count(unit->getObjectID())>0;
}

Coord QuantBot::findSquadRetreatLocation() {
	Coord newSquadRetreatLocation = Coord::Invalid();

	FixPoint closestDistance = FixPt_MAX;
	for (const StructureBase* pStructure : getStructureList()) {
		// if it is our building, check to see if it is closer to the squad rally point then we are
		if (pStructure->getOwner()->getHouseID() == getHouse()->getHouseID()) {
			Coord closestStructurePoint = pStructure->getClosestPoint(squadRallyLocation);
			FixPoint structureDistance = blockDistance(squadRallyLocation, closestStructurePoint);

			if (structureDistance < closestDistance) {
				closestDistance = structureDistance;
				newSquadRetreatLocation = closestStructurePoint;
			}
		}
	}

	return newSquadRetreatLocation;
}

Coord QuantBot::findBaseCentre(int houseID) {
	int buildingCount = 0;
	int totalX = 0;
	int totalY = 0;

	for (const StructureBase* pCurrentStructure : getStructureList()) {
		if (pCurrentStructure->getOwner()->getHouseID() == houseID && pCurrentStructure->getStructureSizeX() != 1) {
			// Lets find the center of mass of our squad
			buildingCount++;
			totalX += pCurrentStructure->getX();
			totalY += pCurrentStructure->getY();
		}
	}

	Coord baseCentreLocation = Coord::Invalid();

	if (buildingCount > 0) {
		baseCentreLocation.x = totalX / buildingCount;
		baseCentreLocation.y = totalY / buildingCount;
	}

	return baseCentreLocation;
}
const UnitBase* QuantBot::findLightRaiderTarget(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    // Do not send raiders across the whole map looking for an ideal target.
    // Their normal hunt logic handles that; this only exploits visible prey in
    // a local combat pocket.
    constexpr int searchRadius = 12;
    const UnitBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive() || !candidate->isVisible(getHouse()->getTeamID())
            || !QuantBotBuildPolicy::isLightRaiderPreferredTarget(candidate->getItemID())
            || !raider->canAttack(candidate)) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= searchRadius && distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

const UnitBase* QuantBot::findThreateningTank(const UnitBase* raider) const {
    if (!raider || !currentGame) return nullptr;

    const UnitBase* threat = nullptr;
    FixPoint threatDistance = FixPt_MAX;
    for (const UnitBase* candidate : getUnitList()) {
        if (!candidate || !candidate->isActive()
            || !QuantBotBuildPolicy::isArmoredTank(candidate->getItemID())
            || candidate->getOwner()->getTeamID() == getHouse()->getTeamID()
            || candidate->getTarget() != raider) continue;
        const FixPoint distance = blockDistance(raider->getLocation(), candidate->getLocation());
        if (distance <= candidate->getWeaponRange() && distance < threatDistance) {
            threat = candidate;
            threatDistance = distance;
        }
    }
    return threat;
}

double QuantBot::getProductionBuildingMultiplier(int itemID) const {
	switch (itemID) {
		case Structure_ConstructionYard:
			return 2.0;
		case Structure_RepairYard:
			return 2.0;
		case Structure_HeavyFactory:
			return 1.5;
		case Structure_Refinery:
			return 1.3;
		case Structure_StarPort:
			return 1.3;
		default:
			return 1.0;
	}
}

Coord QuantBot::findBestDeathHandTarget(int enemyHouseID) {
	const QuantBotConfig& config = getQuantBotConfig();
	const int myTeam = getHouse()->getTeamID();

    const Coord reactorTarget = findNuclearMissileTarget();
    if (reactorTarget.isValid()) return reactorTarget;

	const StructureBase* bestTarget = nullptr;
	double bestScore = -1.0;

	// Evaluate each enemy structure as a potential target
	for (const StructureBase* pCandidate : getStructureList()) {
		if (!pCandidate || !pCandidate->isActive()) {
			continue;
		}

		if (pCandidate->getOwner()->getHouseID() != enemyHouseID) {
			continue;
		}

		if (!pCandidate->isVisible(myTeam)) {
			continue;
		}

		// Get base priority from config
		const QuantBotConfig::TargetPriority& priority = config.getStructurePriority(pCandidate->getItemID());
		const int weight = priority.build + priority.target;
		if (weight <= 0) {
			continue;
		}

		// Apply production building multiplier
		const double productionMultiplier = getProductionBuildingMultiplier(pCandidate->getItemID());
		double score = static_cast<double>(weight) * productionMultiplier;

		// Center of mass calculation: add weighted value of nearby buildings
		// Death hand has 10-tile inaccuracy, so check 5-tile radius for nearby targets
		const Coord candidatePos = pCandidate->getLocation();
		constexpr int CHECK_RADIUS = 5;
		double centerOfMassBonus = 0.0;

		for (const StructureBase* pNearby : getStructureList()) {
			if (!pNearby || !pNearby->isActive() || pNearby == pCandidate) {
				continue;
			}

			if (pNearby->getOwner()->getHouseID() != enemyHouseID) {
				continue;
			}

			if (!pNearby->isVisible(myTeam)) {
				continue;
			}

			FixPoint distance = blockDistance(candidatePos, pNearby->getLocation());
			if (distance.toDouble() <= CHECK_RADIUS) {
				// Get this nearby building's priority weight
				const QuantBotConfig::TargetPriority& nearbyPriority = config.getStructurePriority(pNearby->getItemID());
				const int nearbyWeight = nearbyPriority.build + nearbyPriority.target;

				if (nearbyWeight > 0) {
					// Add distance-weighted contribution: closer buildings contribute more
					centerOfMassBonus += static_cast<double>(nearbyWeight) / (distance.toDouble() + 1.0);
				}
			}
		}

		// Final score is base score plus center of mass bonus
		score += centerOfMassBonus;

		if (score > bestScore) {
			bestScore = score;
			bestTarget = pCandidate;
		}
	}

	if (bestTarget != nullptr) {
		return bestTarget->getLocation();
	}

	// Fallback to center of base if no suitable target found
	return findBaseCentre(enemyHouseID);
}


Coord QuantBot::findSquadCenter(int houseID, bool preferHunting) {
    int count=0, x=0, y=0, huntingCount=0, huntingX=0, huntingY=0, homeCount=0, homeX=0, homeY=0;
    for (const auto* unit : getUnitList()) {
        if (!unit || !unit->getOwner() || unit->getOwner()->getHouseID()!=houseID
            || !unit->isActive() || !unit->isRespondable() || !unit->isAGroundUnit()
            || !unit->canAttack() || unit->getItemID()==Unit_Saboteur || unit->getItemID()==Unit_Sandworm
            || unit->getAttackMode()==RETREAT || reserveDamagedUnitForRepair(unit)) continue;
        ++count; x+=unit->getX(); y+=unit->getY();
        if (unit->getAttackMode()==HUNT) {
            ++huntingCount; huntingX+=unit->getX(); huntingY+=unit->getY();
        } else {
            ++homeCount; homeX+=unit->getX(); homeY+=unit->getY();
        }
    }
    // Reinforcements and home guards must not drag an attacking army backwards.
    if (preferHunting && huntingCount) return Coord(huntingX/huntingCount,huntingY/huntingCount);
    // The troops that stayed behind are the anchor for everyone still at home.
    if (!preferHunting) return homeCount ? Coord(homeX/homeCount,homeY/homeCount) : Coord::Invalid();
    return count ? Coord(x/count,y/count) : Coord::Invalid();
}


/**
 * Kite away from a threat while moving towards squad center.
 * Calculates a retreat position that maintains weapon range from the threat
 * while moving closer to the squad center.
 * 
 * @param pUnit The unit to move (must be non-null and respondable)
 * @param pThreat The threatening unit to kite away from (must be non-null)
 * @param desiredRange The desired distance to maintain from threat (typically weapon range)
 */
void QuantBot::kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange) {
	// Safety checks
	if (!pUnit || !pThreat || !pUnit->isRespondable() || !currentGameMap) {
		return;
	}

	// Don't kite if pathfinding is overloaded
	if (currentGame && currentGame->isPathQueueStressed()) {
		return;
	}

	// CRITICAL: Prevent command spam - only issue kite commands if unit is not currently moving
	// or if destination is significantly different (>2 tiles)
	Coord unitLocation = pUnit->getLocation();
	Coord unitDestination = pUnit->getDestination();

	if (pUnit->isMoving() && unitDestination.isValid() && unitDestination != unitLocation) {
		// Unit is already moving - check if it's moving away from the threat
		Coord threatLocation = pThreat->getLocation();
		FixPoint distDestToThreat = blockDistance(unitDestination, threatLocation);
		FixPoint distCurrentToThreat = blockDistance(unitLocation, threatLocation);

		// If already moving away from threat, don't interrupt
		if (distDestToThreat > distCurrentToThreat) {
			return;
		}
	}

	Coord threatLocation = pThreat->getLocation();

	// Calculate current distance to threat
	FixPoint distToThreat = blockDistance(unitLocation, threatLocation);

	// If already at or beyond desired range, no need to kite
	if (distToThreat >= desiredRange) {
		return;
	}

	// Short combat spacing follows the fighting army, never the home rally. A
	// unit that was not sent on the wave backs onto the body it belongs to.
	Coord squadCenter = findSquadCenter(getHouse()->getHouseID(),
        isCampaignGameType(currentGame->gameType) || gameMode!=GameMode::Custom
        || difficulty>Difficulty::Medium || pUnit->getAttackMode()==HUNT);

	// If no squad center, just move directly away from threat
	if (!squadCenter.isValid()) {
		squadCenter = unitLocation;
	}

	// Calculate direction vectors
	FixPoint dx_threat = unitLocation.x - threatLocation.x;
	FixPoint dy_threat = unitLocation.y - threatLocation.y;
	FixPoint dx_squad = squadCenter.x - unitLocation.x;
	FixPoint dy_squad = squadCenter.y - unitLocation.y;

	// Normalize threat direction (away from threat)
	FixPoint threatDist = FixPoint::sqrt(dx_threat * dx_threat + dy_threat * dy_threat);
	if (threatDist < 0.1_fix) {
		threatDist = 0.1_fix;  // Avoid division by zero
	}
	FixPoint nx_away = dx_threat / threatDist;
	FixPoint ny_away = dy_threat / threatDist;

	// Normalize squad direction (towards squad)
	FixPoint squadDist = FixPoint::sqrt(dx_squad * dx_squad + dy_squad * dy_squad);
	if (squadDist < 0.1_fix) {
		squadDist = 0.1_fix;
	}
	FixPoint nx_squad = dx_squad / squadDist;
	FixPoint ny_squad = dy_squad / squadDist;

	// Blend: 70% away from threat, 30% towards squad
	// This prioritizes safety while still moving towards friendlies
	FixPoint blend_x = nx_away * 0.7_fix + nx_squad * 0.3_fix;
	FixPoint blend_y = ny_away * 0.7_fix + ny_squad * 0.3_fix;

	// Normalize blended direction
	FixPoint blendDist = FixPoint::sqrt(blend_x * blend_x + blend_y * blend_y);
	if (blendDist < 0.1_fix) {
		blendDist = 0.1_fix;
	}
	blend_x /= blendDist;
	blend_y /= blendDist;

	// Calculate retreat distance proportional to threat proximity
	// Closer threats = longer retreat to reach weapon range edge
	FixPoint retreatDistance = std::min(FixPoint(2), desiredRange - distToThreat);
	if (retreatDistance < 1) {
		retreatDistance = 1;  // Minimum 1-tile retreat
	}

	// Calculate target position
	int targetX = lround(unitLocation.x + blend_x * retreatDistance);
	int targetY = lround(unitLocation.y + blend_y * retreatDistance);

	// Clamp to map boundaries with 1-tile safety margin
	int mapWidth = currentGameMap->getSizeX();
	int mapHeight = currentGameMap->getSizeY();
	targetX = std::max(1, std::min(mapWidth - 2, targetX));
	targetY = std::max(1, std::min(mapHeight - 2, targetY));

	// Issue move command (forced so unit actually retreats instead of immediately canceling to attack)
    const auto attackMode = pUnit->getAttackMode();
    doMove2Pos(pUnit, targetX, targetY, true);
    // Moving a Hunt unit implicitly changes it to Guard. Restore the mission
    // after the short dodge so it resumes attacking, not regrouping at home.
    if (attackMode == HUNT) doSetAttackMode(pUnit, HUNT);
    // A unit can reissue this exact retreat each AI tick. One record per
    // unit/threat encounter is enough to explain the tactical decision.
    const uint64_t kiteSignature = (uint64_t(pThreat->getObjectID()) << 16)
        | static_cast<uint64_t>(desiredRange & 0xffff);
    if (lastKiteTrace[pUnit->getObjectID()] != kiteSignature) {
        traceDecision("combat_kite", AITelemetry::Record().set("unit", pUnit->getObjectID())
            .set("target", pThreat->getObjectID()).set("distance", distToThreat.lround())
            .set("desired_range", desiredRange).set("x", targetX).set("y", targetY));
        lastKiteTrace[pUnit->getObjectID()] = kiteSignature;
    }
}

/**
 * Move a unit to the optimal squad position.
 * Chooses between actual squad center and squad rally point based on which is closer.
 * Only moves if the unit is outside the radius of both positions.
 * 
 * @param pUnit The unit to potentially move
 * @param squadRadius The acceptable radius around either position (unit won't move if within this radius)
 */
void QuantBot::moveToOptimalSquadPosition(const UnitBase* unit, FixPoint radius, int* orderBudget) {
    if (!unit || unit->getItemID()==Unit_Saboteur || !unit->isRespondable() || humanControls(unit) || unit->hasATarget()
        || defenceAssignments.count(unit->getObjectID())
        || unit->wasForced() || unit->isMoving() || unit->getAttackMode()==HUNT) return;
    // A unit that has just been stepped off the home rock stays off it for the
    // opening window, instead of being regrouped straight back onto the ground
    // the first buildings need.
    if (holdsOpeningPosition(unit)) return;
    // Easy and Medium keep their reserve at home. Only the units actually sent
    // on a wave advance, and this function never touches a hunting unit.
    const bool homeAnchored = !isCampaignGameType(currentGame->gameType)
        && gameMode==GameMode::Custom && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium);
    Coord regroup = (unit->getAttackMode()==RETREAT || homeAnchored)
        ? squadRallyLocation : findSquadCenter(getHouse()->getHouseID());
    if (regroup.isInvalid()) regroup = homeAnchored
        ? findSquadCenter(getHouse()->getHouseID(),false) : squadRallyLocation;
    if (regroup.isInvalid()) return;
    const_cast<UnitBase*>(unit)->setGuardPoint(regroup);
    if (unit->getAttackMode()!=RETREAT && unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
    if (blockDistance(unit->getLocation(),regroup)<=radius) return;
    // A queued path can exist before isMoving becomes true. Leave its destination
    // alone as well, instead of submitting a different slot on the next AI tick.
    const Coord destination=unit->getDestination();
    if (destination.isValid() && destination!=unit->getLocation()
        && blockDistance(destination,regroup)<=radius*2) return;
    int reactiveBudget=1;
    if (!orderBudget) orderBudget=&reactiveBudget;
    if (*orderBudget<=0 || currentGame->getPathRequestQueueSize()>150) return;
    const auto offset=SimpleArmyPolicy::rallyOffset(unit->getObjectID(),radius.lround(),[&](int x,int y) {
        const Coord p=regroup+Coord(x,y);
        return getMap().tileExists(p) && unit->canPass(p.x,p.y) && dangerAt(p)==0;
    });
    if (!offset) return;
    const Coord p=regroup+Coord(offset->first,offset->second);
    doMove2Pos(unit,p.x,p.y,false);
    --*orderBudget;
}

/**
	Set a rally / retreat location for all our military units.
	This should be near our base but within it
	The retreat mode causes all our military units to move
	to this squad rally location

*/
void QuantBot::retreatAllUnits() {

	// Set the new squad rally location
	squadRallyLocation = findSquadRallyLocation();
	squadRetreatLocation = findSquadRetreatLocation();

	// turning this off fow now
	//retreatTimer = MILLI2CYCLES(90000);

	// If no base exists yet, there is no retreat location
	if (squadRallyLocation.isValid() && squadRetreatLocation.isValid()) {
		for (const UnitBase* pUnit : getUnitList()) {
			if (pUnit->getOwner() == getHouse()
				&& pUnit->getItemID() != Unit_Carryall
				&& pUnit->getItemID() != Unit_Sandworm
				&& pUnit->getItemID() != Unit_Harvester
				&& pUnit->getItemID() != Unit_MCV
				&& pUnit->getItemID() != Unit_Frigate
                        && pUnit->getItemID() != Unit_Saboteur) {

				doSetAttackMode(pUnit, RETREAT);
			}
		}
	}
}


/**
	In dune it is best to mass military units in one location.
	This function determines a squad leader by finding the unit with the most central location
	Amongst all of a players units.

	Rocket launchers and Ornithopters are excluded from having this role as on the
	battle field these units should always have other supporting units to work with

*/
    void QuantBot::checkAllUnits() {
    AITelemetry::PerformanceScope perfScope("ai.checkAllUnits", getGameCycleCount(), getHouse()->getHouseID());
        // Safety check: if our house is null (e.g., during game cleanup), don't check units
        if (getHouse() == nullptr) {
            return;
        }

        refreshTacticalDanger();
        releaseLegacyGroundSquad();
        // The opening window is bounded: after it, ordinary regrouping owns
        // these units again.
        if (!openingDispersal.empty() && getGameCycleCount()>=openingDispersalUntil) openingDispersal.clear();
        if (!supportMode) squadRallyLocation = findSquadRallyLocation();
        const QuantBotConfig& config = getQuantBotConfig();
        const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
        // Recheck active base attacks before regrouping, including units already
        // moving away. Damage callbacks alone miss newly arrived defenders.
        for (const auto* intruder : getUnitList()) {
            if (!intruder->isActive() || !intruder->isVisible(getHouse()->getTeamID())
                || intruder->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
            // Aircraft are handled by the rescue below instead, which does not
            // wait for them to close to weapon range first.
            if (intruder->isAFlyingUnit()) continue;
            const auto* victim=intruder->getTarget();
            if (victim && victim->isAStructure() && victim->getOwner()==getHouse()
                && intruder->isInWeaponRange(victim)) scrambleUnitsAndDefend(intruder);
        }
        // Aircraft on any building we own, main base or outlying colony, on the
        // same cadence and before regrouping can claim the launchers again.
        defendStructuresFromAircraft();
        // Defence is sized on contact. No fixed reserve owns troops or prevents
        // the main body helping when a city/harvester is under attack.
        for (auto it=defenceAssignments.begin();it!=defenceAssignments.end();) {
            const auto* unit=dynamic_cast<const UnitBase*>(getObject(it->first));
            const auto* target=getObject(it->second);
            // A flying contact is answered by the anti-air rescue and ends with
            // it: the engine releases any flying target beyond weapon range, so
            // the forced-transit handling below would only park the defender.
            const bool airborne=unit && target && !unit->isAFlyingUnit()
                && target->isAUnit() && target->isAFlyingUnit();
            if (unit && unit->getItemID()==Unit_Saboteur) {
                it=defenceAssignments.erase(it);
                continue;
            }
            if (!unit || unit->getOwner()!=getHouse() || humanControls(unit)
                || unit->getItemID()==Unit_Ornithopter
                || !target || target->getHealth()<=0 || !target->isActive()
                || target->getOwner()->getTeamID()==getHouse()->getTeamID()
                || reserveDamagedUnitForRepair(unit) || unit->getAttackMode()==RETREAT
                || (airborne ? !airAttackContinues(unit,target) : !campaignDefensiveContact(unit,target))) {
                if (unit && unit->getOwner()==getHouse() && !humanControls(unit)
                    && unit->getAttackMode()==AREAGUARD) {
                    if (airborne && !reserveDamagedUnitForRepair(unit))
                        doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    else const_cast<UnitBase*>(unit)->setForced(false);
                }
                it=defenceAssignments.erase(it);
            } else if (airborne) {
                // Fire whenever the aircraft comes inside weapon range, and
                // otherwise leave the approach chosen for this rescue running.
                if (unit->isInWeaponRange(target) && unit->getTarget()!=target)
                    doAttackObject(unit,target,true);
                ++it;
            } else {
                // Keep the original contact during travel. On arrival restore
                // ordinary target selection and kiting within this district.
                if (unit->isInAttackRange(target)) {
                    const_cast<UnitBase*>(unit)->setForced(false);
                    // Retain the anchored self-defense permission until the
                    // attacker dies or leaves; wave enforcement runs each tick.
                    if (unit->getTarget()!=target) doAttackObject(unit,target,false);
                    ++it;
                } else {
                    if (unit->getTarget()!=target || !unit->wasForced())
                        doAttackObject(unit,target,true);
                    ++it;
                }
            }
        }
        int rallyOrdersRemaining=4;
        int combatCount=0;
        for (const auto* unit:getUnitList()) if (unit->getOwner()==getHouse() && unit->isActive()
            && unit->isAGroundUnit() && unit->canAttack()) ++combatCount;
        int rallyRadius=3;
        while (rallyRadius*rallyRadius*2<std::max(1,combatCount)) ++rallyRadius;
        for (auto it=defenceResponseCycles.begin();it!=defenceResponseCycles.end();)
            if (getGameCycleCount()-it->second>MILLI2CYCLES(30000)) it=defenceResponseCycles.erase(it); else ++it;
        // Use rally location instead of squad center to avoid constant destination changes
        Coord squadCenterLocation = squadRallyLocation;
        if(!supportMode) {
            tryLaunchOrnithopterStrike(diffSettings, config);
        }

        for (const UnitBase* pUnit : getUnitList()) {
            // Safety check: skip null units (can happen during unit destruction)
            if (pUnit == nullptr) {
                continue;
            }

            // Palace saboteurs already hunt autonomously. No tactical or army
            // controller may replace their orders, including forced orders.
            if (pUnit->getItemID()==Unit_Saboteur) continue;
            if (pUnit->getOwner()==getHouse() && humanControls(pUnit)) continue;
            // A captured Devastator is worth exactly one thing, so arm it at the first scan
            // that sees it - ahead of kiting, campaign holds, rally, escort and every other
            // role early return that would otherwise keep deferring the order. Support mode
            // and campaign waves manage their own units the same way. Human-controlled units
            // were released on the line above and are never armed automatically.
            if (pUnit->getOwner() == getHouse() && pUnit->isActive()
                && pUnit->getItemID() == Unit_Devastator
                && pUnit->getDeviationEpisode().credits()) {
                const auto* pDevastator = static_cast<const Devastator*>(pUnit);
                // Re-ordering never restarts the running fuse; skip the order once it is lit.
                if (!pDevastator->isArmedToDevastate()) {
                    doStartDevastate(pDevastator);
                    doSetAttackMode(pDevastator, HUNT);
                    traceDecision("captured_devastator_armed", AITelemetry::Record()
                        .set("unit", pUnit->getObjectID())
                        .set("original_house", pUnit->getOriginalHouseID())
                        .set("support_mode", supportMode)
                        .set("health", pUnit->getHealth().lround()));
                }
                continue;
            }
            // Combat spacing applies to defenders and escorts too, before their
            // strategic-role early return. Guard orders already leave targets alone.
            if (!supportMode && pUnit->getOwner() == getHouse()
                && (pUnit->getItemID() == Unit_Launcher || pUnit->getItemID() == Unit_Deviator)) {
                const auto* target = pUnit->getTarget();
                const int range = currentGame->objectData.data[pUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
                if (target && target->canAttack(pUnit) && QuantBotBuildPolicy::needsKiting(
                        blockDistance(pUnit->getLocation(), target->getLocation()).lround(), range,
                        difficulty == Difficulty::Easy, target->isAUnit() && !static_cast<const UnitBase*>(target)->isAFlyingUnit())) {
                    kiteAwayFromThreat(pUnit, target, range);
                    continue;
                }
            }

            if (campaignControlsUnit(pUnit)) continue;

            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive()
                && QuantBotBuildPolicy::isLightRaider(pUnit->getItemID())) {
                if (const UnitBase* tank = findThreateningTank(pUnit)) {
                    // A short dodge does not release an offensive slot. Keep
                    // custom hunters in their wave just like artillery kiting.
                    if (isCampaignGameType(currentGame->gameType) || pUnit->getAttackMode()!=HUNT)
                        doSetAttackMode(pUnit, AREAGUARD);
                    kiteAwayFromThreat(pUnit, tank, tank->getWeaponRange() + 2);
                    traceDecision("light_raider_evade", AITelemetry::Record().set("unit", pUnit->getObjectID())
                        .set("threat", tank->getObjectID()).set("reason", "tank_targeting_in_range")
                        .set("distance", blockDistance(pUnit->getLocation(), tank->getLocation()).lround())
                        .set("desired_range", tank->getWeaponRange() + 2));
                    continue;
                }
                if (!pUnit->wasForced() && pUnit->getAttackMode() == HUNT
                    && !(isCampaignEnemy() && difficulty<=Difficulty::Medium)) {
                    if (const UnitBase* prey = findLightRaiderTarget(pUnit);
                        prey && prey != pUnit->getTarget()) {
                        doAttackObject(pUnit, prey, false);
                        traceDecision("light_raider_target", AITelemetry::Record().set("unit", pUnit->getObjectID())
                            .set("target", prey->getObjectID()).set("target_item", prey->getItemID())
                            .set("distance", blockDistance(pUnit->getLocation(), prey->getLocation()).lround()));
                    }
                }
            }

            if (!supportMode && pUnit->getOwner() == getHouse() && pUnit->isAGroundUnit()
                && pUnit->isRespondable() && pUnit->isActive() && pUnit->canAttack()
                && pUnit->getItemID() != Unit_Harvester && pUnit->getItemID() != Unit_Saboteur
                && !reserveDamagedUnitForRepair(pUnit) && !pUnit->hasATarget() && !pUnit->wasForced()
                && pUnit->getAttackMode() != HUNT && pUnit->getAttackMode() != RETREAT
                && squadRallyLocation.isValid()) {
                moveToOptimalSquadPosition(pUnit,rallyRadius,&rallyOrdersRemaining);
                continue;
            }

            // Safety check: skip units with invalid owner
            if (pUnit->getOwner() == nullptr) {
                continue;
            }

		if (pUnit->getOwner() == getHouse()) {
                switch (pUnit->getItemID()) {
                case Unit_MCV: {
                    if (!campaignPermitsStructure(Structure_ConstructionYard)) break;
                    const MCV* pMCV = static_cast<const MCV*>(pUnit);
                    if (pMCV != nullptr) manageMcv(pMCV);
                } break;

                case Unit_Harvester: {
                    const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                    if(pHarvester != nullptr && pHarvester->isActive()) {
                        if (manageHarvesterSafety(pHarvester)) break;
                        // Existing check for early return with half spice
						if(getHouse()->getNumItems(Structure_Refinery) < 4
							&& getHouse()->getCredits() < 1000
							&& pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE/2) {
                            doReturn(pHarvester);
                        }

                        // Check if harvester is stuck: not moving for extended period
                        // (Regardless of what it THINKS it's doing - harvesting/returning/idle)
                        bool isMoving = pHarvester->isMoving();

                        if(!isMoving) {
                            // Harvester is not moving - increment stuck counter
                            idleHarvesterCounters[pHarvester->getObjectID()]++;
                            harvesterMovingCounters[pHarvester->getObjectID()] = 0; // Reset moving counter

                            // 10 seconds at 60 fps = 600 game cycles
                            if(idleHarvesterCounters[pHarvester->getObjectID()] >= 600) {
                                // Harvester has been stuck for 10 seconds - take action based on spice level
                                FixPoint spiceAmount = pHarvester->getAmountOfSpice();

                                // If harvester has significant spice (>300 or >40% full), tell it to return
                                if(spiceAmount > 300 || spiceAmount > (HARVESTERMAXSPICE * 2) / 5) {
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - forcing RETURN", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doReturn(pHarvester);
                                } else {
                                    // Low/no spice - reset to harvest mode
                                    SDL_Log("RESETTING STUCK HARVESTER: id=%d stuck for 10s with spice=%.1f - resetting to HARVEST", 
                                        pHarvester->getObjectID(), spiceAmount.toFloat());
                                    doSetAttackMode(pHarvester, HARVEST);
                                }
                                idleHarvesterCounters[pHarvester->getObjectID()] = 0; // Reset counter
                            }
                        } else {
                            // Harvester is moving - increment moving counter
                            harvesterMovingCounters[pHarvester->getObjectID()]++;

                            // Only reset stuck counter if continuously moving for 30+ cycles (0.5 seconds)
                            // This ignores brief jitter/animation frames
                            if(harvesterMovingCounters[pHarvester->getObjectID()] >= 30) {
                                if(idleHarvesterCounters[pHarvester->getObjectID()] > 0) {
                                    idleHarvesterCounters[pHarvester->getObjectID()] = 0;
                                }
                            }
                        }
                    }
                } break;

                case Unit_Carryall: {
                } break;

                case Unit_Frigate: {
                } break;

                case Unit_Sandworm: {
                } break;

                case Unit_Ornithopter: {
                    // Safe strike/defence planner owns targeting and patrol locations.
                } break;

                default: {
                    if (supportMode) {
                        break;
                    }

                    int squadRadius = lround(FixPoint::sqrt(getHouse()->getNumUnits()
                        - getHouse()->getNumItems(Unit_Harvester)
                        - getHouse()->getNumItems(Unit_Carryall)
                        - getHouse()->getNumItems(Unit_Ornithopter)
                        - getHouse()->getNumItems(Unit_Sandworm)
                        - getHouse()->getNumItems(Unit_MCV))) + 1;

                    // Safety check: ensure owner is valid before comparing
                    if (pUnit->getOwner() != nullptr && pUnit->getOwner()->getHouseID() != pUnit->getOriginalHouseID()) {
                        // Captured Devastators are armed centrally above, before any role or
                        // rally early return can divert them, so they never reach this switch.
                        if (pUnit->getItemID() == Unit_Harvester) {
                            const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
                            if (pHarvester->getAmountOfSpice() >= HARVESTERMAXSPICE / 5) {
                                doReturn(pHarvester);
                            }
                            else {
                                    doMove2Pos(pUnit, squadCenterLocation.x, squadCenterLocation.y, true);
                            }
                        }
                        else {
                            // Send deviated unit to squad centre with tight radius (force movement)
                            if (pUnit->getAttackMode() != AREAGUARD) {
                                doSetAttackMode(pUnit, AREAGUARD);
                            }

                            // Use small radius (2 tiles) to ensure deviated units actually move to squad
                            moveToOptimalSquadPosition(pUnit, 2,&rallyOrdersRemaining);
                        }
                    }
                    else if (pUnit->getItemID() != Unit_Ornithopter && pUnit->getItemID() != Unit_Saboteur && pUnit->getAttackMode() != HUNT && !pUnit->hasATarget() && !pUnit->wasForced()) {
                        if (pUnit->getAttackMode() == AREAGUARD && squadCenterLocation.isValid() && (gameMode != GameMode::Campaign)) {
							if (!pUnit->hasATarget()) {
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius,&rallyOrdersRemaining);
                            }
                        }
                        else if (pUnit->getAttackMode() == RETREAT) {
                            if (!pUnit->wasForced()) {
                                if (pUnit->getHealth() < pUnit->getMaxHealth()) {
                                    doRepair(pUnit);
                                }
                                // Move to optimal position (closer of squad center or rally point, only if outside radius)
                                moveToOptimalSquadPosition(pUnit, squadRadius + 2,&rallyOrdersRemaining);
                            }

                            // Check if we've reached the retreat position. An
                            // attack centroid is not a place to end a retreat.
                            Coord actualSquadCenter = findSquadCenter(getHouse()->getHouseID(),
                                isCampaignGameType(currentGame->gameType) || gameMode!=GameMode::Custom
                                || difficulty>Difficulty::Medium);
                            FixPoint distToSquadCenter = actualSquadCenter.isValid() ? 
                                blockDistance(pUnit->getLocation(), actualSquadCenter) : FixPt_MAX;
                            FixPoint distToRallyPoint = squadRallyLocation.isValid() ? 
                                blockDistance(pUnit->getLocation(), squadRallyLocation) : FixPt_MAX;

                            // If within radius of either, we've finished retreating
                            if (distToSquadCenter <= squadRadius + 2 || distToRallyPoint <= squadRadius + 2) {
                                // We have finished retreating back to the rally point
                                doSetAttackMode(pUnit, AREAGUARD);
                            }
                        }
                        else if (pUnit->getAttackMode() == GUARD
                            && ((pUnit->getDestination() != squadRallyLocation) || (blockDistance(pUnit->getLocation(), squadRallyLocation) <= squadRadius))) {
                            // A newly deployed unit has reached the rally point, or has been diverted => Change it to area guard
                            logDebug("UNIT GUARD->AREAGUARD: %s at (%d,%d)", 
                                getItemNameByID(pUnit->getItemID()).c_str(), 
                                pUnit->getLocation().x, pUnit->getLocation().y);
                            doSetAttackMode(pUnit, AREAGUARD);
                        }
                    }
                } break;
            }
        }
    }
}

Coord QuantBot::findFinishedRoadSite(const BuilderBase* yard) {
    const Uint32 id = yard->getObjectID(), cycle = getGameCycleCount();
    const auto retry = roadRedirectRetryCycle.find(id);
    if (retry != roadRedirectRetryCycle.end() && cycle < retry->second) return Coord::Invalid();
    const auto sites = cityRoadRepairSites();
    if (sites.empty()) {
        roadRedirectRetryCycle[id] = cycle + MILLI2CYCLES(5000);
        return Coord::Invalid();
    }
    roadRedirectRetryCycle.erase(id);
    return Coord(sites.front().first,sites.front().second);
}

std::vector<std::pair<int,int>> QuantBot::cityRoadRepairSites() {
    std::set<std::pair<int,int>> planned;
    for (const auto& entry : builderPlaceLocations)
        for (const Coord p : entry.second) planned.emplace(p.x,p.y);
    std::vector<CityRoadRepairPolicy::Footprint> buildings;
    for (const StructureBase* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isActive()) continue;
        const Coord p=structure->getLocation(), size=getStructureSize(structure->getItemID());
        buildings.push_back({p.x,p.y,size.x,size.y});
    }
    return CityRoadRepairPolicy::candidates(buildings,[&](int x,int y) {
        if (!getMap().tileExists(x,y) || planned.count({x,y})) return false;
        const Tile* tile=getMap().getTile(x,y);
        return !tile->isRoadConnection() && !tile->hasCityZone() && !tile->hasAGroundObject()
            && tile->isRock() && !tile->isMountain()
            && !overlapsReservedStructure(x,y,1,1)
            && getMap().okayToPlaceStructure(x,y,1,1,false,getHouse(),false,Structure_Road);
    },[&](int x,int y) {
        return getMap().tileExists(x,y) && getMap().getTile(x,y)->isRoadConnection();
    });
}

int QuantBot::queueCityRoadRepairs(const BuilderBase* yard, int limit) {
    if (limit <= 0) return 0;
    const auto sites = cityRoadRepairSites();
    int queued=0;
    AITelemetry::Record locations;
    for (const auto& site:sites) {
        if (queued>=limit) break;
        builderPlaceLocations[yard->getObjectID()].emplace_back(site.first,site.second);
        doProduceItem(yard,Structure_Road);
        locations.set(std::to_string(queued),AITelemetry::Record().set("x",site.first).set("y",site.second));
        ++queued;
    }
    if (queued) traceDecision("city_road_repair",AITelemetry::Record().set("builder",yard->getObjectID())
        .set("rule","idle_yard_road_gaps").set("segments_queued",queued).set("locations",locations));
    return queued;
}

void QuantBot::manageCityBuilding() {
    AITelemetry::PerformanceScope perfScope("ai.manageCityBuilding", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame) return;
    auto* citySim = currentGame->getCitySimulation();
    if (!citySim || !citySim->isInitialized()) return;
    if (!currentGameMap) return;

    Coord baseCenter = findBaseCentre(getHouse()->getHouseID());
    if (!baseCenter.isValid()) return;

    // Zone structures are now built through the Construction Yard build
    // order (see the Structure_ConstructionYard case in build()).  The old
    // tile-flag approach (CMD_CITY_PLACE_ZONE without a backing structure)
    // created phantom zones that runZoneGrowth() ignored (it requires an
    // actual structure object) and that blocked real zone placement.
    //
    // Road placement remains here: roads are tile-level and don't need the
    // Construction Yard pipeline.

    // Place roads in the gaps between zones/structures. A tile gets a road
    // when it is adjacent to a zone or structure on at least one side AND
    // adjacent to an existing road or zone on at least one side (keeps the
    // network continuous). Scan outward from base center.
    int roadsPlaced = 0;
    constexpr int MAX_ROADS_PER_ROUND = 8;
    constexpr int CITY_RADIUS = 20;

    static constexpr int dx4[] = { 0, 1, 0, -1 };
    static constexpr int dy4[] = { -1, 0, 1, 0 };

    // First repair a missing transport link. City growth checks real road
    // reachability (R -> C, C -> I, I -> R), so spreading local road stubs
    // cannot help two otherwise healthy districts that are disconnected.
    struct RoadLink { Coord from; Coord to; DuneCity::CityRole sourceRole; DuneCity::CityRole targetRole; };
    auto targetRoleFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::CityRole::Commercial;
            case DuneCity::CityRole::Commercial: return DuneCity::CityRole::Industrial;
            case DuneCity::CityRole::Industrial: return DuneCity::CityRole::Residential;
            default: return DuneCity::CityRole::None;
        }
    };
    auto zoneTypeFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::ZoneType::Residential;
            case DuneCity::CityRole::Commercial: return DuneCity::ZoneType::Commercial;
            case DuneCity::CityRole::Industrial: return DuneCity::ZoneType::Industrial;
            default: return DuneCity::ZoneType::None;
        }
    };
    auto perimeterRoads = [&](Coord zone) {
        static constexpr int pdx[] = {-1,0,1,2, -1,0,1,2, -1,2, -1,2};
        static constexpr int pdy[] = {-1,-1,-1,-1, 2,2,2,2, 0,0, 1,1};
        std::vector<Coord> roads;
        for (int i = 0; i < 12; ++i) {
            const int x = zone.x + pdx[i], y = zone.y + pdy[i];
            if (currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->isRoadConnection())
                roads.emplace_back(x, y);
        }
        return roads;
    };

    struct ZoneRoad { Coord zone; DuneCity::CityRole role; std::vector<Coord> roads; };
    std::vector<ZoneRoad> zoneRoads;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const auto role = DuneCity::getStructureCityRole(structure->getItemID());
        if (role == DuneCity::CityRole::None) continue;
        auto roads = perimeterRoads(structure->getLocation());
        if (!roads.empty()) zoneRoads.push_back({structure->getLocation(), role, std::move(roads)});
    }

    DuneCity::TrafficSimulation traffic;
    traffic.init(citySim);
    RoadLink bestLink{Coord::Invalid(), Coord::Invalid(), DuneCity::CityRole::None, DuneCity::CityRole::None};
    int bestLinkDistance = std::numeric_limits<int>::max();
    for (const auto& source : zoneRoads) {
        const auto targetRole = targetRoleFor(source.role);
        if (targetRole == DuneCity::CityRole::None
            || traffic.makeTraffic(source.zone.x, source.zone.y, zoneTypeFor(targetRole)) == 1) continue;
        for (const auto& target : zoneRoads) {
            if (target.role != targetRole) continue;
            for (const auto& from : source.roads) for (const auto& to : target.roads) {
                const int distance = std::abs(from.x - to.x) + std::abs(from.y - to.y);
                if (distance > 1 && distance <= DuneCity::kMaxTrafficDistance && distance < bestLinkDistance) {
                    bestLink = {from, to, source.role, targetRole};
                    bestLinkDistance = distance;
                }
            }
        }
    }

    std::set<std::pair<int, int>> scheduledRoads;
    if (bestLink.from.isValid()) {
        std::vector<Coord> route;
        auto appendLeg = [&](Coord& cursor, int target, bool horizontal) {
            const int step = ((horizontal ? target - cursor.x : target - cursor.y) >= 0) ? 1 : -1;
            while ((horizontal ? cursor.x : cursor.y) != target) {
                if (horizontal) cursor.x += step; else cursor.y += step;
                if (cursor != bestLink.to) route.push_back(cursor);
            }
        };
        Coord cursor = bestLink.from;
        appendLeg(cursor, bestLink.to.x, true);
        appendLeg(cursor, bestLink.to.y, false);

        for (const auto& road : route) {
            if (roadsPlaced >= MAX_ROADS_PER_ROUND) break;
            if (!currentGameMap->tileExists(road.x, road.y)) break;
            Tile* tile = currentGameMap->getTile(road.x, road.y);
            if (tile->isRoadConnection()) continue;
            if (tile->hasCityZone() || tile->hasAStructure() || !tile->isRock() || tile->isMountain()) break;
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) break;
            if (!scheduledRoads.emplace(road.x, road.y).second) continue;
            currentGame->getCommandManager().addCommand(Command(getPlayerID(), CMD_CITY_TOOL,
                static_cast<Uint32>(road.x), static_cast<Uint32>(road.y),
                static_cast<Uint32>(DuneCity::CityTool_Road)));
            ++roadsPlaced;
        }
        if (roadsPlaced > 0) {
            traceDecision("city_road_link", AITelemetry::Record()
                .set("source_role", static_cast<int>(bestLink.sourceRole))
                .set("target_role", static_cast<int>(bestLink.targetRole))
                .set("from_x", bestLink.from.x).set("from_y", bestLink.from.y)
                .set("to_x", bestLink.to.x).set("to_y", bestLink.to.y)
                .set("distance", bestLinkDistance).set("segments_queued", roadsPlaced));
        }
    }

    for (int r = 1; r <= CITY_RADIUS && roadsPlaced < MAX_ROADS_PER_ROUND; r++) {
        for (int angle = 0; angle < r * 8 && roadsPlaced < MAX_ROADS_PER_ROUND; angle++) {
            int ox, oy;
            int side = angle / (r * 2);
            int pos = angle % (r * 2);
            switch (side) {
                case 0: ox = -r + pos; oy = -r; break;
                case 1: ox = r; oy = -r + pos; break;
                case 2: ox = r - pos; oy = r; break;
                default: ox = -r; oy = r - pos; break;
            }

            int tx = baseCenter.x + ox;
            int ty = baseCenter.y + oy;

            if (tx < 0 || tx >= currentGameMap->getSizeX() || ty < 0 || ty >= currentGameMap->getSizeY()) continue;
            if (!currentGameMap->tileExists(tx, ty)) continue;

            Tile* tile = currentGameMap->getTile(tx, ty);
            // Skip tiles that already have road, zone, structure, or aren't buildable
            if (tile->isRoad() || tile->hasCityZone() || tile->hasAStructure()) continue;
            if (scheduledRoads.count({tx, ty}) != 0) continue;
            if (!tile->isRock() || tile->isMountain()) continue;
            // Allow rubble tiles (destroyed structures) — road clears the rubble
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) continue;

            bool nearStructure = false;
            bool nearRoadOrStructure = false;
            for (int d = 0; d < 4; d++) {
                int nx = tx + dx4[d];
                int ny = ty + dy4[d];
                if (nx < 0 || nx >= currentGameMap->getSizeX() || ny < 0 || ny >= currentGameMap->getSizeY()) continue;
                if (!currentGameMap->tileExists(nx, ny)) continue;
                const Tile* nb = currentGameMap->getTile(nx, ny);
                if (nb->hasCityZone() || nb->hasAStructure()) nearStructure = true;
                if (nb->isRoad() || nb->hasCityZone() || nb->hasAStructure()) nearRoadOrStructure = true;
            }

            // Place road if tile is next to a structure AND connects to
            // existing road network or another structure
            if (nearStructure && nearRoadOrStructure) {
                currentGame->getCommandManager().addCommand(
                    Command(getPlayerID(), CMD_CITY_TOOL,
                            static_cast<Uint32>(tx), static_cast<Uint32>(ty),
                            static_cast<Uint32>(1)));
                scheduledRoads.emplace(tx, ty);
                roadsPlaced++;
            }
        }
    }
}

// Supplemental network checkpoint state: normal disk saves intentionally rebuild these plans.
void QuantBot::saveObserverRuntime(OutputStream& s) const {
    const auto coord=[&](Coord v) { s.writeSint32(v.x); s.writeSint32(v.y); };
    s.writeUint32(lastPoliceBudgetReviewCycle);
    s.writeUint32(ixEligibleSinceCycle);
    s.writeUint32(palaceEligibleSinceCycle);
    s.writeUint32(rockSurveyCycle);
    s.writeUint32(refineryQueueSince);
    s.writeUint32(dangerUpdated);
    s.writeUint32(planningBuilder);
    s.writeUint32(placementCacheExcludedBuilder);
    s.writeUint32(cityReadyYardCount);
    s.writeSint32(availableBaseRock);
    s.writeSint32(availableBaseFootprints);
    s.writeSint32(cityBuildTimer);
    s.writeSint32(ornithopterStrikeTeam.minMembers);
    s.writeBool(planningCityProductionPlots);
    s.writeBool(baseProductionRoomBlocked);
    coord(rockExpansionSite);
    s.writeUint32(idleHarvesterCounters.size()); for(const auto& e : idleHarvesterCounters) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(harvesterMovingCounters.size()); for(const auto& e : harvesterMovingCounters) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(mcvSurveyCycles.size()); for(const auto& e : mcvSurveyCycles) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(roadRedirectRetryCycle.size()); for(const auto& e : roadRedirectRetryCycle) { s.writeUint32(e.first); s.writeUint32(e.second); }
    s.writeUint32(mcvExpansionSites.size()); for(const auto& e : mcvExpansionSites) { s.writeUint32(e.first); coord(e.second); }
    s.writeUint32(placementCache.size()); for(const auto& e : placementCache) { s.writeUint32(e.first); coord(e.second); }
    s.writeUint32(tacticalDanger.size()); for(auto v : tacticalDanger) s.writeUint32(v);
    s.writeUint32(harvesterDanger.size()); for(auto v : harvesterDanger) s.writeUint32(v);
    s.writeUint32(lossDanger.size()); for(auto v : lossDanger) s.writeUint32(v);
    s.writeUint32(factoryEnemyClearance.size()); for(auto v : factoryEnemyClearance) s.writeUint32(v);
    s.writeUint32(visibleHarvestLaunchers.size()); for(auto v : visibleHarvestLaunchers) s.writeUint32(v);
    s.writeUint32(visibleEnemyBases.size()); for(auto v : visibleEnemyBases) coord(v);
    s.writeUint32(builderPlaceLocations.size()); for(const auto& e : builderPlaceLocations) { s.writeUint32(e.first); s.writeUint32(e.second.size()); for(auto v : e.second) coord(v); }
    s.writeUint32(reservedStructures.size()); for(const auto& e : reservedStructures) { s.writeUint32(e.first); s.writeUint32(e.second.item); coord(e.second.location); }
    s.writeUint32(cityProductionPlots.size()); for(const auto& e : cityProductionPlots) { s.writeUint32(e.item); coord(e.location); }
    s.writeUint32(ornithopterStrikeTeam.targetId); s.writeUint32Set(ornithopterStrikeTeam.memberIds);
    s.writeUint32(harvesterSafety.size()); for(const auto& e : harvesterSafety) { s.writeUint32(e.first); s.writeUint32(e.second.nextCheck); s.writeUint32(e.second.retreatUntil); coord(e.second.lastLocation); coord(e.second.plannedDestination); s.writeBool(e.second.controlled); }
    s.writeUint32(unsafeFields.size()); for(const auto& e : unsafeFields) { coord(e.location); s.writeUint32(e.cycle); }
}

void QuantBot::loadObserverRuntime(InputStream& s) {
    const auto count=[&]() { auto n=s.readUint32(); if(n>262144) throw std::runtime_error("Oversized spectator AI state"); return n; };
    const auto coord=[&]() { Coord v; v.x=s.readSint32(); v.y=s.readSint32(); return v; };
    lastPoliceBudgetReviewCycle=s.readUint32();
    ixEligibleSinceCycle=s.readUint32();
    palaceEligibleSinceCycle=s.readUint32();
    rockSurveyCycle=s.readUint32();
    refineryQueueSince=s.readUint32();
    dangerUpdated=s.readUint32();
    planningBuilder=s.readUint32();
    placementCacheExcludedBuilder=s.readUint32();
    cityReadyYardCount=s.readUint32();
    availableBaseRock=s.readSint32();
    availableBaseFootprints=s.readSint32();
    cityBuildTimer=s.readSint32();
    ornithopterStrikeTeam.minMembers=s.readSint32();
    planningCityProductionPlots=s.readBool();
    baseProductionRoomBlocked=s.readBool();
    rockExpansionSite=coord();
    idleHarvesterCounters.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); idleHarvesterCounters[id]=s.readUint32(); }
    harvesterMovingCounters.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); harvesterMovingCounters[id]=s.readUint32(); }
    mcvSurveyCycles.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); mcvSurveyCycles[id]=s.readUint32(); }
    roadRedirectRetryCycle.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); roadRedirectRetryCycle[id]=s.readUint32(); }
    mcvExpansionSites.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); mcvExpansionSites[id]=coord(); }
    placementCache.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); placementCache[id]=coord(); }
    tacticalDanger.clear(); for(Uint32 n=count(); n; --n) tacticalDanger.push_back(s.readUint32());
    harvesterDanger.clear(); for(Uint32 n=count(); n; --n) harvesterDanger.push_back(s.readUint32());
    lossDanger.clear(); for(Uint32 n=count(); n; --n) lossDanger.push_back(s.readUint32());
    factoryEnemyClearance.clear(); for(Uint32 n=count(); n; --n) factoryEnemyClearance.push_back(s.readUint32());
    visibleHarvestLaunchers.clear(); for(Uint32 n=count(); n; --n) visibleHarvestLaunchers.push_back(s.readUint32());
    visibleEnemyBases.clear(); for(Uint32 n=count(); n; --n) visibleEnemyBases.push_back(coord());
    builderPlaceLocations.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& list=builderPlaceLocations[id]; for(Uint32 m=count(); m; --m) list.push_back(coord()); }
    reservedStructures.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& e=reservedStructures[id]; e.item=s.readUint32(); e.location=coord(); }
    cityProductionPlots.clear(); for(Uint32 n=count(); n; --n) { PlannedStructure e; e.item=s.readUint32(); e.location=coord(); cityProductionPlots.push_back(e); }
    ornithopterStrikeTeam.targetId=s.readUint32(); ornithopterStrikeTeam.memberIds.clear(); for(Uint32 n=count(); n; --n) ornithopterStrikeTeam.memberIds.insert(s.readUint32());
    harvesterSafety.clear(); for(Uint32 n=count(); n; --n) { auto id=s.readUint32(); auto& e=harvesterSafety[id]; e.nextCheck=s.readUint32(); e.retreatUntil=s.readUint32(); e.lastLocation=coord(); e.plannedDestination=coord(); e.controlled=s.readBool(); }
    unsafeFields.clear(); for(Uint32 n=count(); n; --n) { UnsafeField e; e.location=coord(); e.cycle=s.readUint32(); unsafeFields.push_back(e); }
}
