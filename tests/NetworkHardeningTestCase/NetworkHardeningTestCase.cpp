/*
 *  NetworkHardeningTestCase.cpp - regression tests for the network trust boundary
 *
 *  These tests drive the same functions and parsers the production receive path uses:
 *  NetworkPacketPolicy::classifyPacket() (called from NetworkManager::admitPacket),
 *  CommandValidation (called from CommandManager::addCommandList and Command's stream
 *  constructor), PathBudgetSync (called from Game::handleSetPathBudget), the real
 *  ENetPacketIStream over real ENet packets, and the real ChangeEventList parser.
 *
 *  Packets are built as raw bytes wherever a hostile sender would, so the fixtures are the
 *  malformed wire images themselves rather than a description of them.
 */

#include <catch2/catch_all.hpp>
#include <misc/CampaignControls.h>
#include <misc/FeedbackIssue.h>

#include <CommandAuthorization.h>
#include <CommandValidation.h>
#include <DataTypes.h>
#include <Definitions.h>
#include <GameInitSettings.h>
#include <Menu/LobbyAuthorization.h>
#include <Network/ChangeEventList.h>
#include <Network/GameInitSettingsPolicy.h>
#include <Network/ENetPacketIStream.h>
#include <Network/ENetPacketOStream.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/PathBudgetSync.h>
#include <Network/ObserverStreamPolicy.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModTransferValidation.h>

#include <enet/enet.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <string>
#include <vector>

TEST_CASE("Spectator checkpoint window accepts cumulative progress without trusting unsent offsets", "[network][spectator]") {
    using namespace ObserverStreamPolicy;
    const auto total=5*chunkBytes+17;
    CHECK(canSendChunk(0,3*chunkBytes,total));
    CHECK_FALSE(canSendChunk(0,4*chunkBytes,total));
    CHECK(validChunkAck(0,4*chunkBytes,total,chunkBytes));
    CHECK(canSendChunk(chunkBytes,4*chunkBytes,total));
    CHECK(validChunkAck(chunkBytes,4*chunkBytes,total,4*chunkBytes));
    CHECK_FALSE(validChunkAck(chunkBytes,4*chunkBytes,total,chunkBytes));
    CHECK_FALSE(validChunkAck(chunkBytes,4*chunkBytes,total,0));
    CHECK_FALSE(validChunkAck(chunkBytes,4*chunkBytes,total,5*chunkBytes));
    CHECK_FALSE(validChunkAck(0,4*chunkBytes,total,chunkBytes+1));
    CHECK_FALSE(validChunkAck(0,~std::uint32_t(0),total,chunkBytes));
    CHECK(validChunkAck(5*chunkBytes,total,total,total));
    CHECK_FALSE(canSendChunk(total,total,total));
    CHECK_FALSE(canSendChunk(chunkBytes,0,total));
}

using NetworkPacketPolicy::LocalRole;
using NetworkPacketPolicy::PacketContext;
using NetworkPacketPolicy::PacketVerdict;
using NetworkPacketPolicy::PeerAdmission;
using NetworkPacketPolicy::SessionPhase;

namespace {

struct ENetRuntime {
    ENetRuntime() {
        if(enet_initialize() != 0) {
            throw std::runtime_error("Failed to initialize ENet");
        }
    }
    ~ENetRuntime() { enet_deinitialize(); }
};

/// Builds a raw wire image the way a hostile peer would.
class PacketBuilder {
public:
    PacketBuilder& u8(Uint8 value) {
        bytes.push_back(value);
        return *this;
    }

    PacketBuilder& u16(Uint16 value) {
        bytes.push_back(static_cast<Uint8>(value & 0xFF));
        bytes.push_back(static_cast<Uint8>((value >> 8) & 0xFF));
        return *this;
    }

    PacketBuilder& u32(Uint32 value) {
        for(int i = 0; i < 4; i++) {
            bytes.push_back(static_cast<Uint8>((value >> (8 * i)) & 0xFF));
        }
        return *this;
    }

    PacketBuilder& u64(Uint64 value) {
        for(int i = 0; i < 8; i++) {
            bytes.push_back(static_cast<Uint8>((value >> (8 * i)) & 0xFF));
        }
        return *this;
    }

    PacketBuilder& raw(const std::string& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
        return *this;
    }

    /// Length-prefixed string, exactly as ENetPacketOStream::writeString() encodes it.
    PacketBuilder& str(const std::string& value) {
        u32(static_cast<Uint32>(value.size()));
        return raw(value);
    }

    /// A string header with a length that does not describe the bytes that follow.
    PacketBuilder& lyingStringLength(Uint32 claimedLength, const std::string& actualBytes) {
        u32(claimedLength);
        return raw(actualBytes);
    }

    ENetPacket* build() const {
        ENetPacket* packet = enet_packet_create(bytes.empty() ? nullptr : bytes.data(),
                                                bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        REQUIRE(packet != nullptr);
        return packet;
    }

private:
    std::vector<Uint8> bytes;
};

PacketContext context(Uint32 packetType, LocalRole role, SessionPhase phase,
                      PeerAdmission admission, bool isHostConnection) {
    PacketContext ctx;
    ctx.packetType = packetType;
    ctx.localRole = role;
    ctx.phase = phase;
    ctx.admission = admission;
    ctx.isHostConnection = isHostConnection;
    return ctx;
}

} // namespace

// =============================================================================
// Packet admission: pre-handshake, forged host messages, role and phase
// =============================================================================

TEST_CASE("Admission: a peer that has not completed the handshake can only drive the handshake",
          "[network][security][admission]") {
    const Uint32 handshakePackets[] = {
        NETWORKPACKET_SENDNAME, NETWORKPACKET_CONFIG_HASH, NETWORKPACKET_KEEPALIVE
    };
    for(const Uint32 packetType : handshakePackets) {
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::Lobby,
                            PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    }

    const Uint32 refusedBeforeHandshake[] = {
        NETWORKPACKET_CHATMESSAGE, NETWORKPACKET_CHANGEEVENTLIST, NETWORKPACKET_COMMANDLIST,
        NETWORKPACKET_SELECTIONLIST, NETWORKPACKET_CLIENTSTATS, NETWORKPACKET_MOD_REQUEST,
        NETWORKPACKET_MOD_ACK, NETWORKPACKET_PEER_CONNECTED
    };
    for(const Uint32 packetType : refusedBeforeHandshake) {
        const PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(
            context(packetType, LocalRole::Host, SessionPhase::Lobby,
                    PeerAdmission::Handshaking, false));
        INFO("packet type " << packetType);
        REQUIRE(verdict != PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: a connection without peer state is never obeyed",
          "[network][security][admission]") {
    for(Uint32 packetType = 0; packetType <= NETWORKPACKET_JOIN_ACK; packetType++) {
        const PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(
            context(packetType, LocalRole::Client, SessionPhase::Lobby,
                    PeerAdmission::Unidentified, true));
        INFO("packet type " << packetType);
        REQUIRE(verdict != PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: host-only control messages are refused from a peer that is not the host",
          "[network][security][admission][forgery]") {
    const Uint32 hostOnlyPackets[] = {
        NETWORKPACKET_STARTGAME, NETWORKPACKET_SETPATHBUDGET, NETWORKPACKET_CONNECT,
        NETWORKPACKET_DISCONNECT, NETWORKPACKET_SENDGAMEINFO, NETWORKPACKET_COOP_MISSION,
        NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_CHUNK, NETWORKPACKET_MOD_COMPLETE, NETWORKPACKET_JOIN_SYNC
    };

    for(const Uint32 packetType : hostOnlyPackets) {
        // Another established mesh peer forging the host's control traffic.
        const bool inGamePacket = (packetType == NETWORKPACKET_SETPATHBUDGET);
        const SessionPhase phase = inGamePacket ? SessionPhase::InGame : SessionPhase::Lobby;

        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, phase,
                            PeerAdmission::Established, false))
                == PacketVerdict::RejectNotHostPeer);

        // The same packet arriving on the host, where there is no host connection at all.
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, phase,
                            PeerAdmission::Established, false))
                == PacketVerdict::RejectWrongRole);

        // And the legitimate case still works.
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, phase,
                            PeerAdmission::Established, true))
                == PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: client-only reports are refused on a client and accepted by the host",
          "[network][security][admission]") {
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CLIENTSTATS, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, true)) == PacketVerdict::RejectWrongRole);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CLIENTSTATS, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);

    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_REQUEST, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::RejectWrongRole);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_ACK, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
}

TEST_CASE("Admission: lobby-only packets stop being accepted once the match runs",
          "[network][security][admission][phase]") {
    const Uint32 lobbyOnly[] = {
        NETWORKPACKET_SENDNAME, NETWORKPACKET_CONFIG_HASH, NETWORKPACKET_CHANGEEVENTLIST,
        NETWORKPACKET_STARTGAME, NETWORKPACKET_SENDGAMEINFO,
        NETWORKPACKET_CONNECT, NETWORKPACKET_MOD_INFO, NETWORKPACKET_MOD_CHUNK,
        NETWORKPACKET_MOD_COMPLETE
    };
    for(const Uint32 packetType : lobbyOnly) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::Lobby,
                            PeerAdmission::Established, true)) == PacketVerdict::Accept);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::InGame,
                            PeerAdmission::Established, true)) == PacketVerdict::RejectWrongPhase);
    }

    // A rename during a match is exactly how a peer would try to take over another player's
    // commands, because command lists are resolved to a player by name.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::RejectWrongPhase);
}

TEST_CASE("Admission: co-op continuation is accepted from the established host after a match",
          "[network][security][admission][phase][compatibility]") {
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COOP_MISSION, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COOP_MISSION, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::RejectNotHostPeer);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COOP_MISSION, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::RejectWrongRole);
}

TEST_CASE("Admission: in-game traffic is refused while still in the lobby",
          "[network][security][admission][phase]") {
    const Uint32 inGameOnly[] = {
        NETWORKPACKET_COMMANDLIST, NETWORKPACKET_SELECTIONLIST, NETWORKPACKET_CLIENTSTATS
    };
    for(const Uint32 packetType : inGameOnly) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::Lobby,
                            PeerAdmission::Established, false)) == PacketVerdict::RejectWrongPhase);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Host, SessionPhase::InGame,
                            PeerAdmission::Established, false)) == PacketVerdict::Accept);
    }
}

TEST_CASE("Admission: unknown packet types are refused", "[network][security][admission]") {
    for(const Uint32 packetType : {0u, 25u, 999u, 0xFFFFFFFFu}) {
        INFO("packet type " << packetType);
        REQUIRE(NetworkPacketPolicy::classifyPacket(
                    context(packetType, LocalRole::Client, SessionPhase::Lobby,
                            PeerAdmission::Established, true)) == PacketVerdict::RejectUnknownType);
    }
}

