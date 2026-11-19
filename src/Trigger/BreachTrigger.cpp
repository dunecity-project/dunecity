// SPDX-License-Identifier: GPL-2.0-or-later
#include <Trigger/BreachTrigger.h>

#include <globals.h>
#include <FileClasses/TextManager.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <units/UnitBase.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <algorithm>
#include <utility>

BreachTrigger::BreachTrigger(Uint32 triggerCycleNumber, int x, int y, int width, int height, int worms, std::string message)
    : Trigger(triggerCycleNumber), x(x), y(y), width(width), height(height), worms(worms), message(std::move(message)) {
}

BreachTrigger::BreachTrigger(InputStream& stream) : Trigger(stream) {
    x = stream.readSint32();
    y = stream.readSint32();
    width = stream.readSint32();
    height = stream.readSint32();
    worms = stream.readSint32();
    message = stream.readString();
}

void BreachTrigger::save(OutputStream& stream) const {
    Trigger::save(stream);
    stream.writeSint32(x);
    stream.writeSint32(y);
    stream.writeSint32(width);
    stream.writeSint32(height);
    stream.writeSint32(worms);
    stream.writeString(message);
}

void BreachTrigger::trigger() {
    Map* map = currentGameMap;
    if(map == nullptr || currentGame == nullptr) return;
    int opened = 0;
    for(int ty = std::max(0, y); ty < std::min(map->getSizeY(), y + height); ++ty) {
        for(int tx = std::max(0, x); tx < std::min(map->getSizeX(), x + width); ++tx) {
            Tile* tile = map->getTile(tx, ty);
            if(tile != nullptr && tile->getType() == Terrain_Mountain) {
                tile->setType(Terrain_Sand);
                ++opened;
            }
        }
    }
    map->createSandRegions();

    // Worms surface on the new sand, spread along the gap; an occupied tile is skipped.
    int placed = 0;
    const int cx = x + width / 2, cy = y + height / 2;
    for(int ring = 0; placed < worms && ring <= std::max(width, height); ++ring) {
        for(int dy = -ring; dy <= ring && placed < worms; ++dy) {
            for(int dx = -ring; dx <= ring && placed < worms; ++dx) {
                if(std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                const int tx = cx + dx, ty = cy + dy;
                const Tile* tile = map->tileExists(tx, ty) ? map->getTile(tx, ty) : nullptr;
                if(tile == nullptr || !tile->isSand() || tile->hasAnObject()) continue;
                if(currentGame->getOrCreateNativesHouse()->placeUnit(Unit_Sandworm, tx, ty, true) != nullptr) ++placed;
            }
        }
    }
    SDL_Log("BREACH at cycle %u: %d mountain tiles opened at %d,%d %dx%d, %d worms", getCycleNumber(), opened, x, y,
            width, height, placed);
    currentGame->addToNewsTicker(message.empty() ? std::string(_("The Shield Wall has been breached!")) : message);
}
