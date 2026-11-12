#include <catch2/catch_test_macros.hpp>

#include <misc/TouchGestures.h>

#include <vector>

// Finger sequences as SDL reports them on a phone, against the mouse actions and map
// gestures the game must see (IOS_PORT.md "Touch controls").
namespace {
using Kind = TouchGestures::Kind;

struct Seen {
    Kind kind;
    int x, y, dx, dy;
    bool operator==(const Seen& o) const { return kind == o.kind && x == o.x && y == o.y && dx == o.dx && dy == o.dy; }
};

void add(std::vector<Seen>& seen, const TouchGestures::Actions& actions) {
    for (const auto& a : actions) seen.push_back({a.kind, a.x, a.y, a.dx, a.dy});
}

std::vector<Seen> only(const std::vector<Seen>& seen, Kind kind) {
    std::vector<Seen> out;
    for (const auto& s : seen) if (s.kind == kind) out.push_back(s);
    return out;
}

bool any(const std::vector<Seen>& seen, std::initializer_list<Kind> kinds) {
    for (const auto& s : seen) for (Kind k : kinds) if (s.kind == k) return true;
    return false;
}
} // namespace

TEST_CASE("A tap clicks where the finger came down, and only once it lifts", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 200, 1000));
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp}));   // a second finger may still come

    add(seen, g.fingerMotion(1, 106, 195, 1040));               // jitter within the slop
    add(seen, g.fingerUp(1, 106, 195, 1080));
    CHECK(only(seen, Kind::LeftDown) == std::vector<Seen>{{Kind::LeftDown, 100, 200, 0, 0}});
    CHECK(only(seen, Kind::LeftUp) == std::vector<Seen>{{Kind::LeftUp, 100, 200, 0, 0}});
}

TEST_CASE("Hold and release right-clicks once without first left-clicking", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 540, 260, 1000));
    add(seen, g.fingerMotion(1, 543, 258, 1200));
    add(seen, g.fingerUp(1, 543, 258, 1000 + TouchGestures::kLongPressMs));
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp}));
    CHECK(only(seen, Kind::RightClick) == std::vector<Seen>{{Kind::RightClick, 540, 260, 0, 0}});
    seen.clear();
    add(seen, g.fingerDown(2, 540, 260, 2000));
    add(seen, g.fingerUp(2, 540, 260, 2100));
    CHECK(only(seen, Kind::LeftDown).size() == 1);
    CHECK(only(seen, Kind::RightClick).empty());
}

TEST_CASE("Touch map edges scroll while held and never click or stick after release", "[touch]") {
    for(const auto position : {std::array<int,4>{5,200,-1,0}, {475,200,1,0},
                              {200,45,0,-1}, {200,475,0,1}, {5,475,-1,1}}) {
        TouchGestures g;
        g.setMapArea({0,40,480,440}, {300,46,170,44});
        std::vector<Seen> seen;
        add(seen, g.fingerDown(1,position[0],position[1],100));
        CHECK(g.edgeScroll() == std::array<int,2>{position[2],position[3]});
        add(seen, g.fingerUp(1,position[0],position[1],900));
        CHECK(g.edgeScroll() == std::array<int,2>{0,0});
        CHECK_FALSE(any(seen,{Kind::LeftDown,Kind::LeftUp,Kind::RightClick}));
    }
}

TEST_CASE("Touch edge excludes HUD and sidebar and stops for multitouch or focus loss", "[touch]") {
    TouchGestures g;
    g.setMapArea({0,40,480,440}, {300,46,170,44});
    g.fingerDown(1,460,50,0); // zoom button, inside the top edge band
    CHECK(g.edgeScroll() == std::array<int,2>{0,0});
    auto tap = g.fingerUp(1,460,50,80);
    CHECK(tap.count == 2);
    CHECK(tap.items[0].kind == Kind::LeftDown);
    g.fingerDown(1,540,200,100); // sidebar is not the right map edge
    CHECK(g.edgeScroll() == std::array<int,2>{0,0});
    g.fingerUp(1,540,200,180);
    g.fingerDown(1,5,200,200);
    g.fingerDown(2,100,200,240);
    CHECK(g.edgeScroll() == std::array<int,2>{0,0});
    g.reset();
    CHECK(g.edgeScroll() == std::array<int,2>{0,0});
}

TEST_CASE("Dragging a selection into a map edge scrolls without turning it into a hold", "[touch]") {
    TouchGestures g;
    g.setMapArea({0,40,480,440}, {});
    g.fingerDown(1,100,200,0);
    g.fingerMotion(1,5,200,100);
    CHECK(g.edgeScroll() == std::array<int,2>{-1,0});
    const auto up = g.fingerUp(1,5,200,1000);
    CHECK(up.items[up.count-1].kind == Kind::LeftUp);
    CHECK(g.edgeScroll() == std::array<int,2>{0,0});
}

