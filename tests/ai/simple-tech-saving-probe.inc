// Private balance executable only. Include after tech-production-probe.inc: its real
// World and PaidProduction remain the source of game setup, payment and queues.
// Structures start at zero cash on ordinary engine installments. Upgrades keep
// the human full-credit start rule, with no extra AI reserve. No saved wallet.
#include <chrono>
#include <cstring>
#include <dunecity/ResidentialPopulation.h>

namespace SimpleTechSavingProbe {

using TechProductionProbe::Army;
using TechProductionProbe::World;
using TechProductionProbe::Stage;
using TechProductionProbe::PaidProduction;
using TechProductionProbe::vehicles;
using TechProductionProbe::light;

inline void require(bool ok, const char* reason) {
    if (!ok) {
        SDL_Log("SIMPLE_TECH_SAVING_FAIL: %s", reason);
        throw std::runtime_error(reason);
    }
}

inline bool contentReachable(const World& world, Uint32 goal) {
    const auto& data = world.game->objectData.data;
    const int house = world.owner->getHouseID();
    std::array<int, Num_ItemID> seen{};
    auto visit = [&](auto&& self, Uint32 item) -> bool {
        if (item >= Num_ItemID) return false;
        if (isStructure(item) && world.owner->getNumItems(item) > 0) return true;
        if (seen[item] != 0) return seen[item] == 2;
        seen[item] = 1;
        const auto& spec = data[item][house];
        if (!spec.enabled || spec.price <= 0 || spec.techLevel > world.owner->getTechLevel()) return false;
        for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
            if (isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite]
                && !self(self, prerequisite)) return false;
        seen[item] = 2;
        return true;
    };
    return visit(visit, goal);
}

inline void notApplicable(const char* name, const char* mod, const char* playerClass,
                          const char* reason, Uint32 item = NONE_ID) {
    SDL_Log("SIMPLE_TECH_SAVING_NOT_APPLICABLE: case=%s mod=%s difficulty=%s item=%u reason=%s",
            name, mod, playerClass, item, reason);
}

// Real-time heartbeat and explicit censor diagnostics survive long City runs.
// An interruption never satisfies an assertion: a missed simulation deadline
// still throws, even if crime, loss of income or a blackout explains the miss.
struct Case {
    World& world;
    const char* name;
    const char* mod;
    const char* playerClass;
    const Uint32 startCycle;
    const std::chrono::steady_clock::time_point startWall = std::chrono::steady_clock::now();
    Uint32 lastInspection = 0;
    Uint32 lastHeartbeat = 0;
    int observations = 0;
    int noIncome = 0;
    int blackout = 0;
    int crime = 0;
    int localEmergency = 0;
    int forecast = 0;
    int population = 0;
    int readyUnits = 0;
    int readyValue = 0;
    Uint32 candidate = NONE_ID;
    int quote = 0;
    bool active = false;
    bool ended = false;

    Case(World& world, const char* name, const char* mod, const char* playerClass)
        : world(world), name(name), mod(mod), playerClass(playerClass),
          startCycle(world.game->getGameCycleCount()) {
        SDL_Log("SIMPLE_TECH_SAVING_CASE_START: case=%s mod=%s difficulty=%s enabled=%d cycle=%u initial_cash=%d config_hash=%s",
                name, mod, playerClass, int(world.bot->botConfig().force.simpleTechSavings),
                startCycle, world.owner->getCredits(), world.configHash.c_str());
        inspect();
    }

    ~Case() {
        if (!ended) report("interrupted");
    }

    void report(const char* outcome) const {
        const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startWall).count();
        SDL_Log("SIMPLE_TECH_SAVING_CASE: case=%s mod=%s difficulty=%s outcome=%s enabled=%d "
                "cycle=%u elapsed_cycles=%u wall_ms=%lld observations=%d no_income_observations=%d "
                "blackout_observations=%d crime_observations=%d local_emergency_observations=%d "
                "cash=%d forecast_income=%d population=%d power=%d/%d ready_units=%d ready_value=%d "
                "candidate=%u quote=%d saving_active=%d full_turns=%d config_hash=%s",
                name, mod, playerClass, outcome, int(world.bot->botConfig().force.simpleTechSavings),
                world.game->getGameCycleCount(), world.game->getGameCycleCount() - startCycle,
                static_cast<long long>(wall), observations, noIncome, blackout, crime, localEmergency,
                world.owner->getCredits(), forecast, population, world.owner->getProducedPower(),
                world.owner->getPowerRequirement(), readyUnits, readyValue, candidate, quote,
                int(active), world.fullTurns, world.configHash.c_str());
    }

    void inspect() {
        const auto pass = world.observePlan();
        require(pass.techSavings == world.bot->botConfig().force.simpleTechSavings,
                "Simple fixture did not select the single hashed active-policy build-pass flag");
        if (pass.techSavingActive)
            require(pass.techCandidate.item != NONE_ID && pass.techCandidate.builder != NONE_ID
                        && pass.techCandidate.cost > 0,
                    "Active simple project has no single concrete next legal choice");
        int projects = 0;
        for (const auto& choice : pass.capitalCandidates)
            if (std::strcmp(choice.kind, "tech_progression") == 0
                || std::strcmp(choice.kind, "tech_upgrade") == 0
                || std::strcmp(choice.kind, "tech_research") == 0) ++projects;
        require(projects <= 1, "One planning pass selected several independent technology projects");
        if (pass.techCandidateIndex >= 0) {
            require(size_t(pass.techCandidateIndex) < pass.capitalCandidates.size(),
                    "Simple project points outside the shared capital planner");
            const auto& choice = pass.capitalCandidates[size_t(pass.techCandidateIndex)];
            require(choice.builder == pass.techCandidate.builder && choice.item == pass.techCandidate.item
                        && choice.cost == pass.techCandidate.cost,
                    "Simple project was not the same choice consumed by the shared capital planner");
        }
        if (pass.techFundingPriority)
            require(pass.techSavingActive && pass.techCandidateIndex >= 0
                        && pass.capitalChoice == pass.techCandidateIndex,
                    "Feasible or in-flight technology claimed capital owned by an earlier economic or defensive choice");
        ++observations;
        noIncome += pass.forecastNetIncome <= 0;
        blackout += !world.owner->hasPower();
        crime += pass.dangerousCrimeProperties > 0;
        localEmergency += pass.militaryEmergency;
        forecast = pass.forecastNetIncome;
        population = pass.ownTotalPop;
        const auto ready = TechProductionProbe::actualGroundReadiness(world);
        readyUnits = ready.first;
        readyValue = ready.second;
        candidate = pass.techCandidate.item;
        quote = pass.techCandidate.cost;
        active = pass.techSavingActive;
        lastInspection = world.game->getGameCycleCount();
    }

    void poll() {
        const Uint32 cycle = world.game->getGameCycleCount();
        if (cycle - lastInspection >= MILLI2CYCLES(5000)) inspect();
        if (cycle - startCycle - lastHeartbeat >= MILLI2CYCLES(30000)) {
            lastHeartbeat = cycle - startCycle;
            report("running");
        }
    }

    void step(PaidProduction& evidence) {
        evidence.step(); // Ordinary full build, payment, placement and game tick.
        poll();
    }

    void finish(bool ok, const char* reason) {
        inspect();
        ended = true;
        report(ok ? "passed" : (noIncome || blackout || crime || localEmergency ? "censored" : "failed"));
        require(ok, reason);
    }
};

