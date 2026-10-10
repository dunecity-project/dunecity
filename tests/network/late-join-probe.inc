// Integration-only main: real service, WebRTC, saved game and simulation in three processes.
#include <Game.h>
#include <GUI/dune/JoinRequestsWindow.h>
#include <GUI/dune/JoinProgressWindow.h>
#include <GUI/dune/InGameMenu.h>
#include <House.h>
#include <GameInitSettings.h>
#include <Network/NetworkManager.h>
#include <Network/RoomAdmissionClient.h>
#include <Menu/CrossplayMenu.h>
#include <Network/WorkshopGameContent.h>
#include <players/HumanPlayer.h>
#include <units/UnitBase.h>
#include <units/Harvester.h>
#include <units/RebelHarvester.h>
#include <SpatialGrid.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <thread>
#include <chrono>

static std::string joinRead(const std::string& path) { std::ifstream f(path,std::ios::binary); return std::string(std::istreambuf_iterator<char>(f),{}); }
static void joinWrite(const std::string& path,const std::string& bytes) { { std::ofstream f(path+".tmp",std::ios::binary); f<<bytes; } std::filesystem::rename(path+".tmp",path); }
static void joinCapture(Game& game, const std::string& path) {
    if(!std::getenv("JOIN_CAPTURE_UI")) return;
    game.drawScreen();
    sdl2::surface_ptr image{SDL_CreateRGBSurfaceWithFormat(0,getRendererWidth(),getRendererHeight(),32,SDL_PIXELFORMAT_ARGB8888)};
    if(!image || SDL_RenderReadPixels(renderer,nullptr,image->format->format,image->pixels,image->pitch)!=0
       || SDL_SaveBMP(image.get(),path.c_str())!=0) throw std::runtime_error("could not capture join UI");
}
#include "match-controls-probe.inc"
static void joinCheckpointTrace(Game& game, const std::string& role, const std::string& out) {
    if(!std::getenv("JOIN_CHECKPOINT_TRACE")) return;
    const auto dir=out+"/checkpoint-"+std::to_string(game.getGameCycleCount())+"-"+role;
    if(std::filesystem::exists(dir)) return;
    std::filesystem::create_directory(dir);
    joinWrite(dir+"/digest",GameStateDigest::describe(game.computeStateDigest()));
    joinWrite(dir+"/runtime",game.saveObserverRuntime());
    OMemoryStream save; save.open(); game.saveGame(save);
    joinWrite(dir+"/save",std::string(save.getData(),save.getDataLength()));
    std::ostringstream counters;
    for(const auto* unit : unitList) {
        if(const auto* h=dynamic_cast<const Harvester*>(unit))
            counters<<unit->getObjectID()<<" harvester "<<int(h->pathFailCounter)<<' '<<int(h->returnPathFailCounter)<<'\n';
        if(const auto* h=dynamic_cast<const RebelHarvester*>(unit))
            counters<<unit->getObjectID()<<" rebel "<<int(h->pathFailCounter)<<' '<<int(h->returnPathFailCounter)<<'\n';
    }
    for(const auto& h : game.house) if(h) counters<<"house "<<h->getHouseID()<<' '<<h->getCredits()<<' '
        <<h->getNumUnits()<<' '<<h->getNumStructures()<<' '<<h->getMilitaryValue()<<'\n';
    joinWrite(dir+"/counters",counters.str());
    game.objectManager.forEachObject([&](Uint32 id,const ObjectBase* obj) {
        OMemoryStream bytes; bytes.open(); game.saveObject(bytes,const_cast<ObjectBase*>(obj));
        joinWrite(dir+"/object-"+std::to_string(id),std::string(bytes.getData(),bytes.getDataLength()));
    });
}
// Perturb only this viewer's loaded copy, in a field computeStateDigest() mixes, so the
// host's next fingerprint diverges and one real resync runs. Credits are the cheapest such
// field. Spending down is never clamped; addCityCredits() was, because acceptEarnedCredits()
// caps on getEarnedCreditRoom(), which is zero at a mid-match checkpoint with full storage.
static void joinInjectDivergence(Game& game) {
    for(const auto& h : game.house) {
        if(!h || h->getCredits() < 1) continue;
        const auto before=h->getCredits();
        h->takeCredits(100);
        if(h->getCredits()!=before) return;
    }
    throw std::runtime_error("probe could not perturb any house's credits for the desync");
}
int runLateJoinProbe() {
    const std::string role=std::getenv("JOIN_ROLE"), out=std::getenv("JOIN_OUT"), endpoint=std::getenv("JOIN_ENDPOINT");
    const std::string mode=std::getenv("JOIN_MODE") ? std::getenv("JOIN_MODE") : "replace";
    const bool host=role=="Host", newcomer=role=="Newcomer";
    const bool solo=std::getenv("JOIN_SOLO"), city=std::getenv("JOIN_CITY");
    if(!ModManager::instance().setActiveMod(city ? "dunecity" : "vanilla")) throw std::runtime_error("probe mod unavailable");
    effectiveGameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    const bool promotion=mode=="promote";
    const bool observer=mode=="spectate" || mode=="reject_spectate" || promotion;
    Uint32 verifyCycle=observer ? 1800 : 150;
    if(std::getenv("JOIN_VERIFY_CYCLE")) verifyCycle=std::stoul(std::getenv("JOIN_VERIFY_CYCLE"));
    const bool browserPromotion=promotion && std::getenv("JOIN_BROWSER");
    if(std::getenv("JOIN_MENU_DURING_RESYNC")) {
        if(!std::getenv("JOIN_DESYNC_ONCE") && !std::getenv("JOIN_DESYNC_ALWAYS"))
            throw std::runtime_error("JOIN_MENU_DURING_RESYNC needs JOIN_DESYNC_ONCE to force a real resync");
        if(!observer || promotion || std::getenv("JOIN_BROWSER") || std::getenv("JOIN_MATCH_CONTROLS"))
            throw std::runtime_error("JOIN_MENU_DURING_RESYNC requires a native --mode spectate run without match controls");
    }
    bool promotionCommitted=false;
    settings.general.playerName=role;
    settings.network.metaServer="http://127.0.0.1:1/";
    const bool fastWarmup=std::getenv("JOIN_FAST_WARMUP");
    const Uint32 deadline=SDL_GetTicks()+((std::getenv("JOIN_BROWSER") || fastWarmup) ? 600000 : 160000);
    auto wait=[&](auto condition) { while(!condition()) { if(SDL_TICKS_PASSED(SDL_GetTicks(),deadline)) throw std::runtime_error("probe wait timeout"); SDL_Delay(5); } };
    AdmissionRequest request;
    request.baseUrl=endpoint; request.allowLoopbackPlaintext=true; request.appVersion=VERSIONSTRING;
    request.gameProtocol=NETWORK_PROTOCOL_VERSION; request.runtime="native"; request.contentHash=OnlineModPolicy::fingerprint();
    request.hosting=host; request.publicRoom=true; request.maxPeers=4; request.mode="custom"; request.allowLateJoin=true;
    request.modName=city ? "dunecity" : "vanilla";
    request.mapName=std::getenv("JOIN_TWIN_CITIES") ? "2P - 256x256 - Twin Cities"
        : (city ? "4P - 128x128 - Ergsun-Odenkirk" : "Join probe");
    if(!host) { wait([&](){return std::filesystem::exists(out+"/room");}); request.roomCode=joinRead(out+"/room"); }
    if(newcomer) {
        wait([&](){return std::filesystem::exists(out+"/running");});
        request.operation=AdmissionOperation::JoinRequest; request.displayName=role; request.publicOnly=true; request.spectate=mode=="spectate" || promotion;
    }
    RoomAdmissionClient admission;
    auto ask=[&]() { admission.begin(request); wait([&](){admission.update(); return admission.status()!=RoomAdmissionClient::Status::InProgress;});
        if(admission.status()!=RoomAdmissionClient::Status::Succeeded) throw std::runtime_error(admission.errorMessage()); return admission.response(); };
    auto admitted=ask();
    if(host) joinWrite(out+"/room",admitted.roomCode);
    if(newcomer) {
        request.operation=AdmissionOperation::JoinStatus; request.requestTicket=admitted.requestTicket;
        do { SDL_Delay(3000); admitted=ask(); } while(admitted.requestState=="pending");
        if(admitted.requestState!="approved") throw std::runtime_error("request not approved");
    }
    pNetworkManager=std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    auto& network=*pNetworkManager;
    DirectRoomTransport::Config config;
    config.signalingBaseUrl=endpoint; config.allowLoopbackPlaintext=true; config.grant=admitted.grant;
    config.roomCode=admitted.roomCode; config.displayName=role; config.appVersion=VERSIONSTRING;
    config.contentHash=request.contentHash; config.runtime="native"; config.gameProtocolVersion=NETWORK_PROTOCOL_VERSION;
    config.allowLateJoin=host;
    std::string error; if(!network.startDirectSession(config,error)) throw std::runtime_error(error);
    network.setOnPeerDisconnected([&](const std::string&,bool,int){throw std::runtime_error("probe disconnected: "+network.getRelayClient()->statusMessage());});
    bool cancelled=false;
    if(newcomer && (mode=="abort" || std::getenv("JOIN_STALL"))) network.setOnPeerDisconnected([&](const std::string&,bool,int){cancelled=true;});
    bool start=false;
    network.setOnStartGame([&](unsigned){start=true;});
    if(newcomer) network.expectLateJoin();
    GameInitSettings init;
    if(host) {
        auto map=joinRead(PROBE_MAP_PATH);
        if(std::getenv("JOIN_BUSY")) {
            map+="\n";
            for(int i=0;i<100;++i) map+="ID"+std::to_string(100+i)+"=Player"+std::to_string(i<50 ? 1 : 2)
                +",Tank,256,"+std::to_string((i%50)*29+10)+",64,Area Guard\n";
        }
        init=GameInitSettings("join.ini",map,"Host",true,effectiveGameOptions);
        if(std::getenv("JOIN_SEED")) init.randomSeed=std::stoul(std::getenv("JOIN_SEED"));
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES,1); humans.addPlayerInfo({"Host",HUMANPLAYERCLASS}); if(!solo && mode!="share_human") humans.addPlayerInfo({"Partner",HUMANPLAYERCLASS}); else if(city) humans.addPlayerInfo({"Host AI","qBotHard"}); init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemy(HOUSE_HARKONNEN,2); enemy.addPlayerInfo({mode=="share_human" ? "Partner" : "Enemy AI",mode=="share_human" ? HUMANPLAYERCLASS : (city ? "qBotHard" : "qBotEasy")}); init.addHouseInfo(enemy);
        if(city) {
            init.setMultiplePlayersPerHouse(true);
            if(!std::getenv("JOIN_TWIN_CITIES"))
                for(int h : {HOUSE_ORDOS,HOUSE_FREMEN}) { GameInitSettings::HouseInfo other(static_cast<HOUSETYPE>(h),h+3); other.addPlayerInfo({"Enemy AI "+std::to_string(h),"qBotHard"}); init.addHouseInfo(other); }
        }
        // Retain a real waiting-lobby setup/callback, as the production menu does.
        // Newcomers during a match must never invoke lobby seat assignment.
        network.setGetChangeEventListForNewPlayerCallback([](const std::string& name) {
            if(name=="Newcomer") throw std::runtime_error("late join reopened waiting-lobby assignment");
            return ChangeEventList();
        });
        WorkshopGameContent::pin(init);
        network.startServer(false,"Join probe",role,&init,solo ? 1 : 2,4);
        OMemoryStream bytes; bytes.open(); init.save(bytes); joinWrite(out+"/init",std::string(bytes.getData(),bytes.getDataLength()));
    } else if(!newcomer) {
        wait([&](){network.update();return std::filesystem::exists(out+"/init");});
        const auto bytes=joinRead(out+"/init"); IMemoryStream in(bytes.data(),bytes.size()); init=GameInitSettings(in);
    }
    if(!newcomer) {
        bool sent=false;
        wait([&](){ network.update(); if(host && !sent && network.getConnectedPeers().size()==(solo ? 0u : 1u) && network.isMeshReady()) {sent=network.sendStartGame(0); if(!sent) throw std::runtime_error("start refused");} return start; });
    } else {
        // Model a distant/slow receiver while the host keeps its normal simulation
        // cadence. Several queued snapshot chunks can arrive in each read window.
        const Uint32 snapshotPollMs=std::getenv("JOIN_SNAPSHOT_POLL_MS")
            ? std::stoul(std::getenv("JOIN_SNAPSHOT_POLL_MS")) : 0;
        int previousPercent=0; Uint32 previousEpoch=0; bool sawProgress=false;
        wait([&](){
            if(snapshotPollMs) SDL_Delay(snapshotPollMs);
            network.update();
            if(observer && network.joinStage==NetworkManager::JoinStage::Receiving) {
                const int percent=network.lateJoinPercent();
                if(network.observerEpoch==previousEpoch && percent<previousPercent)
                    throw std::runtime_error("download progress moved backwards inside one transfer");
                if(percent>0 && percent<100) sawProgress=true;
                previousEpoch=network.observerEpoch; previousPercent=percent;
            }
            return network.lateJoinReady() || cancelled;
        });
        // Compressed checkpoints may fit in one chunk and legitimately jump 0 -> 100.
        if(observer && !cancelled && ((!sawProgress && network.observerTotal>48u*1024) || network.lateJoinPercent()!=100))
            throw std::runtime_error("download progress did not report actual transfer completion");
        if(cancelled && mode=="abort") { joinWrite(out+"/Newcomer-cancelled","yes"); network.setOnPeerDisconnected({}); pNetworkManager.reset(); return MENU_QUIT_DEFAULT; }
        if(observer && !std::getenv("JOIN_MATCH_CONTROLS")) {
            joinWrite(out+"/observer-delaying",std::to_string(network.observerFrontier()));
            SDL_Delay(std::getenv("JOIN_STALL") ? 40000 : 5000); // Deliberately stop reading: the players must keep simulating.
            if(!std::filesystem::exists(out+"/Host-advanced") || (!solo && !std::filesystem::exists(out+"/Partner-advanced"))) throw std::runtime_error("players waited for spectator loading");
        }
        if(observer && std::getenv("JOIN_STALL")) {
            wait([&](){network.update(); return cancelled || !network.getRelayClient()->isJoined();});
            joinWrite(out+"/Newcomer-left","timed out without blocking players");
            network.setOnPeerDisconnected({}); pNetworkManager.reset();
            SDL_Log("LATE_JOIN_PROBE_PASS: unresponsive spectator isolated"); return MENU_QUIT_DEFAULT;
        }
        init=*network.takeLateJoin();
    }
    auto game=std::make_unique<Game>(); currentGame=game.get(); game->initGame(init); game->initializeGameLoop();
    if(newcomer) {
        joinCheckpointTrace(*game,role,out);
        joinWrite(out+"/Newcomer-loaded","yes");
        if(std::getenv("JOIN_DESYNC_ONCE") || std::getenv("JOIN_DESYNC_ALWAYS")) joinInjectDivergence(*game);
        if(std::getenv("JOIN_MENU_DURING_RESYNC")) {
            // Open the menu through the shipping entry point before the injected mismatch
            // forces the resync, so the fresh checkpoint arrives with a player-owned modal
            // already up. onOptions() does not pause a network match for a non-host.
            game->onOptions();
            if(!dynamic_cast<InGameMenu*>(game->pInGameMenu.get()))
                throw std::runtime_error("probe could not open the in-game menu before the resync");
        }
    }
    bool requested=false, resumed=newcomer || observer, rejected=false, playRequested=false, retried=false;
    std::string declinedRequest;
    const bool menuDuringResync=newcomer && std::getenv("JOIN_MENU_DURING_RESYNC");
    bool menuReplaced=false;
    Uint32 lastProgress=0;
    Uint32 lastOrdersCycle=~Uint32(0);
    bool targetOrderChecked=false;
    bool warmupReleased=false;
    bool deviationInjected=false;
    bool capturedCatchup=false;
    const bool checkDeparture=observer && !promotion && !std::getenv("JOIN_BROWSER");
    const Uint32 joinCycle=std::getenv("JOIN_AT_CYCLE") ? std::stoul(std::getenv("JOIN_AT_CYCLE")) : 60;
    while(!SDL_TICKS_PASSED(SDL_GetTicks(),deadline)) {
        if(std::filesystem::exists(out+"/done")) break;
        if(std::getenv("JOIN_CHECK_SPATIAL")) {
            auto* grid=game->getSpatialGrid();
            for(const auto* unit : unitList) {
                if(!unit->isActive()) continue;
                const auto cell=grid->clampToCell(unit->getLocation());
                if(grid->isCellCoordValid(cell) &&
                   (!unit->getGridHandle().isValid() || unit->getGridHandle().cell!=cell)) {
                    SDL_Log("Unit index mismatch: id=%u cycle=%u location=%d,%d expected=%d,%d actual=%d,%d valid=%d",
                        unit->getObjectID(),game->getGameCycleCount(),unit->getLocation().x,unit->getLocation().y,
                        cell.x,cell.y,unit->getGridHandle().cell.x,unit->getGridHandle().cell.y,unit->getGridHandle().isValid());
                    throw std::runtime_error("unit targeting index did not follow movement");
                }
            }
            if(host && !targetOrderChecked && game->getGameCycleCount()>=joinCycle) {
                std::map<Uint32,Uint32> targets;
                for(const auto* unit : unitList) {
                    const auto* target=unit->findTarget();
                    targets[unit->getObjectID()]=target ? target->getObjectID() : NONE_ID;
                }
                // Model a different movement history with exactly the same
                // objects and positions. Target selection must not change.
                for(auto& row : grid->grid) for(auto& cell : row) std::reverse(cell.begin(),cell.end());
                bool same=true;
                for(const auto* unit : unitList) {
                    const auto* target=unit->findTarget();
                    same &= targets[unit->getObjectID()]==(target ? target->getObjectID() : NONE_ID);
                }
                for(auto& row : grid->grid) for(auto& cell : row) std::reverse(cell.begin(),cell.end());
                if(!same) throw std::runtime_error("target selection depends on grid insertion order");
                targetOrderChecked=true;
            }
        }
        if(newcomer && network.observerCatchingUp() && network.observerHostCycle && network.lateJoinPercent()>=20 && !capturedCatchup) {
            joinCapture(*game,out+"/catchup-progress.bmp"); capturedCatchup=true;
        }
        if(std::getenv("JOIN_TRACE") && game->getGameCycleCount()%200==0) {
            const auto path=out+"/"+role+"-state-"+std::to_string(game->getGameCycleCount());
            if(!std::filesystem::exists(path)) {
                std::ostringstream state;
                state<<GameStateDigest::describe(game->computeStateDigest())<<'\n';
                for(int h=0;h<NUM_HOUSES;++h) if(game->house[h]) {
                    const auto& house=*game->house[h];
                    state<<"house "<<h<<' '<<house.getCredits()<<' '<<house.getNumStructures()<<' '<<house.getNumUnits()<<'\n';
                }
                game->objectManager.forEachObject([&](Uint32 id,const ObjectBase* object) {
                    if(object) state<<"object "<<id<<' '<<object->getItemID()<<' '<<object->getOriginalHouseID()<<' '
                        <<(object->getOwner() ? object->getOwner()->getHouseID() : -1)<<' '
                        <<object->getHealth().getRawValue()<<' '<<object->getLocation().x<<' '<<object->getLocation().y<<'\n';
                });
                joinWrite(path,state.str());
            }
        }
        const Uint32 target=std::getenv("JOIN_BROWSER") ? 30000 : (checkDeparture && !newcomer && std::filesystem::exists(out+"/Newcomer-left") ? verifyCycle+100 : verifyCycle);
        const bool waiting=game->handleNetworkUpdates();
        // Browser admission needs the shipping client's continued participation. Do not
        // let this manually driven native loop ignore a terminal session and certify it.
        if(browserPromotion && game->bQuitGame && !network.lateJoinReady())
            throw std::runtime_error("browser promotion ended the network game before verification");
        // Only the transfer stages: Ready is also lateJoinPaused(), and closes the modal
        // again before the rebuild. doInput() refuses events for anything but join progress
        // while a transfer runs, so any other modal here would be stuck and then destroyed
        // mid-rebuild.
        if(menuDuringResync && network.lateJoinPaused() && !network.lateJoinReady()) {
            if(!dynamic_cast<JoinProgressWindow*>(game->pInGameMenu.get()))
                throw std::runtime_error("observer resync left a modal that is not join progress");
            menuReplaced=true;
        }
        const bool controlsHold=probeMatchControls(*game,role,out);
        if(!network.observerTransfers.empty()) warmupReleased=true;
        if(host) for(const auto& transfer : network.observerTransfers)
            if(!transfer.second.began) { joinCheckpointTrace(*game,role,out); break; }
        if(newcomer && std::getenv("JOIN_DESYNC_ALWAYS") && !network.joinFailure().empty()) {
            if(network.observerResyncAttempts!=2) throw std::runtime_error("retry limit was not enforced");
            joinWrite(out+"/Newcomer-left",network.joinFailure()); break;
        }
        if(newcomer && !network.getRelayClient()->isJoined())
            throw std::runtime_error("newcomer lost its session before state verification");
        if(SDL_GetTicks()-lastProgress>5000) {
            lastProgress=SDL_GetTicks();
            SDL_Log("Probe progress: role=%s cycle=%u join_stage=%d play_state=%s requests=%zu status=%s",
                role.c_str(),game->getGameCycleCount(),static_cast<int>(network.joinStage),
                network.getDirectTransport()->playRequestState().c_str(),network.getDirectTransport()->joinRequests().size(),network.lateJoinStatus().c_str());
        }
        if(observer && !promotion && !newcomer && (network.lateJoinPaused() || network.lateJoinReady())) throw std::runtime_error("spectator paused a player");
        if(observer && !newcomer && std::filesystem::exists(out+"/observer-delaying") && game->getGameCycleCount()>std::stoul(joinRead(out+"/observer-delaying"))+100) joinWrite(out+"/"+role+"-advanced",std::to_string(game->getGameCycleCount()));
        if(network.lateJoinReady()) {
            auto snapshot=network.takeLateJoin(); game.reset(); currentGame=nullptr;
            game=std::make_unique<Game>(); currentGame=game.get(); game->initGame(*snapshot); game->initializeGameLoop(); resumed=true;
            joinCheckpointTrace(*game,role,out);
            if(newcomer && std::getenv("JOIN_DESYNC_ALWAYS")) game->getHouse(HOUSE_ATREIDES)->addCityCredits(100);
            if(browserPromotion) {
                // Manual browser interaction may take longer than 1,800 ticks.
                // Compare both original peers at the same post-checkpoint cycle.
                verifyCycle=game->getGameCycleCount()+300;
                promotionCommitted=true;
            }
            joinWrite(out+"/"+role+"-loaded","yes");
        }
        if(host && !requested && game->getGameCycleCount()>=joinCycle
           && (!std::getenv("JOIN_MATCH_CONTROLS") || (game->isGamePaused() && game->getGameCycleCount()>=700))) {
            if(std::getenv("JOIN_DEVIATED_UNIT") && !deviationInjected) {
                for(auto* unit : unitList) if(unit->getOriginalHouseID()==HOUSE_ATREIDES) {
                    unit->deviate(game->getHouse(HOUSE_HARKONNEN));
                    deviationInjected=true;
                    SDL_Log("Checkpoint regression: deviated unit %u",unit->getObjectID());
                    break;
                }
                if(!deviationInjected) throw std::runtime_error("No unit available for deviation regression");
            }
            joinWrite(out+"/running","yes");
            const auto& requests=network.getDirectTransport()->joinRequests();
            if(!requests.empty() && observer && (!promotion || requests[0].spectator)) {
                if(mode=="reject_spectate" && !rejected) {
                    if(!network.getDirectTransport()->manageJoin("decline",requests[0].id)) throw std::runtime_error("decline failed");
                    rejected=true;
                }
                // Game::handleNetworkUpdates automatically synchronizes spectator requests.
            } else if(!requests.empty()) {
                if(promotion) {
                    if(requests[0].id==declinedRequest) continue;
                    game->getGameInterface().updateJoinRequestButton();
                    auto& notice=game->getGameInterface().joinRequestButton;
                    if(!notice.isVisible() || notice.getText().find(requests[0].name + " wants to play")==std::string::npos)
                        throw std::runtime_error("host has no persistent approval notice");
                    if(game->pInGameMenu) throw std::runtime_error("request interrupted host with an automatic dialog");
                    joinCapture(*game,out+"/host-pending.bmp");
                    notice.pOnClick();
                    auto* dialog=dynamic_cast<JoinRequestsWindow*>(game->pInGameMenu.get());
                    if(!dialog) throw std::runtime_error("approval notice did not open host controls");
                    const auto& selected=dialog->choices.at(dialog->slots.getSelectedIndex());
                    if(selected.controller!=static_cast<int>(game->getHouse(selected.house)->getPlayerList().size()))
                        throw std::runtime_error("approval dialog default would replace an existing player");
                    joinCapture(*game,out+"/host-approval.bmp");
                    if(!rejected) {
                        if(!network.getDirectTransport()->manageJoin("decline",requests[0].id)) throw std::runtime_error("decline play failed");
                        rejected=true; declinedRequest=requests[0].id;
                        game->resumeGame(); game->pInGameMenu.reset();
                        continue;
                    }
                }
                for(const auto& slot : game->availableJoinSlots()) if(slot.house==(mode=="share_human" ? HOUSE_ATREIDES : HOUSE_HARKONNEN) && slot.controller==((mode=="replace" || mode=="abort") ? 0 : 1)) {
                    if(!game->acceptJoinRequest(requests[0].id,requests[0].name,slot)) throw std::runtime_error("accept failed: "+network.lateJoinStatus());
                    requested=true; break;
                }
            }
        }
        if(promotion && newcomer && network.isSpectating()) {
            auto* direct=network.getDirectTransport();
            if(!playRequested || (!retried && direct->playRequestState()=="declined")) {
                if(!direct->requestToPlay()) throw std::runtime_error("request to play failed");
                game->getGameInterface().updateJoinRequestButton();
                const auto& notice=game->getGameInterface().joinRequestButton;
                if(!notice.isVisible() || notice.getText()!=settings.general.playerName + ": waiting for host approval")
                    throw std::runtime_error("requester has no persistent approval notice");
                joinCapture(*game,out+"/requester-pending.bmp");
                if(playRequested) retried=true;
                playRequested=true;
            }
        }
        if(host && mode=="abort" && network.joinStage==NetworkManager::JoinStage::Sending) {
            network.abortLateJoin("Test cancellation"); joinWrite(out+"/aborted","yes"); resumed=true;
        }
        if(!host && !newcomer && mode=="abort" && std::filesystem::exists(out+"/aborted") && !network.lateJoinPaused()) resumed=true;
        if(!network.lateJoinPaused()) {
            if(host && std::getenv("JOIN_BUSY") && !waiting && game->getGameCycleCount()!=lastOrdersCycle && game->getGameCycleCount()%150==0) {
                lastOrdersCycle=game->getGameCycleCount();
                const auto player=game->getPlayerByName("Host")->getPlayerID();
                for(auto* unit : unitList) if(unit->getOwner()->getHouseID()==HOUSE_ATREIDES && unit->getItemID()==Unit_Tank)
                    game->cmdManager.addCommand(Command(player,CMD_UNIT_MOVE2POS,unit->getObjectID(),
                        Uint32((game->getGameCycleCount()/150)%2 ? 42 : 8),Uint32(15),Uint32(1)));
            }
            game->cmdManager.update();
            if(!controlsHold && !waiting && game->getGameCycleCount()<target && (resumed || game->getGameCycleCount()<65)
               && (!fastWarmup || !host || game->getGameCycleCount()<joinCycle || warmupReleased)) game->updateGameState();
        }
        if(checkDeparture && !newcomer && game->getGameCycleCount()==verifyCycle+100) {
            if(!network.getRelayClient()->isJoined() || network.getConnectedPeers().size()!=(solo ? 0u : 1u)) throw std::runtime_error("spectator departure damaged the match");
            if(!std::filesystem::exists(out+"/"+role+"-after-leave")) joinWrite(out+"/"+role+"-after-leave",GameStateDigest::describe(game->computeStateDigest()));
            if(std::filesystem::exists(out+"/done")) break;
        }
        if(resumed && (!browserPromotion || promotionCommitted) && game->getGameCycleCount()==verifyCycle) {
            if(!game->getPlayerByName("Host") || (!solo && !game->getPlayerByName("Partner")) || (mode!="abort" && !observer && !game->getPlayerByName("Newcomer")) || (mode=="replace" && game->getPlayerByName("Enemy AI")) || (mode=="share_ai" && !game->getPlayerByName("Enemy AI"))) throw std::runtime_error("controller roster wrong");
            if(observer && !promotion) {
                if(game->getPlayerByName("Newcomer") || !game->getPlayerByName("Enemy AI") || (newcomer && !network.isSpectating())) throw std::runtime_error("spectator took a slot");
                if(newcomer) {
                    const auto before=game->cmdManager.timeslot.size();
                    game->cmdManager.addCommand(Command(game->getPlayerByName("Host")->getPlayerID(),CMD_PLAYER_PAUSE));
                    if(game->cmdManager.timeslot.size()!=before) throw std::runtime_error("spectator issued a local command");
                }
            }
            if(promotion && (!game->getPlayerByName("Newcomer") || (newcomer && network.isSpectating()))) throw std::runtime_error("spectator was not promoted");
            if(promotion) {
                if(!game->getPlayerByName("Enemy AI")) throw std::runtime_error("shared promotion removed AI");
                // This probe never runs GameInterface::draw, so the button is only refreshed
                // by this explicit call. Visibility alone is not the contract: once no request
                // is pending, updateJoinRequestButton() legitimately falls back to
                // recentJoinNotice() (GameInterface.cpp) to confirm the completed join. Assert
                // that no approval is still outstanding instead.
                game->getGameInterface().updateJoinRequestButton();
                const auto& promotedNotice=game->getGameInterface().joinRequestButton;
                const auto promotedText=promotedNotice.isVisible() ? promotedNotice.getText() : std::string();
                if(promotedText.find("wants to play")!=std::string::npos
                   || promotedText.find("waiting for host approval")!=std::string::npos)
                    throw std::runtime_error("approval request still pending after promotion: "+promotedText);
            }
            if(mode=="abort" && (!network.getRelayClient()->isJoined() || !game->getPlayerByName("Enemy AI") || game->getPlayerByName("Newcomer"))) throw std::runtime_error("abort changed the original match");
            if(menuDuringResync) {
                // Reaching verifyCycle with a matching digest already proves the rebuild
                // survived; these add that the modal was taken over and handed back.
                if(!menuReplaced) throw std::runtime_error("no observer resync happened while the in-game menu was open");
                if(game->pInGameMenu) throw std::runtime_error("a modal still owns input after the checkpoint rebuild");
            }
            if(!std::filesystem::exists(out+"/"+role+"-digest")) joinWrite(out+"/"+role+"-digest",GameStateDigest::describe(game->computeStateDigest()));
            if(checkDeparture && newcomer && std::filesystem::exists(out+"/Host-digest") && (solo || std::filesystem::exists(out+"/Partner-digest"))) {
                if(std::getenv("JOIN_DESYNC_ONCE") && network.observerResyncAttempts!=1)
                    throw std::runtime_error("transient mismatch was not recovered exactly once");
                if(network.observerCatchingUp()) throw std::runtime_error("progress did not finish after catching up");
                network.disconnect(); joinWrite(out+"/Newcomer-left","yes"); break;
            }
            if(std::filesystem::exists(out+"/done")) break;
        }
        SDL_Delay((fastWarmup && host && game->getGameCycleCount()<joinCycle) ||
            (newcomer && network.isSpectating() && network.observerCatchingUp()) ? 0 : 16);
    }
    if(!(newcomer && std::getenv("JOIN_DESYNC_ALWAYS") && std::filesystem::exists(out+"/Newcomer-left")) && !std::filesystem::exists(out+"/"+role+"-digest")) throw std::runtime_error("simulation did not resume: "+network.lateJoinStatus());
    network.setOnPeerDisconnected({}); game.reset(); currentGame=nullptr; pNetworkManager.reset();
    SDL_Log("LATE_JOIN_PROBE_PASS: %s",role.c_str()); return MENU_QUIT_DEFAULT;
}
