#include <catch2/catch_test_macros.hpp>

#include <mod/Dune2RAssetManager.h>
#include <Network/ENetHttp.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

void writeOfflinePack(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "graphics_hd" / "units" / "offlineunit");
    const std::string bytes = "abc";
    {
        std::ofstream payload(root / "graphics_hd" / "units" / "offlineunit" / "unit.ini", std::ios::binary);
        payload << bytes;
    }
    std::ofstream catalog(root / "asset-catalog.ini");
    const std::string revision(40, 'b');
    catalog << "[Catalog]\nSchema=1\nRevision=" << revision << '\n'
            << "BaseURL=https://raw.githubusercontent.com/dunecity-project/dunecity/"
            << revision << "/mods/Dune2R/graphics_hd/units\nPackCount=1\n"
            << "[Pack.0]\nID=offline\nDisplayName=Offline fixture\nVariant=remastered\n"
            << "Unit=offlineunit\nFileCount=1\nFile.0=unit.ini|" << bytes.size() << '|'
            << Dune2RAssetManager::sha256Bytes(bytes) << '\n';
}

// One pack whose two files hold the same bytes. Only the first is installed, so the
// second can be completed from the verified local duplicate and the download path is
// exercised without any network request.
void writeDuplicatePack(const std::filesystem::path& root, const std::string& version) {
    const std::string bytes = "identical remastered pose";
    const auto unit = root / "graphics_hd" / "units" / "duplicateunit";
    std::filesystem::create_directories(unit / "atlases" / "idle");
    {
        std::ofstream payload(unit / "unit.ini", std::ios::binary);
        payload << bytes;
    }
    const std::string revision(40, 'c');
    std::ofstream catalog(root / "asset-catalog.ini", std::ios::trunc);
    catalog << "[Catalog]\nSchema=1\nRevision=" << revision << '\n';
    if(!version.empty()) catalog << "Version=" << version << '\n';
    catalog << "BaseURL=https://raw.githubusercontent.com/dunecity-project/dunecity/"
            << revision << "/mods/Dune2R/graphics_hd/units\nPackCount=1\n"
            << "[Pack.0]\nID=duplicate\nDisplayName=Duplicate fixture\nVariant=remastered\n"
            << "Unit=duplicateunit\nFileCount=2\nFile.0=unit.ini|" << bytes.size() << '|'
            << Dune2RAssetManager::sha256Bytes(bytes) << '\n'
            << "File.1=atlases/idle/east.png|" << bytes.size() << '|'
            << Dune2RAssetManager::sha256Bytes(bytes) << '\n';
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    for(size_t position = text.find(from); position != std::string::npos;
        position = text.find(from, position + to.size())) {
        text.replace(position, from.size(), to);
    }
    return text;
}

// A catalog published at another asset revision without the Harkonnen Infantry pack,
// which is the shape production still serves online.
std::string withoutInfantryPack(std::string contents, const std::string& currentRevision,
                                const std::string& newRevision = std::string(40, 'a')) {
    const auto infantry = contents.find("[Pack.2]");
    const auto following = contents.find("[Pack.3]");
    REQUIRE(infantry != std::string::npos);
    REQUIRE(following != std::string::npos);
    REQUIRE(contents.find("ID=harkonneninfantry") != std::string::npos);
    contents.erase(infantry, following - infantry);
    for(int pack = 3; pack <= 5; ++pack) {
        contents = replaceAll(contents, "[Pack." + std::to_string(pack) + "]",
                              "[Pack." + std::to_string(pack - 1) + "]");
    }
    contents = replaceAll(contents, "PackCount=6", "PackCount=5");
    REQUIRE(contents.find("ID=harkonneninfantry") == std::string::npos);
    return replaceAll(contents, currentRevision, newRevision);
}

std::string withoutCatalogVersion(std::string contents) {
    const auto version = contents.find("Version=");
    REQUIRE(version != std::string::npos);
    contents.erase(version, contents.find('\n', version) + 1 - version);
    return contents;
}

