// Diagnostic-only real-engine gameplay and save compatibility checks.
// Fixtures place bases, enemies and spice; manufacture, upgrades, harvesting,
// capture, weapons, research, sales and continuation use production commands/updates.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <Command.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <Network/GameStateDigest.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <structures/Refinery.h>
#include <structures/WindTrap.h>
#include <units/Harvester.h>
#include <units/InfantryBase.h>
#include <units/UnitBase.h>
#include <misc/OMemoryStream.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef SAVEGAMEVERSION_TORNIE546
#include <structures/Worfinery.h>
#include <mod/ChaosMode.h>
#include <mod/RadarResearch.h>
#endif

namespace TornieStateProbe {
int checks = 0;
void require(bool ok, const std::string& why) {
    if(!ok) { SDL_Log("TORNIE_GAMEPLAY_FAIL: %s", why.c_str()); throw std::runtime_error(why); }
    ++checks;
}
std::string out(const std::string& name) { return std::string(std::getenv("TORNIE_STATE_OUT")) + "/" + name; }
void clear(std::unique_ptr<Game>& game) {
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr; pLocalHouse = nullptr;
}
void advance(Game& game, int cycles) { for(int i = 0; i < cycles; ++i) game.updateGameState(); }
template<class Pred> void until(Game& game, int limit, Pred pred, const std::string& why) {
    for(int i = 0; i < limit && !pred(); ++i) game.updateGameState();
    if(!pred()) game.saveGame(out("failed-state.dls"));
    require(pred(), why);
}
Uint8 player(const House* house) {
    require(house && !house->getPlayerList().empty(), "Fixture controller missing");
    return house->getPlayerList().front()->getPlayerID();
}
void command(Game& game, const ObjectBase* object, CMDTYPE type) {
    game.getCommandManager().addCommand(Command(player(object->getOwner()), type, object->getObjectID()));
}
void command(Game& game, const ObjectBase* object, CMDTYPE type, Uint32 arg) {
    game.getCommandManager().addCommand(Command(player(object->getOwner()), type, object->getObjectID(), arg));
}
void command(Game& game, const ObjectBase* object, CMDTYPE type, Uint32 a, Uint32 b) {
    game.getCommandManager().addCommand(Command(player(object->getOwner()), type, object->getObjectID(), a, b));
}
void command(Game& game, const ObjectBase* object, CMDTYPE type, Uint32 a, Uint32 b, Uint32 c) {
    game.getCommandManager().addCommand(Command(player(object->getOwner()), type, object->getObjectID(), a, b, c));
}
Coord base(int house) { return Coord(4 + (house % 4) * 30, 4 + (house / 4) * 30); }
void load(std::unique_ptr<Game>& game, const std::string& filename) {
    clear(game); game = std::make_unique<Game>(); currentGame = game.get();
    game->bReplay = true; game->initGame(GameInitSettings(filename)); game->bReplay = false;
    game->initializeGameLoop(); game->bPause = false;
}
void world(std::unique_ptr<Game>& game, const std::string& mod, const std::vector<int>& houses, bool chaos = false) {
    clear(game);
    require(ModManager::instance().setActiveMod(mod), "Cannot activate fixture mod " + mod);
    settings.general.playerName = "Tornie state tester";
    auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    options.fogOfWar = false; options.immortalHumanPlayer = false; options.concreteRequired = false;
    options.instantBuild = false;
#ifdef SAVEGAMEVERSION_TORNIE546
    options.chaosMode = chaos; options.sellingBuildings = true;
#endif
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nLicense=CC0\nTechLevel=9\nWinFlags=3\nLoseFlags=1\n[MAP]\nSizeX=128\nSizeY=128\n";
    for(int y = 0; y < 128; ++y) map << std::setfill('0') << std::setw(3) << y << '=' << std::string(128, '+') << '\n';
    for(size_t row = 0; row < houses.size(); ++row) map << '[' << getHouseNameByNumber(static_cast<HOUSETYPE>(houses[row]))
        << "]\nCredits=50000\nBrain=Team" << row + 1 << '\n';
    map << "[STRUCTURES]\n";
    for(int h : houses) map << "ID" << h << '=' << getHouseNameByNumber(static_cast<HOUSETYPE>(h))
        << ",Construction Yard,256," << base(h).y * 128 + base(h).x << '\n';
    GameInitSettings init("tornie-state.ini", map.str(), "Tornie real-command QA", true, options);
    init.randomSeed = 5469877;
    for(size_t row = 0; row < houses.size(); ++row) {
        const int h = houses[row];
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h), static_cast<int>(row) + 1);
        info.addPlayerInfo({h == houses.front() ? settings.general.playerName : "Fixture " + std::to_string(h), "HumanPlayer"});
        init.addHouseInfo(info);
    }
    game = std::make_unique<Game>(); currentGame = game.get(); game->bReplay = true;
    game->initGame(init); game->bReplay = false; game->initializeGameLoop(); game->bPause = false;
    for(int h : houses) require(game->getHouse(static_cast<HOUSETYPE>(h)) != nullptr, "Named fixture house missing " + std::to_string(h));
    SDL_Log("TORNIE_GAMEPLAY_WORLD: mod=%s chaos=%d seed=%u houses=%zu tech=%d", mod.c_str(), int(chaos),
        init.randomSeed, houses.size(), game->techLevel);
}
StructureBase* place(House* house, int item, Coord at) {
    const Coord size = getStructureSize(item);
    for(int y = 0; y < size.y; ++y) for(int x = 0; x < size.x; ++x)
        currentGameMap->getTile(at + Coord(x,y))->setType(Terrain_Rock);
    auto* structure = house->placeStructure(NONE_ID, item, at.x, at.y, true, true);
    require(structure != nullptr, "Cannot place fixture structure " + std::to_string(item));
    return structure;
}
UnitBase* unit(House* house, int item, Coord at) {
    auto* result = house->createUnit(item, true);
    require(result != nullptr, "Cannot create fixture unit " + std::to_string(item));
    result->deploy(at); result->doSetAttackMode(STOP); result->setVisible(VIS_ALL, true);
    return result;
}
void relocate(UnitBase* unit, Coord at) {
    currentGameMap->removeObjectFromMap(unit->getObjectID()); unit->clearPath(); unit->setTarget(nullptr);
    unit->deploy(at); unit->doSetAttackMode(STOP); unit->setVisible(VIS_ALL, true);
}
Uint64 digest(Game& game) {
    const auto state = game.computeStateDigest();
    GameStateDigest::Hasher hash;
    hash.mixUint32(state.gameCycle); hash.mixUint32(state.randomSeed); hash.mixUint32(state.objectCount);
    hash.mixUint64(state.objectHash); hash.mixUint64(state.houseHash);
    for(int h = 0; h < NUM_HOUSES; ++h) {
        const auto* house = game.getHouse(static_cast<HOUSETYPE>(h));
        if(!house) continue;
        hash.mixInt64(house->getStartingCredits().getRawValue()); hash.mixInt64(house->getStoredCredits().getRawValue());
        hash.mixInt64(house->getCityCredits().getRawValue());
#ifdef SAVEGAMEVERSION_TORNIE546
        hash.mixInt32(house->getRadarResearchActive()); hash.mixInt32(house->getRadarResearchRemaining());
        for(int k = 0; k < radar::Count; ++k) hash.mixUint8(house->hasRadarResearch(k));
#endif
    }
#ifdef SAVEGAMEVERSION_TORNIE546
    hash.mixUint8(game.getChaosMode().isEnabled());
    for(int h = 0; h < NUM_HOUSES; ++h) for(int item : ChaosMode::Structures)
        hash.mixInt32(game.getChaosMode().getTechnologyHouse(h,item));
#endif
    game.getObjectManager().forEachObject([&](Uint32 id, const ObjectBase* object) {
        hash.mixUint32(id); hash.mixInt64(object->getRealX().getRawValue()); hash.mixInt64(object->getRealY().getRawValue());
        hash.mixInt64(object->angle.getRawValue()); hash.mixInt32(object->getDestination().x);
        hash.mixInt32(object->getDestination().y); hash.mixUint32(object->target.getObjectID());
#ifdef SAVEGAMEVERSION_TORNIE546
        hash.mixInt32(object->getProductionHouseID());
        if(const auto* u = dynamic_cast<const UnitBase*>(object)) {
            hash.mixInt32(u->getSlowTimer()); hash.mixInt64(u->dynastyStepTimeOffset.getRawValue());
        }
        if(const auto* r = dynamic_cast<const Refinery*>(object)) {
            hash.mixInt32(r->getNumBookings());
            for(int bay = 0; bay < r->getNumBays(); ++bay) {
                const auto* occupant = r->getContainedHarvester(bay); hash.mixUint32(occupant ? occupant->getObjectID() : NONE_ID);
            }
        }
        if(const auto* trap = dynamic_cast<const WindTrap*>(object)) hash.mixUint8(trap->isPowerUpgraded());
#endif
        if(const auto* worker = dynamic_cast<const Harvester*>(object)) hash.mixInt64(worker->getAmountOfSpice().getRawValue());
    });
    return hash.value();
}
using SavedObjects = std::vector<std::pair<Uint32,std::string>>;
SavedObjects savedObjects(Game& game, std::ofstream& frames, const char* phase) {
    SavedObjects result; result.reserve(game.getObjectManager().getObjectCount());
    game.getObjectManager().forEachObject([&](Uint32 id, const ObjectBase* object) {
        OMemoryStream stream; stream.open(); object->save(stream);
        std::string bytes(stream.getData(),stream.getDataLength());
        if(const auto* structure=dynamic_cast<const StructureBase*>(object)) {
            frames<<phase<<','<<id<<','<<structure->getItemID()<<','<<structure->lastVisibleFrame<<'\n';
            OMemoryStream prefix; prefix.open(); object->ObjectBase::save(prefix);
            // Animation counters are not saved. Only the fog-rendering cache is
            // allowed to differ; every other persisted object field must match.
            const auto offset=prefix.getDataLength()+2; // repairing, fogged, lastVisibleFrame
            std::fill(bytes.begin()+offset,bytes.begin()+offset+sizeof(Uint32),0);
        }
        result.emplace_back(id,std::move(bytes));
    });
    return result;
}
void continuation(std::unique_ptr<Game>& game, const std::string& label, int cycles) {
    const std::string save = out(label + ".dls");
    require(game->saveGame(save), "Cannot write continuation save");
    // The real loop's monotonic origin is runtime, not gameplay save data.
    // Match the existing unit-route continuation probe when replacing Game.
    const auto runtime = game->saveObserverRuntime();
    std::vector<Uint64> original; original.reserve(cycles + 1);
    std::ofstream csv(out(label + "-digests.csv")); csv << "phase,offset,cycle,digest\n";
    const Uint32 start = game->getGameCycleCount();
    for(int i = 0; i <= cycles; ++i) {
        if(i) game->updateGameState();
        original.push_back(digest(*game));
        csv << "original," << i << ',' << game->getGameCycleCount() << ',' << std::hex << original.back() << std::dec << '\n';
    }
    require(game->saveGame(out(label + "-original-final.dls")), "Cannot save original endpoint");
    std::ofstream frames(out(label+"-visual-frames.csv")); frames<<"phase,object,item,last_visible_frame\n";
    const auto persisted=savedObjects(*game,frames,"original");
    load(game, save);
    game->loadObserverRuntime(runtime);
    for(int i = 0; i <= cycles; ++i) {
        if(i) game->updateGameState();
        const auto actual = digest(*game);
        csv << "reload," << i << ',' << game->getGameCycleCount() << ',' << std::hex << actual << std::dec << '\n';
        if(actual != original[i]) {
            csv.flush(); game->saveGame(out(label + "-mismatch.dls"));
            std::ostringstream why; why << label << " continuation mismatch offset=" << i << " cycle=" << game->getGameCycleCount()
                << " original=" << std::hex << original[i] << " reload=" << actual;
            require(false, why.str());
        }
    }
    require(game->saveGame(out(label + "-reload-final.dls")), "Cannot save reloaded endpoint");
    require(savedObjects(*game,frames,"reload")==persisted,"Persisted object gameplay state changed across continuation");
    SDL_Log("TORNIE_GAMEPLAY_CONTINUATION: label=%s start=%u end=%u cycles=%d samples=%zu digest=%016llx equal=1",
        label.c_str(), start, game->getGameCycleCount(), cycles, original.size(), static_cast<unsigned long long>(original.back()));
}

