#ifndef PALACEWORMRULES_H
#define PALACEWORMRULES_H

/*
    The Fremen Palace worm (match rule GameOptionsClass::fremenPalaceWorm, default on; off is the
    Fremen warriors every Fremen Palace called up to 1.0.880). Stefan, 2026-11-27: "i want freeman
    house to get a worm as its palace special - it has full control over the worm and can eat enemy
    units with it via hunt, or can explode to create spice where the ai wants near its base".

    The rules and why (HANDOVER.md, 2026-11-27, has the measured effect):

    - Charge: the Fremen Palace keeps its 5 minutes (Palace::getSpecialWeaponCooldownForHouse), the
      cadence of the Atreides and Ordos Palaces. The worm is stronger than 12 warriors against an
      army on sand and useless against one on rock, so it is not a weapon that needs the 10-minute
      Death Hand charge.
    - One worm a house at a time (kMaxPerHouse). A second Palace that charges while the worm is out
      keeps its charge until the worm is gone. With a 3-minute life and a 5-minute charge one Palace
      never meets this limit.
    - Life: kLifetimeMs, 3 game minutes, then it dives and is gone (no spice), unless it is on its
      way to a Spice Bloom: then it bursts where it is. Long enough to cross
      a 128-tile map at its speed (about 2.5 tiles a second) and eat; short enough that a Fremen
      house has a worm out 60% of the time with one Palace, not always.
    - Meals: kMeals, 6, then it dives sated. A wild worm sleeps after 3; the Palace worm is the
      house's paid special, so twice that, and its owner may spend it on spice before the last meal.
    - Health: it leaves when half its 1000 hit points are gone, as a wild worm does; that is a loss.
    - Spice Bloom: it bursts into a spice field of radius kBloomRadius (4, the field a dying wild
      worm leaves under Killed Sandworms Drop Spice) with thick spice at the centre: about 50 sand
      tiles, roughly 5,500 spice. The burst ends the worm and is not a loss.
    - Rock: a worm never leaves sand (Sandworm::canPass). Hunt takes only units standing on sand in
      its own sand region (Sandworm::canAttack); a unit on rock or beyond a rock ridge is never a
      target. A move ordered onto rock goes as near as the sand allows; a bloom aimed at rock moves
      to the nearest sand tile the worm can reach within kBloomSnapTiles and is refused beyond that.
*/

namespace PalaceWorm {

constexpr int kLifetimeMs = 3 * 60 * 1000;
constexpr int kMeals = 6;
constexpr int kBloomRadius = 4;
constexpr int kBloomSnapTiles = 6;
constexpr int kMaxPerHouse = 1;

} // namespace PalaceWorm

#endif // PALACEWORMRULES_H
