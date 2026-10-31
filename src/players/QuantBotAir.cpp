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

// QuantBot air: anti-air defence of structures, ornithopter strikes, aircraft target authorization
// and the Death Hand target.

#include "QuantBotInternal.h"
#include <DynastyProjectile.h>
#include <array>

int QuantBot::visibleHostileAircraft() const {
    if (getHouse()==nullptr) return 0;
    const int myTeam=getHouse()->getTeamID();
    int seen=0;
    for (const auto* unit : getUnitList()) {
        if (!QuantBotInternal::ornithopterItem(unit->getItemID())) continue;
        if (unit->getHealth()<=0 || !unit->isActive()) continue;
        if (!unit->getOwner() || unit->getOwner()->getTeamID()==myTeam) continue;
        // Observation only. An aircraft we cannot see does not justify spending.
        if (!unit->isVisible(myTeam)) continue;
        ++seen;
    }
    return seen;
}

bool QuantBot::availableAirDefender(const UnitBase* unit, const UnitBase* aircraft) const {
    if (!unit || !aircraft || unit->getOwner()!=getHouse() || !unit->isActive()
        || !unit->isRespondable() || unit->isAFlyingUnit()) return false;
    const Uint32 item=unit->getItemID();
    if (item==Unit_Saboteur || isHarvesterLikeUnit(item)
        || item==Unit_MCV || item==Unit_Carryall || item==Unit_Frigate
        || item==Unit_Sandworm) return false;
    // canAttack settles which weapons reach the sky: launchers and rocket
    // infantry do, tanks and soldiers do not, and a mod may draw the line
    // somewhere else again. Nothing here enumerates unit types itself.
    if (!unit->canAttack(aircraft)) return false;
    // A human order, an explicit retreat and a trip to the repair yard all
    // outrank the rescue. An ordinary attack, raid or rally order does not:
    // interrupting those is the whole point.
    return unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free
        && unit->getAttackMode()!=RETREAT;
}

Coord QuantBot::findAntiAirFiringPosition(const UnitBase* unit, const StructureBase* victim) const {
    if (!unit || !victim) return Coord::Invalid();
    const auto& map=getMap();
    const Coord here=unit->getLocation(), at=victim->getLocation(), size=victim->getStructureSize();
    if (!map.tileExists(here)) return Coord::Invalid();
    // Defend the building's vicinity, not the aircraft's moving position.
    const int reach=std::max(1,std::min(2,unit->getWeaponRange()-1));
    auto covers=[&](Coord p) {
        const int dx=std::max({at.x-p.x,p.x-(at.x+size.x-1),0});
        const int dy=std::max({at.y-p.y,p.y-(at.y+size.y-1),0});
        return std::max(dx,dy)<=reach;
    };
    if (covers(here)) return here;
    // Prove reachability from this defender, not merely connectivity around
    // the building. Ignore moving traffic, but include permanent obstructions.
    // Bound the search to the journey and a detour margin; eight neighbours
    // match the engine's ground pathfinder.
    constexpr int detour=12;
    const int x0=std::max(0,std::min(here.x,at.x)-detour);
    const int y0=std::max(0,std::min(here.y,at.y)-detour);
    const int x1=std::min(map.getSizeX()-1,std::max(here.x,at.x+size.x)+detour);
    const int y1=std::min(map.getSizeY()-1,std::max(here.y,at.y+size.y)+detour);
    const int width=x1-x0+1;
    std::vector<char> reached(static_cast<size_t>(width)*(y1-y0+1),0);
    auto slot=[&](Coord p) { return (p.y-y0)*width+p.x-x0; };
    std::vector<Coord> frontier{here};
    reached[slot(here)]=1;
    for(size_t head=0;head<frontier.size();++head) {
        const Coord from=frontier[head];
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            const Coord next=from+Coord(dx,dy);
            if(next.x<x0 || next.y<y0 || next.x>x1 || next.y>y1 || reached[slot(next)]) continue;
            const auto* tile=map.getTile(next);
            if(tile->hasAStructure() || (tile->isMountain() && !unit->isInfantry())) continue;
            reached[slot(next)]=1;frontier.push_back(next);
        }
    }
    Coord best=Coord::Invalid();
    int bestWalk=std::numeric_limits<int>::max();
    for(int y=std::max(y0,at.y-reach);y<=std::min(y1,at.y+size.y-1+reach);++y)
        for(int x=std::max(x0,at.x-reach);x<=std::min(x1,at.x+size.x-1+reach);++x) {
            const Coord post(x,y);
            if(!reached[slot(post)] || !covers(post) || !unit->canPass(x,y)) continue;
            const int walk=blockDistance(here,post).lround();
            if(walk<bestWalk) { bestWalk=walk;best=post; }
        }
    return best;
}

bool QuantBot::airAttackContinues(const UnitBase* defender, const ObjectBase* aircraft) const {
    if (!defender || !aircraft || aircraft->getHealth()<=0 || !aircraft->isActive()
        || !aircraft->getOwner() || aircraft->getOwner()->getTeamID()==getHouse()->getTeamID()
        || !aircraft->isVisible(getHouse()->getTeamID())) return false;
    const auto* victim=aircraft->isAUnit()
        ? static_cast<const UnitBase*>(aircraft)->getTarget() : nullptr;
    if (victim && victim->getHealth()>0 && victim->isActive() && victim->getOwner()==getHouse()) return true;
    // The attack is over. Keep the defender only while it can still shoot the
    // aircraft from where it stands: nothing chases one that has broken off.
    return defender->isInWeaponRange(aircraft);
}

