// Private-main recovery adapter. All simulation, AI, map loading and victory code is native.
#include "ai-baseline-observer.inc"
#include "ai-recovery-campaign.inc"
#include <players/HumanPlayer.h>
#include <map>

namespace AIRecovery {
struct Track {
    int eliminated = -1;
    Coord start{-1,-1};
};
inline std::string classes(const House& house) {
    std::string out;
    for(const auto& player:house.getPlayerList())
        out+=(out.empty()?"":"+")+player->getPlayerclass();
    return out;
}
inline std::unique_ptr<Game> initMatch(bool humanSeat, Uint32 seed) {
    std::ifstream input(std::getenv("RECOVERY_MAP"),std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open frozen recovery map");
    const std::string contents((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    GameInitSettings init(std::getenv("RECOVERY_MAP_NAME"),contents,humanSeat,settings.gameOptions);
    init.randomSeed=seed;
    std::istringstream roster(std::getenv("RECOVERY_SIDES"));
    std::string entry;
    while(std::getline(roster,entry,',')) {
        const auto first=entry.find(':'),second=entry.find(':',first+1);
        if(first==std::string::npos || second==std::string::npos) throw std::runtime_error("Invalid recovery roster");
        const auto h=static_cast<HOUSETYPE>(std::stoi(entry.substr(0,first)));
        GameInitSettings::HouseInfo info(h,std::stoi(entry.substr(first+1,second-first-1)));
        if(humanSeat && h==HOUSE_ATREIDES) info.addPlayerInfo({settings.general.playerName,"HumanPlayer"});
        info.addPlayerInfo({getHouseNameByNumber(h),entry.substr(second+1)});
        init.addHouseInfo(info);
    }
    auto game=std::make_unique<Game>();
    currentGame=game.get();pLocalHouse=nullptr;pLocalPlayer=nullptr;
    game->bReplay=true;
    game->initGame(init);
    if(humanSeat) {
        if(!pLocalHouse || pLocalHouse->getHouseID()!=HOUSE_ATREIDES || !pLocalPlayer)
            throw std::runtime_error("Recovery AAA did not create its idle shared Atreides human seat");
    } else game->setupSpectatorView();
    game->bReplay=false;
    for(const auto& side:init.getHouseInfoList()) {
        const auto* house=game->getHouse(side.houseID);
        const auto expected=side.playerInfoList.size();
        if(!house || house->getTeamID()!=side.team || house->getPlayerList().size()!=expected)
            throw std::runtime_error("Native recovery roster differs from requested slots");
        auto actual=house->getPlayerList().begin();
        for(const auto& player:side.playerInfoList) {
            if((*actual)->getPlayerclass()!=player.playerClass)
                throw std::runtime_error("Native recovery controller class differs from requested slot");
            ++actual;
        }
    }
    return game;
}
inline void sample(Game& game,const std::map<int,Track>& tracks,const char* marker) {
    const auto cycle=game.getGameCycleCount();
    for(const auto& pair:tracks) {
        const auto* house=game.getHouse(pair.first);
        int armyValue=0,armyUnits=0,harvesters=0,structures=0,factories=0,baseHp=0;
        for(const auto* unit:unitList) if(unit->getOwner()==house && unit->getHealth()>0) {
            if(isHarvesterLikeUnit(unit->getItemID())) ++harvesters;
            if(unit->canAttack()) { armyValue+=game.objectData.data[unit->getItemID()][pair.first].price;++armyUnits; }
        }
        for(const auto* structure:structureList) if(structure->getOwner()==house && structure->getHealth()>0) {
            ++structures;baseHp+=structure->getHealth().lround();
            if(dynamic_cast<const BuilderBase*>(structure)) ++factories;
        }
        SDL_Log("%s: cycle=%u minute=%u house=%d team=%d players=%s alive=%d eliminated_s=%.3f credits=%d kill_value=%d loss_value=%d structures=%d army_value=%d army_units=%d harvesters=%d factories=%d power=%d need=%d built=%d base_hp=%d start=%d:%d",
            marker,cycle,cycle/MILLI2CYCLES(60000),pair.first,house->getTeamID(),classes(*house).c_str(),int(house->isAlive()),
            pair.second.eliminated<0?-1.0:pair.second.eliminated*0.016,house->getCredits(),house->getKillValue(),house->getLossValue(),
            structures,armyValue,armyUnits,harvesters,factories,house->getProducedPower(),house->getPowerRequirement(),house->getNumBuiltStructures(),baseHp,
            pair.second.start.x,pair.second.start.y);
    }
}
}

int runAIRecoveryProbe() {
    const bool campaign=std::string(std::getenv("RECOVERY_FAMILY"))=="campaign";
    const bool humanSeat=std::string(std::getenv("RECOVERY_FAMILY"))=="aaa";
    const int tracked=std::stoi(std::getenv("RECOVERY_HOUSE"));
    const auto seed=static_cast<Uint32>(std::stoul(std::getenv("RECOVERY_SEED")));
    if(!ModManager::instance().setActiveMod("vanilla") || ModManager::instance().getActiveModName()!="vanilla")
        throw std::runtime_error("Recovery vanilla mod did not activate");
    settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    settings.gameOptions.fogOfWar=false;settings.gameOptions.immortalHumanPlayer=false;
    if(const auto* options=std::getenv("RECOVERY_OPTIONS")) if(*options)
        applyGameOptionsFromConfig(INIFile(options),"Game Options",settings.gameOptions);
    settings.general.playerName="Historical recovery idle view";
    settings.general.diagnosticLogs=true;settings.network.metaServer="http://127.0.0.1:1/";
    auto game=campaign?AIRecovery::initCampaign(tracked,std::stoi(std::getenv("RECOVERY_MISSION")),seed)
                      :AIRecovery::initMatch(humanSeat,seed);
    game->initializeGameLoop();
    const int requestedTech=std::stoi(std::getenv("RECOVERY_TECH"));
    if(requestedTech && game->techLevel!=requestedTech)
        throw std::runtime_error("Native recovery map technology differs from requested fixture");
    if(const auto* deploy=std::getenv("RECOVERY_DEPLOY_MCV")) if(*deploy=='1') {
        std::vector<MCV*> mcvs;
        for(auto* unit:unitList) if(auto* mcv=dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
        for(auto* mcv:mcvs) if(!mcv->doDeploy()) throw std::runtime_error("Native recovery CY-start deployment failed");
    }
    const auto& init=game->getGameInitSettings();
    GameStateDigest::Hasher mapHash;
    for(const unsigned char byte:init.getFiledata()) mapHash.mixUint8(byte);
    const auto checksums=ModManager::instance().getEffectiveChecksums();
    SDL_Log("RECOVERY_SETUP: seed=%u tech_level=%d options_hash=%s map_hash=%016llx content_hash=%s object_data_hash=%s config_hash=%s city_sim=%d registered_human=%d",
        seed,game->techLevel,init.getGameOptions().getHash().c_str(),static_cast<unsigned long long>(mapHash.value()),
        checksums.combined.c_str(),checksums.objectData.c_str(),getQuantBotConfig().getConfigHash().c_str(),int(game->isCitySimEnabled()),int(humanSeat));
    std::map<int,AIRecovery::Track> tracks;
    for(int h=0;h<NUM_HOUSES;++h) if(const auto* house=game->getHouse(h)) if(house->isAlive() && !house->getPlayerList().empty()) {
        AIRecovery::Track track;
        for(const auto* structure:structureList) if(structure->getOwner()==house) { track.start=structure->getLocation();break; }
        if(track.start.isInvalid()) for(const auto* unit:unitList) if(unit->getOwner()==house) { track.start=unit->getLocation();break; }
        tracks.emplace(h,track);
        SDL_Log("RECOVERY_ROSTER: house=%d team=%d players=%s",h,house->getTeamID(),AIRecovery::classes(*house).c_str());
    }
    INIFile nativeOptions(true,"Historical recovery effective native options");
    writeGameOptionsToConfig(nativeOptions,"Game Options",init.getGameOptions());
    if(!nativeOptions.saveChangesTo(std::getenv("RECOVERY_NATIVE_OPTIONS")))
        throw std::runtime_error("Cannot retain effective native recovery options");
    const Uint32 minute=MILLI2CYCLES(60000),end=MILLI2CYCLES(std::stoi(std::getenv("RECOVERY_MINUTES"))*60000);
    const size_t initialTriggers=game->getTriggerManager().getTriggers().size();
    int fired=0,winner=0;
    AIRecovery::sample(*game,tracks,"RECOVERY_SAMPLE");
    while(game->getGameCycleCount()<end) {
        const auto before=game->getGameCycleCount();
        const auto& triggers=game->getTriggerManager().getTriggers();
        for(auto it=triggers.begin();it!=triggers.end() && (*it)->getCycleNumber()==before;++it) ++fired;
        game->updateGameState();
        const auto cycle=game->getGameCycleCount();
        if(cycle!=before+1) throw std::runtime_error("Native recovery cycle did not advance exactly once");
        for(auto& pair:tracks) if(pair.second.eliminated<0 && !game->getHouse(pair.first)->isAlive()) pair.second.eliminated=cycle;
        if(cycle%minute==0) AIRecovery::sample(*game,tracks,"RECOVERY_SAMPLE");
        if(campaign?game->finished:(winner=AIBaseline::survivingTeam(*game))!=0) break;
    }
    const auto cycle=game->getGameCycleCount();
    if(cycle%minute) AIRecovery::sample(*game,tracks,"RECOVERY_SAMPLE");
    AIRecovery::sample(*game,tracks,"RECOVERY_HOUSE");
    AIBaseline::digest(*game);
    const auto* house=game->getHouse(tracked);
    const std::string outcome=campaign?(game->finished?(game->won?"won":"lost"):"time_limit")
        :(winner==0?"time_limit":(house && winner==house->getTeamID()?"won":"lost"));
    if(campaign) winner=!game->finished?0:(game->won && house?house->getTeamID():-1);
    SDL_Log("RECOVERY_RESULT: outcome=%s winner_team=%d cycle=%u end_s=%.3f minutes=%.5f triggers=%zu fired=%d win_flags=%d lose_flags=%d quota=%d",
        outcome.c_str(),winner,cycle,cycle*0.016,cycle/static_cast<double>(minute),initialTriggers,fired,game->winFlags,game->loseFlags,house?house->getQuota():0);
    game.reset();currentGame=nullptr;pLocalHouse=nullptr;pLocalPlayer=nullptr;
    return MENU_QUIT_DEFAULT;
}
