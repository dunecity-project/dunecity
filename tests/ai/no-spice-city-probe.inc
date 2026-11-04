#include <structures/ZoneStructure.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/QuantBotBuildPass.h>
#include <string_view>
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
    auto cancelOrders=[&](BuilderBase* builder) {
        // Starport batches do not expose their front through currentProducedItem.
        while(builder->getProductionQueueSize()) {
            const int before=builder->getProductionQueueSize();
            builder->doCancelItem(builder->getProductionQueue().front().itemID);
            require(builder->getProductionQueueSize()<before,"Fixture queue cancellation made no progress");
        }
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot && game.isCitySimEnabled(),"No-spice city fixture needs a city QuantBot");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->role=AIRole::Both;bot->difficulty=QuantBot::Difficulty::Brutal;
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
            cancelOrders(builder);
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
            cancelOrders(builder);
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
        restored.gameMode=QuantBot::GameMode::Custom;restored.role=AIRole::Both;
        restored.difficulty=QuantBot::Difficulty::Brutal;
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            cancelOrders(builder);
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

    // harkonan doing nothing.dls (1.0.864): the seed existed, but only ten
    // spare power remained. The next cheap import repeatedly consumed the
    // generator savings, so a four-lot city stayed four lots indefinitely.
    {
        auto* port=static_cast<StarPort*>(put(Structure_StarPort,2,18));
        put(Structure_Radar,2,24); // The save already unlocked rocket-turret prerequisites.
        const auto savedStructures=structureList;
        const auto savedConfig=bot->configOverride;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.cityOpeningCapital=false; // Isolate the saved growth-power regression.
        config->force.moreRocketTurrets=false; // This case measures zoning resumption, not the standing battery.
        bot->configOverride=config;
        bot->difficulty=QuantBot::Difficulty::Hard;bot->militaryValueLimit=70000;
        // One actual planning yard, as in the save; retain the fixture's real
        // generators, storage, zones and road-connected placement space.
        for(auto* structure:savedStructures)
            if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard
                && structure!=yard)structureList.remove(structure);
        state.resPop=120;state.comPop=3;state.indPop=15;state.taxBaseEighths=304;
        state.avgLandValue=160;state.civicDemandBlocked=0;
        state.resValve=2000;state.comValve=1500;state.indValve=717;
        const auto& wind=game.objectData.data[Structure_WindTrap][HOUSE_HARKONNEN];
        const Coord windSize=getStructureSize(Structure_WindTrap);
        const int windCapital=wind.price+(game.getGameInitSettings().getGameOptions().concreteRequired
            ? windSize.x*windSize.y*game.objectData.data[Structure_Slab1][HOUSE_HARKONNEN].price : 0);
        auto marketDecide=[&](int credits,int power) {
            port->informFrigateDestroyed(); // Reset this fixture's preceding shipment.
            port->updateBuildList();
            // This controlled bargain outranks routine core cover, as the
            // selected imports did in the save; isolate its power-budget claim.
            for(auto& offer:port->buildList)if(offer.itemID==Unit_Quad)offer.price=70;
            for(auto& offer:house->getChoam().availableItems)offer.num=offer.itemID==Unit_Quad ? 10 : 0;
            return decide(credits,power);
        };
        try {
            config->force.cityGrowthPower=false;
            const Pass legacy=marketDecide(windCapital,-1);
            require(ordered(legacy,Unit_Quad),"Power-stall negative control did not spend on the cheap import");
            require(!ordered(legacy,Structure_WindTrap)&&!ordered(legacy,Structure_NuclearPlant),
                "Power-stall negative control already funded generation");
            config->force.cityGrowthPower=true;
            const Pass saving=marketDecide(windCapital-1,-1);
            require(port->getProductionQueueSize()==0,"A Starport bargain consumed required growth-power savings");
            require(!ordered(saving,Structure_WindTrap),"Growth power spent cash not yet in hand");
            const Pass funded=marketDecide(windCapital,-1);
            require(ordered(funded,Structure_WindTrap),"Demanded growth failed to fund its real generator prerequisite");
            require(port->getProductionQueueSize()==0,"Military spending took the generator's purchase budget");
            bot->build(0);
            int pending=0;
            for(const auto& order:yard->getProductionQueue())pending+=order.itemID==Structure_WindTrap;
            require(pending==1,"A pending growth generator was ordered twice");
            const Pass powered=marketDecide(1000,10000);
            require(powered.zones>0,"Funded power did not release the city back to demanded zoning");
            SDL_Log("CITY_GROWTH_POWER_PASS: off buys import; on saves %d, orders one generator, resumes zoning",windCapital);
        } catch (...) {
            bot->configOverride=savedConfig;structureList=savedStructures;throw;
        }
        bot->configOverride=savedConfig;structureList=savedStructures;
    }

    // Random Skirmish 96/None/T9, seed 9101: the first reserve request
    // needed only 40 power, but final vetting turned it into a 2,000-credit
    // reactor and left a 3,000-credit start without useful working cash.
    {
        const auto savedConfig=bot->configOverride;
        const int savedProduced=house->producedPower, savedRequired=house->powerRequirement;
        const int savedFootprints=bot->availableBaseFootprints;
        const auto savedStructures=structureList;
        const int savedGrowth=bot->projectedPowerDemandGrowth;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.cityOpeningCapital=false; // Keep the original competing military quote.
        config->force.moreRocketTurrets=false; // Keep the growth-power quote independent of the standing battery.
        bot->configOverride=config;bot->availableBaseFootprints=4;
        bot->clearPlacementCache();
        require(bot->findPlaceLocation(Structure_WindTrap).isValid()
            && bot->findPlaceLocation(Structure_NuclearPlant).isValid(),
            "Generator-sizing fixture needs legal wind and reactor sites");
        auto choice=[&](bool enabled,int cash,int produced,int required,int reserve,
                        bool installments=false,bool city=true) {
            config->force.cityPowerSizing=enabled;
            house->producedPower=produced;house->powerRequirement=required;
            QuantBot::BuildPass pass(0);
            pass.money=cash;pass.citySimEnabled=city;pass.spiceEconomyViable=true;
            pass.cityWorkingReserve=450;pass.cityPowerReserve=reserve;
            pass.itemCount[Structure_WindTrap]=3;
            QuantBot::BuilderTurn turn(pass.money);turn.pBuilder=yard;
            if(installments)turn.installmentProject=Structure_NuclearPlant;
            QuantBot::YardTurn selected;
            selected.isCitySim=city;selected.itemID=Structure_WindTrap;selected.structureRule="power";
            bot->vetYardChoice(pass,turn,selected);
            return selected.itemID;
        };
        auto clearOrders=[&]() {
            for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
                auto* builder=static_cast<BuilderBase*>(structure);
                cancelOrders(builder);
                builder->upgrading=false;
            }
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        };
        auto growthQuote=[&](bool enabled) {
            clearOrders();config->force.cityPowerSizing=enabled;config->force.cityGrowthPower=true;
            StarPort* market=nullptr;
            for(auto* structure:structureList)
                if(structure->getOwner()==house && structure->getItemID()==Structure_StarPort) {
                    market=static_cast<StarPort*>(structure);break;
                }
            require(market!=nullptr,"Growth-power quote fixture lost its competing Starport");
            market->informFrigateDestroyed();market->updateBuildList();
            for(auto& offer:market->buildList)if(offer.itemID==Unit_Quad)offer.price=70;
            for(auto& offer:house->getChoam().availableItems)offer.num=offer.itemID==Unit_Quad ? 10 : 0;
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=2993;
            house->powerRequirement=240;house->producedPower=250;
            QuantBot::BuildPass pass(0);
            bot->resetBuildPass();bot->planBuildInventory(pass);bot->planBuildBudget(pass);
            bot->planCityState(pass);bot->planStrategicReserve(pass);bot->planSpiceEconomy(pass);
            bot->planPowerAndYards(pass);bot->planCityPopulation(pass);bot->planCivicNeedAndTrips(pass);
            bot->planUnitMix(pass);bot->planCapitalForecast(pass);bot->planCityBootstrap(pass);
            bot->planOpeningProduction(pass);bot->planDefenceCoverage(pass);
            pass.capitalBuilders.clear();pass.capitalBuilders.push_back(yard);pass.capitalBuilders.push_back(market);
            bot->planCapitalCandidates(pass);bot->chooseCapitalPlan(pass);
            require(pass.capitalChoice>=0,"Growth-power quote did not select a capital project");
            const auto selected=pass.capitalCandidates[pass.capitalChoice];
            require(std::string_view(selected.kind)=="city_growth_power",
                "Growth-power quote fixture selected another capital project");
            require(bot->capitalReserve(pass,NONE_ID)==selected.cost,
                "Growth-power reservation does not match the selected generator's full capital");
            return selected;
        };
        try {
            require(choice(false,3000,300,240,100)==Structure_NuclearPlant,
                "Generator-sizing negative control no longer reproduces the oversized reactor");
            require(choice(true,3000,300,240,100)==Structure_WindTrap,
                "A small reserve request consumed the seed town's working capital");
            require(choice(true,12000,300,240,100)==Structure_NuclearPlant,
                "A prosperous city lost its affordable compact reserve");
            require(choice(true,3000,300,800,200)==Structure_NuclearPlant,
                "Seven windtraps were preferred to cheaper useful nuclear capacity");
            require(choice(true,3000,200,240,100)==Structure_NuclearPlant,
                "Affordable blackout recovery lost its existing nuclear preference");
            require(choice(true,500,300,240,100,true)==Structure_NuclearPlant,
                "An already funded installment reactor was changed into another project");
            require(choice(true,3000,300,240,100,false,false)==Structure_WindTrap,
                "City generator sizing changed the vanilla choice");
            clearOrders();
            // Isolate capacity planning from the fixture's deliberately
            // overprovisioned latent zones, retaining real generator sites.
            for(auto* structure:savedStructures)
                if(structure->getOwner()==house && DuneCity::isCityZoneStructure(structure->getItemID()))
                    structureList.remove(structure);
            bot->projectedPowerDemandGrowth=0;
            auto planned=[&](bool enabled,bool windAvailable=true) {
                config->force.cityPowerSizing=enabled;
                house->powerRequirement=330;house->producedPower=400;
                QuantBot::BuildPass pass(0);
                pass.citySimEnabled=true;pass.cityWorkingReserve=450;pass.nuclearBuildAvailable=true;
                pass.windBuildAvailable=windAvailable;
                pass.itemCount[Structure_HeavyFactory]=1;
                pass.itemCount[Structure_WindTrap]=house->getNumItems(Structure_WindTrap);
                pass.itemCount[Structure_NuclearPlant]=house->getNumItems(Structure_NuclearPlant);
                bot->planPowerAndYards(pass);
                return pass.nuclearPlan;
            };
            require(planned(false),"Planned-reactor negative control no longer requests excess capacity");
            require(!planned(true),"One-windtrap reserve entered the yard as an oversized installment reactor");
            require(planned(true,false),"An unavailable cheap generator rejected the only legal reactor");
            structureList=savedStructures;bot->projectedPowerDemandGrowth=savedGrowth;
            const auto oldQuote=growthQuote(false);
            const auto sizedQuote=growthQuote(true);
            require(oldQuote.item==Structure_NuclearPlant,
                "Growth-power quote negative control no longer reserves a reactor");
            QuantBot::BuildPass quotePrice(0);
            require(sizedQuote.item==Structure_WindTrap
                && sizedQuote.cost==bot->buildingCapitalCost(quotePrice,Structure_WindTrap),
                "Sized growth power retained the reactor's item or foundation budget");
            SDL_Log("CITY_POWER_SIZING_PASS: small reserves use wind; rich/load/blackout/installment choices retain nuclear");
        } catch (...) {
            bot->configOverride=savedConfig;bot->availableBaseFootprints=savedFootprints;
            structureList=savedStructures;bot->projectedPowerDemandGrowth=savedGrowth;
            house->producedPower=savedProduced;house->powerRequirement=savedRequired;throw;
        }
        bot->configOverride=savedConfig;bot->availableBaseFootprints=savedFootprints;
        structureList=savedStructures;bot->projectedPowerDemandGrowth=savedGrowth;
        house->producedPower=savedProduced;house->powerRequirement=savedRequired;
    }

    // atreides no expand.dls: the first heavy factory had no shared capital
    // claim. A cheap imported Quad reserved its price from the yard first,
    // then repeated bargains spent the rest while the factory stayed absent.
    {
        require(house->getNumItems(Structure_HeavyFactory)==0,
            "First-factory fixture must start without a heavy factory");
        StarPort* port=nullptr;
        for (auto* structure:structureList)
            if (structure->getOwner()==house && structure->getItemID()==Structure_StarPort
                && structure->getHealth()>0) port=static_cast<StarPort*>(structure);
        require(port!=nullptr,"First-factory fixture needs the standing Starport");
        auto* light=static_cast<BuilderBase*>(put(Structure_LightFactory,2,14));
        yard->updateBuildList();
        const auto savedConfig=bot->configOverride;
        const auto savedState=state;
        const auto savedDifficulty=bot->difficulty;
        std::vector<std::pair<BuilderBase*,int>> yardLevels;
        // The captured yard was already upgraded. Bulk-slab technology is a
        // separate mandatory purchase, not the import competition under test.
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard) {
                auto* builder=static_cast<BuilderBase*>(structure);
                yardLevels.emplace_back(builder,builder->getCurrentUpgradeLevel());
                builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
            }
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.storageCapacity=false; // Isolate construction, not forecast storage relief.
        config->force.moreRocketTurrets=false; // Compare the factory with imports, not a separate battery claim.
        bot->configOverride=config;
        state.resPop=500;state.comPop=25;state.indPop=35;state.taxBaseEighths=4096;
        state.resValve=state.comValve=state.indValve=0;
        auto& factoryData=game.objectData.data[Structure_HeavyFactory][HOUSE_HARKONNEN];
        const bool savedEnabled=factoryData.enabled;
        const int factoryPrice=factoryData.price;
        auto marketDecide=[&](bool enabled,int credits,int power=10000) {
            config->force.cityConstructionCapital=enabled;
            port->informFrigateDestroyed();
            for (auto& offer:house->getChoam().availableItems) {
                offer.num=offer.itemID==Unit_Quad ? 10 : 0;
                if (offer.itemID==Unit_Quad) offer.price=70;
            }
            port->updateBuildList();
            return decide(credits,power);
        };
        auto restore=[&]() {
            factoryData.enabled=savedEnabled;
            port->informFrigateDestroyed();
            cancelOrders(port);cancelOrders(light);
            light->destroy();
            bot->configOverride=savedConfig;state=savedState;bot->difficulty=savedDifficulty;
            for(auto [builder,level]:yardLevels) {
                cancelOrders(builder);
                builder->curUpgradeLev=level;builder->updateBuildList();
            }
        };
        try {
            for(auto level:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=level;
                const Pass unchanged=marketDecide(true,factoryPrice);
                require(ordered(unchanged,Unit_Quad) && !ordered(unchanged,Structure_HeavyFactory),
                    "First-factory protection changed an Easy/Medium house's ordinary import priority");
            }
            for(auto level:{QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
                bot->difficulty=level;
                const Pass eligible=marketDecide(true,factoryPrice);
                require(ordered(eligible,Structure_HeavyFactory) && !ordered(eligible,Unit_Quad),
                    "A fully affordable Hard/Brutal factory lost its cash to an import");
            }
            bot->difficulty=savedDifficulty;
            const Pass legacy=marketDecide(false,factoryPrice);
            require(ordered(legacy,Unit_Quad) && !ordered(legacy,Structure_HeavyFactory),
                "First-factory negative control did not divert affordable factory cash into imports");
            config->force.cityProductionPrerequisites=false;
            const Pass shortCash=marketDecide(true,factoryPrice-1);
            require(!ordered(shortCash,Structure_HeavyFactory) && ordered(shortCash,Unit_Quad),
                "An unaffordable first factory withheld working cash from ordinary imports");
            config->force.cityProductionPrerequisites=true;
            const Pass saving=marketDecide(true,factoryPrice-1);
            require(!ordered(saving,Structure_HeavyFactory) && port->getProductionQueueSize()==0,
                "A solvent multi-yard city spent its first-factory savings on imports");
            const auto allStructures=structureList;
            for (auto* structure:allStructures)
                if (structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard
                    && structure!=yard) structureList.remove(structure);
            Pass singleYard;
            try { singleYard=marketDecide(true,factoryPrice-1); }
            catch (...) { structureList=allStructures;throw; }
            structureList=allStructures;
            require(ordered(singleYard,Unit_Quad),
                "A single-yard opening lost its existing below-quote import budget");
            const int strongTaxBase=state.taxBaseEighths;
            const int upkeep=house->getPowerRequirement()/8+DuneCity::annualPerMinute(state.lastPoliceExpense);
            int weakTaxBase=1;
            while (weakTaxBase<strongTaxBase && DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
                weakTaxBase,sim->getCityTax(HOUSE_HARKONNEN),state.avgLandValue))<=upkeep) ++weakTaxBase;
            state.taxBaseEighths=weakTaxBase;
            require(ordered(marketDecide(true,factoryPrice-1),Unit_Quad),
                "A city whose forecast cannot fund the first factory lost its working cash");
            state.taxBaseEighths=strongTaxBase;
            state.comValve=state.indValve=1500;
            const Pass zoningFirst=marketDecide(false,factoryPrice-1);
            require(zoningFirst.zones>0,"Parallel-growth negative control did not bypass the factory bootstrap");
            const Pass bootstrapFirst=marketDecide(true,factoryPrice-1);
            require(bootstrapFirst.zones==0 && port->getProductionQueueSize()==0,
                "Parallel zoning or imports still consumed a funded first-factory bootstrap");
            for(auto level:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=level;
                require(marketDecide(true,factoryPrice-1).zones>0,
                    "First-factory bootstrap changed Easy/Medium's parallel growth allocation");
            }
            bot->difficulty=savedDifficulty;
            factoryData.enabled=false;
            for(const auto& yardLevel:yardLevels)yardLevel.first->updateBuildList();
            require(marketDecide(true,factoryPrice-1).zones>0,
                "Unavailable factory technology withheld parallel city growth");
            factoryData.enabled=savedEnabled;
            for(const auto& yardLevel:yardLevels)yardLevel.first->updateBuildList();
            state.comValve=state.indValve=0;
            const int growthPower=house->getPowerRequirement()+std::max(0,factoryData.power)+24;
            const Pass lastPower=marketDecide(true,factoryPrice,growthPower-1);
            require(!ordered(lastPower,Structure_HeavyFactory),
                "First heavy factory consumed the last city-growth power headroom");
            const Pass funded=marketDecide(true,factoryPrice,growthPower);
            require(ordered(funded,Structure_HeavyFactory) && !ordered(funded,Unit_Quad),
                "An affordable first heavy factory lost its cash to a Starport bargain");
            auto pendingFactories=[&]() {
                int pending=0;
                for(auto* structure:structureList)
                    if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard)
                        for(const auto& order:static_cast<BuilderBase*>(structure)->getProductionQueue())
                            pending+=order.itemID==Structure_HeavyFactory;
                return pending;
            };
            require(pendingFactories()==1,"The funded pass must queue exactly one first heavy factory across all yards");
            bot->build(0);
            require(pendingFactories()==1,"The protected first heavy factory was duplicated or lost on the next pass");
            factoryData.enabled=false;
            for(const auto& yardLevel:yardLevels)yardLevel.first->updateBuildList();
            const Pass unavailable=marketDecide(true,factoryPrice);
            require(ordered(unavailable,Unit_Quad) && !ordered(unavailable,Structure_HeavyFactory),
                "Unavailable heavy technology kept reserving cash from useful military imports");
            SDL_Log("CITY_CONSTRUCTION_CAPITAL_PASS: Hard/Brutal affordable first factory orders once; Easy/Medium and short cash retain imports");
        } catch (...) {
            restore();throw;
        }
        restore();
    }

    // notcolonizingsadukar.dls: two future 3x2 plots occupied the only
    // production space, but both needed the Light Factory they excluded.
    // Drive the real reservation/search/queue paths on the same two patches.
    {
        const auto savedConfig=bot->configOverride;
        const auto savedState=state;
        const auto savedDifficulty=bot->difficulty;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.storageCapacity=false;
        config->force.moreRocketTurrets=false;
        bot->configOverride=config;
        state.taxBaseEighths=4096;state.resPop=500;
        state.resValve=0;state.comValve=state.indValve=1500;
        auto planPlots=[&](bool enabled) {
            config->force.cityProductionPrerequisites=enabled;
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
            bot->resetBuildPass();
            QuantBot::BuildPass pass(0);
            bot->planBuildInventory(pass);bot->planBuildBudget(pass);
        };
        planPlots(false);
        const auto futurePlots=bot->cityProductionPlots;
        require(futurePlots.size()==2,"Prerequisite fixture needs the two future production plots");
        struct TileState { Tile* tile; int type; int owner; bool road; };
        std::vector<TileState> terrain;
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            auto* tile=currentGameMap->getTile(x,y);
            if(!tile->isRock() || tile->isMountain() || tile->hasAStructure())continue;
            terrain.push_back({tile,tile->getType(),tile->getOwner(),tile->isRoad()});
            bool future=false;
            for(const auto& plot:futurePlots) {
                const Coord size=getStructureSize(plot.item),at=plot.location;
                future|=x>=at.x&&x<at.x+size.x&&y>=at.y&&y<at.y+size.y;
            }
            if(!future)tile->setType(Terrain_Sand);
        }
        BuilderBase* light=nullptr;
        auto restore=[&]() {
            for(auto* structure:structureList)
                if(structure->getOwner()==house && structure->isABuilder())
                    cancelOrders(static_cast<BuilderBase*>(structure));
            if(light)light->destroy();
            for(const auto& tile:terrain) {
                tile.tile->setType(tile.type);tile.tile->setOwner(tile.owner);tile.tile->setRoad(tile.road);
            }
            bot->configOverride=savedConfig;state=savedState;bot->difficulty=savedDifficulty;
            bot->cityProductionPlots.clear();bot->clearPlacementCache();
            bot->rockSurveyCycle=std::numeric_limits<Uint32>::max();
        };
        try {
            planPlots(false);
            int rawLightSites=0;
            const Coord size=getStructureSize(Structure_LightFactory);
            for(int y=0;y<=currentGameMap->getSizeY()-size.y;++y)
                for(int x=0;x<=currentGameMap->getSizeX()-size.x;++x)
                    rawLightSites+=currentGameMap->okayToPlaceStructure(x,y,size.x,size.y,false,house,false,Structure_LightFactory);
            require(rawLightSites>0 && bot->findPlaceLocation(Structure_LightFactory).isInvalid(),
                "Prerequisite negative control did not exclude a physically legal Light Factory");
            require(bot->findPlaceLocation(Structure_ZoneIndustrial).isInvalid(),
                "Prerequisite negative control left the cramped city's growth space available");
            Coord lightSite=Coord::Invalid();
            for(auto level:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                            QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
                bot->difficulty=level;
                planPlots(true);
                require(bot->cityProductionPlots.size()==1
                    && bot->cityProductionPlots.front().item==Structure_LightFactory,
                    "Future production plots did not share their one missing prerequisite");
                lightSite=bot->findPlaceLocation(Structure_LightFactory);
                require(lightSite.isValid(),"A future factory still excluded its own Light Factory");
                require(bot->findPlaceLocation(Structure_ZoneIndustrial).isValid(),
                    "The unused future plot still withheld all demanded city growth");
            }
            // Growth legality was checked with live demand above. Isolate the
            // prerequisite's queue from the independent growth/capital ranking.
            state.comValve=state.indValve=0;
            const Pass unlocked=decide(3000,10000);
            require(ordered(unlocked,Structure_LightFactory),"The unblocked prerequisite was not ordered");
            for(auto* structure:structureList)
                if(structure->getOwner()==house && structure->isABuilder())
                    cancelOrders(static_cast<BuilderBase*>(structure));
            light=static_cast<BuilderBase*>(house->placeStructure(NONE_ID,Structure_LightFactory,
                lightSite.x,lightSite.y,true,true));
            require(light!=nullptr,"Prerequisite fixture failed to place its funded Light Factory");
            for(auto* structure:structureList)
                if(structure->getOwner()==house && structure->isABuilder())
                    static_cast<BuilderBase*>(structure)->updateBuildList();
            planPlots(true);
            require(bot->findPlaceLocation(Structure_HeavyFactory).isValid(),
                "The prerequisite consumed both future factory footprints");
            const Pass heavy=decide(3000,10000);
            require(ordered(heavy,Structure_HeavyFactory),"The unlocked affordable Heavy Factory was not ordered");
            SDL_Log("CITY_PRODUCTION_PREREQUISITES_PASS: off blocks %d legal light sites; on unlocks light/growth on all difficulties, then heavy",rawLightSites);
        } catch (...) {
            restore();throw;
        }
        restore();
    }

    // The right-sized generator alone was insufficient in Random Skirmish
    // 96/None/T9 seed 9101: 900 went to an optional MCV and another 900 to
    // factory upgrades before tax covered upkeep. Exercise the actual queue
    // and upgrade boundaries, with the captured negative four-minute forecast.
    {
        auto* factory=static_cast<BuilderBase*>(put(Structure_HeavyFactory,2,2));
        auto* light=static_cast<BuilderBase*>(put(Structure_LightFactory,2,10));
        factory->curUpgradeLev=1;factory->updateBuildList();
        light->curUpgradeLev=0;light->updateBuildList();
        const auto savedConfig=bot->configOverride;
        const int savedProduced=house->producedPower,savedRequired=house->powerRequirement;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.researchLab=false; // The shared budget must not require the research option.
        bot->configOverride=config;
        auto budgetPass=[&]() {
            cancelOrders(factory);cancelOrders(light);factory->upgrading=false;light->upgrading=false;
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=3000;
            house->producedPower=10000;house->powerRequirement=100;
            QuantBot::BuildPass pass(0);
            bot->planBuildInventory(pass);
            // This controlled pass bypasses chooseCapitalPlan(), whose
            // no-selection sentinel must still precede real queue execution.
            pass.capitalChoice=-1;
            pass.spiceEconomyViable=false;
            pass.money=3000;pass.citySimEnabled=true;pass.cityWorkingReserve=450;pass.cashBuffer=1000;
            pass.forecastTax=0;pass.forecastUpkeep=52;pass.forecastNetIncome=-208;
            pass.cityYards=house->getNumItems(Structure_ConstructionYard);
            pass.cityZonesIncludingQueued=pass.itemCount[Structure_ZoneResidential]
                +pass.itemCount[Structure_ZoneCommercial]+pass.itemCount[Structure_ZoneIndustrial];
            pass.cityStorageCapacityIncludingQueued=house->getCapacity();
            pass.cashFlow.projectedCash=-6070;pass.militaryValue=bot->minWaveValue()+1;
            return pass;
        };
        auto order=[&](QuantBot::BuildPass& pass,BuilderBase* builder,Uint32 item) {
            QuantBot::BuilderTurn turn(pass.money);turn.pBuilder=builder;turn.pStructure=builder;
            return bot->produceItemWithLogging(pass,turn,item,__LINE__,"opening_capital_fixture");
        };
        try {
            config->force.cityOpeningCapital=false;
            auto legacy=budgetPass();
            require(order(legacy,factory,Unit_MCV),"Opening-capital negative control did not buy the optional MCV");
            require(order(legacy,light,Unit_Trike),"Opening-capital negative control did not buy the early light unit");
            cancelOrders(factory);factory->upgrading=false;
            QuantBot::BuilderTurn oldUpgrade(legacy.money);oldUpgrade.pBuilder=factory;oldUpgrade.pStructure=factory;
            require(bot->upgradeWithLogging(legacy,oldUpgrade,__LINE__),
                "Opening-capital negative control did not pay for optional technology");

            config->force.cityOpeningCapital=true;
            auto spice=budgetPass();spice.spiceEconomyViable=true;
            require(order(spice,factory,Unit_MCV),
                "Tax-opening protection constrained a working spice economy's MCV");
            cancelOrders(factory);factory->upgrading=false;
            QuantBot::BuilderTurn spiceUpgrade(spice.money);spiceUpgrade.pBuilder=factory;spiceUpgrade.pStructure=factory;
            require(bot->upgradeWithLogging(spice,spiceUpgrade,__LINE__),
                "Tax-opening protection constrained a working spice economy's technology");

            auto opening=budgetPass();
            require(!order(opening,factory,Unit_MCV),"An optional MCV spent a city's unfunded working capital");
            require(order(opening,light,Unit_Trike),"Optional-capital protection suppressed military reinforcements");
            QuantBot::BuilderTurn upgrade(opening.money);upgrade.pBuilder=factory;upgrade.pStructure=factory;
            require(!bot->upgradeWithLogging(opening,upgrade,__LINE__),
                "Optional producer technology consumed the seed town's cash");
            require(factory->getProductionQueueSize()==0 && light->getProductionQueueSize()==1
                && !factory->isUpgrading(),"Optional capital changed a queue or military reinforcements were lost");
            cancelOrders(light);

            opening.defendingEconomy=true;
            require(bot->canFundProductionUpgrade(opening,factory),"Live defence lost its technology exemption");
            opening.defendingEconomy=false;opening.airEngaged=true;
            require(bot->canFundProductionUpgrade(opening,factory),"A live air threat lost its technology exemption");
            opening.airEngaged=false;opening.colonisationNeeded=true;
            require(bot->canFundCapacityMcv(opening,900,factory->getObjectID()),
                "Actual rock recovery was treated as optional yard capacity");
            opening.colonisationNeeded=false;opening.itemCount[Structure_ConstructionYard]=0;
            require(bot->canFundCapacityMcv(opening,900,factory->getObjectID()),
                "Lost-yard recovery waited for a tax base it cannot rebuild");
            require(bot->canFundProductionUpgrade(opening,yard),
                "Required yard technology was blocked with optional producer upgrades");

            auto funded=budgetPass();
            funded.forecastTax=2000;funded.forecastUpkeep=100;funded.forecastNetIncome=5000;
            funded.cashFlow.projectedCash=5000;
            funded.dangerousCrimeProperties=1;
            require(bot->cityOpeningCapitalFunded(funded,900,factory->getObjectID()),
                "Nonfinancial research readiness blocked funded tax-city capital");
            funded.dangerousCrimeProperties=0;
            require(bot->cityOpeningCapitalFunded(funded,900,factory->getObjectID()),
                "A funded city could not resume optional capital with research disabled");
            funded.money=1000;
            require(!bot->cityOpeningCapitalFunded(funded,900,factory->getObjectID()),
                "Forecast receipts were spent before the real working reserve was protected");
            QuantBot::CapitalCandidate claim;
            claim.builder=factory->getObjectID();claim.item=Unit_MCV;claim.price=900;claim.cost=900;claim.score=5000;
            funded.capitalCandidates.push_back(claim);funded.capitalChoice=0;
            funded.capitalInitialCount=funded.itemCount[Unit_MCV];funded.money=1900;
            require(bot->cityOpeningCapitalFunded(funded,900,factory->getObjectID()),
                "A capital supplier charged its own selected purchase twice");
            auto& supply=funded.capitalCandidates.front();
            supply.builder=yard->getObjectID();supply.item=Structure_StarPort;
            supply.price=bot->purchasePrice(yard,Structure_StarPort);
            supply.cost=bot->buildingCapitalCost(funded,Structure_StarPort);
            supply.foundationCost=supply.cost-supply.price;
            funded.capitalInitialCount=funded.itemCount[Structure_StarPort];
            funded.money=900+1000+supply.cost-1;
            require(!bot->cityOpeningCapitalFunded(funded,900,factory->getObjectID()),
                "An optional MCV consumed the selected Starport's protected purchase");
            funded.capitalCandidates.clear();funded.capitalChoice=-1;funded.money=3000;
            require(order(funded,factory,Unit_MCV),"A funded city never resumed optional yard construction");
            SDL_Log("CITY_OPENING_CAPITAL_PASS: optional MCV/technology preserve seed cash; military, threats and recovery remain live");
        } catch (...) {
            bot->configOverride=savedConfig;
            house->producedPower=savedProduced;house->powerRequirement=savedRequired;throw;
        }
        bot->configOverride=savedConfig;
        house->producedPower=savedProduced;house->powerRequirement=savedRequired;
    }

    // 80rock.dls: Neutral's T1 city overflowed its 1,000-yard + 1,005-refinery
    // vault for 2,870 credits before research unlocked the Silo. Higher-tech
    // cities also waited for Heavy/HighTech/Repair before the old rule ran.
    {
        const auto savedConfig=bot->configOverride;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        bot->configOverride=config;
        const int savedTech=game.techLevel, savedResearched=house->researchedTechLevel;
        StructureBase* refinery=nullptr;
        const auto buildings=structureList;
        for(auto* structure:buildings) if(structure->getOwner()==house) {
            if(structure->isABuilder()) {
                auto* builder=static_cast<BuilderBase*>(structure);
                cancelOrders(builder);builder->upgrading=false;
            }
            const Uint32 item=structure->getItemID();
            if(item==Structure_Refinery && !refinery) refinery=structure;
            else if((game.objectData.data[item][HOUSE_HARKONNEN].capacity>0 && structure!=yard)
                || item==Structure_HeavyFactory || item==Structure_HighTechFactory || item==Structure_RepairYard)
                structure->destroy();
        }
        require(refinery && house->getCapacity()==2005,"Storage fixture must retain the real 2,005-credit physical store");
        house->storedCredits=0;house->startingCredits=0;house->cityCredits=0;
        house->addCityCredits(2005);house->producedPower=10000;
        auto storagePass=[&]() {
            cancelOrders(yard);
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
            bot->planningBuilder=yard->getObjectID();
            yard->updateBuildList();
            QuantBot::BuildPass pass(0);
            bot->planBuildInventory(pass);
            pass.capitalChoice=-1;pass.citySimEnabled=true;pass.money=house->getCredits();
            pass.cityCoreInfrastructureReady=false;
            pass.forecastNetIncome=7200;pass.activeProductionBurn=0;
            pass.populationCeiling=40000;pass.cityDisplayPopulation=40000;
            pass.cityGrowthLimits.sharedZoneCap=QuantBotCityPolicy::kUnlimited;
            return pass;
        };
        try {
            game.techLevel=9;house->researchedTechLevel=9;
            config->force.storageCapacity=false;
            auto old=storagePass();
            require(bot->storageExpansionItem(old,yard)==NONE_ID,
                "Storage-off control no longer reproduces the missing production-core gate");
            require(bot->initialCityPopulation(Structure_Silo)>0 && !bot->cityAdmitsStructure(old,Structure_Silo),
                "Storage-off control must reproduce the Silo's industrial-population cap refusal");
            config->force.storageCapacity=true;
            auto on=storagePass();
            require(bot->storageExpansionItem(on,yard)==Structure_Silo,
                "A full tax city without the production core did not choose its available Silo");
            require(bot->cityAdmitsStructure(on,Structure_Silo)
                && bot->cityAdmitsStructure(on,Structure_Refinery),
                "A full city's population ceiling still rejects physical income storage");
            require(!bot->cityAdmitsStructure(on,Structure_ZoneResidential),
                "Storage relief incorrectly removed the ordinary zoning population ceiling");
            QuantBot::BuilderTurn turn(on.money);turn.pBuilder=yard;turn.pStructure=yard;
            require(bot->queueFoundedStructure(on,turn,Structure_Silo,__LINE__,"storage_capacity"),
                "Full-storage relief did not enter the real founded production queue");
            ++on.itemCount[Structure_Silo];
            require(bot->storageExpansionItem(on,yard)==NONE_ID,"Parallel yards duplicated a pending Silo");
            require(on.money>500,"A small storage purchase consumed the entire army/city budget");

            game.techLevel=1;house->researchedTechLevel=1;
            auto early=storagePass();
            require(!bot->campaignAvailableToBuild(yard,Structure_Silo)
                && bot->campaignAvailableToBuild(yard,Structure_Refinery),
                "T1 storage fixture must have a buildable Refinery and a locked Silo");
            require(bot->storageExpansionItem(early,yard)==Structure_Refinery,
                "T1 overflow relief waited for an unavailable Silo on a spiceless map");
            QuantBot::BuilderTurn earlyTurn(early.money);earlyTurn.pBuilder=yard;earlyTurn.pStructure=yard;
            require(bot->queueFoundedStructure(early,earlyTurn,Structure_Refinery,__LINE__,"storage_capacity"),
                "T1 vault fallback never entered the real founded queue");
            ++early.itemCount[Structure_Refinery];
            require(bot->storageExpansionItem(early,yard)==NONE_ID,"Parallel yards duplicated a pending refinery vault");
            cancelOrders(yard);
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
            SDL_Log("STORAGE_CAPACITY_PASS: full2,005 at40kpopulation without core; founded Silo and T1 refinery; pending vaults deduplicated");
        } catch(...) {
            game.techLevel=savedTech;house->researchedTechLevel=savedResearched;
            bot->configOverride=savedConfig;throw;
        }
        game.techLevel=savedTech;house->researchedTechLevel=savedResearched;
        bot->configOverride=savedConfig;
    }

    SDL_Log("NO_SPICE_CITY_OK");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=no_spice_city outcome=passed");
}