TEST_CASE("Admission: a normal join, lobby and match sequence is accepted end to end",
          "[network][security][admission][compatibility]") {
    // Client side of a join: the connection to the host starts out handshaking.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDGAMEINFO, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CONFIG_HASH, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    // A mesh peer that connected to us sends its name before it is in the peer list.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    // Co-op mission selection and mod sync from the host.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COOP_MISSION, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_CHUNK, LocalRole::Client, SessionPhase::Lobby,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);
    // Match running: command and selection traffic from any established mesh peer.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COMMANDLIST, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SELECTIONLIST, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SETPATHBUDGET, LocalRole::Client, SessionPhase::InGame,
                        PeerAdmission::Established, true)) == PacketVerdict::Accept);

    // Host side of the same session.
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_SENDNAME, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Handshaking, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_PEER_CONNECTED, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_CHANGEEVENTLIST, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_MOD_REQUEST, LocalRole::Host, SessionPhase::Lobby,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
    REQUIRE(NetworkPacketPolicy::classifyPacket(
                context(NETWORKPACKET_COMMANDLIST, LocalRole::Host, SessionPhase::InGame,
                        PeerAdmission::Established, false)) == PacketVerdict::Accept);
}

// =============================================================================
// Identity, mesh targets, received map names, client stats
// =============================================================================

TEST_CASE("Player names are bounded and free of control characters",
          "[network][security][identity]") {
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName("Stefan"));
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName("Player 1 (host)"));
    REQUIRE(NetworkPacketPolicy::isAcceptablePlayerName(std::string(24, 'a')));

    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(""));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(std::string(65, 'a')));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName(std::string("na\0me", 5)));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName("line\nbreak"));
    REQUIRE_FALSE(NetworkPacketPolicy::isAcceptablePlayerName("bell\x07"));
}

TEST_CASE("Mesh connect targets must be plausible unicast addresses",
          "[network][security][mesh]") {
    const Uint32 loopback = 0x7F000001;     // 127.0.0.1
    const Uint32 privateLan = 0xC0A80105;   // 192.168.1.5
    const Uint32 publicHost = 0x08080808;   // 8.8.8.8

    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(loopback, 28747));
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 28747));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(publicHost, 1));

    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(loopback, 0));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0x00000000, 28747));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0x000000FF, 28747));   // 0.0.0.255
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0xFFFFFFFF, 28747));   // broadcast
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0xE0000001, 28747));   // 224.0.0.1
}

TEST_CASE("Received map filenames cannot escape the multiplayer maps directory",
          "[network][security][map]") {
    std::string sanitized;

    SECTION("legitimate custom maps keep working") {
        REQUIRE(NetworkPacketPolicy::sanitizeReceivedMapFilename("Arrakis Duel.ini", sanitized));
        REQUIRE(sanitized == "Arrakis Duel.ini");

        REQUIRE(NetworkPacketPolicy::sanitizeReceivedMapFilename("4P_Spice_Bowl", sanitized));
        REQUIRE(sanitized == "4P_Spice_Bowl.ini");
    }

    SECTION("traversal, absolute and control names are refused") {
        const char* dangerous[] = {
            "../../../../etc/passwd",
            "..",
            "../evil.ini",
            "maps/../../evil.ini",
            "/etc/cron.d/evil.ini",
            "C:\\Windows\\System32\\evil.ini",
            "sub/dir.ini",
            "back\\slash.ini",
            "CON",
            "LPT1.ini",
            "trailing.",
            "trailing ",
            "bell\x07.ini"
        };
        for(const char* name : dangerous) {
            INFO("filename " << name);
            REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(name, sanitized));
        }

        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(
            std::string("nul\0byte.ini", 12), sanitized));
        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename("", sanitized));
        REQUIRE_FALSE(NetworkPacketPolicy::sanitizeReceivedMapFilename(
            std::string(200, 'a') + ".ini", sanitized));
    }
}

TEST_CASE("Client stat values must be finite and non-negative",
          "[network][security][pathbudget]") {
    REQUIRE(NetworkPacketPolicy::isUsableStatValue(0.0f));
    REQUIRE(NetworkPacketPolicy::isUsableStatValue(59.94f));

    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(std::numeric_limits<float>::infinity()));
    REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(-1.0f));
}

TEST_CASE_METHOD(ENetRuntime, "Wire: runtime non-finite stats are refused in fast-math builds",
                 "[network][security][wire][pathbudget]") {
    // Volatile runtime input prevents constant folding from hiding -ffast-math assumptions.
    volatile Uint32 encoded[] = {0x7f800000u, 0xff800000u, 0x7fc00001u, 0x7f800001u};
    for(unsigned i = 0; i < 4; ++i) {
        ENetPacketOStream output(ENET_PACKET_FLAG_RELIABLE);
        output.writeUint32(encoded[i]);
        ENetPacketIStream input(output.getPacket());
        REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(input.readFloat()));
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: runtime finite stats are still accepted",
                 "[network][security][wire][pathbudget][compatibility]") {
    // Values a real client reports, plus the two signed zeroes and the extremes, all taken
    // from runtime bytes so no constant folding can decide the outcome.
    volatile Uint32 accepted[] = {
        0x00000000u,    // +0.0
        0x80000000u,    // -0.0, produced by an idle counter
        0x42700000u,    // 60.0 fps
        0x3f800000u,    // 1.0 ms
        0x7f7fffffu     // FLT_MAX, finite
    };
    for(unsigned i = 0; i < 5; ++i) {
        ENetPacketOStream output(ENET_PACKET_FLAG_RELIABLE);
        output.writeUint32(accepted[i]);
        ENetPacketIStream input(output.getPacket());
        INFO("bit pattern index " << i);
        REQUIRE(NetworkPacketPolicy::isUsableStatValue(input.readFloat()));
    }

    volatile Uint32 refused[] = {
        0xbf800000u,    // -1.0
        0xff7fffffu,    // -FLT_MAX
        0x80000001u     // smallest negative denormal
    };
    for(unsigned i = 0; i < 3; ++i) {
        ENetPacketOStream output(ENET_PACKET_FLAG_RELIABLE);
        output.writeUint32(refused[i]);
        ENetPacketIStream input(output.getPacket());
        INFO("bit pattern index " << i);
        REQUIRE_FALSE(NetworkPacketPolicy::isUsableStatValue(input.readFloat()));
    }
}

// =============================================================================
// Abuse accounting: refusal one-shot, decay, and traffic budgets
// =============================================================================

TEST_CASE("Abuse: a burst of refusals disconnects exactly once",
          "[network][security][abuse]") {
    NetworkPacketPolicy::RefusalCounter counter;
    const Uint32 now = 100000;

    for(Uint32 i = 1; i < NetworkPacketPolicy::kMaxRefusalsPerBurst; i++) {
        INFO("refusal " << i);
        REQUIRE_FALSE(counter.noteRefusal(now + i, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                          NetworkPacketPolicy::kRefusalDecayMs));
        REQUIRE_FALSE(counter.isDisconnecting());
    }

    // The threshold crossing is reported once...
    REQUIRE(counter.noteRefusal(now + NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                NetworkPacketPolicy::kRefusalDecayMs));
    REQUIRE(counter.beginDisconnect());

    // ...and never again: further packets from this peer are not counted, logged or parsed.
    REQUIRE(counter.isDisconnecting());
    REQUIRE_FALSE(counter.beginDisconnect());
    for(Uint32 i = 0; i < 1000; i++) {
        REQUIRE_FALSE(counter.noteRefusal(now + 1000 + i, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                          NetworkPacketPolicy::kRefusalDecayMs));
    }
    REQUIRE_FALSE(counter.beginDisconnect());
}

TEST_CASE("Abuse: isolated refusals never accumulate into a disconnect",
          "[network][security][abuse][compatibility]") {
    NetworkPacketPolicy::RefusalCounter counter;
    Uint32 now = 50000;

    // Phase transitions and peers leaving produce the odd refusal minutes apart. A thousand of
    // those must never disconnect an honest peer.
    for(int i = 0; i < 1000; i++) {
        now += NetworkPacketPolicy::kRefusalDecayMs + 1;
        REQUIRE_FALSE(counter.noteRefusal(now, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                          NetworkPacketPolicy::kRefusalDecayMs));
    }
    REQUIRE_FALSE(counter.isDisconnecting());
}

TEST_CASE("Abuse: the packet budget passes a mod transfer and stops a flood",
          "[network][security][abuse]") {
    NetworkPacketPolicy::RateWindow window;
    const Uint32 start = 20000;

    // A complete 10 MiB mod transfer is ~160 chunk packets plus handshake traffic.
    for(Uint32 i = 0; i < 256; i++) {
        REQUIRE(window.accept(start, 1, NetworkPacketPolicy::kMaxPacketsPerWindow,
                              NetworkPacketPolicy::kTrafficWindowMs));
    }

    // A flood inside the same window is refused once the budget is used up.
    bool refused = false;
    for(Uint32 i = 0; i < NetworkPacketPolicy::kMaxPacketsPerWindow; i++) {
        if(!window.accept(start, 1, NetworkPacketPolicy::kMaxPacketsPerWindow,
                          NetworkPacketPolicy::kTrafficWindowMs)) {
            refused = true;
            break;
        }
    }
    REQUIRE(refused);

    // The next window starts clean, so an honest peer recovers.
    REQUIRE(window.accept(start + NetworkPacketPolicy::kTrafficWindowMs, 1,
                          NetworkPacketPolicy::kMaxPacketsPerWindow,
                          NetworkPacketPolicy::kTrafficWindowMs));
}

TEST_CASE("Abuse: the byte budget passes a full mod burst and stops bulk garbage",
          "[network][security][abuse]") {
    const Uint64 modTransferBytes = 10ull * 1024 * 1024;     // MAX_MOD_TRANSFER_SIZE
    const Uint64 chunkBytes = 64 * 1024;                     // MOD_CHUNK_SIZE
    const Uint32 start = 30000;

    SECTION("a requested 10 MiB transfer arriving in one burst is accepted") {
        NetworkPacketPolicy::RateWindow window;
        for(Uint64 sent = 0; sent < modTransferBytes; sent += chunkBytes) {
            REQUIRE(window.accept(start, chunkBytes,
                                  NetworkPacketPolicy::kMaxModTransferBytesPerWindow,
                                  NetworkPacketPolicy::kTrafficWindowMs));
        }
    }

    SECTION("the same volume is refused when no transfer was requested") {
        NetworkPacketPolicy::RateWindow window;
        bool refused = false;
        for(Uint64 sent = 0; sent < modTransferBytes; sent += chunkBytes) {
            if(!window.accept(start, chunkBytes, NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                              NetworkPacketPolicy::kTrafficWindowMs)) {
                refused = true;
                break;
            }
        }
        REQUIRE(refused);
    }

    SECTION("ordinary lobby and gameplay traffic stays far inside the budget") {
        NetworkPacketPolicy::RateWindow window;
        // A 1 MiB map inside SENDGAMEINFO plus a second of command traffic.
        REQUIRE(window.accept(start, 1024 * 1024, NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                              NetworkPacketPolicy::kTrafficWindowMs));
        for(int i = 0; i < 60; i++) {
            REQUIRE(window.accept(start, 512, NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                                  NetworkPacketPolicy::kTrafficWindowMs));
        }
    }

    SECTION("a byte count near the 64 bit maximum saturates instead of wrapping") {
        NetworkPacketPolicy::RateWindow window;
        REQUIRE_FALSE(window.accept(start, 0xFFFFFFFFFFFFFFFFull,
                                    NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                                    NetworkPacketPolicy::kTrafficWindowMs));
        REQUIRE_FALSE(window.accept(start, 1, NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                                    NetworkPacketPolicy::kTrafficWindowMs));
    }
}

