// Diagnostic-only entry point, included by tests/menu/run-ui-gallery-probe.py into a private copy
// of main.cpp; the shipped executable never includes it. For one UI skin at one logical size it
// renders the real menus and a real DuneCity game (sidebar, inspect panels, pause, chat, ticker,
// F12, city budget, house statistics, a spectated game's end plate and statistics), saves each
// frame and lints every visible widget of every window: inside the screen, label text fits its
// box (after the label's own word wrap), button lettering fits at the size the skin's fitting
// loop picks, and text controls do not overlap one another. Layout findings are reported as
// UI_GALLERY_ISSUE lines (the runner compares them across skins); broken behaviour fails.
//
// Stages (one process): 0 = everything in the starting skin, then Apply the next skin from the
// real Options menu (video reinitialization); 1 = in-game sidebar in that skin, Apply the
// starting skin back; 2 = in-game sidebar again, which must be pixel-identical to stage 0 (no
// stale sprite/style caches after switching), and the profile keeps the starting skin.
#include <Menu/MainMenu.h>
#include <Menu/SinglePlayerMenu.h>
#include <Menu/RandomSkirmishMenu.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/CustomGameStatsMenu.h>
#include <Menu/HouseChoiceMenu.h>
#include <Menu/MultiPlayerMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/ModMenu.h>
#include <Menu/MentatMenu.h>
#include <MapEditor/MapEditor.h>
#include <MapEditor/MapEditorInterface.h>
#include <MapEditor/SkirmishGenerator.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/StaticContainer.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/Checkbox.h>
#include <GUI/DropDownBox.h>
#include <GUI/Spacer.h>
#include <GUI/dune/DuneStyle.h>
#include <GUI/dune/InGameMenu.h>
#include <GUI/dune/CityBudgetWindow.h>
#include <GUI/dune/HouseStatsWindow.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <Game.h>
#include <GameInterface.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <mod/ModManager.h>
#include <misc/MenuPalette.h>
#include <misc/draw_util.h>
#include <misc/string_util.h>
#include <misc/FileSystem.h>
#include <misc/WatchedResultLayout.h>
#include <FileClasses/GFXManager.h>
#include <array>

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace UIGallery {

int issueCount = 0;
int screenCount = 0;

void require(bool ok, const std::string& what) {
    if(!ok) { SDL_Log("UI_GALLERY_FAIL: %s", what.c_str()); throw std::runtime_error(what); }
}

void issue(const std::string& screen, const std::string& kind, const std::string& detail) {
    ++issueCount;
    SDL_Log("UI_GALLERY_ISSUE: screen=%s kind=%s %s", screen.c_str(), kind.c_str(), detail.c_str());
}

std::string outPath(const std::string& name) {
    const char* out = std::getenv("MENU_PROBE_OUT");
    require(out && *out, "MENU_PROBE_OUT is required");
    return (std::filesystem::path(out) / (name + ".png")).string();
}

std::string rectText(const SDL_Rect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
}

// By value: an exact match for any string argument, so ADL's std::quoted (a template) never wins.
std::string quoted(std::string text) {
    std::string result = "'";
    for(char c : text) result += (c == '\n' ? '|' : c);
    return result + "'";
}

DuneStyle& style() { return static_cast<DuneStyle&>(GUIStyle::getInstance()); }

/// The size the skin's own fitting loop (DuneStyle::createButtonSurface) gives a button label,
/// and whether the lettering then fits; mirrors that loop with the same font calls.
std::pair<int, bool> buttonFit(const std::string& text, int width, int height) {
    std::string label = text;
    const int skin = MenuTheme::uiSkin;
    if(MenuTheme::isDesertUISkin(skin))
        for(char& c : label) if(c >= 'a' && c <= 'z') c -= 'a' - 'A';
    const bool display = MenuTheme::isLapisEnamel(skin) && height >= 28;
    const bool retro = MenuTheme::isClassicDesert(skin);
    int size = height >= 48 ? 22 : height >= 38 ? 20 : height >= 30 ? 18 : height >= 24 ? 16 : 14;
    if(display) size += 4;
    const auto w = [&](int s) {
        return retro ? pFontManager->getRetroTextWidth(label, s)
                     : display ? pFontManager->getDisplayTextWidth(label, s) : static_cast<int>(style().getTextWidth(label, s));
    };
    const auto h = [&](int s) {
        return retro ? pFontManager->getRetroTextHeight(label, s)
                     : display ? pFontManager->getDisplayTextHeight(label, s) : static_cast<int>(style().getTextHeight(s));
    };
    while(size > 8 && (width < w(size) + 12 || height < h(size) + 4)) --size;
    return {size, width >= w(size) + 12 && height >= h(size) + 4};
}

/// rect: the widget's box; ink: where its lettering actually is (labels are often wider boxes).
struct Leaf { Widget* widget; SDL_Rect rect; std::string text; SDL_Rect ink; };

void walk(Widget* widget, Point origin, std::vector<Leaf>& leaves);

template<class C> void walkContainer(C* container, Point origin, std::vector<Leaf>& leaves) {
    for(const auto& data : container->containedWidgets) {
        if(!data.pWidget || !data.pWidget->isVisible()) continue;
        walk(data.pWidget, origin + container->getWidgetPosition(data.pWidget), leaves);
    }
}

void walk(Widget* widget, Point origin, std::vector<Leaf>& leaves) {
    if(!widget || !widget->isVisible()) return;
    if(auto* c = dynamic_cast<StaticContainer*>(widget)) { walkContainer(c, origin, leaves); return; }
    if(auto* c = dynamic_cast<HBox*>(widget)) { walkContainer(c, origin, leaves); return; }
    if(auto* c = dynamic_cast<VBox*>(widget)) { walkContainer(c, origin, leaves); return; }
    if(auto* w = dynamic_cast<Window*>(widget)) {
        if(w->pWindowWidget) walk(w->pWindowWidget, origin + w->getPosition(), leaves);
        return;
    }
    const SDL_Rect rect{origin.x, origin.y, widget->getSize().x, widget->getSize().y};
    std::string text;
    if(auto* l = dynamic_cast<Label*>(widget)) text = l->getText();
    else if(auto* b = dynamic_cast<TextButton*>(widget)) text = b->getText();
    else if(auto* b = dynamic_cast<Checkbox*>(widget)) text = b->getText();
    else if(auto* d = dynamic_cast<DropDownBox*>(widget)) text = d->getSelectedEntry();
    leaves.push_back({widget, rect, text, rect});
}

/// Lints the visible widgets of a window (and an open child window).
void lint(Window& window, const std::string& screen, bool checkOverlap = true) {
    std::vector<Leaf> leaves;
    walk(&window, Point(0, 0), leaves);
    const int W = getRendererWidth(), H = getRendererHeight();
    int textWidgets = 0;
    for(auto& leaf : leaves) {
        const auto r = leaf.rect;
        if(r.w <= 0 || r.h <= 0) continue;
        const bool hasText = !leaf.text.empty();
        if(r.x < 0 || r.y < 0 || r.x + r.w > W || r.y + r.h > H)
            issue(screen, "offscreen", rectText(r) + " text=" + quoted(leaf.text));
        if(auto* label = dynamic_cast<Label*>(leaf.widget); label && hasText) {
            ++textWidgets;
            const int fontSize = label->fontSize;
            const bool outlined = MenuTheme::isDesertUISkin() && label->backgroundcolor == COLOR_TRANSPARENT
                                  && label->textshadowcolor == COLOR_BLACK;
            const auto measure = [&](const std::string& t) {
                return outlined ? pFontManager->getTextWidth(t, fontSize) : static_cast<int>(style().getTextWidth(t, fontSize));
            };
            const auto lines = greedyWordWrap(label->getText(), r.w, [&](const std::string& t) {
                return style().getMinimumLabelSize(t, fontSize).x - 4;
            });
            int widest = 0;
            for(const auto& line : lines) widest = std::max(widest, measure(line));
            const int lineHeight = outlined ? pFontManager->getTextHeight(fontSize) : static_cast<int>(style().getTextHeight(fontSize));
            const int textHeight = lineHeight * static_cast<int>(lines.size()) + 2 * (static_cast<int>(lines.size()) - 1);
            // Lettering starts 3 px in (DuneStyle::createLabelSurface); 1-2 px of empty descent is invisible.
            if(widest + 3 > r.w + 1)
                issue(screen, "label-clipped-x", rectText(r) + " needs " + std::to_string(widest + 3) + " font=" + std::to_string(fontSize) + " text=" + quoted(leaf.text));
            if(textHeight > r.h + 3)
                issue(screen, "label-clipped-y", rectText(r) + " needs " + std::to_string(textHeight) + " lines=" + std::to_string(lines.size()) + " font=" + std::to_string(fontSize) + " text=" + quoted(leaf.text));
            const int inkW = std::min(r.w, widest + 6), inkH = std::min(r.h, textHeight + 4);
            const int alignment = label->alignment;
            leaf.ink.w = inkW;
            leaf.ink.h = inkH;
            leaf.ink.x = (alignment & Alignment_Right) ? r.x + r.w - inkW
                       : (alignment & Alignment_Left) ? r.x : r.x + (r.w - inkW) / 2;
            leaf.ink.y = (alignment & Alignment_Bottom) ? r.y + r.h - inkH
                       : (alignment & Alignment_Top) ? r.y : r.y + (r.h - inkH) / 2;
        } else if(dynamic_cast<TextButton*>(leaf.widget) && hasText) {
            ++textWidgets;
            const auto [size, fits] = buttonFit(leaf.text, r.w, r.h);
            if(!fits) issue(screen, "button-clipped", rectText(r) + " text=" + quoted(leaf.text));
            else if(size < 10) issue(screen, "button-tiny-font", rectText(r) + " size=" + std::to_string(size) + " text=" + quoted(leaf.text));
        } else if(dynamic_cast<Checkbox*>(leaf.widget) && hasText) {
            ++textWidgets;
            const auto minimum = style().getMinimumCheckboxSize(leaf.text);
            if(minimum.x > r.w + 1) issue(screen, "checkbox-clipped", rectText(r) + " needs " + std::to_string(minimum.x) + " text=" + quoted(leaf.text));
        } else if(dynamic_cast<DropDownBox*>(leaf.widget) && hasText) {
            ++textWidgets;
            // DuneStyle::createListBoxEntry: 14 pt from x=3 in a (width - 17) entry beside the arrow.
            const int needed = static_cast<int>(style().getTextWidth(leaf.text, 14)) + 3;
            if(needed > r.w - 17) issue(screen, "dropdown-clipped", rectText(r) + " needs " + std::to_string(needed + 17) + " text=" + quoted(leaf.text));
        }
    }
    // Both desert skins draw the original menu background (PictureFactory::createMainBackground):
    // 53x54 house crests 11 px in from the top-left, top-right and bottom-left corners. No
    // widget may cover one (Stefan, 2026-10-24: keep the crests, move the menus). A failure:
    // the runner fails the case on any over-corner-crest finding.
    if(MenuTheme::isDesertUISkin() && window.pBackground.get() == pGFXManager->getUIGraphic(UI_MenuBackground)
       && window.getPosition().x == 0 && window.getPosition().y == 0) {
        const std::array<SDL_Rect, 3> crests{{{11, 11, 53, 54}, {W - 11 - 53, 11, 53, 54}, {11, H - 11 - 54, 53, 54}}};
        for(const auto& leaf : leaves) {
            if(dynamic_cast<Spacer*>(leaf.widget) || dynamic_cast<HSpacer*>(leaf.widget) || dynamic_cast<VSpacer*>(leaf.widget))
                continue;
            // A label draws nothing outside its lettering (wide centred titles, empty status
            // lines); every other widget draws its whole box.
            const bool label = dynamic_cast<Label*>(leaf.widget) != nullptr;
            if(label && leaf.text.empty()) continue;
            const SDL_Rect& drawn = label ? leaf.ink : leaf.rect;
            for(const auto& crest : crests) {
                SDL_Rect both;
                if(SDL_IntersectRect(&drawn, &crest, &both) && both.w > 0 && both.h > 0)
                    issue(screen, "over-corner-crest", UIGallery::quoted(leaf.text) + "@" + rectText(drawn) + " crest@" + rectText(crest));
            }
        }
    }
    if(checkOverlap) {
        for(size_t i = 0; i < leaves.size(); ++i) {
            if(leaves[i].text.empty()) continue;
            for(size_t j = i + 1; j < leaves.size(); ++j) {
                if(leaves[j].text.empty()) continue;
                SDL_Rect both;
                if(SDL_IntersectRect(&leaves[i].ink, &leaves[j].ink, &both) && both.w > 2 && both.h > 2)
                    issue(screen, "overlap", UIGallery::quoted(leaves[i].text) + "@" + rectText(leaves[i].ink) + " vs "
                          + UIGallery::quoted(leaves[j].text) + "@" + rectText(leaves[j].ink));
            }
        }
    }
    ++screenCount;
    SDL_Log("UI_GALLERY_SCREEN: %s widgets=%zu text=%d", screen.c_str(), leaves.size(), textWidgets);
    // A child window (budget, statistics, pause settings) covers the window below it by design:
    // lint it on its own.
    if(window.pChildWindow) lint(*window.pChildWindow, screen + "/child", checkOverlap);
}

void save(const std::string& name) {
    auto image = renderReadSurface(renderer);
    require(image != nullptr, "Reading back " + name + " failed");
    require(SavePNG(image.get(), outPath(name).c_str()) == 0, "Saving " + name + " failed");
    SDL_Log("UI_GALLERY_CAPTURE: %s", outPath(name).c_str());
}

void captureWindow(Window& window, const std::string& name, bool checkOverlap = true) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    window.draw();
    window.drawOverlay();
    if(window.pChildWindow) { window.pChildWindow->draw(); window.pChildWindow->drawOverlay(); }
    save(name);
    lint(window, name, checkOverlap);
}

