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

#include <Menu/CrossplayMenu.h>
#include <mod/ModManager.h>
#include <misc/MenuPalette.h>
#include <Network/WorkshopGameContent.h>
#include <Menu/PlaySetup.h>
#include <Menu/SinglePlayerMenu.h>
#include <Menu/MultiPlayerMenu.h>

#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/SinglePlayerSkirmishMenu.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/MsgBox.h>

#include <Network/DirectPeerConnection.h>
#include <Network/DirectRoomTransport.h>
#include <Network/RoomSessionTransport.h>
#include <Network/NetworkManager.h>
#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <config.h>
#include <globals.h>
#include <main.h>
#include <sand.h>
#include <misc/WebRuntime.h>
#include <players/QuantBotConfig.h>

#include <algorithm>
#include <sstream>

namespace {

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if(first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

std::string CrossplayMenu::contentFingerprint() const {
    const auto active = ModManager::instance().getActiveModName();
    if(fingerprintMod == active && !fingerprintHash.empty()) return fingerprintHash;
    try {
        fingerprintHash = OnlineModPolicy::fingerprint();
        fingerprintMod = active;
        return fingerprintHash;
    }
    catch(const std::exception& error) {
        SDL_Log("Could not fingerprint Workshop content: %s", error.what());
        return {};
    }
}

CrossplayMenu::CrossplayMenu() : MenuBase() {
    setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
    resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    setWindowWidget(&windowWidget);
    captionLabel.setText(_("Play Online"));
    captionLabel.setTextFontSize(22);
    captionLabel.setAlignment(Alignment_HCenter);
    playerNameLabel.setText(_("Player name"));
    playerNameValue.setText(settings.general.playerName);
    chatTitle.setText(_("Public chat"));
    chatTitle.setTextFontSize(18);
    chatLabel.setTextFontSize(12);
    chatHistory.setTextFontSize(14);
    directoryLabel.setTextFontSize(12);
    modeFilter.addEntry(_("All games"), 0);
    modeFilter.addEntry(_("Campaign co-op"), 1);
    modeFilter.addEntry(_("Custom games"), 2);
    modeFilter.setSelectedItem(0);
    modeFilter.setOnSelectionChange([this](bool interactive) { if(interactive) refreshDirectory(); });
    availableMods = ModManager::instance().listMods();
    modFilter.addEntry(_("All mods"), -1);
    for(size_t i=0; i<availableMods.size(); ++i)
        modFilter.addEntry(availableMods[i].displayName, static_cast<int>(i));
    modFilter.setSelectedItem(0);
    modFilter.setOnSelectionChange([this](bool interactive) { if(interactive) refreshDirectory(); });
    waitingLabel.setText(_("Players waiting"));
    waitingLabel.setTextFontSize(14);
    waitingNames.setText(_("Connecting..."));
    waitingNames.setTextFontSize(12);
    refreshGamesButton.setText(_("Refresh"));
    refreshGamesButton.setOnClick([this]() { refreshPublicGames(); });
    moreGamesButton.setText(_("More"));
    moreGamesButton.setOnClick([this]() { refreshPublicGames(nextDirectoryPage); });
    publicGameList.setOnSelectionChange([this](bool) { refreshControls(); });
    publicGameList.setOnDoubleClick([this]() { joinPublicGame(); });
    joinPublicButton.setText(_("Join selected game"));
    joinPublicButton.setOnClick([this]() { joinPublicGame(); });
    joinLabel.setText(_("Invite code"));
    joinCodeTextBox.setMaximumTextLength(16);
    joinCodeTextBox.setOnReturn([this]() { onJoin(); });
    joinButton.setText(_("Join code"));
    joinButton.setOnClick([this]() { onJoin(); });
    hostCustomGameButton.setText(_("Create Custom Game"));
    hostCustomGameButton.setOnClick([this]() { onHostCustomGame(); });
    hostRandomSkirmishButton.setText(_("Create Random Skirmish"));
    hostRandomSkirmishButton.setOnClick([this]() { onHostRandomSkirmish(); });
    hostCoopButton.setText(_("Create Campaign"));
    hostCoopButton.setOnClick([this]() { onHostCampaignCoop(); });
    visibilityChoice.addEntry(_("Private - invite code"));
    visibilityChoice.addEntry(_("Public - anyone can join"));
    visibilityChoice.setSelectedItem(1);
    chatLabel.setText(_("Connecting to public chat..."));
    chatInput.setMaximumTextLength(120);
    chatInput.setOnReturn([this]() { sendLobbyChat(); });
    chatSendButton.setText(_("Send"));
    chatSendButton.setOnClick([this]() { sendLobbyChat(); });
#ifdef __EMSCRIPTEN__
    otherConnections.setText(_("Find Match"));
#else
    otherConnections.setText(_("LAN / direct connection"));
#endif
    otherConnections.setOnClick([]() { MultiPlayerMenu().showMenu(); });
    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onBack(); });
    statusLabel.setTextFontSize(12);
    preparedSummary.setTextFontSize(14);
    selectedGameDetails.setTextFontSize(12);
    showPrivateJoin = true;
    layoutControls();
    if(settings.network.activeDirectEndpoint().empty() || !isDirectPeerConnectionAvailable()) {
        setStatus(_("Online play is unavailable in this build."));
        chatLabel.setText(_("Public chat is unavailable."));
        stage = Stage::Finished;
    } else setStatus(_("Join a game, enter an invite code, or create your own."));
    refreshControls();
    if(stage == Stage::Choosing) refreshPublicGames();
}

