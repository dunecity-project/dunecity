#include <mod/Workshop.h>
#include <mod/ModManager.h>
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

#include <MapEditor/MapEditor.h>

#include <MapEditor/MapGenerator.h>
#include <MapEditor/MapMirror.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/LoadSavePNG.h>

#include <INIMap/MapMetadata.h>
#include <dunecity/CityConstants.h>

#include <structures/Wall.h>

#include <misc/FileSystem.h>
#include <misc/draw_util.h>
#include <misc/format.h>
#include <misc/DiscordManager.h>
#include <misc/TouchInput.h>
#include <CursorManager.h>

#include <mod/ModManager.h>

#include <globals.h>
#include <mmath.h>
#include <sand.h>
#include <main.h>
#include <Tile.h>

#include <config.h>
#include <typeinfo>
#include <algorithm>

extern int currentZoomlevel;

namespace {

bool isThinSpiceTerrain(int terrainType) noexcept {
    return terrainType == Terrain_Spice
        || terrainType == Terrain_GreenSpice
        || terrainType == Terrain_RedSpice
        || terrainType == Terrain_PaleLilacSpice
        || terrainType == Terrain_WhiteSpice;
}

bool isThickSpiceTerrain(int terrainType) noexcept {
    return terrainType == Terrain_ThickSpice
        || terrainType == Terrain_ThickGreenSpice
        || terrainType == Terrain_ThickRedSpice
        || terrainType == Terrain_ThickPaleLilacSpice
        || terrainType == Terrain_ThickWhiteSpice;
}

bool isSpiceTerrain(int terrainType) noexcept {
    return isThinSpiceTerrain(terrainType) || isThickSpiceTerrain(terrainType);
}

int getThinSpiceTerrain(int terrainType) noexcept {
    switch(terrainType) {
        case Terrain_GreenSpice:
        case Terrain_ThickGreenSpice: return Terrain_GreenSpice;
        case Terrain_RedSpice:
        case Terrain_ThickRedSpice: return Terrain_RedSpice;
        case Terrain_PaleLilacSpice:
        case Terrain_ThickPaleLilacSpice: return Terrain_PaleLilacSpice;
        case Terrain_WhiteSpice:
        case Terrain_ThickWhiteSpice: return Terrain_WhiteSpice;
        default: return Terrain_Spice;
    }
}

bool isSameSpiceFamily(int terrainType, int familyType) noexcept {
    return isSpiceTerrain(terrainType) && getThinSpiceTerrain(terrainType) == getThinSpiceTerrain(familyType);
}

unsigned int getTerrainObjPic(int terrainType) noexcept {
    switch(terrainType) {
        case Terrain_GreenSpice:
        case Terrain_ThickGreenSpice:
        case Terrain_GreenSpiceBloom:
            return ObjPic_Terrain_GreenSpice;
        case Terrain_RedSpice:
        case Terrain_ThickRedSpice:
        case Terrain_RedSpiceBloom:
            return ObjPic_Terrain_RedSpice;
        case Terrain_PaleLilacSpice:
        case Terrain_ThickPaleLilacSpice:
        case Terrain_PaleLilacSpiceBloom:
            return ObjPic_Terrain_PaleLilacSpice;
        case Terrain_WhiteSpice:
        case Terrain_ThickWhiteSpice:
        case Terrain_WhiteSpiceBloom:
            return ObjPic_Terrain_WhiteSpice;
        default:
            return ObjPic_Terrain;
    }
}

unsigned int getMapEditorStructureUIGraphic(int itemID) noexcept {
    switch(itemID) {
        case Structure_AdvancedWindTrap:    return UI_MapEditor_AdvancedWindTrap;
        case Structure_AdvancedWindTrapMK2: return UI_MapEditor_AdvancedWindTrapMK2;
        case Structure_AdvancedWindTrapMK3: return UI_MapEditor_AdvancedWindTrapMK3;
        case Structure_Worfinery:           return UI_MapEditor_Worfinery;
        case Structure_Doublefinery:        return UI_MapEditor_Doublefinery;
        case Structure_TechCenter:          return UI_MapEditor_TechCenter;
        case Structure_Scoutpost:           return UI_MapEditor_Scoutpost;
        case Structure_Flamepost:           return UI_MapEditor_Flamepost;
        case Structure_Slowpost:           return NUM_UIGRAPHICS;
        case Structure_Chemipost:           return UI_MapEditor_Chemipost;
        case Structure_LoveFactory:         return UI_MapEditor_LoveFactory;
        case Structure_ChaosFactory:        return UI_MapEditor_ChaosFactory;
        case Structure_PoliceStation:       return UI_MapEditor_PoliceStation;
        case Structure_Stadium:             return UI_MapEditor_Stadium;
        case Structure_Airport:             return UI_MapEditor_Airport;
        default:                            return NUM_UIGRAPHICS;
    }
}

} // namespace

// functor for std::find_if
class CoordDistance {
public:
    CoordDistance(Coord centerCoord, int distance)
     : centerCoord(centerCoord), distance(distance) {
    }

    bool operator()(Coord& coord) {
        return distanceFrom(centerCoord, coord) <= distance;
    }

private:
    Coord centerCoord;
    int distance;
};


MapEditor::MapEditor() : pInterface(nullptr) {
    // Update Discord Rich Presence
    DiscordManager::instance().setMapEditor();
    
    bQuitEditor = false;
    scrollDownMode = false;
    scrollLeftMode = false;
    scrollRightMode = false;
    scrollUpMode = false;
    shift = false;

    bChangedSinceLastSave = false;

    bLeftMousePressed = false;
    lastTerrainEditPosX = -1;
    lastTerrainEditPosY = -1;

    selectedUnitID = INVALID;
    selectedStructureID = INVALID;
    selectedMapItemCoord.invalidate();

    currentZoomlevel = settings.video.preferredZoomLevel;

    sideBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_SideBar), HAlign::Right, VAlign::Top);
    topBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_TopBar), HAlign::Left, VAlign::Top);
    bottomBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_MapEditor_BottomBar), HAlign::Left, VAlign::Bottom);

    SDL_Rect gameBoardRect = { 0, topBarPos.h, sideBarPos.x, getRendererHeight() - topBarPos.h - bottomBarPos.h };
    screenborder = new ScreenBorder(gameBoardRect);

    setMap(MapData(128,128,Terrain_Sand), MapInfo());
    setMirrorMode(MirrorModeNone);

    pInterface = std::make_unique<MapEditorInterface>(this);

    pInterface->onNew();

    // Initialize cursor manager
    cursorManager.initialize();
}

MapEditor::~MapEditor() {
    // Clean up cursor manager
    cursorManager.cleanup();
    
    delete screenborder;
    screenborder = nullptr;
}

std::string MapEditor::generateMapname() const {
    int numPlayers = std::count_if( players.begin(),
                                    players.end(),
                                    [](const MapEditor::Player& player) {
                                        return player.bActive;
                                    });

    return std::to_string(numPlayers) + "P - " + std::to_string(map.getSizeX()) + "x" + std::to_string(map.getSizeY()) + " - " + _("New Map");
}

void MapEditor::setMirrorMode(MirrorMode newMirrorMode) {
    currentMirrorMode = newMirrorMode;

    mapMirror = MapMirror::createMapMirror(currentMirrorMode,map.getSizeX(), map.getSizeY());
}

void MapEditor::RunEditor() {
    while(!bQuitEditor) {

        int frameStart = SDL_GetTicks();

        processInput();
        refreshInterfaceIfNeeded();
        drawScreen();

        // VSync is controlled via SDL_HINT_RENDER_VSYNC in main.cpp
        // No software frame limiting needed in map editor
    }
}

void MapEditor::refreshInterfaceIfNeeded() {
    if(!refreshInterface) return;
    refreshInterface=false;
    // Defer destruction until the old interface's input callbacks have returned.
    pInterface=std::make_unique<MapEditorInterface>(this);
    pInterface->onNewMap();
}

void MapEditor::setMap(const MapData& mapdata, const MapInfo& newMapInfo) {
    map = mapdata;
    mapInfo = newMapInfo;

    screenborder->adjustScreenBorderToMapsize(map.getSizeX(),map.getSizeY());

    // reset tools
    selectedUnitID = INVALID;
    selectedStructureID = INVALID;
    selectedMapItemCoord.invalidate();

    if(pInterface != nullptr) {
        pInterface->deselectAll();
    }

    while(!redoOperationStack.empty()) {
        redoOperationStack.pop();
    }

    while(!undoOperationStack.empty()) {
        undoOperationStack.pop();
    }

    // reset other map properties
    loadedINIFile.reset();
    lastSaveName = "";

    spiceBlooms.clear();
    specialBlooms.clear();
    spiceFields.clear();
    choam.clear();
    reinforcements.clear();
    aiteams.clear();
    structures.clear();
    units.clear();
    players.clear();

    // setup default players
    if(getMapVersion() < 2) {
        players.push_back(Player(getHouseNameByNumber(HOUSE_HARKONNEN),HOUSE_HARKONNEN,HOUSE_HARKONNEN,true,false,"Human",25));
        players.push_back(Player(getHouseNameByNumber(HOUSE_ATREIDES),HOUSE_ATREIDES,HOUSE_ATREIDES,true,false,"CPU",25));
        players.push_back(Player(getHouseNameByNumber(HOUSE_ORDOS),HOUSE_ORDOS,HOUSE_ORDOS,true,false,"CPU",25));
        players.push_back(Player(getHouseNameByNumber(HOUSE_FREMEN),HOUSE_FREMEN,HOUSE_FREMEN,false,false,"CPU",25));
        players.emplace_back(getHouseNameByNumber(HOUSE_SARDAUKAR),HOUSE_SARDAUKAR,HOUSE_SARDAUKAR,true,false,"CPU",25);
        players.push_back(Player(getHouseNameByNumber(HOUSE_MERCENARY),HOUSE_MERCENARY,HOUSE_MERCENARY,false,false,"CPU",25));
        players.push_back(Player(getHouseNameByNumber(HOUSE_NEUTRAL),HOUSE_NEUTRAL,HOUSE_NEUTRAL,false,false,"CPU",25));
        players.push_back(Player(getHouseNameByNumber(HOUSE_REBELS),HOUSE_REBELS,HOUSE_REBELS,false,false,"CPU",25));
        if(isHouseAvailable(HOUSE_CUSTOM) || ModManager::instance().isTornieFamilyContentActive()) {
            players.push_back(Player(getHouseNameByNumber(HOUSE_CUSTOM),HOUSE_CUSTOM,HOUSE_CUSTOM,false,false,"CPU",25));
        }
    } else {
        players.push_back(Player(getHouseNameByNumber(HOUSE_HARKONNEN),HOUSE_HARKONNEN,HOUSE_HARKONNEN,true,true,"Team1"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_ATREIDES),HOUSE_ATREIDES,HOUSE_ATREIDES,true,true,"Team2"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_ORDOS),HOUSE_ORDOS,HOUSE_ORDOS,true,true,"Team3"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_FREMEN),HOUSE_FREMEN,HOUSE_FREMEN,false,false,"Team4"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_SARDAUKAR),HOUSE_SARDAUKAR,HOUSE_SARDAUKAR,true,true,"Team5"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_MERCENARY),HOUSE_MERCENARY,HOUSE_MERCENARY,false,false,"Team6"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_NEUTRAL),HOUSE_NEUTRAL,HOUSE_NEUTRAL,false,false,"Team7"));
        players.push_back(Player(getHouseNameByNumber(HOUSE_REBELS),HOUSE_REBELS,HOUSE_REBELS,false,false,"Team8"));
        if(isHouseAvailable(HOUSE_CUSTOM) || ModManager::instance().isTornieFamilyContentActive()) {
            players.push_back(Player(getHouseNameByNumber(HOUSE_CUSTOM),HOUSE_CUSTOM,HOUSE_CUSTOM,false,false,"Team9"));
        }
    }
    if(ModManager::instance().isTornieFamilyContentActive()) {
        for(int h = HOUSE_WILDSPADE; h < NUM_PLAYER_HOUSES; ++h) {
            const auto house = static_cast<HOUSETYPE>(h);
            const auto brain = getMapVersion() < 2 ? "CPU" : "Team" + std::to_string(h + 1);
            players.emplace_back(getHouseNameByNumber(house), house, static_cast<HOUSETYPE>(getDefaultHouseColorSlot(house)),
                                 false, false, brain, 25);
        }
    }

    // setup default choam
    choam[Unit_Carryall] = 2;
    choam[Unit_Harvester] = 4;
    choam[Unit_Launcher] = 5;
    choam[Unit_MCV] = 2;
    choam[Unit_Ornithopter] = 0;  // Disabled by default (can build from factory but not purchase from starport)
    choam[Unit_Quad] = 5;
    choam[Unit_SiegeTank] = 6;
    choam[Unit_Tank] = 6;
    choam[Unit_Trike] = 5;

    if(pInterface != nullptr) {
        pInterface->onNewMap();
        pInterface->onHouseChanges();
    }

    currentEditorMode = EditorMode();

    bChangedSinceLastSave = true;
}

bool MapEditor::isTileBlocked(int x, int y, bool bSlabIsBlocking, bool bUnitsAreBlocking) const {
    for(const Structure& structure : structures) {
        if(!bSlabIsBlocking && ((structure.itemID == Structure_Slab1) || (structure.itemID == Structure_Slab4) || (structure.itemID == Structure_Road)) ) {
            continue;
        }

        Coord structureSize = getStructureSize(structure.itemID);
        Coord position = structure.position;
        if((x >= position.x) && (x < position.x+structureSize.x) && (y >= position.y) && (y < position.y+structureSize.y)) {
            return true;
        }
    }

    if(bUnitsAreBlocking) {
        for(const Unit& unit : units) {
            if((x == unit.position.x) && (y == unit.position.y)) {
                return true;
            }
        }
    }

    return false;
}

