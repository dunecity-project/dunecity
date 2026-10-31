// SPDX-License-Identifier: GPL-2.0-or-later
#include <Achievements/AchievementManager.h>
#include <Achievements/AchievementDefaults.h>
#ifdef __EMSCRIPTEN__
#include <misc/WebRuntime.h>
#endif
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace achievements {
namespace {
const char* campaignHouses[] = {"Harkonnen","Atreides","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels"};
const std::string victoryStatistics[] = {
    "VictoriesHarkonnen","VictoriesAtreides","VictoriesOrdos","VictoriesFremen",
    "VictoriesSardaukar","VictoriesMercenary","VictoriesNeutral","VictoriesRebels",
    "VictoriesCorruptique","VictoriesWildspade","VictoriesKleshmersh","VictoriesTharpique"
};
std::string trim(std::string s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    return first==std::string::npos ? "" : s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
std::string read(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path),std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),{});
}
template<class T> std::string list(const T& values) {
    std::ostringstream s; for(const auto& v:values) { if(s.tellp()>0)s<<',';s<<v; }return s.str();
}
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> result;std::istringstream s(text);std::string v;
    while(std::getline(s,v,','))if(!v.empty())result.push_back(v);return result;
}
unsigned bits(unsigned n) { unsigned count=0;while(n){count+=n&1;n>>=1;}return count; }
std::uint64_t add(std::uint64_t a,std::uint64_t b) {
    return a>std::numeric_limits<std::uint64_t>::max()-b?std::numeric_limits<std::uint64_t>::max():a+b;
}
}

AchievementManager& AchievementManager::instance(){static AchievementManager manager;return manager;}

std::uint64_t AchievementManager::number(const std::string& s) {
    if(s.empty()||s.front()=='-')return 0;
    try{std::size_t used=0;auto n=std::stoull(s,&used);return used==s.size()?n:0;}catch(...){return 0;}
}

AchievementManager::Ini AchievementManager::parse(const std::string& text) {
    Ini result;std::string section,line;std::istringstream stream(text);
    while(std::getline(stream,line)) {
        if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        line=trim(line);if(line.empty()||line.front()==';'||line.front()=='#')continue;
        if(line.front()=='['&&line.back()==']'){section=line.substr(1,line.size()-2);continue;}
        auto eq=line.find('=');if(eq!=std::string::npos&&!section.empty())
            result[section].emplace(trim(line.substr(0,eq)),trim(line.substr(eq+1)));
    }
    return result;
}

bool AchievementManager::configure(const std::string& catalogPath,const std::string& profile) {
    if(profile==profilePath&&!catalog.empty())return writable;
    save();state.clear();notifications.clear();running=false;dirty=false;writable=true;lastError.clear();catalog.clear();profilePath=profile;
    auto defs=parse(defaultCatalog);
    for(auto& section:parse(read(catalogPath)))defs[section.first]=section.second;
    const std::set<std::string> rules={"Lifetime","Match","Pacifism","TruePacifist","CapturePacifism","NoMercy","Annihilation","Roadkill","TrueColors","CampaignCount","CampaignHouse","ModCampaignCount","NoCasualties","Arsenal","HighDifficulty","VictoryHouses","SpiceTypes","SpiceCount","ChaosCampaign"};
    for(auto& entry:defs) {
        auto& s=entry.second;if(!rules.count(s["Rule"]))continue;
        Achievement a;a.id=entry.first;a.name=s["Name"];a.nameFr=s["NameFr"];
        a.description=s["Description"];a.descriptionFr=s["DescriptionFr"];
        a.rule=s["Rule"];a.statistic=s["Statistic"];a.target=std::max<std::uint64_t>(1,number(s["Target"]));a.secret=s["Secret"]=="true";
        if(a.name.empty())a.name=a.id;if(a.nameFr.empty())a.nameFr=a.name;
        catalog.push_back(std::move(a));
    }
    std::error_code profileError;
    const bool profileExists=!profile.empty()&&std::filesystem::exists(std::filesystem::u8path(profile),profileError);
    if(profileError){writable=false;lastError=profileError.message();return false;}
    if(profileExists) {
        const auto text=read(profile);state=parse(text);
        if(state["Profile"]["Version"]!="1") {
            writable=false;lastError="Unknown or damaged achievement profile; original file preserved.";state.clear();return false;
        }
    }
    return true;
}

