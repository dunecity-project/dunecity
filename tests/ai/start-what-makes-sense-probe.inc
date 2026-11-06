// Private balance executable only. Scenario assets are physically placed; every
// asserted policy start uses the real bot or its ordinary order wrapper, and all
// progress/payment/placement observations use the native engine. Include after
// tech-production-probe.inc, which owns the shared World setup and private access.
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotSpendingPolicy.h>
#include <players/UnitMixPolicy.h>
#include <string_view>

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
    const int64_t one = FixPoint(1).getRawValue();
    const int64_t nativeRate = queueProfile(world, builder).perCycle;
    require(nativeRate > 0, "Payment check has no native positive queue rate");
    const int income = std::max(3, int((nativeRate * 12 + one - 1) / one));
    world.owner->addCredits(FixPoint(income), false); // Declared income funds twelve actual native ticks.
    int64_t firstPaid = 0;
    bool fractionalPayment = false;
    for (int tick = 0; tick < 12 && !builder->isWaitingToPlace(); ++tick) {
        const auto paid = paidTick(world, builder);
        firstPaid += paid;
        fractionalPayment |= paid > 0 && paid % one != 0;
    }
    require(firstPaid > 0 && fractionalPayment,
            "Arriving income did not resume real fractional production payments");
    world.cash(0);
    for (int tick = 0; tick < 12; ++tick) require(paidTick(world, builder) == 0,
        "A production quote was treated as previously paid cash after income stopped");
    world.owner->addCredits(FixPoint(std::max(7, income)), false);
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

// The planner's own receipt forecast: the only income a finite-prefix check may
// count. Tax is bankable only with real storage capacity.
struct Receipts {
    int64_t netTax = 0;     // raw credits per cycle, (tax - upkeep)
    int64_t spice = 0;      // raw credits per cycle from the four-minute fleet forecast
    int64_t spiceStock = 0; // raw remaining field share that ends the spice rate
};

inline Receipts receiptsOf(const World& world, const QuantBot::BuildPass& pass) {
    const bool bankable = world.owner->getCapacity() > 0;
    Receipts result;
    result.netTax = ((FixPoint(bankable ? pass.forecastTax : 0) - FixPoint(pass.forecastUpkeep))
        / DuneCity::kCyclesPerMinute).getRawValue();
    result.spice = (FixPoint(bankable ? pass.forecastSpice : 0)
        / CityEconomyInvestmentPolicy::horizonCycles).getRawValue();
    result.spiceStock = FixPoint(std::max(0, pass.spiceShare)).getRawValue();
    return result;
}

inline bool sameReceipts(const Receipts& a, const Receipts& b) {
    return a.netTax == b.netTax && a.spice == b.spice && a.spiceStock == b.spiceStock;
}

// A whole-basis-point share of a raw 32.32 amount, truncated toward zero.
inline int64_t shareOf(int64_t raw, int bps) {
    return raw / 10000 * bps + raw % 10000 * bps / 10000;
}

// The least share-scaled cash that keeps every listed obligation paid at every
// finite endpoint. An independent integrator: each obligation ends after
// ceil(unpaid/rate) cycles and the spice rate stops when its field share is
// exhausted. It never asks the production predicate for its verdict.
inline int64_t requiredScaled(const std::vector<Profile>& profiles, const Receipts& receipts, int bps) {
    int64_t rate = shareOf(receipts.netTax, bps) + shareOf(receipts.spice, bps);
    std::vector<std::pair<int64_t, int64_t>> ends;
    for (const auto& profile : profiles) {
        if (profile.unpaid <= 0 || profile.perCycle <= 0) continue;
        rate -= profile.perCycle;
        ends.emplace_back((profile.unpaid + profile.perCycle - 1) / profile.perCycle, profile.perCycle);
    }
    if (receipts.spice > 0 && !ends.empty()) {
        const int64_t exhaustion = (receipts.spiceStock + receipts.spice - 1) / receipts.spice;
        if (exhaustion < std::max_element(ends.begin(), ends.end())->first)
            ends.emplace_back(exhaustion, -shareOf(receipts.spice, bps));
    }
    std::sort(ends.begin(), ends.end());
    int64_t previous = 0, balance = 0, required = 0;
    for (const auto& end : ends) {
        balance += rate * (end.first - previous);
        required = std::max(required, -balance);
        rate += end.second;
        previous = end.first;
    }
    return required;
}

// The first whole credit balance whose share covers a share-scaled requirement.
inline int creditsFor(int64_t scaled, int bps) {
    if (scaled <= 0) return 0;
    require(bps > 0, "A zero share cannot fund a positive obligation");
    const int64_t one = FixPoint(1).getRawValue();
    int64_t credits = std::max<int64_t>(0, scaled / one * 10000 / bps - 2);
    while (shareOf(credits * one, bps) < scaled) ++credits;
    return int(credits);
}

// One order's full quote and native per-cycle rate on its real builder.
inline Profile quoteProfile(World& world, BuilderBase* builder, Uint32 item) {
    const int quote = world.bot->purchasePrice(builder, item);
    require(quote > 0, "Order quote control has no real build-list price");
    const auto& spec = world.game->objectData.data[item][builder->getOriginalHouseID()];
    return {FixPoint(quote).getRawValue(),
        ((FixPoint(quote) / (15 * DuneCity::getCityBuildTime(item, spec.buildtime)))
            * builder->getBuildSpeedLimit()).getRawValue()};
}

// The army can spend only through its running military lines and the ready
// producers whose real target, mix and cap gates still admit a unit.
inline int64_t armyCapacity(const World& world, const QuantBot::BuildPass& pass) {
    int64_t capacity = 0;
    for (auto* structure : structureList) {
        auto* builder = dynamic_cast<BuilderBase*>(structure);
        if (!builder || structure->getOwner() != world.owner || builder->getItemID() == Structure_StarPort
            || builder->isUpgrading() || builder->currentProductionQueue.empty()) continue;
        bool military = true;
        for (const auto& order : builder->currentProductionQueue)
            military &= QuantBotBuildPolicy::militaryItem(order.itemID);
        const auto profile = queueProfile(world, builder);
        if (military && profile.unpaid > 0) capacity += profile.perCycle;
    }
    for (const auto& candidate : pass.capitalCandidates) {
        if (candidate.score <= 0 || std::string_view(candidate.kind) != "military" || candidate.price <= 0) continue;
        BuilderBase* builder = nullptr;
        for (auto* structure : structureList)
            if (structure->getObjectID() == candidate.builder) builder = dynamic_cast<BuilderBase*>(structure);
        require(builder && builder->getOwner() == world.owner && QuantBotBuildPolicy::militaryItem(candidate.item),
                "An army entitlement did not come from the house's own military producer");
        const int64_t quote = FixPoint(candidate.price).getRawValue();
        if (builder->getItemID() == Structure_StarPort) {
            // An import is paid at once; its delivery delay bounds the rate.
            const int64_t delay = std::max(1, candidate.delay);
            capacity += quote / delay + (quote % delay != 0);
            continue;
        }
        require(builder->getProductionQueueSize() == 0 && builder->isAvailableToBuild(candidate.item),
                "A local army entitlement did not come from a ready producer's real build list");
        const auto& spec = world.game->objectData.data[candidate.item][builder->getOriginalHouseID()];
        capacity += ((FixPoint(candidate.price) / (15 * DuneCity::getCityBuildTime(candidate.item, spec.buildtime)))
            * builder->getBuildSpeedLimit()).getRawValue();
    }
    return capacity;
}

