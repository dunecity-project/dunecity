#include <players/QuantBot.h>
#include <players/QuantBotCityPolicy.h>
#include <players/HumanPlayer.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <misc/CampaignControls.h>
#include <dunecity/CitySimulation.h>
#include <players/QuantBotConfig.h>
#include <units/Harvester.h>
#include <sand.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <Trigger/ReinforcementTrigger.h>
#include <Trigger/TriggerManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/FileManager.h>
#include <dunecity/CityEffects.h>
#include <structures/BuilderBase.h>
#include <structures/ZoneStructure.h>
#include <dunecity/CityStructurePopulation.h>

int QuantBot::getCityPopulationLimit(int mapArea) const {
    if(!currentGame || !currentGame->isCitySimEnabled()
       || gameMode != GameMode::Custom || isCampaignGameType(currentGame->gameType)) return 0;
    // All custom-game QuantBots share the same difficulty ceiling, including
    // helpers in a human-controlled house and support-mode controllers.
    return std::max(0, QuantBotCityPolicy::populationLimit(static_cast<int>(difficulty),mapArea));
}

bool QuantBot::isAlliedWithHuman() const {
    if (!getHouse()) return false;
    for (int h=0; h<NUM_HOUSES; ++h) if (const auto* house=getHouse(h)) {
        if (house->getTeamID()!=getHouse()->getTeamID()) continue;
        for (const auto& player : house->getPlayerList())
            if (dynamic_cast<const HumanPlayer*>(player.get())) return true;
    }
    return false;
}

// Original permissions and budget are independent of later construction or spice loss.
bool QuantBot::campaignCityEconomy() const {
    return isCampaignEnemy() && currentGame->isCitySimEnabled()
        && currentGame->getGameInitSettings().getModName() == "dunecity"
        && currentGame->getCitySimulation() && currentGame->getCitySimulation()->isInitialized();
}

void QuantBot::noteCampaignOriginalState(bool legacySave) {
    campaignOriginalStructures.clear();
    for (Uint32 item=ItemID_FirstID; item<=ItemID_LastID; ++item)
        if (isStructure(item) && initialItemCount[item]>0) campaignOriginalStructures.insert(item);
    int refineries = initialItemCount[Structure_Refinery];
    // Old saves contain AI-added permissions in initialItemCount. Recover the
    // authored named-house scenario where available, including destroyed types.
    if (legacySave) {
        try {
            const auto& setup = getGameInitSettings();
            auto file = setup.getFiledata().empty() ? pFileManager->openCampaignFile(setup.getFilename())
                : sdl2::RWops_ptr(SDL_RWFromConstMem(setup.getFiledata().data(),int(setup.getFiledata().size())));
            INIFile scenario(file.get());
            if (scenario.hasSection("STRUCTURES")) {
                std::set<Uint32> types; int authoredRefineries=0; bool namedHouses=false;
                for (const auto& key:scenario.getSection("STRUCTURES")) {
                    std::string houseName,building,health,position;
                    const bool generic=key.getKeyName().compare(0,3,"GEN")==0;
                    if (generic) {
                        if (!splitString(key.getStringValue(),houseName,building)) continue;
                    } else if (key.getKeyName().compare(0,2,"ID")!=0
                        || !splitString(key.getStringValue(),houseName,building,health,position)) continue;
                    const int house = getHouseByName(houseName);
                    if (house<0 || house>=NUM_HOUSES) continue;
                    namedHouses=true;
                    if (house!=getHouse()->getHouseID()) continue;
                    const auto item=getItemIDByName(building);
                    if (!isStructure(item) || !currentGame->objectData.data[item][house].enabled) continue;
                    if (!generic || item==Structure_Wall) types.insert(item);
                    if (item==Structure_Refinery) ++authoredRefineries;
                }
                if (namedHouses) {campaignOriginalStructures=std::move(types);refineries=authoredRefineries;}
            }
        } catch (const std::exception&) {
            // Custom/removed older content retains its saved permissions.
            logWarn("Cannot recover original campaign scenario; retaining legacy building permissions");
        }
    }
    campaignBaseline.refineries=refineries;
    const auto& settings=getQuantBotConfig().getSettings(int(difficulty));
    int allowedRefineries=difficulty==Difficulty::Hard && getGameInitSettings().getMission()>=21 ? 2 : refineries;
    allowedRefineries=std::max(allowedRefineries,settings.refineryMinimum);
    int allowance=std::max(0,settings.harvesterLimitPerRefineryMultiplier*allowedRefineries);
    if (const int ceiling=harvesterCountCeiling();ceiling>0) allowance=std::min(allowance,ceiling);
    if (const int ceiling=getHouse()->getMaxHarvesters();ceiling>0) allowance=std::min(allowance,ceiling);
    campaignBaseline.allowance=refineries>0 ? allowance : 0;
    campaignBaselineCaptured=true;
}

