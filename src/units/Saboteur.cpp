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

#include <units/Saboteur.h>
#include <Achievements/AchievementEvents.h>
#include <players/AIDecisionLog.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Map.h>
#include <Game.h>
#include <ScreenBorder.h>
#include <Explosion.h>
#include <SoundPlayer.h>
#include <structures/StructureBase.h>

#include <algorithm>


Saboteur::Saboteur(House* newOwner) : InfantryBase(newOwner)
{
    Saboteur::init();

    setHealth(getMaxHealth());

    setVisible(VIS_ALL, false);
    setVisible(getOwner()->getTeamID(), true);
    attackMode = GUARD;
}

Saboteur::Saboteur(InputStream& stream) : InfantryBase(stream)
{
    Saboteur::init();
}

void Saboteur::init()
{
    itemID = Unit_Saboteur;
    registerUnit();

    graphicID = ObjPic_Saboteur;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());

    numImagesX = 4;
    numImagesY = 3;

    numWeapons = 0;
}

Saboteur::~Saboteur() = default;

namespace {
/// Game seconds since the Palace released it, or -1 for a Saboteur that was not released here.
int ageSeconds(Uint32 releasedCycle) {
    if(releasedCycle == 0) return -1;
    return static_cast<int>((currentGame->getGameCycleCount() - releasedCycle) * GAMESPEED_DEFAULT / 1000);
}
} // namespace

bool Saboteur::keepsOwnHunt() {
    return currentGame->getGameInitSettings().getGameOptions().saboteurHunt;
}

void Saboteur::markReleased() {
    releasedCycle = std::max<Uint32>(1, currentGame->getGameCycleCount());
    loggedMode = attackMode;
}

bool Saboteur::armyOrder(const char* order, const char* caller) {
    // Its own hunt is what the rule protects. A Saboteur a scenario placed on guard, or one whose
    // hunt found nothing left to blow up, still takes its house's orders.
    const bool refused = keepsOwnHunt() && attackMode == HUNT;
    const bool busy = target || moving;
    ++armyOrders;
    if(refused) ++armyRefused;
    else if(busy) ++armyOverwrites;
    if(armyOrders <= 6) {
        // Enough to name the army code; the totals are in this Saboteur's HIT or LOST line.
        SDL_Log("SABOTEUR_ORDER: house=%d object=%u order=%s caller=%s refused=%d busy=%d mode=%d cycle=%u",
                getOwner()->getHouseID(), getObjectID(), order, caller, refused ? 1 : 0, busy ? 1 : 0,
                attackMode, currentGame->getGameCycleCount());
    }
    return refused;
}

void Saboteur::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                            const DeviationReward::Provenance& provenance) {
    if(damage > 0) {
        const ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
        lastDamagerHouse = damagerOwner != nullptr ? damagerOwner->getHouseID() : -1;
        lastDamagerItem = pDamager != nullptr ? pDamager->getItemID() : -1;
        lastDamagerKind = pDamager == nullptr ? "unknown" : pDamager->isAStructure() ? "turret"
            : pDamager->isAFlyingUnit() ? "aircraft" : pDamager->isInfantry() ? "infantry" : "vehicle";
    }
    InfantryBase::handleDamage(damage, damagerID, damagerOwner, provenance);
}


void Saboteur::checkPos()
{
    InfantryBase::checkPos();

    if(active) {
        bool canBeSeen[NUM_TEAM_SLOTS];
        for(int i = 0; i < NUM_TEAM_SLOTS; i++) {
            canBeSeen[i] = false;
        }

        for(int x = location.x - 2; (x <= location.x + 2); x++) {
            for(int y = location.y - 2; (y <= location.y + 2); y++) {
                if(currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->hasAnObject()) {
                    canBeSeen[currentGameMap->getTile(x, y)->getObject()->getOwner()->getTeamID()] = true;
                }
            }
        }

        for(int i = 0; i < NUM_TEAM_SLOTS; i++) {
            setVisible(i, canBeSeen[i]);
        }

        setVisible(getOwner()->getTeamID(), true);    //owner team can always see it
        //setVisible(pLocalHouse->getTeamID(), true);
    }
}

