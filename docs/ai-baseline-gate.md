# Historical AI baseline gate

AI policy changes must clear two independent comparisons: **ON versus the current
same-build OFF**, and **ON versus measured historical releases** on exactly the
same initial maps, rosters, seeds and requested rules. A switch comparison alone
cannot detect cumulative regressions. No scalar "fitness" average may compensate
for a failed behaviour objective.

## Run an audit and gate it

Keep every new policy as an independent, hashed gene, initially OFF. Use
`dunecity-queue submit --cwd "$STAGING" --rev "$COMMIT"` for immutable games;
`agent-workspace run --project dunecity --owner "$OWNER" --tests --wait -- ...`
is the coordinator wrapper. Submit without the queue-side `--wait`, then use
`dunecity-queue wait JOB` outside the coordinator lock. Never compare an
uncommitted source tree with a release-labelled archive.

1. Run `tests/ai/ai-baseline-league.py` twice at one current immutable bridge pin,
   with the policy's committed `--style NAME-off` and `--style NAME-on`. These
   overlays change only native hashed config values; unhashed `[Play Style]`
   behaviours and constructor attack-timer changes are explicitly rejected.
   Requested style bytes and actual per-house config hashes remain in `games.csv`.
   Use separate result directories. A queued genome adapter may instead supply
   `--genome-phase-dir` from commit-pinned inline phase payloads materialized
   inside each private unit. Arbitrary external directories are not queue-frozen.
   The runner snapshots named INI bytes, records their SHA256, clears inherited
   phase environment, and rejects mixing phase inputs with `--style`. The pin
   must include the shared test-only `genome-phases.inc` helper. The ordinary `dunecity-audit.py` and mixed
   genome `results/audit` sidecars remain accepted inputs, but old first-inactivity
   latches cannot certify every terminal FFA rank; missing evidence fails closed.
2. Write a manifest with one entry per result directory, using the pinned source
   commit, native release source, actual research setting and exact fixture
   manifest. Do not relabel a research-OFF game as ON.
3. Normalize observations, then compare both controls:

```sh
python3 tests/ai/ai_baseline_scorecard.py \
  --manifest audit-manifest.json --output audit-observations.csv.gz
python3 tests/ai/ai-baseline-gate.py \
  --history /path/to/history-observations.csv.gz \
  --control /path/to/current-off-observations.csv.gz \
  --candidate /path/to/current-on-observations.csv.gz \
  --output /path/to/baseline-gate
```

Manifest schema (`results`, `fixtures` and optional `options_receipt` resolve as
file paths, not descriptive labels):

```json
{
  "schema_version": 1,
  "runs": [{
    "release": "candidate-COMMIT",
    "source_commit": "FULL_PINNED_COMMIT",
    "baseline_release_source": "FULL_NATIVE_COMMIT",
    "arm": "on",
    "research": "off",
    "mod": "vanilla",
    "format": "audit",
    "results": "/absolute/results/on",
    "fixtures": "/absolute/staging/tests/ai/baseline-fixtures/manifest.json"
  }]
}
```

Use `format: "historical"` for the native league CSVs. When one results directory
contains several mods, supply an explicit `ids` array in each run entry selecting
that mod's exact fixture IDs. Do not silently pool content families. The importable API is
`ai_baseline_scorecard.scorecard(run)`, returning long-form observation rows;
`write_observations` is the common CSV writer. The CLI is standard-library-only
Python 3.9. Source/map/options mismatches and conflicting duplicates are errors,
not excluded samples that can silently improve a candidate.

Exit codes: **0** `DEFAULT_ON_ALLOWED`; **2** `BLOCKED_REGRESSION`; **3**
`INCONCLUSIVE`; **4** invalid input. All nonzero outcomes leave the gene OFF.
`report.json` is authoritative for automation; `report.txt`,
`release-metrics.csv.gz`, `best-historical.csv.gz` and
`paired-comparisons.csv.gz` retain readable and compact numerical evidence.

## Exact initial contexts and historical execution

The fixture manifest is versioned under `tests/ai/baseline-fixtures/` alongside
raw INI map bytes and three full requested-options files (City OFF/ON and classic OFF). Original maps were
recovered from the replay's actual filedata, not regenerated in an old engine.
Additional seed maps are generated once in a pinned current engine and frozen as
exact replay bytes before any historical match is admitted. Map generation runs
are input provenance, not behaviour scorecard results.

