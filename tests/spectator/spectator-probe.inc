// Test-only entry point for the local spectator fixture, linked against the real production
// objects by run-spectator-probe.py. Nothing here ships in the app, and no production object
// gains a test hook: private state is reached through -fno-access-control in the probe build
// only, exactly as the existing menu and campaign-balance probes do.
#include <fstream>
#include <sstream>
#include <typeinfo>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <RadarView.h>
#include <ScreenBorder.h>
#include <GameInterface.h>
#include <Command.h>
#include <CommandManager.h>
#include <GameInitSettings.h>
#include <players/Player.h>
#include <players/PlayerFactory.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/WindTrap.h>
#include <units/UnitBase.h>
#include <dunecity/CitySimulation.h>
#include <mod/ModManager.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <misc/FileSystem.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <GUI/ObjectInterfaces/DefaultStructureInterface.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <GUI/ObjectInterfaces/WindTrapInterface.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/PlaySetup.h>

namespace {

int spectatorChecks = 0;

void require(bool ok, const char* why) {
    if(!ok) { SDL_Log("SPECTATOR_PROBE_FAIL: %s", why); throw std::runtime_error(why); }
    ++spectatorChecks;
}

/// Hash of every tile's per-team exploration and fog bits. Viewing must not move one of them.
std::size_t explorationFingerprint() {
    std::size_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ULL; };
    for(int y = 0; y < currentGameMap->getSizeY(); ++y)
        for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            for(int team = 0; team < NUM_TEAMS; ++team)
                mix(static_cast<std::size_t>(tile->isExploredByTeam(team)) * 2
                    + static_cast<std::size_t>(tile->isFoggedByTeam(team)));
        }
    return hash;
}

std::vector<House*> housesWithControllers(Game& game) {
    std::vector<House*> result;
    for(int h = 0; h < NUM_HOUSES; ++h) {
        House* candidate = game.getHouse(h);
        if(candidate && !candidate->getPlayerList().empty()) result.push_back(candidate);
    }
    return result;
}

StructureBase* anyStructureOf(const House* owner, Uint32 itemID = ItemID_Invalid) {
    for(StructureBase* structure : structureList) {
        if(structure->getOwner() != owner) continue;
        if(itemID != ItemID_Invalid && structure->getItemID() != itemID) continue;
        return structure;
    }
    return nullptr;
}

StructureBase* anyBuilderOf(const House* owner) {
    for(StructureBase* structure : structureList)
        if(structure->getOwner() == owner && dynamic_cast<BuilderBase*>(structure) != nullptr)
            return structure;
    return nullptr;
}

/// Advances real cycles, including CommandManager::executeCommands, so the AI plays normally.
void advance(Game& game, int cycles) {
    for(int i = 0; i < cycles; ++i) game.updateGameState();
}

/// Finds free ground near a house's existing buildings and plants one there, so the fixture
/// never has to skip a check for want of a building of the right kind.
StructureBase* seedStructure(House* owner, Uint32 itemID) {
    if(StructureBase* existing = anyStructureOf(owner, itemID)) return existing;
    StructureBase* anchor = anyStructureOf(owner);
    const Coord from = anchor ? anchor->getLocation() : Coord(8, 8);
    const Coord size = getStructureSize(itemID);
    for(int radius = 2; radius < 40; ++radius)
        for(int dy = -radius; dy <= radius; ++dy)
            for(int dx = -radius; dx <= radius; ++dx) {
                const int x = from.x + dx, y = from.y + dy;
                if(x < 1 || y < 1 || x + size.x >= currentGameMap->getSizeX()
                   || y + size.y >= currentGameMap->getSizeY()) continue;
                bool clear = true;
                for(int sy = 0; sy < size.y && clear; ++sy)
                    for(int sx = 0; sx < size.x && clear; ++sx) {
                        Tile* tile = currentGameMap->getTile(x + sx, y + sy);
                        if(!tile || tile->hasAGroundObject() || !tile->isRock()
                           || tile->isMountain() || tile->hasCityZone()) clear = false;
                    }
                if(!clear) continue;
                if(StructureBase* placed = owner->placeStructure(NONE_ID, itemID, x, y, true, true))
                    return placed;
            }
    return nullptr;
}

/// Installs the panel through the real sidebar, exactly as selecting the object does.
ObjectInterface* installSidebarPanel(Game& game, ObjectBase* object) {
    for(Uint32 selected : game.selectedList)
        if(auto* previous = game.getObjectManager().getObject(selected)) previous->setSelected(false);
    game.selectedList.clear();
    object->setSelected(true);
    game.selectedList.insert(object->getObjectID());
    game.selectionChanged();
    game.pInterface->updateObjectInterface();
    return game.pInterface->pObjectContainer;
}

} // namespace