/// Runs one screen; a construction failure is a finding, not the end of the gallery.
void screen(const std::string& name, const std::function<void()>& body) {
    try { body(); }
    catch(const std::exception& e) { issue(name, "failed", e.what()); }
}

// ---------------------------------------------------------------- in game

sdl2::surface_ptr gameCapture(Game& game, const std::string& name) {
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    require(image != nullptr, "Reading back the game view failed");
    require(SavePNG(image.get(), outPath(name).c_str()) == 0, "Saving " + name + " failed");
    SDL_Log("UI_GALLERY_CAPTURE: %s", outPath(name).c_str());
    return image;
}

/// Exact pixels, for the skin round-trip comparison.
std::string pixelBytes(SDL_Surface* image) {
    return std::string(static_cast<const char*>(image->pixels), static_cast<size_t>(image->pitch) * image->h);
}

void lintGame(Game& game, const std::string& name) {
    // The HUD itself; text widgets of the top bar and sidebar overlap by design only where the
    // sidebar stacks panels, so overlap is checked too (findings are compared across skins).
    lint(*game.pInterface, name);
    if(game.pInGameMenu) lint(*game.pInGameMenu, name + "/menu");
}

std::unique_ptr<Game> startGame(const SkirmishGenerator::Params& p) {
    require(RandomSkirmish::activateMod(p.mod), "Mod " + p.mod + " did not activate");
    const auto result = SkirmishGenerator::generate(p, RandomSkirmish::loadCatalogue(p.mode));
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, p);
    init.randomSeed = p.seed;
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    return game;
}

