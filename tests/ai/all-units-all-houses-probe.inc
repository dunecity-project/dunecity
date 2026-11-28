// Private real-engine acceptance fixture. Initial assets/funds are scenario setup;
// every asserted delivery is paid and deployed by ordinary engine cycles. Human
// orders come from BuilderList clicks; bot orders come only from full build().
#include <Menu/OptionsMenu.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/ScrollView.h>
#include <GUI/dune/BuilderList.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <Game.h>
#include <GameInterface.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <ObjectData.h>
#include <dunecity/AllUnitsRules.h>
#include <dunecity/CityConstants.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <players/HumanPlayer.h>
#include <players/QuantBotStrategy.h>
#include <players/QuantBotConfig.h>
#include <structures/BuilderBase.h>
#include <structures/Palace.h>
#include <structures/StarPort.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace AllUnitsAllHousesProbe {
constexpr auto& units = DuneCity::AllUnitsRules::kUnits;
constexpr std::array<int, 5> producers{Structure_Barracks, Structure_WOR,
    Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory};

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("ALL_UNITS_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

template<class Control> void click(Control& control) {
    require(control.isVisible() && control.isEnabled(), "The player's control is hidden or disabled");
    control.handleMouseLeft(4, control.getSize().y / 2, true);
    control.handleMouseLeft(4, control.getSize().y / 2, false);
}

GameOptionsWindow* openRules(OptionsMenu& menu) {
    click(menu.pageButtons[4]); // Options -> Advanced -> Default Game Rules -> Change...
    click(menu.gameOptionsButton);
    auto* rules = dynamic_cast<GameOptionsWindow*>(menu.pChildWindow);
    require(rules != nullptr, "Default Game Rules did not open its real dialog");
    menu.draw();
    return rules;
}

void requireBounds(Window& window, Widget& control) {
    const auto size = control.getSize();
    Point at(0, 0);
    for(Widget* current = &control; current != &window;) {
        auto* parent = current->getParent();
        require(parent != nullptr, "All Units for All Houses is detached from the rules dialog");
        if(auto* box = dynamic_cast<HBox*>(parent)) at = at + box->getWidgetPosition(current);
        else if(auto* box = dynamic_cast<VBox*>(parent)) at = at + box->getWidgetPosition(current);
        else if(auto* view = dynamic_cast<ScrollView*>(parent)) {
            while(at.y < view->scrollbar.getCurrentValue()
                  || at.y + size.y > view->scrollbar.getCurrentValue() + view->getSize().y) {
                const int before = view->scrollbar.getCurrentValue();
                require(view->handleMouseWheel(1, 1, at.y < before)
                    && view->scrollbar.getCurrentValue() != before,
                    "The real rules viewport cannot scroll the option into view");
            }
            at = at + Point(0, -view->scrollbar.getCurrentValue());
            require(at.x >= 0 && at.y >= 0 && at.x + size.x <= view->getContentWidth()
                && at.y + size.y <= view->getSize().y,
                "All Units for All Houses is clipped by the real rules viewport");
        }
        else require(parent == &window, "Unexpected rules dialog container");
        current = parent;
    }
    at = at + window.getPosition();
    require(size.x > 0 && size.y > 0 && at.x >= 0 && at.y >= 0
        && at.x + size.x <= getRendererWidth() && at.y + size.y <= getRendererHeight(),
        "All Units for All Houses is clipped at the normal 640x480 viewport");
}

SettingsClass::GameOptionsClass choose(bool on) {
    OptionsMenu menu;
    auto* rules = openRules(menu);
    auto& checkbox = rules->allUnitsForAllHousesCheckbox;
    requireBounds(*rules, checkbox);
    require(checkbox.getText() == _("All Units for All Houses") && !checkbox.getTooltipText().empty(),
        "The real all-units checkbox has no correct label/explanation");
    if(checkbox.isChecked() != on) click(checkbox);
    require(checkbox.isChecked() == on, "The real checkbox click lost the selected state");
    click(rules->okbutton);
    menu.processChildWindowOpenCloses();
    require(menu.currentGameOptions.allUnitsForAllHouses == on, "Rules OK lost the all-units choice");
    click(menu.acceptButton);
    require(menu.isQuiting(), "Options APPLY did not accept the settings");
    const INIFile config(getConfigFilepath());
    const auto section = userGameOptionsSection();
    require(!section.empty() && config.hasKey(section, "All Units for All Houses")
        && config.getBoolValue(section, "All Units for All Houses", !on) == on,
        "The player's all-units choice was not saved in the active mod's INI section");
    auto restored = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(restored.allUnitsForAllHouses == on, "INI reload lost the all-units choice");
    effectiveGameOptions = restored;
    OptionsMenu reopened;
    auto* reopenedRules = openRules(reopened);
    require(reopenedRules->allUnitsForAllHousesCheckbox.isChecked() == on,
        "Reopening Options lost the saved all-units checkbox state");
    click(reopenedRules->okbutton);
    reopened.processChildWindowOpenCloses();
    click(reopened.backButton);
    SDL_Log("ALL_UNITS_MENU: option=%d click=1 ini=1 reopened=1 pass=1", int(on));
    return restored;
}

bool ordinary(int item) { return std::find(units.begin(), units.end(), item) != units.end(); }

bool oldProducer(int house, int producer) {
    return !(house == HOUSE_HARKONNEN && producer == Structure_Barracks)
        && !(house == HOUSE_ATREIDES && producer == Structure_WOR);
}

// Independent, exact shipped OFF permissions, including the pre-existing city's
// Harkonnen Trike/Ornithopter exceptions. A locked producer is not supplied by the fixture.
bool oldProduct(int house, int item, bool city = true) {
    if(!oldProducer(house, DuneCity::AllUnitsRules::builderFor(item))) return false;
    switch(item) {
        case Unit_Devastator: return house != HOUSE_ATREIDES && house != HOUSE_ORDOS;
        case Unit_SonicTank: return house != HOUSE_HARKONNEN && house != HOUSE_ORDOS;
        case Unit_Deviator: return house == HOUSE_ORDOS || house >= HOUSE_NEUTRAL;
        case Unit_RaiderTrike: return house != HOUSE_HARKONNEN && house != HOUSE_ATREIDES;
        case Unit_Trike: return house != HOUSE_ORDOS && (city || house != HOUSE_HARKONNEN);
        case Unit_Ornithopter: return city || house != HOUSE_HARKONNEN;
        default: return true;
    }
}

int oldProductCount(int house, bool city = true) {
    return static_cast<int>(std::count_if(units.begin(), units.end(), [=](int item) { return oldProduct(house, item, city); }));
}

enum class Market { Authored, Absent, SoldOut };

const std::string& bundledMap() {
    static const std::string text = [] {
        std::ifstream file(ALL_UNITS_MAP_PATH);
        require(file.is_open(), "The real bundled Habbanya-Autumn map is missing");
        return std::string(std::istreambuf_iterator<char>(file), {});
    }();
    return text;
}

std::string mapFor(Market market) {
    std::string text = bundledMap();
    if(market == Market::Absent) {
        const auto begin = text.find("[CHOAM]");
        const auto end = text.find("[Player1]", begin);
        require(begin != std::string::npos && end != std::string::npos, "The bundled map's CHOAM fixture changed");
        text.erase(begin, end - begin);
    } else if(market == Market::SoldOut) {
        const auto at = text.find("Trike=5");
        require(at != std::string::npos, "The bundled map no longer authors five Trikes");
        text.replace(at, sizeof("Trike=5") - 1, "Trike=0");
    }
    return text;
}

int authoredStock(int item, Market market) {
    if(market == Market::Absent) return INVALID;
    switch(item) {
        case Unit_Carryall: case Unit_MCV: return 2;
        case Unit_Harvester: return 4;
        case Unit_Launcher: case Unit_Ornithopter: case Unit_Quad: return 5;
        case Unit_Trike: return market == Market::SoldOut ? 0 : 5;
        case Unit_SiegeTank: case Unit_Tank: return 6;
        default: return INVALID;
    }
}

struct World {
    std::unique_ptr<Game> game;
    House* own = nullptr;
    QuantBot* bot = nullptr;
    std::vector<BuilderBase*> factories;
    BuilderBase* yard = nullptr;
    StarPort* port = nullptr;
    Palace* palace = nullptr;
    int house;
    bool city, option;

    World(SettingsClass::GameOptionsClass rules, int side, bool botOwner = false, Market market = Market::Authored)
        : house(side), city(ModManager::instance().isDuneCityContentActive()), option(rules.allUnitsForAllHouses) {
        require(currentGame == nullptr, "Acceptance worlds share live engine globals");
        rules.fogOfWar = false;
        rules.startWithExploredMap = true;
        rules.concreteRequired = false;
        rules.instantBuild = false;
        rules.researchLab = false;
        rules.starportPopulation = false; // Isolate unit technology, not the separate city-population rule.
        rules.maximumNumberOfUnitsOverride = 0;
        rules.maximumNumberOfHarvestersOverride = 0;
        GameInitSettings init("2P - 51x31 - 1v1 - Habbanya-Autumn.ini", mapFor(market), true, rules);
        init.randomSeed = 20261128;
        GameInitSettings::HouseInfo owner(static_cast<HOUSETYPE>(side), 1);
        owner.addPlayerInfo({botOwner ? "All Units QuantBot" : settings.general.playerName,
            botOwner ? "qBotBrutal" : HUMANPLAYERCLASS});
        init.addHouseInfo(owner);
        const int opposite = side == HOUSE_HARKONNEN ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
        GameInitSettings::HouseInfo enemy(static_cast<HOUSETYPE>(opposite), 2);
        enemy.addPlayerInfo({botOwner ? settings.general.playerName : "Quiet QuantBot",
            botOwner ? HUMANPLAYERCLASS : "qBotBrutal"});
        init.addHouseInfo(enemy);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true; // A private assertion fixture, never an analytics match.
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        own = game->getHouse(side);
        require(own && game->getHouse(opposite) && own->getTechLevel() == 8
            && game->isCitySimEnabled() == city, "The bundled-map match did not initialize the requested house/mode/tech");
        require(game->getGameInitSettings().getGameOptions().allUnitsForAllHouses == option,
            "Match initialization lost the checkbox choice");
        for(const auto& player : own->getPlayerList())
            if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for(const auto& player : game->getHouse(opposite)->getPlayerList())
            if(auto* quiet = dynamic_cast<QuantBot*>(player.get())) quiet->setRole(AIRole::Off);
        require(botOwner ? bot != nullptr : pLocalHouse == own, "The fixture has the wrong real player controller");
        if(bot) bot->setRole(AIRole::Off);
        own->startingCredits = 100000;
        own->enforceCreditCeiling();
    }

    ~World() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    StructureBase* placeAt(int item, Coord at) {
        const Coord size = getStructureSize(item);
        for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx)
            currentGameMap->getTile(at.x + dx, at.y + dy)->setType(Terrain_Rock);
        auto* structure = own->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(structure != nullptr, "Cannot place a real initial production/prerequisite structure");
        structure->setVisible(VIS_ALL, true);
        return structure;
    }

    StructureBase* place(int item) {
        const Coord size = getStructureSize(item);
        for(int y = 2; y + size.y + 1 < currentGameMap->getSizeY(); ++y)
            for(int x = 2; x + size.x + 1 < currentGameMap->getSizeX(); ++x) {
                bool clear = true;
                for(int dy = -1; dy <= size.y; ++dy) for(int dx = -1; dx <= size.x; ++dx)
                    clear &= !currentGameMap->getTile(x + dx, y + dy)->hasAGroundObject();
                if(clear) return placeAt(item, {x, y});
            }
        throw std::runtime_error("The bundled map has no clear fixture production space");
    }

    void suppliers(bool ai = false) {
        for(int i = 0; i < 14; ++i) place(Structure_WindTrap);
        if(!ai) yard = dynamic_cast<BuilderBase*>(place(Structure_ConstructionYard));
        for(int item : {Structure_Radar, Structure_Refinery, Structure_RepairYard, Structure_IX})
            if(!ai || item != Structure_Refinery) place(item);
        for(int producer : producers) {
            if(!(city && option) && !oldProducer(house, producer)) continue;
            const int copies = ai && producer == Structure_HeavyFactory ? 6 : 1;
            for(int n = 0; n < copies; ++n) {
                auto* factory = dynamic_cast<BuilderBase*>(place(producer));
                require(factory != nullptr, "A real factory is not a BuilderBase");
                factory->curUpgradeLev = factory->getMaxUpgradeLevel();
                factories.push_back(factory);
            }
        }
        if(!ai) {
            port = dynamic_cast<StarPort*>(place(Structure_StarPort));
            palace = dynamic_cast<Palace*>(place(Structure_Palace));
            require(yard && port && palace, "The real yard/starport/Palace fixture is incomplete");
            yard->curUpgradeLev = yard->getMaxUpgradeLevel();
        }
        own->updateBuildLists();
    }

    BuilderBase* producerFor(int item) const {
        const int type = DuneCity::AllUnitsRules::builderFor(item);
        for(auto* factory : factories) if(factory->getItemID() == static_cast<Uint32>(type)) return factory;
        return nullptr;
    }

    void tick() {
        game->negotiatedBudget = 15000;
        game->carryOverTokens = 0;
        game->updateGameState();
        require(!game->finished, "The passive production fixture ended before its bounded assertions");
    }
};