bool Saboteur::update() {
    if(active && keepsOwnHunt()) {
        if(attackMode == HUNT) {
            reconsiderOwnPick();
        } else if(attackMode == GUARD && releasedCycle != 0 && getOwner()->isAI() && !target && !moving
                  && (currentGame->getGameCycleCount() + getObjectID()) % MILLI2CYCLES(10*1000) == 0) {
            // An AI house's released Saboteur that dropped out of its hunt (a carryall that never
            // came, nothing left to blow up at the time) looks again every ten seconds.
            doSetAttackMode(HUNT);
        }
    }
    if(active && releasedCycle != 0 && attackMode != loggedMode) {
        if(modeChanges++ < 6) {
            SDL_Log("SABOTEUR_MODE: house=%d object=%u from=%d to=%d age_s=%d cycle=%u", getOwner()->getHouseID(),
                    getObjectID(), loggedMode, attackMode, ageSeconds(releasedCycle), currentGame->getGameCycleCount());
        }
        loggedMode = attackMode;
    }

    if(active && releasedCycle != 0 && target.getObjectID() != loggedTarget) {
        // What it goes for, each time that changes (the first eight), for the match logs.
        loggedTarget = target.getObjectID();
        if(targetChanges++ < 8) {
            const ObjectBase* pTarget = target.getObjPointer();
            SDL_Log("SABOTEUR_TARGET: house=%d object=%u at=%d,%d target_house=%d target_item=%d target_at=%d,%d "
                    "dist=%d own=%d age_s=%d cycle=%u", getOwner()->getHouseID(), getObjectID(), location.x, location.y,
                    pTarget != nullptr ? pTarget->getOwner()->getHouseID() : -1,
                    pTarget != nullptr ? pTarget->getItemID() : -1,
                    pTarget != nullptr ? pTarget->getLocation().x : -1, pTarget != nullptr ? pTarget->getLocation().y : -1,
                    pTarget != nullptr ? blockDistance(location, pTarget->getClosestPoint(location)).lround() : -1,
                    pTarget != nullptr && pTarget->getObjectID() == ownPick ? 1 : 0,
                    ageSeconds(releasedCycle), currentGame->getGameCycleCount());
        }
    }

    if(active && target.getObjPointer() != nullptr) {
        Coord closestPoint = target.getObjPointer()->getClosestPoint(location);
        FixPoint dist = blockDistance(location, closestPoint);
        
        // Log saboteur state every 50 cycles when close to target
        static int logCounter = 0;
        if(dist <= 3.0_fix && (++logCounter % 50) == 0) {
            SDL_Log("SABOTEUR at (%d,%d): moving=%d target=%d dist=%.2f attackMode=%d destination=(%d,%d)", 
                location.x, location.y, moving, target.getObjPointer()->getItemID(), 
                dist.toDouble(), attackMode, destination.x, destination.y);
        }
        
        const bool enemyTarget = getOwner()->getTeamID() != target.getObjPointer()->getOwner()->getTeamID();
        if(keepsOwnHunt() && enemyTarget && dist <= 4
           && (currentGame->getGameCycleCount() + getObjectID()) % 8 == 0) {
            showTargetToOwner(closestPoint);
        }

        // Up to 1.0.876 it blew up only once it had stopped next to its target, and most lost
        // Saboteurs were shot by that target during the last step. With the rule on it goes off
        // as soon as it is in reach.
        if(!moving || keepsOwnHunt()) {
            //check to see if close enough to blow up target
            if(getOwner()->getTeamID() != target.getObjPointer()->getOwner()->getTeamID()) {
                if(blockDistance(location, closestPoint) <= 1.5_fix) {
                    if(isVisible(getOwner()->getTeamID())) {
                        screenborder->shakeScreen(18);
                    }

                    ObjectBase* pObject = target.getObjPointer();
                    if(keepsOwnHunt()) {
                        showTargetToOwner(closestPoint);    // its owner sees what went up
                    }
                    // One line per Saboteur for the match harnesses (tests/ai/vanilla-match.py).
                    SDL_Log("SABOTEUR_HIT: house=%d object=%u target_house=%d target_item=%d vehicle=%d price=%d cycle=%u "
                            "age_s=%d orders=%d overwrites=%d refused=%d",
                            getOwner()->getHouseID(), getObjectID(), pObject->getOwner()->getHouseID(),
                            pObject->getItemID(), pObject->isAUnit() ? 1 : 0,
                            currentGame->objectData.data[pObject->getItemID()][pObject->getProductionHouseID()].price,
                            currentGame->getGameCycleCount(), ageSeconds(releasedCycle),
                            armyOrders, armyOverwrites, armyRefused);
                    detonated = true;
                    if (AITelemetry::log().enabled()) {
                        const int price = currentGame->objectData.data[pObject->getItemID()][pObject->getProductionHouseID()].price;
                        const int64_t hpMilli = lround(pObject->getHealth()*1000);
                        AITelemetry::log().write(currentGame->getGameCycleCount(), getOwner()->getHouseID(), -1,
                            "palace_sabotage_damage", AITelemetry::Record().set("attacker", getObjectID())
                                .set("target", pObject->getObjectID()).set("target_item", pObject->getItemID())
                                .set("hp_milli", hpMilli).set("damage_milli", hpMilli*price/std::max(1,pObject->getMaxHealth()))
                                .set("destroyed_price", price).set("kills", 1));
                    }
                    // The charge is a hit like any other: its lethal damage goes through the normal
                    // damage path while this Saboteur still exists, so the kill and the hit points
                    // are credited to the Saboteur (Stats screen, the house's kill count, the AI's
                    // record of what it destroyed, achievements). Up to 1.0.880 the target was
                    // destroyed directly and a Saboteur was never credited with anything.
                    pObject->handleDamage(pObject->getHealth().lround() + 1, getObjectID(), getOwner());
                    destroy();
                    pObject->setHealth(0);
                    pObject->destroy();
                    return false;
                }
            }
        }
    }
    
    return InfantryBase::update();
}

