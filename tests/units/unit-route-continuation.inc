// Headless integration fixture; linked with the production game objects.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <units/InfantryBase.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/Ornithopter.h>
#include <units/TankBase.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <sstream>
#include <vector>
#include "hunt-reachability.inc"

int runUnitSpeedProbe() {
    const std::string mod = std::getenv("UNIT_SPEED_PROBE_MOD");
    const bool baseline = std::string(std::getenv("UNIT_SPEED_PROBE_BASELINE")) == "1";
    auto require = [](bool ok, const char* why) {
        if(!ok) throw std::runtime_error(why);
    };
    require(ModManager::instance().setActiveMod(mod), "Missing speed fixture mod");
    settings.general.playerName = "Speed tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("speed.ini", mapData, "Unit speeds", true, settings.gameOptions);
    init.randomSeed = 67;
    require(init.getModName() == mod, "Speed fixture selected the wrong mod");
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Speed tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && owner->isAlive(), "Speed fixture house missing");
    game->initializeGameLoop();
    const Coord site(25,15);
    const std::string out=std::getenv("UNIT_SPEED_PROBE_OUT");
    std::ofstream csv(out+"/"+mod+".csv");
    auto trace=[](UnitBase* u) {
        std::ostringstream s;
        s << u->realX.getRawValue() << ',' << u->realY.getRawValue() << ','
          << u->angle.getRawValue() << ',' << u->location.x << ',' << u->location.y << ','
          << u->moving << ',' << u->dynastyRouteTick << ',' << u->dynastyStepStart << ',' << u->dynastyStepEnd;
        return s.str();
    };
    for(int id : {Unit_Tank,Unit_Soldier,Unit_Harvester}) for(int stage=0;stage<3;++stage) {
        for(int y=site.y-2;y<=site.y+3;++y) for(int x=site.x-2;x<=site.x+9;++x) {
            auto* tile=currentGameMap->getTile(x,y); tile->setType(Terrain_Sand); tile->setRoad(false);
        }
        auto* unit=owner->createUnit(id,true); unit->deploy(site);
        unit->angle=UP; unit->drawnAngle=UP;
        unit->doMove2Pos(site.x+8,site.y,true);
        int ticks=0;
        do {game->updateGameState(); ++ticks;}
        while(ticks<1000 && !(stage==0 ? unit->dynastyRouteTick && !unit->moving && unit->angle!=UP
                    : stage==1 ? unit->moving && unit->realX>site.x*TILESIZE+TILESIZE/2+8
                    : unit->justStoppedMoving));
        require(ticks<1000,"Could not reach save continuation stage");
        const auto objectID=unit->getObjectID();
        const std::string path=out+"/continuation-"+mod+".dls";
        require(game->saveGame(path),"Could not write continuation save");
        const auto runtime=game->saveObserverRuntime();
        std::vector<std::string> expected;
        for(int i=0;i<160;++i) {game->updateGameState();expected.push_back(trace(unit));}
        game.reset();currentGame=nullptr;
        game=std::make_unique<Game>();currentGame=game.get();
        game->initGame(GameInitSettings(path));
        game->initializeGameLoop();
        game->loadObserverRuntime(runtime);
        owner=game->getHouse(HOUSE_ATREIDES);
        unit=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(objectID));
        require(unit!=nullptr,"Saved route unit is missing");
        for(int i=0;i<160;++i) {
            game->updateGameState();
            if(trace(unit)!=expected[i]) throw std::runtime_error("Route save differs: "+std::to_string(id)+" stage "+std::to_string(stage)+" frame "+std::to_string(i)+" got "+trace(unit)+" expected "+expected[i]);
        }
        csv << id << ',' << stage << ",160 exact continuation frames\n";
        unit->cancelDeployment();
    }
    // Custom INI caps and city roads scale the moving portion, keeping the
    // script pause independent of graphics and configuration multipliers.
    Uint64 baseDuration=0;
    int baseTicks=0;
    for(int variant=0;variant<3;++variant) {
        for(int x=site.x;x<=site.x+8;++x) {
            auto* tile=currentGameMap->getTile(x,site.y);
            tile->setType(Terrain_Sand);tile->setRoad(variant==2);
        }
        while(game->getGameCycleCount()%125) game->updateGameState();
        auto& cap=game->objectData.data[Unit_Tank][HOUSE_ATREIDES].maxspeed;
        const auto oldCap=cap;if(variant==1)cap*=2;
        auto* u=owner->createUnit(Unit_Tank,true);u->deploy(site);u->angle=RIGHT;u->drawnAngle=RIGHT;
        const Coord end=site+Coord(8,0);u->doMove2Pos(end.x,end.y,true);
        int ticks=0;Uint64 duration=0;
        do {
            game->updateGameState();++ticks;
            if(duration==0 && u->moving)duration=u->dynastyStepEnd-u->dynastyStepStart;
        } while((u->location!=end || u->moving) && ticks<3000);
        require(ticks<3000,"Custom speed/road route failed");
        if(variant==0) {baseDuration=duration;baseTicks=ticks;}
        else {
            const int scale=variant==1?2:4;
            require(std::abs(static_cast<long long>(duration*scale)-static_cast<long long>(baseDuration))<=scale,
                "Custom cap or road no longer scales movement duration");
            require(ticks<baseTicks,"Speed extension did not shorten the route");
        }
        cap=oldCap;u->cancelDeployment();
    }
    for(int x=site.x;x<=site.x+8;++x)currentGameMap->getTile(x,site.y)->setRoad(false);
    // A replacement order during the first half of a tile must complete the
    // reserved step, then follow the new route without losing map occupancy.
    for(int id : {Unit_Tank,Unit_Soldier}) {
        auto* u=owner->createUnit(id,true);u->deploy(site);u->angle=RIGHT;u->drawnAngle=RIGHT;
        u->doMove2Pos(site.x+8,site.y,true);
        int ticks=0;while(!u->moving && ticks++<200)game->updateGameState();
        require(u->moving,"Replacement-order fixture did not start");
        for(int i=0;i<5;++i)game->updateGameState();
        const Coord end=site+Coord(2,2);u->doMove2Pos(end.x,end.y,true);
        ticks=0;while((u->location!=end || u->moving) && ticks++<3000)game->updateGameState();
        require(ticks<3000,"Replacement route did not finish");
        u->cancelDeployment();
        for(int y=site.y;y<=site.y+2;++y)for(int x=site.x;x<=site.x+8;++x)
            require(!currentGameMap->getTile(x,y)->hasAGroundObject(),"Replacement route left a ghost occupant");
    }
    // Zero INI speed must never reserve the destination tile.
    auto* unit=owner->createUnit(Unit_Tank,true);unit->deploy(site);
    auto& cap=game->objectData.data[Unit_Tank][HOUSE_ATREIDES].maxspeed;
    const auto oldCap=cap;cap=0;
    unit->angle=RIGHT;unit->drawnAngle=RIGHT;unit->doMove2Pos(site.x+3,site.y,true);
    for(int i=0;i<80;++i)game->updateGameState();
    require(unit->location==site && !unit->moving,"Zero-speed unit moved");
    require(!currentGameMap->getTile(site+Coord(1,0))->hasAGroundObject(),"Zero-speed unit reserved a tile");
    cap=oldCap;unit->cancelDeployment();
    csv.close();
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s saved routes",mod.c_str());
    game.reset();currentGame=nullptr;
    runHuntReachabilityProbe(mod);
    runTargetSearchWorkloadProbe(mod);
    return -1;
}
