// Private balance executable only. Scenario assets are physically placed; every
// asserted policy start uses the real bot or its ordinary order wrapper, and all
// progress/payment/placement observations use the native engine. Include after
// tech-production-probe.inc, which owns the shared World setup and private access.
namespace StartWhatMakesSenseProbe {
using TechProductionProbe::World;
using TechProductionProbe::Stage;
using TechProductionProbe::Army;

inline void require(bool ok, const char* reason) {
    if (!ok) {
        SDL_Log("START_WHAT_MAKES_SENSE_FAIL: %s", reason);
        throw std::runtime_error(reason);
    }
}

inline void configure(World& world, bool enabled) {
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.startWhatMakesSense = false;
    const auto offHash = config->getConfigHash();
    config->force.startWhatMakesSense = true;
    require(config->getConfigHash() != offHash, "Progressive spending is not an independently hashed switch");
    config->force.startWhatMakesSense = enabled;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
    world.yard->curUpgradeLev = world.yard->getMaxUpgradeLevel();
    world.owner->updateBuildLists();
    require(!world.game->getGameInitSettings().getGameOptions().instantBuild,
            "Instant production bypasses the installment fixture");
}

inline int64_t cashQ32(const World& world) {
    return (world.owner->getStartingCredits() + world.owner->getEarnedCredits()).getRawValue();
}

inline QuantBot::BuildPass observe(World& world) {
    auto pass = world.observePlan();
    world.bot->planSensibleBudget(pass);
    return pass;
}

struct Profile {
    int64_t unpaid = 0;
    int64_t perCycle = 0;
};

// Independent observation of the actual queue, not a fabricated planner lane.
// A yard's foundation/building sequence pays one item at a time, so its safe
// envelope is the maximum item rate, never the sum of its sequential rates.
inline Profile queueProfile(const World& world, const BuilderBase* builder) {
    Profile result;
    if (builder->isOnHold()) return result;
    const int house = builder->getOriginalHouseID();
    for (const auto& order : builder->currentProductionQueue) {
        const auto& spec = world.game->objectData.data[order.itemID][house];
        const int ticks = 15 * DuneCity::getCityBuildTime(order.itemID, spec.buildtime);
        const auto price = FixPoint(int(order.price));
        result.unpaid += price.getRawValue();
        result.perCycle = std::max(result.perCycle,
            ((price / ticks) * builder->getBuildSpeedLimit()).getRawValue());
    }
    result.unpaid = std::max<int64_t>(0, result.unpaid - builder->getProductionProgress().getRawValue());
    return result;
}

inline void requireProfile(const World& world, const QuantBot::BuildPass& pass,
                           const BuilderBase* builder) {
    const auto expected = queueProfile(world, builder);
    const auto lane = std::find_if(pass.sensibleDrains.begin(), pass.sensibleDrains.end(),
        [&](const auto& entry) { return entry.builder == builder->getObjectID(); });
    if (expected.unpaid == 0 || expected.perCycle == 0) {
        require(lane == pass.sensibleDrains.end(), "A cancelled/held/empty queue retained a cash drain");
    } else {
        require(lane != pass.sensibleDrains.end() && lane->unpaid == expected.unpaid
                    && lane->perCycle == expected.perCycle,
                "Cash profile disagrees with the actual unpaid sequential production queue");
    }
}

inline int64_t paidTick(World& world, BuilderBase* builder) {
    const auto cash = cashQ32(world);
    const auto progress = builder->getProductionProgress().getRawValue();
    const auto spent = world.spentQ32();
    builder->updateProductionProgress();
    const int64_t paid = cash - cashQ32(world);
    require(paid >= 0 && builder->getProductionProgress().getRawValue() - progress == paid,
            "Native production progress differs from the exact 32.32 cash actually paid");
    require(world.spentQ32() - spent == paid,
            "Native economy accounting did not record the exact fractional payment");
    return paid;
}

inline void pauseAndResume(World& world, BuilderBase* builder, const char* label) {
    require(builder->getProductionQueueSize() > 0, "Payment check has no real accepted queue");
    world.cash(0); // A declared no-income interval, not a planner-side reserve.
    const auto progress = builder->getProductionProgress().getRawValue();
    for (int tick = 0; tick < 12; ++tick) require(paidTick(world, builder) == 0,
        "An empty treasury paid for ordinary production");
    require(builder->getProductionProgress().getRawValue() == progress,
            "An empty treasury advanced ordinary production");
    world.owner->addCredits(3_fix, false); // The actual house income API funds resumption.
    int64_t firstPaid = 0;
    bool fractionalPayment = false;
    for (int tick = 0; tick < 12 && !builder->isWaitingToPlace(); ++tick) {
        const auto paid = paidTick(world, builder);
        firstPaid += paid;
        fractionalPayment |= paid > 0 && paid < FixPoint(1).getRawValue();
    }
    require(firstPaid > 0 && fractionalPayment,
            "Arriving income did not resume real fractional production payments");
    world.cash(0);
    for (int tick = 0; tick < 12; ++tick) require(paidTick(world, builder) == 0,
        "A production quote was treated as previously paid cash after income stopped");
    world.owner->addCredits(7_fix, false);
    int64_t resumedPaid = 0;
    for (int tick = 0; tick < 12 && !builder->isWaitingToPlace(); ++tick)
        resumedPaid += paidTick(world, builder);
    require(resumedPaid > 0, "A partially paid native queue failed to resume with later income");
    SDL_Log("START_WHAT_MAKES_SENSE_PAYMENT: case=%s first_paid_q32=%lld resumed_paid_q32=%lld fractional=1 pause=1",
        label, static_cast<long long>(firstPaid), static_cast<long long>(resumedPaid));
}

inline bool queuedZone(const World& world) {
    return world.queued(Structure_ZoneResidential) || world.queued(Structure_ZoneCommercial)
        || world.queued(Structure_ZoneIndustrial);
}

inline void powerSeed(World& world) {
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    for (Coord at : {Coord(3, 18), Coord(10, 18), Coord(17, 18)}) world.placeAt(Structure_Refinery, at);
    world.owner->updateBuildLists();
    world.closeMilitaryMarket();
    require(world.owner->getProducedPower() == 100 && world.owner->getPowerRequirement() == 90,
            "Power-start scenario does not have real 100/90 generation and demand");
}

inline void lightSeed(World& world) {
    for (Coord at : {Coord(18, 8), Coord(22, 8), Coord(26, 8)}) world.placeAt(Structure_WindTrap, at);
    world.placeAt(Structure_Refinery, Coord(3, 18));
    world.placeAt(Structure_Radar, Coord(10, 18));
    world.placeAt(Structure_ZoneResidential, Coord(18, 18));
    world.owner->updateBuildLists();
    world.closeMilitaryMarket();
    require(world.yard->isAvailableToBuild(Structure_LightFactory)
                && !world.yard->isAvailableToBuild(Structure_HeavyFactory) && world.owner->hasPower(),
            "First-light scenario lacks its real prerequisite gap or power");
}

inline void civicSeed(World& world) {
    world.placeAt(Structure_NuclearPlant, Coord(27, 3));
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    for (int i = 0; i < 4; ++i) world.place(Structure_Refinery);
    for (Uint32 item : {Structure_Radar, Structure_LightFactory, Structure_HeavyFactory,
                       Structure_StarPort, Structure_RepairYard, Structure_HighTechFactory}) world.place(item);
    for (int i = 0; i < 8; ++i) world.place(Structure_Silo);
    for (int i = 0; i < 16; ++i) {
        auto* lot = static_cast<ZoneStructure*>(world.placeAt(Structure_ZoneResidential, Coord(3 + 2 * i, 40)));
        lot->setResidentialPopulation(40);
    }
    for (int y = 2; y < 45; ++y) for (int x = 2; x < 37; ++x)
        if (!currentGameMap->getTile(x, y)->hasAStructure()) {
            world.owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
            require(currentGameMap->getTile(x, y)->isRoad(), "Civic district road did not reach the real map");
        }
    world.bot->lastCalculatedSpice = 0;
    world.spawn(world.owner, Unit_Carryall, Coord(18, 35));
    world.game->gameCycleCount = MILLI2CYCLES(300000);
    world.initializePhysicalCity();
    world.closeMilitaryMarket();
    // Storage stays live, but both switch arms receive exactly the same physical
    // vaults, selected from actual net receipts rather than an affordability gate.
    for (int added = 0;; ++added) {
        auto pass = world.observePlan();
        if (world.owner->getCapacity() >= std::max(0, pass.forecastNetIncome)) break;
        require(added < 16, "Physical civic setup cannot bank its own bounded tax forecast");
        world.place(Structure_Silo);
        world.initializePhysicalCity();
    }
    auto pass = world.observePlan();
    require(world.bot->storageExpansionItem(pass, world.yard) == NONE_ID,
            "A real storage shortage competes with the intended civic-start case");
    require(pass.civicStructuralNeed & DuneCity::NeedStadium,
            "Actual residential population did not demand the missing Stadium");
    require(world.yard->isAvailableToBuild(Structure_Stadium) && world.owner->hasPower(),
            "Civic-start scenario lacks real Stadium technology, prerequisites or power");
}

inline void fullBuildStarts(const char* playerClass) {
    for (bool enabled : {false, true}) {
        for (bool lowCash : {false, true}) {
            World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
            configure(world, enabled);
            powerSeed(world);
            const int credits = lowCash ? world.price(Structure_WindTrap) - 1 : 0;
            require(!lowCash || credits >= world.price(Structure_ZoneResidential),
                    "Low-cash wind control cannot afford its competing cheap lot");
            world.cash(credits);
            const auto cash = cashQ32(world), spent = world.spentQ32();
            world.build();
            require(world.queued(Structure_WindTrap) == int(enabled),
                    "Full bot did not preserve OFF full-price waiting and ON zero/low-cash wind starts");
            require(cashQ32(world) == cash && world.spentQ32() == spent,
                    "Accepting a progressive power quote paid cash before an engine tick");
            if (enabled) {
                require(!queuedZone(world), "A selected unfunded windtrap fell back to a cheaper city lot");
                auto pass = observe(world);
                requireProfile(world, pass, world.yard);
                if (!lowCash) pauseAndResume(world, world.yard, "power_zero");
            }
            SDL_Log("START_WHAT_MAKES_SENSE_START: case=power enabled=%d initial_cash=%d queued=%d hash=%s",
                int(enabled), credits, world.queued(Structure_WindTrap), world.configHash.c_str());
        }
    }
    for (bool enabled : {false, true}) {
        for (bool lowCash : {false, true}) {
            World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
            configure(world, enabled);
            lightSeed(world);
            const int credits = lowCash ? world.price(Structure_LightFactory) - 1 : 0;
            world.cash(credits);
            const auto cash = cashQ32(world), spent = world.spentQ32();
            world.build();
            require(world.queued(Structure_LightFactory) == int(enabled),
                    "Full bot did not start its selected first Light Factory at zero/low cash only when enabled");
            require(cashQ32(world) == cash && world.spentQ32() == spent,
                    "A first-factory quote was confused with an actual payment");
            if (enabled && !lowCash) pauseAndResume(world, world.yard, "light_zero");
            SDL_Log("START_WHAT_MAKES_SENSE_START: case=light enabled=%d initial_cash=%d queued=%d hash=%s",
                int(enabled), credits, world.queued(Structure_LightFactory), world.configHash.c_str());
        }
    }
    for (bool enabled : {false, true}) {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, enabled);
        civicSeed(world);
        world.cash(0);
        const auto spent = world.spentQ32();
        world.build();
        require(world.queued(Structure_Stadium) == int(enabled),
                "A real demanded civic did not distinguish OFF start-budget waiting from ON zero-cash admission");
        require(cashQ32(world) == 0 && world.spentQ32() == spent,
                "A zero-cash civic quote was charged up front");
        if (enabled) {
            require(!queuedZone(world), "Cheap zoning replaced the selected missing civic");
            pauseAndResume(world, world.yard, "civic_zero");
        }
        SDL_Log("START_WHAT_MAKES_SENSE_START: case=civic enabled=%d initial_cash=0 queued=%d hash=%s",
            int(enabled), world.queued(Structure_Stadium), world.configHash.c_str());
    }
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, true);
        civicSeed(world);
        require(world.price(Structure_Stadium) > 1, "Low-cash civic control already holds the full quote");
        world.cash(1);
        const auto cash = cashQ32(world), spent = world.spentQ32();
        world.build();
        require(world.queued(Structure_Stadium) == 1 && !queuedZone(world),
                "A real selected civic lost its low-cash start to cheaper growth");
        require(cashQ32(world) == cash && world.spentQ32() == spent,
                "A low-cash civic quote was paid before native production");
        SDL_Log("START_WHAT_MAKES_SENSE_START: case=civic enabled=1 initial_cash=1 queued=1 hash=%s",
            world.configHash.c_str());
    }
}