/**
    Aircraft attacking a building we own, wherever that building stands.

    The reported match lost two outlying construction yards and the colonies
    around them to ornithopters while launchers stayed on ground skirmishes and
    rally points. The generic reinforcement response could not fix it: it skips
    units already firing at something, and what it does order is a forced attack
    on the aircraft's current tile — which the engine drops the moment the
    aircraft is out of weapon range (UnitBase::engageTarget releases any flying
    target beyond range, forced or not), leaving the launcher parked wherever it
    happened to be standing.

    So this owns the whole contact instead. It reads the attack from the
    aircraft's target rather than waiting for damage, gives each attacked
    building a bounded number of anti-air responders, has an in-range defender
    fire instead of taking another move order, and sends the rest to a ground
    tile from which the airspace over the building is covered. Assignments live
    in the saved defenceAssignments map, so they survive save/load and are
    respected by regrouping; nothing new is stored.
*/
void QuantBot::defendStructuresFromAircraft() {
    if (builderOnly() || getHouse()==nullptr) return;
    AITelemetry::PerformanceScope perfScope("ai.air_rescue",getGameCycleCount(),getHouse()->getHouseID());
    const int myTeam=getHouse()->getTeamID();
    struct AirAttack { const UnitBase* aircraft; const StructureBase* victim; };
    std::vector<AirAttack> attacks;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isAFlyingUnit() || unit->getHealth()<=0) continue;
        if (!unit->getOwner() || unit->getOwner()->getTeamID()==myTeam) continue;
        if (!unit->isVisible(myTeam)) continue;
        // The attack itself, not the damage it has already done: waiting for a
        // damage callback is what let the colonies burn down.
        const auto* target=unit->getTarget();
        if (!target || !target->isAStructure() || target->getOwner()!=getHouse()
            || target->getHealth()<=0 || !target->isActive()) continue;
        if (isCampaignEnemy() && !campaignLocalContact(unit)) continue;
        attacks.push_back({unit,static_cast<const StructureBase*>(target)});
    }
    if (attacks.empty()) return;
    std::sort(attacks.begin(),attacks.end(),[](const AirAttack& a,const AirAttack& b) {
        if (a.victim->getObjectID()!=b.victim->getObjectID())
            return a.victim->getObjectID()<b.victim->getObjectID();
        return a.aircraft->getObjectID()<b.aircraft->getObjectID();
    });
    struct Candidate { const UnitBase* unit; int distance, busy; Uint32 id; };
    std::set<Uint32> committed;
    // kAirRescueDefenders is documented above as a bound per attacked building,
    // but it was applied per attack, and one building can be attacked by a whole
    // wing at once: four raiding ornithopters on one colony claimed twelve
    // anti-air responders, and the candidate sort puts launchers first, so the
    // ground waves lost their launchers to a building that only ever needed
    // three. Accounting per victim restores the documented bound. Defenders
    // already travelling to a rescue keep their place - dropping those is the
    // re-chosen rescue that never arrives - so the count only ever settles down
    // towards the bound as contacts end.
    std::map<Uint32,int> defendersPerVictim;
    std::map<Uint32,const AirAttack*> activeAttacks;
    for(const auto& attack:attacks) activeAttacks.emplace(attack.aircraft->getObjectID(),&attack);
    // Count every continuing journey first. Otherwise a newly active aircraft
    // with a lower ID can recruit three before the later contact's three are
    // encountered, multiplying the same building's rescue on the next pass.
    for(const auto& assignment:defenceAssignments) {
        const auto attack=activeAttacks.find(assignment.second);
        if(attack==activeAttacks.end()) continue;
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(assignment.first));
        if(unit && availableAirDefender(unit,attack->second->aircraft)
            && !unit->isInWeaponRange(attack->second->aircraft))
            ++defendersPerVictim[attack->second->victim->getObjectID()];
    }
    for (const auto& attack:attacks) {
        const Uint32 aircraftID=attack.aircraft->getObjectID();
        const Uint32 victimID=attack.victim->getObjectID();
        const Coord contact=attack.victim->getLocation();
        std::vector<const UnitBase*> responders;
        std::vector<Candidate> candidates;
        std::map<Uint32,Coord> posts;
        for (const auto* unit:getUnitList()) {
            const Uint32 id=unit->getObjectID();
            if (committed.count(id) || !availableAirDefender(unit,attack.aircraft)) continue;
            const auto assigned=defenceAssignments.find(id);
            if (assigned!=defenceAssignments.end()) {
                // Already on this aircraft: keep it there. A rescue that is
                // re-chosen every pass never arrives anywhere.
                // Answering another live air attack: leave that one alone.
                const auto* other=getObject(assigned->second);
                if (assigned->second!=aircraftID && other && other->isAUnit() && other->isAFlyingUnit()
                    && airAttackContinues(unit,other)) continue;
            }
            const int distance=blockDistance(unit->getLocation(),contact).lround();
            const bool continuing=assigned!=defenceAssignments.end() && assigned->second==aircraftID;
            if (distance>kAirRescueRadius && !continuing) continue;
            if(continuing) {
                const bool inRange=unit->isInWeaponRange(attack.aircraft);
                const Coord post=inRange
                    ? unit->getLocation() : findAntiAirFiringPosition(unit,attack.victim);
                if(post.isInvalid()) {
                    if(!inRange) --defendersPerVictim[victimID];
                    defenceAssignments.erase(assigned);continue;
                }
                posts[id]=post;responders.push_back(unit);
                continue;
            }
            candidates.push_back({unit,distance,assigned!=defenceAssignments.end() ? 1 : 0,id});
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
            const auto launcher=[](const UnitBase* unit) {
                return unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher;
            };
            // Launchers provide the mobile air cover; nearby rocket infantry
            // are a fallback when no launcher is available.
            if(launcher(a.unit)!=launcher(b.unit)) return launcher(a.unit);
            if (a.distance!=b.distance) return a.distance<b.distance;
            if (a.busy!=b.busy) return a.busy<b.busy;   // Free troops before committed ones.
            return a.id<b.id;
        });
        for (const auto& candidate:candidates) {
            // A defender that can already shoot costs nothing to recruit: it
            // does not travel, so it is not what empties the ground waves, and
            // withholding its fire would only let the aircraft work unopposed.
            // The bound is on responders that have to march at the building.
            const bool inRange=candidate.unit->isInWeaponRange(attack.aircraft);
            if (!inRange && defendersPerVictim[victimID]>=kAirRescueDefenders) continue;
            // Only route the nearest preferred candidates we actually need.
            const Coord post=inRange
                ? candidate.unit->getLocation() : findAntiAirFiringPosition(candidate.unit,attack.victim);
            if(post.isInvalid()) continue;
            posts[candidate.id]=post;responders.push_back(candidate.unit);
            if(!inRange) ++defendersPerVictim[victimID];
        }
        int firing=0, approaching=0, stranded=0;
        for (const auto* unit:responders) {
            const Uint32 id=unit->getObjectID();
            committed.insert(id);
            defenceAssignments[id]=aircraftID;
            groundSquad.erase(id);
            if (unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
            if (unit->isInWeaponRange(attack.aircraft)) {
                // Useful fire outranks another move order: a launcher that can
                // already shoot is not marched at a tile the aircraft happens
                // to be flying over.
                if (unit->getTarget()!=attack.aircraft) doAttackObject(unit,attack.aircraft,true);
                ++firing;
                continue;
            }
            const Coord post=posts.at(id);
            // No reachable ground covers this building for this unit. Ordering
            // it at the aircraft would be ordering it onto thin air.
            if (post.isInvalid()) { ++stranded; continue; }
            if (unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
            const_cast<UnitBase*>(unit)->setGuardPoint(post);
            // Commit transit so incidental ground targets cannot arrest the
            // rescue. The next air pass switches to fire as soon as it can.
            if (unit->getLocation()!=post && (unit->getDestination()!=post
                || !unit->wasForced() || unit->hasATarget()))
                doMove2Pos(unit,post.x,post.y,true);
            ++approaching;
        }
        if (firing || approaching || stranded)
            traceDecision("air_rescue",AITelemetry::Record().set("aircraft",aircraftID)
                .set("victim",attack.victim->getObjectID()).set("item",attack.victim->getItemID())
                .set("x",contact.x).set("y",contact.y).set("firing",firing)
                .set("approaching",approaching).set("no_firing_position",stranded)
                .set("defenders",static_cast<int>(responders.size()))
                .set("victim_travelling_defenders",defendersPerVictim[victimID])
                .set("victim_defender_cap",kAirRescueDefenders));
    }
}

