// The Fremen Palace worm (match rule Fremen Palace Worm; include/units/PalaceWormRules.h).
//
// Each case runs the real Palace special weapon and Game::updateGameState on a 64x64 map: rock in
// the west (x 0-23) where the Palace stands, sand in the east (x 24-63) with a rock island at
// x 44-47, y 40-43. Fremen (team 1) owns the Palace; Harkonnen (team 2) is the enemy.
//
//  - control: the worm surfaces on the sand nearest the Palace, belongs to Fremen, takes move
//    orders, never leaves the sand, is not attacked by its own side, and a second Palace charge
//    waits while it is out;
//  - hunt: in Hunt it eats the enemy units on its sand, never one on rock or one of its own side,
//    and the kills are credited to the Fremen house (Stats: Sandworm kills, the Tanks' kills);
//  - spice bloom: it bursts into a spice field where it was sent (a spot on rock moves to the
//    nearest sand within reach, one deep in the rock is refused), and is not counted as lost;
//  - life: it leaves after three minutes (bursting where it is if it was on its way to a bloom), or
//    sated after six meals, and none of these is a loss; beaten to half health it leaves, a loss;
//  - QuantBot: hunts an enemy on its sand, and with none bursts for spice near its refinery;
//  - save/load: a worm on its way to burst is still Fremen's, with its life and its order;
//  - the rule off (and Atreides with it on): the Palace calls Fremen warriors as before;
//  - the player's own path: the Palace panel's special button (hidden while charging, disabled
//    while the worm is out, the warriors' button for other houses and with the rule off), a click
//    that selects the worm, its Hunt button, the Attack cursor and a right click on an enemy unit,
//    the Spice Bloom button and cursor (right click cancels it, a spot deep in rock is refused),
//    all through Game::doInput with real SDL mouse events and the real panel widgets;
//  - QuantBot's PalaceWorm switch: the AI fires the Palace itself; level 1 hunts prey on its sand
//    and blooms without it, level 0 blooms near the refinery even with prey about, level 2 hunts
//    and never blooms.
#include <Game.h>
#include <GameInterface.h>
#include <ScreenBorder.h>
#include <GUI/ObjectInterfaces/PalaceInterface.h>
#include <GUI/ObjectInterfaces/UnitInterface.h>
#include <players/QuantBotConfig.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <structures/Palace.h>
#include <structures/StructureBase.h>
#include <units/SandWorm.h>
#include <units/PalaceWormRules.h>
#include <units/UnitBase.h>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace PalaceWormProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error("PALACE_WORM_PROBE_FAIL: " + why);
}

inline bool rockTile(int x, int y) { return x < 24 || (x >= 44 && x <= 47 && y >= 40 && y <= 43); }

struct Fixture {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    House* enemy = nullptr;
    Palace* palace = nullptr;
    std::string label;

    bool bundledMap = false;   ///< A bundled map (MAPTEXT) instead of the 64x64 fixture

    Fixture(const std::string& ownerClass, const std::string& caseName, bool rule = true,
            HOUSETYPE ownerHouse = HOUSE_FREMEN, const std::string& mapText = "")
        : label(ownerClass + "/" + caseName), bundledMap(!mapText.empty()) {
        std::ostringstream map;
        if(bundledMap) {
            map << mapText;
        } else {
            map << "[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=64\n";
            for(int y = 0; y < 64; ++y) {
                map << std::setw(3) << std::setfill('0') << y << '=';
                for(int x = 0; x < 64; ++x) map << (rockTile(x, y) ? '%' : '-');
                map << '\n';
            }
            map << "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
        }
        const bool humanOwner = ownerClass == "HumanPlayer";
        settings.general.playerName = humanOwner ? "Owner" : "Enemy";
        settings.gameOptions.fremenPalaceWorm = rule;
        settings.gameOptions.killedSandwormsDropSpice = false;
        game = std::make_unique<Game>();
        currentGame = game.get();
        GameInitSettings init("palace-worm.ini", map.str(), false, settings.gameOptions);
        init.randomSeed = 73;
        GameInitSettings::HouseInfo own(ownerHouse, 1);
        own.addPlayerInfo({"Owner", ownerClass});
        init.addHouseInfo(own);
        GameInitSettings::HouseInfo foe(HOUSE_HARKONNEN, 2);
        foe.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(foe);
        game->initGame(init);
        game->initializeGameLoop();
        owner = game->getHouse(ownerHouse);
        enemy = game->getHouse(HOUSE_HARKONNEN);
        require(owner != nullptr && enemy != nullptr, label + ": fixture houses missing");
        require(owner->isAI() == !humanOwner, label + ": owner is not the requested kind of player");
        if(humanOwner) {
            // The enemy is a second HumanPlayer only so that it stands still, and a local game makes
            // every HumanPlayer it loads the local one (the last wins). A skirmish has one human: the
            // owner, as it is when Stefan plays Fremen.
            for(const auto& player : owner->getPlayerList())
                if(auto* human = dynamic_cast<HumanPlayer*>(player.get())) { pLocalHouse = owner; pLocalPlayer = human; }
            require(pLocalHouse == owner && pLocalPlayer != nullptr, label + ": the owner is not the local player");
        }
        require(game->getGameInitSettings().getGameOptions().fremenPalaceWorm == rule, label + ": the rule did not reach the match");
        const Coord site = bundledMap ? palaceSiteNearOwnArmy() : Coord(10, 30);
        palace = static_cast<Palace*>(owner->placeStructure(NONE_ID, Structure_Palace, site.x, site.y, true, true));
        require(palace != nullptr, label + ": cannot place the Palace");
        // run-palace-worm-probe.py checks the PALACE_WORM log lines of each case between its markers.
        SDL_Log("PALACE_WORM_CASE_BEGIN: %s", label.c_str());
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    bool onRock(const Coord& tile) const {
        return bundledMap ? currentGameMap->getTile(tile)->isRock() : rockTile(tile.x, tile.y);
    }

    /// On a bundled map: the free 3x3 of plain rock nearest the owner's starting units, as a player
    /// would build it beside them.
    Coord palaceSiteNearOwnArmy() const {
        Coord home = Coord::Invalid();
        for(const auto* u : unitList) if(u->getOwner() == owner) { home = u->getLocation(); break; }
        require(home.isValid(), label + ": the owner has no starting units on the map");
        for(int r = 2; r < 40; ++r)
            for(int y = home.y - r; y <= home.y + r; ++y)
                for(int x = home.x - r; x <= home.x + r; ++x) {
                    if(std::max(std::abs(x - home.x), std::abs(y - home.y)) != r) continue;
                    bool free = true;
                    for(int dy = 0; dy < 3 && free; ++dy)
                        for(int dx = 0; dx < 3 && free; ++dx) {
                            if(!currentGameMap->tileExists(x + dx, y + dy)) { free = false; break; }
                            const Tile* t = currentGameMap->getTile(x + dx, y + dy);
                            free = t->getType() == Terrain_Rock && !t->hasAnObject();
                        }
                    if(free) return Coord(x, y);
                }
        require(false, label + ": no free rock for a Palace near the owner's army");
        return Coord::Invalid();
    }

    /// The sand tile in the worm's sand region nearest FROM that is at least MIN tiles from it and free.
    Coord sandNear(const Sandworm* worm, const Coord& from, int min) const {
        const Uint32 region = currentGameMap->getTile(worm->getLocation())->getSandRegion();
        for(int r = min; r < 60; ++r)
            for(int y = from.y - r; y <= from.y + r; ++y)
                for(int x = from.x - r; x <= from.x + r; ++x) {
                    if(std::max(std::abs(x - from.x), std::abs(y - from.y)) != r || !currentGameMap->tileExists(x, y)) continue;
                    const Tile* t = currentGameMap->getTile(x, y);
                    if(t->getType() == Terrain_Sand && t->getSandRegion() == region && !t->hasAnObject()) return Coord(x, y);
                }
        require(false, label + ": no free sand in the worm's region");
        return Coord::Invalid();
    }

    UnitBase* unit(House* house, int itemID, int x, int y, ATTACKMODE mode = STOP) {
        auto* created = house->createUnit(itemID, true);
        require(created != nullptr, label + ": cannot create unit " + std::to_string(itemID));
        created->deploy(Coord(x, y));
        created->doSetAttackMode(mode);
        return created;
    }

    Sandworm* worm() const { return Palace::findPalaceWorm(owner); }

    /// The Fremen player's id, as its commands carry it (the local player may be the enemy).
    Uint8 ownerPlayerID() const { return owner->getPlayerList().front()->getPlayerID(); }

    /// The real special weapon: Palace::doSpecialWeapon() -> Palace::summonPalaceWorm().
    Sandworm* release() {
        for(int i = 0; i < 5; ++i) game->updateGameState();
        palace->specialWeaponTimer = 0;
        palace->doSpecialWeapon();
        Sandworm* summoned = worm();
        require(summoned != nullptr, label + ": the Palace called no worm");
        return summoned;
    }

    bool alive(Uint32 objectID) const { return game->getObjectManager().getObject(objectID) != nullptr; }

    /// Runs until DONE() or SECONDS of game time; requires the worm never to stand on rock.
    template<class Done> int runUntil(Uint32 wormID, int seconds, Done done) {
        const Uint32 start = game->getGameCycleCount();
        const Uint32 deadline = start + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < deadline && !done()) {
            game->updateGameState();
            if(const auto* w = dynamic_cast<const Sandworm*>(game->getObjectManager().getObject(wormID)))
                if(w->isActive()) require(!onRock(w->getLocation()),
                                          label + ": the worm went onto rock at " + std::to_string(w->getLocation().x)
                                          + "," + std::to_string(w->getLocation().y));
        }
        return static_cast<int>((game->getGameCycleCount() - start) * GAMESPEED_DEFAULT / 1000);
    }