TEST_CASE("Abuse: out-of-phase traffic is dropped without being held against the sender",
          "[network][security][abuse][phase][compatibility]") {
    // In-flight command traffic from a peer that started the match first.
    const PacketVerdict staleCommand = NetworkPacketPolicy::classifyPacket(
        context(NETWORKPACKET_COMMANDLIST, LocalRole::Host, SessionPhase::Lobby,
                PeerAdmission::Established, false));
    REQUIRE(staleCommand == PacketVerdict::RejectWrongPhase);
    REQUIRE(NetworkPacketPolicy::isExpectedOrderingRefusal(staleCommand));

    // A late lobby packet arriving after this peer entered the match.
    const PacketVerdict lateLobby = NetworkPacketPolicy::classifyPacket(
        context(NETWORKPACKET_CHANGEEVENTLIST, LocalRole::Client, SessionPhase::InGame,
                PeerAdmission::Established, true));
    REQUIRE(lateLobby == PacketVerdict::RejectWrongPhase);
    REQUIRE(NetworkPacketPolicy::isExpectedOrderingRefusal(lateLobby));

    // Forgery and pre-handshake traffic are not ordering races and do count.
    const PacketVerdict forgedStart = NetworkPacketPolicy::classifyPacket(
        context(NETWORKPACKET_STARTGAME, LocalRole::Client, SessionPhase::Lobby,
                PeerAdmission::Established, false));
    REQUIRE(forgedStart == PacketVerdict::RejectNotHostPeer);
    REQUIRE_FALSE(NetworkPacketPolicy::isExpectedOrderingRefusal(forgedStart));

    const PacketVerdict earlyCommands = NetworkPacketPolicy::classifyPacket(
        context(NETWORKPACKET_COMMANDLIST, LocalRole::Host, SessionPhase::InGame,
                PeerAdmission::Handshaking, false));
    REQUIRE(earlyCommands == PacketVerdict::RejectPreHandshake);
    REQUIRE_FALSE(NetworkPacketPolicy::isExpectedOrderingRefusal(earlyCommands));

    REQUIRE_FALSE(NetworkPacketPolicy::isExpectedOrderingRefusal(PacketVerdict::RejectUnknownType));
    REQUIRE_FALSE(NetworkPacketPolicy::isExpectedOrderingRefusal(
        PacketVerdict::RejectUnidentifiedPeer));
    REQUIRE_FALSE(NetworkPacketPolicy::isExpectedOrderingRefusal(PacketVerdict::RejectWrongRole));
}

// =============================================================================
// Wire decoding: ENetPacketIStream over real packets
// =============================================================================

TEST_CASE_METHOD(ENetRuntime, "Wire: well formed packets still decode",
                 "[network][security][wire][compatibility]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeUint32(NETWORKPACKET_CLIENTSTATS);
    ostream.writeUint32(750);
    ostream.writeFloat(59.5f);
    ostream.writeString("stefan");
    ostream.writeBool(true);
    ostream.writeBool(false);
    ostream.writeUint64(0x0123456789ABCDEFULL);

    ENetPacketIStream istream(ostream.getPacket());
    REQUIRE(istream.readUint32() == NETWORKPACKET_CLIENTSTATS);
    REQUIRE(istream.readUint32() == 750);
    REQUIRE(istream.readFloat() == Catch::Approx(59.5f));
    REQUIRE(istream.readString() == "stefan");
    REQUIRE(istream.readBool() == true);
    REQUIRE(istream.readBool() == false);
    REQUIRE(istream.readUint64() == 0x0123456789ABCDEFULL);
    REQUIRE(istream.getRemainingLength() == 0);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: unaligned fields decode correctly",
                 "[network][security][wire]") {
    // A single leading byte pushes every following field off its natural alignment. The old
    // implementation dereferenced typed pointers here, which is undefined behaviour.
    PacketBuilder builder;
    builder.u8(0xA5).u16(0xBEEF).u32(0xDEADBEEF).u64(0x0011223344556677ULL);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.readUint8() == 0xA5);
    REQUIRE(istream.readUint16() == 0xBEEF);
    REQUIRE(istream.readUint32() == 0xDEADBEEF);
    REQUIRE(istream.readUint64() == 0x0011223344556677ULL);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: truncated packets raise end of file",
                 "[network][security][wire]") {
    SECTION("nothing at all") {
        PacketBuilder builder;
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32(), InputStream::eof);
    }

    SECTION("half a field") {
        PacketBuilder builder;
        builder.u16(0x1234);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32(), InputStream::eof);
    }

    SECTION("string header without the string") {
        PacketBuilder builder;
        builder.lyingStringLength(64, "short");
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readString(), InputStream::eof);
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: overflowing string lengths are refused, not wrapped",
                 "[network][security][wire][overflow]") {
    // On wasm32 size_t is 32 bits, so the old "currentPos + length > dataLength" check wrapped
    // for these lengths and let the read run past the end of the packet.
    const Uint32 overflowLengths[] = {
        0xFFFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFFDu, 0x80000000u, 0x7FFFFFFFu
    };

    for(const Uint32 claimedLength : overflowLengths) {
        INFO("claimed length " << claimedLength);
        PacketBuilder builder;
        builder.u32(NETWORKPACKET_CHATMESSAGE).lyingStringLength(claimedLength, "abcd");
        ENetPacketIStream istream(builder.build());
        REQUIRE(istream.readUint32() == NETWORKPACKET_CHATMESSAGE);
        REQUIRE_THROWS_AS(istream.readString(), InputStream::eof);
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: booleans other than 0 and 1 are refused",
                 "[network][security][wire]") {
    PacketBuilder builder;
    builder.u8(1).u8(0).u8(2).u8(0xFF);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.readBool() == true);
    REQUIRE(istream.readBool() == false);
    REQUIRE_THROWS_AS(istream.readBool(), InputStream::error);
}

TEST_CASE_METHOD(ENetRuntime, "Wire: collection counts are bounded by the bytes that remain",
                 "[network][security][wire][overflow]") {
    SECTION("a set that claims four billion entries") {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu).u32(1).u32(2);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32Set(), InputStream::eof);
    }

    SECTION("a vector that claims more entries than the packet can hold") {
        PacketBuilder builder;
        builder.u32(1000).u32(7);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(istream.readUint32Vector(), InputStream::eof);
    }

    SECTION("an honest collection still decodes") {
        PacketBuilder builder;
        builder.u32(3).u32(10).u32(20).u32(30);
        ENetPacketIStream istream(builder.build());
        const std::set<Uint32> decoded = istream.readUint32Set();
        REQUIRE(decoded == std::set<Uint32>{10, 20, 30});
    }
}

TEST_CASE_METHOD(ENetRuntime, "Wire: remaining length tracks consumption",
                 "[network][security][wire]") {
    PacketBuilder builder;
    builder.u32(1).u32(2);

    ENetPacketIStream istream(builder.build());
    REQUIRE(istream.getRemainingLength() == 8);
    REQUIRE(istream.readUint32() == 1);
    REQUIRE(istream.getRemainingLength() == 4);
    REQUIRE(istream.readUint32() == 2);
    REQUIRE(istream.getRemainingLength() == 0);
}

// =============================================================================
// Lobby event parsing (the real ChangeEventList parser)
// =============================================================================

TEST_CASE_METHOD(ENetRuntime, "Lobby: a normal change event list round-trips",
                 "[network][security][lobby][compatibility]") {
    ChangeEventList original;
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeHouse, 0u, 2u);
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeTeam, 1u, 3u);
    original.changeEventList.emplace_back(1u, std::string("stefan"));

    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    original.save(ostream);

    ENetPacketIStream istream(ostream.getPacket());
    ChangeEventList decoded(istream);

    REQUIRE(decoded.changeEventList.size() == 3);
    auto iter = decoded.changeEventList.begin();
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::ChangeHouse);
    REQUIRE(iter->slot == 0);
    REQUIRE(iter->newValue == 2);
    ++iter;
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::ChangeTeam);
    ++iter;
    REQUIRE(iter->eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer);
    REQUIRE(iter->newStringValue == "stefan");
}

TEST_CASE_METHOD(ENetRuntime, "Lobby: graphics skin changes round-trip",
                 "[network][security][lobby][skins]") {
    ChangeEventList original;
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin, 1u, 1u);
    ENetPacketOStream out(ENET_PACKET_FLAG_RELIABLE);
    original.save(out);
    ENetPacketIStream in(out.getPacket());
    ChangeEventList decoded(in);
    REQUIRE(decoded.changeEventList.size() == 1);
    const auto& event = decoded.changeEventList.front();
    REQUIRE(event.eventType == ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin);
    REQUIRE(event.slot == 1);
    REQUIRE(event.newValue == 1);
}

TEST_CASE_METHOD(ENetRuntime, "Lobby: malformed change event lists are refused",
                 "[network][security][lobby]") {
    SECTION("event count near the 32 bit maximum") {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("more events than a lobby can ever hold") {
        PacketBuilder builder;
        builder.u32(4096);
        for(int i = 0; i < 4096; i++) {
            builder.u32(0).u32(0).u32(0);
        }
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("unknown event type") {
        PacketBuilder builder;
        builder.u32(1).u32(99).u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("truncated event") {
        PacketBuilder builder;
        builder.u32(2).u32(0).u32(0).u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }

    SECTION("a slot far outside the lobby survives parsing and is caught by the handler bound") {
        // The parser is deliberately permissive about the slot value; the receiving handler
        // rejects it against numHouses. What matters here is that it never reaches an array.
        PacketBuilder builder;
        builder.u32(1).u32(0).u32(0xFFFFFFFFu).u32(0);
        ENetPacketIStream istream(builder.build());
        ChangeEventList decoded(istream);
        REQUIRE(decoded.changeEventList.size() == 1);
        REQUIRE(decoded.changeEventList.front().slot == 0xFFFFFFFFu);
        REQUIRE(decoded.changeEventList.front().slot >= static_cast<Uint32>(MAX_CUSTOM_GAME_PLAYERS));
    }
}

// =============================================================================
// Command validation (used by CommandManager::addCommandList)
// =============================================================================

TEST_CASE("Commands: unknown ids are refused", "[network][security][command]") {
    REQUIRE(CommandValidation::isKnownCommandID(CMD_UNIT_MOVE2POS));
    REQUIRE(CommandValidation::isKnownCommandID(CMD_MAX - 1));

    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_NONE));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_MAX));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(CMD_MAX + 1));
    REQUIRE_FALSE(CommandValidation::isKnownCommandID(0xFFFFFFFFu));
}

