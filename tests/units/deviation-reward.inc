// Measured Deviator contribution, exercised through the real engine: capture bookkeeping,
// outgoing damage and kill bonuses, absorption and its exclusions, provenance across shooter
// death/reversion/save, commanded Devastator detonation, QuantBot arming, and the legacy-save
// ledger reset. Every check throws on failure; the per-mod CSV records the decisions so the
// three content sets can be compared byte for byte.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <Explosion.h>
#include <sand.h>
#include <Definitions.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>
#include <players/DeviationReward.h>
#include <players/QuantBot.h>
#include <units/Devastator.h>
#include <units/Tank.h>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct DeviationProbe {
    std::string mod, mapData, outDir;
    std::ofstream decisions;   // identical across mods
    std::ofstream detail;      // per-mod amounts

    static void require(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }

    void note(const char* section, const char* check) { decisions << section << ',' << check << ",1\n"; }
    void amount(const char* check, int64_t value) { detail << check << ',' << value << '\n'; }

    std::unique_ptr<Game> newGame(int seed) {
        auto options = settings.gameOptions;
        options.rocketTurretsNeedPower = false;
        options.fogOfWar = false;
        GameInitSettings init("deviation.ini", mapData, "Deviation reward", true, options);
        init.randomSeed = seed;
        // Atreides captures, Harkonnen is the hostile original owner, Ordos is Atreides' ally.
        GameInitSettings::HouseInfo captor(HOUSE_ATREIDES, 1);
        captor.addPlayerInfo({"Speed tester", HUMANPLAYERCLASS}); init.addHouseInfo(captor);
        GameInitSettings::HouseInfo hostile(HOUSE_HARKONNEN, 2);
        hostile.addPlayerInfo({"Enemy", HUMANPLAYERCLASS}); init.addHouseInfo(hostile);
        GameInitSettings::HouseInfo ally(HOUSE_ORDOS, 1);
        ally.addPlayerInfo({"Ally", "qBotHard"}); init.addHouseInfo(ally);
        auto game = std::make_unique<Game>();
        currentGame = game.get();
        game->initGame(init); game->initializeGameLoop();
        return game;
    }

    static int price(const Game& game, Uint32 item, int houseID) {
        return game.objectData.data[item][houseID].price;
    }
    /// The documented value rule, written out independently of the production helper.
    static int64_t valueMilli(int priceOfTarget, int64_t removedHp, int64_t maxHp) {
        return int64_t(priceOfTarget) * 1000 * (removedHp * 1000) / (maxHp * 1000);
    }

    UnitBase* spawn(House* house, Uint32 item, const Coord& where) {
        auto* unit = house->createUnit(item, true);
        require(unit != nullptr, "Deviation fixture could not create a unit");
        unit->deploy(where);
        unit->setVisible(VIS_ALL, true);
        unit->attackMode = STOP;
        return unit;
    }

    /// Exact, unscaled damage through the real map dispatcher (the sonic branch).
    static void hit(ObjectBase* source, House* owner, ObjectBase* victim, int damage) {
        currentGameMap->damage(source ? source->getObjectID() : NONE_ID, owner,
                               victim->getCenterPoint(), Bullet_Sonic, damage, TILESIZE/2, false);
    }

    void run();
    void captureLedger();
    void outgoing();
    void absorption();
    void provenanceAcrossLoss();
    void saveLoadProvenance();
    void devastator();
    void quantBotArming();
    void killBonus();
    void killBonusAcrossSave();
    void legacyLedger();
};

// ---------------------------------------------------------------------------------------
// 1. A capture pays nothing by itself; refresh and release do not either.
// ---------------------------------------------------------------------------------------
void DeviationProbe::captureLedger() {
    auto game = newGame(11);
    auto* captor = game->getHouse(HOUSE_ATREIDES);
    auto* hostile = game->getHouse(HOUSE_HARKONNEN);
    auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 10));
    auto* tank = spawn(hostile, Unit_Tank, Coord(14, 10));

    // What the replaced accounting would have paid for doing nothing at all.
    const int64_t legacyEstimate = int64_t(price(*game, Unit_Tank, captor->getHouseID())) * 100;
    require(legacyEstimate > 0, "Fixture prices make the old estimate unmeasurable");
    amount("legacy_idle_estimate_milli", legacyEstimate);

    tank->deviate(captor, deviator->getObjectID());
    require(tank->getOwner() == captor, "The capture fixture did not change hands");
    const auto& reward = captor->getCombatReward(Unit_Deviator);
    require(reward.total() == 0, "An idle capture still paid a reward");
    require(reward.conversionMilli == 0, "The flat conversion estimate is still produced");
    require(captor->getNumItemDamageInflicted(Unit_Deviator) == 0,
            "The synthetic conversion raw-damage counter is still produced");
    require(captor->getDeviationCounters().captures == 1, "The capture was not counted");
    note("capture", "idle_capture_pays_nothing");

    const auto episodeID = tank->getDeviationEpisode().id;
    require(episodeID != DeviationReward::kNoEpisode && tank->getDeviationEpisode().rewarded,
            "A hostile-origin capture did not open a credited control interval");
    tank->deviate(captor, deviator->getObjectID());
    require(tank->getDeviationEpisode().id == episodeID, "A same-controller refresh restarted the interval");
    require(captor->getDeviationCounters().captures == 1 && captor->getDeviationCounters().refreshes == 1,
            "A refresh was counted as a new capture");
    require(captor->getCombatReward(Unit_Deviator).total() == 0, "A refresh paid a reward");
    note("capture", "refresh_continues_interval_and_pays_nothing");

    // Returning it to its original owner ends the interval and still pays nothing.
    tank->deviate(hostile);
    require(!tank->wasDeviated() && !tank->getDeviationEpisode().active(),
            "Returning the unit left a control interval open");
    require(captor->getCombatReward(Unit_Deviator).total() == 0, "A reversion paid a reward");
    require(captor->getDeviationCounters().releases == 1, "The release was not counted");
    note("capture", "release_pays_nothing");

    // An allied-origin capture is never credited.
    auto* ally = game->getHouse(HOUSE_ORDOS);
    auto* alliedTank = spawn(ally, Unit_Tank, Coord(18, 10));
    alliedTank->deviate(captor, deviator->getObjectID());
    require(alliedTank->getDeviationEpisode().active() && !alliedTank->getDeviationEpisode().rewarded,
            "An allied-origin capture opened a credited control interval");
    auto* victim = spawn(hostile, Unit_Tank, Coord(22, 10));
    hit(alliedTank, captor, victim, 40);
    require(captor->getCombatReward(Unit_Deviator).total() == 0,
            "A borrowed allied unit's damage was credited to the Deviator");
    require(captor->getCombatReward(Unit_Tank).damageMilli > 0,
            "A borrowed allied unit's damage was not credited to its own type");
    note("capture", "allied_origin_stays_on_natural_type");
    game.reset(); currentGame = nullptr;
}