std::string withCatalogVersion(std::string contents, uint64_t version) {
    const std::string field = "Version=" + std::to_string(version);
    const auto existing = contents.find("Version=");
    if(existing != std::string::npos) {
        contents.replace(existing, contents.find('\n', existing) - existing, field);
        return contents;
    }
    const auto revision = contents.find("Revision=");
    REQUIRE(revision != std::string::npos);
    return contents.insert(contents.find('\n', revision) + 1, field + "\n");
}

// The catalog production still publishes online: an older asset revision, five packs
// without Harkonnen Infantry, and no publication counter at all.
std::string olderPublishedCatalog(const std::string& contents, const std::string& currentRevision) {
    return withoutCatalogVersion(withoutInfantryPack(contents, currentRevision));
}

std::map<std::string, std::string> directoryFingerprint(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        result[entry.path().lexically_relative(root).generic_string()] = entry.is_directory()
            ? "directory" : Dune2RAssetManager::sha256File(entry.path().string());
    }
    return result;
}
}

TEST_CASE("Dune2R snapshot downloads and catalog writes are refused before side effects", "[Dune2RAssets][workshop]") {
    CatalogFixture fixture;
    const std::string message =
        "Switch to the Dune2R working mod to download assets; online snapshots are immutable.";
    for(const auto& folder : {"ws-" + std::string(64, 'a'), std::string("ws-incomplete")}) {
        const auto snapshot = fixture.root / folder;
        writeOfflinePack(snapshot);
        const auto before = directoryFingerprint(snapshot);
        // A trailing directory separator must not bypass the immutable folder guard.
        Dune2RAssetManager manager(snapshot.string() + "/");
        REQUIRE(manager.isReadOnly());
        REQUIRE(manager.getPacks().size() == 1);
        CHECK(manager.isPackInstalled(manager.getPacks().front()));
        const auto revision = manager.getRevision();
        bool progressCalled = false;
        for(const auto& selection : {std::vector<std::string>{},
                                     std::vector<std::string>{"missing"},
                                     std::vector<std::string>{"offline"}}) {
            const auto result = manager.install(selection, [&](const Dune2RAssetProgress&) {
                progressCalled = true;
                return false;
            });
            CHECK_FALSE(result.success);
            CHECK_FALSE(result.changed);
            CHECK(result.message == message);
        }
        for(const auto& contents : {std::string(), fixture.contents}) {
            const auto result = manager.applyCatalog(contents);
            CHECK_FALSE(result.success);
            CHECK_FALSE(result.changed);
            CHECK(result.message == message);
        }
        const auto refresh = manager.refreshCatalog();
        CHECK_FALSE(refresh.success);
        CHECK_FALSE(refresh.changed);
        CHECK(refresh.message == message);
        CHECK_FALSE(progressCalled);
        CHECK(manager.getRevision() == revision);
        CHECK(directoryFingerprint(snapshot) == before);
    }
}

TEST_CASE("Dune2R immutable sidecars protect snapshots under a non-Workshop folder name", "[Dune2RAssets][workshop]") {
    CatalogFixture fixture;
    const auto snapshot = fixture.root / "renamed-snapshot";
    writeOfflinePack(snapshot);
    {
        std::ofstream metadata(snapshot / "workshop-revision.ini");
        metadata << "[Workshop]\nImmutable = true\n";
    }
    const auto before = directoryFingerprint(snapshot);
    Dune2RAssetManager manager(snapshot.string());
    REQUIRE(manager.isReadOnly());
    CHECK(manager.isPackInstalled(manager.getPacks().front()));
    CHECK_FALSE(manager.applyCatalog(fixture.contents).success);
    CHECK_FALSE(manager.refreshCatalog().success);
    CHECK_FALSE(manager.install({"offline"}).success);
    CHECK(directoryFingerprint(snapshot) == before);
}

