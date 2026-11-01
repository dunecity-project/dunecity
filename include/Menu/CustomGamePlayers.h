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

#ifndef CUSTOMGAMEPLAYERS_H
#define CUSTOMGAMEPLAYERS_H

#include <GameInitSettings.h>
#include <Menu/PlaySetup.h>
#include <Menu/LobbyAuthorization.h>

#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/HBox.h>
#include <GUI/Label.h>
#include <GUI/DropDownBox.h>
#include <GUI/TextButton.h>
#include <GUI/ListBox.h>
#include <GUI/PictureLabel.h>
#include <GUI/Checkbox.h>
#include <GUI/TextView.h>
#include <GUI/TextBox.h>
#include <GUI/ScrollView.h>
#include <GUI/dune/AIPlayerSelector.h>

#include <Network/ChangeEventList.h>

#include <DataTypes.h>

#include <string>
#include <list>
#include <set>

#include "MenuBase.h"

#define MENU_QUIT_GAME_FINISHED     (-2)

class INIFile;

class CustomGamePlayers : public MenuBase
{
public:
    CustomGamePlayers(const GameInitSettings& newGameInitSettings, bool server = true, bool LANServer = true, CustomPlaySetup* setup = nullptr, const ChangeEventList* initialPlayers = nullptr, bool startImmediately = false);
    virtual ~CustomGamePlayers();
    int showMenu() override;

    /**
        Applies a change event list. On the host the list is a *request* from senderName and is
        authorized against the seat that player holds before anything is applied; on a client it
        is the host's authoritative view. An empty senderName means "local/host origin".
        \param  senderName          bound peer name of the sender, empty for host-originated
        \param  changeEventList     the requested or authoritative changes
    */
    void onReceiveChangeEventList(const std::string& senderName, const ChangeEventList& changeEventList);

    /// Applies a host-originated change event list (no remote sender to authorize).
    void onReceiveChangeEventList(const ChangeEventList& changeEventList) {
        onReceiveChangeEventList(std::string(), changeEventList);
    }

    /// Builds the current seat occupancy, used to authorize client lobby requests.
    LobbyAuthorization::SeatSnapshot makeSeatSnapshot() const;

    ChangeEventList getChangeEventListForNewPlayer(const std::string& newPlayerName);

    void update() override;

private:
    ChangeEventList getChangeEventList();
    void onChildWindowClose(Window* child) override;
    void rebuildSetup(bool keepPlayers);
    CustomPlaySetup* setup = nullptr;
    bool restoringSetup = false;
    bool startImmediately = false;
    bool automaticStartPending = false;
    bool installerApprovedForDisplay = false;
    StaticContainer launchWidget;
    Label launchTitle;
    TextView launchStatus;
    TextButton cancelLaunch;
    HBox setupMapRow, setupModeRow;
    DropDownBox setupMap, setupMod, setupConnection, setupVisibility;
    Checkbox setupShared, setupAISupport, setupSpectate;
    TextButton setupRules, setupBrowseMaps;
    /// True when this offline setup is watching an all-AI match (CustomPlaySetup::spectate).
    bool spectating = false;
    Label readinessLabel;

    void onReceiveChatMessage(const std::string& name, const std::string& message);
    void onConfigMismatch(const std::string& errorMessage);
    void onReceiveModInfo(const std::string& modName, const std::string& modChecksum);
    void onModDownloadComplete(bool success, const std::string& data);
    void onReceiveModAck(const std::string& playerName, bool success, const std::string& modChecksum);
    void checkAllClientsReady();
    void updateDiscordGameStarting();
    void updateDiscordLobbyPresence();
#ifdef __EMSCRIPTEN__
    /// Browser: refresh the matched opponent's connection state label.
    void updateOpponentLabel();
#endif
    void onPeerDisconnected(const std::string& playername, bool bHost, int cause);

    void extractMapInfo(INIFile* pMap);

