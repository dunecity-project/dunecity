// Included only by run-hospital-care-probe.py, never by the shipped game.
//
// Hospitals heal infantry (dunecity/HospitalCare.h). Each check is a fresh city game on a bare
// rock map, played by Game::updateGameState() alone; orders go through the command manager as a
// player's click does (CMD_UNIT_SENDTOREPAIR, CMD_UNIT_MOVE2POS).
//
//   cap       seven wounded Soldiers sent to one Hospital: never more than five heal at once, the
//             others wait beside it, and all seven leave at full health, paid from the house
//   overflow  the same seven with a second Hospital: the ones that find no bed go there
//   lock      a Hospital keeps its overlay through the census while a soldier walks to it, waits,
//             heals, or is carried; it is released when the soldier is healed, killed or ordered
//             elsewhere, and the next census then makes it an ordinary zone again
//   carryall  a soldier 35 tiles away is flown by the house's Carryall and healed
//   auto      a wounded idle soldier of a human house with a Carryall goes by itself; an AI house's
//             (no QuantBot HospitalInfantry) does not; Hospital Care off sends nobody
//   save      saved with five healing, one waiting and one in a Carryall: the trips, beds and
//             overlays load as they were and the loaded game plays the next cycles exactly as the
//             uninterrupted one
//
// A Hospital is a Residential zone the census marked; the bare fixture has no population, so the
// census would clear it. keep() puts the fixture's Hospitals back after every cycle (as a city
// with people would keep them), except in the lock check, which runs the census itself.
#include <Command.h>
#include <CommandManager.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <structures/ZoneStructure.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/HospitalCare.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace HospitalCareProbe {

namespace HC = DuneCity::HospitalCare;
using DuneCity::HospitalTrip;

constexpr int kMapSize = 64;
std::string gOut;
SettingsClass::GameOptionsClass gModOptions;
std::ofstream gChecks;

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("HOSPITAL_CARE_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

void record(const std::string& check, const std::string& detail) {
    gChecks << check << ",\"" << detail << "\"\n";
    gChecks.flush();
    SDL_Log("HOSPITAL_CARE_PROBE_CHECK: %s %s", check.c_str(), detail.c_str());
}

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTimeOut=0\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    // MCVs keep the houses alive, far from everything.
    data += "[Player1]\nCredits=5000\nQuota=0\n[Player2]\nCredits=5000\nQuota=0\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Area Guard\n";
    return data;
}

/// Atreides is the human tester; Harkonnen an AI Player house (no QuantBot).
std::unique_ptr<Game> newGame(const std::string& mapData, int seed, bool hospitalCare) {
    auto options = gModOptions;
    options.hospitalCare = hospitalCare;
    options.immortalHumanPlayer = false;
    GameInitSettings init("hospital-care.ini", mapData, "Hospital care", true, options);
    init.randomSeed = seed;
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Hospital tester", "HumanPlayer"});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 2);
    ai.addPlayerInfo({"Hospital AI", "AIPlayer:hard"});
    init.addHouseInfo(ai);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->isCitySimEnabled(), "The fixture is not a city game");
    require(game->getGameInitSettings().getGameOptions().hospitalCare == hospitalCare,
            "The game did not keep its Hospital Care rule");
    require(game->getHouse(HOUSE_HARKONNEN)->isAI(), "The second house is not an AI house");
    return game;
}

DuneCity::HospitalCareState& care() { return currentGame->getCitySimulation()->hospitalCare(); }

ZoneStructure* zone(Uint32 id) {
    return dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
}

Uint32 placeHospital(House* house, Coord at) {
    auto* z = dynamic_cast<ZoneStructure*>(house->placeStructure(NONE_ID, Structure_ZoneResidential, at.x, at.y, true, true));
    require(z != nullptr, "Could not place a Residential zone");
    z->setVisible(VIS_ALL, true);
    z->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
    auto overlays = care().overlays();
    overlays.emplace_back(z->getObjectID(), static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital));
    care().setOverlays(overlays);
    return z->getObjectID();
}

