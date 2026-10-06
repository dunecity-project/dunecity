#ifndef DUNE2R_PRESENTATION_H
#define DUNE2R_PRESENTATION_H

#include <SDL.h>
#include <string>

// Presentation only: never change TILESIZE, gameplay footprints or network data.
inline int dune2rPresentationScale(const std::string& activeMod, bool enhanced) {
    return activeMod == "Dune2R" && enhanced ? 3 : 1;
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
