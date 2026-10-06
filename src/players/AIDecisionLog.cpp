#include <players/AIDecisionLog.h>
#include <misc/fnkdat.h>
#include <SDL_log.h>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <limits>

namespace AITelemetry {
std::string Record::quote(const std::string& value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + "\"";
}
void Record::add(const std::string& key, const std::string& encoded) {
    if (!fields.empty()) fields += ',';
    fields += quote(key) + ':' + encoded;
}
Record& Record::set(const std::string& key, int64_t value) { add(key, std::to_string(value)); return *this; }
Record& Record::set(const std::string& key, const std::string& value) { add(key, quote(value)); return *this; }
Record& Record::set(const std::string& key, const Record& value) { add(key, value.json()); return *this; }

bool DecisionLog::start(const std::string& directory, const Record& metadata, uint64_t byteLimit) {
    stop();
    sequence = bytes = 0; captureLimited = false;
    lastCycle = 0; economy.clear(); observationCounts.clear();
    performanceMetrics.clear(); worstFrameUs = -1; worstFrame = Record();
    performanceCycle = 0; performanceDropped = 0;
    sessionStart = performanceStart = std::chrono::steady_clock::now();
    limit = byteLimit;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // Exclusive directory creation also separates concurrent game processes.
    for (int suffix = 0; suffix < 100; ++suffix) {
        session = std::to_string(stamp) + "-" + std::to_string(suffix);
        const auto folder = std::filesystem::path(directory) / session;
        if (!std::filesystem::create_directory(folder, ec)) { if (ec) return false; continue; }
        filename = (folder / "events.jsonl").string();
        stream.clear();
        stream.open(filename, std::ios::out | std::ios::binary);
        if (!stream) { stream.close(); return false; }
        lastFlush = std::chrono::steady_clock::now();
        write(0, -1, -1, "session_start", metadata);
        stream.flush();
        return enabled();
    }
    return false;
}
void DecisionLog::stop() {
    if (enabled()) { flushPerformance(lastCycle, true); write(lastCycle, -1, -1, "session_end", Record()); stream.close(); }
}
uint64_t DecisionLog::write(uint32_t cycle, int house, int player,
                          const std::string& event, const Record& details) {
    if (!enabled()) return 0;
    const auto writeStart = std::chrono::steady_clock::now();
    lastCycle = std::max(lastCycle, cycle);
    const bool routine=event=="city_growth_sample" || event=="harvest_rally_move_order";
    if (routine && (++observationCounts[event]-1)%8!=0) return 0;
    const bool terminal=event=="game_summary" || event=="session_end" || event=="simulation_exception" || event=="capture_limit";
    if (limit>=1024*1024 && !terminal && bytes>=limit-limit/16) {
        if (!captureLimited) {
            captureLimited=true;
            write(cycle,-1,-1,"capture_limit",Record().set("byte_limit",limit)
                .set("captured_bytes",bytes).set("terminal_events_retained",1));
        }
        return 0;
    }
    const uint64_t id = ++sequence;
    const auto row = Record().set("schema_version", 1).set("telemetry_version", 17).set("policy_version", "whole-army-base-scramble-v90")
        .set("session", session).set("seq", id).set("cycle", cycle)
        .set("house", house).set("player", player).set("event", event).set("data", details).json() + '\n';
    if (bytes + row.size() > limit) {
        // Explicit terminal marker; a few hundred bytes beyond the configured cap.
        stream << Record().set("schema_version", 1).set("telemetry_version", 17).set("policy_version", "whole-army-base-scramble-v90").set("session", session).set("seq", id)
            .set("cycle", cycle).set("house", -1).set("player", -1)
            .set("event", "capture_limit").set("data", Record().set("byte_limit", limit)).json() << '\n';
        stream.close();
        SDL_Log("AI telemetry reached its session byte limit: %s", filename.c_str());
        return 0;
    }
    stream << row;
    bytes += row.size();
    const auto now = std::chrono::steady_clock::now();
    if (id % 128 == 0 || now - lastFlush >= std::chrono::seconds(2)) {
        stream.flush(); lastFlush = now;
    }
    if (!stream) {
        stream.close();
        SDL_Log("AI telemetry disabled after write failure: %s", filename.c_str());
        return 0;
    }
    if (event != "performance_window" && event != "session_end")
        performance(cycle,-1,"telemetry.write",std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-writeStart).count());
    return id;
}
void DecisionLog::performance(uint32_t cycle, int house, const std::string& scope,
                              int64_t value, int item, bool duration) {
    if (!enabled()) return;
    lastCycle = std::max(lastCycle, cycle);
    const MetricKey key{scope, house, item, duration};
    // Fixed instrumentation names/item types only; guard accidental unbounded labels.
    if (performanceMetrics.size() >= 512 && !performanceMetrics.count(key)) { ++performanceDropped; return; }
    auto& metric = performanceMetrics[key];
    value = std::max<int64_t>(0, value);
    ++metric.count; metric.sum += value;
    if (metric.count == 1 || value > metric.maximum) {
        metric.maximum = value; metric.maxCycle = cycle;
    }
    if (duration) {
        metric.over33ms += value > 33333;
        metric.over100ms += value > 100000;
        metric.over250ms += value > 250000;
    }
}
void DecisionLog::slowFrame(uint32_t cycle, int64_t microseconds, const Record& context) {
    if (!enabled() || microseconds <= worstFrameUs) return;
    worstFrameUs = microseconds; worstFrameCycle = cycle; worstFrame = context;
}
void DecisionLog::frameStall(uint32_t cycle, int64_t microseconds, const Record& context) {
    if (!enabled() || microseconds<100000) return;
    const auto now=std::chrono::steady_clock::now();
    write(cycle,-1,-1,"frame_stall",Record().set("duration_us",microseconds)
        .set("session_wall_us",std::chrono::duration_cast<std::chrono::microseconds>(now-sessionStart).count())
        .set("context",context));
}
bool DecisionLog::performanceDue() const {
    return enabled() && std::chrono::steady_clock::now() - performanceStart >= std::chrono::seconds(5);
}
void DecisionLog::flushPerformance(uint32_t cycle, bool force) {
    if (!enabled() || (!force && !performanceDue()) || performanceMetrics.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    Record metrics;
    int index = 0;
    for (const auto& entry : performanceMetrics) {
        const auto& [scope, house, item, duration] = entry.first;
        const auto& m = entry.second;
        metrics.set(std::to_string(index++), Record().set("scope",scope).set("house",house)
            .set("item",item).set("unit",duration ? "us" : "count").set("count",m.count)
            .set("sum",m.sum).set("max",m.maximum).set("max_cycle",m.maxCycle)
            .set("over_33ms",m.over33ms).set("over_100ms",m.over100ms).set("over_250ms",m.over250ms));
    }
    const auto elapsed = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();
    };
    const auto row = Record().set("start_cycle",performanceCycle).set("end_cycle",cycle)
        .set("start_wall_us",elapsed(sessionStart,performanceStart))
        .set("end_wall_us",elapsed(sessionStart,now)).set("elapsed_us",elapsed(performanceStart,now))
        .set("metrics",metrics).set("dropped_samples",performanceDropped).set("worst_frame_cycle",worstFrameCycle)
        .set("worst_frame_us",worstFrameUs).set("worst_frame",worstFrame);
    performanceDropped = 0;
    performanceMetrics.clear(); performanceCycle = cycle; performanceStart = now;
    worstFrameUs = -1; worstFrame = Record();
    write(cycle,-1,-1,"performance_window",row);
}
PerformanceScope::PerformanceScope(const char* scope, uint32_t cycle, int house, int item)
    : scope(scope), cycle(cycle), house(house), item(item), active(log().enabled()) {
    if (active) start = std::chrono::steady_clock::now();
}
void PerformanceScope::next(const char* nextScope) {
    if (active) log().performance(cycle,house,scope,
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count(),item);
    scope = nextScope;
    if (active) start = std::chrono::steady_clock::now();
}
PerformanceScope::~PerformanceScope() { next(scope); }
void DecisionLog::account(int house, const std::string& category, int64_t rawCredits) {
    if (!enabled()) return;
    auto& total = economy[house][category];
    if (rawCredits > 0 && total > std::numeric_limits<int64_t>::max() - rawCredits)
        total = std::numeric_limits<int64_t>::max();
    else if (rawCredits < 0 && total < std::numeric_limits<int64_t>::min() - rawCredits)
        total = std::numeric_limits<int64_t>::min();
    else total += rawCredits;
}
Record DecisionLog::economyTotals(int house) const {
    Record result;
    const auto found = economy.find(house);
    if (found != economy.end()) for (const auto& entry : found->second)
        result.set(entry.first, entry.second / (int64_t{1} << 32));
    return result;
}
DecisionLog& log() { static DecisionLog instance; return instance; }
void startGame(const Record& metadata, bool diagnosticsEnabled) {
    log().stop();
    if (!diagnosticsEnabled) return;
    const char* enabled = std::getenv("DUNECITY_AI_TELEMETRY");
    if (enabled && std::string(enabled) == "0") return;
    char root[FILENAME_MAX];
    if (fnkdat("ai-decisions/", root, sizeof(root), FNKDAT_USER | FNKDAT_CREAT) < 0
        || !log().start(root, metadata)) {
        SDL_Log("AI telemetry unavailable; game continues without structured capture");
    } else SDL_Log("AI telemetry: %s", log().path().c_str());
}
} // namespace AITelemetry