    void setPlayer2Slot(const std::string& playername, int slot);

    /**
        The player in a seat (slot = 2*house + controller), as the change events carry it:
        PLAYER_HUMAN, PLAYER_OPEN, PLAYER_CLOSED, the factory index of an AI class, or
        -(index+2) for a loaded game's AI.
    */
    int getSeatPlayer(int slot) const;
    /// What the seat shows: the seated human's name, the AI's name, "open" or "closed".
    std::string getSeatName(int slot) const;
    /// The other seat of the local player's house (AI support sits there), or -1.
    int localPartnerSlot() const;
    /// Shows only this entry in the seat: a seated human or a loaded game's fixed player.
    void setSeatFixed(int slot, const std::string& text, int player);
    /// Offers the seat's open / closed / AI choice and selects player in it.
    void setSeatChoice(int slot, int player);
    /// Selects player in the seat's current entries; false if the seat does not offer it.
    bool selectSeatPlayer(int slot, int player);
    void setSeatEnabled(int slot, bool enabled);
    void setSeatOnClickEnabled(int slot, bool enabled);
    /// Shows Difficulty and Role only beside a seat that holds an AI, and the line they share.
    void updateSeatAIControls();

    void onChangeHousesDropDownBoxes(bool bInteractive, int houseInfoNum = -1);
    void onChangeTeamDropDownBoxes(bool bInteractive, int houseInfoNum = -1);
    void onChangeColorDropDownBoxes(bool bInteractive, int houseInfoNum = -1);
    void onChangeGraphicsSkinDropDownBoxes(bool bInteractive, int houseInfoNum = -1);
    void onChangeSpiceIncomeDropDownBoxes(bool bInteractive, int houseInfoNum = -1);
    void onBonusColorCheckbox(int houseInfoNum);
    void onChangePlayerDropDownBoxes(bool bInteractive, int boxnum);
    void onClickPlayerDropDownBox(int boxnum);
    void onStartGame(unsigned int timeLeft);
    void addToHouseDropDown(DropDownBox& houseDropDownBox, int house, bool bSelect = false);
    void removeFromHouseDropDown(DropDownBox& houseDropDownBox, int house);

    bool isBoundedHouseOnMap(HOUSETYPE houseID);

    void checkPlayerBoxes();
    void addAllPlayersToGameInitSettings();
    bool addPlayerToHouseInfo(GameInitSettings::HouseInfo& newHouseInfo, int player, const std::string& playername);
    void initializeSeparateCoopLobby();
    void updateSeparateCoopControls();
    void assembleSeparateCoopSettings();

    void onNext();
    void onCancel();

    void onSendChatMessage();
    void addInfoMessage(const std::string& message);
    void addChatMessage(const std::string& name, const std::string& message);

    void disableAllDropDownBoxes();

    GameInitSettings::HouseInfoList fixedCoopHouses;
    std::optional<coop::CoopCampaignSession> separateCoopSession;
    SettingsClass::GameOptionsClass separateCoopOptions;
    bool separateCoopSourcePending = false;
    bool separateCoopSetupCommitted = false;
    bool separateCoopSetupTransferPending = false;
    std::array<int, MAX_CUSTOM_GAME_PLAYERS> lastCoopFactionSelections{};
    GameInitSettings                gameInitSettings;
    GameInitSettings::HouseInfoList houseInfoListSetup;     ///< only used if we are loading a savegame

    StaticContainer windowWidget;
    VBox            mainVBox;

    Label           captionLabel;
    HBox            captionHBox;
    TextButton      copyCodeButton;

    HBox            mainHBox;

    // left VBox with player list and map options
    VBox            leftVBox;
    HBox            playerListHBox;
    ScrollView      playerListScroll;
    VBox            playerListVBox;

