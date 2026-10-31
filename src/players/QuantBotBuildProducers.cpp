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

// QuantBot build pass: one producer's turn. The shared builder rules (construction recovery,
// the bulk-slab upgrade, campaign power and repair capacity, the cash reserve and capital
// purchases) run first, then the handler for the producer's type. Construction yards are
// in QuantBotBuildYard.cpp.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>

void QuantBot::buildRepair(BuildPass& pass, const StructureBase* pStructure) {
    const int creditsBeforeUpgrade = getHouse()->getCredits();
    if (doUpgradeScoutpost(pStructure, pass.money)) {
        const int cost = creditsBeforeUpgrade - getHouse()->getCredits();
        pass.money -= cost;
        pass.capitalOrderedCost += cost;
        --pass.itemCount[Structure_Scoutpost];
        ++pass.itemCount[Structure_Slowpost];
    }
    // Condition is production, generation, land value and armour, so
    // anything whose output follows its health — every factory, the
    // yard itself, refineries, the whole windtrap family and the city
    // reactor — is repaired as soon as the engine can be paid at all,
    // as is everything the engine's integer formula repairs for free.
    // City mode extends that to the entire colony, walls included,
    // because a damaged footprint lowers its own land value. The rest
    // waits for a comfortable treasury, and survival and engaged
    // emplacements keep their emergency repair on top of all of it.
    if (!pStructure->isRepairing() && pStructure->getHealth() > 0
        && pStructure->getHealth() < pStructure->getMaxHealth()
        && QuantBotBuildPolicy::canAffordRepairTick(getHouse()->getCredits())) {
        const Uint32 repairItem = pStructure->getItemID();
        const bool maintained = repairPolicyMaintains(pass, pStructure);
        const bool survival = pStructure->getHealth() < pStructure->getMaxHealth() * 0.40_fix
            && pass.money > 1000;
        const bool engaged = QuantBotBuildPolicy::defensiveEmplacement(repairItem)
            && pStructure->hasATarget();
        const bool wealthy = QuantBotBuildPolicy::repairWhenWealthy(pass.money);
        if (maintained || survival || engaged || wealthy) {
            doRepair(pStructure);
            if (AITelemetry::log().enabled())
                traceDecision("structure_repair", AITelemetry::Record()
                    .set("object", pStructure->getObjectID()).set("item", repairItem)
                    .set("health", pStructure->getHealth().lround())
                    .set("max_health", pStructure->getMaxHealth())
                    .set("credits", getHouse()->getCredits()).set("spendable", pass.money)
                    .set("reason", maintained ? "condition_is_output"
                        : survival ? "survival" : engaged ? "engaged_defence" : "wealth"));
        }
    }
}

void QuantBot::buildProducer(BuildPass& pass, const StructureBase* pStructure) {
    BuilderTurn turn(pass.money);
    turn.pStructure = pStructure;
    turn.pBuilder = static_cast<const BuilderBase*>(pStructure);
    beginBuilderTurn(pass, turn);
    if (recoverBuilderConstruction(pass, turn)) return;
    if (upgradeYardForConcrete(pass, turn)) return;
    if (queueCampaignWindtrap(pass, turn, 0)) return;
    if (buildCampaignRepairYard(pass, turn)) return;
    reserveBuilderCash(pass, turn);
    if (buildCapitalUnits(pass, turn)) return;

    switch (pStructure->getItemID()) {
    case Structure_LightFactory: buildLightFactory(pass, turn); break;
    case Structure_WOR: buildWOR(pass, turn); break;
    case Structure_Worfinery:
        if (ModManager::instance().isTornieFamilyContentActive()) buildWOR(pass, turn);
        break;
    case Structure_Barracks: buildBarracks(pass, turn); break;
    case Structure_HighTechFactory: buildHighTechFactory(pass, turn); break;
    case Structure_HeavyFactory: buildHeavyFactory(pass, turn); break;
    case Structure_StarPort: buildStarPort(pass, turn); break;
    case Structure_ConstructionYard: buildConstructionYard(pass, turn); break;
    }
}

void QuantBot::beginBuilderTurn(BuildPass& pass, BuilderTurn& turn) {
    // Retire old-save orders that introduce an unauthored RTS type.
    // Copy the list because cancellation mutates its queue counts.
    if (campaignCityEconomy()) {
        const auto queued = turn.pBuilder->getBuildList();
        for (const auto& order : queued)
            if (!campaignPermitsStructure(order.itemID))
                for (int n = 0; n < order.num; ++n) doCancelItem(turn.pBuilder, order.itemID);
    }

    planningBuilder = turn.pBuilder->getObjectID();
    clearPlacementCache(false,true);

    // Log all builder status for campaign AIs (not just CY)
    if (gameMode == GameMode::Campaign && !builderOnly() && turn.pStructure->getItemID() != Structure_ConstructionYard) {
        logDebug("PRODUCTION: %s - Upgrading:%d Queue:%d Credits:%d",
            getItemNameByID(turn.pStructure->getItemID()).c_str(),
            turn.pBuilder->isUpgrading(), turn.pBuilder->getProductionQueueSize(), pass.money);
    }

    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("producer_status", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
            .set("item", turn.pBuilder->getItemID()).set("queue", turn.pBuilder->getProductionQueueSize())
            .set("current_item", turn.pBuilder->getCurrentProducedItem()).set("hold", turn.pBuilder->isOnHold())
            .set("upgrading", turn.pBuilder->isUpgrading()).set("waiting_to_place", turn.pBuilder->isWaitingToPlace())
            .set("health", turn.pBuilder->getHealth().lround()).set("max_health", turn.pBuilder->getMaxHealth())
            .set("progress_credits", turn.pBuilder->getProductionProgress().lround())
            .set("unit_limit_blocked", turn.pBuilder->isUnitLimitReached(turn.pBuilder->getCurrentProducedItem()))
            .set("military_limit_blocked", pass.militaryValue >= pass.militaryBudget)
            .set("military_budget", pass.militaryBudget)
            .set("power_deficit", !getHouse()->hasPower())
            .set("x", turn.pBuilder->getLocation().x).set("y", turn.pBuilder->getLocation().y)
            .set("credits", getHouse()->getCredits()));
    }
    // Correlate only decisions made for this builder in this planning pass.
    zoneDecisionIds.erase(turn.pBuilder->getObjectID());

    // The one item this builder pass may queue on a production start
    // budget instead of its full price, and the budget it needs. Set
    // only by the growth-cap rules below, and only after live demand,
    // mode, tech, placement, prerequisites, the no-existing-or-pending
    // copy check and a positive income test have all passed. Any other
    // item keeps the shared-cash budget guard unchanged.
    turn.installmentProject = NONE_ID;
    turn.installmentProjectStartBudget = 0;
    turn.emergencyFoundationOrder = false;
}

