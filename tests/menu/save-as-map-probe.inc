// Included only by the private menu navigation fixture (-fno-access-control).
// Starts the real generators, game, pause menu, export dialog, map lists and loaders.
#include <GUI/dune/InGameMenu.h>
#include <GUI/dune/SaveMapWindow.h>
#include <INIMap/MatchMapExport.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <vector>

namespace SaveAsMapProbe {

using MatchMapExport::Moment;
using Counts = std::map<std::pair<int, int>, int>;

struct ObjectExpectation {
    int house;
    int item;
    Coord position;
    int health;
    int angle = 0;
    ATTACKMODE mode = AREAGUARD;
};

struct Snapshot {
    int width = 0;
    int height = 0;
    Coord patch;
    std::vector<int> terrain;
    std::vector<ObjectExpectation> structures;
    std::vector<ObjectExpectation> units;
    ObjectExpectation removedStructure{};
    Counts structureCounts;
    Counts unitCounts;
    int nativeWorms = 0;
    std::map<int, int> credits;
    std::map<int, int> teams;
};

struct ExportedMap {
    std::string name;
    std::string path;
    std::string bytes;
    std::string mod;
    SettingsClass::GameOptionsClass rules;
    Moment moment;
    Snapshot snapshot;
};

void click(Widget& widget) {
    menuRequire(widget.isVisible() && widget.isEnabled(), "Save as Map control is unavailable");
    widget.handleMouseLeft(widget.getSize().x / 2, widget.getSize().y / 2, true);
    widget.handleMouseLeft(widget.getSize().x / 2, widget.getSize().y / 2, false);
}

template<class Operation>
void requiresRuntimeError(Operation operation, const char* message) {
    bool rejected = false;
    try { operation(); }
    catch(const std::runtime_error&) { rejected = true; }
    menuRequire(rejected, message);
}

void dismissMessage(Window& parent) {
    menuRequire(dynamic_cast<MsgBox*>(parent.pChildWindow) != nullptr,
                "Save as Map did not present its acknowledged result");
    parent.closeChildWindow();
    parent.processChildWindowOpenCloses();
    menuRequire(!parent.hasChildWindow(), "Save as Map result did not close cleanly");
}

void requireFit(Window& window, std::initializer_list<Widget*> controls) {
    const auto at = window.getPosition(), size = window.getSize();
    menuRequire(size.x > 0 && size.y > 0 && size.x <= 640 && size.y <= 480
                && at.x >= 0 && at.y >= 0 && at.x + size.x <= getRendererWidth()
                && at.y + size.y <= getRendererHeight(),
                "Save as Map or pause window does not fit the 640x480 viewport");
    std::vector<SDL_Rect> occupied;
    for(auto* control : controls) {
        if(!control->isVisible() || !control->getParent()) continue;
        const auto bounds = researchMenuBounds(window, *control);
        menuRequire(bounds.w > 0 && bounds.h > 0 && bounds.x >= at.x && bounds.y >= at.y
                    && bounds.x + bounds.w <= at.x + size.x && bounds.y + bounds.h <= at.y + size.y,
                    "Save as Map control is clipped by its window");
        // Also project the centred window into 640x480 when this run is wider.
        SDL_Rect compact = bounds;
        compact.x += (640 - size.x) / 2 - at.x;
        compact.y += (480 - size.y) / 2 - at.y;
        menuRequire(compact.x >= 0 && compact.y >= 0 && compact.x + compact.w <= 640
                    && compact.y + compact.h <= 480, "Save as Map control leaves 640x480");
        const SDL_Rect crests[] = {{11, 11, 53, 54}, {640 - 64, 11, 53, 54},
            {11, 480 - 65, 53, 54}, {640 - 64, 480 - 65, 53, 54}};
        for(const auto& crest : crests)
            menuRequire(!SDL_HasIntersection(&compact, &crest),
                        "Save as Map control covers an original 53x54 desert crest at 11px");
        if(auto* label = dynamic_cast<Label*>(control)) {
            if(label->getText().empty()) continue;
            const int font = label->getTextFontSize();
            auto& style = GUIStyle::getInstance();
            const auto lines = greedyWordWrap(label->getText(), bounds.w, [&](const std::string& text) {
                return style.getMinimumLabelSize(text, font).x - 4;
            });
            int width = 0;
            for(const auto& line : lines) width = std::max(width, static_cast<int>(style.getTextWidth(line, font)));
            const int height = static_cast<int>(style.getTextHeight(font)) * static_cast<int>(lines.size())
                + 2 * (static_cast<int>(lines.size()) - 1);
            menuRequire(width + 3 <= bounds.w + 1 && height <= bounds.h + 3,
                        "Save as Map caption or snapshot-loss notes clip within their row");
        } else if(auto* box = dynamic_cast<DropDownBox*>(control)) {
            for(int i = 0; i < box->getNumEntries(); ++i)
                menuRequire(static_cast<int>(GUIStyle::getInstance().getTextWidth(box->getEntry(i), 14)) + 20 <= bounds.w,
                            "Save as Map moment choice does not fit");
        } else if(auto* button = dynamic_cast<TextButton*>(control)) {
            TerrainMixMenuProbe::requireButtonFit(*button, bounds);
        }
        for(const auto& previous : occupied)
            menuRequire(!SDL_HasIntersection(&bounds, &previous), "Save as Map controls or captions overlap");
        occupied.push_back(bounds);
    }
}

void requirePauseFit(InGameMenu& menu) {
    requireFit(menu, {&menu.title, &menu.onlineNotice, &menu.resumeButton, &menu.saveGameButton,
        &menu.saveMapButton, &menu.loadGameButton, &menu.restartGameButton, &menu.gameSettingsButton,
        &menu.quitButton, &menu.skipMissionButton, &menu.pauseGameButton, &menu.joinRequestsButton, &menu.requestPlayButton});
}

void requireDialogFit(SaveMapWindow& dialog) {
    requireFit(dialog, {&dialog.title, &dialog.nameLabel, &dialog.mapName, &dialog.momentLabel,
        &dialog.moment, &dialog.details, &dialog.destinationLabel, &dialog.saveButton, &dialog.cancelButton});
    menuRequire(!dialog.details.getText().empty(), "Save as Map omits its snapshot-loss explanation");
}

struct Match {
    std::unique_ptr<Game> game;

