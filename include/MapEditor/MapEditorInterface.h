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

#ifndef MAPEDITORINTERFACE_H
#define MAPEDITORINTERFACE_H

#include <MapEditor/MapEditorRadarView.h>

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/StaticContainer.h>
#include <GUI/Spacer.h>
#include <GUI/TextButton.h>
#include <GUI/SymbolButton.h>
#include <GUI/PictureLabel.h>
#include <GUI/DropDownBox.h>
#include <GUI/Label.h>
#include <GUI/ScrollView.h>

#include <DataTypes.h>

#include <string>

class MapEditor;

/// This class represents the map editor interface.
class MapEditorInterface : public Window {
public:
    /// default constructor
    explicit MapEditorInterface(MapEditor* pMapEditor);

    /// destructor
    virtual ~MapEditorInterface();

    void onHouseChanges();

    void onNewMap();

    void deselectAll();

    void deselectObject();

    void onObjectSelected();

    /**
        This method is called, when the child window is about to be closed.
        This child window will be closed after this method returns.
        \param  pChildWindow    The child window that will be closed
    */
    void onChildWindowClose(Window* pChildWindow) override;

    void onNew();

    void onUnitRotateLeft(int unitID);

    void onUnitRotateRight(int unitID);

    void onUndo();

    void onRedo();

    void onQuit();

private:

    /**
        This function is called when the user left clicks on the radar
        \param  worldPosition       position in world coordinates
        \param  bRightMouseButton   true = right mouse button, false = left mouse button
        \param  bDrag               true = the mouse was moved while being pressed, e.g. dragging
        \return true if dragging should start or continue
    */
    bool onRadarClick(Coord worldPosition, bool bRightMouseButton, bool bDrag);

    void onSave();

    void onLoad();

    void onPlayers();

    void onMapSettings();

    void onChoam();

    void onReinforcements();

    void onTeams();

    void onHouseDropDownChanged(bool bInteractive);

    void onModeButton(int button);

    void onTerrainButton(int terrainType);
    void onTerrainPenButton(int pensize);
    void onSetTacticalPosition();
    void onStructButton(int structType);
    void onUnitButton(int unitType);

    void onStructureHealthDropDown(bool bInteractive);
    void onUnitHealthDropDown(bool bInteractive);
    void onUnitAttackModeDropDown(bool bInteractive);
    void onSelectedUnitRotateLeft();
    void onSelectedUnitRotateRight();

    void changeHouseDropDown(HOUSETYPE newHouse);

    void changeInterfaceColor(HOUSETYPE newHouse);

    void onMirrorModeButton(int mode);

private:
    MapEditor*          pMapEditor;

    MapEditorRadarView  radarView;

    int                 currentEditStructureID;
    int                 currentEditUnitID;


    StaticContainer     windowWidget;

    HBox                topBarHBox;
    SymbolButton        exitButton;
    SymbolButton        newButton;
    SymbolButton        loadButton;
    SymbolButton        saveButton;
    SymbolButton        undoButton;
    SymbolButton        redoButton;
    SymbolButton        playersButton;
    SymbolButton        mapSettingsButton;
    SymbolButton        choamButton;
    SymbolButton        reinforcementsButton;
    SymbolButton        teamsButton;
    SymbolButton        mirrorModeNoneButton;
    SymbolButton        mirrorModeHorizontalButton;
    SymbolButton        mirrorModeVerticalButton;
    SymbolButton        mirrorModeBothButton;
    SymbolButton        mirrorModePointButton;
    PictureLabel        topBar;

    PictureLabel        sideBar;

    HBox                editorModeChooserHBox;
    TextButton          terrainButton;
    TextButton          structuresButton;
    TextButton          unitsButton;

    DropDownBox         houseDropDownBox;

    VBox                editorModeTerrainVBox;
    ScrollView          editorModeTerrain_ScrollView;

    VBox                editorModeTerrain_VBox;

    HBox                editorModeTerrain_HBox1;
    HBox                editorModeTerrain_HBox2;
    HBox                editorModeTerrain_HBox3;
    HBox                editorModeTerrain_HBox4;
    HBox                editorModeTerrain_HBox5;

