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

#include "QuantBotInternal.h"

using QuantBotInternal::mobileCombatItem;

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


QuantBot::QuantBot(House* associatedHouse, const std::string& playername, Difficulty difficulty, AIRole role)
	: Player(associatedHouse, playername), difficulty(difficulty), role(role) {

	// MULTIPLAYER FIX: Use deterministic stagger based on house ID instead of random
	// This prevents desync issues in multiplayer games
	buildTimer = (getHouse()->getHouseID() % 4) * 50;  // 0-150 cycles stagger

    const QuantBotConfig& config = botConfig();

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

	if (builderOnly()) {
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


bool QuantBot::hasExplicitUnitCountOverride() const {
    return currentGame != nullptr
        && currentGame->getGameInitSettings().getGameOptions().maximumNumberOfUnitsOverride >= 0;
}

bool QuantBot::ignoresUnitCountLimit() const {
    // Hard is unchanged: it always ignores the authored count ceiling.
    if (difficulty == Difficulty::Hard) return true;
    // Brutal honours an explicitly selected override. Override 0 is reported as
    // unlimited by House::getMaxUnits() itself, so nothing is lost there.
    return difficulty == Difficulty::Brutal && !hasExplicitUnitCountOverride();
}

bool QuantBot::overridesMilitaryValueCap() const {
    return difficulty == Difficulty::Brutal && hasExplicitUnitCountOverride();
}

int QuantBot::configuredCustomHarvesterLimit() const {
    // Custom games the house plays itself only: campaigns keep their scripted fleets and a
    // support controller follows its human partner.
    if (builderOnly() || gameMode != GameMode::Custom || currentGame == nullptr || currentGameMap == nullptr
        || isCampaignGameType(currentGame->gameType)) return 0;
    // Maps above 64x64 (the Large and Huge classes: Easy 8/15, Medium 15/25, Hard 40/40, Brutal
    // 100/50). The Small and Medium class values (2-10 workers) are campaign-era fleet sizes that
    // would starve every custom economy on a 64x64 map; there the spice budget alone decides, as before.
    const int mapsize = currentGameMap->getSizeX() * currentGameMap->getSizeY();
    if (mapsize <= 4096) return 0;
    const auto& settings = botConfig().getSettings(static_cast<int>(difficulty));
    const int configured = mapsize <= 16384 ? settings.harvesterLimitCustomLargeMap : settings.harvesterLimitCustomHugeMap;
    return openingCombatBudgetActive() ? std::min(configured, 4) : configured;
}

bool QuantBot::openingCombatBudgetActive() const {
    if (!botConfig().force.openingCombatBudget || difficulty < Difficulty::Hard || builderOnly()
        || gameMode != GameMode::Custom || !currentGame || !currentGameMap
        || isCampaignGameType(currentGame->gameType) || initialMilitaryValue < 0
        || initialItemCount[Structure_HeavyFactory] > 0 || customWave.launched != 0
        || currentGameMap->getSizeX() * currentGameMap->getSizeY() <= 4096) return false;
    const auto& factory = currentGame->objectData.data[Structure_HeavyFactory][getHouse()->getHouseID()];
    return factory.enabled && factory.techLevel <= currentGame->techLevel;
}

bool QuantBot::campaignCombatBudgetActive() const {
    if (!botConfig().force.campaignCombatBudget || difficulty < Difficulty::Hard || builderOnly()
        || !currentGame || !isCampaignGameType(currentGame->gameType) || !isAlliedWithHuman()
        || initialMilitaryValue < 0 || initialItemCount[Structure_HeavyFactory] > 0
        || campaignWave.launched != 0) return false;
    const auto& factory = currentGame->objectData.data[Structure_HeavyFactory][getHouse()->getHouseID()];
    return factory.enabled && factory.techLevel <= currentGame->techLevel;
}

int QuantBot::planningMilitaryBudget(int committedValue, int productionCash, int largestUnitValue) const {
    if (overridesMilitaryValueCap())
        return QuantBotBuildPolicy::rollingMilitaryBudget(committedValue, militaryValueLimit,
                                                          productionCash, largestUnitValue);
    // Custom Medium turns banked credits into army past its cap, so a won economy ends the game
    // (docs/ai-scenario-matrix.md, "Difficulty ladder"); at most half the cap, below Hard's.
    if (difficulty == Difficulty::Medium && autonomousCustomArmy())
        return QuantBotBuildPolicy::surplusMilitaryBudget(militaryValueLimit, productionCash);
    // M (SpendAtCap, test-only, Custom Hard/Brutal): banked credits above the reserve buy heavy
    // units beyond the limit, up to the limit again (QuantBotBuild.cpp keeps the other producers
    // at the limit).
    if (forceVariant(kForceSpendAtCap))
        return QuantBotBuildPolicy::surplusMilitaryBudget(militaryValueLimit, productionCash,
                                                          ForceConcentrationPolicy::kSurplusMaxPercent);
    return militaryValueLimit;
}

bool QuantBot::permitsPoliceReinforcement(int unitValue) const {
    if (difficulty != Difficulty::Brutal) return true;
    if (!currentGame || militaryValueLimit <= 0) return false;
    int value = 0;
    // Match the production allocator's military valuation, including new batch members.
    if (ModManager::instance().isTornieFamilyContentActive()) value = militaryUnitValue();
    else
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

	// Saves from before the role byte only recorded a builder in its class name.
	if (role == AIRole::Both && getPlayerclass().rfind("qBotSupport", 0) == 0) {
		role = AIRole::Builder;
		gameMode = GameMode::Custom;
		attackTimer = std::numeric_limits<Sint32>::max();
	}

    // Both co-controllers and separate human-allied houses develop an economy;
    // only opponents use the campaign enemy rebuild/pressure restrictions.
    if (isAlliedWithHuman() && gameMode == GameMode::Campaign) {
        gameMode = GameMode::Custom;
        initialMilitaryValue = -1;
        const auto& config = botConfig();
        attackTimer = builderOnly() ? std::numeric_limits<Sint32>::max()
            : isCampaignGameType(currentGame->gameType) ? 0
            : SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
                currentGame->getGameInitSettings().getRandomSeed(), getGameCycleCount(), getHouse()->getHouseID());
        logDebug("Human-allied house: using economy development instead of campaign enemy rebuild limits");
    }

    // Allies have no campaign opening grace. Discard a legacy opening timer
    // on load, but preserve the normal 60-second break between their attacks.
    if (!builderOnly() && difficulty != Difficulty::Defend && isAlliedWithHuman()
        && isCampaignGameType(currentGame->gameType) && attackTimer > MILLI2CYCLES(60000))
        attackTimer = 0;

    // The strategy (docs/ai-play-styles.md, "Strategies"): once, from the seed, house and config.
    pickStrategy();

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

			logDebug("Allow Campaign AI one Repair Yard (support: %s)", builderOnly() ? "yes" : "no");
		}

		// Calculate the total military value of the player
		initialMilitaryValue = 0;
		if (currentGame) {
            if (ModManager::instance().isTornieFamilyContentActive()) initialMilitaryValue = militaryUnitValue();
            else
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
	const QuantBotConfig& config = botConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

	// Log which config this QuantBot is using
	logDebug("=== QuantBot [%s - %s] Initialization ===", 
		getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())).c_str(),
		gameMode == GameMode::Campaign ? "Campaign" : "Custom");

	switch (gameMode) {
	case GameMode::Campaign: {
		// Use config values for campaign mode
        harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialRefineryBays();
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
                harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialRefineryBays();
				logDebug("  Mission 21+ override: Refineries=2, MilitaryValueLimit=10000");
			}
		}

		// Refinery top-up: Ensure AI has at least the minimum refineries for difficulty
        if (diffSettings.refineryMinimum > 0 && initialRefineryBays() < diffSettings.refineryMinimum) {
            int refineriesToAdd = diffSettings.refineryMinimum - initialRefineryBays();
            initialItemCount[Structure_Refinery] += refineriesToAdd;
            harvesterLimit = diffSettings.harvesterLimitPerRefineryMultiplier * initialRefineryBays();
			logDebug("  Refinery top-up: Had %d, topped up to %d (granted %d refineries)", 
				initialItemCount[Structure_Refinery] - refineriesToAdd, 
				diffSettings.refineryMinimum,
				refineriesToAdd);
		} else if (diffSettings.refineryMinimum > 0) {
			logDebug("  Refinery check: Has %d (minimum %d already met, no top-up needed)", 
				initialItemCount[Structure_Refinery], diffSettings.refineryMinimum);
		}

		// Apply the game options harvester ceiling if one is set and lower than the calculated limit
		const int harvesterCeiling = currentGame->getGameInitSettings().getGameOptions().harvesterCeiling();
		if (harvesterCeiling > 0 && harvesterCeiling < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterCeiling);
			harvesterLimit = harvesterCeiling;
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
						&& !QuantBotInternal::harvesterItem(pUnit->getItemID())
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

		// Apply the game options harvester ceiling if one is set and lower than the calculated limit
		const int harvesterCeiling = currentGame->getGameInitSettings().getGameOptions().harvesterCeiling();
		if (harvesterCeiling > 0 && harvesterCeiling < harvesterLimit) {
			logDebug("  Game Options Override: Reducing harvester limit from %d to %d", harvesterLimit, harvesterCeiling);
			harvesterLimit = harvesterCeiling;
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
	const QuantBotConfig& config = botConfig();
	const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));

    // No lobby ceiling (unchecked or 0) means no engine ceiling. Start with the remaining
    // resource budget, then let the house's economy/throughput planner choose its fleet.
    int baseHarvesterLimit = std::max(1,lastCalculatedSpice / 2000);

    if (isCampaignEnemy() && difficulty!=Difficulty::Brutal) {
        // Game Options supplies an engine ceiling, not permission for an Easy
        // campaign opponent to expand to a skirmish-sized harvester fleet.
        baseHarvesterLimit=std::max(0,diffSettings.harvesterLimitPerRefineryMultiplier
            * initialRefineryBays());
    }

    if (const int alliedLimit = campaignAllyHarvesterLimit(); alliedLimit > 0) baseHarvesterLimit = alliedLimit;
    // The explicit Game Options ceiling also constrains each AI target.
    if (getHouse()->getMaxHarvesters() > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, getHouse()->getMaxHarvesters());

    // Only opposing Brutal houses receive the seven-worker difficulty ceiling.
    if (const int ceiling = harvesterCountCeiling(); ceiling > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, ceiling);
    // A custom game's difficulty harvester limit for the map size (QuantBot Config.ini
    // HarvesterLimit*Map: on 96-128 maps Easy 8, Medium 15, Hard 40, Brutal 100) is a real ceiling:
    // until 1.0.849 the spice budget above overwrote it and every difficulty planned the same fleet
    // (docs/ai-scenario-matrix.md, "Difficulty separation").
    if (const int configured = configuredCustomHarvesterLimit(); configured > 0)
        baseHarvesterLimit = std::min(baseHarvesterLimit, configured);
	// Apply spice-based reduction for all modes and difficulties
	int maxHarvestersForSpice = lastCalculatedSpice / 2000;
	int oldLimit = harvesterLimit;
	harvesterLimit = std::min(baseHarvesterLimit, std::max(1, maxHarvestersForSpice));
    if (campaignCityEconomy()) harvesterLimit=std::min(harvesterLimit,campaignHarvesterTarget());
    if (campaignCombatBudgetActive()) harvesterLimit=std::min(harvesterLimit,4);

	// Log when the limit changes
	if (oldLimit != harvesterLimit) {
		logDebug("Harvester limit adjusted: %d -> %d (spice: %d, base: %d, mode: %s, diff: %d)", 
			oldLimit, harvesterLimit, lastCalculatedSpice, baseHarvesterLimit, 
			(gameMode == GameMode::Campaign) ? "Campaign" : "Custom", static_cast<int>(difficulty));
	}

    // A driving colonist's escort keeps its ring places every cycle, not only on the unit
    // pass: the places move with each tile the MCV enters (docs/formations.md).
    keepColonistEscortsInPlace();

    // Custom single-player splits the heavy pass into two stateless phases at the
    // same 50-cycle cadence: unit management on one cycle, base building half an
    // interval later, with houses spread evenly instead of bunched one cycle
    // apart. The phases are pure functions of (cycle, houseID) — no clock, frame
    // rate, thread completion or stored cursor — so a reloaded save recomputes
    // them from the cycle counter alone and the serialised buildTimer still
    // carries production continuation. Every other game type keeps the original
    // predicate and the original single-cycle ordering exactly.
    //
    // This is an intentional decision-timing change for custom single-player: a
    // house now evaluates the world at a different cycle than it used to, so its
    // choices and the match trajectory can differ from 1.0.793. Cadence,
    // ordering within a phase and every gameplay option are preserved.
    const bool phased = QuantBotSchedulePolicy::phasedSchedule(
        currentGame->gameType, getGameInitSettings().getGameType());
    const int scheduleHouse = getHouse()->getHouseID();
    const Uint32 scheduleCycle = getGameCycleCount();
    bool phasedBuildDue = false;
    if (phased) {
        // Player houses only: the Natives house has no AI, and the twelve-slot ring keeps every
        // house's decision cycle where it was before the Natives slot existed.
        const bool unitDue = QuantBotSchedulePolicy::unitPhaseDue(
            scheduleCycle, scheduleHouse, AIUPDATEINTERVAL, NUM_PLAYER_HOUSES);
        const bool buildDue = QuantBotSchedulePolicy::buildPhaseDue(
            scheduleCycle, scheduleHouse, AIUPDATEINTERVAL, NUM_PLAYER_HOUSES);
        if (!unitDue && !buildDue) return;
        phasedBuildDue = buildDue;
    } else if (!QuantBotSchedulePolicy::legacyDue(scheduleCycle, scheduleHouse, AIUPDATEINTERVAL)) {
		// we are not updating this AI player this cycle
		return;
	}

    // The remembered enemy army (docs/formations.md "Round 5"; the defence candidates E, G, A,
    // R and U and the late-game target choices H and Z read it), before anything this pass consults it.
    if (forceVariant(kForceEstimate | kForceGarrison | kForceStrengthWaves | kForceReserve | kForceThreatTurrets
                     | kForceFinishWeakest | kForceMassAssault | kForceFinishLeading) || style().counterBps > 0
        || (strategyMix() && strategyMix()->counter)) updateEnemySightings();
    // Army posture is a house-level decision that both phases consult: the unit
    // phase recalls and holds troops with it, and the build phase sizes the front
    // battery with it. It is therefore evaluated on every pass this house
    // actually takes, at the unchanged cadence, before the phase split. Nothing
    // about which cycles a house evaluates on has changed.
    updateArmyPosture();

    if (phased) {
        if (phasedBuildDue) {
            // The WHOLE original timer block is here. Running only build()
            // on this phase while leaving the decrement on the unit phase would
            // advance buildTimer twice per interval and double the cadence.
            AITelemetry::PerformanceScope phaseScope("ai.phase.build",
                scheduleCycle, scheduleHouse);
            const int militaryValue = militaryUnitValue();
            if (fighterOnly()) {
                fighterPass();
            }
            else if (buildTimer <= 0) {
                build(militaryValue);
            }
            else {
                buildTimer -= AIUPDATEINTERVAL;
            }
            return;
        }
        AITelemetry::log().performance(scheduleCycle,scheduleHouse,"ai.phase.unit",1,-1,false);
    }

    updateHarvesterStrikeTelemetry();
	// Calculate the total military value of the player
	const int militaryValue = militaryUnitValue();

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
    // After this pass's unit orders: whoever ended up idle on a planned site is asked off it.
    if (!fighterOnly()) keepPlannedSitesClear();

	// Phased custom single-player handled the whole timer block on its own build
	// phase above and returned; reaching here means this is the unit phase (or a
	// legacy mode running the original combined pass).
	if (!phased) {
		if (fighterOnly()) {
			fighterPass();
		}
		else if (buildTimer <= 0) {
			build(militaryValue);
		}
		else {
			buildTimer -= AIUPDATEINTERVAL;
		}
	}

	if (!builderOnly()) {
		if (attackTimer <= 0) {
			attack(militaryValue);
		} else {
			attackTimer -= AIUPDATEINTERVAL;
		}
	} else {
		attackTimer = std::numeric_limits<Sint32>::max();
	}

	// A Fighter never builds city zones or services.
	if (!fighterOnly()) {
		if (cityBuildTimer <= 0) {
			manageCityBuilding();
			cityBuildTimer = AIUPDATEINTERVAL * 10;
		} else {
			cityBuildTimer -= AIUPDATEINTERVAL;
		}
	}
}


