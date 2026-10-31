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

#ifndef NETWORKPACKETPOLICY_H
#define NETWORKPACKETPOLICY_H

#include <Network/NetworkPacketTypes.h>

#include <misc/SDL2pp.h>
#include <mod/ModTransferValidation.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

/**
    Admission rules for inbound ENet packets.

    These are *not* cryptographic authentication. The legacy ENet transport has no session
    keys and no encryption; a peer's identity here is "which connection did this arrive on and
    what has that connection been admitted to do". What this policy does enforce is that

      - a connection that has not finished the handshake can only drive the handshake,
      - host-only control messages are only honoured on the connection to the designated host,
      - client-only messages are only honoured by a host on an established client connection,
      - lobby-only messages stop being honoured once the match is running, and
      - a peer's identity (its name) is bound once and never re-bound during a match.

    The matrix below is derived from the real mesh handshake in NetworkManager::update() and
    NetworkManager::handlePacket(), so normal joins, late name exchange, co-op mission
    selection and config/mod synchronisation keep working.
*/
/**
 * Reject an incompatible config-hash handshake and dispatch its disconnect cause.
 * Returns true when the peer must be rejected.
 */
template<typename DisconnectFunction>
inline bool rejectIncompatibleNetworkProtocol(Uint32 peerProtocolVersion, DisconnectFunction&& disconnect) {
    if(peerProtocolVersion == NETWORK_PROTOCOL_VERSION) return false;
    disconnect(NETWORKDISCONNECT_PROTOCOL_MISMATCH);
    return true;
}

template<typename DisconnectFunction>
inline bool rejectIncompatibleGameVersion(const std::string& peer, const std::string& local, DisconnectFunction&& disconnect) {
    if (peer == local) return false;
    disconnect(NETWORKDISCONNECT_PROTOCOL_MISMATCH);
    return true;
}

