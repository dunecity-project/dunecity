// Real-engine policy integration probe; never included in the shipped executable.
#include <FileClasses/FileManager.h>
#include <Trigger/TimeoutTrigger.h>
#include <Network/WorkshopGameContent.h>
#include <mod/Workshop.h>

// Fixture-only independent games: never finish or change the outcome of an ordinary
// campaign run. Outcomes and content-scope controls use native constructors and init.
void runCampaignObserverOutcomeProbe(std::unique_ptr<Game>& game, int level,
                                    const std::string& candidateClass, const std::string& enemyClass) {
    const auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("CAMPAIGN_OBSERVER_FAIL: %s",why);throw std::runtime_error(why);}
    };
    struct RestoreState {
        std::unique_ptr<Game>& game;
        std::string mod=ModManager::instance().getActiveModName();
        SettingsClass savedSettings=settings;
        SettingsClass::GameOptionsClass savedEffectiveOptions=effectiveGameOptions;
        QuantBotConfig savedConfig=getQuantBotConfig();
        bool restored=false;

        void clearWorld() {
            // Game and its objects still need the owning engine globals while dying.
            game.reset();currentGame=nullptr;pLocalPlayer=nullptr;pLocalHouse=nullptr;
        }
        bool restore() {
            clearWorld();
            auto& mods=ModManager::instance();
            bool modRestored=false;
            try {
                modRestored=!currentGameMap
                    && (mods.getActiveModName()==mod || mods.setActiveMod(mod));
            } catch(const std::exception& error) {
                SDL_Log("CITY_CAMPAIGN_FAIL: cannot restore active mod: %s",error.what());
            }
            settings=std::move(savedSettings);
            effectiveGameOptions=std::move(savedEffectiveOptions);
            getQuantBotConfig()=std::move(savedConfig);
            restored=true;
            return modRestored;
        }
        ~RestoreState() {
            if(!restored) try {
                if(!restore()) SDL_Log("CITY_CAMPAIGN_FAIL: native fixture restoration failed");
            } catch(const std::exception& error) {
                SDL_Log("CITY_CAMPAIGN_FAIL: native fixture restoration failed: %s",error.what());
            }
        }
    } state{game};
    const HOUSETYPE objectiveID=game->getGameInitSettings().getHouseID();
    const int mission=game->getGameInitSettings().getMission(),tech=game->techLevel;
    const Uint32 seed=game->getGameInitSettings().getRandomSeed();
    const auto options=game->getGameInitSettings().getGameOptions();
    const std::string filename=game->getGameInitSettings().getFilename();
    std::string filedata=game->getGameInitSettings().getFiledata();
    if(filedata.empty()) {
        auto file=pFileManager->openCampaignFile(filename);
        const auto size=SDL_RWsize(file.get());
        require(size>0,"Authored observer scenario is unavailable");
        filedata.resize(static_cast<std::size_t>(size));
        require(SDL_RWread(file.get(),filedata.data(),1,filedata.size())==filedata.size(),
            "Incomplete authored observer scenario");
    }
    const auto start=[&](GameInitSettings init,bool reloadConfig=false) {
        state.clearWorld();
        require(!currentGame && !currentGameMap,"Native fixtures share a live world");
        init.randomSeed=seed;
        init.setLocalSpectator(true);
        init.setMultiplePlayersPerHouse(false);
        for(int h=0;h<NUM_HOUSES;++h) if(isHouseAvailable(static_cast<HOUSETYPE>(h))) {
            GameInitSettings::HouseInfo row(static_cast<HOUSETYPE>(h),h==objectiveID ? 1:2);
            row.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)),h==objectiveID ? candidateClass:enemyClass});
            init.addHouseInfo(row);
        }
        // Pin before constructing Game: an authored mod can activate a ws-<hash>
        // revision here, and no previous world may survive that activation.
        WorkshopGameContent::resolveMod(init,false);
        WorkshopGameContent::pin(init);
        require(init.getModName()==ModManager::instance().getActiveModName(),
            "Native setup did not retain the actual active mod");
        if(reloadConfig) {
            // The global config is lazy, not invalidated by setActiveMod().
            // Reset missing-key defaults as well as loading the new active path.
            getQuantBotConfig()=QuantBotConfig();
            require(getQuantBotConfig().load(getQuantBotConfigFilepath()),
                "Cannot reload the native control's QuantBot configuration");
        }
        game=std::make_unique<Game>();currentGame=game.get();
        game->bReplay=true;
        game->initGame(init);
        game->bReplay=false;
        game->initializeGameLoop();
        game->frameTiming.unitPhases=false;
        require(game->isObserving() && game->getGameInitSettings().isLocalSpectator()
            && !pLocalHouse && pLocalPlayer && !pLocalPlayer->getHouse(),"Observer acquired a player house");
        for(int h=0;h<NUM_HOUSES;++h) if(const House* house=game->getHouse(h))
            for(const auto& player:house->getPlayerList())
                require(!dynamic_cast<const HumanPlayer*>(player.get()),"Observer fixture registered a human");
        const House* objective=game->getHouse(objectiveID);
        require(objective && objective->isAlive() && objective->getPlayerList().size()==1
            && dynamic_cast<const QuantBot*>(objective->getPlayerList().front().get())
            && objective->getPlayerList().front()->getPlayerclass()==candidateClass,
            "Observer fixture lost its selected QuantBot objective");
        require(!game->finished,"Observer fixture ended during initialization");
    };
    const auto timeout=[&](bool win) {
        game->winFlags=WINLOSEFLAGS_TIMEOUT;
        game->loseFlags=win ? WINLOSEFLAGS_TIMEOUT:0;
        game->getTriggerManager().triggers.clear();
        const Uint32 triggerCycle=game->getGameCycleCount()+1;
        game->getTriggerManager().addTrigger(std::make_unique<TimeoutTrigger>(triggerCycle));
        for(int tick=0;tick<3 && !game->finished;++tick) game->updateGameState();
        require(game->finished && game->finishedLevelCycle==triggerCycle,"Native timeout did not finish at its scheduled cycle");
    };
    const char* cases[]={"quota","defeat","timeout_win","timeout_loss"};
    for(bool single:{false,true}) for(int action=0;action<4;++action) {
        GameInitSettings init=single ? GameInitSettings(objectiveID,mission,options)
            : GameInitSettings(objectiveID,options,level);
        if(!single) {
            init.mission=mission;
            init.filename=GameInitSettings::getScenarioFilename(objectiveID,mission);
        }
        start(std::move(init));
        House* objective=game->getHouse(objectiveID);
        require(game->gameType==(single ? GameType::Skirmish:GameType::Campaign)
            && game->getOutcomeHouse()==objective && !game->watchesWithoutSide(),
            "Native mission observer lost its objective-side rules");
        const auto& setup=game->getGameInitSettings();
        require(setup.getMission()==mission && setup.getFilename()==filename && setup.getRandomSeed()==seed
            && setup.getGameOptions().getHash()==options.getHash() && game->techLevel==tech,
            "Native menu mode changed the selected mission, seed, options or technology");
        if(action==0) {
            // Real earned income must reach checkQuotaWin -> House::win, not a
            // fixture call to Game::setGameWon or an observer's generic winner.
            game->winFlags=WINLOSEFLAGS_QUOTA;game->loseFlags=0;
            objective->quota=1;
            objective->capacity=std::max(objective->getCapacity(),objective->getEarnedCredits().lround()+100);
            objective->addCredits(1);
        } else if(action==1) {
            game->winFlags=WINLOSEFLAGS_HUMAN_HAS_BUILDINGS;game->loseFlags=0;
            // The native object counters call House::lose when its last asset
            // dies. Destroyed buildings can leave soldiers, hence bounded rounds.
            for(int round=0;round<64 && objective->isAlive();++round) {
                std::vector<Uint32> doomed;
                for(const auto* building:structureList)
                    if(building->getOwner()==objective) doomed.push_back(building->getObjectID());
                for(const auto* unit:unitList)
                    if(unit->getOwner()==objective || unit->originalHouseID==objectiveID)
                        doomed.push_back(unit->getObjectID());
                for(const Uint32 id:doomed)
                    if(auto* object=game->getObjectManager().getObject(id)) object->destroy();
                if(objective->isAlive()) game->updateGameState();
            }
            require(!objective->isAlive(),"Objective survived bounded native elimination");
        } else {
            timeout(action==2);
        }
        const bool won=action==0 || action==2;
        require(game->finished && game->won==won
            && game->getWatchedWinnerTeam()==(won ? objective->getTeamID():0),
            "Native mission observer reported the wrong win/loss");
        const std::string expected=fmt::sprintf(
            won ? _("House %s completed its mission"):_("House %s failed its mission"),
            getHouseNameByNumber(objectiveID));
        require(game->watchedResultText()==expected,"Native mission observer displayed a generic match result");
        SDL_Log("CAMPAIGN_OBSERVER_OUTCOME_PASS: mode=%s case=%s mission=%d player=%s won=%d",
            single ? "single":"full",cases[action],mission,candidateClass.c_str(),int(won));
    }
    for(bool winFlag:{false,true}) {
        // The same physical map through the Custom Game constructor must remain
        // side-free, regardless of the mission timeout's win/lose flag direction.
        start(GameInitSettings(filename,filedata,false,options));
        require(game->gameType==GameType::CustomGame && !game->getOutcomeHouse() && game->watchesWithoutSide(),
            "Custom observer acquired a mission objective");
        std::set<int> teams;
        for(int h=0;h<NUM_HOUSES;++h) if(const House* house=game->getHouse(h))
            if(house->isAlive() && !house->isNatives() && house->getTeamID()>0) teams.insert(house->getTeamID());
        require(teams.size()>1,"Custom timeout control needs two surviving teams");
        game->setGameWon();game->setGameLost();
        require(!game->finished,"Custom observer accepted a side's native win/loss");
        timeout(winFlag);
        require(!game->won && game->getWatchedWinnerTeam()==0
            && game->watchedResultText()==_("Time is up - no winner"),"Custom observer lost its generic timeout draw");
        SDL_Log("CAMPAIGN_OBSERVER_OUTCOME_PASS: mode=custom case=timeout lose_flag=%d won=0",int(winFlag));
    }

    // The final Custom outcome still owns a world. Destroy it before authoring
    // or activating either independent native Full campaign control.
    state.clearWorld();
    require(!currentGame && !currentGameMap,"Content controls retained an outcome world");
    auto& mods=ModManager::instance();
    const char* profile=std::getenv("DUNECITY_USERDIR");
    require(profile && *profile,"Content controls require an isolated probe profile");
    require(std::filesystem::weakly_canonical(mods.getModsBasePath())
        ==std::filesystem::weakly_canonical(std::filesystem::path(profile)/"mods"),
        "Content control mod directory escaped the isolated probe profile");
    const std::string otherMod="Campaign City Scope "+Workshop::newID();
    // The managed probe profile owns this authored input, including on failure.
    const auto draftPath=mods.getModPath(otherMod);
    require(mods.createMod(otherMod,"vanilla"),"Cannot create the non-DuneCity city control");
    auto info=mods.getModInfo(otherMod);
    require(info.baseMod=="vanilla","Other city control did not inherit Vanilla content");
    info.enablesCityMode=true;
    require(mods.writeModInfo(draftPath,info),"Cannot enable the authored city control");

    for(bool city:{false,true}) {
        state.clearWorld();
        require(!currentGame && !currentGameMap,"Content activation retained a native world");
        const std::string selectedMod=city ? otherMod:"vanilla";
        require(mods.setActiveMod(selectedMod) && mods.getActiveModName()==selectedMod,
            "Cannot activate the native content control");
        effectiveGameOptions=mods.loadEffectiveGameOptions(state.savedSettings.gameOptions);
        GameInitSettings init(objectiveID,effectiveGameOptions,level);
        init.mission=mission;
        init.filename=GameInitSettings::getScenarioFilename(objectiveID,mission);
        require(init.getModName()==selectedMod && init.getGameType()==GameType::Campaign,
            "Full campaign constructor did not capture the selected control mod");
        const auto optionsHash=init.getGameOptions().getHash();
        start(std::move(init),true);

        const auto& setup=game->getGameInitSettings();
        const auto contentBase=mods.getContentBase(mods.getActiveModName());
        require(mods.isInitialized() && setup.getModName()==mods.getActiveModName()
            && contentBase=="vanilla" && !mods.isDuneCityContentActive(),
            "Native content control resolved to DuneCity or a stale mod identity");
        require(game->gameType==GameType::Campaign && setup.getGameType()==GameType::Campaign
            && setup.getMission()==mission && setup.getRandomSeed()==seed
            && setup.getGameOptions().getHash()==optionsHash && game->techLevel==tech
            && game->getOutcomeHouse()==game->getHouse(objectiveID),
            "Content control lost native Full campaign mission, seed, options or objective");
        const auto* sim=game->getCitySimulation();
        const bool initialized=sim && sim->isInitialized();
        require(mods.isCityModeActive()==city && game->isCitySimEnabled()==city && initialized==city,
            "Content control did not initialize its real city-mode capability");
        QuantBot* enemy=nullptr;
        for(int h=0;h<NUM_HOUSES;++h) if(h!=objectiveID)
            if(const auto* house=game->getHouse(static_cast<HOUSETYPE>(h));house && house->isAlive()
               && house->getNumItems(Structure_Refinery)>0)
                for(const auto& player:house->getPlayerList())
                    if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {enemy=bot;break;}
        require(enemy && enemy->getPlayerclass()==enemyClass && !enemy->configOverride
            && enemy->isCampaignEnemy() && !enemy->switchedOff(),
            "Content control has no normally configured eligible campaign enemy");
        const bool cityEconomy=enemy->campaignCityEconomy();
        const bool harvesterAllowed=enemy->campaignCanAddHarvester();
        require(!cityEconomy,"DuneCity campaign economy leaked into a native content control");
        require(harvesterAllowed,"Native content control inherited a DuneCity worker restriction");
        SDL_Log("CITY_CAMPAIGN_CONTENT_SCOPE_PASS: case=%s mode=full mod=%s content_base=%s "
            "city_enabled=%d city_initialized=%d eligible_enemy=1 city_economy=0 harvester_allowed=1 "
            "mission=%d seed=%u options_hash=%s config_hash=%s",
            city ? "other_city":"vanilla",mods.getActiveModName().c_str(),contentBase.c_str(),
            int(game->isCitySimEnabled()),int(initialized),mission,seed,optionsHash.c_str(),
            getQuantBotConfig().getConfigHash().c_str());
    }
    require(state.restore(),"Cannot restore content, configuration and globals after native controls");
}

