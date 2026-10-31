#include <units/Harvester.h>
#include <players/CityServiceInvestmentPolicy.h>
// Recurring policing - stations plus the rocket turrets that also police - is
// a permanent bill against city income, not a spice windfall: at most a third
// of the annual tax left after power while the city is still poor, half once
// savings or cashflow are healthy, and nothing at all when the tax base cannot
// carry it, dangerous crime included. Routine emplacements stop at two until
// the Heavy Factory, High Tech Factory and Repair Yard exist; the third one is
// deferred, not abandoned. Human and campaign funding stays where it is.
void runPoliceBudgetProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("POLICE_BUDGET_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot && game.isCitySimEnabled(),"Police budget fixture needs a city QuantBot");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->role=AIRole::Both;bot->difficulty=QuantBot::Difficulty::Hard;
    game.gameCycleCount=MILLI2CYCLES(120000);

    // A settlement too wide for two emplacements to double-cover every refinery:
    // proactive defence demand stays open for the whole fixture, so a routine
    // rocket is always the standing alternative to the missing suppliers.
    const int span=36;
    Coord origin=Coord::Invalid();
    for(int y=currentGameMap->getSizeY()/3;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<span;++dy)for(int dx=0;dx<span;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Police budget fixture needs empty settlement space");
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    // Free-space search keeps every later placement collision-safe, so extra
    // turrets and suppliers can be added between passes without a fixed layout.
    auto put=[&](Uint32 item)->StructureBase* {
        const Coord size=getStructureSize(item);
        for(int dy=0;dy+size.y<=span;++dy)for(int dx=0;dx+size.x<=span;++dx) {
            bool clear=true;
            for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x)
                clear &= !currentGameMap->getTile(origin.x+dx+x,origin.y+dy+y)->hasAGroundObject();
            if(!clear) continue;
            auto* structure=house->placeStructure(NONE_ID,item,origin.x+dx,origin.y+dy,true,true);
            require(structure!=nullptr,"Police budget fixture placement failed");
            return structure;
        }
        require(false,"No police budget fixture space left");
        return nullptr;
    };
    auto pave=[&] {
        for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);
    };
    auto* yard=static_cast<BuilderBase*>(put(Structure_ConstructionYard));
    for(int i=0;i<4;++i)put(Structure_Refinery);
    for(int i=0;i<3;++i)put(Structure_WindTrap);
    put(Structure_Radar);
    const std::array<Uint32,3> coreSuppliers{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard};
    const auto prerequisites=game.objectData.data[Structure_PoliceStation][HOUSE_HARKONNEN].prerequisiteStructuresSet;
    for(unsigned item=0;item<prerequisites.size();++item) {
        const bool supplier=std::find(coreSuppliers.begin(),coreSuppliers.end(),item)!=coreSuppliers.end();
        if(prerequisites.test(item) && !supplier && !house->getNumItems(item))put(item);
    }
    auto* district=static_cast<ZoneStructure*>(put(Structure_ZoneResidential));
    district->setResidentialPopulation(40);
    put(Structure_RocketTurret);
    yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
    pave();
    for(Uint32 supplier:coreSuppliers)
        require(house->getNumItems(supplier)==0,"Police budget fixture started with a core supplier");
    require(house->getNumItems(Structure_RocketTurret)==1,"Police budget fixture needs exactly one opening turret");

    // Workers and army are already satisfied: the funded construction slot is a
    // straight choice between a routine emplacement, a supplier and city growth.
    bot->harvesterLimit=8;bot->militaryValueLimit=8000;
    while(house->getNumItems(Unit_Harvester)<8) {
        auto* worker=static_cast<Harvester*>(house->createUnit(Unit_Harvester,true));
        require(worker!=nullptr,"Could not create fixture worker");
        bool deployed=false;
        for(int dy=0;dy<span && !deployed;++dy)for(int dx=0;dx<span && !deployed;++dx)
            if(!currentGameMap->getTile(origin.x+dx,origin.y+dy)->hasAGroundObject()) {
                worker->deploy(Coord(origin.x+dx,origin.y+dy));deployed=true;
            }
        require(deployed,"No fixture worker location");
    }
    auto* sim=game.getCitySimulation();
    auto& state=sim->houseState_[HOUSE_HARKONNEN];
    state.policeFundingPercent=100;state.avgLandValue=100;
    state.resValve=0;state.comValve=0;state.indValve=0;
    if(sim->getCityTax()<=0)sim->setCityTax(7);
    house->producedPower=10000;

    auto setCrime=[&](int level) {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            sim->crimeRateMap_.set(x/sim->crimeRateMap_.getBlockSize(),y/sim->crimeRateMap_.getBlockSize(),level);
            sim->crimeBeforePoliceMap_.set(x/sim->crimeBeforePoliceMap_.getBlockSize(),
                y/sim->crimeBeforePoliceMap_.getBlockSize(),level);
        }
    };
    auto reset=[&](int credits) {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
            builder->upgrading=false;
        }
        yard->setHealth(yard->getMaxHealth());
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        // Scenario funding for the policing decision, not earned spice that
        // fills the store and correctly requests a silo before routine defence.
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
    };
    auto queued=[&](Uint32 item) {
        return std::count_if(yard->currentProductionQueue.begin(),yard->currentProductionQueue.end(),
            [&](const auto& order){return order.itemID==item;})>0;
    };
    auto queuedSupplier=[&] {
        return std::any_of(coreSuppliers.begin(),coreSuppliers.end(),[&](Uint32 item){return queued(item);});
    };
    auto queuedZone=[&] {
        return std::any_of(yard->currentProductionQueue.begin(),yard->currentProductionQueue.end(),
            [](const auto& order){return DuneCity::isCityZoneStructure(order.itemID);});
    };

    setCrime(0); // Routine coverage only: nothing here is an emergency response.
    // One emplacement is below the routine ceiling, so the funded pass buys the
    // proactive rocket the spread refineries still demand.
    reset(20000);bot->build(20000);
    require(queued(Structure_RocketTurret),
        "Funded routine pass did not buy the second emplacement the open core demands");

    // Two emplacements reach the routine ceiling. With no Heavy Factory, High
    // Tech Factory or Repair Yard, the third routine rocket must wait and the
    // cash must go into the city instead of idling.
    put(Structure_RocketTurret);pave();
    require(house->getNumItems(Structure_RocketTurret)==2,"Routine ceiling fixture needs two emplacements");
    reset(20000);bot->build(20000);
    require(!queued(Structure_RocketTurret),
        "Third routine rocket outranked the missing Heavy Factory/High Tech Factory/Repair Yard");
    require(queuedSupplier() || queuedZone() || queued(Structure_LightFactory) || yard->isUpgrading(),
        "Deferred routine rocket left the funded construction slot unused");

    // Deferred, not abandoned: once the core suppliers exist the same pass buys
    // the emplacement again.
    for(Uint32 supplier:coreSuppliers)put(supplier);
    require(house->placeStructure(NONE_ID,Structure_Refinery,origin.x+30,origin.y+30,true,true)!=nullptr,
        "Could not place outlying refinery for renewed coverage demand");
    pave();yard->updateBuildList();
    reset(20000);bot->build(20000);
    require(queued(Structure_RocketTurret),
        "Routine emplacements stopped permanently instead of deferring to the core suppliers");

    // --- Recurring policing budget -------------------------------------------
    const int annualPower=house->isPowerRequired() ? house->getPowerRequirement()/8 : 0;
    auto annualTaxOf=[&](int eighths) {
        return DuneCity::computeAnnualTaxRevenue(eighths,sim->getCityTax(),state.avgLandValue);
    };
    // Solve the tax base for a wanted annual yield instead of pinning a magic
    // number to the current tax rate and land value.
    auto eighthsFor=[&](int annualTarget) {
        int eighths=0;
        while(eighths<4000000 && annualTaxOf(eighths)<annualTarget)eighths+=8;
        require(annualTaxOf(eighths)>=annualTarget,"Fixture could not reach the wanted tax base");
        return eighths;
    };
    auto& commands=game.getCommandManager();
    // Returns the funding percentage actually applied by this review.
    auto review=[&](int cash,int annualTaxTarget,int nominalCost,int startPercent) {
        reset(cash);
        state.taxBaseEighths=eighthsFor(annualTaxTarget);
        state.nominalPoliceCost=nominalCost;
        state.policeFundingPercent=startPercent;
        bot->lastPoliceBudgetReviewCycle=0;
        commands.bReadOnly=false;commands.timeslot.clear();
        // Set the owned service inventory independently of the cached city
        // scan, just as newly completed services can precede the next scan.
        const int previousStations=house->numItem[Structure_PoliceStation];
        house->numItem[Structure_PoliceStation]=(nominalCost-30)/100;
        try {bot->build(20000);} catch (...) {
            house->numItem[Structure_PoliceStation]=previousStations;throw;
        }
        house->numItem[Structure_PoliceStation]=previousStations;
        const int funding=state.policeFundingPercent;
        // Budget changes must already be effective. AI runs after command
        // execution; inspecting an unconsumed same-cycle command misses the bug.
        return funding;
    };
    auto recurringCost=[&](int funding,int nominalCost){return funding*nominalCost/100;};

    // Poor city, small savings: a third of the tax left after power.
    const int poorTax=annualPower+700, poorCost=430;
    const int poorTaxIncome=annualTaxOf(eighthsFor(poorTax));
    const int earlyPercent=CityServiceInvestmentPolicy::policingBudgetPercent(1000,poorTaxIncome,annualPower,poorCost);
    const int laterPercent=CityServiceInvestmentPolicy::policingBudgetPercent(6000,poorTaxIncome,annualPower,poorCost);
    require(earlyPercent==33,"Early city policing budget is not a third of tax after power");
    require(laterPercent==50,"Healthy savings did not raise the policing budget to half of tax after power");
    const int earlyExpected=CityServiceInvestmentPolicy::affordablePoliceFunding(
        poorTaxIncome,annualPower,poorCost,earlyPercent);
    const int laterExpected=CityServiceInvestmentPolicy::affordablePoliceFunding(
        poorTaxIncome,annualPower,poorCost,laterPercent);
    require(earlyExpected>0 && laterExpected<100 && laterExpected>earlyExpected,
        "Fixture tax base and service bill do not make the budget share binding");

    const int early=review(1000,poorTax,poorCost,0);
    require(early==earlyExpected,"Poor city funded policing away from its 33% tax share");
    require(recurringCost(early,poorCost)
        <=CityServiceInvestmentPolicy::policingAllowance(poorTaxIncome,annualPower,33),
        "Commanded policing bill exceeded a third of the annual tax after power");
    const int later=review(6000,poorTax,poorCost,0);
    require(later==laterExpected,"Healthy savings did not fund policing up to the 50% share");
    require(recurringCost(later,poorCost)
        <=CityServiceInvestmentPolicy::policingAllowance(poorTaxIncome,annualPower,50),
        "Commanded policing bill exceeded half of the annual tax after power");

    // Healthy cashflow reaches the same half share on a thin wallet: the tax
    // left after power and the standing bill clears the 500 margin.
    const int richCost=1030, richTax=annualPower+1600;
    const int richTaxIncome=annualTaxOf(eighthsFor(richTax));
    require(richTaxIncome-annualPower-richCost>=500,"Fixture cashflow case is below the healthy margin");
    const int cashflowPercent=CityServiceInvestmentPolicy::policingBudgetPercent(1000,richTaxIncome,annualPower,richCost);
    require(cashflowPercent==50,"Healthy cashflow did not raise the policing budget without savings");
    const int cashflowExpected=CityServiceInvestmentPolicy::affordablePoliceFunding(
        richTaxIncome,annualPower,richCost,cashflowPercent);
    require(cashflowExpected>0 && cashflowExpected<100,"Fixture cashflow case is not budget-bound");
    require(review(1000,richTax,richCost,0)==cashflowExpected,
        "Healthy cashflow city did not fund policing to its 50% tax share");

    // A city with no tax base funds nothing, and dangerous crime is no reason to
    // commit an income it does not have: there is no emergency budget bypass.
    setCrime(250);
    const int broke=review(1000,0,poorCost,5);
    require(broke==0,"Dangerous crime bypassed the policing budget the city cannot pay");
    setCrime(0);

    // Human and campaign funding belongs to the player and the mission script.
    std::vector<std::pair<int,int>> otherHouses;
    for(int id=0;id<DuneCity::kMaxCityHouses;++id)
        if(id!=HOUSE_HARKONNEN)otherHouses.push_back({id,sim->houseState_[id].policeFundingPercent});
    const int campaignStart=70;
    bot->gameMode=QuantBot::GameMode::Campaign;game.gameType=GameType::Campaign;
    require(review(1000,poorTax,poorCost,campaignStart)==campaignStart,
        "AI rewrote campaign city funding");
    bot->gameMode=QuantBot::GameMode::Custom;game.gameType=GameType::CustomGame;
    bot->role=AIRole::Builder;
    require(review(1000,poorTax,poorCost,campaignStart)==campaignStart,
        "AI helper rewrote the human player's city funding");
    bot->role=AIRole::Both;
    require(review(1000,poorTax,poorCost,0)==earlyExpected,"Custom AI stopped managing its own funding");
    for(const auto& other:otherHouses)
        require(sim->houseState_[other.first].policeFundingPercent==other.second,
            "AI budget review changed another house's city funding");

    FixPoint actualAnnualCost=0;
    for(const auto* structure:structureList)
        if(structure->getOwner()==house)actualAnnualCost+=DuneCity::getPoliceAnnualCost(structure->getItemID());
    const FixPoint expectedPaid=actualAnnualCost*state.policeFundingPercent/100;
    sim->runDailyBudget();
    require(state.lastPoliceExpense==expectedPaid.lround(),
        "Actual recurring charge did not use the AI's applied funding");
    require(expectedPaid<actualAnnualCost,"Expense regression needs an effective funding reduction");

    put(Structure_PoliceStation);pave();
    state.policeFundingPercent=50;setCrime(250);
    bot->cityServiceSearch.reset();bot->clearPlacementCache();
    Uint32 extraPolice=NONE_ID;Coord extraSite=Coord::Invalid();
    require(!bot->selectCityServiceInvestment(yard,10000,true,extraPolice,extraSite,false,Structure_PoliceStation),
        "Reduced funding triggered another station instead of repairing the recurring budget");

    SDL_Log("POLICE_BUDGET_PASS: recurring policing held to 33%% early and 50%% on savings/cashflow"
        " (early=%d later=%d cashflow=%d broke=%d); third routine rocket deferred to the core suppliers",
        early,later,cashflowExpected,broke);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=police-budget outcome=passed");
}
