// Synthetic, physical city; selection/completion cases use full QuantBot build
// and native paid production/placement. Labelled budget/claim/wrapper controls
// use the same real offers and legal sites. No private user save is required.
#include <string_view>
namespace CompactPowerProbe {
using TechProductionProbe::World;
using TechProductionProbe::Stage;
using TechProductionProbe::Army;
using TechProductionProbe::PaidProduction;
using TechProductionProbe::require;

void seed(World& world, bool enabled, int credits = 2600) {
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.cityCompactPower = enabled;
    config->force.cityCompactPowerInstallments = false;
    config->force.startWhatMakesSense = false;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
    // Initial authored assets: 100 output against three real 30-power bays.
    // The distant wind anchor admits a normal 3x3 reactor without exposing the
    // yard/refineries to its blast; no power counters or build offers are faked.
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    for (Coord site : {Coord(3, 18), Coord(10, 18), Coord(17, 18)})
        world.placeAt(Structure_Refinery, site);
    world.owner->updateBuildLists();
    world.closeMilitaryMarket();
    world.cash(credits); // Scenario grant only; never topped up during production.
    require(world.owner->getProducedPower() == 100 && world.owner->getPowerRequirement() == 90,
            "Compact-power scenario does not have physical 100/90 power");
}

struct ReactorQuote {
    int price = 0, foundation = 0, first = 0;
    int full() const { return price + foundation; }
    int start() const { return first + foundation; }
};

ReactorQuote reactorQuote(World& world) {
    world.bot->planningBuilder = world.yard->getObjectID();
    world.bot->clearPlacementCache(false, true);
    const Coord site = world.bot->findPlaceLocation(Structure_NuclearPlant);
    require(site.isValid(), "Compact financing has no actual reactor site");
    const auto plan = world.bot->foundationPlan(world.yard, Structure_NuclearPlant, site);
    require(plan.complete, "Compact financing has no complete legal foundation");
    ReactorQuote quote;
    quote.price = world.bot->purchasePrice(world.yard, Structure_NuclearPlant);
    for (const auto& slab : plan.orders) quote.foundation += world.bot->purchasePrice(world.yard, slab.item);
    auto pass = world.observePlan();
    quote.first = CityEconomyInvestmentPolicy::firstInstallment(
        quote.price, world.bot->cityBuildTicks(pass, Structure_NuclearPlant));
    return quote;
}

void configureFunding(World& world, bool growth, bool simple) {
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.cityCompactPower = true;
    config->force.cityCompactPowerInstallments = true;
    config->force.startWhatMakesSense = false;
    config->force.cityGrowthPower = growth;
    config->force.simpleTechSavings = simple;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
}

void clearOrders(World& world) {
    for (auto* structure : structureList) {
        auto* builder = dynamic_cast<BuilderBase*>(structure);
        if (!builder || structure->getOwner() != world.owner) continue;
        while (builder->getProductionQueueSize()) builder->doCancelItem(builder->getCurrentProducedItem());
    }
    world.bot->builderPlaceLocations.clear();
    world.bot->reservedStructures.clear();
    world.bot->clearPlacementCache();
}

void fundingBoundaries(World& world, const ReactorQuote& quote) {
    // Actual house cash is deliberately abundant. The compact decision must
    // still use this pass's spendable budget after other commitments.
    world.cash(quote.full() + 100);
    const auto base = world.observePlan();
    require(base.forecastNetIncome > 0 && !base.startWhatMakesSense,
            "Working compact city lacks positive native forecast income with SWMS OFF");
    auto choice = [&](int spendable, int income, bool expected) {
        auto pass = base;
        pass.money = spendable;
        pass.forecastNetIncome = income; // Explicit positive, dry and loss-making outlook controls.
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = world.yard; turn.pBuilder = world.yard;
        QuantBot::YardTurn yard;
        yard.isCitySim = true; yard.itemID = Structure_WindTrap; yard.structureRule = "power";
        world.bot->planningBuilder = world.yard->getObjectID();
        world.bot->clearPlacementCache(false, true);
        world.bot->vetYardChoice(pass, turn, yard);
        require((yard.itemID == Structure_NuclearPlant) == expected,
                "Compact choice ignored its actual start budget or continuing-income condition");
        if (expected && spendable < quote.full())
            require(world.bot->onInstallments(turn, Structure_NuclearPlant)
                        && turn.installmentProjectStartBudget == quote.first,
                    "Compact order did not retain its reactor-only first installment");
    };
    choice(quote.start() - 1, base.forecastNetIncome, false);
    choice(quote.start(), base.forecastNetIncome, true);
    choice(quote.start(), 1, true); // Positive income need not fund the full quote inside one forecast.
    for (const int income : {0, -1}) {
        choice(quote.full() - 1, income, false);
        choice(quote.full(), income, true);
    }

    if (world.game->getGameInitSettings().getGameOptions().concreteRequired) {
        const auto offers = world.yard->buildList;
        world.yard->buildList.remove_if([](const BuildItem& offer) {
            return offer.itemID == Structure_Slab1 || offer.itemID == Structure_Slab4;
        });
        world.bot->planningBuilder = world.yard->getObjectID();
        world.bot->clearPlacementCache(false, true);
        const Coord site = world.bot->findPlaceLocation(Structure_NuclearPlant);
        require(site.isValid() && !world.bot->foundationPlan(world.yard, Structure_NuclearPlant, site).complete,
                "Incomplete-foundation control still has legally buildable concrete");
        auto pass = base;
        require(!world.bot->preferCompactNuclear(pass, world.yard),
                "Even fully funded compact power bypassed an incomplete legal foundation");
        world.yard->buildList = offers;
        world.bot->clearPlacementCache();
    }
}

void samePassAccounting(World& world) {
    // The first reactor has finished. Add real, idle suppliers for a second
    // requested power package and an ordinary factory order in the same pass.
    auto* factory = static_cast<BuilderBase*>(world.place(Structure_LightFactory));
    auto* otherYard = static_cast<ConstructionYard*>(world.place(Structure_ConstructionYard));
    for (auto* builder : {static_cast<BuilderBase*>(world.yard), factory, static_cast<BuilderBase*>(otherYard)})
        builder->curUpgradeLev = builder->getMaxUpgradeLevel();
    world.owner->updateBuildLists();

    for (bool simple : {false, true}) for (bool foundedHelper : {false, true}) {
        clearOrders(world);
        configureFunding(world, true, simple);
        auto quote = reactorQuote(world);
        world.cash(quote.full() + 100);
        auto pass = world.observePlan();
        require(pass.forecastNetIncome > 0, "Same-pass compact fixture lost its continuing income");
        Uint32 unit = NONE_ID;
        for (const auto& offer : factory->getBuildList())
            if (QuantBotBuildPolicy::militaryItem(offer.itemID)
                && world.bot->compositionAdmits(pass, offer.itemID, UnitMixPolicy::kSlots)
                && world.bot->campaignAvailableToBuild(factory, offer.itemID)) { unit = offer.itemID; break; }
        require(unit != NONE_ID, "Ordinary factory control has no legal selected unit");
        const int unitPrice = world.bot->purchasePrice(factory, unit);
        {
            auto ordinary = pass;
            ordinary.money = unitPrice - 1;
            QuantBot::BuilderTurn turn(ordinary.money);
            turn.pStructure = factory; turn.pBuilder = factory;
            require(!world.bot->produceItemWithLogging(ordinary, turn, unit, __LINE__, "compact_factory_floor"),
                    "Ordinary factory admission stopped requiring its full quote");
            ordinary.money = unitPrice;
            require(world.bot->produceItemWithLogging(ordinary, turn, unit, __LINE__, "compact_factory_funded")
                        && ordinary.money == 0 && ordinary.capitalOrderedCost == unitPrice,
                    "Ordinary factory no longer books its accepted full quote");
            factory->doCancelItem(unit);
        }
        pass.money = quote.start();
        const int beforeCount = pass.itemCount[Structure_NuclearPlant];
        const auto actualCash = world.owner->getStartingCredits() + world.owner->getEarnedCredits();
        const auto spent = world.spentQ32();
        {
            QuantBot::BuilderTurn turn(pass.money);
            turn.pStructure = world.yard; turn.pBuilder = world.yard;
            QuantBot::YardTurn yard;
            yard.pConstYard = world.yard;
            yard.placeLocations = &world.bot->builderPlaceLocations[world.yard->getObjectID()];
            yard.isCitySim = true; yard.itemID = Structure_WindTrap; yard.structureRule = "power";
            world.bot->planningBuilder = world.yard->getObjectID();
            world.bot->clearPlacementCache(false, true);
            world.bot->vetYardChoice(pass, turn, yard);
            require(yard.itemID == Structure_NuclearPlant, "Compact accounting control lost its funded reactor");
            if (foundedHelper)
                require(world.bot->queueFoundedStructure(pass, turn, yard.itemID, __LINE__, "compact_founded_helper"),
                        "Founded helper refused a genuinely funded compact installment package");
            else world.bot->placeYardChoice(pass, turn, yard);
        }
        require(world.queued(Structure_NuclearPlant) == 1
                    && pass.money == quote.start() - quote.full() && pass.capitalOrderedCost == quote.full(),
                "Compact slabs/reactor reused the selected-tech no-debit exemption or double-counted their start budget");
        require(world.owner->getStartingCredits() + world.owner->getEarnedCredits() == actualCash
                    && world.spentQ32() == spent,
                "Queue acceptance prepaid or invented actual house credits");
        require(world.bot->reservedStructures.count(world.yard->getObjectID()) == 1,
                "Accepted compact package lost its site reservation");
        if (!foundedHelper) {
            require(pass.orderedThisTick.count(Structure_NuclearPlant) == 1
                        && pass.itemCount[Structure_NuclearPlant] == beforeCount + 1,
                    "Ordinary yard acceptance lost its same-pass generator commitment");
            QuantBot::BuilderTurn second(pass.money);
            second.pStructure = otherYard; second.pBuilder = otherYard;
            world.bot->buildConstructionYard(pass, second);
            require(world.queued(Structure_NuclearPlant) == 1,
                    "Another yard duplicated a compact installment reactor in the same pass");
        }
        const int booked = pass.capitalOrderedCost;
        QuantBot::BuilderTurn next(pass.money);
        next.pStructure = factory; next.pBuilder = factory;
        require(!world.bot->produceItemWithLogging(pass, next, unit, __LINE__, "compact_same_pass_factory")
                    && pass.capitalOrderedCost == booked && factory->getProductionQueueSize() == 0,
                "An ordinary factory spent cash already committed to the compact package");
    }
    clearOrders(world);
}

void fundedTwoYards(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
    seed(world, true);
    configureFunding(world, false, false);
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    // Two separated generation anchors leave two genuinely legal reactor lots.
    // Actual consumers exceed actual output; accepting the first reactor does
    // not cure this blackout before its native production and placement finish.
    world.placeAt(Structure_WindTrap, Coord(36, 8));
    for (Coord site : {Coord(24, 18), Coord(3, 25), Coord(10, 25), Coord(17, 25)})
        world.placeAt(Structure_Refinery, site);
    auto* otherYard = static_cast<ConstructionYard*>(world.place(Structure_ConstructionYard));
    for (auto* yard : {world.yard, otherYard}) yard->curUpgradeLev = yard->getMaxUpgradeLevel();
    world.owner->updateBuildLists();
    const auto quote = reactorQuote(world);
    const int credits = 3 * quote.full();
    while (world.owner->getCapacity() < credits) {
        const int before = world.owner->getCapacity();
        world.place(Structure_Silo);
        require(world.owner->getCapacity() > before, "Funded two-yard city could not add native storage");
    }
    world.cash(credits);
    auto pass = world.observePlan();
    require(!world.owner->hasPower() && world.owner->getProducedPower() < world.owner->getPowerRequirement(),
            "Funded two-yard control has no actual outstanding power demand");
    {
        QuantBot::BuilderTurn first(pass.money);
        first.pStructure = world.yard; first.pBuilder = world.yard;
        world.bot->buildConstructionYard(pass, first);
    }
    require(world.queued(Structure_NuclearPlant) == 1
                && world.bot->reservedStructures.count(world.yard->getObjectID()) == 1,
            "Funded first yard did not reserve and order its real reactor");

    world.bot->planningBuilder = otherYard->getObjectID();
    world.bot->clearPlacementCache(false, true);
    const Coord site = world.bot->findPlaceLocation(Structure_NuclearPlant);
    require(site.isValid(), "Pending-reactor control has no separate legal second reactor lot");
    const auto foundation = world.bot->foundationPlan(otherYard, Structure_NuclearPlant, site);
    require(foundation.complete, "Pending-reactor control cannot legally found its second reactor");
    int secondQuote = world.bot->purchasePrice(otherYard, Structure_NuclearPlant);
    for (const auto& slab : foundation.orders) secondQuote += world.bot->purchasePrice(otherYard, slab.item);
    require(world.owner->getCredits() >= secondQuote && pass.money >= secondQuote,
            "Pending-reactor control is independently blocked by actual or planning cash");
    auto withoutPending = pass;
    withoutPending.itemCount[Structure_NuclearPlant] = world.owner->getNumItems(Structure_NuclearPlant);
    withoutPending.orderedThisTick.erase(Structure_NuclearPlant);
    require(!world.bot->preferCompactNuclear(pass, otherYard)
                && world.bot->preferCompactNuclear(withoutPending, otherYard),
            "Pending commitment, rather than funding/site/dominance, did not exclude the second reactor");
    {
        QuantBot::BuilderTurn second(pass.money);
        second.pStructure = otherYard; second.pBuilder = otherYard;
        world.bot->buildConstructionYard(pass, second);
    }
    require(world.queued(Structure_NuclearPlant) == 1 && world.queued(Structure_WindTrap) >= 1,
            "Funded second yard duplicated the pending reactor or lost legitimate blackout wind recovery");
    SDL_Log("COMPACT_POWER_TWO_YARDS: difficulty=%s remaining_cash=%d second_quote=%d "
            "actual_deficit=1 separate_legal_site=1 pending_blocks_reactor=1 blackout_wind=1",
            playerClass, pass.money, secondQuote);
}

void nativeFinancing(const char* playerClass, bool concrete) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
    seed(world, true);
    configureFunding(world, false, false);
    world.game->gameInitSettings.gameOptions.concreteRequired = concrete;
    world.yard->curUpgradeLev = world.yard->getMaxUpgradeLevel();
    world.owner->updateBuildLists();
    // Initial, working scenario assets, not a fabricated income counter. Loaded
    // workers return real cargo, then harvest the existing real spice field.
    for (int i = 0; i < 8; ++i) {
        auto* worker = static_cast<Harvester*>(world.spawn(world.owner, Unit_Harvester, Coord(37 + i % 3, 25 + i / 3)));
        worker->doSetAttackMode(HARVEST);
        worker->setAmountOfSpice(worker->getSpiceCapacity());
        worker->doReturn();
    }
    // Let the native simulation establish positive housing demand; do not
    // manufacture valves or a tax base to make a growth-power claim exist.
    const Uint32 demandEnd = world.game->getGameCycleCount() + MILLI2CYCLES(30000);
    while (world.game->getGameCycleCount() < demandEnd
        && world.game->getCitySimulation()->getHouseState(world.owner->getHouseID()).resValve <= 0) world.tick();
    require(world.game->getCitySimulation()->getHouseState(world.owner->getHouseID()).resValve > 0,
            "Native compact city never developed demand for its growth-power control");
    const auto quote = reactorQuote(world);
    require(quote.start() > 0 && quote.start() < 100 && quote.start() < quote.price
                && (concrete ? quote.foundation > 0 : quote.foundation == 0),
            "Native compact fixture does not straddle the actual below-100 founded start boundary");
    fundingBoundaries(world, quote);
    {
        auto off = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        off->force.cityCompactPower = true;
        off->force.cityCompactPowerInstallments = false;
        world.bot->configOverride = off;
        world.cash(quote.full() - 1);
        const auto pass = world.observePlan();
        require(pass.forecastNetIncome > 0 && pass.money < quote.full(),
                "Financing-OFF control lacks positive income or is already fully funded");
        world.build();
        require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_NuclearPlant) == 0,
                "Financing-OFF compact city did not retain its underfunded legacy Windtrap choice");
        clearOrders(world);
    }
    {
        auto off = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        off->force.cityCompactPower = false;
        off->force.cityCompactPowerInstallments = true;
        world.bot->configOverride = off;
        world.cash(quote.start());
        world.build();
        require(world.yard->getProductionQueueSize() == 0,
                "Compact-OFF legacy yard gained a below-100 installment entry");
    }
    for (bool growth : {false, true}) for (bool simple : {false, true}) {
        clearOrders(world);
        configureFunding(world, growth, simple);
        world.cash(quote.start()); // Separate initial scenario boundary; no construction has started.
        auto pass = world.observePlan();
        require(pass.forecastNetIncome > 0 && pass.money == quote.start(),
                "Compact start boundary is not spendable with native positive income");
        if (growth) {
            // Exercise the selected growth-power claim itself, independent of
            // incidental ranking against other legitimate opening projects.
            auto selected = pass;
            int index = -1;
            for (size_t i = 0; i < selected.capitalCandidates.size(); ++i) {
                const auto& candidate = selected.capitalCandidates[i];
                if (std::string_view(candidate.kind) == "city_growth_power"
                    && candidate.builder == world.yard->getObjectID()
                    && candidate.item == Structure_NuclearPlant && candidate.score > 0) {
                    require(candidate.price == quote.price && candidate.foundationCost == quote.foundation,
                            "Growth compact quote counted estimated and actual foundations together");
                    index = int(i); break;
                }
            }
            require(index >= 0, "Working compact city has no legal demanded growth-power candidate");
            selected.capitalChoice = index;
            selected.capitalInitialCount = selected.itemCount[Structure_NuclearPlant];
            selected.capitalConsumed = false;
            QuantBot::BuilderTurn turn(selected.money);
            turn.pStructure = world.yard; turn.pBuilder = world.yard;
            QuantBot::YardTurn yard;
            world.bot->planningBuilder = world.yard->getObjectID();
            world.bot->clearPlacementCache(false, true);
            world.bot->startYardSelection(selected, turn, yard);
            require(yard.itemID == Structure_NuclearPlant
                        && world.bot->onInstallments(turn, Structure_NuclearPlant)
                        && turn.installmentProjectStartBudget == quote.first,
                    "Selected growth-power claim held a genuinely funded compact start for its full quote");
        }
        SDL_Log("COMPACT_POWER_START: difficulty=%s concrete=%d growth=%d simple=%d actual_cash=%d "
                "preplan_cash=%d preplan_net_income=%d workers=%d price=%d foundation=%d start=%d "
                "power=%d/%d coverage_shortfall=%d turret_power_ready=%d",
                playerClass, int(concrete), int(growth), int(simple), world.owner->getCredits(),
                pass.money, pass.forecastNetIncome, pass.actualHarvesters, quote.price, quote.foundation, quote.start(),
                world.owner->getProducedPower(), world.owner->getPowerRequirement(),
                world.bot->proactiveCoverageShortfall(pass), int(world.bot->hasPowerBufferForTurret(pass)));
        world.build();
        SDL_Log("COMPACT_POWER_START_RESULT: difficulty=%s concrete=%d growth=%d simple=%d "
                "nuclear_queued=%d wind_queued=%d yard_queue=%d actual_cash=%d",
                playerClass, int(concrete), int(growth), int(simple), world.queued(Structure_NuclearPlant),
                world.queued(Structure_WindTrap), world.yard->getProductionQueueSize(), world.owner->getCredits());
        require(world.queued(Structure_NuclearPlant) == 1 && world.queued(Structure_WindTrap) == 0,
                "Full SWMS-OFF bot did not start a compact reactor at its actual founded start budget");
        world.build();
        require(world.queued(Structure_NuclearPlant) == 1, "Following pass duplicated low-cash compact generation");
    }
    // Keep the final genuine queue, with both interacting features ON. No cash
    // top-ups from here through completion: only native spice/tax receipts pay.
    const int64_t spentBefore = world.spentQ32();
    const int powerBefore = world.owner->getProducedPower();
    PaidProduction paid(world);
    paid.observe();
    const Uint32 first = world.game->getGameCycleCount(), end = first + MILLI2CYCLES(180000);
    while (world.game->getGameCycleCount() < end && !paid.completed(Structure_NuclearPlant)) paid.step();
    require(paid.completed(Structure_NuclearPlant)
                && world.owner->getProducedPower() >= powerBefore + 2000,
            "Low-cash compact reactor did not finish native paid construction and supply power");
    require(world.spentQ32() - spentBefore >= (int64_t(quote.full()) << 32),
            "Low-cash compact package completed without real foundation/reactor payments");
    SDL_Log("COMPACT_POWER_FINANCING: difficulty=%s concrete=%d start=%d full=%d elapsed=%u "
            "cash=%d native_paid=1 growth_simple_matrix=1 hash=%s",
            playerClass, int(concrete), quote.start(), quote.full(), world.game->getGameCycleCount() - first,
            world.owner->getCredits(), world.configHash.c_str());
    samePassAccounting(world);
}