// The army's guaranteed basis points: what its capacity can spend of the
// receipts plus cash spread over the four-minute horizon, up to the reserve.
inline int armyShare(int64_t capacity, const Receipts& receipts, int64_t cash) {
    if (capacity <= 0) return 0;
    const int64_t wallet = std::max<int64_t>(1, std::max<int64_t>(0, receipts.netTax + receipts.spice)
        + std::max<int64_t>(0, cash) / CityEconomyInvestmentPolicy::horizonCycles);
    const int reserve = QuantBotSpendingPolicy::sensibleArmyShareBps;
    return capacity >= wallet ? reserve : int(std::min<int64_t>(reserve, capacity * 10000 / wallet));
}

inline int cheapestMilitaryOffer(const World& world) {
    int cheapest = INT_MAX;
    for (auto* structure : structureList) {
        auto* builder = dynamic_cast<BuilderBase*>(structure);
        if (!builder || structure->getOwner() != world.owner || builder->getItemID() == Structure_StarPort) continue;
        for (const auto& offer : builder->getBuildList())
            if (QuantBotBuildPolicy::militaryItem(offer.itemID))
                cheapest = std::min(cheapest, world.price(offer.itemID));
    }
    require(cheapest < INT_MAX, "Army control has no real local military offer");
    return cheapest;
}

// Physically field real tanks until every unit the ready local producers offer
// would exceed the bot's own military target. Nothing overrides that target.
inline int fieldArmyTarget(World& world) {
    const int tank = world.price(Unit_Tank);
    require(tank > 0, "Army-target control has no real tank value");
    for (int fielded = 0;;) {
        const auto pass = world.observePlan();
        const int64_t room = int64_t(pass.militaryBudget) - pass.militaryValue - cheapestMilitaryOffer(world);
        if (room < 0) return fielded;
        const int count = int(room / tank) + 1;
        require(fielded + count <= 64, "Cannot physically field the bot's own military target");
        TechProductionProbe::spawnGroundBand(world, world.owner, Unit_Tank, count, Coord(20, 56), 0, 8);
        fielded += count;
    }
}

