# QuantBot money planning — legacy audit and conflicts

**Status: historical source audit.** The unpromoted candidate planner and its
experimental power/first-factory policies have been removed. The source-pinned
inventory and measurements below describe the audited revisions, not a promise
that their line numbers or gates match every later revision.

## 1. Scope, provenance and payment rules

Stefan's two premises are correct for ordinary production: an order can start
before its whole price is available, and separate savings rules now make
incompatible decisions about the same treasury. That does **not** establish that
every symptom in a save is a cash gate. Technology, demand caps, supplier legality,
placement, unit composition and a busy yard can independently stop the order.

The audit covers all `src/players/QuantBot*.cpp`, the associated BuildPass and
configuration definitions, financial helpers in `include/players/*Policy*.h`,
`VanillaEconomy.h`, and the shared Player/engine calls those paths use. Spatial
reservations and tactical home-army reserves are not cash reserves; cash-sensitive
army/campaign decisions are listed separately rather than silently omitted.
All 21 producer functions and all 25 yard functions plus their prerequisite helper
were examined, together with the main build-pass planning stages and other
QuantBot files' monetary references.

Initial source HEAD was `82b22e32c6985568ee9152d637b4dbedeef2fdab`.
The shared checkout also contained in-flight ResearchLabAI work, and diagnostic
commits landed during the audit. Therefore source line references describe the
**read snapshots**, not a claim that all inspected bytes belong to that commit.
Measured artifacts below retain their own immutable source revisions. In
particular, the observed research-installment engine changes are not relabelled
as shipped behavior.

### Engine constraints: not all full-price checks are removable AI gates

| Operation | Real payment/admission rule | Consequence for a unified AI planner |
| --- | --- | --- |
| Ordinary unit, building or concrete production | Queue acceptance does not debit the full quote. Normal ticks pay `price/(buildTime*15) * min(health/maxHealth, buildSpeedLimit)`; city timing comes from `getCityBuildTime`. Hold, unit limit or zero displayed credits can stop progress. Instant build requests the remainder. `BuilderBase.cpp:425-456,1061-1087`. | Model the sequential queue's drain; do not require or subtract its entire unpaid remainder from cash. Foundations are progressive production too. |
| Builder upgrade | Payment is progressive over 600 ticks. Under the shared global `Technology Installments=true` rule, `BuilderBase::canStartUpgrade()` accepts without full cash and progress pauses when the next payment is unaffordable. With the rule OFF, acceptance still needs the full quote; this is how legacy 9882/9883 saves load. | Never add an AI full-price gate. Respect the selected global human-and-AI acceptance rule; the AI gene does not retrofit legacy saves or grant a private exemption. |
| Starport cargo | Paid on ordering; pending cargo must count toward fleet demand without charging it again (`QuantBotBuild.cpp:399-403`). | Its actual current build-list quote is a real immediate debit, not an installment. Stock and delivery remain real constraints. |
| Research, legacy/prepaid mode | The whole next-level quote is required and paid at acceptance. Already-paid active research has no remaining cash drain. | Keep a real one-shot cost for a new prepaid study; never reserve an active prepaid timer's original price again. |
| Research, global installment mode | Captures total cost/duration/mode; cumulative due is `floor(totalRaw*fundedTicks/duration)`. A partial next installment is retained; time advances only after it is funded. Pauses do not accumulate catch-up debt. `payCreditsExact` uses fractional balances. | Use captured unpaid cost and funded progress, not the next quote or full-price AI encumbrance. ResearchLabAI owns the shared `Technology Installments` rule for research AND upgrades: save 9884 / MOD19 / protocol113; current new-game default OFF, legacy 9882/83 OFF, explicit ON applies equally to humans and AI, no retroactive refund. |
| Repairs, upkeep, tools and conversions | Repairs/upkeep have their own tick/cadence gates; Scoutpost conversions and imports are real immediate charges. The legacy `CMD_CITY_TOOL/CityTool_Road` overlay in `CitySimulation.cpp:414-429` makes no credit withdrawal; queued Road structures are ordinary production. `House::payCredits` requires >=1 rounded displayed credit; structure repair progress requires >=5. | Include actual recurring drains and true immediate charges. Do not charge free overlay tools or reserve a whole future repair bill. Preserve real shared-engine tick gates. |

Research's current Normal quote is `1000*(target-1)` credits and `target` minutes;
pace scales price and duration by 1/2, 1, 2 or 4 (`TechResearch.h:16-42`). The old
research formula in `quantbot-spending.md` is historical, not the formula used here.

### Notation and defaults

In the inventory, `B`, `P`, `Y`, `A`, `C`, `L` mean respectively
`QuantBotBuild.cpp`, `QuantBotBuildProducers.cpp`, `QuantBotBuildYard.cpp`,
`QuantBotArmy.cpp`, `QuantBotCampaign.cpp`, `QuantBotPlacement.cpp`.
`H`, `S`, `E`, `W` mean `QuantBotBuildPolicy.h`, `QuantBotSpendingPolicy.h`,
`CityEconomyInvestmentPolicy.h`, `QuantBotPowerInvestmentPolicy.h`.
`M` is the current pass/turn's virtual spendable cash, `CASH` the actual house
balance, `Q(x)` its builder's quote, `D(x)` its object-data price, `K(x)` the
estimated `buildingCapitalCost`, and `F` the actual site-specific slab quote.
`K(x)=D(x)+whole-footprint Slab1 estimate+amortized generation capital`; it is
**not** the actual queue's price or its first drain.

Constructor and shipped-default key values read in `QuantBotConfig.cpp:333-380`
and `config/QuantBot Config.ini.default`:

- **ON:** ResearchLab, ResearchLabProgression, CityGrowthPower, CityPowerSizing,
  CityOpeningCapital, CityConstructionCapital, CityProductionPrerequisites,
  CityStarportSupply, StorageCapacity, FundedClassicProduction, TrooperSupport,
  ThreatTurrets, PerimeterTurrets, StrategicSiteGuards, PoliceFundingRecovery,
  ExhaustedArmyDispatch.
- **OFF:** SimpleTechSavings, ExistingCompositionUnitChoice, CapacityMcvRunway,
  CityFirstFactorySaving, MoreRocketTurrets, SpendAtCap, OpeningCombatBudget,
  CampaignCombatBudget, Civ Wars Strategy. New candidates StartWhatMakesSense,
  CityCompactPower, CityCompactPowerInstallments and ContinuousResearch are
  independently default OFF.
- `MinMoneyForProduction=500` is configured/hashed/registered but no runtime
  production consumer was found; hard-coded thresholds below do not use it.
- **Snapshot versus current cutover:** ContinuousResearch was config-only with a
  temporary true constructor value in the initial read snapshot. ResearchLabAI
  subsequently wired its runtime behavior and corrected its default to OFF.
  The inventory below describes the original gates, not a claim that this
  temporary declaration was effective continuous research.
- Effective mod INIs and saved game options can override constructor fallback
  values. Registry defaults are not proof of a loaded save's settings. For
  example, constructor Medium/Hard campaign military multipliers 2.0/2.5 differ
  from registry 2.5/3.0, and Brutal harvester multiplier/refinery minimum 3/2
  differ from registry 999/4; Tornie INIs override them.

