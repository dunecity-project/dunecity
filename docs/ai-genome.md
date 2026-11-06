# QuantBot genomes and regression-gated league

A genome is a collection of independently expressed behaviour features, not one
master AI on/off switch. Search changes only test inputs. **No search command
writes shipped defaults, chooses a release, or treats a missing measurement as a
pass.** Every AI change is opt-in until the integrator accepts the promotion
record: same-source default, frozen league opponents, historical releases and all
reported-issue checks.

## Gene registry

`tests/ai/genome-registry.json` is the single machine-readable registry. Every row
has `id`, `name`, genuine INI `section`/`key`, `kind`, `levels`, `default` and
`owner_doc`; difficulty-specific settings also identify their config block.
The initial committed-config inventory has 336 genes, including all 61 booleans,
formation/strategy choices, wave thresholds, launcher distance, timers, recovery
ratios, force budgets, unit ratios, priority weights and tuned tech-band mixes.
That first source pin is `b10df0c63cf6ade5eb5057f9086a1cfac719f25d`.
The follow-up registry has 339 genes / 64 booleans: `CityCompactPower`
(source `dc2aee77`), `ContinuousResearch` (`f82f13d9`) and
`StartWhatMakesSense` (`d3098d9a`) are independent `0/1`, default-OFF genes.
UnitChoice's `BufferedHeavyProduction` and `IncomeHeavyFactories` (source
`e9f72ce3`, default OFF) brought the QuantBot part to 341 genes / 66 booleans;
`FirstFactoryOwnPrerequisites` (default OFF, `docs/quantbot-money-planning.md`)
brings it to 342 genes / 67 booleans.
The registry also holds one **game-rule gene**, `technology_installments`
(343 rows in total). Every evaluation pin must include its complete
parser/registry pair.

### Game-rule genes

A row with `"scope": "game"` is a whole-game `[Game Options]` rule, not a
QuantBot key. The loader audits its key against the requested-options reader
(`applyGameOptionsFromConfig`) and its default against the compiled
`GameOptionsClass` initializer. A game rule:

- may appear only in a genome's top-level `values`, never in a contextual or
  per-phase expression rule, and is never rendered into a per-house INI;
- applies to **both arms**: one game has one rule level for every house.
  `genome-match.py` resolves it from the roster, requests it explicitly in
  `requested-options.ini` (registry default when no arm sets it, never a mod
  default), records it on the game, house, assignment and observation rows and
  fails the game (`game_rule_mismatch`) unless the effective options carry it.
  Roster genomes, `--technology-installments` and a frozen `--game-options`
  INI that set different levels are an error, not a silent override;
- is not a search locus. A one-gene contrast of the rule alone would compare
  identical AIs under one shared rule. Instead a QuantBot gene declares
  `"requires": {"<rule>": <level>}`: `continuous_research` requires
  `technology_installments: 1`, because ContinuousResearch only acts when the
  Installments rule is on. Validation rejects an active dependent gene (top
  level or any expression rule) without its rule; search operators close the
  rule over active dependents and drop it otherwise. A dependent gene is
  screened one gene at a time (never a fraction column) and its 2x2
  interactions put all four cells under the rule. Analysis strata include
  `technology_installments`, so rule-ON games are never pooled with rule-OFF
  games.

The registry loader audits the real C++ reader, constructor and shipped default
INI. Unknown/forgotten boolean or enum keys, wrong effective defaults and invalid
levels fail explicitly. Existing INI parsing is authoritative: a numeric-looking
value read by `getIntValue` is an integer gene, not an invented floating knob.
Boolean levels are integer `0/1`; decimal levels are exact strings; decision,
mutation and statistical arithmetic does not depend on binary floats.

`genome_model.py` provides `load_registry`, `default_genome`, `validate_genome`,
`resolve`, `render_ini` and `genome_id`. A default chromosome is **sparse**:
`values={}` renders no overrides, retaining the actual native/mod baseline hash.
An explicit assignment to a registry default still renders; it is not silently
dropped. Each phase starts from a fresh native baseline config, so an opening-only
rule cannot leak into the late game. No conditional Tornie-only priorities are
injected into a Vanilla baseline.