bool QuantBot::campaignPermitsStructure(Uint32 item) const {
    if (!campaignCityEconomy() || !isStructure(item)) return true;
    // An RTS building's city employment role is NOT permission to introduce it.
    if (isZoneStructure(item) || item==Structure_PoliceStation || item==Structure_NuclearPlant
        || item==Structure_Stadium || item==Structure_Airport
        || item==Structure_Road || item==Structure_Slab1 || item==Structure_Slab4) return true;
    return campaignBaselineCaptured ? campaignOriginalStructures.count(item)>0 : initialItemCount[item]>0;
}

bool QuantBot::campaignPostSpice() const {
    if (!campaignCityEconomy() || campaignMapHasSpice) return false;
    for (const auto* unit:getUnitList()) if (unit->getOwner()==getHouse() && unit->getHealth()>0)
        if (const auto* worker=dynamic_cast<const Harvester*>(unit);worker && worker->getAmountOfSpice()>0) return false;
    return QuantBotCityCampaignPolicy::postSpiceConfirmed(0,0,campaignSpiceZeroSince,
        getGameCycleCount(),MILLI2CYCLES(30000));
}

QuantBotCityCampaignPolicy::Limits QuantBot::campaignCityLimits() const {
    if (!campaignCityEconomy()) return {};
    return QuantBotCityCampaignPolicy::limits(int(difficulty),
        QuantBotCityCampaignPolicy::displayLevel(currentGame->techLevel),campaignBaseline,
        campaignPostSpice(),getHouse()->getMaxHarvesters());
}

int QuantBot::campaignHarvesterTarget() const {
    return campaignMapHasSpice ? campaignCityLimits().harvesters : 0;
}

int QuantBot::campaignHarvesterCeiling() const {
    if (!campaignCityEconomy() || !campaignBaselineCaptured) return -1;
    return std::min(std::max(0,harvesterLimit),campaignHarvesterTarget());
}

bool QuantBot::campaignCanAddHarvester() const {
    const int ceiling = campaignHarvesterCeiling();
    return ceiling < 0 || campaignCommittedCount(Unit_Harvester)
        + campaignCommittedCount(Unit_RebelHarvester) < ceiling;
}

int QuantBot::campaignCommittedCount(Uint32 item) const {
    int count=getHouse()->getNumItems(item);
    for (const auto* structure:getStructureList()) if (structure->getOwner()==getHouse())
        if (const auto* builder=dynamic_cast<const BuilderBase*>(structure))
            for (const auto& queued:builder->getBuildList()) if (queued.itemID==item) count+=queued.num;
    return count;
}