inline void addMatureYard(World& world) {
    auto* yard = static_cast<BuilderBase*>(world.place(Structure_ConstructionYard));
    yard->curUpgradeLev = yard->getMaxUpgradeLevel();
    yard->updateBuildList();
    world.game->gameInitSettings.gameOptions.maximumNumberOfConstructionYardsOverride = 2;
}

inline void secureInitialCityCore(World& world) {
    if (!world.game->isCitySimEnabled()) return;
    // These are labelled, physically placed scenario assets, not paid bot
    // production. A raw count of eight turrets does not establish coverage of
    // an extra yard or every economic asset. Clear that real competing need
    // before a case claims to test an economy-cleared technology start.
    world.bot->planningBuilder = world.yard->getObjectID();
    for (;;) {
        world.bot->cityTurretSearch.reset();
        world.bot->clearPlacementCache();
        int defence = 0;
        const Coord site = world.bot->findCityTurretPlaceLocation(Structure_RocketTurret, &defence);
        if (defence <= 0) break;
        require(site.isValid(), "Initial core has a real coverage need but no legal scenario emplacement");
        world.placeAt(Structure_RocketTurret, site);
    }
}

inline void completeInitialEconomicAssets(World& world) {
    // A technology-start case begins after the finite income/service opening,
    // not by masking its demand. All additions here are labelled scenario
    // assets; later asserted projects still have to be paid by the real bot.
    for (;;) {
        auto pass = world.observePlan();
        if (CityEconomyInvestmentPolicy::processingCapacityNeeded(pass.itemCount[Structure_Refinery],
                pass.itemCount[Unit_Harvester], world.bot->workerAnnualIncome(pass), pass.bayAnnualIncome)) {
            world.place(Structure_Refinery);
            continue;
        }
        if (pass.civicStructuralNeed != 0) {
            const Uint32 civic = pass.civicStructuralNeed & DuneCity::NeedStadium
                ? Structure_Stadium : Structure_Airport;
            require(contentReachable(world, civic),
                    "Mature technology-start setup has an unreachable required civic");
            world.place(civic);
            continue;
        }
        if (world.bot->needsFirstTransport(pass) && pass.carryallBuildAvailable) {
            world.spawn(world.owner, Unit_Carryall, Coord(18 + world.owner->getNumItems(Unit_Carryall), 34));
            continue;
        }
        break;
    }
    if (world.game->isCitySimEnabled()) world.initializePhysicalCity();
}