inline void foundedStart(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
    configure(world, true);
    powerSeed(world);
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    world.owner->updateBuildLists();
    world.cash(0);
    world.build();
    require(world.queued(Structure_WindTrap) == 1 && world.yard->getProductionQueueSize() > 1,
            "A highest-priority zero-cash foundation package was not queued sequentially");
    require(world.yard->getCurrentProducedItem() == Structure_Slab1
                || world.yard->getCurrentProducedItem() == Structure_Slab4,
            "The real foundation package did not put concrete before its building");
    require(world.yard->currentProductionQueue.back().itemID == Structure_WindTrap,
            "Foundation grants did not keep the building at the end of the same real queue");
    auto pass = observe(world);
    require(pass.sensibleDrains.size() == 1, "Sequential foundations were counted as parallel producers");
    requireProfile(world, pass, world.yard);
    pauseAndResume(world, world.yard, "founded_power");
    const int ownedBefore = world.owner->getNumItems(Structure_WindTrap);
    const int64_t unpaidBefore = queueProfile(world, world.yard).unpaid;
    const auto spentBefore = world.spentQ32();
    const int bound = 2000 + 4 * (world.price(Structure_WindTrap)
        + 15 * world.game->objectData.data[Structure_WindTrap][world.owner->getHouseID()].buildtime);
    int foundations = 0;
    bool placedWind = false;
    for (int ticks = 0; ticks < bound && !placedWind; ++ticks) {
        if (world.yard->isWaitingToPlace()) {
            const Uint32 item = world.yard->getCurrentProducedItem();
            auto& sites = world.bot->builderPlaceLocations[world.yard->getObjectID()];
            require(!sites.empty(), "Paid foundation/building lost the bot's actual reserved site");
            const Coord site = sites.front();
            if (item == Structure_WindTrap) {
                const Coord size = getStructureSize(item);
                for (int y = 0; y < size.y; ++y) for (int x = 0; x < size.x; ++x)
                    require(currentGameMap->getTile(site.x + x, site.y + y)->hasPreparedFoundation(),
                            "A building reached placement before its queued concrete was really laid");
            }
            require(world.bot->doPlaceStructure(world.yard, site.x, site.y),
                    "Paid sequential production could not use its real AI placement site");
            sites.pop_front();
            if (item == Structure_WindTrap) placedWind = true;
            else {
                require(item == Structure_Slab1 || item == Structure_Slab4,
                        "Unrelated production entered the sequential foundation package");
                ++foundations;
            }
        } else {
            world.owner->addCredits(2_fix, false);
            paidTick(world, world.yard);
        }
    }
    require(placedWind && foundations > 0 && world.owner->getNumItems(Structure_WindTrap) == ownedBefore + 1,
            "Incoming income did not pay and place the real foundation/building sequence within its bound");
    require(world.spentQ32() - spentBefore >= unpaidBefore,
            "Sequential concrete and generation completed without their remaining real cash payments");
    SDL_Log("START_WHAT_MAKES_SENSE_FOUNDATIONS: zero_start=1 real_foundations=%d placed=1 paid_q32=%lld",
        foundations, static_cast<long long>(world.spentQ32() - spentBefore));
}

