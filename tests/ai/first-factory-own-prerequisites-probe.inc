// Synthetic, physical city; every asserted order comes from the full QuantBot
// build pass, with ordinary paid production and placement. Never depends on a
// private user save (the reported save is measured separately, never committed).
namespace FirstFactoryOwnPrerequisitesProbe {
using TechProductionProbe::World;
using TechProductionProbe::Stage;
using TechProductionProbe::Army;
using TechProductionProbe::PaidProduction;

inline void require(bool ok, const char* reason) {
    if (!ok) {
        SDL_Log("FIRST_FACTORY_OWN_PREREQUISITES_FAIL: %s", reason);
        throw std::runtime_error(reason);
    }
}

constexpr int kTechnology = 2;

inline int generators(const World& world) {
    return world.queued(Structure_WindTrap) + world.queued(Structure_NuclearPlant);
}

inline bool queuedZone(const World& world) {
    return world.queued(Structure_ZoneResidential) || world.queued(Structure_ZoneCommercial)
        || world.queued(Structure_ZoneIndustrial);
}

inline const QuantBot::CapitalCandidate* candidate(const QuantBot::BuildPass& pass, Uint32 item) {
    for (const auto& offer : pass.capitalCandidates)
        if (offer.item == item) return &offer;
    return nullptr;
}

inline bool plannedFirstFactory(const QuantBot::BuildPass& pass) {
    for (const auto& offer : pass.capitalCandidates)
        if (std::string_view(offer.kind) == "first_factory") return true;
    return false;
}

// A technology-2 Ordos city at work, as in the reported save: generation, a
// refinery (its vault), storage and a road-served district on a spiceless
// map, but no ground producer. The Light Factory's own prerequisites are
// met; the Heavy Factory is not legal at this technology, so the funded
// factory opening can never start. Generation covers the factory's own draw
// and full maturation of the existing lots, not the forecast city reserve.
inline void seed(World& world, bool enabled) {
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.firstFactoryOwnPrerequisites = enabled;
    config->force.startWhatMakesSense = false;
    world.bot->configOverride = config;
    world.configHash = config->getConfigHash();
    world.game->techLevel = kTechnology;
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    for (int y = 0; y < currentGameMap->getSizeY(); ++y)
        for (int x = 0; x < currentGameMap->getSizeX(); ++x) {
            auto* tile = currentGameMap->getTile(x, y);
            if (tile->isSpice() || tile->hasSpice()) {
                tile->setType(Terrain_Sand);
                tile->setSpice(0);
            }
        }
    world.bot->lastCalculatedSpice = 0;
    world.placeAt(Structure_WindTrap, Coord(18, 8));
    world.placeAt(Structure_Refinery, Coord(3, 18));
    world.placeAt(Structure_Silo, Coord(10, 18));
    for (int i = 0; i < 2; ++i) world.placeAt(Structure_ZoneResidential, Coord(18 + 3 * i, 24));
    world.placeAt(Structure_ZoneCommercial, Coord(24, 24));
    world.placeAt(Structure_ZoneIndustrial, Coord(27, 24));
    for (int y : {23, 26}) for (int x = 17; x < 30; ++x)
        if (!currentGameMap->getTile(x, y)->hasAStructure()) {
            world.owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
            require(currentGameMap->getTile(x, y)->isRoad(), "First-factory city lost its real road-served district");
        }
    world.game->gameCycleCount = MILLI2CYCLES(300000);
    world.initializePhysicalCity();
    world.closeMilitaryMarket();
    const int factoryPower = std::max(0,
        world.game->objectData.data[Structure_LightFactory][world.owner->getHouseID()].power);
    for (int assets = 0;; ++assets) {
        require(assets < 64, "Cannot establish the physical first-factory power boundary");
        const auto pass = world.observePlan();
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
        break;
    }
    world.owner->updateBuildLists();
    for (Uint32 item : {Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory,
                       Structure_StarPort})
        require(world.owner->getNumItems(item) == 0, "First-factory city already owns a vehicle supplier");
    world.bot->planningBuilder = world.yard->getObjectID();
    world.bot->clearPlacementCache();
    require(world.owner->getTechLevel() == kTechnology && world.yard->isAvailableToBuild(Structure_LightFactory)
                && !world.yard->isAvailableToBuild(Structure_HeavyFactory),
            "Light Factory is not legal on its own prerequisites, or the Heavy Factory chain is legal");
    require(world.owner->hasPower() && world.bot->findPlaceLocation(Structure_LightFactory).isValid(),
            "First-factory city has no powered legal factory site");
}

// The planner's actual state: the factory's own draw fits, the city's
// forecast reserve does not, lots are demanded and the opening is out of reach.
inline QuantBot::BuildPass requireBoundary(World& world, bool enabled) {
    auto pass = world.observePlan();
    const int factoryPower = std::max(0,
        world.game->objectData.data[Structure_LightFactory][world.owner->getHouseID()].power);
    const int latent = std::max(0, pass.matureZonePower - pass.currentZonePower);
    const int headroom = world.owner->getProducedPower() - world.owner->getPowerRequirement();
    require(headroom >= factoryPower + latent && headroom < pass.cityPowerReserve,
            "Fixture does not separate the factory's actual draw from forecast-only reserve");
    require(pass.ownResValve > 0 || pass.ownComValve > 0 || pass.ownIndValve > 0,
            "First-factory city has no live competing growth demand");
    require(!pass.spiceEconomyViable && !world.bot->cityBootstrapNeeded(pass),
            "First-factory city is not a seeded spiceless economy");
    require(!pass.fundedFactoryOpening, "The funded factory opening could already reach the Light Factory");
    const auto* factory = candidate(pass, Structure_LightFactory);
    require(factory != nullptr, "Planner did not consider the missing Light Factory at all");
    if (enabled) {
        require(std::string_view(factory->kind) == "first_factory"
                    && std::string_view(factory->reason) == "missing_ground_producer" && factory->score > 0
                    && factory->site.isValid() && pass.capitalChoice >= 0
                    && &pass.capitalCandidates[pass.capitalChoice] == factory && pass.protectionCapital,
                "ON did not plan the legal first Light Factory as protected capital");
    } else {
        require(std::string_view(factory->reason) == "bootstrap_managed_separately" && factory->score == 0,
                "OFF changed the Light Factory's separately-managed candidate");
    }
    return pass;
}

inline std::string foundedArm(const char* playerClass, bool enabled) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty, 8, false, HOUSE_ORDOS);
    seed(world, enabled);
    world.cash(2000);
    const auto pass = requireBoundary(world, enabled);
    const int64_t spentBefore = world.spentQ32();
    PaidProduction paid(world);
    world.build(); paid.observe();
    require(world.queued(Structure_LightFactory) == int(enabled),
            "Full bot did not order the legal first Light Factory only when enabled");
    if (enabled) {
        require(generators(world) == 0 && !queuedZone(world),
                "Forecast-only power or city growth displaced the first Light Factory");
    } else {
        require(generators(world) > 0 || queuedZone(world),
                "OFF control did not spend the yard on forecast power or growth instead");
    }
    const int offGenerators = generators(world);
    const bool offZone = queuedZone(world);
    world.build(); paid.observe();
    require(world.queued(Structure_LightFactory) == int(enabled), "Pending first Light Factory was ordered twice");
    Uint32 elapsed = 0;
    if (enabled) {
        const Uint32 first = world.game->getGameCycleCount(), end = first + MILLI2CYCLES(180000);
        while (world.game->getGameCycleCount() < end && !paid.completed(Structure_LightFactory)) paid.step();
        require(paid.completed(Structure_LightFactory) && world.owner->getNumItems(Structure_LightFactory) == 1,
                "First Light Factory did not finish ordinary paid production and placement");
        require(world.spentQ32() - spentBefore >= (int64_t(world.price(Structure_LightFactory)) << 32),
                "First Light Factory completed without the actual house credit payments");
        elapsed = world.game->getGameCycleCount() - first;
        require(!plannedFirstFactory(world.observePlan()), "An owned Light Factory still planned another first factory");
    }
    SDL_Log("FIRST_FACTORY_OWN_PREREQUISITES_CASE: difficulty=%s enabled=%d case=founded tech=%d power=%d/%d "
            "reserve=%d lf_queued=%d generators=%d zones=%d elapsed=%u hash=%s",
            playerClass, int(enabled), world.owner->getTechLevel(), world.owner->getProducedPower(),
            world.owner->getPowerRequirement(), pass.cityPowerReserve, int(enabled), offGenerators,
            int(offZone), elapsed, world.configHash.c_str());
    return world.configHash;
}

