// Private real-engine entry point, linked against the production objects by the adjacent runner.
// Capture treatments issue only ordinary attack/Hunt/acquisition orders. Explicit Capture is used
// solely to prepare pre-existing Soldier occupants; no chooser or capture helper is called here.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <CommandManager.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <players/AIDecisionLog.h>
#include <players/QuantBotConfig.h>
#include <structures/BuilderBase.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <dunecity/InfantrySquadPolicy.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <dunecity/StructureDegradation.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace InfantryZoneCaptureProbe {
constexpr int mapWidth = 64, mapHeight = 48;
constexpr Uint32 noCycle = std::numeric_limits<Uint32>::max();
constexpr std::array<int, 3> zoneKinds{
    Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial};
SettingsClass::GameOptionsClass modOptions;
QuantBotConfig probeConfig;

void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error(why);
}

// The one option-setting site: observations below always use the gameplay authority, not this field.
void setPermission(SettingsClass::GameOptionsClass& options, bool enabled) {
    options.garrisoningAndCapturing = enabled;
}

std::string fixtureMap() {
    std::string text = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n"
                       "[MAP]\nSizeX=" + std::to_string(mapWidth) + "\nSizeY=" + std::to_string(mapHeight) + "\n";
    for(int y = 0; y < mapHeight; ++y) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", y);
        text += key + std::string(mapWidth, '%') + "\n";
    }
    return text + "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n"
        "[UNITS]\nID001=Player1,MCV,256,0,64,Guard\nID002=Player2,MCV,256,"
        + std::to_string(mapWidth * mapHeight - 1) + ",64,Guard\n";
}

enum class Roster { AI, Human, Mixed };

struct Fixture {
    std::string name;
    std::unique_ptr<Game> game;
    House* own = nullptr;
    House* enemy = nullptr;
    QuantBot* bot = nullptr;
    HumanPlayer* human = nullptr;
    Player* opponent = nullptr;

    Fixture(std::string label, Roster roster = Roster::AI, bool permission = true,
            bool rush = false, bool squads = false) : name(std::move(label)) {
        getQuantBotConfig() = probeConfig;
        auto& force = getQuantBotConfig().force;
        force.zoneGarrison = force.zoneGarrisonReinforce = rush;
        force.zoneFrontGarrison = force.zoneCaptureRaid = force.zoneCaptureLevy = force.zoneCaptureChain = false;
        force.squadOrders = squads;
        force.infantryInMix = force.infantryLanes = false;
        auto rules = modOptions;
        setPermission(rules, permission);
        rules.fogOfWar = false;
        rules.startWithExploredMap = true;
        rules.concreteRequired = false;
        rules.immortalHumanPlayer = false;
        rules.instantBuild = false;
        GameInitSettings init("infantry-zone-capture.ini", fixtureMap(), "Infantry zone probe", true, rules);
        init.randomSeed = 20261128;
        GameInitSettings::HouseInfo owners(HOUSE_HARKONNEN, 1);
        if(roster != Roster::Human) owners.addPlayerInfo({"Capture QuantBot", "qBotBrutal"});
        if(roster != Roster::AI) owners.addPlayerInfo({"Capture human", HUMANPLAYERCLASS});
        init.addHouseInfo(owners);
        GameInitSettings::HouseInfo observer(HOUSE_ATREIDES, 2);
        observer.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
        init.addHouseInfo(observer);
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        own = game->getHouse(HOUSE_HARKONNEN);
        enemy = game->getHouse(HOUSE_ATREIDES);
        require(own && enemy && game->isCitySimEnabled(), name + ": real city houses missing");
        for(const auto& player : own->getPlayerList()) {
            if(auto* q = dynamic_cast<QuantBot*>(player.get())) bot = q;
            if(auto* h = dynamic_cast<HumanPlayer*>(player.get())) human = h;
        }
        opponent = game->getPlayerByName(settings.general.playerName);
        require(opponent && pLocalHouse == enemy, name + ": observer/command issuer missing");
        require(bool(bot) == (roster != Roster::Human) && bool(human) == (roster != Roster::AI),
                name + ": real controller roster differs from fixture");
        // Builder suppresses strategic rally/defence interference, not native UnitBase targeting.
        // Rush exercises the normal Both controller, including its real role registries.
        if(bot) bot->setRole(rush ? AIRole::Both : AIRole::Builder);
        require(ZoneStructure::garrisonsEnabled() == permission, name + ": effective permission differs");
        for(House* house : {own, enemy}) {
            house->startingCredits = house->storedCredits = house->cityCredits = 0;
            house->enforceCreditCeiling();
        }
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    ObjectBase* object(Uint32 id) const { return game->getObjectManager().getObject(id); }
    UnitBase* unit(Uint32 id) const { return dynamic_cast<UnitBase*>(object(id)); }
    ZoneStructure* zone(Uint32 id) const { return dynamic_cast<ZoneStructure*>(object(id)); }
    Player* issuer() const { return bot ? static_cast<Player*>(bot) : static_cast<Player*>(human); }
    void queue(const Command& command) { game->getCommandManager().addCommand(command); }
    void step() {
        const Uint32 before = game->getGameCycleCount();
        game->updateGameState();
        require(game->getGameCycleCount() > before, name + ": native game cycle did not advance");
    }
    template<class F> bool until(int seconds, F done) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) {
            if(done()) return true;
            step();
        }
        return done();
    }
    void run(int seconds) { until(seconds, [] { return false; }); }

    StructureBase* place(House* house, int item, Coord at) {
        const Coord size = getStructureSize(item);
        for(int y = at.y; y < at.y + size.y; ++y) for(int x = at.x; x < at.x + size.x; ++x) {
            require(currentGameMap->tileExists(x, y) && !currentGameMap->getTile(x, y)->hasAGroundObject(),
                    name + ": occupied fixture footprint");
            currentGameMap->getTile(x, y)->setType(Terrain_Rock);
        }
        auto* result = house->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(result != nullptr, name + ": real structure placement failed");
        result->setVisible(VIS_ALL, true);
        return result;
    }
    ZoneStructure* addZone(House* house, int item = Structure_ZoneResidential, Coord at = Coord(32, 20)) {
        auto* result = dynamic_cast<ZoneStructure*>(place(house, item, at));
        require(result != nullptr, name + ": R/C/I structure missing");
        result->setStartingLevel(2);
        return result;
    }
    void deploy(UnitBase* unit, Coord at) {
        require(unit && currentGameMap->tileExists(at)
                && !currentGameMap->getTile(at)->hasANonInfantryGroundObject(), name + ": invalid deployment");
        unit->deploy(at);
        unit->setVisible(VIS_ALL, true);
        require(unit->isActive() && unit->getHealth() > 0, name + ": deployment did not activate the real unit");
    }
    UnitBase* spawn(House* house, int item, Coord at) {
        auto* result = house->createUnit(item, true);
        require(result != nullptr, name + ": unit creation failed");
        deploy(result, at);
        return result;
    }
    bool inside(Uint32 unitID, const ZoneStructure* target) const {
        return target && std::find(target->getGarrison().begin(), target->getGarrison().end(), unitID)
            != target->getGarrison().end();
    }
    std::vector<Uint32> soldierOccupants(ZoneStructure* target, int count) {
        const AIRole saved = bot ? bot->getRole() : AIRole::Both;
        if(bot) bot->setRole(AIRole::Builder);
        std::vector<Uint32> ids;
        Player* player = target->getOwner() == own ? issuer() : opponent;
        for(int i = 0; i < count; ++i) {
            auto* man = spawn(target->getOwner(), Unit_Soldier, target->getLocation() + Coord(-1, i % 2));
            ids.push_back(man->getObjectID());
            queue(Command(player->getPlayerID(), CMD_INFANTRY_CAPTURE, man->getObjectID(), target->getObjectID()));
        }
        require(until(5, [&] { return target->getGarrisonSize() == count; }), name + ": setup Soldiers never entered");
        for(Uint32 id : ids) require(inside(id, target), name + ": setup changed Soldier identity");
        if(bot) bot->setRole(saved);
        return ids;
    }
};