    int spiceTilesAround(const Coord& centre, int radius) const {
        int count = 0;
        for(int y = centre.y - radius; y <= centre.y + radius; ++y)
            for(int x = centre.x - radius; x <= centre.x + radius; ++x)
                if(currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->isSpice()) ++count;
        return count;
    }
};

/// It surfaces on the sand nearest the Palace, is Fremen's, moves where it is sent and never onto rock.
inline void control() {
    Fixture f("HumanPlayer", "control");
    auto* own = f.unit(f.owner, Unit_Trooper, 22, 31, GUARD);
    auto* foe = f.unit(f.enemy, Unit_Trooper, 20, 25, GUARD);
    Sandworm* worm = f.release();
    const Uint32 wormID = worm->getObjectID();
    require(worm->getOwner() == f.owner && worm->isPalaceWorm() && !worm->isWildSandworm(),
            f.label + ": the worm is not the Fremen house's own");
    require(worm->getLocation() == Coord(24, 31), f.label + ": the worm did not surface on the sand nearest the Palace ("
            + std::to_string(worm->getLocation().x) + "," + std::to_string(worm->getLocation().y) + ")");
    require(worm->isRespondable() && worm->getAttackMode() == GUARD, f.label + ": the worm does not wait for orders in Guard");
    require(worm->getPalaceWormCyclesLeft() == MILLI2CYCLES(PalaceWorm::kLifetimeMs), f.label + ": wrong life");
    require(!f.palace->isSpecialWeaponReady(), f.label + ": the Palace did not start charging again");
    require(!own->canAttack(worm), f.label + ": a Fremen Trooper may attack its own worm");
    require(!worm->canAttack(own), f.label + ": the worm may eat its own side");

    // A second charge waits while the worm is out.
    f.palace->specialWeaponTimer = 0;
    f.palace->doSpecialWeapon();
    int worms = 0;
    for(const auto* u : unitList) if(u->getItemID() == Unit_Sandworm && u->getOwner() == f.owner) ++worms;
    require(worms == PalaceWorm::kMaxPerHouse && f.palace->isSpecialWeaponReady(),
            f.label + ": a second Palace charge called another worm or was used up");

    // A move order through the network command, as a player sends it.
    Command(f.ownerPlayerID(), CMD_UNIT_MOVE2POS, wormID, 40, 20, 1).executeCommand();
    int seconds = f.runUntil(wormID, 60, [&] { return worm->getLocation() == Coord(40, 20); });
    require(worm->getLocation() == Coord(40, 20), f.label + ": the worm did not reach its move order");
    SDL_Log("PALACE_WORM_CASE: %s surfaced at 24,31 beside the Palace and reached 40,20 in %d s", f.label.c_str(), seconds);

    // Onto the rock island: it stops at the sand's edge.
    worm->doMove2Pos(45, 41, true);
    f.runUntil(wormID, 30, [] { return false; });
    require(blockDistance(worm->getLocation(), Coord(45, 41)) <= 3, f.label + ": the worm did not come up to the rock island");
    require(foe->canAttack(worm) == worm->isVisible(f.enemy->getTeamID()), f.label + ": the enemy may not attack the worm it sees");
    SDL_Log("PALACE_WORM_CASE: %s ordered onto rock, stopped on the sand at %d,%d", f.label.c_str(),
            worm->getLocation().x, worm->getLocation().y);
}

