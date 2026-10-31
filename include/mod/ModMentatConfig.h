#ifndef MODMENTATCONFIG_H
#define MODMENTATCONFIG_H

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include <utility>

struct ModMentatRect { int x, y, w, h; };
struct ModMentatPatch {
    ModMentatRect destination;
    std::vector<ModMentatRect> sources;
};
struct ModMentatPoint { int x, y; };

struct ModMentatInfo {
    bool enabled = false;
    int identityHouse = -1;
    std::string backgroundAsset;
    std::string foregroundAsset;
    std::string eyesAsset;
    std::string mouthAsset;
    int eyesFrames = 5;
    double eyesFrameRate = 0.5;
    bool doubleEyes = true;
    int eyesTransparentColor = -1;
    int eyesX = -1;
    int eyesY = -1;
    int mouthFrames = 5;
    double mouthFrameRate = 5.0;
    bool doubleMouth = true;
    int mouthTransparentColor = -1;
    int mouthX = -1;
    int mouthY = -1;
    bool useBaseExtras = false;
    int eyesCropY = 0, eyesCropHeight = 0, eyesWidth = 0, eyesHeight = 0;
    int mouthCropY = 0, mouthCropHeight = 0, mouthWidth = 0, mouthHeight = 0;
    bool restFromBackground = false;
    bool factionTint = false;
    int eyesBrightness = 255, mouthBrightness = 255;
    std::vector<ModMentatPatch> eyesPatches, mouthPatches;
    std::vector<ModMentatPoint> foregroundPolygon;
};

