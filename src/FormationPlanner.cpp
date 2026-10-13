/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <FormationPlanner.h>

#include <data.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>

namespace FormationPlanner {

namespace {

Sint64 tileKey(const Coord& tile) {
    return (static_cast<Sint64>(tile.y) << 32) ^ static_cast<Uint32>(tile.x);
}

bool insideMap(const Coord& tile, int mapWidth, int mapHeight) {
    return tile.x >= 0 && tile.y >= 0 && tile.x < mapWidth && tile.y < mapHeight;
}

int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

/**
    Shrinks the gaps between the distinct values of one axis to at most
    kMaxCompressedSpacing, keeping their order, and returns the new value for each input.
*/
std::vector<int> compressAxis(const std::vector<int>& values) {
    std::vector<int> distinct = values;
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    std::map<int, int> mapped;
    int position = 0;
    for (size_t i = 0; i < distinct.size(); ++i) {
        if (i > 0) position += std::min(distinct[i] - distinct[i - 1], kMaxCompressedSpacing);
        mapped[distinct[i]] = position;
    }
    std::vector<int> result;
    result.reserve(values.size());
    int sum = 0;
    for (const int value : values) {
        result.push_back(mapped[value]);
        sum += mapped[value];
    }
    // Recentre so the group's centre of mass stays on the target.
    const int shift = floorDiv(sum, static_cast<int>(values.size()));
    for (int& value : result) value -= shift;
    return result;
}

/// Occupancy of the destination tiles handed out so far.
class Occupancy {
public:
    explicit Occupancy(int infantryPerTile) : infantryPerTile_(std::max(1, infantryPerTile)) {}

    bool admits(const Member& member, const Coord& tile) const {
        const auto it = used_.find(tileKey(tile));
        if (it == used_.end()) return true;
        // A vehicle needs the tile to itself; infantry share with infantry up to capacity.
        return member.infantry && it->second > 0 && it->second < infantryPerTile_;
    }

