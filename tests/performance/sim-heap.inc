// Test-only retained-storage census, included in the access-relaxed balance probe.
// BALANCE_HEAP_PROFILE=1: snapshot at setup, every ten game minutes and at end.
// BALANCE_HEAP_TOOLS=1: also retain macOS heap/vmmap and malloc stack reports.
// Container bytes are requested capacity, not allocator overhead; RSS is separate.
#include <AStarSearch.h>
#include <Map.h>
#include <Tile.h>
#include <dunecity/CitySimulation.h>
#include <players/QuantBot.h>
#include <units/UnitBase.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace SimHeap {

template<class T> size_t bytes(const std::vector<T>& v) { return v.capacity() * sizeof(T); }

inline bool enabled() {
    static const bool value = std::getenv("BALANCE_HEAP_PROFILE") != nullptr;
    return value;
}

inline void snapshot(Game& game, bool final = false) {
    if (!enabled()) return;
    static Uint32 lastCycle = std::numeric_limits<Uint32>::max();
    const Uint32 cycle = game.getGameCycleCount();
    if (!final && lastCycle != std::numeric_limits<Uint32>::max() && cycle % 37500 != 0) return;
    if (cycle == lastCycle) return;
    lastCycle = cycle;
    size_t rss = 0, footprint = 0, allocated = 0, reserved = 0, objects = 0;
#ifdef __APPLE__
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &count) == KERN_SUCCESS)
        rss = basic.resident_size;
    task_vm_info_data_t vm{};
    count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS)
        footprint = vm.phys_footprint;
    malloc_statistics_t mallocStats{};
    malloc_zone_statistics(nullptr, &mallocStats);
    allocated = mallocStats.size_in_use;
    reserved = mallocStats.size_allocated;
