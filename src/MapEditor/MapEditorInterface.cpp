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

#include <MapEditor/MapEditorInterface.h>
#include <MapEditor/MapEditor.h>
#include <MapEditor/NewMapWindow.h>
#include <MapEditor/LoadMapWindow.h>
#include <MapEditor/PlayerSettingsWindow.h>
#include <MapEditor/MapSettingsWindow.h>
#include <MapEditor/ChoamWindow.h>
#include <MapEditor/ReinforcementsWindow.h>
#include <MapEditor/TeamsWindow.h>
#include <MapEditor/MapInfo.h>
#include <MapEditor/MapMirror.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <sand.h>
#include <ScreenBorder.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>

#include <ObjectBase.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <FileClasses/INIFile.h>
#include <GUI/MsgBox.h>
#include <GUI/ObjectInterfaces/ObjectInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/QstBox.h>

#include <misc/draw_util.h>
#include <misc/SDL2pp.h>

namespace {
// Sidebar rows below the 132px radar. The mode chooser and the house drop down
// are packed directly under the radar so the two metaserver buttons get a
// full-width row each; the object pickers still start well above the bottom
// bar at the minimum 640x480 resolution.
constexpr int SidebarModeChooserY      = 134;
constexpr int SidebarModeChooserHeight = 22;
constexpr int SidebarHouseY            = 157;
constexpr int SidebarHouseHeight       = 20;
constexpr int SidebarMetaserverSaveY   = 178;
constexpr int SidebarMetaserverLoadY   = 199;
constexpr int SidebarMetaserverHeight  = 20;
constexpr int SidebarPickerY           = 221;
}