void controls(const char* playerClass) {
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        seed(world, true, 1999);
        configureFunding(world, false, false);
        const auto quote = reactorQuote(world);
        const auto pass = world.observePlan();
        require(pass.forecastNetIncome <= 0 && pass.money >= quote.start() && pass.money < quote.full(),
                "1999-credit no-worker control does not exercise the nonpositive-income funding rule");
        world.build();
        require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_NuclearPlant) == 0,
                "An underfunded no-income city started a compact reactor it cannot finish");
    }
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        seed(world, true);
        world.game->techLevel = 5;
        world.owner->updateBuildLists();
        world.build();
        require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_NuclearPlant) == 0,
                "Compact choice bypassed normal reactor technology");
    }
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        seed(world, true);
        world.bot->planningBuilder = world.yard->getObjectID();
        // Author two separate two-column strips beside the existing Windtrap:
        // each holds a 2x2 building plus a two-tile frontage, and the sand
        // column between them means no 3x3 plot exists. As in the ordinary
        // worlds, the light factory reserves its own production plot first;
        // one lot alone would be that plot and leave no generator site.
        // Changing terrain alone retains road flags and can strand the old
        // road network underneath an otherwise legal small footprint.
        const Coord small(18, 11), factoryLot(15, 11);
        for (int y = 0; y < currentGameMap->getSizeY(); ++y)
            for (int x = 0; x < currentGameMap->getSizeX(); ++x) {
                auto* tile = currentGameMap->getTile(x, y);
                if (!tile->hasAStructure()) {
                    tile->setRoad(false);
                    tile->setType(Terrain_Sand);
                }
            }
        for (Coord lot : {small, factoryLot}) {
            for (int y = lot.y - 1; y < lot.y + 2; ++y)
                for (int x = lot.x; x < lot.x + 2; ++x)
                    currentGameMap->getTile(x, y)->setType(Terrain_Rock);
            require(currentGameMap->okayToPlaceStructure(lot.x, lot.y, 2, 2,
                        false, world.owner, false, Structure_WindTrap),
                    "Small generator control is not human-legal");
        }
        world.bot->clearPlacementCache();
        require(world.bot->findPlaceLocation(Structure_NuclearPlant).isInvalid(),
                "Small-plot control still admits a reactor");
        require(world.bot->findPlaceLocation(Structure_WindTrap).isValid(),
                "Small-plot control excludes the windtrap too");
        world.build();
        require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_NuclearPlant) == 0,
                "Compact choice bypassed the real reactor footprint");
    }
}
} // namespace CompactPowerProbe