void AchievementManager::begin(const MatchInfo& info,const std::string& key,bool resumed) {
    save();active=MatchProgress{};active.info=info;running=info.enabled;
    if(!running)return;
    if(resumed&&!key.empty()) {
        const auto found=state.find("Checkpoint:"+key);
        if(found!=state.end()&&decodeMatch(found->second,info))return;
    }
    auto& serial=state["Profile"]["NextRun"];serial=std::to_string(add(number(serial),1));
    active.runID=serial;active.historyKnown=!resumed;dirty=true;
}
void AchievementManager::end(){save();running=false;}
void AchievementManager::suspend(){running=false;}

void AchievementManager::setCount(const std::string& name,std::uint64_t value) {
    if(!running||active.completed)return;
    active.counts[name]=value;
    auto& recorded=state["Run:"+active.runID][name];const auto high=number(recorded);
    if(value>high){auto& total=state["Statistics"][name];total=std::to_string(add(number(total),value-high));recorded=std::to_string(value);dirty=true;}
}
void AchievementManager::increment(const std::string& name,std::uint64_t amount){
    if(!running||active.completed)return;
    setCount(name,add(active.counts[name],amount));
}
void AchievementManager::enemyDamage(){if(running&&!active.completed)active.damagedEnemy=true;}
void AchievementManager::flameSource(std::uint32_t object){if(running&&!active.completed)active.flameSources.insert(object);}
void AchievementManager::wormSource(std::uint32_t object,unsigned source){if(running&&!active.completed)active.wormSources[object]=source;}
void AchievementManager::enemyDestroyed(std::uint32_t object,bool structure,bool worm,bool flame) {
    if(!running||active.completed||!active.destroyed.insert(object).second)return;
    active.destroyedEnemy=true;increment(structure?"EnemyStructuresDestroyed":"EnemyUnitsDestroyed");
    if(worm)increment("SandwormsKilled");if(flame)increment("FlameTankKills");evaluate();
}
void AchievementManager::unitLost(std::uint32_t object) {
    if(running&&!active.completed&&active.lost.insert(object).second){increment("UnitsLost");evaluate();}
}
void AchievementManager::built(bool structure,const std::string& exclusive) {
    if(!running||active.completed)return;
    increment(structure?"StructuresBuilt":"UnitsBuilt");
    if(!structure&&!exclusive.empty())active.arsenal.insert(exclusive);evaluate();
}
void AchievementManager::sold(std::uint64_t refund){if(running&&!active.completed){increment("StructuresSold");increment("CreditsFromSales",refund);evaluate();}}
void AchievementManager::captured(){if(running&&!active.completed){increment("StructuresCaptured");evaluate();}}
void AchievementManager::crushed(std::uint32_t victim,std::uint32_t vehicle,std::uint64_t cycle) {
    if(!running||active.completed||active.destroyed.count(victim))return;
    enemyDestroyed(victim,false,false,false);increment("InfantryCrushed");
    auto& times=active.crushes[vehicle];times.erase(std::remove_if(times.begin(),times.end(),[&](auto t){return t>cycle||cycle-t>active.info.roadkillWindow;}),times.end());times.push_back(cycle);
    active.counts["RoadkillBest"]=std::max<std::uint64_t>(active.counts["RoadkillBest"],times.size());evaluate();
}
void AchievementManager::refinedSpice(std::uint64_t total) {
    if(total>=active.info.initialRefinedSpice)setCount("SpiceHarvested",total-active.info.initialRefinedSpice);evaluate();
}
void AchievementManager::harvestedType(unsigned type){if(running&&!active.completed){active.spiceTypes|=type;evaluate();}}
void AchievementManager::bloom(){if(running&&!active.completed){increment("SpiceBloomsTriggered");evaluate();}}
void AchievementManager::missile(){if(running&&!active.completed){increment("PalaceMissilesLaunched");evaluate();}}
void AchievementManager::missileResult(bool hitObject, std::uint64_t buildings) {
    if(!running || active.completed) return;
    if(!hitObject) increment("EmptyMissiles");
    auto& best = state["Statistics"]["BestMissileStructures"];
    if(buildings > number(best)) { best = std::to_string(buildings); dirty = true; }
    evaluate();
}
void AchievementManager::palace(){if(running&&!active.completed){increment("PalaceAbilitiesUsed");evaluate();}}