/**
    The local-observation fixture. Two real AI factions, no observer controller or start slot.
    Everything is checked against live engine state rather than against the predicates that
    implement the feature, and every refusal is paired with the same action performed by the
    real owner - so a check cannot pass because the command itself stopped working.
*/
void runSpectatorProbe(std::unique_ptr<Game>& owned, const std::string& savePath) {
    Game& game = *owned;

    // ---------------------------------------------------------------- roster and identity
    require(game.isLocalObserver(), "Offline Spectate did not put the game into local observation");
    require(game.isObserving(), "Combined observation predicate is false while locally observing");
    require(!game.isSpectating(), "Local observation must not look like a network spectator");

    const auto houses = housesWithControllers(game);
    require(houses.size() >= 2, "Fixture needs at least two controlled houses");
    int controllers = 0;
    for(House* house : houses)
        for(const auto& player : house->getPlayerList()) {
            ++controllers;
            require(dynamic_cast<HumanPlayer*>(player.get()) == nullptr,
                    "A faction was handed a human controller while spectating");
            require(dynamic_cast<QuantBot*>(player.get()) != nullptr,
                    "Fixture expects every faction to be played by a QuantBot");
            require(game.getPlayerByID(player->getPlayerID()) == player.get(),
                    "A faction controller is not registered under its own id");
            require(player.get() != pLocalPlayer,
                    "The observer identity is in a house's controller list");
        }
    require(controllers == static_cast<int>(game.playerID2Player.size()),
            "The observer consumed a controller slot in the player registry");
    require(pLocalPlayer != nullptr && pLocalHouse != nullptr,
            "The observer still needs a view anchor for the sidebar and camera");
    require(pLocalPlayer->getPlayerID() == Player::OBSERVER_PLAYER_ID,
            "The observer identity does not hold the reserved controller id");
    require(game.getPlayerByID(Player::OBSERVER_PLAYER_ID) == nullptr,
            "The reserved observer id resolves to a registered controller");

    // ------------------------------------------- the minimap is a view, not a radar building
    // The anchor faction deliberately has no radar here, so this is the case where keying the
    // minimap off the view house's installation (or its power) would leave an observer blind.
    for(StructureBase* structure : structureList)
        if(structure->getOwner() == pLocalHouse && structure->getItemID() == Structure_Radar)
            structure->setHealth(0);
    advance(game, 5);
    require(!pLocalHouse->hasRadarOn(),
            "Fixture needs the view house to have no working radar for the minimap check");
    game.pInterface->getRadarView().update();
    const auto radarMode = game.pInterface->getRadarView().currentRadarMode;
    require(radarMode == RadarView::RadarMode::RadarOn
            || radarMode == RadarView::RadarMode::AnimationRadarOn,
            "The real minimap was switched off because the view faction has no powered radar");

    // -------------------------------------------------------- seed what the checks depend on
    // Mandatory, so none of the owner-specific claims below can silently skip.
    House* first = houses[0];
    House* second = houses[1];
    for(House* owner : {first, second}) {
        require(seedStructure(owner, Structure_WindTrap) != nullptr,
                "Could not seed a windtrap for an owner-specific figure check");
        require(seedStructure(owner, Structure_LightFactory) != nullptr,
                "Could not seed a factory for the production panel check");
    }
    require(second->createUnit(Unit_Quad, true) != nullptr, "Could not seed a unit for order checks");
    require(first->createUnit(Unit_Quad, true) != nullptr, "Could not seed a view-house unit");
    advance(game, 20);

    // ------------------------------------------------- cycles advance, no observer defeat
    const Uint32 startCycle = game.gameCycleCount;
    advance(game, 300);
    require(game.gameCycleCount > startCycle, "Cycles did not advance while observing");
    require(!game.finished, "The observed match ended immediately");
    game.setGameLost();
    require(!game.finished, "An observer was defeated with the house it is watching");
    game.setGameWon();
    require(!game.finished, "An observer won the match it is only watching");

    // --------------------------------- viewing neutrality: the option protects no AI faction
    // immortalHumanPlayer is on for this fixture. The guards key off pLocalHouse, which here
    // anchors an AI faction - so without the authority check, watching would make that faction
    // invulnerable and change the result of the match being observed.
    require(game.getGameInitSettings().getGameOptions().immortalHumanPlayer,
            "Fixture needs the immortal-human option enabled to prove viewing neutrality");
    require(!game.localHumanImmortalityApplies(),
            "The immortal-human option still claims to apply while observing");
    {
        StructureBase* anchorBuilding = anyStructureOf(pLocalHouse);
        require(anchorBuilding != nullptr, "Fixture needs a building owned by the view house");
        const FixPoint healthBefore = anchorBuilding->getHealth();
        anchorBuilding->handleDamage(40, NONE_ID, second);
        require(anchorBuilding->getHealth() < healthBefore,
                "Watching made the view faction's building immortal");
        StructureBase* foreignBuilding = anyStructureOf(second);
        require(foreignBuilding != nullptr, "Fixture needs a building owned by the other faction");
        const FixPoint foreignBefore = foreignBuilding->getHealth();
        foreignBuilding->handleDamage(40, NONE_ID, first);
        require(foreignBuilding->getHealth() < foreignBefore,
                "Watching made the other faction's building immortal");
    }
    {
        // The spice-bloom guard shares the same authority. Stand a view-house unit on a bloom.
        UnitBase* victim = nullptr;
        for(UnitBase* candidate : unitList)
            if(candidate->getOwner() == pLocalHouse && candidate->isAGroundUnit()
               && candidate->getItemID() != Unit_Sandworm) { victim = candidate; break; }
        if(victim != nullptr) {
            const Coord where = victim->getLocation();
            Tile* tile = currentGameMap->getTile(where);
            tile->setType(Terrain_SpiceBloom);
            require(tile->isSpiceBloom(), "Could not place a spice bloom under the fixture unit");
            // UnitBase::deploy is where the bloom is stepped on and the guard is applied.
            victim->deploy(where);
            require(victim->getHealth() <= 0,
                    "Watching let a view-faction unit survive a spice bloom");
        } else {
            SDL_Log("SPECTATOR_PROBE_DETAIL: no eligible ground unit; spice-bloom guard not exercised");
        }
    }

    // ------------------------- the AI keeps playing, through the same command queue
    // Bounded progress: the factions must still be building and spending while watched. Counts
    // alone are a poor signal on a seeded fixture (a loss cancels a gain), so this reads the
    // cumulative built value and the credit balances, which only a playing AI moves.
    long long builtBefore = 0, creditsBefore = 0;
    for(House* house : houses) {
        builtBefore += house->getBuiltValue();
        creditsBefore += house->getCredits();
    }
    advance(game, 2500);
    long long builtAfter = 0, creditsAfter = 0;
    for(House* house : houses) {
        builtAfter += house->getBuiltValue();
        creditsAfter += house->getCredits();
    }
    require(builtAfter > builtBefore || creditsAfter != creditsBefore,
            "No faction built or spent anything while being observed");
    SDL_Log("SPECTATOR_PROBE_DETAIL: observed factions built %lld -> %lld, credits %lld -> %lld",
            builtBefore, builtAfter, creditsBefore, creditsAfter);

    // A real controller's city command, enqueued on the shared CommandManager and executed by
    // ordinary cycle advancement - the queue the network spectator gate disables, and which
    // local observation deliberately leaves alone because the city AI uses it.
    {
        Player* controller = houses.front()->getPlayerList().front().get();
        Coord road = Coord::Invalid();
        for(int y = 0; y < currentGameMap->getSizeY() && road.isInvalid(); ++y)
            for(int x = 0; x < currentGameMap->getSizeX() && road.isInvalid(); ++x) {
                Tile* tile = currentGameMap->getTile(x, y);
                if(tile->isRock() && !tile->isMountain() && !tile->hasAStructure()
                   && !tile->isRoad() && !tile->hasCityZone()) road = Coord(x, y);
            }
        require(road.isValid(), "Fixture needs a free rock tile for the AI road check");
        game.getCommandManager().addCommand(
            Command(controller->getPlayerID(), CMD_CITY_TOOL,
                    static_cast<Uint32>(road.x), static_cast<Uint32>(road.y),
                    static_cast<Uint32>(DuneCity::CityTool_Road)));
        advance(game, 8);
        require(currentGameMap->getTile(road)->isRoad(),
                "A real AI city command queued through CommandManager never executed while observing");
        currentGameMap->getTile(road)->setRoad(false);

        // And the same command from the observer, through the same queue, must do nothing.
        game.getCommandManager().addCommand(
            Command(Player::OBSERVER_PLAYER_ID, CMD_CITY_TOOL,
                    static_cast<Uint32>(road.x), static_cast<Uint32>(road.y),
                    static_cast<Uint32>(DuneCity::CityTool_Road)));
        advance(game, 8);
        require(!currentGameMap->getTile(road)->isRoad(),
                "An observer's queued city command built a road");
    }

    // ------------------------------------------- full map is presentation, never simulation
    const std::size_t explorationBefore = explorationFingerprint();
    screenborder->setNewScreenCenter(Coord(currentGameMap->getSizeX() * TILESIZE / 2,
                                           currentGameMap->getSizeY() * TILESIZE / 2));
    for(int frame = 0; frame < 3; ++frame) game.drawScreen();
    int radarOwnerColours = 0, radarBlack = 0;
    for(int y = 0; y < currentGameMap->getSizeY(); ++y)
        for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            if(!tile->hasAnObject() || tile->isExploredByTeam(pLocalHouse->getTeamID())) continue;
            if(tile->getRadarColor(pLocalHouse, true) == COLOR_BLACK) ++radarBlack;
            else ++radarOwnerColours;
        }
    require(explorationFingerprint() == explorationBefore,
            "Viewing the whole map changed the simulation's exploration or fog state");
    require(radarOwnerColours > 0, "The radar showed nothing in territory the view house never explored");
    require(radarBlack == 0, "The radar still blacked out unexplored tiles for an observer");

    // ------------------------------------------- selecting and inspecting every owner's work
    StructureBase* unseen = nullptr;
    for(StructureBase* structure : structureList) {
        if(structure->getOwner() == pLocalHouse) continue;
        Tile* tile = currentGameMap->getTile(structure->getLocation());
        if(tile && !tile->isExploredByTeam(pLocalHouse->getTeamID())) { unseen = structure; break; }
    }
    require(unseen != nullptr, "Fixture needs a foreign structure outside the view house's exploration");
    game.selectedList.clear();
    currentGameMap->selectObjects(pLocalHouse,
        unseen->getLocation().x, unseen->getLocation().y,
        unseen->getLocation().x, unseen->getLocation().y,
        unseen->getLocation().x * TILESIZE + TILESIZE / 2,
        unseen->getLocation().y * TILESIZE + TILESIZE / 2, false);
    require(game.selectedList.count(unseen->getObjectID()) == 1,
            "An observer could not select a building on a tile the view house never explored");

    // Owner-specific figures: two owners, deliberately different power demand, each panel
    // reporting its own owner rather than the view house. Mandatory on both sides.
    first->powerRequirement = 111;
    second->powerRequirement = 222;
    std::string firstFigure, secondFigure;
    for(House* owner : {first, second}) {
        StructureBase* trap = anyStructureOf(owner, Structure_WindTrap);
        require(trap != nullptr, "An owner lost its seeded windtrap");
        auto* panel = installSidebarPanel(game, trap);
        auto* wind = dynamic_cast<WindTrapInterface*>(panel);
        require(wind != nullptr, "A windtrap did not give an observer the owner's power panel");
        wind->update();
        const std::string figure = wind->requiredEnergyLabel.getText();
        require(figure.find(owner == first ? "111" : "222") != std::string::npos,
                "A windtrap panel did not report its own owner's power requirement");
        (owner == first ? firstFigure : secondFigure) = figure;
        screenborder->setNewScreenCenter(trap->getLocation() * TILESIZE);
        game.drawScreen();
        int width, height;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(
            0, width, height, 32, SDL_PIXELFORMAT_ARGB8888));
        require(surface && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                                surface->pixels, surface->pitch) == 0,
                "Could not capture the observer's building detail panel");
        const std::string screenshot = std::string(std::getenv("SPECTATOR_PROBE_OUT"))
            + (owner == first ? "/spectator-first-house.bmp" : "/spectator-second-house.bmp");
        require(SDL_SaveBMP(surface.get(), screenshot.c_str()) == 0,
                "Could not write the observer's building detail screenshot");
    }
    require(firstFigure != secondFigure, "Both owners' panels showed the same power figure");
    SDL_Log("SPECTATOR_PROBE_DETAIL: owner panels reported '%s' and '%s'",
            firstFigure.c_str(), secondFigure.c_str());

    for(House* owner : {first, second}) {
        StructureBase* factory = anyBuilderOf(owner);
        require(factory != nullptr, "An owner lost its seeded factory");
        auto* panel = installSidebarPanel(game, factory);
        require(dynamic_cast<BuilderInterface*>(panel) != nullptr,
                "A factory did not give an observer the owner's production panel");
        require(factory->mayShowOwnerInterface(),
                "An observer was refused the owner panel for a standing factory");
        panel->update();
        auto* builderPanel = static_cast<BuilderInterface*>(panel);
        require(!builderPanel->upgradeButton.isVisible(),
                "The Upgrade button was offered to an observer after an update");
        require(!builderPanel->repairButton.isVisible(),
                "The Repair button was offered to an observer after an update");
        require(builderPanel->pBuilderList != nullptr,
                "The production list was missing from an observer's factory panel");
    }

    // ------------------------------------- the real sidebar container is installed read-only
    {
        StructureBase* factory = anyBuilderOf(first);
        auto* panel = installSidebarPanel(game, factory);
        require(panel != nullptr, "The sidebar installed no panel for the selected factory");
        require(!panel->isEnabled(),
                "The sidebar installed an enabled panel container while observing");
        require(panel->isVisible(), "The observer's panel container is not drawn at all");

        auto* builder = static_cast<BuilderBase*>(factory);
        const auto queueBefore = builder->getProductionQueueSize();
        const bool repairingBefore = builder->isRepairing();
        const bool upgradingBefore = builder->isUpgrading();
        // Sweep real input across the whole installed panel, through the same entry points
        // GameInterface uses. A disabled container returns false without descending, so no
        // child button, build item or action control can fire its callback.
        bool handled = false;
        for(int y = 0; y < panel->getSize().y; y += 8)
            for(int x = 0; x < panel->getSize().x; x += 8) {
                handled |= panel->handleMouseLeft(x, y, true);
                handled |= panel->handleMouseLeft(x, y, false);
                handled |= panel->handleMouseRight(x, y, true);
                handled |= panel->handleMouseRight(x, y, false);
            }
        handled |= panel->handleKeyPress(const_cast<SDL_KeyboardEvent&>(
            *[]{ static SDL_KeyboardEvent event{}; event.keysym.sym = SDLK_RETURN; return &event; }()));
        require(!handled, "The observer's panel container accepted input");
        auto* list = static_cast<BuilderInterface*>(panel)->pBuilderList;
        require(list != nullptr, "The observer needs a factory list to inspect");
        const Point originalSize = list->getSize();
        list->resize(originalSize.x, 120);
        list->currentListPos = 0;
        require(builder->getBuildList().size() > static_cast<std::size_t>(list->getNumButtons(120)),
                "The scrolling fixture has no offscreen production entries");
        const auto creditsBeforeScroll = first->getCredits();
        const auto explorationBeforeScroll = explorationFingerprint();
        require(panel->handleMouseWheel(0, 0, false) && list->currentListPos == 1,
                "An observer could not scroll to offscreen production details");
        require(builder->getProductionQueueSize() == queueBefore
                && first->getCredits() == creditsBeforeScroll
                && explorationFingerprint() == explorationBeforeScroll,
                "Inspecting offscreen production details changed the match");
        list->resize(originalSize);
        panel->update();
        advance(game, 10);
        require(builder->getProductionQueueSize() == queueBefore,
                "Clicking an observer's panel changed the owner's production queue");
        require(builder->isRepairing() == repairingBefore,
                "Clicking an observer's panel started a repair");
        require(builder->isUpgrading() == upgradingBefore,
                "Clicking an observer's panel started an upgrade");
    }

    // ------------------------------------------------------- every mutation route is refused
    const auto runAs = [](Uint8 issuer, CMDTYPE id, std::vector<Uint32> parameters) {
        Command command(issuer, id, 0u);
        command.parameter = std::move(parameters);
        command.executeCommand();
    };

    {
        StructureBase* target = anyStructureOf(second);
        Player* owner = second->getPlayerList().front().get();
        target->health = target->getMaxHealth() / 2;
        target->repairing = false;
        runAs(Player::OBSERVER_PLAYER_ID, CMD_STRUCTURE_REPAIR, {target->getObjectID()});
        require(!target->isRepairing(), "An observer started a repair on another house's building");
        runAs(owner->getPlayerID(), CMD_STRUCTURE_REPAIR, {target->getObjectID()});
        require(target->isRepairing(), "The owner's own repair command stopped working");
        target->repairing = false;
    }

    {
        auto* builder = static_cast<BuilderBase*>(anyBuilderOf(second));
        Player* owner = second->getPlayerList().front().get();
        const auto queueBefore = builder->getProductionQueueSize();
        Uint32 buildable = ItemID_Invalid;
        for(const auto& item : builder->getBuildList())
            if(builder->isAvailableToBuild(item.itemID)) { buildable = item.itemID; break; }
        require(buildable != ItemID_Invalid, "The seeded factory can build nothing to test with");
        runAs(Player::OBSERVER_PLAYER_ID, CMD_BUILDER_PRODUCEITEM,
              {builder->getObjectID(), buildable, 1u});
        require(builder->getProductionQueueSize() == queueBefore,
                "An observer queued production in another house's factory");
        runAs(owner->getPlayerID(), CMD_BUILDER_PRODUCEITEM,
              {builder->getObjectID(), buildable, 1u});
        require(builder->getProductionQueueSize() > queueBefore,
                "The owner's own production command stopped working");
        runAs(owner->getPlayerID(), CMD_BUILDER_CANCELITEM,
              {builder->getObjectID(), buildable, 1u});
    }

    {
        // Unit orders against the *view* house: the case the reserved controller id exists for,
        // because the detached identity is attached to exactly that house.
        UnitBase* unit = nullptr;
        for(UnitBase* candidate : unitList)
            if(candidate->getOwner() == pLocalHouse && candidate->isRespondable()
               && candidate->getHealth() > 0) { unit = candidate; break; }
        require(unit != nullptr, "Fixture needs a live view-house unit for the order check");
        const Coord destinationBefore = unit->getDestination();
        const Coord elsewhere(std::max(1, currentGameMap->getSizeX() - 3),
                              std::max(1, currentGameMap->getSizeY() - 3));
        runAs(Player::OBSERVER_PLAYER_ID, CMD_UNIT_MOVE2POS,
              {unit->getObjectID(), static_cast<Uint32>(elsewhere.x),
               static_cast<Uint32>(elsewhere.y), 1u});
        require(unit->getDestination() == destinationBefore,
                "An observer gave a movement order to the house it is anchored to");
    }

    {
        // City commands carry no acting object, so they have no owner check of their own.
        Coord rock = Coord::Invalid();
        for(int y = 0; y < currentGameMap->getSizeY() && rock.isInvalid(); ++y)
            for(int x = 0; x < currentGameMap->getSizeX() && rock.isInvalid(); ++x) {
                Tile* tile = currentGameMap->getTile(x, y);
                if(tile->isRock() && !tile->isMountain() && !tile->hasANonInfantryGroundObject()
                   && !tile->hasCityZone() && !tile->isRoad()) rock = Coord(x, y);
            }
        require(rock.isValid(), "Fixture needs a free rock tile for the city tool checks");
        Tile* tile = currentGameMap->getTile(rock);
        Player* owner = houses.front()->getPlayerList().front().get();

        runAs(Player::OBSERVER_PLAYER_ID, CMD_CITY_PLACE_ZONE,
              {static_cast<Uint32>(rock.x), static_cast<Uint32>(rock.y),
               static_cast<Uint32>(DuneCity::ZoneType::Residential)});
        require(!tile->hasCityZone(), "An observer zoned a tile");
        runAs(owner->getPlayerID(), CMD_CITY_PLACE_ZONE,
              {static_cast<Uint32>(rock.x), static_cast<Uint32>(rock.y),
               static_cast<Uint32>(DuneCity::ZoneType::Residential)});
        require(tile->hasCityZone(), "A real controller's zoning command stopped working");
        tile->setCityZoneType(DuneCity::ZoneType::None);

        if(auto* city = game.getCitySimulation(); city && city->isInitialized()) {
            const auto taxBefore = city->getCityTax();
            const Uint32 otherTax = static_cast<Uint32>(taxBefore == 9 ? 8 : 9);
            runAs(Player::OBSERVER_PLAYER_ID, CMD_CITY_SET_TAX_RATE, {0u, otherTax});
            require(city->getCityTax() == taxBefore, "An observer changed the city tax rate");
            runAs(owner->getPlayerID(), CMD_CITY_SET_TAX_RATE, {0u, otherTax});
            require(city->getCityTax() == static_cast<int16_t>(otherTax),
                    "A real controller's tax command stopped working");
            runAs(owner->getPlayerID(), CMD_CITY_SET_TAX_RATE, {0u, static_cast<Uint32>(taxBefore)});
        }
    }

    {
        // A command that acts on the raw issuer id before any ownership or parameter check.
        Player* owner = houses.front()->getPlayerList().front().get();
        game.pausedPlayers.clear();
        runAs(Player::OBSERVER_PLAYER_ID, CMD_PLAYER_PAUSE, {});
        require(game.pausedPlayers.empty(),
                "An observer inserted itself into the paused-player set");
        runAs(owner->getPlayerID(), CMD_PLAYER_PAUSE, {});
        require(game.pausedPlayers.count(owner->getPlayerID()) == 1,
                "A real controller's pause command stopped working");
        runAs(owner->getPlayerID(), CMD_PLAYER_RESUME, {});
        game.pausedPlayers.clear();
    }

    // Cheat codes typed into chat are not Commands, so they need their own refusal.
    {
        game.bCheatsEnabled = true;
        const bool debugBefore = debug;
        const bool immortalBefore = game.getGameInitSettings().getGameOptions().immortalHumanPlayer;
        const auto creditsBefore = pLocalHouse->getCredits();
        const std::size_t worldBefore = explorationFingerprint();
        // "cheat" (enable), "money" (credits), "debug", "muaddib" (immortality), "win".
        for(const char* typed : {"cheat", "money", "debug", "muaddib", "win"}) {
            game.chatMode = true;
            game.typingChatMessage = typed;
            SDL_KeyboardEvent entered{};
            entered.keysym.sym = SDLK_RETURN;
            game.handleChatInput(entered);
        }
        require(pLocalHouse->getCredits() == creditsBefore,
                "A chat cheat gave an observer's view house credits");
        require(debug == debugBefore, "A chat cheat turned on debug mode while observing");
        require(game.getGameInitSettings().getGameOptions().immortalHumanPlayer == immortalBefore,
                "A chat cheat toggled immortality while observing");
        require(!game.finished, "A chat cheat ended the observed match");
        require(explorationFingerprint() == worldBefore,
                "A chat cheat changed world state while observing");
        game.bCheatsEnabled = false;
    }

    // The input layer refuses to even enter a mode that could issue those commands.
    for(int mode : {Game::CursorMode_Placing, Game::CursorMode_Attack, Game::CursorMode_Move,
                    Game::CursorMode_Capture, Game::CursorMode_CarryallDrop, Game::CursorMode_Heal,
                    Game::CursorMode_CityZone, Game::CursorMode_CityRoad}) {
        game.setCursorMode(mode);
        require(game.currentCursorMode == Game::CursorMode_Normal,
                "An observer was allowed into an order-issuing cursor mode");
    }
    game.onCityBudget();
    require(!game.pInterface->hasChildWindow(), "The city budget window opened for an observer");
    require(!game.canSkipMission(), "An observer was offered Skip Mission");

    // ------------------------------- the local-only setup flag is not part of the save format
    {
        GameInitSettings streamed = game.getGameInitSettings();
        require(streamed.isLocalSpectator(),
                "The live init settings lost the local Spectate choice");
        OMemoryStream out;
        out.open();
        streamed.save(out);
        const std::string encoded(out.getData(), out.getDataLength());
        IMemoryStream in(encoded.c_str(), encoded.size());
        GameInitSettings decoded(in);
        require(!decoded.isLocalSpectator(),
                "GameInitSettings serialized the local-only Spectate flag");
        require(decoded.getGameType() == streamed.getGameType()
                && decoded.getRandomSeed() == streamed.getRandomSeed(),
                "The GameInitSettings round trip lost ordinary fields");
    }

    // --------------------------------------------------------------- save and reload the view
    const std::size_t explorationAtSave = explorationFingerprint();
    const Uint32 cycleAtSave = game.gameCycleCount;
    require(game.saveGame(savePath.c_str()), "Could not save the observed match");

    // Destroy the old Game before building the reload: Game's destructor deletes the global
    // map and screen border, so two live Games would have the second one's globals torn down
    // by the first one's destructor. This is the same ordering the spice-income fixture uses.
    owned.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;

    auto reloaded = std::make_unique<Game>();
    currentGame = reloaded.get();
    // GameInitSettings(savePath) is exactly what the in-game Load builds, and it has no
    // Spectate flag - so observation has to come back out of the savegame's own
    // local-player byte and nothing else.
    GameInitSettings resumeSettings(savePath);
    require(!resumeSettings.isLocalSpectator(),
            "Settings built from a savegame carried the local-only Spectate flag");
    reloaded->bReplay = true;
    reloaded->initGame(resumeSettings);
    reloaded->bReplay = false;
    reloaded->initializeGameLoop();
    reloaded->bPause = false;

    require(reloaded->isLocalObserver(), "Observation was lost across save and reload");
    require(reloaded->isObserving(), "The reloaded game is not observing");
    require(!reloaded->isSpectating(), "The reloaded local observer looks like a network spectator");
    require(pLocalPlayer != nullptr && pLocalPlayer->getPlayerID() == Player::OBSERVER_PLAYER_ID,
            "The reloaded observer identity lost the reserved controller id");
    require(reloaded->getPlayerByID(Player::OBSERVER_PLAYER_ID) == nullptr,
            "The reloaded observer id was registered as a controller");
    require(reloaded->gameCycleCount == cycleAtSave, "The reload resumed at a different cycle");
    require(explorationFingerprint() == explorationAtSave,
            "Exploration state changed across the observer save and reload");
    require(!reloaded->localHumanImmortalityApplies(),
            "The reloaded observer claimed the immortal-human option");
    const auto reloadedHouses = housesWithControllers(*reloaded);
    require(reloadedHouses.size() >= 2, "The reload lost a faction");
    for(House* house : reloadedHouses)
        for(const auto& player : house->getPlayerList())
            require(dynamic_cast<HumanPlayer*>(player.get()) == nullptr,
                    "A reloaded faction was handed a human controller");
    {
        reloaded->pInterface->getRadarView().update();
        const auto mode = reloaded->pInterface->getRadarView().currentRadarMode;
        require(mode == RadarView::RadarMode::RadarOn
                || mode == RadarView::RadarMode::AnimationRadarOn,
                "The reloaded observer lost the minimap");
    }
    {
        StructureBase* target = anyStructureOf(reloadedHouses.back());
        require(target != nullptr, "The reload lost the other faction's buildings");
        target->health = target->getMaxHealth() / 2;
        target->repairing = false;
        runAs(Player::OBSERVER_PLAYER_ID, CMD_STRUCTURE_REPAIR, {target->getObjectID()});
        require(!target->isRepairing(), "A reloaded observer could repair another house's building");
    }
    long long reloadedBefore = 0;
    for(House* house : reloadedHouses) reloadedBefore += house->getNumStructures() + house->getNumUnits();
    advance(*reloaded, 400);
    require(reloaded->gameCycleCount > cycleAtSave, "The reloaded observed match did not advance");
    long long reloadedAfter = 0;
    for(House* house : reloadedHouses) reloadedAfter += house->getNumStructures() + house->getNumUnits();
    require(reloadedAfter >= reloadedBefore, "The reloaded factions lost ground immediately");

    // Use the same next-game settings copy as the Restart Game confirmation.
    reloaded->setNextGameInitSettings(reloaded->getGameInitSettings());
    const GameInitSettings restartSettings = reloaded->getNextGameInitSettings();
    require(restartSettings.isLocalSpectator(),
            "Restarting a loaded observer lost the local spectator setting");
    reloaded.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;

    auto restarted = std::make_unique<Game>();
    currentGame = restarted.get();
    restarted->bReplay = true;
    restarted->initGame(restartSettings);
    restarted->bReplay = false;
    restarted->initializeGameLoop();
    restarted->bPause = false;
    require(restarted->isLocalObserver() && !restarted->isSpectating(),
            "A restarted match did not restore local observation");
    require(pLocalPlayer && pLocalPlayer->getPlayerID() == Player::OBSERVER_PLAYER_ID
            && restarted->getPlayerByID(Player::OBSERVER_PLAYER_ID) == nullptr,
            "A restarted observer acquired a registered controller");
    const auto restartedHouses = housesWithControllers(*restarted);
    require(restartedHouses.size() == houses.size(), "Restarting lost an observed faction");
    for(House* house : restartedHouses)
        for(const auto& player : house->getPlayerList())
            require(dynamic_cast<HumanPlayer*>(player.get()) == nullptr,
                    "A restarted faction was handed a human controller");
    advance(*restarted, 10);
    require(restarted->gameCycleCount > 0, "The restarted observed match did not advance");
    restarted.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;

    SDL_Log("SPECTATOR_PROBE_PASS: %d checks - all-AI roster with no observer slot, real minimap "
            "without a powered radar faction, viewing neutrality under immortalHumanPlayer, full "
            "map and radar without touching exploration, unseen foreign selection, owner-specific "
            "panels for two owners through the real sidebar, a read-only installed container, "
            "every command and chat-cheat mutation route refused while the owners' own commands "
            "still execute through the shared queue, and observation preserved across save/reload "
            "and restarting a loaded match",
            spectatorChecks);
}

