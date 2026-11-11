// Included only by run-late-join-probe.py's two-native-peer cases (including the
// original --tornie-case). Reuse the real bootstrap and production game/transport.
#include "late-join-probe.inc"
#include <Campaign/CoopCampaignRuntime.h>
#include <Network/GameInitSettingsPolicy.h>
#include <Map.h>
#include <Menu/RandomSkirmishMenu.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/INIFile.h>
#include <algorithm>
#include <array>

int runNativePeerProbe() {
    const std::string role = std::getenv("JOIN_ROLE"), out = std::getenv("JOIN_OUT");
    const std::string which = std::getenv("JOIN_PEER_CASE"), endpoint = std::getenv("JOIN_ENDPOINT");
    const std::string modName = std::getenv("JOIN_PEER_MOD");
    const bool host = role == "Host", campaign = which == "campaign", skirmish = which == "skirmish";
    const bool vanillaCampaign = campaign && modName == "vanilla";
    constexpr Uint32 finalCycle = 3600, seed = 0x546c001u, skirmishSeed = 20261015;
    const Uint32 deadline = SDL_GetTicks() + std::stoul(std::getenv("JOIN_PEER_TIMEOUT_MS"));
    auto require = [](bool ok, const std::string& message) {
        if(!ok) throw std::runtime_error("Native peer probe: " + message);
    };
    auto wait = [&](auto condition) {
        while(!condition()) {
            require(!SDL_TICKS_PASSED(SDL_GetTicks(), deadline), "connection/start timed out");
            SDL_Delay(5);
        }
    };
    auto& mods = ModManager::instance();
    require(mods.setActiveMod(modName), modName + " mod unavailable");
    require(mods.isTornieFamilyContentActive() == (modName == "Tornie"), "wrong content family active");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    settings.general.playerName = role;
    settings.network.metaServer = "http://127.0.0.1:1/";

    GameInitSettings init;
    if(host) {
        if(vanillaCampaign) {
            // Exactly the hosted Vanilla campaign path, not a custom-map facsimile:
            // authored Atreides mission 8, with two controllers sharing its human house.
            init = GameInitSettings(HOUSE_ATREIDES, effectiveGameOptions, 4);
            init.enableCoop(true, "Vanilla direct campaign");
            auto file = pFileManager->openCampaignFile(init.getFilename());
            const auto size = SDL_RWsize(file.get());
            require(size > 0, "authored Vanilla campaign mission unavailable");
            std::string map(static_cast<std::size_t>(size), '\0');
            require(SDL_RWread(file.get(), map.data(), 1, map.size()) == map.size(), "incomplete campaign mission");
            init.setScenarioData(map);
            init.setRandomSeed(seed);
            for(int h = 0; h < NUM_HOUSES; ++h) {
                if(!isHouseAvailable(static_cast<HOUSETYPE>(h))) continue;
                GameInitSettings::HouseInfo house(static_cast<HOUSETYPE>(h), h == HOUSE_ATREIDES ? 1 : 2);
                if(h == HOUSE_ATREIDES) {
                    house.addPlayerInfo({"Host", HUMANPLAYERCLASS});
                    house.addPlayerInfo({"Partner", HUMANPLAYERCLASS});
                } else house.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(h)), "CampaignAIPlayer"});
                init.addHouseInfo(house);
            }
        } else if(campaign) {
            // Preserve the separate-faction Tornie campaign, including a guest
            // matching an authored enemy faction but retaining independent ownership.
            auto session = coop::CoopCampaignSession::create("tornie-network-546", "Tornie",
                mods.getPlayableHouses(), {HOUSE_ATREIDES, HOUSE_MERCENARY}, {"Host", "Partner"}, seed, false);
            for(int stage = 1; stage < 4; ++stage) session.completeMission(true);
            session.setAlliedControl(true);
            session.setAlliedBuilding(true);
            init = coop::makeGameSettings(session, effectiveGameOptions, "Tornie direct campaign", true);
        } else {
            if(skirmish) {
                // The multiplayer generator really creates open Player1/Player2 starts.
                // Never append fixture units or replace its terrain with the custom map.
                SkirmishGenerator::Params params;
                params.multiplayer = true;
                params.starts = 2;
                params.mapSize = 64;
                params.seed = skirmishSeed;
                params.fixedSeed = true;
                params.symmetric = true;
                params.mod = modName;
                params.mode = RandomSkirmish::modeForMod(modName);
                params.techLevel = 8;
                params.startSide = {"", SkirmishGenerator::BaseSize::Outpost, 2, 0};
                const auto generated = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
                require(generated.houses.size() == 2 && !generated.units.empty(), "generator did not create two armed starts");
                for(const auto& house : generated.houses)
                    require(house.role == SkirmishGenerator::Role::Open && house.skipped == 0,
                            "generated start is not open or its planned kit did not fit");
                joinWrite(out + "/generated-map.ini", generated.ini);
                init = GameInitSettings(generated.name, generated.ini, modName + " direct random skirmish", false, effectiveGameOptions);
            } else {
                auto map = joinRead(PROBE_MAP_PATH);
                // A normal custom map with two MCVs; add combat units for real orders.
                map += "\nIDQA1=Player1,Tank,256,249,64,Guard\nIDQA2=Player1,Tank,256,250,64,Guard\n"
                       "IDQA3=Player2,Tank,256,1331,64,Guard\nIDQA4=Player2,Tank,256,1332,64,Guard\n";
                init = GameInitSettings(modName == "Tornie" ? "tornie-peer-custom.ini" : "native-peer-custom.ini",
                                        map, modName + " direct custom", false, effectiveGameOptions);
            }
            init.setRandomSeed(seed);
            GameInitSettings::HouseInfo first(HOUSE_ATREIDES, 1), second(HOUSE_HARKONNEN, 2);
            first.addPlayerInfo({"Host", HUMANPLAYERCLASS});
            second.addPlayerInfo({"Partner", HUMANPLAYERCLASS});
            init.addHouseInfo(first);
            init.addHouseInfo(second);
        }
        WorkshopGameContent::pin(init);
    }

    AdmissionRequest request;
    request.baseUrl = endpoint;
    request.allowLoopbackPlaintext = true;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.runtime = "native";
    request.contentHash = OnlineModPolicy::fingerprint();
    request.hosting = host;
    request.publicRoom = true;
    request.maxPeers = 2;
    request.mode = vanillaCampaign ? "coop" : "custom";
    request.modName = modName;
    request.mapName = host ? init.getFilename() : which;
    if(!host) {
        wait([&] { return std::filesystem::exists(out + "/room"); });
        request.roomCode = joinRead(out + "/room");
    }
    RoomAdmissionClient admission;
    admission.begin(request);
    wait([&] { admission.update(); return admission.status() != RoomAdmissionClient::Status::InProgress; });
    require(admission.status() == RoomAdmissionClient::Status::Succeeded, admission.errorMessage());
    const auto admitted = admission.response();
    if(host) joinWrite(out + "/room", admitted.roomCode);
    SDL_Log("NATIVE_PEER_ADMITTED: mode=%s mod=%s role=%s operation=%s",
            which.c_str(), modName.c_str(), role.c_str(), host ? "host" : "join");

    pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    auto& network = *pNetworkManager;
    bool gotSettings = host, partnerReady = false, started = false, readySent = false;
    network.setOnReceiveGameInfo([&](const GameInitSettings& received, const ChangeEventList&) {
        require(!host && !gotSettings, "unexpected lobby settings delivery");
        init = received;
        gotSettings = true;
    });
    network.setOnReceiveChatMessage([&](const std::string& name, const std::string& message) {
        if(name == "Partner" && message == "Native peer settings received") partnerReady = true;
    });
    unsigned startCallbacks = 0;
    network.setOnStartGame([&](unsigned) { ++startCallbacks; started = true; });
    network.setOnPeerDisconnected([&](const std::string& name, bool, int reason) {
        throw std::runtime_error("Native peer disconnected: " + name + " reason=" + std::to_string(reason));
    });
    if(host) network.startServer(false, modName + " direct " + which + " probe", role, &init, 2, 2);
    DirectRoomTransport::Config config;
    config.signalingBaseUrl = endpoint;
    config.allowLoopbackPlaintext = true;
    config.grant = admitted.grant;
    config.roomCode = admitted.roomCode;
    config.displayName = role;
    config.appVersion = VERSIONSTRING;
    config.contentHash = request.contentHash;
    config.runtime = "native";
    config.gameProtocolVersion = NETWORK_PROTOCOL_VERSION;
    std::string error;
    require(network.startDirectSession(config, error), error);
    bool startSent = false;
    wait([&] {
        network.update();
        if(!host && gotSettings && network.isMeshReady() && !readySent) {
            network.sendChatMessage("Native peer settings received");
            readySent = true;
        }
        if(host && partnerReady && network.getConnectedPeers().size() == 1 && network.isMeshReady() && !startSent) {
            require(network.sendStartGame(0), "start refused");
            startSent = true;
        }
        return started;
    });
    auto* direct = network.getDirectTransport();
    require(gotSettings && network.isDirectSession() && direct && direct->isJoined()
            && network.isMeshReady() && network.getConnectedPeers() == std::list<std::string>{host ? "Partner" : "Host"},
            "the actual named guest did not join a two-peer direct mesh");
    require(startCallbacks == 1 && (host ? startSent && partnerReady : readySent)
            && direct->startStage_ == DirectRoomTransport::StartStage::Committed
            && direct->startCallbackAccepted_ && direct->phase() == RoomRelay::Phase::Match,
            "both-peer start barrier was not committed");
    require(init.getModName() == modName && init.getGameType() ==
            (vanillaCampaign ? GameType::CampaignCoop : GameType::CustomMultiplayer), "wrong actual game mode/mod");
    if(vanillaCampaign)
        require(init.getMission() == 8 && init.getHouseID() == HOUSE_ATREIDES, "not the authored Vanilla campaign mission");
    require((init.getFiledata().find("[RANDOMSKIRMISH]") != std::string::npos) == skirmish,
            "generated skirmish map missing or substituted into another mode");
    SDL_Log("NATIVE_PEER_STARTED: mode=%s mod=%s role=%s remote=%s settings=%s barrier=committed",
            which.c_str(), modName.c_str(), role.c_str(), host ? "Partner" : "Host", host ? "host" : "network");
    require(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(init, error), error);
    OMemoryStream settingsBytes;
    settingsBytes.open();
    init.save(settingsBytes);
    joinWrite(out + "/" + role + "-settings", std::string(settingsBytes.getData(), settingsBytes.getDataLength()));
    joinWrite(out + "/" + role + "-map.ini", init.getFiledata());

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(init);
    game->initializeGameLoop();
    std::array<Player*, 2> players{game->getPlayerByName("Host"), game->getPlayerByName("Partner")};
    require(players[0] && players[1] && pLocalPlayer == players[host ? 0 : 1], "wrong local controller");
    require(players[0] != players[1] && players[0]->getPlayerID() != players[1]->getPlayerID()
            && players[0]->getPlayerclass() == HUMANPLAYERCLASS && players[1]->getPlayerclass() == HUMANPLAYERCLASS,
            "the guest is not an independent human controller");
    require((players[0]->getHouse() == players[1]->getHouse()) == vanillaCampaign, "wrong shared/separate house ownership");
    const auto context = game->getCoopCampaignContext();
    require((campaign && !vanillaCampaign) == context.has_value(), "wrong separate-faction campaign context");
    if(context) {
        require(context->stage == 4 && context->sourceFaction == HOUSE_ATREIDES && context->followsOriginalCampaign(),
                "wrong authored mission");
        require(context->slots[0].faction == HOUSE_ATREIDES && context->slots[1].faction == HOUSE_MERCENARY,
                "separate factions were not retained");
        for(std::size_t i = 0; i < context->slots.size(); ++i) {
            const auto& slot = context->slots[i];
            if(!slot.active()) continue;
            const auto* house = game->getHouse(slot.house);
            require(house && house->getTeamID() == slot.team(), "invalid mission role/team");
            for(std::size_t j = i + 1; j < context->slots.size(); ++j)
                if(context->slots[j].active()) require(slot.house != context->slots[j].house, "mission roles share ownership");
        }
    }

    std::array<Uint32, 2> units{NONE_ID, NONE_ID};
    std::array<Coord, 2> origins;
    for(int i = 0; i < 2; ++i) {
        for(const auto* unit : unitList) {
            if(unit->getOwner() != players[i]->getHouse() || !unit->isActive()) continue;
            if(i == 1 && unit->getObjectID() == units[0]) continue;
            if(skirmish) {
                if(!unit->isAGroundUnit() || unit->getItemID() == Unit_MCV || unit->getItemID() == Unit_Harvester) continue;
            } else if(unit->getItemID() != (campaign ? Unit_Quad : Unit_Tank)) continue;
            if(units[i] == NONE_ID || unit->getObjectID() < units[i]) {
                units[i] = unit->getObjectID();
                origins[i] = unit->getLocation();
            }
        }
        require(units[i] != NONE_ID, "no controllable combat unit for " + players[i]->getPlayername());
    }
    require(units[0] != units[1], "both controllers scripted the same unit");
    if(skirmish) {
        require(currentGameMap->getSizeX() == 64 && currentGameMap->getSizeY() == 64,
                "generated map dimensions changed");
        auto data = sdl2::RWops_ptr{SDL_RWFromConstMem(init.getFiledata().data(), static_cast<int>(init.getFiledata().size()))};
        INIFile map(data.get());
        require(map.getIntValue("RANDOMSKIRMISH", "Seed", 0) == skirmishSeed
                && map.getIntValue("RANDOMSKIRMISH", "Starts", 0) == 2
                && map.getBoolValue("RANDOMSKIRMISH", "Symmetric", false),
                "guest did not receive the actual deterministic multiplayer generator map");
    }
    std::ostringstream details, trace, commands;
    details << "case=" << which << "\ntransport=DirectP2P/libdatachannel/P2PKit (no TURN, no relay)\n"
            << "role=" << role << "\nprotocol=" << NETWORK_PROTOCOL_VERSION << "\nsave=" << SAVEGAMEVERSION
            << "\nmod=" << mods.getActiveModName() << "\ncontent_base=" << mods.getContentBase(mods.getActiveModName())
            << "\nfamily=" << mods.isTornieFamilyContentActive() << "\nmap=" << init.getFilename()
            << "\ngame_type=" << static_cast<int>(init.getGameType()) << "\nlobby_mode=" << request.mode
            << "\nremote=" << (host ? "Partner" : "Host") << "\nguest_joined=Partner\nstart_barrier=committed"
            << "\nownership=" << (vanillaCampaign ? "shared-house" : "separate-houses")
            << "\ngenerated=" << skirmish << "\nseed=" << init.getRandomSeed()
            << "\noptions_hash=" << init.getGameOptions().getHash()
            << "\nallied_control=" << init.getGameOptions().alliedControl
            << "\nallied_building=" << init.getGameOptions().alliedBuilding
            << "\neasy=" << init.getGameOptions().easyMode << "\nchaos=" << init.getGameOptions().chaosMode << '\n';
    for(int i = 0; i < 2; ++i)
        details << "human=" << players[i]->getPlayername() << " player=" << int(players[i]->getPlayerID())
                << " house=" << players[i]->getHouse()->getHouseID() << " team=" << players[i]->getHouse()->getTeamID()
                << " unit=" << units[i] << " origin=" << origins[i].x << ',' << origins[i].y << '\n';
    if(context) {
        details << "stage=" << context->stage << "\nsource_faction=" << context->sourceFaction
                << "\nmission_variant=" << context->missionVariant << '\n';
        for(std::size_t i = 0; i < context->slots.size(); ++i) {
            const auto& slot = context->slots[i];
            details << "slot=" << i << " active=" << slot.active() << " house=" << slot.house
                    << " faction=" << slot.faction << " team=" << slot.team() << " class=" << slot.playerClass << '\n';
        }
    }
    joinWrite(out + "/" + role + "-details", details.str());
    std::array<unsigned, 2> executedMoves{};
    std::array<bool, 2> moved{};
    Uint32 lastIssued = ~Uint32(0), lastTraced = ~Uint32(0);
    while(!SDL_TICKS_PASSED(SDL_GetTicks(), deadline)) {
        const Uint32 cycle = game->getGameCycleCount();
        if(std::filesystem::exists(out + "/done")) break;
        require(!game->bQuitGame && !game->isGameFinished(), "match ended before 3600 cycles");
        const bool waiting = game->handleNetworkUpdates();
        require(network.isDirectSession() && network.getDirectTransport()->isJoined()
                && network.isMeshReady() && network.getConnectedPeers().size() == 1, "direct connection lost");
        if(cycle % 300 == 0 && cycle != lastTraced) {
            lastTraced = cycle;
            trace << GameStateDigest::describe(game->computeStateDigest()) << '\n';
            joinWrite(out + "/" + role + "-trace", trace.str());
            for(int i = 0; i < 2; ++i) {
                const auto* unit = game->getObjectManager().getObject(units[i]);
                require(unit != nullptr, "scripted combat unit was destroyed");
                moved[i] = moved[i] || unit->getLocation() != origins[i];
            }
        }
        if(cycle < finalCycle && cycle != lastIssued && (cycle == 40 || cycle % 300 == 120)) {
            lastIssued = cycle;
            const int local = host ? 0 : 1;
            if(cycle == 40) {
                for(const auto* unit : unitList)
                    if(unit->getOwner() == players[local]->getHouse() && unit->getItemID() == Unit_MCV)
                        game->cmdManager.addCommand(Command(players[local]->getPlayerID(), CMD_MCV_DEPLOY, unit->getObjectID()));
            } else {
                auto target = origins[local];
                if((cycle / 300) % 2 == 0) target.x += target.x + 3 < currentGameMap->getSizeX() ? 3 : -3;
                game->cmdManager.addCommand(Command(players[local]->getPlayerID(), CMD_UNIT_SETMODE, units[local], Uint32(STOP)));
                game->cmdManager.addCommand(Command(players[local]->getPlayerID(), CMD_UNIT_MOVE2POS,
                    units[local], Uint32(target.x), Uint32(target.y), Uint32(1)));
            }
        }
        game->cmdManager.update();
        if(!waiting && cycle < finalCycle) {
            std::array<std::optional<Coord>, 2> expectedTargets;
            for(const auto& command : game->cmdManager.commandsAt(cycle)) {
                for(int i = 0; i < 2; ++i) {
                    if(command.getPlayerID() != players[i]->getPlayerID()) continue;
                    commands << "cycle=" << cycle << " player=" << players[i]->getPlayername()
                             << " command=" << command.getCommandID();
                    for(const auto value : command.getParameter()) commands << ' ' << value;
                    commands << '\n';
                    if(command.getCommandID() != CMD_UNIT_MOVE2POS) continue;
                    const auto& values = command.getParameter();
                    require(values.size() == 4 && values[0] == units[i], "human move addressed the wrong unit");
                    expectedTargets[i] = Coord(values[1], values[2]);
                    ++executedMoves[i];
                }
            }
            game->updateGameState();
            for(int i = 0; i < 2; ++i) {
                if(!expectedTargets[i]) continue;
                const auto* unit = game->getObjectManager().getObject(units[i]);
                require(unit && unit->getDestination() == *expectedTargets[i], "human move was not applied");
            }
        }
        if(cycle == finalCycle && !std::filesystem::exists(out + "/" + role + "-digest")) {
            require(executedMoves[0] == 12 && executedMoves[1] == 12, "missing local or remote human move commands");
            require(moved[0] && moved[1], "both human armies did not actually move");
            commands << "executed_moves Host=" << executedMoves[0] << " Partner=" << executedMoves[1] << '\n';
            joinWrite(out + "/" + role + "-commands", commands.str());
            OMemoryStream save;
            save.open();
            game->saveGame(save);
            joinWrite(out + "/" + role + "-final.dls", std::string(save.getData(), save.getDataLength()));
            joinWrite(out + "/" + role + "-digest", GameStateDigest::describe(game->computeStateDigest()));
        }
        SDL_Delay(5);
    }
    require(std::filesystem::exists(out + "/" + role + "-digest"), "simulation did not reach 3600 cycles");
    network.setOnPeerDisconnected({});
    game.reset();
    currentGame = nullptr;
    pNetworkManager.reset();
    SDL_Log("NATIVE_PEER_PROBE_PASS: mode=%s mod=%s role=%s guest=Partner cycles=%u moves=12/12",
            which.c_str(), modName.c_str(), role.c_str(), finalCycle);
    if(modName == "Tornie") SDL_Log("TORNIE_PEER_PROBE_PASS: %s %s cycles=%u", which.c_str(), role.c_str(), finalCycle);
    return MENU_QUIT_DEFAULT;
}
