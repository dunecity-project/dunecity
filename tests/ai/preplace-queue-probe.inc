// Real-engine probe for the Construction Yard pre-placement queue (docs/preplace-queue.md).
// Drives the real sidebar list, planning cursor, CommandManager, command executor,
// BuilderBase production, House::placeStructure, savegame and renderer. Nothing is mocked:
// fixture setup only clears a patch of map, places a yard and shortens build times.
#include <structures/ConstructionYard.h>
#include <Command.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <misc/draw_util.h>

namespace PreplaceProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("PREPLACE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

FixPoint totalCredits(const House* house) {
    return house->getStartingCredits() + house->getStoredCredits() + house->getCityCredits();
}

ConstructionYard* yard(Game& game, Uint32 id) {
    return dynamic_cast<ConstructionYard*>(game.getObjectManager().getObject(id));
}

const StructureBase* structureAt(const House* house, Uint32 itemID, Coord plot) {
    for(const StructureBase* structure : structureList) {
        if(structure->getOwner() == house && structure->getItemID() == itemID && structure->getLocation() == plot) {
            return structure;
        }
    }
    return nullptr;
}

std::vector<Uint32> queueItems(const ConstructionYard* cy) {
    std::vector<Uint32> items;
    for(const auto& entry : cy->getProductionQueue()) items.push_back(entry.itemID);
    return items;
}

const ProductionQueueItem* entryFor(const ConstructionYard* cy, Uint32 reservationID) {
    for(const auto& entry : cy->getProductionQueue()) {
        if(entry.reservationID == reservationID) return &entry;
    }
    return nullptr;
}

bool tickerHas(Game& game, const std::string& text) {
    std::queue<std::string> messages = game.pInterface->newsticker.messages;
    while(!messages.empty()) {
        if(messages.front() == text) return true;
        messages.pop();
    }
    return false;
}

void advance(Game& game, int cycles) {
    for(int i = 0; i < cycles; ++i) game.updateGameState();
}

/// Run until pred() holds, at most limit cycles; returns whether it held.
template<class Pred>
bool advanceUntil(Game& game, int limit, Pred pred) {
    for(int i = 0; i < limit; ++i) {
        if(pred()) return true;
        game.updateGameState();
    }
    return pred();
}

void reserve(Game& game, Uint32 yardID, Uint32 item, Coord plot) {
    game.getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_RESERVE_PLACEMENT,
        yardID, item, static_cast<Uint32>(plot.x), static_cast<Uint32>(plot.y)));
}

int countCommands(Game& game, CMDTYPE id) {
    int count = 0;
    for(const auto& slot : game.cmdManager.timeslot) for(const auto& command : slot) if(command.getCommandID() == id) ++count;
    return count;
}

} // namespace PreplaceProbe