int QuantBot::campaignIncomeForecastPerMinute() const {
    if (!campaignCityEconomy()) return 0;
    const auto* sim=currentGame->getCitySimulation();
    const auto& state=sim->getHouseState(getHouse()->getHouseID());
    const int tax=sim->getCityTax(),land=state.avgLandValue>0 ? state.avgLandValue : 128;
    int income=DuneCity::computeAnnualTaxRevenue(state.taxBaseEighths,tax,land);
    // Forecast committed/developing zones using the actual tax and population
    // functions, never a universal credits-per-zone constant.
    for (Uint32 kind:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
        int expectedPopulation=std::max(0,campaignCommittedCount(kind)-getHouse()->getNumItems(kind))
            * DuneCity::getZonePopulation(kind,1);
        for (const auto* structure:getStructureList()) if(structure->getOwner()==getHouse() && structure->getItemID()==kind) {
            const auto pos=structure->getLocation();
            if (!getMap().tileExists(pos.x,pos.y)) continue;
            const int population=DuneCity::getStructurePopulation(structure,getMap().getTile(pos.x,pos.y)->getCityZoneDensity());
            expectedPopulation+=std::max(0,DuneCity::getZonePopulation(kind,1)-population);
        }
        income+=DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(kind,expectedPopulation,1),tax,land)/2;
    }
    income-=state.lastPoliceExpense;
    if (getHouse()->isPowerRequired()) income-=getHouse()->getPowerRequirement()/8;
    if (campaignMapHasSpice) {
        const int workers=std::max(campaignHarvesterTarget(),campaignCommittedCount(Unit_Harvester)+campaignCommittedCount(Unit_RebelHarvester));
        income+=workers*QuantBotCityCampaignPolicy::kReferenceIncomePerHarvester;
    }
    return std::max(0,income);
}

bool QuantBot::campaignAllowsZone(int zonesIncludingQueued) const {
    return !campaignCityEconomy() || QuantBotCityCampaignPolicy::allowsDiscretionaryExpansion(
        zonesIncludingQueued,campaignCityLimits().sharedZoneCap,campaignIncomeForecastPerMinute(),
        QuantBotCityCampaignPolicy::totalIncomeGoalPerMinute(campaignBaseline));
}

bool QuantBot::campaignAvailableToBuild(const BuilderBase* builder, Uint32 item) const {
    if (!builder->isAvailableToBuild(item) || !campaignPermitsStructure(item)) return false;
    if (item==Unit_MCV && !campaignPermitsStructure(Structure_ConstructionYard)) return false;
    if (campaignCityEconomy() && isZoneStructure(item))
        return campaignAllowsZone(campaignCommittedCount(Structure_ZoneResidential)
            +campaignCommittedCount(Structure_ZoneCommercial)+campaignCommittedCount(Structure_ZoneIndustrial));
    return true;
}

void QuantBot::doProduceItem(const BuilderBase* builder, Uint32 item) const {
    if (!campaignAvailableToBuild(builder,item)) return;
    if (campaignCityEconomy() && (item==Unit_Harvester || item==Unit_RebelHarvester)) {
        const int ceiling=campaignHarvesterCeiling();
        if (ceiling>=0 && campaignCommittedCount(Unit_Harvester)+campaignCommittedCount(Unit_RebelHarvester)>=ceiling) return;
    }
    Player::doProduceItem(builder,item);
}

int QuantBot::harvesterCountCeiling() const {
    return difficulty == Difficulty::Brutal && isCampaignEnemy() ? 7 : 0;
}

int QuantBot::campaignAllyHarvesterLimit() const {
    return currentGame && isCampaignGameType(currentGame->gameType)
        && difficulty == Difficulty::Brutal && isAlliedWithHuman()
        && currentGame->getGameInitSettings().getMission() >= CampaignControls::firstMission(8)
        && currentGame->getGameInitSettings().getGameOptions().maximumNumberOfHarvestersOverride < 0
        ? 20 : 0;
}

bool QuantBot::isCampaignEnemy() const {
    return currentGame && isCampaignGameType(currentGame->gameType)
        && !supportMode && difficulty != Difficulty::Defend && !isAlliedWithHuman();
}

CampaignDifficultyPolicy::Profile QuantBot::campaignProfile() const {
    return CampaignDifficultyPolicy::profile(static_cast<int>(difficulty),currentGame->techLevel);
}

