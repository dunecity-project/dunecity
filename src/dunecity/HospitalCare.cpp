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

// Hospitals heal infantry: the game-side rules. The rules and their reasons are in
// include/dunecity/HospitalCare.h.

#include <dunecity/HospitalCare.h>

#include <dunecity/CitySimulation.h>
#include <Definitions.h>
#include <data.h>
#include <globals.h>
#include <Tile.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <ObjectManager.h>
#include <mmath.h>
#include <players/QuantBot.h>
#include <structures/ZoneStructure.h>
#include <units/GroundUnit.h>
#include <units/UnitOrderQueue.h>

namespace DuneCity {
namespace HospitalCare {

namespace {

HospitalCareState* state() {
    if(!currentGame || !currentGame->isCitySimEnabled()) return nullptr;
    auto* sim = currentGame->getCitySimulation();
    return sim ? &sim->hospitalCare() : nullptr;
}

ZoneStructure* zoneByID(Uint32 id) {
    return dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
}

/// A zone the soldier may keep healing at: standing, still its owner's, still Residential.
bool usableZone(const ZoneStructure* zone, const House* owner) {
    return zone && zone->getHealth() > 0 && zone->getOwner() == owner
        && zone->getZoneType() == ZoneType::Residential;
}

/// On a tile touching the footprint (diagonals included), not inside it.
bool besideZone(const ZoneStructure* zone, const Coord& at) {
    const Coord& origin = zone->getLocation();
    const int sx = zone->getStructureSizeX(), sy = zone->getStructureSizeY();
    const bool inRing = at.x >= origin.x - 1 && at.x <= origin.x + sx && at.y >= origin.y - 1 && at.y <= origin.y + sy;
    const bool inside = at.x >= origin.x && at.x < origin.x + sx && at.y >= origin.y && at.y < origin.y + sy;
    return inRing && !inside;
}

/// The tile beside the Hospital nearest the soldier, preferring tiles no other patient took.
Coord chooseSpot(const HospitalCareState& care, const ZoneStructure* zone, const Coord& from) {
    const Coord& origin = zone->getLocation();
    const int sx = zone->getStructureSizeX(), sy = zone->getStructureSizeY();
    Coord best = Coord::Invalid();
    int bestScore = 0;
    for(int y = origin.y - 1; y <= origin.y + sy; ++y) {
        for(int x = origin.x - 1; x <= origin.x + sx; ++x) {
            const Coord at(x, y);
            if(!besideZone(zone, at) || !currentGameMap->tileExists(at)) continue;
            const Tile* tile = currentGameMap->getTile(at);
            if(tile->hasANonInfantryGroundObject()) continue;
            int taken = 0;
            for(const auto& [id, t] : care.trips())
                if(t.hospitalID == zone->getObjectID() && t.spotX == x && t.spotY == y) ++taken;
            const int score = blockDistanceApprox(from, at) + 4 * taken;
            if(best.isInvalid() || score < bestScore) { best = at; bestScore = score; }
        }
    }
    return best;
}

/// The nearest own Hospital with a bed not yet claimed by kHospitalBeds soldiers; the nearest one
/// at all when every one is fully claimed. \a exclude is skipped (a waiting soldier's own).
ZoneStructure* chooseHospital(const HospitalCareState& care, const UnitBase* unit, Uint32 exclude, bool freeOnly) {
    ZoneStructure* bestFree = nullptr;
    ZoneStructure* bestAny = nullptr;
    int freeDistance = 0, anyDistance = 0;
    for(const auto& [id, overlay] : care.overlays()) {
        if(overlay != static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital) || id == exclude) continue;
        ZoneStructure* zone = zoneByID(id);
        // The saved census roster decides, not the zone's drawn overlay: a reloaded game puts
        // the overlays back only on its first city phase, after the first unit orders.
        if(!usableZone(zone, unit->getOwner())) continue;
        const int distance = blockDistanceApprox(unit->getLocation(), zone->getClosestPoint(unit->getLocation()));
        if(care.claims(id) < kHospitalBeds && (!bestFree || distance < freeDistance)) { bestFree = zone; freeDistance = distance; }
        if(!bestAny || distance < anyDistance) { bestAny = zone; anyDistance = distance; }
    }
    return bestFree ? bestFree : (freeOnly ? nullptr : bestAny);
}

/// Walk to the spot; ask for a Carryall when it is a flight away. The forced move keeps the
/// soldier from turning aside to fight on the way, as a player's move order does.
void travel(GroundUnit* unit, const Coord& spot) {
    unit->doMove2Pos(spot, true);
    if(unit->getOwner()->hasCarryalls() && blockDistance(unit->getLocation(), spot) >= MIN_CARRYALL_LIFT_DISTANCE) {
        unit->requestCarryall();
    }
}

bool dispatch(HospitalCareState& care, GroundUnit* unit, ZoneStructure* zone) {
    const Coord spot = chooseSpot(care, zone, unit->getLocation());
    if(spot.isInvalid()) return false;
    HospitalTrip trip;
    trip.hospitalID = zone->getObjectID();
    trip.spotX = static_cast<int16_t>(spot.x);
    trip.spotY = static_cast<int16_t>(spot.y);
    trip.startCycle = currentGame->getGameCycleCount();
    // Recorded before the orders: a Carryall booked by them may pick the soldier up at once.
    care.setTrip(unit->getObjectID(), trip);
    if(besideZone(zone, unit->getLocation())) {
        unit->doMove2Pos(unit->getLocation(), false);
    } else {
        travel(unit, spot);
    }
    return true;
}

/// A human house always; an AI house only when its QuantBot has HospitalInfantry on.
bool ownerSendsAutomatically(const House* owner) {
    if(!owner->isAI()) return true;
    for(const auto& player : owner->getPlayerList()) {
        const auto* bot = dynamic_cast<const QuantBot*>(player.get());
        if(bot && bot->sendsInfantryToHospital()) return true;
    }
    return false;
}

} // namespace

bool enabled() {
    return state() != nullptr && currentGame->getGameInitSettings().getGameOptions().hospitalCare;
}

bool isHospital(const StructureBase* structure) {
    const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
    return zone && zone->getHealth() > 0 && zone->getZoneType() == ZoneType::Residential
        && zone->getCivicOverlay() == ZoneStructure::CivicOverlay::Hospital;
}

bool isLockedHospital(const StructureBase* zone) {
    const auto* care = state();
    return zone && care && care->isClaimed(zone->getObjectID());
}

int patientCount(const StructureBase* zone) {
    const auto* care = state();
    return zone && care ? care->patients(zone->getObjectID()) : 0;
}

const HospitalTrip* tripOf(const UnitBase* unit) {
    const auto* care = state();
    return unit && care ? care->trip(unit->getObjectID()) : nullptr;
}

bool canBeHealed(const UnitBase* unit) {
    return unit && unit->isInfantry() && unit->getItemID() != Unit_Saboteur && unit->isEligibleForRepair()
        && unit->getHealth() > 0;
}

bool sendToHospital(GroundUnit* unit) {
    if(!enabled() || !canBeHealed(unit) || unit->getHealth() >= unit->getMaxHealth() || unit->isPickedUp()) {
        return false;
    }
    auto& care = *state();
    if(const auto* trip = care.trip(unit->getObjectID())) {
        // A Carryall's pickup and a repeated click repeat the order: the trip stands.
        if(usableZone(zoneByID(trip->hospitalID), unit->getOwner())) return true;
        care.cancel(unit->getObjectID());
    }
    ZoneStructure* zone = chooseHospital(care, unit, 0, false);
    return zone && dispatch(care, unit, zone);
}

void considerAutomatic(GroundUnit* unit) {
    if(!enabled() || !canBeHealed(unit) || unit->isPickedUp() || !unit->isActive() || unit->wasForced()
        || !UnitOrderPolicy::mayAutoRepair(unit->getAttackMode())
        || unit->getHealth() * 100 >= unit->getMaxHealth() * kHospitalAutoHealthPercent) {
        return;
    }
    const House* owner = unit->getOwner();
    if(!owner->hasCarryalls() || !ownerSendsAutomatically(owner)) return;
    auto& care = *state();
    if(care.trip(unit->getObjectID())) return;
    if(ZoneStructure* zone = chooseHospital(care, unit, 0, false)) dispatch(care, unit, zone);
}

bool keepsHospital(const ZoneStructure* zone) {
    const auto* care = state();
    return zone && care && care->isClaimed(zone->getObjectID());
}

void applyLoadedOverlays() {
    const auto* care = state();
    if(!care) return;
    for(const auto& [id, overlay] : care->overlays()) {
        if(ZoneStructure* zone = zoneByID(id)) zone->setCivicOverlay(static_cast<ZoneStructure::CivicOverlay>(overlay));
    }
}

void update() {
    auto* care = state();
    if(!care || care->trips().empty()) return;
    if(!currentGame->getGameInitSettings().getGameOptions().hospitalCare) { care->trips().clear(); return; }

    const Uint32 cycle = currentGame->getGameCycleCount();
    const bool healTick = cycle % kHospitalHealIntervalCycles == 0;
    const FixPoint cost = FixPoint(kHospitalHealCostTenths) / 10;

    // Ordered by soldier ID, so admission to a contested bed is the same on every peer.
    std::vector<Uint32> ended;
    std::vector<std::pair<Uint32, Uint32>> redirect;  // (soldier, new Hospital)
    for(auto& [unitID, trip] : care->trips()) {
        auto* unit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(unitID));
        if(!unit || unit->getHealth() <= 0 || !canBeHealed(unit)) { ended.push_back(unitID); continue; }
        ZoneStructure* zone = zoneByID(trip.hospitalID);
        if(!usableZone(zone, unit->getOwner())) { ended.push_back(unitID); continue; }
        const Coord spot(trip.spotX, trip.spotY);

        if(trip.stage == HospitalTrip::Stage::Healing) {
            // Discharged early when ordered away, lifted or chasing an enemy off the Hospital.
            if(unit->isPickedUp() || unit->isAwaitingPickup() || !besideZone(zone, unit->getLocation())
                || (unit->getDestination().isValid() && !besideZone(zone, unit->getDestination())
                    && unit->getDestination() != unit->getLocation())) {
                ended.push_back(unitID);
                continue;
            }
            if(healTick) {
                if(unit->getOwner()->takeCredits(cost) > 0) unit->addHealth();
            }
            if(unit->getHealth() >= unit->getMaxHealth()) ended.push_back(unitID);
            continue;
        }

        // Travelling or Waiting.
        if(unit->isPickedUp()) { trip.carried = true; continue; }
        if(unit->isAwaitingPickup() || unit->hasBookedCarrier()) continue;
        if(trip.carried) {
            // Just set down by the Carryall (which may drop beside the spot, not on it).
            trip.carried = false;
            trip.startCycle = cycle;
            trip.bestDistance = 0x7fff;
            if(!besideZone(zone, unit->getLocation())) { unit->doMove2Pos(spot, true); continue; }
        } else if(unit->getDestination() != spot && !besideZone(zone, unit->getLocation())) {
            ended.push_back(unitID);   // ordered somewhere else
            continue;
        }

        if(!besideZone(zone, unit->getLocation())) {
            const int distance = blockDistanceApprox(unit->getLocation(), zone->getClosestPoint(unit->getLocation()));
            if(distance < trip.bestDistance) {
                trip.bestDistance = static_cast<int16_t>(distance);
                trip.startCycle = cycle;
            } else if(cycle - trip.startCycle > kHospitalTripStallCycles) {
                ended.push_back(unitID);
            }
            continue;
        }

        // Beside the Hospital: a free bed admits it, or it waits.
        if(care->patients(trip.hospitalID) < kHospitalBeds) {
            trip.stage = HospitalTrip::Stage::Healing;
            if(unit->getDestination() != unit->getLocation()) unit->doMove2Pos(unit->getLocation(), false);
        } else {
            trip.stage = HospitalTrip::Stage::Waiting;
            if((cycle + unitID) % kHospitalRecheckCycles == 0) {
                if(ZoneStructure* other = chooseHospital(*care, unit, trip.hospitalID, true))
                    redirect.emplace_back(unitID, other->getObjectID());
            }
        }
    }
    for(Uint32 id : ended) care->cancel(id);
    for(const auto& [unitID, hospitalID] : redirect) {
        auto* unit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(unitID));
        ZoneStructure* zone = zoneByID(hospitalID);
        if(!unit || !zone) continue;
        care->cancel(unitID);
        dispatch(*care, unit, zone);
    }
}

} // namespace HospitalCare
} // namespace DuneCity