MapEditorInterface::MapEditorInterface(MapEditor* pMapEditor)
 : Window(0,0,0,0), pMapEditor(pMapEditor), radarView(pMapEditor) {
    house = HOUSE_HARKONNEN;
    color = getHouseInterfaceColor(house);

    currentTerrainType = -1;
    currentTerrainPenSize = -1;

    currentEditStructureID = INVALID;
    currentEditUnitID = INVALID;

    tornieContentVisible_ = (ModManager::instance().isTornieContentActive());


    setTransparentBackground(true);

    setCurrentPosition(0,0,getRendererWidth(),getRendererHeight());

    setWindowWidget(&windowWidget);

    // top bar
    SDL_Texture* pTopBarTexture = pGFXManager->getUIGraphic(UI_TopBar, HOUSE_HARKONNEN);
    topBar.setTexture(pTopBarTexture);
    windowWidget.addWidget(&topBar, calcAlignedDrawingRect(pTopBarTexture, HAlign::Left, VAlign::Top));

    // side bar
    SDL_Texture* pSideBarTexture = pGFXManager->getUIGraphic(UI_MapEditor_SideBar, HOUSE_HARKONNEN);
    sideBar.setTexture(pSideBarTexture);
    windowWidget.addWidget(&sideBar, calcAlignedDrawingRect(pSideBarTexture, HAlign::Right, VAlign::Top));

    // bottom bar
    SDL_Texture* pBottomBarTexture = pGFXManager->getUIGraphic(UI_MapEditor_BottomBar, HOUSE_HARKONNEN);
    bottomBar.setTexture(pBottomBarTexture);
    windowWidget.addWidget(&bottomBar, calcAlignedDrawingRect(pBottomBarTexture, HAlign::Left, VAlign::Bottom));


    // add radar
    windowWidget.addWidget(&radarView,Point(getRendererWidth()-SIDEBARWIDTH+SIDEBAR_COLUMN_WIDTH, 0),radarView.getMinimumSize());
    radarView.setOnRadarClick(std::bind(&MapEditorInterface::onRadarClick, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // add buttons
    windowWidget.addWidget(&topBarHBox,Point(0,3),  Point(getRendererWidth() - sideBar.getSize().x, 24));

    topBarHBox.addWidget(HSpacer::create(3));

    exitButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ExitIcon));
    exitButton.setTooltipText(_("Leave Mapeditor"));
    exitButton.setOnClick(std::bind(&MapEditorInterface::onQuit, this));
    topBarHBox.addWidget(&exitButton,24);

    topBarHBox.addWidget(HSpacer::create(10));

    newButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_NewIcon));
    newButton.setTooltipText(_("New Map"));
    newButton.setOnClick(std::bind(&MapEditorInterface::onNew, this));
    topBarHBox.addWidget(&newButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    loadButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_LoadIcon));
    loadButton.setTooltipText(_("Load Map"));
    loadButton.setOnClick(std::bind(&MapEditorInterface::onLoad, this));
    topBarHBox.addWidget(&loadButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    saveButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SaveIcon));
    saveButton.setTooltipText(_("Save Map"));
    saveButton.setOnClick([this]() { onSave(); });
    topBarHBox.addWidget(&saveButton,24);

    topBarHBox.addWidget(HSpacer::create(3));

    undoButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_UndoIcon));
    undoButton.setTooltipText(_("Undo"));
    undoButton.setOnClick(std::bind(&MapEditorInterface::onUndo, this));
    topBarHBox.addWidget(&undoButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    redoButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RedoIcon));
    redoButton.setTooltipText(_("Redo"));
    redoButton.setOnClick(std::bind(&MapEditorInterface::onRedo, this));
    topBarHBox.addWidget(&redoButton,24);

    topBarHBox.addWidget(HSpacer::create(10));

    playersButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_PlayerIcon));
    playersButton.setTooltipText(_("Player Settings"));
    playersButton.setOnClick(std::bind(&MapEditorInterface::onPlayers, this));
    topBarHBox.addWidget(&playersButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    mapSettingsButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MapSettingsIcon));
    mapSettingsButton.setTooltipText(_("Map Settings"));
    mapSettingsButton.setOnClick(std::bind(&MapEditorInterface::onMapSettings, this));
    topBarHBox.addWidget(&mapSettingsButton,24);

    topBarHBox.addWidget(HSpacer::create(10));

    choamButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ChoamIcon));
    choamButton.setTooltipText(_("Choam"));
    choamButton.setOnClick(std::bind(&MapEditorInterface::onChoam, this));
    topBarHBox.addWidget(&choamButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    reinforcementsButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ReinforcementsIcon));
    reinforcementsButton.setTooltipText(_("Reinforcements"));
    reinforcementsButton.setOnClick(std::bind(&MapEditorInterface::onReinforcements, this));
    topBarHBox.addWidget(&reinforcementsButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    teamsButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_TeamsIcon));
    teamsButton.setTooltipText(_("Teams"));
    teamsButton.setOnClick(std::bind(&MapEditorInterface::onTeams, this));
    teamsButton.setVisible( (pMapEditor->getMapVersion() < 2) );
    topBarHBox.addWidget(&teamsButton,24);

    topBarHBox.addWidget(HSpacer::create(10));

    mirrorModeNoneButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MirrorNoneIcon));
    mirrorModeNoneButton.setToggleButton(true);
    mirrorModeNoneButton.setTooltipText(_("Mirror mode") + ": " + _("Off"));
    mirrorModeNoneButton.setOnClick(std::bind(&MapEditorInterface::onMirrorModeButton, this, MirrorModeNone));
    mirrorModeNoneButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    topBarHBox.addWidget(&mirrorModeNoneButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    mirrorModeHorizontalButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MirrorHorizontalIcon));
    mirrorModeHorizontalButton.setToggleButton(true);
    mirrorModeHorizontalButton.setTooltipText(_("Mirror mode") + ": " + _("Horizontal"));
    mirrorModeHorizontalButton.setOnClick(std::bind(&MapEditorInterface::onMirrorModeButton, this, MirrorModeHorizontal));
    mirrorModeHorizontalButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    topBarHBox.addWidget(&mirrorModeHorizontalButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    mirrorModeVerticalButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MirrorVerticalIcon));
    mirrorModeVerticalButton.setToggleButton(true);
    mirrorModeVerticalButton.setTooltipText(_("Mirror mode") + ": " + _("Vertical"));
    mirrorModeVerticalButton.setOnClick(std::bind(&MapEditorInterface::onMirrorModeButton, this, MirrorModeVertical));
    mirrorModeVerticalButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    topBarHBox.addWidget(&mirrorModeVerticalButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    mirrorModeBothButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MirrorBothIcon));
    mirrorModeBothButton.setToggleButton(true);
    mirrorModeBothButton.setTooltipText(_("Mirror mode") + ": " + _("Horizontal and vertical"));
    mirrorModeBothButton.setOnClick(std::bind(&MapEditorInterface::onMirrorModeButton, this, MirrorModeBoth));
    mirrorModeBothButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    topBarHBox.addWidget(&mirrorModeBothButton,24);

    topBarHBox.addWidget(HSpacer::create(1));

    mirrorModePointButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MirrorPointIcon));
    mirrorModePointButton.setToggleButton(true);
    mirrorModePointButton.setTooltipText(_("Mirror mode") + ": " + _("Inverse"));
    mirrorModePointButton.setOnClick(std::bind(&MapEditorInterface::onMirrorModeButton, this, MirrorModePoint));
    mirrorModePointButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    topBarHBox.addWidget(&mirrorModePointButton,24);


    topBarHBox.addWidget(Spacer::create(),0.5);

    // add editor mode buttons
    windowWidget.addWidget( &editorModeChooserHBox,
                            Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarModeChooserY),
                            Point(sideBar.getSize().x - 15,SidebarModeChooserHeight));

    terrainButton.setText("T");
    terrainButton.setToggleButton(true);
    terrainButton.setOnClick(std::bind(&MapEditorInterface::onModeButton, this, 1));
    editorModeChooserHBox.addWidget(&terrainButton);

    editorModeChooserHBox.addWidget(HSpacer::create(2));

    structuresButton.setText("S");
    structuresButton.setToggleButton(true);
    structuresButton.setOnClick(std::bind(&MapEditorInterface::onModeButton, this, 2));
    editorModeChooserHBox.addWidget(&structuresButton);

    editorModeChooserHBox.addWidget(HSpacer::create(2));

    unitsButton.setText("U");
    unitsButton.setToggleButton(true);
    unitsButton.setOnClick(std::bind(&MapEditorInterface::onModeButton, this, 3));
    editorModeChooserHBox.addWidget(&unitsButton);

    // house choice
    houseDropDownBox.setOnSelectionChange(std::bind(&MapEditorInterface::onHouseDropDownChanged, this, std::placeholders::_1));
    windowWidget.addWidget( &houseDropDownBox,
                            Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarHouseY),
                            Point(sideBar.getSize().x - 15,SidebarHouseHeight));

    // metaserver actions
    const int sidebarButtonWidth = sideBar.getSize().x - 15;
    // A clipped label is worse than a shorter one, so fall back when the sidebar
    // column (or a translation) is too narrow for the full wording.
    const auto setFittingText = [sidebarButtonWidth](TextButton& button, const std::string& text, const std::string& shortText) {
        button.setText(text);
        if(button.getMinimumSize().x > sidebarButtonWidth) button.setText(shortText);
    };

    setFittingText(metaserverSaveButton, _("Save to metaserver"), _("Save online"));
    metaserverSaveButton.setTooltipText(_("Save a map version and publish that exact version to the metaserver"));
    metaserverSaveButton.setOnClick([this]() { onSave(true); });
    windowWidget.addWidget( &metaserverSaveButton,
                            Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarMetaserverSaveY),
                            Point(sidebarButtonWidth,SidebarMetaserverHeight));

    setFittingText(metaserverLoadButton, _("Load from metaserver"), _("Load online"));
    metaserverLoadButton.setTooltipText(_("Download a map published on the metaserver and open it here"));
    metaserverLoadButton.setOnClick([this]() { onLoadFromMetaserver(); });
    windowWidget.addWidget( &metaserverLoadButton,
                            Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarMetaserverLoadY),
                            Point(sidebarButtonWidth,SidebarMetaserverHeight));

    // setup terrain mode

    editorModeTerrain_ScrollView.setContent(&editorModeTerrain_VBox);
    editorModeTerrainVBox.addWidget(&editorModeTerrain_ScrollView, 1.0);

    editorModeTerrain_VBox.addWidget(&editorModeTerrain_HBox1);

    editorModeTerrain_Sand.setToggleButton(true);
    editorModeTerrain_Sand.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Sand));
    editorModeTerrain_HBox1.addWidget(&editorModeTerrain_Sand);

    editorModeTerrain_HBox1.addWidget(HSpacer::create(2));

    editorModeTerrain_Dunes.setToggleButton(true);
    editorModeTerrain_Dunes.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Dunes));
    editorModeTerrain_HBox1.addWidget(&editorModeTerrain_Dunes);

    editorModeTerrain_HBox1.addWidget(HSpacer::create(2));

    editorModeTerrain_SpecialBloom.setToggleButton(true);
    editorModeTerrain_SpecialBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_SpecialBloom));
    editorModeTerrain_HBox1.addWidget(&editorModeTerrain_SpecialBloom);

    editorModeTerrain_VBox.addWidget(VSpacer::create(2));
    editorModeTerrain_VBox.addWidget(&editorModeTerrain_HBox2);

    editorModeTerrain_Spice.setToggleButton(true);
    editorModeTerrain_Spice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Spice));
    editorModeTerrain_HBox2.addWidget(&editorModeTerrain_Spice);

    editorModeTerrain_HBox2.addWidget(HSpacer::create(2));

    editorModeTerrain_ThickSpice.setToggleButton(true);
    editorModeTerrain_ThickSpice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_ThickSpice));
    editorModeTerrain_HBox2.addWidget(&editorModeTerrain_ThickSpice);

    editorModeTerrain_HBox2.addWidget(HSpacer::create(2));

    editorModeTerrain_SpiceBloom.setToggleButton(true);
    editorModeTerrain_SpiceBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_SpiceBloom));
    editorModeTerrain_HBox2.addWidget(&editorModeTerrain_SpiceBloom);

    if(tornieContentVisible_) {
        editorModeTerrain_VBox.addWidget(VSpacer::create(2));
        editorModeTerrain_VBox.addWidget(&editorModeTerrain_HBox4);

        editorModeTerrain_GreenSpice.setToggleButton(true);
        editorModeTerrain_GreenSpice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_GreenSpice));
        editorModeTerrain_HBox4.addWidget(&editorModeTerrain_GreenSpice);

        editorModeTerrain_HBox4.addWidget(HSpacer::create(2));

        editorModeTerrain_ThickGreenSpice.setToggleButton(true);
        editorModeTerrain_ThickGreenSpice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_ThickGreenSpice));
        editorModeTerrain_HBox4.addWidget(&editorModeTerrain_ThickGreenSpice);

        editorModeTerrain_HBox4.addWidget(HSpacer::create(2));

        editorModeTerrain_GreenSpiceBloom.setToggleButton(true);
        editorModeTerrain_GreenSpiceBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_GreenSpiceBloom));
        editorModeTerrain_HBox4.addWidget(&editorModeTerrain_GreenSpiceBloom);

        editorModeTerrain_VBox.addWidget(VSpacer::create(2));
        editorModeTerrain_VBox.addWidget(&editorModeTerrain_HBox5);

        editorModeTerrain_RedSpice.setToggleButton(true);
        editorModeTerrain_RedSpice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_RedSpice));
        editorModeTerrain_HBox5.addWidget(&editorModeTerrain_RedSpice);

        editorModeTerrain_HBox5.addWidget(HSpacer::create(2));

        editorModeTerrain_ThickRedSpice.setToggleButton(true);
        editorModeTerrain_ThickRedSpice.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_ThickRedSpice));
        editorModeTerrain_HBox5.addWidget(&editorModeTerrain_ThickRedSpice);

        editorModeTerrain_HBox5.addWidget(HSpacer::create(2));

        editorModeTerrain_RedSpiceBloom.setToggleButton(true);
        editorModeTerrain_RedSpiceBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_RedSpiceBloom));
        editorModeTerrain_HBox5.addWidget(&editorModeTerrain_RedSpiceBloom);
    }

    editorModeTerrain_VBox.addWidget(VSpacer::create(2));
    editorModeTerrain_VBox.addWidget(&editorModeTerrain_HBox3);

    editorModeTerrain_Rock.setToggleButton(true);
    editorModeTerrain_Rock.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Rock));
    editorModeTerrain_HBox3.addWidget(&editorModeTerrain_Rock);

    editorModeTerrain_HBox3.addWidget(HSpacer::create(2));

    editorModeTerrain_Mountain.setToggleButton(true);
    editorModeTerrain_Mountain.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Mountain));
    editorModeTerrain_HBox3.addWidget(&editorModeTerrain_Mountain);

    editorModeTerrain_HBox3.addWidget(Spacer::create());


    editorModeTerrainVBox.addWidget(VSpacer::create(10));


    // setup terrain pen size buttons
    editorModeTerrainVBox.addWidget(&editorModeTerrain_PenHBox, 40);

    editorModeTerrain_Pen1x1.setToggleButton(true);
    editorModeTerrain_Pen1x1.setOnClick(std::bind(&MapEditorInterface::onTerrainPenButton, this, 1));
    editorModeTerrain_PenHBox.addWidget(&editorModeTerrain_Pen1x1);

    editorModeTerrain_PenHBox.addWidget(HSpacer::create(2));

    editorModeTerrain_Pen3x3.setToggleButton(true);
    editorModeTerrain_Pen3x3.setOnClick(std::bind(&MapEditorInterface::onTerrainPenButton, this, 3));
    editorModeTerrain_PenHBox.addWidget(&editorModeTerrain_Pen3x3);

    editorModeTerrain_PenHBox.addWidget(HSpacer::create(2));

    editorModeTerrain_Pen5x5.setToggleButton(true);
    editorModeTerrain_Pen5x5.setOnClick(std::bind(&MapEditorInterface::onTerrainPenButton, this, 5));
    editorModeTerrain_PenHBox.addWidget(&editorModeTerrain_Pen5x5);

    // setup classic terrain buttons
    editorModeClassicTerrain_MainVBox.addWidget(&editorModeClassicTerrain_VBox, sideBar.getSize().x - 17);

    editorModeClassicTerrain_VBox.addWidget(&editorModeClassicTerrain_HBox1);

    editorModeClassicTerrain_SpiceBloom.setToggleButton(true);
    editorModeClassicTerrain_SpiceBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_SpiceBloom));
    editorModeClassicTerrain_HBox1.addWidget(&editorModeClassicTerrain_SpiceBloom);

    editorModeClassicTerrain_HBox1.addWidget(HSpacer::create(2));

    editorModeClassicTerrain_SpecialBloom.setToggleButton(true);
    editorModeClassicTerrain_SpecialBloom.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_SpecialBloom));
    editorModeClassicTerrain_HBox1.addWidget(&editorModeClassicTerrain_SpecialBloom);

    editorModeClassicTerrain_HBox1.addWidget(HSpacer::create(2));

    editorModeClassicTerrain_SpiceField.setToggleButton(true);
    editorModeClassicTerrain_SpiceField.setOnClick(std::bind(&MapEditorInterface::onTerrainButton, this, Terrain_Spice));
    editorModeClassicTerrain_HBox1.addWidget(&editorModeClassicTerrain_SpiceField);

    editorModeClassicTerrain_VBox.addWidget(VSpacer::create(2));
    editorModeClassicTerrain_VBox.addWidget(Spacer::create());
    editorModeClassicTerrain_VBox.addWidget(VSpacer::create(2));
    editorModeClassicTerrain_SetTacticalPos.setText(_("Set starting screen"));
    editorModeClassicTerrain_SetTacticalPos.setOnClick(std::bind(&MapEditorInterface::onSetTacticalPosition, this));
    editorModeClassicTerrain_VBox.addWidget(&editorModeClassicTerrain_SetTacticalPos);

    // setup structures mode
    editorModeStructs_ScrollView.setContent(&editorModeStructs_VBox);
    editorModeStructs_MainVBox.addWidget(&editorModeStructs_ScrollView, 1.0);

    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox1, 2*D2_TILESIZE + 4);

    editorModeStructs_HBox1.addWidget(&editorModeStructs_SmallStruct_VBox);

    editorModeStructs_SmallStruct_VBox.addWidget(&editorModeStructs_SmallStruct_HBox1);

    editorModeStructs_Slab1.setToggleButton(true);
    editorModeStructs_Slab1.setTooltipText(resolveItemName(Structure_Slab1));
    editorModeStructs_Slab1.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Slab1));
    editorModeStructs_SmallStruct_HBox1.addWidget(&editorModeStructs_Slab1);

    editorModeStructs_Wall.setToggleButton(true);
    editorModeStructs_Wall.setTooltipText(resolveItemName(Structure_Wall));
    editorModeStructs_Wall.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Wall));
    editorModeStructs_SmallStruct_HBox1.addWidget(&editorModeStructs_Wall);

    editorModeStructs_SmallStruct_VBox.addWidget(&editorModeStructs_SmallStruct_HBox2);

    editorModeStructs_GunTurret.setToggleButton(true);
    editorModeStructs_GunTurret.setTooltipText(resolveItemName(Structure_GunTurret));
    editorModeStructs_GunTurret.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_GunTurret));
    editorModeStructs_SmallStruct_HBox2.addWidget(&editorModeStructs_GunTurret);

    editorModeStructs_RocketTurret.setToggleButton(true);
    editorModeStructs_RocketTurret.setTooltipText(resolveItemName(Structure_RocketTurret));
    editorModeStructs_RocketTurret.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_RocketTurret));
    editorModeStructs_SmallStruct_HBox2.addWidget(&editorModeStructs_RocketTurret);

    editorModeStructs_HBox1.addWidget(HSpacer::create(2));

    editorModeStructs_ConstructionYard.setToggleButton(true);
    editorModeStructs_ConstructionYard.setTooltipText(resolveItemName(Structure_ConstructionYard));
    editorModeStructs_ConstructionYard.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_ConstructionYard));
    editorModeStructs_HBox1.addWidget(&editorModeStructs_ConstructionYard);

    editorModeStructs_HBox1.addWidget(HSpacer::create(2));

    editorModeStructs_Windtrap.setToggleButton(true);
    editorModeStructs_Windtrap.setTooltipText(resolveItemName(Structure_WindTrap));
    editorModeStructs_Windtrap.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_WindTrap));
    editorModeStructs_HBox1.addWidget(&editorModeStructs_Windtrap);

    editorModeStructs_AdvancedWindTrap.setToggleButton(true);
    editorModeStructs_AdvancedWindTrap.setTooltipText(resolveItemName(Structure_AdvancedWindTrap));
    editorModeStructs_AdvancedWindTrap.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_AdvancedWindTrap));

    // Tornie: Adv Windtrap MK2 variant — restricted house set (Builder=Invalid for Rebels per OOB)
    editorModeStructs_AdvancedWindTrapMK2.setToggleButton(true);
    editorModeStructs_AdvancedWindTrapMK2.setTooltipText(resolveItemName(Structure_AdvancedWindTrapMK2));
    editorModeStructs_AdvancedWindTrapMK2.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_AdvancedWindTrapMK2));

    editorModeStructs_AdvancedWindTrapMK3.setToggleButton(true);
    editorModeStructs_AdvancedWindTrapMK3.setTooltipText(resolveItemName(Structure_AdvancedWindTrapMK3));
    editorModeStructs_AdvancedWindTrapMK3.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_AdvancedWindTrapMK3));

    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox2, 2*D2_TILESIZE + 4);

    editorModeStructs_Radar.setToggleButton(true);
    editorModeStructs_Radar.setTooltipText(resolveItemName(Structure_Radar));
    editorModeStructs_Radar.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Radar));
    editorModeStructs_HBox2.addWidget(&editorModeStructs_Radar);

    editorModeStructs_HBox2.addWidget(HSpacer::create(2));

    editorModeStructs_Silo.setToggleButton(true);
    editorModeStructs_Silo.setTooltipText(resolveItemName(Structure_Silo));
    editorModeStructs_Silo.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Silo));
    editorModeStructs_HBox2.addWidget(&editorModeStructs_Silo);

    editorModeStructs_HBox2.addWidget(HSpacer::create(2));

    editorModeStructs_IX.setToggleButton(true);
    editorModeStructs_IX.setTooltipText(resolveItemName(Structure_IX));
    editorModeStructs_IX.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_IX));
    editorModeStructs_HBox2.addWidget(&editorModeStructs_IX);


    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox3, 2*D2_TILESIZE + 4);

    editorModeStructs_Barracks.setToggleButton(true);
    editorModeStructs_Barracks.setTooltipText(resolveItemName(Structure_Barracks));
    editorModeStructs_Barracks.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Barracks));
    editorModeStructs_HBox3.addWidget(&editorModeStructs_Barracks);

    editorModeStructs_HBox3.addWidget(HSpacer::create(2));

    editorModeStructs_WOR.setToggleButton(true);
    editorModeStructs_WOR.setTooltipText(resolveItemName(Structure_WOR));
    editorModeStructs_WOR.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_WOR));
    editorModeStructs_HBox3.addWidget(&editorModeStructs_WOR);

    editorModeStructs_HBox3.addWidget(HSpacer::create(2));

    // Tornie: Worfinery — WOR + Refinery combo that produces Troopers
    editorModeStructs_Worfinery.setToggleButton(true);
    editorModeStructs_Worfinery.setTooltipText(resolveItemName(Structure_Worfinery));
    editorModeStructs_Worfinery.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Worfinery));

    editorModeStructs_LightFactory.setToggleButton(true);
    editorModeStructs_LightFactory.setTooltipText(resolveItemName(Structure_LightFactory));
    editorModeStructs_LightFactory.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_LightFactory));
    editorModeStructs_HBox3.addWidget(&editorModeStructs_LightFactory);

    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox4, 2*D2_TILESIZE + 4);

    editorModeStructs_Refinery.setToggleButton(true);
    editorModeStructs_Refinery.setTooltipText(resolveItemName(Structure_Refinery));
    editorModeStructs_Refinery.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Refinery));
    editorModeStructs_HBox4.addWidget(&editorModeStructs_Refinery);

    editorModeStructs_HBox4.addWidget(HSpacer::create(2));

    editorModeStructs_HighTechFactory.setToggleButton(true);
    editorModeStructs_HighTechFactory.setTooltipText(resolveItemName(Structure_HighTechFactory));
    editorModeStructs_HighTechFactory.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_HighTechFactory));
    editorModeStructs_HBox4.addWidget(&editorModeStructs_HighTechFactory);


    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox5, 2*D2_TILESIZE + 4);

    editorModeStructs_HeavyFactory.setToggleButton(true);
    editorModeStructs_HeavyFactory.setTooltipText(resolveItemName(Structure_HeavyFactory));
    editorModeStructs_HeavyFactory.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_HeavyFactory));
    editorModeStructs_HBox5.addWidget(&editorModeStructs_HeavyFactory);

    editorModeStructs_HBox5.addWidget(HSpacer::create(2));

    editorModeStructs_RepairYard.setToggleButton(true);
    editorModeStructs_RepairYard.setTooltipText(resolveItemName(Structure_RepairYard));
    editorModeStructs_RepairYard.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_RepairYard));
    editorModeStructs_HBox5.addWidget(&editorModeStructs_RepairYard);


    editorModeStructs_VBox.addWidget(&editorModeStructs_HBox6, 3*D2_TILESIZE + 4);

    editorModeStructs_Starport.setToggleButton(true);
    editorModeStructs_Starport.setTooltipText(resolveItemName(Structure_StarPort));
    editorModeStructs_Starport.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_StarPort));
    editorModeStructs_HBox6.addWidget(&editorModeStructs_Starport);

    editorModeStructs_HBox6.addWidget(HSpacer::create(2));

    editorModeStructs_Palace.setToggleButton(true);
    editorModeStructs_Palace.setTooltipText(resolveItemName(Structure_Palace));
    editorModeStructs_Palace.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Palace));
    editorModeStructs_HBox6.addWidget(&editorModeStructs_Palace);

    editorModeStructs_HBox6.addWidget(HSpacer::create(2));

    // Tornie: Tech Center — Palace-equivalent that spawns 1-3 random vehicles
    // when House IX is unlocked for the owning house. TechLevel 9 (last building).
    editorModeStructs_TechCenter.setToggleButton(true);
    editorModeStructs_TechCenter.setTooltipText(resolveItemName(Structure_TechCenter));
    editorModeStructs_TechCenter.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_TechCenter));

    editorModeStructs_Scoutpost.setToggleButton(true);
    editorModeStructs_Scoutpost.setTooltipText(resolveItemName(Structure_Scoutpost));
    editorModeStructs_Scoutpost.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Scoutpost));
    editorModeStructs_Flamepost.setToggleButton(true);
    editorModeStructs_Flamepost.setTooltipText(resolveItemName(Structure_Flamepost));
    editorModeStructs_Flamepost.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Flamepost));
    editorModeStructs_Chemipost.setToggleButton(true);
    editorModeStructs_Chemipost.setTooltipText(resolveItemName(Structure_Chemipost));
    editorModeStructs_Chemipost.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Chemipost));
    editorModeStructs_LoveFactory.setToggleButton(true);
    editorModeStructs_LoveFactory.setTooltipText(resolveItemName(Structure_LoveFactory));
    editorModeStructs_LoveFactory.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_LoveFactory));
    editorModeStructs_ChaosFactory.setToggleButton(true);
    editorModeStructs_ChaosFactory.setTooltipText(resolveItemName(Structure_ChaosFactory));
    editorModeStructs_ChaosFactory.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_ChaosFactory));

    // City structures use their own rows within the scrollable palette.
    // Wire all buttons before conditionally adding them for city-capable mods.
    editorModeStructs_ZoneResidential.setToggleButton(true);
    editorModeStructs_ZoneResidential.setTooltipText(resolveItemName(Structure_ZoneResidential));
    editorModeStructs_ZoneResidential.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_ZoneResidential));

    editorModeStructs_ZoneCommercial.setToggleButton(true);
    editorModeStructs_ZoneCommercial.setTooltipText(resolveItemName(Structure_ZoneCommercial));
    editorModeStructs_ZoneCommercial.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_ZoneCommercial));

    editorModeStructs_ZoneIndustrial.setToggleButton(true);
    editorModeStructs_ZoneIndustrial.setTooltipText(resolveItemName(Structure_ZoneIndustrial));
    editorModeStructs_ZoneIndustrial.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_ZoneIndustrial));

    editorModeStructs_Road.setToggleButton(true);
    editorModeStructs_Road.setTooltipText(resolveItemName(Structure_Road));
    editorModeStructs_Road.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Road));

    editorModeStructs_NuclearPlant.setToggleButton(true);
    editorModeStructs_NuclearPlant.setTooltipText(resolveItemName(Structure_NuclearPlant));
    editorModeStructs_NuclearPlant.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_NuclearPlant));

    editorModeStructs_PoliceStation.setToggleButton(true);
    editorModeStructs_PoliceStation.setTooltipText(resolveItemName(Structure_PoliceStation));
    editorModeStructs_PoliceStation.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_PoliceStation));

    editorModeStructs_Stadium.setToggleButton(true);
    editorModeStructs_Stadium.setTooltipText(resolveItemName(Structure_Stadium));
    editorModeStructs_Stadium.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Stadium));

    editorModeStructs_Airport.setToggleButton(true);
    editorModeStructs_Airport.setTooltipText(resolveItemName(Structure_Airport));
    editorModeStructs_Airport.setOnClick(std::bind(&MapEditorInterface::onStructButton, this, Structure_Airport));

    cityStructsVisible_ = ModManager::instance().isCityModeActive();
    if(cityStructsVisible_) {
        // Row 1: R/C/I zones (three 2x2-tile buttons).
        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxCityZones, 2*D2_TILESIZE + 4);
        editorModeStructs_HBoxCityZones.addWidget(&editorModeStructs_ZoneResidential);
        editorModeStructs_HBoxCityZones.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxCityZones.addWidget(&editorModeStructs_ZoneCommercial);
        editorModeStructs_HBoxCityZones.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxCityZones.addWidget(&editorModeStructs_ZoneIndustrial);

        // Row 2: Road (1x1) + NuclearPlant (3x3). Three-tile-tall row matches
        // Nuclear; the smaller Road button is vertically centered by HBox.
        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxCityInfra, 3*D2_TILESIZE + 4);
        editorModeStructs_HBoxCityInfra.addWidget(&editorModeStructs_Road);
        editorModeStructs_HBoxCityInfra.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxCityInfra.addWidget(&editorModeStructs_NuclearPlant);

        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxCityCivic, 3*D2_TILESIZE + 5);
        editorModeStructs_HBoxCityCivic.addWidget(&editorModeStructs_PoliceStation);
        editorModeStructs_HBoxCityCivic.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxCityCivic.addWidget(&editorModeStructs_Stadium);

        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxCityAirport, 3*D2_TILESIZE + 5);
        editorModeStructs_HBoxCityAirport.addWidget(&editorModeStructs_Airport);
        editorModeStructs_HBoxCityAirport.addWidget(Spacer::create());
    }

    if(tornieContentVisible_) {
        // Tornie custom structures: two buttons per row keep the sidebar compact,
        // and the small Scoutpost is centered by the resized SymbolButton.
        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxAdvancedWindTrap, 3*D2_TILESIZE + 4);
        editorModeStructs_HBoxAdvancedWindTrap.addWidget(&editorModeStructs_AdvancedWindTrap);
        editorModeStructs_HBoxAdvancedWindTrap.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxAdvancedWindTrap.addWidget(&editorModeStructs_AdvancedWindTrapMK2);

        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxAdvancedWindTrapMK3, 2*D2_TILESIZE + 4);
        editorModeStructs_HBoxAdvancedWindTrapMK3.addWidget(&editorModeStructs_AdvancedWindTrapMK3);
        editorModeStructs_HBoxAdvancedWindTrapMK3.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxAdvancedWindTrapMK3.addWidget(&editorModeStructs_Worfinery);

        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxTechCenter, 2*D2_TILESIZE + 4);
        editorModeStructs_HBoxTechCenter.addWidget(&editorModeStructs_TechCenter);
        editorModeStructs_HBoxTechCenter.addWidget(HSpacer::create(2));
        editorModeStructs_HBoxTechCenter.addWidget(&editorModeStructs_ChaosFactory);

        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxLoveFactory, 3*D2_TILESIZE + 4);
        editorModeStructs_HBoxLoveFactory.addWidget(&editorModeStructs_LoveFactory);
        editorModeStructs_HBoxLoveFactory.addWidget(Spacer::create());

        // Keep the two compact post buttons at the end of the structure list.
        editorModeStructs_VBox.addWidget(&editorModeStructs_HBoxScoutpost, D2_TILESIZE + 4);
        editorModeStructs_HBoxScoutpost.addWidget(&editorModeStructs_Scoutpost);
        editorModeStructs_HBoxScoutpost.addWidget(&editorModeStructs_Flamepost);
        editorModeStructs_HBoxScoutpost.addWidget(&editorModeStructs_Chemipost);
        editorModeStructs_HBoxScoutpost.addWidget(Spacer::create());
    }

    // setup units mode
    editorModeUnits_ScrollView.setContent(&editorModeUnits_VBox);
    editorModeUnits_MainVBox.addWidget(&editorModeUnits_ScrollView, 1.0);

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox1, 2*D2_TILESIZE);

    editorModeUnits_Soldier.setToggleButton(true);
    editorModeUnits_Soldier.setTooltipText(resolveItemName(Unit_Soldier));
    editorModeUnits_Soldier.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Soldier));
    editorModeUnits_HBox1.addWidget(&editorModeUnits_Soldier);

    editorModeUnits_HBox1.addWidget(HSpacer::create(2));

    editorModeUnits_Trooper.setToggleButton(true);
    editorModeUnits_Trooper.setTooltipText(resolveItemName(Unit_Trooper));
    editorModeUnits_Trooper.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Trooper));
    editorModeUnits_HBox1.addWidget(&editorModeUnits_Trooper);

    editorModeUnits_HBox1.addWidget(HSpacer::create(2));

    editorModeUnits_Harvester.setToggleButton(true);
    editorModeUnits_Harvester.setTooltipText(resolveItemName(Unit_Harvester));
    editorModeUnits_Harvester.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Harvester));
    editorModeUnits_HBox1.addWidget(&editorModeUnits_Harvester);

    // Tornie: Rebel Harvester (Harvester + Siege Tank gun overlay). Rebel-only.
    editorModeUnits_RebelHarvester.setToggleButton(true);
    editorModeUnits_RebelHarvester.setTooltipText(resolveItemName(Unit_RebelHarvester));
    editorModeUnits_RebelHarvester.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_RebelHarvester));

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox2, 2*D2_TILESIZE);

    editorModeUnits_Infantry.setToggleButton(true);
    editorModeUnits_Infantry.setTooltipText(resolveItemName(Unit_Infantry));
    editorModeUnits_Infantry.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Infantry));
    editorModeUnits_HBox2.addWidget(&editorModeUnits_Infantry);

    editorModeUnits_HBox2.addWidget(HSpacer::create(2));

    editorModeUnits_Troopers.setToggleButton(true);
    editorModeUnits_Troopers.setTooltipText(resolveItemName(Unit_Troopers));
    editorModeUnits_Troopers.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Troopers));
    editorModeUnits_HBox2.addWidget(&editorModeUnits_Troopers);

    editorModeUnits_HBox2.addWidget(HSpacer::create(2));

    editorModeUnits_MCV.setToggleButton(true);
    editorModeUnits_MCV.setTooltipText(resolveItemName(Unit_MCV));
    editorModeUnits_MCV.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_MCV));
    editorModeUnits_HBox2.addWidget(&editorModeUnits_MCV);

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox3, 2*D2_TILESIZE);

    editorModeUnits_Trike.setToggleButton(true);
    editorModeUnits_Trike.setTooltipText(resolveItemName(Unit_Trike));
    editorModeUnits_Trike.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Trike));
    editorModeUnits_HBox3.addWidget(&editorModeUnits_Trike);

    editorModeUnits_HBox3.addWidget(HSpacer::create(2));

    editorModeUnits_Raider.setToggleButton(true);
    editorModeUnits_Raider.setTooltipText(resolveItemName(Unit_RaiderTrike));
    editorModeUnits_Raider.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_RaiderTrike));
    editorModeUnits_HBox3.addWidget(&editorModeUnits_Raider);

    editorModeUnits_HBox3.addWidget(HSpacer::create(2));

    editorModeUnits_Quad.setToggleButton(true);
    editorModeUnits_Quad.setTooltipText(resolveItemName(Unit_Quad));
    editorModeUnits_Quad.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Quad));
    editorModeUnits_HBox3.addWidget(&editorModeUnits_Quad);

    editorModeUnits_RocketTrike.setToggleButton(true);
    editorModeUnits_RocketTrike.setTooltipText(resolveItemName(Unit_RocketTrike));
    editorModeUnits_RocketTrike.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_RocketTrike));

    editorModeUnits_SonicTrike.setToggleButton(true);
    editorModeUnits_SonicTrike.setTooltipText(resolveItemName(Unit_SonicTrike));
    editorModeUnits_SonicTrike.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_SonicTrike));

    editorModeUnits_FlameTank.setToggleButton(true);
    editorModeUnits_FlameTank.setTooltipText(resolveItemName(Unit_FlameTank));
    editorModeUnits_FlameTank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_FlameTank));

    editorModeUnits_EliteLauncher.setToggleButton(true);
    editorModeUnits_EliteLauncher.setTooltipText(resolveItemName(Unit_EliteLauncher));
    editorModeUnits_EliteLauncher.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_EliteLauncher));

    editorModeUnits_EliteSiegeTank.setToggleButton(true);
    editorModeUnits_EliteSiegeTank.setTooltipText(resolveItemName(Unit_EliteSiegeTank));
    editorModeUnits_EliteSiegeTank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_EliteSiegeTank));
    editorModeUnits_ChemicalSiegeTank.setToggleButton(true);
    editorModeUnits_ChemicalSiegeTank.setTooltipText(resolveItemName(Unit_ChemicalSiegeTank));
    editorModeUnits_ChemicalSiegeTank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_ChemicalSiegeTank));
    editorModeUnits_ChemicalCarryall.setToggleButton(true);
    editorModeUnits_ChemicalCarryall.setTooltipText(resolveItemName(Unit_ChemicalCarryall));
    editorModeUnits_ChemicalCarryall.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_ChemicalCarryall));

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox4, 2*D2_TILESIZE);

    editorModeUnits_Tank.setToggleButton(true);
    editorModeUnits_Tank.setTooltipText(resolveItemName(Unit_Tank));
    editorModeUnits_Tank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Tank));
    editorModeUnits_HBox4.addWidget(&editorModeUnits_Tank);

    editorModeUnits_HBox4.addWidget(HSpacer::create(2));

    editorModeUnits_SiegeTank.setToggleButton(true);
    editorModeUnits_SiegeTank.setTooltipText(resolveItemName(Unit_SiegeTank));
    editorModeUnits_SiegeTank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_SiegeTank));
    editorModeUnits_HBox4.addWidget(&editorModeUnits_SiegeTank);

    editorModeUnits_HBox4.addWidget(HSpacer::create(2));

    editorModeUnits_Launcher.setToggleButton(true);
    editorModeUnits_Launcher.setTooltipText(resolveItemName(Unit_Launcher));
    editorModeUnits_Launcher.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Launcher));
    editorModeUnits_HBox4.addWidget(&editorModeUnits_Launcher);

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox5, 2*D2_TILESIZE);

    editorModeUnits_Devastator.setToggleButton(true);
    editorModeUnits_Devastator.setTooltipText(resolveItemName(Unit_Devastator));
    editorModeUnits_Devastator.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Devastator));
    editorModeUnits_HBox5.addWidget(&editorModeUnits_Devastator);

    editorModeUnits_HBox5.addWidget(HSpacer::create(2));

    editorModeUnits_SonicTank.setToggleButton(true);
    editorModeUnits_SonicTank.setTooltipText(resolveItemName(Unit_SonicTank));
    editorModeUnits_SonicTank.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_SonicTank));
    editorModeUnits_HBox5.addWidget(&editorModeUnits_SonicTank);

    editorModeUnits_HBox5.addWidget(HSpacer::create(2));

    editorModeUnits_Deviator.setToggleButton(true);
    editorModeUnits_Deviator.setTooltipText(resolveItemName(Unit_Deviator));
    editorModeUnits_Deviator.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Deviator));
    editorModeUnits_HBox5.addWidget(&editorModeUnits_Deviator);

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox6, 2*D2_TILESIZE);

    editorModeUnits_Saboteur.setToggleButton(true);
    editorModeUnits_Saboteur.setTooltipText(resolveItemName(Unit_Saboteur));
    editorModeUnits_Saboteur.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Saboteur));
    editorModeUnits_HBox6.addWidget(&editorModeUnits_Saboteur);

    editorModeUnits_HBox6.addWidget(HSpacer::create(2));

    editorModeUnits_Sandworm.setToggleButton(true);
    editorModeUnits_Sandworm.setTooltipText(resolveItemName(Unit_Sandworm));
    editorModeUnits_Sandworm.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Sandworm));
    editorModeUnits_HBox6.addWidget(&editorModeUnits_Sandworm);

    editorModeUnits_HBox6.addWidget(HSpacer::create(2));

    editorModeUnits_SpecialUnit.setToggleButton(true);
    editorModeUnits_SpecialUnit.setTooltipText("Special");
    editorModeUnits_SpecialUnit.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Special));
    editorModeUnits_HBox6.addWidget(&editorModeUnits_SpecialUnit);

    editorModeUnits_VBox.addWidget(VSpacer::create(2));

    editorModeUnits_VBox.addWidget(&editorModeUnits_HBox7, 2*D2_TILESIZE);

    editorModeUnits_Carryall.setToggleButton(true);
    editorModeUnits_Carryall.setTooltipText(resolveItemName(Unit_Carryall));
    editorModeUnits_Carryall.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Carryall));
    editorModeUnits_HBox7.addWidget(&editorModeUnits_Carryall);

    editorModeUnits_HBox7.addWidget(HSpacer::create(2));

    editorModeUnits_Ornithopter.setToggleButton(true);
    editorModeUnits_Ornithopter.setTooltipText(resolveItemName(Unit_Ornithopter));
    editorModeUnits_Ornithopter.setOnClick(std::bind(&MapEditorInterface::onUnitButton, this, Unit_Ornithopter));
    editorModeUnits_HBox7.addWidget(&editorModeUnits_Ornithopter);

    editorModeUnits_HBox7.addWidget(HSpacer::create(2));

    editorModeUnits_HBox7.addWidget(Spacer::create());

    if(tornieContentVisible_) {
        editorModeUnits_VBox.addWidget(VSpacer::create(2));
        editorModeUnits_VBox.addWidget(&editorModeUnits_HBoxTornie, 2*D2_TILESIZE);
        editorModeUnits_HBoxTornie.addWidget(&editorModeUnits_RebelHarvester);
        editorModeUnits_HBoxTornie.addWidget(HSpacer::create(2));
        editorModeUnits_HBoxTornie.addWidget(&editorModeUnits_RocketTrike);
        editorModeUnits_HBoxTornie.addWidget(HSpacer::create(2));
        editorModeUnits_HBoxTornie.addWidget(&editorModeUnits_FlameTank);

        editorModeUnits_VBox.addWidget(&editorModeUnits_HBoxTornieElite, 2*D2_TILESIZE);
        editorModeUnits_HBoxTornieElite.addWidget(&editorModeUnits_SonicTrike);
        editorModeUnits_HBoxTornieElite.addWidget(HSpacer::create(2));
        editorModeUnits_HBoxTornieElite.addWidget(&editorModeUnits_EliteLauncher);
        editorModeUnits_HBoxTornieElite.addWidget(HSpacer::create(2));
        editorModeUnits_HBoxTornieElite.addWidget(&editorModeUnits_EliteSiegeTank);
        editorModeUnits_VBox.addWidget(&editorModeUnits_HBoxTornieChemical, 2*D2_TILESIZE);
        editorModeUnits_HBoxTornieChemical.addWidget(&editorModeUnits_ChemicalSiegeTank);
        editorModeUnits_HBoxTornieChemical.addWidget(HSpacer::create(2));
        editorModeUnits_HBoxTornieChemical.addWidget(&editorModeUnits_ChemicalCarryall);
        editorModeUnits_HBoxTornieChemical.addWidget(Spacer::create());
    }

    editorModeUnits_MainVBox.addWidget(Spacer::create());



    // bottom bar (structure edit)
    structureDetailsHBox.addWidget(HSpacer::create(4));
    structureDetailsHealthLabel.setTextFontSize(12);
    structureDetailsHealthLabel.setText("Health:");
    structureDetailsHBox.addWidget(&structureDetailsHealthLabel, 0.1);

    for(int i=1;i<=256;i++) {
        structureDetailsHealthDropDownBox.addEntry(std::to_string((i*100)/256) + "% (" + std::to_string(i) + "/256)", i);
    }

    structureDetailsHealthDropDownBox.setOnSelectionChange(std::bind(&MapEditorInterface::onStructureHealthDropDown, this, std::placeholders::_1));

    structureDetailsHBox.addWidget(&structureDetailsHealthDropDownBox, 120);
    structureDetailsHBox.addWidget(Spacer::create(), 1.0);

    // bottom bar (unit edit)
    unitDetailsHBox.addWidget(HSpacer::create(5));
    unitDetailsHealthLabel.setTextFontSize(12);
    unitDetailsHealthLabel.setText(_("Health") + ":");
    unitDetailsHBox.addWidget(&unitDetailsHealthLabel, 0.1);

    for(int i=1;i<=256;i++) {
        unitDetailsHealthDropDownBox.addEntry(std::to_string((i*100)/256) + "% (" + std::to_string(i) + "/256)", i);
    }

    unitDetailsHealthDropDownBox.setOnSelectionChange(std::bind(&MapEditorInterface::onUnitHealthDropDown, this, std::placeholders::_1));

    unitDetailsHBox.addWidget(&unitDetailsHealthDropDownBox, 115);

    unitDetailsHBox.addWidget(HSpacer::create(4));
    unitDetailsAttackModeLabel.setTextFontSize(12);
    unitDetailsAttackModeLabel.setText(_("Attack mode") + ":");
    unitDetailsHBox.addWidget(&unitDetailsAttackModeLabel, 0.1);

    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(GUARD), GUARD);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(AREAGUARD), AREAGUARD);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(AMBUSH), AMBUSH);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(HUNT), HUNT);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(HARVEST), HARVEST);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(SABOTAGE), SABOTAGE);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(STOP), STOP);
    unitDetailsAttackModeDropDownBox.addEntry(getAttackModeNameByMode(HOLD), HOLD);

    unitDetailsAttackModeDropDownBox.setOnSelectionChange(std::bind(&MapEditorInterface::onUnitAttackModeDropDown, this, std::placeholders::_1));

    unitDetailsHBox.addWidget(&unitDetailsAttackModeDropDownBox, 90);

    unitDetailsHBox.addWidget(HSpacer::create(10));

    unitDetailsRotateLeftButton.setTooltipText(_("Rotate left"));
    unitDetailsRotateLeftButton.setOnClick(std::bind(&MapEditorInterface::onSelectedUnitRotateLeft, this));

    unitDetailsHBox.addWidget(&unitDetailsRotateLeftButton, 24);

    unitDetailsHBox.addWidget(HSpacer::create(1));

    unitDetailsRotateRightButton.setTooltipText(_("Rotate right"));
    unitDetailsRotateRightButton.setOnClick(std::bind(&MapEditorInterface::onSelectedUnitRotateRight, this));

    unitDetailsHBox.addWidget(&unitDetailsRotateRightButton, 24);

    unitDetailsHBox.addWidget(Spacer::create(), 1.0);

    onHouseChanges();

    onModeButton(1);

    onTerrainPenButton(1);

    onMirrorModeButton(0);
}