#ifdef SAVEGAMEVERSION_TORNIE546
struct Base {
    BuilderBase* yard; BuilderBase* light; BuilderBase* heavy; BuilderBase* air; BuilderBase* barracks;
    BuilderBase* wor; BuilderBase* combo; StructureBase* radar; WindTrap* wind;
};
Base readyBase(Game& game, int h) {
    auto* house = game.getHouse(static_cast<HOUSETYPE>(h)); const Coord p = base(h);
    Base b{};
    for(auto* s : structureList) if(s->getOwner() == house && s->getItemID() == Structure_ConstructionYard) b.yard = dynamic_cast<BuilderBase*>(s);
    for(int i = 0; i < 5; ++i) b.wind = dynamic_cast<WindTrap*>(place(house, Structure_WindTrap, p + Coord(3 + i * 3,0)));
    b.radar = place(house, Structure_Radar, p + Coord(0,4));
    place(house, Structure_Refinery, p + Coord(3,4)); place(house, Structure_IX, p + Coord(7,4));
    place(house, Structure_StarPort, p + Coord(11,4));
    b.light = dynamic_cast<BuilderBase*>(place(house, Structure_LightFactory, p + Coord(0,8)));
    b.heavy = dynamic_cast<BuilderBase*>(place(house, Structure_HeavyFactory, p + Coord(4,8)));
    b.air = dynamic_cast<BuilderBase*>(place(house, Structure_HighTechFactory, p + Coord(8,8)));
    b.barracks = dynamic_cast<BuilderBase*>(place(house, Structure_Barracks, p + Coord(12,8)));
    b.wor = dynamic_cast<BuilderBase*>(place(house, Structure_WOR, p + Coord(0,12)));
    b.combo = dynamic_cast<BuilderBase*>(place(house, Structure_Worfinery, p + Coord(4,12)));
    house->updateBuildLists(); require(house->hasRadarOn(), "Fixture radar lacks real power");
    return b;
}
void upgrade(Game& game, const std::vector<std::pair<BuilderBase*,int>>& requests) {
    for(int round = 1; round <= 3; ++round) {
        for(const auto& [builder, level] : requests)
            if(builder->getCurrentUpgradeLevel() < std::min(round,level)) command(game,builder,CMD_BUILDER_UPGRADE);
        until(game,12000,[&] {
            return std::all_of(requests.begin(),requests.end(),[&](const auto& request) {
                return request.first->getCurrentUpgradeLevel() >= std::min(round,request.second);
            });
        },"Real factory upgrades did not finish");
    }
}
struct ManufactureOrder {
    BuilderBase* builder;
    int item;
    bool multiple = false;
    int count = 1;
    int before = 0;
    Uint32 next = 0;
};
void enqueue(Game& game, ManufactureOrder& order) {
    auto* builder = order.builder; const int item = order.item;
    builder->updateBuildList();
    require(builder->getBuildItem(item) != nullptr, "Authored build list lacks product " + std::to_string(item)
        + " builder=" + std::to_string(builder->getItemID()) + " house=" + std::to_string(builder->getOwner()->getHouseID())
        + " tech=" + std::to_string(builder->getTechnologyHouseID()));
    const int finalItem = item == Unit_Infantry5 ? Unit_Soldier : item == Unit_Troopers5 ? Unit_Trooper : item;
    order.before = builder->getOwner()->getNumItems(finalItem);
    order.next = game.getObjectManager().nextFreeObjectID;
    command(game,builder,CMD_BUILDER_PRODUCEITEM,item,order.multiple ? 1 : 0);
}
UnitBase* complete(Game& game, const ManufactureOrder& order) {
    auto* builder = order.builder; const int item = order.item;
    const int finalItem = item == Unit_Infantry5 ? Unit_Soldier : item == Unit_Troopers5 ? Unit_Trooper : item;
    until(game,30000,[&] {return builder->getOwner()->getNumItems(finalItem) >= order.before + order.count;},
        "Real manufacture did not deploy " + std::to_string(item));
    require(builder->getOwner()->getNumItems(finalItem) == order.before + order.count,"Manufacture produced the wrong unit count");
    UnitBase* result = nullptr;
    for(auto* u : unitList) if(u->getOwner() == builder->getOwner() && u->getItemID() == finalItem && u->getObjectID() >= order.next) {
        command(game,u,CMD_UNIT_SETMODE,STOP); if(!result) result = u;
    }
    advance(game,2); require(result != nullptr,"Manufactured product has no object");
    require(result->getProductionHouseID() == builder->getTechnologyHouseID(),"Manufacture lost its real blueprint faction");
    SDL_Log("TORNIE_GAMEPLAY_MANUFACTURE: item=%d actual=%d house=%d builder=%u technology=%d count=%d cycle=%u object=%u",
        item,finalItem,builder->getOwner()->getHouseID(),builder->getObjectID(),builder->getTechnologyHouseID(),order.count,
        game.getGameCycleCount(),result->getObjectID());
    return result;
}
UnitBase* manufacture(Game& game, BuilderBase* builder, int item) {
    ManufactureOrder order{builder,item}; enqueue(game,order); return complete(game,order);
}
void combat(Game& game, UnitBase* attacker, bool slow = false, bool gas = false) {
    const Coord site(87,100); relocate(attacker, site);
    const int ownerID=attacker->getOwner()->getHouseID(), item=attacker->getItemID();
    const Uint32 attackerID=attacker->getObjectID();
    const int foeID = ownerID == HOUSE_HARKONNEN ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
    // Aircraft approach from outside firing range instead of a point-blank orbit.
    const int distance=attacker->isAFlyingUnit() ? 16 : 3;
    auto* victim = unit(game.getHouse(static_cast<HOUSETYPE>(foeID)), Unit_Harvester, site + Coord(distance,0));
    const Uint32 victimID = victim->getObjectID(); const FixPoint health = victim->getHealth();
    require(attacker->canAttack(victim), "New unit cannot attack its real target");
    command(game, attacker, CMD_UNIT_ATTACKOBJECT, victimID);
    int sample = 0;
    until(game, gas ? 18000 : 5000, [&] {
        attacker=dynamic_cast<UnitBase*>(game.getObjectManager().getObject(attackerID));
        require(attacker!=nullptr,"Attacker died before its weapon affected the target");
        if(sample++ % 250 == 0) SDL_Log("TORNIE_GAMEPLAY_WEAPON_STATE: item=%d cycle=%u at=%d,%d real=%f,%f angle=%f dest=%d,%d target=%u mode=%d forced=%d reload=%d bullet=%d",
            attacker->getItemID(),game.getGameCycleCount(),attacker->getX(),attacker->getY(),attacker->getRealX().toDouble(),
            attacker->getRealY().toDouble(),attacker->angle.toDouble(),attacker->getDestination().x,attacker->getDestination().y,
            attacker->target.getObjectID(),attacker->getAttackMode(),int(attacker->wasForced()),attacker->primaryWeaponTimer,
            attacker->lastFiredBulletType);
        auto* v = dynamic_cast<UnitBase*>(game.getObjectManager().getObject(victimID));
        return !v || (slow ? v->getSlowTimer() > 0 : gas ? v->getOwner()->getHouseID() == ownerID : v->getHealth() < health);
    }, "New unit weapon did not affect its target " + std::to_string(item));
    auto* surviving = dynamic_cast<UnitBase*>(game.getObjectManager().getObject(victimID));
    SDL_Log("TORNIE_GAMEPLAY_COMBAT: item=%d cycle=%u target=%u health_before=%f health_after=%f slow=%d converted=%d attacker_health=%f",
        item,game.getGameCycleCount(),victimID,health.toDouble(),surviving ? surviving->getHealth().toDouble() : 0,
        surviving ? surviving->getSlowTimer() : 0,int(surviving && surviving->getOwner()->getHouseID()==ownerID),attacker->getHealth().toDouble());
    command(game,attacker,CMD_UNIT_SETMODE,STOP); advance(game,2);
    surviving=dynamic_cast<UnitBase*>(game.getObjectManager().getObject(victimID));
    if(surviving) surviving->cancelDeployment();
    // A real update may destroy a combatant, so reacquire fixture pointers.
    attacker=dynamic_cast<UnitBase*>(game.getObjectManager().getObject(attackerID));
    if(attacker) relocate(attacker,base(ownerID)+Coord(21,20));
}
BuilderBase* capture(Game& game, House* captor, BuilderBase* victim) {
    const Coord at = victim->getLocation(); const int item = victim->getItemID();
    const int production = victim->getProductionHouseID(); const int original = victim->getOriginalHouseID();
    const Uint32 old = victim->getObjectID(); victim->setHealth(victim->getMaxHealth() / 5);
    auto* soldier = unit(captor, Unit_Soldier, at + Coord(-1,0));
    command(game, soldier, CMD_INFANTRY_CAPTURE, old);
    BuilderBase* result = nullptr;
    until(game, 10000, [&] {
        for(auto* s : structureList) if(s->getOwner() == captor && s->getItemID() == item && s->getLocation() == at)
            result = dynamic_cast<BuilderBase*>(s);
        return result != nullptr;
    }, "Real infantry command did not capture factory");
    require(result->getProductionHouseID() == production && result->getOriginalHouseID() == original,
        "Captured factory did not retain donor technology/original faction");
    result->setHealth(result->getMaxHealth());
    SDL_Log("TORNIE_GAMEPLAY_CAPTURE: old=%u new=%u owner=%d original=%d production=%d technology=%d cycle=%u", old,
        result->getObjectID(), captor->getHouseID(), original, production, result->getTechnologyHouseID(), game.getGameCycleCount());
    return result;
}
void mechanics(std::unique_ptr<Game>& game) {
    const std::vector<int> houses{HOUSE_ORDOS,HOUSE_HARKONNEN,HOUSE_ATREIDES,HOUSE_WILDSPADE,HOUSE_KLESHMERSH,HOUSE_CUSTOM,HOUSE_NEUTRAL,HOUSE_REBELS};
    world(game,"Tornie",houses);
    std::array<Base,NUM_HOUSES> bases{};
    for(int h : houses) bases[h] = readyBase(*game,h);
    upgrade(*game,{{bases[HOUSE_KLESHMERSH].light,2},{bases[HOUSE_ORDOS].light,1},
        {bases[HOUSE_ATREIDES].heavy,2},{bases[HOUSE_WILDSPADE].air,3},
        {bases[HOUSE_ORDOS].barracks,2},{bases[HOUSE_ORDOS].combo,1}});
    std::array<ManufactureOrder,9> orders{{
        {bases[HOUSE_KLESHMERSH].light,Unit_FlameQuad},{bases[HOUSE_ORDOS].wor,Unit_GasInfantry},
        {bases[HOUSE_WILDSPADE].air,Unit_EliteOrnithopter},{bases[HOUSE_ORDOS].light,Unit_RaiderQuad,true,5},
        {bases[HOUSE_ATREIDES].heavy,Unit_RangedTank},{bases[HOUSE_CUSTOM].heavy,Unit_SlowTank},
        {bases[HOUSE_ORDOS].barracks,Unit_Infantry5,false,5},{bases[HOUSE_ORDOS].combo,Unit_Troopers5,false,5},
        {bases[HOUSE_NEUTRAL].combo,Unit_Saboteur}
    }};
    for(auto& order : orders) enqueue(*game,order);
    std::array<UnitBase*,9> products{};
    for(size_t i=0;i<orders.size();++i) products[i]=complete(*game,orders[i]);
    auto* flame=products[0]; auto* gas=products[1]; auto* elite=products[2]; auto* raider=products[3];
    auto* ranged=products[4]; auto* slow=products[5]; auto* neutralSaboteur=products[8];
    auto* ordos = game->getHouse(HOUSE_ORDOS);
    auto* foreignFastFactory = capture(*game,ordos,bases[HOUSE_ATREIDES].heavy);
    auto* fast = dynamic_cast<Harvester*>(manufacture(*game,foreignFastFactory,Unit_FastHarvester));
    auto* foreignCapacityFactory = capture(*game,ordos,bases[HOUSE_WILDSPADE].heavy);
    auto* capacity = dynamic_cast<Harvester*>(manufacture(*game,foreignCapacityFactory,Unit_CapacityHarvester));
    require(fast && capacity && capacity->getSpiceCapacity() * 2 == fast->getSpiceCapacity() * 3, "Harvester capacity bonus incorrect");
    require(fast->getMaxSpeed() == game->objectData.data[Unit_FastHarvester][HOUSE_ATREIDES].maxspeed, "Fast Harvester speed does not use captured tech");
    auto* cy = dynamic_cast<ConstructionYard*>(bases[HOUSE_ORDOS].yard);
    cy->updateBuildList(); require(cy->getBuildItem(Structure_Doublefinery), "Authored yard lacks Doublefinery");
    command(*game,cy,CMD_BUILDER_PRODUCEITEM,Structure_Doublefinery,0);
    until(*game,10000,[&] {return cy->isWaitingToPlace();},"Doublefinery manufacture did not finish");
    const Coord refineryAt = base(HOUSE_ORDOS) + Coord(8,17);
    for(int y=0;y<2;++y) for(int x=0;x<5;++x) currentGameMap->getTile(refineryAt+Coord(x,y))->setType(Terrain_Rock);
    command(*game,cy,CMD_PLACE_STRUCTURE,refineryAt.x,refineryAt.y); advance(*game,2);
    Refinery* doublefinery = nullptr;
    for(auto* s:structureList) if(s->getLocation()==refineryAt && s->getItemID()==Structure_Doublefinery) doublefinery=dynamic_cast<Refinery*>(s);
    require(doublefinery && doublefinery->getNumBays()==2,"Real yard placement did not create Doublefinery");
    require(game->saveGame(out("manufactured-before-combat.dls")),"Cannot retain manufactured units evidence");
    SDL_Log("TORNIE_GAMEPLAY_MANUFACTURE: item=%d house=%d object=%u bays=2 cycle=%u",Structure_Doublefinery,HOUSE_ORDOS,doublefinery->getObjectID(),game->getGameCycleCount());
    combat(*game,flame); combat(*game,raider); combat(*game,ranged); combat(*game,elite); combat(*game,slow,true); combat(*game,gas,false,true);
    relocate(gas,Coord(95,112)); relocate(raider,Coord(98,112)); raider->setHealth(raider->getMaxHealth()/2);
    const FixPoint hurt=raider->getHealth(); command(*game,gas,CMD_UNIT_HEAL,raider->getObjectID());
    until(*game,2000,[&] {return raider->getHealth()>hurt;},"Gas Infantry real healing missile did not heal");
    SDL_Log("TORNIE_GAMEPLAY_HEAL: before=%f after=%f",hurt.toDouble(),raider->getHealth().toDouble());
    relocate(neutralSaboteur,Coord(95,118));
    auto* doomed=place(game->getHouse(HOUSE_HARKONNEN),Structure_Silo,Coord(98,118));
    const Uint32 saboteurID=neutralSaboteur->getObjectID(), doomedID=doomed->getObjectID();
    command(*game,neutralSaboteur,CMD_UNIT_ATTACKOBJECT,doomedID);
    until(*game,3000,[&] {return game->getObjectManager().getObject(doomedID)==nullptr;},"Neutral manufactured Saboteur did not detonate");
    require(game->getObjectManager().getObject(saboteurID)==nullptr,"Neutral Saboteur survived its detonation");
    auto* rebels=game->getHouse(HOUSE_REBELS); auto* trap=bases[HOUSE_REBELS].wind;
    const int power=trap->getProducedPower(), cost=rebels->getCredits(), housePower=rebels->getProducedPower();
    command(*game,trap,CMD_WINDTRAP_POWER_UPGRADE); advance(*game,2);
    require(trap->isPowerUpgraded() && trap->getProducedPower()==power*2 && rebels->getProducedPower()==housePower+power
        && rebels->getCredits()==cost-150,"Windtrap command upgrade house power/cost incorrect");
    const Uint32 saleID=doublefinery->getObjectID(); const int refund=doublefinery->getSaleRefund(), credits=ordos->getCredits();
    command(*game,doublefinery,CMD_STRUCTURE_SELL); advance(*game,2);
    require(!game->getObjectManager().getObject(saleID) && ordos->getCredits()==credits+refund,"Building sale command refund/removal incorrect");
    SDL_Log("TORNIE_GAMEPLAY_RULES: neutral_saboteur=1 squads=5,5 shift_order=5 windtrap_before=%d after=%d upgrade_cost=150 sale_refund=%d",power,trap->getProducedPower(),refund);
    continuation(game,"mechanics",128);
}
void busyState(std::unique_ptr<Game>& game) {
    world(game,"Tornie",{HOUSE_ORDOS,HOUSE_ATREIDES,HOUSE_CUSTOM,HOUSE_HARKONNEN},true);
    const Base ordosBase=readyBase(*game,HOUSE_ORDOS), foreignBase=readyBase(*game,HOUSE_ATREIDES);
    readyBase(*game,HOUSE_CUSTOM); readyBase(*game,HOUSE_HARKONNEN);
    auto* owner=game->getHouse(HOUSE_ORDOS);
    auto* captured=capture(*game,owner,foreignBase.heavy);
    const Uint32 capturedID=captured->getObjectID(); const int capturedTechnology=captured->getTechnologyHouseID();
    auto* refinery=dynamic_cast<Refinery*>(place(owner,Structure_Doublefinery,Coord(80,100)));
    // Cargo is gathered from real spice, not assigned to a bay or written into a harvester.
    for(int y=105;y<=111;++y) for(int x=79;x<=89;++x) currentGameMap->getTile(x,y)->setType(Terrain_ThickSpice);
    auto* fast=dynamic_cast<Harvester*>(unit(owner,Unit_FastHarvester,Coord(81,106)));
    auto* capacity=dynamic_cast<Harvester*>(unit(owner,Unit_CapacityHarvester,Coord(86,106)));
    command(*game,fast,CMD_UNIT_SETMODE,HARVEST); command(*game,capacity,CMD_UNIT_SETMODE,HARVEST);
    until(*game,15000,[&] {return fast->getAmountOfSpice()>=400 && capacity->getAmountOfSpice()>=400;},"Real spice harvesting did not fill both variant harvesters");
    // Equal short routes; right-clicking this specific building is a real return
    // order, unlike the generic Return key's least-booked-refinery selection.
    relocate(fast,Coord(80,103)); relocate(capacity,Coord(84,103));
    command(*game,fast,CMD_UNIT_MOVE2OBJECT,refinery->getObjectID());
    command(*game,capacity,CMD_UNIT_MOVE2OBJECT,refinery->getObjectID());
    until(*game,5000,[&] {return refinery->getNumOccupiedBays()==2;},"Real targeted return commands did not occupy both Doublefinery bays");
    // A real Slow Tank projectile creates the persisted debuff.
    auto* slowing=unit(game->getHouse(HOUSE_CUSTOM),Unit_SlowTank,Coord(97,112));
    auto* slowed=unit(owner,Unit_Tank,Coord(101,112));
    const int sightBefore=slowed->getViewRange();
    command(*game,slowing,CMD_UNIT_ATTACKOBJECT,slowed->getObjectID());
    until(*game,1000,[&] {return slowed->getSlowTimer()>0;},"Slow Tank projectile did not create saved slow state");
    require(slowed->getMovementSpeedFactor()==0.5_fix,"Slow projectile did not halve real movement speed");
    command(*game,slowing,CMD_UNIT_SETMODE,STOP); command(*game,slowed,CMD_UNIT_MOVE2POS,105,112,1);
    command(*game,ordosBase.radar,CMD_RADAR_RESEARCH,radar::ExtendedVision); advance(*game,2);
    require(owner->getRadarResearchActive()==radar::ExtendedVision && owner->getRadarResearchRemaining()>0,"Radar research command did not start");
    require(refinery->getNumOccupiedBays()==2 && fast->getAmountOfSpice()>0 && capacity->getAmountOfSpice()>0,"Busy bays drained before the save boundary");
    int exchanged=0;
    for(int h=0;h<NUM_PLAYER_HOUSES;++h) for(int item:ChaosMode::Structures) exchanged+=game->getChaosMode().getTechnologyHouse(h,item)!=h;
    require(game->getChaosMode().isEnabled() && exchanged>0,"Chaos table is not live");
    std::ofstream fields(out("busy-state-fields.txt"));
    fields<<"cycle="<<game->getGameCycleCount()<<" rng="<<game->randomGen.getSeed()<<" chaos_exchanged="<<exchanged<<'\n';
    fields<<"refinery="<<refinery->getObjectID()<<" bookings="<<refinery->getNumBookings()<<" occupied="<<refinery->getNumOccupiedBays()<<'\n';
    for(int bay=0;bay<2;++bay) {const auto* u=dynamic_cast<const Harvester*>(refinery->getContainedHarvester(bay));
        fields<<"bay="<<bay<<" object="<<u->getObjectID()<<" item="<<u->getItemID()<<" cargo_raw="<<u->getAmountOfSpice().getRawValue()<<" active="<<u->isActive()<<'\n';}
    fields<<"slowed="<<slowed->getObjectID()<<" timer="<<slowed->getSlowTimer()<<" speed_factor_raw="<<slowed->getMovementSpeedFactor().getRawValue()<<" step_offset_raw="<<slowed->dynastyStepTimeOffset.getRawValue()<<'\n';
    fields<<"research_active="<<owner->getRadarResearchActive()<<" remaining="<<owner->getRadarResearchRemaining()<<'\n';
    fields<<"captured="<<capturedID<<" owner="<<captured->getOwner()->getHouseID()<<" original="<<captured->getOriginalHouseID()<<" production="<<captured->getProductionHouseID()<<" technology="<<capturedTechnology<<'\n';
    for(int h=0;h<NUM_HOUSES;++h) for(int item:ChaosMode::Structures) fields<<"chaos house="<<h<<" item="<<item<<" donor="<<game->getChaosMode().getTechnologyHouse(h,item)<<'\n';
    fields.close();
    const Uint32 refineryID=refinery->getObjectID(),slowedID=slowed->getObjectID();
    continuation(game,"busy-state",3200);
    owner=game->getHouse(HOUSE_ORDOS);
    require(owner->hasRadarResearch(radar::ExtendedVision) && owner->getRadarResearchActive()==-1,"Reloaded research did not finish");
    require(dynamic_cast<Refinery*>(game->getObjectManager().getObject(refineryID))->getNumOccupiedBays()==0,"Reloaded bays did not deploy their cargo");
    auto* resumed=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(slowedID));
    require(resumed && resumed->getSlowTimer()==0 && resumed->getLocation()==Coord(105,112),
        "Reloaded slowed unit did not finish its real move and expire the debuff");
    require(resumed->getViewRange()==sightBefore+2,"Completed radar research did not extend real unit sight");
    require(dynamic_cast<BuilderBase*>(game->getObjectManager().getObject(capturedID))->getTechnologyHouseID()==capturedTechnology,"Reloaded captured technology changed");
    Worfinery* offer=nullptr;
    for(int i=0;i<80 && !offer;++i) {
        auto* candidate=dynamic_cast<Worfinery*>(place(owner,Structure_Worfinery,Coord(4+(i%16)*4,78+(i/16)*3)));
        if(candidate->hasTameWormOffer()) offer=candidate;
    }
    require(offer,"Synchronized Chaos Worfinery offers produced no tame worm");
    auto* worm=manufacture(*game,offer,Unit_TameSandworm);
    for(int y=97;y<106;++y) for(int x=86;x<105;++x) currentGameMap->getTile(x,y)->setType(Terrain_Sand);
    combat(*game,worm);
    SDL_Log("TORNIE_GAMEPLAY_BUSY: chaos=1 research_completed=1 bays_released=2 slow_expired=1 moved=105,112 sight_before=%d sight_after=%d captured_tech=%d tame_offer=1",
        sightBefore,resumed->getViewRange(),capturedTechnology);
}
void aircraftOrders(std::unique_ptr<Game>& game) {
    for(const std::string mod : {"Tornie","vanilla","dunecity","Dune2R"}) {
        const bool family=mod=="Tornie";
        const int h=family ? HOUSE_WILDSPADE : HOUSE_ATREIDES;
        for(int item : {Unit_Ornithopter,Unit_EliteOrnithopter}) {
            if(!family && item==Unit_EliteOrnithopter) continue;
            world(game,mod,{h,HOUSE_HARKONNEN});
            auto* owner=game->getHouse(static_cast<HOUSETYPE>(h));
            auto* enemy=game->getHouse(HOUSE_HARKONNEN);
            auto* air=unit(owner,item,Coord(20,90));
            air->angle=RIGHT; air->drawnAngle=RIGHT;
            auto* worker=unit(enemy,Unit_Harvester,Coord(65,90));
            place(enemy,Structure_ConstructionYard,Coord(24,86));
            const Uint32 airID=air->getObjectID(),workerID=worker->getObjectID();
            command(*game,air,CMD_UNIT_ATTACKOBJECT,workerID);
            advance(*game,2);
            for(int tick=0;tick<96;++tick) {
                game->updateGameState();
                air=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(airID));
                require(air && air->getTarget()==worker && air->wasForced(),
                    "Explicit aircraft worker target was replaced during refresh cadence");
            }
            require(air->getAttackMode()==(family ? HUNT : STOP),"Aircraft order changed unrelated-mod mode");
            // A real Saboteur attack ends the selected target before the distant
            // aircraft arrives. Target loss must release forced without losing Hunt.
            auto* saboteur=unit(owner,Unit_Saboteur,Coord(64,90));
            command(*game,saboteur,CMD_UNIT_ATTACKOBJECT,workerID);
            until(*game,100,[&] {return game->getObjectManager().getObject(workerID)==nullptr;},
                "Real fixture Saboteur did not end the aircraft target");
            advance(*game,2);
            air=dynamic_cast<UnitBase*>(game->getObjectManager().getObject(airID));
            require(air && !air->wasForced() && air->getAttackMode()==(family ? HUNT : STOP),
                "Aircraft did not preserve family Hunt/legacy mode after target death");
            SDL_Log("TORNIE_GAMEPLAY_AIRCRAFT_ORDER: mod=%s item=%d held_cycles=96 forced_released=1 follow_on_hunt=%d",
                mod.c_str(),item,int(family));
        }
    }
}
void legacy(std::unique_ptr<Game>& game, const std::string& filename) {
    load(game,filename); require(game->getLoadedSavegameVersion()==9877,"Compatibility input was not an authentic 9877 save");
    require(!game->getChaosMode().isEnabled() && !game->getGameInitSettings().isChaosModeEnabled(),"Legacy Chaos defaults changed");
    for(int h=0;h<NUM_HOUSES;++h) if(const auto* house=game->getHouse(static_cast<HOUSETYPE>(h))) {
        require(house->getRadarResearchActive()==-1 && house->getRadarResearchRemaining()==0,"Legacy radar defaults changed");
        for(int k=0;k<radar::Count;++k) require(!house->hasRadarResearch(k),"Legacy save gained research");
    }
    for(auto* u:unitList) require(u->getSlowTimer()==0,"Legacy unit gained slow state");
    for(auto* s:structureList) if(const auto* trap=dynamic_cast<const WindTrap*>(s)) require(!trap->isPowerUpgraded(),"Legacy Windtrap gained upgrade");
    const std::string mod=ModManager::instance().getActiveModName();
    continuation(game,"legacy-9877-"+mod,256);
    SDL_Log("TORNIE_GAMEPLAY_LEGACY: input=%s mod=%s version=9877 defaults=1 continue=256",filename.c_str(),mod.c_str());
}
#endif
} // namespace TornieStateProbe