namespace NetworkPacketPolicy {

/// What the local process is in this session.
enum class LocalRole {
    Host,       ///< this process started the server (NetworkManager::isServer())
    Client      ///< this process joined a server
};

/// Which half of the session lifetime we are in.
enum class SessionPhase {
    Lobby,      ///< game not started yet
    InGame      ///< NetworkManager::beginSimulation() has been called
};

/// How far a peer connection has progressed.
enum class PeerAdmission {
    Unidentified,   ///< no PeerData at all - never trusted with anything
    Handshaking,    ///< known connection that has not been moved to the peer list yet
    Established     ///< member of the peer list
};

/// Why a packet was refused (used for logging and for tests).
enum class PacketVerdict {
    Accept,
    RejectUnknownType,      ///< packet id is not part of the protocol
    RejectUnidentifiedPeer, ///< arrived on a connection with no peer state
    RejectPreHandshake,     ///< peer has not completed admission yet
    RejectWrongRole,        ///< client-only packet on a host or vice versa
    RejectNotHostPeer,      ///< host-only control packet from a peer that is not the host
    RejectWrongPhase        ///< lobby-only packet during a match (or vice versa)
};

struct PacketContext {
    Uint32          packetType      = 0;
    LocalRole       localRole       = LocalRole::Client;
    SessionPhase    phase           = SessionPhase::Lobby;
    PeerAdmission   admission       = PeerAdmission::Unidentified;
    bool            isHostConnection = false;   ///< peer == connectPeer (only meaningful on a client)
};

/**
    Central admission check. Every inbound packet goes through this before any of its payload
    is interpreted.
    \param  context the packet id together with the state of the connection it arrived on
    \return Accept, or the reason the packet must be dropped
*/
inline PacketVerdict classifyPacket(const PacketContext& context) {
    const bool isHost      = (context.localRole == LocalRole::Host);
    const bool inGame      = (context.phase == SessionPhase::InGame);
    const bool established = (context.admission == PeerAdmission::Established);
    const bool identified  = (context.admission != PeerAdmission::Unidentified);
    // On a client, "from the host" means the connection we opened to the server. A host has no
    // host connection of its own, so host-only packets are never acceptable there.
    const bool fromHost    = (!isHost && context.isHostConnection);

    switch(context.packetType) {
        case NETWORKPACKET_KEEPALIVE:
            // Costless and sent from the moment a connection exists.
            return identified ? PacketVerdict::Accept : PacketVerdict::RejectUnidentifiedPeer;

        case NETWORKPACKET_SENDNAME:
            // The name exchange is part of admission itself, so it must be allowed while
            // handshaking - but never once the match is running (identity freeze).
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_CONFIG_HASH:
            // Exchanged during admission and again whenever the lobby re-verifies configs.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_SENDGAMEINFO:
            // Host -> joining client, completes the client side of the handshake.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_CONNECT:
            // Host -> client: "open a mesh connection to this address".
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_DISCONNECT:
            // Only the host tells anybody to drop a peer.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            return PacketVerdict::Accept;

        case NETWORKPACKET_PEER_CONNECTED:
            // Client -> host: "I reached the new peer"; host -> clients: "everybody reached it".
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            if(isHost)       return established ? PacketVerdict::Accept
                                                : PacketVerdict::RejectPreHandshake;
            return fromHost ? PacketVerdict::Accept : PacketVerdict::RejectNotHostPeer;

        case NETWORKPACKET_CHATMESSAGE:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            return PacketVerdict::Accept;

        case NETWORKPACKET_CHANGEEVENTLIST:
            // Lobby slot changes: clients only accept the host's authoritative view.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(isHost)       return PacketVerdict::Accept;
            return fromHost ? PacketVerdict::Accept : PacketVerdict::RejectNotHostPeer;

        case NETWORKPACKET_JOIN_ACK:
            if(!identified) return PacketVerdict::RejectUnidentifiedPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            return isHost ? PacketVerdict::Accept : PacketVerdict::RejectWrongRole;
        case NETWORKPACKET_JOIN_SYNC:
        case NETWORKPACKET_COOP_MISSION:
            // Campaign continuation (including the empty settings used to exit) arrives
            // after the previous simulation, while the session is still marked InGame.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            return PacketVerdict::Accept;

        case NETWORKPACKET_COOP_SETUP_TRANSFER:
            // Direct-only checkpoint offers/chunks and their acknowledgments. The session
            // checks each operation's sender; the relay has no carried-message entry for it.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_COOP_CAMPAIGN_CONTROL:
            // Both directions share a wire record. The payload router checks the operation's
            // role and the active separate-house campaign; ordinary matches cannot use it.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_STARTGAME:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            if(!established) return PacketVerdict::RejectPreHandshake;
            return PacketVerdict::Accept;

        case NETWORKPACKET_COMMANDLIST:
        case NETWORKPACKET_SELECTIONLIST:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_CLIENTSTATS:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!isHost)      return PacketVerdict::RejectWrongRole;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_SETPATHBUDGET:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_MATCH_CONTROL:
            // The shared match settings (speed, pause, resume) are the host's to decide, so a
            // client only ever honours them on the connection to the host. Same shape as
            // SETPATHBUDGET, with the established check made explicit: this changes what every
            // peer's simulation loop does.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_MATCH_RESUME_REQUEST:
            // A request, not an order: only the host receives it, and only from a peer that
            // finished admission. Whether the sender is an active human player is the game's
            // decision - this is the transport half of it.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!isHost)      return PacketVerdict::RejectWrongRole;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(!inGame)      return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_MOD_INFO:
        case NETWORKPACKET_MOD_CHUNK:
        case NETWORKPACKET_MOD_COMPLETE:
            // Mod content may only ever come from the host connection, and only in the lobby.
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(isHost)       return PacketVerdict::RejectWrongRole;
            if(!fromHost)    return PacketVerdict::RejectNotHostPeer;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        case NETWORKPACKET_MOD_REQUEST:
        case NETWORKPACKET_MOD_ACK:
            if(!identified)  return PacketVerdict::RejectUnidentifiedPeer;
            if(!isHost)      return PacketVerdict::RejectWrongRole;
            if(!established) return PacketVerdict::RejectPreHandshake;
            if(inGame)       return PacketVerdict::RejectWrongPhase;
            return PacketVerdict::Accept;

        default:
            return PacketVerdict::RejectUnknownType;
    }
}

/**
    \param  verdict the result of classifyPacket()
    \return a short, stable string for the log line
*/
inline const char* describeVerdict(PacketVerdict verdict) {
    switch(verdict) {
        case PacketVerdict::Accept:                 return "accepted";
        case PacketVerdict::RejectUnknownType:      return "unknown packet type";
        case PacketVerdict::RejectUnidentifiedPeer: return "unidentified peer";
        case PacketVerdict::RejectPreHandshake:     return "handshake not complete";
        case PacketVerdict::RejectWrongRole:        return "wrong role for this packet";
        case PacketVerdict::RejectNotHostPeer:      return "not the designated host";
        case PacketVerdict::RejectWrongPhase:       return "wrong session phase";
        default:                                    return "rejected";
    }
}

