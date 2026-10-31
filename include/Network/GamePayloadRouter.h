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

#ifndef GAMEPAYLOADROUTER_H
#define GAMEPAYLOADROUTER_H

/**
    The half of the receive path that is the same on every transport.

    Everything that reads a game packet's payload, validates it and hands it to the game lives
    here, once. The ENet mesh and the room relay both call it. What stays outside is the part
    that is genuinely transport specific: the ENet mesh handshake (CONNECT, DISCONNECT,
    PEER_CONNECTED) and mod transfers, neither of which exists on the relay.

    The point of sharing is that a validation fix lands on both transports at the same time.
*/

#include <Network/ChangeEventList.h>
#include <Network/CommandList.h>
#include <Network/NetworkPacketTypes.h>

#include <misc/InputStream.h>
#include <misc/SDL2pp.h>

#include <functional>
#include <set>
#include <string>

class GameInitSettings;
namespace coop { struct Context; struct ControlMessage; }

/**
    The callbacks the game installs on its network session. Held by pointer so both transports
    can share one set without either of them copying std::function objects per packet.
*/
struct NetworkSessionCallbacks {
    // Called only after complete game-info validation, before handing it to the lobby.
    std::function<void ()> onGameInfoAccepted;
    const std::function<void (const std::string&, const std::string&)>*           onReceiveChatMessage = nullptr;
    const std::function<void (const GameInitSettings&, const ChangeEventList&)>*  onReceiveGameInfo = nullptr;
    const std::function<void (const std::string&, const ChangeEventList&)>*                           onReceiveChangeEventList = nullptr;
    const std::function<void (unsigned int)>*                                     onStartGame = nullptr;
    const std::function<void (const std::string&, const CommandList&)>*           onReceiveCommandList = nullptr;
    const std::function<void (const std::string&, const std::set<Uint32>&, int)>* onReceiveSelectionList = nullptr;
    const std::function<void (Uint32, Uint32, float, float, Uint32, Uint32)>*     onReceiveClientStats = nullptr;
    const std::function<void (size_t, Uint32)>*                                   onReceiveSetPathBudget = nullptr;
    /// Host's authoritative match state: (revision, speed, pauseCycle, resumedPauseCycle).
    const std::function<void (Uint32, Uint32, Uint32, Uint32)>*                   onReceiveMatchControl = nullptr;
    /**
        A peer asks the host to leave a pause: (peer name, pauseCycle).

        The name is the one bound to the connection the request arrived on, never one the
        sender supplied, so the game can check it against its own active-player roster.
    */
    const std::function<void (const std::string&, Uint32)>*                       onReceiveMatchResumeRequest = nullptr;
    /// Campaign continuation chosen by the host; empty settings mean "leave the campaign".
    const std::function<void (const GameInitSettings&)>*                          onReceiveCoopMission = nullptr;
    const std::function<void (const coop::ControlMessage&)>* onReceiveCoopControl = nullptr;
    const std::function<void (Uint32, Uint32, Uint32, Uint32, const std::string&)>* onJoinSync = nullptr;
    /**
        A peer's game content does not match ours and this transport cannot fix that.

        The lobby uses it to stop the match starting and to say why. It is not called on the mesh
        transport, where a mismatch is the expected prelude to a mod transfer.
    */
    const std::function<void (const std::string&)>*                               onConfigMismatch = nullptr;
};

/**
    One peer, as the shared payload handling needs to see it.

    Deliberately has no address, no port and no socket: an implementation of this interface for
    the relay could not expose one, and the shared code therefore cannot grow a dependency on
    something only the ENet mesh has.
*/
class GamePayloadPeer {
public:
    virtual ~GamePayloadPeer() = default;

    /// Stable identity for this connection. Never an address hash.
    virtual Uint32 clientId() const = 0;

    virtual const std::string& name() const = 0;
    virtual bool nameAssigned() const = 0;

    /**
        Binds this peer's name, once.
        \return false if the name was refused (already bound, or taken by another player); the
                implementation has already dealt with the consequence.
    */
    virtual bool bindName(const std::string& newName) = 0;

    /// True when this peer is the designated host of the session.
    virtual bool isHostPeer() const = 0;

    /// Records a refused payload against this peer and applies the abuse policy.
    virtual void refuse(const char* reason) = 0;

