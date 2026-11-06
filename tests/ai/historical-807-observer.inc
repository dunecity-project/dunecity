// Diagnostic executable only: reuse 807's custom-map initialization and simulation.
// No AI/config/mod or engine object is rebuilt from altered gameplay source.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <structures/BuilderBase.h>
#include <units/HarvesterHelpers.h>
#include <units/MCV.h>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace Historical807 {
constexpr std::array<Uint32,8> mixItems{Unit_Tank,Unit_SiegeTank,Unit_Launcher,
    Unit_SonicTank,Unit_Ornithopter,Unit_Trike,Unit_RaiderTrike,Unit_Quad};
constexpr size_t infantrySlot=8, otherSlot=9, slots=10;

inline bool combatItem(Uint32 item) {
    return isUnit(item) && !isAmbientUnit(item) && !isCarryallUnit(item)
        && !isHarvesterLikeUnit(item) && item!=Unit_MCV && item!=Unit_Sandworm
        && item!=Unit_Saboteur && item!=Unit_Frigate;
}
inline size_t slotOf(Uint32 item) {
    if(item==Unit_Devastator || item==Unit_Deviator || item==Unit_SonicTank) return 3;
    for(size_t i=0;i<mixItems.size();++i) if(item==mixItems[i]) return i;
    return isInfantryUnit(item) ? infantrySlot : otherSlot;
}
inline Uint32 singleItem(Uint32 item) {
    if(item==Unit_Infantry) return Unit_Soldier;
    if(item==Unit_Troopers) return Unit_Trooper;
    return item;
}
inline int productCount(Uint32 item) {
    return item==Unit_Infantry || item==Unit_Troopers ? 3 : 1;
}
inline const QuantBot* botOf(const House* house) {
    for(const auto& player:house->getPlayerList())
        if(const auto* bot=dynamic_cast<const QuantBot*>(player.get())) return bot;
    return nullptr;
}
struct Observer {
    std::array<int64_t,NUM_HOUSES> firstIxCompleted{}, firstIxOwned{}, eliminated{};
    Observer() { firstIxCompleted.fill(-1); firstIxOwned.fill(-1); eliminated.fill(-1); }
    void observe(Game& game) {
        const auto cycle=game.getGameCycleCount();
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* house=game.getHouse(h);
            if(!house || !botOf(house)) continue;
            if(firstIxCompleted[h]<0 && house->getNumBuiltItems(Structure_IX)>0) firstIxCompleted[h]=cycle;
            if(firstIxOwned[h]<0 && house->getNumItems(Structure_IX)>0) firstIxOwned[h]=cycle;
            if(eliminated[h]<0 && !house->isAlive()) eliminated[h]=cycle;
        }
    }
    void sample(Game& game) const {
        // Current-controlled healthy units, including inactive bay/repair occupants.
        // Original-house actual/built/lost ledgers are emitted separately below.
        std::array<std::array<int64_t,Num_ItemID>,NUM_HOUSES> live{}, queued{};
        for(const auto* unit:unitList) {
            if(unit->getHealth()<=0 || !combatItem(unit->getItemID())) continue;
            ++live[unit->getOwner()->getHouseID()][unit->getItemID()];
        }
        for(const auto* structure:structureList) {
            if(structure->getHealth()<=0) continue;
            if(const auto* builder=dynamic_cast<const BuilderBase*>(structure))
                for(const auto& order:builder->currentProductionQueue)
                    if(combatItem(order.itemID))
                        queued[structure->getOwner()->getHouseID()][singleItem(order.itemID)]+=productCount(order.itemID);
        }
        const auto cycle=game.getGameCycleCount();
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* house=game.getHouse(h);
            if(!house) continue;
            const auto* bot=botOf(house);
            if(!bot) continue;
            std::array<int64_t,slots> counts{}, values{}, qcounts{}, qvalues{};
            int64_t armyCount=0, armyValue=0;
            for(Uint32 item=Unit_FirstID;item<=Unit_LastID;++item) {
                if(!combatItem(item)) continue;
                const auto slot=slotOf(item);
                const int price=game.objectData.data[item][h].price;
                counts[slot]+=live[h][item]; values[slot]+=live[h][item]*price;
                qcounts[slot]+=queued[h][item]; qvalues[slot]+=queued[h][item]*price;
                armyCount+=live[h][item]; armyValue+=live[h][item]*price;
                const auto& reward=house->getCombatReward(item);
                SDL_Log("HISTORICAL_UNIT: cycle=%u house=%d item=%u price=%d live_count=%lld live_value=%lld"
                        " queued_count=%lld queued_value=%lld registered_actual=%d built=%d lost=%d"
                        " raw_damage=%d damage_value_milli=%lld kill_bonus_milli=%lld conversion_value_milli=%lld unit_killing_blows=%llu",
                    cycle,h,item,price,static_cast<long long>(live[h][item]),static_cast<long long>(live[h][item]*price),
                    static_cast<long long>(queued[h][item]),static_cast<long long>(queued[h][item]*price),
                    house->getNumItems(item),house->getNumBuiltItems(item),house->getNumLostItems(item),house->getNumItemDamageInflicted(item),
                    static_cast<long long>(reward.damageMilli),static_cast<long long>(reward.killBonusMilli),
                    static_cast<long long>(reward.conversionMilli),static_cast<unsigned long long>(reward.kills));
            }
            const auto& mix=bot->getLastUnitMixBps();
            bool mixObserved=false;
            for(const int weight:mix) mixObserved|=weight>0;
            for(size_t slot=0;slot<slots;++slot)
                SDL_Log("HISTORICAL_COMPOSITION: cycle=%u house=%d slot=%zu item=%u live_count=%lld live_value=%lld"
                        " queued_count=%lld queued_value=%lld target_bps=%d target_observed=%d",
                    cycle,h,slot,slot<mixItems.size()?mixItems[slot]:Uint32(NONE_ID),
                    static_cast<long long>(counts[slot]),static_cast<long long>(values[slot]),
                    static_cast<long long>(qcounts[slot]),static_cast<long long>(qvalues[slot]),
                    mixObserved && slot<mixItems.size()?mix[slot]:-1,int(mixObserved));
            SDL_Log("HISTORICAL_HOUSE: cycle=%u house=%d team=%d alive=%d credits=%d structures=%d"
                    " army_units=%lld army_value=%lld kill_value=%d loss_value=%d tech_level=%d"
                    " ix=%d ix_built=%d first_ix_owned_cycle=%lld first_ix_completed_cycle=%lld eliminated_cycle=%lld",
                cycle,h,house->getTeamID(),int(house->isAlive()),house->getCredits(),house->getNumStructures(),
                static_cast<long long>(armyCount),static_cast<long long>(armyValue),house->getKillValue(),house->getLossValue(),game.techLevel,
                house->getNumItems(Structure_IX),house->getNumBuiltItems(Structure_IX),
                static_cast<long long>(firstIxOwned[h]),static_cast<long long>(firstIxCompleted[h]),static_cast<long long>(eliminated[h]));
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
} // namespace Historical807

