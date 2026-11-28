// Saboteurs Skip City Zones (GameOptionsClass::saboteurSkipsCityZones; Stefan, 2026-11-28: "also make
// sabateurs not target r/i/c (not valuable enough) unless it is garissoned").
//
// Each case runs the real Palace special weapon (Palace::doSpecialWeapon -> spawnSaboteur) and
// Game::updateGameState on a 64x64 Dune City map until the Saboteur is gone, then checks what was
// destroyed. Garrisons are put in with the real capture command (CMD_INFANTRY_CAPTURE) and taken out
// with the real Evacuate command (CMD_ZONE_EVACUATE).
//
// city zones skipped     the nearest enemy Residential, Commercial and Industrial zones hold no garrison:
//                        the Saboteur goes past them to the Wind Trap beyond; all three zones stand
// rule off               the same position with the rule off: the nearest zone is blown up (the rule up
//                        to protocol 149), so the switch is what makes the difference
// garrisoned zone        the nearest zone holds two enemy Soldiers: it is the target and is blown up;
//                        each Soldier takes 10% of the building's actual 200 HP loss (20 HP), so
//                        both die through the ordinary damage path, not silently on replacement
// garrison leaves        the garrisoned zone it is walking to is evacuated: it gives the zone up and
//                        blows up the Wind Trap instead; the zone stands
// garrison decides      a fully built, populated Residential zone without a garrison stands nearest and
//                        an unbuilt one with no residents but a Soldier inside further off: the
//                        Saboteur passes the full one and blows up the garrisoned one
// only ungarrisoned     nothing but ungarrisoned zones: it does not go for them and waits; they stand
// player order           (human owner) the player selects the Saboteur and right-clicks an ungarrisoned
//                        enemy zone through Game::doInput: the order is kept and the zone is blown up
//
// Every case runs for a human and a QuantBot owner (the player order for the human only).
#include <Command.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <structures/Palace.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <units/Saboteur.h>
#include <units/UnitBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace SaboteurZoneProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("SABOTEUR_ZONE_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

constexpr int kMapSize = 64;
SettingsClass::GameOptionsClass gModOptions;

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    data += "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
    return data;
}

/// Ordos (team 1, OWNERCLASS) owns a Palace at (30,30); Harkonnen (team 2, human) is the enemy.
struct Fixture {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    House* enemy = nullptr;
    Palace* palace = nullptr;
    std::string label;
    std::vector<std::pair<std::string, Uint32>> watched;

