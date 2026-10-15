// Real-engine probe for the sidebar build grid (docs/sidebar-grid.md). Selects a DuneCity
// Construction Yard, a factory with a unit in production and a Starport through the real
// selection path, renders Game::drawScreen at the requested viewport and saves PNGs.
// Fixture setup only clears map patches, places buildings, unlocks items and adds credits.
#include <structures/ConstructionYard.h>
#include <structures/StarPort.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <misc/draw_util.h>

namespace SidebarGridProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("SIDEBAR_GRID_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

/// A WxH patch of plain rock, with a margin, away from every object and the house's ground.
Coord clearPatch(House* house, int W, int H, const std::vector<Coord>& avoid) {
    const int M = 3;
    for(int y = M; y + H + M < currentGameMap->getSizeY(); ++y) {
        for(int x = M; x + W + M < currentGameMap->getSizeX(); ++x) {
            bool clear = true;
            for(const Coord& other : avoid) {
                if(std::abs(other.x - x) < W + 2*M && std::abs(other.y - y) < H + 2*M) clear = false;
            }
            for(int dy = -M; dy < H + M && clear; ++dy) for(int dx = -M; dx < W + M && clear; ++dx) {
                const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                clear = !tile->hasAGroundObject() && !tile->hasCityZone() && tile->getOwner() != house->getHouseID();
            }
            if(clear) {
                for(int dy = 0; dy < H; ++dy) for(int dx = 0; dx < W; ++dx) {
                    Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                    tile->setType(Terrain_Rock);
                    tile->setRoad(false);
                    tile->setOwner(NONE_ID);
                }
                return Coord(x, y);
            }
        }
    }
    require(false, "No clear fixture patch");
    return Coord::Invalid();
}

/// Unlock every item this kind of builder can make, so the list is as long as the mod allows.
void unlockItemsOf(Game& game, House* house, Uint32 builderItemID) {
    for(int item = ItemID_FirstID; item <= ItemID_LastID; ++item) {
        auto& data = game.objectData.data[item][house->getHouseID()];
        if(!data.enabled || data.builder != static_cast<int>(builderItemID)) continue;
        data.techLevel = 1;
        data.upgradeLevel = 0;
        data.prerequisiteStructuresSet.reset();
        data.buildtime = 8;
    }
}

BuilderList* select(Game& game, BuilderBase* builder) {
    game.unselectAll(game.selectedList); game.selectedList.clear();
    game.selectedList.insert(builder->getObjectID()); builder->setSelected(true); game.selectionChanged();
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<BuilderInterface*>(game.pInterface->pObjectContainer);
    require(panel != nullptr && panel->pBuilderList != nullptr, "Selected builder has no build list");
    panel->update();
    return panel->pBuilderList;
}

void capture(Game& game, const std::string& name) {
    SDL_SetRenderTarget(renderer, nullptr);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    require(image != nullptr && SavePNG(image.get(), name.c_str()) == 0, "Could not capture " + name);
    SDL_Log("SIDEBAR_GRID_SCREENSHOT: %s", name.c_str());
}

/// Number of list entries drawn at once, and how many the list holds; plus the grid checks
/// that need the real widget: its size, every visible slot hit-tested through getButton,
/// and overlay texts that fit a button.
void report(const char* what, BuilderList* list, BuilderBase* builder) {
    namespace Layout = BuilderListLayout;
    const int height = list->getSize().y;
    const int visible = std::min<int>(list->getNumButtons(height), builder->getBuildListSize());
    SDL_Log("SIDEBAR_GRID_COUNT: %s viewport=%dx%d list=%dx%d slots=%d items=%d visible=%d button=%dx%d",
            what, getRendererWidth(), getRendererHeight(), list->getSize().x, height,
            list->getNumButtons(height), builder->getBuildListSize(), visible, Layout::ButtonWidth, Layout::ButtonHeight);
    require(list->getSize().x == Layout::WidgetWidth, "The list does not span the sidebar column");
    require(list->getNumButtons(height) == Layout::rows(height) * Layout::Columns, "Slots are not rows times columns");
    for(int slot = 0; slot < list->getNumButtons(height); ++slot) {
        const Point corner = list->getButtonPosition(slot);
        const int expected = list->currentListPos + slot < builder->getBuildListSize() ? list->currentListPos + slot : -1;
        require(list->getButton(corner.x, corner.y) == expected
                && list->getButton(corner.x + Layout::ButtonWidth - 1, corner.y + Layout::ButtonHeight - 1) == expected,
                "A visible slot does not hit its own item");
        require(list->getButton(corner.x - 1, corner.y) == -1 && list->getButton(corner.x, corner.y + Layout::ButtonHeight) == -1,
                "A gap next to a slot hits an item");
    }
    for(SDL_Texture* text : { list->pSoldOutTextTexture.get(), list->pAlreadyBuiltTextTexture.get(), list->pPlaceItTextTexture.get(),
                              list->pOnHoldTextTexture.get(), list->pUnitLimitReachedTextTexture.get(),
                              list->pHarvesterLimitReachedTextTexture.get() }) {
        SDL_Log("SIDEBAR_GRID_OVERLAY: %dx%d", getWidth(text), getHeight(text));
        require(getWidth(text) <= Layout::ButtonWidth && getHeight(text) <= Layout::ButtonHeight, "An overlay text is wider than a button");
    }
}

/// Hover the first slot showing itemID long enough for the tooltip.
void hover(BuilderList* list, BuilderBase* builder, Uint32 itemID) {
    int index = 0;
    for(const BuildItem& item : builder->getBuildList()) { if(item.itemID == itemID) break; ++index; }
    require(index < builder->getBuildListSize(), "Hover item is not listed");
    // Scroll with the real arrow handler until the item is on screen.
    for(int step = 0; step < builder->getBuildListSize() && index >= list->currentListPos + list->getNumButtons(list->getSize().y); ++step) {
        list->onDown();
    }
    require(index >= list->currentListPos && index < list->currentListPos + list->getNumButtons(list->getSize().y),
            "Hover item is not on screen");
    const Point slot = list->getButtonPosition(index - list->currentListPos);
    list->lastMousePos = slot + Point(8, 8);
    list->lastMouseMovement = SDL_GetTicks() - 2000;
    require(list->getButton(list->lastMousePos.x, list->lastMousePos.y) == index, "Hovered point does not hit the item");
}

} // namespace SidebarGridProbe

