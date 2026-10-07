#include <structures/PoliceStation.h>
#include <GUI/ObjectInterfaces/PoliceStationInterface.h>
#include <units/UnitBase.h>
#include <map>
#include <utility>
// Police reinforcement admission, driven through a real PoliceStation on a real House.
//
// The station used to carry its own flat ceiling of 250 raw military units and reported
// reaching it to the player as "Unit limit reached". That count treated every
// individual trooper as a whole unit, while the engine counts infantry in thirds, so the
// police ceiling bound far below the limit the lobby selected - and bound at all even
// when the selection was "unlimited". The same predicate hid the sidebar button, stopped
// the human reinforcement command and stopped automatic AI patrols.
//
// House::getMaxUnits() and the per-category House::isUnitLimitReached() are now the only
// authority for both the status and the admission. This fixture pins the boundary: one
// slot below the selected cap deploys a full patrol, the cap itself blocks it, zero means
// unlimited, a disabled override keeps the map/ObjectData default, and the local garrison
// mechanics - cooldown, catalogue availability and deployment space - stay independent
// and are never reported as the game's unit limit.
void runPoliceReinforcementProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("POLICE_REINFORCEMENT_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    require(house!=nullptr && game.isCitySimEnabled(),
        "Police reinforcement fixture needs a city-mode house");
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot!=nullptr,"Police reinforcement fixture needs the shared human/AI house");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;

    // A clear rock pocket: the station's own ring search must find real deployment space,
    // so admission is the only thing that can refuse a patrol.
    const int span=16;
    auto pocketFrom=[&](int firstRow) {
        Coord found=Coord::Invalid();
        for(int y=firstRow;y+span<currentGameMap->getSizeY() && found.isInvalid();++y)
            for(int x=4;x+span<currentGameMap->getSizeX() && found.isInvalid();++x) {
                bool clear=true;
                for(int dy=0;dy<span;++dy)for(int dx=0;dx<span;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
                if(clear)found=Coord(x,y);
            }
        require(found.isValid(),"Police reinforcement fixture needs empty settlement space");
        for(int y=found.y;y<found.y+span;++y)for(int x=found.x;x<found.x+span;++x)
            currentGameMap->getTile(x,y)->setType(Terrain_Rock);
        return found;
    };
    auto placeStation=[&](Coord pocket) {
        auto* built=dynamic_cast<PoliceStation*>(house->placeStructure(NONE_ID,
            Structure_PoliceStation,pocket.x+7,pocket.y+7,true,true));
        require(built!=nullptr && built->getHealth()>0,
            "Police reinforcement fixture needs a station");
        return built;
    };
    const Coord origin=pocketFrom(currentGameMap->getSizeY()/3);
    auto* station=placeStation(origin);

    // Exercise the actual sidebar widget as well as execution admission.
    std::unique_ptr<PoliceStationInterface> panel(PoliceStationInterface::create(station->getObjectID()));
    auto showsUnitLimit=[&] {
        require(panel->update(), "Police sidebar update failed");
        return panel->reinforcementStatusLabel.isVisible()
            && panel->reinforcementStatusLabel.getText()=="Unit limit\nreached";
    };

    const int houseID=station->originalHouseID;
    auto& catalogue=game.objectData.data;
    const bool savedTrike=catalogue[Unit_Trike][houseID].enabled;
    const bool savedTrooper=catalogue[Unit_Trooper][houseID].enabled;
    catalogue[Unit_Trike][houseID].enabled=true;
    catalogue[Unit_Trooper][houseID].enabled=true;
    const auto savedOverride=game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride;
    const int savedMaxUnits=house->maxUnits;
    const auto savedDifficulty=bot->difficulty;
    const int savedValueLimit=bot->militaryValueLimit;
    bot->difficulty=QuantBot::Difficulty::Easy;   // no AI budget gate while the limit is measured
    // Injected garrison counters, per item, removed again at the end. Real units spawned
    // by the fixture are left alone, so the house keeps a consistent count either way.
    std::map<Uint32,int> synthetic;
    auto inject=[&](Uint32 item,int count) {
        house->numItem[item]+=count;house->numUnits+=count;synthetic[item]+=count;
    };

    auto setLimit=[&](int override_,int maxUnits) {
        game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride=override_;
        house->maxUnits=maxUnits;
    };
    // The engine's own ground-equivalent occupancy, used only to choose cap values. Every
    // assertion below goes to the real House and PoliceStation accessors.
    auto groundEquivalents=[&] {
        return house->numUnits-house->numItem[Unit_Soldier]-house->numItem[Unit_Trooper]
            -house->numItem[Unit_Carryall]-house->numItem[Unit_ChemicalCarryall]
            -house->numItem[Unit_Ornithopter]
            +(house->numItem[Unit_Soldier]+2)/3+(house->numItem[Unit_Trooper]+2)/3;
    };
    // Ground rounds infantry thirds up and infantry rounds them down, so whole thirds make
    // one cap value the exact boundary for both categories the patrol uses.
    auto padInfantryToThirds=[&] {
        for(const Uint32 item:{Uint32(Unit_Soldier),Uint32(Unit_Trooper)})
            inject(item,(3-house->numItem[item]%3)%3);
    };
    // Clearing the garrison cooldown first keeps every admission assertion below about
    // the limit under test rather than about the timer.
    auto ready=[&] {station->spawnTimer=0;};
    auto patrol=[&](bool readyCooldown) {
        if(readyCooldown)ready();
        const int trikes=house->getNumItems(Unit_Trike),troopers=house->getNumItems(Unit_Trooper);
        station->doSpawnVehicles();
        return std::pair<int,int>(house->getNumItems(Unit_Trike)-trikes,
                                  house->getNumItems(Unit_Trooper)-troopers);
    };

    // 1. The shipped default: no override selected, so the map/ObjectData ceiling stands
    //    and the station agrees with the engine about it.
    setLimit(-1,savedMaxUnits);
    require(house->getMaxUnits()==savedMaxUnits,
        "Disabled override lost the map/ObjectData unit ceiling");
    require(station->isUnitLimitReached()
            ==(house->isUnitLimitReached(Unit_Trike) && house->isUnitLimitReached(Unit_Trooper)),
        "Police status diverged from the engine limit with the override disabled");
    require(!station->isUnitLimitReached(),"Default fixture starts at the engine limit");
    const auto defaultPatrol=patrol(true);
    require(defaultPatrol.first==1 && defaultPatrol.second==3,
        "Default settings did not deploy one trike and three troopers");

    // 2. The local garrison cooldown is independent of any limit: a patrol that deployed
    //    starts it, and nothing deploys again until it expires.
    require(station->spawnTimer==station->getMaxSpawnTimer(),
        "A deployed patrol did not start its own cooldown");
    require(!station->canSpawnVehicles(),"The garrison cooldown stopped withholding patrols");
    require(!station->isUnitLimitReached(),"A running cooldown was reported as a unit limit");
    const auto onCooldown=patrol(false);
    require(onCooldown.first==0 && onCooldown.second==0,"A patrol deployed during its cooldown");

    // 3. A garrison far above the removed police-local ceiling of 250 raw military units,
    //    but only a third of that in engine ground-equivalents.
    inject(Unit_Trooper,300);
    padInfantryToThirds();
    require(house->getNumItems(Unit_Trooper)>=250,
        "Fixture garrison is not above the removed police-local ceiling");

    // 4. Below the selected cap the patrol is admitted and the sidebar must not claim a
    //    unit limit. The removed 250-unit ceiling failed exactly here.
    const int roomy=groundEquivalents()+40;
    setLimit(roomy,roomy);
    require(house->getMaxUnits()==roomy,"A positive override did not become the effective limit");
    require(!house->isUnitLimitReached(Unit_Trike) && !house->isUnitLimitReached(Unit_Trooper),
        "Below-cap fixture is already at the engine limit");
    require(!station->isUnitLimitReached(),
        "Police reported the game's unit limit while below the selected cap");
    ready();
    require(station->canSpawnVehicles(),
        "Police refused reinforcements while below the selected cap");
    require(!showsUnitLimit() && panel->spawnSelectButton.isVisible(),
        "Police sidebar falsely hid a ready below-cap patrol");
    const auto below=patrol(true);
    require(below.first==1 && below.second==3,
        "Below-cap patrol did not deploy one trike and three troopers");

    // 5. The exact boundary. One free slot still admits the patrol; the cap itself blocks
    //    it, reports it as the unit limit and deploys nothing.
    padInfantryToThirds();
    const int occupied=groundEquivalents();
    setLimit(occupied+1,occupied+1);
    require(!house->isUnitLimitReached(Unit_Trike) && !house->isUnitLimitReached(Unit_Trooper),
        "One slot below the cap already reported the engine limit");
    ready();
    require(!station->isUnitLimitReached() && station->canSpawnVehicles(),
        "One slot below the selected cap still refused reinforcements");
    setLimit(occupied,occupied);
    require(house->getMaxUnits()==occupied,"Exact-cap case lost the selected limit");
    require(house->isUnitLimitReached(Unit_Trike) && house->isUnitLimitReached(Unit_Trooper),
        "Exact-cap fixture did not reach the engine limit");
    require(station->isUnitLimitReached(),
        "Police did not report the selected unit limit at the cap");
    ready();
    require(!station->canSpawnVehicles(),"Police offered reinforcements at the selected limit");
    require(showsUnitLimit() && !panel->spawnSelectButton.isVisible(),
        "Police sidebar did not show the actual unit limit");
    const auto atCap=patrol(true);
    require(atCap.first==0 && atCap.second==0,"A patrol deployed at the selected unit limit");
    require(station->spawnTimer==0,"A blocked patrol consumed the garrison cooldown");

    // A single remaining vehicle slot deploys only the trike, without overshoot.
    setLimit(occupied+1,occupied+1);
    const auto oneSlot=patrol(true);
    require(oneSlot.first==1 && oneSlot.second==0,
        "One remaining ground slot did not deploy exactly one trike");
    require(station->isUnitLimitReached(), "Partial patrol overshot or missed the filled cap");

    // Ground rounds infantry up; infantry admission rounds it down. With one
    // trooper in the next third, only the other two troopers may deploy.
    inject(Unit_Trooper,1);
    setLimit(occupied+2,occupied+2);
    require(house->isUnitLimitReached(Unit_Trike) && !house->isUnitLimitReached(Unit_Trooper),
        "Partial-infantry fixture did not leave only infantry capacity");
    ready();
    require(!showsUnitLimit() && panel->spawnSelectButton.isVisible(),
        "Police sidebar blocked an available partial infantry patrol");
    const auto infantrySlots=patrol(true);
    require(infantrySlots.first==0 && infantrySlots.second==2,
        "Partial patrol did not fill exactly the two remaining infantry slots");
    require(station->isUnitLimitReached(), "Partial infantry patrol did not stop at the cap");

    // 6. Zero means unlimited. This is the reported defect: a long game past 250 military
    //    units with the unlimited setting selected still has police reinforcements.
    setLimit(0,0);
    require(house->getMaxUnits()==0,"Zero override stopped meaning unlimited");
    require(!house->isUnitLimitReached(Unit_Trike) && !house->isUnitLimitReached(Unit_Trooper),
        "Zero override still reported a category limit");
    require(!station->isUnitLimitReached(),
        "Police reported a unit limit under the unlimited setting");
    ready();
    require(station->canSpawnVehicles(),
        "Police refused reinforcements under the unlimited setting");
    require(!showsUnitLimit() && panel->spawnSelectButton.isVisible(),
        "Police sidebar blocked an unlimited patrol");
    const auto unlimited=patrol(true);
    require(unlimited.first==1 && unlimited.second==3,
        "The unlimited setting did not deploy a full patrol");

    // 7. A QuantBot controller's configured military value budget is a local AI spending
    //    decision. It still withholds the patrol, and it is never the game's unit limit.
    bot->difficulty=QuantBot::Difficulty::Brutal;
    bot->militaryValueLimit=0;
    require(station->isReinforcementBudgetReached(),
        "An exhausted AI military value budget stopped withholding reinforcements");
    require(!station->isUnitLimitReached(),
        "An AI military value budget was reported as the game's unit limit");
    ready();
    require(!station->canSpawnVehicles(),"An exhausted AI budget still offered a patrol");
    require(!showsUnitLimit() && panel->reinforcementStatusLabel.isVisible()
            && panel->reinforcementStatusLabel.getText()=="Army target\nreached"
            && !panel->spawnSelectButton.isVisible(),
        "Police sidebar did not distinguish the AI army target from unit capacity");
    const auto budgetBlocked=patrol(true);
    require(budgetBlocked.first==0 && budgetBlocked.second==0,
        "A patrol deployed past the exhausted AI military value budget");
    bot->difficulty=QuantBot::Difficulty::Easy;bot->militaryValueLimit=savedValueLimit;

    // 8. Catalogue availability is its own restriction, and deploying nothing leaves the
    //    ability ready instead of burning the cooldown.
    catalogue[Unit_Trike][houseID].enabled=false;
    catalogue[Unit_Trooper][houseID].enabled=false;
    require(!station->isUnitLimitReached(),"A disabled catalogue entry was reported as a unit limit");
    const auto disabled=patrol(true);
    require(disabled.first==0 && disabled.second==0,"A disabled catalogue unit was still deployed");
    require(station->spawnTimer==0,"A patrol that deployed nothing consumed its cooldown");
    catalogue[Unit_Trike][houseID].enabled=true;
    catalogue[Unit_Trooper][houseID].enabled=true;

    // 9. Deployment space is also its own restriction, on its own station in its own
    //    pocket so the earlier patrols cannot leave a half-full infantry tile behind.
    //    Walling in every ring the station searches keeps the ability ready, still
    //    reports no unit limit and leaves the cooldown untouched.
    auto* boxed=placeStation(pocketFrom(origin.y+span));
    const Coord size=boxed->getStructureSize(),at=boxed->getLocation();
    int reachable=0,walled=0;
    for(int y=at.y-3;y<at.y+size.y+3;++y)for(int x=at.x-3;x<at.x+size.x+3;++x) {
        if(x>=at.x && x<at.x+size.x && y>=at.y && y<at.y+size.y)continue;
        ++reachable;
        if(!currentGameMap->getTile(x,y)->hasAGroundObject())
            house->placeStructure(NONE_ID,Structure_Wall,x,y,true,true);
        // Infantry can share a tile that holds only infantry, so the ring has to be
        // closed with structures for the deployment search to find nothing at all.
        if(currentGameMap->getTile(x,y)->hasANonInfantryGroundObject())++walled;
    }
    SDL_Log("POLICE_REINFORCEMENT_SPACE: station=%d,%d size=%d,%d searched=%d walled=%d",
        at.x,at.y,size.x,size.y,reachable,walled);
    require(walled==reachable,"Fixture could not wall in every ring the station searches");
    require(!boxed->isUnitLimitReached(),"Blocked deployment space was reported as a unit limit");
    boxed->spawnTimer=0;
    require(boxed->canSpawnVehicles(),"A boxed-in station stopped offering its ready patrol");
    const int trikesBefore=house->getNumItems(Unit_Trike);
    const int troopersBefore=house->getNumItems(Unit_Trooper);
    boxed->doSpawnVehicles();
    require(house->getNumItems(Unit_Trike)==trikesBefore
            && house->getNumItems(Unit_Trooper)==troopersBefore,
        "A patrol deployed into walled-in rings");
    require(boxed->spawnTimer==0,"A blocked deployment consumed the garrison cooldown");

    const int garrison=house->getNumItems(Unit_Trooper);
    for(const auto& entry:synthetic) {
        house->numItem[entry.first]-=entry.second;house->numUnits-=entry.second;
    }
    game.gameInitSettings.gameOptions.maximumNumberOfUnitsOverride=savedOverride;
    house->maxUnits=savedMaxUnits;
    bot->difficulty=savedDifficulty;bot->militaryValueLimit=savedValueLimit;
    catalogue[Unit_Trike][houseID].enabled=savedTrike;
    catalogue[Unit_Trooper][houseID].enabled=savedTrooper;

    SDL_Log("POLICE_REINFORCEMENT_PASS: selected limit is the only unit-limit authority"
        " (default=%d+%d below=%d+%d unlimited=%d+%d at_cap=%d+%d garrison=%d troopers)",
        defaultPatrol.first,defaultPatrol.second,below.first,below.second,
        unlimited.first,unlimited.second,atCap.first,atCap.second,garrison);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=police-reinforcement outcome=passed");
}
