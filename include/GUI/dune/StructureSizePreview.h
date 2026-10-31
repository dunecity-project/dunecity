// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <SDL.h>
#include <algorithm>

// Repeat the source artwork's six-pixel cells instead of sampling beyond its
// three-cell lattice. DuneCity's compact sidebar uses four-pixel destination cells.
inline void drawStructureSizePreview(SDL_Renderer* target, SDL_Texture* lattice,
    SDL_Texture* concrete, int x, int y, int columns, int rows, int destinationStep = 6) {
    const auto drawCells = [&](SDL_Texture* texture, int width, int height) {
        for(int column = 0; column < width; ++column) {
            for(int row = 0; row < height; ++row) {
                SDL_Rect source{0, 0, 7, 7};
                SDL_Rect destination{x + column * destinationStep, y + row * destinationStep,
                                     destinationStep + 1, destinationStep + 1};
                SDL_RenderCopy(target, texture, &source, &destination);
            }
        }
    };
    drawCells(lattice, std::max(3, columns), std::max(3, rows));
    drawCells(concrete, columns, rows);
}