enum class Exclusion { None, Health, Permission, Full, Unreachable, HumanOrder, NonZone };
struct CaptureTotals { int cases = 0, eligible = 0, engagements = 0, captures = 0, excluded = 0; long long damage = 0; } captureTotals;

// Only handleDamage combat records carry the actual attacking object ID. A net target-HP
// decrease also includes StructureBase's independent power tick, which bypasses that path.
long long combatInteger(const std::string& row, std::string_view key) {
    const auto offset = row.find(key);
    if(offset == std::string::npos) throw std::runtime_error("Missing combat telemetry field");
    const char* first = row.data() + offset + key.size();
    const char* end = row.data() + row.size();
    long long value = 0;
    const auto parsed = std::from_chars(first, end, value);
    if(parsed.ec != std::errc() || parsed.ptr == end || (*parsed.ptr != ',' && *parsed.ptr != '}'))
        throw std::runtime_error("Invalid combat telemetry integer");
    return value;
}

struct Engagement {
    Uint32 unitID, targetID, lastCycle;
    const House* attacker;
    int attackerItem;
    FixPoint lastHealth;
    int lastPowerTimer = 0, lastProduced = 0, lastRequired = 0, lastOwner = -1;
    Sint32 lastDamageStat;
    uint64_t lastHitStat;
    bool engaged = false, captured = false;
    long long damage = 0, hpRemovedMilli = 0, grossHPDecline = 0, powerDamage = 0, cohortDamage = 0;
    uint64_t combatHits = 0, cohortHits = 0;
    int powerTicks = 0;
    std::string telemetryPath;

    void rememberPower(const ObjectBase* target) {
        if(const auto* structure = dynamic_cast<const StructureBase*>(target)) {
            lastPowerTimer = structure->powerDamageTimer;
            lastProduced = structure->getOwner()->getProducedPower();
            lastRequired = structure->getOwner()->getPowerRequirement();
            lastOwner = structure->getOwner()->getHouseID();
        }
    }
    Engagement(Fixture& f, const UnitBase* man, const ObjectBase* target)
        : unitID(man->getObjectID()), targetID(target->getObjectID()),
          lastCycle(f.game->getGameCycleCount()), attacker(man->getOwner()), attackerItem(man->getItemID()),
          lastHealth(target->getHealth()), lastDamageStat(attacker->getNumItemDamageInflicted(attackerItem)),
          lastHitStat(attacker->getCombatReward(attackerItem).hits) {
        rememberPower(target);
        const char* profile = std::getenv("DUNECITY_USERDIR");
        require(profile && *profile, f.name + ": isolated probe profile missing");
        auto& log = AITelemetry::log();
        require(log.start(std::string(profile) + "/capture-damage",
                    AITelemetry::Record().set("case", f.name).set("unit", unitID).set("target", targetID)),
                f.name + ": cannot start independent combat attribution");
        telemetryPath = log.path();
    }
    void sample(Fixture& f) {
        auto* target = f.object(targetID);
        const FixPoint health = target ? target->getHealth() : FixPoint(0);
        const auto decline = health < lastHealth
            ? static_cast<long long>((lastHealth - health).getRawValue()) : 0;
        grossHPDecline += decline;

        // This conservative, independent guard covers every shooter of this house/type,
        // not just this ID. Signed raw damage also sees a late friendly hit after capture;
        // monotonic hostile-hit totals prevent opposite-signed hits cancelling within a tick.
        const Sint32 damageStat = attacker->getNumItemDamageInflicted(attackerItem);
        const auto hitStat = attacker->getCombatReward(attackerItem).hits;
        const long long delta = static_cast<long long>(damageStat) - lastDamageStat;
        cohortDamage += (delta < 0 ? -delta : delta) * FixPoint(1).getRawValue();
        require(hitStat >= lastHitStat, f.name + ": combat-hit ledger moved backwards");
        cohortHits += hitStat - lastHitStat;
        lastDamageStat = damageStat;
        lastHitStat = hitStat;

        const Uint32 cycle = f.game->getGameCycleCount();
        if(cycle != lastCycle) {
            require(cycle == lastCycle + 1, f.name + ": HP observer skipped a native cycle");
            const auto* structure = dynamic_cast<const StructureBase*>(target);
            if(structure && lastPowerTimer == 1) {
                require(structure->powerDamageTimer == DuneCity::Degradation::powerDamageIntervalCycles(),
                        f.name + ": power timer did not complete its real tick");
                ++powerTicks;
                const int threshold = DuneCity::Degradation::powerHitpointsMax(
                    structure->getMaxHealth(), lastProduced, lastRequired);
                const long long expected = lastHealth > FixPoint(threshold)
                    ? static_cast<long long>(FixPoint(DuneCity::Degradation::kPowerDamagePerInterval).getRawValue()) : 0;
                powerDamage += expected;
                std::printf("INFANTRY_ZONE_DAMAGE_SOURCE: name=%s unit=%u target=%u cycle=%u "
                            "source=%s expected_raw=%lld gross_decline_raw=%lld "
                            "owner=%d produced=%d required=%d threshold=%d timer_before=%d timer_after=%d "
                            "concrete_required=%d\n",
                    f.name.c_str(), unitID, targetID, lastCycle,
                    expected > 0 ? "power_shortage" : "power_tick_no_loss", expected, decline, lastOwner,
                    lastProduced, lastRequired, threshold, lastPowerTimer, structure->powerDamageTimer,
                    int(f.game->getGameInitSettings().getGameOptions().concreteRequired));
            }
        }
        lastCycle = cycle;
        lastHealth = health;
        rememberPower(target);
        const auto* man = f.unit(unitID);
        if(man && man->getTarget() && man->getTarget()->getObjectID() == targetID) engaged = true;
        const auto* zone = dynamic_cast<const ZoneStructure*>(target);
        if(f.inside(unitID, zone)) {
            require(man && !man->isActive() && man->isPickedUp() && zone->getOwner() == man->getOwner(),
                    f.name + ": ID listed inside without actual hidden entry/ownership");
            captured = engaged = true;
        }
    }
    void finishDamage(Fixture& f) {
        auto& log = AITelemetry::log();
        require(log.enabled() && log.path() == telemetryPath && !log.captureLimited && log.stream.good(),
                f.name + ": incomplete combat telemetry cannot prove zero damage");
        log.stop(); // Flush the real hit records, including the projectile-drain interval.
        std::ifstream input(telemetryPath);
        require(input.is_open(), f.name + ": combat telemetry missing");
        bool ended = false;
        std::string row;
        while(std::getline(input, row)) {
            require(row.find("\"event\":\"capture_limit\"") == std::string::npos,
                    f.name + ": truncated combat telemetry");
            if(row.find("\"event\":\"session_end\"") != std::string::npos) ended = true;
            if(row.find("\"event\":\"combat_target_hit\"") == std::string::npos) continue;
            if(combatInteger(row, "\"attacker\":") != unitID
               || combatInteger(row, "\"target\":") != targetID) continue;
            require(combatInteger(row, "\"attacker_item\":") == attackerItem,
                    f.name + ": combat source ID changed infantry type");
            const auto before = combatInteger(row, "\"hp_before_milli\":");
            const auto after = combatInteger(row, "\"hp_after_milli\":");
            require(before > after && after >= 0, f.name + ": combat hit did not remove actual HP");
            hpRemovedMilli += before - after;
            ++combatHits;
        }
        require(input.eof() && !input.bad() && ended, f.name + ": combat telemetry did not close completely");
        // Production hit statistics use milli-HP. The zero/nonzero assertion loses no precision;
        // these controlled infantry hits are integer HP, below the fresh lot's remaining HP.
        damage = hpRemovedMilli * FixPoint(1).getRawValue() / 1000;
    }
};