bool QuantBot::campaignCombatUnit(const UnitBase* unit) const {
    return unit && unit->getOwner()==getHouse() && unit->getHealth()>0
        && unit->canAttack() && unit->getItemID()!=Unit_Saboteur
        && unit->getItemID()!=Unit_Harvester && unit->getItemID()!=Unit_Sandworm
        && unit->getItemID()!=Unit_MCV && unit->getItemID()!=Unit_Carryall;
}

bool QuantBot::reserveDamagedUnitForRepair(const UnitBase* unit) const {
    // Only a unit that can actually be repaired may be withheld for repair.
    // UnitBase::doRepair is empty and only GroundUnit drives to a repair yard,
    // so reserving a damaged aircraft removes it from every later order and
    // never heals it. Damaged aircraft remain available for combat.
    if (unit->isAFlyingUnit()) return false;
    // A deviated unit is refused by our repair yards for as long as we hold it, so reserving it
    // has the same effect as reserving an aircraft: withdrawn from every order, never healed.
    if (!unit->isEligibleForRepair()) return false;
    // Easy/Medium campaign troops cannot recover at home without a repair yard.
    // Keep them available for combat instead of withdrawing and excluding them
    // from every later wave. Explicit retreat/manual orders remain protected.
    return unit->isBadlyDamaged()
        && !(currentGame && isCampaignGameType(currentGame->gameType)
            && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium) && !getHouse()->hasRepairYard());
}

CampaignDifficultyPolicy::Pressure QuantBot::campaignPressure() const {
    CampaignDifficultyPolicy::Pressure result;
    result.lastActive=campaignWave.lastActive;
    for (auto id : campaignWave.members) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(id));
        // Include transported survivors so pickup cannot free an assault slot.
        if (!campaignCombatUnit(unit)) continue;
        ++result.units;
        result.value+=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
    }
    result.houses=result.units>0 ? 1 : 0;
    return result;
}

int QuantBot::campaignRequiredArmy(int configuredThreshold) const {
    int required = std::max(0, configuredThreshold);
    // Once the map is depleted, stop waiting for a larger army. A leftover
    // cash balance or a few cheap survivors must not keep the game idle.
    // Dispatch still enforces home reserves, opening and enemy wave limits.
    // Delay this fallback beyond the initial spice scan and opening phase.
    if (getGameCycleCount() >= MILLI2CYCLES(15 * 60000) && lastCalculatedSpice == 0)
        required = 0;
    return required;
}

bool QuantBot::campaignCanLaunch() const {
    if (!isCampaignEnemy()) return true;
    // Each successful Easy/Medium/Hard dispatch starts its next-wave countdown.
    // Surviving attackers keep fighting but never block a later ready wave.
    return campaignWave.initialized && getGameCycleCount() >= campaignWave.opening
        && (!campaignProfile().limitedWave || attackTimer <= 0);
}

bool QuantBot::campaignLocalContact(const ObjectBase* target) const {
    if (!target || target->getHealth()<=0 || !target->isActive()) return false;
    // Artillery can fire from beyond the old seven-tile perimeter. Include its
    // firing range so a base cannot be shelled without its defenders responding.
    const int radius=std::max(difficulty<=Difficulty::Medium ? 7 : 10,target->getWeaponRange()+2);
    for (const auto* building : getStructureList())
        if (building->getOwner()==getHouse() && building->getHealth()>0
            && blockDistance(target->getLocation(),building->getClosestPoint(target->getLocation()))<=radius) return true;
    // Workers need protection wherever the spice field is. The contact remains
    // bounded around the worker, including an artillery attacker's firing range.
    for (const auto* worker : getUnitList())
        if (worker->getOwner()==getHouse() && worker->getItemID()==Unit_Harvester && worker->isActive()
            && blockDistance(target->getLocation(),worker->getLocation())<=std::max(4,target->getWeaponRange()+2)) return true;
    return false;
}