CrossplayMenu::CrossplayMenu(const GameInitSettings& game, bool publicGame, const ChangeEventList& players, bool allowLateJoin, bool startImmediately)
    : CrossplayMenu() {
    directory.cancel();
    directoryPending = false;
    preparedGame = std::make_unique<GameInitSettings>(game);
    preparedPlayers = players;
    this->allowLateJoin = allowLateJoin && OnlineModPolicy::approved();
    this->startImmediately = startImmediately && OnlineModPolicy::approved();
    hostingCoop = isCoopGameType(game.getGameType());
    visibilityChoice.setSelectedItem(publicGame ? 1 : 0);
    autoHostRequested = stage == Stage::Choosing;
    captionLabel.setText(hostingCoop ? _("Campaign co-op") : _("Custom Game"));
    preparedSummary.setText(game.getModName() + "\n" + game.getFilename()
        + "\n" + (publicGame ? _("Public game") : _("Private game - invite by code")));
    layoutControls();
    refreshControls();
}

void CrossplayMenu::layoutControls() {
    windowWidget.removeAllChildWidgets();
    const int x = 20, w = getSize().x - 40;
    const int h = getSize().y;
    auto place = [this](Widget* widget, int x, int y, int width, int height) {
        windowWidget.addWidget(widget, Point(x,y), Point(width,height));
    };
    place(&captionLabel,x,12,w,30);
    // Desert skins draw the original background's house crests (53x54, 11 px in) in the
    // top-left and bottom-left corners: the controls in those rows start beside them.
    const int crestX = MenuTheme::isDesertUISkin() ? std::max(x, 11 + 53 + 6) : x;
    place(&playerNameLabel,crestX,49,95,24);
    place(&playerNameValue,crestX+100,49,220,26);
    if(preparedGame) {
        place(&preparedSummary,x+20,110,w-40,150);
        place(&refreshGamesButton,x+20,280,160,30);
        refreshGamesButton.setText(_("Retry connection"));
        refreshGamesButton.setOnClick([this]() { if(stage == Stage::Choosing) beginAdmission(true); });
    } else {
        const int chatWidth = std::clamp(w / 3, 220, 420);
        const int leftWidth = w - chatWidth - 16;
        const int chatX = x + leftWidth + 16;
        place(&modeFilter,x,88,150,28);
        place(&modFilter,x+160,88,leftWidth-160,28);
        place(&directoryLabel,x,120,leftWidth-170,26);
        place(&refreshGamesButton,x+leftWidth-165,120,85,26);
        place(&moreGamesButton,x+leftWidth-75,120,75,26);
        place(&publicGameList,x,150,leftWidth,h-406);
        place(&selectedGameDetails,x,h-252,leftWidth,40);
        place(&joinLabel,x,h-208,95,26);
        place(&joinPublicButton,x+leftWidth-130,h-208,130,28);
        place(&joinCodeTextBox,x,h-172,leftWidth-130,28);
        place(&joinButton,x+leftWidth-125,h-172,125,28);
        place(&chatTitle,chatX,49,chatWidth,26);
        place(&chatLabel,chatX,78,chatWidth,32);
        place(&chatHistory,chatX,114,chatWidth,h-372);
        place(&waitingLabel,chatX,h-252,chatWidth,24);
        place(&waitingNames,chatX,h-226,chatWidth,46);
        place(&chatInput,chatX,h-172,chatWidth-65,28);
        place(&chatSendButton,chatX+chatWidth-60,h-172,60,28);
        place(&visibilityChoice,x,h-133,std::min(w,300),28);
        const int hostWidth = (w-20)/3;
        place(&hostCoopButton,x,h-99,hostWidth,28);
        place(&hostCustomGameButton,x+hostWidth+10,h-99,hostWidth,28);
        place(&hostRandomSkirmishButton,x+2*(hostWidth+10),h-99,w-2*(hostWidth+10),28);

    }
    place(&statusLabel,crestX,h-65,w-(crestX-x),26);
    place(&joinProgress,crestX,h-65,w-(crestX-x),26);
    joinProgress.setVisible(false);
    place(&backButton,crestX,h-34,95,28);
    // In the browser this opens the matchmaking lobby (Find Match), natively
    // the LAN/direct connection screen; the button is the same MultiPlayerMenu
    // route either way.
    if(!preparedGame) place(&otherConnections,x+w-215,h-34,215,28);
}