void Saboteur::deploy(const Coord& newLocation) {
    UnitBase::deploy(newLocation);

    setVisible(VIS_ALL, false);
    setVisible(getOwner()->getTeamID(), true);
}


bool Saboteur::canAttack(const ObjectBase* object) const {
    if(object != nullptr){
        if((object->isAStructure() || (object->isAGroundUnit() && !object->isInfantry() && object->getItemID() != Unit_Sandworm)) /* allow attack tanks*/
            && (object->getOwner()->getTeamID() != owner->getTeamID())
            && object->isVisible(getOwner()->getTeamID())){

            return true;
        }

    }

    return false;


}

int Saboteur::sabotageValue(int itemID) {
    // Dune II's worth of a building to a Saboteur: priorityBuild + priorityTarget of its
    // structure table (Dune Dynasty src/table/structureinfo.c). Buildings Dune II did not have
    // take the value of the nearest Dune II counterpart. Walls and parks are worth nothing here:
    // they are left to the general search, which takes them last.
    switch(itemID) {
        case Structure_HeavyFactory:
        case Structure_RepairYard:          return 600;
        case Structure_Palace:              return 400;
        case Structure_ConstructionYard:
        case Structure_WindTrap:
        case Structure_Refinery:            return 300;
        case Structure_Radar:               return 275;    // Outpost
        case Structure_StarPort:            return 250;
        case Structure_GunTurret:           return 225;    // Turret 75 + 150
        case Structure_LightFactory:
        case Structure_HighTechFactory:     return 200;
        case Structure_RocketTurret:        return 175;    // R-Turret 100 + 75
        case Structure_WOR:                 return 175;
        case Structure_Silo:                return 150;
        case Structure_Barracks:
        case Structure_IX:                  return 100;
        // DuneCity
        case Structure_NuclearPlant:        return 300;    // power, as a Wind Trap
        case Structure_Airport:             return 250;    // as a Starport
        case Structure_ResearchLab:         return 200;    // as a Hi-Tech
        case Structure_PoliceStation:
        case Structure_Stadium:             return 100;
        case Structure_ZoneResidential:
        case Structure_ZoneCommercial:
        case Structure_ZoneIndustrial:      return 50;
        // Tornie family
        case Structure_AdvancedWindTrap:
        case Structure_AdvancedWindTrapMK2:
        case Structure_AdvancedWindTrapMK3:
        case Structure_Worfinery:
        case Structure_Doublefinery:        return 300;
        case Structure_TechCenter:
        case Structure_LoveFactory:
        case Structure_ChaosFactory:        return 200;
        case Structure_Scoutpost:
        case Structure_Flamepost:
        case Structure_Chemipost:
        case Structure_Slowpost:            return 225;    // as a Turret
        case Structure_Wall:
        case Structure_Park:                return 0;
        default:                            return 100;
    }
}

