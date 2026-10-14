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
#include <cstdint>
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

bool isSiege(Uint32 itemID) {
    return itemID == Unit_SiegeTank || itemID == Unit_EliteSiegeTank || itemID == Unit_ChemicalSiegeTank;
}

/// Who leads the armour: sonic units, then siege tanks, then the other heavy armour, then light vehicles.
int frontRank(Uint32 itemID) {
    if (isSonic(itemID)) return 0;
    if (isSiege(itemID)) return 1;
    return classOf(itemID) == UnitClass::Heavy ? 2 : 3;
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

/// A wanted destination in the formation's own frame: 'rank' steps along the facing
/// (larger is further forward) and 'column' steps across it.
struct Placed {
    int rank = 0;
    int column = 0;
};

/// One tile of a shape: a vehicle, or up to infantryPerTile infantry.
using Cell = std::vector<size_t>;

/// The pieces every shape other than the Box is built from.
class Layout {
public:
    Layout(const std::vector<Member>& members, std::vector<int> lateral, int perTile)
        : members_(members), lateral_(std::move(lateral)), perTile_(perTile) {}

    /// Member indices of one class, from one flank to the other as they stand now, so
    /// neighbours stay neighbours and paths do not cross - siege tanks first among the heavy
    /// armour, so they hold the front row when it cannot take all of it.
    std::vector<size_t> ofClass(UnitClass unitClass) const {
        std::vector<size_t> result;
        for (size_t i = 0; i < members_.size(); ++i)
            if (classOf(members_[i].itemID) == unitClass) result.push_back(i);
        std::sort(result.begin(), result.end(), [this](size_t a, size_t b) {
            const bool sa = isSiege(members_[a].itemID), sb = isSiege(members_[b].itemID);
            if (sa != sb) return sa;
            return lateralLess(a, b);
        });
        return result;
    }

    bool lateralLess(size_t a, size_t b) const {
        if (lateral_[a] != lateral_[b]) return lateral_[a] < lateral_[b];
        return members_[a].objectID < members_[b].objectID;
    }

    /// One cell per vehicle; consecutive infantry share a cell up to the tile capacity.
    std::vector<Cell> cells(const std::vector<size_t>& order) const {
        std::vector<Cell> result;
        for (const size_t i : order) {
            if (members_[i].infantry && !result.empty()) {
                Cell& last = result.back();
                if (members_[last.front()].infantry && static_cast<int>(last.size()) < perTile_) {
                    last.push_back(i);
                    continue;
                }
            }
            result.push_back({i});
        }
        return result;
    }

    bool sonicCell(const Cell& cell) const { return isSonic(members_[cell.front()].itemID); }
    bool protectedCell(const Cell& cell) const { return isProtected(members_[cell.front()].itemID); }

    /// Cuts cells into rows: the first row firstWidth wide, the others width wide.
    static void appendRows(std::vector<std::vector<Cell>>& rows, const std::vector<Cell>& cells,
                           int firstWidth, int width) {
        size_t next = 0;
        while (next < cells.size()) {
            const size_t take = static_cast<size_t>(std::max(1, rows.empty() ? firstWidth : width));
            rows.emplace_back(cells.begin() + next, cells.begin() + std::min(cells.size(), next + take));
            next += take;
        }
    }

    /**
        Puts the protected cells (MCVs) into the middle row, rows.size() / 2 - behind the front
        row whenever there are two rows or more - and returns the anchor row: the row that
        stands on the target. Without protected cells it is the middle row (rowCount - 1) / 2.
    */
    static int insertProtected(std::vector<std::vector<Cell>>& rows, const std::vector<Cell>& protectedCells) {
        if (protectedCells.empty()) return rows.empty() ? 0 : (static_cast<int>(rows.size()) - 1) / 2;
        if (rows.empty()) rows.emplace_back();
        const int anchor = static_cast<int>(rows.size()) / 2;
        rows[anchor].insert(rows[anchor].end(), protectedCells.begin(), protectedCells.end());
        return anchor;
    }

    /**
        Places the rows front to back with the anchor row on rank 0 (the target). Each row is
        centred on column 0: sonic cells take a contiguous block in the middle, the other cells
        the columns either side of it, all in the order the units stand in now. Protected cells
        take the very middle - splitting the sonic block if they share its row - with the
        middle one of them on column 0, so the MCV stands on the target with the group around
        it. Returns the front row's rank.
    */
    int placeRows(const std::vector<std::vector<Cell>>& rows, std::vector<Placed>& placed, int anchor) const {
        const int rowCount = static_cast<int>(rows.size());
        for (int r = 0; r < rowCount; ++r) placeRow(rows[r], anchor - r, placed);
        return anchor;
    }

private:
    void placeRow(const std::vector<Cell>& row, int rank, std::vector<Placed>& placed) const {
        std::vector<const Cell*> guarded, sonic, other;
        for (const Cell& cell : row) (protectedCell(cell) ? guarded : sonicCell(cell) ? sonic : other).push_back(&cell);
        auto byLateral = [this](const Cell* a, const Cell* b) { return lateralLess(a->front(), b->front()); };
        std::sort(guarded.begin(), guarded.end(), byLateral);
        std::sort(sonic.begin(), sonic.end(), byLateral);
        std::sort(other.begin(), other.end(), byLateral);
        const int count = static_cast<int>(row.size());
        const int sonicCount = static_cast<int>(sonic.size());
        int sonicLeft, otherLeft;
        if (guarded.empty()) {
            // The sonic block in the middle, the others either side of it.
            sonicLeft = 0;
            otherLeft = (count - sonicCount) / 2;
        } else {
            // The protected cells in the middle, half the row either side of them: the sonic
            // cells next to them (split as evenly as the sides allow), the others outside.
            const int sides = count - static_cast<int>(guarded.size());
            const int leftCount = sides / 2;
            sonicLeft = std::min(leftCount, std::max(sonicCount - (sides - leftCount), sonicCount / 2));
            otherLeft = leftCount - sonicLeft;
        }
        std::vector<const Cell*> sequence(other.begin(), other.begin() + otherLeft);
        sequence.insert(sequence.end(), sonic.begin(), sonic.begin() + sonicLeft);
        sequence.insert(sequence.end(), guarded.begin(), guarded.end());
        sequence.insert(sequence.end(), sonic.begin() + sonicLeft, sonic.end());
        sequence.insert(sequence.end(), other.begin() + otherLeft, other.end());
        const int centre = guarded.empty() ? (count - 1) / 2
            : otherLeft + sonicLeft + (static_cast<int>(guarded.size()) - 1) / 2;
        for (int j = 0; j < count; ++j)
            for (const size_t m : *sequence[j]) placed[m] = {rank, j - centre};
    }

    const std::vector<Member>& members_;
    std::vector<int> lateral_;
    int perTile_;
};

/**
    Scout columns: spread across the body's frontage [left, right], and never in a sonic
    lane [sonicLeft - 1, sonicRight + 1] when the front has sonic units (hasSonic). Distinct
    and ascending.
*/
std::vector<int> scoutColumns(int count, int left, int right, bool hasSonic, int sonicLeft, int sonicRight) {
    std::vector<int> columns;
    const int centre = left + (right - left) / 2;
    for (int i = 0; i < count; ++i) {
        if (count == 1) columns.push_back(centre);
        else if (right - left + 1 < count) columns.push_back(centre + 2 * i - (count - 1));   // a narrow front: both flanks
        else columns.push_back(left + (right - left) * i / (count - 1));
    }
    if (!hasSonic) return columns;
    // Out of the lanes: a scout over the sonic block moves to the nearer side of it, and
    // scouts on one side keep their order and distinct columns.
    std::vector<int> leftSide, rightSide;
    const int middle = sonicLeft + (sonicRight - sonicLeft) / 2;
    for (const int column : columns) {
        if (column < sonicLeft - 1 || (column <= sonicRight + 1 && column <= middle)) leftSide.push_back(column);
        else rightSide.push_back(column);
    }
    int previous = sonicLeft - 1;
    for (auto it = leftSide.rbegin(); it != leftSide.rend(); ++it) previous = *it = std::min(*it, previous - 1);
    previous = sonicRight + 1;
    for (int& column : rightSide) previous = column = std::max(column, previous + 1);
    leftSide.insert(leftSide.end(), rightSide.begin(), rightSide.end());
    return leftSide;
}

} // namespace