/// In Hunt it eats the enemy on its sand, not the one on rock and not its own side; credited to Fremen.
inline void hunt(const std::string& ownerClass) {
    Fixture f(ownerClass, "hunt");
    const auto* tankA = f.unit(f.enemy, Unit_Tank, 40, 30);
    const auto* tankB = f.unit(f.enemy, Unit_Tank, 43, 34);
    const auto* onRock = f.unit(f.enemy, Unit_Tank, 21, 34);
    const auto* friendQuad = f.unit(f.owner, Unit_Quad, 41, 33);
    Sandworm* worm = f.release();
    const Uint32 wormID = worm->getObjectID();
    require(!worm->canAttack(onRock), f.label + ": the worm may go for a Tank on rock");
    if(ownerClass == "HumanPlayer") worm->doSetAttackMode(HUNT);
    // An AI house's worm comes out of the Palace already in Hunt (Palace::summonPalaceWorm); QuantBot's
    // own hunt orders are asserted by the ai-level cases.
    else require(worm->getAttackMode() == HUNT, f.label + ": the AI house's worm did not come out hunting");
    const Uint32 a = tankA->getObjectID(), b = tankB->getObjectID(), r = onRock->getObjectID(), q = friendQuad->getObjectID();
    const int seconds = f.runUntil(wormID, 120, [&] { return !f.alive(a) && !f.alive(b); });
    require(!f.alive(a) && !f.alive(b), f.label + ": the hunting worm did not eat both Tanks on the sand");
    require(f.alive(r), f.label + ": the Tank on rock was eaten");
    require(f.alive(q), f.label + ": the worm ate its own side's Quad");
    const auto& reward = f.owner->getCombatReward(Unit_Sandworm);
    require(reward.kills == 2 && f.owner->getNumKilledItems(Unit_Tank) == 2 && f.enemy->getNumLostItems(Unit_Tank) == 2,
            f.label + ": the meals were not credited to the Fremen house (Sandworm kills " + std::to_string(reward.kills)
            + ", Tank kills " + std::to_string(f.owner->getNumKilledItems(Unit_Tank)) + ")");
    SDL_Log("PALACE_WORM_CASE: %s ate both Tanks on the sand in %d s, left the one on rock and its own Quad; credited 2 kills, %lld hit points",
            f.label.c_str(), seconds, static_cast<long long>(reward.hpRemovedMilli / 1000));
}

/// The Spice Bloom order: it bursts where it was sent, and is not lost.
inline void spiceBloom() {
    Fixture f("HumanPlayer", "spice-bloom");
    Sandworm* worm = f.release();
    const Uint32 wormID = worm->getObjectID();
    // Aimed deep in the rock: refused, nothing changes.
    require(!worm->doSpiceBloom(5, 5) && !worm->hasBloomOrder(), f.label + ": a bloom deep in the rock was accepted");
    // Aimed at the rock island: moved to the nearest sand.
    require(worm->doSpiceBloom(45, 41) && !rockTile(worm->getBloomTarget().x, worm->getBloomTarget().y)
            && blockDistance(worm->getBloomTarget(), Coord(45, 41)) <= 2, f.label + ": a bloom aimed at rock did not move to the sand beside it");
    // A move order cancels it.
    worm->doMove2Pos(30, 30, true);
    require(!worm->hasBloomOrder(), f.label + ": a move order did not cancel the bloom");
    // The player's order, through the network command.
    const Coord spot(40, 22);
    require(f.spiceTilesAround(spot, PalaceWorm::kBloomRadius) == 0, f.label + ": the fixture already has spice");
    Command(f.ownerPlayerID(), CMD_SANDWORM_SPICEBLOOM, wormID, spot.x, spot.y).executeCommand();
    require(worm->getBloomTarget() == spot, f.label + ": the Spice Bloom command did not reach the worm");
    const int seconds = f.runUntil(wormID, 60, [&] { return !f.alive(wormID); });
    require(!f.alive(wormID), f.label + ": the worm never burst");
    const int spice = f.spiceTilesAround(spot, PalaceWorm::kBloomRadius);
    require(spice >= 40 && currentGameMap->getTile(spot)->isSpice(), f.label + ": the burst left " + std::to_string(spice) + " spice tiles");
    require(f.owner->getNumLostItems(Unit_Sandworm) == 0 && f.owner->getNumItems(Unit_Sandworm) == 0,
            f.label + ": a burst worm was counted as lost or is still counted");
    SDL_Log("PALACE_WORM_CASE: %s burst at 40,22 after %d s into %d spice tiles; not lost", f.label.c_str(), seconds, spice);
}

