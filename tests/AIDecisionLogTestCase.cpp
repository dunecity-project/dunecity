#include <catch2/catch_test_macros.hpp>
#include <players/AIDecisionLog.h>
#include <DataTypes.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <optional>

namespace {
// Sets (or with nullptr removes) an environment variable for one scope, then restores it.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name(name) {
        if (const char* old = std::getenv(name)) previous = old;
        set(value);
    }
    ~ScopedEnv() { set(previous ? previous->c_str() : nullptr); }
private:
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");
#else
        if (value) setenv(name, value, 1); else unsetenv(name);
#endif
    }
    const char* name;
    std::optional<std::string> previous;
};
std::filesystem::path scratchDirectory(const char* prefix) {
    return std::filesystem::temp_directory_path()/(prefix+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
void writeBytes(const std::filesystem::path& file, size_t size) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << std::string(size, 'x');
}
}

TEST_CASE("Disabled diagnostics close structured capture and skip subsequent records", "[ai][telemetry]") {
    const auto root=scratchDirectory("dunecity-disabled-");
    // startGame prunes the profile's ai-decisions folder: never the real user profile.
    const ScopedEnv profile("DUNECITY_USERDIR",(root/"profile").string().c_str());
    const ScopedEnv forced("DUNECITY_AI_TELEMETRY",nullptr);
    auto& writer=AITelemetry::log();
    REQUIRE(writer.start(root.string(),AITelemetry::Record()));
    const auto file=writer.path();
    AITelemetry::startGame(AITelemetry::Record(),false);
    REQUIRE_FALSE(writer.enabled());
    const auto size=std::filesystem::file_size(file);
    REQUIRE(writer.write(1,0,0,"decision",AITelemetry::Record())==0);
    writer.frameStall(1,200000,AITelemetry::Record());
    writer.performance(1,0,"frame",200000);
    writer.flushPerformance(1,true);
    REQUIRE(std::filesystem::file_size(file)==size);
    std::filesystem::remove_all(root);
}

TEST_CASE("The AI decision log is its own setting, off by default", "[ai][telemetry][settings]") {
    STATIC_REQUIRE_FALSE(SettingsClass::GeneralClass::defaultAIDecisionLog);
    const SettingsClass::GeneralClass general{};
    REQUIRE_FALSE(general.aiDecisionLog);
    // Diagnostic logs keep their own (desktop: on) default and no longer imply the decision log.
    REQUIRE(general.diagnosticLogs==SettingsClass::GeneralClass::defaultDiagnosticLogs);
}

TEST_CASE("startGame follows the AI Decision Log setting; DUNECITY_AI_TELEMETRY overrides it", "[ai][telemetry][settings]") {
    const auto root=scratchDirectory("dunecity-setting-");
    const ScopedEnv profile("DUNECITY_USERDIR",root.string().c_str());
    auto& writer=AITelemetry::log();
    {
        const ScopedEnv forced("DUNECITY_AI_TELEMETRY",nullptr);
        AITelemetry::startGame(AITelemetry::Record(),false);
        REQUIRE_FALSE(writer.enabled());
        REQUIRE_FALSE(std::filesystem::exists(root/"ai-decisions"));
        AITelemetry::startGame(AITelemetry::Record(),true);
        REQUIRE(writer.enabled());
        REQUIRE(std::filesystem::path(writer.path()).parent_path().parent_path()==root/"ai-decisions");
    }
    {
        const ScopedEnv forced("DUNECITY_AI_TELEMETRY","0");
        AITelemetry::startGame(AITelemetry::Record(),true);
        REQUIRE_FALSE(writer.enabled());
    }
    {
        const ScopedEnv forced("DUNECITY_AI_TELEMETRY","1");
        AITelemetry::startGame(AITelemetry::Record(),false);
        REQUIRE(writer.enabled());
    }
    writer.stop();
    std::filesystem::remove_all(root);
}