void AchievementManager::palaceSabotaged(){if(running&&!active.completed){increment("PalacesSabotaged");evaluate();}}

void AchievementManager::finish(bool won,bool enemiesRemain,bool localEliminated,bool allyEliminated,bool allySurvives) {
    if(!running||active.completed)return;
    const auto& record=state["Run:"+active.runID];
    const bool counted=record.count(won?"GamesWon":"GamesLost");
    if(!counted){increment(won?"GamesWon":"GamesLost");if(won)increment("Victories"+active.info.house);}
    else active.counts[won ? "GamesWon" : "GamesLost"] = 1;
    if(won&&active.info.mode==Mode::Campaign&&active.info.mission==22) {
        state["Campaigns"][active.info.house]="1";state["CampaignsByMod"][active.info.mod+":"+active.info.house]="1";dirty=true;
    }
    if(won && active.info.coopCompletionEligible && !active.info.coopSession.empty()) {
        const auto identity = active.info.mod + ":" + active.info.coopSession;
        if(state["CoopCampaigns"].emplace(identity, "1").second) {
            increment("CoopCampaignsCompleted"); dirty = true;
        }
    }
    if(won && active.info.cooperative) {
        if(localEliminated && allySurvives) increment("CoopVictoriesRescued");
        if(!localEliminated && allyEliminated) increment("CoopVictoriesRescuing");
    }
    evaluate(won,enemiesRemain);active.completed=true;save();
}

