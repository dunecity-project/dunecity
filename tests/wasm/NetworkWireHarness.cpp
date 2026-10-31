/*
 *  NetworkWireHarness.cpp - standalone regression harness for the network wire boundary
 *
 *  Purpose: the length and count checks in the network decoders behave differently when
 *  size_t is 32 bits. The desktop test target only ever exercises the LP64 behaviour, so this
 *  harness exists to run the same production headers under wasm32 (and, unchanged, natively).
 *
 *  It deliberately has no Catch2 and no game dependencies: it compiles the real
 *  ENetPacketIStream, ChangeEventList, NetworkPacketPolicy, CommandValidation, PathBudgetSync
 *  and ModTransferValidation headers and drives them with crafted wire images.
 *
 *  Build and run: see tests/wasm/run-network-wire-harness.sh
 *
 *  Exit code 0 means every check passed; any failure prints the failing check and exits 1.
 */

// This harness owns main(); SDL must not rename it (macOS/Windows SDL_main).
#define SDL_MAIN_HANDLED

#include <CommandValidation.h>
#include <Network/ChangeEventList.h>
#include <Network/ENetPacketIStream.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/PathBudgetSync.h>
#include <Network/SnapshotTransfer.h>
#include <mod/ModTransferValidation.h>

#include <enet/enet.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
    checks++;
    if(!condition) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

template<typename Exception, typename Callable>
void checkThrows(Callable&& callable, const char* what) {
    checks++;
    try {
        callable();
    } catch(const Exception&) {
        return;
    } catch(...) {
        failures++;
        std::printf("FAIL: %s (wrong exception type)\n", what);
        return;
    }
    failures++;
    std::printf("FAIL: %s (no exception)\n", what);
}

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

    ENetPacket* build() const {
        return enet_packet_create(bytes.empty() ? nullptr : bytes.data(), bytes.size(),
                                  ENET_PACKET_FLAG_RELIABLE);
    }

private:
    std::vector<Uint8> bytes;
};

void testWordSize() {
    std::printf("sizeof(size_t) = %u, sizeof(void*) = %u\n",
                static_cast<unsigned int>(sizeof(std::size_t)),
                static_cast<unsigned int>(sizeof(void*)));
}

void testStringLengthOverflow() {
    // The lengths that wrap "currentPos + length" when size_t is 32 bits.
    const Uint32 claimedLengths[] = {0xFFFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFFDu, 0x80000000u};

    for(const Uint32 claimedLength : claimedLengths) {
        PacketBuilder builder;
        builder.u32(NETWORKPACKET_CHATMESSAGE).u32(claimedLength).raw("abcd");
        ENetPacketIStream stream(builder.build());

        check(stream.readUint32() == NETWORKPACKET_CHATMESSAGE, "packet type decodes");
        checkThrows<InputStream::eof>([&] { stream.readString(); },
                                      "overflowing string length is refused");
    }
}

void testTruncatedReads() {
    {
        PacketBuilder builder;
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::eof>([&] { stream.readUint32(); }, "empty packet has no fields");
    }
    {
        PacketBuilder builder;
        builder.u16(0x1234);
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::eof>([&] { stream.readUint32(); }, "half a field is refused");
    }
    {
        PacketBuilder builder;
        builder.u32(64).raw("short");
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::eof>([&] { stream.readString(); },
                                      "string header without the string is refused");
    }
}

void testUnalignedDecoding() {
    PacketBuilder builder;
    builder.u8(0xA5).u16(0xBEEF).u32(0xDEADBEEF).u64(0x0011223344556677ULL);

    ENetPacketIStream stream(builder.build());
    check(stream.readUint8() == 0xA5, "unaligned uint8");
    check(stream.readUint16() == 0xBEEF, "unaligned uint16");
    check(stream.readUint32() == 0xDEADBEEF, "unaligned uint32");
    check(stream.readUint64() == 0x0011223344556677ULL, "unaligned uint64");
    check(stream.getRemainingLength() == 0, "stream is fully consumed");
}

void testBooleanEncoding() {
    PacketBuilder builder;
    builder.u8(1).u8(0).u8(2);

    ENetPacketIStream stream(builder.build());
    check(stream.readBool() == true, "true decodes");
    check(stream.readBool() == false, "false decodes");
    checkThrows<InputStream::error>([&] { stream.readBool(); },
                                    "boolean other than 0/1 is refused");
}