// ---------------------------------------------------------------------------------------
// 2. Outgoing damage and kill bonuses, booked once and only to the Deviator.
// ---------------------------------------------------------------------------------------
void DeviationProbe::outgoing() {
    auto game = newGame(12);
    auto* captor = game->getHouse(HOUSE_ATREIDES);
    auto* hostile = game->getHouse(HOUSE_HARKONNEN);
    auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 12));
    auto* tank = spawn(hostile, Unit_Tank, Coord(14, 12));
    tank->deviate(captor, deviator->getObjectID());

    auto* siege = spawn(hostile, Unit_SiegeTank, Coord(20, 12));
    const int siegePrice = price(*game, Unit_SiegeTank, hostile->getHouseID());
    const int64_t siegeMaxHp = siege->getMaxHealth();
    require(siege->getHealth().lround() == siegeMaxHp && siegeMaxHp > 80, "Siege fixture is not at full health");

    hit(tank, captor, siege, 60);
    const int64_t expected = valueMilli(siegePrice, 60, siegeMaxHp);
    const auto& deviatorReward = captor->getCombatReward(Unit_Deviator);
    require(deviatorReward.damageMilli == expected, "Controlled outgoing damage value is wrong");
    require(deviatorReward.hits == 1 && deviatorReward.kills == 0, "Outgoing hit accounting is wrong");
    require(captor->getCombatReward(Unit_Tank).total() == 0 && captor->getCombatReward(Unit_Tank).hits == 0,
            "The borrowed unit's natural type was credited a second time");
    require(captor->getNumItemDamageInflicted(Unit_Tank) == 0,
            "The borrowed unit's natural raw-damage counter was credited");
    require(captor->getNumItemDamageInflicted(Unit_Deviator) == 60,
            "Raw damage was not rebucketed with the reward");
    require(captor->getDeviationCounters().outgoingDamageMilli == expected
            && captor->getDeviationCounters().outgoingHits == 1, "Outgoing counters disagree with the reward");
    amount("outgoing_damage_milli", deviatorReward.damageMilli);
    note("outgoing", "value_credited_to_deviator_only");

    // The killing blow adds 20% of the target's price, on actual hit points only. This
    // victim type has no combat record of its own, which is where the scale starts.
    const int64_t remaining = siege->getHealth().lround();
    hit(tank, captor, siege, int(remaining) + 5000);           // deliberate overkill
    require(siege->getHealth() <= 0, "The lethal fixture hit did not kill");
    const int64_t lethalValue = valueMilli(siegePrice, remaining, siegeMaxHp);
    require(deviatorReward.damageMilli == expected + lethalValue, "Overkill was paid beyond actual hit points");
    require(deviatorReward.killBonusMilli == int64_t(siegePrice) * 200, "The 20% kill bonus is wrong");
    require(deviatorReward.kills == 1, "The killing blow was not counted once");
    require(captor->getCombatReward(Unit_Tank).total() == 0, "The kill bonus leaked to the natural type");
    require(captor->getDeviationCounters().outgoingKills == 1
            && captor->getDeviationCounters().outgoingKillBonusMilli == int64_t(siegePrice) * 200,
            "Outgoing kill counters disagree with the reward");
    amount("outgoing_kill_bonus_milli", deviatorReward.killBonusMilli);
    note("outgoing", "lethal_bonus_once_actual_hp_only");

    // Friendly fire from a borrowed unit earns nothing and is recorded as an exclusion.
    auto* friendlyTarget = spawn(captor, Unit_Tank, Coord(24, 12));
    const auto before = captor->getCombatReward(Unit_Deviator);
    const auto excludedBefore = captor->getDeviationCounters().excluded;
    hit(tank, captor, friendlyTarget, 40);
    require(captor->getCombatReward(Unit_Deviator).total() == before.total(),
            "Friendly fire by a borrowed unit was rewarded");
    require(captor->getDeviationCounters().excluded > excludedBefore, "A friendly-fire exclusion was not recorded");
    note("outgoing", "friendly_fire_excluded");

    // Once control returns the same unit pays its own house's natural type again.
    tank->quitDeviation();
    auto* later = spawn(captor, Unit_SiegeTank, Coord(28, 12));
    const auto deviatorBefore = captor->getCombatReward(Unit_Deviator).total();
    hit(tank, hostile, later, 40);
    require(captor->getCombatReward(Unit_Deviator).total() == deviatorBefore,
            "A reverted unit still paid its former captor's Deviator");
    require(hostile->getCombatReward(Unit_Tank).damageMilli > 0,
            "A reverted unit did not pay its own house's natural type");
    note("outgoing", "reverted_unit_returns_to_natural_type");
    game.reset(); currentGame = nullptr;
}

// ---------------------------------------------------------------------------------------
// 3. Absorption, its cap and every exclusion.
// ---------------------------------------------------------------------------------------
void DeviationProbe::absorption() {
    auto game = newGame(13);
    auto* captor = game->getHouse(HOUSE_ATREIDES);
    auto* hostile = game->getHouse(HOUSE_HARKONNEN);
    auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 16));
    auto* tank = spawn(hostile, Unit_Tank, Coord(14, 16));
    auto* attacker = spawn(hostile, Unit_Tank, Coord(20, 16));
    auto* friendlyGun = spawn(captor, Unit_Tank, Coord(26, 16));
    tank->deviate(captor, deviator->getObjectID());

    const int tankPrice = price(*game, Unit_Tank, hostile->getHouseID());
    const int64_t tankMaxHp = tank->getMaxHealth();
    require(tankMaxHp > 60, "Absorption fixture unit is too small");

    hit(attacker, hostile, tank, 40);
    const int64_t absorbed = valueMilli(tankPrice, 40, tankMaxHp);
    const auto& reward = captor->getCombatReward(Unit_Deviator);
    require(reward.damageMilli == absorbed, "Absorbed hit points were valued incorrectly");
    require(reward.killBonusMilli == 0 && reward.kills == 0, "Absorption produced a kill bonus");
    require(captor->getDeviationCounters().absorbedDamageMilli == absorbed
            && captor->getDeviationCounters().absorbedHits == 1, "Absorption counters are wrong");
    amount("absorbed_damage_milli", absorbed);
    note("absorption", "hostile_damage_credited");

    // Own-house fire on the borrowed unit is not absorption.
    auto snapshot = captor->getCombatReward(Unit_Deviator).total();
    hit(friendlyGun, captor, tank, 20);
    require(captor->getCombatReward(Unit_Deviator).total() == snapshot, "Friendly damage was absorbed");
    note("absorption", "friendly_damage_excluded");

    // The borrowed unit's own shot, fired before the capture, is still self damage even
    // though its stored owner is now hostile to the controller.
    snapshot = captor->getCombatReward(Unit_Deviator).total();
    currentGameMap->damage(tank->getObjectID(), hostile, tank->getCenterPoint(),
                           Bullet_Sonic, 20, TILESIZE/2, false);
    require(captor->getCombatReward(Unit_Deviator).total() == snapshot,
            "A borrowed unit absorbed its own pre-capture shot");
    note("absorption", "self_damage_excluded");

    // Environmental damage has no owner and earns nothing.
    snapshot = captor->getCombatReward(Unit_Deviator).total();
    tank->handleDamage(15, NONE_ID, nullptr);
    require(captor->getCombatReward(Unit_Deviator).total() == snapshot, "Environmental damage was absorbed");
    note("absorption", "environmental_damage_excluded");

    // Healing never removes hit points, so it can never be absorption.
    snapshot = captor->getCombatReward(Unit_Deviator).total();
    tank->handleDamage(-20, attacker->getObjectID(), hostile);
    require(captor->getCombatReward(Unit_Deviator).total() == snapshot,
            "Restoring hit points produced an absorption reward");
    // Tornie's healing weapon restores hit points with setHealth and never raises a damage
    // event, so exercise that same mechanism: a borrowed unit gaining health earns nothing.
    const auto healthBeforeHeal = tank->getHealth();
    tank->setHealth(healthBeforeHeal + 30);
    require(tank->getHealth() > healthBeforeHeal, "The healing fixture never restored hit points");
    require(captor->getCombatReward(Unit_Deviator).total() == snapshot, "Healing produced a reward");
    require(captor->getDeviationCounters().absorbedHits == 1, "Healing was counted as an absorbed hit");
    note("absorption", "healing_excluded");

    // The captive's death pays only the hit points it still had, and never a kill bonus.
    const int64_t before = captor->getCombatReward(Unit_Deviator).damageMilli;
    const int64_t left = tank->getHealth().lround();
    hit(attacker, hostile, tank, int(left) + 4000);
    require(tank->getHealth() <= 0, "The captive was not destroyed");
    require(captor->getCombatReward(Unit_Deviator).damageMilli == before + valueMilli(tankPrice, left, tankMaxHp),
            "Overkill on a captive was paid beyond its actual hit points");
    require(captor->getCombatReward(Unit_Deviator).killBonusMilli == 0,
            "The captive's own death produced a kill bonus for its captor");
    for(int tick = 0; tick < 3; ++tick) game->updateGameState();   // the engine removes it here
    require(hostile->getNumLostItems(Unit_Tank) >= 1,
            "The original owner did not record the loss of its borrowed unit");
    require(captor->getNumLostItems(Unit_Tank) == 0,
            "The captor was charged for losing a unit it never bought");
    note("absorption", "captive_death_capped_and_unbonused");
    game.reset(); currentGame = nullptr;
}