inline void composition(const char* mod, const char* playerClass, bool queuedControl,
                        bool causalControlsOff) {
    std::string offHash;
    int offCheap = 0;
    for (const bool enabled : {false, true}) {
        World world(mod, playerClass, enabled, Stage::Complete,
                    queuedControl ? Army::TinyTank : Army::TinyMixed, 8, causalControlsOff);
        addMatureYard(world);
        world.closeMilitaryMarket(); // Measured heavy completion must come from its real HF.
        world.cash(19999);
        if (queuedControl) {
            // Labelled initial commitments, not the asserted bot's heavy order.
            for (auto* factory : world.lights) factory->doProduceItem(Unit_Quad);
            for (int i = 0; i < 2; ++i) {
                auto* factory = static_cast<BuilderBase*>(world.place(Structure_LightFactory));
                factory->curUpgradeLev = factory->getMaxUpgradeLevel();
                factory->updateBuildList();
                world.lights.push_back(factory);
            }
        }
        auto* heavy = world.producer(Structure_HeavyFactory);
        bool heavyContent = false;
        const auto& data = world.game->objectData.data;
        for (const Uint32 unit : vehicles)
            if (!light(unit) && data[unit][world.owner->getHouseID()].builder == Structure_HeavyFactory
                && contentReachable(world, unit)) heavyContent = true;
        if (!data[Structure_HeavyFactory][world.owner->getHouseID()].enabled || !heavyContent) {
            notApplicable("composition", mod, playerClass, "no_legal_heavy_factory_combat_content");
            return;
        }
        Case run(world, queuedControl ? "queued_composition" : "owned_composition", mod, playerClass);
        const int queuedBefore = world.queuedLight();
        const int oldQuadsBuiltBefore = world.owner->getNumBuiltItems(Unit_Quad);
        const int existingValue = world.vehicleValue(false);
        const int committedValue = world.vehicleValue(true);
        if (queuedControl)
            require(queuedBefore == 3 && committedValue > existingValue && world.lightValue(false) == 0,
                    "Shared-queue control did not create its three real cheap commitments separately from owned units");
        const int committedLight = world.lightValue(true);
        require(heavy && world.bot->militaryValueLimit >= 1000 * committedValue,
                "Composition control has no mature HF or does not separate its current army from the huge cap");
        auto plan = world.observePlan();
        size_t next = UnitMixPolicy::kSlots;
        Uint32 selected = NONE_ID;
        int selectedBuiltBefore = 0;
        if (enabled) {
            static_assert(vehicles.size() == UnitMixPolicy::kSlots, "Fixture slot order must match the actual unit allocator");
            for (size_t slot = 0; slot < vehicles.size(); ++slot)
                require(plan.compositionValue[slot] == int64_t(world.price(vehicles[slot]))
                            * (world.owner->getNumItems(vehicles[slot]) + world.queued(vehicles[slot])),
                        "Current composition omitted owned units or an initial shared factory commitment");
            require(plan.vehiclePlanValue == committedValue && plan.compositionArmyValue == committedValue
                        && int64_t(committedLight) * 10000 > int64_t(committedValue) * plan.lightVehicleBps,
                    "Factory composition still uses a future funded army instead of owned-plus-queued value");
            next = UnitMixPolicy::nextExistingSlot(plan.unitMix, plan.compositionValue,
                plan.compositionPrice, plan.compositionAvailable);
            require(next < UnitMixPolicy::kOrnithopterSlot && heavy->isAvailableToBuild(vehicles[next])
                        && world.price(vehicles[next]) > std::max({world.price(Unit_Trike),
                            world.price(Unit_RaiderTrike), world.price(Unit_Quad)}),
                    "Overweight cheap share has no genuinely legal higher-value next HF unit");
            selected = vehicles[next];
            selectedBuiltBefore = world.owner->getNumBuiltItems(selected);
            const Uint32 nextFactory = data[selected][world.owner->getHouseID()].builder;
            bool sawLightFactory = false, sawHeavyFactory = false;
            for (const auto& choice : plan.capitalCandidates) {
                if (std::strcmp(choice.kind, "production") != 0) continue;
                sawLightFactory |= choice.item == Structure_LightFactory;
                sawHeavyFactory |= choice.item == Structure_HeavyFactory;
                if (choice.item == Structure_LightFactory || choice.item == Structure_HeavyFactory)
                    require(choice.item == nextFactory || choice.score == 0,
                            "Next-factory admission disagrees with the owned-plus-accepted-queue next combat unit");
            }
            require(sawLightFactory && sawHeavyFactory,
                    "Composition case did not exercise both real ground factory-capacity choices");
        }
        PaidProduction evidence(world);
        world.build();
        evidence.observe();
        const int addedCheap = world.queuedLight() - queuedBefore;
        if (!enabled) {
            offHash = world.configHash;
            offCheap = addedCheap;
            require(addedCheap > 0, "Simple OFF did not reproduce the legacy future-funded cheap-unit quota");
            run.finish(true, "Legacy composition control failed");
        } else {
            require(world.configHash != offHash && addedCheap == 0
                        && heavy->getProductionQueueSize() == 1 && heavy->getCurrentProducedItem() == selected
                        && world.queued(Structure_LightFactory) == 0,
                    "Simple ON replaced the selected legal HF unit with a cheap unit or redundant light factory");
            for (int i = 0; i < 5; ++i) world.build();
            require(world.queuedLight() == queuedBefore,
                    "Repeated/parallel producers minted fresh cheap quotas before any commitment completed");
            if (queuedControl)
                for (size_t i = 0; i < 3; ++i)
                    require(world.lights[i]->getProductionQueueSize() == 1
                                && world.lights[i]->getCurrentProducedItem() == Unit_Quad && !world.lights[i]->isOnHold(),
                            "Composition arbitration cancelled, replaced or paused an existing finite production queue");
            bool paidAtHeavy = false;
            const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
            auto oldQueuesDrained = [&] {
                return !queuedControl || world.owner->getNumBuiltItems(Unit_Quad) >= oldQuadsBuiltBefore + queuedBefore;
            };
            while (world.game->getGameCycleCount() < end
                   && (world.owner->getNumBuiltItems(selected) == selectedBuiltBefore || !oldQueuesDrained())) {
                run.step(evidence);
                paidAtHeavy |= heavy->getCurrentProducedItem() == selected && heavy->getProductionProgress() > 0;
            }
            run.finish(paidAtHeavy && evidence.paidHeavyUnits.count(selected)
                           && world.owner->getNumBuiltItems(selected) > selectedBuiltBefore && oldQueuesDrained()
                           && (!queuedControl || evidence.paidFactoryUnits.count(Unit_Quad)),
                       "Selected HF order or the preserved finite old queue never really paid and completed");
        }
        SDL_Log("SIMPLE_TECH_SAVING_COMPOSITION: mod=%s difficulty=%s enabled=%d queued_control=%d "
                "causal_controls_off=%d existing_value=%d committed_value=%d committed_light=%d "
                "cheap_added=%d legacy_cheap_added=%d selected=%u heavy_completed=%d stateless_policy=1 config_hash=%s",
                mod, playerClass, int(enabled), int(queuedControl), int(causalControlsOff), existingValue,
                committedValue, committedLight, addedCheap, offCheap, selected, evidence.heavyCompleted(),
                world.configHash.c_str());
    }
}

enum class Project { HeavyFactory, HeavyUpgrade, IX, Starport, Palace };
enum class StartCash { Zero, BelowQuote, ExactQuote };

