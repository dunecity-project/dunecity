/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXPLOSION_H
#define EXPLOSION_H

#include <DataTypes.h>
#include <players/DeviationReward.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/SDL2pp.h>

class Explosion
{
public:
    Explosion();
    Explosion(Uint32 explosionID, const Coord& position, int house = HOUSE_HARKONNEN);
    Explosion(Uint32 explosionID, const Coord& position, int house, Uint32 damagerID, int persistentDamage, int damageRadius,
              const DeviationReward::Provenance& provenance = DeviationReward::Provenance());
    explicit Explosion(InputStream& stream);
    ~Explosion();

    void init();

    void save(OutputStream& stream) const;

    void blitToScreen() const;

    void update();

    const Coord& getPosition() const { return position; }

private:
    Uint32 explosionID;
    Coord position;
    int house;
    zoomable_texture graphic{};
    int numFrames = 0;
    int currentFrame;
    int frameTimer;
    Uint32 damagerID = NONE_ID;
    int persistentDamage = 0;
    int damageRadius = 0;
    /// Credit for the lingering flame, taken from the projectile that started it.
    /// Serialized with the damage payload from SAVEGAMEVERSION 9849 so a flame that is
    /// still burning across a save keeps both its damage and its beneficiary.
    DeviationReward::Provenance provenance;
};


#endif // EXPLOSION_H
