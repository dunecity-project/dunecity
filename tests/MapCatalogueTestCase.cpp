// Map chooser duplicate handling and sidebar city-stat text fitting.
//
// The chooser bug: "All Maps" concatenates four map directories, so a map that
// exists in more than one of them (metaserver download installed into the user
// profile, editor save of a bundled map, a metadata-annotated copy left beside
// its original) was listed once per copy under one display name.
#include <catch2/catch_test_macros.hpp>

#include <GUI/ObjectInterfaces/CityStatText.h>
#include <INIMap/MapCatalogue.h>
#include <INIMap/MapMetadata.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string sourceDir() {
    const char* dir = std::getenv("DUNE_CITY_SOURCE_DIR");
    return dir ? dir : ".";
}

std::string readBundledMap() {
    const std::filesystem::path path =
        std::filesystem::path(sourceDir()) / "data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini";
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// The annotation a chooser/editor pass adds: a display name and revision keys.
std::string annotated(const std::string& map) {
    const auto basic = map.find("[BASIC]");
    REQUIRE(basic != std::string::npos);
    const auto lineEnd = map.find('\n', basic);
    REQUIRE(lineEnd != std::string::npos);
    return map.substr(0, lineEnd + 1)
         + "Name=Alkozeltser 4 Cities\nMapVersion=2\nMod=dunecity\n"
         + map.substr(lineEnd + 1);
}

/// A genuinely different map that happens to carry the same name.
std::string differentTerrain(const std::string& map) {
    const auto row = map.find("\n000=");
    REQUIRE(row != std::string::npos);
    std::string changed = map;
    changed[row + 5] = changed[row + 5] == '~' ? '%' : '~';
    return changed;
}

} // namespace

TEST_CASE("Released city maps retain their playable catalogue identities", "[maps][chooser]") {
    struct Expected { const char* file; const char* name; int size; int players; int revision; };
    for(const auto& expected : std::vector<Expected>{
        {"4P - 128x128 - 4 corners", "4P - 128x128 - 4 corners", 128, 4, 1},
        {"4P - 192x192 - DuneCity", "4P - 192x192 - DuneCity", 192, 4, 0},
        {"5P - 256x256 - test", "5P - 256x256 - test", 256, 5, 0},
        // Neutral is authored as the eighth house; it is not a selectable catalogue player.
        {"8P - 128x128 - city seige", "8P - 128x128 - city seige", 128, 7, 0},
        {"Alkozeltser 4 Cities", "Alkozeltser 4 Cities", 256, 6, 1},
    }) {
        CAPTURE(expected.file);
        const auto path = std::filesystem::path(sourceDir()) / "data/maps/singleplayer"
                        / (std::string(expected.file) + ".ini");
        REQUIRE(std::filesystem::is_regular_file(path));
        INIFile ini(path.string());
        const auto metadata = MapMetadata::read(ini, expected.file);
        REQUIRE(metadata.name == expected.name);
        REQUIRE(metadata.width == expected.size);
        REQUIRE(metadata.height == expected.size);
        REQUIRE(metadata.players == expected.players);
        REQUIRE(metadata.version == expected.revision);
        REQUIRE(metadata.mod == MapMetadata::ModDuneCity);
        REQUIRE(metadata.dependency == MapMetadata::ModDuneCity);
        REQUIRE(metadata.matches(MapMetadata::ModDuneCity, 0, 0));
    }
}

TEST_CASE("Equivalent copies of a real map share one content key", "[maps][chooser]") {
    const std::string bundled = readBundledMap();
    const std::string key = MapCatalogue::contentKey(bundled);
    REQUIRE_FALSE(key.empty());

    // Metadata-only edits (name, revision, mod tag) do not change the map.
    REQUIRE(MapCatalogue::contentKey(annotated(bundled)) == key);
    // Neither do comments, blank lines, CRLF endings or trailing spaces.
    REQUIRE(MapCatalogue::contentKey("; exported copy\n\n" + bundled + "\n") == key);
    std::string spaced = bundled;
    for(auto at = spaced.find("\n["); at != std::string::npos; at = spaced.find("\n[", at + 3))
        spaced.insert(at + 1, "  ");
    REQUIRE(MapCatalogue::contentKey(spaced) == key);

    // A single changed terrain tile is a different map.
    REQUIRE(MapCatalogue::contentKey(differentTerrain(bundled)) != key);
    // Nothing identifying means nothing to merge on.
    REQUIRE(MapCatalogue::contentKey("[BASIC]\nName=Empty\n").empty());
}