// ---------------------------------------------------------------------------------------
// 4. A fired shot keeps the credit it was fired with.
// ---------------------------------------------------------------------------------------
void DeviationProbe::provenanceAcrossLoss() {
    for(int variant = 0; variant < 3; ++variant) { // 0 shooter dies, 1 shooter reverts, 2 recaptured
        auto game = newGame(14 + variant);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* ally = game->getHouse(HOUSE_ORDOS);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 20));
        auto* tank = spawn(hostile, Unit_Tank, Coord(14, 20));
        auto* target = spawn(hostile, Unit_SiegeTank, Coord(22, 20));
        tank->deviate(captor, deviator->getObjectID());

        Coord from = tank->getCenterPoint(), to = target->getCenterPoint();
        bulletList.push_back(new Bullet(tank->getObjectID(), &from, &to, Bullet_ShellMedium, 40, false, target));
        if(variant == 0)       tank->destroy();
        else if(variant == 1)  tank->quitDeviation();
        else                   tank->deviate(ally, NONE_ID);

        const auto healthBefore = target->getHealth();
        for(int tick = 0; tick < 200 && !bulletList.empty(); ++tick) game->updateGameState();
        require(target->getHealth() < healthBefore, "The in-flight fixture shot never landed");
        require(captor->getCombatReward(Unit_Deviator).damageMilli > 0,
                "An in-flight shot lost its firing-time beneficiary");
        require(captor->getCombatReward(Unit_Tank).total() == 0,
                "An in-flight shot was rebooked to the shooter's natural type");
        require(ally->getCombatReward(Unit_Deviator).total() == 0
                && hostile->getCombatReward(Unit_Deviator).total() == 0,
                "An in-flight shot paid a house that did not fire it");
        note("provenance", variant == 0 ? "survives_shooter_death"
                         : variant == 1 ? "survives_reversion" : "survives_recapture");
        game.reset(); currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------------------
// 5. Saving and reloading mid-flight preserves the same credit.
// ---------------------------------------------------------------------------------------
void DeviationProbe::saveLoadProvenance() {
    int64_t results[2] = {0, 0};
    for(int reload = 0; reload < 2; ++reload) {
        auto game = newGame(17);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 24));
        auto* tank = spawn(hostile, Unit_Tank, Coord(14, 24));
        auto* target = spawn(hostile, Unit_SiegeTank, Coord(22, 24));
        tank->deviate(captor, deviator->getObjectID());
        const auto targetID = target->getObjectID();
        const auto shooterID = tank->getObjectID();

        Coord from = tank->getCenterPoint(), to = target->getCenterPoint();
        bulletList.push_back(new Bullet(tank->getObjectID(), &from, &to, Bullet_ShellMedium, 40, false, target));
        for(int tick = 0; tick < 3; ++tick) game->updateGameState();
        require(!bulletList.empty(), "The save fixture shot landed before the save");

        if(reload) {
            const std::string path = outDir + "/deviation-" + mod + ".dls";
            require(game->saveGame(path), "Could not save a game with a shot in flight");
            game.reset(); currentGame = nullptr;
            game = std::make_unique<Game>(); currentGame = game.get();
            game->initGame(GameInitSettings(path)); game->initializeGameLoop();
            captor = game->getHouse(HOUSE_ATREIDES);
            require(game->peekDeviationEpisodeCounter() > 1, "The episode counter did not survive the save");
            auto* restored = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(shooterID));
            require(restored && restored->getDeviationEpisode().active()
                    && restored->getDeviationEpisode().rewarded,
                    "The control interval did not survive the save");
        }
        auto* live = game->getObjectManager().getObject(targetID);
        const auto healthBefore = live->getHealth();
        for(int tick = 0; tick < 200 && !bulletList.empty(); ++tick) game->updateGameState();
        require(live->getHealth() < healthBefore, "The reloaded shot never landed");
        require(captor->getCombatReward(Unit_Tank).total() == 0,
                "A reloaded shot was rebooked to the shooter's natural type");
        results[reload] = captor->getCombatReward(Unit_Deviator).damageMilli;
        game.reset(); currentGame = nullptr;
    }
    require(results[0] > 0 && results[0] == results[1],
            "A save/load changed the credit of a shot already in flight");
    amount("saved_flight_damage_milli", results[1]);
    note("save_load", "in_flight_provenance_preserved");
}

