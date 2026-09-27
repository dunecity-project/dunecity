// Controlled projectile audit using production Bullet::update and Map::damage.
// This measures mechanics; it is not a battle win-rate benchmark.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <units/InfantryBase.h>
#include <units/Harvester.h>
#include <units/Ornithopter.h>
#include <units/TankBase.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cmath>
#include <Bullet.h>
#include <algorithm>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <sstream>
#include <vector>
#include <DynastyProjectile.h>

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
    std::ofstream csv(std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/"+mod+".csv");
    csv << "type,stage,frames\n";

    auto trace=[]() {
        OMemoryStream out;out.open();out.writeUint32(bulletList.size());
        for(auto* b:bulletList)b->save(out);
        for(auto* u:unitList) {out.writeUint32(u->getObjectID());out.writeFixPoint(u->getHealth());}
        return std::string(out.getData(),out.getDataLength());
    };
    for(int type : {Bullet_Rocket,Bullet_DRocket,Bullet_TurretRocket,Bullet_SmallRocket,Bullet_LargeRocket,Bullet_Flame,Bullet_Heal})
    for(int stage : {1,7,19}) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);shooter->attackMode=STOP;
        auto* victim=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);victim->deploy(site+Coord(12,4));victim->attackMode=STOP;
        const auto shooterID=shooter->getObjectID(),victimID=victim->getObjectID();
        Coord start=shooter->getCenterPoint(),end=victim->getCenterPoint();
        auto* b=new Bullet(shooterID,&start,&end,type,8,true,victim);bulletList.push_back(b);
        for(int i=0;i<stage;++i)game->updateGameState();
        const std::string path=std::string(std::getenv("UNIT_SPEED_PROBE_OUT"))+"/projectiles-"+mod+".dls";
        require(game->saveGame(path),"Could not save in-flight projectiles");
        const auto observer=game->saveObserverRuntime();
        std::vector<std::string> expected;
        for(int i=0;i<160;++i) {game->updateGameState();expected.push_back(trace());}
        game.reset();currentGame=nullptr;game=std::make_unique<Game>();currentGame=game.get();
        game->initGame(GameInitSettings(path));game->initializeGameLoop();game->loadObserverRuntime(observer);
        owner=game->getHouse(HOUSE_ATREIDES);
        for(int i=0;i<160;++i) {game->updateGameState();require(trace()==expected[i],"In-flight save/observer continuation diverged");}
        csv<<type<<','<<stage<<",160\n";
        if(auto* u=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(shooterID)))u->cancelDeployment();
        if(auto* u=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(victimID)))u->cancelDeployment();
        for(auto* bullet:bulletList)delete bullet;bulletList.clear();
    }
    // Synthesize the previous byte layout: preserve prefix and its signed 8-bit cycle timer.
    auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
    Coord start=shooter->getCenterPoint(),end=start+Coord(12*TILESIZE,0);
    for(int type : {Bullet_Rocket,Bullet_TurretRocket,Bullet_Flame,Bullet_Heal}) {
        Bullet original(shooter->getObjectID(),&start,&end,type,8,false,nullptr);
        OMemoryStream out;out.open();original.save(out);
        std::string bytes(out.getData(),out.getDataLength()-19-24);bytes.push_back(type==Bullet_Heal?char(-1):char(120));
        game->loadedSavegameVersion=9844;
        IMemoryStream in(bytes.data(),bytes.size());Bullet restored(in);
        require(in.getRemainingLength()==0,"Old projectile layout read outside its record");
        require(restored.detonationTimer==(type==Bullet_Heal?-1:type==Bullet_Flame?120:39),"Old projectile timer migration incorrect");
        game->loadedSavegameVersion=SAVEGAMEVERSION;
    }
    shooter->cancelDeployment();csv.close();
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile continuation",mod.c_str());
    game.reset();currentGame=nullptr;return -1;
}