uint64_t QuantBot::traceDecision(const std::string& event, AITelemetry::Record details) const {
    if (!AITelemetry::log().enabled()) return 0;
    details.set("state_id", telemetryState);
    return AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), event, details);
}

void QuantBot::onObjectWasBuilt(const ObjectBase* pObject) {
    if (pObject && pObject->getItemID() == Structure_RocketTurret) ++forceStats.turretsBuilt;
    if (pObject) traceDecision("object_built", AITelemetry::Record().set("item", pObject->getItemID())
        .set("object", pObject->getObjectID()).set("x", pObject->getLocation().x).set("y", pObject->getLocation().y));
}


void QuantBot::onDecrementStructures(int itemID, const Coord& location) {
    if (currentGame
            && itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall)
        recentStructureLosses.push_back({location, getStructureSize(itemID), getGameCycleCount(), static_cast<Uint32>(itemID)});
    if (itemID == Structure_RocketTurret) ++forceStats.turretsLost;
    dangerUpdated = std::numeric_limits<Uint32>::max();
    traceDecision("structure_lost", AITelemetry::Record().set("item", itemID).set("x", location.x).set("y", location.y));
}


/// When we take losses we should hold off from attacking for longer...
void QuantBot::onDecrementUnits(int itemID) {
    traceDecision("unit_lost", AITelemetry::Record().set("item", itemID));
    // Material mobile-combat attrition for the posture machine. Only our own
    // mobile combat units count: transports, workers, MCVs, saboteurs, worms and
    // ambient traffic are economic or incidental losses and must never be able
    // to make the whole house withdraw.
    //
    // Limitation, deliberate and documented: this hook carries only the lost
    // item's id, not the unit's original house, so the price comes from our own
    // house's table. For our own losses that is the correct table in every case
    // except a captured unit of another house, where it is the closest figure
    // available without widening an engine interface.
    if (currentGame != nullptr && mobileCombatItem(static_cast<Uint32>(itemID))) {
        const int price = std::max(0,
            currentGame->objectData.data[itemID][getHouse()->getHouseID()].price);
        attrition.lostCost += price;
        // The quiet clock every resume path is measured against.
        lastMaterialLossCycle = getGameCycleCount();
    }
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer += MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 30 / (static_cast<Uint8>(difficulty) + 1));
		//logDebug("loss ");
			retreatTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 20);
	}
}