namespace ModMentatConfig {

inline std::string lowercaseAscii(std::string value) {
    for(char& character : value) {
        if(character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

inline bool parseBoolean(const std::string& text, bool& destination) {
    const std::string normalized = lowercaseAscii(text);
    if(normalized == "true" || normalized == "yes" || normalized == "1") {
        destination = true;
        return true;
    }
    if(normalized == "false" || normalized == "no" || normalized == "0") {
        destination = false;
        return true;
    }
    return false;
}

inline bool parseDouble(const std::string& text, double& destination) noexcept {
    // strtod accepts nan/inf without ERANGE. Reject their spellings before
    // conversion because Release fast-math can optimize isfinite away.
    if(text.empty() || text.find_first_of("nNiI") != std::string::npos) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsedValue = std::strtod(text.c_str(), &end);
    if(errno == ERANGE || end != text.c_str() + text.size() || !std::isfinite(parsedValue)) {
        return false;
    }

    destination = parsedValue;
    return true;
}

inline bool isSafeAssetPath(const std::string& path) {
    if(path.empty()) {
        return true;
    }
    if(path.front() == '/' || path.front() == '\\' || path.find('\\') != std::string::npos
       || path.find(':') != std::string::npos) {
        return false;
    }

    std::size_t segmentStart = 0;
    while(segmentStart <= path.size()) {
        const std::size_t separator = path.find('/', segmentStart);
        const std::size_t segmentEnd = separator == std::string::npos ? path.size() : separator;
        const std::string segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if(segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if(separator == std::string::npos) {
            break;
        }
        segmentStart = separator + 1;
    }
    return true;
}

inline bool parseNumbers(const std::string& text, std::vector<int>& numbers) {
    std::istringstream input(text);
    int value;
    while(input >> value) {
        if(value < 0 || value > 8192) return false;
        numbers.push_back(value);
        input >> std::ws;
        if(input.eof()) return true;
        char separator;
        if(!(input >> separator) || separator != ',') return false;
        input >> std::ws;
        if(input.eof()) return false;
    }
    return false;
}

inline bool parsePatch(const std::string& text, std::vector<ModMentatPatch>& patches) {
    if(text.empty() || text.back() == ';') return false;
    ModMentatPatch patch{};
    std::istringstream input(text);
    std::string rectangle;
    bool first = true;
    while(std::getline(input, rectangle, ';')) {
        std::vector<int> values;
        if(!parseNumbers(rectangle, values) || values.size() != 4
           || values[2] == 0 || values[3] == 0) return false;
        const ModMentatRect rect{values[0], values[1], values[2], values[3]};
        if(first) patch.destination = rect;
        else patch.sources.push_back(rect);
        first = false;
    }
    if(patch.sources.empty() || patch.sources.size() > 64 || patches.size() >= 8) return false;
    patches.push_back(std::move(patch));
    return true;
}

inline bool parsePolygon(const std::string& text, std::vector<ModMentatPoint>& polygon) {
    std::vector<int> values;
    if(!parseNumbers(text, values) || values.size() < 6 || values.size() > 128
       || values.size() % 2) return false;
    std::vector<ModMentatPoint> points;
    for(std::size_t i = 0; i < values.size(); i += 2) points.push_back({values[i], values[i + 1]});
    polygon = std::move(points);
    return true;
}

inline bool isValid(const ModMentatInfo& info) {
    const auto validCoordinate = [](int value) { return value == -1 || (value >= 0 && value <= 4096); };
    const auto validColorKey = [](int value) { return value >= -1 && value <= 255; };
    const auto validLayout = [](int y, int cropHeight, int width, int height) {
        return y >= 0 && y <= 8192 && cropHeight >= 0 && cropHeight <= 8192
            && width >= 0 && width <= 4096 && height >= 0 && height <= 4096
            && ((cropHeight == 0 && width == 0 && height == 0)
                || (cropHeight > 0 && width > 0 && height > 0));
    };
    const auto validPatches = [](const std::vector<ModMentatPatch>& patches, int frames, int width, int height) {
        if(patches.size() > 8) return false;
        for(const auto& patch : patches) {
            const auto& d = patch.destination;
            if(d.x < 0 || d.y < 0 || d.w <= 0 || d.h <= 0 || d.x + d.w > width || d.y + d.h > height
               || patch.sources.size() != static_cast<std::size_t>(frames)) return false;
            for(const auto& s : patch.sources)
                if(s.x < 0 || s.y < 0 || s.w <= 0 || s.h <= 0 || s.x + s.w > 8192 || s.y + s.h > 8192)
                    return false;
        }
        return true;
    };
    if(!info.foregroundPolygon.empty()) {
        if(info.foregroundPolygon.size() < 3 || info.foregroundPolygon.size() > 64
           || info.backgroundAsset.empty()) return false;
        for(const auto& point : info.foregroundPolygon)
            if(point.x < 0 || point.y < 0 || point.x > 4096 || point.y > 4096) return false;
    }

    return info.identityHouse >= -1 && info.identityHouse < 8
        && isSafeAssetPath(info.backgroundAsset)
        && isSafeAssetPath(info.foregroundAsset)
        && isSafeAssetPath(info.eyesAsset)
        && isSafeAssetPath(info.mouthAsset)
        && info.eyesFrames >= 1 && info.eyesFrames <= 64
        && info.mouthFrames >= 1 && info.mouthFrames <= 64
        && info.eyesFrameRate > 0.0 && info.eyesFrameRate <= 100.0
        && info.mouthFrameRate > 0.0 && info.mouthFrameRate <= 100.0
        && validColorKey(info.eyesTransparentColor)
        && validColorKey(info.mouthTransparentColor)
        && validCoordinate(info.eyesX)
        && validCoordinate(info.eyesY)
        && validCoordinate(info.mouthX)
        && validCoordinate(info.mouthY)
        && validLayout(info.eyesCropY, info.eyesCropHeight, info.eyesWidth, info.eyesHeight)
        && validLayout(info.mouthCropY, info.mouthCropHeight, info.mouthWidth, info.mouthHeight)
        && info.eyesBrightness >= 0 && info.eyesBrightness <= 255
        && info.mouthBrightness >= 0 && info.mouthBrightness <= 255
        && validPatches(info.eyesPatches, info.eyesFrames, info.eyesWidth, info.eyesHeight)
        && validPatches(info.mouthPatches, info.mouthFrames, info.mouthWidth, info.mouthHeight)
        && ((info.eyesPatches.empty() && info.mouthPatches.empty()) || info.restFromBackground)
        && (!info.restFromBackground || (!info.backgroundAsset.empty()
            && info.eyesX >= 0 && info.eyesY >= 0 && info.mouthX >= 0 && info.mouthY >= 0
            && info.eyesWidth > 0 && info.mouthWidth > 0
            && !info.doubleEyes && !info.doubleMouth));
}

} // namespace ModMentatConfig

#endif // MODMENTATCONFIG_H
