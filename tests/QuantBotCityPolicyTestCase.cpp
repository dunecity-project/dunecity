#include <players/QuantBotCityPolicy.h>
#include <catch2/catch_test_macros.hpp>
#include <climits>
using namespace QuantBotCityPolicy;
TEST_CASE("Custom city ceilings match approved area and difficulty matrix", "[quantbot][city]") {
    const int areas[]={1024,4096,16384,65536};
    const int expected[][4]={{5000,10000,20000,30000},{10000,20000,40000,60000},{20000,40000,80000,120000}};
    const int hardMatching[]={20000,40000,60000,90000};
    for(int d=0;d<3;++d)for(int band=0;band<4;++band) {
        REQUIRE(populationLimit(d,areas[band],false)==expected[d][band]);
        if(band<3) REQUIRE(populationLimit(d,areas[band]+1,false)==expected[d][band+1]);
        // HardCityMatch changes Hard's row only.
        REQUIRE(populationLimit(d,areas[band],true)==(d==Hard ? hardMatching[band] : expected[d][band]));
    }
    REQUIRE(zoneLimit(populationLimit(Easy,16384,true))==48);
    REQUIRE(zoneLimit(90000)==216);
    for(int area:areas) {
        REQUIRE(populationLimit(Brutal,area,true)==kUnlimited);
        REQUIRE(zoneLimit(populationLimit(Brutal,area,true))==kUnlimited);
        REQUIRE(admitsZone(limits(populationLimit(Brutal,area,true)),9999,999999,999999,999999));
    }
}
TEST_CASE("Hard's city ceiling rises to a bigger rival city and never below its base", "[quantbot][city]") {
    REQUIRE(withLargestRival(90000,0)==90000);
    REQUIRE(withLargestRival(90000,90000)==90000);
    REQUIRE(withLargestRival(90000,135000)==135000);
    REQUIRE(withLargestRival(kUnlimited,135000)==kUnlimited);
    // The land allowance follows the raised ceiling.
    REQUIRE(limits(withLargestRival(90000,135000)).sharedZoneCap==zoneLimit(135000));
    REQUIRE(zoneLimit(135000)>zoneLimit(90000));
}
TEST_CASE("City admission reserves queued population and land without overflow", "[quantbot][city]") {
    const auto cap=limits(populationLimit(Easy,16384,true));
    REQUIRE(admitsZone(cap,47,19600,80,320));
    REQUIRE_FALSE(admitsZone(cap,48,0,0,20));
    REQUIRE_FALSE(admitsZone(cap,47,19600,100,320));
    REQUIRE_FALSE(allowsPopulation(INT_MAX,INT_MAX,INT_MAX,20000));
    REQUIRE_FALSE(admitsZone(cap,1,20001,0,20));
}
