#ifndef AI_DECISION_LOG_H
#define AI_DECISION_LOG_H

#include <cstdint>
#include <fstream>
#include <string>
#include <chrono>
#include <map>
#include <tuple>
#include <array>
#include <limits>

namespace AITelemetry {

// Small typed JSON builder: callers cannot insert unescaped text as JSON.
class Record {
public:
    Record& set(const std::string& key, int64_t value);
    Record& set(const std::string& key, const std::string& value);
    Record& set(const std::string& key, const Record& value);
    std::string json() const { return "{" + fields + "}"; }
    static std::string quote(const std::string& value);
private:
    void add(const std::string& key, const std::string& encoded);
    std::string fields;
};

// Diagnostic counters only: no policy, RNG, savegame or observer state.
enum class RepairMetric : size_t {
    CapitalPaidRaw, CapitalRefundRequestedRaw, CapitalRefundActualRaw,
    HealthRestoredRaw, ServicePaidRaw, Completed, ReturnAttempted, ReleasedGround, HandoffCarrier,
    Delivered, UsefulDelivered, Aborted, ReturnAborted,
    LiveCycles, FreeCycles, DamagedCycles, FullReturnCycles, UnfundedCycles, Count
};
struct RepairCounters {
    std::array<int64_t, static_cast<size_t>(RepairMetric::Count)> values{};
    void add(RepairMetric metric, int64_t amount);
};
struct RepairBayMetrics {
    int house = -1;
    uint32_t yard = std::numeric_limits<uint32_t>::max();
    uint32_t startCycle = 0;
    RepairCounters counters;
    RepairCounters* houseCounters = nullptr;
    uint32_t unit = std::numeric_limits<uint32_t>::max();
    uint64_t job = 0;
    int64_t restoredRaw = 0, paidRaw = 0;
    bool completed = false, jobCensored = false;
    uint32_t builder = std::numeric_limits<uint32_t>::max();
    int64_t capitalProgressRaw = 0, capitalBaselineRaw = 0;
    void add(RepairMetric metric, int64_t amount);
};

// Disk budget of the per-game decision logs under <profile>/ai-decisions: each session is
// capped, and starting a game keeps only the newest sessions within both limits.
constexpr uint64_t kSessionByteLimit = 256ull * 1024 * 1024;
constexpr size_t kKeptSessions = 20;
constexpr uint64_t kKeptBytes = 2ull * 1024 * 1024 * 1024;

// One local stream per game/load. Never reads or changes simulation RNG/state.
// Single game-thread writer; buffered, bounded and optional. SQLite is offline.
class DecisionLog {
public:
    bool start(const std::string& directory, const Record& metadata,
               uint64_t byteLimit = kSessionByteLimit);
    void stop();
    bool enabled() const { return stream.is_open(); }
    uint64_t write(uint32_t cycle, int house, int player, const std::string& event,
                   const Record& details);
    // Accumulate Q32 credit amounts without dropping fractional payouts.
    void account(int house, const std::string& category, int64_t rawCredits);
    Record economyTotals(int house) const;
    // Inclusive scope timings, aggregated in memory; never drive simulation decisions.
    // "count" samples are gauges/work counters; "us" samples are durations.
    void performance(uint32_t cycle, int house, const std::string& scope,
                     int64_t value, int item = -1, bool duration = true);
    bool isWorstFrame(int64_t us) const { return enabled() && us > worstFrameUs; }
    void slowFrame(uint32_t cycle, int64_t microseconds, const Record& context);
    // Every >=100ms frame/gap gets a timestamped record, not only the worst
    // frame in a five-second window. Diagnostic only; no simulation decisions.
    void frameStall(uint32_t cycle, int64_t microseconds, const Record& context);
    bool performanceDue() const;
    void flushPerformance(uint32_t cycle, bool force = false);
    const std::string& path() const { return filename; }
    uint64_t repairEpoch() const { return repairEpoch_; }
    bool repairActive() const { return enabled() && repairActive_; }
    void beginRepairCapture(uint32_t cycle, bool leftCensored);
    RepairBayMetrics* repairBay(int house, uint32_t yard, uint32_t cycle, bool baseline);
    void repairObserve(RepairBayMetrics& bay, uint32_t cycle, uint32_t unit,
                       int64_t healthRaw, int64_t maxHealthRaw, bool baseline = false);
    void repairProgress(RepairBayMetrics& bay, uint32_t cycle, int64_t beforeRaw,
                        int64_t afterRaw, int64_t maxHealthRaw, int64_t paidRaw);
    void repairPrepareReturn(RepairBayMetrics& bay, uint32_t cycle, uint32_t unit,
                             bool carrier, int64_t healthRaw, int64_t maxHealthRaw);
    void repairHandoff(uint32_t cycle, uint32_t unit, bool accepted);
    void repairDelivered(uint32_t cycle, int house, uint32_t unit, bool alive);
    void repairReturnAborted(uint32_t cycle, uint32_t unit, const char* reason);
    void repairJobAborted(RepairBayMetrics& bay, uint32_t cycle, const char* reason);
    void repairBayClosed(uint32_t cycle, uint32_t yard, const char* reason);
    void repairCapitalPaid(int house, int64_t paidRaw);
    void repairCapitalRefund(uint32_t cycle, int house, uint32_t builder, int64_t requestedRaw, int64_t actualRaw);
    void repairCapitalBaseline(int house, uint32_t builder, int64_t progressRaw);
    void repairCapitalPlaced(int house, uint32_t builder, uint32_t yard, uint32_t cycle, int64_t progressRaw);
    void repairCapitalEnded(uint32_t builder);
    void finishRepairCapture(uint32_t cycle);
    Record repairQuality(int house, uint32_t cycle) const;
private:
    struct Metric {
        uint64_t count = 0;
        int64_t sum = 0, maximum = 0;
        uint32_t maxCycle = 0;
        uint64_t over33ms = 0, over100ms = 0, over250ms = 0;
    };
    using MetricKey = std::tuple<std::string, int, int, bool>;
    std::map<MetricKey, Metric> performanceMetrics;
    std::chrono::steady_clock::time_point performanceStart, sessionStart;
    uint32_t performanceCycle = 0, worstFrameCycle = 0;
    uint64_t performanceDropped = 0;
    int64_t worstFrameUs = -1;
    Record worstFrame;
    std::ofstream stream;
    std::string session, filename;
    uint64_t sequence = 0, bytes = 0, limit = 0;
    uint32_t lastCycle = 0;
    bool captureLimited = false;
    std::map<std::string, uint64_t> observationCounts;
    std::map<int, std::map<std::string, int64_t>> economy;
    std::chrono::steady_clock::time_point lastFlush;
    struct RepairHouseMetrics {
        RepairCounters counters;
        int64_t baselineBays = 0, baselinePendingPaidRaw = 0;
    };
    struct RepairReturn {
        int house;
        uint32_t yard;
        uint64_t job;
        bool carrier, useful;
        int64_t restoredRaw, paidRaw;
    };
    void repairReturnCount(const RepairReturn& returning, RepairMetric metric);
    Record repairBayRecord(const RepairBayMetrics& bay) const;
    std::map<int, RepairHouseMetrics> repairHouses;
    std::map<uint32_t, RepairBayMetrics> repairBays;
    std::map<uint32_t, RepairReturn> repairReturns;
    std::map<uint32_t, int64_t> repairCapitalBaselines;
    uint64_t repairEpoch_ = 0, nextRepairJob = 1;
    uint32_t repairStartCycle = 0, repairEndCycle = 0;
    bool repairActive_ = false, repairLeftCensored = false, repairClosed = false;
};

DecisionLog& log();
// Removes the oldest "<microseconds>-<n>" session folders in `directory` until at most
// `keepSessions` remain and their files total at most `maxBytes` (newest kept first).
// Anything else in the folder is left alone. Returns the number of sessions removed.
size_t pruneSessions(const std::string& directory, size_t keepSessions, uint64_t maxBytes);
// Starts this game's decision log when `decisionLogSetting` ([General] AI Decision Log) is on;
// DUNECITY_AI_TELEMETRY=0 forces it off and any other non-empty value forces it on. Always
// prunes old sessions first, so the folder stays within kKeptSessions / kKeptBytes.
void startGame(const Record& metadata, bool decisionLogSetting);

class PerformanceScope {
public:
    PerformanceScope(const char* scope, uint32_t cycle, int house = -1, int item = -1);
    ~PerformanceScope();
    void next(const char* nextScope);
    PerformanceScope(const PerformanceScope&) = delete;
    PerformanceScope& operator=(const PerformanceScope&) = delete;
private:
    const char* scope;
    uint32_t cycle;
    int house, item;
    bool active;
    std::chrono::steady_clock::time_point start;
};
} // namespace AITelemetry
#endif