void runSidebarGridProbe(Game& game) {
    using namespace SidebarGridProbe;
    House* house = pLocalHouse;
    require(house != nullptr && game.isCitySimEnabled(), "Probe needs the dunecity mod and a local house");
    const std::string size = std::to_string(getRendererWidth()) + "x" + std::to_string(getRendererHeight());
    house->storedCredits = 0;
    house->cityCredits = 0;
    house->startingCredits = 60000;
    house->enforceCreditCeiling();

    // ---- DuneCity Construction Yard with every yard item unlocked. ----
    const Coord yardPlot = clearPatch(house, 8, 6, {});
    unlockItemsOf(game, house, Structure_ConstructionYard);
    auto* yard = dynamic_cast<ConstructionYard*>(house->placeStructure(NONE_ID, Structure_ConstructionYard, yardPlot.x, yardPlot.y, true, true));
    require(yard != nullptr, "Could not place the fixture yard");
    yard->updateBuildList();
    require(yard->getBuildItem(Structure_ZoneResidential) && yard->getBuildItem(Structure_PoliceStation)
            && yard->getBuildItem(Structure_Road), "Yard list lacks the DuneCity items");
    yard->handleProduceItemClick(Structure_WindTrap, false);
    yard->handleProduceItemClick(Structure_Silo, false);
    yard->handleProduceItemClick(Structure_Silo, false);
    for(int i = 0; i < 400 && yard->getProductionProgress() < 60; ++i) game.updateGameState();
    require(yard->getCurrentProducedItem() == Structure_WindTrap && yard->getProductionProgress() > 0, "Yard production did not start");
    currentGameMap->viewMap(house->getHouseID(), yardPlot, 10);
    screenborder->setNewScreenCenter(Coord((yardPlot.x + 2) * TILESIZE, (yardPlot.y + 2) * TILESIZE));
    BuilderList* list = select(game, yard);
    report("yard", list, yard);
    {
        // The old single column held (height - 68) / 60 buttons of 91x55.
        const int slots = list->getNumButtons(list->getSize().y);
        const int oldSlots = std::max(0, (list->getSize().y - 68) / 60);
        require(std::min(slots, yard->getBuildListSize()) >= std::min(2*oldSlots, yard->getBuildListSize()),
                "The grid does not show twice the old column's items");
        if(getRendererHeight() >= 768) {
            require(slots >= yard->getBuildListSize(), "The DuneCity yard list still scrolls at 1024x768 or more");
        }
    }
    hover(list, yard, Structure_ZoneResidential);
    require(list->currentListPos == 0, "Hovering scrolled the unscrolled yard list");
    capture(game, "sidebar-yard-" + size + ".png");

    // Same yard, finished building waiting for placement, on hold elsewhere is impossible (one item
    // at a time), so a second shot shows PLACE IT.
    for(int i = 0; i < 4000 && !yard->isWaitingToPlace(); ++i) game.updateGameState();
    require(yard->isWaitingToPlace(), "Yard never finished the windtrap");
    list = select(game, yard);
    list->lastMouseMovement = SDL_GetTicks();
    capture(game, "sidebar-yard-placeit-" + size + ".png");

    // ---- Heavy factory: a unit in production on repeat, a queue count, ON HOLD shot. ----
    const Coord factoryPlot = clearPatch(house, 5, 4, {yardPlot});
    unlockItemsOf(game, house, Structure_HeavyFactory);
    auto* factory = dynamic_cast<BuilderBase*>(house->placeStructure(NONE_ID, Structure_HeavyFactory, factoryPlot.x, factoryPlot.y, true, true));
    require(factory != nullptr, "Could not place the fixture factory");
    factory->updateBuildList();
    require(factory->getBuildListSize() >= 3, "Factory list is too short to be interesting");
    const Uint32 firstUnit = factory->getBuildList().front().itemID;
    const Uint32 secondUnit = std::next(factory->getBuildList().begin())->itemID;
    factory->handleProduceItemClick(firstUnit, false);
    factory->handleSetRepeatClick(firstUnit, true);
    factory->handleProduceItemClick(secondUnit, false);
    factory->handleProduceItemClick(secondUnit, false);
    factory->handleProduceItemClick(secondUnit, false);
    for(int i = 0; i < 400 && factory->getProductionProgress() < 80; ++i) game.updateGameState();
    require(factory->getProductionProgress() > 0 && factory->isRepeating(firstUnit), "Factory production did not start on repeat");
    screenborder->setNewScreenCenter(Coord((factoryPlot.x + 2) * TILESIZE, (factoryPlot.y + 2) * TILESIZE));
    list = select(game, factory);
    report("factory", list, factory);
    hover(list, factory, firstUnit);
    capture(game, "sidebar-factory-" + size + ".png");
    factory->handleSetOnHoldClick(true);   // a command: executes on a later cycle
    for(int i = 0; i < 20 && !factory->isOnHold(); ++i) game.updateGameState();
    require(factory->isOnHold(), "The factory did not go on hold");
    list->lastMouseMovement = SDL_GetTicks();
    capture(game, "sidebar-factory-onhold-" + size + ".png");

    // ---- Starport: one item sold out, ordering open. ----
    const Coord starportPlot = clearPatch(house, 5, 5, {yardPlot, factoryPlot});
    unlockItemsOf(game, house, Structure_StarPort);
    auto* starport = dynamic_cast<StarPort*>(house->placeStructure(NONE_ID, Structure_StarPort, starportPlot.x, starportPlot.y, true, true));
    require(starport != nullptr, "Could not place the fixture starport");
    starport->updateBuildList();
    require(starport->getBuildListSize() >= 2, "Starport list is too short");
    const Uint32 soldOut = starport->getBuildList().front().itemID;
    house->getChoam().setNumAvailable(soldOut, 0);
    starport->handleProduceItemClick(std::next(starport->getBuildList().begin())->itemID, false);
    game.updateGameState();
    screenborder->setNewScreenCenter(Coord((starportPlot.x + 2) * TILESIZE, (starportPlot.y + 2) * TILESIZE));
    list = select(game, starport);
    report("starport", list, starport);
    list->lastMouseMovement = SDL_GetTicks();
    capture(game, "sidebar-starport-" + size + ".png");

    SDL_Log("SIDEBAR_GRID_PROBE: done at %s", size.c_str());
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=sidebar_grid outcome=passed");
}