MapEditorInterface::~MapEditorInterface() = default;


void MapEditorInterface::onHouseChanges() {

    int currentSelection = houseDropDownBox.getSelectedEntryIntData();

    houseDropDownBox.clearAllEntries();

    int currentIndex = 0;
    int currentPlayerNum = 1;
    for(const MapEditor::Player& player : pMapEditor->getPlayers()) {
        std::string entryName = player.bActive ? (player.bAnyHouse ? (_("Player") + " " + std::to_string(currentPlayerNum++)) : player.name) : ("(" + player.name + ")");

        houseDropDownBox.addEntry(entryName, player.house);

        if(player.house == currentSelection) {
            houseDropDownBox.setSelectedItem(currentIndex);
        }
        currentIndex++;
    }

    if(currentSelection == -1) {
        houseDropDownBox.setSelectedItem(0);
    }
}

void MapEditorInterface::onNewMap() {
    onModeButton(1);

    onMirrorModeButton(0);

    teamsButton.setVisible( (pMapEditor->getMapVersion() < 2) );
    mirrorModeNoneButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    mirrorModeHorizontalButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    mirrorModeVerticalButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    mirrorModeBothButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    mirrorModePointButton.setVisible( (pMapEditor->getMapVersion() >= 2) );
    editorModeUnits_SpecialUnit.setVisible( (pMapEditor->getMapVersion() >= 2) );
}