void observeAttack(Fixture& f, UnitBase* man, ObjectBase* target, Exclusion exclusion,
                   const std::function<void()>& stimulus) {
    const bool eligible = exclusion == Exclusion::None;
    if(eligible) {
        auto* zone = dynamic_cast<ZoneStructure*>(target);
        require(zone && ZoneStructure::garrisonsEnabled() && zone->mayEnterGarrison(man),
                f.name + ": controlled capture engagement was not initially eligible");
    }
    const Uint32 began = f.game->getGameCycleCount();
    const auto healthRaw = static_cast<long long>(man->getHealth().getRawValue());
    Engagement observation(f, man, target);
    stimulus();
    observation.sample(f);
    require(!observation.captured, f.name + ": treatment started in entry distance instead of exercising game cycles");
    f.until(40, [&] {
        observation.sample(f);
        return eligible ? observation.captured : observation.cohortDamage > 0;
    });
    // Drain any already-fired projectile: a late hit must not be hidden by a quick ownership change.
    const Uint32 settle = f.game->getGameCycleCount() + MILLI2CYCLES(2000);
    while(f.game->getGameCycleCount() < settle) { f.step(); observation.sample(f); }
    observation.finishDamage(f);
    std::printf("INFANTRY_ZONE_CAPTURE_RESULT: name=%s unit=%u item=%d target=%u health_raw=%lld "
                "eligible=%d engagements=%d captures=%d captured_unit=%u damage_raw=%lld permission=%d "
                "excluded_health=%d excluded_permission=%d excluded_full=%d excluded_unreachable=%d "
                "excluded_human=%d excluded_nonzone=%d cycles=%u gross_hp_decline_raw=%lld "
                "power_damage_raw=%lld power_ticks=%d combat_hp_removed_milli=%lld combat_hits=%llu "
                "cohort_damage_raw=%lld cohort_hits=%llu\n",
        f.name.c_str(), observation.unitID, man->getItemID(), observation.targetID, healthRaw,
        int(eligible), int(observation.engaged), int(observation.captured),
        observation.captured ? observation.unitID : NONE_ID, observation.damage, int(ZoneStructure::garrisonsEnabled()),
        int(exclusion == Exclusion::Health), int(exclusion == Exclusion::Permission), int(exclusion == Exclusion::Full),
        int(exclusion == Exclusion::Unreachable), int(exclusion == Exclusion::HumanOrder), int(exclusion == Exclusion::NonZone),
        f.game->getGameCycleCount() - began, observation.grossHPDecline, observation.powerDamage,
        observation.powerTicks, observation.hpRemovedMilli, static_cast<unsigned long long>(observation.combatHits),
        observation.cohortDamage, static_cast<unsigned long long>(observation.cohortHits));
    std::fflush(stdout);
    require(observation.engaged, f.name + ": no actual target engagement (zero activation)");
    require(eligible ? observation.captured && observation.damage == 0 && observation.combatHits == 0
                         && observation.cohortDamage == 0 && observation.cohortHits == 0
                         && observation.grossHPDecline == observation.powerDamage
                     : !observation.captured && observation.damage > 0 && observation.combatHits > 0,
            f.name + (eligible ? ": eligible infantry did not physically capture without damage"
                               : ": excluded engagement did not retain an actual damaging attack"));
    ++captureTotals.cases;
    captureTotals.engagements += int(observation.engaged);
    captureTotals.eligible += int(eligible);
    captureTotals.captures += int(eligible && observation.captured);
    captureTotals.excluded += int(!eligible);
    if(eligible) captureTotals.damage += observation.damage;
}

