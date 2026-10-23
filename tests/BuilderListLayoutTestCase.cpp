// Sidebar build grid geometry (docs/sidebar-grid.md): slot counts, rectangles, hit-testing
// and row scrolling of BuilderListLayout, which BuilderList draws and hit-tests with.
#include <catch2/catch_test_macros.hpp>

#include <GUI/dune/BuilderListLayout.h>

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <utility>
#include <vector>

namespace Layout = BuilderListLayout;

namespace {

struct Rect { int x, y, w, h; };

Rect slotRect(int slot) {
    return { Layout::slotX(slot), Layout::slotY(slot), Layout::ButtonWidth, Layout::ButtonHeight };
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/// The build-list index under (x,y) for a list scrolled to firstItem: what BuilderList::getButton returns.
int itemAt(int x, int y, int height, int firstItem, int itemCount) {
    const int slot = Layout::slotAt(x, y, height);
    if(slot < 0) return -1;
    const int index = firstItem + slot;
    return index < itemCount ? index : -1;
}

// Object-interface heights: smallest list, 640x480, 1024x768 and 1920x1080 sidebars
// (renderer height - 228), plus degenerate and in-between values.
const std::vector<int> heights = { 0, 50, Layout::FixedHeight, Layout::MinimumHeight, 120, 252, 371, 540, 600, 852, 853 };
const std::vector<int> itemCounts = { 0, 1, 2, 3, 6, 8, 25, 27, 28, 29, 46, 47, 100 };

} // namespace

TEST_CASE("Sidebar grid: icons keep the 91x55 aspect ratio and the grid fits the sidebar column", "[sidebar-grid]") {
    STATIC_REQUIRE(Layout::Columns == 2);
    STATIC_REQUIRE(Layout::WidgetWidth == SIDEBARWIDTH - 25);
    // Within one pixel of 91:55.
    REQUIRE(std::abs(Layout::ButtonHeight * Layout::PictureWidth - Layout::ButtonWidth * Layout::PictureHeight) <= Layout::PictureWidth);
    REQUIRE(Layout::ButtonWidth * 10 >= Layout::PictureWidth * 5);   // at least half size
    const Rect lastInRow = slotRect(Layout::Columns - 1);
    REQUIRE(Layout::slotX(0) >= Layout::BodyLeft);
    REQUIRE(lastInRow.x + lastInRow.w <= Layout::BodyLeft + Layout::BodyWidth);
    REQUIRE(Layout::BodyLeft - Layout::CapLeft >= 0);
    REQUIRE(Layout::BodyLeft + Layout::BodyWidth + Layout::CapRight <= Layout::WidgetWidth);
}

TEST_CASE("Sidebar grid: shows at least twice the old single column at 1024x768 and 1920x1080", "[sidebar-grid]") {
    // Old list: one 91x55 button per 60 px row after 68 px of arrows and Order button.
    auto oldButtons = [](int height) { return std::max(0, (height - 68) / 60); };
    for(int height : { 540, 852 }) {
        INFO("list height " << height);
        REQUIRE(Layout::slots(height) >= 2 * oldButtons(height));
    }
    REQUIRE(oldButtons(540) == 7);
    REQUIRE(Layout::slots(540) == 28);    // a 25-item DuneCity yard list fits unscrolled
    REQUIRE(oldButtons(852) == 13);
    REQUIRE(Layout::slots(852) == 46);
}

TEST_CASE("Sidebar grid: every visible slot is a unique rectangle inside the widget", "[sidebar-grid]") {
    for(int height : heights) {
        INFO("height " << height);
        const int rows = Layout::rows(height);
        REQUIRE(Layout::slots(height) == rows * Layout::Columns);
        // As many rows as fit, and no more.
        REQUIRE(Layout::realHeight(height) == Layout::FixedHeight + rows * Layout::RowPitch);
        if(height >= Layout::FixedHeight) {
            REQUIRE(Layout::realHeight(height) <= height);
            REQUIRE(Layout::realHeight(height) + Layout::RowPitch > height);
        } else {
            REQUIRE(rows == 0);
        }

        const int bodyBottom = Layout::BodyTop + Layout::bodyHeight(height);
        std::vector<Rect> rects;
        for(int slot = 0; slot < Layout::slots(height); ++slot) {
            const Rect r = slotRect(slot);
            INFO("slot " << slot);
            REQUIRE(r.x >= Layout::BodyLeft);
            REQUIRE(r.x + r.w <= Layout::BodyLeft + Layout::BodyWidth);
            REQUIRE(r.y >= Layout::BodyTop);
            REQUIRE(r.y + r.h <= bodyBottom);
            REQUIRE(r.y + r.h <= height);
            REQUIRE(r.x + r.w <= Layout::WidgetWidth);
            for(const Rect& other : rects) REQUIRE_FALSE(overlaps(r, other));
            rects.push_back(r);

            // Row-major order: columns left to right, then the next row.
            REQUIRE(Layout::slotX(slot) == Layout::slotX(slot % Layout::Columns));
            REQUIRE(Layout::slotY(slot) == Layout::slotY((slot / Layout::Columns) * Layout::Columns));
        }
        // Body ends inside the space the realHeight reserves, above the down arrow.
        REQUIRE(bodyBottom + Layout::Spacing + Layout::ArrowButtonHeight + Layout::Spacing + Layout::OrderButtonHeight
                == Layout::realHeight(height));
    }
}

TEST_CASE("Sidebar grid: a point hits exactly the item drawn under it", "[sidebar-grid]") {
    for(int height : heights) {
        for(int itemCount : itemCounts) {
            for(int firstItem = 0; firstItem <= Layout::maxFirstItem(itemCount, height); firstItem += Layout::Columns) {
                INFO("height " << height << " items " << itemCount << " first " << firstItem);
                for(int slot = 0; slot < Layout::slots(height); ++slot) {
                    const Rect r = slotRect(slot);
                    const int expected = firstItem + slot < itemCount ? firstItem + slot : -1;
                    // All four corners and the centre belong to the button.
                    const std::initializer_list<std::pair<int, int>> points = {
                        {r.x, r.y}, {r.x + r.w - 1, r.y}, {r.x, r.y + r.h - 1},
                        {r.x + r.w - 1, r.y + r.h - 1}, {r.x + r.w/2, r.y + r.h/2} };
                    for(const auto& [x, y] : points) {
                        REQUIRE(itemAt(x, y, height, firstItem, itemCount) == expected);
                    }
                    // Just outside each edge is a gap, margin, inset or another button.
                    const int left = itemAt(r.x - 1, r.y, height, firstItem, itemCount);
                    const int right = itemAt(r.x + r.w, r.y, height, firstItem, itemCount);
                    const int above = itemAt(r.x, r.y - 1, height, firstItem, itemCount);
                    const int below = itemAt(r.x, r.y + r.h, height, firstItem, itemCount);
                    REQUIRE(left == -1);
                    REQUIRE(right == -1);
                    REQUIRE(above == -1);
                    REQUIRE(below == -1);
                }
            }
        }
    }
    // Margins, insets, the arrow/Order strips and everything outside the widget.
    const int height = 540;
    for(int y = 0; y < Layout::FirstRowY; ++y) REQUIRE(Layout::slotAt(Layout::slotX(0), y, height) == -1);
    for(int x = 0; x < Layout::slotX(0); ++x) REQUIRE(Layout::slotAt(x, Layout::slotY(0), height) == -1);
    const Rect last = slotRect(Layout::slots(height) - 1);
    for(int x = last.x + last.w; x < Layout::WidgetWidth + 10; ++x) REQUIRE(Layout::slotAt(x, last.y, height) == -1);
    for(int y = last.y + last.h; y < height + 10; ++y) REQUIRE(Layout::slotAt(last.x, y, height) == -1);
    REQUIRE(Layout::slotAt(-1, -1, height) == -1);
    // Each pixel of the widget maps to at most one slot, and that slot's rectangle contains it.
    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < Layout::WidgetWidth; ++x) {
            const int slot = Layout::slotAt(x, y, height);
            if(slot < 0) continue;
            const Rect r = slotRect(slot);
            REQUIRE((x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h));
        }
    }
}