TEST_CASE("A one-finger drag presses at its start and releases where it ends", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 100, 0));
    add(seen, g.fingerMotion(1, 130, 100, 40));
    add(seen, g.fingerMotion(1, 160, 140, 80));
    add(seen, g.fingerUp(1, 160, 140, 120));

    REQUIRE(seen.size() == 6);
    CHECK(seen[1] == Seen{Kind::LeftDown, 100, 100, 0, 0});    // the selection box starts at the touch
    CHECK(seen[2] == Seen{Kind::Motion, 130, 100, 0, 0});
    CHECK(seen[3] == Seen{Kind::Motion, 160, 140, 0, 0});
    CHECK(seen.back() == Seen{Kind::LeftUp, 160, 140, 0, 0});
}

TEST_CASE("Two fingers moved together pan by their midpoint and never click", "[touch]") {
    TouchGestures g;
    g.setMapArea({0, 40, 480, 440}, {});
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 100, 0));
    add(seen, g.fingerDown(2, 200, 100, 60));
    for (int step = 1; step <= 10; ++step) {
        add(seen, g.fingerMotion(1, 100 - 8 * step, 100 + 3 * step, 60 + 16 * step));
        add(seen, g.fingerMotion(2, 200 - 8 * step, 100 + 3 * step, 68 + 16 * step));
    }
    add(seen, g.fingerUp(1, 20, 130, 400));
    add(seen, g.fingerUp(2, 120, 130, 410));

    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp, Kind::RightClick, Kind::ZoomIn, Kind::ZoomOut}));
    int dx = 0, dy = 0;
    for (const auto& pan : only(seen, Kind::Pan)) { dx += pan.dx; dy += pan.dy; }
    CHECK(dx == -80);   // the whole midpoint travel, including the slop before panning starts
    CHECK(dy == 30);
    CHECK(only(seen, Kind::Pan).back().x == 70);
    CHECK(only(seen, Kind::Pan).back().y == 130);
}

TEST_CASE("Spreading two fingers zooms in one step per 1.2x, pinching zooms out", "[touch]") {
    TouchGestures g;
    g.setMapArea({0, 40, 480, 440}, {});
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 300, 200, 0));
    add(seen, g.fingerDown(2, 340, 200, 20));      // 40 px apart
    add(seen, g.fingerMotion(2, 347, 200, 40));    // 47: 1.175x, below one step
    CHECK(only(seen, Kind::ZoomIn).empty());
    add(seen, g.fingerMotion(2, 349, 200, 60));    // 49: 1.225x
    add(seen, g.fingerMotion(2, 359, 200, 80));    // 59: 1.2x the last step
    CHECK(only(seen, Kind::ZoomIn).size() == 2);
    add(seen, g.fingerMotion(2, 330, 200, 100));   // 30: pinched to 0.5x
    CHECK(only(seen, Kind::ZoomOut).size() == 1);
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::RightClick}));
}

TEST_CASE("A pan can spread and pinch at the current midpoint without clicking", "[touch]") {
    for (const int firstLifted : {1, 2}) {
        TouchGestures g;
        g.setMapArea({0, 40, 480, 440}, {});
        std::vector<Seen> seen;
        add(seen, g.fingerDown(1, 200, 200, 0));
        add(seen, g.fingerDown(2, 400, 200, 20));
        add(seen, g.fingerMotion(1, 216, 208, 40));
        add(seen, g.fingerMotion(2, 416, 208, 50));
        add(seen, g.fingerMotion(2, 464, 208, 70)); // 248 px spread after the pan
        add(seen, g.fingerMotion(2, 404, 208, 90)); // 188 px: pinch relative to the last zoom

        CHECK(only(seen, Kind::Pan) == std::vector<Seen>{
            {Kind::Pan, 316, 208, 16, 8},
            {Kind::Pan, 340, 208, 24, 0},
            {Kind::Pan, 310, 208, -30, 0}});
        CHECK(only(seen, Kind::ZoomIn) == std::vector<Seen>{{Kind::ZoomIn, 340, 208, 0, 0}});
        CHECK(only(seen, Kind::ZoomOut) == std::vector<Seen>{{Kind::ZoomOut, 310, 208, 0, 0}});
        CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));

        seen.clear();
        const int remaining = firstLifted == 1 ? 2 : 1;
        add(seen, g.fingerUp(firstLifted, firstLifted == 1 ? 216 : 404, 208, 110));
        add(seen, g.fingerMotion(remaining, 260, 240, 130));
        add(seen, g.fingerUp(remaining, 260, 240, 150));
        CHECK(seen.empty()); // either lift order ends the gesture, with no one-finger selection

        add(seen, g.fingerDown(3, 100, 200, 1000));
        add(seen, g.fingerDown(4, 300, 200, 1020));
        add(seen, g.fingerMotion(3, 116, 200, 1040));
        add(seen, g.fingerMotion(4, 316, 200, 1060));
        add(seen, g.fingerUp(3, 116, 200, 1080));
        add(seen, g.fingerUp(4, 316, 200, 1100));
        CHECK(only(seen, Kind::Pan) == std::vector<Seen>{{Kind::Pan, 216, 200, 16, 0}});
        CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp, Kind::RightClick, Kind::ZoomIn, Kind::ZoomOut}));
    }
}