void explicitAttack(const std::string& name, int kind, Roster roster, bool humanIssuer, bool queued,
                    bool permission = true, bool nonZone = false, bool forcedHunt = false) {
    Fixture f(name, roster, permission);
    auto* target = nonZone ? f.place(f.enemy, Structure_WindTrap, {32, 20})
                          : static_cast<StructureBase*>(f.addZone(f.enemy, kind));
    auto* man = f.spawn(f.own, Unit_Trooper, {29, 20});
    Player* player = humanIssuer ? static_cast<Player*>(f.human) : static_cast<Player*>(f.bot);
    require(player != nullptr, name + ": requested real issuer missing");
    const Exclusion exclusion = !permission ? Exclusion::Permission : nonZone ? Exclusion::NonZone
        : humanIssuer ? Exclusion::HumanOrder : Exclusion::None;
    observeAttack(f, man, target, exclusion, [&] {
        if(forcedHunt) {
            f.queue(Command(player->getPlayerID(), CMD_UNIT_SETMODE, man->getObjectID(), HUNT));
            f.queue(Command(player->getPlayerID(), CMD_UNIT_ATTACKOBJECT, man->getObjectID(), target->getObjectID()));
        } else if(queued) {
            f.queue(Command(player->getPlayerID(), CMD_UNIT_ATTACKOBJECT, man->getObjectID(), target->getObjectID()));
        } else player->doAttackObject(man, target, true);
    });
}

void hunt(const std::string& name, bool ai, int boundary, int kind, bool permission = true, int item = Unit_Trooper) {
    Fixture f(name, ai ? Roster::AI : Roster::Human, permission);
    auto* target = f.addZone(f.enemy, kind);
    auto* man = f.spawn(f.own, item, {29, 20});
    const FixPoint half = FixPoint(man->getMaxHealth()) / 2;
    man->setHealth(half + FixPoint::FromRawValue(boundary));
    require(man->getHealth() == half + FixPoint::FromRawValue(boundary), name + ": health fixture rounded the raw boundary");
    require((man->getHealth() * 2 >= FixPoint(man->getMaxHealth())) == (boundary >= 0),
            name + ": exact fixed-point half-health boundary differs");
    observeAttack(f, man, target, !permission ? Exclusion::Permission
        : boundary < 0 ? Exclusion::Health : Exclusion::None, [&] {
        f.queue(Command(f.issuer()->getPlayerID(), CMD_UNIT_SETMODE, man->getObjectID(), HUNT));
    });
}

void woundedAttack() {
    Fixture f("ai_wounded_attack");
    auto* target = f.addZone(f.enemy);
    auto* man = f.spawn(f.own, Unit_Trooper, {29, 20});
    man->setHealth(FixPoint(man->getMaxHealth()) / 2 - FixPoint::FromRawValue(1));
    require(man->getHealth() * 2 < FixPoint(man->getMaxHealth()), f.name + ": unit is not really below half health");
    observeAttack(f, man, target, Exclusion::None, [&] { f.bot->doAttackObject(man, target, true); });
}

void autonomous(bool attackMove) {
    Fixture f(attackMove ? "ai_attack_move" : "ai_idle");
    auto* target = f.addZone(f.enemy, attackMove ? Structure_ZoneCommercial : Structure_ZoneResidential);
    auto* man = f.spawn(f.own, Unit_Trooper, {29, 20});
    require(!man->getTarget(), f.name + ": deployment already assigned a target");
    observeAttack(f, man, target, Exclusion::None, [&] {
        if(attackMove) f.queue(Command(f.bot->getPlayerID(), CMD_UNIT_ATTACKMOVE, man->getObjectID(), 40, 20, 0));
        // Idle case has deliberately no order; UnitBase acquires the visible enemy zone itself.
    });
}

void blocked(bool full) {
    Fixture f(full ? "full_enemy" : "unreachable");
    auto* target = f.addZone(f.enemy, Structure_ZoneIndustrial);
    std::vector<Uint32> defenders;
    if(full) defenders = f.soldierOccupants(target, DuneCity::ZoneGarrison::kCapacity);
    else {
        const Coord at = target->getLocation(), size = target->getStructureSize();
        for(int y = at.y - 1; y <= at.y + size.y; ++y) for(int x = at.x - 1; x <= at.x + size.x; ++x) {
            if(x >= at.x && x < at.x + size.x && y >= at.y && y < at.y + size.y) continue;
            f.place(f.own, Structure_Wall, {x, y});
        }
    }
    auto* man = f.own->createUnit(Unit_Trooper, true);
    require(man && man->getWeaponRange() >= 3, f.name + ": Trooper cannot fire across the controlled separation");
    f.deploy(man, target->getLocation() - Coord(man->getWeaponRange(), 0));
    if(full) {
        require(!target->mayEnterGarrison(man) && target->heldAgainst(man)
                && target->getGarrisonSize() == DuneCity::ZoneGarrison::kCapacity, f.name + ": enemy shelter was not really full/held");
        for(Uint32 id : defenders) require(f.unit(id)->getWeaponRange() < man->getWeaponRange(),
                                        f.name + ": setup does not isolate the attacking Trooper's shot");
    } else {
        require(target->mayEnterGarrison(man), f.name + ": sealed empty zone failed entry permission rather than reachability");
        const Coord at = target->getLocation(), size = target->getStructureSize();
        for(int y = at.y - 1; y <= at.y + size.y; ++y) for(int x = at.x - 1; x <= at.x + size.x; ++x) {
            if(x >= at.x && x < at.x + size.x && y >= at.y && y < at.y + size.y) continue;
            require(!man->canPathThrough(x, y), f.name + ": supposedly sealed approach has a passable entry tile");
        }
    }
    observeAttack(f, man, target, full ? Exclusion::Full : Exclusion::Unreachable,
                  [&] { f.bot->doAttackObject(man, target, true); });
    require(target->getOwner() == f.enemy, f.name + ": excluded zone changed owner");
}