void CrossplayMenu::refreshDirectory() {
    std::string selectedRoom;
    const int selected = publicGameList.getSelectedIndex();
    if(selected >= 0 && selected < static_cast<int>(publicGames.size())) selectedRoom = publicGames[selected].roomCode;
    publicGameList.clearAllEntries();
    publicGames.clear();
    const int filter = modeFilter.getSelectedIndex();
    const int selectedMod = modFilter.getSelectedEntryIntData();
    for(const auto& game : allPublicGames) {
        if(selectedMod >= 0 && selectedMod < static_cast<int>(availableMods.size())) {
            const auto& wanted = availableMods[selectedMod];
            if(!game.modName.empty() ? (game.modName != wanted.name && game.modName != wanted.displayName)
                : wanted.name != ModManager::instance().getActiveModName() || (!game.contentHash.empty() && game.contentHash != contentFingerprint())) continue;
        }
        if((filter == 1 && game.mode != "coop") || (filter == 2 && game.mode != "custom")) continue;
        std::string modLabel=game.modName;
        for(const auto& mod : availableMods) if(mod.name==game.modName) { modLabel=mod.displayName; break; }
        publicGames.push_back(game);
        std::string mapLabel=game.mapName;
        const auto last=mapLabel.rfind(" - ");
        if(last!=std::string::npos) mapLabel=mapLabel.substr(last+3);
        if(mapLabel.size()>22) mapLabel=mapLabel.substr(0,19)+"...";
        if(modLabel.size()>14) modLabel=modLabel.substr(0,11)+"...";
        if(mapLabel.empty()) mapLabel = _("Unknown map");
        const std::string stateLabel = game.running
            ? _("In progress") + ", " + std::to_string(game.elapsedSeconds/60)+_(" min") : _("Waiting");
        std::string row = mapLabel + " - " + modLabel + " - " + stateLabel;
        // On compact screens the state must not disappear off the clipped right edge.
        if(game.running && static_cast<int>(GUIStyle::getInstance().getTextWidth(row,14)) > publicGameList.getSize().x-24)
            row = stateLabel + " - " + mapLabel + " - " + modLabel;
        publicGameList.addEntry(row);
        if(game.roomCode == selectedRoom) publicGameList.setSelectedItem(static_cast<int>(publicGames.size())-1);
    }
    directoryLabel.setText(publicGames.empty() ? _("No games available") : _("Available games"));
    refreshControls();
}

CrossplayMenu::~CrossplayMenu() {
    if(!joinTicket.empty()) { auto r=lobbyRequest(); r.requestTicket=joinTicket; RoomAdmissionClient::cancelJoinRequest(r); }
    directory.cancel();
    chat.cancel();
    visibilityUpdate.cancel();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRoomSession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager.reset();
    }
}

void CrossplayMenu::setStatus(const std::string& message) {
    statusText = message;
    statusLabel.setText(message);
}

void CrossplayMenu::refreshControls() {
    const bool idle = (stage == Stage::Choosing);
    modFilter.setEnabled(idle);
    modeFilter.setEnabled(idle);
    const bool busy = stage==Stage::WaitingForApproval || (stage == Stage::Requesting) || (stage == Stage::Connecting);

    hostCustomGameButton.setEnabled(idle);
    hostRandomSkirmishButton.setEnabled(idle);
    hostCoopButton.setEnabled(idle);
    const bool invite = idle && showPrivateJoin;
    joinLabel.setVisible(invite);
    joinButton.setVisible(invite);
    joinCodeTextBox.setVisible(invite);
    privateInviteButton.setVisible(idle);
    privateInviteButton.setText(showPrivateJoin ? _("Hide private invite") : _("Join with invite code"));
    joinButton.setEnabled(invite);
    joinCodeTextBox.setEnabled(invite);
    chatInput.setEnabled(!chatSession.empty());
    chatSendButton.setEnabled(!chatSession.empty() && !chatPending);
    visibilityChoice.setEnabled(idle || (stage == Stage::HostReady
        && !visibilityPending && !grantedRoom.controlToken.empty()));
    // A background directory refresh must not steal selection/keyboard focus.
    // Joining a cached listing is safe: admission validates that it is still open.
    publicGameList.setEnabled(idle);
    refreshGamesButton.setEnabled(idle && !directoryPending);
    moreGamesButton.setEnabled(idle && !directoryPending && nextDirectoryPage > 0);
    const int selected = publicGameList.getSelectedIndex();
    if(selected>=0 && static_cast<size_t>(selected)<publicGames.size()) {
        const auto& game=publicGames[selected];
        selectedGameDetails.setText(game.hostName+" | "+game.mapName+" | "+game.modName+"\n"
            + (game.running ? _("In progress") + ", " + std::to_string(game.elapsedSeconds/60)+_(" minutes played") : _("Waiting to start"))
            + " | " + std::to_string(game.players)+_(" players"));
    } else selectedGameDetails.setText(_("Select a game to see its details."));
    joinPublicButton.setText(selected >= 0 && static_cast<std::size_t>(selected) < publicGames.size()
        && publicGames[selected].running ? _("Spectate") : _("Join Game"));
    joinPublicButton.setEnabled(idle && selected >= 0
        && static_cast<std::size_t>(selected) < publicGames.size());

    // Once in a room as the host, the host buttons become "what do you want to play".
    if(stage == Stage::HostReady) {
        hostCustomGameButton.setEnabled(!hostingCoop && !visibilityPending);
        hostRandomSkirmishButton.setEnabled(!hostingCoop && !visibilityPending);
        hostCoopButton.setEnabled(hostingCoop && !visibilityPending);
        hostCustomGameButton.setText(_("Choose a Map"));
        hostRandomSkirmishButton.setText(_("Generate a Random Map"));
        hostCoopButton.setText(_("Choose a Campaign Mission"));
    }

    const bool showCode = !publicRoom && !roomCode.empty() && stage == Stage::HostReady;
    roomCodeLabel.setText(showCode ? (_("Game code: ") + roomCode) : std::string());
    copyCodeButton.setVisible(showCode);
    copyCodeButton.setEnabled(showCode);
    copyCodeButton.setText(_("Copy code"));

    backButton.setEnabled(true);
    backButton.setText(busy ? _("Cancel") : _("Back"));
    if(preparedGame) refreshGamesButton.setEnabled(idle);
}

