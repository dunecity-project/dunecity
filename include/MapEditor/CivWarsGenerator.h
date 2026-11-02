/*
 * This file is part of Dune Legacy, licensed under the GNU GPL version 2 or later.
 */
#ifndef CIVWARSGENERATOR_H
#define CIVWARSGENERATOR_H

#include <MapEditor/SkirmishGenerator.h>

#include <array>
#include <cstdint>
#include <string>

class INIFile;

/**
 * Pure, integer-only Civilization Wars map generation. The catalogue is supplied by
 * the caller, just as for Random Skirmish; generation never reads engine globals.
 *
 * Three vehicle lanes cross the sand between two all-rock team ends. Mountain
 * ridges stop at those ends, so teammates can move between lanes behind the front.
 * Seats are ordered top-to-bottom on the left, then their horizontal mirrors on
 * the right. A 1v1 seat owns all three mouths; in 2v2 the upper seat owns the top
 * and middle mouths, the lower seat the bottom; in 3v3 each seat owns one mouth.
 * Each mouth has two rocket turrets, in addition to each seat's two core turrets.
 */
namespace CivWarsGenerator {

// Odd widths put an odd lane's unpaired worm on the integer centre column.
inline const std::array<Coord, 4> kMapSizes{{{129, 96}, {161, 112}, {193, 128}, {255, 160}}};

struct Params {
    int teamSize = 1;
    int size = 0;                 ///< Index into kMapSizes.
    int wormsPerLane = 1;
    SkirmishGenerator::Spice spice = SkirmishGenerator::Spice::Medium;
    uint32_t seed = 1;
    bool fixedSeed = false;       ///< Menu only; generation always uses seed verbatim.
    int house = HOUSE_INVALID;
    std::string allyClass = "qBotMedium";
    std::string enemyClass = "qBotMedium";
    bool spectate = false;
    std::string spectateClass = "qBotMedium";
    bool multiplayer = false;

    void clamp(int houseCount);
    /// Last-used parameters in the configuration's "Civilization Wars" section.
    void load(const INIFile& config);
    void save(INIFile& config) const;
};

/**
 * Fixed tech-1, zero-credit DuneCity starts: Yard, Research Lab, Refinery (its
 * loader supplies the harvester), Carryall, three mature R lots, one C and one I,
 * police, road access and windtraps with at least ten percent spare power.
 * Building footprints, foundations, roads, aircraft and worm origins mirror
 * exactly. The caller enables the Research Lab and respawning-sandworm options.
 * Every serialized map carries the exact [CIVILIZATIONWARS] Version=1 marker.
 * Throws std::invalid_argument if the supplied catalogue cannot provide the kit.
 */
SkirmishGenerator::Result generate(const Params& params, const SkirmishGenerator::Catalogue& catalogue);

} // namespace CivWarsGenerator

#endif // CIVWARSGENERATOR_H