void squadChildren() {
    Fixture f("squad_children", Roster::AI, true, false, true);
    f.bot->setRole(AIRole::Off); // Only the queued production order may buy units in this fixture.
    for(int x : {3, 6, 9, 12}) f.place(f.own, Structure_WindTrap, {x, 3});
    f.place(f.own, Structure_Radar, {3, 7});
    f.place(f.own, Structure_Barracks, {7, 7});
    auto* wor = dynamic_cast<BuilderBase*>(f.place(f.own, Structure_WOR, {12, 12}));
    require(wor != nullptr, f.name + ": real WOR missing");
    wor->updateBuildList();
    int price = -1;
    for(const BuildItem& item : wor->getBuildList()) if(item.itemID == Unit_Troopers) price = static_cast<int>(item.price);
    require(price == 200 && DuneCity::InfantrySquad::orderSize(Unit_Troopers) == 3,
            f.name + ": normal discounted three-Trooper product missing");
    f.own->startingCredits = 20000;
    f.own->enforceCreditCeiling();
    f.queue(Command(f.bot->getPlayerID(), CMD_BUILDER_PRODUCEITEM, wor->getObjectID(), Unit_Troopers, 0));
    std::vector<Uint32> children;
    const int bound = f.game->objectData.data[Unit_Troopers][f.own->getHouseID()].buildtime * 15 / 62 + 40;
    require(f.until(bound, [&] {
        children.clear();
        for(const auto* unit : unitList) if(unit->getOwner() == f.own && unit->getItemID() == Unit_Trooper
                && unit->isActive() && unit->getHealth() > 0) children.push_back(unit->getObjectID());
        return children.size() == 3 && wor->getProductionQueueSize() == 0;
    }), f.name + ": a real squad order did not deliver three individual Troopers");
    f.bot->setRole(AIRole::Builder);
    std::sort(children.begin(), children.end());
    std::printf("INFANTRY_ZONE_SQUAD: children=%zu price=%d first=%u second=%u third=%u\n",
                children.size(), price, children[0], children[1], children[2]);
    // Independent lots keep every engagement hostile at onset, including later children.
    std::string failures;
    for(size_t i = 0; i < children.size(); ++i) {
        try {
            auto* target = f.addZone(f.enemy, zoneKinds[i], {25 + int(i) * 4, 12});
            auto* man = f.unit(children[i]);
            require(man && man->getItemID() == Unit_Trooper, f.name + ": delivered child identity disappeared");
            observeAttack(f, man, target, Exclusion::None, [&] { f.bot->doAttackObject(man, target, true); });
        } catch(const std::exception& error) {
            std::printf("INFANTRY_ZONE_SQUAD_CHILD: unit=%u pass=0 reason=%s\n", children[i], error.what());
            if(!failures.empty()) failures += "; ";
            failures += std::to_string(children[i]) + ": " + error.what();
        }
    }
    require(failures.empty(), f.name + ": delivered children failed: " + failures);
}

// Rush observations are independent of the controller's order proposals/logs. Availability is
// snapshotted BEFORE its next pass; every completion is an original Trooper ID physically inside.
using Role = QuantBot::UnitRole;
Role role(const Fixture& f, const UnitBase* unit) {
    return f.bot->unitRole(unit, {Role::Human, Role::Repair, Role::BaseDefence, Role::LiveDefence,
        Role::Defence, Role::Wave, Role::Escort, Role::ColonyGuard});
}
bool attacking(const Fixture& f, const UnitBase* unit) {
    const auto* target = unit->getTarget();
    return target && target->isActive() && target->getHealth() > 0
        && target->getOwner()->getTeamID() != f.own->getTeamID();
}
struct RushTotals {
    int eligible = 0, entries = 0, fast = 0, protectedSamples = 0;
    int nearEligible = 0, nearFast = 0, timelyOrders = 0;
} rushTotals;
struct RushReading {
    Uint32 zoneID, nearest = NONE_ID, ordered = NONE_ID, first = NONE_ID;
    Uint32 onset, orderCycle = noCycle, entryCycle = noCycle;
    int available = 0, excludedAttack = 0, excludedDefence = 0, excludedRange = 0, excludedOther = 0;
    FixPoint distance = 0, enemyDistance = 0;
    bool damaged = false, air = false;
    Uint32 lostCycle = noCycle;
    bool zoneLost = false, unitDead = false;
};

void logAvailability(const Fixture& f, const RushReading& read) {
    std::printf("INFANTRY_ZONE_RUSH_ONSET: name=%s zone=%u onset=%u available=%d nearest=%u distance_raw=%lld "
                "enemy_distance_raw=%lld damaged=%d air=%d before_order=1 excluded_attack=%d excluded_defence=%d "
                "excluded_range=%d excluded_other=%d excluded_no_free=%d\n", f.name.c_str(), read.zoneID, read.onset,
        read.available, read.nearest, static_cast<long long>(read.distance.getRawValue()),
        static_cast<long long>(read.enemyDistance.getRawValue()), int(read.damaged), int(read.air),
        read.excludedAttack, read.excludedDefence, read.excludedRange, read.excludedOther, int(read.available == 0));
    std::fflush(stdout);
}

RushReading availability(Fixture& f, ZoneStructure* zone, const UnitBase* threat, bool replacement, bool damaged,
                         bool emit = true) {
    RushReading read;
    read.zoneID = zone->getObjectID();
    read.onset = f.game->getGameCycleCount();
    read.damaged = damaged;
    read.air = threat->isAFlyingUnit();
    read.enemyDistance = blockDistance(threat->getLocation(), zone->getClosestPoint(threat->getLocation()));
    FixPoint best = FixPt_MAX;
    for(const auto* unit : unitList) {
        if(unit->getOwner() != f.own || unit->getItemID() != Unit_Trooper || !unit->isActive()
           || unit->getHealth() <= 0) continue;
        const FixPoint distance = blockDistance(unit->getLocation(), zone->getClosestPoint(unit->getLocation()));
        if(distance > 25) { ++read.excludedRange; continue; }
        const Role claim = role(f, unit);
        if(claim == Role::BaseDefence || claim == Role::LiveDefence || claim == Role::Defence) {
            ++read.excludedDefence;
            continue;
        }
        if(attacking(f, unit) || unit->isAttackMoving() || unit->getAttackPos().isValid()) {
            ++read.excludedAttack;
            continue;
        }
        if(claim != Role::Free || !unit->isRespondable() || unit->getHealth() * 2 < FixPoint(unit->getMaxHealth())
           || (!replacement && !zone->mayEnterGarrison(unit))) { ++read.excludedOther; continue; }
        ++read.available;
        if(distance < best || (distance == best && unit->getObjectID() < read.nearest)) {
            best = distance;
            read.nearest = unit->getObjectID();
            read.distance = distance;
        }
    }
    if(emit) logAvailability(f, read);
    require(read.available > 0 && read.nearest != NONE_ID, f.name + ": no distinct free nearby Trooper before ordering");
    require(damaged || read.enemyDistance <= 6, f.name + ": proximity stimulus was outside six native tiles");
    require(!damaged || read.enemyDistance > 6, f.name + ": damage-only stimulus was also a proximity threat");
    return read;
}

