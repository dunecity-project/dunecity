// Integration-only main (tests/network/run-spectator-end-probe.py): a real networked match played
// to a decided result by separate processes - Host, Partner and a Viewer that joins as a spectator
// mid-game - over the signaling service and WebRTC, each in the real Game::runMainLoop. The match
// is decided by the simulation itself (the winning house's tanks hunt the loser's troopers), so
// every peer decides at the same cycle. The probe only watches (Game::beforeCycleHook), stalls the
// viewer to make it lag, makes the host leave, and writes what each process saw to SE_OUT:
//   <role>-decided   cycle and digest when one team was left (the same point on every peer)
//   <role>-result    how the process's game ended (key=value lines)
//   running          the host passed SE_JOIN_AT; the viewer asks to spectate then
// Roles synchronise through files in SE_OUT; run-spectator-end-probe.py copies them between
// machines for a multi-machine run.
#include <Game.h>
#include <House.h>
#include <GameInitSettings.h>
#include <Network/NetworkManager.h>
#include <Network/RoomAdmissionClient.h>
#include <Network/WorkshopGameContent.h>
#include <Menu/CustomGameStatsMenu.h>
#include <FileClasses/LoadSavePNG.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <misc/draw_util.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
std::string seRole, seOut;
std::string seRead(const std::string& name) {
    std::ifstream f(seOut + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void seWrite(const std::string& name, const std::string& bytes) {
    const auto path = seOut + "/" + name;
    { std::ofstream f(path + ".tmp", std::ios::binary); f << bytes; }
    std::filesystem::rename(path + ".tmp", path);
}
bool seHas(const std::string& name) { return std::filesystem::exists(seOut + "/" + name); }
Uint32 seEnv(const char* name, Uint32 fallback) { const char* v = std::getenv(name); return v ? std::stoul(v) : fallback; }
std::string seEnvText(const char* name, const std::string& fallback) { const char* v = std::getenv(name); return v ? v : fallback; }

struct SeState {
    bool host = false, viewer = false;
    Uint32 joinAt = 0, decidedCycle = 0, stallMs = 0, slowMs = 0, finishedCycles = 0;
    std::string stallTrigger, leave;
    bool stalled = false, captured = false, runningWritten = false;
    Uint32 decidedTicks = 0;
};
SeState se;

std::string seDigest(const Game& game) { return GameStateDigest::describe(game.computeStateDigest()); }

void seCapture(Game& game, const std::string& name) {
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    if(image) SavePNG(image.get(), (seOut + "/" + name).c_str());
}

void seHook(Game& game) {
    const Uint32 cycle = game.getGameCycleCount();
    if(se.host && !se.runningWritten && cycle >= se.joinAt) { seWrite("running", std::to_string(cycle)); se.runningWritten = true; }
    // One team left: the simulation's own verdict, reached at the same point on every peer.
    if(!se.decidedCycle && game.soleSurvivingTeam() != 0) {
        se.decidedCycle = cycle; se.decidedTicks = SDL_GetTicks();
        seWrite(seRole + "-decided", std::to_string(cycle) + " " + seDigest(game) + "\n");
        SDL_Log("SPECTATOR_END: %s decided at cycle %u, team %d left", seRole.c_str(), cycle, game.soleSurvivingTeam());
        if(se.host && se.leave == "decision") { SDL_Log("SPECTATOR_END: host quits at the decision"); game.quitGame(); }
    }
    if(se.viewer && se.stallMs && !se.stalled) {
        const bool due = se.stallTrigger == "host-decided" ? seHas("Host-decided")
                       : se.stallTrigger.rfind("cycle:", 0) == 0 && cycle >= std::stoul(se.stallTrigger.substr(6));
        if(due) {
            se.stalled = true;
            SDL_Log("SPECTATOR_END: viewer stalls %u ms at cycle %u", se.stallMs, cycle);
            SDL_Delay(se.stallMs);  // No reading, no simulating: a hung or very slow spectator.
            seWrite("Viewer-stalled", std::to_string(cycle) + "\n");
        }
    }
    if(se.viewer && se.slowMs && !game.finished) SDL_Delay(se.slowMs);
    if(game.finished) {
        ++se.finishedCycles;
        if(se.viewer && !se.captured) { seCapture(game, "viewer-plate.png"); se.captured = true; }
    }
}
}

int runSpectatorEndProbe() {
    seRole = std::getenv("SE_ROLE"); seOut = std::getenv("SE_OUT");
    const std::string endpoint = std::getenv("SE_ENDPOINT");
    se.host = seRole == "Host"; se.viewer = seRole == "Viewer";
    se.joinAt = seEnv("SE_JOIN_AT", 120);
    se.stallMs = seEnv("SE_STALL_MS", 0); se.slowMs = seEnv("SE_SLOW_MS", 0);
    se.stallTrigger = seEnvText("SE_STALL_AT", "host-decided");
    se.leave = seEnvText("SE_HOST_LEAVE", "end");
    const Uint32 statsMs = seEnv("SE_STATS_MS", 1500);
    const std::string mod = seEnvText("SE_MOD", "vanilla");
    if(!ModManager::instance().setActiveMod(mod)) throw std::runtime_error("probe mod unavailable");
    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    settings.general.playerName = seRole;
    settings.network.metaServer = "http://127.0.0.1:1/";
    const Uint32 deadline = SDL_GetTicks() + seEnv("SE_DEADLINE_S", 400) * 1000;
    auto wait = [&](auto condition) {
        while(!condition()) { if(SDL_TICKS_PASSED(SDL_GetTicks(), deadline)) throw std::runtime_error("probe wait timeout"); SDL_Delay(5); }
    };

    AdmissionRequest request;
    request.baseUrl = endpoint; request.allowLoopbackPlaintext = true; request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION; request.runtime = "native"; request.contentHash = OnlineModPolicy::fingerprint();
    request.hosting = se.host; request.publicRoom = true; request.maxPeers = 4; request.mode = "custom"; request.allowLateJoin = true;
    request.modName = mod; request.mapName = "Spectator end probe";
    if(!se.host) { wait([&]() { return seHas("room"); }); request.roomCode = seRead("room"); }
    if(se.viewer) {
        wait([&]() { return seHas("running"); });
        request.operation = AdmissionOperation::JoinRequest; request.displayName = seRole; request.publicOnly = true; request.spectate = true;
    }
    RoomAdmissionClient admission;
    auto ask = [&]() {
        admission.begin(request);
        wait([&]() { admission.update(); return admission.status() != RoomAdmissionClient::Status::InProgress; });
        if(admission.status() != RoomAdmissionClient::Status::Succeeded) throw std::runtime_error(admission.errorMessage());
        return admission.response();
    };
    auto admitted = ask();
    if(se.host) seWrite("room", admitted.roomCode);
    if(se.viewer) {
        request.operation = AdmissionOperation::JoinStatus; request.requestTicket = admitted.requestTicket;
        do { SDL_Delay(1000); admitted = ask(); } while(admitted.requestState == "pending");
        if(admitted.requestState != "approved") throw std::runtime_error("spectate request not approved");
    }
    pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    auto& network = *pNetworkManager;
    DirectRoomTransport::Config config;
    config.signalingBaseUrl = endpoint; config.allowLoopbackPlaintext = true; config.grant = admitted.grant;
    config.roomCode = admitted.roomCode; config.displayName = seRole; config.appVersion = VERSIONSTRING;
    config.contentHash = request.contentHash; config.runtime = "native"; config.gameProtocolVersion = NETWORK_PROTOCOL_VERSION;
    config.allowLateJoin = se.host;
    std::string error;
    if(!network.startDirectSession(config, error)) throw std::runtime_error(error);
    network.setOnPeerDisconnected([&](const std::string&, bool, int) {
        throw std::runtime_error("probe disconnected before the game: " + network.getRelayClient()->statusMessage());
    });
    bool start = false;
    network.setOnStartGame([&](unsigned) { start = true; });
    if(se.viewer) network.expectLateJoin();
    GameInitSettings init;
    if(se.host) {
        init = GameInitSettings("spectator-end.ini", seRead("map.ini"), "Host", true, effectiveGameOptions);
        init.randomSeed = seEnv("SE_SEED", 4242);
        GameInitSettings::HouseInfo first(HOUSE_ATREIDES, 1); first.addPlayerInfo({"Host", HUMANPLAYERCLASS}); init.addHouseInfo(first);
        GameInitSettings::HouseInfo second(HOUSE_HARKONNEN, 2); second.addPlayerInfo({"Partner", HUMANPLAYERCLASS}); init.addHouseInfo(second);
        network.setGetChangeEventListForNewPlayerCallback([](const std::string& name) {
            if(name == "Viewer") throw std::runtime_error("a spectator reopened waiting-lobby assignment");
            return ChangeEventList();
        });
        WorkshopGameContent::pin(init);
        network.startServer(false, "Spectator end probe", seRole, &init, 2, 4);
        OMemoryStream bytes; bytes.open(); init.save(bytes); seWrite("init", std::string(bytes.getData(), bytes.getDataLength()));
    } else if(!se.viewer) {
        wait([&]() { network.update(); return seHas("init"); });
        const auto bytes = seRead("init"); IMemoryStream in(bytes.data(), bytes.size()); init = GameInitSettings(in);
    }
    if(!se.viewer) {
        bool sent = false;
        wait([&]() {
            network.update();
            if(se.host && !sent && network.getConnectedPeers().size() == 1u && network.isMeshReady()) {
                sent = network.sendStartGame(0);
                if(!sent) throw std::runtime_error("start refused");
            }
            return start;
        });
    } else {
        wait([&]() { network.update(); return network.lateJoinReady(); });
        init = *network.takeLateJoin();
    }

    // As startMultiPlayerGame: a spectator's fresh checkpoint restarts the Game.
    std::unique_ptr<Game> game;
    unsigned restarts = 0;
    while(true) {
        game = std::make_unique<Game>(); currentGame = game.get();
        game->beforeCycleHook = seHook;
        game->initGame(init);
        game->runMainLoop();
        if(auto resumed = network.takeLateJoin()) { init = *resumed; ++restarts; game.reset(); currentGame = nullptr; continue; }
        break;
    }
    const Uint32 endTicks = SDL_GetTicks();
    std::ostringstream result;
    result << "role=" << seRole << "\n"
           << "last_cycle=" << game->getGameCycleCount() << "\n"
           << "last_digest=" << seDigest(*game) << "\n"
           << "decided_cycle=" << se.decidedCycle << "\n"
           << "finished=" << (game->finished ? 1 : 0) << "\n"
           << "finished_level=" << (game->finishedLevel ? 1 : 0) << "\n"
           << "quit=" << (game->bQuitGame ? 1 : 0) << "\n"
           << "won=" << (game->won ? 1 : 0) << "\n"
           << "finished_cycle=" << game->finishedLevelCycle << "\n"
           << "finished_cycles_run=" << se.finishedCycles << "\n"
           << "watched_winner=" << game->getWatchedWinnerTeam() << "\n"
           << "join_failure=" << network.joinFailure() << "\n"
           << "restarts=" << restarts << "\n"
           << "end_after_decision_ms=" << (se.decidedTicks ? endTicks - se.decidedTicks : 0) << "\n";
    if(se.viewer) {
        result << "watched_text=" << game->watchedResultText() << "\n";
        const int next = game->whatNext();
        result << "what_next=" << next << "\n";
        if(next == GAME_CUSTOM_GAME_STATS) {
            CustomGameStatsMenu stats;
            result << "stats_text=" << stats.resultLabel.getText() << "\n"
                   << "stats_colour_matches=" << (stats.resultLabel.textcolor == game->watchedResultColor(3) ? 1 : 0) << "\n";
        }
    }
    // The statistics screen keeps the network running (MenuBase pumps it) until the player
    // leaves; then the session ends as the menus end it.
    if(!se.viewer && se.leave != "decision") {
        const Uint32 until = SDL_GetTicks() + statsMs;
        while(!SDL_TICKS_PASSED(SDL_GetTicks(), until)) { network.update(); SDL_Delay(5); }
    }
    if(!se.viewer && !se.host) {
        // The partner stays until the host has gone, as a player on the statistics screen would.
        const Uint32 until = SDL_GetTicks() + 60000;
        while(network.getRelayClient()->isJoined() && !network.getConnectedPeers().empty()
              && !SDL_TICKS_PASSED(SDL_GetTicks(), until)) { network.update(); SDL_Delay(5); }
    }
    network.setOnPeerDisconnected({});
    game.reset(); currentGame = nullptr;
    pNetworkManager->disconnect();
    pNetworkManager.reset();
    seWrite(seRole + "-result", result.str());
    SDL_Log("SPECTATOR_END_DONE: %s\n%s", seRole.c_str(), result.str().c_str());
    return MENU_QUIT_DEFAULT;
}