std::vector<int> MapEditor::getMirrorStructures(int structureID) {
    std::vector<int> mirrorStructures;

    MapEditor::Structure* pStructure = getStructure(structureID);

    if(pStructure == nullptr) {
        return mirrorStructures;
    }

    Coord structureSize = getStructureSize(pStructure->itemID);

    for(int i=0;i<mapMirror->getSize();i++) {
        Coord position = mapMirror->getCoord( pStructure->position, i, structureSize);

        for(const Structure& structure : structures) {
            if(structure.position == position) {
                mirrorStructures.push_back(structure.id);
                break;
            }
        }
    }

    return mirrorStructures;
}

std::vector<int> MapEditor::getMirrorUnits(int unitID, bool bAddMissingAsInvalid) {
    std::vector<int> mirrorUnits;

    MapEditor::Unit* pUnit = getUnit(unitID);

    if(pUnit == nullptr) {
        return mirrorUnits;
    }

    for(int i=0;i<mapMirror->getSize();i++) {
        Coord position = mapMirror->getCoord( pUnit->position, i);

        for(const Unit& unit : units) {
            if(unit.position == position) {
                mirrorUnits.push_back(unit.id);
                break;
            }
        }

        if(bAddMissingAsInvalid && (mirrorUnits.size() < (unsigned int) (i+1) )) {
            mirrorUnits.push_back(INVALID);
        }
    }

    return mirrorUnits;
}

void MapEditor::setEditorMode(const EditorMode& newEditorMode) {

    if(pInterface != nullptr) {
        pInterface->deselectObject();
    }

    selectedUnitID = INVALID;
    selectedStructureID = INVALID;
    selectedMapItemCoord.invalidate();

    currentEditorMode = newEditorMode;
}

void MapEditor::startOperation() {
    if(undoOperationStack.empty() || !dynamic_cast<MapEditorStartOperation*>( undoOperationStack.top().get() )) {
        addUndoOperation(std::make_unique<MapEditorStartOperation>());
    }
}

void MapEditor::undoLastOperation() {
    if(!undoOperationStack.empty()) {
        redoOperationStack.push(std::make_unique<MapEditorStartOperation>());

        while((!undoOperationStack.empty()) && !dynamic_cast<MapEditorStartOperation*>( undoOperationStack.top().get() )) {
            redoOperationStack.push(undoOperationStack.top()->perform(this));
            undoOperationStack.pop();
        }

        if(!undoOperationStack.empty()) {
            undoOperationStack.pop();
        }
    }
}

void MapEditor::redoLastOperation() {
    if(!redoOperationStack.empty()) {
        undoOperationStack.push(std::make_unique<MapEditorStartOperation>());

        while((!redoOperationStack.empty()) && !dynamic_cast<MapEditorStartOperation*>( redoOperationStack.top().get() )) {
            undoOperationStack.push(redoOperationStack.top()->perform(this));
            redoOperationStack.pop();
        }

        if(!redoOperationStack.empty()) {
            redoOperationStack.pop();
        }
    }
}

void MapEditor::loadMap(const std::string& filepath) {
    // reset tools
    selectedUnitID = INVALID;
    selectedStructureID = INVALID;

    if(pInterface != nullptr) {
        pInterface->deselectAll();
    }

    while(!redoOperationStack.empty()) {
        redoOperationStack.pop();
    }

    while(!undoOperationStack.empty()) {
        undoOperationStack.pop();
    }

    // reset other map properties
    spiceBlooms.clear();
    specialBlooms.clear();
    spiceFields.clear();
    choam.clear();
    reinforcements.clear();
    aiteams.clear();
    structures.clear();
    units.clear();
    players.clear();

    players.emplace_back(getHouseNameByNumber(HOUSE_HARKONNEN),HOUSE_HARKONNEN,HOUSE_HARKONNEN,false,true,"Team1");
    players.push_back(Player(getHouseNameByNumber(HOUSE_ATREIDES),HOUSE_ATREIDES,HOUSE_ATREIDES,false,true,"Team2"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_ORDOS),HOUSE_ORDOS,HOUSE_ORDOS,false,true,"Team3"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_FREMEN),HOUSE_FREMEN,HOUSE_FREMEN,false,false,"Team4"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_SARDAUKAR),HOUSE_SARDAUKAR,HOUSE_SARDAUKAR,false,false,"Team5"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_MERCENARY),HOUSE_MERCENARY,HOUSE_MERCENARY,false,false,"Team6"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_NEUTRAL),HOUSE_NEUTRAL,HOUSE_NEUTRAL,false,false,"Team7"));
    players.push_back(Player(getHouseNameByNumber(HOUSE_REBELS),HOUSE_REBELS,HOUSE_REBELS,false,false,"Team8"));
    if(isHouseAvailable(HOUSE_CUSTOM) || ModManager::instance().isTornieFamilyContentActive()) {
        players.push_back(Player(getHouseNameByNumber(HOUSE_CUSTOM),HOUSE_CUSTOM,HOUSE_CUSTOM,false,false,"Team9"));
    }
    if(ModManager::instance().isTornieFamilyContentActive()) {
        for(int h = HOUSE_WILDSPADE; h < NUM_PLAYER_HOUSES; ++h) {
            const auto house = static_cast<HOUSETYPE>(h);
            players.emplace_back(getHouseNameByNumber(house), house, static_cast<HOUSETYPE>(getDefaultHouseColorSlot(house)),
                                 false, false, "Team" + std::to_string(h + 1));
        }
    }

    // load map
    loadedINIFile = std::make_unique<INIFile>(filepath, false);
    lastSaveName = filepath;

    // do the actual loading
    INIMapEditorLoader INIMapEditorLoader(this, loadedINIFile.get());

    // update interface
    if(pInterface != nullptr) {
        pInterface->onNewMap();
        pInterface->onHouseChanges();
    }

    currentEditorMode = EditorMode();

    bChangedSinceLastSave = false;
}