/// When we get kills we should re-attack sooner...
void QuantBot::onIncrementUnitKills(int itemID) {
    // Legacy statistics also count friendly fire; the separate hostile-death
    // callback below supplies the recovery ledger.
	if (itemID != Unit_Trooper && itemID != Unit_Infantry) {
		//attackTimer -= MILLI2CYCLES(currentGame->objectData.data[itemID][getHouse()->getHouseID()].price * 15);
		//logDebug("kill ");
	}
}

void QuantBot::onHostileUnitKilled(Uint32 itemID, Uint32 originalHouseID) {
    // A worm or crime trooper (the Natives) is no trade with an opposing army.
    if (!currentGame || originalHouseID >= NUM_HOUSES || isNativesHouse(int(originalHouseID))
        || !mobileCombatItem(itemID)) return;
    attrition.killCost += std::max(0, currentGame->objectData.data[itemID][originalHouseID].price);
}

/// A destroyed hostile building is what an assault on a base buys, so it is a kill in the
/// posture machine's trade, at its purchase price. Counting only mobile kills made every
/// attack on a turret-defended base read as a bad trade: the army withdrew from bases with
/// no army left and grinding them out took forty minutes (quantbot_all_against_atreides).
void QuantBot::onHostileStructureDestroyed(Uint32 itemID, Uint32 originalHouseID) {
    if (!currentGame || originalHouseID >= NUM_HOUSES || !isStructure(itemID)) return;
    attrition.killCost += std::max(0, currentGame->objectData.data[itemID][originalHouseID].price);
}

