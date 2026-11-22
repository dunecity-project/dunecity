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
    return currentGame->getGameInitSettings().getGameOptions().saboteurVehicleHunt;
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
            takeNearerTargetWhenSeen();
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
                    // One line per Saboteur for the match harnesses (tests/ai/vanilla-match.py).
                    SDL_Log("SABOTEUR_HIT: house=%d object=%u target_house=%d target_item=%d vehicle=%d price=%d cycle=%u "
                            "age_s=%d orders=%d overwrites=%d refused=%d",
                            getOwner()->getHouseID(), getObjectID(), pObject->getOwner()->getHouseID(),
                            pObject->getItemID(), pObject->isAUnit() ? 1 : 0,
                            currentGame->objectData.data[pObject->getItemID()][pObject->getProductionHouseID()].price,
                            currentGame->getGameCycleCount(), ageSeconds(releasedCycle),
                            armyOrders, armyOverwrites, armyRefused);
                    detonated = true;
                    // Sabotage destroys directly, bypassing CombatReward's damage path.
                    if (AITelemetry::log().enabled()) {
                        const int price = currentGame->objectData.data[pObject->getItemID()][pObject->getProductionHouseID()].price;
                        const int64_t hpMilli = lround(pObject->getHealth()*1000);
                        AITelemetry::log().write(currentGame->getGameCycleCount(), getOwner()->getHouseID(), -1,
                            "palace_sabotage_damage", AITelemetry::Record().set("attacker", getObjectID())
                                .set("target", pObject->getObjectID()).set("target_item", pObject->getItemID())
                                .set("hp_milli", hpMilli).set("damage_milli", hpMilli*price/std::max(1,pObject->getMaxHealth()))
                                .set("destroyed_price", price).set("kills", 1));
                    }
                    AchievementEvents::damage(pObject, getObjectID(), getOwner(), true);
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

const ObjectBase* Saboteur::findTarget() const {
    const ObjectBase* pick = findOwnTarget();
    ownPick = pick != nullptr ? pick->getObjectID() : NONE_ID;
    return pick;
}

void Saboteur::takeNearerTargetWhenSeen() {
    // A Saboteur is invisible until an enemy object is within two tiles; from then on it is shot
    // at. It does not walk on past that object to a vehicle further off: it blows up the nearest
    // thing it can. Only its own pick is replaced, never a target its owner ordered.
    if((currentGame->getGameCycleCount() + getObjectID()) % 8 != 0) {
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
    bool seen = false;
    for(int team = 0; team < NUM_TEAM_SLOTS; ++team) {
        seen = seen || (team != getOwner()->getTeamID() && isVisible(team));
    }
    if(!seen) {
        return;
    }
    const ObjectBase* nearest = InfantryBase::findTarget();    // the nearest structure or vehicle
    if(nearest != nullptr && nearest != current
       && blockDistance(location, nearest->getClosestPoint(location))
          < blockDistance(location, current->getClosestPoint(location))) {
        doAttackObject(nearest, true);
        ownPick = nearest->getObjectID();
    }
}

const ObjectBase* Saboteur::findOwnTarget() const {
    if(!keepsOwnHunt()) {
        // The rule up to 1.0.876: the nearest thing it may attack, structure or vehicle.
        return InfantryBase::findTarget();
    }

    // The search range of each attack mode, as in ObjectBase::findTarget().
    int range = 0;
    bool wholeMap = false;
    switch(attackMode) {
        case GUARD:
        case HOLD:      range = getWeaponRange();   break;
        case AREAGUARD: range = 10;                 break;
        case AMBUSH:    range = getViewRange();     break;
        case HUNT:      wholeMap = true;            break;
        default:        return InfantryBase::findTarget();
    }

    if(currentGameMap->tileExists(location)) {
        const UnitBase* nearest = nullptr;
        FixPoint nearestDistance = FixPt_MAX;
        for(const UnitBase* unit : unitList) {
            // canAttack() admits ground units that are neither infantry nor a sandworm, and structures.
            if(!unit->isActive() || !currentGameMap->tileExists(unit->getLocation()) || !canAttack(unit)) {
                continue;
            }
            const FixPoint distance = blockDistance(location, unit->getLocation());
            if(!wholeMap && distance > FixPoint(range)) {
                continue;
            }
            if(distance < nearestDistance
               || (distance == nearestDistance && nearest != nullptr
                   && unit->getObjectID() < nearest->getObjectID())) {
                nearest = unit;
                nearestDistance = distance;
            }
        }
        if(nearest != nullptr) {
            ++targetSearchStats.searches;
            return nearest;
        }
    }

    // No vehicle to go for: the general search, which leaves the nearest enemy structure.
    return InfantryBase::findTarget();
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
    Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2});
    currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

    if(isVisible(getOwner()->getTeamID())) {
        soundPlayer->playSoundAt(Sound_ExplosionLarge,location);
    }

    InfantryBase::destroy();
}
