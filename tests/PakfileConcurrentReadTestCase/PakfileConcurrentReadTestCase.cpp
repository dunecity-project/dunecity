#include <catch2/catch_test_macros.hpp>
#include <FileClasses/Pakfile.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <vector>

TEST_CASE("Parallel graphics and audio reads preserve PAK entry bytes", "[pak][threading]") {
    struct TemporaryPak {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("dunecity-pak-read-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pak");
        ~TemporaryPak() { std::error_code error; std::filesystem::remove(path, error); }
    } temporary;

    std::array<std::vector<unsigned char>, 2> contents;
    for(size_t entry = 0; entry < contents.size(); ++entry) {
        contents[entry].resize(65536);
        for(size_t i = 0; i < contents[entry].size(); ++i) {
            contents[entry][i] = static_cast<unsigned char>((i * 17 + entry * 113) % 251);
        }
    }
    {
        Pakfile writer(temporary.path.string(), true);
        for(size_t entry = 0; entry < contents.size(); ++entry) {
            auto source = sdl2::RWops_ptr{SDL_RWFromConstMem(contents[entry].data(), static_cast<int>(contents[entry].size()))};
            REQUIRE(source);
            writer.addFile(source.get(), entry == 0 ? "MENTAT.FRE" : "VOICE.VOC");
        }
    }

    Pakfile archive(temporary.path.string());
    std::array<sdl2::RWops_ptr, 4> streams;
    for(size_t worker = 0; worker < streams.size(); ++worker) {
        streams[worker] = archive.openFile(worker % contents.size() == 0 ? "MENTAT.FRE" : "VOICE.VOC");
        REQUIRE(streams[worker]);
    }
    std::array<std::future<bool>, 4> readers;
    // Destroy the promise before the futures if launching a worker throws, so
    // already launched workers cannot remain blocked while their futures join.
    std::promise<void> start;
    const auto ready = start.get_future().share();
    for(size_t worker = 0; worker < readers.size(); ++worker) {
        readers[worker] = std::async(std::launch::async, [&, worker, ready]() {
            const size_t entry = worker % contents.size();
            SDL_RWops* stream = streams[worker].get();
            ready.wait();
            std::array<unsigned char, 127> buffer{};
            for(size_t iteration = 0; iteration < 4000; ++iteration) {
                const size_t offset = (iteration * 131 + worker * 37) % (contents[entry].size() - buffer.size());
                if(SDL_RWseek(stream, static_cast<Sint64>(offset), RW_SEEK_SET) != static_cast<Sint64>(offset) ||
                   SDL_RWread(stream, buffer.data(), 1, buffer.size()) != buffer.size()) {
                    return false;
                }
                for(size_t i = 0; i < buffer.size(); ++i) {
                    if(buffer[i] != contents[entry][offset + i]) { return false; }
                }
            }
            return true;
        });
    }
    start.set_value();
    // Assertions stay on the test thread; all workers finish before the archive closes.
    std::array<bool, 4> results{};
    for(size_t worker = 0; worker < readers.size(); ++worker) { results[worker] = readers[worker].get(); }
    for(const bool correct : results) { REQUIRE(correct); }
}