    explicit Match(const GameInitSettings& init) {
        menuRequire(currentGame == nullptr && currentGameMap == nullptr && screenborder == nullptr,
                    "Save as Map fixture overlaps another live game or editor");
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true; // The same diagnostic-only briefing suppression as the skirmish fixture.
        game->initGame(init);
        game->bReplay = false;
        menuRequire(currentGameMap && pLocalHouse && pLocalPlayer, "Save as Map game did not initialize");
        game->initializeGameLoop();
        game->bPause = false;
    }

    ~Match() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    void advance(int cycles) {
        const auto before = game->getGameCycleCount();
        for(int i = 0; i < cycles && !game->isGameFinished(); ++i) game->updateGameState();
        menuRequire(!game->isGameFinished() && game->getGameCycleCount() == before + cycles,
                    "A Save as Map generated or reloaded game did not advance real simulation cycles");
    }
};

int health256(const ObjectBase& object) {
    return static_cast<int>(std::lround(256.0 * object.getHealth().toDouble() / object.getMaxHealth()));
}

ObjectExpectation expect(const ObjectBase& object) {
    return {object.getOwner()->getHouseID(), object.getItemID(), object.getLocation(), health256(object)};
}

Coord clearPatch() {
    for(int y = 2; y + 8 < currentGameMap->getSizeY(); ++y) {
        for(int x = 2; x + 14 < currentGameMap->getSizeX(); ++x) {
            bool clear = true;
            for(int dy = 0; dy < 8 && clear; ++dy)
                for(int dx = 0; dx < 14 && clear; ++dx) {
                    const auto* tile = currentGameMap->getTile(x + dx, y + dy);
                    clear = !tile->hasAnObject() && !tile->hasCityZone();
                }
            if(clear) return Coord(x, y);
        }
    }
    menuRequire(false, "Generated Save as Map fixture has no clear mutation patch");
    return Coord::Invalid();
}

Snapshot changeCurrent(Game& game) {
    Snapshot result;
    result.width = currentGameMap->getSizeX();
    result.height = currentGameMap->getSizeY();
    result.patch = clearPatch();
    House* owner = pLocalHouse;
    StructureBase* removed = nullptr;
    for(auto* structure : structureList)
        if(structure->getOwner() == owner && structure->getItemID() == Structure_WindTrap) { removed = structure; break; }
    menuRequire(removed != nullptr, "Generated Save as Map fixture has no original Windtrap to remove");
    result.removedStructure = expect(*removed);
    removed->destroy();

    for(int dy = 0; dy < 8; ++dy) for(int dx = 0; dx < 14; ++dx) {
        auto* tile = currentGameMap->getTile(result.patch.x + dx, result.patch.y + dy);
        tile->setType(Terrain_Rock);
        tile->setRoad(false);
        tile->setOwner(owner->getHouseID());
    }
    const int terrains[] = {Terrain_Sand, Terrain_Rock, Terrain_Dunes, Terrain_Mountain,
        Terrain_Spice, Terrain_ThickSpice, Terrain_SpiceBloom, Terrain_SpecialBloom};
    for(int x = 0; x < 8; ++x) currentGameMap->getTile(result.patch.x + x, result.patch.y)->setType(terrains[x]);
    // Independent concrete, independent road, and both on one tile must all survive.
    currentGameMap->getTile(result.patch + Coord(0, 2))->setType(Terrain_Slab);
    currentGameMap->getTile(result.patch + Coord(1, 2))->setRoad(true);
    currentGameMap->getTile(result.patch + Coord(2, 2))->setType(Terrain_Slab);
    currentGameMap->getTile(result.patch + Coord(2, 2))->setRoad(true);

    const Coord windAt = result.patch + Coord(0, 4), wallAt = result.patch + Coord(4, 4);
    auto* wind = owner->placeStructure(NONE_ID, Structure_WindTrap, windAt.x, windAt.y, true);
    auto* wall = owner->placeStructure(NONE_ID, Structure_Wall, wallAt.x, wallAt.y, true);
    menuRequire(wind && wall, "Save as Map fixture could not place real structures");
    wind->setHealth(wind->getMaxHealth() * 3 / 8);
    wall->setHealth(wall->getMaxHealth() * 5 / 8);
    result.structures = {expect(*wind), expect(*wall)};

    const Coord trikeAt = result.patch + Coord(6, 4), infantryAt = result.patch + Coord(8, 4);
    auto* trike = owner->placeUnit(Unit_Trike, trikeAt.x, trikeAt.y, true);
    menuRequire(trike != nullptr, "Save as Map fixture could not place a real vehicle");
    trike->setHealth(trike->getMaxHealth() * 3 / 8);
    trike->setAngle(LEFTDOWN);
    trike->doSetAttackMode(AREAGUARD);
    auto vehicle = expect(*trike);
    vehicle.angle = trike->getAngle(); vehicle.mode = trike->getAttackMode();
    result.units.push_back(vehicle);
    for(int i = 0; i < 2; ++i) {
        auto* soldier = owner->placeUnit(Unit_Soldier, infantryAt.x, infantryAt.y, true);
        menuRequire(soldier != nullptr, "Save as Map fixture could not stack two real infantry");
        soldier->setHealth(soldier->getMaxHealth() * (i + 2) / 4);
        soldier->setAngle(i ? DOWN : RIGHT);
        soldier->doSetAttackMode(AREAGUARD);
        auto infantry = expect(*soldier);
        infantry.angle = soldier->getAngle(); infantry.mode = soldier->getAttackMode();
        result.units.push_back(infantry);
    }
    // A live aircraft can cross a building, but House::placeUnit rejects that
    // overlap on a fresh map. Export must relocate it, not silently lose it.
    auto* aircraft = owner->createUnit(Unit_Ornithopter, true);
    menuRequire(aircraft != nullptr, "Save as Map fixture could not create an aircraft");
    aircraft->deploy(windAt);
    aircraft->setHealth(aircraft->getMaxHealth() / 2);
    menuRequire(aircraft->isActive() && aircraft->getLocation() == windAt,
                "Save as Map fixture aircraft is not crossing its live structure");
    for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) if(auto* house = game.getHouse(h)) {
        // Test-only balance mutation: exercise all three spendable credit buckets.
        house->storedCredits = 100 + h;
        house->cityCredits = 200 + 2 * h;
        house->startingCredits = 12345 + 137 * h - 300 - 3 * h;
        result.credits[h] = house->getCredits();
        result.teams[h] = house->getTeamID();
    }
    for(const auto* structure : structureList)
        if(!structure->getOwner()->isNatives() && structure->getHealth() > 0)
            ++result.structureCounts[{structure->getOwner()->getHouseID(), structure->getItemID()}];
    for(const auto* unit : unitList) if(unit->getHealth() > 0) {
        if(unit->getOwner()->isNatives()) result.nativeWorms += unit->getItemID() == Unit_Sandworm;
        else ++result.unitCounts[{unit->getOwner()->getHouseID(), unit->getItemID()}];
    }
    result.terrain.reserve(result.width * result.height);
    for(int y = 0; y < result.height; ++y) for(int x = 0; x < result.width; ++x)
        result.terrain.push_back(currentGameMap->getTile(x, y)->getType());
    return result;
}