void MapEditor::saveMap(const std::string& filepath) {
    if(!loadedINIFile) {
        std::string comment = "Created with Dune City " + std::string(VERSION) + " Map Editor.";
        loadedINIFile = std::make_unique<INIFile>(false, comment);
    }

    // Save As creates an independent catalogue item, including converted copies.
    // Ordinary saves retain their identity and revision history.
    bool forkShared = !lastSaveName.empty() && filepath != lastSaveName;
    for (const auto& source : {lastSaveName, filepath}) {
        if(!source.empty() && std::filesystem::exists(source + ".workshop.ini")) {
            INIFile previous(source + ".workshop.ini");
            forkShared = forkShared || previous.getBoolValue("Workshop", "Immutable", false);
        }
    }
    if(forkShared || loadedINIFile->getStringValue("Workshop", "ID", "").empty()) {
        loadedINIFile->removeSection("Workshop");
        loadedINIFile->setStringValue("Workshop", "ID", Workshop::newID());
        loadedINIFile->setIntValue("BASIC", "MapVersion", 0);
    }
    loadedINIFile->setIntValue("BASIC", "MapVersion", loadedINIFile->getIntValue("BASIC", "MapVersion", 0)+1);
    int version = (mapInfo.mapSeed == INVALID) ? 2 : 1;

    if(version > 1) {
        loadedINIFile->setIntValue("BASIC", "Version", version);
    }

    if(!mapInfo.license.empty()) {
        loadedINIFile->setStringValue("BASIC", "License", mapInfo.license);
    }

    if(!mapInfo.author.empty()) {
        loadedINIFile->setStringValue("BASIC", "Author", mapInfo.author);
    }

    if((version > 1) && (mapInfo.techLevel > 0)) {
        loadedINIFile->setIntValue("BASIC", "TechLevel", mapInfo.techLevel);
    }

    loadedINIFile->setIntValue("BASIC", "WinFlags", mapInfo.winFlags);
    loadedINIFile->setIntValue("BASIC", "LoseFlags", mapInfo.loseFlags);

    loadedINIFile->setStringValue("BASIC", "LosePicture", mapInfo.losePicture, false);
    loadedINIFile->setStringValue("BASIC", "WinPicture", mapInfo.winPicture, false);
    loadedINIFile->setStringValue("BASIC", "BriefPicture", mapInfo.briefPicture, false);

    loadedINIFile->setIntValue("BASIC","TimeOut", mapInfo.timeout);

    int logicalSizeX;
    //int logicalSizeY;
    int logicalOffsetX;
    int logicalOffsetY;

    if(version < 2) {
        logicalSizeX = 64;
        //logicalSizeY = 64;

        int mapscale = 0;
        switch(map.getSizeX()) {
            case 21: {
                mapscale = 2;
                logicalOffsetX = logicalOffsetY = 11;
            } break;
            case 32: {
                mapscale = 1;
                logicalOffsetX = logicalOffsetY = 16;
            } break;
            case 62:
            default: {
                mapscale = 0;
                logicalOffsetX = logicalOffsetY = 1;
            } break;

        }

        loadedINIFile->setIntValue("BASIC", "MapScale", mapscale);

        int cursorPos = (logicalOffsetY+mapInfo.cursorPos.y) * logicalSizeX + (logicalOffsetX+mapInfo.cursorPos.x);
        loadedINIFile->setIntValue("BASIC", "CursorPos", cursorPos);
        int tacticalPos = (logicalOffsetY+mapInfo.tacticalPos.y) * logicalSizeX + (logicalOffsetX+mapInfo.tacticalPos.x);
        loadedINIFile->setIntValue("BASIC", "TacticalPos", tacticalPos);

        // field, spice bloom and special bloom
        std::string strSpiceBloom = "";
        for(size_t i=0;i<spiceBlooms.size();++i) {
            if(i>0) {
                strSpiceBloom += ",";
            }

            int position = (logicalOffsetY+spiceBlooms[i].y) * logicalSizeX + (logicalOffsetX+spiceBlooms[i].x);
            strSpiceBloom += std::to_string(position);
        }

        if(!strSpiceBloom.empty()) {
            loadedINIFile->setStringValue("MAP", "Bloom", strSpiceBloom, false);
        } else {
            loadedINIFile->removeKey("MAP", "Bloom");
        }


        std::string strSpecialBloom = "";
        for(size_t i=0;i<specialBlooms.size();++i) {
            if(i>0) {
                strSpecialBloom += ",";
            }

            int position = (logicalOffsetY+specialBlooms[i].y) * logicalSizeX + (logicalOffsetX+specialBlooms[i].x);
            strSpecialBloom += std::to_string(position);
        }

        if(!strSpecialBloom.empty()) {
            loadedINIFile->setStringValue("MAP", "Special", strSpecialBloom, false);
        } else {
            loadedINIFile->removeKey("MAP", "Special");
        }


        std::string strFieldBloom = "";
        for(size_t i=0;i<spiceFields.size();++i) {
            if(i>0) {
                strFieldBloom += ",";
            }

            int position = (logicalOffsetY+spiceFields[i].y) * logicalSizeX + (logicalOffsetX+spiceFields[i].x);
            strFieldBloom += std::to_string(position);
        }

        if(!strFieldBloom.empty()) {
            loadedINIFile->setStringValue("MAP", "Field", strFieldBloom, false);
        } else {
            loadedINIFile->removeKey("MAP", "Field");
        }


        loadedINIFile->setIntValue("MAP", "Seed", mapInfo.mapSeed);
    } else {
        logicalSizeX = map.getSizeX();
        //logicalSizeY = map.getSizeY();
        logicalOffsetX = logicalOffsetY = 0;

        loadedINIFile->clearSection("MAP");
        loadedINIFile->setIntValue("MAP", "SizeX", map.getSizeX());
        loadedINIFile->setIntValue("MAP", "SizeY", map.getSizeY());

        for(int y = 0; y < map.getSizeY(); y++) {
            std::string rowKey = fmt::sprintf("%.3d", y);

            std::string row = "";
            for(int x = 0; x < map.getSizeX(); x++) {
                switch(map(x,y)) {

                    case Terrain_Dunes: {
                        // Sand dunes
                        row += '^';
                    } break;

                    case Terrain_Spice: {
                        // Spice
                        row += '~';
                    } break;

                    case Terrain_ThickSpice: {
                        // Thick spice
                        row += '+';
                    } break;

                    case Terrain_GreenSpice: {
                        // Tornie green spice
                        row += 'g';
                    } break;

                    case Terrain_ThickGreenSpice: {
                        // Tornie thick green spice
                        row += 'G';
                    } break;

                    case Terrain_GreenSpiceBloom: {
                        // Tornie green spice bloom
                        row += 'b';
                    } break;

                    case Terrain_RedSpice: {
                        // Tornie red spice
                        row += 'r';
                    } break;

                    case Terrain_ThickRedSpice: {
                        // Tornie thick red spice
                        row += 'R';
                    } break;

                    case Terrain_RedSpiceBloom: {
                        // Tornie red spice bloom
                        row += 'B';
                    } break;
                    case Terrain_PaleLilacSpice: row += 'l'; break;
                    case Terrain_ThickPaleLilacSpice: row += 'L'; break;
                    case Terrain_PaleLilacSpiceBloom: row += 'i'; break;
                    case Terrain_WhiteSpice: row += 'w'; break;
                    case Terrain_ThickWhiteSpice: row += 'W'; break;
                    case Terrain_WhiteSpiceBloom: row += 'x'; break;

                    case Terrain_Rock: {
                        // Rock
                        row += '%';
                    } break;

                    case Terrain_Mountain: {
                        // Mountain
                        row += '@';
                    } break;

                    case Terrain_SpiceBloom: {
                        // Spice Bloom
                        row += 'O';
                    } break;

                    case Terrain_SpecialBloom: {
                        // Special Bloom
                        row += 'Q';
                    } break;

                    case Terrain_Sand:
                    default: {
                        // Normal sand
                        row += '-';
                    } break;
                }
            }

            loadedINIFile->setStringValue("MAP", rowKey, row, false);
        }
    }


    for(int i=1;i<=getNumAvailableHouses();i++) {
        loadedINIFile->removeSection("player" + std::to_string(i));
    }

    std::string house2housename[NUM_HOUSES];
    int currentAnyHouseNumber = 1;
    int i = 0;
    for(const Player& player : players) {
        if(player.bAnyHouse) {
            house2housename[i] =  "Player" + std::to_string(currentAnyHouseNumber);
        } else {
            house2housename[i] =  player.name;
        }

        if(player.bActive) {
            if(version < 2) {
                loadedINIFile->setIntValue(house2housename[i], "Quota", player.quota);
                loadedINIFile->setIntValue(house2housename[i], "Credits", player.credits);
                loadedINIFile->setStringValue(house2housename[i], "Brain", player.brain, false);
                loadedINIFile->setIntValue(house2housename[i], "MaxUnit", player.maxunit);
            } else {
                if(player.quota > 0) {
                    loadedINIFile->setIntValue(house2housename[i], "Quota", player.quota);
                } else {
                    loadedINIFile->removeKey(house2housename[i], "Quota");
                }
                loadedINIFile->setIntValue(house2housename[i], "Credits", player.credits);
                loadedINIFile->setStringValue(house2housename[i], "Brain", player.brain, false);

                if(player.bAnyHouse) {
                    currentAnyHouseNumber++;
                }
            }

            if(player.bAnyHouse) {
                // remove corresponding house name
                loadedINIFile->removeSection(player.name);
            }

        } else {
            // remove corresponding house name
            loadedINIFile->removeSection(player.name);
        }
        i++;
    }

    // remove players that are leftovers
    for(int i=currentAnyHouseNumber;i<getNumAvailableHouses();i++) {
        loadedINIFile->removeSection("Player" + std::to_string(i));
    }


    if(choam.empty()) {
        loadedINIFile->removeSection("CHOAM");
    } else {
        loadedINIFile->clearSection("CHOAM");

        for(auto& choamEntry : choam) {
            int itemID = choamEntry.first;
            int num = choamEntry.second;

            if(num == 0) {
                num = -1;
            }

            loadedINIFile->setIntValue("CHOAM", getItemNameByID(itemID), num);
        }
    }

    if(aiteams.empty()) {
        loadedINIFile->removeSection("TEAMS");
    } else {
        loadedINIFile->clearSection("TEAMS");

        // we start at 0 for version 1 maps if we have 16 entries to not overflow the table
        int currentIndex = ((getMapVersion() < 2) && (aiteams.size() >= 16)) ? 0 : 1;
        for(const AITeamInfo& aiteamInfo : aiteams) {
            std::string value = house2housename[aiteamInfo.houseID] + "," + getAITeamBehaviorNameByID(aiteamInfo.aiTeamBehavior) + "," + getAITeamTypeNameByID(aiteamInfo.aiTeamType) + "," + std::to_string(aiteamInfo.minUnits) + "," + std::to_string(aiteamInfo.maxUnits);
            loadedINIFile->setStringValue("TEAMS", std::to_string(currentIndex), value, false);
            currentIndex++;
        }
    }


    loadedINIFile->clearSection("UNITS");
    for(const Unit& unit : units) {
        std::string unitKey = fmt::sprintf("ID%.3d", unit.id);

        int position = (logicalOffsetY+unit.position.y) * logicalSizeX + (logicalOffsetX+unit.position.x);

        int angle = (int) unit.angle;

        angle = (((NUM_ANGLES - angle) + 2) % NUM_ANGLES) * 32;

        std::string unitValue = house2housename[unit.house] + "," + getItemNameByID(unit.itemID) + "," + std::to_string(unit.health)
                                + "," + std::to_string(position) + "," + std::to_string(angle) + "," + getAttackModeNameByMode(unit.attackmode);

        loadedINIFile->setStringValue("UNITS", unitKey, unitValue, false);
    }

    loadedINIFile->clearSection("STRUCTURES");
    loadedINIFile->removeSection("WINDTRAPUPGRADES");
    for(const Structure& structure : structures) {
        int position = (logicalOffsetY+structure.position.y) * logicalSizeX + (logicalOffsetX+structure.position.x);

        if((structure.itemID == Structure_Slab1) || (structure.itemID == Structure_Slab4) || (structure.itemID == Structure_Wall) || (structure.itemID == Structure_Road)) {
            std::string structureKey = fmt::sprintf("GEN%.3d", position);

            std::string structureValue = house2housename[structure.house] + "," + getItemNameByID(structure.itemID);

            loadedINIFile->setStringValue("STRUCTURES", structureKey, structureValue, false);

        } else {

            std::string structureKey = fmt::sprintf("ID%.3d", structure.id);

            std::string structureValue = house2housename[structure.house] + "," + getItemNameByID(structure.itemID) + "," + std::to_string(structure.health) + "," + std::to_string(position);

            loadedINIFile->setStringValue("STRUCTURES", structureKey, structureValue, false);
            if(structure.itemID == Structure_WindTrap && structure.powerUpgraded) {
                loadedINIFile->setBoolValue("WINDTRAPUPGRADES", structureKey, true);
            }
        }
    }

    // The category is derived from the buildings just written, so an empty map
    // is vanilla even when it was drawn with the city palette. Only the file
    // name can still speak for a map that places (almost) nothing, so the stem
    // goes in as the classifier's fallback name.
    const std::string filenameStem = getBasename(filepath, true);

    // Save As explicitly renames the map, including the name used to classify
    // sparse city starters. An ordinary save preserves its displayed name.
    if(filepath != lastSaveName || loadedINIFile->getStringValue("BASIC", "Name", "").empty()) {
        loadedINIFile->setStringValue("BASIC", "Name", filenameStem, false);
    }

    const std::string derivedCategory = MapMetadata::inferMod(*loadedINIFile, filenameStem);

    // The exact gameplay dependency is saved separately in the revision manifest.
    loadedINIFile->setStringValue("BASIC", "Mod", derivedCategory);

    if(reinforcements.empty()) {
        loadedINIFile->removeSection("REINFORCEMENTS");
    } else {
        loadedINIFile->clearSection("REINFORCEMENTS");

        // we start at 0 for version 1 maps if we have 16 entries to not overflow the table
        int currentIndex = ((getMapVersion() < 2) && (reinforcements.size() >= 16)) ? 0 : 1;
        for(const ReinforcementInfo& reinforcement : reinforcements) {
            std::string value = house2housename[reinforcement.houseID] + "," + getItemNameByID(reinforcement.unitID) + "," + getDropLocationNameByID(reinforcement.dropLocation) + "," + std::to_string(reinforcement.droptime);
            if(reinforcement.bRepeat) {
                value += ",+";
            }
            loadedINIFile->setStringValue("REINFORCEMENTS", std::to_string(currentIndex), value, false);
            currentIndex++;
        }
    }

    const std::string stagedPath = filepath + ".saving";
    if(!loadedINIFile->saveChangesTo(stagedPath, getMapVersion() < 2))
        throw std::runtime_error("The map could not be saved. Check the destination and free disk space.");
    Workshop::replaceFile(stagedPath, filepath);
    Workshop::saveMap(filepath, ModManager::instance().getActiveModName());

    lastSaveName = filepath;
    bChangedSinceLastSave = false;
}

namespace {

// Vanilla content fits everywhere. Conversion removes content exclusive to
// another gameplay mode even when mixed map classification would prefer city.
bool categoryAccepts(const std::string& target, const std::string& itemCategory) {
    if(itemCategory == MapMetadata::ModVanilla) return true;
    return target == itemCategory;
}

class MapEditorModOperation final : public MapEditorOperation {
    std::string modName;
public:
    explicit MapEditorModOperation(std::string name) : modName(std::move(name)) {}
    std::unique_ptr<MapEditorOperation> perform(MapEditor* editor) override {
        auto& manager=ModManager::instance();
        const auto previous=manager.getActiveModName();
        if(!manager.setActiveMod(modName)) throw std::runtime_error("Could not load the conversion's target mod.");
        effectiveGameOptions=manager.loadEffectiveGameOptions(settings.gameOptions);
        editor->requestInterfaceRefresh();
        return std::make_unique<MapEditorModOperation>(previous);
    }
};

bool isFoundationOnly(int itemID) {
    return (itemID == Structure_Wall) || (itemID == Structure_Slab1) || (itemID == Structure_Slab4);
}

} // anonymous namespace

std::string MapEditor::getItemCategory(int itemID) {
    if((isStructure(itemID) && DuneCity::isCityOnlyStructure(itemID)) || isAmbientUnit(itemID)) {
        return MapMetadata::ModDuneCity;
    }

    if(isTornieExclusiveItem(itemID)) {
        return MapMetadata::ModTornie;
    }

    return MapMetadata::ModVanilla;
}

bool MapEditor::isCityMapName(const std::string& name) {
    const std::string trimmed = MapMetadata::trimmedLower(name);
    return trimmed.find("city")!=std::string::npos || trimmed.find("cities")!=std::string::npos;
}

std::string MapEditor::getEffectiveMapName() const {
    if(loadedINIFile) {
        const std::string storedName = loadedINIFile->getStringValue("BASIC", "Name", "");
        if(!storedName.empty()) {
            return storedName;
        }
    }

    // Until the map is saved the classifier has no name to look at; the file
    // stem the user picks then becomes the fallback.
    return lastSaveName.empty() ? "" : getBasename(lastSaveName, true);
}

int MapEditor::countFunctionalBuildings(const std::vector<int>& ignoredStructureIDs) const {
    int count = 0;

    for(const Structure& structure : structures) {
        if(isFoundationOnly(structure.itemID)) {
            continue;
        }

        if(std::find(ignoredStructureIDs.begin(), ignoredStructureIDs.end(), structure.id) != ignoredStructureIDs.end()) {
            continue;
        }

        count++;
    }

    return count;
}

int MapEditor::getFunctionalBuildingCount() const {
    return countFunctionalBuildings({});
}

int MapEditor::getCityNameBuildingLimit() const {
    const int numPlayers = (int) std::count_if( players.begin(),
                                                players.end(),
                                                [](const MapEditor::Player& player) {
                                                    return player.bActive;
                                                });

    return std::max(4, 2*numPlayers);
}

std::string MapEditor::getBuiltMapType(const std::vector<int>& ignoredStructureIDs) const {
    bool tornie = false;

    for(const Structure& structure : structures) {
        if(std::find(ignoredStructureIDs.begin(), ignoredStructureIDs.end(), structure.id) != ignoredStructureIDs.end()) {
            continue;
        }

        const std::string category = getItemCategory(structure.itemID);

        // City content decides immediately; Tornie content only decides once no
        // city building showed up at all. Units never decide the category.
        if(category == MapMetadata::ModDuneCity) {
            return MapMetadata::ModDuneCity;
        } else if(category == MapMetadata::ModTornie) {
            tornie = true;
        }
    }

    return tornie ? MapMetadata::ModTornie : MapMetadata::ModVanilla;
}

std::string MapEditor::getMapType() const {
    const std::string built = getBuiltMapType({});

    if(built == MapMetadata::ModDuneCity) {
        return built;
    }

    // A map called "City" that is barely built on is a city map by name alone.
    if(built == MapMetadata::ModVanilla && isCityMapName(getEffectiveMapName()) && (getFunctionalBuildingCount() <= getCityNameBuildingLimit())) {
        return MapMetadata::ModDuneCity;
    }

    return built;
}

MapEditor::MapTypeConversion MapEditor::planMapTypeConversion(const std::string& targetType) const {
    MapTypeConversion conversion;
    conversion.target = MapMetadata::canonicalCategory(targetType);

    for(const Structure& structure : structures) {
        if(!categoryAccepts(conversion.target, getItemCategory(structure.itemID))) {
            conversion.removedStructureIDs.push_back(structure.id);
        }
    }

    for(const Unit& unit : units) {
        if(!categoryAccepts(conversion.target, getItemCategory(unit.itemID))) {
            conversion.removedUnitIDs.push_back(unit.id);
        }
    }

    // Deliveries and starport stock of units the target category does not know
    // would hand the player something unbuildable, so they go with them.
    for(size_t i = 0; i < reinforcements.size(); i++) {
        if(!categoryAccepts(conversion.target, getItemCategory(reinforcements[i].unitID))) {
            conversion.removedReinforcementIndices.push_back(i);
        }
    }

    for(const auto& choamEntry : choam) {
        if(!categoryAccepts(conversion.target, getItemCategory(choamEntry.first))) {
            conversion.removedChoamItemIDs.push_back(choamEntry.first);
        }
    }

    const std::string built = getBuiltMapType(conversion.removedStructureIDs);
    if((built == MapMetadata::ModVanilla)
        && isCityMapName(getEffectiveMapName())
        && (countFunctionalBuildings(conversion.removedStructureIDs) <= getCityNameBuildingLimit())) {
        // The name keeps this a city map no matter what is removed.
        conversion.resultingType = MapMetadata::ModDuneCity;
    } else {
        conversion.resultingType = built;
    }

    return conversion;
}

