// Test-only whole-commit cutover observer. Original engine/AI/config/mod objects.
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
#if SAVEGAMEVERSION >= 9868
constexpr auto mixItems=QuantBot::kMixItems;
#else
constexpr std::array<Uint32,8> mixItems{Unit_Tank,Unit_SiegeTank,Unit_Launcher,
    Unit_SonicTank,Unit_Ornithopter,Unit_Trike,Unit_RaiderTrike,Unit_Quad};
#endif
constexpr size_t nativeSlots=mixItems.size(), infantrySlot=nativeSlots, otherSlot=nativeSlots+1, slots=nativeSlots+2;

inline bool combatItem(Uint32 item) {
    return isUnit(item) && !isAmbientUnit(item) && !isCarryallUnit(item)
        && !isHarvesterLikeUnit(item) && item!=Unit_MCV && item!=Unit_Sandworm
        && item!=Unit_Saboteur && item!=Unit_Frigate;
}
inline size_t slotOf(Uint32 item) {
#if SAVEGAMEVERSION < 9868
    if(item==Unit_Devastator || item==Unit_Deviator || item==Unit_SonicTank) return 3;
#endif
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
inline bool sonicProducible(const QuantBot& bot) {
    for(const auto* structure:structureList) {
        const auto* builder=dynamic_cast<const BuilderBase*>(structure);
        if(builder && builder->getOwner()==bot.getHouse() && builder->getHealth()>0 && builder->isActive()
            && builder->getItemID()==Structure_HeavyFactory && bot.campaignAvailableToBuild(builder,Unit_SonicTank)) return true;
    }
    return false;
}
struct Observer {
    std::array<int64_t,NUM_HOUSES> firstIxCompleted{},firstIxOwned{},eliminated{},firstSonicBuilt{},lastAllocation{};
    std::array<int,NUM_HOUSES> sonicBuilt{};
    std::array<bool,NUM_HOUSES> priorObserved{};
    Observer() {
        firstIxCompleted.fill(-1);firstIxOwned.fill(-1);eliminated.fill(-1);
        firstSonicBuilt.fill(-1);lastAllocation.fill(-1);
    }
    void observe(Game& game) {
        const auto cycle=game.getGameCycleCount();
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* house=game.getHouse(h);
            if(!house) continue;
            const auto* bot=botOf(house);
            if(!bot) continue;
            if(firstIxCompleted[h]<0 && house->getNumBuiltItems(Structure_IX)>0) firstIxCompleted[h]=cycle;
            if(firstIxOwned[h]<0 && house->getNumItems(Structure_IX)>0) firstIxOwned[h]=cycle;
            if(eliminated[h]<0 && !house->isAlive()) eliminated[h]=cycle;
            const int built=house->getNumBuiltItems(Unit_SonicTank);
            if(built!=sonicBuilt[h]) {
                if(firstSonicBuilt[h]<0 && built>0) firstSonicBuilt[h]=cycle;
                SDL_Log("HISTORICAL_BUILD: cycle=%u house=%d item=%u built=%d delta=%d first_ix_completed_cycle=%lld",
                    cycle,h,Uint32(Unit_SonicTank),built,built-sonicBuilt[h],static_cast<long long>(firstIxCompleted[h]));
                sonicBuilt[h]=built;
            }
            if(bot->strategyPicked && !priorObserved[h]) {
                // Pure starting-prior functions only; never recompute learned targets.
                // pickStrategy already resolved any native memoized fieldable slots.
                const auto fieldable=bot->fieldableMixSlots();
                const auto prior=bot->startingPrior(fieldable);
#if SAVEGAMEVERSION >= 9868
                const int band=bot->techBand();
                const auto& tuned=bot->botConfig().getTunedRatios(h,band);
#else
                const int band=0;
                const auto& tuned=bot->botConfig().getTunedRatios(h);
#endif
                const auto boundedTuned=QuantBotStrategy::bound(tuned,fieldable);
                for(size_t i=0;i<nativeSlots;++i)
                    SDL_Log("HISTORICAL_PRIOR: cycle=%u house=%d slot=%zu item=%u fieldable=%d start_bps=%lld tuned_weight=%lld tuned_bounded_bps=%d strategy_prior_bps=%d mix=%s tempo=%s band=%d",
                        cycle,h,i,mixItems[i],int(fieldable[i]),static_cast<long long>(prior[i]),static_cast<long long>(tuned[i]),
                        boundedTuned[i],bot->strategyPrior[i],QuantBotStrategy::name(bot->strategy.mix),QuantBotStrategy::name(bot->strategy.tempo),band);
                priorObserved[h]=true;
            }
            const auto& snapshot=bot->getAllocationSnapshot();
            if(snapshot.ready && lastAllocation[h]!=snapshot.cycle) {
                const bool producible=sonicProducible(*bot);
                for(size_t i=0;i<nativeSlots;++i) {
                    const auto& slot=snapshot.slots[i];
                    SDL_Log("HISTORICAL_ALLOCATION: cycle=%u house=%d slot=%zu item=%u target_bps=%d allocated=%d score=%lld learning=%d military_value=%d anti_air_floor_bps=%d infantry_quota_bps=%d sonic_producible=%d ground_cap=%d ix=%d ix_built=%d reward_milli=%lld loss_milli=%lld",
                        snapshot.cycle,h,i,slot.itemID,slot.targetBps,int(slot.allocated),static_cast<long long>(slot.score),int(snapshot.learning),
                        snapshot.militaryValue,snapshot.antiAirFloorBps,snapshot.infantryQuotaBps,int(producible),int(house->isGroundUnitLimitReached()),
                        house->getNumItems(Structure_IX),house->getNumBuiltItems(Structure_IX),
                        static_cast<long long>(bot->performanceHistory.reward[i]),static_cast<long long>(bot->performanceHistory.loss[i]));
                }
                lastAllocation[h]=snapshot.cycle;
            }
        }
    }
    void sample(Game& game) const {
        // Present controlling owner, healthy units, including inactive repair/bay occupants.
        std::array<std::array<int64_t,Num_ItemID>,NUM_HOUSES> live{},queued{};
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
            std::array<int64_t,slots> counts{},values{},qcounts{},qvalues{};
            int64_t armyCount=0,armyValue=0;
            for(Uint32 item=Unit_FirstID;item<=Unit_LastID;++item) {
                if(!combatItem(item)) continue;
                const auto slot=slotOf(item);
                const int price=game.objectData.data[item][h].price;
                counts[slot]+=live[h][item];values[slot]+=live[h][item]*price;
                qcounts[slot]+=queued[h][item];qvalues[slot]+=queued[h][item]*price;
                armyCount+=live[h][item];armyValue+=live[h][item]*price;
                const auto& reward=house->getCombatReward(item);
                SDL_Log("HISTORICAL_UNIT: cycle=%u house=%d item=%u price=%d live_count=%lld live_value=%lld queued_count=%lld queued_value=%lld registered_actual=%d built=%d lost=%d raw_damage=%d damage_value_milli=%lld kill_bonus_milli=%lld conversion_value_milli=%lld unit_killing_blows=%llu",
                    cycle,h,item,price,static_cast<long long>(live[h][item]),static_cast<long long>(live[h][item]*price),
                    static_cast<long long>(queued[h][item]),static_cast<long long>(queued[h][item]*price),
                    house->getNumItems(item),house->getNumBuiltItems(item),house->getNumLostItems(item),house->getNumItemDamageInflicted(item),
                    static_cast<long long>(reward.damageMilli),static_cast<long long>(reward.killBonusMilli),
                    static_cast<long long>(reward.conversionMilli),static_cast<unsigned long long>(reward.kills));
            }
            const auto& snapshot=bot->getAllocationSnapshot();
            for(size_t slot=0;slot<slots;++slot)
                SDL_Log("HISTORICAL_COMPOSITION: cycle=%u house=%d slot=%zu item=%u live_count=%lld live_value=%lld queued_count=%lld queued_value=%lld target_bps=%d target_observed=%d allocation_cycle=%lld allocated=%d learning=%d",
                    cycle,h,slot,slot<nativeSlots?mixItems[slot]:Uint32(NONE_ID),
                    static_cast<long long>(counts[slot]),static_cast<long long>(values[slot]),static_cast<long long>(qcounts[slot]),static_cast<long long>(qvalues[slot]),
                    snapshot.ready && slot<nativeSlots?snapshot.slots[slot].targetBps:-1,int(snapshot.ready),
                    snapshot.ready?static_cast<long long>(snapshot.cycle):-1,snapshot.ready && slot<nativeSlots?int(snapshot.slots[slot].allocated):0,int(snapshot.learning));
            SDL_Log("HISTORICAL_HOUSE: cycle=%u house=%d team=%d alive=%d credits=%d structures=%d army_units=%lld army_value=%lld kill_value=%d loss_value=%d tech_level=%d ix=%d ix_built=%d first_ix_owned_cycle=%lld first_ix_completed_cycle=%lld eliminated_cycle=%lld first_sonic_built_cycle=%lld sonic_producible=%d ground_cap=%d",
                cycle,h,house->getTeamID(),int(house->isAlive()),house->getCredits(),house->getNumStructures(),
                static_cast<long long>(armyCount),static_cast<long long>(armyValue),house->getKillValue(),house->getLossValue(),game.techLevel,
                house->getNumItems(Structure_IX),house->getNumBuiltItems(Structure_IX),static_cast<long long>(firstIxOwned[h]),
                static_cast<long long>(firstIxCompleted[h]),static_cast<long long>(eliminated[h]),static_cast<long long>(firstSonicBuilt[h]),
                int(sonicProducible(*bot)),int(house->isGroundUnitLimitReached()));
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
    settings.gameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    applyGameOptionsFromConfig(INIFile(std::getenv("H807_GAME_OPTIONS")),"Game Options",settings.gameOptions);
    settings.general.playerName="Historical cutover observer (detached)";
    settings.network.metaServer="http://127.0.0.1:1/";
    settings.general.diagnosticLogs=true;
    std::ifstream mapFile(std::getenv("H807_MAP"),std::ios::binary);
    if(!mapFile) throw std::runtime_error("Cannot open frozen historical map");
    const std::string contents((std::istreambuf_iterator<char>(mapFile)),std::istreambuf_iterator<char>());
    GameInitSettings init(std::getenv("H807_MAP_NAME"),contents,false,settings.gameOptions);
    init.setLocalSpectator(true);
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
    game->bReplay=true;
    game->initGame(init);
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
        SDL_Log("HISTORICAL_ROSTER: house=%d team=%d ai=%d difficulty=%s",h,house->getTeamID(),int(house->isAI()),bot->getDifficultyName().c_str());
    }
    std::vector<MCV*> mcvs;
    for(auto* unit:unitList) if(auto* mcv=dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
    for(auto* mcv:mcvs) if(!mcv->doDeploy()) throw std::runtime_error("Frozen CY-start MCV could not deploy");
    const auto checksums=ModManager::instance().getEffectiveChecksums();
    SDL_Log("HISTORICAL_SETUP: seed=%u tech_level=%d options_hash=%s config_hash=%s city_sim=%d content_hash=%s object_data_hash=%s ai_content_hash=%s research_supported=0 slow_city_year_supported=0 starport_population_supported=0 local_observer_supported=1 mcvs_deployed=%zu native_mix_slots=%zu",
        init.getRandomSeed(),game->techLevel,game->getGameInitSettings().getGameOptions().getHash().c_str(),getQuantBotConfig().getConfigHash().c_str(),int(game->isCitySimEnabled()),
        checksums.combined.c_str(),checksums.objectData.c_str(),checksums.quantBotConfig.c_str(),mcvs.size(),Historical807::nativeSlots);
    Historical807::Observer observer;
    observer.observe(*game);observer.sample(*game);Historical807::digest(*game);
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
    SDL_Log("HISTORICAL_RESULT: cycle=%u winner_team=%d outcome=%s",game->getGameCycleCount(),winner,winner?"decided":"time_limit");
    game.reset();currentGame=nullptr;pLocalPlayer=nullptr;pLocalHouse=nullptr;
    return MENU_QUIT_DEFAULT;
}