// An independent cycle-by-cycle envelope checks every finite prefix, rather
// than calling the production predicate to manufacture its own acceptance data.
inline int requiredCash(const std::vector<Profile>& profiles, int forecast) {
    int64_t end = 0;
    for (const auto& profile : profiles)
        end = std::max(end, (profile.unpaid + profile.perCycle - 1) / profile.perCycle);
    const int64_t income = (FixPoint(forecast) / CityEconomyInvestmentPolicy::horizonCycles).getRawValue();
    int64_t required = 0;
    for (int64_t tick = 1; tick <= end; ++tick) {
        int64_t paid = 0;
        for (const auto& profile : profiles) {
            const int64_t ticks = (profile.unpaid + profile.perCycle - 1) / profile.perCycle;
            paid += profile.perCycle * std::min(ticks, tick);
        }
        required = std::max(required, paid - income * tick);
    }
    const int64_t one = FixPoint(1).getRawValue();
    return int((required + one - 1) / one);
}

inline void parallelDrains(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
    configure(world, true);
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    auto* secondYard = static_cast<BuilderBase*>(world.placeAt(Structure_ConstructionYard, Coord(8, 18)));
    auto* light = static_cast<BuilderBase*>(world.placeAt(Structure_LightFactory, Coord(18, 18)));
    world.placeAt(Structure_Silo, Coord(25, 18));
    for (auto* builder : {secondYard, light}) builder->curUpgradeLev = builder->getMaxUpgradeLevel();
    world.bot->lastCalculatedSpice = 0;
    world.closeMilitaryMarket();
    world.owner->updateBuildLists();
    require(world.owner->hasPower() && light->isAvailableToBuild(Unit_Quad),
            "Parallel-drain setup lacks a real powered lower-priority producer");
    // Labelled initial commitments on two distinct real yards. These are not
    // claimed as full-bot choices; the later Quad is admitted by its real wrapper.
    world.yard->doProduceItem(Structure_WindTrap);
    secondYard->doProduceItem(Structure_Refinery);
    require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_Refinery) == 1,
            "Parallel-drain control did not create both real primary queues");
    auto pass = observe(world);
    require(pass.sensibleDrains.size() == 2, "Initial actual primary queues did not produce two drain lanes");
    requireProfile(world, pass, world.yard);
    requireProfile(world, pass, secondYard);
    require(pass.forecastNetIncome <= 0, "No-worker no-lot control invented forecast receipts");
    const auto& spec = world.game->objectData.data[Unit_Quad][light->getOriginalHouseID()];
    const int quote = world.bot->purchasePrice(light, Unit_Quad);
    const Profile next{FixPoint(quote).getRawValue(),
        ((FixPoint(quote) / (15 * DuneCity::getCityBuildTime(Unit_Quad, spec.buildtime)))
            * light->getBuildSpeedLimit()).getRawValue()};
    const auto primary = queueProfile(world, world.yard), secondary = queueProfile(world, secondYard);
    const int allCash = requiredCash({primary, secondary, next}, pass.forecastNetIncome);
    require(allCash > requiredCash({primary, next}, pass.forecastNetIncome)
                && allCash > requiredCash({next}, pass.forecastNetIncome),
            "The aggregate-drain boundary does not distinguish every existing primary obligation");
    for (bool funded : {false, true}) {
        world.cash(allCash - (funded ? 0 : 1));
        pass = observe(world);
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = light;
        turn.pBuilder = light;
        world.bot->beginBuilderTurn(pass, turn);
        const auto cash = cashQ32(world), spent = world.spentQ32();
        const int quotes = pass.capitalOrderedCost;
        const bool accepted = world.bot->produceItemWithLogging(pass, turn, Unit_Quad, __LINE__, "fixture_parallel_drain");
        require(accepted == funded && world.queued(Unit_Quad) == int(funded),
                "Actual lower-priority queue acceptance disagrees with the finite aggregate cash profile");
        require(cashQ32(world) == cash && world.spentQ32() == spent && pass.money == world.owner->getCredits(),
                "A lower-priority quote was booked as paid cash or removed from the live wallet");
        require(pass.capitalOrderedCost == quotes + (funded ? quote : 0),
                "Rejected quotes polluted accepted-order accounting");
        require(pass.sensibleDrains.size() == (funded ? 3u : 2u),
                "Rejected/accepted orders did not match the actual queue-based drain count");
        requireProfile(world, pass, light);
    }
    // Paying the real lower lane reduces only its unpaid obligation. Holding and
    // cancellation remove the real lane; accepted quote totals cannot keep it live.
    paidTick(world, light);
    world.bot->sensibleOrderAccepted(pass, light, NONE_ID, 0);
    requireProfile(world, pass, light);
    world.bot->doSetOnHold(light, true);
    world.bot->sensibleOrderAccepted(pass, light, NONE_ID, 0);
    requireProfile(world, pass, light);
    world.bot->doSetOnHold(light, false);
    world.bot->sensibleOrderAccepted(pass, light, NONE_ID, 0);
    requireProfile(world, pass, light);
    light->doCancelItem(Unit_Quad);
    world.bot->sensibleOrderAccepted(pass, light, NONE_ID, 0);
    requireProfile(world, pass, light);
    require(pass.sensibleDrains.size() == 2, "Cancelled lower production left a phantom accepted drain");
    pauseAndResume(world, world.yard, "protected_primary");
    SDL_Log("START_WHAT_MAKES_SENSE_PARALLEL: primary_lanes=2 rejected_cash=%d accepted_cash=%d quote=%d actual_queue_profile=1",
        allCash - 1, allCash, quote);
}