A chromosome example (levels must be registered):

```json
{"id":"timed-waves","values":{"wave_readiness_percent":60},
 "expression":[
   {"when":{"difficulty":["hard","brutal"],"mod":["dunecity"],"size_max":96,"phase":["opening"]},
    "values":{"wave_readiness_percent":45}},
   {"when":{"house":["atreides"],"phase":["late"]},
    "values":{"attack_timer_ms":30000}}]}
```

Predicates can select difficulty, mod, house, map format, inclusive map-size
bounds and opening/mid/late phase. Empty/absent dimensions match all; matching
rules apply in list order, last assignment wins. Genotype SHA-256 excludes the
human label but preserves ordered-rule semantics. Phase boundaries default to
0/10/20 **game** minutes, never wall time. Continuations use elapsed loaded-save
time. Global game rules are whole-game genes (above), never per-house AI genes.

## Mixed-genome games and provenance

`genome-match.py` implements the cluster's `schedule/build_probe/play/run/report`
contract. `--manifest-b64` contains the exact inline manifest; no worker reads a
mutable submitter-only genome file. `--pairs a:default` mirrors each seed between
house assignments. `--ffa a,default,default,default` uses cyclic four-slot
rotations; distinct four-genome quartets work too. House IDs, slots and assignment
receipts are retained. Normal play-style, force-flag and non-genome harnesses keep
their previous path.

Contexts include Medium/Hard/Brutal, Vanilla/Dune2R/DuneCity, CY and Fortress
starts, map sizes, spice, research on/off and applicable global rules. CY/no-army
cohorts measure real technology adoption rather than making prebuilt factories
look like construction success. Phase INIs use real `applyOverrides` and
`getConfigHash`. Per-house/per-controller phase hashes, manifest/registry/schedule
SHA-256, exact map/full serialized options, content/object-data hashes, probe SHA
and immutable source provenance are separate evidence. Config hashing is not a
claim that the private phase schedule is a shipped multiplayer protocol.

The C++ phase helper preserves saved controllers (including multiple QuantBots
on one house) and rejects human/shared-human overrides. It refreshes the existing
cached military limit and strategy/timer state when the corresponding config
changes; it does not replay starting actions or replace a player.

The harness reuses `dunecity-audit.py` detectors and
`ai_baseline_scorecard.scorecard(run)`, not a second behaviour scorecard. Full-evidence
sidecars retain technology, actual production/spending, composition, attacks,
wave engagement, R/I/C and research. Missing/censored/not-applicable values remain
explicit. Screening and evolution default to `--evidence lean`: games,
assignments, config phases, `houses.csv`, gzip scorecard observations and small
summary/map/options receipts. Events, run logs, replays and large tables are
parsed in scratch, then deleted with the game directory, so node archives stay
small. Errored games, a deterministic 1-in-50 game-id audit sample
(`--audit-every`) and elites raced with `--evidence full` keep complete traces.
Measured on `abb7ef58` (four 30-game-minute Hard/DuneCity 1v1 games, no errors):
458,704 bytes in total, **0.115 MB/game** including node archives, versus about
13 MB/game with full capture.

## Screening and epistasis

`genome_search.py screen` writes one-gene default knockouts/activations and
registered parameter-level sweeps. The second screen is a regular resolution-IV
fraction: highest-bit-set Walsh columns are balanced and orthogonal, with no
main-effect column equal to the product of two main columns. It includes the
global foldover. **Two-factor interactions remain aliased with each other.** The
plan records those alias groups; it does not present fractional product contrasts
as isolated causal interactions.

Use fixed common seeds and mirrored/rotated assignments. Average houses and
orientations within independent map-seed blocks before inference. Both fitness
levels/quality and contrasts require the same complete cohort: exact comparison,
map/options/content receipts, genotype digests and every unique house/slot
assignment. Every requested 1v1 house pair must be complete (three houses mean
twelve games, not four). Fragments from different comparisons, rules or options
cannot form a complete seed. Reports retain mod, difficulty, research, format,
size, base, spice, phase, house and native receipt strata; unsupported contexts
are not pooled into a global harmful-gene conclusion.
One-gene binary contrasts are ON minus OFF regardless of shipped default;
parameter/fractional contrasts are explicitly alternative minus default.