TEST_CASE("Commands: parameter counts must match the command table",
          "[network][security][command]") {
    // Exactly the counts Command::executeCommand() requires; a mismatch makes it throw out of
    // the simulation loop, which takes down every peer that accepted the command.
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 4));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 5));

    REQUIRE(CommandValidation::isWellFormedCommand(CMD_PLACE_STRUCTURE, 3));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_MCV_DEPLOY, 1));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_PLAYER_PAUSE, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_PLAYER_PAUSE, 1));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_STRUCTURE_DEMOLISH, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_STRUCTURE_DEMOLISH, 0));

    // The city commands read up to three optional parameters.
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_CITY_PLACE_ZONE, 3));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_CITY_TOOL, 3));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_CITY_TOOL, 4));

    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_NONE, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MAX, 1));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MCV_DEPLOY, 1000000));
}

TEST_CASE("Commands: the whole command table is covered", "[network][security][command]") {
    // Every executable command must have an entry, otherwise legitimate play would be dropped.
    for(Uint32 commandID = static_cast<Uint32>(CMD_NONE) + 1;
        commandID < static_cast<Uint32>(CMD_MAX); commandID++) {
        INFO("command id " << commandID);
        const int arity = CommandValidation::exactParameterCount(static_cast<CMDTYPE>(commandID));
        REQUIRE(arity != -2);

        const std::size_t validCount = (arity == -1) ? 3 : static_cast<std::size_t>(arity);
        REQUIRE(CommandValidation::isWellFormedCommand(commandID, validCount));
    }
}

TEST_CASE("Commands: cycle windows bound the scheduling vector",
          "[network][security][command][overflow]") {
    const Uint32 currentCycle = 1000;
    const Uint32 buffer = 10;

    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle, currentCycle, buffer));
    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle + buffer, currentCycle, buffer));
    // Retransmissions of the rolling history reference cycles that already passed.
    REQUIRE(CommandValidation::isAcceptableCommandCycle(0, currentCycle, buffer));
    REQUIRE(CommandValidation::isAcceptableCommandCycle(currentCycle - 100, currentCycle, buffer));

    // A cycle number of four billion would resize the timeslot vector to four billion entries.
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(0xFFFFFFFEu, currentCycle, buffer));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(0x80000000u, currentCycle, buffer));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(
        CommandValidation::maxAcceptableCommandCycle(currentCycle, buffer) + 1, currentCycle, buffer));

    SECTION("the window arithmetic itself cannot wrap") {
        const Uint32 lateCycle = 0xFFFFFFF0u;
        REQUIRE(CommandValidation::maxAcceptableCommandCycle(lateCycle, buffer)
                == std::numeric_limits<Uint32>::max());
        // A nonsensical buffer must not wrap the limit round to a small number.
        REQUIRE(CommandValidation::maxAcceptableCommandCycle(0, 0xFFFFFFFFu)
                >= 0x7FFFFFFFu);
        REQUIRE(CommandValidation::isAcceptableCommandCycle(0xFFFFFFFFu, lateCycle, buffer));
    }

    SECTION("the processed watermark saturates instead of wrapping to zero") {
        REQUIRE(CommandValidation::nextCycleAfter(41) == 42);
        REQUIRE(CommandValidation::nextCycleAfter(std::numeric_limits<Uint32>::max())
                == std::numeric_limits<Uint32>::max());
    }
}

TEST_CASE("Commands: batch counts are bounded before allocation",
          "[network][security][command][overflow]") {
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(0));
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(64));
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(
        CommandValidation::kMaxCommandListEntries));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandListEntryCount(
        CommandValidation::kMaxCommandListEntries + 1));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandListEntryCount(0xFFFFFFFFu));

    REQUIRE(CommandValidation::isAcceptableCommandCountPerEntry(32));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandCountPerEntry(0xFFFFFFFFu));
}

// =============================================================================
// Path budget orders (used by Game::handleSetPathBudget)
// =============================================================================

TEST_CASE("Path budget: only host-scheduled orders are accepted",
          "[network][security][pathbudget]") {
    const uint32_t interval = 375;
    const size_t minBudget = 5000;
    const size_t maxBudget = 25000;
    const uint32_t currentCycle = 750;
    const uint32_t applyCycle = PathBudgetSync::calculateApplyCycle(currentCycle, interval);

    REQUIRE(applyCycle == 1125);
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(15000, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(minBudget, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));
    REQUIRE(PathBudgetSync::isAcceptableBudgetOrder(maxBudget, applyCycle, currentCycle, interval,
                                                    minBudget, maxBudget));

    SECTION("out of range budgets desync the pathfinding schedule") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(0, applyCycle, currentCycle,
                                                              interval, minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(maxBudget + 1, applyCycle,
                                                              currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(0xFFFFFFFFu, applyCycle, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("apply cycles that are not interval boundaries are refused") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, applyCycle + 1, currentCycle,
                                                              interval, minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, currentCycle + 1, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("orders from the past or the far future are refused") {
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 375, currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 375000, currentCycle, interval,
                                                              minBudget, maxBudget));
        REQUIRE_FALSE(PathBudgetSync::isAcceptableBudgetOrder(15000, 0xFFFFFF00u, currentCycle,
                                                              interval, minBudget, maxBudget));
    }

    SECTION("the pending queue is bounded") {
        REQUIRE(PathBudgetSync::kMaxPendingBudgetChanges > 0);
        REQUIRE(PathBudgetSync::kMaxPendingBudgetChanges <= 16);
    }
}

// =============================================================================
// Mod payload boundaries (used by ModManager::saveReceivedMod)
// =============================================================================

TEST_CASE("Mod unpack: payload bounds cannot be wrapped by a crafted length",
          "[network][security][mod-transfer][overflow]") {
    const std::size_t payloadSize = 1024;

    REQUIRE(ModTransferValidation::fitsWithinPayload(0, 1024, payloadSize));
    REQUIRE(ModTransferValidation::fitsWithinPayload(1000, 24, payloadSize));
    REQUIRE(ModTransferValidation::fitsWithinPayload(1024, 0, payloadSize));

    REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(1000, 25, payloadSize));
    REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(1025, 0, payloadSize));

    // The lengths that wrap "offset + length" when size_t is 32 bits (wasm32).
    const std::uint64_t wrappingLengths[] = {0xFFFFFFFFull, 0xFFFFFFF0ull, 0xFFFFFF00ull};
    for(const std::uint64_t length : wrappingLengths) {
        INFO("length " << length);
        REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(8, length, payloadSize));
        REQUIRE_FALSE(ModTransferValidation::fitsWithinPayload(payloadSize - 4, length, payloadSize));
    }
}

TEST_CASE("Mod unpack: payload paths stay inside the mod directory",
          "[network][security][mod-transfer]") {
    std::filesystem::path normalized;

    REQUIRE(ModTransferValidation::normalizeRelativeFilePath("mod.ini", normalized));
    REQUIRE(normalized.generic_string() == "mod.ini");

    const char* dangerous[] = {
        "../outside.ini", "data/../../outside.ini", "/absolute.ini", "C:/absolute.ini",
        "data/NUL.png", "trailing.", "data//double.png"
    };
    for(const char* name : dangerous) {
        INFO("path " << name);
        REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(name, normalized));
    }
}

// =============================================================================
// Command authorization: the decision Command::executeCommand() makes per object
// =============================================================================

namespace {

using CommandAuthorization::ActorContext;
using CommandAuthorization::Decision;

/// One player in the fixture, as Command::executeCommand() sees it.
struct FixturePlayer {
    bool exists = true;
    bool hasHouse = true;
    int  houseID = 0;
};

/// One object in the fixture, as the command's own dynamic_cast sees it.
struct FixtureObject {
    bool exists = true;
    bool typeMatches = true;
    bool hasOwner = true;
    int  ownerHouseID = 0;
};

/// Mirrors makeActorContext() in Command.cpp: the facts come from the simulation, the verdict
/// comes from the production function under test.
Decision decide(const FixturePlayer& issuer, const FixtureObject& object) {
    ActorContext context;
    context.issuerExists = issuer.exists;
    context.issuerHasHouse = issuer.exists && issuer.hasHouse;
    context.issuerHouseID = issuer.houseID;
    context.objectExists = object.exists;
    context.objectTypeMatches = object.exists && object.typeMatches;
    context.objectHasOwner = object.exists && object.hasOwner;
    context.objectOwnerHouseID = object.ownerHouseID;
    return CommandAuthorization::authorizeActor(context);
}

FixturePlayer playerOf(int houseID) {
    FixturePlayer player;
    player.houseID = houseID;
    return player;
}

FixtureObject objectOf(int ownerHouseID) {
    FixtureObject object;
    object.ownerHouseID = ownerHouseID;
    return object;
}

} // namespace

TEST_CASE("Command authorization: a player may only act on objects of its own house",
          "[command][security][authorization]") {
    const FixturePlayer atreides = playerOf(HOUSE_ATREIDES);
    const FixturePlayer harkonnen = playerOf(HOUSE_HARKONNEN);

    SECTION("own object") {
        REQUIRE(decide(atreides, objectOf(HOUSE_ATREIDES)) == Decision::Allow);
    }

    SECTION("co-op: a second player in the same house keeps control") {
        // Two humans share one house: different player ids, same house.
        const FixturePlayer coopPartner = playerOf(HOUSE_ATREIDES);
        const FixtureObject sharedTank = objectOf(HOUSE_ATREIDES);
        REQUIRE(decide(atreides, sharedTank) == Decision::Allow);
        REQUIRE(decide(coopPartner, sharedTank) == Decision::Allow);
    }

    SECTION("an enemy object is refused") {
        const FixtureObject enemyTank = objectOf(HOUSE_HARKONNEN);
        REQUIRE(decide(atreides, enemyTank) == Decision::NotOwner);
        REQUIRE(decide(harkonnen, enemyTank) == Decision::Allow);
    }

    SECTION("a destroyed or unknown object id is a no-op, not an error") {
        FixtureObject destroyed = objectOf(HOUSE_ATREIDES);
        destroyed.exists = false;
        REQUIRE(decide(atreides, destroyed) == Decision::MissingObject);
    }

    SECTION("an object of the wrong type is refused") {
        // e.g. CMD_MCV_DEPLOY naming a windtrap: the object exists, the cast fails.
        FixtureObject wrongType = objectOf(HOUSE_ATREIDES);
        wrongType.typeMatches = false;
        REQUIRE(decide(atreides, wrongType) == Decision::WrongObjectType);
    }

    SECTION("an ownerless object is refused") {
        FixtureObject ownerless = objectOf(-1);
        ownerless.hasOwner = false;
        REQUIRE(decide(atreides, ownerless) == Decision::NotOwner);
    }

    SECTION("a command from a player that is not in the game is refused") {
        FixturePlayer ghost = playerOf(HOUSE_ATREIDES);
        ghost.exists = false;
        REQUIRE(decide(ghost, objectOf(HOUSE_ATREIDES)) == Decision::NoIssuer);

        FixturePlayer houseless = playerOf(-1);
        houseless.hasHouse = false;
        REQUIRE(decide(houseless, objectOf(HOUSE_ATREIDES)) == Decision::NoIssuer);
    }

    SECTION("only the owning house may act, whichever house that is") {
        // The issuer's house comes from the simulation, never from the packet, so a forged
        // player id only ever selects another player - it cannot select another house's units.
        const FixtureObject harkonnenTank = objectOf(HOUSE_HARKONNEN);
        for(int houseID = 0; houseID < NUM_HOUSES; houseID++) {
            const Decision decision = decide(playerOf(houseID), harkonnenTank);
            INFO("issuer house " << houseID);
            REQUIRE(decision == (houseID == HOUSE_HARKONNEN ? Decision::Allow : Decision::NotOwner));
        }
    }
}