bool QuantBot::tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                          const QuantBotConfig& config, const ObjectBase* emergencyAttacker,
                                          bool emergencyOnBase) {
    const int myTeam=getHouse()->getTeamID();
    const Map& map=getMap();
    std::vector<const UnitBase*> aircraft;
    for(const auto* unit:getUnitList())
        if(unit->getOwner()==getHouse() && QuantBotInternal::ornithopterItem(unit->getItemID())
            && unit->isActive() && unit->getHealth()>0
            && unit->isRespondable() && unitRole(unit, {UnitRole::Human}) == UnitRole::Free) aircraft.push_back(unit);
    ornithopterStrikeTeam.reset(); // Old saved mass-Hunt missions no longer grant permission to attack.
    // Only this pass authorizes a structure sortie. A withdrawn or unavailable
    // aircraft must not retain permission from a previous sortie.
    aircraftTargetAuthority.clear();
    for(auto it=assetAttackerContacts.begin();it!=assetAttackerContacts.end();) {
        if(getGameCycleCount()-it->second.cycle >= MILLI2CYCLES(kAircraftDefenceGraceMs))
            it=assetAttackerContacts.erase(it);
        else ++it;
    }
    if(aircraft.empty()) return false;
    // Difficulty policy for this house, read once per pass.
    const bool offensiveRaids=autonomousAircraftRaids();
    AITelemetry::PerformanceScope perfScope("ai.ornithopter_safe_strikes",getGameCycleCount(),getHouse()->getHouseID());

    AirStrikePolicy::Coverage coverage(map.getSizeX(),map.getSizeY());
    // The strict layer exists only where a wing may not price its way in. It is
    // filled from the same single defender walk below, so no extra scan is
    // added, and Hard/Brutal never allocate or consult it.
    if(!offensiveRaids) coverage.enableStrict();
    int visibleAntiAir=0;
    int visibleMobileLaunchers=0;
    // Escort candidates with their pass-constant weight and guard radius. The
    // priority table is keyed by name, so looking it up once per launcher per
    // pass rather than once per launcher per aircraft removes the whole lookup
    // from the inner escort loop without changing any score.
    struct MobileLauncher { const ObjectBase* object; int weight; int guard; };
    std::vector<MobileLauncher> mobileLaunchers;
    auto addDefender=[&](const ObjectBase* defender) {
        if(!defender || !defender->isActive() || defender->getHealth()<=0
            || !defender->getOwner() || defender->getOwner()->getTeamID()==myTeam
            || !defender->isVisible(myTeam)) return;
        // The strict layer knows every weapon that actually reaches an aircraft,
        // at the reach it actually has - a rocket turret shoots ornithopters
        // three times further than it shoots the ground. Non-anti-air units are
        // not in it, so an ordinary tank never blocks an undefended base.
        if(!offensiveRaids && AirStrikePolicy::strictAntiAir(defender->getItemID()))
            coverage.addStrict(defender->getLocation(),
                AirStrikePolicy::antiAirSafetyRange(defender->getItemID(),defender->getWeaponRange()));
        if(!AirStrikePolicy::antiAir(defender->getItemID())) return;
        // A temporary power outage does not make a turret district a safe sortie.
        const int range=AirStrikePolicy::safetyRange(defender->getWeaponRange());
        // Mobile rocket cover is a price, not a wall: count the overlap per tile
        // so a wing is weighed against the launchers that actually cover it.
        // Turrets and Deviators keep absolute cover. Nothing is inferred through
        // fog here - an unseen launcher charges nothing and protects nothing.
        if(AirStrikePolicy::mobileLauncher(defender->getItemID())) {
            coverage.addMobileLauncher(defender->getLocation(),range);
            const auto& escortPriority=config.getUnitPriority(defender->getItemID());
            mobileLaunchers.push_back({defender,
                std::max(1,escortPriority.build+escortPriority.target),range});
            ++visibleMobileLaunchers;
        } else {
            coverage.add(defender->getLocation(),range);
        }
        ++visibleAntiAir;
    };
    for(const auto* structure:getStructureList()) addDefender(structure);
    for(const auto* unit:getUnitList()) addDefender(unit);

    // The wing that would fly the sortie, indexed around each aircraft rather
    // than around the prey: a formation that gathers just outside a target's own
    // radius is still a wing, and counting only near the target parks it there
    // for the rest of the match. Badly damaged aircraft are not counted towards
    // an offensive ratio; they still defend under the emergency ranks below.
    LocalPointIndex wingIndex(map.getSizeX(),map.getSizeY());
    std::vector<const UnitBase*> wing;
    for(const auto* flier:aircraft) {
        if(flier->getAttackMode()==RETREAT || flier->isBadlyDamaged()) continue;
        if(isCampaignEnemy() && !campaignWave.members.count(flier->getObjectID())) continue;
        wingIndex.add(flier->getLocation().x,flier->getLocation().y,wing.size());
        wing.push_back(flier);
    }

    std::vector<const ObjectBase*> defendedAssets;
    for(const auto* structure:getStructureList())
        if(structure->getOwner()==getHouse() && structure->isActive() && structure->getHealth()>0)
            defendedAssets.push_back(structure);
    for(const auto* worker:getUnitList())
        if(worker->getOwner()==getHouse() && worker->isActive() && worker->getHealth()>0
            && isHarvesterLikeUnit(worker->getItemID()))
            defendedAssets.push_back(worker);
    LocalPointIndex defendedIndex(map.getSizeX(),map.getSizeY());
    int largestAsset=1;
    for(size_t i=0;i<defendedAssets.size();++i) {
        const auto* asset=defendedAssets[i];
        defendedIndex.add(asset->getLocation().x,asset->getLocation().y,i);
        if(asset->isAStructure()) {
            const Coord size=static_cast<const StructureBase*>(asset)->getStructureSize();
            largestAsset=std::max({largestAsset,size.x,size.y});
        }
    }
    struct Candidate { const ObjectBase* object; Coord size; int weight; int rank; bool strict; };
    std::vector<Candidate> candidates;
    auto addCandidate=[&](const ObjectBase* object,const QuantBotConfig::TargetPriority& priority) {
        if(!object || !object->isActive() || object->getHealth()<=0 || !object->getOwner()
            || object->getOwner()->getTeamID()==myTeam || !object->isVisible(myTeam)
            || object->isAFlyingUnit()) return;
        // Enemy workers are not air targets at any rank. Harvesting is answered
        // on the ground; an aircraft strafing a worker is what the complaint
        // was about, and an emergency does not re-open it either, so this sits
        // ahead of the under-attack ranks rather than inside the raid gate.
        if(isHarvesterLikeObject(object)) return;
        const Coord size=object->isAStructure()
            ? static_cast<const StructureBase*>(object)->getStructureSize() : Coord(1,1);
        const auto* victim=object->getTarget();
        const bool engagingAsset=victim && victim->isActive()
            && victim->getHealth()>0 && victim->getOwner()==getHouse()
            && (victim->isAStructure() || isHarvesterLikeUnit(victim->getItemID()))
            && object->canAttack(victim)
            && blockDistance(object->getLocation(),victim->getClosestPoint(object->getLocation()))
                <= object->getWeaponRange()+3;
        // A contact that hit one of our assets a moment ago is still answering
        // for it. Only the restricted difficulties need this: they have no
        // proximity rank, and a rocket launcher has no target of its own while
        // it reloads, so without the grace the reload reads as the attack
        // having stopped. The offensive difficulties keep their exact ranks.
        const auto recentContact=assetAttackerContacts.find(object->getObjectID());
        const bool recentlyHitAsset=!offensiveRaids
            && recentContact!=assetAttackerContacts.end()
            && getGameCycleCount()-recentContact->second.cycle
                < MILLI2CYCLES(kAircraftDefenceGraceMs);
        const bool underAttack=object==emergencyAttacker || engagingAsset || recentlyHitAsset;
        // A building of ours is the base; a worker in the field is not. The
        // damage callback reports which one it was for its own attacker.
        const bool attackingBase=engagingAsset ? victim->isAStructure()
            : (object==emergencyAttacker && emergencyOnBase)
                || (recentlyHitAsset && recentContact->second.base);
        // Cover is no longer decided here. Whether a raid can be flown depends
        // on the wing that would fly it, which is per aircraft below; this pass
        // only establishes what exists and what it is worth. Turret and Deviator
        // cover still excludes everything standing in it, including the turret
        // and the Deviator themselves, because that cover is absolute.
        // All undefended buildings are eligible, including zones absent from the
        // combat priority table; retain configured priorities for ranking.
        bool defensiveContact=false;
        if(!object->isAStructure() && object->canAttack() && offensiveRaids) {
            defendedIndex.visit(object->getLocation().x,object->getLocation().y,
                object->getWeaponRange()+3+largestAsset,[&](size_t i) {
                    if(!defensiveContact && blockDistance(object->getLocation(),
                        defendedAssets[i]->getClosestPoint(object->getLocation())) <= object->getWeaponRange()+3)
                        defensiveContact=true;
                });
        }
        // Ordinary enemy ground units in the open are raid candidates now, not
        // only the ones standing over something of ours. The offensive-enable
        // and campaign-wave gates below are unchanged, so this opens nothing on
        // the difficulties that do not raid, and there is still no mass Hunt:
        // every order issued at the end of this function is an explicit target.
        const int rank=underAttack ? AirStrikePolicy::underAttackRank(attackingBase)
            : AirStrikePolicy::targetRank(object->isAStructure(),defensiveContact,true);
        // Worms and crime troopers (the Natives) are answered when they threaten what we own,
        // never raided as an enemy house's army.
        if (rank==AirStrikePolicy::RaidRank && object->getOwner()->isNatives()) return;
        // Difficulty policy. Hard and Brutal keep every rank they have today,
        // including the configured raid flag. The remaining difficulties may
        // only answer an attack on something we own; their one offensive option
        // is an enemy base building whose raid area has no observed anti-air
        // over it at all, and no wing size buys passage into that cover.
        bool strictCover=false;
        if(offensiveRaids) {
            if (rank==AirStrikePolicy::RaidRank && !diffSettings.ornithopterAttackEnabled) return;
        } else if(rank==AirStrikePolicy::RaidRank) {
            if(!object->isAStructure() || !diffSettings.attackEnabled
                || !AirStrikePolicy::opportunisticBaseStructure(object->getItemID())) return;
            strictCover=true;
        }
        if (isCampaignEnemy() && rank==AirStrikePolicy::DefenseRank && !campaignLocalContact(object)) return;
        if(rank>0) candidates.push_back({object,size,std::max(1,priority.build+priority.target),
            rank,strictCover});
    };
    for(const auto* structure:getStructureList())
        addCandidate(structure,config.getStructurePriority(structure->getItemID()));
    // Defensive interception is available even when offensive raids are disabled.
    for(const auto* unit:getUnitList())
        addCandidate(unit,config.getUnitPriority(unit->getItemID()));

    bool issued=false;
    for(const auto* unit:aircraft) {
        const ObjectBase* target=nullptr;
        double bestScore=-1;
        int bestRank=0;
        // The interception this aircraft is already flying, if it is still a
        // legal target of the same search. It wins every equal-or-lower ranked
        // comparison below, so a wing finishes its attack runs instead of
        // re-choosing between equivalent targets on every damage callback.
        const ObjectBase* held=nullptr;
        int heldRank=0;
        // How many effective aircraft are flying with this one. One bounded
        // index visit, not a scan of every aircraft per candidate.
        bool launcherFirst=false;
        int localWing=0;
        wingIndex.visit(unit->getLocation().x,unit->getLocation().y,
            AirStrikePolicy::kLocalEngagementRadius,[&](size_t i) {
                if(blockDistance(unit->getLocation(),wing[i]->getLocation())
                    <= AirStrikePolicy::kLocalEngagementRadius) ++localWing;
            });
        // Reachable for this aircraft on this pass. Emergencies accept known
        // cover as they always have. A raid needs the whole target footprint
        // and the direct approach to be affordable for this wing, so a
        // favourable endpoint cannot buy passage through a deeper overlap
        // somewhere along the route.
        auto reachable=[&](const Candidate& candidate) {
            if(AirStrikePolicy::emergencyRank(candidate.rank)) return true;
            // Strict opportunism: the whole footprint and the direct approach
            // must be free of every observed anti-air weapon. The local wing is
            // not an argument here, so there is no outnumber override.
            if(candidate.strict)
                return coverage.strictClearFootprint(candidate.object->getLocation(),candidate.size)
                    && coverage.strictClearApproach(unit->getLocation(),
                        candidate.object->getClosestPoint(unit->getLocation()));
            return coverage.clearFootprint(candidate.object->getLocation(),candidate.size,localWing)
                && coverage.clearApproach(unit->getLocation(),
                    candidate.object->getClosestPoint(unit->getLocation()),localWing);
        };
        // Deterministic ordering: rank, then score, then the lower object id.
        auto preferred=[](double score,Uint32 id,double bestSoFar,const ObjectBase* incumbent) {
            if(incumbent==nullptr) return true;
            if(score!=bestSoFar) return score>bestSoFar;
            return id<incumbent->getObjectID();
        };
        if(unitRole(unit, {UnitRole::Repair}) == UnitRole::Free && unit->getAttackMode()!=RETREAT) {
            for(const auto& candidate:candidates) {
                if (isCampaignEnemy() && candidate.rank==AirStrikePolicy::RaidRank
                    && !campaignWave.members.count(unit->getObjectID())) continue;
                if(!unit->canAttack(candidate.object)) continue;
                const Coord endpoint=candidate.object->getClosestPoint(unit->getLocation());
                double score=double(candidate.weight)/(blockDistance(unit->getLocation(),endpoint).toDouble()+1);
                // Decide what this candidate could win before paying for the
                // route. The footprint and approach walks are the expensive
                // part of the pass and every candidate used to pay for one even
                // when its rank and score could not beat the incumbent; with
                // every roaming enemy unit now a candidate that is a whole-map
                // line walk per aircraft per enemy. Same tests, same order,
                // same winner - only the ones that would actually change the
                // choice are costed.
                const bool wouldHold=candidate.object==unit->getTarget() && unit->wasForced()
                    && candidate.rank>heldRank;
                const bool wouldWin=candidate.rank>bestRank
                    || (candidate.rank==bestRank
                        && preferred(score,candidate.object->getObjectID(),bestScore,target));
                if(!wouldHold && !wouldWin) continue;
                if(!reachable(candidate)) continue;
                if(wouldHold) { held=candidate.object; heldRank=candidate.rank; }
                if(!wouldWin) continue;
                bestRank=candidate.rank;
                bestScore=score;
                target=candidate.object;
            }
            if(AirStrikePolicy::holdsInterception(heldRank,bestRank)) {
                target=held;
                bestRank=heldRank;
            }
            // Launchers first, but only the ones in this engagement: the escort
            // standing over our intended prey, or one already standing over the
            // wing, is what kills the wing. A launcher on the far side of the
            // map is still just a ground unit ranked by distance - promoting it
            // on the item flag alone would march the wing past exposed prey.
            // Finish a live launcher run; a newly arrived escort can replace
            // any other prey, including a held emergency attacker.
            // Only the raiding difficulties promote an escort: on the others a
            // launcher standing over the engagement is still an enemy unit that
            // is not attacking anything of ours, so the defensive sortie keeps
            // the contact it was sent at.
            if(target!=nullptr && offensiveRaids
                && !AirStrikePolicy::mobileLauncher(target->getItemID())) {
                const Coord prey=target->getClosestPoint(unit->getLocation());
                const ObjectBase* escort=nullptr;
                double escortScore=-1;
                for(const auto& launcher:mobileLaunchers) {
                    if(!unit->canAttack(launcher.object)) continue;
                    // The escort belongs to the already-authorized engagement.
                    // It remains eligible for defense when raids are disabled.
                    const Candidate candidate{launcher.object,Coord(1,1),launcher.weight,bestRank,false};
                    const Coord at=candidate.object->getClosestPoint(unit->getLocation());
                    const bool guardsPrey=blockDistance(at,prey)<=launcher.guard;
                    const bool guardsWing=blockDistance(at,unit->getLocation())
                        <=std::max(launcher.guard,AirStrikePolicy::kLocalEngagementRadius);
                    if(!guardsPrey && !guardsWing) continue;
                    const double score=double(candidate.weight)
                        /(blockDistance(unit->getLocation(),at).toDouble()+1);
                    if(!preferred(score,candidate.object->getObjectID(),escortScore,escort)) continue;
                    // Entering this engagement is already sanctioned - by the
                    // ratio for a raid, or by the emergency for a rescue - and
                    // the escort is the reason the ratio was measured at all.
                    // Costed last, so only a would-be winner pays for a route.
                    if(!AirStrikePolicy::emergencyRank(bestRank) && !reachable(candidate)) continue;
                    escort=candidate.object;
                    escortScore=score;
                }
                if(escort!=nullptr) { target=escort; launcherFirst=true; }
            }
        }
        const bool modeChanged=unit->getAttackMode()!=STOP;
        const bool hadTarget=unit->hasATarget();
        if(target) {
            // Record the permission before the order, and on every pass that
            // keeps the same target: the hull-side and search-side checks read
            // this, so a legal attack run must not expire while it flies.
            if(!offensiveRaids && target->isAStructure())
                aircraftTargetAuthority[unit->getObjectID()]=target->getObjectID();
            const bool orderNeeded=modeChanged || unit->getTarget()!=target || !unit->wasForced();
            // STOP suppresses autonomous target acquisition; the explicit forced
            // attack still flies and fires. No follow-on Hunt after target death.
            if(modeChanged) doSetAttackMode(unit,STOP);
            if(orderNeeded) {
                doAttackObject(unit,target,true);
                issued=true;
                traceDecision("ornithopter_safe_strike",AITelemetry::Record().set("unit",unit->getObjectID())
                    .set("target",target->getObjectID()).set("target_item",target->getItemID())
                    .set("visible_anti_air",visibleAntiAir).set("safety_margin_tiles",5)
                    .set("held_target",held==target)
                    .set("local_wing",localWing)
                    .set("visible_mobile_launchers",visibleMobileLaunchers)
                    .set("local_covering_launchers",
                        coverage.mobileLaunchersAt(target->getClosestPoint(unit->getLocation())))
                    .set("launcher_first",launcherFirst)
                    // A raid on a roaming unit is not an exposed building, and an
                    // audit that calls it one cannot tell the two apart.
                    .set("reason",launcherFirst ? "launcher_escort_first"
                        : bestRank==AirStrikePolicy::BaseUnderAttackRank ? "intercept_base_attacker"
                        : bestRank==AirStrikePolicy::UnderAttackRank ? "intercept_active_attacker"
                        : bestRank!=AirStrikePolicy::RaidRank ? "defend_base_or_harvester"
                        : target->isAStructure() ? "exposed_building" : "exposed_unit"));
            }
        } else {
            // Nothing this aircraft may attack. Withdrawing the permission is
            // what makes the engine searches and the hull drop anything it is
            // still holding, including an order from an earlier difficulty or a
            // restored save.
            aircraftTargetAuthority.erase(unit->getObjectID());
            if(modeChanged || hadTarget) { doSetAttackMode(unit,STOP); issued=true; }
            // Return along a clear corridor to a real owned building, not a base
            // centroid that can sit in enemy fire. Escape newly arrived AA first.
            Coord home; home.invalidate();
            int bestDistance=std::numeric_limits<int>::max();
            for(const auto* asset:defendedAssets) {
                if(!asset->isAStructure()) continue;
                const Coord point=asset->getLocation();
                const int distance=(blockDistance(unit->getLocation(),point)*100).lround();
                if(distance<bestDistance && coverage.clearApproach(unit->getLocation(),point)) {
                    home=point; bestDistance=distance;
                }
            }
            if(home.isInvalid() && !coverage.safe(unit->getLocation())) home=coverage.escape(unit->getLocation());
            if(home.isValid()) {
                const_cast<UnitBase*>(unit)->setGuardPoint(home.x,home.y);
                const bool unsafeDestination=unit->getDestination().isValid()
                    && !coverage.clearApproach(unit->getLocation(),unit->getDestination());
                if(modeChanged || hadTarget || unsafeDestination
                    || blockDistance(unit->getLocation(),home)>3) {
                    if(unit->getDestination()!=home || hadTarget || modeChanged) {
                        doMove2Pos(unit,home.x,home.y,true);
                        issued=true;
                    }
                }
            }
            if(modeChanged || hadTarget) traceDecision("ornithopter_hold",AITelemetry::Record()
                .set("unit",unit->getObjectID()).set("visible_anti_air",visibleAntiAir)
                .set("local_wing",localWing)
                .set("visible_mobile_launchers",visibleMobileLaunchers)
                .set("reason","no_safe_target_or_approach"));
        }
    }
    return issued;
}