/// It leaves after its life or six meals (not lost); beaten to half health it leaves, lost.
inline void life() {
    {
        Fixture f("HumanPlayer", "expires");
        Sandworm* worm = f.release();
        const Uint32 wormID = worm->getObjectID();
        f.runUntil(wormID, PalaceWorm::kLifetimeMs / 1000 - 2, [] { return false; });
        require(f.alive(wormID), f.label + ": the worm left before its life was up");
        f.runUntil(wormID, 4, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.owner->getNumLostItems(Unit_Sandworm) == 0, f.label + ": the worm outlived its life or was lost");
        SDL_Log("PALACE_WORM_CASE: %s left after %d s, not lost", f.label.c_str(), PalaceWorm::kLifetimeMs / 1000);
    }
    {
        // Its time runs out on the way to burst: it bursts where it is.
        Fixture f("HumanPlayer", "expires-on-the-way");
        Sandworm* worm = f.release();
        const Uint32 wormID = worm->getObjectID();
        require(worm->doSpiceBloom(60, 60), f.label + ": bloom order refused");
        worm->palaceWormCyclesLeft = MILLI2CYCLES(2000);
        for(int i = 0; i < MILLI2CYCLES(1000); ++i) f.game->updateGameState();
        const Coord near = worm->getLocation();
        f.runUntil(wormID, 3, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.spiceTilesAround(near, PalaceWorm::kBloomRadius + 2) >= 30
                && f.owner->getNumLostItems(Unit_Sandworm) == 0,
                f.label + ": a worm out of time on its way to burst did not burst where it was");
        SDL_Log("PALACE_WORM_CASE: %s ran out of time on the way and burst near %d,%d", f.label.c_str(), near.x, near.y);
    }
    {
        Fixture f("HumanPlayer", "sated");
        std::vector<Uint32> prey;
        for(int i = 0; i < PalaceWorm::kMeals + 1; ++i) prey.push_back(f.unit(f.enemy, Unit_Trike, 30 + 3*i, 50)->getObjectID());
        Sandworm* worm = f.release();
        const Uint32 wormID = worm->getObjectID();
        worm->doSetAttackMode(HUNT);
        f.runUntil(wormID, 150, [&] { return !f.alive(wormID); });
        int eaten = 0;
        for(Uint32 id : prey) if(!f.alive(id)) ++eaten;
        require(!f.alive(wormID) && eaten == PalaceWorm::kMeals && f.owner->getNumLostItems(Unit_Sandworm) == 0,
                f.label + ": the worm ate " + std::to_string(eaten) + " and did not leave sated");
        SDL_Log("PALACE_WORM_CASE: %s ate %d Trikes and left sated, the last one spared; not lost", f.label.c_str(), eaten);
    }
    {
        Fixture f("HumanPlayer", "beaten");
        const auto* shooter = f.unit(f.enemy, Unit_Tank, 20, 31);
        Sandworm* worm = f.release();
        const Uint32 wormID = worm->getObjectID();
        worm->handleDamage(worm->getMaxHealth() / 2 + 1, shooter->getObjectID(), f.enemy);
        f.runUntil(wormID, 2, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.owner->getNumLostItems(Unit_Sandworm) == 1, f.label + ": a beaten worm stayed or was not lost");
        SDL_Log("PALACE_WORM_CASE: %s beaten to half health, left, counted as lost", f.label.c_str());
    }
}

/// QuantBot: with no enemy on its sand it bursts for spice near its refinery.
inline void quantBotBloomsNearRefinery() {
    Fixture f("qBotHard", "bloom-near-refinery");
    const auto* refinery = f.owner->placeStructure(NONE_ID, Structure_Refinery, 19, 10, true, true);
    require(refinery != nullptr, f.label + ": cannot place the Refinery");
    Sandworm* worm = f.release();
    const Uint32 wormID = worm->getObjectID();
    const int seconds = f.runUntil(wormID, 90, [&] { return !f.alive(wormID); });
    require(!f.alive(wormID), f.label + ": the QuantBot worm never burst");
    const Coord refineryCentre = refinery->getCenterPoint() / TILESIZE;
    const int spice = f.spiceTilesAround(refineryCentre, 14 + PalaceWorm::kBloomRadius);
    require(spice >= 30, f.label + ": the burst was not near the refinery (" + std::to_string(spice) + " spice tiles within 18)");
    require(f.owner->getNumLostItems(Unit_Sandworm) == 0, f.label + ": the burst worm counted as lost");
    SDL_Log("PALACE_WORM_CASE: %s burst for spice beside its Refinery after %d s: %d spice tiles", f.label.c_str(), seconds, spice);
}

/// A worm on its way to burst survives the full engine save and load: Fremen's, its life and its order.
inline void saveLoad(const std::string& savePath) {
    Fixture f("HumanPlayer", "save-load");
    Sandworm* worm = f.release();
    const Uint32 wormID = worm->getObjectID();
    const Coord spot(50, 52);
    require(worm->doSpiceBloom(spot.x, spot.y), f.label + ": bloom order refused");
    for(int i = 0; i < MILLI2CYCLES(2000); ++i) f.game->updateGameState();
    const Sint32 cyclesLeft = worm->getPalaceWormCyclesLeft();
    const Coord where = worm->getLocation();
    require(f.game->saveGame(savePath), f.label + ": cannot save");
    f.game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    f.game = std::make_unique<Game>();
    currentGame = f.game.get();
    f.game->initGame(GameInitSettings(savePath));
    f.game->initializeGameLoop();
    f.owner = f.game->getHouse(HOUSE_FREMEN);
    worm = dynamic_cast<Sandworm*>(f.game->getObjectManager().getObject(wormID));
    require(worm != nullptr && worm->isPalaceWorm() && worm->getOwner() == f.owner && worm->isRespondable()
            && worm->getPalaceWormCyclesLeft() == cyclesLeft && worm->getBloomTarget() == spot
            && worm->getLocation() == where,
            f.label + ": the worm's owner, life, order or place did not survive save and load");
    require(f.owner->getNumItems(Unit_Sandworm) == 1, f.label + ": the loaded house does not count its worm");
    f.runUntil(wormID, 60, [&] { return !f.alive(wormID); });
    require(!f.alive(wormID) && currentGameMap->getTile(spot)->isSpice(), f.label + ": the loaded worm did not burst at its spot");
    SDL_Log("PALACE_WORM_CASE: %s loaded with %d cycles left and its bloom order at 50,52, then burst there",
            f.label.c_str(), cyclesLeft);
}

/// The rule off, and Atreides with it on: the Palace calls Fremen warriors.
inline void warriors(bool rule, HOUSETYPE house) {
    Fixture f("HumanPlayer", std::string(rule ? "rule-on" : "rule-off") + "-warriors-" + std::to_string(house), rule, house);
    for(int i = 0; i < 5; ++i) f.game->updateGameState();
    f.palace->specialWeaponTimer = 0;
    f.palace->doSpecialWeapon();
    int troopers = 0;
    for(const auto* u : unitList) if(u->getItemID() == Unit_Trooper && u->getOwner() == f.owner) ++troopers;
    require(troopers > 0 && f.worm() == nullptr, f.label + ": the Palace did not call Fremen warriors");
    SDL_Log("PALACE_WORM_CASE: %s called %d Fremen warriors, no worm", f.label.c_str(), troopers);
}


// --- The player's own path -------------------------------------------------------------------

int queued(Game& game, CMDTYPE type) {
    int count = 0;
    for(const auto& [cycle, slot] : game.cmdManager.timeslot)
        for(const auto& command : slot) if(command.getCommandID() == type) ++count;
    return count;
}

/// A real left-button press and release on the widget, as the mouse sends it.
void press(Widget& button) {
    button.handleMouseLeft(5, 5, true);
    button.handleMouseLeft(5, 5, false);
}

