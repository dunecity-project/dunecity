// Private real-engine entry point. The adjacent runner replaces main.cpp only and links the
// production objects. Capture commands prepare occupants; every policy observation runs Both.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <CommandManager.h>
#include <sand.h>
#include <FileClasses/INIFile.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <players/QuantBotConfig.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace GarrisonHoldProbe {
constexpr int mapWidth = 64, mapHeight = 48;
SettingsClass::GameOptionsClass modOptions;
QuantBotConfig probeConfig;
std::string outputDirectory;

void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error(why);
}

std::string stylePath(bool hold) {
    return outputDirectory + (hold ? "/garrison-hold-on.ini" : "/garrison-hold-off.ini");
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

struct Fixture {
    std::string name;
    std::unique_ptr<Game> game;
    House* own = nullptr;
    House* enemy = nullptr;
    QuantBot* bot = nullptr;
    HumanPlayer* human = nullptr;
    Player* opponent = nullptr;
    int controllerPasses = 0;

    Fixture(std::string label, bool hold, bool permission = true, bool defaultHold = false,
            bool sharedHuman = false, bool configuredTactics = false) : name(std::move(label)) {
        getQuantBotConfig() = probeConfig;
        auto rules = modOptions;
        rules.garrisoningAndCapturing = permission;
        rules.fogOfWar = false;
        rules.startWithExploredMap = true;
        rules.concreteRequired = false;
        rules.immortalHumanPlayer = false;
        rules.instantBuild = false;
        GameInitSettings init("garrison-hold.ini", fixtureMap(), "Garrison Hold probe", true, rules);
        init.randomSeed = 20261128;
        GameInitSettings::HouseInfo owners(HOUSE_HARKONNEN, 1);
        owners.addPlayerInfo({"Hold QuantBot", "qBotBrutal"});
        if(sharedHuman) owners.addPlayerInfo({"Hold human", HUMANPLAYERCLASS});
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
        require(bot && opponent && pLocalHouse == enemy && bool(human) == sharedHuman,
                name + ": real controller roster differs");
        auto config = std::make_shared<QuantBotConfig>(probeConfig);
        auto& force = config->force;
        force.zoneGarrison = force.zoneFrontGarrison = force.zoneGarrisonReinforce = configuredTactics;
        force.zoneCaptureRaid = force.zoneCaptureLevy = force.zoneCaptureChain = configuredTactics;
        // Exercise the same INI overlay used by per-house @style and CLI experiment arms.
        if(!defaultHold) require(config->applyOverrides(stylePath(hold)), name + ": per-house style missing");
        bot->configOverride = config;
        bot->setRole(AIRole::Both);
        require(bot->getRole() == AIRole::Both && bot->botConfig().force.garrisonHold == hold
                    && !getQuantBotConfig().force.garrisonHold,
                name + ": raw per-house Hold leaked into the global/control config");
        require(ZoneStructure::garrisonsEnabled() == permission, name + ": effective core permission differs");
        if(permission) require(bot->wantsGarrisonTactic(false), name + ": bundled tactics did not activate");
        for(House* house : {own, enemy}) {
            house->startingCredits = house->storedCredits = house->cityCredits = 0;
            house->enforceCreditCeiling();
        }
    }

    ~Fixture() {
        std::fflush(stdout); // Finish the observation before teardown can emit native events.
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    UnitBase* unit(Uint32 id) const {
        return dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
    }
    ZoneStructure* zone(Uint32 id) const {
        return dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(id));
    }
    bool inside(Uint32 id, const ZoneStructure* target) const {
        return target && std::find(target->getGarrison().begin(), target->getGarrison().end(), id)
            != target->getGarrison().end();
    }
    void queue(const Command& command) { game->getCommandManager().addCommand(command); }
    void step() {
        const Uint32 before = game->getGameCycleCount();
        const auto timer = bot->attackTimer;
        const bool both = bot->getRole() == AIRole::Both;
        game->updateGameState();
        require(game->getGameCycleCount() > before, name + ": native game cycle did not advance");
        // A changed production attack timer witnesses a real unit-policy pass, after
        // updateZoneGarrisons(), rather than estimating passes from elapsed wall/cycle time.
        if(both && bot->attackTimer != timer) ++controllerPasses;
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
    ZoneStructure* addZone(House* house, int kind, Coord at) {
        const Coord size = getStructureSize(kind);
        for(int y = at.y; y < at.y + size.y; ++y) for(int x = at.x; x < at.x + size.x; ++x) {
            require(currentGameMap->tileExists(x, y) && !currentGameMap->getTile(x, y)->hasAGroundObject(),
                    name + ": occupied fixture footprint");
            currentGameMap->getTile(x, y)->setType(Terrain_Rock);
        }
        auto* result = dynamic_cast<ZoneStructure*>(house->placeStructure(NONE_ID, kind, at.x, at.y, true, true));
        require(result != nullptr, name + ": real zone placement failed");
        result->setStartingLevel(2);
        result->setVisible(VIS_ALL, true);
        return result;
    }
    UnitBase* spawn(House* house, int kind, Coord at) {
        auto* result = house->createUnit(kind, true);
        require(result && currentGameMap->tileExists(at)
                    && !currentGameMap->getTile(at)->hasANonInfantryGroundObject(), name + ": invalid unit deployment");
        result->deploy(at);
        result->setVisible(VIS_ALL, true);
        require(result->isActive() && result->getHealth() > 0, name + ": real unit did not deploy");
        return result;
    }
    void held(const ZoneStructure* target, const std::vector<Uint32>& ids) const {
        require(target && target->getGarrisonSize() == int(ids.size()), name + ": original occupancy changed");
        for(Uint32 id : ids) {
            const auto* man = unit(id);
            require(inside(id, target) && man && man->getHealth() > 0 && !man->isActive() && man->isPickedUp(),
                    name + ": an original live ID is no longer physically hidden inside");
        }
    }
    std::vector<Uint32> occupy(ZoneStructure* target, int count, bool mixed = false) {
        const AIRole saved = bot->getRole();
        bot->setRole(AIRole::Builder); // Setup only: restored before any treatment observation.
        std::vector<Uint32> ids;
        for(int i = 0; i < count; ++i) {
            auto* man = spawn(own, mixed && i % 2 ? Unit_Trooper : Unit_Soldier,
                              target->getLocation() + Coord(-1, i % 2));
            ids.push_back(man->getObjectID());
            queue(Command(bot->getPlayerID(), CMD_INFANTRY_CAPTURE, man->getObjectID(), target->getObjectID()));
        }
        require(until(10, [&] { return target->getGarrisonSize() == count; }), name + ": native Capture setup never entered");
        held(target, ids);
        bot->setRole(saved);
        require(bot->getRole() == AIRole::Both, name + ": policy observation was not restored to Both");
        return ids;
    }
    void units(const char* phase, Uint32 targetID, const std::vector<Uint32>& ids) const {
        const auto* target = zone(targetID);
        for(Uint32 id : ids) {
            const auto* man = unit(id);
            std::printf("GARRISON_HOLD_UNIT: name=%s phase=%s zone=%u id=%u item=%d live=%d active=%d hidden=%d inside=%d health_raw=%lld\n",
                name.c_str(), phase, targetID, id, man ? man->getItemID() : -1,
                int(man && man->getHealth() > 0), int(man && man->isActive()), int(man && man->isPickedUp()),
                int(inside(id, target)), man ? static_cast<long long>(man->getHealth().getRawValue()) : 0LL);
        }
        std::fflush(stdout);
    }
};

void configSurface() {
    QuantBotConfig defaults;
    require(!defaults.force.garrisonHold && !probeConfig.force.garrisonHold, "Code/shipped GarrisonHold is not default OFF");
    const INIFile shipped(ModManager::instance().getActiveQuantBotConfigPath());
    require(shipped.hasKey("Force Concentration", "GarrisonHold")
                && !shipped.getBoolValue("Force Concentration", "GarrisonHold", true),
            "Actually shipped Dune City INI is missing GarrisonHold=0");
    auto overlay = defaults;
    overlay.attackTimerMs = 12345;
    const std::string originalHash = overlay.getConfigHash();
    require(overlay.applyOverrides(stylePath(false)) && overlay.getConfigHash() == originalHash,
            "Explicit OFF changed the native hash or unrelated configuration");
    require(overlay.applyOverrides(stylePath(true)) && overlay.force.garrisonHold
                && overlay.attackTimerMs == 12345 && overlay.getConfigHash() != originalHash,
            "ON overlay was not loaded/hashed independently");
    const std::string saved = outputDirectory + "/garrison-hold-saved.ini";
    require(overlay.save(saved), "GarrisonHold save failed");
    const INIFile savedINI(saved);
    require(savedINI.getBoolValue("Force Concentration", "GarrisonHold", false), "Saved INI lost GarrisonHold");
    QuantBotConfig loaded;
    require(loaded.load(saved) && loaded.force.garrisonHold && loaded.attackTimerMs == 12345,
            "GarrisonHold load/save round trip lost configuration");
    require(overlay.applyOverrides(stylePath(false)) && overlay.getConfigHash() == originalHash,
            "OFF overlay failed to restore exactly the previous configuration/hash");
    std::printf("GARRISON_HOLD_CONFIG: code_default=0 shipped_default=0 saved_on=1 loaded_on=1 overlay_independent=1 off_hash_unchanged=1 on_hash_changed=1\n");
}

void quiet(bool hold) {
    Fixture f(hold ? "quiet_on" : "quiet_default_off", hold, true, !hold);
    auto* target = f.addZone(f.own, Structure_ZoneResidential, {32, 20});
    const auto ids = f.occupy(target, DuneCity::ZoneGarrison::kCapacity);
    f.units("onset", target->getObjectID(), ids);
    const Uint32 began = f.game->getGameCycleCount();
    const int passes = f.controllerPasses;
    std::set<Uint32> departures;
    f.until(60, [&] {
        require(f.bot->getRole() == AIRole::Both, f.name + ": controller role changed");
        for(Uint32 id : ids) {
            const auto* man = f.unit(id);
            require(man && man->getHealth() > 0, f.name + ": quiet original died/disappeared");
            if(!f.inside(id, target)) departures.insert(id);
        }
        if(hold) f.held(target, ids);
        return false;
    });
    const int observedPasses = f.controllerPasses - passes;
    require(observedPasses >= 8, f.name + ": fewer than eight witnessed real controller policy passes");
    require(hold ? departures.empty() : departures.size() == ids.size() && !target->isGarrisoned(),
            f.name + ": paired quiet treatment had no required release/retention effect");
    if(!hold) for(Uint32 id : ids) require(f.unit(id)->isActive() && !f.unit(id)->isPickedUp(), f.name + ": released ID is not on map");
    f.units("end", target->getObjectID(), ids);
    std::printf("GARRISON_HOLD_RESULT: name=%s hold=%d permission=1 both=1 zone=%u originals=%zu retained=%d departures=%zu ai_passes=%d cycles=%u\n",
        f.name.c_str(), int(hold), target->getObjectID(), ids.size(), target->getGarrisonSize(), departures.size(),
        observedPasses, f.game->getGameCycleCount() - began);
}

void replacement(bool hold) {
    Fixture f(hold ? "replacement_on" : "replacement_off", hold);
    auto* target = f.addZone(f.own, Structure_ZoneCommercial, {32, 20});
    // Keep the native power-degradation loop out of this no-damage policy fixture:
    // a bare Commercial consumes power and loses HP even when nobody shoots it.
    auto* power = f.own->placeStructure(NONE_ID, Structure_WindTrap, 4, 4, true, true);
    require(power && f.own->getProducedPower() >= f.own->getPowerRequirement(),
            f.name + ": real power production does not cover the shelter");
    const auto ids = f.occupy(target, DuneCity::ZoneGarrison::kCapacity);
    auto* free = f.spawn(f.own, Unit_Trooper, target->getLocation() - Coord(15, 0));
    auto* threat = f.spawn(f.enemy, Unit_Tank,
                           target->getLocation() + Coord(target->getStructureSizeX() - 1 + 6, 0));
    Command(f.opponent->getPlayerID(), CMD_UNIT_SETMODE, threat->getObjectID(), STOP).executeCommand();
    using Role = QuantBot::UnitRole;
    const Role claim = f.bot->unitRole(free, {Role::Human, Role::Repair, Role::BaseDefence, Role::LiveDefence,
        Role::Defence, Role::Wave, Role::Escort, Role::ColonyGuard});
    const FixPoint distance = blockDistance(threat->getLocation(), target->getClosestPoint(threat->getLocation()));
    require(claim == Role::Free && free->isRespondable() && !free->isAttackMoving() && !free->getTarget()
                && !free->getAttackPos().isValid() && free->getGarrisonRelocationTarget() == NONE_ID
                && !target->mayEnterGarrison(free), f.name + ": full shelter/free Trooper opportunity was not real");
    require(distance == FixPoint(6) && threat->isVisible(f.own->getTeamID()) && threat->canAttack()
                && threat->getAttackMode() == STOP && !threat->getTarget(), f.name + ": visible non-firing threat missing");
    for(Uint32 id : ids) require(f.unit(id)->getItemID() == Unit_Soldier && f.unit(id)->getWeaponRange() < distance,
                               f.name + ": shelter is not full of original out-of-range Soldiers");
    const Coord threatAt = threat->getLocation();
    const FixPoint zoneHealth = target->getHealth();
    f.units("onset", target->getObjectID(), ids);
    const Uint32 began = f.game->getGameCycleCount();
    const int passes = f.controllerPasses;
    std::set<Uint32> departures;
    const bool entered = f.until(hold ? 60 : 40, [&] {
        for(Uint32 id : ids) {
            require(f.unit(id) && f.unit(id)->getHealth() > 0, f.name + ": replacement lost an original Soldier");
            if(!f.inside(id, target)) departures.insert(id);
        }
        if(threat->getAttackMode() != STOP || threat->getTarget() || threat->getLocation() != threatAt
           || target->getHealth() != zoneHealth) {
            std::printf("GARRISON_HOLD_CONTROL: name=%s cycle=%u mode=%d threat_target=%u"
                        " threat_x=%d threat_y=%d health_raw=%lld onset_health_raw=%lld"
                        " power_supply=%d power_demand=%d\n",
                f.name.c_str(), f.game->getGameCycleCount(), int(threat->getAttackMode()),
                threat->target.getObjectID(), threat->getLocation().x, threat->getLocation().y,
                static_cast<long long>(target->getHealth().getRawValue()),
                static_cast<long long>(zoneHealth.getRawValue()),
                f.own->getProducedPower(), f.own->getPowerRequirement());
        }
        require(threat->getAttackMode() == STOP && !threat->getTarget() && threat->getLocation() == threatAt
                    && target->getHealth() == zoneHealth, f.name + ": controlled threat fired/moved or shelter took damage");
        if(hold) f.held(target, ids);
        return !hold && f.inside(free->getObjectID(), target);
    });
    int retained = 0;
    for(Uint32 id : ids) retained += int(f.inside(id, target));
    const int observedPasses = f.controllerPasses - passes;
    require(observedPasses >= (hold ? 8 : 1), f.name + ": real controller passes were not witnessed");
    require(hold ? !entered && departures.empty() && retained == int(ids.size()) && !f.inside(free->getObjectID(), target)
                 : entered && departures.size() == 1 && retained == int(ids.size()) - 1,
            f.name + ": paired full-Soldier replacement treatment did not activate correctly");
    f.units("end", target->getObjectID(), ids);
    std::printf("GARRISON_HOLD_RESULT: name=%s hold=%d permission=1 both=1 zone=%u originals=%zu retained=%d departures=%zu candidate=%u candidate_inside=%d free_troopers=1 threat=%u threat_distance_raw=%lld threat_firing=0 ai_passes=%d cycles=%u\n",
        f.name.c_str(), int(hold), target->getObjectID(), ids.size(), retained, departures.size(), free->getObjectID(),
        int(f.inside(free->getObjectID(), target)), threat->getObjectID(), static_cast<long long>(distance.getRawValue()),
        observedPasses, f.game->getGameCycleCount() - began);
}

void relocation() {
    Fixture f("relocation_on", true);
    auto* source = f.addZone(f.own, Structure_ZoneResidential, {20, 20});
    auto* destination = f.addZone(f.own, Structure_ZoneIndustrial, {32, 20});
    const auto ids = f.occupy(source, 2, true);
    f.units("onset", source->getObjectID(), ids);
    const FixPoint boundary = FixPoint(source->getMaxHealth()) * 30 / 100;
    source->setHealth(boundary);
    f.run(1);
    f.held(source, ids);
    require(!source->tooDamagedToHold() && !source->hasAttemptedGarrisonRelocation(), f.name + ": exactly 30% relocated");
    const Uint32 began = f.game->getGameCycleCount();
    source->setHealth(boundary - FixPoint::FromRawValue(1));
    f.step();
    require(source->getGarrisonSize() == 0 && source->hasAttemptedGarrisonRelocation()
                && destination->getGarrisonSize() == 0, f.name + ": building-owned departure was blocked/teleported");
    std::vector<Coord> exits;
    std::vector<bool> walked(ids.size(), false);
    for(Uint32 id : ids) {
        const auto* man = f.unit(id);
        require(man && man->isActive() && !man->isPickedUp()
                    && man->getGarrisonRelocationTarget() == destination->getObjectID(), f.name + ": no living reserved departure");
        exits.push_back(man->getLocation());
    }
    require(f.until(45, [&] {
        for(size_t i = 0; i < ids.size(); ++i) {
            const auto* man = f.unit(ids[i]);
            require(man && man->getHealth() > 0, f.name + ": relocation lost an original ID");
            if(man->isActive() && man->getLocation() != exits[i]) walked[i] = true;
        }
        return destination->getGarrisonSize() == int(ids.size());
    }), f.name + ": reserved originals never reached healthy friendly shelter");
    f.held(destination, ids);
    for(size_t i = 0; i < ids.size(); ++i) require(walked[i] && f.unit(ids[i])->getGarrisonRelocationTarget() == NONE_ID,
                                               f.name + ": arrival did not walk/release its reservation");
    f.until(15, [&] { f.held(destination, ids); return false; });
    f.units("end", destination->getObjectID(), ids);
    std::printf("GARRISON_HOLD_RESULT: name=relocation_on hold=1 permission=1 both=1 zone=%u destination=%u originals=%zu retained=%d departures=%zu walked=%zu arrivals=%d exact30_held=1 health_raw=%lld max_health=%d cycles=%u\n",
        source->getObjectID(), destination->getObjectID(), ids.size(), destination->getGarrisonSize(), ids.size(),
        walked.size(), destination->getGarrisonSize(), static_cast<long long>((boundary - FixPoint::FromRawValue(1)).getRawValue()),
        source->getMaxHealth(), f.game->getGameCycleCount() - began);
}

void destruction() {
    Fixture f("destruction_on", true);
    auto* source = f.addZone(f.own, Structure_ZoneIndustrial, {32, 20});
    const Uint32 sourceID = source->getObjectID();
    const auto ids = f.occupy(source, 3, true);
    require(f.game->objectData.garrisonCollapseDamagePercent > 0 && f.game->objectData.garrisonCollapseDamagePercent < 100,
            f.name + ": normal nonzero/nonlethal-at-full-health collapse rule missing");
    f.unit(ids.front())->setHealth(1); // A real weak occupant must die; full-health originals must survive.
    std::vector<FixPoint> health;
    for(Uint32 id : ids) health.push_back(f.unit(id)->getHealth());
    f.units("onset", sourceID, ids);
    const size_t explosions = f.game->getExplosionList().size();
    const Uint32 began = f.game->getGameCycleCount();
    source->handleDamage(100000, NONE_ID, f.enemy);
    require(source->getHealth() <= 0 && source->getGarrisonSize() == int(ids.size()),
            f.name + ": one lethal native hit did not reach destruction with all occupants still inside");
    for(size_t i = 0; i < ids.size(); ++i) require(f.unit(ids[i])->getHealth() == health[i], f.name + ": lethal building hit damaged the interior");
    require(f.until(5, [&] { return f.zone(sourceID) == nullptr; }), f.name + ": real zone destruction never completed");
    require(f.game->getExplosionList().size() > explosions, f.name + ": no actual building explosion");
    int living = 0, killed = 0;
    for(size_t i = 0; i < ids.size(); ++i) {
        const auto* man = f.unit(ids[i]);
        if(!man || man->getHealth() <= 0) { ++killed; continue; }
        const int injury = static_cast<int>((int64_t{man->getMaxHealth()}
            * f.game->objectData.garrisonCollapseDamagePercent + 99) / 100);
        require(man->isActive() && !man->isPickedUp() && man->isVisible()
                    && currentGameMap->tileExists(man->getLocation()) && man->getHealth() == health[i] - injury,
                f.name + ": a living original did not receive native on-map collapse ejection/injury");
        ++living;
    }
    require(living == 2 && killed == 1 && living + killed == int(ids.size()), f.name + ": original IDs were not completely accounted");
    f.units("end", sourceID, ids);
    std::printf("GARRISON_HOLD_RESULT: name=destruction_on hold=1 permission=1 both=1 zone=%u originals=%zu retained=0 departures=%zu deployed=%d killed=%d explosions=%zu cycles=%u\n",
        sourceID, ids.size(), ids.size(), living, killed, f.game->getExplosionList().size() - explosions,
        f.game->getGameCycleCount() - began);
}

void permissionOff() {
    Fixture f("permission_off", true, false, false, false, true);
    auto* own = f.addZone(f.own, Structure_ZoneResidential, {28, 20});
    auto* enemy = f.addZone(f.enemy, Structure_ZoneCommercial, {44, 30});
    auto* guard = f.spawn(f.own, Unit_Trooper, {27, 20});
    auto* attacker = f.spawn(f.own, Unit_Trooper, {41, 30});
    require(!own->mayEnterGarrison(guard) && !enemy->mayEnterGarrison(attacker), f.name + ": core permission was bypassed");
    f.queue(Command(f.bot->getPlayerID(), CMD_INFANTRY_CAPTURE, guard->getObjectID(), own->getObjectID()));
    f.queue(Command(f.bot->getPlayerID(), CMD_INFANTRY_CAPTURE, attacker->getObjectID(), enemy->getObjectID()));
    f.step();
    require(!own->isGarrisoned() && !enemy->isGarrisoned(), f.name + ": native Capture ignored OFF");
    const FixPoint health = enemy->getHealth();
    f.queue(Command(f.bot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, attacker->getObjectID(), enemy->getObjectID()));
    const Uint32 began = f.game->getGameCycleCount();
    require(f.until(20, [&] {
        require(!ZoneStructure::garrisonsEnabled() && !own->isGarrisoned() && !enemy->isGarrisoned()
                    && own->getOwner() == f.own && enemy->getOwner() == f.enemy, f.name + ": Hold/configured tactics bypassed OFF");
        return enemy->getHealth() < health;
    }), f.name + ": normal native attack never activated while capture was forbidden");
    require(f.until(10, [&] {
        require(!ZoneStructure::garrisonsEnabled() && !own->isGarrisoned() && !enemy->isGarrisoned()
                    && own->getOwner() == f.own && enemy->getOwner() == f.enemy,
                f.name + ": a later Both pass bypassed permission OFF");
        return f.controllerPasses > 0;
    }), f.name + ": no actual Both controller policy pass while permission was OFF");
    std::printf("GARRISON_HOLD_RESULT: name=permission_off hold=1 permission=0 both=1 zone=%u enemy_zone=%u configured_tactics=6 capture_commands=2 entries=0 damage_raw=%lld ai_passes=%d cycles=%u\n",
        own->getObjectID(), enemy->getObjectID(), static_cast<long long>((health - enemy->getHealth()).getRawValue()),
        f.controllerPasses, f.game->getGameCycleCount() - began);
}

void humanOverride() {
    Fixture f("human_override", true, true, false, true);
    auto* source = f.addZone(f.own, Structure_ZoneResidential, {32, 20});
    const auto ids = f.occupy(source, 2);
    f.units("onset", source->getObjectID(), ids);
    f.queue(Command(f.human->getPlayerID(), CMD_ZONE_EVACUATE, source->getObjectID()));
    require(f.until(5, [&] { return !source->isGarrisoned(); }), f.name + ": explicit human evacuation was blocked by Hold");
    for(Uint32 id : ids) require(f.unit(id) && f.unit(id)->getHealth() > 0 && f.unit(id)->isActive()
                                   && !f.unit(id)->isPickedUp(), f.name + ": human evacuation lost an original");
    f.units("end", source->getObjectID(), ids);
    std::printf("GARRISON_HOLD_RESULT: name=human_override hold=1 permission=1 both=1 zone=%u originals=%zu retained=0 departures=%zu human_commands=1\n",
        source->getObjectID(), ids.size(), ids.size());
}
} // namespace GarrisonHoldProbe

int runGarrisonHoldProbe() {
    using namespace GarrisonHoldProbe;
    const char* output = std::getenv("GARRISON_HOLD_OUT");
    require(output && *output, "Missing GARRISON_HOLD_OUT");
    outputDirectory = output;
    settings.general.playerName = "Hold observer";
    require(ModManager::instance().setActiveMod("dunecity"), "Normal bundled Dune City content missing");
    modOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    struct Restore { QuantBotConfig value; ~Restore() { getQuantBotConfig() = value; } } restore{getQuantBotConfig()};
    probeConfig = QuantBotConfig();
    require(probeConfig.load(ModManager::instance().getActiveQuantBotConfigPath()), "Cannot load actually shipped QuantBot INI");
    getQuantBotConfig() = probeConfig;
    int cases = 0, failures = 0;
    auto check = [&](const char* name, const std::function<void()>& test) {
        ++cases;
        std::printf("GARRISON_HOLD_BEGIN: name=%s\n", name);
        std::fflush(stdout);
        try {
            test();
            std::printf("GARRISON_HOLD_CASE: name=%s pass=1\n", name);
        } catch(const std::exception& error) {
            ++failures;
            currentGame = nullptr;
            std::printf("GARRISON_HOLD_FAIL: name=%s pass=0 reason=%s\n", name, error.what());
        }
        std::fflush(stdout);
    };
    check("config_surface", configSurface);
    check("quiet_default_off", [] { quiet(false); });
    check("quiet_on", [] { quiet(true); });
    check("replacement_off", [] { replacement(false); });
    check("replacement_on", [] { replacement(true); });
    check("relocation_on", relocation);
    check("destruction_on", destruction);
    check("permission_off", permissionOff);
    check("human_override", humanOverride);
    std::printf("GARRISON_HOLD_PASS: cases=%d failures=%d\n", cases, failures);
    std::fflush(stdout);
    require(failures == 0, std::to_string(failures) + " Garrison Hold probe cases failed");
    return MENU_QUIT_DEFAULT;
}