The common lane uses size128, CY start, no starting army/city, 3,000 credits,
map technology9, one pathfinding worker and a40-game-minute administrative cap.
DuneCity covers no-spice and medium-spice maps; Vanilla and Dune2R cover medium
spice. Medium, Hard and Brutal, 1v1 and six-house FFA are kept distinct. Every
initial house remains in the denominator, including houses defeated early.

The first four seeds are9101,148680876,55001,77001; independent expansion seeds
are1841712204,8101,8202,8303. Side assignments/rosters are frozen by context,
not inferred from the iteration order of a changed seed list. Multiple game
formats and houses on one seed are not independent trials.

`tests/ai/ai-baseline-league.py` exports the queue's normal
schedule/build_probe/play interfaces. It compiles only a private diagnostic main
against the source's unchanged native engine, AI and content objects. Its
`ai-baseline-observer.inc` reads live/native cumulative ledgers; it does not issue
orders, select units, recalculate targets, inject income, or import modern AI
policies. Games cannot be started directly on the mini through this harness.
A game is decided when every remaining house except team 0 shares one team.
Sources with the hidden Natives house (worms and crime troopers, since
`8dd7d191`) exclude it the same way the engine's own game-over check does:
`House::isAlive()` is always true for Natives, so counting them would keep a
finished 1v1 running to the time limit. Older sources have no Natives house.

```sh
dunecity-queue submit --cwd "$STAGING" --rev "$HISTORICAL_BRIDGE_PIN" \
  --tag historical-ai-league -- python3 tests/ai/ai-baseline-league.py \
  --results /absolute/results/RELEASE \
  --seeds 9101 148680876 55001 77001 1841712204 8101 8202 8303
```

Use one approved shared historical source-only worktree serially; preserve each
native/diagnostic commit pair before moving it. Submit through staging with
`--rev`, never a working-tree overlay. The cluster owns native preparation,
shared compiler caches, low-priority bounded compilation, per-source parity and
all four host admission/guest rules. The Air must remain held while Stefan is
playing. Do not bypass a host's guard to make a report say "all four".

Old releases have different native content/economy/physics. These are measured
**whole-release contrasts**, not causal evidence against one particular gene
and not live games between two executable versions. Research Lab/Pace, Slow City
Year and Starport Population are absent in early releases: requested unsupported
values are disclosed, never emulated. City Effects is supported and must be ON
for DuneCity and OFF for classic content. Research is OFF in the common lane.
Technology9 is a starting condition, not successful research acquisition.
Actual technology1/research-ON progression is a separate supported lane: the
manifest contains 192 common OFF contexts on48 map payloads and96 City ON
contexts on32 technology1 map payloads. Select the latter with `--research on
--mod dunecity`; old native releases reject ON rather than silently running OFF.
The same original eight-seed order is retained. Absent historical support cannot
be filled with zero, technology9, or an OFF run. `--formats 1v1 --spice medium`
selects a bounded exact-map lane; add `--difficulties` and `--seeds` without
regenerating rosters. For example three difficulties and two seeds there mean
six games, not six independent seeds.

## Independent behaviour scorecard

Every observation carries schema version, release label, diagnostic source pin,
native source pin, arm, exact context, seed, content family, difficulty, research
setting, house/team, full raw map/options SHA256, native content/object-data
receipts, metric, minute, value, status and reason. Summary observations use
`minute=-1`. Status is `measured`, `unavailable`, `censored`, or `not_applicable`.
The context SHA256 hashes compact sorted-key JSON of
`{id,mod,difficulty,research,seed}`; the exact raw map has its own independent
receipt. No missing measurement is converted to zero.

| Family | Measurement and interpretation |
|---|---|
| Strength | Native wins and team placement versus other AI houses, kill value, survival/base losses. A timeout is not a material win. |
| Initiative | Exact nonempty native `ground_hunt` dispatches, first-attack indicator and minute; complete callback capture is required for an exact zero. |
| Army | Present-owner healthy mobile-combat replacement value over the actual observed timeline, including inactivity and revival, plus fixed-minute checkpoints. Observed stocks always outrank synthetic zeros; post-horizon observations are censored unless an explicit native-declared-defeat convention applies. |
| Production technology | LF/HF/IX/Starport/Palace adoption from native cumulative completions or proven ownership. Exact per-tick first ownership/completion is separate from first sampled observation. Unreached timing is censored, not-1 minutes. |
| Special units | Native lifetime Sonic/Devastator/Deviator completions/deliveries and present-owner replacement-value share. Native built/lost ledgers are not money spent, nor are they current ownership after deviation. |
| Economy | Harvesters/refineries, credits/idle-money observations, economy-stall exposure fraction, structural/base losses. Low cash by itself is not a stall. |
| City | R/I/C live and native-built counts, population checkpoints and supported research level/progression. Disabled city/research observations are not applicable. |

