#include <catch2/catch_test_macros.hpp>
#include <ScreenBorder.h>
#include <misc/ZoomSteps.h>

#include <cstdlib>

namespace {
struct RestoreZoom {
    int previous = currentZoomlevel;
    ~RestoreZoom() { currentZoomlevel = previous; }
};
}

TEST_CASE("Zoom steps on a sprite level draw directly; steps between them scale a larger level down", "[zoom]") {
    for(int level = 0; level < 3; ++level) {
        const auto render = ZoomSteps::renderFor(ZoomSteps::stepForLevel(level), 800, 600, 8192);
        REQUIRE(render);
        CHECK(render->spriteLevel == level);
        CHECK(render->boardWidth == 800);
        CHECK(render->boardHeight == 600);
    }

    // 125% draws the 2x sprites on a board 1.6x the map area, scaled down onto it.
    const auto between = ZoomSteps::renderFor(2, 800, 600, 8192);
    REQUIRE(ZoomSteps::kScales[2] == 1.25f);
    REQUIRE(between);
    CHECK(between->spriteLevel == 1);
    CHECK(between->boardWidth == 1280);
    CHECK(between->boardHeight == 960);

    // Past 3x there is no larger level: the 3x sprites are scaled up.
    const auto closest = ZoomSteps::renderFor(ZoomSteps::kCount - 1, 800, 600, 8192);
    REQUIRE(closest);
    CHECK(closest->spriteLevel == 2);
    CHECK(closest->boardWidth == 600);
}

TEST_CASE("A zoom step whose board exceeds the texture limit falls back to a smaller level, or is unavailable", "[zoom]") {
    // 1280 px does not fit a 1024 px limit, so 125% scales the 1x sprites up instead.
    const auto fallback = ZoomSteps::renderFor(2, 800, 600, 1024);
    REQUIRE(fallback);
    CHECK(fallback->spriteLevel == 0);
    CHECK(fallback->boardWidth == 640);
    CHECK(fallback->boardHeight == 480);

    // 75% needs a board larger than the screen whatever the level.
    CHECK_FALSE(ZoomSteps::renderFor(0, 800, 600, 1000));
    CHECK(ZoomSteps::renderFor(0, 800, 600, 1100));
}

TEST_CASE("On an offscreen zoom board, screen pixels map to the world point drawn there", "[zoom]") {
    RestoreZoom restore;
    const SDL_Rect area{0, 32, 800, 600};
    const auto render = ZoomSteps::renderFor(2, area.w, area.h, 8192);   // 125%
    REQUIRE(render);
    currentZoomlevel = render->spriteLevel;
    ScreenBorder border(area);
    border.setDrawingBoardSize(render->boardWidth, render->boardHeight);
    REQUIRE(border.drawsOffscreen());
    border.adjustScreenBorderToMapsize(128, 128);
    border.setNewScreenCenter({4096, 4096});

    // The map area shows 800 px / 1.25 of the original 16 px-per-tile graphics.
    CHECK(border.getRight() - border.getLeft() == 800 * TILESIZE / 16 * 4 / 5);

    for(const int worldX : {3000, 4096, 5100}) {
        const int worldY = 4096 + (worldX - 4096) / 2;   // the view spans y 3136..5056
        // Where the drawing board puts the point, then where that lands on the screen.
        const int boardX = border.world2screenX(worldX);
        const int boardY = border.world2screenY(worldY);
        const int screenX = area.x + boardX * area.w / render->boardWidth;
        const int screenY = area.y + boardY * area.h / render->boardHeight;
        INFO(worldX << " drawn at board " << boardX << "," << boardY << ", screen " << screenX << "," << screenY);
        CHECK(border.isScreenCoordInsideMap(screenX, screenY));
        // One screen pixel covers 3.2 world units at 125%.
        CHECK(std::abs(border.screen2worldX(screenX) - worldX) <= 4);
        CHECK(std::abs(border.screen2worldY(screenY) - worldY) <= 4);
    }

    CHECK_FALSE(border.isScreenCoordInsideMap(area.x + area.w + 5, 300));   // sidebar
    CHECK_FALSE(border.isScreenCoordInsideMap(300, area.y - 5));            // top bar

    border.setDrawingBoardSize(area.w, area.h);
    CHECK_FALSE(border.drawsOffscreen());
    CHECK(border.getDrawingRect().y == area.y);
}
