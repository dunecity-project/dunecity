// Exercise the player's city permission menu and real human/AI garrison and capture paths.
#include <Menu/OptionsMenu.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/ScrollView.h>
#include <Game.h>
#include <GameInterface.h>
#include <ScreenBorder.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <GUI/ObjectInterfaces/UnitInterface.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <players/QuantBot.h>
#include <players/HumanPlayer.h>
#include <players/QuantBotConfig.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace GarrisonCaptureOptionProbe {
bool configuredZoneTactics = false;
void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("GARRISON_CAPTURE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

template<class Control> void click(Control& control) {
    require(control.isVisible() && control.isEnabled(), "The player's control is hidden or disabled");
    control.handleMouseLeft(4, control.getSize().y / 2, true);
    control.handleMouseLeft(4, control.getSize().y / 2, false);
}

SDL_Rect ruleBounds(GameOptionsWindow& window, Widget& control) {
    Point at = window.getPosition();
    for(Widget* current = &control; current != &window;) {
        auto* parent = current->getParent();
        require(parent != nullptr, "Rule control is not in the real window");
        if(auto* box = dynamic_cast<HBox*>(parent)) at = at + box->getWidgetPosition(current);
        else if(auto* box = dynamic_cast<VBox*>(parent)) at = at + box->getWidgetPosition(current);
        else if(auto* scroll = dynamic_cast<ScrollView*>(parent))
            at = at + Point(0, -scroll->scrollbar.getCurrentValue());
        else require(parent == &window, "Unexpected rule control container");
        current = parent;
    }
    return {at.x, at.y, control.getSize().x, control.getSize().y};
}

void requireBounds(GameOptionsWindow& window, Widget& control) {
    const auto viewport = ruleBounds(window, window.rulesScroll);
    int wheelEvents = 0;
    for(int attempts = 0; attempts < 100; ++attempts) {
        const auto rect = ruleBounds(window, control);
        if(rect.y >= viewport.y && rect.y + rect.h <= viewport.y + viewport.h) break;
        require(window.handleMouseWheel(viewport.x + 8, viewport.y + viewport.h / 2, rect.y < viewport.y),
                "Game Rules did not route the wheel event needed to reveal its city option");
        ++wheelEvents;
    }
    const auto rect = ruleBounds(window, control);
    require(rect.w > 0 && rect.h > 0 && rect.x >= 0 && rect.y >= 0
            && rect.x + rect.w <= getRendererWidth() && rect.y + rect.h <= getRendererHeight(),
            "Garrisoning and Capturing is clipped or outside the viewport");
    require(rect.x >= viewport.x && rect.x + rect.w <= viewport.x + window.rulesScroll.getContentWidth()
            && rect.y >= viewport.y && rect.y + rect.h <= viewport.y + viewport.h,
            "The city option was not fully revealed inside the real scroll viewport");
    window.draw();
    SDL_Log("GARRISON_CAPTURE_RULE_CONTROL: wheel_events=%d x=%d y=%d width=%d height=%d",
            wheelEvents, rect.x, rect.y, rect.w, rect.h);
}

void click(GameOptionsWindow& window, Widget& control) {
    require(control.isVisible() && control.isEnabled(), "The player's rule control is hidden or disabled");
    const auto rect = ruleBounds(window, control);
    window.handleMouseLeft(rect.x + 4, rect.y + rect.h / 2, true);
    window.handleMouseLeft(rect.x + 4, rect.y + rect.h / 2, false);
}

GameOptionsWindow* openRules(OptionsMenu& menu) {
    click(menu.pageButtons[4]); // Advanced -> Default Game Rules -> Change...
    click(menu.gameOptionsButton);
    auto* rules = dynamic_cast<GameOptionsWindow*>(menu.pChildWindow);
    require(rules != nullptr, "The Options button did not open Game Rules");
    menu.draw();
    return rules;
}

SettingsClass::GameOptionsClass choose(bool on) {
    OptionsMenu menu;
    auto* rules = openRules(menu);
    auto& control = rules->garrisoningAndCapturingCheckbox;
    requireBounds(*rules, control);
    require(control.isVisible(), "The city option is missing from Game Rules");
    require(control.getText() == _("Garrisoning and Capturing"), "The city rule has the wrong label");
    const auto explanation = control.getTooltipText();
    require(explanation.find("Human") != std::string::npos && explanation.find("AI") != std::string::npos
            && explanation.find("garrison") != std::string::npos && explanation.find("capture") != std::string::npos
            && explanation.find("no player") != std::string::npos, "The explanation omits its human/AI permission");
    control.bHover = true;
    control.tooltipLastMouseMotion = SDL_GetTicks() - 1000;
    control.drawOverlay({0, 0});
    require(control.explanationBounds.w > 0 && control.explanationBounds.h > 0,
            "The option's wrapped explanation was not rendered");
    if(control.isChecked() != on) click(*rules, control);
    require(control.isChecked() == on, "The real checkbox click did not change its state");
    click(*rules, rules->okbutton);
    menu.processChildWindowOpenCloses();
    require(menu.currentGameOptions.garrisoningAndCapturing == on, "Game Rules OK lost the selected permission");
    click(menu.acceptButton);
    require(menu.isQuiting(), "Options APPLY did not accept the player's settings");
    const INIFile config(getConfigFilepath());
    const auto section = userGameOptionsSection();
    require(!section.empty() && config.hasKey(section, "Garrisoning and Capturing")
            && config.getBoolValue(section, "Garrisoning and Capturing", !on) == on,
            "The player's choice was not persisted in the mod's Game Options section");
    auto restored = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(restored.garrisoningAndCapturing == on, "Reloading INI defaults lost the player's permission");
    effectiveGameOptions = restored;
    OptionsMenu reopened;
    auto* reopenedRules = openRules(reopened);
    require(reopenedRules->garrisoningAndCapturingCheckbox.isChecked() == on,
            "Reopening the real options path lost the saved checkbox state");
    click(*reopenedRules, reopenedRules->okbutton);
    reopened.processChildWindowOpenCloses();
    click(reopened.backButton);
    return restored;
}

void configureZoneTactics(bool on) {
    auto& force = getQuantBotConfig().force;
    force.zoneGarrison = force.zoneCaptureRaid = force.zoneCaptureLevy = on;
    force.zoneGarrisonReinforce = force.zoneFrontGarrison = force.zoneCaptureChain = on;
    force.squadOrders = force.infantryLanes = force.infantryInMix = false;
    configuredZoneTactics = on;
}

void requireConfigSwitchesUnchanged() {
    const auto& force = getQuantBotConfig().force;
    require(force.zoneGarrison == configuredZoneTactics && force.zoneCaptureRaid == configuredZoneTactics
            && force.zoneCaptureLevy == configuredZoneTactics && force.zoneGarrisonReinforce == configuredZoneTactics
            && force.zoneFrontGarrison == configuredZoneTactics && force.zoneCaptureChain == configuredZoneTactics,
            "The menu silently rewrote the six independent zone config experiments");
    require(!force.squadOrders && !force.infantryLanes && !force.infantryInMix,
            "The permission rule changed the unrelated infantry production switches");
}

void captureByHuman(Game& game, House* own, const std::array<UnitBase*, 2>& units, StructureBase* target) {
    require(pLocalHouse == own, "The shared-house capture must be controlled by its real human player");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(auto* unit : units) {
        game.selectedList.insert(unit->getObjectID());
        unit->setSelected(true);
    }
    game.selectionChanged();
    game.setCursorMode(Game::CursorMode_Normal);
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<MultiUnitInterface*>(game.pInterface->pObjectContainer);
    require(panel != nullptr, "The human's infantry selection has no real multi-unit panel");
    const bool allowed = !game.isCitySimEnabled() || game.getGameInitSettings().getGameOptions().garrisoningAndCapturing;
    if(allowed) {
        require(panel->captureButton.getTooltipText().find(_("Capture")) != std::string::npos,
                "The real infantry panel does not explain Capture");
        click(panel->captureButton);
        require(game.currentCursorMode == Game::CursorMode_Capture, "The Capture button did not open its cursor");
    } else {
        require(!panel->captureButton.isVisible() && !panel->captureButton.isEnabled(),
                "The OFF rule exposes an enabled multi-unit Capture command");
        game.unselectAll(game.selectedList);
        game.selectedList.clear();
        game.selectedList.insert(units[0]->getObjectID());
        units[0]->setSelected(true);
        game.selectionChanged();
        game.pInterface->updateObjectInterface();
        auto* single = dynamic_cast<UnitInterface*>(game.pInterface->pObjectContainer);
        require(single != nullptr && !single->captureButton.isVisible() && !single->captureButton.isEnabled(),
                "The OFF rule exposes an enabled single-unit Capture command");
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        key.key.state = SDL_PRESSED;
        key.key.keysym.sym = SDLK_c;
        key.key.keysym.scancode = SDL_SCANCODE_C;
        require(SDL_PushEvent(&key) == 1, "The real input queue rejected Capture's keyboard shortcut");
        key.type = SDL_KEYUP;
        key.key.state = SDL_RELEASED;
        require(SDL_PushEvent(&key) == 1, "The real input queue rejected Capture's keyboard release");
        game.doInput();
        require(game.currentCursorMode == Game::CursorMode_Normal, "The OFF rule admits the Capture shortcut");
    }
    const Coord tile = target->getLocation();
    currentGameMap->viewMap(own->getHouseID(), tile, 6);
    screenborder->setNewScreenCenter(Coord(tile.x * TILESIZE + TILESIZE / 2, tile.y * TILESIZE + TILESIZE / 2));
    const int x = screenborder->world2screenX(tile.x * TILESIZE + TILESIZE / 2);
    const int y = screenborder->world2screenY(tile.y * TILESIZE + TILESIZE / 2);
    require(screenborder->isScreenCoordInsideMap(x, y), "The garrison click is outside the real map view");
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    require(SDL_PushEvent(&event) == 1, "The real map rejected the garrison mouse press");
    event.type = SDL_MOUSEBUTTONUP;
    require(SDL_PushEvent(&event) == 1, "The real map rejected the garrison mouse release");
    game.doInput();
    require(game.currentCursorMode == Game::CursorMode_Normal, "The player's garrison click was not accepted");
}

enum class Arm { Activation, AIQuiet, HumanFirst, HumanLast, HumanPermission };

void runArm(SettingsClass::GameOptionsClass rules, bool on, bool city = true, Arm arm = Arm::Activation) {
    require(rules.garrisoningAndCapturing == on, "The match did not receive the real menu choice");
    const bool shared = arm == Arm::HumanFirst || arm == Arm::HumanLast || arm == Arm::HumanPermission;
    // Visibility and deterministic placement are fixture conditions, not changes to the tactic rule.
    rules.fogOfWar = false;
    rules.startWithExploredMap = true;
    rules.concreteRequired = false;
    std::ifstream mapFile(GARRISON_CAPTURE_MAP_PATH);
    require(mapFile.is_open(), "The bundled acceptance map is missing");
    const std::string mapText((std::istreambuf_iterator<char>(mapFile)), {});
    GameInitSettings init("2P - 51x31 - 1v1 - Habbanya-Autumn.ini", mapText, shared, rules);
    init.randomSeed = 20261128;
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 1);
    if(arm == Arm::HumanFirst || arm == Arm::HumanPermission) ai.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
    ai.addPlayerInfo({"Garrison QuantBot", "qBotBrutal"});
    if(arm == Arm::HumanLast) ai.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
    init.addHouseInfo(ai);
    GameInitSettings::HouseInfo opponent(HOUSE_ATREIDES, 2);
    // Offline loading selects every HumanPlayer as local in turn. Only this house's human
    // owns the controls in shared cases; the other real QuantBot is switched off below.
    opponent.addPlayerInfo({shared ? "Quiet opponent" : settings.general.playerName,
                           shared ? "qBotBrutal" : HUMANPLAYERCLASS});
    init.addHouseInfo(opponent);
    OMemoryStream sent;
    sent.open();
    init.save(sent);
    sent.writeUint32(0xa1b2c3d4);
    IMemoryStream received(sent.getData(), sent.getDataLength());
    GameInitSettings wire(received);
    auto serializedRules = rules;
    // City Effects is mod-derived and absent from this established settings stream.
    serializedRules.cityEffects = SettingsClass::GameOptionsClass{}.cityEffects;
    require(wire.getGameOptions() == serializedRules
            && wire.getGameOptions().garrisoningAndCapturing == on
            && wire.getGameOptions().getHash() == serializedRules.getHash()
            && received.readUint32() == 0xa1b2c3d4,
            "The match's saved/multiplayer settings did not round-trip");

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // A private assertion fixture, never an analytics sample.
    game->initGame(wire);
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->isCitySimEnabled() == city, "The real game has the wrong city mode");
    auto* own = game->getHouse(HOUSE_HARKONNEN);
    auto* enemy = game->getHouse(HOUSE_ATREIDES);
    require(own && enemy, "The bundled map did not initialize both houses");
    QuantBot* bot = nullptr;
    for(const auto& player : own->getPlayerList())
        if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
    require(bot != nullptr, "The real map has no QuantBot controller");
    require(ZoneStructure::garrisonsEnabled() == (on && city), "The real city's global capture authority is incorrect");
    require(!bot->cityInfantryShare(), "The permission rule silently enabled the unrelated InfantryInMix switch");
    requireConfigSwitchesUnchanged();

    // An otherwise unchanged bundled map, with a small observable city frontier. Orders themselves
    // are exclusively the AI's normal update path: no direct garrison/build/controller invocation.
    Coord origin = Coord::Invalid();
    for(int y = 2; y + 10 < currentGameMap->getSizeY() && origin.isInvalid(); ++y)
        for(int x = 2; x + 12 < currentGameMap->getSizeX() && origin.isInvalid(); ++x) {
            bool clear = true;
            for(int dy = 0; dy < 10; ++dy) for(int dx = 0; dx < 12; ++dx) {
                const auto* tile = currentGameMap->getTile(x + dx, y + dy);
                clear &= !tile->hasAGroundObject() && !tile->isMountain();
            }
            if(clear) origin = Coord(x, y);
        }
    require(origin.isValid(), "The bundled map has no clear city-frontier fixture space");
    auto placeZone = [&](House* owner, Coord offset) {
        const Coord at = origin + offset;
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
            currentGameMap->getTile(at.x + dx, at.y + dy)->setType(Terrain_Rock);
        auto* zone = dynamic_cast<ZoneStructure*>(owner->placeStructure(NONE_ID, Structure_ZoneResidential,
            at.x, at.y, true, true));
        require(zone != nullptr, "A real frontier zone could not be placed");
        zone->setVisible(VIS_ALL, true);
        return zone->getObjectID();
    };
    if(!city) {
        // A city-only OFF preference must not remove ordinary Dune's real Capture command.
        bot->setRole(AIRole::Off);
        const Coord targetAt = origin + Coord(8, 2);
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
            currentGameMap->getTile(targetAt.x + dx, targetAt.y + dy)->setType(Terrain_Rock);
        auto* target = own->placeStructure(NONE_ID, Structure_WindTrap, targetAt.x, targetAt.y, true, true);
        require(target != nullptr, "Vanilla's real capture target could not be placed");
        target->setHealth(target->getMaxHealth() / 5);
        target->setVisible(VIS_ALL, true);
        std::array<UnitBase*, 2> soldiers{};
        for(int i = 0; i < 2; ++i) {
            soldiers[i] = enemy->createUnit(Unit_Soldier, true);
            require(soldiers[i] != nullptr, "Vanilla's human capture fixture has no real soldiers");
            soldiers[i]->deploy(origin + Coord(7, 3 + i));
            soldiers[i]->setVisible(VIS_ALL, true);
        }
        captureByHuman(*game, enemy, soldiers, target);
        bool captured = false;
        const Uint32 stop = game->getGameCycleCount() + MILLI2CYCLES(60000);
        while(!captured && game->getGameCycleCount() < stop) {
            game->updateGameState();
            const auto* object = currentGameMap->getTile(targetAt.x, targetAt.y)->getGroundObject();
            captured = object != nullptr && object->isAStructure() && object->getOwner() == enemy;
        }
        require(captured, "The city permission disabled vanilla's real human building capture");
        SDL_Log("GARRISON_CAPTURE_VANILLA: stored=%d allowed=0 classic_capture=1 cycles=%u pass=1",
            int(on), game->getGameCycleCount());
        game.reset();
        currentGame = nullptr;
        return;
    }
    if(arm == Arm::HumanPermission) {
        // Keep the actual controller roster, but isolate the player's commands from AI orders.
        bot->setRole(AIRole::Off);
        for(const auto& player : enemy->getPlayerList())
            if(auto* opponentBot = dynamic_cast<QuantBot*>(player.get())) opponentBot->setRole(AIRole::Off);
        const Uint32 ownID = placeZone(own, {2, 2});
        const Uint32 enemyID = placeZone(enemy, {8, 2});
        auto* ownZone = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(ownID));
        auto* enemyZone = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(enemyID));
        std::array<UnitBase*, 2> defenders{}, raiders{};
        for(int i = 0; i < 2; ++i) {
            defenders[i] = own->createUnit(Unit_Soldier, true);
            raiders[i] = own->createUnit(Unit_Soldier, true);
            require(defenders[i] && raiders[i], "The human permission fixture has no real soldiers");
            defenders[i]->deploy(origin + Coord(4, 3 + i));
            raiders[i]->deploy(origin + Coord(7, 4 + i));
            defenders[i]->setVisible(VIS_ALL, true);
            raiders[i]->setVisible(VIS_ALL, true);
        }
        captureByHuman(*game, own, defenders, ownZone);
        captureByHuman(*game, own, raiders, enemyZone);
        const Uint32 stop = game->getGameCycleCount() + MILLI2CYCLES(on ? 60000 : 3000);
        while(game->getGameCycleCount() < stop) game->updateGameState();
        require(game->getObjectManager().getObject(ownID) == ownZone
                && game->getObjectManager().getObject(enemyID) == enemyZone, "A human permission target was destroyed");
        require(ownZone->getGarrisonSize() == (on ? 2 : 0), "The real human's friendly-zone Capture ignored the rule");
        require(enemyZone->getGarrisonSize() == (on ? 2 : 0) && enemyZone->getOwner() == (on ? own : enemy),
                "The real human's enemy-zone Capture ignored the rule");
        requireConfigSwitchesUnchanged();
        SDL_Log("GARRISON_CAPTURE_HUMAN: option=%d own=%d enemy=%d captured=%d cycles=%u pass=1",
            int(on), ownZone->getGarrisonSize(), enemyZone->getGarrisonSize(), int(enemyZone->getOwner() == own),
            game->getGameCycleCount());
        game.reset();
        currentGame = nullptr;
        return;
    }
    if(arm != Arm::Activation) {
        require(on && wire.getGameType() == GameType::CustomGame, "Human safety needs the ordinary city option on");
        const auto& controllers = own->getPlayerList();
        const int humanCount = static_cast<int>(std::count_if(controllers.begin(), controllers.end(), [](const auto& player) {
            return dynamic_cast<const HumanPlayer*>(player.get()) != nullptr;
        }));
        require(humanCount == (shared ? 1 : 0), "The real shared-house controller roster is incorrect");
        // Isolate release from economic redevelopment, while retaining the normal AI unit/garrison updates.
        bot->setRole(AIRole::Fighter);
        if(shared) {
            QuantBot* quietOpponent = nullptr;
            for(const auto& player : enemy->getPlayerList())
                if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) quietOpponent = candidate;
            require(quietOpponent != nullptr, "The quiet opponent needs its real AI controller");
            quietOpponent->setRole(AIRole::Off);
            require(quietOpponent->switchedOff(), "The quiet opponent must not create a front");
        }
        const Uint32 id = placeZone(own, {2, 2});
        auto* zone = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(id));
        std::array<UnitBase*, 2> soldiers{};
        for(int i = 0; i < 2; ++i) {
            soldiers[i] = own->createUnit(Unit_Soldier, true);
            require(soldiers[i] != nullptr, "The quiet garrison has no real soldiers");
            soldiers[i]->deploy(origin + Coord(4, 3 + i));
            soldiers[i]->setVisible(VIS_ALL, true);
        }
        require(std::none_of(structureList.begin(), structureList.end(), [own](const auto* structure) {
            return structure->getOwner()->getTeamID() != own->getTeamID();
        }), "The quiet garrison must not be protected by the front-zone exception");
        if(shared) {
            captureByHuman(*game, own, soldiers, zone);
            const Uint32 enterBy = game->getGameCycleCount() + MILLI2CYCLES(30000);
            while(zone->getGarrisonSize() != 2 && game->getGameCycleCount() < enterBy) game->updateGameState();
        } else {
            // Seed the AI-only release control; the shared cases above must use the player's Capture UI.
            for(auto* soldier : soldiers) require(zone->enterGarrison(soldier), "The AI release control could not be seeded");
        }
        require(zone->getGarrisonSize() == 2, "Both real soldiers must enter before the quiet-garrison check");
        const Uint32 stop = game->getGameCycleCount() + MILLI2CYCLES(60000);
        while(game->getGameCycleCount() < stop) {
            game->updateGameState();
            require(game->getObjectManager().getObject(id) == zone, "The quiet fixture zone was destroyed");
            if(shared) require(zone->getGarrisonSize() == 2, "The AI evacuated a human-shared garrison");
        }
        require(bot->initialMilitaryValue >= 0 && !bot->switchedOff(),
                "The shared-garrison assertion must exercise a running QuantBot");
        require(zone->getGarrisonSize() == (shared ? 2 : 0), "Human presence or the AI-only release control was ignored");
        requireConfigSwitchesUnchanged();
        SDL_Log("GARRISON_CAPTURE_SHARED: human=%d order=%s initial=2 final=%d engine_ai=%d cycles=%u pass=1",
            humanCount, arm == Arm::HumanFirst ? "human_first" : arm == Arm::HumanLast ? "human_last" : "ai_only",
            zone->getGarrisonSize(), int(own->isAI()), game->getGameCycleCount());
        game.reset();
        currentGame = nullptr;
        return;
    }
    const Uint32 ownZone = placeZone(own, {2, 2});
    const Uint32 enemyZone = placeZone(enemy, {8, 2});
    for(int i = 0; i < 8; ++i) {
        auto* unit = own->createUnit(i < 6 ? Unit_Soldier : Unit_Trooper, true);
        require(unit != nullptr, "The city frontier has no real infantry");
        unit->deploy(origin + Coord(4 + i % 4, 5 + i / 4));
        unit->setVisible(VIS_ALL, true);
    }
    own->startingCredits = 20000;
    own->enforceCreditCeiling();
    int occupied = 0, captured = 0;
    const Uint32 stop = game->getGameCycleCount() + MILLI2CYCLES(60000);
    SDL_Log("GARRISON_CAPTURE_ARM: option=%d begin=1 configured=%d", int(on), int(configuredZoneTactics));
    while(game->getGameCycleCount() < stop) {
        game->updateGameState();
        for(Uint32 id : {ownZone, enemyZone}) {
            auto* zone = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(id));
            if(zone && zone->getGarrisonHouse() == own) {
                occupied = std::max(occupied, zone->getGarrisonSize());
                if(id == enemyZone && zone->getOwner() == own) captured = 1;
            }
        }
    }
    require(on ? occupied > 0 : occupied == 0 && captured == 0,
            "The ON arm must reach a real garrison; the OFF arm must not garrison or capture");
    requireConfigSwitchesUnchanged();
    SDL_Log("GARRISON_CAPTURE_ARM: option=%d occupied=%d captured=%d cycles=%u pass=1",
            int(on), occupied, captured, game->getGameCycleCount());
    game.reset();
    currentGame = nullptr;
}
} // namespace GarrisonCaptureOptionProbe

