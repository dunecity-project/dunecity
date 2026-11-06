// Synthetic, physical city; all asserted generators use full QuantBot build,
// ordinary paid production and placement. Never depends on a private user save.
namespace CompactPowerProbe {
using TechProductionProbe::World;
using TechProductionProbe::Stage;
using TechProductionProbe::Army;
using TechProductionProbe::PaidProduction;
using TechProductionProbe::require;

void seed(World& world, bool enabled, int credits = 2600) {
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.cityCompactPower = enabled;
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

void controls(const char* playerClass) {
    {
        World world("dunecity", playerClass, false, Stage::NaturalOpening, Army::Empty);
        seed(world, true, 1999);
        world.build();
        require(world.queued(Structure_WindTrap) == 1 && world.queued(Structure_NuclearPlant) == 0,
                "Compact choice bypassed the separate funding policy");
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
        // Author a two-column strip beside the existing Windtrap: a 2x2
        // building plus a complete two-tile frontage, never a 3x3 plot.
        // Changing terrain alone retains road flags and can strand the old
        // road network underneath an otherwise legal small footprint.
        const Coord small(18, 11);
        for (int y = 0; y < currentGameMap->getSizeY(); ++y)
            for (int x = 0; x < currentGameMap->getSizeX(); ++x) {
                auto* tile = currentGameMap->getTile(x, y);
                if (!tile->hasAStructure()) {
                    tile->setRoad(false);
                    tile->setType(Terrain_Sand);
                }
            }
        for (int y = small.y - 1; y < small.y + 2; ++y)
            for (int x = small.x; x < small.x + 2; ++x)
                currentGameMap->getTile(x, y)->setType(Terrain_Rock);
        require(currentGameMap->okayToPlaceStructure(small.x, small.y, 2, 2,
                    false, world.owner, false, Structure_WindTrap),
                "Small generator control is not human-legal");
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
    SDL_Log("COMPACT_POWER_PASS: difficulty=%s negative_control=wind funded_on=nuclear paid=1 tech_control=1 footprint_control=1 pending_control=1",
            playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=compact_power outcome=passed");
}
