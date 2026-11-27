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

#ifndef ZONESTRUCTURE_H
#define ZONESTRUCTURE_H

#include <structures/StructureBase.h>
#include <dunecity/CityConstants.h>
#include <FileClasses/GFXManager.h>

#include <vector>

// Forward declarations
class House;
class Game;
class UnitBase;

/// A base class for city zone structures (Residential, Commercial, Industrial)
class ZoneStructure : public StructureBase {
public:
    explicit ZoneStructure(House* newOwner, DuneCity::ZoneType zoneType);
    explicit ZoneStructure(InputStream& stream);
    virtual ~ZoneStructure() override;

    void save(OutputStream& stream) const override;

    void blitToScreen() override;
    /// Draw the same live zone/civic sprite used on the map into a UI rectangle.
    void drawPreview(const SDL_Rect& bounds) const;

    void setLocation(int xPos, int yPos) override;

    /// Enables placement on sand tiles
    bool canBePlacedAt(int x, int y, bool torch = false) const;

    DuneCity::ZoneType getZoneType() const { return zoneType_; }

    /// Civic overlay: Hospital or Church auto-placed by the game on residential zones.
    enum class CivicOverlay : uint8_t { None = 0, Hospital, Church };
    CivicOverlay getCivicOverlay() const { return civicOverlay_; }
    void setCivicOverlay(CivicOverlay overlay) { civicOverlay_ = overlay; }

    int getResidentialPopulation() const;
    void setResidentialPopulation(int population);
    /// Pre-developed lot from a map's optional zone level (INIMap/ZoneStartLevel.h):
    /// density @a level 0..3; a residential lot gets the matching apartment population.
    void setStartingLevel(int level);

    ObjectInterface* getInterfaceContainer() override;

    void destroy() override;
    void demolish() override; ///< Clear the selected lot without combat explosions or refund.
    void clearZoneState(); ///< Shared tile/power cleanup before removal.

    /// Recompute zone power from current tile density and apply the delta to
    /// the owner's House::powerRequirement. Idempotent; safe to call from
    /// setLocation, density-change hooks, and load.
    void refreshZonePowerDraw();
    int getZonePowerDraw() const { return registeredZonePower_; }
    /// City growth, animation and status share the same house-grid power test.
    bool hasCityPower() const;

    /// Update curAnimFrame from current tile density (column in the atlas)
    /// and the sampled land-value tier (row). Called every tick so the
    /// sprite tracks growth and changes in neighbourhood quality.
    void updateStructureSpecificStuff() override;

    // --- Zone Garrisons (docs/zone-garrisons.md; ZoneStructureGarrison.cpp) ---
    /// The match rule is on for this game.
    static bool garrisonsEnabled();
    bool isGarrisoned() const { return !garrison_.empty(); }
    int getGarrisonSize() const { return static_cast<int>(garrison_.size()); }
    /// Occupants in the order they entered; the first takes the cover damage.
    const std::vector<Uint32>& getGarrison() const { return garrison_; }
    /// The house whose soldiers are inside, or nullptr when empty.
    const House* getGarrisonHouse() const;
    /// May UNIT walk in now (own zone with room, or an enemy zone nobody holds)?
    bool mayEnterGarrison(const UnitBase* unit) const;
    /// Enemy soldiers hold this zone against UNIT: a capture order fires on it instead.
    bool heldAgainst(const UnitBase* unit) const;
    /// Puts UNIT inside; an enemy zone becomes UNIT's house's. False when refused.
    bool enterGarrison(UnitBase* unit);
    /// Deploys the occupants beside the zone. Returns how many came out. DESTROYING kills
    /// those with no free tile around; otherwise they stay inside.
    int evacuateGarrison(bool destroying = false);
    /// Hands the zone, its tiles, power draw and counts to NEWOWNER; city state stays. CAPTURE
    /// false is a captured zone going back to its original owner (no capture achievement).
    void transferOwnership(House* newOwner, bool capture = true);
    /// The house that owned this zone before its present garrison took it, or -1 when it is the
    /// holder's own: once that garrison is gone, by order, at low health or killed, it goes back.
    int getGarrisonOriginalOwner() const { return garrisonOriginalOwner_; }
    /// The share of hit points below which the garrison leaves and nobody may enter (percent).
    static int evacuateHealthPercent();
    /// Too damaged to hold: below evacuateHealthPercent() of its hit points.
    bool tooDamagedToHold() const;

    void handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                      const DeviationReward::Provenance& provenance = DeviationReward::Provenance()) override;

private:
    void drawZoneImage();
    void updateGarrison();
    void dropDeadOccupants();
    /// A captured zone whose garrison is gone returns to its original owner, if that house lives.
    void returnCapturedZone();
    const ObjectBase* findGarrisonTarget(int range) const;
    void drawGarrisonSandbags() const;

    std::vector<Uint32> garrison_;          ///< saved from SAVEGAMEVERSION_ZONE_GARRISON
    Uint32 garrisonTargetID_ = NONE_ID;     ///< saved
    Sint32 garrisonSearchTimer_ = 0;        ///< saved
    Sint32 garrisonOriginalOwner_ = -1;     ///< saved from SAVEGAMEVERSION_ZONE_GARRISON_RETURN

    DuneCity::ZoneType zoneType_;  // The type of zone this structure represents
    uint8_t residentialPopulation_ = 0;
    int registeredZonePower_ = 0;  // Power last reported into the House pool.
    CivicOverlay civicOverlay_ = CivicOverlay::None;
    int skinDensity_ = 0;
    int skinValueTier_ = 0;
};

/// A residential zone structure
class ResidentialZone : public ZoneStructure {
public:
    explicit ResidentialZone(House* newOwner);
    explicit ResidentialZone(InputStream& stream);
    virtual ~ResidentialZone() override = default;
private:
    void init();
};

/// A commercial zone structure
class CommercialZone : public ZoneStructure {
public:
    explicit CommercialZone(House* newOwner);
    explicit CommercialZone(InputStream& stream);
    virtual ~CommercialZone() override = default;
private:
    void init();
};

/// An industrial zone structure
class IndustrialZone : public ZoneStructure {
public:
    explicit IndustrialZone(House* newOwner);
    explicit IndustrialZone(InputStream& stream);
    virtual ~IndustrialZone() override = default;
private:
    void init();
};

#endif // ZONESTRUCTURE_H