void MapEditor::applyMapTypeConversion(const MapTypeConversion& conversion) {
    const std::string target=conversion.target==MapMetadata::ModTornie?"Tornie":conversion.target;
    // Activate before removing anything: failure leaves the map untouched.
    auto restoreMod=MapEditorModOperation(target).perform(this);
    clearRedoOperations();
    startOperation();
    addUndoOperation(std::move(restoreMod));

    for(int structureID : conversion.removedStructureIDs) {
        MapEditorRemoveStructureOperation removeOperation(structureID);
        addUndoOperation(removeOperation.perform(this));
    }

    for(int unitID : conversion.removedUnitIDs) {
        MapEditorRemoveUnitOperation removeOperation(unitID);
        addUndoOperation(removeOperation.perform(this));
    }

    if(!conversion.removedReinforcementIndices.empty()) {
        const std::vector<size_t>& removed = conversion.removedReinforcementIndices;

        std::vector<ReinforcementInfo> keptReinforcements;
        for(size_t i = 0; i < reinforcements.size(); i++) {
            if(std::find(removed.begin(), removed.end(), i) == removed.end()) {
                keptReinforcements.push_back(reinforcements[i]);
            }
        }

        MapEditorChangeReinforcements changeReinforcementsOperation(keptReinforcements);
        addUndoOperation(changeReinforcementsOperation.perform(this));
    }

    for(int itemID : conversion.removedChoamItemIDs) {
        MapEditorChangeChoam changeChoamOperation(itemID, -1);
        addUndoOperation(changeChoamOperation.perform(this));
    }

    // Whatever was selected or being edited may be gone now.
    selectedUnitID = INVALID;
    selectedStructureID = INVALID;
    selectedMapItemCoord.invalidate();
    currentEditorMode = EditorMode();

    // A conversion is a change like any other: it has to be saved to stick and
    // undoing the operation above puts every discarded object back.
    bChangedSinceLastSave = true;

    // The sidebar still shows the old selection, so rebuild the interface - but
    // only once the callback that asked for this conversion has returned.
    requestInterfaceRefresh();
}

void MapEditor::performMapEdit(int xpos, int ypos, bool bRepeated) {
    switch(currentEditorMode.mode) {
        case EditorMode::EditorMode_Terrain: {
            clearRedoOperations();

            if(!bRepeated) {
                startOperation();
            }

            if(getMapVersion() < 2) {
                // classic map
                if(!bRepeated && map.isInsideMap(xpos, ypos)) {
                    TERRAINTYPE terrainType = currentEditorMode.terrainType;

                    switch(terrainType) {
                        case Terrain_SpiceBloom: {
                            MapEditorTerrainAddSpiceBloomOperation editOperation(xpos, ypos);
                            addUndoOperation(editOperation.perform(this));
                        } break;

                        case Terrain_SpecialBloom: {
                            MapEditorTerrainAddSpecialBloomOperation editOperation(xpos, ypos);
                            addUndoOperation(editOperation.perform(this));
                        } break;

                        case Terrain_Spice: {
                            MapEditorTerrainAddSpiceFieldOperation editOperation(xpos, ypos);
                            addUndoOperation(editOperation.perform(this));
                        } break;

                        default: {
                        } break;
                    }


                }

            } else {
                for(int i=0;i<mapMirror->getSize();i++) {

                    Coord position = mapMirror->getCoord( Coord(xpos, ypos), i);

                    int halfsize = currentEditorMode.pensize/2;
                    for(int y = position.y - halfsize; y <= position.y + halfsize; y++) {
                        for(int x = position.x - halfsize; x <= position.x + halfsize; x++) {
                            if(map.isInsideMap(x, y)) {
                                performTerrainChange(x, y, currentEditorMode.terrainType);
                            }
                        }
                    }

                }
            }

        } break;

        case EditorMode::EditorMode_Structure: {
            if(!bRepeated || currentEditorMode.itemID == Structure_Slab1 || currentEditorMode.itemID == Structure_Wall) {

                Coord structureSize = getStructureSize(currentEditorMode.itemID);

                if(mapMirror->mirroringPossible( Coord(xpos, ypos), structureSize) == false) {
                    return;
                }

                // check if all places are free
                for(int i=0;i<mapMirror->getSize();i++) {
                    Coord position = mapMirror->getCoord( Coord(xpos, ypos), i, structureSize);

                    for(int x = position.x; x < position.x + structureSize.x; x++) {
                        for(int y = position.y; y < position.y + structureSize.y; y++) {
                            if(!map.isInsideMap(x,y) || isTileBlocked(x, y, true, (currentEditorMode.itemID != Structure_Slab1) )) {
                                return;
                            }
                        }
                    }
                }

                clearRedoOperations();

                if(!bRepeated) {
                    startOperation();
                }

                int currentHouse = currentEditorMode.house;
                const int houseSlots = ModManager::instance().isTornieFamilyContentActive() ? NUM_PLAYER_HOUSES : getNumAvailableHouses();
                bool bHouseIsActive = players[currentHouse].bActive;
                for(int i=0;i<mapMirror->getSize();i++) {

                    int nextHouse = HOUSE_INVALID;
                    for(int k = currentHouse; k < currentHouse + houseSlots; k++) {
                        const auto candidate = static_cast<HOUSETYPE>(k % houseSlots);
                        if(ModManager::instance().isTornieFamilyContentActive() && !isHouseAvailable(candidate)) continue;
                        if(players[candidate].bActive == bHouseIsActive) {
                            nextHouse = k;
                            break;
                        }
                    }

                    if(nextHouse != HOUSE_INVALID) {
                        Coord position = mapMirror->getCoord( Coord(xpos, ypos), i, structureSize);

                        MapEditorStructurePlaceOperation placeOperation(position, static_cast<HOUSETYPE>(nextHouse % houseSlots), currentEditorMode.itemID, currentEditorMode.health);

                        addUndoOperation(placeOperation.perform(this));

                        currentHouse = nextHouse + 1;
                    }
                }
            }
        } break;

        case EditorMode::EditorMode_Unit: {
            if(!bRepeated) {

                // first check if all places are free
                for(int i=0;i<mapMirror->getSize();i++) {
                    Coord position = mapMirror->getCoord( Coord(xpos, ypos), i);

                    if(!map.isInsideMap(position.x,position.y) || isTileBlocked(position.x, position.y, false, true)) {
                        return;
                    }
                }

                clearRedoOperations();

                startOperation();


                int currentHouse = currentEditorMode.house;
                const int houseSlots = ModManager::instance().isTornieFamilyContentActive() ? NUM_PLAYER_HOUSES : getNumAvailableHouses();
                bool bHouseIsActive = players[currentHouse].bActive;
                for(int i=0;i<mapMirror->getSize();i++) {

                    int nextHouse = HOUSE_INVALID;
                    for(int k = currentHouse; k < currentHouse + houseSlots; k++) {
                        const auto candidate = static_cast<HOUSETYPE>(k % houseSlots);
                        if(ModManager::instance().isTornieFamilyContentActive() && !isHouseAvailable(candidate)) continue;
                        if(players[candidate].bActive == bHouseIsActive) {
                            nextHouse = k;
                            break;
                        }
                    }

                    if(nextHouse != HOUSE_INVALID) {
                        Coord position = mapMirror->getCoord( Coord(xpos, ypos), i);

                        int angle =  mapMirror->getAngle(currentEditorMode.angle, i);

                        MapEditorUnitPlaceOperation placeOperation(position, static_cast<HOUSETYPE>(nextHouse % houseSlots), currentEditorMode.itemID, currentEditorMode.health, angle, currentEditorMode.attackmode);

                        addUndoOperation(placeOperation.perform(this));
                        currentHouse = nextHouse + 1;
                    }
                }
            }
        } break;

        case EditorMode::EditorMode_TacticalPos: {
            if(!map.isInsideMap(xpos,ypos)) {
                return;
            }

            clearRedoOperations();

            startOperation();

            MapEditorSetTacticalPositionOperation setOperation(xpos,ypos);

            addUndoOperation(setOperation.perform(this));

            setEditorMode(EditorMode());
        } break;

        default: {

        } break;

    }
}

void MapEditor::performTerrainChange(int x, int y, TERRAINTYPE terrainType) {

    MapEditorTerrainEditOperation editOperation(x, y, terrainType);
    addUndoOperation(editOperation.perform(this));

    switch(terrainType) {
        case Terrain_Mountain: {
            if(map.isInsideMap(x-1, y) && (map(x-1,y) != Terrain_Mountain) && (map(x-1,y) != Terrain_Rock))     performTerrainChange(x-1,y,Terrain_Rock);
            if(map.isInsideMap(x, y-1) && (map(x,y-1) != Terrain_Mountain) && (map(x,y-1) != Terrain_Rock))     performTerrainChange(x,y-1,Terrain_Rock);
            if(map.isInsideMap(x+1, y) && (map(x+1,y) != Terrain_Mountain) && (map(x+1,y) != Terrain_Rock))     performTerrainChange(x+1,y,Terrain_Rock);
            if(map.isInsideMap(x, y+1) && (map(x,y+1) != Terrain_Mountain) && (map(x,y+1) != Terrain_Rock))     performTerrainChange(x,y+1,Terrain_Rock);
        } break;

        case Terrain_ThickSpice:
        case Terrain_ThickGreenSpice:
        case Terrain_ThickRedSpice:
        case Terrain_ThickPaleLilacSpice:
        case Terrain_ThickWhiteSpice: {
            const auto thinSpice = static_cast<TERRAINTYPE>(getThinSpiceTerrain(terrainType));
            if(map.isInsideMap(x-1, y) && !isSameSpiceFamily(map(x-1,y), terrainType))     performTerrainChange(x-1,y,thinSpice);
            if(map.isInsideMap(x, y-1) && !isSameSpiceFamily(map(x,y-1), terrainType))     performTerrainChange(x,y-1,thinSpice);
            if(map.isInsideMap(x+1, y) && !isSameSpiceFamily(map(x+1,y), terrainType))     performTerrainChange(x+1,y,thinSpice);
            if(map.isInsideMap(x, y+1) && !isSameSpiceFamily(map(x,y+1), terrainType))     performTerrainChange(x,y+1,thinSpice);
        } break;

        case Terrain_Rock: {
            if(map.isInsideMap(x-1, y) && isThickSpiceTerrain(map(x-1,y)))     performTerrainChange(x-1,y,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x-1,y))));
            if(map.isInsideMap(x, y-1) && isThickSpiceTerrain(map(x,y-1)))     performTerrainChange(x,y-1,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x,y-1))));
            if(map.isInsideMap(x+1, y) && isThickSpiceTerrain(map(x+1,y)))     performTerrainChange(x+1,y,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x+1,y))));
            if(map.isInsideMap(x, y+1) && isThickSpiceTerrain(map(x,y+1)))     performTerrainChange(x,y+1,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x,y+1))));
        } break;

        case Terrain_Spice:
        case Terrain_GreenSpice:
        case Terrain_RedSpice:
        case Terrain_PaleLilacSpice:
        case Terrain_WhiteSpice: {
            if(map.isInsideMap(x-1, y) && (map(x-1,y) == Terrain_Mountain))     performTerrainChange(x-1,y,Terrain_Rock);
            if(map.isInsideMap(x, y-1) && (map(x,y-1) == Terrain_Mountain))     performTerrainChange(x,y-1,Terrain_Rock);
            if(map.isInsideMap(x+1, y) && (map(x+1,y) == Terrain_Mountain))     performTerrainChange(x+1,y,Terrain_Rock);
            if(map.isInsideMap(x, y+1) && (map(x,y+1) == Terrain_Mountain))     performTerrainChange(x,y+1,Terrain_Rock);
        } break;

        case Terrain_Sand:
        case Terrain_Dunes:
        case Terrain_SpiceBloom:
        case Terrain_GreenSpiceBloom:
        case Terrain_RedSpiceBloom:
        case Terrain_PaleLilacSpiceBloom:
        case Terrain_WhiteSpiceBloom:
        case Terrain_SpecialBloom: {
            if(map.isInsideMap(x-1, y) && (map(x-1,y) == Terrain_Mountain))     performTerrainChange(x-1,y,Terrain_Rock);
            if(map.isInsideMap(x, y-1) && (map(x,y-1) == Terrain_Mountain))     performTerrainChange(x,y-1,Terrain_Rock);
            if(map.isInsideMap(x+1, y) && (map(x+1,y) == Terrain_Mountain))     performTerrainChange(x+1,y,Terrain_Rock);
            if(map.isInsideMap(x, y+1) && (map(x,y+1) == Terrain_Mountain))     performTerrainChange(x,y+1,Terrain_Rock);

            if(map.isInsideMap(x-1, y) && isThickSpiceTerrain(map(x-1,y)))     performTerrainChange(x-1,y,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x-1,y))));
            if(map.isInsideMap(x, y-1) && isThickSpiceTerrain(map(x,y-1)))     performTerrainChange(x,y-1,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x,y-1))));
            if(map.isInsideMap(x+1, y) && isThickSpiceTerrain(map(x+1,y)))     performTerrainChange(x+1,y,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x+1,y))));
            if(map.isInsideMap(x, y+1) && isThickSpiceTerrain(map(x,y+1)))     performTerrainChange(x,y+1,static_cast<TERRAINTYPE>(getThinSpiceTerrain(map(x,y+1))));
        } break;

        default: {
        } break;
    }

}

void MapEditor::drawScreen() {
    // clear whole screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    //the actuall map
    drawMap(screenborder, false);

    pInterface->draw(Point(0,0));
    pInterface->drawOverlay(Point(0,0));

    // Cursor
    drawCursor();

    presentWithCursor();
}