    void take(const Member& member, const Coord& tile) {
        int& count = used_[tileKey(tile)];
        count = member.infantry ? count + 1 : -1;
    }

private:
    int infantryPerTile_;
    std::map<Sint64, int> used_;    ///< >0: infantry count, -1: a vehicle
};

/// The ring search shared by nearestStandable() and the slot resolution.
template<typename Accept>
Coord searchRings(const Coord& wanted, int mapWidth, int mapHeight, Accept accept) {
    if (insideMap(wanted, mapWidth, mapHeight) && accept(wanted)) return wanted;
    for (int radius = 1; radius <= kMaxSearchRadius; ++radius) {
        Coord best = Coord::Invalid();
        int bestDistance = 0;
        // Ring order: top edge left to right, right edge down, bottom edge right to left,
        // left edge up. Within a ring the Euclidean-nearest tile wins, ties in ring order.
        auto consider = [&](int dx, int dy) {
            const Coord tile(wanted.x + dx, wanted.y + dy);
            if (!insideMap(tile, mapWidth, mapHeight)) return;
            const int distance = dx * dx + dy * dy;
            if (best.isValid() && distance >= bestDistance) return;
            if (!accept(tile)) return;
            best = tile;
            bestDistance = distance;
        };
        for (int dx = -radius; dx <= radius; ++dx) consider(dx, -radius);
        for (int dy = -radius + 1; dy <= radius; ++dy) consider(radius, dy);
        for (int dx = radius - 1; dx >= -radius; --dx) consider(dx, radius);
        for (int dy = radius - 1; dy >= -radius + 1; --dy) consider(-radius, dy);
        if (best.isValid()) return best;
    }
    return Coord::Invalid();
}

bool byRoleThenID(const Member& a, const Member& b) {
    if (a.role != b.role) return a.role < b.role;
    return a.objectID < b.objectID;
}

} // namespace

Role roleOf(Uint32 itemID) {
    switch (itemID) {
        case Unit_Launcher:
        case Unit_EliteLauncher:
        case Unit_SiegeTank:
        case Unit_EliteSiegeTank:
        case Unit_ChemicalSiegeTank:
        case Unit_Deviator:
        case Unit_RocketTrike:
            return Role::Ranged;
        case Unit_Harvester:
        case Unit_RebelHarvester:
        case Unit_MCV:
            return Role::Support;
        default:
            return isInfantryUnit(static_cast<int>(itemID)) ? Role::Infantry : Role::Armour;
    }
}

Coord Plan::slotFor(Uint32 objectID) const {
    const auto it = std::lower_bound(slots.begin(), slots.end(), objectID,
        [](const Slot& slot, Uint32 id) { return slot.objectID < id; });
    return (it != slots.end() && it->objectID == objectID) ? it->tile : Coord::Invalid();
}

int tileDistance(const Coord& a, const Coord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

int boxWidth(int memberCount) {
    // Smallest width with width^2 >= 1.5 * n, i.e. 2 * width^2 >= 3 * n.
    int width = 1;
    while (2 * width * width < 3 * memberCount) ++width;
    return width;
}

Coord facing(const Coord& from, const Coord& to) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    if (dx == 0 && dy == 0) return Coord(0, 0);
    // Eight sectors: a component counts when it is at least ~41% (tan 22.5 deg ~ 5/12)
    // of the larger one.
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    const int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    if (ax * 12 < ay * 5) return Coord(0, sy);
    if (ay * 12 < ax * 5) return Coord(sx, 0);
    return Coord(sx, sy);
}

Coord nearestStandable(const Member& member, const Coord& wanted, int mapWidth, int mapHeight,
                       const CanStand& canStand) {
    return searchRings(wanted, mapWidth, mapHeight,
                       [&](const Coord& tile) { return canStand(member, tile); });
}

Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile) {
    Plan result;
    std::sort(members.begin(), members.end(),
              [](const Member& a, const Member& b) { return a.objectID < b.objectID; });
    if (members.empty()) return result;

    result.groupSpeed = groupPace(members);

    const int count = static_cast<int>(members.size());
    if (count < 2) {
        for (const auto& member : members) result.slots.push_back({member.objectID, target});
        return result;
    }
    result.formation = true;

    int sumX = 0, sumY = 0;
    for (const auto& member : members) { sumX += member.tile.x; sumY += member.tile.y; }
    const Coord centroid(floorDiv(sumX, count), floorDiv(sumY, count));

    bool coherent = true;
    for (const auto& member : members)
        if (tileDistance(member.tile, centroid) > kCoherentRadius) { coherent = false; break; }

    // Wanted destination for each member (indexed like 'members', which is sorted by id).
    std::vector<Coord> wanted(members.size());

    if (coherent) {
        // Keep the shape, translated onto the target and with the gaps closed up.
        std::vector<int> xs, ys;
        for (const auto& member : members) {
            xs.push_back(member.tile.x - centroid.x);
            ys.push_back(member.tile.y - centroid.y);
        }
        const auto cx = compressAxis(xs);
        const auto cy = compressAxis(ys);
        for (size_t i = 0; i < members.size(); ++i) wanted[i] = Coord(target.x + cx[i], target.y + cy[i]);
    } else {
        // A box facing the direction of travel: rows across it, armour in front.
        Coord forward = facing(centroid, target);
        if (forward.x == 0 && forward.y == 0) forward = Coord(0, -1);
        const Coord lateral(-forward.y, forward.x);
        const int width = boxWidth(count);
        const int perTile = std::max(1, infantryPerTile);

        // Members by role, and within a role from one flank to the other as they stand now,
        // so neighbours stay neighbours and paths do not cross.
        std::vector<size_t> order(members.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        auto lateralOf = [&](size_t i) {
            return (members[i].tile.x - centroid.x) * lateral.x + (members[i].tile.y - centroid.y) * lateral.y;
        };
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (members[a].role != members[b].role) return members[a].role < members[b].role;
            const int la = lateralOf(a), lb = lateralOf(b);
            if (la != lb) return la < lb;
            return members[a].objectID < members[b].objectID;
        });

        // Rows of cells; a cell is one tile and holds one vehicle or up to perTile infantry.
        using Cell = std::vector<size_t>;
        std::vector<std::vector<Cell>> rows;
        size_t next = 0;
        while (next < order.size()) {
            const Role role = members[order[next]].role;
            const bool infantry = role == Role::Infantry;
            // Infantry first take the free flank cells of the row in front of them.
            if (!infantry || rows.empty() || static_cast<int>(rows.back().size()) >= width)
                rows.emplace_back();
            while (next < order.size() && members[order[next]].role == role) {
                if (static_cast<int>(rows.back().size()) >= width) rows.emplace_back();
                Cell cell;
                const int capacity = (infantry && members[order[next]].infantry) ? perTile : 1;
                while (next < order.size() && members[order[next]].role == role
                       && static_cast<int>(cell.size()) < capacity
                       && (cell.empty() || members[order[next]].infantry)) {
                    cell.push_back(order[next++]);
                }
                rows.back().push_back(std::move(cell));
            }
        }

        // Box columns from the centre outwards: a short row stands in the middle.
        std::vector<int> centreOut;
        for (int c = 0; c < width; ++c) centreOut.push_back(c - (width - 1) / 2);
        std::stable_sort(centreOut.begin(), centreOut.end(), [](int a, int b) { return std::abs(a) < std::abs(b); });

        const int rowCount = static_cast<int>(rows.size());
        for (int r = 0; r < rowCount; ++r) {
            const auto& row = rows[r];
            const int cells = static_cast<int>(row.size());
            // A row is filled with its own role first and then, possibly, infantry on its
            // flanks: the leading cells take the central columns, the infantry the outer
            // ones, each left to right in the order they were filled.
            int leading = 0;
            while (leading < cells && members[row[leading].front()].role != Role::Infantry) ++leading;
            if (leading == 0) leading = cells;
            std::vector<int> centre(centreOut.begin(), centreOut.begin() + leading);
            std::vector<int> flanks(centreOut.begin() + leading, centreOut.begin() + cells);
            std::sort(centre.begin(), centre.end());
            std::sort(flanks.begin(), flanks.end());
            // The first row stands ahead of the target, the last behind it.
            const int ahead = (rowCount - 1) / 2 - r;
            for (int i = 0; i < cells; ++i) {
                const int column = i < leading ? centre[i] : flanks[i - leading];
                const Coord tile(target.x + forward.x * ahead + lateral.x * column,
                                 target.y + forward.y * ahead + lateral.y * column);
                for (const size_t m : row[i]) wanted[m] = tile;
            }
        }
    }

    // Resolve: armour first, so the front keeps its tiles when two wanted tiles collide.
    std::vector<size_t> resolveOrder(members.size());
    for (size_t i = 0; i < resolveOrder.size(); ++i) resolveOrder[i] = i;
    std::sort(resolveOrder.begin(), resolveOrder.end(),
              [&](size_t a, size_t b) { return byRoleThenID(members[a], members[b]); });
    Occupancy occupancy(infantryPerTile);
    std::vector<Coord> assigned(members.size(), Coord::Invalid());
    for (const size_t i : resolveOrder) {
        const Member& member = members[i];
        Coord tile = searchRings(wanted[i], mapWidth, mapHeight, [&](const Coord& candidate) {
            return occupancy.admits(member, candidate) && canStand(member, candidate);
        });
        // Nothing usable within reach: stay put rather than invent a destination.
        if (tile.isInvalid()) tile = member.tile;
        occupancy.take(member, tile);
        assigned[i] = tile;
    }

    for (size_t i = 0; i < members.size(); ++i) result.slots.push_back({members[i].objectID, assigned[i]});
    return result;
}

FixPoint groupPace(const std::vector<Member>& members) {
    bool anyVehicle = false;
    for (const auto& member : members) anyVehicle = anyVehicle || !member.infantry;
    bool found = false;
    FixPoint pace = 0;
    for (const auto& member : members) {
        if (anyVehicle && member.infantry) continue;
        if (!found || member.speed < pace) pace = member.speed;
        found = true;
    }
    return pace;
}

bool runsAtFullSpeed(int lagTiles, int centroidDistance) {
    return lagTiles > kCatchUpLagTiles || centroidDistance > kCatchUpCentroidTiles;
}

} // namespace FormationPlanner
