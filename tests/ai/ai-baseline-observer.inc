// Diagnostic executable only. No gameplay object, controller, RNG or AI setting is modified.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <structures/BuilderBase.h>
#include <units/HarvesterHelpers.h>
#include <units/MCV.h>
#include <dunecity/CitySimulation.h>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>

namespace AIBaseline {
constexpr std::array<Uint32,5> techItems{Structure_LightFactory,Structure_HeavyFactory,
    Structure_IX,Structure_StarPort,Structure_Palace};
constexpr std::array<const char*,5> techNames{"light","heavy","ix","starport","palace"};
inline bool combatItem(Uint32 item) {
    return isUnit(item) && !isAmbientUnit(item) && !isCarryallUnit(item)
        && !isHarvesterLikeUnit(item) && item!=Unit_MCV && item!=Unit_Sandworm
        && item!=Unit_Saboteur && item!=Unit_Frigate;
}
inline const QuantBot* botOf(const House* house) {
    for(const auto& player:house->getPlayerList())
        if(const auto* bot=dynamic_cast<const QuantBot*>(player.get())) return bot;
    return nullptr;
}
struct Observer {
    std::array<std::array<int64_t,5>,NUM_HOUSES> firstOwned{},firstCompleted{};
    std::array<int64_t,NUM_HOUSES> eliminated{};
    Observer() {
        for(auto& times:firstOwned) times.fill(-1);
        for(auto& times:firstCompleted) times.fill(-1);
        eliminated.fill(-1);
    }
    void observe(Game& game) {
        const auto cycle=game.getGameCycleCount();
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* house=game.getHouse(h);
            if(!house || !botOf(house)) continue;
            for(size_t i=0;i<techItems.size();++i) {
                if(firstOwned[h][i]<0 && house->getNumItems(techItems[i])>0) firstOwned[h][i]=cycle;
                if(firstCompleted[h][i]<0 && house->getNumBuiltItems(techItems[i])>0) firstCompleted[h][i]=cycle;
            }
            if(eliminated[h]<0 && !house->isAlive()) eliminated[h]=cycle;
        }
    }
    void sample(Game& game) const {
        const auto cycle=game.getGameCycleCount();
        std::array<std::array<int64_t,Num_ItemID>,NUM_HOUSES> live{},queued{};
        std::array<int,NUM_HOUSES> harvesters{},refineries{},queue{},upgrading{},waiting{},baseHp{},yardLevel{};
        yardLevel.fill(-1);
        for(const auto* unit:unitList) {
            if(unit->getHealth()<=0) continue;
            const int h=unit->getOwner()->getHouseID();
            if(isHarvesterLikeUnit(unit->getItemID())) ++harvesters[h];
            if(combatItem(unit->getItemID())) ++live[h][unit->getItemID()];
        }
        for(const auto* structure:structureList) {
            if(structure->getHealth()<=0) continue;
            const int h=structure->getOwner()->getHouseID();
            const Uint32 item=structure->getItemID();
            baseHp[h]+=structure->getHealth().lround();
            refineries[h]+=int(item==Structure_Refinery || item==Structure_Worfinery);
            if(const auto* builder=dynamic_cast<const BuilderBase*>(structure)) {
                queue[h]+=builder->getProductionQueueSize();
                upgrading[h]+=int(builder->isUpgrading());
                waiting[h]+=int(builder->isWaitingToPlace());
                if(item==Structure_ConstructionYard) yardLevel[h]=std::max(yardLevel[h],builder->getCurrentUpgradeLevel());
                for(const auto& order:builder->currentProductionQueue) {
                    if(!combatItem(order.itemID)) continue;
                    Uint32 single=order.itemID;
                    int count=1;
                    if(single==Unit_Infantry) { single=Unit_Soldier;count=3; }
                    if(single==Unit_Troopers) { single=Unit_Trooper;count=3; }
#if BASELINE_HAS_FIVE_SQUADS
                    if(single==Unit_Infantry5) { single=Unit_Soldier;count=5; }
                    if(single==Unit_Troopers5) { single=Unit_Trooper;count=5; }
#endif
                    queued[h][single]+=count;
                }
            }
        }
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* house=game.getHouse(h);
            if(!house || !botOf(house)) continue;
            int64_t armyCount=0,armyValue=0;
            for(Uint32 item=Unit_FirstID;item<=Unit_LastID;++item) {
                if(!combatItem(item)) continue;
                const int price=game.objectData.data[item][h].price;
                armyCount+=live[h][item];armyValue+=live[h][item]*price;
                SDL_Log("BASELINE_UNIT: cycle=%u house=%d item=%u price=%d live_count=%lld live_value=%lld queued_count=%lld queued_value=%lld registered_actual=%d built=%d lost=%d",
                    cycle,h,item,price,static_cast<long long>(live[h][item]),static_cast<long long>(live[h][item]*price),
                    static_cast<long long>(queued[h][item]),static_cast<long long>(queued[h][item]*price),
                    house->getNumItems(item),house->getNumBuiltItems(item),house->getNumLostItems(item));
            }
            int population=0;
            if(const auto* sim=game.getCitySimulation();sim && sim->isInitialized())
                population=sim->getHouseState(h).getTotalPop()*DuneCity::CitySimulation::kPopDisplayMultiplier;
            int structuresLost=0;
            for(int item=Structure_FirstID;item<=Structure_LastID;++item)
                if(isStructure(item)) structuresLost+=house->getNumLostItems(item);
            int tech=game.techLevel,researchEnabled=0,researchTarget=0,researchRemaining=0;
#if BASELINE_HAS_RESEARCH
            tech=house->getTechLevel();
            researchEnabled=int(game.getGameInitSettings().getGameOptions().researchLab);
            researchTarget=house->getTechResearchTarget();researchRemaining=house->getTechResearchRemaining();
#endif
            SDL_Log("BASELINE_HOUSE: cycle=%u minute=%u house=%d team=%d alive=%d credits=%d structures=%d army_units=%lld army_value=%lld kill_value=%d loss_value=%d tech_level=%d eliminated_cycle=%lld harvesters=%d refineries=%d population=%d zone_residential=%d zone_industrial=%d zone_commercial=%d zone_residential_built=%d zone_industrial_built=%d zone_commercial_built=%d power=%d need=%d built=%d structures_lost=%d base_hp=%d yards=%d yard_level=%d queue=%d upgrading=%d waiting=%d research_enabled=%d research_target=%d research_remaining=%d",
                cycle,cycle/MILLI2CYCLES(60000),h,house->getTeamID(),int(house->isAlive()),house->getCredits(),house->getNumStructures(),
                static_cast<long long>(armyCount),static_cast<long long>(armyValue),house->getKillValue(),house->getLossValue(),tech,
                static_cast<long long>(eliminated[h]),harvesters[h],refineries[h],population,
                house->getNumItems(Structure_ZoneResidential),house->getNumItems(Structure_ZoneIndustrial),house->getNumItems(Structure_ZoneCommercial),
                house->getNumBuiltItems(Structure_ZoneResidential),house->getNumBuiltItems(Structure_ZoneIndustrial),house->getNumBuiltItems(Structure_ZoneCommercial),
                house->getProducedPower(),house->getPowerRequirement(),house->getNumBuiltStructures(),structuresLost,baseHp[h],
                house->getNumItems(Structure_ConstructionYard),yardLevel[h],queue[h],upgrading[h],waiting[h],researchEnabled,researchTarget,researchRemaining);
            for(size_t i=0;i<techItems.size();++i)
                SDL_Log("BASELINE_TECH: cycle=%u house=%d name=%s live=%d built=%d first_owned_cycle=%lld first_completed_cycle=%lld",
                    cycle,h,techNames[i],house->getNumItems(techItems[i]),house->getNumBuiltItems(techItems[i]),
                    static_cast<long long>(firstOwned[h][i]),static_cast<long long>(firstCompleted[h][i]));
        }
    }
};
inline int survivingTeam(Game& game) {
    int team=0;
    for(int h=0;h<NUM_HOUSES;++h) {
        const auto* house=game.getHouse(h);
        if(!house || !house->isAlive() || house->getTeamID()==0) continue;
        if(team && team!=house->getTeamID()) return 0;
        team=house->getTeamID();
    }
    return team;
}
inline void digest(const Game& game) {
    SDL_Log("PROBE_DIGEST: %s",GameStateDigest::describe(game.computeStateDigest()).c_str());
}
} // namespace AIBaseline

