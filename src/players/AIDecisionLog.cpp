#include <players/AIDecisionLog.h>
#include <misc/fnkdat.h>
#include <SDL_log.h>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <limits>
#include <vector>

namespace AITelemetry {
std::string Record::quote(const std::string& value) {
    // Runs of plain characters are copied whole; only '"', '\\' and control characters
    // are rewritten, as they always were.
    std::string result;
    result.reserve(value.size() + 2);
    result += '"';
    constexpr char hex[] = "0123456789abcdef";
    size_t plain = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c != '"' && c != '\\' && c >= 32) continue;
        result.append(value, plain, i - plain);
        plain = i + 1;
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    result.append(value, plain, std::string::npos);
    result += '"';
    return result;
}
void Record::add(const std::string& key, const std::string& encoded) {
    if (!fields.empty()) fields += ',';
    fields += quote(key);
    fields += ':';
    fields += encoded;
}
Record& Record::set(const std::string& key, int64_t value) { add(key, std::to_string(value)); return *this; }
Record& Record::set(const std::string& key, const std::string& value) { add(key, quote(value)); return *this; }
Record& Record::set(const std::string& key, const Record& value) { add(key, value.json()); return *this; }

namespace {
constexpr uint32_t noRepairObject = std::numeric_limits<uint32_t>::max();
constexpr std::array<const char*, static_cast<size_t>(RepairMetric::Count)> repairFields{{
    "capital_paid_raw_total", "capital_refund_requested_raw_total", "capital_refund_actual_raw_total",
    "health_restored_raw_total", "service_paid_raw_total", "completed_total", "return_attempted_total",
    "released_ground_total", "handoff_carrier_total", "delivered_total", "useful_delivered_total",
    "aborted_total", "return_aborted_total",
    "live_cycles_total", "free_cycles_total", "damaged_cycles_total", "full_return_cycles_total", "unfunded_cycles_total"
}};
int64_t repairSum(int64_t total, int64_t amount) {
    return amount > std::numeric_limits<int64_t>::max() - total ? std::numeric_limits<int64_t>::max() : total + amount;
}
void repairCountersRecord(Record& record, const RepairCounters& counters) {
    for (size_t i = 0; i < repairFields.size(); ++i) record.set(repairFields[i], counters.values[i]);
}
}

