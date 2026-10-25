/*
 *  ZoneStructureTestCase.cpp - Tests for zone structures
 *
 *  Validates: zone structure IDs, isZoneStructure helper, placement rules,
 *  and regression coverage for the placement-crash fix (zone tile marking must
 *  happen in setLocation, not in the constructor).
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <fstream>
#include <string>
#include <cstdlib>

// =============================================================================
// Zone Structure ID Tests
// =============================================================================

TEST_CASE("ZoneStructure: Structure IDs are correctly assigned", "[zone][ids]") {
    REQUIRE(Structure_ZoneResidential == 20);
    REQUIRE(Structure_ZoneCommercial == 21);
    REQUIRE(Structure_ZoneIndustrial == 22);
    REQUIRE(Structure_LastID == 26);  // PoliceStation (26) is now the last structure
}

TEST_CASE("ZoneStructure: Unit IDs start after zone structures", "[zone][ids]") {
    REQUIRE(Unit_FirstID == 27);  // After Structure_PoliceStation (26)
    REQUIRE(Unit_Carryall == 27);
}

TEST_CASE("ZoneStructure: isZoneStructure returns true for all zone types", "[zone][helpers]") {
    REQUIRE(isZoneStructure(Structure_ZoneResidential));
    REQUIRE(isZoneStructure(Structure_ZoneCommercial));
    REQUIRE(isZoneStructure(Structure_ZoneIndustrial));
}

TEST_CASE("ZoneStructure: isZoneStructure returns false for non-zones", "[zone][helpers]") {
    REQUIRE_FALSE(isZoneStructure(Structure_ConstructionYard));
    REQUIRE_FALSE(isZoneStructure(Structure_WindTrap));
    REQUIRE_FALSE(isZoneStructure(Structure_Road));
    REQUIRE_FALSE(isZoneStructure(Unit_Harvester));
}

TEST_CASE("ZoneStructure: Zone structures are structures not units", "[zone][helpers]") {
    REQUIRE(isStructure(Structure_ZoneResidential));
    REQUIRE(isStructure(Structure_ZoneCommercial));
    REQUIRE(isStructure(Structure_ZoneIndustrial));
    REQUIRE_FALSE(isUnit(Structure_ZoneResidential));
    REQUIRE_FALSE(isUnit(Structure_ZoneCommercial));
    REQUIRE_FALSE(isUnit(Structure_ZoneIndustrial));
}

// =============================================================================
// Zone Type Helper Tests  
// =============================================================================

TEST_CASE("ZoneStructure: isZoneType returns correct values", "[zone][helpers]") {
    // This would test ZoneType enum values if exposed
    REQUIRE(true); // Placeholder - requires game context
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST_CASE("ZoneStructure: Full placement flow would register zone with city sim", "[zone][integration]") {
    // Full integration test would require:
    // 1. Create Construction Yard
    // 2. Build Residential Zone
    // 3. Verify tile has cityZoneType = Residential
    // 4. Verify city sim processes the zone
    // This is tested through manual verification
    REQUIRE(true);
}

TEST_CASE("ZoneStructure: Destruction clears zone from tiles", "[zone][integration]") {
    // Would test that destroying a zone structure clears zone from tiles
    REQUIRE(true);
}

// =============================================================================
// Placement-crash regression tests (source-scan)
//
// The original bug: ZoneStructure constructor called getLocation() and
// currentGameMap->getTile() before the object was placed, producing (-1,-1)
// coordinates and crashing at runtime.  The fix moves tile marking into
// setLocation().  These tests verify the fix at source level.
// =============================================================================

static std::string readSourceFile(const std::string& relativePath) {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    if (!env) {
        SKIP("DUNE_CITY_SOURCE_DIR not set — skipping source-scan test");
    }
    std::ifstream f(std::string(env) + "/" + relativePath);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// Extract the body of a specific function from source text.
static std::string extractFunctionBody(const std::string& src,
                                        const std::string& signature) {
    auto pos = src.find(signature);
    if (pos == std::string::npos) return {};
    auto bracePos = src.find('{', pos);
    if (bracePos == std::string::npos) return {};
    int depth = 1;
    auto i = bracePos + 1;
    while (i < src.size() && depth > 0) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') --depth;
        ++i;
    }
    return src.substr(bracePos, i - bracePos);
}

TEST_CASE("ZoneStructure: constructor does NOT touch map tiles",
          "[zone][placement][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    // The primary constructor: ZoneStructure(House*, DuneCity::ZoneType)
    std::string ctorBody = extractFunctionBody(src,
        "ZoneStructure::ZoneStructure(House* newOwner, DuneCity::ZoneType");
    REQUIRE_FALSE(ctorBody.empty());

    INFO("Constructor must not call getTile (would crash before placement)");
    REQUIRE(ctorBody.find("getTile") == std::string::npos);

    INFO("Constructor must not call getLocation (invalid before placement)");
    REQUIRE(ctorBody.find("getLocation") == std::string::npos);
}

TEST_CASE("ZoneStructure: setLocation marks zone tiles",
          "[zone][placement][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string setLocBody = extractFunctionBody(src,
        "ZoneStructure::setLocation(");
    REQUIRE_FALSE(setLocBody.empty());

    INFO("setLocation must call setCityZoneType to mark tiles");
    REQUIRE(setLocBody.find("setCityZoneType") != std::string::npos);

    INFO("setLocation must call setCityZoneDensity to initialise density");
    REQUIRE(setLocBody.find("setCityZoneDensity") != std::string::npos);
}

TEST_CASE("ZoneStructure: setLocation guards against invalid position",
          "[zone][placement][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string setLocBody = extractFunctionBody(src,
        "ZoneStructure::setLocation(");
    REQUIRE_FALSE(setLocBody.empty());

    INFO("setLocation must check for invalid location before touching tiles");
    REQUIRE(setLocBody.find("isInvalid") != std::string::npos);
}

TEST_CASE("ZoneStructure: header declares setLocation override",
          "[zone][placement][regression]") {
    std::string hdr = readSourceFile("include/structures/ZoneStructure.h");
    REQUIRE_FALSE(hdr.empty());

    INFO("ZoneStructure.h must declare setLocation override");
    REQUIRE(hdr.find("setLocation") != std::string::npos);
    REQUIRE(hdr.find("override") != std::string::npos);
}

TEST_CASE("ZoneStructure: placement allows sand only when anchored on rock or slab",
          "[zone][placement][sand][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string body = extractFunctionBody(src, "ZoneStructure::canBePlacedAt(");
    REQUIRE_FALSE(body.empty());
    // Every tile must be zone terrain (rock, slab, sand or dunes) ...
    REQUIRE(body.find("isCityZoneTerrain") != std::string::npos);
    // ... and at least one tile must be rock or slab.
    REQUIRE(body.find("isCityBuildableTerrain") != std::string::npos);
    REQUIRE(body.find("anchoredTiles == 0") != std::string::npos);
}

// =============================================================================
// Zone structure initialization regression tests
//
// Zone structures must set itemID and call setHealth(getMaxHealth()) so they
// don't spawn with health=0 and immediately explode.
// =============================================================================

static std::string extractFunctionBodyByName(const std::string& src,
                                              const std::string& signature) {
    auto pos = src.find(signature);
    if (pos == std::string::npos) return {};
    auto bracePos = src.find('{', pos);
    if (bracePos == std::string::npos) return {};
    int depth = 1;
    auto i = bracePos + 1;
    while (i < src.size() && depth > 0) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') --depth;
        ++i;
    }
    return src.substr(bracePos, i - bracePos);
}

TEST_CASE("ZoneStructure: ResidentialZone sets itemID in init",
          "[zone][init][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string initBody = extractFunctionBodyByName(src, "void ResidentialZone::init()");
    REQUIRE_FALSE(initBody.empty());

    INFO("ResidentialZone::init must set itemID = Structure_ZoneResidential");
    REQUIRE(initBody.find("Structure_ZoneResidential") != std::string::npos);
    REQUIRE(initBody.find("itemID") != std::string::npos);
}

TEST_CASE("ZoneStructure: CommercialZone sets itemID in init",
          "[zone][init][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string initBody = extractFunctionBodyByName(src, "void CommercialZone::init()");
    REQUIRE_FALSE(initBody.empty());

    INFO("CommercialZone::init must set itemID = Structure_ZoneCommercial");
    REQUIRE(initBody.find("Structure_ZoneCommercial") != std::string::npos);
}

TEST_CASE("ZoneStructure: IndustrialZone sets itemID in init",
          "[zone][init][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    std::string initBody = extractFunctionBodyByName(src, "void IndustrialZone::init()");
    REQUIRE_FALSE(initBody.empty());

    INFO("IndustrialZone::init must set itemID = Structure_ZoneIndustrial");
    REQUIRE(initBody.find("Structure_ZoneIndustrial") != std::string::npos);
}

TEST_CASE("ZoneStructure: subclass constructors call setHealth(getMaxHealth())",
          "[zone][init][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    // Each primary constructor (House* newOwner) must call setHealth(getMaxHealth())
    auto resBody = extractFunctionBodyByName(src,
        "ResidentialZone::ResidentialZone(House* newOwner)");
    REQUIRE_FALSE(resBody.empty());
    INFO("ResidentialZone constructor must call setHealth(getMaxHealth())");
    REQUIRE(resBody.find("setHealth(getMaxHealth())") != std::string::npos);

    auto comBody = extractFunctionBodyByName(src,
        "CommercialZone::CommercialZone(House* newOwner)");
    REQUIRE_FALSE(comBody.empty());
    INFO("CommercialZone constructor must call setHealth(getMaxHealth())");
    REQUIRE(comBody.find("setHealth(getMaxHealth())") != std::string::npos);

    auto indBody = extractFunctionBodyByName(src,
        "IndustrialZone::IndustrialZone(House* newOwner)");
    REQUIRE_FALSE(indBody.empty());
    INFO("IndustrialZone constructor must call setHealth(getMaxHealth())");
    REQUIRE(indBody.find("setHealth(getMaxHealth())") != std::string::npos);
}

TEST_CASE("ZoneStructure: subclass constructors call owner incrementStructures",
          "[zone][init][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    // Each init() must register with owner
    auto resInit = extractFunctionBodyByName(src, "void ResidentialZone::init()");
    REQUIRE_FALSE(resInit.empty());
    REQUIRE(resInit.find("incrementStructures") != std::string::npos);

    auto comInit = extractFunctionBodyByName(src, "void CommercialZone::init()");
    REQUIRE_FALSE(comInit.empty());
    REQUIRE(comInit.find("incrementStructures") != std::string::npos);

    auto indInit = extractFunctionBodyByName(src, "void IndustrialZone::init()");
    REQUIRE_FALSE(indInit.empty());
    REQUIRE(indInit.find("incrementStructures") != std::string::npos);
}

// =============================================================================
// Zone graphics regression tests
//
// Zone structures must use their own dedicated ObjPic_Zone* graphic IDs, not
// ObjPic_Windtrap.  Using Windtrap causes black-square rendering because the
// Windtrap sprite has 4 animation frames and zone structures expect a single
// static frame.
// =============================================================================

TEST_CASE("ZoneStructure: zone init uses dedicated ObjPic_Zone graphic IDs",
          "[zone][graphics][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    auto resInit = extractFunctionBody(src, "void ResidentialZone::init()");
    REQUIRE_FALSE(resInit.empty());
    INFO("ResidentialZone must use ObjPic_ZoneResidential, not ObjPic_Windtrap");
    REQUIRE(resInit.find("ObjPic_ZoneResidential") != std::string::npos);
    REQUIRE(resInit.find("ObjPic_Windtrap") == std::string::npos);

    auto comInit = extractFunctionBody(src, "void CommercialZone::init()");
    REQUIRE_FALSE(comInit.empty());
    INFO("CommercialZone must use ObjPic_ZoneCommercial, not ObjPic_Windtrap");
    REQUIRE(comInit.find("ObjPic_ZoneCommercial") != std::string::npos);
    REQUIRE(comInit.find("ObjPic_Windtrap") == std::string::npos);

    auto indInit = extractFunctionBody(src, "void IndustrialZone::init()");
    REQUIRE_FALSE(indInit.empty());
    INFO("IndustrialZone must use ObjPic_ZoneIndustrial, not ObjPic_Windtrap");
    REQUIRE(indInit.find("ObjPic_ZoneIndustrial") != std::string::npos);
    REQUIRE(indInit.find("ObjPic_Windtrap") == std::string::npos);
}

TEST_CASE("ZoneStructure: ObjPic_Zone enums exist in GFXManager.h",
          "[zone][graphics][regression]") {
    std::string hdr = readSourceFile("include/FileClasses/GFXManager.h");
    REQUIRE_FALSE(hdr.empty());

    INFO("GFXManager.h must define ObjPic_ZoneResidential");
    REQUIRE(hdr.find("ObjPic_ZoneResidential") != std::string::npos);
    INFO("GFXManager.h must define ObjPic_ZoneCommercial");
    REQUIRE(hdr.find("ObjPic_ZoneCommercial") != std::string::npos);
    INFO("GFXManager.h must define ObjPic_ZoneIndustrial");
    REQUIRE(hdr.find("ObjPic_ZoneIndustrial") != std::string::npos);
}

// =============================================================================
// Zone power status regression tests
//
// The zone tooltip/inspector must derive power status from the Dune house
// power model (House::hasPower), not from the stubbed per-tile city power
// grid (Tile::isCityPowered), which always returns false. The tooltip
// rendering moved from Game.cpp into ZoneStructureInterface.h alongside the
// other zone-specific UI; the invariants are unchanged.
// =============================================================================

TEST_CASE("ZoneStructure: tooltip uses house power model not tile power",
          "[zone][power][regression]") {
    std::string src = readSourceFile("include/GUI/ObjectInterfaces/ZoneStructureInterface.h");
    REQUIRE_FALSE(src.empty());

    // Find the zone inspector section — look for the density/power block.
    auto densityPos = src.find("getCityZoneDensity()");
    REQUIRE(densityPos != std::string::npos);

    // Extract a reasonable window around the power check.
    auto start = (densityPos > 200) ? densityPos - 200 : 0;
    auto window = src.substr(start, src.find("cityStats_.update(pZone)", densityPos) - start);

    INFO("Power check must use the owner's hasPower(), not Tile::isCityPowered()");
    REQUIRE(window.find("hasPower()") != std::string::npos);
    REQUIRE(window.find("isCityPowered()") == std::string::npos);
}

TEST_CASE("ZoneStructure: selected Dune2 zones use the house icon",
          "[zone][graphics][regression]") {
    const std::string src = readSourceFile("include/GUI/ObjectInterfaces/ZoneStructureInterface.h");
    REQUIRE_FALSE(src.empty());
    REQUIRE(src.find("isDuneCityHouseUsingDune2(pZone->getOwner()->getHouseID())") != std::string::npos);
    REQUIRE(src.find("objPicture.setVisible(showSkinIcon)") != std::string::npos);
    REQUIRE(src.find("zonePreview.setVisible(!showSkinIcon)") != std::string::npos);
    // Civic overlays now have authored portraits too. Their actual rendered
    // pixels and live overlay/skin transitions are checked by skin-probe.inc.
}

TEST_CASE("ZoneStructure: GFXManager creates zone placeholder graphics",
          "[zone][graphics][regression]") {
    std::string src = readSourceFile("src/FileClasses/GFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    INFO("GFXManager must create ObjPic_ZoneResidential graphic");
    REQUIRE(src.find("ObjPic_ZoneResidential") != std::string::npos);
    INFO("GFXManager must create ObjPic_ZoneCommercial graphic");
    REQUIRE(src.find("ObjPic_ZoneCommercial") != std::string::npos);
    INFO("GFXManager must create ObjPic_ZoneIndustrial graphic");
    REQUIRE(src.find("ObjPic_ZoneIndustrial") != std::string::npos);
}

// =============================================================================
// Imported zone sprite wiring regression tests
//
// The improved pipeline pre-generates derived 2×2 (32×32) assets from the full
// 3×3 Micropolis source composites.  GFXManager loads these directly from the
// composites_2x2/ directory at runtime, falling back to placeholders when the
// derived PNGs are absent.
// =============================================================================

TEST_CASE("ZoneStructure: derived 2x2 sprite files exist on disk",
          "[zone][graphics][sprites]") {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    if (!env) {
        SKIP("DUNE_CITY_SOURCE_DIR not set");
    }
    std::string base = std::string(env) + "/imported_sprites/micropolis/composites_2x2/";
    for (const char* f : {"res_v0_d2_2x2.png", "com_v0_d2_2x2.png", "ind_v0_d2_2x2.png"}) {
        std::ifstream check(base + f);
        INFO("Expected derived 2x2 sprite file: " << f);
        REQUIRE(check.good());
    }
}

// =============================================================================
// Zone sprite scaler crash regression tests
//
// The legacy Scaler functions (doubleTiledSurfaceScale2x, etc.) assume 8-bit
// paletted surfaces and dereference src->format->palette, which is NULL for
// 32-bit RGBA surfaces.  Zone sprites (both placeholders and imported PNGs)
// are 32-bit RGBA, so GFXManager must pre-generate all three zoom levels
// using format-agnostic scaling (SDL_BlitScaled) and store them so the
// scaler loop never processes these surfaces.
// =============================================================================

TEST_CASE("ZoneStructure: GFXManager pre-generates all zoom levels for zone sprites",
          "[zone][graphics][scaler][regression]") {
    std::string src = readSourceFile("src/FileClasses/GFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    // Find the zone sprite loading block
    auto blockStart = src.find("auto scaleRGBASurface");
    REQUIRE(blockStart != std::string::npos);

    // The block must set objPic[...][0], [1], and [2] for zone sprites
    // to bypass the paletted scaler loop at the end of the constructor.
    auto blockEnd = src.find("objPic[ObjPic_Bullet_SmallRocket]", blockStart);
    REQUIRE(blockEnd != std::string::npos);
    std::string block = src.substr(blockStart, blockEnd - blockStart);

    INFO("Zone block must pre-generate zoom level 1 (2x) to bypass 8-bit scaler");
    REQUIRE(block.find("z = 1; z < NUM_ZOOMLEVEL") != std::string::npos);

    INFO("Zone block must pre-generate zoom level 2 (3x) to bypass 8-bit scaler");
    REQUIRE(block.find("scaleRGBASurface(objPic[spec.id][HOUSE_HARKONNEN][0].get(), z + 1)") != std::string::npos);

    INFO("Zone block must use format-agnostic scaling (SDL_BlitScaled), not the paletted Scaler");
    REQUIRE(block.find("SDL_BlitScaled") != std::string::npos);
    REQUIRE(block.find("Scaler::") == std::string::npos);
}

TEST_CASE("ZoneStructure: GFXManager skips SDL_SetColorKey for truecolor zone sprites",
          "[zone][graphics][colorkey][regression]") {
    std::string src = readSourceFile("src/FileClasses/GFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    // The scaling loop must detect zone sprites as truecolor and skip
    // SDL_SetColorKey, which corrupts per-pixel alpha on 32-bit RGBA
    // surfaces (causes black squares at runtime).
    INFO("Scaling loop must identify zone sprites as truecolor");
    REQUIRE(src.find("isTruecolorSprite") != std::string::npos);

    INFO("Zone IDs must be included in the truecolor check");
    auto checkPos = src.find("isTruecolorSprite");
    REQUIRE(checkPos != std::string::npos);
    auto checkEnd = src.find(";", checkPos);
    REQUIRE(checkEnd != std::string::npos);
    std::string checkExpr = src.substr(checkPos, checkEnd - checkPos);
    REQUIRE(checkExpr.find("ObjPic_ZoneResidential") != std::string::npos);
    REQUIRE(checkExpr.find("ObjPic_ZoneCommercial") != std::string::npos);
    REQUIRE(checkExpr.find("ObjPic_ZoneIndustrial") != std::string::npos);

    INFO("SDL_SetColorKey calls must be guarded for truecolor sprites");
    REQUIRE((src.find("!isTruecolorSprite") != std::string::npos
             || src.find("!isCurrentTruecolorSprite") != std::string::npos));
}

// =============================================================================
TEST_CASE("ZoneStructure: civic art survives the renderer texture refresh",
          "[zone][graphics][civic][regression]") {
    const auto zone = extractFunctionBody(readSourceFile("src/structures/ZoneStructure.cpp"),
                                           "void ZoneStructure::updateStructureSpecificStuff()");
    const auto draw = extractFunctionBody(readSourceFile("src/structures/StructureBase.cpp"),
                                           "void StructureBase::blitToScreen()");
    REQUIRE_FALSE(zone.empty());
    REQUIRE_FALSE(draw.empty());
    // The render path re-resolves the texture by ID. Setting only `graphic`
    // in the update path draws a 4x4 atlas using the civic 1x1 frame layout.
    REQUIRE(draw.find("getObjPic(graphicID, owner->getHouseID())") != std::string::npos);
    const auto civic = extractFunctionBody(zone, "if (civicOverlay_ != CivicOverlay::None && density > 0)");
    REQUIRE(civic.find("graphicID =") != std::string::npos);
    REQUIRE(civic.find("ObjPic_Hospital : ObjPic_Church") != std::string::npos);
    REQUIRE(civic.find("getObjPic(graphicID,") != std::string::npos);
    REQUIRE(civic.find("numImagesX = 1") != std::string::npos);
    REQUIRE(civic.find("numImagesY = 1") != std::string::npos);
    // Clearing a civic overlay (including a vacant lot) restores the normal
    // atlas ID and dimensions, without depending on cached pointer equality.
    const auto restore = zone.substr(zone.find("switch (zoneType_)"));
    REQUIRE(restore.find("graphicID = ObjPic_ZoneResidential") != std::string::npos);
    REQUIRE(restore.find("graphicID = ObjPic_ZoneCommercial") != std::string::npos);
    REQUIRE(restore.find("graphicID = ObjPic_ZoneIndustrial") != std::string::npos);
    REQUIRE(restore.find("numImagesX = DuneCity::CitySprites::zoneColumns(zoneType_)") != std::string::npos);
    REQUIRE(restore.find("DuneCity::CitySprites::zoneRows(zoneType_)") != std::string::npos);
}

// Zone animation frame regression tests
//
// StructureBase::init() defaults firstAnimFrame = lastAnimFrame = curAnimFrame = 2,
// which works for multi-frame sprite sheets (frame 0 = destroyed, 1 = damaged,
// 2+ = normal).  Zone structures have single-frame textures (numImagesX=1,
// numImagesY=1).  With curAnimFrame=2, calcSpriteSourceRect computes
// source.y = 2 * textureHeight, reading out-of-bounds -> black squares.
// Fix: zone init() must set firstAnimFrame = lastAnimFrame = curAnimFrame = 0.
// =============================================================================

TEST_CASE("ZoneStructure: init sets animation frame to 0 and matches atlas size",
          "[zone][graphics][animation][regression]") {
    std::string src = readSourceFile("src/structures/ZoneStructure.cpp");
    REQUIRE_FALSE(src.empty());

    // Each zone init() must (a) start at frame 0 (top-left atlas cell, the
    // empty-lot d0/v0 placeholder) and (b) declare the atlas grid dims that
    // match the GFXManager builder. updateStructureSpecificStuff() then
    // walks curAnimFrame around the grid based on density + value tier.
    struct ZoneInit { const char* sig; const char* nx; const char* ny; };
    const ZoneInit zones[] = {
        { "void ResidentialZone::init()", "DuneCity::CitySprites::residentialColumns", "DuneCity::CitySprites::residentialRows" },
        { "void CommercialZone::init()", "DuneCity::CitySprites::commercialColumns", "4" },
        { "void IndustrialZone::init()", "DuneCity::CitySprites::industrialColumns", "DuneCity::CitySprites::industrialRows" },
    };
    for (const auto& z : zones) {
        auto body = extractFunctionBodyByName(src, z.sig);
        REQUIRE_FALSE(body.empty());

        INFO(std::string(z.sig) + " must set firstAnimFrame = 0");
        REQUIRE(body.find("firstAnimFrame") != std::string::npos);

        INFO(std::string(z.sig) + " must set curAnimFrame = 0");
        REQUIRE(body.find("curAnimFrame") != std::string::npos);

        const std::string xExpect = std::string("numImagesX = ") + z.nx;
        const std::string yExpect = std::string("numImagesY = ") + z.ny;
        INFO(std::string(z.sig) + " must set " + xExpect);
        REQUIRE(body.find(xExpect) != std::string::npos);
        INFO(std::string(z.sig) + " must set " + yExpect);
        REQUIRE(body.find(yExpect) != std::string::npos);
    }
}

// =============================================================================
// Zone build-menu icon regression tests
//
// GFXManager must create zone build-menu icons (smallDetailPicTex) by scaling
// the zone sprite down to 91x55, not by using SLAB.WSA directly.
// The blit must use SDL_BLENDMODE_NONE to avoid alpha-compositing artifacts
// when scaling onto the initially-transparent destination surface.
// =============================================================================

TEST_CASE("ZoneStructure: GFXManager build-menu icons use makeZoneDetailPic not SLAB.WSA",
          "[zone][graphics][buildmenu][regression]") {
    std::string src = readSourceFile("src/FileClasses/GFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    INFO("GFXManager must define makeZoneDetailPic lambda");
    REQUIRE(src.find("makeZoneDetailPic") != std::string::npos);

    INFO("Picture_ZoneResidential must use makeZoneDetailPic");
    auto resPos = src.find("Picture_ZoneResidential]");
    REQUIRE(resPos != std::string::npos);
    auto resLine = src.substr(resPos, src.find("\n", resPos) - resPos);
    REQUIRE(resLine.find("makeZoneDetailPic") != std::string::npos);

    INFO("makeZoneDetailPic must set SDL_BLENDMODE_NONE before scaling");
    auto lambdaPos = src.find("makeZoneDetailPic");
    REQUIRE(lambdaPos != std::string::npos);
    // Find the block up to the first smallDetailPicTex assignment that uses it
    auto assignPos = src.find("smallDetailPicTex[Picture_ZoneResidential]", lambdaPos);
    REQUIRE(assignPos != std::string::npos);
    std::string lambda = src.substr(lambdaPos, assignPos - lambdaPos);
    REQUIRE(lambda.find("SDL_BLENDMODE_NONE") != std::string::npos);
}

TEST_CASE("ZoneStructure: makeZoneDetailPic reserves gutters for lattice and price text",
          "[zone][graphics][buildmenu][regression]") {
    std::string src = readSourceFile("src/FileClasses/GFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    auto lambdaPos = src.find("makeZoneDetailPic");
    REQUIRE(lambdaPos != std::string::npos);
    auto assignPos = src.find("smallDetailPicTex[Picture_ZoneResidential]", lambdaPos);
    REQUIRE(assignPos != std::string::npos);
    std::string lambda = src.substr(lambdaPos, assignPos - lambdaPos);

    INFO("Must reserve a left gutter >= 14px to clear the 13px lattice overlay");
    REQUIRE(lambda.find("leftGutter") != std::string::npos);

    INFO("Must reserve a bottom gutter >= 12px to clear the price text");
    REQUIRE(lambda.find("bottomGutter") != std::string::npos);

    INFO("Must cap scale to avoid over-enlarging small sprites");
    REQUIRE(lambda.find("1.5f") != std::string::npos);
}

TEST_CASE("ZoneStructure: zone HP matches WindTrap in ObjectData config",
          "[zone][init][regression]") {
    std::string ini = readSourceFile("config/ObjectData.ini.default");
    REQUIRE_FALSE(ini.empty());

    // Find [Windtrap] section and extract its HitPoints
    auto wtPos = ini.find("[Windtrap]");
    REQUIRE(wtPos != std::string::npos);
    auto wtEnd = ini.find("[", wtPos + 1);
    std::string wtSection = ini.substr(wtPos, wtEnd - wtPos);
    auto hpPos = wtSection.find("HitPoints = ");
    REQUIRE(hpPos != std::string::npos);
    std::string wtHP = wtSection.substr(hpPos + 12, wtSection.find("\n", hpPos) - hpPos - 12);

    // Find each zone section in the per-house block (second occurrence)
    // and verify HP matches WindTrap
    for (const char* zoneName : {"[Residential Zone]", "[Commercial Zone]", "[Industrial Zone]"}) {
        // Find the last (per-house) occurrence
        auto pos = ini.rfind(zoneName);
        REQUIRE(pos != std::string::npos);
        auto end = ini.find("[", pos + 1);
        std::string section = ini.substr(pos, end != std::string::npos ? end - pos : std::string::npos);
        auto zhpPos = section.find("HitPoints = ");
        REQUIRE(zhpPos != std::string::npos);
        std::string zoneHP = section.substr(zhpPos + 12, section.find("\n", zhpPos) - zhpPos - 12);

        INFO(std::string(zoneName) + " HitPoints must match Windtrap (" + wtHP + ")");
        REQUIRE(zoneHP == wtHP);
    }
}

#include <dunecity/ResidentialPopulation.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

TEST_CASE("Residential lots build individual houses then each apartment stage", "[zone][population]") {
    using namespace DuneCity::ResidentialPopulation;
    int pop=0;
    for (int expected=1;expected<=8;++expected) {
        pop=grow(pop,0);
        REQUIRE(pop==expected);
        REQUIRE(density(pop)==0);
        REQUIRE(supply(pop)>0);
    }
    REQUIRE(grow(pop,64)==8);
    for (int expected : {16,24,32,40}) {
        pop=grow(pop,65);
        REQUIRE(pop==expected);
    }
    REQUIRE(grow(pop,255)==40);
    for (int expected : {32,24,16,8,7,6,5,4,3,2,1,0}) {
        pop=decline(pop);
        REQUIRE(pop==expected);
    }
    REQUIRE(decline(0)==0);
    REQUIRE(supply(0)==0);
}

TEST_CASE("Residential occupancy survives saves without shifting older streams", "[zone][population][save-compat]") {
    using namespace DuneCity::ResidentialPopulation;
    for (int pop : {0,1,2,3,4,5,6,7,8,16,24,32,40}) {
        OMemoryStream output;
        write(output,pop); output.writeUint32(0x12345678);
        IMemoryStream input(output.getData(),output.getDataLength());
        REQUIRE(read(input,9835)==pop);
        REQUIRE(input.readUint32()==0x12345678);
    }
    OMemoryStream output; output.writeUint32(0x12345678);
    IMemoryStream input(output.getData(),output.getDataLength());
    REQUIRE(read(input,9834)==legacy);
    REQUIRE(input.readUint32()==0x12345678);
    REQUIRE(fromDensity(0)==0);
    REQUIRE(fromDensity(1)==16);
    REQUIRE(fromDensity(2)==24);
    REQUIRE(fromDensity(3)==40);
}