int runAIBaselineProbe() {
    const std::string mod=std::getenv("BASELINE_MOD");
    if(!ModManager::instance().setActiveMod(mod) || ModManager::instance().getActiveModName()!=mod)
        throw std::runtime_error("Baseline native mod did not activate: "+mod);
    settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    applyGameOptionsFromConfig(INIFile(std::getenv("BASELINE_OPTIONS")),"Game Options",settings.gameOptions);
    settings.general.playerName="AI baseline detached observer";
    settings.network.metaServer="http://127.0.0.1:1/";
    settings.general.diagnosticLogs=true;
    std::ifstream mapFile(std::getenv("BASELINE_MAP"),std::ios::binary);
    if(!mapFile) throw std::runtime_error("Cannot open exact baseline map");
    const std::string contents((std::istreambuf_iterator<char>(mapFile)),std::istreambuf_iterator<char>());
    GameInitSettings init(std::getenv("BASELINE_MAP_NAME"),contents,false,settings.gameOptions);
#if BASELINE_HAS_LOCAL_OBSERVER
    init.setLocalSpectator(true);
#endif
    init.randomSeed=static_cast<Uint32>(std::stoul(std::getenv("BASELINE_SEED")));
    std::istringstream sides(std::getenv("BASELINE_SIDES"));
    std::string side;
    while(std::getline(sides,side,',')) {
        const auto first=side.find(':'),second=side.find(':',first+1);
        if(first==std::string::npos || second==std::string::npos) throw std::runtime_error("Invalid baseline roster");
        const int h=std::stoi(side.substr(0,first));
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h),std::stoi(side.substr(first+1,second-first-1)));
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)),side.substr(second+1)});
        init.addHouseInfo(info);
    }
    auto game=std::make_unique<Game>();
    currentGame=game.get();game->bReplay=true;
    game->initGame(init);
