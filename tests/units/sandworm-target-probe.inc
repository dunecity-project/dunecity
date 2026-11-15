// A real sleeping sandworm remains in the object manager and can still be an
// AI unit's target. Threat evaluation must not read its off-map location.
#include <units/SandWorm.h>
void runSandwormTargetProbe(House& owner) {
    auto require=[](bool value,const char* why) { if(!value) throw std::runtime_error(why); };
    Coord sand=Coord::Invalid();
    for(int y=0;y<currentGameMap->getSizeY() && sand.isInvalid();++y)
        for(int x=0;x+1<currentGameMap->getSizeX() && sand.isInvalid();++x) {
            const auto* a=currentGameMap->getTile(x,y);
            const auto* b=currentGameMap->getTile(x+1,y);
            if(!a->hasAGroundObject() && !b->hasAGroundObject()
               && !a->hasAnUndergroundUnit() && !b->hasAnUndergroundUnit()) sand=Coord(x,y);
        }
    require(sand.isValid(),"No free sand for sleeping-worm regression");
    auto* a=currentGameMap->getTile(sand);
    auto* b=currentGameMap->getTile(sand+Coord(1,0));
    const int typeA=a->getType(), typeB=b->getType();
    const auto regionA=a->getSandRegion(), regionB=b->getSandRegion();
    a->setType(Terrain_Sand); b->setType(Terrain_Sand);
    a->setSandRegion(1); b->setSandRegion(1);
    auto* worm=static_cast<Sandworm*>(owner.createUnit(Unit_Sandworm));
    auto* target=owner.createUnit(Unit_Launcher);
    require(worm && target,"Cannot create sleeping-worm regression units");
    worm->deploy(sand); target->deploy(sand+Coord(1,0));
    require(worm->canAttack(target),"Deployed worm cannot attack valid target on connected sand");
    worm->sleep();
    require(worm->getLocation().isInvalid() && !worm->isActive(),"Worm did not enter its real sleeping state");
    require(!worm->canAttack(target),"Sleeping worm was treated as an active attacker");
    require(!worm->canAttack(nullptr),"Sleeping worm accepted null target");
    worm->deploy(sand);
    require(worm->canAttack(target),"Re-deployed worm permanently lost attack capability");
    currentGameMap->removeObjectFromMap(target->getObjectID());
    target->setLocation(Coord::Invalid());
    require(!worm->canAttack(target),"Worm accepted an off-map target");
    target->cancelDeployment(); worm->cancelDeployment();
    a->setType(typeA); b->setType(typeB);
    a->setSandRegion(regionA); b->setSandRegion(regionB);
    SDL_Log("SANDWORM_TARGET_PROBE_PASS: deployed, sleeping, re-deployed and off-map targets");
}