TEST_CASE("Command authorization: every object action is covered, control commands are not",
          "[command][security][authorization]") {
    // Commands whose parameter 0 names an object Command::executeCommand() acts on.
    const CMDTYPE objectActions[] = {
        CMD_PLACE_STRUCTURE, CMD_UNIT_MOVE2POS, CMD_UNIT_MOVE2OBJECT, CMD_UNIT_ATTACKPOS,
        CMD_UNIT_ATTACKOBJECT, CMD_UNIT_HEAL, CMD_INFANTRY_CAPTURE, CMD_UNIT_REQUESTCARRYALLDROP,
        CMD_UNIT_SENDTOREPAIR, CMD_UNIT_SETMODE, CMD_DEVASTATOR_STARTDEVASTATE, CMD_MCV_DEPLOY,
        CMD_HARVESTER_RETURN, CMD_STRUCTURE_SETDEPLOYPOSITION, CMD_STRUCTURE_REPAIR,
        CMD_BUILDER_UPGRADE, CMD_BUILDER_PRODUCEITEM, CMD_BUILDER_CANCELITEM,
        CMD_BUILDER_SETONHOLD, CMD_PALACE_SPECIALWEAPON, CMD_PALACE_DEATHHAND,
        CMD_STARPORT_PLACEORDER, CMD_STARPORT_CANCELORDER, CMD_TURRET_ATTACKOBJECT,
        CMD_TECHCENTER_SPAWN, CMD_SCOUTPOST_UPGRADE, CMD_SCOUTPOST_CHEMIPOST_UPGRADE,
        CMD_POLICE_REINFORCEMENTS, CMD_ZONE_DEMOLISH, CMD_STRUCTURE_DEMOLISH
    };
    for(const CMDTYPE commandID : objectActions) {
        INFO("command " << static_cast<int>(commandID));
        REQUIRE(CommandAuthorization::actsOnOwnedObject(commandID));
    }

    // Commands that carry no acting object: they are authorized by issuer identity alone.
    const CMDTYPE nonObjectCommands[] = {
        CMD_PLAYER_PAUSE, CMD_PLAYER_RESUME, CMD_TEST_SYNC, CMD_HOUSE_AUTO_REPAIR, CMD_CAMPAIGN_SKIP, CMD_MATCH_PAUSE,
        CMD_CITY_PLACE_ZONE, CMD_CITY_SET_TAX_RATE, CMD_CITY_SET_BUDGET, CMD_CITY_TOOL
    };
    for(const CMDTYPE commandID : nonObjectCommands) {
        INFO("command " << static_cast<int>(commandID));
        REQUIRE_FALSE(CommandAuthorization::actsOnOwnedObject(commandID));
    }

    // Every command is classified one way or the other, so one added later cannot silently
    // fall outside the rule.
    for(Uint32 commandID = static_cast<Uint32>(CMD_NONE) + 1;
        commandID < static_cast<Uint32>(CMD_MAX); commandID++) {
        const CMDTYPE typed = static_cast<CMDTYPE>(commandID);
        bool listedAsControl = false;
        for(const CMDTYPE control : nonObjectCommands) {
            if(control == typed) {
                listedAsControl = true;
            }
        }
        INFO("command " << commandID);
        REQUIRE((CommandAuthorization::actsOnOwnedObject(typed) || listedAsControl));
    }
}

TEST_CASE("Command authorization: enum and boolean parameters are validated exactly",
          "[command][security][authorization]") {
    SECTION("attack modes") {
        REQUIRE(CommandAuthorization::isValidAttackMode(GUARD));
        REQUIRE(CommandAuthorization::isValidAttackMode(HUNT));
        REQUIRE(CommandAuthorization::isValidAttackMode(RETREAT));
        REQUIRE_FALSE(CommandAuthorization::isValidAttackMode(ATTACKMODE_MAX));
        REQUIRE_FALSE(CommandAuthorization::isValidAttackMode(0xFFFFFFFFu));
    }

    SECTION("booleans") {
        REQUIRE(CommandAuthorization::isValidBooleanParameter(0));
        REQUIRE(CommandAuthorization::isValidBooleanParameter(1));
        REQUIRE_FALSE(CommandAuthorization::isValidBooleanParameter(2));
        REQUIRE_FALSE(CommandAuthorization::isValidBooleanParameter(0xFFFFFFFFu));
    }

    SECTION("city zone types and tools") {
        REQUIRE(CommandAuthorization::isValidCityZoneType(0));
        REQUIRE(CommandAuthorization::isValidCityZoneType(3));
        REQUIRE_FALSE(CommandAuthorization::isValidCityZoneType(4));
        REQUIRE_FALSE(CommandAuthorization::isValidCityZoneType(0xFFFFFFFFu));

        REQUIRE(CommandAuthorization::isValidCityToolType(0));
        REQUIRE(CommandAuthorization::isValidCityToolType(2));
        REQUIRE_FALSE(CommandAuthorization::isValidCityToolType(3));
        REQUIRE_FALSE(CommandAuthorization::isValidCityToolType(0xFFFFFFFFu));
    }

    SECTION("city tax and funding stay inside the ranges the UI can produce") {
        REQUIRE(CommandAuthorization::isValidCityTaxRate(0));
        REQUIRE(CommandAuthorization::isValidCityTaxRate(CommandAuthorization::kMaxCityTaxRate));
        REQUIRE_FALSE(CommandAuthorization::isValidCityTaxRate(
            CommandAuthorization::kMaxCityTaxRate + 1));
        REQUIRE_FALSE(CommandAuthorization::isValidCityTaxRate(0xFFFFFFFFu));

        REQUIRE(CommandAuthorization::isValidFundingPercent(0));
        REQUIRE(CommandAuthorization::isValidFundingPercent(100));
        REQUIRE_FALSE(CommandAuthorization::isValidFundingPercent(101));
        REQUIRE_FALSE(CommandAuthorization::isValidFundingPercent(0xFFFFFFFFu));
    }
}

// =============================================================================
// Lobby authorization: what a client may ask the host to change
// =============================================================================

namespace {

using LobbyDecision = LobbyAuthorization::Decision;
using LobbyAuthorization::SeatSnapshot;
using LobbyAuthorization::SlotKind;

/// A four-house lobby: host in house 0 seat 0, "stefan" in house 1 seat 0,
/// "quix" sharing house 1 seat 1, house 2 seat 0 is a bot, house 3 seat 0 is closed.
SeatSnapshot exampleLobby() {
    SeatSnapshot snapshot;
    snapshot.numHouses = 4;
    snapshot.multiplePlayersPerHouse = true;

    snapshot.slots[0].kind = SlotKind::Human;
    snapshot.slots[0].name = "host";
    snapshot.slots[1].kind = SlotKind::Open;

    snapshot.slots[2].kind = SlotKind::Human;
    snapshot.slots[2].name = "stefan";
    snapshot.slots[3].kind = SlotKind::Human;
    snapshot.slots[3].name = "quix";

    snapshot.slots[4].kind = SlotKind::AI;
    snapshot.slots[5].kind = SlotKind::Open;

    snapshot.slots[6].kind = SlotKind::Closed;
    snapshot.slots[7].kind = SlotKind::Closed;

    return snapshot;
}

ChangeEventList::ChangeEvent seatClaim(Uint32 slot, const std::string& name) {
    return ChangeEventList::ChangeEvent(slot, name);
}

ChangeEventList::ChangeEvent houseChange(ChangeEventList::ChangeEvent::EventType type,
                                         Uint32 slot, Uint32 value) {
    return ChangeEventList::ChangeEvent(type, slot, value);
}

LobbyDecision judge(const SeatSnapshot& snapshot, const std::string& sender,
               const ChangeEventList::ChangeEvent& event) {
    return LobbyAuthorization::authorizeClientEvent(snapshot, sender, event);
}

} // namespace

TEST_CASE("Lobby authorization: a client may claim a seat only for itself",
          "[lobby][security][authorization]") {
    const SeatSnapshot lobby = exampleLobby();

    SECTION("claiming a free seat is how a player moves") {
        REQUIRE(judge(lobby, "stefan", seatClaim(1, "stefan")) == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "stefan", seatClaim(5, "stefan")) == LobbyDecision::Allow);
    }

    SECTION("taking over a bot seat is allowed, the UI offers it") {
        REQUIRE(judge(lobby, "stefan", seatClaim(4, "stefan")) == LobbyDecision::Allow);
    }

    SECTION("seating somebody else is refused") {
        REQUIRE(judge(lobby, "stefan", seatClaim(1, "quix")) == LobbyDecision::RejectForeignName);
        REQUIRE(judge(lobby, "stefan", seatClaim(1, "host")) == LobbyDecision::RejectForeignName);
        REQUIRE(judge(lobby, "stefan", seatClaim(1, "")) == LobbyDecision::RejectForeignName);
    }

    SECTION("a closed seat stays closed") {
        REQUIRE(judge(lobby, "stefan", seatClaim(6, "stefan")) == LobbyDecision::RejectClosedSeat);
    }

    SECTION("slots outside the lobby are refused") {
        REQUIRE(judge(lobby, "stefan", seatClaim(8, "stefan")) == LobbyDecision::RejectSlotOutOfRange);
        REQUIRE(judge(lobby, "stefan", seatClaim(0xFFFFFFFFu, "stefan"))
                == LobbyDecision::RejectSlotOutOfRange);
    }

    SECTION("second seats do not exist without multiple players per house") {
        SeatSnapshot single = exampleLobby();
        single.multiplePlayersPerHouse = false;
        REQUIRE(judge(single, "stefan", seatClaim(1, "stefan")) == LobbyDecision::RejectSlotOutOfRange);
        REQUIRE(judge(single, "stefan", seatClaim(2, "stefan")) == LobbyDecision::Allow);
    }
}