void MapEditorInterface::deselectAll() {

    deselectObject();

    onTerrainButton(-1);
    onStructButton(-1);
    onUnitButton(-1);
}

void MapEditorInterface::deselectObject() {
    windowWidget.removeChildWidget(&structureDetailsHBox);
    windowWidget.removeChildWidget(&unitDetailsHBox);
}


void MapEditorInterface::onObjectSelected() {
    windowWidget.removeChildWidget(&structureDetailsHBox);
    windowWidget.removeChildWidget(&unitDetailsHBox);

    MapEditor::Structure* pStructure = pMapEditor->getSelectedStructure();

    if(pStructure != nullptr) {
        windowWidget.addWidget( &structureDetailsHBox,
                                Point(0, getRendererHeight() - bottomBar.getSize().y + 14 + 3),
                                Point(getRendererWidth() - sideBar.getSize().x, 24));

        structureDetailsHealthDropDownBox.setSelectedItem(pStructure->health - 1);

        changeHouseDropDown(pStructure->house);
    }

    MapEditor::Unit* pUnit = pMapEditor->getSelectedUnit();

    if(pUnit != nullptr) {
        windowWidget.addWidget( &unitDetailsHBox,
                                Point(0, getRendererHeight() - bottomBar.getSize().y + 14 + 3),
                                Point(getRendererWidth() - sideBar.getSize().x, 24));

        unitDetailsHealthDropDownBox.setSelectedItem(pUnit->health - 1);
        unitDetailsAttackModeDropDownBox.setSelectedItem(pUnit->attackmode);

        changeHouseDropDown(pUnit->house);
    }
}