The economy-stall predicate retains the existing audit rule: a living yard-owning
house with cash below1,000 and no increase in cumulative structures built or
population for four game minutes. Minute samples bound observations; they are
not per-tick payment accounting. Exact native technology completion times are
observed each simulation tick. Native-declared defeat before an event is a
competing event; temporary inventory inactivity is not. First-attack timing is
conditional on an actual attack and must be read with its indicator/coverage.

`House::isAlive()` is a current inventory predicate, not an irreversible death
latch: a late arrival can reactivate an otherwise empty house. The native
observer therefore records `first_inactive_cycle`, resettable
`terminal_inactive_since_cycle`, exact `revival_count`, and cumulative
`inactive_cycles`. First inactivity remains diagnostic. Full-horizon actual
samples supply means, money, population, harvesters and army exposure. A timeout
house that is currently inactive still has administrative follow-up to the game
end, not a fictitious permanent-death time. The last observed inactive episode
can rank final inactive teams; it does not assert anything after the horizon.
Legacy `eliminated_cycle`/`eliminated_s` first-false latches never become terminal
proof: identifiable final standings remain usable, but unresolved old multi-team
FFA placement/score is explicitly unavailable. Exact snapshots through a revival
are never overwritten by post-first-inactivity zeros.

## Statistical and promotion rules

A metric's historical best is selected separately for each comparable
house/difficulty/mod/research/scenario, not declared in advance to be807 and not
collapsed into an overall winner. Every historical release remains in the gate,
including releases that are not the point-estimate winner. Historical selection
must not let an unadjusted interval certify the winner of many noisy trials.

Paired values are averaged within each independent seed block before inference.
Repeated snapshots, opposing houses and alternate formats are never counted as
independent seed trials. Deterministic paired bootstrap intervals resample the
same seed jointly across objectives/releases; simultaneous max-standardized
bounds protect historical selection within seed-set strata, and distinct
seed-set strata receive multiplicity control. Reference means, intervals,
coverage and all source pins are retained. Adjacent chronological release
contrasts identify measured regression intervals; they do not by themselves
prove a causal commit.

Promotion requires complete matched applicable core evidence and at least
**eight independent seeds**; asking `--min-seeds 4` cannot lower the hard safety
floor. Both same-source OFF and every applicable historical reference must be
noninferior on every core objective. Practical margins fixed before candidate
selection: win score 0.05 absolute; kill value, dispatches, army time mean,
aggregate/per-type special builds, City R/I/C counts and population 10% relative
to the reference; adoption 0.10 absolute; individual special-unit value shares
0.05 absolute; supported research level 0.25 absolute; economy-stall and base-loss
fractions 0.05 absolute (lower is better). All 23 hard objectives use final/summary
minute=-1; fixed-minute checkpoints remain visible diagnostics. A zero-army
value-share denominator is undefined, not a fabricated zero or a passing subset.
The confidence interval for direction-adjusted benefit plus margin must be wholly
nonnegative to pass. Relative margins are fixed plug-in constants: the stated
fraction of the mean absolute reference value over the matched seed blocks. Their estimation
uncertainty is not propagated, so an arm identical to its reference passes and
the slack interval is the direction-adjusted delta interval shifted by the
margin. A wholly negative interval blocks promotion; an interval
crossing zero is inconclusive, not evidence of equivalence. Core unsupported,
unavailable, incomplete or selectively intersected seed coverage cannot pass.
Research-ON without a supported historical-ON comparator is explicitly
`NO_HISTORICAL_RESEARCH_COVERAGE`, never a borrowed807OFF comparison.

## Frozen measured reference and regression record

A reference consists of the immutable release/native-bridge manifest, compact
observations, per-metric historical best, host/source receipts and measured
release table. A partially completed league is not an authorized reference.
Regenerating a reference must retain the previous reference and its full
provenance; never replace a bad historical comparison with a switch-only pass.
The measured reference inventory and numerical results are linked here only
after the corresponding pinned games have completed.