    Fixture(const std::string& ownerClass, const std::string& caseName, bool rule = true)
        : label(ownerClass + "/" + caseName) {
        auto options = gModOptions;
        options.saboteurSkipsCityZones = rule;
        options.immortalHumanPlayer = false;
        const bool humanOwner = ownerClass == "HumanPlayer";
        // The local player is the Saboteur's owner in the human cases and its victim in the AI cases.
        settings.general.playerName = humanOwner ? "Owner" : "Enemy";
        GameInitSettings init("saboteur-zone.ini", fixtureMap(), "Saboteur zone", true, options);
        init.randomSeed = 29;
        GameInitSettings::HouseInfo own(HOUSE_ORDOS, 1);
        own.addPlayerInfo({"Owner", ownerClass});
        init.addHouseInfo(own);
        GameInitSettings::HouseInfo foe(HOUSE_HARKONNEN, 2);
        foe.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(foe);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;   // Private fixture, never a public match-analytics sample.
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        owner = game->getHouse(HOUSE_ORDOS);
        enemy = game->getHouse(HOUSE_HARKONNEN);
        require(owner != nullptr && enemy != nullptr, label + ": fixture houses missing");
        require(owner->isAI() == !humanOwner, label + ": owner is not the requested kind of player");
        require(game->isCitySimEnabled(), label + ": the fixture is not a city game");
        require(game->getGameInitSettings().getGameOptions().saboteurSkipsCityZones == rule,
                label + ": the game did not keep the Saboteurs Skip City Zones rule of its arm");
        require(game->getGameInitSettings().getGameOptions().garrisoningAndCapturing,
                label + ": Garrisoning and Capturing is off");
        palace = static_cast<Palace*>(owner->placeStructure(NONE_ID, Structure_Palace, 30, 30, true, true));
        require(palace != nullptr, label + ": cannot place the Palace");
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    ZoneStructure* zone(const std::string& name, int item, int x, int y, int level = 2) {
        auto* z = dynamic_cast<ZoneStructure*>(enemy->placeStructure(NONE_ID, item, x, y, true, true));
        require(z != nullptr, label + ": cannot place " + name);
        z->setStartingLevel(level);
        z->setVisible(VIS_ALL, true);
        watched.push_back({name, z->getObjectID()});
        return z;
    }

    const StructureBase* building(const std::string& name, int item, int x, int y) {
        auto* s = enemy->placeStructure(NONE_ID, item, x, y, true, true);
        require(s != nullptr, label + ": cannot place " + name);
        s->setVisible(VIS_ALL, true);
        watched.push_back({name, s->getObjectID()});
        return s;
    }

    Uint8 playerOf(const House* house) const { return house->getPlayerList().front()->getPlayerID(); }

    /// COUNT enemy Soldiers walk into Z with the real capture command.
    std::vector<Uint32> garrison(ZoneStructure* z, int count) {
        std::vector<Uint32> ids;
        for(int i = 0; i < count; ++i) {
            auto* soldier = enemy->createUnit(Unit_Soldier, true);
            require(soldier != nullptr, label + ": cannot create a Soldier");
            soldier->deploy(Coord(z->getLocation().x - 1, z->getLocation().y + i));
            soldier->doSetAttackMode(GUARD);
            Command(playerOf(enemy), CMD_INFANTRY_CAPTURE, soldier->getObjectID(), z->getObjectID()).executeCommand();
            ids.push_back(soldier->getObjectID());
        }
        require(runUntil(30, [&] { return z->getGarrisonSize() == count; }),
                label + ": the enemy Soldiers did not go into the zone (" + std::to_string(z->getGarrisonSize()) + ")");
        return ids;
    }

    UnitBase* release() {
        for(int i = 0; i < 5; ++i) game->updateGameState();
        palace->specialWeaponTimer = 0;
        palace->doSpecialWeapon();
        UnitBase* saboteur = nullptr;
        for(auto* unit : unitList)
            if(unit->getItemID() == Unit_Saboteur && unit->getOwner() == owner) saboteur = unit;
        require(saboteur != nullptr, label + ": the Palace released no Saboteur");
        require(saboteur->getAttackMode() == HUNT, label + ": the released Saboteur is not hunting");
        return saboteur;
    }

    bool alive(Uint32 id) const { return game->getObjectManager().getObject(id) != nullptr; }

    template<class F> bool runUntil(int seconds, F done) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) {
            if(done()) return true;
            game->updateGameState();
        }
        return done();
    }

    /// Runs until the Saboteur is gone; returns the game seconds used.
    int runUntilGone(Uint32 saboteurID, int seconds) {
        const Uint32 start = game->getGameCycleCount();
        runUntil(seconds, [&] { return !alive(saboteurID); });
        return static_cast<int>((game->getGameCycleCount() - start) * GAMESPEED_DEFAULT / 1000);
    }

    /// Exactly VICTIM of the watched objects is gone, and the Saboteur with it.
    void requireOnlyVictim(Uint32 saboteurID, Uint32 victimID) {
        require(!alive(saboteurID), label + ": the Saboteur never reached a target");
        for(const auto& [name, id] : watched) {
            const bool shouldDie = id == victimID;
            require(alive(id) != shouldDie, label + ": " + name + (shouldDie ? " survived" : " was destroyed instead"));
        }
    }
};

