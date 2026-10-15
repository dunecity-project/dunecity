/*
 *  RtsControlsTestCase.cpp
 *
 *  The pure rules behind the modern RTS controls (docs/rts-controls.md): the per-unit order
 *  queue (Shift-queued orders, patrol cycling, the 16-order cap, skipping dead targets),
 *  the attack-move engagement, leash and arrival rules, HOLD never chasing, and repeat
 *  production. The engine applies exactly these functions; tests/units/rts-controls-probe.inc
 *  shows the same behaviour on real units over real game cycles.
 */

#include <catch2/catch_all.hpp>

#include <units/UnitOrderQueue.h>
#include <structures/ProductionRepeat.h>
#include <Command.h>
#include <CommandAuthorization.h>
#include <CommandValidation.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

#include <string>

namespace {

UnitOrder move(int x, int y) { return UnitOrder::at(UnitOrderType::Move, Coord(x, y)); }
UnitOrder attack(Uint32 id) { return UnitOrder::on(UnitOrderType::AttackObject, id); }

std::vector<Coord> patrolPoints(const UnitOrderQueue& queue) {
    std::vector<Coord> points;
    for(const UnitOrder& order : queue) points.push_back(order.pos);
    return points;
}

template<class T>
std::string saved(const T& value) {
    OMemoryStream out;
    out.open();
    value.save(out);
    return std::string(out.getData(), out.getDataLength());
}

} // namespace

TEST_CASE("Order queue: an unshifted order replaces, a Shift order appends in order", "[rts-controls][queue]") {
    UnitOrderQueue queue;
    queue.replace(move(1, 1));
    REQUIRE(queue.append(move(2, 2)));
    REQUIRE(queue.append(attack(42)));
    REQUIRE(queue.size() == 3);
    REQUIRE(queue[0] == move(1, 1));
    REQUIRE(queue[1] == move(2, 2));
    REQUIRE(queue[2] == attack(42));

    // Completed orders leave from the front, in the order they were given.
    queue.advance();
    REQUIRE(queue.front() == move(2, 2));
    queue.advance();
    REQUIRE(queue.front() == attack(42));
    queue.advance();
    REQUIRE(queue.empty());

    // Without Shift, any order clears everything queued before it.
    queue.replace(move(1, 1));
    queue.append(move(2, 2));
    queue.replace(move(9, 9));
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.front() == move(9, 9));

    // Stop / Hold clear it entirely.
    queue.clear();
    REQUIRE(queue.empty());
    queue.advance();    // finishing nothing is harmless
    REQUIRE(queue.empty());
}

TEST_CASE("Order queue: holds at most 16 orders", "[rts-controls][queue]") {
    UnitOrderQueue queue;
    for(int i = 0; i < 16; ++i) {
        INFO("order " << i);
        REQUIRE(queue.append(move(i, 0)));
    }
    REQUIRE(queue.size() == UnitOrderQueue::kMaxOrders);
    REQUIRE_FALSE(queue.append(move(99, 99)));
    REQUIRE(queue.size() == 16);
    REQUIRE(queue[15] == move(15, 0));
    // A patrol cannot squeeze past the cap either.
    REQUIRE_FALSE(queue.extendPatrol(Coord(40, 40), Coord(0, 0)));
    REQUIRE(queue.size() == 16);
}

TEST_CASE("Order queue: orders whose target is gone are skipped", "[rts-controls][queue]") {
    UnitOrderQueue queue;
    queue.replace(attack(10));
    queue.append(attack(11));
    queue.append(move(5, 5));
    queue.append(attack(12));
    const auto alive = [](const UnitOrder& order) { return !order.usesTarget() || order.targetID == 12; };
    REQUIRE(queue.skipInvalid(alive) == 2);
    REQUIRE(queue.front() == move(5, 5));
    // A valid front is never touched, even with dead targets further back.
    REQUIRE(queue.skipInvalid([](const UnitOrder& order) { return !order.usesTarget(); }) == 0);
    REQUIRE(queue.size() == 2);
    queue.advance();
    REQUIRE(queue.skipInvalid([](const UnitOrder&) { return false; }) == 1);
    REQUIRE(queue.empty());
}

