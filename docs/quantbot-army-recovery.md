# QuantBot army recovery

Custom-game Hard and Brutal bots use a house-level army posture, rather than
asking every unit to decide independently whether the whole army is losing.
Campaign roles, support bots, and Easy/Medium retain their existing pacing.

## Independent existing-composition unit choice

`[Force Concentration] ExistingCompositionUnitChoice` is an independent,
persisted, multiplayer-hashed switch, **OFF after the standalone paired trial**.
It changes unit/factory selection only: the owned plus accepted-queue **VALUE
shares** move toward the existing target, without using a future military limit
as a cheap-unit quota. Once an advanced supplier exists, light
emergency/affordability filler is disabled. Human-order leases, saboteurs and
unit-role ownership remain authoritative.

The target remains the same configured/strategy prior or learned mix, expressed
in basis points of vehicle/aircraft replacement **value**, not object counts.
For each producible target slot at or below its existing share, add one unit's
normal replacement price to its owned plus accepted-queue value, then sum the
absolute differences between the resulting integer-basis-point value shares and
the target shares. Choose the smallest post-order error; ties prefer the
largest existing credit-value shortfall, then stable slot order. No cash balance
or future cap enters that comparison. Empty/exactly balanced armies take the
closest indivisible growth step; a whole unit can overshoot a small share.

This is the historical `nextExistingSlot` selector, unchanged for a causal
unit-choice-only comparison, not a new maximum-deficit or cheapest-unit rule.
Learned or supplier-limited targets can themselves be light-heavy. Busy or
upgrading producers remain in the strategic comparison. If the selected supplier
is busy, the unit unaffordable, or the military ceiling would be exceeded, wait
instead of selecting a cheap alternative. Recompute the shared composition after
every accepted order, including Starport bargains. Existing infantry quotas and
economic/transport unit choices are unchanged.

Standalone selection groups authored custom combat variants into the same ten
families and counts their actual owned/queued nominal value. Actual builder
offers supply fieldability, candidate prices and cached LF/HF/HTF supplier masks;
a variant-only factory does not need an unavailable canonical unit. This does
not aggregate new variant combat evidence into the existing learning model.
The final optional-repeat-factory gate uses those masks in both shared-spending
and funded-parallel modes. True first/rebuilt suppliers and economic/transport
unit policies retain their existing admission.

`--existing-composition-unit-choice-probe` runs this policy's engine cases in a
separate process from `--simple-tech-saving-probe`. CTest registers Medium, Hard
and Brutal cases for vanilla, DuneCity, Dune2R and Tornie; the latter exercises
authored variants and variant-only suppliers. The paired unit-only military
case gives both arms 9,999 credits, below the unrelated third-yard MCV wealth
trigger, while retaining the huge military ceiling and real paid factory-unit
completion and preserved-queue assertions. It does not change that MCV policy.

This switch does not change technology/economy priorities, construction
installments, upgrade payment or capital reservations. It does not enable
`SimpleTechSavings`, whose rejected combined experiment remains independently
**OFF**. The standalone `existing-composition-unit-choice-off/on` styles both
pin `SimpleTechSavings=false` and differ only in the new flag. The probe's
per-player build-site audit manifest records the effective flag and config hash.
The historical Simple results below are not evidence for adopting unit choice
alone.

### Measured unit-choice-only outcome (2026-11-06)

Decision: **keep `ExistingCompositionUnitChoice=false`**. The exact paired
288-game/960-house/four-seed cohort is pinned to
`a1bf0d0be51785f085748dfd1e89a5a8bb1b9975`, with `SimpleTechSavings=false`
in both arms. The primary comparison below is Hard/Brutal, matched houses
averaged within games and the six mod/research cells weighted equally.
Intervals are paired game-bootstrap 95% CIs; the four shared seed blocks are
reported separately in the evidence. These are symmetric global-OFF/global-ON
selfplay results, not direct ON-versus-OFF winning skill.

| Metric | OFF | ON | ON − OFF [95% CI] |
|---|---:|---:|---:|
| Native winning-house share | 21.18% | 13.54% | −7.64 pp [−11.98, −3.82] |
| Native decided matches | 45.83% | 27.08% | −18.75 pp [−28.13, −10.42] |
| Ground dispatches / house | 3.582 | 3.471 | −0.110 [−0.393, 0.181] |
| Credited kill value / house | 43,309 | 36,922 | −6,388 [−8,361, −4,477] |
| Live-object value, observed alive-time mean | 259.07 | 275.67 | +16.60 [10.66, 22.00] |
| Live-object value, final nonempty matched population | 299.82 | 307.61 | +7.79 [−20.61, 34.28] |
| Cheap / military accepted quote share | 28.80% | 20.14% | −8.66 pp [−10.09, −7.30] |
| Latched economy stall | 31.51% | 38.63% | +7.12 pp [−2.78, 16.58] |
| Latched bankrupt/empty queue | 34.98% | 35.33% | +0.35 pp [−6.34, 6.86] |
| Latched missing Starport | 23.35% | 21.79% | −1.56 pp [−3.99, 0.78] |

