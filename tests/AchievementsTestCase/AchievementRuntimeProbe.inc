// Adapted from Tornie upstream tests/AchievementRuntimeSmoke.h.
// Included only by the isolated generated diagnostic main, never by the shipped game.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Achievements/AchievementEvents.h>
#include <Achievements/AchievementManager.h>
#include <GUI/dune/AchievementsWindow.h>
#include <misc/OMemoryStream.h>
#include <misc/FileSystem.h>
#include <misc/format.h>
#include <units/InfantryBase.h>
#include <units/Harvester.h>
#include <units/SandWorm.h>
#include <filesystem>

void runAchievementRuntimeProbe() {
    const auto require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(std::string("Achievement event regression: ")+message);
    };
    const char* profile=std::getenv("DUNECITY_USERDIR");
    require(profile && *profile,"isolated local profile is required");
    const auto save=(std::filesystem::u8path(profile)/"achievement-runtime.sav").u8string();
    auto options=settings.gameOptions;
    options.immortalHumanPlayer=false;
    options.sandwormsRespawn=true;
    std::string map="[BASIC]\nVersion=2\nTechLevel=9\n[MAP]\nSizeX=40\nSizeY=40\n";
    for(int y=0;y<40;++y) map+=fmt::sprintf("%03d=",y)+std::string(40,'%')+"\n";
    map+="[Player1]\nCredits=100000\n[Player2]\nCredits=100000\n[Player3]\nCredits=100000\n";
    GameInitSettings init("achievement-runtime",map,false,options);
    for(auto house:{HOUSE_ATREIDES,HOUSE_HARKONNEN,HOUSE_ORDOS}) {
        GameInitSettings::HouseInfo row(house,house==HOUSE_HARKONNEN?2:1);
        row.colorOfHouse=getDefaultHouseColorSlot(house==HOUSE_ATREIDES?HOUSE_HARKONNEN:house);
        row.addPlayerInfo({house==HOUSE_ATREIDES?settings.general.playerName:"Achievement AI",
                          house==HOUSE_ATREIDES?"HumanPlayer":DEFAULTAIPLAYERCLASS});
        init.addHouseInfo(row);
    }
    auto& manager=achievements::AchievementManager::instance();
    auto game=std::make_unique<Game>();
    currentGame=game.get();
    game->bReplay=true; // Suppress interactive briefing/public analytics, as the other probes do.
    game->initGame(init);
    game->bReplay=false;
    game->winFlags=game->loseFlags=0;
    auto* local=game->getHouse(HOUSE_ATREIDES);
    auto* enemy=game->getHouse(HOUSE_HARKONNEN);
    auto* ally=game->getHouse(HOUSE_ORDOS);
    require(local && enemy && ally && pLocalHouse==local,"fixture houses and local player");
    auto* vehicle=local->createUnit(Unit_Tank);vehicle->deploy(Coord(2,22));
    const auto vehicleID=vehicle->getObjectID();
    auto* victim=enemy->createUnit(Unit_Tank);victim->deploy(Coord(8,22));
    auto* friendly=ally->createUnit(Unit_Tank);friendly->deploy(Coord(12,22));
    AchievementEvents::begin(*game,"",false,true);

    const auto serialized=[&]() {
        OMemoryStream bytes;bytes.open();game->saveGame(bytes);
        return std::string(bytes.getData(),bytes.getDataLength());
    };
    const auto observeWithoutSimulation=[&]() {
        AchievementEvents::damage(victim,vehicle->getObjectID(),local,false);
        AchievementEvents::built(local,vehicle);
        AchievementEvents::captured(local,enemy);
        AchievementEvents::refined(local,12000);
        AchievementEvents::spice(local,currentGameMap->getTile(2,22));
        AchievementEvents::bloom(local);
        AchievementEvents::palace(local);
        AchievementEvents::missile(local);
        AchievementEvents::beginMissileImpact(local,vehicle->getObjectID());
        AchievementEvents::endMissileImpact();
        AchievementEvents::sold(local,250);
        AchievementEvents::pump(*game);
    };
    if(!ModManager::instance().isTornieFamilyContentActive()) {
        const auto before=serialized();
        require(!manager.isActive(),"non-family game must not collect achievements");
        observeWithoutSimulation();AchievementEvents::finish(true);
        require(manager.statistic("GamesWon")==0 && manager.statistic("StructuresCaptured")==0,
                "non-family observation must not add statistics");
        require(serialized()==before,"non-family observation must preserve all save bytes and RNG state");
        require(!existsFile(getDirname(getConfigFilepath())+"/achievements.ini"),
                "non-family game must not create an achievement profile");
    } else {
        require(manager.isActive() && manager.definitions().size()==73,"all 73 definitions loaded");
        require(manager.match().info.color!=manager.match().info.defaultColor,"resolved nonstandard house color");
        local->informWasBuilt(vehicle);local->informWasBuilt(vehicle,false);
        require(manager.statistic("UnitsBuilt")==1,"factory production excludes imported units");
        friendly->handleDamage(1,vehicle->getObjectID(),local);
        require(!manager.match().damagedEnemy,"friendly fire is not adversary damage");
        victim->handleDamage(1,vehicle->getObjectID(),local);
        require(manager.match().damagedEnemy && !manager.match().destroyedEnemy,"nonlethal damage attribution");
        victim->handleDamage(victim->getMaxHealth()+1,vehicle->getObjectID(),ally);
        require(!manager.match().destroyedEnemy,"allied kills are not local kills");

        auto* building=enemy->placeStructure(NONE_ID,Structure_WindTrap,18,3,true,true);
        require(building!=nullptr,"capture target placement");building->setHealth(1);
        auto* infantry=static_cast<InfantryBase*>(local->createUnit(Unit_Soldier));
        infantry->deploy(Coord(18,3));infantry->doCaptureStructure(building);
        infantry->justStoppedMoving=true;infantry->checkPos();
        require(manager.statistic("StructuresCaptured")==1 && manager.statistic("UnitsLost")==1,
                "real capture observes both ownership transfer and consumed infantry");
        require(manager.statistic("EnemyStructuresDestroyed")==0,"capture is not destruction");
        for(int index=0;index<5;++index) {
            auto* soldier=enemy->createUnit(Unit_Soldier);soldier->deploy(Coord(20,20));
        }
        currentGameMap->getTile(20,20)->squash(vehicle);
        require(manager.unlocked("ROADKILL") && manager.statistic("InfantryCrushed")==5,
                "actual same-vehicle crush attribution");
        auto* flame=local->createUnit(Unit_FlameTank);flame->deploy(Coord(2,24));local->informWasBuilt(flame,false);
        require(manager.statistic("UnitsBuilt")==1 && manager.match().arsenal.empty(),
                "imported Flame Tanks do not count as production or Arsenal");
        auto* flameTarget=enemy->createUnit(Unit_Tank);flameTarget->deploy(Coord(8,24));
        flameTarget->handleDamage(flameTarget->getMaxHealth()+1,flame->getObjectID(),local);
        require(manager.statistic("FlameTankKills")==1,"flame source attribution");
        auto* worm=enemy->createUnit(Unit_Sandworm);worm->deploy(Coord(26,26));
        worm->handleDamage(worm->getMaxHealth()/2+1,vehicle->getObjectID(),local);worm->update();
        require(manager.unlocked("WORM_HUNTER") && manager.statistic("SandwormsKilled")==1,
                "worm half-health retreat counts once");
        AchievementEvents::wormDefeated(worm);
        require(manager.statistic("SandwormsKilled")==1,"worm defeat deduplication");
        auto* red=currentGameMap->getTile(25,22);red->setType(Terrain_RedSpice);red->setSpice(500_fix);
        auto* harvester=static_cast<Harvester*>(local->createUnit(Unit_Harvester));
        harvester->deploy(Coord(25,22));harvester->setDestination(Coord(25,22));harvester->move();
        require(manager.unlocked("RED_HARVEST"),"actual harvester spice observation");
        local->addCredits(10000_fix,true);
        require(manager.unlocked("THE_SPICE_MUST_FLOW"),"refinery deposit observation");

        const auto before=serialized();
        observeWithoutSimulation();
        require(serialized()==before,"achievement observations must preserve every simulation/save/RNG byte");
        require(game->saveGame(save),"save with local profile checkpoint");
        const auto runID=manager.match().runID;
        game.reset();currentGame=nullptr;pLocalHouse=nullptr;pLocalPlayer=nullptr;
        game=std::make_unique<Game>();currentGame=game.get();
        require(game->loadSaveGame(save),"reload unchanged game save format");
        AchievementEvents::begin(*game,AchievementEvents::fileKey(save),true,true);
        require(manager.match().runID==runID && manager.match().historyKnown,"local checkpoint run restoration");
        game->setGameWon();
        require(manager.unlocked("TRUE_COLORS") && manager.statistic("GamesWon")==1,
                "real completed-game victory is counted once");
        achievements::AchievementManager reloaded;
        require(reloaded.configure("",getDirname(getConfigFilepath())+"/achievements.ini"),"local profile reload");
        require(reloaded.unlocked("ROADKILL") && reloaded.unlocked("TRUE_COLORS"),"persisted event awards");
        AchievementsWindow window;
        require(window.getSize().x<=getRendererWidth() && window.getSize().y<=getRendererHeight(),"menu window fits display");
        window.draw();
        const auto eligibility=manager.match().info;
        manager.begin(eligibility);
        require(manager.isActive() && !manager.match().completed,"fresh eligible run before cheat suspension");
        const auto builtBeforeSuspension=manager.statistic("UnitsBuilt");
        manager.suspend();
        AchievementEvents::built(pLocalHouse,game->getObjectManager().getObject(vehicleID));
        require(!manager.isActive() && manager.statistic("UnitsBuilt")==builtBeforeSuspension,
                "cheat suspension stays inactive");
    }
    game.reset();currentGame=nullptr;pLocalHouse=nullptr;pLocalPlayer=nullptr;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=achievements outcome=passed");
}