void MapEditorInterface::onChildWindowClose(Window* pChildWindow) {
    NewMapWindow* pNewMapWindow = dynamic_cast<NewMapWindow*>(pChildWindow);
    if(pNewMapWindow != nullptr) {
        std::string loadMapFilepath = pNewMapWindow->getLoadMapFilepath();

        if(loadMapFilepath != "") {
            pMapEditor->loadMap(loadMapFilepath);
        } else {
            const MapData& mapdata = pNewMapWindow->getMapData();

            if(mapdata.getSizeX() > 0) {
                pMapEditor->setMap(mapdata, MapInfo(pNewMapWindow->getMapSeed(), pNewMapWindow->getAuthor(), pNewMapWindow->getLicense()));
                onPlayers();
            }
        }
    }

    LoadMapWindow* pLoadMapWindow = dynamic_cast<LoadMapWindow*>(pChildWindow);
    if(pLoadMapWindow != nullptr) {
        std::string loadMapFilepath = pLoadMapWindow->getLoadMapFilepath();

        if(loadMapFilepath != "") {
            pMapEditor->loadMap(loadMapFilepath);
        }
    }

    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        const bool share = shareAfterSave;
        shareAfterSave = false;
        if(!pLoadSaveWindow->getFilename().empty()) {
            bool saved = false;
            try {
                pMapEditor->saveMap(pLoadSaveWindow->getFilename());
                saved = true;
                const INIFile metadata(pLoadSaveWindow->getFilename() + ".workshop.ini");
                const auto revision = Workshop::store().get(metadata.getStringValue("Workshop", "Hash"));
                saveButton.setTooltipText(_("Saved map version ") + std::to_string(revision.version));
                if(share) Workshop::shareRevision(revision);
                else openWindow(MsgBox::create(_("Saved map version ") + std::to_string(revision.version)));
            } catch(const std::exception& error) {
                openWindow(MsgBox::create(std::string(saved ? _("Map saved, but uploading to the metaserver failed: ") : _("Map save failed: ")) + error.what()));
            }
        }
    }

    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
    if(pQstBox != nullptr) {
        // A pending metaserver map means this box asked about discarding unsaved
        // changes before loading it, not about quitting the editor.
        const std::string metaserverMap = pendingMetaserverMapPath;
        pendingMetaserverMapPath.clear();
        if(pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
            if(metaserverMap.empty()) {
                pMapEditor->onQuit();
            } else {
                loadMetaserverMap(metaserverMap);
            }
        }
    }
}


