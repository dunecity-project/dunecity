#ifndef AIRPORT_H
#define AIRPORT_H

#include <structures/StructureBase.h>
#include <dunecity/AirPatrolCycle.h>

/**
 * Airport — DuneCity economic building.
 *
 * 4x4 footprint (Stefan, 2026-11-23: "make an airport 4x4 squares not 3x3 you can look at the
 * original unskrunken sprite from micropolis"), powered Micropolis animation. Provides
 * commercial/transport boost to the city economy. Maps to CityRole::Commercial so it generates
 * jobs and tax revenue, similar to SimCity Classic's airport zone.
 *
 * A compact Airport keeps the 3x3 footprint the Airport had before: one loaded from a save older
 * than SAVEGAMEVERSION_AIRPORT_4X4 (its tiles in that save hold a 3x3 claim), and one on a
 * scenario map whose neighbours leave no room for the fourth row and column (INIMapLoader::
 * loadStructures decides). It is drawn from the same 64 px sprite cell, scaled into its three
 * tiles (StructureBase::blitToScreen). Every Airport a player, an AI or the map editor places
 * is 4x4.
 */
class Airport final : public StructureBase
{
public:
    explicit Airport(House* newOwner);
    explicit Airport(InputStream& stream);
    virtual ~Airport();
    void save(OutputStream& stream) const override;
    int getMaxSpawnTimer() const;
    int getSpawnTimer() const { return patrol.remainingCycles; }
    int getPendingAircraft() const { return patrol.pendingAircraft; }
    ObjectInterface* getInterfaceContainer() override;
    /// City Plane Purchase: this Airport may launch a plane for CityAircraft::kPlanePrice now
    /// (rule on, city game, powered, cooled down, the house below its plane limit and able to pay).
    bool canBuyAirplane() const;
    /// Pays and launches; false, and nothing changes, unless canBuyAirplane().
    bool buyAirplane();
    /// The local player's Buy button: CMD_AIRPORT_BUY_AIRPLANE.
    void handleBuyAirplaneClick();
    /// Game cycles until this Airport may sell its next plane.
    int getPurchaseCooldown() const { return purchaseCooldown; }
    /// True for a 3x3 Airport (see the class comment), false for the ordinary 4x4 one.
    bool isCompact() const { return compact; }

    /**
        While one of these lives, the next Airport created through House::placeStructure is
        compact (3x3). Only INIMapLoader::loadStructures makes one, around the single
        placeStructure call of an Airport line that has no room to be 4x4; this keeps
        placeStructure's widely used signature as it is. The destructor clears the hint, so it
        cannot outlive that call, also when the call throws.
    */
    class CompactPlacement {
    public:
        CompactPlacement() { nextIsCompact = true; }
        ~CompactPlacement() { nextIsCompact = false; }
        CompactPlacement(const CompactPlacement&) = delete;
        CompactPlacement& operator=(const CompactPlacement&) = delete;
    };
    /// Footprint of the Airport the next House::placeStructure call creates: 4x4, or 3x3
    /// under a CompactPlacement.
    static Coord nextPlacementSize() { return nextIsCompact ? Coord(3, 3) : Coord(4, 4); }

private:
    DuneCity::AirPatrolCycle patrol{getMaxSpawnTimer()};
    Sint32 purchaseCooldown = 0;   ///< Clock started at each purchase; nothing waits on it (saved from 9891)
    bool compact = false;          ///< 3x3 instead of 4x4 (saved from SAVEGAMEVERSION_AIRPORT_4X4)
    static inline bool nextIsCompact = false;   ///< Set only by a living CompactPlacement
    /// Puts a city airplane on the runway. Null when the unit could not be created.
    class AmbientAirplane* launchAirplane(bool bought);
    void init();
    /// Micropolis doAirport(): launch the city airplane or traffic helicopter.
    void updateCityAircraft();
    void updateStructureSpecificStuff() override;
};

#endif // AIRPORT_H
