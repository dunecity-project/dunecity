// Integration-only main (tests/network/run-spectator-end-probe.py): a real networked match played
// to a decided result by separate processes - Host, Partner and a Viewer that joins as a spectator
// mid-game - over the signaling service and WebRTC, each in the real Game::runMainLoop. The match
// is decided by the simulation itself (hunting tanks, or the opt-in feature-combo fixture's
// real crime troopers shooting down an airplane over the last enemy building). All fixture
// actions use absolute simulation cycles, including checkpoint catch-up. The probe writes:
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
#include <Map.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <units/AmbientAirplane.h>
#include <dunecity/CitySimulation.h>
#include <FileClasses/LoadSavePNG.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <misc/draw_util.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

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
    bool features = false, featureCheckpoint = false;
    int winner = HOUSE_HARKONNEN;
    Uint32 firstCycle = 0;
    std::string plateText;
    Uint32 plateTextColour = 0, plateFrameColour = 0;
    bool plateObserved = false, plateFramed = false;
};
SeState se;

std::string seDigest(const Game& game) { return GameStateDigest::describe(game.computeStateDigest()); }
Uint64 seSteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string seTimedEvent(Uint32 cycle) {
    return std::to_string(cycle) + " " + std::to_string(seSteadyNs()) + "\n";
}


void seCapture(Game& game, const std::string& name) {
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    if(image) SavePNG(image.get(), (seOut + "/" + name).c_str());
}
void seRequire(bool ok, const std::string& message) {
    if(!ok) throw std::runtime_error("feature-combo: " + message);
}

// No fixture progress is kept outside the saved simulation. A late checkpoint already contains
// the worms, crime troopers and their orders; actions before its cycle MUST NOT be replayed.
// The original interaction fixture uses the same raiseCrimeTroopers/deploy/doAttackObject path.
void seFeatures(Game& game, Uint32 cycle) {
    constexpr Uint32 crimeAt = 150, shootAt = 1500;
    House* native = game.getHouse(HOUSE_NATIVES);
    seRequire(native && native->getTeamID() == NATIVES_TEAM && native->getPlayerList().empty(),
              "missing hidden Natives house");
    unsigned worms = 0;
    std::vector<UnitBase*> troops;
    AmbientAirplane* plane = nullptr;
    StructureBase* last = nullptr;
    for(UnitBase* unit : unitList) {
        if(unit->getItemID() == Unit_Sandworm) {
            ++worms;
            seRequire(unit->getOwner() == native && unit->getOriginalHouseID() == HOUSE_NATIVES,
                      "worm is not owned and counted by Natives");
        }
        if(unit->getItemID() == Unit_Trooper && unit->getOwner() == native) troops.push_back(unit);
        if(unit->getItemID() == Unit_AmbientAirplane) plane = dynamic_cast<AmbientAirplane*>(unit);
    }
    seRequire(worms == 2 && native->getNumItems(Unit_Sandworm) == 2, "lost a fixture worm");
    for(StructureBase* structure : structureList)
        if(structure->getOwner()->getHouseID() != se.winner) last = structure;
    if(cycle == crimeAt) {
        seRequire(last && game.getCitySimulation(), "no crime origin/city simulation");
        const auto raised = game.getCitySimulation()->raiseCrimeTroopers(last, 3);
        seRequire(raised.natives == native && raised.units.size() == 3, "crime did not raise three Natives troopers");
        for(UnitBase* unit : raised.units) {
            seRequire(unit->getOwner() == native && unit->getOriginalHouseID() == HOUSE_NATIVES
                      && unit->getAttackMode() == HUNT && unit->getTarget() == last, "wrong crime ownership/order");
            unit->doSetAttackMode(STOP);  // Saved, ordinary order: await the identical shootAt cycle.
        }
    }
    if(cycle > crimeAt && cycle <= shootAt) {
        seRequire(troops.size() == 3 && native->getNumItems(Unit_Trooper) == 3, "crime troops did not survive checkpoint");
        for(const UnitBase* unit : troops)
            seRequire(unit->getOriginalHouseID() == HOUSE_NATIVES && unit->getAttackMode() == STOP
                      && !unit->getTarget(), "checkpoint changed the staged crime orders");
        if(se.viewer && !se.featureCheckpoint) {
            seRequire(cycle < shootAt, "spectator missed the crime checkpoint window");
            se.featureCheckpoint = true;
            seWrite("Viewer-feature-checkpoint", std::to_string(cycle) + " " + seDigest(game) + "\n");
        }
    }
    if(cycle == shootAt) {
        seRequire(last && !plane && !game.finished && game.soleSurvivingTeam() == 0,
                  "match decided before the aircraft attack");
        last->setHealth(20);  // As in natives-probe: only the airplane's Death Hand can take it.
        plane = dynamic_cast<AmbientAirplane*>(game.getHouse(se.winner)->createUnit(Unit_AmbientAirplane, true));
        seRequire(plane != nullptr, "could not deploy a real city airplane");
        plane->deploy(last->getLocation() + Coord(1, 1));
        plane->currentMaxSpeed = 0;
        plane->setVisible(VIS_ALL, true);
        plane->setHealth(10);
        for(UnitBase* unit : troops) {
            unit->setVisible(VIS_ALL, true);
            seRequire(unit->canAttack(plane), "crime trooper cannot attack airplane");
            unit->doAttackObject(plane, true);
        }
    }
    if(cycle > shootAt && game.soleSurvivingTeam() != 0 && !se.decidedCycle) {
        seRequire(!last && !plane && !game.getHouse(se.winner == HOUSE_ATREIDES ? HOUSE_HARKONNEN : HOUSE_ATREIDES)->isAlive(),
                  "aircraft crash did not eliminate the last enemy building");
        seRequire(native->getNumKilledItems(Unit_AmbientAirplane) == 1
                  && native->getNumKilledItems(Structure_WindTrap) == 1 && native->getNumDestroyedStructures() == 1,
                  "airplane and crash building kills not credited exactly to Natives");
        seRequire(game.getHouse(se.winner)->getNumLostItems(Unit_AmbientAirplane) == 1,
                  "winner did not lose its airplane");
        for(int h : {HOUSE_ATREIDES, HOUSE_HARKONNEN})
            seRequire(game.getHouse(h)->getNumDestroyedUnits() == 0 && game.getHouse(h)->getNumDestroyedStructures() == 0,
                      "a played house stole Natives kill credit");
        seWrite(seRole + "-features", std::to_string(cycle) + " " + seDigest(game) + " worms="
                + std::to_string(worms) + " native_unit_kills=" + std::to_string(native->getNumDestroyedUnits())
                + " native_airplane_kills=" + std::to_string(native->getNumKilledItems(Unit_AmbientAirplane))
                + " native_structure_kills=" + std::to_string(native->getNumDestroyedStructures())
                + " airplane_losses=" + std::to_string(game.getHouse(se.winner)->getNumLostItems(Unit_AmbientAirplane)) + "\n");
    }
    seRequire(cycle <= shootAt + MILLI2CYCLES(60 * 1000) || game.finished, "airplane was not shot down in 60 game seconds");
}


