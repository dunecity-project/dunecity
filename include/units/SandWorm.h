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

#ifndef SANDWORM_H
#define SANDWORM_H

#define SANDWORM_SEGMENTS 4 //how many shimmer things will be drawn per worm

#include <units/GroundUnit.h>

class Sandworm final : public GroundUnit
{
public:
    explicit Sandworm(House* newOwner, int unitType = Unit_Sandworm);
    explicit Sandworm(InputStream& stream, int unitType = Unit_Sandworm);
    void init(int unitType);
    virtual ~Sandworm();

    void save(OutputStream& stream) const override;

    void deploy(const Coord& newLocation) override;

    void assignToMap(const Coord& pos) override;
    bool attack() override;
    void blitToScreen() override;
    void checkPos() override;

    inline void setLocation(const Coord& location) { setLocation(location.x, location.y); }
    void setLocation(int xPos, int yPos) override;

    void setTarget(const ObjectBase* newTarget) override;

    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;

    /**
        Updates this sandworm.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    bool canAttack(const ObjectBase* object) const override;
    bool canPass(int xPos, int yPos) const override;
    inline int getSleepTimer() const { return sleepTimer; }
    const Coord& getRespawnOrigin() const { return respawnOrigin; }

    int getCurrentAttackAngle() const override;

    void playAttackSound() override;

    FixPoint getTerrainDifficulty(TERRAINTYPE terrainType) const override
    {
        switch(terrainType) {
            case Terrain_Slab:          return 1.0_fix;
            case Terrain_Sand:          return 1.25_fix;
            case Terrain_Rock:          return 1.0_fix;
            case Terrain_Dunes:         return 1.25_fix;
            case Terrain_Mountain:      return 1.0_fix;
            case Terrain_Spice:         return 1.25_fix;
            case Terrain_ThickSpice:    return 1.25_fix;
            case Terrain_GreenSpice:    return 1.25_fix;
            case Terrain_ThickGreenSpice: return 1.25_fix;
            case Terrain_PaleLilacSpice:
            case Terrain_ThickPaleLilacSpice:
            case Terrain_WhiteSpice:
            case Terrain_ThickWhiteSpice: return 1.25_fix;
            case Terrain_RedSpice:      return 1.25_fix;
            case Terrain_ThickRedSpice: return 1.25_fix;
            case Terrain_SpiceBloom:    return 1.25_fix;
            case Terrain_GreenSpiceBloom: return 1.25_fix;
            case Terrain_RedSpiceBloom: return 1.25_fix;
            case Terrain_PaleLilacSpiceBloom:
            case Terrain_WhiteSpiceBloom: return 1.25_fix;
            case Terrain_SpecialBloom:  return 1.25_fix;
            default:                    return 1.0_fix;
        }
    }

    bool isEating() const { return (drawnFrame != INVALID); }

    /**
        The house whose reinforcement brought this worm (a map's [REINFORCEMENTS] line such as
        "Player1,Sandworm,Homebase,5"), or HOUSE_INVALID. The worm stays the Natives' and eats
        anyone else, but never a unit of that house's team for its lifetime: the map gave the
        worm to that side. Saved from SAVEGAMEVERSION 9873.
    */
    void setSummoner(int houseID) { summonerHouseID = houseID; }
    int getSummoner() const { return summonerHouseID; }
    /// True when this worm leaves `object` alone because it belongs to the summoner's team.
    bool spares(const ObjectBase& object) const;

    /**
        The Fremen Palace worm (match rule GameOptionsClass::fremenPalaceWorm; Stefan, 2026-11-27:
        "i want freeman house to get a worm as its palace special - it has full control over the
        worm and can eat enemy units with it via hunt, or can explode to create spice"). Unlike
        every other sandworm it belongs to the house whose Palace called it, not to the Natives:
        it takes that house's orders, never eats its team, is credited with what it eats, never
        sleeps and leaves after PalaceWorm::kLifetimeMs or PalaceWorm::kMeals meals
        (include/units/PalaceWormRules.h). Created only by createPalaceWorm().
    */
    static Sandworm* createPalaceWorm(House* owner);
    bool isPalaceWorm() const { return palaceWorm; }
    /// Game cycles before a Palace worm leaves on its own; 0 for any other worm.
    Sint32 getPalaceWormCyclesLeft() const { return palaceWormCyclesLeft; }
    Sint32 getKills() const { return kills; }
    /// Where a Spice Bloom order will burst, or invalid when the worm has none.
    const Coord& getBloomTarget() const { return bloomTarget; }
    bool hasBloomOrder() const { return bloomTarget.isValid(); }
    /**
        The Spice Bloom order: travel to (x,y), or to the nearest sand tile this worm can reach
        within PalaceWorm::kBloomSnapTiles of it, and burst there into a spice field, which ends
        the worm. A Move, Attack, Guard or Stop order cancels it.
        \return false (nothing changes) for any other worm or a spot it cannot reach
    */
    bool doSpiceBloom(int x, int y);
    void handleSpiceBloomClick(int x, int y);
    /// Game::CursorMode_SpiceBloom click: every selected Palace worm of the local player bursts at (x,y).
    static bool handleSelectedSpiceBloomClick(int x, int y);

    void doMove2Pos(int xPos, int yPos, bool bForced) override;
    void doAttackObject(const ObjectBase* pTargetObject, bool bForced) override;
    void doAttackPos(int xPos, int yPos, bool bForced) override;
    using GroundUnit::doMove2Pos;
    using GroundUnit::doAttackObject;

    /// A Palace worm that burst or left is not a loss; one beaten by the enemy is.
    bool lostWhenDestroyed() const override { return !palaceWorm || !palaceWormSpent; }

protected:
    const ObjectBase* findTarget() const override;
    void engageTarget() override;
    void sleep();
    bool sleepOrDie();

private:
    // sandworm state
    Sint32      kills;                      ///< How many units does this sandworm alreay killed?
    Sint32      attackFrameTimer;           ///< When to show the next attack frame
    Sint32      sleepTimer;                 ///< How long has this sandworm slept?
    Uint16      warningWormSignPlayedFlags; ///< Was the "Worm Sign" warning played? One bit per supported house.

    // drawing information
    Sint32 shimmerOffsetIndex;
    Coord lastLocs[SANDWORM_SEGMENTS];    ///< Last locations of the sandworm

    Sint32 summonerHouseID = HOUSE_INVALID; ///< See setSummoner().
    /// Captured once at initial Civ Wars deployment; sleep and movement never change it.
    Coord respawnOrigin = Coord::Invalid();

    // Fremen Palace worm (saved from SAVEGAMEVERSION_PALACE_WORM)
    bool   palaceWorm = false;              ///< See createPalaceWorm().
    Sint32 palaceWormCyclesLeft = 0;        ///< Cycles before it leaves on its own
    Coord  bloomTarget = Coord::Invalid();  ///< Spice Bloom order, see doSpiceBloom()
    bool   palaceWormSpent = false;         ///< Burst or left (transient: set just before destroy())

    /// Ends a Palace worm that burst or left on its own. Calls destroy(): return false from update().
    void palaceWormLeave(const char* why);
    void palaceWormBurst();
};

#endif // SANDWORM_H