bool QuantBot::managesAutonomousOrnithopter(const UnitBase* unit) const {
    return unit && unit->getOwner()==getHouse() && QuantBotInternal::ornithopterItem(unit->getItemID())
        && !builderOnly() && unitRole(unit, {UnitRole::Human}) == UnitRole::Free;
}

bool QuantBot::autonomousAircraftRaids() const {
    // Offensive air hunts are a Hard and Brutal behaviour, by decision of the
    // project owner. The difficulty decides, not the configured flag: a copied
    // settings block must not be able to re-open unit raids on Easy or Medium.
    return !builderOnly()
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal);
}

bool QuantBot::aircraftDefenceTarget(const ObjectBase* candidate) const {
    if(!candidate || candidate->getHealth()<=0 || !candidate->isActive()
        || !candidate->canAttack() || !candidate->getOwner() || getHouse()==nullptr
        || candidate->getOwner()->getTeamID()==getHouse()->getTeamID()) return false;
    // It hit a building or worker of ours a moment ago. A rocket launcher
    // between volleys has no target of its own for several seconds, and that
    // gap is not the end of the attack.
    const auto recent=assetAttackerContacts.find(candidate->getObjectID());
    if(recent!=assetAttackerContacts.end()
        && getGameCycleCount()-recent->second.cycle < MILLI2CYCLES(kAircraftDefenceGraceMs)) return true;
    // Or it is standing over one right now, with that asset as its own target.
    const auto* victim=candidate->getTarget();
    if(!victim || victim->getHealth()<=0 || !victim->isActive()
        || victim->getOwner()!=getHouse()) return false;
    if(!victim->isAStructure() && !isHarvesterLikeUnit(victim->getItemID())) return false;
    if(!candidate->canAttack(victim)) return false;
    return blockDistance(candidate->getLocation(),victim->getClosestPoint(candidate->getLocation()))
        <= candidate->getWeaponRange()+3;
}

