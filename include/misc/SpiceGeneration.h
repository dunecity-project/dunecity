#pragma once

#include <data.h>
#include <mod/ModManager.h>

#include <utility>

// Variant identities are stable in maps and saves; enabled colours are cached mod content.
inline unsigned activeSpiceVariantMask() {
    const auto& mods = ModManager::instance();
    return mods.isTornieFamilyContentActive() ? mods.getActiveSpiceVariantMask() : 0u;
}

// Disabled content switches do not change the edition's two generator colour identities.
inline std::pair<TERRAINTYPE, TERRAINTYPE> activeSpicePalette() {
    const auto& mods = ModManager::instance();
    const auto base = mods.getContentBase(mods.getActiveModName());
    if(base == "Jericho" || base == "JerichoLite") return {Terrain_RedSpice, Terrain_WhiteSpice};
    if(base == "ArcaKev" || base == "ArcaKevLite") return {Terrain_GreenSpice, Terrain_WhiteSpice};
    return {Terrain_GreenSpice, Terrain_PaleLilacSpice};
}

// Fixed roll bands preserve upstream's deterministic twenty-percent variant budget.
inline std::pair<int, int> generatedSpiceTerrainForRoll(int roll, unsigned mask) {
    if(roll >= 0 && roll < 5 && (mask & 1u)) return {Terrain_GreenSpice, Terrain_ThickGreenSpice};
    if(roll >= 5 && roll < 10 && (mask & 2u)) return {Terrain_RedSpice, Terrain_ThickRedSpice};
    if(roll >= 10 && roll < 15 && (mask & 4u)) return {Terrain_PaleLilacSpice, Terrain_ThickPaleLilacSpice};
    if(roll >= 15 && roll < 20 && (mask & 8u)) return {Terrain_WhiteSpice, Terrain_ThickWhiteSpice};
    return {Terrain_Spice, Terrain_ThickSpice};
}

inline int allowedSpiceTerrain(int type, unsigned mask) {
    unsigned family = 0;
    int normal = Terrain_Spice;
    switch(type) {
        case Terrain_GreenSpice: family = 1u; break;
        case Terrain_ThickGreenSpice: family = 1u; normal = Terrain_ThickSpice; break;
        case Terrain_GreenSpiceBloom: family = 1u; normal = Terrain_SpiceBloom; break;
        case Terrain_RedSpice: family = 2u; break;
        case Terrain_ThickRedSpice: family = 2u; normal = Terrain_ThickSpice; break;
        case Terrain_RedSpiceBloom: family = 2u; normal = Terrain_SpiceBloom; break;
        case Terrain_PaleLilacSpice: family = 4u; break;
        case Terrain_ThickPaleLilacSpice: family = 4u; normal = Terrain_ThickSpice; break;
        case Terrain_PaleLilacSpiceBloom: family = 4u; normal = Terrain_SpiceBloom; break;
        case Terrain_WhiteSpice: family = 8u; break;
        case Terrain_ThickWhiteSpice: family = 8u; normal = Terrain_ThickSpice; break;
        case Terrain_WhiteSpiceBloom: family = 8u; normal = Terrain_SpiceBloom; break;
        default: return type;
    }
    return (mask & family) ? type : normal;
}

inline TERRAINTYPE spiceTerrainForMapChar(char value) {
    switch(value) {
        case '~': return Terrain_Spice;
        case '+': return Terrain_ThickSpice;
        case 'g': return Terrain_GreenSpice;
        case 'G': return Terrain_ThickGreenSpice;
        case 'b': return Terrain_GreenSpiceBloom;
        case 'r': return Terrain_RedSpice;
        case 'R': return Terrain_ThickRedSpice;
        case 'B': return Terrain_RedSpiceBloom;
        case 'l': return Terrain_PaleLilacSpice;
        case 'L': return Terrain_ThickPaleLilacSpice;
        case 'i': return Terrain_PaleLilacSpiceBloom;
        case 'w': return Terrain_WhiteSpice;
        case 'W': return Terrain_ThickWhiteSpice;
        case 'x': return Terrain_WhiteSpiceBloom;
        default: return Terrain_Sand;
    }
}
