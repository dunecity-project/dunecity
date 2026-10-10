// Real-window checks for the read-only Stats window, the observer-browsable
// budget window and the sidebar that has to hold both at the smallest supported
// display. Every value asserted here is compared against the engine counter or
// the production allocation snapshot it is supposed to be showing.
#include <GUI/dune/HouseStatsWindow.h>
#include <GUI/dune/CityBudgetWindow.h>
#include <GUI/dune/ObservedHouseSelector.h>
#include <GameInterface.h>
#include <players/QuantBot.h>
#include <players/CombatReward.h>
#include <dunecity/CitySimulation.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <structures/BuilderBase.h>
#include <units/UnitBase.h>
#include <mod/ModManager.h>
#include <Map.h>
#include <Tile.h>

void runStatsUiProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok){SDL_Log("STATS_UI_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto commandCount=[&]() {
        size_t count=0;
        for(const auto& slot:game.cmdManager.timeslot) count+=slot.size();
        return count;
    };
    auto capture=[&](const char* name) {
        game.drawScreen();
        int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
        sdl2::surface_ptr image{SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888)};
        require(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,
                                     image->pixels,image->pitch)==0,"Stats UI capture failed");
        SDL_SaveBMP(image.get(),name);
        SDL_RenderPresent(renderer);
        SDL_Log("STATS_UI_SHOT: %s %dx%d",name,w,h);
    };
    int rendererW=0,rendererH=0;
    SDL_GetRendererOutputSize(renderer,&rendererW,&rendererH);
    SDL_Log("STATS_UI_DIAG: renderer=%dx%d",rendererW,rendererH);
    if(const char* expected=std::getenv("BALANCE_STATS_EXPECTED_WIDTH"))
        require(rendererW==std::atoi(expected),"Stats renderer width differs from the requested viewport");
    if(const char* expected=std::getenv("BALANCE_STATS_EXPECTED_HEIGHT"))
        require(rendererH==std::atoi(expected),"Stats renderer height differs from the requested viewport");

    require(pLocalHouse!=nullptr,"Stats UI fixture has no local house");
    const int localHouseID=pLocalHouse->getHouseID();
    House* otherHouse=nullptr;
    for(int h=0;h<NUM_HOUSES && otherHouse==nullptr;++h)
        if(auto* candidate=game.getHouse(h); candidate && h!=localHouseID) otherHouse=candidate;
    require(otherHouse!=nullptr,"Stats UI fixture needs a second house to browse to");

    // === A real, nonzero ledger on two different houses ===
    // Written through the engine's own accessors so the window is read against
    // the same counters the simulation maintains.
    auto seedLedger=[&](House* house,Uint32 item,uint64_t kills,int64_t hpRemoved,int deaths) {
        CombatReward::Totals reward{};
        reward.damageMilli=hpRemoved*7;reward.hpRemovedMilli=hpRemoved;
        reward.hits=kills*3;reward.kills=kills;
        house->addCombatReward(item,reward);
        house->numItemLosses[item]+=deaths;   // Test-only access (-fno-access-control).
    };
    seedLedger(pLocalHouse,Unit_Tank,7,123456,3);
    seedLedger(pLocalHouse,Unit_Launcher,2,45678,5);
    seedLedger(otherHouse,Unit_Quad,11,222000,1);
    // The four types the table must never list, given exactly the things that
    // would otherwise make a row appear: real ledger history on two houses and,
    // below, units the house actually owns right now.
    const Uint32 hiddenTypes[]={Unit_AmbientAirplane,Unit_AmbientHelicopter,
                                Unit_RocketTrike,Unit_EliteLauncher};
    seedLedger(pLocalHouse,Unit_EliteLauncher,13,98765,4);
    seedLedger(pLocalHouse,Unit_RocketTrike,5,4321,2);
    seedLedger(otherHouse,Unit_AmbientHelicopter,1,1000,1);
    seedLedger(otherHouse,Unit_AmbientAirplane,0,0,2);
    auto placeOwnedUnit=[&](House* house,Uint32 item) -> UnitBase* {
        for(int y=1;y<currentGameMap->getSizeY()-1;++y)
            for(int x=1;x<currentGameMap->getSizeX()-1;++x) {
                const auto* tile=currentGameMap->getTile(x,y);
                if(tile->hasAGroundObject() || tile->isMountain()) continue;
                if(auto* unit=house->placeUnit(item,x,y)) return unit;
            }
        return nullptr;
    };
    int liveHiddenPlaced=0;
    for(const auto item : hiddenTypes) {
        // The Tornie-only variants cannot be instantiated outside their mod, so
        // only the types this ruleset can really create are placed.
        if(isTornieExclusiveItem(item) && !ModManager::instance().isTornieContentActive()) continue;
        if(placeOwnedUnit(pLocalHouse,item)!=nullptr) ++liveHiddenPlaced;
    }
    require(liveHiddenPlaced>0,
            "The exclusion check could not give this house a live excluded unit");
    pLocalHouse->numBuiltUnits+=42;
    pLocalHouse->setSpiceIncomeMultiplier(5);
    pLocalHouse->harvestedSpice+=FixPoint(1234 * 5);
    otherHouse->numBuiltUnits+=9;
    otherHouse->harvestedSpice+=FixPoint(77);
    const bool cityEconomy=game.getCitySimulation()!=nullptr
        && game.getCitySimulation()->isInitialized();
    SDL_Log("STATS_UI_DIAG: city_economy=%d",int(cityEconomy));
    if(cityEconomy) {
        pLocalHouse->cityTaxReceipts+=FixPoint(5150);
        otherHouse->cityTaxReceipts+=FixPoint(640);
        auto& localCity=game.getCitySimulation()->getHouseStateMut(localHouseID);
        localCity.resPop=30;localCity.comPop=12;localCity.indPop=8;
        auto& otherCity=game.getCitySimulation()->getHouseStateMut(otherHouse->getHouseID());
        otherCity.resPop=5;otherCity.comPop=2;otherCity.indPop=1;
    }

    // === The sidebar button really opens it ===
    game.unselectAll(game.selectedList);
    game.pInterface->updateObjectInterface();
    require(game.pInterface->statsButton.isVisible(),"Sidebar has no visible Stats button");
    require(game.pInterface->statsButton.getText()==std::string("Stats"),
            "The sidebar item is not labelled exactly \"Stats\"");
    // Every stacked sidebar control must stay inside the supported minimum
    // interface height and must not overlap its neighbour.
    {
        const auto* stats=game.pInterface->windowWidget.getWidgetDataFromWidget(&game.pInterface->statsButton);
        require(stats!=nullptr,"Stats button is not laid out in the sidebar");
        require(stats->position.y+stats->size.y<=480,
                "Stats button falls below the 480-line minimum interface height");
        require(stats->size.y>=36,"Stats button lost its click target height");
        const auto* paths=game.pInterface->windowWidget.getWidgetDataFromWidget(&game.pInterface->movementPathsButton);
        require(paths && paths->position.y+paths->size.y<=stats->position.y,
                "Stats button overlaps the movement-paths control");
        if(game.isCitySimEnabled()) {
            const auto* pollution=game.pInterface->windowWidget.getWidgetDataFromWidget(&game.pInterface->pollutionOverlayButton);
            require(pollution && pollution->position.y+pollution->size.y<=stats->position.y,
                    "Stats button overlaps the Pollution overlay control");
        }
        if(game.canSkipMission()) {
            const auto* skip=game.pInterface->windowWidget.getWidgetDataFromWidget(&game.pInterface->skipMissionButton);
            require(skip && skip->position.y>=stats->position.y+stats->size.y
                    && skip->position.y+skip->size.y<=480,
                    "Skip mission no longer fits below Stats at the minimum height");
        }
        SDL_Log("STATS_UI_DIAG: stats_button_y=%d height=%d",stats->position.y,stats->size.y);
    }
    const size_t commandsBeforeOpen=commandCount();
    game.pInterface->statsButton.handleMouseLeft(5,5,true);
    game.pInterface->statsButton.handleMouseLeft(5,5,false);
    game.pInterface->processChildWindowOpenCloses();
    auto* stats=dynamic_cast<HouseStatsWindow*>(game.pInterface->pChildWindow);
    require(stats!=nullptr,"The sidebar Stats button did not open the statistics window");
    require(commandCount()==commandsBeforeOpen,"Opening the statistics window issued a command");
    require(stats->getSize().x<=rendererW && stats->getSize().y<=rendererH,
            "The statistics window does not fit the renderer");

    // === A participant sees their own house and cannot browse ===
    require(!stats->nextHouseButton.isVisible(),
            "A participating player was offered the observer house-cycling control");
    const std::string ownHouseText=std::string("House: ")
        +getHouseNameByNumber(static_cast<HOUSETYPE>(localHouseID));
    require(stats->houseLabel.getText()==ownHouseText,
            "The statistics window does not name the player's own house");
    stats->onNextHouse();
    require(stats->selectedHouseID==localHouseID && stats->houseLabel.getText()==ownHouseText,
            "A participant's statistics window changed house");
    require(pLocalHouse->getHouseID()==localHouseID,"Browsing changed the local house identity");

    // === The summary is the engine's cumulative counters ===
    auto summaryMatches=[&](const House* house,const char* why) {
        stats->draw(Point(0,0));
        require(stats->unitsBuiltLabel.getText()
                ==fmt::sprintf("Units produced: %d",house->getNumBuiltUnits()),why);
        require(stats->spiceHarvestedLabel.getText()
                ==fmt::sprintf("Spice harvested: %d",(house->getHarvestedSpice() / house->getSpiceIncomeMultiplier()).lround()),why);
        if(cityEconomy) {
            require(stats->taxCollectedLabel.getText()
                    ==fmt::sprintf("Tax collected: %d",house->getCityTaxReceipts().lround()),why);
            const auto& state=game.getCitySimulation()->getHouseState(house->getHouseID());
            require(stats->populationLabel.getText()
                    ==fmt::sprintf("Population: %d",
                        state.getTotalPop()*DuneCity::CitySimulation::kPopDisplayMultiplier),why);
        } else {
            // No city economy: both city figures are reported as unavailable
            // rather than as a fabricated zero.
            require(stats->taxCollectedLabel.getText()=="Tax collected: N/A",why);
            require(stats->populationLabel.getText()=="Population: N/A",why);
        }
    };
    summaryMatches(pLocalHouse,"The house summary does not match this house's own engine counters");
    require(stats->unitsBuiltLabel.getText()!=fmt::sprintf("Units produced: %d",
                pLocalHouse->getNumUnits()),
            "Units produced is showing the current unit count instead of the cumulative total");

    // === The unit table is the combat ledger, not the victim counter ===
    auto tableRow=[&](Uint32 item) {
        for(size_t i=0;i<stats->ledger.size();++i) if(stats->ledger[i].itemID==item) return int(i);
        return -1;
    };

    // Five headers, five cells per row, all measured in the font that is really
    // drawing them. Nothing may be wider than the column it was given and the
    // five columns together may not be wider than the table.
    auto checkTableColumns=[&](HouseStatsWindow* w,const char* why) {
        const std::pair<const Label*,const char*> columns[]={
            {&w->headerName,"Type"},{&w->headerActive,"Active"},{&w->headerKills,"Kills"},
            {&w->headerDeaths,"Deaths"},{&w->headerDamage,"Damage"}};
        int total=0;
        for(const auto& column : columns) {
            const Label* label=column.first;
            require(label->getText()==std::string(column.second),
                    "The unit table lost one of its five column headers");
            require(label->isVisible(),"A unit table column header is hidden");
            require(GUIStyle::getInstance().getTextWidth(label->getText(),
                        label->getTextFontSize())<=label->getSize().x,why);
            total+=label->getSize().x;
        }
        require(total<=w->tableHeaderHBox.getSize().x,
                "The five unit table columns do not fit the table width");
        for(const auto& row : w->rows)
            for(const auto* cell : {&row->name,&row->active,&row->kills,&row->deaths,&row->damage})
                require(GUIStyle::getInstance().getTextWidth(cell->getText(),
                            cell->getTextFontSize())<=cell->getSize().x,why);
    };

    // Active is that house's own live item count, every type it owns is listed,
    // and no excluded type ever is.
    auto checkActiveCounts=[&](HouseStatsWindow* w,const House* house,const char* why) {
        for(const auto& entry : w->ledger) {
            require(entry.active==house->getNumItems(entry.itemID),why);
            for(const auto hiddenItem : hiddenTypes)
                require(entry.itemID!=hiddenItem,
                        "An excluded unit type is listed for this house");
        }
        for(int item=0;item<Num_ItemID;++item) {
            if(!isUnit(item) || house->getNumItems(item)<=0) continue;
            bool listed=false;
            for(const auto& entry : w->ledger) if(entry.itemID==Uint32(item)) listed=true;
            bool hidden=false;
            for(const auto hiddenItem : hiddenTypes) if(hiddenItem==Uint32(item)) hidden=true;
            require(listed!=hidden,hidden
                    ?"An excluded unit type is listed although the house owns one"
                    :"A unit type this house owns is missing from the table");
        }
        const size_t first=size_t(w->page)*size_t(w->rowsPerPage);
        for(size_t i=0;i<w->rows.size() && first+i<w->ledger.size();++i)
            require(w->rows[i]->active.getText()==std::to_string(w->ledger[first+i].active),why);
    };

    stats->draw(Point(0,0));
    SDL_Log("STATS_UI_DIAG: columns name=%d active=%d kills=%d deaths=%d damage=%d table=%d"
            " header_text name=%u active=%u kills=%u deaths=%u damage=%u",
            stats->nameColumnWidth,stats->activeColumnWidth,stats->killsColumnWidth,
            stats->deathsColumnWidth,stats->damageColumnWidth,stats->tableHeaderHBox.getSize().x,
            GUIStyle::getInstance().getTextWidth("Type",12),
            GUIStyle::getInstance().getTextWidth("Active",12),
            GUIStyle::getInstance().getTextWidth("Kills",12),
            GUIStyle::getInstance().getTextWidth("Deaths",12),
            GUIStyle::getInstance().getTextWidth("Damage",12));
    checkTableColumns(stats,"A unit table column is wider than its allotted space");
    checkActiveCounts(stats,pLocalHouse,"Active does not match this house's live item counts");

    // Exclusion wins over ledger history and over live ownership alike.
    for(const auto item : hiddenTypes) {
        const bool hasRecord=pLocalHouse->getCombatReward(item).kills>0
            || pLocalHouse->getNumLostItems(item)>0 || pLocalHouse->getNumItems(item)>0
            || otherHouse->getCombatReward(item).kills>0
            || otherHouse->getNumLostItems(item)>0;
        require(hasRecord,"The exclusion check left an excluded type with nothing to hide");
        require(tableRow(item)<0,"An excluded unit type is listed from its stale records");
        // Compared against the text the window would actually draw for that
        // name, so a shortened label cannot pass this unnoticed.
        const std::string drawn=HouseStatsWindow::fitText(getItemNameByID(item),
                                                          stats->nameColumnWidth-4,12);
        for(const auto& row : stats->rows)
            require(row->name.getText()!=drawn,"An excluded unit type is drawn in the unit table");
    }
    int liveHiddenOwned=0;
    for(const auto item : hiddenTypes) liveHiddenOwned+=pLocalHouse->getNumItems(item);
    require(liveHiddenOwned>0,"The exclusion check lost the live excluded units it placed");
    SDL_Log("STATS_UI_DIAG: hidden_live_placed=%d hidden_live_owned=%d",
            liveHiddenPlaced,liveHiddenOwned);
    for(const auto item : {Unit_Tank,Unit_Launcher}) {
        const int index=tableRow(item);
        require(index>=0,"A unit type with a nonzero ledger is missing from the table");
        const auto& entry=stats->ledger[index];
        const auto& reward=pLocalHouse->getCombatReward(item);
        require(entry.kills==reward.kills,"Table kills are not the attacker-type credited kills");
        require(entry.deaths==pLocalHouse->getNumLostItems(item),"Table deaths are not this house's losses");
        require(entry.damageHp==reward.hpRemovedMilli/1000,"Table damage is not the removed hit points");
        require(entry.kills!=static_cast<uint64_t>(pLocalHouse->getNumKilledItems(item))
                || reward.kills==static_cast<uint64_t>(pLocalHouse->getNumKilledItems(item)),
                "Table kills coincide with the victim-type counter");
    }
    require(!stats->ledger.empty() && stats->emptyStateLabel.getText().empty(),
            "A populated ledger still showed the empty state");
    require(stats->pageLabel.getText().find('/')!=std::string::npos,
            "The unit table has no page readout");
    {
        // Paging must reach every row and keep all five columns readable on
        // every page, in both directions.
        const int pages=std::max(1,(int(stats->ledger.size())+stats->rowsPerPage-1)/stats->rowsPerPage);
        SDL_Log("STATS_UI_DIAG: ledger_rows=%d rows_per_page=%d pages=%d",
                int(stats->ledger.size()),stats->rowsPerPage,pages);
        require(stats->pageLabel.getText()==fmt::sprintf("1 / %d",pages),"Wrong initial page readout");
        for(int p=1;p<pages;++p) {
            stats->onNextPage();
            require(stats->page==p,"Next page did not advance the unit table");
            checkTableColumns(stats,"A unit table column overflows on a later page");
            checkActiveCounts(stats,pLocalHouse,"A later page shows the wrong Active counts");
        }
        for(int p=pages-2;p>=0;--p) {
            stats->onPreviousPage();
            require(stats->page==p,"Previous page did not go back");
            checkTableColumns(stats,"A unit table column overflows on an earlier page");
            checkActiveCounts(stats,pLocalHouse,"An earlier page shows the wrong Active counts");
        }
    }

    // === Active follows the live count, on the bounded per-second sample ===
    // A type with no history that this house cannot build must still be listed
    // while the house really owns one, and must keep its recorded loss after.
    {
        // Whichever ordinary fighting type this map left this house without.
        Uint32 probeItem=NONE_ID;
        for(const Uint32 candidate : {Unit_Quad,Unit_Trike,Unit_RaiderTrike,Unit_SiegeTank,
                                      Unit_Devastator,Unit_SonicTank,Unit_Deviator,Unit_Soldier})
            if(probeItem==NONE_ID && pLocalHouse->getNumItems(candidate)==0
               && pLocalHouse->getNumLostItems(candidate)==0
               && pLocalHouse->getCombatReward(candidate).kills==0) probeItem=candidate;
        require(probeItem!=NONE_ID,"The Active check found no type without history on this house");
        SDL_Log("STATS_UI_DIAG: active_probe_item=%u name=%s",probeItem,
                getItemNameByID(probeItem).c_str());
        auto& probeSpec=game.objectData.data[probeItem][localHouseID];
        const bool savedEnabled=probeSpec.enabled;
        probeSpec.enabled=false;       // Test-only: a type this house cannot build.
        game.gameCycleCount+=MILLI2CYCLES(1000);
        stats->draw(Point(0,0));
        require(tableRow(probeItem)<0,"A type with no history and no production was still listed");

        auto* quad=placeOwnedUnit(pLocalHouse,probeItem);
        require(quad!=nullptr,"The Active check could not place a unit");
        require(pLocalHouse->getNumItems(probeItem)==1,"The engine did not count the placed unit");
        stats->draw(Point(0,0));
        require(tableRow(probeItem)<0,
                "The unit table resampled the ledger inside the same simulation second");
        game.gameCycleCount+=MILLI2CYCLES(1000);
        stats->draw(Point(0,0));
        const int owned=tableRow(probeItem);
        require(owned>=0,"A type this house owns was hidden because production is disabled");
        require(stats->ledger[owned].active==pLocalHouse->getNumItems(probeItem)
                && stats->ledger[owned].kills==0 && stats->ledger[owned].deaths==0,
                "The Active column is not the house's live item count");
        checkTableColumns(stats,"A unit table column overflows once an Active count appears");
        checkActiveCounts(stats,pLocalHouse,"Active did not follow the new live unit");

        quad->setVisible(VIS_ALL,false);
        quad->destroy();
        game.gameCycleCount+=MILLI2CYCLES(1000);
        stats->draw(Point(0,0));
        const int lost=tableRow(probeItem);
        require(lost>=0 && stats->ledger[lost].active==0
                && stats->ledger[lost].deaths==pLocalHouse->getNumLostItems(probeItem)
                && stats->ledger[lost].deaths>0,
                "A lost unit did not clear Active while keeping its recorded loss");
        SDL_Log("STATS_UI_DIAG: active_refresh_rows=%d",int(stats->ledger.size()));
        probeSpec.enabled=savedEnabled;
    }
    capture("stats-window-player.bmp");

    // === QuantBot rows are the production allocation, exactly ===
    House* botHouse=nullptr;
    const QuantBot* bot=nullptr;
    for(int h=0;h<NUM_HOUSES && bot==nullptr;++h) {
        auto* candidate=game.getHouse(h);
        if(candidate==nullptr || candidate==pLocalHouse) continue;
        for(const auto& player : candidate->getPlayerList())
            if(const auto* found=dynamic_cast<const QuantBot*>(player.get()); found && !found->supportMode) {
                botHouse=candidate;bot=found;break;
            }
    }
    require(bot!=nullptr && botHouse!=nullptr,"Stats UI fixture has no QuantBot house");
    // A real production pass of that bot, through its own entry point, so the
    // snapshot is the allocation the pass actually used rather than a fixture
    // value. Advance a little first so the house has live state to plan from.
    // Provide a real factory, its upgrades, unit technology and prerequisites.
    // City campaign openings intentionally have none yet; displaying only
    // unavailable slots would never test scores or percentages.
    auto place=[&](Uint32 item) {
        const auto size=getStructureSize(item);
        for(int y=1;y+size.y<currentGameMap->getSizeY();++y)
            for(int x=1;x+size.x<currentGameMap->getSizeX();++x) {
                bool clear=true;
                for(int dy=0;dy<size.y;++dy)for(int dx=0;dx<size.x;++dx) {
                    const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                    if(tile->hasAGroundObject() || tile->isMountain())clear=false;
                }
                if(clear)return botHouse->placeStructure(NONE_ID,item,x,y,true,true);
            }
        throw std::runtime_error("No Stats fixture factory site");
    };
    int upgrade=0;
    for(Uint32 item : {Unit_Tank,Unit_Launcher}) {
        const auto& spec=game.objectData.data[item][botHouse->getHouseID()];
        require(spec.enabled,"The Stats fixture unit is disabled");
        game.techLevel=std::max<int>(game.techLevel,spec.techLevel);
        upgrade=std::max<int>(upgrade,spec.upgradeLevel);
        for(int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
            if(spec.prerequisiteStructuresSet[prerequisite]
                && botHouse->getNumItems(prerequisite)==0)
                require(place(prerequisite)!=nullptr,"Stats unit prerequisite placement failed");
    }
    auto* factory=dynamic_cast<BuilderBase*>(place(Structure_HeavyFactory));
    require(factory!=nullptr,"Stats fixture factory placement failed");
    factory->curUpgradeLev=upgrade;
    factory->updateBuildList();
    require(factory->isAvailableToBuild(Unit_Tank) && factory->isAvailableToBuild(Unit_Launcher),
            "The Stats fixture factory has no real producible allocation units");
    seedLedger(botHouse,Unit_Launcher,31,123456789,7);
    for(int step=0;step<120;++step) { game.processObjects();++game.gameCycleCount; }
    const_cast<QuantBot*>(bot)->build(bot->militaryUnitValue());
    const auto& snapshot=bot->getAllocationSnapshot();
    SDL_Log("STATS_UI_DIAG: snapshot_ready=%d cycle=%u learning=%d",
            int(snapshot.ready),snapshot.cycle,int(snapshot.learning));
    require(snapshot.ready,"No QuantBot production pass produced an allocation snapshot");
    for(size_t i=0;i<snapshot.slots.size();++i)
        require(snapshot.slots[i].targetBps==bot->getLastUnitMixBps()[i],
                "The snapshot share differs from the mix the production pass used");

    // Observe that house and compare the rendered rows with the snapshot.
    game.localObserver=true;      // Test-only access: the real observing path.
    require(game.isObserving(),"The observer fixture is not observing");
    game.pInterface->closeChildWindow();
    game.pInterface->processChildWindowOpenCloses();
    game.pInterface->openWindow(HouseStatsWindow::create());
    game.pInterface->processChildWindowOpenCloses();
    stats=dynamic_cast<HouseStatsWindow*>(game.pInterface->pChildWindow);
    require(stats!=nullptr,"The observer could not open the statistics window");
    require(stats->nextHouseButton.isVisible(),"An observer was not offered the house-cycling control");
    const auto browsable=ObservedHouse::browsable();
    require(browsable.size()>=2,"The observer fixture needs at least two houses");
    // Browse A -> B -> ... -> A and check the name and the values follow.
    std::vector<int> visited;
    for(size_t i=0;i<browsable.size();++i) {
        visited.push_back(stats->selectedHouseID);
        const House* shown=game.getHouse(stats->selectedHouseID);
        require(shown!=nullptr,"Browsed to a house that does not exist");
        require(stats->houseLabel.getText()==std::string("House: ")
                +getHouseNameByNumber(static_cast<HOUSETYPE>(shown->getHouseID())),
                "The observer's house name does not follow the selection");
        summaryMatches(shown,"The observed house summary does not match that house's counters");
        // Each browsed house's own live counts, in its own five columns.
        game.gameCycleCount+=MILLI2CYCLES(1000);
        stats->draw(Point(0,0));
        checkActiveCounts(stats,shown,"A browsed house shows another house's Active counts");
        checkTableColumns(stats,"A unit table column overflows for a browsed house");
        SDL_Log("STATS_UI_DIAG: house=%d ledger_rows=%d units=%d",
                shown->getHouseID(),int(stats->ledger.size()),shown->getNumUnits());
        if(shown==botHouse) {
            stats->draw(Point(0,0));
            bool hasGoal=false;
            for(size_t slot=0;slot<snapshot.slots.size();++slot) {
                const auto& entry=snapshot.slots[slot];
                const auto& row=stats->allocationRows[slot];
                for(const auto* label : {&row.name,&row.performance,&row.goal})
                    require(GUIStyle::getInstance().getTextWidth(label->getText(),
                                label->getTextFontSize())<=label->getSize().x,
                            "An allocation column is wider than its allotted space");
                if(!entry.allocated) {
                    require(row.performance.getText()=="-" && row.goal.getText()=="-",
                            "An unallocated type was given a score or goal");
                } else {
                    hasGoal=true;
                    require(row.performance.getText()==fmt::sprintf("x%.1f",
                                static_cast<double>(entry.score)/1000000.0),
                            "The displayed performance is not the production score multiplier");
                    require(row.goal.getText()==fmt::sprintf("%d.%02d",entry.targetBps/100,
                                entry.targetBps%100),
                            "The displayed Goal % is not the production share");
                }
            }
            require(hasGoal,"The allocation fixture never displayed a real allocated goal");
            require(stats->allocationGoalHeader.getText()=="Goal %",
                    "The goal column is missing its percent unit");
            require(snapshot.learning,"The allocation fixture never exercised measured performance");
            require(stats->allocationStateLabel.getText().find(bot->getDifficultyName())
                    !=std::string::npos,"The allocation section does not name the controller");
            capture("stats-window-observer-quantbot.bmp");
        }
        stats->onNextHouse();
    }
    require(stats->selectedHouseID==visited.front(),"Observer house cycling did not wrap to the first house");
    require(std::set<int>(visited.begin(),visited.end()).size()==browsable.size(),
            "Observer cycling repeated a house instead of visiting each one");
    require(pLocalHouse==nullptr || pLocalHouse->getHouseID()==localHouseID,
            "Observer browsing changed the local house identity");
    capture("stats-window-observer.bmp");

    // A house with no QuantBot still has its ordinary statistics.
    {
        House* plainHouse=nullptr;
        for(int h=0;h<NUM_HOUSES && plainHouse==nullptr;++h) {
            auto* candidate=game.getHouse(h);
            if(candidate==nullptr || candidate==botHouse) continue;
            bool hasBot=false;
            for(const auto& player : candidate->getPlayerList())
                if(dynamic_cast<const QuantBot*>(player.get())) hasBot=true;
            if(!hasBot) plainHouse=candidate;
        }
        if(plainHouse!=nullptr) {
            while(stats->selectedHouseID!=plainHouse->getHouseID()) stats->onNextHouse();
            stats->draw(Point(0,0));
            require(stats->allocationStateLabel.getText()=="No QuantBot on this house",
                    "A house without a QuantBot claimed an allocation");
            summaryMatches(plainHouse,"A house without a QuantBot lost its ordinary statistics");
            SDL_Log("STATS_UI_DIAG: non_quantbot_house=%d units=%d",
                    plainHouse->getHouseID(),plainHouse->getNumBuiltUnits());
        }
    }

    // === The observer's budget window is a read-only browser ===
    if(cityEconomy) {
        game.pInterface->closeChildWindow();game.pInterface->processChildWindowOpenCloses();
        const size_t commandsBeforeBudget=commandCount();
        game.onCityBudget();
        game.pInterface->processChildWindowOpenCloses();
        auto* budget=dynamic_cast<CityBudgetWindow*>(game.pInterface->pChildWindow);
        require(budget!=nullptr,"An observer could not open the budget window");
        require(budget->isReadOnly(),"The observer's budget window is not read-only");
        for(const auto* control : {&budget->confirmButton,&budget->taxPlus,&budget->taxMinus,
                                   &budget->policePlus,&budget->policeMinus})
            require(!control->isVisible() && !control->isEnabled(),
                    "An observer was offered a budget adjustment or Apply");
        require(budget->nextHouseButton.isVisible(),"The observer's budget window cannot browse houses");
        // Invoking the callbacks directly must still generate no command and
        // must not move the pending values of the house being observed.
        const int taxBefore=budget->pendingTaxRate,policeBefore=budget->pendingPolicePercent;
        budget->onTaxIncrease();budget->onTaxDecrease();
        budget->onPoliceIncrease();budget->onPoliceDecrease();
        require(budget->pendingTaxRate==taxBefore && budget->pendingPolicePercent==policeBefore,
                "A read-only budget window accepted an adjustment");
        budget->onConfirm();
        game.pInterface->processChildWindowOpenCloses();
        require(commandCount()==commandsBeforeBudget,
                "An observer's budget window generated a fiscal command");
        // Reopen and browse: the readouts must follow the selected house.
        game.onCityBudget();game.pInterface->processChildWindowOpenCloses();
        budget=dynamic_cast<CityBudgetWindow*>(game.pInterface->pChildWindow);
        require(budget!=nullptr,"The observer could not reopen the budget window");
        std::vector<int> budgetVisited;
        for(size_t i=0;i<browsable.size();++i) {
            budget->draw(Point(0,0));
            budgetVisited.push_back(budget->selectedHouseID);
            const House* shown=game.getHouse(budget->selectedHouseID);
            require(shown!=nullptr,"The budget window browsed to a missing house");
            require(budget->houseLabel.getText().find(
                        getHouseNameByNumber(static_cast<HOUSETYPE>(shown->getHouseID())))
                    !=std::string::npos,"The budget window does not name the browsed house");
            require(budget->treasuryLabel.getText()
                    ==fmt::sprintf("Treasury: %d credits",shown->getCredits()),
                    "The budget treasury does not follow the browsed house");
            const auto& state=game.getCitySimulation()->getHouseState(shown->getHouseID());
            require(budget->totalPopLabel.getText()
                    ==fmt::sprintf("Population: %d",
                        state.getTotalPop()*DuneCity::CitySimulation::kPopDisplayMultiplier),
                    "The budget population does not follow the browsed house");
            if(i==0) capture("budget-window-observer.bmp");
            budget->onNextHouse();
        }
        // Keep the same observer window open while the selected house changes
        // its actual fiscal policy. The readout must follow, rather than keep
        // the values from when this window opened.
        auto* sim=game.getCitySimulation();
        const int savedPolice=sim->getPoliceFundingPercent(budget->selectedHouseID);
        const int savedTax=sim->getCityTax();
        sim->setPoliceFundingPercent(budget->selectedHouseID,35);
        sim->setCityTax(9);
        budget->draw(Point(0,0));
        require(budget->policeValueLabel.getText()=="35%" && budget->taxValueLabel.getText()=="9%",
                "An open observer budget did not refresh the live policy");
        sim->setPoliceFundingPercent(budget->selectedHouseID,savedPolice);
        sim->setCityTax(savedTax);
        require(budget->selectedHouseID==budgetVisited.front(),
                "The budget window did not wrap back to the first house");
        require(commandCount()==commandsBeforeBudget,
                "Browsing the budget window generated a command");
        game.pInterface->closeChildWindow();game.pInterface->processChildWindowOpenCloses();
    }

    // === The snapshot survives an observer checkpoint ===
    {
        const auto runtime=game.saveObserverRuntime();
        auto* mutableBot=const_cast<QuantBot*>(bot);
        const auto saved=mutableBot->allocationSnapshot;
        mutableBot->allocationSnapshot=QuantBot::AllocationSnapshot{};
        require(!mutableBot->getAllocationSnapshot().ready,"The snapshot could not be cleared");
        game.loadObserverRuntime(runtime);
        const auto& restored=bot->getAllocationSnapshot();
        require(restored.ready && restored.cycle==saved.cycle
                && restored.learning==saved.learning
                && restored.militaryValue==saved.militaryValue
                && restored.antiAirFloorBps==saved.antiAirFloorBps
                && restored.infantryQuotaBps==saved.infantryQuotaBps,
                "The observer checkpoint lost the allocation snapshot header");
        for(size_t i=0;i<restored.slots.size();++i)
            require(restored.slots[i].itemID==saved.slots[i].itemID
                    && restored.slots[i].score==saved.slots[i].score
                    && restored.slots[i].targetBps==saved.slots[i].targetBps
                    && restored.slots[i].allocated==saved.slots[i].allocated,
                    "The observer checkpoint lost an allocation slot");
        // The cumulative house ledger the window reads is ordinary saved state and
        // must survive the same checkpoint.
        require(pLocalHouse==nullptr || pLocalHouse->getCombatReward(Unit_Tank).kills>=7,
                "The checkpoint lost the combat ledger the table reads");
        SDL_Log("STATS_UI_DIAG: observer_checkpoint_bytes=%zu",runtime.size());
    }

    // Ordinary House serialization preserves the counters and income factor
    // used by Stats. Exclude players only to avoid registering cloned control
    // identities into this live game's player table.
    {
        auto players=std::move(pLocalHouse->players);
        OMemoryStream savedHouse;
        pLocalHouse->save(savedHouse);
        pLocalHouse->players=std::move(players);
        const auto oldVersion=game.loadedSavegameVersion;
        game.loadedSavegameVersion=SAVEGAMEVERSION;
        IMemoryStream stream(savedHouse.getData(),savedHouse.getDataLength());
        House restored(stream);
        game.loadedSavegameVersion=oldVersion;
        require(restored.getNumBuiltUnits()==pLocalHouse->getNumBuiltUnits()
                && restored.getHarvestedSpice()==pLocalHouse->getHarvestedSpice()
                && restored.getSpiceIncomeMultiplier()==5
                && restored.getCityTaxReceipts()==pLocalHouse->getCityTaxReceipts(),
                "Ordinary house save/load lost cumulative summary counters");
        require(restored.getCombatReward(Unit_Tank).kills==pLocalHouse->getCombatReward(Unit_Tank).kills
                && restored.getCombatReward(Unit_Tank).hpRemovedMilli==pLocalHouse->getCombatReward(Unit_Tank).hpRemovedMilli
                && restored.getNumLostItems(Unit_Tank)==pLocalHouse->getNumLostItems(Unit_Tank),
                "Ordinary house save/load lost the unit ledger");
        SDL_Log("STATS_UI_DIAG: raw_spice_5x=%d ordinary_house_bytes=%zu",
                (restored.getHarvestedSpice()/restored.getSpiceIncomeMultiplier()).lround(),
                savedHouse.getDataLength());
    }

    game.localObserver=false;
    SDL_Log("STATS_UI_PASS: sidebar \"Stats\" item fits the minimum interface height and opens a"
            " read-only window; the house summary is the engine's cumulative units produced, raw"
            " harvested spice, gross tax receipts and that house's own live population (N/A without"
            " a city economy); the unit table is the house's live Active count, the attacker-credited"
            " kills, this house's losses and real removed hit points in five measured columns that"
            " fit on every page and for every browsed house; Active follows the live item count on"
            " the bounded per-second sample and keeps an unbuildable owned type listed; the ambient"
            " aircraft, Rocket Trike and Elite Launcher stay out of the table despite real records"
            " and live units; QuantBot rows are exactly the"
            " performance scores and army-value shares of its last production pass, unallocated types"
            " get no goal, non-QuantBot houses keep their statistics; an observer browses every house"
            " and wraps in both windows without changing any identity; the observer's budget window"
            " withholds every adjustment and Apply and generates no command even when the callbacks"
            " are invoked; and the allocation snapshot survives an observer checkpoint");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=stats_ui outcome=passed");
}