TEST_CASE("Order queue: patrol ping-pongs between two points and cycles through more", "[rts-controls][queue][patrol]") {
    const Coord home(10, 10), a(20, 10), b(20, 20), c(10, 20);
    UnitOrderQueue queue;
    REQUIRE(queue.startPatrol(a, home));
    REQUIRE(patrolPoints(queue) == std::vector<Coord>{a, home});
    REQUIRE(queue[1].patrolAnchor);

    // Two points: there and back, forever.
    for(int lap = 0; lap < 3; ++lap) {
        REQUIRE(queue.front().pos == a);
        queue.advance();
        REQUIRE(queue.front().pos == home);
        queue.advance();
    }
    REQUIRE(queue.size() == 2);

    // Shift-added points extend the route in the order they were clicked.
    REQUIRE(queue.extendPatrol(b, home));
    REQUIRE(queue.extendPatrol(c, home));
    REQUIRE(patrolPoints(queue) == std::vector<Coord>{a, b, c, home});
    std::vector<Coord> visited;
    for(int i = 0; i < 8; ++i) { visited.push_back(queue.front().pos); queue.advance(); }
    REQUIRE(visited == std::vector<Coord>{a, b, c, home, a, b, c, home});

    // Adding a point while walking back to the start still lands between the last point and the start.
    UnitOrderQueue returning;
    REQUIRE(returning.startPatrol(a, home));
    returning.advance();                    // now heading home
    REQUIRE(returning.front().pos == home);
    REQUIRE(returning.extendPatrol(b, home));
    visited.clear();
    for(int i = 0; i < 6; ++i) { visited.push_back(returning.front().pos); returning.advance(); }
    REQUIRE(visited == std::vector<Coord>{home, a, b, home, a, b});
}

TEST_CASE("Order queue: a patrol queued after moves starts where the moves end", "[rts-controls][queue][patrol]") {
    UnitOrderQueue queue;
    queue.replace(move(30, 30));
    REQUIRE(queue.extendPatrol(Coord(40, 30), Coord(0, 0)));
    REQUIRE(queue.size() == 3);
    REQUIRE(queue[2].pos == Coord(30, 30));     // anchored at the move's destination
    REQUIRE(queue[2].patrolAnchor);
    queue.advance();                            // the move is done and does not come back
    REQUIRE(queue.size() == 2);
    queue.advance();
    queue.advance();
    REQUIRE(queue.size() == 2);
    REQUIRE(queue.front().pos == Coord(40, 30));
}

TEST_CASE("Order queue: a patrol leg the unit has already reached is refused", "[rts-controls][queue][patrol]") {
    UnitOrderQueue queue;
    const Coord home(10, 10);
    REQUIRE_FALSE(queue.startPatrol(home, home));
    REQUIRE_FALSE(queue.startPatrol(Coord(12, 12), home));       // within the arrival radius
    REQUIRE(queue.startPatrol(Coord(13, 10), home));             // one tile beyond it
    REQUIRE_FALSE(queue.append(UnitOrder::at(UnitOrderType::Patrol, Coord(1, 1))));   // only via the patrol entry points
}

TEST_CASE("Attack-move: enemies within max(weapon, view) range stop the walk", "[rts-controls][attack-move]") {
    // A tank with weapon range 4 and view range 3 engages at 4, not at 5.
    REQUIRE(UnitOrderPolicy::attackMoveEngages(4, 4, 3));
    REQUIRE_FALSE(UnitOrderPolicy::attackMoveEngages(5, 4, 3));
    // A short-range unit with a wide view uses the view range.
    REQUIRE(UnitOrderPolicy::attackMoveEngages(6, 2, 6));
    REQUIRE_FALSE(UnitOrderPolicy::attackMoveEngages(7, 2, 6));
}

TEST_CASE("Attack-move: the leash lets a unit chase eight tiles from where it broke off", "[rts-controls][attack-move]") {
    const Coord breakOff(20, 20);
    REQUIRE_FALSE(UnitOrderPolicy::attackMoveLeashBroken(breakOff, Coord(28, 20)));
    REQUIRE_FALSE(UnitOrderPolicy::attackMoveLeashBroken(breakOff, Coord(12, 28)));   // diagonal counts as 8
    REQUIRE(UnitOrderPolicy::attackMoveLeashBroken(breakOff, Coord(29, 20)));
    REQUIRE(UnitOrderPolicy::attackMoveLeashBroken(breakOff, Coord(20, 11)));
    // Not fighting: no leash.
    REQUIRE_FALSE(UnitOrderPolicy::attackMoveLeashBroken(Coord::Invalid(), Coord(60, 60)));
}

