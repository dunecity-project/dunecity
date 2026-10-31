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

#include <Menu/SinglePlayerMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <FileClasses/FileManager.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <misc/fnkdat.h>
#include <misc/MenuLayout.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>

#include <Menu/CustomGameMenu.h>
#include <Menu/SinglePlayerSkirmishMenu.h>
#include <Menu/HouseChoiceMenu.h>
#include <Network/OnlineModPolicy.h>

#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/MsgBox.h>

#include <Game.h>
#include <GameInitSettings.h>
#include <sand.h>

SinglePlayerMenu::SinglePlayerMenu() : MenuBase() {
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    campaignButton.setText(_("CAMPAIGN"));
    campaignButton.setOnClick(std::bind(&SinglePlayerMenu::onCampaign, this));
    campaignButton.setActive();
    customButton.setText(_("CUSTOM GAME"));
    customButton.setOnClick(std::bind(&SinglePlayerMenu::onCustom, this));
    skirmishButton.setText(_("SKIRMISH"));
    skirmishButton.setOnClick(std::bind(&SinglePlayerMenu::onSkirmish, this));
    loadSavegameButton.setText(_("LOAD GAME"));
    loadSavegameButton.setOnClick(std::bind(&SinglePlayerMenu::onLoadSavegame, this));
    loadReplayButton.setText(_("LOAD REPLAY"));
    loadReplayButton.setOnClick(std::bind(&SinglePlayerMenu::onLoadReplay, this));
    cancelButton.setText(_("BACK"));
    cancelButton.setOnClick(std::bind(&SinglePlayerMenu::onCancel, this));
    SDL_Texture* pPlanet = pGFXManager->getUIGraphic(UI_PlanetBackground);
    SDL_Texture* pLogo = pGFXManager->getUIGraphic(UI_DuneLegacy);
    planetPicture.setTexture(pPlanet);
    logoPicture.setTexture(pLogo);
    if(validatedStartMenuMode(settings.video.startMenuMode) == 1) {
        const StartMenuLayout layout{getSize().x, getSize().y, 6};
        planetPicture.setFitToSize(true);
        windowWidget.addWidget(&planetPicture, layout.planetBounds());
        logoPicture.setFitToSize(true);
        windowWidget.addWidget(&logoPicture, layout.logoBounds());

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        buttonBorder.setStretchToSize(true);
        windowWidget.addWidget(&buttonBorder, layout.borderBounds());

        TextButton* buttons[] = {&campaignButton, &customButton, &skirmishButton,
                                 &loadSavegameButton, &loadReplayButton, &cancelButton};
        for(int i = 0; i < 6; ++i) windowWidget.addWidget(buttons[i], layout.button(i));
    } else {
        SDL_Rect planetBounds = calcAlignedDrawingRect(pPlanet);
        planetBounds.y = planetBounds.y - getHeight(pPlanet) / 2 + 10;
        windowWidget.addWidget(&planetPicture, planetBounds);

        SDL_Rect logoBounds = calcAlignedDrawingRect(pLogo);
        logoBounds.y = logoBounds.y + getHeight(pLogo) / 2 + 28;
        windowWidget.addWidget(&logoPicture, logoBounds);

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        SDL_Rect borderBounds = calcAlignedDrawingRect(pBorder);
        borderBounds.y = borderBounds.y + getHeight(pBorder) / 2 + 59;
        windowWidget.addWidget(&buttonBorder, borderBounds);

        windowWidget.addWidget(&menuButtonsVBox,
            Point((getSize().x - 160) / 2, getSize().y / 2 + 64), Point(160, 111));
        TextButton* buttons[] = {&campaignButton, &customButton, &skirmishButton,
                                 &loadSavegameButton, &loadReplayButton, &cancelButton};
        for(int i = 0; i < 6; ++i) {
            menuButtonsVBox.addWidget(buttons[i]);
            if(i != 5) menuButtonsVBox.addWidget(VSpacer::create(3));
        }
    }
}

SinglePlayerMenu::~SinglePlayerMenu() = default;

void SinglePlayerMenu::onCampaign() {
    playCampaign();
}