TEST_CASE("Sidebar list taps and drags remain mouse actions with a map area configured", "[touch]") {
    TouchGestures g;
    g.setMapArea({0, 40, 480, 440}, {300, 46, 170, 44});
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 540, 200, 0));
    add(seen, g.fingerUp(1, 540, 200, 80));
    CHECK(seen == std::vector<Seen>{
        {Kind::Motion, 540, 200, 0, 0},
        {Kind::LeftDown, 540, 200, 0, 0},
        {Kind::LeftUp, 540, 200, 0, 0}});

    seen.clear();
    add(seen, g.fingerDown(2, 540, 200, 200));
    add(seen, g.fingerMotion(2, 540, 230, 240));
    add(seen, g.fingerMotion(2, 540, 260, 280));
    add(seen, g.fingerUp(2, 540, 260, 320));
    CHECK(seen == std::vector<Seen>{
        {Kind::Motion, 540, 200, 0, 0},
        {Kind::LeftDown, 540, 200, 0, 0},
        {Kind::Motion, 540, 230, 0, 0},
        {Kind::Motion, 540, 260, 0, 0},
        {Kind::Motion, 540, 260, 0, 0},
        {Kind::LeftUp, 540, 260, 0, 0}});
    CHECK(g.edgeScroll() == std::array<int, 2>{0, 0});
}

TEST_CASE("Both touch origins must be on the map rather than a list or map control", "[touch]") {
    for (const auto starts : {std::array<int, 4>{100, 200, 540, 200}, {540, 200, 100, 200},
                              {520, 200, 560, 200}, {350, 60, 200, 200},
                              {200, 200, 350, 60}, {200, 20, 300, 20}}) {
        TouchGestures g;
        g.setMapArea({0, 40, 480, 440}, {300, 46, 170, 44});
        std::vector<Seen> seen;
        add(seen, g.fingerDown(1, starts[0], starts[1], 0));
        add(seen, g.fingerDown(2, starts[2], starts[3], 20));
        add(seen, g.fingerMotion(1, 220, 240, 40));
        add(seen, g.fingerMotion(2, 440, 240, 60)); // entering the map cannot admit the gesture
        add(seen, g.fingerUp(1, 220, 240, 80));
        add(seen, g.fingerUp(2, 440, 240, 100));
        CHECK_FALSE(any(seen, {Kind::Pan, Kind::ZoomIn, Kind::ZoomOut,
                              Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));

        seen.clear();
        add(seen, g.fingerDown(3, starts[0], starts[1], 1000));
        add(seen, g.fingerDown(4, starts[2], starts[3], 1020));
        add(seen, g.fingerUp(3, starts[0], starts[1], 1080));
        add(seen, g.fingerUp(4, starts[2], starts[3], 1100));
        CHECK(only(seen, Kind::RightClick) == std::vector<Seen>{
            {Kind::RightClick, (starts[0] + starts[2]) / 2, (starts[1] + starts[3]) / 2, 0, 0}});
        CHECK_FALSE(any(seen, {Kind::Pan, Kind::ZoomIn, Kind::ZoomOut, Kind::LeftDown, Kind::LeftUp}));
    }
}

TEST_CASE("Moving the first touch into the map before the second arrives does not admit it", "[touch]") {
    TouchGestures g;
    g.setMapArea({0, 40, 480, 440}, {});
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 484, 200, 0));
    add(seen, g.fingerMotion(1, 476, 200, 10)); // still within the one-finger tap slop
    add(seen, g.fingerDown(2, 400, 200, 20));
    add(seen, g.fingerMotion(2, 320, 240, 40));
    add(seen, g.fingerUp(1, 476, 200, 60));
    add(seen, g.fingerUp(2, 320, 240, 80));
    CHECK_FALSE(any(seen, {Kind::Pan, Kind::ZoomIn, Kind::ZoomOut,
                          Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));
}

