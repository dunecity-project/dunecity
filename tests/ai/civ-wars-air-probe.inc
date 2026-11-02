// Private balance executable only. No defended orders, money, health, rules or AI timers change.
#include <Bullet.h>
#include <Command.h>
#include <DynastyProjectile.h>
#include <players/HumanPlayer.h>
#include <players/PlayerFactory.h>
#include <units/Ornithopter.h>

#include <array>
#include <exception>
#include <optional>

namespace CivWarsGate {

// MeasuredFactories' existing init scope ends immediately after initGame, before any updates.
// This opt-in observation also proves the ordinary control really enabled/disabled research.
inline void reportOrdinarySetup(Game& game) {
    const auto& init = game.getGameInitSettings();
    const auto& options = init.getGameOptions();
    GameStateDigest::Hasher hash;
    for (const unsigned char byte : init.getFiledata()) hash.mixUint8(byte);
    int bots = 0, labs = 0;
    for (const House* house : EliminationMatch::playingHouses(game))
        for (const auto& player : house->getPlayerList())
            if (dynamic_cast<const QuantBot*>(player.get())) ++bots;
    for (const auto* structure : structureList)
        if (structure->getItemID() == Structure_ResearchLab && structure->getHealth() > 0) ++labs;
    SDL_Log("CIV_ORDINARY_SETUP: seed=%u civ_wars=%d city_sim=%d research_lab=%d observing=%d "
            "config_hash=%s options_hash=%s map_hash=%016llx map_hash_algorithm=fnv1a64 "
            "baseline_cycle=%u quantbots=%d initial_labs=%d",
        init.getRandomSeed(), int(init.isCivilizationWars()), int(game.isCitySimEnabled()), int(options.researchLab),
        int(game.isObserving()), getQuantBotConfig().getConfigHash().c_str(), options.getHash().c_str(),
        static_cast<unsigned long long>(hash.value()), game.getGameCycleCount(), bots, labs);
}

class EarlyAirStrike {
    struct Core {
        Uint32 id = NONE_ID;
        long long initial = 0, minimum = 0;
    };
    Game& game;
    House* defender = nullptr;
    House* attacker = nullptr;
    const QuantBot* attackerBot = nullptr;
    HumanPlayer* pilot = nullptr; // Owned and normally registered by the attacking House.
    std::array<Core, 2> core{}; // Preserve the ORIGINAL yard and lab, not replacements.
    Uint32 targetID = NONE_ID, aircraftID = NONE_ID;
    Uint32 strikeCycle = 0, endCycle = 0, defeatedCycle = UINT32_MAX;
    int starterCover = 0, projectileTargetCycles = 0, strikeProjectileCycles = 0;
    int humanAuthorityCycles = 0;
    int turretFires = 0, turretHits = 0, turretKills = 0;
    int launcherFires = 0, launcherHits = 0, launcherKills = 0;
    bool injected = false, lostCore = false, aircraftDamaged = false;
    bool humanAuthorityHeld = false;
    long long aircraftInitial = 0;
    std::vector<Uint32> starterTurrets;