void CrossplayMenu::refreshPublicGames(unsigned offset) {
    if((stage != Stage::Choosing && stage != Stage::HostReady && stage != Stage::ClientWaiting)
       || directoryPending) return;
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    const std::string fingerprint = contentFingerprint();
    if(chatContentHash != fingerprint) {
        // Changing mods moves discovery and chat to the same content lobby.
        chat.cancel();
        chatPending = false;
        chatSession.clear();
        chatCursor = 0;
        chatLines.clear();
        chatHistory.setText("");
        chatContentHash = fingerprint;
        nextChatPoll = 0;
    }
    if(fingerprint.empty()) {
        directoryLabel.setText(_("Cannot check game content"));
        return;
    }
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.listing = true;
    request.allMods = true;
    request.details = true;
    request.listOffset = offset;
    directoryPending = true;
    directoryLabel.setText(_("Finding public games..."));
    directory.begin(request);
    nextDirectoryRefresh = SDL_GetTicks() + 15000;
    refreshControls();
}

bool CrossplayMenu::activateGameContent(const std::string& fingerprint, bool running) {
    if(!OnlineModPolicy::approvedName(fingerprint).empty()) {
        if(OnlineModPolicy::activateApproved(fingerprint)) return true;
        setStatus(_("This game needs the matching approved mod. Update the game to join."));
        return false;
    }
    if(running) {
        setStatus(_("New mods cannot be joined after the game starts. Join their pregame lobby."));
        return false;
    }
    if(fingerprint.size() != 64 || !RoomRelay::isLowercaseHex(fingerprint)) {
        setStatus(_("This host has not shared a verified mod revision.")); return false;
    }
    if(Workshop::activateModRevision(fingerprint)
       || (Workshop::downloadWithProgress(fingerprint) && Workshop::activateModRevision(fingerprint))) return true;
    setStatus(_("The game's exact mod version could not be downloaded."));
    return false;
}

void CrossplayMenu::joinPublicGame() {
    const int index = publicGameList.getSelectedIndex();
    if(stage != Stage::Choosing || directoryPending || index < 0
       || static_cast<std::size_t>(index) >= publicGames.size()) return;
    const auto& game=publicGames[index];
    if(!activateGameContent(game.contentHash, game.running)) return;
    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    // Running games always open in the passive view. A spectator can ask the
    // host for a playing slot after the map has loaded.
    joiningAsSpectator=game.running;
    beginAdmission(false,true);
}

AdmissionRequest CrossplayMenu::lobbyRequest() const {
    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = NETWORK_PROTOCOL_VERSION;
    request.contentHash = contentFingerprint();
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    return request;
}

void CrossplayMenu::changeVisibility() {
    if(stage == Stage::Choosing) { publicRoom = visibilityChoice.getSelectedIndex() == 1; return; }
    if(stage != Stage::HostReady || visibilityPending || grantedRoom.controlToken.empty()) return;
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::Visibility;
    request.roomCode = roomCode;
    request.controlToken = grantedRoom.controlToken;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 1;
    visibilityPending = true;
    setStatus(_("Updating game visibility..."));
    visibilityUpdate.begin(request);
    refreshControls();
}

void CrossplayMenu::enterLobbyChat() {
    if(chatPending || !chatSession.empty()) return;
    // The settings name is the single identity for chat and game admission.
    const auto& name = settings.general.playerName;
    nextChatPoll = SDL_GetTicks() + 15000;
    if(!RoomRelay::isAcceptableDisplayName(name)) {
        chatLabel.setText(_("Set your player name in Settings to chat."));
        return;
    }
    auto request = lobbyRequest();
    if(request.contentHash.empty()) { chatLabel.setText(_("Cannot check game content.")); return; }
    request.operation = AdmissionOperation::ChatEnter;
    request.displayName = name;
    chatAction = request.operation;
    chatPending = true;
    chatLabel.setText(_("Connecting to public chat..."));
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::sendLobbyChat() {
    if(chatSession.empty() || chatPending) return;
    const auto text = trimmed(chatInput.getText());
    if(text.empty()) return;
    if(text.size() > 120) { chatLabel.setText(_("Message is too long. Please shorten it.")); return; }
    auto request = lobbyRequest();
    request.operation = AdmissionOperation::ChatSay;
    request.chatSession = chatSession;
    request.chatText = text;
    chatAction = request.operation;
    chatPending = true;
    chat.begin(request);
    refreshControls();
}

void CrossplayMenu::updateLobbyChat() {
    chat.update();
    if(chatPending && chat.status() != RoomAdmissionClient::Status::InProgress) {
        chatPending = false;
        if(chat.status() == RoomAdmissionClient::Status::Succeeded) {
            const auto& response = chat.response();
            if(response.hasPresence) {
                waitingLabel.setText(_("Players waiting: ") + std::to_string(response.onlineCount));
                std::string names;
                for(const auto& name : response.waitingNames) { if(!names.empty()) names += ", "; names += name; }
                if(response.onlineCount > response.waitingNames.size()) names += " (+" + std::to_string(response.onlineCount-response.waitingNames.size()) + ")";
                waitingNames.setText(names.empty() ? _("No players waiting") : names);
            } else if(chatAction==AdmissionOperation::ChatPoll) {
                waitingLabel.setText(_("Players waiting"));
                waitingNames.setText(_("Online count unavailable"));
            }
            if(chatAction == AdmissionOperation::ChatEnter) {
                chatSession = response.chatSession;
                chatCursor = response.chatCursor;
            } else if(chatAction == AdmissionOperation::ChatSay) {
                chatInput.setText("");
            } else if(chatAction == AdmissionOperation::ChatPoll) {
                if(response.chatCursor < chatCursor) {
                    chatSession.clear();
                    chatLabel.setText(_("Reconnecting to public chat..."));
                } else {
                    if(response.chatGap) chatLines.push_back(_("Older lobby messages have expired."));
                    for(const auto& message : response.messages) {
                        if(message.id > chatCursor) chatLines.push_back(message.name + ": " + message.text);
                    }
                    chatCursor = response.chatCursor;
                    if(chatLines.size() > 60) chatLines.erase(chatLines.begin(), chatLines.end() - 60);
                    if(!response.messages.empty() || response.chatGap) {
                        std::string text;
                        for(const auto& line : chatLines) text += line + "\n";
                        chatHistory.setText(text);
                        chatHistory.scrollToEnd();
                    }
                }
            }
            if(!chatSession.empty()) chatLabel.setText(_("Chatting as ") + settings.general.playerName);
            nextChatPoll = SDL_GetTicks() + (chatAction == AdmissionOperation::ChatPoll ? 5000 : 0);
        } else {
            chatLabel.setText(chat.errorMessage());
            waitingLabel.setText(_("Players waiting"));
            waitingNames.setText(_("Unable to refresh online players"));
            if(chat.response().errorCode == "session_expired") chatSession.clear();
            nextChatPoll = SDL_GetTicks() + 15000;
        }
        chat.cancel();
        refreshControls();
    }
    if(!chatPending && !chatSession.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), nextChatPoll)) {
        auto request = lobbyRequest();
        request.operation = AdmissionOperation::ChatPoll;
        request.presence = true;
        request.chatSession = chatSession;
        request.chatCursor = chatCursor;
        chatAction = request.operation;
        chatPending = true;
        chat.begin(request);
        refreshControls();
    }
}

