// SPDX-License-Identifier: GPL-2.0-or-later
#include <Achievements/AchievementManager.h>
#include <Achievements/AchievementDefaults.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <algorithm>

using namespace achievements;
namespace {
struct Fixture {
    std::filesystem::path dir;
    AchievementManager manager;
    MatchInfo info;
    Fixture(){
        static unsigned serial=0;
        dir=std::filesystem::temp_directory_path()/("dunelegacy-achievements-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        std::filesystem::create_directories(dir);
        REQUIRE(manager.configure("",(dir/"achievements.ini").u8string()));
        info.house="Atreides";info.hadEnemies=true;info.color=1;info.defaultColor=1;manager.begin(info);
    }
    ~Fixture(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
};
}
TEST_CASE_METHOD(Fixture,"Pacifism distinguishes damage, deaths and captures","[achievements]"){
    SECTION("capture and other-faction/environment deaths do not invalidate"){
        manager.captured();manager.finish(true,true);
        REQUIRE(manager.unlocked("PACIFISM"));REQUIRE(manager.unlocked("TRUE_PACIFIST"));REQUIRE(manager.unlocked("SANS_TIRER"));
    }
    SECTION("nonlethal damage only invalidates True Pacifist"){
        manager.enemyDamage();manager.finish(true,true);
        REQUIRE(manager.unlocked("PACIFISM"));REQUIRE_FALSE(manager.unlocked("TRUE_PACIFIST"));
    }
    SECTION("attributed kill invalidates both"){
        manager.enemyDestroyed(10,false,false,false);manager.finish(true,false);
        REQUIRE_FALSE(manager.unlocked("PACIFISM"));REQUIRE_FALSE(manager.unlocked("TRUE_PACIFIST"));
    }
    SECTION("loss and quitting never grant victory achievements"){
        manager.captured();manager.finish(false,false);manager.end();
        REQUIRE_FALSE(manager.unlocked("PACIFISM"));REQUIRE_FALSE(manager.unlocked("TRUE_COLORS"));
        REQUIRE(manager.statistic("GamesLost")==1);REQUIRE(manager.statistic("GamesWon")==0);
    }
}
TEST_CASE_METHOD(Fixture,"Roadkill uses a sliding cycle window per vehicle","[achievements]"){
    info.roadkillWindow=100;manager.begin(info);
    SECTION("same vehicle and inclusive boundary"){
        for(unsigned i=0;i<5;i++)manager.crushed(100+i,10,20+i*25);
        REQUIRE(manager.unlocked("ROADKILL"));REQUIRE(manager.statistic("InfantryCrushed")==5);
        manager.crushed(104,10,120);REQUIRE(manager.statistic("EnemyUnitsDestroyed")==5);
    }
    SECTION("different vehicles do not combine"){
        for(unsigned i=0;i<5;i++)manager.crushed(100+i,10+i,20);
        REQUIRE_FALSE(manager.unlocked("ROADKILL"));
    }
    SECTION("outside window"){
        for(unsigned i=0;i<5;i++)manager.crushed(100+i,10,i*26);
        REQUIRE_FALSE(manager.unlocked("ROADKILL"));
    }
}
TEST_CASE_METHOD(Fixture,"True Colors requires a custom or multiplayer victory","[achievements]"){
    for(auto mode:{Mode::Campaign,Mode::Skirmish,Mode::Custom,Mode::Multiplayer}){
        AchievementManager isolated;REQUIRE(isolated.configure("",""));info.mode=mode;info.color=3;isolated.begin(info);isolated.finish(true,true);
        REQUIRE(isolated.unlocked("TRUE_COLORS")== (mode==Mode::Custom||mode==Mode::Multiplayer));
    }
    info.color=info.defaultColor;manager.begin(info);manager.finish(true,true);REQUIRE_FALSE(manager.unlocked("TRUE_COLORS"));
}
TEST_CASE_METHOD(Fixture,"No Mercy inspects all forces before campaign mission-one victory","[achievements]"){
    info.mode=Mode::Campaign;info.mission=1;manager.begin(info);
    SECTION("remaining noncombat unit prevents award"){manager.finish(true,true);REQUIRE_FALSE(manager.unlocked("NO_MERCY"));}
    SECTION("all eliminated"){manager.finish(true,false);REQUIRE(manager.unlocked("NO_MERCY"));}
    SECTION("wrong mission"){info.mission=2;manager.begin(info);manager.finish(true,false);REQUIRE_FALSE(manager.unlocked("NO_MERCY"));}
    SECTION("empty initial map"){info.hadEnemies=false;manager.begin(info);manager.finish(true,false);REQUIRE_FALSE(manager.unlocked("NO_MERCY"));}
}
TEST_CASE_METHOD(Fixture,"Annihilation requires the complete opposing force to be gone","[achievements]"){
    manager.finish(true,true);REQUIRE_FALSE(manager.unlocked("TOTAL_ANNIHILATION"));
    manager.begin(info);manager.finish(true,false);REQUIRE(manager.unlocked("TOTAL_ANNIHILATION"));
}
TEST_CASE_METHOD(Fixture,"Profile round trip preserves awards and lifetime statistics","[achievements]"){
    manager.captured();manager.built(true);manager.built(false);manager.unitLost(5);manager.enemyDestroyed(9,true,false,true);manager.finish(true,false);
    AchievementManager reloaded;REQUIRE(reloaded.configure("",(dir/"achievements.ini").u8string()));
    REQUIRE(reloaded.unlocked("FIRST_VICTORY"));REQUIRE(reloaded.statistic("StructuresCaptured")==1);REQUIRE(reloaded.statistic("StructuresBuilt")==1);
    REQUIRE(reloaded.statistic("UnitsBuilt")==1);REQUIRE(reloaded.statistic("UnitsLost")==1);REQUIRE(reloaded.statistic("EnemyStructuresDestroyed")==1);
    REQUIRE(reloaded.statistic("VictoriesAtreides")==1);REQUIRE_FALSE(reloaded.unlocked("NO_CASUALTIES"));
}
TEST_CASE_METHOD(Fixture,"Reloading a checkpoint restores eligibility and does not double-count","[achievements]"){
    manager.built(false);manager.checkpoint("save-hash");manager.enemyDestroyed(1,false,false,false);manager.built(false);manager.save();
    AchievementManager reloaded;REQUIRE(reloaded.configure("",(dir/"achievements.ini").u8string()));
    reloaded.begin(info,"save-hash",true);REQUIRE(reloaded.match().historyKnown);REQUIRE_FALSE(reloaded.match().destroyedEnemy);
    reloaded.built(false);REQUIRE(reloaded.statistic("UnitsBuilt")==2);
    reloaded.finish(true,false);REQUIRE(reloaded.unlocked("PACIFISM"));
    reloaded.begin(info,"save-hash",true);reloaded.finish(true,false);REQUIRE(reloaded.statistic("GamesWon")==1);
}
TEST_CASE_METHOD(Fixture,"Legacy saves do not fabricate unknown no-loss or pacifism history","[achievements]"){
    manager.begin(info,"untracked-old-save",true);manager.finish(true,false);
    REQUIRE(manager.unlocked("FIRST_VICTORY"));REQUIRE_FALSE(manager.unlocked("PACIFISM"));REQUIRE_FALSE(manager.unlocked("TRUE_PACIFIST"));REQUIRE_FALSE(manager.unlocked("NO_CASUALTIES"));
}
TEST_CASE_METHOD(Fixture,"Profiles cannot transplant a checkpoint to another faction or mod","[achievements]"){
    manager.checkpoint("save-hash");info.house="Ordos";manager.begin(info,"save-hash",true);REQUIRE_FALSE(manager.match().historyKnown);
}
TEST_CASE_METHOD(Fixture,"Captures, worms, flames and production reach cumulative thresholds","[achievements]"){
    for(int i=0;i<10;i++)manager.captured();REQUIRE(manager.unlocked("INFILTRATION"));
    manager.enemyDestroyed(12,false,true,false);REQUIRE(manager.unlocked("WORM_HUNTER"));
    for(unsigned i=0;i<50;i++)manager.enemyDestroyed(100+i,false,false,true);REQUIRE(manager.unlocked("FLAME_MASTER"));
    for(int i=0;i<100;i++)manager.built(true);REQUIRE(manager.unlocked("BUILDER"));
    for(int i=0;i<500;i++)manager.built(false);REQUIRE(manager.unlocked("ARMY_OF_ARRAKIS"));
}
TEST_CASE_METHOD(Fixture,"Arsenal requires distinct produced types and known spice types","[achievements]"){
    manager.built(false,"FlameTank");manager.built(false,"FlameTank");manager.built(false,"RocketTrike");REQUIRE_FALSE(manager.unlocked("TORNIE_ARSENAL"));
    manager.built(false,"SonicTrike");REQUIRE(manager.unlocked("TORNIE_ARSENAL"));
    manager.harvestedType(2);REQUIRE(manager.unlocked("RED_HARVEST"));REQUIRE_FALSE(manager.unlocked("GREEN_HARVEST"));
    manager.harvestedType(4);REQUIRE(manager.unlocked("GREEN_HARVEST"));REQUIRE(manager.unlocked("SPICE_COLLECTOR"));
    manager.bloom();manager.palace();REQUIRE(manager.unlocked("SPICE_BLOOM"));REQUIRE(manager.unlocked("PALACE_POWER"));
}
TEST_CASE_METHOD(Fixture,"Spice credits exclude the initial balance of an old save","[achievements]"){
    info.initialRefinedSpice=8000;manager.begin(info,"old-save",true);
    manager.refinedSpice(17999);REQUIRE_FALSE(manager.unlocked("THE_SPICE_MUST_FLOW"));
    manager.refinedSpice(18000);REQUIRE(manager.unlocked("THE_SPICE_MUST_FLOW"));REQUIRE(manager.statistic("SpiceHarvested")==10000);
    manager.begin(info);manager.refinedSpice(98000);REQUIRE(manager.unlocked("SPICE_EMPIRE"));
}
TEST_CASE_METHOD(Fixture,"All eight campaign and victory houses are tracked independently","[achievements]"){
    info.mode=Mode::Campaign;info.mission=22;
    for(auto house:{"Atreides","Harkonnen","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels"}){info.house=house;manager.begin(info);manager.finish(true,false);}
    REQUIRE(manager.unlocked("MASTER_OF_ARRAKIS"));REQUIRE_FALSE(manager.unlocked("HOUSE_COLLECTOR"));REQUIRE(manager.unlocked("NEUTRAL_COMMANDER"));
    REQUIRE(manager.statistic("GamesWon")==8);
}
TEST_CASE_METHOD(Fixture,"Disabled, replay or cheat sessions emit no achievements or statistics","[achievements]"){
    info.enabled=false;manager.begin(info);manager.captured();manager.built(false);manager.enemyDestroyed(1,false,false,false);manager.finish(true,false);
    REQUIRE(manager.statistic("GamesWon")==0);REQUIRE(manager.statistic("StructuresCaptured")==0);REQUIRE_FALSE(manager.unlocked("FIRST_VICTORY"));
    manager.flameSource(4);manager.wormSource(5,2);manager.crushed(6,7,10);manager.refinedSpice(100);
    REQUIRE(manager.match().counts.empty());REQUIRE(manager.match().flameSources.empty());
    REQUIRE(manager.match().wormSources.empty());REQUIRE(manager.match().destroyed.empty());
}
TEST_CASE_METHOD(Fixture,"Secret and custom stat achievements remain extensible","[achievements]"){
    const auto path=dir/"catalog.ini";std::ofstream f(path);f<<"[SECRET_CAPTURE]\nName=Secret Capture\nNameFr=Capture secrète\nDescription=Capture one\nRule=Lifetime\nStatistic=StructuresCaptured\nTarget=1\nSecret=true\n";f.close();
    AchievementManager custom;REQUIRE(custom.configure(path.u8string(),(dir/"custom.ini").u8string()));
    auto a=std::find_if(custom.definitions().begin(),custom.definitions().end(),[](auto& d){return d.id=="SECRET_CAPTURE";});REQUIRE(a!=custom.definitions().end());
    REQUIRE(custom.displayName(*a,true)=="???");REQUIRE(custom.progress(*a)==0);
    custom.begin(info);custom.captured();REQUIRE(custom.unlocked(a->id));REQUIRE(custom.displayName(*a,true)=="Capture secrète");
}
TEST_CASE_METHOD(Fixture,"A damaged or newer profile is preserved rather than overwritten","[achievements]"){
    const auto path=dir/"future.ini";std::ofstream f(path);f<<"[Profile]\nVersion=999\nProtected=original\n";f.close();
    AchievementManager future;REQUIRE_FALSE(future.configure("",path.u8string()));future.begin(info);future.captured();REQUIRE_FALSE(future.save());
    std::ifstream in(path);const std::string text(std::istreambuf_iterator<char>(in),{});REQUIRE(text.find("Protected=original")!=std::string::npos);
}
TEST_CASE_METHOD(Fixture,"Catalog ships every proposed achievement and notifications fire once","[achievements]"){
    REQUIRE(manager.definitions().size()==73);REQUIRE(manager.unlock("PACIFISM"));REQUIRE_FALSE(manager.unlock("PACIFISM"));
    REQUIRE(manager.takeNotification()=="PACIFISM");REQUIRE(manager.takeNotification().empty());REQUIRE_FALSE(manager.unlock("UNKNOWN"));
}
TEST_CASE_METHOD(Fixture,"Extra factions complete a campaign without replacing the eight required houses","[achievements]"){
    info.house="Tharpique";info.mod="Jericho";info.mode=Mode::Campaign;info.mission=22;manager.begin(info);manager.finish(true,false);
    REQUIRE(manager.unlocked("CONQUER_ARRAKIS"));REQUIRE_FALSE(manager.unlocked("MASTER_OF_ARRAKIS"));REQUIRE_FALSE(manager.unlocked("HOUSE_COLLECTOR"));
}
TEST_CASE_METHOD(Fixture,"Veteran and high-difficulty victories use completed matches","[achievements]"){
    info.highDifficulty=true;
    for(int i=0;i<25;i++){manager.begin(info);manager.finish(true,true);manager.finish(true,true);}
    REQUIRE(manager.statistic("GamesWon")==25);REQUIRE(manager.unlocked("VETERAN_COMMANDER"));REQUIRE(manager.unlocked("AGAINST_THE_ODDS"));
}

TEST_CASE_METHOD(Fixture,"Campaign score uses the displayed victorious score without adding it to lifetime totals","[achievements]"){
    info.mode=Mode::Campaign;manager.begin(info);
    manager.campaignScore(2000);REQUIRE_FALSE(manager.unlocked("RULER_OF_ARRAKIS"));
    manager.finish(true,false);manager.campaignScore(999);REQUIRE_FALSE(manager.unlocked("RULER_OF_ARRAKIS"));
    manager.campaignScore(1000);REQUIRE(manager.unlocked("RULER_OF_ARRAKIS"));
    manager.campaignScore(900);manager.campaignScore(1000);REQUIRE(manager.statistic("BestCampaignScore")==1000);
    AchievementManager reloaded;REQUIRE(reloaded.configure("",(dir/"achievements.ini").u8string()));
    REQUIRE(reloaded.unlocked("RULER_OF_ARRAKIS"));REQUIRE(reloaded.statistic("BestCampaignScore")==1000);
}
TEST_CASE_METHOD(Fixture,"Defeats, custom matches and disabled sessions cannot unlock a campaign score award","[achievements]"){
    SECTION("defeat"){info.mode=Mode::Campaign;manager.begin(info);manager.finish(false,false);}
    SECTION("custom"){info.mode=Mode::Custom;manager.begin(info);manager.finish(true,false);}
    SECTION("disabled"){info.mode=Mode::Campaign;info.enabled=false;manager.begin(info);manager.finish(true,false);}
    manager.campaignScore(1239);REQUIRE_FALSE(manager.unlocked("RULER_OF_ARRAKIS"));
    REQUIRE(manager.statistic("BestCampaignScore")==0);
}
TEST_CASE_METHOD(Fixture,"Jericho campaigns are mod scoped and include all four named factions","[achievements]"){
    info.mode=Mode::Campaign;info.mission=22;info.mod="Tornie";
    for(auto house:{"Atreides","Harkonnen","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels","Corruptique","Wildspade","Kleshmersh","Tharpique"}){
        info.house=house;manager.begin(info);manager.finish(true,false);
    }
    REQUIRE_FALSE(manager.unlocked("JERICHO_MASTER"));
    info.mod="Jericho";
    for(auto house:{"Atreides","Harkonnen","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels","Corruptique","Wildspade","Kleshmersh","Tharpique"}){
        info.house=house;manager.begin(info);manager.finish(true,false);
    }
    REQUIRE(manager.unlocked("JERICHO_MASTER"));
    for(auto id:{"CORRUPTIQUE_COMMANDER","WILDSPADE_COMMANDER","KLESHMERSH_COMMANDER","THARPIQUE_COMMANDER"})REQUIRE(manager.unlocked(id));
}

TEST_CASE_METHOD(Fixture,"House Collector requires twelve distinct victories and persists all named factions","[achievements]"){
    const auto award=std::find_if(manager.definitions().begin(),manager.definitions().end(),[](const auto& a){return a.id=="HOUSE_COLLECTOR";});
    REQUIRE(award!=manager.definitions().end());REQUIRE(award->target==12);
    for(auto house:{"Atreides","Harkonnen","Ordos","Fremen","Sardaukar","Mercenary","Neutral","Rebels"}){
        info.house=house;manager.begin(info);manager.finish(true,false);
    }
    REQUIRE_FALSE(manager.unlocked("HOUSE_COLLECTOR"));REQUIRE(manager.progress(*award)==8);
    info.house="Atreides";manager.begin(info);manager.finish(true,false);
    info.house="Unknown";manager.begin(info);manager.finish(true,false);
    info.house="Tharpique";manager.begin(info);manager.finish(false,false);
    REQUIRE(manager.progress(*award)==8);
    for(auto house:{"Wildspade","Kleshmersh","Corruptique"}){
        info.house=house;manager.begin(info);manager.finish(true,false);
    }
    REQUIRE_FALSE(manager.unlocked("HOUSE_COLLECTOR"));REQUIRE(manager.progress(*award)==11);
    AchievementManager reloaded;REQUIRE(reloaded.configure("",(dir/"achievements.ini").u8string()));
    const auto loadedAward=std::find_if(reloaded.definitions().begin(),reloaded.definitions().end(),[](const auto& a){return a.id=="HOUSE_COLLECTOR";});
    REQUIRE(loadedAward!=reloaded.definitions().end());REQUIRE(reloaded.progress(*loadedAward)==11);
    info.house="Tharpique";reloaded.begin(info);reloaded.finish(true,false);reloaded.finish(true,false);
    REQUIRE(reloaded.unlocked("HOUSE_COLLECTOR"));REQUIRE(reloaded.progress(*loadedAward)==12);
    for(auto house:{"Wildspade","Kleshmersh","Tharpique","Corruptique"})REQUIRE(reloaded.statistic(std::string("Victories")+house)==1);
}
TEST_CASE_METHOD(Fixture,"House Collector keeps an award already earned under the older eight-house rule","[achievements]"){
    const auto legacy=dir/"legacy.ini";
    {std::ofstream f(legacy);f<<"[Profile]\nVersion=1\n[Unlocked]\nHOUSE_COLLECTOR=123456789\n[Statistics]\nVictoriesAtreides=2\n";}
    AchievementManager reloaded;REQUIRE(reloaded.configure("",legacy.u8string()));
    REQUIRE(reloaded.unlocked("HOUSE_COLLECTOR"));REQUIRE(reloaded.statistic("VictoriesAtreides")==2);
    info.house="Kleshmersh";reloaded.begin(info);reloaded.finish(true,false);REQUIRE(reloaded.save());
    const auto text=[&]{std::ifstream f(legacy);return std::string(std::istreambuf_iterator<char>(f),{});}();
    REQUIRE(text.find("HOUSE_COLLECTOR=123456789")!=std::string::npos);
}


TEST_CASE_METHOD(Fixture,"Missile barrage counts one match and resumes its saved progress","[achievements]") {
    manager.missile();manager.missile();manager.palace();
    REQUIRE_FALSE(manager.unlocked("MISSILE_BARRAGE"));
    manager.checkpoint("two-missiles");
    AchievementManager resumed;REQUIRE(resumed.configure("",(dir/"achievements.ini").u8string()));
    resumed.begin(info,"two-missiles",true);resumed.missile();
    REQUIRE(resumed.unlocked("MISSILE_BARRAGE"));
    REQUIRE(resumed.statistic("PalaceMissilesLaunched")==3);
}
TEST_CASE_METHOD(Fixture,"Missiles across separate matches cannot unlock Missile Barrage","[achievements]") {
    manager.missile();manager.missile();manager.begin(info);manager.missile();
    REQUIRE(manager.statistic("PalaceMissilesLaunched")==3);
    REQUIRE_FALSE(manager.unlocked("MISSILE_BARRAGE"));
    manager.missile();REQUIRE_FALSE(manager.unlocked("MISSILE_BARRAGE"));
    manager.missile();REQUIRE(manager.unlocked("MISSILE_BARRAGE"));
}
TEST_CASE_METHOD(Fixture,"Ineligible and completed matches cannot grant Missile Barrage","[achievements]") {
    info.enabled=false;manager.begin(info);manager.missile();manager.missile();manager.missile();
    REQUIRE_FALSE(manager.unlocked("MISSILE_BARRAGE"));
    info.enabled=true;manager.begin(info);manager.missile();manager.missile();manager.finish(false,false);manager.missile();
    REQUIRE_FALSE(manager.unlocked("MISSILE_BARRAGE"));
}

TEST_CASE_METHOD(Fixture,"Blue and purple harvest awards distinguish each spice type","[achievements]") {
    manager.harvestedType(1);manager.harvestedType(2);manager.harvestedType(4);
    REQUIRE_FALSE(manager.unlocked("BLUE_HARVEST"));REQUIRE_FALSE(manager.unlocked("PURPLE_HARVEST"));
    manager.harvestedType(16);REQUIRE(manager.unlocked("BLUE_HARVEST"));REQUIRE_FALSE(manager.unlocked("PURPLE_HARVEST"));
    manager.checkpoint("blue-harvest");
    AchievementManager resumed;REQUIRE(resumed.configure("",(dir/"achievements.ini").u8string()));
    resumed.begin(info,"blue-harvest",true);REQUIRE(resumed.unlocked("BLUE_HARVEST"));
    resumed.harvestedType(8);REQUIRE(resumed.unlocked("PURPLE_HARVEST"));
}

TEST_CASE_METHOD(Fixture,"Chaos Conqueror requires an eligible campaign finale victory","[achievements]") {
    info.mode=Mode::Campaign;info.mission=22;info.chaosCampaignEligible=true;
    bool victory=true;
    bool expected=true;
    SECTION("campaign finale") {}
    SECTION("earlier campaign mission") {info.mission=21;expected=false;}
    SECTION("campaign started without Chaos Mode") {info.chaosCampaignEligible=false;expected=false;}
    SECTION("skirmish") {info.mode=Mode::Skirmish;expected=false;}
    SECTION("custom game") {info.mode=Mode::Custom;expected=false;}
    SECTION("multiplayer") {info.mode=Mode::Multiplayer;expected=false;}
    SECTION("defeat") {victory=false;expected=false;}
    SECTION("disabled achievements") {info.enabled=false;expected=false;}
    manager.begin(info);manager.finish(victory,false);
    REQUIRE(manager.unlocked("CHAOS_CONQUEROR")==expected);
}

TEST_CASE_METHOD(Fixture,"Chaos campaign checkpoints preserve eligibility and cannot add it retroactively","[achievements]") {
    info.mode=Mode::Campaign;info.mission=22;
    bool savedEligible=true;
    bool resumedEligible=true;
    bool expected=true;
    SECTION("eligible checkpoint resumes") {}
    SECTION("checkpoint from a normal campaign stays ineligible") {savedEligible=false;expected=false;}
    SECTION("current session disables Chaos eligibility") {resumedEligible=false;expected=false;}
    info.chaosCampaignEligible=savedEligible;
    manager.begin(info);manager.checkpoint("chaos-finale");
    info.chaosCampaignEligible=resumedEligible;
    AchievementManager resumed;REQUIRE(resumed.configure("",(dir/"achievements.ini").u8string()));
    resumed.begin(info,"chaos-finale",true);resumed.finish(true,false);
    REQUIRE(resumed.unlocked("CHAOS_CONQUEROR")==expected);
}

TEST_CASE_METHOD(Fixture,"Missile awards distinguish a missed strike and a single demolition strike","[achievements]") {
    manager.missileResult(true, 2);
    REQUIRE_FALSE(manager.unlocked("DEMOLITION_STRIKE"));
    REQUIRE_FALSE(manager.unlocked("FUEL_WASTE"));
    manager.missileResult(true, 1);
    REQUIRE_FALSE(manager.unlocked("DEMOLITION_STRIKE")); // Different strikes cannot combine.
    manager.missileResult(false, 0);
    REQUIRE(manager.unlocked("FUEL_WASTE"));
    manager.missileResult(true, 3);
    REQUIRE(manager.unlocked("DEMOLITION_STRIKE"));
    manager.checkpoint("missile-result");
    manager.begin(info,"missile-result",true);
    REQUIRE(manager.statistic("BestMissileStructures") == 3);
    manager.finish(false,true); manager.missileResult(false,0);
    REQUIRE(manager.statistic("EmptyMissiles") == 1);
}
TEST_CASE_METHOD(Fixture,"Cooperative completion requires the complete shared campaign and counts once","[achievements]") {
    info.mode=Mode::Multiplayer; info.coopSession="common-123";
    manager.begin(info); manager.finish(true,false);
    REQUIRE_FALSE(manager.unlocked("COOP_CONQUEROR"));
    info.coopCompletionEligible=true; manager.begin(info); manager.finish(false,true);
    REQUIRE_FALSE(manager.unlocked("COOP_CONQUEROR"));
    manager.begin(info); manager.checkpoint("final-coop"); manager.finish(true,false);
    REQUIRE(manager.unlocked("COOP_CONQUEROR"));
    REQUIRE(manager.statistic("CoopCampaignsCompleted")==1);
    manager.begin(info,"final-coop",true); manager.finish(true,false);
    REQUIRE(manager.statistic("CoopCampaignsCompleted")==1);
    info.coopSession="common-456"; manager.begin(info); manager.finish(true,false);
    REQUIRE(manager.statistic("CoopCampaignsCompleted")==2);
}
TEST_CASE_METHOD(Fixture,"Mission score tiers and score total cannot be farmed by reloading","[achievements]") {
    info.mode=Mode::Campaign; info.mission=8; manager.begin(info); manager.checkpoint("score");
    manager.finish(true,false); manager.campaignScore(1500);
    REQUIRE(manager.unlocked("ELITE_SCORE")); REQUIRE_FALSE(manager.unlocked("LEGENDARY_SCORE"));
    REQUIRE(manager.statistic("CampaignScoreTotal")==1500);
    manager.begin(info,"score",true); manager.finish(true,false); manager.campaignScore(1500);
    REQUIRE(manager.statistic("CampaignScoreTotal")==1500);
    manager.campaignScore(2000); REQUIRE(manager.unlocked("LEGENDARY_SCORE"));
    REQUIRE(manager.statistic("CampaignScoreTotal")==2000);
    manager.begin(info); manager.finish(false,true); manager.campaignScore(9999);
    REQUIRE(manager.statistic("CampaignScoreTotal")==2000);
    manager.begin(info); manager.finish(true,false); manager.campaignScore(8000);
    REQUIRE(manager.unlocked("SCORE_COLLECTOR"));
}
TEST_CASE_METHOD(Fixture,"New cumulative thresholds reuse stored counters and retain existing awards","[achievements]") {
    for(unsigned i=1;i<=1000;++i) manager.enemyDestroyed(i,false,false,false);
    for(unsigned i=1001;i<=1250;++i) manager.enemyDestroyed(i,true,false,false);
    REQUIRE(manager.unlocked("ENEMY_ARMY")); REQUIRE(manager.unlocked("BASE_BREAKER"));
    for(unsigned i=0;i<50;++i)manager.captured();
    REQUIRE(manager.unlocked("INFILTRATION_EXPERT")); REQUIRE(manager.unlocked("INFILTRATION"));
    for(unsigned i=0;i<25;++i){manager.bloom();manager.missile();}
    REQUIRE(manager.unlocked("BLOOM_EXPLORER")); REQUIRE(manager.unlocked("MISSILE_COMMAND"));
    manager.checkpoint("new-counters"); manager.begin(info,"new-counters",true);
    REQUIRE(manager.statistic("EnemyUnitsDestroyed")==1000);
}

TEST_CASE_METHOD(Fixture,"Completion requires every current achievement and survives reload","[achievements][completion]") {
    REQUIRE_FALSE(AchievementManager{}.completed());
    REQUIRE_FALSE(manager.completed());
    const auto definitions=manager.definitions();
    for(std::size_t i=0;i+1<definitions.size();++i)manager.unlock(definitions[i].id);
    REQUIRE_FALSE(manager.completed());
    manager.unlock(definitions.back().id);REQUIRE(manager.completed());REQUIRE(manager.save());
    AchievementManager restored;REQUIRE(restored.configure("",(dir/"achievements.ini").u8string()));REQUIRE(restored.completed());
}

TEST_CASE_METHOD(Fixture,"Building sales unlock once and retain replay-safe lifetime proceeds","[achievements][selling]") {
    manager.sold(250);
    REQUIRE(manager.unlocked("SELL_FIRST_BUILDING"));
    REQUIRE(manager.statistic("StructuresSold")==1);
    REQUIRE(manager.statistic("CreditsFromSales")==250);
    manager.checkpoint("before-second-sale");
    manager.sold(400);manager.save();
    AchievementManager resumed;
    REQUIRE(resumed.configure("",(dir/"achievements.ini").u8string()));
    resumed.begin(info,"before-second-sale",true);resumed.sold(400);
    REQUIRE(resumed.statistic("StructuresSold")==2);
    REQUIRE(resumed.statistic("CreditsFromSales")==650);
    REQUIRE(resumed.unlocked("SELL_FIRST_BUILDING"));
    info.enabled=false;resumed.begin(info);resumed.sold(1000);
    REQUIRE(resumed.statistic("StructuresSold")==2);
}

TEST_CASE_METHOD(Fixture,"ArcaKev campaign awards separate full and Lite progress and survive reload","[achievements]") {
    info.mode=Mode::Campaign;info.mission=22;info.mod="Jericho";
    const char* houses[]={"Harkonnen","Atreides","Ordos","Fremen","Sardaukar","Mercenary","Kleshmersh","Wildspade","Rebels","Neutral","Tharpique","Corruptique"};
    for(auto house:houses){info.house=house;manager.begin(info);manager.finish(true,false);}
    REQUIRE_FALSE(manager.unlocked("ARCAKEV_MASTER"));
    info.mod="ArcaKev";
    for(int i=0;i<11;++i){info.house=houses[i];manager.begin(info);manager.finish(true,false);}
    REQUIRE_FALSE(manager.unlocked("ARCAKEV_MASTER"));
    info.house=houses[11];manager.begin(info);manager.finish(true,false);REQUIRE(manager.unlocked("ARCAKEV_MASTER"));
    REQUIRE_FALSE(manager.unlocked("ARCAKEV_LITE_MASTER"));info.mod="ArcaKevLite";
    for(int i=0;i<6;++i){info.house=houses[i];manager.begin(info);manager.finish(true,false);}
    REQUIRE(manager.unlocked("ARCAKEV_LITE_MASTER"));REQUIRE(manager.save());
    AchievementManager loaded;REQUIRE(loaded.configure("",(dir/"achievements.ini").u8string()));
    REQUIRE(loaded.unlocked("ARCAKEV_MASTER"));REQUIRE(loaded.unlocked("ARCAKEV_LITE_MASTER"));
}

TEST_CASE_METHOD(Fixture,"Co-op rescue awards require team victory and the right survival outcome","[achievements]") {
    info.cooperative=true;manager.begin(info);
    SECTION("eliminated local player rescued by a surviving ally") {
        manager.finish(true,false,true,false,true);
        REQUIRE(manager.unlocked("COOP_RESCUED"));REQUIRE_FALSE(manager.unlocked("COOP_RESCUER"));
        manager.finish(true,false,true,false,true);REQUIRE(manager.statistic("CoopVictoriesRescued")==1);
    }
    SECTION("survivor wins for at least one defeated ally in larger teams") {
        manager.finish(true,false,false,true,true);
        REQUIRE(manager.unlocked("COOP_RESCUER"));REQUIRE_FALSE(manager.unlocked("COOP_RESCUED"));
    }
    SECTION("loss grants neither") {
        manager.finish(false,false,true,true,true);
        REQUIRE_FALSE(manager.unlocked("COOP_RESCUER"));REQUIRE_FALSE(manager.unlocked("COOP_RESCUED"));
    }
    SECTION("no eliminated ally grants neither") {
        manager.finish(true,false,false,false,true);
        REQUIRE_FALSE(manager.unlocked("COOP_RESCUER"));REQUIRE_FALSE(manager.unlocked("COOP_RESCUED"));
    }
    SECTION("no surviving ally cannot rescue local player") {
        manager.finish(true,false,true,true,false);REQUIRE_FALSE(manager.unlocked("COOP_RESCUED"));
    }
    SECTION("ordinary allied matches are not co-op campaigns") {
        info.cooperative=false;manager.begin(info);manager.finish(true,false,true,true,true);
        REQUIRE_FALSE(manager.unlocked("COOP_RESCUER"));REQUIRE_FALSE(manager.unlocked("COOP_RESCUED"));
    }
}

TEST_CASE_METHOD(Fixture,"Palace sabotage is tracked and disabled matches are excluded","[achievements]") {
    manager.palaceSabotaged();REQUIRE(manager.unlocked("PALACE_SABOTEUR"));
    REQUIRE(manager.statistic("PalacesSabotaged")==1);
    manager.finish(true,false);manager.palaceSabotaged();REQUIRE(manager.statistic("PalacesSabotaged")==1);
    info.enabled=false;manager.begin(info);manager.palaceSabotaged();REQUIRE(manager.statistic("PalacesSabotaged")==1);
}

TEST_CASE("Installed achievement catalog matches all built-in definitions","[achievements][catalog]") {
    const char* configuredRoot=std::getenv("DUNE_CITY_SOURCE_DIR");
    const auto source=configuredRoot?std::filesystem::u8path(configuredRoot)
        :std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream catalog(source/"config"/"Achievements.ini",std::ios::binary);
    REQUIRE(catalog.is_open());
    std::string text(std::istreambuf_iterator<char>(catalog),{});
    text.erase(std::remove(text.begin(),text.end(),'\r'),text.end());
    REQUIRE(text==defaultCatalog);
}