bool QuantBot::campaignDefensiveContact(const UnitBase* unit, const ObjectBase* target) const {
    if (!target || target->getHealth()<=0 || !target->isActive()) return false;
    if (campaignLocalContact(target)) return true;
    // A lone defender also fights back when shot outside the base perimeter.
    // Anchor this response to where it was hit, rather than authorizing a chase
    // across the map. Existing assignment/guard-point state survives save/load.
    const auto assigned=defenceAssignments.find(unit->getObjectID());
    return assigned!=defenceAssignments.end() && assigned->second==target->getObjectID()
        && blockDistance(target->getLocation(),unit->getGuardPoint())
            <= std::max(unit->getWeaponRange(),target->getWeaponRange())+2;
}

void QuantBot::onScriptedReinforcement(const UnitBase* unit) {
    if (!isCampaignEnemy() || !campaignCombatUnit(unit)) return;
    // Authored attacks keep their orders independently of automatic wave slots
    // and timers. Register cargo now so transit and save/load preserve intent.
    scriptedAssaults.insert(unit->getObjectID());
    traceDecision("campaign_scripted_assault",AITelemetry::Record()
        .set("unit",unit->getObjectID()).set("item",unit->getItemID())
        .set("active",unit->isActive()));
}

void QuantBot::holdCampaignUnit(const UnitBase* unit) {
    if (!campaignCombatUnit(unit) || !unit->isActive() || !unit->isRespondable() || unit->getAttackMode()==RETREAT) return;
    const StructureBase* home=nullptr; int distance=INT32_MAX;
    for (const auto* building : getStructureList()) {
        if (building->getOwner()!=getHouse() || building->getHealth()<=0) continue;
        int d=blockDistance(unit->getLocation(),building->getLocation()).lround();
        if (d<distance) {home=building;distance=d;}
    }
    const Coord point=home ? home->getClosestPoint(unit->getLocation()) : unit->getLocation();
    defenceAssignments.erase(unit->getObjectID());
    if (unit->hasATarget()) doMove2Pos(unit,unit->getX(),unit->getY(),false);
    if (unit->hasATarget() || unit->getAttackMode()!=AREAGUARD) doSetAttackMode(unit,AREAGUARD);
    const_cast<UnitBase*>(unit)->setGuardPoint(point);
    if (home && distance>6 && (!unit->isMoving() || !unit->wasForced()))
        doMove2Pos(unit,point.x,point.y,true);
}