/// The fixture's Hospitals stay Hospitals, as a populated city's would.
void keep(const std::vector<Uint32>& hospitals) {
    auto overlays = care().overlays();
    for(Uint32 id : hospitals) {
        auto* z = zone(id);
        if(!z) continue;
        z->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
        const auto entry = std::make_pair(id, static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital));
        if(std::find(overlays.begin(), overlays.end(), entry) == overlays.end()) overlays.push_back(entry);
    }
    care().setOverlays(overlays);
}

void step(Game& game, const std::vector<Uint32>& hospitals, int cycles = 1) {
    for(int i = 0; i < cycles; ++i) {
        game.updateGameState();
        keep(hospitals);
    }
}

GroundUnit* soldier(House* house, Coord at, int health, int item = Unit_Soldier) {
    auto* unit = dynamic_cast<GroundUnit*>(house->createUnit(item, true));
    require(unit != nullptr, "Could not create the soldier");
    unit->deploy(at);
    unit->setVisible(VIS_ALL, true);
    unit->setHealth(FixPoint(health));
    unit->doSetAttackMode(GUARD);
    return unit;
}

GroundUnit* unitByID(Uint32 id) {
    return dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(id));
}

void order(Game& game, Uint32 unitID) {
    game.getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_SENDTOREPAIR, unitID));
}

bool beside(const ZoneStructure* z, const Coord& at) {
    const Coord& o = z->getLocation();
    const bool ring = at.x >= o.x - 1 && at.x <= o.x + 2 && at.y >= o.y - 1 && at.y <= o.y + 2;
    const bool in = at.x >= o.x && at.x < o.x + 2 && at.y >= o.y && at.y < o.y + 2;
    return ring && !in;
}

std::string digest(const std::vector<Uint32>& units) {
    std::ostringstream out;
    out << "cycle=" << currentGame->getGameCycleCount();
    for(Uint32 id : units) {
        const auto* unit = unitByID(id);
        out << " | " << id << ':';
        if(!unit) { out << "gone"; continue; }
        out << unit->getHealth().getRawValue() << '@' << unit->getLocation().x << ',' << unit->getLocation().y
            << (unit->isPickedUp() ? " carried" : "");
        if(const auto* t = care().trip(id))
            out << " trip=" << t->hospitalID << '/' << int(t->stage) << '/' << t->spotX << ',' << t->spotY << '/' << t->carried;
    }
    out << " credits=" << currentGame->getHouse(HOUSE_ATREIDES)->getCredits();
    return out.str();
}

// ---------------------------------------------------------------------------------------------