/// Longest player name accepted off the wire.
constexpr std::size_t kMaxPlayerNameLength = 64;

/**
    Player names end up in the UI, in chat and - through CommandManager::addCommandList() - as
    the key that resolves a command list to a player. Control characters and unbounded lengths
    have no legitimate use there.
    \param  name    the name as received
    \return true if the name may be bound to a peer
*/
inline bool isAcceptablePlayerName(const std::string& name) {
    if(name.empty() || name.size() > kMaxPlayerNameLength) {
        return false;
    }
    for(const unsigned char c : name) {
        if(c < 32 || c == 127) {
            return false;
        }
    }
    return true;
}

/// Lowest UDP port a mesh introduction may name.
constexpr Uint16 kMinMeshTargetPort = 1024;

/**
    A mesh CONNECT tells this client to open a UDP connection to an address of the host's
    choosing. That is a useful primitive for an attacker, so obviously non-peer destinations
    are refused. Loopback and private ranges stay allowed: LAN games and local testing need
    them.
    \param  hostOrderAddress    the IPv4 address in host byte order
    \param  port                the UDP port
    \return true if a mesh connection to this address is plausible
*/
inline bool isPlausibleMeshTarget(Uint32 hostOrderAddress, Uint16 port) {
    if(port < kMinMeshTargetPort) {
        // Game ports are ephemeral or the default 28747; the well-known range below 1024 only
        // makes this useful as a way to poke at local or private services.
        return false;
    }

    const Uint32 firstOctet = (hostOrderAddress >> 24) & 0xFF;
    if(firstOctet == 0) {
        return false;           // 0.0.0.0/8 "this network"
    }
    if(firstOctet >= 224) {
        return false;           // multicast, reserved and 255.255.255.255
    }
    return true;
}

/// Largest multiplayer map file accepted from a host (INI maps are a few tens of KiB).
constexpr std::size_t kMaxReceivedMapSize = 1024 * 1024;

/**
    Validates the filename of a map received with SENDGAMEINFO and produces the name that may
    be written into the user's maps/multiplayer directory. The name has to be a single portable
    path component so it cannot escape that directory, and it must carry the .ini extension the
    map loader expects.
    \param  filename        the filename as received
    \param  sanitizedName   set to the filename to use on success
    \return true if the map may be written to disk
*/
inline bool sanitizeReceivedMapFilename(const std::string& filename, std::string& sanitizedName) {
    if(filename.empty() || filename.size() > 128) {
        return false;
    }
    if(filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos
       || filename.find('\0') != std::string::npos) {
        return false;
    }
    if(!ModTransferValidation::isPortablePathComponent(filename)) {
        return false;
    }

    std::string candidate = filename;
    const bool hasIniSuffix = candidate.size() >= 4
        && candidate.compare(candidate.size() - 4, 4, ".ini") == 0;
    if(!hasIniSuffix) {
        candidate += ".ini";
        // Appending must not produce something non-portable either (e.g. a reserved stem).
        if(candidate.size() > 132 || !ModTransferValidation::isPortablePathComponent(candidate)) {
            return false;
        }
    }

    sanitizedName = candidate;
    return true;
}

/**
    Client performance statistics are advisory numbers that feed the host's path-budget
    decision. NaN or infinity there poisons every later comparison, so they are refused at the
    boundary.
    \param  value   a float taken from a CLIENTSTATS packet
    \return true if the value can be used
*/
inline bool isUsableStatValue(float value) {
    // Older Release builds used -ffast-math, which lets the compiler
    // assume floating-point operands are finite - std::isfinite() is then folded away, and a
    // mixed check still leaves a floating-point comparison the optimiser can fold back into
    // the same assumption. The test is therefore entirely integer: reject any exponent of all
    // ones (infinity and NaN) and any negative value other than -0.0.
    static_assert(sizeof(float) == sizeof(Uint32), "Network stats require binary32 floats");
    Uint32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const Uint32 magnitude = bits & 0x7fffffffu;
    return magnitude < 0x7f800000u && ((bits & 0x80000000u) == 0 || magnitude == 0);
}

