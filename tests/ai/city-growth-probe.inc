#include <units/Harvester.h>
#include <players/RocketTurretPolicy.h>
// A funded single-yard city opening must keep developing R/C/I while proactive
// rocket overlap and the opening worker supplier are still outstanding, and
// must still refuse to spend an urgent crime reserve on a cheap plot.
void runCityGrowthProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("CITY_GROWTH_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot && game.isCitySimEnabled(),"City growth fixture needs a city QuantBot");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->role=AIRole::Both;bot->difficulty=QuantBot::Difficulty::Hard;
    game.gameCycleCount=MILLI2CYCLES(120000);

    // A settlement wide enough that no two emplacements can double-cover every
    // refinery: the overlap demand stays unmet for the whole fixture.
    const int span=36;
    Coord origin=Coord::Invalid();
    for(int y=currentGameMap->getSizeY()/3;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<span;++dy)for(int dx=0;dx<span;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"City growth fixture needs empty settlement space");
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    auto put=[&](Uint32 item,int dx,int dy) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+dx,origin.y+dy,true,true);
        require(structure!=nullptr,"City growth fixture placement failed");
        return structure;
    };
    auto* yard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,16,16));
    for(Coord at:{Coord(2,2),Coord(30,2),Coord(2,30),Coord(30,30)})put(Structure_Refinery,at.x,at.y);
    for(Coord at:{Coord(12,2),Coord(20,2),Coord(12,30)})put(Structure_WindTrap,at.x,at.y);
    put(Structure_Radar,20,30);
    for(Coord at:{Coord(14,12),Coord(20,20)})put(Structure_RocketTurret,at.x,at.y);
    const auto prerequisites=game.objectData.data[Structure_PoliceStation][HOUSE_HARKONNEN].prerequisiteStructuresSet;
    for(unsigned item=0;item<prerequisites.size();++item)
        if(prerequisites.test(item) && !house->getNumItems(item))put(item,2,16);
    auto* district=static_cast<ZoneStructure*>(put(Structure_ZoneResidential,16,22));
    district->setResidentialPopulation(40);
    yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);

    // Four workers on a rich field: income flows, but the opening floor and the
    // worker supplier are still unmet, exactly like the reported opening.
    for(int i=0;i<4;++i) {
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
    state.resValve=2000;state.comValve=2000;state.indValve=2000;
    house->producedPower=10000;

    auto setCrime=[&](int level) {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            sim->crimeRateMap_.set(x/sim->crimeRateMap_.getBlockSize(),y/sim->crimeRateMap_.getBlockSize(),level);
            sim->crimeBeforePoliceMap_.set(x/sim->crimeBeforePoliceMap_.getBlockSize(),
                y/sim->crimeBeforePoliceMap_.getBlockSize(),level);
        }
    };
    auto decide=[&](int credits) {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
        yard->upgrading=false;yard->setHealth(yard->getMaxHealth());
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        // Starting funds bypass spice-storage limits, matching the funded opening.
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(0);
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                return order.itemID;
        return Uint32(NONE_ID);
    };

    setCrime(0);
    // Both proactive goals are outstanding here: two emplacements are short of
    // the Hard overlap the spread core demands, and no worker supplier exists.
    require(house->getNumItems(Structure_RocketTurret)==2
        && house->getNumItems(Structure_HeavyFactory)==0
        && house->getNumItems(Structure_StarPort)==0,"Fixture defensive/supplier goals already met");
    const Uint32 plot=decide(150);
    require(DuneCity::isCityZoneStructure(plot),
        "Funded opening idled on an unreachable defence/supplier goal instead of developing the city");
    require(DuneCity::isCityZoneStructure(decide(20000)),
        "Affordable optional tech displaced a missing opening district");
    put(Structure_ZoneCommercial,22,22);
    put(Structure_ZoneIndustrial,26,22);
    // Defence keeps its priority when it is actually affordable; the fix must
    // not simply stop building emplacements or the worker supplier.
    const Uint32 funded=decide(20000);
    require(!DuneCity::isCityZoneStructure(funded) && funded!=NONE_ID,
        "Fully funded pass skipped proactive defence and the opening worker supplier");

    // Deepening the overlap on core assets that are already covered is a plan,
    // not a present loss: it shares the yard with the city instead of taking
    // every slot. A core asset with no cover at all is still defended at once.
    {
        // The production core the city already owns in the reported match, so
        // routine coverage is unlocked and only the overlap is outstanding.
        if(!house->getNumItems(Structure_HeavyFactory))put(Structure_HeavyFactory,6,6);
        if(!house->getNumItems(Structure_HighTechFactory))put(Structure_HighTechFactory,6,10);
        if(!house->getNumItems(Structure_RepairYard))put(Structure_RepairYard,6,14);
        if(!house->getNumItems(Structure_StarPort))put(Structure_StarPort,10,10);
        if(!house->getNumItems(Structure_LightFactory))put(Structure_LightFactory,10,14);
        const int radius=std::max(1,
            game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].weaponrange-1);
        auto coveringTurrets=[&](const StructureBase* asset) {
            int covered=0;
            for(const auto* turret:structureList)
                if(turret->getOwner()==house && turret->getHealth()>0
                    && turret->getItemID()==Structure_RocketTurret
                    && RocketTurretPolicy::coversBuilding(turret->getLocation(),
                        asset->getLocation(),asset->getStructureSize(),radius))++covered;
            return covered;
        };
        // One turret for every core asset that has none, so the base is short of
        // the difficulty's overlap everywhere but uncovered nowhere.
        for(bool progressed=true;progressed;) {
            progressed=false;
            const StructureBase* uncovered=nullptr;
            for(auto* structure:structureList)
                if(!uncovered && structure->getOwner()==house && structure->getHealth()>0
                    && RocketTurretPolicy::coreAsset(structure->getItemID())
                    && coveringTurrets(structure)==0)uncovered=structure;
            if(!uncovered)break;
            const Coord at=uncovered->getLocation();
            for(int dy=-radius;dy<=radius && !progressed;++dy)
                for(int dx=-radius;dx<=radius && !progressed;++dx) {
                    const Coord site(at.x+dx,at.y+dy);
                    if(!RocketTurretPolicy::coversBuilding(site,at,uncovered->getStructureSize(),radius)
                        || !currentGameMap->tileExists(site.x,site.y)
                        || !currentGameMap->okayToPlaceStructure(site.x,site.y,1,1,false,house,false,
                            Structure_RocketTurret))continue;
                    auto* turret=house->placeStructure(NONE_ID,Structure_RocketTurret,site.x,site.y,true,true);
                    if(!turret)continue;
                    progressed=RocketTurretPolicy::coversBuilding(site,at,uncovered->getStructureSize(),radius);
                    if(!progressed)turret->setHealth(0);
                }
            require(progressed,"Fixture could not cover a core asset");
        }
        for(auto* structure:structureList)
            require(!(structure->getOwner()==house && structure->getHealth()>0
                && RocketTurretPolicy::coreAsset(structure->getItemID())
                && coveringTurrets(structure)==0),"Fixture left a core asset uncovered");
        setCrime(0);
        bot->nonServiceConstructionOrders=0;
        const Uint32 interleaved=decide(20000);
        require(interleaved!=Structure_RocketTurret,
            "Extra overlap took the construction slot while every core asset was already covered");
        // Losing the only turret over a refinery is a present loss again.
        StructureBase* sacrificed=nullptr;
        for(auto* structure:structureList)
            if(!sacrificed && structure->getOwner()==house && structure->getHealth()>0
                && structure->getItemID()==Structure_Refinery && coveringTurrets(structure)==1)
                for(auto* turret:structureList)
                    if(!sacrificed && turret->getOwner()==house && turret->getHealth()>0
                        && turret->getItemID()==Structure_RocketTurret
                        && RocketTurretPolicy::coversBuilding(turret->getLocation(),
                            structure->getLocation(),structure->getStructureSize(),radius))sacrificed=turret;
        require(sacrificed!=nullptr,"Fixture found no single-turret refinery to uncover");
        sacrificed->setHealth(0);
        bot->nonServiceConstructionOrders=0;
        const Uint32 restored=decide(20000);
        require(restored==Structure_RocketTurret,
            "An uncovered core asset did not get its first turret back");
        sacrificed->setHealth(sacrificed->getMaxHealth());
        SDL_Log("CITY_DEFENCE_INTERLEAVE_PASS: covered base built %u instead of more overlap;"
            " uncovered refinery rebuilt cover (%u)",interleaved,restored);
    }

    // An urgent crime reserve still outranks a cheap plot at the same budget.
    // Remove effective turret coverage before demanding a new emergency
    // service: a raw map stamp alone is inconsistent with two working turrets.
    for(auto* structure:structureList)
        if(structure->getOwner()==house && structure->getItemID()==Structure_RocketTurret)
            structure->setHealth(0);
    // This city can sustainably fund protection; the separate budget probe
    // covers a zero-tax city, where an emergency cannot bypass the spending cap.
    state.taxBaseEighths=2000;
    state.policeFundingPercent=100; // This case starts funded; increase smoothing has its own tests.
    setCrime(250);
    const Uint32 urgent=decide(150);
    require(!DuneCity::isCityZoneStructure(urgent),
        "Urgent crime reserve was spent on city growth");

    SDL_Log("CITY_GROWTH_PASS: funded single-yard opening keeps zoning with defence/supplier unmet;"
        " funded passes still buy defence; urgent crime reserve preserved (growth=%u funded=%u urgent=%u)",
        plot,funded,urgent);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_growth outcome=passed");
}