    ObjectBase* object(Uint32 id) const { return game.getObjectManager().getObject(id); }
    bool survives(const Core& saved) const {
        const auto* live = object(saved.id);
        return live && live->getOwner() == defender && live->getHealth() > 0;
    }
    int coverage(const Coord& point, bool report) const {
        int count = 0;
        for (Uint32 id : starterTurrets) {
            const auto* turret = object(id);
            if (!turret || turret->getOwner() != defender || turret->getHealth() <= 0) continue;
            // Exactly RocketTurret::findTarget's ornithopter geometry, not ground range.
            const Coord centre = point * TILESIZE + Coord(TILESIZE / 2, TILESIZE / 2);
            const int distance = DynastyProjectile::distance(turret->getCenterPoint() * 4, centre * 4);
            const bool covers = distance <= turret->getWeaponRange() * 3 * TILESIZE * 4;
            if (covers) ++count;
            if (report)
                SDL_Log("CIV_AIR_COVER: cycle=%u turret=%u distance_256=%d air_range_256=%d covers=%d",
                    game.getGameCycleCount(), id, distance, turret->getWeaponRange() * 3 * TILESIZE * 4, int(covers));
        }
        return count;
    }

public:
    explicit EarlyAirStrike(Game& current, Uint32 end) : game(current), endCycle(end) {
        const int team = std::atoi(std::getenv("BALANCE_CIV_AIR_TEAM"));
        for (int h = 0; h < NUM_HOUSES; ++h) {
            House* house = game.getHouse(h);
            if (!house || !house->isAlive()) continue;
            if (!defender && house->getTeamID() == team) defender = house;
            if (!attacker && house->getTeamID() == (team == 1 ? 2 : 1)) attacker = house;
        }
        if (!game.getGameInitSettings().isCivilizationWars() || !defender || !attacker)
            throw std::runtime_error("Early air probe needs the canonical generated opposing teams");
        if (attacker->getPlayerList().size() != 1)
            throw std::runtime_error("Early air probe needs one real enemy controller");
        // Keep the original enemy QuantBot running. Only the injected aircraft receives
        // a genuine shared-house human order, not an AI order its planner may cancel.
        attackerBot = dynamic_cast<const QuantBot*>(attacker->getPlayerList().front().get());
        if (!attackerBot || !attackerBot->autonomousAircraftRaids())
            throw std::runtime_error("Early air probe attacker must be Hard or Brutal");
        starterTurrets.reserve(8);
        for (const auto* structure : structureList) {
            if (structure->getOwner() != defender || structure->getHealth() <= 0) continue;
            const int item = structure->getItemID();
            if (item == Structure_RocketTurret) starterTurrets.push_back(structure->getObjectID());
            const int index = item == Structure_ConstructionYard ? 0 : item == Structure_ResearchLab ? 1 : -1;
            if (index >= 0 && core[index].id == NONE_ID) {
                auto& saved = core[index];
                saved.id = structure->getObjectID();
                saved.initial = saved.minimum = structure->getHealth().getRawValue();
            }
        }
        if (core[0].id == NONE_ID || core[1].id == NONE_ID)
            throw std::runtime_error("Early air probe is missing an original generated yard/lab");
        const bool lab = std::string(std::getenv("BALANCE_CIV_AIR_PROBE")) == "lab";
        targetID = core[lab ? 1 : 0].id;
        const auto* target = object(targetID);
        const Coord location = target->getLocation();
        const auto* structure = static_cast<const StructureBase*>(target);
        const Coord centre = location + Coord(structure->getStructureSizeX() / 2, structure->getStructureSizeY() / 2);
        starterCover = coverage(centre, true);
        // The generator grants two core rocket turrets. One full-health aircraft against at
        // least two already covering turrets is a bounded sortie, not an impossible swarm.
        if (starterCover < 2)
            throw std::runtime_error("Early air probe requires two starter rocket turrets covering its core target");
        strikeCycle = game.getGameCycleCount() + MILLI2CYCLES(30000);
        if (endCycle < strikeCycle + MILLI2CYCLES(60000))
            throw std::runtime_error("Early air probe needs at least a minute after the 30-second strike");
        SDL_Log("CIV_AIR_SETUP: seed=%u defender=%d defender_team=%d attacker=%d target=%u target_kind=%s "
                "yard=%u lab=%u strike_cycle=%u end_cycle=%u starter_cover=%d strike_count=1 "
                "strength_policy=one_aircraft_for_two_starter_turrets health_policy=engine_default "
                "defender_orders=unmodified defender_updates=real strike_authority=registered_human_command "
                "attacker_quantbot=unmodified",
            game.getGameInitSettings().getRandomSeed(), defender->getHouseID(), team, attacker->getHouseID(), targetID,
            lab ? "lab" : "yard", core[0].id, core[1].id, strikeCycle, endCycle, starterCover);
    }