// Below the founded quote the legacy plan saves: no cheaper lot or generator
// spends the savings, and the factory starts once its quote is in hand.
inline void savingArm(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty, 8, false, HOUSE_ORDOS);
    seed(world, true);
    world.cash(0);
    const auto plan = requireBoundary(world, true);
    const auto* factory = candidate(plan, Structure_LightFactory);
    const int quote = factory->price + factory->foundationCost;
    require(quote - 1 >= world.price(Structure_ZoneResidential) && quote - 1 >= world.price(Structure_WindTrap),
            "Saving control cannot afford its competing lot and generator");
    world.cash(quote - 1);
    const int64_t spentBefore = world.spentQ32();
    world.build();
    require(world.queued(Structure_LightFactory) == 0 && generators(world) == 0 && !queuedZone(world),
            "An unfunded first Light Factory's savings bought a lot or forecast generator");
    require(world.owner->getCredits() == quote - 1 && world.spentQ32() == spentBefore,
            "Saving for the first Light Factory moved real cash");
    world.cash(quote);
    world.build();
    require(world.queued(Structure_LightFactory) == 1, "Saved first Light Factory did not start at its founded quote");
    SDL_Log("FIRST_FACTORY_OWN_PREREQUISITES_CASE: difficulty=%s enabled=1 case=saving quote=%d price=%d "
            "foundation=%d held=1 started=1 hash=%s",
            playerClass, quote, factory->price, factory->foundationCost, world.configHash.c_str());
}