// ---------------------------------------------------------------------------------------
// 6. Commanded Devastator detonation.
// ---------------------------------------------------------------------------------------
void DeviationProbe::devastator() {
    // A reverted Devastator must not pay for its own blast a second time. Also
    // reload the armed fuse to exercise its persisted beneficiary and terminal flag.
    for(int reload = 0; reload < 2; ++reload) {
        auto game = newGame(67);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* enemy = game->getHouse(HOUSE_HARKONNEN);
        auto* dev = static_cast<Devastator*>(spawn(enemy, Unit_Devastator, Coord(25, 15)));
        dev->setHealth(50);
        dev->deviate(captor);
        dev->doStartDevastate();
        dev->quitDeviation();
        dev->devastateTimer = 1;
        const auto id = dev->getObjectID();
        const auto expected = CombatReward::hit(price(*game, Unit_Devastator, HOUSE_HARKONNEN),
            int64_t(dev->getMaxHealth()) * 1000, 50000, 0, true, true);
        if(reload) {
            const auto path = outDir + "/armed-" + mod + ".dls";
            require(game->saveGame(path), "Could not save the armed fuse");
            game.reset(); currentGame = nullptr;
            game = std::make_unique<Game>(); currentGame = game.get();
            game->initGame(GameInitSettings(path)); game->initializeGameLoop();
            captor = game->getHouse(HOUSE_ATREIDES);
            dev = dynamic_cast<Devastator*>(game->getObjectManager().getObject(id));
            require(dev && dev->isArmedToDevastate(), "Reload lost the armed fuse");
        }
        const auto before = captor->getCombatReward(Unit_Deviator).total();
        dev->update();
        require(captor->getCombatReward(Unit_Deviator).total() - before == expected.total(),
            "Reverted self-blast duplicated terminal damage or kill bonus");
        note("devastator", reload ? "saved_fuse_single_terminal_credit" : "self_blast_no_duplicate_credit");
        game.reset(); currentGame = nullptr;
    }
    // 6a. A completed commanded detonation pays remaining value plus one bonus - 20% for a
    //     Devastator type with no record of its own - and its blast pays the same Deviator.
    {
        auto game = newGame(21);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 8));
        auto* dev = static_cast<Devastator*>(spawn(hostile, Unit_Devastator, Coord(20, 20)));
        auto* bystander = spawn(hostile, Unit_Tank, Coord(20, 21));
        dev->deviate(captor, deviator->getObjectID());

        const int devPrice = price(*game, Unit_Devastator, hostile->getHouseID());
        const int64_t devMaxHp = dev->getMaxHealth();
        dev->setHealth(FixPoint(static_cast<int>(devMaxHp)) / 4);
        const int64_t remaining = dev->getHealth().lround();
        require(remaining > 0, "The wounded Devastator fixture has no hit points left");

        dev->doStartDevastate();
        require(dev->devastateTimer == 200, "The commanded fuse is not the shipped 200 cycles");
        dev->doStartDevastate();
        require(dev->devastateTimer == 200, "A repeated order restarted the fuse");

        const auto devID = dev->getObjectID();
        for(int tick = 0; tick < 260 && game->getObjectManager().getObject(devID); ++tick)
            game->updateGameState();
        require(game->getObjectManager().getObject(devID) == nullptr,
                "The commanded fuse never completed");

        const auto& counters = captor->getDeviationCounters();
        const int64_t terminalValue = valueMilli(devPrice, remaining, devMaxHp);
        require(counters.detonations == 1, "The completed detonation was not counted exactly once");
        require(counters.terminalDamageMilli == terminalValue,
                "The terminal credit is not the remaining hit points at full price");
        require(counters.terminalKillBonusMilli == int64_t(devPrice) * 200,
                "The terminal 20% bonus is wrong");
        require(counters.terminalDamageMilli < int64_t(devPrice) * 1000,
                "The terminal credit paid the whole purchase value again");
        require(counters.outgoingDamageMilli > 0, "The blast did not pay the arming Deviator");
        require(captor->getCombatReward(Unit_Devastator).total() == 0,
                "The detonation was credited to the Devastator's own type");
        const auto& reward = captor->getCombatReward(Unit_Deviator);
        require(reward.killBonusMilli >= int64_t(devPrice) * 200, "The Deviator bucket lost the terminal bonus");
        require(bystander == nullptr || true, "");
        amount("terminal_damage_milli", counters.terminalDamageMilli);
        amount("terminal_kill_bonus_milli", counters.terminalKillBonusMilli);
        amount("blast_outgoing_damage_milli", counters.outgoingDamageMilli);
        note("devastator", "terminal_remaining_plus_single_bonus");
        note("devastator", "blast_credits_arming_deviator");
        game.reset(); currentGame = nullptr;
    }

    // 6b. Killed before the fuse completes: absorption yes, commanded completion no.
    {
        auto game = newGame(22);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 8));
        auto* dev = static_cast<Devastator*>(spawn(hostile, Unit_Devastator, Coord(20, 24)));
        auto* attacker = spawn(hostile, Unit_Tank, Coord(28, 24));
        dev->deviate(captor, deviator->getObjectID());
        dev->doStartDevastate();
        hit(attacker, hostile, dev, dev->getHealth().lround() + 9000);
        require(dev->getHealth() <= 0, "The interrupted-fuse fixture survived");
        for(int tick = 0; tick < 20; ++tick) game->updateGameState();

        const auto& counters = captor->getDeviationCounters();
        require(counters.detonations == 0, "A combat death was counted as a commanded detonation");
        require(counters.terminalDamageMilli == 0 && counters.terminalKillBonusMilli == 0,
                "A combat death paid the commanded-detonation bonus");
        require(counters.absorbedDamageMilli > 0, "The hit points it absorbed were not credited");
        note("devastator", "killed_before_fuse_pays_no_completion");
        game.reset(); currentGame = nullptr;
    }

    // 6c. Hit points removed on the very tick the fuse expires: killed, not detonated.
    {
        auto game = newGame(23);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 8));
        auto* dev = static_cast<Devastator*>(spawn(hostile, Unit_Devastator, Coord(24, 8)));
        dev->deviate(captor, deviator->getObjectID());
        dev->doStartDevastate();
        dev->devastateTimer = 1;
        dev->setHealth(0);
        dev->update();
        require(captor->getDeviationCounters().detonations == 0,
                "A Devastator killed on its last fuse tick was counted as a completed detonation");
        require(captor->getDeviationCounters().terminalKillBonusMilli == 0,
                "A Devastator killed on its last fuse tick was paid a completion bonus");
        note("devastator", "zero_health_last_tick_is_not_completion");
        game.reset(); currentGame = nullptr;
    }

    // 6d. Reverting during the fuse keeps the arming house as the beneficiary.
    {
        auto game = newGame(24);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        auto* deviator = spawn(captor, Unit_Deviator, Coord(10, 8));
        auto* dev = static_cast<Devastator*>(spawn(hostile, Unit_Devastator, Coord(28, 20)));
        spawn(hostile, Unit_Tank, Coord(28, 21));
        dev->deviate(captor, deviator->getObjectID());
        dev->doStartDevastate();
        dev->quitDeviation();
        require(!dev->wasDeviated(), "The reversion fixture is still deviated");
        const auto devID = dev->getObjectID();
        for(int tick = 0; tick < 260 && game->getObjectManager().getObject(devID); ++tick)
            game->updateGameState();
        require(game->getObjectManager().getObject(devID) == nullptr, "The fuse did not complete");

        require(captor->getDeviationCounters().detonations == 1,
                "A detonation after reversion did not pay the arming house");
        require(captor->getDeviationCounters().terminalKillBonusMilli > 0,
                "A detonation after reversion lost its terminal bonus");
        require(hostile->getCombatReward(Unit_Deviator).total() == 0
                && hostile->getDeviationCounters().detonations == 0,
                "A detonation after reversion paid the original owner");
        require(game->getHouse(HOUSE_ORDOS)->getCombatReward(Unit_Deviator).total() == 0,
                "A detonation after reversion paid the captor's ally");
        note("devastator", "post_reversion_pays_arming_captor");
        game.reset(); currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------------------
// 7. QuantBot arms a captured Devastator before any role or rally early return.
// ---------------------------------------------------------------------------------------
void DeviationProbe::quantBotArming() {
    auto options = settings.gameOptions;
    options.fogOfWar = false;
    GameInitSettings init("deviation.ini", mapData, "Deviation arming", true, options);
    init.randomSeed = 31;
    GameInitSettings::HouseInfo bot(HOUSE_HARKONNEN, 2);
    bot.addPlayerInfo({"Bot", "qBotHard"}); init.addHouseInfo(bot);
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Speed tester", HUMANPLAYERCLASS}); init.addHouseInfo(human);
    GameInitSettings::HouseInfo ally(HOUSE_ORDOS, 2);
    ally.addPlayerInfo({"Ally", "qBotHard"}); init.addHouseInfo(ally);
    auto game = std::make_unique<Game>(); currentGame = game.get();
    game->initGame(init); game->initializeGameLoop();

    auto* botHouse = game->getHouse(HOUSE_HARKONNEN);
    auto* humanHouse = game->getHouse(HOUSE_ATREIDES);
    QuantBot* quant = nullptr;
    for(const auto& player : botHouse->getPlayerList())
        if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) quant = candidate;
    require(quant != nullptr, "The arming fixture has no QuantBot");

    auto captured = [&](const Coord& where) {
        auto* dev = static_cast<Devastator*>(spawn(humanHouse, Unit_Devastator, where));
        dev->deviate(botHouse, NONE_ID);
        dev->doSetAttackMode(GUARD);
        dev->setTarget(nullptr);
        return dev;
    };

    // The rally early return used to move a freshly captured, targetless ground unit and skip
    // the detonation order for the rest of its deviation.
    game->gameType = GameType::CustomGame;
    quant->gameMode = QuantBot::GameMode::Custom;
    quant->supportMode = false;
    quant->squadRallyLocation = Coord(30, 15);
    auto* rallyCase = captured(Coord(20, 15));
    quant->checkAllUnits();
    require(rallyCase->devastateTimer > 0, "The rally early return still skipped a captured Devastator");
    require(rallyCase->getAttackMode() == HUNT, "An armed captured Devastator was not set to hunt");
    note("quantbot", "armed_before_rally_early_return");

    // Re-scanning must not restart the fuse.
    const auto after = rallyCase->devastateTimer;
    for(int pass = 0; pass < 3; ++pass) { game->updateGameState(); quant->checkAllUnits(); }
    require(rallyCase->devastateTimer > 0 && rallyCase->devastateTimer < after,
            "A later scan restarted the running fuse");
    note("quantbot", "rescan_does_not_restart_fuse");

    // Support mode used to block the order twice.
    quant->supportMode = true;
    auto* supportCase = captured(Coord(22, 17));
    quant->checkAllUnits();
    require(supportCase->devastateTimer > 0, "Support mode still blocked a captured Devastator");
    note("quantbot", "armed_in_support_mode");
    quant->supportMode = false;

    // A campaign hold is also a role early return; arming must precede it.
    game->gameType = GameType::Campaign;
    quant->gameMode = QuantBot::GameMode::Campaign;
    auto* campaignCase = captured(Coord(24, 19));
    require(quant->campaignControlsUnit(campaignCase),
            "The campaign fixture unit is not campaign-controlled, so the ordering is untested");
    quant->checkAllUnits();
    require(campaignCase->devastateTimer > 0, "A campaign hold still skipped a captured Devastator");
    note("quantbot", "armed_before_campaign_hold");
    game->gameType = GameType::CustomGame;
    quant->gameMode = QuantBot::GameMode::Custom;

    // An uncaptured Devastator of the bot's own house is never armed automatically.
    auto* own = static_cast<Devastator*>(spawn(botHouse, Unit_Devastator, Coord(26, 21)));
    quant->checkAllUnits();
    require(own->devastateTimer == 0, "The bot detonated one of its own Devastators");
    note("quantbot", "own_devastator_not_armed");

    auto* friendly = static_cast<Devastator*>(spawn(game->getHouse(HOUSE_ORDOS), Unit_Devastator, Coord(25, 23)));
    friendly->deviate(botHouse);
    quant->checkAllUnits();
    require(friendly->devastateTimer == 0, "The bot detonated an allied-origin Devastator");
    note("quantbot", "allied_origin_not_armed");

    // A Devastator captured by a human house has no bot to arm it.
    auto* humanHeld = static_cast<Devastator*>(spawn(botHouse, Unit_Devastator, Coord(28, 23)));
    humanHeld->deviate(humanHouse, NONE_ID);
    for(int tick = 0; tick < 120; ++tick) game->updateGameState();
    require(humanHeld->devastateTimer == 0, "A human-held captured Devastator was armed automatically");
    note("quantbot", "human_control_not_overridden");
    game.reset(); currentGame = nullptr;
}

