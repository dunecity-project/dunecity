// Included only by campaign-balance-probe.inc (run-campaign-balance.py), never by the shipped game.
//
// Determinism fingerprint and loop timing for headless batches (docs/headless-performance.md).
// PROBE_DIGEST lines carry the lockstep digest (Network/GameStateDigest.h: cycle, shared random
// seed, house credits/counts, every object's id, item, owner, health and tile) extended with each
// object's exact fixed-point position and angle, its destination and target, and each house's
// exact stored/starting/city credits. Two builds that print the same lines for the same seed ran
// the same simulation; an engine optimisation must not change a single one of them.
// PROBE_TIMING separates the game loop from process start-up and game setup.
#include <network/GameStateDigest.h>

#include <chrono>
#include <cstdlib>

namespace ProbeDigest {

inline std::uint64_t deep(Game& game) {
    const GameStateDigest::Digest lockstep = game.computeStateDigest();
    GameStateDigest::Hasher hash;
    hash.mixUint32(lockstep.gameCycle);
    hash.mixUint32(lockstep.randomSeed);
    hash.mixUint32(lockstep.objectCount);
    hash.mixUint64(lockstep.objectHash);
    hash.mixUint64(lockstep.houseHash);
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const House* house = game.getHouse(static_cast<HOUSETYPE>(h));
        hash.mixUint8(house != nullptr ? 1 : 0);
        if (house == nullptr) continue;
        hash.mixInt64(static_cast<std::int64_t>(house->getStoredCredits().getRawValue()));
        hash.mixInt64(static_cast<std::int64_t>(house->getStartingCredits().getRawValue()));
        hash.mixInt64(static_cast<std::int64_t>(house->getCityCredits().getRawValue()));
    }
    game.getObjectManager().forEachObject([&](Uint32 objectID, const ObjectBase* object) {
        if (object == nullptr) return;
        hash.mixUint32(objectID);
        hash.mixInt64(static_cast<std::int64_t>(object->getRealX().getRawValue()));
        hash.mixInt64(static_cast<std::int64_t>(object->getRealY().getRawValue()));
        hash.mixInt64(static_cast<std::int64_t>(object->angle.getRawValue()));
        hash.mixInt32(object->getDestination().x);
        hash.mixInt32(object->getDestination().y);
        hash.mixUint32(object->target.getObjectID());
    });
    return hash.value();
}

/// The cycle interval of PROBE_DIGEST lines: BALANCE_DIGEST_CYCLES, default 750 (12 game seconds); 0 = off.
inline Uint32 interval() {
    static const Uint32 cycles = std::getenv("BALANCE_DIGEST_CYCLES")
        ? static_cast<Uint32>(std::strtoul(std::getenv("BALANCE_DIGEST_CYCLES"), nullptr, 10)) : 750;
    return cycles;
}

/// Logs a digest when the current cycle is on the interval.
inline void periodic(Game& game) {
    const Uint32 cycle = game.getGameCycleCount();
    if (interval() != 0 && cycle % interval() == 0)
        SDL_Log("PROBE_DIGEST: cycle=%u digest=%016llx", cycle, static_cast<unsigned long long>(deep(game)));
}

/// The final digest, whatever the interval.
inline void atEnd(Game& game) {
    SDL_Log("PROBE_DIGEST: cycle=%u digest=%016llx final=1", game.getGameCycleCount(),
            static_cast<unsigned long long>(deep(game)));
}

using Clock = std::chrono::steady_clock;

inline long long millisecondsSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

} // namespace ProbeDigest