void RepairCounters::add(RepairMetric metric, int64_t amount) {
    if (amount <= 0) return;
    auto& total = values[static_cast<size_t>(metric)];
    total = repairSum(total, amount);
}
void RepairBayMetrics::add(RepairMetric metric, int64_t amount) {
    counters.add(metric, amount);
    houseCounters->add(metric, amount);
}
void DecisionLog::beginRepairCapture(uint32_t cycle, bool leftCensored) {
    ++repairEpoch_;
    repairHouses.clear(); repairBays.clear(); repairReturns.clear(); repairCapitalBaselines.clear();
    repairStartCycle = repairEndCycle = cycle;
    repairLeftCensored = leftCensored;
    repairActive_ = enabled(); repairClosed = false; nextRepairJob = 1;
    lastCycle = std::max(lastCycle, cycle);
}
RepairBayMetrics* DecisionLog::repairBay(int house, uint32_t yard, uint32_t cycle, bool baseline) {
    if (!repairActive()) return nullptr;
    auto found = repairBays.find(yard);
    if (found != repairBays.end() && found->second.house != house) {
        repairBayClosed(cycle, yard, "owner_changed");
        found = repairBays.end();
    }
    if (found == repairBays.end()) {
        auto& owner = repairHouses[house];
        RepairBayMetrics bay;
        bay.house = house; bay.yard = yard; bay.startCycle = cycle; bay.houseCounters = &owner.counters;
        found = repairBays.emplace(yard, bay).first;
        if (baseline) ++owner.baselineBays;
        write(cycle, house, -1, "repair_yard_quality", repairBayRecord(found->second)
            .set("kind", baseline ? "baseline" : "created"));
    }
    return &found->second;
}
void DecisionLog::repairObserve(RepairBayMetrics& bay, uint32_t cycle, uint32_t unit,
                               int64_t healthRaw, int64_t maxHealthRaw, bool baseline) {
    if (!repairActive() || bay.unit == unit) return;
    if (bay.unit != noRepairObject) repairJobAborted(bay, cycle, "occupant_changed");
    if (unit == noRepairObject) return;
    repairReturnAborted(cycle, unit, "reentered_service");
    bay.unit = unit; bay.job = nextRepairJob++;
    bay.restoredRaw = bay.paidRaw = 0; bay.completed = false; bay.jobCensored = baseline;
    write(cycle, bay.house, -1, "repair_job", repairBayRecord(bay)
        .set("kind", baseline ? "baseline" : "entered").set("health_raw", healthRaw).set("max_health_raw", maxHealthRaw));
}
void DecisionLog::repairProgress(RepairBayMetrics& bay, uint32_t cycle, int64_t beforeRaw,
                                int64_t afterRaw, int64_t maxHealthRaw, int64_t paidRaw) {
    if (!repairActive()) return;
    const int64_t restored = std::max<int64_t>(0, afterRaw - beforeRaw);
    bay.add(RepairMetric::HealthRestoredRaw, restored);
    bay.add(RepairMetric::ServicePaidRaw, paidRaw);
    bay.restoredRaw = repairSum(bay.restoredRaw, restored);
    bay.paidRaw = repairSum(bay.paidRaw, std::max<int64_t>(0, paidRaw));
    if (!bay.completed && beforeRaw < maxHealthRaw && afterRaw >= maxHealthRaw) {
        bay.completed = true;
        bay.add(RepairMetric::Completed, 1);
        write(cycle, bay.house, -1, "repair_job", repairBayRecord(bay)
            .set("kind", "completed").set("health_raw", afterRaw).set("max_health_raw", maxHealthRaw));
    }
}
void DecisionLog::repairPrepareReturn(RepairBayMetrics& bay, uint32_t cycle, uint32_t unit,
                                     bool carrier, int64_t healthRaw, int64_t maxHealthRaw) {
    if (!repairActive()) return;
    bay.add(RepairMetric::ReturnAttempted, 1);
    repairReturnAborted(cycle, unit, "new_return");
    repairReturns.emplace(unit, RepairReturn{bay.house, bay.yard, bay.job, carrier,
        bay.restoredRaw > 0 && healthRaw >= maxHealthRaw, bay.restoredRaw, bay.paidRaw});
    write(cycle, bay.house, -1, "repair_job", repairBayRecord(bay)
        .set("kind", "return_started").set("carrier", carrier).set("health_raw", healthRaw));
    bay.unit = noRepairObject; bay.job = 0;
}
void DecisionLog::repairReturnCount(const RepairReturn& returning, RepairMetric metric) {
    auto bay = repairBays.find(returning.yard);
    if (bay != repairBays.end() && bay->second.house == returning.house) bay->second.add(metric, 1);
    else repairHouses[returning.house].counters.add(metric, 1);
}
void DecisionLog::repairHandoff(uint32_t cycle, uint32_t unit, bool accepted) {
    if (!repairActive()) return;
    auto found = repairReturns.find(unit);
    if (found == repairReturns.end()) return;
    if (!accepted) { repairReturnAborted(cycle, unit, "handoff_failed"); return; }
    const auto& returning = found->second;
    repairReturnCount(returning, RepairMetric::HandoffCarrier);
    write(cycle, returning.house, -1, "repair_job", Record().set("kind", "handoff_carrier")
        .set("yard", returning.yard).set("unit", unit).set("job", returning.job));
}
void DecisionLog::repairDelivered(uint32_t cycle, int house, uint32_t unit, bool alive) {
    if (!repairActive() || repairReturns.empty()) return;
    auto found = repairReturns.find(unit);
    if (found == repairReturns.end()) return;
    if (!alive || found->second.house != house) {
        repairReturnAborted(cycle, unit, alive ? "owner_changed" : "deployment_not_alive");
        return;
    }
    const auto& returning = found->second;
    if (!returning.carrier) repairReturnCount(returning, RepairMetric::ReleasedGround);
    repairReturnCount(returning, RepairMetric::Delivered);
    if (returning.useful) repairReturnCount(returning, RepairMetric::UsefulDelivered);
    write(cycle, house, -1, "repair_job", Record().set("kind", "delivered")
        .set("yard", returning.yard).set("unit", unit).set("job", returning.job)
        .set("carrier", returning.carrier).set("useful", returning.useful)
        .set("health_restored_raw", returning.restoredRaw).set("service_paid_raw", returning.paidRaw));
    repairReturns.erase(found);
}
void DecisionLog::repairReturnAborted(uint32_t cycle, uint32_t unit, const char* reason) {
    if (!repairActive()) return;
    auto found = repairReturns.find(unit);
    if (found == repairReturns.end()) return;
    const auto& returning = found->second;
    repairReturnCount(returning, RepairMetric::ReturnAborted);
    write(cycle, returning.house, -1, "repair_job", Record().set("kind", "return_aborted")
        .set("yard", returning.yard).set("unit", unit).set("job", returning.job).set("reason", reason));
    repairReturns.erase(found);
}
void DecisionLog::repairJobAborted(RepairBayMetrics& bay, uint32_t cycle, const char* reason) {
    if (!repairActive() || bay.unit == noRepairObject) return;
    bay.add(RepairMetric::Aborted, 1);
    write(cycle, bay.house, -1, "repair_job", repairBayRecord(bay).set("kind", "aborted").set("reason", reason));
    bay.unit = noRepairObject; bay.job = 0;
}
Record DecisionLog::repairBayRecord(const RepairBayMetrics& bay) const {
    Record result;
    result.set("version", 1).set("yard", bay.yard).set("owner", bay.house).set("unit", bay.unit)
        .set("job", bay.job).set("start_cycle", bay.startCycle).set("job_left_censored", bay.jobCensored)
        .set("job_health_restored_raw", bay.restoredRaw).set("job_service_paid_raw", bay.paidRaw)
        .set("builder", bay.builder).set("capital_progress_known", bay.builder != noRepairObject)
        .set("capital_progress_raw", bay.capitalProgressRaw)
        .set("capital_baseline_paid_raw", bay.capitalBaselineRaw);
    repairCountersRecord(result, bay.counters);
    return result;
}
void DecisionLog::repairBayClosed(uint32_t cycle, uint32_t yard, const char* reason) {
    if (!repairActive()) return;
    auto found = repairBays.find(yard);
    if (found == repairBays.end()) return;
    repairJobAborted(found->second, cycle, reason);
    write(cycle, found->second.house, -1, "repair_yard_quality", repairBayRecord(found->second)
        .set("kind", "closed").set("end_cycle", cycle).set("reason", reason));
    repairBays.erase(found);
}
void DecisionLog::repairCapitalPaid(int house, int64_t paidRaw) {
    if (repairActive()) repairHouses[house].counters.add(RepairMetric::CapitalPaidRaw, paidRaw);
}
void DecisionLog::repairCapitalRefund(uint32_t cycle, int house, uint32_t builder, int64_t requestedRaw, int64_t actualRaw) {
    if (!repairActive()) return;
    auto& counters = repairHouses[house].counters;
    counters.add(RepairMetric::CapitalRefundRequestedRaw, requestedRaw);
    counters.add(RepairMetric::CapitalRefundActualRaw, actualRaw);
    write(cycle, house, -1, "repair_yard_quality", Record().set("kind", "capital_refund")
        .set("builder", builder).set("refund_requested_raw", requestedRaw).set("refund_actual_raw", actualRaw));
    repairCapitalBaselines.erase(builder);
}
void DecisionLog::repairCapitalBaseline(int house, uint32_t builder, int64_t progressRaw) {
    if (!repairActive() || progressRaw <= 0 || repairCapitalBaselines.count(builder)) return;
    repairCapitalBaselines.emplace(builder, progressRaw);
    repairHouses[house].baselinePendingPaidRaw = repairSum(repairHouses[house].baselinePendingPaidRaw, progressRaw);
}
void DecisionLog::repairCapitalPlaced(int house, uint32_t builder, uint32_t yard, uint32_t cycle, int64_t progressRaw) {
    if (auto* bay = repairBay(house, yard, cycle, false)) {
        bay->builder = builder; bay->capitalProgressRaw = progressRaw;
        const auto baseline = repairCapitalBaselines.find(builder);
        if (baseline != repairCapitalBaselines.end()) bay->capitalBaselineRaw = baseline->second;
        // The house already counted each actual debit; bind its observed share
        // to this completed yard without charging the house again.
        bay->counters.add(RepairMetric::CapitalPaidRaw, std::max<int64_t>(0, progressRaw - bay->capitalBaselineRaw));
        write(cycle, house, -1, "repair_yard_quality", repairBayRecord(*bay).set("kind", "capital_placed"));
    }
    repairCapitalBaselines.erase(builder);
}
void DecisionLog::repairCapitalEnded(uint32_t builder) {
    if (repairActive()) repairCapitalBaselines.erase(builder);
}
void DecisionLog::finishRepairCapture(uint32_t cycle) {
    if (!repairActive()) return;
    repairEndCycle = cycle; repairClosed = true;
    for (const auto& entry : repairBays)
        write(cycle, entry.second.house, -1, "repair_yard_quality", repairBayRecord(entry.second)
            .set("kind", "capture_end").set("end_cycle", cycle)
            .set("right_censored", entry.second.unit != noRepairObject));
    repairActive_ = false;
}
Record DecisionLog::repairQuality(int house, uint32_t cycle) const {
    Record result;
    if (!enabled() || (!repairActive_ && !repairClosed)) return result;
    int64_t live = 0, jobs = 0, returns = 0;
    for (const auto& entry : repairBays) if (entry.second.house == house) {
        ++live; if (entry.second.unit != noRepairObject) ++jobs;
    }
    for (const auto& entry : repairReturns) if (entry.second.house == house) ++returns;
    result.set("version", 1).set("start_cycle", repairStartCycle)
        .set("end_cycle", repairClosed ? repairEndCycle : cycle)
        .set("left_censored", repairLeftCensored).set("capture_complete", repairClosed && !captureLimited)
        .set("live_bays", live).set("right_censored_jobs", jobs).set("right_censored_returns", returns);
    auto found = repairHouses.find(house);
    static const RepairHouseMetrics empty;
    const auto& metrics = found == repairHouses.end() ? empty : found->second;
    result.set("baseline_bays", metrics.baselineBays).set("baseline_pending_paid_raw", metrics.baselinePendingPaidRaw);
    repairCountersRecord(result, metrics.counters);
    return result;
}