void QuantBot::onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) {
	const ObjectBase* pDamager = getObject(damagerID);

	if (pDamager == nullptr || pDamager->getOwner() == getHouse() || pObject->getItemID() == Unit_Sandworm || pObject->getItemID() == Unit_Saboteur) {
		return;
	}

    // If the human has attacked us then its time to start fighting back... unless its an attack on a special unit
    // Don't trigger with fremen or saboteur
    bool bPossiblyOwnFremen = (pObject->getOwner()->getHouseID() == HOUSE_ATREIDES) && (pObject->getItemID() == Unit_Trooper) && (currentGame->techLevel > 7);
    // A worm or a crime trooper (the Natives) is no player opening hostilities.
    if(gameMode == GameMode::Campaign && !pDamager->getOwner()->isAI() && !pDamager->getOwner()->isNatives()
        && !campaignAIAttackFlag && !bPossiblyOwnFremen && (pObject->getItemID() != Unit_Saboteur)) {
        campaignAIAttackFlag = true;
    }
    // A confirmed hit on a building or worker we own makes the attacker a legal
    // air defence contact for a short grace, so the reload between volleys does
    // not read as the attack having stopped. Bounded: pruned on every air pass.
    if (!builderOnly() && !autonomousAircraftRaids() && damage>0
        && pObject->getOwner()==getHouse() && pObject->getHealth()>0
        && (pObject->isAStructure() || QuantBotInternal::harvesterItem(pObject->getItemID())
            || pObject->getItemID()==Unit_RebelHarvester)
        && pDamager->getOwner()!=nullptr
        && pDamager->getOwner()->getTeamID()!=getHouse()->getTeamID()) {
        assetAttackerContacts[damagerID]={getGameCycleCount(),pObject->isAStructure()};
    }
    if (pObject->isAStructure()) {
        // Repair is the builder's spending; a Fighter only defends the building.
        if (!fighterOnly()) doRepair(pObject);
        // no point scrambling to defend a missile
        if(pDamager->getItemID() != Structure_Palace) {
            scrambleUnitsAndDefend(pDamager,false,pObject);
        }

	}
	else if (!builderOnly() && pObject->isAGroundUnit()) {
		const GroundUnit* pGroundUnit = static_cast<const GroundUnit*>(pObject);

		if (pGroundUnit->isAwaitingPickup()) {
			return;
		}
        // A hit on a hunt party's member is contact (Blob waves): a gathering, marching or
        // reforming party unblobs into the fight before the per-unit reactions below.
        if (damage>0 && pDamager->getOwner()->getTeamID()!=getHouse()->getTeamID())
            huntPartyMemberHit(pGroundUnit, pDamager);

        const bool autonomousAttack = engineHuntAttack(pGroundUnit);
		// Stop him dead in his tracks if he's going to rally point. An MCV keeps
		// driving: a colonist halted under fire is the one that does not arrive.
		if (!autonomousAttack && unitRole(pGroundUnit, {UnitRole::Human}) == UnitRole::Free && pGroundUnit->wasForced()
			&& !QuantBotInternal::harvesterItem(pGroundUnit->getItemID()) && pGroundUnit->getItemID() != Unit_MCV) {
			doMove2Pos(pGroundUnit,
				pGroundUnit->getLocation().x,
				pGroundUnit->getLocation().y,
				false);
		}

        if (isCampaignEnemy() && damage>0 && campaignCombatUnit(pGroundUnit)
            && pGroundUnit->canAttack(pDamager) && unitRole(pGroundUnit, {UnitRole::Repair}) == UnitRole::Free
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

		if (QuantBotInternal::harvesterItem(pGroundUnit->getItemID())) {
			// Always keep Harvesters away from harm
			// Defend the harvester!
			const Harvester* pHarvester = static_cast<const Harvester*>(pGroundUnit);
			if (pHarvester->isActive()) {
				// A Palace is a missile's launcher, not a contact: never scramble to it (vanilla
				// m22: a Death Hand on a harvester sent the army into the Sardaukar base).
				if (pDamager->getItemID() != Structure_Palace) scrambleUnitsAndDefend(pDamager,false,pObject);
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
		else if (pGroundUnit->getItemID() == Unit_MCV) {
			// A colonist under fire: its escort turns on the attacker.
			for (const auto& entry : colonistEscorts()) {
				if (entry.second != pGroundUnit->getObjectID()) continue;
				const auto* escort = dynamic_cast<const UnitBase*>(getObject(entry.first));
				if (escort && unitRole(escort, {UnitRole::Human}) == UnitRole::Free && escort->canAttack(pDamager)
					&& escort->getTarget() != pDamager)
					doAttackObject(escort, pDamager, false);
			}
		}
		else if (autonomousAttack && (pGroundUnit->getItemID() == Unit_Launcher
			|| pGroundUnit->getItemID() == Unit_EliteLauncher)) {
			// A hit identifies the attacker even if targeting changed between AI
			// passes, but distance still decides: only an enemy that is already
			// too close makes a hunting launcher break contact, and it keeps
			// Hunt and its place in the tracked army while it does.
			escapeCloseThreatWhileHunting(pGroundUnit, pDamager);
		}
		else if ((pGroundUnit->getItemID() == Unit_Launcher
			|| pGroundUnit->getItemID() == Unit_Deviator)
			&& !builderOnly() && !autonomousAttack) {
			// Keep Launchers/Deviators away from harm when taking damage (not in support mode)
			doSetAttackMode(pGroundUnit, AREAGUARD);
			int weaponRange = currentGame->objectData.data[pGroundUnit->getItemID()][getHouse()->getHouseID()].weaponrange;
			kiteAwayFromThreat(pGroundUnit, pDamager, weaponRange);

		}
		else if (!autonomousAttack && QuantBotBuildPolicy::isLightRaider(pGroundUnit->getItemID())
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
					// Test-only Blob variant: a damaged member stays with a party that has not met
					// a fight yet (its repair run would strip the blob mid-march).
					&& !((blobVariantOverride & kBlobKeepDamaged) && huntPartyOf(pGroundUnit->getObjectID())
						&& huntPartyOf(pGroundUnit->getObjectID())->phase != BlobWavePolicy::Phase::Fight)
					) {
					if (huntPartyOf(pGroundUnit->getObjectID())) ++blobStats.repairs;
					doRepair(pGroundUnit);
				}

				// Rotate unit backwards if it is taking damage if it is softer. A hunt party's
				// member stays where its party wants it.
				else if (!autonomousAttack && !huntPartyOf(pGroundUnit->getObjectID())
						&& !forceHeld(pGroundUnit->getObjectID())
						&& pGroundUnit->getItemID() != Unit_Devastator
						&& pGroundUnit->getItemID() != Unit_SiegeTank) {
					doSetAttackMode(pGroundUnit, AREAGUARD);
					moveToOptimalSquadPosition(pGroundUnit, 6);  // 6 tile radius
				}


			}
		}
	}
}

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
    factoryEnemyClearanceStale = true;
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

const std::vector<int>& QuantBot::factoryClearance() const {
    if (factoryEnemyClearanceStale) {
        TacticalSafetyPolicy::enemyClearanceInto(tacticalDanger, getMap().getSizeX(), getMap().getSizeY(), factoryEnemyClearance);
        factoryEnemyClearanceStale = false;
    }
    return factoryEnemyClearance;
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

int QuantBot::initialRefineryBays() const {
    return initialItemCount[Structure_Refinery] + (ModManager::instance().isTornieFamilyContentActive()
        ? 2 * initialItemCount[Structure_Doublefinery] + initialItemCount[Structure_Worfinery] : 0);
}

int QuantBot::militaryUnitValue() const {
    // Preserve the original house-priced sum outside the family. Tornie armies
    // include non-contiguous custom IDs and the technology of each produced unit.
    // Both scheduling modes read the same live value, without cached state.
    int militaryValue = 0;
    if (currentGame) {
        if (ModManager::instance().isTornieFamilyContentActive()) {
            for (const auto* unit : getUnitList())
                if (unit->getOwner() == getHouse() && QuantBotBuildPolicy::militaryItem(unit->getItemID())
                    && !isAmbientUnit(unit->getItemID()))
                    militaryValue += currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price;
        } else {
            for (Uint32 i = Unit_FirstID; i <= Unit_LastID; i++) {
                if (i != Unit_Carryall && i != Unit_Harvester && i != Unit_MCV && i != Unit_Sandworm)
                    militaryValue += getHouse()->getNumItems(i) * currentGame->objectData.data[i][getHouse()->getHouseID()].price;
            }
        }
    }
    // A Fighter's readiness counts only the army it may command: units the human is ordering
    // would be excluded from the wave and launch it undersized.
    if (currentGame && fighterOnly()) {
        for (const auto& order : manualUnitOrders) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(order.first));
            if (unit && unit->getOwner() == getHouse() && humanControls(unit)
                && unit->getItemID() != Unit_Carryall && !QuantBotInternal::harvesterItem(unit->getItemID())
                && unit->getItemID() != Unit_MCV && unit->getItemID() != Unit_Sandworm)
                militaryValue -= currentGame->objectData.data[unit->getItemID()][ModManager::instance().isTornieFamilyContentActive()
                    ? unit->getProductionHouseID() : getHouse()->getHouseID()].price;
        }
    }
    return std::max(0, militaryValue);
}

