#ifndef UNITORDERQUEUE_H
#define UNITORDERQUEUE_H

#include <DataTypes.h>
#include <Definitions.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <vector>

/**
    The player orders a unit carries: Shift-queued orders, attack-move and patrol
    (docs/rts-controls.md). The front entry is the order the unit is executing; the
    rest run after it, in order. Everything here is pure data and integer rules, so the
    simulation, the save format and the unit tests share exactly the same behaviour.
*/
enum class UnitOrderType : Uint8 {
    Move = 0,           ///< walk to pos (forced, like a right-click move)
    MoveToObject,       ///< follow / enter targetID (right-click on an own or allied object)
    AttackObject,       ///< attack targetID
    AttackMove,         ///< walk to pos, fighting enemies met on the way
    Patrol,             ///< attack-move to pos, then cycle to the back of the queue
    Capture,            ///< infantry capture of structure targetID
    HarvesterReturn,    ///< harvester returns its load
    Count
};

struct UnitOrder {
    UnitOrderType type = UnitOrderType::Move;
    Coord pos = Coord::Invalid();
    Uint32 targetID = NONE_ID;
    /// The patrol entry that closes the route (the unit's position when the patrol was
    /// issued). Later Shift+patrol points are inserted before it.
    bool patrolAnchor = false;

    static UnitOrder at(UnitOrderType type, const Coord& pos) {
        UnitOrder order; order.type = type; order.pos = pos; return order;
    }
    static UnitOrder on(UnitOrderType type, Uint32 targetID) {
        UnitOrder order; order.type = type; order.targetID = targetID; return order;
    }

    bool usesPosition() const {
        return type == UnitOrderType::Move || type == UnitOrderType::AttackMove || type == UnitOrderType::Patrol;
    }
    bool usesTarget() const {
        return type == UnitOrderType::MoveToObject || type == UnitOrderType::AttackObject || type == UnitOrderType::Capture;
    }

    bool operator==(const UnitOrder& other) const {
        return type == other.type && pos == other.pos && targetID == other.targetID && patrolAnchor == other.patrolAnchor;
    }
    bool operator!=(const UnitOrder& other) const { return !(*this == other); }
};

/// Integer rules shared by the simulation and the tests.
namespace UnitOrderPolicy {

/// An attack-moving unit stops chasing once it is this many tiles from where it broke off.
constexpr int kAttackMoveLeashTiles = 8;
/// A walk counts as arrived this close to its goal, so a column ordered onto one tile completes.
constexpr int kArrivalRadiusTiles = 2;
/// Times a walk the engine dropped (no path) is resumed before the order is given up.
constexpr int kWalkRetries = 3;

/// Tiles between two map coordinates, counting diagonal steps as one.
inline int tileDistance(const Coord& a, const Coord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

/// Does an attack-moving unit stop for an enemy this far away? Same reach as doHuntToward.
inline bool attackMoveEngages(int enemyDistanceTiles, int weaponRange, int viewRange) {
    return enemyDistanceTiles <= std::max(weaponRange, viewRange);
}

/// Has a fight pulled an attack-moving unit too far off its walk?
inline bool attackMoveLeashBroken(const Coord& breakOff, const Coord& location) {
    return breakOff.isValid() && tileDistance(breakOff, location) > kAttackMoveLeashTiles;
}

enum class WalkProgress { Walking, Arrived, Resume, GiveUp };

/**
    Where a walking order stands. \a idle means the engine has no movement or target left for
    the unit (it reached its destination or dropped the route). Arrival is judged by distance
    to the goal, so a unit parked next to an occupied goal tile still completes.
*/
inline WalkProgress walkProgress(bool idle, const Coord& location, const Coord& goal, int retriesSoFar) {
    if(!idle) return WalkProgress::Walking;
    if(tileDistance(location, goal) <= kArrivalRadiusTiles) return WalkProgress::Arrived;
    return retriesSoFar >= kWalkRetries ? WalkProgress::GiveUp : WalkProgress::Resume;
}

enum class OutOfRange { Approach, Release };

/// A target outside weapon range: chase it, or let it go. HOLD never chases, and nobody
/// chases an aircraft on the ground.
inline OutOfRange outOfRangeResponse(ATTACKMODE mode, bool targetIsFlying) {
    return (mode == HOLD || targetIsFlying) ? OutOfRange::Release : OutOfRange::Approach;
}

/// May a damaged unit leave on its own for the repair yard? Not while holding.
inline bool mayAutoRepair(ATTACKMODE mode) {
    return mode != HOLD;
}

} // namespace UnitOrderPolicy

class UnitOrderQueue {
public:
    static constexpr std::size_t kMaxOrders = 16;

    bool empty() const { return orders.empty(); }
    std::size_t size() const { return orders.size(); }
    const UnitOrder& front() const { return orders.front(); }
    const UnitOrder& operator[](std::size_t i) const { return orders[i]; }
    std::vector<UnitOrder>::const_iterator begin() const { return orders.begin(); }
    std::vector<UnitOrder>::const_iterator end() const { return orders.end(); }

    void clear() { orders.clear(); }