bool CrossplayMenu::validatePlayerName() {
    if(!RoomRelay::isAcceptableDisplayName(settings.general.playerName)) {
        openWindow(MsgBox::create(_("Please set your player name in Settings.")));
        return false;
    }
    return true;
}

void CrossplayMenu::onHostCustomGame() {
    if(!validatePlayerName()) return;
    playCustomGame(true, visibilityChoice.getSelectedIndex() == 1);
    refreshPublicGames();
}
void CrossplayMenu::onHostRandomSkirmish() {
    if(!validatePlayerName()) return;
    playCustomGame(true, visibilityChoice.getSelectedIndex() == 1, true);
    refreshPublicGames();
}
void CrossplayMenu::onHostCampaignCoop() {
    if(!validatePlayerName()) return;
    SinglePlayerMenu::playCampaign(true);
    refreshPublicGames();
}

void CrossplayMenu::onJoin() {
    std::string normalized;
    if(!RoomRelay::normalizeRoomCode(joinCodeTextBox.getText(), normalized)) {
        openWindow(MsgBox::create(_("That game code is not valid. Codes look like ABCD-EFGH-JKMN.")));
        return;
    }
    joinCodeTextBox.setText(normalized);
    beginAdmission(false);
}

void CrossplayMenu::onBack() {
    if(!joinTicket.empty()) {
        auto request=lobbyRequest(); request.requestTicket=joinTicket; RoomAdmissionClient::cancelJoinRequest(request); joinTicket.clear();
        admission.cancel(); stage=Stage::Choosing; joiningRunning=false; setStatus(_("Join request cancelled.")); refreshControls(); return;
    }
    teardownSession(std::string());
    quit();
}

void CrossplayMenu::beginAdmission(bool hosting, bool publicJoin) {
    if(!validatePlayerName()) return;
    if(settings.network.activeDirectEndpoint().empty()) {
        setStatus(_("Online play has not been set up in this copy of the game."));
        return;
    }

    if(!hosting && !publicJoin && (!codeInspected || inspectedCode != joinCodeTextBox.getText())) {
        auto lookup = lobbyRequest();
        lookup.operation = AdmissionOperation::Inspect;
        lookup.hosting = false;
        lookup.roomCode = joinCodeTextBox.getText();
        inspectingCode = true;
        codeInspected = false;
        stage = Stage::Requesting;
        setStatus(_("Checking this game's required mod version..."));
        admission.begin(lookup);
        refreshControls();
        return;
    }
    if(hosting && preparedGame) {
        try { WorkshopGameContent::pin(*preparedGame, true); }
        catch(const std::exception& error) { setStatus(error.what()); return; }
    }
    if(hosting && !OnlineModPolicy::approved()) {
        allowLateJoin = false;
        startImmediately = false;
    }

    fingerprintHash.clear(); // Recheck the selected mod rather than the discovery cache.
    // Fail closed. Going online without being able to describe our own content would ask the
    // game service to match us against a fingerprint we never computed, and would leave the
    // lobby with nothing to compare either.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online. Reinstalling the game usually fixes this."));
        return;
    }

    AdmissionRequest request;
    request.baseUrl = settings.network.activeDirectEndpoint();
    request.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
    request.appVersion = VERSIONSTRING;
    request.gameProtocol = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    request.contentHash = fingerprint;
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.hosting = hosting;
    request.publicRoom = visibilityChoice.getSelectedIndex() == 1;
    if(hosting) {
        // Co-op is a two-player arrangement; a custom game uses the lobby's own limit.
        request.mode = hostingCoop ? "coop" : "custom";
        request.modName = OnlineModPolicy::approved() ? ModManager::instance().getActiveModName()
            : Workshop::store().get(fingerprint).name;
        request.maxPeers = hostingCoop ? 2 : static_cast<std::uint8_t>(RoomRelay::Limits::kMaxPeersPerRoom);
        request.allowLateJoin = allowLateJoin;
        request.mapName = preparedGame ? preparedGame->getFilename() : "";
    } else {
        request.publicOnly = publicJoin;
        request.roomCode = publicJoin ? publicGames[publicGameList.getSelectedIndex()].roomCode
                                      : joinCodeTextBox.getText();
    }

    joiningRunning = !hosting && (publicJoin ? publicGames[publicGameList.getSelectedIndex()].running : joiningRunning);
    if(joiningRunning) { request.operation=AdmissionOperation::JoinRequest; request.displayName=settings.general.playerName; request.spectate=joiningAsSpectator; }
    pendingHosting = hosting;
    directory.cancel();
    directoryPending = false;
    stage = Stage::Requesting;
    setStatus(hosting ? _("Creating a game...") : _("Looking for that game..."));
    refreshControls();

    admission.begin(request);
}