// An actual blackout keeps its generator first; the factory is not planned
// while its own draw does not fit.
inline void blackoutArm(const char* playerClass) {
    World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty, 8, false, HOUSE_ORDOS);
    seed(world, true);
    for (int consumers = 0; world.owner->hasPower(); ++consumers) {
        require(consumers < 16, "Physical blackout control did not exhaust actual generation");
        world.place(Structure_Radar);
    }
    world.owner->updateBuildLists();
    world.cash(2000);
    require(!plannedFirstFactory(world.observePlan()), "Blackout planned the first Light Factory before its power");
    world.build();
    require(world.queued(Structure_LightFactory) == 0 && generators(world) > 0,
            "Actual blackout did not keep its generator ahead of the first Light Factory");
    SDL_Log("FIRST_FACTORY_OWN_PREREQUISITES_CASE: difficulty=%s enabled=1 case=blackout power=%d/%d "
            "lf_queued=0 generators=%d hash=%s",
            playerClass, world.owner->getProducedPower(), world.owner->getPowerRequirement(),
            generators(world), world.configHash.c_str());
}
} // namespace FirstFactoryOwnPrerequisitesProbe

void runFirstFactoryOwnPrerequisitesProbe(const char* playerClass) {
    using namespace FirstFactoryOwnPrerequisitesProbe;
    require(!QuantBotConfig().force.firstFactoryOwnPrerequisites, "FirstFactoryOwnPrerequisites must remain default OFF");
    const std::string off = foundedArm(playerClass, false);
    const std::string on = foundedArm(playerClass, true);
    require(off != on, "FirstFactoryOwnPrerequisites is not part of the multiplayer config hash");
    savingArm(playerClass);
    blackoutArm(playerClass);
    SDL_Log("FIRST_FACTORY_OWN_PREREQUISITES_PASS: difficulty=%s off_no_factory=1 "
            "on_factory_before_forecast_power_and_growth=1 paid_completion=1 founded_saving=1 "
            "blackout_power_first=1 no_repeat=1 hashed=1", playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=first_factory_own_prerequisites outcome=passed");
}