#if !BASELINE_HAS_LOCAL_OBSERVER
    game->setupSpectatorView();
#endif
    game->bReplay=false;
    AITelemetry::log().limit=std::stoull(std::getenv("BASELINE_CAPTURE_MIB"))*1024*1024;
    game->initializeGameLoop();
    if(game->techLevel!=9) throw std::runtime_error("Historical comparison requires real map tech9");
    for(const auto& info:init.getHouseInfoList()) {
        const auto* house=game->getHouse(info.houseID);
        if(!house || !house->isAI() || house->getPlayerList().size()!=1 || !AIBaseline::botOf(house))
            throw std::runtime_error("Baseline house is not one standalone native QuantBot");
        SDL_Log("BASELINE_ROSTER: house=%d team=%d difficulty=%s",int(info.houseID),house->getTeamID(),AIBaseline::botOf(house)->getDifficultyName().c_str());
    }
    std::vector<MCV*> mcvs;
    for(auto* unit:unitList) if(auto* mcv=dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
    for(auto* mcv:mcvs) if(!mcv->doDeploy()) throw std::runtime_error("Baseline CY-start native MCV deploy failed");
    const auto checksums=ModManager::instance().getEffectiveChecksums();
    SDL_Log("BASELINE_SETUP: seed=%u tech_level=%d options_hash=%s config_hash=%s city_sim=%d content_hash=%s object_data_hash=%s ai_content_hash=%s research_supported=%d local_observer_supported=%d mcvs_deployed=%zu",
        init.getRandomSeed(),game->techLevel,game->getGameInitSettings().getGameOptions().getHash().c_str(),getQuantBotConfig().getConfigHash().c_str(),int(game->isCitySimEnabled()),
        checksums.combined.c_str(),checksums.objectData.c_str(),checksums.quantBotConfig.c_str(),BASELINE_HAS_RESEARCH,BASELINE_HAS_LOCAL_OBSERVER,mcvs.size());
    AIBaseline::Observer observer;
    observer.observe(*game);observer.sample(*game);AIBaseline::digest(*game);
    const Uint32 minute=MILLI2CYCLES(60000),end=MILLI2CYCLES(std::stoi(std::getenv("BASELINE_MINUTES"))*60000);
    int winner=0;
    while(game->getGameCycleCount()<end) {
        const auto before=game->getGameCycleCount();game->updateGameState();
        if(game->getGameCycleCount()!=before+1) throw std::runtime_error("Baseline game did not advance exactly one cycle");
        observer.observe(*game);
        const auto cycle=game->getGameCycleCount();
        if(cycle%minute==0) observer.sample(*game);
        if(cycle%GameStateDigest::kDigestIntervalCycles==0) AIBaseline::digest(*game);
        if((winner=AIBaseline::survivingTeam(*game))!=0) break;
    }
    if(game->getGameCycleCount()%minute) observer.sample(*game);
    AIBaseline::digest(*game);
    SDL_Log("BASELINE_RESULT: cycle=%u winner_team=%d outcome=%s",game->getGameCycleCount(),winner,winner?"decided":"time_limit");
    game.reset();currentGame=nullptr;pLocalPlayer=nullptr;pLocalHouse=nullptr;
    return MENU_QUIT_DEFAULT;
}
