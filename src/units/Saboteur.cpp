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
#include <misc/draw_util.h>
#include <structures/ZoneStructure.h>
#include <mmath.h>
#include <sand.h>


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

bool Saboteur::skipsEmptyZones() {
    return currentGame->getGameInitSettings().getGameOptions().saboteurSkipsEmptyZones;
}

namespace {
bool isZone(const ObjectBase* object) {
    const int item = object->getItemID();
    return item == Structure_ZoneResidential || item == Structure_ZoneCommercial || item == Structure_ZoneIndustrial;
}

/// Soldiers inside a zone, or -1 for anything that is not a zone (for the match log).
int garrisonOf(const ObjectBase* object) {
    if(object == nullptr || !isZone(object)) return -1;
    const auto* zone = dynamic_cast<const ZoneStructure*>(object);
    return zone != nullptr ? zone->getGarrisonSize() : 0;
}
} // namespace

bool Saboteur::passesOver(const ObjectBase* object) {
    return object != nullptr && skipsEmptyZones() && garrisonOf(object) == 0;
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

    if(active && releasedCycle != 0 && target.getObjectID() != loggedTarget) {
        // What it goes for, each time that changes (the first eight), for the match logs.
        loggedTarget = target.getObjectID();
        if(targetChanges++ < 8) {
            const ObjectBase* pTarget = target.getObjPointer();
            SDL_Log("SABOTEUR_TARGET: house=%d object=%u at=%d,%d target_house=%d target_item=%d target_at=%d,%d "
                    "dist=%d own=%d age_s=%d cycle=%u garrison=%d", getOwner()->getHouseID(), getObjectID(), location.x, location.y,
                    pTarget != nullptr ? pTarget->getOwner()->getHouseID() : -1,
                    pTarget != nullptr ? pTarget->getItemID() : -1,
                    pTarget != nullptr ? pTarget->getLocation().x : -1, pTarget != nullptr ? pTarget->getLocation().y : -1,
                    pTarget != nullptr ? blockDistance(location, pTarget->getClosestPoint(location)).lround() : -1,
                    pTarget != nullptr && pTarget->getObjectID() == ownPick ? 1 : 0,
                    ageSeconds(releasedCycle), currentGame->getGameCycleCount(), garrisonOf(pTarget));
        }
        // The in-game check of Saboteurs Skip Empty Zones (every Saboteur, every new target): one
        // it chose itself is never a zone nobody garrisons. tests/units/saboteur-zone-report.py
        // fails a game with any such line.
        const ObjectBase* pChosen = target.getObjPointer();
        if(pChosen != nullptr && pChosen->getObjectID() == ownPick && passesOver(pChosen)) {
            SDL_Log("SABOTEUR_ZONE_RULE: event=violation house=%d object=%u zone_item=%d zone_at=%d,%d cycle=%u",
                    getOwner()->getHouseID(), getObjectID(), pChosen->getItemID(), pChosen->getLocation().x,
                    pChosen->getLocation().y, currentGame->getGameCycleCount());
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
                            "age_s=%d orders=%d overwrites=%d refused=%d garrison=%d own=%d",
                            getOwner()->getHouseID(), getObjectID(), pObject->getOwner()->getHouseID(),
                            pObject->getItemID(), pObject->isAUnit() ? 1 : 0,
                            currentGame->objectData.data[pObject->getItemID()][pObject->getProductionHouseID()].price,
                            currentGame->getGameCycleCount(), ageSeconds(releasedCycle),
                            armyOrders, armyOverwrites, armyRefused, garrisonOf(pObject),
                            pObject->getObjectID() == ownPick ? 1 : 0);
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
                    // destroyed directly and a Saboteur was never credited with anything, so the
                    // Stats screen showed 0 kills, 0 damage and every detonation as a death.
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

void Saboteur::blitToScreen() {
    InfantryBase::blitToScreen();
    if(currentGame == nullptr || !currentGame->isObserving()) return;
    const Uint32 color = getHouseRadarColor(static_cast<HOUSETYPE>(getOwner()->getHouseID()));
    const int x = screenborder->world2screenX(realX);
    const int y = screenborder->world2screenY(realY);
    if(const ObjectBase* pTarget = target.getObjPointer()) {
        // Two pixels wide in the owner's colour over a dark shadow.
        const Coord to = pTarget->getClosestCenterPoint(location);
        const int tx = screenborder->world2screenX(to.x), ty = screenborder->world2screenY(to.y);
        renderDrawLine(renderer, x + 1, y + 2, tx + 1, ty + 2, COLOR_BLACK);
        renderDrawLine(renderer, x, y, tx, ty, color);
        renderDrawLine(renderer, x + 1, y, tx + 1, ty, color);
        renderDrawLine(renderer, x, y + 1, tx, ty + 1, color);
    }
    // A ring about a tile across that breathes over 0.6 s: three pixels of the owner's colour
    // between dark edges, so it shows on sand, rock and city alike.
    const int pulse = static_cast<int>((SDL_GetTicks() / 100) % 6);
    const int half = world2zoomedWorld(TILESIZE / 2) + 2 * (pulse < 3 ? pulse : 6 - pulse);
    renderDrawRect(renderer, x - half - 1, y - half - 1, x + half + 1, y + half + 1, COLOR_BLACK);
    for(int inset = 0; inset < 3; ++inset)
        renderDrawRect(renderer, x - half + inset, y - half + inset, x + half - inset, y + half - inset, color);
    renderDrawRect(renderer, x - half + 3, y - half + 3, x + half - 3, y + half - 3, COLOR_BLACK);
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
            && object->isVisible(getOwner()->getTeamID())
            && !(ownSearch && passesOver(object))){

            return true;
        }

    }

    return false;


}