Evidence codes in the tables: **S1** exact initial `notbuilding` save audit,
**S2** historical research/crime save, **S3** restored-revision `nonuke` audit,
**T** rejected tech-saving cohorts, **U** unit-choice cash/throughput cohorts,
**HIST** earlier isolated ablations.
A dash means **source-proven rule, binding not measured in the cited saves**,
not that the gate never binds. A measured treasury or model projection is not
proof that an individual order was sustainable or that a gate caused its delay.

## 2. Inventory of monetary gates and reservations

### 2.1 Shared accounting, reserves and capital funding

| ID / source | Check or reservation; price versus drain | Control / default; affected purchases | Binding evidence |
| --- | --- | --- | --- |
| A1 — B:380-407,623-630 | Sum full unpaid upgrade and non-Starport queue remainders, then `M=max(0,CASH-commitments)`. Simple subtracts queued technology debt from that encumbrance. Existing cargo is correctly excluded. | Always; SimpleTechSavings OFF changes its exception. Every later purchase sees this virtual cash. | S1: 260->108 with152 debt; 519->415 with104; 498->432 with66. |
| A2 — B:629-630 | Non-city funded-economy reserve `max(2000,refinery+2*harvester)`; zero in city mode. Full replacement capital, not operating drain. | Vanilla economy, extended to powered Custom Brutal by FundedClassicProduction ON. Competing non-suppliers and capacity. | U/HIST measure the affected paths; no isolated reserve ablation. |
| A3 — B:886-889 | `cityWorkingReserve=tankPrice + missing-worker?harvesterPrice + non-city missing-bays?refineryPrice`. | Always computed, conditional components. Research, MCVs, infantry, WOR and other capital callers. | S1:300 in all four houses. |
| A4 — B:713-800 | After feasible eligibility timers, protect full next IX or Palace price; stable power and legal site required. IX waits75/50/35/20s; Palace120/90/60/45s for Easy/Medium/Hard/Brutal. | Built-in Custom non-Builder strategic policy; Palace limits retained. Competing factories/imports/capital. | — |
| A5 — E:73-108; B:2461-2627 | No-spice seed/storage: `(4-queuedOrOwnedZones)*lotCapital + missingLegalStorageCapital`; lot capital includes roads and estimated power. Releases as real commitments are made; only protects demand/initial seed/storage. | Built-in city bootstrap. Optional technology, ports, repairs, coverage and unit purchases. | T shows aggregate progression failure, not isolated A5 causality. |
| A6 — B:2476-2500 | Missing civic protects estimated foundation-plus-first-installment; Simple yard unlock can protect full upgrade. A queued civic stops adding this reserve, but A1 then encumbers its entire remainder. | Built-in demanded civics; SimpleTechSavings OFF affects upgrade case. Other producers. | S1 civic caps observed; no recurrent payment trace yet. |
| A7 — B:936-951 | Human-allied campaign helper saves every missing worker at full catalog price; cheaper market offer can raise target to positive engine harvester cap. Queued workers release it. | Campaign/allied/custom-role conditions, not a gene. Competing optional producers/imports. | HIST documents helper exception; not measured in S1. |
| A8 — P:729-780; BuildPass.h:371-404 | Scoped builder reserve is mostly `max(strategic,economy,civic,MCV,worker,carryall,nuclear,helper,capital,bootstrap,research)` with supplier/yard/recovery exemptions. Temporarily subtract then restore at turn end. Nuclear alone can protect full2000 before it is ordered. | Built-in plus relevant demand genes. Every non-owning producer. Storage recovery can clear the yard's hold. | — |
| A9 — B:4260-4277; S:76-78 | Protect selected capital's full `price+foundation` from other builders; zero for its supplier/emergency/consumed order. First WOR/recruitment adds `cityWorCashReserve`; Simple selected structure returns0. | Shared capital policy, TrooperSupport ON / Simple OFF exceptions. Other queues and bargains. | S1 Neutral selects protected police rather than an affordable C zone. |
| A10 — B:2050-2086 | Production cash subtracts `max(strategic,economy)+cityWorkingReserve`. Funded army target is `min(limit,committed+max(0,productionCash))`. WOR cash reserve is `max(strategic,economy)+cityWorkingReserve+one residential lot` in city mode; Palace exception for troop support. | Built-in, TrooperSupport ON, composition genes OFF. Unit shares, WOR and troop funding. | U shows idle/choice interactions; no cash-debit double counting proven by that alone. |
| A11 — B:2261-2379; S:11-34 | Four-minute net income: capped actual-fleet spice +4*(tax-upkeep). `forecastFunding=max(0,M+netIncome)`. `projectedCash=M+netIncome-continuedMilitary-continuedConstruction`; each line uses `max(0,fourMinuteCapacity-existingUnpaid)` for continued cost. Both present and projected cash must cover cashBuffer to fund parallel production. Buffer=max(1000,cityWorking or economyReserve). | Built-in. Shared-vs-parallel branch, all capacity scoring and optional capital. Simple forces shared branch. | U directly measures fundsParallel and next-HF tests. This formula intentionally avoids subtracting the same old queue twice. |
| A12 — B:178-220; H:242-250 | `cash-cost>=reserve` **and** `projectedCash-orderedCost-cost>=reserve`; reserve=`max(1000,cashBuffer,economy,cityWorking)+max(strategic,civic,bootstrap,helper,capital)`. Civ Wars reduces to current cash, no orderedCost and max(300,cityWorking). Full cost plus cash stock. | CityOpeningCapital ON protects optional tax-city MCVs and producer upgrades; optional research uses it too. Spice/recovery/emergency exemptions. | S2 for old readiness layer; S1 full-price research funded flags false. |
| A13 — B:160-175,200-213 | Capital readiness requires viable delivered spice or bankable tax, positive net forecast, bootstrap complete, power/committed load, no economy/air attack, and fleet/crime conditions unless required research/Civ Wars exempts them. Required research still needs full cost +max(300,cityWorking) in current M. | ResearchLab ON; ResearchLabProgression ON changes exemptions, not payment math. Lab/research. | S2 directly proves the former crime veto despite28k+ cash. S1 all four are economy/investment ready. |
| A14 — B:240-283; H:263-271 | `planTechResearch` returns unless required research or Civ Wars strategy, then optional wave/HF/repair pacing. Missing lab/failed study saves `cost+max(300,working)` only if entire amount fits four-minute forecast. **The end-of-build fallback separately calls `startTechResearch` for ordinary non-Civ-Wars, non-required research**; its full funding/readiness gates still apply. | ResearchLab/ResearchLabProgression ON; Civ Wars Strategy OFF. ContinuousResearch was not wired in the original snapshot; it is now an independent default-OFF want policy. | S1 all four full-price research funding flags false; H6 is 79 credits below its 2300 threshold. H5/H7 required=0 alone does NOT prove that no research attempt occurs. |
| A15 — B:2630-2678 | First HF opening requires **whole missing prerequisite chain +buffer** in current cash, plus four-minute new production in projection. Parallel city bootstrap adds seed commitment. `nextHeavyRunway=K(HF)+max(firstFactoryProduction,laneCapacity[HF])+buffer`; city production requires M and projection >= it. | Built-in opening/parallel policy; relevant city branches. HF/prerequisites, MCV opening exemptions, expansion. | U: strict next-HF-funded/all-HF-queued only3.16% common time ON. |
| A16 — B:3730-3765,3952-3961 | CityConstructionCapital only makes tax-city first HF a protected candidate once price+actual foundation is affordable. Below-quote saving needs >1yard and `forecastNetIncome>=quote+queuedProductionCost`. | CityConstructionCapital ON; CityFirstFactorySaving OFF; Hard/Brutal only, port/seed/power/no-attack conditions. First HF, not LF. | —; S1's no-LF case must not be attributed to this HF-only rule. |
| A17 — B:3724-3728,2378-2379; S:59-75 | Additional production=`max(0,min(addedCapacity,armyShortfall-existingCapacity,funding-existingCapacity-capital))`; includes maximum eligible line rates even for idle/held factories. Extra capacity can score0 before dispatch. ExistingComposition uses harmonic mix throughput and income-share factory demand instead. | Shared policy; ExistingCompositionUnitChoice OFF selects alternative capacity path. Repeated LF/HF/HTF. | S1 Mercenary LF reason income_or_army_bottleneck; U has missing candidate telemetry in funded-parallel classic branch, not zero demand. |
| A18 — B:2454-2458; H:356-374 | Optional city MCV needs quote+max(1000,working). CapacityMcvRunway adds `projectedCash-capacityMcvOrderedCost>=quote+cashBuffer`; CityOpeningCapital independently applies even if runway gene is off. Candidate forecast also needs quote+max(1000,working). | CityOpeningCapital ON, CapacityMcvRunway OFF; lost-yard/space/colonisation exemptions. MCVs. | — |
| A19 — B:2876-2930; H:278-285 | Storage needs positive net income, earned-cash pressure after actual modeled burn/accepted costs, no pending vault, safe legal site and **full estimated K(storage)<=M**; choose best cost/capacity. OFF legacy requires80% earned capacity and completed core. | StorageCapacity ON; Simple also selects new path. Vault/refinery/Silo. | — |
| A20 — B:3058-3332,3984-4185 | SimpleTechSavings allows only one chosen first-tech/producer-upgrade project outside explicit emergencies. Income/power/bootstrap/crime/fleet/transport stability activates it; other capital, civics, income suppliers and city seed can override it. `spendingAdmits` is chiefly an ownership veto, not a numeric drain test. | SimpleTechSavings OFF. First HF/repair/HTF/IX/Palace/lab, producer upgrades, research selection; it also changes composition. | T: severe passivity measured; causality is combined policy, not an isolated single predicate. |

