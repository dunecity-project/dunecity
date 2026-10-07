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

#ifndef CHANGEEVENTLIST_H
#define CHANGEEVENTLIST_H

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/SDL2pp.h>

#include <list>

/// A maximal lobby refresh is 7 events per house row - house, team, colour, graphics skin,
/// spice income, and one event for each of the two player seats - and the lobby has at most
/// MAX_CUSTOM_GAME_PLAYERS = 9 rows, so 7 * 9 = 63 events.
///
/// The largest list that is ever sent is one more than that: getChangeEventListForNewPlayer
/// appends the seat claim for the joining player to a full refresh, which is 64. So this bound
/// is exactly the worst legitimate case, with nothing to spare - adding another per-row event
/// means raising it. Anything beyond is refused before a single event is allocated.
#define CHANGEEVENTLIST_MAX_EVENTS 64

class ChangeEventList {
public:
    class ChangeEvent {
    public:
        /// Wire values. Append only: a peer decodes these by number, so renumbering an entry
        /// would make two builds disagree about what a lobby change means.
        enum class EventType {
            ChangeHouse,
            ChangeTeam,
            ChangeColor,
            ChangePlayer,
            SetHumanPlayer,
            ChangeGraphicsSkin,
            ChangeSpiceIncome    ///< host-only; see LobbyAuthorization
        };

        /// The highest event type this build understands, and therefore the bound the decoder
        /// checks. Kept next to the enum so adding a type cannot forget to widen it.
        static constexpr EventType lastEventType = EventType::ChangeSpiceIncome;


        ChangeEvent(EventType eventType, Uint32 slot, Uint32 newValue)
         : eventType(eventType), slot(slot), newValue(newValue) {

        }

        ChangeEvent(Uint32 slot, const std::string& newStringValue)
         : eventType(EventType::SetHumanPlayer), slot(slot), newStringValue(newStringValue) {
        }

        explicit ChangeEvent(InputStream& stream) {
            const Uint32 rawEventType = stream.readUint32();
            if(rawEventType > static_cast<Uint32>(lastEventType)) {
                throw InputStream::error("ChangeEventList: unknown change event type!");
            }
            eventType = static_cast<EventType>(rawEventType);
            slot = stream.readUint32();

            if(eventType == EventType::SetHumanPlayer) {
                newStringValue = stream.readString();
            } else {
                newValue = stream.readUint32();
            }
        }

        void save(OutputStream& stream) const {
            stream.writeUint32(static_cast<Uint32>(eventType));
            stream.writeUint32(slot);
            if(eventType == EventType::SetHumanPlayer) {
                stream.writeString(newStringValue);
            } else {
                stream.writeUint32(newValue);
            }
        }

        EventType   eventType;
        Uint32      slot = 0;
        Uint32      newValue = 0;
        std::string newStringValue;
    };

    ChangeEventList() = default;

    explicit ChangeEventList(InputStream& stream) {
        Uint32 numChangeEvents = stream.readUint32();
        if(numChangeEvents > CHANGEEVENTLIST_MAX_EVENTS) {
            throw InputStream::error("ChangeEventList: too many change events!");
        }
        // One event is at least type + slot + value = 12 bytes.
        stream.requireReadableElements(numChangeEvents, 12);
        for(Uint32 i = 0; i < numChangeEvents; i++) {
            changeEventList.emplace_back(stream);
        }
    }

    ~ChangeEventList() = default;

    void save(OutputStream& stream) const {
        stream.writeUint32(changeEventList.size());
        for(const ChangeEvent& changeEvent : changeEventList) {
            changeEvent.save(stream);
        }
    }

    std::list<ChangeEvent> changeEventList;
};

#endif //CHANGEEVENTLIST_H