void QuantBot::updateCampaignWave() {
    if (!isCampaignEnemy()) return;
    const auto now=getGameCycleCount();
    const auto profile=campaignProfile();
    if (!campaignWave.initialized) {
        // The map's first offensive enemy reinforcement is the opening signal.
        // Human deliveries, home reinforcements and economic cargo do not count.
        Uint32 anchor=std::numeric_limits<Uint32>::max();
        for (const auto& trigger : currentGame->getTriggerManager().getTriggers()) {
            const auto* reinforcement=dynamic_cast<const ReinforcementTrigger*>(trigger.get());
            if (!reinforcement || reinforcement->getDropLocation()==Drop_Homebase) continue;
            const auto* house=getHouse(reinforcement->getHouseID());
            if (!house || house->getTeamID()!=getHouse()->getTeamID()) continue;
            bool enemy=false;
            for (const auto& player : house->getPlayerList())
                if (const auto* bot=dynamic_cast<const QuantBot*>(player.get());bot && bot->isCampaignEnemy()) enemy=true;
            if (!enemy) continue;
            bool combat=false;
            for (auto item : reinforcement->getDroppedUnits())
                combat |= item!=Unit_Harvester && item!=Unit_RebelHarvester
                    && item!=Unit_Carryall && item!=Unit_MCV && item!=Unit_Sandworm;
            if (combat) anchor=std::min(anchor,reinforcement->getCycleNumber());
        }
        // The original first two levels have no reinforcement entries. Give
        // their existing troops an explicit four-minute opening signal rather
        // than adding free units. Easy/Medium then stagger within minutes 4–6.
        const auto& setup=getGameInitSettings();
        const bool earlyCoreMap=setup.getMission()>=1 && setup.getMission()<=4
            && (setup.getModName()=="vanilla" || setup.getModName()=="dunecity");
        const bool earlySignal=anchor==std::numeric_limits<Uint32>::max() && earlyCoreMap;
        if (earlySignal) anchor=MILLI2CYCLES(4*60000);
        // An unknown missing trigger must never authorize an immediate attack.
        if (anchor==std::numeric_limits<Uint32>::max()) {
            campaignWave.opening=anchor;campaignWave.initialized=true;
            attackTimer=std::numeric_limits<Sint32>::max();
            traceDecision("campaign_opening_missing_trigger",AITelemetry::Record());
            return;
        }
        const auto delay=CampaignDifficultyPolicy::openingDelayMs(static_cast<int>(difficulty),
            getGameInitSettings().getRandomSeed(),anchor,getHouse()->getHouseID());
        campaignWave.opening=anchor+MILLI2CYCLES(delay);
        campaignWave.initialized=true;
        attackTimer=campaignWave.opening>now ? campaignWave.opening-now : 0;
        traceDecision("campaign_opening",AITelemetry::Record().set("trigger_cycle",anchor)
            .set("delay_ms",delay).set("opening_cycle",campaignWave.opening)
            .set("early_map_signal",earlySignal));
    }
    // Easy/Medium retain their saved break between waves. Hard/Brutal use
    // readiness alone after the opening signal.
    if (now>=campaignWave.opening && !profile.limitedWave) attackTimer=0;
    for(auto it=scriptedAssaults.begin();it!=scriptedAssaults.end();) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(*it));
        if (!campaignCombatUnit(unit)) it=scriptedAssaults.erase(it);
        else if (reserveDamagedUnitForRepair(unit) || unit->getAttackMode()==RETREAT) {
            holdCampaignUnit(unit);it=scriptedAssaults.erase(it);
        } else ++it;
    }
    const bool hadWave=!campaignWave.members.empty();
    for (auto it=campaignWave.members.begin();it!=campaignWave.members.end();) {
        const auto* unit=dynamic_cast<const UnitBase*>(getObject(*it));
        if (!campaignCombatUnit(unit)) {it=campaignWave.members.erase(it);continue;}
        if (reserveDamagedUnitForRepair(unit) || unit->getAttackMode()==RETREAT
            || (unit->isActive() && !unit->hasATarget() && !unit->isMoving()
                && now-campaignWave.launched>=MILLI2CYCLES(profile.sortieMs))) {
            holdCampaignUnit(unit); it=campaignWave.members.erase(it); continue;
        }
        ++it;
    }
    if (hadWave || !campaignWave.members.empty()) campaignWave.lastActive=now;
    if (hadWave && campaignWave.members.empty()) {
        traceDecision("campaign_wave_end",AITelemetry::Record().set("next_attack_in_cycles",std::max(0,attackTimer)));
    }
    for (const auto* unit : getUnitList()) if (campaignCombatUnit(unit) && unit->isActive()
        && !campaignWave.members.count(unit->getObjectID())
        && !scriptedAssaults.count(unit->getObjectID())) {
        // Authored offensive arrivals are registered at the trigger. Only
        // unassigned troops and autonomous defensive pursuit are held here.
        if (unit->getAttackMode()==HUNT || (unit->hasATarget() && !campaignDefensiveContact(unit,unit->getTarget())))
            holdCampaignUnit(unit);
    }
}

const ObjectBase* QuantBot::campaignObjective(const UnitBase* unit, int group) const {
    const ObjectBase* chosen=nullptr; int best=INT32_MAX;
    for (const auto* building : getStructureList()) {
        if (building->getOwner()->getTeamID()==getHouse()->getTeamID() || building->getHealth()<=0
            || !building->isVisible(getHouse()->getTeamID())) continue;
        int score=blockDistance(unit->getLocation(),building->getLocation()).lround()*10;
        if (difficulty>=Difficulty::Hard && group>0
            && (building->getItemID()==Structure_Refinery || building->getItemID()==Structure_HeavyFactory)) score-=100;
        if (difficulty==Difficulty::Brutal && group>0)
            score+=(building->getOwner()->getHouseID()+getHouse()->getHouseID()+group)%3*30;
        if (score<best) {best=score;chosen=building;}
    }
    return chosen;
}