void runCompactPowerProbe(const char* playerClass) {
    using namespace CompactPowerProbe;
    for (bool enabled : {false, true}) {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        seed(world, enabled);
        const Uint32 expected = enabled ? Structure_NuclearPlant : Structure_WindTrap;
        const int outputBefore = world.owner->getProducedPower();
        const int64_t spentBefore = world.spentQ32();
        PaidProduction paid(world);
        world.build(); paid.observe();
        require(world.queued(expected) == 1, "Full bot did not order the expected OFF/ON generator");
        require(world.queued(enabled ? Structure_WindTrap : Structure_NuclearPlant) == 0,
                "Full bot ordered both generators for the same power purchase");
        world.build(); paid.observe();
        require(world.queued(expected) == 1, "Pending generation was ordered twice");
        const Uint32 first = world.game->getGameCycleCount(), end = first + MILLI2CYCLES(120000);
        while (world.game->getGameCycleCount() < end && !paid.completed(expected)) paid.step();
        require(paid.completed(expected), "Chosen generator did not finish ordinary paid production and placement");
        require(world.owner->getProducedPower() >= outputBefore + (enabled ? 2000 : 100),
                "Completed generator did not supply its actual rated power");
        require(world.spentQ32() - spentBefore >= (int64_t(world.price(expected)) << 32),
                "Generator completed without the actual house credit payments");
        SDL_Log("COMPACT_POWER_CASE: difficulty=%s enabled=%d item=%u elapsed=%u power=%d/%d cash=%d hash=%s",
                playerClass, int(enabled), expected, world.game->getGameCycleCount() - first,
                world.owner->getProducedPower(), world.owner->getPowerRequirement(), world.owner->getCredits(),
                world.configHash.c_str());
    }
    controls(playerClass);
    nativeFinancing(playerClass, false);
    nativeFinancing(playerClass, true);
    fundedTwoYards(playerClass);
    SDL_Log("COMPACT_POWER_PASS: difficulty=%s negative_control=wind funded_on=nuclear paid=1 "
            "low_cash_paid=1 start_boundary=1 nonpositive_income=1 concrete_controls=1 "
            "growth_simple_matrix=1 same_pass_quotes=1 tech_control=1 footprint_control=1 pending_control=1",
            playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=compact_power outcome=passed");
}