inline void nextLegalInstallments(const char* mod, const char* playerClass, Project goal, StartCash cashMode) {
    const bool upgrade = goal == Project::HeavyUpgrade;
    const Stage stage = goal == Project::HeavyFactory ? Stage::FirstHeavy
        : goal == Project::IX ? Stage::Advanced : goal == Project::Starport ? Stage::FirstStarport
        : goal == Project::Palace ? Stage::FirstPalace : Stage::Complete;
    const Uint32 item = goal == Project::IX ? Structure_IX : goal == Project::Starport ? Structure_StarPort
        : goal == Project::Palace ? Structure_Palace : Structure_HeavyFactory;
    const char* name = upgrade ? "ordinary_heavy_upgrade"
        : goal == Project::HeavyFactory ? "installment_first_heavy" : goal == Project::IX ? "installment_ix"
        : goal == Project::Starport ? "installment_first_starport" : "installment_first_palace";
    World world(mod, playerClass, true, stage);
    if (!world.game->objectData.data[item][world.owner->getHouseID()].enabled || !contentReachable(world, item)) {
        notApplicable(name, mod, playerClass, "content_or_prerequisite_not_enabled_at_actual_tech", item);
        return;
    }
    addMatureYard(world); // One first copy, not an independent project per yard.
    completeInitialEconomicAssets(world);
    secureInitialCityCore(world);
    BuilderBase* heavy = world.producer(Structure_HeavyFactory);
    int beforeLevel = 0;
    int initialQuote = world.price(item);
    if (upgrade) {
        require(heavy, "Upgrade control has no actual heavy factory");
        heavy->curUpgradeLev = 0; // Initial, explicitly unupgraded scenario supplier.
        heavy->updateBuildList();
        const auto& data = world.game->objectData.data;
        bool upgradeContent = false;
        for (Uint32 unit = Unit_FirstID; unit <= ItemID_LastID; ++unit) {
            const auto& spec = data[unit][world.owner->getHouseID()];
            if (isUnit(unit) && spec.builder == Structure_HeavyFactory && spec.upgradeLevel > 0
                && spec.upgradeLevel <= heavy->getMaxUpgradeLevel()
                && QuantBotBuildPolicy::militaryItem(unit) && contentReachable(world, unit)) upgradeContent = true;
        }
        if (!upgradeContent) {
            notApplicable(name, mod, playerClass, "no_legal_heavy_military_upgrade", item);
            return;
        }
        require(heavy->isAllowedToUpgrade(), "Legal initial heavy upgrade is not offered by the actual engine supplier");
        beforeLevel = heavy->getCurrentUpgradeLevel();
        initialQuote = heavy->getUpgradeCost();
    } else {
        require(world.owner->getNumItems(item) == 0 && world.yard->isAvailableToBuild(item),
                "Enabled next-technology control is unexpectedly absent from its mature legal yard catalogue");
        if (item == Structure_StarPort)
            require(world.owner->meetsStarportPopulation(), "First-port project has no genuine legal City population");
    }
    require(initialQuote > 0, "Next-technology control has no real positive quote");
    const int initialCash = cashMode == StartCash::Zero ? 0
        : cashMode == StartCash::ExactQuote ? initialQuote : initialQuote - 1;
    world.cash(initialCash);
    world.bot->militaryValueLimit = 6000;
    Case run(world, name, mod, playerClass);
    auto initial = world.observePlan();
    require(initial.techSavings && initial.techSavingActive && initial.techFundingPriority
                && initial.capitalChoice == initial.techCandidateIndex
                && initial.techCandidate.item == item && initial.techCandidate.cost == initialQuote
                && initial.techUpgrade == upgrade && !initial.techResearch
                && initial.strategicReserveCost == 0 && !initial.militaryEmergency
                && initial.civicStructuralNeed == 0 && initial.civicReserveCost == 0
                && !world.bot->openingWorkersNeeded(initial)
                && (initial.techUpgrade || initial.techCandidate.site.isValid()),
            "Technology-start setup still has an earlier economic, civic, power or defensive capital owner");
    BuilderBase* projectBuilder = nullptr;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getObjectID() == initial.techCandidate.builder)
            projectBuilder = dynamic_cast<BuilderBase*>(structure);
    require(projectBuilder && (!upgrade || projectBuilder == heavy),
            "Selected installment project has no actual matching engine builder");
    const auto receiptsBefore = world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts();
    const int64_t spentBefore = world.spentQ32();
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    const bool startFunded = initialCash >= initialQuote;
    if (upgrade) {
        require(heavy->isUpgrading() == startFunded,
                "Upgrade bypassed the human full-credit gate or demanded an extra reserve above the exact quote");
        require(heavy->getUpgradeProgress() == 0 && world.owner->getCredits() == initialCash,
                "Upgrade start granted progress or spent credits outside ordinary engine installment ticks");
    } else {
        require(world.queued(item) == 1,
                "Full quote, cash reserve or forecast precondition blocked immediate legal structure acceptance");
        require(evidence.paidTechnology.count(item) == 0
                    && world.owner->getCredits() >= 0 && world.owner->getCredits() <= initialCash,
                "New installment structure was granted progress or invented credits at acceptance");
        if (goal == Project::Starport)
            require(world.queued(Structure_IX) == 0 && world.queued(Structure_Palace) == 0,
                    "Discretionary technology bypassed the first legal population-qualified Starport");
    }
    for (int i = 0; i < 5; ++i) world.build();
    require(upgrade ? heavy->isUpgrading() == startFunded : world.queued(item) == 1,
            "Repeated full passes bypassed the upgrade cash gate or duplicated/cancelled its structure project");
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
    auto complete = [&] {
        return upgrade
            ? heavy->getCurrentUpgradeLevel() > beforeLevel && evidence.paidUpgrades.count(heavy->getObjectID()) != 0
            : evidence.completed(item) && world.owner->getNumItems(item) == 1;
    };
    bool observedEnginePayment = false;
    bool upgradeStartedWithRealCash = upgrade && startFunded;
    while (world.game->getGameCycleCount() < end && !complete()) {
        const int cashBefore = world.owner->getCredits();
        const int64_t spentAtTick = world.spentQ32();
        const FixPoint progressBefore = upgrade ? heavy->getUpgradeProgress() : projectBuilder->getProductionProgress();
        const bool upgradeWaiting = upgrade && !heavy->isUpgrading()
            && heavy->getCurrentUpgradeLevel() == beforeLevel;
        const bool buildDue = world.game->getGameCycleCount() % 25 == 0;
        const int upgradeCostBefore = upgradeWaiting ? heavy->getUpgradeCost() : 0;
        run.step(evidence);
        if (upgradeWaiting && heavy->isUpgrading()) {
            require(buildDue && cashBefore >= upgradeCostBefore,
                    "Needed upgrade began without the same real full-credit gate as a human");
            upgradeStartedWithRealCash = true;
        }
        const bool paid = upgrade ? evidence.paidUpgrades.count(heavy->getObjectID()) != 0
            : evidence.paidTechnology.count(item) != 0;
        const FixPoint progressAfter = upgrade ? heavy->getUpgradeProgress() : projectBuilder->getProductionProgress();
        observedEnginePayment |= paid && (progressAfter > progressBefore || complete())
            && world.spentQ32() > spentAtTick;
        for (const Uint32 unique : {Structure_IX, Structure_Palace, Structure_StarPort})
            require(world.owner->getNumItems(unique) + world.queued(unique) <= 1,
                    "Parallel yards duplicated a one-off or first Starport during installment construction");
        if (goal == Project::HeavyFactory && world.owner->getNumItems(item) == 0)
            require(world.queued(item) <= 1, "Parallel yards independently started the same first heavy factory");
        if ((goal == Project::HeavyFactory || goal == Project::IX) && !complete())
            require(world.queued(Structure_LightFactory) == 0,
                    "A buildable first HF or IX was replaced by discretionary light-factory filler");
        if (goal == Project::Starport && world.owner->getNumItems(item) == 0)
            require(world.queued(Structure_IX) == 0 && world.queued(Structure_Palace) == 0,
                    "Discretionary technology jumped ahead of the still-pending first Starport");
    }
    const int64_t spent = world.spentQ32() - spentBefore;
    run.finish(complete() && observedEnginePayment && spent >= FixPoint(initialQuote).getRawValue()
                   && (!upgrade || upgradeStartedWithRealCash)
                   && world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > receiptsBefore,
               "Selected project did not receive real engine-paid installments and bounded income-funded completion");
    SDL_Log("SIMPLE_TECH_SAVING_INSTALLMENTS: case=%s mod=%s difficulty=%s item=%u quote=%d initial_cash=%d "
            "zero_cash=%d exact_quote=%d upgrade_human_gate=%d parallel_yards=2 real_receipts=1 "
            "engine_payment=1 spent_total_q32=%lld paid_completed=1 stateless_policy=1",
            name, mod, playerClass, item, initial.techCandidate.cost, initialCash,
            int(cashMode == StartCash::Zero), int(cashMode == StartCash::ExactQuote), int(upgrade),
            static_cast<long long>(spent));
}

