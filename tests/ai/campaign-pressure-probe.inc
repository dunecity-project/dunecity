// Real engine fixture; only compiled by the diagnostic driver.
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <Menu/HouseChoiceMenu.h>
#include <Trigger/ReinforcementTrigger.h>
#include <units/Carryall.h>

void runCampaignPressureProbe(Game& game) {
    auto require=[](bool ok,const char* reason){if(!ok){SDL_Log("PRESSURE_FAIL: %s",reason);throw std::runtime_error(reason);}};
    std::vector<QuantBot*> enemies;
    QuantBot* partner=nullptr;
    for (int h=0;h<NUM_HOUSES;++h) if (auto* house=game.getHouse(h))
        for (const auto& player:house->getPlayerList()) if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
            if (h==HOUSE_HARKONNEN) partner=bot;
            else if (house->getNumUnits()) enemies.push_back(bot);
        }
    require(enemies.size()>=2,"Pressure probe needs two campaign enemy houses");
    require(partner && !partner->isCampaignEnemy(),"Human partner inherited enemy restrictions");
    // The engine ceiling follows controller difficulty, including a human's
    // shared house. Switching back restores the authored limit; workers stay capped.
    // No explicit unit-count override is selected here, which is what keeps Hard and
    // Brutal unlimited; explicit override cases have their own registered fixture.
    const auto savedUnitOverride=game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride;
    game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride=-1;
    for (auto* bot : {partner,enemies[0]}) {
        auto* house=const_cast<House*>(bot->getHouse());
        const auto savedDifficulty=bot->difficulty;
        const int savedLimit=house->maxUnits;
        const int savedWorkers=house->numItem[Unit_Harvester];
        house->maxUnits=1;
        house->numItem[Unit_Harvester]=house->getMaxHarvesters();
        for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                       QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal,QuantBot::Difficulty::Defend}) {
            bot->difficulty=tier;
            const bool unlimited=tier==QuantBot::Difficulty::Hard || tier==QuantBot::Difficulty::Brutal;
            require(house->getMaxUnits()==(unlimited ? 0 : 1),"Wrong effective engine unit ceiling");
            require(house->isGroundUnitLimitReached()!=unlimited,"Ground limit did not follow controller");
            if(unlimited) require(!house->isInfantryUnitLimitReached() && !house->isAirUnitLimitReached(),
                "Infantry/air retained a count limit");
            house->numItem[Unit_Harvester]=house->getMaxHarvesters();
            require(house->isHarvesterLimitReached(),"Removing unit ceiling removed the worker ceiling");
            if(tier==QuantBot::Difficulty::Brutal) {
                if(bot==partner) require(bot->harvesterCountCeiling()==0,"Shared helper inherited enemy worker cap");
                else require(house->getMaxHarvesters()<=7,"Brutal enemy engine worker cap exceeds seven");
            }
        }
        house->maxUnits=savedLimit;house->numItem[Unit_Harvester]=savedWorkers;bot->difficulty=savedDifficulty;
    }
    game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride=savedUnitOverride;
    // Earliest offensive trigger, excluding earlier home and worker deliveries.
    auto savedTriggers=std::move(game.getTriggerManager().triggers);
    game.getTriggerManager().addTrigger(std::make_unique<ReinforcementTrigger>(enemies[0]->getHouse()->getHouseID(),Unit_Tank,Drop_Homebase,false,100));
    game.getTriggerManager().addTrigger(std::make_unique<ReinforcementTrigger>(enemies[0]->getHouse()->getHouseID(),Unit_Harvester,Drop_Enemybase,false,200));
    game.getTriggerManager().addTrigger(std::make_unique<ReinforcementTrigger>(enemies[0]->getHouse()->getHouseID(),Unit_Tank,Drop_Enemybase,false,30000));
    for(auto* bot:enemies) for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
        bot->difficulty=tier;bot->campaignWave={};bot->attackTimer=INT32_MAX;
        bot->updateCampaignWave();
        require(bot->campaignWave.opening>=30000,"AI opened before map attack trigger");
        require(bot->campaignWave.opening<=30000+MILLI2CYCLES(120000),"Opening delay exceeds two minutes");
        if(tier==QuantBot::Difficulty::Brutal) require(bot->campaignWave.opening==30000,"Brutal delayed opening");
    }
    game.getTriggerManager().triggers.clear();
    enemies[0]->difficulty=QuantBot::Difficulty::Brutal;enemies[0]->campaignWave={};
    enemies[0]->updateCampaignWave();
    require(enemies[0]->campaignWave.opening==UINT32_MAX && !enemies[0]->campaignCanLaunch(),"Missing trigger allowed immediate Brutal attack");
    const auto savedMission=game.gameInitSettings.mission;
    const auto savedMod=game.gameInitSettings.modName;
    const auto savedHouse=game.gameInitSettings.houseID;
    for (const auto* mod:{"vanilla","dunecity"}) for(auto house:{HOUSE_ATREIDES,HOUSE_HARKONNEN,HOUSE_ORDOS})
        for(int mission=1;mission<=4;++mission) for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            game.gameInitSettings.mission=mission;game.gameInitSettings.modName=mod;game.gameInitSettings.houseID=house;
            auto* bot=enemies[0];bot->difficulty=tier;bot->campaignWave={};bot->updateCampaignWave();
            require(bot->campaignWave.opening>=MILLI2CYCLES(4*60000)
                && bot->campaignWave.opening<=MILLI2CYCLES(6*60000),"Early map opening outside minutes 4–6");
            if(tier==QuantBot::Difficulty::Brutal)
                require(bot->campaignWave.opening==MILLI2CYCLES(4*60000),"Early Brutal missed start signal");
        }
    game.gameInitSettings.mission=savedMission;game.gameInitSettings.modName=savedMod;game.gameInitSettings.houseID=savedHouse;
    game.getTriggerManager().triggers=std::move(savedTriggers);
    game.gameCycleCount=1000;
    game.techLevel=3; // No repair-yard prerequisite in the controlled wave fixture.
    auto reset=[&](QuantBot::Difficulty difficulty) {
        for (auto* bot:enemies) {
            bot->difficulty=difficulty;bot->gameMode=QuantBot::GameMode::Campaign;
            bot->militaryValueLimit=0;bot->attackTimer=0;
            bot->campaignWave={};bot->campaignWave.initialized=true;bot->scriptedAssaults.clear();
            for (auto* unit:unitList) if (unit->getOwner()==bot->getHouse()) {
                unit->setHealth(unit->getMaxHealth()); bot->doSetAttackMode(unit,GUARD);
            }
        }
    };
    for (auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
        reset(tier);
        enemies[0]->launchGroundHunt();
        require(!enemies[0]->campaignWave.members.empty(),"First enemy failed to launch fixture wave");
        const auto first=enemies[0]->campaignWave.members;
        enemies[0]->militaryValueLimit=1000000;
        enemies[0]->launchGroundHunt();
        require(enemies[0]->campaignWave.members==first,"Away attackers bypassed fresh-wave readiness");
        enemies[1]->launchGroundHunt();
        require(!enemies[1]->campaignWave.members.empty(),"Another house incorrectly blocked independent attack");
        const auto profile=enemies[0]->campaignProfile();const auto used=enemies[0]->campaignPressure();
        require(!profile.limitedWave || (used.units<=profile.units && used.value<=profile.value),"Alliance exceeded count/value cap");
        require(used.houses<=profile.houses,"Alliance exceeded house cap");
        SDL_Log("PRESSURE_TIER_PASS: tier=%d houses=%d units=%d value=%d",int(tier),used.houses,used.units,used.value);
    }
    // Large real armies must not be cut back to the former alliance caps.
    // Each house gets 40 tanks: Hard observes its credit cap, Brutal sends all 40.
    std::vector<UnitBase*> assaultArmy;
    for (auto* bot:enemies) {
        int spawned=0;
        for(int y=1;y<currentGameMap->getSizeY()-1 && spawned<40;++y)
            for(int x=1;x<currentGameMap->getSizeX()-1 && spawned<40;++x)
                if(!currentGameMap->getTile(x,y)->hasAGroundObject() && !currentGameMap->getTile(x,y)->isMountain()) {
                    auto* unit=const_cast<House*>(bot->getHouse())->createUnit(Unit_Tank);
                    require(unit,"Assault tank creation failed");
                    unit->deploy(Coord(x,y));assaultArmy.push_back(unit);++spawned;
                }
        require(spawned==40,"Insufficient space for assault fixture");
    }
    const auto allUnits=unitList;
    unitList.clear();for(auto* unit:assaultArmy)unitList.push_back(unit);
    try {
        game.techLevel=9;
        for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard}) {
            reset(tier);
            auto* bot=enemies[0];
            const int pct=int(getQuantBotConfig().getSettings(int(tier)).attackThresholdPercent*100);
            const int price=game.objectData.data[Unit_Tank][bot->getHouse()->getHouseID()].price;
            bot->militaryValueLimit=20*price*100/pct;
            std::vector<UnitBase*> recovering;
            for(auto* unit:assaultArmy) if(unit->getOwner()==bot->getHouse() && recovering.size()<5) {
                unit->doSetAttackMode(RETREAT);recovering.push_back(unit);
            }
            bot->attack(0);
            require(bot->campaignWave.members.empty(),"Aggregate readiness was bypassed");
            bot->attack(40*price);
            const auto initial=bot->campaignWave.members;
            const size_t expected=tier==QuantBot::Difficulty::Easy ? 5 : bot->campaignProfile().value/price;
            require(initial.size()==expected,"First wave missed its configured size");
            for(int attempt=0;attempt<10;++attempt)bot->attack(40*price);
            require(bot->campaignWave.members==initial,"Away or injured troops satisfied fresh-wave readiness");
            // Ready replacements wait for the dispatch countdown, not for
            // the current attackers to die or return.
            const auto cycle=game.getGameCycleCount();
            for(auto* unit:recovering) unit->doSetAttackMode(GUARD);
            const int pause=bot->attackTimer;
            const int minimum=60000;
            require(pause>=MILLI2CYCLES(minimum) && pause<=MILLI2CYCLES(minimum+120000),
                "Dispatch countdown is outside its difficulty range");
            for(int attempt=0;attempt<10;++attempt)bot->attack(40*price);
            require(bot->campaignWave.members==initial,"New wave launched before countdown expired");
            bot->updateCampaignWave();
            require(bot->attackTimer==pause,"Wave maintenance restarted the countdown");
            game.gameCycleCount+=pause;bot->attackTimer=1;
            bot->attack(40*price);
            require(bot->campaignWave.members==initial,"Wave launched one tick before countdown expired");
            bot->attackTimer=0;bot->attack(40*price);
            require(game.getGameCycleCount()>cycle && bot->campaignWave.members.size()==2*expected,
                "Surviving first wave blocked a ready second wave");
            for(auto id:initial)require(bot->campaignWave.members.count(id),"New wave discarded surviving attackers");
            const auto second=bot->campaignWave.members;
            for(int attempt=0;attempt<10;++attempt)bot->attack(40*price);
            require(bot->campaignWave.members==second,"Second dispatch did not restart its countdown");
            const int remaining=bot->attackTimer;
            for(auto id:second) {
                auto* casualty=dynamic_cast<UnitBase*>(game.getObjectManager().getObject(id));
                require(casualty,"Wave member missing");casualty->setHealth(0);
            }
            bot->updateCampaignWave();
            require(bot->campaignWave.members.empty() && bot->attackTimer==remaining,
                "Wave casualties changed the already-running countdown");
        }
        for (auto tier:{QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            reset(tier);
            for(size_t i=0;i<enemies.size();++i) {
                auto* bot=enemies[i];bot->militaryValueLimit=12000;bot->launchGroundHunt();
                const size_t expected=tier==QuantBot::Difficulty::Hard ? 3500/game.objectData.data[Unit_Tank][bot->getHouse()->getHouseID()].price : 40;
                require(bot->campaignWave.members.size()==expected,"Hard/Brutal failed army commitment or house concurrency");
                const auto members=bot->campaignWave.members;
                bot->launchGroundHunt();
                require(bot->campaignWave.members==members,"Large assault topped up on repeat dispatch");
            }
            SDL_Log("CAMPAIGN_COMMITMENT_PASS: tier=%d tanks_per_house=40 sent_per_house=%d houses=%d",
                int(tier),int(enemies[0]->campaignWave.members.size()),enemies[0]->campaignPressure().houses);
        }
        reset(QuantBot::Difficulty::Brutal);
        auto* bot=enemies[0];
        auto* manual=assaultArmy[0];auto* repairing=assaultArmy[1];auto* damaged=assaultArmy[2];
        bot->onHumanUnitOrder(manual->getObjectID());
        bot->doSetAttackMode(repairing,RETREAT);damaged->setHealth(1);
        bot->launchGroundHunt();
        require(bot->campaignWave.members.size()==37,"All-in included unavailable troops or left ready troops home");
        require(!bot->campaignWave.members.count(manual->getObjectID())
            && !bot->campaignWave.members.count(repairing->getObjectID())
            && !bot->campaignWave.members.count(damaged->getObjectID()),"All-in overrode manual orders or repair");
        damaged->setHealth(damaged->getMaxHealth());bot->doSetAttackMode(damaged,GUARD);
        bot->attack(0);
        require(bot->campaignWave.members.size()==38 && bot->campaignWave.members.count(damaged->getObjectID()),
            "Newly ready Brutal unit waited for previous attackers to disappear");
        bot->manualUnitOrders.clear();
    } catch (...) {unitList=allUnits;throw;}
    unitList=allUnits;
    game.techLevel=3;
    reset(QuantBot::Difficulty::Easy);enemies[0]->launchGroundHunt();
    auto* first=enemies[0];auto* second=enemies[1];
    // Serialize an actual QuantBot, not just the extracted policy object.
    OMemoryStream out;out.open();first->save(out);
    const auto previousVersion=game.loadedSavegameVersion;game.loadedSavegameVersion=SAVEGAMEVERSION;
    IMemoryStream input(out.getData(),out.getDataLength());
    {QuantBot restored(input,const_cast<House*>(first->getHouse()));
        require(restored.campaignWave.members==first->campaignWave.members,"Bot save lost assault membership");
        require(restored.campaignWave.launched==first->campaignWave.launched,"Bot save lost launch time");
    }
    game.loadedSavegameVersion=9838;
    IMemoryStream previousInput(out.getData(),out.getDataLength());
    {QuantBot previous(previousInput,const_cast<House*>(first->getHouse()));
        require(previous.campaignWave.initialized==first->campaignWave.initialized
            && previous.campaignWave.opening==first->campaignWave.opening,
            "Previous save lost its opening after consumed map triggers");
        require(previous.scriptedAssaults.empty(),"Previous save invented scripted attackers");
    }
    game.loadedSavegameVersion=9837;
    IMemoryStream legacyInput(out.getData(),out.getDataLength());
    {QuantBot legacy(legacyInput,const_cast<House*>(first->getHouse()));
        require(!legacy.campaignWave.initialized && legacy.campaignWave.members.empty(),"Legacy save invented wave state");
    }
    game.loadedSavegameVersion=previousVersion;
    // An unregistered HUNT defender still cannot bypass the attack scheduler.
    const UnitBase* held=nullptr;
    for (auto* unit:unitList) if(second->campaignCombatUnit(unit) && unit->isActive()) {held=unit;break;}
    require(held,"Missing reinforcement stand-in");second->doSetAttackMode(held,HUNT);second->updateCampaignWave();
    require(held->getAttackMode()!=HUNT,"Unassigned defender kept HUNT");
    // Real authored drops join the assault even while their carryall is in transit.
    const auto before=second->scriptedAssaults;
    ReinforcementTrigger drop(second->getHouse()->getHouseID(),Unit_Tank,Drop_Enemybase,false,game.getGameCycleCount());
    drop.trigger();
    require(second->scriptedAssaults.size()==before.size()+1,"Scripted attack cargo not registered");
    const UnitBase* cargo=nullptr;
    for(auto id:second->scriptedAssaults) if(!before.count(id))cargo=dynamic_cast<const UnitBase*>(second->getObject(id));
    require(cargo && !cargo->isActive(),"Fixture needs actual carried reinforcement");
    second->updateCampaignWave();
    require(second->scriptedAssaults.count(cargo->getObjectID()),"In-flight attacker lost membership");
    // Save/load retains scripted intent, not merely ordinary wave membership.
    game.loadedSavegameVersion=SAVEGAMEVERSION;
    OMemoryStream scriptedOut;scriptedOut.open();second->save(scriptedOut);
    IMemoryStream scriptedIn(scriptedOut.getData(),scriptedOut.getDataLength());
    {QuantBot restored(scriptedIn,const_cast<House*>(second->getHouse()));
        require(restored.scriptedAssaults.count(cargo->getObjectID()),"Save lost scripted cargo intent");}
    game.loadedSavegameVersion=previousVersion;
    Carryall* transport=nullptr;
    for(auto* unit:unitList) if(auto* carrier=dynamic_cast<Carryall*>(unit))
        for(auto id:carrier->pickedUpUnitList)if(id==cargo->getObjectID())transport=carrier;
    require(transport,"Scripted cargo missing transport");
    transport->deployUnit(cargo->getObjectID());
    require(cargo->isActive(),"Actual carryall failed deployment");
    second->doMove2Pos(cargo,cargo->getX(),cargo->getY(),false);
    second->campaignControlsUnit(cargo);
    require(cargo->hasATarget() || cargo->isMoving() || cargo->getAttackMode()==HUNT,"Landed attack received no offensive order");
    game.gameCycleCount+=MILLI2CYCLES(second->campaignProfile().sortieMs)+1;
    // A real attack target must survive the former unconditional sortie recall.
    const StructureBase* target=nullptr;
    for(auto* building:structureList)if(building->getOwner()->getTeamID()!=second->getHouse()->getTeamID()){target=building;break;}
    require(target,"Missing enemy objective");second->doAttackObject(cargo,target,true);
    second->updateCampaignWave();
    require(second->scriptedAssaults.count(cargo->getObjectID()),"Fighting scripted attacker recalled by timer");
    // Home/economic deliveries never create an offensive wave.
    const auto scriptedCount=second->scriptedAssaults.size();
    ReinforcementTrigger home(second->getHouse()->getHouseID(),Unit_Tank,Drop_Homebase,false,game.getGameCycleCount());home.trigger();
    ReinforcementTrigger worker(second->getHouse()->getHouseID(),Unit_Harvester,Drop_Enemybase,false,game.getGameCycleCount());worker.trigger();
    require(second->scriptedAssaults.size()==scriptedCount,"Home or economic delivery became attacker");
    second->attackTimer=0;
    require(second->campaignCanLaunch(),"Scripted cargo blocks the house's own scheduled attack");
    // Saved repeat cooldowns are ignored; only each house's opening gates it.
    first->campaignWave.members.clear();second->campaignWave.members.clear();
    first->attackTimer=MILLI2CYCLES(240000);second->attackTimer=0;
    require(!first->campaignCanLaunch(),"Easy ignored its repeat cooldown");
    require(second->campaignCanLaunch(),"Other house inherited attack countdown");
    first->campaignWave.opening=game.getGameCycleCount()+MILLI2CYCLES(60000);
    first->attackTimer=0;
    require(!first->campaignCanLaunch(),"Budget bypassed opening timer");
    first->campaignWave.opening=game.getGameCycleCount();
    first->attackTimer=MILLI2CYCLES(240000);first->updateCampaignWave();
    require(first->attackTimer==MILLI2CYCLES(240000),"Saved repeat cooldown was discarded after opening");
    // Enemy helper-role changes are read from controllers, not legacy isAI flags.
    require(!partner->isCampaignEnemy(),"Shared human house was restricted");
    for (auto* bot : {first,second,partner}) {
        bot->difficulty=bot==second ? QuantBot::Difficulty::Medium : QuantBot::Difficulty::Easy;
        auto* house=const_cast<House*>(bot->getHouse());
        BuilderBase* yard=nullptr;
        for (auto* building : structureList)
            if (building->getOwner()==house && building->getItemID()==Structure_ConstructionYard)
                {yard=dynamic_cast<BuilderBase*>(building);break;}
        require(yard,"Power fixture needs construction yard");
        const int oldPower=house->powerRequirement;
        house->powerRequirement=house->getProducedPower()+300;
        house->addCredits(10000);bot->lastCalculatedSpice=100000;
        auto structures=structureList; structureList.clear();structureList.push_back(yard);
        try {bot->build(0);}catch(...){structureList=structures;throw;}
        structureList=structures;
        require(yard->getCurrentProducedItem()==Structure_WindTrap,"Easy/Medium did not queue power deficit Windtrap");
        house->powerRequirement=oldPower;
    }
    HouseChoiceMenu menu;
    menu.setCurrentPosition(0,0,640,480);
    const auto size=menu.getSize();
    sdl2::texture_ptr imageTarget(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,size.x,size.y));
    require(imageTarget && SDL_SetRenderTarget(renderer,imageTarget.get())==0,"Menu render target failed");
    SDL_RenderSetLogicalSize(renderer,size.x,size.y);
    for (int tier=0;tier<4;++tier) {
        menu.supportBotDropDown.setSelectedItem(5+tier);menu.onSupportBotSelectionChanged(false);
        menu.enemyAIDropDown.setSelectedItem(tier);menu.onEnemyAISelectionChanged(false);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        menu.draw();auto screenshot=renderReadSurface(renderer);
        const std::string filename="campaign-difficulty-menu-"+std::to_string(tier)+".png";
        require(SavePNG(screenshot.get(),filename.c_str())==0,"Menu screenshot failed");
    }
    SDL_SetRenderTarget(renderer,nullptr);
    SDL_Log("CAMPAIGN_PRESSURE_PROBE_PASS: opening gates, fresh-wave readiness, Easy/Medium dispatch countdown with surviving attackers, reserve protection, saved cooldown, scripted cargo and sustained combat");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=pressure outcome=passed");
}