Intervals are deterministic seed-block bootstrap intervals. A shared-seed
max-deviation bootstrap supplies simultaneous 95% intervals within each metric
family. Incomplete seed designs or fewer than four independent seeds cannot
receive a decisive interval. These exploratory screens do not authorize
promotion; elite confirmation uses at least eight held-out seeds, with a further
16-seed race if precision remains inadequate.

After screening, `interactions --genes ...` creates explicit 2x2 followups for the
six materially active/related genes (all pairs, retaining all four cells).
Estimate `AB-A-B+baseline` within seed/context. In particular investigate
production choice × producer capacity, research × civic unlocks, and power ×
spending priorities. A negative knockout can reflect a broken prerequisite,
context or interaction: retain its trace and diagnose the refused order, funding,
legal-site, power or dispatch reason before proposing a parameter/logic change.
Simply disabling the feature is not the default remedy.

## Evolution and quality diversity

Concrete league settings: 24 parents, 24 offspring per round, size-3 tournament
selection, uniform locus crossover for switch/categorical genes, and adjacent
registered-level mutation for integer/decimal genes (about two mutated loci per
child, at least one). Context rules are preserved as ordered blocks; mutation can
add context/phase-specific expression. Integer mutation step adapts by a 1/5
success rule. This is a mixed discrete `(mu+lambda)` search, **not CMA-ES**: most
loci are categorical or sparse contextual switches, so a Gaussian covariance
model would be an unjustified representation here.

