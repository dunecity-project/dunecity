#include <catch2/catch_test_macros.hpp>
#include <players/AIDecisionLog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

TEST_CASE("Disabled diagnostics close structured capture and skip subsequent records", "[ai][telemetry]") {
    const auto root=std::filesystem::temp_directory_path()/("dunecity-disabled-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