void testCollectionBounds() {
    {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu).u32(1).u32(2);
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::eof>([&] { stream.readUint32Set(); },
                                      "four billion set entries are refused");
    }
    {
        PacketBuilder builder;
        builder.u32(3).u32(10).u32(20).u32(30);
        ENetPacketIStream stream(builder.build());
        const std::set<Uint32> decoded = stream.readUint32Set();
        check(decoded.size() == 3 && decoded.count(20) == 1, "honest set decodes");
    }
}

void testChangeEventListParsing() {
    {
        PacketBuilder builder;
        builder.u32(0xFFFFFFFFu);
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::exception>([&] { ChangeEventList list(stream); (void)list; },
                                            "four billion change events are refused");
    }
    {
        PacketBuilder builder;
        builder.u32(1).u32(99).u32(0).u32(0);
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::exception>([&] { ChangeEventList list(stream); (void)list; },
                                            "unknown change event type is refused");
    }
    {
        // Two events announced, only one event worth of bytes present.
        PacketBuilder builder;
        builder.u32(2).u32(0).u32(1).u32(2);
        ENetPacketIStream stream(builder.build());
        checkThrows<InputStream::exception>([&] { ChangeEventList list(stream); (void)list; },
                                            "truncated change event list is refused");
    }
}

void testChangeEventListPositive() {
    PacketBuilder builder;
    builder.u32(2)
           .u32(static_cast<Uint32>(ChangeEventList::ChangeEvent::EventType::ChangeHouse)).u32(0).u32(2)
           .u32(static_cast<Uint32>(ChangeEventList::ChangeEvent::EventType::SetHumanPlayer)).u32(1)
           .u32(6).raw("stefan");

    ENetPacketIStream stream(builder.build());
    ChangeEventList list(stream);
    check(list.changeEventList.size() == 2, "legitimate change event list decodes");
    check(list.changeEventList.back().newStringValue == "stefan", "player name decodes");
}

void testAdmissionPolicy() {
    NetworkPacketPolicy::PacketContext ctx;
    ctx.localRole = NetworkPacketPolicy::LocalRole::Client;
    ctx.phase = NetworkPacketPolicy::SessionPhase::Lobby;
    ctx.admission = NetworkPacketPolicy::PeerAdmission::Established;
    ctx.isHostConnection = false;

    ctx.packetType = NETWORKPACKET_STARTGAME;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectNotHostPeer,
          "forged STARTGAME from a non-host peer is refused");

    ctx.isHostConnection = true;
    check(NetworkPacketPolicy::classifyPacket(ctx) == NetworkPacketPolicy::PacketVerdict::Accept,
          "STARTGAME from the host is accepted in the lobby");

    ctx.phase = NetworkPacketPolicy::SessionPhase::InGame;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectWrongPhase,
          "STARTGAME during a match is refused");

    ctx.packetType = NETWORKPACKET_SENDNAME;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectWrongPhase,
          "rename during a match is refused");

    ctx.phase = NetworkPacketPolicy::SessionPhase::Lobby;
    ctx.admission = NetworkPacketPolicy::PeerAdmission::Handshaking;
    check(NetworkPacketPolicy::classifyPacket(ctx) == NetworkPacketPolicy::PacketVerdict::Accept,
          "name exchange during admission is accepted");

    ctx.packetType = NETWORKPACKET_COMMANDLIST;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectPreHandshake,
          "commands before the handshake completes are refused");

    ctx.admission = NetworkPacketPolicy::PeerAdmission::Unidentified;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectUnidentifiedPeer,
          "an unidentified connection is never obeyed");

    ctx.admission = NetworkPacketPolicy::PeerAdmission::Established;
    ctx.packetType = 999;
    check(NetworkPacketPolicy::classifyPacket(ctx)
              == NetworkPacketPolicy::PacketVerdict::RejectUnknownType,
          "unknown packet types are refused");
}

void testMapFilenames() {
    std::string sanitized;
    check(NetworkPacketPolicy::sanitizeReceivedMapFilename("Arrakis.ini", sanitized)
              && sanitized == "Arrakis.ini",
          "legitimate map name is kept");
    check(NetworkPacketPolicy::sanitizeReceivedMapFilename("Arrakis", sanitized)
              && sanitized == "Arrakis.ini",
          "missing extension is added");

    const char* dangerous[] = {"../evil.ini", "/etc/evil.ini", "C:\\evil.ini", "a/b.ini",
                               "..", "CON", "trailing."};
    for(const char* name : dangerous) {
        check(!NetworkPacketPolicy::sanitizeReceivedMapFilename(name, sanitized),
              "traversal or non-portable map name is refused");
    }
}

