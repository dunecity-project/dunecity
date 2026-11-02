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
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
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

std::unique_ptr<Game> researchGame(bool enabled, bool ai = false) {
    using namespace SkirmishGenerator;
    Params params;
    params.house = HOUSE_ATREIDES;
    params.mapSize = 64;
    params.techLevel = 1;
    params.researchLab = enabled;
    params.mod = "dunecity";
    params.mode = Mode::DuneCity;
    params.seed = 20261102u;
    params.credits = 20000;
    params.allies = 0;
    params.enemies = 1;
    params.rock = Rock::High;
    params.spice = Spice::High;
    params.playerSide = Side{"", BaseSize::Small, 0, 0};
    params.enemySide = Side{"HumanPlayer", BaseSize::Outpost, 0, 0};
    const auto map = generate(params, RandomSkirmish::loadCatalogue(params.mode));
    for(const auto& entry : map.structures)
        researchRequire(entry.item != Structure_ResearchLab, "Random Skirmish granted a free lab");
    auto options = effectiveGameOptions;
    options.researchLab = enabled;
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

    auto game = researchGame(true);
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
    const int credits = owner->getCredits();
    const auto held = owner->payCredits(credits);
    researchRequire(!owner->startTechResearch(labID), "Unaffordable research was accepted");
    owner->addUnstoredCredits(599.75_fix);
    researchRequire(!owner->startTechResearch(labID), "Rounded displayed credits funded unaffordable research");
    owner->takeCredits(599.75_fix);
    owner->returnCredits(held.total, held.fromStarting);
    const auto enemyPlayer = opponent->getPlayerList().front()->getPlayerID();
    Command(enemyPlayer, CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(!owner->isTechResearchActive() && owner->getCredits() == credits,
                    "An enemy command researched/spent the lab owner's credits");
    const auto ownPlayer = owner->getPlayerList().front()->getPlayerID();
    Command(ownPlayer, CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(owner->isTechResearchActive() && owner->getTechResearchTarget() == 2
                    && owner->getCredits() == credits - 600
                    && owner->getTechResearchRemaining() == House::techResearchCycles(2),
                    "Owner command did not start and pay exactly the next research");
    researchRequire(!owner->startTechResearch(spareID), "Second lab started concurrent house research");
    for(int cycle = 0; cycle < 123; ++cycle) game->updateGameState();
    note("active_before_save", *game, owner);
    const int remaining = owner->getTechResearchRemaining();
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
        const int duration = House::techResearchCycles(target);
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
