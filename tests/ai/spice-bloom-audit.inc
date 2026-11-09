// Read-only probe measurements of spice blooms (docs/ai-scenario-matrix.md, "Spice bloom
// harvest"), the same in every arm. A bloom that disappears marks the tiles it turned into spice
// (spice now, not spice at the last refresh of its spill disc); a harvester that loads cargo
// while standing on such a tile counts that cargo as bloom-field harvest for its house. Per
// house the line also carries the blooms the house set off (the engine's spice_bloom_burst
// ledger: decision telemetry, --attack-events, only) and QuantBot's SpiceBloomHarvest fire
// orders and blooms held back as wasteful.
#include <map>
namespace SpiceBloomAudit {
/// Tile::triggerSpiceBloom spills over plain sand within this radius (Map::createSpiceField).
constexpr int kSpillRadius = 5;
constexpr int kSpan = 2 * kSpillRadius + 1;
/// How often a live bloom's spill disc is re-read.
constexpr Uint32 kRefreshCycles = 32;
struct Bloom { Coord at; std::array<bool, kSpan * kSpan> spice{}; };
struct Cargo { int house = -1; FixPoint load = 0; };
inline const Game* owner = nullptr;
inline Uint32 observed = std::numeric_limits<Uint32>::max();
inline int width = 0, height = 0, initialBlooms = 0, burstBlooms = 0, fieldTiles = 0;
inline std::vector<Bloom> blooms;
inline std::vector<uint8_t> field;
inline std::map<Uint32, Cargo> cargo;
inline std::array<FixPoint, NUM_HOUSES> harvested{};

inline bool inSpill(int dx, int dy) { return distanceFrom(Coord(0, 0), Coord(dx, dy)) <= kSpillRadius; }

inline void refresh(Bloom& bloom) {
    for (int dx = -kSpillRadius; dx <= kSpillRadius; ++dx)
        for (int dy = -kSpillRadius; dy <= kSpillRadius; ++dy) {
            const Coord at = bloom.at + Coord(dx, dy);
            bloom.spice[(dx + kSpillRadius) * kSpan + dy + kSpillRadius] =
                currentGameMap->tileExists(at) && currentGameMap->getTile(at)->isSpice();
        }
}

inline void observe(Game& game) {
    const Uint32 now = game.getGameCycleCount();
    if (owner != &game || now < observed) {
        owner = &game;
        width = currentGameMap->getSizeX(); height = currentGameMap->getSizeY();
        blooms.clear(); cargo.clear(); harvested = {};
        field.assign(size_t(width) * height, 0);
        burstBlooms = fieldTiles = 0;
        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y)
                if (currentGameMap->getTile(x, y)->isSpiceBloom()) {
                    blooms.push_back({Coord(x, y)});
                    refresh(blooms.back());
                }
        initialBlooms = static_cast<int>(blooms.size());
    }
    if (now == observed) return;
    observed = now;
    for (auto bloom = blooms.begin(); bloom != blooms.end();) {
        if (currentGameMap->getTile(bloom->at)->isSpiceBloom()) {
            if (now % kRefreshCycles == 0) refresh(*bloom);
            ++bloom;
            continue;
        }
        ++burstBlooms;
        for (int dx = -kSpillRadius; dx <= kSpillRadius; ++dx)
            for (int dy = -kSpillRadius; dy <= kSpillRadius; ++dy) {
                const Coord at = bloom->at + Coord(dx, dy);
                if (!inSpill(dx, dy) || !currentGameMap->tileExists(at)
                    || bloom->spice[(dx + kSpillRadius) * kSpan + dy + kSpillRadius]
                    || !currentGameMap->getTile(at)->isSpice()) continue;
                auto& marked = field[size_t(at.y) * width + at.x];
                if (!marked) { marked = 1; ++fieldTiles; }
            }
        bloom = blooms.erase(bloom);
    }
    if (fieldTiles == 0) return;
    for (const auto* unit : unitList) {
        if (!isHarvesterLikeUnit(unit->getItemID()) || !unit->getOwner()) continue;
        const auto* harvester = dynamic_cast<const Harvester*>(unit);
        if (!harvester) continue;
        auto& state = cargo[unit->getObjectID()];
        const int house = unit->getOwner()->getHouseID();
        const FixPoint load = harvester->getAmountOfSpice();
        const Coord at = unit->getLocation();
        if (state.house == house && load > state.load && currentGameMap->tileExists(at)
            && field[size_t(at.y) * width + at.x]) harvested[house] += load - state.load;
        state.house = house; state.load = load;
    }
}

inline void sample(Game& game, int minute) {
    observe(game);
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const int h = house->getHouseID();
        int64_t triggered = 0;
        const auto ledger = AITelemetry::log().economy.find(h);
        if (ledger != AITelemetry::log().economy.end()) {
            const auto entry = ledger->second.find("spice_bloom_burst");
            if (entry != ledger->second.end()) triggered = entry->second / (int64_t{1} << 32);
        }
        const QuantBot* bot = FfaArmy::quantBotOf(house);
        SDL_Log("SPICE_BLOOM_AUDIT: minute=%d cycle=%u house=%d ledger_enabled=%d blooms_initial=%d blooms_burst=%d"
                " bloom_field_tiles=%d blooms_triggered=%lld bloom_orders=%u blooms_skipped_wasteful=%u"
                " bloom_field_harvested=%d",
                minute, game.getGameCycleCount(), h, int(AITelemetry::log().enabled()), initialBlooms, burstBlooms,
                fieldTiles, static_cast<long long>(triggered), bot ? bot->spiceBloomStats.orders : 0u,
                bot ? bot->spiceBloomStats.wasteful : 0u, harvested[h].lround());
    }
}
}