/// A real SDL mouse click on TILE of the map view, through Game::doInput.
void clickMap(Fixture& f, Coord tile, Uint8 button = SDL_BUTTON_LEFT) {
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

/// Selects the object on TILE with a real left click and returns the side panel the player sees.
template<class Panel> Panel* selectByClick(Fixture& f, Coord tile, Uint32 objectID, const std::string& what) {
    Game& game = *f.game;
    require(game.pInterface != nullptr, f.label + ": the game has no interface");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.setCursorMode(Game::CursorMode_Normal);
    clickMap(f, tile);
    require(game.selectedList.size() == 1 && game.selectedList.count(objectID) == 1,
            f.label + ": a click on " + what + " did not select it (" + std::to_string(game.selectedList.size()) + " selected)");
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<Panel*>(game.pInterface->pObjectContainer);
    require(panel != nullptr && panel->update(), f.label + ": selecting " + what + " did not open its panel");
    return panel;
}

void runSeconds(Fixture& f, int seconds) {
    for(int i = 0; i < MILLI2CYCLES(seconds * 1000); ++i) f.game->updateGameState();
}

/// The Palace panel's special button, pressed as the player presses it, once charged. Returns the
/// worm it called (nullptr when the rule gives warriors). Asserts the button through its states.
Sandworm* pressPalaceSpecial(Fixture& f, bool expectWorm) {
    Game& game = *f.game;
    runSeconds(f, 1);
    f.palace->specialWeaponTimer = f.palace->getMaxSpecialWeaponTimer();
    auto* panel = selectByClick<PalaceInterface>(f, f.palace->getLocation() + Coord(1, 1), f.palace->getObjectID(), "the Palace");
    require(!panel->weaponSelectButton.isVisible(), f.label + ": the special button is shown while the Palace charges");
    SDL_Texture* worm = pGFXManager->getSmallDetailPic(Picture_Sandworm);
    require((panel->weaponProgressBar.pBackground.get() == worm) == expectWorm,
            f.label + ": the charging picture is " + std::string(expectWorm ? "not" : "") + " the sandworm");
    f.palace->specialWeaponTimer = 0;
    runSeconds(f, 1);
    panel->update();
    require(panel->weaponSelectButton.isVisible() && panel->weaponSelectButton.isEnabled(),
            f.label + ": the charged special button is not shown and enabled");
    const bool saysWorm = panel->weaponSelectButton.getTooltipText().find("sandworm") != std::string::npos;
    require(saysWorm == expectWorm, f.label + ": the special button's tooltip " + std::string(expectWorm ? "does not name" : "names")
            + " the sandworm: '" + panel->weaponSelectButton.getTooltipText() + "'");
    const int before = queued(game, CMD_PALACE_SPECIALWEAPON);
    press(panel->weaponSelectButton);
    require(queued(game, CMD_PALACE_SPECIALWEAPON) == before + 1, f.label + ": the special button sent no command");
    for(int i = 0; i < MILLI2CYCLES(3000) && f.palace->isSpecialWeaponReady(); ++i) game.updateGameState();
    require(!f.palace->isSpecialWeaponReady(), f.label + ": the Palace did not use its charge");
    panel->update();
    require(!panel->weaponSelectButton.isVisible(), f.label + ": the special button is still shown after use");
    Sandworm* summoned = f.worm();
    require((summoned != nullptr) == expectWorm, f.label + ": the special button " + std::string(expectWorm ? "called no" : "called a") + " worm");
    return summoned;
}

/// The Palace button calls the worm; a click selects it; its Hunt button makes it eat the enemy.
inline void playerHuntButton() {
    Fixture f("HumanPlayer", "player-hunt-button");
    require(pLocalHouse == f.owner, f.label + ": the player is not the Fremen house");
    const Uint32 tank = f.unit(f.enemy, Unit_Tank, 40, 30)->getObjectID();
    const auto* rock = f.unit(f.enemy, Unit_Tank, 21, 36);
    Sandworm* worm = pressPalaceSpecial(f, true);
    const Uint32 wormID = worm->getObjectID();
    auto* panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    require(panel->huntButton.isVisible() && panel->huntButton.isEnabled(), f.label + ": the worm's Hunt button is not shown");
    require(panel->spiceBloomButton.isVisible() && panel->spiceBloomButton.isEnabled(), f.label + ": the worm has no Spice Bloom button");
    require(panel->moveButton.isVisible() && panel->attackButton.isVisible(), f.label + ": the worm has no Move or Attack button");
    require(!panel->huntButton.getToggleState(), f.label + ": the new worm already shows Hunt");
    const int before = queued(*f.game, CMD_UNIT_SETMODE);
    press(panel->huntButton);
    require(queued(*f.game, CMD_UNIT_SETMODE) == before + 1, f.label + ": the Hunt button sent no command");
    f.runUntil(wormID, 1, [] { return false; });
    require(worm->getAttackMode() == HUNT, f.label + ": the Hunt command did not reach the worm");
    panel->update();
    require(panel->huntButton.getToggleState(), f.label + ": the Hunt button does not show the worm hunts");
    const int seconds = f.runUntil(wormID, 120, [&] { return !f.alive(tank); });
    require(!f.alive(tank), f.label + ": the hunting worm did not eat the Tank on the sand");
    require(f.alive(rock->getObjectID()), f.label + ": the hunting worm ate the Tank on rock");
    require(f.owner->getNumKilledItems(Unit_Tank) == 1 && f.owner->getCombatReward(Unit_Sandworm).kills == 1,
            f.label + ": the meal was not credited to the Fremen house");
    SDL_Log("PALACE_WORM_CASE: %s Palace button -> worm, click-selected, Hunt button -> ate the Tank in %d s, spared the one on rock",
            f.label.c_str(), seconds);
}

/// The Attack button and cursor on an enemy unit, then a right click on another: both eaten.
inline void playerAttackClicks() {
    Fixture f("HumanPlayer", "player-attack-click");
    const auto* first = f.unit(f.enemy, Unit_Tank, 36, 26);
    const auto* second = f.unit(f.enemy, Unit_Quad, 50, 20);
    Sandworm* worm = pressPalaceSpecial(f, true);
    const Uint32 wormID = worm->getObjectID(), a = first->getObjectID(), b = second->getObjectID();
    auto* panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->attackButton);
    require(f.game->currentCursorMode == Game::CursorMode_Attack, f.label + ": the Attack button did not open its cursor");
    int before = queued(*f.game, CMD_UNIT_ATTACKOBJECT);
    clickMap(f, first->getLocation());
    require(queued(*f.game, CMD_UNIT_ATTACKOBJECT) == before + 1, f.label + ": the Attack click on the Tank sent no attack order");
    require(f.game->currentCursorMode == Game::CursorMode_Normal, f.label + ": the Attack cursor stayed open");
    int seconds = f.runUntil(wormID, 90, [&] { return !f.alive(a); });
    require(!f.alive(a), f.label + ": the worm did not eat the Tank it was sent at");
    require(f.alive(b) && worm->getAttackMode() != HUNT, f.label + ": the worm went hunting on its own");
    // A right click on the Quad, the default order.
    selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm again");
    before = queued(*f.game, CMD_UNIT_ATTACKOBJECT);
    clickMap(f, second->getLocation(), SDL_BUTTON_RIGHT);
    require(queued(*f.game, CMD_UNIT_ATTACKOBJECT) == before + 1, f.label + ": a right click on the Quad sent no attack order");
    const int more = f.runUntil(wormID, 90, [&] { return !f.alive(b); });
    require(!f.alive(b), f.label + ": the worm did not eat the Quad it was right-clicked onto");
    require(f.owner->getCombatReward(Unit_Sandworm).kills == 2, f.label + ": the two meals were not credited");
    SDL_Log("PALACE_WORM_CASE: %s Attack cursor on a Tank: eaten in %d s; right click on a Quad: eaten in %d s",
            f.label.c_str(), seconds, more);
}