    SymbolButton        editorModeTerrain_Sand;
    SymbolButton        editorModeTerrain_Dunes;
    SymbolButton        editorModeTerrain_SpecialBloom;
    SymbolButton        editorModeTerrain_Spice;
    SymbolButton        editorModeTerrain_ThickSpice;
    SymbolButton        editorModeTerrain_GreenSpice;
    SymbolButton        editorModeTerrain_ThickGreenSpice;
    SymbolButton        editorModeTerrain_GreenSpiceBloom;
    SymbolButton        editorModeTerrain_RedSpice;
    SymbolButton        editorModeTerrain_ThickRedSpice;
    SymbolButton        editorModeTerrain_RedSpiceBloom;
    SymbolButton        editorModeTerrain_SpiceBloom;
    SymbolButton        editorModeTerrain_Rock;
    SymbolButton        editorModeTerrain_Mountain;

    HBox                editorModeTerrain_PenHBox;

    SymbolButton        editorModeTerrain_Pen1x1;
    SymbolButton        editorModeTerrain_Pen3x3;
    SymbolButton        editorModeTerrain_Pen5x5;


    VBox                editorModeClassicTerrain_MainVBox;

    VBox                editorModeClassicTerrain_VBox;

    HBox                editorModeClassicTerrain_HBox1;
    SymbolButton        editorModeClassicTerrain_SpiceBloom;
    SymbolButton        editorModeClassicTerrain_SpecialBloom;
    SymbolButton        editorModeClassicTerrain_SpiceField;
    TextButton          editorModeClassicTerrain_SetTacticalPos;

    VBox                editorModeStructs_MainVBox;
    ScrollView          editorModeStructs_ScrollView;

    VBox                editorModeStructs_VBox;

    VBox                editorModeStructs_SmallStruct_VBox;
    HBox                editorModeStructs_SmallStruct_HBox1;
    SymbolButton        editorModeStructs_Slab1;
    SymbolButton        editorModeStructs_Wall;
    HBox                editorModeStructs_SmallStruct_HBox2;
    SymbolButton        editorModeStructs_GunTurret;
    SymbolButton        editorModeStructs_RocketTurret;
    HBox                editorModeStructs_HBox1;
    SymbolButton        editorModeStructs_ConstructionYard;
    SymbolButton        editorModeStructs_Windtrap;
    HBox                editorModeStructs_HBoxAdvancedWindTrap;
    HBox                editorModeStructs_HBoxAdvancedWindTrapMK2;
    HBox                editorModeStructs_HBoxAdvancedWindTrapMK3;
    SymbolButton        editorModeStructs_AdvancedWindTrap;   ///< Tornie: 3x3 high-output power building
    SymbolButton        editorModeStructs_AdvancedWindTrapMK2; ///< Tornie: 2x3 high-output power building
    SymbolButton        editorModeStructs_AdvancedWindTrapMK3; ///< Tornie: 3x2 high-output power building
    HBox                editorModeStructs_HBox2;
    SymbolButton        editorModeStructs_Radar;
    SymbolButton        editorModeStructs_Silo;
    SymbolButton        editorModeStructs_IX;
    HBox                editorModeStructs_HBox3;
    SymbolButton        editorModeStructs_Barracks;
    SymbolButton        editorModeStructs_WOR;
    HBox                editorModeStructs_HBoxWorfinery;
    SymbolButton        editorModeStructs_Worfinery;            ///< Tornie: WOR + Refinery combo
    SymbolButton        editorModeStructs_LightFactory;
    HBox                editorModeStructs_HBox4;
    SymbolButton        editorModeStructs_Refinery;
    SymbolButton        editorModeStructs_HighTechFactory;
    HBox                editorModeStructs_HBox5;
    SymbolButton        editorModeStructs_HeavyFactory;
    SymbolButton        editorModeStructs_RepairYard;
    HBox                editorModeStructs_HBox6;
    SymbolButton        editorModeStructs_Starport;
    SymbolButton        editorModeStructs_Palace;
    HBox                editorModeStructs_HBoxTechCenter;
    SymbolButton        editorModeStructs_TechCenter;   ///< Tornie: Palace-equivalent that spawns vehicles
    SymbolButton        editorModeStructs_ChaosFactory; ///< Tornie: random factory
    HBox                editorModeStructs_HBoxScoutpost;
    SymbolButton        editorModeStructs_Scoutpost;     ///< Tornie: power/defense/recon post
    SymbolButton        editorModeStructs_Flamepost;     ///< Tornie: flame defense post
    SymbolButton        editorModeStructs_Chemipost;     ///< Tornie: healing defense post
    HBox                editorModeStructs_HBoxLoveFactory;
    SymbolButton        editorModeStructs_LoveFactory;
    // DuneCity: SimCity-style buildings, only added to the picker when the
    // dune city mod is active. Buttons are always declared so the shared
    // setHouse symbol-wiring and onStructButton toggle reset can address
    // them uniformly without further branching.
    // Separate rows keep the 3x3 civic buildings within the scrolling sidebar.
    HBox                editorModeStructs_HBoxCityZones;
    HBox                editorModeStructs_HBoxCityInfra;
    HBox                editorModeStructs_HBoxCityCivic;
    HBox                editorModeStructs_HBoxCityAirport;
    SymbolButton        editorModeStructs_ZoneResidential;
    SymbolButton        editorModeStructs_ZoneCommercial;
    SymbolButton        editorModeStructs_ZoneIndustrial;
    SymbolButton        editorModeStructs_Road;
    SymbolButton        editorModeStructs_NuclearPlant;
    SymbolButton        editorModeStructs_PoliceStation;
    SymbolButton        editorModeStructs_Stadium;
    SymbolButton        editorModeStructs_Airport;
    bool                cityStructsVisible_ = false;
    bool                tornieContentVisible_ = false;