TEST_CASE("Attack-move: arrival, resume and give-up", "[rts-controls][attack-move]") {
    using UnitOrderPolicy::WalkProgress;
    const Coord goal(40, 40);
    // Still moving or fighting: nothing to decide.
    REQUIRE(UnitOrderPolicy::walkProgress(false, Coord(40, 40), goal, 0) == WalkProgress::Walking);
    REQUIRE(UnitOrderPolicy::walkProgress(false, Coord(0, 0), goal, 3) == WalkProgress::Walking);
    // At the goal, or parked next to an occupied goal tile: arrived.
    REQUIRE(UnitOrderPolicy::walkProgress(true, goal, goal, 0) == WalkProgress::Arrived);
    REQUIRE(UnitOrderPolicy::walkProgress(true, Coord(42, 38), goal, 0) == WalkProgress::Arrived);
    // Dropped far away: resumed three times, then given up.
    for(int retries = 0; retries < UnitOrderPolicy::kWalkRetries; ++retries) {
        INFO("retry " << retries);
        REQUIRE(UnitOrderPolicy::walkProgress(true, Coord(43, 40), goal, retries) == WalkProgress::Resume);
    }
    REQUIRE(UnitOrderPolicy::walkProgress(true, Coord(43, 40), goal, UnitOrderPolicy::kWalkRetries) == WalkProgress::GiveUp);
}

TEST_CASE("HOLD never chases and never leaves for repairs", "[rts-controls][hold]") {
    using UnitOrderPolicy::OutOfRange;
    // Every mode that moves keeps chasing ground targets; HOLD lets them go.
    for(int mode = 0; mode < ATTACKMODE_MAX; ++mode) {
        const auto attackMode = static_cast<ATTACKMODE>(mode);
        INFO("mode " << mode);
        REQUIRE(UnitOrderPolicy::outOfRangeResponse(attackMode, true) == OutOfRange::Release);
        if(attackMode == HOLD) {
            REQUIRE(UnitOrderPolicy::outOfRangeResponse(attackMode, false) == OutOfRange::Release);
            REQUIRE_FALSE(UnitOrderPolicy::mayAutoRepair(attackMode));
        } else {
            REQUIRE(UnitOrderPolicy::outOfRangeResponse(attackMode, false) == OutOfRange::Approach);
            REQUIRE(UnitOrderPolicy::mayAutoRepair(attackMode));
        }
    }
    // HOLD follows RETREAT and is a mode a command may carry.
    REQUIRE(HOLD == RETREAT + 1);
    REQUIRE(ATTACKMODE_MAX == HOLD + 1);
    REQUIRE(CommandAuthorization::isValidAttackMode(HOLD));
}

TEST_CASE("Repeat production is offered by factories only, for units only", "[rts-controls][repeat]") {
    for(int builder : {Structure_Barracks, Structure_WOR, Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory}) {
        INFO("builder " << builder);
        REQUIRE(ProductionRepeat::builderSupportsRepeat(builder));
    }
    REQUIRE_FALSE(ProductionRepeat::builderSupportsRepeat(Structure_ConstructionYard));
    REQUIRE_FALSE(ProductionRepeat::builderSupportsRepeat(Structure_StarPort));
    REQUIRE(ProductionRepeat::itemSupportsRepeat(Structure_HeavyFactory, Unit_Tank));
    REQUIRE_FALSE(ProductionRepeat::itemSupportsRepeat(Structure_ConstructionYard, Structure_WindTrap));
    REQUIRE_FALSE(ProductionRepeat::itemSupportsRepeat(Structure_HeavyFactory, Structure_WindTrap));
}