void record(const std::string& label, const std::string& detail) {
    SDL_Log("SABOTEUR_ZONE_CASE: %s %s", label.c_str(), detail.c_str());
}

int targetItem(const UnitBase* u) { return u->getTarget() != nullptr ? u->getTarget()->getItemID() : -1; }

/// Ungarrisoned Residential, Commercial and Industrial zones stand between the Palace and a Wind Trap: the
/// Saboteur goes past them to the Wind Trap. With the rule off it blows up the nearest zone.
void cityZonesSkipped(const std::string& ownerClass, bool rule) {
    Fixture f(ownerClass, rule ? "city-zones-skipped" : "rule-off", rule);
    const auto* residential = f.zone("Residential zone 4 tiles east", Structure_ZoneResidential, 35, 30);
    f.zone("Commercial zone 5 tiles south", Structure_ZoneCommercial, 30, 36);
    f.zone("Industrial zone 6 tiles west", Structure_ZoneIndustrial, 22, 30);
    const auto* trap = f.building("Wind Trap 13 tiles north", Structure_WindTrap, 30, 15);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    const ObjectBase* expected = rule ? static_cast<const ObjectBase*>(trap) : residential;
    require(saboteur->getTarget() == expected,
            f.label + ": the released Saboteur's target is item " + std::to_string(targetItem(saboteur)) + ", not "
            + (rule ? "the Wind Trap beyond the ungarrisoned zones" : "the nearest (ungarrisoned) zone"));
    // On its way past the zones it must never switch to one of them.
    bool switched = false;
    const Uint32 victimID = expected->getObjectID();
    const Uint32 deadline = f.game->getGameCycleCount() + MILLI2CYCLES(120 * 1000);
    while(f.alive(saboteurID) && f.game->getGameCycleCount() < deadline) {
        f.game->updateGameState();
        if(!f.alive(saboteurID)) break;
        const ObjectBase* t = saboteur->getTarget();
        switched = switched || (t != nullptr && t->getObjectID() != victimID);
    }
    require(!switched, f.label + ": on its way the Saboteur switched its target");
    f.requireOnlyVictim(saboteurID, victimID);
    record(f.label, rule ? "went past three ungarrisoned enemy zones and blew up the Wind Trap; all zones stand"
                         : "rule off: blew up the nearest ungarrisoned zone (the rule up to protocol 149)");
}

/// A garrisoned zone is a target like any building: the nearest, garrisoned, is blown up before a
/// farther Wind Trap. Each 20-HP Soldier takes 10% of the actual 200-HP loss:
/// both die, independently of the charge's overkill.
void garrisonedZoneTaken(const std::string& ownerClass) {
    Fixture f(ownerClass, "garrisoned-zone");
    auto* z = f.zone("garrisoned Commercial zone 4 tiles east", Structure_ZoneCommercial, 35, 30);
    z->foundationDegrades = false;
    f.building("Wind Trap 13 tiles north", Structure_WindTrap, 30, 15);
    const auto soldiers = f.garrison(z, 2);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const Uint32 zoneID = z->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == z, f.label + ": the released Saboteur did not pick the garrisoned zone (target item "
            + std::to_string(targetItem(saboteur)) + ")");
    f.runUntilGone(saboteurID, 120);
    f.runUntil(1, [] { return false; });    // objects destroyed in the blast are removed next cycle
    f.requireOnlyVictim(saboteurID, zoneID);
    int killed = 0;
    for(const Uint32 id : soldiers) {
        const auto* soldier = f.game->getObjectManager().getObject(id);
        require(soldier == nullptr, f.label + ": a 20-HP Soldier survived its actual-loss 20-HP damage share");
        ++killed;
    }
    require(killed == 2, f.label + ": the destroyed zone did not account for both occupants");
    record(f.label, "picked the garrisoned zone and blew it up; both Soldiers killed by 20-HP actual-loss shares; Wind Trap stands");
}

