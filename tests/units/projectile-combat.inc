// Full headless Game::updateGameState attack passes, using real weapon selection.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <sand.h>
#include <Definitions.h>
#include <GasDeviationPolicy.h>
#include <misc/Random.h>
#include <mod/ModManager.h>
#include <units/Ornithopter.h>
#include <units/Launcher.h>
#include <units/Deviator.h>
#include <units/Carryall.h>
#include <structures/RocketTurret.h>
#include <fstream>
#include <memory>
#include <algorithm>

namespace {

// Check deterministic hit rolls, then real gas shots and loaded-carrier reversion.
void runDeviatorGasProbe(const std::string& mod, const std::string& mapData, const std::string& outDir) {
    auto require=[](bool ok,const char* why) {if(!ok)throw std::runtime_error(why);};
    std::ofstream csv(outDir+"/"+mod+"-deviator.csv");
    csv << "phase,case,seed,converted,expected,hp_lost,cargo_kept,reverted,ticks\n";

    auto newGame=[&](int seed) {
        auto options=settings.gameOptions;
        options.rocketTurretsNeedPower=false;
        options.fogOfWar=false;
        GameInitSettings init("projectiles.ini",mapData,"Deviator gas",true,options);
        init.randomSeed=seed;
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES,1);
        humans.addPlayerInfo({"Speed tester","HumanPlayer"});init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN,2);
        enemies.addPlayerInfo({"Enemy","HumanPlayer"});init.addHouseInfo(enemies);
        auto game=std::make_unique<Game>();currentGame=game.get();
        game->initGame(init);game->initializeGameLoop();
        return game;
    };

    // ---- Phase 1: exact probability boundary and RNG accounting -------------
    {
        auto game=newGame(7);
        auto* captor=game->getHouse(HOUSE_ATREIDES);
        auto* victimHouse=game->getHouse(HOUSE_HARKONNEN);
        require(captor!=nullptr && victimHouse!=nullptr,"Gas fixture houses missing");

        auto* shooter=captor->createUnit(Unit_Deviator,true);
        shooter->deploy(Coord(5,20));
        auto* friendly=captor->createUnit(Unit_Ornithopter,true);
        friendly->deploy(Coord(5,25));friendly->setVisible(VIS_ALL,true);
        require(!shooter->canAttack(friendly),"Deviator targets a friendly aircraft");

        // Far apart, so each impact can reach only one unit and therefore draws once.
        struct Case { int item; Coord where; bool eligible; };
        const Case cases[]={{Unit_Tank,Coord(10,6),true},
                            {Unit_Ornithopter,Coord(35,6),true},
                            {Unit_Carryall,Coord(35,15),true},
                            {Unit_Frigate,Coord(35,25),false}};
        for(const auto& probe : cases) {
            auto* victim=victimHouse->createUnit(probe.item,true);
            require(victim!=nullptr,"Could not create a gas boundary target");
            victim->deploy(probe.where);
            victim->setVisible(VIS_ALL,true);
            require(shooter->canAttack(victim)==probe.eligible,"Deviator target eligibility differs from gas impact");
            const FixPoint startHealth=victim->getHealth();
            int hits=0,misses=0;
            const int trials=probe.eligible?60:8;
            for(int trial=1;trial<=trials;++trial) {
                const Uint32 seed=(Uint32(trial)*2654435761U)&0x7fffffffU;
                if(victim->wasDeviated())victim->quitDeviation();
                require(victim->getOwner()==victimHouse,"Gas boundary target did not reset");
                game->randomGen.setSeed(seed);
                Random peek(seed);
                const FixPoint draw=peek.randFixPoint();
                const bool expected=probe.eligible && draw<GasDeviationPolicy::conversionChance();
                // Non-zero damage on purpose: gas must still remove no hit points.
                currentGameMap->damage(NONE_ID,captor,victim->getCenterPoint(),Bullet_DRocket,
                                       100,2*TILESIZE,victim->isAFlyingUnit(),false);
                const bool converted=victim->getOwner()==captor;
                require(converted==expected,"Gas conversion did not follow the shared 0.80 boundary");
                require(victim->getHealth()==startHealth,"Gas removed hit points");
                if(converted)require(victim->deviationTimer==DEVIATIONTIME,"Gas set a short deviation timer");
                require(game->randomGen.getSeed()==(probe.eligible?peek.getSeed():Uint32(seed)),
                        "Gas impact did not consume exactly the expected random draws");
                converted?++hits:++misses;
                csv<<"boundary,"<<probe.item<<','<<seed<<','<<converted<<','<<expected<<",0,-1,-1,0\n";
            }
            if(probe.eligible) {
                SDL_Log("GAS_BOUNDARY item=%d hits=%d misses=%d of %d",probe.item,hits,misses,trials);
                require(hits>0,"The shared chance never converted an eligible target");
                require(misses>0,"The shared chance never missed, so the boundary is untested");
            } else {
                require(hits==0,"An ineligible target was converted by gas");
            }
            if(victim->wasDeviated())victim->quitDeviation();
        }

        // A gas rocket must neither damage nor capture a building.
        auto* building=victimHouse->placeStructure(NONE_ID,Structure_RocketTurret,20,15,true,true);
        require(building!=nullptr,"Could not place the gas fixture building");
        building->setVisible(VIS_ALL,true);
        require(!shooter->canAttack(building),"Deviator targets a structure");
        const FixPoint buildingHealth=building->getHealth();
        for(int seed=1;seed<=4;++seed) {
            game->randomGen.setSeed(seed);
            currentGameMap->damage(NONE_ID,captor,building->getCenterPoint(),Bullet_DRocket,
                                   100,2*TILESIZE,false,false);
            require(building->getHealth()==buildingHealth,"Gas damaged a building");
            require(building->getOwner()==victimHouse,"Gas captured a building");
            require(game->randomGen.getSeed()==Uint32(seed),"Gas drew randomness for a building");
            csv<<"structure,"<<Structure_RocketTurret<<','<<seed<<",0,0,0,-1,-1,0\n";
        }
        game.reset();currentGame=nullptr;
    }

    // ---- Phase 2: real rockets against hostile aircraft ---------------------
    int conversions[2]={};
    for(int kind=0;kind<2;++kind) for(int seed : {11,22,33}) {
        auto game=newGame(seed);
        auto* captor=game->getHouse(HOUSE_ATREIDES);
        auto* victimHouse=game->getHouse(HOUSE_HARKONNEN);
        const Coord site(20,15);
        auto* deviator=captor->createUnit(Unit_Deviator,true);
        require(deviator!=nullptr,"Could not create the Deviator");
        deviator->deploy(site);
        deviator->setVisible(VIS_ALL,true);
        // Ordinary area guard, so the conversion goes through real target selection
        // and therefore through Deviator::canAttack.
        deviator->doSetAttackMode(AREAGUARD);

        // The aircraft crosses the Deviator's cover without shooting back, so the
        // scenario measures the gas weapon and not an exchange of fire.
        auto* aircraft=victimHouse->createUnit(kind==0?Unit_Ornithopter:Unit_Carryall,true);
        require(aircraft!=nullptr,"Could not create the hostile aircraft");
        aircraft->deploy(Coord(kind==0?8:24,site.y));
        aircraft->setVisible(VIS_ALL,true);
        aircraft->doSetAttackMode(STOP);

        Uint32 cargoID=NONE_ID;
        if(kind==1) {
            auto* cargo=victimHouse->createUnit(Unit_Quad,true);
            require(cargo!=nullptr,"Could not create the Carryall cargo");
            cargo->deploy(Coord(24,site.y+2));
            cargoID=cargo->getObjectID();
            static_cast<Carryall*>(aircraft)->giveCargo(cargo);
            require(static_cast<Carryall*>(aircraft)->hasCargo(),"Carryall did not take the cargo");
        }
        // Exercise a normal Ornithopter pass and a loaded Carryall on its slow
        // final delivery approach, giving gas a real opportunity to hit each.
        aircraft->doMove2Pos(kind==0?45:24,site.y+(kind==0?0:1),true);

        const auto aircraftID=aircraft->getObjectID(),deviatorID=deviator->getObjectID();
        const FixPoint startHealth=aircraft->getHealth();
        FixPoint minHealth=startHealth;
        bool converted=false,acquired=false,cargoKept=(kind==0),reverted=false;
        int ticks=0;
        for(;ticks<3000;++ticks) {
            game->updateGameState();
            aircraft=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(aircraftID));
            deviator=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(deviatorID));
            if(!aircraft || !deviator)break;
            // Keep the pair mutually observable; this fixture is about the weapon,
            // not about map exploration.
            aircraft->setVisible(VIS_ALL,true);deviator->setVisible(VIS_ALL,true);
            currentGameMap->viewMap(HOUSE_ATREIDES,site,12);
            minHealth=std::min(minHealth,aircraft->getHealth());
            if(deviator->hasATarget() && deviator->getTarget()==aircraft)acquired=true;
            if(aircraft->getOwner()==captor) {
                converted=true;
                if(kind==1)cargoKept=static_cast<Carryall*>(aircraft)->hasCargo();
                break;
            }
        }
        require(aircraft!=nullptr,"The hostile aircraft disappeared during the pass");
        require(deviator!=nullptr,"The Deviator disappeared during the pass");
        require(acquired,"Deviator target selection never accepted the hostile aircraft");
        require(minHealth==startHealth,"The gas pass removed hit points from the aircraft");

        if(converted) {
            ++conversions[kind];
            require(aircraft->deviationTimer>0 && aircraft->deviationTimer<=DEVIATIONTIME,"Capture set an invalid deviation timer");
            require(aircraft->getOriginalHouseID()==int(HOUSE_HARKONNEN),"Capture rewrote the original house");
            if(kind==1) {
                require(cargoKept,"Capture dropped the Carryall cargo");
                auto* cargo=game->getObjectManager().getObject(cargoID);
                require(cargo!=nullptr,"Capture destroyed the Carryall cargo");
                require(cargo->getOwner()==victimHouse,"Capture transferred the cargo's owner");
                const auto* held=static_cast<Carryall*>(aircraft);
                require(std::count(held->pickedUpUnitList.begin(),held->pickedUpUnitList.end(),cargoID)==1,
                        "Capture duplicated or lost the cargo manifest entry");
                // A captured transport must not insert its enemy passenger into
                // the captor's repair yard. It must unload safely beside it.
                auto* yard=captor->placeStructure(NONE_ID,Structure_RepairYard,40,20,true,true);
                require(yard!=nullptr,"Could not place the captured-cargo repair yard");
                aircraft->setLocation(Coord(40,20));
                auto* carrier=static_cast<Carryall*>(aircraft);
                carrier->deployUnit(cargoID);
                require(!carrier->hasCargo(),"Carryall retained an unloaded passenger");
                require(cargo->isActive(),"Hostile cargo was admitted into the captor's repair yard");
                require(cargo->getOwner()==victimHouse,"Unloading transferred the passenger's owner");
            }
            // The impact test checks the full duration; shorten it for bounded reversion.
            aircraft->deviationTimer=2;
            for(int t=0;t<20 && aircraft;++t) {
                game->updateGameState();
                aircraft=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(aircraftID));
                if(aircraft && !aircraft->wasDeviated()) {reverted=true;break;}
            }
            require(aircraft!=nullptr,"The aircraft was lost while the deviation expired");
            require(reverted,"The deviation never reverted to the original house");
            require(aircraft->getOwner()==victimHouse,"Reversion picked the wrong house");
            if(kind==1) {
                auto* cargo=game->getObjectManager().getObject(cargoID);
                require(cargo!=nullptr,"Reversion destroyed the Carryall cargo");
                require(cargo->getOwner()==victimHouse,"Reversion changed the cargo's owner");
            }
        }
        csv<<"flight,"<<(kind==0?Unit_Ornithopter:Unit_Carryall)<<','<<seed<<','<<converted
           <<",-1,"<<(minHealth!=startHealth)<<','<<cargoKept<<','<<reverted<<','<<ticks<<'\n';
        game.reset();currentGame=nullptr;
    }
    SDL_Log("GAS_FLIGHT ornithopter=%d/3 carryall=%d/3",conversions[0],conversions[1]);
    require(conversions[0]>0,"Deviators never converted an airborne Ornithopter");
    require(conversions[1]>0,"Deviators never converted an airborne Carryall");
    csv.close();
}

} // namespace