int Saboteur::vehicleValue(int itemID) {
    // Dune II's worth of a unit as a target (priorityBuild + priorityTarget of its unit table),
    // on the same scale as sabotageValue(). Vehicles Dune II did not have take the value of the
    // nearest Dune II counterpart.
    switch(itemID) {
        case Unit_Devastator:           return 355;
        case Unit_SiegeTank:
        case Unit_EliteSiegeTank:
        case Unit_ChemicalSiegeTank:    return 280;
        case Unit_Launcher:
        case Unit_EliteLauncher:        return 250;
        case Unit_Deviator:             return 225;
        case Unit_SonicTank:            return 190;
        case Unit_Tank:
        case Unit_FlameTank:
        case Unit_RangedTank:
        case Unit_SlowTank:             return 180;
        case Unit_Harvester:
        case Unit_RebelHarvester:
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_MCV:                  return 160;
        case Unit_Quad:
        case Unit_FlameQuad:
        case Unit_RaiderQuad:           return 120;
        case Unit_RaiderTrike:
        case Unit_RocketTrike:
        case Unit_SonicTrike:           return 115;
        case Unit_Trike:                return 100;
        default:                        return 150;
    }
}

const ObjectBase* Saboteur::bestVehicleNearby() const {
    // Enemy vehicles it meets on the way: those within kVehicleReach tiles, scored like a
    // building (value / distance). Any farther would be a chase after something that drives.
    const ObjectBase* best = nullptr;
    FixPoint bestScore = 0;
    for(int y = location.y - kVehicleReach; y <= location.y + kVehicleReach; ++y) {
        for(int x = location.x - kVehicleReach; x <= location.x + kVehicleReach; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            const Tile* tile = currentGameMap->getTile(x, y);
            if(!tile->hasANonInfantryGroundObject()) continue;
            const ObjectBase* object = tile->getNonInfantryGroundObject();
            if(object == nullptr || !object->isAUnit() || !object->isActive() || !canAttack(object)) continue;
            const FixPoint distance = blockDistance(location, object->getClosestPoint(location));
            if(distance > kVehicleReach) continue;
            const FixPoint score = FixPoint(vehicleValue(object->getItemID())) / std::max(FixPoint(1), distance);
            if(best == nullptr || score > bestScore
               || (score == bestScore && object->getObjectID() < best->getObjectID())) {
                best = object;
                bestScore = score;
            }
        }
    }
    return best;
}

const StructureBase* Saboteur::bestBuilding() const {
    // Dune II's sabotage script (UNIT.EMC, action Sabotage) asks FindBestTarget in mode 4:
    // buildings first, anywhere on the map, only those its house has seen, the one with the
    // highest value / distance (Unit_FindBestTargetStructure, Unit_GetTargetStructurePriority);
    // units only when no building is left. Lower object ID on a tie.
    const StructureBase* best = nullptr;
    FixPoint bestScore = 0;
    for(const StructureBase* structure : structureList) {
        const int value = sabotageValue(structure->getItemID());
        if(value <= 0 || !structure->isActive() || !canAttack(structure)) continue;
        const FixPoint distance = std::max(FixPoint(1), blockDistance(location, structure->getClosestPoint(location)));
        const FixPoint score = FixPoint(value) / distance;
        if(best == nullptr || score > bestScore
           || (score == bestScore && structure->getObjectID() < best->getObjectID())) {
            best = structure;
            bestScore = score;
        }
    }
    return best;
}

FixPoint Saboteur::sabotageScore(const ObjectBase* object) const {
    const int value = object->isAStructure() ? sabotageValue(object->getItemID()) : vehicleValue(object->getItemID());
    return FixPoint(value) / std::max(FixPoint(1), blockDistance(location, object->getClosestPoint(location)));
}

const ObjectBase* Saboteur::findTarget() const {
    // Hunting (as every Palace releases it) it goes for enemy buildings the way Dune II's
    // Saboteur does: the best one for its value and distance, anywhere its house has seen
    // (bestBuilding). Stefan, 2026-11-27: "make it like dynasty but allow them to also blow up
    // vehicles". So an enemy vehicle it meets on the way (within kVehicleReach tiles) competes
    // on the same scale, value / distance, and wins only when it is near enough and worth
    // enough: with a Heavy Factory (600) four tiles off (150), a Tank (180) beside it (180) is
    // taken, a Trike (100) two tiles off (50) is not. A vehicle that drives out of reach is
    // dropped for the building again, so it never chases. With no building left it takes the
    // nearest vehicle anywhere.
    // Outside Hunt (a guard mode its owner set) it takes the nearest thing canAttack()
    // accepts, walls and parks last. Up to 1.0.880 it took whatever was nearest, so most went
    // after vehicles that drove off or died under fire on the way.
    // The pick is remembered so that only it, never a target the owner ordered, is replaced
    // on the way (reconsiderOwnPick).
    const ObjectBase* pick = nullptr;
    if(attackMode == HUNT) {
        pick = bestBuilding();
        if(pick != nullptr) {
            const ObjectBase* vehicle = bestVehicleNearby();
            if(vehicle != nullptr && sabotageScore(vehicle) > sabotageScore(pick)) pick = vehicle;
        }
    }
    if(pick == nullptr) pick = InfantryBase::findTarget();
    ownPick = pick != nullptr ? pick->getObjectID() : NONE_ID;
    return pick;
}