void observeRush(Fixture& f, std::vector<RushReading>& readings, const std::vector<Uint32>& protectedIDs = {}) {
    std::set<Uint32> nearest;
    for(const auto& reading : readings)
        require(nearest.insert(reading.nearest).second, f.name + ": fixtures do not provide a distinct nearest free Trooper per zone");
    int protectedSamples = 0;
    f.until(70, [&] {
        bool done = true;
        for(auto& read : readings) {
            const auto* zone = f.zone(read.zoneID);
            const auto* nearestMan = f.unit(read.nearest);
            read.unitDead = !nearestMan || nearestMan->getHealth() <= 0;
            read.zoneLost = !zone || zone->getOwner() != f.own;
            if((read.zoneLost || read.unitDead) && read.lostCycle == noCycle)
                read.lostCycle = f.game->getGameCycleCount();
            if(read.zoneLost) continue;
            for(const auto* man : unitList) if(man->getOwner() == f.own && man->getItemID() == Unit_Trooper
                    && man->isActive() && man->getTarget() && man->getTarget()->getObjectID() == read.zoneID
                    && man->getAttackMode() == CAPTURE && read.orderCycle == noCycle) {
                read.ordered = man->getObjectID();
                read.orderCycle = f.game->getGameCycleCount();
            }
            if(read.entryCycle == noCycle) for(Uint32 id : zone->getGarrison()) {
                const auto* man = f.unit(id);
                if(man && man->getItemID() == Unit_Trooper) {
                    require(!man->isActive() && man->isPickedUp(), f.name + ": rush entry was not physically hidden");
                    read.first = id;
                    read.entryCycle = f.game->getGameCycleCount();
                    break;
                }
            }
            done &= read.entryCycle != noCycle || read.unitDead;
        }
        for(Uint32 id : protectedIDs) if(const auto* man = f.unit(id); man && man->getHealth() > 0) {
            for(const auto& read : readings)
                require(!f.inside(id, f.zone(read.zoneID))
                        && !(man->getAttackMode() == CAPTURE && man->getTarget()
                             && man->getTarget()->getObjectID() == read.zoneID),
                        f.name + ": a protected combat assignment was stolen");
            if(man->isActive() && (role(f, man) != Role::Free || attacking(f, man) || man->isAttackMoving()))
                ++protectedSamples;
        }
        return done;
    });
    for(const auto& read : readings) {
        const int elapsed = read.entryCycle == noCycle ? -1 : static_cast<int>(read.entryCycle - read.onset);
        const bool fast = elapsed >= 0 && elapsed <= MILLI2CYCLES(20000);
        const bool near = read.distance <= 10;
        const bool timely = read.orderCycle != noCycle
            && read.orderCycle - read.onset <= Uint32(MILLI2CYCLES(1000));
        const bool lostBeforeOpportunity = read.lostCycle != noCycle
            && read.lostCycle - read.onset <= Uint32(MILLI2CYCLES(1000));
        const bool farDeviation = !near && (!fast || read.zoneLost || read.unitDead);
        std::printf("INFANTRY_ZONE_RUSH_RESULT: name=%s zone=%u eligible=1 available=%d nearest=%u ordered=%u first=%u "
                    "onset=%u order_cycle=%u entry_cycle=%u elapsed_cycles=%d elapsed_ms=%d within20=%d "
                    "excluded_attack=%d excluded_defence=%d excluded_range=%d excluded_other=%d protected_samples=%d "
                    "distance_raw=%lld near=%d order_within1=%d entry_observed=%d zone_lost=%d unit_dead=%d far_deviation=%d\n",
            f.name.c_str(), read.zoneID, read.available, read.nearest, read.ordered, read.first, read.onset,
            read.orderCycle, read.entryCycle, elapsed, elapsed < 0 ? -1 : elapsed * 1000 / MILLI2CYCLES(1000), int(fast),
            read.excludedAttack, read.excludedDefence, read.excludedRange, read.excludedOther, protectedSamples,
            static_cast<long long>(read.distance.getRawValue()), int(near), int(timely), int(read.entryCycle != noCycle),
            int(read.zoneLost), int(read.unitDead), int(farDeviation));
        ++rushTotals.eligible;
        rushTotals.entries += int(read.entryCycle != noCycle);
        rushTotals.fast += int(fast);
        rushTotals.nearEligible += int(near);
        rushTotals.nearFast += int(near && fast);
        rushTotals.timelyOrders += int(timely);
        require(timely || (!near && lostBeforeOpportunity), f.name + ": a legal rush order was not issued within one second");
        if(read.orderCycle != noCycle)
            require(read.ordered == read.nearest, f.name + ": order did not go to the nearest FREE Trooper");
        if(read.entryCycle != noCycle)
            require(read.first == read.nearest, f.name + ": nearest FREE Trooper was not the first actual entrant");
        else
            require(!near || read.unitDead || read.zoneLost, f.name + ": live nearby Trooper never reached its surviving shelter");
    }
    std::fflush(stdout);
    rushTotals.protectedSamples += protectedSamples;
    require(protectedIDs.empty() || protectedSamples > 0, f.name + ": protected-role case never had live protected samples");
}

void quiet(Fixture& f, const std::vector<ZoneStructure*>& zones) {
    for(const auto* building : structureList)
        require(building->getOwner() != f.enemy, f.name + ": quiet fixture has a visible enemy building/front");
    f.run(3);
    for(const auto* zone : zones) require(zone->getGarrisonSize() == 0, f.name + ": a quiet zone was already occupied");
}
UnitBase* proximityThreat(Fixture& f, ZoneStructure* zone, bool air = false) {
    const Coord at = zone->getLocation();
    auto* enemy = f.spawn(f.enemy, air ? Unit_Ornithopter : Unit_Tank,
                         at + Coord(zone->getStructureSizeX() - 1 + (air ? 3 : 6), 0));
    f.queue(Command(f.opponent->getPlayerID(), CMD_UNIT_SETMODE, enemy->getObjectID(), STOP));
    return enemy;
}

