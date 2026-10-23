#ifndef WATCHEDRESULTLAYOUT_H
#define WATCHEDRESULTLAYOUT_H

#include <FileClasses/FontManager.h>
#include <globals.h>

#include <string>

/// The point size of a watched game's end line (Game::drawScreen): 28, or smaller (down to 12)
/// so the line and its 18-pixel plate margins fit the \a mapWidth pixels left of the sidebar.
/// A three-house team line is wider than the map beside the sidebar at 640 and 854 pixels.
inline int watchedResultFontSize(const std::string& message, int mapWidth) {
    int size = 28;
    while(size > 12 && pFontManager->getTextWidth(message, size) + 2 * 18 > mapWidth) size -= 2;
    return size;
}

#endif // WATCHEDRESULTLAYOUT_H