The alive-time value gain and kill-value loss have the same sign in all four
pooled seed blocks. Kill-value point estimates are lower in all six Hard/Brutal
mod/research cells. Thus the value rule reduced cheap accepted-quote share and raised the
observed-time unit value, but failed the “not consistently weaker” condition.
Its dispatch result did **not** reproduce the combined trial's 3.58 → 0.99
collapse; the other components or interactions are not identified by this trial.

Completed-building adoption and selected paired-achiever timing:

| Building | Completed OFF → ON | Adoption Δ [95% CI] | Paired completion minute OFF → ON | Time Δ [95% CI] |
|---|---:|---:|---:|---:|
| Heavy Factory | 83.33% → 84.20% | +0.87 pp [0.35, 1.48] | 11.52 → 11.38 | −0.14 [−0.32, 0.03] |
| IX | 71.70% → 80.90% | +9.20 pp [5.64, 13.02] | 22.72 → 18.68 | −4.05 [−4.77, −3.26] |
| Starport | 93.06% → 93.06% | 0 pp [0, 0] | 9.95 → 9.96 | +0.02 [−0.06, 0.10] |

All 288 native captures and quote histories were complete; no sample or
composition rows were missing. Final prices require both-arm nonempty armies
(201/320 Hard/Brutal houses); missing-Starport eligibility is 304/320. Timing
conditions on both arms completing: HF 242, IX 160 and SP 296 house pairs.
Death and administrative censoring, lower/upper by-40 bounds, all Medium/Hard/
Brutal cells and both pooling sensitivities remain explicit in the CSV evidence.
Accepted quotes are commitments, not paid installment cash. All starts were T9;
research-enabled arms do not demonstrate research acquisition.

Evidence:
`/Users/stefan/Documents/projects/outputs/unit-choice-20261106/analysis-a1bf0d0b/`
(`report.txt`, `per-cell.csv`, `pooled.csv`, `seed-blocks.csv`, `map-blocks.csv`,
and completed-building/coverage tables). Historical target mix and why the
unchanged integer-L1 selector bought cheap units are separately preserved in
`prior-target-mix-report.txt`, `prior-target-mix-by-house-difficulty-mod.csv`
and `prior-cheap-choice-mechanisms.csv` in that evidence root's parent.

Source qualification is separate from the outcome estimates. The study pin's
full suite passed 160/166; it is not relabelled as green. After fixture-only
separation and setup corrections, qualification pin
`616d86d73ff2c38d502a6fa95af0ef99826ebb77` passed **175/178**, including all
**12/12** standalone unit-choice engine cases (four mods × three difficulties).
The all-against-Atreides regressions (vanilla and Dune2R) and scenario matrix
passed at both pins. Three `quantbot_simple_tech_savings_dunecity` cases
(Medium/Hard/Brutal) still failed on both Macs; these exercise the separately
rejected technology/economy policy and are not claimed fixed. No simulation
policy changed after the study pin; later changes were test harness, fixture
setup and documentation only. See `final-qualification-616d86d7.json` and
`decision-a1bf0d0b.json` in the evidence root for exact sources and receipts.

## Stateless technology experiment and save compatibility

`[Force Concentration] SimpleTechSavings` is hashed and **OFF after rejection**.
The last authorized unreleased iteration combines existing-owned plus accepted-queue composition
with economy-first, ordinary installment construction. A military limit is a
ceiling, never the denominator that entitles cheap units to an opening quota.
The next unit/factory moves the existing value composition toward its target;
legal advanced ground production disables cheap emergency/affordability filler.
Standing infantry uses the existing army share. Independent reactive rocket
infantry remains available when a legal advanced launcher cannot supply it.

There is no threat-sized military hold while a tech item builds. Factories keep
their ordinary purchasing, economic working cash and real production commitments.
A selected legal tech structure may start at zero credits and pays through the
same engine installments as a human order. Its yard first visits real power,
storage, city seed/service, worker/refining and first-Starport selectors; needed
economic capital owns the shared planner before optional tech. Positive repeat
zoning is not an endless tech veto, and other yards retain their normal growth
fallback. Upgrades retain the full live-credit rule and normal quote debit in
the AI's planning cash; the engine grants no AI exemption. Required Research Lab
progression keeps its independent ordinary investment path. Campaign, human-order,
saboteur and unit-role authority are unchanged.