    VBox                editorModeUnits_MainVBox;

    ScrollView          editorModeUnits_ScrollView;
    VBox                editorModeUnits_VBox;

    HBox                editorModeUnits_HBox1;
    SymbolButton        editorModeUnits_Soldier;
    SymbolButton        editorModeUnits_Trooper;
    SymbolButton        editorModeUnits_Harvester;
    SymbolButton        editorModeUnits_RebelHarvester;   ///< Tornie: Rebel-only Harvester
    HBox                editorModeUnits_HBox2;
    SymbolButton        editorModeUnits_Infantry;
    SymbolButton        editorModeUnits_Troopers;
    SymbolButton        editorModeUnits_MCV;
    HBox                editorModeUnits_HBox3;
    SymbolButton        editorModeUnits_Trike;
    SymbolButton        editorModeUnits_Raider;
    SymbolButton        editorModeUnits_Quad;
    HBox                editorModeUnits_HBoxTornie;            ///< Tornie: RocketTrike / FlameTank row
    SymbolButton        editorModeUnits_RocketTrike;
    SymbolButton        editorModeUnits_FlameTank;
    HBox                editorModeUnits_HBoxTornieElite;       ///< Tornie: SonicTrike / EliteLauncher / EliteSiegeTank row
    SymbolButton        editorModeUnits_SonicTrike;
    SymbolButton        editorModeUnits_EliteLauncher;
    SymbolButton        editorModeUnits_EliteSiegeTank;
    HBox                editorModeUnits_HBoxTornieChemical;
    SymbolButton        editorModeUnits_ChemicalSiegeTank;
    SymbolButton        editorModeUnits_ChemicalCarryall;
    HBox                editorModeUnits_HBox4;
    SymbolButton        editorModeUnits_Tank;
    SymbolButton        editorModeUnits_SiegeTank;
    SymbolButton        editorModeUnits_Launcher;
    HBox                editorModeUnits_HBox5;
    SymbolButton        editorModeUnits_Devastator;
    SymbolButton        editorModeUnits_SonicTank;
    SymbolButton        editorModeUnits_Deviator;
    HBox                editorModeUnits_HBox6;
    SymbolButton        editorModeUnits_Saboteur;
    SymbolButton        editorModeUnits_Sandworm;
    SymbolButton        editorModeUnits_SpecialUnit;
    HBox                editorModeUnits_HBox7;
    SymbolButton        editorModeUnits_Carryall;
    SymbolButton        editorModeUnits_Ornithopter;

    PictureLabel        bottomBar;

    HBox                structureDetailsHBox;
    Label               structureDetailsHealthLabel;
    DropDownBox         structureDetailsHealthDropDownBox;

    HBox                unitDetailsHBox;
    Label               unitDetailsHealthLabel;
    DropDownBox         unitDetailsHealthDropDownBox;
    Label               unitDetailsAttackModeLabel;
    DropDownBox         unitDetailsAttackModeDropDownBox;
    SymbolButton        unitDetailsRotateLeftButton;
    SymbolButton        unitDetailsRotateRightButton;

    int                 currentTerrainType;
    int                 currentTerrainPenSize;

    HOUSETYPE           house;
    Uint32              color;
};
#endif // MAPEDITORINTERFACE_H