void seHook(Game& game) {
    const Uint32 cycle = game.getGameCycleCount();
    if(se.features) {
        if(!se.firstCycle) {
            se.firstCycle = cycle;
            if(se.viewer) seRequire(cycle > 150 && cycle < 1500, "late spectator did not load a post-crime checkpoint");
        }
        seFeatures(game, cycle);
    }
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
            if(se.features) seWrite("Viewer-stall-start", seTimedEvent(cycle));
            SDL_Delay(se.stallMs);  // No reading, no simulating: a hung or very slow spectator.
            if(se.features) {
                // Causal proof, without comparing clocks on different machines: the Host saw
                // this start before its real verdict, and these actual Host events reached the
                // blocked Viewer's directory before it records the end of its stall.
                seRequire(seHas("Host-verdict-event") && seHas("Host-departed"),
                          "host verdict/departure did not reach the viewer during its stall");
                seWrite("Viewer-stall-end", seTimedEvent(cycle)
                        + seRead("Host-verdict-event") + seRead("Host-departed"));
            }
            seWrite("Viewer-stalled", std::to_string(cycle) + "\n");
        }
    }
    if(se.viewer && se.slowMs && !game.finished) SDL_Delay(se.slowMs);
    if(game.finished) {
        ++se.finishedCycles;
        if(!se.captured && !(se.host && se.leave == "decision")) {
            seCapture(game, se.viewer ? "viewer-plate.png" : seRole + "-plate.png");
            se.captured = true;
        }
    }
}
}

// Called only by the private diagnostic Game.cpp, at the production verdict and rendering
// entry points. These observe actual branches/parameters; they never select an expected result.
extern "C" void spectatorProbeVerdict(unsigned cycle, bool applied) {
    if(!se.features || !se.host) return;
    if(!applied) {
        seRequire(seHas("Viewer-stall-start") && !seHas("Viewer-stall-end"),
                  "actual host verdict did not follow the viewer's stall start");
        seWrite("Host-verdict-armed", seTimedEvent(cycle) + seRead("Viewer-stall-start"));
    } else {
        seRequire(seHas("Host-verdict-armed"), "host verdict did not enter its observed production branch");
        seWrite("Host-verdict-event", seTimedEvent(cycle) + seRead("Host-verdict-armed"));
    }
}

