# QuantBot shared spending — 1.0.706

A deterministic, four-simulated-minute forecast compares the next economy,
combat-unit and additional-production investments. It is a bounded scoring
model, not a full mathematical optimizer or a prediction of combat outcomes.
No new saved AI state or save-format change is required.

## Cash and independent queues

Spendable cash is current credits less unpaid production and upgrades already
queued. Paid Starport cargo counts toward unit totals without reserving its
price twice. Every newly accepted order, foundation, upgrade and road batch
charges this same planning budget exactly once. Starport budgeting uses the
port's displayed offer, which is the price its queue actually charges; market
quotes can refresh before the build list does.

Protect the price of the highest-scoring next purchase (plus needed concrete).
Other yards/factories can use the remaining credits immediately. Construction
therefore runs alongside factory harvesters and military production when funds
permit. Saving for a worthwhile worker does not reserve the cost of an entire
hypothetical future fleet. Existing human-allied campaign full-fleet rules are
an explicit exception; custom allies and opponents use normal shared spending.

In Dune City, once multiple construction yards exist, the oldest usable yard
is dedicated to demanded R/C/I. Its current job finishes normally; its next plot
gets an explicit construction assignment and its purchase budget is protected.
Other yards still handle police, defence, power, factories and refineries, and
may also build zones. Incidental zoning by another yard does not divert the
dedicated yard into services. Recompute the assignment from real builders every
pass; no random preference, timer or new save field is involved. If that yard
has no legal plot, another usable yard can take over.

Every otherwise idle city yard can build an affordable, demanded plot, including
when a dearer project is waiting for credits, another yard claimed its order, or
its chosen structure has no legal site. Choose R/C/I directly from demand and
committed balance, without letting a refinery or a four-minute return comparison
replace this fallback. Check the actual offer price (a 100-credit plot needs 100,
not an arbitrary 200-credit balance), legal placement and at least 24 spare power.
Existing commitments remain reserved; no forecast income is spent as cash.

The single opening yard retains its essential infrastructure progression, but
also gets this idle fallback. The existing protection against military savings
now binds the actual yard selection as well as reserving credits. A blackout,
absent demand, insufficient cash or unavailable land still prevents zoning.

`[Force Concentration] CityGrowthPower=1` (hashed, default on) extends that
growth allocation to its real generation prerequisite. When a demanded legal
R/C/I plot has a positive return but less than 24 spare power, the capital
planner substitutes a legal Windtrap, or an already affordable Nuclear Plant,
for the plot. Its full price and required concrete are reserved from military
orders and Starport bargains, including while cash accumulates. A pending
generator prevents another allocation; after it completes the ordinary zoning
and military rules resume. Actual air/crime protection and required civics keep
their existing priority. The switch is inert outside city games; off preserves
the previous allocation, including the power-starved Starport stall.

The `quantbot_no_spice_city` engine fixture reproduces the four-zone,
ten-spare-power/cheap-import deadlock from `harkonan doing nothing.dls`, checks
the off/on queues and exact funded generator, prevents a duplicate pending
generator, and confirms zoning resumes with power. No saved AI state changes.

Falling cash alone is not a shortage. The four-minute runway comparison is:

`projected cash = cash − unpaid orders + forecast net income − continued production cost`

Continued production cost covers available unit factories **and** demanded
construction/economy work. Each line deducts its already-reserved queue from its
four-minute operating cost, avoiding a second charge. Build prices, city build
times, health and speed limits determine its rate. The log separates the current
production burn from the sustained rate if available lines keep running. Income
uses delivered workers/bays and current tax, capped by remaining spice; unbuilt
workers and undeveloped zones are not immediate income. Paid cargo still counts
towards fleet targets. Future repair bills remain an unmodelled variable.

If current uncommitted cash and projected cash both cover working capital,
vanilla uses its established parallel MCV/factory/economy opening. There is no
fixed 20,000-credit wealth switch: All against Atreides' 100,000-credit grant
provides runway even with falling cash, while a larger base can exhaust the same
grant faster. Otherwise marginal priorities protect the next investment. Dune
City retains city/service candidates; factory expansion must leave funding for
existing construction throughput as well as unit lines.