Removal-only commit `3710464d` retired the rejected income-ledger experiment,
not merely disabled it: no `TechSavings` option/hash key, factorial styles,
saved balances, debt/share arithmetic or ledger-only fixtures remain. The nine
old ledger tests are retired with that implementation. The stateless helpers
and Simple fixtures use no ledger API or compatibility alias.

Current saves are **9883** and protocol **112**, matching the installed format.
The experimental 33-byte QuantBot tail and its digest contribution are gone;
the ordinary 9882/9883 layout is restored. Simple needs no saved runtime state,
so multiplayer saves/observer base snapshots need no additional wire bump.
The existing supplemental observer layout is unchanged. Interim unshipped 9884
files are unsupported. Synthetic engine fixtures load both 9882 and 9883 bot
blocks, resave/reload 9883, check following-record alignment and verify that
Simple OFF/ON alone adds no serialized bytes. Private saves stay outside Git.

At `3710464d`, Main's full suite was **159/165** (six known Simple failures).
The separate retirement-only selection was **158/159**: `mouse_controls_probe`
timed out on Pro and its automatic Mini retry; every selected AI/save/network
test passed. These are different runs, not interchangeable green claims.

The economy-first policy at `8a89f35d` passed **156/165**, with all nine Simple
fixtures failing and every other test passing. Archived native evidence found
three fixture-premise defects: ROI cost 712 was compared with physical HF quote
600; a fully prefunded 300-credit upgrade finished before its first new income
receipt; and the Vanilla Hard/Brutal setup still lacked the bounded fourth
opening refinery. The fixture-only correction checks physical price/foundation
components, requires arriving income for unfunded starts, and genuinely clears
that refinery opening. Payment, progress, completion and zero-cash acceptance
assertions remain. Its own pinned results must not retroactively relabel
`8a89f35d` all-green.

Fixture-only `88fbd772` passed **6/9** (all Dune2R/Vanilla); its three City
failures had insufficient physical initial power reserve. `048a5fae` corrects
that setup with real Windtraps, without changing gameplay thresholds or payment
rules. Its separate City selection still passed **0/3**, on both Mini and Pro
(`20261106T030739-tech-city-physical-power-048-f30819`).
The remaining bounded failure is an HF upgrade: Medium/Hard start with zero
cash, Brutal just below its 300-credit quote. After 18,750 cycles (five simulated
minutes), cash is 1,214/1,354 with arriving income and sufficient power, but no
target upgrade payment/completion. Emerging dangerous City crime disables
`techSavingActive`; inactive/unselected tech cannot admit that upgrade.
The initial stable premise does not persist. This is observed non-completion,
not an all-green fixture or justification to bypass civic/emergency priority.
No further fixture or policy iteration was made after the final cohort rejected
the experiment.

The approved private full-Game probes at `8a89f35d` passed **2/2**: actual 9882
and 9883 saves loaded, ran 600 cycles, resaved/reloaded as 9883 and continued with
native-worm/state checks. Their input/native binary hashes and local-only
receipts are in `../outputs/tech-saving-20261105/private-compatibility-8a89f35d/`.
No private save was committed. The later Air modification-time query timed out;
no newer save, or absence of one, is claimed.

### Final economy-first trial: rejected, default OFF

The last authorized policy iteration is `8a89f35d`: **288 original native games,
960 house histories, four seeds**, DuneCity/Dune2R/Vanilla and Medium/Hard/Brutal.
TrooperSupport and FundedClassicProduction stayed ON in both arms. Whole-stream
admission reconciled 11,431,003 raw records, 254,076 accepted quote records and
47,707 complete cheap-order contexts. The obsolete ledger is absent, not a
zero-balance treatment. Neither the ledger design nor the Simple iterations
passed adoption. **No further policy iteration was made; Simple remains OFF.**

Hard/Brutal matched equal-game means, balanced across the six mod/research cells:

| Metric | OFF | ON | Paired change, 95% interval |
| --- | ---: | ---: | --- |
| Native credited wins per house | 0.2118 | 0 | -0.2118 [-0.2517, -0.1684] |
| Native decided-match fraction | 0.4583 | 0 | -0.4583 [-0.5417, -0.3750] |
| Dispatches | 3.5816 | 0.9896 | -2.5920 [-2.9801, -2.2083] |
| Kill value | 43,309 | 11,252 | -32,057 [-36,113, -28,121] |
| Actual live per-object replacement price | 308.39 | 271.93 | -36.46 [-53.69, -18.54] |
| Economy-stall detector | 0.3151 | 0.1632 | -0.1519 [-0.2379, -0.0660] |
| Bankrupt/empty-queue detector | 0.3498 | 0.1936 | -0.1563 [-0.2431, -0.0720] |
| Missing-Starport detector | 0.2335 | 0.1849 | -0.0486 [-0.0816, -0.0174] |
| Gross spice per observed house history | 85,295 | 75,998 | -9,297 [-12,489, -5,891] |
| Gross tax per observed house history | 6,972 | 5,115 | -1,856 [-2,971, -852] |