void selectMoment(SaveMapWindow& dialog, Moment wanted, const std::string& capture) {
    menuRequire(dialog.moment.getNumEntries() == 2
                && dialog.moment.getEntry(0) == _("From the start")
                && dialog.moment.getEntry(1) == _("From right now")
                && dialog.moment.getEntryIntData(0) == static_cast<int>(Moment::Start)
                && dialog.moment.getEntryIntData(1) == static_cast<int>(Moment::Current),
                "Save as Map moment labels or enum data are wrong");
    auto& box = dialog.moment;
    box.handleMouseLeft(3, box.getSize().y / 2, true);
    menuRequire(box.bShowListBox, "Save as Map moment selector does not open by mouse");
    const auto bounds = researchMenuBounds(dialog, box);
    const auto popup = box.listBox.getSize();
    const int popupY = box.bListBoxAbove ? bounds.y - popup.y : bounds.y + bounds.h;
    menuRequire(popupY >= 0 && popupY + popup.y <= getRendererHeight()
                && popup.y >= 2 * GUIStyle::getInstance().getListBoxEntryHeight() + 2,
                "Save as Map moment dropdown clips its two choices");
    menuCapture(dialog, capture + "-moment", true);
    const int row = wanted == Moment::Start ? 0 : 1;
    const int height = GUIStyle::getInstance().getListBoxEntryHeight();
    box.handleMouseLeftOverlay(3, 1 + row * height + height / 2
        + (box.bListBoxAbove ? -popup.y : box.getSize().y), true);
    menuRequire(!box.bShowListBox && box.getSelectedEntryIntData() == static_cast<int>(wanted),
                "Save as Map actual dropdown click selected the wrong snapshot moment");
    const auto& explanation = dialog.details.getText();
    menuRequire(wanted == Moment::Start
                    ? explanation.find(_("Exports the match's embedded map unchanged.")) != std::string::npos
                    : explanation.find(_("City growth, research and queues are not saved.")) != std::string::npos
                        && explanation.find(_("Use Save Game to resume the complete match.")) != std::string::npos,
                "Save as Map does not explain the chosen moment or its snapshot losses");
}