### 2.2 Common acceptance, producers and imports

These are **additional** checks after the shared planning layers above. Unless a
row names a gene, it is a built-in legacy rule; a gene that changes the candidate
does not remove the common full-quote gate.

| ID / source | Monetary condition and price basis | Control/default and blocked items | Evidence |
| --- | --- | --- | --- |
| P1 — P:148-217,246-273 | Ordinary acceptance requires `M>=actual Q(item)`; exact installment project uses its start budget; blackout generators/marked slabs and Simple project have exceptions. Accepted ordinary/civic orders deduct full quote from M and add it to capitalOrderedCost; Simple project/slabs do not deduct M. | Built-in; Simple OFF. All unit/building/slab orders using wrapper. MCV adds A18. | S1/T show relevant virtual accounting; no causal isolation. |
| P2 — P:192-198 | City infantry needs quote+cityWorkingReserve; city WOR needs quote+cityWorCashReserve, troop content and no opening worker need. Standing troop reserve also preserves army-cap slots, not a second actual cash debit. | TrooperSupport ON; infantry quotas/composition remain separate. | — |
| P3 — P:348-392 | Upgrade requires full M>=cost, working-capital admission and spendingAdmits, then engine's full-CASH acceptance. | Built-in; CityOpeningCapital ON / Simple OFF modify extra layers. All builder upgrades. | — |
| P4 — P:398-444 | Founded package requires complete legal foundation and **building quote +sum actual slab quotes** in M, except exact Simple project; each constituent again uses P1. Failed partial package cancellation does not reverse the pass's prior quote bookings in these callers. | Built-in recovery/campaign/research path; Simple OFF exception. | —; same-pass understated cash after rollback is [INFERENCE], not measured. |
| P5 — P:26-64; H:483-522; Player.cpp:318-325 | Scoutpost conversion needs its quote<=M and books actual debit. Repair start needs actual credits>=5 plus maintained-output/free/city repair, or health<40% with M>1000, engaged emplacement, or M>5000. Direct damage/campaign/upgrade repair calls can bypass this AI threshold, not engine repair gate. | Built-in repair policy. Conversions and discretionary repairs. | — |
| P6 — P:313-345 | Missing yard+MCV with a listed market MCV makes all builder turns return, even if temporarily sold out. A ready stocked port temporarily restores encumbered raw cash and attempts full-quote MCV. | Built-in construction recovery. Every other producer while recovery awaits stock/funds. | — |
| P7 — P:447-525,688-726 | Bulk Slab4 unlock holds yard for full upgrade price; serial yard-upgrade guard. Campaign wind prechecks full D(wind) then P4; campaign repair/prerequisite needs D(step)+300 then P4. | Built-in; campaign restrictions remain. Concrete unlock, generation, repair chain. | — |
| P8 — P:542-685; W:128-203 | Non-city power prices **entire cheapest generator package** after economy reserve; positive four-minute income must replace capital in30s (net4m>=8*package), and avoided repairs repay in10m or spare cash>5000. Retry smaller operating shortage with same gates. | FundedClassicProduction ON extends vanilla path; other non-city vanilla cases built-in. Generators. | — |
| P9 — P:783-900 | Capital-unit selection requires positive score and candidate full price<=M. Some Tornie unlock/variant paths require M>1500 before actual variant quote. Selected transport supplier can hold. | Built-in; Simple OFF adds admission. Economic units/variant capability. | — |
| P10 — P:902-969 | Light military starts only M>500 city / >700 non-city, or >0 Simple emergency. Upgrade path additionally raw credits>1500; actual order then P1/P3. Missing-advanced fallback filters full prices. | Built-in; Simple OFF exception. Light units/upgrades. | — |
| P11 — P:972-1105 | WOR needs M>450, barracks>300, or >0 Simple emergency. Troop-support selector filters actual quote against M after city reserves; variant/quota filters additional. | TrooperSupport ON, Simple OFF. Infantry. | — |
| P12 — P:1107-1204; H:330-353 | First carryall holds line unless its full quote fits; aircraft needs full quote+army room/mix. Ordinary upgrade selection M>500. Air-strategy unlock uses raw credits>500 and temporarily restores reserve, but P3 still applies. | Built-in Air strategy / Simple OFF admission. Carryalls, aircraft and HTF upgrades. | — |
| P13 — P:1206-1388 | Expansion HF returns below available MCV quote/unlock. City/vanilla MCV leaves max(1000,working)/1000 except recovery; MCV upgrade has same add-on. Harvester requires price plus0/1000/tank price by opening/campaign/economy state; temporary reserve restoration still protects selected capital. Other upgrades M>10000 or >500. Classic capacity uses yard+MCV<min(8,M/4000); low-cash worker recovery M<2000 or campaign. | Built-in and FundedClassicProduction ON routing; A18/P3 still apply. Heavy economy, MCVs and upgrades. | U records upgrade/order exposure, not upgrade payment attribution. |
| P14 — P:1389-1470; H:113-166 | Heavy military entry M>500 city/vanilla, >2000 otherwise, or >0 Simple. Unit/deficit/fill choice filters full price<=M and army headroom. Existing choice can idle on its selected slot without affordable substitute. | Simple / ExistingComposition OFF; otherwise built-in. Heavy military. | U: both affordability and composition-ready idleness measured, not identical mechanisms. |
| P15 — P:1474-1554 | Port economic cash=`M+turnReserved-max(capitalReserve,bootstrap)`; actual worker/carryall quote must fit now. Worker four-minute marginal receipts must exceed price unless explicit helper/cheap-human-ally exception. Later carryalls wait for helper-worker reserve; useful-work/stock remain. | Built-in plus existing economic genes. Imported workers/transports/MCV capital. | HIST/T contain routes, not isolated funding ablation. |
| P16 — P:1556-1629 | Bargain quote must be positive and strictly below nominal value, then fit `M+turnReserved-max(helper,capital,bootstrap)` and military cap. Existing composition can stop on unavailable/unaffordable selected family. | ExistingComposition OFF; discount rule built-in. Military imports. | T/U label accepted quotes separately from paid cash. |
| P17 — Player.cpp:265-315; Y:2258-2266 | Tornie worker/building alternatives require variant object-data price<=M before substitution; wrapper later checks actual Q. | Content-conditioned built-in variant selection. | — |
| P18 — Y:2517-2541; B:4367-4382,4386-4565 | Idle road repair queues directly up to min(8,(M-economyReserve)/roadPrice), books full data price, bypasses P1. `manageCityBuilding` queues up to8 direct road-tool commands without this pass ledger. Non-city idle concrete expansion requires M>500 then P1. | Built-in direct/idle work. Roads/slabs. | —; direct tools are a separate spend route, not proof they are free. |

