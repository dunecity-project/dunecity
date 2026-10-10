#ifndef AMBIENTHELICOPTER_H
#define AMBIENTHELICOPTER_H

#include <units/AirUnit.h>

/**
 * The original Micropolis traffic helicopter.
 *
 * Launched by an operational city Airport, it patrols its owner's city, is
 * steered towards the heaviest friendly traffic and reports it, then goes home
 * and lands when its patrol count runs out.  Behaviour is ported from
 * Micropolis `sprite.cpp::doCopterSprite` (control < 0 branch) and
 * `traffic.cpp::addToTrafficDensityMap`'s helicopter steering; see
 * `dunecity/CityAircraftPolicy.h` for the exact references.
 *
 * It is a civilian unit: it carries no weapon, is not selectable or
 * commandable, and never attacks.  It is an ordinary air unit in every other
 * respect, so hostile anti-air can see it, shoot it and destroy it.
 */
class AmbientHelicopter final : public AirUnit
{
public:
    explicit AmbientHelicopter(House* newOwner);
    explicit AmbientHelicopter(InputStream& stream);
    void init();
    virtual ~AmbientHelicopter();

    void save(OutputStream& stream) const override;

    bool canPass(int xPos, int yPos) const override;

    bool update() override;
    FixPoint getMaxSpeed() const override;
    void turn() override;
    void deploy(const Coord& newLocation) override;

    /// The airport tile the helicopter returns to and lands at.
    void setHome(const Coord& tile);
    const Coord& getHome() const { return home; }

    /// Remaining patrol sprite ticks (Micropolis `sprite->count`).
    Sint32 getPatrolCount() const { return patrolCount; }
    /// Remaining report cooldown in sprite ticks (Micropolis `sprite->soundCount`).
    Sint32 getReportCooldown() const { return reportCooldown; }
    /// Number of heavy-traffic reports this helicopter has made.
    Uint32 getReportCount() const { return reportCount; }
    bool isReturningHome() const { return returningHome; }

private:
    /// Current position in Micropolis 1/16-tile units.
    Coord micropolisPosition() const;
    /// Destination in Micropolis 1/16-tile units.
    Coord micropolisDestination() const;
    /// traffic.cpp:185 — point the helicopter at the heaviest friendly traffic.
    void seekHeavyTraffic();
    /// Pick the next patrol waypoint over the owner's own city.
    void pickPatrolDestination();
    /// doCopterSprite's report gate, restricted to the owner's own roads.
    void reportHeavyTraffic();
    /// Land without an explosion (original: `sprite->frame = 0`).
    void land();

    Coord  home;             ///< Micropolis origX/origY, as a tile
    Sint32 patrolCount;      ///< Micropolis sprite->count, in sprite ticks
    Sint32 reportCooldown;   ///< Micropolis sprite->soundCount, in sprite ticks
    Sint32 seekCooldown;     ///< Sprite ticks until the next traffic scan
    Uint32 reportCount;      ///< Reports made; diagnostics and tests
    bool   returningHome;    ///< Patrol finished, flying back to the airport
};

#endif // AMBIENTHELICOPTER_H
