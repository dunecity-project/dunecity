// Test-only entry point linked with the production objects by run-research-lab-probe.py.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Command.h>
#include <Menu/RandomSkirmishMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <players/QuantBotConfig.h>
#include <structures/BuilderBase.h>
#include <structures/ResearchLab.h>
#include <Network/GameStateDigest.h>
#include <Network/WorkshopGameContent.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <utility>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>
#include "research-lab-ui-probe.inc"

namespace {
void researchRequire(bool value, const char* why) {
    if(!value) {
        SDL_Log("RESEARCH_LAB_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

std::unique_ptr<Game> researchGame(bool enabled, bool ai = false, int mapSize = 64,
                                 TechResearch::Pace pace = TechResearch::Pace::Normal,
                                 std::optional<bool> installments = std::nullopt, int techLevel = 1) {
    using namespace SkirmishGenerator;
    Params params;
    params.house = HOUSE_ATREIDES;
    params.mapSize = mapSize;
    params.techLevel = techLevel;
    params.researchLab = enabled;
    params.mod = "dunecity";
    params.mode = Mode::DuneCity;
    params.seed = 20261102u;
    params.credits = 20000;
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
    if(installments) options.technologyInstallments = *installments;
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
    if(!ai) {
        // Offline loading adopts the last human seat, which here is the enemy.
        // Seat the authored tester before creating the sidebar or issuing clicks.
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* controller = dynamic_cast<HumanPlayer*>(owner->getPlayerList().front().get());
        researchRequire(controller && game->getPlayerByID(controller->getPlayerID()) == controller,
                        "Research fixture has no registered Atreides human controller");
        pLocalHouse = owner;
        pLocalPlayer = controller;
        researchRequire(game->canControlHouse(owner), "Research fixture did not seat its human controller");
    }
    game->initializeGameLoop();
    // Generator presets cap opening cash at 20k; fund the complete L2-L9 timing fixture.
    if(!ai) game->getHouse(HOUSE_ATREIDES)->addUnstoredCredits(80000);
    if(ai && enabled) {
        bool configured = false;
        for(const auto& player : game->getHouse(HOUSE_ATREIDES)->getPlayerList()) {
            if(auto* bot = dynamic_cast<QuantBot*>(player.get())) {
                auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
                config->force.continuousResearch = true;
                bot->configOverride = config;
                configured = true;
            }
        }
        researchRequire(configured, "Natural research fixture did not configure its real QuantBot");
        SDL_Log("RESEARCH_QUANTBOT_GENE: continuous_research=1");
    }
    researchRequire(game->techLevel == techLevel, "Fixture did not retain its authored match technology");
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

FixPoint researchBalance(const House* owner) {
    return owner->storedCredits + owner->startingCredits + owner->cityCredits;
}

void researchEmptyCredits(House* owner) {
    // The ordinary debit API intentionally ignores rounded-zero balances. Top up
    // only for fixture cash removal; research must itself debit sub-credit cash.
    if(researchBalance(owner) > 0 && owner->getCredits() < 1) owner->addUnstoredCredits(1);
    const FixPoint balance = researchBalance(owner);
    researchRequire(owner->takeCredits(balance) == balance && researchBalance(owner) == 0,
                    "Fixture could not remove spendable cash through the public debit API");
}

void researchNote(std::ostream& results, const char* name, const Game& game, const House* owner) {
    results << name << ',' << game.getGameCycleCount() << ',' << owner->getTechLevel() << ','
            << owner->getTechResearchTarget() << ',' << owner->getTechResearchRemaining() << ','
            << owner->getTechResearchLabID() << ',' << owner->getCredits() << ','
            << static_cast<unsigned>(game.getGameInitSettings().getGameOptions().researchPace) << ','
            << owner->isTechResearchInstallment() << ',' << owner->getTechResearchTotalCost() << ','
            << owner->getTechResearchTotalCycles() << ',' << owner->getTechResearchPaid().getRawValue() << ','
            << owner->isTechResearchPaused() << ',' << researchBalance(owner).getRawValue() << '\n';
}

std::unique_ptr<Game> researchLoad(const std::string& filename) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(filename));
    game->bReplay = false;
    game->initializeGameLoop();
    return game;
}

Sint64 researchDue(Sint64 totalRaw, int completed, int duration) {
    return (totalRaw / duration) * completed + ((totalRaw % duration) * completed) / duration;
}

struct ResearchSnapshot {
    int level;
    int target;
    int remaining;
    Uint32 lab;
    bool installment;
    int totalCost;
    int totalCycles;
    FixPoint paid;
    bool paused;
    FixPoint balance;
};

ResearchSnapshot researchSnapshot(const House* owner) {
    return {owner->getTechLevel(), owner->getTechResearchTarget(), owner->getTechResearchRemaining(),
            owner->getTechResearchLabID(), owner->isTechResearchInstallment(), owner->getTechResearchTotalCost(),
            owner->getTechResearchTotalCycles(), owner->getTechResearchPaid(), owner->isTechResearchPaused(),
            researchBalance(owner)};
}

void researchSameState(const House* owner, const ResearchSnapshot& state) {
    researchRequire(owner && owner->getTechLevel() == state.level && owner->getTechResearchTarget() == state.target
        && owner->getTechResearchRemaining() == state.remaining && owner->getTechResearchLabID() == state.lab
        && owner->isTechResearchInstallment() == state.installment && owner->getTechResearchTotalCost() == state.totalCost
        && owner->getTechResearchTotalCycles() == state.totalCycles && owner->getTechResearchPaid() == state.paid
        && owner->isTechResearchPaused() == state.paused && researchBalance(owner) == state.balance,
        "Save/load changed captured research terms, partial payment, paused state or exact credits");
}

void researchInstallmentTotals(std::ostream& results) {
    using TechResearch::Pace;
    for(const auto pace : {Pace::Fast, Pace::Normal, Pace::Slow, Pace::VerySlow}) {
        auto game = researchGame(true, false, 64, pace);
        House* owner = game->getHouse(HOUSE_ATREIDES);
        auto* lab = researchPlaceLab(owner);
        const auto player = owner->getPlayerList().front()->getPlayerID();
        const auto originalRules = game->getGameInitSettings().getGameOptions();
        researchRequire(originalRules.technologyInstallments, "New-game source default did not enable installments");
        for(int target = 2; target <= 9; ++target) {
            researchEmptyCredits(owner);
            const int cost = owner->getTechResearchCost();
            const int duration = owner->techResearchCycles(target);
            const Sint64 totalRaw = FixPoint(cost).getRawValue();
            owner->addUnstoredCredits(FixPoint::FromRawValue(totalRaw + 1));
            const auto seed = game->randomGen.getSeed();
            Command(player, CMD_RESEARCH_TECH_LEVEL, lab->getObjectID()).executeCommand();
            researchRequire(owner->isTechResearchActive() && owner->isTechResearchInstallment()
                && owner->getTechResearchTarget() == target && owner->getTechResearchRemaining() == duration
                && owner->getTechResearchTotalCost() == cost && owner->getTechResearchTotalCycles() == duration
                && owner->getTechResearchPaid() == 0 && researchBalance(owner).getRawValue() == totalRaw + 1,
                "Installment start charged cash or failed to capture the current quote");

            // Independent running quotient/carry reference, not the engine's cumulative helper.
            const Sint64 base = totalRaw / duration;
            const Sint64 remainder = totalRaw % duration;
            researchRequire(remainder != 0, "Exact-payment fixture did not exercise integer remainders");
            Sint64 due = 0, carry = 0;
            for(int completed = 1; completed <= duration; ++completed) {
                if(target == 4 && completed == duration / 2) {
                    auto changed = originalRules;
                    changed.technologyInstallments = false;
                    changed.researchPace = pace == Pace::VerySlow ? Pace::Fast : Pace::VerySlow;
                    game->gameInitSettings.setGameOptions(changed);
                }
                due += base;
                carry += remainder;
                if(carry >= duration) {
                    due += carry / duration;
                    carry %= duration;
                }
                owner->updateTechResearch();
                researchRequire(researchBalance(owner).getRawValue() == totalRaw + 1 - due,
                                "A research tick did not debit its exact cumulative integer installment");
                if(completed < duration) {
                    researchRequire(owner->getTechLevel() == target - 1
                        && owner->getTechResearchRemaining() == duration - completed
                        && owner->getTechResearchPaid().getRawValue() == due
                        && owner->isTechResearchInstallment() && !owner->isTechResearchPaused()
                        && owner->getTechResearchTotalCost() == cost && owner->getTechResearchTotalCycles() == duration,
                        "Research pace/rule changes altered a captured installment timer or payment");
                }
            }
            researchRequire(due == totalRaw && carry == 0 && researchBalance(owner).getRawValue() == 1
                && owner->getTechLevel() == target && !owner->isTechResearchActive()
                && owner->getTechResearchRemaining() == 0 && game->randomGen.getSeed() == seed,
                "Research did not finish on its exact funded tick, debit the exact total, or preserve RNG");
            game->gameInitSettings.setGameOptions(originalRules);
            researchNote(results, "installment_exact_total", *game, owner);
            SDL_Log("RESEARCH_INSTALLMENT_TOTAL: pace=%u target=%d cost=%d cycles=%d total_raw=%lld remainder=%lld",
                    static_cast<unsigned>(pace), target, cost, duration,
                    static_cast<long long>(totalRaw), static_cast<long long>(remainder));
        }
        researchRequire(!owner->startTechResearch(lab->getObjectID()) && owner->getTechResearchCost() == 0,
                        "Installments bypassed the technology cap");
        researchEnd(game);
    }
}

void researchPrepaidRuleChange(std::ostream& results) {
    auto game = researchGame(true, false, 64, TechResearch::Pace::Normal, false);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* lab = researchPlaceLab(owner);
    researchEmptyCredits(owner);
    const int cost = owner->getTechResearchCost(), duration = owner->techResearchCycles(2);
    owner->addUnstoredCredits(cost);
    Command(owner->getPlayerList().front()->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, lab->getObjectID()).executeCommand();
    researchRequire(owner->isTechResearchActive() && !owner->isTechResearchInstallment()
        && owner->getTechResearchPaid() == cost && researchBalance(owner) == 0,
        "Explicit OFF did not debit the entire research price once");
    auto changed = game->getGameInitSettings().getGameOptions();
    changed.technologyInstallments = true;
    changed.researchPace = TechResearch::Pace::VerySlow;
    game->gameInitSettings.setGameOptions(changed);
    for(int completed = 1; completed <= duration; ++completed) {
        owner->updateTechResearch();
        researchRequire(researchBalance(owner) == 0 && owner->getTechResearchRemaining() == duration - completed,
                        "Turning installments on recharged or paused an already-paid timer");
        if(completed < duration)
            researchRequire(!owner->isTechResearchInstallment() && !owner->isTechResearchPaused()
                && owner->getTechResearchTotalCost() == cost && owner->getTechResearchTotalCycles() == duration
                && owner->getTechResearchPaid() == cost, "Prepaid captured terms changed with the live rules");
    }
    researchRequire(owner->getTechLevel() == 2, "Already-paid timer missed its captured completion tick");
    researchNote(results, "upfront_rule_change_complete", *game, owner);
    researchEnd(game);
}

std::vector<std::string> researchResume(Game& game, Sint64 partialPaid,
                                      const std::vector<std::string>* expected = nullptr) {
    auto* owner = game.getHouse(HOUSE_ATREIDES);
    const int duration = owner->getTechResearchTotalCycles();
    const int pendingCycles = owner->getTechResearchRemaining();
    const int initialCompleted = duration - pendingCycles;
    const Sint64 totalRaw = FixPoint(owner->getTechResearchTotalCost()).getRawValue();
    std::vector<std::string> digests;
    if(!expected) digests.reserve(pendingCycles + 31);
    Sint64 authoredPaid = partialPaid;
    int completed = initialCompleted;
    // Isolate the authored cash schedule from normal city/refinery income. The
    // house still pays through its real updater before the rest of each game tick.
    for(int step = 0; step < pendingCycles + 31; ++step) {
        researchEmptyCredits(owner);
        if(step == 24 || step >= 32) {
            const Sint64 due = researchDue(totalRaw, completed + 1, duration);
            const Sint64 installment = due - owner->getTechResearchPaid().getRawValue();
            researchRequire(installment > 0, "Resume schedule created a free or catch-up research tick");
            owner->addUnstoredCredits(FixPoint::FromRawValue(installment));
            authoredPaid += installment;
            ++completed;
        }
        game.updateGameState();
        researchRequire(owner->getTechResearchRemaining() == duration - completed,
                        "Cash exhaustion cancelled/advanced research or resumption accumulated catch-up debt");
        if(completed < duration)
            researchRequire(owner->isTechResearchActive() && owner->isTechResearchInstallment()
                && owner->getTechResearchPaid().getRawValue()
                    == (completed == initialCompleted ? partialPaid : researchDue(totalRaw, completed, duration)),
                "Paused/resumed research lost a partial installment or changed the exact cumulative amount");
        if(expected)
            researchRequire(step < static_cast<int>(expected->size())
                && GameStateDigest::describe(game.computeStateDigest()) == (*expected)[step],
                "Partial-installment save/observer continuation diverged");
        else
            digests.push_back(GameStateDigest::describe(game.computeStateDigest()));
    }
    researchRequire(completed == duration && authoredPaid == totalRaw
        && owner->getTechLevel() == 2 && !owner->isTechResearchActive(),
        "Resume did not complete after exactly the funded cycles and exact original total");
    researchRequire(!expected || expected->size() == static_cast<std::size_t>(pendingCycles + 31),
                    "Save continuation omitted digest samples");
    return digests;
}

void researchInstallmentContinuation(const std::string& out, std::ostream& results) {
    auto game = researchGame(true);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    House* opponent = nullptr;
    for(int id = 0; id < NUM_HOUSES; ++id)
        if(game->getHouse(id) && id != HOUSE_ATREIDES && !game->getHouse(id)->isNatives()) opponent = game->getHouse(id);
    researchRequire(opponent != nullptr, "Installment authentication fixture has no opponent");
    auto* lab = researchPlaceLab(owner);
    auto* spare = researchPlaceLab(owner);
    const Uint32 labID = lab->getObjectID(), spareID = spare->getObjectID();
    researchEmptyCredits(owner);
    const auto seed = game->randomGen.getSeed();
    Command(opponent->getPlayerList().front()->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(!owner->isTechResearchActive() && researchBalance(owner) == 0
        && game->randomGen.getSeed() == seed, "Enemy command started zero-cash installment research");
    researchRequire(!owner->startTechResearch(NONE_ID)
        && !owner->startTechResearch(researchYard(owner)->getObjectID()),
        "Installments accepted a missing object or a non-lab building");
    researchUIOptionsProbe(*game);
    researchUIProbe(*game, lab, "installment_available");
    researchRequire(owner->canStartTechResearch(labID), "A real human cannot start research with zero cash");
    researchUIStart(*game, lab);
    game->updateGameState();
    researchEmptyCredits(owner);
    const int cost = owner->getTechResearchCost(), duration = owner->techResearchCycles(2);
    researchRequire(owner->isTechResearchActive() && owner->isTechResearchInstallment()
        && owner->getTechResearchTarget() == 2 && owner->getTechResearchRemaining() == duration
        && owner->getTechResearchPaid() == 0 && owner->isTechResearchPaused()
        && researchBalance(owner) == 0, "Zero-cash human UI command did not start and pause without charging");
    researchUIProbe(*game, lab, "installment_paused");
    Command(owner->getPlayerList().front()->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, spareID).executeCommand();
    researchRequire(!owner->startTechResearch(spareID) && owner->getTechResearchLabID() == labID
        && owner->getTechResearchRemaining() == duration && owner->getTechResearchPaid() == 0,
        "A second lab duplicated/restarted the house research");
    const Sint64 totalRaw = FixPoint(cost).getRawValue();
    const int fundedCycles = duration / 3;
    const Sint64 funded = researchDue(totalRaw, fundedCycles, duration);
    owner->addUnstoredCredits(FixPoint::FromRawValue(funded));
    for(int tick = 0; tick < fundedCycles; ++tick) owner->updateTechResearch();
    researchRequire(owner->getTechResearchRemaining() == duration - fundedCycles
        && owner->getTechResearchPaid().getRawValue() == funded && researchBalance(owner) == 0,
        "Research did not exhaust an exact funded third before the mid-study partial installment");
    const Sint64 nextDue = researchDue(totalRaw, fundedCycles + 1, duration);
    const Sint64 partialInstallment = (nextDue - funded) / 2;
    researchRequire(partialInstallment > 1, "Partial-payment fixture has no representable installment");
    owner->addUnstoredCredits(FixPoint::FromRawValue(partialInstallment));
    game->updateGameState();
    researchEmptyCredits(owner);
    researchRequire(owner->getTechResearchPaid().getRawValue() == funded + partialInstallment
        && owner->getTechResearchRemaining() == duration - fundedCycles && owner->isTechResearchPaused()
        && researchBalance(owner) == 0, "Research failed to keep an affordable sub-credit partial installment");
    const auto digestBefore = game->computeStateDigest().houseHash;
    owner->addUnstoredCredits(FixPoint::FromRawValue(1));
    owner->updateTechResearch();
    const Sint64 paid = funded + partialInstallment + 1;
    researchRequire(owner->getCredits() == 0 && owner->getTechResearchPaid().getRawValue() == paid
        && owner->getTechResearchRemaining() == duration - fundedCycles && game->computeStateDigest().houseHash != digestBefore,
        "Exact partial payments disappeared from the digest when displayed credits and timer were unchanged");
    researchUIProbe(*game, lab, "installment_partial");
    owner->addUnstoredCredits(FixPoint::FromRawValue(nextDue - paid));
    researchRequire(!owner->isTechResearchPaused(), "Funding the next outstanding installment did not unpause research");
    researchUIProbe(*game, lab, "installment_researching");
    researchEmptyCredits(owner);
    const auto state = researchSnapshot(owner);
    const auto digest = GameStateDigest::describe(game->computeStateDigest());
    const std::string save = out + "/research-installment-partial.dls";
    researchRequire(game->saveGame(save), "Partial-installment save failed");
    const auto observer = game->saveObserverRuntime();
    researchNote(results, "installment_partial_before_save", *game, owner);
    const auto expected = researchResume(*game, paid);
    researchUIProbe(*game, lab, "installment_complete");
    researchEnd(game);
    game = researchLoad(save);
    game->loadObserverRuntime(observer);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchSameState(owner, state);
    researchRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION
        && game->getGameInitSettings().getGameOptions().technologyInstallments
        && GameStateDigest::describe(game->computeStateDigest()) == digest,
        "Partial save did not preserve the payment rule and exact state digest");
    researchResume(*game, paid, &expected);
    researchNote(results, "installment_exact_resume_complete", *game, owner);

    lab = dynamic_cast<ResearchLab*>(game->getObjectManager().getObject(labID));
    spare = dynamic_cast<ResearchLab*>(game->getObjectManager().getObject(spareID));
    opponent = nullptr;
    for(int id = 0; id < NUM_HOUSES; ++id)
        if(game->getHouse(id) && id != HOUSE_ATREIDES && !game->getHouse(id)->isNatives()) opponent = game->getHouse(id);
    researchRequire(lab && spare && opponent, "Reloaded cancellation fixture lost its labs or opponent");
    researchEmptyCredits(owner);
    Command(owner->getPlayerList().front()->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    owner->addUnstoredCredits(FixPoint::FromRawValue(1));
    owner->updateTechResearch();
    owner->cancelTechResearch(spareID);
    spare->demolish();
    researchRequire(owner->isTechResearchActive() && owner->getTechResearchLabID() == labID
        && owner->getTechResearchPaid().getRawValue() == 1,
        "Losing or cancelling a different lab cancelled the active house study");
    const auto balance = researchBalance(owner);
    lab->setOwner(opponent);
    owner->updateTechResearch();
    researchRequire(!owner->isTechResearchActive() && owner->getTechLevel() == 2 && researchBalance(owner) == balance,
                    "Loss of the active lab's ownership did not cancel without refunding partial payments");
    lab->setOwner(owner);
    Command(opponent->getPlayerList().front()->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, labID).executeCommand();
    researchRequire(!owner->isTechResearchActive(), "Enemy command bypassed ownership after a cancelled study");
    researchRequire(owner->startTechResearch(labID), "Owner could not restart after lab ownership was restored");
    owner->addUnstoredCredits(FixPoint::FromRawValue(1));
    owner->updateTechResearch();
    lab->demolish();
    researchRequire(!owner->isTechResearchActive() && owner->getTechLevel() == 2
        && researchBalance(owner) == balance, "Destroying the active lab refunded or retained partially paid research");
    researchNote(results, "installment_cancel_no_refund", *game, owner);
    researchEnd(game);
}

void researchWriteLegacyLayout(Game& game, Uint32 version, const std::string& filename) {
    // These are explicitly synthetic old-layout saves, not archived player saves.
    // Keep the complete real save, changing only the frozen setup/House suffixes.
    OMemoryStream saved; saved.open(); game.saveGame(saved);
    std::string bytes(saved.getData(), saved.getDataLength());
    for(int id = 0; id < NUM_HOUSES; ++id) if(const auto* owner = game.getHouse(id)) {
        OMemoryStream house; house.open(); owner->save(house);
        const std::string encoded(house.getData(), house.getDataLength());
        const auto offset = bytes.find(encoded);
        researchRequire(encoded.size() > 17 && offset != std::string::npos
            && bytes.find(encoded, offset + 1) == std::string::npos,
            "Synthetic legacy fixture could not identify exactly one serialized House");
        bytes.erase(offset + encoded.size() - 17, 17);
    }
    OMemoryStream setup; setup.open(); game.getGameInitSettings().save(setup);
    const std::string encoded(setup.getData(), setup.getDataLength());
    auto legacy = encoded;
    const auto marker = legacy.rfind("JDOM");
    researchRequire(marker != std::string::npos, "Synthetic legacy fixture has no current setup marker");
    if(version == 9882) {
        legacy[marker] = 'H';
        legacy.resize(legacy.size() - 3);
        constexpr Uint32 frozenArea = 12384;
        for(int shift = 0; shift < 32; shift += 8) legacy.push_back(static_cast<char>(frozenArea >> shift));
        legacy.push_back(static_cast<char>(game.getGameInitSettings().getGameOptions().damageLowersLandValue));
    } else {
        researchRequire(version == 9883, "Unsupported synthetic legacy format");
        legacy[marker] = 'I';
        legacy.pop_back();
    }
    const auto offset = bytes.find(encoded);
    researchRequire(offset != std::string::npos && bytes.find(encoded, offset + 1) == std::string::npos,
                    "Synthetic legacy fixture could not identify exactly one saved setup");
    bytes.replace(offset, encoded.size(), legacy);
    OMemoryStream header; header.open(); header.writeUint32(SAVEMAGIC); header.writeUint32(version);
    bytes.replace(0, header.getDataLength(), header.getData(), header.getDataLength());
    std::ofstream output(filename, std::ios::binary);
    output.write(bytes.data(), bytes.size());
    researchRequire(output.good(), "Synthetic legacy fixture could not write its full save");
}

void researchLegacyLayouts(const std::string& out, std::ostream& results) {
    for(const Uint32 version : {Uint32(9882), Uint32(9883)}) {
        auto game = researchGame(true, false, 64, TechResearch::Pace::Normal, false);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* lab = researchPlaceLab(owner);
        researchEmptyCredits(owner);
        const int cost = owner->getTechResearchCost();
        owner->addUnstoredCredits(cost);
        researchRequire(owner->startTechResearch(lab->getObjectID()), "Synthetic prepaid legacy research did not start");
        owner->techResearchRemaining += 123;
        owner->techResearchTotalCycles += 123;
        const int remaining = owner->getTechResearchRemaining();
        const std::string prefix = out + "/research-synthetic-" + std::to_string(version);
        researchWriteLegacyLayout(*game, version, prefix + ".dls");
        researchEnd(game);
        game = researchLoad(prefix + ".dls");
        owner = game->getHouse(HOUSE_ATREIDES);
        researchRequire(game->getLoadedSavegameVersion() == version
            && !game->getGameInitSettings().getGameOptions().technologyInstallments
            && owner->getTechResearchRemaining() == remaining && !owner->isTechResearchInstallment()
            && owner->getTechResearchTotalCycles() == remaining && owner->getTechResearchTotalCost() == cost
            && owner->getTechResearchPaid() == cost && !owner->isTechResearchPaused() && researchBalance(owner) == 0,
            "Synthetic 9882/9883 full save changed old rules or an already-paid longer timer");
        const auto state = researchSnapshot(owner);
        researchRequire(game->saveGame(prefix + "-resaved.dls"), "Legacy timer could not resave as the current format");
        researchEnd(game);
        game = researchLoad(prefix + "-resaved.dls");
        owner = game->getHouse(HOUSE_ATREIDES);
        researchRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION
            && !game->getGameInitSettings().getGameOptions().technologyInstallments,
            "A current-format resave changed an old match from upfront to installments");
        researchSameState(owner, state);
        auto changed = game->getGameInitSettings().getGameOptions();
        changed.technologyInstallments = true;
        changed.researchPace = TechResearch::Pace::VerySlow;
        game->gameInitSettings.setGameOptions(changed);
        for(int completed = 1; completed <= remaining; ++completed) {
            owner->updateTechResearch();
            researchRequire(owner->getTechResearchRemaining() == remaining - completed
                && researchBalance(owner) == 0 && !owner->isTechResearchPaused(),
                "A migrated paid timer was recharged, paused or rescaled by current rules");
        }
        researchRequire(owner->getTechLevel() == 2 && !owner->isTechResearchActive(),
                        "Migrated prepaid research missed its preserved completion tick");
        researchNote(results, "synthetic_legacy_paid_complete", *game, owner);
        SDL_Log("RESEARCH_LEGACY_LAYOUT_PASS: version=%u resaved=%u synthetic=1 paid=%d preserved_cycles=%d",
                version, SAVEGAMEVERSION, cost, remaining);
        researchEnd(game);
    }
}

const char* upgradeKind(int item) {
    return item == Structure_ConstructionYard ? "cy" : "factory";
}

BuilderBase* upgradeBuilder(House* owner, int item) {
    BuilderBase* builder = nullptr;
    if(item == Structure_ConstructionYard) builder = researchYard(owner);
    else {
        researchRequire(item == Structure_HeavyFactory, "Unsupported upgrade fixture supplier");
        for(int y = 1; !builder && y + 2 < currentGameMap->getSizeY(); ++y)
        for(int x = 1; !builder && x + 3 < currentGameMap->getSizeX(); ++x) {
            if(currentGameMap->okayToPlaceStructure(x, y, 3, 2, false, owner, false, item))
                builder = dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID, item, x, y, true));
        }
    }
    researchRequire(builder && builder->isActive() && builder->isAllowedToUpgrade()
        && builder->getUpgradeCost() > 0 && !builder->isUpgrading(),
        "Authored tech-9 match did not provide a naturally eligible yard/factory upgrade");
    return builder;
}

Sint64 upgradeStep(Sint64 total) {
    constexpr int duration = 600;
    return total / duration + (total % duration != 0);
}

FixPoint upgradeTick(BuilderBase* builder) {
    const FixPoint before = researchBalance(builder->getOwner());
    const FixPoint progress = builder->getUpgradeProgress();
    const int level = builder->getCurrentUpgradeLevel(), cost = builder->getUpgradeCost();
    researchRequire(builder->isUpgrading() && builder->update(), "Real upgrade supplier stopped updating");
    const FixPoint charged = before - researchBalance(builder->getOwner());
    const FixPoint gain = builder->isUpgrading() ? builder->getUpgradeProgress() - progress : FixPoint(cost) - progress;
    researchRequire(charged == gain && charged >= 0
        && builder->getCurrentUpgradeLevel() == level + (builder->isUpgrading() ? 0 : 1),
        "Upgrade charged more than its real paid progress or changed level without completion");
    return charged;
}

bool upgradeAIStart(House* owner, BuilderBase* builder) {
    auto* bot = dynamic_cast<QuantBot*>(owner->getPlayerList().front().get());
    researchRequire(owner->isAI() && bot, "Upgrade fixture did not retain its real QuantBot controller");
    return bot->doUpgrade(builder); // The same ownership-checked Player API used by QuantBot's build pass.
}

void upgradeNote(std::ostream& results, const char* name, const char* controller,
                 const Game& game, const BuilderBase* builder, Sint64 paid, Uint32 version = 0) {
    results << name << ',' << controller << ',' << builder->getItemID() << ',' << game.getGameCycleCount() << ','
            << builder->getCurrentUpgradeLevel() << ',' << builder->isUpgrading() << ','
            << builder->getUpgradeProgress().getRawValue() << ',' << builder->getUpgradeProgressFromStartingCredits().getRawValue()
            << ',' << builder->isUpgradePaused() << ',' << builder->getUpgradeCost() << ',' << paid << ','
            << researchBalance(builder->getOwner()).getRawValue() << ',' << version << '\n';
}

void upgradeExactTotals(std::ostream& results) {
    for(const bool ai : {false, true})
    for(const int item : {Structure_ConstructionYard, Structure_HeavyFactory}) {
        auto game = researchGame(false, ai, 64, TechResearch::Pace::Normal, std::nullopt, 9);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* builder = upgradeBuilder(owner, item);
        const int level = builder->getCurrentUpgradeLevel(), cost = builder->getUpgradeCost();
        const Sint64 total = FixPoint(cost).getRawValue(), step = upgradeStep(total);
        researchRequire(game->getGameInitSettings().getGameOptions().technologyInstallments
            && !game->getGameInitSettings().getGameOptions().instantBuild,
            "Exact upgrade fixture did not retain the normal source-default technology rules");
        researchEmptyCredits(owner);
        researchRequire(builder->canStartUpgrade(), "ON upgrade still requires its full price at zero cash");
        owner->addUnstoredCredits(FixPoint::FromRawValue(total + 1));
        if(ai) researchRequire(upgradeAIStart(owner, builder), "Real QuantBot shared upgrade API rejected the order");
        else Command(owner->getPlayerList().front()->getPlayerID(), CMD_BUILDER_UPGRADE, builder->getObjectID()).executeCommand();
        researchRequire(builder->isUpgrading() && builder->getUpgradeProgress() == 0
            && researchBalance(owner).getRawValue() == total + 1,
            "Upgrade start charged cash before its ordinary per-tick payer");
        Sint64 paid = 0;
        for(int tick = 1; tick <= 600; ++tick) {
            const Sint64 expected = std::min(total, step * tick);
            paid += upgradeTick(builder).getRawValue();
            researchRequire(paid == expected && researchBalance(owner).getRawValue() == total + 1 - expected,
                            "Upgrade did not charge the exact capped integer installment");
            if(tick < 600)
                researchRequire(builder->isUpgrading() && !builder->isUpgradePaused()
                    && builder->getCurrentUpgradeLevel() == level
                    && builder->getUpgradeProgress().getRawValue() == expected
                    && builder->getUpgradeProgressFromStartingCredits().getRawValue() == expected,
                    "Fully funded upgrade finished early or lost exact paid/provenance progress");
        }
        researchRequire(paid == total && researchBalance(owner).getRawValue() == 1
            && !builder->isUpgrading() && builder->getCurrentUpgradeLevel() == level + 1
            && builder->getUpgradeProgress() == 0 && builder->getUpgradeProgressFromStartingCredits() == 0,
            "Fully funded upgrade missed exactly 600 cycles, overpaid, or retained completed progress");
        upgradeNote(results, "exact_600_ticks", ai ? "quantbot" : "human", *game, builder, paid);
        SDL_Log("UPGRADE_INSTALLMENT_TOTAL: controller=%s item=%d cycles=600 cost=%d paid_raw=%lld",
                ai ? "quantbot" : "human", item, cost, static_cast<long long>(paid));
        researchEnd(game);
    }
}

void upgradeAIPartials(std::ostream& results) {
    for(const int item : {Structure_ConstructionYard, Structure_HeavyFactory}) {
        auto game = researchGame(false, true, 64, TechResearch::Pace::Normal, std::nullopt, 9);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* builder = upgradeBuilder(owner, item);
        const int level = builder->getCurrentUpgradeLevel();
        const Sint64 total = FixPoint(builder->getUpgradeCost()).getRawValue();
        researchEmptyCredits(owner);
        owner->addUnstoredCredits(FixPoint::FromRawValue(1));
        researchRequire(owner->getCredits() == 0 && upgradeAIStart(owner, builder)
            && builder->getUpgradeProgress() == 0 && researchBalance(owner).getRawValue() == 1,
            "Actual QuantBot shared API required the full upgrade price or charged on start");
        researchRequire(upgradeTick(builder).getRawValue() == 1
            && builder->getUpgradeProgress().getRawValue() == 1 && builder->isUpgradePaused(),
            "QuantBot-owned upgrade did not spend a sub-credit raw unit exactly");
        researchRequire(!upgradeAIStart(owner, builder), "A repeated AI upgrade order reset the active study");
        for(int paused = 0; paused < 80; ++paused)
            researchRequire(upgradeTick(builder) == 0 && builder->isUpgrading() && builder->isUpgradePaused()
                && builder->getCurrentUpgradeLevel() == level && builder->getUpgradeProgress().getRawValue() == 1,
                "AI upgrade cancelled, changed level or lost paid progress while cash was empty");
        owner->addUnstoredCredits(FixPoint::FromRawValue(total - 1));
        Sint64 paid = 1;
        for(int tick = 0; builder->isUpgrading() && tick < 600; ++tick) paid += upgradeTick(builder).getRawValue();
        researchRequire(!builder->isUpgrading() && builder->getCurrentUpgradeLevel() == level + 1
            && paid == total && researchBalance(owner) == 0, "AI upgrade did not resume to the exact same total");
        upgradeNote(results, "ai_partial_resume_complete", "quantbot", *game, builder, paid);
        SDL_Log("UPGRADE_AI_PARTIAL_PASS: item=%d paid_raw=%lld paused_cycles=80", item, static_cast<long long>(paid));
        researchEnd(game);
    }
}

std::vector<std::string> upgradeResume(Game& game, Uint32 builderID, int originalLevel, Sint64 savedPaid,
                                     const std::vector<std::string>* expected = nullptr) {
    auto* builder = dynamic_cast<BuilderBase*>(game.getObjectManager().getObject(builderID));
    researchRequire(builder != nullptr, "Saved upgrade supplier is missing");
    auto* owner = builder->getOwner();
    const Sint64 total = FixPoint(builder->getUpgradeCost()).getRawValue(), step = upgradeStep(total);
    const int fundedTicks = static_cast<int>((total - savedPaid) / step + ((total - savedPaid) % step != 0));
    std::vector<std::string> digests;
    if(!expected) digests.reserve(fundedTicks + 17);
    Sint64 paid = savedPaid;
    for(int tick = 0; tick < fundedTicks + 17; ++tick) {
        researchEmptyCredits(owner);
        if(tick >= 17) {
            const Sint64 next = std::min(step, total - paid);
            owner->addUnstoredCredits(FixPoint::FromRawValue(next));
            paid += next;
        }
        game.updateGameState();
        researchRequire(builder->getCurrentUpgradeLevel() == originalLevel + (paid == total ? 1 : 0)
            && builder->isUpgrading() == (paid < total)
            && builder->getUpgradeProgress().getRawValue() == (paid < total ? paid : 0),
            "Full-game upgrade pause/resume lost progress, accumulated debt or changed completion level");
        if(expected)
            researchRequire(tick < static_cast<int>(expected->size())
                && GameStateDigest::describe(game.computeStateDigest()) == (*expected)[tick],
                "Mid-upgrade full-save/observer continuation digest diverged");
        else digests.push_back(GameStateDigest::describe(game.computeStateDigest()));
    }
    researchRequire(paid == total && !builder->isUpgrading()
        && (!expected || expected->size() == static_cast<std::size_t>(fundedTicks + 17)),
        "Upgrade continuation omitted cycles or failed to pay the exact remainder");
    return digests;
}

void upgradeHumanContinuations(const std::string& out, std::ostream& results) {
    for(const int item : {Structure_ConstructionYard, Structure_HeavyFactory}) {
        auto game = researchGame(false, false, 64, TechResearch::Pace::Normal, std::nullopt, 9);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* builder = upgradeBuilder(owner, item);
        const Uint32 builderID = builder->getObjectID();
        const int level = builder->getCurrentUpgradeLevel(), cost = builder->getUpgradeCost();
        const Sint64 total = FixPoint(cost).getRawValue(), step = upgradeStep(total);
        const std::string kind = upgradeKind(item);
        auto draw = [&](const char* state) {
            const auto name = kind + "_" + state;
            upgradeUIProbe(*game, builder, name.c_str());
        };
        researchEmptyCredits(owner);
        for(int id = 0; id < NUM_HOUSES; ++id) {
            auto* other = game->getHouse(id);
            if(other && other != owner && !other->getPlayerList().empty())
                Command(other->getPlayerList().front()->getPlayerID(), CMD_BUILDER_UPGRADE, builderID).executeCommand();
        }
        researchRequire(!builder->isUpgrading() && builder->getUpgradeProgress() == 0,
                        "Enemy upgrade command acted on the owner's yard/factory");
        owner->addUnstoredCredits(FixPoint::FromRawValue(1));
        researchRequire(owner->getCredits() == 0 && builder->canStartUpgrade(),
                        "Real human upgrade button eligibility requires a full displayed credit");
        draw("ready_partial");
        upgradeUIStart(*game, builder);
        game->getCommandManager().executeCommands(game->getGameCycleCount());
        researchRequire(builder->isUpgrading() && builder->getUpgradeProgress() == 0
            && researchBalance(owner).getRawValue() == 1, "Human command did not start an upgrade with partial cash");
        draw("active");
        researchRequire(upgradeTick(builder).getRawValue() == 1 && builder->getUpgradeProgress().getRawValue() == 1
            && builder->isUpgradePaused() && researchBalance(owner) == 0,
            "Real human upgrade did not drain a fractional raw unit and pause without cancelling");
        draw("paused");
        for(int paused = 0; paused < 31; ++paused)
            researchRequire(upgradeTick(builder) == 0 && builder->isUpgrading() && builder->isUpgradePaused()
                && builder->getCurrentUpgradeLevel() == level && builder->getUpgradeProgress().getRawValue() == 1,
                "Human upgrade changed level/progress or cancelled during a zero-cash pause");
        const auto digestBefore = GameStateDigest::describe(game->computeStateDigest());
        owner->addUnstoredCredits(FixPoint::FromRawValue(1));
        researchRequire(upgradeTick(builder).getRawValue() == 1 && builder->getUpgradeProgress().getRawValue() == 2
            && GameStateDigest::describe(game->computeStateDigest()) != digestBefore,
            "Fractional upgrade payment was omitted from the deterministic state digest");
        owner->addUnstoredCredits(FixPoint::FromRawValue(step * 200));
        for(int tick = 0; tick < 200; ++tick) upgradeTick(builder);
        const Sint64 paid = 2 + step * 200;
        researchRequire(builder->isUpgrading() && builder->getUpgradeProgress().getRawValue() == paid
            && builder->getUpgradeProgressFromStartingCredits().getRawValue() == paid && researchBalance(owner) == 0,
            "Mid-upgrade fixture lost exact paid/provenance progress");
        // Run the world's initial power-accounting tick with an empty till. The
        // continuation then finishes before its normal 15-second upkeep cadence.
        game->updateGameState();
        researchEmptyCredits(owner);
        researchRequire(builder->getUpgradeProgress().getRawValue() == paid && builder->isUpgradePaused(),
                        "Entering normal world updates changed the paused upgrade");
        draw("partial");
        const FixPoint provenance = builder->getUpgradeProgressFromStartingCredits();
        const auto digest = GameStateDigest::describe(game->computeStateDigest());
        const auto observer = game->saveObserverRuntime();
        const std::string save = out + "/upgrade-" + kind + "-partial.dls";
        researchRequire(game->saveGame(save), "Mid-upgrade full save failed");
        upgradeNote(results, "human_partial_saved", "human", *game, builder, paid);
        const auto expected = upgradeResume(*game, builderID, level, paid);
        draw("completed");
        researchEnd(game);
        game = researchLoad(save);
        game->loadObserverRuntime(observer);
        owner = game->getHouse(HOUSE_ATREIDES);
        builder = dynamic_cast<BuilderBase*>(game->getObjectManager().getObject(builderID));
        researchRequire(builder && game->getLoadedSavegameVersion() == SAVEGAMEVERSION
            && game->getGameInitSettings().getGameOptions().technologyInstallments
            && builder->isUpgrading() && builder->isUpgradePaused() && builder->getCurrentUpgradeLevel() == level
            && builder->getUpgradeProgress().getRawValue() == paid
            && builder->getUpgradeProgressFromStartingCredits() == provenance && researchBalance(owner) == 0
            && GameStateDigest::describe(game->computeStateDigest()) == digest,
            "Full save/load lost an upgrade's exact paid/provenance/paused state or digest");
        upgradeResume(*game, builderID, level, paid, &expected);
        upgradeNote(results, "human_resume_complete", "human", *game, builder, total);
        SDL_Log("UPGRADE_SAVE_CONTINUATION_PASS: item=%d paid_raw=%lld digest_cycles=%zu",
                item, static_cast<long long>(total), expected.size());
        researchEnd(game);
    }
}

void upgradeLegacyLayouts(const std::string& out, std::ostream& results) {
    for(const Uint32 version : {Uint32(9882), Uint32(9883)})
    for(const int item : {Structure_ConstructionYard, Structure_HeavyFactory}) {
        auto game = researchGame(false, false, 64, TechResearch::Pace::Normal, false, 9);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* builder = upgradeBuilder(owner, item);
        const Uint32 builderID = builder->getObjectID();
        const int level = builder->getCurrentUpgradeLevel(), cost = builder->getUpgradeCost();
        researchEmptyCredits(owner);
        researchRequire(!builder->canStartUpgrade() && !builder->doUpgrade(),
                        "Explicit OFF allowed a zero-cash upgrade past the legacy full-price start gate");
        owner->addUnstoredCredits(cost - 1);
        Command(owner->getPlayerList().front()->getPlayerID(), CMD_BUILDER_UPGRADE, builderID).executeCommand();
        researchRequire(!builder->isUpgrading() && !builder->canStartUpgrade()
            && builder->getUpgradeProgress() == 0 && researchBalance(owner) == cost - 1,
            "Explicit OFF accepted a below-full-price upgrade or spent rejected funds");
        researchEmptyCredits(owner);
        owner->addUnstoredCredits(0.25_fix);
        const std::string prefix = out + "/upgrade-synthetic-" + std::to_string(version) + "-" + upgradeKind(item);
        researchWriteLegacyLayout(*game, version, prefix + ".dls");
        researchEnd(game);
        game = researchLoad(prefix + ".dls");
        owner = game->getHouse(HOUSE_ATREIDES);
        builder = dynamic_cast<BuilderBase*>(game->getObjectManager().getObject(builderID));
        researchRequire(builder && game->getLoadedSavegameVersion() == version
            && !game->getGameInitSettings().getGameOptions().technologyInstallments
            && !builder->canStartUpgrade() && !builder->doUpgrade() && researchBalance(owner) == 0.25_fix,
            "Synthetic old save acquired new upgrade start privileges or changed its fractional balance");
        owner->addUnstoredCredits(FixPoint(cost) - 0.25_fix);
        Command(owner->getPlayerList().front()->getPlayerID(), CMD_BUILDER_UPGRADE, builderID).executeCommand();
        researchRequire(builder->isUpgrading() && builder->getUpgradeProgress() == 0 && researchBalance(owner) == cost,
                        "Legacy fully funded upgrade did not retain its old start-without-immediate-debit behavior");
        upgradeTick(builder);
        const FixPoint paid = builder->getUpgradeProgress(), provenance = builder->getUpgradeProgressFromStartingCredits();
        researchEmptyCredits(owner);
        researchRequire(upgradeTick(builder) == 0 && builder->isUpgrading() && builder->isUpgradePaused()
            && builder->getCurrentUpgradeLevel() == level && builder->getUpgradeProgress() == paid,
            "Legacy upgrade lost paid progress or cancelled while the cash drain was paused");
        researchRequire(game->saveGame(prefix + "-resaved.dls"), "Legacy active upgrade did not resave");
        researchEnd(game);
        game = researchLoad(prefix + "-resaved.dls");
        owner = game->getHouse(HOUSE_ATREIDES);
        builder = dynamic_cast<BuilderBase*>(game->getObjectManager().getObject(builderID));
        researchRequire(builder && game->getLoadedSavegameVersion() == SAVEGAMEVERSION
            && !game->getGameInitSettings().getGameOptions().technologyInstallments && builder->isUpgrading()
            && builder->isUpgradePaused() && builder->getCurrentUpgradeLevel() == level
            && builder->getUpgradeProgress() == paid && builder->getUpgradeProgressFromStartingCredits() == provenance
            && researchBalance(owner) == 0, "Current-format resave changed legacy upgrade rules or paid progress");
        upgradeNote(results, "legacy_full_price_gate", "human", *game, builder, paid.getRawValue(), version);
        SDL_Log("UPGRADE_LEGACY_LAYOUT_PASS: version=%u item=%d resaved=%u synthetic=1", version, item, SAVEGAMEVERSION);
        researchEnd(game);
    }
}
} // namespace

