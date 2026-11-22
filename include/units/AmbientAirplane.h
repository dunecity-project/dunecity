#ifndef AMBIENTAIRPLANE_H
#define AMBIENTAIRPLANE_H

#include <units/AirUnit.h>
#include <dunecity/CityAircraftPolicy.h>

#include <vector>

/**
 * The original Micropolis airplane.
 *
 * Launched by an operational city Airport, it runs the three-frame take-off
 * sequence, then cruises between destinations across the map and its outer
 * margin, picking a new one each time it arrives, until it leaves. Behaviour
 * is ported from Micropolis `sprite.cpp::doAirplaneSprite` and the
 * `SPRITE_AIRPLANE` case of `newSprite`; see `dunecity/CityAircraftPolicy.h`
 * for the exact references.  It is not a patrol: it never seeks traffic and
 * never returns to its airport.  Unlike the original it flies straight legs
 * (CityAircraft::airplaneSteer) and is drawn from sixteen headings.
 *
 * It is not selectable or commandable and never chooses a target.  With the
 * match rule City Plane Bombing (default on) it drops bombs on enemy buildings
 * that lie under its course (dropBomb, CityAircraft::kBomb*); it never leaves
 * its course for one.  It is an ordinary air unit in every other respect, so hostile
 * anti-air can see it, shoot it and destroy it - and a plane brought down by
 * damage crashes with the effect of a Death Hand impact (crash()).  Anti-air
 * holds its fire while that blast would land on its own side
 * (crashEndangers()).  A plane that flies off the map ends without a blast.
 *
 * The original's airplane-to-aircraft collision explosions are deliberately
 * not ported: they are part of Micropolis's disaster system.
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
    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;

    /**
        Anti-air rule for the canAttack() of every weapon that can hit aircraft:
        true when `target` is a city airplane and shooting it down would set its
        crash blast off over `attacker`'s own team - where the plane is now, or
        where it will be by the time a missile fired now reaches it. Natives
        objects (worms, crime troopers) are nobody's own team, not even the
        Natives' own: a blast over them never holds anyone's fire.

        With the match rule City Plane Defence (default on) only the shot that
        could bring the plane down is held: one that, with the shots already on
        their way to it, could take its last hit points. A Rocket Turret, whose
        rocket flies to the meeting point, is then only held while the crash at
        that point would land on its own side.
    */
    static bool crashEndangers(const ObjectBase& attacker, const ObjectBase& target);

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

    /// Every city airplane of the running game, in no particular order. Not saved: a
    /// plane enters in its constructor and leaves in its destructor.
    static const std::vector<AmbientAirplane*>& all();
    /// Flight per game cycle in quarter world units times 16 (CityAircraft::intercept).
    Coord velocity16() const;
    /// Where a turret rocket leaving `missile` (quarter world units) now meets this plane.
    DuneCity::CityAircraft::Intercept turretRocketIntercept(const Coord& missile) const;
    /// Bombs on board (CityAircraft::kBombLoad when full).
    Sint32 getBombs() const { return bombs; }

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
    /// Draw the cruise heading: the nearest of the sixteen sheet headings.
    void showHeading();
    /// Shot down: a Death Hand impact at the plane, credited to `shooterOwner`.
    void crash(Uint32 shooterID, House* shooterOwner, const DeviationReward::Provenance& provenance);
    /// True when the crash blast anywhere between `from` and `to` world units
    /// ahead on the plane's course would reach an object of team `teamID`.
    bool crashWouldHit(int teamID, FixPoint from, FixPoint to) const;
    /// City Plane Bombing: one bomb on the enemy building under the bomb sight, if any.
    void dropBomb(Uint32 gameCycle);

    Sint32 takeoffFrame;    ///< Micropolis frames 11..9, 0 once cruising
    Sint32 remainingTicks;  ///< Cruise budget; zero means an outward departure
    Sint32 bombs;           ///< Bombs on board (saved from 9890)
    Sint32 bombTimer;       ///< Sprite ticks towards the next bomb, 0 while full (saved from 9890)
};

#endif // AMBIENTAIRPLANE_H