void runTornieStateProbe(std::unique_ptr<Game>& game) {
    using namespace TornieStateProbe;
#ifdef SAVEGAMEVERSION_TORNIE546
    if(const char* filename=std::getenv("TORNIE_STATE_LEGACY_SAVE")) legacy(game,filename);
    else {
        const std::string section=std::getenv("TORNIE_STATE_SECTION") ? std::getenv("TORNIE_STATE_SECTION") : "all";
        if(section=="aircraft") aircraftOrders(game);
        else {
            if(section!="busy") mechanics(game);
            if(section!="mechanics") busyState(game);
            if(section=="all") aircraftOrders(game);
        }
    }
    SDL_Log("TORNIE_GAMEPLAY_PASS: checks=%d save_version=%d",checks,SAVEGAMEVERSION);
#else
    const std::string mod=std::getenv("TORNIE_STATE_MOD");
    world(game,mod,{HOUSE_ATREIDES,HOUSE_HARKONNEN});
    place(game->getHouse(HOUSE_ATREIDES),Structure_WindTrap,Coord(8,4));
    place(game->getHouse(HOUSE_ATREIDES),Structure_Refinery,Coord(12,4));
    auto* tank=unit(game->getHouse(HOUSE_ATREIDES),Unit_Tank,Coord(20,20));
    command(*game,tank,CMD_UNIT_MOVE2POS,30,20,1); advance(*game,300);
    require(SAVEGAMEVERSION==9877,"Baseline build must genuinely write 9877");
    const auto path=out("authentic-9877-"+mod+".dls"); require(game->saveGame(path),"Cannot create authentic main save");
    load(game,path); require(game->getLoadedSavegameVersion()==9877,"Main could not reload its own authentic save");
    advance(*game,256);
    SDL_Log("TORNIE_GAMEPLAY_BASELINE: mod=%s version=%d cycle=%u digest=%016llx save=%s",mod.c_str(),SAVEGAMEVERSION,
        game->getGameCycleCount(),static_cast<unsigned long long>(digest(*game)),path.c_str());
#endif
}
int runTornieStateProbeDriver() {
    std::unique_ptr<Game> game;
    runTornieStateProbe(game); TornieStateProbe::clear(game); return MENU_QUIT_DEFAULT;
}