/**
    Ordinary (non-observing) save/reload, so the shared local-player byte is proven not to have
    changed meaning for a normal single-player game, and normal human immortality is proven to
    be intact.
*/
void runOrdinaryPlayerRoundTrip(std::unique_ptr<Game>& owned, const std::string& savePath) {
    Game& game = *owned;
    require(!game.isObserving(), "The ordinary fixture must not be observing");
    require(!game.isLocalObserver(), "The ordinary fixture must not be a local observer");
    require(pLocalPlayer != nullptr && dynamic_cast<HumanPlayer*>(pLocalPlayer) != nullptr,
            "The ordinary fixture needs a human controller");
    require(pLocalPlayer->getPlayerID() != Player::OBSERVER_PLAYER_ID,
            "An ordinary human controller was given the reserved observer id");
    const Uint8 humanID = pLocalPlayer->getPlayerID();
    const int humanHouseID = pLocalHouse->getHouseID();

    // Normal human immortality still applies: the authority check must not have taken it away.
    require(game.getGameInitSettings().getGameOptions().immortalHumanPlayer,
            "Fixture needs the immortal-human option enabled");
    require(game.localHumanImmortalityApplies(),
            "An ordinary human house lost the immortal-human option");
    {
        StructureBase* mine = anyStructureOf(pLocalHouse);
        require(mine != nullptr, "The ordinary fixture needs a building of its own");
        const FixPoint healthBefore = mine->getHealth();
        mine->handleDamage(40, NONE_ID, nullptr);
        require(mine->getHealth() == healthBefore,
                "An ordinary human house lost its immortality protection");
    }

    advance(game, 60);
    const Uint32 cycleAtSave = game.gameCycleCount;
    require(game.saveGame(savePath.c_str()), "Could not save the ordinary match");

    owned.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;

    auto reloaded = std::make_unique<Game>();
    currentGame = reloaded.get();
    reloaded->bReplay = true;
    reloaded->initGame(GameInitSettings(savePath));
    reloaded->bReplay = false;
    reloaded->initializeGameLoop();
    reloaded->bPause = false;

    require(!reloaded->isLocalObserver(), "An ordinary save came back as an observer");
    require(!reloaded->isObserving(), "An ordinary save came back observing");
    require(pLocalPlayer != nullptr && pLocalPlayer->getPlayerID() == humanID,
            "An ordinary save did not restore its own human controller");
    require(dynamic_cast<HumanPlayer*>(pLocalPlayer) != nullptr,
            "An ordinary save restored a non-human local player");
    require(pLocalHouse != nullptr && pLocalHouse->getHouseID() == humanHouseID,
            "An ordinary save restored the wrong local house");
    require(reloaded->gameCycleCount == cycleAtSave,
            "The ordinary reload resumed at a different cycle");
    require(reloaded->localHumanImmortalityApplies(),
            "The reloaded ordinary human house lost the immortal-human option");
    // The ordinary minimap still follows the house's own radar, as it always did.
    reloaded->pInterface->getRadarView().update();
    const auto mode = reloaded->pInterface->getRadarView().currentRadarMode;
    const bool radarOn = mode == RadarView::RadarMode::RadarOn
                      || mode == RadarView::RadarMode::AnimationRadarOn;
    require(radarOn == pLocalHouse->hasRadarOn(),
            "An ordinary game's minimap no longer follows its own house's radar");
    advance(*reloaded, 60);

    // Multiplayer-format saves omit the local-player byte. Opening one through the
    // local Load menu must find a real human, including under a different local profile.
    const std::string networkSavePath = savePath + ".network";
    reloaded->gameInitSettings.gameType = GameType::CustomMultiplayer;
    require(reloaded->saveGame(networkSavePath.c_str()), "Could not save a multiplayer-layout control");
    reloaded.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    const std::string originalName = settings.general.playerName;
    for(const std::string& profile : {originalName, std::string("Different local profile")}) {
        settings.general.playerName = profile;
        auto localLoad = std::make_unique<Game>();
        currentGame = localLoad.get();
        localLoad->bReplay = true;
        localLoad->initGame(GameInitSettings(networkSavePath));
        localLoad->bReplay = false;
        localLoad->initializeGameLoop();
        require(!localLoad->isObserving(), "A multiplayer-layout control loaded as an observer");
        require(pLocalPlayer && pLocalPlayer->getPlayerID() == humanID,
                "Local loading a multiplayer save failed to restore a real human");
        require(pLocalHouse && pLocalHouse->getHouseID() == humanHouseID,
                "Local loading a multiplayer save selected the wrong house");
        require(localLoad->getPlayerByID(humanID) == pLocalPlayer,
                "The restored multiplayer human is not a registered controller");
        localLoad.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }
    settings.general.playerName = originalName;
    SDL_Log("SPECTATOR_PROBE_PASS: %d checks - ordinary single-player keeps its own human "
            "controller and immortality, its minimap still follows its own radar, and its save "
            "is never read back as an observer; multiplayer-layout saves also restore a real "
            "human when loaded locally with the same or a different profile",
            spectatorChecks);
}