void MapEditor::processInput() {
    SDL_Event event;

    while(SDL_PollEvent(&event)) {
        updateCursorVisibilityForInput(event);
        if(TouchInput::translateTouchEvent(event)) {
            continue;
        }

        // first of all update mouse
        if(event.type == SDL_MOUSEMOTION) {
            SDL_MouseMotionEvent* mouse = &event.motion;
            drawnMouseX = std::max(0, std::min(mouse->x, settings.video.width-1));
            drawnMouseY = std::max(0, std::min(mouse->y, settings.video.height-1));
        }

        if(pInterface->hasChildWindow()) {
            pInterface->handleInput(event);
        } else {
            switch (event.type) {
                case SDL_KEYDOWN:
                {
                    switch(event.key.keysym.sym) {

                        case SDLK_RETURN: {
                            if(SDL_GetModState() & KMOD_ALT) {
                                toogleFullscreen();
                            }
                        } break;

                        case SDLK_TAB: {
                            if(SDL_GetModState() & KMOD_ALT) {
                                SDL_MinimizeWindow(window);
                            }
                        } break;

                        case SDLK_F1: {
                            Coord oldCenterCoord = screenborder->getCurrentCenter();
                            currentZoomlevel = 0;
                            screenborder->adjustScreenBorderToMapsize(map.getSizeX(), map.getSizeY());
                            screenborder->setNewScreenCenter(oldCenterCoord);
                        } break;

                        case SDLK_F2: {
                            Coord oldCenterCoord = screenborder->getCurrentCenter();
                            currentZoomlevel = 1;
                            screenborder->adjustScreenBorderToMapsize(map.getSizeX(), map.getSizeY());
                            screenborder->setNewScreenCenter(oldCenterCoord);
                        } break;

                        case SDLK_F3: {
                            Coord oldCenterCoord = screenborder->getCurrentCenter();
                            currentZoomlevel = 2;
                            screenborder->adjustScreenBorderToMapsize(map.getSizeX(), map.getSizeY());
                            screenborder->setNewScreenCenter(oldCenterCoord);
                        } break;

                        case SDLK_p: {
                            if(SDL_GetModState() & KMOD_CTRL) {
                                saveMapshot();
                            }
                        } break;

                        case SDLK_PRINTSCREEN:
                        case SDLK_SYSREQ: {
                            saveMapshot();
                        } break;

                        case SDLK_z: {
                            if(SDL_GetModState() & KMOD_CTRL) {
                                    pInterface->onUndo();
                            }
                        } break;

                        case SDLK_y: {
                            if(SDL_GetModState() & KMOD_CTRL) {
                                    pInterface->onRedo();
                            }
                        } break;

                        default:
                            break;
                    }

                } break;

                case SDL_KEYUP:
                {
                    switch(event.key.keysym.sym) {
                        case SDLK_ESCAPE: {
                            // quiting
                            pInterface->onQuit();
                        } break;

                        case SDLK_DELETE:
                        case SDLK_BACKSPACE: {

                            // check units first
                            if(selectedUnitID != INVALID) {
                                clearRedoOperations();
                                startOperation();

                                std::vector<int> selectedUnits = getMirrorUnits(selectedUnitID);

                                for(size_t i=0;i<selectedUnits.size();i++) {
                                    MapEditorRemoveUnitOperation removeOperation(selectedUnits[i]);
                                    addUndoOperation(removeOperation.perform(this));
                                }
                                selectedUnitID = INVALID;

                                pInterface->deselectAll();
                            } else if(selectedStructureID != INVALID) {
                                // We only try deleting structures if we had not yet deleted a unit (e.g. a unit on concrete)
                                clearRedoOperations();
                                startOperation();

                                std::vector<int> selectedStructures = getMirrorStructures(selectedStructureID);

                                for(size_t i=0;i<selectedStructures.size();i++) {
                                    MapEditorRemoveStructureOperation removeOperation(selectedStructures[i]);
                                    addUndoOperation(removeOperation.perform(this));
                                }
                                selectedStructureID = INVALID;

                                pInterface->deselectAll();
                            } else if(selectedMapItemCoord.isValid()) {
                                std::vector<Coord>::iterator iter = std::find(specialBlooms.begin(), specialBlooms.end(), selectedMapItemCoord);

                                if(iter != specialBlooms.end()) {
                                    clearRedoOperations();
                                    startOperation();
                                    MapEditorTerrainRemoveSpecialBloomOperation removeOperation(iter->x, iter->y);
                                    addUndoOperation(removeOperation.perform(this));

                                    selectedMapItemCoord.invalidate();
                                } else {
                                    iter = std::find(spiceBlooms.begin(), spiceBlooms.end(), selectedMapItemCoord);

                                    if(iter != spiceBlooms.end()) {
                                        clearRedoOperations();
                                        startOperation();
                                        MapEditorTerrainRemoveSpiceBloomOperation removeOperation(iter->x, iter->y);
                                        addUndoOperation(removeOperation.perform(this));

                                        selectedMapItemCoord.invalidate();
                                    } else {
                                        iter = std::find(spiceFields.begin(), spiceFields.end(), selectedMapItemCoord);

                                        if(iter != spiceFields.end()) {
                                            clearRedoOperations();
                                            startOperation();
                                            MapEditorTerrainRemoveSpiceFieldOperation removeOperation(iter->x, iter->y);
                                            addUndoOperation(removeOperation.perform(this));

                                            selectedMapItemCoord.invalidate();
                                        }
                                    }
                                }
                            }

                        } break;

                        default:
                            break;
                    }
                } break;

                case SDL_MOUSEMOTION:
                {
                    pInterface->handleMouseMovement(drawnMouseX,drawnMouseY);

                    if(bLeftMousePressed) {
                        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY) == true) {
                            //if mouse is not over side bar

                            int xpos = screenborder->screen2MapX(drawnMouseX);
                            int ypos = screenborder->screen2MapY(drawnMouseY);

                            if((xpos != lastTerrainEditPosX) || (ypos != lastTerrainEditPosY)) {
                                performMapEdit(xpos, ypos, true);
                            }
                        }
                    }
                } break;

                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y != 0) {
                        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY) == true) {
                            //if mouse is not over side bar
                            int xpos = screenborder->screen2MapX(drawnMouseX);
                            int ypos = screenborder->screen2MapY(drawnMouseY);

                            for(const Unit& unit : units) {
                                Coord position = unit.position;
                                if((position.x == xpos) && (position.y == ypos)) {
                                    if(event.wheel.y > 0) {
                                        pInterface->onUnitRotateLeft(unit.id);
                                    } else {
                                        pInterface->onUnitRotateRight(unit.id);
                                    }
                                    break;
                                }
                            }
                        }

                        pInterface->handleMouseWheel(drawnMouseX,drawnMouseY,(event.wheel.y > 0));
                    }
                } break;

                case SDL_MOUSEBUTTONDOWN:
                {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {
                            if(pInterface->handleMouseLeft(mouse->x, mouse->y, true) == false) {

                                bLeftMousePressed = true;

                                if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                    //if mouse is not over side bar

                                    int xpos = screenborder->screen2MapX(mouse->x);
                                    int ypos = screenborder->screen2MapY(mouse->y);

                                    performMapEdit(xpos, ypos, false);
                                }
                            }

                        } break;

                        case SDL_BUTTON_RIGHT: {
                            if(pInterface->handleMouseRight(mouse->x, mouse->y, true) == false) {

                                if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                    //if mouse is not over side bar
                                    setEditorMode(EditorMode());
                                    pInterface->deselectAll();
                                }
                            }
                        } break;

                    }
                } break;

                case SDL_MOUSEBUTTONUP:
                {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {

                            pInterface->handleMouseLeft(mouse->x, mouse->y, false);

                            if(bLeftMousePressed) {

                                bLeftMousePressed = false;
                                lastTerrainEditPosX = -1;
                                lastTerrainEditPosY = -1;

                                if(currentEditorMode.mode == EditorMode::EditorMode_Selection) {
                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        //if mouse is not over side bar

                                        int xpos = screenborder->screen2MapX(mouse->x);
                                        int ypos = screenborder->screen2MapY(mouse->y);

                                        selectedUnitID = INVALID;
                                        selectedStructureID = INVALID;
                                        selectedMapItemCoord.invalidate();

                                        bool bUnitSelected = false;

                                        for(const Unit& unit : units) {
                                            Coord position = unit.position;

                                            if((position.x == xpos) && (position.y == ypos)) {
                                                selectedUnitID = unit.id;
                                                bUnitSelected = true;
                                                pInterface->onObjectSelected();
                                                mapInfo.cursorPos = position;
                                                break;
                                            }
                                        }

                                        bool bStructureSelected = false;

                                        for(const Structure& structure : structures) {
                                            const Coord& position = structure.position;
                                            Coord structureSize = getStructureSize(structure.itemID);

                                            if(!bUnitSelected && (xpos >= position.x) && (xpos < position.x+structureSize.x) && (ypos >= position.y) && (ypos < position.y+structureSize.y)) {
                                                selectedStructureID = structure.id;
                                                bStructureSelected = true;
                                                pInterface->onObjectSelected();
                                                mapInfo.cursorPos = position;
                                                break;
                                            }
                                        }

                                        if(!bUnitSelected && !bStructureSelected) {
                                            pInterface->deselectAll();

                                            // find map items (spice bloom, special bloom or spice field)
                                            if( (std::find(spiceBlooms.begin(), spiceBlooms.end(), Coord(xpos,ypos)) != spiceBlooms.end())
                                                || (std::find(specialBlooms.begin(), specialBlooms.end(), Coord(xpos,ypos)) != specialBlooms.end())
                                                || (std::find(spiceFields.begin(), spiceFields.end(), Coord(xpos,ypos)) != spiceFields.end())) {
                                                selectedMapItemCoord = Coord(xpos, ypos);
                                            }
                                        }


                                    }
                                }
                            }
                        } break;

                        case SDL_BUTTON_RIGHT: {
                            pInterface->handleMouseRight(mouse->x, mouse->y, false);
                        } break;
                    }
                } break;

                case SDL_QUIT:
                {
                    bQuitEditor = true;
                } break;
            }
        }
    }

    if((pInterface->hasChildWindow() == false) && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS)) {
        const Uint8 *keystate = SDL_GetKeyboardState(nullptr);
        scrollDownMode =  (drawnMouseY >= getRendererHeight()-1-SCROLLBORDER) || keystate[SDL_SCANCODE_DOWN];
        scrollLeftMode = (drawnMouseX <= SCROLLBORDER) || keystate[SDL_SCANCODE_LEFT];
        scrollRightMode = (drawnMouseX >= getRendererWidth()-1-SCROLLBORDER) || keystate[SDL_SCANCODE_RIGHT];
        scrollUpMode = (drawnMouseY <= SCROLLBORDER) || keystate[SDL_SCANCODE_UP];

        if(scrollLeftMode && scrollRightMode) {
            // do nothing
        } else if(scrollLeftMode) {
            scrollLeftMode = screenborder->scrollLeft();
        } else if(scrollRightMode) {
            scrollRightMode = screenborder->scrollRight();
        }

        if(scrollDownMode && scrollUpMode) {
            // do nothing
        } else if(scrollDownMode) {
            scrollDownMode = screenborder->scrollDown();
        } else if(scrollUpMode) {
            scrollUpMode = screenborder->scrollUp();
        }
    } else {
        scrollDownMode = false;
        scrollLeftMode = false;
        scrollRightMode = false;
        scrollUpMode = false;
    }
}

void MapEditor::drawCursor() {
    // Map editor always uses normal cursor
    if (cursorManager.isInitialized()) {
        cursorManager.setCursorMode(Game::CursorMode_Normal);
    }
}

TERRAINTYPE MapEditor::getTerrain(int x, int y) {
    TERRAINTYPE terrainType = map(x,y);

    if(map(x,y) == Terrain_Sand) {
        if(std::find(spiceFields.begin(), spiceFields.end(), Coord(x,y)) != spiceFields.end()) {
            terrainType = Terrain_ThickSpice;
        } else if(std::find_if(spiceFields.begin(), spiceFields.end(), CoordDistance(Coord(x,y),5)) != spiceFields.end()) {
            terrainType = Terrain_Spice;
        }
    }

    // check for classic map items (spice blooms, special blooms)
    if(std::find(spiceBlooms.begin(), spiceBlooms.end(), Coord(x,y)) != spiceBlooms.end()) {
        terrainType = Terrain_SpiceBloom;
    }

    if(std::find(specialBlooms.begin(), specialBlooms.end(), Coord(x,y)) != specialBlooms.end()) {
        terrainType = Terrain_SpecialBloom;
    }

    return terrainType;
}

