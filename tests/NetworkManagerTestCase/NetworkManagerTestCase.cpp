/*
 *  NetworkManagerTestCase.cpp - Unit tests for NetworkManager functionality
 *
 *  Tests packet serialization, address handling, and protocol helpers.
 *  Does NOT test actual network I/O (that requires integration tests).
 */

#include <catch2/catch_all.hpp>

#include <Network/ENetHelper.h>
#include <Network/NetworkManager.h>
#include <Network/ENetPacketOStream.h>
#include <Network/ENetPacketIStream.h>
#include <mod/Dune2RAssetManager.h>
#include <mod/ModPayloadIntegrity.h>
#include <mod/ModTransferValidation.h>

#include <enet/enet.h>

#include <chrono>
#include <fstream>

// The wire values every shipped client agrees on. These are written out independently of
// NetworkManager.h and then compared against the production constants, so a renumbering shows
// up here instead of silently breaking compatibility with released builds.
// The previous version of this file carried 1 for SENDGAMEINFO - the wrong value, and never
// compared - so it protected nothing.
static constexpr int kWireSendGameInfo  = 4;
static constexpr int kWireClientStats   = 13;
static constexpr int kWireKeepAlive     = 19;
static constexpr int kWireCoopMission   = 20;
static constexpr int kWireProtocolVersion = 61;

TEST_CASE("NetworkManager: wire constants match the shipped protocol", "[network][protocol]") {
    REQUIRE(NETWORKPACKET_SENDGAMEINFO == kWireSendGameInfo);
    REQUIRE(NETWORKPACKET_CLIENTSTATS == kWireClientStats);
    REQUIRE(NETWORKPACKET_KEEPALIVE == kWireKeepAlive);
    REQUIRE(NETWORKPACKET_COOP_MISSION == kWireCoopMission);
}

TEST_CASE("NetworkManager: the shipped protocol version is the pinned one",
          "[network][protocol]") {
    // 48 added the ChangeSpiceIncome lobby event and the MOD7 game-init block, and mixed the
    // factor into the state digest. 49 changed carryall pickup and containment recovery.
    // 50 adds the QuantBot army posture: a Custom Hard/Brutal house withdraws, assembles at a
    // protected rally and gates offensive dispatch, so a 49 peer issues different AI orders
    // from the same cycle. 51 allows city batteries to replace enemy-facing R/I/C zones.
    // 54 restores Hunt launcher spacing and releases rebuilt armies by strength.
    // 55 sends the whole ground army to Hunt live attacks on owned buildings.
    // 56 accepts 2/3ms speed settings which older peers reject.
    // 57 wraps the rounded combat turn direction, so a 56 peer writes a different
    // track index and rotates ground units differently from the same cycle.
    // 58 gives depleted-map city income priority over optional technology.
    // 60 changes required civic priority and prerequisite construction orders.
    // 61 launches the Micropolis city aircraft from the Airport with the
    // original doAirport odds, drawn from the shared simulation RNG, and flies
    // them on the original sprite clock. A 60 peer draws a different number of
    // RNG values on the same cycle and keeps a different fleet, so the two
    // diverge immediately.
    // Different AI movement decisions cannot share lockstep with older peers.
    REQUIRE(NETWORK_PROTOCOL_VERSION == kWireProtocolVersion);
    REQUIRE(NETWORK_PROTOCOL_VERSION == 61);
    REQUIRE(NETWORKDISCONNECT_PROTOCOL_MISMATCH == 5);
}

TEST_CASE("NetworkManager: current protocol handshake does not disconnect",
          "[network][protocol][handshake]") {
    bool disconnectCalled = false;
    const bool rejected = rejectIncompatibleNetworkProtocol(
        NETWORK_PROTOCOL_VERSION,
        [&disconnectCalled](int) {
            disconnectCalled = true;
        });

    REQUIRE_FALSE(rejected);
    REQUIRE_FALSE(disconnectCalled);
}

TEST_CASE("NetworkManager: mismatched protocol handshake dispatches rejection cause",
          "[network][protocol][handshake][regression]") {
    bool disconnectCalled = false;
    int disconnectCause = -1;
    const bool rejected = rejectIncompatibleNetworkProtocol(
        NETWORK_PROTOCOL_VERSION - 1,
        [&disconnectCalled, &disconnectCause](int cause) {
            disconnectCalled = true;
            disconnectCause = cause;
        });

    REQUIRE(rejected);
    REQUIRE(disconnectCalled);
    REQUIRE(disconnectCause == NETWORKDISCONNECT_PROTOCOL_MISMATCH);
}