TEST_CASE("Dune2R mutable working mods retain catalog refresh and verified offline installation", "[Dune2RAssets][workshop]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    {
        std::ofstream metadata(working / "workshop-revision.ini");
        metadata << "[Workshop]\nImmutable = false\n";
    }
    Dune2RAssetManager manager(working.string());
    REQUIRE_FALSE(manager.isReadOnly());
    const auto installed = manager.install({"offline"});
    CHECK(installed.success);
    CHECK_FALSE(installed.changed);
    CHECK(manager.isPackInstalled(manager.getPacks().front()));
    CHECK(manager.install({}).message == "No Dune2R asset pack was selected.");
    CHECK(manager.install({"missing"}).message == "Unknown Dune2R asset pack: missing");
    const auto refreshed = manager.applyCatalog(fixture.contents);
    CHECK(refreshed.success);
    CHECK(std::filesystem::is_regular_file(working / "asset-catalog-online.ini"));
}

TEST_CASE("Dune2R refreshed catalogs persist independently of bundled catalogs", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto oldRevision = manager.getRevision();
    const std::string revision(40, 'a');
    // A new asset revision is a new target, so it arrives as the next publication.
    const auto updated = withCatalogVersion(replaceAll(fixture.contents, oldRevision, revision),
                                            manager.getCatalogVersion() + 1);
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
        // A different base URL is a different target, so it carries the next number.
        candidate = withCatalogVersion(candidate, manager.getCatalogVersion() + 1);
        const auto result = manager.applyCatalog(candidate);
        INFO(result.message);
        REQUIRE(result.success);
        CHECK(result.changed);
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

TEST_CASE("Dune2R readiness recognises a complete cached target without any request", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    Dune2RAssetManager manager(working.string());
    REQUIRE_FALSE(manager.isReadOnly());
    REQUIRE(manager.getPacks().size() == 1);

    // The catalog URLs point at a revision that does not exist, so a complete local
    // target is the only way this can report ready: nothing is fetched.
    const auto ready = manager.checkReadiness();
    CHECK(ready.complete);
    CHECK(ready.missingPackIDs.empty());
    CHECK(ready.missingBytes == 0);
    CHECK(ready.totalBytes == 3);
    CHECK(ready.revision == manager.getRevision());
    CHECK_FALSE(ready.fingerprint.empty());
    CHECK(manager.installedFingerprint() == ready.fingerprint);

    // A verified target is reused instead of being installed again.
    const auto reinstalled = manager.install({"offline"});
    CHECK(reinstalled.success);
    CHECK_FALSE(reinstalled.changed);
    CHECK(manager.installedFingerprint() == ready.fingerprint);

    // Losing a file must invalidate both the readiness and the cached digest.
    std::filesystem::remove(working / "graphics_hd" / "units" / "offlineunit" / "unit.ini");
    const auto missing = manager.checkReadiness();
    CHECK_FALSE(missing.complete);
    CHECK(missing.missingPackIDs == std::vector<std::string>{"offline"});
    CHECK(missing.missingBytes == 3);
    CHECK(missing.fingerprint != ready.fingerprint);
}

TEST_CASE("Dune2R readiness reports between files and can be abandoned", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeDuplicatePack(working, "1791400001");
    Dune2RAssetManager manager(working.string());
    REQUIRE(manager.getPacks().size() == 1);
    const uint64_t target = manager.getPacks().front().totalBytes();

    // The caller that has to keep one thread responsive is reported to before every
    // file, with the total of the whole target known from the first report.
    std::vector<uint64_t> checkedAt;
    const auto readiness = manager.checkReadiness([&](uint64_t checked, uint64_t total) {
        CHECK(total == target);
        checkedAt.push_back(checked);
        return true;
    });
    // Before each of the two files and once for the finished scan, so a progress
    // display driven from here reaches the whole target.
    CHECK(checkedAt == std::vector<uint64_t>{0, target / 2, target});
    CHECK_FALSE(readiness.complete);
    CHECK(readiness.totalBytes == target);
    CHECK(readiness.missingPackIDs == std::vector<std::string>{"duplicate"});
    CHECK_FALSE(readiness.fingerprint.empty());

    // Refusing at the first file abandons the scan: no further file is read, and the
    // partial result can neither report a complete target nor be cached, because it
    // carries no fingerprint to compare a later target against.
    int reports = 0;
    const auto abandoned = manager.checkReadiness([&](uint64_t, uint64_t) {
        ++reports;
        return false;
    });
    CHECK(reports == 1);
    CHECK_FALSE(abandoned.complete);
    CHECK(abandoned.fingerprint.empty());
    CHECK(abandoned.missingPackIDs.empty());
    CHECK(abandoned.revision == manager.getRevision());
    CHECK(abandoned.totalBytes == target);

    // A complete target still verifies, and abandoning it is equally refused.
    const auto unit = working / "graphics_hd" / "units" / "duplicateunit";
    std::filesystem::copy_file(unit / "unit.ini", unit / "atlases" / "idle" / "east.png",
                               std::filesystem::copy_options::overwrite_existing);
    CHECK(manager.checkReadiness().complete);
    CHECK_FALSE(manager.checkReadiness([](uint64_t, uint64_t) { return false; }).complete);
}

TEST_CASE("Dune2R readiness treats changed published artwork as missing bytes", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    Dune2RAssetManager manager(working.string());
    REQUIRE(manager.checkReadiness().complete);
    const auto installedDigest = manager.installedFingerprint();

    // Same path, new published bytes: a newer catalog, so the update is accepted.
    const std::string replacement = "redrawn";
    const std::string revision(40, 'd');
    std::ostringstream updated;
    updated << "[Catalog]\nSchema=1\nRevision=" << revision << "\nVersion=1791400000\n"
            << "BaseURL=https://raw.githubusercontent.com/dunecity-project/dunecity/"
            << revision << "/mods/Dune2R/graphics_hd/units\nPackCount=1\n"
            << "[Pack.0]\nID=offline\nDisplayName=Offline fixture\nVariant=remastered\n"
            << "Unit=offlineunit\nFileCount=1\nFile.0=unit.ini|" << replacement.size() << '|'
            << Dune2RAssetManager::sha256Bytes(replacement) << '\n';
    const auto applied = manager.applyCatalog(updated.str());
    INFO(applied.message);
    REQUIRE(applied.success);
    CHECK(applied.changed);
    CHECK(manager.getCatalogVersion() == 1791400000u);

    const auto readiness = manager.checkReadiness();
    CHECK_FALSE(readiness.complete);
    CHECK(readiness.missingPackIDs == std::vector<std::string>{"offline"});
    CHECK(readiness.missingBytes == replacement.size());
    // The readiness digest follows the catalog, so a cached "ready" cannot survive
    // an artwork change even when every installed file is untouched.
    CHECK(readiness.fingerprint != installedDigest);
}

TEST_CASE("An older published Dune2R catalog cannot remove the shipped artwork", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto revision = manager.getRevision();
    const auto version = manager.getCatalogVersion();
    REQUIRE(version > 1700000000u);
    REQUIRE(manager.getPacks().size() == 6);
    const auto older = olderPublishedCatalog(fixture.contents, revision);

    const auto refused = manager.applyCatalog(older);
    CHECK_FALSE(refused.success);
    CHECK_FALSE(refused.changed);
    CHECK(refused.message.find("older than the installed") != std::string::npos);
    CHECK(manager.getRevision() == revision);
    CHECK(manager.getCatalogVersion() == version);
    CHECK(manager.getPacks().size() == 6);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "asset-catalog-online.ini"));

    // A profile that production already refreshed holds that older catalog on disk.
    // Loading must still pick the newer bundled target, including its Infantry pack.
    {
        std::ofstream installed(fixture.root / "asset-catalog-online.ini", std::ios::trunc);
        installed << older;
    }
    Dune2RAssetManager reloaded(fixture.root.string());
    CHECK(reloaded.getRevision() == revision);
    CHECK(reloaded.getCatalogVersion() == version);
    REQUIRE(reloaded.getPacks().size() == 6);
    const auto& packs = reloaded.getPacks();
    CHECK(std::any_of(packs.begin(), packs.end(),
                      [](const auto& pack) { return pack.id == "harkonneninfantry"; }));
    CHECK_FALSE(reloaded.checkReadiness().complete);
}

