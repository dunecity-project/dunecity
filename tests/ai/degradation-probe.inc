// Real-engine checks for Dynasty-aligned structure degradation, run in every
// mod. Everything here drives production code: structures come from
// House::placeStructure, damage comes from StructureBase::updateDegradation(),
// and the save checks round-trip a real Silo through the real stream format.
// Nothing reimplements the rules; the expected values are the Dynasty ones.
#include <structures/Silo.h>
#include <structures/WindTrap.h>
#include <structures/AdvancedWindTrap.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

void runStructureDegradationProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("DEGRADATION_FAIL: %s",why);throw std::runtime_error(why);}};
    // Dynasty ticks: house power every 15 s, foundation decay every 180 s.
    const int powerInterval=MILLI2CYCLES(15*1000);
    const int decayInterval=MILLI2CYCLES(180*1000);
    require(powerInterval==937 && decayInterval==11250,"Degradation interval lengths changed");
    // Dynasty g_table_houseInfo degradingAmount.
    auto degradingAmount=[](int houseID){return houseID==HOUSE_HARKONNEN ? 3 : houseID==HOUSE_ORDOS ? 2 : 1;};

    auto* house=pLocalHouse;
    house->setAutoRepairEnabled(false);
    const bool concreteRequired=game.getGameInitSettings().getGameOptions().concreteRequired;
    const auto& mods = ModManager::instance();
    const std::string contentBase = mods.getContentBase(mods.getActiveModName());
    require(concreteRequired==(contentBase!="Tornie" && contentBase!="dunecity"),
            "Degradation fixture did not load the selected mod's concrete default");
    // Family defaults must not upgrade/rewrite the nonfamily AI configurations:
    // doing so changes saved-mod identity before degradation is even exercised.
    const QuantBotConfig defaults;
    const bool family = mods.isTornieFamilyContentActive();
    for (const char* name : {"Doublefinery", "Slowpost"})
        require((defaults.findStructurePriority(name) != nullptr) == family,
                "Family structure priorities leaked across mod boundaries");
    for (const char* name : {"Troopers5", "Infantry5", "FlameQuad", "GasInfantry", "FlameTrooper",
             "EliteOrnithopter", "RaiderQuad", "RangedTank", "SlowTank", "CapacityHarvester", "FastHarvester", "TameSandworm"})
        require((defaults.findUnitPriority(name) != nullptr) == family,
                "Family unit priorities leaked across mod boundaries");

    // Diagnostic fixture ground: free rock so every foundation case is placeable.
    for(int y=0;y<currentGameMap->getSizeY();++y)
        for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAGroundObject())currentGameMap->getTile(x,y)->setType(Terrain_Rock);

    int nextX=2,nextY=2;
    auto site=[&](int w,int h)->Coord {
        for(int y=nextY;y<currentGameMap->getSizeY()-h-2;++y) {
            for(int x=(y==nextY ? nextX : 2);x<currentGameMap->getSizeX()-w-2;++x) {
                bool clear=true;
                for(int dy=-1;dy<=h && clear;++dy)
                    for(int dx=-1;dx<=w && clear;++dx)
                        if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
                if(clear){nextX=x+w+2;nextY=y;return Coord(x,y);}
            }
            nextX=2;
        }
        throw std::runtime_error("No degradation fixture site");
    };
    auto place=[&](Uint32 item,Coord pos)->StructureBase* {
        auto* placed=house->placeStructure(NONE_ID,item,pos.x,pos.y,false,true);
        require(placed!=nullptr,"Fixture placement failed");
        return placed;
    };
    auto concreteAt=[&](int x,int y){house->placeStructure(NONE_ID,Structure_Slab1,x,y,true,true);};
    auto setPower=[](House* h,int produced,int required){h->producedPower=produced;h->powerRequirement=required;};
    auto reset=[&](StructureBase* s,bool foundation){
        s->setHealth(FixPoint(s->getMaxHealth()));
        s->foundationDegrades=foundation;
        s->powerDamageTimer=powerInterval;
        s->foundationDecayTimer=decayInterval;
    };
    auto run=[](StructureBase* s,int cycles){for(int i=0;i<cycles;++i)s->updateDegradation();};

    // --- placement: complete, missing and partial foundations -----------------
    const Coord fullSite=site(2,2);
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)concreteAt(fullSite.x+dx,fullSite.y+dy);
    auto* founded=place(Structure_Silo,fullSite);
    const int maxHealth=founded->getMaxHealth();
    require(!founded->foundationDegrades,"Complete foundation is still marked as decaying");
    require(founded->getHealth()==FixPoint(maxHealth),"Complete foundation cost placement health");

    const Coord bareSite=site(2,2);
    auto* bare=place(Structure_Silo,bareSite);
    const int bareMax=bare->getMaxHealth();
    // Four missing tiles cost maxHealth/(2*tiles) each: the unchanged half-health penalty.
    FixPoint expectedBare=FixPoint(bareMax);
    if(concreteRequired)for(int i=0;i<4;++i)expectedBare=expectedBare-FixPoint(bareMax)/8;
    require(bare->foundationDegrades==concreteRequired,"Missing foundation flag does not follow the concrete rules");
    require(bare->getHealth()==expectedBare,"Missing foundation placement penalty changed");

    const Coord partialSite=site(2,2);
    concreteAt(partialSite.x,partialSite.y);concreteAt(partialSite.x+1,partialSite.y);
    auto* partial=place(Structure_Silo,partialSite);
    FixPoint expectedPartial=FixPoint(partial->getMaxHealth());
    if(concreteRequired)for(int i=0;i<2;++i)expectedPartial=expectedPartial-FixPoint(partial->getMaxHealth())/8;
    require(partial->foundationDegrades==concreteRequired,"Partial foundation flag does not follow the concrete rules");
    require(partial->getHealth()==expectedPartial,"Partial foundation penalty is not charged per tile");

    // --- power damage on the raw owner figures --------------------------------
    setPower(house,0,100);
    reset(founded,false);
    run(founded,powerInterval-1);
    require(founded->getHealth()==FixPoint(maxHealth),"Power damage fired before its interval");
    run(founded,1);
    require(founded->getHealth()==FixPoint(maxHealth-1),"Power shortage did not cost exactly one hitpoint");
    // Vanilla reports full power regardless; degradation must not depend on it.
    if(!house->isPowerRequired())require(house->hasPower(),"Vanilla power predicate changed unexpectedly");
    run(founded,powerInterval*3);
    require(founded->getHealth()==FixPoint(maxHealth-4),"Power damage rate changed");

    setPower(house,200,200);
    reset(founded,false);
    run(founded,powerInterval*5);
    require(founded->getHealth()==FixPoint(maxHealth),"Covered power still degraded a structure");

    setPower(house,0,0);
    reset(founded,false);
    run(founded,powerInterval*5);
    require(founded->getHealth()==FixPoint(maxHealth),"A house without power demand degraded");

    // Partial supply stops at its own threshold, well above half health.
    setPower(house,3,4);
    reset(founded,false);
    const int partialThreshold=(maxHealth*3)/4;
    run(founded,powerInterval*(maxHealth+2));
    require(partialThreshold>maxHealth/2,"Fixture cannot distinguish the partial threshold from half health");
    require(founded->getHealth()==FixPoint(partialThreshold),"Partial power did not stop at its threshold");

    // No supply stops at half health, and restoring power never heals.
    setPower(house,0,100);
    reset(founded,false);
    run(founded,powerInterval*(maxHealth+2));
    require(founded->getHealth()==FixPoint(maxHealth/2),"Power damage did not stop at half health");
    const FixPoint damaged=founded->getHealth();
    setPower(house,400,100);
    run(founded,powerInterval*4);
    require(founded->getHealth()==damaged,"Restored power healed the structure");

    // Disabled foundations also ignore a saved/stale missing-foundation flag.
    auto& mutableOptions=const_cast<SettingsClass::GameOptionsClass&>(game.getGameInitSettings().getGameOptions());
    if (!concreteRequired) {
        setPower(house,200,200);reset(bare,true);run(bare,decayInterval);
        require(bare->getHealth()==FixPoint(bareMax),"Disabled foundations still caused damage");
        const Coord disabledSlabSite=site(2,2);
        house->placeStructure(NONE_ID,Structure_Slab1,disabledSlabSite.x,disabledSlabSite.y,false,true);
        require(currentGameMap->getTile(disabledSlabSite)->getType()!=Terrain_Slab,"Disabled concrete was placeable");
    }
    auto* slabYard=dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard,site(2,2)));
    require(slabYard!=nullptr,"Slab-menu fixture needs a yard");
    slabYard->curUpgradeLev=slabYard->getMaxUpgradeLevel();
    mutableOptions.concreteRequired=false;
    slabYard->updateBuildList();
    require(!slabYard->isAvailableToBuild(Structure_Slab1) && !slabYard->isAvailableToBuild(Structure_Slab4),
        "Disabled concrete remained in construction menu");
    slabYard->doProduceItem(Structure_Slab1);
    require(slabYard->getProductionQueueSize()==0,"Disabled concrete entered a production queue");
    // Explicitly enable foundations for the independent mechanics cases below.
    mutableOptions.concreteRequired=true;

    // --- foundation decay ------------------------------------------------------
    setPower(house,200,200);        // covered power isolates decay
    reset(bare,true);
    run(bare,decayInterval-1);
    require(bare->getHealth()==FixPoint(bareMax),"Foundation decay fired before 180 seconds");
    run(bare,1);
    require(bare->getHealth()==FixPoint(bareMax-degradingAmount(house->getHouseID())),
            "Foundation decay did not cost the house amount");

    House* bareOwner=bare->owner;
    for(int h : {HOUSE_HARKONNEN,HOUSE_ORDOS,HOUSE_ATREIDES,HOUSE_SARDAUKAR,HOUSE_MERCENARY}) {
        auto* other=game.getHouse(h);
        if(other==nullptr)continue;
        setPower(other,200,200);
        bare->owner=other;
        reset(bare,true);
        run(bare,decayInterval);
        require(bare->getHealth()==FixPoint(bareMax-degradingAmount(h)),"House degrading amount changed");
    }
    bare->owner=bareOwner;
    setPower(house,200,200);

    // The last hit is applied in full and may undershoot half health; decay stops there.
    reset(bare,true);
    const int half=bareMax/2;
    bare->setHealth(FixPoint(half+1));
    run(bare,decayInterval);
    const FixPoint undershot=FixPoint(half+1-degradingAmount(house->getHouseID()));
    require(bare->getHealth()==undershot,"Foundation decay clamped its last hit");
    run(bare,decayInterval*3);
    require(bare->getHealth()==undershot,"Foundation decay continued below half health");

    // A complete foundation never decays, however long it stands.
    reset(founded,false);
    run(founded,decayInterval*3);
    require(founded->getHealth()==FixPoint(maxHealth),"A complete foundation decayed");

    // Both mechanisms must run independently through the real update entry point.
    reset(bare,true);
    setPower(house,0,100);
    bare->powerDamageTimer=1;bare->foundationDecayTimer=1;
    bare->update();
    require(bare->getHealth()==FixPoint(bareMax-degradingAmount(house->getHouseID())-1),
            "Power damage and foundation decay did not both run through update");
    bare->setHealth(FixPoint(half+1));
    bare->powerDamageTimer=1;bare->foundationDecayTimer=1;
    bare->updateDegradation();
    require(bare->getHealth()==undershot,"Simultaneous threshold hits did not process foundation decay first");

    // --- walls are excluded from both mechanisms --------------------------------
    const Coord wallSite=site(1,1);
    auto* wall=place(Structure_Wall,wallSite);
    require(wall->getHealth()==FixPoint(wall->getMaxHealth()),"Wall lost placement health on bare rock");
    setPower(house,0,100);
    wall->foundationDegrades=true;      // even when forced, a wall must not degrade
    wall->powerDamageTimer=powerInterval;
    wall->foundationDecayTimer=decayInterval;
    run(wall,decayInterval);
    require(wall->getHealth()==FixPoint(wall->getMaxHealth()),"Walls must not degrade");

    // --- campaign exemption ------------------------------------------------------
    auto& init=const_cast<GameInitSettings&>(game.getGameInitSettings());
    const int originalMission=init.mission;      // test-only access (-fno-access-control)
    const GameType originalType=game.gameType;
    setPower(house,200,200);
    for(int mission : {1,2,4}) {                 // campaign levels 1 and 2
        init.mission=mission;
        reset(bare,true);
        run(bare,decayInterval*2);
        require(bare->getHealth()==FixPoint(bareMax),"An exempt campaign level lost health to foundation decay");
    }
    init.mission=5;                              // campaign level 3
    reset(bare,true);
    run(bare,decayInterval);
    require(bare->getHealth()==FixPoint(bareMax-degradingAmount(house->getHouseID())),
            "A later campaign level skipped foundation decay");
    init.mission=1;                              // power damage is never exempt
    setPower(house,0,100);
    reset(bare,true);
    run(bare,powerInterval);
    require(bare->getHealth()==FixPoint(bareMax-1),"The campaign exemption wrongly covered power damage");
    game.gameType=GameType::CustomGame;init.mission=0;
    setPower(house,200,200);
    reset(bare,true);
    run(bare,decayInterval);
    require(bare->getHealth()==FixPoint(bareMax-degradingAmount(house->getHouseID())),
            "A custom game skipped foundation decay");
    game.gameType=originalType;init.mission=originalMission;

    // Real windtrap bookkeeping: placement, damage, healing, and lethal damage.
    auto* wind=dynamic_cast<WindTrap*>(place(Structure_WindTrap,site(2,2)));
    require(wind!=nullptr,"Windtrap fixture missing");
    wind->setHealth(wind->getMaxHealth());
    const int nominal=abs(game.objectData.data[Structure_WindTrap][wind->getOriginalHouseID()].power);
    const int fullTotal=house->getProducedPower();
    wind->setHealth(FixPoint(wind->getMaxHealth())/2);
    require(wind->getProducedPower()==nominal/2 && house->getProducedPower()==fullTotal-nominal+nominal/2,
        "Half-health windtrap did not halve house generation");
    wind->setHealth(FixPoint(wind->getMaxHealth())/4);
    require(wind->getProducedPower()==nominal/4,"Windtrap output retained an original-Dune half-power floor");
    wind->setHealth(wind->getMaxHealth());
    require(house->getProducedPower()==fullTotal,"Windtrap repair did not restore generation");
    wind->setHealth(0);
    require(house->getProducedPower()==fullTotal-nominal,"Lethal windtrap damage retained generation");

    if (ModManager::instance().getActiveModName()=="Tornie") {
        for (Uint32 item : {Structure_AdvancedWindTrap,Structure_AdvancedWindTrapMK2,Structure_AdvancedWindTrapMK3}) {
            const Coord size=getStructureSize(item);
            auto* advanced=dynamic_cast<AdvancedWindTrap*>(place(item,site(size.x,size.y)));
            require(advanced!=nullptr,"Advanced windtrap fixture missing");
            advanced->setHealth(advanced->getMaxHealth());
            const int output=advanced->getProducedPower(), total=house->getProducedPower();
            advanced->setHealth(FixPoint(advanced->getMaxHealth())/2);
            require(advanced->getProducedPower()==output/2 && house->getProducedPower()==total-output+output/2,
                "Advanced windtrap damage did not update generation");
            advanced->setHealth(advanced->getMaxHealth());
            require(house->getProducedPower()==total,"Advanced windtrap repair did not restore generation");
        }
    }

    // --- save and load -----------------------------------------------------------
    const Coord siloSite=site(2,2);
    auto* silo=place(Structure_Silo,siloSite);
    silo->foundationDegrades=true;
    silo->powerDamageTimer=123;
    silo->foundationDecayTimer=4567;
    OMemoryStream out;out.open();silo->save(out);
    IMemoryStream in(out.getData(),out.getDataLength());
    const Uint32 savedVersion=game.loadedSavegameVersion;
    game.loadedSavegameVersion=SAVEGAMEVERSION;     // a load of what was just written
    // Give the diagnostic copy its own manager ID so normal destruction is safe.
    auto restored=std::make_unique<Silo>(in);
    restored->objectID=game.getObjectManager().addObject(restored.get());
    require(restored->foundationDegrades,"Saved foundation flag was lost");
    require(restored->powerDamageTimer==123,"Saved power timer was lost");
    require(restored->foundationDecayTimer==4567,"Saved foundation decay timer was lost");
    silo->setHealth(FixPoint(silo->getMaxHealth()));
    restored->setHealth(silo->getHealth());
    setPower(house,0,100);
    run(silo,123);run(restored.get(),123);
    require(silo->getHealth()==restored->getHealth()
            && silo->getHealth()==FixPoint(silo->getMaxHealth()-1)
            && silo->powerDamageTimer==restored->powerDamageTimer
            && silo->foundationDecayTimer==restored->foundationDecayTimer,
            "Save/load continuation changed degradation timing or damage");
    restored.reset();

    // A pre-9847 save carries one degrade timer: it keeps the power timer and
    // never starts foundation decay for a building it cannot identify.
    const int legacyTimer=powerInterval/3;
    OMemoryStream legacy;legacy.open();
    silo->ObjectBase::save(legacy);
    legacy.writeBool(false);                // repairing
    legacy.writeBool(false);                // fogged
    legacy.writeUint32(2);                  // lastVisibleFrame
    legacy.writeSint32(legacyTimer);        // the single pre-9847 degrade timer
    legacy.writeUint32(0);                  // smoke
    legacy.writeUint8(0);                   // city occupancy
    game.loadedSavegameVersion=9846;
    IMemoryStream legacyIn(legacy.getData(),legacy.getDataLength());
    auto migrated=std::make_unique<Silo>(legacyIn);
    migrated->objectID=game.getObjectManager().addObject(migrated.get());
    require(!migrated->foundationDegrades,"A legacy building started decaying from a foundation");
    require(migrated->powerDamageTimer==legacyTimer,"The legacy power timer was not preserved");
    migrated.reset();
    game.loadedSavegameVersion=savedVersion;

    // Real city scan: every city-only building loses local land value from
    // damage, including services with no health-scaled production mechanic.
    if (auto* sim=game.getCitySimulation()) {
        for (Uint32 item : {Structure_ZoneResidential,Structure_ZoneCommercial,
                Structure_ZoneIndustrial,Structure_PoliceStation,Structure_NuclearPlant,
                Structure_Stadium,Structure_Airport,Structure_Wall}) {
            const Coord size=getStructureSize(item), at=site(size.x,size.y);
            auto* building=place(item,at);
            const auto crime=sim->crimeRateMap_;
            auto valueAtCondition=[&](FixPoint health) {
                building->setHealth(health);
                sim->crimeRateMap_=crime; // isolate condition from next-scan crime feedback
                sim->runEffectsScans();
                return sim->getLandValueMap().worldGet(at.x,at.y);
            };
            const int full=valueAtCondition(building->getMaxHealth());
            require(full>1,"City condition fixture needs developed land");
            const int damaged=valueAtCondition(FixPoint(building->getMaxHealth())/2);
            require(damaged==std::max(1,full/2),"Half-health city building did not halve its local land value");
            require(valueAtCondition(building->getMaxHealth())==full,"Repair did not restore city land value");
        }
    }

    mutableOptions.concreteRequired=concreteRequired;

    // The removed degrade-on-concrete option still occupies its settings byte,
    // so the rest of the layout must survive a round trip untouched.
    game.loadedSavegameVersion=SAVEGAMEVERSION;
    OMemoryStream settingsOut;settingsOut.open();init.save(settingsOut);
    IMemoryStream settingsIn(settingsOut.getData(),settingsOut.getDataLength());
    const GameInitSettings reloaded(settingsIn);
    auto serializedOptions=init.getGameOptions();
    // City Effects is a mod-derived option, absent from the existing settings
    // stream. Compare the fields that this stream actually serializes.
    serializedOptions.cityEffects=SettingsClass::GameOptionsClass().cityEffects;
    require(reloaded.getGameOptions()==serializedOptions,"Game options did not survive the reserved option byte");
    require(reloaded.getMission()==init.getMission() && reloaded.getGameType()==init.getGameType(),
            "The reserved option byte shifted the settings layout");
    game.loadedSavegameVersion=savedVersion;

    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=degradation outcome=passed mod=%s concrete_required=%d power_interval=%d decay_interval=%d",
            ModManager::instance().getActiveModName().c_str(),concreteRequired ? 1 : 0,powerInterval,decayInterval);
}