TEST_CASE("Lobby authorization: house settings are restricted to the sender's own house",
          "[lobby][security][authorization]") {
    const SeatSnapshot lobby = exampleLobby();
    using EventType = ChangeEventList::ChangeEvent::EventType;

    SECTION("the house the sender occupies") {
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS))
                == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeTeam, 1, 2))
                == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeColor, 1, 3))
                == LobbyDecision::Allow);
        // The co-op partner in the same house has the same rights.
        REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS))
                == LobbyDecision::Allow);
    }

    SECTION("another player's house is refused") {
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeHouse, 0, HOUSE_ORDOS))
                == LobbyDecision::RejectNotYourHouse);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeTeam, 2, 1))
                == LobbyDecision::RejectNotYourHouse);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeColor, 3, 1))
                == LobbyDecision::RejectNotYourHouse);
    }

    SECTION("skin choices follow the same house ownership rules") {
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeGraphicsSkin, 1, 1))
                == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeGraphicsSkin, 1, 0))
                == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeGraphicsSkin, 0, 1))
                == LobbyDecision::RejectNotYourHouse);
        REQUIRE(judge(lobby, "intruder", houseChange(EventType::ChangeGraphicsSkin, 1, 1))
                == LobbyDecision::RejectUnknownSender);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeGraphicsSkin, 4, 1))
                == LobbyDecision::RejectSlotOutOfRange);
    }

    SECTION("changing the partner slot of the sender's own house is allowed") {
        SeatSnapshot withSupport = lobby;
        withSupport.slots[3] = {SlotKind::AI, {}};
        REQUIRE(judge(withSupport, "stefan", houseChange(EventType::ChangePlayer, 3, 1))
                == LobbyDecision::Allow);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 3, 1))
                == LobbyDecision::RejectOccupiedSeat);
    }

    SECTION("changing a slot in another house is refused") {
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 0, 0))
                == LobbyDecision::RejectNotYourHouse);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 4, 0))
                == LobbyDecision::RejectNotYourHouse);
    }

    SECTION("a sender that holds no seat may not change anything but a seat claim") {
        REQUIRE(judge(lobby, "intruder", houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS))
                == LobbyDecision::RejectUnknownSender);
        REQUIRE(judge(lobby, "intruder", houseChange(EventType::ChangePlayer, 1, 0))
                == LobbyDecision::RejectUnknownSender);
        REQUIRE(judge(lobby, "intruder", seatClaim(1, "intruder")) == LobbyDecision::Allow);
    }

    SECTION("house-level slots are bounded by the house count, not the slot count") {
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeHouse, 4, HOUSE_ORDOS))
                == LobbyDecision::RejectSlotOutOfRange);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeHouse, 0xFFFFFFFFu, 0))
                == LobbyDecision::RejectSlotOutOfRange);
    }

    SECTION("the spice income factor is host-only, including for the sender's own house") {
        // Unlike house/team/colour/skin, this is not a seat preference: it decides how much a
        // house earns. A client's selector is disabled, so no client UI can produce it - and
        // the refusal does not depend on which row is named, nor on the factor being sensible.
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeSpiceIncome, 1, 2))
                == LobbyDecision::RejectHostOnly);
        REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeSpiceIncome, 1, 1))
                == LobbyDecision::RejectHostOnly);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeSpiceIncome, 0, 5))
                == LobbyDecision::RejectHostOnly);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeSpiceIncome, 4, 3))
                == LobbyDecision::RejectHostOnly);
        REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangeSpiceIncome, 0xFFFFFFFFu, 3))
                == LobbyDecision::RejectHostOnly);
        // Host-only is decided before seating is even consulted, so a sender that holds no
        // seat is refused for that reason rather than for being a stranger. Either way it
        // changes nothing; the distinction only shows up in the log line.
        REQUIRE(judge(lobby, "intruder", houseChange(EventType::ChangeSpiceIncome, 1, 2))
                == LobbyDecision::RejectHostOnly);
    }
}

TEST_CASE("Lobby authorization: a transaction is judged as a whole",
          "[lobby][security][authorization]") {
    const SeatSnapshot lobby = exampleLobby();
    using EventType = ChangeEventList::ChangeEvent::EventType;
    std::size_t refused = 0;

    SECTION("the pair the house drop-down sends is accepted") {
        // CustomGamePlayers::onChangeHousesDropDownBoxes sends ChangeHouse plus ChangeColor.
        std::list<ChangeEventList::ChangeEvent> events;
        events.push_back(houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS));
        events.push_back(houseChange(EventType::ChangeColor, 1, HOUSE_INVALID));
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                == LobbyDecision::Allow);
    }

    SECTION("one illegal event refuses the whole transaction") {
        std::list<ChangeEventList::ChangeEvent> events;
        events.push_back(houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS));
        events.push_back(houseChange(EventType::ChangeTeam, 0, 1));     // another house
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                == LobbyDecision::RejectNotYourHouse);
        REQUIRE(refused == 1);
    }

    SECTION("a client cannot occupy several seats in one transaction") {
        std::list<ChangeEventList::ChangeEvent> events;
        events.push_back(seatClaim(1, "stefan"));
        events.push_back(seatClaim(5, "stefan"));
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                == LobbyDecision::RejectTooManyClaims);
    }

    SECTION("the host's full lobby snapshot is not something a client may send") {
        // This is what getChangeEventList() produces: every seat and house in one list.
        std::list<ChangeEventList::ChangeEvent> events;
        for(Uint32 house = 0; house < 4; house++) {
            events.push_back(houseChange(EventType::ChangeHouse, house, HOUSE_ORDOS));
            events.push_back(houseChange(EventType::ChangeTeam, house, 1));
            events.push_back(houseChange(EventType::ChangeColor, house, 1));
        }
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                != LobbyDecision::Allow);
    }

    SECTION("an empty transaction changes nothing and is harmless") {
        const std::list<ChangeEventList::ChangeEvent> events;
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                == LobbyDecision::Allow);
    }

    SECTION("a lobby with no houses refuses everything") {
        SeatSnapshot empty;
        std::list<ChangeEventList::ChangeEvent> events;
        events.push_back(seatClaim(0, "stefan"));
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(empty, "stefan", events, refused)
                == LobbyDecision::RejectUnknownSender);
    }

    SECTION("a spice income event poisons an otherwise legal transaction") {
        // Mixing it with changes the sender really is allowed to make must not smuggle it
        // through: the whole list is judged first, so nothing at all is applied.
        std::list<ChangeEventList::ChangeEvent> events;
        events.push_back(houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS));
        events.push_back(houseChange(EventType::ChangeSpiceIncome, 1, 5));
        events.push_back(houseChange(EventType::ChangeColor, 1, HOUSE_INVALID));
        REQUIRE(LobbyAuthorization::authorizeClientTransaction(lobby, "stefan", events, refused)
                == LobbyDecision::RejectHostOnly);
        REQUIRE(refused == 1);
    }

    SECTION("the host's maximal lobby snapshot is still smaller than the event bound") {
        // getChangeEventList() sends seven events per row - house, team, colour, skin, spice
        // income and the two seats - for up to MAX_CUSTOM_GAME_PLAYERS rows. That list has to
        // fit through the decoder, and it is a client's lobby refresh that carries it.
        PacketBuilder builder;
        const Uint32 rows = static_cast<Uint32>(MAX_CUSTOM_GAME_PLAYERS);
        const Uint32 eventsPerRow = 7;
        REQUIRE(rows * eventsPerRow <= CHANGEEVENTLIST_MAX_EVENTS);
        builder.u32(rows * eventsPerRow);
        for(Uint32 houseRow = 0; houseRow < rows; houseRow++) {
            builder.u32(static_cast<Uint32>(EventType::ChangeHouse)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeTeam)).u32(houseRow).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangeColor)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeGraphicsSkin)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeSpiceIncome)).u32(houseRow).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangePlayer)).u32(houseRow * 2).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangePlayer)).u32(houseRow * 2 + 1).u32(1);
        }
        ENetPacketIStream istream(builder.build());
        ChangeEventList decoded(istream);
        REQUIRE(decoded.changeEventList.size() == rows * eventsPerRow);
        REQUIRE(decoded.changeEventList.size() == 63);
        // getChangeEventListForNewPlayer appends the joining player's seat claim to exactly
        // that list, which is the largest list the lobby ever produces. It has to decode.
        REQUIRE(rows * eventsPerRow + 1 == CHANGEEVENTLIST_MAX_EVENTS);
    }

    SECTION("a full refresh plus the joining player's seat claim still decodes") {
        PacketBuilder builder;
        const Uint32 rows = static_cast<Uint32>(MAX_CUSTOM_GAME_PLAYERS);
        builder.u32(rows * 7 + 1);
        for(Uint32 houseRow = 0; houseRow < rows; houseRow++) {
            builder.u32(static_cast<Uint32>(EventType::ChangeHouse)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeTeam)).u32(houseRow).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangeColor)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeGraphicsSkin)).u32(houseRow).u32(0);
            builder.u32(static_cast<Uint32>(EventType::ChangeSpiceIncome)).u32(houseRow).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangePlayer)).u32(houseRow * 2).u32(1);
            builder.u32(static_cast<Uint32>(EventType::ChangePlayer)).u32(houseRow * 2 + 1).u32(1);
        }
        builder.u32(static_cast<Uint32>(EventType::SetHumanPlayer)).u32(0).str("stefan");
        ENetPacketIStream istream(builder.build());
        ChangeEventList decoded(istream);
        REQUIRE(decoded.changeEventList.size() == CHANGEEVENTLIST_MAX_EVENTS);
        REQUIRE(decoded.changeEventList.back().newStringValue == "stefan");
    }

    SECTION("one event more than the maximal refresh is refused") {
        PacketBuilder builder;
        builder.u32(CHANGEEVENTLIST_MAX_EVENTS + 1);
        for(int i = 0; i <= CHANGEEVENTLIST_MAX_EVENTS; i++) builder.u32(0).u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }
}

TEST_CASE_METHOD(ENetRuntime, "Lobby: spice income changes round-trip",
                 "[network][security][lobby][spiceincome]") {
    ChangeEventList original;
    original.changeEventList.emplace_back(
        ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome, 2u, SpiceIncome::kMax);
    ENetPacketOStream out(ENET_PACKET_FLAG_RELIABLE);
    original.save(out);
    ENetPacketIStream in(out.getPacket());
    ChangeEventList decoded(in);
    REQUIRE(decoded.changeEventList.size() == 1);
    const auto& event = decoded.changeEventList.front();
    REQUIRE(event.eventType == ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome);
    REQUIRE(event.slot == 2);
    REQUIRE(event.newValue == SpiceIncome::kMax);

    SECTION("the appended type did not renumber the existing ones") {
        // These numbers are on the wire; a peer decodes by value, so they are frozen.
        using EventType = ChangeEventList::ChangeEvent::EventType;
        REQUIRE(static_cast<Uint32>(EventType::ChangeHouse) == 0);
        REQUIRE(static_cast<Uint32>(EventType::ChangeTeam) == 1);
        REQUIRE(static_cast<Uint32>(EventType::ChangeColor) == 2);
        REQUIRE(static_cast<Uint32>(EventType::ChangePlayer) == 3);
        REQUIRE(static_cast<Uint32>(EventType::SetHumanPlayer) == 4);
        REQUIRE(static_cast<Uint32>(EventType::ChangeGraphicsSkin) == 5);
        REQUIRE(static_cast<Uint32>(EventType::ChangeSpiceIncome) == 6);
    }

    SECTION("the type after the last known one is still refused") {
        PacketBuilder builder;
        builder.u32(1)
               .u32(static_cast<Uint32>(ChangeEventList::ChangeEvent::lastEventType) + 1)
               .u32(0).u32(0);
        ENetPacketIStream istream(builder.build());
        REQUIRE_THROWS_AS(ChangeEventList(istream), InputStream::exception);
    }
}

