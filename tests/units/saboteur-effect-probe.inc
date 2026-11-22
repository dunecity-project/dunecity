// How much does the Saboteur vehicle hunt (GameOptionsClass::saboteurVehicleHunt) change what a
// Palace-released Saboteur achieves? One binary plays every trial twice, rule off and rule on.
//
// A trial is one release from the real Palace special weapon against a seeded enemy position: a
// base of buildings with gun and rocket turrets, a few Troopers and five vehicles, in one of four
// layouts. The enemy is a human house nobody plays: its units keep their default orders and
// shoot a Saboteur they can see. The trial ends when the Saboteur is gone, when it has stood
// without a target for 20 game seconds, or after 150 game seconds.
// tests/units/run-saboteur-effect-probe.py turns the SABOTEUR_EFFECT_TRIAL lines into the report.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <mod/ModManager.h>
#include <structures/Palace.h>
#include <structures/StructureBase.h>
#include <units/Saboteur.h>
#include <units/UnitBase.h>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace SaboteurEffectProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error("SABOTEUR_EFFECT_PROBE_FAIL: " + why);
}

/// The layout's own generator: a trial's position depends on (layout, seed) only, never on the
/// rule or the owner, so both arms and both owners face the same enemy.
struct Rng {
    Uint32 state;
    Uint32 next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int range(int low, int high) { return low + static_cast<int>(next() % static_cast<Uint32>(high - low + 1)); }
};

struct Victim {
    int itemID;
    bool vehicle;
    int price;
};

constexpr const char* layoutNames[] = {"base-in-front", "vehicles-forward", "vehicles-in-base", "scattered"};
constexpr int kLayouts = 4;

inline bool freeArea(int x, int y, int w, int h) {
    for(int dy = 0; dy < h; ++dy)
        for(int dx = 0; dx < w; ++dx) {
            if(!currentGameMap->tileExists(x + dx, y + dy)) return false;
            if(currentGameMap->getTile(x + dx, y + dy)->hasAGroundObject()) return false;
        }
    return true;
}