For the first heavy factory in a custom game, budget its missing prerequisites
and four minutes of operation. If available/projected cash covers both plus the
working buffer, unlock that production line before extra opening refineries or
the Starport. A rich map does not need to wait through an income bootstrap.
Poorer openings retain their income-first progression. Both Starport construction
paths require an enabled unit in the map's CHOAM catalogue: zero stock can restock,
but an absent entry cannot. Owning a heavy factory does not waive this check.

`[Force Concentration] CityStarportSupply=1` (hashed, default on) makes the first
useful Starport a protected one-off capital project for a city whose seed economy
is complete and whose tax forecast exceeds upkeep and can fund the building.
It uses the real CHOAM catalogue, legal site, prerequisites, concrete and queue
commitments. A pending or standing port ends the claim. Without it, cheap city
plots and the first-heavy-factory saving repeatedly win before the Starport
fallback is reached, even when a developed no-spice city earns enough to buy it.

`ThreatTurrets=1` in the same section (hashed, default on) lets Custom Hard/Brutal
buy local rocket cover against **visible armed ground units near the production,
income and power core (including Radar)**, rather than chasing every outlying plot.
The build's existing visible-unit survey supplies contacts; hidden armies, aircraft,
workers and saboteurs do not increase this goal. Contacts are transient per-pass
scratch (no unsaved two-second decision cache). One rocket per 400 credits of
nearby observed fighters, rounded up, demands 2–8 local firing covers, with a
12-rocket house-wide ceiling. Existing and reserved local emplacements count.
An active core raid protects the next turret or power/tech prerequisite ahead of
repeat plots and unit spending, without waiting for two refineries and a
completed production core. This finite goal and the house-wide cap bound the
claim; the first useful Starport then owns its one-off funding. Prematurely
yielding the budget after only two new guns loses both the supplier and the base.
An import's high return score cannot replace either bounded claim with cheap
zoning through the military/growth interleave while its price is accumulating.
The peaceful 8–12 battery and growth interleave stay unchanged. Site search is
bounded to the threatened asset's weapon-range window, first minimizes observed
return fire while retaining a hostile target in range, then favours firing cover.
It preserves spacing, roads and ground access. Once the local goal is met or
contacts disappear, the claim ends.
Ground-defence generation and tech rebuilds prefer legal sites inside the standing
rocket battery, minimizing observed return fire; a Radar rebuilt outside it can
otherwise die again before its first replacement rocket finishes.
Switches off preserve the old paths. No save-format or city-tax-rule change.

Funded Dune City custom games use the established parallel factory/repair build
order when current cash and projected cash cover another heavy factory, four
minutes of its operation and the working buffer. The city-yard target is never
a prerequisite for factories. In this funded case, the opening worker target
also does not block an affordable MCV. Factories use their normal upgrade,
MCV, worker and military paths instead of repeatedly taking the scored worker
shortcut. The one dedicated R/C/I yard remains active alongside production.
When the forecast no longer covers parallel expansion, shared spending resumes.

Before zoning, reserve two currently legal sites: one heavy-factory footprint
and one repair-yard footprint, each with a one-tile access margin. R/C/I cannot
consume those sites. Recompute from the real map each build pass; military and
infrastructure construction may use the plots. No save-format change is needed.

The default/zero harvester option means no engine ceiling. A positive Game Options
override is enforced. Old serialized map-size defaults no longer restrict a
loaded house. QuantBot still chooses economic targets from remaining spice and
income/capacity, and campaign enemy/late-mission helper policies still shape its
own fleet; those decisions no longer impose an engine cap on a shared human house.

## Optional Research Lab policy

`[Force Concentration] ResearchLab=1` is a multiplayer-hashed QuantBot setting,
on by default even in older configuration files without the key. It acts only
when the match enables the separate **Research Lab** game option. With that
option off, no lab planning, research payment or extra random draw is performed;
the original match technology still supplies the house's build gates.

The normal yard order builds one lab after essential income, power, services,
basic production and defence, before optional advanced technology and production
expansion. Campaign reconstruction keeps its authored essentials first. The
lab uses the same legal-site search, complete foundations, queue acceptance and
shared cash ledger as other buildings. Pending labs count across every yard.
If its extra power is not funded by standing generation, a legal generator is
bought first, retaining funding for both purchases. A destroyed lab is rebuilt
on the same terms, including at tech 9; research itself stops at level 9.
Fighter-only and switched-off partners do not spend on either.