void endGame(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

SkirmishGenerator::Params params(bool spectate, int enemies) {
    using namespace SkirmishGenerator;
    Params p;
    p.house = HOUSE_ORDOS;
    p.seed = 20261024;
    p.techLevel = 8;
    p.mod = "dunecity";
    p.mode = RandomSkirmish::modeForMod(p.mod);
    p.mapSize = 96;
    p.allies = 0;
    p.enemies = enemies;
    p.enemiesAllied = false;
    p.allySide = Side{"qBotEasy", BaseSize::Small, 1, 1};
    p.enemySide = Side{"qBotEasy", BaseSize::Small, 1, 1};
    p.playerSide = Side{"", BaseSize::Small, 1, 1};
    p.spectate = spectate;
    p.spectateClass = "qBotMedium";
    return p;
}

void runCycles(Game& game, Uint32 cycles) {
    const Uint32 end = game.getGameCycleCount() + cycles;
    while(game.getGameCycleCount() < end && !game.finishedLevel) game.updateGameState();
}

void select(Game& game, ObjectBase* object) {
    game.unselectAll(game.getSelectedList());
    game.getSelectedList().clear();
    if(object) {
        game.getSelectedList().insert(object->getObjectID());
        object->setSelected(true);
        screenborder->setNewScreenCenter(object->getLocation() * TILESIZE);
    }
    game.selectionChanged();
    game.pInterface->updateObjectInterface();
    game.pInterface->updateObjectInterface();
}

Coord freeSand(Game& game, Coord near) {
    for(int radius = 2; radius < 30; ++radius)
        for(int dy = -radius; dy <= radius; ++dy)
            for(int dx = -radius; dx <= radius; ++dx) {
                const Coord c = near + Coord(dx, dy);
                if(!currentGameMap->tileExists(c)) continue;
                Tile* tile = currentGameMap->getTile(c);
                if(tile->isSand() && !tile->hasAGroundObject() && !tile->hasANonInfantryGroundObject()) return c;
            }
    require(false, "No free sand near the base");
    return near;
}

std::string sidebarRoundTrip() {
    auto game = startGame(params(false, 1));
    StructureBase* yard = nullptr;
    for(auto* s : structureList) if(s->getOwner() == pLocalHouse && s->getItemID() == Structure_ConstructionYard) { yard = s; break; }
    require(yard != nullptr, "The played skirmish has no construction yard");
    select(*game, yard);
    const auto pixels = pixelBytes(gameCapture(*game, "game-sidebar-yard-switch-skin" + std::to_string(MenuTheme::uiSkin)).get());
    endGame(game);
    return pixels;
}

void runInGame() {
    auto game = startGame(params(false, 1));
    require(game->isCitySimEnabled() && !game->isObserving(), "The played DuneCity skirmish is not a played city game");
    runCycles(*game, 40);
    StructureBase* yard = nullptr;
    StructureBase* other = nullptr;
    StructureBase* enemyStructure = nullptr;
    for(auto* s : structureList) {
        if(s->getOwner() == pLocalHouse) {
            if(s->getItemID() == Structure_ConstructionYard && !yard) yard = s;
            else if(!other && s->getItemID() != Structure_ConstructionYard && s->getItemID() != Structure_Wall) other = s;
        } else if(!enemyStructure && s->getOwner()->getTeamID() != pLocalHouse->getTeamID()) enemyStructure = s;
    }
    require(yard && other, "The played skirmish lacks a yard or another building");
    UnitBase* ownUnit = nullptr;
    for(auto* u : unitList) if(u->getOwner() == pLocalHouse && u->getItemID() != Unit_Harvester) { ownUnit = u; break; }

    screen("game-sidebar-yard", [&] { select(*game, yard); gameCapture(*game, "game-sidebar-yard"); lintGame(*game, "game-sidebar-yard"); });
    screen("game-structure-inspect", [&] { select(*game, other); gameCapture(*game, "game-structure-inspect"); lintGame(*game, "game-structure-inspect"); });
    if(ownUnit) screen("game-unit-inspect", [&] { select(*game, ownUnit); gameCapture(*game, "game-unit-inspect"); lintGame(*game, "game-unit-inspect"); });
    if(enemyStructure) screen("game-enemy-structure-inspect", [&] {
        enemyStructure->setVisible(pLocalHouse->getTeamID(), true);
        select(*game, enemyStructure); gameCapture(*game, "game-enemy-structure-inspect"); lintGame(*game, "game-enemy-structure-inspect");
    });
    for(const auto& [item, name] : std::vector<std::pair<int, std::string>>{{Unit_Sandworm, "worm"}, {Unit_Trooper, "trooper"}}) {
        const std::string shot = "game-natives-" + name + "-inspect";
        screen(shot, [&, item = item] {
            House* natives = game->getOrCreateNativesHouse();
            UnitBase* unit = natives->createUnit(item);
            require(unit != nullptr, "Natives could not create the " + shot + " unit");
            unit->deploy(freeSand(*game, yard->getLocation() + Coord(4, 4)));
            unit->setVisible(pLocalHouse->getTeamID(), true);
            select(*game, unit);
            auto* panel = dynamic_cast<DefaultObjectInterface*>(game->pInterface->pObjectContainer);
            require(panel != nullptr, shot + ": no inspect panel");
            require(panel->getOwnerName() == "Natives", shot + ": owner line is '" + panel->getOwnerName() + "'");
            gameCapture(*game, shot);
            lintGame(*game, shot);
        });
    }
    select(*game, yard);
    screen("game-chat-ticker", [&] {
        auto& chat = game->pInterface->getChatManager();
        // Regression (1.0.852): the first chat lines overwrote the city's Pop/RCI readouts.
        auto& hud = game->pInterface->windowWidget;
        const auto* chatData = hud.getWidgetDataFromWidget(&chat);
        const auto* rciData = hud.getWidgetDataFromWidget(&game->pInterface->rciDemandLabel);
        require(chatData && rciData, "The HUD has no chat or RCI readout");
        require(game->pInterface->rciDemandLabel.isVisible(), "The city game hides its RCI readout");
        require(chatData->position.y >= rciData->position.y + rciData->size.y,
                "The chat starts at y=" + std::to_string(chatData->position.y) + ", over the RCI readout ending at "
                + std::to_string(rciData->position.y + rciData->size.y));
        chat.addChatMessage("Vuk", "Classic Desert chat line: does it read over the map?");
        chat.addInfoMessage("Stefan joined the game");
        game->pInterface->addToNewsTicker("Spice harvested: 1200 credits - Classic Desert ticker check");
        game->pInterface->newsticker.timer = 0;
        gameCapture(*game, "game-chat-ticker");
        lintGame(*game, "game-chat-ticker");
    });
    screen("game-f12-overlay", [&] {
        game->bShowFPS = true;
        runCycles(*game, 4);
        gameCapture(*game, "game-f12-overlay");
        game->bShowFPS = false;
    });
    screen("game-pause-menu", [&] {
        game->onOptions();
        gameCapture(*game, "game-pause-menu");
        lintGame(*game, "game-pause-menu");
        auto* pause = dynamic_cast<InGameMenu*>(game->pInGameMenu.get());
        require(pause != nullptr, "No pause menu");
        pause->onSettings(); pause->processChildWindowOpenCloses();
        gameCapture(*game, "game-pause-settings");
        lintGame(*game, "game-pause-settings");
        pause->closeChildWindow(); pause->processChildWindowOpenCloses();
        game->pInGameMenu.reset(); game->bMenu = false; game->resumeGame();
    });
    screen("game-city-budget", [&] {
        game->onCityBudget(); game->pInterface->processChildWindowOpenCloses();
        require(game->pInterface->pChildWindow != nullptr, "The city budget did not open");
        gameCapture(*game, "game-city-budget");
        lintGame(*game, "game-city-budget");
        game->pInterface->closeChildWindow(); game->pInterface->processChildWindowOpenCloses();
    });
    screen("game-house-stats", [&] {
        game->onHouseStats(); game->pInterface->processChildWindowOpenCloses();
        require(game->pInterface->pChildWindow != nullptr, "The house statistics did not open");
        gameCapture(*game, "game-house-stats");
        lintGame(*game, "game-house-stats");
        game->pInterface->closeChildWindow(); game->pInterface->processChildWindowOpenCloses();
    });
    endGame(game);
}

/// The end plate of a watched game. Three allied enemies beat the watched house, so the line is
/// a long team line ("Team 2 has won: A, B and C"); it and its plate must fit the map beside the
/// sidebar at every size (watchedResultFontSize). Then the plate in every house colour slot
/// (incl. Rebels): the winner's colour comes from its slot, which a lobby can set to any slot.
void runSpectatorEnd() {
    auto p = params(true, 3);
    p.enemiesAllied = true;
    auto game = startGame(p);
    require(game->isObserving(), "The spectated skirmish is not observed");
    runCycles(*game, 60);
    House* watched = game->getHouse(HOUSE_ORDOS);
    require(watched != nullptr && watched->isAlive(), "The watched house is missing");
    for(int round = 0; round < 64 && watched->isAlive(); ++round) {
        std::vector<Uint32> doomed;
        for(const StructureBase* s : structureList) if(s->getOwner() == watched) doomed.push_back(s->getObjectID());
        for(const UnitBase* u : unitList) if(u->getOwner() == watched || u->originalHouseID == HOUSE_ORDOS) doomed.push_back(u->getObjectID());
        for(Uint32 id : doomed) if(ObjectBase* o = game->getObjectManager().getObject(id)) o->destroy();
        if(watched->isAlive()) game->updateGameState();
    }
    for(int i = 0; i < 4 && !game->finished; ++i) game->updateGameState();
    require(game->finished && game->getWatchedWinnerTeam() > 0, "The spectated game did not end with a winner");
    const std::string text = game->watchedResultText();
    require(text.rfind("Team ", 0) == 0 && text.find(" and ") != std::string::npos, "Not a three-house team line: " + text);
    int winner = -1;   // the house whose colour the plate takes (Game::watchedResultColor)
    for(int h = 0; h < NUM_HOUSES && winner < 0; ++h)
        if(game->getHouse(h) && game->getHouse(h)->getTeamID() == game->getWatchedWinnerTeam()
           && !game->getHouse(h)->getPlayerList().empty()) winner = h;
    require(winner >= 0, "No winning house");
    // Regression (1.0.852): at 28 pt a team line was wider than the map at 640 and 854 pixels.
    const int mapWidth = game->sideBarPos.x;
    const int size = watchedResultFontSize(text, mapWidth);
    const int plateWidth = pFontManager->getTextWidth(text, size) + 36;
    SDL_Log("UI_GALLERY_PLATE_FIT: '%s' %d pt, plate %d of map %d", text.c_str(), size, plateWidth, mapWidth);
    require(plateWidth <= mapWidth, "The end plate is wider than the map: " + std::to_string(plateWidth) + " > " + std::to_string(mapWidth));
    const std::string longest = "Team 3 has won: Kleshmersh, Wildspade and Tharpique";
    require(pFontManager->getTextWidth(longest, watchedResultFontSize(longest, mapWidth)) + 36 <= mapWidth,
            "The longest team line does not fit the map");
    if(mapWidth >= 900) require(size == 28, "A team line that fits was shrunk");
    const int originalSlot = houseToVisualHouse[winner];
    for(int slot = 0; slot < NUM_PLAYER_HOUSES; ++slot) {
        if(!isValidHouseColorSlot(slot)) continue;
        houseToVisualHouse[winner] = slot;
        const auto image = gameCapture(*game, "game-spectator-end-colour" + std::to_string(slot));
        const int textH = pFontManager->getTextHeight(size);
        const int cx = mapWidth / 2, cy = game->topBarPos.h + (getRendererHeight() - game->topBarPos.h) / 2;
        const int py = cy - textH / 2 - 10 + 1;   // the frame's top row
        const auto want = RGBA2SDL(getHouseColorRGB(slot, 1));
        // The frame row: winner-coloured from its left to its right end, both inside the map.
        int left = cx, right = cx;
        {
            sdl2::surface_lock lock{image.get()};
            const auto frame = [&](int x) {
                SDL_Color c{};
                SDL_GetRGBA(getPixel(image.get(), x, py), image->format, &c.r, &c.g, &c.b, &c.a);
                return std::abs(c.r - want.r) <= 2 && std::abs(c.g - want.g) <= 2 && std::abs(c.b - want.b) <= 2;
            };
            require(frame(cx), "The plate frame is not slot " + std::to_string(slot) + "'s colour");
            while(left > 0 && frame(left - 1)) --left;
            while(right + 1 < image->w && frame(right + 1)) ++right;
        }
        require(left > 0 && right < mapWidth - 1, "The plate frame runs off the map: " + std::to_string(left) + ".." + std::to_string(right));
        SDL_Log("UI_GALLERY_PLATE: slot=%d frame=%d,%d,%d x=%d..%d text=%s", slot, want.r, want.g, want.b, left, right, text.c_str());
    }
    houseToVisualHouse[winner] = originalSlot;
    screen("game-spectator-stats", [&] {
        CustomGameStatsMenu stats;
        require(stats.resultLabel.getText() == text, "The statistics name another winner");
        captureWindow(stats, "game-spectator-stats");
    });
    endGame(game);
}

// ---------------------------------------------------------------- menus

void runMenus() {
    screen("menu-main", [] {
        MainMenu menu;
        captureWindow(menu, "menu-main");
        // Regression (1.0.852): update() re-laid the menu out every frame, throwing away every
        // button texture, so each idle frame re-rendered all lettering (17-33 ms with Classic).
        menu.update();
        menu.draw();
        const auto* button = menu.onlineButton.pUnpressedTexture.get();
        const auto* banner = menu.activeModLabel.pTexture.get();
        require(button != nullptr && banner != nullptr, "The main menu drew no button or banner texture");
        for(int frame = 0; frame < 3; ++frame) { menu.update(); menu.draw(); }
        require(menu.onlineButton.pUnpressedTexture.get() == button && menu.activeModLabel.pTexture.get() == banner,
                "An idle main-menu frame re-rendered the buttons or the banner");
    });
    screen("menu-single-player", [] { SinglePlayerMenu menu; captureWindow(menu, "menu-single-player"); });
    screen("menu-random-skirmish", [] {
        RandomSkirmishMenu menu;
        captureWindow(menu, "menu-random-skirmish");
        menu.spectateBox.setChecked(true);
        menu.onSpectateChanged();
        captureWindow(menu, "menu-random-skirmish-spectate");
        menu.spectateBox.setChecked(false);
        menu.onSpectateChanged();
    });
    screen("menu-custom-game", [] { CustomGameMenu menu(false, false); captureWindow(menu, "menu-custom-game"); });
    screen("menu-custom-game-players", [] {
        const char* mapPath = std::getenv("UI_GALLERY_MAP");
        require(mapPath && *mapPath, "UI_GALLERY_MAP is required");
        ModManager::instance().setActiveMod("dunecity");
        effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        CustomPlaySetup setup;
        setup.rules = effectiveGameOptions;
        setup.maps = {mapPath};
        setup.mods = ModManager::instance().listModChoices();
        setup.online = false;
        const auto build = [&] {
            return std::make_unique<CustomGamePlayers>(GameInitSettings(getBasename(mapPath, true), readCompleteFile(mapPath),
                                                                        setup.sharedHouse, setup.rules), true, false, &setup);
        };
        {
            auto menu = build();
            captureWindow(*menu, "menu-custom-game-players");
            menu->setupSpectate.setChecked(true);
            menu->setupSpectate.pOnClick();   // records the choice in the setup, as a click does
        }
        require(setup.spectate, "Clicking Spectate did not record the choice");
        auto watching = build();
        captureWindow(*watching, "menu-custom-game-players-spectate");
    });
    screen("menu-campaign", [] { HouseChoiceMenu menu(false); captureWindow(menu, "menu-campaign"); });
    screen("menu-multiplayer", [] { MultiPlayerMenu menu; captureWindow(menu, "menu-multiplayer"); });
    screen("menu-multiplayer-lobby", [] {
        CrossplayMenu menu;
        menu.chatHistory.setText("Alice: Looking for a campaign partner.\nBob: I can join after this match.");
        captureWindow(menu, "menu-multiplayer-lobby");
    });
    screen("menu-options", [] {
        OptionsMenu menu;
        for(int page = 0; page < 5; ++page) {
            menu.showPage(page);
            captureWindow(menu, "menu-options-" + std::to_string(page));
        }
        // Preview of the next skin and Back: nothing applied, nothing written.
        menu.showPage(1);
        const int skin = settings.video.uiSkin;
        menu.uiSkinDropDownBox.setSelectedItem((skin + 1) % 3);
        menu.onChangeOption(true);
        captureWindow(menu, "menu-options-graphics-preview");
        require(MenuTheme::uiSkin == skin && settings.video.uiSkin == skin, "The skin preview changed the live skin");
        menu.onOptionsCancel();
        require(settings.video.uiSkin == skin, "Back applied the previewed skin");
    });
    screen("menu-mods", [] { ModMenu menu(ModMenu::Purpose::ModEditor); captureWindow(menu, "menu-mods"); });
    screen("menu-mentat", [] {
        MentatMenu mentat(HOUSE_ORDOS);
        mentat.setText("The Mentat reads this line over the original artwork; it must stay white and outlined.");
        captureWindow(mentat, "menu-mentat");
    });
    screen("menu-map-editor", [] {
        ModManager::instance().setActiveMod("dunecity");
        MapEditor editor;
        auto& ui = *editor.pInterface;
        if(ui.hasChildWindow()) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            editor.drawMap(screenborder, false);
            ui.draw(Point(0, 0)); ui.drawOverlay(Point(0, 0));
            save("menu-map-editor-new-map");
            lint(ui, "menu-map-editor-new-map");
        }
        while(ui.hasChildWindow()) { ui.closeChildWindow(); ui.processChildWindowOpenCloses(); }
        editor.setMap(MapData(64, 64, Terrain_Sand), MapInfo());
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
        editor.drawMap(screenborder, false);
        ui.draw(Point(0, 0)); ui.drawOverlay(Point(0, 0));
        save("menu-map-editor");
        lint(ui, "menu-map-editor");
    });
    ModManager::instance().setActiveMod("vanilla");
}

