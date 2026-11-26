// Real-engine QuantBot Wind Trap retirement (NuclearConsolidation, docs/formations.md "Economy: nuclear
// consolidation and palace districts"). Included only by run-campaign-balance.py --wind-trap-probe;
// never linked into the game.
//
// Stefan, 2026-11-27: "windtraps being destroyed should only be when the ai is low on free building
// space. if it has space it should keep the windtraps, not blow them up".
//
// Drives the production QuantBot::planCityEconomyProjects() on a Custom Brutal city house that owns a
// Nuclear Plant and Wind Traps with power to spare, so that every other retirement condition holds:
//   1. Room to build (the rock survey says footprints are left, a production building fits and free
//      rock is above ForceConcentrationPolicy::kWindTrapRetireFreeRock): no Wind Trap is demolished,
//      over repeated passes. Before protocol 141 one went every pass.
//   2. Each way of being low on room on its own (fewer than kCrampedFootprints footprints, no site for
//      a production building, free rock below the line): exactly one Wind Trap goes per pass, and it is
//      the one with the most own lots within 6 tiles (the choice before 141).
//   3. Low on room, pass after pass: never below kKeepWindTraps.
//   4. Room again after some went: the rest stay.
#include <players/QuantBotBuildPass.h>
#include <players/QuantBotColonisationPolicy.h>
#include <players/ForceConcentrationPolicy.h>

void runWindTrapRetirementProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("WIND_TRAP_PROBE_FAIL: %s",why);throw std::runtime_error(why);}
    };
    namespace Force=ForceConcentrationPolicy;
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    require(house,"Wind trap fixture missing its house");
    require(game.isCitySimEnabled(),"Wind trap fixture needs city mode");
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Wind trap fixture missing its bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->role=AIRole::Both;
    bot->difficulty=QuantBot::Difficulty::Brutal;
    require(bot->botConfig().force.nuclearConsolidation,"NuclearConsolidation is not the default");

    // A free rock block clear of every building, for a reactor, four Wind Traps and three lots (no
    // house owns a lot yet, so only these lots decide the choice).
    // Layout (x, y from the block's corner): reactors 0,0 0,5 4,5; Wind Traps 4,0 7,0 10,0 13,0;
    // residential lots 16,0 18,0 16,2. The lots stand within 6 tiles of the Wind Trap at 13,0 (3)
    // and of the one at 10,0 (2), so the existing choice takes 13,0 first.
    const int blockW=20,blockH=8,clearance=2;
    auto& map=*currentGameMap;
    auto clear=[&](int x0,int y0) {
        for(int y=y0;y<y0+blockH;++y) for(int x=x0;x<x0+blockW;++x) {
            if(!map.tileExists(x,y)) return false;
            const auto* tile=map.getTile(x,y);
            if(!tile->isRock()||tile->isMountain()||tile->hasAnObject()||tile->hasCityZone()) return false;
        }
        for(const auto* structure:structureList) {
            const Coord at=structure->getLocation(),size=structure->getStructureSize();
            if(at.x+size.x+clearance>x0 && at.x<x0+blockW+clearance
                && at.y+size.y+clearance>y0 && at.y<y0+blockH+clearance) return false;
        }
        return true;
    };
    Coord origin=Coord::Invalid();
    for(int y=0;y<map.getSizeY() && !origin.isValid();++y)
        for(int x=0;x<map.getSizeX() && !origin.isValid();++x)
            if(clear(x,y)) origin=Coord(x,y);
    require(origin.isValid(),"No free rock block for the wind trap fixture");
    auto place=[&](Uint32 item,int dx,int dy) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+dx,origin.y+dy,true,true);
        require(structure && structure->getItemID()==Uint32(item),"Fixture placement failed");
        return structure->getObjectID();
    };
    // Three reactors to start with; phase 3 adds more if it needs them.
    for(const Coord at:{Coord(0,0),Coord(0,5),Coord(4,5)}) place(Structure_NuclearPlant,at.x,at.y);
    std::vector<Uint32> traps;
    for(const int dx:{4,7,10,13}) traps.push_back(place(Structure_WindTrap,dx,0));
    place(Structure_ZoneResidential,16,0);
    place(Structure_ZoneResidential,18,0);
    place(Structure_ZoneResidential,16,2);
    SDL_Log("WIND_TRAP_PROBE: fixture at %d,%d wind_traps=%d nuclear=%d produced=%lld required=%lld",
        origin.x,origin.y,house->getNumItems(Structure_WindTrap),house->getNumItems(Structure_NuclearPlant),
        (long long)house->getProducedPower(),(long long)house->getPowerRequirement());

    auto alive=[&](Uint32 id) {
        const auto* object=game.getObjectManager().getObject(id);
        return object && object->getHealth()>0;
    };
    // Every other retirement condition: power to spare after one more goes, with no reserve held.
    auto spare=[&] {
        const int windOutput=std::max(1,-int(game.objectData.data[Structure_WindTrap][HOUSE_HARKONNEN].power));
        return house->getProducedPower()-windOutput-house->getPowerRequirement()>=windOutput;
    };
    struct Room { const char* name; int freeRock; int footprints; bool blocked; };
    const Room roomy{"roomy",Force::kWindTrapRetireFreeRock+200,QuantBotColonisationPolicy::kCrampedFootprints,false};
    const Room lowFootprints{"few_footprints",Force::kWindTrapRetireFreeRock+200,
        QuantBotColonisationPolicy::kCrampedFootprints-1,false};
    const Room blocked{"production_blocked",Force::kWindTrapRetireFreeRock+200,QuantBotColonisationPolicy::kCrampedFootprints,true};
    const Room lowRock{"little_rock",Force::kWindTrapRetireFreeRock-1,QuantBotColonisationPolicy::kCrampedFootprints,false};
    auto pass=[&](const Room& room) {
        bot->availableBaseRock=room.freeRock;
        bot->availableBaseFootprints=room.footprints;
        bot->baseProductionRoomBlocked=room.blocked;
        QuantBot::BuildPass buildPass(bot->militaryUnitValue());
        buildPass.citySimEnabled=true;
        buildPass.cityPowerReserve=0;
        buildPass.rocketPowerBuffer=0;
        buildPass.committedPowerDemand=0;
        buildPass.stablePower=false;
        for(Uint32 item=0;item<Num_ItemID;++item) buildPass.itemCount[item]=house->getNumItems(item);
        const int before=house->getNumItems(Structure_WindTrap);
        bot->planCityEconomyProjects(buildPass);
        const int after=house->getNumItems(Structure_WindTrap);
        SDL_Log("WIND_TRAP_PROBE: room=%s free_rock=%d footprints=%d blocked=%d wind_traps %d -> %d",
            room.name,room.freeRock,room.footprints,int(room.blocked),before,after);
        return before-after;
    };
    require(house->getNumItems(Structure_WindTrap)>Force::kKeepWindTraps+4,
        "The fixture house needs more Wind Traps than the kept ones plus the low-room cases");
    require(spare(),"The fixture house has no power to spare");

    // 1. Room to build: every Wind Trap stays.
    for(int i=0;i<5;++i) require(pass(roomy)==0,"A Wind Trap was demolished although the base has room to build");
    for(const Uint32 id:traps) require(alive(id),"A fixture Wind Trap went while the base had room");

    // 2. Each way of being low on room: one goes, the one with most lots around it first.
    require(pass(lowFootprints)==1,"Few free footprints did not retire a Wind Trap");
    require(!alive(traps[3]) && alive(traps[2]) && alive(traps[1]) && alive(traps[0]),
        "The retired Wind Trap is not the one with most own lots within 6 tiles");
    require(pass(blocked)==1,"No site for a production building did not retire a Wind Trap");
    require(!alive(traps[2]),"The second Wind Trap retired is not the one next nearest the lots");
    require(pass(lowRock)==1,"Free rock below the line did not retire a Wind Trap");

    // 4. Room again: the rest stay.
    for(int i=0;i<3;++i) require(pass(roomy)==0,"A Wind Trap was demolished after the base had room again");

    // 3. Low on room pass after pass: down to kKeepWindTraps, never below. The map gives this house
    //    dozens of Wind Traps; all but two of them go first, so power stays spare all the way down.
    {
        std::vector<Uint32> others;
        for(const auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_WindTrap
                && std::find(traps.begin(),traps.end(),structure->getObjectID())==traps.end())
                others.push_back(structure->getObjectID());
        for(size_t i=2;i<others.size();++i)
            if(auto* structure=dynamic_cast<StructureBase*>(game.getObjectManager().getObject(others[i])))
                structure->destroy();
        // A reactor's output here is not its nominal 2,000 (10,133 with three and the map's 70 Wind
        // Traps), so more go on free rock until the house has power to spare for this phase.
        for(int added=0;added<12 && !spare();++added) {
            Coord site=Coord::Invalid();
            for(int y=0;y+3<=map.getSizeY() && !site.isValid();++y)
                for(int x=0;x+3<=map.getSizeX() && !site.isValid();++x) {
                    bool free=true;
                    for(int dy=0;dy<3 && free;++dy) for(int dx=0;dx<3 && free;++dx) {
                        const auto* tile=map.getTile(x+dx,y+dy);
                        free=tile->isRock() && !tile->isMountain() && !tile->hasAnObject() && !tile->hasCityZone();
                    }
                    if(free) site=Coord(x,y);
                }
            require(site.isValid(),"No free rock for another fixture reactor");
            auto* reactor=house->placeStructure(NONE_ID,Structure_NuclearPlant,site.x,site.y,true,true);
            require(reactor && reactor->getItemID()==Structure_NuclearPlant,"Fixture reactor placement failed");
        }
        SDL_Log("WIND_TRAP_PROBE: cleared to wind_traps=%d produced=%lld required=%lld",
            house->getNumItems(Structure_WindTrap),(long long)house->getProducedPower(),
            (long long)house->getPowerRequirement());
        require(house->getNumItems(Structure_WindTrap)>Force::kKeepWindTraps,"Phase 3 needs more than the kept Wind Traps");
        require(spare(),"Phase 3 has no power to spare");
    }
    for(int i=0;i<200 && spare() && house->getNumItems(Structure_WindTrap)>Force::kKeepWindTraps;++i)
        require(pass(lowFootprints)==1,"A low-room pass with power to spare retired nothing");
    require(house->getNumItems(Structure_WindTrap)==Force::kKeepWindTraps,
        "Low-room retirement stopped short of the kept Wind Traps");
    for(int i=0;i<3;++i) require(pass(lowFootprints)==0,"A Wind Trap below the kept ones was retired");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=wind_trap_retirement outcome=passed");
}