int runUnitSpeedProbe() {
    const std::string mod=std::getenv("UNIT_SPEED_PROBE_MOD");
    auto require=[](bool ok,const char* why) {if(!ok)throw std::runtime_error(why);};
    require(ModManager::instance().setActiveMod(mod),"Missing projectile combat mod");
    settings.general.playerName="Speed tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)),{});
    const std::string outDir=std::getenv("UNIT_SPEED_PROBE_OUT");
    std::ofstream csv(outDir+"/"+mod+".csv");
    csv << "shooter,seed,distance,heading,seconds,killed,damaged,shots,air_shots\n";
    int kills[2]={},damageCases[2]={},fired[2]={},airShots=0;
    for(int kind=0;kind<2;++kind) for(int seed=1;seed<=4;++seed)
    for(int distance : {4,8,12,18}) for(int heading : {LEFT,UP,DOWN,RIGHT}) {
        auto game=std::make_unique<Game>();currentGame=game.get();
        auto options=settings.gameOptions;options.rocketTurretsNeedPower=false;
        GameInitSettings init("projectiles.ini",mapData,"Projectile combat",true,options);init.randomSeed=seed;
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES,1);humans.addPlayerInfo({"Speed tester","HumanPlayer"});init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN,2);enemies.addPlayerInfo({"Enemy","HumanPlayer"});init.addHouseInfo(enemies);
        game->initGame(init);game->initializeGameLoop();
        // Diagnostic sensitivity experiment only; never changes shipped data.
        if(const char* reload = std::getenv("AA_RELOAD_CYCLES")) {
            const int cycles = std::atoi(reload);
            require(cycles > 0 && cycles <= 10000, "Invalid diagnostic rocket reload");
            for(int house = 0; house < NUM_HOUSES; ++house)
                game->objectData.data[Structure_RocketTurret][house].weaponreloadtime = cycles;
        }
        auto* owner=game->getHouse(HOUSE_ATREIDES);
        const Coord site(20,15);
        ObjectBase* shooter=nullptr;
        if(kind==0) {auto* u=owner->createUnit(Unit_Launcher,true);u->deploy(site);shooter=u;}
        else shooter=owner->placeStructure(NONE_ID,Structure_RocketTurret,site.x,site.y,true,true);
        require(shooter!=nullptr,"Could not deploy anti-air shooter");
        shooter->setVisible(VIS_ALL,true);
        auto* aircraft=static_cast<Ornithopter*>(game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true));
        aircraft->deploy(site+Coord(distance,0));aircraft->angle=heading;aircraft->drawnAngle=heading;aircraft->setVisible(VIS_ALL,true);
        aircraft->doAttackObject(shooter,true);
        const auto targetID=aircraft->getObjectID(),shooterID=shooter->getObjectID();
        const auto hp=aircraft->getHealth();
        if(kind==0)static_cast<Launcher*>(shooter)->doAttackObject(aircraft,true);
        bool killed=false,damaged=false;int ticks=0,shots=0,orniShots=0;
        Uint32 previousShot=0;
        for(;ticks<3750;++ticks) {
            game->updateGameState();
            aircraft=dynamic_cast<Ornithopter*>(game->getObjectManager().getObject(targetID));
            auto* firingObject=game->getObjectManager().getObject(shooterID);
            if(!aircraft || aircraft->getHealth()<=0) {killed=true;break;}
            damaged |= aircraft->getHealth()<hp;
            if(aircraft->timeLastShot && aircraft->timeLastShot!=previousShot) {++orniShots;previousShot=aircraft->timeLastShot;}
            if(!firingObject || firingObject->getHealth()<=0)break;
        }
        shots=kind==0?game->combatStats.launcherRocketsSpawned:game->combatStats.turretRocketsSpawned;
        kills[kind]+=killed;damageCases[kind]+=damaged||killed;fired[kind]+=shots;airShots+=orniShots;
        csv<<kind<<','<<seed<<','<<distance<<','<<heading<<','<<ticks*.016<<','<<killed<<','<<(damaged||killed)<<','<<shots<<','<<orniShots<<'\n';
        game.reset();currentGame=nullptr;
    }
    for(int kind=0;kind<2;++kind) {
        SDL_Log("AA_COMBAT kind=%d kills=%d/64 damage=%d shots=%d",kind,kills[kind],damageCases[kind],fired[kind]);
        require(fired[kind]>0,"Anti-air shooter never fired");
        require(kills[kind]>0,"Anti-air shooter could not kill any attacking ornithopter");
    }
    require(airShots>0,"Ornithopters never fired during combat");
    csv.close();
    runDeviatorGasProbe(mod,mapData,outDir);
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile combat",mod.c_str());return -1;
}