TEST_CASE("Without an active map two-finger movement never emits map gestures or clicks", "[touch]") {
    TouchGestures g; // menu/list translation supplies no map area
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 200, 0));
    add(seen, g.fingerDown(2, 200, 200, 20));
    add(seen, g.fingerMotion(2, 260, 240, 40));
    add(seen, g.fingerUp(1, 100, 200, 60));
    add(seen, g.fingerUp(2, 260, 240, 80));
    CHECK_FALSE(any(seen, {Kind::Pan, Kind::ZoomIn, Kind::ZoomOut,
                          Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));
}

TEST_CASE("Disabling the map cancels admission until both fingers lift", "[touch]") {
    for (const bool secondAlreadyDown : {false, true}) {
        TouchGestures g;
        g.setMapArea({0, 40, 480, 440}, {});
        std::vector<Seen> seen;
        add(seen, g.fingerDown(1, 100, 200, 0));
        if(secondAlreadyDown) add(seen, g.fingerDown(2, 200, 200, 20));
        g.setMapArea({}, {}); // a modal opens before or during the two-finger gesture
        if(!secondAlreadyDown) add(seen, g.fingerDown(2, 200, 200, 20));
        add(seen, g.fingerMotion(2, 260, 200, 40));
        g.setMapArea({0, 40, 480, 440}, {});
        add(seen, g.fingerMotion(2, 320, 200, 60));
        add(seen, g.fingerUp(1, 100, 200, 80));
        add(seen, g.fingerUp(2, 320, 200, 100));
        CHECK_FALSE(any(seen, {Kind::Pan, Kind::ZoomIn, Kind::ZoomOut,
                              Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));

        seen.clear();
        add(seen, g.fingerDown(3, 100, 200, 1000));
        add(seen, g.fingerDown(4, 200, 200, 1020));
        add(seen, g.fingerMotion(4, 230, 200, 1040));
        CHECK(only(seen, Kind::ZoomIn) == std::vector<Seen>{{Kind::ZoomIn, 165, 200, 0, 0}});
    }
}

TEST_CASE("A quick two-finger tap right-clicks between the fingers", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 100, 0));
    add(seen, g.fingerDown(2, 140, 120, 50));
    add(seen, g.fingerUp(1, 100, 100, 200));
    add(seen, g.fingerUp(2, 140, 120, 210));

    CHECK(only(seen, Kind::RightClick) == std::vector<Seen>{{Kind::RightClick, 120, 110, 0, 0}});
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp}));
}

TEST_CASE("Two fingers resting too long before lifting do nothing", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 100, 100, 0));
    add(seen, g.fingerDown(2, 140, 120, 50));
    add(seen, g.fingerUp(1, 100, 100, 50 + TouchGestures::kTwoFingerTapMs + 1));
    add(seen, g.fingerUp(2, 140, 120, 600));
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp, Kind::RightClick}));
}

TEST_CASE("Holding one finger and tapping a second right-clicks under the first", "[touch]") {
    TouchGestures g;
    std::vector<Seen> seen;
    add(seen, g.fingerDown(1, 400, 250, 0));
    add(seen, g.fingerDown(2, 500, 300, TouchGestures::kTwoFingerWindowMs + 100));
    add(seen, g.fingerUp(2, 500, 300, 400));
    add(seen, g.fingerUp(1, 400, 250, 500));

    CHECK(only(seen, Kind::RightClick) == std::vector<Seen>{{Kind::RightClick, 400, 250, 0, 0}});
    // No left click under the target: it would select the enemy just ordered at.
    CHECK_FALSE(any(seen, {Kind::LeftDown, Kind::LeftUp}));
}

TEST_CASE("After a gesture the next single tap clicks again", "[touch]") {
    TouchGestures g;
    g.setMapArea({0, 40, 480, 440}, {});
    g.fingerDown(1, 100, 100, 0);
    g.fingerDown(2, 200, 100, 30);
    g.fingerMotion(2, 260, 100, 60);
    g.fingerUp(2, 260, 100, 90);
    g.fingerMotion(1, 140, 100, 100);   // the remaining finger does not drag a selection box
    g.fingerUp(1, 140, 100, 120);

    std::vector<Seen> seen;
    add(seen, g.fingerDown(3, 50, 60, 2000));
    add(seen, g.fingerUp(3, 50, 60, 2050));
    CHECK(only(seen, Kind::LeftDown) == std::vector<Seen>{{Kind::LeftDown, 50, 60, 0, 0}});
    CHECK(only(seen, Kind::LeftUp) == std::vector<Seen>{{Kind::LeftUp, 50, 60, 0, 0}});
}
