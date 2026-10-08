// Growth-cap infrastructure on installments: a demanded Stadium or Airport and a
// planned Nuclear Plant must enter production without the price banked, be charged
// by the engine out of the credits that actually arrive, pause at zero and resume,
// never be ordered twice across yards, and never pre-empt blackout recovery.
//
// The reported 1.0.817 match (4P - 192x192 - New Beginning, seed 59770805) stalled
// because the opposite was required: residential population passed the stadium
// threshold at cycle ~76000, the demand valve was pinned to 0 for the remaining
// 170000 cycles, and the yard never held 3000 credits at the instant it was polled
// because the cheap-zone fallback spent the same credits every pass. Zero Stadium
// production orders were recorded in 247109 cycles.
//
// The civic need here is the structural one: the district is large enough that
// DuneCity::missingDemandCivics asks for the Stadium and the Airport on its own,
// and the clip mask is deliberately cleared before each civic case. Cases 4a-4e
// then measure priority against competing rules that are left fully enabled and
// whose own triggers are real, rather than disabled as 1.0.818's fixture did.
//
// Linked against the 1.0.818 QuantBot object the DEFAULT run fails in this order:
//   case 0  "A capped yard did not order the Stadium prerequisite it is missing"
//           - 818 erases the demand when the civic is unavailable instead of
//             walking the mod's prerequisite chain.
//   case 1  "A demanded Stadium did not enter production on a start budget"
//           - 818 reads the transient clip mask, which this case clears.
//   case 4b "A required civic lost the yard to the live competing investment rule"
//           - 818 evaluates the civic below protection coverage/saving, opening
//             tech and planned reactor capacity, so a live rule takes the yard.
//             Case 4a (optional protection) only runs when the district's crime
//             is below the dangerous band; on this fixture it is not, and the
//             case logs a loud SKIP with the measured crime instead.
//
// With BALANCE_CIVIC_PRIORITY_ONLY set, cases 0-3 and 4c-4e are skipped, the
// prerequisite is placed up front and the clip mask is SET while the real
// population stays over the threshold with the civic absent, no blackout and
// ample headroom. 818 then fails only on 4a and 4b - the priority ordering -
// with the derived-need and prerequisite fixes taken out of the picture.
#include <cstdlib>
#include <vector>
#include <structures/ZoneStructure.h>
#include <structures/ConstructionYard.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotSpendingPolicy.h>

void runGrowthInstallmentProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("GROWTH_INSTALLMENT_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot!=nullptr,"Growth installment fixture needs a QuantBot");
    require(game.isCitySimEnabled(),"Run the growth installment fixture with the Dune City mod");
    auto* sim=game.getCitySimulation();
    require(sim!=nullptr,"City simulation missing");
    const int houseID=static_cast<int>(house->getHouseID());
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;bot->difficulty=QuantBot::Difficulty::Brutal;
    game.gameCycleCount=MILLI2CYCLES(600000);
    require(!game.getGameInitSettings().getGameOptions().instantBuild,
        "Instant build would bypass the installment accounting under test");

    // A settlement with room for several 3x3 civic footprints: the developed rows
    // hold the base and its lots, the last rows stay bare rock so every civic case
    // below has somewhere legal to go.
    const int span=48;
    const int developedRows=34;
    Coord origin=Coord::Invalid();
    for(int y=currentGameMap->getSizeY()/3;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<span;++dy)for(int dx=0;dx<span;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Growth installment fixture needs empty settlement space");
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    auto put=[&](Uint32 item,int dx,int dy) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+dx,origin.y+dy,true,true);
        require(structure!=nullptr,"Growth installment fixture placement failed");
        return structure;
    };
    auto* yard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,2,2));
    auto* secondYard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,2,10));
    // Power surplus, a heavy factory and storage: the power, vehicle-production
    // bootstrap and silo rules that run before the civic rule all stay quiet, so
    // the civic decision is the one under test.
    // Enough generation that the city power rule, which runs before the civic
    // rule, has nothing to ask for while the civic cases are measured, and that
    // the developed commercial district below never browns out.
    for(int i=0;i<12;++i)put(Structure_WindTrap,4+2*i,2);
    for(int i=0;i<12;++i)put(Structure_WindTrap,4+2*i,4);
    // The spice-income ladder (four refineries, the port, its repair support) also
    // runs before the civic rule and legitimately claims the yard while it is
    // incomplete. Complete it in the fixture so the civic rule is what is measured.
    for(int i=0;i<4;++i)put(Structure_Refinery,4+5*i,8);
    put(Structure_HeavyFactory,26,8);
    put(Structure_StarPort,4,14);
    put(Structure_RepairYard,10,14);
    put(Structure_Silo,16,14);
    // Every Stadium prerequisite the mod declares EXCEPT the last one, which is
    // left out on purpose: the prerequisite-repair case below starts from a house
    // that genuinely cannot build the civic it is capped by, which is the state the
    // reported match's other AIs were in after their Radar was destroyed. Nothing
    // here names Radar - the chain is read out of ObjectData, so a mod that
    // changes the chain changes this fixture with it.
    const auto prerequisites=game.objectData.data[Structure_Stadium][houseID].prerequisiteStructuresSet;
    Uint32 withheldPrerequisite=NONE_ID;
    for(unsigned item=0;item<prerequisites.size();++item)
        if(prerequisites.test(item) && !house->getNumItems(item))withheldPrerequisite=item;
    require(withheldPrerequisite!=NONE_ID,
        "Fixture needs at least one Stadium prerequisite it does not already own");
    for(unsigned item=0;item<prerequisites.size();++item)
        if(prerequisites.test(item) && item!=withheldPrerequisite && !house->getNumItems(item))
            put(item,2,20);
    auto refreshYards=[&]() {
        for(auto* builder:{yard,secondYard}) {
            builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
            builder->upgrading=false;
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
    };
    refreshYards();
    require(!yard->isAvailableToBuild(Structure_Stadium),
        "Fixture must start without the withheld Stadium prerequisite");

    // A taxable city: developed residential lots on a road network, advanced by the
    // real simulation so the house state is its own.
    //
    // The sizes are the city simulation's own civic thresholds, not fixture taste:
    // DuneCity::missingDemandCivics asks a Stadium of a house once resPop passes
    // 500 and an Airport once comPop passes 100, and the planner derives its need
    // from exactly that function. One apartment lot normalizes to at most 40
    // people (ResidentialPopulation::normalize) and one commercial lot to 5 jobs
    // at its top density (getZonePopulation), so the district has to be this big
    // for the real demand the AI reads to exist at all.
    std::vector<ZoneStructure*> apartments;
    for(int i=0;i<16;++i)
        apartments.push_back(static_cast<ZoneStructure*>(put(Structure_ZoneResidential,4+2*i,26)));
    std::vector<StructureBase*> offices;
    for(int row=0;row<2;++row)for(int i=0;i<12;++i)
        offices.push_back(put(Structure_ZoneCommercial,4+2*i,30+2*row));
    for(int y=origin.y;y<origin.y+developedRows;++y)for(int x=origin.x;x<origin.x+span;++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);
    // One road edge so the bare civic land is reachable, then leave it clear.
    for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,origin.y+developedRows)->setRoad(true);
    // CitySimulation::advancePhase only runs its effects scan - the pass that
    // recounts each house's resPop/comPop from the zones - when a new city day
    // has started, so the clock has to move with it. Calling it repeatedly on one
    // cycle scans once and leaves the populations stale.
    auto advanceCityDay=[&]() {
        game.gameCycleCount+=DuneCity::kCyclesPerCityDay+1;
        sim->advancePhase(game.getGameCycleCount());
    };
    for(int i=0;i<16;++i)advanceCityDay();
    // Settle the district at its top density, then let one real day recount the
    // house populations from the zones themselves. Nothing writes resPop/comPop
    // directly; the growth phase this schedules runs on the next settle, which
    // re-pins the district before it.
    auto setDistrictPopulated=[&](bool populated) {
        for(auto* lot:apartments)lot->setResidentialPopulation(populated?40:0);
        for(auto* office:offices) {
            const auto at=office->getLocation();
            for(int dy=0;dy<office->getStructureSizeY();++dy)
                for(int dx=0;dx<office->getStructureSizeX();++dx)
                    if(auto* t=currentGameMap->getTile(at.x+dx,at.y+dy))
                        t->setCityZoneDensity(populated?3:0);
            static_cast<ZoneStructure*>(office)->refreshZonePowerDraw();
        }
        advanceCityDay();
    };
    auto settleDistrict=[&]() { setDistrictPopulated(true); };
    settleDistrict();
    SDL_Log("GROWTH_INSTALLMENT: district res_pop=%d com_pop=%d power=%d/%d",
        sim->getHouseState(houseID).resPop,sim->getHouseState(houseID).comPop,
        house->getProducedPower(),house->getPowerRequirement());
    require(sim->getHouseState(houseID).resPop>500,
        "Fixture district must pass the simulation's own Stadium population threshold");
    require(sim->getHouseState(houseID).comPop>100,
        "Fixture district must pass the simulation's own Airport population threshold");
    require(house->hasPower(),"Fixture district must not brown out");
    // The reported city is spice-free and lives on tax, like this fixture. It also
    // keeps the opening worker reserve out of the measurement: that reserve is a
    // pre-existing guard this change must not weaken, so the fixture satisfies it
    // instead of competing with it.
    bot->lastCalculatedSpice=0;
    // One transport already exists, so the first-transport reserve - another
    // pre-existing guard - is satisfied rather than competing with the measurement.
    if(auto* lift=house->createUnit(Unit_Carryall,true))
        lift->deploy(Coord(origin.x+span-2,origin.y+developedRows-2));

    // The same four-minute forecast the capital planner computes, from the engine's
    // own tax tables. Every scenario below is deliberately outside it.
    auto fourMinuteForecast=[&]() {
        const auto& hs=sim->getHouseState(houseID);
        const int annualTax=DuneCity::computeAnnualTaxRevenue(hs.taxBaseEighths,sim->getCityTax(),hs.avgLandValue);
        const int upkeep=house->getPowerRequirement()/8+hs.lastPoliceExpense;
        return (annualTax-upkeep)*QuantBotSpendingPolicy::horizonMinutes;
    };
    // Pin a small, steady tax base: the point under test is the funding rule, not
    // the growth model. The base is walked up until the forecast first reaches the
    // declared target, so the fixture states its income instead of guessing a
    // table constant that a mod change could move.
    auto pinTaxBase=[&](int targetForecast) {
        auto& hs=sim->getHouseStateMut(houseID);
        hs.avgLandValue=120;hs.lastPoliceExpense=0;hs.taxBaseEighths=0;
        for(int base=8;base<=40000;base+=8) {
            hs.taxBaseEighths=base;
            if(fourMinuteForecast()>=targetForecast)break;
        }
    };
    // ~100 credits per minute of bankable tax: small enough that every one of the
    // three projects below costs more than cash plus the whole four-minute forecast.
    const int targetForecast=100*QuantBotSpendingPolicy::horizonMinutes;
    pinTaxBase(targetForecast);

    auto setCredits=[&](int amount) {
        house->takeCredits(FixPoint(house->getCredits()+1));
        if(amount>0)house->addCredits(FixPoint(amount),false);
    };
    auto queuedAnywhere=[&](Uint32 item) {
        int count=0;
        for(auto* structure:structureList) {
            if(structure->getOwner()!=house || !structure->isABuilder()) continue;
            auto* builder=static_cast<BuilderBase*>(structure);
            // BuildItem::num is the queued count for that item on this builder,
            // the same field queuedProductionCost accounts against.
            for(const auto& order:builder->getBuildList())
                if(order.itemID==item)count+=order.num;
        }
        return count;
    };
    auto queuedOn=[&](const BuilderBase* builder,Uint32 item) {
        int count=0;
        for(const auto& order:builder->getBuildList())
            if(order.itemID==item)count+=order.num;
        return count;
    };
    auto dropQueues=[&]() {
        for(auto* structure:structureList) {
            if(structure->getOwner()!=house || !structure->isABuilder()) continue;
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
    };
    auto setItemEnabled=[&](Uint32 item,bool on) {
        game.objectData.data[item][houseID].enabled=on;
        for(auto* builder:{yard,secondYard})builder->updateBuildList();
    };
    // Highest crime on any of this house's own city-role buildings. The service
    // evaluation calls >=192 dangerous and >=128 moderate, so this is how the
    // competing-protection case below proves it is an OPTIONAL coverage need and
    // not the dangerous-crime emergency that legitimately outranks a civic.
    auto peakOwnCrime=[&]() {
        int peak=0;
        for(const auto* structure:structureList) {
            if(structure->getOwner()!=house || structure->getHealth()<=0
                || DuneCity::getStructureCityRole(structure->getItemID())==DuneCity::CityRole::None)
                continue;
            const auto at=structure->getLocation();
            peak=std::max(peak,int(sim->getCrimeRateMap().worldGet(at.x,at.y)));
        }
        return peak;
    };
    // A district over the civic population thresholds generates real crime, and
    // within a few city days some zone crosses the >=192 dangerous band. That is a
    // genuine emergency which legitimately outranks a civic, so it decides whether
    // the optional-protection competing case below can run at all. Measured here,
    // once, and reported rather than assumed. Measured on this fixture: 8 police
    // stations (coverage 23) only take a 300 raw crime rate down to 250, because
    // the band is driven by population density against a land value of ~1 at the
    // district edge - neither of which a fixture can set.
    {
        int worst=-1;Coord worstAt=Coord::Invalid();Uint32 worstItem=NONE_ID;
        for(const auto* structure:structureList) {
            if(structure->getOwner()!=house || structure->getHealth()<=0
                || DuneCity::getStructureCityRole(structure->getItemID())==DuneCity::CityRole::None)
                continue;
            const auto at=structure->getLocation();
            const int crime=int(sim->getCrimeRateMap().worldGet(at.x,at.y));
            if(crime>worst) {worst=crime;worstAt=at;worstItem=structure->getItemID();}
        }
        SDL_Log("GROWTH_INSTALLMENT: district crime peak=%d at item=%u (%d,%d) "
                "before_police=%d coverage=%d land_value=%d density=%d stations=%d",
            worst,worstItem,worstAt.x,worstAt.y,
            worstAt.isValid()?int(sim->getCrimeBeforePoliceMap().worldGet(worstAt.x,worstAt.y)):-1,
            worstAt.isValid()?int(sim->getPoliceCoverageMap().worldGet(worstAt.x,worstAt.y)):-1,
            worstAt.isValid()?int(sim->getLandValueMap().worldGet(worstAt.x,worstAt.y)):-1,
            worstAt.isValid()?int(sim->getPopulationDensityMap().worldGet(worstAt.x,worstAt.y)):-1,
            house->getNumItems(Structure_PoliceStation));
    }
    // Charge the house exactly the way an ordinary game cycle does, and account for
    // every credit: progress may only ever grow by what the house actually paid.
    struct Charged { int paid=0; int progressGain=0; };
    auto tick=[&](BuilderBase* builder) {
        const int before=builder->getProductionProgress().lround();
        const int cash=house->getCredits();
        builder->updateProductionProgress();
        Charged result;
        result.paid=cash-house->getCredits();
        result.progressGain=builder->getProductionProgress().lround()-before;
        return result;
    };

    // Ordinary paid progress, a pause at an empty till, a resume, and completion
    // within a declared bound. The bound assumes income keeps arriving at
    // 'incomePerCycle'; it is the price divided by that, plus the nominal build
    // time and slack. This is a bounded progress check, not a liveness claim: with
    // no income at all the engine correctly makes no progress.
    const int incomePerCycle=2;
    // Scarce, but above the pre-existing reserves this change must not weaken: a
    // settled city keeps an MCV's worth of cash for expansion and a transport, and
    // a civic has to start from what is genuinely spare rather than by raiding them.
    const int scarce=1200;
    auto runToCompletion=[&](BuilderBase* builder,Uint32 item,int price,const char* label) {
        require(builder->getCurrentProducedItem()==item,"Lost the order before the progress check");
        int totalPaid=0;
        for(int i=0;i<20 && !builder->isWaitingToPlace();++i) {
            const auto charged=tick(builder);
            require(charged.paid>=0,"A cycle refunded credits while building");
            require(charged.progressGain-charged.paid<=1,
                "Progress exceeded the credits the house actually paid");
            totalPaid+=charged.paid;
        }
        require(totalPaid>0,"A funded growth-cap project made no paid progress");
        require(builder->getProductionProgress().lround()<price,
            "Fixture completed the project before the pause/resume checks");
        setCredits(0);
        const int pausedAt=builder->getProductionProgress().lround();
        for(int i=0;i<20;++i) {
            const auto charged=tick(builder);
            require(charged.paid==0 && charged.progressGain==0,"An empty till still funded progress");
        }
        require(builder->getProductionProgress().lround()==pausedAt,"Progress moved with no credits");
        // A single tick charges a FixPoint fraction of a credit on a long build, so
        // resumption is measured over a few ticks rather than one rounded sample.
        setCredits(200);
        int resumedPaid=0, resumedProgress=0;
        for(int i=0;i<10;++i) {
            const auto charged=tick(builder);
            resumedPaid+=charged.paid;resumedProgress+=charged.progressGain;
        }
        require(resumedProgress>0 && resumedPaid>0,"Progress did not resume when income arrived");

        const int bound=price/incomePerCycle+15*game.objectData.data[item][houseID].buildtime+1000;
        int cycles=0, granted=0;
        while(!builder->isWaitingToPlace() && cycles<bound) {
            house->addCredits(FixPoint(incomePerCycle),false);granted+=incomePerCycle;
            tick(builder);++cycles;
        }
        SDL_Log("GROWTH_INSTALLMENT: %s completed after %d cycles (bound %d), granted %d, progress %d/%d",
            label,cycles,bound,granted,builder->getProductionProgress().lround(),price);
        require(builder->isWaitingToPlace(),"A project funded by continuing income never completed");
        require(granted>=price-scarce-400,"The project completed without being paid for");
    };

    const int stadiumPrice=game.objectData.data[Structure_Stadium][houseID].price;
    const int airportPrice=game.objectData.data[Structure_Airport][houseID].price;
    const int reactorPrice=game.objectData.data[Structure_NuclearPlant][houseID].price;

    // Pay an accepted order through the engine and place what it produces into the
    // AI's own reserved sites, exactly as a running game does: queued foundations
    // first, then the building. Positive continuing income is an explicit
    // assumption of this fixture and not free or instant production - the engine
    // makes no progress at all at an empty till, which cases 1 to 3 measure.
    auto fundAndPlace=[&](BuilderBase* builder,Uint32 item,const char* label) {
        const int bound=item==NONE_ID ? 0
            : game.objectData.data[item][houseID].price/incomePerCycle
              +15*game.objectData.data[item][houseID].buildtime+2000;
        int cycles=0, placements=0, granted=0;
        bool placedItem=false;
        while(cycles<bound && builder->getProductionQueueSize()>0 && !placedItem) {
            if(builder->isWaitingToPlace()) {
                const auto produced=builder->getCurrentProducedItem();
                auto& locations=bot->builderPlaceLocations[builder->getObjectID()];
                require(!locations.empty(),"Missing the AI's reserved site for a paid order");
                const auto location=locations.front();
                require(bot->doPlaceStructure(static_cast<ConstructionYard*>(builder),
                        location.x,location.y),
                    "A paid order could not be placed at the AI's reserved site");
                locations.pop_front();++placements;
                placedItem=produced==item;
            } else {
                house->addCredits(FixPoint(incomePerCycle),false);granted+=incomePerCycle;
                const auto charged=tick(builder);
                require(charged.progressGain-charged.paid<=1,
                    "An order progressed beyond the credits the house actually paid");
            }
            ++cycles;
        }
        SDL_Log("GROWTH_INSTALLMENT: %s paid over %d cycles (bound %d), granted %d, "
                "placements %d, owned=%d",
            label,cycles,bound,granted,placements,house->getNumItems(item));
        require(placements>0,"The paid order never reached a real placement");
        require(house->getNumItems(item)>0,"The AI's own order never completed and placed");
    };

    // Diagnostic-only isolation mode, read from the environment and nowhere in
    // shipping policy. Codex gates it from the owned driver. It prepares the same
    // district and prerequisites, skips the prerequisite and funding cases, and
    // runs only the priority cases with the clip mask SET and the population over
    // the real threshold - so linking the 1.0.818 QuantBot object fails on
    // PRIORITY alone rather than on the separate derived-need/prerequisite fixes.
    // The default run still exercises the cleared-mask and negative-valve cases.
    const bool priorityOnly=std::getenv("BALANCE_CIVIC_PRIORITY_ONLY")!=nullptr;

    auto runPrerequisiteAndFundingCases=[&]() {
    // ---- 0. Prerequisite repair. The district is over the Stadium threshold and
    // the yard cannot build a Stadium at all, which is where the reported match's
    // other capped AIs were. The yard must buy the missing prerequisite rather
    // than treat the cap as absent demand, must not order the civic it cannot
    // build, and a second yard must not duplicate the prerequisite.
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=0;
    SDL_Log("GROWTH_INSTALLMENT: prerequisite case withheld=%u available=%d cash=%d",
        withheldPrerequisite,yard->isAvailableToBuild(Structure_Stadium)?1:0,house->getCredits());
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(withheldPrerequisite)==1,
        "A capped yard did not order the Stadium prerequisite it is missing");
    require(queuedAnywhere(Structure_Stadium)==0,
        "A yard ordered a Stadium it cannot build");
    bot->planningBuilder=secondYard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(withheldPrerequisite)==1,
        "A second yard duplicated the queued Stadium prerequisite");
    require(queuedAnywhere(Structure_Stadium)==0,
        "A second yard ordered the Stadium while its prerequisite was still queued");

    // ---- 0b. The AI's own prerequisite order is paid and placed through the
    // engine, into the site the planner reserved for it - nothing is handed to the
    // house. The second yard must still not duplicate it while it is in flight,
    // and the same planner must then reach the civic with no manual placement.
    BuilderBase* prerequisiteYard=nullptr;
    for(auto* builder:{yard,secondYard})
        for(const auto& order:builder->getBuildList())
            if(order.itemID==withheldPrerequisite && order.num>0) prerequisiteYard=builder;
    require(prerequisiteYard!=nullptr,"Lost the prerequisite order");
    fundAndPlace(prerequisiteYard,withheldPrerequisite,"stadium prerequisite");
    require(queuedAnywhere(withheldPrerequisite)==0,
        "A duplicate prerequisite survived its own completion");
    for(auto* builder:{yard,secondYard})builder->updateBuildList();
    require(yard->isAvailableToBuild(Structure_Stadium),
        "The AI's own completed prerequisite did not unlock the Stadium");
    require(yard->isAvailableToBuild(Structure_Airport),"Fixture yard cannot build an Airport");
    require(yard->isAvailableToBuild(Structure_NuclearPlant),"Fixture yard cannot build a reactor");
    // Nothing was handed to the house and nothing is cancelled and replaced: the
    // Stadium is now buildable only because the AI's own order was paid for and
    // placed. Case 1 immediately below is therefore the proof that the same
    // planner reaches the civic off a prerequisite it built itself, and case 2
    // that no second yard duplicates it.
    SDL_Log("GROWTH_INSTALLMENT: real prerequisite stands res_pop=%d stadium_available=%d "
            "earned=%d cash=%d",
        sim->getHouseState(houseID).resPop,yard->isAvailableToBuild(Structure_Stadium)?1:0,
        house->getEarnedCredits().lround(),house->getCredits());
    dropQueues();
    pinTaxBase(targetForecast);

    // ---- 1. A demanded Stadium starts on scarce cash, well beyond the forecast.
    // The clip mask is cleared on purpose: the need is the structural one the
    // district itself creates, and the planner must act on it during the phases
    // where the raw valve is not being pinned at this instant.
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=0;
    const int forecast=fourMinuteForecast();
    SDL_Log("GROWTH_INSTALLMENT: stadium price=%d cash=%d four_minute_forecast=%d",
        stadiumPrice,scarce,forecast);
    require(forecast>0,"Fixture city must have positive bankable income");
    require(stadiumPrice>scarce+forecast,
        "Fixture must put the Stadium beyond cash plus the four-minute forecast");
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_Stadium)==1,
        "A demanded Stadium did not enter production on a start budget");
    require(house->getCredits()>0,"Ordering the Stadium must not consume the whole till");

    // ---- 2. No duplicate commitment across yards.
    bot->planningBuilder=secondYard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_Stadium)==1,"A second yard ordered a duplicate Stadium");

    // ---- 3. Ordinary cycles spend only the credits that exist, and pause at zero.
    BuilderBase* stadiumYard=yard->getCurrentProducedItem()==Structure_Stadium ? yard : secondYard;
    require(stadiumYard->getCurrentProducedItem()==Structure_Stadium,"Lost the Stadium order");
    int totalPaid=0;
    for(int i=0;i<20;++i) {
        const auto charged=tick(stadiumYard);
        require(charged.paid>=0,"A cycle refunded credits while building");
        require(charged.progressGain==charged.paid || charged.progressGain-charged.paid<=1,
            "Progress exceeded the credits the house actually paid");
        totalPaid+=charged.paid;
    }
    require(totalPaid>0,"A funded Stadium made no paid progress");
    runToCompletion(stadiumYard,Structure_Stadium,stadiumPrice,"stadium");
    };

    if(priorityOnly) {
        // The prerequisite chain and the funding ladder are measured by the
        // default run; this mode isolates priority, so the prerequisite is simply
        // placed and the district settled.
        put(withheldPrerequisite,32,8);
        for(auto* builder:{yard,secondYard})builder->updateBuildList();
        require(yard->isAvailableToBuild(Structure_Stadium),
            "Priority-only mode still needs a buildable Stadium");
        settleDistrict();
        pinTaxBase(targetForecast);
    } else {
        runPrerequisiteAndFundingCases();
    }

    // The funding cases above left both yards with the protection items disabled,
    // which is how 1.0.818's fixture measured funding. The priority cases below
    // restore them where the competing rule under test needs them.
    setItemEnabled(Structure_RocketTurret,false);
    setItemEnabled(Structure_PoliceStation,false);

    // Enough headroom for the civic's own power - a civic ordered into a deficit
    // would brown out the zones it exists to unlock - and still far short of this
    // district's own power reserve, so the planned-reactor investment stays live.
    // The coverage rule additionally wants an emplacement power buffer, so the
    // protection case asks for more; both stay under the reserve.
    const int civicHeadroom=40;
    const int coverageHeadroom=150;
    // Cash that comfortably buys either competing project outright, so these
    // cases measure priority and not affordability - affordability is cases 1-3.
    const int ample=reactorPrice+2000;
    // In the isolation mode the clip mask is set, so 1.0.818 reads a demanded
    // civic and can only fail on where that civic sits in the rule chain.
    const uint8_t measuredMask=priorityOnly ? uint8_t(DuneCity::NeedStadium) : uint8_t(0);

    auto preparePriorityPass=[&](bool windAvailable,int headroom) {
        dropQueues();
        setItemEnabled(Structure_WindTrap,windAvailable);
        settleDistrict();
        pinTaxBase(targetForecast);
        house->powerRequirement=house->getProducedPower()-headroom;
        setCredits(ample);
        sim->getHouseStateMut(houseID).civicDemandBlocked=measuredMask;
        bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    };
    // ---- 4a. Priority against a live OPTIONAL PROTECTION rule - the class that
    // took the yard on most captured passes. Rocket turrets and police stations
    // are restored as real buildable items, the district is powered and occupied,
    // and the house owns no emplacement at all, so the proactive core-coverage
    // rule has a genuine shortfall to answer.
    //
    // It must be optional protection and not the dangerous-crime emergency, which
    // keeps its urgency by design and is checked by source review only. Whether
    // that is reachable is a property of the crime model, not of this fixture, so
    // it is measured rather than assumed: see the district crime log above. The
    // whole case is skipped - loudly - when the district is in the dangerous band,
    // because there the civic correctly yields and there is nothing to measure.
    const int protectionCrime=peakOwnCrime();
    const bool optionalProtectionReachable=protectionCrime<192;
    if(!optionalProtectionReachable) {
        SDL_Log("GROWTH_INSTALLMENT: OPTIONAL-PROTECTION CASE SKIPPED - district crime %d "
                "is in the dangerous band (>=192), where protection is a real emergency "
                "and the civic correctly yields. Crime here is driven by population "
                "density against low land value, neither of which this fixture can set.",
            protectionCrime);
    } else {
        // The only variable between a control and its measurement is whether the
        // mod offers the civic at all. District, cash, power reserve and the
        // competing rule itself are identical, and a mod without a Stadium is a
        // real configuration rather than a switch on the branch under test.
        setItemEnabled(Structure_RocketTurret,true);
        setItemEnabled(Structure_PoliceStation,true);
        setItemEnabled(Structure_Stadium,false);
        setItemEnabled(Structure_Airport,false);
        preparePriorityPass(true,coverageHeadroom);
        SDL_Log("GROWTH_INSTALLMENT: service control res_pop=%d com_pop=%d peak_crime=%d "
                "turrets=%d headroom=%d cash=%d",
            sim->getHouseState(houseID).resPop,sim->getHouseState(houseID).comPop,
            peakOwnCrime(),house->getNumItems(Structure_RocketTurret),
            coverageHeadroom,house->getCredits());
        require(peakOwnCrime()<192,
            "Competing-protection case must stay an optional coverage need, not an emergency");
        require(house->hasPower(),"Competing-protection case must not be a blackout");
        bot->build(0);
        SDL_Log("GROWTH_INSTALLMENT: service control yard queued turret=%d police=%d reactor=%d zone=%d",
            queuedOn(yard,Structure_RocketTurret),queuedOn(yard,Structure_PoliceStation),
            queuedOn(yard,Structure_NuclearPlant),queuedOn(yard,Structure_ZoneResidential));
        require(queuedOn(yard,Structure_RocketTurret)+queuedOn(yard,Structure_PoliceStation)>=1,
            "Fixture must leave a live optional protection rule that claims this yard");

        setItemEnabled(Structure_Stadium,true);
        setItemEnabled(Structure_Airport,true);
        preparePriorityPass(true,coverageHeadroom);
        require(sim->getHouseState(houseID).resPop>500 && !house->getNumItems(Structure_Stadium),
            "Competing-protection measurement needs the residential cap unanswered");
        int servicePasses=0;
        while(servicePasses<2 && queuedOn(yard,Structure_Stadium)==0) {
            ++servicePasses;
            bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
            bot->build(0);
        }
        SDL_Log("GROWTH_INSTALLMENT: civic beat optional protection after %d pass(es) "
                "turret=%d police=%d mask=%u",
            servicePasses,queuedOn(yard,Structure_RocketTurret),
            queuedOn(yard,Structure_PoliceStation),unsigned(measuredMask));
        require(queuedOn(yard,Structure_Stadium)==1,
            "A required civic lost the yard to an optional protection rule");
        require(queuedOn(yard,Structure_RocketTurret)+queuedOn(yard,Structure_PoliceStation)==0,
            "Optional protection coverage pre-empted a required civic");

        // Restore the controls the reactor and funding cases rely on.
        dropQueues();
        setItemEnabled(Structure_RocketTurret,false);
        setItemEnabled(Structure_PoliceStation,false);
    }

    // ---- 4b. Priority against whatever live investment rule the chain actually
    // wants on this state: the house is powered but has no power reserve, the
    // cheap generator is unavailable and its production core is incomplete, so
    // planned capacity and opening technology are both genuinely due. Every
    // competing rule is left enabled and none is named by the fixture - the
    // control pass records which project the chain chooses, and the measurement
    // requires the civic to beat exactly that project. On the shipped tree the
    // control choice is recorded below: it can be planned generation or opening
    // technology, both classes that took the yard on the captured passes.
    setItemEnabled(Structure_Stadium,false);
    setItemEnabled(Structure_Airport,false);
    preparePriorityPass(false,civicHeadroom);
    SDL_Log("GROWTH_INSTALLMENT: competing control res_pop=%d com_pop=%d headroom=%d cash=%d",
        sim->getHouseState(houseID).resPop,sim->getHouseState(houseID).comPop,
        civicHeadroom,house->getCredits());
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: competing control yard queued reactor=%d windtrap=%d refinery=%d zone=%d "
            "queue=%u produced=%u upgrading=%d hold=%d reactor_site=%d reactor_available=%d "
            "second_queue=%u cash=%d",
        queuedOn(yard,Structure_NuclearPlant),queuedOn(yard,Structure_WindTrap),
        queuedOn(yard,Structure_Refinery),queuedOn(yard,Structure_ZoneResidential),
        yard->getProductionQueueSize(),yard->getCurrentProducedItem(),
        yard->isUpgrading()?1:0,yard->isOnHold()?1:0,
        bot->findPlaceLocation(Structure_NuclearPlant).isValid()?1:0,
        yard->isAvailableToBuild(Structure_NuclearPlant)?1:0,
        secondYard->getProductionQueueSize(),house->getCredits());
    // Whatever the chain actually wants here is the competing project: the rule is
    // not named by the fixture, it is observed. On this state it is the opening
    // technology / planned capacity class - the same class that took the yard on
    // the captured passes - and the measurement below has to beat exactly it.
    // The building, not the concrete the planner queues ahead of it: foundations
    // are shared bookkeeping and would make this assertion accidental.
    Uint32 controlChoice=NONE_ID;
    for(const auto& order:yard->getBuildList()) {
        if(order.num<=0 || order.itemID==Structure_Slab1 || order.itemID==Structure_Slab4
            || order.itemID==Structure_Road) continue;
        controlChoice=order.itemID;break;
    }
    require(yard->getProductionQueueSize()>0 && controlChoice!=Uint32(NONE_ID),
        "Fixture must leave a live competing rule that claims this yard");
    require(queuedOn(yard,Structure_Stadium)+queuedOn(yard,Structure_Airport)==0,
        "Control pass must not order a civic the mod has disabled");

    setItemEnabled(Structure_Stadium,true);
    setItemEnabled(Structure_Airport,true);
    preparePriorityPass(false,civicHeadroom);
    require(sim->getHouseState(houseID).resPop>500,"Measurement pass needs the residential cap back");
    // Declared bound: the yard must act on an unanswered cap within two of its
    // own planning passes, not eventually.
    int civicPasses=0;
    while(civicPasses<2 && queuedOn(yard,Structure_Stadium)==0) {
        ++civicPasses;
        bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
        bot->build(0);
    }
    SDL_Log("GROWTH_INSTALLMENT: civic claimed the yard after %d pass(es), "
            "competing item=%u still queued=%d mask=%u",
        civicPasses,controlChoice,queuedOn(yard,controlChoice),unsigned(measuredMask));
    require(queuedOn(yard,Structure_Stadium)==1,
        "A required civic lost the yard to the live competing investment rule");
    require(queuedOn(yard,controlChoice)==0,
        "The competing investment the control chose pre-empted a required civic");

    if(priorityOnly) {
        // Isolation mode stops here: the clip mask was set and the population was
        // over the real threshold throughout, so a failure above is a priority
        // failure and nothing else.
        dropQueues();
        setItemEnabled(Structure_WindTrap,true);
        SDL_Log("GROWTH_INSTALLMENT_OK");
        SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=civic_priority outcome=passed");
        return;
    }

    // ---- 4c. The clip mask is not the need. civicDemandBlocked only marks a city
    // update where a positive valve was pinned; between those updates the raw
    // valve is negative and the flag is clear, while the population threshold is
    // still exceeded and the civic is still absent. Priority must not flicker.
    dropQueues();
    {
        auto& hs=sim->getHouseStateMut(houseID);
        hs.civicDemandBlocked=0;
        hs.resValve=-1500;hs.comValve=-1200;hs.indValve=-1200;
    }
    setCredits(scarce);
    require(sim->getHouseState(houseID).resPop>500 && !house->getNumItems(Structure_Stadium),
        "Negative-valve case needs the structural threshold still exceeded");
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: negative-valve phase res_valve=%d clip=%u res_pop=%d stadium=%d",
        sim->getHouseState(houseID).resValve,
        unsigned(sim->getHouseState(houseID).civicDemandBlocked),
        sim->getHouseState(houseID).resPop,queuedAnywhere(Structure_Stadium));
    require(queuedAnywhere(Structure_Stadium)==1,
        "A negative demand valve above the structural threshold dropped the civic's priority");

    // ---- 4d. Observed enemy aircraft keep their urgency. An aircraft actually
    // over our own buildings is a present loss, and the civic yields that pass.
    dropQueues();
    settleDistrict();
    pinTaxBase(targetForecast);
    house->powerRequirement=house->getProducedPower()-civicHeadroom;
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=0;
    auto* hostile=game.getHouse(HOUSE_ATREIDES);
    require(hostile!=nullptr && hostile->getTeamID()!=house->getTeamID(),
        "Fixture needs a hostile house for the observed-aircraft case");
    auto* raider=hostile->createUnit(Unit_Ornithopter,true);
    require(raider!=nullptr,"Observed-aircraft case could not create the attacker");
    raider->deploy(Coord(origin.x+4,origin.y+4));
    raider->setVisible(house->getTeamID(),true);
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: aircraft over the base stadium=%d",queuedAnywhere(Structure_Stadium));
    require(queuedAnywhere(Structure_Stadium)==0,
        "A required civic pre-empted aircraft already over the base");
    // With the attacker gone the same pass buys the civic again.
    raider->setHealth(0);
    dropQueues();
    setCredits(scarce);
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: aircraft gone stadium=%d",queuedAnywhere(Structure_Stadium));
    require(queuedAnywhere(Structure_Stadium)==1,
        "The civic did not resume once the observed aircraft was gone");

    // ---- 4e. An actual blackout still outranks the civic: the city the Stadium
    // is meant to unlock cannot develop without power at all.
    dropQueues();
    setItemEnabled(Structure_WindTrap,true);
    const int poweredAt=house->getProducedPower();
    house->powerRequirement=poweredAt+1;
    setCredits(scarce);
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: blackout recovery windtrap=%d stadium=%d powered=%d",
        queuedAnywhere(Structure_WindTrap),queuedAnywhere(Structure_Stadium),house->hasPower()?1:0);
    require(queuedAnywhere(Structure_WindTrap)>=1,"A blackout behind a capped city could not recover");
    require(queuedAnywhere(Structure_Stadium)==0,"A required civic pre-empted blackout recovery");
    dropQueues();
    // Generous headroom again for the Airport's own 40 power in the cases below.
    house->powerRequirement=poweredAt-200;

    // ---- 5. The same rule for a demanded Airport.
    while(yard->getProductionQueueSize())yard->doCancelItem(yard->getCurrentProducedItem());
    while(secondYard->getProductionQueueSize())secondYard->doCancelItem(secondYard->getCurrentProducedItem());
    put(Structure_Stadium,2,38); // The stadium now exists; the next cap is commercial.
    settleDistrict();
    pinTaxBase(targetForecast);
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=0;
    require(sim->getHouseState(houseID).comPop>100 && house->getNumItems(Structure_Stadium)>0,
        "Airport case needs the commercial threshold passed and the residential cap lifted");
    require(airportPrice>scarce+fourMinuteForecast(),
        "Fixture must put the Airport beyond cash plus the four-minute forecast");
    bot->clearPlacementCache();
    for(auto* builder:{yard,secondYard})
        SDL_Log("GROWTH_INSTALLMENT: airport gate builder=%u available=%d site=%d queue=%d waiting=%d "
                "blocked=%u have_airport=%d have_stadium=%d",
            builder->getObjectID(),builder->isAvailableToBuild(Structure_Airport)?1:0,
            bot->findPlaceLocation(Structure_Airport).isValid()?1:0,
            builder->getProductionQueueSize(),builder->isWaitingToPlace()?1:0,
            unsigned(sim->getHouseState(houseID).civicDemandBlocked),
            house->getNumItems(Structure_Airport),house->getNumItems(Structure_Stadium));
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_Airport)==1,
        "A demanded Airport did not enter production on a start budget");
    bot->planningBuilder=secondYard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_Airport)==1,"A second yard ordered a duplicate Airport");

    // ---- 6. The Airport is charged the same way: paid progress, a pause at zero
    // and completion once income resumes.
    BuilderBase* airportYard=yard->getCurrentProducedItem()==Structure_Airport ? yard : secondYard;
    require(airportYard->getCurrentProducedItem()==Structure_Airport,"Lost the Airport order");
    runToCompletion(airportYard,Structure_Airport,airportPrice,"airport");

    auto clearQueues=[&]() {
        for(auto* structure:structureList) {
            if(structure->getOwner()!=house || !structure->isABuilder()) continue;
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
    };

    // ---- 7. A planned reactor on installments: powered now, but wind cannot serve
    // this city's demand, so the reactor is the project the plan wants. Measured
    // before any generator has ever been queued, so the one-generator-in-flight
    // rule is not already satisfied by something else.
    clearQueues();
    // Both civics now exist, so there is no structural cap left for the civic rule
    // above the reactor to answer. The AI's own Airport finished waiting-to-place
    // and was just cancelled with the queues, so place one outright.
    put(Structure_Airport,8,38);
    settleDistrict();
    pinTaxBase(targetForecast);
    {
        // No civic cap and no zone demand: with nothing cheaper worth building, the
        // reactor is the project the growth plan wants. Zone demand would otherwise
        // claim the yard first through the protected growth plot, which is correct
        // behaviour and not what this case measures.
        auto& hs=sim->getHouseStateMut(houseID);
        hs.civicDemandBlocked=0;
        hs.resValve=hs.comValve=hs.indValve=-1000;
    }
    const int produced=house->getProducedPower();
    const int windtrapPrice=game.objectData.data[Structure_WindTrap][houseID].price;
    require(scarce>=windtrapPrice,"Fixture needs enough cash for the cheap generator");
    setItemEnabled(Structure_WindTrap,false);
    house->powerRequirement=produced-1; // powered, but no growth headroom at all
    setCredits(scarce);
    require(reactorPrice>scarce+fourMinuteForecast(),
        "Fixture must put the reactor beyond cash plus the four-minute forecast");
    setCredits(scarce);
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    SDL_Log("GROWTH_INSTALLMENT: reactor AI selection queued=%d cash=%d forecast=%d",
        queuedAnywhere(Structure_NuclearPlant),house->getCredits(),fourMinuteForecast());
    require(queuedAnywhere(Structure_NuclearPlant)==1,"The AI did not select a needed reactor below full price");
    bot->planningBuilder=secondYard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_NuclearPlant)==1,"A second yard ordered a duplicate reactor");

    // ---- 8. A later blackout, with that underfunded reactor still queued, must
    // still recover immediately. Before the fix the pending reactor satisfied the
    // one-generator-in-flight rule, so no cheap generator could be ordered while
    // the blackout destroyed the income the reactor was being charged against; the
    // acceptance budget would also have refused its concrete.
    BuilderBase* reactorYard=nullptr;
    for(auto* builder:{yard,secondYard})
        for(const auto& order:builder->getBuildList())
            if(order.itemID==Structure_NuclearPlant && order.num>0) reactorYard=builder;
    require(reactorYard!=nullptr,"Lost the reactor order");
    // The AI queued real foundations ahead of the reactor. Pay and place them
    // through the engine before measuring the building's installments.
    int foundationTicks=0, foundationsPlaced=0;
    while(reactorYard->getCurrentProducedItem()!=Structure_NuclearPlant && foundationTicks<6000) {
        const auto current=reactorYard->getCurrentProducedItem();
        require(current==Structure_Slab1 || current==Structure_Slab4,
            "Unexpected order ahead of the reactor");
        if(reactorYard->isWaitingToPlace()) {
            auto& locations=bot->builderPlaceLocations[reactorYard->getObjectID()];
            require(!locations.empty(),"Missing the AI's foundation site");
            const auto location=locations.front();
            require(bot->doPlaceStructure(static_cast<ConstructionYard*>(reactorYard),location.x,location.y),
                "A paid reactor foundation could not be placed");
            locations.pop_front();++foundationsPlaced;
        } else {
            const auto charged=tick(reactorYard);
            require(charged.progressGain-charged.paid<=1,"A foundation progressed without payment");
        }
        ++foundationTicks;
    }
    require(reactorYard->getCurrentProducedItem()==Structure_NuclearPlant,
        "Reactor foundations never completed on the available start budget");
    require(foundationsPlaced>0,"The reactor fixture must exercise a real foundation queue");
    require(house->getCredits()<reactorPrice,"Foundation checks accidentally banked the full reactor price");
    setItemEnabled(Structure_WindTrap,true);
    house->powerRequirement=produced+1; // actual blackout
    setCredits(scarce);
    BuilderBase* other=reactorYard==yard ? secondYard : yard;
    bot->planningBuilder=other->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_NuclearPlant)==1,"The queued reactor was lost or duplicated");
    require(queuedAnywhere(Structure_WindTrap)>=1,
        "A blackout behind a pending reactor could not recover at all");

    // ---- 9. The reactor is charged in installments too, and completes.
    house->powerRequirement=produced-1;
    while(other->getProductionQueueSize())other->doCancelItem(other->getCurrentProducedItem());
    runToCompletion(reactorYard,Structure_NuclearPlant,reactorPrice,"reactor");

    // ---- 10. Mode control: outside city simulation none of this applies.
    clearQueues();
    game.citySimEnabled_=false;
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=DuneCity::NeedStadium|DuneCity::NeedAirport;
    bot->planningBuilder=yard->getObjectID();bot->clearPlacementCache();
    bot->build(0);
    require(queuedAnywhere(Structure_Stadium)==0 && queuedAnywhere(Structure_Airport)==0,
        "Civic growth-cap orders leaked into a non-city game");
    game.citySimEnabled_=true;
    SDL_Log("GROWTH_INSTALLMENT_OK");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=growth_installment outcome=passed");
}