Both lab construction and research require delivered spice income or an
established, bankable tax base, a positive net-income forecast, a funded opening
fleet, and completed income bootstrap. Neither runs during a visible attack on
our structures/workers, an engaged air attack, dangerous developed-district
crime, or a power shortage. Keep at least 1,000 credits of working capital
(or the larger existing economy/unit buffer), **plus** the largest currently
protected civic, strategic, worker or capital purchase. Current uncommitted cash
and the four-minute production forecast, after this pass's accepted spending,
must both retain that reserve. Forecast receipts never pay an immediate bill.

After ordinary producers have taken their turn, the lowest-object-ID live owned
lab starts the next level through the House research API. The full price is
`300 × target tech`; duration is `30 + 10 × target tech` simulated seconds.
Accepted research charges this pass's shared ledger once. The AI emits
`research_started` with `from`, `to`, `cost`, `cycles` and `lab`; House events
record completion or cancellation. Lab destruction cancels its active work
without a refund, not the levels already completed.

Production gates, prospective unit-mix slots and strategy tech bands use the
house's researched level. The runtime slot cache and strategy expansion refresh
when that level changes without drawing a new saved strategy or tempo.
Campaign mission-stage difficulty and opening timing still use the match's
authored level, not research progress.

## Comparison

| Investment | Forecast and priority |
| --- | --- |
| Harvester | Additional receipts after production/delivery, accounting for the existing fleet, unloading capacity, travel and remaining spice. Must recover its purchase cost within the horizon. |
| Carryalls | First transport priority 5,500; scale with worker/repair ratios and unserved pickups. Below half target scores 4,500, other shortages 2,000. Real Starport stock or High Tech production is required. |
| Refinery | Additional unloading capacity and the included worker; excludes existing income. Actual loaded-worker queues can justify a bay even as unharvested spice declines. |
| Dune City R/C/I | Demand/site-supported growth after construction and growth delay, net of power upkeep, with allocated generation/foundation cost. Existing unfinished plots reduce confidence. No fixed tax-to-spice ratio gate. |
| Combat unit | Military value per purchase credit, weighted by the fraction of the army target still missing. Active attacks on the base/workers raise defence priority. Existing composition and difficulty limits remain. |
| Extra factory | Constrained spending requires busy existing lines and forecast funding/army shortfall for added production. Funded city expansion uses the established cash/income factory target without waiting for the construction-yard target. Light expansion still needs a light-unit deficit. |
| Extra construction capacity | Dune City demand plus usable rock or an expansion site, below its yard target, with enough forecast funding for the MCV and working capital. City score 5,000 (vanilla 4,500) protects its purchase price even before cash reaches it. A currently idle yard is not proof that one yard can meet sustained demand. |
| Police and rocket turrets | Actual uncovered buildings or crime justify services through the existing placement/coverage calculation. Moderate crime scores 2,500, dangerous crime 6,000, uncovered air defence 2,000. These can save their price instead of depending on leftover cash. |
| Repair capacity | Fleet baseline plus damaged-vehicle queues. Extra bays score 1,800 for fleet growth or 3,500 for a backlog. Existing difficulty/technology restrictions remain. |

Economy score is forecast net receipts divided by total capital cost (scaled
by 1,000, capped at 4,000). Normal military score is value/price multiplied by
the army shortfall in thousandths; active defence adds 4,000 before applying
value/price. Additional-factory score uses only the extra military production
that can actually be funded beyond existing capacity. Stable ordering resolves
ties; forecast income is never treated as cash available to spend now.

Spice receipts compare the current fleet with the expanded fleet over the same
remaining resource pool, including harvesting during delivery delay. This
reduces the value of extra harvesters as the existing fleet can exhaust the
remaining fields. The travel estimate is a bounded local sample, not a new
pathfinding pass. Existing Carryalls shorten the supported share of the fleet's
trip estimate (up to five workers each), using flight speed plus a pickup/landing
allowance; queued aircraft do not count as delivered capacity. It does not model
individual Carryall routing. Forecasts
remain estimates; use delivered-spice and tax telemetry to assess their error.

