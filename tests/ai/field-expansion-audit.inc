// Read-only probe measurements. Initial connected spice components retain their
// identity after depletion; later spice spills are deliberately not new fields.
#include <map>
namespace FieldExpansionAudit {
inline const Game* owner = nullptr;
inline Uint32 sampledCycle = std::numeric_limits<Uint32>::max();
inline std::vector<int> membership, initial;
inline int width = 0, height = 0;
struct Travel {
    int house = -1, cargo = 0;
    Uint32 cycle = 0, returnStart = 0, outboundStart = 0;
    bool moving = false, active = false, returning = false;
};
inline std::map<Uint32,Travel> travel;
inline const Game* travelOwner = nullptr;
inline Uint32 observed = std::numeric_limits<Uint32>::max();
struct Totals {
    uint64_t movingCycles = 0, activeCycles = 0;
    uint64_t returnCycles = 0, returnTrips = 0, outboundCycles = 0, outboundTrips = 0;
};
inline std::array<Totals,NUM_HOUSES> totals;
inline void observe(Game& game) {
    const Uint32 now = game.getGameCycleCount();
    if (travelOwner != &game || now < observed) {
        travelOwner = &game; travel.clear(); totals = {};
    }
    if (now == observed) return;
    observed = now;
    for (const auto* unit : unitList) {
        if (unit->getItemID() != Unit_Harvester || !unit->getOwner()) continue;
        const auto* harvester = static_cast<const Harvester*>(unit);
        const int house = unit->getOwner()->getHouseID();
        auto& state = travel[unit->getObjectID()];
        if (state.house != house) { state = {}; state.house = house; state.cycle = now; }
        auto& sum = totals[house];
        const Uint32 elapsed = now - state.cycle;
        sum.movingCycles += state.moving ? elapsed : 0;
        sum.activeCycles += state.active ? elapsed : 0;
        const int cargo = harvester->getAmountOfSpice().lround();
        if (harvester->isReturning() && !state.returning) state.returnStart = now;
        // First actual unloading, not becoming inactive inside a carryall.
        if (state.returnStart && cargo < state.cargo) {
            sum.returnCycles += now - state.returnStart; ++sum.returnTrips; state.returnStart = 0;
        }
        // Includes refinery release/transport waits; reported separately from
        // pure movement and from the return-to-first-unload interval.
        if (cargo == 0 && state.cargo > 0) state.outboundStart = now;
        if (state.outboundStart && harvester->isHarvesting()) {
            sum.outboundCycles += now - state.outboundStart; ++sum.outboundTrips; state.outboundStart = 0;
        }
        state.cycle = now; state.cargo = cargo;
        state.returning = harvester->isReturning();
        state.moving = harvester->isMoving(); state.active = harvester->isActive();
    }
}
inline void sample(Game& game, int minute) {
    const auto cycle = game.getGameCycleCount();
    if (owner != &game || cycle < sampledCycle || membership.empty()) {
        owner = &game;
        width = currentGameMap->getSizeX(); height = currentGameMap->getSizeY();
        membership.assign(width * height, -1); initial.clear();
        std::vector<int> flood;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int start = y * width + x;
            if (membership[start] >= 0 || !currentGameMap->getTile(x,y)->hasSpice()) continue;
            const int field = static_cast<int>(initial.size());
            initial.push_back(0); flood.clear(); flood.push_back(start); membership[start] = field;
            for (size_t at = 0; at < flood.size(); ++at) {
                const int p = flood[at], px = p % width, py = p / width;
                initial[field] += currentGameMap->getTile(px,py)->getSpice().lround();
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = px + dx, ny = py + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const int next = ny * width + nx;
                    if (membership[next] >= 0 || !currentGameMap->getTile(nx,ny)->hasSpice()) continue;
                    membership[next] = field; flood.push_back(next);
                }
            }
        }
    }
    sampledCycle = cycle;
    std::vector<int> remaining(initial.size(), 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        if (const int field = membership[y * width + x]; field >= 0)
            remaining[field] += currentGameMap->getTile(x,y)->getSpice().lround();
    for (size_t field = 0; field < initial.size(); ++field)
        SDL_Log("SPICE_FIELD_AUDIT: minute=%d cycle=%u field=%zu initial=%d remaining=%d",
            minute,cycle,field,initial[field],remaining[field]);
    for (const House* house : EliminationMatch::playingHouses(game)) {
        std::vector<const StructureBase*> refs;
        for (const auto* s : structureList)
            if (s->getOwner() == house && s->getHealth() > 0 && s->acceptsHarvesterDropoff()) refs.push_back(s);
        int workers = 0, moving = 0, harvesting = 0, travel = 0, local = 0;
        for (const auto* u : unitList) {
            const auto* h = dynamic_cast<const Harvester*>(u);
            if (!h || h->getOwner() != house || !h->isActive() || !h->getLocation().isValid()) continue;
            ++workers; moving += h->isMoving(); harvesting += h->isHarvesting();
            int distance = width + height;
            for (const auto* ref : refs)
                distance = std::min(distance,blockDistance(h->getLocation(),ref->getClosestPoint(h->getLocation())).lround());
            if (!refs.empty()) travel += distance;
        }
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto* tile = currentGameMap->getTile(x,y);
            if (!tile->hasSpice()) continue;
            for (const auto* ref : refs) if (blockDistance(Coord(x,y),ref->getLocation()) <= 16) {
                local += tile->getSpice().lround(); break;
            }
        }
        SDL_Log("FIELD_TRAVEL_AUDIT: minute=%d cycle=%u house=%d field_workers=%d field_moving=%d field_harvesting=%d"
                " field_travel_sum=%d field_local_spice=%d field_refineries=%zu",
            minute,cycle,house->getHouseID(),workers,moving,harvesting,travel,local,refs.size());
        const auto& sum = totals[house->getHouseID()];
        SDL_Log("FIELD_TRAVEL_AUDIT: minute=%d cycle=%u house=%d field_moving_cycles=%llu field_active_cycles=%llu"
                " field_return_cycles=%llu field_return_trips=%llu field_outbound_cycles=%llu field_outbound_trips=%llu",
            minute,cycle,house->getHouseID(),static_cast<unsigned long long>(sum.movingCycles),
            static_cast<unsigned long long>(sum.activeCycles),static_cast<unsigned long long>(sum.returnCycles),
            static_cast<unsigned long long>(sum.returnTrips),static_cast<unsigned long long>(sum.outboundCycles),
            static_cast<unsigned long long>(sum.outboundTrips));
    }
}
}