All six cells' dispatch and kill-value game intervals were adverse, with four
negative seed blocks in each. Outcome, income, stall and bankruptcy coverage is
320/320 paired houses; live price is conditioned on 205/320 nonempty measured
pairs, not zero-imputed. Missing-Starport has 304/320 eligible pairs: ever-latched
absence at a sampled minute >=20 while alive with a yard, with both arms exposed;
later completion does not clear the flag. Own-house elimination fell
0.2969→0.0642 and alive-final rose 0.7031→0.9358; observed right-censored survival
was 36.690→39.483 minutes. In symmetric global-OFF/global-ON selfplay, fewer
eliminations and no decided games indicate passivity/unresolved matches, **not
proof of greater relative winning skill** or uncensored life expectancy.

City completed HF/IX/Starport by the observed end, all three difficulties:

| Research option | Houses per arm | OFF HF/IX/SP | ON HF/IX/SP |
| --- | ---: | --- | --- |
| OFF | 192 | 101 / 55 / 168 | 122 / 27 / 165 |
| ON | 48 | 24 / 23 / 36 | 20 / 6 / 37 |

Paired-achiever HF/IX/SP minutes were 15.886/24.853/21.829→
26.921/29.442/22.620 with research OFF (89/16/145 paired achievers), and
13.641/26.150/20.887→32.416/35.167/23.976 with research ON (17/6/28).
These are selected timing contrasts, not population completion-time effects;
death/admin censoring and achievement bounds remain in the companion tables.
All production cohorts began at T9, so research acquisition is unmeasured.

For City/research-OFF, accepted production/economy/military quotes were
1.014/4.092/3.493M→0.798/3.477/2.622M; accepted cheap quotes **rose**
0.831M→1.143M. Actual physical payments were 9.221M→7.400M; gross spice
5.712M→5.387M and gross tax 3.790M→3.037M. Quote classes are commitments,
not allocated income or physical-payment buckets; cheap/tech/city overlays
are not additive. The largest ON cheap path was `existing_army_composition`,
1,018,900 credits over 5,870 City/research-OFF orders. At the 40-minute H/B
checkpoint, 114 paired-alive City/research-OFF armies grew 32.95→41.12 objects
but value fell 8,409→8,069. Mean price was 215.89→181.86 in the separately
conditioned 106 paired nonempty armies, not a ratio of those aggregate means:
more cheap volume did not satisfy the requested higher-value mix.

The four-minute diagnostic has 141 candidate groups and six exact higher-tech
not-ordered/diversion examples. Windows overlap and flags are planner
observations; do not sum their cheap quotes, infer independent trials or treat
full-quote coverage as an installment admission requirement. Actual pending
tech and final selected capital are separate fields; only explicit raw order
pointers join an order to its capital decision.

Evidence: `../outputs/tech-saving-20261105/confirmation-analysis-8a89f35d/`.
`compact-summary.txt`, `primary-native-outcomes-summary.txt` and
`population-timing-supplement.txt` give compact tables; the per-house checkpoint,
composition, timing/censoring, spend, every-cheap-order and four-minute CSVs
retain full provenance. Historical T/F ablations remain separate: supplier
TrooperSupport missing-SP was 15/109→31/111 (paired delta +0.0983,
95% [+0.0385,+0.1624]); the separate powered-classic seven-seed FundedClassic
trial was 0W/4L/3 time limits→7W/0L/0. These are not effects estimated by this
fixed-T/F Simple cohort. Historical retained raw subsets lack exact acceptance
vectors/full price coverage and must not be pooled with current observers.

### Earlier rejected Simple cohorts

Two earlier separately pinned canonical 288-game/four-seed Simple cohorts were rejected.
TrooperSupport/FundedClassicProduction were ON in both arms. At `5f2bdb16`, all
six Hard/Brutal mod/research cells had four adverse dispatch and kill-value seed
blocks. City research-OFF achieved HF/IX/Starport by40 minutes in
101/55/168 of192 OFF house histories versus178/77/80 ON; research-ON was
24/23/36 of48 versus42/21/21. Death and administrative censoring are retained.
Paired-achiever research-OFF means were HF15.779→12.545, IX26.353→33.434 and
Starport18.987→31.860 minutes; these are selection-conditioned, not all-house
completion times.

