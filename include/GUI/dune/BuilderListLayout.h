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

#ifndef BUILDERLISTLAYOUT_H
#define BUILDERLISTLAYOUT_H

#include <Definitions.h>

#include <algorithm>

/**
    Geometry of the sidebar build grid (docs/sidebar-grid.md): build items sit in a grid of
    Columns columns, each icon scaled down from the 91x55 picture with its aspect ratio kept.
    Pure integer layout shared by BuilderList and its unit test; every coordinate is relative
    to the BuilderList widget, which spans the whole object-interface column.

    "Slot" is a visible grid cell (0 = top left, row-major); the list shows build item
    firstItem + slot in slot. Scrolling moves firstItem by one row (Columns items).
*/
namespace BuilderListLayout {

constexpr int ArrowButtonWidth = 48;
constexpr int ArrowButtonHeight = 16;
constexpr int OrderButtonHeight = 16;
constexpr int Spacing = 5;      ///< between the arrows, the list body and the Order button
constexpr int RowInset = 5;     ///< black above the first and below the last row

constexpr int CapLeft = 3;      ///< cap art overhang left of the list body
constexpr int CapRight = 8;     ///< cap art (frame edge and divider) right of the list body
constexpr int WidgetWidth = SIDEBARWIDTH - 25;  ///< the object-interface column (GameInterface)
constexpr int BodyLeft = CapLeft;
constexpr int BodyWidth = WidgetWidth - CapLeft - CapRight;

constexpr int Columns = 2;
constexpr int Margin = 2;       ///< black left of the first and right of the last column
constexpr int Gap = 2;          ///< between neighbouring buttons, both directions

constexpr int PictureWidth = 91;
constexpr int PictureHeight = 55;
constexpr int ButtonWidth = (BodyWidth - 2*Margin - (Columns - 1)*Gap) / Columns;
constexpr int ButtonHeight = (ButtonWidth*PictureHeight + PictureWidth/2) / PictureWidth;
constexpr int ColumnPitch = ButtonWidth + Gap;
constexpr int RowPitch = ButtonHeight + Gap;

constexpr int BodyTop = ArrowButtonHeight + Spacing;
constexpr int FirstRowY = BodyTop + RowInset;
/// Height of everything except the rows: up arrow, insets, down arrow, Order button, spacing.
constexpr int FixedHeight = BodyTop + 2*RowInset - Gap + Spacing + ArrowButtonHeight + Spacing + OrderButtonHeight;
constexpr int MinimumRows = 3;
constexpr int MinimumHeight = FixedHeight + MinimumRows*RowPitch;

static_assert(ButtonWidth >= 48, "Build icons became too small to read");
static_assert(BodyLeft + 2*Margin + Columns*ButtonWidth + (Columns - 1)*Gap <= BodyLeft + BodyWidth,
              "The grid does not fit the list body");

/// Number of whole rows a widget of this height shows.
constexpr int rows(int height) {
    return std::max(0, (height - FixedHeight) / RowPitch);
}

/// Number of visible slots (rows * columns).
constexpr int slots(int height) {
    return rows(height) * Columns;
}

/// Height actually used by the rows that fit; the remainder stays below the Order button.
constexpr int realHeight(int height) {
    return FixedHeight + rows(height) * RowPitch;
}

/// Height of the black list body behind the rows.
constexpr int bodyHeight(int height) {
    return 2*RowInset - Gap + rows(height) * RowPitch;
}

constexpr int slotX(int slot) {
    return BodyLeft + Margin + (slot % Columns) * ColumnPitch;
}

constexpr int slotY(int slot) {
    return FirstRowY + (slot / Columns) * RowPitch;
}

/// The visible slot under (x,y), or -1 for the margins, gaps and everything outside the rows.
constexpr int slotAt(int x, int y, int height) {
    const int dx = x - (BodyLeft + Margin);
    const int dy = y - FirstRowY;
    if(dx < 0 || dy < 0) return -1;
    const int column = dx / ColumnPitch;
    const int row = dy / RowPitch;
    if(column >= Columns || row >= rows(height)) return -1;
    if(dx % ColumnPitch >= ButtonWidth || dy % RowPitch >= ButtonHeight) return -1;
    return row * Columns + column;
}

/// Largest first item: the last row of items sits on the last visible row, never an empty row below.
constexpr int maxFirstItem(int itemCount, int height) {
    const int itemRows = (std::max(0, itemCount) + Columns - 1) / Columns;
    return std::max(0, itemRows - rows(height)) * Columns;
}

constexpr int scrollUp(int firstItem) {
    return std::max(0, firstItem - Columns);
}

constexpr int scrollDown(int firstItem, int itemCount, int height) {
    const int last = maxFirstItem(itemCount, height);
    return firstItem < last ? std::min(firstItem + Columns, last) : firstItem;
}

} // namespace BuilderListLayout

#endif // BUILDERLISTLAYOUT_H