ExportedMap saveThroughPause(Game& game, const GameInitSettings& init, const Snapshot& snapshot,
                            const std::string& name, Moment moment, bool exerciseOverwrite) {
    const auto before = game.computeStateDigest();
    const auto data = MatchMapExport::mapData(game, moment);
    const auto path = MatchMapExport::destination(name, data);
    menuRequire(!existsFile(path), "Save as Map fixture output already exists in its isolated profile");
    InGameMenu pause(false, getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3));
    menuRequire(pause.saveMapButton.isVisible() && pause.saveMapButton.isEnabled()
                && pause.saveMapButton.getParent(), "Custom/skirmish pause menu hides Save as Map");
    requirePauseFit(pause);
    menuCapture(pause, name + "-pause");
    click(pause.saveMapButton);
    auto* dialog = dynamic_cast<SaveMapWindow*>(pause.pChildWindow);
    menuRequire(dialog && dialog->title.getText() == _("Save as Map")
                && dialog->moment.getSelectedEntryIntData() == static_cast<int>(Moment::Start)
                && dialog->mapName.getText() == MatchMapExport::defaultName(game)
                && !dialog->mapName.getText().empty() && dialog->getSavedPath().empty(),
                "Pause Save as Map does not open a named, start-defaulted real dialog");
    if(moment == Moment::Start) {
        dialog->mapName.setText("../save-as-map-escape");
        dialog->onSave();
        menuRequire(dialog->getSavedPath().empty() && !existsFile(path),
                    "Invalid Save as Map name wrote a file or reported success");
        auto* failure = dynamic_cast<MsgBox*>(dialog->pChildWindow);
        menuRequire(failure != nullptr, "Invalid map name did not show an error");
        requireFit(*failure, {&failure->textLabel, &failure->okbutton});
        menuCapture(*dialog, name + "-invalid-name");
        dismissMessage(*dialog);
    }
    dialog->mapName.setText(name);
    selectMoment(*dialog, moment, name);
    requireDialogFit(*dialog);
    menuCapture(*dialog, name + "-dialog");
    click(dialog->saveButton);
    menuRequire(dialog->getSavedPath() == path && existsFile(path) && readCompleteFile(path) == data,
                "Save as Map dialog did not write the exact requested map bytes");
    menuCapture(*dialog, name + "-saved");
    dismissMessage(*dialog);
    if(exerciseOverwrite) {
        const auto sentinel = data + "\r\n; overwrite confirmation sentinel\r\n";
        requiresRuntimeError([&] { MatchMapExport::save(path, sentinel); },
                             "Map export API replaced a file without overwrite approval");
        menuRequire(readCompleteFile(path) == data, "Rejected API overwrite changed map bytes");
        MatchMapExport::save(path, sentinel, true);
        menuRequire(readCompleteFile(path) == sentinel, "Confirmed API overwrite did not preserve exact CRLF bytes");
        dialog->onSave();
        auto* question = dynamic_cast<QstBox*>(dialog->pChildWindow);
        menuRequire(question && readCompleteFile(path) == sentinel,
                    "Save as Map overwrote a file before asking for confirmation");
        menuCapture(*dialog, name + "-overwrite");
        question->onButton(QSTBOX_BUTTON2);
        dialog->processChildWindowOpenCloses();
        menuRequire(!dialog->hasChildWindow() && readCompleteFile(path) == sentinel,
                    "Cancelling Save as Map overwrite changed existing map bytes");
        dialog->onSave();
        question = dynamic_cast<QstBox*>(dialog->pChildWindow);
        menuRequire(question != nullptr, "Save as Map overwrite retry lost confirmation");
        question->onButton(QSTBOX_BUTTON1);
        dialog->processChildWindowOpenCloses();
        menuRequire(dialog->getSavedPath() == path && readCompleteFile(path) == data,
                    "Confirmed Save as Map overwrite did not write the chosen snapshot");
        dismissMessage(*dialog);
    }
    click(dialog->cancelButton);
    pause.processChildWindowOpenCloses();
    menuRequire(!pause.hasChildWindow() && readCompleteFile(path) == data,
                "Closing Save as Map changed its completed file");
    // A fresh Cancel is not a save, even after another export succeeded in this pause menu.
    pause.onSaveMap();
    dialog = dynamic_cast<SaveMapWindow*>(pause.pChildWindow);
    menuRequire(dialog != nullptr, "Pause menu cannot reopen Save as Map");
    const auto cancelledName = name + "-cancelled";
    const auto cancelledPath = MatchMapExport::destination(cancelledName, data);
    dialog->mapName.setText(cancelledName);
    menuRequire(dialog->getSavedPath().empty(), "A new Save as Map dialog inherited another dialog's result");
    click(dialog->cancelButton);
    pause.processChildWindowOpenCloses();
    menuRequire(!existsFile(cancelledPath), "Cancelling Save as Map created a map");
    menuRequire(game.computeStateDigest() == before && game.getGameInitSettings().getFiledata() == init.getFiledata(),
                "Exporting a map changed the live simulation or its original embedded map");
    const auto expectedParent = std::filesystem::path(std::getenv("DUNECITY_USERDIR")) / "maps/multiplayer";
    menuRequire(std::filesystem::path(path).parent_path().lexically_normal() == expectedParent.lexically_normal()
                && MatchMapExport::playerCount(data) == static_cast<int>(init.getHouseInfoList().size()),
                "Save as Map chose the wrong user map category or lost player starts");
    return {name, path, data, init.getModName(), init.getGameOptions(), moment, snapshot};
}

