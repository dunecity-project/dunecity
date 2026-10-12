// Included only by run-save-defence-probe.py, never by the shipped game.
//
// Loads a real saved game and runs the full simulation forward, sampling how
// every QuantBot house answers attacks on what it owns: buildings (base,
// colonies, R/C/I zones) and harvesters. It reports, per sample, how many of
// the house's free ground combat troops are fighting, walking to a fight or
// sitting idle while something nearby is being hit. Nothing is simulated or
// staged: the save is played exactly as the game would play it.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <SpecialVehicle.h>
#include <data.h>
#include <sand.h>
#include <players/QuantBot.h>
#include <structures/StructureBase.h>
#include <units/GroundUnit.h>
#include <units/UnitBase.h>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

bool defenceProbeCombatUnit(const UnitBase* unit) {
    const int item=unit->getItemID();
    return unit->isActive() && unit->getHealth()>0 && unit->isAGroundUnit() && unit->canAttack()
        && !isHarvesterLikeUnit(item) && !isCarryallUnit(item) && !isAmbientUnit(item)
        && item!=Unit_MCV && item!=Unit_Sandworm && item!=Unit_Saboteur && item!=Unit_Frigate;
}

bool defenceProbeAsset(const ObjectBase* object, const House* house) {
    return object && object->getHealth()>0 && object->getOwner()==house
        && (object->isAStructure() || isHarvesterLikeUnit(object->getItemID()));
}

struct DefenceProbeTotals {
    int samples=0, samplesUnderAttack=0;
    long idleNearSum=0, engagedSum=0, walkingSum=0, freeSum=0;
    int structuresAtStart=0, enemyKilledNearAssets=0, armyLost=0, structuresLost=0;
    std::set<Uint32> buildings;
    bool dumped=false;
    std::set<Uint32> army;
    std::map<Uint32,int> lastHealth;
    long assetDamage=0;
};