### 2.3 Yard selection, vetting and final founded-order gates

| ID / source | Monetary condition, including preselection vetoes | Control/default; affected construction | Evidence |
| --- | --- | --- | --- |
| Y1 — Y:49-101 | Planning entry requires actual credits>=100, idle/not upgrading/nonempty list, except priority Simple project. Exactly100 passes despite telemetry saying <=100 is insufficient. | Built-in; Simple OFF exception. All new yard choices. | S1 Ordos yard initially busy with wind. |
| Y2 — Y:104-137; H:634-668 | Extra ground lane requires current lane, no pending duplicate, >=75% busy, deficit>=factory price and M>=reserve+factory+1000. Air adds capable line, air room and aircraft price. Targets also depend on wealth/income/losses; city HF target=max(1+creditsPerSecond/50,1+max(0,credits-2000)/2500), non-city1+credits/4000, clamped1..24. >=8000 plus loss/busy can raise it. | Built-in capacity rules; composition gene OFF changes demand path. Extra factories. | U; some funded classic plans omit unit/candidate detail, which is unknown not zero. |
| Y3 — Y:192-298 | Campaign rebuild uses P4; yard upgrade M>1500; optional rocket M>3000. A city-zone M>200 branch appears unreachable through this non-city campaign caller [INFERENCE]. | Built-in campaign. Rebuilds, upgrades, turrets. | — |
| Y4 — Y:305-423; B:2876-2930 | Storage uses A19. Selected city-growth generator holds unless full candidate price+foundation fits; dedicated growth requires full affordableCityZone. | StorageCapacity ON, CityGrowthPower ON. Vaults, power, R/I/C. | — |
| Y5 — Y:547-601,889-961,2075-2123; H:581-594 | Deficit reactor has full D(nuclear) prefilter; otherwise wind. Planned reactor uses exact installment exception. City sizing requires M>=nuclear+max(1000,working,bootstrap) and shortage OR M>=5*nuclear+reserve OR insufficient wind sites OR wind package>=nuclear. Exact installment project bypasses final sizing veto. Main planning call uses hypothetical enough cash for sizing only; other paths use actual M. | CityPowerSizing ON; CityGrowthPower ON. Wind versus nuclear, not tech legality. | S1 T2 cannot legally order tech6 nuclear. S3 T9 can, but all four balances are below2000; H4 books a100 zoning quote against virtual M before generator selection sees M83. |
| Y6 — Y:603-740 | Bootstrap storage/seed require full estimated capital, seed also leaves missing-storage price. Demanded civic upgrade can save if current/forecast covers full quote, starts only if current M fits. Civic structure uses E.installmentOrder: positive net income plus `M>=estimatedFoundation+max(1,price/buildTicks)`; with no positive income full price+foundation required. | Built-in demanded civic path; Simple OFF affects admission. Stadium, airport and chain. | S1 civic caps observed, not completion evidence. |
| Y7 — Y:741-819 | Selected protected service needs full price+foundation; early rocket also leaves a residential lot. Defence/expansion upgrade holds for full quote. One-yard opening seed requires full capital and24power; otherwise fully estimated wind; profitable opening refinery holds for full capital. | Relevant capital/service genes ON. Services, unlocks and first economy. | S1 Neutral's selected urgent police outranks valid affordable C. |
| Y8 — Y:823-888,963-1088 | Core/supplier step considered only if M or forecast covers estimated cost; only current cash starts. Heavy-recovery/first-factory/first-port prerequisite walk needs full data price unless exact installment project. Port ladder also has refinery/repair sequence and viable-spice/market requirements. | Built-in; CityProductionPrerequisites ON corrects footprint ownership, not this affordability. | — |
| Y9 — Y:1091-1245 | First city refinery, seed/refinery choice, vehicle bootstrap, unloading bay and first transport HTF need full data price. **City LF bootstrap waits at400** in the saved mod; coverage rocket leaves one plot unless current threat. Worker/transport need can hold extra HF. | Built-in, narrow exact-installment exceptions. First LF and opening suppliers. | S1 Ordos M108<LF400 and positive net918/min; recurrent diversion not yet measured. |
| Y10 — Y:1247-1382; L:1915-1986 | Crime evaluator can inspect with a price floor, then erases unaffordable service; dangerous crime can keep saving if projected cash covers it. Land-value service gets max(0,M-working). Early repair price+1000; light-first M>500; extra non-city refinery may be chosen specifically below2000, still subject P1. | Built-in service/repair rules. Police/turrets, repair, LF and refinery. | S1 service priority measured; not cash starvation for its valid C zone. |
| Y11 — Y:1384-1577 | City core considered income-ready at>=3zones or M>3000. First HF waits full price; rich vanilla prioritization M>=20000 and bounded factory/yard ratio. City defence upgrade leaves upgrade+lot; save depends on forecast/proactiveSavingHold. Funded repair K+buffer. Campaign first port M>1000; Radar/LF M>500. | Built-in and relevant city/defence genes. Factories, repair, ports, radar, unlocks. | — |
| Y12 — Y:1580-1745 | Barracks M>400; WOR>600 and city WOR quote+WOR reserve. First city repair leaves one lot. City turret unlock needs upgrade+lot and M>500; vanilla unlock M>economyReserve+rocket. Baseline/counter-air rocket full price; perimeter rocket economyReserve+price+500. First HF>500; first HTF>1000. | PerimeterTurrets ON controls old `kForceThreatTurrets` branch (not ThreatTurrets); TrooperSupport ON. | — |
| Y13 — Y:1748-1905 | Lab needs full estimated K under canFundTechInvestment; if power missing, lab+cheapest generator is priced too. IX/port prerequisite M>1000; repeated HF/repair M>max(2000,economyReserve+price). starportUnlocks itself requires full port price and site. | ResearchLab/ResearchLabProgression ON, StrategicSiteGuards ON; match research required. | S2 old lab readiness veto; S1 full funding flags. |
| Y14 — Y:1908-2073 | Palace and prerequisite port M>5000; ordinary defence M>max(1000,economyReserve+1000); front battery economyReserve+rocket. Ordinary zone selection M>200, existing wind and24headroom; otherwise power selection. | Built-in plus strategic/defence genes. Palace, turrets, R/I/C. | — |
| Y15 — Y:2075-2255 | Vet erases extra factory without positive shared candidate unless funded parallel city path, composition permission, etc. Erases city economy/ordinary vanilla-city items below data price unless exact installment or blackout generator. Nuclear sizing rechecked; zones need24power. | Built-in; Simple/ExistingComposition OFF, CityPowerSizing ON. Already-selected items can disappear before P1. | Source chain explains multiple independent veto opportunities; no individual recurrent cause asserted. |
| Y16 — Y:2258-2487 | idle_city_growth can replace a dear/unplaceable/admission-vetoed choice **even if skipRemainingStructureLogic was set**, unless named service/protection/installment hold. Actual full foundation required. With slabs or city WOR, floor is full building+actual slabs (+WORreserve), or slabs+one installment for exact project; blackout bypass. Then each item uses P1. | Built-in fallbacks and named exceptions. All founded choices and zoning replacement. | S1 measured four recurrences: Windtrap300 rejected at239/248/227/178; Commercial100 substituted, LF never ordered before CY loss. |
| Y17 — Y:2545-2616; L:2684-2815 | Before clearing a zone for an already-completed turret foundation, turret must remain queued, clearanceCommitmentShortfall<=0 and M>=economyReserve. It does not charge turret price again. | Built-in battery clearance safety. Redevelopment placement, not a new order. | — |

