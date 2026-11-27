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
// The player's own path (CLAUDE.md, "Player features: real assertion tests"): the soldier is
// selected as a click on it does, the real unit panel's button or the game's own map-click handler
// gives the order, and only game cycles run it.
//   ui_repair the Repair button: shown for a wounded Soldier with its tooltip, hidden at full
//             health; one click queues one command; the soldier walks to the Hospital (not the
//             nearer Repair Yard) and heals to full
//   walkin    right-clicking an own Hospital, the Move button then a click beside it, and a group
//             right-clicked beside it: every wounded soldier heals there. Not: a healthy soldier, a
//             wounded one moved four tiles away, an enemy Hospital, a Saboteur, the rule off
//   ui_no_hospital  no Hospital: the Repair button shows disabled with "Needs a Hospital"; neither
//             it, the R hotkey nor a right-click on the Repair Yard sends the soldier to the Repair Yard;
//             a Saboteur's button says it cannot be healed; a Hospital enables the button. Rule off:
//             the old order (the yard is booked) stands
//   ui_beds   seven wounded soldiers selected as a group, one click on the group panel's button:
//             never more than five heal, the others wait, the Hospital stays locked, all heal to full
//   ui_carryall the Repair button for a Trooper 35 tiles away: a Carryall flies it, it heals
//   metropolis  the bundled 4P Metropolis map (a played city of 2,478 zones), started through the
//             Custom Game lobby with the local human and three AI Players, played only by game cycles:
//             before the first census no Hospital, the Repair button disabled and no soldier sent to
//             its Repair Yard; the census then gives the house Hospitals that stay on their zones for
//             the whole run while the real city grows and declines; the Repair button heals a
//             wounded Trooper beside a Hospital; a Carryall flies one 6+ tiles away; seven soldiers
//             right-clicked onto one Hospital: five beds, the type kept while they come
//   pacing    a populated city (no fixture help) whose zones keep changing population: a Hospital
//             never moves; a house gains at most one Hospital a quarter year, loses a surplus one
//             only after four city years and at most one a year; Churches likewise. Patients sent
//             by right-click keep a Hospital that is due to go until they stop coming
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
#include <players/QuantBot.h>
#include <structures/RepairYard.h>
#include <structures/ZoneStructure.h>
#include <GameInterface.h>
#include <GUI/ObjectInterfaces/UnitInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <misc/Hotkeys.h>
#include <misc/FileSystem.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/PlaySetup.h>
#include <players/PlayerFactory.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/HospitalCare.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
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