int runResearchLabProbe() {
    const std::string out = std::getenv("RESEARCH_LAB_PROBE_OUT");
    researchRequire(RandomSkirmish::activateMod("dunecity"), "DuneCity activation failed");
    settings.general.playerName = "Research tester";
    std::ofstream results(out + "/research.csv");
    std::ofstream upgrades(out + "/upgrades.csv");
    upgrades << "case,controller,item,cycle,level,active,progress_raw,provenance_raw,paused,cost,paid_raw,balance_raw,legacy_version\n";
    researchRequire(SettingsClass::GameOptionsClass{}.technologyInstallments && effectiveGameOptions.technologyInstallments,
                    "The isolated source-default profile did not enable technology installments");
    results << "case,cycle,tech_level,target,remaining,lab,credits,pace,installment,total_cost,total_cycles,paid_raw,paused,balance_raw\n";
    auto note = [&](const char* name, const Game& game, const House* owner) {
        researchNote(results, name, game, owner);
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
        researchRequire(rules.technologyInstallments == setup.getGameOptions().technologyInstallments,
                        "Save changed its research payment mode");
        const bool legacyPaid = loaded->getLoadedSavegameVersion() == 9882 || loaded->getLoadedSavegameVersion() == 9883;
        if(legacyPaid) researchRequire(!rules.technologyInstallments, "A real old save acquired new-game installment rules");
        SDL_Log("RESEARCH_SAVED_MATCH: cycle=%u version=%u width=%d height=%d pace=%u options=%s",
            loaded->getGameCycleCount(), loaded->getLoadedSavegameVersion(), currentGameMap->getSizeX(),
            currentGameMap->getSizeY(), static_cast<unsigned>(rules.researchPace), rules.getHash().c_str());
        for(const auto& row : rows) if(const House* house = loaded->getHouse(row.houseID)) {
            note("old_save_loaded", *loaded, house);
            SDL_Log("RESEARCH_SAVED_HOUSE: house=%d tech=%d target=%d remaining=%d credits=%d next_cost=%d next_cycles=%d",
                house->getHouseID(), house->getTechLevel(), house->getTechResearchTarget(), house->getTechResearchRemaining(),
                house->getCredits(), house->getTechResearchCost(), house->techResearchCycles(std::min(9, house->getTechLevel() + 1)));
            if(house->isTechResearchActive()) {
                if(legacyPaid)
                    researchRequire(!house->isTechResearchInstallment() && !house->isTechResearchPaused()
                        && house->getTechResearchPaid() == house->getTechResearchTotalCost(),
                        "A real old save's active research timer was not kept fully prepaid");
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
        std::vector<std::pair<int, ResearchSnapshot>> snapshots;
        for(const auto& row : rows) if(const House* house = loaded->getHouse(row.houseID))
            snapshots.emplace_back(row.houseID, researchSnapshot(house));
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
        researchRequire(loaded->getGameInitSettings().getGameOptions().technologyInstallments
                            == setup.getGameOptions().technologyInstallments,
                        "Resave/reload changed the original research payment rule");
        for(const auto& state : snapshots) {
            const House* house = loaded->getHouse(state.first);
            researchSameState(house, state.second);
            researchRequire(house->getTechResearchCost() == TechResearch::cost(house->getTechLevel() + 1,
                setup.getGameOptions().researchPace), "Migrated house did not use the new research price");
            note("old_save_resaved_reloaded", *loaded, house);
        }
        SDL_Log("RESEARCH_SAVED_RELOAD: cycle=%u version=%u houses=%zu",
            loaded->getGameCycleCount(), loaded->getLoadedSavegameVersion(), snapshots.size());
        researchEnd(loaded);
        researchRequire(RandomSkirmish::activateMod("dunecity"), "DuneCity reactivation after user save failed");
    }

    researchInstallmentTotals(results);
    researchPrepaidRuleChange(results);
    researchInstallmentContinuation(out, results);
    researchLegacyLayouts(out, results);
    upgradeExactTotals(upgrades);
    upgradeAIPartials(upgrades);
    upgradeHumanContinuations(out, upgrades);
    upgradeLegacyLayouts(out, upgrades);

    // Preserve all original upfront assertions under an explicit OFF rule.
    auto game = researchGame(true, false, 64, TechResearch::Pace::Normal, false);
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
    researchUIProbe(*game, lab, "upfront_researching");
    // A paid legacy timer may be longer than the new quote, even after a new-format resave.
    constexpr int beforeSaveCycles = 123;
    owner->techResearchRemaining += 2 * beforeSaveCycles;
    owner->techResearchTotalCycles += 2 * beforeSaveCycles;
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

    game = researchGame(true, false, 160, TechResearch::Pace::Normal, false);
    owner = game->getHouse(HOUSE_ATREIDES);
    researchRequire(owner->getTechResearchCost() == 1000 && owner->techResearchCycles(2) == 7500
        && game->getGameInitSettings().getGameOptions().getHash() == normalRulesHash,
        "Map dimensions changed the research quote or rules hash");
    note("map_independent_quote", *game, owner);
    researchEnd(game);

    game = researchGame(true, false, 160, TechResearch::Pace::Slow, false);
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
    researchRequire(game->getGameInitSettings().getGameOptions().technologyInstallments,
                    "Natural QuantBot fixture did not retain the new-game installment default");
    bool seenInstallment = false;
    const int limit = MILLI2CYCLES(15 * 60 * 1000);
    int seenLab = -1;
    for(int cycle = 0; cycle < limit && owner->getTechLevel() < 2; ++cycle) {
        game->updateGameState();
        if(owner->isTechResearchActive()) {
            researchRequire(owner->isTechResearchInstallment()
                && owner->getTechResearchTotalCost() == TechResearch::cost(owner->getTechResearchTarget(), TechResearch::Pace::Normal)
                && owner->getTechResearchTotalCycles() == owner->techResearchCycles(owner->getTechResearchTarget()),
                "QuantBot bypassed the shared captured installment engine API");
            seenInstallment = true;
        }
        if(seenLab < 0 && owner->getNumItems(Structure_ResearchLab) > 0) {
            seenLab = game->getGameCycleCount();
            note("quantbot_built_lab", *game, owner);
        }
    }
    note("quantbot_final", *game, owner);
    researchRequire(seenLab >= 0 && owner->getTechLevel() >= 2, "Tech-1 QuantBot failed to build a lab and research");
    researchRequire(seenInstallment, "QuantBot did not expose an ordinary progressive research timer");
    researchEnd(game);
    SDL_Log("RESEARCH_LAB_PROBE_PASS: technology installments; 32 exact research totals; human zero-cash UI; research/upgrade partial-save digest continuation; 600-cycle CY/factory upgrades human+QuantBot; pause/resume; captured research terms; synthetic9882/9883 old rules; original upfront cases; ownership/auth");
    return MENU_QUIT_DEFAULT;
}