The first carryall uses the displayed Starport price, includes paid/in-flight
cargo in its committed count, and is purchased before optional repair-yard
construction. If cash is short, cheaper troops cannot consume its savings.
Unavailable stock/technology and air limits do not reserve funds. There is no second
fixed half-cash restriction on economic imports; the shared reservation and actual
prices still protect competing purchases and prevent overspending. This applies in both mods and to campaign helpers and custom bots.

### Support ratios and queues

These are initial tuning baselines, not measured optimal ratios:

- Carryalls: `ceil(workers/5) + min(ceil(combat vehicles/20), 2*operating repair yards)`.
  Include committed workers, exclude infantry and aircraft from repair traffic.
- Repair yards: `max(workers>0 ? 1 : 0, ceil(combat vehicles/25))`. A Starport army
  needs support independently of its number of heavy factories. The previous
  four-yard/factory-count cap is removed.
- Refinery forecast: `ceil(committed workers * worker receipts per minute / bay receipts per minute)`.
  With short trips this is typically around one bay per 4–6 workers. Travel,
  remaining spice and actual unloading queues refine it; vanilla's existing
  opening refinery priorities remain. A persistent ten-second queue of at least
  two full workers can justify an extra bay without waiting for the estimate.
  Count loaded field returners targeting occupied bays too: they are blocked
  before they reach the base because no free bay can accept a Carryall delivery.
  Only free, unbooked bays offset that queue. Pending refineries prevent duplicate
  queue-relief orders. This now reaches the vanilla build path as well as city.

If Carryalls are below target and the Starport is sold out (or absent), a legal
first High Tech Factory receives transport priority. An existing or pending
factory prevents duplicate supplier orders. This keeps a ratio from becoming
a permanent unmet target when imports alone cannot supply the fleet.

Two or more unserved jobs with all suppliers busy raise the Carryall/repair
target by one, provided no replacement/expansion is already queued or paid for.
Idle suppliers prevent this queue override. A finished repair or empty harvester
waiting for a return flight is transport pressure, not another busy repair or
unloading job. This prevents solving a Carryall shortage with extra buildings.

Dune City includes R/C/I, tax and municipal/power expenses. Vanilla has no zone
candidates or tax forecast. Its configured power rules still apply. First
income/technology prerequisites, demanded civic buildings, replacement of lost
infrastructure, power recovery and campaign-authored restrictions retain their
existing rules. Emergency generator orders can commit more than current cash;
ordinary purchases cannot. Running repair bills are not reserved in full.

## Decision capture and SQLite

Telemetry version 15, policy `dedicated-city-growth-v71`, records:

- `capital_plan`: cash/commitments, horizon, resource and income estimates,
  military target/current value, producer queues, all common spending
  candidates, unit-mix deficits, prices, scores, eligibility and chosen option.
  Plan policy version 2 also includes cash runway, active/sustained burn,
  projected cash, construction/unit operating costs, funded army target,
  transport/repair/refinery targets and queues, and property crime counts.
  City allocation adds `city_growth_dedicated_yard`, `city_growth_yards_busy`
  and `city_growth_builder`; accepted orders distinguish `dedicated_city_growth`
  from `idle_city_growth`. The dedicated candidate score 1 represents an explicit
  allocation, not a forecast return. These fields are available in SQLite too.
  Cash-funded vanilla passes are captured too, with reason `funded_parallel_production`.
  Version 14 adds usable Starport market, funded first-factory capital/operating
  costs, protected city growth, unbooked refinery bays, blocked field returners,
  and walking versus transport-adjusted trip estimates. SQLite exposes these
  decisions; protected growth uses reason `protect_demanded_city_growth`.
  Version 16 adds `funded_city_production` and `next_heavy_runway`, also in SQLite.
- `city_production_plots`: the reserved factory/repair sites; placement quality
  records `production_plot_rejections` when zoning would consume them.
- `city_economy_comparison` and `zone_evaluation`: detailed tax/refinery
  forecasts, demand, growth confidence and placement rejection reasons.
- `production_order`: accepted/rejected queue order, actual quote, rule,
  builder, state and `capital_plan` sequence reference.
- `capital_upgrade`, `capital_road_batch`, `capital_order_blocked` and
  `capital_outcome`: auxiliary commitments, blocked purchases, selected-order
  fulfilment and remaining budget.