/// Atreides is the human tester; Harkonnen an AI Player house (no QuantBot), or with \a aiClass
/// another AI player class.
std::unique_ptr<Game> newGame(const std::string& mapData, int seed, bool hospitalCare,
                              const std::string& aiClass = "AIPlayer:hard", bool slowCityYear = true) {
    auto options = gModOptions;
    options.hospitalCare = hospitalCare;
    options.slowCityYear = slowCityYear;
    options.immortalHumanPlayer = false;
    GameInitSettings init("hospital-care.ini", mapData, "Hospital care", true, options);
    init.randomSeed = seed;
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Hospital tester", "HumanPlayer"});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 2);
    ai.addPlayerInfo({"Hospital AI", aiClass});
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
            out << " trip=" << t->hospitalID << '/' << int(t->stage) << '/' << t->spotX << ',' << t->spotY << '/' << t->carried << '/' << t->bestDistance << '/' << t->startCycle;
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
    // Seven soldiers on the tiles round the first Hospital, so they all arrive at once.
    const Coord around[7] = {Coord(19, 22), Coord(20, 22), Coord(21, 22), Coord(22, 22), Coord(22, 21), Coord(22, 20), Coord(19, 21)};
    for(int i = 0; i < 7; ++i) units.push_back(soldier(owner, around[i], 4)->getObjectID());
    const int before = owner->getCredits();
    for(Uint32 id : units) order(*game, id);
    step(*game, hospitals, 3);
    for(Uint32 id : units) require(care().trip(id) != nullptr, "A soldier ordered to repair has no Hospital trip");

    int most = 0, waited = 0;
    std::vector<int> peak(hospitals.size(), 0);
    bool everyoneDone = false;
    for(int cycle = 0; cycle < 20000 && !everyoneDone; ++cycle) {
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
            if(t.stage == HospitalTrip::Stage::Healing) {
                require(beside(zone(t.hospitalID), unitByID(id)->getLocation()), "A healing soldier is not beside its Hospital");
                require(unitByID(id)->getAttackMode() == HOLD, "A healing soldier does not hold its bed");
            }
        }
        waited = std::max(waited, waitingNow);
        everyoneDone = care().trips().empty();
    }
    require(everyoneDone, "Not every soldier was discharged");
    for(Uint32 id : units) {
        const auto* unit = unitByID(id);
        require(unit && unit->getHealth() >= unit->getMaxHealth(), "A soldier left the Hospital without full health");
        require(unit->getAttackMode() == GUARD, "A healed soldier did not get its Guard mode back");
    }
    const int paid = before - owner->getCredits();
    // 7 soldiers x 16 hit points x 0.2 credits = 22.4 (credits are shown rounded).
    require(paid >= 21 && paid <= 24, "The house did not pay 0.2 credits a hit point");
    SDL_Log("HOSPITAL_CARE_PROBE_DIAG: most=%d waited=%d paid=%d", most, waited, paid);
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
    for(int i = 0; i < 20000 && care().trip(walker); ++i) {
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
    for(int i = 0; i < 20000 && (care().trip(id) || !carried); ++i) {
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

// HospitalInfantry: a QuantBot soldier below 60% waits until no armed enemy has had it in reach for
// kHospitalQuietCycles. A builder-role QuantBot leaves the soldier where it is put.
void checkBotQuiet(const std::string& mapData) {
    auto game = newGame(mapData, 16, true, "qBotSupportHard");
    House* human = game->getHouse(HOUSE_ATREIDES);
    House* bots = game->getHouse(HOUSE_HARKONNEN);
    QuantBot* bot = nullptr;
    for(const auto& player : bots->getPlayerList()) if(!bot) bot = dynamic_cast<QuantBot*>(player.get());
    require(bot != nullptr, "The second house is not a QuantBot");
    auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
    config->force.hospitalInfantry = true;
    bot->configOverride = config;
    require(bot->sendsInfantryToHospital(), "HospitalInfantry did not switch on for the bot");
    const std::vector<Uint32> hospitals{placeHospital(bots, Coord(40, 40))};
    parkCarryall(bots, Coord(44, 44));
    auto* wounded = soldier(bots, Coord(30, 30), 8);            // 40%
    const Uint32 id = wounded->getObjectID();
    auto* quad = dynamic_cast<UnitBase*>(human->createUnit(Unit_Quad, true));
    require(quad != nullptr, "Could not create the enemy Quad");
    quad->deploy(Coord(33, 30));
    quad->setActive(false);                                       // armed and in reach, but holds fire
    step(*game, hospitals, 1000);
    require(HC::inContact(wounded), "An armed enemy three tiles away is not contact");
    require(care().trip(id) == nullptr, "A QuantBot soldier was sent to a Hospital while in contact");
    quad->cancelDeployment();
    const Uint32 quietFrom = game->getGameCycleCount();
    require(!HC::inContact(wounded), "Contact outlived the enemy");
    Uint32 sentAt = 0;
    for(int i = 0; i < 2000 && !sentAt; ++i) {
        step(*game, hospitals);
        if(care().trip(id)) sentAt = game->getGameCycleCount();
    }
    require(sentAt != 0, "A quiet QuantBot soldier below 60% was never sent");
    const Uint32 waited = sentAt - quietFrom;
    require(waited >= DuneCity::kHospitalQuietCycles && waited <= DuneCity::kHospitalQuietCycles + 2 * DuneCity::kHospitalRecheckCycles,
            "A QuantBot soldier was not sent about eight seconds after its last contact");
    // The effect: the bot's soldier is healed to full beside its Hospital.
    for(int i = 0; i < 20000 && care().trip(id); ++i) step(*game, hospitals);
    require(!care().trip(id) && wounded->getHealth() == wounded->getMaxHealth()
            && beside(zone(hospitals[0]), wounded->getLocation()), "The QuantBot soldier was not healed at its Hospital");
    record("bot_quiet", "held while an armed enemy was in reach; sent " + std::to_string(waited)
           + " cycles after it left; healed to full beside the Hospital");
    game.reset();
    currentGame = nullptr;

    // Switch off (the default): the same quiet, wounded bot soldier stays where it is.
    auto off = newGame(mapData, 16, true, "qBotSupportHard");
    House* offBots = off->getHouse(HOUSE_HARKONNEN);
    QuantBot* offBot = nullptr;
    for(const auto& player : offBots->getPlayerList()) if(!offBot) offBot = dynamic_cast<QuantBot*>(player.get());
    require(offBot != nullptr && !offBot->sendsInfantryToHospital(), "HospitalInfantry is on by default");
    const std::vector<Uint32> offHospitals{placeHospital(offBots, Coord(40, 40))};
    parkCarryall(offBots, Coord(44, 44));
    const Uint32 stays = soldier(offBots, Coord(30, 30), 8)->getObjectID();
    step(*off, offHospitals, 3000);
    require(!care().trip(stays) && unitByID(stays)->getHealth() < unitByID(stays)->getMaxHealth(),
            "A QuantBot soldier went to a Hospital with HospitalInfantry off");
    record("bot_off", "HospitalInfantry off: a quiet soldier at 40% was not sent in 3000 cycles");
    off.reset();
    currentGame = nullptr;
}

void checkSave(const std::string& mapData) {
    auto game = newGame(mapData, 15, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
    parkCarryall(owner, Coord(24, 18));
    // The far Trooper is ordered first; once its Carryall has it, six soldiers beside the Hospital.
    const Uint32 trooper = soldier(owner, Coord(50, 50), 3, Unit_Trooper)->getObjectID();
    order(*game, trooper);
    for(int i = 0; i < 20000 && !(unitByID(trooper) && unitByID(trooper)->isPickedUp()); ++i) step(*game, hospitals);
    require(unitByID(trooper) && unitByID(trooper)->isPickedUp(), "The save fixture's Trooper was never lifted");
    std::vector<Uint32> units;
    const Coord around[6] = {Coord(19, 22), Coord(20, 22), Coord(21, 22), Coord(22, 22), Coord(22, 21), Coord(22, 20)};
    for(int i = 0; i < 6; ++i) units.push_back(soldier(owner, around[i], 1)->getObjectID());
    units.push_back(trooper);
    for(int i = 0; i < 6; ++i) order(*game, units[i]);
    // Until five heal, one waits and the far Trooper is in the air.
    bool ready = false;
    for(int i = 0; i < 20000 && !ready; ++i) {
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

// --- The player's own path -------------------------------------------------------------------

size_t commandCount(const Game& game) {
    size_t count = 0;
    for(const auto& [cycle, slot] : game.cmdManager.timeslot) count += slot.size();
    return count;
}

/// Selects the units as a click (or a drag box) on them does; returns the side panel it opened.
ObjectInterface* select(Game& game, const std::vector<Uint32>& ids) {
    require(game.pInterface != nullptr, "The game has no interface");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(Uint32 id : ids) {
        game.selectedList.insert(id);
        game.getObjectManager().getObject(id)->setSelected(true);
    }
    game.selectionChanged();
    game.pInterface->updateObjectInterface();
    ObjectInterface* panel = game.pInterface->pObjectContainer;
    require(panel != nullptr && panel->update(), "Selecting the soldiers opened no unit panel");
    return panel;
}

void clickButton(Widget& button) {
    button.handleMouseLeft(2, 2, true);
    button.handleMouseLeft(2, 2, false);
}

/// Plays until \a done holds, at most \a limit cycles; returns the cycles played.
template<class Done> int playUntil(Game& game, const std::vector<Uint32>& hospitals, int limit, Done done) {
    int played = 0;
    while(!done() && played < limit) { step(game, hospitals); ++played; }
    return played;
}

/// The soldiers went by the order's own effect: each a trip to this Hospital, never a Repair Yard,
/// never carried (the fixture house has no Carryall), healed beside it and left at full health.
void healsThere(Game& game, const std::vector<Uint32>& hospitals, const std::vector<Uint32>& units, Uint32 hospital,
                const std::string& label) {
    const auto allSent = [&] {
        return std::all_of(units.begin(), units.end(), [](Uint32 id) { return care().trip(id) != nullptr; });
    };
    playUntil(game, hospitals, 200, allSent);
    std::map<Uint32, int> before;
    std::map<Uint32, bool> healing;
    for(Uint32 id : units) {
        require(care().trip(id) && care().trip(id)->hospitalID == hospital, label + ": no trip to the Hospital");
        before[id] = unitByID(id)->getHealth().lround();
    }
    const auto anyOnTrip = [&] {
        return std::any_of(units.begin(), units.end(), [](Uint32 id) { return care().trip(id) != nullptr; });
    };
    for(int i = 0; i < 20000 && anyOnTrip(); ++i) {
        step(game, hospitals);
        for(Uint32 id : units) {
            auto* unit = unitByID(id);
            require(unit && unit->getHealth() > 0, label + ": a soldier died");
            require(!unit->goingToRepairYard, label + ": a soldier went for a Repair Yard");
            require(!unit->isPickedUp(), label + ": a soldier was carried, not walked");
            if(care().trip(id) && care().trip(id)->stage == HospitalTrip::Stage::Healing) {
                healing[id] = true;
                require(beside(zone(hospital), unit->getLocation()), label + ": healing away from the Hospital");
            }
        }
    }
    for(Uint32 id : units) {
        auto* unit = unitByID(id);
        require(healing[id] && !care().trip(id), label + ": a soldier never healed or never left");
        require(unit->getHealth() == unit->getMaxHealth() && unit->getHealth().lround() > before[id],
                label + ": a soldier left without full health");
    }
}
void healsThere(Game& game, const std::vector<Uint32>& hospitals, Uint32 unitID, Uint32 hospital, const std::string& label) {
    healsThere(game, hospitals, std::vector<Uint32>{unitID}, hospital, label);
}

void checkUiRepair(const std::string& mapData) {
    auto game = newGame(mapData, 21, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
    // A Repair Yard nearer the soldier than the Hospital: an infantry order must not use it.
    auto* yard = dynamic_cast<RepairYard*>(owner->placeStructure(NONE_ID, Structure_RepairYard, 30, 30, true, true));
    require(yard != nullptr, "Could not place the Repair Yard");
    auto* healthy = soldier(owner, Coord(40, 40), 1);
    healthy->setHealth(FixPoint(healthy->getMaxHealth()));
    auto* wounded = soldier(owner, Coord(34, 34), 0);
    wounded->setHealth(FixPoint(std::max(1, wounded->getMaxHealth() / 4)));
    step(*game, hospitals, 2);

    auto* panel = dynamic_cast<UnitInterface*>(select(*game, {healthy->getObjectID()}));
    require(panel != nullptr, "A Soldier's panel is not the unit panel");
    require(!panel->sendToRepairButton.isVisible(), "The Repair button shows for a Soldier at full health");

    panel = dynamic_cast<UnitInterface*>(select(*game, {wounded->getObjectID()}));
    require(panel != nullptr && panel->sendToRepairButton.isVisible() && panel->sendToRepairButton.isEnabled(),
            "The Repair button is not clickable for a wounded Soldier");
    require(!panel->sendToRepairButton.getTooltipText().empty()
            && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::healAtHospital(),
            "The Repair button does not say it sends the soldier to a Hospital");
    const size_t queued = commandCount(*game);
    clickButton(panel->sendToRepairButton);
    require(commandCount(*game) == queued + 1, "The Repair button did not queue exactly one command");
    require(!care().trip(wounded->getObjectID()), "The button acted without a command");
    healsThere(*game, hospitals, wounded->getObjectID(), hospitals[0], "Repair button");
    require(yard->getNumBookings() == 0, "The Repair Yard was booked for a Soldier");
    record("ui_repair", "Repair button shown with its tooltip only when wounded; one click, one command; "
           "walked past the nearer Repair Yard to the Hospital and healed to full");
    currentGame = nullptr;
}

void checkWalkIn(const std::string& mapData) {
    auto game = newGame(mapData, 22, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    House* enemy = game->getHouse(HOUSE_HARKONNEN);
    const Uint32 hospital = placeHospital(owner, Coord(20, 20));
    const Uint32 enemyHospital = placeHospital(enemy, Coord(50, 10));
    const std::vector<Uint32> hospitals{hospital, enemyHospital};
    const auto wounded = [&](Coord at, int item = Unit_Soldier) {
        auto* unit = soldier(owner, at, 1, item);
        unit->setHealth(FixPoint(std::max(1, unit->getMaxHealth() / 4)));
        return unit->getObjectID();
    };
    step(*game, hospitals, 2);

    // 1. Right-click the Hospital (the zone's own tile) with a wounded Soldier.
    const Uint32 a = wounded(Coord(32, 20));
    select(*game, {a});
    size_t queued = commandCount(*game);
    require(game->handleSelectedObjectsActionClick(21, 21), "The right-click on the Hospital was not accepted");
    require(commandCount(*game) == queued + 1, "The right-click did not queue one command");
    healsThere(*game, hospitals, a, hospital, "Right-click on the Hospital");
    record("walkin_rightclick", "a wounded Soldier right-clicked onto its Hospital walked in and healed to full");

    // 2. The Move button, then a click on a tile beside the Hospital.
    const Uint32 b = wounded(Coord(34, 26), Unit_Trooper);
    auto* panel = dynamic_cast<UnitInterface*>(select(*game, {b}));
    require(panel != nullptr && panel->moveButton.isVisible(), "The Move button is not shown");
    clickButton(panel->moveButton);
    require(game->currentCursorMode == Game::CursorMode_Move, "The Move button did not open the Move cursor");
    queued = commandCount(*game);
    require(game->handleSelectedObjectsMoveClick(22, 22), "The Move click beside the Hospital was not accepted");
    require(game->currentCursorMode == Game::CursorMode_Normal, "The Move cursor stayed open");
    require(commandCount(*game) == queued + 1, "The Move click did not queue one command");
    healsThere(*game, hospitals, b, hospital, "Move beside the Hospital");
    record("walkin_move", "a wounded Trooper sent with the Move button to a tile beside its Hospital healed to full");

    // 3. A group right-clicked onto a free tile beside the Hospital (a formation move).
    const std::vector<Uint32> group{wounded(Coord(36, 34)), wounded(Coord(37, 34)), wounded(Coord(38, 34))};
    select(*game, group);
    require(game->handleSelectedObjectsActionClick(19, 22), "The group's right-click beside the Hospital was not accepted");
    healsThere(*game, hospitals, group, hospital, "Group beside the Hospital");
    record("walkin_group", "three wounded Soldiers right-clicked as a group beside their Hospital all healed to full");

    // 4. What does not walk in.
    auto* fit = soldier(owner, Coord(40, 20), 1);
    fit->setHealth(FixPoint(fit->getMaxHealth()));
    select(*game, {fit->getObjectID()});
    require(game->handleSelectedObjectsActionClick(21, 21), "The healthy Soldier's right-click was not accepted");
    playUntil(*game, hospitals, 400, [] { return false; });
    require(!care().trip(fit->getObjectID()), "A Soldier at full health was put on a Hospital trip");

    const Uint32 far = wounded(Coord(40, 30));
    select(*game, {far});
    require(game->handleSelectedObjectsActionClick(20, 26), "The move four tiles off the Hospital was not accepted");
    playUntil(*game, hospitals, 400, [] { return false; });
    require(!care().trip(far) && unitByID(far)->getHealth() < unitByID(far)->getMaxHealth()
            && !beside(zone(hospital), unitByID(far)->getLocation()),
            "A soldier moved four tiles from the Hospital was diverted to it");

    const Uint32 raider = wounded(Coord(44, 10));
    select(*game, {raider});
    require(game->handleSelectedObjectsActionClick(50, 10), "The right-click on the enemy Hospital was not accepted");
    playUntil(*game, hospitals, 200, [] { return false; });
    require(!care().trip(raider), "A soldier right-clicked onto an enemy Hospital went to heal");

    const Uint32 saboteur = wounded(Coord(32, 24), Unit_Saboteur);
    select(*game, {saboteur});
    require(game->handleSelectedObjectsActionClick(21, 21), "The Saboteur's right-click was not accepted");
    playUntil(*game, hospitals, 200, [] { return false; });
    require(!care().trip(saboteur), "A Saboteur was put on a Hospital trip");
    record("walkin_not", "no walk-in for a healthy Soldier, a move four tiles away, an enemy Hospital or a Saboteur");
    currentGame = nullptr;

    // 5. Hospital Care off: the right-click is an ordinary move.
    auto off = newGame(mapData, 23, false);
    House* offOwner = off->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> offHospitals{placeHospital(offOwner, Coord(20, 20))};
    auto* offSoldier = soldier(offOwner, Coord(32, 20), 1);
    offSoldier->setHealth(FixPoint(std::max(1, offSoldier->getMaxHealth() / 4)));
    step(*off, offHospitals, 2);
    select(*off, {offSoldier->getObjectID()});
    require(off->handleSelectedObjectsActionClick(21, 21), "The right-click with the rule off was not accepted");
    playUntil(*off, offHospitals, 400, [] { return false; });
    require(!care().trip(offSoldier->getObjectID()) && offSoldier->getHealth() < offSoldier->getMaxHealth(),
            "A soldier healed with Hospital Care off");
    record("walkin_rule_off", "with Hospital Care off a right-click on a Hospital is a plain move");
    currentGame = nullptr;
}

void pressKey(Game& game, SDL_Keycode sym) {
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = sym;
    key.keysym.mod = KMOD_NONE;
    game.handleKeyInput(key);
}

void checkUiNoHospital(const std::string& mapData) {
    using DuneCity::HospitalCare::RepairButton;
    const auto savedHotkeys = settings.hotkeys;
    settings.hotkeys = Hotkeys::Bindings::defaults();
    for(const bool rule : {true, false}) {
        auto game = newGame(mapData, rule ? 25 : 26, rule);
        House* owner = game->getHouse(HOUSE_ATREIDES);
        auto* yard = dynamic_cast<RepairYard*>(owner->placeStructure(NONE_ID, Structure_RepairYard, 30, 30, true, true));
        require(yard != nullptr, "Could not place the Repair Yard");
        auto* wounded = soldier(owner, Coord(40, 40), 1);
        wounded->setHealth(FixPoint(std::max(1, wounded->getMaxHealth() / 4)));
        const Uint32 id = wounded->getObjectID();
        const std::vector<Uint32> none;
        step(*game, none, 2);
        auto* panel = dynamic_cast<UnitInterface*>(select(*game, {id}));
        require(panel != nullptr && panel->sendToRepairButton.isVisible(), "The Repair button is not shown for a wounded Soldier");
        if(!rule) {
            // Hospital Care off: the old order. The button sends the soldier for the Repair Yard.
            require(panel->sendToRepairButton.isEnabled() && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::repairUnit(),
                    "Rule off: the Repair button is not the ordinary one");
            clickButton(panel->sendToRepairButton);
            playUntil(*game, none, 200, [&] { return wounded->goingToRepairYard; });
            require(wounded->goingToRepairYard && yard->getNumBookings() == 1, "Rule off: the soldier did not book the Repair Yard");
            record("ui_no_hospital_rule_off", "Hospital Care off: the Repair button books the Repair Yard as before");
            game.reset();
            currentGame = nullptr;
            continue;
        }
        require(DuneCity::HospitalCare::repairButtonFor(wounded) == RepairButton::NeedsHospital, "A soldier with no Hospital is not 'needs a Hospital'");
        require(!panel->sendToRepairButton.isEnabled(), "The Repair button is enabled with no Hospital");
        require(panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::needsHospital(),
                "The disabled Repair button does not say it needs a Hospital");
        const size_t queued = commandCount(*game);
        clickButton(panel->sendToRepairButton);
        require(commandCount(*game) == queued, "The disabled Repair button queued a command");
        // The R hotkey queues the order anyway; the order itself must not go to the yard.
        pressKey(*game, SDLK_r);
        require(commandCount(*game) == queued + 1, "The R hotkey did not queue the repair order");
        const Coord start = wounded->getLocation();
        step(*game, none, 600);
        require(!wounded->goingToRepairYard && yard->getNumBookings() == 0 && wounded->getLocation() == start
                && !care().trip(id), "The repair order sent a soldier toward the Repair Yard");
        // A right-click on the Repair Yard is a plain move: no repair booking for a soldier.
        select(*game, {id});
        require(game->handleSelectedObjectsActionClick(31, 31), "The right-click on the Repair Yard was not accepted");
        step(*game, none, 400);
        require(!wounded->goingToRepairYard && yard->getNumBookings() == 0, "A right-click booked the Repair Yard for a soldier");
        // A Saboteur: no Hospital and no yard heals it.
        auto* saboteur = soldier(owner, Coord(44, 40), 1, Unit_Saboteur);
        saboteur->setHealth(FixPoint(std::max(1, saboteur->getMaxHealth() / 4)));
        panel = dynamic_cast<UnitInterface*>(select(*game, {saboteur->getObjectID()}));
        require(panel && !panel->sendToRepairButton.isEnabled()
                && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::saboteurCannotHeal(),
                "A Saboteur's Repair button does not say it cannot be healed");
        // A Hospital appears: the soldier's button is enabled and names it.
        const std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
        panel = dynamic_cast<UnitInterface*>(select(*game, {id}));
        require(panel && panel->update() && panel->sendToRepairButton.isEnabled()
                && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::healAtHospital(),
                "With a Hospital the Repair button is not the Heal order");
        clickButton(panel->sendToRepairButton);
        healsThere(*game, hospitals, id, hospitals[0], "Heal after the Hospital appeared");
        require(yard->getNumBookings() == 0, "The Repair Yard was booked");
        record("ui_no_hospital", "no Hospital: button disabled with 'Needs a Hospital', button, R hotkey and right-click "
               "on the Repair Yard left the soldier where it stood and the yard unbooked; Saboteur 'cannot be healed'; "
               "with a Hospital the button heals");
        game.reset();
        currentGame = nullptr;
    }
    settings.hotkeys = savedHotkeys;
}

void checkUiBeds(const std::string& mapData) {
    auto game = newGame(mapData, 27, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(20, 20))};
    // On the ring round the Hospital, so all seven arrive at once (a soldier walks a tile in
    // about 300 cycles: from apart they would come one by one and never fill the beds).
    const Coord ring[7] = {Coord(19, 19), Coord(20, 19), Coord(21, 19), Coord(22, 19), Coord(22, 20), Coord(22, 21), Coord(19, 20)};
    std::vector<Uint32> group;
    for(int i = 0; i < 7; ++i) {
        auto* unit = soldier(owner, ring[i], 1, i % 2 ? Unit_Trooper : Unit_Soldier);
        unit->setHealth(FixPoint(std::max(1, unit->getMaxHealth() / 5)));
        group.push_back(unit->getObjectID());
    }
    step(*game, hospitals, 2);
    auto* panel = dynamic_cast<MultiUnitInterface*>(select(*game, group));
    require(panel != nullptr, "Seven soldiers did not open the group panel");
    require(panel->sendToRepairButton.isVisible() && panel->sendToRepairButton.isEnabled()
            && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::healAtHospital(),
            "The group's Repair button is not the Heal order");
    const size_t queued = commandCount(*game);
    clickButton(panel->sendToRepairButton);
    require(commandCount(*game) == queued + group.size(), "The group's button did not queue one order a soldier");
    playUntil(*game, hospitals, 200, [&] {
        return std::all_of(group.begin(), group.end(), [](Uint32 id) { return care().trip(id) != nullptr; });
    });
    int maxHealing = 0, maxWaiting = 0;
    std::map<Uint32, int> start;
    for(Uint32 id : group) {
        require(care().trip(id) != nullptr, "A soldier of the group got no trip");
        start[id] = unitByID(id)->getHealth().lround();
    }
    for(int i = 0; i < 40000 && std::any_of(group.begin(), group.end(), [](Uint32 id) { return care().trip(id) != nullptr; }); ++i) {
        step(*game, hospitals);
        int healing = 0, waiting = 0;
        for(Uint32 id : group) {
            const auto* t = care().trip(id);
            if(t && t->stage == HospitalTrip::Stage::Healing) ++healing;
            if(t && t->stage == HospitalTrip::Stage::Waiting) ++waiting;
        }
        require(healing <= DuneCity::kHospitalBeds, "More than five soldiers healed at one Hospital");
        if(care().claims(hospitals[0]) > 0)
            require(HC::isLockedHospital(zone(hospitals[0])), "A Hospital with patients was not locked");
        maxHealing = std::max(maxHealing, healing);
        maxWaiting = std::max(maxWaiting, waiting);
    }
    require(maxHealing == DuneCity::kHospitalBeds && maxWaiting >= 1, "The five beds were never all in use with others waiting");
    for(Uint32 id : group) {
        auto* unit = unitByID(id);
        require(unit && !care().trip(id) && unit->getHealth() == unit->getMaxHealth() && unit->getHealth().lround() > start[id],
                "A soldier of the group did not leave at full health");
    }
    record("ui_beds", "seven soldiers, one click on the group's Heal button: five healed at once, "
           + std::to_string(maxWaiting) + " waited, the Hospital stayed locked, all left at full health");
    game.reset();
    currentGame = nullptr;
}

void checkUiCarryall(const std::string& mapData) {
    auto game = newGame(mapData, 28, true);
    House* owner = game->getHouse(HOUSE_ATREIDES);
    const std::vector<Uint32> hospitals{placeHospital(owner, Coord(10, 10))};
    parkCarryall(owner, Coord(12, 14));
    auto* trooper = soldier(owner, Coord(45, 45), 1, Unit_Trooper);
    trooper->setHealth(FixPoint(std::max(1, trooper->getMaxHealth() / 4)));
    const Uint32 id = trooper->getObjectID();
    step(*game, hospitals, 2);
    require(blockDistance(trooper->getLocation(), zone(hospitals[0])->getClosestPoint(trooper->getLocation())) >= MIN_CARRYALL_LIFT_DISTANCE,
            "The fixture Trooper is not a flight away");
    auto* panel = dynamic_cast<UnitInterface*>(select(*game, {id}));
    require(panel && panel->sendToRepairButton.isEnabled(), "The Trooper's Repair button is not clickable");
    clickButton(panel->sendToRepairButton);
    bool carried = false;
    for(int i = 0; i < 20000 && (care().trip(id) || !carried); ++i) {
        step(*game, hospitals);
        require(unitByID(id) != nullptr, "The Trooper died on the flight");
        if(unitByID(id)->isPickedUp()) carried = true;
        require(!unitByID(id)->goingToRepairYard, "The Trooper was flown for a Repair Yard");
    }
    require(carried, "No Carryall picked the Trooper up");
    require(!care().trip(id) && trooper->getHealth() == trooper->getMaxHealth() && beside(zone(hospitals[0]), trooper->getLocation()),
            "The flown Trooper was not healed beside the Hospital");
    record("ui_carryall", "the Repair button for a Trooper 35 tiles away: lifted by the Carryall, healed beside the Hospital");
    game.reset();
    currentGame = nullptr;
}

// --- A real game: the bundled Metropolis map --------------------------------------------------

/// The main menu's Custom Game with the saved dunecity mod: the lobby seats the local human and an
/// AI Player (easy) in every other slot, as a player starting the map does.
std::unique_ptr<Game> startMetropolis(int seed) {
    const char* mapEnv = std::getenv("HOSPITAL_CARE_METROPOLIS");
    require(mapEnv != nullptr, "Missing HOSPITAL_CARE_METROPOLIS");
    const std::string mapPath = mapEnv;
    CustomPlaySetup setup;
    setup.rules = initialCustomGameRules();
    require(ModManager::instance().getActiveModName() == "dunecity", "Custom Game did not activate the saved dunecity mod");
    require(setup.rules.hospitalCare, "A Dune City Custom Game does not start with Hospital Care on");
    setup.maps = {mapPath};
    setup.map = 0;
    setup.mods = ModManager::instance().listModChoices();
    for(size_t i = 0; i < setup.mods.size(); ++i)
        if(setup.mods[i].matchesSelectionName(ModManager::instance().getActiveModName())) setup.mod = static_cast<int>(i);
    setup.online = false;
    setup.sharedHouse = false;
    GameInitSettings init(getBasename(mapPath, true), readCompleteFile(mapPath), setup.multiplePlayersPerHouse(), setup.rules);
    {
        CustomGamePlayers lobby(init, true, false, &setup);
        lobby.update();
        require(lobby.numHouses == 4, "The lobby does not seat Metropolis's four houses");
        const int ai = PlayerFactory::getIndexByPlayerClass("AIPlayerEasy");
        require(ai >= 0, "Missing AI Player (easy)");
        int humans = 0;
        for(int row = 0; row < lobby.numHouses; ++row) {
            if(lobby.getSeatPlayer(2 * row) == 0) { ++humans; continue; }
            require(lobby.selectSeatPlayer(2 * row, ai), "The lobby cannot seat an AI Player");
            lobby.onChangePlayerDropDownBoxes(true, 2 * row);
        }
        require(humans == 1, "The lobby did not seat exactly the local human");
        lobby.addAllPlayersToGameInitSettings();
        init = lobby.gameInitSettings;
    }
    init.randomSeed = seed;
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // a private fixture, never a match-analytics sample
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    require(game->isCitySimEnabled() && game->getGameInitSettings().getGameOptions().hospitalCare,
            "Metropolis did not start as a city game with Hospital Care");
    require(pLocalHouse != nullptr && pLocalPlayer != nullptr, "Metropolis has no local human house");
    return game;
}

std::vector<ZoneStructure*> hospitalsOf(const House* house) {
    std::vector<ZoneStructure*> out;
    for(const auto& [id, overlay] : care().overlays()) {
        auto* z = zone(id);
        if(overlay == static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital) && z && z->getOwner() == house) out.push_back(z);
    }
    return out;
}

/// Free tiles (no structure, no vehicle) beside \a z: where a patient can stand.
std::vector<Coord> freeRing(const ZoneStructure* z) {
    std::vector<Coord> out;
    const Coord& o = z->getLocation();
    for(int y = o.y - 1; y <= o.y + z->getStructureSizeY(); ++y)
        for(int x = o.x - 1; x <= o.x + z->getStructureSizeX(); ++x) {
            const Coord at(x, y);
            if(beside(z, at) && currentGameMap->tileExists(at) && !currentGameMap->getTile(at)->hasANonInfantryGroundObject()
               && !currentGameMap->getTile(at)->isMountain())
                out.push_back(at);
        }
    return out;
}

/// The nearest free, walkable tile at least \a minDistance from \a from (spiral search).
Coord freeTileNear(const Coord& from, int minDistance, int maxDistance) {
    for(int r = minDistance; r <= maxDistance; ++r)
        for(int dy = -r; dy <= r; ++dy)
            for(int dx = -r; dx <= r; ++dx) {
                if(std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const Coord at(from.x + dx, from.y + dy);
                if(!currentGameMap->tileExists(at)) continue;
                const Tile* t = currentGameMap->getTile(at);
                if(!t->hasAnObject() && !t->isMountain() && t->isRock()) return at;
            }
    return Coord::Invalid();
}

void checkMetropolis() {
    using CO = ZoneStructure::CivicOverlay;
    using DuneCity::HospitalCare::RepairButton;
    const auto savedHotkeys = settings.hotkeys;
    settings.hotkeys = Hotkeys::Bindings::defaults();
    auto game = startMetropolis(20261127);
    House* home = pLocalHouse;
    auto* sim = game->getCitySimulation();
    const std::vector<Uint32> none;
    RepairYard* yard = nullptr;
    for(StructureBase* structure : structureList)
        if(structure->getItemID() == Structure_RepairYard && structure->getOwner() == home && !yard) yard = static_cast<RepairYard*>(structure);
    require(yard != nullptr, "The local Metropolis city has no Repair Yard");

    // 1. Before the first census: no Hospital. The Repair button is disabled, and neither it nor
    //    the R hotkey sends a wounded soldier to the city's Repair Yard.
    require(hospitalsOf(home).empty(), "Metropolis has a Hospital before its first census");
    const Coord nearYard = freeTileNear(yard->getLocation(), 4, 12);
    require(nearYard.isValid(), "No free tile near the Repair Yard");
    auto* early = soldier(home, nearYard, 1);
    early->setHealth(FixPoint(std::max(1, early->getMaxHealth() / 4)));
    auto* panel = dynamic_cast<UnitInterface*>(select(*game, {early->getObjectID()}));
    require(panel && panel->sendToRepairButton.isVisible() && !panel->sendToRepairButton.isEnabled()
            && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::needsHospital(),
            "Metropolis, no Hospital: the Repair button is not disabled with 'Needs a Hospital'");
    const size_t queued = commandCount(*game);
    clickButton(panel->sendToRepairButton);
    require(commandCount(*game) == queued, "Metropolis: the disabled Repair button queued a command");
    pressKey(*game, SDLK_r);
    require(commandCount(*game) == queued + 1, "Metropolis: the R hotkey did not queue the order");
    const Coord earlyAt = early->getLocation();
    const int bookingsBefore = yard->getNumBookings();
    for(int i = 0; i < 600 && hospitalsOf(home).empty(); ++i) {
        game->updateGameState();
        require(!early->goingToRepairYard && yard->getNumBookings() == bookingsBefore,
                "Metropolis: the repair order sent a soldier for the Repair Yard");
    }
    require(early->getLocation() == earlyAt && !care().trip(early->getObjectID()),
            "Metropolis: a soldier moved on a repair order with no Hospital");
    record("metro_no_hospital", "before the first census: Repair button disabled ('Needs a Hospital'); button and R "
           "hotkey left the soldier in place and the Repair Yard unbooked");

    // 2. The census designates the city's Hospitals from its own population.
    std::map<Uint32, uint32_t> madeOn;          // every house's Hospitals: zone -> city day
    std::map<int, uint32_t> lastAdd, lastRemove;
    int adds = 0, removes = 0;
    long densityChanges = 0;
    std::map<const ZoneStructure*, int> density;
    std::map<Uint32, CO> seen;
    const auto watch = [&](const std::string& label) {
        const uint32_t day = sim->lastProcessedDay_;
        std::map<int, int> changesToday;
        for(StructureBase* structure : structureList) {
            auto* z = dynamic_cast<ZoneStructure*>(structure);
            if(!z || z->getZoneType() != DuneCity::ZoneType::Residential) continue;
            const int d = currentGameMap->getTile(z->getLocation())->getCityZoneDensity();
            auto it = density.find(z);
            if(it != density.end() && it->second != d) ++densityChanges;
            density[z] = d;
            const CO now = z->getCivicOverlay();
            auto& was = seen.emplace(z->getObjectID(), CO::None).first->second;
            if((was == CO::Hospital) == (now == CO::Hospital)) { was = now; continue; }
            const int h = z->getOwner()->getHouseID();
            require(++changesToday[h] <= 1, label + ": a house changed two Hospitals in one census");
            if(now == CO::Hospital) {
                require(!lastAdd.count(h) || day - lastAdd[h] >= DuneCity::kHospitalAddSpacingDays,
                        label + ": a house gained two Hospitals within a quarter year");
                lastAdd[h] = day; madeOn[z->getObjectID()] = day; ++adds;
            } else if(d > 0) {
                require(day - madeOn[z->getObjectID()] >= DuneCity::kHospitalMinLifeDays,
                        label + ": a Hospital was removed " + std::to_string(day - madeOn[z->getObjectID()]) + " days after it was made");
                require(!lastRemove.count(h) || day - lastRemove[h] >= DuneCity::kHospitalRemoveSpacingDays,
                        label + ": a house lost two Hospitals within a city year");
                require(!HC::isLockedHospital(z), label + ": a Hospital with patients lost its type");
                lastRemove[h] = day; ++removes;
            }
            was = now;
        }
    };
    for(int i = 0; i < 6000 && hospitalsOf(home).empty(); ++i) { game->updateGameState(); watch("Metropolis start"); }
    require(!hospitalsOf(home).empty(), "Metropolis: the census never gave the local city a Hospital");

    // 3. The Repair button heals a wounded Trooper standing near a Hospital (walked: under six tiles).
    ZoneStructure* hospital = nullptr;
    for(auto* z : hospitalsOf(home)) if(!hospital && freeRing(z).size() >= 2) hospital = z;
    for(int i = 0; i < 20000 && !hospital; ++i) {
        game->updateGameState(); watch("Metropolis waiting for a reachable Hospital");
        for(auto* z : hospitalsOf(home)) if(!hospital && freeRing(z).size() >= 2) hospital = z;
    }
    require(hospital != nullptr, "Metropolis: no local Hospital has two free tiles beside it");
    const Uint32 hospitalID = hospital->getObjectID();
    const auto metroStep = [&](int cycles, const std::string& label) {
        for(int i = 0; i < cycles; ++i) {
            game->updateGameState();
            watch(label);
            if(care().claims(hospitalID) > 0)
                require(zone(hospitalID) && zone(hospitalID)->getCivicOverlay() == CO::Hospital,
                        label + ": a Hospital with patients coming lost its type");
        }
    };
    // \a expected 0: whichever own Hospital the order chose (the nearest with a free bed).
    const auto healed = [&](const std::vector<Uint32>& ids, int limit, const std::string& label, bool expectFlight, Uint32 expected) {
        std::map<Uint32, int> start;
        std::map<Uint32, Uint32> target;
        bool flew = false;
        int maxHealing = 0;
        metroStep(1, label);
        for(int i = 0; i < 200 && std::any_of(ids.begin(), ids.end(), [](Uint32 id) { return !care().trip(id); }); ++i) metroStep(1, label);
        for(Uint32 id : ids) {
            require(care().trip(id) && (!expected || care().trip(id)->hospitalID == expected), label + ": a soldier has no trip to the Hospital");
            require(zone(care().trip(id)->hospitalID)->getOwner() == home, label + ": the trip is to another house's Hospital");
            start[id] = unitByID(id)->getHealth().lround();
            target[id] = care().trip(id)->hospitalID;
        }
        for(int i = 0; i < limit && std::any_of(ids.begin(), ids.end(), [](Uint32 id) { return care().trip(id) != nullptr; }); ++i) {
            metroStep(1, label);
            int healing = 0;
            for(Uint32 id : ids) {
                auto* unit = unitByID(id);
                require(unit && unit->getHealth() > 0, label + ": a soldier died");
                require(!unit->goingToRepairYard, label + ": a soldier went for a Repair Yard");
                if(unit->isPickedUp()) flew = true;
                const auto* t = care().trip(id);
                if(t && t->stage == HospitalTrip::Stage::Healing) ++healing;
            }
            for(const auto& [unitID, h] : target)
                require(care().patients(h) <= DuneCity::kHospitalBeds, label + ": more than five healed at one Hospital");
            maxHealing = std::max(maxHealing, healing);
        }
        for(Uint32 id : ids) {
            auto* unit = unitByID(id);
            require(!care().trip(id) && unit->getHealth() == unit->getMaxHealth() && unit->getHealth().lround() > start[id],
                    label + ": a soldier did not leave the Hospital at full health");
            require(beside(zone(target[id]), unit->getLocation()), label + ": a soldier was healed away from the Hospital");
        }
        require(flew == expectFlight, label + (expectFlight ? ": no Carryall flew the soldier" : ": the soldier was flown"));
        return maxHealing;
    };
    {
        const Coord at = freeTileNear(hospital->getLocation(), 3, 5);
        require(at.isValid(), "Metropolis: no free tile near the Hospital");
        auto* trooper = soldier(home, at, 1, Unit_Trooper);
        trooper->setHealth(FixPoint(std::max(1, trooper->getMaxHealth() / 4)));
        panel = dynamic_cast<UnitInterface*>(select(*game, {trooper->getObjectID()}));
        require(panel && panel->sendToRepairButton.isEnabled() && panel->sendToRepairButton.getTooltipText() == UnitCommandTooltips::healAtHospital(),
                "Metropolis: with a Hospital the Repair button is not the Heal order");
        clickButton(panel->sendToRepairButton);
        require(blockDistance(trooper->getLocation(), zone(hospitalID)->getClosestPoint(trooper->getLocation())) < MIN_CARRYALL_LIFT_DISTANCE,
                "Metropolis: the near Trooper is a flight away");
        healed({trooper->getObjectID()}, 20000, "Metropolis Repair button", false, 0);
        record("metro_repair", "the Repair button: a wounded Trooper walked to a Hospital of the played city and healed to full");
    }

    // 4. A Carryall flies a soldier six or more tiles from the Hospital.
    {
        if(!home->hasCarryalls()) parkCarryall(home, freeTileNear(hospital->getLocation(), 6, 30));
        Coord at = Coord::Invalid();
        for(int r = 20; r <= 60 && at.isInvalid(); r += 4) {
            const Coord c = freeTileNear(hospital->getLocation(), r, r + 3);
            if(c.isInvalid()) continue;
            bool far = true;
            for(auto* z : hospitalsOf(home))
                if(blockDistance(c, z->getClosestPoint(c)) < 8) far = false;
            if(far) at = c;
        }
        require(at.isValid(), "Metropolis: no free tile 8+ tiles from every Hospital");
        auto* flyer = soldier(home, at, 1, Unit_Trooper);
        flyer->setHealth(FixPoint(std::max(1, flyer->getMaxHealth() / 4)));
        panel = dynamic_cast<UnitInterface*>(select(*game, {flyer->getObjectID()}));
        require(panel && panel->sendToRepairButton.isEnabled(), "Metropolis Carryall: the Repair button is not clickable");
        clickButton(panel->sendToRepairButton);
        healed({flyer->getObjectID()}, 30000, "Metropolis Carryall", true, 0);
        record("metro_carryall", "a soldier 20+ tiles from a Hospital: lifted by a Carryall after the Repair button, healed to full");
    }

    // 5. Seven wounded soldiers on the Hospital's free side tiles, right-clicked onto it as a group.
    {
        const auto ring = freeRing(zone(hospitalID));
        require(ring.size() >= 2, "Metropolis: the Hospital lost its free side tiles");
        std::vector<Uint32> ward;
        for(int i = 0; i < 7; ++i) {
            auto* unit = soldier(home, ring[i % ring.size()], 1, i % 2 ? Unit_Trooper : Unit_Soldier);
            unit->setHealth(FixPoint(1));
            ward.push_back(unit->getObjectID());
        }
        metroStep(2, "Metropolis beds");
        select(*game, ward);
        require(game->handleSelectedObjectsActionClick(zone(hospitalID)->getLocation().x, zone(hospitalID)->getLocation().y),
                "Metropolis: the group's right-click on the Hospital was not accepted");
        const int maxHealing = healed(ward, 40000, "Metropolis beds", false, hospitalID);
        require(maxHealing == DuneCity::kHospitalBeds, "Metropolis: the five beds were never all in use (" + std::to_string(maxHealing) + ")");
        record("metro_beds", "seven soldiers right-clicked onto one Hospital: five healed at once, all left at full health; "
               "the Hospital kept its type at every census while they came");
    }

    // 6. The real city keeps living for two more city years: no Hospital moves, every change paced.
    const uint32_t until = sim->lastProcessedDay_ + 2 * 48;
    while(sim->lastProcessedDay_ < until) metroStep(1, "Metropolis two years");
    require(densityChanges > 0, "Metropolis: the city's zones never changed density");
    require(!hospitalsOf(home).empty(), "Metropolis: the local city lost every Hospital in two city years");
    // The healing Hospital stays unless its zone was lost or emptied (an overlay is drawn only on
    // a populated zone); a removal for surplus is impossible within four city years.
    if(zone(hospitalID) && currentGameMap->getTile(zone(hospitalID)->getLocation())->getCityZoneDensity() > 0)
        require(zone(hospitalID)->getCivicOverlay() == CO::Hospital, "Metropolis: a populated Hospital was retyped within two city years");
    record("metro_stable", std::to_string(densityChanges) + " residential density changes, " + std::to_string(adds)
           + " Hospitals made and " + std::to_string(removes) + " removed over all four cities, every change paced; "
           "the healing Hospital stood throughout");
    settings.hotkeys = savedHotkeys;
    game.reset();
    currentGame = nullptr;
}

/// A populated city, no fixture help: the census alone designates and keeps the overlays.
void checkPacing(const std::string& mapData) {
    auto game = newGame(mapData, 24, true, "AIPlayer:hard", false);   // a city year is one game minute
    House* owner = game->getHouse(HOUSE_ATREIDES);
    auto* sim = game->getCitySimulation();
    std::vector<ZoneStructure*> zones;
    for(int j = 0; j < 5; ++j)
        for(int i = 0; i < 6; ++i) {
            auto* z = dynamic_cast<ZoneStructure*>(owner->placeStructure(NONE_ID, Structure_ZoneResidential,
                                                                         6 + 3 * i, 6 + 3 * j, true, true));
            require(z != nullptr, "Could not place a city zone");
            zones.push_back(z);
        }
    using CO = ZoneStructure::CivicOverlay;
    std::map<Uint32, uint32_t> added;            // Hospital zone -> day it became one
    uint32_t lastAdd = 0, lastRemove = 0, lastChurchChange = 0;
    bool anyAdd = false, anyRemove = false, anyChurch = false;
    int adds = 0, removes = 0, populationChanges = 0, maxHospitals = 0;
    std::map<Uint32, CO> seen;
    for(auto* z : zones) seen[z->getObjectID()] = CO::None;
    const uint32_t lastDay = 300;
    const uint32_t lowDay = 150;
    Uint32 lockedHospital = 0;
    std::vector<Uint32> patients;
    int patientVisits = 0;
    while(sim->lastProcessedDay_ < lastDay) {
        const uint32_t day = sim->lastProcessedDay_;
        // The city's people keep moving: three zones a day empty or fill (never a Hospital, so
        // a Hospital is never dropped for being empty), and from day 150 the city shrinks to
        // a single Hospital's worth.
        for(size_t k = 0; k < zones.size(); ++k) {
            auto* z = zones[k];
            int level = day < lowDay ? 3 : (z->getCivicOverlay() == CO::Hospital ? 2 : 1);
            if(z->getCivicOverlay() != CO::Hospital && (k + day) % 8 < 3) level = day < lowDay ? (day % 2 ? 0 : 1) : 0;
            if(currentGameMap->getTile(z->getLocation())->getCityZoneDensity() != level) {
                z->setStartingLevel(level);
                ++populationChanges;
            }
        }
        // Days 185-205: patients keep coming to the first Hospital (due to go from day 193), sent by
        // a right-click on it; it must stay a Hospital while they come.
        if(day >= 185 && day < 205 && !added.empty()) {
            if(!lockedHospital) {
                lockedHospital = added.begin()->first;
                for(const auto& [zoneID, madeOn] : added) if(madeOn < added[lockedHospital]) lockedHospital = zoneID;
                require(zone(lockedHospital)->getCivicOverlay() == CO::Hospital, "The first Hospital is already gone at day 185");
                const Coord o = zone(lockedHospital)->getLocation();
                for(int i = 0; i < 3; ++i) patients.push_back(soldier(owner, Coord(o.x - 1, o.y + i - 1 + 1), 1)->getObjectID());
            }
            if(patientVisits == 0) {
                select(*game, patients);
                require(game->handleSelectedObjectsActionClick(zone(lockedHospital)->getLocation().x, zone(lockedHospital)->getLocation().y),
                        "The patients' right-click on their Hospital was not accepted");
                ++patientVisits;
            }
            // New wounds keep them in their beds (a ward that never empties) until day 205.
            for(Uint32 id : patients) {
                auto* unit = unitByID(id);
                const auto* t = care().trip(id);
                if(unit && t && t->stage == HospitalTrip::Stage::Healing && unit->getHealth() * 2 > unit->getMaxHealth()) {
                    unit->setHealth(FixPoint(1));
                    ++patientVisits;
                }
            }
        }
        game->updateGameState();
        if(lockedHospital && day >= 187 && day < 205)
            require(zone(lockedHospital)->getCivicOverlay() == CO::Hospital, "A Hospital with patients coming lost its type on day " + std::to_string(day));
        int hospitalsNow = 0, hospitalChanges = 0;
        for(auto* z : zones) {
            const Uint32 id = z->getObjectID();
            const CO now = z->getCivicOverlay(), was = seen[id];
            if(now == CO::Hospital) ++hospitalsNow;
            if(now == was) continue;
            if(was == CO::Hospital || now == CO::Hospital) {
                ++hospitalChanges;
                if(now == CO::Hospital) {
                    require(!anyAdd || day - lastAdd >= DuneCity::kHospitalAddSpacingDays,
                            "Two Hospitals were added within a quarter year (day " + std::to_string(day) + ")");
                    added[id] = day; lastAdd = day; anyAdd = true; ++adds;
                } else {
                    require(currentGameMap->getTile(z->getLocation())->getCityZoneDensity() > 0,
                            "A Hospital was dropped from a populated zone by a resize, not a removal");
                    require(day - added[id] >= DuneCity::kHospitalMinLifeDays,
                            "A Hospital was removed " + std::to_string(day - added[id]) + " days after it was made");
                    require(!anyRemove || day - lastRemove >= DuneCity::kHospitalRemoveSpacingDays,
                            "Two Hospitals were removed within a city year");
                    lastRemove = day; anyRemove = true; ++removes;
                }
            }
            if((was == CO::Church) != (now == CO::Church) && was != CO::Hospital && now != CO::Hospital) {
                // A Church emptied by the scripted churn is dropped (an overlay is drawn only on a
                // populated zone); a designation change is paced.
                if(currentGameMap->getTile(z->getLocation())->getCityZoneDensity() > 0) {
                    require(!anyChurch || day - lastChurchChange >= std::min(DuneCity::kChurchAddSpacingDays,
                                                                             DuneCity::kChurchRemoveSpacingDays),
                            "Two Churches were designated within their spacing");
                    lastChurchChange = day; anyChurch = true;
                }
            }
            seen[id] = now;
        }
        require(hospitalChanges <= 1, "More than one Hospital changed in one census");
        maxHospitals = std::max(maxHospitals, hospitalsNow);
        if(day == lowDay - 1) require(hospitalsNow == 3, "The full city does not have its three Hospitals by day 149 ("
                                                          + std::to_string(hospitalsNow) + ")");
    }
    int hospitalsNow = 0;
    for(auto* z : zones) if(z->getCivicOverlay() == CO::Hospital) ++hospitalsNow;
    require(adds == 3 && removes == 2 && hospitalsNow == 1,
            "Expected three Hospitals made and two removed, got " + std::to_string(adds) + " and " + std::to_string(removes));
    require(populationChanges > 300, "The scripted city did not keep changing");
    require(lockedHospital != 0 && patientVisits >= 6, "The patients did not keep coming to the first Hospital");
    // The record the next save carries: every overlay with its day.
    for(const auto& [id, overlay] : care().overlays())
        require(care().overlayDays().count(id) && care().overlayDays().at(id) > 0, "An overlay has no designation day");
    record("pacing", "300 city days, " + std::to_string(populationChanges) + " zone population changes: 3 Hospitals made "
           "a quarter year apart and kept on their zones, 2 removed after 4 city years a year apart, never two changes a census; "
           + std::to_string(patientVisits) + " patient visits kept the first Hospital through days 185-205");
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
    if(wanted("bot_quiet")) checkBotQuiet(mapData);
    if(wanted("save")) checkSave(mapData);
    if(wanted("ui_repair")) checkUiRepair(mapData);
    if(wanted("walkin")) checkWalkIn(mapData);
    if(wanted("ui_no_hospital")) checkUiNoHospital(mapData);
    if(wanted("ui_beds")) checkUiBeds(mapData);
    if(wanted("ui_carryall")) checkUiCarryall(mapData);
    if(wanted("pacing")) checkPacing(mapData);
    if(wanted("metropolis")) checkMetropolis();
    gChecks.close();
    SDL_Log("HOSPITAL_CARE_PROBE_PASS");
    return -1;
}