    /// Sends this peer our own protocol version, game version and content hashes.
    virtual void replyConfigHash() = 0;

    virtual std::string& gameVersion() = 0;
    virtual std::string& quantBotConfigHash() = 0;
    virtual std::string& objectDataHash() = 0;
    virtual std::string& modRevisionHash() = 0;

    /// Ends the session with this peer, using a NETWORKDISCONNECT_* cause.
    virtual void disconnectWithCause(int cause) = 0;
};

/// What the local process is doing right now.
struct GamePayloadContext {
    bool   isHost         = false;
    bool   inGame         = false;
    Uint32 simulationSeed = 0;
    /**
        Whether a received multiplayer map may be written into the user's maps directory.

        The ENet path does this so a custom map can be replayed later. The relay path does not:
        relay v1 plays bundled, manifest-matched content only, so the map text is used from
        memory and never becomes a file.
    */
    bool   allowMapWrite  = true;
    /**
        Whether the peer that sent a campaign continuation is this client's only remote partner.

        The existing same-house campaign keeps its two-player restriction. Separate-house
        Tornie-family campaigns instead provide an explicit authenticated campaign context.
        The caller owns peer-count and transport bookkeeping.
    */
    bool   coopPartnerIsSolePeer = false;
    /// Set only by an active family [COOP] runtime on a direct transport, never the relay.
    const coop::Context* coopCampaign = nullptr;
    Uint32 coopEpoch = 0;
    /**
        Whether a client answers a received config hash with its own.

        True on the mesh, where the exchange is a request/response pair. False on the relay,
        where the host may declare the match started in the same breath as it sends its hashes:
        a reply sent a moment later would arrive after the room had left the lobby and be
        refused. The relay client sends its hashes once when it enters the lobby instead.
    */
    bool   replyToConfigHash = true;
    /**
        Whether a content mismatch is fatal to the session rather than something to resolve.

        On the mesh a mismatch is expected: the host announces its mod and the client downloads
        it, so reporting it as an error would break a working flow. On the relay there is no
        content transfer at all, so a mismatch is final and has to stop the match rather than
        appear only in a log.
    */
    bool   contentMustMatch = false;
};

namespace GamePayloadRouter {

/**
    Handles one game packet payload.

    The packet id has already been read from `stream`, and the caller has already applied
    NetworkPacketPolicy::classifyPacket() to decide that this peer is allowed to send this packet
    at all. What happens here is the payload validation and the hand-off to the game.

    \param  packetType  the packet id
    \param  stream      positioned immediately after the packet id
    \param  peer        the peer the packet arrived from
    \param  context     local role, phase and simulation seed
    \param  callbacks   the game's callbacks
    \return true if this packet id belongs to the shared set and was dealt with here
*/
bool handle(Uint32 packetType, InputStream& stream, GamePayloadPeer& peer,
            const GamePayloadContext& context, const NetworkSessionCallbacks& callbacks);

/**
    True if handle() would claim this packet id.

    Inline on purpose: it is a table, and both transports and the tests need it without dragging
    in the game headers that handle() itself needs.
*/
inline bool handles(Uint32 packetType) {
    switch(packetType) {
        case NETWORKPACKET_SENDGAMEINFO:
        case NETWORKPACKET_SENDNAME:
        case NETWORKPACKET_CHATMESSAGE:
        case NETWORKPACKET_CHANGEEVENTLIST:
        case NETWORKPACKET_CONFIG_HASH:
        case NETWORKPACKET_JOIN_SYNC:
        case NETWORKPACKET_JOIN_ACK:
        case NETWORKPACKET_COOP_MISSION:
        case NETWORKPACKET_COOP_CAMPAIGN_CONTROL:
        case NETWORKPACKET_STARTGAME:
        case NETWORKPACKET_COMMANDLIST:
        case NETWORKPACKET_SELECTIONLIST:
        case NETWORKPACKET_CLIENTSTATS:
        case NETWORKPACKET_SETPATHBUDGET:
        case NETWORKPACKET_MATCH_CONTROL:
        case NETWORKPACKET_MATCH_RESUME_REQUEST:
        case NETWORKPACKET_KEEPALIVE:
            return true;
        default:
            return false;
    }
}

} // namespace GamePayloadRouter

#endif // GAMEPAYLOADROUTER_H
