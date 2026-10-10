// Headless integration fixture; linked with the production game objects.
//
// The long-stall carryall rescue, against the real engine: a ground unit that has been on the
// same tile for thirty seconds of simulation time while it still wants to be somewhere else asks
// for a lift, independently of anything the pathfinder is doing.
//
// Every phase drives production UnitBase::update() on production units placed on a production
// map. Nothing here simulates the stall: the subject is walled in with real structures and
// really cannot move, and the clock is read from the unit itself.
#include <Game.h>
#include <Command.h>
#include <players/HumanPlayer.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <units/UnitBase.h>
#include <structures/StructureBase.h>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

int gStallChecks = 0;

void stallRequire(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("STALLED_CARRYALL_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
    ++gStallChecks;
}

constexpr Sint32 kStallCycles = UnitBase::longStallRescueCycles;

}  // namespace

int runStalledCarryallProbe() {
    const std::string mod = std::getenv("STALL_PROBE_MOD");
    stallRequire(ModManager::instance().setActiveMod(mod), "Missing stall fixture mod");
    settings.general.playerName = "Stall tester";
    // The rescue honours the same authorization the existing automatic requests use, so the
    // fixture opts the human house in explicitly. One phase below turns it back off and proves
    // the gate still holds.
    settings.gameOptions.manualCarryallDrops = true;

    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("stall.ini", mapData, "Stalled carryall", true, settings.gameOptions);
    init.randomSeed = 67;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Stall tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->bReplay = true;  // Private synthetic fixture: no public match analytics.
    game->initGame(init);
    game->bReplay = false;

    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* foe = game->getHouse(HOUSE_HARKONNEN);
    stallRequire(owner && owner->isAlive(), "Stall fixture house missing");
    stallRequire(foe && foe->isAlive(), "Stall fixture enemy house missing");
    stallRequire(game->getGameInitSettings().getGameOptions().manualCarryallDrops,
                 "Fixture did not opt the human house into carryall drops");

    // An open rectangle with room for a walled box, a drop site and parked aircraft.
    auto openPocket = [&](int width, int height) {
        for(int y = 3; y + height < currentGameMap->getSizeY() - 3; ++y) {
            for(int x = 3; x + width < currentGameMap->getSizeX() - 3; ++x) {
                bool clear = true;
                for(int dy = 0; dy < height && clear; ++dy) {
                    for(int dx = 0; dx < width; ++dx) {
                        const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                        if(tile->hasAGroundObject() || tile->isMountain()) { clear = false; break; }
                    }
                }
                if(clear) return Coord(x, y);
            }
        }
        throw std::runtime_error("Fixture needs an open pocket");
    };
    const Coord pocket = openPocket(16, 12);
    const Coord boxCentre = pocket + Coord(3, 3);
    const Coord dropSite = pocket + Coord(13, 9);

    // Deployment-style relocation: release the old tile first so the fixture never leaves ghost
    // occupancy behind for a later phase to trip over.
    auto relocate = [&](UnitBase* unit, const Coord& at) {
        currentGameMap->removeObjectFromMap(unit->getObjectID());
        unit->setLocation(at);
    };

    std::vector<StructureBase*> walls;
    auto sealBox = [&](const Coord& centre) {
        for(int dy = -1; dy <= 1; ++dy) {
            for(int dx = -1; dx <= 1; ++dx) {
                if(dx == 0 && dy == 0) continue;
                auto* wall = owner->placeStructure(NONE_ID, Structure_Wall,
                                                  centre.x + dx, centre.y + dy, true);
                stallRequire(wall != nullptr, "Could not seal the stall box");
                walls.push_back(wall);
            }
        }
    };
    auto openBox = [&] {
        for(StructureBase* wall : walls) wall->destroy();
        walls.clear();
    };

    auto makeTank = [&](const Coord& at) {
        auto* tank = static_cast<GroundUnit*>(owner->createUnit(Unit_Tank, true));
        stallRequire(tank != nullptr, "Cannot create stall subject");
        tank->deploy(at);
        tank->doSetAttackMode(GUARD);
        return tank;
    };
    auto makeCarryall = [&](House* house, const Coord& at) {
        auto* carry = static_cast<Carryall*>(house->createUnit(Unit_Carryall, true));
        stallRequire(carry != nullptr, "Cannot create carryall");
        carry->deploy(at);
        return carry;
    };

    // One simulated cycle: the production unit update plus the cycle counter the stall clock is
    // measured against. Deliberately no path scheduler run, so requests this unit enqueues stay
    // queued forever - the exact condition the old fallback could not see past.
    auto step = [&](const std::vector<UnitBase*>& units, int cycles,
                    const std::function<void(int)>& perCycle = {}) {
        for(int i = 0; i < cycles; ++i) {
            if(perCycle) perCycle(i);
            for(UnitBase* unit : units) unit->update();
            ++game->gameCycleCount;
        }
    };

    // -----------------------------------------------------------------------------------------
    // 1. A queued search and a route cleared every cycle still reach the threshold.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        const Coord goal = pocket + Coord(14, 1);
        tank->doMove2Pos(goal, true);
        stallRequire(tank->getDestination() == goal, "Subject did not take its order");

        bool sawQueuedRequest = false;
        int clearPathCalls = 0;
        step({tank}, kStallCycles - 2, [&](int) {
            if(tank->hasPendingPathRequest()) sawQueuedRequest = true;
            tank->clearPath();
            ++clearPathCalls;
        });
        stallRequire(sawQueuedRequest, "Subject never had a path request outstanding");
        stallRequire(clearPathCalls >= kStallCycles - 2, "clearPath was not exercised every cycle");
        stallRequire(tank->getLocation() == boxCentre, "Walled subject moved");
        stallRequire(!tank->isLongStalled(), "Threshold fired early");
        stallRequire(tank->getStalledCycleCount() >= static_cast<Uint32>(kStallCycles) - 4,
                     "Repeated clearPath reset the stall clock");

        // No carrier exists yet, so nothing can be booked and the unit must be left alone.
        step({tank}, 6, [&](int) { tank->clearPath(); });
        stallRequire(tank->isLongStalled(), "Threshold never fired");
        stallRequire(tank->getAttackMode() == GUARD, "A rescue with no carryall changed the mode");
        stallRequire(!tank->isAwaitingPickup() && !tank->hasBookedCarrier(),
                     "A rescue with no carryall booked something");
        SDL_Log("STALLED_CARRYALL_PROBE_THRESHOLD: cycles=%u pending=1 clearpath=%d",
                tank->getStalledCycleCount(), clearPathCalls);

        // Opening the box lets it take a real step, which is the only thing that resets the clock.
        openBox();
        tank->clearPath();
        tank->recalculatePathTimer = 0;
        int moved = 0;
        while(moved < 600 && tank->getLocation() == boxCentre) {
            game->processPathRequests();
            tank->update();
            ++game->gameCycleCount;
            ++moved;
        }
        stallRequire(tank->getLocation() != boxCentre, "Subject could not move after the box opened");
        stallRequire(tank->getStalledCycleCount() < static_cast<Uint32>(kStallCycles) / 4,
                     "A real tile change did not reset the stall clock");
        tank->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 2. A new order resets the clock; an identical AI-style re-issue does not.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        const Coord goal = pocket + Coord(14, 1);
        tank->doMove2Pos(goal, true);
        step({tank}, kStallCycles / 2);
        const Uint32 beforeReissue = tank->getStalledCycleCount();
        stallRequire(beforeReissue > static_cast<Uint32>(kStallCycles) / 4, "Clock did not accumulate");

        // The same order again, the way a bot re-issues a route every pass. It must not keep the
        // unit out of detection when the unit never actually moved.
        for(int i = 0; i < 40; ++i) {
            tank->doMove2Pos(goal, true);
            tank->update();
            ++game->gameCycleCount;
        }
        stallRequire(tank->getStalledCycleCount() > beforeReissue,
                     "An identical re-issued order suppressed stall detection");

        // A different destination is a different job.
        tank->doMove2Pos(pocket + Coord(14, 10), true);
        tank->update();
        stallRequire(tank->getStalledCycleCount() <= 1, "A changed order did not reset the clock");

        // And an explicit player order restarts it even when the coordinates repeat.
        step({tank}, 400);
        stallRequire(tank->getStalledCycleCount() > 300, "Clock did not resume after the new order");
        Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS, tank->getObjectID(),
                tank->getDestination().x, tank->getDestination().y, 1).executeCommand();
        stallRequire(tank->getStalledCycleCount() == 0,
                     "An explicit player order did not restart the clock");
        step({tank}, 100);
        const auto beforeInvalid = tank->getStalledCycleCount();
        Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS, tank->getObjectID(),
                tank->getDestination().x, tank->getDestination().y, 2).executeCommand();
        stallRequire(tank->getStalledCycleCount() == beforeInvalid,
                     "An invalid order reset the stall clock");
        Command(foe->getPlayerList().front()->getPlayerID(), CMD_UNIT_MOVE2POS, tank->getObjectID(),
                tank->getDestination().x, tank->getDestination().y, 1).executeCommand();
        stallRequire(tank->getStalledCycleCount() == beforeInvalid,
                     "An unauthorized order reset the stall clock");
        openBox();
        tank->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 3. Chasing one target rewrites destination constantly. That is the same job.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* quarry = static_cast<GroundUnit*>(foe->createUnit(Unit_Tank, true));
        stallRequire(quarry != nullptr, "Cannot create chase target");
        const Coord quarryStart = pocket + Coord(14, 2);
        quarry->deploy(quarryStart);
        quarry->doSetAttackMode(STOP);
        tank->doAttackObject(quarry, true);
        stallRequire(tank->getTarget() == quarry, "Subject did not take the chase target");

        std::set<std::pair<int,int>> seenDestinations;
        int shuffle = 0;
        step({tank}, kStallCycles + 8, [&](int i) {
            if(i % 7 == 0) {
                // A real relocation of the chased unit, which is what makes engageTarget rewrite
                // the subject's destination.
                const Coord at = quarryStart + Coord((shuffle++ % 3) - 1, 0);
                relocate(quarry, at);
            }
            seenDestinations.insert({tank->getDestination().x, tank->getDestination().y});
        });
        stallRequire(seenDestinations.size() >= 2,
                     "The chased target never rewrote the subject's destination");
        stallRequire(tank->getLocation() == boxCentre, "Walled chaser moved");
        stallRequire(tank->isLongStalled(),
                     "Same-target destination churn suppressed stall detection");
        SDL_Log("STALLED_CARRYALL_PROBE_CHASE: destinations=%zu stalled=%u",
                seenDestinations.size(), tank->getStalledCycleCount());

        // A different target is a different job.
        auto* other = static_cast<GroundUnit*>(foe->createUnit(Unit_Tank, true));
        other->deploy(pocket + Coord(14, 6));
        other->doSetAttackMode(STOP);
        tank->doAttackObject(other, true);
        tank->update();
        stallRequire(tank->getStalledCycleCount() <= 1, "A changed target did not reset the clock");

        openBox();
        tank->cancelDeployment(); quarry->cancelDeployment(); other->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 4. Exclusions: stopped, parked, cargo, inactive, and physically mid-step.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));

        tank->doMove2Pos(pocket + Coord(14, 1), true);
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier(), "An eligible stalled unit was not rescued");
        tank->cancelCarryallPickup();
        stallRequire(!tank->isAwaitingPickup() && !tank->hasBookedCarrier(), "Cancel left a booking");
        carry->setTarget(nullptr);

        // Stop: no intent, so no clock and no rescue.
        tank->doSetAttackMode(STOP);
        tank->carryallRequestCooldown = 0;
        step({tank}, kStallCycles + 68);
        stallRequire(tank->getStalledCycleCount() == 0, "A stopped unit accumulated stall time");
        stallRequire(!tank->hasBookedCarrier(), "A stopped unit was rescued");

        // Parked on its own destination, which is also how engageTarget signals a firing halt.
        tank->doSetAttackMode(GUARD);
        tank->setDestination(tank->getLocation());
        tank->carryallRequestCooldown = 0;
        step({tank}, kStallCycles + 68);
        stallRequire(tank->getStalledCycleCount() == 0,
                     "A unit whose destination is its own tile accumulated stall time");
        stallRequire(!tank->hasBookedCarrier(), "A deliberately halted unit was rescued");

        // Mid-step: an ordinary movement delay, not a stall.
        tank->doMove2Pos(pocket + Coord(14, 1), true);
        tank->lastProgressCycle = game->gameCycleCount - (kStallCycles + 50);
        tank->stallAnchor = tank->getLocation();
        tank->stallIntentTarget = NONE_ID;
        tank->stallIntentDestination = tank->getDestination();
        tank->carryallRequestCooldown = 0;
        tank->moving = true;
        tank->tryLongStallCarryallRescue();
        stallRequire(!tank->hasBookedCarrier(), "A unit in the middle of a step was rescued");
        tank->moving = false;

        // Cargo and off-map units are inactive, and update() does not touch them at all.
        tank->setActive(false);
        tank->carryallRequestCooldown = 0;
        step({tank}, 200);
        stallRequire(!tank->hasBookedCarrier(), "An inactive unit was rescued");
        tank->setActive(true);

        openBox();
        tank->cancelDeployment(); carry->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5. Authorization: a human house that has not opted in is never rescued automatically.
    // -----------------------------------------------------------------------------------------
    {
        game->gameInitSettings.gameOptions.manualCarryallDrops = false;
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));
        tank->doMove2Pos(pocket + Coord(14, 1), true);
        step({tank}, kStallCycles + 68);
        stallRequire(tank->isLongStalled(), "The clock stopped when the option was off");
        stallRequire(!tank->hasBookedCarrier() && tank->getAttackMode() == GUARD,
                     "A human house that did not opt in was rescued anyway");
        game->gameInitSettings.gameOptions.manualCarryallDrops = true;
        openBox();
        tank->cancelDeployment(); carry->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 6. A short trip is still rescued once it has been stalled for the full threshold.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));
        const Coord shortGoal = boxCentre + Coord(2, 0);
        tank->doMove2Pos(shortGoal, true);
        stallRequire(blockDistance(tank->getLocation(), tank->getDestination())
                         < MIN_CARRYALL_LIFT_DISTANCE,
                     "The short-trip phase needs a goal inside the legacy lift floor");
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier(),
                     "The legacy six-tile lift floor blocked the long-stall rescue");
        SDL_Log("STALLED_CARRYALL_PROBE_SHORT: distance=%d booked=1",
                blockDistance(boxCentre, shortGoal).lround());
        tank->cancelCarryallPickup();
        openBox();
        tank->cancelDeployment(); carry->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 7. Candidate selection: exactly one free owned carrier, and nothing else.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);

        auto* hostile = makeCarryall(foe, pocket + Coord(1, 1));
        auto* busy = makeCarryall(owner, pocket + Coord(1, 3));
        auto* delivery = makeCarryall(owner, pocket + Coord(1, 5));
        delivery->setOwned(false);
        delivery->setDropOfferer(true);
        auto* firstFree = makeCarryall(owner, pocket + Coord(1, 7));
        auto* secondFree = makeCarryall(owner, pocket + Coord(1, 9));

        // A real booking for the busy carrier: a target is what isBooked() reports.
        auto* decoy = makeTank(pocket + Coord(14, 1));
        busy->setTarget(decoy);
        stallRequire(busy->isBooked(), "The busy carrier fixture is not actually booked");

        const auto& candidates = game->getCarryallCandidateIds();
        stallRequire(candidates.size() >= 5, "The carrier index missed carryalls");
        for(const Uint32 id : candidates) {
            auto* unit = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
            stallRequire(unit != nullptr && isCarryallUnit(unit->getItemID()),
                         "The carrier index listed something that is not a carryall");
        }

        tank->doMove2Pos(pocket + Coord(14, 10), true);
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier(), "No carrier was booked");
        const UnitBase* chosen = tank->getCarrier();
        stallRequire(chosen == firstFree, "Rescue did not take the first free owned carrier");
        stallRequire(chosen != hostile, "Rescue booked an enemy carryall");
        stallRequire(chosen != busy, "Rescue booked an already busy carryall");
        stallRequire(chosen != delivery, "Rescue booked a one-way delivery flight");
        stallRequire(!secondFree->isBooked(), "Rescue booked more than one carrier");

        // An equal-count replacement in the same cycle must invalidate the derived carrier index.
        game->getCarryallCandidateIds();
        const auto replacedId = firstFree->getObjectID();
        const auto countBefore = unitList.size();
        const auto cycleBefore = game->gameCycleCount;
        firstFree->destroy();
        auto* replacement = makeCarryall(owner, pocket + Coord(1, 7));
        stallRequire(unitList.size() == countBefore && game->gameCycleCount == cycleBefore,
                     "Carrier replacement changed count or cycle");
        const auto& refreshed = game->getCarryallCandidateIds();
        stallRequire(std::find(refreshed.begin(), refreshed.end(), replacedId) == refreshed.end(),
                     "The carrier index retained a removed carrier");
        stallRequire(std::find(refreshed.begin(), refreshed.end(), replacement->getObjectID()) != refreshed.end(),
                     "The carrier index missed a same-cycle replacement");

        // Carrier loss: the stale booking clears itself and the retry takes the next free carrier.
        tank->update();
        stallRequire(!tank->hasBookedCarrier(), "A destroyed carrier left a stale booking");
        tank->carryallRequestCooldown = 0;
        step({tank}, 400);
        stallRequire(tank->hasBookedCarrier() && tank->getCarrier() == secondFree,
                     "The unit did not retry after losing its carrier");
        tank->cancelCarryallPickup();

        openBox();
        tank->cancelDeployment(); decoy->cancelDeployment();
        hostile->cancelDeployment(); busy->cancelDeployment(); delivery->cancelDeployment();
        secondFree->cancelDeployment();
        replacement->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 8. A fully booked fleet: bounded retries, and the unit is never frozen waiting.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* only = makeCarryall(owner, pocket + Coord(1, 10));
        auto* decoy = makeTank(pocket + Coord(14, 1));
        only->setTarget(decoy);
        stallRequire(only->isBooked(), "The fleet fixture is not fully booked");

        tank->doMove2Pos(pocket + Coord(14, 10), true);
        step({tank}, kStallCycles + 68);

        int attempts = 0;
        const int observed = 3000;
        Sint32 previousCooldown = tank->carryallRequestCooldown;
        step({tank}, observed, [&](int) {
            if(tank->carryallRequestCooldown > previousCooldown) ++attempts;
            previousCooldown = tank->carryallRequestCooldown;
        });
        const int ceiling = observed / UnitBase::carryallRescueCooldownCycles + 2;
        stallRequire(attempts > 0, "A stalled unit stopped retrying when the fleet was busy");
        stallRequire(attempts <= ceiling, "Failed rescues are not throttled");
        stallRequire(tank->getAttackMode() == GUARD,
                     "A failed rescue parked the unit in CARRYALLREQUESTED");
        stallRequire(!tank->isAwaitingPickup() && !tank->hasBookedCarrier(),
                     "A failed rescue left a phantom pickup");
        stallRequire(tank->isLongStalled(),
                     "A failed rescue restarted the thirty-second clock instead of retrying");
        SDL_Log("STALLED_CARRYALL_PROBE_THROTTLE: attempts=%d cycles=%d ceiling=%d",
                attempts, observed, ceiling);

        // Freeing the carrier is picked up on the next retry, not on the next thirty seconds.
        only->setTarget(nullptr);
        step({tank}, UnitBase::carryallRescueCooldownCycles + UnitBase::carryallRescueJitterCycles + 4);
        stallRequire(tank->hasBookedCarrier(), "A freed carrier was not taken on the next retry");
        tank->cancelCarryallPickup();

        openBox();
        tank->cancelDeployment(); decoy->cancelDeployment(); only->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 9. Cancellation: a forced move and Stop both release an outstanding booking.
    // -----------------------------------------------------------------------------------------
    for(const bool useStop : {false, true}) {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));
        tank->doMove2Pos(pocket + Coord(14, 10), true);
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier() && carry->isBooked(), "Cancellation phase never booked");

        if(useStop) {
            tank->doSetAttackMode(STOP);
            stallRequire(tank->getAttackMode() == STOP, "Stop did not take");
        } else {
            tank->doMove2Pos(pocket + Coord(14, 1), true);
        }
        stallRequire(!tank->isAwaitingPickup(), "The order did not cancel the pickup");
        stallRequire(!tank->hasBookedCarrier(), "The order left the carrier booked");

        stallRequire(!carry->isBooked(), "The carryall kept a cancelled assignment until its next tick");

        // Rebooking in this same cycle must leave one reciprocal assignment, not two aircraft.
        auto* second = makeCarryall(owner, pocket + Coord(1, 8));
        carry->setTarget(second);  // The former carrier is now busy with a different job.
        Command(pLocalPlayer->getPlayerID(), CMD_UNIT_REQUESTCARRYALLDROP, tank->getObjectID(),
                dropSite.x, dropSite.y).executeCommand();
        stallRequire(tank->getCarrier() == second && second->getTarget() == tank,
                     "A new explicit drop order did not book the free second carrier immediately");
        stallRequire(carry->getTarget() != tank, "Two aircraft retained the same passenger");
        // An attack order must also cancel the promised pickup through the synchronized command path.
        Command(pLocalPlayer->getPlayerID(), CMD_UNIT_ATTACKPOS, tank->getObjectID(),
                dropSite.x, dropSite.y, 1).executeCommand();
        stallRequire(!tank->isAwaitingPickup() && !second->isBooked(),
                     "An explicit attack order retained a pickup booking");
        SDL_Log("STALLED_CARRYALL_PROBE_CANCEL: stop=%d immediate=1 rebook=1", useStop ? 1 : 0);

        openBox();
        tank->cancelDeployment(); carry->cancelDeployment(); second->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 10. The whole trip: a rescue books, collects and puts the unit down at its own goal.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));
        tank->doMove2Pos(dropSite, true);
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier(), "The delivery phase never booked a carrier");
        stallRequire(tank->getDestination() == dropSite, "Booking changed the requested destination");

        int ticks = 0;
        while(!carry->hasCargo() && ticks < 6000) {
            stallRequire(carry->update(), "The carryall vanished before pickup");
            tank->update();
            ++game->gameCycleCount;
            ++ticks;
        }
        stallRequire(carry->hasCargo(), "The carryall never collected the stalled unit");
        stallRequire(!tank->isActive() && tank->pickedUp, "The collected unit is still on the map");
        stallRequire(carry->getDestination() == dropSite,
                     "The carryall did not inherit the unit's own destination");

        // The box can come down now: the unit is in the air and its goal is elsewhere.
        openBox();
        while(carry->hasCargo() && ticks < 12000) {
            stallRequire(carry->update(), "The carryall vanished during delivery");
            ++game->gameCycleCount;
            ++ticks;
        }
        stallRequire(!carry->hasCargo(), "The carryall never delivered");
        stallRequire(tank->isActive() && !tank->pickedUp, "The delivered unit is not back on the map");
        const int missed = blockDistance(tank->getLocation(), dropSite).lround();
        stallRequire(missed <= 4, "The unit was dropped nowhere near its goal");
        stallRequire(currentGameMap->getTile(tank->getLocation())->hasAGroundObject(),
                     "The delivered unit does not occupy its tile");
        stallRequire(tank->getLocation() != boxCentre, "The unit was put back where it was stuck");
        stallRequire(!currentGameMap->getTile(boxCentre)->hasAGroundObject(),
                     "Ghost occupancy left behind at the stall site");
        SDL_Log("STALLED_CARRYALL_PROBE_DELIVERY: ticks=%d missed_tiles=%d", ticks, missed);
        tank->cancelDeployment(); carry->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 11. Observer runtime 7 carries the clock; an ordinary load deliberately does not.
    // -----------------------------------------------------------------------------------------
    {
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        tank->doMove2Pos(pocket + Coord(14, 10), true);
        step({tank}, 900);
        const Uint32 stalledBefore = tank->getStalledCycleCount();
        stallRequire(stalledBefore > 800, "The roundtrip phase needs an accumulated clock");

        OMemoryStream out; out.open();
        tank->saveObserverRuntime(out);
        const std::string bytes(out.getData(), out.getDataLength());

        // Clobber exactly the fields the stream is responsible for.
        tank->lastProgressCycle = game->gameCycleCount;
        tank->stallAnchor = Coord::Invalid();
        tank->stallIntentTarget = 4242;
        tank->stallIntentDestination = Coord::Invalid();
        stallRequire(tank->getStalledCycleCount() == 0, "The clobber did not clear the clock");

        IMemoryStream in(bytes.data(), bytes.size());
        tank->loadObserverRuntime(in);
        stallRequire(tank->getStalledCycleCount() == stalledBefore,
                     "The observer runtime did not restore the stall clock");
        stallRequire(tank->stallAnchor == boxCentre, "The observer runtime lost the stall anchor");
        stallRequire(tank->stallIntentTarget == NONE_ID, "The observer runtime lost the stall intent");

        // A clock stamped in the future is rejected rather than silently disabling the rescue.
        {
            OMemoryStream bad; bad.open();
            tank->lastProgressCycle = game->gameCycleCount + 1000;
            tank->saveObserverRuntime(bad);
            tank->lastProgressCycle = game->gameCycleCount;
            IMemoryStream reader(bad.getData(), bad.getDataLength());
            bool rejected = false;
            try { tank->loadObserverRuntime(reader); } catch(const std::exception&) { rejected = true; }
            stallRequire(rejected, "A stall clock from the future was accepted");
        }

        // The whole-game checkpoint announces version 8: the stall clock added in 7 is still
        // carried, and 8 appends QuantBot's aircraft permissions and asset-attack contacts.
        const std::string runtime = game->saveObserverRuntime();
        IMemoryStream header(runtime.data(), runtime.size());
        stallRequire(header.readUint32() == 8, "The observer runtime version is not 8");
        stallRequire(header.readUint32() == game->gameCycleCount,
                     "The observer runtime header does not lead with version then cycle");

        // An ordinary load leaves the clock at its constructed defaults, which is a fresh grace
        // period: the first update stamps "now" however long the save sat on disk.
        auto* fresh = makeTank(pocket + Coord(14, 4));
        stallRequire(fresh->lastProgressCycle == 0 && fresh->stallAnchor.isInvalid(),
                     "A newly constructed unit carries a stall clock");
        game->gameCycleCount += 50000;
        fresh->doMove2Pos(pocket + Coord(14, 10), true);
        stallRequire(fresh->getStalledCycleCount() == 0,
                     "An unstamped unit inherited elapsed match time");
        fresh->update();
        stallRequire(fresh->getStalledCycleCount() == 0,
                     "A unit restored without a clock did not get a fresh grace period");

        openBox();
        tank->cancelDeployment(); fresh->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 12. The one documented exception: a badly damaged passenger goes to the repair yard.
    // -----------------------------------------------------------------------------------------
    {
        auto* yard = static_cast<StructureBase*>(owner->placeStructure(
            NONE_ID, Structure_RepairYard, pocket.x + 9, pocket.y, true, true));
        stallRequire(yard != nullptr, "Could not place the repair yard");
        auto* tank = makeTank(boxCentre);
        sealBox(boxCentre);
        auto* carry = makeCarryall(owner, pocket + Coord(1, 10));
        // Forced, so the ordinary "drive yourself to the yard" rule in checkPos stays out of the
        // way and the booking really comes from the long-stall rescue.
        tank->doMove2Pos(dropSite, true);
        tank->setHealth(tank->getMaxHealth() / 3);
        stallRequire(tank->isBadlyDamaged(), "The repair-priority phase needs a damaged unit");
        step({tank}, kStallCycles + 68);
        stallRequire(tank->hasBookedCarrier(), "A damaged stalled unit was not rescued");
        stallRequire(tank->getDestination() == dropSite, "Booking changed the requested destination");

        int ticks = 0;
        while(!carry->hasCargo() && ticks < 6000) {
            stallRequire(carry->update(), "The carryall vanished before the damaged pickup");
            tank->update();
            ++game->gameCycleCount;
            ++ticks;
        }
        stallRequire(carry->hasCargo(), "The damaged unit was never collected");
        // Repair priority overrides the unit's own goal at pickup time. That is the existing
        // Carryall::pickupTarget rule - doRepair() retargets the passenger, and the carryall takes
        // the repair yard as its own target - and it is deliberately left alone here.
        stallRequire(carry->getTarget() == yard,
                     "A badly damaged passenger was not redirected to the repair yard");
        stallRequire(carry->getDestination() != dropSite,
                     "The repair diversion did not override the unit's own destination");
        SDL_Log("STALLED_CARRYALL_PROBE_REPAIR: redirected=1 ticks=%d", ticks);

        openBox();
        tank->setHealth(tank->getMaxHealth());
        tank->cancelDeployment(); carry->cancelDeployment(); yard->destroy();
    }

    SDL_Log("STALLED_CARRYALL_PROBE_PASS: checks=%d mod=%s threshold_cycles=%d cooldown_cycles=%d",
            gStallChecks, mod.c_str(), kStallCycles, UnitBase::carryallRescueCooldownCycles);
    game.reset();
    currentGame = nullptr;
    return -1;  // Leaves the host's menu loop, the same convention the flight probe uses.
}