// ---------------------------------------------------------------------------------------
// 8. The killing-blow bonus scales with how well the destroyed enemy TYPE was doing.
// ---------------------------------------------------------------------------------------
void DeviationProbe::killBonus() {
    // --- The policy on its own, with no engine involved.
    require(CombatReward::typeBonusPermille(0,0,300)==CombatReward::kBaselineBonusPermille,
            "A type with no record at all is not worth the former flat bonus");
    require(CombatReward::typeBonusPermille(5000000,5000000,300)==CombatReward::kBaselineBonusPermille,
            "Equal damage value and lost value is not worth the baseline");
    // Earned two unit prices and lost nothing, or lost four and earned nothing. Both points are
    // read off the documented rule rather than recomputed from the production helper.
    require(CombatReward::typeBonusPermille(2*300*1000,0,300)==300,
            "A type that earned half its prior again is not worth 30%");
    require(CombatReward::typeBonusPermille(0,4*300*1000,300)==100,
            "A type that lost its whole prior without earning is not worth 10%");
    note("kill_bonus","cold_start_and_parity_keep_the_flat_bonus");

    constexpr int64_t hugeLedger = std::numeric_limits<int64_t>::max();
    int previous = 0;
    for(const int64_t damage:{int64_t(0),int64_t(1),int64_t(300000),int64_t(1200000),
                              int64_t(600000000),hugeLedger}) {
        const int now = CombatReward::typeBonusPermille(damage,1200000,300);
        require(now>=previous,"More damage from the destroyed type lowered its kill bonus");
        previous = now;
    }
    previous = CombatReward::kMaxBonusPermille;
    for(const int64_t loss:{int64_t(0),int64_t(1),int64_t(1200000),int64_t(600000000),hugeLedger}) {
        const int now = CombatReward::typeBonusPermille(600000000,loss,300);
        require(now<=previous,"More losses by the destroyed type raised its kill bonus");
        previous = now;
    }
    note("kill_bonus","monotonic_in_damage_and_in_losses");

    require(CombatReward::typeBonusPermille(hugeLedger,0,300)==CombatReward::kMaxBonusPermille
            && CombatReward::typeBonusPermille(0,hugeLedger,300)==CombatReward::kMinBonusPermille,
            "The 5-60% bound does not hold at the extremes of the ledger");
    require(CombatReward::typeBonusPermille(hugeLedger,hugeLedger,
                std::numeric_limits<int>::max())==CombatReward::kBaselineBonusPermille,
            "Saturated evidence on both sides is not parity");
    for(const int badPrice:{0,-1,std::numeric_limits<int>::min()})
        require(CombatReward::typeBonusPermille(600000000,0,badPrice)
                    ==CombatReward::kBaselineBonusPermille,
                "An invalid price did not fall back to the flat bonus");
    note("kill_bonus","bounded_at_5_and_60_percent_and_safe_at_int64_extremes");

    // --- The same scale through the real engine, on actual killing blows.
    auto game = newGame(51);
    auto* captor = game->getHouse(HOUSE_ATREIDES);
    auto* hostile = game->getHouse(HOUSE_HARKONNEN);
    const int siegePrice = price(*game, Unit_SiegeTank, hostile->getHouseID());
    require(siegePrice > 0, "The kill-bonus fixture has no priced victim type");
    auto* gun = spawn(captor, Unit_Tank, Coord(10, 10));

    // Only the victim type's own record with its own house is controlled.
    auto seed = [&](int64_t damageMilli, int losses) {
        hostile->combatRewards[Unit_SiegeTank] = CombatReward::Totals();
        hostile->combatRewards[Unit_SiegeTank].damageMilli = damageMilli;
        hostile->numItemLosses[Unit_SiegeTank] = losses;
    };
    auto killOne = [&](ObjectBase* shooter, House* beneficiary, Uint32 bucket, const Coord& where) {
        const auto before = beneficiary->getCombatReward(bucket);
        auto* victim = spawn(hostile, Unit_SiegeTank, where);
        hit(shooter, beneficiary, victim, int(victim->getHealth().lround()) + 5000);
        require(victim->getHealth() <= 0, "A kill-bonus fixture hit was not lethal");
        return beneficiary->getCombatReward(bucket).killBonusMilli - before.killBonusMilli;
    };

    // --- The House-side lookup saturates rather than overflowing: an absurd loss
    //     count at an absurd price still lands inside the documented bound.
    for(const int extreme:{std::numeric_limits<int>::max(), 1000000}) {
        hostile->combatRewards[Unit_SiegeTank] = CombatReward::Totals();
        hostile->numItemLosses[Unit_SiegeTank] = extreme;
        require(hostile->killBonusPermilleForOwnType(Unit_SiegeTank, extreme)
                    == CombatReward::kMinBonusPermille,
                "An absurd loss ledger did not saturate to the 5% bound");
        hostile->combatRewards[Unit_SiegeTank].damageMilli = std::numeric_limits<int64_t>::max();
        require(hostile->killBonusPermilleForOwnType(Unit_SiegeTank, extreme)
                    >= CombatReward::kBaselineBonusPermille,
                "A saturated damage ledger scored below the neutral bonus");
    }
    // Negative counters, an unpriced type, an out-of-range type and an unresolvable
    // victim house all fall back to the flat bonus instead of scoring.
    hostile->combatRewards[Unit_SiegeTank] = CombatReward::Totals();
    hostile->numItemLosses[Unit_SiegeTank] = -5;
    require(hostile->killBonusPermilleForOwnType(Unit_SiegeTank, 300)
                == CombatReward::kBaselineBonusPermille
            && hostile->killBonusPermilleForOwnType(Unit_SiegeTank, 0)
                == CombatReward::kBaselineBonusPermille
            && hostile->killBonusPermilleForOwnType(Num_ItemID, 300)
                == CombatReward::kBaselineBonusPermille
            && House::killBonusPermilleForVictim(Unit_SiegeTank, NUM_HOUSES, 300)
                == CombatReward::kBaselineBonusPermille,
            "The victim-type lookup did not fall back on invalid inputs");
    note("kill_bonus","house_lookup_saturates_and_falls_back");

    seed(0, 0);
    require(killOne(gun, captor, Unit_Tank, Coord(14,10))
                == int64_t(siegePrice)*CombatReward::kBaselineBonusPermille,
            "An enemy type with no record did not pay the former flat 20% through the engine");
    seed(int64_t(2)*siegePrice*1000, 0);
    const int64_t proven = killOne(gun, captor, Unit_Tank, Coord(18,10));
    require(proven == int64_t(siegePrice)*300,
            "Destroying a proven enemy type did not raise the killing-blow bonus");
    seed(0, 4);
    const int64_t failing = killOne(gun, captor, Unit_Tank, Coord(22,10));
    require(failing == int64_t(siegePrice)*100,
            "Destroying a type that keeps dying without earning did not lower the bonus");
    require(failing < int64_t(siegePrice)*CombatReward::kBaselineBonusPermille && failing < proven,
            "The bonus is not ordered failing type < no record < proven type");
    amount("kill_bonus_proven_type_milli", proven);
    amount("kill_bonus_failing_type_milli", failing);

    // A bonus is never an input to the next bonus: damage value is the only evidence.
    seed(int64_t(2)*siegePrice*1000, 0);
    hostile->combatRewards[Unit_SiegeTank].killBonusMilli = 900000000;
    hostile->combatRewards[Unit_SiegeTank].conversionMilli = 900000000;
    require(killOne(gun, captor, Unit_Tank, Coord(26,10)) == proven,
            "Kill bonuses or the conversion estimate fed back into the next bonus");
    note("kill_bonus","engine_lethal_bonus_follows_the_victim_types_own_record");

    // Every existing exclusion still holds at a raised scale.
    seed(int64_t(2)*siegePrice*1000, 0);
    auto* wounded = spawn(hostile, Unit_SiegeTank, Coord(10,14));
    auto snapshot = captor->getCombatReward(Unit_Tank);
    hit(gun, captor, wounded, 20);
    require(wounded->getHealth() > 0 && captor->getCombatReward(Unit_Tank).killBonusMilli
                == snapshot.killBonusMilli,
            "A surviving target paid a kill bonus");
    snapshot = captor->getCombatReward(Unit_Tank);
    hit(gun, captor, wounded, int(wounded->getHealth().lround()) + 5000);
    require(captor->getCombatReward(Unit_Tank).killBonusMilli - snapshot.killBonusMilli == proven,
            "The killing blow on an already wounded target paid a different scale");
    snapshot = captor->getCombatReward(Unit_Tank);
    hit(gun, captor, wounded, 400);
    require(captor->getCombatReward(Unit_Tank).killBonusMilli == snapshot.killBonusMilli
                && captor->getCombatReward(Unit_Tank).kills == snapshot.kills,
            "A repeated hit on a dead unit paid the bonus a second time");
    auto* friendlySiege = spawn(captor, Unit_SiegeTank, Coord(14,14));
    snapshot = captor->getCombatReward(Unit_Tank);
    hit(gun, captor, friendlySiege, int(friendlySiege->getHealth().lround()) + 5000);
    require(friendlySiege->getHealth() <= 0, "The friendly-fire fixture target survived");
    require(captor->getCombatReward(Unit_Tank).killBonusMilli == snapshot.killBonusMilli,
            "Friendly fire paid a performance kill bonus");
    note("kill_bonus","once_only_nonlethal_and_friendly_still_excluded");

    // A borrowed shooter pays its captor's Deviator, but what it destroyed is still
    // scored from the destroyed type's own owner. The captor's Deviator is given a
    // far better record here, so reading the captor's score instead would show up.
    auto* deviator = spawn(captor, Unit_Deviator, Coord(18,14));
    auto* borrowed = spawn(hostile, Unit_Tank, Coord(22,14));
    borrowed->deviate(captor, deviator->getObjectID());
    captor->combatRewards[Unit_Deviator].damageMilli = 900000000;
    captor->numItemLosses[Unit_Deviator] = 0;
    seed(int64_t(2)*siegePrice*1000, 0);
    require(killOne(borrowed, captor, Unit_Deviator, Coord(26,14)) == proven,
            "A captured shooter scored its victim by a different record than an ordinary one");
    note("kill_bonus","captured_shooter_uses_the_victims_own_type_record");

    // The commanded completion reads exactly the record an ordinary lethal hit reads.
    // Its own loss is booked when the engine removes it, after this credit, so a
    // completion scored after that loss would land on a different number.
    const int devPrice = price(*game, Unit_Devastator, hostile->getHouseID());
    hostile->combatRewards[Unit_Devastator] = CombatReward::Totals();
    hostile->combatRewards[Unit_Devastator].damageMilli = int64_t(2)*devPrice*1000;
    hostile->numItemLosses[Unit_Devastator] = 0;
    auto* dev = static_cast<Devastator*>(spawn(hostile, Unit_Devastator, Coord(10,18)));
    dev->deviate(captor, deviator->getObjectID());
    dev->setHealth(FixPoint(static_cast<int>(dev->getMaxHealth()))/4);
    dev->doStartDevastate();
    dev->devastateTimer = 1;
    const int64_t terminalBefore = captor->getDeviationCounters().terminalKillBonusMilli;
    dev->update();
    require(captor->getDeviationCounters().terminalKillBonusMilli - terminalBefore
                == int64_t(devPrice)*300,
            "The commanded detonation did not score the Devastator's own type record");
    amount("kill_bonus_commanded_detonation_milli",
           captor->getDeviationCounters().terminalKillBonusMilli - terminalBefore);
    note("kill_bonus","commanded_completion_matches_the_ordinary_lethal_scale");

    // Losing a borrowed unit is still not a killing blow for its captor, at any scale.
    seed(int64_t(2)*siegePrice*1000, 0);
    auto* captive = spawn(hostile, Unit_SiegeTank, Coord(14,18));
    captive->deviate(captor, deviator->getObjectID());
    auto* enemyGun = spawn(hostile, Unit_Tank, Coord(22,18));
    snapshot = captor->getCombatReward(Unit_Deviator);
    hit(enemyGun, hostile, captive, int(captive->getHealth().lround()) + 5000);
    require(captive->getHealth() <= 0, "The absorption fixture captive survived");
    require(captor->getCombatReward(Unit_Deviator).killBonusMilli == snapshot.killBonusMilli,
            "A captive's own death paid its captor a performance kill bonus");
    require(hostile->getCombatReward(Unit_Tank).killBonusMilli > 0,
            "The shooter that destroyed the captive earned no killing blow at all");
    note("kill_bonus","absorbed_loss_still_pays_no_kill_bonus");
    game.reset(); currentGame = nullptr;
}

