// Test-only entry point linked with the production objects by run-research-lab-probe.py.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Command.h>
#include <Menu/RandomSkirmishMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <mod/ModManager.h>
#include <structures/BuilderBase.h>
#include <structures/ResearchLab.h>
#include <Network/GameStateDigest.h>
#include <Network/WorkshopGameContent.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <array>
#include <fstream>
#include <memory>
#include <vector>

namespace {
void researchRequire(bool value, const char* why) {
    if(!value) {
        SDL_Log("RESEARCH_LAB_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

std::unique_ptr<Game> researchGame(bool enabled, bool ai = false, int mapSize = 64,
                                 TechResearch::Pace pace = TechResearch::Pace::Normal) {
    using namespace SkirmishGenerator;
    Params params;
    params.house = HOUSE_ATREIDES;
    params.mapSize = mapSize;
    params.techLevel = 1;
    params.researchLab = enabled;
    params.mod = "dunecity";
    params.mode = Mode::DuneCity;
    params.seed = 20261102u;
    params.credits = ai ? 20000 : 100000;
    params.allies = 0;
    params.enemies = 1;
    params.sandPercent = 65;
    params.rockPercent = 30;
    params.mountainPercent = 5;
    params.spice = Spice::High;
    params.playerSide = Side{"", BaseSize::Small, 0, 0};
    params.enemySide = Side{"HumanPlayer", BaseSize::Outpost, 0, 0};
    const auto map = generate(params, RandomSkirmish::loadCatalogue(params.mode));
    for(const auto& entry : map.structures)
        researchRequire(entry.item != Structure_ResearchLab, "Random Skirmish granted a free lab");
    auto options = effectiveGameOptions;
    options.researchLab = enabled;
    options.researchPace = pace;
    options.trooperAntiAir = false;
    options.concreteRequired = false;
    GameInitSettings init(map.name, map.ini, false, options);
    init.randomSeed = params.seed;
    for(const auto& entry : map.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(entry.house), entry.team);
        info.addPlayerInfo({entry.house == HOUSE_ATREIDES ? "Research tester" : "Enemy",
                           entry.house == HOUSE_ATREIDES && ai ? "qBotSupportHard" : "HumanPlayer"});
        init.addHouseInfo(info);
    }
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    researchRequire(game->techLevel == 1, "Fixture did not start at match tech 1");
    return game;
}

void researchEnd(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalHouse = nullptr;
    pLocalPlayer = nullptr;
}

ResearchLab* researchPlaceLab(House* owner) {
    for(int y = 1; y + 2 < currentGameMap->getSizeY(); ++y)
    for(int x = 1; x + 3 < currentGameMap->getSizeX(); ++x) {
        if(!currentGameMap->okayToPlaceStructure(x, y, 3, 2, false, owner, false, Structure_ResearchLab)) continue;
        auto* lab = dynamic_cast<ResearchLab*>(owner->placeStructure(NONE_ID, Structure_ResearchLab, x, y, true));
        if(lab) return lab;
    }
    researchRequire(false, "No legal lab site beside the starting base");
    return nullptr;
}

BuilderBase* researchYard(House* owner) {
    for(auto* building : structureList)
        if(building->getOwner() == owner && building->getItemID() == Structure_ConstructionYard)
            return static_cast<BuilderBase*>(building);
    researchRequire(false, "Missing construction yard");
    return nullptr;
}

Uint64 researchLegacyHouseDigest(Game& game) {
    GameStateDigest::Hasher houses;
    for(int id = 0; id < NUM_HOUSES; ++id) {
        const auto* owner = game.getHouse(id);
        houses.mixUint8(static_cast<Uint8>(id));
        houses.mixUint8(owner != nullptr ? 1 : 0);
        if(!owner) continue;
        houses.mixInt32(owner->getCredits());
        houses.mixInt32(owner->getNumStructures());
        houses.mixInt32(owner->getNumUnits());
        houses.mixUint32(owner->getSpiceIncomeMultiplier());
    }
    return houses.value();
}
} // namespace

int runResearchLabProbe() {
    const std::string out = std::getenv("RESEARCH_LAB_PROBE_OUT");
    researchRequire(RandomSkirmish::activateMod("dunecity"), "DuneCity activation failed");
    settings.general.playerName = "Research tester";
    std::ofstream results(out + "/research.csv");
    results << "case,cycle,tech_level,target,remaining,lab,credits\n";
    auto note = [&](const char* name, const Game& game, const House* owner) {
        results << name << ',' << game.getGameCycleCount() << ',' << owner->getTechLevel() << ','
                << owner->getTechResearchTarget() << ',' << owner->getTechResearchRemaining() << ','
                << owner->getTechResearchLabID() << ',' << owner->getCredits() << '\n';
    };

    if(const char* savedMatch = std::getenv("RESEARCH_LAB_LOAD_SAVE")) {
        // Fail noninteractively if the exact pinned content is unavailable.
        const auto bytes = readCompleteFile(savedMatch);
        IMemoryStream input(bytes.data(), bytes.size());
        GameInitSettings::HouseInfoList rows;
        auto setup = GameInitSettings::readSaveSetup(input, rows);
        WorkshopGameContent::resolveMod(setup, false);
        auto loaded = std::make_unique<Game>();
        currentGame = loaded.get();
        loaded->bReplay = true;
        loaded->initGame(GameInitSettings(savedMatch));
        loaded->bReplay = false;
        loaded->initializeGameLoop();
        const auto& rules = loaded->getGameInitSettings().getGameOptions();
        researchRequire(rules.researchPace == setup.getGameOptions().researchPace, "Save lost its Research Pace");
        researchRequire(rules.mountainScenery == setup.getGameOptions().mountainScenery, "Save changed its scenery rule");
        SDL_Log("RESEARCH_SAVED_MATCH: cycle=%u version=%u width=%d height=%d pace=%u options=%s",
            loaded->getGameCycleCount(), loaded->getLoadedSavegameVersion(), currentGameMap->getSizeX(),
            currentGameMap->getSizeY(), static_cast<unsigned>(rules.researchPace), rules.getHash().c_str());
        for(const auto& row : rows) if(const House* house = loaded->getHouse(row.houseID)) {
            note("old_save_loaded", *loaded, house);
            SDL_Log("RESEARCH_SAVED_HOUSE: house=%d tech=%d target=%d remaining=%d credits=%d next_cost=%d next_cycles=%d",
                house->getHouseID(), house->getTechLevel(), house->getTechResearchTarget(), house->getTechResearchRemaining(),
                house->getCredits(), house->getTechResearchCost(), house->techResearchCycles(std::min(9, house->getTechLevel() + 1)));
            if(house->isTechResearchActive()) {
                OMemoryStream originalState;
                originalState.open();
                originalState.writeSint32(house->getTechLevel());
                originalState.writeSint32(house->getTechResearchTarget());
                originalState.writeSint32(house->getTechResearchRemaining());
                originalState.writeUint32(house->getTechResearchLabID());
                const std::string encoded(originalState.getData(), originalState.getDataLength());
                researchRequire(bytes.find(encoded) != std::string::npos, "Load changed the serialized paid research timer");
            }
        }
        loaded->updateGameState();
        std::vector<std::array<Uint32, 5>> snapshots;
        for(const auto& row : rows) if(const House* house = loaded->getHouse(row.houseID))
            snapshots.push_back({static_cast<Uint32>(row.houseID), static_cast<Uint32>(house->getTechLevel()),
                static_cast<Uint32>(house->getTechResearchTarget()), static_cast<Uint32>(house->getTechResearchRemaining()),
                house->getTechResearchLabID()});
        const std::string resaved = out + "/research-user-resaved.dls";
        researchRequire(loaded->saveGame(resaved), "User's migrated save did not resave");
        researchEnd(loaded);
        loaded = std::make_unique<Game>();
        currentGame = loaded.get();
        loaded->bReplay = true;
        loaded->initGame(GameInitSettings(resaved));
        loaded->bReplay = false;
        loaded->initializeGameLoop();
        researchRequire(loaded->getLoadedSavegameVersion() == SAVEGAMEVERSION, "User save did not migrate to the current format");
        researchRequire(loaded->getGameInitSettings().getGameOptions().researchPace == setup.getGameOptions().researchPace,
                        "Resave/reload lost the original Research Pace");
        for(const auto& state : snapshots) {
            const House* house = loaded->getHouse(static_cast<int>(state[0]));
            researchRequire(house && house->getTechLevel() == static_cast<int>(state[1])
                && house->getTechResearchTarget() == static_cast<int>(state[2])
                && house->getTechResearchRemaining() == static_cast<int>(state[3])
                && house->getTechResearchLabID() == state[4], "Resave/reload changed paid research state");
            researchRequire(house->getTechResearchCost() == TechResearch::cost(house->getTechLevel() + 1,
                setup.getGameOptions().researchPace), "Migrated house did not use the new research price");
            note("old_save_resaved_reloaded", *loaded, house);
        }
        SDL_Log("RESEARCH_SAVED_RELOAD: cycle=%u version=%u houses=%zu",
            loaded->getGameCycleCount(), loaded->getLoadedSavegameVersion(), snapshots.size());
        researchEnd(loaded);
        researchRequire(RandomSkirmish::activateMod("dunecity"), "DuneCity reactivation after user save failed");
    }

    auto game = researchGame(true);
    const auto normalRulesHash = game->getGameInitSettings().getGameOptions().getHash();
    House* owner = game->getHouse(HOUSE_ATREIDES);
    House* opponent = nullptr;
    for(int id = 0; id < NUM_HOUSES; ++id)
        if(game->getHouse(id) && id != HOUSE_ATREIDES && !game->getHouse(id)->isNatives()) opponent = game->getHouse(id);
    researchRequire(owner && opponent, "Two playable houses are required");
    auto* yard = researchYard(owner);
    researchRequire(yard->isAvailableToBuild(Structure_ResearchLab), "Tech-1 yard cannot rebuild a Research Lab");
    const int laterItem = Structure_Radar;
    researchRequire(game->objectData.data[laterItem][owner->getFactionID()].techLevel == 2,
                    "Build-list fixture expects the shipped tech-2 Radar");
    researchRequire(!yard->isAvailableToBuild(laterItem), "Tech-2 Radar leaked into the tech-1 build list");
    auto* lab = researchPlaceLab(owner);
    auto* secondLab = researchPlaceLab(owner);
    const Uint32 labID = lab->getObjectID();
    const Uint32 spareID = secondLab->getObjectID();
    researchRequire(owner->getTechResearchCost() == 1000 && owner->techResearchCycles(2) == 7500,
                    "Normal level-2 quote is not 1000 credits and two game minutes");
    const int credits = owner->getCredits();
    const int nextCost = owner->getTechResearchCost();
    const auto held = owner->payCredits(credits);
    researchRequire(!owner->startTechResearch(labID), "Unaffordable research was accepted");
    const FixPoint nearlyEnough = FixPoint(nextCost) - 0.25_fix;
    owner->addUnstoredCredits(nearlyEnough);
    researchRequire(!owner->startTechResearch(labID), "Rounded displayed credits funded unaffordable research");
    owner->takeCredits(nearlyEnough);
    owner->returnCredits(held.total, held.fromStarting);
    const auto enemyPlayer = opponent->getPlayerList().front()->getPlayerID();
    Command(enemyPlayer, CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(!owner->isTechResearchActive() && owner->getCredits() == credits,
                    "An enemy command researched/spent the lab owner's credits");
    const auto ownPlayer = owner->getPlayerList().front()->getPlayerID();
    Command(ownPlayer, CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(owner->isTechResearchActive() && owner->getTechResearchTarget() == 2
                    && owner->getCredits() == credits - nextCost
                    && owner->getTechResearchRemaining() == owner->techResearchCycles(2),
                    "Owner command did not start and pay exactly the next research");
    researchRequire(!owner->startTechResearch(spareID), "Second lab started concurrent house research");
    // A paid legacy timer may be longer than the new quote, even after a new-format resave.
    constexpr int beforeSaveCycles = 123;
    owner->techResearchRemaining += 2 * beforeSaveCycles;
    for(int cycle = 0; cycle < beforeSaveCycles; ++cycle) game->updateGameState();
    note("active_before_save", *game, owner);
    const int remaining = owner->getTechResearchRemaining();
    researchRequire(remaining > owner->techResearchCycles(2), "Carryover-timer fixture did not exceed the new quote");
    const std::string save = out + "/research-active.dls";
    researchRequire(game->saveGame(save), "Active research save failed");
    const auto observer = game->saveObserverRuntime();
    std::vector<std::string> expected;
    for(int cycle = 0; cycle < remaining; ++cycle) {
        game->updateGameState();
        expected.push_back(GameStateDigest::describe(game->computeStateDigest()));
    }
    researchRequire(owner->getTechLevel() == 2 && opponent->getTechLevel() == 1,
                    "Completion did not raise only the researching house");
    researchRequire(yard->isAvailableToBuild(laterItem), "Completion did not refresh the yard build list");
    researchEnd(game);
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(save));
    game->initializeGameLoop();
    game->loadObserverRuntime(observer);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchRequire(owner->getTechLevel() == 1 && owner->getTechResearchTarget() == 2
                    && owner->getTechResearchRemaining() == remaining && owner->getTechResearchLabID() == labID,
                    "Save/load lost the active house research state");
    for(int cycle = 0; cycle < remaining; ++cycle) {
        game->updateGameState();
        researchRequire(GameStateDigest::describe(game->computeStateDigest()) == expected[cycle],
                        "Active research save/observer continuation diverged");
    }
    note("completed_after_reload", *game, owner);
    researchRequire(owner->startTechResearch(labID), "Next research did not start");
    const int paid = owner->getCredits();
    auto* doomed = dynamic_cast<ResearchLab*>(game->getObjectManager().getObject(labID));
    researchRequire(doomed != nullptr, "Reloaded lab missing");
    doomed->demolish();
    researchRequire(!owner->isTechResearchActive() && owner->getCredits() == paid,
                    "Destroyed active lab did not cancel without a refund");
    researchRequire(owner->getTechLevel() == 2, "Losing a lab lost completed technology");
    researchRequire(researchYard(owner)->isAvailableToBuild(Structure_ResearchLab), "Destroyed lab is not rebuildable");
    researchRequire(owner->startTechResearch(spareID), "Surviving lab could not restart the cancelled level");
    for(int target = 3; target <= 9; ++target) {
        if(target != 3) researchRequire(owner->startTechResearch(spareID), "Research chain stopped before tech 9");
        const int duration = owner->techResearchCycles(target);
        for(int cycle = 0; cycle < duration; ++cycle) game->updateGameState();
        researchRequire(owner->getTechLevel() == target && !owner->isTechResearchActive(),
                        "Research did not finish at its deterministic duration");
        note("level_complete", *game, owner);
    }
    researchRequire(!owner->startTechResearch(spareID) && owner->getTechResearchCost() == 0,
                    "Research exceeded the level-9 cap");
    researchEnd(game);

    game = researchGame(false);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchRequire(!researchYard(owner)->isAvailableToBuild(Structure_ResearchLab), "Off rule still offers a lab");
    auto* inactive = researchPlaceLab(owner);
    const auto seed = game->randomGen.getSeed();
    const int oldCredits = owner->getCredits();
    researchRequire(!owner->startTechResearch(inactive->getObjectID())
                    && seed == game->randomGen.getSeed() && oldCredits == owner->getCredits(),
                    "Rejected off-state research changed credits or RNG");
    for(int cycle = 0; cycle < 200; ++cycle) {
        game->updateGameState();
        researchRequire(owner->getTechLevel() == game->techLevel
                        && game->computeStateDigest().houseHash == researchLegacyHouseDigest(*game),
                        "Research-off technology/digest changed from the original match path");
    }
    note("off_legacy_digest", *game, owner);
    researchEnd(game);

    game = researchGame(true, false, 160);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchRequire(owner->getTechResearchCost() == 1000 && owner->techResearchCycles(2) == 7500
        && game->getGameInitSettings().getGameOptions().getHash() == normalRulesHash,
        "Map dimensions changed the research quote or rules hash");
    note("map_independent_quote", *game, owner);
    researchEnd(game);

    game = researchGame(true, false, 160, TechResearch::Pace::Slow);
    owner = game->getHouse(HOUSE_ATREIDES);
    lab = researchPlaceLab(owner);
    researchRequire(owner->getTechResearchCost() == 2000 && owner->techResearchCycles(2) == 15000,
                    "Real House ignored Slow research pace");
    researchRequire(owner->startTechResearch(lab->getObjectID()), "Slow research did not start");
    const int slowDuration = owner->techResearchCycles(2);
    for(int cycle = 0; cycle < slowDuration - 1; ++cycle) game->updateGameState();
    researchRequire(owner->getTechLevel() == 1 && owner->getTechResearchRemaining() == 1,
                    "Slow research completed before its exact cycle");
    game->updateGameState();
    researchRequire(owner->getTechLevel() == 2, "Slow research missed its exact completion cycle");
    note("pace_slow_complete", *game, owner);
    researchEnd(game);

    game = researchGame(true, true);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchRequire(owner->getNumItems(Structure_ResearchLab) == 0, "AI was granted a free lab");
    const int limit = MILLI2CYCLES(15 * 60 * 1000);
    int seenLab = -1;
    for(int cycle = 0; cycle < limit && owner->getTechLevel() < 2; ++cycle) {
        game->updateGameState();
        if(seenLab < 0 && owner->getNumItems(Structure_ResearchLab) > 0) {
            seenLab = game->getGameCycleCount();
            note("quantbot_built_lab", *game, owner);
        }
    }
    note("quantbot_final", *game, owner);
    researchRequire(seenLab >= 0 && owner->getTechLevel() >= 2, "Tech-1 QuantBot failed to build a lab and research");
    researchEnd(game);
    SDL_Log("RESEARCH_LAB_PROBE_PASS: owner command, active-save continuation, cancellation/rebuild, level9, off digest, QuantBot");
    return MENU_QUIT_DEFAULT;
}