inline void rollback(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::Complete, Army::Empty);
    configure(world, true);
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    world.owner->updateBuildLists();
    world.cash(0);
    require(world.owner->getNumItems(Structure_Palace) == 1
                && world.game->getGameInitSettings().getGameOptions().onlyOnePalace
                && world.yard->isAvailableToBuild(Structure_Palace),
            "Rollback control lacks the real one-Palace engine rejection");
    auto pass = observe(world);
    QuantBot::BuilderTurn turn(pass.money);
    turn.pStructure = world.yard;
    turn.pBuilder = world.yard;
    world.bot->beginBuilderTurn(pass, turn);
    const auto spent = world.spentQ32();
    require(!world.bot->queueFoundedStructure(pass, turn, Structure_Palace, __LINE__, "fixture_real_one_palace"),
            "A second Palace bypassed the actual one-Palace engine rule");
    require(pass.capitalOrderedCost > 0, "Rollback control never accepted any real foundation quotes");
    require(world.yard->getProductionQueueSize() == 0 && pass.sensibleDrains.empty()
                && world.bot->builderPlaceLocations[world.yard->getObjectID()].empty(),
            "Rejected founded package retained slabs, placement sites or a phantom cash profile");
    require(cashQ32(world) == 0 && world.spentQ32() == spent,
            "Rolling back unpaid foundation quotes spent or invented house cash");
    requireProfile(world, pass, world.yard);
    SDL_Log("START_WHAT_MAKES_SENSE_ROLLBACK: real_engine_rejection=one_palace accepted_foundation_quotes=%d actual_queue=0 drains=0 paid=0",
        pass.capitalOrderedCost);
}
} // namespace StartWhatMakesSenseProbe

void runStartWhatMakesSenseProbe(const char* playerClass) {
    using namespace StartWhatMakesSenseProbe;
    require(!QuantBotConfig().force.startWhatMakesSense, "StartWhatMakesSense must remain default OFF");
    fullBuildStarts(playerClass);
    foundedStart(playerClass);
    parallelDrains(playerClass);
    rollback(playerClass);
    SDL_Log("START_WHAT_MAKES_SENSE_PASS: difficulty=%s full_bot_power_light_civic=1 off_wait=1 zero_start=1 low_cash=1 no_cheap_fallback=1 quotes_not_paid=1 finite_parallel_drains=1 fractional_pause_resume=1 sequential_foundations=1 rollback_actual_queue=1",
        playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=start_what_makes_sense outcome=passed");
}
