#include <misc/TouchGestures.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

float TouchGestures::spread() const {
    return std::max(1.0f, std::hypot(static_cast<float>(first.x - second.x), static_cast<float>(first.y - second.y)));
}

std::array<int, 2> TouchGestures::edgeAt(int x, int y) const {
    if(!mapArea.contains(x, y) || mapControls.contains(x, y)) return {0, 0};
    return {x < mapArea.x + kEdgeWidth ? -1 : (x >= mapArea.x + mapArea.w - kEdgeWidth ? 1 : 0),
            y < mapArea.y + kEdgeWidth ? -1 : (y >= mapArea.y + mapArea.h - kEdgeWidth ? 1 : 0)};
}

std::array<int, 2> TouchGestures::edgeScroll() const {
    if(fingersDown != 1 || (state != State::Edge && state != State::Pressed)) return {0, 0};
    return edgeAt(first.x, first.y);
}

TouchGestures::Actions TouchGestures::fingerDown(int64_t finger, int x, int y, uint32_t ms) {
    Actions out;
    if(state == State::Idle) {
        // A lost FINGERUP (app switch, system gesture) must not keep later touches waiting.
        fingersDown = 1;
        first = Finger{finger, x, y};
        startX = x;
        startY = y;
        startMs = ms;
        state = State::Pending;
        if(edgeAt(x, y) != std::array<int, 2>{0, 0}) state = State::Edge;
        out.add(Kind::Motion, x, y);
        return out;
    }

    ++fingersDown;
    if(state != State::Pending && state != State::Edge) {
        return out;  // a dragging finger or an ongoing two-finger gesture ignores extra fingers
    }

    if(ms - startMs <= kTwoFingerWindowMs) {
        second = Finger{finger, x, y};
        startMidX = lastMidX = midX();
        startMidY = lastMidY = midY();
        startSpread = zoomSpread = spread();
        startMs = ms;
        moving = false;
        state = State::TwoFinger;
    } else if(state != State::Edge) {
        // The first finger has been held still: the second finger's tap right-clicks under it.
        out.add(Kind::Motion, first.x, first.y);
        out.add(Kind::RightClick, first.x, first.y);
        state = State::Done;
    } else {
        state = State::Done; // an edge hold must not turn into a map order
    }
    return out;
}

TouchGestures::Actions TouchGestures::fingerMotion(int64_t finger, int x, int y, uint32_t) {
    Actions out;
    switch(state) {
        case State::Pending: {
            if(finger != first.id) break;
            first.x = x;
            first.y = y;
            if(std::abs(x - startX) > kTapSlop || std::abs(y - startY) > kTapSlop) {
                out.add(Kind::LeftDown, startX, startY);
                out.add(Kind::Motion, x, y);
                state = State::Pressed;
            }
        } break;

        case State::Edge: {
            if(finger == first.id) { first.x = x; first.y = y; }
        } break;

        case State::Pressed: {
            if(finger != first.id) break;
            first.x = x;
            first.y = y;
            out.add(Kind::Motion, x, y);
        } break;

        case State::TwoFinger: {
            if(finger == first.id) {
                first.x = x;
                first.y = y;
            } else if(finger == second.id) {
                second.x = x;
                second.y = y;
            } else {
                break;
            }

            const int mx = midX();
            const int my = midY();
            const float ratio = spread() / startSpread;
            if(!moving && (std::abs(mx - startMidX) > kPanSlop || std::abs(my - startMidY) > kPanSlop
                           || ratio > kPinchSlop || ratio < 1.0f / kPinchSlop)) {
                moving = true;
            }
            if(!moving) break;

            // The map follows the fingers from where they were put down, slop included.
            if(mx != lastMidX || my != lastMidY) {
                out.add(Kind::Pan, mx, my, mx - lastMidX, my - lastMidY);
                lastMidX = mx;
                lastMidY = my;
            }
            const float zoomRatio = spread() / zoomSpread;
            if(zoomRatio >= kZoomStep) {
                out.add(Kind::ZoomIn, mx, my);
                zoomSpread = spread();
            } else if(zoomRatio <= 1.0f / kZoomStep) {
                out.add(Kind::ZoomOut, mx, my);
                zoomSpread = spread();
            }
        } break;

        default:
            break;
    }
    return out;
}

TouchGestures::Actions TouchGestures::fingerUp(int64_t finger, int x, int y, uint32_t ms) {
    Actions out;
    fingersDown = std::max(0, fingersDown - 1);

    switch(state) {
        case State::Pending: {
            if(finger != first.id) break;
            // A held icon must never enqueue an item before its cancel action.
            if(ms - startMs >= kLongPressMs) {
                out.add(Kind::RightClick, startX, startY);
            } else {
                out.add(Kind::LeftDown, startX, startY);
                out.add(Kind::LeftUp, startX, startY);
            }
            state = State::Done;
        } break;

        case State::Pressed: {
            if(finger != first.id) break;
            out.add(Kind::Motion, x, y);
            out.add(Kind::LeftUp, x, y);
            state = State::Done;
        } break;

        case State::TwoFinger: {
            if(finger != first.id && finger != second.id) break;
            if(!moving && ms - startMs <= kTwoFingerTapMs) {
                out.add(Kind::Motion, midX(), midY());
                out.add(Kind::RightClick, midX(), midY());
            }
            state = State::Done;  // lifting one finger ends the gesture; no one-finger drag follows
        } break;

        default:
            break;
    }

    if(fingersDown == 0) {
        state = State::Idle;
    }
    return out;
}