/**
    The Offline Custom Game setup row itself, against the real CustomGamePlayers widgets.
*/
void runSpectatorSetupProbe() {
    ModManager::instance().setActiveMod("dunecity");
    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    settings.general.playerName = "Spectator probe";

    const char* mapPath = std::getenv("SPECTATOR_MAP");
    const std::string contents = readCompleteFile(mapPath);
    const auto build = [&](CustomPlaySetup& setup) {
        return std::make_unique<CustomGamePlayers>(
            GameInitSettings(getBasename(mapPath, true), contents, setup.sharedHouse, setup.rules),
            true, false, &setup);
    };
    const auto capture = [](Window& menu, const std::string& name) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        menu.draw();
        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888));
        require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                     surface->pixels, surface->pitch) == 0, "Capture failed");
        const std::string path = std::string(std::getenv("SPECTATOR_PROBE_OUT")) + "/" + name + ".bmp";
        require(SDL_SaveBMP(surface.get(), path.c_str()) == 0, "Screenshot write failed");
        SDL_Log("SPECTATOR_PROBE_DETAIL: screenshot %s", path.c_str());
    };

    CustomPlaySetup setup;
    setup.rules = effectiveGameOptions;
    setup.maps = {mapPath};
    setup.mods = ModManager::instance().listModChoices();
    setup.online = false;

    // Offline, not yet watching: the checkbox exists, is clear, and row 0 holds the human.
    {
        auto menu = build(setup);
        require(!menu->spectating, "A fresh Offline setup started out observing");
        require(menu->setupSpectate.isVisible(), "The Offline setup row has no Spectate checkbox");
        require(!menu->setupSpectate.isChecked(), "The Spectate checkbox started out checked");
        require(menu->houseInfo[0].player1DropDown.getSelectedEntry() == settings.general.playerName,
                "A fresh Offline setup did not seat the local player in the first row");
        capture(*menu, "custom-game-offline-not-spectating");
        // Configure the other rows, so the toggle can be shown to preserve them.
        menu->houseInfo[1].teamDropDown.setSelectedItem(1);
        setup.players = menu->getChangeEventList();
    }

    // Turning Spectate on: the human seat becomes an AI and no row keeps a human.
    {
        auto menu = build(setup);
        menu->setupSpectate.setChecked(true);
        menu->setupSpectate.pOnClick();
        require(setup.spectate, "Clicking Spectate did not record the choice in the setup");
        require(!setup.players.changeEventList.empty(),
                "Toggling Spectate discarded the configured roster");
        bool humanEvent = false, aiSeatEvent = false;
        for(const auto& event : setup.players.changeEventList) {
            if(event.eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer)
                humanEvent = true;
            if(event.slot == 0
               && event.eventType == ChangeEventList::ChangeEvent::EventType::ChangePlayer)
                aiSeatEvent = true;
        }
        require(!humanEvent, "The retained roster still seats a human while spectating");
        require(aiSeatEvent, "The human seat was not converted to an AI row");
    }

    // Rebuilt while watching: every row is an AI, and no faction is consumed by the observer.
    {
        auto menu = build(setup);
        require(menu->spectating, "The rebuilt Offline setup did not stay in Spectate");
        require(menu->setupSpectate.isChecked(), "The rebuilt Spectate checkbox lost its state");
        int aiRows = 0;
        for(int row = 0; row < menu->numHouses; ++row) {
            const auto& box = menu->houseInfo[row].player1DropDown;
            require(box.getSelectedEntry() != settings.general.playerName,
                    "A faction row was handed the local human while spectating");
            require(box.getSelectedEntryIntData() != 0, "A row still reports a human controller");
            if(box.getSelectedEntryIntData() > 0) ++aiRows;
        }
        require(aiRows >= 2, "Watching left fewer than two AI factions configured");
        require(!menu->setupShared.isEnabled(), "Shared house stayed editable while spectating");
        // The human is refused even if a stale seat event is replayed.
        menu->setPlayer2Slot(settings.general.playerName, 0);
        require(menu->houseInfo[0].player1DropDown.getSelectedEntry() != settings.general.playerName,
                "A replayed seat event seated a human while spectating");
        capture(*menu, "custom-game-offline-spectating");
        menu->addAllPlayersToGameInitSettings();
        require(menu->gameInitSettings.isLocalSpectator(),
                "The started game was not marked as locally observed");
        int humanRows = 0;
        for(const auto& row : menu->gameInitSettings.getHouseInfoList())
            for(const auto& player : row.playerInfoList)
                if(player.playerClass == HUMANPLAYERCLASS) ++humanRows;
        require(humanRows == 0, "A started observed game still contains a human controller");
        require(menu->gameInitSettings.getHouseInfoList().size() >= 2,
                "A started observed game has fewer than two factions");
    }

    // Preserved across a map/mod/rules rebuild, which is a fresh menu over the same setup.
    {
        setup.rules.fogOfWar = !setup.rules.fogOfWar;
        auto menu = build(setup);
        require(menu->spectating, "Spectate was lost across a rules rebuild");
        setup.rules.fogOfWar = !setup.rules.fogOfWar;
    }

    // Switching to Online clears it, so a network host can never become a local observer.
    {
        setup.online = true;
        auto menu = build(setup);
        require(!setup.spectate, "Choosing Online left the Spectate choice set");
        require(!menu->spectating, "An Online setup is observing");
        require(!menu->setupSpectate.isVisible(),
                "The Spectate checkbox is offered in an Online setup");
        setup.online = false;
    }

    // Turning it back off restores an ordinary human seat while keeping the AI rows.
    {
        setup.spectate = true;
        auto menu = build(setup);
        require(menu->spectating, "The setup did not return to Spectate for the toggle-off check");
        menu->houseInfo[1].player1DropDown.setSelectedItem(
            menu->houseInfo[1].player1DropDown.getNumEntries() - 1);
        const std::string keptAI = menu->houseInfo[1].player1DropDown.getSelectedEntry();
        menu->setupSpectate.setChecked(false);
        menu->setupSpectate.pOnClick();
        require(!setup.spectate, "Unchecking Spectate did not clear the choice");

        auto restored = build(setup);
        require(!restored->spectating, "The rebuilt setup is still observing");
        require(restored->houseInfo[0].player1DropDown.getSelectedEntry() == settings.general.playerName,
                "Turning Spectate off did not restore an ordinary human seat");
        require(restored->houseInfo[1].player1DropDown.getSelectedEntry() == keptAI,
                "Turning Spectate off discarded a configured AI row");
        capture(*restored, "custom-game-offline-spectate-off");
    }

    SDL_Log("SPECTATOR_PROBE_PASS: %d checks - the Offline Spectate checkbox exists and is "
            "offline-only, replaces the human seat with an AI while keeping configured rows, "
            "survives a rules rebuild, is cleared by choosing Online, refuses a replayed human "
            "seat, starts an all-AI locally observed game, and restores a human when turned off",
            spectatorChecks);
}