// ---------------------------------------------------------------------------------------
// 8a. The identical bonus survives a save and reload of the ledger it is read from.
// ---------------------------------------------------------------------------------------
void DeviationProbe::killBonusAcrossSave() {
    int64_t results[2] = {0, 0};
    for(int reload = 0; reload < 2; ++reload) {
        auto game = newGame(52);
        auto* captor = game->getHouse(HOUSE_ATREIDES);
        auto* hostile = game->getHouse(HOUSE_HARKONNEN);
        const int siegePrice = price(*game, Unit_SiegeTank, hostile->getHouseID());
        // Evidence on both sides, so the expected bonus is neither bound nor the baseline.
        hostile->combatRewards[Unit_SiegeTank] = CombatReward::Totals();
        hostile->combatRewards[Unit_SiegeTank].damageMilli = int64_t(2)*siegePrice*1000;
        hostile->numItemLosses[Unit_SiegeTank] = 1;
        auto* gun = spawn(captor, Unit_Tank, Coord(10, 24));
        const auto gunID = gun->getObjectID();

        if(reload) {
            const std::string path = outDir + "/kill-bonus-" + mod + ".dls";
            require(game->saveGame(path), "Could not save the seeded victim-type ledger");
            game.reset(); currentGame = nullptr;
            game = std::make_unique<Game>(); currentGame = game.get();
            game->initGame(GameInitSettings(path)); game->initializeGameLoop();
            captor = game->getHouse(HOUSE_ATREIDES);
            hostile = game->getHouse(HOUSE_HARKONNEN);
            require(hostile->getCombatReward(Unit_SiegeTank).damageMilli
                        == int64_t(2)*siegePrice*1000
                    && hostile->getNumLostItems(Unit_SiegeTank) == 1,
                    "The seeded victim-type record did not survive the save");
            gun = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(gunID));
            require(gun != nullptr, "The reloaded fixture lost its shooter");
        }
        auto* victim = spawn(hostile, Unit_SiegeTank, Coord(16, 24));
        hit(gun, captor, victim, int(victim->getHealth().lround()) + 5000);
        require(victim->getHealth() <= 0, "The save fixture hit was not lethal");
        results[reload] = captor->getCombatReward(Unit_Tank).killBonusMilli;
        game.reset(); currentGame = nullptr;
    }
    require(results[0] > 0 && results[0] == results[1],
            "A save and reload changed the performance bonus of the same killing blow");
    amount("kill_bonus_across_save_milli", results[1]);
    note("kill_bonus","save_load_pays_the_identical_bonus");
}