int runHistorical807Probe() {
    const std::string mod=std::getenv("H807_MOD");
    if(!ModManager::instance().setActiveMod(mod) || ModManager::instance().getActiveModName()!=mod)
        throw std::runtime_error("Historical mod did not activate: "+mod);
    // Same supported Custom Game options as 807's menu. Newer unknown options
    // are NOT emulated; the Python harness reports their exact absence.
    settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    applyGameOptionsFromConfig(INIFile(std::getenv("H807_GAME_OPTIONS")),"Game Options",settings.gameOptions);
    settings.general.playerName="Historical observer (detached)";
    settings.network.metaServer="http://127.0.0.1:1/";
    settings.general.diagnosticLogs=true;
    std::ifstream mapFile(std::getenv("H807_MAP"),std::ios::binary);
    if(!mapFile) throw std::runtime_error("Cannot open frozen historical map");
    const std::string contents((std::istreambuf_iterator<char>(mapFile)),std::istreambuf_iterator<char>());
    // Name and bytes are the exact map strings recovered from the current replay.
    GameInitSettings init(std::getenv("H807_MAP_NAME"),contents,false,settings.gameOptions);
    init.randomSeed=static_cast<Uint32>(std::stoul(std::getenv("H807_SEED")));
    std::istringstream sides(std::getenv("H807_SIDES"));
    std::string side;
    while(std::getline(sides,side,',')) {
        const auto first=side.find(':'),second=side.find(':',first+1);
        if(first==std::string::npos || second==std::string::npos) throw std::runtime_error("Invalid historical roster");
        const int h=std::stoi(side.substr(0,first));
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h),std::stoi(side.substr(first+1,second-first-1)));
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)),side.substr(second+1)});
        init.addHouseInfo(info);
    }
    auto game=std::make_unique<Game>();
    currentGame=game.get();
    game->bReplay=true; // Suppress briefing/public analytics, as in the old harness.
    game->initGame(init);
    // 807 has no local-spectator flag. Its existing detached view supplies UI
    // pointers without registering a HumanPlayer or turning any AI into a helper.
    game->setupSpectatorView();
    game->bReplay=false;
    AITelemetry::log().limit=std::stoull(std::getenv("H807_CAPTURE_MIB"))*1024*1024;
    game->initializeGameLoop();
    if(game->techLevel!=9) throw std::runtime_error("Frozen comparison requires actual map T9");
    for(const auto& info:init.getHouseInfoList()) {
        const int h=info.houseID;
        const auto* house=game->getHouse(h);
        if(!house || !house->isAI() || house->getPlayerList().size()!=1 || !Historical807::botOf(house))
            throw std::runtime_error("Historical roster is not one standalone QuantBot per house");
        const auto* bot=Historical807::botOf(house);
        SDL_Log("HISTORICAL_ROSTER: house=%d team=%d ai=%d difficulty=%s",
            h,house->getTeamID(),int(house->isAI()),bot->getDifficultyName().c_str());
    }
    // Reuse the modern cohort's explicit CY-start click, using 807's real deploy.
    std::vector<MCV*> mcvs;
    for(auto* unit:unitList) if(auto* mcv=dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
    for(auto* mcv:mcvs)
        if(!mcv->doDeploy()) throw std::runtime_error("Frozen CY-start MCV could not deploy");
    const auto checksums=ModManager::instance().getEffectiveChecksums();
    SDL_Log("HISTORICAL_SETUP: seed=%u tech_level=%d options_hash=%s config_hash=%s city_sim=%d"
            " content_hash=%s object_data_hash=%s ai_content_hash=%s"
            " research_supported=0 slow_city_year_supported=0 starport_population_supported=0 local_observer_supported=0 mcvs_deployed=%zu",
        init.getRandomSeed(),game->techLevel,game->getGameInitSettings().getGameOptions().getHash().c_str(),
        getQuantBotConfig().getConfigHash().c_str(),int(game->isCitySimEnabled()),
        checksums.combined.c_str(),checksums.objectData.c_str(),checksums.quantBotConfig.c_str(),mcvs.size());
    Historical807::Observer observer;
    observer.observe(*game); observer.sample(*game); Historical807::digest(*game);
    const Uint32 minute=MILLI2CYCLES(60000);
    const Uint32 end=MILLI2CYCLES(std::stoi(std::getenv("H807_MINUTES"))*60000);
    int winner=0;
    while(game->getGameCycleCount()<end) {
        const auto before=game->getGameCycleCount();
        game->updateGameState();
        if(game->getGameCycleCount()!=before+1) throw std::runtime_error("Historical simulation did not advance one cycle");
        observer.observe(*game);
        const auto cycle=game->getGameCycleCount();
        if(cycle%minute==0) observer.sample(*game);
        if(cycle%GameStateDigest::kDigestIntervalCycles==0) Historical807::digest(*game);
        if((winner=Historical807::survivingTeam(*game))!=0) break;
    }
    if(game->getGameCycleCount()%minute) observer.sample(*game);
    Historical807::digest(*game);
    SDL_Log("HISTORICAL_RESULT: cycle=%u winner_team=%d outcome=%s",
        game->getGameCycleCount(),winner,winner?"decided":"time_limit");
    game.reset();currentGame=nullptr;pLocalPlayer=nullptr;pLocalHouse=nullptr;
    return MENU_QUIT_DEFAULT;
}
