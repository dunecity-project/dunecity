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

#ifndef LOBBYAUTHORIZATION_H
#define LOBBYAUTHORIZATION_H

#include <DataTypes.h>
#include <Network/ChangeEventList.h>

#include <array>
#include <cstddef>
#include <string>

/**
    What a client may ask the host to change in the lobby.

    The host owns the lobby: it assigns a seat when a player joins
    (CustomGamePlayers::getChangeEventListForNewPlayer) and it broadcasts the authoritative
    view. A client only ever sends *requests*, and the widgets it can operate decide which
    requests are legitimate:

      - it can claim an open seat or an unowned bot seat without displacing a human
        (CustomGamePlayers::onClickPlayerDropDownBox sends SetHumanPlayer with its own name);
      - the house, team and partner-slot drop-downs are only enabled for the house row
        the client currently occupies (CustomGamePlayers.cpp, the bIsThisPlayer loops), so
        ChangeHouse, ChangeTeam and ChangePlayer are legitimate only for a house where the
        sender holds a seat. ChangeColor retains the same ownership rule for setup
        compatibility, although the colour selector has been removed from the menu;
      - the spice income selector is never enabled on a client, not even for its own row, so
        ChangeSpiceIncome from a client is always refused.

    Anything else - seating another player, renaming a seat, changing a house the sender does
    not occupy - is not something the client UI can produce, so the host refuses it. The whole
    transaction is judged before anything is applied, so a list that mixes a legal and an
    illegal event changes nothing.

    This is authorization, not authentication: the sender identity comes from the connection's
    bound peer name, which the ENet transport does not cryptographically prove.
*/
namespace LobbyAuthorization {

/// What currently occupies one player slot.
enum class SlotKind {
    Open,       ///< free, anybody may take it
    Closed,     ///< host closed it; nobody may take it
    AI,         ///< a bot the host placed
    Human       ///< a human player, identified by name
};

struct SlotState {
    SlotKind    kind = SlotKind::Open;
    std::string name;   ///< only meaningful for SlotKind::Human
};

/// The lobby as the host currently sees it. Slot i belongs to house i/2.
struct SeatSnapshot {
    int numHouses = 0;
    bool multiplePlayersPerHouse = false;
    std::array<SlotState, static_cast<std::size_t>(MAX_CUSTOM_GAME_PLAYERS) * 2> slots;

    /// \return the number of slots that exist in this lobby
    int slotCount() const {
        return numHouses * 2;
    }

    /**
        \param  name    the player to look for
        \return true if that player holds any seat
    */
    bool isSeated(const std::string& name) const {
        for(int slot = 0; slot < slotCount(); slot += multiplePlayersPerHouse ? 1 : 2) {
            if(slots[static_cast<std::size_t>(slot)].kind == SlotKind::Human
               && slots[static_cast<std::size_t>(slot)].name == name) {
                return true;
            }
        }
        return false;
    }