std::uint64_t AchievementManager::statistic(const std::string& name) const {
    const auto s=state.find("Statistics");if(s==state.end())return 0;
    auto v=s->second.find(name);return v==s->second.end()?0:number(v->second);
}
void AchievementManager::campaignScore(int score) {
    // The campaign results screen supplies its actual displayed score after victory.
    if(!running || !active.completed || active.info.mode!=Mode::Campaign
       || active.counts["GamesWon"]==0 || score<0) return;
    auto& best=state["Statistics"]["BestCampaignScore"];
    if(static_cast<std::uint64_t>(score)>number(best)) {
        best=std::to_string(score);dirty=true;
    }
    // Record only the highest score seen for this winning run, including reloads.
    auto& recorded = state["Run:" + active.runID]["CampaignScore"];
    const auto previous = number(recorded);
    if(static_cast<std::uint64_t>(score) > previous) {
        auto& total = state["Statistics"]["CampaignScoreTotal"];
        total = std::to_string(add(number(total), static_cast<std::uint64_t>(score) - previous));
        recorded = std::to_string(score); dirty = true;
    }
    evaluate();save();
}
bool AchievementManager::unlocked(const std::string& id) const {
    const auto s=state.find("Unlocked");return s!=state.end()&&s->second.count(id);
}
std::uint64_t AchievementManager::progress(const Achievement& a) const {
    return progress(a, false);
}
std::uint64_t AchievementManager::progress(const Achievement& a,bool revealSecret) const {
    if(unlocked(a.id))return a.target;
    if(a.secret&&!revealSecret)return 0;
    if(a.rule=="Lifetime")return std::min(a.target,statistic(a.statistic));
    if(a.rule=="CampaignHouse"){auto s=state.find("Campaigns");return s!=state.end()&&s->second.count(a.statistic)?1:0;}
    if(a.rule=="ModCampaignCount") {
        const auto s=state.find("CampaignsByMod");std::uint64_t count=0;
        if(s!=state.end())for(const auto& entry:s->second)
            if(entry.first.size()>a.statistic.size()&&entry.first.compare(0,a.statistic.size(),a.statistic)==0
               &&entry.first[a.statistic.size()]==':')++count;
        return std::min(count,a.target);
    }
    if(a.rule=="VictoryHouses") {
        std::uint64_t count=0;
        for(const auto& name:victoryStatistics) count+=statistic(name)>0;
        return std::min(count,a.target);
    }
    if(a.rule=="CampaignCount") {
        const auto s=state.find("Campaigns");
        if(s==state.end())return 0;
        if(a.statistic=="Any")return std::min<std::uint64_t>(a.target,s->second.size());
        std::uint64_t count=0;for(auto h:campaignHouses)count+=s->second.count(h);
        return std::min(count,a.target);
    }
    return 0;
}
void AchievementManager::evaluate(bool victory,bool enemiesRemain) {
    if(!running)return;
    const bool pacifist=active.historyKnown&&!active.destroyedEnemy;
    for(const auto& a:catalog) {
        if(unlocked(a.id))continue;
        bool earned=false;
        if(a.rule=="Lifetime"||a.rule=="CampaignCount"||a.rule=="CampaignHouse"||a.rule=="ModCampaignCount"||a.rule=="VictoryHouses"){
            earned=progress(a,true)>=a.target;
        }
        else if(a.rule=="Match"){
            const auto count=active.counts.find(a.statistic);
            earned=count!=active.counts.end()&&count->second>=a.target;
        }
        else if(a.rule=="Roadkill"){
            const auto count=active.counts.find("RoadkillBest");
            earned=count!=active.counts.end()&&count->second>=a.target;
        }
        else if(a.rule=="Arsenal")earned=active.arsenal.size()>=a.target;
        else if(a.rule=="SpiceTypes")earned=(active.spiceTypes&a.target)==a.target;
        else if(a.rule=="SpiceCount")earned=bits(active.spiceTypes)>=a.target;
        else if(victory) {
            if(a.rule=="ChaosCampaign")earned=active.info.mode==Mode::Campaign&&active.info.mission==22&&active.info.chaosCampaignEligible;
            else if(a.rule=="Pacifism")earned=pacifist;
            else if(a.rule=="TruePacifist")earned=active.historyKnown&&!active.damagedEnemy&&!active.destroyedEnemy;
            else if(a.rule=="CapturePacifism"){
                const auto count=active.counts.find("StructuresCaptured");
                earned=pacifist&&count!=active.counts.end()&&count->second>0;
            }
            else if(a.rule=="NoCasualties"){
                const auto count=active.counts.find("UnitsLost");
                earned=active.historyKnown&&(count==active.counts.end()||count->second==0);
            }
            else if(a.rule=="TrueColors")earned=(active.info.mode==Mode::Custom||active.info.mode==Mode::Multiplayer)&&active.info.color!=active.info.defaultColor;
            else if(a.rule=="NoMercy")earned=active.info.mode==Mode::Campaign&&active.info.mission==1&&active.info.hadEnemies&&!enemiesRemain;
            else if(a.rule=="Annihilation")earned=active.info.mode!=Mode::Campaign&&active.info.hadEnemies&&!enemiesRemain;
            else if(a.rule=="HighDifficulty")earned=active.info.highDifficulty&&(active.info.mode==Mode::Custom||active.info.mode==Mode::Multiplayer);
        }
        if(earned)unlock(a.id);
    }
}
bool AchievementManager::unlock(const std::string& id) {
    if(unlocked(id)||std::none_of(catalog.begin(),catalog.end(),[&](const auto& a){return a.id==id;}))return false;
    state["Unlocked"][id]=std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    notifications.push_back(id);dirty=true;return true;
}
std::string AchievementManager::takeNotification(){if(notifications.empty())return {};auto id=std::move(notifications.front());notifications.pop_front();return id;}
std::string AchievementManager::displayName(const Achievement& a,bool fr) const{return a.secret&&!unlocked(a.id)?"???":fr?a.nameFr:a.name;}
std::string AchievementManager::displayDescription(const Achievement& a,bool fr) const{return a.secret&&!unlocked(a.id)?(fr?"Haut fait secret.":"Secret achievement."):fr?a.descriptionFr:a.description;}