void checkCap(const std::string& mapData, bool second) {
    auto game = newGame(mapData, 11, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
    if(second) hospitals.push_back(placeHospital(owner, Coord(34, 20)));
    std::vector<Uint32> units;
    for(int i = 0; i < 7; ++i) units.push_back(soldier(owner, Coord(23 + (i % 3), 25 + i / 3), 4)->getObjectID());
    const int before = owner->getCredits();
    for(Uint32 id : units) order(*game, id);
    step(*game, hospitals, 3);
    for(Uint32 id : units) require(care().trip(id) != nullptr, "A soldier ordered to repair has no Hospital trip");

    int most = 0, waited = 0;
    std::vector<int> peak(hospitals.size(), 0);
    bool everyoneDone = false;
    for(int cycle = 0; cycle < 6000 && !everyoneDone; ++cycle) {
        step(*game, hospitals);
        for(size_t h = 0; h < hospitals.size(); ++h) {
            const int healing = care().patients(hospitals[h]);
            require(healing <= DuneCity::kHospitalBeds, "A Hospital healed more than five soldiers at once");
            peak[h] = std::max(peak[h], healing);
            most = std::max(most, healing);
        }
        int waitingNow = 0;
        for(const auto& [id, t] : care().trips()) {
            if(t.stage == HospitalTrip::Stage::Waiting) {
                ++waitingNow;
                require(beside(zone(t.hospitalID), unitByID(id)->getLocation()), "A waiting soldier is not beside its Hospital");
            }
            if(t.stage == HospitalTrip::Stage::Healing)
                require(beside(zone(t.hospitalID), unitByID(id)->getLocation()), "A healing soldier is not beside its Hospital");
        }
        waited = std::max(waited, waitingNow);
        everyoneDone = care().trips().empty();
    }
    require(everyoneDone, "Not every soldier was discharged");
    for(Uint32 id : units) {
        const auto* unit = unitByID(id);
        require(unit && unit->getHealth() >= unit->getMaxHealth(), "A soldier left the Hospital without full health");
    }
    const int paid = before - owner->getCredits();
    // 7 soldiers x 16 hit points x 0.2 credits = 22.4 (credits are shown rounded).
    require(paid >= 21 && paid <= 24, "The house did not pay 0.2 credits a hit point");
    if(!second) {
        require(most == DuneCity::kHospitalBeds, "Five soldiers never healed together");
        require(waited >= 2, "No soldier waited for a bed");
    } else {
        require(peak[1] >= 1, "No soldier went on to the second Hospital");
    }
    for(Uint32 id : hospitals) require(!HC::isLockedHospital(zone(id)), "A Hospital stayed locked after its last patient");
    record(second ? "overflow" : "cap", "max_healing=" + std::to_string(most) + " peak_first=" + std::to_string(peak[0])
                       + (second ? " peak_second=" + std::to_string(peak[1]) : "") + " most_waiting=" + std::to_string(waited)
                       + " paid=" + std::to_string(paid) + " cycles=" + std::to_string(game->getGameCycleCount()));
    game.reset();
    currentGame = nullptr;
}

/// The census pass that designates civic overlays (runZoneGrowth), run now.
void census(Game& game) {
    game.getCitySimulation()->runZoneGrowth();
}

ZoneStructure::CivicOverlay overlayOf(Uint32 id) { return zone(id)->getCivicOverlay(); }

void checkLock(const std::string& mapData) {
    auto game = newGame(mapData, 12, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const Uint32 hospital = placeHospital(owner, Coord(20, 20));
    const std::vector<Uint32> none;

    // Without a claim the empty fixture's census clears the overlay: what the lock prevents.
    census(*game);
    require(overlayOf(hospital) == ZoneStructure::CivicOverlay::None, "The unclaimed fixture Hospital survived the census");
    zone(hospital)->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
    care().setOverlays({{hospital, 1}});

    // 1. Walking to it, then waiting/healing, then healed: locked until discharged.
    const Uint32 walker = soldier(owner, Coord(30, 30), 4)->getObjectID();
    order(*game, walker);
    step(*game, none, 2);
    require(care().trip(walker) && care().trip(walker)->stage == HospitalTrip::Stage::Travelling, "The walker has no trip");
    census(*game);
    require(HC::isLockedHospital(zone(hospital)) && overlayOf(hospital) == ZoneStructure::CivicOverlay::Hospital,
            "A Hospital lost its overlay while a soldier walked to it");
    bool sawHealing = false;
    for(int i = 0; i < 4000 && care().trip(walker); ++i) {
        step(*game, none);
        if(care().trip(walker) && care().trip(walker)->stage == HospitalTrip::Stage::Healing && !sawHealing) {
            sawHealing = true;
            census(*game);
            require(overlayOf(hospital) == ZoneStructure::CivicOverlay::Hospital, "A Hospital lost its overlay while healing");
        }
        require(overlayOf(hospital) == ZoneStructure::CivicOverlay::Hospital, "The game's own census retyped a locked Hospital");
    }
    require(sawHealing && !care().trip(walker), "The walker was not healed and discharged");
    census(*game);
    require(overlayOf(hospital) == ZoneStructure::CivicOverlay::None, "The Hospital stayed locked after the soldier was healed");
    record("lock_healed", "locked while walking and healing, released when healed");

    // 2. Ordered elsewhere: released.
    zone(hospital)->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
    care().setOverlays({{hospital, 1}});
    const Uint32 deserter = soldier(owner, Coord(32, 32), 4)->getObjectID();
    order(*game, deserter);
    step(*game, none, 30);
    require(care().trip(deserter) != nullptr, "The second soldier has no trip");
    census(*game);
    require(overlayOf(hospital) == ZoneStructure::CivicOverlay::Hospital, "Locked Hospital retyped");
    game->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS, deserter, 50u, 50u, 1u));
    step(*game, none, 3);
    require(care().trip(deserter) == nullptr, "A soldier ordered elsewhere kept its Hospital claim");
    census(*game);
    require(overlayOf(hospital) == ZoneStructure::CivicOverlay::None, "The Hospital stayed locked after a re-order");
    record("lock_reordered", "released by CMD_UNIT_MOVE2POS");

    // 3. Killed on the way: released.
    zone(hospital)->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
    care().setOverlays({{hospital, 1}});
    auto* victim = soldier(owner, Coord(40, 40), 4);
    const Uint32 victimID = victim->getObjectID();
    order(*game, victimID);
    step(*game, none, 30);
    require(care().trip(victimID) != nullptr, "The third soldier has no trip");
    victim->setHealth(0);
    step(*game, none, 3);
    require(care().trip(victimID) == nullptr, "A dead soldier kept its Hospital claim");
    census(*game);
    require(overlayOf(hospital) == ZoneStructure::CivicOverlay::None, "The Hospital stayed locked after its soldier died");
    record("lock_killed", "released by the soldier's death");
    game.reset();
    currentGame = nullptr;
}