Balanced Hard/Brutal equal-game dispatches were3.5816→1.8793 and kill value
43,309→19,963. Actual live per-object replacement value was299.26→314.11,
conditioned on199/320 paired nonempty measured armies; its paired95% interval
included a decrease. This is not accepted-order mean price. City research-OFF
gross spice fell5.712M→4.071M and gross tax3.790M→0.983M. Accepted P/E/M quotes
were1.014/4.092/3.493M→1.052/2.360/1.743M: spending classes, **not income
allocations**. Physical payments and gross income are reported separately.

PRIMARY native credited wins/house were0.2118→0.0625 and decided-match fraction
0.4583→0.1250; own-house elimination fell0.2969→0.1259 and observed survival
rose36.690→38.365 minutes. Alive histories are right-censored: this is not life
expectancy or proof of strength. These symmetric all-QuantBot global-OFF/global-ON
runs measure match health/decisiveness/passivity, not direct ON-versus-OFF winning
power. The missing-Starport detector latches any eligible alive-with-yard absence
at sampled minute≥20; later port completion does not clear it.

Evidence: `../outputs/tech-saving-20261105/confirmation-analysis-df7da313/` and
`confirmation-analysis-5f2bdb16/`, including canonical house/game outcomes,
paired uncertainty, spend/technology/composition timelines and test addenda.
`df7da313` had152/164 passes (three old-ledger City failures and nine Simple
zero-cash failures); `5f2bdb16` targeted Simple tests had three Dune2R passes and
six City-upgrade/Vanilla-payment-evidence failures. No later source receives
retroactive credit. Research-enabled production cohorts started atT9 and do
not demonstrate research acquisition. Four-minute quote coverage is not a
structure-start affordability condition. The audit keeps unique scenario
`city` and diagnostic `city_spend` columns, exact acceptance contexts, and
historical table-aware provenance.

## Required Research Lab progression

With the match's Research Lab option enabled, the separately hashed
`[Force Concentration] ResearchLabProgression` switch treats the lab and its
research as prerequisites while the opening-fleet or dangerous-crime policy
would otherwise veto technology. Once those gates are satisfied, the existing
ordinary investment budget and research pacing apply; already-working classic
openings do not become a different research rush. The switch defaults ON;
OFF preserves the prior ordinary-match policy. Generated Civilization Wars
retains required progression in either state. `ResearchLab=false` still
disables the AI's lab/research policy altogether.

The AI must have a powered, bootstrapped, positive-income economy and enough
uncommitted cash for the lab/foundation or the complete research price plus
working cash. Current defence emergencies and accepted production commitments
remain protected. It does not wait for the larger opening harvester fleet,
perfect crime coverage or repeated optional factory/city allocations: later
technology may be needed to supply those very workers and facilities.
Required research reserves its purchase before repeatable orders spend it,
and an idle yard tries the first lab after power/storage emergencies.

Build-list prerequisites, placement, foundations, generation and research
payment/timing still use the ordinary engine paths shared with humans.
Queue-inclusive counts prevent duplicate labs across yards. There is no
new saved AI state, save migration, protocol change or random draw.
Easy, Medium, Hard, Brutal, Defend and Builder authority use the same rule;
Fighter-only and switched-off QuantBots remain excluded.

`quantbot_research_lab_<vanilla|dunecity>_<difficulty>` runs a real tech-1
full-build fixture with paired hashed switches, paid placement, normal
level-2 research completion, Builder/Defend and negative legality/funding
controls. `tests/ai/research-lab-continuation.py` audits and resumes an exact
save through commit-pinned cluster units. Its frozen diagnostic input stores
the save's SHA256 and gzip/base64 bytes; measured OFF/ON units reload it
independently, without a diagnostic planning pass changing their starting
state.

`quantbot_research_lab_saved_crime` pins Stefan's original 9883
`notbuildingresearchlab` save (SHA256
`8772302e2199d4de929c1b109f2c3151cc10e651b911358ea862e7c5882ebcb7`)
and its exact Workshop mod
`27a8c8d92876c5001b7744c7252eca5971327edbc17cde5d5249e1d6351fb0f4`.
The verified revision manifest and four historical configuration files are
archived; all 906 files, 34,791,606 bytes, are checked against their hashes.
Unchanged artwork is restored from the source tree into isolated profiles;
the original profile and save are never modified.

At saved cycle 49,399, Ordos and Neutral are Brutal QuantBots with 28,981
and 27,533 credits, positive net tax forecasts of 1,128 and 1,238 per minute,
and 333 and 268 legal lab sites. Neither has an opening-worker deficit.
Four and three dangerous-crime properties alone make the old investment
readiness false despite their nine and eight Police Stations. The paired
three-minute saved-game regression requires that refusal OFF and real lab
placement plus normal level-2 research startup ON. This is separate from
the general tech-1 opening-fleet and full research-completion fixtures.

