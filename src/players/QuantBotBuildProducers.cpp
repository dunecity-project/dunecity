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
#include <players/StarportBuyingPolicy.h>

void QuantBot::buildRepair(BuildPass& pass, const StructureBase* pStructure) {
    const int creditsBeforeUpgrade = getHouse()->getCredits();
    const int upgradeCash = pass.money;
    if (doUpgradeScoutpost(pStructure, upgradeCash)) {
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
    // A selected ordinary-installment project needs no legacy yard-entry cash.
    if (pass.techSavings && cityHarvesterReplacementFloor(pass) <= pass.itemCount[Unit_Harvester]
        && buildTechSavings(pass, turn)) return;
    if (upgradeYardForConcrete(pass, turn)) return;
    if (queueCampaignWindtrap(pass, turn, 0)) return;
    if (buildCampaignRepairYard(pass, turn)) return;
    reserveBuilderCash(pass, turn);
    if (buildCityHarvesterReplacement(pass, turn)) return;
    if (turn.pBuilder->getItemID() != Structure_ConstructionYard && buildTechSavings(pass, turn)) return;
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

bool QuantBot::vanillaBoomAdmits(BuildPass& pass, Uint32 item) {
    if (pass.vanillaPlan == QuantBotVanillaPolicy::Plan::None) return true;
    if (pass.vanillaBoldTechDeferred && (item == Structure_IX || item == Structure_Palace)) return false;
    if (pass.vanillaBoldMilitaryHeld && !pass.defendingEconomy && QuantBotBuildPolicy::militaryItem(item)) return false;
    if (item == Unit_Harvester)
        return pass.itemCount[item] < pass.spiceHarvesterTarget
            && (!pass.vanillaBoomArmy || pass.itemCount[item] < 4);
    if (item == Unit_MCV)
        return pass.itemCount[item] == 0 && (pass.itemCount[Structure_ConstructionYard] == 0
            || (!pass.vanillaBoomArmy && (pass.rockExpansionNeeded || pass.colonisationNeeded)));
    if (pass.vanillaBoomArmy && item == Unit_Carryall) return pass.itemCount[item] == 0;

    const bool required = item == pass.vanillaBoomRequiredItem && !pass.orderedThisTick.count(item);
    const bool recovery = (item == Structure_Refinery || item == Structure_HeavyFactory)
        && pass.itemCount[item] == 0 && getHouse()->getNumLostItems(item) > 0;
    if (!required && !recovery) {
        if (item == Structure_Refinery && pass.itemCount[item] >= pass.vanillaBoomTargets.refineries) return false;
        if (item == Structure_HeavyFactory && pass.itemCount[item] >= pass.vanillaBoomTargets.factories) return false;
    }
    if (!pass.vanillaBoomArmy || required || recovery || QuantBotBuildPolicy::militaryItem(item)) return true;
    // Foundations are admitted together with their building, before the first
    // slab is queued. Idle concrete expansion is disabled at the yard caller.
    if (item == Structure_Slab1 || item == Structure_Slab4)
        return getGameInitSettings().getGameOptions().concreteRequired;
    if (item == Structure_WindTrap || item == Structure_NuclearPlant)
        return pass.itemCount[Structure_WindTrap] == 0
            || (!powerGenerationPending(pass)
                && (!getHouse()->hasPower() || getHouse()->getProducedPower()
                    < getHouse()->getPowerRequirement() + pass.committedPowerDemand));
    const bool emergency = pass.defendingEconomy || pass.airEngaged || pass.groundThreatAsset.isValid();
    if (emergency && (QuantBotBuildPolicy::defensiveEmplacement(item)
        || item == Structure_Wall || (item == Structure_Radar && pass.itemCount[item] == 0))) return true;
    return emergency && pass.protectionCapital && capitalPending(pass)
        && pass.capitalCandidates[pass.capitalChoice].urgent
        && pass.capitalCandidates[pass.capitalChoice].item == item;
}

bool QuantBot::vanillaBoomUpgradeAdmitted(BuildPass& pass, const BuilderBase* builder) {
    if (!pass.vanillaBoomArmy || builder->getObjectID() == pass.vanillaBoomUpgradeBuilder) return true;
    const int house = getHouse()->getHouseID();
    const auto& data = currentGame->objectData.data;
    const auto unlocks = [&](Uint32 item) {
        const auto& spec = data[item][house];
        return spec.enabled && spec.builder == int(builder->getItemID())
            && spec.techLevel <= getHouse()->getTechLevel()
            && builder->getCurrentUpgradeLevel() < spec.upgradeLevel
            && builder->getMaxUpgradeLevel() >= spec.upgradeLevel;
    };
    if (builder->getItemID() == Structure_ConstructionYard
        && getGameInitSettings().getGameOptions().concreteRequired && unlocks(Structure_Slab4)) return true;
    if (pass.itemCount[Structure_ConstructionYard] == 0 && pass.itemCount[Unit_MCV] == 0
        && unlocks(Unit_MCV)) return true;
    if (pass.itemCount[Unit_Harvester] < std::min(4, pass.vanillaBoomTargets.workers)
        && unlocks(Unit_Harvester)) return true;
    if (needsFirstTransport(pass) && unlocks(Unit_Carryall)) return true;
    const bool emergency = pass.defendingEconomy || pass.airEngaged || pass.groundThreatAsset.isValid();
    if (!emergency) return false;
    if (builder->getItemID() == Structure_ConstructionYard)
        return unlocks(Structure_GunTurret) || unlocks(Structure_RocketTurret);
    // An emergency unlock is necessary only if this line cannot already
    // produce combat units; insufficient cash is not a technology shortfall.
    bool militaryUnlock = false;
    for (Uint32 item : kMixItems) {
        if (!QuantBotBuildPolicy::militaryItem(item) || data[item][house].builder != int(builder->getItemID())) continue;
        if (campaignAvailableToBuild(builder, item)) return false;
        if (!unlocks(item)) continue;
        bool prerequisites = true;
        for (int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
            if (data[item][house].prerequisiteStructuresSet[prerequisite]
                && getHouse()->getNumItems(prerequisite) == 0) { prerequisites = false; break; }
        militaryUnlock = militaryUnlock || prerequisites;
    }
    return militaryUnlock;
}

// Record actual queue acceptance for unit and structure production.
bool QuantBot::produceItemWithLogging(BuildPass& pass, BuilderTurn& turn, Uint32 itemID, int sourceLine, const char* rule) {
    const Uint32 requestedItem = itemID;
    const bool replacementWorker = botConfig().force.cityHarvesterReplacement
        && (requestedItem == Unit_Harvester
            || (ModManager::instance().isTornieFamilyContentActive()
                && (requestedItem == Unit_CapacityHarvester || requestedItem == Unit_FastHarvester)));
    const int replacement = replacementWorker ? cityHarvesterReplacementFloor(pass) : 0;
    if (replacementWorker && replacement >= 0 && pass.itemCount[Unit_Harvester] >= replacement) return false;
    if (itemID == Unit_Harvester) itemID = chooseTornieHarvester(turn.pBuilder, pass.money);
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.fleetRatios) {
        if (requestedItem == Unit_Harvester && pass.itemCount[Unit_Harvester] >= pass.spiceHarvesterTarget)
            return false;
        if (itemID == Structure_Refinery && pass.itemCount[itemID] >= pass.fleetRefineryTarget)
            return false;
        if (itemID == Unit_Carryall && pass.itemCount[itemID] >= pass.transportTarget)
            return false;
    }
    if (!vanillaBoomAdmits(pass, itemID)
        || (turn.spendingItem != NONE_ID && !vanillaBoomAdmits(pass, turn.spendingItem))) return false;
    if(!cityAdmitsStructure(pass, itemID)) return false;
    if (itemID == Structure_RepairYard && pass.repairCapacityDemand) {
        int liveCommitments = getHouse()->getNumItems(Structure_RepairYard);
        for (const auto* structure : getStructureList()) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || builder->getOwner() != getHouse() || builder->getHealth() <= 0
                || builder->getProductionQueueSize() == 0) continue;
            int queued = 0;
            for (const auto& offer : builder->getBuildList())
                if (offer.itemID == Structure_RepairYard) queued += offer.num;
            const auto reserved = reservedStructures.find(builder->getObjectID());
            liveCommitments += queued > 0 ? queued
                : reserved != reservedStructures.end() && reserved->second.item == Structure_RepairYard ? 1 : 0;
        }
        // Execution revalidates ownership/queues, independently of a selector's
        // stale pass snapshot. Already accepted orders are never cancelled.
        pass.itemCount[Structure_RepairYard] = std::max(pass.itemCount[Structure_RepairYard], liveCommitments);
    }
    if (itemID==Structure_RepairYard && !repairYardAdmitted(pass)) return false;
    // Also cover campaign rebuild orders, which bypass the strategic planner.
    if (itemID==Structure_Palace && pass.itemCount[Structure_Palace] >=
        QuantBotBuildPolicy::difficultyPalaceCap(static_cast<int>(difficulty))) return false;
    if(itemID == Unit_MCV && pass.yardLimit > 0
        && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] >= pass.yardLimit) return false;
    const int quotedPrice=purchasePrice(turn.pBuilder,itemID);
    const auto& portPolicy = botConfig().force;
    const bool portScope = (portPolicy.starportBuying > 0 || portPolicy.starportCashStockpile > 0
            || portPolicy.starportEconomyPriority > 0)
        && gameMode == GameMode::Custom
        && (getGameInitSettings().getModName() == "vanilla"
            || ModManager::instance().isDuneCityContentActive())
        && (getGameInitSettings().getGameType() == GameType::CustomGame
            || getGameInitSettings().getGameType() == GameType::CustomMultiplayer)
        && getGameInitSettings().getInitialSpiceTilePermille() > 0;
    // The experimental ceiling applies to every accepted import, including
    // economy/capital paths that do not visit the bargain loop.
    if (portScope && portPolicy.starportBuying > 0
        && turn.pBuilder->getItemID() == Structure_StarPort
        && !StarportBuyingPolicy::admitted(quotedPrice,
            getHouse()->radarUnitPrice(itemID,data[itemID][houseID].price),
            portPolicy.starportBuying)) return false;
    // Only optional factory combat orders wait for the market. Economy,
    // construction recovery and the port itself keep their native cash access.
    if (portScope && portPolicy.starportCashStockpile > 0
        && turn.pBuilder->getItemID() != Structure_StarPort
        && QuantBotBuildPolicy::militaryItem(itemID)
        && StarportBuyingPolicy::preserveCash(pass.money, quotedPrice,
            getHouse()->radarUnitPrice(itemID, data[itemID][houseID].price),
            portPolicy.starportCashStockpile)) {
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_StarPort
                || structure->getHealth() <= 0 || !structure->isActive()) continue;
            const auto* port = static_cast<const StarPort*>(structure);
            if (!port->okToOrder() || !port->isAvailableToBuild(itemID)
                || getHouse()->getChoam().getNumAvailable(itemID) <= 0) continue;
            const int factory = getHouse()->radarUnitPrice(itemID, data[itemID][houseID].price);
            if (!StarportBuyingPolicy::admitted(purchasePrice(port, itemID), factory, portPolicy.starportBuying)) {
                if (pass.emitStatsLog) traceDecision("starport_cash_stockpile", AITelemetry::Record()
                    .set("item",itemID).set("cash",pass.money).set("factory_price",factory)
                    .set("market_price",purchasePrice(port,itemID)).set("level",portPolicy.starportCashStockpile));
                return false;
            }
        }
    }
    const Uint32 spendingItem = turn.spendingItem == NONE_ID ? itemID : turn.spendingItem;
    const auto spendingBucket = QuantBotBuildPolicy::spendingBucket(spendingItem);
    const bool techInstallment = pass.techSavings
        && turn.installmentProject != NONE_ID && turn.installmentProject == spendingItem;
    if (!compositionAdmits(pass, itemID) || !spendingAdmits(pass, spendingItem, quotedPrice)) return false;
    if (itemID==Unit_MCV && !canFundCapacityMcv(pass,quotedPrice,turn.pBuilder->getObjectID())) {
        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
            .set("builder",turn.pBuilder->getObjectID()).set("item",itemID).set("rule",rule)
            .set("price",quotedPrice).set("projected_cash",pass.cashFlow.projectedCash)
            .set("capacity_mcv_ordered_cost",pass.capacityMcvOrderedCost).set("cash_buffer",pass.cashBuffer)
            .set("reason",pass.citySimEnabled && botConfig().force.cityOpeningCapital
                ? "city_working_capital" : "existing_production_runway"));
        return false;
    }
    // Every producer shares the same committed troop count and infantry budget.
    const bool supportTroop = pass.trooperSupport && QuantBotBuildPolicy::rocketInfantry(itemID);
    if (supportTroop && !trooperOrderAdmitted(pass, itemID, data[itemID][houseID].price)) return false;
    // Keep the unmet floor's army slots across every military producer. Its
    // real cash is allocated to the WOR by the shared capital supplier, ahead
    // of repeat growth as well as competing military purchases.
    if (pass.trooperSupport && pass.trooperReserveValue > 0 && !supportTroop
        && QuantBotBuildPolicy::militaryItem(itemID)
        && int64_t(pass.militaryValue) + data[itemID][houseID].price + pass.trooperReserveValue > pass.lightBudget) {
        if (pass.emitStatsLog && AITelemetry::log().enabled())
            traceDecision("trooper_reserve_blocked", AITelemetry::Record()
                .set("builder", turn.pBuilder->getObjectID()).set("item", itemID)
                .set("military", pass.militaryValue).set("military_limit", pass.lightBudget)
                .set("reserved_value", pass.trooperReserveValue).set("spendable", pass.money));
        return false;
    }
    if (cityInfantryItem(pass, itemID) && ((!pass.cityAntiAirInfantryDemand && !supportTroop)
        || (!supportTroop && !infantryQuotaAdmits(pass, data[itemID][houseID].price))
        || int64_t(quotedPrice) + pass.cityWorkingReserve > pass.money)) return false;
    if (pass.citySimEnabled && itemID == Structure_WOR
        && ((!pass.cityAntiAirInfantryDemand && !pass.trooperSupport) || !troopContentExists(pass, turn.pBuilder)
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
    if (!emergencyGenerator && !techInstallment && pass.money<budgetFloor) {
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
    const auto compositionBefore = [&]() {
        AITelemetry::Record mix;
        if (AITelemetry::log().enabled() && QuantBotBuildPolicy::militaryItem(itemID))
            for (size_t i = 0; i < UnitMixPolicy::kSlots; ++i)
                mix.set(std::to_string(kMixItems[i]), AITelemetry::Record()
                    .set("committed_value", pass.compositionValue[i]).set("target_bps", pass.unitMix[i])
                    .set("available", pass.compositionAvailable[i]));
        return mix;
    }();
    doProduceItem(turn.pBuilder, itemID);
    const bool accepted = turn.pBuilder->getProductionQueueSize() > before;
    if (accepted) {
        if (replacement > pass.itemCount[Unit_Harvester]) {
            const int live = getAIHarvesterCount();
            traceDecision("harvester_replacement", AITelemetry::Record()
                .set("live", live).set("queued", std::max(0, pass.itemCount[Unit_Harvester] - live))
                .set("floor", replacement).set("spice_cap", pass.cityReplacementSpiceCap)
                .set("capacity", pass.cityReplacementCapacity)
                .set("source", turn.pBuilder->getItemID() == Structure_StarPort ? "starport" : "factory"));
        }
        if (pass.techSavings || AITelemetry::log().enabled()) {
            const size_t slot = compositionSlot(itemID);
            if (slot < UnitMixPolicy::kSlots) {
                pass.compositionValue[slot] += data[itemID][houseID].price;
                pass.compositionArmyValue += data[itemID][houseID].price;
                if (pass.techSavings) pass.vehiclePlanValue = pass.compositionArmyValue;
            }
        }
        if (pass.techSavings && QuantBotBuildPolicy::militaryItem(itemID)) {
            const size_t groundSlot = compositionSlot(itemID);
            if (isInfantryUnit(itemID) || (groundSlot < UnitMixPolicy::kSlots
                && groundSlot != UnitMixPolicy::kOrnithopterSlot && !isSandwormUnit(itemID))) {
                pass.committedGroundUnits += itemID == Unit_Infantry || itemID == Unit_Troopers ? 3
                    : itemID == Unit_Infantry5 || itemID == Unit_Troopers5 ? 5 : 1;
                pass.committedGroundValue += data[itemID][houseID].price;
            }
        }
        if (itemID != requestedItem) ++pass.itemCount[itemID];
        if (supportTroop) {
            const int people = QuantBotBuildPolicy::rocketInfantrySize(itemID);
            pass.trooperCount += people;
            pass.trooperReserveValue = int(std::max<int64_t>(0,
                int64_t(pass.trooperReserveValue) - int64_t(people) * data[Unit_Trooper][houseID].price));
        }
        pass.cityPendingDisplayPopulation += QuantBotCityPolicy::displayPopulation(initialCityPopulation(itemID));
        if(DuneCity::isCityZoneStructure(itemID)) ++pass.cityZonesIncludingQueued;
        if (itemID>=Structure_FirstID && itemID<=Structure_LastID)
            pass.cityStorageCapacityIncludingQueued+=std::max(0,data[itemID][houseID].capacity);
        if (!techInstallment) pass.money-=quotedPrice;
        pass.capitalOrderedCost+=quotedPrice;
        // Ordinary unit/zone orders are already forecast above;
        // only new optional MCV capital consumes this headroom.
        if (pass.capacityMcvRunway && itemID==Unit_MCV) pass.capacityMcvOrderedCost+=quotedPrice;
        if (AITelemetry::log().enabled()) pass.capitalOrders.set(std::to_string(turn.pBuilder->getObjectID())+":"+std::to_string(before),
            AITelemetry::Record().set("item",itemID).set("price",quotedPrice).set("rule",rule));
        if (turn.pBuilder->getItemID() == Structure_StarPort && AITelemetry::log().enabled())
            traceDecision("starport_purchase", AITelemetry::Record()
                .set("item",itemID).set("item_name",getItemNameByID(itemID))
                .set("market_price",quotedPrice).set("normal_price",data[itemID][houseID].price)
                .set("factory_price",getHouse()->radarUnitPrice(itemID,data[itemID][houseID].price))
                .set("rule",rule).set("spendable_after",pass.money));
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
        .set("spending_bucket", QuantBotSpendingPolicy::bucketName(spendingBucket))
        .set("composition_basis", pass.techSavings ? "existing_and_queued" : "funded_cap_headroom")
        .set("composition_before", compositionBefore)
        .set("composition", [&]() {
            AITelemetry::Record mix;
            for (size_t i = 0; i < UnitMixPolicy::kSlots; ++i)
                mix.set(std::to_string(kMixItems[i]), AITelemetry::Record()
                    .set("committed_value", pass.compositionValue[i]).set("target_bps", pass.unitMix[i])
                    .set("available", pass.compositionAvailable[i]));
            return mix;
        }())
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

bool QuantBot::buildTechSavings(BuildPass& pass, BuilderTurn& turn) {
    if (pass.vanillaBoomArmy && (pass.techResearch
        || !pass.techUpgrade || turn.pBuilder->getObjectID() != pass.vanillaBoomUpgradeBuilder)) return false;
    if (!pass.techSavingActive || pass.techResearch || !capitalPending(pass)
        || pass.capitalChoice != pass.techCandidateIndex
        || turn.pBuilder->getObjectID() != pass.techCandidate.builder) return false;
    // Reserve the funded upgrade's producer while its accepted units finish;
    // otherwise every busy factory appends another unit and never upgrades.
    if (turn.pBuilder->isUpgrading() || (turn.pBuilder->getProductionQueueSize() > 0
        && !pass.techUpgrade))
        return turn.pBuilder->getItemID() != Structure_ConstructionYard;
    const int cost = pass.techCandidate.cost;
    if (!spendingAdmits(pass, pass.techCandidate.item, cost, pass.techUpgrade)) return false;
    if (pass.techUpgrade && getHouse()->getCredits() < cost) return false;
    if (!pass.techUpgrade) {
        turn.installmentProject = pass.techCandidate.item;
        turn.installmentProjectStartBudget = 0;
    }
    if (pass.techUpgrade) {
        if (upgradeWithLogging(pass, turn, __LINE__)) pass.capitalConsumed = true;
        return !pass.vanillaBoomArmy || turn.pBuilder->isUpgrading();
    }
    if (queueFoundedStructure(pass, turn, pass.techCandidate.item, __LINE__, "saved_technology")) {
        ++pass.itemCount[pass.techCandidate.item];
        pass.orderedThisTick.insert(pass.techCandidate.item);
        pass.capitalConsumed = true;
    }
    return true;
}

bool QuantBot::upgradeWithLogging(BuildPass& pass, BuilderTurn& turn, int sourceLine) {
    if (!vanillaBoomUpgradeAdmitted(pass, turn.pBuilder)) return false;
    const int price=turn.pBuilder->getUpgradeCost();
    const bool simpleProject = pass.techSavings && pass.techSavingActive
        && pass.techUpgrade && pass.techCandidate.builder == turn.pBuilder->getObjectID();
    const bool affordable = (simpleProject ? getHouse()->getCredits() : pass.money) >= price;
    const bool funded=canFundProductionUpgrade(pass,turn.pBuilder);
    const bool bucketFunded = spendingAdmits(pass, turn.pBuilder->getItemID(), price, true);
    const bool accepted=affordable && funded && bucketFunded && doUpgrade(turn.pBuilder);
    if (accepted) {
        if (!simpleProject) pass.money-=price;
        pass.capitalOrderedCost+=price;
    }
    traceDecision("capital_upgrade",AITelemetry::Record().set("capital_plan",pass.capitalDecision)
        .set("builder",turn.pBuilder->getObjectID()).set("price",price).set("accepted",accepted)
        .set("reason",!affordable ? "shared_cash_budget" : !bucketFunded ? "unplanned_technology" : !funded ? "city_working_capital"
            : accepted ? "technology_unlock" : "unavailable")
        .set("source_line",sourceLine));
    return accepted;
}

// Campaign reconstruction and power orders do not pass through
// the standard construction selection below. Give them the same
// founded queue and per-yard site reservation.
bool QuantBot::queueFoundedStructure(BuildPass& pass, BuilderTurn& turn, Uint32 item, int sourceLine, const char* rule) {
    if (turn.pBuilder->getItemID()!=Structure_ConstructionYard
        || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0
        || !vanillaBoomAdmits(pass, item)
        || !campaignAvailableToBuild(turn.pBuilder,item) || !cityAdmitsStructure(pass, item)) return false;
    if (item == Structure_RepairYard && pass.repairCapacityDemand && !repairYardAdmitted(pass)) return false;
    planningBuilder=turn.pBuilder->getObjectID();
    clearPlacementCache(false,true);
    const Coord site=item==Structure_RepairYard && pass.repairCapacityDemand ? repairCapacitySite(pass)
        : item==Structure_RocketTurret || item==Structure_GunTurret
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
    const bool techInstallment = pass.techSavings && onInstallments(turn, item);
    if ((!techInstallment && pass.money < total) || !spendingAdmits(pass, item, total)) return false;
    const Uint32 previousSpendingItem = turn.spendingItem;
    turn.spendingItem = item;
    auto& places=builderPlaceLocations[planningBuilder];
    places.clear();
    // Concrete this building will not stand on is wasted cash.
    // If any part of the sequence is refused, give it all back.
    auto abandonFoundation=[&](size_t laid) {
        for(size_t i=0;i<laid;++i) doCancelItem(turn.pBuilder,plan.orders[i].item);
        places.clear();
        turn.spendingItem = previousSpendingItem;
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
    turn.spendingItem = previousSpendingItem;
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
        && data[Structure_Slab4][houseID].techLevel <= getHouse()->getTechLevel()
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
    if (pass.vanillaBoomArmy) return NONE_ID;
    if (!pass.vanillaEconomy || builderOnly() || pass.defendingEconomy
        || turn.pBuilder->getItemID()!=Structure_ConstructionYard
        || turn.pBuilder->isUpgrading() || turn.pBuilder->getProductionQueueSize()!=0) return NONE_ID;
    // Income and whatever production core this tree actually
    // offers come first; absent technology is not a blocker.
    auto coreEstablished=[&](Uint32 item) {
        return !data[item][houseID].enabled
            || data[item][houseID].techLevel>getHouse()->getTechLevel()
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
    if (campaignPolicyApplies()
        && !(pass.cityGrowthProtected && pass.capitalCandidates[pass.capitalChoice].builder==turn.pBuilder->getObjectID())
        && !(capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall)
        && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal
            || (difficulty==Difficulty::Medium && initialItemCount[Structure_RepairYard]>0))
        && data[Structure_RepairYard][houseID].enabled
        && getHouse()->getTechLevel()>=data[Structure_RepairYard][houseID].techLevel
        && turn.pBuilder->getItemID()==Structure_ConstructionYard
        && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize()==0
        && pass.itemCount[Structure_RepairYard]==0
        && getAIRefineryBays()>0
        && getAIHarvesterCount()>0
        && ((gameMode == GameMode::Campaign && !pass.citySimEnabled && difficulty != Difficulty::Brutal)
            || !pass.starportMarketAvailable || !data[Structure_StarPort][houseID].enabled
            || getHouse()->getTechLevel() < data[Structure_StarPort][houseID].techLevel
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
    turn.campaignLightWorkingCash = botConfig().force.campaignLightWorkingCash
        && !pass.citySimEnabled && !pass.techSavings && !builderOnly()
        && campaignPolicyApplies() && isAlliedWithHuman()
        && (!data[Structure_HeavyFactory][houseID].enabled
            || data[Structure_HeavyFactory][houseID].techLevel > getHouse()->getTechLevel());
    // The factory that has to supply the next yard: either because
    // the rock ran out under the old exhaustion rule, or because
    // the city is built out and has somewhere to settle. Both save
    // for that MCV instead of spending the same cash on units.
    const bool expansionNeeded = (pass.rockExpansionNeeded || pass.colonisationNeeded)
        && (pass.vanillaPlan == QuantBotVanillaPolicy::Plan::None || pass.itemCount[Structure_ConstructionYard] == 0);
    turn.expansionProducer = expansionNeeded && pass.itemCount[Unit_MCV] == 0
        && turn.pBuilder->getItemID() == Structure_HeavyFactory;
    const bool workerDue = !pass.vanillaBoomArmy || pass.itemCount[Unit_Harvester] < std::min(4, pass.vanillaBoomTargets.workers);
    const bool openingWorker = workerDue && openingWorkersNeeded(pass) && getHouse()->hasPower();
    turn.workerProducer = workerDue && turn.pBuilder->getItemID() == Structure_HeavyFactory
        && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
        && (openingWorker || (pass.brutalEconomy && factoryPrefersHarvester(pass, turn.pBuilder)));
    const bool firstCarryall = needsFirstTransport(pass) && pass.carryallBuildAvailable && getHouse()->hasPower();
    turn.transportProducer = firstCarryall && turn.pBuilder->getItemID() == Structure_HighTechFactory
        && campaignAvailableToBuild(turn.pBuilder,Unit_Carryall);
    int protectedCash = turn.pBuilder->getItemID() == Structure_ConstructionYard || turn.transportProducer || turn.workerProducer || turn.expansionProducer
        ? 0 : std::max({pass.strategicReserveCost,pass.economyReserve,pass.civicReserveCost});
    // A cramped start still needs power, income and a factory before
    // it can expand. Don't protect cash for an MCV we cannot build.
    if (expansionNeeded && pass.mcvBuildAvailable && pass.itemCount[Unit_MCV]==0 && !turn.expansionProducer)
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
    protectedCash = std::max(protectedCash,pass.civWarsResearchReserve);
    if (pass.techSavings && pass.militaryEmergency) protectedCash = 0;
    if (turn.pBuilder->getItemID()==Structure_ConstructionYard
        && storageExpansionItem(pass,turn.pBuilder)!=NONE_ID) protectedCash=0;
    // Commitments were already removed from pass.money. During a live attack,
    // spend the remaining economic savings on real ground replacements first.
    const Uint32 producer = turn.pBuilder->getItemID();
    if (pass.campaignThreatProduction
        && (producer == Structure_LightFactory || producer == Structure_HeavyFactory
            || producer == Structure_Barracks || producer == Structure_WOR)) protectedCash = 0;
    if (pass.vanillaFfaThreatProduction
        && (producer == Structure_LightFactory || producer == Structure_HeavyFactory
            || producer == Structure_Barracks || producer == Structure_WOR)) {
        // Only optional savings yield. Accepted queues are already deducted;
        // required cover, first transport and first-wave capital stay funded.
        protectedCash = std::max(pass.civicReserveCost,
            pass.protectionCapital ? capitalReserve(pass, turn.pBuilder->getObjectID()) : 0);
        if (firstCarryall && !turn.transportProducer)
            protectedCash = std::max(protectedCash, data[Unit_Carryall][houseID].price);
    }
    if (pass.vanillaFfaWaveCapitalReserve > 0 && pass.itemCount[Structure_RepairYard] == 0
        && producer != Structure_ConstructionYard)
        protectedCash = std::max(protectedCash, pass.vanillaFfaWaveCapitalReserve);
    if (turn.campaignLightWorkingCash && producer == Structure_LightFactory) {
        protectedCash = std::max(pass.strategicReserveCost, pass.civicReserveCost);
        // Keep the one-worker opening from spending every receipt on light
        // units. Queue-inclusive counting releases this seed as soon as its
        // refinery is funded; the worker does not have to be delivered.
        if (botConfig().force.campaignLightIncomeSeed
            && pass.itemCount[Structure_Refinery] < std::min(2, pass.fundedHarvesterTarget))
            protectedCash = std::max(protectedCash, buildingCapitalCost(pass, Structure_Refinery));
    }
    if (pass.vanillaBoomArmy && producer != Structure_ConstructionYard) {
        // Accepted queues have already been charged. Conversion releases
        // optional economic/tech savings, not the next real recovery quote.
        protectedCash = capitalReserve(pass, turn.pBuilder->getObjectID());
        if (pass.itemCount[Structure_ConstructionYard] == 0 && pass.itemCount[Unit_MCV] == 0
            && producer != Structure_HeavyFactory && producer != Structure_StarPort)
            protectedCash = std::max(protectedCash, data[Unit_MCV][houseID].price);
        if (openingWorker && !turn.workerProducer)
            protectedCash = std::max(protectedCash, data[Unit_Harvester][houseID].price);
        if (firstCarryall && !turn.transportProducer)
            protectedCash = std::max(protectedCash, data[Unit_Carryall][houseID].price);
    }
    // A lost processing bay must not compete afresh with optional imports,
    // workers or technology. Live ground replacement production keeps its
    // emergency spending; all other producers leave the yard's quote intact.
    if (producer != Structure_ConstructionYard
        && (!pass.campaignThreatProduction || (producer != Structure_LightFactory
            && producer != Structure_HeavyFactory && producer != Structure_Barracks
            && producer != Structure_WOR)))
        protectedCash = std::max(protectedCash, campaignRefineryReplacementCost(pass));
    // Optional capital ownership must not let another army producer consume
    // the replacement wallet; live emergencies make this floor inactive.
    if (producer != Structure_ConstructionYard
        && cityHarvesterReplacementFloor(pass) > pass.itemCount[Unit_Harvester])
        protectedCash = std::max(protectedCash, harvesterInvestmentReserve(pass));
    turn.reserve.reserved = pass.money - QuantBotBuildPolicy::spendableCredits(pass.money,protectedCash);
    pass.money -= turn.reserve.reserved;
}

bool QuantBot::buildCityHarvesterReplacement(BuildPass& pass, BuilderTurn& turn) {
    const int floor = cityHarvesterReplacementFloor(pass);
    if (pass.itemCount[Unit_Harvester] >= floor || getHouse()->isGroundUnitLimitReached()) return false;
    const bool port = turn.pBuilder->getItemID() == Structure_StarPort;
    if (!port && turn.pBuilder->getItemID() != Structure_HeavyFactory) return false;
    if (!campaignAvailableToBuild(turn.pBuilder, Unit_Harvester)) return false;
    // An idle factory supplies the worker without tying it up in a shipment.
    // Busy, upgrading or held factories leave the market lane available.
    if (port && cityHarvesterReplacementFactoryReady(pass)) return true;
    if (turn.pBuilder->isUpgrading() || turn.pBuilder->isOnHold()) return true;
    if (!port && turn.pBuilder->getProductionQueueSize() > 0) return true;
    const auto* starport = port ? static_cast<const StarPort*>(turn.pBuilder) : nullptr;
    if (starport && !starport->okToOrder()) return true;
    const int protectedCapital = std::max(capitalReserve(pass, turn.pBuilder->getObjectID()),
        cityBootstrapReserve(pass));
    const int availableReserve = std::max(0, turn.reserve.reserved - protectedCapital);
    pass.money += availableReserve;
    bool ordered = false;
    while (pass.itemCount[Unit_Harvester] < floor) {
        const Uint32 item = chooseTornieHarvester(turn.pBuilder, pass.money);
        const int price = purchasePrice(turn.pBuilder, item);
        if (price <= 0 || pass.money < price || turn.pBuilder->isUnitLimitReached(item)
            || (starport && getHouse()->getChoam().getNumAvailable(item) <= 0)) break;
        if (!produceItemWithLogging(pass, turn, Unit_Harvester, __LINE__, "harvester_replacement")) break;
        ++pass.itemCount[Unit_Harvester];
        pass.orderedSpiceHarvester = true;
        ordered = true;
        if (!port) break;
    }
    pass.money -= availableReserve;
    if (starport && ordered) doPlaceOrder(starport);
    // The idle factory waits for its protected price instead of buying army.
    return true;
}

bool QuantBot::buildCapitalUnits(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.vanillaBoomArmy && turn.pBuilder->getObjectID() == pass.vanillaBoomUpgradeBuilder
        && turn.pBuilder->getProductionQueueSize() == 0 && !turn.pBuilder->isUpgrading()
        && upgradeWithLogging(pass, turn, __LINE__)) {
        if (turn.capitalSupplier) pass.capitalConsumed = true;
        return true;
    }
    if (turn.capitalSupplier && pass.vanillaBoldCapital == pass.capitalChoice
        && std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "vanilla_bold_upgrade") {
        if (pass.money >= pass.capitalCandidates[pass.capitalChoice].price
            && turn.pBuilder->getProductionQueueSize() == 0 && !turn.pBuilder->isUpgrading()
            && upgradeWithLogging(pass, turn, __LINE__)) pass.capitalConsumed = true;
        if (!pass.vanillaBoomArmy || turn.pBuilder->isUpgrading()) return true;
    }
    // The capital allocator also offers workers ahead of the type-specific
    // military handler. Do not let it bypass the live-threat replacement rule.
    if ((pass.campaignThreatProduction || pass.vanillaFfaThreatProduction)
        && turn.pBuilder->getItemID() == Structure_HeavyFactory
        && canBuildMilitaryVehicle(pass, turn.pBuilder)) return false;
    // An optional capital choice owns the producer only when the same
    // unit/price admitted by the acceptance wrapper can actually be funded.
    const auto unitAdmitted = [&](Uint32 item) {
        if (!vanillaBoomAdmits(pass, item)) return false;
        if (!pass.techSavings) return true;
        if (item == Unit_MCV && pass.yardLimit > 0
            && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] >= pass.yardLimit) return false;
        const Uint32 actualItem = item == Unit_Harvester ? chooseTornieHarvester(turn.pBuilder, pass.money) : item;
        const int price = purchasePrice(turn.pBuilder, actualItem);
        return (pass.money >= price || spendingEmergency(pass, actualItem))
            && spendingAdmits(pass, actualItem, price);
    };
    if (turn.capitalSupplier && capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall
        && pass.money<pass.capitalCandidates[pass.capitalChoice].price && unitAdmitted(Unit_Carryall)) {
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
        && vanillaBoomAdmits(pass, Unit_MCV)
        && turn.pBuilder->getItemID()==Structure_HeavyFactory
        && !campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
        && turn.pBuilder->getCurrentUpgradeLevel()<turn.pBuilder->getMaxUpgradeLevel()
        && DuneCity::prioritizeVanillaMcv(pass.money,getAIHarvesterCount(),
            pass.itemCount[Structure_ConstructionYard],pass.itemCount[Unit_MCV],data[Unit_MCV][houseID].price)
        && (!pass.techSavings || ((pass.yardLimit <= 0
                || pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] < pass.yardLimit)
            && spendingAdmits(pass, turn.pBuilder->getItemID(), turn.pBuilder->getUpgradeCost(), true)));
    const bool parallelHeavyProduction=pass.fundedCityProduction
        && turn.pBuilder->getItemID()==Structure_HeavyFactory;
    if (!turn.transportProducer && !turn.expansionProducer && !unlockingVanillaMcv && !parallelHeavyProduction)
        for (const auto& candidate:pass.capitalCandidates)
            if (candidate.builder==turn.pBuilder->getObjectID() && isUnit(candidate.item)
                && (candidate.item!=Unit_Carryall || pass.itemCount[Unit_Carryall]<pass.transportTarget)
                && !QuantBotBuildPolicy::militaryItem(candidate.item)
                && (!turn.capitalSupplier || candidate.item==pass.capitalCandidates[pass.capitalChoice].item)
                && candidate.score>0 && candidate.price<=pass.money
                && (!localUnitChoice || candidate.score>localUnitChoice->score)
                && unitAdmitted(candidate.item)) localUnitChoice=&candidate;
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
        && !unlockingVanillaMcv && !parallelHeavyProduction
        && unitAdmitted(pass.capitalCandidates[pass.capitalChoice].item)) return true;

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
    // A finite opening raid, not a permanent light-unit composition.
    if (duelOpeningPlan() == 2 && pass.lightVehicleCount < 4
        && !turn.pBuilder->isUpgrading() && turn.pBuilder->getProductionQueueSize() == 0) {
        for (const Uint32 item : {Uint32(Unit_Quad), Uint32(Unit_RaiderTrike), Uint32(Unit_Trike)}) {
            if (!campaignAvailableToBuild(turn.pBuilder, item) || pass.money < purchasePrice(turn.pBuilder, item)) continue;
            if (produceItemWithLogging(pass, turn, item, __LINE__, "duel_opening_raid")) {
                ++pass.itemCount[item];
                ++pass.lightVehicleCount;
                pass.lightVehicleValue += data[item][houseID].price;
                pass.militaryValue += data[item][houseID].price;
                return;
            }
        }
    }
    const int lightPlanValue = turn.campaignLightWorkingCash
        ? std::max(0, QuantBotBuildPolicy::fundedArmyTarget(pass.militaryValue, pass.lightBudget, pass.money)
            - pass.infantryValue)
        : pass.vehiclePlanValue;
    if (!turn.pBuilder->isUpgrading()
        && pass.money > (pass.vanillaBoomArmy || (pass.techSavings && pass.militaryEmergency) ? 0
            : (turn.campaignLightWorkingCash || pass.vanillaFfaLightWorkingCash) ? 0
            : pass.citySimEnabled ? 500 : 700)
        && turn.pBuilder->getProductionQueueSize() < 1
        && turn.pBuilder->getBuildListSize() > 0
        && pass.militaryValue < pass.lightBudget
        && (pass.vanillaBoomArmy || (pass.techSavings
            ? (pass.militaryEmergency || UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
                pass.compositionPrice, pass.compositionAvailable) >= UnitMixPolicy::kFirstLightSlot)
            : ((pass.citySimEnabled && pass.itemCount[Structure_HeavyFactory] == 0)
                || ((!pass.learningUnitMix && pass.lightVehicleCount < 2)
                    || int64_t(pass.lightVehicleValue) * 10000 < int64_t(lightPlanValue) * pass.lightVehicleBps))))) {

        if (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel()
            && vanillaBoomUpgradeAdmitted(pass, turn.pBuilder)
            && !(pass.techSavings && pass.militaryEmergency)
            && (!pass.vanillaFfaLightWorkingCash || pass.money > 700)
            && getHouse()->getCredits() > 1500 && canFundProductionUpgrade(pass,turn.pBuilder)
            && spendingAdmits(pass, turn.pBuilder->getItemID(), turn.pBuilder->getUpgradeCost(), true)) {
            upgradeWithLogging(pass, turn, __LINE__);
        }
        else if (!getHouse()->isGroundUnitLimitReached()) {
            Uint32 itemID = NONE_ID;
            int64_t bestDeficit = 0;
            for (size_t i=UnitMixPolicy::kFirstLightSlot; i<kSlots; ++i) {
                const Uint32 candidate = mixItems[i];
                if (!campaignAvailableToBuild(turn.pBuilder,candidate)) continue;
                if (pass.vanillaBoomArmy && (purchasePrice(turn.pBuilder, candidate) > pass.money
                    || int64_t(pass.militaryValue) + data[candidate][houseID].price > pass.lightBudget)) continue;
                if (pass.techSavings) {
                    if (i == UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
                            pass.compositionPrice, pass.compositionAvailable)) itemID = candidate;
                    continue;
                }
                const auto deficit = UnitMixPolicy::deficit(pass.unitMix[i], lightPlanValue,
                    pass.itemCount[candidate], data[candidate][houseID].price);
                if ((pass.vanillaBoomArmy && itemID == NONE_ID) || deficit > bestDeficit) { bestDeficit = deficit; itemID = candidate; }
            }
            if (itemID == NONE_ID && pass.techSavings && (pass.militaryEmergency || pass.vanillaBoomArmy))
                for (size_t i = UnitMixPolicy::kFirstLightSlot; i < kSlots; ++i)
                    if (campaignAvailableToBuild(turn.pBuilder, mixItems[i])
                        && purchasePrice(turn.pBuilder, mixItems[i]) <= pass.money
                        && (itemID == NONE_ID || data[mixItems[i]][houseID].price < data[itemID][houseID].price))
                        itemID = mixItems[i];
            // Bootstrap a small sample before combat learning begins.
            if (itemID == NONE_ID && !pass.techSavings && !pass.learningUnitMix && pass.lightVehicleCount < 2)
                for (size_t i=UnitMixPolicy::kFirstLightSlot; i<kSlots; ++i)
                    if (campaignAvailableToBuild(turn.pBuilder,mixItems[i])
                        && (itemID == NONE_ID || pass.itemCount[mixItems[i]] < pass.itemCount[itemID])) itemID = mixItems[i];
            if (itemID != NONE_ID && int64_t(pass.militaryValue) + data[itemID][houseID].price <= pass.lightBudget
                && produceItemWithLogging(pass, turn, itemID, __LINE__,
                    pass.techSavings ? (pass.militaryEmergency ? "emergency_affordable_light" : "existing_army_composition")
                        : pass.citySimEnabled && pass.itemCount[Structure_HeavyFactory] == 0 ? "city_no_heavy_light_replenishment"
                        : !pass.learningUnitMix && pass.lightVehicleCount < 2 ? "opening_light_sample" : "funded_light_mix")) {
                ++pass.itemCount[itemID];
                ++pass.lightVehicleCount;
                pass.lightVehicleValue += data[itemID][houseID].price;
                pass.militaryValue += data[itemID][houseID].price;

            }
        }
    }
}

Uint32 QuantBot::supportTrooperOrder(BuildPass& pass, const BuilderBase* builder, int money, int target) {
    if (builder->isUpgrading() || builder->getProductionQueueSize() > 0
        || getHouse()->isGroundUnitLimitReached()) return NONE_ID;
    const int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? builder->getTechnologyHouseID() : getHouse()->getHouseID();
    const auto& data = currentGame->objectData.data;
    Uint32 item = NONE_ID;
    int bestPrice = 0, bestSize = 0;
    for (const Uint32 candidate : {Unit_Trooper, Unit_Troopers}) {
        const int size = QuantBotBuildPolicy::rocketInfantrySize(candidate);
        if (pass.trooperCount + size > target || !campaignAvailableToBuild(builder, candidate)
            || !trooperOrderAdmitted(pass, candidate, data[candidate][houseID].price)) continue;
        const int price = purchasePrice(builder, candidate);
        if (price > money) continue;
        if (item == NONE_ID || int64_t(price) * bestSize < int64_t(bestPrice) * size
            || (int64_t(price) * bestSize == int64_t(bestPrice) * size && size > bestSize)) {
            item = candidate; bestPrice = price; bestSize = size;
        }
    }
    return item;
}

void QuantBot::buildWOR(BuildPass& pass, BuilderTurn& turn) {
    int houseID = ModManager::instance().isTornieFamilyContentActive()
        ? turn.pBuilder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.trooperSupport) {
        const bool floorSupplier = turn.capitalSupplier
            && std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "trooper_recruitment";
        const int64_t reserve = floorSupplier ? pass.cityWorCashReserve
            : (pass.citySimEnabled ? pass.cityWorkingReserve : 0);
        const int target = floorSupplier
            ? std::min(pass.trooperTarget, QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft))
            : pass.trooperTarget;
        const Uint32 item = supportTrooperOrder(pass, turn.pBuilder,
            int(std::max<int64_t>(0, int64_t(pass.money) - reserve)), target);
        if (item != NONE_ID && produceItemWithLogging(pass, turn, item, __LINE__,
                pass.hostileAircraft > 0 ? "trooper_anti_air"
                    : pass.techSavings ? "existing_infantry_composition"
                    : pass.trooperCount < QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft)
                        ? "trooper_standing_floor" : "trooper_infantry_share")) {
            ++pass.itemCount[item];
            pass.infantryCount += QuantBotBuildPolicy::rocketInfantrySize(item);
            pass.infantryValue += data[item][houseID].price;
            pass.militaryValue += data[item][houseID].price;
            if (floorSupplier) pass.capitalConsumed = true;
        }
        return;
    }
    // A city raises rocket infantry only while a hostile wing is actually
    // observed, and then only inside the ordinary infantry quota, because they
    // supplement the launcher share and the turrets rather than replacing them.
    // The opening bootstrap of three stays a non-city rule: a city has no
    // standing infantry role to bootstrap.
    if ((!pass.citySimEnabled || pass.cityAntiAirInfantryDemand)
        && !turn.pBuilder->isUpgrading()
        && turn.pBuilder->getProductionQueueSize() < 1
        && turn.pBuilder->getBuildListSize() > 0
        && pass.money > (pass.vanillaBoomArmy || (pass.techSavings && pass.militaryEmergency) ? 0 : 450)
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
            if (pass.vanillaBoomArmy && purchasePrice(turn.pBuilder, candidate) > pass.money) continue;
            if (candidate >= Unit_Troopers5
                && (purchasePrice(turn.pBuilder,candidate) > pass.money || !infantryQuotaAdmits(pass, value))) continue;
            if (int64_t(pass.militaryValue)+value > pass.lightBudget) continue;
            if (pass.citySimEnabled
                && (purchasePrice(turn.pBuilder,candidate) > pass.money - pass.cityWorkingReserve
                    || !infantryQuotaAdmits(pass, value))) continue;
            if (itemID == NONE_ID || pass.itemCount[candidate] < pass.itemCount[itemID]
                || (candidate == Unit_Troopers5 && pass.itemCount[candidate] == pass.itemCount[itemID])) itemID = candidate;
        }
        if (itemID != NONE_ID && produceItemWithLogging(pass, turn, itemID, __LINE__,
                !pass.citySimEnabled && pass.infantryCount < 3 ? "wor_opening_sample" : "wor_existing_infantry_share")) {
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
        && pass.money > (pass.vanillaBoomArmy || (pass.techSavings && pass.militaryEmergency) ? 0 : 300)
        && pass.militaryValue < pass.lightBudget
        && !getHouse()->isGroundUnitLimitReached()
        && (pass.infantryCount < 3
            || pass.infantryValue * 100 < std::max(pass.militaryValue, 1) * pass.infantryPercent)) {
        Uint32 itemID = NONE_ID;
        for (Uint32 candidate : {Unit_Soldier, Unit_Infantry, Unit_Infantry5, Unit_GasInfantry, Unit_FlameTrooper}) {
            if (candidate >= Unit_Troopers5 && !ModManager::instance().isTornieFamilyContentActive()) continue;
            if (pass.vanillaBoomArmy && (purchasePrice(turn.pBuilder, candidate) > pass.money
                || int64_t(pass.militaryValue) + data[candidate][houseID].price > pass.lightBudget)) continue;
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
            && produceItemWithLogging(pass, turn, itemID, __LINE__,
                pass.infantryCount < 3 ? "barracks_opening_sample" : "barracks_existing_infantry_share")) {
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
    air.canUpgrade = turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel()
        && vanillaBoomUpgradeAdmitted(pass, turn.pBuilder);
    air.spendable = pass.money; // Economy/strategic reserves were removed above.
    air.ornithopterPrice = ornithopterPrice;
    air.carryallPrice = carryallPrice;
    air.carryalls = pass.itemCount[Unit_Carryall];
    air.carryallTarget = pass.vanillaBoomArmy ? std::min(1, pass.transportTarget) : pass.transportTarget;
    air.armyValue = pass.militaryValue;
    air.armyLimit = pass.lightBudget;
    air.vehiclePlanValue = pass.vehiclePlanValue;
    air.airCommittedValue = ornithopterValue;
    air.airTargetBps = pass.unitMix[kOrnithopterSlot];
    air.airStrategy = pass.strategyMixProfile && strategyStart[kOrnithopterSlot] > 0;
    air.credits = getHouse()->getCredits();
    bool growthHeld = false;
    if (pass.techSavings && !air.busy && !air.upgrading && !air.airLimit) {
        if (air.carryallAvailable && air.carryalls < air.carryallTarget
            && !spendingAdmits(pass, Unit_Carryall, purchasePrice(turn.pBuilder, Unit_Carryall))) {
            air.carryallAvailable = false;
            growthHeld = true;
        }
        if (air.canUpgrade) {
            const int heldCash = air.airStrategy && !air.ornithopterAvailable
                ? std::max(0, getHouse()->getCredits() - pass.money) : 0;
            pass.money += heldCash;
            const bool admitted = !pass.militaryEmergency && pass.money >= turn.pBuilder->getUpgradeCost()
                && canFundProductionUpgrade(pass, turn.pBuilder)
                && spendingAdmits(pass, turn.pBuilder->getItemID(), turn.pBuilder->getUpgradeCost(), true);
            pass.money -= heldCash;
            air.canUpgrade = admitted;
            growthHeld = growthHeld || !admitted;
        }
    }
    auto decision = QuantBotBuildPolicy::chooseAirProduction(air);
    if (pass.techSavings && !air.busy && !air.upgrading && !air.airLimit && air.ornithopterAvailable
        && (decision.order == QuantBotBuildPolicy::AirOrder::None
            || decision.order == QuantBotBuildPolicy::AirOrder::Ornithopter)
        && int64_t(pass.militaryValue) + ornithopterPrice <= pass.lightBudget) {
        decision = compositionAdmits(pass, ornithopter) && pass.money >= ornithopterPrice
            ? QuantBotBuildPolicy::AirDecision{QuantBotBuildPolicy::AirOrder::Ornithopter,
                growthHeld ? "existing_army_composition_after_growth_hold" : "existing_army_composition"}
            : QuantBotBuildPolicy::AirDecision{QuantBotBuildPolicy::AirOrder::None, "existing_air_share_satisfied"};
    }
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("air_production_decision", AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("reason",decision.reason).set("growth_held",growthHeld)
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
    bool growthHeld = false;
    // Do not enter an exclusive economy branch only to have the shared
    // acceptance wrapper reject its wallet (or an already-full yard cap).
    // Resolve Tornie's worker against the same cash used by the real order.
    const auto growthAdmitted = [&](Uint32 item, int cash, bool upgrade = false) {
        if (upgrade ? !vanillaBoomUpgradeAdmitted(pass, turn.pBuilder) : !vanillaBoomAdmits(pass, item)) {
            growthHeld = true;
            return false;
        }
        if (!pass.techSavings) return true;
        const Uint32 actualItem = upgrade ? turn.pBuilder->getItemID()
            : item == Unit_Harvester ? chooseTornieHarvester(turn.pBuilder, cash) : item;
        const int price = upgrade ? turn.pBuilder->getUpgradeCost() : purchasePrice(turn.pBuilder, actualItem);
        const bool yardFull = item == Unit_MCV && pass.yardLimit > 0
            && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] >= pass.yardLimit;
        const bool admitted = !yardFull && cash >= price && spendingAdmits(pass, actualItem, price, upgrade);
        if (!admitted) {
            growthHeld = true;
            if (pass.emitStatsLog && AITelemetry::log().enabled())
                traceDecision("factory_growth_hold", AITelemetry::Record()
                    .set("builder", turn.pBuilder->getObjectID()).set("item", item).set("actual_item", actualItem)
                    .set("upgrade", upgrade).set("price", price).set("spendable", cash)
                    .set("reason", yardFull ? "yard_limit" : cash < price ? "shared_cash_budget" : "unplanned_technology"));
        }
        return admitted;
    };
    const bool prioritizeCityMcv = !pass.postponeCapacityMcv && pass.citySimEnabled && gameMode == GameMode::Custom
        && (pass.fundedCityProduction || !openingWorkersNeeded(pass) || pass.rockExpansionNeeded)
        && !getHouse()->isGroundUnitLimitReached()
        && canFundCapacityMcv(pass,data[Unit_MCV][houseID].price,turn.pBuilder->getObjectID())
        && (turn.expansionProducer || QuantBotBuildPolicy::canFundCityYard(cityMcvCash, data[Unit_MCV][houseID].price,
            pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV], pass.cityYardTarget, pass.cityWorkingReserve));
    // Test-only expansion style (docs/ai-play-styles.md): keep at least
    // PlayStyle::minYards yards once the first heavy factory stands.
    const int styleYardShortfall = pass.vanillaEconomy && gameMode == GameMode::Custom
        && pass.itemCount[Structure_HeavyFactory] > 0
        ? style().minYards - pass.itemCount[Structure_ConstructionYard] - pass.itemCount[Unit_MCV] : 0;
    const bool prioritizeMcv = vanillaBoomAdmits(pass, Unit_MCV)
        && ((fieldExpansion.yard.isValid() && pass.colonisationNeeded
             && pass.money >= data[Unit_MCV][houseID].price + 1000) || prioritizeCityMcv
            || (!pass.postponeCapacityMcv && pass.vanillaEconomy && gameMode == GameMode::Custom
        && !getHouse()->isGroundUnitLimitReached()
        && (DuneCity::prioritizeVanillaMcv(pass.money, getAIHarvesterCount(),
            pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV], data[Unit_MCV][houseID].price)
            || (styleYardShortfall > 0 && pass.money >= data[Unit_MCV][houseID].price + 1000))));
    // A built-out city needs one yard it cannot express as a
    // capacity shortfall, so the factory may still unlock MCV
    // production for it.
    const int mcvShortfall = pass.citySimEnabled
        ? std::max(pass.colonisationNeeded ? 1 : 0,
            pass.cityYardTarget - pass.itemCount[Structure_ConstructionYard] - pass.itemCount[Unit_MCV])
        : std::max(pass.colonisationNeeded ? 1 : 0,
            std::max(styleYardShortfall, DuneCity::vanillaMcvShortfall(pass.money, getAIHarvesterCount(),
                pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV])));
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
            && canFundCapacityMcv(pass,purchasePrice(turn.pBuilder,Unit_MCV),turn.pBuilder->getObjectID())
            && growthAdmitted(Unit_MCV, pass.money)) {
            if (produceItemWithLogging(pass, turn, Unit_MCV, __LINE__,
                    pass.citySimEnabled ? "city_cash_construction_capacity" : "cash_construction_capacity")) {
                ++pass.itemCount[Unit_MCV];
            }
        }
        else if (prioritizeMcv && pass.mcvUpgradesInProgress < mcvShortfall
            && !campaignAvailableToBuild(turn.pBuilder,Unit_MCV)
            && turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel()
            && (pass.citySimEnabled ? cityMcvCash : pass.money) >= turn.pBuilder->getUpgradeCost()
                + (turn.expansionProducer ? 0 : pass.citySimEnabled ? std::max(1000, pass.cityWorkingReserve) : 1000)
            && (!pass.techSavings || canFundProductionUpgrade(pass, turn.pBuilder))
            && growthAdmitted(Unit_MCV, pass.money, true)) {
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
            && vanillaBoomAdmits(pass, Unit_Harvester)
            && factoryPrefersHarvester(pass, turn.pBuilder)
            && pass.itemCount[Unit_Harvester] < (pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget)
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester) && !getHouse()->isGroundUnitLimitReached()
            && pass.money + ((pass.vanillaEconomy || harvesterInvestmentReserve(pass) > 0) ? turn.reserve.reserved : 0) >= data[Unit_Harvester][houseID].price
                + ((pass.campaignEconomyPush || harvesterInvestmentReserve(pass) > 0) ? 0 : (openingWorkersNeeded(pass) || pass.brutalEconomy) ? 0 : pass.vanillaEconomy ? 1000 : data[Unit_Tank][houseID].price)
            && (!pass.techSavings || growthAdmitted(Unit_Harvester,
                pass.money + ((pass.vanillaEconomy || harvesterInvestmentReserve(pass) > 0)
                    ? std::max(0, turn.reserve.reserved - capitalReserve(pass, turn.pBuilder->getObjectID())) : 0)))) {
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
        else if ((pass.money > 10000) && (turn.pBuilder->isUpgrading() == false)
            && !(pass.techSavings && pass.militaryEmergency)
            && (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel())
            && canFundProductionUpgrade(pass,turn.pBuilder)
            && growthAdmitted(turn.pBuilder->getItemID(), pass.money, true)) {
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
            && canFundCapacityMcv(pass,purchasePrice(turn.pBuilder,Unit_MCV),turn.pBuilder->getObjectID())
            && pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] < std::min(8, pass.money / 4000)
            && growthAdmitted(Unit_MCV, pass.money)) {
            if (produceItemWithLogging(pass, turn, Unit_MCV, __LINE__, "construction_capacity")) {
                pass.itemCount[Unit_MCV]++;
            }
        }
        else if (gameMode == GameMode::Custom && !pass.vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
            && !getHouse()->isGroundUnitLimitReached()
            && pass.itemCount[Unit_Harvester] < pass.militaryValue / 1000
            && pass.itemCount[Unit_Harvester] < harvesterLimit && lastCalculatedSpice > 0
            && growthAdmitted(Unit_Harvester, pass.money)) {
            // In case we get given lots of money, it will eventually run out so we need to be prepared
            // Classic-mode harvester expansion; city spice investment is handled above.
            if (produceItemWithLogging(pass, turn, Unit_Harvester, __LINE__)) pass.itemCount[Unit_Harvester]++;
        }
        else if (!pass.vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
            && lastCalculatedSpice > 0
            && pass.itemCount[Unit_Harvester] < harvesterLimit
            && campaignAvailableToBuild(turn.pBuilder,Unit_Harvester)
            && !getHouse()->isGroundUnitLimitReached()
            && (pass.money < 2000 || gameMode == GameMode::Campaign)
            && growthAdmitted(Unit_Harvester, pass.money)) {
            // Classic-mode recovery; city spice investment is handled above.
            if (produceItemWithLogging(pass, turn, Unit_Harvester, __LINE__)) pass.itemCount[Unit_Harvester]++;
        }
        else if ((pass.money > 500) && (turn.pBuilder->isUpgrading() == false)
            && !(pass.techSavings && pass.militaryEmergency)
            && (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel())
            && canFundProductionUpgrade(pass,turn.pBuilder)
            && growthAdmitted(turn.pBuilder->getItemID(), pass.money, true)) {
            // Upgrade before military — unlocks MCV(1), Launcher(2), SiegeTank(3)
            if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
                upgradeWithLogging(pass, turn, __LINE__);
            }
            else {
                doRepair(turn.pBuilder);
            }
        }
        else if (pass.money > (pass.vanillaBoomArmy || pass.techSavings ? 0 : pass.citySimEnabled || pass.vanillaEconomy ? 500 : 2000)
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
            int selected = -1;
            bool expansionFallback = false;
            if (pass.techSavings) {
                const size_t slot = UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
                    pass.compositionPrice, pass.compositionAvailable);
                if (slot < types.size() && candidates[slot].available && candidates[slot].price <= pass.money
                    && int64_t(pass.militaryValue) + candidates[slot].price <= pass.militaryBudget)
                    selected = int(slot);
                if (selected < 0 && (pass.militaryEmergency || pass.vanillaBoomArmy))
                    selected = QuantBotBuildPolicy::largestAffordableDeficit(
                        candidates, pass.militaryValue, pass.money, pass.militaryBudget);
            } else {
                selected = QuantBotBuildPolicy::fundedDeficit(
                    candidates,pass.militaryValue,pass.money,pass.militaryBudget,pass.vehiclePlanValue);
                expansionFallback = selected < 0;
                if (expansionFallback) selected = QuantBotBuildPolicy::capacityFill(
                    candidates,pass.militaryValue,pass.money,pass.militaryBudget);
            }
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
                        : pass.techSavings ? (growthHeld ? "existing_army_composition_after_growth_hold" : "existing_army_composition")
                        : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit"));
                lastHeavyAllocationTrace[turn.pBuilder->getObjectID()] = {allocationSignature, allocationCycle};
            }
            if (selected>=0 && produceItemWithLogging(pass, turn, types[selected],__LINE__,
                pass.techSavings ? (growthHeld ? "existing_army_composition_after_growth_hold" : "existing_army_composition")
                    : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit")) {
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
        const auto& policy = botConfig().force;
        const bool scope = (policy.starportBuying > 0 || policy.starportCashStockpile > 0
                || policy.starportEconomyPriority > 0)
            && gameMode == GameMode::Custom
            && (getGameInitSettings().getModName() == "vanilla"
                || ModManager::instance().isDuneCityContentActive())
            && (getGameInitSettings().getGameType() == GameType::CustomGame
                || getGameInitSettings().getGameType() == GameType::CustomMultiplayer)
            && getGameInitSettings().getInitialSpiceTilePermille() > 0;
        const int priceLevel = scope ? policy.starportBuying : 0;
        const int economyPriority = scope ? policy.starportEconomyPriority : 0;

        // Economic imports may use the cash held for the economy,
        // just as factory-built harvesters do. Market discounts are
        // irrelevant to needed workers and the first transport.
        int workerTarget = pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget;
        // A bargain worker is a cheap economy upgrade for a human
        // ally. Fill the permitted fleet instead of stopping at the
        // normal-price, remaining-spice planning target.
        const bool bargainWorkers = campaignPolicyApplies()
            && isAlliedWithHuman() && lastCalculatedSpice > 0
            && purchasePrice(turn.pBuilder,Unit_Harvester) > 0
            && purchasePrice(turn.pBuilder,Unit_Harvester) < data[Unit_Harvester][houseID].price;
        if (bargainWorkers) {
            if (getHouse()->getMaxHarvesters() > 0)
                workerTarget = getHouse()->getMaxHarvesters();
        }
        if (pass.vanillaBoomArmy) workerTarget = std::min(workerTarget, 4);
        // The shared reservation protects the next construction,
        // transport or combat order. Spend the remainder on useful
        // workers without applying a second arbitrary half-cash cap.
        auto buyEconomicImport = [&](Uint32 item, const char* rule) {
            if (!vanillaBoomAdmits(pass, item)) return false;
            const int price = purchasePrice(turn.pBuilder,item);
            const bool plannedMcv = economyPriority > 1 && item == Unit_MCV && capitalPending(pass)
                && pass.capitalCandidates[pass.capitalChoice].item == Unit_MCV;
            const Uint32 reserveOwner = plannedMcv ? pass.capitalCandidates[pass.capitalChoice].builder
                : turn.pBuilder->getObjectID();
            const int portReserve = std::max(capitalReserve(pass, reserveOwner),cityBootstrapReserve(pass));
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

        // Do not raise the fleet or yard target: this only changes which of
        // the already-funded economic orders gets a scarce market window.
        if (economyPriority > 0) {
            while (pass.itemCount[Unit_Harvester] < workerTarget)
                if (!buyEconomicImport(Unit_Harvester, "starport_priority_worker")) break;
            if (economyPriority > 1 && capitalPending(pass)
                && pass.capitalCandidates[pass.capitalChoice].item == Unit_MCV
                && StarportBuyingPolicy::admitted(purchasePrice(turn.pBuilder,Unit_MCV),
                    getHouse()->radarUnitPrice(Unit_MCV,data[Unit_MCV][houseID].price),priceLevel))
                buyEconomicImport(Unit_MCV, "starport_priority_planned_mcv");
        }

        // The priority purchase above imports the colonist. If
        // it is still unaffordable, preserve this port's savings.
        if (turn.capitalSupplier && capitalPending(pass)
            && pass.capitalCandidates[pass.capitalChoice].item==Unit_MCV
            && vanillaBoomAdmits(pass, Unit_MCV)
            && (!pass.techSavings || ((pass.yardLimit <= 0
                    || pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV] < pass.yardLimit)
                && spendingAdmits(pass, Unit_MCV, purchasePrice(turn.pBuilder, Unit_MCV))))) return;
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
        while (!pass.vanillaBoomArmy && transportUseful && harvesterInvestmentReserve(pass) == 0
            && pass.itemCount[Unit_Carryall] < pass.transportTarget)
            if (!buyEconomicImport(Unit_Carryall, "starport_transport_capacity")) break;

        // Imports are opportunistic: every discounted combat type
        // is eligible, regardless of the factory composition targets.
        // Buy the best percentage discount first, with stable item-ID
        // tie breaking. Keep the military-value budget and normal caps.
        std::vector<Uint32> bargains;
        for (const auto& offer : pStarPort->getBuildList()) {
            const Uint32 unit = offer.itemID;
            const int factory = priceLevel == 0 ? data[unit][houseID].price
                : getHouse()->radarUnitPrice(unit,data[unit][houseID].price);
            if (!isUnit(unit) || unit == Unit_Harvester || unit == Unit_Carryall
                || unit == Unit_MCV || !StarportBuyingPolicy::admitted(
                    purchasePrice(turn.pBuilder,unit),factory,priceLevel)) continue;
            bargains.push_back(unit);
        }
        std::sort(bargains.begin(), bargains.end(), [&](Uint32 a, Uint32 b) {
            const int64_t left = int64_t(purchasePrice(turn.pBuilder,a)) * data[b][houseID].price;
            const int64_t right = int64_t(purchasePrice(turn.pBuilder,b)) * data[a][houseID].price;
            return left != right ? left < right : a < b;
        });
        // A bargain is still optional spending: it may not come
        // out of the cash a no-spice city needs to start earning.
        const int bargainReserve = pass.vanillaBoomArmy ? turn.reserve.reserved
            : std::max({harvesterInvestmentReserve(pass),
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