void CrossplayMenu::openDirectSession() {
    fingerprintHash.clear();
    // The fingerprint is recomputed rather than remembered: admission and the handshake must
    // describe the same install, and anything that changed in between has to be caught here.
    const std::string fingerprint = contentFingerprint();
    if(fingerprint.empty()) {
        setStatus(_("This copy of the game could not check its own content files, "
                    "so it cannot play online."));
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    // Direct only. The address comes from this installation's settings, never from the admission
    // answer: a service that could hand out a gameplay endpoint could move the match back onto a
    // server, which is the whole thing this transport exists to stop. grantedRoom.socketUrl is
    // deliberately ignored.
    DirectRoomTransport::Config config;
    config.signalingBaseUrl = settings.network.activeDirectEndpoint();
    config.grant       = grantedRoom.grant;
    config.roomCode    = grantedRoom.roomCode;
    config.displayName = settings.general.playerName;
    config.appVersion  = VERSIONSTRING;
    config.contentHash = fingerprint;
    config.gameProtocolVersion = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    config.allowLateJoin = pendingHosting && allowLateJoin;
    config.allowLoopbackPlaintext = settings.network.relayUseDevelopmentEndpoint;
#ifdef __EMSCRIPTEN__
    config.runtime = "browser";
#else
    config.runtime = "native";
#endif

    try {
        pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
    } catch(const std::exception& error) {
        setStatus(error.what());
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    std::string failure;
    if(!pNetworkManager->startDirectSession(config, failure)) {
        pNetworkManager.reset();
        setStatus(failure);
        stage = Stage::Finished;
        refreshControls();
        return;
    }

    pNetworkManager->setOnReceiveGameInfo(
        std::bind(&CrossplayMenu::onReceiveGameInfo, this,
                  std::placeholders::_1, std::placeholders::_2));
    pNetworkManager->setOnPeerDisconnected(
        std::bind(&CrossplayMenu::onPeerDisconnected, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    if(joiningRunning) pNetworkManager->expectLateJoin();
    roomCode = grantedRoom.roomCode;
    SDL_Log("Online lobby: admission granted (%s); opening direct session", pendingHosting ? "host" : "guest");
    publicRoom = grantedRoom.visibility == "public";
    pNetworkManager->setPublicRelayRoom(publicRoom);
    stage = Stage::Connecting;
    setStatus(_("Connecting..."));
    refreshControls();
}

void CrossplayMenu::teardownSession(std::string reason) {
    joinProgress.setVisible(false); statusLabel.setVisible(true);
    joiningRunning=false;
    // Own the reason before releasing the relay whose status may contain it.
    pendingGameInfo.reset();
    pendingLobbyChanges = ChangeEventList();
    pendingDisconnectReason.clear();
    admission.cancel();
    visibilityUpdate.cancel();
    visibilityPending = false;
    if(pNetworkManager != nullptr && pNetworkManager->isRoomSession()) {
        pNetworkManager->setOnReceiveGameInfo(
            std::function<void (const GameInitSettings&, const ChangeEventList&)>());
        pNetworkManager->setOnPeerDisconnected(
            std::function<void (const std::string&, bool, int)>());
        pNetworkManager->disconnect();
        pNetworkManager.reset();
    }

    roomCode.clear();
    grantedRoom = AdmissionResponse();
    stage = Stage::Choosing;
    if(!reason.empty()) setStatus(reason);
    hostCustomGameButton.setText(_("Create Custom Game"));
    hostRandomSkirmishButton.setText(_("Create Random Skirmish"));
    hostCoopButton.setText(_("Create Campaign Co-op"));
    refreshControls();
}

void CrossplayMenu::update() {
    if(autoHostRequested) { autoHostRequested = false; beginAdmission(true); }
    if(preparedGame && stage == Stage::HostReady) {
        int result;
        { CustomGamePlayers lobby(*preparedGame, true, false, nullptr, &preparedPlayers, startImmediately); result = lobby.showMenu(); }
        teardownSession({});
        quit(result);
        return;
    }
    if(!preparedGame && (stage == Stage::Choosing || stage == Stage::WaitingForApproval)) {
        if(!chatPending && chatSession.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), nextChatPoll))
            enterLobbyChat();
        updateLobbyChat();
    }
    visibilityUpdate.update();
    if(visibilityPending && visibilityUpdate.status() != RoomAdmissionClient::Status::InProgress) {
        visibilityPending = false;
        if(visibilityUpdate.status() == RoomAdmissionClient::Status::Succeeded) {
            publicRoom = visibilityUpdate.response().visibility == "public";
            if(!visibilityUpdate.response().roomCode.empty()) {
                roomCode = visibilityUpdate.response().roomCode;
                grantedRoom.roomCode = roomCode;
                if(pNetworkManager && pNetworkManager->getRelayClient())
                    pNetworkManager->getRelayClient()->updateInvitationCode(roomCode);
            }
            if(pNetworkManager) pNetworkManager->setPublicRelayRoom(publicRoom);
            setStatus(publicRoom ? _("Your public game is listed. Players can join from the lobby.")
                                 : _("Your private game is unlisted. Share its invitation code."));
        } else {
            setStatus(_("Visibility could not be confirmed. Choose it again to confirm your game and invitation code."));
        }
        visibilityChoice.setSelectedItem(publicRoom ? 1 : 0);
        visibilityUpdate.cancel();
        refreshControls();
    }
    directory.update();
    if(directoryPending && (directory.status() == RoomAdmissionClient::Status::Succeeded
                            || directory.status() == RoomAdmissionClient::Status::Failed)) {
        nextDirectoryPage = 0;
        directoryPending = false;
        if(directory.status() == RoomAdmissionClient::Status::Succeeded) {
            allPublicGames = directory.response().games;
            nextDirectoryPage = directory.response().nextPage;
            refreshDirectory();
        } else {
            allPublicGames.clear();
            refreshDirectory();
            directoryLabel.setText(_("Public list unavailable"));
            setStatus(directory.errorMessage());
        }
        directory.cancel();
        refreshControls();
    }
    if(!preparedGame && (stage == Stage::Choosing || stage == Stage::HostReady || stage == Stage::ClientWaiting)
       && !directoryPending
       && SDL_TICKS_PASSED(SDL_GetTicks(), nextDirectoryRefresh)) refreshPublicGames();
    // Network callbacks run inside NetworkManager::update(). Defer menu loops and
    // manager destruction until that dispatch has returned to MenuBase.
    if(!pendingDisconnectReason.empty()) {
        teardownSession(pendingDisconnectReason);
        return;
    }
    if(pendingGameInfo) {
        auto gameInfo = std::move(pendingGameInfo);
        auto changes = std::move(pendingLobbyChanges);
        enterReceivedLobby(*gameInfo, changes);
        return;
    }
    admission.update();

    if(stage == Stage::Requesting) {
        switch(admission.status()) {
            case RoomAdmissionClient::Status::Succeeded:
                if(inspectingCode) {
                    const auto inspected = admission.response();
                    admission.cancel();
                    inspectingCode = false;
                    stage = Stage::Choosing;
                    if(!activateGameContent(inspected.contentHash, inspected.running)) {
                        refreshControls(); return;
                    }
                    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
                    inspectedCode = joinCodeTextBox.getText();
                    codeInspected = true;
                    joiningRunning = inspected.running;
                    joiningAsSpectator = inspected.running;
                    beginAdmission(false, false);
                    return;
                }
                if(joiningRunning) {
                    joinTicket=admission.response().requestTicket;
                    admission.cancel(); stage=Stage::WaitingForApproval; nextJoinPoll=0; joinPollPending=false; joinRequestDeadline=SDL_GetTicks()+180000;
                    setStatus(joiningAsSpectator ? _("Connecting as a spectator...") : _("Waiting for a player slot. If rejected, you will spectate."));
                    refreshControls(); break;
                }
                grantedRoom = admission.response();
                admission.cancel();
                openDirectSession();
                break;
            case RoomAdmissionClient::Status::Failed:
                inspectingCode = false; codeInspected = false;
                setStatus(admission.errorMessage());
                // Compatibility failures need an acknowledged prompt, including replies from
                // older services that group version and content mismatches together.
                if(admission.response().errorCode == "version_mismatch"
                   || admission.response().errorCode == "unsupported_version"
                   || admission.response().errorCode == "content_mismatch") {
                    std::istringstream words(admission.errorMessage());
                    std::string wrapped, line, word;
                    const unsigned maxWidth=static_cast<unsigned>(std::max(120,std::min(540,getRendererWidth()-60)));
                    while(words >> word) {
                        const auto next=line.empty() ? word : line+" "+word;
                        if(!line.empty() && GUIStyle::getInstance().getTextWidth(next,16)>maxWidth) {
                            wrapped+=line+"\n"; line=word;
                        } else line=next;
                    }
                    openWindow(MsgBox::create(wrapped+line));
                }
                admission.cancel();
                stage = Stage::Choosing;
                refreshControls();
                break;
            default:
                break;
        }
        return;
    }

    if(stage==Stage::WaitingForApproval) {
        if(joinPollPending && admission.status()==RoomAdmissionClient::Status::Succeeded && !admission.response().requestState.empty()) {
            joinPollPending=false;
            const auto answer=admission.response();
            admission.cancel();
            if(answer.requestState=="approved") { grantedRoom=answer; joinTicket.clear(); openDirectSession(); }
            else if(answer.requestState!="pending") { joinTicket.clear(); stage=Stage::Choosing; setStatus(_("The join request ended: ")+answer.requestState); refreshControls(); }
        }
        if(joinPollPending && admission.status()==RoomAdmissionClient::Status::Failed) {
            joinPollPending=false; setStatus(admission.errorMessage());
            if(admission.response().errorCode=="request_expired" || admission.response().errorCode=="room_not_found") { joinTicket.clear(); stage=Stage::Choosing; refreshControls(); }
        }
        if(stage==Stage::WaitingForApproval && SDL_TICKS_PASSED(SDL_GetTicks(),joinRequestDeadline)) { onBack(); setStatus(_("No response from the host. You can request again.")); return; }
        if(stage==Stage::WaitingForApproval && SDL_TICKS_PASSED(SDL_GetTicks(),nextJoinPoll) && admission.status()!=RoomAdmissionClient::Status::InProgress) {
            auto request=lobbyRequest(); request.operation=AdmissionOperation::JoinStatus; request.requestTicket=joinTicket;
            joinPollPending=true; admission.begin(request); nextJoinPoll=SDL_GetTicks()+3000;
        }
        return;
    }
    if(pNetworkManager == nullptr || !pNetworkManager->isRoomSession()) {
        return;
    }

    if(joiningRunning) {
        if(auto resumed=pNetworkManager->takeLateJoin()) {
            pNetworkManager->setOnReceiveGameInfo({}); pNetworkManager->setOnPeerDisconnected({});
            try { startMultiPlayerGame(*resumed); }
            catch(const std::exception& error) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Online checkpoint load failed: %s",error.what());
                if(pNetworkManager) pNetworkManager->failObserver("The downloaded game could not be loaded.\nPlease try joining again.");
            }
            const auto failure=pNetworkManager ? pNetworkManager->joinFailure() : std::string();
            teardownSession(failure);
            if(!failure.empty()) { openWindow(MsgBox::create(failure)); refreshPublicGames(); }
            else quit(MENU_QUIT_GAME_FINISHED);
            return;
        }
        if(pNetworkManager->lateJoinPaused()) {
            statusLabel.setVisible(false); joinProgress.setVisible(true);
            joinProgress.setProgress(pNetworkManager->lateJoinPercent());
            joinProgress.setText(pNetworkManager->lateJoinProgressText());
        }
    }
    RoomSessionTransport* relay = pNetworkManager->getRelayClient();
    if(relay == nullptr) {
        return;
    }

    if(stage == Stage::Connecting && relay->isJoined()) {
        roomCode = relay->roomCode();
        if(pendingHosting) {
            stage = Stage::HostReady;
            setStatus(startImmediately ? _("Starting campaign...") : publicRoom
                ? _("Your public game is listed. Opening the lobby...")
                : _("Your private game is open. Opening the lobby..."));
        } else {
            stage = Stage::ClientWaiting;
            setStatus(joiningAsSpectator ? _("Connecting directly to the host to spectate...")
                                        : _("Connecting to the host and receiving game setup..."));
        }
        refreshControls();
        if(!preparedGame) refreshPublicGames();
        return;
    }

    if(relay->status() == RoomSessionTransport::Status::Closed
       && (stage == Stage::Connecting || stage == Stage::HostReady
           || stage == Stage::ClientWaiting)) {
        teardownSession(relay->statusMessage().empty()
            ? std::string(_("The connection to the game was lost."))
            : relay->statusMessage());
    }
}

void CrossplayMenu::onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                                      const ChangeEventList& changeEventList) {
    if(joiningRunning || pendingHosting || pendingGameInfo || !pendingDisconnectReason.empty()) {
        return;     // running joins use the checkpoint, never the original waiting lobby
    }

    pendingGameInfo = std::make_unique<GameInitSettings>(gameInitSettings);
    pendingLobbyChanges = changeEventList;
    SDL_Log("Online lobby: received host setup; queued lobby transition at %u ms", SDL_GetTicks());
}