// The real founded-structure order wrapper on one builder's ordinary turn.
inline bool orderStructure(World& world, QuantBot::BuildPass& pass, BuilderBase* builder, Uint32 item,
                           const char* rule) {
    QuantBot::BuilderTurn turn(pass.money);
    turn.pStructure = builder;
    turn.pBuilder = builder;
    world.bot->beginBuilderTurn(pass, turn);
    return world.bot->queueFoundedStructure(pass, turn, item, __LINE__, rule);
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

enum class FirstFactoryPower { ReserveOnly, DueBeforeCompletion, Blackout };

inline void threatenedFactorySeed(World& world) {
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    for (Coord at : {Coord(3, 18), Coord(10, 18), Coord(17, 18), Coord(24, 18)})
        world.placeAt(Structure_Refinery, at);
    world.placeAt(Structure_Radar, Coord(3, 8));
    world.placeAt(Structure_ZoneResidential, Coord(18, 24));
    for (int y = 2; y < 29; ++y) for (int x = 2; x < 37; ++x)
        if (!currentGameMap->getTile(x, y)->hasAStructure()) {
            world.owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
            require(currentGameMap->getTile(x, y)->isRoad(),
                    "Threatened first-factory scenario lost its real road-served growth opportunity");
        }
    world.game->gameCycleCount = MILLI2CYCLES(300000);
    world.initializePhysicalCity();
    world.closeMilitaryMarket();
    const auto attackers = TechProductionProbe::spawnGroundBand(
        world, world.enemy, Unit_Tank, 1, world.yard->getLocation(), 1, 3);
    auto* attacker = attackers.front();
    Command(world.pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, attacker->getObjectID(),
            world.yard->getObjectID()).executeCommand();
    require(attacker->getTarget() == world.yard && attacker->isInWeaponRange(world.yard),
            "First-factory fixture lacks a real visible ground attacker engaging its actual yard");
    world.bot->updateEnemySightings();
    // Clear actual urgent firing-cover claims with physical emplacements; do not
    // disable their gene or manufacture an empty threat survey. Leave forecast
    // reserve expansion live, while supplying enough real generation for the LF
    // and even full maturation of the existing vacant residential lot.
    const int factoryPower = std::max(0,
        world.game->objectData.data[Structure_LightFactory][world.owner->getHouseID()].power);
    for (int assets = 0;; ++assets) {
        require(assets < 64, "Cannot establish the physical first-factory power/cover boundary");
        auto pass = world.observePlan();
        if (pass.groundThreatAsset.isValid()) {
            world.bot->planningBuilder = world.yard->getObjectID();
            world.bot->clearPlacementCache();
            const Coord site = world.bot->findThreatTurretPlaceLocation(
                pass.groundThreatAsset, pass.groundThreatApproach);
            require(site.isValid(), "Threatened core has no legal scenario emplacement for its real cover need");
            world.placeAt(Structure_RocketTurret, site);
            continue;
        }
        const int latent = std::max(0, pass.matureZonePower - pass.currentZonePower);
        const int headroom = world.owner->getProducedPower() - world.owner->getPowerRequirement();
        if (headroom < factoryPower + latent + 1) {
            world.place(Structure_WindTrap);
            continue;
        }
        if (headroom >= pass.cityPowerReserve) {
            world.place(Structure_Radar);
            continue;
        }
        require(pass.defendingEconomy && !pass.airEngaged && pass.civicStructuralNeed == 0,
                "First-factory priority scenario is not a focused real ground defence");
        require(pass.ownResValve > 0 || pass.ownComValve > 0 || pass.ownIndValve > 0,
                "First-factory fixture erased city demand instead of competing with live growth");
        require(world.yard->isAvailableToBuild(Structure_LightFactory),
                "Threatened first Light Factory is not genuinely available to the real yard");
        break;
    }
    for (Uint32 item : {Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory,
                       Structure_Barracks, Structure_WOR, Structure_StarPort})
        require(world.owner->getNumItems(item) == 0,
                "Threatened first-factory fixture already has a real army supplier");
    require(TechProductionProbe::actualGroundReadiness(world).first == 0,
            "Threatened first-factory fixture inherited a defending field army");
    world.owner->updateBuildLists();
}

inline int imminentConsumerQueues(World& world) {
    const int house = world.owner->getHouseID();
    const auto& radar = world.game->objectData.data[Structure_Radar][house];
    const auto& factory = world.game->objectData.data[Structure_LightFactory][house];
    const int factoryTicks = 15 * DuneCity::getCityBuildTime(Structure_LightFactory, factory.buildtime);
    const int radarTicks = 15 * DuneCity::getCityBuildTime(Structure_Radar, radar.buildtime);
    const int64_t price = FixPoint(radar.price).getRawValue();
    require(radar.power > 0, "Imminent consumer control has no actual positive power draw");
    world.cash(10000); // Labelled initial commitments are paid, not fake progress.
    int queuedPower = 0;
    for (int count = 0; queuedPower <= world.owner->getProducedPower() - world.owner->getPowerRequirement(); ++count) {
        require(count < 8, "Imminent consumer setup cannot cross its real current power headroom");
        auto* builder = static_cast<BuilderBase*>(world.placeAt(Structure_ConstructionYard,
            Coord(40 + 6 * (count % 2), 36 + 6 * (count / 2))));
        builder->curUpgradeLev = builder->getMaxUpgradeLevel();
        builder->updateBuildList();
        auto pass = world.observePlan();
        world.bot->planSensibleBudget(pass);
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = builder;
        turn.pBuilder = builder;
        world.bot->beginBuilderTurn(pass, turn);
        require(world.bot->queueFoundedStructure(pass, turn, Structure_Radar, __LINE__,
                    "fixture_imminent_consuming_commitment"),
                "Initial consuming commitment has no real legal production/placement package");
        require(builder->getCurrentProducedItem() == Structure_Radar,
                "Imminent consumer control did not create a real consuming building queue");
        const int64_t rate = ((FixPoint(radar.price) / radarTicks)
            * builder->getBuildSpeedLimit()).getRawValue();
        require(rate > 0, "Imminent real consumer has no native production rate");
        int paidTicks = 0;
        while (price - builder->getProductionProgress().getRawValue() > 2 * rate) {
            require(++paidTicks <= 2 * radarTicks, "Native initial consumer payment exceeded its declared bound");
            require(paidTick(world, builder) > 0, "A funded real consumer did not make native paid progress");
        }
        const int64_t remaining = price - builder->getProductionProgress().getRawValue();
        require(remaining > 0 && !builder->isWaitingToPlace()
                    && (remaining + rate - 1) / rate < factoryTicks,
                "Countercase consumer is already placeable or would not finish before the first LF");
        queuedPower += radar.power;
    }
    require(world.owner->hasPower(), "Imminent-queue countercase is already in an actual blackout");
    return queuedPower;
}

inline void threatenedFirstFactory(const char* playerClass) {
    for (bool enabled : {false, true}) for (FirstFactoryPower power :
            {FirstFactoryPower::ReserveOnly, FirstFactoryPower::DueBeforeCompletion, FirstFactoryPower::Blackout}) {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, enabled);
        threatenedFactorySeed(world);
        int imminentPower = 0;
        if (power == FirstFactoryPower::DueBeforeCompletion) {
            imminentPower = imminentConsumerQueues(world);
        } else if (power == FirstFactoryPower::Blackout) {
            for (int consumers = 0; world.owner->hasPower(); ++consumers) {
                require(consumers < 8, "Physical blackout control did not exhaust actual generation");
                world.place(Structure_Radar);
            }
            world.owner->updateBuildLists();
        }
        world.cash(6000);
        const auto plan = world.observePlan();
        const int produced = world.owner->getProducedPower(), required = world.owner->getPowerRequirement();
        const int factoryPower = std::max(0,
            world.game->objectData.data[Structure_LightFactory][world.owner->getHouseID()].power);
        const int latent = std::max(0, plan.matureZonePower - plan.currentZonePower);
        require(plan.defendingEconomy, "Real engaging enemy disappeared before the full bot decision");
        require(plan.ownResValve > 0 || plan.ownComValve > 0 || plan.ownIndValve > 0,
                "Full first-factory decision has no genuine competing city demand");
        if (power == FirstFactoryPower::ReserveOnly) {
            require(world.owner->hasPower() && produced - required >= factoryPower + latent
                        && produced - required < plan.cityPowerReserve,
                    "Positive control does not separate actual LF power needs from forecast-only reserve");
        } else if (power == FirstFactoryPower::DueBeforeCompletion) {
            require(world.owner->hasPower() && imminentPower > produced - required
                        && plan.committedPowerDemand >= imminentPower,
                    "Real soon-completing consumers would not cause a pre-LF power deficit");
        } else {
            require(!world.owner->hasPower() && required > produced,
                    "Blackout countercase did not create an actual engine power deficit");
        }
        const auto cash = cashQ32(world), spent = world.spentQ32();
        world.build();
        const bool factoryFirst = enabled && power == FirstFactoryPower::ReserveOnly;
        const int generators = world.queued(Structure_WindTrap) + world.queued(Structure_NuclearPlant);
        require(world.queued(Structure_LightFactory) == int(factoryFirst),
                "Threatened first LF did not distinguish forecast reserve from imminent/actual power loss");
        require(factoryFirst ? generators == 0 : generators > 0,
                "Full real yard did not preserve the OFF forecast-power choice or ON power-loss exceptions");
        require(!queuedZone(world),
                "Live city growth displaced the threatened first capability or its necessary power");
        require(cashQ32(world) == cash && world.spentQ32() == spent,
                "First-factory/power selection treated an accepted quote as an up-front cash payment");
        require(paidTick(world, world.yard) > 0,
                "The full bot's selected LF or necessary generator did not enter ordinary paid production");
        SDL_Log("START_WHAT_MAKES_SENSE_THREATENED_FACTORY: enabled=%d case=%s power=%d/%d reserve=%d latent=%d imminent_power=%d lf_first=%d generators=%d live_city_demand=1 native_paid=1 hash=%s",
            int(enabled), power == FirstFactoryPower::ReserveOnly ? "forecast_reserve"
                : power == FirstFactoryPower::DueBeforeCompletion ? "deficit_before_lf" : "actual_blackout",
            produced, required, plan.cityPowerReserve, latent, imminentPower, int(factoryFirst), generators,
            world.configHash.c_str());
    }
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
    // A civic-start case must not also be an unanswered crime emergency. The
    // dense initial industrial core and the two residential edges need actual
    // nearby service, not disabled service genes or overwritten crime maps.
    // These road-served stations share the core's and apartments' native
    // six-tile police cells; the real effects scan computes their coverage.
    for (int y : {1, 11, 37}) for (int x : {3, 9, 15, 21, 27, 33})
        world.placeAt(Structure_PoliceStation, Coord(x, y));
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
    require(pass.dangerousCrimeProperties == 0,
            "Physical civic district still has an urgent crime emergency instead of a clear civic-start premise");
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
    // Ready light and heavy factories whose real target, mix and caps still
    // admit units keep the army's guaranteed share: a zero-cash civic waits.
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, true);
        civicSeed(world);
        world.cash(0);
        const auto plan = observe(world);
        const int64_t capacity = armyCapacity(world, plan);
        require(capacity > 0 && plan.sensibleArmyShare > 0
                    && plan.sensibleArmyShare == armyShare(capacity, receiptsOf(world, plan), cashQ32(world)),
                "Civic army-need control has no live, exactly reserved army entitlement");
        const auto spent = world.spentQ32();
        world.build();
        require(world.queued(Structure_Stadium) == 0,
                "A zero-cash civic started on the army's reserved share instead of its own funding");
        require(cashQ32(world) == 0 && world.spentQ32() == spent, "Holding a reserved-share civic moved real cash");
        SDL_Log("START_WHAT_MAKES_SENSE_START: case=civic enabled=1 army_need=1 army_share_bps=%d initial_cash=0 queued=0 hash=%s",
            plan.sensibleArmyShare, world.configHash.c_str());
    }
    // A physically fielded army at the bot's own target leaves the whole wallet
    // to development: OFF still waits for full price, ON starts at zero/low cash.
    for (bool enabled : {false, true}) {
        for (bool lowCash : {false, true}) {
            if (!enabled && lowCash) continue;
            World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
            configure(world, enabled);
            civicSeed(world);
            const int fielded = fieldArmyTarget(world);
            require(!lowCash || world.price(Structure_Stadium) > 1, "Low-cash civic control already holds the full quote");
            world.cash(lowCash ? 1 : 0);
            if (enabled) {
                const auto plan = observe(world);
                require(plan.sensibleArmyShare == 0 && armyCapacity(world, plan) == 0 && plan.sensibleDrains.empty(),
                        "A satisfied army still reserved a share, or other work competes with the civic start");
            }
            const auto cash = cashQ32(world), spent = world.spentQ32();
            world.build();
            require(world.queued(Structure_Stadium) == int(enabled),
                    "A real demanded civic did not distinguish OFF start-budget waiting from ON zero/low-cash admission");
            require(cashQ32(world) == cash && world.spentQ32() == spent,
                    "A zero/low-cash civic quote was charged up front");
            if (enabled) {
                require(!queuedZone(world), "Cheap zoning replaced the selected missing civic");
                if (!lowCash) pauseAndResume(world, world.yard, "civic_zero");
            }
            SDL_Log("START_WHAT_MAKES_SENSE_START: case=civic enabled=%d army_target_met=1 fielded_tanks=%d initial_cash=%d queued=%d hash=%s",
                int(enabled), fielded, lowCash ? 1 : 0, world.queued(Structure_Stadium), world.configHash.c_str());
        }
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
    for (const auto& lane : pass.sensibleDrains)
        require(!lane.military && !lane.emergency, "Initial primary queues were not ordinary development lanes");
    require(pass.forecastNetIncome <= 0, "No-worker no-lot control invented forecast receipts");
    const auto receipts = receiptsOf(world, pass);
    // The idle factory is the army's whole spending capacity here, and only while
    // its real target, mix and caps still admit a unit.
    const int64_t capacity = armyCapacity(world, pass);
    const int quote = world.bot->purchasePrice(light, Unit_Quad);
    const auto next = quoteProfile(world, light, Unit_Quad);
    const auto primary = queueProfile(world, world.yard), secondary = queueProfile(world, secondYard);
    const auto whole = [&](const std::vector<Profile>& profiles) {
        return creditsFor(requiredScaled(profiles, receipts, 10000), 10000);
    };
    const int allCash = whole({primary, secondary, next});
    require(allCash > whole({primary, next}) && allCash > whole({next}),
            "The aggregate-drain boundary does not distinguish every existing primary obligation");
    // The reserved share is a claim on this same real wallet, not another
    // payment scheduler. These two native civil queues already consume more
    // than their 70% allowance: a unit fitting its own 30% must still wait.
    const auto reserved = [&](int credits) {
        const int64_t cash = FixPoint(credits).getRawValue();
        const int share = armyShare(capacity, receipts, cash);
        return share > 0 && shareOf(cash, share) >= requiredScaled({next}, receipts, share);
    };
    int nominalCash = 0;
    while (nominalCash < allCash && !reserved(nominalCash)) ++nominalCash;
    require(nominalCash > 0 && nominalCash < allCash && reserved(nominalCash),
            "Civil-overbook control does not separate the nominal army share from the real wallet");
    world.cash(nominalCash);
    pass = observe(world);
    const int developmentShare = 10000 - pass.sensibleArmyShare;
    require(sameReceipts(receiptsOf(world, pass), receipts) && armyCapacity(world, pass) == capacity
                && pass.sensibleArmyShare == armyShare(capacity, receipts, cashQ32(world))
                && requiredScaled({primary, secondary}, receipts, developmentShare)
                    > shareOf(cashQ32(world), developmentShare),
            "Existing civil queues do not exceed their nominal development share");
    {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = light;
        turn.pBuilder = light;
        world.bot->beginBuilderTurn(pass, turn);
        const auto cash = cashQ32(world), spent = world.spentQ32();
        const int quotes = pass.capitalOrderedCost;
        require(!world.bot->produceItemWithLogging(pass, turn, Unit_Quad, __LINE__, "fixture_civil_overbook"),
                "The nominal army share started a new unit beside insolvent real civil queues");
        require(world.queued(Unit_Quad) == 0 && world.queued(Structure_WindTrap) == 1
                    && world.queued(Structure_Refinery) == 1 && pass.sensibleDrains.size() == 2
                    && cashQ32(world) == cash && world.spentQ32() == spent && pass.capitalOrderedCost == quotes
                    && !world.yard->isOnHold() && !secondYard->isOnHold(),
                "Refusing an overbooked unit changed existing work, cash or accepted-order accounting");
        requireProfile(world, pass, world.yard);
        requireProfile(world, pass, secondYard);
    }
    SDL_Log("START_WHAT_MAKES_SENSE_ARMY_OVERBOOK: civil_lanes=2 civil_over_development_share=1 army_nominal_fits=1 "
            "cash=%d whole_profile_cash=%d unit_held=1 existing_work_unchanged=1", nominalCash, allCash);
    const int boundary = allCash;
    const char* rule = reserved(boundary) ? "army_share" : "spare_funds";
    for (bool funded : {false, true}) {
        world.cash(boundary - (funded ? 0 : 1));
        pass = observe(world);
        require(sameReceipts(receiptsOf(world, pass), receipts) && armyCapacity(world, pass) == capacity
                    && pass.sensibleArmyShare == armyShare(capacity, receipts, cashQ32(world)),
                "The reserved army share disagrees with its live military capacity and wallet");
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = light;
        turn.pBuilder = light;
        world.bot->beginBuilderTurn(pass, turn);
        const auto cash = cashQ32(world), spent = world.spentQ32();
        const int quotes = pass.capitalOrderedCost;
        const bool accepted = world.bot->produceItemWithLogging(pass, turn, Unit_Quad, __LINE__, "fixture_parallel_drain");
        require(accepted == funded && world.queued(Unit_Quad) == int(funded),
                "Lower-priority military acceptance disagrees with the entire real wallet's finite cash profile");
        require(cashQ32(world) == cash && world.spentQ32() == spent && pass.money == world.owner->getCredits(),
                "A lower-priority quote was booked as paid cash or removed from the live wallet");
        require(pass.capitalOrderedCost == quotes + (funded ? quote : 0),
                "Rejected quotes polluted accepted-order accounting");
        require(pass.sensibleDrains.size() == (funded ? 3u : 2u),
                "Rejected/accepted orders did not match the actual queue-based drain count");
        requireProfile(world, pass, light);
        if (funded) {
            const auto lane = std::find_if(pass.sensibleDrains.begin(), pass.sensibleDrains.end(),
                [&](const auto& entry) { return entry.builder == light->getObjectID(); });
            require(lane != pass.sensibleDrains.end() && lane->military && !lane->emergency,
                    "The accepted Quad's actual queue was not an ordinary military lane");
        }
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
    SDL_Log("START_WHAT_MAKES_SENSE_PARALLEL: primary_lanes=2 rule=%s army_share_bps=%d rejected_cash=%d accepted_cash=%d whole_profile_cash=%d quote=%d actual_queue_profile=1",
        rule, armyShare(capacity, receipts, FixPoint(boundary).getRawValue()), boundary - 1, boundary, allCash, quote);
}

// A real lower-priority development queue on a second yard: a labelled earlier
// commitment that no declared cash or receipt funds. A working spice worker
// keeps refinery/worker orders from being a separate narrow emergency here.
inline ConstructionYard* lowerPriorityQueue(World& world) {
    auto* second = dynamic_cast<ConstructionYard*>(world.placeAt(Structure_ConstructionYard, Coord(25, 18)));
    require(second, "Lower-priority control lacks its real second yard");
    second->curUpgradeLev = second->getMaxUpgradeLevel();
    world.spawn(world.owner, Unit_Harvester, Coord(37, 29));
    world.owner->updateBuildLists();
    second->doProduceItem(Structure_Silo);
    require(second->getCurrentProducedItem() == Structure_Silo,
            "Lower-priority control did not create its real labelled queue");
    return second;
}

// A new higher-priority generator no longer starts unconditionally beside an
// underfunded real queue: the earlier work must be funded alongside it or be
// finished first. An actual blackout remains the narrow emergency exception.
inline void underfundedPredecessor(const char* playerClass) {
    enum class Arm { Underfunded, Funded, Finished, Blackout };
    for (Arm arm : {Arm::Underfunded, Arm::Funded, Arm::Finished, Arm::Blackout}) {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, true);
        powerSeed(world);
        auto* second = lowerPriorityQueue(world);
        if (arm == Arm::Blackout) {
            for (int consumers = 0; world.owner->hasPower(); ++consumers) {
                require(consumers < 8, "Physical blackout control did not exhaust actual generation");
                world.place(Structure_Radar);
            }
            world.owner->updateBuildLists();
        }
        world.cash(0);
        auto pass = observe(world);
        require(pass.sensibleDrains.size() == 1 && pass.sensibleArmyShare == 0 && armyCapacity(world, pass) == 0,
                "Predecessor control is not one real queue without an army entitlement");
        requireProfile(world, pass, second);
        const auto lane = pass.sensibleDrains.front();
        require(lane.builder == second->getObjectID() && !lane.military && !lane.emergency
                    && lane.priority > world.bot->sensiblePriority(pass, Structure_WindTrap),
                "The started queue is not a lower-priority ordinary development lane");
        const auto silo = queueProfile(world, second);
        const auto receipts = receiptsOf(world, pass);
        require(requiredScaled({silo}, receipts, 10000) > 0,
                "The started lower-priority queue is already funded by receipts alone");
        const auto spent = world.spentQ32();
        int boundary = 0;
        if (arm == Arm::Underfunded) {
            require(!orderStructure(world, pass, world.yard, Structure_WindTrap, "fixture_underfunded_predecessor"),
                    "A higher-priority generator still started unconditionally beside an underfunded real queue");
            require(pass.sensiblePending.purpose == Structure_WindTrap
                        && pass.sensiblePending.builder == world.yard->getObjectID(),
                    "The refused generator was not kept as this pass's pending highest-priority want");
            require(world.yard->getProductionQueueSize() == 0, "A refused generator left a real order behind");
            world.build();
            require(world.queued(Structure_WindTrap) == 0 && !queuedZone(world)
                        && world.yard->getProductionQueueSize() == 0,
                    "The full bot added new work beside its underfunded real queue");
            require(cashQ32(world) == 0 && world.spentQ32() == spent, "Holding new work moved real cash");
            requireProfile(world, observe(world), second);
        } else if (arm == Arm::Funded) {
            const auto wind = quoteProfile(world, world.yard, Structure_WindTrap);
            boundary = creditsFor(requiredScaled({silo, wind}, receipts, 10000), 10000);
            require(boundary > 0, "Funded-predecessor control needs no cash for its two real lanes");
            for (bool funded : {false, true}) {
                world.cash(boundary - (funded ? 0 : 1));
                pass = observe(world);
                require(sameReceipts(receiptsOf(world, pass), receipts) && pass.sensibleArmyShare == 0,
                        "Changing only cash changed receipts or created an army entitlement");
                const auto cash = cashQ32(world);
                require(orderStructure(world, pass, world.yard, Structure_WindTrap, "fixture_funded_predecessor") == funded,
                        "A generator beside a real queue disagreed with the exact finite funding of both lanes");
                require(world.queued(Structure_WindTrap) == int(funded) && cashQ32(world) == cash
                            && world.spentQ32() == spent && pass.sensibleDrains.size() == (funded ? 2u : 1u),
                        "A funded start booked its quote as cash or disagreed with the actual queues");
                requireProfile(world, pass, second);
                requireProfile(world, pass, world.yard);
            }
        } else if (arm == Arm::Finished) {
            world.cash(10000); // A declared paid interval for the earlier commitment only.
            const auto& spec = world.game->objectData.data[Structure_Silo][world.owner->getHouseID()];
            const int bound = 2 * 15 * DuneCity::getCityBuildTime(Structure_Silo, spec.buildtime) + 8;
            for (int tick = 0; !second->isWaitingToPlace(); ++tick) {
                require(tick < bound, "The funded earlier commitment did not finish within its native bound");
                require(paidTick(world, second) > 0, "The earlier commitment made no native paid progress");
            }
            world.bot->planningBuilder = second->getObjectID();
            world.bot->clearPlacementCache();
            const Coord site = world.bot->findPlaceLocation(Structure_Silo);
            require(site.isValid() && world.bot->doPlaceStructure(second, site.x, site.y)
                        && second->getProductionQueueSize() == 0,
                    "The finished earlier commitment had no real legal placement");
            world.cash(0);
            pass = observe(world);
            require(pass.sensibleDrains.empty() && pass.sensibleArmyShare == 0 && world.owner->hasPower(),
                    "Finished-predecessor control still has running work, an army entitlement or no power");
            const auto paid = world.spentQ32();
            world.build();
            require(world.queued(Structure_WindTrap) == 1 && !queuedZone(world)
                        && cashQ32(world) == 0 && world.spentQ32() == paid,
                    "After its earlier work finished, the bot did not start its generator from zero cash");
        } else {
            require(!world.owner->hasPower(), "Blackout countercase still has actual generation headroom");
            world.build();
            require(world.queued(Structure_WindTrap) + world.queued(Structure_NuclearPlant) == 1 && !queuedZone(world),
                    "An actual blackout generator lost its narrow emergency start beside an underfunded queue");
            require(cashQ32(world) == 0 && world.spentQ32() == spent, "An emergency start paid cash up front");
        }
        SDL_Log("START_WHAT_MAKES_SENSE_PREDECESSOR: case=%s lower_priority=%d both_funded_cash=%d wind=%d hash=%s",
            arm == Arm::Underfunded ? "underfunded" : arm == Arm::Funded ? "funded_boundary"
                : arm == Arm::Finished ? "finished_first" : "blackout_emergency",
            lane.priority, boundary, world.queued(Structure_WindTrap), world.configHash.c_str());
    }
}