bool sameBlueprint(const ObjectData::ObjectDataStruct& a, const ObjectData::ObjectDataStruct& b) {
    return a.enabled == b.enabled && a.hitpoints == b.hitpoints && a.price == b.price
        && a.power == b.power && a.viewrange == b.viewrange && a.capacity == b.capacity
        && a.weapondamage == b.weapondamage && a.weaponrange == b.weaponrange
        && a.weaponreloadtime == b.weaponreloadtime && a.maxspeed == b.maxspeed && a.turnspeed == b.turnspeed
        && a.buildtime == b.buildtime && a.infspawnprop == b.infspawnprop && a.builder == b.builder
        && a.prerequisiteStructuresSet == b.prerequisiteStructuresSet && a.techLevel == b.techLevel
        && a.upgradeLevel == b.upgradeLevel;
}

void requireOverlay(const World& world, const ObjectData& baseline) {
    const bool applied = world.city && world.option;
    for(int item = 0; item < Num_ItemID; ++item) for(int side = 0; side < NUM_HOUSES; ++side) {
        auto expected = baseline.data[item][side];
        if(applied && (ordinary(item) || item == Structure_Barracks || item == Structure_WOR)) {
            expected.enabled = true;
            expected.builder = ordinary(item) ? DuneCity::AllUnitsRules::builderFor(item) : Structure_ConstructionYard;
        }
        require(sameBlueprint(world.game->objectData.data[item][side], expected),
            "The option changed more than normal product/producer permission: item=" + std::to_string(item)
                + " house=" + std::to_string(side));
    }
}