    void beforeUpdate() {
        if (injected || game.getGameCycleCount() != strikeCycle) return;
        if (!survives(core[0]) || !survives(core[1]))
            throw std::runtime_error("Early air probe lost a core before its strike");
        auto* target = object(targetID);
        const auto* structure = static_cast<const StructureBase*>(target);
        const Coord centre = target->getLocation()
            + Coord(structure->getStructureSizeX() / 2, structure->getStructureSizeY() / 2);
        // Approach from the enemy-facing side, on a real map tile; no later teleports,
        // retargeting, damage injection, repairs, grants or extra reinforcements.
        const Coord spawn = centre + Coord(defender->getTeamID() == 1 ? 8 : -8, 0);
        if (!currentGameMap->tileExists(spawn) || coverage(spawn, false) < 2)
            throw std::runtime_error("Early air approach is not covered by two original starter turrets");
        // House::addPlayer is the normal attachment/registration API. Adding a copilot
        // after setup preserves the canonical map, observer, initial bot roster and
        // BOTH original QuantBots; no defended controller, policy or timer changes.
        const auto* factory = PlayerFactory::getByPlayerClass(HUMANPLAYERCLASS);
        if (!factory) throw std::runtime_error("Early air probe cannot find the normal HumanPlayer factory");
        auto human = factory->create(attacker, "Civ Wars strike pilot");
        pilot = dynamic_cast<HumanPlayer*>(human.get());
        if (!pilot || pilot->getPlayerclass() != HUMANPLAYERCLASS)
            throw std::runtime_error("Early air probe factory did not create a genuine HumanPlayer");
        attacker->addPlayer(std::move(human));
        if (game.getPlayerByID(pilot->getPlayerID()) != pilot || !game.isObserving()
            || defender->getPlayerList().size() != 1 || attacker->getPlayerList().size() != 2)
            throw std::runtime_error("Early air probe did not register its genuine attacking human copilot");
        SDL_Log("CIV_AIR_PILOT: cycle=%u house=%d player=%u class=%s registered=1 "
                "attachment=House.addPlayer attacker_players=%zu defender_players=%zu attacker_quantbot_player=%u",
            game.getGameCycleCount(), attacker->getHouseID(), pilot->getPlayerID(), pilot->getPlayerclass().c_str(),
            attacker->getPlayerList().size(), defender->getPlayerList().size(), attackerBot->getPlayerID());
        auto* aircraft = attacker->createUnit(Unit_Ornithopter);
        if (!aircraft) throw std::runtime_error("Cannot create the real strike ornithopter");
        aircraft->deploy(spawn);
        aircraftID = aircraft->getObjectID();
        aircraftInitial = aircraft->getHealth().getRawValue();
        if (aircraft->getHealth() != aircraft->getMaxHealth())
            throw std::runtime_error("Early strike aircraft did not receive ordinary engine health");
        const auto& stats = game.combatStats;
        turretFires = stats.rocketTurretFiresOnOrni;
        turretHits = stats.turretRocketsHitOrni;
        turretKills = stats.turretRocketsKillOrni;
        launcherFires = stats.launcherFiresOnOrni;
        launcherHits = stats.launcherRocketsHitOrni;
        launcherKills = stats.launcherRocketsKillOrni;
        // Once through the real human command path: Command::executeCommand itself
        // grants the existing shared-house lease. Never edit or refresh that ledger.
        Command(pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, aircraftID, targetID).executeCommand();
        humanAuthorityHeld = attackerBot->humanControls(aircraft);
        if (aircraft->getTarget() != target || !humanAuthorityHeld)
            throw std::runtime_error("Real human ornithopter attack command or its ordinary lease was not accepted");
        injected = true;
        SDL_Log("CIV_AIR_STRIKE: cycle=%u object=%u owner=%d target=%u x=%d y=%d health_raw=%lld "
                "max_health=%d command=CMD_UNIT_ATTACKOBJECT accepted=1 count=1 "
                "issuer=%u issuer_class=HumanPlayer human_lease=1",
            game.getGameCycleCount(), aircraftID, attacker->getHouseID(), targetID, spawn.x, spawn.y,
            aircraftInitial, aircraft->getMaxHealth(), pilot->getPlayerID());
    }