inline void trial(const std::string& mod, bool ruleOn, const std::string& ownerClass, int layout, int seed) {
    const bool humanOwner = ownerClass == "HumanPlayer";
    const std::string label = std::string(ruleOn ? "on" : "off") + "/" + ownerClass + "/" + layoutNames[layout]
        + "/" + std::to_string(seed);
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=64\n";
    for(int y = 0; y < 64; ++y) map << std::setw(3) << std::setfill('0') << y << '=' << std::string(64, '%') << '\n';
    map << "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
    settings.general.playerName = humanOwner ? "Owner" : "Enemy";
    auto options = settings.gameOptions;
    options.saboteurVehicleHunt = ruleOn;
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("saboteur-effect.ini", map.str(), false, options);
    init.randomSeed = 1000 + seed;
    GameInitSettings::HouseInfo own(HOUSE_ORDOS, 1);
    own.addPlayerInfo({"Owner", ownerClass});
    init.addHouseInfo(own);
    GameInitSettings::HouseInfo foe(HOUSE_HARKONNEN, 2);
    foe.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(foe);
    game->initGame(init);
    game->initializeGameLoop();
    require(game->getGameInitSettings().getGameOptions().saboteurVehicleHunt == ruleOn, label + ": the rule did not reach the match");
    auto* owner = game->getHouse(HOUSE_ORDOS);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    require(owner != nullptr && enemy != nullptr, label + ": fixture houses missing");
    auto* palace = static_cast<Palace*>(owner->placeStructure(NONE_ID, Structure_Palace, 3, 30, true, true));
    require(palace != nullptr, label + ": cannot place the Palace");

    Rng rng{static_cast<Uint32>(layout * 7919 + seed * 104729 + 17)};
    std::map<Uint32, Victim> victims;
    auto price = [&](const ObjectBase* object) {
        return currentGame->objectData.data[object->getItemID()][object->getProductionHouseID()].price;
    };
    auto placeStructure = [&](int itemID, int x, int y) {
        const auto* structure = enemy->placeStructure(NONE_ID, itemID, x, y, true, true);
        if(structure != nullptr) victims[structure->getObjectID()] = {itemID, false, price(structure)};
    };
    auto placeUnit = [&](int itemID, int xLow, int xHigh, int yLow, int yHigh) {
        for(int attempt = 0; attempt < 200; ++attempt) {
            const int x = rng.range(xLow, xHigh), y = rng.range(yLow, yHigh);
            if(!freeArea(x, y, 1, 1)) continue;
            auto* unit = enemy->createUnit(itemID, true);
            require(unit != nullptr, label + ": cannot create an enemy unit");
            unit->deploy(Coord(x, y));
            victims[unit->getObjectID()] = {itemID, unit->isAGroundUnit() && !unit->isInfantry(), price(unit)};
            return;
        }
        require(false, label + ": no free tile for an enemy unit");
    };

    // The base: eight buildings in a 4x4 grid of 4x3-tile lots, turrets among them.
    const int structures[] = {Structure_ConstructionYard, Structure_WindTrap, Structure_Refinery, Structure_Barracks,
                              Structure_GunTurret, Structure_RocketTurret, Structure_HeavyFactory, Structure_Silo};
    const int vehicles[] = {Unit_Tank, Unit_Quad, Unit_Trike, Unit_Harvester, Unit_Launcher, Unit_SiegeTank};
    const int baseX = layout == 0 ? 28 : layout == 1 ? 48 : 40;
    const int baseY = rng.range(20, 42);
    if(layout == 3) {
        for(int itemID : structures)
            for(int attempt = 0; attempt < 200; ++attempt) {
                const int x = rng.range(18, 58), y = rng.range(6, 55);
                if(!freeArea(x - 1, y - 1, 5, 4)) continue;
                placeStructure(itemID, x, y);
                break;
            }
    } else {
        int lots[16];
        for(int i = 0; i < 16; ++i) lots[i] = i;
        for(int i = 15; i > 0; --i) std::swap(lots[i], lots[rng.range(0, i)]);
        for(int i = 0; i < 8; ++i)
            placeStructure(structures[i], baseX - 8 + 4 * (lots[i] % 4), baseY - 6 + 3 * (lots[i] / 4));
    }
    for(int i = 0; i < 3; ++i) placeUnit(Unit_Trooper, baseX - 9, baseX + 9, baseY - 7, baseY + 7);
    for(int i = 0; i < 5; ++i) {
        const int itemID = vehicles[rng.range(0, 5)];
        switch(layout) {
            case 0: placeUnit(itemID, baseX + 12, 60, std::max(3, baseY - 12), std::min(60, baseY + 12)); break;
            case 1: placeUnit(itemID, 20, 34, std::max(3, baseY - 12), std::min(60, baseY + 12)); break;
            case 2: placeUnit(itemID, baseX - 11, baseX + 10, baseY - 8, baseY + 8); break;
            default: placeUnit(itemID, 20, 60, 4, 59); break;
        }
    }
    int vehicleCount = 0, structureCount = 0;
    for(const auto& entry : victims) {
        vehicleCount += entry.second.vehicle;
        structureCount += currentGame->getObjectManager().getObject(entry.first)->isAStructure();
    }

    for(int i = 0; i < 5; ++i) game->updateGameState();
    palace->specialWeaponTimer = 0;
    palace->doSpecialWeapon();
    UnitBase* saboteur = nullptr;
    for(auto* unit : unitList)
        if(unit->getItemID() == Unit_Saboteur && unit->getOwner() == owner) saboteur = unit;
    require(saboteur != nullptr, label + ": the Palace released no Saboteur");
    const Uint32 saboteurID = saboteur->getObjectID();

    auto alive = [&](Uint32 objectID) { return game->getObjectManager().getObject(objectID) != nullptr; };
    const Uint32 start = game->getGameCycleCount();
    const Uint32 deadline = start + MILLI2CYCLES(150 * 1000);
    const Uint32 idleLimit = MILLI2CYCLES(20 * 1000);
    Uint32 idleSince = start;
    std::string outcome = "enroute";
    const Victim* hit = nullptr;
    while(game->getGameCycleCount() < deadline) {
        game->updateGameState();
        if(!alive(saboteurID)) {
            // Whatever enemy object went with it this cycle is what it blew up.
            for(const auto& entry : victims)
                if(!alive(entry.first)) hit = &entry.second;
            outcome = hit == nullptr ? "died" : hit->vehicle ? "vehicle" : "structure";
            break;
        }
        // Nothing else on the map attacks the enemy: an object lost while the Saboteur lives is a fixture fault.
        for(const auto& entry : victims)
            require(alive(entry.first), label + ": an enemy object was lost without the Saboteur");
        if(saboteur->getTarget() != nullptr || saboteur->moving) idleSince = game->getGameCycleCount();
        if(game->getGameCycleCount() - idleSince >= idleLimit) {
            outcome = "idle";
            break;
        }
    }
    SDL_Log("SABOTEUR_EFFECT_TRIAL: mod=%s rule=%s owner=%s layout=%s seed=%d outcome=%s item=%d price=%d seconds=%d vehicles=%d structures=%d",
            mod.c_str(), ruleOn ? "on" : "off", ownerClass.c_str(), layoutNames[layout], seed, outcome.c_str(),
            hit ? hit->itemID : -1, hit ? hit->price : 0,
            static_cast<int>((game->getGameCycleCount() - start) * GAMESPEED_DEFAULT / 1000), vehicleCount, structureCount);
    game.reset();
    currentGame = nullptr;
}

} // namespace SaboteurEffectProbe

int runSaboteurEffectProbe() {
    using namespace SaboteurEffectProbe;
    const std::string mod = std::getenv("SABOTEUR_EFFECT_PROBE_MOD");
    const int seeds = std::atoi(std::getenv("SABOTEUR_EFFECT_PROBE_SEEDS"));
    require(seeds > 0, "SABOTEUR_EFFECT_PROBE_SEEDS must be positive");
    require(ModManager::instance().setActiveMod(mod), "missing fixture mod " + mod);
    int trials = 0;
    for(const char* ownerClass : {"HumanPlayer", "qBotHard"})
        for(int layout = 0; layout < kLayouts; ++layout)
            for(int seed = 1; seed <= seeds; ++seed)
                for(bool ruleOn : {false, true}) {
                    trial(mod, ruleOn, ownerClass, layout, seed);
                    ++trials;
                }
    SDL_Log("SABOTEUR_EFFECT_PROBE_PASS: mod=%s trials=%d", mod.c_str(), trials);
    return -1;
}
