#ifndef AMBIENTAIRPLANE_H
#define AMBIENTAIRPLANE_H

#include <units/AirUnit.h>

/**
 * The original Micropolis airplane.
 *
 * Launched by an operational city Airport, it runs the three-frame take-off
 * sequence, then cruises between destinations across the map and its outer
 * margin, picking a new one each time it arrives, until it leaves. Behaviour
 * is ported from Micropolis `sprite.cpp::doAirplaneSprite` and the
 * `SPRITE_AIRPLANE` case of `newSprite`; see `dunecity/CityAircraftPolicy.h`
 * for the exact references.  It is not a patrol: it never seeks traffic and
 * never returns to its airport.
 *
 * It is a civilian unit: no weapon, not selectable or commandable, never
 * attacks.  It is an ordinary air unit in every other respect, so hostile
 * anti-air can see it, shoot it and destroy it.
 *
 * The original's airplane-to-aircraft collision explosions are deliberately
 * not ported: they are part of Micropolis's disaster system, and DuneCity
 * already has a way for these aircraft to die.
 */
class AmbientAirplane final : public AirUnit
{
public:
    explicit AmbientAirplane(House* newOwner);
    explicit AmbientAirplane(InputStream& stream);
    void init();
    virtual ~AmbientAirplane();

    void save(OutputStream& stream) const override;

    bool canPass(int xPos, int yPos) const override;

    bool update() override;
    FixPoint getMaxSpeed() const override;
    void turn() override;
    void deploy(const Coord& newLocation) override;

    /// Start the original take-off run (frames 11 -> 10 -> 9 -> cruise east).
    void beginTakeoff();
    /// Start already cruising west, as the original does for an airport close
    /// to the eastern map edge.
    void beginWestboundCruise();

    /// Current take-off frame (11, 10, 9) or 0 once cruising.
    Sint32 getTakeoffFrame() const { return takeoffFrame; }
    /// Remaining cruise ticks before the plane begins its outward departure.
    Sint32 getRemainingTicks() const { return remainingTicks; }
    /// True once the flight budget is spent and the plane is heading off the map.
    bool isDeparting() const { return remainingTicks <= 0; }

private:
    Coord micropolisPosition() const;
    Coord micropolisDestination() const;
    /// Accept a waypoint outside the map, which ObjectBase::setDestination
    /// discards. Airplane-only; every other unit keeps the map-tile validation.
    void setFlightDestination(const Coord& waypoint);
    /// doAirplaneSprite: `destX = getRandom((WORLD_W * 16) + 100) - 50`.
    void pickDestination();
    /// Route out through the nearest edge once the flight budget is spent, so a
    /// flight still ends by crossing the boundary and never by disappearing.
    void beginDeparture();
    /// Leave the map without an explosion (original: `sprite->frame = 0`).
    void leaveMap();

    Sint32 takeoffFrame;    ///< Micropolis frames 11..9, 0 once cruising
    Sint32 remainingTicks;  ///< Cruise budget; zero means an outward departure
};

#endif // AMBIENTAIRPLANE_H
