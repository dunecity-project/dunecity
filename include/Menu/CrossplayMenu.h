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

#ifndef CROSSPLAYMENU_H
#define CROSSPLAYMENU_H

/**
    Hosting and joining a game that desktop and browser players can share.

    Public games are discovered in the lobby; private games use invitation codes.
    There is no address to enter and no router to configure, because both sides only ever make
    one outbound connection to the same game service.
*/

#include <GUI/Checkbox.h>
#include <GUI/DropDownBox.h>
#include <GUI/HBox.h>
#include <GUI/Label.h>
#include <GUI/ProgressBar.h>
#include <GUI/ListBox.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextBox.h>
#include <GUI/TextView.h>
#include <GUI/TextButton.h>
#include <GUI/Spacer.h>
#include <GUI/VBox.h>

#include <Network/ChangeEventList.h>
#include <Network/RoomAdmissionClient.h>

#include <GameInitSettings.h>
#include <mod/ModInfo.h>

#include <memory>
#include <string>

#include "MenuBase.h"

class CrossplayMenu : public MenuBase {
public:
    CrossplayMenu();
    CrossplayMenu(const GameInitSettings& game, bool publicGame, const ChangeEventList& players = {}, bool allowLateJoin = true, bool startImmediately = false);
    ~CrossplayMenu() override;

    void update() override;

private:
    enum class Stage {
        Choosing,       ///< nothing in flight
        WaitingForApproval,
        Requesting,     ///< waiting for the game service to answer
        Connecting,     ///< opening the game connection
        HostReady,      ///< in the room as host; may now choose what to play
        ClientWaiting,  ///< in the room as a guest; waiting for the host's lobby
        Finished        ///< the session ended; the reason is on screen
    };

    void refreshDirectory();
    void layoutControls();
    void onHostCustomGame();
    void onHostRandomSkirmish();
    void onHostCampaignCoop();
    void onJoin();
    void onBack();
    void refreshPublicGames(unsigned offset = 0);
    void joinPublicGame();

    void beginAdmission(bool hosting, bool publicJoin = false);
    AdmissionRequest lobbyRequest() const;
    void enterLobbyChat();
    void sendLobbyChat();
    void updateLobbyChat();
    void changeVisibility();
    /// Starts the direct session once admission has produced a room and a grant.
    void openDirectSession();
    void teardownSession(std::string reason);
    void enterReceivedLobby(const GameInitSettings& gameInitSettings,
                            const ChangeEventList& changeEventList);
    void setStatus(const std::string& message);
    void refreshControls();

    bool validatePlayerName();
    bool activateGameContent(const std::string& fingerprint, bool running);

    void onReceiveGameInfo(const GameInitSettings& gameInitSettings,
                           const ChangeEventList& changeEventList);
    void onPeerDisconnected(const std::string& playerName, bool isHost, int cause);

    /// Fingerprint of the bundled content, as the relay and the lobby both understand it.
    std::string contentFingerprint() const;
    mutable std::string fingerprintMod, fingerprintHash;

    std::unique_ptr<GameInitSettings> preparedGame;
    ChangeEventList preparedPlayers;
    bool allowLateJoin = true, joiningRunning = false, joiningAsSpectator = false;
    std::string joinTicket;
    bool joinPollPending = false;
    Uint32 nextJoinPoll = 0, joinRequestDeadline = 0;
    bool autoHostRequested = false;
    bool startImmediately = false;
    DropDownBox modeFilter, modFilter;
    std::vector<ModInfo> availableMods;
    TextButton otherConnections;
    TextView preparedSummary;
    TextView selectedGameDetails;
    std::vector<PublicRelayGame> allPublicGames;
    Stage       stage = Stage::Choosing;
    bool        hostingCoop = false;
    bool        pendingHosting = false;
    bool inspectingCode = false, codeInspected = false;
    std::string inspectedCode;
    std::string roomCode;
    std::string statusText;
    std::unique_ptr<GameInitSettings> pendingGameInfo;
    ChangeEventList pendingLobbyChanges;
    std::string pendingDisconnectReason;

    RoomAdmissionClient admission;
    RoomAdmissionClient directory;
    RoomAdmissionClient chat;
    RoomAdmissionClient visibilityUpdate;
    AdmissionOperation chatAction = AdmissionOperation::Room;
    std::string chatSession;
    std::string chatContentHash;
    std::uint64_t chatCursor = 0;
    Uint32 nextChatPoll = 0;
    bool chatPending = false;
    bool visibilityPending = false;
    bool publicRoom = true;
    bool showPrivateJoin = false;
    std::vector<std::string> chatLines;
    bool directoryPending = false;
    unsigned nextDirectoryPage = 0;
    Uint32 nextDirectoryRefresh = 0;
    std::vector<PublicRelayGame> publicGames;
    AdmissionResponse   grantedRoom;

    StaticContainer windowWidget;
    VBox            mainVBox;
    Label           captionLabel;

    HBox            playerNameHBox;
    Label           playerNameLabel;
    Label           playerNameValue;
    Label           chatTitle;
    TextButton      privateInviteButton;
    Label           chatLabel;
    TextView        chatHistory;
    Label           waitingLabel;
    TextView        waitingNames;
    HBox            chatInputHBox;
    TextBox         chatInput;
    TextButton      chatSendButton;
    HBox            visibilityHBox;
    Label           visibilityLabel;
    DropDownBox     visibilityChoice;
    HBox            directoryHBox;
    Label           directoryLabel;
    TextButton      refreshGamesButton;
    TextButton      moreGamesButton;
    HBox            joinPublicHBox;
    TextButton      joinPublicButton;
    ListBox         publicGameList;

    Label           statusLabel;
    TextProgressBar joinProgress;
    Label           roomCodeLabel;
    HBox            roomCodeHBox;
    TextButton      copyCodeButton;

    HBox            hostHBox;
    TextButton      hostCustomGameButton;
    TextButton      hostRandomSkirmishButton;
    TextButton      hostCoopButton;

    HBox            joinHBox;
    Label           joinLabel;
    TextBox         joinCodeTextBox;
    TextButton      joinButton;

    HBox            buttonHBox;
    TextButton      backButton;
};

#endif // CROSSPLAYMENU_H