void rushDistance(int distance, int soldiers = 0, bool replacement = false, bool air = false) {
    const std::string name = replacement ? "rush_soldier_replacement" : soldiers ? "rush_existing_soldier"
        : air ? "rush_air" : "rush_distance_" + std::to_string(distance);
    Fixture f(name, Roster::AI, true, true);
    auto* zone = f.addZone(f.own);
    quiet(f, {zone});
    const auto occupants = soldiers ? f.soldierOccupants(zone, soldiers) : std::vector<Uint32>();
    auto* free = f.spawn(f.own, Unit_Trooper, zone->getLocation() - Coord(distance, 0));
    if(!soldiers && !air) f.spawn(f.own, Unit_Trooper, zone->getLocation() - Coord(distance + 2, 0));
    auto* enemy = proximityThreat(f, zone, air);
    std::vector<RushReading> readings{availability(f, zone, enemy, replacement, false)};
    require(readings[0].nearest == free->getObjectID() && readings[0].distance == FixPoint(distance),
            name + ": supplier was not at the specified exact native distance");
    observeRush(f, readings);
    int retained = 0;
    for(Uint32 id : occupants) {
        require(f.unit(id) && f.unit(id)->getHealth() > 0, name + ": replacement lost an original Soldier");
        retained += int(f.inside(id, zone));
    }
    require(retained == soldiers - int(replacement), name + ": existing Soldier shelter was evacuated beyond one required slot");
}

void rushFairness() {
    Fixture f("rush_fairness", Roster::AI, true, true);
    std::vector<ZoneStructure*> zones;
    for(int i = 0; i < 3; ++i) zones.push_back(f.addZone(f.own, zoneKinds[i], {32, 8 + i * 14}));
    quiet(f, zones);
    for(const auto* zone : zones) f.spawn(f.own, Unit_Trooper, zone->getLocation() - Coord(15, 0));
    std::vector<RushReading> readings;
    for(auto* zone : zones) readings.push_back(availability(f, zone, proximityThreat(f, zone), false, false));
    observeRush(f, readings);
}

void rushProtected(bool defence) {
    Fixture f(defence ? "rush_base_defence" : "rush_active_attack", Roster::AI, true, true);
    auto* protectedMan = f.spawn(f.own, Unit_Trooper, defence ? Coord(27, 32) : Coord(26, 20));
    Uint32 attackTarget = NONE_ID;
    if(defence) {
        auto* asset = f.place(f.own, Structure_ConstructionYard, {24, 34});
        auto* attacker = f.spawn(f.enemy, Unit_Devastator, {30, 34});
        attackTarget = attacker->getObjectID();
        f.opponent->doAttackObject(attacker, asset, true);
        require(f.until(20, [&] {
            const Role claim = role(f, protectedMan);
            return claim == Role::BaseDefence || claim == Role::LiveDefence || claim == Role::Defence;
        }), f.name + ": real enemy attack never created an actual defence registry claim");
    } else {
        auto* victim = f.spawn(f.enemy, Unit_Tank, {22, 20});
        f.queue(Command(f.opponent->getPlayerID(), CMD_UNIT_SETMODE, victim->getObjectID(), STOP));
        attackTarget = victim->getObjectID();
        f.bot->doAttackObject(protectedMan, victim, true);
        require(f.until(5, [&] { return victim->getHealth() < victim->getMaxHealth(); }),
                f.name + ": protected attacker never actually shot/damaged its target");
    }
    auto* zone = f.addZone(f.own, Structure_ZoneCommercial, defence ? Coord(32, 25) : Coord(32, 20));
    const Coord supply = defence ? Coord(17, 25) : Coord(32, 5);
    f.spawn(f.own, Unit_Trooper, supply);
    auto* threat = proximityThreat(f, zone);
    require(defence ? role(f, protectedMan) != Role::Free
                    : protectedMan->getTarget() && protectedMan->getTarget()->getObjectID() == attackTarget,
            f.name + ": protected engagement ended before rush onset");
    std::vector<RushReading> readings{availability(f, zone, threat, false, false)};
    require(defence ? readings[0].excludedDefence > 0 : readings[0].excludedAttack > 0,
            f.name + ": no protected candidate was actually excluded at onset");
    observeRush(f, readings, {protectedMan->getObjectID()});
    require(f.object(attackTarget) && f.object(attackTarget)->getHealth() > 0,
            f.name + ": protected combat target died before the observation finished");
}

void rushAttackMove() {
    Fixture f("rush_attack_move_protected", Roster::AI, true, true);
    auto* zone = f.addZone(f.own, Structure_ZoneIndustrial);
    quiet(f, {zone});
    auto* protectedMan = f.spawn(f.own, Unit_Trooper, {26, 20});
    const Coord began = protectedMan->getLocation();
    f.queue(Command(f.bot->getPlayerID(), CMD_UNIT_ATTACKMOVE, protectedMan->getObjectID(), 1, 20, 0));
    require(f.until(3, [&] { return protectedMan->isAttackMoving() && protectedMan->getLocation() != began; }),
            f.name + ": protected attack-move order never made a real walking step");
    f.spawn(f.own, Unit_Trooper, {32, 5});
    std::vector<RushReading> readings{availability(f, zone, proximityThreat(f, zone), false, false)};
    require(readings[0].excludedAttack > 0, f.name + ": no real attack-move candidate was excluded");
    observeRush(f, readings, {protectedMan->getObjectID()});
}

void rushDamage() {
    Fixture f("rush_damage", Roster::AI, true, true);
    auto* zone = f.addZone(f.own, Structure_ZoneIndustrial);
    zone->setStartingLevel(4);
    zone->setHealth(zone->getMaxHealth());
    quiet(f, {zone});
    auto* attacker = f.enemy->createUnit(Unit_Launcher, true);
    require(attacker && attacker->getWeaponRange() > 6, f.name + ": no ranged damage-only stimulus");
    f.deploy(attacker, zone->getLocation() + Coord(zone->getStructureSizeX() - 1 + attacker->getWeaponRange(), 0));
    f.spawn(f.own, Unit_Trooper, zone->getLocation() - Coord(15, 0));
    const FixPoint before = zone->getHealth();
    RushReading reading = availability(f, zone, attacker, false, true, false);
    f.opponent->doAttackObject(attacker, zone, true);
    // Preserve the actual pre-damage free-unit snapshot. The native hit callback may order it
    // during the very next game update, before a post-hit observer could see it still free.
    require(f.until(15, [&] {
        if(zone->getHealth() < before) return true;
        reading = availability(f, zone, attacker, false, true, false);
        return false;
    }), f.name + ": real distant shot never damaged the zone");
    f.opponent->doSetAttackMode(attacker, STOP);
    logAvailability(f, reading);
    std::printf("INFANTRY_ZONE_RUSH_DAMAGE: zone=%u attacker=%u hit_cycle=%u damage_raw=%lld before_raw=%lld after_raw=%lld\n",
        zone->getObjectID(), attacker->getObjectID(), f.game->getGameCycleCount() - 1,
        static_cast<long long>((before - zone->getHealth()).getRawValue()),
        static_cast<long long>(before.getRawValue()), static_cast<long long>(zone->getHealth().getRawValue()));
    require(zone->getHealth() > 0 && !zone->tooDamagedToHold(), f.name + ": controlled single volley removed entry eligibility");
    std::vector<RushReading> readings{reading};
    observeRush(f, readings);
}

} // namespace InfantryZoneCaptureProbe

