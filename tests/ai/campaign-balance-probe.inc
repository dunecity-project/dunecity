// Included only by run-campaign-balance.py, never by the shipped game.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Network/WorkshopGameContent.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <players/QuantBotConfig.h>
#include <units/HarvesterHelpers.h>
#include <memory>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "starport-economy-probe.inc"
#include "city-placement-probe.inc"
#include "controls-probe.inc"
#include "stats-ui-probe.inc"
#include "sourceforge-regressions-probe.inc"
#include "harvester-safety-probe.inc"
#include "shared-spending-probe.inc"
#include "police-placement-probe.inc"
#include "police-budget-probe.inc"
#include "police-reinforcement-probe.inc"
#include "city-growth-probe.inc"
#include "no-spice-city-probe.inc"
#include "growth-installment-probe.inc"
#include "mcv-deployment-probe.inc"
#include "opening-economy-probe.inc"
#include "army-movement-probe.inc"
#include "army-recovery-probe.inc"
#include "army-recovery-continuation-probe.inc"
#include "whole-army-dispatch-probe.inc"
#include "launcher-kiting-probe.inc"
#include "air-defense-probe.inc"
#include "custom-attack-probe.inc"
#include "custom-city-cap-probe.inc"
#include "factory-recovery-probe.inc"
#include "strategic-site-probe.inc"
#include "radar-power-probe.inc"
#include "campaign-stats-probe.inc"
#include "campaign-pressure-probe.inc"
#include "campaign-nuclear-probe.inc"
#include "campaign-defence-probe.inc"
#include "campaign-pacing-probe.inc"
#include "campaign-repair-probe.inc"
#include "campaign-helper-economy-probe.inc"
#include "city-income-probe.inc"
#include "credit-storage-probe.inc"
#include "credit-capture-probe.inc"
#include "refund-provenance-probe.inc"
#include "campaign-city-economy-probe.inc"
#include "no-population-victory-probe.inc"
#include "degradation-probe.inc"
#include "spice-income-probe.inc"
#include "preplace-queue-probe.inc"
#include "sidebar-grid-probe.inc"
#include "formation-wave-measure.inc"
#include "wave-formation-probe.inc"
#include "blob-wave-probe.inc"
#include "force-concentration-probe.inc"
#include "formation-duel.inc"
#include "probe-digest.inc"
#include "elimination-match.inc"
#include "vanilla-match.inc"
#include "../performance/game-perf.inc"
#include "scenario-match.inc"
#include "civ-wars-match.inc"
#include <FileClasses/INIFile.h>
#include "../AchievementsTestCase/AchievementRuntimeProbe.inc"
#include "tornie-campaign-audit.inc"
#include "trooper-support-probe.inc"
#include "zone-power-probe.inc"
#include "tech-production-probe.inc"
#include "simple-tech-saving-probe.inc"
#include "research-lab-continuation-probe.inc"
#include "research-lab-ai-probe.inc"