TEST_CASE("Decision log sessions are pruned to the newest ones within the count and byte budget", "[ai][telemetry]") {
    const auto root=scratchDirectory("dunecity-prune-");
    // Newest first by the microsecond stamp, then the suffix; names, not file times, decide.
    writeBytes(root/"1000-0"/"events.jsonl",100);
    writeBytes(root/"2000-0"/"events.jsonl",100);
    writeBytes(root/"2000-1"/"events.jsonl",100);
    writeBytes(root/"3000-0"/"events.jsonl",100);
    writeBytes(root/"4000-0"/"events.jsonl",50);
    writeBytes(root/"4000-0"/"extra"/"notes.txt",20);
    writeBytes(root/"notes"/"keep.txt",500);        // not a session folder
    writeBytes(root/"5000-x"/"events.jsonl",500);   // not a session folder
    writeBytes(root/"readme.txt",10);
    const auto exists=[&](const char* name) { return std::filesystem::exists(root/name); };

    REQUIRE(AITelemetry::pruneSessions(root.string(),10,1000)==0);
    REQUIRE(AITelemetry::pruneSessions(root.string(),3,1000)==2);   // count: keeps 4000-0, 3000-0, 2000-1
    REQUIRE(exists("4000-0"));REQUIRE(exists("3000-0"));REQUIRE(exists("2000-1"));
    REQUIRE_FALSE(exists("2000-0"));REQUIRE_FALSE(exists("1000-0"));
    REQUIRE(AITelemetry::pruneSessions(root.string(),3,170)==1);    // bytes: 70 + 100 fit, the third does not
    REQUIRE(exists("4000-0"));REQUIRE(exists("3000-0"));REQUIRE_FALSE(exists("2000-1"));
    REQUIRE(AITelemetry::pruneSessions(root.string(),0,1000)==2);
    REQUIRE_FALSE(exists("4000-0"));REQUIRE_FALSE(exists("3000-0"));
    REQUIRE(exists("notes"));REQUIRE(exists("5000-x"));REQUIRE(exists("readme.txt"));
    REQUIRE(AITelemetry::pruneSessions((root/"missing").string(),0,0)==0);
    std::filesystem::remove_all(root);
}

TEST_CASE("Starting a logged game keeps at most the newest kKeptSessions sessions", "[ai][telemetry]") {
    const auto root=scratchDirectory("dunecity-startprune-");
    const ScopedEnv profile("DUNECITY_USERDIR",root.string().c_str());
    const ScopedEnv forced("DUNECITY_AI_TELEMETRY",nullptr);
    for (size_t i = 0; i < AITelemetry::kKeptSessions + 5; ++i)
        writeBytes(root/"ai-decisions"/(std::to_string(1000+i)+"-0")/"events.jsonl",10);
    AITelemetry::startGame(AITelemetry::Record(),true);
    REQUIRE(AITelemetry::log().enabled());
    AITelemetry::log().stop();
    size_t sessions=0;
    for (const auto& entry : std::filesystem::directory_iterator(root/"ai-decisions")) sessions+=entry.is_directory();
    REQUIRE(sessions==AITelemetry::kKeptSessions);                 // 19 old ones + this game's
    REQUIRE(std::filesystem::exists(root/"ai-decisions"/(std::to_string(1000+AITelemetry::kKeptSessions+4)+"-0")));
    REQUIRE_FALSE(std::filesystem::exists(root/"ai-decisions"/"1005-0"));
    // A disabled game prunes to the full budget and writes nothing.
    AITelemetry::startGame(AITelemetry::Record(),false);
    REQUIRE_FALSE(AITelemetry::log().enabled());
    std::filesystem::remove_all(root);
}

TEST_CASE("AI telemetry escapes text and preserves numeric/nested fields", "[ai][telemetry]") {
    const auto row = AITelemetry::Record().set("reason", "a\"b\n\\c\t")
        .set("demand", -300).set("state", AITelemetry::Record().set("credits", 17922)).json();
    REQUIRE(row == "{\"reason\":\"a\\\"b\\u000a\\\\c\\u0009\",\"demand\":-300,\"state\":{\"credits\":17922}}");
}