bool QuantBot::scoutCampaignFront(const UnitBase* unit) {
    // HUNT can repeatedly acquire a distant carryall, which ground units then
    // refuse to chase. Explore terrain when there is no visible base instead
    // of treating HUNT alone as an exploration order. No hidden objects are read.
    if (!unit->isAGroundUnit() || !unit->isActive() || !unit->isRespondable()
        || unit->isMoving() || unit->hasATarget() || reserveDamagedUnitForRepair(unit)
        || unit->getAttackMode()==RETREAT || humanControls(unit)) return false;
    Coord destination=Coord::Invalid();int best=-1;
    for (int y=0;y<getMap().getSizeY();++y) for (int x=0;x<getMap().getSizeX();++x) {
        if (getMap().getTile(x,y)->isExploredByTeam(getHouse()->getTeamID()) || !unit->canPass(x,y)) continue;
        const int distance=blockDistance(unit->getLocation(),Coord(x,y)).lround();
        if (distance>best) {best=distance;destination=Coord(x,y);}
    }
    if (!destination.isValid()) return false;
    doMove2Pos(unit,destination.x,destination.y,true);
    doSetAttackMode(unit,HUNT);
    traceDecision("campaign_scout",AITelemetry::Record().set("unit",unit->getObjectID())
        .set("x",destination.x).set("y",destination.y));
    return true;
}

bool QuantBot::campaignControlsUnit(const UnitBase* unit) {
    if (!campaignCombatUnit(unit)) return false;
    if (!isCampaignEnemy()) {
        // Only an already dispatched helper attacker can scout. Home guards,
        // economy-only support and manual orders keep their existing roles.
        if (isCampaignGameType(currentGame->gameType) && !supportMode
            && unit->isActive() && unit->isRespondable() && !reserveDamagedUnitForRepair(unit)
            && !humanControls(unit) && unit->getAttackMode()==HUNT
            && !unit->isMoving() && !unit->hasATarget()) {
            if (const auto* objective=campaignObjective(unit,0)) {
                doAttackObject(unit,objective,true);
                return true;
            }
            return scoutCampaignFront(unit);
        }
        return false;
    }
    if (!campaignWave.members.count(unit->getObjectID()) && !scriptedAssaults.count(unit->getObjectID())) {
        if (!campaignDefensiveContact(unit,unit->getTarget())) {holdCampaignUnit(unit);return true;}
        return false; // Defenders retain combat micro.
    }
    if (!unit->isActive() || !unit->isRespondable() || reserveDamagedUnitForRepair(unit)) return true;
    // Ordinary combat micro may respond to close threats. Only redirect idle
    // survivors; never override an evasive move or a repair order.
    if (unit->isAGroundUnit() && !unit->hasATarget() && !unit->isMoving()) {
        const auto& members=scriptedAssaults.count(unit->getObjectID()) ? scriptedAssaults : campaignWave.members;
        const auto index=std::distance(members.begin(),members.find(unit->getObjectID()));
        const int group=difficulty>=Difficulty::Hard ? index%2 : 0;
        const auto* objective = group==0 ? getObject(campaignWave.front) : nullptr;
        if (!objective || objective->getHealth()<=0 || objective->getOwner()->getTeamID()==getHouse()->getTeamID()) {
            objective=campaignObjective(unit,group);
            if (group==0) campaignWave.front=objective ? objective->getObjectID() : NONE_ID;
        }
        if (objective) {
            doSetAttackMode(unit,AREAGUARD);
            doAttackObject(unit,objective,true);
        } else if (!scoutCampaignFront(unit)) doSetAttackMode(unit,HUNT);
    }
    return false;
}