All are written to the existing native/browser session JSONL and imported into
SQLite by the existing importer. Join references with both session and sequence.
New views: `capital_plans`, `capital_candidates`, `capital_orders`,
`capital_outcomes`. `capital_orders` includes successful production, upgrades and
road batches. Detailed rejection/state payloads remain in `events.data`.
`capital_plans` exposes runway, net burn, projected cash and support targets/queues.

```sh
python3 scripts/ai-decisions.py --db /tmp/game.sqlite import /path/events.jsonl
python3 scripts/ai-decisions.py --db /tmp/game.sqlite query "
 SELECT p.house,c.kind,c.item,c.score,c.reason,p.spendable,p.spice_share
 FROM capital_plans p JOIN capital_candidates c USING(session,plan)
 WHERE c.selected=1 ORDER BY p.cycle"
python3 tests/ai/report-spending.py /path/events.jsonl --output /tmp/spending.json --check
```

The audit checks plan/order links, ordinary overspending, accepted-cost
reconciliation, and accidental vanilla zone orders. It reports capture
completeness and refuses a clean audit on a capture-limit marker. Session
capture remains bounded (normally 256 MiB); reaching the routine-event allowance
now emits an explicit marker while retaining room for terminal summaries.

## Reproducible validation

The engine probe exercises exact worker savings, simultaneous construction,
depleted spice, cash-starved factories and funded production expansion in both
mods. It also checks factory upgrades, MCV savings, multiple yards, defensive
yard unlocks and additional transport. Its Research Lab cases cover the hashed
policy/match off switches, reserve protection, one real lab order across yards,
research payment/completion, refreshed factory offers and cached strategy slots,
cancellation and rebuilding. SQLite and spending-audit tests count the AI
research charge once, not again from the House lifecycle event. The Starport
probe also covers stale market/display quotes and legacy campaign/custom
bulk-import rules. Tests run against an isolated profile.

```sh
python3 tests/ai/run-campaign-balance.py --build-dir build-705 \
  --output-dir /tmp/new-spending-test --level 9 --mod dunecity \
  --house harkonnen --partner-difficulty brutal --enemy-difficulty hard \
  --seed 701 --minutes 1 --shared-spending-probe
```

Repeat with `--mod vanilla`; use `--starport-probe` for imports. The runner also
accepts `--custom-map PATH` for all occupied custom-map slots, preserving named
house teams; `--free-for-all` gives each house its own team. `--enemy-ai ai-player`
selects the legacy AI Player controller (default is QuantBot); `--roster` accepts
explicit `house:team` slots for reproducing a reported lobby. Diagnostic capture
defaults to 1 GiB without changing the shipped game's limit. A time-limited
simulation is behavioural evidence, not proof of balance across all maps.

## Regression comparison with 1.0.700

The 1.0.701 common purchasing shortcut ran before the existing factory upgrade
and MCV branches. Repeated orders therefore suppressed technology progression.
It also computed light-unit deficits against the full military ceiling instead
of the funded army plan, and permitted factory expansion without checking light
composition. A busy factory is not by itself evidence that more light units
are useful.

1.0.703 lets normal factory upgrades and composition allocation run before
ordinary military orders. Worker shortcuts also yield to a funded vanilla MCV
unlock. Dune City unlocks one defensive yard before repeat zoning; other yards
keep constructing. The existing power, technology and difficulty prerequisites still
apply. No saved state or random tie-breaking was added.

| Decision | 1.0.700 | Regression in 1.0.701–702 | 1.0.703 correction |
| --- | --- | --- | --- |
| Military production | Upgrade, then funded composition | Immediate orders bypassed upgrades; deficits used ceiling | Restore upgrade path and funded deficits |
| City growth | Separate economy slot; cash-funded MCVs | Factories outbid R/C/I; idle-yard and military-capacity gates blocked MCVs | Parallel R/C/I; reserve one useful MCV; gate duplicate factories on demand |
| Transport | Existing fleet/repair ratio, often late | Only the first carryall received priority in 702 | First plus scalable shortage candidates for both suppliers |
| Crime/defence | Service search needed spare funds | Other spending repeatedly consumed service price; zoning bypassed defence unlock | Reserve affordable service plans and unlock one defensive yard |

The model is still a bounded estimate. Do not call it a full optimizer or assume
that one won match proves balance. Compare actual economy, production and combat
telemetry, not just which candidate had the largest score.