void QuantBot::onHumanUnitOrder(Uint32 id) {
    manualUnitOrders[id] = getGameCycleCount();
    groundSquad.erase(id);
    defenceAssignments.erase(id);
}

bool QuantBot::humanControls(const UnitBase* unit) const {
    // A captured technology unit's persistent capture order is its own authority.
    if (ModManager::instance().isTornieFamilyContentActive() && unit->getAttackMode() == SABOTAGE) return true;
    const auto it = manualUnitOrders.find(unit->getObjectID());
    return it != manualUnitOrders.end() && (getGameCycleCount()-it->second < MILLI2CYCLES(120000)
        || unit->wasForced() || unit->isMoving() || unit->hasATarget());
}

QuantBot::UnitRole QuantBot::unitRole(const UnitBase* unit, UnitRoles among) const {
    const Uint32 id = unit->getObjectID();
    if (among.contains(UnitRole::Human) && humanControls(unit)) return UnitRole::Human;
    if (among.contains(UnitRole::Repair) && reserveDamagedUnitForRepair(unit)) return UnitRole::Repair;
    if (among.contains(UnitRole::BaseDefence) && baseDefenceAssignment(unit)) return UnitRole::BaseDefence;
    if (among.contains(UnitRole::LiveDefence) && activeDefenceAssignment(unit)) return UnitRole::LiveDefence;
    if (among.contains(UnitRole::Defence) && defenceAssignments.count(id)) return UnitRole::Defence;
    if (among.contains(UnitRole::Wave) && customWave.members.count(id)) return UnitRole::Wave;
    // Guards are picked from units that are not escorts in the same cycle, so the
    // two never overlap and this order cannot change the answer. Guards come
    // first because that is the order the controllers derive the two caches in.
    if (among.contains(UnitRole::ColonyGuard) && recoveryActive() && colonyGuardPosts().count(id))
        return UnitRole::ColonyGuard;
    if (among.contains(UnitRole::Escort) && colonistEscorts().count(id)) return UnitRole::Escort;
    return UnitRole::Free;
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

Coord QuantBot::findBaseCentre(int houseID) const {
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