/// Only the garrison decides, not how built up the zone is: a dense, populated Residential zone with
/// nobody inside is passed over for a farther, unbuilt one (no residents) that holds a Soldier.
void garrisonDecides(const std::string& ownerClass) {
    Fixture f(ownerClass, "garrison-decides");
    auto* full = f.zone("dense populated Residential zone 4 tiles east, no garrison", Structure_ZoneResidential, 35, 30, 3);
    auto* bare = f.zone("unbuilt Residential zone 7 tiles west, one Soldier inside", Structure_ZoneResidential, 22, 30, 0);
    f.building("Wind Trap 13 tiles north", Structure_WindTrap, 30, 15);
    f.garrison(bare, 1);
    require(full->getResidentialPopulation() > 0 && !full->isGarrisoned(),
            f.label + ": the near zone is not populated and ungarrisoned (" + std::to_string(full->getResidentialPopulation())
            + " residents)");
    require(bare->getResidentialPopulation() == 0 && bare->isGarrisoned(),
            f.label + ": the far zone is not unpopulated and garrisoned (" + std::to_string(bare->getResidentialPopulation())
            + " residents)");
    const Uint32 bareID = bare->getObjectID();
    const int residents = full->getResidentialPopulation();
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == bare, f.label + ": the released Saboteur did not pick the garrisoned unbuilt zone (target item "
            + std::to_string(targetItem(saboteur)) + ")");
    f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, bareID);
    record(f.label, "passed the populated zone (" + std::to_string(residents) + " residents, no garrison) and blew up the "
           "unbuilt zone with a Soldier inside");
}

/// The zone it is walking to loses its garrison (the enemy's Evacuate): it gives the zone up and goes
/// for the Wind Trap.
void garrisonLeaves(const std::string& ownerClass) {
    Fixture f(ownerClass, "garrison-leaves");
    auto* z = f.zone("garrisoned Residential zone 8 tiles east", Structure_ZoneResidential, 39, 30);
    const auto* trap = f.building("Wind Trap 13 tiles north", Structure_WindTrap, 30, 15);
    const Uint32 trapID = trap->getObjectID();
    f.garrison(z, 1);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == z, f.label + ": the released Saboteur did not pick the garrisoned zone (target item "
            + std::to_string(targetItem(saboteur)) + ")");
    f.runUntil(2, [] { return false; });
    require(f.alive(saboteurID) && saboteur->getTarget() == z, f.label + ": the Saboteur is not still on its way");
    Command(f.playerOf(f.enemy), CMD_ZONE_EVACUATE, z->getObjectID()).executeCommand();
    require(f.runUntil(2, [&] { return !z->isGarrisoned(); }), f.label + ": Evacuate did not empty the zone");
    require(f.runUntil(2, [&] { return saboteur->getTarget() == trap; }),
            f.label + ": the Saboteur kept going for the zone after its garrison left (target item "
            + std::to_string(targetItem(saboteur)) + ")");
    f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, trapID);
    record(f.label, "gave up the zone when its garrison left and blew up the Wind Trap; the zone stands");
}

/// Nothing but ungarrisoned zones: it does not go for them.
void onlyUngarrisonedZones(const std::string& ownerClass) {
    Fixture f(ownerClass, "only-ungarrisoned-zones");
    f.zone("Residential zone 4 tiles east", Structure_ZoneResidential, 35, 30);
    f.zone("Industrial zone 6 tiles west", Structure_ZoneIndustrial, 22, 30);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    bool targetedZone = false;
    const Uint32 end = f.game->getGameCycleCount() + MILLI2CYCLES(40 * 1000);
    while(f.game->getGameCycleCount() < end && f.alive(saboteurID)) {
        f.game->updateGameState();
        const ObjectBase* t = f.alive(saboteurID) ? saboteur->getTarget() : nullptr;
        targetedZone = targetedZone || t != nullptr;
    }
    require(f.alive(saboteurID) && !targetedZone, f.label + ": with only ungarrisoned zones near the Saboteur went for one");
    for(const auto& [name, id] : f.watched) require(f.alive(id), f.label + ": " + name + " was destroyed");
    record(f.label, "only ungarrisoned zones near: no zone taken as a target in 40 s, both stand");
}