void CrossplayMenu::enterReceivedLobby(const GameInitSettings& gameInitSettings,
                                       const ChangeEventList& changeEventList) {

    setStatus(_("Joining the game..."));

    auto verifiedSettings = gameInitSettings;
    try { WorkshopGameContent::resolveMod(verifiedSettings); }
    catch(const std::exception& error) { teardownSession(error.what()); return; }
    auto pCustomGamePlayers = std::make_unique<CustomGamePlayers>(verifiedSettings, false);
    pCustomGamePlayers->onReceiveChangeEventList(changeEventList);
    SDL_Log("Online lobby: showing guest roster at %u ms", SDL_GetTicks());
    const int result = pCustomGamePlayers->showMenu();
    pCustomGamePlayers.reset();

    switch(result) {
        case MENU_QUIT_DEFAULT:
            teardownSession(_("You left the game."));
            break;
        case MENU_QUIT_GAME_FINISHED:
            quit(MENU_QUIT_GAME_FINISHED);
            break;
        default:
            teardownSession(_("The connection to the game was lost."));
            break;
    }
}

void CrossplayMenu::onPeerDisconnected(const std::string& playerName, bool isHost, int cause) {
    if(!isHost) {
        return;
    }

    if(pNetworkManager && pNetworkManager->getRelayClient()) {
        const auto* relay = pNetworkManager->getRelayClient();
        if(relay->status() == RoomSessionTransport::Status::Closed && !relay->statusMessage().empty()) {
            pendingDisconnectReason = relay->statusMessage();
            return;
        }
    }
    std::string message;
    switch(cause) {
        case NETWORKDISCONNECT_TIMEOUT:
            message = _("The connection stopped responding.");
            break;
        case NETWORKDISCONNECT_GAME_FULL:
            message = _("There is no free player slot in this game left!");
            break;
        case NETWORKDISCONNECT_PROTOCOL_MISMATCH:
            message = _("That game was created by a different version of Dune City.");
            break;
        default:
            message = playerName.empty() ? std::string(_("The online game ended."))
                                         : (playerName + _(" left the game."));
            break;
    }
    // A host leaving arrives twice: once as that peer departing, once as the room closing. The
    // first carries the name and is the more useful sentence, so it is the one that is kept.
    if(pendingDisconnectReason.empty()) {
        pendingDisconnectReason = std::move(message);
    }
}
