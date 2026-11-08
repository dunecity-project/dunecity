# AI scenario matrix

Stefan, 2026-10-18: "you need to increase the diversity of game you test - construction yard, prebuilt
base, dunecity vs vanilla, all against atreides VANILLA, some campaign games to make sure campaigns
don't break. e.g. a brutal should defeat a medium quantbot on the enemy. a hard or maybe even a medium
should beat an easy. test different levels from 1-9 ... a few of the MP popular maps for 1v1, 2v2, 3v3".

Until 1.0.847 almost every AI measurement was a 1v1 or All against Atreides. The matrix plays many
kinds of game with one driver, `tests/ai/scenario-matrix.py`, and prints one summary table.

## How a game is played

Every game is a real match played headless to its end by the balance probe:
`run-campaign-balance.py --scenario` (probe code `tests/ai/scenario-match.inc`, dispatched from
`campaign-balance-probe.inc`). Nothing in the shipped game changes.

- **Matches** (`--scenario-sides house:team:class,...` with `--custom-map FILE` or
  `--skirmish key=value,...`): every house gets its own team number and controller class (QuantBot
  `qBotEasy..qBotBrutal`, AI Player `AIPlayerEasy..Hard`); a local spectator watches. The game runs until
  one team is left (`House::isAlive`, the engine's defeat rule) or the time limit. `--skirmish` generates
  a Random Skirmish *multiplayer* map (one start per side, every start alike: `size`, `base`
  none = MCV only / `cy` / outpost / small / medium / large / fortress, `army` = `kArmyBudgets` index
  0 none, 2 = 2,500 small, 5 = 20,000 large, `city`, `seed`, `tech`, `rock`, `spice`, `credits`,
  `symmetric`); its mode (vanilla or DuneCity city simulation) follows `--mod` as in the menu.
  `base=cy` is a Construction Yard only start: each start's MCV is deployed where the map put it before
  the first cycle (`SCENARIO_CY_START`), no army, the menu's default 3,000 credits.
- **Campaign missions** (no sides; `--house`, `--level`, optional `--mission`): the full-tier
  vanilla gate uses a sole Brutal QuantBot on the player house, authored scenario-settings
  `CampaignAIPlayer` opponents and `--campaign-ai-only`. No human is registered or issues orders.
  A detached, command-disabled UI identity anchors the campaign's player house, without enabling
  observer match-end semantics. The real mission's win/lose conditions (`Game::finished`) decide
  conquest, loss and spice-quota victory; no forced conquest or scripted orders substitute for them.
  DuneCity partner-difficulty diagnostics and the existing smoke campaign cells retain their prior setup.
- Game rules come from the mod (`ModManager::loadEffectiveGameOptions`) as a new Custom Game takes them;
  `--game-options` (All against Atreides: Stefan's own options) applies on top.
- Once a game minute every house is sampled, and its **sortie** measured: combat ground units more than
  20 tiles (Chebyshev) from every structure of their house; a minute with 3 or more away is a sortie
  minute. Scenario triggers (reinforcements, time-outs) are counted as they fire.
- The end logs `SCENARIO_HOUSE` per house (team, controllers, alive, elimination time, kill/loss value,
  structures, army, peak units away, sortie minutes) and `SCENARIO_RESULT` (outcome, winning team, game
  minutes, triggers and how many fired, win/lose flags, quota, city population),
  then the final state digest (`PROBE_DIGEST ... final=1`).

For a natural-duration diagnostic, `--minutes 120` overrides only the selected
run's cell limits. It does not change the ordinary matrix limits or winner
assertions; use a separate results directory for the extended observation.
For matched fresh-seed campaign screens, `--campaign-seeds 1280000 1280001`
replaces the fixed seeds for each selected campaign mission (duplicate seeds are
played once). `--cases` can restrict the missions. The default fixed 66-game gate,
the idle-human AI-support seating, and every non-campaign seed remain unchanged.
Use separate result directories for each arm and keep observer flags identical.

Campaign support field experiment: `[Force Concentration] CampaignHomeFields`
is hashed and defaults to 0 (OFF). Level 1 rejects spice jobs closer to any
hostile base centre than the support AI's own base; level 2 leaves a four-tile
inward margin. Base centres use live deterministic structures, ignoring fog.
Existing immediate-danger, route, unloading and ownership rules remain.
It does not increase refinery or worker targets and is inert without spice,
outside classic campaign human support, or at level 0. Styles:
`campaign-home-fields-off`, `campaign-home-fields-home`, `campaign-home-fields-margin`.
The hypothesis is worker survival without drawing the whole opening army into
an enemy base to rescue a worker; measure worker deaths and defence-response
locations as well as delivered spice, army, idle cash and wins.


### Genuine exhaustion regression

The mixed-difficulty vanilla FFA keeps seed **90866** as an explicit exhaustion
check, not a winner check. At runtime `cf05f6b7` it still had two surviving houses
after **120 natural game minutes** (receipt
`20261105T010832-suite-90866-natural-120-1acf45`, digest `32f5b20f27feaa01`).
Accepted switch arms changed which side could finish: launcher-kite OFF gave QM
a 46.96-minute win, while deviated-harvester early-return OFF gave QB a
35.94-minute win. These are different accepted trajectories, not evidence for
disabling either default.

The case passes only with a complete per-house engine audit: the production
`exhaustedArmyEconomy()` predicate must prove cash below the cheapest enabled
unit, no reachable harvestable spice, no pending cargo or possible tax income;
there must be no idle raid-ready combat units and no unaffordable offensive
repair trips or contained repair holds. Existing damaged-air, human, defence
and saboteur authority remains intact. Missing audit evidence fails the case.
The next seed from the same generator, **91875**, is also in smoke and keeps the
unchanged FFA-must-finish, elimination and sortie assertions. Every other seed
keeps its existing completion requirement; draw scoring is unchanged.

The driver writes `games.csv` (one row per game; the win type of a campaign win: conquest or
quota) and `houses.csv` (one row per house, with its `match_score` placement score: teams are entrants,
a time-out is a negative draw). Games already in `games.csv` are skipped; failed games are retried on the
next run; a crashed game's last log lines go to `failed-*.txt`. A smoke timeout keeps its
complete engine log in `timeout-*.log`, including the final unit/target, exact cargo,
repair-owner and containment audits, before ordinary game-directory cleanup.
The timeout still fails the same winner assertion.
With `--attack-events`, raw logs for every result instead live under
`diagnostics/<game>/run.log`, so same-source A/B runs can compare their complete
`PROBE_DIGEST` prefix before the first changed endgame decision.

### Source-separated economy curves

For a real Random Skirmish city game, `tests/ai/dunecity-audit.py --attack-events`
retains once-per-minute credit balances and the existing accounting ledger in
`samples.csv`, including starting cash remaining, refined-spice and tax gross
receipts, separate spice/city balances, storage capacity, the Construction
Yards' share of that capacity, police funding, military target and unit-limit
state. Spending, power, police, refunds and storage losses are cumulative
ledger fields; `ledger_enabled=1` identifies a captured ledger (without
`--attack-events` those fields are unavailable, not measured zero).

The source counters are read-only in `DUNECITY_ECONOMY` probe rows. A Yard's
storage is capacity, never income. Pair these rows with the existing queue,
upgrade, held-front, growth and attack samples before calling a rising balance
an AI spending stall. Use `--style police-recovery-off` and
`--style police-recovery-on` for a same-build A/B of the default-on hashed
`[General Behavior] PoliceFundingRecovery` switch. No income multiplier,
starting grant or balance constant changes with that switch.


## The matrix

Controller codes: QE/QM/QH/QB = QuantBot Easy/Medium/Hard/Brutal, AE/AM/AH = AI Player Easy/Medium/Hard.
Houses are Harkonnen, Atreides, Ordos, Sardaukar, Fremen, Mercenary, Rebels, Neutral (first N), rotated
by the seed's index so every controller plays every house; map-named houses (All against Atreides, the
6P maps) stay fixed.

| Category | Cells | What is checked |
|---|---|---|
| `skirmish` | 13 Random Skirmish cells: 1v1, 3-8 player FFA, 2v2, 3v3, 4v4; MCV only, small, medium and fortress bases; no, small and large armies; 8 vanilla, 5 DuneCity; QuantBot difficulties mixed with AI Player; 4 seeds each | decided, eliminations, idle armies; team cells with a stronger team must be won by it; placement score per controller (QB > QH > QM > QE) |
| `cy` | 7 Construction Yard only cells: 1v1 QH v AH, QH v QM, QB v QE (40 min), QH mirror; 4P FFA vanilla and DuneCity; 2v2 mirror | as above |
| `aaa` | All against Atreides: vanilla Brutal v 4 AI Player Hard (6 fixed seeds, win within **50 min**); DuneCity (6 seeds, win within its 60-min cap); saved Dune2R rules (7 seeds, unchanged 40-min gate); reverse vanilla Atreides Medium v 4 QuantBot Hard (6 seeds, must lose) | expected winner |
| `campaign` | Vanilla: every real mission ID **1–22**, Atreides/Harkonnen/Ordos, one fixed seed per mission (**66 games**); sole Brutal player against authored `CampaignAIPlayer`. DuneCity retains Hard/Medium/Easy partner diagnostics, now also covering all 22 IDs | vanilla and Hard-partner missions must be won by their native conditions; conquest/quota and triggers reported |
| `ladder` | Every adjacent pair (QB v QH, QH v QM, QM v QE) plus QB v QM and QH v QE. Long maps (the decision): Random Skirmish 128x128 (75 min) and 160x160 (90 min), vanilla and DuneCity, medium bases, no army, 18 seeds each played twice with the houses swapped (`#swap`; each side keeps its start): 144 games per pair. Short maps (reported): 96x96 small bases vanilla and DuneCity, Habbanya-Penny, a vanilla CY 96x96 start, 9 seeds x both houses: 72 per pair | the stronger QuantBot's placement score interval above 0 over the long maps (the interval resamples seeds); game length per cell |
| `mp` | The most-played bundled maps (dunelegacy.com usage statistics, all time, 2026-10-18): 1v1 Habbanya-Penny (364 games), Tuono-Orac (104), Sihaya-Chvrches (82); 2v2 Sihaya-Toto (113), Deserted (105), 4P DuneCity (68, DuneCity mod); 3v3 Sihaya-Lenka (536), Full Wormage, Alkozeltser 4 (265, 256x256: 2 seeds, 80 min). QH teams v QH teams (4 seeds), and QH teams v AH teams on the 2v2/3v3 maps (must win) | decided, idle armies, expected winner |

Time limits: 60 game minutes (7-8 player and 256x256 maps 80; All against Atreides vanilla 51,
with victory required by 50; saved Dune2R remains 40; campaign 80).

### Flags

The report lists, per cell, the winners by team and controllers, the share decided, the expectation
check (k/n with a Wilson 95% interval) and these flags, and collects every problem at the end:

- **crash**: the probe failed or left no result;
- **time-out**: two or more teams alive at the limit; **nobody eliminated** in a game of 3+ houses;
- **idle**: a house that lasted 20+ game minutes without a single sortie minute or kill;
- **campaign not finished / lost** (the vanilla Brutal player and DuneCity Hard partner must win every mission);
- **inverted**: an expectation's interval wholly below 50%, or the ladder's strong side scoring below 0.

## Tiers

**Smoke** (`ctest -R quantbot_scenario_matrix`, mandatory with `quantbot_all_against_atreides`):

| Game | Pass when |
|---|---|
| skirmish 4P FFA, 128x128, medium bases, small armies, vanilla, QB/QH/QM/QE | someone eliminated, nobody idle, decided |
| cy 1v1 QB v QE, 64x64, DuneCity, played twice | QB wins within 40 min; the replay's final digest equals the first game's |
| aaa reverse: Atreides QM v 4 QH (first quantbot_all_against_atreides seed) | the enemies win |
| ladder QH v QE, vanilla Random Skirmish 96x96, both sides | QH wins both |
| mp 2v2 Sihaya-Toto, QH+QH v QH+QH | decided, two or more houses sortie |
| campaign Atreides L1 (vanilla), Ordos L4 (vanilla), Harkonnen L9 (DuneCity), Hard partner | won |

Every game must also finish without a crash. 10 games, three at a time.

**Full** (`--tier full`): the whole matrix; `--list` prints the current schedule and counts.

## Running

### Complete vanilla campaign recovery gate

`run-campaign-matrix.py` is a thin entry point to the existing `campaign` category,
not a separate campaign convention. It selects `player-brutal-v-campaign-ai/vanilla`,
enforces native wins (`--check`), forwards the scenario-matrix options and writes the
same `games.csv` / `houses.csv` evidence.

```sh
python3 tests/ai/run-campaign-matrix.py --list
python3 tests/ai/run-campaign-matrix.py --list --shard 1/4
dunecity-queue submit --cwd /Users/stefan/Documents/projects/dunecity-feature-batch --rev COMMIT --tag campaign-recovery -- \
  python3 tests/ai/scenario-matrix.py --tier full --categories campaign \
  --cells '^campaign/player-brutal-v-campaign-ai/vanilla$' --check --build-dir build-int --results RESULTS
```

Coverage per house: L1=mission 1; L2=2/3/4; L3=5/6/7; L4=8/9/10; L5=11/12/13;
L6=14/15/16; L7=17/18/19; L8=20/21; L9=22. The previous first/second-only
enumeration skipped **4, 7, 10, 13, 16 and 19** in every house. Mission 22 is the
final mission, not another L8 branch. Each mission keeps its deterministic
mission-specific seed from `seeds_for`; `--list` exposes those seeds.
`--shard K/N` partitions the same ordered 66-game plan without replacing seeds or
opponents. Merge shard directories with the existing `--report DIR1 DIR2 ...`.
The default 80-minute cap is not a victory condition: unfinished quotas and other
unfinished missions fail. Historical smoke expectations are unchanged.

**The campaign gate is the AI-support setup** (Stefan, 2026-11-08: QuantBot always
plays the campaign as AI support beside a human). The default schedule gives the
selected house an idle human seat plus the Brutal QuantBot support, so the AI has to
carry both roles; IDs end `#human-partner`. `--campaign-ai-only` keeps the old
no-human diagnostic and is never gate evidence.

`[Force Concentration] CampaignPlayerHouse` (default off) identifies the selected
campaign house from `GameInitSettings`, independently of registered human seats.
ON gives its sole QuantBot the same Custom economic planning, player-side
readiness and attacks as an existing human partner; authored enemy-house roles
and human-partner behaviour are unchanged. OFF retains the old human-presence
role test, including its AI-only campaign opening stall. It only matters for
`--campaign-ai-only`; the AI-support gate is digest-identical either way, so the
switch is parked off.

Measured on `08eafddc` (2026-11-08), `~/Documents/projects/outputs/ai-recovery-20261108/fixes/CampaignRole/`:
AI-only **4W/11L/51T → 56W/10L/0T**, paired score Δ **+1.00 [95% CI +0.81,+1.19]**.
All four prior wins remain wins; seven time-outs become losses, so the role fix alone
does not pass the all-missions-win gate. Its 66 final digests match the original
human-partner arm. That arm remains **56W/10L/0T**, with all 66 digests unchanged;
vanilla AAA remains **2/2** at **36.47/34.54 min**, also digest-identical.
OFF equivalence is recorded for 17 AI-only and 22 partner missions plus both AAA seeds;
the redundant remaining OFF games were cancelled with integrator approval. The switch
remains default off pending owner review.

The independent, hashed `[Force Concentration]` candidates
`CampaignMilitaryOpening` and `CampaignAdaptiveReadiness` originally defaulted off.
The first establishes one available combat producer after the first refinery,
before repeat refinery/storage/Starport expansion, preserving power and live
air-defence priority. It walks the actual prerequisite tree and saves for the
producer rather than spending its price on another refinery. The second applies
the partner readiness scale (1.25x the strongest opposing **team's** army,
floor 2,000 and ceiling 75% of its military limit) from the first wave, capped
by the configured threshold. It does not mistake one house of an enemy alliance
for the entire hostile army; OFF keeps the old post-fifteen-minute per-house rule.
Authored campaign opponents and noncampaign skirmishes are unchanged.
Use `campaign-opening-off` / `campaign-opening-on` for the paired combination;
`campaign-military-opening-on` and `campaign-adaptive-readiness-on` isolate each
change. These styles do not enable `CampaignPlayerHouse`: add that switch only
when comparing the AI-only role-fixed arm.

`CampaignThreatProduction` was initially a separate default-off hashed candidate for the
remaining funding hold: while a visible enemy is actually firing at the
campaign player's buildings or harvesters, ground producers plan before
economic expansion and can spend economic savings on replacements. A heavy
factory that can afford a military vehicle does not choose another worker in
that pass. Existing queued obligations, prices, unit budgets and unit limits
remain enforced; without a live attack, normal economic priorities resume.
`campaign-threat-production-on` isolates it; `campaign-opening-funded-on`
enables all three opening/readiness/funding candidates.

Measured on `e26bd898` (2026-11-08), all 66 human-partner missions:

| Opening switches | Won | Lost | Paired score difference [95% CI] |
|---|---:|---:|---|
| Control, all off | 56 | 10 | — |
| Military opening | 55 | 11 | −0.03 [−0.24,+0.18] |
| Military opening + threat production | 60 | 6 | +0.12 [−0.09,+0.33] |
| All three | 61 | 5 | +0.15 [−0.06,+0.36] |

**Those original combinations were rejected for default adoption.** All three
rescue nine original losses (Atreides 2/5/6/9/20, Harkonnen 21/22, Ordos 5/6),
but lose four prior wins (Atreides 16/21, Ordos 2/21); Ordos 4 still loses.
The strict same-build vanilla sample (`7c2ed4dd`, 48 cases × two seats) matches
all 96 OFF/ON digests: score +0.203 in both arms, zero difference in every
factor stratum. Vanilla AAA remains 2/2 at 36.47/34.54 minutes, digest-identical
to control; four OFF campaign checks also retain the control digests.

Regression telemetry shows an early Light Factory occupying capital while idle
until minutes 8–12 (Ordos 2 never orders a military unit), delaying workers,
imports and replacement strength. A single bounded attempt (`31f12e62`) reused
the capital planner's delivered-worker seed before that factory. It won 7/13:
only Atreides 16 was repaired; Atreides 21 and Ordos 2/21 still lost, and rescued
Atreides 20 plus Ordos 5/6 became losses. AAA remained 2/2. The attempt was
rejected and its source edit reverted, not promoted or tuned further.

Evidence: `~/Documents/projects/outputs/ai-recovery-20261108/fixes/CampaignOpening/`
(`paired-results.json`, `paired-campaign-cases.csv`, `*-comparison.txt`,
`bounded-seed13/`, `bounded-seed-aaa2/`). These results do not pass the
all-missions-win recovery gate.

The next campaign tuning round isolated the cheap ablations on `fa5228ac`,
paired against a fresh 66-mission control on `c598fc46`. The fresh OFF arm
matches all 66 original control digests:

| Campaign switches, no military opening | Won | Lost | New control losses | Rescued |
|---|---:|---:|---:|---|
| Threat production only | 56 | 10 | 0 | — |
| Adaptive readiness only | 56 | 10 | 0 | — |
| Readiness + threat production | 57 | 9 | 0 | Harkonnen 21 (31.85 min) |

Readiness plus threat is the first clean campaign improvement: it preserves all
56 control wins. Its paired score difference is +0.0303, with normal 95% interval
[-0.0291,+0.0897], so the one-mission gain is not an established aggregate
statistical improvement. Neither isolated switch rescues Harkonnen 21. AAA2
passes with both original control digests. These isolated arms are not the
adopted defaults; the five-switch combination below is the accepted set.

Independent hashed light-production candidates investigated the observed idle
factory. `CampaignFundedLightOpening` leaves normal yard economy
active until the opening Light Factory, a legal first vehicle, and the
producer's protected cash can all be funded together; it does not wait for a
particular delivered-worker count. Its completed paired 66-mission run is
**58W/8L**, preserving all 56 control wins and rescuing Harkonnen 21 plus Ordos 6.
The isolated real-price-entry experiment (`CampaignAffordableLight`) still had
four regressions and an Ordos 4 loss on the weak 14; its switch/code were
removed, with the result and immutable test pin retained in the ledger.

Ordos 4 telemetry isolates another hold: at 2.55–9.13 min, its 100% Raider mix
targets exactly the existing four Raiders' 600 credits. Composition planning
still subtracts the 2,000-credit economy buffer after live-threat producer
admission releases it. `CampaignLightWorkingCash` tests matching light growth
to genuinely uncommitted cash before heavy-factory technology, while preserving
strategic/civic protection, prices, commitments, army budgets and unit limits.
Combining it with the funded factory gate (`7b3001e4`,
`campaign-light-funded-working-on`) achieves **61W/5L**, preserving every control
win and rescuing Atreides 2/5, Harkonnen 21, and Ordos 5/6. Paired score difference
is +0.1515, normal 95% interval [+0.0229,+0.2802]. AAA2 passes both original
digests, within 50 minutes. The remaining losses are Atreides 6/9/20, Harkonnen
22, and Ordos 4. Aggregate campaign wins exceed the 1.0.796/1.0.807 references
(56/59), but those references win A6/A9 (both) and A20/H22 (1.0.807); preserving
those historical mission wins is not yet established.

The accepted round defaults are `CampaignMilitaryOpening`,
`CampaignAdaptiveReadiness`, `CampaignThreatProduction`,
`CampaignFundedLightOpening`, `CampaignLightWorkingCash`, and
`CampaignLightIncomeSeed`, together with Main's protocol/save version bump.
All six are confined to non-city campaigns with human support; AdaptiveReadiness
explicitly excludes city simulation. `CampaignPlayerHouse` remains OFF.
Constructor, shipped INI and registry defaults agree. `campaign-defaults-on`
selects all six; `campaign-income-seed-off` retains the five-switch diagnostic;
`campaign-opening-off` explicitly disables all six for original-control digest
comparison. Historical ablation styles override the adopted settings explicitly.

Working cash without the factory gate starts O4 replacement production at
2.92 min and buys 36 Raiders, but never dispatches: the one-worker army grows
slower than its readiness requirement (3,990 versus 5,000 at 19.21 min), then
loses at 40.51 min. `CampaignLightIncomeSeed` protects only the second refinery's
founded quote from working-cash light production until that refinery is queued.
It does not wait for worker delivery or change factory admission.
Its diagnostic style `campaign-light-income-seed-on` (funded factory gate OFF)
wins O4 by conquest at **17.71 min**, digest `f8d68a18d4f2a43a`, on `a764605b`.
The separate full-set arm on `98ddf9d7` achieves **62W/4L**: it preserves all
61 prior wins and adds A6 (21.03 min), but O4 still loses. Main accepted Seed ON
under the no-control-win-lost plus gain rule. All three 68-game confirmation
arms pass AAA2 with the original digests; defaults reproduce 66/66 prior61
campaign digests, and explicit all-OFF reproduces 66/66 original control digests.
Remaining losses are **A9/A20/H22/O4**. Full paired rows and economy diagnostics
are in `~/Documents/projects/outputs/ai-tuning-20261108/CampaignLane/ledger.md`.


For the recovery skirmish **sample** (48 balanced cases per mod, both candidate
seats, not a full cross product), use `dunecity-audit.py --mode sample --mod both`;
see [sampler factors and research verification](quantbot-dunecity-audit.md#balanced-recovery-skirmish-sampler).

For a sampler candidate/control comparison, use **`--candidate-style NAME`**,
not `--style`: only the sampled candidate seat receives that INI; opponents
retain their native QuantBot config even when they are Brutal too. Both seat
assignments keep the same maps, seeds, rosters and IDs. `games.csv` records the
candidate style and every house's effective config hash. Missing candidate
application or a changed opponent hash is an error, not a measured game.
The transport reuses the probe's per-house config inputs with one constant
overlay for the whole game; no genome search or changing phases is involved.
`--style` remains available for explicitly **all-QuantBot** experiments and is
mutually exclusive with `--candidate-style`. Campaign/AAA `--style` comparisons
do not change their non-QuantBot opponents.

For several variations in the **same** game, use
`--seat-styles control,A,B,C,D` (play-style names without `.ini`). `control`
means native config, not another experimental INI. Each cohort rotates once
through every house/start and controller tier on an unchanged map. Spare seats
are native controls. If the list exceeds the game's capacity, consecutive
cohorts contain at most seats-minus-one variants plus control; 1v1 therefore
plays each variant directly against control. A sampler uses each original
roster once per rotation instead of duplicating the singled-out candidate
swap. An all-control list retains the ordinary unexpanded schedule and IDs.
Sharding occurs after cohort expansion.

`games.csv` retains the house/style mapping, cohort and rotation;
`houses.csv` identifies each seat's style. Reports give each variant's placement
score, wins, observed survival and better/tied/worse finishing order against
opposing controls **in the same game**. Allied controls are excluded from that
head-to-head count. Different cohorts and multiple controls are not independent
paired trials or evidence for enabling a default. Promote a promising variant
to the unchanged candidate-only full-mode and cross-mode gates.

The vanilla FFA round isolates four default-off hashed `[Force Concentration]`
switches: `VanillaFfaWeakestTarget` uses the existing weakest-rival hunt even
before leading; `VanillaFfaNearbyTarget` adds 200 credits per tile to remembered
army/turret strength; `VanillaFfaReserve` retains at least 25% of available wave
value nearest the protected rally; `VanillaFfaFullWaves` tests 100% readiness
instead of the smaller strategy waves. They require vanilla Custom mode and at
least two hostile teams in the original roster; 1v1, 2v2, campaigns and cities
keep their existing behavior. Human orders and live defence ownership remain
authoritative; the reserve is bypassed when the economy is exhausted.
Use the `vanilla-ffa-{weakest,nearby,reserve,full-waves}-on` styles, native
`control` in shared games, and `vanilla-ffa-off` for candidate-only OFF parity.

Round2 adds three independent default-off, hashed and registry-listed candidates:
`VanillaFfaStarportAfterWave` defers the first optional port until a ground wave
dispatches, once four real workers, unload capacity and a legal local Heavy
Factory exist. Its first-wave Repair Yard remains funded at its actual price
and legal foundation; missing/unplaceable recovery capital releases the deferral.
`VanillaFfaThreatProduction` puts legal ground replacements before uncommitted
optional-growth savings only under actual visible ground fire and below ordinary
army readiness. Both require remaining spice, a yard and the worker seed, and
leave space/yard recovery, accepted orders and required defence/transport intact.
`VanillaFfaSingleFront` sends fresh reinforcements to the existing live army's
known, reachable structure objective. It reuses checkpointed wave state and
existing recovery/tracking, with no new tactical cache; local combat and defence
still override transit. It is inactive after spice exhaustion. Their styles are
`vanilla-ffa-{starport-after-wave,threat-production,single-front}-on`, with
`vanilla-ffa-round2-off` for explicit OFF parity; they do not reset earlier flags.

The minute audit appends `refineries` and `power_required` after all existing log
and CSV fields without changing their order or parser. Legacy captures leave the
new columns blank. Raw `power < need` is not a powered-mode economy penalty when
`power_required=0`. Use `--attack-events` only for diagnostic captures that need
spending, first dispatch and the candidate activation/front-continuation records.

Round3 isolates the observed winning-economy gap with two more default-off,
hashed, registry-listed switches (both spice-backed vanilla multi-enemy only).
`VanillaFfaLightWorkingCash` removes the flat700-credit light-factory entry hurdle
below ordinary readiness after four real workers. Actual quotes, protected cash,
unit limits and the existing mix remain authoritative; it does not give cheap
entry to optional factory upgrades. `VanillaFfaFundedTech` defers optional
IX/Palace orders and their savings until up to eight sustainable real workers
and ordinary army readiness, while a legal local Heavy Factory can supply the
army without them. The first port, transport, repair, accepted commitments and
space/yard recovery keep their existing rules. Styles:
`vanilla-ffa-light-working-cash-on`, `vanilla-ffa-funded-tech-on`,
`vanilla-ffa-round3-off`. New capital-plan fields and `front_continued` are built
only in their enabled vanilla switch scope, not for OFF/campaign telemetry.

The scoped follow-up retains the hashed, default-OFF keys
`VanillaFfaThreatProduction` and `VanillaFfaFundedTech`, but activates them only
in vanilla multi-enemy skirmishes with **at least five original occupied player
houses**. Original setup, not current survivors, determines match size; no
mission, map-name or seed condition participates. Four-player FFA, 1v1, teams,
other mods and campaigns remain inert. The existing nine-switch round-trip/hash
fixture still covers both keys. Acceptance uses fresh sample seed9202, both arms
observer-off, requiring a gain inside the five-plus-player stratum and identical
native digests in every smaller match. The earlier observed9101 screen is
discovery evidence, not the fresh-seed acceptance gate.

For outcome/digest promotion gates use **`--observer-off`** in both arms.
It removes `BALANCE_DUNECITY_AUDIT`, keeps real elimination samples/final results,
and leaves power/refinery/audit-dependent metrics unmeasured rather than inventing
zeros. `games.csv` appends the verified `observer_mode` receipt; comparison rejects
mixed or missing modes and resume requires a new directory when the mode changes.
Do not compare an observed screen with an observer-off gate. Any screen winner
still needs the full candidate-only vanilla96 against observer-off native on the
same pin, plus AAA2 and the cross-mode/default gates, before proposing ON.

```sh
# Real games run only through the committed cluster queue, never a local mini batch.
dunecity-queue submit --cwd /Users/stefan/Documents/projects/dunecity-feature-batch --rev COMMIT --tag sm-smoke -- \
  python3 tests/ai/scenario-matrix.py --tier smoke --jobs 3 --build-dir build-int --results RESULTS
# Full tier: change --tier smoke to --tier full; --categories / --cells select a slice.
# --seeds-per-cell limits seeds without replacing them; --list prints the plan.
python3 tests/ai/scenario-matrix.py --report DIR1 DIR2 ...
# Same-source switch A/B: add --style NAME to the queued candidate arm, then compare.
python3 tests/ai/scenario-matrix.py --compare CONTROL CANDIDATE
```

The queue distributes the existing harness over the cluster, including headless-only
Air commands; it never submits the dirty checkout. For explicit independent shards,
add `--shard K/N`, give each shard a distinct results directory, and combine them
with `--report`. Do not launch simultaneous local batches or create another build tree.


## Machines and time

Full tier, 635 games, 2026-10-18, split `--shard K/4`:

| Machine | Jobs | Games | Wall | Notes |
|---|---|---|---|---|
| MacBook Pro (`dunecity-pro`) | 8 | 159 | 21 min | |
| MacBook Air (`dunecity-air`) | 7 | 159 + 158 | 21.5 + 25 min | ran shards 2 and 4 (the mini queue was full) |
| Windows PC (`dunecity-win`) | 8 | 157 | 32 min | one 4v4 DuneCity fortress game hit the 1,800 s wall limit (625 s on the Pro) and was replayed on the Air |
| Mac mini (queue) | 3 | smoke only | 1.7-2.2 min smoke | queue was full all afternoon |

So the full tier is about 25 minutes on three runners (about 3 CPU-hours: campaign missions take
5-40 s wall, most matches 20-120 s, 7-8 player and 4v4 DuneCity games up to 10 minutes). The smoke
tier takes 70 s (8 jobs) to 130 s (3 jobs, loaded mini). Parity: the smoke tier's ten final digests are
identical on the mini, the Pro, the Air and the PC (the PC prints some `end_s` 0.1 s lower: printf
rounding of the same cycle count).

## Results on 1.0.847 (staging `integrate/feature-batch` b683286f)

Full report: `docs/ai-scenario-matrix-results/847/report.md`; raw CSVs
`~/Documents/projects/dunecity-results/scenario-matrix/full-847`. Smoke on 1.0.847: 9/10 pass, the
4P FFA times out with nobody eliminated (ctest `quantbot_scenario_matrix` fails on this baseline; the
other 36 tests of `ctest -j3 -R 'quantbot_|dunelegacy_tests|skirmish_generator'` pass).

No crash in 635 games (the only failures were harness ones: Full Wormage's binary map header, now
read; one PC wall-time limit), no idle army anywhere (every house that lasted 20 minutes made sorties or
kills). What holds:

- **All against Atreides vanilla** 6/6 within 40 min (mean 31 min); **reverse** (Atreides Medium v 4
  QuantBot Hard) lost 6/6 as it must; **DuneCity** Brutal wins 5/6 (mean 41 min; one seed lost).
- **Hard v Easy** 23-1 (score +0.92 [+0.75, +1.00]); **Brutal v Medium** 17-5-2 (+0.48 [+0.12, +0.78]),
  but only 4-4 on the small vanilla Random Skirmish map; QuantBot QH teams beat AI Player Hard teams
  in every 2v2/3v3 (24/24) and QH beats AH 1v1 CY 4/4.
- Placement score by controller over Random Skirmish: QB +0.30, QH +0.18, QM -0.13, AH -0.28,
  QE -0.35, AM -0.57 - the QuantBot order holds overall.
- **Campaign**: 411 missions run, every one loads and runs, triggers fire (1,287 fired); the Hard
  partner wins 174/192 (vanilla 84/96, DuneCity 90/96).

Problems found:

1. **Free-for-alls do not end.** 4-8 house FFAs time out in 17 of 24 skirmish FFA games and 5/8 CY
   FFAs (6P DuneCity 4/4), often with nobody eliminated although every house fights (kill values 20-90k).
   Team games mostly end. The smoke FFA is one of them (ArmyStuck's bug).
2. **Medium does not beat Easy.** QM v QE 7-3-14: 8/8 time-outs on Habbanya-Penny, 6/8 on the DuneCity
   map, 5-3 on the vanilla map; score +0.02 [-0.23, +0.31] - the ladder order is not shown.
3. **3v3 Hard team loses to the Medium team** on the Random Skirmish 160x160 medium-base map (0/4).
4. **Hard partner loses late campaign missions**: vanilla level 8-9 (missions 20-22) Atreides/Harkonnen
   lose 8 of 12 Hard games against QuantBot Easy enemies (Ordos m21 once); Atreides m11 lost once;
   Harkonnen m14 and Ordos m18 once not finished; DuneCity: Atreides/Harkonnen m22 lost once each, Ordos
   m17 never finishes (80 min, 2/2), Harkonnen m3 and Ordos m1 once not finished; against
   `CampaignAIPlayer` 4/27 lost (Atreides m2, m5, m20; Ordos m5).
5. **DuneCity campaigns were won economically on 1.0.847**: 74 of 90 DuneCity Hard wins were the economic
   victory (since removed: `fix/remove-economic-victory`), e.g. 505/500 population at 8.6 min on Ordos
   level 4, with little or no fighting; mean win 16 min against 20 min vanilla.
6. DuneCity games end later: Brutal v Medium on the DuneCity Random Skirmish map 2/8 time-outs, 4v4
   fortress 3/4 time-outs (the Hard team wins the fourth), the 4P DuneCity map QH mirror 3/4 time-outs.

## Difficulty ladder, team games and late campaign missions (branch `fix/ai-difficulty-ladder`)

Stefan: "Brutal > Medium, Hard > Easy, Medium > Easy". Before = `fix/ai-army-stuck-in-base` 8a8e4833 (+ the
ladder's CY map), after = 5ad8968d (game code identical to the branch head). Results:
`~/Documents/projects/dunecity-results/difficulty-ladder/` (`before`, `c9`, `c5-campaign`; reports
`before-report.md`, `after-report.md`, `after-campaign-report.md`; diagnoses `diag-*`).

### What Medium and Easy differed in, and why Habbanya-Penny timed out

In custom games Easy and Medium shared one army behaviour: a standing raid share (Easy 25%, Medium 40% of
the ground army per wave, the rest at home, `SimpleArmyPolicy::customAttack`), both at the global 40%
attack threshold of their limits (Easy 15,000 / Medium 25,000 on 96-128 maps), no ornithopter raids. They
differed only in limits, harvesters, defenders, infantry share and repair yards. On Habbanya-Penny both
reached their caps (Medium 24,000 of 25,000 with 117,000-158,000 credits unspent, Easy 15,000 behind its
turrets) and traded evenly for 60 minutes: 8/8 time-outs on 1.0.847.

Brutal lost to Medium on small maps for the opposite reason: its first wave waited for 25% of its 80,000
limit (20,000) while Medium's left at 10,000; in 23 of 72 games Brutal had 3,500-10,500 at home, never
launched (sortie 0-2 minutes) and died to Medium's first wave at 17-27 minutes. On CY starts Medium's
ornithopters also razed Brutal's undefended base at minute 9-10.

### Changes

- **Custom Medium joins the whole-army difficulties** (`QuantBot::autonomousCustomArmy`, formerly
  `autonomousCustomHardBrutal`): one concentrated wave, recovery posture, force concentration, engine Hunt,
  whole-army base defence. Easy keeps the raid share (Easy at its own level is unchanged). Air raids stay
  Hard/Brutal.
- **Held opening capped at Medium's limit:** a whole-army house that started without an army able to strike
  ends its held opening at its own threshold share of at most Medium's limit (Brutal 6,250, Hard 7,500
  (global 40% = 10,000), Medium 10,000 on 96-128 maps). Starting armies keep their thresholds
  (`quantbot_ffa_army_deploys`). Applying it to every wave (c10) was not better and was reverted.
- **Reachable readiness:** a custom house whose credits plus the spice left cannot buy its threshold attacks
  with what it has (`SimpleArmyPolicy::reachableReadiness`).
- **Medium spends banked credits:** up to half its cap above it from credits over 10,000
  (`QuantBotBuildPolicy::surplusMilitaryBudget`; Medium x 1.5 stays below Hard's cap). +100% let the reverse
  All against Atreides Medium hold off four Hard houses for 60 minutes.
- **Team play:** "outnumbered" and the perimeter turret goal compare our team's ground army with the
  strongest enemy team (`teamGroundArmyValue`), and the held opening ends once the team's army is worth the
  house threshold. In 1.0.847 each Hard ally compared its own army with the whole Medium team, was
  "outnumbered" and garrisoned, and none of the three launched a wave in any of 12 seeds (1/12 won).
- **Campaign partner:** the economy push no longer plans 43-56 harvesters as if alone on the map (no army
  for 12 minutes in missions 20-22); a Palace missile on a harvester is not a contact to scramble the army to
  (vanilla/DuneCity m22); after 15 minutes the partner's wave readiness is measured against the strongest
  enemy house's army (1.25x, at least 2,000, at most 75% of its limit) instead of a fixed 8,000.
- **DuneCity campaign stalls:** structures keep off the reserved factory plots (five refineries on the heavy
  factory's plot, Ordos m17: no heavy factory in 80 minutes); silos are built where the tech level has no
  heavy factory; the spice quota counts the store's tax credits too in DuneCity (taxes fill the shared store
  first, so Harkonnen m3 / Ordos m1 quotas could never be met - this also applies to human players).
- **Medium starport opening** walks to the heavy factory when it may not add a repair yard (the 7P FFA
  idle Medium had 17 harvesters and no factory).
- **Random Skirmish generator:** an asymmetric map whose start has no spice within three times its own
  field's seeding distance by ground is regenerated (next seed; the first connected one if none passes).
  The 7P FFA idle house's nearest spice was 91 tiles round a massif. Symmetric maps are fair by construction
  and unchanged; every pinned generator digest is unchanged.

### Results (cluster, full tier)

| Ladder pair (72 games) | Before W-L-T | Before score [95% CI] | After W-L-T | After score [95% CI] |
|---|---|---|---|---|
| QB v QM | 43-24-5 | +0.25 [+0.02, +0.47] | 46-24-2 | +0.30 [+0.08, +0.50] |
| QH v QE | 62-9-1 | +0.73 [+0.57, +0.87] | 64-8-0 | +0.78 [+0.64, +0.92] |
| QM v QE | 32-10-30 | +0.20 [+0.02, +0.38] | 58-11-3 | +0.64 [+0.46, +0.80] |

QM v QE time-outs 30/72 -> 3/72; on Habbanya-Penny 0-0-18 -> 17-0-1. QB v QM by map after: Habbanya-Penny +0.82, rs-vanilla
+0.22, rs-dunecity +0.15, CY 0.00 (before -0.29).

Campaign, Hard partner (QuantBot Easy enemies): vanilla 84/96 -> 92/96, DuneCity 90/96 -> 96/96 (no
mission left unfinished; Ordos m17, Harkonnen m3, Ordos m1 all won); vanilla missions 20-22 Atreides and
Harkonnen 4/12 -> 9/12. Still lost: Atreides m20 (both seeds; the Harkonnen enemy holds 11,000-16,000 and 8
turrets, waves trade 1:1), Harkonnen m22 seed 95897 (a captured Devastator detonated in the base, a
sandworm), Atreides m17 seed 80762 (new). Against `CampaignAIPlayer` 23/27 as before.

Team games: 3v3 Hard v Medium 0/4 -> 1/4 (the Hard houses now sortie 5-21 minutes; a Medium team has the
same army tactics now, so Hard's edge is its economy and limit only - not fixed); 4v4 fortress 1/3 -> 2/4;
reverse All against Atreides 6/6 lost by Medium as required; All against Atreides vanilla 6/6 within 40
min, DuneCity 6/6 (before 5/6). 7P FFA: no idle house (before QM idle 1/4). Regressions: 6P DuneCity FFA
0/4 decided (before 3/4), the skirmish controller order is QH +0.26 > QB +0.18 > QM +0.12 > QE (Brutal
plays 20 houses, interval -0.13..+0.48).

## Difficulty separation (branch `fix/difficulty-separation`, protocol 88)

Stefan: a clear ladder, Brutal > Hard > Medium > Easy; "brutal should have a higher harvester limit and military
limit"; "don't test on such short timelines". Data: `~/Documents/projects/dunecity-results/difficulty-sep/`
(`before` = 1.0.848 game code, `after2` = this branch; `armA`/`armF`/`armAF` the A/B; `sfix-long`, `e1` screens;
`mirror-qh`; reports `*-report.md`; `ctest-bea4449c.log`).

### What flattened Brutal v Medium (A/B on the short maps, 72 games each, paired with 1.0.848 by game id)

| Arm | QB v QM | QH v QM | QM v QE |
|---|---|---|---|
| 1.0.848 | +0.12 [-0.11, +0.34] | +0.12 [-0.09, +0.34] | +0.85 |
| A: Medium without the strategy profile | +0.33 (paired +0.20 [+0.04, +0.38]) | +0.40 (+0.28 [+0.10, +0.46]) | +0.61 (-0.25) |
| F: FinishWhenLeading Hard/Brutal only | +0.14 (+0.02 [-0.06, +0.10]) | +0.12 (+0.00) | +0.88 (+0.03) |
| A + F | +0.35 (+0.23) | +0.36 (+0.24) | +0.58 (-0.27) |

The cause is the strategy gate: 1.0.848 renamed `autonomousCustomHardBrutal` to `autonomousCustomArmy`, so
Custom Medium also played the default `Fixed:Tuned,Swarm` (tuned on Hard/Brutal). FinishWhenLeading does
nothing in 1v1 and stays on. Fix: `QuantBot::playsStrategy()` (Custom Hard/Brutal) gates the strategy pick,
`strategyMix()` and `strategyTempo()`.

### Brutal's limits did not take effect

In a custom game Brutal and Hard ran the same army and economy code; nobody gets cheats. The configured
`HarvesterLimit*Map` (96-128 maps: Easy 8, Medium 15, Hard 40, Brutal 100; 160+: 15/25/40/50) was overwritten
every update by the spice budget (`max(1, spice/2000)`), so every difficulty planned the same fleet: on a
96x96 ladder map the target was 63-73 workers for both Brutal and Hard, and both actually ran 4-30 harvesters,
held back by the same army-first spending rule. The military limit (Brutal 80,000/100,000 v Hard
50,000/70,000) binds only in long games: in long DuneCity games Hard ends at its cap (52,760 on 128x128,
74,330 on 160x160 at most) while Brutal's armies reach 175,000-365,000, so that limit already works and needs
no change.

Changes (no resource bonus; a 15% delivery bonus tried earlier was removed):
- **The configured harvester limit is a real ceiling** in custom games above 64x64
  (`QuantBot::configuredCustomHarvesterLimit`; up to 64x64 the campaign-era values of 2-10 workers would starve
  every economy, so the spice budget alone decides there as before; campaigns and support controllers unchanged).
- **Brutal's economy step in vanilla custom games** (DuneCity custom games had it already): an 8-12 worker opening
  floor and equal, not double, army capital per worker while the fleet grows towards its larger limit
  (`QuantBotBuild.cpp` `brutalEconomy`, `CityEconomyInvestmentPolicy::preferFactoryHarvester`). Not while it faces
  more enemy houses than its team has or is outnumbered: against four AI Player Hard houses (All against
  Atreides) the economy-first opening lost 6 of 6.

Harvesters over time (local, QB v QH, vanilla): before, both houses ran 4/6/9/14/19/25 at minutes 3/6/9/12/15/20
(Hard 4/6/9/13/19/27); after, Brutal 12/21/32/37 at minutes 9/12/15/20 against Hard 7/11/17/24 (CY start), and
on Habbanya-Penny Brutal 42-81 at minutes 15-25 against Hard's 40-worker cap.

### Ladder before/after

Long maps decide (Random Skirmish 128x128 and 160x160, vanilla and DuneCity, medium bases, no army, 75/90
minutes; 144 games per pair, 18 seeds x both houses; interval over seeds). Short maps (96x96 small bases,
Habbanya-Penny, CY 96x96; mostly over in 15-35 minutes) are reported: 1.0.848 144 games, after 72.

| Pair | Long 1.0.848 W-L-T, score, mean/max min | Long after | Paired | Short 1.0.848 | Short after |
|---|---|---|---|---|---|
| Brutal v Hard | 91-38-15, +0.34 [+0.22, +0.48], 41/90 | 113-18-13, **+0.64 [+0.50, +0.76]**, 41/90 | +0.30 [+0.17, +0.43] | +0.11 [-0.08, +0.29] | +0.19 [-0.05, +0.42] |
| Hard v Medium | 92-36-16, +0.36 [+0.23, +0.49], 46/90 | 99-28-17, **+0.46 [+0.34, +0.59]**, 45/90 | +0.10 [-0.02, +0.23] | +0.08 [-0.09, +0.24] | +0.25 [-0.01, +0.50] |
| Medium v Easy | 133-7-4, +0.87 [+0.80, +0.94], 39/75 | 137-2-5, **+0.93 [+0.87, +0.98]**, 33/75 | +0.06 [-0.01, +0.13] | +0.74 [+0.62, +0.85] | +0.85 [+0.74, +0.95] |
| Brutal v Medium | 105-39-0, +0.46 [+0.31, +0.61], 35/60 | 132-12-0, **+0.83 [+0.71, +0.93]**, 36/69 | +0.38 [+0.24, +0.53] | +0.15 [-0.05, +0.34] | +0.51 [+0.26, +0.73] |
| Hard v Easy | 140-4-0, +0.94 [+0.89, +0.99], 33/63 | 144-0-0, **+1.00**, 29/50 | +0.06 [+0.01, +0.11] | +0.85 [+0.74, +0.94] | +0.94 [+0.86, +1.00] |

Time-outs on the long maps after: QB v QH 13/144 (DuneCity 128x128 10), QH v QM 17/144 (DuneCity 160x160 14),
QM v QE 5/144. On the long maps the ladder already held on 1.0.848 for the adjacent pairs; the flattening was
on the short maps, where Brutal v Hard is still not shown (+0.19) - those games end before limits matter.
Vanilla games stay short even on 128x128/160x160 maps (mean 22-36 minutes); DuneCity games run 35-71.

Skirmish category (52 games, mixed controllers): QH +0.42, QB +0.18 [-0.12, +0.49] (20 houses, all in free-for-alls
where the vanilla economy step is off), QM -0.08, QE -0.47; 9 time-outs, no crash.

### House or start?

The ladder used to alternate houses between seeds, so a seed's start and its house were confounded. In a Hard
mirror (144 games, both houses of every seed, each side keeping its start) Harkonnen won 65, Atreides 74, 5
draws: no Harkonnen advantage overall. The start decides more than the house (the same start won both games of
a seed in 41 of 72 pairs, the same house in 27); the one house effect is Habbanya-Penny, Atreides 25-10 from
either start. Every ladder seed is now played with both houses (`#swap`).

## City population ceilings (custom DuneCity games, protocol 110)

Each capped QuantBot's displayed city population (`QuantBotCityPolicy`), checked both when it orders
population-bearing buildings and in natural zone growth; the R+C+I zone allowance is 48 zones per
20,000 of the ceiling, rounded up. Human houses and campaigns are never capped.

| Difficulty | up to 32x32 | up to 64x64 | up to 128x128 | larger |
|---|---|---|---|---|
| Easy | 5,000 | 10,000 | 20,000 | 30,000 |
| Medium | 10,000 | 20,000 | 40,000 | 60,000 |
| Hard | 20,000 | 40,000 | 60,000 | 90,000 |
| Brutal, Defend | none | none | none | none |

Hard (Stefan, 2026-11-01; `[Force Concentration] HardCityMatch`, hashed, on): while any other house's
city is bigger than Hard's ceiling, the ceiling rises to that city's population (last census), so
Hard keeps pace with a large human or Brutal city but no further. Off restores the former fixed Hard
row 20,000 / 40,000 / 80,000 / 120,000. In the 1.0.864 1v3 on the MacBook Air (160x160, three Hard
houses) one Hard city sat at its 120,000 ceiling from minute ~50 while the human's was ~56,000.

## Ladder and campaign candidates (protocol 98, default off)

These candidates are hashed `[Force Concentration]` settings, not environment-variable gameplay
switches. They do not become defaults until their paired score difference clears the 95% rule.
Use `--compare CONTROL CANDIDATE` on matching game ids; bootstrap clusters keep both starting sides
of a ladder/team seed together. Campaign house/mission identities remain separate even if their
numeric seed happens to be equal. Campaign time-outs use the same −0.25 duel draw as other games.

### Team games

`JoinAllyWaves` (`--style allyjoin`) lets Custom Hard/Brutal join an allied QuantBot's whole-army
wave launched in the last 30 seconds, keeping its objective and the viable-wave minimum.
The completed 2v2 extension contains **96 games per arm** (24 seeds per mod, both sets of starts),
reusing the first 48 rather than replaying them: paired Δ is +0.06 [−0.06,+0.18],
with 4 control versus 6 candidate time-outs. The 3v3 comparison remains 48 per arm,
Δ +0.04 [−0.17,+0.25]. Neither clears the default rule. In 54 bundled multiplayer-map
games per arm, both configurations beat AI Player Hard in all 20 such games;
total time-outs are 8 off and 7 on. No accepted team default yet.

### First-wave funding

Saved telemetry for the 96×96 DuneCity Hard-v-Medium loss on seed 65829 shows the Hard heavy factory
buying four harvesters and three MCVs, but no combat units, between minutes 8 and 16. Medium's heavy
factory produces seven launchers in that interval. Capping the fleet only until factory construction
(HeavyFactoryFirst), or until the remembered enemy army leads (ArmyBeforeFleet), did not establish
a paired improvement and those candidates were removed.

`OpeningCombatBudget` (`--style openingcombat`) instead keeps a Custom Hard/Brutal start's
four-worker economy until its first **actual full wave dispatch**, and defers optional extra
construction capacity in that window. Recovering a lost yard and opening new rock when the base
cannot grow still take precedence. The first-wave readiness is 100%; subsequent waves and normal
fleet limits resume at dispatch. A starting heavy factory makes the rule inert, preserving the
medium-base long ladder; Easy/Medium and campaigns are unchanged.

`CampaignCombatBudget` (`--style campcombat`) applies that four-worker first-dispatch budget only
to a human's Hard/Brutal campaign partner that started without a heavy factory. It does not alter
enemy controllers, Easy/Medium partners, resources, production costs, fog or combat strength.
Completion uses the existing saved campaign-wave launch marker. This differs from the rejected
CampaignFactoryFirst rule, which released the fleet as soon as the factory stood.

Both first-wave funding switches remain **off and rejected for default use**.
The shared-build comparisons from source `06bda05bd9f8` completed without
relaxing deadlines or reusing a mismatched physics arm:

| Comparison | Games per arm | Control score | Candidate score | Paired difference [95% interval] |
|---|---:|---:|---:|---|
| Short-map Brutal versus Hard, opening budget | 72 | +0.22 | +0.14 | -0.08 [-0.30, +0.14] |
| Short-map Hard versus Medium, opening budget | 72 | +0.23 | -0.49 | -0.71 [-0.99, -0.44] |
| All standard Hard campaign partners, campaign budget | 192 | +0.95 | +0.91 | -0.04 [-0.10, +0.01] |

The opening rule significantly worsens the Hard/Medium comparison. The campaign
rule does not establish improvement: Vanilla's 96 cases move from +0.94 to +0.85,
while Dune City's 96 remain +0.96. Atreides mission 20 seeds 74708 and 75717
still lose; mission 22 gains one Vanilla seed but loses another. The Dune City
Harkonnen mission 22 loss at 95897 has an identical on/off final digest.
Passing the opening/campaign-pressure consumer probes is not balance acceptance.

Evidence: `~/Documents/projects/dunecity-results/ladder-campaign/`,
`shared-{opening,campaign}-{off,on}/`, `shared-{opening,campaign}-comparison.txt`
and `shared-campaign-critical-cases.json`. Jobs are
`20261024T232452-opening-{off-902203,on-e7b0c8}` and
`20261024T232453-campaign-{off-f544e9,on-8f53d6}`. The 672 total games use
the shared build's default movement flag off. Team play and the unresolved
named losses are not declared fixed by these results.

### Optional construction-capacity runway (candidate, default off)

`CapacityMcvRunway` (`--style mcvrunway`) keeps optional MCV expansion behind the
existing four-minute production forecast and cash reserve. It subtracts only
newly accepted optional MCV purchases from that headroom; ordinary unit and zone
orders are already forecast and must not be charged twice. Actual loss of the
last yard, existing rock-expansion and colonisation recovery remain exempt.
Shared combat upgrades, worker targets and first-wave readiness are unchanged.
The style does not enable either rejected first-wave budget above.

The retained `sdc` trace at cycle 52,325 bought a 900-credit MCV with projected
cash −406, after forecasting 13,071 income/cash against 13,477 continuing-production
cost. The new same-engine queue regression reproduces the off-arm purchase,
requires on-arm rejection **and affordable combat fallthrough**, and retains
funded MCV expansion, real lost-yard recovery and shared technology progression.
All three shared-spending mod probes pass at `1d0a93be`.

This forecast includes existing idle/held production capacity, not only unpaid
orders. It is a conservative capacity rule; passing queue boundaries does not
establish campaign, team or ladder improvement. The completed source `64c370dc04c7`
comparison contains **876 games**, with matched inputs and zero game errors:

| Comparison | Games per arm | Control score | Candidate score | Paired difference [95% interval] |
|---|---:|---:|---:|---|
| Short-map Brutal versus Hard | 72 | +0.22 | +0.19 | −0.03 [−0.08,0.00] |
| Short-map Hard versus Medium | 72 | +0.23 | +0.23 | 0.00 [−0.06,+0.06] |
| Standard Hard campaign partners | 192 | +0.95 | +0.95 | 0.00 [0.00,0.00] |
| Hard versus Medium, 2v2 teams | 48 | +0.57 | +0.50 | −0.07 [−0.24,+0.09] |
| Hard versus Medium, 3v3 teams | 48 | +0.29 | +0.35 | +0.06 [−0.22,+0.33] |

No group establishes paired improvement. All five named campaign losses remain
losses; nine campaign final digests change, so unchanged scores are not evidence
that every campaign was inert. With corrected movement on, the mandatory six-seed
AAA comparison is 5/6 within 40 minutes in both MCV states, with all six paired
digests identical. Seed906213928 still times out at41; this rule does not fix it.
`CapacityMcvRunway` stays off and is rejected for default use.

Raw data, reports, matching-input coverage and named losses:
`~/Documents/projects/dunecity-results/ladder-campaign/mcv-runway-64c370dc/`.

### Light-raider service ownership (candidate, default off)

`RespectServiceOwnership` (`--style serviceownership`) keeps the periodic
light-raider scan from replacing an existing repair reservation or promised
carryall pickup with an evade/prey order. It uses the existing repair role and
pickup ownership checks already respected by hunting-launcher reactions.
It does not change movement, firing, carrier selection, rescue timing or the
three rejected funding/team policies.

The actual corrected-movement battle at source `64c370dc04c7` records Quad3190
repairing and awaiting pickup at cycle98,053. At98,104, periodic raider micro
changes its mode, releases pickup/repair ownership and selects a retreat.
The unit dies at98,139, but the trace lacks the original carrier/arrival history:
preserving that service is **not** established to save it or fix AAA.

The existing real kiting probe at `c22c9455` now checks eight matched off/on
cases: repair-only reservation, healthy pickup-only booking, combined repair
and pickup, and an unreserved raider that must still evade. Repair requests and
reciprocal carrier targets come from normal engine APIs; ownership flags are
not manufactured. The probe passes; no completed rescue or strategic-outcome
claim is made. The switch remains off pending the unchanged outcome gates.

The completed same-native-build comparison contains36 games (18 per arm),
with matched inputs, unchanged limits and zero game errors. Corrected movement is
on in both; only the independent service-ownership style differs. Native binaries
were retained across reporting-only sources `78eaa25ebef0` and `d9d8788c918d`,
verified on all four hosts.

| Mandatory vanilla seed | Service off, game minutes | Service on, game minutes |
|---|---:|---:|
|1756374161|38.34|36.20|
|906213928|time limit41|35.26|
|41562270|34.36|32.60|
|1841712204|38.11|35.72|
|1430976924|33.90|time limit41|
|1804669015|33.64|36.44|

Both arms meet the original all-four-enemies/40-minute condition in5/6 cases.
The previous failed seed now passes, but another fails: this is **not** movement
acceptance. DuneCity wins and the reverse Medium-versus-four-Hard expected
defeats each remain6/6 in both arms at their original60-minute caps.
`RespectServiceOwnership` remains off and unaccepted; the genuine booking
regression is fixed only when explicitly enabled, not claimed shipped by default.
Raw inputs, all36 outcomes and actual host coverage:
`~/Documents/projects/dunecity-results/unit-movement/serviceownership-78eaa25e/`.


## Police recovery and rich-city audit (2026-11-03)

Source `2456373e6002`, same-build `police-recovery-off/on`: ten 30-minute
Random Skirmish FFA games, forty AI-house histories. Four paired Fortress
games cover Hard/Brutal, seeds 9101/148680876, 160×160, 20,000 armies,
40,000 cities and Medium spice. A fifth pair uses the ordinary Medium-base,
no-city, 2,500-army 128×128 Hard start, seed 9101. All houses receive 3,000
credits and spice factor 1. All games reach the time limit without a harness
error; this is an economy/recovery observation, not an expected-winner gate.

Minute-level per-AI credit curves, sources, spending and funding are retained
under `~/Documents/projects/outputs/ai-rich-police-20261103/`; the complete
tables, capped-city spending explanation and unimplemented balance
recommendation are in [City economy: Rich-AI investigation](city-economy-balance.md#rich-ai-investigation-2026-11-03-protocol-111-staging).
The wealthiest case is Hard Atreides, seed 9101: 37,257 credits at minute 30,
from ordinary starting cash plus 159,401 refined spice and 147,294 gross tax,
not Yard storage income. Existing population/army admission ceilings constrain
late construction. Fortress OFF/ON curves and final digests match; ordinary
starts exercise budget changes and diverge. No income or difficulty-cap change
is silently folded into police recovery.

The real-engine `quantbot_police_budget` gate now proves funding **100→53→81**
after cash recovers and **100→97→100** after a small affordable recovery; the
off-switch control remains 97. Its 30-second/two-minute review boundaries,
immediate cuts and per-house/campaign/helper ownership checks pass.

Acceptance: cluster `20261103T202057-ctest-daf8f5` **11/11 PASS**:
police budget/placement, shared-spending DuneCity, no-spice city, growth
installments, custom attack, strategic-site stall, mandatory all-against-Atreides,
scenario matrix smoke, FFA deployment and performance. Air ran the strategic
stall/FFA gates; mini ran the six short probes; Pro ran performance and the two
match matrices. `dunelegacy_tests` separately passed on PC in the shared
same-source job `20261103T201949-ctest-746ac5`.

### Opening-market fixture and Starport Population

The full-suite opening-economy fixture's synthetic base does not run a city
census before testing market support. With the default-on Starport Population
rule, that is not an eligible city: a supported but sold-out import cannot
justify a Starport below 5,000 displayed population. The fixture now establishes
and later restores its own eligible census for the absent-market,
sold-out-supported and disabled-import assertions, and adds a separate
below-threshold negative control. It does not disable or bypass the match rule;
campaign exemption follows `isCampaignGameType(game.gameType)`.

The Starport-economy probe's late empty-market technology cases similarly
switch from their initial campaign into a custom game. Those cases establish
and restore the required population before checking locked/disabled/unrelated
technology, exactly one prerequisite Starport, pending-order deduplication
and resumed IX/Palace construction. Earlier campaign import and MCV-recovery
checks keep their original population and assertions.