void Saboteur::showTargetToOwner(const Coord& targetTile) {
    // A Saboteur sees one tile around it and goes off half a step before it stands next to its
    // target: without this its owner got 0.4 s to see what it blew up, on ground never explored
    // nothing at all, and the Saboteur seemed to blow up by itself.
    currentGameMap->viewMap(getOwner()->getHouseID(), targetTile, 2);
}

bool Saboteur::fallsWhenDestroyed() const {
    return keepsOwnHunt() && !detonated;
}

void Saboteur::reconsiderOwnPick() {
    // On the way it asks its own search again from where it stands, a few times a second: a
    // vehicle it meets that is worth more for its distance, a building it walked up to or one
    // put down since replaces its pick. Only its own pick is replaced, never a target its owner
    // ordered.
    if((currentGame->getGameCycleCount() + getObjectID()) % kReconsiderCycles != 0) {
        return;
    }
    const ObjectBase* current = target.getObjPointer();
    if(current == nullptr) {
        // Its target is gone (destroyed by someone else, lifted by a carryall, driven into a
        // refinery). The general rule only looks for the next one after the walk to where the
        // old one stood, usually among its escorts. A Saboteur on its own hunt looks at once;
        // one walking where its owner sent it (a forced move, a hunt-toward goal) does not.
        if(!target && !attackPos && !forced && moving && !isHuntingToward()) {
            if(const ObjectBase* next = findTarget()) {
                doAttackObject(next, true);
            }
        }
        return;
    }
    if(target.getObjectID() != ownPick) {
        return;
    }
    const Uint32 currentID = current->getObjectID();
    const ObjectBase* best = findTarget();    // sets ownPick
    if(best == nullptr) {
        ownPick = currentID;
    } else if(best != current) {
        doAttackObject(best, true);
    }
}

void Saboteur::destroy()
{
    if(!detonated) {
        // Shot, squashed or otherwise gone without having sabotaged anything. What killed it and
        // how far it still had to go tell an interrupted hunt from a walk through enemy fire.
        const ObjectBase* pTarget = target.getObjPointer();
        const bool onMap = currentGameMap->tileExists(location);
        const char* killer = lastDamagerKind;
        if(onMap && currentGameMap->getTile(location)->hasANonInfantryGroundObject()
           && currentGameMap->getTile(location)->getNonInfantryGroundObject()->isAUnit()) {
            killer = "squashed";
        }
        SDL_Log("SABOTEUR_LOST: house=%d object=%u cycle=%u age_s=%d mode=%d killer=%s killer_item=%d killer_house=%d "
                "target_item=%d target_dist=%d orders=%d overwrites=%d refused=%d",
                getOwner()->getHouseID(), getObjectID(), currentGame->getGameCycleCount(), ageSeconds(releasedCycle),
                attackMode, killer, lastDamagerItem, lastDamagerHouse,
                pTarget != nullptr ? pTarget->getItemID() : -1,
                pTarget != nullptr && onMap ? blockDistance(location, pTarget->getClosestPoint(location)).lround() : -1,
                armyOrders, armyOverwrites, armyRefused);
    }

    Coord realPos(lround(realX), lround(realY));
    // Drawn either way, so that the random stream does not depend on how a Saboteur ended.
    Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2});
    // With the rule on only the charge going off is a blast. A Saboteur that was shot blew up in
    // the same way, which looked as if it had sabotaged nothing at all; it now falls like any
    // foot soldier (InfantryBase::destroy).
    if(!fallsWhenDestroyed()) {
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

        if(isVisible(getOwner()->getTeamID())) {
            soundPlayer->playSoundAt(Sound_ExplosionLarge,location);
        }
    }

    InfantryBase::destroy();
}
