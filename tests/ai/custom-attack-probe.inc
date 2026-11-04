#include "threat-turret-probe.inc"
#include <dunecity/CitySimulation.h>
#include <structures/RepairYard.h>
#include <units/GroundUnit.h>
#include <units/Harvester.h>
#include <array>
#include <map>
#include <tuple>

// Both switch states use the same real-engine roster. Drive the production
// economy predicate, repair owner, selector and attack() entry point rather
// than duplicating their decisions in a test policy.
void runExhaustedArmyProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if (!ok) { SDL_Log("EXHAUSTED_ARMY_FAIL: %s",why);throw std::runtime_error(why); }
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for (const auto& player:house->getPlayerList())
        if (auto* candidate=dynamic_cast<QuantBot*>(player.get())) bot=candidate;
    require(bot,"Exhausted army fixture missing bot");
    StructureBase* defendedBuilding=nullptr;
    for (auto* structure:structureList)
        if (structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard
            && structure->getHealth()>0) { defendedBuilding=structure;break; }
    require(defendedBuilding,"Exhausted army fixture missing a real defence asset");

    const auto oldUnits=unitList;
    const auto oldStructures=structureList;
    const auto oldConfig=bot->configOverride;
    const auto oldDifficulty=bot->difficulty;
    const auto oldMode=bot->gameMode;
    const auto oldRole=bot->role;
    const auto oldType=game.gameType;
    const auto oldTech=game.techLevel;
    const auto oldResearchedTech=house->researchedTechLevel;
    const auto oldCity=game.citySimEnabled_;
    const bool hadSimulation=bool(game.citySimulation_);
    const auto oldCityState=hadSimulation
        ? game.citySimulation_->getHouseState(house->getHouseID()) : DuneCity::HouseCityState{};
    const auto oldStarting=house->startingCredits;
    const auto oldStored=house->storedCredits;
    const auto oldCityCredits=house->cityCredits;
    std::array<int,Num_ItemID> oldItemCounts;
    std::copy_n(house->numItem,Num_ItemID,oldItemCounts.begin());
    const auto oldManual=bot->manualUnitOrders;
    const auto oldDefence=bot->defenceAssignments;
    const auto oldWave=bot->customWave;
    const auto oldPosture=bot->armyPosture;
    const auto oldTimer=bot->attackTimer;
    const auto oldLimit=bot->militaryValueLimit;
    const auto oldSpice=bot->lastCalculatedSpice;
    const auto oldForce=bot->forceVariantOverride;
    const auto oldFormation=bot->waveFormationOverride;
    const auto oldStrategyPicked=bot->strategyPicked;
    const auto oldBundleSince=bot->bundleGateSince;
    const auto oldForceStats=bot->forceStats;
    struct ResourceTile { Tile* tile; int terrain; FixPoint spice; };
    std::vector<ResourceTile> resources;
    std::vector<UnitBase*> created;
    std::vector<StructureBase*> buildings;
    auto invalidate=[&] { bot->exhaustedEconomyCycle=std::numeric_limits<Uint32>::max(); };
    auto cleanup=[&] {
        for (auto* structure:buildings)
            if (auto* yard=dynamic_cast<RepairYard*>(structure);yard && !yard->isFree())
                yard->deployRepairUnit();
        for (auto* unit:created) unit->destroy();
        for (auto* structure:buildings) structure->destroy();
        unitList=oldUnits;structureList=oldStructures;
        for (const auto& resource:resources) {
            resource.tile->setType(resource.terrain);
            if (resource.spice>0) resource.tile->setSpice(resource.spice);
        }
        bot->configOverride=oldConfig;bot->difficulty=oldDifficulty;
        bot->gameMode=oldMode;bot->role=oldRole;
        bot->manualUnitOrders=oldManual;bot->defenceAssignments=oldDefence;
        bot->customWave=oldWave;bot->armyPosture=oldPosture;bot->attackTimer=oldTimer;
        bot->militaryValueLimit=oldLimit;bot->lastCalculatedSpice=oldSpice;
        bot->forceVariantOverride=oldForce;bot->waveFormationOverride=oldFormation;
        bot->strategyPicked=oldStrategyPicked;
        bot->bundleGateSince=oldBundleSince;bot->forceStats=oldForceStats;
        house->startingCredits=oldStarting;house->storedCredits=oldStored;
        house->cityCredits=oldCityCredits;
        std::copy(oldItemCounts.begin(),oldItemCounts.end(),house->numItem);
        house->researchedTechLevel=oldResearchedTech;
        game.gameType=oldType;game.techLevel=oldTech;game.citySimEnabled_=oldCity;
        if (hadSimulation) game.citySimulation_->getHouseStateMut(house->getHouseID())=oldCityState;
        else game.citySimulation_.reset();
        invalidate();
    };
    try {
        std::vector<Coord> sites;
        std::map<int,std::vector<Coord>> sitesByRegion;
        for (int y=1;y<currentGameMap->getSizeY()-1 && sites.size()<8;++y)
            for (int x=1;x<currentGameMap->getSizeX()-1 && sites.size()<8;++x) {
                auto* tile=currentGameMap->getTile(x,y);
                if (tile->hasAGroundObject() || tile->isMountain()) continue;
                const int region=currentGameMap->vehicleTerrainRegion(Coord(x,y));
                if (region==0) continue;
                auto& connected=sitesByRegion[region];
                connected.emplace_back(x,y);
                if (connected.size()==8) sites=std::move(connected);
            }
        require(sites.size()==8,"Exhausted army fixture needs eight free ground tiles");
        auto* field=currentGameMap->getTile(sites.back());
        // Make the zero-spice observation true on the map, not just in the
        // survey cache. Restore original resource tiles on every exit.
        for (int y=0;y<currentGameMap->getSizeY();++y)
            for (int x=0;x<currentGameMap->getSizeX();++x) {
                auto* tile=currentGameMap->getTile(x,y);
                if (tile==field || tile->hasSpice() || tile->isSpiceBloom() || tile->isSpecialBloom()) {
                    resources.push_back({tile,tile->getType(),tile->getSpice()});
                    tile->setType(Terrain_Sand);
                }
            }
        auto spawn=[&](Uint32 item) {
            auto* unit=house->createUnit(item);
            require(unit,"Exhausted army fixture unit creation failed");
            created.push_back(unit);unit->deploy(sites[created.size()-1]);return unit;
        };
        auto* first=spawn(Unit_Trooper);
        auto* second=spawn(Unit_Trooper);
        auto* damaged=spawn(Unit_Quad);
        auto* manual=spawn(Unit_Quad);
        auto* defender=spawn(Unit_Quad);
        auto* saboteur=spawn(Unit_Saboteur);
        auto* worker=dynamic_cast<Harvester*>(spawn(Unit_Harvester));
        require(worker,"Exhausted army fixture needs a real harvester");
        unitList.clear();for (auto* unit:created) unitList.push_back(unit);
        // The fixture hides the original roster, so hide its inventory too:
        // militaryUnitValue() deliberately uses the House item counters.
        for (const auto* unit:oldUnits)
            if (unit->getOwner()==house) --house->numItem[unit->getItemID()];
        structureList.clear();structureList.push_back(defendedBuilding);
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->recovery.enabled=false; // Isolate rebuild gates, not tactical withdrawal.
        config->force.exhaustedArmyDispatch=true;
        bot->configOverride=config;
        bot->difficulty=QuantBot::Difficulty::Hard;bot->gameMode=QuantBot::GameMode::Custom;
        bot->role=AIRole::Both;game.gameType=GameType::CustomGame;
        bot->forceVariantOverride=0; // No test-style bundling, garrison or reserve hold.
        bot->waveFormationOverride=QuantBotConfig::kWaveFormationNone;
        bot->strategyPicked=false;
        bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
        bot->militaryValueLimit=80000;bot->lastCalculatedSpice=0;
        game.techLevel=9;house->researchedTechLevel=9;
        house->numItem[Structure_RepairYard]=0;
        house->startingCredits=0;house->storedCredits=0;house->cityCredits=0;
        game.citySimEnabled_=false;
        require(house->getTechLevel()>4 && !house->hasRepairYard() && !bot->lightFirstOpening(),
            "Exhausted army fixture does not exercise the repair-yard prerequisite");
        require(bot->minWaveUnits()==6 && bot->minWaveValue()==3000,
            "Exhausted army fixture lost the legacy six-unit/3000-value floor");

        auto reset=[&] {
            bot->manualUnitOrders.clear();bot->defenceAssignments.clear();
            bot->customWave.members.clear();bot->customWave.front=NONE_ID;
            bot->bundleGateSince=0;
            for (auto* unit:created) {
                unit->setHealth(unit->getMaxHealth());unit->setTarget(nullptr);
                bot->doSetAttackMode(unit,GUARD);unit->setForced(false);
            }
            damaged->setHealth(damaged->getMaxHealth()/10);
            manual->setHealth(manual->getMaxHealth()/10);
            defender->setHealth(defender->getMaxHealth()/10);
            saboteur->setHealth(saboteur->getMaxHealth()/10);
            bot->doMove2Pos(manual,sites[3].x,sites[3].y,true);
            bot->onHumanUnitOrder(manual->getObjectID());
            bot->doSetAttackMode(defender,AREAGUARD);
            bot->doMove2Pos(defender,defendedBuilding->getX(),defendedBuilding->getY(),true);
            bot->defenceAssignments[defender->getObjectID()]=defendedBuilding->getObjectID();
            bot->doSetAttackMode(saboteur,SABOTAGE);
            invalidate();
        };
        auto order=[](const UnitBase* unit) {
            return std::make_tuple(unit->getAttackMode(),unit->getDestination(),unit->getGuardPoint(),
                                   unit->getTarget(),unit->wasForced());
        };
        auto expectHeld=[&](const char* why) {
            reset();
            require(!bot->exhaustedArmyEconomy(),why);
            require(bot->reserveDamagedUnitForRepair(damaged),
                "A recoverable economy released a damaged unit's repair reservation");
            bot->launchGroundHunt();
            require(first->getAttackMode()!=HUNT && second->getAttackMode()!=HUNT
                && damaged->getAttackMode()!=HUNT,why);
            bot->attack(bot->militaryUnitValue());
            require(first->getAttackMode()!=HUNT && second->getAttackMode()!=HUNT
                && damaged->getAttackMode()!=HUNT,why);
        };
        auto expectLaunched=[&](bool throughAttack) {
            reset();
            require(bot->exhaustedArmyEconomy(),"A truly exhausted Custom economy was not recognised");
            require(bot->militaryUnitValue()<3000,
                "Exhausted army fixture is not below the legacy military-value floor");
            require(bot->declinesUnfundedRepair(damaged) && !bot->reserveDamagedUnitForRepair(damaged)
                && bot->unitRole(damaged,{QuantBot::UnitRole::Repair})==QuantBot::UnitRole::Free,
                "An unaffordable AI repair reservation kept owning its unit");
            require(bot->humanControls(manual) && bot->activeDefenceAssignment(defender),
                "Exhausted army fixture did not establish higher-priority owners");
            for (auto* unit:{manual,defender,saboteur})
                require(!bot->declinesUnfundedRepair(unit),
                    "The repair release gate claimed a human, defender or saboteur");
            const auto manualOrder=order(manual),defenderOrder=order(defender),sabotageOrder=order(saboteur);
            if (throughAttack) bot->attack(bot->militaryUnitValue());
            else bot->launchGroundHunt();
            for (auto* unit:{first,second,damaged})
                require(unit->getAttackMode()==HUNT && bot->customWave.members.count(unit->getObjectID()),
                    "An exhausted sub-minimum army did not launch and join the real wave");
            require(order(manual)==manualOrder && bot->humanControls(manual),
                "Exhausted dispatch stole a human order");
            require(order(defender)==defenderOrder && bot->activeDefenceAssignment(defender),
                "Exhausted dispatch stole a live defence order");
            require(order(saboteur)==sabotageOrder,"Exhausted dispatch stole a saboteur order");
            for (auto* unit:{manual,defender,saboteur,static_cast<UnitBase*>(worker)})
                require(!bot->customWave.members.count(unit->getObjectID()),
                    "Exhausted dispatch recruited a protected order or worker");
        };

        const QuantBotConfig defaults;
        require(defaults.force.exhaustedArmyDispatch && getQuantBotConfig().force.exhaustedArmyDispatch,
            "ExhaustedArmyDispatch is not default-on in compiled and shipped config");
        config->force.exhaustedArmyDispatch=false;
        expectHeld("Legacy OFF dispatched an exhausted sub-minimum army");
        config->force.exhaustedArmyDispatch=true;
        expectLaunched(false);
        expectLaunched(true);
        config->force.exhaustedArmyDispatch=false;
        expectHeld("Restoring legacy OFF retained the exhausted dispatch bypass");
        config->force.exhaustedArmyDispatch=true;

        field->setSpice(500);bot->lastCalculatedSpice=500;
        require(currentGameMap->vehicleTerrainRegion(worker->getLocation())
                ==currentGameMap->vehicleTerrainRegion(sites.back()),
            "Exhausted army spice control is not reachable by its harvester");
        expectHeld("Harvestable spice did not inhibit exhausted dispatch");
        field->setType(Terrain_Sand);bot->lastCalculatedSpice=0;
        worker->setAmountOfSpice(1);
        expectHeld("Pending harvester cargo did not inhibit exhausted dispatch");
        worker->setAmountOfSpice(0);

        // Construct the funding boundary from actual content and real offers;
        // the exhaustion decision below remains the production predicate.
        int cheapest=INT32_MAX;
        for (Uint32 item=Unit_FirstID;item<Num_ItemID;++item) {
            if (!isUnit(item) || isAmbientUnit(item)) continue;
            const auto& spec=game.objectData.data[item][house->getHouseID()];
            if (spec.enabled && spec.price>0 && spec.techLevel<=house->getTechLevel())
                cheapest=std::min(cheapest,spec.price);
        }
        for (const auto& offer:static_cast<BuilderBase*>(defendedBuilding)->getBuildList())
            if (isUnit(offer.itemID) && offer.price>0
                && bot->campaignAvailableToBuild(static_cast<BuilderBase*>(defendedBuilding),offer.itemID))
                cheapest=std::min(cheapest,int(offer.price));
        require(cheapest!=INT32_MAX,"Exhausted army fixture has no enabled unit funding boundary");
        house->startingCredits=FixPoint(cheapest)-FixPoint(1)/2;
        reset();
        require(bot->exhaustedArmyEconomy(),"A fractional shortfall was rounded up to a funded purchase");
        house->startingCredits=cheapest;
        expectHeld("Exact cash for the cheapest unit did not inhibit exhausted dispatch");
        house->startingCredits=0;

        game.citySimEnabled_=true;
        if (!game.citySimulation_) {
            game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
            game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
        }
        auto& city=game.citySimulation_->getHouseStateMut(house->getHouseID());
        city=DuneCity::HouseCityState{};
        city.taxBaseEighths=65536;city.avgLandValue=128;city.taxRate=7;
        expectHeld("Positive city income did not inhibit exhausted dispatch");
        city.taxBaseEighths=0;
        expectLaunched(true);

        // Build a real yard, admit actual units through setGettingRepaired(),
        // and let attack/launch release its jobs. No mock reservation or bay.
        Coord yardOrigin=Coord::Invalid();
        for (int y=3;y+8<currentGameMap->getSizeY() && yardOrigin.isInvalid();++y)
            for (int x=3;x+8<currentGameMap->getSizeX() && yardOrigin.isInvalid();++x) {
                bool clear=true;
                for (int dy=0;dy<8 && clear;++dy) for (int dx=0;dx<8;++dx)
                    if (currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) {clear=false;break;}
                if (clear) yardOrigin=Coord(x,y);
            }
        require(yardOrigin.isValid(),"Exhausted army repair fixture needs a clear eight-tile square");
        for (int dy=0;dy<8;++dy) for (int dx=0;dx<8;++dx) {
            auto* tile=currentGameMap->getTile(yardOrigin+Coord(dx,dy));
            if (std::none_of(resources.begin(),resources.end(),[&](const auto& saved) {return saved.tile==tile;}))
                resources.push_back({tile,tile->getType(),tile->getSpice()});
            tile->setType(Terrain_Rock);
        }
        auto* lot=house->placeStructure(NONE_ID,Structure_ZoneResidential,
            yardOrigin.x+5,yardOrigin.y+5,true,true);
        require(lot,"Exhausted army future-income lot placement failed");buildings.push_back(lot);
        expectHeld("A developing city lot lost its future-income protection before its first taxpayer");
        game.citySimEnabled_=false;
        auto* yard=dynamic_cast<RepairYard*>(house->placeStructure(NONE_ID,Structure_RepairYard,
            yardOrigin.x+1,yardOrigin.y+1,true,true));
        require(yard,"Exhausted army repair-yard placement failed");buildings.push_back(yard);
        auto* carrier=spawn(Unit_Carryall);unitList.push_back(carrier);
        require(house->hasCarryalls(),"Exhausted army automatic-repair control has no carryall");

        config->force.exhaustedArmyDispatch=false;reset();
        damaged->doRepair();
        require(damaged->getTarget()==yard && damaged->goingToRepairYard,
            "Exhausted army fixture did not establish a real repair-yard booking");
        const auto humanOrder=order(manual),defenderOrder=order(defender),sabotageOrder=order(saboteur);
        bot->launchGroundHunt();
        require(damaged->getTarget()==yard && damaged->goingToRepairYard,
            "Legacy OFF cancelled an AI repair trip");
        config->force.exhaustedArmyDispatch=true;invalidate();
        bot->launchGroundHunt();
        require(damaged->getTarget()!=yard && !damaged->goingToRepairYard
            && damaged->getAttackMode()==HUNT,
            "Exhausted dispatch did not cancel and launch an unaffordable AI repair trip");
        require(order(manual)==humanOrder && order(defender)==defenderOrder
            && order(saboteur)==sabotageOrder,
            "Repair-trip release cancelled a human, defender or saboteur order");
        require(damaged->getHealth()<damaged->getMaxHealth()/2 && !damaged->wasForced()
            && damaged->isActive() && yard->isFree() && house->hasCarryalls(),
            "Released repair fixture would not otherwise qualify for engine automatic repair");
        damaged->checkPos();
        require(damaged->getTarget()!=yard && !damaged->goingToRepairYard,
            "The engine immediately rebooked a released unaffordable AI repair");

        // A real yard admits only one inbound booking. Exercise each protected
        // traveller separately rather than asking one bay to book three units.
        for (auto* protectedUnit:{manual,defender}) {
            config->force.exhaustedArmyDispatch=false;reset();
            protectedUnit->doRepair();
            require(protectedUnit->getTarget()==yard && protectedUnit->goingToRepairYard,
                "Protected repair traveller was never booked with the real yard");
            const auto protectedOrder=order(protectedUnit);
            config->force.exhaustedArmyDispatch=true;invalidate();
            bot->launchGroundHunt();
            require(order(protectedUnit)==protectedOrder && protectedUnit->goingToRepairYard,
                "Exhausted dispatch cancelled a human or defender's repair trip");
        }

        config->force.exhaustedArmyDispatch=false;reset();
        damaged->doRepair();damaged->setGettingRepaired();
        require(!damaged->isActive() && yard->getContainedRepairUnit()==damaged,
            "Exhausted army fixture did not enter the actual repair bay");
        bot->launchGroundHunt();
        require(!damaged->isActive() && yard->getContainedRepairUnit()==damaged,
            "Legacy OFF released an unpaid repair-yard occupant");
        config->force.exhaustedArmyDispatch=true;invalidate();
        bot->attack(bot->militaryUnitValue());
        require(yard->isFree() && damaged->isActive() && damaged->getAttackMode()==HUNT,
            "attack() did not deploy and launch the unaffordable AI yard occupant");

        // Exercise the real debit gate, not just a model of per-point cost.
        // Cash below 0.5 rounds to zero and cannot buy even the last point.
        for (const int missing:{1,5}) for (const bool funded:{false,true}) {
            reset();
            damaged->setHealth(damaged->getMaxHealth()-missing);
            const FixPoint required=FixPoint(1)/2+(missing-1)*UNIT_REPAIRCOST;
            house->startingCredits=required-(funded?FixPoint(0):FixPoint(1)/1000);
            invalidate();
            damaged->doRepair();damaged->setGettingRepaired();
            damaged->setGuardPoint(yard->getLocation());
            require(yard->getContainedRepairUnit()==damaged && !damaged->isActive(),
                "Fractional repair fixture did not enter the actual repair bay");
            require(bot->declinesUnfundedRepair(damaged)!=funded,
                "Repair ownership ignored the real rounded-credit payment threshold");
            for (int tick=0;tick<=missing;++tick) yard->updateStructureSpecificStuff();
            require((damaged->getHealth()==damaged->getMaxHealth())==funded,
                "Actual repair progress disagreed with the cash-to-finish boundary");
            if (!funded)
                require(yard->getContainedRepairUnit()==damaged && !damaged->isActive(),
                    "The underfunded repair control did not actually stall in its bay");
            bot->releaseExhaustedArmyRepairs();
            require(yard->isFree() && damaged->isActive(),
                "The real fractional-cash repair hold was not released when it could not finish");
            house->startingCredits=0;
        }

        for (auto* protectedUnit:{manual,defender}) {
            config->force.exhaustedArmyDispatch=false;reset();
            protectedUnit->doRepair();protectedUnit->setGettingRepaired();
            require(yard->getContainedRepairUnit()==protectedUnit && !protectedUnit->isActive(),
                "Protected repair occupant was never admitted to the real bay");
            config->force.exhaustedArmyDispatch=true;invalidate();
            bot->launchGroundHunt();
            require(yard->getContainedRepairUnit()==protectedUnit && !protectedUnit->isActive(),
                "Exhausted dispatch evicted a human or defender's repair-yard occupant");
            yard->deployRepairUnit();
        }

        // Compare actual legacy campaign orders, rather than assuming those
        // paths must hold: a shared-house helper still has its own reserve rule.
        bot->gameMode=QuantBot::GameMode::Campaign;
        for (auto type:{GameType::Campaign,GameType::CampaignCoop}) {
            game.gameType=type;
            config->force.exhaustedArmyDispatch=false;reset();
            bot->launchGroundHunt();bot->attack(bot->militaryUnitValue());
            std::vector<decltype(order(first))> legacy;
            for (auto* unit:created) legacy.push_back(order(unit));
            config->force.exhaustedArmyDispatch=true;reset();
            require(!bot->exhaustedArmyEconomy(),"A campaign acquired the Custom economy bypass");
            require(bot->reserveDamagedUnitForRepair(damaged),
                "A campaign lost its legacy repair reservation");
            bot->launchGroundHunt();bot->attack(bot->militaryUnitValue());
            for (size_t i=0;i<created.size();++i)
                require(order(created[i])==legacy[i],
                    "Exhausted dispatch changed the actual legacy campaign orders");
        }
        SDL_Log("EXHAUSTED_ARMY_PASS: off_on_off=held_launched_held spice_cash_cargo_city=held authority=retained");
    } catch (...) { cleanup();throw; }
    cleanup();
}