extern "C" void spectatorProbePlate(const char* text, unsigned colour, unsigned frame, bool framed) {
    se.plateObserved = true;
    se.plateText = text;
    se.plateTextColour = colour;
    se.plateFrameColour = frame;
    se.plateFramed = framed;
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
    se.features = seEnv("SE_FEATURE_COMBO", 0) != 0;
    se.winner = seEnv("SE_HOST_WINS", 0) ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
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
        if(se.features) {
            // Fixture-only rules are installed on EVERY init, including a resumed checkpoint.
            // The hovering-plane interaction must not rely on an unsaved mutation on the host.
            game->objectData.data[Unit_AmbientAirplane][se.winner].maxspeed = 0;
            for(int house : {HOUSE_ATREIDES, HOUSE_HARKONNEN})
                game->objectData.data[Structure_WindTrap][house].infspawnprop = 0;
        }
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
           << "feature_checkpoint=" << (se.featureCheckpoint ? 1 : 0) << "\n"
           << "initial_cycle=" << se.firstCycle << "\n"
           << "end_after_decision_ms=" << (se.decidedTicks ? endTicks - se.decidedTicks : 0) << "\n";
    result << "plate_presented=" << (se.plateObserved ? 1 : 0) << "\n"
           << "plate_text=" << se.plateText << "\n"
           << "plate_text_colour=" << se.plateTextColour << "\n"
           << "plate_frame_colour=" << se.plateFrameColour << "\n"
           << "plate_text_white=" << (se.plateTextColour == COLOR_WHITE ? 1 : 0) << "\n"
           << "plate_framed=" << (se.plateFramed ? 1 : 0) << "\n"
           << "plate_frame_matches=" << (se.plateFramed && se.plateFrameColour == game->watchedResultColor(1) ? 1 : 0) << "\n"
           << "watched_text=" << game->watchedResultText() << "\n";
    const int next = game->whatNext();
    result << "what_next=" << next << "\n";
    const bool quitBoundary = se.host && se.leave == "decision" && game->bQuitGame && next == GAME_RETURN_TO_MENU;
    result << "presentation_boundary=" << (quitBoundary ? "host-decision-quit-return-to-menu" : "normal") << "\n"
           << "stats_presented=" << (next == GAME_CUSTOM_GAME_STATS ? 1 : 0) << "\n";
    if(next == GAME_CUSTOM_GAME_STATS) {
        CustomGameStatsMenu stats;
        const Uint32 localColour = getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3);
        const auto& winnerRow = stats.houseStat[se.winner];
        result << "stats_text=" << stats.resultLabel.getText() << "\n"
               << "stats_colour=" << stats.resultLabel.textcolor << "\n"
               << "stats_colour_matches=" << (stats.resultLabel.textcolor == game->watchedResultColor(3) ? 1 : 0) << "\n"
               << "stats_caption=" << stats.captionLabel.getText() << "\n"
               << "stats_caption_colour=" << stats.captionLabel.textcolor << "\n"
               << "stats_caption_colour_matches=" << (stats.captionLabel.textcolor == localColour ? 1 : 0) << "\n"
               << "stats_winner_row=" << winnerRow.houseName.getText() << "\n"
               << "stats_winner_colour=" << winnerRow.houseName.textcolor << "\n"
               << "stats_winner_colour_matches=" << (winnerRow.houseName.textcolor == getHouseColorRGB(getHouseVisualHouse(se.winner), 3) ? 1 : 0) << "\n"
               << "stats_local_row=" << stats.houseStat[pLocalHouse->getHouseID()].houseName.getText() << "\n"
               << "stats_local_colour=" << stats.houseStat[pLocalHouse->getHouseID()].houseName.textcolor << "\n"
               << "stats_local_colour_matches=" << (stats.houseStat[pLocalHouse->getHouseID()].houseName.textcolor == localColour ? 1 : 0) << "\n"
               << "stats_natives_hidden=" << (stats.houseStat[HOUSE_NATIVES].houseName.getText().empty() ? 1 : 0) << "\n";
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
    const Uint32 departedCycle = game->getGameCycleCount();
    game.reset(); currentGame = nullptr;
    pNetworkManager->disconnect();
    if(se.host && se.features) {
        seRequire(!pNetworkManager->getRelayClient()->isJoined(), "host transport still joined after disconnect");
        seRequire(seHas("Viewer-stall-start") && !seHas("Viewer-stall-end"),
                  "host departure did not occur in the armed viewer stall");
        seWrite("Host-departed", seTimedEvent(departedCycle) + seRead("Host-verdict-armed"));
    }
    pNetworkManager.reset();
    seWrite(seRole + "-result", result.str());
    SDL_Log("SPECTATOR_END_DONE: %s\n%s", seRole.c_str(), result.str().c_str());
    return MENU_QUIT_DEFAULT;
}