### 2.4 Economic-return, recurring-service and peripheral fiscal checks

| ID / source | Check | Control/default; blocked behavior | Evidence |
| --- | --- | --- | --- |
| X1 — B:1444-1609; E:12-25,38-65,190-242 | Worker/refinery marginal four-minute spice proceeds must exceed full capital; refinery competes by proceeds/capital versus confidence-discounted zone return. Existing unfinished zones, crime, pollution, delay and upkeep reduce confidence. Worker-vs-army preference uses workers*price*(1 Brutal opening, otherwise2), bounded opening fleet and targets. | Built-in economic selection, not just liquidity. Harvesters/refineries/zone choice. | S1 C valid while some valves0; no isolated ROI ablation. |
| X2 — B:2949-2955 | Proactive saving hold unless growth turn (`zones<3+2*services`) and projected cash can fund service price+one plot. | Built-in interleave. Quiet coverage versus zoning. | — |
| X3 — B:3506-3512,3539-3564 | First city port requires healthy tax/seed/catalog; forecast must cover K unless TrooperSupport uses positive income. Early tax battery requires forecast covering all remaining yard upgrades+rocket+concrete+lot, and no unpriced prerequisites. | CityStarportSupply ON, MoreRocketTurrets OFF, TrooperSupport ON. Supplier/battery wants. | HIST supplier interaction; no exact S1 port-money attribution. |
| X4 — B:656-688; CityServiceInvestmentPolicy.h:45-72 | Police budget uses50% of tax-after-power if cash>=5000 or tax-power-nominalPolice>=500, otherwise33%; funding=clamped affordable allowance/nominal cost. Cuts immediate; increases wait1875cycles; <5point increases wait4x when recovery enabled. Pending service expenses count. | PoliceFundingRecovery ON; operating funding, not a construction reserve. | S1 tax/police quantities measured; no isolated budget-policy cause. |
| X5 — L:1915-1977,2159-2224; CityServiceInvestmentPolicy.h:8-19,76-105 | Passed-budget affordability and power; station blocked with existing station and funding<100; new full-strength expense must fit policing allowance except bounded danger/no-turret emergency. Benefit must exceed build+upkeep+power+overlap unless danger relief>=32/preoutbreak>=64; land-value-only turret needs positive neighbourhood tax and tax+growth>cost. | Built-in service economics. Police/turret selection. | S1 dangerous police chosen; full causal service ledger unmeasured. |
| X6 — QuantBot.cpp:249-278; H:58-59,79-172 | Military-value ceiling; autonomous Medium can add cash above10000 up to50% cap. SpendAtCap OFF permits relevant heavy surplus up to100% if enabled. Explicit Brutal unit override uses rolling headroom>=max(configuredTarget,cash,largestUnit), not an extra fixed unit ceiling. Nominal unit value and actual discount differ. Free patrols also respect military cap. | Difficulty/options; SpendAtCap OFF. All military demands, including non-purchased patrols. | U/T outcomes retain direct cap fields; not a cash accounting debit. |
| X7 — A:430-535,568-570; SimpleArmyPolicy.h:22-28 | Exhaustion checks actual fractional cash below eligible unit prices and no remaining income/cargo/finished relevant queue. Unfundable unit repair released if cash<0.5+(ceil(missingHP)-1)*0.1. Reachable attack readiness can fall to0 if missing army value>cash+remaining spice. | ExhaustedArmyDispatch ON; dispatch/repair availability, not production reserve. Human/defence/escort authority exempt. | — |
| X8 — A:3836-3844 | <4refinery bays, cash<1000 and >=half cargo triggers early worker return. | Built-in liquidity response; worker timing only. | — |
| X9 — C:55-58,98-115,161-210; QuantBotCityCampaignPolicy.h:33-145 | Campaign-enemy discretionary city zoning stops at difficulty/level queued-inclusive zone cap or forecast net income>=450*original worker allowance. Forecast includes current/half-developing tax, police/power cost and300*max(workerTarget,committedWorkers) while spice exists. | Built-in DuneCity campaign permission, original baseline and game options; not a private money allowance. Zoning. | —; crediting target workers before delivery is [INFERENCE] optimism, not measured receipts. |
| X10 — QuantBot.cpp:217-244,605-635; C:98-115 | Map spice /2000 or city per-house /2250, worker/refinery throughput, engine limits and campaign baselines bound economic demand. Opening/CampaignCombatBudget may cap workers4 and postpone optional capacity until first wave. | Both combat-budget genes OFF; remaining caps built-in/difficulty. Workers/optional capacity, not upfront affordability. | — |
| X11 — QuantBotColonisation.cpp:224-248 | Civ Wars strategic colonisation requires spice+workers+bays or bankable tax>police+power, plus army sufficiency. | Civ Wars Strategy OFF; colonisation demand, actual MCV still funded elsewhere. | — |
| X12 — H:600-612; QuantBotConfig | recoveryPoliceFunding helper exists but has only tests/no production callers. MinMoneyForProduction has no runtime consumer. ContinuousResearch was config-only in the initial snapshot, but ResearchLabAI has since wired its independent default-OFF runtime path. | Do not treat the first two as active production gates. Distinguish the initial snapshot from the later ContinuousResearch implementation. | Source search, not a gameplay test. |