void requireEditorSnapshot(MapEditor& editor, const Snapshot& snapshot) {
    const auto& map = editor.getMap();
    for(int y = 0; y < snapshot.height; ++y) for(int x = 0; x < snapshot.width; ++x) {
        const int terrain = snapshot.terrain[y * snapshot.width + x];
        menuRequire(map(x, y) == (terrain == Terrain_Slab ? Terrain_Rock : terrain),
                    "Editor current-map terrain differs from the running match");
    }
    for(const auto& expected : snapshot.structures) {
        int matches = 0;
        for(const auto& actual : editor.getStructureList())
            if(actual.house == expected.house && actual.itemID == expected.item && actual.position == expected.position) {
                ++matches;
                menuRequire(actual.health == expected.health, "Editor lost current structure/wall damage");
            }
        menuRequire(matches == 1, "Editor lost or duplicated a newly placed current structure");
    }
    for(const auto& actual : editor.getStructureList())
        menuRequire(!(actual.house == snapshot.removedStructure.house && actual.itemID == snapshot.removedStructure.item
                      && actual.position == snapshot.removedStructure.position),
                    "Current map resurrected a destroyed starting structure");
    Counts structures, units;
    for(const auto& actual : editor.getStructureList())
        if(actual.itemID != Structure_Slab1 && actual.itemID != Structure_Slab4 && actual.itemID != Structure_Road)
            ++structures[{actual.house, actual.itemID}];
    int worms = 0;
    for(const auto& actual : editor.getUnitList()) {
        if(actual.itemID == Unit_Sandworm) ++worms; // An unused ordinary faction token in the map format.
        else ++units[{actual.house, actual.itemID}];
    }
    menuRequire(worms == snapshot.nativeWorms, "Editor lost the independent native sandworms");
    menuRequire(structures == snapshot.structureCounts && units == snapshot.unitCounts,
                "Editor current map lost or duplicated the live per-house buildings or army");
    for(const auto& expected : snapshot.units) {
        int matches = 0;
        for(const auto& actual : editor.getUnitList())
            if(actual.house == expected.house && actual.itemID == expected.item && actual.position == expected.position
                && actual.health == expected.health && actual.angle == expected.angle && actual.attackmode == expected.mode) ++matches;
        menuRequire(matches == 1, "Editor lost current unit position, damage, facing, orders or shared infantry tile");
    }
    for(const auto& [house, credits] : snapshot.credits) {
        bool found = false;
        for(const auto& player : editor.getPlayers()) if(player.bActive && player.house == house) {
            found = true;
            menuRequire(!player.bAnyHouse && player.credits == credits
                        && player.brain == "Team" + std::to_string(snapshot.teams.at(house)),
                        "Editor current map lost house identity, credits or teams");
        }
        menuRequire(found, "Editor current map lost a live house");
    }
    for(int dx = 0; dx < 3; ++dx) {
        int concrete = 0, roads = 0;
        for(const auto& structure : editor.getStructureList()) if(structure.position == snapshot.patch + Coord(dx, 2)) {
            concrete += structure.itemID == Structure_Slab1;
            roads += structure.itemID == Structure_Road;
        }
        menuRequire(concrete == (dx != 1) && roads == (dx != 0),
                    "Editor lost independent concrete/road layers on the same tile");
    }
}