void AchievementManager::encodeMatch(Section& s) const {
    s.clear();s["RunID"]=active.runID;s["House"]=active.info.house;s["Mod"]=active.info.mod;s["Mode"]=std::to_string(static_cast<int>(active.info.mode));
    s["Mission"]=std::to_string(active.info.mission);s["Color"]=std::to_string(active.info.color);s["DefaultColor"]=std::to_string(active.info.defaultColor);
    s["SpiceBaseline"]=std::to_string(active.info.initialRefinedSpice);s["HadEnemies"]=active.info.hadEnemies?"1":"0";s["HighDifficulty"]=active.info.highDifficulty?"1":"0";
    s["HistoryKnown"]=active.historyKnown?"1":"0";s["Damaged"]=active.damagedEnemy?"1":"0";s["DestroyedEnemy"]=active.destroyedEnemy?"1":"0";
    s["ChaosCampaignEligible"]=active.info.chaosCampaignEligible?"1":"0";
    s["Completed"]=active.completed?"1":"0";s["SpiceTypes"]=std::to_string(active.spiceTypes);s["Destroyed"]=list(active.destroyed);s["Lost"]=list(active.lost);s["Arsenal"]=list(active.arsenal);
    s["FlameSources"]=list(active.flameSources);
    for(const auto& c:active.counts)s["Count."+c.first]=std::to_string(c.second);
    for(const auto& c:active.crushes)s["Crush."+std::to_string(c.first)]=list(c.second);
    for(const auto& c:active.wormSources)s["WormSource."+std::to_string(c.first)]=std::to_string(c.second);
}
bool AchievementManager::decodeMatch(const Section& section,const MatchInfo& info) {
    const std::string empty;
    const auto value=[&](const char* key)->const std::string& {
        const auto found=section.find(key);
        return found==section.end()?empty:found->second;
    };
    if(value("RunID").empty()||value("House")!=info.house||value("Mod")!=info.mod
       ||number(value("Mode"))!=static_cast<unsigned>(info.mode)
       ||number(value("Mission"))!=static_cast<unsigned>(std::max(0,info.mission)))return false;
    active.runID=value("RunID");
    active.historyKnown=value("HistoryKnown")=="1";
    active.damagedEnemy=value("Damaged")=="1";
    active.destroyedEnemy=value("DestroyedEnemy")=="1";
    active.completed=value("Completed")=="1";
    active.spiceTypes=static_cast<unsigned>(number(value("SpiceTypes"))&31);
    active.info.color=static_cast<int>(number(value("Color")));
    active.info.defaultColor=static_cast<int>(number(value("DefaultColor")));
    active.info.initialRefinedSpice=number(value("SpiceBaseline"));
    active.info.chaosCampaignEligible=info.chaosCampaignEligible&&value("ChaosCampaignEligible")=="1";
    active.info.hadEnemies=value("HadEnemies")=="1";
    active.info.highDifficulty=value("HighDifficulty")=="1";
    for(const auto& v:split(value("Destroyed")))active.destroyed.insert(static_cast<std::uint32_t>(number(v)));
    for(const auto& v:split(value("Lost")))active.lost.insert(static_cast<std::uint32_t>(number(v)));
    for(const auto& v:split(value("FlameSources")))active.flameSources.insert(static_cast<std::uint32_t>(number(v)));
    for(const auto& v:split(value("Arsenal")))active.arsenal.insert(v);
    for(const auto& e:section){if(e.first.compare(0,6,"Count.")==0)active.counts[e.first.substr(6)]=number(e.second);
        else if(e.first.compare(0,6,"Crush.")==0){auto& queue=active.crushes[static_cast<std::uint32_t>(number(e.first.substr(6)))];for(const auto& v:split(e.second))queue.push_back(number(v));}
        else if(e.first.compare(0,11,"WormSource.")==0)active.wormSources[static_cast<std::uint32_t>(number(e.first.substr(11)))]=static_cast<unsigned>(number(e.second));}
    return true;
}
void AchievementManager::checkpoint(const std::string& key){if(running&&!key.empty()){encodeMatch(state["Checkpoint:"+key]);dirty=true;save();}}

bool AchievementManager::save() {
    if(!dirty||profilePath.empty())return true;
    if(!writable)return false;
    try {
        const auto path=std::filesystem::u8path(profilePath);auto temp=path;temp+=".tmp";
        if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
        state["Profile"]["Version"]="1";
        std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<"; Dune Legacy Tornie local achievements.\n";
        for(const auto& section:state){f<<'['<<section.first<<"]\n";for(const auto& v:section.second)f<<v.first<<'='<<v.second<<'\n';f<<'\n';}
        f.flush();if(!f)throw std::runtime_error("Cannot write achievement profile");f.close();
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace achievement profile");
#else
        std::filesystem::rename(temp,path);
#endif
#ifdef __EMSCRIPTEN__
        WebRuntime::syncPersistentFiles();
#endif
        dirty=false;lastError.clear();return true;
    }catch(const std::exception& e){lastError=e.what();return false;}
}
}

bool achievements::AchievementManager::completed() const {
    return !catalog.empty() && std::all_of(catalog.begin(),catalog.end(),[this](const Achievement& a){return unlocked(a.id);});
}