bool QuantBot::authorizesAircraftTarget(const UnitBase* aircraft, const ObjectBase* candidate) const {
    if(!aircraft || !candidate || !getHouse() || aircraft->getOwner()!=getHouse()
        || !candidate->getOwner() || candidate->getHealth()<=0 || !candidate->isActive()
        || candidate->getOwner()->getTeamID()==getHouse()->getTeamID()
        || !candidate->isVisible(getHouse()->getTeamID())) return false;
    // The worker veto is absolute at every difficulty and every rank.
    if(isHarvesterLikeObject(candidate)) return false;
    // Hard and Brutal retain offensive targeting.
    if(autonomousAircraftRaids()) return true;
    // A past order never authorizes an unrelated unit once its attack ends.
    // Only a current asset attack or the short confirmed-hit grace does.
    if(aircraftDefenceTarget(candidate)) return true;
    if(!candidate->isAStructure()) return false;
    // The structure sortie admitted by the shared anti-air check this pass.
    const auto authorized=aircraftTargetAuthority.find(aircraft->getObjectID());
    if(authorized!=aircraftTargetAuthority.end()
        && authorized->second==candidate->getObjectID()) return true;
    return false;
}

namespace {
/// DeathHandTurrets: the fewest seen enemy turrets one Death Hand must be able to hit before
/// it is aimed at turrets instead of the production ranking.
constexpr int kDeathHandMinTurrets = 4;

/// The tiles, relative to the aimed tile, whose structures a Death Hand landing there damages.
/// The missile lands on a tile centre and Map::damage hurts a structure only when one of the
/// seventeen blast centres (DynastyProjectile::deathHandBlast*) lies inside its footprint:
/// the 3x3 block around the impact plus eight tiles two away.
std::array<Coord, DynastyProjectile::deathHandBlastCentres> deathHandHitTiles() {
    const auto tileOf = [](int pixels) { return (pixels >= 0 ? pixels : pixels - (TILESIZE - 1)) / TILESIZE; };
    std::array<Coord, DynastyProjectile::deathHandBlastCentres> tiles;
    for (int i = 0; i < DynastyProjectile::deathHandBlastCentres; ++i)
        tiles[i] = Coord(tileOf(TILESIZE/2 + DynastyProjectile::deathHandBlastX[i]),
                         tileOf(TILESIZE/2 + DynastyProjectile::deathHandBlastY[i]));
    return tiles;
}
}

