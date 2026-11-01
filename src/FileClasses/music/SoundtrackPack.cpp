#include <FileClasses/music/SoundtrackPack.h>

#include <FileClasses/INIFile.h>
#include <misc/fnkdat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>

namespace {
constexpr std::array<const char*, MUSIC_NUM_MUSIC_TYPES> slotNames{
    "attack", "peace", "intro", "menu", "briefingH", "briefingA", "briefingO",
    "winH", "winA", "winO", "loseH", "loseA", "loseO", "gamestats", "mapchoice",
    "meanwhile", "finaleH", "finaleA", "finaleO", "credits"
};

bool supportedFile(const std::filesystem::path& file) {
    auto extension = file.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".ogg" || extension == ".mp3" || extension == ".wav"
        || extension == ".flac" || extension == ".mid";
}
}

std::optional<SoundtrackPack> SoundtrackPack::load(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::error_code error;
    const auto root = fs::canonical(directory, error);
    if(error || !fs::is_regular_file(root / "manifest.ini", error)) return std::nullopt;

    try {
        INIFile manifest((root / "manifest.ini").u8string());
        if(manifest.getIntValue("Soundtrack", "Format", 0) != 1) return std::nullopt;
        SoundtrackPack pack;
        pack.name = manifest.getStringValue("Soundtrack", "Name");
        pack.author = manifest.getStringValue("Soundtrack", "Author");
        pack.youtube = manifest.getStringValue("Soundtrack", "YouTube");
        if(pack.name.empty()) return std::nullopt;

        bool hasTracks = false;
        for(std::size_t slot = 0; slot < slotNames.size(); ++slot) {
            if(!manifest.hasSection(slotNames[slot])) continue;
            for(const auto& key : manifest.getSection(slotNames[slot])) {
                const auto filename = key.getStringValue();
                const auto relative = fs::u8path(filename);
                // Reject Windows absolute paths even when parsing a pack on Unix, and vice versa.
                if(filename.empty() || filename.find_first_of("\\:") != std::string::npos
                    || relative.is_absolute()
                    || std::any_of(relative.begin(), relative.end(), [](const fs::path& part) {
                        return part == ".." || part == ".";
                    })) continue;
                const auto file = fs::canonical(root / relative, error);
                if(error || !fs::is_regular_file(file, error) || !supportedFile(file)) continue;
                // Symlinks must not turn a local pack into a reference outside its directory.
                if(std::mismatch(root.begin(), root.end(), file.begin(), file.end()).first != root.end()) continue;
                pack.tracks[slot].push_back(file.u8string());
                hasTracks = true;
            }
        }
        if(hasTracks) return pack;
    } catch(const std::exception& exception) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot load alternative soundtrack: %s", exception.what());
    }
    return std::nullopt;
}

std::optional<SoundtrackPack> SoundtrackPack::loadInstalled() {
    char path[FILENAME_MAX];
    if(fnkdat("music/dune2-remake", path, sizeof(path), FNKDAT_USER) != 0) return std::nullopt;
    return load(std::filesystem::u8path(path));
}

const char* SoundtrackPack::slotName(MUSICTYPE slot) {
    return slot >= 0 && slot < MUSIC_NUM_MUSIC_TYPES ? slotNames[slot] : "random";
}

std::size_t SoundtrackPack::trackCount(MUSICTYPE slot) const {
    if(slot == MUSIC_RANDOM) return tracks[MUSIC_ATTACK].size() + tracks[MUSIC_PEACE].size();
    return slot >= 0 && slot < MUSIC_NUM_MUSIC_TYPES ? tracks[slot].size() : 0;
}

const std::string* SoundtrackPack::trackFor(MUSICTYPE slot, std::size_t selection) const {
    const auto count = trackCount(slot);
    if(count == 0) return nullptr;
    selection %= count;
    if(slot == MUSIC_RANDOM) {
        if(selection < tracks[MUSIC_ATTACK].size()) return &tracks[MUSIC_ATTACK][selection];
        return &tracks[MUSIC_PEACE][selection - tracks[MUSIC_ATTACK].size()];
    }
    return &tracks[slot][selection];
}

std::string SoundtrackPack::creditText() const {
    return name + "\n" + (author.empty() ? "Author not supplied" : author)
        + "\n" + (youtube.empty() ? "YouTube link not supplied" : youtube);
}