    // right VBox with mini map
    VBox            rightVBox;
    PictureLabel    minimap;
    HBox            mapPropertiesHBox;
    VBox            mapPropertyNamesVBox;
    VBox            mapPropertyValuesVBox;
    Label           mapPropertySize;
    Label           mapPropertyPlayers;
    Label           mapPropertyAuthors;
    Label           mapPropertyLicense;
    Label           mapPropertyMod;
#ifdef __EMSCRIPTEN__
    Label           opponentLabel;   ///< Browser: the matched opponent's connection state.
#endif
    Label           mapPropertyCity;

    // bottom row of buttons
    HBox            buttonHBox;
    VBox            backButtonVBox;
    TextButton      backButton;
    VBox            chatVBox;
    TextView        chatTextView;
    TextBox         chatTextBox;
    VBox            nextButtonVBox;
    TextButton      nextButton;

    /**
        A house row line that takes no height while hidden: a shared row only grows its
        Difficulty + Role line while one of its seats holds an AI.
    */
    class CollapsibleHBox : public HBox {
    public:
        Point getMinimumSize() const override {
            const Point size = HBox::getMinimumSize();
            return isVisible() ? size : Point(size.x, 0);
        }
    };

    /**
        One controller seat. Its box shows either one fixed entry (a seated human, or a loaded
        game's player) or the AI choice: the selector's AI box with the open and closed seat
        states, beside its Difficulty and Role boxes.
    */
    class Seat {
    public:
        StaticContainer     box;            ///< fixedBox and selector.aiBox in the same place
        DropDownBox         fixedBox;
        AIPlayerSelector    selector;
        bool                fixed = true;
        bool                enabled = true;     ///< whether the seat may be edited; only its shown box is enabled
    };

    /// Enables the box the seat shows if the seat is enabled, and always disables the hidden one.
    void applySeatEnabled(Seat& seat);

    class HouseInfo {
    public:
        VBox            houseInfoVBox;
        HBox            houseHBox;
        Label           houseLabel;
        DropDownBox     houseDropDown;
        DropDownBox     teamDropDown;
        Checkbox        bonusColorCheckbox;
        DropDownBox     colorDropDown;
        Label           graphicsSkinLabel;
        DropDownBox     graphicsSkinDropDown;
        Label           spiceIncomeLabel;
        DropDownBox     spiceIncomeDropDown;
        HBox            playerHBox;
        CollapsibleHBox aiHBox;             ///< shared rows too narrow for one line: Difficulty + Role
        PictureLabel    player1ArrowLabel;
        Label           player1Label;
        Label           player2Label;
        Seat            seats[2];
    };

    LobbyAuthorization::SeatSnapshot editableSeats;
    std::array<int, MAX_CUSTOM_GAME_PLAYERS * 2> lastPlayerSelections{};
    bool                    bServer;
    bool                    bLANServer;
    bool                    duneCitySkinControls;
    HouseInfo               houseInfo[MAX_CUSTOM_GAME_PLAYERS];
    int                     numHouses;
    std::list<HOUSETYPE>    boundHousesOnMap;
    Uint32                  startGameTime;
    bool                    bConfigMismatchDetected;
    std::string             hostModName;                ///< The mod name sent by the host
    std::string             hostModChecksum;            ///< The mod checksum sent by the host
    bool waitingForModInstall = false, rebuildAfterModTransfer = false;
    bool communityModDownloadAttempted = false, communityModDownloadInProgress = false;
    ChangeEventList delayedModChanges;
    bool                    bModDownloadInProgress;     ///< Whether mod download is in progress
    std::set<std::string>   clientsAckedMod;            ///< Clients that have acknowledged mod sync (host only)
    bool                    bWaitingForModAcks;         ///< Whether host is waiting for mod ACKs
    int                     brainEqHumanSlot;           ///< If we have an old map with Brain=Human and Brain=CPU, store index of Brain=Human here
    int                     slotToTeam[MAX_CUSTOM_GAME_PLAYERS];     ///< Maps the slot number to a team number (both zero-based indices)
};

#endif //CUSTOMGAMEPLAYERS_H
