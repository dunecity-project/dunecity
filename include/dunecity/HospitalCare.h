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

#ifndef DUNECITY_HOSPITALCARE_H
#define DUNECITY_HOSPITALCARE_H

// Hospitals heal infantry (Stefan, 2026-11-27: "i want troopers and infantry to be able to be
// healed / repaied via a hospital R zone - carryalls can also pick them up in dune city. a single
// hispital can heal up to 5 men at a time. while a hospital is healing me or while a unit is
// walking to get healed or being carryalled to be healed it cannot change type from hospital to
// something else").
//
// A Hospital is an own Residential zone the city census marked with the Hospital civic overlay
// (CitySimulation::runZoneGrowth). A wounded soldier sent for repair (CMD_UNIT_SENDTOREPAIR, or
// the engine's automatic rule) walks - or is flown by a Carryall - to a free tile beside it and
// heals standing there. Up to kHospitalBeds soldiers heal at once; the rest wait beside it.
//
// Every trip (walking, carried, waiting, healing) is a claim on its Hospital, and a claimed
// Hospital keeps its overlay through every census: it cannot turn into an ordinary Residential
// zone or a Church while a soldier depends on it (isLockedHospital). The claim ends when the
// soldier is healed, dies, is ordered elsewhere, changes owner, or the zone is lost.
//
// The trips and the census overlays are saved in the CitySimulation stream
// (SAVEGAMEVERSION_HOSPITAL_CARE), so a reloaded game heals, locks and dispatches exactly as the
// uninterrupted one. HospitalCareState is plain data (unit tested); the game-side rules that read
// units and structures are the HospitalCare functions below (src/dunecity/HospitalCare.cpp).

#include <DataTypes.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

class GroundUnit;
class House;
class StructureBase;
class UnitBase;
class ZoneStructure;

namespace DuneCity {

/// A Hospital heals at most this many soldiers at a time.
constexpr int kHospitalBeds = 5;
/// One hit point per patient every this many game cycles (a quarter of a Repair Yard's rate for a
/// single vehicle; five beds make a Hospital 1.25 Repair Yards of throughput).
constexpr int kHospitalHealIntervalCycles = 4;
/// Credits per hit point, in tenths: 0.2 credits. A Soldier (20 HP, 60 credits) costs 4 credits
/// from near death, about 6.7% of its price, as a Quad's full repair is 6.5% of its price.
constexpr int kHospitalHealCostTenths = 2;
/// The automatic rule sends a soldier below half health, as a Repair Yard's rule does vehicles.
constexpr int kHospitalAutoHealthPercent = 50;
/// The automatic rule and a waiting soldier look again every this many cycles.
constexpr int kHospitalRecheckCycles = 32;
/// QuantBot (HospitalInfantry): a soldier below this share of its health goes to a Hospital...
constexpr int kHospitalBotHealthPercent = 60;
/// ...once no armed enemy has had it in reach for this many cycles (8 s at the default speed).
/// The first measurement sent soldiers the moment they were hit: 637 of 891 trips a game ended
/// with the soldier dead on the way, a median 169 cycles after it was sent.
constexpr uint32_t kHospitalQuietCycles = 500;
/// The contact scan looks this many tiles round the soldier (the longest weapon reach in play).
constexpr int kHospitalContactScanTiles = 9;
/// A walk that has come no closer to its Hospital for this many cycles (one game minute at the
/// default speed) is abandoned: the route is blocked. A Soldier needs about 300 cycles a tile.
constexpr uint32_t kHospitalTripStallCycles = 3750;

/// One soldier's claim on a Hospital.
struct HospitalTrip {
    enum class Stage : uint8_t { Travelling = 0, Waiting = 1, Healing = 2 };

