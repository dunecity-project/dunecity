// Included only by run-repair-return-probe.py, never by the shipped game.
//
// The Repair Yard's "Return units" switch (CMD_HOUSE_REPAIR_RETURN) and the Dune City default
// for Manual Carryall Drops, through the paths a player uses:
//   - the game is the main menu's Custom Game (initialCustomGameRules, the map and rules screen,
//     the lobby's Start), started from a profile that holds Stefan's saved options;
//   - the switch is the real button in the selected Repair Yard's side panel, and what it queues
//     is an ordinary command the command manager executes a few cycles later;
//   - every trip is flown and repaired by Game::updateGameState() alone.
// Cases, each a fresh match (REPAIR_RETURN_CASE):
//   on            the starting state: a unit flown in for repairs is flown back to where it was
//                 picked up
//   off           the button switched off: the repaired unit is released beside the yard and no
//                 Carryall comes for it; switched on again, the next unit is flown back
//   drove_in      no Carryall was free, so the unit drove in by itself. On: a Carryall that is
//                 free when it is repaired takes it back to the spot it guarded. Off: it stays.
//                 A house without any Carryall releases it beside the yard either way.
//   save          switched off, saved while a unit is in the bay, loaded: the house is still off,
//                 the unit stays by the yard; switched on and saved again, it loads on
//   ai            a QuantBot house starts on and its unit is flown back; a command from another
//                 house's player cannot change it
//   rule_off      Manual Carryall Drops unticked in Game Rules: no return flight (as before this
//                 switch existed) and the button is not shown
//   old_save      a save from before the switch loads with every house on
//   default       only the Manual Carryall Drops default: what the Custom Game screen offers for
//                 the profile the runner wrote (REPAIR_RETURN_EXPECT_DROPS), after the game's own
//                 start-up has read it. "stored_off" is a Dune City section saved before the
//                 default was adopted (as on Stefan's MacBook Air on 2026-11-23): it must come up
//                 on, and an "off" the player saves afterwards must stay off.
#include <Command.h>
#include <CommandManager.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <GameInterface.h>
#include <FileClasses/INIFile.h>
#include <House.h>
#include <Map.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/PlaySetup.h>
#include <GUI/ObjectInterfaces/RepairYardInterface.h>
#include <mod/ModManager.h>
#include <players/PlayerFactory.h>
#include <players/QuantBot.h>
#include <structures/RepairYard.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace RepairReturnProbe {

constexpr int kMapSize = 64;
const Coord kYardSpot(6, 6);                 ///< The local house's Repair Yard, top-left corner.
const Coord kParking(7, 11);                 ///< Where its idle Carryall waits.
const Coord kPost(34, 36);                   ///< Where the damaged unit stands, 28 tiles away.
const Coord kSecondPost(30, 40);             ///< A second unit's post.
/// The map is point-symmetric: the other house's yard, parking and posts are these, mirrored.
Coord mirror(const Coord& c) { return Coord(kMapSize - 1 - c.x, kMapSize - 1 - c.y); }
constexpr Uint32 kTripCycles = 14000;        ///< A whole trip: lift, repair and the flight back.
constexpr Uint32 kWatchCycles = 1500;        ///< How long a released unit is watched by the yard.
constexpr int kNearPost = 3;                 ///< "Back where it was picked up", in tiles.
constexpr int kByYard = 5;                   ///< "Beside the yard", in tiles from its nearest tile.