void requireReloadedSnapshot(Game& game, const Snapshot& snapshot) {
    for(int y = 0; y < snapshot.height; ++y) for(int x = 0; x < snapshot.width; ++x)
        menuRequire(currentGameMap->getTile(x, y)->getType() == snapshot.terrain[y * snapshot.width + x],
                    "Reloaded current map does not retain the match's terrain and foundations");
    Counts structures, units;
    for(const auto* actual : structureList) if(!actual->getOwner()->isNatives())
        ++structures[{actual->getOwner()->getHouseID(), actual->getItemID()}];
    int worms = 0;
    for(const auto* actual : unitList) {
        if(actual->getItemID() == Unit_Sandworm) {
            ++worms;
            menuRequire(actual->getOwner()->isNatives(), "Reloaded sandworm became a playable faction's unit");
        } else if(!actual->getOwner()->isNatives()) {
            ++units[{actual->getOwner()->getHouseID(), actual->getItemID()}];
        }
    }
    menuRequire(worms == snapshot.nativeWorms, "Starting the current map lost or duplicated native sandworms");
    menuRequire(structures == snapshot.structureCounts && units == snapshot.unitCounts,
                "Starting the current map lost or duplicated a live building or unit");
    for(const auto& expected : snapshot.structures) {
        int matches = 0;
        for(const auto* actual : structureList)
            if(actual->getOwner()->getHouseID() == expected.house && actual->getItemID() == expected.item
                && actual->getLocation() == expected.position && health256(*actual) == expected.health) ++matches;
        menuRequire(matches == 1, "Starting the current map lost a placed building or its damage");
    }
    for(const auto& expected : snapshot.units) {
        int matches = 0;
        for(const auto* actual : unitList)
            if(actual->getOwner()->getHouseID() == expected.house && actual->getItemID() == expected.item
                && actual->getLocation() == expected.position && health256(*actual) == expected.health
                && actual->getAngle() == expected.angle && actual->getAttackMode() == expected.mode) ++matches;
        menuRequire(matches == 1, "Starting the current map lost unit state or could not stack its two infantry");
    }
    for(const auto& [house, credits] : snapshot.credits)
        menuRequire(game.getHouse(house) && game.getHouse(house)->getCredits() == credits,
                    "Starting the current map did not restore each house's complete current credit balance");
    for(int dx = 0; dx < 3; ++dx) {
        const auto* tile = currentGameMap->getTile(snapshot.patch + Coord(dx, 2));
        menuRequire(tile->isConcrete() == (dx != 1) && tile->isRoad() == (dx != 0),
                    "Starting the current map lost the concrete/road overlay combination");
    }
}

void verifyListsAndStarts(const std::vector<ExportedMap>& maps) {
    {
        LoadMapWindow chooser;
        chooser.onMapTypeChange(1);
        for(const auto& exported : maps) {
            int found = -1;
            for(int i = 0; i < chooser.mapList.getNumEntries(); ++i)
                if(chooser.mapList.getEntry(i) == exported.name) found = i;
            menuRequire(found >= 0, "Editor MP User Maps does not list an in-game exported map");
            chooser.mapList.setSelectedItem(found);
            chooser.onLoad();
            menuRequire(chooser.getLoadMapFilepath() == exported.path && !chooser.isLoadMapSingleplayer(),
                        "Editor chooser cannot select an in-game exported map");
        }
        menuCapture(chooser, "save-as-map-editor-list");
    }
    for(const auto& exported : maps) {
        menuRequire(RandomSkirmish::activateMod(exported.mod), "Cannot activate exported map's game mod");
        {
            MapEditor editor;
            editor.loadMap(exported.path);
            menuRequire(editor.getMap().getSizeX() == exported.snapshot.width
                        && editor.getMap().getSizeY() == exported.snapshot.height
                        && !editor.getStructureList().empty() && !editor.getUnitList().empty(),
                        "Editor cannot load a generated in-game exported map");
            if(exported.moment == Moment::Current) requireEditorSnapshot(editor, exported.snapshot);
        }
        CustomPlaySetup setup;
        setup.mods = ModManager::instance().listModChoices();
        setup.rules = exported.rules;
        bool modFound = false;
        for(size_t i = 0; i < setup.mods.size(); ++i) if(setup.mods[i].name == exported.mod) {
            setup.mod = static_cast<int>(i); modFound = true;
        }
        menuRequire(modFound, "Custom Game cannot offer the exported map's original mod");
        {
            CustomGameMenu browser(false, false, &setup);
            menuRequire(browser.selectUserMap(exported.path), "Custom Game MP User Maps hides an in-game exported map");
            browser.onMapListSelectionChange(false);
            menuRequire(browser.getSelectedMapPath() == exported.path && browser.nextButton.isEnabled(),
                        "Custom Game cannot preview or select an in-game exported map");
            const auto& entry = browser.mapEntries[browser.visibleMaps[browser.mapList.getSelectedIndex()]];
            menuRequire(entry.metadata.width == exported.snapshot.width && entry.metadata.height == exported.snapshot.height
                        && entry.metadata.players == static_cast<int>(exported.snapshot.credits.size())
                        && entry.metadata.name == exported.name,
                        "Custom Game misreads exported dimensions, house starts or the chosen map name");
            menuCapture(browser, exported.name + "-custom");
            browser.onNext();
            menuRequire(browser.isQuiting() && setup.maps.at(setup.map) == exported.path && !setup.online,
                        "Custom Game Next does not hand the exported map to its offline lobby");
        }
        GameInitSettings replay(getBasename(exported.path, true), readCompleteFile(exported.path), false, setup.rules);
        {
            CustomGamePlayers lobby(replay, true, false, &setup);
            lobby.update();
            menuRequire(lobby.numHouses == MatchMapExport::playerCount(exported.bytes),
                        "Custom Game lobby cannot seat all exported houses");
            const int ai = PlayerFactory::getIndexByPlayerClass("qBotEasy");
            menuRequire(ai >= 0, "Save as Map fixture cannot find the real QuantBot Easy controller");
            int humans = 0;
            for(int row = 0; row < lobby.numHouses; ++row) {
                if(lobby.getSeatPlayer(2 * row) == 0) ++humans; // PLAYER_HUMAN in the real lobby.
                else {
                    menuRequire(lobby.selectSeatPlayer(2 * row, ai), "Exported lobby cannot assign an opponent");
                    lobby.onChangePlayerDropDownBoxes(true, 2 * row);
                }
            }
            menuRequire(humans == 1, "Exported map's offline lobby did not seat exactly one local human");
            lobby.addAllPlayersToGameInitSettings();
            replay = lobby.gameInitSettings;
            menuRequire(replay.getFiledata() == exported.bytes
                        && replay.getHouseInfoList().size() == exported.snapshot.credits.size(),
                        "Custom Game start changed exported map bytes or dropped a house");
        }
        {
            Match match(replay);
            menuRequire(currentGameMap->getSizeX() == exported.snapshot.width
                        && currentGameMap->getSizeY() == exported.snapshot.height
                        && match.game->isCitySimEnabled() == (exported.mod == "CivWars"),
                        "Exported map cannot start with its selected mod and dimensions");
            if(exported.moment == Moment::Current) requireReloadedSnapshot(*match.game, exported.snapshot);
            match.advance(16);
        }
        menuRequire(readCompleteFile(exported.path) == exported.bytes,
                    "Editor, Custom Game or starting a match changed an exported file");
    }
}