/// The Spice Bloom button and cursor: right click cancels, deep rock is refused, a spot on the
/// sand gets the spice field.
inline void playerSpiceBloom() {
    Fixture f("HumanPlayer", "player-spice-bloom");
    const auto* trooper = f.unit(f.owner, Unit_Trooper, 22, 34, GUARD);
    Sandworm* worm = pressPalaceSpecial(f, true);
    const Uint32 wormID = worm->getObjectID();
    // A Fremen Trooper has no Spice Bloom button, and its selection cannot take the cursor.
    auto* troopPanel = selectByClick<UnitInterface>(f, trooper->getLocation(), trooper->getObjectID(), "the Trooper");
    require(!troopPanel->spiceBloomButton.isVisible(), f.label + ": a Trooper shows the Spice Bloom button");
    f.game->setCursorMode(Game::CursorMode_SpiceBloom);
    require(f.game->currentCursorMode == Game::CursorMode_Normal, f.label + ": a Trooper selection took the Spice Bloom cursor");

    auto* panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    require(panel->spiceBloomButton.getTooltipText().find("Spice Bloom") != std::string::npos,
            f.label + ": the Spice Bloom button's tooltip does not say Spice Bloom");
    press(panel->spiceBloomButton);
    require(f.game->currentCursorMode == Game::CursorMode_SpiceBloom, f.label + ": the Spice Bloom button did not open its cursor");
    panel->update();
    require(panel->spiceBloomButton.getToggleState(), f.label + ": the Spice Bloom button does not show it is active");
    // Right click: the cursor closes, nothing is ordered.
    int before = queued(*f.game, CMD_SANDWORM_SPICEBLOOM);
    clickMap(f, Coord(40, 22), SDL_BUTTON_RIGHT);
    require(f.game->currentCursorMode == Game::CursorMode_Normal && queued(*f.game, CMD_SANDWORM_SPICEBLOOM) == before,
            f.label + ": a right click did not cancel the Spice Bloom cursor");
    // Deep in the rock: the order goes out, the worm refuses it.
    panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->spiceBloomButton);
    clickMap(f, Coord(5, 5));
    require(queued(*f.game, CMD_SANDWORM_SPICEBLOOM) == before + 1, f.label + ": the click on rock sent no Spice Bloom order");
    f.runUntil(wormID, 1, [] { return false; });
    require(f.alive(wormID) && !worm->hasBloomOrder(), f.label + ": a bloom deep in the rock was taken");
    // On the sand.
    const Coord spot(40, 22);
    require(f.spiceTilesAround(spot, PalaceWorm::kBloomRadius) == 0, f.label + ": the fixture already has spice");
    panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->spiceBloomButton);
    before = queued(*f.game, CMD_SANDWORM_SPICEBLOOM);
    clickMap(f, spot);
    require(queued(*f.game, CMD_SANDWORM_SPICEBLOOM) == before + 1, f.label + ": the Spice Bloom click sent no order");
    require(f.game->currentCursorMode == Game::CursorMode_Normal, f.label + ": the Spice Bloom cursor stayed open after the click");
    f.runUntil(wormID, 1, [] { return false; });
    require(worm->getBloomTarget() == spot, f.label + ": the Spice Bloom order did not reach the worm");
    const int seconds = f.runUntil(wormID, 60, [&] { return !f.alive(wormID); });
    require(!f.alive(wormID), f.label + ": the worm never burst");
    const int spice = f.spiceTilesAround(spot, PalaceWorm::kBloomRadius);
    require(spice >= 40 && currentGameMap->getTile(spot)->isSpice(), f.label + ": the burst left " + std::to_string(spice) + " spice tiles");
    require(f.owner->getNumLostItems(Unit_Sandworm) == 0, f.label + ": the burst worm was counted as lost");
    SDL_Log("PALACE_WORM_CASE: %s button, cursor, right click cancels, rock refused, click at 40,22: burst in %d s, %d spice tiles",
            f.label.c_str(), seconds, spice);
}

/// A second Palace charged while the worm is out: its button is shown but disabled and sends nothing.
inline void playerWormStillOut() {
    Fixture f("HumanPlayer", "player-worm-still-out");
    auto* second = static_cast<Palace*>(f.owner->placeStructure(NONE_ID, Structure_Palace, 10, 20, true, true));
    require(second != nullptr, f.label + ": cannot place the second Palace");
    pressPalaceSpecial(f, true);
    second->specialWeaponTimer = 0;
    runSeconds(f, 1);
    auto* panel = selectByClick<PalaceInterface>(f, second->getLocation() + Coord(1, 1), second->getObjectID(), "the second Palace");
    require(panel->weaponSelectButton.isVisible() && !panel->weaponSelectButton.isEnabled(),
            f.label + ": the second Palace's button is not shown disabled while the worm is out");
    require(panel->weaponSelectButton.getTooltipText().find("still out") != std::string::npos,
            f.label + ": the disabled button does not say the worm is still out");
    const int before = queued(*f.game, CMD_PALACE_SPECIALWEAPON);
    press(panel->weaponSelectButton);
    require(queued(*f.game, CMD_PALACE_SPECIALWEAPON) == before, f.label + ": the disabled button sent a command");
    SDL_Log("PALACE_WORM_CASE: %s second Palace charged: button shown disabled, 'still out', sends nothing", f.label.c_str());
}

