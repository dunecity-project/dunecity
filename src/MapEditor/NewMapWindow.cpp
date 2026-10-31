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

#include <MapEditor/NewMapWindow.h>
#include <MapEditor/LoadMapWindow.h>

#include <GUI/Spacer.h>
#include <GUI/dune/DuneStyle.h>

#include <globals.h>

#include <sand.h>
#include <mmath.h>

#include <MapEditor/MapGenerator.h>
#include <MapSeed.h>
#include <mod/ModManager.h>

#include <misc/draw_util.h>

#include <misc/SpiceGeneration.h>
#include <RadarViewBase.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <algorithm>

NewMapWindow::NewMapWindow(HOUSETYPE currentHouse) : Window(0,0,0,0), house(currentHouse), mapSeed(INVALID), loadMapSingleplayer(false) {

    color = getHouseInterfaceColor(house);

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_NewMapWindow);
    setBackground(pBackground);

    setCurrentPosition(calcAlignedDrawingRect(pBackground, HAlign::Center, VAlign::Center));

    setWindowWidget(&mainHBox);

    mainHBox.addWidget(HSpacer::create(16));
    mainHBox.addWidget(&mainVBox);
    mainHBox.addWidget(HSpacer::create(16));

    titleLabel.setTextColor(COLOR_LIGHTYELLOW, COLOR_TRANSPARENT);
    titleLabel.setAlignment((Alignment_Enum) (Alignment_HCenter | Alignment_VCenter));
    titleLabel.setText(_("New Map"));
    mainVBox.addWidget(&titleLabel);

    mainVBox.addWidget(VSpacer::create(8));

    mainVBox.addWidget(&centralVBox, 360);

    basicMapPropertiesHBox.addWidget(&basicMapPropertiesVBox);
    minimap.setSurface( GUIStyle::getInstance().createButtonSurface(130,130,"", true, false) );
    basicMapPropertiesHBox.addWidget(&minimap);


    emptyMapRadioButton.setText(_("Empty Map"));
    emptyMapRadioButton.setTextColor(color);
    emptyMapRadioButton.setOnClick(std::bind(&NewMapWindow::onMapTypeChanged, this, 0));
    mapTypeHBox.addWidget(&emptyMapRadioButton);
    randomMapRadioButton.setText(_("Random Map"));
    randomMapRadioButton.setTextColor(color);
    randomMapRadioButton.setOnClick(std::bind(&NewMapWindow::onMapTypeChanged, this, 1));
    mapTypeHBox.addWidget(&randomMapRadioButton);
    seedMapRadioButton.setText(_("Dune2-compatible  Map"));
    seedMapRadioButton.setTextColor(color);
    seedMapRadioButton.setOnClick(std::bind(&NewMapWindow::onMapTypeChanged, this, 2));
    mapTypeHBox.addWidget(&seedMapRadioButton);
    mapTypeRadioButtons.registerRadioButtons(3, &emptyMapRadioButton, &randomMapRadioButton, &seedMapRadioButton);
    mapTypeHBox.addWidget(Spacer::create(),5.0);
    basicMapPropertiesVBox.addWidget(&mapTypeHBox);

    basicMapPropertiesVBox.addWidget(VSpacer::create(4));
    basicMapPropertiesVBox.addWidget(&mapSizeHBox);

    // Widgets for MapSize_HBox (will be added when radio button is changed)
    mapSizeXLabel.setText(_("Map Width:"));
    mapSizeXLabel.setTextColor(color);
    mapSizeXDropDownBox.setColor(color);
    mapSizeXDropDownBox.addEntry("64",64);
    mapSizeXDropDownBox.addEntry("128",128);
    mapSizeXDropDownBox.addEntry("192",192);
    mapSizeXDropDownBox.addEntry("256",256);
    mapSizeXDropDownBox.addEntry("320",320);
    mapSizeXDropDownBox.addEntry("384",384);
    mapSizeXDropDownBox.addEntry("448",448);
    mapSizeXDropDownBox.addEntry("512",512);
    mapSizeXDropDownBox.setSelectedItem(1);
    mapSizeXDropDownBox.setOnSelectionChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    mapSizeYLabel.setText(_("Map Height:"));
    mapSizeYLabel.setTextColor(color);
    mapSizeYDropDownBox.setColor(color);
    mapSizeYDropDownBox.addEntry("64",64);
    mapSizeYDropDownBox.addEntry("128",128);
    mapSizeYDropDownBox.addEntry("192",192);
    mapSizeYDropDownBox.addEntry("256",256);
    mapSizeYDropDownBox.addEntry("320",320);
    mapSizeYDropDownBox.addEntry("384",384);
    mapSizeYDropDownBox.addEntry("448",448);
    mapSizeYDropDownBox.addEntry("512",512);
    mapSizeYDropDownBox.setSelectedItem(1);
    mapSizeYDropDownBox.setOnSelectionChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));


    mapScaleLabel.setText(_("Map Scale:"));
    mapScaleLabel.setTextColor(color);
    mapScaleDropDownBox.setColor(color);
    mapScaleDropDownBox.addEntry(_("Tiny (21x21)"),2);
    mapScaleDropDownBox.addEntry(_("Small (32x32)"),1);
    mapScaleDropDownBox.addEntry(_("Normal (62x62)"),0);
    mapScaleDropDownBox.setSelectedItem(2);
    mapScaleDropDownBox.setOnSelectionChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));

    basicMapPropertiesVBox.addWidget(VSpacer::create(4));
    basicMapPropertiesVBox.addWidget(&rngHBox);

    rngSeedLabel.setText(_("Random Seed:"));
    rngSeedLabel.setTextColor(color);
    rngHBox.addWidget(&rngSeedLabel);
    rngSeedTextBox.setMinMax(0, 32767);
    rngSeedTextBox.setValue(getRandomInt(0, 32768));
    rngSeedTextBox.setColor(house, color);
    rngSeedTextBox.setOnValueChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    rngHBox.addWidget(&rngSeedTextBox,80);

    rngHBox.addWidget(HSpacer::create(2));
    rngHBox.addWidget(Spacer::create(),3.0);

    rockLabel.setText(_("Rock:"));
    rockLabel.setTextColor(color);
    rngHBox.addWidget(&rockLabel);
    rockDigitsTextBox.setMinMax(0, 99);
    rockDigitsTextBox.setValue(ROCKFIELDS);
    rockDigitsTextBox.setColor(house, color);
    rockDigitsTextBox.setOnValueChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    rngHBox.addWidget(&rockDigitsTextBox,45);

    rngHBox.addWidget(HSpacer::create(2));
    rngHBox.addWidget(Spacer::create(),3.0);

    spiceLabel.setText(_("Spice:"));
    spiceLabel.setTextColor(color);
    rngHBox.addWidget(&spiceLabel);
    spiceDigitsTextBox.setMinMax(0, 99);
    spiceDigitsTextBox.setValue(SPICEFIELDS);
    spiceDigitsTextBox.setColor(house, color);
    spiceDigitsTextBox.setOnValueChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    rngHBox.addWidget(&spiceDigitsTextBox,45);

    rngHBox.addWidget(Spacer::create(),3.0);

    centralVBox.addWidget(&basicMapPropertiesHBox);

    const auto spicePalette = activeSpicePalette();
    greenSpiceCheckbox.setText(spicePalette.first == Terrain_RedSpice ? _("Red spice") : _("Green spice"));
    greenSpiceCheckbox.setTextColor(color);
    greenSpiceCheckbox.setOnClick(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    tornieSpiceHBox.addWidget(&greenSpiceCheckbox, 120);
    greenSpiceDigitsTextBox.setMinMax(0, 99);
    greenSpiceDigitsTextBox.setValue(0);
    greenSpiceDigitsTextBox.setColor(house, color);
    greenSpiceDigitsTextBox.setOnValueChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    tornieSpiceHBox.addWidget(&greenSpiceDigitsTextBox, 45);

    tornieSpiceHBox.addWidget(HSpacer::create(18));

    redSpiceCheckbox.setText(spicePalette.second == Terrain_WhiteSpice ? _("Blue spice") : _("Purple spice"));
    redSpiceCheckbox.setTextColor(color);
    redSpiceCheckbox.setOnClick(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    tornieSpiceHBox.addWidget(&redSpiceCheckbox, 110);
    redSpiceDigitsTextBox.setMinMax(0, 99);
    redSpiceDigitsTextBox.setValue(0);
    redSpiceDigitsTextBox.setColor(house, color);
    redSpiceDigitsTextBox.setOnValueChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    tornieSpiceHBox.addWidget(&redSpiceDigitsTextBox, 45);
    tornieSpiceHBox.addWidget(Spacer::create(), 5.0);
    centralVBox.addWidget(&tornieSpiceHBox, 28);

    centralVBox.addWidget(VSpacer::create(10));

    mirrorModeLabel.setText(_("Mirror mode") + ":");
    mirrorModeLabel.setTextColor(color);
    mirrorModeHBox.addWidget(&mirrorModeLabel);
    mirrorModeDropDownBox.addEntry(_("Off"), MirrorModeNone);
    mirrorModeDropDownBox.addEntry(_("Horizontal"), MirrorModeHorizontal);
    mirrorModeDropDownBox.addEntry(_("Vertical"), MirrorModeVertical);
    mirrorModeDropDownBox.addEntry(_("Horizontal and vertical"), MirrorModeBoth);
    mirrorModeDropDownBox.addEntry(_("Inverse"), MirrorModePoint);
    mirrorModeDropDownBox.setSelectedItem(0);
    mirrorModeDropDownBox.setOnSelectionChange(std::bind(&NewMapWindow::onMapPropertiesChanged,this));
    mirrorModeDropDownBox.setColor(color);
    mirrorModeHBox.addWidget(&mirrorModeDropDownBox, 160);
    mirrorModeHBox.addWidget(Spacer::create(), 5.0);
    centralVBox.addWidget(&mirrorModeHBox);

    centralVBox.addWidget(Spacer::create(), 5.0);

    centralVBox.addWidget(VSpacer::create(10));

    authorLabel.setText(_("Author:"));
    authorLabel.setTextColor(color);
    authorHBox.addWidget(&authorLabel, 120);
    authorTextBox.setText("");
    authorTextBox.setTextColor(color);
    authorHBox.addWidget(&authorTextBox);
    authorHBox.addWidget(HSpacer::create(140));
    centralVBox.addWidget(&authorHBox);

    centralVBox.addWidget(VSpacer::create(10));

    licenseLabel.setText(_("License:"));
    licenseLabel.setTextColor(color);
    licenseHBox.addWidget(&licenseLabel, 120);
    licenseTextBox.setText("CC-BY-SA");
    licenseTextBox.setTextColor(color);
    licenseHBox.addWidget(&licenseTextBox);
    licenseHBox.addWidget(HSpacer::create(140));
    centralVBox.addWidget(&licenseHBox);

    centralVBox.addWidget(Spacer::create());

    mainVBox.addWidget(VSpacer::create(5));

    mainVBox.addWidget(&buttonHBox);

    cancelButton.setText(_("Cancel"));
    cancelButton.setTextColor(color);
    cancelButton.setOnClick(std::bind(&NewMapWindow::onCancel, this));

    buttonHBox.addWidget(&cancelButton);

    buttonHBox.addWidget(HSpacer::create(8));

    loadButton.setText(_("Load Map") + "...");
    loadButton.setTextColor(color);
    loadButton.setOnClick(std::bind(&NewMapWindow::onLoad, this));

    buttonHBox.addWidget(&loadButton);

    buttonHBox.addWidget(HSpacer::create(8));

    createButton.setText(_("Create"));
    createButton.setTextColor(color);
    createButton.setOnClick(std::bind(&NewMapWindow::onCreate, this));

    buttonHBox.addWidget(&createButton);

    mainVBox.addWidget(VSpacer::create(10));

    randomMapRadioButton.setChecked(true);
    onMapTypeChanged(1);
}

void NewMapWindow::onCancel() {
    mapdata = MapData(0,0);

    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void NewMapWindow::onLoad() {
    openWindow(LoadMapWindow::create(color));
}

void NewMapWindow::onCreate() {
    Window* pParentWindow = dynamic_cast<Window*>(getParent());
    if(pParentWindow != nullptr) {
        pParentWindow->closeChildWindow();
    }
}

void NewMapWindow::onChildWindowClose(Window* pChildWindow) {
    LoadMapWindow* pLoadMapWindow = dynamic_cast<LoadMapWindow*>(pChildWindow);
    if(pLoadMapWindow != nullptr) {
        loadMapFilepath = pLoadMapWindow->getLoadMapFilepath();
        loadMapname = pLoadMapWindow->getLoadMapname();
        loadMapSingleplayer = pLoadMapWindow->isLoadMapSingleplayer();

        if(loadMapFilepath != "") {
            Window* pParentWindow = dynamic_cast<Window*>(getParent());
            if(pParentWindow != nullptr) {
                pParentWindow->closeChildWindow();
            }
        }
    }
}

void NewMapWindow::onMapTypeChanged(int buttonID) {

    mapSizeHBox.removeAllChildWidgets();

    if(buttonID == 2) {
        mapSizeHBox.addWidget(&mapScaleLabel);
        mapSizeHBox.addWidget(&mapScaleDropDownBox,120);
        mapSizeHBox.addWidget(Spacer::create(),5.0);
    } else {
        mapSizeHBox.addWidget(&mapSizeXLabel);
        mapSizeHBox.addWidget(&mapSizeXDropDownBox, 50);
        mapSizeHBox.addWidget(HSpacer::create(30));
        mapSizeHBox.addWidget(&mapSizeYLabel);
        mapSizeHBox.addWidget(&mapSizeYDropDownBox, 50);
        mapSizeHBox.addWidget(Spacer::create(),5.0);
    }

    rngSeedLabel.setVisible( (buttonID != 0) );
    rngSeedTextBox.setVisible( (buttonID != 0) );

    rockLabel.setVisible( (buttonID == 1) );
    rockDigitsTextBox.setVisible( (buttonID == 1) );
    spiceLabel.setVisible( (buttonID == 1) );
    spiceDigitsTextBox.setVisible( (buttonID == 1) );
    const bool showTornieSpice = (buttonID == 1) && ModManager::instance().isTornieFamilyContentActive();
    tornieSpiceHBox.setVisible(showTornieSpice);
    greenSpiceDigitsTextBox.setEnabled(showTornieSpice && greenSpiceCheckbox.isChecked());
    redSpiceDigitsTextBox.setEnabled(showTornieSpice && redSpiceCheckbox.isChecked());

    mirrorModeLabel.setVisible( (buttonID == 1) );
    mirrorModeDropDownBox.setVisible( (buttonID == 1) );

    onMapPropertiesChanged();
}

void NewMapWindow::onMapPropertiesChanged() {
    const bool showTornieSpice = randomMapRadioButton.isChecked() && ModManager::instance().isTornieFamilyContentActive();
    const auto spicePalette = activeSpicePalette();
    const unsigned spiceMask = activeSpiceVariantMask();
    const bool firstEnabled = showTornieSpice && (spiceMask & (spicePalette.first == Terrain_RedSpice ? 2u : 1u));
    const bool secondEnabled = showTornieSpice && (spiceMask & (spicePalette.second == Terrain_WhiteSpice ? 8u : 4u));
    greenSpiceCheckbox.setEnabled(firstEnabled);
    redSpiceCheckbox.setEnabled(secondEnabled);
    greenSpiceDigitsTextBox.setEnabled(firstEnabled && greenSpiceCheckbox.isChecked());
    redSpiceDigitsTextBox.setEnabled(secondEnabled && redSpiceCheckbox.isChecked());

    if(emptyMapRadioButton.isChecked()) {
        int sizeX = mapSizeXDropDownBox.getSelectedEntryIntData();
        int sizeY = mapSizeYDropDownBox.getSelectedEntryIntData();

        mapSeed = INVALID;
        mapdata = MapData(sizeX, sizeY, Terrain_Sand);
    } else if(randomMapRadioButton.isChecked()) {
        int sizeX = mapSizeXDropDownBox.getSelectedEntryIntData();
        int sizeY = mapSizeYDropDownBox.getSelectedEntryIntData();

        int seed = rngSeedTextBox.getValue();
        int rock = rockDigitsTextBox.getValue();
        int spice = spiceDigitsTextBox.getValue();
        const int greenSpice = firstEnabled && greenSpiceCheckbox.isChecked() ? greenSpiceDigitsTextBox.getValue() : 0;
        const int redSpice = secondEnabled && redSpiceCheckbox.isChecked() ? redSpiceDigitsTextBox.getValue() : 0;

        MirrorMode mirrorMode = (MirrorMode) mirrorModeDropDownBox.getSelectedEntryIntData();

        mapSeed = INVALID;
        mapdata = generateRandomMap(sizeX, sizeY, seed, rock, spice, mirrorMode, greenSpice, redSpice,
                                    spicePalette.first, spicePalette.second);

    } else if(seedMapRadioButton.isChecked()) {
        int seed = rngSeedTextBox.getValue();
        int scale = mapScaleDropDownBox.getSelectedEntryIntData();

        mapSeed = seed;
        mapdata = createMapWithSeed(seed, scale);
    }

    minimap.setSurface(createMinimapPicture(mapdata, 1, DuneStyle::buttonBorderColor));
}

sdl2::surface_ptr NewMapWindow::createMinimapPicture(MapData& mapdata, int borderWidth, Uint32 borderColor) {
    sdl2::surface_ptr pMinimap = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, 128+2*borderWidth, 128+2*borderWidth, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!pMinimap) {
        return nullptr;
    }
    SDL_FillRect(pMinimap.get(), nullptr, borderColor);
    SDL_Rect dest = { borderWidth, borderWidth, pMinimap->w - 2*borderWidth, pMinimap->h - 2*borderWidth};
    SDL_FillRect(pMinimap.get(), &dest, COLOR_BLACK);

    int offsetX;
    int offsetY;

    RadarScaleInfo scaleInfo = RadarViewBase::calculateScaleAndOffsets(mapdata.getSizeX(), mapdata.getSizeY());

    offsetX = scaleInfo.offsetX + borderWidth;
    offsetY = scaleInfo.offsetY + borderWidth;
    sdl2::surface_lock lock{pMinimap.get()};
    for(int pixelY = 0; pixelY < scaleInfo.scaledHeight; ++pixelY) {
        const int tileY = std::clamp(static_cast<int>(pixelY / scaleInfo.scale), 0, mapdata.getSizeY() - 1);
        Uint32* row = reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(pMinimap->pixels) + (offsetY + pixelY) * pMinimap->pitch) + offsetX;
        for(int pixelX = 0; pixelX < scaleInfo.scaledWidth; ++pixelX) {
            const int tileX = std::clamp(static_cast<int>(pixelX / scaleInfo.scale), 0, mapdata.getSizeX() - 1);

            TERRAINTYPE terrainType = mapdata(tileX, tileY);

            Uint32 color = getColorByTerrainType(terrainType);

            row[pixelX] = MapRGBA(pMinimap->format, color);
        }
    }

    return pMinimap;
}