    uint32_t hospitalID = 0;
    int16_t spotX = -1, spotY = -1;   ///< The tile beside the Hospital it walks or is flown to.
    Stage stage = Stage::Travelling;
    bool carried = false;            ///< Seen inside a Carryall since the last walking order.
    uint32_t startCycle = 0;         ///< Cycle of the dispatch or of the last step closer.
    int16_t bestDistance = 0x7fff;   ///< Closest the walk has come, in tiles (stall detection).
    /// The attack mode a healing soldier had before it held its bed (HOLD while healing, so it fires
    /// from the bed but does not chase off it); restored when the trip ends. 0xff: not healing yet.
    uint8_t priorMode = 0xff;

    bool operator==(const HospitalTrip& o) const {
        return hospitalID == o.hospitalID && spotX == o.spotX && spotY == o.spotY && stage == o.stage
            && carried == o.carried && startCycle == o.startCycle && bestDistance == o.bestDistance
            && priorMode == o.priorMode;
    }
};

/// Plain, saved state: the trips by soldier object ID and the census overlays by zone object ID.
class HospitalCareState {
public:
    using Trips = std::map<uint32_t, HospitalTrip>;
    using Overlays = std::vector<std::pair<uint32_t, uint8_t>>;  ///< (zone ID, civic overlay)

    const Trips& trips() const { return trips_; }
    Trips& trips() { return trips_; }
    const HospitalTrip* trip(uint32_t unitID) const {
        const auto it = trips_.find(unitID);
        return it == trips_.end() ? nullptr : &it->second;
    }
    void setTrip(uint32_t unitID, const HospitalTrip& trip) { trips_[unitID] = trip; }
    void cancel(uint32_t unitID) { trips_.erase(unitID); }

    /// Trips of any stage on this Hospital: walking, carried, waiting or healing.
    int claims(uint32_t hospitalID) const {
        int n = 0;
        for(const auto& [id, t] : trips_) if(t.hospitalID == hospitalID) ++n;
        return n;
    }
    /// Soldiers healing in this Hospital's beds.
    int patients(uint32_t hospitalID) const {
        int n = 0;
        for(const auto& [id, t] : trips_)
            if(t.hospitalID == hospitalID && t.stage == HospitalTrip::Stage::Healing) ++n;
        return n;
    }
    bool isClaimed(uint32_t hospitalID) const {
        for(const auto& [id, t] : trips_) if(t.hospitalID == hospitalID) return true;
        return false;
    }
    bool bedFree(uint32_t hospitalID) const { return patients(hospitalID) < kHospitalBeds; }

    const Overlays& overlays() const { return overlays_; }
    void setOverlays(Overlays overlays) { overlays_ = std::move(overlays); }

    /// QuantBot soldiers below kHospitalBotHealthPercent with no enemy in reach: the cycle each was
    /// first seen out of contact (HospitalInfantry's quiet rule). Rebuilt on every recheck.
    using QuietClocks = std::map<uint32_t, uint32_t>;
    const QuietClocks& quietSince() const { return quietSince_; }
    void setQuietSince(QuietClocks clocks) { quietSince_ = std::move(clocks); }

    void clear() { trips_.clear(); overlays_.clear(); quietSince_.clear(); }

    void save(OutputStream& stream) const {
        stream.writeUint32(static_cast<Uint32>(overlays_.size()));
        for(const auto& [zone, overlay] : overlays_) {
            stream.writeUint32(zone);
            stream.writeUint8(overlay);
        }
        stream.writeUint32(static_cast<Uint32>(trips_.size()));
        for(const auto& [unit, t] : trips_) {
            stream.writeUint32(unit);
            stream.writeUint32(t.hospitalID);
            stream.writeSint16(t.spotX);
            stream.writeSint16(t.spotY);
            stream.writeUint8(static_cast<Uint8>(t.stage));
            stream.writeBool(t.carried);
            stream.writeUint32(t.startCycle);
            stream.writeSint16(t.bestDistance);
            stream.writeUint8(t.priorMode);
        }
        stream.writeUint32(static_cast<Uint32>(quietSince_.size()));
        for(const auto& [unit, since] : quietSince_) {
            stream.writeUint32(unit);
            stream.writeUint32(since);
        }
    }