Role roleOf(Uint32 itemID) {
    switch (itemID) {
        // Siege tanks are armour: they hold the front row with the tanks.
        case Unit_Launcher:
        case Unit_EliteLauncher:
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

bool joinsFormations(Uint32 itemID) {
    // The harvester-like units (isHarvesterLikeUnit, units/HarvesterHelpers.h).
    return itemID != Unit_Harvester && itemID != Unit_RebelHarvester;
}

bool isProtected(Uint32 itemID) {
    return itemID == Unit_MCV;
}

Shape shapeFromIndex(int index) {
    return (index >= 0 && index < kShapeCount) ? static_cast<Shape>(index) : Shape::Box;
}

const char* shapeName(Shape shape) {
    switch (shape) {
        case Shape::Combined: return "Combined arms";
        case Shape::Line:     return "Line";
        case Shape::Column:   return "Column";
        case Shape::Box:
        default:              return "Box";
    }
}

bool isSonic(Uint32 itemID) {
    return itemID == Unit_SonicTank || itemID == Unit_SonicTrike;
}

bool mayStandInLane(Uint32 sonicItemID, Uint32 otherItemID) {
    return isSonic(sonicItemID) && otherItemID == sonicItemID;
}

UnitClass classOf(Uint32 itemID) {
    if (isSonic(itemID)) return UnitClass::Sonic;
    switch (itemID) {
        case Unit_Quad:
        case Unit_Trike:
        case Unit_RaiderTrike:
            return UnitClass::Light;
        default:
            break;
    }
    switch (roleOf(itemID)) {
        case Role::Ranged:   return UnitClass::Ranged;
        case Role::Infantry: return UnitClass::Infantry;
        case Role::Support:  return UnitClass::Support;
        case Role::Armour:
        default:             return UnitClass::Heavy;
    }
}

bool inSonicLane(const Coord& sonic, const Coord& other, const Coord& forward) {
    const int dx = other.x - sonic.x;
    const int dy = other.y - sonic.y;
    if (dx * forward.x + dy * forward.y <= 0) return false;
    // One column either side: the lateral step is (-forward.y, forward.x), whose squared
    // length is 1 straight and 2 diagonal.
    const int across = dy * forward.x - dx * forward.y;
    return std::abs(across) <= forward.x * forward.x + forward.y * forward.y;
}

int scoutCount(int memberCount, int lightCount) {
    const int wanted = memberCount < 8 ? 1 : (memberCount < 16 ? 2 : 3);
    return std::max(0, std::min(wanted, lightCount));
}

int scoutDistance(int memberCount) {
    return 4 + (memberCount >= 10 ? 1 : 0) + (memberCount >= 20 ? 1 : 0);
}

int columnWidth(int cellCount) {
    return cellCount <= 3 ? 1 : (cellCount <= 16 ? 2 : 3);
}

namespace {
const Coord kFacings[8] = {Coord(0, -1), Coord(1, -1), Coord(1, 0), Coord(1, 1),
                           Coord(0, 1), Coord(-1, 1), Coord(-1, 0), Coord(-1, -1)};
} // namespace

Uint8 facingCode(const Coord& forward) {
    for (Uint8 i = 0; i < 8; ++i)
        if (kFacings[i] == forward) return static_cast<Uint8>(i + 1);
    return 0;
}

Coord facingFromCode(Uint8 code) {
    return (code >= 1 && code <= 8) ? kFacings[code - 1] : Coord(0, 0);
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

namespace {

/**
    The Box: rows by role (armour, ranged, infantry), each role starting a new row except
    infantry, which first fill the free flank cells of the row in front of them. Sonic units
    lead the armour, all in the first row (widening it if needed), as a contiguous block in
    the middle of its armour cells. MCVs take the middle of the middle row, on the target
    (Layout::insertProtected), the rest of that row moving aside for them.
*/
std::vector<Placed> boxLayout(const std::vector<Member>& members, const std::vector<int>& lateral, int perTile) {
    const int count = static_cast<int>(members.size());
    int sonicCount = 0;
    for (const auto& member : members) sonicCount += isSonic(member.itemID) ? 1 : 0;
    const int width = std::max(boxWidth(count), sonicCount);

    // Members by role, sonic units first among the armour, and within that from one flank
    // to the other as they stand now, so neighbours stay neighbours and paths do not cross.
    std::vector<size_t> order, guarded;
    for (size_t i = 0; i < members.size(); ++i) (isProtected(members[i].itemID) ? guarded : order).push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (members[a].role != members[b].role) return members[a].role < members[b].role;
        // Within the armour: sonic units, siege tanks, the other heavy armour, light vehicles.
        if (frontRank(members[a].itemID) != frontRank(members[b].itemID)) return frontRank(members[a].itemID) < frontRank(members[b].itemID);
        if (lateral[a] != lateral[b]) return lateral[a] < lateral[b];
        return members[a].objectID < members[b].objectID;
    });

    // Rows of cells; a cell is one tile and holds one vehicle or up to perTile infantry.
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
    std::sort(guarded.begin(), guarded.end(), [&](size_t a, size_t b) {
        if (lateral[a] != lateral[b]) return lateral[a] < lateral[b];
        return members[a].objectID < members[b].objectID;
    });
    if (!guarded.empty() && rows.empty()) rows.emplace_back();
    const int anchor = guarded.empty() ? (static_cast<int>(rows.size()) - 1) / 2 : static_cast<int>(rows.size()) / 2;

    // Box columns from the centre outwards: a short row stands in the middle.
    std::vector<int> centreOut;
    for (int c = 0; c < width; ++c) centreOut.push_back(c - (width - 1) / 2);
    std::stable_sort(centreOut.begin(), centreOut.end(), [](int a, int b) { return std::abs(a) < std::abs(b); });

    std::vector<Placed> placed(members.size());
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
        // Sonic cells (the first ones of the front row) take the middle of the leading
        // columns; the other leading cells keep their order on the columns either side.
        int sonicCells = 0;
        while (sonicCells < leading && isSonic(members[row[sonicCells].front()].itemID)) ++sonicCells;
        std::vector<int> leadingColumns;
        const int firstSonic = (leading - sonicCells) / 2;
        for (int j = firstSonic; j < firstSonic + sonicCells; ++j) leadingColumns.push_back(centre[j]);
        for (int j = 0; j < leading; ++j)
            if (j < firstSonic || j >= firstSonic + sonicCells) leadingColumns.push_back(centre[j]);
        // The anchor row stands on the target, the rows before it ahead, the others behind.
        const int ahead = anchor - r;
        for (int i = 0; i < cells; ++i) {
            const int column = i < leading ? leadingColumns[i] : flanks[i - leading];
            for (const size_t m : row[i]) placed[m] = {ahead, column};
        }
    }
    // MCVs in the very middle of the anchor row: its cells left of column 0 move left and the
    // others right, just far enough to free the columns the MCVs take.
    const int guardedCount = static_cast<int>(guarded.size());
    if (guardedCount > 0) {
        const int lowest = -(guardedCount - 1) / 2;
        for (const Cell& cell : rows[anchor])
            for (const size_t m : cell)
                placed[m].column += placed[m].column < 0 ? lowest : lowest + guardedCount;
        for (int k = 0; k < guardedCount; ++k) placed[guarded[k]] = {0, lowest + k};
    }
    return placed;
}

/**
    Combined arms: sonic units, tanks and siege tanks in the front row (further armour in rows
    behind it), ranged units behind them, then infantry at the rear; MCVs in the middle of
    the middle row, on the target; light vehicles on both flanks of the fighting rows, and
    the fastest of them as scouts ahead of the front.
*/
std::vector<Placed> combinedLayout(const std::vector<Member>& members, const Layout& layout,
                                   std::vector<Uint32>& scouts) {
    const int count = static_cast<int>(members.size());
    std::vector<size_t> lights = layout.ofClass(UnitClass::Light);

    // Scouts: the fastest light vehicles, ties by object id.
    std::vector<size_t> fastest = lights;
    std::sort(fastest.begin(), fastest.end(), [&](size_t a, size_t b) {
        if (members[a].speed != members[b].speed) return members[a].speed > members[b].speed;
        return members[a].objectID < members[b].objectID;
    });
    fastest.resize(static_cast<size_t>(scoutCount(count, static_cast<int>(lights.size()))));
    std::vector<size_t> scouting = fastest;
    std::sort(scouting.begin(), scouting.end(), [&](size_t a, size_t b) { return layout.lateralLess(a, b); });
    std::vector<size_t> flankers;
    for (const size_t i : lights)
        if (std::find(scouting.begin(), scouting.end(), i) == scouting.end()) flankers.push_back(i);

    std::vector<size_t> front = layout.ofClass(UnitClass::Sonic);
    const int sonicCount = static_cast<int>(front.size());
    for (const size_t i : layout.ofClass(UnitClass::Heavy)) front.push_back(i);
    const std::vector<size_t> ranged = layout.ofClass(UnitClass::Ranged);
    // Without armour or ranged units the light vehicles are the front themselves.
    if (front.empty() && ranged.empty()) std::swap(front, flankers);

    const auto frontCells = layout.cells(front);
    const auto rangedCells = layout.cells(ranged);
    const auto infantryCells = layout.cells(layout.ofClass(UnitClass::Infantry));
    const auto guardedCells = layout.cells(layout.ofClass(UnitClass::Support));
    const int bodyCells = static_cast<int>(frontCells.size() + rangedCells.size() + infantryCells.size() + guardedCells.size());
    const int width = std::max(boxWidth(bodyCells), sonicCount);
    std::vector<std::vector<Cell>> rows;
    Layout::appendRows(rows, frontCells, width, width);
    Layout::appendRows(rows, rangedCells, width, width);
    const int fightingRows = std::max<int>(1, static_cast<int>(rows.size()));
    Layout::appendRows(rows, infantryCells, width, width);
    const int anchor = Layout::insertProtected(rows, guardedCells);

    std::vector<Placed> placed(members.size());
    const int frontRank = layout.placeRows(rows, placed, anchor);

    // The fighting rows' extent, and the sonic block in the front row.
    int left = 0, right = 0, sonicLeft = 0, sonicRight = 0;
    bool any = false, hasSonic = false;
    for (int r = 0; r < fightingRows && r < static_cast<int>(rows.size()); ++r)
        for (const Cell& cell : rows[r])
            for (const size_t m : cell) {
                const int column = placed[m].column;
                left = any ? std::min(left, column) : column;
                right = any ? std::max(right, column) : column;
                any = true;
                if (isSonic(members[m].itemID)) {
                    sonicLeft = hasSonic ? std::min(sonicLeft, column) : column;
                    sonicRight = hasSonic ? std::max(sonicRight, column) : column;
                    hasSonic = true;
                }
            }

    // Flanks: the left half (as they stand now) beside the fighting rows on the left, the
    // rest on the right; front row first, then outwards in further flank columns.
    const size_t leftCount = (flankers.size() + 1) / 2;
    for (size_t i = 0; i < flankers.size(); ++i) {
        const bool onLeft = i < leftCount;
        // Inner columns first: the left flank counted from its right-hand end.
        const int k = onLeft ? static_cast<int>(leftCount - 1 - i) : static_cast<int>(i - leftCount);
        const int row = k % fightingRows;
        const int step = 1 + k / fightingRows;
        placed[flankers[i]] = {frontRank - row, onLeft ? left - step : right + step};
    }

    // Scouts ahead of the front, spread across it and clear of every sonic lane.
    const auto columns = scoutColumns(static_cast<int>(scouting.size()), left, right, hasSonic, sonicLeft, sonicRight);
    for (size_t i = 0; i < scouting.size(); ++i) {
        placed[scouting[i]] = {frontRank + scoutDistance(count), columns[i]};
        scouts.push_back(members[scouting[i]].objectID);
    }
    std::sort(scouts.begin(), scouts.end());
    return placed;
}

/**
    Line and Column: every cell in the order sonic, heavy, light, ranged, infantry, cut into
    rows of 'width' (the front row at least as wide as the sonic units); MCVs in the middle
    of the middle row, on the target.
*/
std::vector<Placed> rowsLayout(const std::vector<Member>& members, const Layout& layout, bool line) {
    std::vector<size_t> order;
    for (const UnitClass unitClass : {UnitClass::Sonic, UnitClass::Heavy, UnitClass::Light,
                                      UnitClass::Ranged, UnitClass::Infantry}) {
        const auto ofClass = layout.ofClass(unitClass);
        order.insert(order.end(), ofClass.begin(), ofClass.end());
    }
    const auto cells = layout.cells(order);
    const auto guardedCells = layout.cells(layout.ofClass(UnitClass::Support));
    int sonicCells = 0;
    for (const Cell& cell : cells) sonicCells += layout.sonicCell(cell) ? 1 : 0;
    const int cellCount = static_cast<int>(cells.size() + guardedCells.size());
    const int width = line ? std::min(cellCount, kLineMaxWidth) : columnWidth(cellCount);
    std::vector<std::vector<Cell>> rows;
    Layout::appendRows(rows, cells, std::max(width, sonicCells), width);
    const int anchor = Layout::insertProtected(rows, guardedCells);
    std::vector<Placed> placed(members.size());
    layout.placeRows(rows, placed, anchor);
    return placed;
}

/// Does any wanted tile stand in a sonic member's lane that it may not stand in?
bool blocksSonicLane(const std::vector<Member>& members, const std::vector<Coord>& wanted, const Coord& forward) {
    for (size_t s = 0; s < members.size(); ++s) {
        if (!isSonic(members[s].itemID)) continue;
        for (size_t i = 0; i < members.size(); ++i)
            if (i != s && inSonicLane(wanted[s], wanted[i], forward)
                && !mayStandInLane(members[s].itemID, members[i].itemID)) return true;
    }
    return false;
}

/**
    Resolves wanted tiles into usable, unique ones: sonic units first, then MCVs, then armour,
    so the front and the protected centre keep their tiles when two wanted tiles collide. A
    relocated tile never puts a unit in a sonic lane unless it mayStandInLane (a sonic unit
    of the same type). Indexed like 'members'; independent of their order.
*/
std::vector<Coord> resolveTiles(const std::vector<Member>& members, const std::vector<Coord>& wanted, const Coord& forward,
                                int mapWidth, int mapHeight, const CanStand& canStand, int infantryPerTile) {
    std::vector<size_t> resolveOrder(members.size());
    for (size_t i = 0; i < resolveOrder.size(); ++i) resolveOrder[i] = i;
    std::sort(resolveOrder.begin(), resolveOrder.end(), [&](size_t a, size_t b) {
        const bool sa = isSonic(members[a].itemID), sb = isSonic(members[b].itemID);
        if (sa != sb) return sa;
        const bool pa = isProtected(members[a].itemID), pb = isProtected(members[b].itemID);
        if (pa != pb) return pa;
        return byRoleThenID(members[a], members[b]);
    });
    Occupancy occupancy(infantryPerTile);
    std::vector<std::pair<Coord, Uint32>> sonicTiles;     // tile and item of every placed sonic unit
    std::vector<Coord> assigned(members.size(), Coord::Invalid());
    for (const size_t i : resolveOrder) {
        const Member& member = members[i];
        const bool sonic = isSonic(member.itemID);
        // Sonic units are resolved first, so a sonic unit only meets sonic units here.
        auto clearOfLanes = [&](const Coord& candidate) {
            for (const auto& [tile, item] : sonicTiles) {
                if (inSonicLane(tile, candidate, forward) && !mayStandInLane(item, member.itemID)) return false;
                if (sonic && inSonicLane(candidate, tile, forward) && !mayStandInLane(member.itemID, item)) return false;
            }
            return true;
        };
        Coord tile = searchRings(wanted[i], mapWidth, mapHeight, [&](const Coord& candidate) {
            return occupancy.admits(member, candidate) && clearOfLanes(candidate) && canStand(member, candidate);
        });
        // Nothing usable within reach: stay put rather than invent a destination.
        if (tile.isInvalid()) tile = member.tile;
        occupancy.take(member, tile);
        assigned[i] = tile;
        if (sonic) sonicTiles.emplace_back(tile, member.itemID);
    }
    return assigned;
}

} // namespace

Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile, Shape shape) {
    Plan result;
    // Harvesters keep their ordinary orders: they never get a slot.
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [](const Member& member) { return !joinsFormations(member.itemID); }),
                  members.end());
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

    // Every shape faces the direction of travel: rows across it, the front ahead.
    Coord forward = facing(centroid, target);
    if (forward.x == 0 && forward.y == 0) forward = Coord(0, -1);
    const Coord lateral(-forward.y, forward.x);
    result.forward = forward;
    const int perTile = std::max(1, infantryPerTile);
    std::vector<int> lateralOf(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        lateralOf[i] = (members[i].tile.x - centroid.x) * lateral.x + (members[i].tile.y - centroid.y) * lateral.y;

    // Wanted destination for each member (indexed like 'members', which is sorted by id).
    std::vector<Coord> wanted(members.size());
    bool laidOut = false;

    bool anyProtected = false;
    for (const auto& member : members) anyProtected = anyProtected || isProtected(member.itemID);
    if (shape == Shape::Box && !anyProtected) {
        bool coherent = true;
        for (const auto& member : members)
            if (tileDistance(member.tile, centroid) > kCoherentRadius) { coherent = false; break; }
        if (coherent) {
            // Keep the shape, translated onto the target and with the gaps closed up -
            // unless that leaves someone in front of a sonic unit; then it forms the box.
            // A group with an MCV always forms the box, around the MCV.
            std::vector<int> xs, ys;
            for (const auto& member : members) {
                xs.push_back(member.tile.x - centroid.x);
                ys.push_back(member.tile.y - centroid.y);
            }
            const auto cx = compressAxis(xs);
            const auto cy = compressAxis(ys);
            for (size_t i = 0; i < members.size(); ++i) wanted[i] = Coord(target.x + cx[i], target.y + cy[i]);
            laidOut = !blocksSonicLane(members, wanted, forward);
        }
    }

    if (!laidOut) {
        std::vector<Placed> placed;
        const Layout layout(members, lateralOf, perTile);
        switch (shape) {
            case Shape::Combined: placed = combinedLayout(members, layout, result.scouts); break;
            case Shape::Line:     placed = rowsLayout(members, layout, true); break;
            case Shape::Column:   placed = rowsLayout(members, layout, false); break;
            case Shape::Box:
            default:              placed = boxLayout(members, lateralOf, perTile); break;
        }
        for (size_t i = 0; i < members.size(); ++i)
            wanted[i] = Coord(target.x + forward.x * placed[i].rank + lateral.x * placed[i].column,
                              target.y + forward.y * placed[i].rank + lateral.y * placed[i].column);
    }

    const auto assigned = resolveTiles(members, wanted, forward, mapWidth, mapHeight, canStand, infantryPerTile);
    for (size_t i = 0; i < members.size(); ++i) result.slots.push_back({members[i].objectID, assigned[i]});
    return result;
}