## Dispatch and the military limit

The default outnumbered threshold is an observed enemy advantage of at least
1.5 times the effective friendly ground force. That blocks a fresh main ground
attack while healthy, deployable military value is below 80% of the configured
`militaryValueLimit`. Reaching 80% removes this particular veto; readiness,
recovery, and emergency base defence still apply.

For an 80,000-credit configured limit, the bypass starts at 64,000 credits.
This denominator stays fixed even when Brutal's unit override permits building
past the normal limit. A zero unit override still means unlimited production.
This feature adds no production cap. A nonpositive military-value limit has no
meaningful percentage reference and does not grant an automatic bypass.

Limit accounting and tactical strength serve different purposes. The bypass
uses actual purchase prices weighted by remaining health, including deployable
combat aircraft. Ground-wave readiness and strength are assessed separately.
Harvesters, transports, inactive cargo, units inside repair yards, and units
reserved for repair do not count as deployable attackers. Human orders take
priority. Enemy assessments use observed hostile teams and exclude neutral
sandworms. Allies contribute only where they can support the assessed fight.

## Retreat and rebuilding

The saved postures are Offensive, Withdrawing, and Recovering. The normal
strategic trigger requires a complete 90-second combat-loss window, at least
1,500 credits lost, losses of at least 20% of the starting deployable force,
confirmed enemy losses below 65% of own losses, and corroborating deterioration
in readiness or front strength. These defaults are tuning parameters, not a
claim that purchase price predicts every unit matchup.

A badly outmatched local wave can withdraw before the whole house meets the
attrition trigger. Serious danger to the main army or production core can
trigger an immediate withdrawal. Ordinary raids and one isolated survivor must
not cause house-wide panic.

While recovering, the bot holds fresh troops at a protected rally and recalls
the tracked wave in bounded batches. Emergency defenders and repair remain
available. Recall uses forced movement in Area Guard: the engine suppresses
new target acquisition during movement and restores local defence on arrival.
The rally belongs to a real defended base cluster. Units use dispersed slots
and an assembly area that grows with the army. Existing movement orders and
path-queue backpressure prevent repeated path submissions. Each call issues at
most 12 recall orders; above 150 queued requests it permits replacements of
pending work and at most two fresh requests, preserving progress under contention.

Recovery releases a rebuilt army after the 25-second dwell when healthy ground
value meets the existing attack threshold (with the 3,000-credit main-wave
floor) and the outnumbered dispatch gate allows it. A true core emergency or
severe local defeat still holds it back. Assembly percentage and a period
without losses no longer block release: scattered reserves and ongoing raids
must not keep a rebuilt army at home indefinitely. Release starts a new
attrition observation window, preserving lifetime loss/kill totals. New losses
can still trigger a later withdrawal.

Custom Hard/Brutal launches every available ground combat unit on Hunt in one
pass, wherever it stands. Existing hunters retain their orders. Human control,
repair/transit and ongoing asset defence retain priority. Fresh recruits gather
for the next viable wave instead of being sent individually. Ordinary scans do
not move hunters back into a formation or replace their target selection.

Hunting standard and elite launchers retain close-range spacing: a visible
ground enemy that can attack them, inside the shorter of its reach plus one and
half the launcher range plus one, triggers a short escape. The launcher keeps
Hunt and its wave membership and can resume firing afterward. Damage from a
distant enemy does not trigger retreat. The scan uses nearby tile occupants;
workers, aircraft, human orders, repair and ordinary posted defence contacts
retain their existing handling.

## Exhausted Custom economies

`[Force Concentration] ExhaustedArmyDispatch=true` is a default-on, hashed
QuantBot switch (`force.exhaustedArmyDispatch`). It removes rebuild-only waits
when a Custom army cannot replenish itself. Setting it to `false` retains the
legacy minimum-wave, repair-yard and repair-reservation behaviour; no seed,
match timeout or victory condition changes with this switch.

The conservative exhaustion gate requires an active, non-builder-only Custom
controller outside a campaign, with the switch enabled, and all of the following:

- No harvestable spice reachable by an owned harvester or refinery/dropoff
  region, including the existing carryall route. A zero periodic resource
  survey is the no-spice observation; a positive survey is not exhaustion merely
  because one field is inaccessible.
- Actual starting and earned credit balances, before rounding, below the
  cheapest enabled, positive-price, current-tech nonambient unit. Existing
  builder offers, including Starport quotes, can lower that conservative funding
  boundary. Cash sufficient for even that cheapest purchase keeps normal pacing.
