// Exercise the real placement execution path, including sites invalidated
// after planning. No production objects are replaced by test doubles.
#include <structures/ConstructionYard.h>
#include <Command.h>
#include <Tile.h>

void runCityPlacementProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) { SDL_Log("CITY_PLACEMENT_FAIL: %s",why);throw std::runtime_error(why); }
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    Coord site=Coord::Invalid();
    for(int y=2;y+7<currentGameMap->getSizeY() && site.isInvalid();++y)
        for(int x=2;x+7<currentGameMap->getSizeX() && site.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<7;++dy) for(int dx=0;dx<7;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->hasCityZone())clear=false;
            }
            if(clear)site=Coord(x,y);
        }
    require(site.isValid(),"Missing clear placement fixture");
    auto reset=[&]() {
        for(int dy=0;dy<7;++dy) for(int dx=0;dx<7;++dx) {
            auto* tile=currentGameMap->getTile(site.x+dx,site.y+dy);
            tile->setType(Terrain_Rock);tile->setRoad(false);tile->setOwner(house->getHouseID());
        }
    };
    const int terrains[]={Terrain_Spice,Terrain_ThickSpice,Terrain_GreenSpice,
        Terrain_ThickGreenSpice,Terrain_RedSpice,Terrain_ThickRedSpice,
        Terrain_SpiceBloom,Terrain_GreenSpiceBloom,Terrain_RedSpiceBloom,Terrain_SpecialBloom};
    const int items[]={Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial,
        Structure_NuclearPlant,Structure_PoliceStation,Structure_Stadium,Structure_Airport,
        Structure_Road,Structure_PowerLine};
    int rejected=0;
    for(int item:items) {
        const Coord size=getStructureSize(item);
        reset();
        require(currentGameMap->okayToPlaceStructure(site.x,site.y,size.x,size.y,false,house,false,item),
            "Clear rock should be a valid planned site");
        for(int terrain:terrains) for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx) {
            auto* tile=currentGameMap->getTile(site.x+dx,site.y+dy);
            tile->setType(terrain);
            const auto spice=tile->getSpice();
            const auto structures=house->getNumStructures();
            require(!currentGameMap->okayToPlaceStructure(site.x,site.y,size.x,size.y,false,house,false,item),
                "Resource tile accepted during planning");
            require(!house->placeStructure(NONE_ID,item,site.x,site.y,false,false),
                "Resource tile accepted during placement execution");
            require(!house->placeStructure(NONE_ID,item,site.x,site.y,false,true),
                "Forced gameplay placement bypassed resource protection");
            require(tile->getType()==terrain && tile->getSpice()==spice
                && !tile->hasAGroundObject() && !tile->hasCityZone() && !tile->isRoadConnection()
                && house->getNumStructures()==structures,"Rejected placement changed resources or objects");
            ++rejected;tile->setType(Terrain_Rock);
        }
    }
    for(int item:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
        reset();
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            currentGameMap->getTile(site.x+dx,site.y+dy)->setType(dx==0 && dy==0 ? Terrain_Rock : Terrain_Sand);
        require(currentGameMap->okayToPlaceStructure(site.x,site.y,2,2,false,house,false,item),
            "Ordinary sand beside rock became invalid");
        auto* zone=house->placeStructure(NONE_ID,item,site.x,site.y,false,false);
        require(zone,"Valid anchored sand zone failed execution");
        zone->destroy();
    }
    reset();
    auto* north = house->placeStructure(NONE_ID,Structure_ZoneResidential,site.x+2,site.y,false,false);
    require(north,"Could not create the one-sided residential neighbour");
    // The 864 desync invented a second occupied side on this otherwise empty lot.
    // Read every surrounding vacancy while another live zone can resolve by id.
    for(int dy=0;dy<7;++dy) for(int dx=0;dx<7;++dx) {
        const bool occupied=dx>=2 && dx<4 && dy<2;
        auto* tile=currentGameMap->getTile(site.x+dx,site.y+dy);
        require(tile->hasANonInfantryGroundObject()==occupied,"Unexpected fixture occupancy");
        require(tile->getNonInfantryGroundObject()==(occupied ? north : nullptr),
            "Vacant tile resolved to a phantom residential neighbour");
    }
    north->destroy();
    require(currentGameMap->getTile(site.x+2,site.y)->getNonInfantryGroundObject()==nullptr,
        "A cleared tile retained its former ground object");
    reset();
    ConstructionYard* yard=nullptr;
    for(auto* structure:structureList) if(structure->getOwner()==house)
        if(auto* candidate=dynamic_cast<ConstructionYard*>(structure)) {yard=candidate;break;}
    require(yard,"Missing construction yard");
    while(yard->getProductionQueueSize()>0)yard->doCancelItem(yard->getCurrentProducedItem());
    yard->doProduceItem(Structure_ZoneResidential);
    require(yard->getCurrentProducedItem()==Structure_ZoneResidential,"Could not queue test zone");
    yard->productionProgress=yard->getBuildItem(Structure_ZoneResidential)->price;
    yard->setWaitingToPlace();
    auto* resource=currentGameMap->getTile(site.x+1,site.y+1);
    resource->setType(Terrain_SpiceBloom);
    const auto queue=yard->getProductionQueueSize();
    const auto progress=yard->getProductionProgress();
    const auto credits=house->getCredits();
    for(const auto& player:house->getPlayerList()) {
        Command(player->getPlayerID(),CMD_PLACE_STRUCTURE,yard->getObjectID(),site.x,site.y).executeCommand();
        require(yard->isWaitingToPlace() && yard->getProductionQueueSize()==queue
            && yard->getProductionProgress()==progress && house->getCredits()==credits
            && resource->isSpiceBloom() && !resource->hasAGroundObject(),
            "Rejected player/AI placement consumed the ready building or bloom");
    }
    resource->setType(Terrain_Sand);
    require(yard->doPlaceStructure(site.x,site.y) && yard->getProductionQueueSize()==queue-1,
        "Ready building could not be placed after resource cleared");
    SDL_Log("CITY_PLACEMENT_PASS: %d resource-footprint rejections; plain sand zones preserved",rejected);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_placement outcome=passed");
}