// Light and heavy factories, ready and powered, with no running work.
inline void armySeed(World& world) {
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    world.placeAt(Structure_Silo, Coord(25, 18));
    for (Uint32 item : {Structure_LightFactory, Structure_HeavyFactory}) {
        auto* factory = dynamic_cast<BuilderBase*>(world.place(item));
        require(factory, "Army-entitlement control lacks a real factory");
        factory->curUpgradeLev = factory->getMaxUpgradeLevel();
    }
    world.bot->lastCalculatedSpice = 0;
    world.closeMilitaryMarket();
    world.owner->updateBuildLists();
    require(world.owner->hasPower() && world.producer(Structure_LightFactory)->isAvailableToBuild(Unit_Quad),
            "Army-entitlement control lacks powered ready local producers");
}

// The army keeps its guaranteed share only while ready producers' real target,
// mix ratio and unit cap still admit a unit. While it is reserved, development
// must fit the remaining share even when the whole finite prefix is funded.
inline void armyEntitlement(const char* playerClass) {
    const int reserve = QuantBotSpendingPolicy::sensibleArmyShareBps;
    require(reserve > 0 && reserve < 10000, "The army share is not a partial reservation");
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        configure(world, true);
        armySeed(world);
        world.cash(0);
        auto pass = observe(world);
        const int64_t capacity = armyCapacity(world, pass);
        const auto receipts = receiptsOf(world, pass);
        require(capacity > 0 && pass.sensibleDrains.empty() && pass.sensibleArmyShare > 0
                    && pass.sensibleArmyShare == armyShare(capacity, receipts, cashQ32(world)),
                "Ready factories with a live army need did not reserve the exact army share");
        require(!orderStructure(world, pass, world.yard, Structure_WindTrap, "fixture_reserved_army_zero")
                    && world.yard->getProductionQueueSize() == 0,
                "A zero-cash development start took the army's reserved share as a sole primary");
        const auto wind = quoteProfile(world, world.yard, Structure_WindTrap);
        const int whole = creditsFor(requiredScaled({wind}, receipts, 10000), 10000);
        const auto admits = [&](int credits) {
            const int64_t cash = FixPoint(credits).getRawValue();
            const int development = 10000 - armyShare(capacity, receipts, cash);
            return shareOf(cash, development) >= requiredScaled({wind}, receipts, development)
                && cash >= requiredScaled({wind}, receipts, 10000);
        };
        int boundary = whole;
        while (!admits(boundary))
            require(++boundary < House::MAX_GAME_CREDITS, "No cash funds a development start beside the army share");
        require(boundary > whole,
                "The army's reserved share did not separate a funded start from the whole finite prefix");
        for (bool funded : {false, true}) {
            world.cash(boundary - (funded ? 0 : 1));
            pass = observe(world);
            require(sameReceipts(receiptsOf(world, pass), receipts) && armyCapacity(world, pass) == capacity
                        && pass.sensibleArmyShare == armyShare(capacity, receipts, cashQ32(world)),
                    "The reserved army share disagrees with its live capacity and wallet");
            const auto cash = cashQ32(world), spent = world.spentQ32();
            require(orderStructure(world, pass, world.yard, Structure_WindTrap, "fixture_reserved_army_share") == funded,
                    "Development admission disagreed with the exact cash left beside the army's reserved share");
            require(world.queued(Structure_WindTrap) == int(funded) && cashQ32(world) == cash
                        && world.spentQ32() == spent,
                    "A development start beside the army reserve booked its quote as cash");
            requireProfile(world, pass, world.yard);
        }
        SDL_Log("START_WHAT_MAKES_SENSE_ARMY_SHARE: case=need army_share_bps=%d whole_prefix_cash=%d rejected_cash=%d accepted_cash=%d",
            armyShare(capacity, receipts, FixPoint(boundary).getRawValue()), whole, boundary - 1, boundary);
        // Ratio: physically field the light factory's own mix share while the
        // target and caps still admit more. Only that line leaves the reservation.
        auto* light = world.producer(Structure_LightFactory);
        const int64_t one = FixPoint(1).getRawValue();
        const int spread = int(capacity * 2 * 10000 / reserve * CityEconomyInvestmentPolicy::horizonCycles / one) + 1;
        world.cash(spread);
        pass = observe(world);
        const int needShare = pass.sensibleArmyShare;
        require(armyCapacity(world, pass) == capacity
                    && needShare == armyShare(capacity, receiptsOf(world, pass), cashQ32(world))
                    && needShare > 0 && needShare < reserve,
                "A large declared balance did not scale the army share by its live capacity");
        int fieldedLight = 0;
        for (;;) {
            Uint32 wanted = NONE_ID;
            for (const auto& candidate : pass.capitalCandidates)
                if (candidate.builder == light->getObjectID() && candidate.score > 0
                    && std::string_view(candidate.kind) == "military") wanted = candidate.item;
            if (wanted == NONE_ID) break;
            require(++fieldedLight <= 4, "A few real light units did not meet the light factory's mix share");
            TechProductionProbe::spawnGroundBand(world, world.owner, wanted, 1, Coord(20, 56), 0, 8);
            pass = observe(world);
        }
        require(fieldedLight > 0 && !pass.usesCompositionChoice(),
                "The light factory had no ordinary mix entitlement to satisfy");
        for (const auto& offer : light->getBuildList()) {
            if (!QuantBotBuildPolicy::militaryItem(offer.itemID)) continue;
            const int value = world.price(offer.itemID);
            int bps = 0;
            for (size_t slot = 0; slot < QuantBot::kMixItems.size(); ++slot)
                if (QuantBot::kMixItems[slot] == offer.itemID) bps = pass.unitMix[slot];
            require(int64_t(pass.militaryValue) + value <= pass.militaryBudget
                        && !world.owner->isUnitLimitReached(offer.itemID)
                        && (bps <= 0 || int64_t(pass.vehiclePlanValue) * bps
                            <= int64_t(pass.itemCount[offer.itemID]) * value * 10000),
                    "The light factory's entitlement ended for a reason other than its met mix ratio");
        }
        const int64_t ratioCapacity = armyCapacity(world, pass);
        require(ratioCapacity > 0 && ratioCapacity < capacity
                    && pass.sensibleArmyShare == armyShare(ratioCapacity, receiptsOf(world, pass), cashQ32(world))
                    && pass.sensibleArmyShare < needShare,
                "Meeting one producer's mix ratio did not remove exactly its line from the army share");
        SDL_Log("START_WHAT_MAKES_SENSE_ARMY_SHARE: case=ratio fielded_light=%d need_share_bps=%d ratio_share_bps=%d cash=%d",
            fieldedLight, needShare, pass.sensibleArmyShare, spread);
    }
    // Target, then an explicit unit cap: a satisfied or unbuildable army keeps
    // no share, so the same development start is a zero-cash sole primary again.
    for (bool capped : {false, true}) {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty, 8, false,
                    HOUSE_HARKONNEN, 3, capped ? 1 : -1);
        configure(world, true);
        armySeed(world);
        world.cash(0);
        int fielded = 0;
        if (capped) {
            require(world.owner->getMaxUnits() == 1 && !world.owner->isGroundUnitLimitReached(),
                    "The explicit unit-cap control was not honoured below its cap");
            auto below = observe(world);
            require(armyCapacity(world, below) > 0 && below.sensibleArmyShare > 0
                        && !orderStructure(world, below, world.yard, Structure_WindTrap, "fixture_army_below_cap"),
                    "Below its explicit cap the army did not keep its reserved share");
            TechProductionProbe::spawnGroundBand(world, world.owner, Unit_Tank, 1, Coord(20, 56), 0, 8);
            fielded = 1;
        } else {
            fielded = fieldArmyTarget(world);
        }
        auto pass = observe(world);
        const bool targetMet = int64_t(pass.militaryValue) + cheapestMilitaryOffer(world) > pass.militaryBudget;
        require(capped ? world.owner->isGroundUnitLimitReached() && !targetMet
                       : targetMet && !world.owner->isGroundUnitLimitReached(),
                "The army control did not isolate its target or its unit cap");
        require(armyCapacity(world, pass) == 0 && pass.sensibleArmyShare == 0 && pass.sensibleDrains.empty(),
                "A satisfied or capped army still reserved a share of the wallet");
        const auto spent = world.spentQ32();
        require(orderStructure(world, pass, world.yard, Structure_WindTrap,
                    capped ? "fixture_army_capped" : "fixture_army_target_met")
                    && world.queued(Structure_WindTrap) == 1 && cashQ32(world) == 0 && world.spentQ32() == spent,
                "Without an army entitlement, development did not start from zero cash as sole primary");
        requireProfile(world, pass, world.yard);
        SDL_Log("START_WHAT_MAKES_SENSE_ARMY_SHARE: case=%s fielded=%d military=%d budget=%d share_bps=0 zero_cash_start=1",
            capped ? "unit_cap" : "target_met", fielded, pass.militaryValue, pass.militaryBudget);
    }
}