// Record actual queue acceptance for unit and structure production.
bool QuantBot::produceItemWithLogging(BuildPass& pass, BuilderTurn& turn, Uint32 itemID, int sourceLine, const char* rule) {
    const Uint32 requestedItem = itemID;
    if (itemID == Unit_Harvester) itemID = chooseTornieHarvester(turn.pBuilder, pass.money);
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if(!cityAdmitsStructure(pass, itemID)) return false;
    if (itemID==Structure_RepairYard && !canAddRepairYard(pass.itemCount[Structure_RepairYard])) return false;
    // Also cover campaign rebuild orders, which bypass the strategic planner.
    if (itemID==Structure_Palace && pass.itemCount[Structure_Palace] >=
        QuantBotBuildPolicy::difficultyPalaceCap(static_cast<int>(difficulty))) return false;
    if(itemID == Unit_MCV && pass.yardLimit > 0
        && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] >= pass.yardLimit) return false;
    const int quotedPrice=purchasePrice(turn.pBuilder,itemID);
    if (itemID==Unit_MCV && !canFundCapacityMcv(pass, quotedPrice)) {
        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
            .set("builder",turn.pBuilder->getObjectID()).set("item",itemID).set("rule",rule)
            .set("price",quotedPrice).set("projected_cash",pass.cashFlow.projectedCash)
            .set("capacity_mcv_ordered_cost",pass.capacityMcvOrderedCost).set("cash_buffer",pass.cashBuffer)
            .set("reason","existing_production_runway"));
        return false;
    }
    // Enforce reactive infantry and its funded quota at queue acceptance,
    // including callers outside the WOR selection branch.
    if (cityInfantryItem(pass, itemID) && (!pass.cityAntiAirInfantryDemand
        || !infantryQuotaAdmits(pass, data[itemID][houseID].price)
        || int64_t(quotedPrice) + pass.cityWorkingReserve > pass.money)) return false;
    if (pass.citySimEnabled && itemID == Structure_WOR
        && (!pass.cityAntiAirInfantryDemand || !cityTroopContentExists(pass)
            || openingWorkersNeeded(pass)
            || int64_t(quotedPrice) + pass.cityWorCashReserve > pass.money)) return false;
    const bool emergencyGenerator=!getHouse()->hasPower()
        && (itemID==Structure_WindTrap || itemID==Structure_NuclearPlant
            || (turn.emergencyFoundationOrder
                && (itemID==Structure_Slab1 || itemID==Structure_Slab4)));
    // A validated growth-cap project is charged by the engine in
    // installments out of arriving income, so the shared cash
    // budget asks it for its production start budget instead of
    // its price. The obligation is still booked in full below, so
    // nothing else spends the same credits.
    const bool installment = itemID!=NONE_ID && itemID==turn.installmentProject;
    const int budgetFloor = installment ? turn.installmentProjectStartBudget : quotedPrice;
    if (!emergencyGenerator && pass.money<budgetFloor) {
        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
            .set("builder",turn.pBuilder->getObjectID()).set("item",itemID).set("rule",rule)
            .set("price",quotedPrice).set("spendable",pass.money)
            .set("budget_floor",budgetFloor).set("installments",installment)
            .set("reason",installment ? "installment_start_budget" : "shared_cash_budget"));
        return false;
    }

    if (gameMode == GameMode::Campaign && !builderOnly() && currentGame) {
        std::string itemName = getItemNameByID(itemID);
        logDebug("Queuing %s (ID:%d)", itemName.c_str(), itemID);
    }
    Coord nuclearSite=Coord::Invalid();
    if (itemID==Structure_NuclearPlant) {
        // Campaign rebuild/power orders bypass the general site
        // reservation path. Recheck and reserve at queue acceptance.
        placementCache.erase(itemID);
        nuclearSite=findPlaceLocation(itemID);
        if (!nuclearSite.isValid()) {
            traceDecision("construction_rejected",AITelemetry::Record()
                .set("builder",planningBuilder).set("item",itemID).set("reason","no_nuclear_site"));
            return false;
        }
    }
    const int before = turn.pBuilder->getProductionQueueSize();
    const auto preOrder = AITelemetry::log().enabled() ? decisionState(pass) : AITelemetry::Record();
    doProduceItem(turn.pBuilder, itemID);
    const bool accepted = turn.pBuilder->getProductionQueueSize() > before;
    if (accepted) {
        if (itemID != requestedItem) ++pass.itemCount[itemID];
        pass.cityPendingDisplayPopulation += QuantBotCityPolicy::displayPopulation(initialCityPopulation(itemID));
        if(DuneCity::isCityZoneStructure(itemID)) ++pass.cityZonesIncludingQueued;
        if (itemID>=Structure_FirstID && itemID<=Structure_LastID)
            pass.cityStorageCapacityIncludingQueued+=std::max(0,data[itemID][houseID].capacity);
        pass.money-=quotedPrice;
        pass.capitalOrderedCost+=quotedPrice;
        // Ordinary unit/zone orders are already forecast above;
        // only new optional MCV capital consumes this headroom.
        if (pass.capacityMcvRunway && itemID==Unit_MCV) pass.capacityMcvOrderedCost+=quotedPrice;
        if (AITelemetry::log().enabled()) pass.capitalOrders.set(std::to_string(turn.pBuilder->getObjectID())+":"+std::to_string(before),
            AITelemetry::Record().set("item",itemID).set("price",quotedPrice).set("rule",rule));
    }
    if (accepted && capitalPending(pass) && (requestedItem==pass.capitalCandidates[pass.capitalChoice].item
        || (pass.cityGrowthProtected && pass.capitalCandidates[pass.capitalChoice].builder==turn.pBuilder->getObjectID()
            && DuneCity::isCityZoneStructure(itemID))))
        pass.capitalConsumed=true;
    if (accepted && nuclearSite.isValid()) {
        reservedStructures[planningBuilder]={itemID,nuclearSite};
        auto& sites=builderPlaceLocations[planningBuilder];
        if (sites.empty()) sites.push_back(nuclearSite);
        clearPlacementCache();
        traceDecision("site_reserved",AITelemetry::Record().set("builder",planningBuilder)
            .set("item",itemID).set("x",nuclearSite.x).set("y",nuclearSite.y));
    }
    if (AITelemetry::log().enabled()) traceDecision("production_order", AITelemetry::Record().set("capital_plan",pass.capitalDecision)
        .set("builder", turn.pBuilder->getObjectID()).set("builder_item", turn.pBuilder->getItemID())
        .set("item", itemID).set("item_name", getItemNameByID(itemID)).set("accepted", accepted)
        .set("source_line", sourceLine).set("rule", rule).set("queue_before", before).set("queue_after", turn.pBuilder->getProductionQueueSize())
        .set("quoted_price", quotedPrice)
        .set("zone_decision", itemID >= Structure_ZoneResidential && itemID <= Structure_ZoneIndustrial ? zoneDecisionIds[turn.pBuilder->getObjectID()] : 0)
        .set("state", preOrder));
    if (!accepted && pass.emitStatsLog) {
        logDebug("PRODUCTION: builder=%u rejected item=%u credits=%d", turn.pBuilder->getObjectID(), itemID, pass.money);
    }
    return accepted;
}

bool QuantBot::recoverBuilderConstruction(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Recover construction on every difficulty before withholding
    // optional economy/civic/capital reserves. Count paid cargo and
    // factory queues as MCVs so another port cannot duplicate it.
    // A sold-out catalogue entry can restock; save rather than spend
    // the recovery cash on workers or military in the meantime.
    const bool starportRecovery = pass.itemCount[Structure_ConstructionYard] == 0
        && pass.itemCount[Unit_MCV] == 0 && pass.itemCount[Structure_StarPort] > 0
        && data[Unit_MCV][houseID].enabled
        && getHouse()->getChoam().getNumAvailable(Unit_MCV) >= 0;
    if (starportRecovery) {
        if (turn.pBuilder->getItemID() == Structure_StarPort) {
            const auto* port = static_cast<const StarPort*>(turn.pBuilder);
            if (port->okToOrder() && port->isAvailableToBuild(Unit_MCV)
                && getHouse()->getChoam().getNumAvailable(Unit_MCV) > 0) {
                // Existing factory queues pay gradually. Recovery may
                // use that still-unspent cash, but never actual money
                // already charged by another order in this pass.
                const int heldCash = std::max(0, getHouse()->getCredits() - pass.money);
                pass.money += heldCash;
                const bool accepted = produceItemWithLogging(pass, turn, Unit_MCV, __LINE__, "starport_construction_recovery");
                pass.money -= heldCash;
                if (accepted) {
                    ++pass.itemCount[Unit_MCV];
                    doPlaceOrder(port);
                }
            }
        }
        return true;
    }
    return false;
}

bool QuantBot::upgradeWithLogging(BuildPass& pass, BuilderTurn& turn, int sourceLine) {
    const int price=turn.pBuilder->getUpgradeCost();
    const bool affordable=pass.money>=price;
    const bool accepted=affordable && doUpgrade(turn.pBuilder);
    if (accepted) { pass.money-=price; pass.capitalOrderedCost+=price; }
    traceDecision("capital_upgrade",AITelemetry::Record().set("capital_plan",pass.capitalDecision)
        .set("builder",turn.pBuilder->getObjectID()).set("price",price).set("accepted",accepted)
        .set("reason",!affordable ? "shared_cash_budget" : accepted ? "technology_unlock" : "unavailable")
        .set("source_line",sourceLine));
    return accepted;
}

// Campaign reconstruction and power orders do not pass through
// the standard construction selection below. Give them the same
// founded queue and per-yard site reservation.
bool QuantBot::queueFoundedStructure(BuildPass& pass, BuilderTurn& turn, Uint32 item, int sourceLine, const char* rule) {
    if (turn.pBuilder->getItemID()!=Structure_ConstructionYard
        || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0
        || !campaignAvailableToBuild(turn.pBuilder,item) || !cityAdmitsStructure(pass, item)) return false;
    planningBuilder=turn.pBuilder->getObjectID();
    clearPlacementCache(false,true);
    const Coord site=item==Structure_RocketTurret || item==Structure_GunTurret
        ? findEffectiveTurretPlaceLocation(item) : findPlaceLocation(item);
    if (!site.isValid()) return false;
    // Legacy campaign reconstruction and power orders share the
    // one foundation rule: no site it cannot prepare in full.
    const auto plan=foundationPlan(turn.pBuilder,item,site);
    if(!plan.complete) {
        traceDecision("construction_rejected",AITelemetry::Record()
            .set("builder",planningBuilder).set("item",item).set("rule",rule)
            .set("reason","foundation_incomplete"));
        return false;
    }
    int total=purchasePrice(turn.pBuilder,item);
    for(const auto& foundation:plan.orders)total+=purchasePrice(turn.pBuilder,foundation.item);
    if(pass.money<total) return false;
    auto& places=builderPlaceLocations[planningBuilder];
    places.clear();
    // Concrete this building will not stand on is wasted cash.
    // If any part of the sequence is refused, give it all back.
    auto abandonFoundation=[&](size_t laid) {
        for(size_t i=0;i<laid;++i) doCancelItem(turn.pBuilder,plan.orders[i].item);
        places.clear();
        return false;
    };
    for(size_t i=0;i<plan.orders.size();++i) {
        if(!produceItemWithLogging(pass, turn, plan.orders[i].item,sourceLine,"building_foundation"))
            return abandonFoundation(i);
        places.push_back(Coord(plan.orders[i].x,plan.orders[i].y));
    }
    const auto priorSites=places.size();
    if(!produceItemWithLogging(pass, turn, item,sourceLine,rule))
        return abandonFoundation(plan.orders.size());
    if(places.size()==priorSites)places.push_back(site); // Nuclear may reserve in the acceptance wrapper.
    reservedStructures[planningBuilder]={item,site};
    clearPlacementCache();
    return true;
}

