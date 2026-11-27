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

#include <SDL2/SDL_log.h>

#include <string>

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
void travel(GroundUnit* unit, const Coord& spot, bool mayFly = true) {
    unit->doMove2Pos(spot, true);
    if(mayFly && unit->getOwner()->hasCarryalls() && blockDistance(unit->getLocation(), spot) >= MIN_CARRYALL_LIFT_DISTANCE) {
        unit->requestCarryall();
    }
}

/// One line per trip event for match logs (tests/ai/vanilla-match.py, hospital.csv).
void logEvent(const char* event, const UnitBase* unit, Uint32 unitID, Uint32 hospitalID, const char* why) {
    SDL_Log("HOSPITAL_CARE: event=%s house=%d object=%u item=%d hp=%d max_hp=%d hospital=%u why=%s cycle=%u",
            event, unit ? unit->getOwner()->getHouseID() : -1, unitID, unit ? int(unit->getItemID()) : -1,
            unit ? unit->getHealth().lround() : 0, unit ? unit->getMaxHealth() : 0, hospitalID, why,
            currentGame->getGameCycleCount());
}

/// \a mayFly false: a player's walk-in order walks, as it was given.
bool dispatch(HospitalCareState& care, GroundUnit* unit, ZoneStructure* zone, const char* why, bool mayFly = true) {
    const Coord spot = chooseSpot(care, zone, unit->getLocation());
    if(spot.isInvalid()) return false;
    HospitalTrip trip;
    trip.hospitalID = zone->getObjectID();
    trip.spotX = static_cast<int16_t>(spot.x);
    trip.spotY = static_cast<int16_t>(spot.y);
    trip.startCycle = currentGame->getGameCycleCount();
    // Recorded before the orders: a Carryall booked by them may pick the soldier up at once.
    care.setTrip(unit->getObjectID(), trip);
    logEvent("sent", unit, unit->getObjectID(), trip.hospitalID, why);
    if(besideZone(zone, unit->getLocation())) {
        unit->doMove2Pos(unit->getLocation(), false);
    } else {
        travel(unit, spot, mayFly);
    }
    return true;
}

/// Ends a trip: a soldier holding its bed gets back the mode it had (a player's order already chose
/// one), and one line for match logs.
void finishTrip(HospitalCareState& care, Uint32 id, const char* why) {
    const auto* t = care.trip(id);
    auto* left = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(id));
    if(left && t && t->priorMode != 0xff && left->getAttackMode() == HOLD && left->getHealth() > 0)
        left->doSetAttackMode(static_cast<ATTACKMODE>(t->priorMode));
    logEvent(std::string(why) == "healed" ? "healed" : "ended", left, id, t ? t->hospitalID : 0, why);
    care.cancel(id);
}

/// A Hospital in the census roster (not only drawn as one) that the house may heal at.
bool rosterHospital(const HospitalCareState& care, const ZoneStructure* zone, const House* owner) {
    if(!usableZone(zone, owner)) return false;
    const auto entry = std::make_pair(zone->getObjectID(), static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital));
    return std::find(care.overlays().begin(), care.overlays().end(), entry) != care.overlays().end();
}

/// A human house always; an AI house only when its QuantBot has HospitalInfantry on.
/// An AI house whose QuantBot has HospitalInfantry on.
bool botSends(const House* owner) {
    if(!owner->isAI()) return false;
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
    return zone && dispatch(care, unit, zone, "order");
}

void considerAutomatic(GroundUnit* unit) {
    if(!enabled() || !canBeHealed(unit) || unit->isPickedUp() || !unit->isActive() || unit->wasForced()
        || !UnitOrderPolicy::mayAutoRepair(unit->getAttackMode())
        || unit->getHealth() * 100 >= unit->getMaxHealth() * kHospitalAutoHealthPercent) {
        return;
    }
    const House* owner = unit->getOwner();
    if(!owner->hasCarryalls() || owner->isAI()) return;
    auto& care = *state();
    if(care.trip(unit->getObjectID())) return;
    if(ZoneStructure* zone = chooseHospital(care, unit, 0, false)) dispatch(care, unit, zone, "auto");
}