TEST_CASE("A changed expected size invalidates a cached Dune2R ready result", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    Dune2RAssetManager manager(working.string());
    const auto ready = manager.checkReadiness();
    REQUIRE(ready.complete);
    REQUIRE(ready.totalBytes == 3);

    // The installed bytes are untouched, but the catalog now expects a different size
    // for the same path and hash. A cached "ready" must not survive that.
    const std::string revision(40, 'b');
    std::ostringstream resized;
    resized << "[Catalog]\nSchema=1\nRevision=" << revision << "\nVersion=7\n"
            << "BaseURL=https://raw.githubusercontent.com/dunecity-project/dunecity/"
            << revision << "/mods/Dune2R/graphics_hd/units\nPackCount=1\n"
            << "[Pack.0]\nID=offline\nDisplayName=Offline fixture\nVariant=remastered\n"
            << "Unit=offlineunit\nFileCount=1\nFile.0=unit.ini|4|"
            << Dune2RAssetManager::sha256Bytes("abc") << '\n';
    const auto applied = manager.applyCatalog(resized.str());
    INFO(applied.message);
    REQUIRE(applied.success);
    CHECK(applied.changed);

    const auto stale = manager.checkReadiness();
    CHECK_FALSE(stale.complete);
    CHECK(stale.missingBytes == 4);
    CHECK(stale.fingerprint != ready.fingerprint);
    CHECK(manager.installedFingerprint() == stale.fingerprint);
}

