// Real engine fixture; only compiled by the diagnostic driver.
//
// Exercises the credit storage limit on the live House object: refined spice
// and city taxes share the refinery/silo capacity, starting cash alone is
// exempt, and everything that does not fit is destroyed at once instead of
// leaking away over later cycles. Every movement below goes through the
// shipped entry points (House::addCredits, addCityCredits, takeCredits,
// returnCredits, incrementStructures/decrementStructures, update) and through
// real structure placement and destruction for the capacity changes.
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <structures/StructureBase.h>
#include <Map.h>
#include <Tile.h>

#include <vector>

void runCreditStorageProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("CREDIT_STORAGE_FAIL: %s",why);throw std::runtime_error(why);}};
    House* house = pLocalHouse ? pLocalHouse : game.getHouse(HOUSE_HARKONNEN);
    require(house!=nullptr,"Missing measured house");

    // Keep the registered player objects alive but out of House::update, so no
    // AI decision can spend or earn credits between the assertions.
    auto savedPlayers=std::move(house->players);
    struct RestorePlayers{House* house; decltype(savedPlayers)& players;
        ~RestorePlayers(){house->players=std::move(players);}} restore{house,savedPlayers};
    const auto savedWinFlags=game.winFlags;
    const int savedQuota=house->quota;

    // Fixture state: the pools and the capacity the scenario starts from. The
    // power charge is pushed out of reach so an update() only enforces storage.
    auto fixture=[&](int capacity,FixPoint stored,FixPoint city,FixPoint starting){
        house->capacity=capacity;
        house->storedCredits=stored; house->cityCredits=city; house->startingCredits=starting;
        house->powerUsageTimer=MILLI2CYCLES(15*1000);
        house->oldCredits=house->getCredits();
    };

    // --- 1. No storage at all: starting cash survives, earned income does not.
    {
        House fresh(HOUSE_MERCENARY, 3000, 0, 0, 0);
        require(fresh.getCapacity()==0,"A new house starts with storage it did not build");
        require(fresh.getStartingCredits()==3000 && fresh.getCredits()==3000,
                "New starting cash did not survive house creation without storage");
        fresh.enforceCreditCapacity();
        require(fresh.getStartingCredits()==3000 && fresh.getEarnedCredits()==0,
                "The storage limit consumed the exempt starting cash of a new house");
    }

    fixture(0,0,0,2000);
    house->update();
    require(house->getStartingCredits()==2000 && house->getCredits()==2000,
            "An update without storage spent the exempt starting cash");
    const FixPoint grossBeforeZero=house->harvestedSpice;
    house->addCredits(500,true);
    require(house->getStoredCredits()==0,"Refined spice was stored without any storage");
    require(house->harvestedSpice-grossBeforeZero==500,
            "Gross harvested spice dropped the part the storage could not hold");
    house->addCityCredits(500);
    require(house->getCityCredits()==0,"City tax was stored without any storage");
    require(house->getStartingCredits()==2000,"Unstorable income was charged to the starting cash");

    // --- 2. Spice and city taxes share one store, in either order. ---
    fixture(1000,0,0,0);
    house->addCredits(600,true);
    require(house->getStoredCredits()==600,"Refined spice below the capacity was not stored");
    house->addCityCredits(600);
    require(house->getCityCredits()==400 && house->getEarnedCredits()==1000,
            "City tax ignored the spice the same store already held");

    fixture(1000,0,0,0);
    const FixPoint grossBeforeCityFirst=house->harvestedSpice;
    house->addCityCredits(600);
    house->addCredits(600,true);
    require(house->getCityCredits()==600 && house->getStoredCredits()==400,
            "Refined spice ignored the city tax the same store already held");
    require(house->getEarnedCredits()==1000,"The shared store passed its capacity");
    require(house->harvestedSpice-grossBeforeCityFirst==600,
            "Gross harvested spice dropped the part the city tax displaced");

    // --- 3. Fractional boundary: an exact fill fits, a fraction more does not.
    fixture(100,FixPoint(199)/2,0,0);
    house->addCredits(FixPoint(1)/2,true);
    require(house->getStoredCredits()==FixPoint(100),"An exact fractional fill was refused or overshot");
    house->addCredits(FixPoint(1)/4,true);
    require(house->getStoredCredits()==FixPoint(100),"A full store accepted a fraction more");
    fixture(100,FixPoint(199)/2,0,0);
    house->addCredits(1,true);
    require(house->getStoredCredits()==FixPoint(100),"A fractional overflow was stored instead of lost");

    // --- 4. Refunds are earned income, even when the purchase used starting cash.
    fixture(500,0,0,1000);
    require(house->takeCredits(800)==800,"Starting cash did not pay for the purchase");
    require(house->getStartingCredits()==200,"Spending did not come out of the starting cash");
    house->storedCredits=500;   // the store filled up while the order was queued
    house->returnCredits(300);
    require(house->getStartingCredits()==200,
            "A refund refilled the exempt starting pool and re-created the exemption");
    require(house->getStoredCredits()==500 && house->getEarnedCredits()==500,
            "A refund was stored past the capacity");
    require(house->getCredits()==700,"Refunded credits reappeared outside both pools");

    fixture(500,100,0,1000);
    require(house->takeCredits(800)==800,"Mixed pools did not pay for the purchase");
    require(house->getStoredCredits()==0 && house->getStartingCredits()==300,
            "Spending did not consume the earned credits first");
    house->returnCredits(300);
    require(house->getStoredCredits()==300 && house->getStartingCredits()==300,
            "A refund of a partly starting-cash purchase did not land in the capped pool");

    // --- 5. Real storage structures: losing one clamps at once, adding one
    //        admits further income. ---
    Coord plot=Coord::Invalid();
    for(int y=2;y+6<currentGameMap->getSizeY() && plot.isInvalid();++y)
        for(int x=2;x+6<currentGameMap->getSizeX() && plot.isInvalid();++x) {
            bool clear=true;
            for(int dy=-1;dy<=5 && clear;++dy)
                for(int dx=-1;dx<=5 && clear;++dx) {
                    const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                    if(!tile || tile->hasAnObject() || tile->hasCityZone() || tile->isRoad()) clear=false;
                }
            if(clear) plot=Coord(x,y);
        }
    require(plot.isValid(),"No clear plot for the storage fixture on this map");
    for(int dy=0;dy<5;++dy)
        for(int dx=0;dx<5;++dx) {
            auto* tile=currentGameMap->getTile(plot.x+dx,plot.y+dy);
            tile->setType(Terrain_Rock); tile->setRoad(false); tile->setOwner(house->getHouseID());
        }

    const int baseCapacity=house->capacity;
    auto* firstSilo=house->placeStructure(NONE_ID,Structure_Silo,plot.x,plot.y,false,true);
    require(firstSilo!=nullptr,"The storage fixture could not place a silo");
    const int oneSiloCapacity=house->capacity;
    const int siloCapacity=oneSiloCapacity-baseCapacity;
    require(siloCapacity>0,"A placed silo added no storage");

    fixture(oneSiloCapacity,FixPoint(oneSiloCapacity),0,0);
    house->addCredits(100,true);
    require(house->getStoredCredits()==FixPoint(oneSiloCapacity),"A full store grew past its silo capacity");

    auto* secondSilo=house->placeStructure(NONE_ID,Structure_Silo,plot.x+3,plot.y,false,true);
    require(secondSilo!=nullptr,"The storage fixture could not place a second silo");
    require(house->capacity==oneSiloCapacity+siloCapacity,"A second silo did not add storage");
    house->addCredits(FixPoint(siloCapacity),true);
    require(house->getStoredCredits()==FixPoint(house->capacity),
            "New storage did not admit the income the full store had refused");

    const Uint32 cycleBeforeLoss=game.getGameCycleCount();
    secondSilo->destroy();
    require(game.getGameCycleCount()==cycleBeforeLoss,"The fixture advanced the game while destroying a silo");
    require(house->capacity==oneSiloCapacity,"Destroying a silo did not remove its storage");
    require(house->getEarnedCredits()==FixPoint(oneSiloCapacity),
            "Credits above the lost storage survived the loss instead of being destroyed at once");
    firstSilo->destroy();
    require(house->capacity==baseCapacity,"Destroying the last silo did not remove its storage");
    require(house->getEarnedCredits()==FixPoint(baseCapacity),
            "Credits above the remaining storage were left to leak away later");

    // --- 5b. A DuneCity Construction Yard stores 1000 credits (so city tax is
    //         banked before the first Refinery); outside city mods it stores none. ---
    const int yardStorage=game.isCitySimEnabled() ? 1000 : 0;
    require(game.objectData.data[Structure_ConstructionYard][house->getHouseID()].capacity==yardStorage,
            "Construction Yard storage is not 1000 in a city match and 0 otherwise");
    auto* storageYard=house->placeStructure(NONE_ID,Structure_ConstructionYard,plot.x,plot.y,false,true);
    require(storageYard!=nullptr,"The storage fixture could not place a Construction Yard");
    require(house->capacity==baseCapacity+yardStorage,"A placed Construction Yard did not add its storage");
    storageYard->destroy();
    require(house->capacity==baseCapacity,"Destroying the Construction Yard did not remove its storage");

    // --- 6. Loading: a saved balance is not clamped until the world is back. ---
    // The saved balance is far above anything the reloaded silo can hold, the
    // way a save written before the limit covered city income looks on resume.
    fixture(0,0,0,0);
    const auto savedState=game.gameState;
    game.gameState=GameState::Loading;
    house->storedCredits=50000; house->cityCredits=15000;
    house->enforceCreditCapacity();
    require(house->getEarnedCredits()==65000,
            "A saved balance was clamped before the storage structures were loaded");
    house->incrementStructures(Structure_Silo);
    require(house->getEarnedCredits()==65000,
            "Registering storage during loading destroyed the saved cash");
    const int loadedCapacity=house->capacity;
    require(loadedCapacity>0 && loadedCapacity<65000,"The loading fixture did not register a smaller storage");
    game.gameState=savedState;
    house->update();
    require(house->getEarnedCredits()==FixPoint(loadedCapacity),
            "The first update after loading did not clamp the whole overfull balance at once");
    require(house->getStoredCredits()>=0 && house->getCityCredits()>=0,
            "Clamping an overfull balance drove a pool negative");
    house->decrementStructures(Structure_Silo,plot,false);
    require(house->capacity==0,"The loading fixture did not release its registered storage");

    // --- 7. Save, load and resume keep the pools exactly as they were. ---
    fixture(1500,900,400,250);
    OMemoryStream out; out.open();
    house->save(out);   // players were moved aside above, so none are serialized
    const auto savedVersion=game.loadedSavegameVersion;
    game.loadedSavegameVersion=SAVEGAMEVERSION;
    IMemoryStream input(out.getData(),out.getDataLength());
    {
        House resumed(input);
        game.loadedSavegameVersion=savedVersion;
        require(resumed.getStoredCredits()==900 && resumed.getCityCredits()==400
                && resumed.getStartingCredits()==250,"Save and load changed the credit pools");
        require(resumed.getCredits()==house->getCredits(),"The resumed house holds a different total");
        resumed.capacity=1500;
        resumed.enforceCreditCapacity();
        require(resumed.getStoredCredits()==900 && resumed.getCityCredits()==400
                && resumed.getStartingCredits()==250,
                "Resuming clamped a balance that fits the restored storage");
    }
    game.loadedSavegameVersion=savedVersion;

    // --- 8. Gross statistics are what was collected, not what was kept. ---
    fixture(0,0,0,0);
    const FixPoint receiptsBefore=house->cityTaxReceipts;
    const FixPoint grossBefore=house->harvestedSpice;
    house->addCityTaxReceipts(750);
    house->addCityCredits(750);
    house->addCredits(750,true);
    require(house->cityTaxReceipts-receiptsBefore==750,
            "The gross tax statistic lost the income the storage could not hold");
    require(house->harvestedSpice-grossBefore==750,
            "The gross harvested statistic lost the spice the storage could not hold");
    require(house->getEarnedCredits()==0,"Storage-less income was spendable after all");

    // --- 9. The game credit ceiling still applies, starting cash included. ---
    fixture(House::MAX_GAME_CREDITS*2,0,0,FixPoint(999000));
    house->addCredits(5000,true);
    require(house->getStoredCredits()==999,"Earned income passed the game credit ceiling");
    require(house->getCredits()==House::MAX_GAME_CREDITS,"The house holds more than the ceiling allows");
    fixture(House::MAX_GAME_CREDITS*2,0,0,FixPoint(House::MAX_GAME_CREDITS));
    house->addCredits(1000,true);
    require(house->getStoredCredits()==0,"Income was stored at the ceiling");
    require(house->getCredits()==House::MAX_GAME_CREDITS,"The ceiling was passed by starting cash plus income");

    {
        House richStart(HOUSE_MERCENARY, 2000000, 0, 0, 0);
        require(richStart.getCredits()==House::MAX_GAME_CREDITS,"Starting cash exceeded the absolute ceiling");
        fixture(2000000,800000,800000,800000);
        OMemoryStream oversized;oversized.open();house->save(oversized);
        IMemoryStream legacy(oversized.getData(),oversized.getDataLength());
        const auto previousVersion=game.loadedSavegameVersion;
        const auto previousState=game.gameState;
        game.loadedSavegameVersion=SAVEGAMEVERSION;game.gameState=GameState::Loading;
        House restored(legacy);
        require(restored.getCredits()==House::MAX_GAME_CREDITS,"Loaded balances exceeded the absolute ceiling");
        require(restored.getStartingCredits()==800000 && restored.getStoredCredits()==199999 && restored.getCityCredits()==0,
                "Loaded balances were only visually capped instead of reducing the actual pools");
        fixture(2000000,500,500,2000000);
        house->enforceCreditCapacity();
        require(house->getCredits()==House::MAX_GAME_CREDITS && house->getEarnedCredits()==0,
                "The absolute cap was skipped during storage reconstruction");
        game.loadedSavegameVersion=previousVersion;game.gameState=previousState;
    }

    // --- 10. The quota counts the spice the house kept, not what overflowed. --
    game.winFlags|=WINLOSEFLAGS_QUOTA;
    house->quota=900;
    fixture(500,0,0,0);
    game.finished=false; game.won=false;
    house->addCredits(5000,true);
    require(house->getStoredCredits()==500,"The quota delivery was stored past the capacity");
    require(!game.finished,"Spice that overflowed the storage counted towards the quota");
    house->capacity=1000;
    house->addCredits(500,true);
    require(house->getStoredCredits()==1000,"Retained spice was not stored after the capacity grew");
    require(game.finished && game.won,"Retained spice at the quota did not win the mission");
    game.finished=false; game.won=false;
    house->quota=savedQuota; game.winFlags=savedWinFlags;

    // --- 11. A spice quota is met by the house's whole earned store: refined spice and city tax
    //         credits alike (Stefan, 1.0.848: "anything with a spice quota can be achieved with tax
    //         credits also"). Run in a campaign and in a custom game, for every house in the game
    //         as the local house (the engine checks the local house's quota). Without the city
    //         simulation there are no taxes, and only refined spice counts. Starting cash never does.
    {
        const bool city=game.isCitySimEnabled();
        House* const savedLocal=pLocalHouse;
        int checked=0;
        for(int h=0;h<NUM_HOUSES;++h) {
            House* quotaHouse=game.getHouse(h);
            if(!quotaHouse) continue;
            const int savedHouseQuota=quotaHouse->quota, savedCapacity=quotaHouse->capacity;
            const FixPoint savedStored=quotaHouse->storedCredits, savedCity=quotaHouse->cityCredits,
                savedStarting=quotaHouse->startingCredits;
            pLocalHouse=quotaHouse;
            game.winFlags|=WINLOSEFLAGS_QUOTA;
            quotaHouse->quota=1000; quotaHouse->capacity=5000;
            quotaHouse->storedCredits=0; quotaHouse->cityCredits=0; quotaHouse->startingCredits=5000;
            game.finished=false; game.won=false;
            quotaHouse->addCityCredits(999);
            require(!game.finished,"Starting cash or 999 tax credits met a 1000-credit quota");
            quotaHouse->addCityCredits(1);
            if(city) require(game.finished && game.won,"City tax credits alone did not meet the spice quota");
            else require(!game.finished,"Tax credits met a spice quota without the city simulation");

            quotaHouse->storedCredits=0; quotaHouse->cityCredits=0;
            game.finished=false; game.won=false;
            quotaHouse->addCredits(600,true);
            require(!game.finished,"600 refined spice met a 1000-credit quota");
            quotaHouse->addCityCredits(400);
            require(game.finished==city && game.won==city,
                city ? "Refined spice and tax credits together did not meet the spice quota"
                     : "Tax credits counted towards a spice quota without the city simulation");
            quotaHouse->addCredits(400,true);
            require(game.finished && game.won,"1000 refined spice did not meet the spice quota");

            quotaHouse->quota=savedHouseQuota; quotaHouse->capacity=savedCapacity;
            quotaHouse->storedCredits=savedStored; quotaHouse->cityCredits=savedCity;
            quotaHouse->startingCredits=savedStarting;
            game.finished=false; game.won=false;
            ++checked;
        }
        pLocalHouse=savedLocal; game.winFlags=savedWinFlags;
        require(checked>=2,"Fewer than two houses to check the spice quota with");
        SDL_Log("CREDIT_STORAGE_QUOTA: game_type=%d city=%d houses=%d",int(game.gameType),int(city),checked);
    }

    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=credit-storage outcome=passed");
    SDL_Log("CREDIT_STORAGE_PROBE_PASS: exempt starting cash, shared spice/tax storage in both orders, "
            "fractional boundary, refunds into the capped pool, immediate loss on silo destruction, "
            "capacity growth admitting income, loading skip with clamp on the first update, "
            "save/load/resume pools, gross statistics, credit ceiling, retained-only quota and a spice "
            "quota met by spice and tax credits for every house");
}