TEST_CASE("The chooser lists one row per distinct map copy", "[maps][chooser]") {
    const std::string bundled = readBundledMap();
    const std::string key = MapCatalogue::contentKey(bundled);
    const std::string other = MapCatalogue::contentKey(differentTerrain(bundled));

    auto indices = [](const std::vector<MapCatalogue::Kept>& kept) {
        std::vector<std::size_t> result;
        for(const auto& row : kept) result.push_back(row.index);
        return result;
    };

    SECTION("a download in the user profile collapses onto the installed copy") {
        // Scan order is SP install, SP user, MP install, MP user: a download or
        // editor save in the user profile must not displace the installed map,
        // or a caller's map list no longer contains the selected path.
        const std::vector<MapCatalogue::Copy> copies = {
            {"Alkozeltser 4 Cities", key, 0},   // installed map directory
            {"Alkozeltser 4 Cities", key, 2},   // metaserver download in the user profile
            {"Alkozeltser 4 Cities", other, 0}, // same name, different map
        };
        const auto kept = MapCatalogue::keptCopies(copies);
        REQUIRE(indices(kept) == std::vector<std::size_t>{0, 2});
        // The surviving row still reports the newest revision of the group.
        REQUIRE(kept[0].version == 2);
        REQUIRE(kept[1].version == 0);
    }

    SECTION("an annotated copy beside its original merges either way round") {
        const std::vector<MapCatalogue::Copy> copies = {
            {"Alkozeltser 4 Cities", key, 2},
            {"alkozeltser 4 cities ", key, 0},  // case/space only differences still merge
        };
        const auto kept = MapCatalogue::keptCopies(copies);
        REQUIRE(indices(kept) == std::vector<std::size_t>{0});
        REQUIRE(kept[0].version == 2);
    }

    SECTION("equal copies keep the path the catalogue scanned first") {
        const std::vector<MapCatalogue::Copy> copies = {
            {"Twin Cities", key, 1}, {"Twin Cities", key, 1}};
        REQUIRE(indices(MapCatalogue::keptCopies(copies)) == std::vector<std::size_t>{0});
    }

    SECTION("different names, different content and unread entries are never merged") {
        const std::vector<MapCatalogue::Copy> copies = {
            {"Alkozeltser 4 Cities", key, 0},
            {"Alkozeltser 4", key, 0},          // same content, different name
            {"Alkozeltser 4 Cities", other, 0}, // same name, different content
            {"Alkozeltser 4 Cities", "", 3},    // metaserver row: no local file read
            {"Alkozeltser 4 Cities", "", 3},
        };
        REQUIRE(indices(MapCatalogue::keptCopies(copies)) == std::vector<std::size_t>{0, 1, 2, 3, 4});
    }
}

TEST_CASE("Map copy identity preserves BASIC gameplay rules and mod variants", "[maps][chooser]") {
    const std::string base="[BASIC]\nVersion=2\nTechLevel=4\nWinFlags=3\nTimeOut=0\n[MAP]\nSizeX=2\nSizeY=2\n000=%%\n001=%%\n";
    const auto key=MapCatalogue::contentKey(base);
    for(const std::pair<std::string,std::string>& rule : {
            std::pair<std::string,std::string>{"TechLevel=4","TechLevel=8"},
            {"WinFlags=3","WinFlags=1"}, {"TimeOut=0","TimeOut=300"}, {"Version=2","Version=1"}}) {
        auto changed=base;changed.replace(changed.find(rule.first),rule.first.size(),rule.second);
        REQUIRE(MapCatalogue::contentKey(changed)!=key);
    }
    REQUIRE(MapCatalogue::contentKey(base+"[Workshop]\nID=another-copy\nVersion=9\n")==key);
    const std::vector<MapCatalogue::Copy> copies={
        {"Same map",key,1,"vanilla"}, {"Same map",key,1,"dunecity"}};
    REQUIRE(MapCatalogue::keptCopies(copies).size()==2);
}