## 3. What the existing measurements establish

### S1 — exact initial `notbuilding` state: several distinct constraints

Artifact: `/Users/stefan/Documents/projects/outputs/city-zoning-20261106/exact-audit/`;
`audit-measurements.json` and `summary.txt`, source
`1e56d1bef80c7d93516b0024c844812420a11570`. Save SHA256:
`277f2e520c3ea3099f0c7948c8e581f1f919270a4f45c7212d07fddced797c49`.
Loaded cycle30088, save9883; exact Workshop revision
`27a8c8d92876c5001b7744c7252eca5971327edbc17cde5d5249e1d6351fb0f4`
was verified across906 files /34,791,606bytes (separate pinned-revision receipt).

| House | Tech / LF count | Actual cash -> virtual M / queued debt | Forecast tax-upkeep per minute | R/C/I | Raw power produced/required |
| --- | --- | --- | --- | --- | --- |
| Ordos2 | T2 /0 |260->108 /152|1558-640=918|45/32/22|2100/1524|
| Mercenary5 | T2 /2 |519->415 /104|1883-556=1327|45/34/22|2500/1648|
| Neutral6 | T2 /1 |2221->2221 /0|1558-542=1016|45/32/22|2300/1542|
| Rebels7 | T2 /1 |498->432 /66|1705-552=1153|45/28/22|2400/1623|

All four have bankable storage, positive forecast income, no opening fleet or
bootstrap deficit, and economy/investment readiness true. Working reserve=300;
ordinary cash buffer=1000. Research funding evaluates false for all four. For
Neutral, required next T3 costs2000, so cash2221 is79 below the required2300
cash-plus-working threshold. This is an arithmetic explanation of that observed
predicate, not proof that removing it alone completes all research. Mercenary
and Rebels have `research_required=0`, but the end-of-build fallback still tries
ordinary research; that bit alone is not a missing-request diagnosis. Ordos
already has an active target3 timer, not an absent-research-start case.

The completed 15-minute continuation is pinned to
`df06ededc5f8080dca98a1b937f475a8d048eefb`:
`outputs/not-building-lf-20261106/baseline-causal-sequence.json`,
`baseline-fixed/*-measurements.json` and its gzipped raw decision events.
The original CSV finalizer raised a reporter-only `KeyError: read`; both native
games returned0. Read-only finalization at
`a558be7803fbae0a774991abd655f412c8e7e055` records2/2 complete/PASS without
replaying or rewriting the native measurements.

The first binding cycle is a **power-to-cheap-filler loop**, not an LF400 veto:

| Seconds after load | Cash | Selected useful power quote / need | Executed replacement |
| --- | --- | --- | --- |
|7.12|239|Windtrap300 /70|Commercial100, `idle_city_growth`|
|16.72|248|Windtrap300 /107|Commercial100, `idle_city_growth`|
|26.32|227|Windtrap300 /237|Commercial100, `idle_city_growth`|
|35.92|178|Windtrap300 /347|Commercial100, `idle_city_growth`|

Ordos2 has legal/offered LF,365 raw LF sites, AI site(23,9), actual LF400
(founded estimate464), and918/min modeled net tax. It never orders LF before
its CY dies at+121.584s. Only a real blackout at+114.32s starts Windtrap.
Starting the selected progressive power project, rather than buying another
cheap zone, is therefore necessary before testing subsequent LF acquisition.
Neutral6's different missing-LF endpoint is also not “never builds LF”: it
starts with LF1, loses its CY at+365.888s and LF at+416.768s, leaving no rebuild
supplier. These causes must not be pooled.

There are369/359/358/340 legal zone sites. Initial R demand is0/0/0/-95;
C demand908/0/373/0; I demand0 throughout. CityZoning found missing Stadium
caps for all four, missing Starport industrial caps, and missing Airport caps for
Mercenary/Rebels. Neutral selects urgent police (score6000) ahead of an affordable,
valid100-credit C zone (score8); this is a priority decision, not lack of cash for
that lot. No full-price initial zoning veto was established. All houses are T2,
where the saved mod's Nuclear Plant is tech6: money cannot make it legal.

The same pinned 15-minute zoning continuation is summarized in
`outputs/city-zoning-20261106/baseline-15min.json`: Ordos completes5C,
Neutral7C before its yard loss, Mercenary2R after reachingT4 at11.789min and
Stadium at13.282min, Rebels0 (T3 only at13.871min; civics remain unavailable).
Counts deduplicate `(house,object)` because co-op Ordos has two controllers.
Thus “no new R/I/C anywhere” is not the observed baseline, and a money gene must
not manufacture demand or ignore real civic/tech caps.

`problem1` and `ainonuclear` frozen save hashes are respectively
`3471c2f58cf449cf49fa38538db923877b94d9b3b668be68927d15e949e94db6`
and `91c8f0e23deff5bca62a29f36c714493fe000a11b5b6172608b535e7f6f7821b`.
PowerPlants' completed `problem1` baseline is pinned to
`68d07089102756c627558110cf409f8ca375c94e`,
`outputs/problem1-20261106/problem1-baseline/refusal-summary.json`.
All8 Brutal houses areT8, have legal reactors and valid sites (17/24/73/28/
42/41/19/23 sites initially). It records **35 reactor refusals with enough cash
for the complete2000 quote**. The1000 protected cash and small-shortfall/cost
preference still choose wind: examples include H0 need140/cash2196,
H6 need203/cash3694 and H7 need105/cash3750. Initial low-cash houses also exist
(395/3101/0/958/492/134/851/848); these are distinct full-quote versus generator
utility/footprint conflicts, not evidence of a universal reactor-tech lock.
CityCompactPower is the independent power-choice gene; StartWhatMakesSense
owns finance. The malformed staged-only `974b2650` insertion was corrected by
`dc2aee77` before native acceptance work; no result should use the malformed pin.

### S2 — historical saved crime veto with abundant cash

`docs/quantbot-army-recovery.md`, Required Research Lab progression, records
`notbuildingresearchlab`, SHA256
`8772302e2199d4de929c1b109f2c3151cc10e651b911358ea862e7c5882ebcb7`, same exact
Workshop revision. At cycle49399 Ordos/Neutral hold28981/27533credits, forecast
net tax1128/1238 per minute and333/268 legal lab sites, with no opening worker
shortfall. Four/three dangerous-crime properties alone made the **old** readiness
predicate false despite9/8 Police Stations. The saved-crime fixture's documented
OFF refusal versus ON real lab/research startup is evidence for that previous
veto, not a claim the newer required-research exception still refuses it.

### S3 — restored `nonuke`: full-price gating and earlier zoning, not a tech lock

PowerPlants' read-only audit at source
`d2bfb8296fe5dc701ae58c38ef85b177c55dcd94`, job
`20261107T012721-nonuke-exact-restored-revision-1a6bd0`, records save SHA256
`ee99099fdc0edef627394a046606d1ba3f8f9ff842445c732f381246db59431a`.
The native revision
`1a84d06fabab68d5342459e48c7a061f3d48975f920e4c4b6a0a559f6cc13d8f`
was reconstructed and verified across906 files from the archived3710464d bundle.
Receipts are `outputs/nonuke-20261107/audit-conclusion.json` and
`revision-reconstruction.json`.