int requireStock(World& world, Market market) {
    int stocked = 0;
    for(int item : units) {
        int expected = authoredStock(item, market);
        if(world.city && world.option && expected == INVALID) expected = 1;
        require(world.own->getChoam().getNumAvailable(item) == expected,
            "The rule lost an authored CHOAM quantity or failed to add a missing normal product");
        stocked += expected != INVALID;
    }
    require(world.own->getChoam().getNumAvailable(Unit_Saboteur) == INVALID
        && world.own->getChoam().getNumAvailable(Unit_Sandworm) == INVALID,
        "Palace-only products leaked into the ordinary starport market");
    return stocked;
}

void requireCatalogue(World& world) {
    const bool all = world.city && world.option;
    int offered = 0, imported = 0;
    for(int item : units) {
        const bool expected = all || oldProduct(world.house, item, world.city);
        auto* producer = world.producerFor(item);
        const bool actual = producer && producer->isAvailableToBuild(item);
        require(actual == expected, "Wrong real factory permission: house=" + std::to_string(world.house)
            + " item=" + std::to_string(item));
        for(auto* factory : world.factories)
            require(factory->isAvailableToBuild(item) == (expected
                && factory->getItemID() == static_cast<Uint32>(DuneCity::AllUnitsRules::builderFor(item))),
                "A normal unit is offered by the wrong factory");
        const bool stock = authoredStock(item, Market::Authored) != INVALID;
        require(world.port->isAvailableToBuild(item) == (all || stock), "The starport does not offer its real catalogue");
        offered += actual;
        imported += world.port->isAvailableToBuild(item);
        SDL_Log("ALL_UNITS_AVAIL: house=%d city=%d option=%d item=%d factory=%d starport=%d stock=%d",
            world.house, int(world.city), int(world.option), item, int(actual),
            int(world.port->isAvailableToBuild(item)), world.own->getChoam().getNumAvailable(item));
    }
    for(auto* factory : world.factories) for(const auto& offer : factory->getBuildList())
        require(ordinary(offer.itemID), "An extra non-roster unit leaked into a normal city factory");
    for(int producer : {Structure_Barracks, Structure_WOR})
        require(world.yard->isAvailableToBuild(producer) == (all || oldProducer(world.house, producer)),
            "The real Construction Yard cannot construct the intended infantry producer");
    const int fallback = getHouseFallbackHouse(static_cast<HOUSETYPE>(world.house));
    const bool missile = fallback == HOUSE_HARKONNEN || fallback == HOUSE_SARDAUKAR;
    // Custom uses its fallback weapon but keeps its own five-minute cooldown.
    // Palace::getSpecialWeaponCooldownForHouse keys the timer by technology
    // identity, not by the weapon's fallback house.
    const bool longCooldown = world.house == HOUSE_HARKONNEN || world.house == HOUSE_SARDAUKAR;
    require(world.palace->usesTargetedSpecialWeapon() == missile
        && world.palace->getMaxSpecialWeaponTimer() == MILLI2CYCLES((longCooldown ? 10 : 5) * 60000)
        && world.palace->usesPalaceWorm() == (fallback == HOUSE_FREMEN
            && world.game->getGameInitSettings().getGameOptions().fremenPalaceWorm),
        "The all-units option changed the Palace's house-specific weapon/cooldown");
    for(auto* factory : world.factories)
        require(!factory->isAvailableToBuild(Unit_Saboteur) && !factory->isAvailableToBuild(Unit_Sandworm),
            "A Palace summon became a normal factory product");
    require(offered == (all ? 17 : oldProductCount(world.house, world.city)) && imported == (all ? 17 : 9),
        "The exact ON/OFF catalogue totals changed");
    SDL_Log("ALL_UNITS_CATALOG: house=%d city=%d option=%d factories=%d starport=%d palace=1 pass=1",
        world.house, int(world.city), int(world.option), offered, imported);
}