void verifyAvailability(const GameInitSettings& randomInit) {
    {
        auto watched = randomInit;
        watched.setLocalSpectator(true);
        for(auto& house : watched.houseInfoList)
            for(auto& player : house.playerInfoList) player.playerClass = "qBotEasy";
        Match match(watched);
        match.advance(16);
        menuRequire(match.game->isObserving() && !pNetworkManager && MatchMapExport::available(*match.game),
                    "Offline spectator cannot export a custom/skirmish map");
        menuRequire(MatchMapExport::mapData(*match.game, Moment::Start) == watched.getFiledata(),
                    "Offline spectator start export is not the embedded generated map");
        InGameMenu pause(false, getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3));
        menuRequire(pause.saveMapButton.isVisible() && pause.saveMapButton.isEnabled(),
                    "Offline spectator pause menu hides Save as Map");
        requirePauseFit(pause);
        menuCapture(pause, "save-as-map-offline-spectator-pause");
        pause.onSaveMap();
        auto* dialog = dynamic_cast<SaveMapWindow*>(pause.pChildWindow);
        menuRequire(dialog != nullptr, "Offline spectator Save as Map does not open its dialog");
        requireDialogFit(*dialog);
        menuCapture(*dialog, "save-as-map-offline-spectator-dialog");
        click(dialog->cancelButton);
        pause.processChildWindowOpenCloses();
    }
    menuRequire(RandomSkirmish::activateMod("vanilla"), "Cannot activate campaign export visibility fixture");
    {
        GameInitSettings campaign(HOUSE_ATREIDES, effectiveGameOptions, 1);
        Match match(campaign);
        menuRequire(!MatchMapExport::available(*match.game), "Campaign incorrectly permits Save as Map");
        InGameMenu pause(false, getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3));
        menuRequire(!pause.saveMapButton.isVisible() && !pause.saveMapButton.isEnabled(),
                    "Campaign pause menu exposes Save as Map");
        pause.onSaveMap();
        menuRequire(!pause.hasChildWindow(), "Campaign callback bypasses hidden Save as Map");
        requiresRuntimeError([&] { MatchMapExport::mapData(*match.game, Moment::Start); },
                             "Campaign start map export was not rejected");
        requiresRuntimeError([&] { MatchMapExport::mapData(*match.game, Moment::Current); },
                             "Campaign current map export was not rejected");
        requirePauseFit(pause);
        menuCapture(pause, "save-as-map-campaign-hidden");
    }
}

} // namespace SaveAsMapProbe