int runSaveDefenceProbe() {
    const char* savePath=std::getenv("DEFENCE_PROBE_SAVE");
    const char* cyclesText=std::getenv("DEFENCE_PROBE_CYCLES");
    const char* intervalText=std::getenv("DEFENCE_PROBE_INTERVAL");
    const int cycles=cyclesText && *cyclesText ? std::atoi(cyclesText) : 7500;
    const int interval=intervalText && *intervalText ? std::atoi(intervalText) : 125;
    try {
        auto game=std::make_unique<Game>();
        currentGame=game.get();
        game->bReplay=true;  // No interactive briefing or public match analytics.
        game->initGame(GameInitSettings(std::string(savePath ? savePath : "")));
        game->bReplay=false;
        game->initializeGameLoop();
        game->bPause=false;

        std::vector<QuantBot*> bots;
        for(int h=0;h<NUM_HOUSES;++h) {
            auto* house=game->getHouse(h);
            if(!house) continue;
            for(const auto& player:house->getPlayerList())
                if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
                    bots.push_back(bot);
                    SDL_Log("DEFENCE_PROBE_HOUSE: house=%s id=%d team=%d difficulty=%d mode=%d whole_army=%d support=%d",
                        getHouseNameByNumber(static_cast<HOUSETYPE>(h)).c_str(),h,house->getTeamID(),
                        static_cast<int>(bot->difficulty),static_cast<int>(bot->gameMode),
                        bot->wholeArmyBaseDefence() ? 1 : 0,bot->supportMode ? 1 : 0);
                }
        }
        std::map<const QuantBot*,DefenceProbeTotals> totals;
        // Enemies seen within 10 tiles of an asset, per bot, so their deaths
        // count as kills made defending it.
        std::map<const QuantBot*,std::set<Uint32>> enemiesNearAssets;
        const Uint32 start=game->getGameCycleCount();
        SDL_Log("DEFENCE_PROBE_START: cycle=%u cycles=%d interval=%d",start,cycles,interval);

        for(int step=0;step<=cycles;++step) {
            if(step>0) game->updateGameState();
            if(step%interval!=0) continue;
            for(const auto* bot:bots) {
                const House* house=bot->getHouse();
                const int team=house->getTeamID();
                auto& total=totals[bot];
                // Assets under fire right now: an enemy whose target is ours
                // and is within its weapon range.
                std::set<const ObjectBase*> attacked;
                int hostilesNear=0;
                std::vector<Coord> assetSpots;
                for(const auto* structure:structureList)
                    if(structure->getOwner()==house && structure->getHealth()>0) assetSpots.push_back(structure->getLocation());
                for(const auto* unit:unitList)
                    if(defenceProbeAsset(unit,house)) assetSpots.push_back(unit->getLocation());
                auto nearAsset=[&](Coord at,int tiles) {
                    for(const auto& spot:assetSpots) if(blockDistance(at,spot)<=tiles) return true;
                    return false;
                };
                for(const auto* unit:unitList) {
                    if(!unit->isActive() || unit->getHealth()<=0 || unit->getOwner()->getTeamID()==team) continue;
                    if(!unit->canAttack()) continue;
                    const auto* target=unit->getTarget();
                    if(defenceProbeAsset(target,house) && unit->isInWeaponRange(target)) attacked.insert(target);
                    if(!unit->isAFlyingUnit() && nearAsset(unit->getLocation(),10)) {
                        ++hostilesNear;
                        enemiesNearAssets[bot].insert(unit->getObjectID());
                    }
                }
                for(auto it=total.army.begin();it!=total.army.end();) {
                    const auto* object=game->getObjectManager().getObject(*it);
                    if(!object || object->getHealth()<=0 || object->getOwner()!=house) { ++total.armyLost; it=total.army.erase(it); }
                    else ++it;
                }
                for(auto it=enemiesNearAssets[bot].begin();it!=enemiesNearAssets[bot].end();) {
                    const auto* object=game->getObjectManager().getObject(*it);
                    if(!object || object->getHealth()<=0) { ++total.enemyKilledNearAssets; it=enemiesNearAssets[bot].erase(it); }
                    else ++it;
                }
                for(auto it=total.buildings.begin();it!=total.buildings.end();) {
                    const auto* object=game->getObjectManager().getObject(*it);
                    if(!object || object->getHealth()<=0 || object->getOwner()!=house) { ++total.structuresLost; it=total.buildings.erase(it); }
                    else ++it;
                }
                int structures=0, structureHealth=0, zones=0;
                for(const auto* structure:structureList) {
                    if(structure->getOwner()!=house || structure->getHealth()<=0) continue;
                    ++structures; structureHealth+=structure->getHealth().lround();
                    total.buildings.insert(structure->getObjectID());
                    const auto previous=total.lastHealth.find(structure->getObjectID());
                    if(previous!=total.lastHealth.end() && previous->second>structure->getHealth().lround())
                        total.assetDamage+=previous->second-structure->getHealth().lround();
                    total.lastHealth[structure->getObjectID()]=structure->getHealth().lround();
                    if(isZoneStructure(structure->getItemID())) ++zones;
                }
                if(total.samples==0) total.structuresAtStart=structures;
                int army=0, free=0, engaged=0, walking=0, idle=0, idleNear=0, forced=0, hunt=0, baseDefence=0;
                for(const auto* unit:unitList) {
                    if(unit->getOwner()!=house || !defenceProbeCombatUnit(unit)) continue;
                    ++army;
                    total.army.insert(unit->getObjectID());
                    if(unit->wasForced()) ++forced;
                    if(unit->getAttackMode()==HUNT) ++hunt;
                    if(bot->unitRole(unit,{QuantBot::UnitRole::BaseDefence})==QuantBot::UnitRole::BaseDefence) ++baseDefence;
                    if(bot->unitRole(unit,{QuantBot::UnitRole::Human,QuantBot::UnitRole::Repair})!=QuantBot::UnitRole::Free) continue;
                    ++free;
                    const auto* target=unit->getTarget();
                    const bool hostile=target && target->getHealth()>0 && target->getOwner()
                        && target->getOwner()->getTeamID()!=team;
                    if(hostile && blockDistance(unit->getLocation(),target->getLocation())<=unit->getWeaponRange()+1) ++engaged;
                    else if(hostile || unit->isMoving()) ++walking;
                    else {
                        ++idle;
                        for(const auto* asset:attacked)
                            if(blockDistance(unit->getLocation(),asset->getLocation())<=20) { ++idleNear; break; }
                    }
                }
                if(!attacked.empty()) {
                    // Where the idle troops stand relative to the nearest asset under fire.
                    int bands[6]={0,0,0,0,0,0};
                    for(const auto* unit:unitList) {
                        if(unit->getOwner()!=house || !defenceProbeCombatUnit(unit) || unit->isMoving()) continue;
                        const auto* target=unit->getTarget();
                        if(target && target->getHealth()>0 && target->getOwner()->getTeamID()!=team) continue;
                        int nearest=1<<20;
                        for(const auto* asset:attacked) nearest=std::min(nearest,blockDistance(unit->getLocation(),asset->getLocation()).lround());
                        bands[nearest<=12?0:nearest<=20?1:nearest<=40?2:nearest<=60?3:nearest<=100?4:5]++;
                    }
                    const auto* first=*attacked.begin();
                    SDL_Log("DEFENCE_PROBE_IDLE_BANDS: house=%s cycle=%u at=%d,%d le12=%d le20=%d le40=%d le60=%d le100=%d gt100=%d",
                        getHouseNameByNumber(static_cast<HOUSETYPE>(house->getHouseID())).c_str(),game->getGameCycleCount(),
                        first->getLocation().x,first->getLocation().y,bands[0],bands[1],bands[2],bands[3],bands[4],bands[5]);
                }
                // Who is holding the idle forced troops? One dump per house, at
                // the first sample with something under attack.
                if(std::getenv("DEFENCE_PROBE_DUMP") && step>=std::atoi(std::getenv("DEFENCE_PROBE_DUMP")) && !attacked.empty() && !total.dumped) {
                    total.dumped=true;
                    std::map<std::string,int> kinds;
                    int printed=0;
                    for(const auto* unit:unitList) {
                        if(unit->getOwner()!=house || !defenceProbeCombatUnit(unit) || !unit->wasForced()) continue;
                        const auto* target=unit->getTarget();
                        const Coord dest=unit->getDestination();
                        const int destDistance=dest.isValid() ? blockDistance(unit->getLocation(),dest).lround() : -1;
                        const char* role="free";
                        if(bot->unitRole(unit,{QuantBot::UnitRole::Human})!=QuantBot::UnitRole::Free) role="human";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::Repair})!=QuantBot::UnitRole::Free) role="repair";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::BaseDefence})!=QuantBot::UnitRole::Free) role="basedef";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::Defence})!=QuantBot::UnitRole::Free) role="defence";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::Wave})!=QuantBot::UnitRole::Free) role="wave";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::Escort})!=QuantBot::UnitRole::Free) role="escort";
                        else if(bot->unitRole(unit,{QuantBot::UnitRole::ColonyGuard})!=QuantBot::UnitRole::Free) role="colonyguard";
                        const std::string key=std::string("mode=")+std::to_string(int(unit->getAttackMode()))
                            +" role="+role+" squad="+std::to_string(int(bot->groundSquad.count(unit->getObjectID())))
                            +" moving="+std::to_string(int(unit->isMoving()))
                            +" target="+(target?(target->getOwner()->getTeamID()==team?"own":"hostile"):"none")
                            +" dest="+(destDistance<0?"none":destDistance==0?"here":destDistance<=5?"near":destDistance<=20?"mid":"far")
                            +" path="+std::to_string(int(unit->getPlannedPath().size()>0))
                            +" nocloser="+std::to_string(int(unit->noCloserPointCount>0));
                        ++kinds[key];
                        if(printed<15 && !unit->isMoving() && !target) {
                            ++printed;
                            SDL_Log("DEFENCE_PROBE_FORCED_UNIT: house=%s id=%u item=%d at=%d,%d dest=%d,%d mode=%d role=%s squad=%d path=%zu nocloser=%d pickup=%d",
                                getHouseNameByNumber(static_cast<HOUSETYPE>(house->getHouseID())).c_str(),unit->getObjectID(),
                                unit->getItemID(),unit->getLocation().x,unit->getLocation().y,dest.x,dest.y,int(unit->getAttackMode()),
                                role,int(bot->groundSquad.count(unit->getObjectID())),unit->getPlannedPath().size(),
                                int(unit->noCloserPointCount),int(static_cast<const GroundUnit*>(unit)->isAwaitingPickup()));
                        }
                    }
                    for(const auto& [key,count]:kinds)
                        SDL_Log("DEFENCE_PROBE_FORCED_KIND: house=%s count=%d %s",
                            getHouseNameByNumber(static_cast<HOUSETYPE>(house->getHouseID())).c_str(),count,key.c_str());
                }
                ++total.samples;
                if(!attacked.empty()) {
                    ++total.samplesUnderAttack;
                    total.idleNearSum+=idleNear; total.engagedSum+=engaged;
                    total.walkingSum+=walking; total.freeSum+=free;
                }
                SDL_Log("DEFENCE_PROBE_SAMPLE: house=%s cycle=%u structures=%d zones=%d structure_hp=%d attacked_assets=%zu hostiles_near=%d "
                        "army=%d free=%d engaged=%d walking=%d idle=%d idle_near_attack=%d forced=%d hunt=%d base_defence=%d",
                    getHouseNameByNumber(static_cast<HOUSETYPE>(house->getHouseID())).c_str(),
                    game->getGameCycleCount(),structures,zones,structureHealth,attacked.size(),hostilesNear,
                    army,free,engaged,walking,idle,idleNear,forced,hunt,baseDefence);
            }
        }
        for(const auto* bot:bots) {
            const auto& total=totals[bot];
            int structures=0;
            for(const auto* structure:structureList)
                if(structure->getOwner()==bot->getHouse() && structure->getHealth()>0) ++structures;
            const long n=std::max(1,total.samplesUnderAttack);
            SDL_Log("DEFENCE_PROBE_SUMMARY: house=%s structures_start=%d structures_end=%d structures_lost=%d structure_damage=%ld "
                    "enemy_killed_near_assets=%d army_lost=%d samples_under_attack=%d mean_free=%.1f mean_engaged=%.1f mean_walking=%.1f mean_idle_near_attack=%.1f",
                getHouseNameByNumber(static_cast<HOUSETYPE>(bot->getHouse()->getHouseID())).c_str(),
                total.structuresAtStart,structures,total.structuresLost,total.assetDamage,total.enemyKilledNearAssets,total.armyLost,total.samplesUnderAttack,
                double(total.freeSum)/n,double(total.engagedSum)/n,double(total.walkingSum)/n,double(total.idleNearSum)/n);
        }
        SDL_Log("DEFENCE_PROBE_PASS: cycle=%u",game->getGameCycleCount());
    } catch(const std::exception& error) {
        SDL_Log("DEFENCE_PROBE_FAIL: %s",error.what());
    }
    return MENU_QUIT_DEFAULT;
}

} // namespace