/// Atreides with the rule on, and Fremen with it off: the button shows and calls the warriors.
inline void playerWarriorsButton(bool rule, HOUSETYPE house) {
    Fixture f("HumanPlayer", std::string("player-warriors-") + (rule ? "rule-on-" : "rule-off-") + std::to_string(house), rule, house);
    pressPalaceSpecial(f, false);
    int troopers = 0;
    for(const auto* u : unitList) if(u->getItemID() == Unit_Trooper && u->getOwner() == f.owner) ++troopers;
    require(troopers > 0, f.label + ": the Palace button called no warriors");
    SDL_Log("PALACE_WORM_CASE: %s Palace button shows the warriors and called %d", f.label.c_str(), troopers);
}

// --- QuantBot's PalaceWorm switch ------------------------------------------------------------

/// The owner's QuantBot with PalaceWorm set to LEVEL.
void setPalaceWormLevel(Fixture& f, int level) {
    auto* bot = dynamic_cast<QuantBot*>(f.owner->getPlayerList().front().get());
    require(bot != nullptr, f.label + ": the owner is not a QuantBot");
    QuantBotConfig config = bot->botConfig();
    config.force.palaceWorm = level;
    bot->configOverride = std::make_shared<const QuantBotConfig>(config);
    require(bot->botConfig().force.palaceWorm == level, f.label + ": the PalaceWorm level did not take");
}

/// The AI fires its charged Palace on its own and the worm comes up.
Sandworm* aiFires(Fixture& f) {
    f.palace->specialWeaponTimer = 0;
    for(int i = 0; i < MILLI2CYCLES(30000) && f.worm() == nullptr; ++i) f.game->updateGameState();
    Sandworm* worm = f.worm();
    require(worm != nullptr, f.label + ": QuantBot did not fire its charged Palace within 30 s");
    return worm;
}

inline void quantBotLevel(int level) {
    Fixture f("qBotHard", "ai-level-" + std::to_string(level));
    setPalaceWormLevel(f, level);
    const auto* refinery = f.owner->placeStructure(NONE_ID, Structure_Refinery, 19, 10, true, true);
    require(refinery != nullptr, f.label + ": cannot place the Refinery");
    const Coord refineryCentre = refinery->getCenterPoint() / TILESIZE;
    const Uint32 a = f.unit(f.enemy, Unit_Tank, 44, 28)->getObjectID();
    const Uint32 b = f.unit(f.enemy, Unit_SiegeTank, 46, 30)->getObjectID();
    Sandworm* worm = aiFires(f);
    const Uint32 wormID = worm->getObjectID();
    if(level == 0) {
        // It bursts beside the refinery straight away, the Tanks left alone.
        f.runUntil(wormID, 3, [&] { return worm->hasBloomOrder(); });
        require(f.alive(wormID) && worm->hasBloomOrder(), f.label + ": level 0 gave the worm no bloom order");
        require(blockDistance(worm->getBloomTarget(), refineryCentre) <= 14 + 1, f.label + ": level 0's bloom site is not near the refinery");
        f.runUntil(wormID, 90, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.alive(a) && f.alive(b), f.label + ": the level 0 worm ate or never burst");
        require(f.spiceTilesAround(refineryCentre, 14 + PalaceWorm::kBloomRadius) >= 30, f.label + ": no spice near the refinery");
        SDL_Log("PALACE_WORM_CASE: %s burst beside the Refinery at once, both Tanks left", f.label.c_str());
        return;
    }
    // Levels 1 and 2 hunt both Tanks.
    const int seconds = f.runUntil(wormID, 120, [&] { return !f.alive(a) && !f.alive(b); });
    require(!f.alive(a) && !f.alive(b), f.label + ": the AI worm did not eat both Tanks on its sand");
    require(f.alive(wormID) && worm->getAttackMode() == HUNT && !worm->hasBloomOrder(), f.label + ": the worm left or was sent to bloom while prey was about");
    if(level == 1) {
        // No prey left: it bursts beside the refinery.
        f.runUntil(wormID, 90, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.spiceTilesAround(refineryCentre, 14 + PalaceWorm::kBloomRadius) >= 30,
                f.label + ": with the prey eaten the worm did not burst near the refinery");
        SDL_Log("PALACE_WORM_CASE: %s ate both Tanks in %d s, then burst beside the Refinery", f.label.c_str(), seconds);
    } else {
        // Hunts only: no bloom, it leaves when its life is up.
        f.runUntil(wormID, PalaceWorm::kLifetimeMs / 1000, [&] { return !f.alive(wormID); });
        require(!f.alive(wormID) && f.spiceTilesAround(refineryCentre, 14 + PalaceWorm::kBloomRadius) == 0,
                f.label + ": the hunt-only worm burst for spice");
        SDL_Log("PALACE_WORM_CASE: %s ate both Tanks in %d s, never burst, left at the end of its life", f.label.c_str(), seconds);
    }
}


/// Shot on its way to burst, in Hunt: it does not turn on the shooter, keeps its one bloom order
/// and bursts there (before the fix each hit cancelled the trip and the AI re-ordered it every pass).
inline void bloomUnderFire(const std::string& ownerClass) {
    Fixture f(ownerClass, "bloom-under-fire");
    const auto* shooter = f.unit(f.enemy, Unit_Tank, 34, 40);
    const Coord spot(52, 22);
    Sandworm* worm = ownerClass == "HumanPlayer" ? f.release() : aiFires(f);
    const Uint32 wormID = worm->getObjectID();
    if(ownerClass == "HumanPlayer") {
        worm->doSetAttackMode(HUNT);
        Command(f.ownerPlayerID(), CMD_SANDWORM_SPICEBLOOM, wormID, spot.x, spot.y).executeCommand();
    } else {
        setPalaceWormLevel(f, 0);
        f.runUntil(wormID, 3, [&] { return worm->hasBloomOrder(); });
    }
    require(worm->hasBloomOrder(), f.label + ": the worm got no bloom order");
    const Coord aim = worm->getBloomTarget();
    for(int hit = 0; hit < 6 && f.alive(wormID); ++hit) {
        worm->handleDamage(5, shooter->getObjectID(), f.enemy);
        require(worm->hasBloomOrder() && worm->getBloomTarget() == aim && worm->getTarget() == nullptr,
                f.label + ": a hit turned the worm off its bloom trip");
        f.runUntil(wormID, 2, [] { return false; });
    }
    f.runUntil(wormID, 90, [&] { return !f.alive(wormID); });
    require(!f.alive(wormID) && currentGameMap->getTile(aim)->isSpice() && f.alive(shooter->getObjectID()),
            f.label + ": the worm under fire did not burst at its spot");
    SDL_Log("PALACE_WORM_CASE: %s hit 6 times on its way, kept its bloom order and burst at %d,%d",
            f.label.c_str(), aim.x, aim.y);
}