TEST_CASE("A Dune2R catalog with a zero-byte asset entry is refused entirely", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    std::ifstream input(working / "asset-catalog.ini");
    const std::string valid((std::istreambuf_iterator<char>(input)), {});
    const auto zeroSized = replaceAll(valid, "unit.ini|3|", "unit.ini|0|");
    REQUIRE(zeroSized != valid);

    // An entry that claims nothing has to be fetched would otherwise verify as a
    // complete target while the artwork is absent.
    Dune2RAssetManager manager(working.string());
    const auto refused = manager.applyCatalog(withCatalogVersion(zeroSized, 9));
    INFO(refused.message);
    CHECK_FALSE(refused.success);
    CHECK(manager.checkReadiness().complete);
    CHECK_FALSE(std::filesystem::exists(working / "asset-catalog-online.ini"));

    // The same catalog as the only one present leaves no usable target at all, so no
    // readiness can be reported from it.
    const auto malformed = fixture.root / "malformed";
    std::filesystem::create_directories(malformed);
    {
        std::ofstream broken(malformed / "asset-catalog.ini", std::ios::trunc);
        broken << zeroSized;
    }
    CHECK_THROWS(Dune2RAssetManager(malformed.string()));
}

TEST_CASE("A reused Dune2R publication number cannot describe a different target", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto revision = manager.getRevision();
    const auto version = manager.getCatalogVersion();
    const auto identity = manager.getTargetIdentity();
    REQUIRE(version > 0);
    REQUIRE_FALSE(identity.empty());

    // Same number, but a different asset revision, base URL, expected size or expected
    // hash. A counter identifies one target; a reused one must never replace it.
    const auto conflictingRevision = replaceAll(fixture.contents, revision, std::string(40, 'b'));
    auto conflictingSize = fixture.contents;
    const auto sizeStart = conflictingSize.find('|', conflictingSize.find("File.0=")) + 1;
    conflictingSize.replace(sizeStart, conflictingSize.find('|', sizeStart) - sizeStart, "4242");
    auto conflictingHash = fixture.contents;
    const auto hashStart = conflictingHash.find('|', sizeStart) + 1;
    conflictingHash.replace(hashStart, 64, std::string(64, 'd'));
    auto conflictingMetadata = replaceAll(fixture.contents, "DisplayName=Harkonnen Infantry Remastered",
                                          "DisplayName=Harkonnen Infantry");
    for(const auto& conflict : {conflictingRevision, conflictingSize, conflictingHash,
                                conflictingMetadata}) {
        const auto refused = manager.applyCatalog(conflict);
        INFO(refused.message);
        CHECK_FALSE(refused.success);
        CHECK_FALSE(refused.changed);
        CHECK(refused.message.find("already describes a different artwork target")
              != std::string::npos);
        CHECK(manager.getRevision() == revision);
        CHECK(manager.getTargetIdentity() == identity);
        CHECK_FALSE(std::filesystem::exists(fixture.root / "asset-catalog-online.ini"));
    }

    // The very same target may be refreshed; nothing changed, so nothing is reinstalled.
    const auto unchanged = manager.applyCatalog(fixture.contents);
    INFO(unchanged.message);
    CHECK(unchanged.success);
    CHECK_FALSE(unchanged.changed);
    CHECK(manager.getTargetIdentity() == identity);

    // A changed target under the next number is an update, and metadata alone counts.
    const auto renamed = withCatalogVersion(conflictingMetadata, version + 1);
    const auto applied = manager.applyCatalog(renamed);
    INFO(applied.message);
    REQUIRE(applied.success);
    CHECK(applied.changed);
    CHECK(manager.getRevision() == revision);
    CHECK(manager.getTargetIdentity() != identity);

    // A conflicting online file on disk is ignored in favour of the bundled catalog.
    {
        std::ofstream installed(fixture.root / "asset-catalog-online.ini", std::ios::trunc);
        installed << withCatalogVersion(conflictingRevision, version);
    }
    Dune2RAssetManager reloaded(fixture.root.string());
    CHECK(reloaded.getRevision() == revision);
    CHECK(reloaded.getCatalogVersion() == version);
    CHECK(reloaded.getTargetIdentity() == identity);
}