Carryall* parkCarryall(House* owner, Coord at) {
    auto* carryall = dynamic_cast<Carryall*>(owner->createUnit(Unit_Carryall, true));
    require(carryall != nullptr, "Could not create the Carryall");
    carryall->deploy(at);
    carryall->setVisible(VIS_ALL, true);
    carryall->setGuardPoint(at.x, at.y);
    return carryall;
}

void checkCarryall(const std::string& mapData) {
    auto game = newGame(mapData, 13, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(10, 10))};
    parkCarryall(owner, Coord(12, 14));
    const Uint32 id = soldier(owner, Coord(45, 45), 6, Unit_Trooper)->getObjectID();
    order(*game, id);
    bool carried = false;
    int liftedAt = -1;
    for(int i = 0; i < 8000 && (care().trip(id) || !carried); ++i) {
        step(*game, hospitals);
        const auto* unit = unitByID(id);
        require(unit != nullptr, "The Trooper died on the flight");
        if(unit->isPickedUp() && !carried) { carried = true; liftedAt = i; }
        if(unit->isPickedUp()) require(HC::isLockedHospital(zone(hospitals[0])), "A Hospital was not locked while its patient flew");
    }
    const auto* unit = unitByID(id);
    require(carried, "No Carryall picked the soldier up");
    require(!care().trip(id) && unit->getHealth() >= unit->getMaxHealth(), "The flown soldier was not healed");
    require(beside(zone(hospitals[0]), unit->getLocation()), "The flown soldier was not healed beside the Hospital");
    record("carryall", "lifted_at_cycle=" + std::to_string(liftedAt) + " healed_at_cycle=" + std::to_string(game->getGameCycleCount()));
    game.reset();
    currentGame = nullptr;
}

void checkAuto(const std::string& mapData) {
    for(const bool rule : {true, false}) {
        auto game = newGame(mapData, 14, rule);
        House* human = game->getHouse(HOUSE_ATREIDES);
        House* ai = game->getHouse(HOUSE_HARKONNEN);
        std::vector<Uint32> hospitals{placeHospital(human, Coord(10, 10)), placeHospital(ai, Coord(50, 50))};
        parkCarryall(human, Coord(12, 14));
        parkCarryall(ai, Coord(50, 46));
        const Uint32 mine = soldier(human, Coord(20, 20), 8)->getObjectID();
        const Uint32 theirs = soldier(ai, Coord(42, 42), 8)->getObjectID();
        const Uint32 healthy = soldier(human, Coord(22, 22), 15)->getObjectID();   // 75%: stays
        step(*game, hospitals, DuneCity::kHospitalRecheckCycles + 2);
        require((care().trip(mine) != nullptr) == rule, rule ? "A wounded idle soldier did not go to the Hospital by itself"
                                                              : "Hospital Care off still sent a soldier");
        require(!care().trip(theirs), "An AI Player house's soldier went although no QuantBot asked for it");
        require(!care().trip(healthy), "A soldier above half health went by itself");
        if(!rule) {
            order(*game, mine);
            step(*game, hospitals, 3);
            require(!care().trip(mine), "Hospital Care off accepted a repair order for a Hospital");
        }
        record(rule ? "auto_on" : "auto_off", rule ? "human soldier at 40% went, AI Player's and the 75% one stayed"
                                                   : "rule off: no automatic trip and no ordered trip");
        game.reset();
        currentGame = nullptr;
    }
}

