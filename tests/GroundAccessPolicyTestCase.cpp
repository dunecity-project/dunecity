#include <catch2/catch_test_macros.hpp>
#include <players/GroundAccessPolicy.h>
#include <players/CityPlacementPolicy.h>

namespace {
struct AccessMap {
    int w=24,h=20;
    std::vector<uint8_t> tiles=std::vector<uint8_t>(w*h,1);
    void block(int x,int y,int width,int height) {
        for(int yy=y;yy<y+height;++yy) for(int xx=x;xx<x+width;++xx) tiles[yy*w+xx]=0;
    }
    bool allows(GroundAccessPolicy::Rect r,bool factory=true,bool diagonal=false) const {
        return GroundAccessPolicy::allows(r,factory,[&](int x,int y){
            return x>=0 && y>=0 && x<w && y<h && tiles[y*w+x];
        },diagonal);
    }
};
}
TEST_CASE("Local placement leaves an open perimeter", "[ai][placement]") {
    AccessMap m;
    REQUIRE(m.allows({8,8,2,2}));
    REQUIRE(m.allows({0,0,3,2})); // map edge needs no off-map road
    m.block(7,7,5,4);
    REQUIRE_FALSE(m.allows({8,8,3,2}));
}
TEST_CASE("Buildings can share a wall without sealing the surrounding passage", "[ai][placement]") {
    AccessMap m;m.block(5,5,2,2);
    REQUIRE(m.allows({7,5,2,2}));
    REQUIRE(m.allows({5,7,2,2}));
}
TEST_CASE("Local placement cannot close a lane between existing blocks", "[ai][placement]") {
    AccessMap m;m.block(5,3,2,10);m.block(8,3,2,10);
    REQUIRE_FALSE(m.allows({7,6,1,1},false));
    REQUIRE(m.allows({11,6,2,2}));
}
TEST_CASE("Reservations and mountains use the same local occupancy rules", "[ai][placement]") {
    AccessMap m;m.block(5,3,2,10);
    REQUIRE(m.allows({7,6,1,1},false));
    m.block(8,3,2,10); // reserved neighbouring building
    REQUIRE_FALSE(m.allows({7,6,1,1},false));
}
TEST_CASE("Rocket junctions retain diagonal movement around the turret", "[ai][placement]") {
    AccessMap m;
    REQUIRE(m.allows({8,8,1,1},false,true));
    m.block(7,7,1,3);m.block(9,7,1,3);
    REQUIRE_FALSE(m.allows({8,8,1,1},false,true));
}
TEST_CASE("Placement inspection is bounded independently of map size", "[ai][placement]") {
    int reads=0;
    REQUIRE(GroundAccessPolicy::allows({500,500,3,2},true,[&](int x,int y){
        REQUIRE(x>=499);REQUIRE(x<=503);REQUIRE(y>=499);REQUIRE(y<=502);
        ++reads;return true;
    }));
    REQUIRE(reads==20);
}
TEST_CASE("Residential gaps only outrank equally suitable expansion sites", "[ai][placement]") {
    using CityPlacementPolicy::preferCitySite;
    REQUIRE_FALSE(preferCitySite(true,2,0,0,true,0,2,500));
    REQUIRE(preferCitySite(true,2,2,0,true,0,2,500));
    REQUIRE_FALSE(preferCitySite(false,3,2,500,true,0,0,0));
    REQUIRE(preferCitySite(true,0,2,500,true,2,0,0));
    REQUIRE_FALSE(preferCitySite(true,0,2,500,true,2,2,0));
}
TEST_CASE("Residential placement ties retain the first real candidate", "[ai][placement]") {
    using CityPlacementPolicy::preferCitySite;
    REQUIRE_FALSE(preferCitySite(true,1,8,301,true,0,8,301));
    REQUIRE_FALSE(preferCitySite(true,2,8,264,true,2,8,264));
    // One north neighbour is not infill: it must not beat the higher-scoring site.
    REQUIRE_FALSE(preferCitySite(true,1,8,264,true,0,8,301));
    REQUIRE(preferCitySite(true,0,8,301,true,1,8,264));
}

#include "performance/reference-ground-access.inc"
TEST_CASE("Word-sized access checks match the original graph for every 2x2 occupancy", "[ai][placement]") {
    for (unsigned mask=0;mask<65536;++mask) {
        auto passable=[&](int x,int y) {return (mask & (1u<<(y*4+x)))!=0;};
        for (bool diagonal:{false,true}) for (bool exit:{false,true})
            REQUIRE(GroundAccessPolicy::allows({1,1,2,2},exit,passable,diagonal)
                ==ReferenceGroundAccessPolicy::allows({1,1,2,2},exit,passable,diagonal));
    }
}
TEST_CASE("Access checks preserve larger footprints and the vector fallback", "[ai][placement]") {
    unsigned random=123;
    for (int w=1;w<=8;++w) for (int h=1;h<=8;++h) for (int round=0;round<40;++round) {
        std::vector<bool> open((w+2)*(h+2));
        for (size_t i=0;i<open.size();++i) {random=random*1664525u+1013904223u;open[i]=(random>>29)!=0;}
        auto passable=[&](int x,int y) {return open[y*(w+2)+x];};
        for (bool diagonal:{false,true}) for (bool exit:{false,true})
            REQUIRE(GroundAccessPolicy::allows({1,1,w,h},exit,passable,diagonal)
                ==ReferenceGroundAccessPolicy::allows({1,1,w,h},exit,passable,diagonal));
    }
}