TEST_CASE("A newer Dune2R publication cannot drop a bundled asset pack", "[Dune2RAssets]") {
    CatalogFixture fixture;
    Dune2RAssetManager manager(fixture.root.string());
    const auto revision = manager.getRevision();
    const auto version = manager.getCatalogVersion();
    REQUIRE(manager.getPacks().size() == 6);
    REQUIRE(manager.getRequiredPackIDs().size() == 6);

    // Another branch may have published a higher counter, but a build that ships the
    // Infantry pack must never be talked out of it.
    const auto newerWithoutInfantry =
        withCatalogVersion(withoutInfantryPack(fixture.contents, revision), version + 1000);
    const auto refused = manager.applyCatalog(newerWithoutInfantry);
    INFO(refused.message);
    CHECK_FALSE(refused.success);
    CHECK(refused.message.find("missing the bundled asset pack harkonneninfantry")
          != std::string::npos);
    CHECK(manager.getPacks().size() == 6);
    CHECK(manager.getCatalogVersion() == version);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "asset-catalog-online.ini"));

    // The same catalog already written into the profile is ignored on load.
    {
        std::ofstream installed(fixture.root / "asset-catalog-online.ini", std::ios::trunc);
        installed << newerWithoutInfantry;
    }
    Dune2RAssetManager reloaded(fixture.root.string());
    CHECK(reloaded.getRevision() == revision);
    CHECK(reloaded.getCatalogVersion() == version);
    REQUIRE(reloaded.getPacks().size() == 6);
    const auto& packs = reloaded.getPacks();
    CHECK(std::any_of(packs.begin(), packs.end(),
                      [](const auto& pack) { return pack.id == "harkonneninfantry"; }));

    // Adding a pack is allowed: the floor is a minimum, not an exact set.
    auto extended = withCatalogVersion(fixture.contents, version + 1);
    extended = replaceAll(extended, "PackCount=6", "PackCount=7");
    extended += "\n[Pack.6]\nID=extra\nDisplayName=Added Pack\nVariant=remastered\n"
                "Unit=extraunit\nFileCount=1\nFile.0=unit.ini|7|"
                + Dune2RAssetManager::sha256Bytes("[Unit]\n") + "\n";
    const auto accepted = reloaded.applyCatalog(extended);
    INFO(accepted.message);
    REQUIRE(accepted.success);
    CHECK(accepted.changed);
    CHECK(reloaded.getPacks().size() == 7);
}