/**
    True when a refusal is the expected result of two peers changing phase at slightly
    different times rather than a peer misbehaving.

    Clients start their countdown half a round trip before the host and campaign co-op moves
    between missions, so command, selection, stats and late lobby packets can legitimately be
    in flight across a phase boundary. They must still be dropped - the phase gate is what
    protects the receiver - but they must not count towards the abuse budget, or an honest
    session can disconnect itself. A flood of them is still bounded by the per-peer packet
    rate and byte budgets.
    \param  verdict the result of classifyPacket()
    \return true if the refusal should be dropped quietly
*/
inline bool isExpectedOrderingRefusal(PacketVerdict verdict) {
    return verdict == PacketVerdict::RejectWrongPhase;
}

/// Length of the window the traffic budgets below are measured over.
constexpr Uint32 kTrafficWindowMs = 1000;
/// Packets one peer may deliver per window. A full 12 MiB transfer uses 192 chunks and
/// in-game traffic is a few dozen per second.
constexpr Uint32 kMaxPacketsPerWindow = 4096;
/// Bytes one peer may deliver per window outside a mod transfer. Gameplay traffic is a few
/// KiB/s; the headroom covers a full-size map packet arriving with the rest of the handshake.
constexpr Uint64 kMaxPeerBytesPerWindow = 8ull * 1024 * 1024;
/// Raised byte budget while this peer is delivering a mod transfer that was asked for. A
/// complete 12 MiB transfer arriving in a single burst (loopback or LAN) stays inside it.
constexpr Uint64 kMaxModTransferBytesPerWindow = 24ull * 1024 * 1024;
/// Largest selection a peer may announce. A player can box-select an army, so this is well
/// above any real control group, but it is bounded: the set is copied into the receiving
/// player's selection lists.
constexpr std::size_t kMaxSelectionSize = 2048;
/// Refused packets in one burst that justify dropping a peer.
constexpr Uint32 kMaxRefusalsPerBurst = 64;
/// Gap after which a burst of refusals is considered over.
constexpr Uint32 kRefusalDecayMs = 10000;

/**
    Fixed-window accounting for "how much has this peer sent recently", used for both the
    packet count and the aggregate byte budget. Transport independent on purpose: the same
    accounting applies to any future transport.
*/
struct RateWindow {
    Uint32 windowStart = 0;     ///< start of the current window, in SDL_GetTicks() milliseconds
    Uint64 amount = 0;          ///< amount accumulated inside the current window

    /**
        Adds to the current window, starting a new one when the old one has elapsed.
        \param  nowMs       current time in milliseconds
        \param  amountToAdd packets or bytes this call accounts for
        \param  budget      most that may be accumulated inside one window
        \param  windowMs    length of the window
        \return true if the peer is still inside its budget
    */
    bool accept(Uint32 nowMs, Uint64 amountToAdd, Uint64 budget, Uint32 windowMs) {
        if(windowMs == 0) {
            return true;
        }
        if(nowMs - windowStart >= windowMs) {
            windowStart = nowMs;
            amount = 0;
        }
        // Saturating: a pathological amount must not wrap the accumulator back under budget.
        if(amountToAdd > (0xFFFFFFFFFFFFFFFFull - amount)) {
            amount = 0xFFFFFFFFFFFFFFFFull;
        } else {
            amount += amountToAdd;
        }
        return amount <= budget;
    }
};

/**
    Abuse accounting for refused packets, with a decay so isolated refusals never accumulate,
    and a one-shot disconnect so a peer being dropped is logged once and its queued packets
    are not parsed again.
*/
struct RefusalCounter {
    Uint32 refusals = 0;
    Uint32 lastRefusalTime = 0;
    bool   disconnecting = false;

    /// \return true once a drop has been started for this peer
    bool isDisconnecting() const { return disconnecting; }

    /**
        Marks this peer as being dropped.
        \return true the first time only, so the caller logs and requests the disconnect once
    */
    bool beginDisconnect() {
        if(disconnecting) {
            return false;
        }
        disconnecting = true;
        return true;
    }

    /**
        Counts one refused packet.
        \param  nowMs       current time in milliseconds
        \param  maxRefusals refusals in one burst that justify a disconnect
        \param  decayMs     gap after which the burst is considered over
        \return true when this refusal crosses the threshold
    */
    bool noteRefusal(Uint32 nowMs, Uint32 maxRefusals, Uint32 decayMs) {
        if(disconnecting) {
            return false;
        }
        if(lastRefusalTime != 0 && (nowMs - lastRefusalTime) > decayMs) {
            refusals = 0;
        }
        lastRefusalTime = nowMs;
        refusals++;
        return refusals >= maxRefusals;
    }
};

} // namespace NetworkPacketPolicy

#endif // NETWORKPACKETPOLICY_H
