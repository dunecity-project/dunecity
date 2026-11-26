#include <catch2/catch_test_macros.hpp>
#include <FileClasses/VocPlaceholder.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Issue #129: with the language set to French the game died at launch with
// "Cannot find 'FWORMY.VOC'!". The data directory ships a 30-byte FWORMY.VOC
// stand-in (and Tornie.PAK another), FRENCH.PAK has no FWORMY.VOC at all.
// FileManager::exists() counted the stand-in, FileManager::openFile() skipped
// it, found nothing else and threw. exists() now asks these helpers, which
// apply the same placeholder rule as openFile().

namespace {

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("dunecity-voc-placeholder-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

std::vector<char> bytes(size_t size) { return std::vector<char>(size, 'x'); }

void writeFile(const std::filesystem::path& path, size_t size) {
    std::ofstream out(path, std::ios::binary);
    const auto content = bytes(size);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void writePak(const std::filesystem::path& path, const std::vector<std::pair<std::string, size_t>>& entries) {
    Pakfile writer(path.string(), true);
    for(const auto& [name, size] : entries) {
        auto content = bytes(size);
        auto source = sdl2::RWops_ptr{SDL_RWFromConstMem(content.data(), static_cast<int>(content.size()))};
        REQUIRE(source);
        writer.addFile(source.get(), name);
    }
}

} // namespace

TEST_CASE("Tiny VOC stand-ins never count as an existing voice file", "[filemanager][voc]") {
    TemporaryDirectory dir;
    const auto root = dir.path.string();

    SECTION("loose files in a data directory") {
        writeFile(dir.path / "FWORMY.VOC", 30);    // the shipped stand-in
        writeFile(dir.path / "FWARNING.VOC", 33);  // one byte over the limit is a real sound
        writeFile(dir.path / "NOTES.TXT", 4);      // the rule is only for VOC files

        CHECK_FALSE(VocPlaceholder::hasUsableFile("FWORMY.VOC", root + "/FWORMY.VOC"));
        CHECK_FALSE(VocPlaceholder::hasUsableFile("fwormy.voc", root + "/fwormy.voc"));
        CHECK(VocPlaceholder::hasUsableFile("FWARNING.VOC", root + "/FWARNING.VOC"));
        CHECK(VocPlaceholder::hasUsableFile("NOTES.TXT", root + "/NOTES.TXT"));
        CHECK_FALSE(VocPlaceholder::hasUsableFile("GWORMY.VOC", root + "/GWORMY.VOC"));
    }

    SECTION("PAK entries") {
        const auto frenchPath = dir.path / "FRENCH.PAK";
        const auto torniePath = dir.path / "Tornie.PAK";
        const auto germanPath = dir.path / "GERMAN.PAK";
        writePak(frenchPath, {{"FWARNING.VOC", 4096}});
        writePak(torniePath, {{"FWARNING.VOC", 30}, {"FWORMY.VOC", 30}});
        writePak(germanPath, {{"GWARNING.VOC", 4096}, {"GWORMY.VOC", 4096}});
        Pakfile french(frenchPath.string());
        Pakfile tornie(torniePath.string());
        Pakfile german(germanPath.string());

        // French: "Warning" exists, "Wormsign" exists nowhere usable.
        CHECK(VocPlaceholder::hasUsablePakEntry(french, "FWARNING.VOC"));
        CHECK_FALSE(VocPlaceholder::hasUsablePakEntry(french, "FWORMY.VOC"));
        CHECK_FALSE(VocPlaceholder::hasUsablePakEntry(tornie, "FWORMY.VOC"));
        CHECK_FALSE(VocPlaceholder::hasUsablePakEntry(tornie, "FWARNING.VOC"));

        // German has both halves.
        CHECK(VocPlaceholder::hasUsablePakEntry(german, "GWARNING.VOC"));
        CHECK(VocPlaceholder::hasUsablePakEntry(german, "GWORMY.VOC"));
    }

    SECTION("an opened stream is judged by size and extension") {
        auto tiny = bytes(VocPlaceholder::kMaxPlaceholderBytes);
        auto real = bytes(VocPlaceholder::kMaxPlaceholderBytes + 1);
        auto tinyStream = sdl2::RWops_ptr{SDL_RWFromConstMem(tiny.data(), static_cast<int>(tiny.size()))};
        auto realStream = sdl2::RWops_ptr{SDL_RWFromConstMem(real.data(), static_cast<int>(real.size()))};

        CHECK(VocPlaceholder::isTinyVocPlaceholder("FWORMY.VOC", tinyStream.get()));
        CHECK_FALSE(VocPlaceholder::isTinyVocPlaceholder("FWORMY.VOC", realStream.get()));
        CHECK_FALSE(VocPlaceholder::isTinyVocPlaceholder("FWORMY.WAV", tinyStream.get()));
        CHECK_FALSE(VocPlaceholder::isTinyVocPlaceholder("FWORMY.VOC", nullptr));
    }
}