TEST_CASE("Legacy Dune2R catalogs keep their refreshed-online precedence", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeOfflinePack(working);
    const auto bundled = Dune2RAssetManager(working.string()).getRevision();
    CHECK(Dune2RAssetManager(working.string()).getCatalogVersion() == 0);

    // Neither file carries a version, which is what an installation refreshed before
    // this field looks like. The refreshed online catalog still wins.
    std::ifstream input(working / "asset-catalog.ini");
    const std::string legacy((std::istreambuf_iterator<char>(input)), {});
    const std::string refreshedRevision(40, 'e');
    {
        std::ofstream online(working / "asset-catalog-online.ini", std::ios::trunc);
        online << replaceAll(legacy, bundled, refreshedRevision);
    }
    Dune2RAssetManager manager(working.string());
    CHECK(manager.getRevision() == refreshedRevision);
    CHECK(manager.getCatalogVersion() == 0);
}

TEST_CASE("A cancelled Dune2R completion keeps the installed artwork and blocks readiness", "[Dune2RAssets]") {
    CatalogFixture fixture;
    const auto working = fixture.root / "Dune2R";
    writeDuplicatePack(working, "1791310979");
    const auto unit = working / "graphics_hd" / "units" / "duplicateunit";
    const auto before = directoryFingerprint(unit);

    Dune2RAssetManager manager(working.string());
    REQUIRE_FALSE(manager.isReadOnly());
    const auto incomplete = manager.checkReadiness();
    REQUIRE_FALSE(incomplete.complete);
    REQUIRE(incomplete.missingPackIDs == std::vector<std::string>{"duplicate"});
    CHECK(incomplete.missingBytes == incomplete.totalBytes / 2);

    int progressCalls = 0;
    const auto cancelled = manager.install({"duplicate"}, [&](const Dune2RAssetProgress& progress) {
        ++progressCalls;
        CHECK(progress.totalBytes == incomplete.totalBytes);
        return false;
    });
    CHECK(progressCalls == 1);
    CHECK_FALSE(cancelled.success);
    CHECK_FALSE(cancelled.changed);
    CHECK(cancelled.message.find("Download cancelled") != std::string::npos);
    // The previously verified file is still exactly where it was, and the game may
    // not start: readiness is still incomplete.
    CHECK(directoryFingerprint(unit) == before);
    CHECK_FALSE(manager.checkReadiness().complete);

    // Retrying completes the pack from the verified local duplicate, so a cancel
    // costs no progress and the final target is the installed, verified one.
    const auto completed = manager.install({"duplicate"});
    INFO(completed.message);
    REQUIRE(completed.success);
    CHECK(completed.changed);
    CHECK(manager.checkReadiness().complete);
}

TEST_CASE("Dune2R readiness inspects an immutable snapshot without changing it", "[Dune2RAssets][workshop]") {
    CatalogFixture fixture;
    const auto snapshot = fixture.root / ("ws-" + std::string(64, 'f'));
    writeDuplicatePack(snapshot, "1791310979");
    const auto before = directoryFingerprint(snapshot);

    Dune2RAssetManager manager(snapshot.string());
    REQUIRE(manager.isReadOnly());
    const auto readiness = manager.checkReadiness();
    CHECK_FALSE(readiness.complete);
    CHECK(readiness.missingPackIDs == std::vector<std::string>{"duplicate"});
    // Pinned content: completing it here is refused before any file is touched.
    const auto refused = manager.install({"duplicate"});
    CHECK_FALSE(refused.success);
    CHECK(refused.message.find("immutable") != std::string::npos);
    CHECK(directoryFingerprint(snapshot) == before);
    CHECK(manager.installedFingerprint() == readiness.fingerprint);
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