const ObjectBase* Saboteur::findTarget() const {
    // Buildings and vehicles alike, whichever is nearest (walls and parks last): the general
    // search over what canAttack() accepts, without the zones nobody garrisons (searchOwn). The
    // pick is remembered so that only it, never a target the owner ordered, is replaced on the
    // way (takeNearerTargetWhenSeen).
    const ObjectBase* pick = searchOwn();
    ownPick = pick != nullptr ? pick->getObjectID() : NONE_ID;
    return pick;
}

const ObjectBase* Saboteur::searchOwn() const {
    if(!skipsEmptyZones()) {
        return InfantryBase::findTarget();
    }
    ownSearch = true;
    const ObjectBase* pick = InfantryBase::findTarget();
    ownSearch = false;
    if(zoneSkips < 4 && (pick == nullptr || pick->getObjectID() != ownPick)) {
        // For the match log (the rule's activation): once per new choice, what the rule up to
        // protocol 149 would have gone for. Reads only; the random stream is not touched.
        const ObjectBase* before = InfantryBase::findTarget();
        if(before != nullptr && before != pick && passesOver(before)) {
            ++zoneSkips;
            SDL_Log("SABOTEUR_ZONE_SKIP: house=%d object=%u at=%d,%d zone_house=%d zone_item=%d zone_at=%d,%d "
                    "zone_dist=%d picked_item=%d picked_dist=%d cycle=%u", getOwner()->getHouseID(), getObjectID(),
                    location.x, location.y, before->getOwner()->getHouseID(), before->getItemID(),
                    before->getLocation().x, before->getLocation().y,
                    blockDistance(location, before->getClosestPoint(location)).lround(),
                    pick != nullptr ? pick->getItemID() : -1,
                    pick != nullptr ? blockDistance(location, pick->getClosestPoint(location)).lround() : -1,
                    currentGame->getGameCycleCount());
        }
    }
    return pick;
}

bool Saboteur::dropEmptyZone() {
    const ObjectBase* current = target.getObjPointer();
    // Its own pick, or (after a load, which forgets the pick) an AI house's hunting Saboteur,
    // which no order reaches. A zone its owner ordered it onto is kept.
    const bool own = current != nullptr && (current->getObjectID() == ownPick
                                            || (getOwner()->isAI() && attackMode == HUNT && !forced));
    if(!own || !passesOver(current)) {
        return false;
    }
    const ObjectBase* next = findTarget();
    SDL_Log("SABOTEUR_ZONE_DROP: house=%d object=%u zone_item=%d zone_at=%d,%d next_item=%d cycle=%u",
            getOwner()->getHouseID(), getObjectID(), current->getItemID(), current->getLocation().x,
            current->getLocation().y, next != nullptr ? next->getItemID() : -1, currentGame->getGameCycleCount());
    if(next != nullptr) {
        doAttackObject(next, true);
    } else {
        doSetAttackMode(GUARD);    // nothing worth it left; an AI house's Saboteur looks again (update)
    }
    return true;
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

void Saboteur::takeNearerTargetWhenSeen() {
    // A Saboteur is invisible until an enemy object is within two tiles; from then on it is shot
    // at. If something it can blow up is by then nearer than its pick (a vehicle that drove up,
    // a building put down since), it takes that instead of walking on under fire. Only its own
    // pick is replaced, never a target its owner ordered.
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
    if(dropEmptyZone() || target.getObjectID() != ownPick) {
        return;
    }
    bool seen = false;
    for(int team = 0; team < NUM_TEAM_SLOTS; ++team) {
        seen = seen || (team != getOwner()->getTeamID() && isVisible(team));
    }
    if(!seen) {
        return;
    }
    ownSearch = skipsEmptyZones();
    const ObjectBase* nearest = InfantryBase::findTarget();    // the nearest structure or vehicle
    ownSearch = false;
    if(nearest != nullptr && nearest != current
       && blockDistance(location, nearest->getClosestPoint(location))
          < blockDistance(location, current->getClosestPoint(location))) {
        doAttackObject(nearest, true);
        ownPick = nearest->getObjectID();
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
    if(detonated) {
        // Its charge going off is a building-sized blast, as Dune II's saboteur explosion is; a
        // medium blast was the same small flash as any light unit dying, and could not be told
        // from one (Stefan, 2026-11-27: the blast was not to be seen). Same random draw as before.
        explosionID = explosionID == Explosion_Medium1 ? Explosion_Large1 : Explosion_Large2;
    }
    if(!fallsWhenDestroyed()) {
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

        if(isVisible(getOwner()->getTeamID())) {
            soundPlayer->playSoundAt(Sound_ExplosionLarge,location);
        }
    }

    InfantryBase::destroy();
}
