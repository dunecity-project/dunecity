#ifndef TOUCHGESTURES_H
#define TOUCHGESTURES_H

#include <array>
#include <cstdint>

/**
    Turns finger events into mouse actions and map gestures on phones and tablets
    (IOS_PORT.md "Touch controls"). It has no SDL dependency so the desktop tests cover it.
    Coordinates are logical screen pixels, times are milliseconds.

    - One finger: a tap is a left click. The press waits until the finger moves or lifts,
      because a second finger arriving within kTwoFingerWindowMs makes it a two-finger
      gesture and no left click may have happened by then (it would start a selection box,
      or issue the order of an Attack/Move cursor). Moving past kTapSlop presses at the
      start point and drags (selection box, minimap).
    - Two fingers placed together: moving them pans, spreading or pinching them zooms one
      step (misc/ZoomSteps.h) per kZoomStep change in their distance, about the ratio
      between neighbouring steps, and lifting them quickly without either is a right
      click at their midpoint.
    - Holding one finger and then tapping a second is a right click at the first finger.
    - Holding still for kLongPressMs and releasing is a right click without a left click.
    - A gesture beginning at the configured map edge scrolls without clicking.
*/
class TouchGestures {
public:
    enum class Kind { Motion, LeftDown, LeftUp, RightClick, Pan, ZoomIn, ZoomOut };

    struct Action {
        Kind kind = Kind::Motion;
        int x = 0;      ///< pointer position; the midpoint of the fingers for Pan and zoom
        int y = 0;
        int dx = 0;     ///< Pan: how far the midpoint moved since the last Pan
        int dy = 0;
    };

    struct Actions {
        std::array<Action, 3> items{};
        int count = 0;

        void add(Kind kind, int x, int y, int dx = 0, int dy = 0) { items[count++] = Action{kind, x, y, dx, dy}; }
        const Action* begin() const { return items.data(); }
        const Action* end() const { return items.data() + count; }
    };

    static constexpr uint32_t kTwoFingerWindowMs = 150;
    static constexpr uint32_t kTwoFingerTapMs = 350;
    static constexpr uint32_t kLongPressMs = 550;
    static constexpr int kEdgeWidth = 20;
    static constexpr int kTapSlop = 10;
    static constexpr int kPanSlop = 12;
    static constexpr float kPinchSlop = 1.15f;
    static constexpr float kZoomStep = 1.2f;

    Actions fingerDown(int64_t finger, int x, int y, uint32_t ms);
    Actions fingerMotion(int64_t finger, int x, int y, uint32_t ms);
    Actions fingerUp(int64_t finger, int x, int y, uint32_t ms);

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(int px, int py) const {
            return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h;
        }
    };
    void setMapArea(Rect area, Rect controls) { mapArea = area; mapControls = controls; }
    /// Held touch only: never scroll from a lifted finger's stale mouse position.
    std::array<int, 2> edgeScroll() const;

    /// Forgets all fingers, e.g. when the app leaves the foreground mid-touch.
    void reset() { *this = TouchGestures{}; }

private:
    enum class State { Idle, Pending, Pressed, Edge, TwoFinger, Done };
    std::array<int, 2> edgeAt(int x, int y) const;
    Rect mapArea, mapControls;

    struct Finger {
        int64_t id = 0;
        int x = 0;
        int y = 0;
    };

    int midX() const { return (first.x + second.x) / 2; }
    int midY() const { return (first.y + second.y) / 2; }
    float spread() const;

    State state = State::Idle;
    int fingersDown = 0;
    Finger first;
    Finger second;
    int startX = 0;
    int startY = 0;
    uint32_t startMs = 0;

    // Two-finger gesture
    int startMidX = 0;
    int startMidY = 0;
    int lastMidX = 0;
    int lastMidY = 0;
    float startSpread = 1.0f;
    float zoomSpread = 1.0f;
    bool moving = false;
};

#endif // TOUCHGESTURES_H
