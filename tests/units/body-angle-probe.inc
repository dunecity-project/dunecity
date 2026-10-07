// Exercise a real unit switching from movement rotation to combat rotation.
#include <Game.h>
#include <House.h>
#include <mod/ModManager.h>
#include <units/UnitBase.h>
#include <fstream>
#include <memory>
int runBodyAngleProbe() {
    const std::string mod=std::getenv("BODY_ANGLE_PROBE_MOD");
    auto require=[](bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); };
    require(ModManager::instance().setActiveMod(mod),"Missing rotation fixture mod");
    settings.general.playerName="Rotation tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)),{});
    auto game=std::make_unique<Game>(); currentGame=game.get();
    GameInitSettings init("rotation.ini",mapData,"Rotation regression",true,settings.gameOptions);
    init.randomSeed=67;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES,1);
    humans.addPlayerInfo({"Rotation tester","HumanPlayer"}); init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN,2);
    enemies.addPlayerInfo({"Enemy","HumanPlayer"}); init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner=game->getHouse(HOUSE_ATREIDES);
    for(int id : {Unit_Deviator,Unit_Launcher,Unit_SonicTank,Unit_Tank,Unit_Harvester}) {
        auto* unit=owner->createUnit(id,true);
        require(unit!=nullptr,"Cannot create rotation subject");
        unit->angle=7.75_fix; unit->drawnAngle=0;
        unit->moving=false; unit->justStoppedMoving=false;
        unit->dynastyRouteTick=5; game->gameCycleCount=13;
        require(unit->turnDynastyBody(RIGHT),"Movement rotation did not run");
        require(unit->angle>=7.5_fix && unit->angle<8,"Did not reach the movement/combat boundary");
        // navigate() clears the route clock when the movement destination is reached.
        unit->dynastyRouteTick=0; unit->nextSpotAngle=RIGHTDOWN; unit->targetAngle=RIGHTDOWN;
        for(int step=0; step<128; ++step) {
            unit->turn();
            if(step==0 || unit->getAngle()<0 || unit->getAngle()>=NUM_ANGLES) SDL_Log("BODY_ANGLE_STATE: mod=%s item=%d drawn=%d raw=%lld",mod.c_str(),id,unit->getAngle(),(long long)unit->angle.getRawValue());
            require(unit->getAngle()>=0 && unit->getAngle()<NUM_ANGLES,"Combat rotation produced a direction outside 0..7");
        }
        require(unit->getAngle()==RIGHTDOWN,"Combat rotation did not reach its target");
        unit->cancelDeployment();
    }
    game.reset(); currentGame=nullptr;
    SDL_Log("BODY_ANGLE_PROBE_PASS: %s",mod.c_str());
    return -1;
}
