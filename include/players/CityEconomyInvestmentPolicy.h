#ifndef CITY_ECONOMY_INVESTMENT_POLICY_H
#define CITY_ECONOMY_INVESTMENT_POLICY_H

#include <players/QuantBotBuildPolicy.h>
#include <dunecity/CityEffects.h>

namespace CityEconomyInvestmentPolicy {
// Compare both investments over four simulated minutes, including the time
// before their first income. These are forecasts, not measured cash flows.
constexpr int horizonCycles = 4 * DuneCity::kCyclesPerCityYear;
struct Investment {
    int cost = 0;
    int annualIncome = 0;
    int annualUpkeep = 0;
    int delayCycles = 0;
    int confidence = 1000;
    int projectedProceeds = -1; // Optional delivery-cycle forecast, before confidence.
    int proceeds() const {
        if (projectedProceeds >= 0)
            return int(int64_t(projectedProceeds) * std::clamp(confidence,0,1000) / 1000);
        return static_cast<int>(int64_t(std::max(0,annualIncome-annualUpkeep))
            * std::max(0,horizonCycles-delayCycles) * std::clamp(confidence,0,1000)
            / (DuneCity::kCyclesPerCityYear * 1000));
    }
};
// Refinery capacity must follow the fleet, never cap factory production.
inline int factoryHarvesterTarget(int sustainableWorkers, int mapLimit) {
    return std::max(0, std::min(sustainableWorkers, mapLimit));
}
// A small opening fleet must compound before optional technology. This is a
// priority floor bounded by remaining spice/map capacity, never a worker cap.
// A flat four workers under-invests on a spice-rich map whose sustainable
// target is far larger, so the floor follows half the field's own target
// between the established four/eight minimum and a bounded eight/twelve
// ceiling. Depleted fields keep their smaller opening unchanged.
inline int openingWorkerFloor(int target, bool brutal) {
    const int minimum = brutal ? 8 : 4;
    return std::min(std::clamp(std::max(0,target)/2, minimum, brutal ? 12 : 8),
                    std::max(0,target));
}
inline bool openingWorkersNeeded(int workers, int target, bool brutal = false) {
    return workers < openingWorkerFloor(target,brutal);
}
// This is production priority, not a worker cap. While the army is short,
// keep twice the harvester capital in military strength (equal capital on
// Brutal city games, allowing faster compounding). Rebuild
// a collapsed workforce first; once army needs are met, expand to the spice target.
inline bool preferFactoryHarvester(int workers, int target, int armyValue,
                                   int armyTarget, int workerPrice, bool canBuildMilitary, bool cityOpening = false, bool brutal = false) {
    if (workers >= target) return false;
    if ((cityOpening && openingWorkersNeeded(workers,target,brutal)) || workers < 2 || !canBuildMilitary || armyValue >= armyTarget) return true;
    return int64_t(armyValue) >= std::min<int64_t>(armyTarget,
        int64_t(std::max(0,workers))*std::max(0,workerPrice)*(cityOpening && brutal ? 1 : 2));
}
// A profitable refinery may supply the opening fleet even beyond four bays.
// Remaining spice, worker demand and the return comparison bound expansion;
// once the opening workforce exists, actual throughput governs extra bays.
inline bool openingRefineryInvestment(bool brutal, int workers, int target, int refineries) {
    return openingWorkersNeeded(workers,target,brutal)
        && refineries < std::max(0,target);
}
// A city map whose remaining spice cannot support one worker earns nothing
// from a processing bay, a port's income ladder or an imported transport. The
// sustainable worker target is the line, because it is already the remaining
// field divided by the active houses and recomputed from the live map: an
// initially rich map that later depletes crosses it without a second
// threshold, and nothing has to be stored in the save to notice.
inline bool spiceEconomyViable(int sustainableWorkers) { return sustainableWorkers > 0; }

// The smallest city that pays its own bills: the same four 100-credit lots
// that preferRefinery() already trades a 400-credit refinery against.
constexpr int kBootstrapZoneSeed = 4;

// Tax is only income once it can be banked. A house with no storage capacity
// at all drops every credit its city earns on arrival, so the first capacity
// source belongs to the opening and not to optional tech - a developed city
// with zero capacity is the same bankruptcy as no city at all, just with a
// larger gross figure. Queued capacity counts, so one order settles it.
inline bool cityStorageMissing(int capacityIncludingQueued) {
    return capacityIncludingQueued <= 0;
}

// One taxable residential lot does not fund a city opening. Keep the small
// seed and storage ahead of optional technology; queue-inclusive commitments
// reserve their own cost while the real simulation develops the lots.
inline bool cityBootstrapIncomplete(bool citySim, bool spiceViable,
                                    int zonesIncludingQueued,
                                    int capacityIncludingQueued) {
    if (!citySim || spiceViable) return false;
    if (cityStorageMissing(capacityIncludingQueued)) return true;
    return zonesIncludingQueued < kBootstrapZoneSeed;
}

// Cash the opening still needs: the uncommitted lots plus, while nothing can
// be banked, the real price of the cheapest legal capacity source. Committed
// orders have already charged their own price, so only the remainder is
// withheld from optional spending.
inline int cityBootstrapReserve(bool incomplete, int zonesIncludingQueued, int lotCost,
                                int storageCost) {
    if (!incomplete) return 0;
    return std::max(0, kBootstrapZoneSeed - std::max(0, zonesIncludingQueued))
            * std::max(0, lotCost)
        + std::max(0, storageCost);
}

// An economic transport is an economy upgrade only when there is something to
// carry: a working harvesting fleet, or damaged vehicles and a bay to repair
// them in. Mirrors the capital transport lane's own usefulness gate so a port
// import cannot buy what a factory would have refused.
inline bool economicTransportUseful(int spiceRemaining, int workers, int refineries,
                                    int combatVehicles, int repairYards) {
    if (spiceRemaining > 0 && workers > 0 && refineries > 0) return true;
    return combatVehicles > 0 && repairYards > 0;
}

// Accepting an order only puts an item into a builder's queue. Nothing is
// charged at that moment: BuilderBase::updateProductionProgress() deducts
// price/(buildTime*15) per tick from whatever credits the house actually has,
// makes no progress at all while the till is empty, and resumes when income
// arrives (src/structures/BuilderBase.cpp). Foundation slabs are ordinary
// queued orders ahead of the building and are paid the same way; the planner's
// reserves are bookkeeping, not credits already spent.
//
// A growth-cap project therefore never needed its price banked, and it does not
// need its price covered by any short forecast window either. It needs enough
// in hand to enter production and make real progress, and income that keeps
// arriving. A 3000-credit stadium on 400 cash and 300 tax per minute finishes
// in about ten minutes; gating it on a four-minute forecast would reproduce the
// original deadlock, where the yard held back, the cheap-zone fallback spent
// the same credits next pass, and the residential valve stayed capped at 0 for
// a whole match.
struct InstallmentOrder {
    bool order = false;   ///< start now: the production start budget is in hand
    bool reserve = false; ///< short of the start budget only; keep credits for it
    /// Worth holding credits for. False means neither income nor cash can pay
    /// for it at all, so nothing is withheld and the city keeps spending.
    bool funded() const { return order || reserve; }
};

/// An integer planning approximation of what the engine charges per tick once
/// production runs. BuilderBase works in FixPoint and charges price/(buildTime*15)
/// per tick, which for a long build is a fraction of a credit; this rounds that
/// down to whole credits and never below 1, so the planner asks for a credit it
/// can actually hold rather than a fraction it cannot represent.
inline int firstInstallment(int price, int buildTicks) {
    if (price <= 0) return 0;
    return std::max(1, price / std::max(1, buildTicks));
}

/// The budget an order needs in hand to enter production and progress: the
/// foundation orders the planner will queue ahead of it, plus one installment
/// of the building. Deliberately not a fraction of the price - the remainder is
/// what future income is for.
inline int productionStartBudget(int price, int foundationCost, int buildTicks) {
    return std::max(0, foundationCost) + firstInstallment(price, buildTicks);
}

/// `continuingIncome` is bankable net income per planning horizon (tax less
/// upkeep, plus spice receipts): strictly positive means the installments will
/// keep being paid, however long that takes. With no income at all the project
/// still goes ahead if the house can simply pay for it outright, and otherwise
/// nothing is reserved, because a permanently stalled order would occupy the
/// yard and starve the city instead of buying something it can finish.
inline InstallmentOrder installmentOrder(int spendable, int price, int foundationCost,
                                         int buildTicks, int continuingIncome) {
    InstallmentOrder result;
    if (price <= 0) return result;
    if (continuingIncome <= 0 && spendable < price + std::max(0, foundationCost))
        return result;
    result.order = spendable >= productionStartBudget(price, foundationCost, buildTicks);
    result.reserve = !result.order;
    return result;
}

inline int demandedCivic(uint8_t blocked, int stadiumCommitted, bool stadiumAvailable,
                         int airportCommitted, bool airportAvailable) {
    if ((blocked & DuneCity::NeedStadium) && stadiumCommitted == 0 && stadiumAvailable)
        return Structure_Stadium;
    if ((blocked & DuneCity::NeedAirport) && airportCommitted == 0 && airportAvailable)
        return Structure_Airport;
    return NONE_ID;
}
// Loaded workers returning to occupied bays are observed capacity pressure,
// not a theoretical harvesting/travel estimate. Let an ordered bay arrive first.
inline bool unloadingQueueNeedsBay(int waiting, int freeBays, int pendingBays, bool persistent) {
    return persistent && waiting >= std::max(0,freeBays)+2 && pendingBays == 0;
}
inline bool processingCapacityNeeded(int refineries, int committedWorkers,
                                    int workerAnnualIncome, int bayAnnualCapacity) {
    return int64_t(std::max(0,committedWorkers)) * std::max(0,workerAnnualIncome)
        > int64_t(std::max(0,refineries)) * std::max(0,bayAnnualCapacity);
}
inline bool considerRefinery(bool processingNeeded, bool wantedIncludedWorker,
                             bool factoryCanSupply, bool workerRecovery = false) {
    // Military production is temporary. Do not buy a permanent unused bay just
    // because that factory is busy this pass. Recover a collapsed fleet first.
    return processingNeeded || (wantedIncludedWorker && (!factoryCanSupply || workerRecovery));
}
inline bool preferRefinery(const Investment& refinery, const Investment& zone,
                           bool refineryUseful, bool residentialHedge, bool processingNeeded = false) {
    if (!refineryUseful || refinery.cost <= 0 || refinery.proceeds() <= refinery.cost) return false;
    if (processingNeeded) return true; // Release an economically worthwhile unloading bottleneck first.
    if (residentialHedge) return false;
    if (zone.cost <= 0) return true;
    // Return per credit accounts for the four 100-credit plots that can be
    // bought instead of a 400-credit refinery. Ties favour permanent tax income.
    return int64_t(refinery.proceeds()) * zone.cost > int64_t(zone.proceeds()) * refinery.cost;
}
inline int marginalSpiceIncome(int workers, int refineries, bool freeWorker,
                              int workerAnnualIncome, int refineryAnnualCapacity) {
    const int before = std::min(workers*workerAnnualIncome,refineries*refineryAnnualCapacity);
    const int after = std::min((workers+int(freeWorker))*workerAnnualIncome,
                              (refineries+1)*refineryAnnualCapacity);
    return std::max(0,after-before);
}
// Existing/queued workers are common to both yard choices. Credit only bay
// relief plus actual full deliveries from the refinery's one included worker.
// A first load is a receipt, not an extra full-trip delay before steady income.
inline int refineryProceeds(int workers, int refineries, bool freeWorker,
                            int workerIncome, int bayIncome, int buildCycles,
                            int roundTripCycles, int unloadCycles, int load, int upkeep) {
    const int operating = std::max(0,horizonCycles-buildCycles);
    const int relief = marginalSpiceIncome(workers,refineries,false,workerIncome,bayIncome);
    const int addedWorker = marginalSpiceIncome(workers,refineries,freeWorker,workerIncome,bayIncome)-relief;
    const int deliveries = operating/std::max(1,roundTripCycles);
    const int64_t receipts = int64_t(deliveries)*load*addedWorker/std::max(1,workerIncome);
    const int64_t released = int64_t(relief)*std::max(0,operating-unloadCycles)/DuneCity::kCyclesPerCityYear;
    const int64_t bills = int64_t(upkeep)*operating/DuneCity::kCyclesPerCityYear;
    return int(std::max<int64_t>(0,receipts+released-bills));
}
inline int zoneConfidence(int demand, int maximum, int pollution, int crime, int unfinished) {
    if (demand <= 0) return 0;
    const int demandConfidence = std::clamp(demand*1000/std::max(1,maximum),250,1000);
    const int environment = pollution >= DuneCity::kPollutionGrowthBlock ? 0
        : pollution > DuneCity::kPollutionGrowthThreshold ? 500 : 1000;
    return demandConfidence * environment / 1000 * (crime>=192 ? 500 : 1000) / 1000
        / (1+std::max(0,unfinished));
}
}
#endif
