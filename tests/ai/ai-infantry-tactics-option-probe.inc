// Test-only entry point: real Options/Game Rules clicks, then real AI updates on a bundled map.
#include <Menu/OptionsMenu.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <Game.h>
#include <GameInterface.h>
#include <ScreenBorder.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
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

namespace AIInfantryTacticsOptionProbe {
void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("AI_INFANTRY_TACTICS_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

template<class Control> void click(Control& control) {
    require(control.isVisible() && control.isEnabled(), "The player's control is hidden or disabled");
    control.handleMouseLeft(4, control.getSize().y / 2, true);
    control.handleMouseLeft(4, control.getSize().y / 2, false);
}

void requireBounds(Window& window, Widget& control) {
    Point at = window.getPosition();
    for(Widget* current = &control; current != &window;) {
        auto* parent = current->getParent();
        require(parent != nullptr, "Rule control is not in the real window");
        if(auto* box = dynamic_cast<HBox*>(parent)) at = at + box->getWidgetPosition(current);
        else if(auto* box = dynamic_cast<VBox*>(parent)) at = at + box->getWidgetPosition(current);
        else require(parent == &window, "Unexpected rule control container");
        current = parent;
    }
    const auto size = control.getSize();
    require(size.x > 0 && size.y > 0 && at.x >= 0 && at.y >= 0
            && at.x + size.x <= getRendererWidth() && at.y + size.y <= getRendererHeight(),
            "AI infantry tactics is clipped or outside the viewport");
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
    auto& control = rules->aiInfantryTacticsCheckbox;
    requireBounds(*rules, control);
    require(control.isVisible(), "The city option is missing from Game Rules");
    require(control.getText() == _("AI infantry tactics"), "The city option has the wrong label");
    const auto explanation = control.getTooltipText();
    require(explanation.find("garrison") != std::string::npos && explanation.find("capture") != std::string::npos
            && explanation.find("squad") != std::string::npos && explanation.find("lanes") != std::string::npos,
            "The option explanation does not describe its tactics");
    control.bHover = true;
    control.tooltipLastMouseMotion = SDL_GetTicks() - 1000;
    control.drawOverlay({0, 0});
    require(control.explanationBounds.w > 0 && control.explanationBounds.h > 0,
            "The option's wrapped explanation was not rendered");
    if(control.isChecked() != on) click(control);
    require(control.isChecked() == on, "The real checkbox click did not change its state");
    click(rules->okbutton);
    menu.processChildWindowOpenCloses();
    require(menu.currentGameOptions.aiInfantryTactics == on, "Game Rules OK lost the selected option");
    click(menu.acceptButton);
    require(menu.isQuiting(), "Options APPLY did not accept the player's settings");
    const INIFile config(getConfigFilepath());
    const auto section = userGameOptionsSection();
    require(!section.empty() && config.hasKey(section, "AI Infantry Tactics")
            && config.getBoolValue(section, "AI Infantry Tactics", !on) == on,
            "The player's choice was not persisted in the mod's Game Options section");
    auto restored = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(restored.aiInfantryTactics == on, "Reloading INI defaults lost the player's choice");
    effectiveGameOptions = restored;
    OptionsMenu reopened;
    auto* reopenedRules = openRules(reopened);
    require(reopenedRules->aiInfantryTacticsCheckbox.isChecked() == on,
            "Reopening the real options path lost the saved checkbox state");
    click(reopenedRules->okbutton);
    reopened.processChildWindowOpenCloses();
    click(reopened.backButton);
    return restored;
}

void clearConfigSwitches() {
    auto& force = getQuantBotConfig().force;
    force.zoneGarrison = force.zoneCaptureRaid = force.zoneCaptureLevy = false;
    force.zoneGarrisonReinforce = force.zoneFrontGarrison = force.zoneCaptureChain = false;
    force.squadOrders = force.infantryLanes = force.infantryInMix = false;
}

void requireConfigSwitchesOff() {
    const auto& force = getQuantBotConfig().force;
    require(!force.zoneGarrison && !force.zoneCaptureRaid && !force.zoneCaptureLevy
            && !force.zoneGarrisonReinforce && !force.zoneFrontGarrison && !force.zoneCaptureChain
            && !force.squadOrders && !force.infantryLanes && !force.infantryInMix,
            "The menu option modified the independent QuantBot config switches");
}

void captureByHuman(Game& game, House* own, const std::array<UnitBase*, 2>& units, ZoneStructure* zone) {
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
    require(panel->captureButton.getTooltipText().find(_("Capture")) != std::string::npos,
            "The real infantry panel does not explain Capture");
    click(panel->captureButton);
    require(game.currentCursorMode == Game::CursorMode_Capture, "The Capture button did not open its cursor");
    const Coord tile = zone->getLocation();
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

enum class Arm { Activation, AIQuiet, HumanFirst, HumanLast };

void runArm(SettingsClass::GameOptionsClass rules, bool on, bool city = true, Arm arm = Arm::Activation) {
    require(rules.aiInfantryTactics == on, "The match did not receive the real menu choice");
    const bool shared = arm == Arm::HumanFirst || arm == Arm::HumanLast;
    // Visibility and deterministic placement are fixture conditions, not changes to the tactic rule.
    rules.fogOfWar = false;
    rules.startWithExploredMap = true;
    rules.concreteRequired = false;
    std::ifstream mapFile(AI_INFANTRY_TACTICS_MAP_PATH);
    require(mapFile.is_open(), "The bundled acceptance map is missing");
    const std::string mapText((std::istreambuf_iterator<char>(mapFile)), {});
    GameInitSettings init("2P - 51x31 - 1v1 - Habbanya-Autumn.ini", mapText, shared, rules);
    init.randomSeed = 20261128;
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 1);
    if(arm == Arm::HumanFirst) ai.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
    ai.addPlayerInfo({"Tactics QuantBot", "qBotBrutal"});
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
            && wire.getGameOptions().aiInfantryTactics == on
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
    require(bot->aiInfantryTacticsEnabled(false) == (on && city) && bot->aiInfantryTacticsEnabled(true),
            "The match option is not ORed with independent config experiments");
    requireConfigSwitchesOff();
    if(!city) {
        SDL_Log("AI_INFANTRY_TACTICS_VANILLA: option=%d effective=0 config_override=1 pass=1", int(on));
        game.reset();
        currentGame = nullptr;
        return;
    }

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
        requireConfigSwitchesOff();
        SDL_Log("AI_INFANTRY_TACTICS_SHARED: human=%d order=%s initial=2 final=%d engine_ai=%d cycles=%u pass=1",
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
    SDL_Log("AI_INFANTRY_TACTICS_ARM: option=%d begin=1", int(on));
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
    requireConfigSwitchesOff();
    SDL_Log("AI_INFANTRY_TACTICS_ARM: option=%d occupied=%d captured=%d cycles=%u pass=1",
            int(on), occupied, captured, game->getGameCycleCount());
    game.reset();
    currentGame = nullptr;
}
} // namespace AIInfantryTacticsOptionProbe

int runAIInfantryTacticsOptionProbe() {
    using namespace AIInfantryTacticsOptionProbe;
    auto& mods = ModManager::instance();
    settings.general.playerName = "AI tactics observer";
    require(mods.setActiveMod("dunecity"), "The normal Dune City content is missing");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(rules->aiInfantryTacticsCheckbox.isVisible() && rules->aiInfantryTacticsCheckbox.isChecked(),
                "AI infantry tactics is not on by default in the normal city menu");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    clearConfigSwitches();
    runArm(choose(false), false);
    const auto onRules = choose(true);
    runArm(onRules, true);
    runArm(onRules, true, true, Arm::AIQuiet);
    runArm(onRules, true, true, Arm::HumanFirst);
    runArm(onRules, true, true, Arm::HumanLast);
    require(mods.setActiveMod("vanilla"), "Cannot inspect vanilla's real rules menu");
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    {
        OptionsMenu menu;
        auto* rules = openRules(menu);
        require(!rules->aiInfantryTacticsCheckbox.isVisible()
                && rules->aiInfantryTacticsCheckbox.getParent() == nullptr,
                "Vanilla offers the city-only AI option");
        click(rules->okbutton);
        menu.processChildWindowOpenCloses();
        click(menu.backButton);
    }
    runArm(effectiveGameOptions, effectiveGameOptions.aiInfantryTactics, false);
    SDL_Log("AI_INFANTRY_TACTICS_PASS: default_city_on=1 ini=1 settings=1 on_activation=1 off_activation=0 vanilla_hidden=1");
    return MENU_QUIT_DEFAULT;
}
