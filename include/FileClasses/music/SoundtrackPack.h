#ifndef SOUNDTRACKPACK_H
#define SOUNDTRACKPACK_H

#include <FileClasses/music/MusicPlayer.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Local-only music: never part of a mod/workshop payload or multiplayer content identity.
class SoundtrackPack {
public:
    static std::optional<SoundtrackPack> load(const std::filesystem::path& directory);
    static std::optional<SoundtrackPack> loadInstalled();
    static const char* slotName(MUSICTYPE slot);

    std::size_t trackCount(MUSICTYPE slot) const;
    // nullptr means the caller must use the current soundtrack for this slot.
    const std::string* trackFor(MUSICTYPE slot, std::size_t selection) const;
    std::string creditText() const;

    std::string name;
    std::string author;
    std::string youtube;

private:
    std::array<std::vector<std::string>, MUSIC_NUM_MUSIC_TYPES> tracks;
};

#endif // SOUNDTRACKPACK_H
