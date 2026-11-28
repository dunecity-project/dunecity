/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VOCPLACEHOLDER_H
#define VOCPLACEHOLDER_H

#include <FileClasses/Pakfile.h>
#include <misc/FileSystem.h>
#include <misc/SDL2pp.h>
#include <misc/string_util.h>

#include <string>

// The Tornie voice set ships 30-byte stand-in VOC files (for example
// FWORMY.VOC loose in the data directory and inside Tornie.PAK). They are not
// playable sounds. FileManager::openFile skips them and keeps looking, so every
// "does this asset exist?" check has to skip them by the same rule; otherwise
// exists() says yes, openFile() finds nothing usable and throws "Cannot find"
// (issue #129: French voices crashed at launch on FWORMY.VOC).
namespace VocPlaceholder {

inline constexpr Sint64 kMaxPlaceholderBytes = 32;

inline bool isVocFilename(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    return upperFilename.size() >= 4 && upperFilename.compare(upperFilename.size() - 4, 4, ".VOC") == 0;
}

inline bool isTinyVocPlaceholder(const std::string& filename, SDL_RWops* rwop) {
    if(rwop == nullptr || !isVocFilename(filename)) {
        return false;
    }

    const auto size = SDL_RWsize(rwop);
    return size >= 0 && size <= kMaxPlaceholderBytes;
}

/// True when filepath (matched case-insensitively) exists and is not a tiny VOC placeholder.
inline bool hasUsableFile(const std::string& filename, std::string filepath) {
    if(!getCaseInsensitiveFilename(filepath)) {
        return false;
    }
    if(!isVocFilename(filename)) {
        return true;
    }

    const sdl2::RWops_ptr rwop{SDL_RWFromFile(filepath.c_str(), "rb")};
    return rwop && !isTinyVocPlaceholder(filename, rwop.get());
}

/// True when the PAK holds filename and the entry is not a tiny VOC placeholder.
inline bool hasUsablePakEntry(Pakfile& pakFile, const std::string& filename) {
    if(!pakFile.exists(filename)) {
        return false;
    }
    if(!isVocFilename(filename)) {
        return true;
    }

    const auto rwop = pakFile.openFile(filename);
    return rwop && !isTinyVocPlaceholder(filename, rwop.get());
}

} // namespace VocPlaceholder

#endif // VOCPLACEHOLDER_H