ZoneStructure* walkInHospital(const UnitBase* unit, const StructureBase* target, const Coord& tile, int tiles) {
    if(!enabled() || !canBeHealed(unit) || unit->getHealth() >= unit->getMaxHealth() || unit->isPickedUp()) {
        return nullptr;
    }
    const House* owner = unit->getOwner();
    if(owner->isAI() && !botSends(owner)) return nullptr;
    const auto& care = *state();
    if(target != nullptr) {
        auto* zone = dynamic_cast<ZoneStructure*>(const_cast<StructureBase*>(target));
        return rosterHospital(care, zone, owner) ? zone : nullptr;
    }
    if(tile.isInvalid()) return nullptr;
    ZoneStructure* best = nullptr;
    int bestDistance = 0;
    for(const auto& [id, overlay] : care.overlays()) {
        if(overlay != static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital)) continue;
        ZoneStructure* zone = zoneByID(id);
        if(!usableZone(zone, owner)) continue;
        // Chebyshev distance from the tile to the 2x2 footprint: 0 on it, 1 beside it.
        const Coord& o = zone->getLocation();
        const int dx = std::max({o.x - tile.x, tile.x - (o.x + zone->getStructureSizeX() - 1), 0});
        const int dy = std::max({o.y - tile.y, tile.y - (o.y + zone->getStructureSizeY() - 1), 0});
        const int distance = std::max(dx, dy);
        if(distance <= tiles && (!best || distance < bestDistance)) { best = zone; bestDistance = distance; }
    }
    return best;
}

bool walkIn(UnitBase* unitBase, ZoneStructure* zone) {
    auto* unit = dynamic_cast<GroundUnit*>(unitBase);
    auto* care = state();
    if(!unit || !zone || !care) return false;
    if(const auto* trip = care->trip(unit->getObjectID())) {
        if(trip->hospitalID == zone->getObjectID()) {
            // Ordered to the Hospital it is already going to: the walk to its tile stands, and a
            // healing soldier stays in its bed.
            if(trip->stage == HospitalTrip::Stage::Healing || besideZone(zone, unit->getLocation())) {
                unit->doMove2Pos(unit->getLocation(), false);
            } else {
                travel(unit, Coord(trip->spotX, trip->spotY), false);
            }
            return true;
        }
        finishTrip(*care, unit->getObjectID(), "reordered");
    }
    return dispatch(*care, unit, zone, "walkin", false);
}

bool replacesRepairYard(const UnitBase* unit) {
    return unit != nullptr && unit->isInfantry() && enabled();
}

