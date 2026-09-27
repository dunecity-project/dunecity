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
#include <DynastyProjectile.h>
#include <structures/RocketTurret.h>
#include <structures/Palace.h>

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
    csv << "type,target,case,speed_tiles_s,initial_fuse,turn_deg_s,lifetime_s,target_damage,end_distance_tiles\n";
    for(int type : {Bullet_Rocket,Bullet_DRocket,Bullet_TurretRocket,Bullet_SmallRocket,Bullet_LargeRocket,Bullet_Flame})
    for(int targetType : {Unit_Tank,Unit_Ornithopter,Unit_Carryall})
    for(int scenario=0;scenario<3;++scenario) {
        auto* shooter=owner->createUnit(type==Bullet_Flame?Unit_Trooper:Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(targetType,true);target->deploy(site+Coord(8,0));
        target->setVisible(VIS_ALL,true);
        Coord start=shooter->getCenterPoint(),end=target->getCenterPoint();
        auto* b=new Bullet(shooter->getObjectID(),&start,&end,type,40,targetType!=Unit_Tank,target);
        bulletList.push_back(b);
        b->destination=end; // Isolate steering/fuses from random scatter.
        const auto hp=target->getHealth();
        const int fuse=b->detonationTimer;const double speed=b->speed.toDouble()/64/0.016;
        if(scenario==1) {b->angle+=128;if(b->angle>=256)b->angle-=256;b->projectileHeading=(b->projectileHeading+128)&255;b->projectileAim=b->projectileHeading;b->xSpeed= -b->xSpeed;b->ySpeed= -b->ySpeed;}
        if(auto p=DynastyProjectile::parameters(type);p.step) {
            require(std::abs(speed-p.step*20.0/256)<0.002,"Incorrect nominal missile speed");
            require(fuse==p.delay*(targetType==Unit_Tank?1:2),"Incorrect ground/air arming counter");
        }
        const auto firstAngle=b->angle;double turn=0,distance=8;
        int ticks=0;
        for(;ticks<1000;++ticks) {
            if(scenario==2 && targetType!=Unit_Tank) {
                // Controlled crossing target at the corrected ornithopter cruise speed.
                auto y=target->realY+11.52_fix;
                if(y>(site.y+6)*64) y=site.y*64;
                target->unassignFromMap(target->location);
                target->setLocation(site.x+8,(y/64).floor());
                target->realY=y;
            }
            distance=distanceFrom(b->realX,b->realY,target->getCenterPoint().x,target->getCenterPoint().y).toDouble()/64;
            b->update();
            const bool alive=std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end();
            if(!alive) {++ticks;break;}
            if(ticks==0) {
                double delta=std::abs((b->angle-firstAngle).toDouble());if(delta>128)delta=256-delta;
                turn=delta*360/256/0.016;
            }
        }
        if(type==Bullet_TurretRocket && scenario==1)
            require(target->getHealth()==hp,"Wrong-way turret rocket caused remote damage");
        // Gas may detonate on physical aircraft contact before its arming counter.
        const bool contactFuse = type==Bullet_TurretRocket || (type==Bullet_DRocket && targetType!=Unit_Tank);
        if(DynastyProjectile::parameters(type).step && !contactFuse)
            require(ticks*0.016 >= fuse*0.05-0.016,"Missile exploded before its arming counter");
        csv<<type<<','<<targetType<<','<<scenario<<','<<speed<<','<<fuse<<','<<turn<<','<<ticks*0.016<<','<<(hp-target->getHealth()).toDouble()<<','<<distance<<'\n';
        if(std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end()) {bulletList.remove(b);delete b;}
        target->cancelDeployment();shooter->cancelDeployment();
    }
    for(int offset : {0,24,40}) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);target->deploy(site+Coord(8,0));
        const auto hp=target->getHealth();const Coord point=target->getCenterPoint()+Coord(offset,0);
        currentGameMap->damage(shooter->getObjectID(),owner,point,Bullet_Rocket,8,TILESIZE,true);
        require(hp-target->getHealth()==(offset==0?8:offset==24?4:2),"Rocket splash differs from Dynasty falloff");
        csv<<"blast,35,"<<offset<<",0,0,0,0,"<<(hp-target->getHealth()).toDouble()<<','<<offset/64.0<<'\n';
        target->cancelDeployment();shooter->cancelDeployment();
    }
    // Turret swept collision: physical contact vs paths crossing at different times.
    for(int scenario=0;scenario<3;++scenario) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
        auto* target=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);target->deploy(site);
        Coord start=shooter->getCenterPoint(),end=start+Coord(8*TILESIZE,0);
        auto* b=new Bullet(shooter->getObjectID(),&start,&end,Bullet_TurretRocket,30,true,target);
        bulletList.push_back(b);b->projectileClock=0;b->projectileHeading=64;b->projectileAim=64;b->destination=end;
        if(scenario==0) { // Aircraft crosses north to south while missile flies east.
            b->projectileTargetPosition=(start+Coord(18,-18))*4;
            target->realX=start.x+18;target->realY=start.y+18;
        } else { // Parallel motion, at different times, or target disappears.
            b->projectileTargetPosition=(start+Coord(16,12))*4;
            target->realX=start.x+52;target->realY=start.y+12;
        }
        const auto hp=target->getHealth();
        if(scenario==2) {target->cancelDeployment();target=nullptr;}
        b->update();
        const bool alive=std::find(bulletList.begin(),bulletList.end(),b)!=bulletList.end();
        require(scenario==0?!alive:alive,"Swept collision misclassified an aircraft crossing");
        if(target) require(scenario==0?target->getHealth()==0:target->getHealth()==hp,"Incorrect physical-intercept damage");
        if(alive) {bulletList.remove(b);delete b;}
        if(target)target->cancelDeployment();shooter->cancelDeployment();
        csv<<"intercept,"<<scenario<<",pass\n";
    }
    // Nearby air and ground units receive the same falloff regardless of aimed layer.
    for(bool aimedAir : {false,true}) {
        auto* shooter=owner->createUnit(Unit_Launcher,true);shooter->deploy(site);
        auto* tank=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Tank,true);tank->deploy(site+Coord(8,0));
        auto* air=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);air->deploy(site+Coord(8,0));
        const auto hp=tank->getHealth();
        currentGameMap->damage(shooter->getObjectID(),owner,tank->getCenterPoint()+Coord(24,0),Bullet_Rocket,8,TILESIZE,aimedAir);
        require(tank->getHealth()==hp-4 && air->getHealth()==air->getMaxHealth()-4,"Rocket blast incorrectly filtered an air/ground layer");
        air->cancelDeployment();tank->cancelDeployment();shooter->cancelDeployment();
        csv<<"layers,"<<aimedAir<<",pass\n";
    }
    // Exercise the close-range weapon selector, not just constructed missiles.
    {
        auto* turret=static_cast<RocketTurret*>(owner->placeStructure(NONE_ID,Structure_RocketTurret,site.x,site.y,true,true));
        require(turret!=nullptr,"Could not place close-AA turret");
        auto* air=game->getHouse(HOUSE_HARKONNEN)->createUnit(Unit_Ornithopter,true);air->deploy(site+Coord(1,0));
        turret->setTarget(air);turret->weaponTimer=0;turret->attack();
        require(!bulletList.empty(),"Close turret did not fire at aircraft");
        auto* shell=bulletList.back();require(shell->getBulletID()==Bullet_ShellTurret,"Close turret used the wrong weapon");
        for(int i=0;i<20 && std::find(bulletList.begin(),bulletList.end(),shell)!=bulletList.end();++i)shell->update();
        require(air->getHealth()<air->getMaxHealth(),"Close turret shell could not damage aircraft");
        air->cancelDeployment();turret->destroy();
        csv<<"close_turret,pass\n";
    }
    // A real Palace launch uses the corrected scatter conversion and damage.
    {
        auto* palace=static_cast<Palace*>(game->getHouse(HOUSE_HARKONNEN)->placeStructure(NONE_ID,Structure_Palace,8,8,true,true));
        require(palace!=nullptr,"Could not place Palace");palace->specialWeaponTimer=0;
        const auto count=bulletList.size();palace->doLaunchDeathhand(40,15);
        require(bulletList.size()==count+1,"Palace failed to launch Death Hand");
        auto* missile=bulletList.back();
        require(missile->damage==200 && missile->detonationTimer==15,"Palace missile parameters differ from Dynasty");
        require(missile->destination.x%TILESIZE==TILESIZE/2 && missile->destination.y%TILESIZE==TILESIZE/2,"Death Hand scatter did not resolve to a tile centre");
        missile->realX=25*TILESIZE;missile->realY=15*TILESIZE;
        const auto explosions=game->getExplosionList().size();missile->destroy();
        require(game->getExplosionList().size()==explosions+17,"Death Hand blast centre count differs from Dynasty");
        palace->destroy();csv<<"palace,pass\n";
    }
    csv.close();SDL_Log("UNIT_SPEED_PROBE_PASS: %s projectile audit",mod.c_str());
    game.reset();currentGame=nullptr;return -1;
}