This is a DuneCity city game (`city=1`), not Vanilla: four qBotBrutal houses at
T9, loaded cycle96192, saved Technology Installments OFF. Nuclear is enabled
and offered at yard level2 for every house. All houses already own reactors;
the save's name does not establish zero nuclear construction.

| House | Actual cash / virtual M | Current power surplus / reserve target | Existing Windtrap / Nuclear |
| --- | --- | --- | --- |
| Atreides1 |1452 /1133|3101 /2780|5 /4|
| Sardaukar4 |356 /183|2014 /2525|27 /2|
| Neutral6 |1307 /860|3783 /2489|25 /3|
| Rebels7 |878 /303|2821 /2516|28 /2|

All actual and virtual balances are below the2000 reactor quote, even before
the1000 protected-cash rule. Only H4 asks for reserve power. Its dedicated
growth yard first accepts an Industrial Zone at100; later generator selection
sees M83, need511, no actual blackout, a legal Nuclear site, nuclear price2000
and protected cash1000. It selects Windtrap but accepts neither generator.
CityCompactPower OFF/ON yields the same audit choices and accepted orders:
changing generator utility alone does not bypass the financial/yard ordering.

Both arms finish at the loaded cycle with zero elapsed gameplay. These are
same-pass observed selections, not paid/completed continuations or evidence
that removing one gate alone improves the economy. Other houses' placement
constraints remain separate; H1's audited AI Nuclear sites fail ground access.

A separate native15-minute continuation at the same source and save is now
complete: job `20261107T014632-nonuke-compact-only-swms-off-15m-e90114`,
receipt `outputs/nonuke-20261107/continuation-comparison.json`. Both arms
advance cycle96192→152442 (56250cycles); StartWhatMakesSense is explicitly0
and saved Technology Installments remains0. Observed CityCompactPower flags
are0/1, not merely requested overlays. Each arm accepts and completes12
Windtraps, no Nuclear Plants, with3600 credits of native generator production
payments (excluding foundations/upkeep). Outcomes are identical.

H4 supplies the funding-policy evidence:81 distinct generator evaluations
offer Nuclear and find an AI-approved site, but planning cash is0..612, below
the legacy2000 full-price AI gate. It accepts9 Windtraps and pays2700 credits;
there is no fully funded Nuclear refusal in this pair. H1 accepts the other3
Windtraps but never has an approved Nuclear site. H6 never requests power;
H7's power requests have no approved Nuclear site and yield no generator.
The full-price condition is an AI start policy, **not human engine illegality**:
ordinary generator construction already pays progressively for everyone.
This does not establish that an unfunded reactor would finish safely or that
the unified planner improves outcomes; the continuation isolates Compact only.

Any later SWMS comparison must start from the base immutable
`outputs/nonuke-20261107/nonuke-audit-with-revision.ini`, not this pair's
derived input that explicitly freezes SWMS0.

### T — saving can improve one milestone while making the army passive

`outputs/tech-saving-20261105/confirmation-analysis-8a89f35d/complete-decision-summary.txt`,
source `8a89f35db3ecadd709404aca70a42cdec58de0d7`:288 canonical games,
960 house histories, four seeds. Hard/Brutal balanced matched-game results:

- Dispatch3.5816->0.9896; delta-2.5920,95% CI[-2.9801,-2.2083].
- Kill value43309->11252; delta-32057, CI[-36113,-28121].
- Native decided games45.83%->0%; final nonempty military object price
  308.39->271.93, delta-36.46 CI[-53.69,-18.54].
- Latched missing-Starport improves23.35%->18.49%, but that single improvement
  does not clear the army/passivity failure. All six Hard/Brutal cells have
  adverse dispatch/kills in all four seed blocks.

These are global-OFF/global-ON selfplay health/decisiveness observations, not
relative ON-versus-OFF winning skill. All arms started/remained actualT9; a
research-enabled option is **not** low-tech research-acquisition evidence.
Earlier certified source69d5f5c4 has city ON0 accepted cheap orders,0 attacks,
0 kills and no HF/IX/Starport in18 histories while retaining mean3667 credits
at40m. Keep source cohorts separate; do not attribute all harm to one reserve.

### U — idle cash and capacity are real, but not a universal extra-factory need

`outputs/unit-choice-20261106/cash-capacity-diagnosis-5d7f1a27/findings.txt`, source
`5d7f1a2753d9835227eeefd5d312fb0ea4eaaad4`:288 games/960 houses, with
Hard/Brutal primary96 paired games/320 paired houses across six equally weighted
mod/research cells and11,584.50 common observed house-minutes per arm:

- Treasury1683.23->2835.01; +1151.78 CI[885.20,1453.85]. All four seed
  differences positive. Exact10-20m cash drift212.46->431.73credits/min;
  later cash runs down, so it is not endless whole-match accumulation.
- Extra held cash is classic-content-specific; city deltas+6.43/+2.77 have
  intervals spanning0. Do not claim city cash piling from this study.
- During paired rich-and-HF-present time, any ready-idle HF14.26%->20.02%.
  Yet strict next-HF-runway-funded AND all-HF-queued occupies only1.00%->3.16%
  of whole common matched time. City exposure under that strict test is0.
- Actual factory military output valued at nominal replacement prices is
  880.66->873.63/min, delta-7.04 CI[-39.73,25.66]; HF+67.57 and LF-88.98.
  Accepted military quotes fall111.56/min, but actual **all-scope** payCredits
  withdrawals fall34.25 CI[-71.66,3.73]. There is no military-only payment ledger.
- Missing classic funded-parallel candidate/options payloads are unmeasured,
  not0 demand. The forecast uses maximum eligible line throughput, not the
  realized mix; cash above buffer alone does not demonstrate funded expansion.

Separate earlier source `a1bf0d0be51785f085748dfd1e89a5a8bb1b9975`,
`throughput-diagnosis-a1bf0d0b/factory-report.txt`, shows composition blocking
34.25% of ON HF-object-time,28.62% post-IX. Do not pool those numbers with
5d7f1a27 or diagnose every idle factory as a cash gate. Both composition genes
remain OFF pending their owners' independent trials.

The native composition/capacity fixture preserves the existing city growth
interleave with a labelled, finite initial zoning queue on the oldest yard and
two other idle yards for income-scaled HF capacity and pending-order deduplication.
Its selected HF traverses `startYardSelection`, lane selection, final vetting and
the normal queue-acceptance wrapper. A default-constructed `YardTurn` alone is not
an initialized selection; a city with every yard idle may correctly allocate
growth before optional capacity. These setup requirements do not change any
production policy or enable the independently default-OFF genes.

### HIST — funding switches are context-sensitive

`outputs/tech-saving-20261105/baseline-summary.txt` and
`completed-evidence-summary.txt` distinguish actual FundedClassicProduction
OFF0W/4L/3T versus ON7W/0L/0T at source6aa02856 from similarly named
TrooperSupport `funded-*` arms. Historical supplier TrooperSupport increased
missing Starport15/109->31/111 exposed houses. Neither result proves a unified
planner good or bad; they require explicit interaction/context checks.

## 4. Conflicts and failure mechanisms