- No positive cargo in an owned Harvester or Rebel Harvester, including cargo
  still on its way home.
- No possible city tax income: a positive tax rate with either an existing tax
  base or any owned R/C/I lot inhibits the bypass. Developing lots are protected
  even before the census reports taxable population; a currently empty wallet
  is not proof that a city cannot recover.

When these conditions hold, the ordinary Custom whole-army selector may send
its remaining available ground troops below the six-unit/3,000-credit main-wave
floor, and `attack()` does not wait for an unaffordable missing repair yard.
This is not permission to override a tactical withdrawal, emergency defence,
campaign timing or a human command. Easy's existing standing-army share remains
its own launch path.

The same gate releases only AI-owned repair work that cannot finish under the
yard's real payment rule:
`cash < 0.5 + (ceil(missing health)-1) * UNIT_REPAIRCOST`.
`House::payCredits` requires rounded credits of at least one before every debit,
so a sub-half-credit balance cannot pay even an otherwise affordable last point.
The gate makes damaged combat troops available, cancels repair-yard travel and
releases unaffordable AI yard occupants;
the engine's automatic repair decision uses the same owner check so it cannot
immediately rebook them. Human orders, saboteurs, live/base/posted defenders,
colony guards and colonist escorts keep their orders and repair authority.
Campaign repair and dispatch paths are unchanged, even in an empty economy.

## Whole-army base defence

In Custom Hard/Brutal games, a real attack on any owned building, including a
remote R/C/I colony, sends every available ground combat unit to Hunt the
invasion. Damage callbacks and ordinary scans both recognise building attacks;
the scan also handles hostile turrets firing at a forward building. Recovery,
assembly, military strength, the local response radius and proportional quotas
do not withhold the ground army during this emergency. Human commands, the
existing repair reservation policy, carryall pickups, workers, builders,
transports and aircraft retain their authority. Air rescue keeps its existing
specialised response; ordinary harvester raids keep their bounded local response.

Remote defenders travel towards the actual attacker, then let engine Hunt own
combat inside weapon range. Repeated scans retain an existing live base contact,
including when several districts are attacked, instead of restarting travel
orders. A worker raid cannot redirect troops away from that live base response.
Hunting launchers retain their short escape and resume firing afterward. When
the original attacker dies or stops attacking the building, its defence
assignment and forced travel expire on the normal scan.

These mobile Hunt defenders use an invalid guard point in the existing saved
order fields; ordinary posted defenders keep their valid guard point. This
distinction preserves local defence authority without adding serialized state.
Protocol 55 separates peers with different emergency AI behaviour; save format
9852 and observer runtime 7 remain unchanged.

## Batteries and special units

Once two refineries, a heavy factory, and a repair yard exist, important bases
can grow overlapping rocket batteries towards the observed enemy approach.
Coverage demand sizes the goal inside an 8–12 turret band; the band is also the
ceiling. The former open-ended goal (demand plus up to half again for a threat)
bought a rich base 90–100 turrets and, through the wall rule, as many walls, and
lost or stretched `quantbot_all_against_atreides` (see
`docs/quantbot-all-against-atreides.md`). First cover for critical buildings, remote
colonies, economy reserves, power rules, construction exits, and movement
corridors retain priority. Battery construction is spread across passes.
Destroyed hostile buildings count as kills in the attrition trade, at their
purchase price, so assaulting a turret-defended base is not by itself a bad trade.

A city's due, buildable first-cover rocket takes priority over founding its
first WOR for standing trooper support. Extra overlap on already covered core
assets still shares construction with growth; losing a refinery's only
covering turret restores the first-cover priority immediately. The existing
`quantbot_police_budget` and `quantbot_city_growth` probes preserve these
distinctions after core suppliers are present, without disabling
`TrooperSupport` or pre-filling its missing supplier.

In DuneCity Custom Hard/Brutal games, a battery that has no usable free site can
replace an owned residential, commercial or industrial lot on the observed
enemy side of its construction colony. Free ground wins, including ground that
opens while construction is underway. Lower displacement cost wins among lots:
less development, lower land value and weaker demand are preferred.

Clearance leaves at least three lots of the displaced type and six total lots
within two rocket-turret weapon ranges of the yard. A distant colony cannot
supply this floor. Hospitals and churches are protected. Modest lots (density
at most one, with at most eight residents) remain eligible during construction;
more developed lots must account for at most 20% of the local population or jobs
of their type. Roads, factory exits, neighbouring zone frontage, movement
corridors, turret spacing and the front quota are checked on the map after the
proposed clearance. Human/shared houses, essential infrastructure and foreign
lots cannot be cleared by this fallback.