bool QuantBot::upgradeYardForConcrete(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Bulk concrete comes before anything optional this yard could
    // buy, in every mode and at every difficulty, as soon as the
    // technology allows it. Every ordinary building now waits for a
    // full foundation, and the 2x2 slab is one order where Slab1
    // needs four — so income, reserve, power and defence
    // preconditions were all gating the unlock behind the buildings
    // it makes cheaper. Only the cost of the upgrade itself remains,
    // and a damaged yard repairs first so it can accept it. A yard
    // that cannot reach the level, or a tree without Slab4, simply
    // keeps founding with single slabs.
    const bool bulkSlabReachable =
        data[Structure_Slab4][houseID].enabled
        && data[Structure_Slab4][houseID].techLevel <= currentGame->techLevel
        && data[Structure_Slab4][houseID].builder == int(Structure_ConstructionYard)
        && campaignPermitsStructure(Structure_Slab4);
    auto upgradeForConcrete = [&]() {
        if (turn.pBuilder->getItemID()!=Structure_ConstructionYard
            || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0
            || !QuantBotBuildPolicy::bulkSlabUpgradePending(
                getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                turn.pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                turn.pBuilder->getMaxUpgradeLevel())) return false;
        // The upgrade is owed and reachable, so this yard spends the
        // pass on it and on nothing else — waiting for the credits or
        // for another yard's turn is still the upgrade's pass, not a
        // licence to buy something cheaper first.
        const bool anotherUpgrading = std::any_of(pass.capitalBuilders.begin(), pass.capitalBuilders.end(),
            [&](const auto* other) {
                return other != turn.pBuilder && other->getItemID() == Structure_ConstructionYard
                    && other->isUpgrading();
            });
        const int upgradeCost = turn.pBuilder->getUpgradeCost();
        auto hold = [&](const char* reason) {
            traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
                .set("builder", turn.pBuilder->getObjectID()).set("accepted", false)
                .set("cost", upgradeCost).set("spendable", pass.money)
                .set("level", turn.pBuilder->getCurrentUpgradeLevel())
                .set("reason", reason));
        };
        if (!QuantBotBuildPolicy::upgradeYardForBulkSlab(
                getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                turn.pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                turn.pBuilder->getMaxUpgradeLevel(), pass.money, upgradeCost, anotherUpgrading)) {
            if (anotherUpgrading) { hold("another_yard_unlocking_bulk_slab"); return true; }
            hold("saving_for_bulk_slab");
            return true;
        }
        if (turn.pBuilder->getHealth() < turn.pBuilder->getMaxHealth()) {
            if (!turn.pBuilder->isRepairing()) doRepair(turn.pBuilder);
            hold("repair_before_bulk_slab");
            return true; // The affordable upgrade owns this pass.
        }
        const bool accepted = upgradeWithLogging(pass, turn, __LINE__);
        traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
            .set("builder", turn.pBuilder->getObjectID()).set("accepted", accepted)
            .set("cost", upgradeCost).set("spendable", pass.money)
            .set("level", turn.pBuilder->getCurrentUpgradeLevel())
            .set("reason", "unlock_bulk_slab"));
        return accepted;
    };
    if (upgradeForConcrete()) return true;
    return false;
}

bool QuantBot::queueCampaignWindtrap(BuildPass& pass, BuilderTurn& turn, int nextDemand) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (!campaignPowerNeeded(pass, nextDemand) || turn.pBuilder->getItemID()!=Structure_ConstructionYard
        || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0
        || !campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap)) return false;
    const Coord site=findPlaceLocation(Structure_WindTrap);
    if (!site.isValid() || pass.money < data[Structure_WindTrap][houseID].price) return false;
    if (!queueFoundedStructure(pass, turn, Structure_WindTrap,__LINE__,"campaign_required_power")) return false;
    ++pass.itemCount[Structure_WindTrap];
    traceDecision("campaign_power",AITelemetry::Record().set("produced",getHouse()->getProducedPower())
        .set("required",getHouse()->getPowerRequirement()).set("next_demand",nextDemand));
    return true;
}

// Generation outside the city simulation is an investment, not
// a function. Vanilla's House::hasPower() answers true whatever
// the meters say, so this never asks it: it compares the raw
// requirement, plus the demand of everything already on order,
// with raw production plus the output that repairs in progress
// and queued generators will deliver, keeps a reserve on top of
// that, and puts the remaining package through the documented
// economics in QuantBotPowerInvestmentPolicy. A base running at
// 1800 against 1840 is one order away from another shortage, so
// being exactly powered is not the target. City power rules, the
// first prerequisite windtrap and the turret-power option are all
// untouched and still run ahead of this. Custom and legacy
// campaign games at every difficulty share the one gate.
Uint32 QuantBot::economicGenerator(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (!pass.vanillaEconomy || builderOnly() || pass.defendingEconomy
        || turn.pBuilder->getItemID()!=Structure_ConstructionYard
        || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0) return NONE_ID;
    // Income and whatever production core this tree actually
    // offers come first; absent technology is not a blocker.
    auto coreEstablished=[&](Uint32 item) {
        return !data[item][houseID].enabled
            || data[item][houseID].techLevel>currentGame->techLevel
            || getHouse()->getNumItems(item)>0;
    };
    if (getAIRefineryBays()==0 || getAIHarvesterCount()==0
        || !coreEstablished(Structure_LightFactory)
        || !coreEstablished(Structure_HeavyFactory)) return NONE_ID;
    // Capacity already paid for: the health a damaged generator
    // regains under the repair policy, and every queued
    // generator anywhere in the base. Demand already ordered
    // counts on the other side of the ledger.
    QuantBotPowerInvestmentPolicy::PowerTarget target;
    target.demand=getHouse()->getPowerRequirement();
    target.produced=getHouse()->getProducedPower();
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0) continue;
        const int nominal=-data[structure->getItemID()][structure->getProductionHouseID()].power;
        if (nominal<=0 || structure->getMaxHealth()<=0) continue;
        const int current=(structure->getHealth()*nominal/structure->getMaxHealth()).floor();
        target.restorable+=std::max(0,nominal-std::min(nominal,current));
    }
    for (int item=Structure_FirstID;item<=Structure_LastID;++item) {
        const int ordered=std::max(0,pass.itemCount[item]-getHouse()->getNumItems(item));
        if (ordered<=0) continue;
        const int nominal=-data[item][houseID].power;
        if (nominal>0) target.pendingOutput+=ordered*nominal;
        else target.queuedDemand+=ordered*std::max(0,data[item][houseID].power);
    }
    // Every generator this yard could actually place, so the
    // reserve is sized on a real building rather than a guess.
    // The plain windtrap is the standard unit where the tree has
    // one; otherwise the smallest available generator is, which
    // keeps the figure the same for every peer.
    struct Candidate { Uint32 item; int output; int unitCapital; };
    std::vector<Candidate> available;
    for (Uint32 generator:{Uint32(Structure_WindTrap),Uint32(Structure_AdvancedWindTrap),
            Uint32(Structure_AdvancedWindTrapMK2),Uint32(Structure_AdvancedWindTrapMK3),
            Uint32(Structure_NuclearPlant)}) {
        const int output=-data[generator][houseID].power;
        if (output<=0 || !campaignAvailableToBuild(turn.pBuilder,generator)
            || !findPlaceLocation(generator).isValid()) continue;
        available.push_back({generator,output,buildingCapitalCost(pass, generator)});
    }
    if (available.empty()) return NONE_ID;
    for (const auto& candidate:available) {
        if (candidate.item==Structure_WindTrap) { target.standardOutput=candidate.output; break; }
        target.standardOutput=target.standardOutput==0
            ? candidate.output : std::min(target.standardOutput,candidate.output);
    }
    // One package at a time, but a pending generator that cannot
    // reach the target no longer blocks the rest of it: its
    // output is already counted, so the next pass simply orders
    // what is still missing.
    const int deficit=QuantBotPowerInvestmentPolicy::bufferedShortfall(target);
    const int shortage=QuantBotPowerInvestmentPolicy::operatingShortfall(target);
    if (deficit<=0) return NONE_ID;
    // Exactly the repairs a closed deficit stops paying for,
    // at the engine's own integer per-hitpoint charge.
    int repairMilli=0;
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0
            && structure->getItemID()!=Structure_Wall
            && (repairPolicyMaintains(pass, structure)
                || QuantBotBuildPolicy::repairWhenWealthy(pass.money)))
            repairMilli+=QuantBotBuildPolicy::repairCreditsPerHitpointMilli(
                structure->getMaxHealth(),repairPrice(structure));
    // Cheapest complete package for a given size, ties broken by
    // the lowest item id so every peer picks the same building.
    auto cheapestFor=[&](int size) {
        QuantBotPowerInvestmentPolicy::GeneratorChoice best;
        for (const auto& generator:available) {
            QuantBotPowerInvestmentPolicy::GeneratorChoice candidate;
            candidate.item=generator.item;
            candidate.output=generator.output;
            candidate.unitCapital=generator.unitCapital;
            candidate.packageCapital=QuantBotPowerInvestmentPolicy::generatorsNeeded(
                size,generator.output)*candidate.unitCapital;
            if (QuantBotPowerInvestmentPolicy::preferGenerator(candidate,best)) best=candidate;
        }
        return best;
    };
    // Forecast recurring income from the current fleet and field;
    // starting cash is not income, and core queues keep their reserve.
    auto priced=[&](const QuantBotPowerInvestmentPolicy::GeneratorChoice& choice,int size) {
        QuantBotPowerInvestmentPolicy::Investment investment;
        investment.deficit=size;
        investment.generatorOutput=choice.output;
        investment.unitCapital=choice.unitCapital;
        investment.spendable=QuantBotBuildPolicy::spendableCredits(pass.money,pass.economyReserve);
        investment.netIncome=pass.forecastNetIncome;
        investment.incomeMinutes=QuantBotSpendingPolicy::horizonMinutes;
        investment.repairMilliPerHitpoint=repairMilli;
        return investment;
    };
    const auto best=cheapestFor(deficit);
    if (best.item==NONE_ID) return NONE_ID;
    const auto investment=priced(best,deficit);
    // The buffered package is the offer; the bare shortage, with
    // its own cheapest generator, is the fallback the same gates
    // judge if the buffered one is refused.
    const auto relief=cheapestFor(shortage);
    const auto decision=QuantBotPowerInvestmentPolicy::decide(
        investment,priced(relief,shortage));
    const auto verdict=decision.verdict;
    const bool buy=QuantBotPowerInvestmentPolicy::buys(verdict);
    const auto& chosen=decision.trimmedToShortage ? relief : best;
    traceDecision("power_investment",AITelemetry::Record()
        .set("builder",turn.pBuilder->getObjectID()).set("item",chosen.item)
        .set("raw_required",getHouse()->getPowerRequirement())
        .set("raw_produced",getHouse()->getProducedPower())
        .set("queued_demand",target.queuedDemand)
        .set("anticipated_demand",QuantBotPowerInvestmentPolicy::anticipatedDemand(target))
        .set("repair_restored_output",target.restorable)
        .set("pending_output",target.pendingOutput)
        .set("effective_capacity",QuantBotPowerInvestmentPolicy::effectiveCapacity(target))
        .set("standard_generator_output",target.standardOutput)
        .set("buffer_reserve",QuantBotPowerInvestmentPolicy::bufferReserve(target))
        .set("buffered_target",QuantBotPowerInvestmentPolicy::bufferedTarget(target))
        .set("operating_shortage",shortage)
        .set("buffered_shortfall",deficit)
        .set("deficit",decision.deficit).set("generator_output",chosen.output)
        .set("unit_capital",chosen.unitCapital)
        .set("generators_needed",QuantBotPowerInvestmentPolicy::generatorsNeeded(decision.deficit,chosen.output))
        .set("package_capital",decision.capital)
        .set("buffered_package_capital",QuantBotPowerInvestmentPolicy::packageCapital(investment))
        .set("trimmed_to_shortage",decision.trimmedToShortage)
        .set("spendable_after_reserve",investment.spendable)
        .set("economy_reserve",pass.economyReserve)
        .set("forecast_net_income",pass.forecastNetIncome)
        .set("forecast_minutes",QuantBotSpendingPolicy::horizonMinutes)
        .set("maintained_repair_milli_per_hitpoint",repairMilli)
        .set("avoided_repairs_per_minute",QuantBotPowerInvestmentPolicy::avoidedRepairPerMinute(investment))
        .set("accepted",buy)
        .set("reason",QuantBotPowerInvestmentPolicy::describe(verdict)));
    return buy ? chosen.item : Uint32(NONE_ID);
}