#endif
    const auto* map = currentGameMap;
    size_t tileStorage = 0, tileDamage = 0, tileCorpses = 0, tileOccupants = 0, tileIndices = 0;
    if (map) {
        tileStorage = bytes(map->tiles);
        tileIndices = bytes(map->vehicleTerrainRegions) + bytes(map->activeDeadUnitTiles) + bytes(map->objectTiles);
        for (const auto& tile : map->tiles) {
            tileDamage += bytes(tile.damage);
            tileCorpses += bytes(tile.deadUnits);
            tileOccupants += std::distance(tile.assignedAirUnitList.begin(), tile.assignedAirUnitList.end())
                + std::distance(tile.assignedInfantryList.begin(), tile.assignedInfantryList.end())
                + std::distance(tile.assignedUndergroundUnitList.begin(), tile.assignedUndergroundUnitList.end())
                + std::distance(tile.assignedNonInfantryGroundObjectList.begin(), tile.assignedNonInfantryGroundObjectList.end());
        }
    }
    size_t history = bytes(game.cmdManager.timeslot), commands = 0, occupiedCycles = 0;
    for (const auto& slot : game.cmdManager.timeslot) {
        history += bytes(slot);
        occupiedCycles += !slot.empty();
        commands += slot.size();
        for (const auto& command : slot) history += bytes(command.parameter);
    }
    size_t aiDanger = 0, aiAnchors = 0, aiDistances = 0, aiSightings = 0, aiTelemetryEntries = 0, aiOther = 0;
    auto distanceBytes = [](const CityDistanceField& field) {
        return bytes(field.rects_) + bytes(field.start_) + bytes(field.members_);
    };
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
        for (const auto& player : house->getPlayerList()) if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) {
            aiDanger += bytes(bot->tacticalDanger) + bytes(bot->harvesterDanger) + bytes(bot->lossDanger)
                + bytes(bot->factoryEnemyClearance);
            aiAnchors += bytes(bot->anchorFieldCache.field.sat_);
            const auto& d = bot->placementDistances;
            aiDistances += bytes(d.key) + distanceBytes(d.nearestResidential) + distanceBytes(d.nearestCommercial)
                + distanceBytes(d.nearestIndustrial) + distanceBytes(d.pollutionSeparation);
            aiSightings += bot->enemySightings.size();
            aiTelemetryEntries += bot->lastKiteTrace.size() + bot->lastMcvTrace.size() + bot->lastHarvesterSafetyTrace.size()
                + bot->lastHeavyAllocationTrace.size() + bot->placementScoreDetails.size() + bot->lastEconomyTraceCycle.size()
                + bot->zoneDecisionIds.size() + bot->lastZoneTraceCycle.size();
            aiOther += bytes(bot->recentStructureLosses) + bytes(bot->unsafeFields) + bytes(bot->visibleEnemyBases)
                + bytes(bot->visibleHarvestLaunchers) + bytes(bot->visibleGroundTurretThreats) + bytes(bot->cityProductionPlots);
        }
    }
    size_t city = 0;
    if (const auto* sim = game.getCitySimulation()) {
        city = sizeof(*sim) + bytes(sim->powerGridMap_.data_) + bytes(sim->trafficDensityMap_.data_)
            + bytes(sim->pollutionDensityMap_.data_) + bytes(sim->landValueMap_.data_)
            + bytes(sim->hostileLandValuePenaltyMap_.data_) + bytes(sim->crimeRateMap_.data_)
            + bytes(sim->policeCoverageMap_.data_) + bytes(sim->crimeBeforePoliceMap_.data_)
            + bytes(sim->populationDensityMap_.data_) + bytes(sim->growthRateMap_.data_) + bytes(sim->crimeUnrestProgress_);
    }
    size_t pathPool = 0, pathFrontiers = 0, pathNodes = 0;
    for (const auto& pool : AStarSearch::tilePool) pathPool += pool.capacity * sizeof(AStarSearch::TileData);
    for (const auto& job : game.activePathJobs) if (job.search) {
        pathFrontiers += bytes(job.search->openList) + bytes(job.search->depthCheckCount);
    }
    for (const auto* unit : unitList) pathNodes += unit->getPlannedPath().size();
    size_t objectCount = 0;
    game.getObjectManager().forEachObject([&](Uint32, const ObjectBase* object) {
        if (!object) return;
        ++objectCount;
#ifdef __APPLE__
        objects += malloc_size(object);
#endif
    });
    SDL_Log("SIM_HEAP: cycle=%u final=%d rss=%zu footprint=%zu malloc_live=%zu malloc_reserved=%zu "
            "tiles=%zu tile_damage=%zu tile_corpses=%zu tile_occupants=%zu tile_indices=%zu "
            "command_history=%zu commands=%zu command_cycles=%zu ai_danger=%zu ai_anchors=%zu ai_distances=%zu "
            "ai_sightings=%zu ai_telemetry_entries=%zu ai_other=%zu city=%zu path_pool=%zu path_frontiers=%zu "
            "path_nodes=%zu objects=%zu object_count=%zu tile_size=%zu path_tile_size=%zu",
        cycle, int(final), rss, footprint, allocated, reserved, tileStorage, tileDamage, tileCorpses, tileOccupants,
        tileIndices, history, commands, occupiedCycles, aiDanger, aiAnchors, aiDistances, aiSightings,
        aiTelemetryEntries, aiOther, city, pathPool, pathFrontiers, pathNodes, objects, objectCount,
        sizeof(Tile), sizeof(AStarSearch::TileData));
#ifdef __APPLE__
    if (std::getenv("BALANCE_HEAP_TOOLS")) {
        char command[512];
        const int pid = static_cast<int>(getpid());
        std::snprintf(command, sizeof(command),
            "MallocStackLogging=0 /usr/bin/heap -s --showSizes %d > heap-%u.txt 2>&1; "
            "MallocStackLogging=0 /usr/bin/vmmap -summary %d > vmmap-%u.txt 2>&1", pid, cycle, pid, cycle);
        const int toolsStatus = std::system(command);
        int stacksStatus = 0;
        if (std::getenv("MallocStackLogging")) {
            std::snprintf(command, sizeof(command),
                "MallocStackLogging=0 /usr/bin/malloc_history %d -allBySize > malloc-%u.txt 2>&1", pid, cycle);
            stacksStatus = std::system(command);
        }
        SDL_Log("SIM_HEAP_TOOLS: cycle=%u tools_status=%d stacks_status=%d", cycle, toolsStatus, stacksStatus);
    }
#endif
}

} // namespace SimHeap