int runCampaignBalanceProbe() {
    const auto probeStart = ProbeDigest::Clock::now();
    const int level = std::atoi(std::getenv("BALANCE_LEVEL"));
    const int minutes = std::atoi(std::getenv("BALANCE_MINUTES"));
    const auto seed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
    const std::string partner = std::getenv("BALANCE_PARTNER");
    const std::string partnerClass = partner == "hard" ? "qBotHard" : partner == "medium" ? "qBotMedium"
        : partner == "brutal" ? "qBotBrutal" : "qBotEasy";
    const std::string enemy = std::getenv("BALANCE_ENEMY") ? std::getenv("BALANCE_ENEMY") : "easy";
    std::string enemyClass = enemy == "hard" ? "qBotHard" : enemy == "medium" ? "qBotMedium"
        : enemy == "brutal" ? "qBotBrutal" : "qBotEasy";
    if(std::getenv("BALANCE_ENEMY_AI") && std::string(std::getenv("BALANCE_ENEMY_AI")) == "ai-player")
        enemyClass = enemy == "hard" ? "AIPlayerHard" : enemy == "medium" ? "AIPlayerMedium" : "AIPlayerEasy";
    // The campaign menu's "scenario settings" enemy (HouseChoiceMenu kEnemyAIClasses).
    if(std::getenv("BALANCE_ENEMY_AI") && std::string(std::getenv("BALANCE_ENEMY_AI")) == "campaign")
        enemyClass = "CampaignAIPlayer";
    const int percent = std::atoi(std::getenv("BALANCE_ATTACK_PERCENT"));
    const auto humanHouse=static_cast<HOUSETYPE>(std::getenv("BALANCE_HOUSE") ? std::atoi(std::getenv("BALANCE_HOUSE")) : HOUSE_HARKONNEN);
    const std::string requestedMod=std::getenv("BALANCE_MOD") ? std::getenv("BALANCE_MOD") : "vanilla";
    if(!ModManager::instance().setActiveMod(requestedMod)
       || ModManager::instance().getActiveModName()!=requestedMod)
        throw std::runtime_error("Requested probe mod did not activate: "+requestedMod);
    // A scenario-matrix game takes its rules from its mod, as a new Custom Game or campaign does.
    const bool scenario=std::getenv("BALANCE_SCENARIO")!=nullptr;
    const bool civWars=std::getenv("BALANCE_CIV_WARS")!=nullptr;
    if(civWars && requestedMod!="CivWars")
        throw std::runtime_error("Generated Civilization Wars needs the exact CivWars mod");
    if(std::getenv("BALANCE_DEGRADATION_PROBE") || scenario || civWars)
        settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    getQuantBotConfig().easy.attackForceMilitaryValueRatio = percent / 100.0f;
    if (const char* shape = std::getenv("BALANCE_WAVE_FORMATION")) {
        const int parsed = QuantBotConfig::parseWaveFormation(shape, INT_MIN);
        if (parsed == INT_MIN) throw std::runtime_error(std::string("Unknown wave formation: ") + shape);
        getQuantBotConfig().waveFormation = parsed;
    }
    const std::string civStrategy=CivWarsMatch::configureStrategy(civWars);
    if(!civStrategy.empty())
        SDL_Log("PROBE_CONFIG: civ_wars=%d civ_strategy=%s global_civ_strategy=%d config_hash=%s",
            int(civWars),civStrategy.c_str(),int(getQuantBotConfig().civWarsStrategy),
            getQuantBotConfig().getConfigHash().c_str());
    SDL_Log("CAMPAIGN_BALANCE_WAVE_FORMATION: %s", QuantBotConfig::waveFormationName(getQuantBotConfig().waveFormation));
    settings.general.playerName = "Campaign balance observer";
    settings.network.metaServer = "http://127.0.0.1:1/";
    settings.gameOptions.fogOfWar = false;
    if (std::getenv("BALANCE_ROCKET_TURRETS_NEED_POWER"))
        settings.gameOptions.rocketTurretsNeedPower=std::atoi(std::getenv("BALANCE_ROCKET_TURRETS_NEED_POWER"))!=0;
    settings.gameOptions.immortalHumanPlayer = false;
    if (std::getenv("BALANCE_CONCRETE_REQUIRED"))
        settings.gameOptions.concreteRequired=std::atoi(std::getenv("BALANCE_CONCRETE_REQUIRED"))!=0;
    if (std::getenv("BALANCE_HARVESTER_LIMIT"))
        settings.gameOptions.maximumNumberOfHarvestersOverride=std::atoi(std::getenv("BALANCE_HARVESTER_LIMIT"));
    if (std::getenv("BALANCE_PACING_PROBE")) settings.gameOptions.maximumNumberOfHarvestersOverride=100;
    // A whole [Game Options] section, as a player's Dune City.ini stores it (the
    // all-against-Atreides fixture is Stefan's own). Applied last, so it wins.
    if (const char* options = std::getenv("BALANCE_GAME_OPTIONS")) {
        const INIFile optionsFile(options);
        if (!optionsFile.hasSection("Game Options")) throw std::runtime_error("No [Game Options] in BALANCE_GAME_OPTIONS");
        applyGameOptionsFromConfig(optionsFile, "Game Options", settings.gameOptions);
    }
    if(std::getenv("BALANCE_ACHIEVEMENTS_PROBE")) {
        runAchievementRuntimeProbe();
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_TROOPER_SUPPORT_PROBE")) {
        runTrooperSupportProbe();
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_RESEARCH_LAB_AI_PROBE")) {
        runResearchLabAIProbe(requestedMod.c_str(), partnerClass.c_str());
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_SIMPLE_TECH_SAVING_PROBE")) {
        runSimpleTechSavingProbe(requestedMod.c_str(), partnerClass.c_str());
        return MENU_QUIT_DEFAULT;
    }
    GameInitSettings init(humanHouse, settings.gameOptions, level);
    // Scenario matrix: a campaign mission other than the level's first (levels 2-8 have three).
    if (const char* mission = std::getenv("BALANCE_MISSION")) {
        init.mission = std::atoi(mission); // Test-only access, provided by -fno-access-control.
        init.filename = GameInitSettings::getScenarioFilename(humanHouse, init.mission);
    }
    const char* customMap=std::getenv("BALANCE_CUSTOM_MAP");
    if(customMap) {
        std::ifstream file(customMap);
        if(!file)throw std::runtime_error("Cannot read custom map");
        const std::string contents((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        init=GameInitSettings(customMap,contents,true,settings.gameOptions);
    }
    // Scenario matrix: every house's team and controller, and optionally a generated Random
    // Skirmish map (one start per side) instead of a map file.
    const std::vector<ScenarioMatch::Side> scenarioSides=ScenarioMatch::sidesFromEnv();
    const char* skirmishSpec=std::getenv("BALANCE_SKIRMISH");
    bool constructionYardOnly=false;
    if(skirmishSpec) {
        if(customMap || scenarioSides.empty()) throw std::runtime_error("A skirmish scenario needs sides and no custom map");
        const ScenarioMatch::Skirmish skirmish=ScenarioMatch::parseSkirmish(skirmishSpec,static_cast<int>(scenarioSides.size()));
        constructionYardOnly=skirmish.constructionYardOnly;
        init=ScenarioMatch::makeSkirmishInit(skirmish,settings.gameOptions);
        if(std::getenv("BALANCE_TORNIE_COLOURED_SPICE"))
            init.filedata = tornieColouredSpiceFixture(init.getFiledata());
    }
    if(!scenarioSides.empty() && !customMap && !skirmishSpec) throw std::runtime_error("Scenario sides need a map");
    // Formation duel: a generated Random Skirmish 1v1 instead of a campaign level or map file.
    const bool duel=std::getenv("BALANCE_DUEL_MAP_SIZE")!=nullptr;
    std::vector<FormationDuel::Side> duelSides;
    if(duel) {
        duelSides=FormationDuel::sidesFromEnv();
        init=FormationDuel::makeInit(std::atoi(std::getenv("BALANCE_DUEL_MAP_SIZE")),
            static_cast<Uint32>(std::stoul(std::getenv("BALANCE_DUEL_MAP_SEED"))),duelSides,enemyClass,settings.gameOptions);
    }
    // Vanilla match: the custom map's slots, one side (QuantBot flags or AI Player) per house; or a
    // generated free-for-all Random Skirmish map with one start per side (vanilla-match.py --skirmish).
    const std::vector<FormationDuel::Side> matchSides=VanillaMatch::sidesFromEnv();
    const bool skirmishFfa=std::getenv("BALANCE_SKIRMISH_FFA_SIZE")!=nullptr;
    if(!matchSides.empty() && (!(customMap || skirmishFfa) || duel)) throw std::runtime_error("A vanilla match needs a map");
    if(skirmishFfa)
        init=VanillaMatch::makeSkirmishInit(std::atoi(std::getenv("BALANCE_SKIRMISH_FFA_SIZE")),
            static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SKIRMISH_MAP_SEED"))),matchSides,enemy,
            std::getenv("BALANCE_MOD") ? std::getenv("BALANCE_MOD") : "vanilla",settings.gameOptions);
    if(civWars) init=CivWarsMatch::makeInit(seed,partnerClass,enemyClass,settings.gameOptions);
    init.randomSeed = seed; // Test-only access, provided by -fno-access-control.
    // Continue a saved match instead (its own seed, roster and options).
    const char* loadSave = std::getenv("BALANCE_LOAD_SAVE");
    if (loadSave) {
        // Headless continuations must fail closed on unavailable pinned content,
        // never hang in the interactive Workshop-download dialog.
        const auto bytes = readCompleteFile(loadSave);
        IMemoryStream input(bytes.data(), bytes.size());
        GameInitSettings::HouseInfoList savedHouses;
        auto saved = GameInitSettings::readSaveSetup(input, savedHouses);
        WorkshopGameContent::resolveMod(saved, false);
        init = GameInitSettings(loadSave);
    }
    const bool localSpectator=std::getenv("BALANCE_LOCAL_SPECTATOR")!=nullptr;
    init.setLocalSpectator(localSpectator);
    init.setMultiplePlayersPerHouse(!localSpectator);
    std::vector<std::pair<int,int>> roster;
    if(!scenarioSides.empty() && !loadSave) {
        for(const auto& side : scenarioSides) roster.emplace_back(side.house, side.team);
    } else if(customMap && !loadSave) {
        std::istringstream input(std::getenv("BALANCE_ROSTER") ? std::getenv("BALANCE_ROSTER") : "");
        std::string slot;
        while(std::getline(input,slot,',')) {
            const auto colon=slot.find(':');
            if(colon==std::string::npos) throw std::runtime_error("Invalid test roster");
            roster.emplace_back(std::stoi(slot.substr(0,colon)),std::stoi(slot.substr(colon+1)));
        }
        if(roster.empty()) throw std::runtime_error("Custom test requires a roster");
    } else if(!civWars && !duel && !skirmishFfa && !loadSave) {
        for(int h=0;h<NUM_HOUSES;++h)
            if(isHouseAvailable(static_cast<HOUSETYPE>(h))) roster.emplace_back(h,h==humanHouse ? 1 : 2);
    }
    int rosterRow = 0;
    for (const auto& [h,team] : roster) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h), team);
        if (h==humanHouse && !localSpectator) info.addPlayerInfo({settings.general.playerName,"HumanPlayer"});
        // The pre-placement and sidebar grid probes drive the human house alone: a partner
        // AI would order and place from the same yards.
        if (!((std::getenv("BALANCE_PREPLACE_PROBE") || std::getenv("BALANCE_SIDEBAR_GRID_PROBE")) && h==humanHouse)) {
            std::string playerClass = h==humanHouse ? partnerClass : enemyClass;
            if (!matchSides.empty()) {
                const auto side = std::find_if(matchSides.begin(), matchSides.end(), [h=h](const auto& s) { return s.house == h; });
                if (side == matchSides.end()) throw std::runtime_error("A roster house has no vanilla match side");
                playerClass = VanillaMatch::playerClass(*side, enemy);
            }
            if (!scenarioSides.empty()) {
                const auto side = std::find_if(scenarioSides.begin(), scenarioSides.end(), [h=h](const auto& s) { return s.house == h; });
                playerClass = side->playerClass;
            }
            info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)),playerClass});
        }
        // Give every row a different delivery income factor, as a host would in the lobby, so
        // the probe can check which house ended up with which - including that no row picked
        // up a neighbour's rate. Matches SpiceIncomeProbe::assignedFactors.
        if(std::getenv("BALANCE_SPICE_INCOME_PROBE"))
            info.spiceIncomeMultiplier = SpiceIncome::kMin + (rosterRow % SpiceIncome::kChoiceCount);
        ++rosterRow;
        init.addHouseInfo(info);
    }
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    // Suppress the interactive briefing and public match analytics at setup.
    // Restore ordinary AI/command execution before advancing any game cycles.
    game->bReplay = true;
    {
        CivWarsMatch::MeasuredFactories measuredFactories(civWars);
        game->initGame(init);
    }
    if(localSpectator && (!game->isObserving() || !game->getGameInitSettings().isLocalSpectator()))
        throw std::runtime_error("Custom diagnostic did not create the real local observer");
    // This diagnostic can retain a full hour of every house's decisions.
    // The normal game capture limit is deliberately unchanged.
    if(std::getenv("BALANCE_CAPTURE_MIB"))
        AITelemetry::log().limit=std::stoull(std::getenv("BALANCE_CAPTURE_MIB"))*1024*1024;
    game->bReplay = false;
    game->initializeGameLoop();
    // Headless: nothing reads the per-unit phase timing (DuneCity-Performance.log), and its
    // clock reads cost about 5% of a busy match. BALANCE_UNIT_PHASE_TIMING=1 keeps it.
    game->frameTiming.unitPhases = std::getenv("BALANCE_UNIT_PHASE_TIMING") != nullptr;
    if(civWars) {
        CivWarsMatch::applyTeamStrategy(*game,civStrategy);
        CivWarsMatch::reportSetup(*game,partner,enemy,civStrategy);
    }
    if(std::getenv("BALANCE_DEGRADATION_PROBE")) {
        runStructureDegradationProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_HARVESTER_SAFETY_PROBE")) {
        runHarvesterSafetyProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_SOURCEFORGE_PROBE")) {
        runSourceforgeRegressionsProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_CONTROLS_PROBE")) {
        runControlsProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if(std::getenv("BALANCE_STATS_UI_PROBE")) {
        runStatsUiProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CITY_CAMPAIGN_PROBE")) {
        runCampaignCityEconomyProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_NO_POPULATION_VICTORY_PROBE")) {
        runNoPopulationVictoryProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_SPICE_INCOME_PROBE")) {
        // Takes the owning pointer: it saves the match, replaces the Game with a reload and
        // measures the resumed delivery on the loaded one.
        runSpiceIncomeProbe(game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_PREPLACE_PROBE")) {
        // Takes the owning pointer: it saves and reloads the match, then loads a 9854 save.
        runPreplaceQueueProbe(game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_SIDEBAR_GRID_PROBE")) {
        runSidebarGridProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CREDIT_STORAGE_PROBE")) {
        runCreditStorageProbe(*game);runCreditCaptureProbe(*game);runRefundProvenanceProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CITY_INCOME_PROBE")) {
        runCityIncomeProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CITY_PLACEMENT_PROBE")) {
        runCityPlacementProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_OPENING_ECONOMY_PROBE")) {
        runOpeningEconomyProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_POLICE_PLACEMENT_PROBE")) {
        runPolicePlacementProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_POLICE_BUDGET_PROBE")) {
        runPoliceBudgetProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_POLICE_REINFORCEMENT_PROBE")) {
        runPoliceReinforcementProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CITY_GROWTH_PROBE")) {
        runCityGrowthProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_ZONE_POWER_PROBE")) {
        runZonePowerProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_ZONE_POWER_CONTINUATION")) {
        runZonePowerContinuation(*game,minutes);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_NO_SPICE_CITY_PROBE")) {
        runNoSpiceCityProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_GROWTH_INSTALLMENT_PROBE")) {
        runGrowthInstallmentProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_MCV_DEPLOYMENT_PROBE")) {
        runMcvDeploymentProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_SHARED_SPENDING_PROBE")) {
        runSharedSpendingProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_AIR_DEFENSE_PROBE")) {
        runAirDefenseProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_ARMY_PROBE")) {
        runArmyMovementProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_RECOVERY_PROBE")) {
        runArmyRecoveryProbe(*game);
        if (!std::getenv("BALANCE_BATTERY_CLEARANCE_ONLY")) runArmyRecoveryContinuationProbe(game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_WHOLE_ARMY_PROBE")) {
        runWholeArmyDispatchProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_WAVE_FORMATION_PROBE")) {
        runWaveFormationProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_BLOB_WAVE_PROBE")) {
        runBlobWaveProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_FORCE_PROBE")) {
        runForceConcentrationProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_LAUNCHER_KITING_PROBE")) {
        runLauncherKitingProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_CUSTOM_ATTACK_PROBE")) {
        runCustomAttackProbe(*game);runCustomCityCapProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_RADAR_PROBE")) {
        runRadarPowerProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_NUCLEAR_PROBE")) {
        runCampaignNuclearProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_REPAIR_PROBE")) {
        runCampaignRepairProbe(*game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_PACING_PROBE")) {
        runCampaignPacingProbe(*game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_DEFENCE_PROBE")) {
        runCampaignDefenceProbe(*game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_PRESSURE_PROBE")) {
        runCampaignPressureProbe(*game);
        game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_STATS_PROBE")) {
        runCampaignStatsProbe(*game, level);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_HELPER_ECONOMY_PROBE")) {
        runCampaignHelperEconomyProbe(*game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    // Same-build config comparisons, including the strategic-site regression's negative control,
    // a continued savegame and a custom-map match (switch off/on on the same position).
    if (scenario || loadSave || customMap || std::getenv("BALANCE_STRATEGIC_SITE_PROBE"))
        if (const char* style = std::getenv("BALANCE_SCENARIO_STYLE"); style && *style)
            for (int h = 0; h < NUM_HOUSES; ++h)
                if (House* house = game->getHouse(h))
                    for (const auto& player : house->getPlayerList())
                        if (auto* bot = dynamic_cast<QuantBot*>(player.get())) FormationDuel::applyStyle(*bot, style);
    if (std::getenv("BALANCE_RESEARCH_LAB_CONTINUATION")) {
        runResearchLabContinuation(*game, minutes);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_STRATEGIC_SITE_PROBE")) {
        runStrategicSiteProbe(*game);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_FACTORY_RECOVERY_PROBE")) {
        runFactoryRecoveryProbe(*game);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_STARPORT_PROBE")) {
        runStarportEconomyProbe(*game);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        return MENU_QUIT_DEFAULT;
    }
    const TornieCampaignAudit tornieAudit(*game);
    const Uint32 end = MILLI2CYCLES(minutes * 60000);
    std::string outcome;
    const long long setupMs = ProbeDigest::millisecondsSince(probeStart);
    const auto loopStart = ProbeDigest::Clock::now();
    const Uint32 firstCycle = game->getGameCycleCount();
    if (civWars) {
        outcome = CivWarsMatch::run(*game, end);
    } else if (duel) {
        FormationDuel::applyShapes(*game, duelSides);
        FormationWaveMeasure::run(*game, end, [&]() {
            ProbeDigest::periodic(*game);
            return FormationDuel::decided(*game, duelSides);
        });
        FormationDuel::report(*game, duelSides);
    } else if (std::getenv("BALANCE_FORMATION_MEASURE")) {
        FormationWaveMeasure::run(*game, end, [&]() { ProbeDigest::periodic(*game); return false; });
    } else if (scenario) {
        outcome = ScenarioMatch::run(*game, end, scenarioSides.empty(), humanHouse, constructionYardOnly);
    } else if (!matchSides.empty()) {
        VanillaMatch::run(*game, end, matchSides);
    } else if (std::getenv("BALANCE_ELIMINATION_MATCH")) {
        outcome = EliminationMatch::run(*game, end, humanHouse);
    } else if (std::getenv("BALANCE_PERF")) {
        // Frame-cost measurement (tests/performance/game-perf.py): BALANCE_PERF_CYCLES cycles from
        // wherever the match starts (a loaded save continues from its own cycle).
        const GameInitSettings& setup = game->getGameInitSettings();
        const auto& options = setup.getGameOptions();
        SDL_Log("GAME_PERF_SETUP: seed=%u cycle=%u map=%s options=%s units_override=%d harvesters_override=%d "
                "concrete=%d explored=%d turrets_need_power=%d worms_respawn=%d",
            setup.getRandomSeed(), game->getGameCycleCount(), setup.getFilename().c_str(), options.getHash().c_str(),
            options.maximumNumberOfUnitsOverride, options.maximumNumberOfHarvestersOverride, options.concreteRequired,
            options.startWithExploredMap, options.rocketTurretsNeedPower, options.sandwormsRespawn);
        for (const auto& info : setup.getHouseInfoList())
            for (const auto& player : info.playerInfoList)
                SDL_Log("GAME_PERF_SETUP: house=%d team=%d player=%s class=%s", static_cast<int>(info.houseID), info.team,
                    player.playerName.c_str(), player.playerClass.c_str());
        // Save investigations need physical legal origins separately from the
        // AI's reserved plots. This opt-in snapshot does not advance the game
        // or run a planner before the measured continuation.
        if (const char* auditHouse=std::getenv("BALANCE_BUILD_SITE_AUDIT")) {
            const int h=std::stoi(auditHouse);
            if (const auto* house=game->getHouse(h)) {
                AITelemetry::Record row,sites,market,players,builders,items;
                const QuantBot* bot=nullptr;
                for (const auto& player:house->getPlayerList())
                    if (const auto* q=dynamic_cast<const QuantBot*>(player.get())) {
                        bot=q;
                        const auto& f=q->botConfig().force;
                        players.set(std::to_string(q->getPlayerID()),AITelemetry::Record()
                            .set("difficulty",int(q->difficulty)).set("role",int(q->role))
                            .set("config_hash",q->botConfig().getConfigHash())
                            .set("CityGrowthPower",f.cityGrowthPower).set("CityPowerSizing",f.cityPowerSizing)
                            .set("CityOpeningCapital",f.cityOpeningCapital).set("CityConstructionCapital",f.cityConstructionCapital)
                            .set("CityProductionPrerequisites",f.cityProductionPrerequisites)
                            .set("CityFirstFactorySaving",f.cityFirstFactorySaving)
                            .set("SimpleTechSavings",f.simpleTechSavings)
                            .set("ExistingCompositionUnitChoice",f.existingCompositionUnitChoice)
                            .set("TrooperSupport",f.trooperSupport).set("FundedClassicProduction",f.fundedClassicProduction)
                            .set("ThreatTurrets",f.threatTurrets).set("MoreRocketTurrets",f.moreRocketTurrets)
                            .set("CityStarportSupply",f.cityStarportSupply).set("StorageCapacity",f.storageCapacity));
                    }
                for (Uint32 item:{Structure_LightFactory,Structure_HeavyFactory,Structure_ZoneCommercial}) {
                    const Coord size=getStructureSize(item);
                    int live=0,traffic=0;
                    for (int y=0;y<=currentGameMap->getSizeY()-size.y;++y)
                        for (int x=0;x<=currentGameMap->getSizeX()-size.x;++x) {
                            live+=currentGameMap->okayToPlaceStructure(x,y,size.x,size.y,false,house,false,item);
                            traffic+=bot && bot->placeableAmongOwnUnits(x,y,size.x,size.y,item);
                        }
                    sites.set(getItemNameByID(item),AITelemetry::Record().set("width",size.x).set("height",size.y)
                        .set("legal_origins_live",live).set("legal_origins_own_traffic_allowed",traffic));
                }
                for (Uint32 item=ItemID_FirstID;item<=ItemID_LastID;++item)
                    if (const int count=house->getNumItems(item)) items.set(getItemNameByID(item),count);
                for (const auto* structure:structureList) if (structure->getOwner()==house && structure->isABuilder()) {
                    const auto* builder=static_cast<const BuilderBase*>(structure);
                    builders.set(std::to_string(builder->getObjectID()),AITelemetry::Record()
                        .set("item",getItemNameByID(builder->getItemID())).set("queue",builder->getProductionQueueSize())
                        .set("upgrade",builder->getCurrentUpgradeLevel()).set("upgrading",builder->isUpgrading())
                        .set("hold",builder->isOnHold()).set("heavy_available",builder->isAvailableToBuild(Structure_HeavyFactory))
                        .set("mcv_available",builder->isAvailableToBuild(Unit_MCV)));
                }
                for (const auto& offer:house->getChoam().availableItems)
                    market.set(getItemNameByID(offer.itemID),AITelemetry::Record().set("stock",offer.num).set("price",offer.price));
                row.set("cycle",game->getGameCycleCount()).set("house",h).set("credits",house->getCredits())
                    .set("starting_credits",house->getStartingCredits().lround()).set("city_credits",house->getCityCredits().lround())
                    .set("capacity",house->getCapacity()).set("power_produced",house->getProducedPower())
                    .set("power_required",house->getPowerRequirement()).set("starport_population_rule",options.starportPopulation)
                    .set("players",players).set("builders",builders).set("items",items).set("market",market).set("sites",sites);
                if (const auto* sim=game->getCitySimulation()) {
                    const auto& state=sim->getHouseState(h);
                    row.set("display_population",state.getTotalPop()*20).set("annual_tax",state.budget.getLastTaxRevenue())
                        .set("annual_police",state.lastPoliceExpense);
                }
                SDL_Log("BUILD_SITE_AUDIT: %s",row.json().c_str());
            }
        }
        // The map text a match was started from (a Random Skirmish save carries its generated map).
        if (const char* mapOut = std::getenv("BALANCE_PERF_DUMP_MAP"))
            std::ofstream(mapOut, std::ios::binary) << setup.getFiledata();
        const Uint32 perfCycles = std::getenv("BALANCE_PERF_CYCLES")
            ? static_cast<Uint32>(std::strtoul(std::getenv("BALANCE_PERF_CYCLES"), nullptr, 10)) : end;
        GamePerf::run(*game, perfCycles, [&]() { ProbeDigest::periodic(*game); });
    } else {
        while (!game->finished && game->getGameCycleCount() < end) {
            ProbeDigest::periodic(*game);
            game->updateGameState();
        }
    }
    if (outcome.empty()) outcome = game->finished ? (game->won ? "won" : "lost") : "time_limit";
    {
        const long long loopMs = ProbeDigest::millisecondsSince(loopStart);
        const Uint32 cycles = game->getGameCycleCount() - firstCycle;
        ProbeDigest::atEnd(*game);
        SDL_Log("PROBE_TIMING: setup_ms=%lld loop_ms=%lld cycles=%u cycles_per_s=%.0f", setupMs, loopMs, cycles,
                loopMs > 0 ? cycles * 1000.0 / loopMs : 0.0);
    }
    tornieAudit.finish(*game);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=%d seed=%u attack_percent=%d partner=%s enemy=%s cycle=%u outcome=%s",
        level, game->getGameInitSettings().getRandomSeed(), percent, partner.c_str(), enemy.c_str(), game->getGameCycleCount(),
        outcome.c_str());
    if (outcome == "time_limit") {
        for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game->getHouse(h))
            for (const auto& player : house->getPlayerList())
                if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) {
                    int idleReady = 0, heldRepairs = 0;
                    for (const auto* unit : unitList) {
                        if (!bot->campaignCombatUnit(unit)) continue;
                        const auto* target = unit->getTarget();
                        if (unit->isActive() && bot->declinesUnfundedRepair(unit) && target
                            && target->getOwner() == house && target->getItemID() == Structure_RepairYard)
                            ++heldRepairs;
                        // Damaged aircraft are not raid-ready under the existing air policy.
                        // Ground troops, including damaged ones released by exhaustion, are.
                        if (unit->isActive() && (!unit->isAFlyingUnit() || !unit->isBadlyDamaged())
                            && !unit->isMoving() && !unit->hasATarget()
                            && bot->unitRole(unit,{QuantBot::UnitRole::Human,QuantBot::UnitRole::Repair,
                                QuantBot::UnitRole::BaseDefence,QuantBot::UnitRole::LiveDefence,
                                QuantBot::UnitRole::Defence,QuantBot::UnitRole::Wave,
                                QuantBot::UnitRole::Escort,QuantBot::UnitRole::ColonyGuard})
                                == QuantBot::UnitRole::Free) ++idleReady;
                    }
                    for (const auto* building : structureList)
                        if (building->getOwner() == house)
                            if (const auto* occupant = building->getContainedRepairUnit();
                                occupant && bot->declinesUnfundedRepair(occupant)) ++heldRepairs;
                    SDL_Log("CAMPAIGN_ENDGAME_AUDIT: house=%d exhausted=%d credits_raw=%lld spice=%d idle_ready=%d held_repairs=%d",
                        h, bot->exhaustedArmyEconomy(),
                        static_cast<long long>((house->getStartingCredits()+house->getEarnedCredits()).getRawValue()),
                        bot->lastCalculatedSpice, idleReady, heldRepairs);
                }
        for (const auto* unit:unitList) {
            const auto* target=unit->getTarget();
            const auto* candidate=unit->findTarget();
            const QuantBot* controller = nullptr;
            for (const auto& player : unit->getOwner()->getPlayerList())
                if (const auto* bot = dynamic_cast<const QuantBot*>(player.get()); bot && !bot->builderOnly()) {
                    controller = bot;
                    break;
                }
            SDL_Log("CAMPAIGN_REPAIR_AUDIT: id=%u cargo_raw=%lld reserved=%d unfunded=%d role=%d",
                unit->getObjectID(), static_cast<long long>(harvesterGetAmountOfSpice(unit).getRawValue()),
                controller && controller->reserveDamagedUnitForRepair(unit),
                controller && controller->declinesUnfundedRepair(unit),
                controller ? int(controller->unitRole(unit,{QuantBot::UnitRole::Human,QuantBot::UnitRole::Defence,
                    QuantBot::UnitRole::BaseDefence,QuantBot::UnitRole::LiveDefence,QuantBot::UnitRole::Escort,
                    QuantBot::UnitRole::ColonyGuard})) : -1);
            SDL_Log("CAMPAIGN_TARGET_AUDIT: id=%u attack_pos=%d find_timer=%d pending=%d candidate=%u candidate_item=%u",
                unit->getObjectID(),unit->attackPos.isValid(),unit->findTargetTimer,int(unit->pendingTargetRequest),
                candidate?candidate->getObjectID():NONE_ID,candidate?candidate->getItemID():NONE_ID);
            SDL_Log("CAMPAIGN_UNIT_AUDIT: house=%d id=%u item=%u hp=%d/%d active=%d moving=%d forced=%d mode=%d pos=%d,%d dest=%d,%d guard=%d,%d target=%u target_item=%u target_pos=%d,%d",
                unit->getOwner()->getHouseID(),unit->getObjectID(),unit->getItemID(),unit->getHealth().lround(),unit->getMaxHealth(),
                unit->isActive(),unit->isMoving(),unit->wasForced(),int(unit->getAttackMode()),
                unit->getLocation().x,unit->getLocation().y,unit->getDestination().x,unit->getDestination().y,
                unit->getGuardPoint().x,unit->getGuardPoint().y,target?target->getObjectID():NONE_ID,
                target?target->getItemID():NONE_ID,target?target->getLocation().x:-1,target?target->getLocation().y:-1);
        }
        for (const auto* building:structureList) {
            const auto* occupant = building->getContainedRepairUnit();
            SDL_Log("CAMPAIGN_BUILDING_AUDIT: house=%d item=%u hp=%d/%d pos=%d,%d occupant=%u",
                building->getOwner()->getHouseID(),building->getItemID(),building->getHealth().lround(),building->getMaxHealth(),
                building->getLocation().x,building->getLocation().y,occupant?occupant->getObjectID():NONE_ID);
        }
    }
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    return MENU_QUIT_DEFAULT;
}
