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
//  - the rule off (and Atreides with it on): the Palace calls Fremen warriors as before.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <structures/Palace.h>
#include <structures/StructureBase.h>
#include <units/SandWorm.h>
#include <units/PalaceWormRules.h>
#include <units/UnitBase.h>
#include <cstdlib>
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

    Fixture(const std::string& ownerClass, const std::string& caseName, bool rule = true,
            HOUSETYPE ownerHouse = HOUSE_FREMEN) : label(ownerClass + "/" + caseName) {
        std::ostringstream map;
        map << "[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=64\n";
        for(int y = 0; y < 64; ++y) {
            map << std::setw(3) << std::setfill('0') << y << '=';
            for(int x = 0; x < 64; ++x) map << (rockTile(x, y) ? '%' : '-');
            map << '\n';
        }
        map << "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
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
        require(game->getGameInitSettings().getGameOptions().fremenPalaceWorm == rule, label + ": the rule did not reach the match");
        palace = static_cast<Palace*>(owner->placeStructure(NONE_ID, Structure_Palace, 10, 30, true, true));
        require(palace != nullptr, label + ": cannot place the Palace");
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    UnitBase* unit(House* house, int itemID, int x, int y, ATTACKMODE mode = STOP) {
        auto* created = house->createUnit(itemID, true);
        require(created != nullptr, label + ": cannot create unit " + std::to_string(itemID));
        created->deploy(Coord(x, y));
        created->doSetAttackMode(mode);
        return created;
    }

    Sandworm* worm() const { return Palace::findPalaceWorm(owner); }

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
                if(w->isActive()) require(!rockTile(w->getLocation().x, w->getLocation().y),
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
    Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS, wormID, 40, 20, 1).executeCommand();
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
    Command(pLocalPlayer->getPlayerID(), CMD_SANDWORM_SPICEBLOOM, wormID, spot.x, spot.y).executeCommand();
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
    SDL_Log("PALACE_WORM_PROBE_PASS: mod=%s control, hunt (human and QuantBot), bloom, life, QuantBot bloom, save/load, warriors", mod.c_str());
    return -1;
}
