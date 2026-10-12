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

#include <GUI/dune/InGameMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <misc/fnkdat.h>
#include <Game.h>
#include <main.h>
#include <GameInitSettings.h>
#include <Network/NetworkManager.h>

#include <GUI/MsgBox.h>
#include <GUI/QstBox.h>
#include <GUI/dune/JoinRequestsWindow.h>
#include <GUI/dune/InGameSettingsMenu.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <misc/MenuPalette.h>


Point InGameMenuButton::getMinimumSize() const {
    return Point(GUIStyle::getInstance().getTextWidth(getText(),20)+32,40);
}

InGameMenu::InGameMenu(bool bMultiplayer, int color)
 : Window(0,0,0,0), bMultiplayer(bMultiplayer), color(color) {
    const bool canSkip = currentGame->canSkipMission();
    const bool canJoin=pNetworkManager && pNetworkManager->isServer() && pNetworkManager->getDirectTransport() && pNetworkManager->getDirectTransport()->allowsLateJoin();
    const bool canRequest=currentGame->isSpectating() && pNetworkManager->getDirectTransport();
    const int buttons = ((canJoin || canRequest) ? 4 : 3) + (bMultiplayer ? 2 : 3) + (canSkip ? 1 : 0);
    const int width = std::min(440,getRendererWidth()-32);
    const int height = 92 + buttons*40 + (buttons-1)*6;
    auto background = GUIStyle::getInstance().createBackground(width, height);
    // Retain the Modern menu's existing outline. Classic uses the selected
    // style's authentic sand texture and raised gold frame.
    if(!MenuTheme::isClassicDesert())
        drawRect(background.get(),0,0,width-1,height-1,COLOR_RGB(190,153,77));
    setBackground(std::move(background));
    setCurrentPosition((getRendererWidth()-width)/2,(getRendererHeight()-height)/2,width,height);
    setWindowWidget(&mainHBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainHBox.addWidget(&mainVBox);
    mainHBox.addWidget(HSpacer::create(24));
    mainVBox.addWidget(VSpacer::create(12));
    title.setText("Dune City");
    title.setTextFontSize(24);
    title.setTextColor(MenuTheme::text,COLOR_TRANSPARENT);
    title.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    mainVBox.addWidget(&title,32);
    onlineNotice.setTextColor(MenuTheme::muted,COLOR_TRANSPARENT);
    onlineNotice.setTextFontSize(14);
    onlineNotice.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    mainVBox.addWidget(&onlineNotice,20);
    mainVBox.addWidget(VSpacer::create(12));

    auto addButton=[&](InGameMenuButton& button,const std::string& text,auto callback) {
        button.setText(text);
        button.setOnClick(callback);
        mainVBox.addWidget(&button,40);
    };
    auto gap=[&]() { mainVBox.addWidget(VSpacer::create(6)); };
    addButton(resumeButton,bMultiplayer ? _("Back to Game") : _("Resume Game"),std::bind(&InGameMenu::onResume,this));
    if(canJoin) {
        gap(); addButton(joinRequestsButton,"Join requests ("+std::to_string(pNetworkManager->getDirectTransport()->joinRequests().size())+")",[this](){openWindow(JoinRequestsWindow::create());});
    }
    if(canRequest) {
        auto* direct=pNetworkManager->getDirectTransport();
        const bool pending=direct->playRequestState()=="pending";
        gap(); addButton(joinRequestsButton,pending ? "Cancel request to play" : "Request to play",[this,pending]() {
            auto* direct=pNetworkManager->getDirectTransport();
            if(direct->requestToPlay(pending)) currentGame->resumeGame();
        });
    }
    if(canSkip) {
        gap();addButton(skipMissionButton,_("Skip mission..."),std::bind(&InGameMenu::onSkipMission,this));
    }
    gap();addButton(saveGameButton,_("Save Game"),std::bind(&InGameMenu::onSave,this));
    loadGameButton.setVisible(!bMultiplayer);loadGameButton.setEnabled(!bMultiplayer);

    restartGameButton.setVisible(!bMultiplayer);restartGameButton.setEnabled(!bMultiplayer);
    if(!bMultiplayer) {
        gap();addButton(loadGameButton,_("Load Game"),std::bind(&InGameMenu::onLoad,this));
        gap();addButton(restartGameButton,_("Restart Game"),std::bind(&InGameMenu::onRestart,this));
    }
    gap();addButton(gameSettingsButton,_("Game Settings"),std::bind(&InGameMenu::onSettings,this));
    if (bMultiplayer) {
        gap();addButton(pauseGameButton,_("Pause match"),[]() {
            currentGame->toggleMatchPause();
            currentGame->resumeGame(); // Close the menu after the explicit control action.
        });
    }
    gap();addButton(quitButton,_("Quit to Menu"),std::bind(&InGameMenu::onQuit,this));
    mainVBox.addWidget(VSpacer::create(16));
    updateMatchControls();
}

void InGameMenu::updateMatchControls() {
    // The host's own menu requests a shared pause on the way in, and any peer can
    // pause or resume while this menu is open, so both the notice and the button
    // follow the live match state instead of the state at construction time.
    const bool paused = currentGame->isGamePaused();
    const bool pending = currentGame->isPauseRequestPending();
    const std::string notice = paused ? _("Game paused")
        : !pNetworkManager ? _("Game menu")
        : pending ? _("Pausing...") : _("Online game continues");
    if(onlineNotice.getText()!=notice) onlineNotice.setText(notice);
    if(!bMultiplayer) return;
    const std::string pauseText = paused ? _("Resume match")
        : pending ? _("Pausing...") : _("Pause match");
    if(pauseGameButton.getText()!=pauseText) pauseGameButton.setText(pauseText);
    pauseGameButton.setEnabled(currentGame->canToggleMatchPause() && (paused || !pending));
}

void InGameMenu::draw(Point position) {
    updateMatchControls();
    Window::draw(position);
}

InGameMenu::~InGameMenu()
{
    ;
}

bool InGameMenu::handleKeyPress(SDL_KeyboardEvent& key) {
    switch( key.keysym.sym ) {
        case SDLK_ESCAPE:
        {
            currentGame->resumeGame();
        } break;

        case SDLK_RETURN:
            if(SDL_GetModState() & KMOD_ALT) {
                toogleFullscreen();
            }
            break;

        case SDLK_TAB:
            if(SDL_GetModState() & KMOD_ALT) {
                SDL_MinimizeWindow(window);
            }
            break;

        default:
            break;
    }

    return Window::handleKeyPress(key);
}

void InGameMenu::onChildWindowClose(Window* pChildWindow) {
    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        std::string FileName = pLoadSaveWindow->getFilename();
        bool bSave = pLoadSaveWindow->isSaveWindow();

        if(FileName != "") {
            if(bSave == false) {
                // load window
                try {
                    currentGame->setNextGameInitSettings(GameInitSettings(FileName));
                } catch (std::exception& e) {
                    // most probably the savegame file is not valid or from a different dune legacy version
                    openWindow(MsgBox::create(e.what()));
                }

                currentGame->resumeGame();
                currentGame->quitGame();

            } else {
                // save window
                currentGame->saveGame(FileName);

                currentGame->resumeGame();
            }
        }
    } else {
        QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
        if(pQstBox != nullptr) {
            if(pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
                if(pQstBox->getText() == _("Do you really want to quit this game?")) {
                    // quit
                    currentGame->quitGame();
                } else if (pQstBox->getText()==_("Skip this mission and continue to the next level?")) {
                    currentGame->confirmSkipMission();
                    currentGame->resumeGame();
                } else {
                    // restart
                    // set new current init settings as init info for next game
                    currentGame->setNextGameInitSettings(currentGame->getGameInitSettings());

                    // quit current game
                    currentGame->resumeGame();
                    currentGame->quitGame();
                }
            }
        }
    }
}

