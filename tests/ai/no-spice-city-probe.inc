#include <structures/ZoneStructure.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

// A city map with no usable spice must grow a tax base out of the starting
// grant. 1.0.816 spent all 2,000 on a Windtrap, a Refinery and a Starport that
// could never earn anything, then an imported Carryall, and finished the match
// bankrupt with no R/C/I at all. These checks drive the real build decision and
// the real production queue: essential power still comes first, the demanded
// lots come next, queued orders hold their own place across several yards, and
// a city that funds itself goes back to the ordinary spice/port ladder.
void runNoSpiceCityProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("NO_SPICE_CITY_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot && game.isCitySimEnabled(),"No-spice city fixture needs a city QuantBot");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;bot->difficulty=QuantBot::Difficulty::Brutal;
    game.gameCycleCount=MILLI2CYCLES(60000);

    // The captured map's condition: not a low field, no field. Clearing the
    // tiles as well as the cached total means the periodic rescan inside
    // build() cannot quietly reintroduce spice halfway through the fixture.
    auto stripSpice=[&]() {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            auto* tile=currentGameMap->getTile(x,y);
            if(tile->hasSpice()||tile->isSpiceBloom()||tile->isSpecialBloom())tile->setType(Terrain_Sand);
        }
        bot->lastCalculatedSpice=0;
    };
    stripSpice();

    const int span=30;
    Coord origin=Coord::Invalid();
    for(int y=currentGameMap->getSizeY()/3;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<span;++dy)for(int dx=0;dx<span;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"No-spice city fixture needs empty settlement space");
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    auto put=[&](Uint32 item,int dx,int dy) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+dx,origin.y+dy,true,true);
        require(structure!=nullptr,"No-spice city fixture placement failed");
        return structure;
    };
    auto* yard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,14,14));
    // Generation the house actually owns. Without it the power planner rightly
    // buys a generator before any lot, because an injected producedPower figure
    // is not a reserve the city can grow against.
    for(Coord at:{Coord(8,8),Coord(8,12),Coord(12,8)})put(Structure_WindTrap,at.x,at.y);
    yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
    for(int y=origin.y;y<origin.y+span;++y)for(int x=origin.x;x<origin.x+span;++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);

    auto* sim=game.getCitySimulation();
    auto& state=sim->houseState_[HOUSE_HARKONNEN];
    state.avgLandValue=100;state.policeFundingPercent=100;
    state.resValve=2000;state.comValve=1500;state.indValve=1500;
    state.taxBaseEighths=0;

    // One real decision pass. Returns every order the yards actually queued, so
    // the checks read ordinary command execution rather than a predicate.
    struct Pass { std::vector<Uint32> orders; Uint32 first=NONE_ID; int zones=0; };
    auto decide=[&](int credits,int power) {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
            builder->upgrading=false;builder->setHealth(builder->getMaxHealth());
        }
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        // Explicit scenario funding, not an over-capacity spice deposit. The
        // grant is the real 2,000 the reported match started with: a fixture
        // that hands the bot a fortune would hide the original symptom.
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        // A negative figure means "keep the lights on but leave less headroom
        // than a lot needs to develop", which is the real growth constraint.
        house->producedPower=power<0 ? house->getPowerRequirement()+10 : power;
        bot->build(0);
        Pass pass;
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder())
            for(const auto& order:static_cast<BuilderBase*>(structure)->currentProductionQueue) {
                if(order.itemID==Structure_Slab1||order.itemID==Structure_Slab4
                    ||order.itemID==Structure_Road)continue;
                pass.orders.push_back(order.itemID);
                if(DuneCity::isCityZoneStructure(order.itemID))++pass.zones;
                if(pass.first==NONE_ID)pass.first=order.itemID;
            }
        return pass;
    };
    auto ordered=[](const Pass& pass,Uint32 item) {
        return std::find(pass.orders.begin(),pass.orders.end(),item)!=pass.orders.end();
    };
    auto noSpiceCapital=[&](const Pass& pass,const char* why) {
        require(!ordered(pass,Structure_Refinery),why);
        require(!ordered(pass,Structure_StarPort),why);
        require(!ordered(pass,Unit_Carryall),why);
    };

    // A map-placed market must protect the income budget before the first
    // demand update. The small grant belongs entirely to storage and lots.
    {
        auto* port=static_cast<BuilderBase*>(put(Structure_StarPort,2,18));
        state.resValve=state.comValve=state.indValve=0;
        const Pass pass=decide(600,10000);
        require(port->getProductionQueueSize()==0,
            "A preplaced starport spent the zero-demand opening's income budget");
        require(!ordered(pass,Unit_Carryall),"A spiceless empty fleet imported a transport");
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
        port->destroy();
        state.resValve=2000;state.comValve=1500;state.indValve=1500;
    }
    // These queue tests isolate lot growth once its storage prerequisite exists.
    // The separate full-game progress gates start with zero capacity and check
    // actual cumulative city_net_applied at both 1,500 and 2,000 credits.
    put(Structure_Refinery,18,8);

    // 1. Funded and powered: the grant buys a demanded lot, and none of the
    //    three purchases that bankrupted the reported match.
    {
        const Pass pass=decide(2000,10000);
        if(!DuneCity::isCityZoneStructure(pass.first)) {
            std::string seen;
            for(Uint32 item:pass.orders)seen+=" "+std::to_string(item);
            SDL_Log("NO_SPICE_CITY: funded opening ordered%s (first=%u)",
                seen.empty()?" nothing":seen.c_str(),pass.first);
        }
        require(DuneCity::isCityZoneStructure(pass.first),
            "A funded zero-spice city opening did not start with a demanded R/C/I lot");
        noSpiceCapital(pass,"A zero-spice opening still bought refinery/port/transport capital first");
    }

    // A small nonzero field has the same opening constraint. Use its real
    // amount so space and money planning see the identical worker target.
    {
        auto* tile=currentGameMap->getTile(origin.x+span+1,origin.y);
        tile->setType(Terrain_Spice);
        bot->lastCalculatedSpice=tile->getSpice().lround();
        require(bot->lastCalculatedSpice>0 && bot->lastCalculatedSpice<2250,
            "Tiny-field boundary must contain less than one sustainable worker's spice");
        const Pass pass=decide(2000,10000);
        require(DuneCity::isCityZoneStructure(pass.first),"A tiny unusable field blocked demanded zoning");
        noSpiceCapital(pass,"A tiny field diverted the income opening into spice technology");
        stripSpice();
    }

    // 2. Already bankrupt: nothing is affordable, and the pass must not commit
    //    to capital it cannot pay for either.
    {
        const Pass pass=decide(30,10000);
        require(pass.zones==0,"A bankrupt city ordered a lot it could not pay for");
        noSpiceCapital(pass,"A bankrupt zero-spice city committed to refinery/port/transport capital");
    }

    // 3. Power outranks growth. With less spare generation than a lot needs to
    //    develop, the seed waits: an unpowered 2x2 never grows, so buying it
    //    would be the same waste in a different shape. Whether the generator
    //    itself is ordered on this very pass belongs to the power planner, not
    //    to this rule; the 20-minute runs show it buying 2-8 Windtraps per
    //    house and finishing with produced power above requirement.
    {
        const Pass pass=decide(2000,-1);
        require(pass.zones==0,"A zero-spice city zoned while its own power headroom was short");
        noSpiceCapital(pass,"A power-short zero-spice city bought refinery/port/transport capital");
    }

    // 4. Pending orders and several yards share one seed. Both yards decide in
    //    the same pass; queued lots count, so the two of them together never
    //    exceed the seed, and no yard re-buys what the other just queued.
    {
        put(Structure_ConstructionYard,2,2);
        const Pass pass=decide(2000,10000);
        require(pass.zones>0,"Two yards on a zero-spice city map seeded nothing");
        require(pass.zones<=CityEconomyInvestmentPolicy::kBootstrapZoneSeed,
            "Parallel yards queued more lots in one pass than the seed they share");
        noSpiceCapital(pass,"A second yard spent the seed cash on refinery/port/transport capital");
    }

    // 5. Depletion and recovery run off live state, so a map that regains a
    //    usable field returns to the normal spice opening with no stored flag.
    {
        for(int i=0;i<64;++i)
            currentGameMap->getTile(origin.x+span+1,origin.y+i%span)->setType(Terrain_ThickSpice);
        bot->lastCalculatedSpice=1300000;
        const Pass rich=decide(2000,10000);
        require(rich.first!=NONE_ID,"A usable field left the city opening idle");
        require(ordered(rich,Structure_Refinery)||ordered(rich,Structure_StarPort)
                ||ordered(rich,Unit_Harvester)||DuneCity::isCityZoneStructure(rich.first),
            "A usable field did not produce an ordinary city opening order");
        stripSpice();
        const Pass depleted=decide(2000,10000);
        require(DuneCity::isCityZoneStructure(depleted.first),
            "A depleted field did not pivot back to demanded R/C/I growth");
        noSpiceCapital(depleted,"A depleted field still bought refinery/port/transport capital");
    }

    // 6. The decision is re-derived from saved economic state, not carried in a
    //    new counter: a real save round trip reproduces the same answer.
    {
        OMemoryStream out; out.open();
        bot->save(out);
        const std::string bytes(out.getData(),out.getDataLength());
        IMemoryStream in(bytes.data(),bytes.size());
        const Uint32 savedVersion=currentGame->loadedSavegameVersion;
        currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
        QuantBot restored(in,house);
        currentGame->loadedSavegameVersion=savedVersion;
        require(restored.lastCalculatedSpice==bot->lastCalculatedSpice,
            "A reloaded bot lost the remaining-spice total the bootstrap reads");
        restored.gameMode=QuantBot::GameMode::Custom;restored.supportMode=false;
        restored.difficulty=QuantBot::Difficulty::Brutal;
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
            builder->upgrading=false;builder->setHealth(builder->getMaxHealth());
        }
        restored.builderPlaceLocations.clear();restored.reservedStructures.clear();
        restored.clearPlacementCache();
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=2000;
        house->producedPower=10000;
        restored.build(0);
        bool restoredCapital=false,restoredZone=false;
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder())
            for(const auto& order:static_cast<BuilderBase*>(structure)->currentProductionQueue) {
                if(DuneCity::isCityZoneStructure(order.itemID))restoredZone=true;
                if(order.itemID==Structure_Refinery||order.itemID==Structure_StarPort
                    ||order.itemID==Unit_Carryall)restoredCapital=true;
            }
        require(restoredZone,"A reloaded zero-spice city did not resume demanded growth");
        require(!restoredCapital,
            "A reloaded zero-spice city bought refinery/port/transport capital the live bot refused");
    }

    // 7. The seed is a priority, not a ban. With the seed committed and a
    //    taxable base of its own, the ordinary spice/port ladder is available
    //    again, so military prerequisites stay reachable on a spiceless map.
    {
        for(Coord at:{Coord(20,20),Coord(24,20),Coord(20,24),Coord(24,24)})
            put(Structure_ZoneResidential,at.x,at.y);
        state.taxBaseEighths=4096;
        const Pass funded=decide(20000,10000);
        require(funded.first!=NONE_ID,"A self-funding zero-spice city stopped spending entirely");
        require(!DuneCity::isCityZoneStructure(funded.first)||funded.orders.size()>1,
            "A self-funding city never returned to the ordinary ladder beside its growth");
        state.taxBaseEighths=0;
    }

    SDL_Log("NO_SPICE_CITY_OK");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=no_spice_city outcome=passed");
}