void requireNegativeGates(World& world) {
    int tech = 0, upgrades = 0, prerequisites = 0;
    std::set<int> requiredStructures;
    for(int item : units) {
        auto* producer = world.producerFor(item);
        require(producer && producer->isAvailableToBuild(item), "The negative-gate control is not fully unlocked");
        const auto& spec = world.game->objectData.data[item][world.house];
        if(spec.techLevel > 1) {
            world.game->techLevel = spec.techLevel - 1;
            producer->updateBuildList();
            world.port->updateBuildList();
            require(!producer->isAvailableToBuild(item) && !world.port->isAvailableToBuild(item),
                "All-units bypasses a normal factory/starport technology gate");
            ++tech;
            world.game->techLevel = 8;
            producer->updateBuildList();
            world.port->updateBuildList();
        }
        if(spec.upgradeLevel > 0) {
            const int level = producer->getCurrentUpgradeLevel();
            producer->curUpgradeLev = spec.upgradeLevel - 1;
            producer->updateBuildList();
            require(!producer->isAvailableToBuild(item) && world.port->isAvailableToBuild(item),
                "All-units bypasses a factory upgrade or invents a starport factory-upgrade requirement");
            ++upgrades;
            producer->curUpgradeLev = level;
            producer->updateBuildList();
        }
        for(size_t prerequisite = 0; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
            if(spec.prerequisiteStructuresSet[prerequisite]) requiredStructures.insert(static_cast<int>(prerequisite));
    }
    for(int prerequisite : requiredStructures) {
        StructureBase* removed = nullptr;
        for(auto* structure : structureList)
            if(structure->getOwner() == world.own && structure->getItemID() == static_cast<Uint32>(prerequisite)) removed = structure;
        require(removed && world.own->getNumItems(prerequisite) == 1, "A gate needs one real removable prerequisite");
        const Coord at = removed->getLocation();
        removed->destroy();
        require(world.own->getNumItems(prerequisite) == 0, "Destroying the real prerequisite did not remove its authority");
        world.own->updateBuildLists();
        for(int item : units) if(world.game->objectData.data[item][world.house].prerequisiteStructuresSet[prerequisite]) {
            require(!world.producerFor(item)->isAvailableToBuild(item) && !world.port->isAvailableToBuild(item),
                "All-units bypasses a destroyed normal factory/starport prerequisite");
            ++prerequisites;
        }
        world.placeAt(prerequisite, at);
        world.own->updateBuildLists();
    }
    for(int item : units)
        require(world.producerFor(item)->isAvailableToBuild(item) && world.port->isAvailableToBuild(item),
            "Restoring ordinary tech/upgrades/prerequisites failed to restore availability");
    require(tech > 0 && upgrades > 0 && prerequisites > 0, "The negative-gate experiment had no real activations");
    SDL_Log("ALL_UNITS_GATES: house=%d tech=%d upgrades=%d prerequisites=%d pass=1",
        world.house, tech, upgrades, prerequisites);
}

BuilderList* openList(World& world, BuilderBase& builder) {
    auto& game = *world.game;
    require(pLocalHouse == world.own && game.canControlHouse(world.own), "The production click has no real human authority");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(builder.getObjectID());
    builder.setSelected(true);
    game.selectionChanged();
    game.setCursorMode(Game::CursorMode_Normal);
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<BuilderInterface*>(game.pInterface->pObjectContainer);
    require(panel && panel->pBuilderList && panel->update(), "The selected factory has no real production sidebar");
    require(panel->pBuilderList->isVisible() && panel->pBuilderList->isEnabled(), "The real production list is disabled");
    return panel->pBuilderList;
}

int deployedItem(int order) { return order == Unit_Infantry ? Unit_Soldier : order == Unit_Troopers ? Unit_Trooper : order; }
int deployedCount(int order) { return order == Unit_Infantry || order == Unit_Troopers ? 3 : 1; }

void clickProduct(World& world, BuilderBase* producer, int item) {
    auto* list = openList(world, *producer);
    int index = 0;
    for(const auto& offer : producer->getBuildList()) { if(offer.itemID == static_cast<Uint32>(item)) break; ++index; }
    require(index < producer->getBuildListSize(), "The player's unit has no real sidebar button");
    const int slots = list->getNumButtons(list->getSize().y);
    while(index < list->currentListPos) {
        const int before = list->currentListPos;
        require(list->handleMouseWheel(1, 1, true) && list->currentListPos < before,
            "The player's real production sidebar cannot scroll back to the unit");
    }
    while(index >= list->currentListPos + slots) {
        const int before = list->currentListPos;
        require(list->handleMouseWheel(1, 1, false) && list->currentListPos > before,
            "The player's real production sidebar cannot scroll to the unit");
    }
    require(list->getItemIDFromIndex(index) == item, "The visible production button names the wrong unit");
    const Point at = list->getButtonPosition(index - list->currentListPos) + Point(10, 10);
    SDL_SetModState(KMOD_NONE);
    require(list->handleMouseLeft(at.x, at.y, true) && list->handleMouseLeft(at.x, at.y, false),
        "The real sidebar did not accept the production press/release");
}

void buyNewProduct(World& world, int item) {
    auto* producer = world.producerFor(item);
    require(producer != nullptr, "The new-to-house product has no real producer");
    const auto& blueprint = world.game->objectData.data[item][world.house];
    require(producer->getBuildItem(item)->price == blueprint.price,
        "The real factory offer changed the unit's normal price");
    std::set<Uint32> before;
    for(auto* unit : unitList) if(unit->getOwner() == world.own) before.insert(unit->getObjectID());
    const int actual = deployedItem(item), expected = deployedCount(item);
    const int builtBefore = world.own->getNumBuiltItems(actual);
    clickProduct(world, producer, item);
    FixPoint paid = 0;
    int delivered = 0;
    const Uint32 stop = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
    do {
        world.tick();
        if(producer->getCurrentProducedItem() == static_cast<Uint32>(item)) paid = std::max(paid, producer->getProductionProgress());
        delivered = 0;
        for(auto* unit : unitList)
            if(unit->getOwner() == world.own && unit->getItemID() == static_cast<Uint32>(actual)
                && !before.count(unit->getObjectID()) && unit->isActive() && unit->getHealth() > 0
                && unit->getLocation().isValid()) ++delivered;
    } while((delivered < expected || producer->getProductionQueueSize() != 0) && world.game->getGameCycleCount() < stop);
    require(delivered == expected && producer->getProductionQueueSize() == 0
        && world.own->getNumBuiltItems(actual) - builtBefore == expected,
        "A real player's paid new-to-house production click did not deploy its complete unit/squad");
    // A completed order clears its progress in the same game cycle as deployment,
    // so the last observable sample may be one production payment short. Assert
    // its exact normal offer price and the unrounded final-payment boundary;
    // rounding the previous sample loses a credit for some legitimate tick sizes.
    const int buildTime = DuneCity::getCityBuildTime(item, blueprint.buildtime);
    const FixPoint normalPayment = FixPoint(blueprint.price) / (buildTime * 15);
    require(paid > 0 && paid + normalPayment >= blueprint.price
        && paid <= FixPoint(blueprint.price) + normalPayment,
        "The newly allowed unit did not reach its unchanged normal factory price: progress="
            + paid.toString() + " price=" + std::to_string(blueprint.price)
            + " tick=" + normalPayment.toString());
    SDL_Log("ALL_UNITS_PLAYER: house=%d option=1 item=%d actual=%d completed=%d cycles=%u pass=1",
        world.house, item, actual, delivered, world.game->getGameCycleCount());
}

void buyNewImports(SettingsClass::GameOptionsClass rules) {
    World world(rules, HOUSE_HARKONNEN);
    world.suppliers();
    world.tick(); // The ordinary first CHOAM price update precedes player input.
    std::set<Uint32> seen;
    for(auto* unit : unitList) if(unit->getOwner() == world.own) seen.insert(unit->getObjectID());
    std::array<int, Num_ItemID> expected{}, delivered{}, completed{}, builtBefore{};
    for(int item : units) builtBefore[item] = world.own->getNumBuiltItems(item);
    int orders = 0, spent = 0;
    for(int item : units) {
        if(authoredStock(item, Market::Authored) != INVALID) continue;
        const int price = world.port->getBuildItem(item)->price;
        const int cash = world.own->getCredits();
        const int stock = world.own->getChoam().getNumAvailable(item);
        require(price > 0 && price == world.own->getChoam().getPrice(item) && stock > 0,
            "The expanded Starport product lacks a real market quote or stock");
        clickProduct(world, world.port, item);
        world.tick();
        require(cash - world.own->getCredits() == price
            && world.own->getChoam().getNumAvailable(item) == stock - 1,
            "The real Starport click did not charge its quoted price and consume stock");
        expected[deployedItem(item)] += deployedCount(item);
        ++orders;
        spent += price;
    }
    require(orders == 8 && world.port->getProductionQueueSize() == orders,
        "The expanded Starport did not accept all eight previously absent products");
    auto* list = openList(world, *world.port);
    require(list->orderButton.getText() == _("Order"), "The real Starport shipment control is missing");
    click(list->orderButton);
    world.tick();
    require(!world.port->okToOrder(), "The Order button did not dispatch the player's paid shipment");
    const Uint32 stop = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
    // Unit simulation runs in the same cycle as cargo release. Require live
    // witnesses for every type and every individual completion via the counter
    // that StarPort increments only after a successful real deploy().
    do {
        world.tick();
        for(auto* unit : unitList)
            if(unit->getOwner() == world.own
                && unit->isActive() && unit->getHealth() > 0 && unit->getLocation().isValid()
                && expected[unit->getItemID()] > 0 && seen.insert(unit->getObjectID()).second)
                ++delivered[unit->getItemID()];
        for(int item : units) if(expected[item] > 0)
            completed[item] = world.own->getNumBuiltItems(item) - builtBefore[item];
    } while((completed != expected || world.port->getProductionQueueSize() != 0)
            && world.game->getGameCycleCount() < stop);
    for(int item : units) {
        if(expected[item] == 0) continue;
        require(completed[item] == expected[item] && delivered[item] > 0,
            "The paid Starport cargo lacks its complete deployment counter or a live unit-type witness");
        SDL_Log("ALL_UNITS_IMPORT_OBSERVED: item=%d expected=%d deployed=%d witnessed=%d queue=%zu arrival=%d deploying=%d timer=%d",
            item, expected[item], completed[item], delivered[item], world.port->currentProductionQueue.size(),
            world.port->arrivalTimer, world.port->deploying, world.port->deployTimer);
    }
    for(auto* unit : unitList) {
        if(unit->getOwner() != world.own || unit->getItemID() != Unit_Frigate) continue;
        SDL_Log("ALL_UNITS_SHIP_OBSERVED: item=%u active=%d location=%d,%d health=%d",
            unit->getItemID(), unit->isActive(), unit->getLocation().x, unit->getLocation().y,
            unit->getHealth().lround());
    }
    require(completed == expected && world.port->getProductionQueueSize() == 0,
        "The real Starport shipment did not deploy every paid new product and complete infantry squad");
    int total = 0;
    for(int count : completed) total += count;
    require(total == 12, "The eight expanded Starport orders did not deploy twelve individual units");
    SDL_Log("ALL_UNITS_IMPORT: orders=%d deployed=%d spent=%d pass=1", orders, total, spent);
}

void requireStartingPrior(QuantBot& bot, bool sharedArsenal) {
    // Frozen pre-option bounded starting-prior recipe. OFF and vanilla must
    // match it exactly, not merely keep the same configuration hash.
    require(bot.style().startBounds, "The normal AI prior fixture lost its default bounds");
    const auto slots = bot.fieldableMixSlots();
    const auto& ratios = bot.botConfig().getRatios(bot.getHouse()->getHouseID());
    const UnitMixPolicy::GroupWeights group{
        static_cast<int>(ratios.tank * 10000), static_cast<int>(ratios.siegeTank * 10000),
        static_cast<int>(ratios.launcher * 10000), static_cast<int>(ratios.special * 10000),
        static_cast<int>(ratios.ornithopter * 10000), 0, 0, 0};
    const auto opening = UnitMixPolicy::openingMix(bot.getHouse()->getTechLevel(),
        UnitMixPolicy::expandGroups(group, slots), slots);
    UnitMixPolicy::Weights legacy{};
    for(size_t i = 0; i < legacy.size(); ++i) legacy[i] = opening[i];
    if(const auto* strategy = bot.strategyMix())
        legacy = QuantBotStrategy::applyPrior(legacy,
            strategy->counter ? bot.enemyCounterPrior() : bot.strategyStart, strategy->priorBps, slots);
    const auto bounded = QuantBotStrategy::bound(legacy, slots);
    UnitMixPolicy::Weights expected{};
    for(size_t i = 0; i < expected.size(); ++i) expected[i] = bounded[i];
    const auto actual = bot.startingPrior(slots);
    int64_t oldSpecial = 0, newSpecial = 0;
    for(size_t s = 0; s < UnitMixPolicy::kSpecialSlots; ++s) {
        const auto slot = UnitMixPolicy::kFirstSpecialSlot + s;
        oldSpecial += expected[slot];
        newSpecial += actual[slot];
    }
    require(!sharedArsenal ? actual == expected : oldSpecial == newSpecial,
        "OFF/vanilla changed the legacy AI prior or ON changed its total special budget");
    if(sharedArsenal) {
        const auto first = UnitMixPolicy::kFirstSpecialSlot;
        for(size_t s = 0; s < UnitMixPolicy::kSpecialSlots; ++s)
            require(actual[first + s] > 0 && actual[first + s] >= actual[first] - 1
                && actual[first + s] <= actual[first] + 1,
                "The shared arsenal still reserves a special's prior for its old native house");
    }
    SDL_Log("ALL_UNITS_PRIOR: house=%d city_shared=%d legacy_equal=%d special_before=%lld special_after=%lld pass=1",
        bot.getHouse()->getHouseID(), int(sharedArsenal), int(actual == expected),
        static_cast<long long>(oldSpecial), static_cast<long long>(newSpecial));
}

void runBot(SettingsClass::GameOptionsClass rules, int house) {
    World world(rules, house, true);
    world.suppliers(true);
    require(world.bot && world.own->getNumItems(Structure_ConstructionYard) == 0,
        "The isolated factory experiment inherited a construction yard");
    // Exactly the normal ratios/configuration: no forced unit choice, style,
    // reward, artificial queue or doProduce call. Full build()/pickStrategy()
    // run as Both at the normal 50-cycle cadence. Only the intervening simulation
    // updates suspend the unrelated unit/MCV controller, so no yard, import,
    // refinery grant or Palace can masquerade as completed factory production.
    world.bot->initialMilitaryValue = world.bot->militaryUnitValue();
    world.bot->militaryValueLimit = 30000;
    world.bot->harvesterLimit = 8;
    for(int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
        world.bot->initialItemCount[item] = world.own->getNumItems(item);
    world.bot->role = AIRole::Both;
    world.bot->pickStrategy();
    requireStartingPrior(*world.bot, world.city && world.option);
    world.bot->role = AIRole::Off;
    const auto configHash = world.bot->botConfig().getConfigHash();
    std::set<Uint32> seen;
    for(auto* unit : unitList) if(unit->getOwner() == world.own) seen.insert(unit->getObjectID());
    std::array<int, Num_ItemID> builtBefore{};
    for(int item : units) builtBefore[item] = world.own->getNumBuiltItems(item);
    int delivered = 0, newToHouse = 0, turns = 0;
    const Uint32 first = world.game->getGameCycleCount();
    const Uint32 stop = first + MILLI2CYCLES(360000);
    while(world.game->getGameCycleCount() < stop) {
        if((world.game->getGameCycleCount() - first) % 50 == 0) {
            world.bot->role = AIRole::Both;
            world.bot->build(world.bot->militaryUnitValue());
            world.bot->role = AIRole::Off;
            ++turns;
        }
        world.tick();
        require(world.own->getNumItems(Structure_ConstructionYard) == 0
            && world.own->getNumItems(Structure_StarPort) == 0 && world.own->getNumItems(Structure_Palace) == 0
            && world.own->getNumItems(Structure_Refinery) == 0,
            "A non-factory source contaminated the actual factory-output count");
        for(auto* unit : unitList) {
            if(unit->getOwner() != world.own || seen.count(unit->getObjectID()) || !unit->isActive()
                || unit->getHealth() <= 0 || unit->getLocation().isInvalid()) continue;
            seen.insert(unit->getObjectID());
            const int item = unit->getItemID();
            require(ordinary(item) && world.own->getNumBuiltItems(item) > builtBefore[item],
                "A counted bot activation was not a completed, deployed ordinary factory product");
            const bool isNew = !oldProduct(house, item);
            ++delivered;
            newToHouse += isNew;
            SDL_Log("ALL_UNITS_BOT_UNIT: house=%d option=%d item=%d object=%u new=%d source=factory completed=1",
                house, int(rules.allUnitsForAllHouses), item, unit->getObjectID(), int(isNew));
        }
    }
    require(world.bot->allocationSnapshot.ready && turns > 0 && delivered > 0,
        "The real QuantBot mix/producer experiment had no completed factory production");
    SDL_Log("ALL_UNITS_BOT_OBSERVED: house=%d option=%d completed=%d new=%d turns=%d cycles=%u",
        house, int(rules.allUnitsForAllHouses), delivered, newToHouse, turns,
        world.game->getGameCycleCount() - first);
    require(rules.allUnitsForAllHouses ? newToHouse > 0 : newToHouse == 0,
        "Each baseline-restricted house must deploy a new-to-house unit ON and none of its denied factory products OFF");
    require(world.bot->botConfig().getConfigHash() == configHash, "The bot experiment altered its normal ratio/configuration");
    SDL_Log("ALL_UNITS_BOT: house=%d option=%d applicable=1 completed=%d new=%d turns=%d cycles=%u config=%s pass=1",
        house, int(rules.allUnitsForAllHouses), delivered, newToHouse, turns,
        world.game->getGameCycleCount() - first, configHash.c_str());
}
} // namespace AllUnitsAllHousesProbe

int runAllUnitsAllHousesProbe() {
    using namespace AllUnitsAllHousesProbe;
    auto& mods = ModManager::instance();
    settings.general.playerName = "All Units observer";
    require(!SettingsClass::GameOptionsClass{}.allUnitsForAllHouses, "The generic/vanilla default must remain OFF");
    require(mods.setActiveMod("dunecity"), "The built-in Dune City content is missing");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    const auto roster = mods.getPlayableHouses();
    require(roster.size() >= 6 && std::set<int>(roster.begin(), roster.end()).size() == roster.size(),
        "The real playable city roster is incomplete or duplicated");
    for(int house = HOUSE_HARKONNEN; house <= HOUSE_MERCENARY; ++house)
        require(std::find(roster.begin(), roster.end(), house) != roster.end(), "A usual six-house faction disappeared from the real roster");
    for(int house : roster) require(house >= 0 && house < NUM_PLAYER_HOUSES, "The city roster includes a non-player house");
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(rules->allUnitsForAllHousesCheckbox.isVisible()
            && rules->allUnitsForAllHousesCheckbox.isChecked(), "The built-in city option is not visibly ON by default");
        requireBounds(*rules, rules->allUnitsForAllHousesCheckbox);
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    SDL_Log("ALL_UNITS_DEFAULT: city_on=1 generic_off=1 roster=%zu pass=1", roster.size());
    const auto off = choose(false);
    const auto on = choose(true);
    std::unique_ptr<ObjectData> cityBaseline;
    for(bool enabled : {false, true}) for(int house : roster) {
        World world(enabled ? on : off, house);
        if(!cityBaseline) cityBaseline = std::make_unique<ObjectData>(world.game->objectData);
        requireOverlay(world, *cityBaseline);
        requireStock(world, Market::Authored);
        world.suppliers();
        requireCatalogue(world);
        if(enabled) {
            requireNegativeGates(world);
            for(int item : units) if(!oldProduct(house, item)) buyNewProduct(world, item);
        }
    }
    for(Market market : {Market::Absent, Market::SoldOut}) for(bool enabled : {false, true}) {
        World world(enabled ? on : off, HOUSE_HARKONNEN, false, market);
        const int stocked = requireStock(world, market);
        SDL_Log("ALL_UNITS_MARKET: case=%s option=%d stocked=%d trike=%d pass=1",
            market == Market::Absent ? "absent" : "sold_out", int(enabled), stocked,
            world.own->getChoam().getNumAvailable(Unit_Trike));
    }
    for(int house : roster) for(bool enabled : {false, true}) {
        if(oldProductCount(house) < 17) runBot(enabled ? on : off, house);
        else SDL_Log("ALL_UNITS_BOT: house=%d option=%d applicable=0 completed=0 new=0 turns=0 cycles=0 config=not_applicable pass=1",
            house, int(enabled)); // Already-all-units factions have no possible new-to-house activation.
    }
    buyNewImports(on);
    require(mods.setActiveMod("vanilla"), "Cannot inspect vanilla's real game rules");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    require(!effectiveGameOptions.allUnitsForAllHouses, "The city choice leaked into vanilla's effective default");
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(!rules->allUnitsForAllHousesCheckbox.isVisible()
            && rules->allUnitsForAllHousesCheckbox.getParent() == nullptr,
            "Vanilla exposes the city-only all-units checkbox");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    std::unique_ptr<ObjectData> vanillaBaseline;
    for(bool stored : {false, true}) {
        auto rules = effectiveGameOptions;
        rules.allUnitsForAllHouses = stored;
        World world(rules, HOUSE_HARKONNEN);
        if(!vanillaBaseline) vanillaBaseline = std::make_unique<ObjectData>(world.game->objectData);
        requireOverlay(world, *vanillaBaseline);
        requireStock(world, Market::Authored);
        world.suppliers();
        requireCatalogue(world);
    }
    for(bool stored : {false, true}) {
        auto rules = effectiveGameOptions;
        rules.allUnitsForAllHouses = stored;
        World world(rules, HOUSE_HARKONNEN, true);
        world.bot->role = AIRole::Both;
        world.bot->pickStrategy();
        requireStartingPrior(*world.bot, false);
        world.bot->role = AIRole::Off;
    }
    SDL_Log("ALL_UNITS_PASS: roster=%zu default_city_on=1 ini=1 real_sidebar=1 gates=1 palace_unchanged=1 vanilla_hidden_off=1", roster.size());
    return MENU_QUIT_DEFAULT;
}