/// A real SDL mouse click on TILE of the map view, through Game::doInput.
void clickMap(Fixture& f, Coord tile, Uint8 button) {
    currentGameMap->viewMap(f.owner->getHouseID(), tile, 6);
    screenborder->setNewScreenCenter(Coord(tile.x * TILESIZE + TILESIZE / 2, tile.y * TILESIZE + TILESIZE / 2));
    const int x = screenborder->world2screenX(tile.x * TILESIZE + TILESIZE / 2);
    const int y = screenborder->world2screenY(tile.y * TILESIZE + TILESIZE / 2);
    require(screenborder->isScreenCoordInsideMap(x, y), f.label + ": the click point is outside the map view");
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    require(SDL_PushEvent(&event) == 1, f.label + ": the mouse press was rejected");
    event.type = SDL_MOUSEBUTTONUP;
    require(SDL_PushEvent(&event) == 1, f.label + ": the mouse release was rejected");
    f.game->doInput();
}

/// The player's own order is kept: the Saboteur selected, the ungarrisoned zone right-clicked on the map.
void playerOrderKept() {
    Fixture f("HumanPlayer", "player-order");
    const auto* z = f.zone("ungarrisoned Industrial zone 8 tiles east", Structure_ZoneIndustrial, 39, 30);
    const Uint32 zoneID = z->getObjectID();
    f.building("Wind Trap 6 tiles north", Structure_WindTrap, 30, 22);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() != z, f.label + ": the released Saboteur picked the ungarrisoned zone itself");
    Game& game = *f.game;
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(saboteurID);
    saboteur->setSelected(true);
    game.selectionChanged();
    game.setCursorMode(Game::CursorMode_Normal);
    clickMap(f, z->getLocation(), SDL_BUTTON_RIGHT);
    require(f.runUntil(2, [&] { return saboteur->getTarget() == z; }),
            f.label + ": the right click on the ungarrisoned enemy zone did not send the Saboteur at it (target item "
            + std::to_string(targetItem(saboteur)) + ")");
    f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, zoneID);
    record(f.label, "right-clicked onto an ungarrisoned enemy zone: the order was kept and the zone blown up");
}

} // namespace SaboteurZoneProbe

int runSaboteurZoneProbe() {
    using namespace SaboteurZoneProbe;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    gModOptions.garrisoningAndCapturing = true; // These cases exercise garrisoned zones.
    require(gModOptions.saboteurSkipsCityZones, "Saboteurs Skip City Zones is not on by default in the Dune City mod");
    int failed = 0, cases = 0;
    auto run = [&](auto check) {
        ++cases;
        try {
            check();
        } catch(const std::exception&) {
            ++failed;   // require() logged SABOTEUR_ZONE_PROBE_FAIL with the reason.
            currentGame = nullptr;
        }
    };
    for(const std::string ownerClass : {"HumanPlayer", "qBotHard"}) {
        run([&] { cityZonesSkipped(ownerClass, true); });
        run([&] { cityZonesSkipped(ownerClass, false); });
        run([&] { garrisonedZoneTaken(ownerClass); });
        run([&] { garrisonDecides(ownerClass); });
        run([&] { garrisonLeaves(ownerClass); });
        run([&] { onlyUngarrisonedZones(ownerClass); });
    }
    run([] { playerOrderKept(); });
    if(failed == 0) SDL_Log("SABOTEUR_ZONE_PROBE_PASS: %d cases", cases);
    else SDL_Log("SABOTEUR_ZONE_PROBE_SUMMARY: %d of %d cases failed", failed, cases);
    return -1;
}