Retain frozen past champions/defaults, a payoff history and four main-exploiter
slots aimed at the current champion. Opponent priorities are Laplace-smoothed
squared loss rates with a nonzero exploration floor (integer arithmetic). Main
candidates face the current default, a frozen champion and a prioritized hard
opponent; exploiters face the champion. **Every child also plays the same-source
default**: that common opponent is the only source of comparable fitness levels
and paired `candidate` contrasts. Games without the default are league payoff
evidence only (`analyse` writes `payoffs.json` for the next round's PFSP), never
pooled into another genome's fitness. Duplicate opponents are not falsely
counted as distinct. Initial racing uses two common seeds and all four
genotype-to-house × house-to-slot orientations per 1v1 house pair. A
four-player cohort has 4 × 4 = 16 orientations. Elites receive additional
held-out seed blocks and context coverage before promotion: `race` refuses
seeds from the entire recorded selection ancestry and fewer than eight seeds.
Each round re-measures retained non-default parents against the same-source
default on its new common seeds, so subsequent `(mu+lambda)` survival compares
parents and children rather than silently discarding the parents.
`race --contexts MOD:DIFFICULTY:RESEARCH ...` expands held-out coverage while
retaining the selecting plan's size/base/spice. For example, City Brutal OFF/ON
and classic Hard/Brutal lanes test context/interaction concerns rather than
turning one descriptive selfplay difference into a blanket gene rejection.

Genes outside the screened plan, for example newly registered default-OFF
switches, enter a round through `evolve --seed-genes`: each gets one child, the
best-ranked parent with that gene at its alternative level (dependent game
rules closed), before ordinary tournament offspring fill the round. `evolve
--behaviour games.csv... --evaluation-rev FULL_SHA` ranks genotypes feasible <
unverified < infeasible before quality coverage and Pareto rank. Evidence must
match that exact source, genotype, save/check/registry receipts and effective
game rule (explicit genome value, otherwise the frozen save's registered rule).
Missing, errored or capped runs remain unverified; retries never erase a measured
failure. A genotype is feasible only when every registered case passed.
`--seed-combinations GENE_A,GENE_B,...` adds an explicit joint child alongside
individual seeds, within the same offspring count. It inherits the best-ranked
parent and sets those loci to their registered alternatives. This gives a
promising combined selfplay treatment real head-to-head evidence without
inventing its fitness or attributing a joint effect to one component.

Fitness is **not a weighted sum**. Report independent strength (placement/wins,
kills, survival), army/economy and behaviour objectives, Pareto fronts and coverage.
Maintain observed rush/tech/economy/balanced niches separately per full context;
keep a Pareto set in a niche rather than erasing a useful technology specialist
because another chromosome rushes better. Rule/options/content strata retain
separate objective vectors and cannot numerically pool or dominate each other.
A multi-rule control has no single pooled fitness summary. Missing objective
coverage cannot establish dominance. Search may explore unverified candidates; they are not
feasible elites or releasable defaults until every hard gate is satisfied.

## Reported-save constraints

Every reported issue becomes a permanent feasibility check.
`genome-behavior-checks.json` registers ten required cases. Six exact-save
adapters are implemented by `genome-behaviors.py` and the test-only
`genome-behavior-probe.inc`:

| Save | Hard checks during 15 game minutes or an earlier fully observed native terminal |
| --- | --- |
| notbuilding | Ordos (house 2) accepts a Light Factory order before losing its initial yard. Houses 2/5/6/7 make research progress when a legal start is observed; absent opportunity is not applicable. Positive, legal residential plots reach the real funding gate (CityZoning admission rule). |
| problem1 / ainonuclear | No funded, available, legal nuclear-plant choice is rejected. At least one funded opportunity is required; none is not applicable. These frozen saves require no concrete, so price is the complete quote. |
| nonuke_lowcash | Exact 160-map save `ee99099f…`, house 4 / colour slot 4, saved **Technology Installments OFF**. An authoritative compact-nuclear choice using the native **building-installments** route with spendable cash below the nuclear quote must accept a new nuclear order, produce a positive new-built delta and pay at least that accepted quote in native generator-spent telemetry. Existing standing nuclear plants never count; no funded low-cash opportunity is not applicable (whole case unverified). No global Wind Trap ban. |
| notbuildingresearchlab | Houses 2 and 6 physically complete a Research Lab and start level-2 research. |
| norocketturrets | The original loaded blue/cyan AI is **Neutral, house 6, visual colour slot 6** (RGB 28/164/171), not Atreides slot 1. It must order or build a rocket turret. The saved human/Fighter-shared Sardaukar house is never overridden. A mismatched/ambiguous blue roster is unverified. |

Building installments in `nonuke_lowcash` are the existing progressive construction
payments, independent of the global Technology Installments research/upgrade rule;
this save deliberately keeps that rule OFF.

Atreides no-expand, purple zero-power, Sardaukar LF/expansion and insufficient
rocket cover remain registered but **unadapted/unverified**. Consequently no
genome is certified feasible and promotion remains blocked. An all-not-applicable
case is unverified, not a pass. Tournament strength cannot clear these gates.

Private original saves are checksum-pinned; `--freeze` writes an input INI for
the queue's existing frozen `--game-options` transport. Private save bytes,
frozen inputs and profile copies stay outside git. Context pins the loaded map
size/mod and uses `format=saved`; phases use elapsed continuation time and each
saved controller's actual difficulty, including multiple QuantBots on one house.
Non-QuantBot/shared-human houses are never overridden. An explicit game-rule
gene changes Installments equally for all controllers after load; when absent,
the save's own rule and existing paid countdowns are retained.

For an original save whose pinned Workshop revision is not in the committed
fixture store, add `--freeze-profile ORIGINAL_PROFILE` to the freeze command.
The frozen INI carries its exact manifests/version plus only payload bytes not
already available from checksum-matching source/retired artwork. Restoration
verifies every original file's size and SHA-256; it never substitutes the current
mod, downloads mutable content, or transports profile credentials. The private
norocketturrets input needs this path (revision `d4a80b1c…`, four config files;
902 unchanged files resolve from source).

Snapshots at load/every minute/end and per-cycle changes observe ordinary play:
no planner audit, RNG draw, scripted order or synthetic building site is added.
Accepted orders, physical completions and later combat losses remain distinct;
two controllers cannot duplicate one physical completion. The residential rule
requires shared-budget admission, not forced completion when a higher real drain
wins; it also rejects optional, unexecuted MCV/WOR candidates owning the lane.
Legacy opportunities must have an affordable residential capital candidate.

`games.csv` carries source/genotype/save/input/registry/check/probe receipts,
effective Installments, feasibility and infrastructure error separately.
Per-check CSV and gzip-projected events retain only fields needed by the checks,
plus snapshots, phase hashes and complete effective options/map. Missing,
unfinished or capture-capped evidence is **UNVERIFIED**, including when a partial
trace contains a failure. A fully observed native victory/defeat is a valid
terminal, not a wall-time censor: losing the blue AI before it builds rockets
cannot turn that observed failure into an unverified game. Malformed/truncated
JSONL retains the valid prefix and writes an explicit unverified result rather
than aborting the reporter or silently skipping a corrupt record.
Use at least `--capture-mib 1024` and `--wall-timeout 3600`; ainonuclear has measured
2,250–2,700-second continuations, and busy-cluster reruns use 7,200 seconds.
An infeasible genome is valid measured evidence, not a queue error to retry.

Measured diagnosis from the 2026-11-06 original notbuilding continuation: the
zoning selector is not universally idle. Ordos completed five commercial zones
and Neutral seven before losing their yards; Mercenary reached T4, built its
Stadium and then two residential zones. T2 civic caps lawfully suppress R/I and
some C demand. Do not require every house to grow all three categories before
those unlocks or invent a standalone zoning gene without causal evidence. The
actual failed LF assertion is that Ordos, with a legal offered plot, orders a
Light Factory; the baseline ordered none before its yard was destroyed.

New fixes, including independent heavy-capacity/buffered-production,
ContinuousResearch, CityCompactPower and StartWhatMakesSense, start OFF. Heavy
capacity keys enter the registry only after their genuine parser/logic lands.
Technology Installments is a global human-and-AI payment rule: a whole-game
genome rule shared by both arms, never a per-house switch. Preserve shipped
defaults while collecting the second block.

## Historical reference and promotion

Reuse the historical league's long-form observations and `ai-baseline-gate.py`.
Historical releases (including native 1.0.807 through its test-only bridge), every
accepted prior default and future champions form the Hall of Fame. Historical
**whole-engine** scorecards are a separate track: running a current config is not
running an old AI binary. Exact map/requested options and supported content/rules
must be comparable; absent old telemetry is unavailable, never zero.

A default recommendation requires all of:

1. All required real-issue and synthetic behaviour checks pass on the exact
   candidate source/config; missing/censored evidence is unverified.
   The exact candidate must additionally **win as Brutal Atreides against four
   allied AIPlayer Hard houses within 40 game minutes on every original vanilla
   seed (six), and the separate saved Dune2R gate (seven)**. Survival, draws,
   timeouts, late wins and a default-OFF CTest pass do not qualify a candidate.
   The default CTests remain unchanged. Candidate `scenario-matrix.py` accepts
   `--manifest-b64 INLINE_BASE64 --genome CANDIDATE --qualification-source FULL_SHA`
   only with `--tier full --check` and the unchanged mandatory cells
   `^aaa/(qb-v-4ah/vanilla|qb-v-4ah-saved/Dune2R)$`; use a fresh per-candidate
   results directory. Only the actual Brutal Atreides receives its resolved
   opening/mid/late overlays. Compressed per-seed proof records native roster,
   source/probe and genome hashes, effective options/map, applied phases and
   exact cycle/win time. Missing/stale/wrong-source/wrong-genome receipts remain
   **UNVERIFIED**; a valid loss/draw/game timeout/late win rejects qualification.
   Every serialized effective rule is pinned to the source's compiled defaults,
   unchanged fixture values and declared whole-game gene levels. The complete
   dictionary must match, and its native FNV options hash is recomputed using the
   pinned C++ field order; a partial dictionary or self-reported SHA is not proof.
   Source certification requires explicit `workingTreeModified=false`, no
   fingerprint error, and the v2 fingerprint matching immutable Git blobs.
   `--qualification-smoke` exercises one original seed without qualifying;
   resume the same results directory without this flag for all six plus seven.
   Saved difficulty variants use native enum IDs (Easy 0, Medium 1, Hard 2,
   Brutal 3, Defend 4), never the expression vocabulary's alphabetical order.
   `evolve --qualification RESULTS/games.csv ...` and
   `qualify --plan PLAN --evaluation-rev FULL_SHA --qualification RESULTS/games.csv
   --behaviour BEHAVIOUR/games.csv --output QUALIFICATION.json` enforce these
   native gates alongside issue checks. `qualify` is hard feasibility only,
   never production promotion or a substitute for held-out/history evidence.
2. Same-source paired held-out strength is better than the current default:
   simultaneous 95% lower bound above zero overall, with no meaningful context
   regression (placement tolerance 0.02, not an inferred equivalence from low N).
3. The independent historical gate passes all supported core objectives against
   same-source OFF and the historical references, retaining selection-adjusted
   seed-block intervals and per-metric best historical observations.
4. Report Pareto trade-offs, weakest opponents, niche/context gaps, exact pins,
   traces for harmful effects and the actual recommended chromosome. The
   integrator, not the search runner, changes shipped defaults.

## Offline normal-app trial

For an explicit offline normal-app trial, launch the **app executable** with
`DUNECITY_QUANTBOT_OVERLAY=/absolute/profile/overlay.ini`; optionally use an
isolated `DUNECITY_USERDIR` too. The external file uses ordinary QuantBot
Config.ini sections/keys. The conservative C2 overlay is:

```ini
[Force Concentration]
CityCompactPower=true
FirstFactoryOwnPrerequisites=true
StartWhatMakesSense=false
```

New games and existing saves first resolve their actual mod/pinned Workshop
revision, then read fresh QuantBot defaults, the **normal config source selected
after that resolution**, and this static overlay before any QuantBot constructor/load.
Vanilla retains the profile's `config/QuantBot Config.ini` customizations; if that
file is absent, fresh compiled defaults are used without creating/writing it.
Other mods retain normal mod/fallback config resolution. Other native base values
remain unchanged. Neither the INI nor pinned content is written; saved game options
are unchanged. Missing, unreadable or non-absolute overlay paths abort instead of
silently running baseline. Network/co-op/late-join launches and replays are
refused while the overlay is requested.

One warning-level `[QuantBotTrial]` normal-runtime attestation, visible without
Diagnostic Logs, reports the resolved mod/revision, base/overlay paths, installed
config hash and actual CityCompactPower, StartWhatMakesSense,
FirstFactoryOwnPrerequisites, ContinuousResearch and SimpleTechSavings values.
The owning Game restores the exact previous global config on exit or init/load
failure, before a later lobby hash can use it.

This is a **static global whole-game overlay**, not per-house/phase genome
execution or promotion. Saved QuantBot ratios, strategy/mix and timers are
preserved: no reinitialization or save migration. The attested config hash
describes the installed config, not all saved AI runtime state. C2's dynamic
CompactPower + FirstFactory booleans with SWMS OFF are the intended old-save
trial. The overlay is intentionally not saved/replayed: loading a trial save
again requires the explicit env/file; unset the env for ordinary pinned/base
behaviour. Candidate-native qualification clears this desktop overlay variable
and applies only its independently recorded genome transport.

## Commands and cluster operation

```sh
python3 tests/ai/genome_search.py screen --switches-only --one-alternate \
  --sizes 96 --bases cy --spice medium --research off --modes 1v1 \
  --seeds 110601 110602 110603 110604 --output SCREEN.json.gz
python3 tests/ai/genome_search.py run --plan SCREEN.json.gz --rev FULL_COMMIT_SHA \
  --output RESULTS --tag genome-screen
python3 tests/ai/genome-match.py --report --results RESULTS --scores observations.csv.gz
python3 tests/ai/genome_search.py analyse --plan SCREEN.json.gz \
  --observations observations.csv.gz --output REPORT [--native-equivalent SHA_A SHA_B]
python3 tests/ai/genome_search.py interactions --genes focus_fire converge \
  --output INTERACTIONS.json.gz
python3 tests/ai/genome_search.py evolve --plan SCREEN.json.gz \
  --report REPORT/report.json.gz --payoffs REPORT/payoffs.json --generation 1 \
  --seed-genes city_compact_power continuous_research --race-seeds 110601 110602 \
  --seed-combinations existing_composition_unit_choice,buffered_heavy_production,income_heavy_factories \
  --behaviour BEHAVIOUR/games.csv --evaluation-rev FULL_COMMIT_SHA --output ROUND1.json.gz
python3 tests/ai/genome_search.py race --plan SCREEN.json.gz ROUND1.json.gz \
  --elites ELITE... --seeds EIGHT_HELD_OUT_SEEDS... \
  --contexts dunecity:hard:off dunecity:brutal:off dunecity:brutal:on \
  --output RACE.json.gz
```

Exact-save behaviour input and immutable cluster evaluation (repeat per case;
`--manifest-b64` is the same inline `{schema_version:1,genomes:[...]}` contract):

```sh
python3 tests/ai/genome-behaviors.py --freeze PRIVATE_SAVE --case notbuilding \
  --output PRIVATE_INPUT.ini
dunecity-queue submit --cwd STAGING --rev FULL_COMMIT_SHA --tag genome-behaviour -- \
  python3 tests/ai/genome-behaviors.py --case notbuilding \
  --game-options PRIVATE_INPUT.ini --manifest-b64 INLINE_BASE64 --results RESULTS \
  --capture-mib 1024 --wall-timeout 3600
python3 tests/ai/genome-behaviors.py --report --results RESULTS
```

`--native-equivalent` pools observation sources only after `git diff` proves the
commits differ solely in the genome bookkeeping files (`genome-match.py`,
`genome_search.py`, `genome_model.py`, registry, genome tests, docs); any other
difference, including a Python harness that patches or drives the probe build,
refuses pooling. The proof is stored in the report.

All games use `dunecity-queue submit --rev <immutable SHA>` through the project
coordinator. The scheduler uses mini/Pro/PC and Air only when guest/game/battery
rules permit. Idle filler may consume explicit immutable backlog entries at low
priority; it must not invent repetitions, disable Air's playing guard, perform
promotion or use dirty sources. Read the global compute/disk guard before
submitting. Keep diagnostic evidence gzip-compressed and leased scratch bounded.

`run --emit-backlog BACKLOG.json` exports immutable, once-only four-game 1v1
cohorts rather than submitting. Each entry pins a full SHA, exact plan and
registry digests, inline two-genome manifest, one common seed/context and an
explicit two-house roster. The runner, not the producer, enforces idle admission.
Do not put 16-game FFA cohorts into the four-game idle-filler contract.

The shared native helper is `tests/ai/genome-phases.inc`; frozen-reference probes
can include it without depending on formation-wave measurement. Historical
comparisons must use the reference league's exact frozen fixtures/options, not
assume independently generated maps match.


### Measured first pilot, not a throughput promise

Eighteen default games on `df06ededc5f8080dca98a1b937f475a8d048eefb` completed:
three mods × three difficulties × 1v1/4P, 96-tile CY/no-army starts, spice medium,
seed 110601, 30 game-minute cap. Per-game wall time was 79.725–139.246 seconds,
not the assumed 20 seconds. All Dune2R/City games timed out; two Vanilla 1v1 games
timed out. The recorded multi-job wait was 704.62 seconds including preparation
and shared-machine scheduling, not a controlled throughput benchmark. Evidence:
`outputs/genome-20261106/default-pilot-corrected/`.

### First existing-switch screening snapshot

The committed `b10df0c6` cohort has 61 one-gene variants plus 128 resolution-IV
fractional rows, four common seeds and four 1v1 orientations: **3,024 games** in
Hard/DuneCity, 96-tile CY/no-army, medium spice, research OFF. Other contexts are
separate unsubmitted filler entries, not measured coverage.

The first recorded analysis saw 780 collected game rows with no game error.
All 61 one-gene placement effects were available, but at most **two complete
independent seed blocks**: none has a decisive interval. `CapacityMcvRunway=1`
had a provisional paired ON-minus-OFF placement effect of `+1/2` over two seeds.
That is an exploratory estimate, not a winner, win rate or promotion decision.
The historical and exact-save feasibility gates have not been cleared.

Evidence: `outputs/genome-20261106/screen-interim/`, including
`per-gene-placement-effects.csv.gz`, complete metric/house tables and raw-backed
`report.json.gz`. Transport-lost units are missing evidence, never AI losses.

An exhaustive initial 61-switch/128-row fraction × nine mod/difficulty contexts ×
four seeds × four orientations is 27,216 games before parameter/interaction races.
Therefore the first table is produced for an explicit initial context and then
expanded continuously; it must not imply that unmeasured contexts were tested.
No first promotion date is promised while a required behaviour check fails.

## Literature and adopted methods

- [Rosin & Belew (1997), New Methods for Competitive Coevolution](https://direct.mit.edu/evco/article/5/1/1/790/New-Methods-for-Competitive-Coevolution)
  ([accessible abstract](https://pubmed.ncbi.nlm.nih.gov/10021751),
  [author manuscript](https://www.chrisrosin.com/newmethods.ps)): distinguish
  competitive fitness sharing, diverse opponent sampling and a Hall of Fame.
  Adopt frozen historical/champion memory to guard against cycling/forgetting,
  not a claim that one recent win proves global progress.
- [Vinyals et al., AlphaStar](https://storage.googleapis.com/deepmind-media/research/alphastar/AlphaStar_unformatted.pdf),
  multi-agent learning/PFSP methods: frozen league players, squared-loss opponent
  prioritization and main exploiters. Adopt these opponent-selection principles,
  **not** AlphaStar's neural RL, TPU scale or its exact training mixture.
- [Mouret & Clune (2015), MAP-Elites](https://arxiv.org/abs/1504.04909), especially
  algorithm Figure 2 and section 3: keep high-performing observed behavioural
  niches instead of one global winner. Use a small, interpretable context/style
  archive with Pareto sets; this is a quality-diversity adaptation, not evidence
  that all niches have already been filled.
- [Rakshit, Konar & Das, noisy evolutionary optimization survey](https://www.sciencedirect.com/science/article/abs/pii/S221065021630308X)
  ([accepted manuscript](https://openmole.org/paper/rakshit2016.pdf)), explicit and
  sequential sampling/robust selection; [Merelo et al., noisy game fitness](https://carloscotta.com/papers/merelo14studying.pdf),
  fitness memory and statistical partial orders: adopt common seeds, persistent
  observations, elite re-evaluation/racing and intervals before promotion. Do not
  infer independent replicates from correlated snapshots or favourable retries.
- [Ponsen et al. (2005), AKADS/Wargus](https://cdn.aaai.org/AAAI/2005/IAAI05-012.pdf),
  sections 4–6: parameterized build/research/economy/combat genes and state-specific
  tactics support contextual expression. Its warning about early-rush overfitting
  motivates late-game/quality constraints here; its scalar fitness is not copied.
- [Miles & Louis (2006)](https://www.cse.unr.edu/~miles/papers/cig2006.pdf), sections
  IX–XI: coevolved parameterized RTS decision makers, with Hall-of-Fame/speciation
  discussed as extensions. This is tactical precedent, not proof about full
  QuantBot economy or performance.
- [Stanley & Miikkulainen (2004), NEAT competitive coevolution](https://jair.org/index.php/jair/article/download/10367/24809),
  sections 2.2 and 5.1–5.2: diverse champions, Hall-of-Fame opponents and independent
  dominance evaluation. Adopt the preservation/evaluation lesson, not topology
  evolution: QuantBot's genome is a finite audited feature/parameter representation.