bool QuantBot::buildCampaignRepairYard(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Hard/Brutal establish repair capacity; Medium only replaces an authored
    // yard before optional expansion, including its missing prerequisites.
    // Count queued structures and preserve normal costs/placement rules.
    if (isCampaignGameType(currentGame->gameType)
        && !(pass.cityGrowthProtected && pass.capitalCandidates[pass.capitalChoice].builder==turn.pBuilder->getObjectID())
        && !(capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall)
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal
            || (difficulty==Difficulty::Medium && initialItemCount[Structure_RepairYard]>0))
        && data[Structure_RepairYard][houseID].enabled
        && currentGame->techLevel>=data[Structure_RepairYard][houseID].techLevel
        && turn.pBuilder->getItemID()==Structure_ConstructionYard
        && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize()==0
        && pass.itemCount[Structure_RepairYard]==0
        && getAIRefineryBays()>0
        && getAIHarvesterCount()>0
        && ((gameMode == GameMode::Campaign && !pass.citySimEnabled && difficulty != Difficulty::Brutal)
            || !pass.starportMarketAvailable || !data[Structure_StarPort][houseID].enabled
            || currentGame->techLevel < data[Structure_StarPort][houseID].techLevel
            || getHouse()->getNumItems(Structure_StarPort)>0)
        && (getHouse()->hasHeavyFactory() || getHouse()->getNumItems(Structure_StarPort)>0)) {
        Uint32 repairStep=Structure_RepairYard;
        if (!campaignAvailableToBuild(turn.pBuilder,repairStep)) {
            repairStep=NONE_ID;
            for (int i=Structure_FirstID;i<=Structure_LastID;++i)
                if (data[Structure_RepairYard][houseID].prerequisiteStructuresSet[i]
                    && pass.itemCount[i]==0 && campaignAvailableToBuild(turn.pBuilder,i)) {repairStep=i;break;}
        }
        if (repairStep!=NONE_ID && pass.money>=data[repairStep][houseID].price+300) {
            const Coord site=findPlaceLocation(repairStep);
            if (site.isValid() && queueFoundedStructure(pass, turn, repairStep,__LINE__,"campaign_repair_capacity")) {
                ++pass.itemCount[repairStep];
                return true;
            }
        }
    }
    return false;
}

void QuantBot::reserveBuilderCash(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // The factory that has to supply the next yard: either because
    // the rock ran out under the old exhaustion rule, or because
    // the city is built out and has somewhere to settle. Both save
    // for that MCV instead of spending the same cash on units.
    turn.expansionProducer = (pass.rockExpansionNeeded||pass.colonisationNeeded)&&pass.itemCount[Unit_MCV]==0
        && turn.pBuilder->getItemID()==Structure_HeavyFactory;
    const bool openingWorker = openingWorkersNeeded(pass) && getHouse()->hasPower();
    turn.workerProducer = turn.pBuilder->getItemID() == Structure_HeavyFactory
        && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
        && (openingWorker || (pass.brutalEconomy && factoryPrefersHarvester(pass, turn.pBuilder)));
    const bool firstCarryall = needsFirstTransport(pass) && pass.carryallBuildAvailable && getHouse()->hasPower();
    turn.transportProducer = firstCarryall && turn.pBuilder->getItemID() == Structure_HighTechFactory
        && campaignAvailableToBuild(turn.pBuilder,Unit_Carryall);
    int protectedCash = turn.pBuilder->getItemID() == Structure_ConstructionYard || turn.transportProducer || turn.workerProducer || turn.expansionProducer
        ? 0 : std::max({pass.strategicReserveCost,pass.economyReserve,pass.civicReserveCost});
    // A cramped start still needs power, income and a factory before
    // it can expand. Don't protect cash for an MCV we cannot build.
    if ((pass.rockExpansionNeeded||pass.colonisationNeeded) && pass.mcvBuildAvailable && pass.itemCount[Unit_MCV]==0 && !turn.expansionProducer)
        protectedCash=std::max(protectedCash,int(data[Unit_MCV][houseID].price));
    if (!openingWorker && !turn.expansionProducer && turn.pBuilder->getItemID() != Structure_ConstructionYard)
        protectedCash = std::max(protectedCash,pass.civicReserveCost);
    if (openingWorker && !turn.workerProducer)
        protectedCash = std::max(protectedCash,data[Unit_Harvester][houseID].price);
    if (firstCarryall && !turn.transportProducer)
        protectedCash = std::max(protectedCash,data[Unit_Carryall][houseID].price);
    if (pass.nuclearPlan && getHouse()->hasPower() && !powerGenerationPending(pass)
        && turn.pBuilder->getItemID() != Structure_ConstructionYard && !turn.transportProducer && !turn.workerProducer && !turn.expansionProducer)
        protectedCash = std::max(protectedCash,data[Structure_NuclearPlant][houseID].price);
    if (turn.pBuilder->getItemID() != Structure_ConstructionYard && !turn.expansionProducer && !turn.transportProducer)
        protectedCash = std::max(protectedCash,harvesterInvestmentReserve(pass));
    turn.capitalSupplier = capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].builder==turn.pBuilder->getObjectID();
    if (turn.capitalSupplier && (pass.cityGrowthProtected || pass.protectionCapital || pass.defendingEconomy || harvesterInvestmentReserve(pass)==0
        || pass.capitalCandidates[pass.capitalChoice].item==Unit_Harvester
        || pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall
        || pass.capitalCandidates[pass.capitalChoice].item==Unit_MCV)) protectedCash=0;
    protectedCash=std::max(protectedCash,capitalReserve(pass, turn.pBuilder->getObjectID()));
    // A no-spice city's seed cash belongs to the yard that can
    // zone. No factory, port or market bargain may spend it on
    // the way there; only construction yards build R/C/I lots, so
    // nothing legitimate is withheld from its owner.
    if (turn.pBuilder->getItemID() != Structure_ConstructionYard)
        protectedCash = std::max(protectedCash,cityBootstrapReserve(pass));
    // Saving for optional capital must not stop the yard from
    // relieving the storage ceiling that prevents further saving.
    if (turn.pBuilder->getItemID()==Structure_ConstructionYard && storageExpansionNeeded(pass)
        && campaignAvailableToBuild(turn.pBuilder,Structure_Silo)) protectedCash=0;
    turn.reserve.reserved = pass.money - QuantBotBuildPolicy::spendableCredits(pass.money,protectedCash);
    pass.money -= turn.reserve.reserved;
}