    /// An order given without Shift: it replaces everything.
    void replace(const UnitOrder& order) { orders.assign(1, order); }

    /// A Shift-queued order. Refused (false) when the queue is full.
    bool append(const UnitOrder& order) {
        if(orders.size() >= kMaxOrders || order.type == UnitOrderType::Patrol) return false;
        orders.push_back(order);
        return true;
    }

    /**
        Patrol without Shift: a route between \a from (where the unit stands when the order
        executes) and \a point. Refused when both are the same tile.
    */
    bool startPatrol(const Coord& point, const Coord& from) {
        // A leg the unit would count as arrived at the moment it starts is no patrol.
        if(point.isInvalid() || from.isInvalid()
           || UnitOrderPolicy::tileDistance(point, from) <= UnitOrderPolicy::kArrivalRadiusTiles) return false;
        orders.clear();
        orders.push_back(UnitOrder::at(UnitOrderType::Patrol, point));
        UnitOrder anchor = UnitOrder::at(UnitOrderType::Patrol, from);
        anchor.patrolAnchor = true;
        orders.push_back(anchor);
        return true;
    }

    /**
        Patrol with Shift. An existing route gets \a point inserted before its closing
        anchor, so the cycle keeps the order the points were clicked in. Without a route,
        a new one starts from wherever the queue leaves the unit (\a from when nothing
        queued says otherwise). Refused when the queue has no room.
    */
    bool extendPatrol(const Coord& point, const Coord& from) {
        if(point.isInvalid()) return false;
        const auto anchor = std::find_if(orders.begin(), orders.end(),
                                         [](const UnitOrder& o) { return o.type == UnitOrderType::Patrol && o.patrolAnchor; });
        if(anchor != orders.end()) {
            if(orders.size() >= kMaxOrders) return false;
            const UnitOrder entry = UnitOrder::at(UnitOrderType::Patrol, point);
            if(anchor == orders.begin()) {
                // The unit is walking back to the anchor right now; the new point follows the
                // last point it reaches after that, which is the back of the cycle.
                orders.push_back(entry);
            } else {
                orders.insert(anchor, entry);
            }
            return true;
        }
        const Coord start = lastPosition(from);
        if(start.isInvalid() || UnitOrderPolicy::tileDistance(start, point) <= UnitOrderPolicy::kArrivalRadiusTiles
           || orders.size() + 2 > kMaxOrders) return false;
        orders.push_back(UnitOrder::at(UnitOrderType::Patrol, point));
        UnitOrder closing = UnitOrder::at(UnitOrderType::Patrol, start);
        closing.patrolAnchor = true;
        orders.push_back(closing);
        return true;
    }

    /// The front order is finished: a patrol point goes round to the back, anything else is dropped.
    void advance() {
        if(orders.empty()) return;
        const UnitOrder done = orders.front();
        orders.erase(orders.begin());
        if(done.type == UnitOrderType::Patrol) orders.push_back(done);
    }

    /// Drops front orders \a isValid rejects (a dead target, for instance); never touches a
    /// valid front. Returns how many were dropped.
    template<class Predicate>
    std::size_t skipInvalid(Predicate isValid) {
        std::size_t dropped = 0;
        while(!orders.empty() && !isValid(orders.front())) {
            orders.erase(orders.begin());
            ++dropped;
        }
        return dropped;
    }

    /// The last position the queued orders lead to, or \a fallback.
    Coord lastPosition(const Coord& fallback) const {
        for(auto it = orders.rbegin(); it != orders.rend(); ++it)
            if(it->usesPosition()) return it->pos;
        return fallback;
    }

    bool hasPatrol() const {
        return std::any_of(orders.begin(), orders.end(),
                           [](const UnitOrder& o) { return o.type == UnitOrderType::Patrol; });
    }

    void save(OutputStream& stream) const {
        stream.writeUint8(static_cast<Uint8>(orders.size()));
        for(const UnitOrder& order : orders) {
            stream.writeUint8(static_cast<Uint8>(order.type));
            stream.writeBool(order.patrolAnchor);
            stream.writeSint32(order.pos.x);
            stream.writeSint32(order.pos.y);
            stream.writeUint32(order.targetID);
        }
    }

    void load(InputStream& stream) {
        orders.clear();
        const Uint8 count = stream.readUint8();
        if(count > kMaxOrders) throw std::runtime_error("Unit order queue exceeds its limit");
        for(Uint8 i = 0; i < count; ++i) {
            UnitOrder order;
            const Uint8 type = stream.readUint8();
            if(type >= static_cast<Uint8>(UnitOrderType::Count)) throw std::runtime_error("Invalid unit order type");
            order.type = static_cast<UnitOrderType>(type);
            order.patrolAnchor = stream.readBool();
            order.pos.x = stream.readSint32();
            order.pos.y = stream.readSint32();
            order.targetID = stream.readUint32();
            orders.push_back(order);
        }
    }

    bool operator==(const UnitOrderQueue& other) const { return orders == other.orders; }

private:
    std::vector<UnitOrder> orders;
};

#endif // UNITORDERQUEUE_H