RepairButton repairButtonFor(const UnitBase* unit) {
    if(!replacesRepairYard(unit)) return RepairButton::RepairYard;
    if(!canBeHealed(unit)) return RepairButton::CannotHeal;
    return chooseHospital(*state(), unit, 0, false) ? RepairButton::Hospital : RepairButton::NeedsHospital;
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

/// Every kHospitalRecheckCycles, while any house has a Hospital: the automatic rule for every
/// soldier, in unit-list order (the same on every peer).
bool inContact(const UnitBase* unit) {
    const Coord at = unit->getLocation();
    const int team = unit->getOwner()->getTeamID();
    const int ownReach = unit->getWeaponRange();
    auto armedEnemyInReach = [&](const ObjectBase* other) {
        if(!other || other->getHealth() <= 0 || !other->canAttack() || other->getOwner()->getTeamID() == team) return false;
        const int distance = blockDistanceApprox(at, other->getClosestPoint(at));
        return distance <= std::max(ownReach, other->getWeaponRange()) + 1;
    };
    for(int y = at.y - kHospitalContactScanTiles; y <= at.y + kHospitalContactScanTiles; ++y) {
        for(int x = at.x - kHospitalContactScanTiles; x <= at.x + kHospitalContactScanTiles; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            const Tile* tile = currentGameMap->getTile(x, y);
            if(armedEnemyInReach(tile->getNonInfantryGroundObject())) return true;
            for(Uint32 id : tile->getInfantryList())
                if(armedEnemyInReach(currentGame->getObjectManager().getObject(id))) return true;
            for(Uint32 id : tile->getAirUnitList())
                if(armedEnemyInReach(currentGame->getObjectManager().getObject(id))) return true;
        }
    }
    return false;
}

/// HospitalInfantry: a QuantBot soldier below kHospitalBotHealthPercent goes once it has been out
/// of contact for kHospitalQuietCycles; the clock restarts at every contact.
static void considerBotSoldier(HospitalCareState& care, GroundUnit* unit, Uint32 cycle,
                               HospitalCareState::QuietClocks& quiet) {
    if(!canBeHealed(unit) || unit->isPickedUp() || !unit->isActive() || unit->getAttackMode() == HOLD
        || unit->getHealth() * 100 >= unit->getMaxHealth() * kHospitalBotHealthPercent
        || care.trip(unit->getObjectID()) || inContact(unit)) {
        return;
    }
    const auto it = care.quietSince().find(unit->getObjectID());
    const Uint32 since = it != care.quietSince().end() ? it->second : cycle;
    if(cycle - since < kHospitalQuietCycles) {
        quiet[unit->getObjectID()] = since;
        return;
    }
    if(ZoneStructure* zone = chooseHospital(care, unit, 0, false)) dispatch(care, unit, zone, "quiet");
}

static void considerEveryone(HospitalCareState& care, Uint32 cycle) {
    if(cycle % kHospitalRecheckCycles != 0) return;
    const bool anyHospital = std::any_of(care.overlays().begin(), care.overlays().end(), [](const auto& entry) {
        return entry.second == static_cast<uint8_t>(ZoneStructure::CivicOverlay::Hospital);
    });
    if(!anyHospital) { care.setQuietSince({}); return; }
    HospitalCareState::QuietClocks quiet;
    for(UnitBase* unit : unitList) {
        if(!unit->isInfantry()) continue;
        if(botSends(unit->getOwner())) {
            considerBotSoldier(care, static_cast<GroundUnit*>(unit), cycle, quiet);
        } else if(unit->getHealth() * 100 < unit->getMaxHealth() * kHospitalAutoHealthPercent) {
            considerAutomatic(static_cast<GroundUnit*>(unit));
        }
    }
    care.setQuietSince(std::move(quiet));
}

void update() {
    auto* care = state();
    if(!care) return;
    if(!currentGame->getGameInitSettings().getGameOptions().hospitalCare) { care->trips().clear(); return; }
    const Uint32 cycle = currentGame->getGameCycleCount();
    considerEveryone(*care, cycle);
    if(care->trips().empty()) return;

    const bool healTick = cycle % kHospitalHealIntervalCycles == 0;
    const FixPoint cost = FixPoint(kHospitalHealCostTenths) / 10;

    // Ordered by soldier ID, so admission to a contested bed is the same on every peer.
    std::vector<std::pair<Uint32, const char*>> ended;  // (soldier, why)
    std::vector<std::pair<Uint32, Uint32>> redirect;  // (soldier, new Hospital)
    for(auto& [unitID, trip] : care->trips()) {
        auto* unit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(unitID));
        if(!unit || unit->getHealth() <= 0 || !canBeHealed(unit)) { ended.emplace_back(unitID, "lost"); continue; }
        ZoneStructure* zone = zoneByID(trip.hospitalID);
        if(!usableZone(zone, unit->getOwner())) { ended.emplace_back(unitID, "hospital_lost"); continue; }
        const Coord spot(trip.spotX, trip.spotY);

        if(trip.stage == HospitalTrip::Stage::Healing) {
            // Discharged early when ordered away, lifted or chasing an enemy off the Hospital.
            if(unit->isPickedUp() || unit->isAwaitingPickup() || !besideZone(zone, unit->getLocation())
                || (unit->getDestination().isValid() && !besideZone(zone, unit->getDestination())
                    && unit->getDestination() != unit->getLocation())) {
                ended.emplace_back(unitID, "left_bed");
                continue;
            }
            if(healTick) {
                if(unit->getOwner()->takeCredits(cost) > 0) unit->addHealth();
            }
            if(unit->getHealth() >= unit->getMaxHealth()) ended.emplace_back(unitID, "healed");
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
            ended.emplace_back(unitID, "reordered");
            continue;
        }

        if(!besideZone(zone, unit->getLocation())) {
            const int distance = blockDistanceApprox(unit->getLocation(), zone->getClosestPoint(unit->getLocation()));
            if(distance < trip.bestDistance) {
                trip.bestDistance = static_cast<int16_t>(distance);
                trip.startCycle = cycle;
            } else if(cycle - trip.startCycle > kHospitalTripStallCycles) {
                ended.emplace_back(unitID, "stalled");
            }
            continue;
        }

        // Beside the Hospital: a free bed admits it, or it waits.
        if(care->patients(trip.hospitalID) < kHospitalBeds) {
            trip.stage = HospitalTrip::Stage::Healing;
            if(unit->getDestination() != unit->getLocation()) unit->doMove2Pos(unit->getLocation(), false);
            // Hold the bed: fire at enemies in range, never chase off it (measured: in Guard a
            // healing soldier left its bed after a median 44 cycles, at 42% health). A Carryall
            // request left over from the walk is no mode to return to.
            const ATTACKMODE mode = unit->getAttackMode();
            trip.priorMode = static_cast<uint8_t>((mode == HOLD || mode == CARRYALLREQUESTED) ? GUARD : mode);
            unit->doSetAttackMode(HOLD);
        } else {
            trip.stage = HospitalTrip::Stage::Waiting;
            if((cycle + unitID) % kHospitalRecheckCycles == 0) {
                if(ZoneStructure* other = chooseHospital(*care, unit, trip.hospitalID, true))
                    redirect.emplace_back(unitID, other->getObjectID());
            }
        }
    }
    for(const auto& [id, why] : ended) finishTrip(*care, id, why);
    for(const auto& [unitID, hospitalID] : redirect) {
        auto* unit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(unitID));
        ZoneStructure* zone = zoneByID(hospitalID);
        if(!unit || !zone) continue;
        care->cancel(unitID);
        dispatch(*care, unit, zone, "redirect");
    }
}

} // namespace HospitalCare
} // namespace DuneCity