bool DecisionLog::start(const std::string& directory, const Record& metadata, uint64_t byteLimit) {
    stop();
    ++repairEpoch_; repairActive_ = repairClosed = false;
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
    if (enabled()) { finishRepairCapture(lastCycle); flushPerformance(lastCycle, true); write(lastCycle, -1, -1, "session_end", Record()); stream.close(); }
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
    const auto row = Record().set("schema_version", 1).set("telemetry_version", 18).set("policy_version", "city-growth-power-v100")
        .set("session", session).set("seq", id).set("cycle", cycle)
        .set("house", house).set("player", player).set("event", event).set("data", details).json() + '\n';
    if (bytes + row.size() > limit) {
        // Explicit terminal marker; a few hundred bytes beyond the configured cap.
        stream << Record().set("schema_version", 1).set("telemetry_version", 18).set("policy_version", "city-growth-power-v100").set("session", session).set("seq", id)
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
size_t pruneSessions(const std::string& directory, size_t keepSessions, uint64_t maxBytes) {
    namespace fs = std::filesystem;
    struct Session { uint64_t stamp, suffix, bytes; fs::path path; };
    std::vector<Session> sessions;
    std::error_code ec;
    const auto number = [](const std::string& text, uint64_t& value) {
        if (text.empty() || text.size() > 19) return false;
        value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }
        return true;
    };
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (it->is_symlink(entryError) || !it->is_directory(entryError)) continue;
        const std::string name = it->path().filename().string();
        const auto dash = name.find('-');
        Session session{0, 0, 0, it->path()};
        if (dash == std::string::npos || !number(name.substr(0, dash), session.stamp)
            || !number(name.substr(dash + 1), session.suffix)) continue;
        for (fs::recursive_directory_iterator file(it->path(), entryError), fileEnd;
             !entryError && file != fileEnd; file.increment(entryError)) {
            std::error_code sizeError;
            if (file->is_regular_file(sizeError)) {
                const auto size = file->file_size(sizeError);
                if (!sizeError) session.bytes += size;
            }
        }
        sessions.push_back(std::move(session));
    }
    std::sort(sessions.begin(), sessions.end(), [](const Session& a, const Session& b) {
        return a.stamp != b.stamp ? a.stamp > b.stamp : a.suffix > b.suffix;
    });
    // Newest first: keep the longest run of recent sessions that fits both limits.
    uint64_t total = 0;
    size_t removed = 0;
    for (size_t index = 0; index < sessions.size(); ++index) {
        total += sessions[index].bytes;
        if (index < keepSessions && total <= maxBytes) continue;
        std::error_code removeError;
        fs::remove_all(sessions[index].path, removeError);
        if (!removeError) ++removed;
    }
    return removed;
}
void startGame(const Record& metadata, bool decisionLogSetting) {
    log().stop();
    bool enabled = decisionLogSetting;
    if (const char* forced = std::getenv("DUNECITY_AI_TELEMETRY"); forced && *forced)
        enabled = std::string(forced) != "0";
    char root[FILENAME_MAX];
    if (fnkdat("ai-decisions/", root, sizeof(root), FNKDAT_USER | (enabled ? FNKDAT_CREAT : 0)) < 0) {
        if (enabled) SDL_Log("AI telemetry unavailable; game continues without structured capture");
        return;
    }
    // Leave room for this game's session so the folder stays within both limits with it.
    const size_t removed = enabled
        ? pruneSessions(root, kKeptSessions - 1, kKeptBytes - kSessionByteLimit)
        : pruneSessions(root, kKeptSessions, kKeptBytes);
    if (removed) SDL_Log("AI telemetry: removed %zu old decision log session(s) from %s", removed, root);
    if (!enabled) return;
    if (!log().start(root, metadata)) {
        SDL_Log("AI telemetry unavailable; game continues without structured capture");
    } else SDL_Log("AI telemetry: %s", log().path().c_str());
}
} // namespace AITelemetry