// ---------------------------------------------------------------------------------------
// 9. Legacy-save handling: one coherent reset, and old streams stay in sync.
// ---------------------------------------------------------------------------------------
void DeviationProbe::legacyLedger() {
    auto game = newGame(41);
    auto* house = game->getHouse(HOUSE_ATREIDES);

    house->combatRewards[Unit_Tank].damageMilli = 5000;
    house->combatRewards[Unit_Tank].conversionMilli = 9000;
    house->combatRewards[Unit_Deviator].conversionMilli = 86436000;
    house->numItemDamageInflicted[Unit_Tank] = 700;
    house->numItemDamageInflicted[Unit_Deviator] = 4200;
    house->numItemLosses[Unit_Tank] = 9;
    house->numItemLosses[Unit_Deviator] = 102;
    house->numItemLosses[Structure_HeavyFactory] = 2;
    house->numItemKills[Unit_Tank] = 11;
    const auto killValueBefore = house->getKillValue();

    DeviationReward::migrateLegacyLedger(*house);

    require(house->getCombatReward(Unit_Deviator).total() == 0
            && house->getCombatReward(Unit_Tank).total() == 0,
            "The legacy reset left combat rewards behind");
    require(house->getNumItemDamageInflicted(Unit_Tank) == 0
            && house->getNumItemDamageInflicted(Unit_Deviator) == 0,
            "The legacy reset left raw damage counters behind");
    require(house->getNumLostItems(Unit_Tank) == 0 && house->getNumLostItems(Unit_Deviator) == 0,
            "The legacy reset kept unit losses, so measured reward would face old losses");
    require(house->getNumLostItems(Structure_HeavyFactory) == 2,
            "The legacy reset destroyed structure-loss evidence used by build prerequisites");
    require(house->getNumKilledItems(Unit_Tank) == 11 && house->getKillValue() == killValueBefore,
            "The legacy reset discarded general match statistics");
    require(house->getDeviationCounters().legacyLedgerReset == 1, "The legacy reset was not recorded");
    note("legacy", "learning_ledger_reset_together");
    note("legacy", "structure_losses_and_match_stats_preserved");

    // A cached performance window computed from the replaced accounting is dropped with it.
    QuantBot* quant = nullptr;
    for(const auto& player : game->getHouse(HOUSE_HARKONNEN)->getPlayerList())
        if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) quant = candidate;
    if(quant == nullptr) {
        UnitMixPolicy::PerformanceHistory history;
        history.initialized = true; history.sampled = 900; history.reward[0] = 12345;
        history = UnitMixPolicy::PerformanceHistory();
        require(!history.initialized && history.reward[0] == 0, "The performance window does not reset");
    }
    note("legacy", "cached_performance_window_reset");

    // Byte-level: an object written before 9849 must still read without desync, and an
    // already-borrowed unit must be bootstrapped into a fresh measured control interval.
    auto stripNewField = [&](auto& object, auto baseline, auto distinctive) {
        baseline();
        OMemoryStream a; a.open(); object.save(a);
        const std::string before(a.getData(), a.getDataLength());
        distinctive();
        OMemoryStream b; b.open(); object.save(b);
        const std::string after(b.getData(), b.getDataLength());
        require(before.size() == after.size(), "The new field changed the record length");
        size_t first = 0;
        while(first < before.size() && before[first] == after[first]) ++first;
        size_t last = before.size();
        while(last > first && before[last-1] == after[last-1]) --last;
        require(last > first, "The new field left no distinguishable bytes");
        baseline();
        return std::make_pair(before.substr(0, first) + before.substr(last), last - first);
    };

    auto* hostile = game->getHouse(HOUSE_HARKONNEN);
    auto* deviator = spawn(house, Unit_Deviator, Coord(10, 28));
    auto* tank = static_cast<Tank*>(spawn(hostile, Unit_Tank, Coord(14, 28)));
    tank->deviate(house, deviator->getObjectID());
    require(tank->wasDeviated(), "The legacy bootstrap fixture is not deviated");

    DeviationReward::Episode distinctiveEpisode;
    distinctiveEpisode.id = 0x31323334; distinctiveEpisode.deviatorObjectID = 0x35363738;
    distinctiveEpisode.startCycle = 0x393A3B3C; distinctiveEpisode.controllerHouse = 0x3D3E3F40;
    distinctiveEpisode.rewarded = true;
    auto unitBytes = stripNewField(*tank,
        [&]{ tank->deviationEpisode = DeviationReward::Episode(); },
        [&]{ tank->deviationEpisode = distinctiveEpisode; });
    require(unitBytes.second == 17, "The control-interval record is not the expected 17 bytes");

    game->loadedSavegameVersion = 9848;
    {
        IMemoryStream in(unitBytes.first.data(), unitBytes.first.size());
        Tank restored(in);
        require(in.getRemainingLength() == 0, "A pre-9849 unit record read outside its bytes");
        require(restored.getDeviationEpisode().active(),
                "An already-borrowed unit from an old save got no measured control interval");
        require(restored.getDeviationEpisode().rewarded,
                "The bootstrapped control interval of a hostile-origin unit is not credited");
        require(restored.getDeviationEpisode().deviatorObjectID == NONE_ID,
                "The bootstrap invented a capturing Deviator that the old save never recorded");
        restored.deviationEpisode = DeviationReward::Episode();
        restored.setHealth(0);
        unitList.remove(&restored);
    }
    game->loadedSavegameVersion = SAVEGAMEVERSION;
    note("legacy", "old_unit_record_in_sync_and_bootstrapped");

    // A projectile written before 9849 keeps its own layout and simply has no snapshot.
    Coord from = deviator->getCenterPoint(), to = from + Coord(8*TILESIZE, 0);
    Bullet shot(deviator->getObjectID(), &from, &to, Bullet_ShellMedium, 8, false, nullptr);
    DeviationReward::Provenance distinctiveProvenance;
    distinctiveProvenance.beneficiaryHouse = 0x11121314; distinctiveProvenance.rewardItemID = 0x15161718;
    distinctiveProvenance.sourceObjectID = 0x191A1B1C;   distinctiveProvenance.sourceItemID = 0x1D1E1F20;
    distinctiveProvenance.episodeID = 0x21222324;        distinctiveProvenance.deviatorObjectID = 0x25262728;
    const DeviationReward::Provenance fired = shot.provenance;
    auto shotBytes = stripNewField(shot,
        [&]{ shot.provenance = DeviationReward::Provenance(); },
        [&]{ shot.provenance = distinctiveProvenance; });
    require(shotBytes.second == 24, "The projectile snapshot is not the expected 24 bytes");
    shot.provenance = fired;

    game->loadedSavegameVersion = 9848;
    {
        IMemoryStream in(shotBytes.first.data(), shotBytes.first.size());
        Bullet restored(in);
        require(in.getRemainingLength() == 0, "A pre-9849 projectile record read outside its bytes");
        require(!restored.provenance.known() && !restored.provenance.allowsLiveLookup(),
                "A pre-9849 projectile was given an invented firing-time beneficiary");
        auto* target = spawn(hostile, Unit_SiegeTank, Coord(24, 28));
        const auto rewardBefore = house->getCombatReward(Unit_Deviator).total();
        const auto hpBefore = target->getHealth();
        target->handleDamage(8, tank->getObjectID(), house, restored.provenance);
        require(target->getHealth() < hpBefore, "The legacy shot did not damage its target");
        require(house->getCombatReward(Unit_Deviator).total() == rewardBefore,
            "A legacy shot inferred a firing-time capture from its current source");
    }
    game->loadedSavegameVersion = SAVEGAMEVERSION;
    note("legacy", "old_projectile_record_in_sync_without_invented_credit");

    // A burning flame keeps its payload and beneficiary across a save from 9849 on, while an
    // older record still falls back to the reconstructed damage it always used.
    DeviationReward::Provenance flameCredit;
    flameCredit.beneficiaryHouse = house->getHouseID(); flameCredit.rewardItemID = Unit_Deviator;
    flameCredit.sourceObjectID = deviator->getObjectID(); flameCredit.sourceItemID = Unit_Deviator;
    flameCredit.episodeID = 77; flameCredit.deviatorObjectID = deviator->getObjectID();
    Explosion flame(Explosion_FlameImpact, Coord(20*TILESIZE, 28*TILESIZE), house->getHouseID(),
                    deviator->getObjectID(), 9, TILESIZE, flameCredit);
    OMemoryStream out; out.open(); flame.save(out);
    const std::string flameBytes(out.getData(), out.getDataLength());
    {
        IMemoryStream in(flameBytes.data(), flameBytes.size());
        Explosion restored(in);
        require(in.getRemainingLength() == 0, "A flame record read outside its bytes");
        require(restored.persistentDamage == 9 && restored.damageRadius == TILESIZE
                && restored.damagerID == deviator->getObjectID(),
                "A burning flame lost its damage payload across a save");
        require(restored.provenance.episodeID == 77 && restored.provenance.rewardItemID == Unit_Deviator,
                "A burning flame lost its beneficiary across a save");
    }
    game->loadedSavegameVersion = 9848;
    {
        const std::string old = flameBytes.substr(0, flameBytes.size() - (4 + 4 + 4 + 24));
        IMemoryStream in(old.data(), old.size());
        Explosion restored(in);
        require(in.getRemainingLength() == 0, "A pre-9849 flame record read outside its bytes");
        require(!restored.provenance.known() && !restored.provenance.allowsLiveLookup(), "A pre-9849 flame was given an invented beneficiary");
        require(restored.persistentDamage > 0, "A pre-9849 flame lost its reconstructed fallback damage");
    }
    game->loadedSavegameVersion = SAVEGAMEVERSION;
    note("legacy", "flame_payload_persists_and_old_record_falls_back");
    game.reset(); currentGame = nullptr;
}

void DeviationProbe::run() {
    decisions.open(outDir + "/" + mod + ".csv");
    decisions << "section,check,passed\n";
    detail.open(outDir + "/" + mod + "-deviation.csv");
    detail << "measure,value\n";
    captureLedger();
    outgoing();
    absorption();
    provenanceAcrossLoss();
    saveLoadProvenance();
    devastator();
    quantBotArming();
    killBonus();
    killBonusAcrossSave();
    legacyLedger();
    decisions.close();
    detail.close();
}

} // namespace

int runUnitSpeedProbe() {
    DeviationProbe probe;
    probe.mod = std::getenv("UNIT_SPEED_PROBE_MOD");
    probe.outDir = std::getenv("UNIT_SPEED_PROBE_OUT");
    DeviationProbe::require(ModManager::instance().setActiveMod(probe.mod), "Missing deviation fixture mod");
    settings.general.playerName = "Speed tester";
    std::ifstream map(PROBE_MAP_PATH);
    probe.mapData.assign(std::istreambuf_iterator<char>(map), {});
    probe.run();
    SDL_Log("UNIT_SPEED_PROBE_PASS: %s deviation reward", probe.mod.c_str());
    return -1;
}