TEST_CASE("AI telemetry separates sessions and ends bounded capture explicitly", "[ai][telemetry]") {
    const auto root = std::filesystem::temp_directory_path() / ("dunecity-telemetry-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(), AITelemetry::Record().set("test", 1), 1200));
    const auto first = writer.path();
    REQUIRE(writer.write(200, 6, 2, "decision", AITelemetry::Record().set("item", 20)) == 2);
    for (int i = 0; i < 20; ++i) writer.write(201 + i, 6, 2, "decision", AITelemetry::Record());
    REQUIRE_FALSE(writer.enabled());
    std::ifstream input(first);
    std::stringstream contents; contents << input.rdbuf();
    input.close(); // Windows cannot remove the fixture while its reader is open.
    REQUIRE(contents.str().find("\"event\":\"capture_limit\"") != std::string::npos);
    REQUIRE(contents.str().find("\"cycle\":200") != std::string::npos);
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    REQUIRE(writer.path() != first);
    writer.stop();
    REQUIRE(std::filesystem::exists(first));
    if (const char* artifact = std::getenv("DUNECITY_AI_TEST_EXPORT"))
        std::filesystem::copy_file(first, artifact, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove_all(root);
}

TEST_CASE("AI telemetry retains fractional income and records the real closing cycle", "[ai][telemetry]") {
    const auto root = std::filesystem::temp_directory_path() / ("dunecity-ledger-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    const auto file = writer.path();
    for (int n = 0; n < 8; ++n) writer.account(3, "city_gross", int64_t{1} << 30);
    REQUIRE(writer.economyTotals(3).json() == "{\"city_gross\":2}");
    REQUIRE(writer.economyTotals(7).json() == "{}");
    writer.write(746182, 3, 50, "state_snapshot", AITelemetry::Record());
    writer.stop();
    std::ifstream input(file);
    std::string line, last;
    while (std::getline(input, line)) last = line;
    input.close();
    REQUIRE(last.find("\"cycle\":746182") != std::string::npos);
    REQUIRE(last.find("\"event\":\"session_end\"") != std::string::npos);
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    REQUIRE(writer.economyTotals(3).json() == "{}");
    writer.stop();
    std::filesystem::remove_all(root);
}

TEST_CASE("Telemetry thins repeated growth observations without dropping actual changes", "[ai][telemetry]") {
    const auto root=std::filesystem::temp_directory_path()/("dunecity-thinning-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(),AITelemetry::Record(),2*1024*1024));
    int samples=0,changes=0;
    for (int i=0;i<16;++i) {
        samples+=writer.write(i,0,0,"city_growth_sample",AITelemetry::Record())!=0;
        changes+=writer.write(i,0,0,"city_level_changed",AITelemetry::Record())!=0;
    }
    REQUIRE(samples==2);
    REQUIRE(changes==16);
    const auto path=writer.path();
    const std::string payload(10000,'x');
    for (int i=0;i<250;++i) writer.write(20+i,0,0,"decision",AITelemetry::Record().set("payload",payload));
    REQUIRE(writer.enabled()); // capture space retained for the outcome
    REQUIRE(writer.write(300,-1,-1,"game_summary",AITelemetry::Record().set("ended",1))!=0);
    writer.stop();
    std::ifstream input(path); std::stringstream contents; contents<<input.rdbuf();
    input.close();
    REQUIRE(contents.str().find("\"event\":\"game_summary\"")!=std::string::npos);
    REQUIRE(contents.str().find("\"event\":\"capture_limit\"")!=std::string::npos);
    REQUIRE(contents.str().find("\"terminal_events_retained\":1")!=std::string::npos);
    REQUIRE(contents.str().find("\"event\":\"session_end\"")!=std::string::npos);
    std::filesystem::remove_all(root);
}

TEST_CASE("Performance capture aggregates every sample and flushes partial windows on stop", "[ai][telemetry][performance]") {
    const auto root=std::filesystem::temp_directory_path()/("dunecity-performance-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(),AITelemetry::Record()));
    const auto file=writer.path();
    writer.performance(10,4,"ai.build",10000);
    writer.performance(20,4,"ai.build",300000);
    writer.performance(30,4,"ai.build",40000);
    writer.performance(30,0,"ai.build",2000); // separate house
    writer.performance(30,4,"path.nodes",999999,33,false); // work is not a duration
    writer.slowFrame(20,350000,AITelemetry::Record().set("worst_house",4));
    writer.slowFrame(30,50000,AITelemetry::Record().set("worst_house",0));
    REQUIRE_FALSE(writer.isWorstFrame(340000));
    REQUIRE(writer.isWorstFrame(360000));
    writer.stop();
    std::ifstream in(file); std::string line, window, last;
    int windows=0;
    while(std::getline(in,line)) {
        if(line.find("\"event\":\"performance_window\"")!=std::string::npos) { window=line; ++windows; }
        last=line;
    }
    in.close();
    REQUIRE(windows==1);
    REQUIRE(window.find("\"count\":3,\"sum\":350000,\"max\":300000,\"max_cycle\":20")!=std::string::npos);
    REQUIRE(window.find("\"over_33ms\":2,\"over_100ms\":1,\"over_250ms\":1")!=std::string::npos);
    REQUIRE(window.find("\"unit\":\"count\",\"count\":1,\"sum\":999999,\"max\":999999,\"max_cycle\":30,\"over_33ms\":0")!=std::string::npos);
    REQUIRE(window.find("\"worst_frame_cycle\":20,\"worst_frame_us\":350000,\"worst_frame\":{\"worst_house\":4}")!=std::string::npos);
    REQUIRE(last.find("\"event\":\"session_end\"")!=std::string::npos);
    REQUIRE(last.find("\"cycle\":30")!=std::string::npos);
    REQUIRE(writer.start(root.string(),AITelemetry::Record()));
    const auto second=writer.path();
    REQUIRE(writer.isWorstFrame(1));
    writer.stop();
    std::ifstream secondIn(second); std::stringstream contents; contents<<secondIn.rdbuf();
    secondIn.close();
    REQUIRE(contents.str().find("ai.build")==std::string::npos);
    if(const char* artifact=std::getenv("DUNECITY_PERFORMANCE_TEST_EXPORT"))
        std::filesystem::copy_file(file,artifact,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove_all(root);
}

TEST_CASE("Every substantial frame stall is timestamped including smaller consecutive stalls", "[ai][telemetry][performance]") {
    const auto root=std::filesystem::temp_directory_path()/("dunecity-stalls-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(),AITelemetry::Record()));
    const auto file=writer.path();
    writer.frameStall(10,99999,AITelemetry::Record().set("kind","game_frame"));
    writer.frameStall(11,750000,AITelemetry::Record().set("kind","game_frame").set("ai_us",730000));
    writer.frameStall(12,100000,AITelemetry::Record().set("kind","game_frame"));
    writer.frameStall(13,1500000,AITelemetry::Record().set("kind","between_frames"));
    writer.stop();
    std::ifstream in(file);std::string line,all;int stalls=0;
    while (std::getline(in,line)) {
        if (line.find("\"event\":\"frame_stall\"")==std::string::npos) continue;
        ++stalls;all+=line;
        REQUIRE(line.find("\"session_wall_us\":")!=std::string::npos);
    }
    in.close();
    REQUIRE(stalls==3);
    REQUIRE(all.find("\"duration_us\":100000")!=std::string::npos);
    REQUIRE(all.find("\"duration_us\":1500000")!=std::string::npos);
    REQUIRE(all.find("\"ai_us\":730000")!=std::string::npos);
    REQUIRE(all.find("\"kind\":\"between_frames\"")!=std::string::npos);
    std::filesystem::remove_all(root);
}
