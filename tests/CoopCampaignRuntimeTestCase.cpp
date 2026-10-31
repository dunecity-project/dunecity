#include <catch2/catch_all.hpp>
#include <Campaign/CoopCampaignSession.h>
#include <Campaign/CoopCampaignTransition.h>
#include <GameInitSettings.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/P2PWireFraming.h>
#include <Network/RoomRelayProtocol.h>
#include <Network/SnapshotTransfer.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>

namespace {
coop::CoopCampaignSession campaign() {
    return coop::CoopCampaignSession::create("checkpoint-eight", "TornieLite", {0,1,2,3,4,5},
        {1,2}, {"Host","Guest"}, 546, true, "legacy");
}

std::string mapTemplate(int stage) {
    return "[BASIC]\nVersion=2\nTechLevel=" + std::to_string(stage)
        + "\n[COOP_TEMPLATE]\nStage=" + std::to_string(stage)
        + "\n[Player1]\nCredits=1200\n[Player2]\nCredits=1200\n"
          "[Player3]\nCredits=500\n[Player4]\nCredits=500\n[Player5]\nCredits=500\n"
          "[COOP_EXTRA_PLAYER3]\nCredits=1200\n[COOP_EXTRA_UNITS3]\n[COOP_EXTRA_STRUCTURES3]\n"
          "[COOP_EXTRA_PLAYER4]\nCredits=1200\n[COOP_EXTRA_UNITS4]\n[COOP_EXTRA_STRUCTURES4]\n";
}

std::vector<std::string> configureEight(coop::CoopCampaignSession& session) {
    session.enableExpandedAllies(true);
    const std::array<std::array<std::string, 2>, 4> names{{
        {{"Host","Host Two"}}, {{"Guest","Guest Two"}},
        {{"Third","Third Two"}}, {{"Fourth","Fourth Two"}}
    }};
    std::vector<std::string> result;
    for(int i = 0; i < 4; ++i) {
        session.configureAlly(i, i + 1, HUMANPLAYERCLASS, names[i][0], {names[i][0], names[i][1]});
        result.insert(result.end(), names[i].begin(), names[i].end());
    }
    return result;
}

struct ProgressDirectory {
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("dunecity-coop-progress-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ProgressDirectory() { std::filesystem::create_directories(root); }
    ~ProgressDirectory() { std::error_code error; std::filesystem::remove_all(root, error); }
};
}

TEST_CASE("All seven remote controllers must agree before advancing a campaign", "[coop][network][archon]") {
    const std::vector<std::string> guests{"Two","Three","Four","Five","Six","Seven","Eight"};
    coop::MissionBarrier barrier("eight", 3, guests);
    for(std::size_t i = 0; i + 1 < guests.size(); ++i) {
        REQUIRE(barrier.receive(guests[i], "eight", 3, coop::Outcome::Won));
        REQUIRE_FALSE(barrier.decision(coop::Outcome::Won, 9));
    }
    REQUIRE_FALSE(barrier.receive("Stranger", "eight", 3, coop::Outcome::Won));
    REQUIRE_FALSE(barrier.receive(guests.back(), "other-session", 3, coop::Outcome::Won));
    REQUIRE_FALSE(barrier.receive(guests.back(), "eight", 2, coop::Outcome::Won));
    REQUIRE(barrier.receive(guests.back(), "eight", 3, coop::Outcome::Won));
    REQUIRE(barrier.decision(coop::Outcome::Won, 9) == coop::Advance::Next);
    REQUIRE(barrier.receive(guests.back(), "eight", 3, coop::Outcome::Lost));
    REQUIRE(barrier.decision(coop::Outcome::Won, 9) == coop::Advance::Aborted);
    REQUIRE(barrier.receive(guests.back(), "eight", 3, coop::Outcome::Won));
    REQUIRE(barrier.decision(coop::Outcome::Won, 9) == coop::Advance::Aborted);
}

TEST_CASE("Departure aborts immediately even while other controllers have not readied", "[coop][network]") {
    coop::MissionBarrier barrier("departure", 1, std::vector<std::string>{"First","Second"});
    REQUIRE(barrier.receive("Second", "departure", 1, coop::Outcome::Aborted));
    REQUIRE(barrier.decision(coop::Outcome::Won, 9) == coop::Advance::Aborted);
    coop::MissionBarrier retry("retry", 1, "Guest");
    REQUIRE(retry.receive("Guest", "retry", 1, coop::Outcome::Lost));
    REQUIRE(retry.decision(coop::Outcome::Lost, 9) == coop::Advance::Retry);
    REQUIRE_FALSE(retry.accepts(coop::Advance::Next, coop::Outcome::Lost, 9));
    coop::MissionBarrier final("final", 9, "Guest");
    REQUIRE(final.receive("Guest", "final", 9, coop::Outcome::Won));
    REQUIRE(final.decision(coop::Outcome::Won, 9) == coop::Advance::Complete);
    REQUIRE_FALSE(final.accepts(coop::Advance::Next, coop::Outcome::Won, 9));
}

TEST_CASE("Direct campaign control round-trips binary settings without a supplied identity", "[coop][network][serialization]") {
    coop::ControlMessage message{coop::ControlOperation::Advance, 4, "eight", 3,
        static_cast<unsigned char>(coop::Advance::Next), std::string("map\0settings", 12), "Spoofed name"};
    REQUIRE(coop::validControl(message));
    OMemoryStream output;
    output.open();
    output.writeUint32(NETWORKPACKET_COOP_CAMPAIGN_CONTROL);
    coop::writeControl(output, message);
    const auto envelope = P2PWire::encodeGameEnvelope(reinterpret_cast<const std::uint8_t*>(output.getData()),
        output.getDataLength(), 0, 7);
    P2PWire::Envelope received;
    const char* reason = nullptr;
    REQUIRE(P2PWire::decodeEnvelope(envelope, received, reason));
    REQUIRE(received.recipient == 7);
    REQUIRE(received.channel == 0);
    IMemoryStream input(reinterpret_cast<const char*>(received.payload.data()), received.payload.size());
    REQUIRE(input.readUint32() == NETWORKPACKET_COOP_CAMPAIGN_CONTROL);
    const auto parsed = coop::readControl(input);
    REQUIRE(coop::validControl(parsed));
    REQUIRE(parsed.operation == message.operation);
    REQUIRE(parsed.epoch == 4);
    REQUIRE(parsed.sessionId == "eight");
    REQUIRE(parsed.stage == 3);
    REQUIRE(parsed.value == message.value);
    REQUIRE(parsed.settingsBlob == message.settingsBlob);
    REQUIRE(parsed.playerName.empty());
    REQUIRE(input.getRemainingLength() == 0);
    REQUIRE_FALSE(RoomRelay::gameMessageRule(NETWORKPACKET_COOP_CAMPAIGN_CONTROL).carried);
    IMemoryStream truncated(output.getData() + 4, output.getDataLength() - 5);
    REQUIRE_THROWS(coop::readControl(truncated));
}

TEST_CASE("Malformed readiness cannot smuggle settings or an impossible mission action", "[coop][network][serialization]") {
    coop::ControlMessage ready{coop::ControlOperation::Ready, 1, "eight", 1,
        static_cast<unsigned char>(coop::Outcome::Won), {}, {}};
    REQUIRE(coop::validControl(ready));
    ready.settingsBlob = "not a readiness report";
    REQUIRE_FALSE(coop::validControl(ready));
    ready.settingsBlob.clear();
    ready.epoch = 0;
    REQUIRE_FALSE(coop::validControl(ready));
    ready.epoch = 1;
    ready.stage = 10;
    REQUIRE_FALSE(coop::validControl(ready));
    ready.stage = 1;
    ready.value = 255;
    REQUIRE_FALSE(coop::validControl(ready));
    ready.operation = coop::ControlOperation::Advance;
    ready.value = static_cast<unsigned char>(coop::Advance::Next);
    REQUIRE_FALSE(coop::validControl(ready));
    ready.settingsBlob = "next mission";
    REQUIRE(coop::validControl(ready));
    ready.value = static_cast<unsigned char>(coop::Advance::Complete);
    REQUIRE_FALSE(coop::validControl(ready));
}

TEST_CASE("Checkpoint metadata and setup preserve both controllers on all four allied bases", "[coop][save][archon]") {
    auto session = campaign();
    const auto names = configureEight(session);
    session.setAlliedControl(true);
    session.setAlliedBuilding(true);
    session.setEasyMode(true);
    session.setExtraEnemyForces(true);
    session.setExtraHardEnemyForces(true);
    const auto map = session.prepareMap(mapTemplate(1));
    const auto context = coop::readContext(map);
    REQUIRE(context);
    REQUIRE(context->humanNames() == names);
    SettingsClass::GameOptionsClass options;
    options.easyMode = options.chaosMode = options.alliedControl = options.alliedBuilding = true;
    GameInitSettings original("coop-faction1-coop01.ini", map, "Eight controllers", true, options);
    original.setModIdentity("TornieLite", "");
    for(const auto& slot : context->slots) {
        if(!slot.active()) continue;
        GameInitSettings::HouseInfo house(static_cast<HOUSETYPE>(slot.house), slot.team());
        if(slot.playerClass == HUMANPLAYERCLASS) {
            for(const auto& name : slot.humanNames()) house.addPlayerInfo({name, HUMANPLAYERCLASS});
        } else house.addPlayerInfo({slot.playerName, slot.playerClass});
        original.addHouseInfo(house);
    }
    OMemoryStream output;
    output.open();
    original.save(output);
    IMemoryStream input(output.getData(), output.getDataLength());
    const GameInitSettings loaded(input);
    const auto restored = coop::readContext(loaded.getFiledata());
    REQUIRE(restored);
    REQUIRE(coop::sameCampaignContext(*context, *restored));
    REQUIRE(restored->humanNames() == names);
    REQUIRE(loaded.isMultiplePlayersPerHouse());
    REQUIRE(loaded.getGameOptions() == options);
    std::vector<std::string> configured;
    for(const auto& house : loaded.getHouseInfoList())
        for(const auto& player : house.playerInfoList)
            if(player.playerClass == HUMANPLAYERCLASS) configured.push_back(player.playerName);
    REQUIRE(configured == names);

    // Resume renames every seat atomically, including a host/guest swap.
    auto resumed = coop::CoopCampaignSession::fromMapData(map);
    std::vector<coop::Slot> allies;
    for(const int index : restored->alliedSlots()) allies.push_back(restored->slots[index]);
    std::swap(allies[0].controllers, allies[1].controllers);
    allies[0].playerName = allies[0].controllers.front();
    allies[1].playerName = allies[1].controllers.front();
    resumed.reconfigureAllies(allies);
    const auto rebound = coop::readContext(resumed.prepareMap(map));
    REQUIRE(rebound);
    REQUIRE(rebound->humanNames().size() == 8);
    REQUIRE(rebound->humanNames()[0] == "Guest");
    REQUIRE(rebound->humanNames()[1] == "Guest Two");
    REQUIRE(rebound->humanNames()[6] == "Fourth");
    REQUIRE(rebound->humanNames()[7] == "Fourth Two");
    REQUIRE_FALSE(coop::sameCampaignContext(*restored, *rebound));
}

TEST_CASE("A saved campaign resumes the checkpoint and loss cannot consume a mission", "[coop][save][progress]") {
    auto session = campaign();
    configureEight(session);
    session = coop::CoopCampaignSession::fromMapData(session.prepareMap(mapTemplate(1)));
    ProgressDirectory directory;
    const auto progress = (directory.root / "progress.ini").string();
    const auto checkpoint = (directory.root / "checkpoint.dls").string();
    const std::string binarySettings("settings\0eight", 14);
    session.saveProgress(progress, binarySettings, checkpoint);
    const auto restored = coop::CoopCampaignSession::loadProgress(progress);
    REQUIRE(coop::sameCampaignContext(session.context(), restored.context()));
    REQUIRE(restored.settingsBlob() == binarySettings);
    REQUIRE(restored.checkpointPath() == checkpoint);
    session.completeMission(false);
    REQUIRE(session.context().stage == 1);
    REQUIRE(session.context().completedMask == 0);
    for(int stage = 1; stage <= coop::StageCount; ++stage) {
        REQUIRE(session.context().stage == stage);
        session.completeMission(true);
    }
    REQUIRE(session.isComplete());
    REQUIRE(session.context().completedMask == (1u << coop::StageCount) - 1);
    session.saveProgress(progress, "complete");
    REQUIRE(coop::CoopCampaignSession::loadProgress(progress).isComplete());
}

TEST_CASE("Campaign runtime houses never allocate the Natives identity", "[coop][campaign][ownership]") {
    auto session = campaign();
    configureEight(session);
    const auto context = coop::readContext(session.prepareMap(mapTemplate(1)));
    REQUIRE(context);
    std::set<int> houses;
    for(const auto& slot : context->slots) {
        REQUIRE(slot.house >= 0);
        REQUIRE(slot.house < NUM_PLAYER_HOUSES);
        REQUIRE(houses.insert(slot.house).second);
    }
    REQUIRE_THROWS(coop::CoopCampaignSession::create("natives", "Tornie", {0,1,2,3,HOUSE_NATIVES},
        {0,1}, {"Host","Guest"}, 546, false, "legacy"));
    REQUIRE_FALSE(coop::readContext("[BASIC]\nVersion=2\n[Atreides]\nBrain=Human\n"));
}

TEST_CASE("Campaign readiness is established-match traffic while legacy mission orders remain host-only",
          "[coop][network][admission]") {
    using namespace NetworkPacketPolicy;
    PacketContext context;
    context.packetType = NETWORKPACKET_COOP_CAMPAIGN_CONTROL;
    context.phase = SessionPhase::InGame;
    context.admission = PeerAdmission::Established;
    for(const auto role : {LocalRole::Host, LocalRole::Client}) {
        context.localRole = role;
        REQUIRE(classifyPacket(context) == PacketVerdict::Accept);
        context.phase = SessionPhase::Lobby;
        REQUIRE(classifyPacket(context) == PacketVerdict::RejectWrongPhase);
        context.phase = SessionPhase::InGame;
        context.admission = PeerAdmission::Handshaking;
        REQUIRE(classifyPacket(context) != PacketVerdict::Accept);
        context.admission = PeerAdmission::Established;
    }
    context.packetType = NETWORKPACKET_COOP_MISSION;
    context.localRole = LocalRole::Client;
    context.isHostConnection = false;
    REQUIRE(classifyPacket(context) == PacketVerdict::RejectNotHostPeer);
    context.isHostConnection = true;
    REQUIRE(classifyPacket(context) == PacketVerdict::Accept);
    context.localRole = LocalRole::Host;
    REQUIRE(classifyPacket(context) == PacketVerdict::RejectWrongRole);
}

TEST_CASE("Large checkpoint setup uses bounded direct records for compressed and raw snapshots",
          "[coop][network][save][serialization]") {
    const bool compressible = GENERATE(false, true);
    std::string checkpoint(600 * 1024, '\0');
    if(!compressible) {
        std::uint32_t random = 546;
        for(char& byte : checkpoint) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = static_cast<char>(random);
        }
    }
    REQUIRE(checkpoint.size() > P2PWire::Limits::kMaxGamePayloadBytes);
    SnapshotTransfer::Sender sender;
    REQUIRE(sender.prepare(checkpoint));
    std::uint32_t decodedSize = 0;
    REQUIRE(SnapshotTransfer::readHeader(sender.header(), sender.bytes.size(), decodedSize));
    std::string wire;
    for(std::size_t offset = 0; offset < sender.bytes.size();) {
        const auto count = std::min<std::size_t>(48u * 1024, sender.bytes.size() - offset);
        OMemoryStream record;
        record.open();
        record.writeUint32(NETWORKPACKET_COOP_SETUP_TRANSFER);
        record.writeUint8(2);
        record.writeUint32(546);
        record.writeUint32(static_cast<Uint32>(offset));
        record.writeString(sender.bytes.substr(offset, count));
        REQUIRE(record.getDataLength() < P2PWire::Limits::kMaxGamePayloadBytes);
        const auto encoded = P2PWire::encodeGameEnvelope(reinterpret_cast<const std::uint8_t*>(record.getData()),
            record.getDataLength(), 0, 7);
        P2PWire::Envelope envelope;
        const char* reason = nullptr;
        REQUIRE(P2PWire::decodeEnvelope(encoded, envelope, reason));
        IMemoryStream input(reinterpret_cast<const char*>(envelope.payload.data()), envelope.payload.size());
        REQUIRE(input.readUint32() == NETWORKPACKET_COOP_SETUP_TRANSFER);
        REQUIRE(input.readUint8() == 2);
        REQUIRE(input.readUint32() == 546);
        REQUIRE(input.readUint32() == wire.size());
        wire += input.readString();
        REQUIRE(input.getRemainingLength() == 0);
        offset += count;
    }
    REQUIRE(wire.size() == sender.bytes.size());
    std::string restored;
    REQUIRE(SnapshotTransfer::decode(wire, decodedSize, restored));
    REQUIRE(restored == checkpoint);
    REQUIRE_FALSE(RoomRelay::gameMessageRule(NETWORKPACKET_COOP_SETUP_TRANSFER).carried);
    using namespace NetworkPacketPolicy;
    PacketContext context;
    context.packetType = NETWORKPACKET_COOP_SETUP_TRANSFER;
    context.localRole = LocalRole::Client;
    context.admission = PeerAdmission::Established;
    context.phase = SessionPhase::Lobby;
    REQUIRE(classifyPacket(context) == PacketVerdict::Accept);
    context.phase = SessionPhase::InGame;
    REQUIRE(classifyPacket(context) == PacketVerdict::RejectWrongPhase);
}