void InGameMenu::onResume()
{
    currentGame->resumeGame();
}

void InGameMenu::onSettings()
{
    openWindow(InGameSettingsMenu::create());
}

void InGameMenu::onSave()
{
    char tmp[FILENAME_MAX];
    fnkdat(bMultiplayer ? "mpsave/" : "save/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(true, _("Save Game"), savepath, "dls", "", color));
}

void InGameMenu::onLoad()
{
    char tmp[FILENAME_MAX];
    fnkdat("save/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Game"), savepath, "dls", "", color));
}

void InGameMenu::onRestart()
{
    QstBox* pQstBox = QstBox::create(   _("Do you really want to restart this game?"),
                                        _("Yes"),
                                        _("No"),
                                        QSTBOX_BUTTON2);

    pQstBox->setTextColor(color);

    openWindow(pQstBox);
}

void InGameMenu::onQuit()
{
    QstBox* pQstBox = QstBox::create(   _("Do you really want to quit this game?"),
                                        _("Yes"),
                                        _("No"),
                                        QSTBOX_BUTTON2);

    pQstBox->setTextColor(color);

    openWindow(pQstBox);
}

void InGameMenu::onSkipMission() {
    if(!currentGame->canSkipMission())return;
    auto* confirmation=QstBox::create(_("Skip this mission and continue to the next level?"),
        _("Skip mission"),_("Cancel"),QSTBOX_BUTTON2);
    confirmation->setTextColor(color);
    openWindow(confirmation);
}
