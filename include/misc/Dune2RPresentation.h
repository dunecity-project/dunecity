#ifndef DUNE2R_PRESENTATION_H
#define DUNE2R_PRESENTATION_H

#include <SDL.h>
#include <string>

// The content base must come from ModManager's verified Workshop revision, not
// an unvalidated mod.ini. Arbitrary Dune2R-derived mods keep their prior scale.
inline bool dune2rIsWorkshopSnapshotName(const std::string& activeMod) {
    return activeMod.size() == 67 && activeMod.compare(0, 3, "ws-") == 0
        && activeMod.find_first_not_of("0123456789abcdef", 3) == std::string::npos;
}

inline bool dune2rUsesRemasterPresentation(const std::string& activeMod,
                                          const std::string& verifiedContentBase = {},
                                          bool verifiedCanonicalSnapshot = false) {
    if(activeMod == "Dune2R") {
        return true;
    }
    return verifiedCanonicalSnapshot && verifiedContentBase == "Dune2R"
        && dune2rIsWorkshopSnapshotName(activeMod);
}

// Presentation only: never change TILESIZE, gameplay footprints or network data.
inline int dune2rPresentationScale(const std::string& activeMod, bool enhanced,
                                   const std::string& verifiedContentBase = {},
                                   bool verifiedCanonicalSnapshot = false) {
    return enhanced && dune2rUsesRemasterPresentation(activeMod, verifiedContentBase,
                                                     verifiedCanonicalSnapshot) ? 3 : 1;
}

// User render preferences must never change immutable Workshop payloads.
inline std::string dune2rRenderPreferencesPath(const std::string& activeMod,
                                              const std::string& modPath,
                                              const std::string& userConfigPath) {
    return dune2rIsWorkshopSnapshotName(activeMod) ? userConfigPath : modPath + "/workshop-render.ini";
}

inline std::string dune2rRenderPreferencesSection(const std::string& activeMod) {
    return dune2rIsWorkshopSnapshotName(activeMod) ? "Dune2R EditoR " + activeMod : "Dune2R EditoR";
}

inline thread_local int dune2rWorldDrawingScale = 1;

class Dune2RWorldDrawingScope final {
public:
    explicit Dune2RWorldDrawingScope(int scale)
        : previous(dune2rWorldDrawingScale) {
        dune2rWorldDrawingScale = scale == 3 ? 3 : 1;
    }
    ~Dune2RWorldDrawingScope() { reset(); }
    Dune2RWorldDrawingScope(const Dune2RWorldDrawingScope&) = delete;
    Dune2RWorldDrawingScope& operator=(const Dune2RWorldDrawingScope&) = delete;
    void reset() {
        if(active) {
            dune2rWorldDrawingScale = previous;
            active = false;
        }
    }
private:
    int previous;
    bool active = true;
};

inline int dune2rWorldExtent(int classicPixels) {
    return classicPixels * dune2rWorldDrawingScale;
}

#endif