inline void boundedProgression(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::Progression);
    completeInitialEconomicAssets(world);
    secureInitialCityCore(world);
    std::set<Uint32> required;
    for (const Uint32 item : {Structure_HeavyFactory, Structure_RepairYard, Structure_HighTechFactory,
                             Structure_IX, Structure_Palace})
        if (contentReachable(world, item)) required.insert(item);
        else notApplicable("bounded_progression_goal", mod, playerClass,
                           "content_or_prerequisite_not_enabled_at_actual_tech", item);
    if (!required.count(Structure_HeavyFactory)) {
        notApplicable("bounded_progression", mod, playerClass, "no_reachable_heavy_factory");
        return;
    }
    require(world.yard->isAvailableToBuild(Structure_HeavyFactory) && world.owner->hasPower()
                && world.bot->ownGroundArmyValue() >= world.bot->minWaveValue(),
            "Progression control lacks its legal first HF, power or real dispatch-capable army");
    world.cash(0);
    world.bot->militaryValueLimit = 6000;
    Case run(world, "bounded_progression", mod, playerClass);
    PaidProduction evidence(world);
    const auto receiptsBefore = world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts();
    world.build();
    evidence.observe();
    require(world.queued(Structure_HeavyFactory) == 1,
            "Zero-cash bounded progression did not immediately start its legal first HF on installments");
    bool upgradeRequired = false;
    const auto& data = world.game->objectData.data;
    for (Uint32 unit = Unit_FirstID; unit <= ItemID_LastID; ++unit) {
        const auto& spec = data[unit][world.owner->getHouseID()];
        if (isUnit(unit) && spec.builder == Structure_HeavyFactory && spec.upgradeLevel > 0
            && QuantBotBuildPolicy::militaryItem(unit) && contentReachable(world, unit)) upgradeRequired = true;
    }
    auto complete = [&] {
        for (const Uint32 item : required) if (!evidence.completed(item)) return false;
        return evidence.factoryUnitCompleted() && evidence.heavyCompleted() > 0
            && (!upgradeRequired || !evidence.paidUpgrades.empty());
    };
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(480000);
    while (world.game->getGameCycleCount() < end && !complete()) {
        run.step(evidence);
        for (const Uint32 item : required)
            if (world.owner->getNumItems(item) == 0 || item == Structure_IX || item == Structure_Palace)
                require(world.owner->getNumItems(item) + world.queued(item) <= 1,
                        "Progression duplicated a pending first supplier or one-off technology");
    }
    run.finish(complete() && world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > receiptsBefore,
               "Real income failed to pay and place the reachable bounded technology chain and a heavy factory unit");
}

inline void exactOpeningWorker(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::FirstHeavy, Army::ReadyLight, 0);
    auto* port = world.producer(Structure_StarPort);
    require(port && port->isAvailableToBuild(Unit_Harvester), "Exact worker control has no genuine import supplier");
    world.owner->getChoam().setNumAvailable(Unit_Harvester, 2);
    const auto* offer = port->getBuildItem(Unit_Harvester);
    require(offer && offer->price > 0 && world.owner->getNumHarvesters() == 0,
            "Opening worker control has no positive actual market quote or already owns a worker");
    const int price = int(offer->price);
    world.cash(price);
    Case run(world, "exact_opening_worker", mod, playerClass);
    auto plan = world.observePlan();
    require(world.bot->openingWorkersNeeded(plan) && !plan.techFundingPriority,
            "Discretionary technology retained the capital needed by a real missing opening worker");
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(world.queued(Unit_Harvester) == 1 && world.owner->getCredits() == 0,
            "Simple reserve starved or overcharged the first worker's exact paid import budget");
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(120000);
    while (world.game->getGameCycleCount() < end && world.owner->getNumHarvesters() == 0) run.step(evidence);
    run.finish(world.owner->getNumHarvesters() > 0,
               "Exactly paid opening-worker import never delivered a real worker through ordinary engine cycles");
    SDL_Log("SIMPLE_TECH_SAVING_OPENING_WORKER: mod=%s difficulty=%s exact_quote=%d paid=1 delivered=1 stateless_policy=1",
            mod, playerClass, price);
}

enum class EconomicNeed { Refinery, IndustrialSeed, Stadium };