Queue acceptance leaves the lot intact. Immediately before deletion the yard
must be active, owned, holding finished material and still have a legal,
needed battery project. With concrete off, deletion and placement of the
finished turret occur in the same build call. With concrete required, deletion
occurs when the completed one-tile foundation is placed, with the turret still
queued and all queued costs funded. Cash shortfalls retain the lot and ready
material; invalid projects cancel without deleting the lot. After foundation
placement, destruction of the yard or cancellation can still prevent the
turret from completing. This construction-time exposure is not an atomic
guarantee of a completed turret under attack.

Execution clears at most one lot per build pass across every yard, regardless
of how many earlier reservations are ready. Clearance and free-site caches are
invalidated by geometry and planning-builder changes. The local economic census
runs once per search, rather than once per candidate tile. No additional state
is serialized: save format remains 9852; protocol 51 separates peers whose AI
would preserve the lots.

Devastators, Sonic Tanks, and Deviators retain their shared outer production
budget. Selection within that group uses per-type recorded return/loss evidence
and an exploration prior, so candidate ordering alone does not favour the first
affordable type. The strength model does not reuse those scores as unit damage
weights.

## Verification and limits

`quantbot_army_recovery` exercises the real-engine integration alongside unit
policy tests and existing custom/campaign/production probes. Important checks
include the 80% boundary, minor raids, recovery/resumption, old-save migration,
ordinary save and scoped observer AI continuation, legal battery sites, and actual
movement of a large recalled army. Changes to saved state and multiplayer
behaviour require explicit format/protocol gates.

`quantbot_custom_attack` contains same-runtime OFF/ON/OFF exhausted-economy
coverage through the actual `exhaustedArmyEconomy()` predicate,
`declinesUnfundedRepair()`/repair reservation owner, `launchGroundHunt()` and
`attack()` paths. Its controls retain legacy holds for reachable spice, exact
cheapest-unit funding (with a fractional-shortfall boundary), pending harvester
cargo, city tax income and developing lots. It compares actual campaign orders
across switch states and protects human, defence and saboteur orders. Actual
repair-yard trips and bay occupants exercise AI-only release, protected human
and defender occupants, and the engine's automatic-repair no-rebooking rule.
The single-bay traveller controls establish one real booking at a time.
The actual repair-yard update proves both sides of the rounded-credit boundary
for one and five missing health points.
These fixture assertions describe coverage, not an unrun full-match or win-rate
certification.

`quantbot_whole_army_dispatch` checks whole-army building defence for both Hard
and Brutal in all three postures, including a reserve 47 tiles away, remote
colonies, two simultaneous attacks, hostile turret fire, actual movement and
damage, repeated scans and contact expiry. It retains human, repair, transport,
worker and aircraft boundaries and the existing bounded harvester response.
`quantbot_launcher_kiting` checks actual spacing and subsequent damage during
the base Hunt response, including a simultaneous worker raid, while retaining
ordinary local defence authority.

`quantbot_battery_clearance` uses real production and engine frames with concrete
on and off. It checks completed turrets, intact lots before material is ready,
cancellation, destroyed yards, changed occupancy, free-site retargeting, two
colonies, shared-human authority, local economic floors, all R/C/I types, and
the one-clearance limit across two yards holding real finished turrets. The
funding check includes a one-credit shortfall and exact funding after queued
costs have been withheld. Ready material also refuses changed occupancy, shared
human ownership and lost power. One/four actual path workers produce the same
clearance sites, timings in simulation frames, placements and zone counts. Bot-stream reload checks derived site selection;
this is not a new full-world save/observer certification. The progress fixture
supplies stable cash, power and an observed enemy approach without interference.

Purchase price and health provide a cheap, deterministic strength proxy. They
do not model every counter, splash hit, terrain bottleneck, or human manoeuvre.
Confirmed hostile fatalities use the victim's original-house purchase price,
independently of telemetry. Friendly fire, support units, repeated damage on a
dead object, and deviation rewards without a hostile fatality do not credit the
kill ledger. The existing own-loss callback supplies only the item type, so
borrowed units' own-loss cost remains an approximation using this house's price.
Full-match results must be read with these limits; fixture success does not
establish improved win rate.

The continuation probe starts from a canonical ordinary load and crosses the
unit/build phase boundary. It compares saved bot bytes, all unit orders, every
object's physical digest, RNG and the tested house's exact credit balances for
100 frames, and checks fresh survey reconstruction before both build passes.
Repeated ordinary loads compare complete house-inclusive digests. An observed
one-credit difference in another house's pre-existing city checkpoint accounting
means this is not certification of general full-world spectator parity.