void MapEditor::drawMap(ScreenBorder* pScreenborder, bool bCompleteMap) {
    int zoomedTilesize = world2zoomedWorld(TILESIZE);

    Coord TopLeftTile = pScreenborder->getTopLeftTile();
    Coord BottomRightTile = pScreenborder->getBottomRightTile();

    // extend the view a little bit to avoid graphical glitches
    TopLeftTile.x = std::max(0, TopLeftTile.x - 1);
    TopLeftTile.y = std::max(0, TopLeftTile.y - 1);
    BottomRightTile.x = std::min(map.getSizeX()-1, BottomRightTile.x + 1);
    BottomRightTile.y = std::min(map.getSizeY()-1, BottomRightTile.y + 1);

    // Load Terrain Surface
    SDL_Texture* TerrainSprite = pGFXManager->getZoomedObjPic(ObjPic_Terrain, currentZoomlevel);

    /* draw ground */
    for(int y = TopLeftTile.y; y <= BottomRightTile.y; y++) {
        for(int x = TopLeftTile.x; x <= BottomRightTile.x; x++) {

            int tile;

            const TERRAINTYPE terrainType = getTerrain(x,y);

            switch(terrainType) {
                case Terrain_Slab: {
                    tile = Tile::TerrainTile_Slab;
                } break;

                case Terrain_Sand: {
                    tile = Tile::TerrainTile_Sand;
                } break;

                case Terrain_Rock: {
                    //determine which surounding tiles are rock
                    bool up = (y-1 < 0) || (getTerrain(x, y-1) == Terrain_Rock) || (getTerrain(x, y-1) == Terrain_Slab) || (getTerrain(x, y-1) == Terrain_Mountain);
                    bool right = (x+1 >= map.getSizeX()) || (getTerrain(x+1, y) == Terrain_Rock) || (getTerrain(x+1, y) == Terrain_Slab) || (getTerrain(x+1, y) == Terrain_Mountain);
                    bool down = (y+1 >= map.getSizeY()) || (getTerrain(x, y+1) == Terrain_Rock) || (getTerrain(x, y+1) == Terrain_Slab) || (getTerrain(x, y+1) == Terrain_Mountain);
                    bool left = (x-1 < 0) || (getTerrain(x-1, y) == Terrain_Rock) || (getTerrain(x-1, y) == Terrain_Slab) || (getTerrain(x-1, y) == Terrain_Mountain);

                    tile = Tile::TerrainTile_Rock + (up | (right << 1) | (down << 2) | (left << 3));
                } break;

                case Terrain_Dunes: {
                    //determine which surounding tiles are dunes
                    bool up = (y-1 < 0) || (getTerrain(x, y-1) == Terrain_Dunes);
                    bool right = (x+1 >= map.getSizeX()) || (getTerrain(x+1, y) == Terrain_Dunes);
                    bool down = (y+1 >= map.getSizeY()) || (getTerrain(x, y+1) == Terrain_Dunes);
                    bool left = (x-1 < 0) || (getTerrain(x-1, y) == Terrain_Dunes);

                    tile = Tile::TerrainTile_Dunes + (up | (right << 1) | (down << 2) | (left << 3));
                } break;

                case Terrain_Mountain: {
                    //determine which surounding tiles are mountains
                    bool up = (y-1 < 0) || (getTerrain(x, y-1) == Terrain_Mountain);
                    bool right = (x+1 >= map.getSizeX()) || (getTerrain(x+1, y) == Terrain_Mountain);
                    bool down = (y+1 >= map.getSizeY()) || (getTerrain(x, y+1) == Terrain_Mountain);
                    bool left = (x-1 < 0) || (getTerrain(x-1, y) == Terrain_Mountain);

                    tile = Tile::TerrainTile_Mountain + (up | (right << 1) | (down << 2) | (left << 3));
                } break;

                case Terrain_Spice:
                case Terrain_GreenSpice:
                case Terrain_RedSpice:
                case Terrain_PaleLilacSpice:
                case Terrain_WhiteSpice: {
                    //determine which surounding tiles are spice
                    bool up = (y-1 < 0) || isSameSpiceFamily(getTerrain(x, y-1), terrainType);
                    bool right = (x+1 >= map.getSizeX()) || isSameSpiceFamily(getTerrain(x+1, y), terrainType);
                    bool down = (y+1 >= map.getSizeY()) || isSameSpiceFamily(getTerrain(x, y+1), terrainType);
                    bool left = (x-1 < 0) || isSameSpiceFamily(getTerrain(x-1, y), terrainType);

                    tile = Tile::TerrainTile_Spice + (up | (right << 1) | (down << 2) | (left << 3));
                } break;

                case Terrain_ThickSpice:
                case Terrain_ThickGreenSpice:
                case Terrain_ThickRedSpice:
                case Terrain_ThickPaleLilacSpice:
                case Terrain_ThickWhiteSpice: {
                    //determine which surounding tiles are thick spice
                    bool up = (y-1 < 0) || (getTerrain(x, y-1) == terrainType);
                    bool right = (x+1 >= map.getSizeX()) || (getTerrain(x+1, y) == terrainType);
                    bool down = (y+1 >= map.getSizeY()) || (getTerrain(x, y+1) == terrainType);
                    bool left = (x-1 < 0) || (getTerrain(x-1, y) == terrainType);

                    tile = Tile::TerrainTile_ThickSpice + (up | (right << 1) | (down << 2) | (left << 3));
                } break;

                case Terrain_SpiceBloom:
                case Terrain_GreenSpiceBloom:
                case Terrain_RedSpiceBloom:
                case Terrain_PaleLilacSpiceBloom:
                case Terrain_WhiteSpiceBloom: {
                    tile = Tile::TerrainTile_SpiceBloom;
                } break;

                case Terrain_SpecialBloom: {
                    tile = Tile::TerrainTile_SpecialBloom;
                } break;

                default: {
                    THROW(std::runtime_error, "MapEditor::DrawMap(): Invalid terrain type");
                } break;
            }

            //draw map[x][y]
            SDL_Rect source = { (tile % NUM_TERRAIN_TILES_X)*zoomedTilesize, (tile / NUM_TERRAIN_TILES_X)*zoomedTilesize,
                                zoomedTilesize, zoomedTilesize };
            SDL_Rect drawLocation = {   pScreenborder->world2screenX(x*TILESIZE), pScreenborder->world2screenY(y*TILESIZE),
                                        zoomedTilesize, zoomedTilesize };
            SDL_Texture* terrainSprite = TerrainSprite;
            const unsigned int terrainObjPic = getTerrainObjPic(terrainType);
            if(terrainObjPic != ObjPic_Terrain) {
                terrainSprite = pGFXManager->getZoomedObjPic(terrainObjPic, currentZoomlevel);
            }
            SDL_RenderCopy(renderer, terrainSprite, &source, &drawLocation);
        }
    }


    std::vector<int> selectedStructures = getMirrorStructures(selectedStructureID);

    for(const Structure& structure : structures) {

        Coord position = structure.position;

        SDL_Rect selectionDest;
        if(structure.itemID == Structure_Slab1) {
            // Load Terrain sprite
            SDL_Texture* TerrainSprite = pGFXManager->getZoomedObjPic(ObjPic_Terrain, currentZoomlevel);

            SDL_Rect source = { Tile::TerrainTile_Slab * zoomedTilesize, 0, zoomedTilesize, zoomedTilesize };
            SDL_Rect dest = { pScreenborder->world2screenX(position.x*TILESIZE), pScreenborder->world2screenY(position.y*TILESIZE), zoomedTilesize, zoomedTilesize };

            SDL_RenderCopy(renderer, TerrainSprite, &source, &dest);

            selectionDest = dest;
        } else if(structure.itemID == Structure_Road) {
            // Auto-tile the road by checking adjacent road structures (same
            // 4-bit mask scheme Tile::blitGround uses for in-game rendering).
            const auto adjacent = [&](int dx, int dy) {
                for(const Structure& s : structures) {
                    if(s.itemID == Structure_Road
                       && s.position.x == position.x + dx
                       && s.position.y == position.y + dy) {
                        return true;
                    }
                }
                return false;
            };
            const bool up = adjacent(0, -1);
            const bool right = adjacent(1, 0);
            const bool down = adjacent(0, 1);
            const bool left = adjacent(-1, 0);
            const int mask = ((int)up) | ((int)right << 1) | ((int)down << 2) | ((int)left << 3);

            SDL_Texture* RoadSprite = pGFXManager->getZoomedObjPic(ObjPic_CityRoad, currentZoomlevel);
            SDL_Rect source = { mask * zoomedTilesize, 0, zoomedTilesize, zoomedTilesize };
            SDL_Rect dest = { pScreenborder->world2screenX(position.x*TILESIZE), pScreenborder->world2screenY(position.y*TILESIZE), zoomedTilesize, zoomedTilesize };

            SDL_RenderCopy(renderer, RoadSprite, &source, &dest);

            selectionDest = dest;
        } else if(structure.itemID == Structure_Slab4) {
            // Load Terrain Surface
            SDL_Texture* TerrainSprite = pGFXManager->getZoomedObjPic(ObjPic_Terrain, currentZoomlevel);

            for(int y = position.y; y < position.y+2; y++) {
                for(int x = position.x; x < position.x+2; x++) {
                    SDL_Rect source = { Tile::TerrainTile_Slab * zoomedTilesize, 0, zoomedTilesize, zoomedTilesize };
                    SDL_Rect dest = { pScreenborder->world2screenX(x*TILESIZE), pScreenborder->world2screenY(y*TILESIZE), zoomedTilesize, zoomedTilesize };

                    SDL_RenderCopy(renderer, TerrainSprite, &source, &dest);
                }
            }

            selectionDest.x = pScreenborder->world2screenX(position.x*TILESIZE);
            selectionDest.y = pScreenborder->world2screenY(position.y*TILESIZE);
            selectionDest.w = world2zoomedWorld(2*TILESIZE);
            selectionDest.h = world2zoomedWorld(2*TILESIZE);
        } else if(structure.itemID == Structure_Wall) {
            bool left = false;
            bool down = false;
            bool right = false;
            bool up = false;
            for(const Structure& structure : structures) {
                if(structure.itemID == Structure_Wall) {
                    if((structure.position.x == position.x - 1) && (structure.position.y == position.y))  left = true;
                    if((structure.position.x == position.x) && (structure.position.y == position.y + 1))  down = true;
                    if((structure.position.x == position.x + 1) && (structure.position.y == position.y))  right = true;
                    if((structure.position.x == position.x) && (structure.position.y == position.y - 1))  up = true;
                }
            }

            int maketile = 0;
            if((left == true) && (right == true) && (up == true) && (down == true)) {
                maketile = Wall::Wall_Full; //solid wall
            } else if((left == false) && (right == true) && (up == true) && (down == true)) {
                maketile = Wall::Wall_UpDownRight; //missing left edge
            } else if((left == true) && (right == false)&& (up == true) && (down == true)) {
                maketile = Wall::Wall_UpDownLeft; //missing right edge
            } else if((left == true) && (right == true) && (up == false) && (down == true)) {
                maketile = Wall::Wall_DownLeftRight; //missing top edge
            } else if((left == true) && (right == true) && (up == true) && (down == false)) {
                maketile = Wall::Wall_UpLeftRight; //missing bottom edge
            } else if((left == false) && (right == true) && (up == false) && (down == true)) {
                maketile = Wall::Wall_DownRight; //missing top left edge
            } else if((left == true) && (right == false) && (up == true) && (down == false)) {
                maketile = Wall::Wall_UpLeft; //missing bottom right edge
            } else if((left == true) && (right == false) && (up == false) && (down == true)) {
                maketile = Wall::Wall_DownLeft; //missing top right edge
            } else if((left == false) && (right == true) && (up == true) && (down == false)) {
                maketile = Wall::Wall_UpRight; //missing bottom left edge
            } else if((left == true) && (right == false) && (up == false) && (down == false)) {
                maketile = Wall::Wall_LeftRight; //missing above, right and below
            } else if((left == false) && (right == true) && (up == false) && (down == false)) {
                maketile = Wall::Wall_LeftRight; //missing above, left and below
            } else if((left == false) && (right == false) && (up == true) && (down == false)) {
                maketile = Wall::Wall_UpDown; //only up
            } else if((left == false) && (right == false) && (up == false) && (down == true)) {
                maketile = Wall::Wall_UpDown; //only down
            } else if((left == true) && (right == true) && (up == false) && (down == false)) {
                maketile = Wall::Wall_LeftRight; //missing above and below
            } else if((left == false) && (right == false) && (up == true) && (down == true)) {
                maketile = Wall::Wall_UpDown; //missing left and right
            } else if((left == false) && (right == false) && (up == false) && (down == false)) {
                maketile = Wall::Wall_Standalone; //missing left and right
            }

            // Load Wall texture
            SDL_Texture* WallSprite = pGFXManager->getZoomedObjPic(ObjPic_Wall, currentZoomlevel);

            SDL_Rect source = { maketile * zoomedTilesize, 0, zoomedTilesize, zoomedTilesize };
            SDL_Rect dest = { pScreenborder->world2screenX(position.x*TILESIZE), pScreenborder->world2screenY(position.y*TILESIZE), zoomedTilesize, zoomedTilesize };

            SDL_RenderCopy(renderer, WallSprite, &source, &dest);

            selectionDest = dest;
        } else {

            int objectPic = 0;
            switch(structure.itemID) {
                case Structure_Barracks:            objectPic = ObjPic_Barracks;            break;
                case Structure_ConstructionYard:    objectPic = ObjPic_ConstructionYard;    break;
                case Structure_GunTurret:           objectPic = ObjPic_GunTurret;           break;
                case Structure_HeavyFactory:        objectPic = ObjPic_HeavyFactory;        break;
                case Structure_HighTechFactory:     objectPic = ObjPic_HighTechFactory;     break;
                case Structure_IX:                  objectPic = ObjPic_IX;                  break;
                case Structure_LightFactory:        objectPic = ObjPic_LightFactory;        break;
                case Structure_Palace:              objectPic = ObjPic_Palace;              break;
                case Structure_Radar:               objectPic = ObjPic_Radar;               break;
                case Structure_Refinery:            objectPic = ObjPic_Refinery;            break;
                case Structure_Doublefinery:        objectPic = ObjPic_Doublefinery;        break;
                case Structure_RepairYard:          objectPic = ObjPic_RepairYard;          break;
                case Structure_RocketTurret:        objectPic = ObjPic_RocketTurret;        break;
                case Structure_Silo:                objectPic = ObjPic_Silo;                break;
                case Structure_StarPort:            objectPic = ObjPic_Starport;            break;
                case Structure_Wall:                objectPic = ObjPic_Wall;                break;
                case Structure_WindTrap:            objectPic = ObjPic_Windtrap;            break;
                case Structure_WOR:                 objectPic = ObjPic_WOR;                 break;
                case Structure_ZoneResidential:     objectPic = ObjPic_ZoneResidential;     break;
                case Structure_ZoneCommercial:      objectPic = ObjPic_ZoneCommercial;      break;
                case Structure_ZoneIndustrial:      objectPic = ObjPic_ZoneIndustrial;      break;
                case Structure_NuclearPlant:        objectPic = ObjPic_NuclearPlant;        break;
                case Structure_PoliceStation:       objectPic = ObjPic_PoliceStation;       break;
                case Structure_Stadium:             objectPic = ObjPic_Stadium;             break;
                case Structure_Airport:             objectPic = ObjPic_Airport;             break;
                case Structure_AdvancedWindTrap:    objectPic = ObjPic_AdvancedWindTrap;    break;
                case Structure_AdvancedWindTrapMK2: objectPic = ObjPic_AdvancedWindTrap2x3; break;
                case Structure_AdvancedWindTrapMK3: objectPic = ObjPic_AdvancedWindTrap3x2; break;
                case Structure_Worfinery:           objectPic = ObjPic_Worfinery;           break;
                case Structure_TechCenter:          objectPic = ObjPic_TechCenter;          break;
                case Structure_Scoutpost:           objectPic = ObjPic_Scoutpost;           break;
                case Structure_Flamepost:           objectPic = ObjPic_Flamepost;           break;
                case Structure_Slowpost:            objectPic = ObjPic_Slowpost;            break;
                case Structure_Chemipost:           objectPic = ObjPic_Chemipost;           break;
                case Structure_LoveFactory:         objectPic = ObjPic_LoveFactory;         break;
                case Structure_ChaosFactory:        objectPic = ObjPic_ChaosFactory;        break;
                default:                            objectPic = 0;                          break;
            }

            const bool greenWindtrap = structure.itemID == Structure_WindTrap && structure.powerUpgraded
                && ModManager::instance().isTornieFamilyContentActive();
            SDL_Texture* ObjectSprite = greenWindtrap
                ? pGFXManager->getObjPic(objectPic, structure.house, true)[currentZoomlevel]
                : pGFXManager->getZoomedObjPic(objectPic, structure.house, currentZoomlevel);

            Coord frameSize = world2zoomedWorld(getStructureSize(structure.itemID)*TILESIZE);

            // Frame index: vanilla WindTrap uses frame 9. Tornie custom atlases keep
            // construction/destroyed/active frames horizontally, so the active frame is 2.
            int frameIdx = 2;
            if(structure.itemID == Structure_WindTrap) {
                frameIdx = 9;
            }
            else if(structure.itemID == Structure_ZoneResidential
                 || structure.itemID == Structure_ZoneCommercial
                 || structure.itemID == Structure_ZoneIndustrial) frameIdx = 0;
            SDL_Rect source = { frameSize.x*frameIdx, 0, frameSize.x, frameSize.y };
            SDL_Rect dest = { pScreenborder->world2screenX(position.x*TILESIZE), pScreenborder->world2screenY(position.y*TILESIZE), frameSize.x, frameSize.y };

            bool structureDrawn = false;
            const auto editorStructureUIGraphic = getMapEditorStructureUIGraphic(structure.itemID);
            if(editorStructureUIGraphic != NUM_UIGRAPHICS) {
                if(SDL_Texture* uiStructureTexture = pGFXManager->getUIGraphic(editorStructureUIGraphic, structure.house)) {
                    SDL_RenderCopy(renderer, uiStructureTexture, nullptr, &dest);
                    structureDrawn = true;
                }
            }

            if(!structureDrawn && ObjectSprite != nullptr) {
                int textureWidth = 0;
                int textureHeight = 0;
                if(SDL_QueryTexture(ObjectSprite, nullptr, nullptr, &textureWidth, &textureHeight) == 0) {
                    if(source.x < 0 || source.y < 0 || source.x + source.w > textureWidth || source.y + source.h > textureHeight) {
                        source.x = 0;
                        source.y = 0;
                        source.w = std::min(frameSize.x, textureWidth);
                        source.h = std::min(frameSize.y, textureHeight);
                    }
                }
                SDL_RenderCopy(renderer, ObjectSprite, &source, &dest);
            }

            selectionDest = dest;
        }

        // draw selection frame
        if(!bCompleteMap && (std::find(selectedStructures.begin(), selectedStructures.end(), structure.id) != selectedStructures.end()) ) {
            //now draw the selection box thing, with parts at all corners of structure

            // top left bit
            for(int i=0;i<=currentZoomlevel;i++) {
                renderDrawHLine(renderer, selectionDest.x+i, selectionDest.y+i, selectionDest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
                renderDrawVLine(renderer, selectionDest.x+i, selectionDest.y+i, selectionDest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
            }

            // top right bit
            for(int i=0;i<=currentZoomlevel;i++) {
                renderDrawHLine(renderer, selectionDest.x + selectionDest.w-1 - i, selectionDest.y+i, selectionDest.x + selectionDest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                renderDrawVLine(renderer, selectionDest.x + selectionDest.w-1 - i, selectionDest.y+i, selectionDest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
            }

            // bottom left bit
            for(int i=0;i<=currentZoomlevel;i++) {
                renderDrawHLine(renderer, selectionDest.x+i, selectionDest.y + selectionDest.h-1 - i, selectionDest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
                renderDrawVLine(renderer, selectionDest.x+i, selectionDest.y + selectionDest.h-1 - i, selectionDest.y + selectionDest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
            }

            // bottom right bit
            for(int i=0;i<=currentZoomlevel;i++) {
                renderDrawHLine(renderer, selectionDest.x + selectionDest.w-1 - i, selectionDest.y + selectionDest.h-1 - i, selectionDest.x + selectionDest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                renderDrawVLine(renderer, selectionDest.x + selectionDest.w-1 - i, selectionDest.y + selectionDest.h-1 - i, selectionDest.y + selectionDest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
            }
        }

    }

    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieFamilyContentActive();

    for(const Unit& unit : units) {

        const Coord& position = unit.position;

        const Coord tankTurretOffset[] =    {   Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0),
                                                Coord(0, 0)
                                            };

        const Coord siegeTankTurretOffset[] =   {   Coord(8, -12),
                                                    Coord(0, -20),
                                                    Coord(0, -20),
                                                    Coord(-4, -20),
                                                    Coord(-8, -12),
                                                    Coord(-8, -4),
                                                    Coord(-4, -12),
                                                    Coord(8, -4)
                                            };

        const Coord sonicTankTurretOffset[] =   {   Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8)
                                                };

        const Coord launcherTurretOffset[] =    {   Coord(0, -12),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -12),
                                                    Coord(0, -8),
                                                    Coord(0, -8),
                                                    Coord(0, -8)
                                                };

        const Coord devastatorTurretOffset[] =  {
                                                    Coord(8, -16),
                                                    Coord(-4, -12),
                                                    Coord(0, -16),
                                                    Coord(4, -12),
                                                    Coord(-8, -16),
                                                    Coord(0, -12),
                                                    Coord(-4, -12),
                                                    Coord(0, -12)
                                                };

        const Coord harvestankTurretOffset[] =   {
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0),
                                                    Coord(0, 0)
                                                };

        int objectPicBase = 0;
        int framesX = NUM_ANGLES;
        int framesY = 1;
        int objectPicGun = -1;
        int objectPicGunHouse = unit.house;
        const Coord* gunOffset = nullptr;
        switch(unit.itemID) {
            case Unit_Carryall:         objectPicBase = ObjPic_Carryall;         framesY = 2;                                                                    break;
            case Unit_ChemicalCarryall: objectPicBase = ObjPic_ChemicalCarryall; framesY = 2;                                                                    break;
            case Unit_Devastator:       objectPicBase = ObjPic_Devastator_Base; objectPicGun = ObjPic_Devastator_Gun;   gunOffset = devastatorTurretOffset;     break;
            case Unit_Deviator:         objectPicBase = ObjPic_Tank_Base;       objectPicGun = tornieActive ? ObjPic_DeviatorGunTornie : ObjPic_Launcher_Gun; objectPicGunHouse = tornieActive ? HOUSE_HARKONNEN : unit.house; gunOffset = launcherTurretOffset; break;
            case Unit_Frigate:          objectPicBase = ObjPic_Frigate;                                                                                         break;
            case Unit_Harvester:        objectPicBase = ObjPic_Harvester;                                                                                       break;
            case Unit_RebelHarvester:   objectPicBase = ObjPic_Harvester;     objectPicGun = tornieActive ? ObjPic_HarvestankGunTornie : -1; gunOffset = harvestankTurretOffset; break;
            case Unit_Soldier:          objectPicBase = ObjPic_Soldier;         framesX = 4;    framesY = 3;                                                    break;
            case Unit_Launcher:         objectPicBase = ObjPic_Tank_Base;       objectPicGun = ObjPic_Launcher_Gun;     gunOffset = launcherTurretOffset;       break;
            case Unit_MCV:              objectPicBase = ObjPic_MCV;                                                                                             break;
            case Unit_EliteOrnithopter: objectPicBase = ObjPic_EliteOrnithopter; framesY = 3;                                                                    break;
            case Unit_Ornithopter:      objectPicBase = ObjPic_Ornithopter;     framesY = 3;                                                                    break;
            case Unit_Quad:             objectPicBase = ObjPic_Quad;                                                                                            break;
            case Unit_Saboteur:         objectPicBase = ObjPic_Saboteur;        framesX = 4;    framesY = 3;                                                    break;
            case Unit_Sandworm:         objectPicBase = ObjPic_Sandworm;        framesX = 1;    framesY = 9;                                                    break;
            case Unit_SiegeTank:        objectPicBase = ObjPic_Siegetank_Base;  objectPicGun = ObjPic_Siegetank_Gun;    gunOffset = siegeTankTurretOffset;      break;
            case Unit_SonicTank:        objectPicBase = ObjPic_Tank_Base;       objectPicGun = ObjPic_Sonictank_Gun;    gunOffset = sonicTankTurretOffset;      break;
            case Unit_Tank:             objectPicBase = ObjPic_Tank_Base;       objectPicGun = ObjPic_Tank_Gun;         gunOffset = tankTurretOffset;           break;
            case Unit_Trike:            objectPicBase = ObjPic_Trike;                                                                                           break;
            case Unit_RaiderTrike:      objectPicBase = ObjPic_Trike;                                                                                           break;
            case Unit_Trooper:          objectPicBase = ObjPic_Trooper;         framesX = 4;    framesY = 3;                                                    break;
            case Unit_Special:          objectPicBase = ObjPic_Devastator_Base; objectPicGun = ObjPic_Devastator_Gun;   gunOffset = devastatorTurretOffset;     break;
            case Unit_Infantry5:
            case Unit_Infantry:         objectPicBase = ObjPic_Infantry;         framesX = 4;    framesY = 4;                                                   break;
            case Unit_Troopers5:
            case Unit_Troopers:         objectPicBase = ObjPic_Troopers;         framesX = 4;    framesY = 4;                                                   break;
            case Unit_RaiderQuad:       objectPicBase = ObjPic_RaiderQuad;                                                                                       break;
            case Unit_RangedTank:      objectPicBase = ObjPic_RangedTankBase; objectPicGun = ObjPic_RangedTankTurret; gunOffset = tankTurretOffset;               break;
            case Unit_SlowTank:        objectPicBase = ObjPic_SlowTankBase; objectPicGun = ObjPic_SlowTankTurret; gunOffset = tankTurretOffset;                   break;
            case Unit_CapacityHarvester: objectPicBase = ObjPic_CapacityHarvester;                                                                               break;
            case Unit_FastHarvester:   objectPicBase = ObjPic_FastHarvester;                                                                                     break;
            case Unit_TameSandworm:    objectPicBase = ObjPic_TameSandworm; framesX = 1; framesY = 9;                                                            break;
            case Unit_FlameQuad:       objectPicBase = ObjPic_FlameQuad;                                                                                        break;
            case Unit_GasInfantry:     objectPicBase = ObjPic_GasInfantry; framesX = 4; framesY = 3;                                                             break;
            case Unit_FlameTrooper:    objectPicBase = ObjPic_Trooper; framesX = 4; framesY = 3;                                                                 break;
            case Unit_RocketTrike:      objectPicBase = ObjPic_RocketTrike;                                                                                       break;
            case Unit_SonicTrike:       objectPicBase = ObjPic_SonicTrike;                                                                                        break;
            case Unit_FlameTank:        objectPicBase = ObjPic_Tank_Base;       objectPicGun = tornieActive ? ObjPic_FlameTankGunTornie : ObjPic_Launcher_Gun; objectPicGunHouse = tornieActive ? HOUSE_HARKONNEN : unit.house; gunOffset = launcherTurretOffset; break;
            case Unit_EliteLauncher:    objectPicBase = ObjPic_Tank_Base;       objectPicGun = tornieActive ? ObjPic_EliteLauncherGunTornie : ObjPic_Launcher_Gun; objectPicGunHouse = tornieActive ? HOUSE_HARKONNEN : unit.house; gunOffset = launcherTurretOffset; break;
            case Unit_EliteSiegeTank:   objectPicBase = ObjPic_Siegetank_Base;  objectPicGun = ObjPic_EliteSiegeTankGunTornie; gunOffset = siegeTankTurretOffset; break;
            case Unit_ChemicalSiegeTank: objectPicBase = ObjPic_Siegetank_Base; objectPicGun = ObjPic_ChemicalSiegeTankGunTornie; gunOffset = siegeTankTurretOffset; break;
        }

        SDL_Texture* pObjectSprite = pGFXManager->getZoomedObjPic(objectPicBase, unit.house, currentZoomlevel);

        int angle = unit.angle / (NUM_ANGLES/framesX);

        int frame = (unit.itemID == Unit_Sandworm || unit.itemID == Unit_TameSandworm) ? 5 : 0;

        SDL_Rect source = calcSpriteSourceRect(pObjectSprite, angle, framesX, frame, framesY);
        int frameSizeX = source.w;
        int frameSizeY = source.h;
        SDL_Rect drawLocation = calcSpriteDrawingRect(  pObjectSprite,
                                                        pScreenborder->world2screenX((position.x*TILESIZE)+(TILESIZE/2)),
                                                        pScreenborder->world2screenY((position.y*TILESIZE)+(TILESIZE/2)),
                                                        framesX, framesY, HAlign::Center, VAlign::Center);

        SDL_RenderCopy(renderer, pObjectSprite, &source, &drawLocation);

        if(objectPicGun >= 0) {
            SDL_Texture* pGunSprite = pGFXManager->getZoomedObjPic(objectPicGun, objectPicGunHouse, currentZoomlevel);

            SDL_Rect source2 = calcSpriteSourceRect(pGunSprite, unit.angle, NUM_ANGLES);
            SDL_Rect drawLocation2 = calcSpriteDrawingRect( pGunSprite,
                                                            pScreenborder->world2screenX((position.x*TILESIZE)+(TILESIZE/2)+gunOffset[unit.angle].x),
                                                            pScreenborder->world2screenY((position.y*TILESIZE)+(TILESIZE/2)+gunOffset[unit.angle].y),
                                                            NUM_ANGLES, 1, HAlign::Center, VAlign::Center);

            SDL_RenderCopy(renderer, pGunSprite, &source2, &drawLocation2);
        }

        const bool yellowStarMarker = (unit.itemID == Unit_RaiderTrike || unit.itemID == Unit_Special
                                       || unit.itemID == Unit_Deviator);
        const bool customStarMarker = (unit.itemID == Unit_RocketTrike || unit.itemID == Unit_SonicTrike
                                       || unit.itemID == Unit_FlameTank || unit.itemID == Unit_EliteLauncher
                                       || unit.itemID == Unit_EliteSiegeTank || unit.itemID == Unit_ChemicalSiegeTank
                                       || unit.itemID == Unit_ChemicalCarryall || unit.itemID == Unit_RebelHarvester);
        if(yellowStarMarker || customStarMarker) {
            SDL_Texture* pStarSprite = pGFXManager->getZoomedObjPic(ObjPic_Star, currentZoomlevel);

            const int markerRight = pScreenborder->world2screenX((position.x*TILESIZE)+(TILESIZE/2)) + frameSizeX/2 - 1;
            const int markerBottom = pScreenborder->world2screenY((position.y*TILESIZE)+(TILESIZE/2)) + frameSizeY/2 - 1;

            if(customStarMarker) {
                const int radius = (currentZoomlevel == 0) ? 2 : ((currentZoomlevel == 1) ? 3 : 5);
                const int cx = markerRight - radius;
                const int cy = markerBottom - radius;
                renderDrawLine(renderer, cx - radius, cy, cx + radius, cy, COLOR_BLUE);
                renderDrawLine(renderer, cx, cy - radius, cx, cy + radius, COLOR_BLUE);
                renderDrawLine(renderer, cx - radius + 1, cy - radius + 1, cx + radius - 1, cy + radius - 1, COLOR_BLUE);
                renderDrawLine(renderer, cx - radius + 1, cy + radius - 1, cx + radius - 1, cy - radius + 1, COLOR_BLUE);
                renderDrawLine(renderer, cx - radius + 1, cy, cx + radius - 1, cy, COLOR_LIGHTBLUE);
                renderDrawLine(renderer, cx, cy - radius + 1, cx, cy + radius - 1, COLOR_LIGHTBLUE);
            } else {
                SDL_Rect drawLocation2 = calcDrawingRect(pStarSprite,
                                                         markerRight,
                                                         markerBottom,
                                                         HAlign::Right, VAlign::Bottom);
                SDL_RenderCopy(renderer, pStarSprite, nullptr, &drawLocation2);
            }
        }

    }

    // draw tactical pos rectangle (the starting screen)
    if(!bCompleteMap && getMapVersion() < 2 && mapInfo.tacticalPos.isValid()) {

        SDL_Rect dest;
        dest.x = pScreenborder->world2screenX( mapInfo.tacticalPos.x*TILESIZE);
        dest.y = pScreenborder->world2screenY( mapInfo.tacticalPos.y*TILESIZE);
        dest.w = world2zoomedWorld(15*TILESIZE);
        dest.h = world2zoomedWorld(10*TILESIZE);

        renderDrawRect(renderer, &dest, COLOR_DARKGREY);
    }

    SDL_Texture* validPlace = nullptr;
    SDL_Texture* invalidPlace = nullptr;
    SDL_Texture* greyPlace = nullptr;

    switch(currentZoomlevel) {
        case 0: {
            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel0);
            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel0);
            greyPlace = pGFXManager->getUIGraphic(UI_GreyPlace_Zoomlevel0);
        } break;

        case 1: {
            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel1);
            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel1);
            greyPlace = pGFXManager->getUIGraphic(UI_GreyPlace_Zoomlevel1);
        } break;

        case 2:
        default: {
            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel2);
            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel2);
            greyPlace = pGFXManager->getUIGraphic(UI_GreyPlace_Zoomlevel2);
        } break;
    }

    if(!bCompleteMap && !pInterface->hasChildWindow() && pScreenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY)) {

        int xPos = pScreenborder->screen2MapX(drawnMouseX);
        int yPos = pScreenborder->screen2MapY(drawnMouseY);

        if(currentEditorMode.mode == EditorMode::EditorMode_Terrain) {

            int halfsize = currentEditorMode.pensize/2;

            for(int m=0;m<mapMirror->getSize();m++) {

                Coord position = mapMirror->getCoord( Coord(xPos, yPos), m);


                SDL_Rect dest;
                dest.x = pScreenborder->world2screenX( (position.x-halfsize)*TILESIZE);
                dest.y = pScreenborder->world2screenY( (position.y-halfsize)*TILESIZE);
                dest.w = world2zoomedWorld(currentEditorMode.pensize*TILESIZE);
                dest.h = world2zoomedWorld(currentEditorMode.pensize*TILESIZE);

                // now draw the box with parts at all corners
                // top left bit
                for(int i=0;i<=currentZoomlevel;i++) {
                    renderDrawHLine(renderer, dest.x+i, dest.y+i, dest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
                    renderDrawVLine(renderer, dest.x+i, dest.y+i, dest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
                }

                // top right bit
                for(int i=0;i<=currentZoomlevel;i++) {
                    renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.x + dest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                    renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
                }

                // bottom left bit
                for(int i=0;i<=currentZoomlevel;i++) {
                    renderDrawHLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
                    renderDrawVLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                }

                // bottom right bit
                for(int i=0;i<=currentZoomlevel;i++) {
                    renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.x + dest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                    renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
                }
            }

        } else if(currentEditorMode.mode == EditorMode::EditorMode_Structure) {
            Coord structureSize = getStructureSize(currentEditorMode.itemID);

            for(int m=0;m<mapMirror->getSize();m++) {

                Coord position = mapMirror->getCoord( Coord(xPos, yPos), m, structureSize);

                for(int x = position.x; x < (position.x + structureSize.x); x++) {
                    for(int y = position.y; y < (position.y + structureSize.y); y++) {
                        SDL_Texture* image = validPlace;

                        // check if mirroring of the original (!) position is possible
                        if(mapMirror->mirroringPossible( Coord(xPos, yPos), structureSize) == false) {
                            image = invalidPlace;
                        }

                        // check all mirrored places
                        for(int k=0;k<mapMirror->getSize();k++) {
                            Coord pos = mapMirror->getCoord( Coord(x, y), k);

                            if(!map.isInsideMap(pos.x,pos.y) || isTileBlocked(pos.x, pos.y, true, (currentEditorMode.itemID != Structure_Slab1) )) {
                                image = invalidPlace;
                            } else if((image != invalidPlace) && (map(pos.x,pos.y) != Terrain_Rock)) {
                                image = greyPlace;
                            }
                        }

                        SDL_Rect drawLocation = {   pScreenborder->world2screenX(x*TILESIZE), pScreenborder->world2screenY(y*TILESIZE),
                                                    zoomedTilesize, zoomedTilesize };
                        SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
                    }
                }
            }
        } else if(currentEditorMode.mode == EditorMode::EditorMode_Unit) {
            for(int m=0;m<mapMirror->getSize();m++) {

                Coord position = mapMirror->getCoord( Coord(xPos, yPos), m);

                SDL_Texture* image = validPlace;
                // check all mirrored places
                for(int k=0;k<mapMirror->getSize();k++) {
                    Coord pos = mapMirror->getCoord( position, k);

                    if(!map.isInsideMap(pos.x,pos.y) || isTileBlocked(pos.x, pos.y, false, true)) {
                        image = invalidPlace;
                    }
                }
                SDL_Rect drawLocation = calcDrawingRect(image, pScreenborder->world2screenX(position.x*TILESIZE), pScreenborder->world2screenY(position.y*TILESIZE));
                SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
            }
        } else if(currentEditorMode.mode == EditorMode::EditorMode_TacticalPos) {
            // draw tactical pos rectangle (the starting screen)
            if(mapInfo.tacticalPos.isValid()) {

                SDL_Rect dest = {   pScreenborder->world2screenX(xPos*TILESIZE), pScreenborder->world2screenY(yPos*TILESIZE),
                                    world2zoomedWorld(15*TILESIZE), world2zoomedWorld(10*TILESIZE) };
                renderDrawRect(renderer, &dest, COLOR_WHITE);
            }
        }
    }

    // draw selection rect for units (selection rect for structures is already drawn)
    if(!bCompleteMap) {
        std::vector<int> selectedUnits = getMirrorUnits(selectedUnitID);

        for(const Unit& unit : units) {
            if(std::find(selectedUnits.begin(), selectedUnits.end(), unit.id) != selectedUnits.end()) {
                const Coord& position = unit.position;

                SDL_Texture* selectionBox = nullptr;

                switch(currentZoomlevel) {
                    case 0:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel0);   break;
                    case 1:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel1);   break;
                    case 2:
                    default:    selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel2);   break;
                }

                SDL_Rect dest = calcDrawingRect(selectionBox,
                                                pScreenborder->world2screenX((position.x*TILESIZE)+(TILESIZE/2)),
                                                pScreenborder->world2screenY((position.y*TILESIZE)+(TILESIZE/2)),
                                                HAlign::Center, VAlign::Center);

                SDL_RenderCopy(renderer, selectionBox, nullptr, &dest);
            }
        }
    }

    // draw selection rect for map items (spice bloom, special bloom or spice field)
    if(!bCompleteMap && selectedMapItemCoord.isValid()
        && ( (std::find(spiceBlooms.begin(), spiceBlooms.end(), selectedMapItemCoord) != spiceBlooms.end())
            || (std::find(specialBlooms.begin(), specialBlooms.end(), selectedMapItemCoord) != specialBlooms.end())
            || (std::find(spiceFields.begin(), spiceFields.end(), selectedMapItemCoord) != spiceFields.end()) )) {

        SDL_Texture* selectionBox = nullptr;

        switch(currentZoomlevel) {
            case 0:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel0);   break;
            case 1:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel1);   break;
            case 2:
            default:    selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel2);   break;
        }

        SDL_Rect dest = calcDrawingRect(selectionBox,
                                        pScreenborder->world2screenX((selectedMapItemCoord.x*TILESIZE)+(TILESIZE/2)),
                                        pScreenborder->world2screenY((selectedMapItemCoord.y*TILESIZE)+(TILESIZE/2)),
                                        HAlign::Center, VAlign::Center);

        SDL_RenderCopy(renderer, selectionBox, nullptr, &dest);
    }
}