inline void economicPriority(const char* mod, const char* playerClass, EconomicNeed need) {
    World world(mod, playerClass, true, Stage::Advanced);
    const bool city = need != EconomicNeed::Refinery;
    const Uint32 item = need == EconomicNeed::Refinery ? Structure_Refinery
        : need == EconomicNeed::IndustrialSeed ? Structure_ZoneIndustrial : Structure_Stadium;
    const char* name = need == EconomicNeed::Refinery ? "needed_refinery_before_tech"
        : need == EconomicNeed::IndustrialSeed ? "needed_industrial_seed_before_tech" : "needed_stadium_before_tech";
    if ((city && !world.game->isCitySimEnabled()) || !contentReachable(world, item)) {
        notApplicable(name, mod, playerClass, city && !world.game->isCitySimEnabled()
            ? "not_city_content" : "content_or_prerequisite_not_enabled_at_actual_tech", item);
        return;
    }
    if (need != EconomicNeed::Refinery) completeInitialEconomicAssets(world);
    // Remove only labelled initial assets to create the actual economic
    // shortfall. The bot's rules, demand flags and research defaults stay intact.
    std::vector<StructureBase*> initial;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getItemID() == item)
            initial.push_back(structure);
    const size_t keep = need == EconomicNeed::Refinery ? 1 : 0;
    require(initial.size() > keep, "Economic priority setup has no initial asset to withhold");
    for (size_t i = keep; i < initial.size(); ++i) {
        initial[i]->setVisible(VIS_ALL, false);
        initial[i]->destroy();
    }
    if (need == EconomicNeed::Stadium) {
        for (;;) {
            for (auto* structure : structureList)
                if (structure->getOwner() == world.owner && structure->getItemID() == Structure_ZoneResidential)
                    static_cast<ZoneStructure*>(structure)->setResidentialPopulation(
                        DuneCity::ResidentialPopulation::fromDensity(3));
            world.initializePhysicalCity();
            if (world.observePlan().civicStructuralNeed & DuneCity::NeedStadium) break;
            world.place(Structure_ZoneResidential);
        }
    } else if (world.game->isCitySimEnabled()) {
        world.initializePhysicalCity();
    }
    secureInitialCityCore(world);
    world.cash(world.price(item));
    world.bot->militaryValueLimit = 6000;
    Case run(world, name, mod, playerClass);
    auto plan = world.observePlan();
    require(world.owner->hasPower() && !plan.militaryEmergency,
            "Economic priority setup accidentally measures blackout or a live enemy instead");
    if (need == EconomicNeed::Refinery)
        require(CityEconomyInvestmentPolicy::processingCapacityNeeded(plan.itemCount[Structure_Refinery],
                    plan.itemCount[Unit_Harvester], world.bot->workerAnnualIncome(plan), plan.bayAnnualIncome),
                "Withheld refinery did not create a real processing-capacity shortfall");
    else if (need == EconomicNeed::IndustrialSeed)
        require(plan.itemCount[item] == 0 && world.bot->chooseCityZone(plan, world.yard, true, true) == item,
                "Missing industrial seed is not a real legal economic choice");
    else
        require((plan.civicStructuralNeed & DuneCity::NeedStadium) && plan.civicReserveCost > 0
                    && world.yard->isAvailableToBuild(item),
                "Physical residential population did not create a legal demanded Stadium");
    require(!plan.techFundingPriority,
            "A merely feasible discretionary project took this pass from its earlier needed economy or service");
    const int before = world.owner->getNumBuiltItems(item);
    const int64_t spentBefore = world.spentQ32();
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(world.queued(item) == 1 && world.queued(Structure_IX) == 0 && world.queued(Structure_Palace) == 0,
            "Discretionary technology displaced the actual demanded economic or civic order");
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
    while (world.game->getGameCycleCount() < end && world.owner->getNumBuiltItems(item) == before)
        run.step(evidence);
    run.finish(evidence.completed(item) && evidence.paidTechnology.count(item)
                   && world.spentQ32() - spentBefore >= FixPoint(world.price(item)).getRawValue(),
               "Protected economic or civic order did not really pay, complete and get placed");
}

inline void realPowerRecovery(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::Advanced);
    if (!world.owner->isPowerRequired()) {
        notApplicable("power_recovery", mod, playerClass, "actual_options_do_not_require_power");
        return;
    }
    std::vector<StructureBase*> generators;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getItemID() == Structure_WindTrap)
            generators.push_back(structure);
    require(world.owner->hasPower(), "Power control did not start from a genuinely powered base");
    for (auto* generator : generators) {
        if (!world.owner->hasPower()) break;
        generator->setVisible(VIS_ALL, false);
        generator->destroy();
    }
    require(!world.owner->hasPower(), "Power control did not actually cause a generation deficit");
    world.cash(world.price(Structure_WindTrap));
    Case run(world, "power_recovery", mod, playerClass);
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(world.queued(Structure_WindTrap) + world.queued(Structure_NuclearPlant) > 0
                && world.queued(Structure_IX) == 0 && world.queued(Structure_Palace) == 0,
            "Optional technology displaced actual blackout recovery");
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
    auto complete = [&] {
        return world.owner->hasPower()
            && (evidence.completed(Structure_WindTrap) || evidence.completed(Structure_NuclearPlant));
    };
    while (world.game->getGameCycleCount() < end && !complete()) run.step(evidence);
    run.finish(complete(), "Protected generation was not really paid, placed and sufficient to recover actual power");
}

