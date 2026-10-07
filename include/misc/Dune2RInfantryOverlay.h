#ifndef DUNE2R_INFANTRY_OVERLAY_H
#define DUNE2R_INFANTRY_OVERLAY_H

#include <SDL.h>
#include <algorithm>
#include <cstddef>
#include <deque>

// Local renderer history, not a unit/death simulation or serialized game state.
class Dune2RInfantryOverlay final {
public:
    struct Event {
        Uint32 token;
        Uint32 startMs;
        int direction;
        int house;
    };
    static constexpr std::size_t capacity = 2048;
    static constexpr Uint32 lifetimeMs = 32016;

    Uint32 record(Uint32 now, int direction, int house) {
        if(house != 0 || direction < 0 || direction >= 8) {
            return 0;
        }
        prune(now);
        if(events.size() >= capacity) {
            events.pop_front();
        }
        if(nextToken == 0) {
            events.clear();
            nextToken = 1;
        }
        const Uint32 token = nextToken++;
        events.push_back({token, now, direction, house});
        return token;
    }

    const Event* find(Uint32 token, Uint32 now, int house) {
        prune(now);
        if(token == 0) {
            return nullptr;
        }
        const auto found = std::find_if(events.begin(), events.end(),
            [token, house](const Event& event) {
                return event.token == token && event.house == house;
            });
        return found == events.end() ? nullptr : &*found;
    }

    void clear() { events.clear(); }
    std::size_t size() const { return events.size(); }

private:
    void prune(Uint32 now) {
        events.erase(std::remove_if(events.begin(), events.end(),
            [now](const Event& event) {
                return now < event.startMs || now - event.startMs > lifetimeMs;
            }), events.end());
    }

    Uint32 nextToken = 1; // Never reused when a map/mount invalidates the cache.
    std::deque<Event> events;
};

#endif
