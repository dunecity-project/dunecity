#include <catch2/catch_test_macros.hpp>

#include <mod/Dune2RAssetManager.h>
#include <Network/ENetHttp.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>

namespace {
struct CatalogFixture {
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("dune2r-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string contents;
    CatalogFixture() {
        const auto source = std::filesystem::path(std::getenv("DUNE_CITY_SOURCE_DIR"))
                            / "mods/Dune2R/asset-catalog.ini";
        std::filesystem::create_directories(root);
        std::filesystem::copy_file(source, root / "asset-catalog.ini");
        std::ifstream input(source);
        contents.assign(std::istreambuf_iterator<char>(input), {});
    }
    ~CatalogFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
}

TEST_CASE("Dune2R refreshed catalogs persist independently of bundled catalogs", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto oldRevision = manager.getRevision();
    auto updated = fixture.contents;
    const std::string revision(40, 'a');
    size_t pos = 0;
    while((pos = updated.find(oldRevision, pos)) != std::string::npos) {
        updated.replace(pos, oldRevision.size(), revision);
        pos += revision.size();
    }
    const auto result = manager.applyCatalog(updated);
    INFO(result.message);
    REQUIRE(result.success);
    CHECK(result.changed);
    CHECK(manager.getRevision() == revision);
    CHECK(Dune2RAssetManager(fixture.root.string()).getRevision() == revision);
    CHECK(std::filesystem::exists(fixture.root / "asset-catalog.ini"));
}

TEST_CASE("Dune2R rejects untrusted catalog updates without losing the installed catalog", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto revision = manager.getRevision();
    const auto packCount = manager.getPacks().size();
    auto badHost = fixture.contents;
    badHost.replace(badHost.find("raw.githubusercontent.com"), 25, "untrusted.invalid");
    auto executable = fixture.contents;
    const auto pathStart = executable.find("File.0=") + 7;
    executable.replace(pathStart, executable.find('|', pathStart) - pathStart, "execute.py");
    auto traversal = fixture.contents;
    traversal.replace(pathStart, traversal.find('|', pathStart) - pathStart, "../outside.png");
    for(const auto& candidate : {std::string(), std::string(1024 * 1024 + 1, 'x'), badHost, executable, traversal}) {
        CHECK_FALSE(manager.applyCatalog(candidate).success);
        CHECK(manager.getRevision() == revision);
        CHECK(manager.getPacks().size() == packCount);
    }
    CHECK_FALSE(std::filesystem::exists(fixture.root / "asset-catalog-online.ini"));
}

TEST_CASE("Dune2R catalog supports the transferred repository without widening trust", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const std::string suffix = manager.getRevision() + "/mods/Dune2R/graphics_hd/units";
    const std::string legacy = "https://raw.githubusercontent.com/VR48/dunecity/";
    const std::string organization = "https://raw.githubusercontent.com/dunecity-project/dunecity/";
    for(const auto& trusted : {legacy, organization}) {
        auto candidate = fixture.contents;
        const auto start = candidate.find("BaseURL=") + 8;
        candidate.replace(start, candidate.find_first_of("\r\n", start) - start, trusted + suffix);
        REQUIRE(manager.applyCatalog(candidate).success);
    }
    for(const auto& untrusted : {
            std::string("https://raw.githubusercontent.com/dunecity-project/other/"),
            std::string("https://raw.githubusercontent.com/other/dunecity/"),
            std::string("http://raw.githubusercontent.com/dunecity-project/dunecity/")}) {
        auto candidate = fixture.contents;
        const auto start = candidate.find("BaseURL=") + 8;
        candidate.replace(start, candidate.find_first_of("\r\n", start) - start, untrusted + suffix);
        CHECK_FALSE(manager.applyCatalog(candidate).success);
    }
}

TEST_CASE("Dune2R falls back to its bundled catalog when its online cache is corrupt", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto expected = Dune2RAssetManager(fixture.root.string()).getPacks().size();
    {
        std::ofstream output(fixture.root / "asset-catalog-online.ini");
        output << "not a catalog";
    }
    Dune2RAssetManager manager(fixture.root.string());
    CHECK(manager.getPacks().size() == expected);
}

TEST_CASE("Dune2R asset paths reject traversal", "[Dune2RAssets]") {
    CHECK(Dune2RAssetManager::isSafeRelativeAssetPath("atlases/idle/east.png"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("../unit.ini"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("/unit.ini"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("atlases\\east.png"));
    CHECK_FALSE(Dune2RAssetManager::isSafeRelativeAssetPath("atlases/east file.png"));
}

TEST_CASE("Dune2R asset SHA-256 matches the standard vector", "[Dune2RAssets]") {
    const auto path = std::filesystem::temp_directory_path() / "dunecity-sha256-test.txt";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    CHECK(Dune2RAssetManager::sha256File(path.string())
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("Dune2R source catalog loads immutable packs", "[Dune2RAssets]") {
    const char* source = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(source != nullptr);
    const auto modPath = std::filesystem::path(source) / "mods" / "Dune2R";
    Dune2RAssetManager manager(modPath.string());
    REQUIRE(manager.getRevision().size() == 40);
    REQUIRE(manager.getRevision().find_first_not_of("0123456789abcdef") == std::string::npos);
    for(const auto* id : {"gravel", "sand", "refinery", "harkonnendevastator", "ordostank", "harkonneninfantry"}) {
        const auto& packs = manager.getPacks();
        const auto found = std::find_if(packs.begin(), packs.end(), [&](const auto& pack) { return pack.id == id; });
        REQUIRE(found != packs.end());
        CHECK(found->variant == "remastered");
        CHECK(found->totalBytes() > 0);
        if(found->id == "harkonneninfantry") {
            CHECK(found->files.size() == 49);
            CHECK(found->displayName == "Harkonnen Infantry Remastered");
        }
        if(found->id == "refinery") {
            CHECK(found->displayName == "Atreides Refinery Remastered");
        }
    }
}

TEST_CASE("Dune2R downloader resumes and verifies a live asset", "[Dune2RAssets][network]") {
    const char* enabled = std::getenv("DUNE2R_NETWORK_TEST");
    if(enabled == nullptr || std::string(enabled) != "1") {
        SKIP("Set DUNE2R_NETWORK_TEST=1 to run the live GitHub download check");
    }

    const char* source = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(source != nullptr);
    const std::string revision = "1275ed1036828b8f3365395c66ca4499cd2d266a";
    const std::string baseURL = "https://raw.githubusercontent.com/VR48/dunecity/" + revision
                                + "/mods/Dune2R/graphics_hd/units";
    const std::string remoteData = loadFromHttp(
        baseURL + "/harkonnendevastator/unit.ini");
    REQUIRE(remoteData.size() > 64);

    const auto remoteCopy = std::filesystem::temp_directory_path()
                            / "dunecity-dune2r-network-source.ini";
    {
        std::ofstream output(remoteCopy, std::ios::binary | std::ios::trunc);
        output.write(remoteData.data(), static_cast<std::streamsize>(remoteData.size()));
    }
    const uint64_t sourceSize = remoteData.size();
    const std::string sourceHash = Dune2RAssetManager::sha256File(remoteCopy.string());

    const auto root = std::filesystem::temp_directory_path() / "dunecity-dune2r-network-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream catalog(root / "asset-catalog.ini", std::ios::trunc);
        catalog << "[Catalog]\nSchema=1\nRevision=" << revision << "\n"
                << "BaseURL=" << baseURL << "\nPackCount=1\n\n"
                << "[Pack.0]\nID=smoke\nDisplayName=Network Smoke Test\n"
                << "Variant=remastered\nUnit=harkonnendevastator\nFileCount=1\n"
                << "File.0=unit.ini|" << sourceSize << "|" << sourceHash << "\n";
    }

    const auto partial = root / "graphics_hd" / "units"
                         / "harkonnendevastator.download" / "unit.ini.part";
    std::filesystem::create_directories(partial.parent_path());
    {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        output.write(remoteData.data(), 64);
    }

    Dune2RAssetManager manager(root.string());
    const auto result = manager.install({"smoke"});
    INFO(result.message);
    REQUIRE(result.success);
    REQUIRE(result.changed);
    REQUIRE(manager.isPackInstalled(manager.getPacks().front()));
    std::filesystem::remove_all(root);
    std::filesystem::remove(remoteCopy);
}