void SinglePlayerMenu::playCampaign(bool showLobby) {
  // Hosting from the online lobby is always online; otherwise start from the last choice.
  bool online = showLobby || settings.general.campaignOnline;
  bool keepRules = false;
  for(;;) {
    int player = HouseChoiceMenu(online, keepRules, showLobby).showMenu();
    keepRules = true;

    if(player < 0) {
        return;
    }

    // Get AI settings from HouseChoiceMenu static storage
    const std::string& partnerClass = HouseChoiceMenu::getPartnerClass();
    const std::string& enemyAIClass = HouseChoiceMenu::getEnemyClass();
    SettingsClass::GameOptionsClass gameOptions = HouseChoiceMenu::getGameOptions();

    const auto* partner = PlayerFactory::getByPlayerClass(partnerClass);
    const bool supportSelected = (partner != nullptr) && (!HouseChoiceMenu::isOnline() || (!showLobby && OnlineModPolicy::approved()));

    GameInitSettings init = HouseChoiceMenu::isSingleMission()
        ? GameInitSettings((HOUSETYPE) player, HouseChoiceMenu::getStartLevel(), gameOptions)
        : GameInitSettings((HOUSETYPE) player, gameOptions, HouseChoiceMenu::getStartLevel());
    const auto campaignSkin = GameInitSettings::sanitizeGraphicsSkin(HouseChoiceMenu::getCampaignSkin());
    init.setCampaignGraphicsSkin(campaignSkin);
    if(supportSelected) {
        init.setMultiplePlayersPerHouse(true);
    }

    for(int houseID = 0; houseID < NUM_HOUSES; houseID++) {
        if(!isHouseAvailable(static_cast<HOUSETYPE>(houseID))) {
            continue;
        }
        if(houseID == player) {
            GameInitSettings::HouseInfo humanHouseInfo((HOUSETYPE) player, 1);
            humanHouseInfo.graphicsSkin = campaignSkin;
            humanHouseInfo.addPlayerInfo( GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS) );

            if(supportSelected) {
                const AIRole role = partner->getRole();
                const char* roleLabel = role == AIRole::Builder ? "Builder" : role == AIRole::Fighter ? "Fighter" : "Builder + Fighter";
                std::string allyName = getHouseNameByNumber((HOUSETYPE) houseID) + " (" + _(roleLabel) + ")";
                humanHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(allyName, partnerClass));
            }

            init.addHouseInfo(humanHouseInfo);
        } else {
            GameInitSettings::HouseInfo aiHouseInfo((HOUSETYPE) houseID, 2);
            aiHouseInfo.graphicsSkin = campaignSkin;
            aiHouseInfo.addPlayerInfo( GameInitSettings::PlayerInfo(getHouseNameByNumber( (HOUSETYPE) houseID), enemyAIClass) );
            init.addHouseInfo(aiHouseInfo);
        }
    }

    if(HouseChoiceMenu::isOnline()) {
        init.enableCoop(!HouseChoiceMenu::isSingleMission(), settings.general.playerName + "'s co-op game");
        auto file = pFileManager->openCampaignFile(init.getFilename());
        const auto size = SDL_RWsize(file.get());
        if(size <= 0) throw std::runtime_error("Could not read this campaign mission.");
        std::string data(static_cast<size_t>(size), '\0');
        if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size()) throw std::runtime_error("Could not read this campaign mission.");
        init.setScenarioData(data);
        if(CrossplayMenu(init, HouseChoiceMenu::isPublicGame(), {}, OnlineModPolicy::approved(), !showLobby && OnlineModPolicy::approved()).showMenu() == MENU_QUIT_GAME_FINISHED) return;
        online = true;
    } else { startSinglePlayerGame(init); return; }
  }
}

void SinglePlayerMenu::onCustom() {
    CustomGameMenu(false).showMenu();
}

void SinglePlayerMenu::onSkirmish() {
    SinglePlayerSkirmishMenu().showMenu();
}

void SinglePlayerMenu::onLoadSavegame() {
    char tmp[FILENAME_MAX];
    fnkdat("save/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Game"), savepath, "dls"));
}

void SinglePlayerMenu::onLoadReplay() {
    char tmp[FILENAME_MAX];
    fnkdat("replay/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string replaypath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Replay"), replaypath, "rpl"));
}

void SinglePlayerMenu::onCancel() {
    quit();
}

void SinglePlayerMenu::onChildWindowClose(Window* pChildWindow) {
    std::string filename = "";
    std::string extension = "";
    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        filename = pLoadSaveWindow->getFilename();
        extension = pLoadSaveWindow->getExtension();
    }

    if(filename != "") {
        if(extension == "dls") {

            try {
                startSinglePlayerGame( GameInitSettings(filename) );
            } catch (std::exception& e) {
                // most probably the savegame file is not valid or from a different dune legacy version
                openWindow(MsgBox::create(e.what()));
            }
        } else if(extension == "rpl") {
            startReplay(filename);
        }
    }
}
