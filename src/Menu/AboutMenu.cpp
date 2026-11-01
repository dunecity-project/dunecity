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

#include <Menu/AboutMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/SoundtrackPack.h>

AboutMenu::AboutMenu() : MenuBase()
{
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    const int panelWidth = std::min(getRendererWidth() - 48, 680);
    const int panelHeight = std::min(getRendererHeight() - 48, 520);
    const int panelX = (getRendererWidth() - panelWidth) / 2;
    const int panelY = (getRendererHeight() - panelHeight) / 2;

    title.setText(_("ABOUT DUNE LEGACY"));
    title.setTextFontSize(20);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(panelX, panelY), Point(panelWidth, 34));

    credits.setTextFontSize(14);
    credits.setAutohideScrollbar(false);
    const auto soundtrack = SoundtrackPack::loadInstalled();
    const std::string soundtrackCredit = soundtrack
        ? "ALTERNATIVE SOUNDTRACK\n" + soundtrack->creditText() + "\n\n" : "";
    credits.setText(
        std::string("Dune Legacy, DuneCity, and Dune2R have been built and maintained by many people.\n\n")
        + soundtrackCredit +
        "PROJECT LEADERSHIP\n"
        "Stefan van der Wel - creator of Dune Legacy and DuneCity\n"
        "Vukasin Ristic - Dune Legacy repository maintainer; DuneCity maintainer and gameplay developer; "
        "creator of Dune2R\n\n"
        "DUNE LEGACY CONTRIBUTORS\n"
        "Anthony Cole\n"
        "Richard Schaller\n"
        "Olaf van der Spek\n"
        "Raal Goff\n"
        "Stefen Hendriks - random map generator\n"
        "Felix Medrano - Spanish translation\n"
        "and many others\n\n"
        "Dune Legacy team - base engine and ongoing contributions\n"
        "SimHacker and Micropolis teams - city simulation reference\n"
        "Tornie - Tornie mod content and design\n\n"
        "Most graphics and sounds are loaded from the original Dune II PAK files, "
        "which are not distributed with Dune Legacy. Shipped map-editor icons are "
        "primarily from the public-domain Tango icon theme. Other contributed graphics "
        "use the Dune Legacy license; maps are CC-BY-SA.\n\n"
        "Dune Legacy collects gameplay statistics when matches start and end, including "
        "player display names, houses, results, and unit statistics. These statistics help "
        "improve game balance and the player experience.\n\n"
        "Dune Legacy is free software released under the GNU General Public License.");
    windowWidget.addWidget(&credits, Point(panelX, panelY + 40), Point(panelWidth, panelHeight - 88));

    backButton.setText(_("BACK"));
    backButton.setOnClick(std::bind(&AboutMenu::onBack, this));
    backButton.setActive();
    windowWidget.addWidget(&backButton, Point(panelX + (panelWidth - 180) / 2, panelY + panelHeight - 40),
                           Point(180, 32));

    if(soundtrack && settings.audio.alternativeSoundtrack && musicPlayer) musicPlayer->changeMusic(MUSIC_CREDITS);
}

AboutMenu::~AboutMenu()
{
    if(settings.audio.alternativeSoundtrack && musicPlayer) musicPlayer->changeMusic(MUSIC_MENU);
}

void AboutMenu::onBack()
{
    quit();
}