1. **Progressive payment is treated as an upfront debt.** A1 subtracts whole
   existing remainders, P1 requires a whole next quote, and many selectors/vets
   repeat full-data-price checks before P1. A project can pass a narrow civic or
   reactor installment exception, then immediately encumber its whole remainder
   on the next pass and freeze unrelated production. Simple skips some of those
   encumbrances instead: it is an inconsistent exception, not a shared drain model.
2. **A good endpoint forecast does not establish near-term liquidity.** The
   current four-minute projection cannot say whether two simultaneous queues
   empty the till before the next tax/spice receipt. Conversely a long civic can
   be affordable through slow installments even when its whole quote is outside
   the four-minute window. A single end-balance number answers neither question.
3. **There is no single owner of admission.** A selected item crosses readiness,
   full-price candidate filtering, per-builder reserves, yard vetting, founded
   package checks and a final wrapper. Narrow flags must survive every layer.
   LF saving can be overridden by cheap fallback, and full-price vetting can
   erase a choice before an installment-capable executor sees it.
4. **Overlapping protection is not the same as a proven double cash debit.**
   A11 deliberately uses `max(0,capacity-unpaid)` and Starport cargo is excluded;
   those correct protections must not be called bugs. However A12 adds a working
   maximum to a capital maximum whose WOR value already embeds working/strategic
   cash; A10 separately reduces the army's funded target by similar replacement
   capital. These are overlapping constraints on one balance. A16's extra queued
   debt test adds another full-debt prerequisite after A1. The redesign removes
   duplicate *requirements*, not fictitiously refunds already-paid cash.
5. **ROI capital and immediate obligation are mixed.** K includes worst-case
   slabs and amortized generation even when standing power/prepared ground
   already suffice. Civic/reactor callers pass K-price as a foundation budget,
   while execution uses real slab quotes. Estimated economic cost can therefore
   become an unnecessary first-payment floor. Power and slab dependencies should
   be explicit queue phases, not phantom immediate cash charges.
6. **Research readiness can depend on what research unlocks.** Fleet/crime/
   bootstrap/stability vetoes can require the very capability the next level
   would supply. S2 proves a historical crime veto with abundant cash. S1 proves
   full funding flags false, not absence of all research attempts: ordinary
   houses have an end-of-build fallback even when `research_required=0`.
7. **Separate power economies disagree.** Planned city reactor sizing can pretend
   enough cash, final sizing can demand full reactor+reserve, and vanilla power
   judges a whole package against30s income replacement/10m payback. The nuclear
   choice is also behind real tech/prerequisite rules. Twenty windtraps at T2 do
   not prove a T6 reactor was cash-blocked; technology progression is part of the
   dependency graph, not permission to bypass it.
8. **Hypothetical maximum production can crowd out actual starts.** Idle and held
   factories count as maximum sustained capacity in the four-minute model. This
   is deliberately conservative, but can make a positive-cash base appear unable
   to use even one existing line. U shows both idle capacity and a narrower true
   saturation/expansion opportunity. More factories alone cannot solve the former.
9. **Rollback and direct expenses lack one admission view.** Damage-triggered
   repairs, conversions and air/recovery reserve restoration use separate
   routes. Partial foundation cancellation does not restore the legacy pass's
   prior quote bookings [INFERENCE: later builders may see too little M].
   Free direct road overlays are not a missing cash debit; queued Road structures
   do cost money. Refresh actual queues on accepted production and rollback,
   and include actual repairs instead of inventing a second subtraction.
10. **A money planner cannot manufacture demand or fix composition.** The civic
    demand caps and C-versus-police choice in S1, and producer-family blocking in
    U, remain independent policies. They must supply valid wants/dependencies or
    be tested as separate genes, not be hidden in a funding change.

## 5. Existing first-AA selection and savings protection

`DefencePrerequisites` remains the existing hashed, default-ON switch. In the
original `norocketturrets` blue-house-6 continuation, b3 selected growth Nuclear
at cycle 78700 while a 200-credit urgent AA upgrade was affordable with 977
spendable. That proves a selection failure, not upgrade completion: the retained
negative control lacks the yard's health. The default arm also bought a
240-credit import while Radar was queued, immediately before a 250-credit
rocket candidate had only 32 spendable. These b3 failures remain negative evidence.
SourceY's C3 continuation later paid its `core_coverage` upgrade and Radar, but
queued a 2,000-credit Nuclear at 72300 with 267 spendable and power 500/499,
without an AA candidate. That is first-project continuity failure, not evidence
that aircraft were engaged at that instant.

The repair reuses the existing protected service/supplier classes in capital
selection and reservation. Canonically urgent services precede discretionary
military, additional production and growth, not genuine power recovery or native
income suppliers. The original non-military emergency comparison runs before
the urgent-service override. Only pending first-AA work or a real blackout
defers the yard's growth-power/plot lane behind service and power arbitration;
powered nonurgent cases retain their early allocation. Dangerous crime retains
its existing priority; moderate crime cannot relabel urgent first AA as optional.
The new arbitration/order is gated by `DefencePrerequisites`; OFF keeps the
original military/growth interleave and early allocation.

The first-AA project reuses the existing unlock demand: engaged aircraft, or
uncovered core assets after the existing four-zone opening. This is the same
predicate used by unlock candidates, not a new goal or saved latch. Until a
rocket is physically owned, its demand survives an absent candidate, inadequate
headroom or a temporarily unpriceable site. A useful legal chain protects its
**unordered** native upgrade, prerequisite, turret and actual foundation quotes.
Queued prerequisites and the current upgrade remainder are already deducted
from `money`, so are not reserved again.
An included AA component can spend its own savings even for nonurgent
`core_coverage`: it need not fund that same quote twice or wait for the entire
future chain. Native building/prerequisite offers share that global project;
only its priced yard's included upgrade gets local upgrade access. Native
affordability still applies, and another yard's unreserved upgrade gains no
component allowance or extra release.
The reserve survives a busy Radar/yard upgrade and is not waived to discretionary
factory/Starport orders during a blackout. It records the priced chain's builder,
included upgrade balance and actual per-step quotes. Same-pass releases remove
only a component that was included: another yard's local upgrade cannot reduce
the cheaper busy yard's Radar/rocket balance. Selected essential economy
suppliers retain their existing access **before** affordability/hold checks.
A committed rocket adds no second reserve: native queue accounting owns its
debt, but the physical first-AA project is still pending until placement.

Other yards cannot start an optional/proactive package by borrowing that cash
through a first-installment exception. One native-quote predicate checks the
actual building-plus-foundation package before slabs and again at acceptance,
subtracting neither queued debt nor an already-withheld producer reserve twice.
Fully funded free-cash projects and canonical essential economy retain access.
Real power need retains the timely Windtrap path. Nuclear quote admission is
applied during generator selection, including the legacy `nuclearPlan`
installment branch, so a refused reactor retains the legal Windtrap fallback
instead of ending the yard's turn repeatedly. Some power need does not license
an unfunded Nuclear project when legal wind is available. No broader power
selection model, credits, price, cap, goal or saved latch is added.

`threat-turret-probe.inc` contains native chain-identity, first-Carryall, repeated
nuclear-to-wind, OFF-control and paid AA completion consumers. The private
reported-save observer retains yard health, upgrade and full queue state at its
existing blue-house-6 samples.
None of these additions establishes a passing replay: the original save still
requires an actual newly placed rocket before natural loss under the unchanged
continuation contract.

