#include <catch2/catch_test_macros.hpp>
#include <dunecity/CitySpritePolicy.h>
#include <dunecity/StructureTerrainPolicy.h>
#include <FileClasses/lodepng.h>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace DuneCity;
using namespace DuneCity::CitySprites;

TEST_CASE("Transparent civic structures retain terrain beneath their footprint", "[city][sprites][terrain]") {
    for(int item : {Structure_NuclearPlant, Structure_PoliceStation,
                   Structure_Stadium, Structure_Airport}) {
        CHECK(DuneCity::shouldDrawTerrainBelowStructure(item));
    }
    // Existing transparent Tornie art must retain its exemption, while opaque
    // vanilla buildings still use the terrain-culling fast path.
    for(int item : {Structure_AdvancedWindTrap, Structure_AdvancedWindTrapMK2,
                   Structure_AdvancedWindTrapMK3, Structure_Worfinery,
                   Structure_TechCenter, Structure_ResearchLab, Structure_Scoutpost, Structure_Flamepost,
                   Structure_Chemipost, Structure_LoveFactory, Structure_ChaosFactory}) {
        CHECK(DuneCity::shouldDrawTerrainBelowStructure(item));
    }
    for(int item : {Structure_ConstructionYard, Structure_Barracks, Structure_Refinery}) {
        CHECK_FALSE(DuneCity::shouldDrawTerrainBelowStructure(item));
    }
}

TEST_CASE("All imported RCI models are reachable without changing simulation density", "[city][sprites]") {
    for (auto type : {ZoneType::Residential, ZoneType::Commercial, ZoneType::Industrial}) {
        const int cols = zoneColumns(type);
        const int tiers = type == ZoneType::Industrial ? 2 : 4;
        for (int tier = 0; tier < tiers; ++tier) {
            std::set<int> models;
            for (int pop : {0,1,2,3,4,5,6,7,8,16,24,32,40}) for (int x = 0; x < 30; ++x) for (int y = 0; y < 30; ++y) {
                const int d = ResidentialPopulation::density(pop);
                const auto frame = zoneFrame(type, d, tier, x, y, 0, false,pop);
                const int stride = type == ZoneType::Residential ? cols*2 : cols;
                models.insert(frame % stride);
                CHECK(frame / stride == (type == ZoneType::Industrial ? tier * 9 : tier));
            }
            CHECK(models.size() == static_cast<size_t>(type == ZoneType::Residential ? 29 : cols));
        }
    }
}

TEST_CASE("City animation frames respect density power and atlas bounds", "[city][sprites]") {
    for (auto type : {ZoneType::Residential, ZoneType::Commercial, ZoneType::Industrial}) {
        for (int density : {-1, 0, 1, 2, 3, 99}) for (int tier : {-1, 0, 1, 2, 3, 99}) {
            const int cols = zoneColumns(type);
            const int count = cols * zoneRows(type);
            for (uint32_t cycle : {0u, frameCycles, 7 * frameCycles, 0xffffffffu}) {
                const int frame = zoneFrame(type, density, tier, 17, 9, cycle, true);
                CHECK(frame >= 0);
                CHECK(frame < count);
                // Growth/value variants are stable with time, including when smoke runs.
                CHECK(frame % cols == zoneFrame(type, density, tier, 17, 9, 0, false) % cols);
                if (type == ZoneType::Industrial) {
                    CHECK((zoneFrame(type, density, tier, 17, 9, cycle, false) / cols) % 9 == 0);
                    CHECK(((frame / cols) % 9 > 0) == (density > 0));
                }
            }
        }
    }
    std::set<int> radar;
    for (uint32_t cycle = 0; cycle < 8 * frameCycles; cycle += frameCycles) {
        CHECK(poweredFrame(cycle, false) == 0);
        radar.insert(poweredFrame(cycle, true));
        CHECK(stadiumFrame(cycle, 1, 2, false) == 0);
    }
    CHECK(radar == std::set<int>{1,2,3,4,5,6,7,8});
    int matchSeconds = 0;
    for (int second = 0; second < 32; ++second)
        matchSeconds += stadiumFrame(second * MILLI2CYCLES(1000), 7, 11, true) != 0;
    CHECK(matchSeconds == 8);
}

TEST_CASE("Traffic follows Micropolis display thresholds and cannot leak through fog", "[city][sprites]") {
    for (int traffic = 0; traffic <= 255; ++traffic) {
        std::set<int> rows;
        for (uint32_t cycle = 0; cycle < 4 * frameCycles; cycle += frameCycles) {
            rows.insert(roadRow(traffic, cycle));
            CHECK(roadRow(traffic, cycle, false) == 0);
        }
        if (traffic < 64) CHECK(rows == std::set<int>{0});
        else if (traffic < 192) CHECK(rows == std::set<int>{1,2,3,4});
        else CHECK(rows == std::set<int>{5,6,7,8});
    }
}