TEST_CASE("A locally edited workshop map keeps its authored mod in the type filter", "[maps][chooser]") {
    // The reported case: a DuneCity map downloaded, edited in the editor and saved.
    // The edit removed the city buildings that name the category, so the content
    // classifier reads it as vanilla, while the sidecar still records the exact mod
    // revision it belongs to. Filtering on the derived category alone hid the map
    // from the very mod it was authored for. Neither value is rewritten here: the
    // filter accepts either, so content classification and the revision metadata
    // both stay as they were.
    MapMetadata edited;
    edited.name = "4P - 192x192 - New Beginning";
    edited.width = edited.height = 192;
    edited.players = 4;
    edited.mod = MapMetadata::ModVanilla;        // derived from [STRUCTURES]
    edited.dependency = MapMetadata::ModDuneCity; // from <map>.ini.workshop.ini

    REQUIRE(edited.matches("", 0, 0));
    REQUIRE(edited.matches(MapMetadata::ModDuneCity, 0, 0));
    REQUIRE(edited.matches(MapMetadata::ModVanilla, 0, 0));
    REQUIRE_FALSE(edited.matches(MapMetadata::ModTornie, 0, 0));
    // Size and player filters are unaffected by the dependency.
    REQUIRE(edited.matches(MapMetadata::ModDuneCity, 3, 4));
    REQUIRE_FALSE(edited.matches(MapMetadata::ModDuneCity, 2, 0));
    REQUIRE_FALSE(edited.matches(MapMetadata::ModDuneCity, 0, 6));

    // An ordinary map, bundled or downloaded, has dependency == mod (read() sets
    // it) and so is unchanged: it never leaks into another mod's filter.
    MapMetadata plain;
    plain.width = plain.height = 64;
    plain.players = 2;
    plain.mod = plain.dependency = MapMetadata::ModVanilla;
    REQUIRE(plain.matches(MapMetadata::ModVanilla, 0, 0));
    REQUIRE_FALSE(plain.matches(MapMetadata::ModDuneCity, 0, 0));
    REQUIRE_FALSE(plain.matches(MapMetadata::ModTornie, 0, 0));

    // A metaserver row carries no dependency at all, and must not match everything.
    MapMetadata remote;
    remote.width = remote.height = 128;
    remote.players = 4;
    remote.mod = MapMetadata::ModDuneCity;
    remote.dependency = "";
    REQUIRE(remote.matches(MapMetadata::ModDuneCity, 0, 0));
    REQUIRE_FALSE(remote.matches(MapMetadata::ModVanilla, 0, 0));
}

TEST_CASE("City stat lines are shortened to the sidebar column", "[citystats][sidebar]") {
    // Deterministic stand-in for the bitmap font: 6 pixels per character.
    const auto measure = [](const std::string& text) { return static_cast<int>(text.size()) * 6; };
    const int column = 119; // SIDEBARWIDTH - 25

    REQUIRE(CityStatText::fit(" Crime: Safe", column, measure) == " Crime: Safe");
    for(const char* line : {" Pollution: Very Heavy (emits)", " Local pollution: Very Heavy",
                            " Value: Middle Class", " Crime: Dangerous",
                            " Pop: 12345 (lvl 12/16)", " Role: Residential"}) {
        const std::string fitted = CityStatText::fit(line, column, measure);
        INFO(line << " -> " << fitted);
        REQUIRE(measure(fitted) <= column);
        REQUIRE_FALSE(fitted.empty());
    }
    // The emitter marker survives abbreviation; it is only dropped if it must be.
    REQUIRE(CityStatText::fit(" Pollution: Very Heavy (emits)", column, measure).find("(e)")
            != std::string::npos);
    // A column too narrow for the qualifier keeps the value itself readable.
    REQUIRE(CityStatText::fit(" Pollution: Very Heavy (emits)", 95, measure) == " Poll.: V.Heavy");

    SECTION("an impossible column still yields a drawable, valid string") {
        const std::string fitted = CityStatText::fit(" Pollution: Very Heavy (emits)", 24, measure);
        REQUIRE(measure(fitted) <= 24);
        REQUIRE(fitted.substr(fitted.size() - 3) == "\xE2\x80\xA6");
    }

    SECTION("an unknown column width leaves the text alone") {
        REQUIRE(CityStatText::fit(" Crime: Dangerous", 0, measure) == " Crime: Dangerous");
    }
}