// Real production selector and reserve movement; never linked into the game.
void runCustomAttackProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Custom attack fixture missing bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->role=AIRole::Both;
    bot->manualUnitOrders.clear();bot->defenceAssignments.clear();
    std::vector<UnitBase*> army;
    for(int y=1;y<currentGameMap->getSizeY()-1 && army.size()<76;++y)
        for(int x=1;x<currentGameMap->getSizeX()-1 && army.size()<76;++x) {
            const auto* tile=currentGameMap->getTile(x,y);
            if(tile->hasAGroundObject() || tile->isMountain())continue;
            auto* unit=house->createUnit(Unit_Trooper);
            require(unit,"Custom attack fixture creation failed");
            unit->deploy(Coord(x,y));army.push_back(unit);
        }
    require(army.size()==76,"Custom attack fixture needs 76 empty tiles");
    auto saved=unitList;unitList.clear();
    for(auto* unit:army)unitList.push_back(unit);
    try {
        auto reset=[&](QuantBot::Difficulty difficulty) {
            bot->difficulty=difficulty;
            for(auto* unit:army) {bot->doSetAttackMode(unit,GUARD);unit->setForced(false);}
        };
        auto hunters=[&] {
            int n=0;for(auto* unit:army)if(unit->getAttackMode()==HUNT)++n;return n;
        };
        // Verify compiled defaults and the independently loaded shipped config.
        // Wave sizes below depend on owned troops, not the production target.
        const QuantBotConfig defaults;
        require(defaults.easy.attackForceMilitaryValueRatio==0.25f
            && defaults.medium.attackForceMilitaryValueRatio==0.40f
            && defaults.hard.attackForceMilitaryValueRatio==0.50f
            && defaults.brutal.attackForceMilitaryValueRatio==0.60f,
            "Default custom attack shares are not 25/40/50/60 percent");
        require(defaults.easy.militaryValueLimitCustomLargeMap==15000
            && defaults.easy.militaryValueLimitCustomHugeMap==25000
            && defaults.medium.militaryValueLimitCustomLargeMap==25000
            && defaults.medium.militaryValueLimitCustomHugeMap==40000,
            "Default Easy/Medium large and huge military limits drifted");
        const auto& loaded=getQuantBotConfig();
        require(loaded.easy.militaryValueLimitCustomLargeMap==15000
            && loaded.easy.militaryValueLimitCustomHugeMap==25000
            && loaded.medium.militaryValueLimitCustomLargeMap==25000
            && loaded.medium.militaryValueLimitCustomHugeMap==40000,
            "Shipped config did not load the approved military limits");
        // An enormous production target must not enlarge this 7600-value army's attack.
        bot->militaryValueLimit=999999;
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0.25f;
        getQuantBotConfig().medium.attackForceMilitaryValueRatio=0.40f;
        // 76 troopers at 100 credits: 25 percent buys 19, 40 percent buys 30.
        reset(QuantBot::Difficulty::Easy);bot->launchGroundHunt();
        require(hunters()==19,"Custom Easy did not send its 25-percent share");
        // Forced movement must not remove survivors from the counted roster.
        for(auto* unit:army)if(unit->getAttackMode()==HUNT)unit->setForced(true);
        bot->launchGroundHunt();
        require(hunters()==19,"A repeated dispatch or forced hunter exceeded the share");
        // Custom Medium left the share path: it launches whole-army waves like Hard and Brutal
        // (docs/ai-scenario-matrix.md, "Difficulty ladder"; quantbot_whole_army_dispatch).
        reset(QuantBot::Difficulty::Medium);
        require(bot->autonomousCustomArmy(),"Custom Medium is not an autonomous custom army");
        // The difficulty steps (docs/ai-scenario-matrix.md, "Difficulty separation"): the production
        // strategy is Hard's and Brutal's alone.
        require(!bot->playsStrategy(),"Custom Medium plays the Hard/Brutal strategy");
        reset(QuantBot::Difficulty::Hard);
        require(bot->playsStrategy(),"Custom Hard lost its strategy");
        reset(QuantBot::Difficulty::Brutal);
        require(bot->playsStrategy(),"Custom Brutal lost its strategy");
        reset(QuantBot::Difficulty::Easy);
        require(!bot->playsStrategy(),"Custom Easy plays a strategy");
        // The configured harvester limits take effect in custom games above 64x64 and rise with the
        // difficulty; up to 64x64 the spice budget alone decides (no configured ceiling).
        const bool limitedMap=currentGameMap->getSizeX()*currentGameMap->getSizeY()>4096;
        int previousLimit=0;
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                             QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            reset(difficulty);
            const int limit=bot->configuredCustomHarvesterLimit();
            require(limitedMap ? limit>previousLimit : limit==0,
                "A custom difficulty's harvester limit does not exceed the one below (or binds on a small map)");
            previousLimit=limit;
        }
        bot->role=AIRole::Builder;
        require(bot->configuredCustomHarvesterLimit()==0,"A support controller took the custom harvester limit");
        bot->role=AIRole::Both;
        // A Fighter (docs/ai-roles.md) commands the army as Both does, but its wave readiness
        // leaves out the units its human partner is ordering: those never join the wave.
        bot->role=AIRole::Fighter;
        reset(QuantBot::Difficulty::Medium);
        require(bot->autonomousCustomArmy(),"A Fighter lost the autonomous custom army");
        bot->role=AIRole::Both;
        const int wholeArmy=bot->militaryUnitValue();
        for(int i=0;i<4;++i)bot->onHumanUnitOrder(army[i]->getObjectID());
        require(bot->militaryUnitValue()==wholeArmy,"Both stopped counting human-ordered units");
        bot->role=AIRole::Fighter;
        const int trooperPrice=game.objectData.data[Unit_Trooper][house->getHouseID()].price;
        require(bot->militaryUnitValue()==wholeArmy-4*trooperPrice,
            "A Fighter counted its human partner's ordered units as ready");
        bot->manualUnitOrders.clear();
        require(bot->militaryUnitValue()==wholeArmy,"A Fighter lost units its human released");
        bot->role=AIRole::Both;
        reset(QuantBot::Difficulty::Easy);
        for(auto difficulty:{QuantBot::Difficulty::Easy}) {
            reset(difficulty);bot->launchGroundHunt();
            auto* reserve=army.back();
            bot->squadRallyLocation=army[army.size()-2]->getLocation();
            bot->moveToOptimalSquadPosition(reserve,1);
            require(reserve->getGuardPoint()==bot->squadRallyLocation,
                "Reserve followed offensive centroid instead of home rally");
            auto* hunter=army.front();const auto guard=hunter->getGuardPoint();
            hunter->setForced(false);bot->moveToOptimalSquadPosition(hunter,1);
            require(hunter->getAttackMode()==HUNT && hunter->getGuardPoint()==guard,
                "Home rally stole an active attacker");
        }
        reset(QuantBot::Difficulty::Easy);bot->launchGroundHunt();
        game.gameType=GameType::Campaign;
        auto* helperReserve=army.back();
        bot->doMove2Pos(helperReserve,helperReserve->getX(),helperReserve->getY(),false);
        helperReserve->setForced(false);
        const auto assaultCenter=bot->findSquadCenter(house->getHouseID());
        bot->moveToOptimalSquadPosition(helperReserve,1);
        require(blockDistance(helperReserve->getGuardPoint(),assaultCenter)<=3,
            "Custom reserve rule changed campaign helper movement");
        game.gameType=GameType::CustomGame;
        reset(QuantBot::Difficulty::Easy);
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0;
        bot->launchGroundHunt();require(hunters()==0,"Zero percent still attacked");
        // Restore normal percentage and expose only four 100-credit troopers.
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0.25f;
        unitList.clear();for(int i=0;i<4;++i)unitList.push_back(army[i]);
        bot->launchGroundHunt();require(hunters()==1,"Small army ignored 25-percent budget");
        bot->launchGroundHunt();require(hunters()==1,"Small army leaked its reserve on repeat");
    } catch(...) {unitList=saved;throw;}
    unitList=saved;
    runThreatTurretProbe(game);
    runExhaustedArmyProbe(game);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=custom_attack outcome=passed");
}