int runInfantryZoneCaptureProbe() {
    using namespace InfantryZoneCaptureProbe;
    const char* requested = std::getenv("INFANTRY_ZONE_CASE_GROUP");
    const std::string group = requested ? requested : "capture";
    require(group == "capture" || group == "rush" || group == "all", "Unknown case group");
    settings.general.playerName = "Capture observer";
    require(ModManager::instance().setActiveMod("dunecity"), "Normal bundled Dune City content missing");
    modOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    probeConfig = getQuantBotConfig();
    struct Restore { QuantBotConfig value; ~Restore() { getQuantBotConfig() = value; } } restore{probeConfig};
    int failures = 0, cases = 0, groups = 0;
    auto check = [&](const char* family, const std::string& name, const std::function<void()>& test) {
        ++cases;
        try {
            test();
            std::printf("INFANTRY_ZONE_CASE: group=%s name=%s pass=1\n", family, name.c_str());
        } catch(const std::exception& error) {
            ++failures;
            currentGame = nullptr;
            std::printf("INFANTRY_ZONE_FAIL: group=%s name=%s pass=0 reason=%s\n", family, name.c_str(), error.what());
        }
        std::fflush(stdout);
    };
    if(group != "rush") {
        ++groups;
        for(size_t i = 0; i < zoneKinds.size(); ++i) {
            const std::string name = std::string("quantbot_player_") + "rci"[i];
            check("capture", name, [&] { explicitAttack(name, zoneKinds[i], Roster::AI, false, false); });
        }
        check("capture", "quantbot_command", [] { explicitAttack("quantbot_command", Structure_ZoneCommercial, Roster::AI, false, true); });
        check("capture", "ai_idle", [] { autonomous(false); });
        check("capture", "ai_attack_move", [] { autonomous(true); });
        for(bool ai : {false, true}) for(int boundary : {-1, 0, 1}) {
            const std::string name = std::string(ai ? "ai_hunt_" : "human_hunt_")
                + (boundary < 0 ? "below" : boundary == 0 ? "exact" : "above");
            check("capture", name, [&] { hunt(name, ai, boundary, zoneKinds[boundary + 1]); });
        }
        check("capture", "soldier_hunt_exact", [] { hunt("soldier_hunt_exact", false, 0, Structure_ZoneCommercial, true, Unit_Soldier); });
        check("capture", "ai_wounded_attack", woundedAttack);
        check("capture", "permission_off_attack", [] { explicitAttack("permission_off_attack", Structure_ZoneResidential, Roster::AI, false, false, false); });
        for(bool ai : {false, true}) {
            const std::string name = ai ? "permission_off_ai_hunt" : "permission_off_human_hunt";
            check("capture", name, [&] { hunt(name, ai, 0, Structure_ZoneIndustrial, false); });
        }
        check("capture", "full_enemy", [] { blocked(true); });
        check("capture", "unreachable", [] { blocked(false); });
        check("capture", "human_attack", [] { explicitAttack("human_attack", Structure_ZoneResidential, Roster::Human, true, false); });
        check("capture", "human_hunt_forced_attack", [] { explicitAttack("human_hunt_forced_attack", Structure_ZoneCommercial, Roster::Human, true, true, true, false, true); });
        for(bool human : {false, true}) {
            const std::string name = human ? "nonzone_human" : "nonzone_ai";
            check("capture", name, [&] { explicitAttack(name, Structure_ZoneResidential, human ? Roster::Human : Roster::AI, human, false, true, true); });
        }
        for(bool human : {false, true}) for(bool queued : {false, true}) {
            const std::string name = std::string("mixed_") + (human ? "human_" : "bot_") + (queued ? "command" : "player");
            check("capture", name, [&] { explicitAttack(name, Structure_ZoneIndustrial, Roster::Mixed, human, queued); });
        }
        check("capture", "squad_children", squadChildren);
        check("capture", "capture_threshold", [] {
            require(captureTotals.eligible > 0 && captureTotals.captures * 100 >= captureTotals.eligible * 90
                    && captureTotals.damage == 0, "Capture activation/rate/zero-damage threshold failed");
        });
        std::printf("INFANTRY_ZONE_GROUP: group=capture eligible=%d engagements=%d captures=%d excluded=%d damage_raw=%lld\n",
                    captureTotals.eligible, captureTotals.engagements, captureTotals.captures, captureTotals.excluded, captureTotals.damage);
    }
    if(group != "capture") {
        ++groups;
        for(int distance : {3, 6, 9, 10, 15, 25}) check("rush", "rush_distance_" + std::to_string(distance), [&] { rushDistance(distance); });
        check("rush", "rush_existing_soldier", [] { rushDistance(15, 1); });
        check("rush", "rush_soldier_replacement", [] { rushDistance(15, DuneCity::ZoneGarrison::kCapacity, true); });
        check("rush", "rush_fairness", rushFairness);
        check("rush", "rush_active_attack", [] { rushProtected(false); });
        check("rush", "rush_base_defence", [] { rushProtected(true); });
        check("rush", "rush_air", [] { rushDistance(15, 0, false, true); });
        check("rush", "rush_damage", rushDamage);
        check("rush", "rush_attack_move_protected", rushAttackMove);
        check("rush", "rush_threshold", [] {
            require(rushTotals.eligible > 0 && rushTotals.entries > 0 && rushTotals.nearEligible > 0
                    && rushTotals.nearFast * 100 >= rushTotals.nearEligible * 80 && rushTotals.protectedSamples > 0,
                    "Rush activation/one-second-order/nearby-80%-within20s/protected-role threshold failed");
        });
        std::printf("INFANTRY_ZONE_GROUP: group=rush eligible=%d entries=%d within20=%d near_eligible=%d near_within20=%d orders_within1=%d protected_samples=%d\n",
                    rushTotals.eligible, rushTotals.entries, rushTotals.fast, rushTotals.nearEligible,
                    rushTotals.nearFast, rushTotals.timelyOrders, rushTotals.protectedSamples);
    }
    std::printf("INFANTRY_ZONE_PASS: groups=%d cases=%d failures=%d\n", groups, cases, failures);
    std::fflush(stdout);
    require(failures == 0, std::to_string(failures) + " infantry zone probe cases failed");
    return MENU_QUIT_DEFAULT;
}