// An economic import is a real up-front debit. Its selected quote is not a
// hypothetical builder drain: when short by one credit it protects that cash
// from a lower-priority lot, even with no running work (sole-primary path).
inline void immediateImportSaving(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::Complete, Army::Empty, 8, false,
                HOUSE_HARKONNEN, 0, 1);
    configure(world, true);
    TechProductionProbe::spawnGroundBand(world, world.owner, Unit_Tank, 1, Coord(20, 56), 0, 8);
    auto* port = dynamic_cast<StarPort*>(world.producer(Structure_StarPort));
    require(port && port->okToOrder() && world.owner->isGroundUnitLimitReached(),
            "Immediate-import saving control lacks its real ready port or capped army");
    auto& market = world.owner->getChoam();
    market.availableItems.clear();
    market.addItem(Unit_Carryall, 4);
    market.availableItems.back().price = world.price(Unit_Carryall);
    port->updateBuildList();
    const int price = world.bot->purchasePrice(port, Unit_Carryall);
    require(price > world.price(Structure_ZoneResidential) && port->isAvailableToBuild(Unit_Carryall),
            "Immediate-import saving control lacks a legal quote above its competing lot");
    world.cash(price - 1);
    auto pass = observe(world);
    require(pass.sensibleArmyShare == 0 && pass.sensibleDrains.empty(),
            "Immediate-import saving control has another live or reserved spending claim");
    QuantBot::BuilderTurn turn(pass.money);
    turn.pStructure = port;
    turn.pBuilder = port;
    world.bot->beginBuilderTurn(pass, turn);
    const auto cash = cashQ32(world), spent = world.spentQ32();
    require(!world.bot->produceItemWithLogging(pass, turn, Unit_Carryall, __LINE__, "fixture_immediate_import"),
            "An unaffordable import borrowed the missing cash");
    require(pass.sensiblePending.builder == port->getObjectID()
                && pass.sensiblePending.purpose == Unit_Carryall
                && pass.sensiblePending.immediate == FixPoint(price).getRawValue()
                && pass.sensiblePending.unpaid == 0 && pass.sensiblePending.perCycle == 0,
            "The refused real import did not retain its immediate cash floor");
    require(!orderStructure(world, pass, world.yard, Structure_ZoneResidential, "fixture_import_savings_lot")
                && !queuedZone(world) && world.queued(Unit_Carryall) == 0
                && cashQ32(world) == cash && world.spentQ32() == spent,
            "A lower-priority sole-primary lot consumed the selected import's savings");
    // A labelled funded counterarm: the same import spends exactly its native
    // quote, clears only its own pending claim and does not borrow engine cash.
    world.cash(price);
    require(world.bot->produceItemWithLogging(pass, turn, Unit_Carryall, __LINE__, "fixture_immediate_import_funded")
                && world.queued(Unit_Carryall) == 1 && pass.sensiblePending.purpose == NONE_ID
                && cashQ32(world) == 0 && world.spentQ32() - spent == FixPoint(price).getRawValue(),
            "The funded real import did not pay exactly its quote and release its pending floor");
    SDL_Log("START_WHAT_MAKES_SENSE_IMPORT_SAVING: quote=%d short_cash=%d immediate_floor=1 "
            "no_live_work=1 cheap_lot_held=1 native_debit_exact=1", price, price - 1);
}