inline void defensiveReinforcement(const char* mod, const char* playerClass, bool distantArmy) {
    World world(mod, playerClass, true, Stage::FirstHeavy,
                distantArmy ? Army::ReadyLight : Army::Empty, 8, false, HOUSE_HARKONNEN, 3,
                distantArmy ? 0 : -1);
    constexpr int distantDefenders = 60;
    world.closeMilitaryMarket(); // No heavy import can substitute for the measured local LF lane.
    if (distantArmy) {
        StructureBase* asset = nullptr;
        for (auto* structure : structureList)
            if (structure->getOwner() == world.owner && structure->getItemID() == Structure_Refinery) {
                asset = structure;
                break;
            }
        require(asset, "Local-defence control has no real threatened economic asset");
        const Coord center = asset->getLocation();
        const auto attackers = TechProductionProbe::spawnGroundBand(world, world.enemy, Unit_Tank, 18, center, 1, 4);
        for (auto* tank : attackers)
            Command(world.pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, tank->getObjectID(),
                    asset->getObjectID()).executeCommand();
        const auto defenders = TechProductionProbe::spawnGroundBand(world, world.owner, Unit_SiegeTank,
            distantDefenders, center, 25, 64);
        for (const auto* defender : defenders)
            require(blockDistance(defender->getLocation(), center) > 24,
                    "Distant-army control accidentally supplied local mobile cover");
        world.bot->updateEnemySightings();
    } else {
        // A labelled initial yard commitment leaves only the affordable local
        // combat lane ready; it is not an asserted technology order.
        world.yard->doProduceItem(Structure_WindTrap);
    }
    world.cash(3000);
    Case run(world, distantArmy ? "unmet_local_defence" : "opening_combat_lane", mod, playerClass);
    const auto plan = world.observePlan();
    const auto ready = TechProductionProbe::actualGroundReadiness(world);
    require(!plan.advancedGroundAvailable,
            "Local-light control unexpectedly has an available advanced ground production choice");
    if (distantArmy)
        require(plan.defendingEconomy && plan.militaryEmergency && !plan.techFundingPriority
                    && ready.first >= distantDefenders && ready.second > 0,
                "A large distant army hid the actual threatened economy or retained discretionary technology funding");
    else
        require(ready.first == 0, "Opening-combat control unexpectedly owns a ready ground army");
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(world.queuedLight() > 0, "No-advanced-supplier control lost its affordable local combat lane");
    bool paid = false;
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(10000);
    while (world.game->getGameCycleCount() < end && !paid) {
        run.step(evidence);
        for (const auto* factory : world.lights)
            paid |= light(factory->getCurrentProducedItem()) && factory->getProductionProgress() > 0;
    }
    run.finish(paid, "Protected local defensive reinforcement never received ordinary factory payment");
}

inline void freshCity(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) {
        notApplicable("fresh_city", mod, playerClass, "not_city_content");
        return;
    }
    World world(mod, playerClass, true, Stage::NaturalOpening, Army::Empty, 0);
    Case run(world, "fresh_city", mod, playerClass);
    PaidProduction evidence(world);
    const int minimum = std::max(3000, world.bot->minWaveValue());
    bool supplierBelowFloor = false, supplierWorkerPower = false;
    bool reachedFloor = false, dispatched = false;
    int lotsAtSupplier = -1;
    Uint32 floorCycle = 0;
    const bool requireHeavy = contentReachable(world, Structure_HeavyFactory);
    const bool requirePort = contentReachable(world, Structure_StarPort);
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(1800000);
    auto complete = [&] {
        return supplierBelowFloor && supplierWorkerPower && reachedFloor && dispatched
            && evidence.factoryUnitCompleted() && TechProductionProbe::builtCityLots(world) > lotsAtSupplier
            && (!requireHeavy || (evidence.completed(Structure_HeavyFactory) && evidence.heavyCompleted() > 0))
            && (!requirePort || evidence.completed(Structure_StarPort))
            && world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > 0;
    };
    while (world.game->getGameCycleCount() < end && !complete()) {
        run.step(evidence);
        const auto ready = TechProductionProbe::actualGroundReadiness(world);
        if (lotsAtSupplier < 0 && (evidence.paidTechnology.count(Structure_LightFactory)
                                  || evidence.paidTechnology.count(Structure_HeavyFactory))) {
            supplierBelowFloor = ready.first < 6 || ready.second < minimum;
            supplierWorkerPower = world.owner->getNumHarvesters() > 0 && world.owner->hasPower();
            lotsAtSupplier = TechProductionProbe::builtCityLots(world);
        }
        if (!reachedFloor && ready.first >= 6 && ready.second >= minimum) {
            reachedFloor = true;
            floorCycle = world.game->getGameCycleCount();
        }
        if (reachedFloor && !dispatched && world.game->getGameCycleCount() % 50 == 0) {
            world.bot->attack(world.bot->militaryUnitValue());
            dispatched = world.bot->customWave.launched != 0 && !world.bot->customWave.members.empty();
        }
        if (reachedFloor && world.game->getGameCycleCount() - floorCycle > MILLI2CYCLES(90000))
            require(dispatched, "Simple technology reservation indefinitely leased a real dispatch-capable opening army");
    }
    SDL_Log("SIMPLE_TECH_SAVING_FRESH_CITY_EVIDENCE: mod=%s difficulty=%s supplier_below_floor=%d "
            "supplier_worker_power=%d ready_floor=%d dispatched=%d paid_factory_unit=%d heavy_required=%d "
            "heavy_complete=%d starport_required=%d starport_complete=%d lots_at_supplier=%d lots_now=%d",
            mod, playerClass, int(supplierBelowFloor), int(supplierWorkerPower), int(reachedFloor), int(dispatched),
            int(evidence.factoryUnitCompleted()), int(requireHeavy), int(evidence.completed(Structure_HeavyFactory)),
            int(requirePort), int(evidence.completed(Structure_StarPort)), lotsAtSupplier,
            TechProductionProbe::builtCityLots(world));
    run.finish(complete(), "CY-only City opening did not pay its supplier below the floor, build a real army and continue economy/technology");
}

inline int firstCombatTech(const World& world) {
    const auto& data = world.game->objectData.data;
    const int house = world.owner->getHouseID();
    std::array<int, Num_ItemID> levels;
    levels.fill(-1);
    auto level = [&](auto&& self, Uint32 item) -> int {
        int& result = levels[item];
        if (result >= 0) return result;
        result = INT_MAX;
        if (isStructure(item) && world.owner->getNumItems(item) > 0) return result = 0;
        const auto& spec = data[item][house];
        if (!spec.enabled || spec.price <= 0) return result;
        int required = spec.techLevel;
        for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
            if (isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite])
                required = std::max(required, self(self, prerequisite));
        return result = required;
    };
    int result = INT_MAX;
    for (const Uint32 unit : vehicles) {
        const int supplier = data[unit][house].builder;
        if (supplier != Structure_LightFactory && supplier != Structure_HeavyFactory) continue;
        result = std::min(result, std::max(level(level, unit), level(level, Uint32(supplier))));
    }
    return result;
}