void testCommandValidation() {
    check(CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 4), "move command is valid");
    check(!CommandValidation::isWellFormedCommand(CMD_UNIT_MOVE2POS, 3),
          "wrong parameter count is refused");
    check(!CommandValidation::isWellFormedCommand(CMD_MAX, 1), "unknown command id is refused");
    check(!CommandValidation::isWellFormedCommand(CMD_NONE, 0), "CMD_NONE is refused");

    check(CommandValidation::isAcceptableCommandCycle(1000, 1000, 10), "current cycle accepted");
    check(CommandValidation::isAcceptableCommandCycle(500, 1000, 10),
          "retransmitted past cycle accepted");
    check(!CommandValidation::isAcceptableCommandCycle(0xFFFFFFFEu, 1000, 10),
          "four billion cycles ahead is refused");
    check(CommandValidation::maxAcceptableCommandCycle(0xFFFFFFF0u, 10)
              == std::numeric_limits<Uint32>::max(),
          "cycle window arithmetic saturates");
    check(CommandValidation::nextCycleAfter(std::numeric_limits<Uint32>::max())
              == std::numeric_limits<Uint32>::max(),
          "processed watermark saturates");

    check(!CommandValidation::isAcceptableCommandListEntryCount(0xFFFFFFFFu),
          "four billion command list entries are refused");
    check(!CommandValidation::isAcceptableCommandCountPerEntry(0xFFFFFFFFu),
          "four billion commands per entry are refused");
}

void testPathBudgetOrders() {
    const uint32_t interval = 375;
    check(PathBudgetSync::isAcceptableBudgetOrder(15000, 1125, 750, interval, 5000, 25000),
          "host scheduled budget order is accepted");
    check(!PathBudgetSync::isAcceptableBudgetOrder(0xFFFFFFFFu, 1125, 750, interval, 5000, 25000),
          "out of range budget is refused");
    check(!PathBudgetSync::isAcceptableBudgetOrder(15000, 1126, 750, interval, 5000, 25000),
          "off-boundary apply cycle is refused");
    check(!PathBudgetSync::isAcceptableBudgetOrder(15000, 0xFFFFFF00u, 750, interval, 5000, 25000),
          "far future apply cycle is refused");
}

void testTrafficBudgets() {
    NetworkPacketPolicy::RateWindow window;
    const Uint32 start = 30000;
    const Uint64 chunkBytes = MOD_CHUNK_SIZE;

    bool burstAccepted = true;
    for(Uint64 sent = 0; sent < MAX_MOD_TRANSFER_SIZE; sent += chunkBytes) {
        if(!window.accept(start, chunkBytes, NetworkPacketPolicy::kMaxModTransferBytesPerWindow,
                          NetworkPacketPolicy::kTrafficWindowMs)) {
            burstAccepted = false;
            break;
        }
    }
    check(burstAccepted, "a requested maximum-sized mod burst fits the raised budget");

    NetworkPacketPolicy::RateWindow ordinary;
    bool bulkRefused = false;
    for(Uint64 sent = 0; sent < MAX_MOD_TRANSFER_SIZE; sent += chunkBytes) {
        if(!ordinary.accept(start, chunkBytes, NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                            NetworkPacketPolicy::kTrafficWindowMs)) {
            bulkRefused = true;
            break;
        }
    }
    check(bulkRefused, "bulk data without a requested transfer is refused");

    NetworkPacketPolicy::RateWindow saturating;
    check(!saturating.accept(start, 0xFFFFFFFFFFFFFFFFull,
                             NetworkPacketPolicy::kMaxPeerBytesPerWindow,
                             NetworkPacketPolicy::kTrafficWindowMs),
          "a byte count near the 64 bit maximum saturates instead of wrapping");

    NetworkPacketPolicy::RefusalCounter counter;
    bool crossed = false;
    for(Uint32 i = 0; i < NetworkPacketPolicy::kMaxRefusalsPerBurst; i++) {
        crossed = counter.noteRefusal(start + i, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                      NetworkPacketPolicy::kRefusalDecayMs);
    }
    check(crossed, "a burst of refusals crosses the disconnect threshold");
    check(counter.beginDisconnect(), "the disconnect is started once");
    check(!counter.beginDisconnect(), "the disconnect is not started twice");
    check(!counter.noteRefusal(start + 5000, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                               NetworkPacketPolicy::kRefusalDecayMs),
          "packets from a peer being dropped are no longer counted");

    NetworkPacketPolicy::RefusalCounter isolated;
    Uint32 now = start;
    bool everCrossed = false;
    for(int i = 0; i < 200; i++) {
        now += NetworkPacketPolicy::kRefusalDecayMs + 1;
        everCrossed |= isolated.noteRefusal(now, NetworkPacketPolicy::kMaxRefusalsPerBurst,
                                            NetworkPacketPolicy::kRefusalDecayMs);
    }
    check(!everCrossed, "isolated phase-race refusals never accumulate into a disconnect");

    check(NetworkPacketPolicy::isExpectedOrderingRefusal(
              NetworkPacketPolicy::PacketVerdict::RejectWrongPhase),
          "out-of-phase traffic is an expected ordering refusal");
    check(!NetworkPacketPolicy::isExpectedOrderingRefusal(
              NetworkPacketPolicy::PacketVerdict::RejectNotHostPeer),
          "forged host traffic is not an ordering refusal");
}

