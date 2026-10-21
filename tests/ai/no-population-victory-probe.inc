// Real-engine fixture; only compiled by the diagnostic driver.
//
// There is no population/economic victory. Original Dune II scenarios such as
// SCENA008-010 and SCENF002-008 carry WinFlags=19/23, whose bit 0x10 means
// nothing in Dune II or upstream Dune Legacy; DuneCity once read it as "win at
// 500 city population" and ended those campaign missions within minutes. The
// mission must now run on with a city far above 500 population and be decided
// only by its normal conditions; vanilla (no city simulation) is unchanged.
void runNoPopulationVictoryProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("NO_POPULATION_VICTORY_FAIL: %s",why);throw std::runtime_error(why);}};
    require(game.gameType==GameType::Campaign,"Fixture must be a campaign mission");
    require((game.winFlags&0x10)!=0,"Fixture mission does not carry WinFlags bit 0x10 (pick a WinFlags=19/23 level)");
    require((game.winFlags&0x03)==0x03,"The mission's normal conquest conditions were not loaded");
    require(pLocalHouse!=nullptr,"Missing local house");
    auto* city=game.isCitySimEnabled() ? game.getCitySimulation() : nullptr;
    const int localHouse=pLocalHouse->getHouseID();
    int maxPop=0;
    for(int cycle=0;cycle<480 && !game.finished;++cycle) {
        if(city) {
            // Census phases recount from zones; every other cycle sees the forced
            // city at the point where the retired check used to fire.
            auto& state=city->getHouseStateMut(localHouse);
            state.resPop=400; state.comPop=200; state.indPop=200;
        }
        game.updateGameState();
        if(city) maxPop=std::max(maxPop,city->getTotalPop());
    }
    if(city) require(maxPop>500,"The forced city never stood above 500 population during the run");
    require(!game.finished && !game.won,"The mission was ended by city population");
    SDL_Log("NO_POPULATION_VICTORY_PROBE_PASS: mod=%s winFlags=%u cycles=%u max_pop=%d finished=%d",
        game.gameInitSettings.modName.c_str(),game.winFlags,game.gameCycleCount,maxPop,int(game.finished));
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=no-population-victory outcome=passed");
}