void MapEditorInterface::onNew() {
    openWindow(NewMapWindow::create(house));
}

bool MapEditorInterface::onRadarClick(Coord worldPosition, bool bRightMouseButton, bool bDrag) {
    screenborder->setNewScreenCenter(worldPosition);
    return true;
}


void MapEditorInterface::onQuit() {
    if(pMapEditor->hasChangeSinceLastSave()) {
        QstBox* pQstBox = QstBox::create(_("Do you really want to quit and lose unsaved changes to this map?"), _("Yes"), _("No"));
        pQstBox->setTextColor(color);
        openWindow(pQstBox);
    } else {
        pMapEditor->onQuit();
    }
}

void MapEditorInterface::onSave(bool share) {
    shareAfterSave = share;

    std::vector<std::string> mapDirectories;
    std::vector<std::string> directoryTitles;

    char tmp[FILENAME_MAX];
    fnkdat("maps/singleplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    mapDirectories.emplace_back(tmp);
    directoryTitles.push_back(_("SP Maps"));

    fnkdat("maps/multiplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    mapDirectories.emplace_back(tmp);
    directoryTitles.push_back(_("MP Maps"));

    const std::string& lastSaveName = pMapEditor->getLastSaveName();
    std::string mapname;
    int lastSaveDirectoryIndex = 0;
    if(lastSaveName.empty()) {
        mapname = pMapEditor->generateMapname();
    } else {
        mapname = getBasename(lastSaveName, true);

        std::string pathName = getBasename(getDirname(lastSaveName));

        for(int i = 0; i < (int) mapDirectories.size(); i++) {
            if(getBasename(mapDirectories[i]) == pathName) {
                lastSaveDirectoryIndex = i;
                break;
            }
        }
    }

    openWindow(LoadSaveWindow::create(true, _("Save Map"), mapDirectories, directoryTitles, pMapEditor->getMapVersion() < 2 ? "INI" : "ini", lastSaveDirectoryIndex , mapname, color));
}


void MapEditorInterface::onLoad() {
    openWindow(LoadMapWindow::create(color));
}

void MapEditorInterface::onLoadFromMetaserver() {
    std::string mapPath;
    try {
        mapPath = Workshop::loadMapFromMetaserver();
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(std::string(_("Could not load a map from the metaserver: ")) + error.what()));
        return;
    }

    if(mapPath.empty()) {
        // Nothing was chosen; the current map is untouched.
        return;
    }

    if(pMapEditor->hasChangeSinceLastSave()) {
        pendingMetaserverMapPath = mapPath;
        QstBox* pQstBox = QstBox::create(_("Do you really want to load this map and lose unsaved changes to the current map?"), _("Yes"), _("No"));
        pQstBox->setTextColor(color);
        openWindow(pQstBox);
        return;
    }

    loadMetaserverMap(mapPath);
}

void MapEditorInterface::loadMetaserverMap(const std::string& path) {
    try {
        const INIFile metadata(path + ".workshop.ini");
        const auto revision=Workshop::store().get(metadata.getStringValue("Workshop", "Hash"));
        if(!revision.modHash.empty() && !Workshop::activateModRevision(revision.modHash))
            throw std::runtime_error("Could not load the required mod.");
        effectiveGameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        pMapEditor->loadMap(path);
        pMapEditor->requestInterfaceRefresh();
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(std::string(_("Could not open the map: ")) + error.what()));
    }
}

void MapEditorInterface::onPlayers() {
    openWindow(PlayerSettingsWindow::create(pMapEditor,house));
}

void MapEditorInterface::onMapSettings() {
    openWindow(MapSettingsWindow::create(pMapEditor,house));
}

void MapEditorInterface::onChoam() {
    openWindow(ChoamWindow::create(pMapEditor,house));
}

void MapEditorInterface::onReinforcements() {
    openWindow(ReinforcementsWindow::create(pMapEditor,house));
}

void MapEditorInterface::onTeams() {
    openWindow(TeamsWindow::create(pMapEditor,house));
}

void MapEditorInterface::onUndo() {
    pMapEditor->undoLastOperation();

    currentEditUnitID = INVALID;
    currentEditStructureID = INVALID;

    onObjectSelected();
}

void MapEditorInterface::onRedo() {
    pMapEditor->redoLastOperation();

    currentEditUnitID = INVALID;
    currentEditStructureID = INVALID;

    onObjectSelected();
}

void MapEditorInterface::onHouseDropDownChanged(bool bInteractive) {
    int index = houseDropDownBox.getSelectedEntryIntData();
    if(index < 0) {
        return;
    }

    changeInterfaceColor(static_cast<HOUSETYPE>(index));

    if(bInteractive) {
        pMapEditor->setEditorMode(MapEditor::EditorMode());
        deselectAll();
    }
}


void MapEditorInterface::onModeButton(int button) {
    terrainButton.setToggleState( (button == 1) );
    structuresButton.setToggleState( (button == 2) );
    unitsButton.setToggleState( (button == 3) );

    windowWidget.removeChildWidget(&editorModeTerrainVBox);
    windowWidget.removeChildWidget(&editorModeClassicTerrain_MainVBox);
    windowWidget.removeChildWidget(&editorModeStructs_MainVBox);
    windowWidget.removeChildWidget(&editorModeUnits_MainVBox);

    switch(button) {
        case 1: {
            // add terrain mode
            if(pMapEditor->getMapVersion() < 2) {
                windowWidget.addWidget( &editorModeClassicTerrain_MainVBox,
                                        Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarPickerY),
                                        Point(sideBar.getSize().x - 14,getRendererHeight() - SidebarPickerY));
            } else {
                windowWidget.addWidget( &editorModeTerrainVBox,
                                        Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarPickerY),
                                        Point(sideBar.getSize().x - 14,getRendererHeight() - SidebarPickerY));
            }
        } break;

        case 2: {
            // add structs mode
            windowWidget.addWidget( &editorModeStructs_MainVBox,
                                    Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarPickerY),
                                    Point(sideBar.getSize().x - 14,getRendererHeight() - SidebarPickerY));
        } break;


        case 3: {
            // add units mode
            windowWidget.addWidget( &editorModeUnits_MainVBox,
                                    Point(getRendererWidth() - sideBar.getSize().x + 14, SidebarPickerY),
                                    Point(sideBar.getSize().x - 14,getRendererHeight() - SidebarPickerY));
        } break;

        default: {
            // should never be reached
        } break;
    }

    pMapEditor->setEditorMode(MapEditor::EditorMode());
    deselectAll();
}

void MapEditorInterface::onTerrainButton(int terrainType) {
    currentTerrainType = terrainType;

    editorModeTerrain_Sand.setToggleState( (terrainType == Terrain_Sand) );
    editorModeTerrain_Dunes.setToggleState( (terrainType == Terrain_Dunes) );
    editorModeTerrain_SpecialBloom.setToggleState( (terrainType == Terrain_SpecialBloom) );
    editorModeTerrain_Spice.setToggleState( (terrainType == Terrain_Spice) );
    editorModeTerrain_ThickSpice.setToggleState( (terrainType == Terrain_ThickSpice) );
    editorModeTerrain_GreenSpice.setToggleState( (terrainType == Terrain_GreenSpice) );
    editorModeTerrain_ThickGreenSpice.setToggleState( (terrainType == Terrain_ThickGreenSpice) );
    editorModeTerrain_GreenSpiceBloom.setToggleState( (terrainType == Terrain_GreenSpiceBloom) );
    editorModeTerrain_RedSpice.setToggleState( (terrainType == Terrain_RedSpice) );
    editorModeTerrain_ThickRedSpice.setToggleState( (terrainType == Terrain_ThickRedSpice) );
    editorModeTerrain_RedSpiceBloom.setToggleState( (terrainType == Terrain_RedSpiceBloom) );
    editorModeTerrain_SpiceBloom.setToggleState( (terrainType == Terrain_SpiceBloom) );
    editorModeTerrain_Rock.setToggleState( (terrainType == Terrain_Rock) );
    editorModeTerrain_Mountain.setToggleState( (terrainType == Terrain_Mountain) );

    editorModeClassicTerrain_SpiceBloom.setToggleState( (terrainType == Terrain_SpiceBloom) );
    editorModeClassicTerrain_SpecialBloom.setToggleState( (terrainType == Terrain_SpecialBloom) );
    editorModeClassicTerrain_SpiceField.setToggleState( (terrainType == Terrain_Spice) );

    if(currentTerrainType >= 0) {
        pMapEditor->setEditorMode(MapEditor::EditorMode((TERRAINTYPE) currentTerrainType, currentTerrainPenSize));
    }
}

void MapEditorInterface::onTerrainPenButton(int pensize) {
    currentTerrainPenSize = pensize;

    editorModeTerrain_Pen1x1.setToggleState( (pensize == 1) );
    editorModeTerrain_Pen3x3.setToggleState( (pensize == 3) );
    editorModeTerrain_Pen5x5.setToggleState( (pensize == 5) );

    if(currentTerrainType >= 0) {
        pMapEditor->setEditorMode(MapEditor::EditorMode((TERRAINTYPE) currentTerrainType, currentTerrainPenSize));
    }
}

void MapEditorInterface::onSetTacticalPosition() {
    deselectAll();
    pMapEditor->setEditorMode(MapEditor::EditorMode(true));
}