FixPoint groupPace(const std::vector<Member>& members) {
    FixPoint pace = 0;
    for (size_t i = 0; i < members.size(); ++i)
        if (i == 0 || members[i].speed < pace) pace = members[i].speed;
    return pace;
}

std::vector<Coord> formUpTiles(const std::vector<Member>& members, const std::vector<Coord>& slots, const Coord& forward,
                               int mapWidth, int mapHeight, const CanStand& canStand, int infantryPerTile) {
    const int count = static_cast<int>(members.size());
    if (count < 2 || slots.size() != members.size() || (forward.x == 0 && forward.y == 0)) return {};
    int tileX = 0, tileY = 0, slotX = 0, slotY = 0;
    for (int i = 0; i < count; ++i) {
        tileX += members[i].tile.x; tileY += members[i].tile.y;
        slotX += slots[i].x; slotY += slots[i].y;
    }
    // The shape centred on the group as it stands...
    Coord shift(floorDiv(slotX, count) - floorDiv(tileX, count), floorDiv(slotY, count) - floorDiv(tileY, count));
    auto along = [&forward](const Coord& tile) { return tile.x * forward.x + tile.y * forward.y; };
    const int step = forward.x * forward.x + forward.y * forward.y;
    // ...moved forward until its foremost slot is level with the foremost member, so nobody
    // walks back to form up - but never past the target.
    int foremostMember = INT32_MIN, foremostSlot = INT32_MIN;
    for (int i = 0; i < count; ++i) {
        foremostMember = std::max(foremostMember, along(members[i].tile));
        foremostSlot = std::max(foremostSlot, along(slots[i] - shift));
    }
    const int ahead = std::min(std::max(0, foremostMember - foremostSlot), std::max(0, along(shift))) / step;
    shift = Coord(shift.x - forward.x * ahead, shift.y - forward.y * ahead);
    // A short move needs no form-up: the members go straight to their slots.
    if (tileDistance(shift, Coord(0, 0)) < kFormUpMinMarch) return {};
    std::vector<Coord> wanted;
    for (int i = 0; i < count; ++i) wanted.push_back(slots[i] - shift);
    return resolveTiles(members, wanted, forward, mapWidth, mapHeight, canStand, infantryPerTile);
}

March marchState(int behindLeader, int aheadOfLast) {
    if (aheadOfLast > kHoldLeadTiles) return March::Hold;
    if (behindLeader > kRecoverLagTiles) return March::Recover;
    return March::Pace;
}

} // namespace FormationPlanner