void checkSave(const std::string& mapData) {
    auto game = newGame(mapData, 15, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
    parkCarryall(owner, Coord(24, 18));
    std::vector<Uint32> units;
    for(int i = 0; i < 6; ++i) units.push_back(soldier(owner, Coord(23 + (i % 3), 24 + i / 3), 3)->getObjectID());
    units.push_back(soldier(owner, Coord(50, 50), 3, Unit_Trooper)->getObjectID());
    for(Uint32 id : units) order(*game, id);
    // Until five heal, one waits and the far Trooper is in the air.
    bool ready = false;
    for(int i = 0; i < 4000 && !ready; ++i) {
        step(*game, hospitals);
        int waiting = 0;
        for(const auto& [id, t] : care().trips()) waiting += t.stage == HospitalTrip::Stage::Waiting;
        const auto* trooper = unitByID(units.back());
        ready = care().patients(hospitals[0]) == DuneCity::kHospitalBeds && waiting >= 1 && trooper && trooper->isPickedUp();
    }
    require(ready, "The save fixture never had five healing, one waiting and one carried");
    const auto savedTrips = care().trips();
    const auto savedOverlays = care().overlays();
    const std::string before = digest(units);
    const std::string path = gOut + "/hospital-care.dls";
    require(game->saveGame(path), "Could not write the save");
    constexpr int kAfter = 600;
    step(*game, hospitals, kAfter);
    const std::string expected = digest(units);
    game.reset();
    currentGame = nullptr;

    game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    require(game->getGameInitSettings().getGameOptions().hospitalCare, "The save lost the Hospital Care rule");
    require(care().trips() == savedTrips, "The trips did not load as they were saved");
    require(care().overlays() == savedOverlays, "The census overlays did not load as they were saved");
    require(digest(units) == before, "The soldiers did not load as they were saved");
    require(HC::isLockedHospital(zone(hospitals[0])), "The loaded Hospital is not locked");
    step(*game, hospitals, 1);
    require(overlayOf(hospitals[0]) == ZoneStructure::CivicOverlay::Hospital, "The loaded zone is not drawn as a Hospital");
    step(*game, hospitals, kAfter - 1);
    const std::string actual = digest(units);
    if(actual != expected) {
        SDL_Log("HOSPITAL_CARE_PROBE_EXPECTED: %s", expected.c_str());
        SDL_Log("HOSPITAL_CARE_PROBE_ACTUAL: %s", actual.c_str());
    }
    require(actual == expected, "The loaded game did not play the next cycles as the uninterrupted one");
    record("save", "trips=" + std::to_string(savedTrips.size()) + " overlays=" + std::to_string(savedOverlays.size())
                       + " identical_after_cycles=" + std::to_string(kAfter));
    game.reset();
    currentGame = nullptr;
}

} // namespace HospitalCareProbe

int runHospitalCareProbe() {
    using namespace HospitalCareProbe;
    const char* outEnv = std::getenv("HOSPITAL_CARE_OUT");
    require(outEnv != nullptr, "Missing HOSPITAL_CARE_OUT");
    gOut = outEnv;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "Hospital tester";
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(gModOptions.hospitalCare, "Hospital Care is not on by default");
    const std::string mapData = fixtureMap();
    const char* only = std::getenv("HOSPITAL_CARE_CASE");
    auto wanted = [&](const char* name) { return !only || !*only || std::string(only) == name; };

    gChecks.open(gOut + "/checks.csv");
    gChecks << "check,detail\n";
    if(wanted("cap")) checkCap(mapData, false);
    if(wanted("overflow")) checkCap(mapData, true);
    if(wanted("lock")) checkLock(mapData);
    if(wanted("carryall")) checkCarryall(mapData);
    if(wanted("auto")) checkAuto(mapData);
    if(wanted("save")) checkSave(mapData);
    gChecks.close();
    SDL_Log("HOSPITAL_CARE_PROBE_PASS");
    return -1;
}
