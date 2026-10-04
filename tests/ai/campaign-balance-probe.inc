// Included only by run-campaign-balance.py, never by the shipped game.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <players/QuantBotConfig.h>
#include <memory>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "starport-economy-probe.inc"
#include "city-placement-probe.inc"
#include "controls-probe.inc"
#include "sourceforge-regressions-probe.inc"
#include "harvester-safety-probe.inc"
#include "shared-spending-probe.inc"
#include "police-placement-probe.inc"
#include "police-budget-probe.inc"
#include "police-reinforcement-probe.inc"
#include "city-growth-probe.inc"
#include "mcv-deployment-probe.inc"
#include "opening-economy-probe.inc"
#include "army-movement-probe.inc"
#include "army-recovery-probe.inc"
#include "army-recovery-continuation-probe.inc"
#include "air-defense-probe.inc"
#include "custom-attack-probe.inc"
#include "custom-city-cap-probe.inc"
#include "factory-recovery-probe.inc"
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
#include "degradation-probe.inc"
#include "spice-income-probe.inc"

int runCampaignBalanceProbe() {
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
    const int percent = std::atoi(std::getenv("BALANCE_ATTACK_PERCENT"));
    const auto humanHouse=static_cast<HOUSETYPE>(std::getenv("BALANCE_HOUSE") ? std::atoi(std::getenv("BALANCE_HOUSE")) : HOUSE_HARKONNEN);
    const std::string requestedMod=std::getenv("BALANCE_MOD") ? std::getenv("BALANCE_MOD") : "vanilla";
    if(!ModManager::instance().setActiveMod(requestedMod)
       || ModManager::instance().getActiveModName()!=requestedMod)
        throw std::runtime_error("Requested probe mod did not activate: "+requestedMod);
    if(std::getenv("BALANCE_DEGRADATION_PROBE"))
        settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    getQuantBotConfig().easy.attackForceMilitaryValueRatio = percent / 100.0f;
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
    GameInitSettings init(humanHouse, settings.gameOptions, level);
    const char* customMap=std::getenv("BALANCE_CUSTOM_MAP");
    if(customMap) {
        std::ifstream file(customMap);
        if(!file)throw std::runtime_error("Cannot read custom map");
        const std::string contents((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        init=GameInitSettings(customMap,contents,true,settings.gameOptions);
    }
    init.randomSeed = seed; // Test-only access, provided by -fno-access-control.
    init.setMultiplePlayersPerHouse(true);
    std::vector<std::pair<int,int>> roster;
    if(customMap) {
        std::istringstream input(std::getenv("BALANCE_ROSTER") ? std::getenv("BALANCE_ROSTER") : "");
        std::string slot;
        while(std::getline(input,slot,',')) {
            const auto colon=slot.find(':');
            if(colon==std::string::npos) throw std::runtime_error("Invalid test roster");
            roster.emplace_back(std::stoi(slot.substr(0,colon)),std::stoi(slot.substr(colon+1)));
        }
        if(roster.empty()) throw std::runtime_error("Custom test requires a roster");
    } else {
        for(int h=0;h<NUM_HOUSES;++h)
            if(isHouseAvailable(static_cast<HOUSETYPE>(h))) roster.emplace_back(h,h==humanHouse ? 1 : 2);
    }
    int rosterRow = 0;
    for (const auto& [h,team] : roster) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h), team);
        if (h==humanHouse) info.addPlayerInfo({settings.general.playerName,"HumanPlayer"});
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)),h==humanHouse ? partnerClass : enemyClass});
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
    game->initGame(init);
    // This diagnostic can retain a full hour of every house's decisions.
    // The normal game capture limit is deliberately unchanged.
    if(std::getenv("BALANCE_CAPTURE_MIB"))
        AITelemetry::log().limit=std::stoull(std::getenv("BALANCE_CAPTURE_MIB"))*1024*1024;
    game->bReplay = false;
    game->initializeGameLoop();
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
    if (std::getenv("BALANCE_CITY_CAMPAIGN_PROBE")) {
        runCampaignCityEconomyProbe(*game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
        return MENU_QUIT_DEFAULT;
    }
    if (std::getenv("BALANCE_SPICE_INCOME_PROBE")) {
        // Takes the owning pointer: it saves the match, replaces the Game with a reload and
        // measures the resumed delivery on the loaded one.
        runSpiceIncomeProbe(game);
        game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
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
        runArmyRecoveryProbe(*game);runArmyRecoveryContinuationProbe(game);game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
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
    const Uint32 end = MILLI2CYCLES(minutes * 60000);
    while (!game->finished && game->getGameCycleCount() < end) {
        game->updateGameState();

    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=%d seed=%u attack_percent=%d partner=%s enemy=%s cycle=%u outcome=%s",
        level, seed, percent, partner.c_str(), enemy.c_str(), game->getGameCycleCount(), game->finished ? (game->won ? "won" : "lost") : "time_limit");
    if (!game->finished) {
        for (const auto* unit:unitList) {
            const auto* target=unit->getTarget();
            const auto* candidate=unit->findTarget();
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
        for (const auto* building:structureList)
            SDL_Log("CAMPAIGN_BUILDING_AUDIT: house=%d item=%u hp=%d/%d pos=%d,%d",
                building->getOwner()->getHouseID(),building->getItemID(),building->getHealth().lround(),building->getMaxHealth(),
                building->getLocation().x,building->getLocation().y);
    }
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    return MENU_QUIT_DEFAULT;
}