TEST_CASE("Mod transfer accepts portable nested payload paths", "[network][mod-transfer][security]") {
    std::filesystem::path normalized;
    REQUIRE(ModTransferValidation::isValidModName("Tornie 1.0"));
    REQUIRE(ModTransferValidation::normalizeRelativeFilePath(
        "campaign/scena001.ini", normalized));
    REQUIRE(normalized.generic_string() == "campaign/scena001.ini");
    REQUIRE(ModTransferValidation::normalizeRelativeFilePath(
        "data\\units\\ChemicalSiegeTank.png", normalized));
    REQUIRE(normalized.generic_string() == "data/units/ChemicalSiegeTank.png");
    const std::string lowercaseKey = ModTransferValidation::portablePathKey(normalized);
    REQUIRE(lowercaseKey == "data/units/chemicalsiegetank.png");
    REQUIRE(lowercaseKey == ModTransferValidation::portablePathKey(
        std::filesystem::path("DATA/Units/ChemicalSiegeTank.PNG")));
}

TEST_CASE("Mod transfer rejects traversal and non-portable names", "[network][mod-transfer][security]") {
    std::filesystem::path normalized;
    REQUIRE_FALSE(ModTransferValidation::isValidModName("../Tornie"));
    REQUIRE_FALSE(ModTransferValidation::isValidModName("Tornie/Next"));
    REQUIRE_FALSE(ModTransferValidation::isValidModName("CON"));
    REQUIRE_FALSE(ModTransferValidation::isValidModName("LPT1.assets"));
    REQUIRE_FALSE(ModTransferValidation::isValidModName("Tornie."));
    REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(
        "campaign/../mod.ini", normalized));
    REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(
        "../outside.ini", normalized));
    REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(
        "data//asset.png", normalized));
    REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(
        "data/NUL.png", normalized));
    REQUIRE_FALSE(ModTransferValidation::normalizeRelativeFilePath(
        "C:/outside.ini", normalized));
}

namespace {

class TemporaryModPayload {
public:
    TemporaryModPayload() {
        const auto uniqueValue = std::chrono::high_resolution_clock::now()
                                     .time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path()
              / ("dunecity-mod-integrity-" + std::to_string(uniqueValue));
        std::filesystem::create_directories(root_ / "data");
    }

    ~TemporaryModPayload() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& root() const { return root_; }

    void write(const std::filesystem::path& relativePath, const std::string& contents) const {
        const std::filesystem::path path = root_ / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << contents;
    }

    void writeChecksums(const std::vector<std::filesystem::path>& files) const {
        std::ofstream checksumFile(root_ / "checksums.sha256", std::ios::binary);
        for(const auto& relativePath : files) {
            checksumFile << Dune2RAssetManager::sha256File((root_ / relativePath).string())
                         << "  " << relativePath.generic_string() << "\n";
        }
    }

private:
    std::filesystem::path root_;
};

} // namespace

TEST_CASE("Checksummed mod payload rejects drift before activation",
          "[network][mod-transfer][integrity]") {
    TemporaryModPayload payload;
    payload.write("mod.ini", "[Mod]\nName=Tornie\n");
    payload.write("data/unit.dat", "authoritative asset");
    payload.writeChecksums({"mod.ini", "data/unit.dat"});

    std::string error;
    REQUIRE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
    REQUIRE(error.empty());

    SECTION("changed file") {
        payload.write("data/unit.dat", "altered asset");
        REQUIRE_FALSE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
        REQUIRE(error.find("checksum mismatch") != std::string::npos);
    }

    SECTION("missing file") {
        std::filesystem::remove(payload.root() / "data/unit.dat");
        REQUIRE_FALSE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
        REQUIRE(error.find("missing or unsafe") != std::string::npos);
    }

    SECTION("unlisted extra file") {
        payload.write("data/unlisted.dat", "not in the manifest");
        REQUIRE_FALSE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
        REQUIRE(error.find("absent from checksums") != std::string::npos);
    }

    SECTION("managed installation stamp") {
        payload.write(".dunecity-managed", "bundle fingerprint");
        REQUIRE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
    }

    SECTION("duplicate manifest path") {
        std::ofstream checksumFile(payload.root() / "checksums.sha256",
                                   std::ios::binary | std::ios::app);
        checksumFile << Dune2RAssetManager::sha256File(
                            (payload.root() / "data/unit.dat").string())
                     << "  DATA/UNIT.DAT\n";
        checksumFile.close();
        REQUIRE_FALSE(ModPayloadIntegrity::verifyChecksummedPayload(payload.root(), error));
        REQUIRE(error.find("duplicate or case-colliding") != std::string::npos);
    }
}

