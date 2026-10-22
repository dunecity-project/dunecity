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

#include <Menu/CustomGameStatsMenu.h>

#include <globals.h>

#include <misc/FileSystem.h>
#include <misc/format.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <Game.h>
#include <House.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <misc/draw_util.h>
#include <array>
#include <cmath>

#include <algorithm>


CustomGameStatsMenu::CustomGameStatsMenu() : MenuBase()
{
    // Restore the original yellow patterned wallpaper used by classic results.
    auto background = copySurface(pGFXManager->getBackgroundSurface());
    const Point size(background->w, background->h);
    setBackground(std::move(background));
    resize(size);
    setWindowWidget(&windowWidget);

    const Uint32 localHouseColor = getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3);
    showTaxStatistics = currentGame->isCitySimEnabled()
        && ModManager::instance().getContentBase(currentGame->getGameInitSettings().getModName()) == "dunecity";
    const int columns = showTaxStatistics ? 4 : 3;
    const int contentWidth = size.x - 48;
    const int nameWidth = size.x < 800 ? 132 : 150;
    const int gap = 10;
    const int metricWidth = (contentWidth - nameWidth - gap * columns) / columns;
    const int fontSize = size.x < 800 ? 12 : 14;
    windowWidget.addWidget(&mainVBox, Point(24,23), Point(contentWidth, size.y - 46));
    captionLabel.setText(getBasename(currentGame->getGameInitSettings().getFilename(), true));
    captionLabel.setTextColor(localHouseColor);
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(24));
    mainVBox.addWidget(Spacer::create(), 0.05);
    mainVBox.addWidget(&mainHBox, 0.80);
    mainHBox.addWidget(&playerStatListVBox, 1.0);
    headerHBox.addWidget(&headerLabelDummy, nameWidth);
    const std::array<std::string,4> titles{{_("Built Objects"),_("Destroyed"),_("Harvested Spice"),_("Tax Collected")}};
    for(int column=0;column<columns;++column) {
        headerHBox.addWidget(HSpacer::create(gap));
        headers[column].setText(titles[column]);
        headers[column].setTextFontSize(fontSize);
        headers[column].setAlignment(Alignment_HCenter);
        headers[column].setTextColor(localHouseColor);
        headerHBox.addWidget(&headers[column],metricWidth);
    }
    playerStatListVBox.addWidget(&headerHBox,25);
    playerStatListVBox.addWidget(VSpacer::create(15));
    auto values=[](const House* house) {
        return std::array<double,4>{{double(house->getBuiltValue()),double(house->getDestroyedValue())*100,
            house->getHarvestedSpice().toDouble(),house->getCityTaxReceipts().toDouble()}};
    };
    std::array<double,4> maxima{};
    // Player houses only: the Natives (worms, crime troopers) are no contestant and get no row.
    for(int i=0;i<NUM_PLAYER_HOUSES;++i) if(const auto* house=currentGame->getHouse(i)) {
        const auto totals=values(house);
        for(int column=0;column<columns;++column) maxima[column]=std::max(maxima[column],totals[column]);
    }
    for(int i=0;i<NUM_PLAYER_HOUSES;++i) if(const auto* house=currentGame->getHouse(i)) {
        auto& row=houseStat[i];
        const auto totals=values(house);
        const int visualColor=getHouseVisualHouse(i);
        const Uint32 textColor=getHouseColorRGB(visualColor,3);
        row.houseName.setText(_("House")+" "+getHouseNameByNumber(static_cast<HOUSETYPE>(i)));
        row.houseName.setTextFontSize(fontSize);
        row.houseName.setTextColor(textColor);
        row.houseHBox.addWidget(&row.houseName,nameWidth);
        for(int column=0;column<columns;++column) {
            row.houseHBox.addWidget(HSpacer::create(gap));
            row.values[column].setText(std::to_string(std::llround(totals[column])));
            row.values[column].setTextFontSize(12);
            row.values[column].setAlignment(Alignment_Right);
            row.values[column].setTextColor(textColor);
            row.progressBars[column].setProgress(maxima[column]>0 ? totals[column]*100/maxima[column] : 0);
            row.progressBars[column].setDrawShadow(true);
            row.progressBars[column].setColor(getHouseColorRGB(visualColor,1));
            // Stacking keeps long totals legible even with four columns at 640px.
            row.metricBoxes[column].addWidget(&row.values[column],16);
            row.metricBoxes[column].addWidget(&row.progressBars[column],8);
            row.houseHBox.addWidget(&row.metricBoxes[column],metricWidth);
        }
        playerStatListVBox.addWidget(&row.houseHBox,26);
        playerStatListVBox.addWidget(VSpacer::create(10));
    }

    mainVBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(VSpacer::create(20));
    mainVBox.addWidget(&buttonHBox, 24);
    mainVBox.addWidget(VSpacer::create(14), 0.0);

    buttonHBox.addWidget(HSpacer::create(70));
    int totalTime = currentGame->getGameTime()/1000;
    timeLabel.setText(fmt::sprintf(_("@DUNE.ENG|22#Time: %d:%02d"), totalTime/3600, (totalTime%3600)/60));
    timeLabel.setTextColor(localHouseColor);
    buttonHBox.addWidget(&timeLabel, 0.2);

    buttonHBox.addWidget(Spacer::create(), 0.0625);
    buttonHBox.addWidget(Spacer::create(), 0.475);
    buttonHBox.addWidget(Spacer::create(), 0.0625);

    okButton.setText(_("OK"));
    okButton.setTextColor(localHouseColor);
    okButton.setOnClick(std::bind(&CustomGameStatsMenu::onOK, this));
    buttonHBox.addWidget(&okButton, 0.2);
    buttonHBox.addWidget(HSpacer::create(90));
}

CustomGameStatsMenu::~CustomGameStatsMenu()
{
    ;
}

void CustomGameStatsMenu::onOK()
{
    quit();
}