Coord QuantBot::findBestDeathHandTarget(int enemyHouseID) {
	const QuantBotConfig& config = botConfig();
	const int myTeam = getHouse()->getTeamID();

    const Coord reactorTarget = findNuclearMissileTarget();
    if (reactorTarget.isValid()) return reactorTarget;

    // DeathHandTurrets (force.deathHandTurrets): aim at the seen enemy turret whose tile hits
    // the most seen enemy turrets of the team our ground wave is fighting, else of any enemy
    // team, once that is at least kDeathHandMinTurrets; else the production ranking below.
    if (config.force.deathHandTurrets) {
        struct SeenTurret { Coord at; int team; };
        std::vector<SeenTurret> turrets;
        for (const StructureBase* structure : getStructureList())
            if (structure->isActive() && knownEnemyStructure(structure) && structure->canAttack()
                && structure->isVisible(myTeam) && !structure->getOwner()->isNatives())
                turrets.push_back({structure->getLocation(), structure->getOwner()->getTeamID()});
        // The wave's team: H's / Z's weakest house when those rules lead it, else the team most
        // of the wave's ground units are shooting at (ties to the lower team id).
        int waveTeam = -1;
        if (forceVariant(kForceFinishWeakest) || forceVariant(kForceMassAssault)) {
            waveTeam = finishTargetTeam();
        } else {
            std::map<int, int> engaged;
            for (const Uint32 id : customWave.members) {
                const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
                if (!unit || unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isAGroundUnit()) continue;
                const ObjectBase* target = unit->getTarget();
                if (target && target->getOwner() && target->getOwner()->getTeamID() != myTeam
                    && !target->getOwner()->isNatives()) ++engaged[target->getOwner()->getTeamID()];
            }
            int most = 0;
            for (const auto& [team, count] : engaged)
                if (count > most) { most = count; waveTeam = team; }
        }
        const auto hitTiles = deathHandHitTiles();
        // Candidate centres are the turret tiles; -1 counts every enemy team's turrets.
        const auto bestCluster = [&](int team, int& bestCount) {
            Coord best = Coord::Invalid();
            bestCount = 0;
            for (const auto& centre : turrets) {
                if (team >= 0 && centre.team != team) continue;
                int count = 0;
                for (const auto& other : turrets) {
                    if (team >= 0 && other.team != team) continue;
                    const Coord offset = other.at - centre.at;
                    if (std::find(hitTiles.begin(), hitTiles.end(), offset) != hitTiles.end()) ++count;
                }
                if (count > bestCount) { bestCount = count; best = centre.at; }
            }
            return best;
        };
        int hits = 0;
        Coord aim = waveTeam >= 0 ? bestCluster(waveTeam, hits) : Coord::Invalid();
        const bool waveCluster = hits >= kDeathHandMinTurrets;
        if (!waveCluster) aim = bestCluster(-1, hits);
        if (hits >= kDeathHandMinTurrets) {
            traceDecision("deathhand_turret_aim", AITelemetry::Record().set("x", aim.x).set("y", aim.y)
                .set("turrets", hits).set("wave_team", waveTeam).set("wave_cluster", waveCluster ? 1 : 0)
                .set("seen_turrets", static_cast<int>(turrets.size())));
            return aim;
        }
    }

	const StructureBase* bestTarget = nullptr;
	double bestScore = -1.0;

	// Evaluate each enemy structure as a potential target
	for (const StructureBase* pCandidate : getStructureList()) {
		if (!pCandidate || !pCandidate->isActive()) {
			continue;
		}

		if (pCandidate->getOwner()->getHouseID() != enemyHouseID) {
			continue;
		}

		if (!pCandidate->isVisible(myTeam)) {
			continue;
		}

		// Get base priority from config
		const QuantBotConfig::TargetPriority& priority = config.getStructurePriority(pCandidate->getItemID());
		const int weight = priority.build + priority.target;
		if (weight <= 0) {
			continue;
		}

		// Apply production building multiplier
		const double productionMultiplier = getProductionBuildingMultiplier(pCandidate->getItemID());
		double score = static_cast<double>(weight) * productionMultiplier;

		// Center of mass calculation: add weighted value of nearby buildings
		// Death hand has 10-tile inaccuracy, so check 5-tile radius for nearby targets
		const Coord candidatePos = pCandidate->getLocation();
		constexpr int CHECK_RADIUS = 5;
		double centerOfMassBonus = 0.0;

		for (const StructureBase* pNearby : getStructureList()) {
			if (!pNearby || !pNearby->isActive() || pNearby == pCandidate) {
				continue;
			}

			if (pNearby->getOwner()->getHouseID() != enemyHouseID) {
				continue;
			}

			if (!pNearby->isVisible(myTeam)) {
				continue;
			}

			FixPoint distance = blockDistance(candidatePos, pNearby->getLocation());
			if (distance.toDouble() <= CHECK_RADIUS) {
				// Get this nearby building's priority weight
				const QuantBotConfig::TargetPriority& nearbyPriority = config.getStructurePriority(pNearby->getItemID());
				const int nearbyWeight = nearbyPriority.build + nearbyPriority.target;

				if (nearbyWeight > 0) {
					// Add distance-weighted contribution: closer buildings contribute more
					centerOfMassBonus += static_cast<double>(nearbyWeight) / (distance.toDouble() + 1.0);
				}
			}
		}

		// Final score is base score plus center of mass bonus
		score += centerOfMassBonus;

		if (score > bestScore) {
			bestScore = score;
			bestTarget = pCandidate;
		}
	}

	if (bestTarget != nullptr) {
		return bestTarget->getLocation();
	}

	// Fallback to center of base if no suitable target found
	return findBaseCentre(enemyHouseID);
}