void MapEditorInterface::onStructButton(int structType) {
    editorModeStructs_Slab1.setToggleState( (structType == Structure_Slab1) );
    editorModeStructs_Wall.setToggleState( (structType == Structure_Wall) );
    editorModeStructs_GunTurret.setToggleState( (structType == Structure_GunTurret) );
    editorModeStructs_RocketTurret.setToggleState( (structType == Structure_RocketTurret) );
    editorModeStructs_ConstructionYard.setToggleState( (structType == Structure_ConstructionYard) );
    editorModeStructs_Windtrap.setToggleState( (structType == Structure_WindTrap) );
    editorModeStructs_AdvancedWindTrap.setToggleState( (structType == Structure_AdvancedWindTrap) );
    editorModeStructs_AdvancedWindTrapMK2.setToggleState( (structType == Structure_AdvancedWindTrapMK2) );
    editorModeStructs_AdvancedWindTrapMK3.setToggleState( (structType == Structure_AdvancedWindTrapMK3) );
    editorModeStructs_Radar.setToggleState( (structType == Structure_Radar) );
    editorModeStructs_Silo.setToggleState( (structType == Structure_Silo) );
    editorModeStructs_IX.setToggleState( (structType == Structure_IX) );
    editorModeStructs_Barracks.setToggleState( (structType == Structure_Barracks) );
    editorModeStructs_WOR.setToggleState( (structType == Structure_WOR) );
    editorModeStructs_Worfinery.setToggleState( (structType == Structure_Worfinery) );
    editorModeStructs_LightFactory.setToggleState( (structType == Structure_LightFactory) );
    editorModeStructs_Refinery.setToggleState( (structType == Structure_Refinery) );
    editorModeStructs_HighTechFactory.setToggleState( (structType == Structure_HighTechFactory) );
    editorModeStructs_HeavyFactory.setToggleState( (structType == Structure_HeavyFactory) );
    editorModeStructs_RepairYard.setToggleState( (structType == Structure_RepairYard) );
    editorModeStructs_Starport.setToggleState( (structType == Structure_StarPort) );
    editorModeStructs_Palace.setToggleState( (structType == Structure_Palace) );
    editorModeStructs_TechCenter.setToggleState( (structType == Structure_TechCenter) );
    editorModeStructs_Scoutpost.setToggleState( (structType == Structure_Scoutpost) );
    editorModeStructs_Flamepost.setToggleState( (structType == Structure_Flamepost) );
    editorModeStructs_Chemipost.setToggleState( (structType == Structure_Chemipost) );
    editorModeStructs_LoveFactory.setToggleState( (structType == Structure_LoveFactory) );
    editorModeStructs_ChaosFactory.setToggleState( (structType == Structure_ChaosFactory) );

    editorModeStructs_ZoneResidential.setToggleState( (structType == Structure_ZoneResidential) );
    editorModeStructs_ZoneCommercial.setToggleState( (structType == Structure_ZoneCommercial) );
    editorModeStructs_ZoneIndustrial.setToggleState( (structType == Structure_ZoneIndustrial) );
    editorModeStructs_Road.setToggleState( (structType == Structure_Road) );
    editorModeStructs_NuclearPlant.setToggleState( (structType == Structure_NuclearPlant) );
    editorModeStructs_Airport.setToggleState( (structType == Structure_Airport) );
    editorModeStructs_Stadium.setToggleState( (structType == Structure_Stadium) );
    editorModeStructs_PoliceStation.setToggleState( (structType == Structure_PoliceStation) );

    if(structType >= 0) {
        HOUSETYPE house = (HOUSETYPE) houseDropDownBox.getSelectedEntryIntData();
        pMapEditor->setEditorMode(MapEditor::EditorMode(house, structType, 256));
    }
}

void MapEditorInterface::onUnitButton(int unitType) {
    editorModeUnits_Soldier.setToggleState( (unitType == Unit_Soldier) );
    editorModeUnits_Trooper.setToggleState( (unitType == Unit_Trooper) );
    editorModeUnits_Harvester.setToggleState( (unitType == Unit_Harvester) );
    editorModeUnits_RebelHarvester.setToggleState( (unitType == Unit_RebelHarvester) );
    editorModeUnits_Infantry.setToggleState( (unitType == Unit_Infantry) );
    editorModeUnits_Troopers.setToggleState( (unitType == Unit_Troopers) );
    editorModeUnits_MCV.setToggleState( (unitType == Unit_MCV) );
    editorModeUnits_Trike.setToggleState( (unitType == Unit_Trike) );
    editorModeUnits_Raider.setToggleState( (unitType == Unit_RaiderTrike) );
    editorModeUnits_Quad.setToggleState( (unitType == Unit_Quad) );
    editorModeUnits_RocketTrike.setToggleState( (unitType == Unit_RocketTrike) );
    editorModeUnits_SonicTrike.setToggleState( (unitType == Unit_SonicTrike) );
    editorModeUnits_FlameTank.setToggleState( (unitType == Unit_FlameTank) );
    editorModeUnits_EliteLauncher.setToggleState( (unitType == Unit_EliteLauncher) );
    editorModeUnits_EliteSiegeTank.setToggleState( (unitType == Unit_EliteSiegeTank) );
    editorModeUnits_ChemicalSiegeTank.setToggleState( (unitType == Unit_ChemicalSiegeTank) );
    editorModeUnits_ChemicalCarryall.setToggleState( (unitType == Unit_ChemicalCarryall) );
    editorModeUnits_Tank.setToggleState( (unitType == Unit_Tank) );
    editorModeUnits_SiegeTank.setToggleState( (unitType == Unit_SiegeTank) );
    editorModeUnits_Launcher.setToggleState( (unitType == Unit_Launcher) );
    editorModeUnits_Devastator.setToggleState( (unitType == Unit_Devastator) );
    editorModeUnits_SonicTank.setToggleState( (unitType == Unit_SonicTank) );
    editorModeUnits_Deviator.setToggleState( (unitType == Unit_Deviator) );
    editorModeUnits_Saboteur.setToggleState( (unitType == Unit_Saboteur) );
    editorModeUnits_Sandworm.setToggleState( (unitType == Unit_Sandworm) );
    editorModeUnits_SpecialUnit.setToggleState( (unitType == Unit_Special) );
    editorModeUnits_Carryall.setToggleState( (unitType == Unit_Carryall) );
    editorModeUnits_Ornithopter.setToggleState( (unitType == Unit_Ornithopter) );

    if(unitType >= 0) {
        HOUSETYPE house = (HOUSETYPE) houseDropDownBox.getSelectedEntryIntData();
        pMapEditor->setEditorMode(MapEditor::EditorMode(house, unitType, 256, 0, AREAGUARD));
    }
}

void MapEditorInterface::onStructureHealthDropDown(bool bInteractive) {

    if(bInteractive) {
        currentEditUnitID = INVALID;

        if(pMapEditor->getSelectedStructureID() != currentEditStructureID) {
            pMapEditor->startOperation();
            currentEditStructureID = pMapEditor->getSelectedStructureID();
        }

        std::vector<int> selectedStructures = pMapEditor->getMirrorStructures(currentEditStructureID);

        for(size_t i = 0; i < selectedStructures.size(); i++) {
            MapEditorEditStructureOperation editStructureOperation(selectedStructures[i], structureDetailsHealthDropDownBox.getSelectedEntryIntData());
            pMapEditor->addUndoOperation(editStructureOperation.perform(pMapEditor));
        }
    }
}

void MapEditorInterface::onUnitHealthDropDown(bool bInteractive) {

    if(bInteractive) {
        currentEditStructureID = INVALID;

        if(pMapEditor->getSelectedUnitID() != currentEditUnitID) {
            pMapEditor->startOperation();
            currentEditUnitID = pMapEditor->getSelectedUnitID();
        }

        std::vector<int> selectedUnits = pMapEditor->getMirrorUnits(currentEditUnitID);

        for(size_t i = 0; i < selectedUnits.size(); i++) {
            MapEditor::Unit* pUnit = pMapEditor->getUnit(selectedUnits[i]);
            MapEditorEditUnitOperation editUnitOperation(pUnit->id, unitDetailsHealthDropDownBox.getSelectedEntryIntData(), pUnit->angle, pUnit->attackmode);
            pMapEditor->addUndoOperation(editUnitOperation.perform(pMapEditor));
        }
    }
}

void MapEditorInterface::onSelectedUnitRotateLeft() {
    onUnitRotateLeft(pMapEditor->getSelectedUnitID());
}

void MapEditorInterface::onUnitRotateLeft(int unitID) {
    currentEditStructureID = INVALID;

    if(unitID != currentEditUnitID) {
        pMapEditor->startOperation();
        currentEditUnitID = unitID;
    }

    std::vector<int> mirrorUnits = pMapEditor->getMirrorUnits(unitID,true);
    for(int i = 0; i < (int) mirrorUnits.size(); i++) {
        if(mirrorUnits[i] == INVALID) {
            continue;
        }

        MapEditor::Unit* pMirrorUnit = pMapEditor->getUnit(mirrorUnits[i]);

        int currentAngle = pMirrorUnit->angle;
        currentAngle = pMapEditor->getMapMirror()->getAngle(currentAngle, i);
        if(pMirrorUnit->itemID == Unit_Soldier || pMirrorUnit->itemID == Unit_Saboteur || pMirrorUnit->itemID == Unit_Trooper || pMirrorUnit->itemID == Unit_Infantry || pMirrorUnit->itemID == Unit_Troopers) {
            currentAngle += 2;
        } else {
            currentAngle++;
        }
        if(currentAngle >= NUM_ANGLES) {
            currentAngle = 0;
        }
        currentAngle = pMapEditor->getMapMirror()->getAngle(currentAngle, i);

        MapEditorEditUnitOperation editUnitOperation(pMirrorUnit->id, pMirrorUnit->health, currentAngle, pMirrorUnit->attackmode);

        pMapEditor->addUndoOperation(editUnitOperation.perform(pMapEditor));

    }
}

void MapEditorInterface::onSelectedUnitRotateRight() {
    onUnitRotateRight(pMapEditor->getSelectedUnitID());
}

void MapEditorInterface::onUnitRotateRight(int unitID) {
    currentEditStructureID = INVALID;

    if(unitID != currentEditUnitID) {
        pMapEditor->startOperation();
        currentEditUnitID = unitID;
    }

    std::vector<int> mirrorUnits = pMapEditor->getMirrorUnits(unitID,true);
    for(int i = 0; i < (int) mirrorUnits.size(); i++) {
        if(mirrorUnits[i] == INVALID) {
            continue;
        }

        MapEditor::Unit* pMirrorUnit = pMapEditor->getUnit(mirrorUnits[i]);

        int currentAngle = pMirrorUnit->angle;
        currentAngle = pMapEditor->getMapMirror()->getAngle(currentAngle, i);
        if(pMirrorUnit->itemID == Unit_Soldier || pMirrorUnit->itemID == Unit_Saboteur || pMirrorUnit->itemID == Unit_Trooper || pMirrorUnit->itemID == Unit_Infantry || pMirrorUnit->itemID == Unit_Troopers) {
            currentAngle -= 2;
        } else {
            currentAngle--;
        }
        if(currentAngle < 0) {
            currentAngle = NUM_ANGLES-1;
        }
        currentAngle = pMapEditor->getMapMirror()->getAngle(currentAngle, i);

        MapEditorEditUnitOperation editUnitOperation(pMirrorUnit->id, pMirrorUnit->health, currentAngle, pMirrorUnit->attackmode);

        pMapEditor->addUndoOperation(editUnitOperation.perform(pMapEditor));

    }
}


