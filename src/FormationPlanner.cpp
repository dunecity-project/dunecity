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

Shape shapeFromId(int id) {
    for (const Shape shape : kShapes)
        if (static_cast<int>(shape) == id) return shape;
    return Shape::Box;
}

Shape shapeAt(int position) {
    return (position >= 0 && position < kShapeCount) ? kShapes[position] : Shape::Box;
}

int positionOf(Shape shape) {
    for (int position = 0; position < kShapeCount; ++position)
        if (kShapes[position] == shape) return position;
    return 0;
}

const char* shapeName(Shape shape) {
    switch (shape) {
        case Shape::Column:    return "Column";
        case Shape::Echelon:   return "Echelon";
        case Shape::Staggered: return "Staggered";
        case Shape::Pincer:    return "Pincer";
        case Shape::Box:
        default:               return "Box";
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

/// The order the row shapes take their units in: sonic, heavy (siege tanks first), light, ranged, infantry.
std::vector<size_t> fightingOrder(const Layout& layout) {
    std::vector<size_t> order;
    for (const UnitClass unitClass : {UnitClass::Sonic, UnitClass::Heavy, UnitClass::Light,
                                      UnitClass::Ranged, UnitClass::Infantry}) {
        const auto ofClass = layout.ofClass(unitClass);
        order.insert(order.end(), ofClass.begin(), ofClass.end());
    }
    return order;
}

/// Cells abreast of the row shapes for a group of cellCount cells (MCVs included).
int rowWidth(Shape shape, int cellCount) {
    switch (shape) {
        case Shape::Column:  return columnWidth(cellCount);
        case Shape::Echelon: return std::min(cellCount, kEchelonMaxWidth);
        case Shape::Pincer:  return std::min(cellCount, 2 * boxWidth((cellCount + 1) / 2));
        default:             return boxWidth(cellCount);
    }
}

/**
    Column, Staggered and Pincer (and a one-row Echelon): every cell in fightingOrder(), cut
    into rows of rowWidth() (the front row at least as wide as the sonic units); MCVs in the
    middle of the middle row, on the target. 'width' receives the width.
*/
std::vector<Placed> rowsLayout(const std::vector<Member>& members, const Layout& layout, Shape shape, int* width = nullptr) {
    const auto cells = layout.cells(fightingOrder(layout));
    const auto guardedCells = layout.cells(layout.ofClass(UnitClass::Support));
    int sonicCells = 0;
    for (const Cell& cell : cells) sonicCells += layout.sonicCell(cell) ? 1 : 0;
    const int abreast = rowWidth(shape, static_cast<int>(cells.size() + guardedCells.size()));
    if (width) *width = abreast;
    std::vector<std::vector<Cell>> rows;
    Layout::appendRows(rows, cells, std::max(abreast, sonicCells), abreast);
    const int anchor = Layout::insertProtected(rows, guardedCells);
    std::vector<Placed> placed(members.size());
    layout.placeRows(rows, placed, anchor);
    return placed;
}

/**
    The Echelon's rank offset of a column, relative to the flat block [blockLeft, blockRight]
    in the middle of the front row (the sonic units and column 0): the left flank leads and
    the right flank trails, one rank every two columns. It is never positive within one column
    of the block, so nothing stands in a sonic lane.
*/
int echelonOffset(int column, int blockLeft, int blockRight) {
    const int left = blockLeft - column, right = column - blockRight;
    return left > 0 ? left / 2 : -((std::max(0, right) + 1) / 2);
}

/// A destination in the Echelon.
struct Spot {
    int rank = 0;
    int column = 0;
};

/// The groups of cells the Echelon hands its spots to, in turn.
struct Batches {
    std::vector<Cell> sonic, guarded;
    std::vector<std::vector<Cell>> fighting;   ///< siege tanks, other heavy armour, light, ranged, infantry
    int total = 0;
};

Batches batchesOf(const std::vector<Member>& members, const Layout& layout) {
    Batches batches;
    batches.sonic = layout.cells(layout.ofClass(UnitClass::Sonic));
    batches.guarded = layout.cells(layout.ofClass(UnitClass::Support));
    std::vector<size_t> siege, heavy;
    for (const size_t i : layout.ofClass(UnitClass::Heavy)) (isSiege(members[i].itemID) ? siege : heavy).push_back(i);
    for (const auto& group : {siege, heavy, layout.ofClass(UnitClass::Light), layout.ofClass(UnitClass::Ranged),
                              layout.ofClass(UnitClass::Infantry)})
        if (!group.empty()) batches.fighting.push_back(layout.cells(group));
    batches.total = static_cast<int>(batches.sonic.size() + batches.guarded.size());
    for (const auto& batch : batches.fighting) batches.total += static_cast<int>(batch.size());
    return batches;
}

/**
    Hands 'cells' (in their flank-to-flank order) the first cells.size() of 'pool', removing
    them from it; among the spots taken the left-most goes to the left-most cell, so
    neighbours stay neighbours.
*/
void handOut(const std::vector<Cell>& cells, std::vector<Spot>& pool, std::vector<Placed>& placed) {
    const size_t take = std::min(cells.size(), pool.size());
    std::vector<Spot> taken(pool.begin(), pool.begin() + take);
    pool.erase(pool.begin(), pool.begin() + take);
    std::sort(taken.begin(), taken.end(), [](const Spot& a, const Spot& b) {
        return a.column != b.column ? a.column < b.column : a.rank > b.rank;
    });
    for (size_t k = 0; k < take; ++k)
        for (const size_t m : cells[k]) placed[m] = {taken[k].rank, taken[k].column};
}

/// Front first, then the middle of the shape first, then left to right.
bool frontFirst(const Spot& a, const Spot& b) {
    if (a.rank != b.rank) return a.rank > b.rank;
    if (std::abs(a.column) != std::abs(b.column)) return std::abs(a.column) < std::abs(b.column);
    return a.column < b.column;
}

/// Columns of a row of 'width' cells centred on column 0, as Layout::placeRow lays them out.
int rowColumn(int index, int width) { return index - (width - 1) / 2; }

/**
    Echelon: rows up to kEchelonMaxWidth wide, each column moved forward or back by
    echelonOffset(). Sonic units take the middle of the front row as one block and MCVs the
    middle of the middle row (on the target); the other spots go front first to siege tanks,
    then the other heavy armour, light vehicles, ranged units and infantry, so the leading edge
    of the slant is armour.
*/
std::vector<Placed> echelonLayout(const std::vector<Member>& members, const Layout& layout) {
    const Batches batches = batchesOf(members, layout);
    const int sonicCount = static_cast<int>(batches.sonic.size());
    const int width = rowWidth(Shape::Echelon, batches.total);
    const int frontWidth = std::min(batches.total, std::max(width, sonicCount));
    const int rowCount = 1 + (batches.total - frontWidth + width - 1) / width;
    // One row with an MCV in it is a flat line: the MCV never leads.
    if (rowCount == 1 && !batches.guarded.empty()) return rowsLayout(members, layout, Shape::Echelon);
    const int anchor = batches.guarded.empty() ? (rowCount - 1) / 2 : rowCount / 2;

    // The flat block: the sonic units in the middle of the front row, and column 0.
    const int sonicLeft = rowColumn((frontWidth - sonicCount) / 2, frontWidth);
    const int blockLeft = std::min(0, sonicLeft), blockRight = std::max(0, sonicLeft + sonicCount - 1);
    std::vector<Spot> sonicSpots, guardedSpots, pool;
    int remaining = batches.total;
    for (int r = 0; r < rowCount; ++r) {
        const int cells = std::min(remaining, r == 0 ? frontWidth : width);
        remaining -= cells;
        std::vector<Spot> row;
        for (int j = 0; j < cells; ++j) {
            const int column = rowColumn(j, cells);
            row.push_back({anchor - r + echelonOffset(column, blockLeft, blockRight), column});
        }
        for (Spot& spot : row) {
            if (r == 0 && spot.column >= sonicLeft && spot.column < sonicLeft + sonicCount) sonicSpots.push_back(spot);
            else pool.push_back(spot);
        }
    }
    // MCVs: the spots of the middle row nearest its centre.
    if (!batches.guarded.empty()) {
        std::vector<Spot> middle;
        for (const Spot& spot : pool)
            if (spot.rank - echelonOffset(spot.column, blockLeft, blockRight) == 0) middle.push_back(spot);
        std::sort(middle.begin(), middle.end(), [](const Spot& a, const Spot& b) {
            return std::abs(a.column) != std::abs(b.column) ? std::abs(a.column) < std::abs(b.column) : a.column < b.column;
        });
        middle.resize(std::min(middle.size(), batches.guarded.size()));
        for (const Spot& spot : middle) {
            guardedSpots.push_back(spot);
            pool.erase(std::find_if(pool.begin(), pool.end(), [&](const Spot& s) { return s.rank == spot.rank && s.column == spot.column; }));
        }
    }
    std::sort(pool.begin(), pool.end(), frontFirst);
    std::vector<Placed> placed(members.size());
    handOut(batches.sonic, sonicSpots, placed);
    handOut(batches.guarded, guardedSpots, placed);
    for (const auto& batch : batches.fighting) handOut(batch, pool, placed);
    return placed;
}

/// Staggered: the box's rows two tiles apart, every unit two columns from its neighbours and
/// alternate rows shifted by one column, so no two units stand next to each other.
void staggerSpots(std::vector<Placed>& placed) {
    for (Placed& p : placed) {
        p.column = 2 * p.column + (p.rank & 1);
        p.rank *= 2;
    }
}

/// Pincer: the row layout split down the middle into two wings 'gap' columns either side of
/// the centre line; MCVs (column 0 of the middle row) stay between the wings.
void splitWings(const std::vector<Member>& members, std::vector<Placed>& placed, int gap) {
    for (size_t i = 0; i < placed.size(); ++i) {
        if (isProtected(members[i].itemID)) continue;
        placed[i].column += placed[i].column <= 0 ? -gap : gap - 1;
    }
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

namespace {

/// The members a plan works with: harvesters left out (they keep their ordinary orders and
/// never get a slot), sorted by object id so nothing depends on the caller's order.
std::vector<Member> joiningMembers(std::vector<Member> members) {
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [](const Member& member) { return !joinsFormations(member.itemID); }),
                  members.end());
    std::sort(members.begin(), members.end(),
              [](const Member& a, const Member& b) { return a.objectID < b.objectID; });
    return members;
}

/**
    Lays out a formation of two or more joining members (sorted by id) around 'target', its
    front towards 'forward'. With 'keepTightShape' a coherent group keeps its own shape,
    compressed and translated onto the target, unless that blocks a sonic lane.
*/
Plan layOut(const std::vector<Member>& members, const Coord& centroid, const Coord& target, const Coord& forward,
            bool keepTightShape, int mapWidth, int mapHeight, const CanStand& canStand, int infantryPerTile, Shape shape) {
    Plan result;
    result.groupSpeed = groupPace(members);
    result.formation = true;
    const Coord lateral(-forward.y, forward.x);
    result.forward = forward;
    const int perTile = std::max(1, infantryPerTile);
    std::vector<int> lateralOf(members.size());
    for (size_t i = 0; i < members.size(); ++i)
        lateralOf[i] = (members[i].tile.x - centroid.x) * lateral.x + (members[i].tile.y - centroid.y) * lateral.y;

    // Wanted destination for each member (indexed like 'members', which is sorted by id).
    std::vector<Coord> wanted(members.size());
    bool laidOut = false;

    if (keepTightShape) {
        bool coherent = true;
        for (const auto& member : members)
            if (tileDistance(member.tile, centroid) > kCoherentRadius) { coherent = false; break; }
        if (coherent) {
            // Keep the shape, translated onto the target and with the gaps closed up -
            // unless that leaves someone in front of a sonic unit; then it forms the box.
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
            case Shape::Column:    placed = rowsLayout(members, layout, shape); break;
            case Shape::Echelon:   placed = echelonLayout(members, layout); break;
            case Shape::Staggered: placed = rowsLayout(members, layout, shape); staggerSpots(placed); break;
            case Shape::Pincer: {
                int width = 0;
                placed = rowsLayout(members, layout, shape, &width);
                splitWings(members, placed, std::max(2, (width + 1) / 2));
                break;
            }
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

} // namespace

Coord centroidOf(const std::vector<Member>& members) {
    const std::vector<Member> joining = joiningMembers(members);
    if (joining.empty()) return Coord::Invalid();
    int sumX = 0, sumY = 0;
    for (const auto& member : joining) { sumX += member.tile.x; sumY += member.tile.y; }
    const int count = static_cast<int>(joining.size());
    return Coord(floorDiv(sumX, count), floorDiv(sumY, count));
}

Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile, Shape shape) {
    members = joiningMembers(std::move(members));
    Plan result;
    if (members.empty()) return result;
    if (members.size() < 2) {
        result.groupSpeed = groupPace(members);
        for (const auto& member : members) result.slots.push_back({member.objectID, target});
        return result;
    }
    const Coord centroid = centroidOf(members);
    // Every shape faces the direction of travel: rows across it, the front ahead.
    Coord forward = facing(centroid, target);
    if (forward.x == 0 && forward.y == 0) forward = Coord(0, -1);
    // A tight Box group keeps its shape; a group with an MCV always forms the box, around the MCV.
    bool anyProtected = false;
    for (const auto& member : members) anyProtected = anyProtected || isProtected(member.itemID);
    return layOut(members, centroid, target, forward, shape == Shape::Box && !anyProtected,
                  mapWidth, mapHeight, canStand, infantryPerTile, shape);
}

Plan planInPlace(std::vector<Member> members, const Coord& forward, int mapWidth, int mapHeight,
                 const CanStand& canStand, int infantryPerTile, Shape shape) {
    members = joiningMembers(std::move(members));
    Plan result;
    if (members.size() < 2) {
        // Nothing to form: everyone stays where it is.
        result.groupSpeed = groupPace(members);
        for (const auto& member : members) result.slots.push_back({member.objectID, member.tile});
        return result;
    }
    const Coord centroid = centroidOf(members);
    const Coord front = facingCode(forward) != 0 ? forward : Coord(0, -1);
    // The anchor is the centroid, or the nearest tile the anchor member may stand on: the
    // MCV (it takes the anchor), else the first vehicle, else the first member, by id.
    const Member* anchorMember = &members.front();
    for (const auto& member : members)
        if (isProtected(member.itemID)) { anchorMember = &member; break; }
    if (!isProtected(anchorMember->itemID))
        for (const auto& member : members)
            if (!member.infantry) { anchorMember = &member; break; }
    const Coord standable = nearestStandable(*anchorMember, centroid, mapWidth, mapHeight, canStand);
    const Coord anchor = standable.isValid() ? standable : centroid;
    // Always the shape itself: the player asked for it, so a tight Box group forms the box.
    return layOut(members, centroid, anchor, front, false, mapWidth, mapHeight, canStand, infantryPerTile, shape);
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