void verifySaveAsMap() {
    using namespace SaveAsMapProbe;
    menuRequire(std::getenv("DUNECITY_USERDIR") && !pNetworkManager && MenuTheme::isDesertUISkin(),
                "Save as Map fixture requires the isolated offline profile and real desert UI");
    const auto previousMod = ModManager::instance().getActiveModName();
    const auto previousOptions = effectiveGameOptions;
    const auto previousCustomMod = settings.general.customGameMod;
    const auto previousSkin = settings.general.duneCityCampaignSkin;
    INIFile previousConfig(getConfigFilepath());
    GameInitSettings randomInit;
    std::vector<ExportedMap> exported;
    for(bool civWars : {false, true}) {
        GameInitSettings init;
        if(civWars) {
            CivWarsMenu generator;
            CivWarsGenerator::Params params;
            params.teamSize = 1; params.size = 0; params.wormsPerLane = 1;
            params.house = HOUSE_ATREIDES; params.seed = 20261104; params.fixedSeed = true;
            params.enemyClass = "qBotEasy";
            generator.showParams(params);
            menuRequire(generator.prepareStart(init) && init.isCivilizationWars(),
                        "Real Civilization Wars menu could not prepare the export fixture");
        } else {
            menuRequire(RandomSkirmish::activateMod("vanilla"), "Cannot activate random export fixture");
            RandomSkirmishMenu generator;
            for(size_t i = 0; i < generator.availableMods.size(); ++i)
                if(generator.availableMods[i].name == "vanilla") generator.modBox.setSelectedItem(static_cast<int>(i));
            generator.onModChanged();
            SkirmishGenerator::Params params;
            params.mapSize = 96; params.house = HOUSE_ATREIDES; params.seed = 20261104; params.fixedSeed = true;
            params.playerSide.base = params.enemySide.base = SkirmishGenerator::BaseSize::Small;
            params.playerSide.army = params.enemySide.army = 1; params.enemySide.playerClass = "qBotEasy";
            generator.showParams(params);
            menuRequire(generator.prepareStart(init), "Real Random Skirmish menu could not prepare the export fixture");
            randomInit = init;
        }
        Match match(init);
        match.advance(32);
        const auto snapshot = changeCurrent(*match.game);
        menuRequire(MatchMapExport::available(*match.game) && !MatchMapExport::defaultName(*match.game).empty(),
                    "Generated custom match has no map-export capability or default name");
        const auto start = MatchMapExport::mapData(*match.game, Moment::Start);
        const auto current = MatchMapExport::mapData(*match.game, Moment::Current);
        menuRequire(start == init.getFiledata() && current != start
                    && current == MatchMapExport::mapData(*match.game, Moment::Current),
                    "Start is not byte-exact, current ignores mutations, or current export is nondeterministic");
        if(civWars) {
            auto rw = sdl2::RWops_ptr(SDL_RWFromConstMem(current.data(), static_cast<int>(current.size())));
            menuRequire(rw != nullptr, "Cannot parse current Civilization Wars export");
            INIFile parsed(rw.get());
            menuRequire(parsed.getIntValue("CIVILIZATIONWARS", "Version", 0) == 1,
                        "Current Civilization Wars export lost its playable mode marker");
        }
        for(const std::string invalid : {"", " ", ".", "..", "../escape", "nested/map", "nested\\map"})
            requiresRuntimeError([&] { MatchMapExport::destination(invalid, start); },
                                 "Map export accepted an invalid or traversing filename");
        requiresRuntimeError([&] { MatchMapExport::save(std::filesystem::path(std::getenv("DUNECITY_USERDIR")).string(), start); },
                             "Map export accepted a directory as a writable map file");
        const std::string prefix = civWars ? "save-as-map-civ-wars" : "save-as-map-random";
        exported.push_back(saveThroughPause(*match.game, init, snapshot, prefix + "-start", Moment::Start, false));
        menuRequire(readCompleteFile(exported.back().path) == init.getFiledata(),
                    "Start file differs byte-for-byte from the generator's embedded map");
        exported.push_back(saveThroughPause(*match.game, init, snapshot, prefix + "-current", Moment::Current, true));
    }
    const std::string onePlayer = "[BASIC]\nVersion=2\n[MAP]\nSizeX=2\nSizeY=2\n000=--\n001=--\n[Player1]\nCredits=1000\n";
    const auto twoPlayers = onePlayer + "[Player4]\nCredits=2000\n";
    menuRequire(MatchMapExport::playerCount(onePlayer) == 1 && MatchMapExport::playerCount(twoPlayers) == 2,
                "Map export miscounts one-player or sparse PlayerN starts");
    const auto user = std::filesystem::path(std::getenv("DUNECITY_USERDIR"));
    menuRequire(std::filesystem::path(MatchMapExport::destination("save-as-map-one-player", onePlayer)).parent_path().lexically_normal()
                    == (user / "maps/singleplayer").lexically_normal()
                && std::filesystem::path(MatchMapExport::destination("save-as-map-two-players", twoPlayers)).parent_path().lexically_normal()
                    == (user / "maps/multiplayer").lexically_normal(),
                "Map export destination does not distinguish SP and MP user maps");
    requiresRuntimeError([] { MatchMapExport::playerCount(""); }, "Map export accepted empty map data");
    verifyAvailability(randomInit);
    verifyListsAndStarts(exported);
    menuRequire(previousConfig.saveChangesTo(getConfigFilepath()), "Cannot restore the Save as Map fixture profile choices");
    settings.general.customGameMod = previousCustomMod;
    settings.general.duneCityCampaignSkin = previousSkin;
    menuRequire(RandomSkirmish::activateMod(previousMod), "Cannot restore the mod after Save as Map fixture");
    effectiveGameOptions = previousOptions;
    // The navigation runner accepts exactly one final MENU_PROBE_PASS line.
    SDL_Log("SAVE_AS_MAP_OK: Random Skirmish and Civilization Wars start/current exact files, live mutations,"
            " editor and Custom Game lists, four playable starts, independent worms, spectator/campaign policy, safe names,"
            " confirmed/cancelled overwrites, real dialog clicks and 640x480 crest-safe controls");
}