// Repeated discounted military imports share the real wallet. A fresh pass
// (as for another controller of this house) observes the first native debit
// and reconstructs the higher-priority transport's pending cash floor.
inline void immediateMilitaryRemainder(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::Complete, Army::Empty, 8, false, HOUSE_HARKONNEN, 0);
    configure(world, true);
    auto* port = dynamic_cast<StarPort*>(world.producer(Structure_StarPort));
    require(port && port->okToOrder(), "Repeated-import control lacks a real ready Starport");
    auto& market = world.owner->getChoam();
    market.availableItems.clear();
    market.addItem(Unit_Carryall, 4);
    market.availableItems.back().price = world.price(Unit_Carryall);
    market.addItem(Unit_Tank, 4);
    market.availableItems.back().price = 2; // Labelled native-market discount; the engine debits this exact quote.
    port->updateBuildList();
    const int transportPrice = world.bot->purchasePrice(port, Unit_Carryall);
    const int militaryPrice = world.bot->purchasePrice(port, Unit_Tank);
    require(port->isAvailableToBuild(Unit_Carryall) && port->isAvailableToBuild(Unit_Tank)
                && militaryPrice > 1 && militaryPrice < transportPrice,
            "Repeated-import control has no real discounted military/transport quotes");
    world.cash(transportPrice + militaryPrice + militaryPrice / 2);
    auto pass = observe(world);
    const auto import = [&](QuantBot::BuildPass& plan, Uint32 item) {
        QuantBot::BuilderTurn turn(plan.money);
        turn.pStructure = port;
        turn.pBuilder = port;
        world.bot->beginBuilderTurn(plan, turn);
        return world.bot->produceItemWithLogging(plan, turn, item, __LINE__, "fixture_import_remainder");
    };
    require(pass.sensibleArmyShare > 0 && pass.sensibleDrains.empty()
                && !import(pass, Unit_Carryall)
                && pass.sensiblePending.immediate == FixPoint(transportPrice).getRawValue(),
            "The selected transport did not retain its development cash-floor claim beside army demand");
    const auto spent = world.spentQ32(), cash = cashQ32(world);
    require(import(pass, Unit_Tank) && world.queued(Unit_Tank) == 1
                && cashQ32(world) == cash - FixPoint(militaryPrice).getRawValue()
                && world.spentQ32() - spent == FixPoint(militaryPrice).getRawValue()
                && pass.sensibleDrains.empty(),
            "First military import did not debit exactly once while leaving the transport funded");
    pass = observe(world);
    require(!import(pass, Unit_Carryall) && pass.sensiblePending.immediate == FixPoint(transportPrice).getRawValue()
                && shareOf(cashQ32(world), pass.sensibleArmyShare) >= FixPoint(militaryPrice).getRawValue()
                && cashQ32(world) >= FixPoint(transportPrice).getRawValue()
                && cashQ32(world) - FixPoint(militaryPrice).getRawValue() < FixPoint(transportPrice).getRawValue(),
            "Fresh-pass control does not separate the reused nominal army weight from the required remainder");
    require(!import(pass, Unit_Tank) && world.queued(Unit_Tank) == 1
                && world.spentQ32() - spent == FixPoint(militaryPrice).getRawValue()
                && cashQ32(world) == cash - FixPoint(militaryPrice).getRawValue(),
            "A repeated instant purchase consumed the pending transport's required real cash remainder");
    SDL_Log("START_WHAT_MAKES_SENSE_IMPORT_REMAINDER: transport=%d military=%d first_paid_once=1 "
            "fresh_pass=1 nominal_weight_fits_second=1 second_held=1 pending_remainder_preserved=1 no_fake_debt=1",
            transportPrice, militaryPrice);
}