void runPreplaceQueueProbe(std::unique_ptr<Game>& game) {
    using namespace PreplaceProbe;
    House* house = pLocalHouse;
    require(house != nullptr && game->isCitySimEnabled(), "Probe needs the dunecity mod and a local house");
    require(settings.general.chooseBuildingLocation, "The pre-placement setting must default to on");

    // Fixture area: a 14x9 patch (plus a 2-tile margin) away from every object and from the
    // house's own ground, turned into plain rock. The fixture yard sits in its top-left corner,
    // so the yard alone decides the real build range.
    const int W = 14, H = 9, M = 3;
    Coord origin = Coord::Invalid();
    for(int y = M; y + H + M < currentGameMap->getSizeY() && origin.isInvalid(); ++y) {
        for(int x = M; x + W + M < currentGameMap->getSizeX() && origin.isInvalid(); ++x) {
            bool clear = true;
            for(int dy = -M; dy < H + M && clear; ++dy) for(int dx = -M; dx < W + M && clear; ++dx) {
                const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                clear = !tile->hasAGroundObject() && !tile->hasCityZone() && tile->getOwner() != house->getHouseID();
            }
            if(clear) origin = Coord(x, y);
        }
    }
    require(origin.isValid(), "No clear fixture area");
    for(int dy = 0; dy < H; ++dy) for(int dx = 0; dx < W; ++dx) {
        Tile* tile = currentGameMap->getTile(origin.x + dx, origin.y + dy);
        tile->setType(Terrain_Rock);
        tile->setRoad(false);
        tile->setOwner(NONE_ID);
    }
    auto at = [&](int dx, int dy) { return Coord(origin.x + dx, origin.y + dy); };

    auto* fixtureYard = dynamic_cast<ConstructionYard*>(house->placeStructure(NONE_ID, Structure_ConstructionYard, origin.x, origin.y, true, true));
    require(fixtureYard != nullptr, "Could not place the fixture yard");
    const Uint32 yardID = fixtureYard->getObjectID();
    currentGameMap->viewMap(house->getHouseID(), at(7,4), 12);
    // Make every fixture item genuinely available, so build-list refreshes after each
    // placement keep the queued orders (as the SourceForge palace fixture does).
    const Uint32 items[] = {Structure_WindTrap, Structure_Barracks, Structure_Silo, Structure_ZoneResidential};
    auto prepareItems = [&]() {
        for(Uint32 item : items) {
            auto& data = game->objectData.data[item][pLocalHouse->getHouseID()];
            data.enabled = true;
            data.builder = Structure_ConstructionYard;
            data.techLevel = 1;
            data.upgradeLevel = 0;
            data.prerequisiteStructuresSet.reset();
            data.buildtime = 8;   // 120 cycles: quick, but slow enough to see progress
        }
    };
    prepareItems();
    fixtureYard->updateBuildList();
    for(Uint32 item : items) require(fixtureYard->getBuildItem(item) != nullptr, "Fixture item not in the yard's list: " + std::to_string(item));
    house->storedCredits = 0;
    house->cityCredits = 0;
    house->startingCredits = 40000;
    house->enforceCreditCeiling();

    // ---- Sidebar list: the setting decides what a click on a yard item does. ----
    game->unselectAll(game->selectedList); game->selectedList.clear();
    game->selectedList.insert(yardID); fixtureYard->setSelected(true); game->selectionChanged();
    game->pInterface->updateObjectInterface();
    auto* builderInterface = dynamic_cast<BuilderInterface*>(game->pInterface->pObjectContainer);
    require(builderInterface != nullptr && builderInterface->pBuilderList != nullptr, "Yard sidebar has no build list");
    BuilderList* list = builderInterface->pBuilderList;
    auto clickItem = [&](Uint32 item, SDL_Keymod mod) {
        int index = 0;
        for(const BuildItem& buildItem : fixtureYard->getBuildList()) { if(buildItem.itemID == item) break; ++index; }
        require(index < fixtureYard->getBuildListSize(), "Item missing from list");
        list->currentListPos = index;
        const Point button = list->getButtonPosition(0) + Point(10, 10);
        SDL_SetModState(mod);
        list->handleMouseLeft(button.x, button.y, true);
        list->handleMouseLeft(button.x, button.y, false);
        SDL_SetModState(KMOD_NONE);
    };
    const int produceBefore = countCommands(*game, CMD_BUILDER_PRODUCEITEM);
    clickItem(Structure_WindTrap, KMOD_SHIFT);
    require(game->isPlanningPlacement() && game->getPlanningItemID() == Structure_WindTrap,
        "Setting on: a yard list click must enter the planning cursor");
    require(countCommands(*game, CMD_BUILDER_PRODUCEITEM) == produceBefore && fixtureYard->getProductionQueueSize() == 0,
        "Setting on: a list click must not start production");
    game->setCursorMode(Game::CursorMode_Normal);
    settings.general.chooseBuildingLocation = false;
    clickItem(Structure_WindTrap, KMOD_SHIFT);
    require(!game->isPlanningPlacement(), "Setting off: no planning cursor");
    bool multiple = false;
    for(auto& slot : game->cmdManager.timeslot) {
        for(auto it = slot.begin(); it != slot.end(); ++it) {
            if(it->getCommandID() == CMD_BUILDER_PRODUCEITEM) {
                const auto p = it->getParameter();
                multiple = p.size() == 3 && p[0] == yardID && p[1] == Structure_WindTrap && p[2] == 1;
                slot.erase(it);   // only the click's meaning is under test
                break;
            }
        }
    }
    require(multiple, "Setting off: Shift+click must keep the x5 order");
    settings.general.chooseBuildingLocation = true;
    SDL_Log("PREPLACE_PROBE: sidebar setting on=planning off=x5 order");

    // ---- Planning cursor: it stays on the building after a plot, overlap with a pending plot is
    // refused, Esc leaves. ----
    require(fixtureYard->checkReservationPlot(Structure_Barracks, at(4,0).x, at(4,0).y) == PlacementReservation::Verdict::OutOfRange,
        "The chained plot must be out of the yard's own range");
    clickItem(Structure_WindTrap, KMOD_NONE);
    SDL_SetModState(KMOD_NONE);
    require(game->handlePlacementClick(at(2,0).x, at(2,0).y), "Planning click on a valid plot failed");
    require(game->isPlanningPlacement() && game->getPlanningItemID() == Structure_WindTrap,
            "A plain click must keep the same building selected for the next plot");
    require(!game->handlePlacementClick(at(3,1).x, at(3,1).y), "A plot overlapping a just-ordered one was accepted");
    SDL_KeyboardEvent esc{}; esc.type = SDL_KEYDOWN; esc.keysym.sym = SDLK_ESCAPE;
    game->handleKeyInput(esc);
    require(!game->isPlanningPlacement() && game->pInGameMenu == nullptr, "Esc must leave planning without opening the menu");
    require(countCommands(*game, CMD_BUILDER_RESERVE_PLACEMENT) == 1, "Exactly one reservation should have been ordered");
    advance(*game, 2);
    require(fixtureYard->getProductionQueueSize() == 1, "The planning click did not queue the order");

    // ---- The other reservations through the command manager. ----
    const Coord pWind = at(2,0), pBarracks = at(4,0), pSilo = at(0,2), pZone = at(2,2), pBlocked = at(6,0);
    reserve(*game, yardID, Structure_Barracks, pBarracks);          // chained off the windtrap plot
    reserve(*game, yardID, Structure_Silo, pSilo);
    reserve(*game, yardID, Structure_ZoneResidential, pZone);
    reserve(*game, yardID, Structure_WindTrap, pBlocked);           // chained off the barracks plot
    // Refused orders: overlap, out of range, off the map, unavailable item, another house.
    reserve(*game, yardID, Structure_Silo, at(3,1));
    reserve(*game, yardID, Structure_WindTrap, at(11,6));
    reserve(*game, yardID, Structure_WindTrap, Coord(currentGameMap->getSizeX() - 1, 0));
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_RESERVE_PLACEMENT,
        yardID, static_cast<Uint32>(Structure_StarPort), static_cast<Uint32>(at(8,4).x), static_cast<Uint32>(at(8,4).y)));
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_RESERVE_PLACEMENT,
        yardID, static_cast<Uint32>(Structure_WindTrap), 0xFFFFFFF0u, 0x7FFFFFFFu));
    const Player* enemyPlayer = nullptr;
    for(int h = 0; h < NUM_HOUSES && !enemyPlayer; ++h)
        if(House* other = game->getHouse(h); other && other != house && !other->getPlayerList().empty()) enemyPlayer = other->getPlayerList().front().get();
    require(enemyPlayer != nullptr, "No enemy player fixture");
    Command(enemyPlayer->getPlayerID(), CMD_BUILDER_RESERVE_PLACEMENT, yardID, Structure_WindTrap,
            static_cast<Uint32>(at(8,4).x), static_cast<Uint32>(at(8,4).y)).executeCommand();
    advance(*game, 2);
    require(queueItems(fixtureYard) == std::vector<Uint32>({Structure_WindTrap, Structure_Barracks, Structure_Silo,
                                                             Structure_ZoneResidential, Structure_WindTrap}),
        "Accepted orders or their order are wrong");
    std::vector<Uint32> ids;
    for(const auto& entry : fixtureYard->getProductionQueue()) ids.push_back(entry.reservationID);
    require(ids == std::vector<Uint32>({1, 2, 3, 4, 5}), "Reservation ids are not stable and sequential");
    require(entryFor(fixtureYard, 2)->plotX == pBarracks.x && entryFor(fixtureYard, 5)->plotX == pBlocked.x,
        "Reserved plots were not recorded");
    SDL_Log("PREPLACE_PROBE: 5 reservations queued (2 chained), 6 invalid or foreign orders refused");

    // ---- Save -> load round trip with one plot part built. ----
    require(advanceUntil(*game, 400, [&] { return fixtureYard->getProductionProgress() > 40; }), "Production did not start");
    const FixPoint savedProgress = fixtureYard->getProductionProgress();
    struct Saved { Uint32 item, id; int x, y; bool failed; };
    std::vector<Saved> before;
    for(const auto& e : fixtureYard->getProductionQueue()) before.push_back({e.itemID, e.reservationID, e.plotX, e.plotY, e.reservationFailed});
    const Uint32 nextIDBefore = fixtureYard->nextReservationID;
    const std::string savePath = "preplace-roundtrip.sav";
    require(game->saveGame(savePath), "Could not save");
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(savePath));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    house = pLocalHouse;
    fixtureYard = yard(*game, yardID);
    require(fixtureYard != nullptr && game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "Yard lost across reload");
    std::vector<Saved> after;
    for(const auto& e : fixtureYard->getProductionQueue()) after.push_back({e.itemID, e.reservationID, e.plotX, e.plotY, e.reservationFailed});
    require(after.size() == before.size(), "Queue length changed across reload");
    for(std::size_t i = 0; i < before.size(); ++i) {
        require(after[i].item == before[i].item && after[i].id == before[i].id && after[i].x == before[i].x
                && after[i].y == before[i].y && after[i].failed == before[i].failed, "A reservation changed across reload");
    }
    require(fixtureYard->nextReservationID == nextIDBefore && fixtureYard->getProductionProgress() == savedProgress,
        "Builder reservation state changed across reload");
    prepareItems();
    SDL_Log("PREPLACE_PROBE: save/load round trip kept %zu reservations, next id %u", after.size(), nextIDBefore);

    // ---- Screenshot of the numbered ghosts, the first one under construction. ----
    auto capture = [&](const char* filename) {
        game->unselectAll(game->selectedList); game->selectedList.clear();
        screenborder->setNewScreenCenter(Coord((origin.x + 5) * TILESIZE, (origin.y + 3) * TILESIZE));
        SDL_SetRenderTarget(renderer, nullptr);
        game->drawScreen();
        auto image = renderReadSurface(renderer);
        require(image != nullptr && SavePNG(image.get(), filename) == 0, std::string("Could not capture ") + filename);
    };
    capture("preplace-ghosts.png");
    SDL_Log("PREPLACE_SCREENSHOT: preplace-ghosts.png progress=%d%%",
            static_cast<int>(lround(fixtureYard->getProductionProgress() * 100 / static_cast<int>(fixtureYard->getBuildItem(Structure_WindTrap)->price))));

    // ---- Completion: all four valid plots are built where planned, without placement clicks. ----
    // An own unit parked on the silo plot is asked to move and the silo still lands there.
    UnitBase* parked = house->createUnit(Unit_Quad);
    parked->deploy(Coord(pSilo.x + 1, pSilo.y + 1));
    require(currentGameMap->getTile(pSilo.x + 1, pSilo.y + 1)->hasAGroundObject(), "Parked unit fixture missing");
    // A structure placed meanwhile blocks the fifth plot.
    require(house->placeStructure(NONE_ID, Structure_Wall, pBlocked.x + 1, pBlocked.y + 1, true, true) != nullptr, "Wall fixture failed");
    const int placeCommands = countCommands(*game, CMD_PLACE_STRUCTURE);
    const std::string fallbackMessage = resolveItemName(Structure_WindTrap) + " could not be placed at the planned site";
    require(advanceUntil(*game, 3000, [&] {
        return fixtureYard->getProductionQueueSize() == 1 && fixtureYard->isWaitingToPlace();
    }), "The queue did not reach the blocked fifth order");
    require(structureAt(house, Structure_WindTrap, pWind) && structureAt(house, Structure_Barracks, pBarracks)
            && structureAt(house, Structure_Silo, pSilo) && structureAt(house, Structure_ZoneResidential, pZone),
        "A reserved building is missing or not exactly on its plot");
    require(parked->getLocation() != Coord(pSilo.x + 1, pSilo.y + 1), "The parked unit was not moved off the plot");
    require(countCommands(*game, CMD_PLACE_STRUCTURE) == placeCommands, "A placement click was needed");
    const ProductionQueueItem& blocked = fixtureYard->getProductionQueue().front();
    require(blocked.reservationID == 5 && blocked.reservationFailed && !fixtureYard->isAwaitingReservedPlacement(),
        "The blocked plot did not fall back");
    require(!structureAt(house, Structure_WindTrap, pBlocked), "The blocked windtrap was placed anyway");
    require(tickerHas(*game, fallbackMessage), "The fallback ticker message is missing");
    SDL_Log("PREPLACE_PROBE: 4 reserved buildings placed on plan (1 chained, 1 after moving a unit); 5th fell back: %s", fallbackMessage.c_str());
    capture("preplace-fallback.png");
    SDL_Log("PREPLACE_SCREENSHOT: preplace-fallback.png (red ghost, ready to place)");

    // The player places the fallen-back order by hand: the ordinary flow.
    const Coord manual = at(6,2);
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE, yardID,
        static_cast<Uint32>(manual.x), static_cast<Uint32>(manual.y)));
    advance(*game, 2);
    require(structureAt(house, Structure_WindTrap, manual) && fixtureYard->getProductionQueueSize() == 0,
        "Manual placement of the fallen-back order failed");

    // ---- Mixed queue: an unreserved order blocks the reserved one behind it, as today. ----
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_PRODUCEITEM, yardID,
        static_cast<Uint32>(Structure_Silo), 0u));
    const Coord pAfter = at(8,2);
    reserve(*game, yardID, Structure_WindTrap, pAfter);
    advance(*game, 2);
    require(fixtureYard->getProductionQueue().back().reservationID == 6, "Reserved order behind an unreserved one missing");
    require(advanceUntil(*game, 400, [&] { return fixtureYard->isWaitingToPlace(); }), "Unreserved silo did not finish");
    advance(*game, 200);
    require(fixtureYard->getCurrentProducedItem() == Structure_Silo && fixtureYard->isWaitingToPlace()
            && !structureAt(house, Structure_WindTrap, pAfter), "The reserved order overtook the unreserved one");
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE, yardID,
        static_cast<Uint32>(at(8,4).x), static_cast<Uint32>(at(8,4).y)));
    require(advanceUntil(*game, 400, [&] { return structureAt(house, Structure_WindTrap, pAfter) != nullptr; }),
        "The reserved order behind the placed one was not built on plan");
    SDL_Log("PREPLACE_PROBE: mixed queue kept order (unreserved waited for the player, reserved followed on plan)");

    // ---- Cancel and refund. ----
    const Coord pCancelA = at(10,2), pCancelB = at(10,4);
    reserve(*game, yardID, Structure_WindTrap, pCancelA);
    reserve(*game, yardID, Structure_Barracks, pCancelB);
    advance(*game, 2);
    require(advanceUntil(*game, 200, [&] { return fixtureYard->getProductionProgress() > 30; }), "Cancel fixture did not start");
    const Uint32 idA = fixtureYard->getProductionQueue().front().reservationID;
    const Uint32 idB = fixtureYard->getProductionQueue().back().reservationID;
    FixPoint credits = totalCredits(house);
    const FixPoint progress = fixtureYard->getProductionProgress();
    Command(enemyPlayer->getPlayerID(), CMD_BUILDER_CANCEL_RESERVATION, yardID, idA).executeCommand();
    require(fixtureYard->getProductionQueueSize() == 2, "Another house cancelled our reservation");
    Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_CANCEL_RESERVATION, yardID, 999u).executeCommand();
    require(fixtureYard->getProductionQueueSize() == 2, "An unknown reservation id cancelled something");
    Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_CANCEL_RESERVATION, yardID, idB).executeCommand();
    require(fixtureYard->getProductionQueueSize() == 1 && totalCredits(house) == credits
            && fixtureYard->getProductionProgress() == progress, "Cancelling a queued reservation changed money or progress");
    Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_CANCEL_RESERVATION, yardID, idA).executeCommand();
    require(fixtureYard->getProductionQueueSize() == 0 && totalCredits(house) == credits + progress,
        "Cancelling the reservation under construction did not refund exactly its progress");
    SDL_Log("PREPLACE_PROBE: cancel queued refund=0, cancel in production refund=%d", progress.lround());

    // Right-clicking a ghost on the map and the list's right-click cancel the same way.
    reserve(*game, yardID, Structure_WindTrap, pCancelA);
    reserve(*game, yardID, Structure_WindTrap, pCancelB);
    advance(*game, 2);
    require(fixtureYard->getProductionQueueSize() == 2, "Cancel fixture orders missing");
    const Uint32 lastID = fixtureYard->getProductionQueue().back().reservationID;
    fixtureYard->handleCancelItemClick(Structure_WindTrap, false);   // list right-click: newest windtrap
    advance(*game, 1);
    require(fixtureYard->getProductionQueueSize() == 1 && entryFor(fixtureYard, lastID) == nullptr,
        "List cancel did not drop the most recently queued reservation");
    require(game->handleReservationGhostRightClick(pCancelA.x + 1, pCancelA.y), "Right-click on a ghost was not handled");
    advance(*game, 1);
    require(fixtureYard->getProductionQueueSize() == 0, "Right-clicking the ghost did not cancel its reservation");
    SDL_Log("PREPLACE_PROBE: list cancel drops the newest reservation; ghost right-click cancels its own");

    // ---- A destroyed yard takes its reservations with it, refunding as a dying builder does today. ----
    reserve(*game, yardID, Structure_WindTrap, pCancelA);
    reserve(*game, yardID, Structure_WindTrap, pCancelB);
    advance(*game, 2);
    require(advanceUntil(*game, 200, [&] { return fixtureYard->getProductionProgress() > 30; }), "Destruction fixture did not start");
    credits = totalCredits(house);
    fixtureYard->destroy();
    fixtureYard = nullptr;
    require(yard(*game, yardID) == nullptr, "The yard survived destruction");
    require(totalCredits(house) == credits, "Destroying a yard with reservations changed the house's money");
    for(const auto& footprint : ConstructionYard::reservedFootprints(house)) {
        require(!(footprint.x == pCancelA.x && footprint.y == pCancelA.y) && !(footprint.x == pCancelB.x && footprint.y == pCancelB.y),
            "A destroyed yard's reservation still counts");
    }
    SDL_Log("PREPLACE_PROBE: destroyed yard's reservations vanished, money unchanged as for any builder");

    // ---- A 9854 save (no reservations) still loads and keeps the place-it-yourself flow. ----
    const char* oldSave = std::getenv("BALANCE_PREPLACE_9854_SAVE");
    require(oldSave != nullptr, "Missing 9854 fixture");
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(oldSave));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    require(game->getLoadedSavegameVersion() == 9854, "Fixture is not a 9854 save");
    ConstructionYard* oldYard = nullptr;
    for(StructureBase* s : structureList) if(s->getOwner() == pLocalHouse && s->getItemID() == Structure_ConstructionYard) oldYard = static_cast<ConstructionYard*>(s);
    require(oldYard != nullptr && queueItems(oldYard) == std::vector<Uint32>({Structure_WindTrap, Structure_Barracks}),
        "The 9854 queue did not load");
    for(const auto& entry : oldYard->getProductionQueue()) require(entry.reservationID == 0, "A 9854 order loaded with a reservation");
    require(oldYard->nextReservationID == 1 && oldYard->reservedPlacementWaitCycles == 0 && oldYard->getProductionProgress() > 0,
        "9854 builder state did not load with defaults");
    require(advanceUntil(*game, 3000, [&] { return oldYard->isWaitingToPlace(); }), "9854 order did not finish");
    require(oldYard->getCurrentProducedItem() == Structure_WindTrap && oldYard->getProductionQueueSize() == 2,
        "A 9854 order was not left for the player to place");
    SDL_Log("PREPLACE_PROBE: 9854 save loaded; its orders keep the manual flow");

    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=preplace_queue outcome=passed");
}