TEST_CASE("Received game settings bound the spice income factor of every row",
          "[network][security][spiceincome]") {
    const auto buildSettings = [](Uint32 firstFactor, Uint32 closedFactor) {
        GameInitSettings settings("map.ini", "[MAP]\nSizeX=64\nSizeY=64\n", "Host", false,
                                  SettingsClass::GameOptionsClass{});
        GameInitSettings::HouseInfo playing(HOUSE_ATREIDES, 1);
        playing.spiceIncomeMultiplier = firstFactor;
        playing.addPlayerInfo({"Host", HUMANPLAYERCLASS});
        settings.addHouseInfo(playing);
        // An explicitly closed row. The lobby can reopen it, so it is bounded too.
        GameInitSettings::HouseInfo closed(HOUSE_UNUSED, 2);
        closed.spiceIncomeMultiplier = closedFactor;
        settings.addHouseInfo(closed);
        return settings;
    };

    std::string reason;
    REQUIRE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(
        buildSettings(SpiceIncome::kMin, SpiceIncome::kMax), reason));
    REQUIRE(reason.empty());

    SECTION("an occupied row out of range is refused") {
        const Uint32 bad = GENERATE(0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu);
        REQUIRE_FALSE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(
            buildSettings(bad, SpiceIncome::kMin), reason));
        REQUIRE(reason == "spice income factor out of range");
    }

    SECTION("a closed row out of range is refused too") {
        const Uint32 bad = GENERATE(0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu);
        REQUIRE_FALSE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(
            buildSettings(SpiceIncome::kMin, bad), reason));
        REQUIRE(reason == "spice income factor out of range");
    }
}

// =============================================================================
// Received game info: semantic bounds before anything is committed
// =============================================================================

namespace {

/// A settings object of the shape a legitimate host sends for a custom multiplayer game.
GameInitSettings plausibleReceivedSettings(const std::string& mapName = "Arrakis Duel.ini",
                                           const std::string& mapData = "[MAP]\n") {
    SettingsClass::GameOptionsClass options;
    options.gameSpeed = GAMESPEED_DEFAULT;

    GameInitSettings settings(mapName, mapData, "a server", true, options);

    GameInitSettings::HouseInfo houseInfo(HOUSE_ATREIDES, 1);
    houseInfo.addPlayerInfo(GameInitSettings::PlayerInfo("stefan", "HumanPlayer"));
    settings.addHouseInfo(houseInfo);

    GameInitSettings::HouseInfo secondHouse(HOUSE_HARKONNEN, 2);
    secondHouse.addPlayerInfo(GameInitSettings::PlayerInfo("quix", "HumanPlayer"));
    settings.addHouseInfo(secondHouse);

    return settings;
}

bool accepts(const GameInitSettings& settings, std::string& reason) {
    return GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(settings, reason);
}

} // namespace

TEST_CASE("Received game info: a normal host snapshot is accepted",
          "[network][security][gameinfo][compatibility]") {
    std::string reason;
    REQUIRE(accepts(plausibleReceivedSettings(), reason));
    REQUIRE(reason.empty());
}

TEST_CASE("Received game info: impossible snapshots are refused before anything is committed",
          "[network][security][gameinfo]") {
    std::string reason;

    SECTION("more houses than the lobby has seats") {
        GameInitSettings settings = plausibleReceivedSettings();
        for(int i = 0; i < MAX_CUSTOM_GAME_PLAYERS; i++) {
            settings.addHouseInfo(GameInitSettings::HouseInfo(HOUSE_ORDOS, 1));
        }
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "too many houses");
    }

    SECTION("more players in a house than seats") {
        GameInitSettings settings = plausibleReceivedSettings();
        settings.clearHouseInfo();
        GameInitSettings::HouseInfo crowded(HOUSE_ATREIDES, 1);
        for(int i = 0; i < 5; i++) {
            crowded.addPlayerInfo(GameInitSettings::PlayerInfo("p", "HumanPlayer"));
        }
        settings.addHouseInfo(crowded);
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "too many players in one house");
    }

    SECTION("a map payload beyond the size limit") {
        GameInitSettings settings = plausibleReceivedSettings(
            "Arrakis Duel.ini", std::string(GameInitSettingsPolicy::kMaxMapFileSize + 1, 'x'));
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "map payload too large");
    }

    SECTION("an over-long filename") {
        GameInitSettings settings = plausibleReceivedSettings(
            std::string(GameInitSettingsPolicy::kMaxFilenameLength + 1, 'a'), "[MAP]\n");
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "filename too long");
    }

    SECTION("an out-of-range game speed") {
        GameInitSettings settings = plausibleReceivedSettings();
        settings.setGameSpeed(GAMESPEED_MAX + 1);
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "game speed out of range");

        settings.setGameSpeed(GAMESPEED_MIN - 1);
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "game speed out of range");

        // Nothing below the fastest setting is a duration a match could be paced by.
        settings.setGameSpeed(0);
        REQUIRE_FALSE(accepts(settings, reason));
        settings.setGameSpeed(-1);
        REQUIRE_FALSE(accepts(settings, reason));
    }

    SECTION("a team number outside the lobby") {
        GameInitSettings settings = plausibleReceivedSettings();
        settings.clearHouseInfo();
        settings.addHouseInfo(GameInitSettings::HouseInfo(HOUSE_ATREIDES, 9999));
        REQUIRE_FALSE(accepts(settings, reason));
        REQUIRE(reason == "team out of range");
    }
}

TEST_CASE("Received game info: the host's shared pacing may be any settable speed",
          "[network][security][gameinfo][gamespeed]") {
    std::string reason;

    // Every millisecond from the fastest to the slowest setting is a state the host can
    // legitimately broadcast, so a client has to admit all of them - including the two new
    // fast ones, which is what a peer on protocol 55 refuses and why the version moved.
    for(int speed = GAMESPEED_MIN; speed <= GAMESPEED_MAX; speed++) {
        GameInitSettings settings = plausibleReceivedSettings();
        settings.setGameSpeed(speed);
        INFO("game speed " << speed << " ms per cycle");
        REQUIRE(accepts(settings, reason));
    }

    // The fastest setting is twice the previous one and the default is unchanged, so an
    // existing profile and an existing saved match keep the pacing they were written with.
    REQUIRE(GAMESPEED_MIN == 2);
    REQUIRE(GAMESPEED_DEFAULT == 16);
    REQUIRE(SettingsClass::GameOptionsClass().gameSpeed == GAMESPEED_DEFAULT);
    REQUIRE(plausibleReceivedSettings().getGameOptions().gameSpeed == GAMESPEED_DEFAULT);

    // Shared settings distinguish the selected speed so peers agree on the host's pacing.
    SettingsClass::GameOptionsClass fastest;
    fastest.gameSpeed = GAMESPEED_MIN;
    SettingsClass::GameOptionsClass standard;
    REQUIRE(fastest != standard);
    REQUIRE(fastest.getHash() != standard.getHash());

    // The same bytes carry a lobby snapshot and a saved match. The speed has always been a
    // whole uint32 field, so the new values need no layout change and a 4ms match written by
    // an older build still reads back as 4ms.
    for(const int speed : { GAMESPEED_MIN, GAMESPEED_MIN + 1, 4, GAMESPEED_DEFAULT }) {
        GameInitSettings original = plausibleReceivedSettings();
        original.setGameSpeed(speed);
        OMemoryStream out;
        out.open();
        original.save(out);
        IMemoryStream in(out.getData(), out.getDataLength());
        GameInitSettings restored(in);
        INFO("round-tripped game speed " << speed);
        REQUIRE(restored.getGameOptions().gameSpeed == speed);
        REQUIRE(restored.getGameOptions().getHash() == original.getGameOptions().getHash());
        REQUIRE(accepts(restored, reason));
        REQUIRE(restored.networkSnapshot("saved simulation").getGameOptions().gameSpeed == speed);
    }
}

TEST_CASE("Received game info: game type and house enums must be known",
          "[network][security][gameinfo]") {
    REQUIRE(GameInitSettingsPolicy::isKnownGameType(GameType::CustomMultiplayer));
    REQUIRE(GameInitSettingsPolicy::isKnownGameType(GameType::CampaignCoop));
    REQUIRE(GameInitSettingsPolicy::isKnownGameType(GameType::Invalid));
    REQUIRE_FALSE(GameInitSettingsPolicy::isKnownGameType(static_cast<GameType>(42)));
    REQUIRE_FALSE(GameInitSettingsPolicy::isKnownGameType(static_cast<GameType>(-7)));

    REQUIRE(GameInitSettingsPolicy::isKnownHouse(HOUSE_HARKONNEN));
    REQUIRE(GameInitSettingsPolicy::isKnownHouse(HOUSE_INVALID));
    REQUIRE(GameInitSettingsPolicy::isKnownHouse(static_cast<HOUSETYPE>(NUM_HOUSES - 1)));
    REQUIRE_FALSE(GameInitSettingsPolicy::isKnownHouse(static_cast<HOUSETYPE>(NUM_HOUSES)));
    REQUIRE_FALSE(GameInitSettingsPolicy::isKnownHouse(static_cast<HOUSETYPE>(-99)));
}

TEST_CASE("Command batches: aggregate bounds are far below the product of the per-list bounds",
          "[network][security][command][overflow]") {
    REQUIRE(CommandValidation::isAcceptableCommandTotal(0));
    REQUIRE(CommandValidation::isAcceptableCommandTotal(CommandValidation::kMaxCommandsPerPacket));
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandTotal(
        static_cast<std::size_t>(CommandValidation::kMaxCommandsPerPacket) + 1));

    // A packet that is nominally valid under the per-entry and per-list bounds alone would
    // carry a quarter of a million commands; the aggregate bound is what actually stops it.
    const std::size_t productOfBounds =
        static_cast<std::size_t>(CommandValidation::kMaxCommandListEntries)
        * static_cast<std::size_t>(CommandValidation::kMaxCommandsPerEntry);
    REQUIRE_FALSE(CommandValidation::isAcceptableCommandTotal(productOfBounds));

    // A real packet: the rolling history is ~156 cycles at the default speed and a busy cycle
    // holds a command per selected unit.
    REQUIRE(CommandValidation::isAcceptableCommandTotal(200));
    REQUIRE(CommandValidation::isAcceptableCommandCountPerEntry(300));
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(200));
}

TEST_CASE("Replay bounds: a crafted file cannot drive an unbounded allocation",
          "[command][security][replay]") {
    // CommandManager::load() resizes its per-cycle vector to the cycle it reads, so the cycle
    // bound is what stops a four-billion-entry allocation from a local file.
    REQUIRE(CommandValidation::kMaxReplayCycle < std::numeric_limits<Uint32>::max());
    REQUIRE(CommandValidation::kMaxReplayCommands > 0);

    // Still generous for real play: at the default game speed this is many hours.
    const Uint32 cyclesPerHour = static_cast<Uint32>(3600000 / GAMESPEED_DEFAULT);
    REQUIRE(CommandValidation::kMaxReplayCycle > cyclesPerHour * 8);

    // The same well-formedness rule the network path uses is applied to replay records, so a
    // crafted file cannot smuggle in a command that throws inside the simulation loop.
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 4));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 2));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_MAX, 1));
}

