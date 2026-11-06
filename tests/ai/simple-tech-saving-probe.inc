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
            // Reused ordinary candidates include amortized power in cost for
            // ROI ranking. Their actual payable quote is price + foundation,
            // exactly as capitalReserve and the technology order account it.
            require(choice.builder == pass.techCandidate.builder && choice.item == pass.techCandidate.item
                        && choice.price == pass.techCandidate.price
                        && choice.foundationCost == pass.techCandidate.foundationCost
                        && int64_t(choice.price) + choice.foundationCost == pass.techCandidate.cost,
                    "Simple project changed owner, item or physical quote in the shared capital planner");
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
    for (;;) {
        world.bot->planningBuilder = world.yard->getObjectID();
        world.bot->cityTurretSearch.reset();
        world.bot->clearPlacementCache();
        int defence = 0;
        const Coord site = world.bot->findCityTurretPlaceLocation(Structure_RocketTurret, &defence);
        if (defence > 0) {
            require(site.isValid(), "Initial core has a real coverage need but no legal scenario emplacement");
            world.placeAt(Structure_RocketTurret, site);
            continue;
        }
        // Cover consumes power too. Clear the ordinary city's real reserve,
        // not merely its blackout threshold, before optional tech is measured.
        // Loop back so each added generator receives its actual spatial cover.
        const auto pass = world.observePlan();
        const int produced = world.owner->getProducedPower();
        if (produced - world.owner->getPowerRequirement() >= pass.cityPowerReserve) break;
        world.place(Structure_WindTrap);
        require(world.owner->getProducedPower() > produced,
                "Initial city Windtrap did not provide real generation for the demanded power reserve");
    }
}