    void load(InputStream& stream) {
        clear();
        const Uint32 overlayCount = stream.readUint32();
        stream.requireReadableElements(overlayCount, 5);
        for(Uint32 i = 0; i < overlayCount; ++i) {
            const Uint32 zone = stream.readUint32();
            const Uint8 overlay = stream.readUint8();
            if(overlay > 2) throw std::runtime_error("Invalid civic overlay.");
            overlays_.emplace_back(zone, overlay);
        }
        const Uint32 tripCount = stream.readUint32();
        stream.requireReadableElements(tripCount, 21);
        for(Uint32 i = 0; i < tripCount; ++i) {
            const Uint32 unit = stream.readUint32();
            HospitalTrip t;
            t.hospitalID = stream.readUint32();
            t.spotX = stream.readSint16();
            t.spotY = stream.readSint16();
            const Uint8 stage = stream.readUint8();
            if(stage > 2) throw std::runtime_error("Invalid hospital trip stage.");
            t.stage = static_cast<HospitalTrip::Stage>(stage);
            t.carried = stream.readBool();
            t.startCycle = stream.readUint32();
            t.bestDistance = stream.readSint16();
            t.priorMode = stream.readUint8();
            trips_[unit] = t;
        }
        const Uint32 quietCount = stream.readUint32();
        stream.requireReadableElements(quietCount, 8);
        for(Uint32 i = 0; i < quietCount; ++i) {
            const Uint32 unit = stream.readUint32();
            quietSince_[unit] = stream.readUint32();
        }
    }

private:
    Trips trips_;
    Overlays overlays_;
    QuietClocks quietSince_;
};

/// Game-side rules (src/dunecity/HospitalCare.cpp). All of them are no-ops outside a city game or
/// with the match rule Hospital Care off.
namespace HospitalCare {

/// The match rule (Game Options "Hospital Care", default on) in a running city game.
bool enabled();

/// An own, standing Residential zone with the Hospital overlay.
bool isHospital(const StructureBase* structure);

/// True while any soldier heals at, waits at, walks to or is carried to this Hospital. A locked
/// Hospital keeps its overlay; redevelopment must not demolish or retype it.
bool isLockedHospital(const StructureBase* zone);

/// Soldiers healing at this Hospital now (0..kHospitalBeds).
int patientCount(const StructureBase* zone);

/// The soldier's Hospital trip, or nullptr.
const HospitalTrip* tripOf(const UnitBase* unit);

/// True when \a unit is infantry that a Hospital may heal (not a Saboteur, not deviated).
bool canBeHealed(const UnitBase* unit);

/// The explicit repair order for infantry. Sends a wounded soldier to the nearest own Hospital
/// with a free bed (the nearest one at all when every bed is claimed), asking for a Carryall when
/// it is far. Returns false when there is no Hospital, so the caller can fall back to a Repair
/// Yard. A soldier already on a trip keeps it (idempotent: a Carryall's pickup repeats the order).
bool sendToHospital(GroundUnit* unit);

/// The automatic rule, checked by update() every kHospitalRecheckCycles for every soldier while
/// its house has a Hospital (InfantryBase::checkPos does not run GroundUnit's): a human house's
/// soldier below half health, if the house has Carryalls, as for vehicles and Repair Yards.
void considerAutomatic(GroundUnit* unit);

/// An armed enemy (a unit or structure that can attack) has \a unit within its weapon reach, or
/// is within the soldier's own: the soldier is in contact.
bool inContact(const UnitBase* unit);

/// One game cycle: validate every trip, admit arrivals, heal and discharge. Called by Game after
/// the city simulation phase.
void update();

/// The census's designation pass: true when this zone must stay a Hospital.
bool keepsHospital(const ZoneStructure* zone);

/// After loading: put the saved census overlays back on their zones.
void applyLoadedOverlays();

} // namespace HospitalCare

} // namespace DuneCity

#endif // DUNECITY_HOSPITALCARE_H