bool QuantBot::buildCapitalUnits(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (turn.capitalSupplier && capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall
        && pass.money<pass.capitalCandidates[pass.capitalChoice].price) {
        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
            .set("builder",turn.pBuilder->getObjectID()).set("item",Unit_Carryall)
            .set("price",pass.capitalCandidates[pass.capitalChoice].price).set("spendable",pass.money)
            .set("reason",pass.itemCount[Unit_Carryall]==0 ? "saving_first_transport" : "saving_transport_capacity"));
        return true;
    }
    const CapitalCandidate* localUnitChoice=nullptr;
    // A rich opening can fund its MCV now. Do not repeatedly buy
    // workers before the factory gets to unlock that construction.
    const bool unlockingVanillaMcv=!pass.postponeCapacityMcv && pass.vanillaEconomy && gameMode==GameMode::Custom
        && turn.pBuilder->getItemID()==Structure_HeavyFactory
        && !campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
        && turn.pBuilder->getCurrentUpgradeLevel()<turn.pBuilder->getMaxUpgradeLevel()
        && DuneCity::prioritizeVanillaMcv(pass.money,getAIHarvesterCount(),
            pass.itemCount[Structure_ConstructionYard],pass.itemCount[Unit_MCV],data[Unit_MCV][houseID].price);
    const bool parallelHeavyProduction=pass.fundedCityProduction
        && turn.pBuilder->getItemID()==Structure_HeavyFactory;
    if (!turn.transportProducer && !turn.expansionProducer && !unlockingVanillaMcv && !parallelHeavyProduction)
        for (const auto& candidate:pass.capitalCandidates)
            if (candidate.builder==turn.pBuilder->getObjectID() && isUnit(candidate.item)
                && (candidate.item!=Unit_Carryall || pass.itemCount[Unit_Carryall]<pass.transportTarget)
                && !QuantBotBuildPolicy::militaryItem(candidate.item)
                && (!turn.capitalSupplier || candidate.item==pass.capitalCandidates[pass.capitalChoice].item)
                && candidate.score>0 && candidate.price<=pass.money
                && (!localUnitChoice || candidate.score>localUnitChoice->score)) localUnitChoice=&candidate;
    if (localUnitChoice && turn.pBuilder->getProductionQueueSize()==0
        && !turn.pBuilder->isUpgrading() && !turn.pBuilder->isOnHold()) {
        const auto choice=*localUnitChoice;
        if (produceItemWithLogging(pass, turn, choice.item,__LINE__,turn.capitalSupplier ? "shared_capital_priority" : "parallel_capital_purchase")) {
            ++pass.itemCount[choice.item];
            if (QuantBotBuildPolicy::militaryItem(choice.item)) pass.militaryValue+=data[choice.item][houseID].price;
            if (choice.item==Unit_Harvester) pass.orderedSpiceHarvester=true;
            if (turn.pBuilder->getItemID()!=Structure_StarPort) return true;
            // Imports can buy further workers and bargains in the same shipment.
        }
    } else if (turn.capitalSupplier && isUnit(pass.capitalCandidates[pass.capitalChoice].item)
        && !QuantBotBuildPolicy::militaryItem(pass.capitalCandidates[pass.capitalChoice].item)
        && turn.pBuilder->getItemID()!=Structure_StarPort && !turn.transportProducer && !turn.expansionProducer
        && !unlockingVanillaMcv && !parallelHeavyProduction) return true;

    // Unlock a new offer only after its tech and structure prerequisites exist.
    // In particular, a factory able to build the ordinary ornithopter must still
    // reach the Elite Ornithopter's third upgrade; the ten-slot mix stays intact.
    const bool infantryProducer = turn.pBuilder->getItemID() == Structure_Barracks
        || turn.pBuilder->getItemID() == Structure_WOR || turn.pBuilder->getItemID() == Structure_Worfinery;
    if (!turn.transportProducer && !turn.workerProducer && !turn.expansionProducer
        && hasTornieProductionUpgradeGoal(turn.pBuilder)
        && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize() == 0
        && pass.money > 1500 && turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()
        && (!infantryProducer || !pass.citySimEnabled || pass.cityAntiAirInfantryDemand)
        && upgradeWithLogging(pass, turn, __LINE__)) return true;

    if (turn.pBuilder->getItemID() != Structure_StarPort && !turn.transportProducer && !turn.workerProducer && !turn.expansionProducer && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize() < 1
        && pass.money > 1500) {
        const int customItem = chooseLowPriorityCustomUnit(turn.pBuilder);
        if (customItem != ItemID_Invalid) {
            const int value = data[customItem][turn.pBuilder->getTechnologyHouseID()].price;
            const bool military = QuantBotBuildPolicy::militaryItem(customItem);
            const bool infantry = isInfantryUnit(customItem) && customItem != Unit_Saboteur;
            const bool worker = customItem == Unit_CapacityHarvester || customItem == Unit_FastHarvester;
            if ((military && int64_t(pass.militaryValue) + value > pass.lightBudget)
                || (worker && pass.itemCount[Unit_Harvester] >= harvesterLimit)
                || (infantry && !infantryQuotaAdmits(pass, value))
                || !produceItemWithLogging(pass, turn, customItem, __LINE__)) return false;
            ++pass.itemCount[customItem];
            if (military) pass.militaryValue += value;
            if (worker) { ++pass.itemCount[Unit_Harvester]; pass.orderedSpiceHarvester = true; }
            if (infantry) { ++pass.infantryCount; pass.infantryValue += value; }
            if (QuantBotBuildPolicy::isLightRaider(customItem)) {
                ++pass.lightVehicleCount;
                pass.lightVehicleValue += value;
            }
            return true;
        }
    }
    return false;
}

void QuantBot::buildLightFactory(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    constexpr auto& mixItems = kMixItems;
    constexpr size_t kSlots = UnitMixPolicy::kSlots;
    if (!turn.pBuilder->isUpgrading()
        && pass.money > (pass.citySimEnabled ? 500 : 700)
        && turn.pBuilder->getProductionQueueSize() < 1
        && turn.pBuilder->getBuildListSize() > 0
        && pass.militaryValue < pass.lightBudget
        && ((pass.citySimEnabled && pass.itemCount[Structure_HeavyFactory] == 0)
            || ((!pass.learningUnitMix && pass.lightVehicleCount < 2)
                || int64_t(pass.lightVehicleValue) * 10000 < int64_t(pass.vehiclePlanValue) * pass.lightVehicleBps))) {

        if (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel() && getHouse()->getCredits() > 1500) {
            upgradeWithLogging(pass, turn, __LINE__);
        }
        else if (!getHouse()->isGroundUnitLimitReached()) {
            Uint32 itemID = NONE_ID;
            int64_t bestDeficit = 0;
            for (size_t i=UnitMixPolicy::kFirstLightSlot; i<kSlots; ++i) {
                const Uint32 candidate = mixItems[i];
                if (!campaignAvailableToBuild(turn.pBuilder,candidate)) continue;
                const auto deficit = UnitMixPolicy::deficit(pass.unitMix[i], pass.vehiclePlanValue,
                    pass.itemCount[candidate], data[candidate][houseID].price);
                if (deficit > bestDeficit) { bestDeficit = deficit; itemID = candidate; }
            }
            // Bootstrap a small sample before combat learning begins.
            if (itemID == NONE_ID && !pass.learningUnitMix && pass.lightVehicleCount < 2)
                for (size_t i=UnitMixPolicy::kFirstLightSlot; i<kSlots; ++i)
                    if (campaignAvailableToBuild(turn.pBuilder,mixItems[i])
                        && (itemID == NONE_ID || pass.itemCount[mixItems[i]] < pass.itemCount[itemID])) itemID = mixItems[i];
            if (itemID != NONE_ID && int64_t(pass.militaryValue) + data[itemID][houseID].price <= pass.lightBudget
                && produceItemWithLogging(pass, turn, itemID, __LINE__, "adaptive_light_mix")) {
                ++pass.itemCount[itemID];
                ++pass.lightVehicleCount;
                pass.lightVehicleValue += data[itemID][houseID].price;
                pass.militaryValue += data[itemID][houseID].price;

            }
        }
    }
}

