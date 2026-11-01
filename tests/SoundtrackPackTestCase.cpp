#include <catch2/catch_test_macros.hpp>
#include <FileClasses/music/SoundtrackPack.h>

#include <SDL2/SDL.h>

#include <filesystem>
#include <fstream>

namespace {
struct SoundtrackFixture {
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("soundtrack-test-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::path directory = root / "pack";

    SoundtrackFixture() { std::filesystem::create_directories(directory); }
    ~SoundtrackFixture() { std::error_code error; std::filesystem::remove_all(root, error); }

    void file(const std::string& name) { std::ofstream(directory / name) << "manifest fixture"; }
    void manifest(const std::string& slots, const std::string& metadata =
                  "[Soundtrack]\nFormat = 1\nName = Reimagined\nAuthor = Test Musician\nYouTube = https://www.youtube.com/@test-musician\n") {
        std::ofstream(directory / "manifest.ini") << metadata << slots;
    }
};
}

TEST_CASE("A local soundtrack parses credits and maps tracks without filling missing slots", "[music][soundtrack]") {
    SoundtrackFixture fixture;
    fixture.file("peace one.mp3");
    fixture.file("peace two.OGG");
    fixture.file("attack.flac");
    fixture.file("intro.wav");
    fixture.file("credits.mp3");
    fixture.manifest("[peace]\n1 = peace one.mp3\n2 = peace two.OGG\n"
                     "[attack]\n1 = attack.flac\n[intro]\n1 = intro.wav\n[credits]\n1 = credits.mp3\n");
    const auto pack = SoundtrackPack::load(fixture.directory);
    REQUIRE(pack);
    REQUIRE(pack->name == "Reimagined");
    REQUIRE(pack->author == "Test Musician");
    REQUIRE(pack->youtube == "https://www.youtube.com/@test-musician");
    REQUIRE(pack->creditText().find(pack->author) != std::string::npos);
    REQUIRE(pack->creditText().find(pack->youtube) != std::string::npos);
    REQUIRE(pack->trackCount(MUSIC_PEACE) == 2);
    REQUIRE(std::filesystem::u8path(*pack->trackFor(MUSIC_PEACE, 0)).filename() == "peace one.mp3");
    REQUIRE(std::filesystem::u8path(*pack->trackFor(MUSIC_PEACE, 3)).filename() == "peace two.OGG");
    REQUIRE(pack->trackFor(MUSIC_INTRO, 0));
    REQUIRE(pack->trackFor(MUSIC_CREDITS, 0));
    REQUIRE(pack->trackFor(MUSIC_MENU, 0) == nullptr);
    REQUIRE(pack->trackFor(MUSIC_MAPCHOICE, 0) == nullptr);
    REQUIRE(pack->trackFor(MUSIC_BRIEFING_H, 0) == nullptr);
    REQUIRE(pack->trackCount(MUSIC_RANDOM) == 3);
    REQUIRE(pack->trackFor(MUSIC_RANDOM, 0) == pack->trackFor(MUSIC_ATTACK, 0));
    REQUIRE(pack->trackFor(MUSIC_RANDOM, 1) == pack->trackFor(MUSIC_PEACE, 0));
    REQUIRE(pack->trackFor(MUSIC_RANDOM, 2) == pack->trackFor(MUSIC_PEACE, 1));
}

TEST_CASE("A soundtrack skips absent unsupported and escaping paths instead of hiding the original music", "[music][soundtrack]") {
    SoundtrackFixture fixture;
    fixture.file("valid.mp3");
    fixture.file("unsupported.txt");
    std::ofstream(fixture.root / "outside.mp3") << "outside pack";
    fixture.manifest("[peace]\n1 = valid.mp3\n[attack]\n1 = missing.mp3\n2 = unsupported.txt\n"
                     "3 = ../outside.mp3\n4 = /outside.mp3\n5 = C:\\outside.mp3\n");
    const auto pack = SoundtrackPack::load(fixture.directory);
    REQUIRE(pack);
    REQUIRE(pack->trackCount(MUSIC_PEACE) == 1);
    REQUIRE(pack->trackFor(MUSIC_ATTACK, 0) == nullptr);
    REQUIRE(pack->trackCount(MUSIC_RANDOM) == 1);
    REQUIRE(pack->trackFor(MUSIC_RANDOM, 0) == pack->trackFor(MUSIC_PEACE, 0));
}

TEST_CASE("Soundtrack symlinks cannot load audio outside their pack", "[music][soundtrack]") {
    SoundtrackFixture fixture;
    fixture.file("valid.ogg");
    std::ofstream(fixture.root / "outside.mp3") << "outside pack";
    std::error_code error;
    std::filesystem::create_symlink(fixture.root / "outside.mp3", fixture.directory / "link.mp3", error);
    if(error) SKIP("This host does not allow file symlinks");
    fixture.manifest("[peace]\n1 = valid.ogg\n[attack]\n1 = link.mp3\n");
    const auto pack = SoundtrackPack::load(fixture.directory);
    REQUIRE(pack);
    REQUIRE(pack->trackFor(MUSIC_ATTACK, 0) == nullptr);
}

TEST_CASE("Absent empty and unsupported-version packs leave the current soundtrack available", "[music][soundtrack]") {
    SoundtrackFixture fixture;
    REQUIRE_FALSE(SoundtrackPack::load(fixture.directory));
    fixture.manifest("[peace]\n1 = absent.mp3\n");
    REQUIRE_FALSE(SoundtrackPack::load(fixture.directory));
    fixture.file("peace.mp3");
    fixture.manifest("[peace]\n1 = peace.mp3\n", "[Soundtrack]\nFormat = 2\nName = Future pack\n");
    REQUIRE_FALSE(SoundtrackPack::load(fixture.directory));
    fixture.manifest("[peace]\n1 = peace.mp3\n", "[Soundtrack]\nFormat = 1\n");
    REQUIRE_FALSE(SoundtrackPack::load(fixture.directory));
}

TEST_CASE("Missing credit metadata is explicit and never invents an author or channel", "[music][soundtrack]") {
    SoundtrackFixture fixture;
    fixture.file("peace.mp3");
    fixture.manifest("[peace]\n1 = peace.mp3\n", "[Soundtrack]\nFormat = 1\nName = Local pack\n");
    const auto pack = SoundtrackPack::load(fixture.directory);
    REQUIRE(pack);
    REQUIRE(pack->author.empty());
    REQUIRE(pack->youtube.empty());
    REQUIRE(pack->creditText().find("Author not supplied") != std::string::npos);
    REQUIRE(pack->creditText().find("YouTube link not supplied") != std::string::npos);
    REQUIRE(std::string(SoundtrackPack::slotName(MUSIC_CREDITS)) == "credits");
}