/// The player's path on the bundled 4P Deserted map (Fremen is Player1 with its starting army, the
/// Harkonnen Player2): Palace button, click the worm, Guard and Move, Hunt eats an enemy Tank on the
/// sand, Spice Bloom bursts where clicked.
inline void playerOnBundledMap(const std::string& mapFile) {
    std::ifstream in(mapFile);
    require(in.good(), "cannot read the bundled map " + mapFile);
    std::stringstream text;
    text << in.rdbuf();
    Fixture f("HumanPlayer", "bundled-deserted", true, HOUSE_FREMEN, text.str());
    require(pLocalHouse == f.owner && currentGameMap->getSizeX() == 128, f.label + ": the bundled map did not load for the Fremen player");
    Sandworm* worm = pressPalaceSpecial(f, true);
    const Uint32 wormID = worm->getObjectID();
    require(!f.onRock(worm->getLocation()), f.label + ": the worm came up on rock");
    // Guard, then a Move order through the Move button and a map click.
    auto* panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->guardButton);
    f.runUntil(wormID, 1, [] { return false; });
    require(worm->getAttackMode() == GUARD, f.label + ": the Guard button did not reach the worm");
    const Coord moveTo = f.sandNear(worm, worm->getLocation(), 8);
    panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->moveButton);
    require(f.game->currentCursorMode == Game::CursorMode_Move, f.label + ": the Move button did not open its cursor");
    const int moves = queued(*f.game, CMD_UNIT_MOVE2POS);
    clickMap(f, moveTo);
    require(queued(*f.game, CMD_UNIT_MOVE2POS) == moves + 1, f.label + ": the Move click sent no order");
    int seconds = f.runUntil(wormID, 90, [&] { return blockDistance(worm->getLocation(), moveTo) <= 1; });
    require(blockDistance(worm->getLocation(), moveTo) <= 1, f.label + ": the worm did not reach the clicked spot "
            + std::to_string(moveTo.x) + "," + std::to_string(moveTo.y));
    SDL_Log("PALACE_WORM_CASE: %s Move click: reached %d,%d in %d s", f.label.c_str(), moveTo.x, moveTo.y, seconds);
    // Hunt: an enemy Tank on the sand nearby is eaten.
    const Coord prey = f.sandNear(worm, worm->getLocation(), 10);
    const Uint32 tank = f.unit(f.enemy, Unit_Tank, prey.x, prey.y, GUARD)->getObjectID();
    const int killsBefore = f.owner->getCombatReward(Unit_Sandworm).kills;
    panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->huntButton);
    seconds = f.runUntil(wormID, 120, [&] { return !f.alive(tank); });
    require(!f.alive(tank), f.label + ": the hunting worm did not eat the Tank at " + std::to_string(prey.x) + "," + std::to_string(prey.y));
    require(f.owner->getCombatReward(Unit_Sandworm).kills > killsBefore, f.label + ": the meal was not credited to Fremen");
    SDL_Log("PALACE_WORM_CASE: %s Hunt button: ate the Harkonnen Tank in %d s", f.label.c_str(), seconds);
    // Spice Bloom where the player clicks.
    press(selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm")->guardButton);
    f.runUntil(wormID, 1, [] { return false; });
    const Coord spot = f.sandNear(worm, worm->getLocation(), 6);
    panel = selectByClick<UnitInterface>(f, worm->getLocation(), wormID, "the worm");
    press(panel->spiceBloomButton);
    require(f.game->currentCursorMode == Game::CursorMode_SpiceBloom, f.label + ": the Spice Bloom button did not open its cursor");
    clickMap(f, spot);
    seconds = f.runUntil(wormID, 90, [&] { return !f.alive(wormID); });
    const int spice = f.spiceTilesAround(spot, PalaceWorm::kBloomRadius);
    require(!f.alive(wormID) && spice >= 30, f.label + ": the worm did not burst into spice at the clicked spot ("
            + std::to_string(spice) + " spice tiles)");
    SDL_Log("PALACE_WORM_CASE: %s Spice Bloom click at %d,%d: burst in %d s, %d spice tiles", f.label.c_str(), spot.x, spot.y, seconds, spice);
}

} // namespace PalaceWormProbe

int runPalaceWormProbe() {
    using namespace PalaceWormProbe;
    const std::string mod = std::getenv("PALACE_WORM_PROBE_MOD");
    const std::string savePath = std::getenv("PALACE_WORM_PROBE_SAVE");
    require(ModManager::instance().setActiveMod(mod), "missing fixture mod " + mod);
    control();
    hunt("HumanPlayer");
    hunt("qBotHard");
    spiceBloom();
    life();
    quantBotBloomsNearRefinery();
    saveLoad(savePath);
    warriors(false, HOUSE_FREMEN);
    warriors(true, HOUSE_ATREIDES);
    playerHuntButton();
    playerAttackClicks();
    playerSpiceBloom();
    playerWormStillOut();
    playerWarriorsButton(true, HOUSE_ATREIDES);
    playerWarriorsButton(false, HOUSE_FREMEN);
    quantBotLevel(1);
    quantBotLevel(0);
    quantBotLevel(2);
    bloomUnderFire("HumanPlayer");
    bloomUnderFire("qBotHard");
    playerOnBundledMap(std::getenv("PALACE_WORM_PROBE_BUNDLED_MAP"));
    SDL_Log("PALACE_WORM_PROBE_PASS: mod=%s control, hunt (human and QuantBot), bloom, life, QuantBot bloom, save/load, warriors, "
            "player path (Palace button, select, Hunt, Attack, right click, Spice Bloom, still out, warriors), QuantBot levels 1/0/2, bloom under fire, the player path on the bundled Deserted map", mod.c_str());
    return -1;
}