    /**
        \param  name        the player to look for
        \param  houseIndex  the house row
        \return true if that player holds one of the two seats of this house
    */
    bool occupiesHouse(const std::string& name, int houseIndex) const {
        if(houseIndex < 0 || houseIndex >= numHouses) {
            return false;
        }
        for(int offset = 0; offset < (multiplePlayersPerHouse ? 2 : 1); offset++) {
            const std::size_t slot = static_cast<std::size_t>(houseIndex * 2 + offset);
            if(slots[slot].kind == SlotKind::Human && slots[slot].name == name) {
                return true;
            }
        }
        return false;
    }
};

/// Why a requested lobby change was refused.
enum class Decision {
    Allow,
    RejectUnknownSender,    ///< the sender holds no seat in this lobby
    RejectSlotOutOfRange,   ///< the slot does not exist in this lobby
    RejectForeignName,      ///< a seat claim carrying somebody else's name
    RejectClosedSeat,       ///< the host closed that seat
    RejectNotYourHouse,     ///< changing a house the sender does not occupy
    RejectHostOnly,         ///< an event only the host may originate
    RejectOccupiedSeat,     ///< another human or their support slot is already assigned
    RejectTooManyClaims     ///< more than one seat claim in one transaction
};

/// The host may configure unoccupied houses; each human owns their own partner/bot slot.
/// No selector may replace a human, including the local player, with a bot.
inline bool mayConfigurePlayerSlot(const SeatSnapshot& snapshot, const std::string& name,
                                  Uint32 slot, bool isHost) {
    if(snapshot.numHouses <= 0 || snapshot.numHouses > MAX_CUSTOM_GAME_PLAYERS
       || slot >= static_cast<Uint32>(snapshot.slotCount())
       || (!snapshot.multiplePlayersPerHouse && slot % 2 == 1)
       || snapshot.slots[slot].kind == SlotKind::Human) return false;
    const int house = static_cast<int>(slot / 2);
    if(snapshot.occupiesHouse(name, house)) return true;
    return isHost && snapshot.slots[house * 2].kind != SlotKind::Human
                  && (!snapshot.multiplePlayersPerHouse || snapshot.slots[house * 2 + 1].kind != SlotKind::Human);
}

/**
    Judges one event a client sent to the host.
    \param  snapshot    the host's current view of the lobby
    \param  senderName  the bound peer name of the connection the event arrived on
    \param  event       the requested change
    \return Allow, or the reason the whole transaction must be dropped
*/
inline Decision authorizeClientEvent(const SeatSnapshot& snapshot, const std::string& senderName,
                                     const ChangeEventList::ChangeEvent& event) {
    if(senderName.empty() || snapshot.numHouses <= 0
       || snapshot.numHouses > MAX_CUSTOM_GAME_PLAYERS) {
        return Decision::RejectUnknownSender;
    }

    const Uint32 slot = event.slot;

    switch(event.eventType) {
        case ChangeEventList::ChangeEvent::EventType::SetHumanPlayer: {
            // A seat claim: any seat that exists and is not closed, but only ever for the
            // sender itself. The host assigns the first seat; this is how a player moves.
            if(slot >= static_cast<Uint32>(snapshot.slotCount())) {
                return Decision::RejectSlotOutOfRange;
            }
            if(event.newStringValue != senderName) {
                return Decision::RejectForeignName;
            }
            if(snapshot.slots[slot].kind == SlotKind::Closed) {
                return Decision::RejectClosedSeat;
            }
            if(!snapshot.multiplePlayersPerHouse && (slot % 2) == 1) {
                // The second seat of a house does not exist in this lobby.
                return Decision::RejectSlotOutOfRange;
            }
            const auto& target = snapshot.slots[slot];
            if((target.kind == SlotKind::Human && target.name != senderName)
               || (target.kind == SlotKind::AI
                   && !mayConfigurePlayerSlot(snapshot, senderName, slot, true))) {
                return Decision::RejectOccupiedSeat;
            }
            return Decision::Allow;
        }

        case ChangeEventList::ChangeEvent::EventType::ChangePlayer: {
            // Setting the other seat of the sender's own house to open/closed/a bot.
            if(slot >= static_cast<Uint32>(snapshot.slotCount())) {
                return Decision::RejectSlotOutOfRange;
            }
            if(!snapshot.multiplePlayersPerHouse && slot % 2 == 1) {
                return Decision::RejectSlotOutOfRange;
            }
            if(!snapshot.isSeated(senderName)) {
                return Decision::RejectUnknownSender;
            }
            if(!snapshot.occupiesHouse(senderName, static_cast<int>(slot) / 2)) {
                return Decision::RejectNotYourHouse;
            }
            if(!mayConfigurePlayerSlot(snapshot, senderName, slot, false) || event.newValue == 0) {
                return Decision::RejectOccupiedSeat;
            }
            return Decision::Allow;
        }

        case ChangeEventList::ChangeEvent::EventType::ChangeHouse:
        case ChangeEventList::ChangeEvent::EventType::ChangeTeam:
        case ChangeEventList::ChangeEvent::EventType::ChangeColor:
        case ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin: {
            // House-level settings: the slot is the house row itself.
            if(slot >= static_cast<Uint32>(snapshot.numHouses)) {
                return Decision::RejectSlotOutOfRange;
            }
            if(!snapshot.isSeated(senderName)) {
                return Decision::RejectUnknownSender;
            }
            if(!snapshot.occupiesHouse(senderName, static_cast<int>(slot))) {
                return Decision::RejectNotYourHouse;
            }
            return Decision::Allow;
        }

        case ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome:
            // The spice income factor is a match rule, not a seat preference: it decides how
            // much every house earns, so only the host sets it - including for the row the
            // sender occupies. The client's own selector is disabled, so its UI cannot produce
            // this event at all; one arriving over the wire is a peer that was modified.
            // Deliberately *not* folded into the own-house case above.
            return Decision::RejectHostOnly;

        default:
            return Decision::RejectHostOnly;
    }
}

/**
    Judges a complete transaction. Nothing is applied unless every event is allowed, and one
    transaction may contain at most one seat claim - the client UI sends one action at a time
    (a house change sends ChangeHouse plus ChangeColor together, which is allowed).
    \param  snapshot    the host's current view of the lobby
    \param  senderName  the bound peer name of the connection the events arrived on
    \param  events      the requested changes, in order
    \param  refused     set to the first refused event index when the result is not Allow
    \return Allow, or the reason the whole transaction must be dropped
*/
inline Decision authorizeClientTransaction(const SeatSnapshot& snapshot,
                                           const std::string& senderName,
                                           const std::list<ChangeEventList::ChangeEvent>& events,
                                           std::size_t& refused) {
    refused = 0;
    std::size_t index = 0;
    int seatClaims = 0;

    for(const ChangeEventList::ChangeEvent& event : events) {
        const Decision decision = authorizeClientEvent(snapshot, senderName, event);
        if(decision != Decision::Allow) {
            refused = index;
            return decision;
        }
        if(event.eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer) {
            seatClaims++;
            if(seatClaims > 1) {
                refused = index;
                return Decision::RejectTooManyClaims;
            }
        }
        index++;
    }

    return Decision::Allow;
}

/**
    \param  decision    the result of an authorization call
    \return a short, stable description for the log
*/
inline const char* describeDecision(Decision decision) {
    switch(decision) {
        case Decision::Allow:                return "allowed";
        case Decision::RejectUnknownSender:  return "sender holds no seat";
        case Decision::RejectSlotOutOfRange: return "slot does not exist";
        case Decision::RejectForeignName:    return "seat claim for another player";
        case Decision::RejectClosedSeat:     return "seat is closed";
        case Decision::RejectNotYourHouse:   return "house not occupied by sender";
        case Decision::RejectHostOnly:       return "host-only change";
        case Decision::RejectOccupiedSeat:   return "seat belongs to another player or is human";
        case Decision::RejectTooManyClaims:  return "more than one seat claim";
        default:                             return "refused";
    }
}

} // namespace LobbyAuthorization

#endif // LOBBYAUTHORIZATION_H