void runCampaignCityEconomyPositiveProbe(Game& game) {
    auto require=[](bool ok,const char* reason) {
        if(!ok) {SDL_Log("CITY_CAMPAIGN_FAIL: %s",reason);throw std::runtime_error(reason);}
    };
    require(game.isCitySimEnabled(),"Fixture requires Dune City");
    QuantBot* enemy=nullptr; QuantBot* ally=nullptr;
    std::set<Uint32> original;
    for(int h=0;h<NUM_HOUSES;++h) if(auto* owner=game.getHouse(h)) {
        for(const auto& player:owner->getPlayerList()) if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
            if(bot->isAlliedWithHuman()) ally=bot;
            else if(!enemy && owner->getNumItems(Structure_Refinery)>0) enemy=bot;
        }
    }
    require(enemy && ally,"Fixture needs economic enemy and human-side helper");
    auto* owner=const_cast<House*>(enemy->getHouse());
    for(Uint32 item=ItemID_FirstID;item<=ItemID_LastID;++item)
        if(isStructure(item) && owner->getNumItems(item)>0) original.insert(item);
    const int originalRefineries=owner->getNumItems(Structure_Refinery);
    game.gameCycleCount=123;enemy->update();ally->update();
    require(enemy->campaignCityEconomy() && !ally->campaignCityEconomy(),"Policy leaked to allied helper");
    require(enemy->campaignBaseline.refineries==originalRefineries,"Baseline lost original refinery count");
    for(auto item:{Structure_LightFactory,Structure_HeavyFactory,Structure_HighTechFactory,
                   Structure_Radar,Structure_RepairYard,Structure_IX,Structure_Palace})
        require(enemy->campaignPermitsStructure(item)==bool(original.count(item)),"RTS building permission differs from original mission");
    require(enemy->campaignPermitsStructure(Structure_ZoneResidential)
        && enemy->campaignPermitsStructure(Structure_ZoneCommercial)
        && enemy->campaignPermitsStructure(Structure_ZoneIndustrial),"RCI mix was restricted");
    const auto before=enemy->campaignCityLimits();
    require(before.sharedZoneCap>0 && before.harvesters<=enemy->campaignBaseline.allowance,"Invalid campaign economy limits");

    const auto originalGameType=game.gameType;
    game.gameType=GameType::CustomGame;
    require(!enemy->campaignCityEconomy(),"Policy leaked into custom games");
    game.gameType=originalGameType;

    auto place=[&](Uint32 item) {
        for(int y=1;y+4<currentGameMap->getSizeY();y+=4)
            for(int x=1;x+4<currentGameMap->getSizeX();x+=4) {
                bool clear=true;
                for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) return owner->placeStructure(NONE_ID,item,x,y,true,true);
            }
        throw std::runtime_error("No policy fixture site");
    };
    auto* first=dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard));
    auto* second=dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard));
    require(first && second,"Two-yard fixture failed");
    owner->addCredits(100000);
    for(auto* yard:{first,second})
        for(auto kind:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial,Structure_LightFactory})
            if(!yard->isAvailableToBuild(kind)) yard->buildList.emplace_back(kind,100);
    const auto baseline=enemy->campaignBaseline;
    enemy->campaignBaseline.allowance=100; // Avoid income cutoff while testing the physical queue ceiling.
    const int cap=enemy->campaignCityLimits().sharedZoneCap;
    const Uint32 kinds[]={Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial};
    {
        const auto savedConfig=enemy->configOverride;
        const auto savedRole=enemy->role;
        const auto savedDifficulty=enemy->difficulty;
        const auto savedTeam=owner->teamID;
        auto config=std::make_shared<QuantBotConfig>(enemy->botConfig());
        enemy->configOverride=config;
        const auto checkOrders=[&](bool limited) {
            for(int i=0;i<cap+6;++i) enemy->doProduceItem(i%2 ? second:first,kinds[i%3]);
            int committed=0;
            for(auto kind:kinds) committed+=enemy->campaignCommittedCount(kind);
            require(committed==(limited ? cap:cap+6),
                "Mission mode or controller role changed the actual shared RCI queue ceiling");
            for(auto* yard:{first,second}) for(auto kind:kinds)
                while(yard->getBuildItem(kind)->num>0) enemy->doCancelItem(yard,kind);
        };
        for(bool enabled:{false,true}) {
            config->force.singleMissionCampaignPolicy=enabled;
            for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,
                          GameType::SkirmishCoop,GameType::CustomGame,GameType::CustomMultiplayer}) {
                game.gameType=type;
                const bool limited=type==GameType::Campaign || type==GameType::CampaignCoop
                    || (enabled && (type==GameType::Skirmish || type==GameType::SkirmishCoop));
                checkOrders(limited);
            }
        }
        config->force.singleMissionCampaignPolicy=true;
        for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,GameType::SkirmishCoop}) {
            game.gameType=type;
            enemy->role=AIRole::Builder;checkOrders(false);
            enemy->role=savedRole;
            enemy->difficulty=QuantBot::Difficulty::Defend;checkOrders(false);
            enemy->difficulty=savedDifficulty;
            owner->teamID=ally->getHouse()->getTeamID();checkOrders(false);
            owner->teamID=savedTeam;
        }
        game.gameType=originalGameType;

        // Exercise the engine build list: the population exemption must follow
        // the same per-controller switch, never leak into a custom game.
        const auto savedPopulationRule=game.gameInitSettings.gameOptions.starportPopulation;
        const auto savedInitGameType=game.gameInitSettings.gameType;
        auto& starport=game.objectData.data[Structure_StarPort][first->getOriginalHouseID()];
        const auto savedStarportTech=starport.techLevel;
        const auto savedUpgradeLevel=first->curUpgradeLev;
        auto& city=game.getCitySimulation()->getHouseStateMut(owner->getHouseID());
        const int savedResidential=city.resPop,savedCommercial=city.comPop,savedIndustrial=city.indPop;
        const auto restoreStarportFixture=[&] {
            city.resPop=savedResidential;city.comPop=savedCommercial;city.indPop=savedIndustrial;
            game.gameInitSettings.gameOptions.starportPopulation=savedPopulationRule;
            game.gameInitSettings.gameType=savedInitGameType;
            starport.techLevel=savedStarportTech;first->curUpgradeLev=savedUpgradeLevel;
            game.gameType=originalGameType;enemy->configOverride=savedConfig;
        };
        try {
            city.resPop=city.comPop=city.indPop=0;
            // Early missions may not meet this product's configured technology.
            // Isolate population policy by lowering only its fixture tech gate;
            // retain the real enabled, builder, upgrade and structure prerequisites.
            starport.techLevel=std::min<int>(savedStarportTech,owner->getTechLevel());
            first->curUpgradeLev=first->getMaxUpgradeLevel();
            game.gameInitSettings.gameOptions.starportPopulation=false;
            first->updateBuildList();
            require(first->isAvailableToBuild(Structure_StarPort),
                "Starport fixture lacks non-population build prerequisites");
            game.gameInitSettings.gameOptions.starportPopulation=true;
            for(bool enabled:{false,true}) {
                config->force.singleMissionCampaignPolicy=enabled;
                for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,
                              GameType::SkirmishCoop,GameType::CustomGame,GameType::CustomMultiplayer}) {
                    game.gameType=type;game.gameInitSettings.gameType=type;first->updateBuildList();
                    const bool exempt=type==GameType::Campaign || type==GameType::CampaignCoop
                        || (enabled && (type==GameType::Skirmish || type==GameType::SkirmishCoop));
                    require(owner->meetsStarportPopulation()==exempt,
                        "Starport population exemption differs between campaign and Single mission policy");
                    require(first->isAvailableToBuild(Structure_StarPort)==exempt,
                        "Starport build permission differs between campaign and Single mission policy");
                }
            }
        } catch(...) {
            restoreStarportFixture();
            throw;
        }
        restoreStarportFixture();
        for(auto* yard:{first,second}) {
            yard->updateBuildList();
            // Keep the existing policy-only forbidden-building check independent
            // of the engine's prerequisite list rebuilt by the Starport boundary.
            if(!yard->isAvailableToBuild(Structure_LightFactory))
                yard->buildList.emplace_back(Structure_LightFactory,100);
        }
        SDL_Log("SINGLE_MISSION_POLICY_PASS: actual RCI queues and Starport permissions, "
            "full/single/co-op, OFF/ON, custom, allied, Builder and Defend boundaries");
    }
    enemy->campaignBaseline=baseline;
    if(!original.count(Structure_LightFactory)) {
        enemy->doProduceItem(first,Structure_LightFactory);
        require(first->getBuildItem(Structure_LightFactory)->num==0,"Actual order introduced forbidden light factory");
    }

    // A paid factory order must reserve the slot before a refinery can deliver.
    const auto difficulty=enemy->difficulty;
    enemy->difficulty=QuantBot::Difficulty::Brutal;
    enemy->campaignBaseline.allowance=7;
    enemy->harvesterLimit=enemy->campaignHarvesterTarget();
    first->buildList.emplace_back(Unit_Harvester,100);
    for(int i=0;i<10;++i) enemy->doProduceItem(first,Unit_Harvester);
    require(!enemy->campaignCanAddHarvester(),"Paid worker orders did not consume worker slots");
    const auto workersBefore=owner->getNumItems(Unit_Harvester);
    for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,GameType::SkirmishCoop}) {
        game.gameType=type;
        for(auto* structure:structureList) if(structure->getOwner()==owner && structure->getItemID()==Structure_Refinery) {
            owner->freeHarvester(structure->getLocation());break;
        }
        require(owner->getNumItems(Unit_Harvester)==workersBefore,"Refinery delivery overtook paid worker orders");
    }
    game.gameType=originalGameType;
    while(first->getBuildItem(Unit_Harvester)->num>0) enemy->doCancelItem(first,Unit_Harvester);
    enemy->difficulty=difficulty;enemy->campaignBaseline=baseline;
    enemy->harvesterLimit=enemy->campaignHarvesterTarget();

    // The House-level delivery path must not evade the difficulty/worker target.
    auto* worker=static_cast<Harvester*>(nullptr);
    for(auto* unit:unitList) if(unit->getOwner()==owner && unit->getItemID()==Unit_Harvester) {
        worker=dynamic_cast<Harvester*>(unit);break;
    }
    require(worker,"Fixture needs a real worker for fractional-cargo checks");

    // Advance the actual update path with fractional spice present first.
    for(int y=0;y<currentGameMap->getSizeY();++y) for(int x=0;x<currentGameMap->getSizeX();++x)
        currentGameMap->getTile(x,y)->setSpice(0);
    for(auto* unit:unitList) if(unit->getOwner()==owner)
        if(auto* h=dynamic_cast<Harvester*>(unit)) h->setAmountOfSpice(0);
    auto* spiceTile=currentGameMap->getTile(0,0);
    spiceTile->setSpice(FixPoint(1)/4);
    const auto tick=[&](Uint32 cycle) {
        game.gameCycleCount=cycle;enemy->buildTimer=INT32_MAX;enemy->update();
    };
    for(Uint32 cycle=1000;cycle<3500;++cycle) tick(cycle);
    require(!enemy->campaignPostSpice(),"Fractional map spice was rounded away");
    spiceTile->setSpice(0);
    for(Uint32 cycle=3500;cycle<4000;++cycle) tick(cycle);
    const auto zeroSince=enemy->campaignSpiceZeroSince;
    require(zeroSince!=UINT32_MAX,"Fresh zero-spice scan was not recorded");
    for(Uint32 cycle=4000;cycle<zeroSince+MILLI2CYCLES(30000);++cycle) {
        tick(cycle);require(!enemy->campaignPostSpice(),"Post-spice allowance activated before30 game seconds");
    }
    for(Uint32 cycle=zeroSince+MILLI2CYCLES(30000);cycle<=zeroSince+MILLI2CYCLES(31000);++cycle) tick(cycle);
    require(enemy->campaignPostSpice(),"Confirmed map exhaustion did not activate");
    const auto exhausted=enemy->campaignCityLimits();
    const int expected=enemy->difficulty==QuantBot::Difficulty::Hard ? 24
        : enemy->difficulty==QuantBot::Difficulty::Brutal ? 40 : before.sharedZoneCap;
    require(exhausted.sharedZoneCap==expected,"Wrong difficulty post-spice zone cap");
    worker->setAmountOfSpice(FixPoint(1)/4);
    require(!enemy->campaignPostSpice(),"Fractional/inactive worker cargo ignored");
    worker->setAmountOfSpice(0);

    // New saves keep the original permissions, frozen budget and depletion clock.
    const auto previousVersion=game.loadedSavegameVersion;game.loadedSavegameVersion=SAVEGAMEVERSION;
    OMemoryStream saved;saved.open();enemy->save(saved);
    for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,GameType::SkirmishCoop}) {
        game.gameType=type;
        IMemoryStream input(saved.getData(),saved.getDataLength());
        QuantBot loaded(input,owner);
        require(loaded.campaignBaselineCaptured==enemy->campaignBaselineCaptured
            && loaded.campaignBaseline.refineries==enemy->campaignBaseline.refineries
            && loaded.campaignBaseline.allowance==enemy->campaignBaseline.allowance,
            "Save/load changed original income budget");
        require(loaded.campaignOriginalStructures==enemy->campaignOriginalStructures,"Save/load changed original building permissions");
        require(loaded.campaignSpiceZeroSince==enemy->campaignSpiceZeroSince,"Save/load reset exhaustion timer");
        require(loaded.campaignCityLimits().sharedZoneCap==exhausted.sharedZoneCap,"Save/load changed active zone cap");
    }
    game.gameType=originalGameType;
    // The old prefix remains readable; reconstruct authored permissions before top-ups.
    game.loadedSavegameVersion=9841;
    const auto trailerSize=18+4*enemy->campaignOriginalStructures.size();
    for(auto type:{GameType::Campaign,GameType::CampaignCoop,GameType::Skirmish,GameType::SkirmishCoop}) {
        game.gameType=type;
        IMemoryStream legacyInput(saved.getData(),saved.getDataLength()-trailerSize);
        QuantBot legacy(legacyInput,owner);
        require(!legacy.campaignBaselineCaptured,"Legacy stream consumed new-format state");
        legacy.noteCampaignOriginalState(true);
        for(auto item:legacy.campaignOriginalStructures) if(!original.count(item)) SDL_Log("LEGACY_EXTRA: %u",item);
        for(auto item:original) if(!legacy.campaignOriginalStructures.count(item)) SDL_Log("LEGACY_MISSING: %u",item);
        require(legacy.campaignOriginalStructures==original,"Legacy save did not recover authored building types");
        require(legacy.campaignBaseline.refineries==originalRefineries,"Legacy save did not recover original refineries");
    }
    game.gameType=originalGameType;
    game.loadedSavegameVersion=previousVersion;
    spiceTile->setSpice(FixPoint(1)/4);
    const auto resume=game.gameCycleCount+1;
    for(Uint32 cycle=resume;cycle<resume+501;++cycle) tick(cycle);
    require(!enemy->campaignPostSpice() && enemy->campaignCityLimits().sharedZoneCap==before.sharedZoneCap,
        "Restored map spice left extra expansion enabled");
}

void runCampaignCityEconomyProbe(std::unique_ptr<Game>& ownedGame, int level,
                                const std::string& candidateClass, const std::string& enemyClass) {
    // Positive-world references and controller pointers end before native
    // outcome/content controls replace and finally destroy the owned world.
    runCampaignCityEconomyPositiveProbe(*ownedGame);
    runCampaignObserverOutcomeProbe(ownedGame,level,candidateClass,enemyClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_campaign outcome=passed");
    SDL_Log("CITY_CAMPAIGN_PROBE_PASS: original permissions, ally exclusion, fractional spice/cargo, exact depletion delay, restore and save/load; native Full/Single observer quota, defeat and timeout outcomes; generic Custom observer timeouts; real native Vanilla and non-DuneCity city content controls");
}
