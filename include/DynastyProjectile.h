// SPDX-License-Identifier: GPL-2.0-or-later
// Normal-speed projectile geometry from Dune Dynasty 4469449c (OpenDUNE).
// Coordinates here are 256/tile; DuneCity world coordinates are 64/tile.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <DataTypes.h>
#include <data.h>
namespace DynastyProjectile {
inline constexpr int deathHandDamage = 200;
/// Dynasty's seventeen Death Hand blast centres around the impact, in world units
/// (256 -> 64 coordinates). Bullet::detonateDeathHand() damages around each.
inline constexpr int deathHandBlastCentres = 17;
inline constexpr int deathHandBlastX[deathHandBlastCentres] =
    {0, 0, 50, 64, 50, 0, -50, -64, -50, 0, 100, 128, 100, 0, -100, -128, -100};
inline constexpr int deathHandBlastY[deathHandBlastCentres] =
    {0, -64, -50, 0, 50, 64, 50, 0, -50, -128, -100, 0, 100, 128, 100, 0, -100};
struct Parameters { int step, turn, delay; };
inline Parameters parameters(int id) {
    switch(id) {
        case Bullet_Rocket: return {192, 8, 8};
        case Bullet_DRocket: return {192, 8, 7};
        case Bullet_TurretRocket: return {144, 32, 60};
        case Bullet_SmallRocket: return {176, 20, 3};
        case Bullet_LargeRocket: return {240, 8, 15};
        default: return {0, 0, 0};
    }
}
inline int distance(Coord a, Coord b) {
    int dx=std::abs(a.x-b.x), dy=std::abs(a.y-b.y);
    return std::max(dx,dy)+std::min(dx,dy)/2;
}
inline constexpr int k_orientationOffsets[4] = {
	0x40, 0x80, 0x00, 0xC0
};
inline constexpr int k_gradients[32] = {
	0x3FFF, 0x28BC, 0x145A, 0x0D8E, 0x0A27, 0x081B, 0x06BD, 0x05C3,
	0x0506, 0x0474, 0x03FE, 0x039D, 0x034B, 0x0306, 0x02CB, 0x0297,
	0x026A, 0x0241, 0x021D, 0x01FC, 0x01DE, 0x01C3, 0x01AB, 0x0194,
	0x017F, 0x016B, 0x0159, 0x0148, 0x0137, 0x0128, 0x011A, 0x010C
};
inline constexpr int k_stepX[256] = {
	   0,    3,    6,    9,   12,   15,   18,   21,   24,   27,   30,   33,   36,   39,   42,   45,
	  48,   51,   54,   57,   59,   62,   65,   67,   70,   73,   75,   78,   80,   82,   85,   87,
	  89,   91,   94,   96,   98,  100,  101,  103,  105,  107,  108,  110,  111,  113,  114,  116,
	 117,  118,  119,  120,  121,  122,  123,  123,  124,  125,  125,  126,  126,  126,  126,  126,
	 127,  126,  126,  126,  126,  126,  125,  125,  124,  123,  123,  122,  121,  120,  119,  118,
	 117,  116,  114,  113,  112,  110,  108,  107,  105,  103,  102,  100,   98,   96,   94,   91,
	  89,   87,   85,   82,   80,   78,   75,   73,   70,   67,   65,   62,   59,   57,   54,   51,
	  48,   45,   42,   39,   36,   33,   30,   27,   24,   21,   18,   15,   12,    9,    6,    3,
	   0,   -3,   -6,   -9,  -12,  -15,  -18,  -21,  -24,  -27,  -30,  -33,  -36,  -39,  -42,  -45,
	 -48,  -51,  -54,  -57,  -59,  -62,  -65,  -67,  -70,  -73,  -75,  -78,  -80,  -82,  -85,  -87,
	 -89,  -91,  -94,  -96,  -98, -100, -102, -103, -105, -107, -108, -110, -111, -113, -114, -116,
	-117, -118, -119, -120, -121, -122, -123, -123, -124, -125, -125, -126, -126, -126, -126, -126,
	-126, -126, -126, -126, -126, -126, -125, -125, -124, -123, -123, -122, -121, -120, -119, -118,
	-117, -116, -114, -113, -112, -110, -108, -107, -105, -103, -102, -100,  -98,  -96,  -94,  -91,
	 -89,  -87,  -85,  -82,  -80,  -78,  -75,  -73,  -70,  -67,  -65,  -62,  -59,  -57,  -54,  -51,
	 -48,  -45,  -42,  -39,  -36,  -33,  -30,  -27,  -24,  -21,  -18,  -15,  -12,   -9,   -6,   -3
};
inline constexpr int k_stepY[256] = {
	 127,  126,  126,  126,  126,  126,  125,  125,  124,  123,  123,  122,  121,  120,  119,  118,
	 117,  116,  114,  113,  112,  110,  108,  107,  105,  103,  102,  100,   98,   96,   94,   91,
	  89,   87,   85,   82,   80,   78,   75,   73,   70,   67,   65,   62,   59,   57,   54,   51,
	  48,   45,   42,   39,   36,   33,   30,   27,   24,   21,   18,   15,   12,    9,    6,    3,
	   0,   -3,   -6,   -9,  -12,  -15,  -18,  -21,  -24,  -27,  -30,  -33,  -36,  -39,  -42,  -45,
	 -48,  -51,  -54,  -57,  -59,  -62,  -65,  -67,  -70,  -73,  -75,  -78,  -80,  -82,  -85,  -87,
	 -89,  -91,  -94,  -96,  -98, -100, -102, -103, -105, -107, -108, -110, -111, -113, -114, -116,
	-117, -118, -119, -120, -121, -122, -123, -123, -124, -125, -125, -126, -126, -126, -126, -126,
	-126, -126, -126, -126, -126, -126, -125, -125, -124, -123, -123, -122, -121, -120, -119, -118,
	-117, -116, -114, -113, -112, -110, -108, -107, -105, -103, -102, -100,  -98,  -96,  -94,  -91,
	 -89,  -87,  -85,  -82,  -80,  -78,  -75,  -73,  -70,  -67,  -65,  -62,  -59,  -57,  -54,  -51,
	 -48,  -45,  -42,  -39,  -36,  -33,  -30,  -27,  -24,  -21,  -18,  -15,  -12,   -9,   -6,   -3,
	   0,    3,    6,    9,   12,   15,   18,   21,   24,   27,   30,   33,   36,   39,   42,   45,
	  48,   51,   54,   57,   59,   62,   65,   67,   70,   73,   75,   78,   80,   82,   85,   87,
	  89,   91,   94,   96,   98,  100,  101,  103,  105,  107,  108,  110,  111,  113,  114,  116,
	 117,  118,  119,  120,  121,  122,  123,  123,  124,  125,  125,  126,  126,  126,  126,  126
};
inline int direction(Coord from, Coord to)
{
	int quadrant = 0;   /* SE, SW, NE, NW. */
	int dx = to.x - from.x;
	int dy = to.y - from.y;
	int gradient;
	unsigned int i;

	if (abs(dx) + abs(dy) > 8000) {
		dx /= 2;
		dy /= 2;
	}

	if (dy <= 0) {
		quadrant |= 0x2;
		dy = -dy;
	}

	if (dx < 0) {
		quadrant |= 0x1;
		dx = -dx;
	}

	if (dx >= dy) {
		gradient = (dy != 0) ? ((dx << 8) / dy) : 0x7FFF;
	} else {
		gradient = (dx != 0) ? ((dy << 8) / dx) : 0x7FFF;
	}

	for (i = 0; i < 32; i++) {
		if (k_gradients[i] <= gradient)
			break;
	}

	if (dx >= dy)
		i = 64 - i;

	if (quadrant == 0 || quadrant == 3) {
		return k_orientationOffsets[quadrant] + 64 - i;
	} else {
		return k_orientationOffsets[quadrant] + i;
	}
}

inline Coord move(Coord p, int direction, int distance) {
    // Dynasty enhanced movement uses symmetric signed rounding.
    int dx=k_stepX[direction&255], dy=k_stepY[direction&255];
    return {p.x+(dx*distance+(dx<0?-64:64))/128,
            p.y-(dy*distance+(dy<0?-64:64))/128};
}
} // namespace DynastyProjectile