namespace {
struct Pixels {
    std::vector<unsigned char> rgba;
    unsigned w = 0, h = 0;
    explicit Pixels(const std::string& name) {
        const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
        REQUIRE(root != nullptr);
        const auto path = std::string(root) + "/imported_sprites/micropolis/atlases/" + name + ".png";
        REQUIRE(lodepng::decode(rgba, w, h, path) == 0);
    }
    std::vector<unsigned char> cell(int col, int row, int size) const {
        std::vector<unsigned char> out;
        REQUIRE((col + 1) * size <= static_cast<int>(w));
        REQUIRE((row + 1) * size <= static_cast<int>(h));
        for (int y = row * size; y < (row + 1) * size; ++y) {
            auto start = rgba.begin() + (y * w + col * size) * 4;
            out.insert(out.end(), start, start + size * 4);
        }
        return out;
    }
};
}

TEST_CASE("Shipped city atlases match renderer dimensions and contain real animations", "[city][sprites][assets]") {
    struct Spec { const char* name; int cols, rows, cell; };
    for (const auto& spec : {Spec{"residential", residentialColumns, residentialRows, 32},
                            Spec{"commercial", commercialColumns, 4, 32},
                            Spec{"industrial", industrialColumns, industrialRows, 32},
                            Spec{"roads", 16, roadRows, 16},
                            Spec{"stadium", specialFrames, 1, 48},
                            Spec{"airport", specialFrames, 1, 48},
                            Spec{"nuclear", specialFrames, 1, 48}}) {
        INFO(spec.name);
        Pixels pixels(spec.name);
        REQUIRE(pixels.w == spec.cols * spec.cell);
        REQUIRE(pixels.h == spec.rows * spec.cell);
        CHECK(pixels.w * 3 <= 2048);
        CHECK(pixels.h * 3 <= 2048);
    }
    Pixels industry("industrial");
    for (int row = 1; row < industrialRows; ++row)
        CHECK(industry.cell(0, row, 32) == industry.cell(0, 0, 32)); // empty lots never smoke
    for (int tier = 0; tier < 2; ++tier) for (int model = 1; model <= 4; ++model) {
        const bool hasChimney = tier == 0 ? model != 2 : model >= 3;
        CHECK((industry.cell(model, tier * 9 + 1, 32) != industry.cell(model, tier * 9 + 2, 32)) == hasChimney);
    }
    for (const auto* name : {"stadium", "airport", "nuclear"}) {
        Pixels pixels(name);
        CHECK(pixels.cell(1, 0, 48) != pixels.cell(2, 0, 48));
    }
    Pixels roads("roads");
    for (int mask = 0; mask < 16; ++mask) {
        CHECK(roads.cell(mask, 1, 16) != roads.cell(mask, 2, 16));
        CHECK(roads.cell(mask, 5, 16) != roads.cell(mask, 6, 16));
    }
}

TEST_CASE("Residential house artwork changes only one perimeter site per growth step", "[city][sprites][assets]") {
    Pixels pixels("residential");
    const auto framePixels=[&](int pop,int x,int y,int tier) {
        const int frame=zoneFrame(ZoneType::Residential,ResidentialPopulation::density(pop),tier,x,y,0,true,pop);
        return pixels.cell(frame%residentialColumns,frame/residentialColumns,32);
    };
    for (int tier=0;tier<4;++tier) for (int site=0;site<3;++site) {
        auto previous=framePixels(0,site*3,9,tier);
        for (int pop=1;pop<=8;++pop) {
            const auto next=framePixels(pop,site*3,9,tier);
            int changed=0,minX=32,minY=32,maxX=-1,maxY=-1;
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                const int i=(y*32+x)*4;
                // Ignore faint Lanczos/unsharp halos outside the changed house tile.
                int delta=0;
                for (int c=0;c<4;++c) delta=std::max(delta,std::abs(int(previous[i+c])-int(next[i+c])));
                if (delta>8) {
                    ++changed; minX=std::min(minX,x); maxX=std::max(maxX,x);
                    minY=std::min(minY,y); maxY=std::max(maxY,y);
                }
            }
            REQUIRE(changed>0);
            REQUIRE(maxX-minX<14);
            REQUIRE(maxY-minY<14);
            previous=next;
        }
        for (int pop : {16,24,32,40}) {
            auto next=framePixels(pop,site*3,9,tier);
            REQUIRE(previous!=next);
            previous=next;
        }
    }
}