int runGarrisonCaptureOptionProbe() {
    using namespace GarrisonCaptureOptionProbe;
    auto& mods = ModManager::instance();
    settings.general.playerName = "Garrison observer";
    require(mods.setActiveMod("dunecity"), "The normal Dune City content is missing");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    if(const char* expected = SDL_getenv("GARRISON_CAPTURE_MIGRATION")) {
        require((expected[0] == '0' || expected[0] == '1') && (expected[1] == '0' || expected[1] == '1')
                && expected[2] == '\0', "The legacy migration fixture has an invalid expectation");
        const bool global = expected[0] == '1', city = expected[1] == '1';
        const INIFile config(getConfigFilepath());
        require(settings.gameOptions.garrisoningAndCapturing == global
                && config.getBoolValue("Game Options", "Garrisoning and Capturing", true) == global,
                "Startup did not preserve the global legacy permission/new-key precedence");
        require(effectiveGameOptions.garrisoningAndCapturing == city
                && config.getBoolValue("Game Options dunecity", "Garrisoning and Capturing", true) == city,
                "Startup did not preserve the city legacy permission/new-key precedence");
        for(const auto& section : config) {
            const auto name = section.getSectionName();
            if(SDL_strcasecmp(name.c_str(), "Game Options") == 0
               || SDL_strncasecmp(name.c_str(), "Game Options ", sizeof("Game Options ") - 1) == 0)
                require(!config.hasKey(name, "Zone Garrisons") && !config.hasKey(name, "AI Infantry Tactics"),
                        "Startup retained a retired Game Options key");
        }
        require(!config.getBoolValue("gAmE oPtIoNs archived", "Garrisoning and Capturing", true),
                "Startup did not migrate an inactive mod's mixed-case Game Options section");
        require(config.getStringValue("Unrelated", "Zone Garrisons", "") == "untouched"
                && config.getStringValue("Unrelated", "AI Infantry Tactics", "") == "untouched",
                "Startup migration altered an unrelated INI section");
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(rules->garrisoningAndCapturingCheckbox.isVisible()
                && rules->garrisoningAndCapturingCheckbox.isChecked() == city,
                "The real city menu lost the migrated permission");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
        SDL_Log("GARRISON_CAPTURE_MIGRATION: global=%d city=%d old_keys=0 menu=1 pass=1", int(global), int(city));
        return MENU_QUIT_DEFAULT;
    }
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(rules->garrisoningAndCapturingCheckbox.isVisible() && rules->garrisoningAndCapturingCheckbox.isChecked(),
                "Garrisoning and Capturing is not on by default in the normal city menu");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    configureZoneTactics(true);
    const auto offRules = choose(false);
    runArm(offRules, false);
    runArm(offRules, false, true, Arm::HumanPermission);
    configureZoneTactics(false);
    const auto onRules = choose(true);
    runArm(onRules, true);
    runArm(onRules, true, true, Arm::HumanPermission);
    runArm(onRules, true, true, Arm::AIQuiet);
    runArm(onRules, true, true, Arm::HumanFirst);
    runArm(onRules, true, true, Arm::HumanLast);
    require(mods.setActiveMod("vanilla"), "Cannot inspect vanilla's real rules menu");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(!rules->garrisoningAndCapturingCheckbox.isVisible()
                && rules->garrisoningAndCapturingCheckbox.getParent() == nullptr,
                "Vanilla offers the city-only Garrisoning and Capturing rule");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    for(bool stored : {false, true}) {
        auto vanillaRules = effectiveGameOptions;
        vanillaRules.garrisoningAndCapturing = stored;
        runArm(vanillaRules, stored, false);
    }
    SDL_Log("GARRISON_CAPTURE_PASS: default_city_on=1 ini=1 settings=1 human_on=1 human_off=0 ai_on=1 ai_off=0 vanilla_hidden=1");
    return MENU_QUIT_DEFAULT;
}