int applySkin(int skin) {
    OptionsMenu options;
    options.showPage(1);
    options.uiSkinDropDownBox.setSelectedItem(skin);
    options.onChangeOption(true);
    options.onOptionsOK();
    require(options.isQuiting() && !options.hasChildWindow(), "UI skin Apply failed or opened an error");
    require(settings.video.uiSkin == skin, "Apply did not accept the chosen UI skin");
    return MENU_QUIT_REINITIALIZE;
}

/// CPU ms of one idle main-menu frame as MenuBase::showMenu runs it (update, then draw): the
/// median of 7 batches of 40 frames, after a warm-up batch.
double menuFrameCpuMs() {
    MainMenu menu;
    std::vector<double> batches;
    for(int batch = 0; batch < 8; ++batch) {
        const std::clock_t start = std::clock();
        for(int frame = 0; frame < 40; ++frame) {
            SDL_RenderClear(renderer);
            menu.update();
            menu.draw();
            menu.drawOverlay();
        }
        if(batch > 0) batches.push_back(1000.0 * (std::clock() - start) / CLOCKS_PER_SEC / 40);
    }
    std::sort(batches.begin(), batches.end());
    return batches[batches.size() / 2];
}

int run() {
    const char* profile = std::getenv("DUNECITY_USERDIR");
    require(profile && *profile, "The gallery requires an isolated profile");
    if(std::getenv("UI_GALLERY_NEW_PROFILE")) {
        // A profile the game created itself on this start (no Dune City.ini before it).
        INIFile created(getConfigFilepath());
        require(settings.video.uiSkin == 2 && MenuTheme::uiSkin == 2, "A new profile did not start in Classic Desert");
        require(created.getIntValue("Video", "UI Skin", -1) == 2, "A new profile's Dune City.ini does not save UI Skin = 2");
        SDL_Log("UI_GALLERY_PASS: new profile starts in Classic Desert and saves UI Skin = 2");
        return MENU_QUIT_DEFAULT;
    }
    require(getRendererWidth() == std::atoi(std::getenv("MENU_PROBE_WIDTH"))
            && getRendererHeight() == std::atoi(std::getenv("MENU_PROBE_HEIGHT")),
            "Logical size " + std::to_string(getRendererWidth()) + "x" + std::to_string(getRendererHeight()) + " differs from the case");
    const int startSkin = std::atoi(std::getenv("UI_GALLERY_SKIN"));
    static int stage = 0;
    static std::string stage0Pixels;
    static std::array<double, 3> menuMs{};
    const int expected = stage == 1 ? (startSkin + 1) % 3 : startSkin;
    require(settings.video.uiSkin == expected && MenuTheme::uiSkin == expected,
            "Stage " + std::to_string(stage) + " runs skin " + std::to_string(MenuTheme::uiSkin) + ", expected " + std::to_string(expected));
    INIFile saved(getConfigFilepath());
    if(stage == 0) {
        const bool fresh = std::getenv("UI_GALLERY_FRESH_PROFILE") != nullptr;
        require(validatedUISkin(saved.getIntValue("Video", "UI Skin", 2)) == startSkin,
                "The profile does not hold the starting skin");
        if(fresh) require(startSkin == 2, "A profile without a saved UI Skin did not get Classic Desert");
        runMenus();
        runInGame();
        runSpectatorEnd();
        stage0Pixels = sidebarRoundTrip();
        menuMs[MenuTheme::uiSkin] = menuFrameCpuMs();
        ++stage;
        return applySkin((startSkin + 1) % 3);
    }
    require(validatedUISkin(saved.getIntValue("Video", "UI Skin", -1)) == expected, "Apply did not persist the skin");
    if(stage == 1) {
        const auto other = sidebarRoundTrip();
        require(other != stage0Pixels, "The in-game sidebar looks the same in two different skins");
        menuMs[MenuTheme::uiSkin] = menuFrameCpuMs();
        // Regression (1.0.852): an idle Classic main-menu frame re-rendered every button's pixel
        // lettering (17-33 ms CPU against Modern's 2-3 ms). Classic -> Modern runs compare both.
        if(startSkin == 2) {
            SDL_Log("UI_GALLERY_MENU_FRAME: Classic %.3f ms, Modern %.3f ms CPU per idle main-menu frame", menuMs[2], menuMs[0]);
            require(menuMs[2] <= 1.5 * menuMs[0] + 0.5,
                    "An idle Classic main-menu frame costs " + std::to_string(menuMs[2]) + " ms, Modern "
                    + std::to_string(menuMs[0]) + " ms (more than 1.5x + 0.5 ms)");
        }
        ++stage;
        return applySkin(startSkin);
    }
    const auto back = sidebarRoundTrip();
    if(back != stage0Pixels) {
        size_t differing = 0;
        for(size_t i = 0; i < back.size() && i < stage0Pixels.size(); ++i) differing += back[i] != stage0Pixels[i];
        require(false, "Switching skins and back changed the in-game sidebar: " + std::to_string(differing) + " of "
                + std::to_string(back.size()) + " bytes differ (stale sprite or style cache)");
    }
    SDL_Log("UI_GALLERY_PASS: skin=%d size=%dx%d screens=%d issues=%d", startSkin, getRendererWidth(), getRendererHeight(),
            screenCount, issueCount);
    return MENU_QUIT_DEFAULT;
}

} // namespace UIGallery

int runUIGalleryProbe() {
    try { return UIGallery::run(); }
    catch(const std::exception& e) { SDL_Log("UI_GALLERY_FAIL: %s", e.what()); std::exit(1); }
}
