#include <INIMap/INIMapPreviewCreator.h>

#include <FileClasses/FileManager.h>

#include <MapSeed.h>
#include <RadarView.h>

#include <misc/draw_util.h>
#include <misc/format.h>
#include <misc/exceptions.h>
#include <misc/FrameYield.h>
#include <misc/SpiceGeneration.h>
#include <INIMap/SpiceVariants.h>

#include <mmath.h>
#include <sand.h>
#include <globals.h>

#include <algorithm>
#include <cmath>

INIMapPreviewCreator::INIMapPreviewCreator(INIMap::inifile_ptr pINIFile)
 : INIMap(std::move(pINIFile))
{

}

INIMapPreviewCreator::~INIMapPreviewCreator() = default;

/**
    This method is used to create a mini map of a map file before the map is being played (e.g. in the map selection menu).
    The surface is 128x128 pixels plus 2*borderWidth and the map is scaled appropriately.
    \param  borderWidth the width of the border
    \param  borderColor the color of the border
    \return the minimap of size (128+2*borderWidth)x(128+2*borderWidth)
*/
sdl2::surface_ptr INIMapPreviewCreator::createMinimapImageOfMap(int borderWidth, Uint32 borderColor) {
    checkFeatures();
    const unsigned spiceMask = activeSpiceVariantMask();
    const int variantPercent = inifile->getIntValue("MAP", "SpiceVariantPercent", 0);

    auto pMinimap = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, 128+2*borderWidth, 128+2*borderWidth, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(pMinimap == nullptr) {
        return nullptr;
    }
    SDL_FillRect(pMinimap.get(), nullptr, borderColor);
    SDL_Rect dest = { borderWidth, borderWidth, pMinimap->w - 2*borderWidth, pMinimap->h - 2*borderWidth};
    SDL_FillRect(pMinimap.get(), &dest, COLOR_BLACK);

    int version = inifile->getIntValue("BASIC", "Version", 1);

    int offsetX = 0;
    int offsetY = 0;
    int scale = 1;
    int sizeX = 64;
    int sizeY = 64;
    int logicalSizeX = 64;
    int logicalSizeY = 64;
    int logicalOffsetX = 0;
    int logicalOffsetY = 0;
    RadarScaleInfo scaleInfo{};

    if(version < 2) {
        // old map format with seed value
        int SeedNum = inifile->getIntValue("MAP","Seed",-1);

        if(SeedNum == -1) {
            logError("Cannot read Seed in this map!");
        }

        int mapscale = inifile->getIntValue("BASIC","MapScale",0);

        switch(mapscale) {
            case 0: {
                scale = 2;
                sizeX = 62;
                sizeY = 62;
                offsetX = 2;
                offsetY = 2;
                logicalOffsetX = 1;
                logicalOffsetY = 1;
            } break;

            case 1: {
                scale = 4;
                sizeX = 32;
                sizeY = 32;
                logicalOffsetX = 16;
                logicalOffsetY = 16;
            } break;

            case 2: {
                scale = 5;
                sizeX = 21;
                sizeY = 21;
                offsetX = 11;
                offsetY = 11;
                logicalOffsetX = 11;
                logicalOffsetY = 11;
            } break;

            default: {
                logError("Unknown MapScale!");
            } break;
        }

        logicalSizeX = 64;
        logicalSizeY = 64;

        offsetX += borderWidth;
        offsetY += borderWidth;

        Uint16 SeedMap[64*64];
        createMapWithSeed(SeedNum,SeedMap);

        // "draw" spice fields into SeedMap
        std::string FieldString = inifile->getStringValue("MAP","Field");
        if(FieldString != "") {
            std::vector<std::string> FieldPositions  = splitStringToStringVector(FieldString);

            for(unsigned int i=0; i < FieldPositions.size();i++) {
                // set bloom
                int FieldPos;
                if(parseString(FieldPositions[i], FieldPos)) {
                    int xpos = FieldPos % logicalSizeX;
                    int ypos = FieldPos / logicalSizeX;

                    for(int x = -5; x <= 5; x++) {
                        for(int y = -5; y <= 5; y++) {

                            if(xpos+x >= 0 && xpos+x < logicalSizeX && ypos+y >= 0 && ypos+y < logicalSizeY) {
                                if(((SeedMap[64*(ypos+y) + (xpos+x)] >> 4) == 0x7)
                                    && (distanceFrom(xpos, ypos, xpos + x, ypos + y) <= 5)) {

                                    SeedMap[64*(ypos+y) + (xpos+x)] = (x==0 && y==0) ? 0xC0 : 0xB0;
                                }
                            }
                        }
                    }

                } else {
                    logError(inifile->getKey("MAP", "Field")->getLineNumber(), "Invalid value for key Field: " + FieldString);
                }
            }
        }

        std::string spiceVariants;
        if(spiceMask != 0 && variantPercent > 0) {
            spiceVariants.assign(static_cast<size_t>(sizeX) * sizeY, '-');
            for(int y = 0; y < sizeY; ++y) {
                for(int x = 0; x < sizeX; ++x) {
                    const int type = SeedMap[(y + logicalOffsetY) * 64 + x + logicalOffsetX] >> 4;
                    spiceVariants[static_cast<size_t>(y) * sizeX + x] = type == 0xb ? '~' : type == 0xc ? '+' : '-';
                }
            }
            for(const char* key : {"Bloom", "Special"}) {
                for(const auto& value : splitStringToStringVector(inifile->getStringValue("MAP", key))) {
                    int position;
                    if(!parseString(value, position)) continue;
                    const int x = position % logicalSizeX - logicalOffsetX;
                    const int y = position / logicalSizeX - logicalOffsetY;
                    if(x >= 0 && x < sizeX && y >= 0 && y < sizeY) {
                        spiceVariants[static_cast<size_t>(y) * sizeX + x] = '-';
                    }
                }
            }
            spiceVariants = SpiceVariants::apply(std::move(spiceVariants), sizeX, variantPercent,
                static_cast<uint32_t>(inifile->getIntValue("MAP", "SpiceVariantSeed", 0)),
                inifile->getIntValue("MAP", "SpiceVariantVersion", 1));
        }

        for(int y = 0; y < sizeY; y++) {
#ifdef __EMSCRIPTEN__
            // Browser build: this preview render runs inside a click handler
            // (map row select / lobby entry) with no frame boundary until it
            // finishes. Yield every few rows so input, signaling and the
            // compositor keep flowing during the render. No surface lock is
            // held across the yield (putPixel writes directly).
            if((y & 7) == 7) {
                yieldFrameToBrowser();
            }
#endif
            for(int x = 0; x < sizeX; x++) {
                Uint32 color = COLOR_BLACK;
                unsigned char seedmaptype = SeedMap[(y+logicalOffsetY)*64+x+logicalOffsetX] >> 4;
                switch(seedmaptype) {

                    case 0x7: {
                        // Normal sand
                        color = COLOR_DESERTSAND;
                    } break;

                    case 0x2:
                    case 0x8: {
                        // Rock or building
                        color = COLOR_ROCK;
                    } break;

                    case 0x9: {
                        // Sand dunes
                        color = COLOR_DESERTSAND;
                    } break;

                    case 0xa: {
                        // Mountain
                        color = COLOR_MOUNTAIN;
                    } break;

                    case 0xb: {
                        // Spice
                        color = COLOR_SPICE;
                    } break;

                    case 0xc: {
                        // Thick spice
                        color = COLOR_THICKSPICE;
                    } break;
                }
                if(!spiceVariants.empty()) {
                    const auto type = spiceTerrainForMapChar(spiceVariants[static_cast<size_t>(y) * sizeX + x]);
                    if(type != Terrain_Sand) color = getColorByTerrainType(allowedSpiceTerrain(type, spiceMask));
                }

                for(int i=0;i<scale;i++) {
                    for(int j=0;j<scale;j++) {
                        putPixel(pMinimap.get(), x*scale + i + offsetX, y*scale + j + offsetY, color);
                    }
                }
            }
        }

        // draw spice blooms
        std::string BloomString = inifile->getStringValue("MAP","Bloom");
        if(BloomString != "") {
            std::vector<std::string> BloomPositions  = splitStringToStringVector(BloomString);

            for(const std::string& strBloomPos : BloomPositions) {
                // set bloom
                int BloomPos;
                if(parseString(strBloomPos, BloomPos)) {
                    int xpos = BloomPos % logicalSizeX - logicalOffsetX;
                    int ypos = BloomPos / logicalSizeX - logicalOffsetY;
                    if(xpos >= 0 && xpos < sizeX && ypos >= 0 && ypos < sizeY) {
                        for(int i=0;i<scale;i++) {
                            for(int j=0;j<scale;j++) {
                                putPixel(pMinimap.get(), xpos*scale + i + offsetX, ypos*scale + j + offsetY, COLOR_BLOOM);
                            }
                        }
                    }
                } else {
                    logError(inifile->getKey("MAP", "Bloom")->getLineNumber(), "Invalid value for key Bloom: " + BloomString);
                }
            }
        }

        // draw special blooms
        std::string SpecialString = inifile->getStringValue("MAP","Special");
        if(SpecialString != "") {
            std::vector<std::string> SpecialPositions  = splitStringToStringVector(SpecialString);

            for(const std::string& strSpecialPos : SpecialPositions) {
                // set bloom
                int SpecialPos;
                if(parseString(strSpecialPos, SpecialPos)) {
                    int xpos = SpecialPos % logicalSizeX - logicalOffsetX;
                    int ypos = SpecialPos / logicalSizeX - logicalOffsetY;
                    if(xpos >= 0 && xpos < sizeX && ypos >= 0 && ypos < sizeY) {
                        for(int i=0;i<scale;i++) {
                            for(int j=0;j<scale;j++) {
                                putPixel(pMinimap.get(), xpos*scale + i + offsetX, ypos*scale + j + offsetY, COLOR_BLOOM);
                            }
                        }
                    }
                } else {
                    logError(inifile->getKey("MAP", "Special")->getLineNumber(), "Invalid value for key Special: " + SpecialString);
                }
            }
        }

        scaleInfo.scale = static_cast<float>(scale);
        scaleInfo.offsetX = offsetX;
        scaleInfo.offsetY = offsetY;
        scaleInfo.scaledWidth = sizeX * scale;
        scaleInfo.scaledHeight = sizeY * scale;


    } else {
        // new map format with saved map

        if((inifile->hasKey("MAP","SizeX") == false) || (inifile->hasKey("MAP","SizeY") == false)) {
            logError("SizeX and SizeY must be specified!");
        }

        sizeX = inifile->getIntValue("MAP","SizeX", 0);
        sizeY = inifile->getIntValue("MAP","SizeY", 0);

        logicalSizeX = sizeX;
        logicalSizeY = sizeY;

        scaleInfo = RadarView::calculateScaleAndOffsets(sizeX, sizeY);
        scaleInfo.offsetX += borderWidth;
        scaleInfo.offsetY += borderWidth;

        std::vector<Uint32> tileColors(sizeX * sizeY, MapRGBA(pMinimap->format, COLOR_BLACK));
        std::string spiceVariants;
        if(spiceMask != 0 && variantPercent > 0) spiceVariants.assign(static_cast<size_t>(sizeX) * sizeY, '-');

        for(int y=0;y<sizeY;y++) {
#ifdef __EMSCRIPTEN__
            // Browser build: parsing a saved map row by row (up to 256 rows
            // of per-tile characters) is a long synchronous block inside the
            // map-selection / lobby-creation click handler. Yield every few
            // rows so the page stays responsive while it grinds.
            if((y & 15) == 15) {
                yieldFrameToBrowser();
            }
#endif
            std::string rowKey = fmt::sprintf("%.3d", y);

            if(inifile->hasKey("MAP", rowKey) == false) {
                logError(inifile->getSection("MAP").getLineNumber(), "Map row " + std::to_string(y) + " does not exist!");
                continue;
            }

            std::string rowString = inifile->getStringValue("MAP",rowKey);
            int rowLength = rowString.size();

            if(rowLength < sizeX) {
                logWarning(inifile->getKey("MAP", rowKey)->getLineNumber(), "Map row " + std::to_string(y) + " is not long enough!");
            } else if(rowLength > sizeX) {
                logWarning(inifile->getKey("MAP", rowKey)->getLineNumber(), "Map row " + std::to_string(y) + " is too long!");
                rowLength = sizeX;
            }

            for(int x=0;x<rowLength;x++) {
                Uint32 color = COLOR_BLACK;
                switch(rowString.at(x)) {
                    case '-': {
                        // Normal sand
                        color = COLOR_DESERTSAND;
                    } break;

                    case '^': {
                        // Sand dunes
                        color = COLOR_DESERTSAND;
                    } break;

                    case '~': {
                        // Spice
                        color = COLOR_SPICE;
                    } break;

                    case '+': {
                        // Thick spice
                        color = COLOR_THICKSPICE;
                    } break;

                    case 'g':
                    case 'G':
                    case 'b':
                    case 'r':
                    case 'R':
                    case 'B':
                    case 'l':
                    case 'L':
                    case 'i':
                    case 'w':
                    case 'W':
                    case 'x':
                        color = getColorByTerrainType(allowedSpiceTerrain(spiceTerrainForMapChar(rowString.at(x)), spiceMask));
                        break;

                    case '%': {
                        // Rock
                        color = COLOR_ROCK;
                    } break;

                    case '@': {
                        // Mountain
                        color = COLOR_MOUNTAIN;
                    } break;

                    case 'O':
                    case 'Q': {
                        // Spice Bloom and Special Bloom
                        color = COLOR_BLOOM;
                    } break;

                    default: {
                        logError(inifile->getKey("MAP", rowKey)->getLineNumber(), std::string("Unknown map tile type '") + rowString.at(x) + "' in map tile (" + std::to_string(x) + ", " + std::to_string(y) + ")!");
                    } break;
                }

                tileColors[y * sizeX + x] = MapRGBA(pMinimap->format, color);
                if(!spiceVariants.empty()) {
                    const int type = allowedSpiceTerrain(spiceTerrainForMapChar(rowString.at(x)), spiceMask);
                    spiceVariants[static_cast<size_t>(y) * sizeX + x] =
                        type == Terrain_Spice ? '~' : type == Terrain_ThickSpice ? '+' : '-';
                }
            }

            for(int x=rowLength; x < sizeX; x++) {
                tileColors[y * sizeX + x] = MapRGBA(pMinimap->format, COLOR_DESERTSAND);
            }
        }

        if(!spiceVariants.empty()) {
            spiceVariants = SpiceVariants::apply(std::move(spiceVariants), sizeX, variantPercent,
                static_cast<uint32_t>(inifile->getIntValue("MAP", "SpiceVariantSeed", 0)),
                inifile->getIntValue("MAP", "SpiceVariantVersion", 1));
            for(size_t index = 0; index < spiceVariants.size(); ++index) {
                const auto type = spiceTerrainForMapChar(spiceVariants[index]);
                if(type != Terrain_Sand) {
                    tileColors[index] = MapRGBA(pMinimap->format, getColorByTerrainType(allowedSpiceTerrain(type, spiceMask)));
                }
            }
        }

        sdl2::surface_lock lock{pMinimap.get()};
        for(int pixelY = 0; pixelY < scaleInfo.scaledHeight; ++pixelY) {
            const int tileY = std::clamp(static_cast<int>(pixelY / scaleInfo.scale), 0, sizeY - 1);
            Uint32* row = reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(pMinimap->pixels)
                            + (scaleInfo.offsetY + pixelY) * pMinimap->pitch) + scaleInfo.offsetX;

            for(int pixelX = 0; pixelX < scaleInfo.scaledWidth; ++pixelX) {
                const int tileX = std::clamp(static_cast<int>(pixelX / scaleInfo.scale), 0, sizeX - 1);
                row[pixelX] = tileColors[tileY * sizeX + tileX];
            }
        }
    }

    auto paintTileRect = [&](int tileX, int tileY, int width, int height, Uint32 surfaceColor) {
        if(scaleInfo.scaledWidth == 0 || scaleInfo.scaledHeight == 0) {
            return;
        }

        if(width <= 0 || height <= 0) {
            return;
        }

        const int startTileX = std::max(0, tileX);
        const int startTileY = std::max(0, tileY);
        const int endTileX = std::min(sizeX, tileX + width);
        const int endTileY = std::min(sizeY, tileY + height);

        if(startTileX >= endTileX || startTileY >= endTileY) {
            return;
        }

        const int pixelStartX = scaleInfo.offsetX + static_cast<int>(std::floor(startTileX * scaleInfo.scale));
        const int pixelStartY = scaleInfo.offsetY + static_cast<int>(std::floor(startTileY * scaleInfo.scale));
        const int pixelWidth = std::max(1, static_cast<int>(std::round((endTileX - startTileX) * scaleInfo.scale)));
        const int pixelHeight = std::max(1, static_cast<int>(std::round((endTileY - startTileY) * scaleInfo.scale)));

        const int maxPixelX = scaleInfo.offsetX + scaleInfo.scaledWidth;
        const int maxPixelY = scaleInfo.offsetY + scaleInfo.scaledHeight;

        for(int py = 0; py < pixelHeight; ++py) {
            const int drawY = pixelStartY + py;
            if(drawY < scaleInfo.offsetY || drawY >= maxPixelY) {
                continue;
            }

            auto* row = reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(pMinimap->pixels) + drawY * pMinimap->pitch);

            for(int px = 0; px < pixelWidth; ++px) {
                const int drawX = pixelStartX + px;
                if(drawX < scaleInfo.offsetX || drawX >= maxPixelX) {
                    continue;
                }

                row[drawX] = surfaceColor;
            }
        }
    };

    // draw structures
    if(inifile->hasSection("STRUCTURES")) {
        for(const INIFile::Key& key : inifile->getSection("STRUCTURES")) {
            std::string tmpkey = key.getKeyName();
            std::string tmp = key.getStringValue();

            if(tmpkey.compare(0,3,"GEN") == 0) {
                // Gen Object/Structure

                std::string PosStr = tmpkey.substr(3,tmpkey.size()-3);
                int pos;
                if(!parseString(PosStr, pos)) {
                    continue;
                }

                std::string HouseStr, BuildingStr;
                splitString(tmp, HouseStr, BuildingStr);

                int house = getHouseByName(HouseStr);
                Uint32 color = COLOR_WHITE;
                if(house != HOUSE_INVALID) {
                    color = getHouseRadarColor(static_cast<HOUSETYPE>(house));
                } else {
                    convertToLower(HouseStr);
                    if(HouseStr.length() == 7 && HouseStr.substr(0,6) == "player") {
                        int playernum = HouseStr.at(6)-'0';

                        if(playernum >= 1 && playernum <= 6) {
                            int val = 32*(playernum - 1) + 32;
                            color = COLOR_RGB(val, val, val);
                        }
                    } else {
                        logError(key.getLineNumber(), "Invalid house string: '" + HouseStr + "'!");
                    }
                }

                if(BuildingStr == "Concrete") {
                    // nothing
                } else if(BuildingStr == "Wall") {
                    int x = pos % logicalSizeX - logicalOffsetX;
                    int y = pos / logicalSizeX - logicalOffsetY;

                    if(x >= 0 && x < sizeX && y >= 0 && y < sizeY) {
                        const Uint32 surfaceColor = MapRGBA(pMinimap->format, color);
                        paintTileRect(x, y, 1, 1, surfaceColor);
                    }
                }
            } else if(key.getKeyName().find("ID") == 0) {
                // other structure
                std::string HouseStr, BuildingStr, health, PosStr;
                splitString(tmp, HouseStr, BuildingStr, health, PosStr);

                int pos;
                if(!parseString(PosStr, pos)) {
                    continue;
                }

                int house = getHouseByName(HouseStr);
                Uint32 color = COLOR_WHITE;
                if(house != HOUSE_INVALID) {
                    color = getHouseRadarColor(static_cast<HOUSETYPE>(house));
                } else {
                    convertToLower(HouseStr);
                    if(HouseStr.length() == 7 && HouseStr.substr(0,6) == "player") {
                        int playernum = HouseStr.at(6)-'0';

                        if(playernum >= 1 && playernum <= 6) {
                            int val = 32*(playernum - 1) + 32;
                            color = COLOR_RGB(val, val, val);
                        }
                    } else {
                        logError(key.getLineNumber(), "Invalid house string: '" + HouseStr + "'!");
                    }
                }

                Coord size = getStructureSize(getItemIDByName(BuildingStr));

                int posX = pos % logicalSizeX - logicalOffsetX;
                int posY = pos / logicalSizeX - logicalOffsetY;
                const Uint32 surfaceColor = MapRGBA(pMinimap->format, color);
                paintTileRect(posX, posY, size.x, size.y, surfaceColor);
            }
        }
    }

    return pMinimap;
}