TEST_CASE("Mesh introductions: privileged and non-unicast destinations are refused",
          "[network][security][mesh]") {
    const Uint32 privateLan = 0xC0A80105;   // 192.168.1.5

    // The game's own default port and ephemeral ports stay usable.
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, DEFAULT_PORT));
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan,
                                                       NetworkPacketPolicy::kMinMeshTargetPort));
    REQUIRE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 65535));

    // A host cannot point a client at a well-known service port.
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 22));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 53));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan, 443));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(0x7F000001, 631));
    REQUIRE_FALSE(NetworkPacketPolicy::isPlausibleMeshTarget(privateLan,
                                                             NetworkPacketPolicy::kMinMeshTargetPort - 1));
}

TEST_CASE("Received game info cannot select local file loaders", "[network][security][gameinfo]") {
    ENetRuntime runtime;
    std::string reason;
    for(auto type : {GameType::LoadSavegame, GameType::Campaign, GameType::Skirmish,
                     GameType::CustomGame, GameType::Invalid}) {
        ENetPacketOStream out(ENET_PACKET_FLAG_RELIABLE);
        plausibleReceivedSettings().save(out);
        ENetPacket* packet = out.getPacket();
        packet->data[0] = static_cast<Uint8>(type);
        ENetPacketIStream in(packet);
        const GameInitSettings received(in);
        INFO("wire game type=" << static_cast<int>(type));
        REQUIRE_FALSE(accepts(received, reason));
    }
    const GameInitSettings endCampaign;
    REQUIRE_FALSE(accepts(endCampaign, reason));
    REQUIRE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(endCampaign, reason, true));
}

TEST_CASE("Received save settings preserve larger snapshots and closed house rows", "[network][gameinfo][compatibility]") {
    ENetRuntime runtime;
    std::string reason;
    auto settings = plausibleReceivedSettings("network.dls", std::string(1024 * 1024 + 1, 'x'));
    settings.addHouseInfo(GameInitSettings::HouseInfo(HOUSE_UNUSED, 0));
    ENetPacketOStream out(ENET_PACKET_FLAG_RELIABLE);
    settings.save(out);
    ENetPacket* packet = out.getPacket();
    packet->data[0] = static_cast<Uint8>(GameType::LoadMultiplayer);
    ENetPacketIStream in(packet);
    const GameInitSettings received(in);
    REQUIRE(accepts(received, reason));
}

TEST_CASE("Lobby authorization: support AI belongs to its human, including against host UI",
          "[lobby][security][authorization]") {
    SeatSnapshot lobby = exampleLobby();
    lobby.slots[1] = {SlotKind::AI, {}};
    lobby.slots[3] = {SlotKind::AI, {}};
    using EventType = ChangeEventList::ChangeEvent::EventType;
    REQUIRE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "host", 1, true));
    REQUIRE_FALSE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "host", 3, true));
    REQUIRE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "host", 4, true));
    REQUIRE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "stefan", 3, false));
    REQUIRE_FALSE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "stefan", 1, false));
    REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 1, 1))
            == LobbyDecision::RejectNotYourHouse);
    REQUIRE(judge(lobby, "stefan", seatClaim(1, "stefan")) == LobbyDecision::RejectOccupiedSeat);
    REQUIRE(judge(lobby, "host", seatClaim(3, "host")) == LobbyDecision::RejectOccupiedSeat);
    REQUIRE(judge(lobby, "stefan", seatClaim(0, "stefan")) == LobbyDecision::RejectOccupiedSeat);
    REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 2, 1))
            == LobbyDecision::RejectOccupiedSeat);
    REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 3, 0))
            == LobbyDecision::RejectOccupiedSeat);
    lobby.multiplePlayersPerHouse = false;
    REQUIRE(judge(lobby, "stefan", houseChange(EventType::ChangePlayer, 3, 1))
            == LobbyDecision::RejectSlotOutOfRange);
}

TEST_CASE("Lobby authorization: inactive partner seats cannot grant ownership",
          "[lobby][security][authorization]") {
    SeatSnapshot lobby = exampleLobby();
    lobby.multiplePlayersPerHouse = false;
    using EventType = ChangeEventList::ChangeEvent::EventType;
    REQUIRE_FALSE(lobby.isSeated("quix"));
    REQUIRE_FALSE(lobby.occupiesHouse("quix", 1));
    REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeHouse, 1, HOUSE_ORDOS))
            == LobbyDecision::RejectUnknownSender);
    REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeTeam, 1, 2))
            == LobbyDecision::RejectUnknownSender);
    REQUIRE(judge(lobby, "quix", houseChange(EventType::ChangeColor, 1, 3))
            == LobbyDecision::RejectUnknownSender);
    lobby.slots[2] = {SlotKind::AI, {}};
    REQUIRE(LobbyAuthorization::mayConfigurePlayerSlot(lobby, "host", 2, true));
}

TEST_CASE("Only human campaign controllers can skip missions", "[campaign][command]") {
    for(auto type : {GameType::Campaign, GameType::CampaignCoop}) {
        REQUIRE(CampaignControls::maySkip(type, HOUSE_ATREIDES, HOUSE_ATREIDES, true));
        REQUIRE_FALSE(CampaignControls::maySkip(type, HOUSE_ATREIDES, HOUSE_HARKONNEN, true));
        REQUIRE_FALSE(CampaignControls::maySkip(type, HOUSE_ATREIDES, HOUSE_ATREIDES, false));
        REQUIRE_FALSE(CampaignControls::maySkip(type, HOUSE_ATREIDES, -1, false));
    }
    for(auto type : {GameType::Skirmish, GameType::SkirmishCoop, GameType::CustomGame,
                     GameType::CustomMultiplayer, GameType::Invalid})
        REQUIRE_FALSE(CampaignControls::maySkip(type, HOUSE_ATREIDES, HOUSE_ATREIDES, true));
    REQUIRE(CommandValidation::isWellFormedCommand(CMD_CAMPAIGN_SKIP, 0));
    REQUIRE_FALSE(CommandValidation::isWellFormedCommand(CMD_CAMPAIGN_SKIP, 1));
}

TEST_CASE("Feedback submissions preserve text and restrict returned issue links", "[feedback]") {
    const auto fields = FeedbackIssue::fields("id", "Bug & labels=admin", "Café #1\n100% + spice?", "Mod: vanilla");
    REQUIRE(fields.at("title") == "Bug & labels=admin");
    REQUIRE(fields.at("details") == "Café #1\n100% + spice?");
    REQUIRE(fields.at("context") == "Mod: vanilla");
    REQUIRE(FeedbackIssue::isIssueUrl("https://github.com/ggtothemax/dunecity/issues/123"));
    for(const auto* url : {"https://github.com/ggtothemax/dunecity/issues/new", "https://evil.test/123",
        "https://github.com/ggtothemax/dunecity/issues/1?evil=1", "https://github.com/ggtothemax/dunecity/issues/"})
        REQUIRE_FALSE(FeedbackIssue::isIssueUrl(url));
    REQUIRE_THROWS_AS(FeedbackIssue::fields("id", " ", "Details", ""), std::invalid_argument);
    REQUIRE_THROWS_AS(FeedbackIssue::fields("id", "Title", "\n\t", ""), std::invalid_argument);
    REQUIRE_THROWS_AS(FeedbackIssue::fields("id", "Title", std::string(8001, '#'), ""), std::invalid_argument);
}

#include <Network/LateJoinPolicy.h>
TEST_CASE("Late join request queues are bounded and require a complete unique envelope", "[network][latejoin]") {
    std::vector<LateJoinPolicy::Request> result;
    const auto entry="request="+std::string(64,'a')+"|"+RoomAdmission::hexText("New player")+"\n";
    REQUIRE(LateJoinPolicy::parseQueue("status=ok\nprotocol=1\n"+entry,result));
    REQUIRE(result.size()==1);
    REQUIRE(result[0].name=="New player");
    REQUIRE_FALSE(result[0].spectator);
    const auto prefix="status=ok\nprotocol=1\nrequest="+std::string(64,'b')+"|"+RoomAdmission::hexText("Observer");
    REQUIRE(LateJoinPolicy::parseQueue(prefix+"|spectator\n",result));
    REQUIRE(result[0].spectator);
    REQUIRE_FALSE(LateJoinPolicy::parseQueue(prefix+"|admin\n",result));
    REQUIRE_FALSE(LateJoinPolicy::parseQueue(prefix+"|spectator|player\n",result));
    REQUIRE_FALSE(LateJoinPolicy::parseQueue("status=ok\n"+entry,result));
    REQUIRE_FALSE(LateJoinPolicy::parseQueue("status=ok\nprotocol=1\n"+entry+entry,result));
    REQUIRE_FALSE(LateJoinPolicy::parseQueue("status=ok\nprotocol=1\nstatus=ok\n",result));
    REQUIRE_FALSE(LateJoinPolicy::parseQueue("junkstatus=ok\nprotocol=1\n",result));
}
TEST_CASE("Running discovery carries map mod and elapsed time without admitting a player", "[network][latejoin]") {
    PublicRelayGame game;
    const auto row="ABCD-EFGH-JKMN|2|4|custom|"+RoomAdmission::hexText("Host")+"|"+std::string(64,'a')+"|"+RoomAdmission::hexText("Tornie")+"|"+RoomAdmission::hexText("Test map")+"|match|125|1";
    REQUIRE(RoomAdmission::parsePublicGame(row,game));
    REQUIRE(game.running); REQUIRE(game.allowLateJoin); REQUIRE(game.elapsedSeconds==125);
    REQUIRE(game.mapName=="Test map"); REQUIRE(game.modName=="Tornie");
    AdmissionResponse response; std::string error;
    REQUIRE(RoomAdmission::parseAdmissionResponse("status=ok\nprotocol=1\nrequest="+std::string(64,'a')+"\nrequestState=pending\n",response,error,false,AdmissionOperation::JoinRequest));
    REQUIRE(response.grant.empty());
    REQUIRE_FALSE(RoomAdmission::parseAdmissionResponse("status=ok\nprotocol=1\nrequestState=approved\n",response,error,false,AdmissionOperation::JoinStatus));
}

TEST_CASE("Shared match controls require the correct role and a live admitted match", "[network][pause][security]") {
    for(bool host : {false,true}) for(bool fromHost : {false,true})
    for(bool live : {false,true}) for(bool established : {false,true}) {
        const auto admission=established ? PeerAdmission::Established : PeerAdmission::Handshaking;
        const auto role=host ? LocalRole::Host : LocalRole::Client;
        const auto phase=live ? SessionPhase::InGame : SessionPhase::Lobby;
        REQUIRE((NetworkPacketPolicy::classifyPacket(context(NETWORKPACKET_MATCH_CONTROL,
            role,phase,admission,fromHost))==PacketVerdict::Accept) == (!host && fromHost && live && established));
        REQUIRE((NetworkPacketPolicy::classifyPacket(context(NETWORKPACKET_MATCH_RESUME_REQUEST,
            role,phase,admission,fromHost))==PacketVerdict::Accept) == (host && live && established));
    }
}