TEST_CASE("Repeat production re-queues through limits and stops on cancel", "[rts-controls][repeat]") {
    ProductionRepeat::RepeatSet repeat;
    repeat.set(Unit_Tank, true);
    REQUIRE(repeat.requeueAfterCompletion(Unit_Tank, true));
    REQUIRE_FALSE(repeat.requeueAfterCompletion(Unit_Launcher, true));
    // Not buildable any more (prerequisite lost): nothing is queued.
    REQUIRE_FALSE(repeat.requeueAfterCompletion(Unit_Tank, false));

    // The re-queued copy waits: a reached unit limit, no credits or on-hold pause production
    // but are no reason not to queue it, so it is still there once the limit frees up.
    REQUIRE(repeat.requeueAfterCompletion(Unit_Tank, true));
    REQUIRE_FALSE(ProductionRepeat::productionAdvances(false, true, true));     // unit limit
    REQUIRE_FALSE(ProductionRepeat::productionAdvances(false, false, false));   // no credits
    REQUIRE_FALSE(ProductionRepeat::productionAdvances(true, false, true));     // on hold
    REQUIRE(ProductionRepeat::productionAdvances(false, false, true));

    // Cancelling (right-click) clears the repeat.
    repeat.set(Unit_Tank, false);
    REQUIRE_FALSE(repeat.contains(Unit_Tank));
    REQUIRE_FALSE(repeat.requeueAfterCompletion(Unit_Tank, true));
    REQUIRE(repeat.empty());
}

TEST_CASE("Save round trip: order queue and repeat flags", "[rts-controls][save]") {
    UnitOrderQueue queue;
    queue.replace(move(3, 4));
    queue.append(attack(77));
    queue.append(UnitOrder::on(UnitOrderType::MoveToObject, 78));
    queue.append(UnitOrder::on(UnitOrderType::Capture, 79));
    queue.append(UnitOrder::on(UnitOrderType::HarvesterReturn, NONE_ID));
    queue.append(UnitOrder::at(UnitOrderType::AttackMove, Coord(50, 60)));
    REQUIRE(queue.extendPatrol(Coord(70, 60), Coord(0, 0)));

    const std::string bytes = saved(queue);
    IMemoryStream in(bytes.data(), bytes.size());
    UnitOrderQueue restored;
    restored.load(in);
    REQUIRE(in.getRemainingLength() == 0);
    REQUIRE(restored == queue);
    REQUIRE(restored[restored.size() - 1].patrolAnchor);

    ProductionRepeat::RepeatSet repeat;
    repeat.set(Unit_Tank, true);
    repeat.set(Unit_Trooper, true);
    const std::string repeatBytes = saved(repeat);
    IMemoryStream repeatIn(repeatBytes.data(), repeatBytes.size());
    ProductionRepeat::RepeatSet repeatRestored;
    repeatRestored.load(repeatIn);
    REQUIRE(repeatIn.getRemainingLength() == 0);
    REQUIRE(repeatRestored == repeat);
    REQUIRE(repeatRestored.contains(Unit_Trooper));

    // An empty queue is the fixed 1-byte count the 9855 unit record starts with.
    REQUIRE(saved(UnitOrderQueue()).size() == 1);
}

TEST_CASE("Save load rejects corrupt order queues and repeat sets", "[rts-controls][save]") {
    UnitOrderQueue queue;
    queue.replace(move(3, 4));
    std::string bytes = saved(queue);
    std::string badType = bytes;
    badType[1] = static_cast<char>(UnitOrderType::Count);
    IMemoryStream badTypeIn(badType.data(), badType.size());
    UnitOrderQueue restored;
    REQUIRE_THROWS(restored.load(badTypeIn));

    std::string tooMany = bytes;
    tooMany[0] = static_cast<char>(UnitOrderQueue::kMaxOrders + 1);
    IMemoryStream tooManyIn(tooMany.data(), tooMany.size());
    REQUIRE_THROWS(restored.load(tooManyIn));

    OMemoryStream out;
    out.open();
    out.writeUint32(1);
    out.writeUint32(Structure_WindTrap);
    IMemoryStream structureIn(out.getData(), out.getDataLength());
    ProductionRepeat::RepeatSet repeat;
    REQUIRE_THROWS(repeat.load(structureIn));
}

TEST_CASE("RTS control commands have fixed arity and act on owned objects", "[rts-controls][command]") {
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_ATTACKMOVE, 4));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_PATROL, 4));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_QUEUEORDER, 4));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_BUILDER_SETREPEAT, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_ATTACKMOVE, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_BUILDER_SETREPEAT, 4));
    for(CMDTYPE command : {CMD_UNIT_ATTACKMOVE, CMD_UNIT_PATROL, CMD_UNIT_QUEUEORDER, CMD_BUILDER_SETREPEAT}) {
        INFO("command " << static_cast<int>(command));
        REQUIRE(CommandAuthorization::actsOnOwnedObject(command));
    }
}