void MapEditorInterface::onUnitAttackModeDropDown(bool bInteractive) {

    if(bInteractive) {
        currentEditStructureID = INVALID;

        if(pMapEditor->getSelectedUnitID() != currentEditUnitID) {
            pMapEditor->startOperation();
            currentEditUnitID = pMapEditor->getSelectedUnitID();
        }

        std::vector<int> selectedUnits = pMapEditor->getMirrorUnits(currentEditUnitID);

        for(size_t i = 0; i < selectedUnits.size(); i++) {
            MapEditor::Unit* pUnit = pMapEditor->getUnit(selectedUnits[i]);
            MapEditorEditUnitOperation editUnitOperation(pUnit->id, pUnit->health, pUnit->angle, (ATTACKMODE) unitDetailsAttackModeDropDownBox.getSelectedEntryIntData());
            pMapEditor->addUndoOperation(editUnitOperation.perform(pMapEditor));
        }
    }
}

void MapEditorInterface::changeHouseDropDown(HOUSETYPE newHouse) {

    for(size_t i = 0 ; i < pMapEditor->getPlayers().size() ; i++) {
        if(pMapEditor->getPlayers()[i].house == newHouse) {
            houseDropDownBox.setSelectedItem(i);
            break;
        }
    }
}

void MapEditorInterface::changeInterfaceColor(HOUSETYPE newHouse) {
    house = newHouse;
    color = getHouseInterfaceColor(newHouse);

    terrainButton.setTextColor(color);
    structuresButton.setTextColor(color);
    unitsButton.setTextColor(color);

    houseDropDownBox.setColor(color);

    // top bar
    SDL_Texture* pTopBarTexture = pGFXManager->getUIGraphic(UI_TopBar, newHouse);
    topBar.setTexture(pTopBarTexture);
    // side bar
    SDL_Texture* pSideBarTexture = pGFXManager->getUIGraphic(UI_MapEditor_SideBar, newHouse);
    sideBar.setTexture(pSideBarTexture);

    // bottom bar
    SDL_Texture* pBottomBarTexture = pGFXManager->getUIGraphic(UI_MapEditor_BottomBar, newHouse);
    bottomBar.setTexture(pBottomBarTexture);

    editorModeTerrain_Sand.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Sand, newHouse));
    editorModeTerrain_Dunes.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Dunes, newHouse));
    editorModeTerrain_SpecialBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SpecialBloom, newHouse));
    editorModeTerrain_Spice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Spice, newHouse));
    editorModeTerrain_ThickSpice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ThickSpice, newHouse));
    editorModeTerrain_GreenSpice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_GreenSpice, newHouse));
    editorModeTerrain_ThickGreenSpice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ThickGreenSpice, newHouse));
    editorModeTerrain_GreenSpiceBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_GreenSpiceBloom, newHouse));
    editorModeTerrain_RedSpice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RedSpice, newHouse));
    editorModeTerrain_ThickRedSpice.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ThickRedSpice, newHouse));
    editorModeTerrain_RedSpiceBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RedSpiceBloom, newHouse));
    editorModeTerrain_SpiceBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SpiceBloom, newHouse));
    editorModeTerrain_Rock.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Rock, newHouse));
    editorModeTerrain_Mountain.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Mountain, newHouse));

    editorModeTerrain_Pen1x1.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Pen1x1, newHouse));
    editorModeTerrain_Pen3x3.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Pen3x3, newHouse));
    editorModeTerrain_Pen5x5.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Pen5x5, newHouse));

    editorModeClassicTerrain_SpiceBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SpiceBloom, newHouse));
    editorModeClassicTerrain_SpecialBloom.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SpecialBloom, newHouse));
    editorModeClassicTerrain_SpiceField.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Spice, newHouse));
    editorModeClassicTerrain_SetTacticalPos.setTextColor(color);

    editorModeStructs_Slab1.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Slab1, newHouse));
    editorModeStructs_Wall.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Wall, newHouse));
    editorModeStructs_GunTurret.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_GunTurret, newHouse));
    editorModeStructs_RocketTurret.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RocketTurret, newHouse));
    editorModeStructs_ConstructionYard.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ConstructionYard, newHouse));
    editorModeStructs_Windtrap.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Windtrap, newHouse));
    editorModeStructs_AdvancedWindTrap.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_AdvancedWindTrap, newHouse));
    editorModeStructs_AdvancedWindTrapMK2.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_AdvancedWindTrapMK2, newHouse));
    editorModeStructs_AdvancedWindTrapMK3.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_AdvancedWindTrapMK3, newHouse));
    editorModeStructs_Radar.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Radar, newHouse));
    editorModeStructs_Silo.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Silo, newHouse));
    editorModeStructs_IX.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_IX, newHouse));
    editorModeStructs_Barracks.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Barracks, newHouse));
    editorModeStructs_WOR.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_WOR, newHouse));
    editorModeStructs_Worfinery.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Worfinery, newHouse));
    editorModeStructs_LightFactory.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_LightFactory, newHouse));
    editorModeStructs_Refinery.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Refinery, newHouse));
    editorModeStructs_HighTechFactory.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_HighTechFactory, newHouse));
    editorModeStructs_HeavyFactory.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_HeavyFactory, newHouse));
    editorModeStructs_RepairYard.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RepairYard, newHouse));
    editorModeStructs_Starport.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Starport, newHouse));
    editorModeStructs_Palace.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Palace, newHouse));
    editorModeStructs_TechCenter.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_TechCenter, newHouse));
    editorModeStructs_Scoutpost.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Scoutpost, newHouse));
    editorModeStructs_Scoutpost.setTooltipText(resolveItemName(Structure_Scoutpost));
    editorModeStructs_Flamepost.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Flamepost, newHouse));
    editorModeStructs_Flamepost.setTooltipText(resolveItemName(Structure_Flamepost));
    editorModeStructs_Chemipost.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Chemipost, newHouse));
    editorModeStructs_Chemipost.setTooltipText(resolveItemName(Structure_Chemipost));
    editorModeStructs_LoveFactory.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_LoveFactory, newHouse));
    editorModeStructs_ChaosFactory.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ChaosFactory, newHouse));
    editorModeStructs_ChaosFactory.setTooltipText(resolveItemName(Structure_ChaosFactory));

    editorModeStructs_ZoneResidential.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ZoneResidential, newHouse));
    editorModeStructs_ZoneCommercial.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ZoneCommercial, newHouse));
    editorModeStructs_ZoneIndustrial.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ZoneIndustrial, newHouse));
    editorModeStructs_Road.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Road, newHouse));
    editorModeStructs_NuclearPlant.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_NuclearPlant, newHouse));
    editorModeStructs_Airport.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Airport, newHouse));
    editorModeStructs_Stadium.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Stadium, newHouse));
    editorModeStructs_PoliceStation.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_PoliceStation, newHouse));

    editorModeUnits_Soldier.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Soldier, newHouse));
    editorModeUnits_Trooper.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Trooper, newHouse));
    editorModeUnits_Harvester.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Harvester, newHouse));
    editorModeUnits_RebelHarvester.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RebelHarvester, newHouse));
    editorModeUnits_Infantry.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Infantry, newHouse));
    editorModeUnits_Troopers.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Troopers, newHouse));
    editorModeUnits_MCV.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_MCV, newHouse));
    editorModeUnits_Trike.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Trike, newHouse));
    editorModeUnits_Raider.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Raider, newHouse));
    editorModeUnits_Quad.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Quad, newHouse));
    editorModeUnits_RocketTrike.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RocketTrike, newHouse));
    editorModeUnits_SonicTrike.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SonicTrike, newHouse));
    editorModeUnits_FlameTank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_FlameTank, newHouse));
    editorModeUnits_EliteLauncher.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_EliteLauncher, newHouse));
    editorModeUnits_EliteSiegeTank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_EliteSiegeTank, newHouse));
    editorModeUnits_ChemicalSiegeTank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ChemicalSiegeTank, newHouse));
    editorModeUnits_ChemicalCarryall.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_ChemicalCarryall, newHouse));
    editorModeUnits_Tank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Tank, newHouse));
    editorModeUnits_SiegeTank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SiegeTank, newHouse));
    editorModeUnits_Launcher.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Launcher, newHouse));
    editorModeUnits_Devastator.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Devastator, newHouse));
    editorModeUnits_SonicTank.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SonicTank, newHouse));
    editorModeUnits_Deviator.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Deviator, newHouse));
    editorModeUnits_Saboteur.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Saboteur, newHouse));
    editorModeUnits_Sandworm.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Sandworm, newHouse));
    editorModeUnits_SpecialUnit.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_SpecialUnit, newHouse));
    editorModeUnits_Carryall.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Carryall, newHouse));
    editorModeUnits_Ornithopter.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_Ornithopter, newHouse));


    structureDetailsHealthLabel.setTextColor(color);
    structureDetailsHealthDropDownBox.setColor(color);

    unitDetailsHealthLabel.setTextColor(color);
    unitDetailsHealthDropDownBox.setColor(color);

    unitDetailsAttackModeLabel.setTextColor(color);
    unitDetailsAttackModeDropDownBox.setColor(color);

    unitDetailsRotateLeftButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RotateLeftIcon, newHouse),
                                            pGFXManager->getUIGraphicSurface(UI_MapEditor_RotateLeftHighlightIcon, newHouse));
    unitDetailsRotateRightButton.setSymbol(pGFXManager->getUIGraphicSurface(UI_MapEditor_RotateRightIcon, newHouse),
                                             pGFXManager->getUIGraphicSurface(UI_MapEditor_RotateRightHighlightIcon, newHouse));
}

void MapEditorInterface::onMirrorModeButton(int mode) {
    pMapEditor->setMirrorMode( (MirrorMode) mode);

    mirrorModeNoneButton.setToggleState( (mode == MirrorModeNone) );
    mirrorModeHorizontalButton.setToggleState( (mode == MirrorModeHorizontal) );
    mirrorModeVerticalButton.setToggleState( (mode == MirrorModeVertical) );
    mirrorModeBothButton.setToggleState( (mode == MirrorModeBoth) );
    mirrorModePointButton.setToggleState( (mode == MirrorModePoint) );
}