void QuantBot::buildWOR(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // A city raises rocket infantry only while a hostile wing is actually
    // observed, and then only inside the ordinary infantry quota, because they
    // supplement the launcher share and the turrets rather than replacing them.
    // The opening bootstrap of three stays a non-city rule: a city has no
    // standing infantry role to bootstrap.
    if ((!pass.citySimEnabled || pass.cityAntiAirInfantryDemand)
        && !turn.pBuilder->isUpgrading()
        && turn.pBuilder->getProductionQueueSize() < 1
        && turn.pBuilder->getBuildListSize() > 0
        && pass.money > 450
        && pass.militaryValue < pass.lightBudget
        && !getHouse()->isGroundUnitLimitReached()
        && ((!pass.citySimEnabled && pass.infantryCount < 3)
            || pass.infantryValue * 100 < std::max(pass.militaryValue, 1) * pass.infantryPercent)) {
        Uint32 itemID = NONE_ID;
        // Budget, cash and quota decide which offers are candidates at all, so a
        // 300-credit squad cannot shadow an affordable single Trooper. `money` is
        // already net of the strategic, economy and capital reserves this builder
        // protects, and the acceptance wrapper charges it for every order placed,
        // so one pass cannot spend the same credits at several WORs; the city's
        // own working reserve is not part of that protection and is withheld
        // here. A finished Unit_Troopers order deploys three Unit_Trooper, so its
        // queued squad price is what both sides of the quota account for.
        for (Uint32 candidate : {Unit_Trooper, Unit_Troopers, Unit_Troopers5, Unit_GasInfantry, Unit_FlameTrooper}) {
            if (candidate >= Unit_Troopers5 && !ModManager::instance().isTornieFamilyContentActive()) continue;
            if (!campaignAvailableToBuild(turn.pBuilder,candidate)) continue;
            const int value = data[candidate][houseID].price;
            if (candidate >= Unit_Troopers5
                && (purchasePrice(turn.pBuilder,candidate) > pass.money || !infantryQuotaAdmits(pass, value))) continue;
            if (int64_t(pass.militaryValue)+value > pass.lightBudget) continue;
            if (pass.citySimEnabled
                && (purchasePrice(turn.pBuilder,candidate) > pass.money - pass.cityWorkingReserve
                    || !infantryQuotaAdmits(pass, value))) continue;
            if (itemID == NONE_ID || pass.itemCount[candidate] < pass.itemCount[itemID]
                || (candidate == Unit_Troopers5 && pass.itemCount[candidate] == pass.itemCount[itemID])) itemID = candidate;
        }
        if (itemID != NONE_ID && produceItemWithLogging(pass, turn, itemID, __LINE__)) {
            pass.itemCount[itemID]++;
            pass.infantryCount++;
            pass.infantryValue += data[itemID][houseID].price;
            pass.militaryValue += data[itemID][houseID].price;

        }
    }
}

void QuantBot::buildBarracks(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (!pass.citySimEnabled
        && !turn.pBuilder->isUpgrading()
        && turn.pBuilder->getProductionQueueSize() < 1
        && turn.pBuilder->getBuildListSize() > 0
        && pass.money > 300
        && pass.militaryValue < pass.lightBudget
        && !getHouse()->isGroundUnitLimitReached()
        && (pass.infantryCount < 3
            || pass.infantryValue * 100 < std::max(pass.militaryValue, 1) * pass.infantryPercent)) {
        Uint32 itemID = NONE_ID;
        for (Uint32 candidate : {Unit_Soldier, Unit_Infantry, Unit_Infantry5, Unit_GasInfantry, Unit_FlameTrooper}) {
            if (candidate >= Unit_Troopers5 && !ModManager::instance().isTornieFamilyContentActive()) continue;
            if (candidate >= Unit_Troopers5
                && (purchasePrice(turn.pBuilder,candidate) > pass.money
                    || int64_t(pass.militaryValue) + data[candidate][houseID].price > pass.lightBudget
                    || !infantryQuotaAdmits(pass, data[candidate][houseID].price))) continue;
            if (campaignAvailableToBuild(turn.pBuilder,candidate)
                && (itemID == NONE_ID || pass.itemCount[candidate] < pass.itemCount[itemID]
                    || (candidate == Unit_Infantry5 && pass.itemCount[candidate] == pass.itemCount[itemID]))) {
                itemID = candidate;
            }
        }
        if (itemID != NONE_ID && int64_t(pass.militaryValue)+data[itemID][houseID].price<=pass.lightBudget
            && produceItemWithLogging(pass, turn, itemID, __LINE__)) {
            pass.itemCount[itemID]++;
            pass.infantryCount++;
            pass.infantryValue += data[itemID][houseID].price;
            pass.militaryValue += data[itemID][houseID].price;

        }
    }
}

void QuantBot::buildHighTechFactory(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    constexpr size_t kOrnithopterSlot = UnitMixPolicy::kOrnithopterSlot;
    const Uint32 ornithopter = ModManager::instance().isTornieFamilyContentActive()
        && campaignAvailableToBuild(turn.pBuilder, Unit_EliteOrnithopter) ? Unit_EliteOrnithopter : Unit_Ornithopter;
    const int technology = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : houseID;
    const int ornithopterPrice = data[ornithopter][technology].price;
    const int carryallPrice = data[Unit_Carryall][technology].price;
    const int ornithopterValue = data[Unit_Ornithopter][houseID].price * pass.itemCount[Unit_Ornithopter]
        + (ModManager::instance().isTornieFamilyContentActive()
            ? data[Unit_EliteOrnithopter][houseID].price * pass.itemCount[Unit_EliteOrnithopter] : 0);
    QuantBotBuildPolicy::AirProductionState air;
    air.busy = turn.pBuilder->getProductionQueueSize() > 0;
    air.upgrading = turn.pBuilder->isUpgrading();
    air.airLimit = getHouse()->isAirUnitLimitReached();
    air.ornithopterAvailable = campaignAvailableToBuild(turn.pBuilder,ornithopter);
    air.carryallAvailable = campaignAvailableToBuild(turn.pBuilder,Unit_Carryall);
    air.canUpgrade = turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel();
    air.spendable = pass.money; // Economy/strategic reserves were removed above.
    air.ornithopterPrice = ornithopterPrice;
    air.carryallPrice = carryallPrice;
    air.carryalls = pass.itemCount[Unit_Carryall];
    air.carryallTarget = pass.transportTarget;
    air.armyValue = pass.militaryValue;
    air.armyLimit = pass.lightBudget;
    air.vehiclePlanValue = pass.vehiclePlanValue;
    air.airCommittedValue = ornithopterValue;
    air.airTargetBps = pass.unitMix[kOrnithopterSlot];
    air.airStrategy = pass.strategyMixProfile && strategyStart[kOrnithopterSlot] > 0;
    air.credits = getHouse()->getCredits();
    const auto decision = QuantBotBuildPolicy::chooseAirProduction(air);
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("air_production_decision", AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("reason",decision.reason)
            .set("credits",getHouse()->getCredits()).set("planning_spendable",pass.money)
            .set("economy_reserve",pass.economyReserve).set("strategic_reserve",pass.strategicReserveCost)
            .set("cash_threshold",ornithopterPrice).set("ornithopter_price",ornithopterPrice)
            .set("air_target_value",(pass.vehiclePlanValue*pass.ornithopterPercent).lround())
            .set("air_committed_value",ornithopterValue).set("air_target_bps",pass.unitMix[kOrnithopterSlot])
            .set("military_value",pass.militaryValue).set("military_limit",militaryValueLimit)
            .set("military_budget",pass.militaryBudget)
            .set("ornithopter_available",air.ornithopterAvailable)
            .set("carryall_target",pass.transportTarget).set("carryalls_committed",pass.itemCount[Unit_Carryall])
            .set("queue",turn.pBuilder->getProductionQueueSize()).set("current_item",turn.pBuilder->getCurrentProducedItem())
            .set("upgrading",turn.pBuilder->isUpgrading()).set("hold",turn.pBuilder->isOnHold()));
    }
    using AirOrder = QuantBotBuildPolicy::AirOrder;
    if (decision.order == AirOrder::Upgrade) {
        if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
            // An Air strategy's unlock may use credits the reserves hold back, as
            // the starport recovery does, never cash already charged this pass.
            const bool airUnlock = air.airStrategy && !air.ornithopterAvailable;
            const int heldCash = airUnlock ? std::max(0, getHouse()->getCredits() - pass.money) : 0;
            pass.money += heldCash;
            upgradeWithLogging(pass, turn, __LINE__);
            pass.money -= heldCash;
        }
        else doRepair(turn.pBuilder);
    } else if (decision.order == AirOrder::Ornithopter || decision.order == AirOrder::Carryall) {
        const Uint32 item = decision.order == AirOrder::Ornithopter ? ornithopter : Unit_Carryall;
        if (produceItemWithLogging(pass, turn, item, __LINE__, decision.reason)) {
            ++pass.itemCount[item];

            if (QuantBotInternal::ornithopterItem(item)) pass.militaryValue += ornithopterPrice;
        }
    }
}