int runSpectatorProbeMain() {
    const std::string out = std::getenv("SPECTATOR_PROBE_OUT") ? std::getenv("SPECTATOR_PROBE_OUT") : ".";
    if(std::getenv("SPECTATOR_SETUP_PROBE")) {
        runSpectatorSetupProbe();
        return MENU_QUIT_DEFAULT;
    }

    const std::string mod = std::getenv("SPECTATOR_MOD") ? std::getenv("SPECTATOR_MOD") : "dunecity";
    if(!ModManager::instance().setActiveMod(mod) || ModManager::instance().getActiveModName() != mod)
        throw std::runtime_error("Requested probe mod did not activate: " + mod);

    settings.general.playerName = "Spectator probe";
    settings.network.metaServer = "http://127.0.0.1:1/";
    settings.gameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    // Fog on and no pre-explored map: the fixture depends on the view house genuinely not
    // having explored the other faction's ground, which is what the observer bypass is for.
    settings.gameOptions.fogOfWar = true;
    settings.gameOptions.startWithExploredMap = false;
    // On deliberately: the point is that watching must not make the anchored AI immortal.
    settings.gameOptions.immortalHumanPlayer = true;

    const char* mapPath = std::getenv("SPECTATOR_MAP");
    if(!mapPath) throw std::runtime_error("SPECTATOR_MAP is required");
    const std::string contents = readCompleteFile(mapPath);
    const Uint32 seed = static_cast<Uint32>(std::strtoul(
        std::getenv("SPECTATOR_SEED") ? std::getenv("SPECTATOR_SEED") : "20260817", nullptr, 10));
    const bool ordinary = std::getenv("SPECTATOR_ORDINARY_ROUND_TRIP") != nullptr;

    GameInitSettings init(mapPath, contents, false, settings.gameOptions);
    init.randomSeed = seed; // Test-only access, provided by -fno-access-control.
    // Two real factions, both AI when observing, so no faction or start position is consumed.
    const HOUSETYPE firstHouse = HOUSE_ATREIDES;
    const HOUSETYPE secondHouse = HOUSE_HARKONNEN;
    {
        GameInitSettings::HouseInfo one(firstHouse, 1);
        one.addPlayerInfo({ordinary ? settings.general.playerName
                                    : getHouseNameByNumber(firstHouse),
                           ordinary ? HUMANPLAYERCLASS : DEFAULTAIPLAYERCLASS});
        init.addHouseInfo(one);
        GameInitSettings::HouseInfo two(secondHouse, 2);
        two.addPlayerInfo({getHouseNameByNumber(secondHouse), DEFAULTAIPLAYERCLASS});
        init.addHouseInfo(two);
    }
    if(!ordinary) init.setLocalSpectator(true);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    // Suppress the interactive briefing at setup, exactly as the balance probe does, then
    // restore ordinary AI and command execution before advancing a single cycle.
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;

    const std::string savePath = out + (ordinary ? "/ordinary-resume.dls" : "/observer-resume.dls");
    if(ordinary) runOrdinaryPlayerRoundTrip(game, savePath);
    else runSpectatorProbe(game, savePath);

    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    return MENU_QUIT_DEFAULT;
}
