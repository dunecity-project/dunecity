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
 * damage crashes with one bomb's blast on the tile it falls on (crash()).  Anti-air
 * fires at it wherever it is, over its own base too.  A plane that flies off the
 * map ends without a blast.
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
    /// Bought at an Airport (Airport::buyAirplane), as opposed to the Airport's free plane.
    bool isBought() const { return bought; }
    void setBought(bool value) { bought = value; }
    /// Living free planes of `house` in the air: the Airport's free roll waits for none.
    static int freePlanes(const House* house);
    /// Flight per game cycle in quarter world units times 16 (CityAircraft::intercept).
    Coord velocity16() const;
    /// Where a turret rocket leaving `missile` (quarter world units) now meets this plane.
    DuneCity::CityAircraft::Intercept turretRocketIntercept(const Coord& missile) const;
    /// The same for any rocket type's speed (a Launcher's is faster than a turret's).
    DuneCity::CityAircraft::Intercept rocketIntercept(const Coord& missile, Uint32 bulletID) const;
    /// Where on a rocket's step from `rocketFrom` to `rocketTo` (quarter world units) it touches
    /// this plane, in 1024ths of the step, or -1 when it passes more than an eighth of a tile off.
    int touchedBy(const Coord& rocketFrom, const Coord& rocketTo) const;
    /// How far from its launch point a rocket of house `houseID` may chase a city airplane, in
    /// quarter world units (CityAircraft::kRocketChaseRangeFactor x the Rocket Turret's range).
    static int rocketChaseRange4(int houseID);
    /// How many times its WeaponRange a Rocket Turret reaches a city airplane, and its rocket
    /// chases one: CityAircraft::kTurretPlaneReachFactor (2: 16 tiles; Ornithopters keep 3). Balance
    /// experiments only: DUNECITY_TEST_PLANE_REACH_FACTOR (1 to 3) for a whole process, read
    /// once; never set in a game people play (it is not a match rule).
    static int turretReachFactor();
    /// The rocket rules of 2026-11-23 (a Launcher's rocket leads a plane and goes off on touching
    /// it; a rocket goes off on any enemy plane it touches). Always true in the game. Balance
    /// experiments only: DUNECITY_TEST_PLANE_ROCKET_HITS=0 for a whole process restores the
    /// rockets before them (Stefan: "is the rockets hitting planes too much of a nerf? test that").
    static bool rocketsHitPlanes();
    /// Balance experiments only, read once per process and never a match rule (Stefan,
    /// 2026-11-23: "test a few different things, we could increase the cost but make it stronger
    /// hp, faster, do more damage"): DUNECITY_TEST_PLANE_HP and DUNECITY_TEST_PLANE_SPEED_PCT
    /// change the city airplane's hit points and speed in the unit data of a new game. Without
    /// them applyBalance() only lifts the seeded 100 hit points to CityAircraft::kPlaneHitPoints.
    static void applyBalance(ObjectData& data);
    /// Damage of one stick bomb: CityAircraft::kStickDamage, or DUNECITY_TEST_BOMB_DAMAGE.
    static int stickDamage();
    /// True in the game: a plane that is shot down does one stick bomb's damage on the tile it
    /// falls on (crash(); Stefan, 2026-11-24, after OP.dls, where shot-down Death Hand crashes did
    /// 45% of all plane damage). Balance experiment only, read once per process and never a match
    /// rule: DUNECITY_TEST_PLANE_DEATH_HAND_CRASH=1 restores the Death Hand impact of 2026-10-22.
    static bool smallCrash();
    /// The most a stick bomb landing on `aim` (world units) would do to one building or ground
    /// unit of another team (0: nothing in its blast). `hit` receives that object.
    int bombEffectAt(const Coord& aim, const ObjectBase** hit = nullptr) const;
    /// True when a turret rocket fired now from `launch` (world units) meets this plane no
    /// farther than `range4` quarter world units from `launch`.
    bool rocketCanMeet(const Coord& launch, int range4) const;
    /// Bombs on board (CityAircraft::kBombLoad when full).
    Sint32 getBombs() const { return bombs; }
    /// Bombs released since this object was created (diagnostics and logs; not saved).
    Uint32 getBombsReleased() const { return bombsReleased; }

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
    /// Shot down: the crash at the plane (smallCrash: one bomb on its tile, or a Death Hand with the
    /// experiment switch), dealt in the name of the plane's owner; `shooterOwner` is credited the plane.
    void crash(Uint32 shooterID, House* shooterOwner, const DeviationReward::Provenance& provenance);
    /// City Plane Bombing: one bomb when the bomb sight enters a tile of an enemy building.
    void dropBomb(Uint32 gameCycle);
    /// City Plane Carpet Bombing: one game cycle of the stick (CityAircraft::kStick*).
    void dropStick(Uint32 gameCycle);
    /// The point CityAircraft::kBombSightDistance ahead on the course, in world units.
    Coord bombSight() const;
    /// City Plane Bombing Runs: aim the next leg across a random enemy building. False when
    /// no other team has a building left.
    bool pickBombingRun();
    /// Match log (diagnostics only): the first time this plane approaches and is over each enemy
    /// Airport (CITY_PLANE_NEAR_AIRPORT). Runs when the plane enters a new tile.
    void logAirportPasses();


    Sint32 takeoffFrame;    ///< Micropolis frames 11..9, 0 once cruising
    Sint32 remainingTicks;  ///< Cruise budget; zero means an outward departure
    Sint32 bombs;           ///< Aimed bombs on board; the stick does not use a rack (saved from 9890)
    Sint32 bombTimer;       ///< Sprite ticks towards the next bomb, 0 while full (saved from 9890)
    Coord  bombSightTile;   ///< Tile the bomb sight was over last cycle (saved from 9890)
    Sint32 stickGap;        ///< Bombs due since an enemy building was under the sight; above kStickTailBombs: no stick (saved from 9891)
    Sint32 stickPhase;      ///< The stick's clock towards its next bomb (saved from 9891)
    bool bought = false;        ///< bought at an Airport, not its free plane (saved from 9892)
    Uint32 bombsReleased = 0;   ///< Diagnostics only, not saved and never read by the simulation
    Coord airportCheckTile = Coord::Invalid();   ///< Diagnostics only, not saved: tile of the last logAirportPasses
    std::vector<std::pair<Uint32, Uint8>> airportsSeen;   ///< Diagnostics only, not saved: Airport id, 1 approached | 2 over
};

#endif // AMBIENTAIRPLANE_H