void testStatValues() {
    // Runtime bytes, so a fast-math build cannot fold the decision away.
    volatile Uint32 refused[] = {0x7f800000u, 0xff800000u, 0x7fc00001u, 0xbf800000u};
    for(unsigned i = 0; i < 4; ++i) {
        float value;
        Uint32 bits = refused[i];
        std::memcpy(&value, &bits, sizeof(value));
        check(!NetworkPacketPolicy::isUsableStatValue(value),
              "non-finite or negative stat value is refused");
    }

    volatile Uint32 accepted[] = {0x00000000u, 0x80000000u, 0x42700000u, 0x7f7fffffu};
    for(unsigned i = 0; i < 4; ++i) {
        float value;
        Uint32 bits = accepted[i];
        std::memcpy(&value, &bits, sizeof(value));
        check(NetworkPacketPolicy::isUsableStatValue(value), "finite stat value is accepted");
    }
}

void testModPayloadBounds() {
    const std::size_t payloadSize = 1024;
    check(ModTransferValidation::fitsWithinPayload(0, payloadSize, payloadSize),
          "exact payload fits");
    check(!ModTransferValidation::fitsWithinPayload(8, 0xFFFFFFFFull, payloadSize),
          "wrapping length is refused");
    check(!ModTransferValidation::fitsWithinPayload(payloadSize - 4, 0xFFFFFFF0ull, payloadSize),
          "wrapping length near the end is refused");
    check(!ModTransferValidation::fitsWithinPayload(payloadSize + 1, 0, payloadSize),
          "offset past the payload is refused");
}

void testSnapshotCompression() {
    using namespace SnapshotTransfer;
    Sender sender;
    const std::string original(maxBytes, '\0');
    check(sender.prepare(original), "maximum checkpoint compresses");
    std::uint32_t decodedSize=0;
    check(readHeader(sender.header(), sender.bytes.size(), decodedSize), "compressed header parses");
    check(decodedSize==maxBytes, "decoded length survives wasm32 header parsing");
    std::string decoded;
    check(decode(sender.bytes, decodedSize, decoded) && decoded==original, "maximum checkpoint restores exactly");
    check(!decode(sender.bytes, 8, decoded), "expansion beyond advertised length rejected");
    check(!decode(sender.bytes, 0xffffffffu, decoded), "oversized decode rejected before allocation");
    check(!decode(sender.bytes+"x", decodedSize, decoded), "trailing compressed bytes rejected");
    auto corrupt=sender.bytes; corrupt.back()^=1;
    check(!decode(corrupt, decodedSize, decoded), "checksum corruption rejected");
    check(!readHeader(std::string("DCZ1\xff\xff\xff\xff",8), 8, decodedSize), "oversized header rejected");
    check(sender.acknowledge(0)==Sender::Ack::RetryRaw, "legacy receiver requests raw retry");
    check(sender.header().empty() && sender.bytes==original, "fallback keeps original checkpoint");
    check(sender.acknowledge(0)==Sender::Ack::Ready, "legacy retry acknowledged");
}

} // namespace

int main() {
    testWordSize();
    testStringLengthOverflow();
    testTruncatedReads();
    testUnalignedDecoding();
    testBooleanEncoding();
    testCollectionBounds();
    testChangeEventListPositive();
    testChangeEventListParsing();
    testAdmissionPolicy();
    testMapFilenames();
    testCommandValidation();
    testPathBudgetOrders();
    testTrafficBudgets();
    testStatValues();
    testModPayloadBounds();
    testSnapshotCompression();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