// ENet initialization fixture
struct ENetFixture {
    ENetFixture() {
        if (enet_initialize() != 0) {
            throw std::runtime_error("Failed to initialize ENet");
        }
    }
    ~ENetFixture() {
        enet_deinitialize();
    }
};

// =============================================================================
// Address Utility Tests (require ENet)
// =============================================================================

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Address2String for IPv4", "[network][address]") {
    ENetAddress addr;
    addr.host = 0x0100007F;  // 127.0.0.1 in little-endian
    addr.port = 12345;
    
    REQUIRE(Address2String(addr) == "127.0.0.1");
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Address2String for localhost", "[network][address]") {
    ENetAddress addr;
    enet_address_set_host(&addr, "localhost");
    addr.port = 8080;
    
    REQUIRE(Address2String(addr) == "127.0.0.1");
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Address2String for broadcast", "[network][address]") {
    ENetAddress addr;
    addr.host = ENET_HOST_BROADCAST;
    addr.port = 12345;
    
    REQUIRE(Address2String(addr) == "255.255.255.255");
}

// =============================================================================
// ENet Packet Stream Tests (require ENet)
// =============================================================================

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream write/read uint32", "[network][packet]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeUint32(0x12345678);
    ostream.writeUint32(0xDEADBEEF);
    
    ENetPacket* packet = ostream.getPacket();
    REQUIRE(packet != nullptr);
    REQUIRE(packet->dataLength == 8);
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readUint32() == 0x12345678);
    REQUIRE(istream.readUint32() == 0xDEADBEEF);
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream write/read string", "[network][packet]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeString("Hello, Dune Legacy!");
    
    ENetPacket* packet = ostream.getPacket();
    REQUIRE(packet != nullptr);
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readString() == "Hello, Dune Legacy!");
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream complex packet", "[network][packet]") {
    // Write a complex packet similar to NETWORKPACKET_CLIENTSTATS
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeUint32(NETWORKPACKET_CLIENTSTATS);
    ostream.writeUint32(750);
    ostream.writeFloat(60.0f);
    ostream.writeFloat(0.5f);
    ostream.writeUint32(100);
    ostream.writeUint32(15000);
    
    ENetPacket* packet = ostream.getPacket();
    REQUIRE(packet != nullptr);
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readUint32() == NETWORKPACKET_CLIENTSTATS);
    REQUIRE(istream.readUint32() == 750);
    REQUIRE(istream.readFloat() == Catch::Approx(60.0f));
    REQUIRE(istream.readFloat() == Catch::Approx(0.5f));
    REQUIRE(istream.readUint32() == 100);
    REQUIRE(istream.readUint32() == 15000);
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream empty string", "[network][packet]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeString("");
    ostream.writeString("after empty");
    
    ENetPacket* packet = ostream.getPacket();
    REQUIRE(packet != nullptr);
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readString() == "");
    REQUIRE(istream.readString() == "after empty");
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream bool values", "[network][packet]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeBool(true);
    ostream.writeBool(false);
    ostream.writeBool(true);
    
    ENetPacket* packet = ostream.getPacket();
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readBool() == true);
    REQUIRE(istream.readBool() == false);
    REQUIRE(istream.readBool() == true);
}

TEST_CASE_METHOD(ENetFixture, "NetworkManager: Packet stream various int sizes", "[network][packet]") {
    ENetPacketOStream ostream(ENET_PACKET_FLAG_RELIABLE);
    ostream.writeUint8(0xFF);
    ostream.writeUint16(0xABCD);
    ostream.writeUint32(0x12345678);
    ostream.writeUint64(0xDEADBEEFCAFEBABE);
    
    ENetPacket* packet = ostream.getPacket();
    
    ENetPacketIStream istream(packet);
    REQUIRE(istream.readUint8() == 0xFF);
    REQUIRE(istream.readUint16() == 0xABCD);
    REQUIRE(istream.readUint32() == 0x12345678);
    REQUIRE(istream.readUint64() == 0xDEADBEEFCAFEBABE);
}

TEST_CASE("Executable mismatch is rejected even when mod sync is available", "[network][handshake]") {
    int calls = 0;
    auto disconnect = [&](int cause) { REQUIRE(cause == NETWORKDISCONNECT_PROTOCOL_MISMATCH); ++calls; };
    REQUIRE_FALSE(rejectIncompatibleGameVersion("1.0.553", "1.0.553", disconnect));
    REQUIRE(calls == 0);
    REQUIRE(rejectIncompatibleGameVersion("1.0.547", "1.0.553", disconnect));
    REQUIRE(calls == 1);
}