TEST_CASE("Sidebar grid: scrolling moves one row and never shows an empty row past the end", "[sidebar-grid]") {
    for(int height : heights) {
        for(int itemCount : itemCounts) {
            INFO("height " << height << " items " << itemCount);
            const int slots = Layout::slots(height);
            const int last = Layout::maxFirstItem(itemCount, height);
            REQUIRE(last % Layout::Columns == 0);
            if(slots == 0) continue;
            if(itemCount <= slots) REQUIRE(last == 0);

            int first = 0;
            int steps = 0;
            while(true) {
                const int next = Layout::scrollDown(first, itemCount, height);
                if(next == first) break;
                REQUIRE(next - first == Layout::Columns);
                first = next;
                REQUIRE(++steps <= itemCount);
            }
            REQUIRE(first == last);
            // At the end the last item is visible and the bottom visible row holds items.
            if(itemCount > 0) {
                REQUIRE(first + slots >= itemCount);
                if(itemCount > slots) REQUIRE(itemCount > first + slots - Layout::Columns);
            }
            // Back up to the top one row at a time.
            while(first > 0) {
                const int previous = Layout::scrollUp(first);
                REQUIRE(first - previous == Layout::Columns);
                first = previous;
            }
            REQUIRE(Layout::scrollUp(0) == 0);
        }
    }
    // A position set off the row grid (as a probe may) still reaches the true end.
    REQUIRE(Layout::scrollDown(1, 47, 540) == 3);
    REQUIRE(Layout::scrollDown(Layout::maxFirstItem(47, 540) - 1, 47, 540) == Layout::maxFirstItem(47, 540));
}