inline void completeInitialEconomicAssets(World& world) {
    // A technology-start case begins after the finite income/service opening,
    // not by masking its demand. All additions here are labelled scenario
    // assets; later asserted projects still have to be paid by the real bot.
    for (;;) {
        auto pass = world.observePlan();
        // The ordinary non-city low-cash opening still asks for four bays
        // before optional technology, even if three cover current throughput.
        const bool classicOpening = !world.game->isCitySimEnabled()
            && pass.itemCount[Structure_Refinery] < 4;
        if (classicOpening || CityEconomyInvestmentPolicy::processingCapacityNeeded(pass.itemCount[Structure_Refinery],
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
                        bool causalControlsOff, bool unitChoiceOnly = false) {
    std::string offHash;
    int offCheap = 0;
    for (const bool enabled : {false, true}) {
        World world(mod, playerClass, enabled && !unitChoiceOnly, Stage::Complete,
                    queuedControl ? Army::TinyTank : Army::TinyMixed, 8, causalControlsOff);
        if (unitChoiceOnly) {
            auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
            config->force.existingCompositionUnitChoice = enabled;
            world.bot->configOverride = config;
            world.configHash = config->getConfigHash();
        }
        addMatureYard(world);
        world.closeMilitaryMarket(); // Measured heavy completion must come from its real HF.
        if (unitChoiceOnly) {
            // Satisfy the unchanged legacy wealth/capacity target with real
            // initial yards. Lower cash also closes the OFF light-budget gate
            // and therefore cannot establish the intended cheap-order control.
            addMatureYard(world);
            addMatureYard(world);
        }
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
        Case run(world, unitChoiceOnly ? (queuedControl ? "unit_choice_queued" : "unit_choice_owned")
            : queuedControl ? "queued_composition" : "owned_composition", mod, playerClass);
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
        if (unitChoiceOnly)
            require(!plan.techSavings && !plan.techSavingActive && !plan.techFundingPriority
                        && !plan.techProjectPending && plan.existingCompositionChoice == enabled,
                    "Standalone unit choice enabled technology savings or installment admission");
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
            next = plan.nextCompositionSlot();
            const bool selectedHeavySupplier = unitChoiceOnly
                ? next < UnitMixPolicy::kSlots && plan.compositionFactoryAvailable[1][next]
                : next < UnitMixPolicy::kOrnithopterSlot && heavy->isAvailableToBuild(vehicles[next]);
            const int selectedPrice = next < UnitMixPolicy::kSlots
                ? (unitChoiceOnly ? int(plan.compositionPrice[next]) : world.price(vehicles[next])) : 0;
            require(next < UnitMixPolicy::kOrnithopterSlot && selectedHeavySupplier
                        && selectedPrice > std::max({world.price(Unit_Trike),
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
                    require((unitChoiceOnly ? plan.compositionFactoryAdmits(choice.item) : choice.item == nextFactory)
                                || choice.score == 0,
                            "Next-factory admission disagrees with the owned-plus-accepted-queue next combat unit");
            }
            require(sawLightFactory && sawHeavyFactory,
                    "Composition case did not exercise both real ground factory-capacity choices");
        }
        PaidProduction evidence(world);
        world.build();
        if (enabled && unitChoiceOnly && heavy->getProductionQueueSize() == 1) {
            // A real mapped variant may fulfill the same selected family.
            selected = heavy->getCurrentProducedItem();
            selectedBuiltBefore = world.owner->getNumBuiltItems(selected);
            require(QuantBot::compositionSlot(selected, true) == next,
                    "Standalone HF accepted a custom unit outside the selected family");
        }
        evidence.observe();
        const int addedCheap = world.queuedLight() - queuedBefore;
        if (!enabled) {
            offHash = world.configHash;
            offCheap = addedCheap;
            require(addedCheap > 0, "Simple OFF did not reproduce the legacy future-funded cheap-unit quota");
            run.finish(true, "Legacy composition control failed");
        } else {
            require(world.configHash != offHash, "Composition switch did not change the configuration hash");
            require(unitChoiceOnly || addedCheap == 0,
                    "Simple ON replaced the selected legal HF unit with a cheap unit");
            if (unitChoiceOnly) {
                // An overweight light group can still contain an absent, underweight
                // family. Local factories may fill that deficit once, not mint a
                // fresh quota for a family that is already at or above its target.
                require(addedCheap >= 0, "Local composition choice cancelled an existing cheap commitment");
                for (size_t slot = UnitMixPolicy::kFirstLightSlot; slot < vehicles.size(); ++slot) {
                    const int64_t price = world.price(vehicles[slot]);
                    const int64_t after = price
                        * (world.owner->getNumItems(vehicles[slot]) + world.queued(vehicles[slot]));
                    const int64_t added = after - plan.compositionValue[slot];
                    const bool underweight = int64_t(plan.unitMix[slot]) * committedValue
                        > plan.compositionValue[slot] * 10000;
                    require(added >= 0 && (underweight ? added <= price : added == 0),
                            "Local LF bought an overweight family or more than one indivisible deficit order");
                }
            }
            require(heavy->getProductionQueueSize() == 1 && heavy->getCurrentProducedItem() == selected,
                    "Composition choice did not queue the selected legal HF family");
            require(world.queued(Structure_LightFactory) == 0,
                    "Composition choice bought redundant LF capacity for the overweight light group");
            const int queuedAfter = world.queuedLight();
            for (int i = 0; i < 5; ++i) world.build();
            require(world.queuedLight() == queuedAfter,
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

inline void standaloneStarport(const char* mod, const char* playerClass) {
    World world(mod, playerClass, false, Stage::FirstHeavy, Army::TinyTank);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.existingCompositionUnitChoice = false;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
    std::vector<StructureBase*> localFactories;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner
            && (structure->getItemID() == Structure_LightFactory
                || structure->getItemID() == Structure_HighTechFactory))
            localFactories.push_back(structure);
    for (auto* factory : localFactories) {
        factory->setVisible(VIS_ALL, false);
        factory->destroy();
    }
    for (auto& offer : world.owner->getChoam().availableItems)
        if (std::find(vehicles.begin(), vehicles.end(), offer.itemID) != vehicles.end())
            offer.price = world.price(offer.itemID) * 3 / 4;
    world.owner->updateBuildLists();
    world.cash(19999);
    const auto offPlan = world.observePlan();
    require(!offPlan.usesCompositionChoice()
                && std::all_of(offPlan.unitMix.begin(), offPlan.unitMix.end(), [](int share) { return share == 0; }),
            "Telemetry changed the legacy factory-only target mask while both selection switches were off");
    config->force.existingCompositionUnitChoice = true;
    world.configHash = config->getConfigHash();
    Case run(world, "unit_choice_starport_only", mod, playerClass);
    auto plan = world.observePlan();
    const auto next = plan.nextCompositionSlot();
    auto* port = world.producer(Structure_StarPort);
    require(!plan.techSavings && plan.existingCompositionChoice
                && !world.producer(Structure_HeavyFactory)
                && !world.producer(Structure_LightFactory)
                && !world.producer(Structure_HighTechFactory)
                && port && next < vehicles.size()
                && plan.unitMix[next] > 0 && plan.compositionAvailable[next]
                && port->isAvailableToBuild(vehicles[next]),
            "Standalone import-only supplier has no positive available composition target");
    const int queuedBefore = world.queued(vehicles[next]);
    world.build();
    run.finish(world.queued(vehicles[next]) > queuedBefore,
               "Standalone composition refused its selected real Starport bargain without a local factory");
}

// Real authored Tornie offers, not synthetic ObjectData replacements. Canonical
// learning inputs stay unchanged; these controls concern suppliers and value only.
inline void standaloneVariantAccounting(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isTornieFamilyContentActive()) return;
    constexpr std::array<std::pair<Uint32, size_t>, 12> variants{{
        {Unit_RocketTrike, 7}, {Unit_FlameTank, 0}, {Unit_EliteLauncher, 2}, {Unit_EliteSiegeTank, 1},
        {Unit_SonicTrike, 7}, {Unit_ChemicalSiegeTank, 1}, {Unit_FlameQuad, 9},
        {Unit_EliteOrnithopter, 6}, {Unit_RaiderQuad, 9}, {Unit_RangedTank, 0},
        {Unit_SlowTank, 0}, {Unit_TameSandworm, 3}
    }};
    std::array<bool, variants.size()> covered{};
    for (const auto& [item, slot] : variants)
        require(QuantBot::compositionSlot(item, true) == slot, "A real mobile combat variant has no mix family");
    for (const Uint32 item : {Unit_Soldier, Unit_Infantry, Unit_Trooper, Unit_Troopers,
            Unit_Troopers5, Unit_Infantry5, Unit_GasInfantry, Unit_FlameTrooper,
            Unit_Harvester, Unit_RebelHarvester, Unit_CapacityHarvester, Unit_FastHarvester,
            Unit_Carryall, Unit_ChemicalCarryall, Unit_Frigate, Unit_MCV, Unit_Saboteur,
            Unit_Sandworm, Unit_AmbientAirplane, Unit_AmbientHelicopter})
        require(QuantBot::compositionSlot(item, true) == UnitMixPolicy::kSlots,
                "Standalone composition captured an infantry, economy, transport, saboteur or native-worm exemption");
    for (const HOUSETYPE side : {HOUSE_HARKONNEN, HOUSE_ATREIDES, HOUSE_ORDOS, HOUSE_NEUTRAL,
            HOUSE_REBELS, HOUSE_CUSTOM, HOUSE_WILDSPADE, HOUSE_KLESHMERSH}) {
        World world(mod, playerClass, false, Stage::Complete, Army::Empty, 8, true, side);
        world.closeMilitaryMarket();
        const int house = world.owner->getHouseID();
        if (world.game->objectData.data[Structure_Worfinery][house].enabled
            && contentReachable(world, Structure_Worfinery)) {
            auto* combo = static_cast<BuilderBase*>(world.place(Structure_Worfinery));
            combo->curUpgradeLev = combo->getMaxUpgradeLevel();
            combo->updateBuildList();
        }
        UnitMixPolicy::Weights expected{}, legacy{};
        int count = 0;
        for (size_t i = 0; i < variants.size(); ++i) {
            const auto [item, slot] = variants[i];
            BuilderBase* supplier = nullptr;
            for (auto* structure : structureList) {
                auto* builder = dynamic_cast<BuilderBase*>(structure);
                if (builder && builder->getOwner() == world.owner && builder->isAvailableToBuild(item)) {
                    supplier = builder;
                    break;
                }
            }
            if (!supplier) continue;
            covered[i] = true;
            world.spawn(world.owner, item, Coord(4 + count++, 40));
            // Labelled scenario commitments, not claimed bot production.
            supplier->doProduceItem(item);
            require(world.owner->getNumItems(item) == 1 && world.queued(item) == 1,
                    "Variant accounting control lacks its separate live unit and real queued order");
            const int value = world.price(item) * 2;
            expected[slot] += value;
            const auto oldSlot = QuantBot::compositionSlot(item);
            if (oldSlot < UnitMixPolicy::kSlots) legacy[oldSlot] += value;
        }
        auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        world.bot->configOverride = config;
        const auto off = world.observePlan();
        config->force.simpleTechSavings = true;
        const auto simple = world.observePlan();
        require(off.compositionValue == legacy && simple.compositionValue == legacy
                    && off.unitMix == simple.unitMix,
                "Extended variant accounting or supplier scans leaked into OFF or Simple-only");
        config->force.simpleTechSavings = false;
        config->force.existingCompositionUnitChoice = true;
        const auto on = world.observePlan();
        require(on.compositionValue == expected && on.vehiclePlanValue == on.compositionArmyValue,
                "Standalone composition omitted a variant's actual owned or queued nominal value");
        int total = 0;
        for (size_t slot = 0; slot < expected.size(); ++slot) {
            total += int(expected[slot]);
            if (expected[slot] > 0)
                require(on.compositionAvailable[slot] && on.compositionPrice[slot] > 0
                            && world.bot->allocationSnapshot.slots[slot].allocated && on.unitMix[slot] > 0,
                        "A real variant supplier disappeared from the current or strategic family mask");
        }
        require(on.compositionArmyValue == total, "Variant family totals use canonical prices or count a commitment twice");
        for (const auto& [item, slot] : variants) {
            if (world.owner->getNumItems(item) == 0 || QuantBot::compositionSlot(item) < UnitMixPolicy::kSlots) continue;
            auto accepted = on;
            accepted.unitMix = {};
            accepted.unitMix[slot] = 10000; // Labelled selected-family acceptance control.
            accepted.money = 19999;
            BuilderBase* supplier = nullptr;
            for (auto* structure : structureList) {
                auto* builder = dynamic_cast<BuilderBase*>(structure);
                if (builder && builder->getOwner() == world.owner && builder->isAvailableToBuild(item)) {
                    supplier = builder;
                    break;
                }
            }
            require(supplier && accepted.nextCompositionSlot() == slot,
                    "Actual custom acceptance control has no selected real supplier");
            QuantBot::BuilderTurn turn(accepted.money);
            turn.pStructure = supplier;
            turn.pBuilder = supplier;
            require(world.bot->produceItemWithLogging(accepted, turn, item, __LINE__, "fixture_selected_variant_commitment")
                        && accepted.compositionValue[slot] == on.compositionValue[slot] + world.price(item)
                        && accepted.compositionArmyValue == on.compositionArmyValue + world.price(item),
                    "Accepted custom order did not immediately update the shared family and army commitment");
        }
        SDL_Log("SIMPLE_TECH_SAVING_VARIANTS: mod=%s difficulty=%s house=%d actual_variants=%d "
                "owned_and_queued_value=%d off_simple_preserved=1", mod, playerClass, house, count, total);
    }
    if (ModManager::instance().isTornieCustomUnitsAndBuildingsEnabled())
        for (size_t i = 0; i < variants.size(); ++i)
            if (variants[i].first != Unit_TameSandworm)
                require(covered[i], "Authored Tornie variant coverage missed a real available combat supplier");
    // A Tame Sandworm is a real combat family, but its Worfinery offer additionally
    // requires a chaos-mode event. Ordinary scenarios do not invent that offer.
    if (!covered.back())
        notApplicable("unit_choice_variant_accounting", mod, playerClass, "no_actual_chaos_worfinery_offer", Unit_TameSandworm);
}

inline void standaloneVariantSupplier(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isTornieFamilyContentActive()) return;
    World world(mod, playerClass, false, Stage::FirstHeavy, Army::Empty, 8, true, HOUSE_REBELS);
    world.closeMilitaryMarket();
    std::vector<StructureBase*> aircraftFactories;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getItemID() == Structure_HighTechFactory)
            aircraftFactories.push_back(structure);
    for (auto* factory : aircraftFactories) {
        factory->setVisible(VIS_ALL, false);
        factory->destroy();
    }
    auto* lightFactory = world.producer(Structure_LightFactory);
    require(lightFactory && !lightFactory->isAvailableToBuild(Unit_Trike)
                && lightFactory->isAvailableToBuild(Unit_SonicTrike),
            "Rebels variant-only supplier control is not using its authored Sonic Trike offer");
    for (int i = 0; i < 12; ++i) world.spawn(world.owner, Unit_RaiderTrike, Coord(4 + i, 40));
    world.cash(19999);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    world.bot->configOverride = config;
    const auto off = world.observePlan();
    config->force.simpleTechSavings = true;
    const auto simple = world.observePlan();
    require(!off.compositionAvailable[7] && !simple.compositionAvailable[7]
                && off.unitMix[7] == 0 && simple.unitMix[7] == 0,
            "Variant-only fieldability changed the OFF or Simple-only canonical controls");
    config->force.simpleTechSavings = false;
    config->force.existingCompositionUnitChoice = true;
    auto on = world.observePlan();
    require(on.nextCompositionSlot() == 7 && on.unitMix[7] > 0 && on.compositionAvailable[7]
                && on.compositionPrice[7] == world.price(Unit_SonicTrike)
                && on.compositionFactoryAdmits(Structure_LightFactory)
                && !on.compositionFactoryAdmits(Structure_HeavyFactory),
            "Variant-only family has no positive actual-price target or actual factory supplier");
    QuantBot::BuilderTurn turn(on.money);
    turn.pStructure = lightFactory;
    turn.pBuilder = lightFactory;
    const auto before = on.compositionValue[7];
    require(world.bot->produceItemWithLogging(on, turn, Unit_SonicTrike, __LINE__, "fixture_actual_variant_supplier")
                && world.queued(Unit_SonicTrike) == 1
                && on.compositionValue[7] == before + world.price(Unit_SonicTrike),
            "Selected variant-only supplier could not accept and book its real unit");
}

inline void standaloneCustomBypass(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isTornieFamilyContentActive()) return;
    World world(mod, playerClass, false, Stage::Complete, Army::TinyMixed, 8, true, HOUSE_NEUTRAL);
    world.closeMilitaryMarket();
    for (int i = 0; i < 12; ++i) world.spawn(world.owner, Unit_RocketTrike, Coord(4 + i, 40));
    for (auto* factory : world.lights) {
        require(factory->isAvailableToBuild(Unit_RocketTrike), "Cheap custom control lacks its authored Rocket Trike");
        factory->doProduceItem(Unit_RocketTrike);
    }
    auto* idle = static_cast<BuilderBase*>(world.place(Structure_LightFactory));
    idle->curUpgradeLev = idle->getMaxUpgradeLevel();
    idle->updateBuildList();
    world.cash(19999);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    world.bot->configOverride = config;
    auto off = world.observePlan();
    config->force.simpleTechSavings = true;
    const auto simple = world.observePlan();
    require(off.compositionValue[7] == 0 && simple.compositionValue[7] == 0,
            "Cheap custom OFF/Simple controls no longer have their historical unmapped accounting");
    config->force.simpleTechSavings = false;
    config->force.existingCompositionUnitChoice = true;
    auto on = world.observePlan();
    require(on.compositionValue[7] == int64_t(world.price(Unit_RocketTrike)) * 15
                && on.nextCompositionSlot() < UnitMixPolicy::kOrnithopterSlot
                && !on.compositionFactoryAdmits(Structure_LightFactory),
            "Cheap custom owned-plus-queue control did not expose the overweight light family");
    {
        QuantBot::BuilderTurn turn(on.money);
        turn.pStructure = idle;
        turn.pBuilder = idle;
        require(!world.bot->produceItemWithLogging(on, turn, Unit_RocketTrike, __LINE__, "fixture_custom_bypass")
                    && idle->getProductionQueueSize() == 0,
                "Low-priority cheap custom unit bypassed the selected advanced family");
    }
    for (int i = 0; i < 5; ++i) world.build();
    require(world.queued(Unit_RocketTrike) == 3 && idle->getProductionQueueSize() == 0,
            "Full parallel producer passes refilled an overweight custom family");
    {
        QuantBot::BuilderTurn turn(off.money);
        turn.pStructure = idle;
        turn.pBuilder = idle;
        require(world.bot->produceItemWithLogging(off, turn, Unit_RocketTrike, __LINE__, "fixture_legacy_custom_control")
                    && idle->getCurrentProducedItem() == Unit_RocketTrike && off.compositionValue[7] == 0,
                "Standalone OFF no longer preserves legacy custom admission and accepted-order accounting");
    }
}

inline void standaloneInfantryCapital(const char* mod, const char* playerClass) {
    World world(mod, playerClass, false, Stage::Complete, Army::TinyTank, 8, true, HOUSE_ATREIDES);
    world.closeMilitaryMarket();
    for (int i = 0; i < 12; ++i) world.spawn(world.owner, Unit_Soldier, Coord(4 + i, 40));
    world.cash(1000);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.existingCompositionUnitChoice = true;
    world.bot->configOverride = config;
    auto on = world.observePlan();
    require(on.infantryCount >= 3 && !world.bot->infantryQuotaAdmits(on, world.price(Unit_Soldier)),
            "Infantry capital control has not filled its genuine aggregate quota");
    // The candidate evaluator must agree with the unchanged Barracks producer,
    // even on cash-funded arms where normal capital arbitration is bypassed.
    on.sharedSpending = true;
    on.capitalCandidates.clear();
    world.bot->planCapitalCandidates(on);
    for (const auto& candidate : on.capitalCandidates)
        if (std::strcmp(candidate.kind, "military") == 0)
            require(!isInfantryUnit(candidate.item),
                    "Standalone capital reserved cash for an impossible overweight infantry order");
    auto* barracks = world.producer(Structure_Barracks);
    require(barracks, "Infantry capital control lacks its actual Barracks");
    QuantBot::BuilderTurn turn(on.money);
    turn.pStructure = barracks;
    turn.pBuilder = barracks;
    world.bot->buildBarracks(on, turn);
    require(barracks->getProductionQueueSize() == 0,
            "Standalone composition changed the exempt infantry producer's legacy quota");
}

inline void standaloneFactoryVet(const char* mod, const char* playerClass) {
    constexpr std::array<Uint32, 3> factories{Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory};
    {
        World world(mod, playerClass, false, Stage::Complete, Army::TinyMixed, 8, true, HOUSE_ATREIDES);
        world.closeMilitaryMarket();
        auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        config->force.existingCompositionUnitChoice = true;
        world.bot->configOverride = config;
        auto base = world.observePlan();
        for (size_t supplier = 0; supplier < factories.size(); ++supplier) {
            const auto& mask = base.compositionFactoryAvailable[supplier];
            const auto found = std::find(mask.begin(), mask.end(), true);
            require(found != mask.end(), "Factory final-vet fixture has no actual mapped supplier offer");
            const size_t slot = size_t(found - mask.begin());
            for (const auto mode : {std::pair{false, false}, std::pair{false, true}, std::pair{true, true}})
                for (const bool enabled : {false, true})
                    for (size_t candidate = 0; candidate < factories.size(); ++candidate)
                        for (const char* rule : {"cash_factory_expansion", "light_unit_backlog",
                                "heavy_unit_backlog", "air_unit_backlog", "heavy_production"}) {
                            auto pass = base;
                            pass.unitMix = {};
                            pass.unitMix[slot] = 10000; // Labelled chosen-family caller control.
                            pass.existingCompositionChoice = enabled;
                            pass.sharedSpending = mode.first;
                            pass.fundedCityProduction = mode.second;
                            pass.money = 19999;
                            QuantBot::BuilderTurn turn(pass.money);
                            turn.pStructure = world.yard;
                            turn.pBuilder = world.yard;
                            world.bot->beginBuilderTurn(pass, turn);
                            QuantBot::YardTurn yard;
                            yard.itemID = factories[candidate];
                            yard.structureRule = rule;
                            yard.crimeServiceSite = Coord::Invalid();
                            world.bot->vetYardChoice(pass, turn, yard);
                            const bool admitted = !enabled || base.compositionFactoryAvailable[candidate][slot];
                            require(yard.itemID == (admitted ? factories[candidate] : Uint32(NONE_ID)),
                                    "Cash/backlog/funded final-yard admission disagrees with the actual next-family supplier");
                        }
        }
    }
    {
        World world(mod, playerClass, false, Stage::FirstHeavy, Army::TinyTank, 8, true, HOUSE_ATREIDES);
        world.closeMilitaryMarket();
        auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        config->force.existingCompositionUnitChoice = true;
        world.bot->configOverride = config;
        auto vetHeavy = [&](QuantBot::BuildPass& pass) {
            pass.sharedSpending = false;
            pass.fundedCityProduction = true;
            pass.money = 19999;
            QuantBot::BuilderTurn turn(pass.money);
            turn.pStructure = world.yard;
            turn.pBuilder = world.yard;
            world.bot->beginBuilderTurn(pass, turn);
            QuantBot::YardTurn yard;
            yard.itemID = Structure_HeavyFactory;
            yard.structureRule = "heavy_production";
            yard.crimeServiceSite = Coord::Invalid();
            world.bot->vetYardChoice(pass, turn, yard);
            return yard.itemID;
        };
        auto first = world.observePlan();
        require(first.itemCount[Structure_HeavyFactory] == 0
                    && vetHeavy(first) == Structure_HeavyFactory,
                "Standalone factory choice blocked a genuine first economic/MCV supplier");
        world.yard->doProduceItem(Structure_HeavyFactory); // Labelled initial real supplier commitment.
        auto pending = world.observePlan();
        require(world.owner->getNumItems(Structure_HeavyFactory) == 0
                    && pending.itemCount[Structure_HeavyFactory] == 1
                    && vetHeavy(pending) == NONE_ID,
                "Pending first supplier did not block an optional mismatched second copy");
    }
}

inline void standaloneLocalProducers(const char* mod, const char* playerClass) {
    World world(mod, playerClass, false, Stage::Complete, Army::Empty, 8, true);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.existingCompositionUnitChoice = true;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
    world.closeMilitaryMarket();
    for (int i = 0; i < 3; ++i) addMatureYard(world);
    world.cash(19999);
    auto* heavy = world.producer(Structure_HeavyFactory);
    auto* lightFactory = world.lights.front();
    size_t heavySlot = UnitMixPolicy::kSlots, excessSlot = UnitMixPolicy::kSlots;
    size_t lightSlot = UnitMixPolicy::kSlots;
    for (size_t slot = 0; slot < UnitMixPolicy::kOrnithopterSlot; ++slot)
        if (heavy && world.bot->campaignAvailableToBuild(heavy, vehicles[slot])) {
            if (heavySlot == UnitMixPolicy::kSlots) heavySlot = slot;
            else { excessSlot = slot; break; }
        }
    for (size_t slot = UnitMixPolicy::kFirstLightSlot; slot < vehicles.size(); ++slot)
        if (world.bot->campaignAvailableToBuild(lightFactory, vehicles[slot])) { lightSlot = slot; break; }
    if (excessSlot == UnitMixPolicy::kSlots || lightSlot == UnitMixPolicy::kSlots) {
        notApplicable("unit_choice_local_producers", mod, playerClass, "no_two_canonical_heavy_families_and_light_offer");
        return;
    }
    for (int i = 0; i < 10; ++i)
        world.spawn(world.owner, vehicles[excessSlot], Coord(4 + i, 40));
    // This real old commitment leaves the globally largest light deficit busy.
    lightFactory->doProduceItem(vehicles[lightSlot]);
    auto* second = static_cast<BuilderBase*>(world.place(Structure_HeavyFactory));
    second->curUpgradeLev = second->getMaxUpgradeLevel();
    second->updateBuildList();
    world.owner->updateBuildLists();
    auto pass = world.observePlan();
    pass.unitMix = {}; // Labelled local-allocation control, not a new configured prior.
    pass.unitMix[lightSlot] = 5000;
    pass.unitMix[heavySlot] = pass.unitMix[excessSlot] = 2500;
    pass.capitalCandidates.clear();
    pass.capitalChoice = -1;
    pass.sharedSpending = false;
    require(pass.nextCompositionSlot() == lightSlot
                && world.bot->localCompositionSlot(pass, heavy, -1) == heavySlot
                && world.bot->localCompositionSlot(pass, second, -1) == heavySlot,
            "Local-producer control does not separate the global light choice from eligible heavy deficits");
    const int64_t committedBefore = pass.compositionValue[heavySlot];
    {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = heavy;
        turn.pBuilder = heavy;
        world.bot->buildHeavyFactory(pass, turn);
    }
    {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = second;
        turn.pBuilder = second;
        world.bot->buildHeavyFactory(pass, turn);
    }
    require(heavy->getProductionQueueSize() == 1 && second->getProductionQueueSize() == 1
                && heavy->getCurrentProducedItem() == vehicles[heavySlot]
                && second->getCurrentProducedItem() == vehicles[heavySlot]
                && pass.compositionValue[heavySlot] == committedBefore + 2 * world.price(vehicles[heavySlot])
                && pass.compositionRevision == 2
                && lightFactory->getProductionQueueSize() == 1
                && lightFactory->getCurrentProducedItem() == vehicles[lightSlot] && !lightFactory->isOnHold(),
            "A busy global-choice producer blocked parallel real heavy orders or lost their shared commitments");
    require(pass.nextCompositionSlot() == lightSlot && pass.compositionFactoryAdmits(Structure_HeavyFactory),
            "Capital capacity still answers to another producer's globally largest deficit");
    SDL_Log("UNIT_CHOICE_PER_FACTORY: mod=%s difficulty=%s outcome=passed two_heavy_orders=1 old_light_queue_preserved=1",
            mod, playerClass);
}


inline bool standaloneBufferedHeavy(const char* mod, const char* playerClass) {
    World world(mod,playerClass,false,Stage::Complete,Army::Empty,8,true,HOUSE_ATREIDES);
    auto config=std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.existingCompositionUnitChoice=true;
    config->force.bufferedHeavyProduction=true;
    config->force.incomeHeavyFactories=false;
    config->force.startWhatMakesSense=false;
    world.bot->configOverride=config;
    world.closeMilitaryMarket();
    auto* heavy=world.producer(Structure_HeavyFactory);
    size_t heavySlot=UnitMixPolicy::kSlots, lightSlot=UnitMixPolicy::kSlots;
    for (size_t slot=0;slot<UnitMixPolicy::kOrnithopterSlot;++slot)
        if (heavy && world.bot->campaignAvailableToBuild(heavy,vehicles[slot])) { heavySlot=slot;break; }
    for (size_t slot=UnitMixPolicy::kFirstLightSlot;slot<vehicles.size();++slot)
        if (world.bot->campaignAvailableToBuild(world.lights.front(),vehicles[slot])) { lightSlot=slot;break; }
    if (heavySlot==UnitMixPolicy::kSlots || lightSlot==UnitMixPolicy::kSlots) {
        notApplicable("buffered_heavy_queue_budget",mod,playerClass,"no_canonical_heavy_and_light_offers");
        return false;
    }
    auto* second=static_cast<BuilderBase*>(world.place(Structure_HeavyFactory));
    second->curUpgradeLev=second->getMaxUpgradeLevel();second->updateBuildList();
    completeInitialEconomicAssets(world);
    secureInitialCityCore(world);
    for (int i=0;i<2;++i) world.spawn(world.owner,vehicles[heavySlot],Coord(4+i,40));
    world.cash(19999);
    const int price=world.bot->purchasePrice(heavy,vehicles[heavySlot]);
    const auto withMargin=[&](int margin) {
        const auto before=world.observePlan();
        world.cash(world.owner->getCredits()+before.cashBuffer+margin
            -std::min(before.money,before.cashFlow.projectedCash));
        auto pass=world.observePlan();
        pass.unitMix={}; // Labelled ratio control: both real HFs are currently overweight.
        pass.unitMix[heavySlot]=2000;pass.unitMix[lightSlot]=8000;
        pass.sharedSpending=false;pass.capitalChoice=-1;pass.capitalCandidates.clear();
        require(pass.compositionSurplusBudget()==margin,"Real cash-flow setup did not establish the requested surplus margin");
        return pass;
    };
    const auto order=[&](QuantBot::BuildPass& pass,BuilderBase* producer) {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure=producer;turn.pBuilder=producer;turn.bufferedHeavyOrder=true;
        return world.bot->produceItemWithLogging(pass,turn,vehicles[heavySlot],__LINE__,"buffered_heavy_value_choice");
    };
    auto low=withMargin(price-1);
    require(world.bot->localCompositionSlot(low,heavy,-1)==UnitMixPolicy::kSlots
                && !order(low,heavy) && heavy->getProductionQueueSize()==0,
            "Overweight HF production spent the protected buffer when only the unit price was affordable");
    auto funded=withMargin(price);
    const int64_t before=funded.compositionValue[heavySlot];
    require(world.bot->localCompositionSlot(funded,heavy,funded.compositionSurplusBudget())==heavySlot
                && order(funded,heavy) && heavy->getProductionQueueSize()==1
                && funded.compositionValue[heavySlot]==before+world.price(vehicles[heavySlot])
                && funded.compositionSurplusBudget()==0 && !order(funded,second)
                && second->getProductionQueueSize()==0 && !funded.techSavings,
            "Buffered heavy production failed its real queue, shared quote budget or existing-value bookkeeping");
    return true;
}

inline bool standaloneIncomeHeavy(const char* mod, const char* playerClass) {
    World world(mod,playerClass,false,Stage::Complete,Army::Empty,8,true,HOUSE_ATREIDES);
    auto config=std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.existingCompositionUnitChoice=false;
    config->force.bufferedHeavyProduction=false;
    config->force.incomeHeavyFactories=true;
    config->force.startWhatMakesSense=false;
    world.bot->configOverride=config;
    world.closeMilitaryMarket();
    addMatureYard(world);
    std::array<BuilderBase*,3> factories{world.producer(Structure_HeavyFactory),nullptr,nullptr};
    for (size_t i=1;i<factories.size();++i) {
        factories[i]=static_cast<BuilderBase*>(world.place(Structure_HeavyFactory));
        factories[i]->curUpgradeLev=factories[i]->getMaxUpgradeLevel();factories[i]->updateBuildList();
    }
    size_t slot=UnitMixPolicy::kSlots;
    for (size_t candidate=0;candidate<UnitMixPolicy::kOrnithopterSlot;++candidate)
        if (factories[0] && world.bot->campaignAvailableToBuild(factories[0],vehicles[candidate])) { slot=candidate;break; }
    if (slot==UnitMixPolicy::kSlots) {
        notApplicable("income_heavy_capacity",mod,playerClass,"no_canonical_heavy_offer");
        return false;
    }
    world.cash(100000);
    completeInitialEconomicAssets(world);secureInitialCityCore(world);
    world.place(Structure_WindTrap); // Labelled physical headroom for the additional factory.
    for (auto* factory:factories) factory->doProduceItem(vehicles[slot]);
    auto initial=world.observePlan();
    world.cash(world.owner->getCredits()+std::max(0,initial.nextHeavyRunway
        -std::min(initial.money,initial.cashFlow.projectedCash)));
    const auto pressure=[&]() {
        auto pass=world.observePlan();
        pass.unitMix={};pass.unitMix[slot]=10000; // Labelled single-family income-pressure control.
        world.bot->planCapitalForecast(pass);
        require(pass.heavyIncomeFactoryCapacity>0 && pass.allHeavyQueued,
                "Income capacity control did not observe three real saturated heavy factories");
        pass.forecastNetIncome=pass.heavyIncomeCapacity+3*pass.heavyIncomeFactoryCapacity;
        pass.heavyIncomeDemand=QuantBotSpendingPolicy::incomeShareDemand(pass.forecastNetIncome,10000);
        pass.heavyIncomeFactoryTarget=QuantBotSpendingPolicy::factoryTargetForDemand(
            3,pass.heavyIncomeCapacity,pass.heavyIncomeFactoryCapacity,pass.heavyIncomeDemand);
        pass.capitalCandidates.clear();pass.capitalChoice=-1;
        world.bot->planCompositionHeavyCapacity(pass);
        return pass;
    };
    auto pass=pressure();
    require(pass.heavyIncomeFactoryTarget==6 && !pass.existingCompositionChoice && !pass.techSavings,
            "Independent income capacity retained a one/two-factory ceiling or enabled another selection policy");
    world.bot->chooseCapitalPlan(pass);
    require(pass.capitalChoice>=0 && std::string_view(pass.capitalCandidates[pass.capitalChoice].reason)=="income_heavy_capacity",
            "Legal saturated heavy capacity did not become the selected real capital project");
    const auto builder=pass.capitalCandidates[pass.capitalChoice].builder;
    const auto found=std::find_if(pass.capitalBuilders.begin(),pass.capitalBuilders.end(),
        [&](const auto* value) { return value->getObjectID()==builder; });
    require(found!=pass.capitalBuilders.end(),"Selected heavy capital project has no actual construction yard");
    QuantBot::BuilderTurn turn(pass.money);turn.pStructure=*found;turn.pBuilder=*found;
    world.bot->beginBuilderTurn(pass,turn);
    QuantBot::YardTurn yard;yard.isCitySim=world.game->isCitySimEnabled();
    world.bot->chooseYardProductionLanes(pass,turn,yard);world.bot->vetYardChoice(pass,turn,yard);
    require(yard.itemID==Structure_HeavyFactory,"Income-scaled factory choice was rejected by a legacy cap or final mix gate");
    const_cast<BuilderBase*>(*found)->doProduceItem(Structure_HeavyFactory);
    auto pending=pressure();
    require(pending.itemCount[Structure_HeavyFactory]==4 && pending.heavyIncomePendingFactories==1
                && std::none_of(pending.capitalCandidates.begin(),pending.capitalCandidates.end(),
                    [](const auto& value) { return std::string_view(value.reason)=="income_heavy_capacity"; }),
            "Another ready yard duplicated the real pending income-capacity factory");
    return true;
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
                && (!initial.citySimEnabled || world.owner->getProducedPower() - world.owner->getPowerRequirement()
                    >= initial.cityPowerReserve)
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
    const bool receivedIncome =
        world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > receiptsBefore;
    const bool incomeRequired = initialCash < initialQuote;
    const bool paidCompleted = complete() && observedEnginePayment && spent >= FixPoint(initialQuote).getRawValue()
        && (!upgrade || upgradeStartedWithRealCash);
    SDL_Log("SIMPLE_TECH_SAVING_INSTALLMENTS: case=%s mod=%s difficulty=%s item=%u quote=%d initial_cash=%d "
            "zero_cash=%d exact_quote=%d upgrade_human_gate=%d parallel_yards=2 income_required=%d real_receipts=%d "
            "funding=%s engine_payment=%d spent_total_q32=%lld paid_completed=%d stateless_policy=1",
            name, mod, playerClass, item, initial.techCandidate.cost, initialCash,
            int(cashMode == StartCash::Zero), int(cashMode == StartCash::ExactQuote), int(upgrade),
            int(incomeRequired), int(receivedIncome), incomeRequired ? "arriving_income" : "initial_cash",
            int(observedEnginePayment), static_cast<long long>(spent), int(paidCompleted));
    run.finish(paidCompleted && (!incomeRequired || receivedIncome),
               incomeRequired
                   ? "Selected project did not receive real engine-paid installments and bounded income-funded completion"
                   : "Fully cash-funded project did not receive real engine-paid installments and bounded completion");
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
    // This policy regression retains its original upfront-payment contract;
    // the shared installment engine and continuous policy have separate fixtures.
    world.game->gameInitSettings.gameOptions.technologyInstallments = false;
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

void runExistingCompositionUnitChoiceProbe(const char* mod, const char* playerClass) {
    struct RestoreConfig {
        QuantBotConfig saved = getQuantBotConfig();
        ~RestoreConfig() { getQuantBotConfig() = saved; }
    } restore;
    const auto start = std::chrono::steady_clock::now();
    for (const bool causalControlsOff : {false, true}) {
        SimpleTechSavingProbe::composition(mod, playerClass, false, causalControlsOff, true);
        SimpleTechSavingProbe::composition(mod, playerClass, true, causalControlsOff, true);
    }
    SimpleTechSavingProbe::standaloneStarport(mod, playerClass);
    SimpleTechSavingProbe::standaloneVariantAccounting(mod, playerClass);
    SimpleTechSavingProbe::standaloneVariantSupplier(mod, playerClass);
    SimpleTechSavingProbe::standaloneCustomBypass(mod, playerClass);
    SimpleTechSavingProbe::standaloneInfantryCapital(mod, playerClass);
    SimpleTechSavingProbe::standaloneFactoryVet(mod, playerClass);
    SimpleTechSavingProbe::standaloneLocalProducers(mod, playerClass);
    const bool bufferedChecked=SimpleTechSavingProbe::standaloneBufferedHeavy(mod,playerClass);
    const bool incomeChecked=SimpleTechSavingProbe::standaloneIncomeHeavy(mod,playerClass);
    const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=existing_composition_unit_choice outcome=passed mod=%s difficulty=%s "
            "producer_scope=full_build paired_off=1 queued_composition=1 paid_factory_completion=1 "
            "starport_acceptance=1 per_factory_selection=1 independent_technology_policy=1 "
            "buffered_heavy_queue_budget=%d income_factory_target_over_two=%d wall_ms=%lld",
            mod,playerClass,int(bufferedChecked),int(incomeChecked),static_cast<long long>(wall));
}
