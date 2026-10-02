// Included only by run-ai-cache-probe.py, never by the shipped game.
//
// Drives the production QuantBot::findPlaceLocation against a real city save and
// asserts on the production placement caches themselves -- the per-item location
// cache and the distance-field cache's validity flags, key and rebuild counters.
// Nothing here re-derives the cache key: a reuse is distinguished from a silent
// rebuild only by the counters the production code increments.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <players/QuantBot.h>
#include <structures/StructureBase.h>
#include <mod/ModManager.h>
#include <memory>
#include <vector>

namespace {

int gChecks = 0;

void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("AI_CACHE_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
    ++gChecks;
}

} // namespace

int runAiCacheProbe() {
    ModManager::instance().setActiveMod("dunecity");
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // No interactive briefing or public match analytics.
    game->initGame(GameInitSettings(std::string(std::getenv("AI_CACHE_PROBE_SAVE"))));
    require(game->isCitySimEnabled(), "Fixture save has the city simulation enabled");

    // A crowded base can reject every candidate before scoring, in which case the
    // distance fields are never consulted and there is nothing to test. So find a
    // bot that actually reaches the scoring stage for both a sensitive and a
    // polluting item, and report the whole matrix either way.
    QuantBot* bot = nullptr;
    Uint32 sensitiveItem = NONE_ID, polluterItem = NONE_ID;
    const Uint32 sensitiveCandidates[] = {Structure_ZoneResidential, Structure_ZoneCommercial};
    const Uint32 polluterCandidates[] = {Structure_ZoneIndustrial, Structure_HeavyFactory, Structure_NuclearPlant};
    for(int houseID = 0; houseID < NUM_HOUSES && bot == nullptr; ++houseID) {
        House* candidateHouse = game->getHouse(houseID);
        if(candidateHouse == nullptr || !candidateHouse->isAlive()) continue;
        for(const auto& player : candidateHouse->getPlayerList()) {
            auto* candidate = dynamic_cast<QuantBot*>(player.get());
            if(candidate == nullptr) continue;
            Uint32 sensitiveHit = NONE_ID, polluterHit = NONE_ID;
            for(const Uint32 item : sensitiveCandidates) {
                candidate->clearPlacementCache(true, false);
                const Uint32 before = candidate->placementDistances.originBuilds;
                candidate->findPlaceLocation(item);
                const bool built = candidate->placementDistances.originBuilds > before;
                SDL_Log("AI_CACHE_PROBE_SCAN: house=%d item=%u sensitive built=%d", houseID, item, built?1:0);
                if(built && candidate->placementDistances.sensitive) { sensitiveHit = item; break; }
            }
            for(const Uint32 item : polluterCandidates) {
                candidate->clearPlacementCache(true, false);
                const Uint32 before = candidate->placementDistances.pollutionBuilds;
                candidate->findPlaceLocation(item);
                const bool built = candidate->placementDistances.pollutionBuilds > before;
                SDL_Log("AI_CACHE_PROBE_SCAN: house=%d item=%u polluter built=%d", houseID, item, built?1:0);
                if(built && candidate->placementDistances.polluter
                    && !candidate->placementDistances.sensitive) { polluterHit = item; break; }
            }
            if(sensitiveHit != NONE_ID && polluterHit != NONE_ID) {
                bot = candidate; sensitiveItem = sensitiveHit; polluterItem = polluterHit; break;
            }
        }
    }
    require(bot != nullptr, "A QuantBot reaches placement scoring for a sensitive and a polluting item");
    House* house = game->getHouse(bot->getHouse()->getHouseID());
    SDL_Log("AI_CACHE_PROBE_SUBJECT: house=%d structures=%d units=%d sensitive_item=%u polluter_item=%u",
        house->getHouseID(), house->getNumStructures(), house->getNumUnits(), sensitiveItem, polluterItem);

    auto& cache = bot->placementDistances;
    const Uint32 neutralItem = Structure_Wall; // Neither sensitive nor polluting.

    auto freshPass = [&]() {
        // The ordinary start of a planning pass: clear both caches.
        bot->clearPlacementCache(true, false);
        require(bot->placementCache.empty(), "A fresh pass starts with no cached locations");
        require(!cache.originFieldsValid && !cache.pollutionValid, "A fresh pass starts with invalid fields");
    };

    // ---------- 1. a repeated request reuses the per-item location cache ----------
    freshPass();
    const Uint32 originBuildsAtStart = cache.originBuilds;
    const Coord first = bot->findPlaceLocation(sensitiveItem);
    require(bot->placementCache.count(sensitiveItem) == 1, "The first search populated the location cache");
    const Uint32 originBuildsAfterFirst = cache.originBuilds;
    const Uint32 pollutionBuildsAfterFirst = cache.pollutionBuilds;
    require(originBuildsAfterFirst > originBuildsAtStart, "The first sensitive search built the origin fields");
    require(cache.originFieldsValid && cache.pollutionValid, "The first search left both field groups valid");
    require(cache.sensitive, "The cached pollution field records the sensitive role");
    require(cache.width == currentGameMap->getSizeX() && cache.height == currentGameMap->getSizeY(),
        "The cached fields match the map size");
    require(!cache.key.empty(), "The cached key recorded contributors");
    const size_t contributors = cache.key.size();

    const Coord repeated = bot->findPlaceLocation(sensitiveItem);
    require(repeated == first, "A repeated request returns the cached location");
    require(cache.originBuilds == originBuildsAfterFirst, "A location-cache hit rebuilds no origin field");
    require(cache.pollutionBuilds == pollutionBuildsAfterFirst, "A location-cache hit rebuilds no pollution field");

    // ---------- 2. a different item reuses the item-independent origin fields ----------
    // Same geometry, different planned item: the nearest-origin fields do not
    // depend on the item, so they must be reused while the pollution field,
    // whose roles changed, must be rebuilt.
    const Coord polluterSite = bot->findPlaceLocation(polluterItem);
    require(cache.originBuilds == originBuildsAfterFirst, "A new item reuses the origin fields");
    require(cache.pollutionBuilds > pollutionBuildsAfterFirst, "A changed role rebuilds the pollution field");
    require(cache.polluter, "The cached pollution field records the polluter role");
    require(cache.key.size() == contributors, "Unchanged geometry keeps the same contributor count");
    const Uint32 pollutionBuildsAfterPolluter = cache.pollutionBuilds;

    // Asking for the same roles again rebuilds nothing at all.
    bot->placementCache.erase(polluterItem);
    const Coord polluterAgain = bot->findPlaceLocation(polluterItem);
    require(polluterAgain == polluterSite, "A forced location-cache miss reproduces the cached site");
    require(cache.originBuilds == originBuildsAfterFirst, "Identical inputs rebuild no origin field");
    require(cache.pollutionBuilds == pollutionBuildsAfterPolluter, "Identical inputs rebuild no pollution field");

    // ---------- 3. an item with neither role leaves the fields untouched ----------
    const Uint32 originBuildsBeforeNeutral = cache.originBuilds;
    const Uint32 pollutionBuildsBeforeNeutral = cache.pollutionBuilds;
    bot->findPlaceLocation(neutralItem);
    require(cache.originBuilds == originBuildsBeforeNeutral, "A role-free item builds no origin field");
    require(cache.pollutionBuilds == pollutionBuildsBeforeNeutral, "A role-free item builds no pollution field");

    // ---------- 4. a contributor change invalidates ----------
    // Losing an owned structure changes the contribution list, so the fields must
    // be rebuilt even though the planned item and the builder are unchanged.
    StructureBase* victim = nullptr;
    for(StructureBase* structure : structureList) {
        if(structure->getOwner() == house && structure->getHealth() > 0) { victim = structure; break; }
    }
    require(victim != nullptr, "Found an owned structure to remove from the contributor set");
    const FixPoint victimHealth = victim->getHealth();
    victim->setHealth(0); // Drops out of the "live owned structure" contributor set.
    bot->placementCache.erase(polluterItem);
    const Uint32 originBuildsBeforeLoss = cache.originBuilds;
    const Uint32 pollutionBuildsBeforeLoss = cache.pollutionBuilds;
    bot->findPlaceLocation(polluterItem);
    require(cache.originBuilds > originBuildsBeforeLoss, "A lost contributor rebuilds the origin fields");
    require(cache.pollutionBuilds > pollutionBuildsBeforeLoss, "A lost contributor rebuilds the pollution field");
    require(cache.key.size() == contributors - 1, "The contributor count dropped by the lost structure");
    victim->setHealth(victimHealth);
    bot->placementCache.erase(polluterItem);
    const Uint32 originBuildsBeforeRestore = cache.originBuilds;
    bot->findPlaceLocation(polluterItem);
    require(cache.originBuilds > originBuildsBeforeRestore, "Restoring the contributor rebuilds again");
    require(cache.key.size() == contributors, "The contributor count returned to its original value");

    // ---------- 5. a reservation change invalidates ----------
    // Reservations belonging to other builders contribute; the planning builder's
    // own reservation is excluded, so moving the exclusion must invalidate too.
    const Uint32 otherBuilder = 0x51A7E1u;
    bot->reservedStructures[otherBuilder] = {polluterItem, Coord(10,10)};
    bot->placementCache.erase(polluterItem);
    const Uint32 originBuildsBeforeReservation = cache.originBuilds;
    bot->findPlaceLocation(polluterItem);
    require(cache.originBuilds > originBuildsBeforeReservation, "A new reservation rebuilds the fields");
    require(cache.key.size() == contributors + 1, "The reservation joined the contributor set");

    // Excluding that reservation by making it the planning builder's own must
    // change the contribution list again -- this is the case a builder-identity
    // key gets wrong, so it is asserted on the production key directly.
    const Uint32 previousPlanningBuilder = bot->planningBuilder;
    bot->planningBuilder = otherBuilder;
    bot->placementCache.erase(polluterItem);
    const Uint32 originBuildsBeforeExclusion = cache.originBuilds;
    bot->findPlaceLocation(polluterItem);
    require(cache.originBuilds > originBuildsBeforeExclusion, "Excluding the planning builder's reservation rebuilds");
    require(cache.key.size() == contributors, "The planning builder's own reservation is excluded");
    bot->planningBuilder = previousPlanningBuilder;
    bot->reservedStructures.erase(otherBuilder);

    // ---------- 6. a forced cache miss reproduces the cached decision ----------
    // Every field cache state must produce the same placement as a cold start.
    freshPass();
    const Coord cold = bot->findPlaceLocation(sensitiveItem);
    require(cold == first, "A cold pass reproduces the original sensitive placement");
    const Coord coldPolluter = bot->findPlaceLocation(polluterItem);
    freshPass();
    const Coord coldPolluterFirst = bot->findPlaceLocation(polluterItem);
    require(coldPolluterFirst == coldPolluter,
        "The polluter placement is the same whether or not the origin fields were warm");

    // ---------- 7. clearPlacementCache releases the field memory ----------
    bot->clearPlacementCache(true, false);
    require(!cache.originFieldsValid && !cache.pollutionValid, "Clearing invalidates both field groups");
    require(cache.key.empty(), "Clearing drops the contributor key");
    require(cache.width == 0 && cache.height == 0, "Clearing releases the map-sized cells");

    SDL_Log("AI_CACHE_PROBE_PASS: checks=%d contributors=%zu origin_builds=%u pollution_builds=%u first=%d,%d",
        gChecks, contributors, cache.originBuilds, cache.pollutionBuilds, first.x, first.y);
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    return MENU_QUIT_DEFAULT;
}
