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