void QuantBot::buildHeavyFactory(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    constexpr auto& mixItems = kMixItems;
    // Log HF status when idle with money (Custom mode diagnostics)
    if (gameMode == GameMode::Custom && pass.emitStatsLog) {
        logDebug("HF=%u: upgrading=%d queue=%d buildList=%d upgLv=%d/%d unitLimit=%d spendable=%d hold=%d military=%d/%d reserve=%d",
            turn.pBuilder->getObjectID(), turn.pBuilder->isUpgrading(), turn.pBuilder->getProductionQueueSize(),
            turn.pBuilder->getBuildListSize(),
            turn.pBuilder->getCurrentUpgradeLevel(), turn.pBuilder->getMaxUpgradeLevel(),
            getHouse()->isGroundUnitLimitReached(), pass.money, turn.pBuilder->isOnHold(),
            pass.militaryValue, militaryValueLimit, pass.strategicReserveCost);
    }
    const int cityMcvCash = pass.money;
    if(turn.expansionProducer && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize()==0
        && pass.money < (campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            ? data[Unit_MCV][houseID].price : turn.pBuilder->getUpgradeCost())) return;
    const bool prioritizeCityMcv = !pass.postponeCapacityMcv && pass.citySimEnabled && gameMode == GameMode::Custom
        && (pass.fundedCityProduction || !openingWorkersNeeded(pass) || pass.rockExpansionNeeded)
        && !getHouse()->isGroundUnitLimitReached()
        && (turn.expansionProducer || QuantBotBuildPolicy::canFundCityYard(cityMcvCash, data[Unit_MCV][houseID].price,
            pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV], pass.cityYardTarget, pass.cityWorkingReserve));
    // Test-only expansion style (docs/ai-play-styles.md): keep at least
    // PlayStyle::minYards yards once the first heavy factory stands.
    const int styleYardShortfall = pass.vanillaEconomy && gameMode == GameMode::Custom
        && pass.itemCount[Structure_HeavyFactory] > 0
        ? style().minYards - pass.itemCount[Structure_ConstructionYard] - pass.itemCount[Unit_MCV] : 0;
    const bool prioritizeMcv = prioritizeCityMcv || (!pass.postponeCapacityMcv && pass.vanillaEconomy && gameMode == GameMode::Custom
        && !getHouse()->isGroundUnitLimitReached()
        && (DuneCity::prioritizeVanillaMcv(pass.money, getAIHarvesterCount(),
            pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV], data[Unit_MCV][houseID].price)
            || (styleYardShortfall > 0 && pass.money >= data[Unit_MCV][houseID].price + 1000)));
    // A built-out city needs one yard it cannot express as a
    // capacity shortfall, so the factory may still unlock MCV
    // production for it.
    const int mcvShortfall = pass.citySimEnabled
        ? std::max(pass.colonisationNeeded ? 1 : 0,
            pass.cityYardTarget - pass.itemCount[Structure_ConstructionYard] - pass.itemCount[Unit_MCV])
        : std::max(styleYardShortfall, DuneCity::vanillaMcvShortfall(pass.money, getAIHarvesterCount(),
            pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV]));
    if (pass.emitStatsLog && AITelemetry::log().enabled())
        traceDecision("factory_economy_priority",AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("harvesters_committed",pass.itemCount[Unit_Harvester])
            .set("spice_target",pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget)
            .set("army_value",pass.militaryValue).set("army_target",militaryValueLimit)
            .set("army_budget",pass.militaryBudget)
            .set("military_available",canBuildMilitaryVehicle(pass, turn.pBuilder))
            .set("prefer_harvester",factoryPrefersHarvester(pass, turn.pBuilder))
            .set("opening_workers_needed",openingWorkersNeeded(pass)).set("spendable",pass.money)
            .set("protected_cash",turn.reserve.reserved).set("queued_cost",pass.queuedProductionCost));
    // only if the factory isn't busy
    if ((turn.pBuilder->isUpgrading() == false) && (turn.pBuilder->getProductionQueueSize() < 1) && (turn.pBuilder->getBuildListSize() > 0)) {
        // we need a construction yard. Build an MCV if we don't have a starport
        if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
            && pass.itemCount[Unit_MCV] + pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Structure_StarPort] < 1
            && campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            && !getHouse()->isGroundUnitLimitReached()) {
            if (produceItemWithLogging(pass, turn, Unit_MCV, __LINE__)) pass.itemCount[Unit_MCV]++;
        }
        else if (prioritizeMcv && campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            && (!pass.capacityMcvRunway || canFundCapacityMcv(pass, purchasePrice(turn.pBuilder,Unit_MCV)))) {
            if (produceItemWithLogging(pass, turn, Unit_MCV, __LINE__,
                    pass.citySimEnabled ? "city_cash_construction_capacity" : "cash_construction_capacity")) {
                ++pass.itemCount[Unit_MCV];
            }
        }
        else if (prioritizeMcv && pass.mcvUpgradesInProgress < mcvShortfall
            && !campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            && turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel()
            && (pass.citySimEnabled ? cityMcvCash : pass.money) >= turn.pBuilder->getUpgradeCost()
                + (turn.expansionProducer ? 0 : pass.citySimEnabled ? std::max(1000, pass.cityWorkingReserve) : 1000)) {
            if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
                const bool accepted = upgradeWithLogging(pass, turn, __LINE__);
                if (accepted) {
                    ++pass.mcvUpgradesInProgress;
                }
                traceDecision("mcv_unlock", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
                    .set("accepted", accepted).set("rule", pass.citySimEnabled
                        ? "city_cash_construction_capacity" : "cash_construction_capacity"));
            } else if (!turn.pBuilder->isRepairing()) {
                doRepair(turn.pBuilder);
            }
        }
        else if ((pass.citySimEnabled || pass.vanillaEconomy) && !pass.orderedSpiceHarvester
            && factoryPrefersHarvester(pass, turn.pBuilder)
            && pass.itemCount[Unit_Harvester] < (pass.vanillaEconomy ? pass.spiceHarvesterTarget : pass.fundedHarvesterTarget)
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester) && !getHouse()->isGroundUnitLimitReached()
            && pass.money + ((pass.vanillaEconomy || harvesterInvestmentReserve(pass) > 0) ? turn.reserve.reserved : 0) >= data[Unit_Harvester][houseID].price
                + ((pass.campaignEconomyPush || harvesterInvestmentReserve(pass) > 0) ? 0 : (openingWorkersNeeded(pass) || pass.brutalEconomy) ? 0 : pass.vanillaEconomy ? 1000 : data[Unit_Tank][houseID].price)) {
            const int workerCash=(pass.vanillaEconomy || harvesterInvestmentReserve(pass)>0)
                ? std::max(0,turn.reserve.reserved-capitalReserve(pass, turn.pBuilder->getObjectID())) : 0;
            pass.money+=workerCash;
            const bool accepted=produceItemWithLogging(pass, turn, Unit_Harvester,__LINE__,"spice_economy");
            pass.money-=workerCash;
            if (accepted) {
                ++pass.itemCount[Unit_Harvester];
                pass.orderedSpiceHarvester = true;
            }
        }
        else if ((pass.money > 10000) && (turn.pBuilder->isUpgrading() == false) && (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel())) {
            if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
                upgradeWithLogging(pass, turn, __LINE__);
            }
            else {
                doRepair(turn.pBuilder);
            }
        }
        else if (gameMode == GameMode::Custom && !pass.vanillaEconomy && !pass.citySimEnabled
            && campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            && !getHouse()->isGroundUnitLimitReached()
            && (!pass.capacityMcvRunway || canFundCapacityMcv(pass, purchasePrice(turn.pBuilder,Unit_MCV)))
            && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] < std::min(8, pass.money / 4000)) {
            if (produceItemWithLogging(pass, turn, Unit_MCV, __LINE__, "construction_capacity")) {
                pass.itemCount[Unit_MCV]++;
            }
        }
        else if (gameMode == GameMode::Custom
            && !pass.vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
            && !getHouse()->isGroundUnitLimitReached()
            && pass.itemCount[Unit_Harvester] < pass.militaryValue / 1000
            && pass.itemCount[Unit_Harvester] < harvesterLimit && lastCalculatedSpice > 0) {
            // In case we get given lots of money, it will eventually run out so we need to be prepared
            // Classic-mode harvester expansion; city spice investment is handled above.
            if (produceItemWithLogging(pass, turn, Unit_Harvester, __LINE__)) pass.itemCount[Unit_Harvester]++;
        }
        else if (!pass.vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
            && lastCalculatedSpice > 0
            && pass.itemCount[Unit_Harvester] < harvesterLimit
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
            && !getHouse()->isGroundUnitLimitReached()
            && (pass.money < 2000 || gameMode == GameMode::Campaign)) {
            // Classic-mode recovery; city spice investment is handled above.
            if (produceItemWithLogging(pass, turn, Unit_Harvester, __LINE__)) pass.itemCount[Unit_Harvester]++;
        }
        else if ((pass.money > 500) && (turn.pBuilder->isUpgrading() == false) && (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel())) {
            // Upgrade before military — unlocks MCV(1), Launcher(2), SiegeTank(3)
            if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
                upgradeWithLogging(pass, turn, __LINE__);
            }
            else {
                doRepair(turn.pBuilder);
            }
        }
        else if (pass.money > (pass.citySimEnabled || pass.vanillaEconomy ? 500 : 2000)
            && pass.militaryValue < pass.militaryBudget && !getHouse()->isGroundUnitLimitReached()) {
            // Every heavy mix slot is a candidate with its own learned share and
            // committed value - each special its own slot since 9868, so a house
            // that builds several specials weighs them by their own results.
            constexpr size_t kHeavy = UnitMixPolicy::kOrnithopterSlot;
            std::array<Uint32,kHeavy> types{};
            std::array<QuantBotBuildPolicy::AllocationCandidate,kHeavy> candidates;
            AITelemetry::Record choices;
            for (size_t i=0; i<types.size(); ++i) {
                const auto item = types[i] = mixItems[i];
                candidates[i] = {data[item][houseID].price, data[item][houseID].price * pass.itemCount[item],
                    pass.unitMix[i], campaignAvailableToBuild(turn.pBuilder,item)
                        && (item!=Unit_Tank || !pass.surplusArmy
                            || int64_t(pass.militaryValue)+data[item][houseID].price<=militaryValueLimit)};
            }
            const int normalHorizon = pass.vehiclePlanValue;
            const int expansionHorizon = pass.vehiclePlanValue;
            for (size_t i=0; i<types.size(); ++i) {
                const auto item=types[i];
                const auto& c = candidates[i];
                choices.set(std::to_string(item), AITelemetry::Record().set("price",c.price)
                    .set("committed_value",c.committedValue).set("target_bps",c.targetBps)
                    .set("available",c.available).set("affordable",c.price<=pass.money)
                    .set("deficit_scaled",int64_t(normalHorizon)*c.targetBps-int64_t(c.committedValue)*10000)
                    .set("expansion_deficit_scaled",int64_t(expansionHorizon)*c.targetBps-int64_t(c.committedValue)*10000));
            }
            int selected = QuantBotBuildPolicy::fundedDeficit(
                candidates,pass.militaryValue,pass.money,pass.militaryBudget,pass.vehiclePlanValue);
            const bool expansionFallback = selected < 0;
            if (expansionFallback) selected = QuantBotBuildPolicy::capacityFill(
                candidates,pass.militaryValue,pass.money,pass.militaryBudget);
            // Candidate tables are useful when the choice changes, but logging the
            // identical no-deficit decision for every idle factory rapidly exhausts
            // the match capture. Keep a periodic heartbeat for diagnosis.
            const Uint32 allocationCycle = getGameCycleCount();
            const uint64_t allocationSignature = (uint64_t(selected + 1) << 40)
                | static_cast<uint64_t>(expansionFallback);
            const auto traceIt = lastHeavyAllocationTrace.find(turn.pBuilder->getObjectID());
            const bool allocationChanged = traceIt == lastHeavyAllocationTrace.end()
                || traceIt->second.first != allocationSignature
                || allocationCycle - traceIt->second.second >= MILLI2CYCLES(30000);
            if (allocationChanged) {
                traceDecision("heavy_allocation_decision",AITelemetry::Record()
                    .set("builder",turn.pBuilder->getObjectID()).set("army_value",pass.militaryValue)
                    .set("army_limit",pass.militaryBudget).set("configured_army_target",militaryValueLimit)
                    .set("army_budget_source",pass.militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
                    .set("horizon_value",normalHorizon)
                    .set("expansion_horizon",expansionHorizon).set("expansion_fallback",expansionFallback)
                    .set("spendable",pass.money).set("candidates",choices)
                    .set("selected",selected<0 ? NONE_ID : types[selected])
                    .set("reason",selected<0 ? "no_affordable_capacity"
                        : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit"));
                lastHeavyAllocationTrace[turn.pBuilder->getObjectID()] = {allocationSignature, allocationCycle};
            }
            if (selected>=0 && produceItemWithLogging(pass, turn, types[selected],__LINE__,
                expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit")) {
                ++pass.itemCount[types[selected]];

                pass.militaryValue += candidates[selected].price;
            }
        }
    }
}

void QuantBot::buildStarPort(BuildPass& pass, BuilderTurn& turn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const StarPort* pStarPort = static_cast<const StarPort*>(turn.pBuilder);
    if (pStarPort->okToOrder()) {
        const Choam& choam = getHouse()->getChoam();

        // Economic imports may use the cash held for the economy,
        // just as factory-built harvesters do. Market discounts are
        // irrelevant to needed workers and the first transport.
        int workerTarget = pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget;
        // A bargain worker is a cheap economy upgrade for a human
        // ally. Fill the permitted fleet instead of stopping at the
        // normal-price, remaining-spice planning target.
        const bool bargainWorkers = isCampaignGameType(currentGame->gameType)
            && isAlliedWithHuman() && lastCalculatedSpice > 0
            && purchasePrice(turn.pBuilder,Unit_Harvester) > 0
            && purchasePrice(turn.pBuilder,Unit_Harvester) < data[Unit_Harvester][houseID].price;
        if (bargainWorkers) {
            if (getHouse()->getMaxHarvesters() > 0)
                workerTarget = getHouse()->getMaxHarvesters();
        }
        // The shared reservation protects the next construction,
        // transport or combat order. Spend the remainder on useful
        // workers without applying a second arbitrary half-cash cap.
        auto buyEconomicImport = [&](Uint32 item, const char* rule) {
            const int price = purchasePrice(turn.pBuilder,item);
            const int portReserve = std::max(capitalReserve(pass, turn.pBuilder->getObjectID()),
                cityBootstrapReserve(pass));
            const int cash = pass.money + turn.reserve.reserved - portReserve;
            if (item==Unit_Harvester && !bargainWorkers && harvesterInvestmentReserve(pass)==0
                && QuantBotSpendingPolicy::marginalSpice(pass.spiceShare,
                    std::min(pass.itemCount[item]*workerAnnualIncome(pass),pass.itemCount[Structure_Refinery]*pass.bayAnnualIncome),
                    std::min((pass.itemCount[item]+1)*workerAnnualIncome(pass),pass.itemCount[Structure_Refinery]*pass.bayAnnualIncome),
                    MILLI2CYCLES(30000),CityEconomyInvestmentPolicy::horizonCycles,
                    DuneCity::kCyclesPerMinute)<=price) return false;
            if (item == Unit_Harvester && getHouse()->getMaxHarvesters() > 0
                && pass.itemCount[item] >= getHouse()->getMaxHarvesters()) return false;
            if (price <= 0 || cash < price || choam.getNumAvailable(item) <= 0
                || !pStarPort->isAvailableToBuild(item)) return false;
            traceDecision("starport_economy_purchase", AITelemetry::Record()
                .set("item", item).set("market_price", price)
                .set("normal_price", data[item][houseID].price)
                .set("spendable", pass.money).set("economic_cash", cash)
                .set("reserved_cash", turn.reserve.reserved).set("worker_target", workerTarget));
            const int availableReserve=std::max(0,turn.reserve.reserved-portReserve);
            pass.money+=availableReserve;
            const bool accepted=produceItemWithLogging(pass, turn, item,__LINE__,rule);
            pass.money-=availableReserve;
            if (!accepted) return false;
            ++pass.itemCount[item];
            // The scoped reserve is restored after this builder;
            // deducting here charges this order exactly once.

            return true;
        };

        // The priority purchase above imports the colonist. If
        // it is still unaffordable, preserve this port's savings.
        if (turn.capitalSupplier && capitalPending(pass)
            && pass.capitalCandidates[pass.capitalChoice].item==Unit_MCV) return;
        // A market transport is still only worth its price when
        // there is something to carry. The capital/factory lane
        // already refuses one without a working harvesting
        // fleet or vehicle repair traffic; a discounted import
        // is the same purchase, so it answers to the same gate.
        const bool transportUseful = CityEconomyInvestmentPolicy::economicTransportUseful(
            lastCalculatedSpice,getAIHarvesterCount(),
            getAIRefineryBays(),pass.combatVehicles,
            getHouse()->getNumItems(Structure_RepairYard));
        if (pass.itemCount[Unit_Carryall] == 0 && transportUseful)
            buyEconomicImport(Unit_Carryall, "first_economic_transport");
        while (pass.itemCount[Unit_Harvester] < workerTarget) {
            if (!buyEconomicImport(Unit_Harvester, "starport_spice_economy")) break;
        }
        while (transportUseful && harvesterInvestmentReserve(pass) == 0
            && pass.itemCount[Unit_Carryall] < pass.transportTarget)
            if (!buyEconomicImport(Unit_Carryall, "starport_transport_capacity")) break;

        // Imports are opportunistic: every discounted combat type
        // is eligible, regardless of the factory composition targets.
        // Buy the best percentage discount first, with stable item-ID
        // tie breaking. Keep the military-value budget and normal caps.
        std::vector<Uint32> bargains;
        for (const auto& offer : pStarPort->getBuildList()) {
            const Uint32 unit = offer.itemID;
            if (!isUnit(unit) || unit == Unit_Harvester || unit == Unit_Carryall
                || unit == Unit_MCV || data[unit][houseID].price <= 0
                || purchasePrice(turn.pBuilder,unit) <= 0 || purchasePrice(turn.pBuilder,unit) >= data[unit][houseID].price) continue;
            bargains.push_back(unit);
        }
        std::sort(bargains.begin(), bargains.end(), [&](Uint32 a, Uint32 b) {
            const int64_t left = int64_t(purchasePrice(turn.pBuilder,a)) * data[b][houseID].price;
            const int64_t right = int64_t(purchasePrice(turn.pBuilder,b)) * data[a][houseID].price;
            return left != right ? left < right : a < b;
        });
        // A bargain is still optional spending: it may not come
        // out of the cash a no-spice city needs to start earning.
        const int bargainReserve = std::max({harvesterInvestmentReserve(pass),
            capitalReserve(pass, turn.pBuilder->getObjectID()),cityBootstrapReserve(pass)});
        for (Uint32 unit : bargains) {
            const int price = purchasePrice(turn.pBuilder,unit);
            const int value = data[unit][houseID].price;
            while (pass.money + turn.reserve.reserved - bargainReserve >= price && choam.getNumAvailable(unit) > 0
                && int64_t(pass.militaryValue) + value <= pass.militaryBudget
                && !getHouse()->isUnitLimitReached(unit)) {
                const int availableReserve=std::max(0,turn.reserve.reserved - bargainReserve);
                pass.money+=availableReserve;
                const bool accepted=produceItemWithLogging(pass, turn, unit,__LINE__,"starport_bargain");
                pass.money-=availableReserve;
                if (!accepted) break;
                ++pass.itemCount[unit];

                pass.militaryValue += value;
            }
        }

        doPlaceOrder(pStarPort);
    }
}