inline void actualT1Research(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) {
        notApplicable("actual_t1_research", mod, playerClass, "not_city_content");
        return;
    }
    World world(mod, playerClass, true, Stage::NaturalResearch, Army::Empty, 0);
    const int ceiling = firstCombatTech(world);
    const auto& labSpec = world.game->objectData.data[Structure_ResearchLab][world.owner->getHouseID()];
    if (ceiling <= 1 || ceiling > 9 || !labSpec.enabled || labSpec.techLevel > 1) {
        notApplicable("actual_t1_research", mod, playerClass,
                      ceiling <= 1 ? "first_supplier_needs_no_research" : "no_reachable_research_supplier_chain");
        return;
    }
    Case run(world, "actual_t1_research", mod, playerClass);
    PaidProduction evidence(world);
    std::map<int, Uint32> started;
    std::set<int> completed;
    std::set<int> expected;
    for (int target = 2; target <= ceiling; ++target) expected.insert(target);
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(1200000);
    while (world.game->getGameCycleCount() < end && !evidence.factoryUnitCompleted()) {
        if (world.game->getGameCycleCount() % 25 == 0) {
            const bool active = world.owner->isTechResearchActive();
            const int cost = world.owner->getTechResearchCost();
            const int cash = world.owner->getCredits();
            world.build();
            if (!active && world.owner->isTechResearchActive()) {
                const int target = world.owner->getTechResearchTarget();
                require(expected.count(target) && started.count(target) == 0,
                        "First-supplier research was repeated or leaked into optional technology before the real army floor");
                require(cost > 0 && world.owner->getCredits() <= cash - cost
                            && world.owner->getTechResearchRemaining() == world.owner->techResearchCycles(target),
                        "T1 research did not pay its full actual cash quote and begin its ordinary engine duration");
                started.emplace(target, world.game->getGameCycleCount());
            }
        }
        evidence.observe();
        world.tick();
        evidence.observe();
        run.poll();
        for (const auto& research : started)
            if (world.owner->getTechLevel() >= research.first) {
                require(world.game->getGameCycleCount() - research.second + 1
                            >= Uint32(world.owner->techResearchCycles(research.first)),
                        "First-supplier research completed without the real normal-pace engine duration");
                completed.insert(research.first);
            }
    }
    const auto ready = TechProductionProbe::actualGroundReadiness(world);
    SDL_Log("SIMPLE_TECH_SAVING_ACTUAL_T1_EVIDENCE: mod=%s difficulty=%s initial_tech=1 "
            "initial_structures=1 initial_units=0 ceiling=%d studies_started=%zu studies_completed=%zu "
            "lab_completed=%d LF_completed=%d HF_completed=%d paid_factory_unit=%d ready_units=%d ready_value=%d",
            mod, playerClass, ceiling, started.size(), completed.size(), int(evidence.completed(Structure_ResearchLab)),
            int(evidence.completed(Structure_LightFactory)), int(evidence.completed(Structure_HeavyFactory)),
            int(evidence.factoryUnitCompleted()), ready.first, ready.second);
    run.finish(evidence.completed(Structure_ResearchLab) && completed == expected
                   && world.owner->getTechLevel() == ceiling
                   && (evidence.completed(Structure_LightFactory) || evidence.completed(Structure_HeavyFactory))
                   && evidence.factoryUnitCompleted()
                   && (ready.first < 6 || ready.second < std::max(3000, world.bot->minWaveValue())),
               "Actual T1 City opening did not pay/place its lab, complete real studies and pay/complete its first factory unit below the army floor");
}

} // namespace SimpleTechSavingProbe

void runSimpleTechSavingProbe(const char* mod, const char* playerClass) {
    struct RestoreConfig {
        QuantBotConfig saved = getQuantBotConfig();
        ~RestoreConfig() { getQuantBotConfig() = saved; }
    } restore;
    const auto start = std::chrono::steady_clock::now();
    {
        SimpleTechSavingProbe::World world(mod, playerClass, true, SimpleTechSavingProbe::Stage::Complete);
        TechProductionProbe::statelessSaveRoundTrips(world);
    }
    for (const bool causalControlsOff : {false, true}) {
        SimpleTechSavingProbe::composition(mod, playerClass, false, causalControlsOff);
        SimpleTechSavingProbe::composition(mod, playerClass, true, causalControlsOff);
    }
    for (const auto goal : {SimpleTechSavingProbe::Project::HeavyFactory, SimpleTechSavingProbe::Project::HeavyUpgrade,
                            SimpleTechSavingProbe::Project::IX, SimpleTechSavingProbe::Project::Starport,
                            SimpleTechSavingProbe::Project::Palace}) {
        for (const auto cash : {SimpleTechSavingProbe::StartCash::Zero, SimpleTechSavingProbe::StartCash::BelowQuote})
            SimpleTechSavingProbe::nextLegalInstallments(mod, playerClass, goal, cash);
        if (goal == SimpleTechSavingProbe::Project::HeavyUpgrade)
            SimpleTechSavingProbe::nextLegalInstallments(mod, playerClass, goal, SimpleTechSavingProbe::StartCash::ExactQuote);
    }
    SimpleTechSavingProbe::boundedProgression(mod, playerClass);
    SimpleTechSavingProbe::exactOpeningWorker(mod, playerClass);
    for (const auto need : {SimpleTechSavingProbe::EconomicNeed::Refinery,
                            SimpleTechSavingProbe::EconomicNeed::IndustrialSeed,
                            SimpleTechSavingProbe::EconomicNeed::Stadium})
        SimpleTechSavingProbe::economicPriority(mod, playerClass, need);
    SimpleTechSavingProbe::realPowerRecovery(mod, playerClass);
    SimpleTechSavingProbe::defensiveReinforcement(mod, playerClass, false);
    SimpleTechSavingProbe::defensiveReinforcement(mod, playerClass, true);
    SimpleTechSavingProbe::freshCity(mod, playerClass);
    SimpleTechSavingProbe::actualT1Research(mod, playerClass);
    const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=simple_tech_saving outcome=passed mod=%s difficulty=%s "
            "producer_scope=full_build paid_progress=1 paired_off=1 queued_composition=1 "
            "single_legal_project=1 structure_installments=1 ordinary_upgrade_gate=1 stateless_policy=1 causal_controls_off=1 wall_ms=%lld",
            mod, playerClass, static_cast<long long>(wall));
}
