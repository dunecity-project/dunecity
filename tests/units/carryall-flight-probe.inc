// Headless integration fixture; linked with the production game objects.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <structures/RepairYard.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cmath>

int runCarryallFlightProbe() {
    const std::string mod = std::getenv("CARRYALL_PROBE_MOD");
    const bool baseline = std::string(std::getenv("CARRYALL_PROBE_BASELINE")) == "1";
    auto require = [](bool ok, const char* why) {
        if(!ok) throw std::runtime_error(why);
    };
    require(ModManager::instance().setActiveMod(mod), "Missing flight fixture mod");
    settings.general.playerName = "Flight tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("flight.ini", mapData, "Carryall flight", true, settings.gameOptions);
    init.randomSeed = 67;
    require(init.getModName() == mod, "Flight fixture selected the wrong mod");
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Flight tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && owner->isAlive(), "Flight fixture house missing");
    Coord site = Coord::Invalid();
    for(int y=5; y<currentGameMap->getSizeY()-7 && site.isInvalid(); ++y)
        for(int x=22; x<currentGameMap->getSizeX()-7; ++x)
            if(!currentGameMap->getTile(x,y)->hasAGroundObject()
               && !currentGameMap->getTile(x,y+4)->hasAGroundObject()) { site = Coord(x,y); break; }
    require(site.isValid(), "No flight fixture space");
    std::ofstream csv(std::string(std::getenv("CARRYALL_PROBE_OUT"))+"/"+mod+".csv");
    csv << std::setprecision(9) << "kind,distance_tiles,heading_error_degrees,seconds,step_tiles,heading_degrees\n";
    auto aircraft = [&] {
        auto* carry = static_cast<Carryall*>(owner->createUnit(Unit_Carryall, true));
        require(carry != nullptr, "Cannot create carryall");
        carry->deploy(site - Coord(2,0));
        carry->angle = 0; carry->drawnAngle = 0;
        return carry;
    };
    // Calling the real turn method in place isolates angular rate from translation.
    for(int start : {2,4}) {
        auto* carry = aircraft();
        carry->setDestination(site); carry->angle = start; carry->drawnAngle = start;
        int ticks=0;
        while(FixPoint::abs(carry->angle - carry->getDestinationAngle()) > 0.0001_fix
              && ticks<1000) { carry->turn(); ++ticks; }
        require(ticks<1000, "Carryall failed to turn toward target");
        if(!baseline) require(ticks == (start == 2 ? 23 : 45),
                              "Carryall turn rate is not Dynasty's 253.125 degrees/s");
        csv << "rotation,0," << start*45 << ',' << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment();
    }
    for(double distance : {20.0,10.0,8.0,4.0,2.0,1.5,1.0,0.5,0.25,0.125}) {
        for(int heading : {0,1,2,4}) {
            auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank, true));
            require(tank != nullptr, "Cannot create pickup unit");
            tank->deploy(site); tank->setDestination(site+Coord(0,4));
            auto* carry = aircraft();
            carry->unassignFromMap(carry->location);
            const Coord old = carry->location;
            carry->realX = tank->getRealX() - FixPoint(static_cast<int>(distance*TILESIZE));
            carry->realY = tank->getRealY();
            carry->location = Coord(carry->realX.lround()/TILESIZE, carry->realY.lround()/TILESIZE);
            carry->assignToMap(carry->location);
            if(auto* grid = game->getSpatialGrid()) grid->move(*carry,carry->getGridHandle(),old,carry->location);
            carry->angle = heading; carry->drawnAngle = heading;
            carry->setDestination(site); carry->setTarget(tank);
            tank->bookCarrier(carry);
            const double x=carry->getRealX().toDouble(), y=carry->getRealY().toDouble();
            require(carry->update(), "Carryall vanished during approach");
            const double step=std::hypot(carry->getRealX().toDouble()-x,carry->getRealY().toDouble()-y)/TILESIZE;
            if(!baseline) {
                require(step <= 0.24001, "Pickup movement exceeded the cruise cap");
                if(distance == 20.0) {
                    const double expected = heading == 0 ? 0.24 : heading == 1 ? 0.20 : heading == 2 ? 0.18 : 0.12;
                    require(std::abs(step-expected) < 0.0001, "Missing Dynasty heading-dependent throttle");
                }
                if(distance == 1.5 && heading == 0)
                    require(std::abs(step-0.04) < 0.0001, "Pickup approach still jumps at short range");
                if(distance == 0.25)
                    require(std::abs(step-0.004) < 0.0001, "Final creep adds forward flight or ignores script pacing");
            }
            csv << "step," << distance << ',' << heading*45 << ",0.016," << step << ',' << carry->angle.toDouble()*45 << '\n';
            int ticks=1;
            while(!carry->hasCargo() && ticks<2500) { require(carry->update(), "Carryall vanished before pickup"); ++ticks; }
            require(carry->hasCargo(), "Carryall circled or stalled without picking up");
            if(!baseline && distance == 0.25)
                require(ticks*0.016 > 0.45 && ticks*0.016 < 0.8, "Final pickup rushes or stalls");
            csv << "pickup," << distance << ',' << heading*45 << ',' << ticks*0.016 << ",0,0\n";
            ticks=0;
            while(carry->hasCargo() && ticks<2500) { require(carry->update(), "Carryall vanished during delivery"); ++ticks; }
            require(!carry->hasCargo() && tank->isActive(), "Carryall failed to deliver cargo");
            csv << "delivery," << distance << ',' << heading*45 << ',' << ticks*0.016 << ",0,0\n";
            carry->cancelDeployment(); tank->cancelDeployment();
        }
    }
    // Follow an actual off-centre unit, then keep tracking it while it moves.
    {
        auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setDestination(site+Coord(0,4));
        auto* carry = aircraft();
        carry->setTarget(tank); tank->bookCarrier(carry);
        tank->realY += 12;
        int ticks=0;
        while(!carry->hasCargo() && ticks<2500) {
            if(ticks<100) tank->realY += 0.1_fix;
            require(carry->update(), "Moving-target carryall vanished"); ++ticks;
        }
        require(carry->hasCargo(), "Carryall could not pick up an off-centre moving unit");
        while(carry->hasCargo() && ticks<5000) { carry->update(); ++ticks; }
        require(!carry->hasCargo() && tank->isActive(), "Moving-target cargo not delivered");
        csv << "moving_target,2,0," << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment(); tank->cancelDeployment();
    }
    for(bool disappeared : {false,true}) {
        auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setDestination(site+Coord(0,4));
        auto* carry = aircraft();
        carry->setTarget(tank); tank->bookCarrier(carry);
        if(disappeared) tank->cancelDeployment();
        else tank->setAwaitingPickup(false);
        require(carry->update() && !carry->hasCargo() && !carry->target,
                "Cancelled or disappeared pickup left a stale target");
        carry->cancelDeployment();
        if(!disappeared) tank->cancelDeployment();
    }
    // Exercise both directions of a real repair-yard transfer, not just drops
    // on empty sand. Its multi-tile closest-point destination can change side.
    {
        RepairYard* yard=nullptr;
        const Coord size=getStructureSize(Structure_RepairYard);
        for(int y=14;y+size.y<currentGameMap->getSizeY()-1 && !yard;++y)
            for(int x=32;x+size.x<currentGameMap->getSizeX()-1 && !yard;++x) {
                bool clear=true;
                for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx)
                    clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
                if(clear) yard=static_cast<RepairYard*>(owner->placeStructure(NONE_ID,Structure_RepairYard,x,y,true,true));
            }
        require(yard != nullptr, "Cannot place repair-yard flight fixture");
        auto* tank=static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
        tank->deploy(site); tank->setHealth(tank->getMaxHealth()/3);
        auto* carry=aircraft();
        tank->doRepair();
        require(tank->getTarget()==yard, "Damaged tank did not request repair");
        carry->setTarget(tank); tank->bookCarrier(carry);
        int ticks=0;
        while(yard->getRepairUnit()!=tank && ticks<3000) { carry->update(); ++ticks; }
        require(yard->getRepairUnit()==tank && !carry->hasCargo(), "Carryall failed to dock at repair yard");
        csv << "repair_delivery,0,0," << ticks*0.016 << ",0,0\n";
        tank->setHealth(tank->getMaxHealth());
        carry->setTarget(yard);
        ticks=0;
        while(!carry->hasCargo() && ticks<1000) { carry->update(); ++ticks; }
        require(carry->hasCargo() && yard->isFree(), "Repaired cargo not collected from yard");
        while(carry->hasCargo() && ticks<4000) { carry->update(); ++ticks; }
        require(!carry->hasCargo() && tank->isActive(), "Repaired cargo not returned to ground");
        csv << "repair_return,0,0," << ticks*0.016 << ",0,0\n";
        carry->cancelDeployment(); tank->cancelDeployment();

        // A temporarily deviated unit is not repaired by the house holding it. Every repair path
        // is driven with real objects: the inherited trip, the automatic trip, an explicit repair
        // order, an ordered move onto the yard, the arrival gate and a carryall delivery. Ordinary
        // repairs return by themselves once the deviation ends.
        auto* captive=game->getHouse(HOUSE_HARKONNEN);
        require(captive != nullptr, "Deviation fixture is missing its second house");
        RepairYard* homeYard=nullptr;
        for(int y=1;y+size.y<currentGameMap->getSizeY()-1 && !homeYard;++y)
            for(int x=1;x+size.x<currentGameMap->getSizeX()-1 && !homeYard;++x) {
                // Keep the original owner's yard clear of the tile the fixture unit deploys on.
                bool clear=blockDistance(Coord(x,y),site).lround()>=6;
                for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx)
                    clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
                if(clear) homeYard=static_cast<RepairYard*>(captive->placeStructure(NONE_ID,Structure_RepairYard,x,y,true,true));
            }
        require(homeYard != nullptr, "Cannot place the deviation fixture's home repair yard");
        auto* borrowed=static_cast<GroundUnit*>(captive->createUnit(Unit_Tank,true));
        require(borrowed != nullptr, "Cannot create the deviation fixture unit");
        borrowed->deploy(site); borrowed->setHealth(borrowed->getMaxHealth()/3);
        // Both houses keep a carryall: the original owner's lift is what the inherited repair
        // trip books, and the holding house's lift makes automatic repair available to it.
        auto* homeLift=static_cast<Carryall*>(captive->createUnit(Unit_Carryall,true));
        require(homeLift != nullptr, "Cannot create the original owner's carryall");
        homeLift->deploy(site - Coord(4,0));
        auto* lift=aircraft();
        // Its own house repairs it, and it books a carryall for the trip.
        borrowed->doRepair();
        require(borrowed->getTarget()==homeYard && borrowed->goingToRepairYard
                && homeYard->getNumBookings()==1,
                "The fixture unit does not request repair from its own house");
        require(borrowed->isAwaitingPickup() && borrowed->hasBookedCarrier(),
                "The fixture repair trip booked no carryall, so conversion has nothing to release");
        // Conversion cancels the inherited trip: no target, no bay booking, no waiting lift.
        borrowed->deviate(owner);
        require(borrowed->wasDeviated() && borrowed->getOwner()==owner,
                "Deviation fixture did not hand the unit to the repair yard's house");
        require(borrowed->getTarget()==nullptr && !borrowed->goingToRepairYard
                && homeYard->getNumBookings()==0,
                "Conversion kept the inherited repair-yard trip or its booking");
        require(!borrowed->isAwaitingPickup() && !borrowed->hasBookedCarrier(),
                "Conversion left the unit waiting for a carryall its old owner will never send");
        // Explicit order, automatic trip and arrival gate all refuse while deviated.
        borrowed->doRepair();
        require(borrowed->getTarget()==nullptr && yard->getNumBookings()==0 && yard->isFree(),
                "An explicit repair order sent a deviated unit to the captor's yard");
        borrowed->checkPos();
        require(borrowed->getTarget()==nullptr && !borrowed->goingToRepairYard,
                "Automatic repair sent a deviated unit to the captor's yard");
        borrowed->doMove2Object(yard);
        require(borrowed->getTarget()==yard && !borrowed->goingToRepairYard
                && yard->getNumBookings()==0,
                "An ordered move onto the captor's yard booked a repair bay for a deviated unit");
        borrowed->setGettingRepaired();
        require(yard->isFree() && yard->getRepairUnit()==nullptr && borrowed->isActive()
                && borrowed->isVisible() && borrowed->getHealth()<borrowed->getMaxHealth(),
                "A deviated unit entered the captor's repair yard on arrival");
        // A carryall of the holding house delivers it to that house's yard: dropped on the
        // ground beside the bay instead of docked, and the bay stays free and unbooked.
        borrowed->setTarget(nullptr);
        lift->giveCargo(borrowed);
        require(lift->hasCargo(), "Deviated unit was not loaded for the delivery check");
        lift->setTarget(yard);
        ticks=0;
        while(lift->hasCargo() && ticks<4000) { require(lift->update(), "Delivery carryall vanished"); ++ticks; }
        require(!lift->hasCargo(), "Carryall never delivered its deviated cargo");
        require(yard->isFree() && yard->getRepairUnit()==nullptr && yard->getNumBookings()==0,
                "A carryall docked a deviated unit in the captor's repair yard");
        require(borrowed->isActive() && borrowed->getHealth()<borrowed->getMaxHealth(),
                "The deviated cargo was not put back on the ground still damaged");
        csv << "deviated_repair_refused,0,0," << ticks*0.016 << ",0,0\n";
        // Ownership reverts, and the ordinary repair trip works again.
        borrowed->quitDeviation();
        require(!borrowed->wasDeviated() && borrowed->getOwner()==captive,
                "Deviation did not revert to the original house");
        borrowed->doRepair();
        require(borrowed->getTarget()==homeYard && borrowed->goingToRepairYard
                && homeYard->getNumBookings()==1,
                "Repairs were not restored after the deviation ended");
        lift->cancelDeployment(); homeLift->cancelDeployment(); borrowed->cancelDeployment();
        homeYard->destroy();

        // A saved occupant can carry a fractional health residual: the stalled 1.0.794 game held
        // 47 bays at values such as 199.875 of 200, unchanged over 1000 cycles with no booked
        // carrier. The final healing step must land exactly on the maximum and release the bay.
        // Ground release only, so no option or flight is involved: the guard point is a legal tile
        // beside the bay, inside MIN_CARRYALL_LIFT_DISTANCE. Deliberately emits no CSV row, so the
        // three per-mod trajectory files stay byte-identical to the measurements above.
        require(yard->isFree() && yard->getNumBookings()==0,
                "The fractional repair scenario needs the fixture bay free and unbooked");
        for(const FixPoint residual : {0.5_fix, 1_fix}) {
            auto* patient=static_cast<GroundUnit*>(owner->createUnit(Unit_Tank,true));
            require(patient != nullptr, "Cannot create the fractional repair fixture unit");
            const Coord bay=yard->getLocation();
            const Coord spot=currentGameMap->findDeploySpot(patient,bay,currentGame->randomGen,
                                                            Coord::Invalid(),yard->getStructureSize());
            require(spot.isValid(), "No legal tile beside the fractional repair fixture bay");
            patient->deploy(spot);
            require(patient->getGuardPoint().isValid()
                    && blockDistance(bay,patient->getGuardPoint())<MIN_CARRYALL_LIFT_DISTANCE,
                    "The fractional repair fixture needs a valid guard point beside its bay");
            const FixPoint maximum=FixPoint(patient->getMaxHealth());
            patient->setHealth(maximum-residual);
            require(patient->getHealth()==maximum-residual,
                    "Fractional starting health was rejected by the health setter");
            patient->setTarget(yard);
            require(yard->getNumBookings()==1 && patient->goingToRepairYard,
                    "The fractional repair fixture did not book its bay");
            patient->setGettingRepaired();
            require(yard->getRepairUnit()==patient && !yard->isFree(),
                    "The fractional repair fixture did not enter its bay");
            require(!patient->hasBookedCarrier() && !patient->isAwaitingPickup(),
                    "The fractional repair fixture must not be waiting for a carryall");
            owner->addCredits(1000,false);
            require(owner->getCredits()>=1, "The fractional repair fixture bay cannot pay for healing");
            const House* originalOwner=patient->getOwner();
            const FixPoint creditsBefore=owner->storedCredits+owner->startingCredits+owner->cityCredits;
            ticks=0;
            // One update heals the last step, the next releases: a stall shows up immediately.
            while(!yard->isFree() && ticks<16) { yard->update(); ++ticks; }
            require(yard->isFree(),
                    "Repair yard never released a fractionally damaged occupant");
            require(patient->getHealth()==maximum,
                    "The released occupant did not reach exactly full health");
            require(creditsBefore-(owner->storedCredits+owner->startingCredits+owner->cityCredits)==UNIT_REPAIRCOST,
                    "The final healing step did not charge exactly one repair tick");
            require(patient->getOwner()==originalOwner,
                    "Repair release changed the occupant ownership");
            require(yard->getNumBookings()==0 && yard->getRepairUnit()==nullptr,
                    "Releasing the bay did not clear its job and booking");
            require(patient->isActive() && patient->isVisible()
                    && currentGameMap->tileExists(patient->getLocation()),
                    "The released occupant was not put back on the map");
            patient->cancelDeployment();
        }
    }
    csv.close();
    SDL_Log("CARRYALL_FLIGHT_PROBE_PASS: %s (%s)",mod.c_str(),baseline ? "baseline" : "regression");
    game.reset(); currentGame=nullptr;
    return -1;
}