    void observe() {
        for (auto& saved : core) {
            if (!survives(saved)) { saved.minimum = 0; lostCore = true; }
            else saved.minimum = std::min(saved.minimum, static_cast<long long>(object(saved.id)->getHealth().getRawValue()));
        }
        if (!injected) return;
        const auto* aircraft = object(aircraftID);
        if (aircraft && aircraft->getHealth() > 0) {
            ++humanAuthorityCycles;
            humanAuthorityHeld = humanAuthorityHeld
                && attackerBot->humanControls(static_cast<const UnitBase*>(aircraft));
        }
        if (aircraft && aircraft->getHealth().getRawValue() < aircraftInitial) aircraftDamaged = true;
        if ((!aircraft || aircraft->getHealth() <= 0) && defeatedCycle == UINT32_MAX) {
            defeatedCycle = game.getGameCycleCount();
            SDL_Log("CIV_AIR_DEFEATED: cycle=%u object=%u", defeatedCycle, aircraftID);
        }
        // Read the actual engine projectile list. This counts occupied simulation cycles,
        // NOT shots: one homing missile can be visible for many cycles. Telemetry attributes
        // the lethal hit independently, so deletion/despawn cannot masquerade as a kill.
        bool defence = false, strike = false;
        for (const auto* bullet : bulletList) {
            if (bullet->target.getObjectID() == aircraftID && bullet->owner
                && bullet->owner->getTeamID() == defender->getTeamID()) defence = true;
            if (bullet->shooterID == aircraftID) strike = true;
        }
        if (defence) ++projectileTargetCycles;
        if (strike) ++strikeProjectileCycles;
    }

    void finish() const {
        const auto& stats = game.combatStats;
        const bool survived = !lostCore && survives(core[0]) && survives(core[1]) && defender->isAlive();
        const bool pass = injected && defeatedCycle != UINT32_MAX && survived && projectileTargetCycles > 0
            && humanAuthorityHeld && game.getGameCycleCount() == endCycle;
        SDL_Log("CIV_AIR_RESULT: pass=%d cycle=%u end_cycle=%u injected=%d strike_count=%d defeated=%d "
                "defeated_cycle=%u core_survives=%d yard_survives=%d lab_survives=%d "
                "yard_initial_raw=%lld yard_min_raw=%lld lab_initial_raw=%lld lab_min_raw=%lld "
                "defence_projectile_target_cycles=%d strike_projectile_cycles=%d aircraft_damage_observed=%d "
                "turret_fires_global=%d turret_hits_global=%d turret_kills_global=%d "
                "launcher_fires_global=%d launcher_hits_global=%d launcher_kills_global=%d "
                "human_authority_held=%d human_authority_cycles=%d "
                "kill_attribution=requires_complete_lethal_damage_telemetry",
            int(pass), game.getGameCycleCount(), endCycle, int(injected), int(injected), int(defeatedCycle != UINT32_MAX),
            defeatedCycle, int(survived), int(survives(core[0])), int(survives(core[1])),
            core[0].initial, core[0].minimum, core[1].initial, core[1].minimum,
            projectileTargetCycles, strikeProjectileCycles, int(aircraftDamaged),
            stats.rocketTurretFiresOnOrni - turretFires, stats.turretRocketsHitOrni - turretHits,
            stats.turretRocketsKillOrni - turretKills, stats.launcherFiresOnOrni - launcherFires,
            stats.launcherRocketsHitOrni - launcherHits, stats.launcherRocketsKillOrni - launcherKills,
            int(humanAuthorityHeld), humanAuthorityCycles);
    }
};

} // namespace CivWarsGate