void require(bool ok, const std::string& what) {
    if(!ok) throw std::runtime_error(what);
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string coord(const Coord& c) { return std::to_string(c.x) + "," + std::to_string(c.y); }

struct Match {
    std::unique_ptr<Game> game;
    House* home = nullptr;
    House* enemy = nullptr;
    RepairYard* yard = nullptr;
    RepairYard* enemyYard = nullptr;
    bool mirrored = false;   ///< the lobby seated the local human in the map's second corner
    Coord at(const Coord& c) const { return mirrored ? mirror(c) : c; }
    Coord enemyAt(const Coord& c) const { return mirrored ? c : mirror(c); }
};

void locate(Match& match) {
    match.home = pLocalHouse;
    require(match.home != nullptr && pLocalPlayer != nullptr, "The match has no local human house");
    match.enemy = nullptr;
    match.yard = nullptr;
    for(int h = 0; h < NUM_HOUSES; ++h) {
        House* house = match.game->getHouse(h);
        if(house && house != match.home && house->getNumStructures() > 0) match.enemy = house;
    }
    match.enemyYard = nullptr;
    for(StructureBase* structure : structureList) {
        if(structure->getItemID() != Structure_RepairYard) continue;
        if(structure->getOwner() == match.home) match.yard = static_cast<RepairYard*>(structure);
        else if(structure->getOwner() == match.enemy) match.enemyYard = static_cast<RepairYard*>(structure);
    }
    require(match.enemy != nullptr && match.yard != nullptr && match.enemyYard != nullptr,
            "The fixture houses or their Repair Yards are missing");
    match.mirrored = match.yard->getLocation() != kYardSpot;
}

/// The main menu's Custom Game, as city-plane-flights.inc starts one: the saved mod is activated,
/// its effective options are the rules, the lobby seats the local human and QuantBot Brutal.
Match start(bool manualDrops) {
    const std::string mapPath = env("REPAIR_RETURN_MAP");
    CustomPlaySetup setup;
    setup.rules = initialCustomGameRules();
    require(ModManager::instance().getActiveModName() == "dunecity", "Custom Game did not activate the saved dunecity mod");
    // The default under test: a Dune City Custom Game offers Manual Carryall Drops ticked.
    require(setup.rules.manualCarryallDrops, "A Dune City Custom Game does not start with Manual Carryall Drops on");
    setup.maps = {mapPath};
    setup.map = 0;
    setup.mods = ModManager::instance().listModChoices();
    for(size_t i = 0; i < setup.mods.size(); ++i)
        if(setup.mods[i].matchesSelectionName(ModManager::instance().getActiveModName())) setup.mod = static_cast<int>(i);
    setup.online = false;
    setup.sharedHouse = false;
    {
        CustomGameMenu browser(false, false, &setup);
        require(browser.currentGameOptions == setup.rules && browser.currentGameOptions.manualCarryallDrops,
                "The Custom Game screen does not show Manual Carryall Drops on");
    }
    setup.rules.manualCarryallDrops = manualDrops;   // the Game Rules window's result for this case
    GameInitSettings init(getBasename(mapPath, true), readCompleteFile(mapPath), setup.multiplePlayersPerHouse(), setup.rules);
    {
        CustomGamePlayers lobby(init, true, false, &setup);
        lobby.update();
        require(lobby.numHouses == 2, "The lobby does not seat the map's two houses");
        const int ai = PlayerFactory::getIndexByPlayerClass("qBotBrutal");
        require(ai >= 0, "Missing QuantBot Brutal");
        int humans = 0;
        for(int row = 0; row < lobby.numHouses; ++row) {
            if(lobby.getSeatPlayer(2 * row) == 0) { ++humans; continue; }
            require(lobby.selectSeatPlayer(2 * row, ai), "The lobby cannot seat QuantBot Brutal");
            lobby.onChangePlayerDropDownBoxes(true, 2 * row);
        }
        require(humans == 1, "The lobby did not seat exactly the local human");
        lobby.addAllPlayersToGameInitSettings();
        init = lobby.gameInitSettings;
    }
    init.randomSeed = 20261123;
    Match match;
    match.game = std::make_unique<Game>();
    currentGame = match.game.get();
    match.game->bReplay = true;   // a private fixture, never a match-analytics sample
    match.game->initGame(init);
    match.game->bReplay = false;
    match.game->initializeGameLoop();
    match.game->bPause = false;
    require(match.game->getGameInitSettings().getGameOptions().manualCarryallDrops == manualDrops,
            "The started game did not keep the Manual Carryall Drops rule");
    locate(match);
    return match;
}

void finish(Match& match) {
    match.game.reset();
    currentGame = nullptr;
    pLocalHouse = nullptr;
    pLocalPlayer = nullptr;
}

GroundUnit* tank(House* house, const Coord& at) {
    auto* unit = static_cast<GroundUnit*>(house->createUnit(Unit_Tank));
    require(unit != nullptr, "Cannot create the tank");
    unit->deploy(at);
    return unit;
}

Carryall* carryall(House* house, const Coord& at) {
    auto* unit = static_cast<Carryall*>(house->createUnit(Unit_Carryall));
    require(unit != nullptr, "Cannot create the Carryall");
    unit->deploy(at);
    return unit;
}

void run(Game& game, Uint32 cycles) {
    for(Uint32 t = 0; t < cycles; ++t) {
        game.updateGameState();
        require(!game.finished, "The fixture match was decided at cycle " + std::to_string(game.getGameCycleCount()));
    }
}

size_t commandCount(const Game& game) {
    size_t count = 0;
    for(const auto& [cycle, slot] : game.cmdManager.timeslot) count += slot.size();
    return count;
}

/// Selects the yard as a click on it does and returns the real side panel.
RepairYardInterface* select(Game& game, RepairYard* yard) {
    require(game.pInterface != nullptr, "The game has no interface");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(yard->getObjectID());
    yard->setSelected(true);
    game.selectionChanged();
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<RepairYardInterface*>(game.pInterface->pObjectContainer);
    require(panel && panel->update(), "Selecting the Repair Yard did not open its panel");
    return panel;
}

/// Clicks the panel's switch and plays until the command has run. Returns the new state.
bool clickSwitch(Match& match, const std::string& label) {
    Game& game = *match.game;
    auto* panel = select(game, match.yard);
    require(panel->returnButton.isVisible() && panel->returnButton.isEnabled(), label + ": the Return units button is not clickable");
    const bool before = match.home->isRepairReturnEnabled();
    require(panel->returnButton.getText() == (before ? _("Return units on") : _("Return units off")),
            label + ": the button does not show the house's state");
    const size_t queued = commandCount(game);
    panel->returnButton.handleMouseLeft(5, 5, true);
    panel->returnButton.handleMouseLeft(5, 5, false);
    require(commandCount(game) == queued + 1, label + ": the button did not queue exactly one command");
    require(match.home->isRepairReturnEnabled() == before, label + ": the button changed the house without a command");
    Uint32 waited = 0;
    while(match.home->isRepairReturnEnabled() == before && waited < 200) { run(game, 1); ++waited; }
    require(match.home->isRepairReturnEnabled() != before, label + ": the command never changed the house");
    // Every Repair Yard panel of the house shows the new state on its next update.
    require(panel->update() && panel->returnButton.getText() == (before ? _("Return units off") : _("Return units on")),
            label + ": the button kept its old text");
    SDL_Log("REPAIR_RETURN_PROBE_SWITCH: %s now=%d after_cycles=%u", label.c_str(), int(!before), waited);
    return !before;
}

struct Trip {
    Uint32 pickedUp = 0, inBay = 0, released = 0, flownBack = 0, landed = 0;
    Coord releasedAt = Coord::Invalid();
    Coord end = Coord::Invalid();
    Coord pickupSpot = Coord::Invalid();
};

/// Plays one repair trip to its end: into the bay, out again and, where a Carryall takes the
/// repaired unit, until it has been set down. Then watches it for kWatchCycles.
Trip follow(Game& game, GroundUnit* unit, RepairYard* yard) {
    Trip trip;
    bool wasInBay = false, out = false;
    Uint32 quiet = 0;
    for(Uint32 t = 1; t <= kTripCycles + kWatchCycles; ++t) {
        game.updateGameState();
        require(!game.finished, "The fixture match was decided at cycle " + std::to_string(game.getGameCycleCount()));
        const bool inBay = yard->getContainedRepairUnit() == unit;
        if(!wasInBay && !out && !trip.pickedUp && unit->isPickedUp()) {
            trip.pickedUp = t;
        }
        if(!trip.pickedUp && !inBay && !out && unit->isActive()) trip.pickupSpot = unit->getLocation();
        if(inBay && !wasInBay) { trip.inBay = t; wasInBay = true; }
        if(wasInBay && !inBay && !out) {
            out = true;
            trip.released = t;
            if(unit->isPickedUp()) trip.flownBack = t;
            else trip.releasedAt = unit->getLocation();
        }
        if(out) {
            if(unit->isPickedUp() && !trip.flownBack) trip.flownBack = t;
            if(trip.flownBack && !trip.landed && unit->isActive() && !unit->isPickedUp()) trip.landed = t;
            const bool settled = unit->isActive() && !unit->isPickedUp() && !unit->isAwaitingPickup();
            quiet = settled ? quiet + 1 : 0;
            if(quiet >= kWatchCycles) break;
        }
    }
    trip.end = unit->getLocation();
    return trip;
}

std::string describe(const GroundUnit* unit, const Trip& trip) {
    std::ostringstream text;
    text << " (picked_up=" << trip.pickedUp << " in_bay=" << trip.inBay << " released=" << trip.released
         << " flown_back=" << trip.flownBack << " landed=" << trip.landed << " end=" << coord(trip.end)
         << " guard=" << coord(unit->getGuardPoint()) << " mode=" << unit->getAttackMode()
         << " health=" << unit->getHealth().lround() << '/' << unit->getMaxHealth() << ')';
    return text.str();
}

void expectBack(const std::string& label, const GroundUnit* unit, const Coord& post, const Trip& trip) {
    const std::string why = describe(unit, trip);
    require(trip.inBay != 0 && trip.released != 0, label + ": the unit was never repaired and released" + why);
    require(unit->getHealth() == unit->getMaxHealth(), label + ": the unit came out unrepaired" + why);
    require(trip.flownBack != 0 && trip.landed != 0, label + ": no Carryall flew the repaired unit back" + why);
    require(blockDistance(trip.end, post).lround() <= kNearPost,
            label + ": the unit did not end where it was picked up, " + coord(post) + why);
    SDL_Log("REPAIR_RETURN_PROBE_BACK: %s post=%s end=%s picked_up=%u in_bay=%u flown_back=%u landed=%u",
            label.c_str(), coord(post).c_str(), coord(trip.end).c_str(), trip.pickedUp, trip.inBay, trip.flownBack, trip.landed);
}

void expectStays(const std::string& label, const GroundUnit* unit, RepairYard* yard, const Trip& trip) {
    const std::string why = describe(unit, trip);
    require(trip.inBay != 0 && trip.released != 0, label + ": the unit was never repaired and released" + why);
    require(unit->getHealth() == unit->getMaxHealth(), label + ": the unit came out unrepaired" + why);
    require(trip.flownBack == 0, label + ": a Carryall took the repaired unit away" + why);
    require(blockDistance(trip.end, yard->getClosestPoint(trip.end)).lround() <= kByYard,
            label + ": the unit did not stay beside the Repair Yard" + why);
    require(!unit->isAwaitingPickup() && !unit->hasBookedCarrier(), label + ": the unit is still waiting for a Carryall" + why);
    SDL_Log("REPAIR_RETURN_PROBE_STAYS: %s end=%s picked_up=%u in_bay=%u released=%u",
            label.c_str(), coord(trip.end).c_str(), trip.pickedUp, trip.inBay, trip.released);
}

void damage(GroundUnit* unit) { unit->setHealth(FixPoint(unit->getMaxHealth()) / 3); }

void idleFleet(const std::string& label, House* house) {
    for(const UnitBase* unit : unitList)
        if(unit->getOwner() == house && isCarryallUnit(unit->getItemID()))
            require(!static_cast<const Carryall*>(unit)->hasCargo(), label + ": a Carryall still holds cargo");
}

void caseOn() {
    Match match = start(true);
    require(match.home->isRepairReturnEnabled() && match.enemy->isRepairReturnEnabled(),
            "on: a new game does not start with Return units on for every house");
    carryall(match.home, match.at(kParking));
    auto* unit = tank(match.home, match.at(kPost));
    run(*match.game, 10);
    auto* panel = select(*match.game, match.yard);
    require(panel->returnButton.isVisible() && panel->returnButton.getText() == _("Return units on"),
            "on: the Repair Yard panel does not show Return units on");
    damage(unit);
    const Trip trip = follow(*match.game, unit, match.yard);
    require(trip.pickedUp != 0, "on: the damaged unit was not flown to the yard" + describe(unit, trip));
    expectBack("on", unit, match.at(kPost), trip);
    idleFleet("on", match.home);
    finish(match);
}

void caseOff() {
    Match match = start(true);
    carryall(match.home, match.at(kParking));
    auto* unit = tank(match.home, match.at(kPost));
    run(*match.game, 10);
    require(!clickSwitch(match, "off"), "off: the first click did not switch Return units off");
    damage(unit);
    const Trip trip = follow(*match.game, unit, match.yard);
    require(trip.pickedUp != 0, "off: the damaged unit was not flown to the yard" + describe(unit, trip));
    expectStays("off", unit, match.yard, trip);
    idleFleet("off", match.home);
    // On again: the next unit goes back to its post, and the one already by the yard stays.
    require(clickSwitch(match, "off/on_again"), "off: the second click did not switch Return units on");
    const Coord rested = unit->getLocation();
    auto* second = tank(match.home, match.at(kSecondPost));
    run(*match.game, 10);
    damage(second);
    const Trip again = follow(*match.game, second, match.yard);
    expectBack("off/on_again", second, match.at(kSecondPost), again);
    require(blockDistance(unit->getLocation(), rested).lround() <= 1, "off: switching on again moved a unit released earlier");
    finish(match);
}

void caseDroveIn() {
    for(const bool on : {true, false}) {
        const std::string label = on ? "drove_in/on" : "drove_in/off";
        Match match = start(true);
        auto* unit = tank(match.home, match.at(kPost));
        run(*match.game, 10);
        if(!on) require(!clickSwitch(match, label), label + ": could not switch Return units off");
        require(!match.home->hasCarryalls(), label + ": the fixture house owns a Carryall");
        damage(unit);
        unit->handleSendToRepairClick();   // without a Carryall the engine's own rule does not send it
        Carryall* late = nullptr;
        Trip trip;
        {
            // The unit drives all the way in. A Carryall only joins once it is in the bay.
            Uint32 t = 0;
            while(match.yard->getContainedRepairUnit() != unit && t < kTripCycles) { run(*match.game, 1); ++t; }
            require(match.yard->getContainedRepairUnit() == unit, label + ": the unit never drove into the bay");
            require(!unit->isPickedUp(), label + ": a unit with no Carryall was carried");
            late = carryall(match.home, match.at(kParking));
            trip = follow(*match.game, unit, match.yard);
            trip.inBay = t;
        }
        if(on) {
            // No pickup spot exists for a unit that drove in. It goes back to the post it was
            // guarding, which is what the yard has always used for the flight.
            require(trip.released != 0 && trip.flownBack != 0 && trip.landed != 0
                    && blockDistance(trip.end, match.at(kPost)).lround() <= kNearPost,
                    label + ": the unit that drove in was not taken back to its post" + describe(unit, trip));
            SDL_Log("REPAIR_RETURN_PROBE_BACK: %s post=%s end=%s drove_in=1 flown_back=%u landed=%u", label.c_str(),
                    coord(match.at(kPost)).c_str(), coord(trip.end).c_str(), trip.flownBack, trip.landed);
        } else {
            expectStays(label, unit, match.yard, trip);
        }
        require(!late->hasCargo(), label + ": the Carryall still holds cargo");
        finish(match);
    }
    // No Carryall at all: nothing to fly, on or off. The unit is released beside the yard.
    Match match = start(true);
    auto* unit = tank(match.home, match.at(kPost));
    run(*match.game, 10);
    damage(unit);
    unit->handleSendToRepairClick();
    const Trip trip = follow(*match.game, unit, match.yard);
    require(trip.pickedUp == 0, "drove_in/no_carryall: a unit with no Carryall was carried");
    expectStays("drove_in/no_carryall", unit, match.yard, trip);
    finish(match);
}

void caseSave() {
    const std::string out = env("REPAIR_RETURN_OUT");
    Match match = start(true);
    carryall(match.home, match.at(kParking));
    auto* unit = tank(match.home, match.at(kPost));
    run(*match.game, 10);
    require(!clickSwitch(match, "save"), "save: could not switch Return units off");
    damage(unit);
    Uint32 t = 0;
    while(match.yard->getContainedRepairUnit() != unit && t < kTripCycles) { run(*match.game, 1); ++t; }
    require(match.yard->getContainedRepairUnit() == unit, "save: the unit never reached the bay");
    const Uint32 unitID = unit->getObjectID();
    const int homeID = match.home->getHouseID(), enemyID = match.enemy->getHouseID();
    const std::string offPath = out + "/repair-return-off.dls";
    require(match.game->saveGame(offPath), "save: could not write the save");
    finish(match);

    match.game = std::make_unique<Game>();
    currentGame = match.game.get();
    match.game->initGame(GameInitSettings(offPath));
    match.game->initializeGameLoop();
    match.game->bPause = false;
    locate(match);
    require(match.home->getHouseID() == homeID && !match.home->isRepairReturnEnabled(),
            "save: the loaded house forgot that Return units is off");
    require(match.game->getHouse(enemyID)->isRepairReturnEnabled(), "save: the other house lost its own state");
    unit = dynamic_cast<GroundUnit*>(match.game->getObjectManager().getObject(unitID));
    require(unit != nullptr && match.yard->getContainedRepairUnit() == unit, "save: the unit in the bay did not come back");
    auto* panel = select(*match.game, match.yard);
    require(panel->returnButton.getText() == _("Return units off"), "save: the loaded panel does not show Return units off");
    const Trip trip = follow(*match.game, unit, match.yard);
    Trip shown = trip;
    shown.inBay = t;
    expectStays("save/loaded_off", unit, match.yard, shown);
    // Switched on in the loaded game and saved again: it loads on.
    require(clickSwitch(match, "save/on"), "save: could not switch Return units on in the loaded game");
    const std::string onPath = out + "/repair-return-on.dls";
    require(match.game->saveGame(onPath), "save: could not write the second save");
    finish(match);
    match.game = std::make_unique<Game>();
    currentGame = match.game.get();
    match.game->initGame(GameInitSettings(onPath));
    match.game->initializeGameLoop();
    locate(match);
    require(match.home->isRepairReturnEnabled(), "save: the second save did not load with Return units on");
    SDL_Log("REPAIR_RETURN_PROBE_SAVE: off_restored=1 on_restored=1 save_format=%d", SAVEGAMEVERSION);
    finish(match);

}

void caseOldSave() {
    // A save from before the switch existed has no such flag: every house loads on.
    const std::string oldSave = env("REPAIR_RETURN_OLD_SAVE");
    require(!oldSave.empty(), "old_save: no fixture");
    Match match;
    match.game = std::make_unique<Game>();
    currentGame = match.game.get();
    match.game->initGame(GameInitSettings(oldSave));
    match.game->initializeGameLoop();
    require(match.game->getLoadedSavegameVersion() < SAVEGAMEVERSION_REPAIR_RETURN,
            "old_save: the fixture is not older than the switch");
    int houses = 0;
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(const House* house = match.game->getHouse(h)) {
            ++houses;
            require(house->isRepairReturnEnabled(), "old_save: an older save loaded with Return units off");
        }
    require(houses >= 2, "old_save: the fixture has no houses");
    SDL_Log("REPAIR_RETURN_PROBE_SAVE: old_save_format=%d houses=%d all_on=1",
            int(match.game->getLoadedSavegameVersion()), houses);
    for(int i = 0; i < 50; ++i) match.game->updateGameState();
    finish(match);
}

/// What the Custom Game screen offers for this profile, after main() has read it.
void caseDefault() {
    const bool expect = env("REPAIR_RETURN_EXPECT_DROPS") == "1";
    const std::string profile = env("REPAIR_RETURN_PROFILE");
    CustomPlaySetup setup;
    setup.rules = initialCustomGameRules();
    require(ModManager::instance().getActiveModName() == "dunecity", "default: Custom Game did not activate dunecity");
    setup.maps = {env("REPAIR_RETURN_MAP")};
    setup.map = 0;
    setup.mods = ModManager::instance().listModChoices();
    CustomGameMenu browser(false, false, &setup);
    require(browser.currentGameOptions.manualCarryallDrops == expect && setup.rules.manualCarryallDrops == expect,
            std::string("default/") + profile + ": the Custom Game screen offers Manual Carryall Drops "
                + (expect ? "off" : "on"));
    INIFile config(getConfigFilepath());
    const bool stored = config.hasKey(kDuneCityGameOptionsSection, "Manual Carryall Drops");
    const int revision = config.getIntValue(kDuneCityGameOptionsSection, kCarryallDropDefaultKey, 0);
    if(profile == "stored_off") {
        require(!stored && revision == 1, "default/stored_off: start-up did not drop the stored off and record the revision");
        // Unticked again in Game Rules with "Remember for new games": that one is the player's.
        auto chosen = setup.rules;
        chosen.manualCarryallDrops = false;
        saveGameOptionsAsDefaults(chosen);
        INIFile saved(getConfigFilepath());
        require(!saved.getBoolValue(kDuneCityGameOptionsSection, "Manual Carryall Drops", true)
                && saved.getIntValue(kDuneCityGameOptionsSection, kCarryallDropDefaultKey, 0) == 1,
                "default/stored_off: the player's own off was not stored with the revision");
        require(!adoptCarryallDropDefault(saved), "default/stored_off: the player's own off would be dropped at the next start");
        require(!initialCustomGameRules().manualCarryallDrops, "default/stored_off: the player's own off is not offered");
    } else if(profile == "chosen_off") {
        require(stored && revision == 1, "default/chosen_off: start-up touched a choice saved after the default");
    } else if(profile == "fresh") {
        require(!config.hasSection(kDuneCityGameOptionsSection), "default/fresh: start-up created a Dune City section");
    }
    // Vanilla keeps its own default, off, whatever the Dune City section says.
    require(ModManager::instance().setActiveMod("vanilla"), "default: cannot activate vanilla");
    require(!ModManager::instance().loadEffectiveGameOptions(settings.gameOptions).manualCarryallDrops,
            "default: Vanilla took the Dune City Carryall default");
    require(ModManager::instance().setActiveMod("dunecity"), "default: cannot restore dunecity");
    SDL_Log("REPAIR_RETURN_PROBE_DEFAULT: profile=%s manual_carryall_drops=%d stored=%d revision=%d", profile.c_str(),
            int(expect), int(stored), revision);
}

void caseAI() {
    // The QuantBot house of the same Custom Game: its own yard, Carryall and damaged unit.
    Match match = start(true);
    House* bot = match.enemy;
    require(bot->isAI() && bot->isRepairReturnEnabled(), "ai: the QuantBot house does not start with Return units on");
    RepairYard* yard = match.enemyYard;
    const Coord post = match.enemyAt(kPost);
    carryall(bot, match.enemyAt(kParking));
    auto* unit = tank(bot, post);
    run(*match.game, 10);
    // A command from the human changes the human's own house only, whatever it says.
    require(!clickSwitch(match, "ai/human_off"), "ai: could not switch the human house off");
    require(bot->isRepairReturnEnabled(), "ai: the human's command changed the QuantBot house");
    damage(unit);
    const Trip trip = follow(*match.game, unit, yard);
    require(bot->isRepairReturnEnabled(), "ai: the QuantBot house changed its own state");
    require(trip.inBay != 0 && trip.released != 0 && trip.flownBack != 0 && trip.landed != 0,
            "ai: the QuantBot's repaired unit was not flown out of the yard" + describe(unit, trip));
    require(blockDistance(trip.end, yard->getClosestPoint(trip.end)).lround() > kByYard,
            "ai: the QuantBot's repaired unit was left beside the yard" + describe(unit, trip));
    SDL_Log("REPAIR_RETURN_PROBE_BACK: ai post=%s end=%s flown_back=%u landed=%u", coord(post).c_str(),
            coord(trip.end).c_str(), trip.flownBack, trip.landed);
    finish(match);
}

void caseRuleOff() {
    Match match = start(false);
    carryall(match.home, match.at(kParking));
    auto* unit = tank(match.home, match.at(kPost));
    run(*match.game, 10);
    auto* panel = select(*match.game, match.yard);
    require(!panel->returnButton.isVisible(), "rule_off: the button is shown although the rule allows no return flight");
    require(match.home->isRepairReturnEnabled(), "rule_off: the house's own state changed with the rule");
    damage(unit);
    const Trip trip = follow(*match.game, unit, match.yard);
    expectStays("rule_off", unit, match.yard, trip);
    finish(match);
}

} // namespace RepairReturnProbe

int runRepairReturnProbe() {
    using namespace RepairReturnProbe;
    const std::string name = env("REPAIR_RETURN_CASE");
    settings.network.metaServer = "http://127.0.0.1:1/";
    try {
        if(name == "on") caseOn();
        else if(name == "off") caseOff();
        else if(name == "drove_in") caseDroveIn();
        else if(name == "save") caseSave();
        else if(name == "ai") caseAI();
        else if(name == "rule_off") caseRuleOff();
        else if(name == "old_save") caseOldSave();
        else if(name == "default") caseDefault();
        else throw std::runtime_error("unknown case");
    } catch(const std::exception& e) {
        SDL_Log("REPAIR_RETURN_PROBE_FAIL: %s: %s", name.c_str(), e.what());
        return MENU_QUIT_DEFAULT;
    }
    SDL_Log("REPAIR_RETURN_PROBE_PASS: %s", name.c_str());
    return MENU_QUIT_DEFAULT;
}
