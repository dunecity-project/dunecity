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
    // rule, has nothing to ask for while the civic cases are measured.
    for(int i=0;i<12;++i)put(Structure_WindTrap,4+2*i,2);
    // The spice-income ladder (four refineries, the port, its repair support) also
    // runs before the civic rule and legitimately claims the yard while it is
    // incomplete. Complete it in the fixture so the civic rule is what is measured.
    for(int i=0;i<4;++i)put(Structure_Refinery,4+5*i,8);
    put(Structure_HeavyFactory,26,8);
    put(Structure_Radar,32,8);
    put(Structure_StarPort,4,14);
    put(Structure_RepairYard,10,14);
    put(Structure_Silo,16,14);
    const auto prerequisites=game.objectData.data[Structure_Stadium][houseID].prerequisiteStructuresSet;
    for(unsigned item=0;item<prerequisites.size();++item)
        if(prerequisites.test(item) && !house->getNumItems(item))put(item,2,20);
    for(auto* builder:{yard,secondYard}) {
        builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
        builder->upgrading=false;
        while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
    }
    require(yard->isAvailableToBuild(Structure_Stadium),"Fixture yard cannot build a Stadium");
    require(yard->isAvailableToBuild(Structure_Airport),"Fixture yard cannot build an Airport");
    require(yard->isAvailableToBuild(Structure_NuclearPlant),"Fixture yard cannot build a reactor");

    // A taxable city: developed residential lots on a road network, advanced by the
    // real simulation so the house state is its own.
    for(int i=0;i<10;++i) {
        auto* lot=static_cast<ZoneStructure*>(put(Structure_ZoneResidential,4+2*i,26));
        lot->setResidentialPopulation(48);
    }
    for(int i=0;i<6;++i)put(Structure_ZoneCommercial,4+2*i,30);
    for(int y=origin.y;y<origin.y+developedRows;++y)for(int x=origin.x;x<origin.x+span;++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);
    // One road edge so the bare civic land is reachable, then leave it clear.
    for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,origin.y+developedRows)->setRoad(true);
    for(int i=0;i<8;++i)sim->advancePhase(game.getGameCycleCount());
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

    // ---- 1. A demanded Stadium starts on scarce cash, well beyond the forecast.
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=DuneCity::NeedStadium;
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

    // Crime protection and proactive core defence legitimately outrank an optional
    // civic and claimed both yards once the city had grown. Take them out of the
    // remaining measurements rather than reordering production code: what is under
    // test is how a growth-cap project is funded, not its priority against defence.
    auto setItemEnabled=[&](Uint32 item,bool on) {
        game.objectData.data[item][houseID].enabled=on;
        for(auto* builder:{yard,secondYard})builder->updateBuildList();
    };
    setItemEnabled(Structure_RocketTurret,false);
    setItemEnabled(Structure_PoliceStation,false);

    // ---- 5. The same rule for a demanded Airport.
    while(yard->getProductionQueueSize())yard->doCancelItem(yard->getCurrentProducedItem());
    while(secondYard->getProductionQueueSize())secondYard->doCancelItem(secondYard->getCurrentProducedItem());
    put(Structure_Stadium,2,38); // The stadium now exists; the next cap is commercial.
    sim->advancePhase(game.getGameCycleCount());
    pinTaxBase(targetForecast);
    setCredits(scarce);
    sim->getHouseStateMut(houseID).civicDemandBlocked=DuneCity::NeedAirport;
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