inline void rollback(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::Complete, Army::Empty);
    configure(world, true);
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    world.owner->updateBuildLists();
    // The complete base's ready factories keep the army's reserved share, so the
    // package needs a declared balance for its development share.
    world.cash(10 * world.price(Structure_Palace));
    require(world.owner->getNumItems(Structure_Palace) == 1
                && world.game->getGameInitSettings().getGameOptions().onlyOnePalace
                && world.yard->isAvailableToBuild(Structure_Palace),
            "Rollback control lacks the real one-Palace engine rejection");
    auto pass = observe(world);
    QuantBot::BuilderTurn turn(pass.money);
    turn.pStructure = world.yard;
    turn.pBuilder = world.yard;
    world.bot->beginBuilderTurn(pass, turn);
    const auto cash = cashQ32(world), spent = world.spentQ32();
    require(!world.bot->queueFoundedStructure(pass, turn, Structure_Palace, __LINE__, "fixture_real_one_palace"),
            "A second Palace bypassed the actual one-Palace engine rule");
    require(pass.capitalOrderedCost > 0, "Rollback control never accepted any real foundation quotes");
    require(world.yard->getProductionQueueSize() == 0 && pass.sensibleDrains.empty()
                && world.bot->builderPlaceLocations[world.yard->getObjectID()].empty(),
            "Rejected founded package retained slabs, placement sites or a phantom cash profile");
    require(cashQ32(world) == cash && world.spentQ32() == spent,
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
    threatenedFirstFactory(playerClass);
    foundedStart(playerClass);
    parallelDrains(playerClass);
    underfundedPredecessor(playerClass);
    armyEntitlement(playerClass);
    immediateImportSaving(playerClass);
    immediateMilitaryRemainder(playerClass);
    rollback(playerClass);
    SDL_Log("START_WHAT_MAKES_SENSE_PASS: difficulty=%s full_bot_power_light_civic=1 off_wait=1 zero_start=1 low_cash=1 no_cheap_fallback=1 threatened_first_factory=1 imminent_power_first=1 blackout_power_first=1 quotes_not_paid=1 finite_parallel_drains=1 army_share_parallel=1 army_share_all_lanes_solvent=1 civil_overbook_held=1 immediate_import_saving=1 underfunded_predecessor_first=1 blackout_emergency_exception=1 army_reserve_not_whole_prefix=1 army_entitlement_need_ratio_target_cap=1 fractional_pause_resume=1 sequential_foundations=1 rollback_actual_queue=1",
        playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=start_what_makes_sense outcome=passed");
}