void MapEditor::saveMapshot() {
    int oldCurrentZoomlevel = currentZoomlevel;
    currentZoomlevel = 0;

    std::string mapshotFilename = (lastSaveName.empty() ? generateMapname() : getBasename(lastSaveName, true)) + ".png";

    int sizeX = world2zoomedWorld(map.getSizeX()*TILESIZE);
    int sizeY = world2zoomedWorld(map.getSizeY()*TILESIZE);

    SDL_Rect board = { 0, 0, sizeX, sizeY };

    ScreenBorder tmpScreenborder(board);
    tmpScreenborder.adjustScreenBorderToMapsize(map.getSizeX(), map.getSizeY());

    auto renderTarget = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, sizeX, sizeY) };
    if(renderTarget == nullptr) {
        SDL_Log("SDL_CreateTexture() failed: %s", SDL_GetError());
        currentZoomlevel = oldCurrentZoomlevel;
        return;
    }

    SDL_Texture* oldRenderTarget = SDL_GetRenderTarget(renderer);
    if(SDL_SetRenderTarget(renderer, renderTarget.get()) != 0) {
        SDL_Log("SDL_SetRenderTarget() failed: %s", SDL_GetError());
        SDL_SetRenderTarget(renderer, oldRenderTarget);
        currentZoomlevel = oldCurrentZoomlevel;
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    drawMap(&tmpScreenborder, true);

    sdl2::surface_ptr pMapshotSurface = renderReadSurface(renderer);
    SavePNG(pMapshotSurface.get(), mapshotFilename.c_str());

    SDL_SetRenderTarget(renderer, oldRenderTarget);

    currentZoomlevel = oldCurrentZoomlevel;
}
