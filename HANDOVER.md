# 2026-10-03 — Long-stall carryall rescue and DuneCity default (1.0.799)

Eligible active ground units with an outstanding movement intent now request an
owned free carryall after 30 seconds / 1875 simulation cycles on the same tile.
The clock survives clearPath, queued searches and identical AI route reissues.
A changed tile or job resets it; accepted explicit human movement/attack orders
reset it through synchronized Command execution even when coordinates repeat.
Stopped, parked, inactive, carried and physically moving units are excluded.
Short blocked trips qualify too. Existing pickup, nearby legal landing and
repair-yard priority rules remain authoritative; destination arrival still needs
an available aircraft and a legal landing area.

The first automatic attempt and retries use deterministic per-unit phases.
Unavailable transport leaves the original mode/navigation/combat intact and
retries after roughly five seconds plus jitter. Explicit carryall orders keep
transport-wait intent and bypass an earlier automatic cooldown. Candidate lookup
scans a derived carrier-ID index in unitList order, validates ownership/activity/
health/booking and excludes temporary delivery flights. Creation/removal and
checkpoint restoration invalidate the index, including equal-count replacements.
Stop or a new player movement/attack order releases both sides of a pickup
immediately, preventing a cancelled assignment from surviving same-cycle rebooking.

DuneCity's bundled Manual Carryall Drops default is true. Explicit per-mod off
choices and saved match rules remain effective. Saving mod options now writes
only that mod's section; it no longer exports the mod's effective rules into the
Vanilla global defaults. Vanilla, Tornie and Dune2R defaults are unchanged.
Ordinary save format remains 9850 with a fresh rescue grace period on load.
Observer runtime 7 serializes the rescue timer/intent; protocol 47 separates
these simulation decisions from older peers. All three source version files are 799.

Codex delegated the bounded source investigation and implementation to local
subscription-authenticated Claude Code, then reviewed and integrated command
validation, reciprocal cancellation, lifecycle invalidation and first-attempt
spreading. Focused real-engine checks pass in Vanilla and DuneCity (802 checks
per mode), including queued/cleared paths, moving-target destination churn,
invalid/unauthorized orders, a two-tile stall, unavailable/busy/enemy/delivery
carriers, same-cycle replacement/rebooking, complete pickup/drop and repair
priority. Disabling only the new fallback in a private negative-control binary
fails the expected eligible-stalled-unit booking assertion. No production object
was replaced for the negative control. Existing flight tests cover all three modes.

The captured MBA save/mod was replayed for 4000 ticks with fixed 5000-node budget:

| Measurement | 1.0.798 | 1.0.799 |
| --- | ---: | ---: |
| Initially ordered local cohort changing tiles / 900 | 511 | 528 |
| Distinct units observed as carryall cargo | 737 | 828 |
| Final queued paths | 2642 | 2616 |
| Mean time inside simulation updates (ms) | 7.637 | 8.008 |
| p99 update time (ms) | 37.565 | 38.101 |

Cargo counts include 118 units already carried at load and all existing transport,
not only new rescues. Tile changes include transport and do not prove arrival.
Timings are steady-clock durations on claw.local including worker waits/internal
logging, excluding loading/compiling/extra observations; they are not MBA FPS or
process CPU. Later workloads differ with gameplay. This adds recovery rather than
a measured FPS gain and does not clear every large-army stall.

One versus four workers produced all 41 identical digests, identical gameplay
save bytes and identical final observer runtime bytes. The path budget/traffic/
inputs/control/observer/parser/ordinary-load/cleanup probe passes; restored
120-tick tails have identical states and saved bytes. Dependency audits and native
Release builds pass, with source version agreement. Full regression and guarded
MBA installation are being completed; no push or public release.
Evidence: ../outputs/stuck-carryall-799/ and ../outputs/carryall-install-799/.

# 2026-10-03 — 1.0.798 installed on the MBA

At the user's request, installed the tested code commit
`eaa25981990f0d3084c68106d308e23e490b80d4` at `/Applications/dunecity.app` on
`Stefans-MacBook-Air.local`. The installer checked that no game was running before
staging and again before replacing the app; no game was stopped or match launched.
The package includes the 1.0.797 police capacity fix and 1.0.798 pathfinding changes
described below. Shared navigation remains unimplemented.

Fresh `cmake --install` bundled all runtime dependencies. The package's 36 ARM64
Mach-O files have portable library references. Deep/strict signatures and bundled
SDL initialization/hidden-window rendering passed locally, during the guarded
installation and independently from the installed app. Installed version and
executable SHA-256 were independently verified:
`b8c1ee655f50c8843f2879eab94b88880cc2cbe8cc418625518e8ef41d88075c`.

All 1776 profile save/INI files remained byte-identical. The previous verified
1.0.796 app is retained at
`/Applications/.dunecity-backup-before-798-20261003/dunecity.app` (executable SHA-256
`1bb8574583e286df42eb68cc2df92a5e507d60a0de41b929ffe53ef9b4979db5`).
Receipts, portable package and independent verification are in
`/Users/stefan/Documents/projects/outputs/path-install-798/`.
This was a private MBA installation; nothing was pushed or publicly released.

# 2026-10-03 — Large-army path backlog (local 1.0.798)

The MBA's installed 1.0.796 match was captured as `path-stuck.dls` at cycle 75937,
with exact pinned DuneCity mod revision `c8d6d3bed38f1cd8`. Live logs showed about
5880 units, 3320–3440 queued paths and saturation of the 5000-node budget. The save
SHA256 is `cdfe1a141a228d50cbba1779f8a9297c7a580b64ad353fd00c62fb2da1ce3cb8`.
This patch is local; the MBA installation and production release were not replaced.

Ground-unit tile assignment/removal no longer changes the global pathing revision.
Structure placement/destruction, mountain transitions and repaired zone footprints
still invalidate it. Long-route prefix validation now records its revision. Every
actual step still checks live collision rules. AStar accepts an adjacent approach
when its original goal cannot be entered, retaining the original goal and cache /
fingerprint identity; free booked refinery and repair-yard entries remain exact.
Both harvester classes exclude pending searches from blocked-refinery failures.
No shared flow fields, formations, local-detour system or adaptive-budget changes
are included. The remaining congestion needs shared strategic navigation with
local collision handling and nearby arrival positions, rather than a dependency
on a single squad leader.

Protocol 46 separates the changed navigation/patrol rules from older peers.
Ordinary save format 9850 and observer runtime 6 are unchanged; no fields were added.
The native Release build and source metadata are 1.0.798.

Private replay: same save/mod, 2000 ticks, four workers, fixed 5000 nodes/tick.
The reference was local 1.0.797 (same pathfinder as public 796, with its police fix).

| Measurement | Reference | 1.0.798 |
| --- | ---: | ---: |
| Applied search results (including empty routes) | 3817 | 8648 |
| Final queued paths | 3375 | 2618 |
| Initial local cohort that changed tiles / 900 | 383 | 472 |
| Time inside simulation updates (seconds) | 22.803 | 15.323 |

These are steady-clock update durations on claw.local, including worker waits and
internal logging, excluding compilation/loading and extra observer scans. They are
not MBA FPS or aggregate process CPU time. Different movement produces different
later gameplay workloads. Cohort movement means at least one tile change, including
possible transport, not destination arrival. Tripling the original budget alone
moved 476/900 and took 26.416 seconds; the implemented fixes achieve similar cohort
progress with the original budget. The remaining queue means this is a measured
first improvement, not a claim that all large-army stalls are eliminated.

Validation: 46 CTest targets passed in the full run; the unit target passed after
its independently pinned protocol expectation was intentionally updated 45→46.
Three optional live-download/GPU unit cases stayed skipped. Native build, source
version agreement, Ninja dependency audits and whitespace checks passed.
The real-engine path probe passed strict budget, lifecycle, cancellation, terrain
churn, ordinary mid-search load, observer continuation/control and cleanup checks
(480 static slice/whole comparisons; 7,456,537 diagnostic assertions overall).
Controlled production vehicles retained routes through future traffic, rejected
an occupied immediate step, invalidated routes for actual walls/mountains, and
reached free refinery/repair-yard entry tiles. The pending-harvester counter was
exercised in the captured DuneCity mode; the parallel RebelHarvester check runs
only for a Tornie save, so its new guard was source-reviewed here.

One and four workers, including deliberately delayed four-worker completion,
matched all 21 checkpoints and final gameplay-save bytes over 2000 ticks. The
comparator excludes only the release-label field. Saved-state SHA256:
`2b3ddbd8b3e47b87181ae62f9c25cb9ae4bfa2827980a8ec689ca0ba1bce912e`.
Reference AStar checks sampled eight units per type: 728 queries, 15 types, three
rounds. Enterable goals kept exact routes/node counts; blocked goals saved 292960
nodes in the first round. Unchanged unreachable fallback routes remain permitted.
Restoring the previous Tile, AStar or Harvester implementation individually in a
private binary reproduced its corresponding new traffic/cost/pending-counter
failure; the final traffic fixture passed all 244 checks.

The first traffic fixture allowed unrelated combat/transport to change its subject.
It was replaced with controlled production vehicles, and fixture relocation now
releases prior tile occupancy before assigning the new position. The initial goal
cost bound also passed baseline; the final bound requires no more expansions than
the otherwise identical free goal (119 versus 132; baseline blocked goal 245).

Subscription delegation used Claude session `5fd94ace-d1a6-4eaf-bd46-28822b35b48e`
for source investigation/implementation. Its narrow shell permissions blocked
preparatory commands, so it claimed no runtime checks. Codex reviewed/integrated,
corrected the fixtures and ran all validation. Captures, scripts, raw measurements
and the diagnosis are in `/Users/stefan/Documents/projects/outputs/path-stall-20261003/`.

# 2026-10-03 — Police unit capacity fix (local 1.0.797)

The user requested that police must not claim "Unit limit reached" below the game's
max-units limit. PoliceStation had a separate 250-raw-military-unit ceiling, counting
individual infantry one-for-one rather than the engine's thirds, and combined that
ceiling with Brutal's army-value gate for both deployment and the sidebar label.

The separate 250 ceiling is removed. The station now queries House's existing Trike
and Trooper admission policies; a partial patrol remains available while either
category has room. Every deployed member is checked again against House capacity.
Effective default, zero/unlimited, positive override and shared-Hard count policies
remain owned by House. The Brutal army-value gate remains independent and the actual
sidebar identifies it as "Army target reached" instead of claiming a unit limit.
Cooldown, disabled catalogue items, deployment space, police funding and charges
retain their prior rules. No save fields changed; format remains 9850.

Protocol 45 separates peers whose automatic/commanded police patrols can differ.
Source metadata and the rebuilt native app are 1.0.797. This is a local development
build: production remains 1.0.796 and the MBA installation was not replaced.

Validation uses the real-engine `police_reinforcement_limit` CTest fixture, including
actual PoliceStationInterface widgets (not source-string matching). Restoring the
old 250 predicate reproduced a false unit-limit result below the selected cap;
restoring the fix passed. Boundary checks cover 300+ individual troopers, default
and explicit-positive limits, exact-cap blocking, zero/unlimited, one remaining
vehicle slot, two remaining infantry slots, the separate Brutal target label,
cooldowns, disabled catalogue and blocked deployment space. A blocked patrol
retains its ready cooldown. Synthetic fixture counts are restored before exit.
An initial blocked-space fixture shared half-full infantry tiles with earlier
patrols; an independent clear pocket plus structure-blocked rings corrected the
fixture without changing movement rules.

The delegated worker's 15 affected/adjacent CTest targets passed (unit, lobby,
network wire, commands, Brutal override, police placement/budget/reinforcement,
city growth, MCV/custom attack, Starport, degradation, credit storage and stats).
After review replaced two source-contract assertions with actual sidebar checks
and added partial-patrol boundaries, the full unit target and police reinforcement
target passed again. Native Release build, version agreement, Ninja dependency
audits and diff whitespace checks passed. This is targeted verification, not a
claim that all 47 registered CTest targets were rerun for this patch.

Standing subscription delegation was honoured with Claude Max session
7f57c13b-366d-420c-a470-db54d27ce608; Codex reviewed/integrated the patch, actual UI
checks, partial-patrol checks and protocol/version metadata. Evidence is in
`/Users/stefan/Documents/projects/outputs/police-unit-limit-797/`.

# 1.0.796 full release preparation (2026-10-03)

The authorized production release combines the private 1.0.793–795 performance,
repair and Brutal unit-override changes with current main's city-art refresh.
Version metadata is 1.0.796; multiplayer protocol remains 44 and save format 9850.

The five maps requested from the MBA are bundled in `data/maps/singleplayer/`:
`4P - 128x128 - 4 corners.ini`, `4P - 192x192 - DuneCity.ini`,
`5P - 256x256 - test.ini`, `8P - 128x128 - city seige.ini` and
`Alkozeltser 4 Cities.ini`. Four files are newly bundled; Four Corners replaces
its older bundled contents with the exact MBA revision (additional starting
Deviators and catalogue v1 metadata). Alkozeltser retains its author, CC-BY-SA
license, six houses and catalogue v1; its name refers to cities, not player count.
All five source files are copied byte-for-byte, without installing user-profile
workshop sidecars or depending on a remote cached mod. Existing category logic
recognizes their city structures or sparse city starter layout.
City seige's authored `8P` name includes Neutral; the existing catalogue correctly
reports seven selectable houses. The initial new test assumed eight from the name
and failed locally and in Linux CI; its expected count was corrected to seven
without altering map bytes or the established classifier.

Native builds copy the whole data directory; Windows/Linux installation does
likewise, and Emscripten preloads it at `/`. The browser payload checker now
requires all five maps to match the tagged source bytes, with missing/corrupt
map rejection tests. Catalogue tests verify names, map dimensions, house counts,
revision labels and DuneCity category/dependency from the actual map INIs.

Validation: rebuilt the native Release app, passed dependency audits before and
after the build, verified all five bundled-map SHA256s against the MBA capture,
and passed all eight browser payload-checker tests. The required full CTest
suite and public release gates are in progress; this entry is preparation,
not a claim of production publication. Captures, provenance and release evidence
are under `/Users/stefan/Documents/projects/outputs/release-796/`.

## 2026-10-02 — Repair release and Brutal unit overrides (local 1.0.795)

The MBA repair-stuck save at cycle 46155 contained 47 occupied yards across six
houses, including 12 Harkonnen yards. Every occupant had fractional health less
than one HP below maximum and no booked carryall. All 47 retained identical
health and occupancy after 1000 ticks in both 1.0.794 and the retained pre-performance
1.0.792 engine. ObjectBase::addHealth tried health+1; setHealth rejected the
overshoot, so repair never reached its release branch. INIMapLoader can create
fractional unit health from the map's health/256 ratio.

The healing step now clamps at maximum, preserving setter validation, fixed-point
arithmetic, normal healing cadence and the per-tick cost. The final combined replay
released all 47 original occupants alive at exact maximum health with unchanged
ownership within 758 ticks. One and four path workers
matched all 11 checkpoints and complete saved bytes over 1000 ticks. Those are
bounded checks with this save and available funds/access/transport, not universal
handoff guarantees.

The user also requested a performance stress setting: Brutal with an explicit
max-units override bypasses its configured military-value ceiling. Positive
values use the existing engine unit-count policy; zero is unlimited for that
policy. The existing per-category formula remains (including the air allowance
computed from the selected ground limit); this is not a new global total. A Hard
controller sharing a house retains its existing house-level count exemption.
Disabled override and other difficulties retain their previous behavior.
Military production remains subject to funds, reserves, catalogue/prerequisites
and engine admission. Review removed an arbitrary INT_MAX/4 budget ceiling and
widened allocation sums; only the int representation boundary saturates. Free
police reinforcement is outside this funded-production change. The effective planning budget is derived at runtime; the serialized
militaryValueLimit and save9850 field order remain. Protocol44 separates peers
with these changed repair and production rules; observer runtime remains v6.

### Property and evidence matrix

Verification follows /Users/stefan/.codex/skills/verify-system-invariants/SKILL.md.
These are source reviews and executable tests, not formal proofs.

| Property | Enforcement/source | Evidence/result | Assumptions or limits |
| --- | --- | --- | --- |
| A funded fractional repair reaches exact maximum without overshoot | ObjectBase::addHealth; RepairYard::updateStructureSpecificStuff | Old-code regression failed; fixed regression passed Vanilla/DuneCity/Dune2R; 47/47 captured jobs released | Yard receives updates and can pay the existing per-tick charge |
| Release clears one repair booking and retains ownership; one final healing tick costs UNIT_REPAIRCOST | RepairYardJob::finish; RepairYard::deployRepairUnit; House::payCredits | Existing cleanup/ownership coverage and final exact cost/ownership assertions passed in all three modes | Ground-release fixture has a valid nearby deploy spot and funded owner |
| Brutal bypasses the configured value cap only for explicit override | QuantBot::overridesMilitaryValueCap; planningMilitaryBudget; funded production sites | Helper boundaries and real-engine -1/0/positive/other-difficulty matrix passed in Vanilla/DuneCity/Dune2R | -1 means disabled; positive/0 are explicit; no new serialized policy |
| Positive overrides are enforced by the engine; zero is unlimited for unit policy | QuantBot::ignoresUnitCountLimit; House::getMaxUnits; BuilderBase progress; StarPort delivery guard | Heavy/air/import below/at-cap and zero checks passed; direct builder progress blocked at cap; paid imports hold and resume exactly once | Existing per-category formulas remain; a shared Hard controller retains its house-level exemption |
| Spending, prerequisites and queued military accounting remain independent | FundedArmyTarget/allocation helpers; QuantBot build and Starport guards | Source guards retained; final real-engine aircraft/Starport tests passed; nominal queued value counted and discounted imports not reserved twice | No promise of production with insufficient money, prerequisites or capacity |
| Worker completion order does not change fixed replay state | Deterministic path scheduler; simulation ticks | Final source: 11 checkpoints and raw full save equal with 1/4 workers; override0 stress also matched gameplay bytes (local camera field excluded) | Fixed initial save, settings and tick count; override intentionally changes army growth |
| Earlier simulation peers cannot join protocol44 | NetworkPacketPolicy; NetworkManagerTestCase | Full final CTest 46/46 passed, including unit/lobby/network/relay gates | Every multiplayer peer needs the updated rules |

Paid Starport units under a Brutal explicit-positive override now wait at the
execution layer if their category filled while the shipment was in transit. The
existing two-second deployment timer retries the front entry without popping it,
spawning, charging or refunding. A single available slot releases exactly one unit
and then blocks the next. Disabled/zero/other-difficulty/non-unit paths retain
prior behavior; the existing shared-Hard house exemption remains. No new save
state or unbounded delivery deadline was added.

The retained unregistered campaign-pressure diagnostic reached its controller
count checks but failed a separate Easy/Medium power-deficit setup with the trial
level9/vanilla/harvester-limit7 invocation. It is not used as passing evidence;
the registered override fixture covers the per-difficulty count matrix in three
mods. Its existing classic-count block explicitly disables the override now.

Transport booking/cancellation/timeout logic was not changed. A surviving
carryall whose assignment is stale can still cause an unrelated wait; this was
not the captured defect, whose 47 occupants had no booked carrier. No unbounded
transport liveness claim or unrelated carryall refactor is made.

Final native Release build and dependency audits passed. Full CTest: 46/46 passed
in 567.21s. Registered override tests cover heavy, aircraft, imports, paid queue
accounting, exactly one available delivery slot, fixed-point funds, independent
builder enforcement and unchanged difficulty/default semantics in three mods.

A private override0 replay of the same immutable save advanced 1000 cycles. The
actual QuantBot policy value grew from 98980 to 236620 for Harkonnen (100000
configured target), with unit count 527 to 778; four houses exceeded the target.
Those policy figures are recomputed at current catalogue prices, not the House
lifetime production accumulator. One/four workers matched all 11 checkpoints and
every saved gameplay byte. An initial strict full-file comparison found a local
camera-center difference from screen shake; serialized object/bullet/explosion
sections locate those four presentation bytes exactly, and same-worker repetition
also varied there. Only that local view field and release label are excluded in
the stress comparison. The unchanged-options repair replay matched raw bytes.
The replay ran alongside validation: its CPU timings are not an FPS benchmark.

Final staged app: 36 ARM64 Mach-O files, portable library loads, deep/strict
signature and bundled SDL initialization/hidden rendering passed. Executable SHA:
bffaf429b8b1aee2165931fccff5bee21040927b98664d86599a212089c92e3a.
Guarded MBA installation completed with no running game, followed by independent
installed-version/hash/signature verification. /Applications/dunecity.app is
1.0.795 from code commit 20439671150da4652683f2a7cd8f54b9180baa00 with the SHA above.
Bundled SDL initialization and hidden rendering passed on the MBA. Settings stayed
byte-identical. Prior 1.0.794 app retained at
/Applications/.dunecity-backup-before-795-20261002/dunecity.app (SHA
c3408696f898d8920ab44cdb83992e83199773ee5d0cbe38119f26f4afde7ba9).
Receipt and independent verification are in ../outputs/repair-install-795/.
No match was launched, and no public release/push/PR was performed.

For a subsequent controlled large-army performance test, check Maximum Number of
Units Override and use a positive value or 0. The captured repair save stores -1,
so its normal military budget remains unless a new fixture/lobby selects an
override. Compare the same save, mod revision, settings, tick workload and hardware;
record unit counts and frame-time tails as well as average FPS.

Root proofs: root-final-repair-proof.json and root-final-stress-proof.json.
Evidence: ../outputs/repair-yard-live-20261002/ and ../outputs/repair-install-795/.

## 2026-10-02 — AI work phasing and stutter fixes (local 1.0.794)

The live MBA capture showed AI and city simulation causing long frames while
pathfinding averaged only 1.49ms/frame. Low FPS also reduced the single-player
path budget to 5000 tokens, increasing queued paths and movement pauses.

### Final implementation

- Custom single-player QuantBot unit management and building planning run on
  separate fixed simulation phases, 25 cycles apart, each retaining its original
  50-cycle cadence. House phases are distributed over the interval: houses 0 and
  4 are now 16 cycles apart instead of 4. The entire build timer action moves to
  the building phase, including decrement and reset; military value is evaluated
  from the live world. No frame clock, worker completion, pending cursor or new
  serialized state determines this schedule.
- Phasing is excluded for network launches, including loading a CustomGame save
  into LoadMultiplayer. Campaign, skirmish and multiplayer retain the original
  AI timing. Custom single-player decisions now occur at different cycles and
  can change the match trajectory; this is an intentional timing change.
- Building searches skip candidates outside the exact construction range using
  a summed-area ownership field. One lazily built field serves a build invocation
  and is invalidated before placement/demolition, including their callbacks.
  An RAII guard clears it on every exit. Reservations and builder changes still
  participate in the original checks; full-map scoring and tie order remain.
- Harvester crowding uses the exact integer falloff stamped into a spatial field.
  An ordered spice list is shared only within one unit-management invocation.
  Passability, danger, unsafe-field memory and other harvesters' reservations are
  checked live. MCV handling invalidates spice membership conservatively.
- Expansion cover gathers the main-yard and living turret facts once. Both
  defence gates around the rock survey remain. The survey uses the same exact
  ownership field instead of repeated neighborhood scans.
- Hostile land-value penalties use a spatial index with the original visibility,
  team and distance checks. Off-map units are retained in edge buckets. Strongest
  penalty accumulation remains a maximum, with no changed city outcome.
- Single-player FPS-driven path-budget reductions require pathfinding itself to
  average at least 4ms/frame. This prevents unrelated stalls starving movement.
  The existing path worker pool and multiplayer budget negotiation remain.
- Diagnostic scopes/work counts distinguish candidate searches, field builds,
  unit/build phases and city effect subphases. Future ownership or spice mutations
  added inside these AI invocations must invalidate their scoped caches.

### Validation and measurements

Same immutable interesting.dls, 5000 engine cycles on the local native build:

| Build | Total CPU time | 99th-percentile cycle | Maximum cycle |
|---|---:|---:|---:|
| Installed 1.0.793 baseline | 43.25s | 159.60ms | 3094.73ms |
| 1.0.794 pure optimizations | 34.63s | 88.96ms | 942.06ms |
| 1.0.794 with SP phasing (root final source) | 32.37s | 74.68ms | 664.54ms |

These are headless replay CPU timings, not MBA FPS or a guarantee of hitch-free
rendering. Phasing changes the trajectory, so its throughput delta also reflects
different gameplay. Warm per-house peaks remain (183ms unit work, 127ms building
work in the sampled replay); city effects reached 45.5ms. Cold unit planning
still reached 703ms immediately after load. No win-rate balance study was run.

Before adding phasing, all 51 checkpoints matched 1.0.793 and the complete save
differed only in its version byte at offset26. After phasing, one versus four path
workers matched all 51 new checkpoints and the complete same-version save.
The existing 9850 save loads; save9850, protocol43 and observerv6 remain.
Scheduler tests cover cadence, real production timer resets, frame chunking and
all network launch markers. Spatial field tests compare the production helpers
against brute-force references, including off-map sources and map edges.

Root final dependency audit and native build passed. Full CTest passed 45/45
(543.50s). The final-source 5000-cycle replay matched all phased-reference
checkpoints and saved state. The ARM64 1.0.794 portable app passed deep/strict
signature verification, all 36 Mach-O dependency audits, and packaged SDL
initialization/hidden-window rendering; no external non-system loads remain.
No push or public release. Evidence is in ../outputs/stutter-ai-implementation-794/.

### Installed on the MBA

Installed source commit 13e054bb79f1123f6a87aaf417e3af5301e1f312 as 1.0.794
at /Applications/dunecity.app on Stefans-MacBook-Air.local. Independent installed
version/hash/deep-strict signature checks passed, and the installed bundled SDL
runtime initialized and rendered successfully. Executable SHA-256:
c3408696f898d8920ab44cdb83992e83199773ee5d0cbe38119f26f4afde7ba9.

The installer checked that no game was running before both staging and swapping.
No game was stopped or match launched. User config bytes stayed identical.
Previous 1.0.793 app is preserved at
/Applications/.dunecity-backup-before-794-20261002/dunecity.app with its verified
9554509cf2dbc5200c743f0037cf29de7d47d1e2567a6c273facfe258436b317 hash.
Receipt, portable package and installed-runtime logs:
../outputs/performance-install-794/. The next normal launch uses this build.


## 2026-10-02 — Performance build installed on the MBA (1.0.793)

At the user's request, installed the tested e8fab5ec performance build at
/Applications/dunecity.app on Stefans-MacBook-Air.local. Native pathfinding
uses up to four workers by default. Fresh cmake --install bundled the runtime
dependencies; all 36 Mach-O files have no Homebrew/workspace load references.
Deep/strict signatures and bundled SDL initialization/hidden rendering passed
both before the swap and from the installed app. Installed executable SHA-256:
9554509cf2dbc5200c743f0037cf29de7d47d1e2567a6c273facfe258436b317.

Previous 1.0.792 app is retained at
/Applications/.dunecity-backup-before-793-20261002/dunecity.app with its original
7ad79f38b65932123485c26f015c4cd8ab2cc8f2a99c0701e4970cfad16e6f86
executable hash. No game was running or stopped; no match was launched and
user saves/config were untouched. The packaging check exits before opening a
profile. Receipt, portable local package and logs:
../outputs/performance-install-793/. Nothing was pushed or publicly released.

## 2026-10-02 — Bounded pathfinding and worker pool (local 1.0.793)

Implements the live MBA performance review's recommendations 2–4, followed by
parallel pathfinding. Active corpse tiles avoid full-map timer scans; generation
stamps avoid full A* scratch resets; build-range checks short-circuit; QuantBot
reuses four distance fields keyed by their actual contributors and avoids
unnecessary small set allocations.

A* searches now retain their frontiers across cycles. Fixed batches of four,
fixed node quotas and fixed result application order enforce the effective
per-cycle budget. Changed orders/targets/control cancel and fairly requeue a
search. Unrelated geometry changes retain the frontier to avoid starvation;
passability refreshes per slice, cached routes validate a near prefix, and
movement checks every committed step against the live world. Thus paths can
reflect earlier geometry observations, while current movement stays checked.

Only frontier advancement runs on persistent workers, after main-thread input
validation and before a mandatory batch barrier. Native default is up to four
workers, bounded by reported hardware concurrency; startup failure and browser
builds without pthreads use the same algorithm inline. Developer overrides:
DUNECITY_PATH_WORKERS=1/2/4 and DUNECITY_PATH_WORKER_DELAY_US. Worker timing never
selects quotas or gameplay decisions. Ordinary save format remains 9850;
observer continuation is v6 and network protocol is 43 (peers must match).

Validation: final native Release dependency audits and all 45 CTest targets pass.
The nine-phase real-engine budget probe passes 3,572,392 assertions, including
bounded work, changing inputs, geometry churn, parser rejection, save/load,
teardown and observer continuation. A real serial-host spectator snapshot with
a dead active unit restores into four workers and matches 60 state and complete
continuation checks plus the complete gameplay save after documented local-view
metadata normalization. Real PHP/WebRTC late join transfers four suspended
searches byte-for-byte and stays in sync through 1,800 cycles. One/two/four-worker
and delayed runs match 21 checkpoints and complete raw saved bytes over 2,000
cycles. Standalone ThreadSanitizer covers the actual pool's lifecycle/barriers
and exception handling; the full engine was not sanitizer-instrumented.

On the MBA, fixed 2,000-cycle simulation cost fell from 21.094 s on 1.0.792 to
13.223–13.299 s with four workers (about 37% less); the optimized one-worker
version took 16.113–16.788 s, so threading adds about 19% less cost. These are
simulation CPU timings, not measured live FPS. A roughly two-second cold AI
spike remains. The non-pthread web branch compiles in isolation; a full Wasm
build remains unverified because the local Emscripten toolchain lacks Binaryen.

Final 1.0.793 MBA check: the default worker setting took 13.746 s on repeat,
versus 16.403 s with one worker (about 16% less), and the explicit four-worker
repeat took 14.601 s. All 21 checkpoints and complete raw saves match across
these final controls; compared with the earlier build, the save differs only
in its recorded version byte (offset 26, 792 to 793). The first candidate run
was a slower 25.889 s outlier, so timings under live desktop load vary; the
repeat default is about 35% below the original 21.094 s baseline.

Build: build/bin/dunecity.app, private development bundle using Homebrew
libraries. MBA private test copy: /tmp/dune-performance-test-20261002/candidate-1.0.793,
with an isolated profile and Launch DuneCity Test.command. The installed
/Applications/dunecity.app stays at 1.0.792; no user profile was changed and
nothing was pushed or published. Full evidence is outside the checkout at
../outputs/dune-performance-implementation/; final-report.md supersedes the
worker's earlier phase2-results.md checkpoint blockers.

## 2026-09-29 — Original Dune II damage option (local 1.0.792)

User resolved the historical choice: checked means original Dune II/Dynasty
classic ordinary ground damage, not pre-2013 Legacy. Default remains the
approved hybrid: classic Soldier through Quad and lower City heavy damage.
See `docs/combat-damage.md` for both complete per-projectile splash tables.

The checkbox is labelled "Original Dune II damage". The custom wrapped hover
overlay uses Tank 12-to-25 and Launcher 37-to-75 examples and explains splash.
Building/air damage and special weapons retain their existing behavior.
Both modes use base Dynasty splash: max-axis plus half-min-axis distance,
quarter-tile halving, strict one-tile cutoff and whole-number HP truncation.

MOD6 persists the flag through config, saves, multiplayer and campaign setup;
older settings default it off. Save format remains 9850, protocol is now 42.
Validation: all 44 CTest targets pass across the focused unit/projectile runs,
menu run and remaining 41-target regression run. The unit suite has 897 cases
with zero failures and three optional tests skipped. The real-engine probe
measures both complete damage tables, off/on/off behavior, source death and a
saved Ornithopter projectile landing for 38 after settings and bullet reload.
Native build and dependency audits pass. The 1.0.792 app passes hidden-window
SDL initialization/rendering. Tooltip layout passes at 640x480, 854x480 and
1280x720; smallest and largest screenshots were visually checked.

Local development app: `build/bin/dunecity.app` (uses local Homebrew libraries).

Installed on Stefans-MacBook-Air.local at `/Applications/dunecity.app` on
2026-09-29 at the user's request. Fresh `cmake --install` bundled dependencies
and applied an ad-hoc signature. Version 1.0.792, deep/strict signature, bundled
SDL initialization and hidden-window rendering passed after installation.
Installed executable SHA-256:
`7ad79f38b65932123485c26f015c4cd8ab2cc8f2a99c0701e4970cfad16e6f86`.
Previous 1.0.790 retained at `/Applications/.dunecity-backup-before-792/dunecity.app`.
No game was running, no match was launched and no user profile was changed.
Package: `../outputs/damage-install-792/DuneCity-1.0.792.zip`. No publication.

## 2026-09-27 — Measured Deviator rewards (local 1.0.790)

Replaces the old instant conversion estimate in the active QuantBot learning
ledger. A captured hostile unit's damage and unit killing blows now pay the
controller's Deviator bucket: target price times actual HP removed / maximum HP,
plus 20% of target price for a unit kill. Hostile damage absorbed while controlled
also pays the Deviator, without a kill bonus. Refreshing control alone earns zero.
Friendly, self and environmental damage do not earn absorption credit.

QuantBot arms captured hostile Devastators on its first eligible unit scan,
before rally, support and campaign orders. The existing 200-cycle fuse remains.
Completed detonation pays remaining HP value and one 20% Devastator kill bonus;
hostile blast damage and kills are additional. Self-blast cannot duplicate the
terminal reward. Allied-origin and human-controlled Devastators are excluded.

Projectiles, persistent flames and armed fuses retain firing/arming attribution
through source death, reversion, recapture and save/load. Telemetry declares
`deviation_reward_version: 1` and separates outgoing, absorbed and terminal value.
Save format is 9849; network protocol is 40. Loading an older save resets the
whole unit learning ledger and cached learning window, so new measured rewards
and loss denominators start together. Historical overall match totals and
structure losses used by build prerequisites are preserved. Already controlled
units begin measured intervals at load. Old in-flight projectiles/fuses lack
historical attribution and earn no outgoing reward; their physical damage remains.
Old matches cannot be retroactively measured from their conversion-proxy logs.

Includes the earlier 80% aircraft conversion, map revision labels and no-repair
changes. The new real-engine scoring probe passes 38 decisions in Vanilla,
Dune City and Dune2R, including saved fuses, single terminal credit, hostile
absorption, killing blows, source death/reversion/recapture and legacy attribution.
An independent 50-HP reverted Devastator probe now earns exactly 260 credits,
not the erroneous 520, in all three modes. Native build and dependency audits pass.
All 44 CTest targets pass: 41 in the full run, followed by the three stats probes
after updating their synthetic 9840 House records to omit the new counter block.
The old-projectile fixture similarly omits the new 24-byte attribution block.

The portable app is staged on the MBA at
~/Downloads/DuneCity-1.0.790-staged/dunecity.app. Signature, bundled runtime,
hidden rendering, version and executable SHA-256 were verified there. The
installed /Applications/dunecity.app remains 1.0.788 per the user's stage-only
instruction. No running game was stopped and nothing was published.

## 2026-09-27 — Deviated units cannot seek repairs (local 1.0.789)

Shared repair eligibility rejects temporarily deviated units for explicit/automatic
repair orders, yard bookings and Carryall delivery. Conversion cancels inherited
pickup requests. QuantBot excludes these units from repair reservations and repair
demand; QuantBot and Mentat use their combat fallback instead of issuing a rejected
repair order. Ordinary repairs resume after ownership reverts. Protocol is 39;
save format is unchanged.

Native build and six focused CTest targets passed: unit suite, Carryall repair
regression, gas combat regression, and QuantBot air-defense/repair reservation
probes in Vanilla, Dune City and Dune2R. Packaged app passed signature and hidden
rendering verification on the MacBook Air. Per user request it is staged at
~/Downloads/DuneCity-1.0.789-staged/dunecity.app on the MBA; /Applications remains
1.0.788 and the running game was not interrupted. No publication.

MBA match 1790508268440308-0 was analysed from a completed telemetry copy. The
37-minute 1.0.788 4-corners match began with 22 Deviators per house; Ordos produced
54 more. Rebels also had production availability but built none. Detailed reward
comparison and current conversion-proxy limitations are recorded outside this
repository in ../dunecity-deviator-analysis/mba-1790508268440308/analysis.md.

## 2026-09-27 — Deviator aircraft conversion

Deviators can target and convert hostile Ornithopters and Carryalls (including
Chemical Carryalls). Gas eligibility and its fixed-point 80% conversion chance
are shared by target selection and impact, independent of house or game type.
Frigates, Sandworms, structures and ambient city aircraft remain excluded.
The old custom-game 100% and campaign house-specific probabilities are removed.

Gas rockets use the existing narrow relative-motion aircraft contact check so
an aircraft cannot pass through a gas rocket before its delayed explosion.
Contact triggers the ordinary gas conversion roll, never HP damage. Captured
Carryalls retain passenger ownership and cannot insert hostile cargo into the
captor's refinery or repair yard. Network protocol is 38; save format is unchanged.
The local app version is 1.0.788. Reward attribution and Devastator detonation priority
remain separate planned work.

Validation: all 43 CTest targets passed (42 in the final full run, then the
projectile mechanics target after updating its aircraft gas-contact expectation).
The native build, dependency audit and whitespace checks passed.
The real-engine projectile probe covers
exact seeded success/failure decisions and RNG consumption, actual aircraft
acquisition and conversion, zero HP damage, loaded-cargo preservation, hostile
repair-yard unloading and timed ownership reversion. Results are compared across
Vanilla, Dune City and Dune2R. The local 1.0.788 build also includes map revision
labels from e40bd85f (cherry-picked as fe8bc8ee). Installed to
/Applications/DuneCity.app; not published. Version metadata, installed bundle
checksums and signature verified. Unit, map collection and menu navigation
checks passed again after integration; menu assertions now require revision labels.

## 2026-09-27 — Fresh guest multiplayer map reception

Fixed a live 1.0.783 join failure on current main a676d235. The guest's map
metadata writer required a mod revision that is resolved only after map reception.
It now omits the optional catalogue hint until that revision is available, while
the immutable manifest still pins the exact dependency and normal lobby checks
remain in place. The real SENDGAMEINFO regression fails against the old native
object with the observed Workshop error and passes with the fix, including
altered-byte and mismatched-dependency rejection and later mod activation.

An unchanged 1.0.786 browser host and a fresh patched browser guest reached
gameplay through the real local PHP service. Two-way chat, guest movement, clean
guest exit and map/metadata persistence after reload passed. Native and browser
builds, dependency/package checks and all 43 CTest targets pass.
See docs/multiplayer-fresh-guest.md
and ../outputs/multiplayer-check-20260927/ for evidence. This is a local fix;
no production release or installed-app replacement has been performed.

## 2026-09-26 — Graphics PR integration candidate (1.0.786)

Clean integration branch from main 6d8e2a92 replays PR77 (bb88766b, bb2c1481)
and ports the useful PR75-only cache recovery and industrial activity support.
All 397 PR77 skin files are unchanged: 34 packages (23 zones, 11 buildings),
32 authored icons. The PR75 payload is an exact 395-file subset; PR77 adds
Atreides Stadium. No current-main AI/gameplay/map/protocol work was replaced.

Graphics-only desktop refresh no longer triggers a gameplay reseed and detects
same-version/same-size byte changes. Android has a separate extraction marker
inside graphics_skins; the Advanced cache action recovers on process restart.
Local cache metadata no longer invalidates stock-mod content approval. Actual
art and rule files remain checked. New real-SDL tests cover cache safety,
selected zone portraits/fallbacks and campaign/co-op setup propagation.

Native Release app is built in dunecity-skins-integration/build/bin/dunecity.app
on claw.local. Dependency audits and eight packaging tests pass. Full CTest
and focused rerun evidence is in ../outputs/skins-integration-786/. Initial full
suite failures in menu approval and city stats shared the cache-stamp approval
cause, now corrected. See docs/dunecity-skins-integration.md for full dispositions,
play-test steps, and final validation limits; no phone or two-player test claimed.

Stefan subsequently confirmed to integrate whatever assets are present. Neither
source PR changed when rechecked. The absent industrial Active phase images are
an acknowledged limitation, not a merge blocker. Atreides Stadium is included.
Integration CI 36199247907 passed Windows, Linux, browser, unit and service jobs;
Mac was built/tested locally. Do not claim visible industrial smoke from these
static packages, Android device testing, or a real two-player acceptance test.

## 2026-09-25 — Selected DuneCity zone icons (PR #77, 1.0.785)

The selected-zone sidebar was still hiding the ordinary per-house portrait and drawing a live top-down zone preview over it. Dune2-skinned Residential, Commercial, and Industrial zones now display the same authored Icon Sprite portrait used by the build menu. Unskinned zones and Hospital/Church civic overlays retain the live preview. A zone UI regression check covers this switch. Source metadata is bumped to 1.0.785 for the PR update.

## 2026-09-25 — Latest main, DuneCity skins, Android refresh and campaign skin choice (local 1.0.783)

Fast-forwarded this Windows checkout to Stefan's `origin/main` at `6d8e2a92` and synchronized 34 accepted Oathkeeper DuneCity packages (23 zones, 11 buildings). The skin packager now includes an accepted landscape Icon Sprite Compact as `icon.png`; 32 icon files are present, while packages without accepted icons keep the engine fallback. The two units without eligible Compact cells remain skipped.

Android APK staging now keys its payload marker to both the bundled skin bytes and the native library, so same-version test rebuilds refresh assets. Android extracts the payload directly to its app-storage mod directory; the C++ seeder now recognizes that source and destination are identical and avoids copying each skin onto itself. Desktop's separate bundled source and profile destination use a content fingerprint to reseed changed skins, including same-size artwork changes. Campaign setup exposes a DuneCity SimCity/Dune2 skin selector; its choice is put into campaign init settings and the co-op lobby default. Joining players can adjust their house's Skin dropdown in the co-op lobby.

`python -m unittest tests.test_dunecity_skin_packaging` passed (4 tests). Android arm64 native and debug APK builds succeeded with the pinned `p2pkit` dependency installed using `npm ci --allow-git=all` in `platform/web`. Installed via `adb install -r` on device `3107TF1010002728`, preserving app data. The game launched as app 1.0.783; all 32 installed icons were present, and the installed Atreides Residential icon SHA-256 matched the packaged source. An on-device screenshot confirmed the new campaign selector shows Dune2. A full campaign/co-op session and visual sidebar-icon pass remain for play testing; no online session was started during this check.

The review branch bumps source metadata to 1.0.784 for PR CI; the on-device APK above is the pre-bump 1.0.783 test build, not a 1.0.784 release build.

## 2026-09-24 — Opening space, prompt colonies and launcher air defence (local 1.0.783)

Captured MBA 782 session 1790250645152912-0 on Sihaya-O'Donnell 128x64,
seed 444573528, DuneCity tech 8, concrete off and turret power required.
House 1's MCV 215 arrived at 11.80 game minutes but had no destination until
12.73, then deployed at 13.09. The base briefly reported four free plots while
no local yard footprint was usable; missing Heavy/High-Tech/Repair buildings
closed remote expansion again. Ornithopters also destroyed remote yards at
(90,52), (36,52) and (90,10), plus surrounding buildings. Generic reinforcement
selection retained launchers in ground fights, and forced attacks on distant
flying targets were dropped by the unit engine outside weapon range.

Custom opening combat units now take distinct short steps toward visible enemies
(or map centre) onto safe connected sand. Human orders, existing forced orders,
workers, MCVs, transports, saboteurs and already-off-rock units are preserved.
No safe nearby sand means no opening order. A 90-second hold keeps ordinary
regrouping from pulling those units straight back onto the construction rock;
combat can still take precedence. Campaign and support openings are unchanged.
The hold and destinations persist in ordinary saves/checkpoints (save 9848);
older saves load with no invented opening state.

An already-delivered MCV whose local deployment search actually fails may waive
the factory-core prerequisite when seeking safe remote rock, independently of a
temporarily optimistic space survey. Approved remote destinations retain that
exception at deployment. Existing turret cover, threat/loss/route checks and
purchase limits remain; this changes deployment, not MCV purchase frequency.

Aircraft targeting any owned structure trigger mobile anti-air response before
the first damage callback. Launchers are preferred; ordinary AI ground attacks
and rally orders yield, human orders and necessary repair do not. Responders
move to reachable ground within two tiles of the attacked building instead of
chasing the aircraft's tile. Transit is committed against incidental ground
contacts, in-range responders fire, and ended attacks release the assignment.
The existing saved defenceAssignments map holds the mission. Up to three
responders per aircraft are recruited within 40 tiles, with stable assignments.
Ground reachability uses a bounded eight-neighbour search around the journey,
ignoring movable traffic but respecting buildings and vehicle terrain barriers.

Version 783, protocol 37, policy opening-and-colony-defence-v85. Claude provided
the initial implementation and fixtures; its second bounded run timed out after
writing the patch. Codex completed reachability, committed transit, ground-contact
priority, actual-firing tests and integration. Evidence lives in
../outputs/quantbot-opening-space-783/. This candidate has not been installed,
pushed or released.

Native build, dependency audits, version consistency and diff checks pass.
All 42 ordinary CTest targets pass. After the final route-selection optimization,
the app was rebuilt and all three air-defence probes passed again. The probes
require real travel and a launcher missile fired at the aircraft, protection
against incidental ground-contact orders, human-order preservation, rejection of
an enclosed launcher, stable in-range firing and release after the attack ends.
The expanded MCV probe passes first-call remote assignment with missing factories
and a misleading four-plot survey, actual travel/deployment, opening sand moves,
no-safe-sand fallback and current/older save restoration. No new full-match
victory result is claimed for this build.

## 2026-09-24 — Cramped production and Starport colonists (local 1.0.782)

Current Air 781 session 1790248536426276-0, DuneCity Sihaya-O'Donnell 128x64,
seed 673490541, Rebels Brutal vs Fremen Brutal, tech 8, concrete off, powered
rocket turrets. At 17.28 minutes Rebels had 2,919 credits, one yard, one Starport,
no Heavy Factory and no MCV. It reported 63 free rock tiles but no factory site;
base_built_out was true and colonisation_due false. The Starport kept buying
combat units. Heavy-production and repair-capacity orders were rejected for
no_site. Missing Heavy/High-Tech/Repair buildings also closed the remote survey,
a circular prerequisite when the reason to expand is lack of room for them.

Production placement now tries both ordinary searches first, then a city-only
fallback relaxing road continuity/frontage and neighbouring road-access layout
preferences for builders and factories. Engine legality, ground egress,
reservations, threat and reactor clearance stay mandatory; existing buildings
are not demolished by this fallback. Preferred road-fronted sites still win.
This may reduce city road connectivity locally, as explicitly requested to
unblock production. Telemetry reports roading_relaxed and search pass 2.

Built-out bases may survey/deploy remotely before completing the missing factory
core. The current survey's footprint measurement controls that exception;
existing expansion turret coverage, target/route danger and loss checks remain.
A custom city with no MCV-capable factory can select a Starport colonist through
the shared capital plan, at actual market price. That reserves its cash against
other producers and makes the port wait rather than spend its savings on
bargains. Stock, unit/yard caps, existing/paid/queued MCVs and campaign/helper
boundaries remain. First-yard emergency recovery is unchanged.

Version 782, network protocol 36, policy cramped-city-starport-v84. No new saved
fields or checkpoint-layout change. Claude implemented the initial patch and
fixtures, then hit its bounded turn limit; Codex reviewed and completed shared
capital reservation, current-survey gating and city-only fallback scope.

Native build and dependency audits pass. Seven focused CTests pass, including
all-mode opening economy, both Starport recovery probes and unit tests. The
expanded MCV engine probe also passes with Heavy/High-Tech/Repair counts all
zero: sold-out and unaffordable orders are refused, one funded MCV is dispatched,
paid cargo and a second port cannot duplicate it, and the factory path resumes
when restored. Cramped placement accepts the only legal unfronted site, refuses
a mountain-enclosed exit, and prefers proper frontage when available. Existing
local/remote MCV travel/deployment checks remain.

Same map/seed/roster/settings 20-minute baseline and candidate simulations do
not exactly replay the live trajectory. Baseline orders HF at 5.33 minutes and
MCV at 9.68; candidate HF at 14.72 and MCV at 17.78, ending with two yards and a
Heavy/High-Tech Factory. These are progression smoke checks, not a claim of a
speed improvement or a match victory. Direct fixtures prove the blocked cases.
All 42 ordinary CTest targets pass (seven focused plus 35 remaining). The
full-match regression, seed 1804669015 on All against Atreides, wins against
four Hard AI Player opponents at 53.70 game minutes, with concrete and
rocket-turret power required. This checks broader play, not a replay of Sihaya.
Evidence: ../outputs/quantbot-cramped-starport-782/. Subsequently installed on
the MBA; version, signature, matching binary and runtime rendering verified
(../outputs/mba-test-782/install-verification.log). Not pushed or released.

## 2026-09-24 — Space-driven MCV colonisation (local 1.0.781)

Separate custom DuneCity colonisation demand now bypasses a satisfied production
construction-yard target. Every 15 game seconds the rock survey counts up to four
disjoint free 2x2 footprints within the base's build range and checks whether a
Heavy Factory can legally fit. Fewer than four footprints, or no factory site,
requests one MCV when safe reachable unoccupied rock exists. Existing/queued MCVs,
credit reserves, the yard cap and remote expansion defence requirements still
apply. All custom difficulties share the rule; campaigns and support helpers do
not gain this new purchase trigger. Production MCVs retain prompt local deployment
while the base has room. Cramped bases prefer remote settlement, retaining valid
trip assignments and falling back locally if no remote site can be used.

Captured Air 780 session 1790245284856094-0 (seed 1804669015) had six yards, no
MCVs and 57,588 credits at 34.48 game minutes. It repeatedly found remote sites,
but 620 scattered free tiles kept the old raw-tile exhaustion trigger off. The
capture alone does not prove every local placement was impossible; the new rule
measures usable footprints as well as raw area. Newly cleared enemy rock becomes
eligible on the next survey, rather than remaining permanently excluded.

Version 781, network protocol 35, policy space-driven-colonisation-v83. The two
survey fields are in observer-runtime checkpoint version 5; older network
checkpoints are rejected. Ordinary save layout is unchanged.

Native Release build and both dependency audits pass. All 42 ordinary CTest
targets pass. The expanded real-engine MCV probe proves: a roomy base buys no
colonist; a cramped base with 78 loose free tiles and zero 2x2 footprints rejects
unreachable/enemy-held rock; removing the enemy causes exactly one accepted MCV
order despite the production quota already being met; an existing MCV suppresses
duplicates; it drives from (68,10) to (97,5), deploys there, and stops further
colonist purchases once room is available. Earlier local-deployment coverage at
all four difficulties remains. Fixture isolation retires earlier test MCVs and
removes stale map ownership before surveying the new terrain.

The current live seed 1804669015 wins against four Hard AI Player opponents in
56.29 game minutes, with concrete required and rocket-turret power required.
That match retains local building room and does not exercise the new colony
trigger; the controlled engine probe above is the direct colonisation evidence.
The three existing match regressions also won (seed: game minutes):
1841712204: 55.80; 41562270: 71.89; 1430976924: 61.78.
Claude implemented the bounded task but reached its turn limit; Codex reviewed,
completed fixture isolation, and independently built and ran CTest. Evidence:
../outputs/quantbot-colonisation-781/. Later installed on the MBA at
/Applications/dunecity.app; version/signature/binary hash and rendering verified
(../outputs/mba-test-781/install-verification.log). Not pushed or released.

## 2026-09-24 — QuantBot city production, foundations and aircraft (1.0.780)

Local candidate on `fix/dynasty-structure-degradation`, based on 95f27106.
Finished buildings no longer get refunded because their foundations are incomplete.
Slab failures refund only the unusable slab and retain the building queue/site.
Nearby fully prepared sites are preferred only when normal placement, access,
road, threat and reactor-clearance checks pass; otherwise the engine applies
normal foundation damage. Foundation planning and the early 2x2 upgrade remain.

Aircraft are excluded from the ground-unit repair reservation: damaged
ornithopters remain available to attack. Forced targets survive equal/lower
priority alternatives; a building under attack outranks a remote worker rescue,
and both outrank raids. Offensive anti-air avoidance and human control remain.
City optional turret overlap is interleaved with three non-service construction
orders; uncovered core assets and aircraft near the base retain defensive priority.
The existing local MCV deployment and Vanilla power-buffer rules remain.

The Palace keeps its +40 land-value bonus and radius 8, plus its R+C roles.
It no longer satisfies the Stadium requirement above 500 residential population.
Protocol 34, telemetry policy city-production-air-recovery-v82; no save-layout change.

Native Release build and dependency audit passed. All 42 ordinary CTest targets
passed, including damaged-air/stable-target/base-priority probes in Vanilla,
DuneCity and Dune2R, all-mode foundation fallback/repair opening checks, city
coverage interleaving, MCV deployment, Palace civic and land-value assertions,
menu/network/weapon/movement/degradation regressions. New placement fixtures use
passive silos so they do not change later factory-spending checks, and check the
structure's remembered foundation flag because placement consumes slab tiles.

Full matches: Brutal Atreides QuantBot against four Hard AI Player opponents on
`5P - 128x128 - All against Atreides`, concrete required on, fog off, no immortal
human or special bonuses. Actual engine victories:

| Seed | Victory (game minutes) |
| --- | ---: |
| 1841712204 | 52.56 |
| 41562270 | 54.55 |
| 1430976924 | 61.78 |

The third seed first reached the 60-minute cutoff with three opponents defeated
and a small fourth base remaining. Extending the horizon alone produced victory
at 61.78; no AI code or opponent settings changed between those runs. Optional
`DUNECITY_ENABLE_AI_MATCH_TESTS=ON` registers all three victory-required CTests
with a 90-minute game limit; defeat/time limit fails while retaining telemetry.
The 1.0.779 baseline on seed 1841712204 did not win within 60 minutes. At 10.16
minutes the fixed AI had 8 Heavy Factories versus 6 and a stadium versus none;
its stadium first appeared at 9.15 minutes. It placed 37 incomplete-foundation
buildings instead of refunding them; remaining foundation cancellations were
slabs only. The baseline cancelled 39 completed buildings for missing concrete.
These three seeds are regression evidence, not a guarantee on every map/seed.

Receipts: `../outputs/quantbot-city-recovery-780/`, plus per-seed
`build/city-hard-match-*/run-*/summary.json` and isolated decision logs. The bounded
Claude implementation worker reached its turn limit; Codex reviewed/completed
integration and independently ran all builds and checks. Local only: not pushed,
released or installed over the MBA's 1.0.779 testing app.

## 2026-09-24 — Windtrap condition, city land value and QuantBot concrete

Local candidate 1.0.776 on `fix/dynasty-structure-degradation`, building on the
local 1.0.775 degradation commit. Standard and advanced windtraps now scale
output linearly with health using Dynasty enhanced integer output rules; there
is no 50% output floor. Existing load reconstruction refreshes saved generation.
Every damaged structure in city mode proportionally lowers land value on its
footprint; shared blocks use the worst condition, and repairs restore value on
the next effects scan. This feeds the existing growth and tax calculations.

Game Rules now labels the existing option **Concrete is required**. Default is
off in DuneCity, on in Vanilla; player overrides remain respected. Off hides and
rejects slab production/placement and suppresses placement/foundation damage,
including stale saved foundation flags. Roads/authored slabs remain supported.
Power-shortage damage is independent. The removed degrade-on-concrete setting
stays removed. No save-layout change; protocol 30 and telemetry policy v78.

QuantBot uses foundations for productive buildings, windtrap variants, refineries
and defensive emplacements; city mode extends this to other buildings for land
value. Passive optional foundations outside city mode require >5,000 spendable
credits, positive repair savings and a post-purchase reserve. Urgent recovery
and cash-limited zoning can skip unaffordable foundations. Yard upgrades for
2x2 slabs move ahead of optional infrastructure after income exists, subject to
actual tech/availability, cash reserves and immediate defence/power needs.
Only one yard upgrades for slabs at once. City zoning with foundations selects
rock rather than unpaveable sand. Legacy campaign reconstruction/power orders
share the same foundation queue and early upgrade. Low-power repairs include advanced windtraps
and real Vanilla shortages. No changes to general factory/refinery repair gates.

Inventory and economic rationale: `docs/building-condition-and-concrete.md`.
Focused native checks pass: all-difficulty real AI opening/queue/repair checks
in Vanilla, DuneCity and Dune2R; generator/land-value/foundation/save probes in
all four modes; unit checks and menu rendering/persistence. All 41 CTest
targets passed (full suite plus targeted reruns). Shared-spending fixtures now
isolate cash sharing after the early yard upgrade; campaign queues are checked
across all difficulties. Native dependency audit and version checks passed.
Receipts: `/Users/stefan/Documents/projects/outputs/concrete-policy/`.

Built app: `dunecity-degradation/build/bin/dunecity.app`. Local changes only;
not pushed, released or installed over the user's game.

## 2026-09-24 — Dynasty structure degradation across all modes

Local candidate 1.0.775, branch `fix/dynasty-structure-degradation`, based on
`e3eb127e` in `dunecity-degradation`. All modes now use independent power damage
(1 absolute HP every 15 game seconds, down to Dynasty's quantized supply/demand
threshold with a half-health minimum) and foundation decay (3 HP Harkonnen,
2 HP Ordos, 1 HP all other houses every 180 game seconds). Complete prepared
foundations prevent foundation decay. The existing Concrete Required switch
still exempts foundation requirements when disabled. Power damage uses actual
supply/demand even in Vanilla, without changing unrelated turret/power rules.
Walls are excluded; foundation decay is exempt in campaign levels 1–2. A final
foundation hit can cross half health, matching Dynasty's pre-hit threshold.

Removed Structures Degrade On Concrete from UI, defaults, mod parsing, option
hash/equality, telemetry, AI branches, translations and probe controls. Old
settings streams retain an ignored reserved byte; config writes remove the
obsolete key. Updated Tornie's payload checksum and made the diagnostic driver
fail if its requested mod does not activate, preventing false Vanilla fallback
passes. Degradation probes explicitly load each mod's real defaults.

Save format 9847 persists both timers and the original foundation flag. Old
saves' terrain and single timer cannot reliably identify the original slab
coverage, so existing buildings loaded from pre-9847 saves receive power damage
but no foundation decay. New placements record the flag normally. Protocol 29
rejects peers using the old health rules. Placement health penalties unchanged.

Validation: native app build and dependency audit; unit suite; real-engine
placement, simultaneous damage, thresholds, wall exclusion, campaign gates,
save/load continuation and old-save migration probes in Vanilla, DuneCity,
Dune2R and Tornie; all 41 CTest targets passed (including corrected reruns). Initial menu/weapon failures exposed the
Tornie checksum omission and were rerun after correction. Receipts:
`/Users/stefan/Documents/projects/outputs/degradation/`.

Built app: `dunecity-degradation/build/bin/dunecity.app`. Committed locally only;
not pushed, released or installed over the user's game.

## 2026-09-24 — QuantBot builds an empty-market Starport for late tech

User observed House IX locked because QuantBot skipped an empty CHOAM Starport.
At the existing House IX and Palace priority positions, QuantBot now orders one
missing Starport when it is the configured missing prerequisite. The early
import-priority rule is unchanged. Enabled flags, tech/yard-upgrade gates, other
prerequisites, campaign restrictions, placement, money and pending orders remain
respected. Queued ports prevent parallel-yard duplicates; the next planning pass
after completion selects the intended tech building. No extra overdue override
was added: the existing overdue timer only starts when the goal is buildable.

Local candidate 1.0.774, network protocol28 (AI decisions changed), telemetry
policy v77; save format unchanged. Built in `dunecity-ix-starport/build`.
Regression uses the real QuantBot build path and real construction queues for IX
and Palace, all four difficulties, Vanilla and DuneCity. Checks empty market,
locked/disabled/upgrade-locked goals, absent configured dependency, queued-port
deduplication and target selection after port completion. Fixtures use upgraded
yards, exempt starting cash and available rock to isolate tech policy.

Validation: native build, dependency audit, version consistency, diff check, and
37/37 CTest passed (331.18s), including the expanded Starport probes and existing
Vanilla/DuneCity/Dune2R opening/spending checks. Receipts:
`/Users/stefan/Documents/projects/outputs/ix-live/`.
Committed locally on `fix/ix-starport-prerequisite`; not pushed, released or
installed over the user's game. Production remains 1.0.773.

## 2026-09-24 — All local work integrated and installed on the MacBook Air

User authorized combining the local branch work and installing it. New checkout
`/Users/stefan/Documents/projects/dunecity-all-local`, branch
`integrate/all-local-773`, version 1.0.773, protocol 27. Base `610ddca4` includes
the latest AI/map/menu work; aircraft-AA merged at `2c260400`, all city scenarios
at `52da93bc`, historical carryall release notes at `40f82be9`. The older Starport
recovery is already present byte-identically; play-mode commits are patch-equivalent.
No local gameplay fix was omitted. Save layout unchanged. No push/PR/public release.

Fresh native build and all 37 CTest tests passed. All city maps validate; all
227 packaged maps match source. The 552-row aircraft audit matches the earlier
cadence-fix results. Packaged with cmake --install (bundled dylibs, verified
signature), transferred by SSH, installed at `/Applications/dunecity.app` on
`Stefans-MacBook-Air.local`. Installed version 1.0.773, binary SHA-256
`125a45097557c76f97af732eca3dc8c7a950673b926f72bc7832cda9798b6e3d`, signature
and hidden-window runtime/rendering verified on the Air after replacement.
Old 1.0.768 retained at `/Applications/.dunecity-backup-before-773-cf537620/dunecity.app`.
No running game was interrupted, no profile/save edited, no game launched.

Detailed inclusion table and evidence: docs/local-build-773.md. Receipts/package
in `/Users/stefan/Documents/projects/outputs/all-local-773/`. Claude subscription
worker audited branch completeness and packaging read-only; Codex integrated,
reviewed, built/tested, packaged and verified the actual MBA installation.

## 2026-09-24 — QuantBot progression, air rescue and unreachable Hunt targets

Local worktree `/Users/stefan/Documents/projects/dunecity-vanilla-ai`, branch
`fix/vanilla-ai-progression`, based on `a242f85c` plus the two setup commits
`b4578eb7`/`7f2bc6a7`. Version 1.0.772, lockstep protocol 26; save layout unchanged.
No publishing or replacement of the running MacBook Air game was performed.

The captured 1.0.768 Brutal Vanilla match had Mercenary High Tech/IX orders at
35.65/35.68 minutes, no silo through 40.9 minutes, and 13 windtraps. Runtime turret
power was incorrectly enabled although the active Vanilla defaults disable it.
New custom games now reload the chosen mod's defaults even if it is already
active; deliberate overrides survive Back/Next. The integrated map-selection
fix preserves that choice when loading an authored map.

QuantBot reserves its first production core, available IX and needed refinery
capacity ahead of repeated counter-air turret construction, retaining the first
two defensive rockets. Worker/field targets and pending refinery orders still
bound expansion. Existing early storage priority now counts the capacity of
queued refineries instead of vetoing every silo during refinery expansion.
Non-city turret headroom uses actual turret draw instead of the old 225 reserve.
The newly enabled Dune2R spending probe also exposed its legacy worker branch
buying harvesters with no spice left; both legacy purchase branches now stop
when the surveyed field is exhausted.

Ornithopters prioritize actual attackers of owned structures/harvesters above
raids and speculative contacts, including AA-covered attackers. Damage callbacks
recall eligible aircraft through the existing two-second district debounce;
periodic targeting also recognizes active attacks. Human orders and aircraft
reserved for repair retain their exclusions. Ordinary raids still avoid AA.

Autonomous vehicle Hunt rejects terrain-disconnected targets at selection and
before deferred pathfinding. It accepts reachable firing positions, infantry,
flyers and explicit forced orders. A lazy, shared eight-neighbour terrain cache
is rebuilt only when mountain topology changes, not on traffic revisions. No
reachable target returns Hunt to Guard. Deferred paths safely handle a target
that died after the request was queued. Dynamic occupancy remains the ordinary
pathfinder's responsibility. Regression probes cover all three shipped modes,
1000 traffic revisions without a cache rebuild, zero A* expansions for rejected
hunts, opening/reclosing a detour, and expired target handles.

Validation artifacts and captured live evidence are outside the repository at
`/Users/stefan/Documents/projects/quantbot-live-20260924/`. The reported map's
SHA-256 matches the installed MacBook Air map. Focused menu, Hunt, air-defense
and economy probes pass. The final 40-minute simulation completed: Mercenary
High Tech/IX at 10.96/13.55 minutes, first silo at 19.04, 30 refineries at minute
30 versus nine in the capture, and only the initial windtrap for both houses.
Neither house recorded storage loss. The simulation logged 1107 active-attacker
air rescue orders; the slowest instrumented A* search was 4.13 ms, with none
over 33 ms. Target-queue processing peaked at 1.129 ms. This is a controlled
same-map/seed run without human commands,
not a deterministic replay of all inputs in the live match. Full CTest ran 36
targets: 35 passed initially; the additional Dune2R spending target passed after
the depleted-field fix. Affected Dune2R/shared-spending/Starport and unit tests
were rerun after that correction. Native build and pre/post dependency checks
passed; browser runtime and a packaged/public release were not exercised.

## 2026-09-24 — The selected mod owns a new custom game

Same branch and worktree, on top of the connection-choice commit below.
Picking a mod in custom-game setup is now the decision: choosing a map, browsing
maps, pressing Next or backing out never changes it, and the choice is stored as
`General/Custom Game Mod` so it survives reopening the setup and a restart. Mods
activated automatically — by a save, a join or a campaign — do not overwrite it.
`CustomGameMenu::prepareSelectedMap` no longer activates a map's authored mod;
`playCustomGame` and `CustomGameMenu::onNext` use the new
`WorkshopGameContent::applyMapRevisionForSelectedMod`, which reuses a downloaded
map's authored revision only when it belongs to the selected mod and otherwise
lets `pin()` capture the same bytes as a revision of that mod, leaving the
authored revision immutable. The now-unused strict `applyMapDependency` was
removed; saves and joins still resolve their exact pinned mod via `resolveMod`.
The player-screen mod picker is enabled for downloaded maps too. This also fixes
the report that starting a game with Dune City selected ran with MOD: VANILLA.
The menu probe gained a regression for it (local and downloaded Vanilla map with
Dune City selected: identity, city flag, setup mod, pin/resolve, reopen and the
saved configuration). No gameplay, protocol, save-format or version changes.
Unit suite and menu navigation probe passed; logs in
`/tmp/dunecity-remember-play-mode/`. Browser runtime not exercised.

## 2026-09-24 — Remember campaign and custom-game connection choices

Local branch `fix/remember-play-mode`, based on `224b4342` (1.0.769).
Campaign and custom-game setup now persist their last user-selected Offline/Online
mode separately in General settings. Changes save immediately, including when
backing out without starting a game. Custom player setup shares the custom-game
preference. Normal entry restores it; explicit Play Online entry still starts
online. Fresh profiles retain the existing defaults. The existing web filesystem
sync is invoked after saving. No gameplay, protocol, or save-format changes.

Native app rebuilt in this worktree's `build/bin/dunecity.app`; Ninja dependency
integrity, unit suite and menu navigation probe (three resolutions) passed.
Browser runtime reload has not been exercised.
No push or public release.
## 2026-09-24 — Align turret and launcher cadence with Dynasty

User authorized matching both after the engine-aware timing recheck. Shared
rocket turret reload is now 170 cycles / 2.720 s; its close cannon scales the
configured gun turret reload by 128/240 (2.048 s by default). Standalone gun
turrets retain their reload. Standard/elite launchers use 750 cycles / 12 s
from the second shot, with an 18-cycle / 0.288 s burst gap (12.288 s pair period).
At <=50% HP they fire singles. Missed secondary opportunities are cancelled;
the fallback primary cooldown remains live. Other units keep legacy timing.
Values translate measured Dynasty intervals to 16 ms ticks rather than copying
its 60/20 Hz counters. No new random draws or serialized fields. Protocol 25
prevents lockstep mixing with earlier combat rules. No release/version bump,
install, push or PR. Local app rebuilt in this checkout.

Tornie has a separate ObjectData override: updated that and its integrity
checksum too. Native CMake now relinks/recopies for bundled ObjectData and
checksum changes; the initial regression caught stale copied data, then the
missing checksum update. Source/bundle hashes now match and Tornie's entire
checksum manifest verifies. See docs/weapon-cadence-alignment.md.

Aircraft sim: 552 rows across Vanilla/DuneCity/Dune2R, identical mode replays.
Powered three-turret attacking-ornithopter kills 12/18 -> 18/18; lone turret
0/18 -> 2/18. Fast carryall crossings still 0/36 kills. Separate 64-case matrix:
turret 16 -> 27 kills; launcher 15 -> 8, consistent with the slower launcher
cadence. No aircraft HP, speed or missile motion changes. Receipts are in
../outputs/aircraft-aa-alignment/; final weapon timestamp receipts in
build/weapon-reload-probe/. Original Dynasty reference still passes ASan/UBSan.

Claude subscription worker produced the bounded production patch but reached
its turn cap without tests/final report. Codex reviewed and completed burst
cancellation, runtime timing/edge tests, Tornie integration, packaging and
verification. All 32 CTest tests passed across the full run and final focused
reruns; native build/dependency audits passed. The final reload probe covers
138 steady-fire cases and 15 interruption controls across four modes.

## 2026-09-24 — Reload timing rechecked across both engines

Follow-up on aircraft audit. Confirmed actual steady turret missiles every
5.760 s in DuneCity versus 2.667–2.750 s in original Dynasty. These include
engine scheduling, not equal numeric counter assumptions. Launcher result is
the opposite: healthy pairs every 5.760 s (0.240 s within pair) versus Dynasty
12.250–12.417 s (0.250–0.333 s within pair). Damaged launchers fire singles;
exact 50% HP boundary differs. See docs/weapon-reload-comparison.md.

New timestamp controls run 30 cases × 120 seconds per engine (DuneCity replayed
in all 3 modes); native ASan/UBSan and host shot/timer cross-checks passed.
Both ground/air targets, three seeds, 100/50/33% launcher HP. Normal timebase
verified in source: host 62.5 Hz, Dynasty 60 Hz with 20 Hz unit cooldown counters
and 5-game-tick script scheduling. Shared gameplay values remain unchanged.
Receipts: ../outputs/reload-doublecheck/. Claude's independent read-only source
audit hit its 32-turn cap without a final report; Codex performed and verified
the actual runtime measurements and timebase/source checks.

## 2026-09-24 — Aircraft anti-air audit (tests only)

Branch `investigate/aircraft-aa`, checkout `dunecity-aircraft-aa`, based on
origin/main `224b4342`. No production combat change, version/protocol change,
installation, push or release. User requested Dynasty comparison and simulations.

Confirmed a cadence mismatch: shared rocket turret reload is 360 x 16 ms =
5.76 s. Original Dynasty unit/structure loops with UNIT.EMC/BUILD.EMC measured
2.667–2.750 s between stationary-target turret shots (30 damage in both).
The existing 64-case attacking-ornithopter matrix yields 16 kills per mode;
a test-only 170-cycle reload raises that to 24, with launchers unchanged at 15.
All three modes agree. This suggests correcting cadence before changing HP or
missile speed, but is not a validated balance change for ground combat.

Original-engine reference: 91 cases with ASan/UBSan; actual attack scripts,
controlled stationary/flyby aircraft, separate repeated-shot control. It also
shows weak aircraft interception. Carryalls retain Dynasty's 100 HP and 15
tiles/s versus 11.25 tiles/s turret missiles; no inflated-HP defect found.
Native reference must not be presented as an exactly matched flight-controller
benchmark. See `docs/dynasty-aircraft-reference.md` for measurements/reproduction.

Claude Max prepared the additional powered-turret aircraft matrix; Codex reviewed
fixture semantics, supplied original-engine reference/cadence sensitivity runs,
and independently verified the existing mechanics/combat/continuation probes.
The initial worker's run1/run2 mislabelled orbiting aircraft as stationary and
failed to end flybys; those outputs are superseded and are not final evidence.
See `docs/aircraft-aa-audit.md` for corrected motion/power/shot accounting.
Artifacts: `../outputs/aircraft-aa/`. Native build, dependency audit, existing
three projectile CTest probes and cadence experiment passed.
## 2026-09-24 — Habbanya-Penny-inspired pair, installed and published

Added Habbanya Crescent (2P, Atreides township versus larger Harkonnen city)
and Habbanya Narrows (3P, mobile Atreides colony versus mutually hostile
Harkonnen/Ordos towns), both 128x128 single-player conquest maps. Original
terrain follows Rippsblack's Habbanya-Penny geometry: enclosing mountain rim,
paired C-shaped spice basins, curved battle lanes and open expansion shelves.
Twelve neutral Fremen sandworms per map do not add a lobby/enemy slot. Actual
Dune R/C/I roles, populated-zone power reserves, no population win condition.

Generator: `scripts/gen_habbanya_scenarios.py`, reusing the existing city helpers.
Checks include connected starts/shelves/basins/outer spice, spare buildable rock,
legal worms, army/production asymmetry, plus the shared structural validator.
Claude Max supplied the generator/maps (48-turn ceiling reached after files);
Codex reviewed exact final bytes, previews, routes and real-engine behaviour.
Both deterministically reproduce and load without warnings. Final 25-minute
Medium helper versus Medium enemy runs survived in both maps with no unrest.
Idle Crescent lost near minute 12; idle Narrows survived 12. No full human
victory/balance claim. Final test evidence uses `release-*` directories; earlier
probes in the same output folder concern superseded layout drafts.

Published both as v1 to the production metaserver, verified live catalogue,
complete downloads, manifest/file hashes and downloaded structural checks.
Installed byte-identical user-map and built-app resources; codesign verification
passes. Guide and default validator include both additions. No game source,
version/protocol, release, push or PR changes. Evidence, preview, ZIP and receipts:
`../outputs/habbanya-scenarios/` (live proof `publish/verified.json`).

## 2026-09-24 — Ten redesigned city scenarios, published and verified

Replaced Sihaya Basin, Ash Quarter and Coriolis Gap with irregular, tactical
layouts; added Cielago Watch, Hagal Flats, Tuono Crossing, Carthag Vise,
Arrakeen Blackout, Shield Wall Rift and Harg Pass Convoy. All ten are standalone
Atreides-human single-player maps, with a proposed campaign order and briefings
in `docs/city-scenarios.md`; no campaign menu registration. Preserved the
separate SimCity spacing repair unchanged. No engine/protocol/release changes.

Micropolis Detroit/Bern/Dullsville binary city layouts and existing All Against
Atreides, Sardaukar Outpost and Alkozeltser maps informed the redesign. Terrain
and streets now form islands, ribbons, gates, mesas, valleys and scattered towns.
Ordinary Dune buildings count toward their actual R/C/I roles. Population is
never a win condition; 5000 displayed population is only unrest eligibility.
Hagal requires 24000 stored spice credits and starts with 12010 storage, so two
more silos are needed. Cielago wins after 30 minutes; other missions use conquest.

Claude Max implemented the generator/maps and static checks; its bounded run
hit 96 turns after producing the patch. Codex reviewed/integrated it, corrected
Hagal's already-satisfied storage capacity, reduced Ash's immediate squatter
army so crime can mature, added targeted precincts, and ran real-engine probes.
Power budgets include populated-zone demand except deliberate Arrakeen blackout.
All ten deterministically regenerate, pass structural checks and load without
warnings in the real engine. Ash's real hostile unrest occurred around minute
five in both tested seeds (18 and 28 troops total over twelve idle minutes).
Both starting MCVs in each expansion map have traversable paths to distant legal
yard footprints. Active helper benchmarks sustained Sihaya for 35 minutes;
Cielago and Coriolis lost before completing their objectives. These are mechanics
and opening checks, not completed human balance playtests. Enemy-city unrest
still occurs, but entrenched opponents retain much larger production and armies.

Published Sihaya/Ash/Coriolis v2 under their existing identities and seven v1
maps to the production metaserver, retaining the verified DuneCity dependency.
Verified all ten live catalogue entries, complete downloads, manifest/file hashes
and downloaded structural checks. SimCity remains v2. Installed local user and
built-app copies; signed-bundle verification passes. No push/PR/game release.
Evidence, telemetry, overview images, ZIP and publication receipts:
`../outputs/city-scenarios-redesign/`; live proof `publish/verified.json`.

## 2026-09-24 — City scenarios published to metaserver

User explicitly requested publication. Used the existing Workshop API and
publishing capability to publish Sihaya Basin v1, Ash Quarter v1, Coriolis Gap v1
and SimCity v2 at `https://dunelegacy.com/p2p`. SimCity retains the original
seeded item identity `bd959851733f7ed42f1401e1e4689717`; no third lineage added.
All four retain the existing verified DuneCity mod dependency. Verified each
live catalogue entry, downloaded manifest and complete map payload, checked
SHA-256/byte equality and re-ran structural validation on downloaded maps.
Evidence: `../outputs/city-scenarios/publish/verified.json`, upload/verify logs,
and `download-validation.log`. No service deployment, game release or push.

## 2026-09-24 — Compact city scenarios (local, unreleased)

Branch `feat/compact-city-scenarios`, worktree `dunecity-city-scenarios`.
Three standalone single-player Atreides maps: Sihaya Basin (60-minute conquest),
Ash Quarter (damaged city recovery), Coriolis Gap (contested mountain corridor).
Micropolis Dullsville, Detroit/Hamburg and Bern inspired the mission problems;
existing DuneCity scenarios/campaign formats supplied supported mechanics.
Briefings and proposed campaign order: `docs/city-scenarios.md`. Not wired into
the campaign menu. SimCity is repacked into compact mixed neighbourhoods.

Dune infrastructure contributes its actual R/C/I role. Silos, construction yards
and starports provide industrial destinations without industrial pollution.
Local trade works on both sides of Coriolis's tactical blockade. Removed the
fixed 500-population win condition after real-engine probes showed it winning
these prebuilt cities within seconds. No engine/protocol/version changes.

Both generators reproduce byte-identical maps; structural validation passes.
Native build, dependency audit, core CTest and signed-bundle verification pass.
Real-engine initial traffic routes: Sihaya 237/237, Ash 315/315, Coriolis 432/432,
SimCity 1222/1222 (original SimCity 487/1087). All three new maps ran 12 simulated
minutes with no human/helper orders against Medium QuantBots; SimCity ran five.
No map-loader warnings. These are one-seed opening smoke checks, not full human
balance or victory playtests. Ash's missing police coverage is intentional.

Installed copies in this host's Dune City `maps/singleplayer` user directory and
in `build/bin/dunecity.app`. Evidence and downloadable map pack live in
`../outputs/city-scenarios/`. Claude Max supplied generators/maps/validator;
Codex reviewed real mechanics, corrected objectives and Coriolis local supply,
unified the player house, verified the maps and integrated locally. No push,
PR, campaign registration or release performed.

## 2026-09-23 — Sustainable policing and core-first spice opening (local 1.0.764)

Follow-up to the live 763 budget complaint. Version 1.0.764, protocol 22;
save format unchanged. Same branch/worktree. No push, PR or public release.
Live Alkozeltser 4 telemetry, seed 1952910083, showed policing at 56–70% of
annual tax for five houses around minute 15, despite only 1–3 stations. The
Budget police line also includes turret upkeep. User explicitly requested
33% early / 50% established spending, core factories before extra rockets,
and continued profitable refinery expansion beyond the four-bay opening.

The AI now caps funded policing (stations, gun and rocket turrets, including
queued commitments) at 33% of annual tax after power costs. Healthy uncommitted
cash >=5000 or annual tax surplus after power and nominal policing >=500 permits
50%. Cash does not count as recurring revenue. Funding can fall to zero when
there is no sustainable city income; there is no emergency spending bypass.
Custom AI applies its own funding directly during deterministic house updates.
The old same-cycle command was added AFTER Game had executed that cycle's
commands, so intended AI budget changes never affected the actual bill. Tests
now verify applied state and the real budget charge, not merely queued intent.
Human/helper and campaign budgets remain under their existing control.

An underfunded existing/pending station blocks further station purchases: a
funding cut must not cause a loop buying more permanent capacity. At full
funding, stations still require dangerous-crime relief and prefer a useful
cheaper turret when full-strength station upkeep exceeds the allowance.
No change to the game's player-facing police prices or coverage strength.

Custom city AI keeps the first two rockets, then completes Heavy Factory,
High Tech Factory and Repair Yard (including prerequisites/power) before more
rockets. Enemy aircraft elsewhere no longer override this core progression.
Unavailable tech is excluded; completed core infrastructure releases the
existing difficulty-scaled overlapping coverage policy. R/C/I seeds and
independent-yard city growth are preserved.

The four-refinery opening eligibility cutoff is removed. A profitable refinery
and its included worker can keep competing through the opening workforce phase
(eight on rich Medium/Hard fields, twelve Brutal), subject to remaining spice,
worker limits, placement, power and expected return. After that phase, workers
and actual processing pressure still govern capacity; this is not a fixed cap
on total refineries. Medium had the same old four-refinery threshold as Hard.

Final same-map/seed/roster 24-minute powered-rocket simulations:
- Medium: all six houses reached 8 refineries; every third rocket followed
  completion of all three core facilities. Zero crime outbreaks or yard losses.
  Maximum observed actual policing/tax ratio 44.60%; 904 funding changes, none
  exceeded the applicable 33%/50% allowance.
- Hard: all six houses reached 8 refineries; third-rocket ordering also passed.
  Zero crime outbreaks; maximum observed actual policing/tax ratio 46.34%;
  1265 funding changes, none exceeded the allowance. Combat/expansion still
  lost construction yards (11 total); these runs do not establish loss immunity.
These are bounded single-seed balance checks. Intermediate runs measuring only
queued budget commands are invalid budget evidence and were superseded.

Claude Max supplied the bounded police-budget fixture after the first read-only
run exhausted its turn limit. Codex implemented/reviewed production changes,
corrected fixture assumptions and tested applied charges and underfunded-station
feedback. Release build, pre/post Ninja dependency audits and all 29 CTest
targets passed (231.68s), followed by all five affected real-engine tests after
the final station feedback guard (31.61s). Packaged signature, SDL initialization
and hidden-window rendering passed. Source version metadata agrees.
Evidence: ../outputs/ai-police-excess/, especially final-comparison.json.

Final archive: ../outputs/ai-police-excess/install-764/dunecity-1.0.764.zip
SHA256: 3ec6ffbe0d99cf4bcfe748683285063ce72209569c52caacec1deb81e374c25f
Binary SHA256: 051e16f5936a25b0afa352e1f8c23c359e86c879d7cd96538d3bce399c6e32b5
Installation pending: Air became unreachable; user was asked to save/close the
running 763 game and wake the Air. Do not replace a running game. The final
packaged app is in install-764/final-stage; earlier stage/archive is intermediate.

## 2026-09-23 — Restore early city growth and sustain rocket coverage (local 1.0.763)

Follow-up to the user's 762 regression report. Version 1.0.763, protocol 21;
save format unchanged. Branch/worktree remain fix/ai-crime-air-economy /
dunecity-ai-defense. No push, PR or public release.

Live Medium telemetry on Alkozeltser 4 (seed 324192283, powered rockets enabled)
showed repeated core-defence/supplier savings monopolizing the single yard.
The 762 reproduction had no R/C/I at six minutes; first zoning was delayed
9.6–14.1 minutes for five houses and beyond sixteen for Harkonnen. Earlier
762 validation measured late aggregate income/defence and missed this opening
regression, as well as the user's powered-rocket option.

Peaceful core coverage no longer takes an unconditional emergency reserve.
Proactive savings permit a small city opening, then pace further plots against
services so cheap lots cannot starve rocket upgrades or the worker supplier.
After two refineries, a single yard seeds missing positive-demand R/C/I and
its power before optional upgrades. Urgent protection, transport capacity,
blackouts, population limits, campaign rules and legal placement still apply;
multiple yards retain their existing dedicated-growth allocation. Powered
rockets reserve their actual draw plus a small plot instead of two extra
windtraps. The peaceful coverage claim is bounded by actual base demand.

Police investment requires meaningful dangerous-crime relief at occupied
properties; routine relief prefers multipurpose rocket turrets. Medium and
above now also seek two overlapping covers for ordinary districts as they
grow. Core overlap remains two on Medium/Hard and three on Brutal. Placement
still values construction yards, refineries and factories most, and counts
pending emplacements. Fixed the service-order counter's unreachable six-order
interval. Existing aircraft defence, MCV recovery and yellow/tax results remain.

Final powered-rocket runs use the same map/seed/roster as the regression:
- Medium, 16 minutes: 75 R/C/I built versus 20 in 762; 44 rockets versus 36.
  All houses start residential at 1.15–1.20 minutes and industry at 1.33–1.39;
  commercial follows positive demand at 5.56–11.20. Rockets start 4.32–4.93.
- Medium, 32 minutes: 226 zones, 229 rockets built (225 remaining), 14 police,
  50 refineries and 212 harvesters across six houses; zero crime outbreaks.
  One construction yard was lost; the run does not establish immunity to losses.
- Hard, 16 minutes: 74 zones, 48 rockets, 12 police, 24 refineries, 63 harvesters;
  zero crime outbreaks or construction-yard losses.
These are bounded single-seed simulations, not a universal balance guarantee.

Claude subscription worker supplied the initial savings/coverage patch and
city-growth fixture; it reached its turn limit. Codex reviewed and completed
opening priorities, power, police preference, district coverage and validation.
Real-engine tests now cover routine turrets vs dangerous-crime police, optimal
station placement, all-tier core/district overlap and reserved coverage, and
single-yard growth vs funded defence and emergency reserves. The harness can
explicitly enable powered rockets. Evidence: ../outputs/ai-opening-regression/.

Release build, Ninja dependency audits and all 28 CTest targets passed
(227.39 seconds).
Packaged runtime initialization, hidden rendering and signature checks passed.
Archive: ../outputs/ai-opening-regression/install-763/dunecity-1.0.763.zip
SHA256: 3dd48147b5d2a8149e1223dd71e7b1046d97dd7bdc5a59c8e574ae24c061ca09
Binary SHA256: a5fd5650f0959b2a54aac7d20f66b93e670809a3c593956b13df871e19688e42
Installed on Stefans-MacBook-Air.local at /Applications/dunecity.app after the
user requested installation with the game closed. Transfer and installed binary
hashes match the verified package above; signature and packaged SDL startup /
hidden rendering passed on the Air. All 511 checked profile INIs are unchanged.
Previous app retained at /Applications/.dunecity-763-w5PQF9/dunecity-previous.app.
Receipt: ../outputs/ai-opening-regression/install-763/mba-receipt.json.

## 2026-09-23 — Proactive city defence and opening economy (local 1.0.762)

Branch `fix/ai-crime-air-economy`, worktree `dunecity-ai-defense`, based on
1.0.760 plus emergency Starport MCV recovery (original commit 66624b15).
Version 1.0.762, protocol 20; save format unchanged. No push, PR or public release.
Installed on Stefans-MacBook-Air.local at /Applications/dunecity.app after the
user saved/closed the game. Packaged runtime rendering and signature checks
passed on the Air; binary SHA256 matches the local package:
cf6418c14a8f58e4234e6c2f0cefc8cfcb1ae2d16495e1a4e7506007fb304387.
All 511 checked profile INIs remained unchanged. Previous app retained at
/Applications/.dunecity-762-KSxU7E/dunecity-previous.app; installation receipt
in ../outputs/ai-defense/install-762/mba-receipt.json.

The live Alkozeltser 4 telemetry confirmed late rocket construction, crime
outbreaks destroying the Rebels construction yard, and aircraft raids
outnumbering defensive sorties. Police and turret placement now compare all
owned neighbourhoods within one cached planning pass instead of a rotating
map stripe. Police sites maximize marginal crime relief at occupied buildings,
including existing/pending coverage. Pre-outbreak relief qualifies for urgent
investment; service savings survive cheap fallback zoning.

Medium and Hard aim for two overlapping rocket covers of construction yards,
refineries and other core production assets; Brutal aims for three. Ordinary
buildings still demand coverage, and pending turrets prevent duplicate plans.
The opening unlocks rocket prerequisites after three refineries; after two
emplacements it funds the fourth refinery and a worker supplier before finishing
peaceful overlap. Observed air threats and urgent crime retain priority.
Rich-field opening worker priority rises to eight (twelve Brutal), bounded by
remaining spice, unit limits, processing throughput and useful returns. Supplier
upgrades/savings no longer wait for a factory to exist to recognize missing
workers. Higher-priority transport investment keeps its funding.

Ornithopters intercept attackers threatening their own base or harvesters before
raiding exposed enemy buildings. Anti-air avoidance, campaign wave restrictions,
repair and human orders remain respected. Custom results restore the classic
yellow wallpaper and show gross Tax Collected separately from Harvested Spice;
the tax column is city-content-only and fits the 640px minimum window.

Native Release build, Ninja dependency audit and all 27 CTest targets passed
(222.57 seconds). Version metadata agrees on 1.0.762; gameplay protocol is 20.
Validation evidence is in `../outputs/ai-defense/`. Real-engine regressions cover
police placement across scan phases, overlapping/reserved rocket coverage on all
difficulties, defensive aircraft targeting, spending, opening economy, MCV
recovery and both results modes. Results were rendered at 640, 854 and 1280px.

Same-map/seed 16-minute six-house simulations (Alkozeltser 4, seed 704066612):
1.0.760 Hard had 8 outbreaks/95 rebels, 2 rockets built, 1 yard lost, 70 surviving
harvesters and 98,675 spice refined. Final Hard had 0 outbreaks, 44 rockets,
0 yards lost, 75 harvesters and 103,548 spice refined. Final Medium had 0
outbreaks, 43 rockets, 0 yards lost and 72 harvesters. All final houses built
four refineries. First final rockets completed around 3.4–3.9 game minutes;
baseline only two houses had rockets by 15.4–15.9 minutes. These are bounded
single-seed regressions, not a claim that outbreaks or losses are impossible.

## 2026-09-23 — Emergency Starport MCV recovery (local 1.0.761)

QuantBot now recovers a missing construction yard through an available Starport
on Easy, Medium, Hard and Brutal. Previously Easy/Medium were excluded outright;
Hard/Brutal attempted the purchase only after optional spending reserves were
withheld. Recovery now runs before those reserves and optional builder orders,
uses actual unspent cash even when committed to gradual factory production,
and saves while the MCV is unaffordable or temporarily sold out. Existing units,
factory queues and paid Starport cargo prevent duplicate replacement orders.
This is missing-yard recovery, not a change to normal expansion policy.

Version 1.0.761, network protocol 19 (AI lockstep orders changed), save schema
unchanged. Native app is built at build/bin/dunecity.app in the isolated checkout
/Users/stefan/Documents/projects/dunecity-ai-mcv. All 16 existing CTest targets
pass, including the updated protocol expectation. Both newly registered Starport
CTest probes pass (classic and city mode, campaign level 9 setup): all four
difficulties, one credit short, exact price, sold-out/restock, two-port and
pending-delivery deduplication, plus actual cash reserved by an unpaid light
factory queue. All 18 targets verified across the full and focused runs.
Claude Max supplied the bounded recovery fixture; Codex reviewed, added the
queued-cash case and ran verification. No push, public release or install onto
the MacBook Air has been performed. Logs: ../outputs/ai-mcv/.
## 2026-09-22 — Production 1.0.760 published

User authorized production/website publication. PR #69 merged at 952bbbd6,
tag v1.0.760. CI caught lowercase definitions.h in CarryallFlight.h on
case-sensitive Linux/Emscripten; corrected to Definitions.h (12384f1d).
Mac rebuild/dependency audits and carryall probe passed after that include-only fix.
PR CI 35716132681 passed; stable release 35717278503 passed all platform/test,
Apple signing/notarization, signed update feed and publication jobs.

GitHub release has 13 assets (8 packages, 3 signed manifests, 2 appcasts).
Downloaded release and independently verified all manifest signatures, archive
sizes/hashes and both native appcast archive signatures against the checked-in key.
SourceForge 35718535053 verified all uploaded checksums, source branch/tag and
Windows/Mac/Linux defaults. No source archive uploaded.

Website a9ed96e (deploy 35718632273) publishes tagged CI browser artifact,
matching signaling package and current release prose on both download pages.
All 7 public browser files match live 1.0.760 manifest hashes/source 952bbbd6;
both public download pages point to 1.0.760. Redundant automatic browser build
35718535065 cancelled after packaging the exact tagged artifact.
Fresh Chrome startup reached the rendered main menu showing App v1.0.760;
verification tab closed afterward. This was a startup smoke check, not a new
multiplayer match. Evidence: ../outputs/release-760/. MBA remains locally
installed 1.0.760.

## 2026-09-22 — Combined local test build 1.0.760 installed on MBA

Merged helper population-cap commits b8d78daf and b9266479 with the local
carryall/unit timing, projectile alignment/physical AA interception and
crime-service spending fixes on branch test/all-local-760 (merge bb184a62).
All recent local gameplay commits are included. Version 1.0.760, protocol 18,
save format 9845. No public release, push or PR.

Build and dependency audit passed, all 16 CTest targets passed (158.34 seconds),
and the real-engine shared-spending crime-priority probe passed. Packaged SDL
runtime initialization and hidden-window rendering passed locally and on MBA.
Installed /Applications/dunecity.app on Stefans-MacBook-Air.local; binary hash
matches local package: 57b45b7814cd6cd5691d2ea8726724de97335c7c1eab42e6b23a62de9c2a7a16.
Previous app retained at /Applications/.dunecity-760-CNQ4FW/dunecity-previous.app.
All 511 checked profile INIs preserved unchanged; saves/preferences not modified.
No gameplay window launched. Receipts and test logs: ../outputs/combined-760/.
Claude Max bounded integration review found no blocker; helper cap and crime
priority exercised by their respective regression fixtures.

## 2026-09-22 — Projectile alignment and physical turret intercept (local 759)

Implemented Dynasty 4469449c missile speed/steering, exact 20Hz movement/15Hz
rotation, integer geometry, arming, scatter and actual-position explosions in
all three modes. Launcher anti-air fire skips the facing gate, doubles arming to
0.8s and guides at the live aircraft while retaining its scattered destination.
Restored turret triple range/visibility exception for ornithopters and close
cannon fire. Rocket splash affects both layers with quarter-tile falloff; removed
orni-only full splash. Gas radius/no structure HP damage corrected; house/mod
deviation chances retained. Trooper damage reduction moved to long-range rockets.
Palace uses corrected scatter and 17 Death Hand blasts at 200 damage; nuclear
plant balance remains independent. INI template notes updated; no invented keys.

Intentional deviation: strict Dynasty turret arrival gave 0/64 isolated AA kills.
Added a 1/8-tile swept relative-motion physical intercept, never a destination
snap. Final full-game encounters: launcher15/64, turret16/64 kills; same all modes,
384 encounters total. Mechanics192 cases, original-source trace32,826 exact
samples in540 standard scenarios (turret-air extension tested separately),
10,080 exact save/observer continuation frames and old byte migration pass.
Source reference ASan/UBSan and unit-speed/real aircraft attack-pass probe pass.
Existing13 CTest targets pass, including carryall, ground continuation, commands
and menu. All three newly registered projectile CTest probes also pass (16 total).
Save9845, protocol17. Local759 rebuilt; no push/PR/MBA install. Full results and
reproduction: docs/projectile-comparison.md; receipts ../outputs/projectile-fix/.
Claude Max supplied the bounded launcher/air review; Codex integrated and tested.

## 2026-09-22 — Protect crime-service construction from growth starvation

Diagnosed live MBA 1.0.758 session1790068138510777-0 (Alkozeltser4).
Ordos completed zero police/rocket turrets before elimination. Its planner
identified useful police sites, but opening infrastructure, extra power and
idle-yard zoning preempted the winning service. At cycle28748,515 credits
became a300-credit windtrap despite a500-credit police candidate (foundations
also cost money). Crime250 later spawned45 hostile units at cycle36114.
Neutral's first40-unit unrest preceded its first rocket turret (37596 vs55944).

QuantBot now preserves a winning crime-prevention investment against dedicated
city growth and the idle-zone fallback, and executes/reserves it before optional
power headroom, nuclear growth, opening tech and civic expansion. Actual blackout
recovery remains first. This handles police and eligible rocket turrets through
the existing service scorer; it does not remove turret tech requirements.

Real-engine shared-spending probe passes at DuneCity level9: single/multiple-yard
savings, later funded police orders with rocket tech unavailable, explicit rocket
orders, peaceful growth, parallel production, blackout and placement controls.
Unit CTest and dependency audit pass. Receipts: /tmp/dunecity-ai-defense/verified.log,
verified/run.log and ctest-final.log. Claude subscription diagnosis exhausted its
bounded run and focused retry without a final report; Codex completed diagnosis,
implementation and verification from source and live telemetry.

Local source fix only, no push, install or release. Running MBA758 is unchanged.
Before distributing, bump app version and network compatibility for changed
lockstep AI decisions; no save-layout changes were introduced.

## 2026-09-22 — Projectile comparison (audit only, app remains758)

Compared standard rocket/missile types against Dynasty4469449c. Current launcher/
gas18.75tiles/s vs15, turret18.75vs11.25, MiniRocket22.5vs13.75, DeathHand31.25vs18.75.
Current homing395.508deg/s vs Dynasty168.75 launcher/gas,675turret,421.875mini;
current mini/deathhand do not steer. Arming counters/air tracking and impact
coordinates differ; turret60/120cycle constructor override becomes0.96/1.92s
forced expiry, not Dynasty3/6s steering/arming counter. Dynasty does not have a
universal timeout explosion. Rocket scatter is~4x too small in current world units.

Controlled actual-code tests reproduced a turret missile snapping an explosion
onto an ornithopter8tiles away (25HP killed in16ms); Dynasty inflicted0. Our
orni-only full splash damage also differs:8damage at0.375tile vs4Dynasty, but
current0 at0.625tile vs2Dynasty. Gas effect radius and close turret AA handling
also differ. See docs/projectile-comparison.md for complete rules and boundaries.

171 current cases across3modes identical;48 original-source reference cases pass
ASan/UBSan. Fixtures measure projectile mechanics, not natural combat hit rates;
scatter is fixed in trajectory tests. Repro scripts committed; artifacts under
../outputs/projectile-audit/. No gameplay changes, push, release or installation.
Claude Max static-table audit followed by Codex runtime investigation/testing.
## 2026-09-22 — Restore missing shared-helper population ceiling on758

Fresh ai-audit.dls from the running MBA758 game has Fremen238440 population,
Neutral120000 and Rebels118840. Detailed telemetry had reached its capture
limit at19:22, so current statistics were recovered from the19:35 save using
matching758 source and its pinned mod, without advancing simulation cycles.
Full per-unit statistics: ../outputs/live-game-20260922/report.md.

The758 branch omitted the earlier fbbc3ce1 fix: getCityPopulationLimit returned
zero for every human-shared or support-mode QuantBot. Ported only its population
policy, comments and regression probe; preserved758 version/network metadata.
Shared helpers now apply their difficulty ceiling to their own construction;
human commands and natural shared-city growth remain unrestricted. Campaign
and unlimited difficulties preserve existing policy. Existing orders may finish.

Expanded actual build/growth tests to Hard as well as Easy/Medium. CTest unit
suite and quantbot_custom_attack pass; the latter checks helper refusal, actual
human orders and natural growth, with Brutal positive control. A separate load
of Stefan's actual save confirms the Hard Fremen helper reports120000 and adds
no population-bearing orders at238440. Receipts: /tmp/dunecity-live-audit/
cap-ctest.log and fixed-save-test.log (SAVED_FREMEN_CAP_PASS).

Source fix only on fix/helper-population-cap-758; no publish/install. Include
this change alongside the other pending fixes before the next release, bumping
app/network compatibility for changed lockstep AI decisions. Never copy754's
old version/protocol metadata over the newer758 branch.

## 2026-09-22 — Ground route timing aligned (local 758)

Completed issue67 follow-up: standard ground routes use Dynasty's15/60-second
script retry cadence, movement-before-routing order, reset accumulator/arrival
and diagonal-table timing, and exact heading completion on4/60-second rotation
ticks. Positions interpolate smoothly, retaining infantry slots and rock wobble.
Custom INI caps and city roads scale movement; nominal INI numbers remain correct.
Zero-speed units do not reserve tiles. Mod-only units and flight handling retained.
Source defaults document the new timing. Save9844 persists phase/deadlines/start/
endpoint; old saves finish the current Legacy step. Protocol16 gates new behavior.

Original Dynasty4469449c core plus UNIT.EMC reference:15,600 scenarios under
ASan/UBSan, repeated for produced/scenario units identically. Production game
loops run32,500 routes per mode (97,500 total), identical Vanilla/DuneCity/Dune2R.
260 groups:8/16tile sand; diagonals; rock/dunes/spice/slabs/mountain foot units;
damage; loaded harvesters;90degree starts; two-leg corner orders. Worst mean
error0.593%, within2% acceptance. Details: docs/unit-route-comparison.md.

CTest unit suite, menu, command, carryall and unit-speed probes pass. New route
continuation probe checks4,320 exact post-save/observer frames, custom caps/roads,
zero-speed occupancy and replacement commands. Reused command-probe profile had
a stale Workshop selection after default-template refresh; fresh isolated profile
passed. No user-profile reset. Full receipts: ../outputs/route-speed-alignment/.

Built758; dependency/version audits and portable packaged runtime pass. Installed
on MBA at /Applications/dunecity.app; all18 speed/turn INI values verified locally
and remotely, with Dune2R inheriting shared rules. Saved game-speed preferences
are unchanged (Vanilla4ms, city18ms, Dune2R16ms); compare at equal game speed.
Prior757 preserved at /Applications/.dunecity-758-4ove7wuf/dunecity-previous.app.
No push, PR or public release.

Claude Max review completed after fixing the worker invocation: --project expects
canonical slug dunecity, not checkout path. Memory service was healthy. Launcher
now validates the slug and distinguishes invalid requests from connection failure
(hermes-orchestration commit9ce8319). Claude flagged the zero-speed reservation
edge, which was fixed and regression-tested. Other conditional concerns were
checked against current caps, initialized save fields and complete observer loads.

## 2026-09-22 — Full-route timing comparison (no app change)

Compared757 ground travel with Dynasty4469449c using unmodified production
movement/map/pathfinder/script VM plus real UNIT.EMC from installed DUNE.PAK.
Headless UI/audio/network stubs; source-built ASan/UBSan reference passes and
reproduces initial CSV. All3360 Dynasty routes repeated with scenario=false
(produced units) identically. DuneCity's full updateGameState runs280 routes per
mode (Vanilla/Dune City/Dune2R), identical acrossmodes. Eight/sixteen flat-sand
tiles, aligned/90degree start, phase sweeps. Most straight routes differ1–4%;
Raider Trike and launcher-family take11–12% longer; Soldier/Saboteur arrive7–11%
sooner. Initial90degree tank penalty0.80s vs1.25s Dynasty because Legacy starts
movement on rounded heading plus different scheduling. Nominal speeds match,
complete travel times do not. No gameplay/config/preference changes in this turn.

Repro: tests/units/compare-dynasty-routes.py --dynasty-dir ../dunedynasty
--output-dir ../outputs/route-speed-comparison/repro. Data not committed.
Results: docs/unit-route-comparison.md, ../outputs/route-speed-comparison/.
Claude compiled initial reference objects but both bounded runs exhausted turns;
Codex finished harness/stubs and independently verified/reproduced timings.
Air saved speed preferences: Vanilla4ms, Dune City18ms, Dune2R defaults16ms;
normal-speed comparison uses16ms. User preferences left unchanged.

## 2026-09-22 — All standard unit movement aligned (local757)

Follow-up to carryall issue67 audits17 other standard mobile types against
Dynasty4469449c. Shared ObjectData now converts quantized normal-speed cruise/
concrete caps and nominal rotation correctly. Ground movement applies entered-
terrain throttle, <half-health reduction and harvester0–100 load before Dynasty
integer quantization; smooth updates retain the average below16-step cadence.
Removed the duplicate half-speed damage penalty for these standard units,
unified infantry slot movement modifiers, and corrected tank/siege turret rates.
Custom caps scale the rates; city roads retain4x. Mod-only units retain explicit
legacy behavior. Existing saves keep caps but use new modifiers. No save fields;
protocol15 prevents mixing changed simulation with756/protocol14.

Source-extracted Unit_SetSpeed with parsed Dynasty unit/terrain tables runs under
ASan/UBSan and reproduces committed oracle CSV. Real production-object tests
measure translation/turning across terrain, damage, load, cardinal/diagonal and
infantry slots in Vanilla/Dune City/Dune2R; all-house stats, roads, entered terrain
and custom caps checked. Full headless Game::updateGameState completes14 ground
unit routes and two damaging ornithopter attack passes per mode. Cross-mode CSVs
are identical. CTest unit suite, command/menu/carryall probes and unit-speed probe
pass; version/dependency audits pass. Initial failures were stale protocol test
expectation and diagnostic setup/handwritten expectation errors, since corrected.

Installed757 on Stefans-MacBook-Air.local at /Applications/dunecity.app; archive
SHA256 verified and bundled runtime/hidden rendering passed at final path.
Prior756 backed up at /Applications/.dunecity-757-g5jiz5bo/dunecity-previous.app.

Details and reproduction: docs/unit-speeds.md. Receipts ../outputs/unit-speed-audit/.
This is nominal-rate alignment, not a port of Dynasty's pathfinder/VM/heading
rounding. Flight combat/Frigate approach remain existing engine behavior; Infantry/
Troopers squad map entries still expand into individual units. No push/release.

## 2026-09-22 — Dynasty carryall turning and approach (local 756)

Follow-up to issue67: live production-object baseline confirmed the inherited
2-tile snap added16 world units/axis/update on top of forward flight, defeating
the speed cap. An aligned pickup from1.5 tiles took0.080s. Removed double
movement; applied Dynasty's distance/heading throttle, half-tile final docking
at0.25 tile/s/axis, and arrival-distance-based cargo deployment. TurnSpeed0.09
matches253.125deg/s (previous0.099 was278.4375). The same pickup now takes2.208s.
Turning/docking use our smooth fixed-point updates, not Dynasty's discrete VM.
Shared caps cover Vanilla/Dune City/Dune2R; custom caps are respected.

Tested real Carryall objects across40 pickup/delivery cases per mode, moving
and cancelled/disappeared targets, and repair-yard delivery/collection/return.
All three modes produce identical seeded trajectories. CTest unit suite,
command regressions, menu probe and new carryall_flight_probe pass. Source-
extracted Dynasty functions independently checked and executed with ASan/UBSan.
Upgrade testing exposed stale Workshop-materialized Dune2R ObjectData; managed
refresh now catches it, with an explicit regression in the menu probe.

Built1.0.756 locally at build/bin/dunecity.app; version/dependency audits pass.
Network14 prevents joining older simulation rules; no save fields added.
Saved ObjectData caps remain saved, while new approach code applies on load.
No push/release. Details: docs/carryall-speed.md; receipts:
../outputs/carryall-handling-67/ and build/carryall-flight-probe/.

## 2026-09-22 — Carryall cruise speed aligned with Dynasty (local 755)

Issue67 reported overly fast carryalls in Dune2R1.0.737. Pulled
gameflorist/dunedynasty at4469449c and traced its normal-speed movement:
192 position units every3 ticks at60Hz /256 units per tile =15 nominal tiles/s.
The old shared carryall cap was18.75 tiles/s because its conversion multiplied
Dynasty pixels by5 instead of the actual4x tile scale. Changed MaxSpeed19.2
to15.36 in the shared ObjectData template. Dune City, Vanilla and Dune2R all
load this value; Tornie's independent override remains outside this request.

App1.0.755 built locally in dunecity-carryall-audit/build/bin/dunecity.app.
Version consistency and post-build Ninja dependency audit pass. CTest
dunelegacy_tests and menu_navigation_probe pass; the latter loads real installed
data for all three modes and every house at640/854/1280. No push or release.
Existing saves retain serialized unit data. Approach/turning logic and automatic
repair behavior are unchanged; see docs/carryall-speed.md for source derivation
and the separate repair-loop finding. Receipts: ../outputs/carryall-speed-67/.

## 2026-09-21 — Public753 release preparation and bundled-mod lobby repair

User authorized public installers/browser deployment; bumped app752->753 so Air
can test its real updater. Keep Air752 installed; nuclear remains tech6, mod1.002.
Preflight real browser pairing exposed stale onReceiveModInfo ws-hash-name-only
guard rejecting identical installer Vanilla content. Claude supplied a helper;
Codex wired it into lobby handling and tests. Bundled names now require installer
approval AND exact pinned content hash; ws aliases still undergo following full
content/checksum checks. Both fresh bundled and11MiB custom-mod browser runs pass
pair/cancel/retry/lobby/start/commands/quit/reload, zero command drops. Updated
smoke expectations: bundled start performs zero uploads; custom remains published.
Release PR66. CI/publication receipts follow after successful deployment.

## 2026-09-21 — Local752 custom QuantBot city ceilings

Implements approved displayed-population ceilings per AI house, by map tile area:
<=1024: Easy5000/Medium10000/Hard20000; <=4096:10000/20000/40000;
<=16384:20000/40000/80000; larger:30000/60000/120000. Brutal and Defend
uncapped. Shared total R+C+I zone cap scales from48 zones per20000 population
(rounded up); QuantBot chooses its mix. Orders reserve queued population and
zone slots across yards. Runtime growth shares the house budget across nodes
and reserves accepted construction orders. RTS city roles and both Palace
population contributions count. Existing oversized cities are preserved, with
further positive growth blocked; zero-population services remain buildable.
Campaign policy, human/shared houses and support-mode helpers are excluded.
No change to approved military limits, attack percentages, palace or yard rules.
App1.0.752, independent mod1.002, network13 rejects incompatible simulations;
save layout remains9843. No server update, push or public release.

Claude supplied initial policy/planner work in bounded subscription runs, then
hit turn limits; Codex completed integration and verification. All ten CTest
groups pass across full run and focused fixture corrections. Tests cover the
matrix, boundary/queued admission, real Easy growth blocking, uncapped Brutal,
and human-shared/campaign exemptions. Initial60-minute FourQuadrants run reached
48 queued-inclusive zones and20020 population; final queue reservation correction
and follow-up simulation passed:225000 cycles/60 simulated minutes,67919 enemy
house events, max20000 displayed population and48 queued-inclusive zones.
The other house is a human-shared helper and intentionally remains unrestricted.
Receipts: ../outputs/mba-test-1.0.752/validation/city-cap-simulation.json.

752 delivery: sourcee1e468bb, app notarization Accepted. Installed and verified
/Applications/dunecity.app on Stefans-MacBook-Air.local; existing Desktop shortcut
preserved,751 backed up, game not launched. Signed-archive/installed executable
SHA256 b650b94f6077cd8924535a4c8f6ed70f0fc97e7b213864a1e44bc2bbb538395d.
Receipts: ../outputs/mba-test-1.0.752/verification.json and air-install.json.

## 2026-09-21 — Local751: mod identity, lobby admission and building caps

Dune City mod is Stefan's independently versioned1.001 (`DUNECITY_MOD_VERSION`),
not the app's1.0.751. Picker and in-game MOD label use mod metadata; main menu
labels app version explicitly. GameVersion remains installer-refresh metadata.
Hotjoin approval verifies actual payload against installed defaults/bundled assets,
including metadata, rule files and asset content; folder name alone is insufficient.
Changed/new mods use pregame lobby. No startup upload added. Cached per-file hashes
avoid repeatedly reading unchanged assets; directory/file stamps rechecked. Optional
Dune2R packs differing from installer fail closed into lobby. Existing snapshot dedup
and pinned revisions remain. Full AI rosters can create lobby; existing AI takeover
and spectator paths retained. UI guidance no longer demands an Open seat.

QuantBot palace ceiling: Easy1, Medium3, Hard/Brutal no additional difficulty cap.
Existing population target and stricter scenario rules still apply; owned/queued
items count across yards. Central production guard also covers campaign rebuild.
Game Rules adds construction-yard limit per house (-1/0 unlimited, positive1..999).
Both humans/bots: MCV deployment at cap refuses without consuming MCV, including
same-tick subsequent deploys. QuantBot avoids ordering unusable extra MCVs.
Persisted defaults, mod settings, equality/hash, network validation all carry option.
MOD5 setup extension, save9843, network12: old saves load unlimited; old clients
cannot join new games because they cannot enforce the new rule. No service update.

Claude bounded investigations hit turn limits; palace worker supplied policy/tests,
yard worker partial initializer. Codex completed/reviewed implementation and tests.
City-cap analysis worker produced telemetry aggregate/report. Population caps are
PROPOSED ONLY, not implemented. Last MBA game1789987534476330-0: FourQuadrants128²,
Easy house4, seed852790524, build750. Peak displayedpop234500, peak queued-inclusive
zones471, end actualzones463, yards8/palaces8; grosscitytax719574, net628469,
spicerefined814320, endingcredits823411. No custom population/zone cap currently.
Do not conflate this with campaign-only50percent-income/zone policy.

Validation receipts and signed local build: ../outputs/mba-test-1.0.751/validation.
All ten CTest groups passed across full run plus focused corrective reruns. Added
modifiedrules/metadata/assets and extra-payload rejection, AI-filledlobby, modidentity,
yardUI, actualMCV preservation/deployment and MOD4/MOD5 tests. Native dependencies
and sourceversion audits pass. No push or public release. Installation receipt follows.

751 delivery verified: source3fa6c862, Apple accepted app and DMG. Installed
/Applications/dunecity.app on Stefans-MacBook-Air.local with existing Desktop
shortcut;750 backed up, game not launched. ExecutableSHA256
7c29b73f87b3f05633738c0ce32da80cbc7f464a955f494a92ae9dc470cfa331
matches signed archive. Receipts: ../outputs/mba-test-1.0.751/verification.json.
Population proposal: ../outputs/mba-test-1.0.751/city-cap-proposal.md.

## 2026-09-21 — Approved custom military targets and percentage-only attacks, 1.0.750

Supersedes749's added custom attack caps. Easy military production target is now
15000 on Large maps (4097–16384 tiles) and25000 on Huge; Medium25000/40000.
Small/Medium map buckets, other difficulties and campaign rules are unchanged.
Both constructor defaults and shipped QuantBot Config.ini.default match. Normal
version-based official-mod reseeding supplies the defaults on next launch.
Custom attack budget remains the configured percentage of CURRENT owned ground
combat value: Easy25%, Medium40%, Hard50%, Brutal60%. Existing hunters, including
forced/busy survivors, consume that budget. Removed the8/14 headcount and2400/4200
flat value caps. No replacement headcount/composition restriction. Existing
Easy/Medium home-reserve behavior retained. Engine/map population rules and
campaign wave caps are separate and unchanged. No save/network-format change.

Claude supplied the scoped source/config/test patch in its20-turn bounded run
(turn limit before final report). Codex reviewed it and added loaded-template
checks plus an oversized production-target fixture proving dispatch still uses
owned ground troops. All ten CTest groups pass; dependency/version audits pass.
Real-engine76-trooper fixture sends19 Easy/30 Medium, repeated forced hunters
cannot stack attacks, and campaign helper rally is unchanged. A fresh20-minute
Four Quadrants simulation (seed1330896984, AI-controlled test houses, not an
exact replay of Stefan's human orders) reaches75000 cycles. Both Easy houses
load15000 targets; all14 dispatches respect owned-ground percentage budgets,
without count/flat caps. First dispatch6 units/3250 from13000 ground value.
Evidence: ../outputs/mba-test-1.0.750/validation and /tmp/dunecity-750-balance.

Local Mac package and installation receipts will be recorded in
../outputs/mba-test-1.0.750/verification.json. Keep the normal app/Desktop shortcut,
back up749, and never interrupt a running game. No push/public release requested.
Old saves/replays may pin previous mod configurations; use a new match for these
updated military targets.

Delivery750 verified: source065ae7ea. App and DMG accepted by Apple; installed
/Applications/dunecity.app on Stefans-MacBook-Air.local with existing Desktop
shortcut retained. Installed executable SHA256
1ade8b90c006a3605e53551f93d1a315e98be11333144b811d7ea11998d38871
matches the signed archive. The installed bundle contains all four new military
limits.749 backed up under DuneCity Local Test/backups. No app launch or profile
editing. Full receipts: ../outputs/mba-test-1.0.750/verification.json.

## 2026-09-21 — Custom Easy/Medium attack pressure, local 1.0.749

Stefan's MBA Four Quadrants128x128 tech8 custom Easy replay (seed1330896984)
reproduces76 attackers /17650 value at cycle37695 in748. The configured25%
was computed then ignored by the custom dispatch branch. Campaign selectors
already applied separate wave limits. Custom now strictly honors configured
percentage of ground combat value, subtracting existing hunters; Easy also caps
simultaneous hunters at8/2400 value, Medium14/4200. Both caps apply, including
forced/busy survivors; no oversized-single-unit exception. Hard/Brutal use their
configured percentages without new absolute caps. No campaign economy, attack
schedule, save or network format change. Easy/Medium idle reserves use the home
rally instead of following the HUNT centroid. Their defensive kiting/retreat
centers exclude hunters. These changes explicitly exclude campaign helpers even
when their internal gameMode is Custom; real-engine coverage verifies that boundary.
Custom light raider dodges preserve HUNT so they remain
counted. Telemetry adds ground budget, simultaneous pressure and explicit caps.

Claude supplied a partial implementation in the bounded retry (both runs hit
turn limits). Codex reviewed/completed survivor accounting, isolated changed
rally behavior to Easy/Medium, preserved raider membership, added regressions,
and verified the actual replay. Ten CTest groups pass including new real-engine
custom-attack fixture; unit budget/count/zero/determinism cases pass. Existing
army movement fixture plus raider dodge passes at level9. Campaign pressure
fixture passes at level9 with explicit harvester limit7 (its prerequisites;
default level4 lacks required houses and default unlimited workers violates its
worker-cap assertion). Replay through85000 cycles passes: first attack4/2400,
all10 dispatches keep total committed pressure <=8 units and <=2400 value.
These are simulations of recorded commands under the new AI, not a claim that
the changed match follows the original state after the first behavior change.

Build/dependency audit/version checks pass. Local Mac package/delivery receipts
will be recorded in ../outputs/mba-test-1.0.749/verification.json. Stefan quit748 and explicitly requested replacing the normal installed app
and retaining the existing Desktop shortcut. Nothing pushed or publicly
released. Restart into749/new match for the new behavior; loading a pre-fix save
does not automatically recall units that were already attacking.

Delivery749 verified: source5cd86309. Signed, Apple-accepted app installed at
/Applications/dunecity.app on Stefans-MacBook-Air.local. Existing Desktop shortcut
retained;748 backed up under DuneCity Local Test/backups. Installed executable
SHA256e585804857eec49040cb52e4da0a2bd5d34a2b162f047603a25dbd2095b5c8b5
matches the notarized archive. No app launch/profile modification. Verification
receipt: ../outputs/mba-test-1.0.749/verification.json. Final signed artifacts are
under verified-signed/; earlier final-signed/ artifacts were superseded before
installation by the campaign-helper-scope refinement.

## 2026-09-21 — Four Quadrants crash, menu resume and mod choices, local 1.0.748

Reproduced Stefan's actual MBA replay from 1.0.747: Four Quadrants, seed545318708,
throws `Tile (-1, -1) does not exist!` at cycle39600. Throw-site stack identifies
`Sandworm::canAttack -> QuantBot::checkAllUnits -> QuantBot::update -> House::update`.
This coincided with a Starport purchase, but the exception came from an existing
sleeping sandworm being evaluated as an attacker. `sleep()` deliberately removes
it from terrain, marks it inactive/invisible, and parks it at INVALID_POS until
respawn. `canAttack` now rejects an invalid own position before reading terrain.
Exact replay passes through60000 after the fix. New real-engine regression covers
sleep, redeployment, valid connected-sand targets and off-map targets; registered
with command regressions in CTest. No simulation/save/network format bump.

Host Back to Game / Escape now releases only the pause created by that Options
menu. Manual pre-existing pauses remain explicit. Closing while the command is
in flight releases its eventual pause; a later distinct pause is preserved.
Native three-peer controls test passes normal close, Escape and rapid close,
manual pause preservation, paused spectator join and matching state through1900.

Map chooser says Next. Campaign/custom/editor mod choices show official Dune City
as `Dune City 1.748`. Content-derived automatic Workshop copies from old builds
are represented by the official entry, including correct initial selection when
a cached name was active. Changed authored mods remain separate. Picker filtering
is separate from the complete registry, so pinned save dependencies stay intact.
Metadata-only enumeration avoids rehashing every official cached asset per menu.

Validation: native dependency audits and version check pass; all nine CTest groups
pass (new terrain fixture corrected, then its group rerun successfully). Menu
rendering tested640x480,854x480,1280x720. Actual replay before/after plus native
three-peer controls evidence under ../outputs/mba-test-1.0.748/validation/.
Browser interactive play not repeated. No public push/release/update feed change.

Remote access to MBA verified: Stefans-MacBook-Air.local, userstefan, arm64.
claw.local already had the authorized DuneCity Air identity; added its IdentityFile
mapping to local ~/.ssh/config. No Air SSH settings or credentials changed.
MBA diagnostic session1789979475402524-0 and auto.rpl preserved in local evidence.

Delivery verified: source64205d48. App and DMG accepted by Apple; signatures,
staples, Gatekeeper and mounted portable runtime pass. Local DMG URL:
http://claw.local:18738/DuneCity-1.0.748-macOS.dmg
SHA2568e9c763fe327dafb799e50d4660b6dc14e7feb29af7a245fbe8d46547853618e.
Installed on MBA at /Applications/dunecity.app; signed executable SHA256 matches
packaged ZIP. Desktop/DuneCity.app is a shortcut. Previous747 retained under
Library/Application Support/DuneCity Local Test/backups/dunecity-1.0.747.app.
No game launched or user profile modified during installation. Full verification
and receipts: ../outputs/mba-test-1.0.748/verification.json and validation/.

## 2026-09-21 — Host Options auto-pause, local 1.0.747

Opening the host's in-game Options menu (including Escape) requests the existing
synchronized shared pause. Guest/spectator menus stay local. Entry while paused
or pending never toggles Resume. Closing Options preserves the shared pause;
any active player can explicitly resume. Other popups are unchanged. The open
Options menu refreshes its status and Pause/Resume button as shared state changes.

Claude supplied the four-file patch within its bounded run (turn cap before final
report); Codex reviewed it and extended the regression to retain the same menu
through pending -> paused and verify button/notice refresh. All eight CTest groups
pass. Native three-peer test passes host-menu pause at cycle711, guest-menu
isolation, repeated paused entry, explicit resume and matching state at1800 and
after spectator departure at1900. Screenshots checked. No protocol/save change;
version bump747 includes all746 controls. Browser/ENet interactive play not repeated.
Evidence: /tmp/dunecity-host-menu-pause and ../outputs/mba-test-1.0.747/validation/.
Local delivery complete: source aeab3f50; Developer ID signature, Apple app/DMG
acceptance, staples, Gatekeeper and mounted portable-runtime checks pass.
Download verified byte-for-byte at
http://claw.local:18738/DuneCity-1.0.747-macOS.dmg.
Durable receipts/hash/tests: ../outputs/mba-test-1.0.747/verification.json and
validation/. Nothing pushed or published to public release/update channels.

## 2026-09-21 — Multiplayer host settings and shared pause, local 1.0.746

Implemented host-only game-speed changes during play, exposed Game Settings to
all peers for personal sound/scroll/credits-sound controls, and added top-bar and
Options Pause/Resume buttons plus Space. Any human playing seat can pause/resume;
spectators cannot. Closing Options does not lift an explicit shared pause.
See docs/multiplayer-match-controls.md for protocol, synchronization and tests.
Protocol11; observer runtime4 (reader also accepts3). Existing source/build745
features remain included. Everyone in a test match must use the new app.

Claude's bounded network implementation and focused completion run hit their
turn limits without a final report; Codex reviewed and completed integration,
UI, regression coverage and the spectator-specific control delivery/epoch fix.
All eight CTest groups pass across the full run and corrected unit rerun; 200
Node relay tests pass. Real native three-peer WebRTC test passes client pause,
host resume/speed, concurrent pause requests/client resume, menu-close behavior,
malformed payload validation, and spectator admission at paused cycle711.
Host/Partner/Newcomer match at cycle1800 and original peers match after departure.
Screenshots reviewed for toolbar and host-only settings. Evidence:
/tmp/dunecity-multiplayer-controls/live-4 and ctest*.log, relay-tests-2.log.
Interactive browser crossplay and ENet LAN play were not repeated.
Local delivery complete: source019a70f3; Developer ID signature, Apple app/DMG
acceptance, staples, Gatekeeper and mounted portable-runtime checks all pass.
Download verified byte-for-byte at
http://claw.local:18738/DuneCity-1.0.746-macOS.dmg.
Package and durable evidence: ../outputs/mba-test-1.0.746/verification.json and
validation/. Nothing pushed or published to the public release/update channels.

## 2026-09-21 — Implemented QuantBot city campaign economy, local 1.0.745

Implemented the accepted 150%-of-vanilla planning target and the level/difficulty
shared RCI matrix; see docs/quantbot-campaign-income-150.md for exact counts and
validation. Frozen original refinery multiplier/minimum budget; no new RTS types;
shared placed+queued caps; queued workers reserve refinery delivery slots. Hard/
Brutal get total caps24/40 only after30 game seconds without map spice or owned
cargo. New blooms remove extra future expansion. Normal earned income is untouched.
Scope is Dune City campaign enemy QuantBot only. Starting assets are retained.
Save9842 stores policy state; older saves recover original authored permissions
through the campaign-specific resource loader. New saves need the newer app.

Claude's bounded core implementation and completion attempts hit their turn caps;
Codex finished integration and review. A separate bounded Claude task delivered
11 unit cases including all36 matrix entries. Eight CTest groups pass, plus
real-engine Easy2/Hard5/Brutal9 probes. Three ordinary campaign runs checked134
enemy snapshots with no cap violations or new RTS types. Hard5/Brutal5 observed
net income1748/3030 per minute versus goals1800/3150; not a full balance study.
Evidence and measured limitations are documented alongside the matrix.
Native spectator hot-join also passes with matching state at cycle1800.
No browser-crossplay or public release claim.

Local delivery complete: Developer ID signed, app and DMG accepted by Apple,
stapled and Gatekeeper-verified. Mounted package runtime/portable-library checks
pass. Download verified byte-for-byte at
http://claw.local:18738/DuneCity-1.0.745-macOS.dmg.
Package: ../outputs/mba-test-1.0.745/final-signed/DuneCity-1.0.745-macOS.dmg.
Source implementation835b5794; verification.json records SHA256 and Apple receipts.
Nothing pushed or published to the public release/update channels.

## 2026-09-21 — User target: 150% combined income; post-spice expansion

New design in `docs/quantbot-campaign-income-150.md` supersedes lower-income targets.
User requests50% more TOTAL harvesting+tax income versus the original vanilla
house budget, plus more RCI after map spice exhaustion on higher difficulties.
Interpret higher as Hard/Brutal (proposal assumption). Freeze original budget;
do not rebase against zero spice income or add a second50% boost. Hard/Brutal
post-spice taxes aim to replace lost harvesting within the SAME total goal.
Easy/Medium retain normal zone allowance. Counts/shared caps remain flexiblemix;
no fixedRCI quotas, no fakecash/tax clipping, no new RTS building-type permission.

Exact DESIGN GOALS for vanilla1/2/4/7worker budgets are450/900/1800/3150credits/min,
using the prior300perworker planning assumption. Revised harvest/tax split and
all36settings/48house budgets in ../outputs/quantbot-income-150/per-house-budget.csv.
These are targets, not measured future incomes. Old physical zonecaps require
recalibration:20testedzones yielded1628–1755/min, not proof of3150Brutal tax-only.
Do not claim an untested30/40zone count will hit it. Proposed post-spice guard
uses fresh sustained mapwide zero, allworker/cargo accounting, savedinvestment
funds, gradualfinitecap expansion and handling of spice blooms restoring income.
Claude boundedread-only audit hitturnlimit; Codex reviewed identifiedsourcepaths.
Arithmetic assertions pass. Proposal/docs only; no game/build/deployment change.

## 2026-09-21 — Corrected refinery-based campaign economy comparison

User identified wrong balance denominator: previous proposal used observed opening
fleets instead of refinery multiplier/minimum rules. Supersedes that matrix with
`docs/quantbot-campaign-economy-comparison.md` and
`../outputs/quantbot-ric-balance-comparison/complete-per-house-comparison.csv`.
Verified source + actual paired-run configs: Easy1x/min0, Medium2x/min0,
Hard text2.5 parsed integer2x/min2; mission21+ explicitly sets Hard refinery
allowance2. Brutal ini999x/min4 is superseded at runtime by enemy target7.
Spice/options/house ceilings also apply. The old diagnostic field named
configured_harvester_limit is maximum runtime harvester_ai_limit, not raw config.
48 paired house rows match formula; fleets remain separate from limits. A new
refinery can grant a harvester checked only against engine ceiling; do not claim
the observed16-worker event chain was proven or permitted by the AI target.

Revised proposal, still unimplemented: per economic enemy house, Level2
Easy1H/1zone, Medium2H/2zones, Hard3H/5zones, Brutal6H/6zones. Later normal-budget
Easy1H/3zones, Medium2H/8zones, Hard2H/12zones, Brutal3H/20zones; smaller original
level9 houses scale down. Counts are permissions, not grants. Preserve starting
RTS-type restrictions, no economy for scripted zero-economy houses, no deletion
of initial workers, no artificial income/tax cap. Full-fleet projection uses
300/min per worker consistently plus controlled net-city samples; NOT actual
future AI average income. Target tests/release remain pending. Source audit used
one bounded Claude Max worker plus one focused retry; Codex rejected unsupported
worker inferences about over-limit harvesters and checked parser/config/runtime
source independently. No runtime edits, app build, version change or deployment.

## 2026-09-21 — Revised joint harvester / shared-zone balance proposal

User rejected fixed R/I/C quotas, treating six as an Easy requirement, absent harvester
substitution and inadequate tax scaling by difficulty. Revised proposal is in
`docs/quantbot-campaign-balance-proposal.md`; it is not implemented. Shared TOTAL RCI
caps let QuantBot choose residential-heavy mixes. Normal tax remains; previous artificial
AI tax-cap suggestion withdrawn. Level2 proposed1H/1zone Easy,1H/2zones Medium,
1H/5zones Hard,1H/6zones Brutal; early two-harvester observed Hard/Brutal reference
trades one harvester for taxes. Later references adapt to larger vanilla fleets;
late Easy1H/3zones, Medium2H/8zones, Hard2H/12zones, Brutal2H/20zones. One-refinery
level9 Easy/Medium houses need lower1H/1zone and1H/3zone allowances. Preserve actual
starting assets and RTS-type restrictions; caps do not grant harvesters/factories.

Collected28 additional one-hour real-engine controlled residential-heavy cases on
Atreides level2/9, last10-minute income. Level2:1R31.8net/min,2R66.5, fivezones315–389,
six361–468. Level9:threezones335.7,eight533–633,twelve829–862,twenty1628–1755.
These are sampled mixes/layouts, not guaranteed yield ranges for all cities. Full
matrix is a starting balance proposal requiring live capped-AI verification. Early
fleets are observed in one opening sample, not universally proven harvester ceilings.

Used claude-engineering shared-memory wrapper with projectdunecity/Max auth for bounded
12-turn worker; it extended diagnostic capacity/power provisioning, exhausted its turns.
Codex added missing ZonePower include, compiled diagnosticTU, ran28 cases and checked
cash reconciliation, sufficient power, no units and fixed structures. Dependency audit
passes. No shipping gameplay, version, saves, deployments or user game changed.
Evidence: ../outputs/quantbot-ric-balance-proposal/{proposal.md,proposed-joint-matrix.csv,
results.csv,results.json,measurements}. Diagnostic capacity raised12→20zones.

## 2026-09-21 — Measured QuantBot and controlled city income; prior forecast withdrawn

The user challenged the unsupported assumption that six zones add30/min. Replaced
that proposal in docs/quantbot-campaign-economy-proposal.md with actual measurements:
36 paired vanilla/DuneCity campaign settings plus27 controlled60-minute city fixtures.
Default7% tax, last10-minute Level2 Atreides samples:2R/2I/2C yielded199.0 net/min;
3R/1I/2C467.7;2R/2I/2C plus one100%-funded police station94.5. These include actual
police and power costs. Adding an assumed300/min harvester projects499/768/395 total;
these are not measured capped-AI gameplay outcomes. Six zones cannot be assumed to
produce30/min; mix/density/land value/service costs matter. Future caps remain unvalidated.

Current unrestricted AI measurements: Level3 Easy2521.8/min combined versus280.9
vanilla (+797.8%), with33 peak zones; Level5 Easy4307.5 versus573.2 (+651.5%),63 zones.
Full36-row matrix sums all enemy houses; per-house CSV available. One seed/first layout
per level, automated human-side helper, up to10 game minutes; rates use actual elapsed
cycles when campaigns finish early. No claim of reading the user's unavailable MBA log.

Claude subscription worker produced diagnostic scaffolding within two bounded runs.
Codex completed/reviewed it: road placement returns no object; player callbacks must be
suppressed inside House::update; takeCredits drains city cash first, so meter power and
police costs directly; use minute50–60 for a true10-minute window. Assertions verify
conservation, no units, fixed structure counts and sufficient power across all27 traces.
Only diagnostic main TU compiled and linked; dependency audits pass. No shipping runtime,
save format, app version, service or gameplay balance changes. Current local app remains744.
Evidence: ../outputs/quantbot-income-measured/{measured-report.md,measured-team-matrix.csv,
measured-per-house.csv,controlled-zone-income.csv,controlled-fixtures,dunecity-telemetry}.
Original estimated matrix is superseded; physical caps need a separate verified income policy.

## 2026-09-21 — Classic campaign results and actual tax receipts, local 1.0.744

Restored the original FAME.CPS framed results artwork replaced by the 716 menu
restyle. Dune City content alone gets a fourth animated Tax collected by group;
its four row backgrounds reuse the classic artwork, with compact label spacing.
Vanilla/Tornie/Dune2R retain three groups. Team classification and score are unchanged.

House tracks gross fixed-point receipts at the actual city payout, separately
from spice, spendable city funds, service costs and projected annual revenue.
The counter saturates safely at one billion credits. Save format 9841 persists
it; older saves remain readable with zero historical tax receipts. UI totals
use display-only doubles to sum houses without fixed-point overflow. Older
apps cannot read the new save format; existing version rejection remains.

Claude investigated the original art and produced the restoration and partial
tax implementation via the subscription CLI. Codex reviewed/integrated it,
fixed overflow/gating/layout, added tax save compatibility and display probes,
and verified actual renders at 1024x768 and 640x480. All eight CTest groups pass;
real House roundtrip/9840 alignment, spending/refunds/spice independence,
mod gating, team attribution, saturation and finished animation probes pass.
Two native peers hot join/promote with matching cycle-1800 state:
76292739 / 215 objects / 15a4cab16d7019d4 / 454ef57cf3e1da2b.
Evidence: ../outputs/mba-test-1.0.744/{stats-city-final,stats-compact,stats-vanilla-9,hot-join}.

Local 1.0.744 delivery is complete: Developer ID signed; app and DMG notarization
Accepted, stapled, Gatekeeper accepted. Full LAN-download SHA256 verified:
82643de359daad46de2965124018783f11564ad80c4bbc55edc7e062af4ca13c.
Download http://claw.local:18738/DuneCity-1.0.744-macOS.dmg.
Evidence: ../outputs/mba-test-1.0.744/verification.json. No public release/update feed changed.

Economy investigation: 36/36 unchanged-vanilla simulations completed (Atreides,
first layout per level, seed486409243, up to10 game minutes, automated human-side
helper). See docs/quantbot-campaign-economy-proposal.md for the requested full
level1–9 Easy–Brutal matrix, explicit modeled-income assumptions, actual sample
results, conditional tax/harvester substitution, and limitations. The matrix is
not implemented. Easy can have two harvesters in later layouts (one per initial
refinery); Brutal samples exceeded its nominal7 ceiling, peaking16. Source path
city campaigns bypasses restricted rebuild planner; original MBA logs unavailable.
Raw telemetry/CSV and repeatable runner are under ../outputs/mba-test-1.0.744/economy-matrix.

The campaign AI economy/base-size issue is a separate requested investigation
and proposal; no QuantBot balance changes are included in this build. User wants
combined RCI caps (level2 Easy example6), modest total-income uplift over vanilla,
and greater tax substitution for harvesters on higher difficulty. MBA game logs
were not available on this host; do not claim its actual last game was inspected.

## 2026-09-21 — Sidebar mission skip, local 1.0.743

Restored Skip mission in the right-hand campaign controls, beneath Pollution in
city mode and Paths in ordinary campaigns. A burgundy background distinguishes
it from routine controls. The button opens a direct confirmation with Cancel
focused; Cancel or Escape returns to gameplay. Confirm uses the existing
authorized deterministic campaign command and victory/next-level path. Options
retains its existing skip action. Replays/spectators/non-campaign games cannot
skip, and the sidebar action hides while object controls occupy the sidebar.

Native build/dependency audits and the real command-execution probe pass. The
expanded probe checks city layout within a 480px interface, rendered color and
dialog screenshots, cancellation/Escape, exactly one confirmed command, replay
visibility, selected-object overlap, normal progression and final mission exit.
Evidence is in ../outputs/mba-test-1.0.743/command-probe/. The prior compression,
fast campaign startup, lobby policy and complete editor changes are retained.
This is a local Mac candidate; no push or public release.

Final source: 54ec0573. All eight CTest groups pass; after the final Escape
dispatch correction, the command probe passes using actual SDL Return/Escape
events. Signed/notarized app and DMG plus packaged runtime checks pass.
Final package: ../outputs/mba-test-1.0.743/final-signed/DuneCity-1.0.743-macOS.dmg
Download: http://claw.local:18738/DuneCity-1.0.743-macOS.dmg
Downloaded SHA256: 06455eac013ceccfb4272a0d01129e4d4f350deb28374bcf91aa49d16fca64a6.
Evidence: ../outputs/mba-test-1.0.743/verification.json. Earlier staging folders
native/ and signed/ were superseded by native-final/ and final-signed/.

## 2026-09-21 — Compression integrated into the MBA candidate, local 1.0.742

Imported 7979748be11131e8813925e09325308606b1c5ab from the separate local
improve/snapshot-transfer-compression worktree onto the 741 candidate. Kept both
Workshop and compression test sources when resolving the overlapping CMake edit,
and retained both worktrees' handover history. This candidate includes the fast
approved campaign startup, lobby-only authored mods and complete city editor.
Compression changes the spectator checkpoint transfer only; it does not add mod
uploads or change campaign/lobby policy. Zlib is linked for native and browser
builds, with the existing negotiated raw-transfer fallback retained.

Combined verification: native and browser builds pass, all eight CTest groups
pass, and the native ASan/UBSan and wasm32 wire harnesses each pass 92 checks.
The standalone native harness was launched directly with
DYLD_LIBRARY_PATH=/opt/homebrew/lib after compilation; passing it only to the
system bash wrapper loses that variable and leaves SDL2's missing-SDL3 alert open.
The packaged runtime initialization/rendering check passes independently.

The real solo-host Twin Cities spectator/promotion probe sends 5,976,622 raw
checkpoint bytes as 100,593 wire bytes and resumes both peers at cycle 1800 with
217 objects and identical state digests. Actual Chromium clients running play742
also pass direct Dune City campaign startup and hot joining into live spectating.
Browser promotion and cross-device/NAT conditions were not repeated in this turn.
The original compression worktree remains unchanged. Nothing pushed or released;
no production server update was needed.

Build source: 1a090ec3. Signed/notarized DMG:
../outputs/mba-test-1.0.742/signed/DuneCity-1.0.742-macOS.dmg
Download: http://claw.local:18738/DuneCity-1.0.742-macOS.dmg
Downloaded SHA256: 0836b75ad48bb576d6dcd1171cf40adac4d75d911c42644d2982a225117461e4.
Evidence: ../outputs/mba-test-1.0.742/verification.json and hot-join/.

## 2026-09-21 — Spectator transfer compression (isolated development branch)

Branch `improve/snapshot-transfer-compression` starts at main `5848c51`. It is local,
unmerged, and unreleased; version metadata remains 1.0.737 pending release coordination.
The separate worktree is `~/Documents/projects/dunecity-transfer-compression`.

Spectator checkpoints now negotiate bounded, lossless zlib compression. Older hosts and
viewers keep using the existing raw format. Level-9 ten-minute campaign samples fall from
702–715 KB to 54–64 KB. See [the protocol and test guide](docs/spectator-transfer.md).

Verification in this worktree:
- Native build/dependency audit and all eight CTest groups pass (777 main test cases pass,
  three existing skips). Native ASan/UBSan and wasm32 wire harnesses each pass 92 checks.
  This Mac's standalone sanitizer harness needs `DYLD_LIBRARY_PATH=/opt/homebrew/lib`
  so SDL2 can locate SDL3; the normal app and CTest targets already work.
- Full pinned Emscripten build, generated-JS checks and packaged-mod validation pass.
- Real local WebRTC spectating, old-737 host/new viewer, new host/old-737 viewer, and
  declined/retried/approved spectator promotion all reach matching cycle-1800 state.
- The 256x256 Twin Cities probe transfers 5,975,340 raw bytes as 88,012 bytes and reaches
  matching cycle-1800 state with 217 objects. Player progress and viewer departure pass.
- Logs and diagnostic executables are under `build/transfer-*`. The campaign-byte
  benchmark is in `build/transfer-benchmark/results.txt`. Browser interactive crossplay
  and Windows runtime testing were not repeated for this branch.

## 2026-09-21 — Fast approved campaigns and lobby-only new mods, local 1.0.741

Approved shipped mods (vanilla, dunecity, Tornie, Dune2R) now start campaigns
without Workshop capture, publication, queueing or download. Admission/readiness
use a self-identifying token from installed rule checksums; app/protocol and
runtime configuration checks remain. The main Campaign route defaults online and
starts automatically. Join Online -> Create Campaign retains the pregame roster.
Authored copies do not inherit approval from BaseMod: they require the lobby and
cannot hot join. Both admission and running-game discovery enforce the policy.
The setup and lobby describe this restriction. Automatically generated approved
snapshots selected by 738-740 migrate back to the shipped mod; authored IDs and
saved game files are preserved. The complete 739 editor palette is retained.

All eight native CTest groups pass. Expanded real-menu tests cover all approved
mods with the content endpoint disabled, zero new snapshots on campaign startup,
local hot-join activation, mismatched rules, custom-mod restrictions, old snapshot
migration and both campaign entry routes at three sizes. A native two-peer Dune
City spectator test continues at cycle 1800 with identical simulation digests.
Real Chromium testing against the isolated local service reached gameplay within
6.108 seconds of clicking Start Campaign. Both clients had /v1/content/* blocked;
startup, hot joining and spectator-to-player promotion made zero content requests.
The second browser player joined the live campaign, requested play, was accepted
into shared control and both clients resumed. A separate Play Online -> Create
Campaign test stayed in the editable pregame lobby until cancelled. All test tabs
were closed and temporary network blocking removed. These are local checks, not
a cross-network/MBA execution claim.

Native/browser source: 6c38946a. The signed and notarized Mac candidate is under
../outputs/mba-test-1.0.741/final-signed. Local browser test files are in play741;
use relay=http://127.0.0.1:18738&relaydev=1 for the isolated service. No game push,
tag or public release. The authorized 740 service deployment remains compatible;
no additional production server change is needed for this policy. Package and
live-flow evidence is recorded in ../outputs/mba-test-1.0.741/verification.json.

Download: http://claw.local:18738/DuneCity-1.0.741-macOS.dmg
Verified downloaded SHA256: e3f0b61753cd5cd5d5c1cb42fc247992fe3d0fdb16f736b99f5f6972d4957d5c.

## 2026-09-21 — Online direct Campaign and explicit online lobby, local 1.0.740

Corrects 739's offline default: Campaign starts online/public by default, then
starts automatically through the normal acknowledged network handshake. Join
Online -> Create Campaign retains the editable pregame lobby and Start button.
Connectivity and entry route are separate choices. Direct campaigns keep their
selected AI partner; the second controller can remain open for a later join.
During connection/start the direct route displays progress instead of a roster.
The complete city editor palette from 739 remains included.

Also fixes Apache ingress: the packaged .htaccess omitted all content routes and
admission/inspect, and its request limit rejected large Workshop manifests. The
route allowlist and 524288-byte outer bound now match PHP; smaller signaling
limits remain enforced by PHP. A regression checks every declared PHP route and
the content bound. Public content/list returned Apache 404 during investigation.
This service correction must be deployed before standalone public online tests.

Native and browser builds pass. All eight CTest groups pass, including real menus
at three sizes, both entry defaults, automatic-launch progress and retained AI.
Real Chromium checks against the isolated local service confirm Campaign ->
Start Campaign reaches live Dune City gameplay without a roster or second click;
Play Online -> Create Campaign -> Create Lobby remains in its pregame roster.
All 207 PHP tests and the verified service installer pass. Legacy claim tests
pass 40 admission/grant/signaling/start cases for 1.0.737/protocol9 and another
40 for protocol8. These are HTTP compatibility tests, not old executable runs.

The signed/notarized DMG and ZIP are under ../outputs/mba-test-1.0.740/signed.
The DMG is available at http://claw.local:18738/DuneCity-1.0.740-macOS.dmg;
its downloaded SHA256 matches e5bd5656a9b86e5db50339d9bc8e373d3d4b509d7bb6b19efc1cb769049317dd.
Code/package source is 8b352f03; browser test files live in play740 with explicit
relay=http://127.0.0.1:18738&relaydev=1 parameters for the local service.
The user authorized the public server update on 21 September. Website PR14
(https://github.com/VR48/dunelegacy.com/pull/14) merged as a73ad507 and deployed
successfully in run35543136731. Installed service release c340772c matches the
verified 740 snapshot; existing public browser remains 1.0.737. All 207 service
checks and 80 legacy protocol checks were repeated successfully before deploy.
Live health, protocol9 directory and content listing pass; foreign origins and
unknown/private paths remain rejected. A real Dune City mod upload passed the
142738-byte Apache request (larger than the former bound), uploaded 290 unique
files/10835830 bytes, committed revision ddcd9b38149235cfbaa455ea8dc43ec9626d965777505777a78b3b3d5fd70617,
and read back the identical manifest and largest blob with verified SHA256.
No live rooms or Discord test posts were created. Public online Workshop use is
now enabled for the standalone candidate. Evidence: ../outputs/mba-test-1.0.740/
deployment.json, live-endpoint-checks.json and live-service-verification.json.
Website worktree: ../dunelegacy-workshop-service, clean branch fix/workshop-service-740.
Prepared service bundle: ../outputs/mba-test-1.0.740/p2p-service.

## 2026-09-20 — Complete city editor and direct solo campaigns, local 1.0.739

Added Police Station, Stadium and Airport to the city-capable map editor palette,
including RGBA icons for every house, correct placed-map graphics, and scrolling
rows. Existing R/C/I zones, roads and nuclear plants remain available. City-only
rows remain hidden for vanilla; gameplay footprints and map formats are unchanged.

The main Campaign and Single Player entries now default to offline and launch
through startSinglePlayerGame without a lobby or Workshop upload. Play Online ->
Create Campaign explicitly retains online hosting. Offline setup hides public/
private visibility and explains direct launch. This removes the community-service
failure from the solo campaign path; it does not deploy the public Workshop API.

Native build and dependency audit pass. All eight CTest groups pass, with the
expanded real-menu probe checked at 640/854/1280 widths. It selects all eight city
tools through the scroll view, places them, checks undo/redo and saves/reloads the
map with exact item identities. Campaign controls verify both entry defaults.
A real Dune City level-1 native campaign ran 3,750 cycles (one simulated minute).
The local MBA test package is signed/notarized separately; no push or public release.

## 2026-09-20 — Workshop integrated with released main, local 1.0.738

Fresh origin/main and the published v1.0.737 tag both resolved to 5848c510;
there were no unreleased commits on main at inspection. This local candidate
merges all those changes with Workshop, retaining public/solo campaigns,
browser P2P/matchmaking, graphics skins, menus and current hot-join behavior.

Broad tests found and fixed the transport API integration, browser publication
ordering and missing-mod resolution, first received-map directory creation,
client/server manifest validation, Android-compatible staged file copies, and
the stale gravel/missing Sand optional catalog. Native and browser builds pass,
as do all eight CTest groups (784 main cases, three skipped), 206 PHP tests,
42 Python checks, browser JS suites and wasm wire/lifecycle sanitizer probes.
Five real native multiplayer checkpoint scenarios pass. Two real Chromium
clients play successfully, including a missing 11 MiB mod resolved through the
community service with exact byte verification and zero dropped commands.
Received map bytes and numbered metadata also survive a full browser reload.
See docs/workshop-validation-738.md for detailed scope, evidence and limits.

The local app is 1.0.738. Nothing was pushed, tagged, released or deployed.
The new community API still requires deployment. Windows/Linux/Android candidate
execution and cross-device/NAT play remain separate checks.

## 2026-09-20 — Workshop and exact content revisions, local 1.0.736

Workshop now contains Map Editor, Mod Editor, Asset Editors and Community Maps &
Mods; Extras contains replay/help/credits. Mod selection remains in game setup.
The real mod editor stages typed rule/stat/AI and metadata edits. Full mod copies
retain assets/campaigns/house metadata; Dune2R render settings are now authored
per mod. Map saves capture numbered immutable revisions with exact mod dependencies.

Workshop storage uses SHA-256 manifests and full verified snapshots. The PHP
signaling service has standalone content list/upload/download routes, atomic
version allocation, owner capabilities, chunk verification and quotas. Online
hosting publishes before admission; LAN captures and queues publication while
retaining bounded peer transfer. MOD4 settings pin original map/mod revisions;
protocol10 and save9840 retain older save parsing. Engine capabilities survive
hash-specific installed folder names. See docs/workshop.md for storage and flows.

This is a local source/build change, not a public service deployment or release.
The production PHP service needs deployment before the new community API works
there. Claude CLI was unavailable (not logged in); Hermes supplied a protocol
review. Available coding agents handled UI, service, and network integration.
Validation: native Release app rebuilt as 1.0.736, version consistency and Ninja
header-dependency audit passed. All eight CTest groups pass, including the real
menu probe at 640/854/1280 widths, new immutable-storage tests, MOD4/legacy wire
checks, and offline LAN import/tamper checks. Workshop/Extras/editor/community
screens were visually inspected. PHP content 12 and signaling 167 tests pass;
service-agent late-join 19/activity 7 tests also passed. The real C++ bounded-HTTP
client/PHP integration passes interrupted resume, duplicate large assets,
dependency closure, idempotent versioning, cache repair and ownership rejection.
Browser/Windows/Android device builds and live two-device play were not run in
this turn. Public deployment, push, tag and release remain separate actions.
## 2026-09-20 — Stop unpublished 737 for complete skin packaging

The stable artifact audit found that the browser preload omitted the DuneCity
skin tree. A fresh native profile also failed to install those files: the bundle
lookup required mod.ini, whereas DuneCity's package contains presentation assets
only and generates its configuration from engine defaults. The initial 737 build
was cancelled before any GitHub release was published; 735 remains untouched.

Recognize the DuneCity graphics-only bundle, repair profiles missing the skin
folder, and preload the same files in browser builds. The browser payload checker
now compares every bundled skin file with tagged source and rejects missing or
corrupt art. The real menu probe checks that a fresh profile installs a skin
manifest. The previous native build fails that assertion, and the previous stable
browser artifact fails the new payload audit. After validation and main merge,
replace only the unpublished 737 tag and run the gated release again.

## 2026-09-20 — Final 737 acceptance

The complete 737 raw browser build passes public matchmaking and gameplay again
following the CMake runtime fix. Native and browser builds, all eight CTest groups,
29 browser glue tests, four build safety checks, generated-JS and bundled-mod
verification pass. Incremental CMake plus wrapper packaging is byte-identical.
The solo-host spectator promotion test passes at cycle 1800 with matching state,
including a declined request, retry and shared-control admission. See the PR49
validation document for evidence and the same-network ICE test limitation.

## 2026-09-20 — Complete browser runtime on every build path

A raw CMake rebuild linked successfully but omitted the P2PKit runtime, making
public pairing fail with RTCTransport unavailable. CMake now prepends the pinned
runtime after every link. The standalone web publisher now installs the pinned
SDK and uses the same verified build script as game CI. The old optional wrapper
prepend remains idempotent. This fixes a separate production publication path
that the main CI artifact build alone did not exercise.

The solo-host spectator promotion probe initially expected obsolete anonymous
approval text. Updated its assertions to the current named notices; the real
host and newcomer then resumed with matching state at cycle 1800.

## 2026-09-20 — Final combined release advances to 1.0.737

The separately completed public-campaign defaults and privacy explanation are
now merged too (a50938a). Release 1.0.737 from main includes these local commits
alongside the complete 736 candidate and published 735. The 736 candidate was
never published. Website PR12 is merged and deployed on website main.

## 2026-09-20 — Campaign privacy explanation (local 1.0.737)

Added a short note directly above the campaign connection/visibility controls:
"Public by default: others can watch or ask to join. Choose Private for invite-only
play, or Offline." The title sits alongside the note without moving the controls.
Native app rebuilt; dependency audits, version consistency and the menu navigation
probe pass at all three sizes. Visually checked the 640x480 rendering. No push or
public release performed.

## 2026-09-20 — Public campaign by default (local 1.0.736)

Campaign entry now defaults to Online co-op with the existing Public visibility
setting. Players can select Offline before starting, or Private for invitations.
The co-op lobby allows a human host to start with the partner slot open or closed,
without requiring a second human or an AI. Existing late-join shared-control slots
and CampaignCoop progression carry this through the full level 1–9 campaign.
Menu guidance explains starting solo and later spectators/request-to-play joins.

Native app rebuilt at build/bin/dunecity.app. Dependency audits and version check
pass; all eight CTest suites pass. Menu regressions cover the online/public default,
switching offline, and solo lobby readiness/roster at 640, 854 and 1280 widths.
The 640-pixel campaign and solo-lobby screenshots were visually checked. Live
native/browser joins and a full nine-level playthrough were not rerun for this
change. No public release or push performed.

## 2026-09-20 — Combined main release candidate 1.0.736

Released 1.0.735 (f836940) already contains the graphics skins and campaign menu
changes. Preserve that published tag and all its assets. The combined 1.0.736
candidate includes that exact main history plus PR49 browser matchmaking,
PR28 direct-P2P command pacing, PR62 Windows dependency caching, and PR45's
screenshot fix intent while retaining main's stronger physical-target bounds.
Every origin branch tip inventoried below is now an ancestor of this candidate;
see `docs/branch-consolidation-736.md` for the superseded historical snapshots.
Merge this PR into main before creating v1.0.736; remove incorporated branches
only after checking their current tips against the merged main.

Native and pinned Emscripten builds pass. All eight CTest groups pass (772 main
suite cases passed, three skipped), as do the wasm32 ASan lifecycle harness and
real three-peer hot-join replacement test (matching state at cycle 150).
Two Chromium profiles using the candidate 736 game and actual production
`wss://dunelegacy.com/` pass Find Match, cancellation/retry, lobby/start, two-way
chat, movement, sustained command exchange and guest exit. No reported packet
drops, browser errors or long menu sleeps. Both browsers share one network;
selected ICE paths are host/UDP, so this is not a different-NAT traversal test.

The separate production matcher is installed from website commit a2ed864 under
`/opt/dunecity-matchmaking`, with a hardened systemd service and Apache TLS proxy.
The existing room service remains healthy. Restart/recovery and root/dedicated
WebSocket routes are checked separately. Website source PR12 must also land on
its main branch. Public game assets remain 735 until the 736 release pipeline.

## 2026-09-20 — PR 49 refreshed onto main 1.0.734

Main advanced to c505255 while the original PR validation run was completing.
Integrated its hot-join checkpoint recovery and named join UI changes, preserving
the browser transport and lifecycle fixes. The combined build is 1.0.735.
Native and browser builds, all eight CTest groups, 193 signaling tests and the
real three-peer hot-join replacement regression pass on the combined tree.
The wasm32 ASan harness and real two-browser pairing/play/quit test pass again.

## 2026-09-20 — PR 49 browser multiplayer review, 1.0.734

Integrated current `main` (8057d80) into the matchmaking branch. Browser peer
cleanup now removes aliases before deletion and defers local rejection cleanup
until packet handling returns. Fixed the rejection log varargs mismatch,
restored incoming-byte limits, guarded room-session status queries, and reset
match state on retry. Browser menu pacing is capped at 50 ms: the real exit
test caught a 484-second sleep after a nested match returned. Cancel releases paired connections, map-selection Back
leaves the pair, and the matchmaking screen now renders its Back button.
Preserved main's updater, observer/late-join behavior and direct-RTC diagnostics.

Native and Emscripten builds pass; all eight CTest groups pass (770 main-suite
cases passed, three skipped). The original teardown fails the new wasm32 ASan
harness with heap-use-after-free; fixed lifecycle cases pass. Browser glue has
29 passing tests; direct transport/bridge has 18. Real Chromium clients paired,
entered the same map, and exchanged thousands of command packets without drops.
See `docs/browser-multiplayer-pr49-validation.md` for evidence and limits.

The production matchmaking WebSocket service must be deployed separately;
configuration, trust model, STUN defaults and fixture provenance are documented
in `platform/web/README.md`. This review does not publish or merge the game.

## 2026-09-20 — Graphics skins integrated with current main, 1.0.735

PR53 integrates canonical main 24eed049, including PR63 campaign dropdowns. Preserve the new
selectable skins and high-detail Compacts together with current hot-join,
campaign controls, preview bounds and platform build support. Desktop metadata
is 1.0.735; Android retains the PR's independent 0.2.26 / 1000546 metadata.
Stefan requested the v1.0.735 release tag after integration. PR63 created that
tag concurrently; its unpublished build 35489068976 was cancelled so the final
735 tag can include both feature sets. No published release assets were replaced.

Integration fixes: the wire parser accepts ChangeGraphicsSkin; the lobby applies
the same own-house authorization as team/color changes; readSaveSetup restores
MOD3 skins by house identity; co-op save configuration retains campaign skin.
Compact lobby rows make room for the skin selector beside the map preview.

Validation: clean native Release build and dependency audits, all seven non-UI
CTest groups, four skin-packaging tests (Python 3.14/Pillow), and the menu probe
at 640/854/1280 widths pass. Regression coverage includes skin wire round trips,
unauthorized lobby changes, co-op/save skin retention, and real lobby/settings
widgets. This is local native validation; release CI and public publication are
tracked separately. Release notes: releases/desktop/1.0.735.md.

## 2026-09-18 — High-detail DuneCity Compact source contract

Classic Dune II object sheets are indexed 8-bit art and use DuneLegacy's real
tiled Scale2x/Scale3x path. DuneCity skin PNGs are RGBA; the compatibility atlas
path fits those images into native SimCity cells and therefore cannot retain
extra source detail. Zone manifests already separate source-frame dimensions
from the immutable logical footprint. The DuneCity packager now preserves a
declared 1x-4x Compact frame and records its logical 16px/tile footprint
separately, so a 64x64 source can still occupy a 2x2 / 32x32 world rectangle at
base zoom. R/C/I placement, collision, and simulation remain 2x2.

Special DuneCity buildings now have a direct manifest-frame draw path. It draws
the selected high-detail frame into the classic destination rectangle already
calculated by `StructureBase`, preserving exact on-map size, anchor, engine
animation frame selection, and fog fallback while bypassing the lossy native
atlas round trip. Native SimCity compatibility atlases and UI portraits remain
available and fixed to their existing slots. The bot-side `Compact All` modal
selects 1x-4x (16-64 source pixels per tile) and current-unit/all-DuneCity scope.

Successful Compact All batches now feed a repeatable local play-test pipeline.
`sync-dunecity-skins.py` discovers Oathkeeper manifests, transactionally replaces
supported zone/building packages, and preserves authored icons.
`deploy-dunecity-skin-test.ps1` then waits for the Windows and Android builds and
installs the APK when ADB has an authorized device. Oathkeeper streams stage
progress into its config panel and distinguishes Compact failures from later
packaging/build failures. The same wrapper is the manual recovery path; no
hand-copied per-unit filename list is required.

## 2026-09-17 — Selectable DuneCity graphics skins on canonical main

Ported the presentation-only SimCity/Dune2 skin system onto a clean worktree of
`ggtothemax/dunecity` main at `52fe7aca`; the older QBot simulator was used only
as the source of previously tested visual-integration changes. Campaign options
and custom/multiplayer house slots now select and serialize skins independently.
The Dune2 path mounts all currently authored zone and special-building Compacts,
derives matching construction/detail portraits, preserves native SimCity art as
the per-cell fallback, and suppresses the native R/C/I colour overlay only for
Dune2-skinned houses. Complete-cell RGBA replacement prevents SimCity art from
bleeding through transparent pixels. Skin-only alpha-aware, cell-isolated
Scale2x/Scale3x leaves the classic indexed-palette scaler unchanged.

The Android debug package uses DuneCity payload 1.0.707 and includes the full
graphics-skin tree. This is a local playable integration build, not a release or
version bump. Installed and exercised on the connected Armor 21: Dune2 zone
Compacts replace (rather than alpha-overlay) native cells, the first eight
residential occupants advance through the authored growth stages, and no native
green/blue R/C/I background remains. Construction-list and selected-building
portraits resolve against the active owning house and derive directly from the
accepted Compact named by `zone.ini`; the native 15x8 SimCity atlas layout is
not used to crop a 4x4 authored skin. Portrait creation also does not require a
per-house native atlas surface: non-Harkonnen houses normally palette-map the
single native base atlas, while their Dune2 portraits must load their own
manifest Compacts independently. A future authored `icon.png` remains the
highest-priority override. Nothing was pushed or deployed.

Skirmish and multiplayer use the same per-house path: the lobby's Skin dropdown
emits `ChangeGraphicsSkin`, hosts mirror it to the matching slot, MOD3 settings
serialize one value per `HouseInfo`, and match initialization applies those
values before construction/detail portraits are requested. Campaign applies its
single selection to every participating house. Live Android verification covers
the three campaign houses; the custom/network serialization path was inspected
but has not yet been exercised with a second connected client.
## 2026-09-20 — Campaign dropdown visibility (local 1.0.735)

Fixed dropdown placement to respect the menu's SDL clip rectangle and include
the closed control's height when checking available space. Campaign mod and AI
lists now open upward when needed; oversized lists fit whole rows and use the
existing scrollbar and mouse wheel. The selected row stays visible on resizing.

The menu regression reproduced the old clipping failure, then passed at
640x480, 854x480 and 1280x720. It checks all four lower campaign selectors and
scrolls a constrained 40-entry list to select its final row through the menu.
Rendered screenshots were visually checked. Native build, dependency audits,
version consistency and all eight CTest suites pass. Local build/bin/dunecity.app
is 1.0.735; the existing running process needs a restart. No push, tag or public
release performed.

## 2026-09-20 — Dune City 1.0.734 published and upgrade ready

PR61 merged as c505255c6fd23ef92f2ec91a60c5eeb8c4bde8b7; tag v1.0.734 points there.
Stable build 35485484374 passed and published all 13 assets (eight packages and
five signed feeds). Independently downloaded the release and verified all three
Ed25519 manifests, archive sizes/hashes and both appcast signatures. The exact
VR48 latest-feed URLs embedded in the Air's installed 733 app return valid 734
Mac feeds. The Air installation was left untouched for Stefan's upgrade test.

Both the Mac DMG and update ZIP pass strict signatures, stapler and Gatekeeper
as Notarized Developer ID. The DMG also passes portable-library, metadata and
isolated SDL rendering checks. Public DMG SHA256:
697c1ad848c69b49d062ab1b1d9736b11d1414cc6bca91c45952b470b7094409.

At Stefan's request the browser was deployed before the slow Windows release job
finished: website PR11, merge 4760d7e, deployment 35486509988. It uses the exact
stable run's Emscripten artifact. The later release-link deployment 35486713967
also passed. All seven live browser artifacts match the 734 tagged manifest;
both download pages and their public package URLs pass checks. A fresh Chrome
tab renders v1.0.734; signaling health is status=ok. Redundant browser workflow
35486716168 was cancelled after this verified publication.

SourceForge run 35486716196 passed: nine uploaded files readback-hashed, released
source refs updated and Windows/macOS/Linux defaults confirmed. Existing local
733 browser test clients were preserved. Evidence is task work/release734-*.

Windows slowness was diagnosed from the PR build log: the removed x-gha backend
disabled binary caching; dependencies took 13m17s (OpenSSL 7.7 minutes) before
8m17s of compilation. A files-provider cache fix is committed separately in
PR62, worktree task work/windows-cache-fix. Its first CI run 35486310606 is still
running; do not claim warm-cache performance or that the fix has merged yet.
No paid runner or Windows-laptop setup was performed. Stefan offered a laptop;
we recommended measuring the cache repair first, then trusted self-hosted builds
if needed. The release and Mac upgrade are ready independently of that work.

## 2026-09-20 — Windows dependency-cache repair

The 734 PR run 35484459930 spent 13m17s configuring dependencies (OpenSSL alone
7.7 minutes), then 8m17s compiling. Its log explicitly warns that run-vcpkg's
`clear;x-gha,readwrite` selects a removed backend, disabling reusable binaries.
Windows now uses the same files-provider approach as Linux/Mac, persisted through
actions/cache. Cache keys include runner image and manifest; prefix restores keep
compatible packages across version bumps, with vcpkg performing ABI validation.
This infrastructure change does not alter the already-tagged 734 release. A cold
run must populate the cache before a later run can demonstrate the time saved.

## 2026-09-20 — Prominent in-game join names (local 734)

The map notice now names the first player requesting approval, with an additional
request count and all pending names in its tooltip. Requesters see their own name
while waiting. Approval and synchronization dialogs have a large gold player name;
the host also sees named spectator arrivals. After a controller checkpoint is
loaded and the new simulation begins, every controller sees a named "joined to
play" notice for 12 seconds. The name survives checkpoint handoff; transfer alone
does not announce successful admission.

Version bumped to 1.0.734. Native dependency audits/build, the browser build and all
eight CTest suites pass. The menu render suite passes again after enlarging the name region; screenshots
confirm both ordinary names and 64-character names fit at 640/854/1280 widths.
Build/test logs are in the existing task work/hotjoin-733 directory as join-names-*.
The running 733 browser match was preserved; these UI changes require the new client
build. No 734 installer, public upload, update-feed change, push or tag yet.

## 2026-09-20 — Named multiplayer Discord announcements (deployed)

The signaling notification hook now supplies human player and spectator rosters.
Initial start identifies the host and players present. Hot-join events identify the
participant by name and role: a spectator event follows committed session admission;
a player event follows the host successfully resuming the match after admission or
promotion. Cancelled controller joins, nonce retries and repeated phase requests
do not announce success. Transport credentials remain excluded.

The companion website change is on `fix/multiplayer-roster-notifications` in
`../dunelegacy.com`: named Discord embeds, separate spectator/player events,
participant/role deduplication, literal Markdown display and bounded roster fields.
All 193 signaling tests and website notifier tests pass. Server-only change; no
client rebuild/version bump required. At Stefan's request, website PR10 was merged
as 9cf16b1d and deployment run 35483230025 passed. Live health returned status=ok;
live notifier and signaling SHA-256 hashes match the tested local sources. Game
source commit fe29d281 is packaged by that deployment; game release 733 remains
unpublished. A fresh public Twin Cities lobby hosted by Codex Web 733 is waiting
for Stefan to join before start, to check named start and hot-join Discord messages.
The previous browser match had already returned to the online menu. Stefan confirmed the live start announcement names Codex Web 733 and ggtothemax.
A second Chrome client, Codex Web Guest (tab 1889837000), hot-joined that running
match as a spectator, loaded the map, requested play, and was approved to share
Codex Web 733. It resumed without the Spectating label and continued simulating.
Both browser tabs are retained; do not close either while this match is running.
Delivery of the two hot-join Discord announcements still awaits user confirmation.

Stefan confirmed the local 733 web-host/native-Air hot join and approved shared
control work. This is additional user acceptance evidence for the next section.

## 2026-09-20 — Hot-join restoration, progress and recovery (unreleased 733)

Branch `fix/hot-join-progress-733`. The Air's installed 732 log was preserved before
any restart: at cycle 42200 its RNG and object digest matched the host but its house
digest differed. The live host is Brave on the Air; its exact saved checkpoint and
host log were not retrieved. Stefan does not want console/export/manual diagnostic
steps. Local real-peer reproductions provided the required evidence instead.

Fixed unit registration on checkpoint load: temporarily deviated units count toward
their original house, and loading does not add military value already restored from
the save a second time. A converted-unit test reproduced the house mismatch before
the change and passes afterward. A populated four-corner match additionally exposed
lost harvester path-failure counters and sandworm attack modes being overwritten on
load. Runtime supplement version 3 preserves the counters; loaded worms retain their
saved mode. Ordinary save layout remains unchanged.

Added actual byte progress to the joining lobby/controller dialog, then a map banner
for catch-up progress. Host-frontier packets and a 64-tick replay window let viewers
catch up in bounded batches. Fingerprint mismatches request a fresh checkpoint, with
distinct snapshot epochs and at most two restarts per connection. Transfer expiry
also uses that bounded recovery. Permanent failure returns the viewer to a usable
lobby with a message; the host keeps playing. Both sides require 733, enforced by the
existing exact-version gate. See docs/late-join-protocol.md.

Validation: native dependency audits/build and all eight CTest suites pass; browser
build passes. Real-peer regressions pass for deviated ownership with 300 ms snapshot
polling, transient mismatch recovery, permanent mismatch with two retries and host
continuation, and three-peer approved shared control retaining the AI. The populated
four-corner regression joins at cycle 42100, stalls the viewer for five seconds and
matches state at 45000. Native screenshots verify download/catch-up bars, including
640/854/1280 lobby layouts. An actual Chrome 733 client joined a native host checkpoint
at cycle 4212 (2,203,318 bytes), rendered the map and stayed connected beyond host cycle
10531; this is browser-as-viewer evidence, not the user's Brave-as-host reproduction.

Signed/notarized local Mac ZIP and DMG are in task work/hotjoin-733/signed-verified.
The DMG passes stapler, Gatekeeper, strict signature, portable-library and actual SDL
runtime rendering checks on the mini. It is copied to the Air's Desktop as
DuneCity-1.0.733-macOS.dmg; its hash, signature, Gatekeeper and isolated dummy-video
runtime probe pass there too. Existing installed apps, profiles and running games
were not changed. DMG SHA256:
e2be6b993d41fbc8aa1e9544dcb346cdecf9c0e9fb584af8020e9eb559585172.

Evidence directory: /Users/stefan/Documents/Codex/2026-09-19/i-h/work/hotjoin-733.
Includes preserved Air log/replay, before/after checkpoints, regression logs, UI PNGs,
browser observations, Apple acceptance JSON and package verification logs. An initial
signing process was waiting on the locked keychain; stopped only that process,
unlocked the existing signing/notarization keychains using protected saved credentials
and rebuilt in signed-verified. No new credential or user setup is needed.

733 has not been pushed, tagged, or published to the website/update feeds/installers.
Public release remains 732. Do not ask Stefan for manual console diagnostics or
another password setup; prepare any next release through the existing release process.

## 2026-09-20 — Dune City 1.0.732 published and verified

Release source/tag: a319a104b83d9b53ce31ffcf7ee7d171cc5bad2e / v1.0.732.
Stable build 35474893099 succeeded on attempt 3 and published 13 assets: eight
packages and five signed update-feed files. Independently downloaded the public
release and verified all three Ed25519 manifests, both appcast signatures and
actual archive sizes/hashes. The game's existing VR48 latest/download URLs
redirect to the same verified public feeds.

The published Mac DMG and update ZIP both pass stapler and Gatekeeper as
Notarized Developer ID. The mounted DMG passes strict signature, portable-library,
bundle metadata and actual SDL hidden-window rendering checks. CI's vcpkg build
uses static SDL. This verifies the public CI artifacts, not just the earlier
Homebrew package tested on the Air.

- Published DMG SHA256: e145a4c70bf74d9c61e5dba98bca1edcacdc6f43644fa50fdc10692da6dac6ab
- Published Mac ZIP SHA256: 7a19cffd790bf2e72d6efc9cd21955ac68bd3b346b9731d4ba4a76a84b691455
- SourceForge workflow 35477564826 verified all nine files, published the tagged
  source, advanced its dunecity branch and confirmed EXE/DMG/AppImage defaults.
- Website PR9 merged as 3aa679cf7ffb30cfa24fd83ec9ba26907e6f675c. Deployment
  35477688189 passed. Browser assets reuse the stable run's exact Emscripten
  artifact and identify a319a104 in build.json; redundant rebuild 35477564808
  was cancelled before publication.
- Independently hashed all seven live browser artifacts, checked both download
  pages and every desktop download link, and verified public signaling health.
  A fresh Chrome tab rendered the main menu with v1.0.732 visible.

Apple setup is complete. Both DuneCity-Signing and the separate
DuneCity-Notarization keychain profiles independently authenticate. The revised
interactive setup does not save plaintext Apple passwords; future signing-key
repairs must preserve the separate notarization keychain and its protected local
password. No more user credential setup is pending.

Existing native/host games were not interrupted. Earlier game versions require
one manual install to gain the updater. Android stays on its independent 0.2.25
release. Actual old-to-new installation/relaunch tests on every supported OS
remain separate validation; this release verification does not claim those tests.

## 2026-09-20 — Release 732 publication and signing setup recovery

PR59 is merged at a319a104b83d9b53ce31ffcf7ee7d171cc5bad2e and v1.0.732 points
at that source. Stable run 35474893099 passed Linux/Windows/browser and test jobs;
Mac signing failed in attempts 1 and 2. No stable 732 assets are published yet.
Website PR9 contains matching browser artifacts and download copy, has passed
checks and awaits desktop publication before merge.

The dedicated signing keychain was absent from the user search list. A successful
GUI signing probe used a duplicate login identity and did not establish runner
access. The saved dedicated-keychain password also failed once the keychain was
locked. Preserved the original keychain and saved password in the protected
signing directory, restored the identity from its encrypted P12, and verified an
explicit lock/unlock cycle. Prepending the dedicated keychain while preserving
existing search entries makes a SessionCreate=true LaunchAgent signing probe
pass. No runner session setting was changed.

The original notarization credential remains in the preserved, locked keychain.
User re-entry initially received Apple's HTTP 401. Verified Chrome's Apple
account is icloudlogin@fastmail.com and the regenerated DuneCity Mac mini entry
exists. Added setup-macos-notarization.py to check paste formatting, use a hidden
TTY instead of password process arguments, and retain a separate encrypted
notarization profile after successful authentication. Its local prompt transport,
redaction, input validation and timeout cleanup pass with fake credentials.
The user completed the revised setup; both stored profiles pass independent
notarytool history authentication. Stable run attempt 3 is now rebuilding Mac.
Publication remains pending that run. See the runner runbook.

## 2026-09-19 — Fix packaged SDL3 startup failure (unreleased 732)

Stefan's 731 installer failed at startup with "Failed loading SDL3 library."
Homebrew's SDL2 target resolves to sdl2-compat, which dlopens libSDL3.dylib.
BundleUtilities sees linked libraries only, so 731 omitted SDL3 even though its
signatures, notarization and static dependency checks passed. Do not recommend
the 731 package. The previous validation was insufficient to establish launch.

Mac install rules now detect the compatibility library, require SDL3 and its
license, copy it beside SDL2 as libSDL3.dylib and include it in dependency fixup
and signing. Added --check-desktop-runtime before profile/game initialization:
it initializes SDL video/timers, renders a hidden window and reports the loaded
SDL paths. scripts/verify-macos-runtime.py rejects missing SDL3, external SDL
libraries and startup/render failures. The signing pipeline checks the hardened
runtime app before Apple submission; the DMG verifier runs the same check from
the mounted package. The new verifier correctly rejects the broken 731 package.
Also corrected the DMG dependency parser to ignore universal-binary headers.

Native dependency audits/build, all eight CTest suites and Emscripten build pass.
The installed, Developer ID signed and final mounted packages pass runtime checks.
Launched the final ZIP's app with an isolated profile; native screenshot shows
v1.0.732 and the rendered main menu/first-run welcome dialog. This establishes
launch and rendering, not an end-to-end gameplay or updater test. The temporary
test process was stopped afterward. Existing installs/profiles were not replaced.

Apple accepted app 5800889c-1175-4955-bb70-9e31bd123429 and DMG
a4c2b5de-c8e9-49fb-be76-b2b335633eb7; both stapled and Gatekeeper accepted.
Task work/notarization-732 contains the final ZIP, DMG and acceptance evidence.
DMG SHA256: cab6635ffebb4c88b461e84e6a32449a824b16a04eb309f0697930649319cc9b
ZIP SHA256: 15ef90d5e8f418763b7454247b58924a360a2b8cc9beabd2ff52ae7d8c7532db
The identical DMG is on BOTH Macs at Desktop/DuneCity-1.0.732-macOS.dmg.
On the Air (macOS 26.5.2), SHA256, stapler, Gatekeeper, strict deep app signature
and the packaged SDL initialization/render probe pass (dummy video/audio driver
for the remote probe; no user game/profile opened). All four SDL libraries,
including SDL3, load from the mounted app. No push, tag or publication occurred.
Real old-to-new updater tests remain separate work.

## 2026-09-19 — Updater edition 731 signed and notarized on the mini

Stefan completed the mini credential shortcut. DuneCityNotarization in the
dedicated DuneCity-Signing keychain now authenticates successfully; no further
Mac credential setup is pending. Native dependency audits, version check and
incremental build pass; a fresh cmake install produced the portable bundle.

The first real packaging run exposed two script-only validation bugs, now fixed:
file(1) descriptions of bundled data can contain non-UTF-8 bytes, and otool emits
an absolute filename header for each slice of a universal binary. Detection now
checks the Mach-O marker as bytes and inspects only indented dependency lines.
The full signing/notarization run then succeeded without manual intervention.

Apple accepted the app submission 5ecdeea2-e3f3-41f5-a84e-11eb4cc30916 and the
DMG submission 1efa22f4-29fc-46a5-ad7f-5ab7d6c2bb7e. Both are stapled and pass
Gatekeeper as Notarized Developer ID. Extracting the final ZIP independently
passes strict deep signature verification, ticket validation and Gatekeeper.
The final DMG also passes scripts/verify-macos-dmg.sh. Output and submission
evidence live in this task's work/notarization-731 directory:

- DuneCity-1.0.731-macOS.dmg SHA256:
  9f56cf3bc7f8dde3d9bcb49fcbbd13206437ca50af167e81e06f108ee09fb002
- DuneCity-1.0.731-macOS.zip SHA256:
  e2ffa0d08d8b1868c36f2939c210bcab57f5b0f351916c88d04beefcc0047727

The local update-feeds subfolder contains the signed Mac manifest and appcast.
Both Ed25519 signatures and the final archive size/hash were independently
verified using only the committed public key. Their future release URL has not
been published. No push, release, application replacement or Air transfer took
place. Real old-to-new updater/relaunch tests on each OS remain outstanding;
successful package notarization does not establish those runtime results.

## 2026-09-19 — Cross-platform desktop updater (unreleased 731)

Implemented main-menu update checks, Install/Later confirmation, signed Ed25519
metadata and native Sparkle (Mac) / WinSparkle (Windows EXE) integration. Linux
AppImages verify the download and atomically replace the original with a retained
backup before relaunch. DEB/RPM users receive manual package-update guidance;
no package repository was provisioned. Updates do not run during a match or
replace the separate saves/settings directory. Browser/Android builds exclude
the updater. See docs/desktop-updates.md for behavior, trust and recovery limits.

Stable-release CI now prepares notarized Mac DMG/ZIP, Windows EXE/ZIP and signed
platform feeds; it uploads to a draft before publication. Existing published
releases cannot be overwritten. SourceForge adds the Windows EXE from 731 while
preserving older backfill layouts. No push, tag, CI run or publication occurred.
The initial updater edition still requires a manual install on each machine.

The encrypted update-signing key and its password are outside Git, under the
mini's protected Library/Application Support/DuneCity Signing/updates directory.
Only the public key is tracked. Back up those protected files securely.
Apple signing identity is already installed, but the mini's dedicated keychain
still lacks the DuneCityNotarization credential profile (verified this session).
Stefan has been asked to run Desktop/DuneCity-Automatic-Updates-Setup.command on
the mini. The Air's existing login-keychain profile does not configure the mini.
Never print key/password contents. The new 731 DMG is a local ad-hoc test build,
not a newly Developer ID signed/notarized release.

Validation: native build and dependency audits pass; all eight CTest suites pass.
After the final no-thread compilation guards, menu/security suites pass again
and the Emscripten build passes. Signed-feed tests and nine SourceForge tests
pass. The HTTPS AppImage fixture passes success, corruption, truncation,
tampered signature, downgrade, wrong platform and untrusted TLS cases. On this
Mac it exercises the production POSIX replacement path, not Linux runtime launch.
Windows adapters cross-compile with MinGW; a dummy CPack/NSIS fixture validates
installer syntax only. Native Mac bundle installation and DMG portability checks
pass; Sparkle initializes and reaches its unpublished-feed error UI. The rendered
640-pixel update confirmation was inspected. Evidence is under this task's work/
directory (updater-* logs/artifacts). Dummy Windows fixtures are not game builds.

Before public release: provision mini notarization credentials and validate the
new signed/notarized packages; perform real old-to-new upgrades on Mac, Windows
and Linux, including relaunch, retained user data and multiplayer compatibility.
Windows Authenticode is not provisioned. Older Mac compatibility remains untested:
local Homebrew dependencies produce newer minimum-OS warnings. The running 730
Chrome/native game was left untouched; no 731 build was transferred to the Air.

## 2026-09-19 — Developer ID signing and first notarization verified

Stefan renewed his individual Developer Program membership through September 20,
2027 (team 34X7AYJZ93). Developer ID Application certificate was matched to the
mini-generated CSR and installed. Certificate expiry is February 1, 2027 (G1),
separate from membership renewal. Encrypted key/archive material stays outside
git in the mini's protected Library/Application Support/DuneCity Signing folder;
the Air has only the public certificate. Never print passwords or key contents.
A dedicated DuneCity-Signing keychain also holds the identity for codesign.

After Stefan approved the mini's codesign prompt, the staged 1.0.730 bundle and
all 30 Mach-O files were Developer ID signed with hardened runtime and secure
timestamps. cmake --install bundled portable dylibs first; strict deep signature
verification passed on both Macs. The running game/build bundle was unchanged.

The signed ZIP is in session work/notarization-730 and on the Air under
Documents/projects/outputs/DuneCity-730-Notarization. SHA256:
8b06f0342bcc3a5745b7d8275dbb8b434d0170c854a659e416c003b95e5f409c.
Stefan ran the credential setup and submission shortcuts in the Air's local
Terminal using Keychain profile DuneCityNotarization. Apple accepted submission
561fcf03-1554-4322-a9d3-93eeeaeab542 on September 19, 2026 with no issues.
The Air login keychain remains locked over SSH; use the local Terminal shortcut
for future authenticated submissions. Retries reuse the recorded submission ID.

DuneCity-1.0.730-notarized.zip contains the stapled app. Ticket validation,
strict deep signature verification and Gatekeeper assessment passed on the Air,
and again on the mini after copying and extracting the final archive.
Gatekeeper reports source=Notarized Developer ID. Final ZIP SHA256:
32c3fe23378079ee1cb8c18db9a8af74735481b19b188bb422ddc3ffe48153f0.
The final ZIP and submission.json/status.json/apple-log.json are on both Macs in
the folders above. Mini extracted verification copy is under
session work/notarization-730/verified/dunecity.app. Local signing/notarization
setup is working; automated release signing remains separate work.
No game push, public release, CI secret upload or CI signing change occurred.

## 2026-09-19 — Visible play approvals and shared control (unreleased 730)

Stefan confirmed the live Air promotion worked. The host approved Replace
Harkonnen, which removed its AI; this was the selected controller slot, not a
failed shared join. Shared house / Multiple players per house already supports
joining alongside an existing human or AI (two controllers per house).

Code 894f6fe9 adds a persistent map-screen button on both host and requester while
a play request awaits approval. Its text pulses white/gold without hiding the
click target. The host clicks it to approve/decline; requests no longer force a
dialog over active play. The requester clicks for request options, sees declined
or failed status, and the notice clears after promotion or cancellation. The
approval dialog defaults to sharing when available and explicitly labels AI
replacement. No simulation, wire-format or service changes.

Validation: all seven CTest suites, native dependency audits/build, and a real
three-peer city promotion probe pass. The probe checks both notices, host click
entry, decline/retry, default sharing, retained AI, notice removal and equal state
at cycle 1800. Native rendered captures inspected in session
work/join-notice-promote/{host-pending,requester-pending,host-approval}.png.
The current live Chrome/Air match is intentionally left running on its existing
code; refreshed binaries take effect on next launch. No game push or release.
The pinned Emscripten build also passes. The Air fast-forwarded to 894f6fe9;
its native rebuild, before/after dependency audits and strict deep codesign
verification pass. Desktop/DuneCity-730-Test.app still points to that rebuilt
build-714 bundle. Both sides must relaunch/reload for the new UI; the current
match was not restarted.

## 2026-09-19 — Disconnect after map appears: spatial lookup drift (unreleased 730)

Stefan's Air loaded the map, then disconnected on the next spectator fingerprint.
The same failure reproduced in an automated native viewer of the Chrome host.
The extended native Twin Cities probe reproduced it at cycle 60200: matching RNG,
counts and house state, but launcher 980 chose a different target after loading.
The checkpoint's ordinary object bytes round-tripped exactly after preserving
queued-work timer sentinels; resetting those alone did not fix the disconnect.

The targeting spatial grid was stale for aircraft, carryall pickup adjustments
and infantry movement. These paths now update it like ordinary ground movement.
Loading excludes inactive cargo/repair-yard units that retain their old location.
Cell query results use stable object-ID order: reversing a populated checkpoint's
cell entries previously changed four units' target choices with no other changes.
Spectator loads retain negative target/path timer sentinels for restored queues.
No save or wire format changes; both endpoints need the matching simulation build.

Regression coverage: JOIN_FAST_WARMUP, JOIN_SEED and JOIN_CHECK_SPATIAL in the
real-peer fixture reproduce battles efficiently and check each active unit's grid
cell plus target-choice invariance under reversed cell insertion order. The check
caught carryall 13's pickup movement at cycle 127 before its correction. The final
seed 118705914 Twin Cities run joined at 60147, matched through 63000 (seed
1f8fb888, 699 objects, digest 8fbb2992f2d692a7/ebccff888fc2a4b8), then verified the
host continued after viewer departure. Three-peer city promotion matched cycle
1800 (seed 7292527d, 51 objects). All seven CTest suites, native dependency audits,
native build and pinned Emscripten build pass. Evidence: session work/aged-*;
the successful extended run is aged-all-grid, promotion is aged-fix-promotion.

The earlier live service update is already deployed (website PR8 / 01ebe6a).
This game fix is committed as f835d748 and has not been pushed or released. The
Air fast-forwarded to that commit and rebuilt locally; dependency audits, all
seven CTest suites and codesign verification pass. Desktop/DuneCity-730-Test.app
still points to its build-714 bundle. The fresh Chrome host is running in tab
1889836980, Codex Mini 730, Twin Cities, room FAQA-S97J-GHGA (temporary). The
user has been asked to open the Desktop test app, join, then Request to Play.
His own stable join and promotion remain unverified; automated promotion is not
confirmation of his session. No initial setup help is currently needed.

Live follow-up: the Air launched updated 730 (PID 26400), was admitted to the
fresh host and advanced past cycle 13800 with no spectator mismatch/disconnect
in its current log. A separate native viewer also followed this Chrome host
from cycle 7755 through 8974 without mismatch, then was deliberately stopped.
The host Options menu still showed Join requests (0); Stefan's Request to Play
has not arrived yet. Host was left running, with the temporary console hook
removed. The restart-resistant Air capture remains in session
work/air-live-monitor.log for the next live attempt.

## 2026-09-19 — Live Request to Play needs the matching service update

Stefan's Air successfully spectates but clicking Request to Play closes its menu
without a request appearing on the Chrome host. The live service still has 729's
action allowlist. A read-only play_status probe using the host's own session
returned HTTP 400, bad_request, "The 'action' field is missing or not valid."
The host's normal queue polling returns HTTP 200 with an empty queue. No host
approval occurred; do not claim the player was promoted or ask him to keep retrying.

The website checkout /Users/stefan/Documents/projects/dunelegacy.com now has local
branch fix/spectator-play-requests-730 with the candidate service packaged from
game commit 2697bf1f. Only p2p-service/{SOURCE.json,public/index.php,src/LateJoin.php}
change. All 190 real-HTTP service tests, the atomic installer/config-preservation
test and website security/artifact checks pass.

Stefan explicitly approved deployment. Website PR8 merged as 01ebe6a; production
deployment 35432271746 succeeded and installed manifest
c83e70232a0a96cb7a422fb3d7749ad99f96a699eb7ef19833d70bda808c0593. Live health is OK.
The host's authenticated play_status probe now returns the expected HTTP 403,
"Only a spectator can request to play," rather than rejecting the action syntax.
The host and Air continued running across deployment. A fresh Request to Play
click is still needed after the earlier rejection; then approve through the UI.
The live host is Chrome tab 1889836980; verify its current state before acting.
No client rebuild is needed. The website checkout is clean on main at 01ebe6a.

## 2026-09-19 — Slow spectator checkpoint transfer (unreleased 730)

The Air's native 730 joined the mini's Chrome 730 Twin Cities host but disconnected
while loading. A bounded temporary browser trace captured 4,816,896 acknowledged
bytes of a 6,144,474-byte snapshot before the host closed the stream. Individual
chunk acknowledgements often took 200–300 ms; stop-and-wait delivery exhausted
the 1,500-cycle catch-up history before loading finished. No manual pause is needed.

Snapshot sends now allow four 48 KiB chunks in flight. Cumulative acknowledgements
must advance within sent data and land at a chunk boundary or the exact end.
The global 64 KiB/update allowance, 8 MiB snapshot cap, bounded history and timeouts
are unchanged. The wire format remains compatible with the Air's existing 730.
Host logs record snapshot start/load and expired-transfer byte/cycle frontiers.

The real WebRTC probe accepts JOIN_SNAPSHOT_POLL_MS and JOIN_VERIFY_CYCLE. With
300 ms receiver polling and a 3,000-cycle target, the old ObserverStream object
reproduced a disconnect after about 40 seconds; the fixed peers matched at cycle
3,000 (seed 4d1cbac9, 225 objects, digest 858fbdebd8b01bf3/f1e24626c740fa65), then
the spectator left without stopping the host. Unit coverage rejects duplicate,
backward, unsent, unaligned and pre-header ACKs. Seven CTest suites, native build,
before/after dependency audits and the pinned browser build pass.

Evidence: session work/twin-slow-old-3000, twin-slow-fixed, snapshot-window-*.
The old 1,800-cycle probe target could stop the host just before history eviction;
the longer target is necessary for this regression. No release or push occurred.

Live retest: rebuilt Chrome host Codex Mini 730, Twin Cities, room
X3E3-EPHC-MHXG (temporary; do not assume it remains live). Stefan's existing Air
process successfully loaded the snapshot and continued city simulation, with a
reported direct RTT of 182 ms. Stefan confirmed "Yes, the map loaded." Its log is
retained in session work/air-window-fixed.log. The Air checkout also fast-forwarded
to 8df2eae6 and rebuilt locally; dependency audits, seven suites and bundle signature
verification passed. Its Desktop/DuneCity-730-Test.app shortcut still points to
the local build. Controller promotion in this live Air session remains untested.

## 2026-09-19 — Twin Cities live spectator checkpoint fixes (unreleased 730)

Stefan hosted Twin Cities in downloaded 729 as ggtothemax. Both the downloaded
729 native client and production browser were admitted, then disconnected before
map loading. No manual pause is required for a spectator; controller promotion
uses the existing automatic synchronization pause. The user's running host was
not changed. The valid 256x256 map remains unchanged.

The local production-code probe reproduced a 5,290,427-byte observer envelope,
above the old 5 MiB limit. The observer envelope is now bounded at 8 MiB on both
endpoints; the 4 MiB embedded save and 48 KiB chunk limits remain unchanged.
Rejection logs now include envelope size or checkpoint preparation errors.

After admission, populated city checkpoints exposed additional divergence:
- Ordinary save reconciliation ran an extra city effects/growth pass on only
  the viewer. Observer runtime version 2 now restores the exact phase, derived
  tax/civic state and city grids; viewers skip ordinary save reconciliation.
- Restored zone occupancy needs its dynamic power draw registered without growth.
- Reconfiguring unchanged mixed human/AI controllers changed unit rally logic.
  Viewers keep the saved roster, and the supplement restores the live house AI
  flags, including values changed by earlier controller promotion.
- UnitBase resolved targets while objects were still loading in ID order. A
  forward reference became NONE_ID. The failing checkpoint showed carryall 27's
  target 216 replaced by 0xffffffff before the first replay tick. Unit loading
  now defers resolving targets that have not been constructed yet. This also
  preserves forward targets in ordinary saves without changing the file format.

The Twin Cities probe supports --twin-cities --city --solo and JOIN_AT_CYCLE=1400
for later checkpoints. JOIN_TRACE=1 retains per-peer state every 200 cycles.
City runtime tests cover partial grid blocks, derived values, map-size mismatch
and truncation. Stream mismatch logs now print expected and actual fingerprints.
Early and later native joins matched through cycle 1800 and spectator departure
left the host running. The prior three-peer city promotion regression also passed.
Chrome/native Twin Cities ran without mismatch through more than 4700 host cycles
before final house-flag preservation was added. Reloading that browser was refused
while its old Player identity was still retained; no second successful join from
that run is claimed. The final source passes all seven CTest suites, native
and pinned-Emscripten builds, before/after native dependency audits and version
consistency checks. The final later native join matches cycle 1800, seed 2c041e17,
215 objects, digest 6180964237499f3f/1b997b15b2862830. Final three-peer promotion
matches cycle 1800, seed 1700292b, 51 objects. Final Chrome/native Twin Cities
spectating visibly runs beyond host cycle 2095 without a mismatch.

Evidence lives in /Users/stefan/Documents/Codex/2026-09-19/i-h/work/:
twin-controller-fixed, twin-later-join (failure), twin-later-object (target bytes),
twin-target-fixed (passing later join), twin-browser-final (passing browser),
twin-regression-promotion, and twin-final-* (final build and verification).
No release, push, PR, installed-app replacement or service deployment occurred.
Continue's original mouse-click freeze remains unreproduced. Full Access, native
build dependencies, Emscripten and Chrome control are working on the mini. Apple
signing/notarization still needs the user's Developer enrollment/signing identity.

## 2026-09-19 — Mac mini spectator failure reproduced and fixed; candidate remains unreleased

Work now runs locally on Stefan's Mac mini with Full Access and a connected
Chrome extension. Checkout: `/Users/stefan/Documents/projects/dunecity-campaign-controls`.
Native Homebrew dependencies and the pinned Emscripten 4.0.14 SDK are installed;
native and browser candidate 1.0.730 builds succeed. Do not use the laptop UI.

The reported released-729 native/browser spectator failure was reproduced using
the downloaded 729 DMG and the production 729 browser. Download SHA-256:
`70ce731928825498c582dbaef116995aab26d130924efc3d3d309321d324c224`.
The map is valid: Ergsun-Odenkirk intentionally has Player1,2,3,5. The menu counted
four houses but scanned only Player1..4 for teams, leaving the fourth team's
selection blank (-1). The runtime converted it to 255, so spectator checkpoint
validation rejected it before map loading. CustomGamePlayers now scans the full
supported slot range. The actual-map menu regression failed before the fix and
passes after it. Observer checkpoint refusal also logs the policy reason.
No map, save format, checkpoint limits or simulation/path budgets were changed.

Both released-client directions work when Team4 is selected manually for that
fourth house: production browser host/downloaded native spectator and downloaded
native host/production browser spectator. This isolates the menu bug without
rebuilding either release client. The temporary public test room was closed.
No installed app, quarantine state, production website or service was modified.

A separate real-peer promotion race is fixed: prepare now waits for same-roster
readiness across independent channels before ACK (bounded 30 seconds, no replay
extension). Changed readiness reports refresh our own report without echoing
identical ones, recovering reports ignored before an authenticated role change.
Regression tests cover delayed readiness, replay, timeout, premature commit and
conflicting roster. Two native three-peer city promotion runs matched at cycle
1800. Chrome candidate 730 joined two native candidate peers, kept spectating
after decline, retried, and became a Harkonnen controller with build controls.
Original peers matched at cycle 9431, seed 1b565295, 84 objects,
digest da2616c82add0d95/1041afe9dcdd63c3. The browser probe now compares 300 ticks
after its promotion checkpoint, allowing manual interaction before verification.
Protocol-9 behavior is documented in docs/late-join-protocol.md.

Continue remains an investigation, not a claimed fix. Stefan confirmed the
original action was a mouse click. The exact transferred `cities 3.dls` loads at
329538 and runs past the laptop's paused cycle 329556 on the mini using mouse-click
Continue in downloaded 729. The original save remains untouched; testing uses an
isolated profile. With diagnostics enabled, candidate 730 now records actual
pause transitions and their source (Options/Mentat/feedback/skip/Space/repeat),
plus resume events. This will identify a future unexpected pause without changing
pause behavior. Do not infer the cause was keyboard input.

Evidence is under `/Users/stefan/Documents/Codex/2026-09-19/i-h/work/`:
`sparse-teams-before.log`, `sparse-teams-tests.log`,
`promote-refresh-city.log`, `promote-refresh-city2.log`,
`browser-promote-verified.log` and its Host/Partner logs and digests.
Final build/test logs use the `final-` prefix. Native/browser final builds and
all seven CTest suites pass, with clean before/after native dependency audits.
The pause diagnostic was verified in candidate 730: an intentional Space pause
recorded source=space, menu_open=0, cycle=330855. That candidate run used keyboard
Continue after native automation mouse clicks did not activate its menu; the
released-729 Continue reproduction above used mouse clicks. 190 real-HTTP service
tests also passed on this mini.

Release remains pending: no push, PR, tag or deployment. Apple signing/notarization
still needs Stefan's Apple Developer enrollment and signing identity (none found
on either Mac). Never bypass Gatekeeper. Earlier isolated remote test directories
listed below have not been touched by this local test pass.

## 2026-09-19 — Unreleased 1.0.730 checkpoint; move interactive testing to Mac mini

Stefan asks for browser/native testing on the Mac mini so agents do not control
his laptop browser. Stop laptop UI interaction. SSH alias `claw` is reachable as
`/Users/stefan`; Codex CLI 0.153.4 is installed. No Codex/ChatGPT desktop app was
found in /Applications on the mini. An SSH project alone does not relocate this
session's local computer-use tools. Prepare a separate checkout and explicit
handover before resuming there; do not overwrite another agent's checkout.

Current changes are NOT released. Candidate 730/protocol 9 adds spectator-first
public running-game admission, an in-game request/cancel-to-play button, automatic
host request popup, decline preserving observation, and promotion using the
existing controller checkpoint barrier. An authenticated service roster change
and host prepare packet are both required before a viewer becomes a controller.
Promotion must discover original controllers, not just its previously known host.
Native dependency audits and all seven CTest suites pass. The real three-peer
promotion probe passes decline, retry, host popup and matching cycle-1800 state;
190 real-HTTP service tests and 18 browser transport tests pass. Browser 730 has
not been built or inspected yet; protocol documentation and final review remain.
Evidence under ../outputs: spectator-730-promote3.log, spectator-730-tests2.log,
spectator-730-service-all.log and spectator-730-web-transport.log.

The user's actual installed 729/native and published 729/Brave spectator failure
is UNRESOLVED, in both directions. Local Homebrew native host + Brave worked on
the same Ergsun-Odenkirk map, but that does not verify the downloaded static-vcpkg
app. A real public Brave room was created at Stefan's request: gg, Dune City,
4P - 128x128 - Ergsun-Odenkirk, shared hard AI plus three hard AI opponents. The
installed native ggtothemax was admitted, then reported direct connection lost
before loading the map. Native evidence: ../outputs/spectator-730-installed-browser-host-failure.log.
The host's Brave console later stopped accepting input for both Stefan and CUA;
no host-side root cause was captured. Do not blame a firewall without evidence.
The new rare failure diagnostics are not yet in published 729. The browser room
was left running; do not claim its continuing health without checking.

Continue's apparent freeze was a different observed state: the installed app
rendered ~60 fps but paused at cycle 329556 after advancing 18 cycles from load,
with no menu open. Pause origin remains unverified. Exact saved telemetry:
~/Library/Application Support/Dune City/ai-decisions/1789796530698464-0/events.jsonl.
Captured stack/log: ../outputs/spectator-730-installed-freeze.{txt,log}.
No save files changed; no fix claimed. Current laptop app was restarted at the
menu earlier and subsequently used by Stefan for the failed spectator attempt.

Gatekeeper remains an unsigned-distribution issue: installed 729 is ad-hoc signed,
no Apple signing identities were available on laptop or mini, and Stefan confirmed
he has no Apple Developer account yet. No notarization/signing pipeline changes
have been made. Do not remove quarantine or disable Gatekeeper. CUA selecting the
mounted DMG previously launched the wrong copy and caused a warning; never use
that path to attach the installed app.

Owned isolated remote test directories still need cleanup after testing:
/var/www/html/play-test-730-city and /var/www/data/dunecity-test-730-city.
They currently serve protocol-8 staging; production has not been changed here.

## 2026-09-19 — Combined 1.0.729 published and verified

PR 57 merged as 8416d26c; stable tag v1.0.729 points to that commit. All builds
and tests passed in release run 35421479427. GitHub publishes six desktop assets
(ZIP, DMG, AppImage, DEB, RPM, tar.gz) with the combined online/performance notes.
SourceForge run 35422575737 read back and verified all eight files, published
`dunecity-v1.0.729`, advanced `dunecity` from 52fe7aca to 8416d26c and confirmed
all three OS download defaults. Legacy master and historical releases are intact.

Website 2197473 deployed successfully in run 35422620015. Its browser is the
exact DuneCity-Emscripten artifact from stable run 35421479427, packaged from
8416d26c; redundant browser rebuild 35422575654 was cancelled. Live verification
matched all seven browser artifact hashes and the manifest, both release pages
and all six installer links, all 14 private signaling files and public route
parity. Health is OK. The earlier service-only rollout was e5dac6e/run35420385656.
The current public activity schema records named spectator seating but does not
store a separate spectator flag; committed start rosters contain controllers.

The exact release browser showed 1.0.729, applied 4:3, retained it after reload,
restored 1280x720/16:9 and rendered fullscreen at an enlarged viewport. Diagnostics
were off. The browser key-injection tool did not establish native Escape exit;
closing that temporary tab restored normal layout. Original settings/name were
restored; temporary public previews were removed. No local installed app was
replaced. Android remains 0.2.25. Gameplay/source verification is below.

Evidence: ../outputs/release-729-live-verification.json,
release-729-sourceforge.log, release-729-browser-qa.json and release-729-github.json.
The source-verified hot-join/protocol document was distributed as shared revision
4 of dunecity-hot-join-protocol-6; completion was confirmed for all destinations.

## 2026-09-19 — Passive spectators verified, 1.0.729/protocol 8

Stefan requires spectators to have no gameplay actions and never make active
players wait. Protocol 8 replaces the unreleased synchronized-observer design.
Spectators have a host-only connection, independent checkpoint/canonical tick
stream, bounded history/bandwidth/ACK windows and isolated timeout handling.
They are excluded from controller readiness, start barriers, timing and budgets.
Only the viewer loads and catches up; periodic state fingerprints drop a
divergent viewer alone. Rejected play requests become spectators. Actual player
hot joining retains its barrier, excluding viewers. Save format and pathfinding
node budgets are unchanged. See docs/late-join-protocol.md.

Verification on production source 423e403: native and Emscripten builds,
dependency/version checks, all seven CTest suites and 188 real-HTTP service tests.
The busy three-peer test includes armies, AI construction and a five-second
non-reading spectator: original players advance over 100 cycles during the delay,
all peers match at cycle 1800, and original players match at 1900 after departure.
A 40-second unresponsive viewer is disconnected alone while the original players
continue identically. Reject-to-spectate passes; AI-replacement hot joining still
matches at cycle 150. Evidence: ../outputs/spectator-729-{busy2,stall2,rejected},
../outputs/spectator-729-tests-final.log, spectator-729-service-final.log and
../outputs/hotjoin-729-regression.log.

Actual browser 1.0.729 on isolated public HTTPS staging chose Spectate, loaded
automatically, received moving armies and new AI construction, moved its camera
across the full map and exited cleanly. The original native players continued;
their cycle-1800 digests match. Browser integrity checks remained connected, but
the browser itself is not included in the fixture's file-digest comparison.
Evidence: ../outputs/spectator-729-https-browser3 and its adjacent log. Browser
profile name restored. Test markers now use atomic rename and the interactive
fixture continues beyond the digest checkpoint to permit live browser inspection.

Remote release remains authorized and is being completed through PR 57. At this
checkpoint no 729 stable tag/client publication exists; production service still
runs the earlier protocol-7-capable package. All service tests use isolated state
with notifications and analytics disabled. No local installed app was replaced.

## 2026-09-19 — Browser-host admission regression found before release

A real three-browser HTTPS test exposed a production-lobby interaction absent
from the initial native harness: when the spectator's channel arrived, the host
sent its original waiting-lobby settings and called its seat-assignment callback.
The newcomer's CrossplayMenu entered the old lobby instead of consuming the
synchronized checkpoint. The original players continued but the observer waited.
Stable release is held until this regression is retested.

The host now suppresses waiting-lobby delivery while a match or join transaction
is active, and a running-game newcomer ignores old lobby setup packets. The menu
probe checks both late-join rejection and normal waiting-lobby acceptance. The
real-peer fixture now retains the actual lobby setup/callback and fails if a
late newcomer invokes assignment. No version beyond the unreleased 1.0.728 is
needed. Production PHP was deployed successfully as website 3fb0090 in run
35416871576; all service hashes and request routes were read back. Client
publication, website installer links and SourceForge remain pending.

## 2026-09-19 — Spectator candidate 1.0.728; release paused for the addition

Stefan paused the authorized remote publication to add a play/spectate choice for
running games, automatic spectator admission, and reject-to-spectator behavior.
The 727 preparation branch was pushed, but no PR, stable tag or client release
was created. This candidate retains all earlier combined UI/performance/online
work. Network protocol is now 7; game save format remains unchanged.

The running-game Join Game prompt offers Request to play, Spectate and Cancel.
Host rejection converts the pending request to observation. Observers synchronize
a checkpoint automatically, have no house/controller slot, see the full map and
can chat/leave. They cannot send local commands, selections or path-budget stats;
receivers reject those packets too. A detached UI player is never registered or
saved. Observer departures do not end the original match. Co-op keeps two
controllers but allows observers within the existing eight-connection limit.
Older protocol service rooms retain their original queue/decline wire behavior.
Details: docs/late-join-protocol.md.

Final candidate verification: native and Emscripten builds, dependency/version
checks, bundled-mod validation, all seven CTest suites and 187 real-HTTP service
tests pass. Real three-peer reject-to-spectate tests pass locally and through an
isolated public HTTPS staging service, with matching state at cycle 150 and the
original two players continuing identically to cycle 180 after observer exit.
The normal AI-replacement hot-join regression also passes on protocol 7.
An actual fresh-profile browser chose Spectate in the prompt, automatically
loaded the running two-native-player checkpoint and showed the full-map
Spectating view. The native peers matched at cycle 150; the browser was visually
verified, not included in the digest comparison. The fixture deliberately stops
native command production at that point, so its later waiting overlay is expected.
The original browser profile correctly refused stale mod data; its test name was
restored and a fresh www-origin profile used instead, without deleting user data.

An earlier observer teardown crash was fixed by keeping its detached UI player
alive until object selection cleanup finishes. A test-only peer shutdown race was
fixed by checking the shared completion marker before pumping shutdown. The
isolated HTTPS service uses no production notification or analytics hooks.
Evidence is under ../outputs/spectator-728-{tests-final,service-final,
rejected-final,https-native,https-browser} and hotjoin-728-regression.
Remote release remains authorized but has not yet been published at this commit.

## 2026-09-19 — Explicit version-mismatch prompts; unified 1.0.727

Stefan asked for a prompt whenever online game versions differ. Normal directory
and invitation-code admission now compare the exact application version before
reserving a seat, not merely protocol/content. Hot-join admission uses the same
version-specific refusal and leaves the host request queue untouched. Redemption
also checks the host version for grants issued before a service update.
The service returns version_mismatch with host/client numbers and a same-version
instruction. New clients show an OK popup for that refusal, unsupported_version,
and older services' generic content_mismatch; failures return to choosing a game.
Text wraps to the screen width and does not repeatedly reopen after dismissal.
Same-version protocol/content checks remain in force; protocol stays 6 and saves
are unchanged. All prior hot-join, UI, performance and diagnostic work is retained.

Validation: 183 real-HTTP service tests pass, including normal code/public joins
and running-game requests with different version numbers but matching content
and protocol; a subsequent matching-version attempt succeeds. Native build and
dependency/version checks pass. Six core CTest suites pass, and the updated menu
probe passes at 640/854/1280 widths, asserting popup presence, both version numbers,
screen bounds, return to lobby and single dismissal for normal/hot join. Visually
checked the 640×480 popup in outputs/version-727-prompt.png. An initial probe-only
compile error (missing MsgBox include in the test harness) was fixed and rerun.
Final browser build and bundled-mod validation pass. Follow-up metaserver package
04c3c0bc5b1ac6f89a2a84577942aa73ba8aedba deployed successfully in run 35414815966:
https://github.com/VR48/dunelegacy.com/actions/runs/35414815966
All fourteen live service files match game source
2b5287af923c8345b2a1a1a9bbc48f8b57fd25bd; live health is OK. Installer and website
security/hash checks pass. No production test lobbies/chat were created. This is
the requested version-check follow-up to the authorized hot-join service update.
The local native/browser builds are 1.0.727; the hosted browser remains 1.0.707.
Restart the local app for the popup; older clients still see the server refusal
in their existing status area.

## 2026-09-19 — Host-approved hot joining; unified 1.0.726

Stefan requested an enabled-by-default checkbox, subsequently named **Allow hot
join**, on online custom-game setup. The lobby now lists waiting and joinable
running games with map/mod/status or elapsed minutes; selecting a row shows full
metadata. Running entries send a request rather than seating a stranger.
The host receives a news notification and uses Options → Join requests to choose
an eligible living house/controller, replace an AI, or share with an existing
human/AI where shared-house/co-op rules permit. Existing humans cannot be replaced.

This is direct-session synchronization, not just listing/UI. The host captures
an in-memory checkpoint, pauses existing peers, approves a name-bound service
grant, and transfers the bounded checkpoint over WebRTC. The existing roster
barrier commits the enlarged mesh, then every peer reloads the same checkpoint.
Original player state, teams, ownership and house colors remain; commands from
the old simulation epoch are discarded. A progress dialog allows the host to
cancel before commitment; aborted joins resume the original match. General
admission remains closed after start. Server-side named activity logs record the
new public participant and resumed roster without counting another new match.

Protocol is now 6; all participants need compatible 726 clients. Save version is
unchanged. Transport capacity remains eight humans and shared houses retain two
controllers. Network saves retain the existing 4 MiB cap; oversized checkpoints
fail before pausing. Listed minutes are wall time since first start, including
pauses. ENet/LAN and legacy relay hot join are not implemented. Details and bounds
are documented in docs/late-join-protocol.md.

Validation: final native build/dependency audit and all seven CTest suites pass;
181 real-HTTP service tests pass. Three-process local WebRTC probes cover AI
replacement, sharing with a human, sharing with AI, and cancellation, with matching
post-resume simulation digests. Menu probes cover 640×480, 854×480 and 1280×720.
Final browser build passes. An actual Chrome 726 newcomer discovered the running
native two-player match, requested entry, and loaded the assigned Harkonnen house
after approval. Original native peers matched at cycle 150; the browser was
visually verified, not included in the digest comparison. The fixture intentionally
stops natives at cycle 150, after which the browser waits for their commands.
The browser probe uses a same-origin local HTTP proxy to preserve the production
CSP; its first separate-origin attempt was blocked by that policy, not hot join.

All earlier performance/UI/logging changes remain in the same native and browser
726 build trees. No pathfinding node-budget changes. Stefan then explicitly authorized deployment of the new hot-join metaserver.
Website commit 98ec581f878e7c65d7f57a9eb2b973ada4d4be29 was pushed to main;
Deploy to Droplet run 35414236527 succeeded:
https://github.com/VR48/dunelegacy.com/actions/runs/35414236527
All fourteen installed service files and the manifest match game source
30c6e6d9161c74806d7c64e4b8a8439f56aa5601; live /p2p/v1/health is OK.
Installer integrity, browser security/hash, public activity and notification
fixture tests passed. No fabricated public chat/game records were inserted.
The hosted browser remains 1.0.707; only the metaserver was deployed. Local
native/browser 726 clients are ready; no public binary release was requested.
User settings/saves and any original match were not used by integration probes.

## 2026-09-19 — All mods, waiting players and public activity; unified 1.0.725

Stefan requested All mods as the default discovery filter, waiting-player count
and names, and named public-chat/public-game history in metaserver data. The
combined native/browser 725 also retains 724's Play Online rename/position below
Continue, 723's integrated chat/settings identity/public default, and all prior
performance/visual/settings work. No simulation or node-budget changes.

Discovery opts into all same-protocol content hashes with mod metadata. All mods
is the default; selecting a mod filters locally without changing the active mod.
Joining switches only to an installed mod with a matching content fingerprint;
a mismatch restores the previous mod, and authoritative admission checks remain.
Older service responses retain their original compatible-content behavior.

Players waiting appears below chat with a count and up to twelve names across
mods. The existing five-second chat poll carries presence, with a twenty-second
activity window. It counts waiting sessions, including yourself, not people in
matches. Failed polls clear stale counts; older services explicitly report count
unavailable. No presence records or extra polling loop are added.

The PHP service emits separate trusted public-activity events for accepted chat
(session name/text/time), newly seated public host/client names, and the roster
at public match start. The website receiver in ../dunelegacy.com persists these
in analytics_public_activity via PDO or Python, separate from anonymous lifecycle
and existing match data. Private games are excluded from the named table; tokens,
codes and addresses are omitted. Server analytics_enabled controls capture,
independent of client diagnostics. Game events deduplicate retries. No historical
backfill or outage retry journal; storage errors do not block accepted actions.
Stefan explicitly authorized pushing/deploying the metaserver update in this
session. Website changes rebased over newer published 707/download-stat updates
and shipped as 834db03 + 3472e53. Deploy to Droplet run 35410689143 succeeded:
https://github.com/VR48/dunelegacy.com/actions/runs/35410689143
Live service hashes verify all thirteen files against game commit 8a3978f; five
website receiver/entrypoint files match local, analytics_enabled is true, health
is OK, and analytics_public_activity is initialized with database quick_check OK.
No fabricated public chat/game records were inserted (table empty at verification).
Private SQLite backup verified before deployment:
/var/www/data/backups/public-activity-20260919T004957Z/games.sqlite.
The hosted browser game remains the independently published 1.0.707; the requested
combined 1.0.725 browser assets are built locally. This deployment publishes the
metaserver service, not a new public game release. Restart the local native 725
app to see waiting counts. Original running match was not restarted.

Validation: native/browser builds and dependency/version checks pass; all seven
CTest suites pass, including 640/854/1280 real-menu renders, All mods versus a
specific mod, settings identity, waiting display and stale-count clearing.
Seven new real-HTTP service tests plus all 166 existing service tests pass.
Website tests cover PHP/Python validation, both database paths, UTF-8, retries,
conflicts, roster persistence and old-table preservation; existing analytics
Python/PHP tests pass (one existing environment skip). Real service-generated
create/join/start/chat events were also passed through both website storage paths
and deduplicated to four rows in a disposable database.

Unmodified browser assets tested on a fresh loopback origin: Settings name
Browser tester, automatic chat entry, All mods listing Vanilla and Dune City,
specific Dune City filtering, Players waiting: 2 / Alice, Bob. No public posts or
rooms; original desktop match/profile untouched. Browser test tabs/servers closed.
Screenshots: ../outputs/interface-725/. Build/test logs: ../outputs/interface-725-*,
public-activity-tests.log, signaling-725-tests.log, public-activity-725-integration.log,
relay-analytics-725* and analytics-runtime-725.log. Canonical app remains
build-714/bin/dunecity.app; browser remains build-714/emscripten/bin.

## 2026-09-19 — Play Online immediately below Continue, unified 1.0.724

Renamed Join Online to Play Online on home and the lobby heading. Home order is
Continue (when available), Play Online, Campaign, Custom Game, Load Game,
Settings, Extras, Quit. Keyboard navigation follows the same order; without a
save, Play Online is the first active destination. Both native and browser 724
builds include all 723/earlier work. All seven CTest suites, dependency and version
checks pass. Real menu renders cover Continue present/absent at all three probe
sizes; ../outputs/interface-724/home-continue.png verifies the requested order.
Build/test logs are ../outputs/interface-724-{native-build,browser-build,ctest}.log.

## 2026-09-19 — Public default and integrated lobby chat, unified 1.0.723

Stefan repeated the intended online interface: Public - anyone by default, public
chat in the right pane of the first Join Online screen, and the Settings player
name without a confirmation button. Implemented in both native/browser 723.
Custom and campaign setup now start public; reopening online saves for hosting
also defaults public. Explicit private selections still pass through unchanged.

Join Online shows the Settings name as a label and keeps game discovery, invite
entry and public chat visible together. Chat enters automatically on menu update,
reconnects after expiry with retry spacing, and resets into the matching content
lobby when mods change. Invalid names are corrected in Settings; this screen no
longer edits or saves a separate name. No simulation, node budget or logging
default changes. All prior visual, performance and diagnostic-setting work remains.

All seven CTest suites pass, including real menu checks at 640×480, 854×480 and
1280×720, automatic chat entry/re-entry, settings identity, public defaults and
explicit private preservation. Native/browser builds and dependency/version
checks pass. Actual unmodified browser assets tested against the loopback-only
tests/menu/serve-browser-lobby-fixture.py: set Browser tester in Settings, Apply,
Join Online; observed enter/poll requests and the right-hand chat displaying the
fixture message without confirmation. No messages sent to the public service.
Browser Custom Game visibly defaults Public - anyone. Test tab/server closed.

Screenshots, local fixture request log and build hashes: ../outputs/interface-723/.
Build/test logs: ../outputs/interface-723-{native-build,browser-build,ctest}.log.
Canonical native app stays build-714/bin/dunecity.app; browser stays
build-714/emscripten/bin. Original user match/profile untouched. Restart the
desktop app or reload the browser to use 723. No push or publishing.

## 2026-09-19 — Actual browser logging on/off benchmark, 1.0.722

Stefan requested measured browser performance with logging disabled. Tested the
shipped 722 JS/Wasm/data in Chromium 152 using the actual cities 3 save and copied
1440×900/4 ms settings/mods. Four sequential on/off/off/on runs, 10 s warmup plus
120 s measured each, fresh private profiles, four periodic storage flushes each,
all visible. Test-only RAM probe counts complete presentation intervals: SDL
swap and game loop each yield once; counting every yield incorrectly doubles
FPS. First eight stacks verify alternating call sites in every accepted run.

Median FPS 13.26 on → 13.54 off (+2.1%); p99 frame 225.35 → 217.05 ms (-3.7%).
Both modes averaged 217 frames >100 ms per run. Worst observed frame across each
mode: 321.1 ms on, 338.1 ms off. No >500 ms frames reproduced. Off created zero
diagnostic files versus 26.2–26.7 MB on per run including warmup. This supports
keeping browser diagnostics off by default but does not establish logging as
the main stall cause. Small differences have limited precision with two runs
per mode and the user's desktop game/background apps still running.

On-run internal telemetry attributes 52.4% of accumulated frame time to paths,
with individual AI frames up to 253 ms and city phases up to 125 ms. Those
windows include warmup; not exactly the browser comparison interval. No new
optimization or path-budget change was made. See docs/browser-logging-performance.md
and tests/performance/*browser-logging-benchmark* for method/reproduction.
Raw data/manifests: ../outputs/browser-logging-performance-722/; calibration files
excluded. All four results pass visibility, timing-callsite, file-state and
storage-sync validation. Test tabs/server closed; original match untouched.

## 2026-09-19 — Settings-driven diagnostics, unified 1.0.722

Stefan requested diagnostic logs for development, off by default in browsers.
Source f325c7b adds Settings → Advanced → Diagnostic logs (development), stored
as General / Diagnostic Logs in the user INI. Browser default is false (including
older profiles without the key); desktop default remains true. Fresh config
creation explicitly uses the platform default even when copying a template.
Apply persists the choice and reinitializes the menu; the next match uses it.

Disabled capture stops AI JSONL/ledger/performance aggregation, performance text
writes, routine SDL messages and the extra development crash-log mirror. Errors,
warnings and critical messages remain on stderr; fatal exceptions also log at
critical priority. Browser stdout/stderr stays in the console rather than a
persistent profile file. Save/config persistence, node budgets, simulation and
public match reporting are unchanged. Existing traces are retained, not deleted.
DUNECITY_AI_TELEMETRY=0 remains an extra structured-capture opt-out; --showlog
only controls the native output destination. See docs/diagnostic-logging.md.

Both native build-714/bin/dunecity.app and browser build-714/emscripten/bin are
1.0.722 with all earlier visual/performance changes. Browser checked in a fresh
isolated origin: checkbox defaults off; opt-in survives reload; switching off
survives reload. Native Settings rendered correctly at 640×480 and the menu
probe checks the preference, Apply state, persistence and Advanced layout.
All seven CTest suites, dependency and version checks pass. Two real-engine
cities 3 runs (diagnostics off/on) each matched 41 checkpoints across 4,000 cycles
and the same complete saved gameplay state as 721, excluding only release label.
Disabled run created no events.jsonl, performance text or development mirror;
the deliberate error-reporting check still reached stderr. Enabled run created
both structured and performance captures. Timings overlapped build activity and
are not a performance benchmark. Source checkpoint 065bca5 includes the test
assertions. Logs/comparisons/manifest and desktop UI capture are in the parent
workspace's outputs/browser-diagnostics-722/. No live game restart or publishing.

## 2026-09-19 — Same-save graphical stall reproduction, unified 1.0.721

Stefan reported continuing whole-picture pauses with the OS pointer still moving,
and explicitly requested testing the same game. Loaded his actual `cities 3.dls`
at cycle 329538 with copied 1440×900 settings/mod overrides and 4 ms game speed
in a private profile. The normal graphical/input/pacing loop ran for 60 seconds
before and after the change. The rendered city was visually checked. The baseline
reproduced a 298 ms frame at cycle 331600, also seen in the live user's match:
275 ms was AI work. We did not capture the estimated one-second freeze.

Source 64fe4aa reuses identical building-placement searches across equivalent
construction yards within a single AI pass. Reservation exclusions are part of
the cache context; geometry, reservation and production-mode changes invalidate
results. No AI cadence, pathfinding node budget, scoring or simulation changes.
All earlier UI/zone-preview/menu/A* improvements remain in the same build.
Observed graphical maximum fell 298.460→157.497 ms; frames over 100 ms fell
51/1877→32/2000 in sequential one-minute runs. Live-game CPU contention varies,
and shorter stalls remain. Do not describe this as eliminating all freezes.

New `frame_stall` events record every frame of at least 100 ms, including smaller
consecutive stalls, with session wall timestamps, input/command and phase timing,
menu/focus state, and separate between-frame gaps. All 32 candidate stalls were
captured individually. AI build evaluation/orders and cache hits have own metrics.
Logs remain under the user profile's `ai-decisions/<session>/events.jsonl`.
See docs/cyclic-stall-performance.md for details and reproduction commands.

Both native and browser builds are 1.0.721 from source 64fe4aa. Native remains
build-714/bin/dunecity.app (build symlink unchanged), browser remains
build-714/emscripten/bin on port 8714; browser home version visually verified.
All seven CTest suites and native dependency/version checks pass. The packaged
engine matches the frozen 720 engine across 4,000 cycles, all 41 checkpoints,
and all saved gameplay bytes except the release label. That final correctness
run overlapped browser compilation and is not a timing benchmark.
Artifacts, live samples, graphical logs, fixed comparisons and build manifest
are under the parent projects workspace's outputs/game-cyclic-lag-720/.
No live-match restart, save overwrite, push or public release. Save and restart
the canonical app to use this combined build.

## 2026-09-19 — Reduced periodic planning pauses, unified 1.0.720

Stefan reported regular pauses in the running 1.0.719 match. Live telemetry
identified AI construction planning as the largest spike (235 ms of a 260 ms
frame, versus 14 ms pathfinding). Commit 4350ce7 reduces local access-check
allocation/work, repeated city-neighbour scans and per-cycle tax map scans.
Node budgets, AI cadence, city growth/effect cadence and payment timing are
unchanged. All earlier menu, sidebar/zone-preview and pathfinding changes remain.

The real-engine test ran the same large city for 2,000 cycles. Three baseline
and three optimized runs matched all 21 checkpoints and byte-identical final
saves. Median p99 simulation-cycle time fell 124.49→66.18 ms; wall-clock results
varied with the live match still running, and occasional longer pauses remain.
This is not an FPS claim. Exhaustive access-graph comparisons and distance-field
oracle tests pass; all seven CTest suites pass in the final native build.
The final 720 engine also matched the baseline save except its release label.
See docs/periodic-pause-performance.md for measurements and reproduction.

Both native and full browser builds now come from source 4350ce7 at 1.0.720.
Desktop: build-714/bin/dunecity.app (build remains a symlink to build-714).
Browser: build-714/emscripten/bin, preview port 8714; home-screen version 1.0.720
visually verified. Native dependency and version checks passed. Artifact hashes,
logs and comparisons are in the parent projects workspace's
outputs/game-pauses-719/unified-build-manifest.json and neighbouring files.
The packaged-engine comparison ran alongside the browser compiler; its timing
is excluded from the benchmark table. No live game restart, push or release.

## 2026-09-19 — Cheaper A* searches, local 1.0.719

Follow-up: Stefan requested one build containing all changes. Both native and
full Emscripten outputs now come from source commit 660d913 at version 1.0.719,
including 717 menu fixes, 718 sidebar/previews and 719 pathfinding. The usual
build-714/bin app and build-714/emscripten/bin preview on port 8714 are current.
Browser startup was visually verified: v1.0.719, teal mod label, correct initial
Campaign focus. Artifact hashes are recorded in
`outputs/game-performance-20260919/unified-build-manifest.json` in the parent
projects directory. The native build is up to date; the seven integrated CTest
suites already passed. No restart, public publish or additional source change.

Stefan requested pathfinding optimization without changing the node budget.
Searches now cache passability per tile for one synchronous search, skip closed
neighbours before occupancy/cost work, and calculate parent direction once per
expanded node. Node budgets, expansion limit, queue order, heuristic and heap
ordering are unchanged. Tile-buffer reset clears the cache between searches.

The real-engine differential probe compared 3,815 queries across 11 ground-unit
types, repeated three times with an obstacle added/restored between rounds.
Every route and expanded-node count matched the pre-change implementation.
Search time fell from 6,625.871 to 4,804.240 ms (27.5%); this is a pathfinding
benchmark, not a measured full-game FPS gain. Clean native Release, seven CTest
suites, dependency/version checks and Emscripten A* compilation passed before
integration. See docs/pathfinding-performance.md and the reusable
`tests/pathfinding/run-pathfinding-probe.py` for method and evidence.

The change is integrated over the 1.0.718 sidebar work. The usual build-714 app
is rebuilt as 1.0.719 without restarting the running match. No push or release.

## 2026-09-19 — Readable building sidebar and live zone previews, local 1.0.718

Stefan requested readable selected-building text and an icon matching the selected
building. Structure sidebars now draw an opaque shared dark panel. City stats use
light text with no shadow (zones 14px, other structures 12px), larger row spacing,
and zone labels use 14px. Zones omit the redundant Role row and population-level
suffix because name/density are already shown above. Zone names wrap deliberately;
civic replacements are identified as Hospital or Church.

The zone sidebar hides the generic construction icon and draws its current map
sprite. ZoneStructure::drawPreview uses the live atlas/frame, owner colour and
aspect ratio, including vacant lots, developed zones and civic overlays; fogged
objects use the remembered frame. It refreshes textures through GFXManager to
avoid stale pointers after mod/cache changes. The optional Dune2 zone skin uses
the same skin-selection path with UI destination bounds. This is presentation
only; no city simulation, saves or commands changed. Other structures retain
their existing detail portraits.

Native Release and Emscripten builds and all seven CTest suites passed. Real controls probe at
/tmp/dunecity-sidebar-718-probe passed and compares preview pixels with the actual
selected atlas frame, verifies a changed preview after growth, and checks hospital/
church previews and names. Vacant/developed/civic sidebar captures were inspected
at 640x480. Desktop build remains build-714/bin/dunecity.app (build symlink), now
1.0.718; browser output remains build-714/emscripten/bin on port 8714. Restart or
reload to use it. No push/release or running-match restart.

Concurrent AStarSearch edits and tests/pathfinding belong to another task; they
are not part of this sidebar change.

## 2026-09-19 — Home menu colour and focus, local 1.0.717

Stefan reported Campaign always outlined even with the pointer elsewhere and
requested a distinct mod-label colour. The active mod banner is now teal on the
shared dark background, without the black strip or duplicated white shadow.
MainMenu keeps the default keyboard destination but suppresses its focus outline
until keyboard input; pointer movement/clicks restore hover-only highlighting.
Keyboard input clears stale hover so two destinations do not appear selected.
The premature Campaign activation before attachment to its container is removed.
Button exposes an opt-in focus-visibility setting with its existing behaviour
as the default, so other menus, confirmations and toggle states are unaffected.
The real menu probe checks initial, keyboard, pointer and enter/leave hover states
and captures the resulting home screens at its three renderer sizes.

Native Release and Emscripten builds, dependency checks and all seven CTest suites passed
(/tmp/dunecity-home-717-tests.log). Home captures confirm teal text, no initial
outline, and a visible keyboard-focus outline at 640x480, 854x480 and 1280x720.

Build location stays build-714/bin/dunecity.app (also build/bin/dunecity.app), now
1.0.717; web output stays build-714/emscripten/bin, served on port 8714. No release
or push; a running app needs a restart to show the updated menu.

## 2026-09-19 — Consistent readable menus, local 1.0.716

Stefan requested consistent, readable menus, including Game Settings, City Budget,
the top buttons and sidebar. Shared DuneStyle now uses opaque dark backgrounds,
light text without the old text shadow, gray controls and gold focus/toggle edges.
Buttons size their text to available space; lists use 14px text and 20px rows.
Dark team-colored labels are lightened while explicitly colored label backgrounds
retain their intended contrast. Sidebar action symbols use light ink with alpha
preserved; the actual black cursor assets and explicit-Move rules are unchanged.
Options/Mentat now use the same text-button style as Budget and feedback. The
empty-sidebar path toggle says Paths on/off and keeps the existing saved setting.

Game Settings is now 440x352, with 24px title, 18px labels, visible slider tracks,
36x32 adjustment buttons and Apply/Cancel. Save/Load is 440x360 with larger title,
list and buttons. Game Rules and confirmation dialogs have larger text. Campaign
results use the dark background while retaining house emblems and team bars.
City Budget is 620x460 with a 24px title, gold section headings, two columns for
forecast/status, and separate tax/funding controls. It fits 640x480 and preserves
the deterministic tax/funding commands. The budget test checks representative
long figures from Stefan's screenshot fit the forecast column.

Validation: native Release and Emscripten builds passed; seven CTest suites pass
(menu flows/captures at 640x480, 854x480 and 1280x720). Real-engine controls and
budget probe passed in DuneCity at /tmp/dunecity-menus-716-city-ready; results
probe passed at /tmp/dunecity-menus-716-stats-ready (level 9 is required for its
three-house fixture; level 4 cannot supply the fixture). Inspected rendered
settings, budget, save, confirmation, sidebars, game rules and results. Browser
startup/welcome, keyboard navigation and Settings were checked in the in-app
browser. No Windows/Linux runtime verification. Dependency and version checks
pass. Bundled font lacks Unicode minus: use ASCII '-' for adjustment buttons.
GFXManager loads before the global GUIStyle exists, so asset generation uses a
local DuneStyle, not GUIStyle::getInstance().

Current app: build-714/bin/dunecity.app, reached by the existing build symlink;
this configured tree now contains 1.0.716. Web preview remains port 8714. Restart
or reload to use it; no running match was restarted and nothing was pushed or
released. Suggested helpers (advice only): Next construction yard/factory (existing
G/F actions), Idle harvesters, and Latest attack. No speculative helpers added.

## 2026-09-19 — Readable pause menu and top-row paths toggle, local 1.0.715

Stefan requested the movement-path toggle alongside the top action icons and a
larger, readable pause menu, explicitly allowing a new visual style. The pause
menu now uses a 440px-wide dark panel, 40px-high dark buttons with 20px white
labels, a 24px light title and six-pixel gaps. Width fits the renderer; the full
seven-action campaign menu is 408px high and fits 640x480. Keyboard hover/focus
uses a gold outline. Existing callbacks, multiplayer restrictions and skip/quit/
restart confirmations remain; online games still display their ongoing status.

UnitActionBar packs visible unit actions in four columns, with Paths immediately
after Move and Attack. Single-unit and group sidebars share it. It updates when
capabilities change and wraps larger selections without shrinking icons. Low
renderer heights use compact stance-button gaps so even ten actions and all six
stances fit 640x480. Retreat is full width again. The path toggle continues to
use the same saved preference; explicit-Move cursor behavior is preserved.

Validation: native Release and Emscripten builds passed; all seven CTest suites
passed (`/tmp/dunecity-menu-715-final-tests.log`). The existing controls probe
passed and produced visually inspected pause-menu, single/group sidebar and
maximum-action-count captures at `/tmp/dunecity-menu-715-final`. Native dependency
checks and version consistency pass. No new live browser interaction claim.

The configured `build-714` cache now contains **1.0.715**, reached by the existing
`build` link; it was rebuilt in place rather than relocating a configured tree.
The browser preview remains http://127.0.0.1:8714/dunecity.html and now serves 715.
Restart the desktop app or reload the preview to use it; no running match was
restarted. No push, PR, release or SourceForge mutation in this follow-up.

## 2026-09-19 — Move cursor only for explicit orders (1.0.714 follow-up)

Stefan requested that the move icon appear only after clicking Move or pressing M.
Ordinary ground/friendly-unit hover now retains the pointer. Contextual attack
and harvester-return cursors remain. Desktop and browser use the same cursor
selection code; explicit Move mode already maps to the move icon on both.
The existing real-engine regression now checks the pointer on ordinary terrain,
M and the actual sidebar Move button, plus hidden-enemy pointer behavior.
Native Release and Emscripten builds passed; real-engine regression passed at
`/tmp/dunecity-explicit-move-714`. Native dependency and version checks passed.
The existing 1.0.714 local builds were refreshed; restart the desktop app or
reload the 8714 browser preview to pick up this follow-up. No running match
was restarted, and no release or push was performed.

## 2026-09-19 — SourceForge fixes, local 1.0.714

Implemented bugs 105 (Ctrl+0), 86 (screenshots), 113 (palace queue cancellation),
88 (Stop shortcut), and features 62 (matching units) and 45 (hover intent/feedback).
Ctrl+0 now clears selection and group membership with a single sidebar refresh;
null/stale IDs are tolerated. Palace placement cancels other yards' palace orders
only when Only One Palace is enabled. Screenshot allocation follows physical
renderer/texture dimensions; writing uses the writable user screenshots folder,
checks failures, syncs browser storage and offers a browser PNG download.

S stops owned selected units through normal multiplayer commands; with WASD,
Shift+S stops while S remains camera movement. T selects matching owned active
unit types on screen, Ctrl+T across the map. Shift+T preserves the old timer
shortcut. The earlier review incorrectly called T a no-op. Contextual cursors
show move, attack or harvester return intent; menus, selection drags and friendly
left-click selection retain the arrow. Normal object orders flash their target
outline. Both hover and normal orders use explored, unfogged, visible targets.
Local feedback adds no network/save state.

Validation: native Release and Emscripten 4.0.14 builds passed, all seven CTest
suites passed, dependency and version checks passed. Real-engine SourceForge
probe passed in Vanilla and DuneCity, including both palace-limit settings,
stale IDs, group clearing, Stop/WASD, on/off-screen/mixed-type selection, timer,
visible/hidden target intent, feedback and actual screenshot writes at physical
and texture sizes. Evidence: `/tmp/dunecity-sourceforge-714c` and
`/tmp/dunecity-sourceforge-714-city`. Existing controls/sidebar probe passed at
`/tmp/dunecity-controls-714b`. Two diagnostic fixture fixes were necessary:
Palace must meet campaign prerequisites to survive a build-list refresh, and
Tile::setExplored takes a house ID, not a team ID. The stale-ID test exposed an
extra Ctrl+0 sidebar refresh crash, fixed before the passing runs.

`build` points to `build-714`. Local browser preview: http://127.0.0.1:8714/dunecity.html.
No existing match was replaced. SourceForge comments/closures are authorized but
not yet submitted: computer use reports the Mac locked. Browser interaction and
PNG-download checks likewise remain pending; a successful web build is not a
browser runtime test. No Windows/Linux runtime test, push, PR or release performed.
The SourceForge review document records the six follow-ups and remaining scope.

## 2026-09-19 — SourceForge outstanding issue review (no game change)

Reviewed the public REST inventory: 185 total bug/feature/support tickets, 89
not closed. Full triage with links is in
`docs/sourceforge-issue-review-2026-09-19.md`, against local 1.0.713 / 386ca05.
Three defects remain demonstrable: bug 105 Ctrl+0 selection iterator invalidation
(SIGSEGV), bug 86 screenshot logical/output buffer mismatch (guarded SDL read
wrote 678,656 bytes beyond nominal logical allocation), and bug 113 unconditional
cancellation of another yard's palace queue with onlyOnePalace=false.
Bug 88 Stop shortcut is absent; feature 62 select-same-type is absent (correction: T previously toggled the timer).
Mentat help 115 passed a real topic/click/update test. Many older reports have
later-version success comments or specific code guards; unresolved old crashes
were not declared fixed just from their age. Useful remaining features include
hover-intent cursors, accessible friend/foe colors, attack-move and queued orders.

Diagnostic evidence: `/tmp/dunecity-sourceforge-probe-713{,b,c}`; API snapshot
`/tmp/dunecity-sourceforge-details-20260919.json`. The diagnostic used isolated
profiles and existing build objects; user matches were not touched. This was
assessment only: no engine changes or ticket comments/closures, no push/release.

## 2026-09-19 — Harvest elsewhere after danger, local 1.0.713

Stefan reported harvesters evacuating to base and then returning to the same
unsafe spice. QuantBot's safety policy previously prioritized refinery refuge
for every threatened vehicle, including empty ones, and retained the old
harvesting guard point across unloading/deployment.

Safety now chooses a safe alternate spice field first for empty/partial loads.
Full loads and existing cargo-return trips still unload; an unsafe remembered
job is replaced with the safe field before the refinery order. If no safe field
exists, loaded vehicles unload with harvesting stopped, while empty vehicles
move to a safe hold. Safe unloading trips are preserved. Visible threats at the
vehicle/job now refresh field memory before damage occurs; fields within six
tiles of an incident are excluded for two minutes instead of receiving a weak
distance penalty. Fog therefore does not immediately reopen the evacuated field.
The existing corridor checks still apply; no new omniscient enemy query, command
format, save field or platform-specific behavior was added.

Validation: the new real-engine `--harvester-safety-probe` fails against 712 with
"Evacuation unnecessarily returned to refinery" and passes against 713 in both
vanilla and DuneCity modes. It checks empty/partial relocation, full-load return,
replacement guard point and actual deployment hook, hidden-enemy memory, no-safe-
field holding/unloading, preserved return orders, and cooldown expiry/resumption.
Evidence: `/tmp/dunecity-harvester-713-before2`,
`/tmp/dunecity-harvester-713-after`, `/tmp/dunecity-harvester-713-city`.
Native Release and Emscripten 4.0.14 builds succeeded; all seven CTest suites
passed (`/tmp/dunecity-713-tests.log`); Ninja dependency and version checks passed.

`build` points to `build-713`; browser preview is http://127.0.0.1:8713/dunecity.html.
Existing running games were left intact. Changes committed locally; no push,
PR or deployment. No specific GitHub ticket was supplied for this follow-up.

## 2026-09-19 — Sidebar path toggle and clear targeting icons, local 1.0.712

Stefan requested an in-game sidebar toggle for movement paths, matching sidebar
icons, and transparent white areas so action cursors do not hide their target.
Single-unit and group sidebars now have a route-symbol toggle beside Retreat;
the empty-selection sidebar has a Movement paths button. All use the same
General/Movement Paths preference as Settings, persist immediately, and request
browser filesystem sync. This is local display state, not a simulation command.

Unit/group move, attack, capture, drop, heal, repair, return, deploy and destruct
buttons now render compact icons from CursorAppearance's shared vector geometry.
The black action symbols have no white paint and an open 7-unit diameter aiming
centre. The ordinary pointer keeps its contrasting white border. Button icons
fit the existing 26px rows; no extra vertical row was added to the unit controls.
The route toggle shares the Retreat row, including at 640x480.

Validation: native Release and Emscripten 4.0.14 builds succeeded; all seven CTest
suites passed. Cursor tests check transparency at the hotspot at every supported
size, absence of white action pixels and distinct silhouettes. The real controls
probe now runs at 640x480, clicks single/group/empty-selection sidebar toggles,
checks persistence and synchronization, and captures all three layouts. Reviewed
`/tmp/dunecity-controls-712c/sidebar-{single,group,empty}.bmp`; all controls fit.
The diagnostic must present between captures: repeated draw/readback without a
frame boundary hid newly allocated group-button textures under sdl2-compat;
adding the real frame boundary fixed the diagnostic, with no production workaround.
Evidence: `/tmp/dunecity-712-tests.log`, `/tmp/dunecity-controls-712c`,
`/tmp/dunecity-712-web.log`. Browser preview: http://127.0.0.1:8712/dunecity.html.

`build` now points to `build-712`. Existing live games were not restarted. No
push, PR or deployment. The 711 issue closures remain historical local-build
notes; this follow-up changes the action icons' white border to transparency.

## 2026-09-19 — Feature requests and shared cursors, local 1.0.711

Implemented #50/#55: one SDL platform cursor path, shared by desktop and web,
with black vector shapes, a smooth white border, 1.5x default size, and matching
pointer, force move, attack, capture, carryall drop and heal icons. The old game
frame overlay and duplicate platform-sprite cache were removed. Menu/modal
frames use the normal pointer; gameplay supplies the current action. Hidden
mode never installs a new visible action cursor. Resources are released before
SDL shutdown, including menu-only sessions. Geometry and hotspots live in
`include/misc/CursorAppearance.h`; no browser-specific icon set is maintained.
Stefan rejected the first white raster arrow/double-outline design. The black
second pointer in his screenshot was Codex's pointer, not evidence of a game
cursor bug; the original #55 report remains the motivation for single ownership.

Settings > Controls now offers optional WASD camera (Shift+A attack, Shift+D
carryall drop), optional left-click orders (drag/Shift/friendly clicks still
select; right-click cancels), and selected-unit movement paths. Keyboard options
are local preferences, not simulation/network state. #29 moved Skip mission to
the pause menu, with Cancel focused by default and eligibility rechecked before
queuing the existing campaign command. #5 adds an explicit City sim On/Off lobby
row and updates it on host mod changes/downloads. #54 preserves audio errors and
falls back to SDL's silent mixer if the device cannot open, without overwriting
music/SFX preferences; Audio settings explains that restart retries the device.

Validation: native Release and pinned Emscripten 4.0.14 builds; seven CTest suites;
real menu rendering at 640x480, 854x480, 1280x720 with unavailable audio driver;
real campaign controls probe covering default/left move, left attack, drag/Shift/
friendly selection, right cancel, shifted attack, path renderer state, skip
cancel/confirm/replay guard. Native Cocoa accepted all 30 cursor action/scale
combinations and visibility changes. Browser SDL emits a 33x33 PNG CSS cursor
with explicit hotspot at default scale. Browser playtest confirmed distinct
force-move and attack cursor images (16,16 hotspots), followed by the normal
menu cursor (6,6) on opening pause. No Windows/Linux hardware run is claimed.
Evidence: `/tmp/dunecity-711-tests.log`, `/tmp/dunecity-menus-711`,
`/tmp/dunecity-controls-711d`, `/tmp/dunecity-711-web-build4.log`.

Older #14: source already includes dual-mono default, mixer format logging and
channel-safe ADL callback from 365272c; no reproduced distortion, so keep open.
#16: inspected original 1.0.86 Windows attachment; unsymbolized SIGSEGV addresses,
no save, small auto.rpl, and a misleading "game not initialized" crash diagnostic
while match telemetry was active. Cannot establish a current fix; keep open.
#12/#13 still include obsolete 3x3 specifications that conflict with the settled
2x2 architecture; do not change that architecture silently. Spacing Guild and
fire services explicitly excluded by Stefan. No related implementation added.

`build` points to `build-711`; the user's running 708 process was not restarted.
This is local work only: no push, PR or deployment. Closed issues #1, #2, #5,
#29, #43, #50, #54, #55 with implementation/validation comments. #1/#2 were
already implemented; new-fix comments explicitly say local and pending release.
Unresolved/partial #3, #12, #13, #14 and #16 remain open.

## 2026-09-19 — Keep reactors behind the fighting, local 1.0.710

Stefan reported Fremen repeatedly building nuclear plants beside the enemy.
Inspected the running 708 app and capture `1789747703462791-0`: SCENF022.INI,
seed 648039438, Fremen house 3 full QuantBot Brutal (`support=0`). Eight Fremen
reactors detonated in the captured match. Later construction selections report
700–2100 enemy-fire risk and negative rear scores; one completed replacement
lasted eight game seconds. These are observations of 708, not a 710 replay.

Reactors were exempt from normal live-fire rejection. Their ranking also put
blast spacing above the amount of enemy fire when every plot had some risk,
and distance behind the base was only a packing-score bonus. Emergency power
placement repeated that exemption. Reactors now reject known weapon halos,
rank safe plots by loss history, clearance beyond enemy weapon reach, blast
spacing and rear position before packing, and compare the full buildable map.
Redevelopment uses the same ranking. Only windtraps retain the exposed emergency
placement fallback. A finished reactor with no unexposed physical footprint is
refunded, freeing the yard for a windtrap; a friendly unit occupying a safe
footprint still causes a wait. Existing blast-spacing preferences and decaying
loss history remain; this is not an absolute ban on recently damaged districts.
No new saved state or hidden-enemy knowledge was introduced. Policy v73.

The real-engine `--reactor-safety-probe` first fails against 709 with
`Reactor planning accepts enemy fire` (`/tmp/dunecity-reactor-before-710/run.log`).
710 passes rejection, two-plot weapon distance/rear selection, blocked-unit
waiting, exposed finished-order refund/no immediate requeue, and safe windtrap
recovery (`/tmp/dunecity-reactor-c-710/run.log`). The pre-existing disappearing
footprint probe also passes (`/tmp/dunecity-nuclear-b-710/run.log`); its fixture
now clears an unrelated yard upgrade before the independent recovery check.
Pure policy comparisons and all 7 CTest groups pass, along with pre/post build
dependency audits, version agreement and app signature verification.
`/tmp/dunecity-710-ctest.log` contains the complete standard test result.

`build` points to self-contained `build-710` (1.0.710), ready for the next launch.
The live 708 match was not restarted or altered; no push or deployment performed.

## 2026-09-19 — Protect spice from city placement, local 1.0.709

Stefan requested that SimCity buildings cannot cover spice or spice blooms.
Preview and AI planning already reject these terrains, but `House::placeStructure`
only checked occupancy before creating/placing the object. A direct execution
test against 708 reproduced a residential zone placed over spice. The final
placement path now checks every city-only footprint tile before any mutation,
including roads/power lines, all thin/thick spice colours and ordinary/coloured/
special blooms. Even forced gameplay placement rejects resources. Authored
scenario placement is preserved; save loading and the existing anchored plain
sand zone rule are unchanged.

`run-campaign-balance.py --mod dunecity --city-placement-probe` covers 450
item/terrain/footprint combinations with normal and forced placement, confirms
rejected commands preserve resources/object counts, and exercises real human
and AI placement commands without consuming a finished zone or credits. Clearing
the bloom to sand permits placement and consumes the queue once. Plain-sand
R/C/I zones anchored on rock still succeed. Before/after evidence:
`/tmp/dunecity-spice-before/run.log`, `/tmp/dunecity-spice-after-final/run.log`.
All 7 CTest groups, clean native build, dependency audit, version agreement and
signature verification pass (`/tmp/dunecity-709-ctest.log`).
`build` now points to self-contained `build-709`; the running 708 app remains
in place. No restart, remote push, installation or public deployment performed.

## 2026-09-19 — Local build cleanup

At Stefan's request, moved ten obsolete build directories (`build`, `build-692`,
`build-705`, `build-706`, `build-707`, `build-web`, `build.bad`, `build2`,
`build_phase4`, `buildtests`) into
`~/.Trash/DuneCity-old-builds-20260919-015246`, with a restore-path manifest.
`build-708` is self-contained and was left in place, including its running app.
The usual `build` path is now a local ignored symlink to `build-708`. The stale
generated artifacts previously committed under four legacy build directories
are removed from tracking and explicitly ignored. Build scripts remain intact.
Dependency audit, app signature check and `ninja -C build -n` pass; no work is due.
Old historical build paths below refer to the builds as they existed at the time.

## 2026-09-19 — Preserve selected campaign AI partner, local 1.0.708

Stefan reported choosing full QuantBot while the running purple Sardaukar army
did not defend. Native 707 capture `1789745328908274-0` (SCENS022.INI,
seed 1180012387) identifies its controller as AI Support Brutal (`support=1`),
which intentionally does not command combat units. That runtime state is not
evidence that Stefan selected Support. The two preceding Neutral campaigns
in the same launch had full QuantBot Hard (`support=0`).

Reproduced a setup failure against the unmodified 707 objects: a list selection
within 200 ms of the previous click changed the chosen row but suppressed the
selection callback as a double click, even across different rows and in dropdowns
with no double-click action. Selecting full QuantBot could therefore leave the
campaign's cached partner set to Support. The original user's click sequence was
not captured; this is a verified failure path, not proof of that exact sequence.
ListBox now treats different-row clicks as selections and only invokes a real
double-click action on the same row. Campaign Start also reads the live partner,
enemy and level widgets before returning its setup. Support combat policy is unchanged.

Regression covers the real dropdown overlay, every partner choice, stale setup
at Start, rapid different-row clicks and ordinary same-row double-click activation.
The 707 reproduction fails with `Full AI selection left campaign launch set to
economy-only Support`; the 708 probe passes at 640, 854 and 1280 pixels. Clean
native build, 7/7 CTest groups, pre/post dependency checks, version consistency
and app signature verification pass. Logs: `/tmp/dunecity-708-{ctest,menu-after}.log`
and `/tmp/dunecity-708-overlay-before/run-640.log`.
Local app: `build-708/bin/dunecity.app`. Existing 707 match and binary remain;
no restart, installation, push or public deployment was performed.

## 2026-09-18 — Release 1.0.707 published

Stefan authorized rebuilding and deploying the commercial-demand fix remotely,
including the website. Version 1.0.707 contains the raw Micropolis commercial
projection change and regression tests below. PR #52 merged as `52fe7aca3df5818d7642f2786c2dbd5f96d55981`, tagged
`v1.0.707`. All PR checks passed (run 35236887818). Release run 35244762737
passed on attempt 2: first publication failed with a duplicate RPM upload;
rerunning only failed jobs reused all successful builds and published six assets.
SourceForge run 35256695885 verified uploaded checksums, all three OS defaults,
and source branch/tag; HTTPS ls-remote independently matched the release commit.
Browser run 35256695872 published website commit `6a6165c`; all seven live browser
artifact SHA256 values and manifest source/version were verified. The browser
reached the main menu showing v1.0.707. Website release copy is `d9f0320`, deployed
successfully by 35267823112; both live pages show six 707 links and the new text.
Local `build-707/bin/dunecity.app`: clean build, 7/7 CTest groups, dependency audits,
version and signature verification passed. Binary SHA256:
`ebdb406839b39ced66213c2615a6ed4cf98b145d152b773867b5b4d7ce444c23`.
No user game was restarted and no /Applications installation was created.
Verification files: `/tmp/dunecity-707-{ctest,sourceforge}.log`,
`/tmp/dunecity-707-live-artifacts.json`.

## 2026-09-18 — Commercial demand before the first shop

Running 1.0.706 session `1789655267563429-0` (seed 2098043535) logged
Harkonnen/house 0 commercial demand pinned to zero while commercial population
was zero, including an opening with ten industrial residents. The empty-C
branch forced its growth ratio to 1 regardless of the projected market.
The initial fix used `max(1.0, projectedComPop)`. Following source comparison
and Stefan's request for Micropolis behaviour, use raw `projectedComPop` as in
`../simcity/MicropolisCore/MicropolisEngine/src/simulate.cpp:648-652`. Markets
below one now reduce C demand, and growing markets raise it before the first
shop. Other population formulas, industrial safeguards, tax effects and civic
caps are unchanged; this restores the commercial zero-population branch, not
whole-simulation parity.
This is shared simulation logic, not a Harkonnen-specific modifier.

Regression tests cover the captured industrial opening, residential-only market,
accumulation, negative sub-unit projections, valve/ratio limits and high-tax
suppression. Numeric fixtures verify -275/-550 and +210/+600 market deltas. The complete
`dunelegacy_tests` CTest group and pre/post dependency checks passed in build-706.
Only the test executable was rebuilt; the running game binary was not changed
or restarted. Source fix is local; no release bump, push or deployment.

## 2026-09-17 — Restore funded city production, 1.0.706

Native 705 All against Atreides was still holding 84,862 credits at 6 minutes with
one heavy factory and one repair yard. Shared spending gated duplicate factories
on the city-yard target, displaced the old funded production order and could
cancel factories again after selection. Its worker shortcut also delayed normal
factory upgrades/MCVs. Rich city openings now use the established production
path when current cash and four-minute runway cover another line. Affordable
MCVs need not wait for opening workers in that case. Keep the dedicated R/I/C
yard; leave a legal factory and repair plot with access space clear of zoning.

Matched Dune City / seed 906213928 / Atreides Brutal versus legacy AI Player Hard:
700 survived 30 minutes with 65,600 army value at 15:14; 705 had 29,150 and lost 19:07;
706 had 79,570, 20 delivered heavy factories, 8 repair yards and 111 R/I/C plots,
then won 21:41. Army figures include queued units; fielded 706 value was 74,870.
This fixes the reported regression, not a claim about every map/seed.

7/7 CTest groups, city/vanilla engine fixtures, rich/poor opening fixture,
30-minute normal Dune City FFA, spending audits, SQLite import, dependency and
signature checks passed. Telemetry 16 / policy `funded-parallel-city-production-v72`.
Evidence/options/limits: `docs/quantbot-706-production-review.md`.
Local app: `build-706/bin/dunecity.app`; running 705 left untouched. No push/deploy.

## 2026-09-16 — Dedicate a construction yard to R/I/C, 1.0.705

Native 704 session `1789562162651301-0` was growing, but services took much of
the construction capacity. Harkonnen had about 1,439 free base-rock tiles and
18,424 spendable credits at 33.77 minutes. The preceding five minutes of planning
samples show roughly 41% of yard time zoning, 48% other construction and 10% idle.
704's protected plot budget did not bind the actual yard choice: it could spend
that allocation on police or a turret instead.

With multiple yards, 705 assigns the oldest usable yard to demanded R/I/C,
including its actual construction choice. Other yards handle services and
production. Idle yards also try an affordable legal plot when their selected
project cannot be afforded or placed, including at exactly 100 remaining credits.
Keep actual commitments, demand, placement and power checks. No new save state,
random personalities, difficulty settings or yard-count targets.

Validation: 7/7 CTest groups, city and vanilla real-engine spending fixtures,
dependency checks, version agreement and app signature pass. City fixtures cover
parallel crime protection, another yard already zoning, exact plot cash, negative
demand, blackout and no available site, plus existing military/MCV/transport tests.
Two completed same-seed 30-minute DuneCity matches pass the spending audit:
Harkonnen orders 150 -> 165 plots, and 52 -> 62 in minutes 25-30. This validates
increased construction, not general combat superiority. Telemetry 15 / policy
`dedicated-city-growth-v71` exposes assigned yard and accepted growth rules;
the SQLite view imports them. Full evidence: `docs/quantbot-705-growth-review.md`.

Local build: `build-705/bin/dunecity.app`. The user's running 704 app in `build-692`
was not replaced or stopped. No remote push or public deployment for this patch.

## 2026-09-16 — Fund continuous city growth and rich openings, 1.0.704

Native703 All against Atreides session1789556870788357-0, seed1293696382,
Atreides Brutal vs four legacy AI Player Hard, had97k cash and no heavy factory
at4min. Its empty CHOAM catalogue could never supply its Starport. Re-running
engine700/37e5772 on the same seed/options won34.70min with the former default60
worker limit: the limit alone did not explain703's loss. Rich openings now budget
the first heavy line's missing prerequisites plus four minutes of operation and
unlock it before extra refineries/port/repair when funded. Both ordinary Starport
paths check enabled CHOAM membership (zero stock is restockable, absent is not).

Default/-1 and explicit0 mean no engine harvester cap; positive Game Options
limits still apply. Ignore legacy map-derived caps stored on houses. QuantBot
custom targets come from remaining spice and economic planning; campaign enemy
and late-campaign helper planning policies remain AI decisions, not shared-house
engine restrictions. Explicit0 no longer incorrectly prevents economic imports.

Native703 DuneCity session1789558312233110-0, seed118157932, four Brutal houses,
showed Fremen with4 idle CYs, positive demand,1100 free base-rock tiles and an
affordable100-credit residential plot. Saving218 spendable credits toward a450
launcher withheld all construction cash. When military wins the raw score,
reserve one useful demanded R/C/I lot instead, then let factories spend the rest.
Services/real income bottlenecks keep their priority. No new timers/save fields.

Refinery pressure now counts loaded field returners to occupied bays before
they walk to within6 tiles. Offset only free unbooked bays; pending refineries
prevent repeated additions. Credit existing Carryalls' supported fleet share in
the delivery forecast. The queue relief branch now works in vanilla too.
Telemetry14/funded-growth-opening-v70 and SQLite expose protected city growth,
market eligibility, funded opening costs, blocked field returns and adjusted trips.

Final704 tests:7/7 CTest groups, pre/post Ninja dependency checks, codesign and
version metadata pass. Engine probes cover poor/rich openings, absent/sold-out/
disabled markets, default/explicit caps, distant loaded returns, city/vanilla
shared budgets and campaign pacing. Low-land-value218/1000-credit city fixtures
actually trigger protected growth; the latter also funds military production.
All against Atreides same seed wins20.17min with default unlimited and21.65min
with configured60 (old700:34.70min). First heavy ordered1.44min vs703's3.47min;
no Starport ordered. Configured60 run has a one-worker transient overshoot from
existing concurrent delivery/completion semantics; this patch does not change
that engine behaviour. Default run's peak delivered fleet is120.

City test uses current native roster Fremen1/Neutral2/Atreides3/Sardaukar4 and
seed118157932. Fremen orders61 lots +89 combat units in minutes30–35; native703
ordered0 lots +109 combat units then. It still loses at38.78min in this all-Brutal
FFA: proof of continued growth, not proof of optimal combat balance or a guaranteed
win. Three completed match captures pass the spending/link/overspend audit.
Follow-up budget/utilisation check: those61 lots cost6,100 versus31,450 in combat
orders (plus6,250 turrets/police and1,600 Carryalls). Original Fremen yards were
99.5% idle in minutes30–35; final704 has88.1% busy/11.1% idle/0.8% upgrading,
weighted by planning samples. Original failure was allocation, not insufficient
yard count. The lot count alone does not prove balanced spending; no additional
income-based yard-target change was implemented after this review.
Full evidence: docs/quantbot-704-growth-review.md. Native build:
build-692/bin/dunecity.app. No replacement of a running app or public deployment.

## 2026-09-16 — Restore city growth and use support queues/runway, 1.0.703

Reported native 702 session `1789545923897200-0`, DuneCity seed1105042893,
Fremen/team1 with three other Brutal houses, worker override100. At15.25min:
1CY,2R/0C/0I,4Light/2Heavy,25workers,4refineries,1Carryall,0police,1rocket;
tax22. Heavy/light factories never upgraded and no MCV was ordered. Compared
with700/37e5772 and701/c54a344: the new immediate military shortcut bypassed
upgrades, full-ceiling deficits exaggerated light demand, extra factories beat
zoning, MCV funding/idle-yard gates blocked construction throughput, and service
spending depended on leftovers. This was a real regression, not a screenshot
interpretation or evidence that trikes were unusually effective.

703 restores factory upgrades/MCV unlocking and funded composition before
ordinary military allocation; extra lights need a real deficit. City R/C/I can
use independent yards; demand plus usable rock and forecast working capital
justify saving one MCV price. Services reserve their price, and one yard upgrades
for defence while other yards continue building. Removed the extra half-cash
Starport worker cap; actual quotes, stock, useful workers and shared reserves
still constrain orders. First Carryall precedes bargain campaign workers too.

Cash runway includes available unit lines and demanded construction throughput.
Only delivered workers/refineries plus current tax supply forecast income;
spice is finite. Deduct already-reserved queues once. High cash can fund parallel
vanilla expansion despite falling cash: no fixed20k wealth switch. Exclude MCVs
from continuous military burn; treating every heavy factory as a permanent MCV
line falsely choked the100k All against Atreides opening in an intermediate test.

Support baselines: ceil(workers/5)+min(ceil(combatVehicles/20),2*repairYards)
Carryalls; max(workerFleet?1:0,ceil(combatVehicles/25)) repair yards; refineries
use delivery/unload throughput and the existing ten-second loaded-worker queue.
Two unserved jobs with all suppliers busy add one supplier, with pending capacity
preventing duplicates. Finished repairs and empty refinery workers awaiting a
return flight are transport pressure, not another repair/unload bay. No cap from
heavy-factory count. Medium vanilla's no-new-repair restriction remains; all city
difficulties can add them. Unmet transport demand also funds a legal first High
Tech Factory when imports are sold out/absent; an engine fixture covers this last
supplier gap discovered in the city simulation.

Telemetry13/city-capacity-recovery-v69, capital plan schema2, adds active burn,
sustained construction/unit cost, projected cash/runway, support queues/targets,
funded army basis and crime. SQLite capital_plans exposes these. All non-support
vanilla passes are logged, including cash-funded parallel mode. No save changes.

Built/signed local703: build-692/bin/dunecity.app. Seven CTest groups pass, Ninja
pre/post dependency checks and version metadata pass. Shared-spending/Starport
real-engine probes pass in both mods, including real queue prices and no duplicate
commitments. Full-match comparison on the runway/ratio implementation: All against
Atreides with100worker cap beats legacy AI Player Hard at31.21min (700:32.67;
702:lost20.72); city FFA vs AI Player Hard wins33.76min; vanilla campaign9 Atreides
Brutal helper vs Medium enemies wins20.55min. Four-Brutal reproduction loses48.88min
but has4CY/21zones/6Carryalls at15.25min and111zones at29.95min. Full matches precede
the last sold-out-transport factory fix. Final20min checks in
/tmp/dunecity-703-balanced-{city,current,shared} pass: reported Fremen has4CY,
29R/10C/4I,44workers,7refineries,12Carryalls (target12),5police,3rockets;
tax4018 versus original73. Atreides city test has4CY,11R/4C/0I and12Carryalls
(target12): transport is supplied but zoning is slower than the intermediate
under-supplied-transport run. City MCV priority5000 exceeds additional transport
4500; first Carryall5500 still wins. This is tested, not a claim of optimal tuning.
Completed captures have no accounting/link/overspend/truncation audit violations.

Test runner now supports legacy AI Player opponents and explicit house/team
rosters; it records those plus source revision at launch. Evidence and exact
comparison matrix: docs/quantbot-703-regression-review.md. Policy and telemetry:
docs/quantbot-spending.md. No install over /Applications, push, or public deployment
in this task. Existing gameplay remains separate from isolated test profiles.

## 2026-09-16 — Shared QuantBot spending and audit trail, 1.0.701

Implemented Stefan's common credit-allocation request across economy, units and
production expansion. Four-minute integer forecast ranks marginal spice/tax
receipts, military readiness/value per credit and funded capacity beyond existing
busy factories. Protect only the chosen next order; let independent queues spend
the remainder. Removed the fixed tax/spice hedge and alternating investment-window
gates. Finite-spice forecasts credit only receipts beyond the existing fleet;
Dune City includes demand/site-supported R/C/I and power/upkeep, vanilla excludes
tax/zones. Existing recovery, prerequisite, campaign helper/enemy and difficulty
rules remain. Extra construction retains rock/map/working-capital checks.

Central acceptance now charges all production/foundations/upgrades once against
uncommitted cash; roads report their aggregate charge. Paid imports are not
reserved again. A seeded campaign audit exposed Starport planning using a newer
CHOAM quote while the actual port queue charged its older displayed price
(plan4127:1880 charged against1782 spendable). Use the actual build-list offer
throughout comparison, affordability, bulk purchases and logging. Engine regression
with market80/displayed120 verifies350 buys two tanks with110 left.

Telemetry12/shared-capital-spending-v68 logs capital_plan inputs/options/reasons,
linked orders and capital_outcome, upgrades/roads/blocked orders, plus detailed
city comparisons. Capture-limit marker is explicit even when reserving terminal
summary space. New SQLite views and tests: capital_plans/candidates/orders/outcomes.
Report/audit: tests/ai/report-spending.py <events.jsonl> --output <report> --check.
Documentation: docs/quantbot-spending.md. No save-format changes.

Validation: native1.0.701 in build-692/bin/dunecity.app, pre/post Ninja checks,
version metadata consistency and7/7 CTests. Six real-engine probes pass across
vanilla/DuneCity: shared spending, Starport imports and lost-factory recovery.
The recovery fixture supplies prepared foundations so its exact600-credit test
continues to isolate building priority; foundation costs are now actually budgeted.
Focused results /tmp/dunecity-701-{final,verified}-<probe>-<mod>.
Additional multi-yard probe verifies two simultaneous city zone orders plus a
protected factory harvester (/tmp/dunecity-701-multi-yard).

Two15-minute seed701 simulations: vanilla campaign8/helperBrutal/enemyHard and
DuneCity two-house Twin Cities custom/Brutal-v-Hard. Logs and reports under
/tmp/dunecity-701-validated-match-{vanilla,dunecity}.3936 capital plans imported
into /tmp/dunecity-701-validation.sqlite; no ordinary overspend, missing links,
charge reconciliation errors or capture truncation. City houses added45/46 zones
(21R/9C/15I and20R/11C/15I) alongside armies and workers, continuing after spice
reached zero. Vanilla produced no zones. These are bounded simulation windows,
not a claim of final balance across all maps. Build/test logs /tmp/build-701-verified.log and
/tmp/ctest-701-verified.log.

Not installed, pushed or published in this task. Installed app and public browser
remain1.0.699; local source previous HEAD was700. Use this build for next playtest.

## 2026-09-16 — Restore heavy production before extra light factories, 1.0.700

Browser session1789523730176999-0 (vanilla SCENH022, seed158928782,
Harkonnen human/QuantBot) lost both light and heavy factories at602.416s.
Light replacements finished648,705.6,753.6s; heavy returned only900.8s.
Extra lights were selected by light_unit_backlog. The configured mix had
collapsed to100% quads because availability came only from surviving factories;
quad performance did not justify this (reward115.546 vs launchers1119.996).
Evidence exported to /tmp/harkonnen-1789523730176999-0.jsonl.

Keep recoverable heavy units in the strategic mix after a heavy factory loss,
using existing saved House loss counters plus enabled/tech/prerequisite gates.
Actual production still obeys normal factory upgrades and availability. Restore
one lost heavy line (including a missing light prerequisite) before optional
construction. Count pending buildings to avoid duplicate replacements; wait for
its purchase price only when a valid site exists. In normal openings, build one
heavy before expanding beyond one light factory, after existing opening economy
priorities. Low-tech/disabled-heavy missions retain light expansion. Optional
vanilla concrete now respects the planner's save/wait flag, matching roads.

Validation: native1.0.700 build, pre/post Ninja dependency checks, version check,
and7/7 CTests (/tmp/ctest-700-final.log). New real-engine factory-recovery probe
passes vanilla and city: simultaneous losses, preserved tank demand, light then
heavy recovery, insufficient/exact funds, pending duplicate prevention, busy
light production, and blocked placement. Results in
/tmp/dunecity-700-factory-{vanilla,dunecity}. Runner:
python3 tests/ai/run-campaign-balance.py --build-dir build-692 --output-dir <new-dir>
--level 9 --mod vanilla --house harkonnen --partner-difficulty brutal
--enemy-difficulty hard --seed 700 --minutes 1 --factory-recovery-probe

Built app: build-692/bin/dunecity.app. Not installed or published by this change;
the live browser/downloads remain1.0.699. No save-format changes.

## 2026-09-15 — Custom economy parity and local base defence, 1.0.699

Stefan clarified custom-game allies/opponents should share normal economy rules,
with military built alongside workers; aggressive full-fleet saving belongs to
campaign helpers. Seven-worker Brutal ceiling now applies only to campaign
enemies. Custom bots retain authored map/override/spice ceilings regardless of
human alliance. Full-fleet worker cash reserve and bargain bypass of spice
planning are campaign-helper-only. Custom Starport orders allocate at most half
available cash to economy while army value is below target, except emergency
first two workers; remaining money buys cheap troops. Existing factory capital
balance again applies without the campaign reserve overriding it.

Base defenders were released from defenceAssignments upon entering guard range,
allowing regroup to steal them after target loss; proportional response could
also leave nearby troops idle despite the base being attacked. Rescan visible
units actively firing at owned structures before regroup, using actual weapon
range (isInAttackRange instead reflects guard orders and always passes Hunt).
Scramble all eligible responders within12 tiles for such a base attack; preserve
assignments through arrival until target death/departure, reacquire targets, and
exclude assigned defenders from regroup/new offensive dispatch. Human orders,
saboteurs, explicit retreats and damaged units reserved for repair remain exempt.

Remote 698 PR build caught Tornie QuantBot Config checksum stale after693 tuning.
Updated only its hash in the existing manifest; integrity checks remain enabled.
No698 public release occurred. Release notes now cover693–699 together.

Validation: 7/7 CTests; pre/post dependency checks; native vanilla and city
Starport probes preserve campaign purchases and custom1800 buys5 workers plus9
tanks; native army probe verifies nearby regroup travel interrupted for base
attack, assignment retained/reacquired and Hunt/kiting unchanged. Native defence
and pacing probes pass. /tmp/dunecity-699-{imports,city-imports,army-final,defence,pacing};
/tmp/ctest-699-final.log. Local699 installed with698 backup at
/tmp/dunecity-before-699.app. Public release pending PR47 and stable tag builds.

## 2026-09-15 — SimCity starting defences and allied opening, 1.0.698

Added ten Rocket-Turrets per player to the bundled single-player
`2P - 192x192 - SimCity.ini`. Every construction yard, refinery, repair yard,
factory, House IX, outpost and nuclear plant now has at least two rocket turrets
within five tiles of its centre. Additions occupy unused rock, preserve roads,
existing buildings and units, and reinforce both starting cities equally.
The historical user-map packing script was not rerun; this is an additive edit
to the shipped scenario.

Human-allied campaign QuantBots enter normal development with attackTimer=0,
so their existing army threshold can authorize the first attack immediately.
Loaded legacy countdowns greater than60s are discarded instead of shortened to60s.
Normal60s repeat breaks remain. Enemy campaign trigger/opening logic, support
and Defend exclusions are unchanged. No save-format change.

Validation: 7/7 CTests, pre/post dependency checks, native pacing probe verifies
new and loaded allied opening removal plus retained60s repeat break; native
pressure probe verifies enemy reinforcement/opening gates remain enforced.
Artifacts /tmp/dunecity-698-{pacing,pressure}, /tmp/ctest-698.log. Map verification
checks20 additions only, unique IDs, unused rock and exact built-bundle copy.
Built in build-692 and installed /Applications/dunecity.app as1.0.698; previous
app backed up at /tmp/dunecity-before-698.app. No public push or deployment.

## 2026-09-15 — Keep artillery assaults engaged; campaign limits, 1.0.697

Stefan reported Atreides launchers returning home after Hunt in vanilla campaign
SCENA022, live696 session1789475830006137-0, seed249628688. Telemetry showed
repeated combat_kite events. Source confirmed two mode-reset causes: artillery
kiting explicitly set Area Guard, and UnitBase::doMove2Pos implicitly sets Hunt
to Guard. Losing the target then invoked base-only regrouping. A duplicate
artillery branch also kited away from buildings. This was not an attack budget
or harvester-cap issue.

Compared moveToOptimalSquadPosition with pre-4dfd9fa code (old army-centre/base
nearest choice). Restored army-centre regrouping while improving its inputs:
active, responsive ground fighters only; prefer Hunt members when an assault
exists, exclude retreating/badly damaged/noncombat units and saboteurs. Home
guards and workers no longer drag the assault centre backwards. Regroup uses
safe passable spread slots and retains path/command budgets; explicit Retreat
uses home. Regroup never overrides Hunt or human orders.

Short kiting is capped at two tiles before coordinate rounding, biased toward
the fighting army rather than base; restores Hunt after the forced move command.
Only approaching armed ground units trigger artillery kiting; removed duplicate
building-kiting path. Existing repair and base/harvester defence remain.

Friendly campaign QuantBots now schedule attacks every60s when ready (existing
readiness checks retained; retry15s when understrength). Conversion and loaded
helper countdowns are capped at60s. Enemy opening/wave timing is unchanged.
Brutal human-allied campaign houses get a default maximum20 harvesters on levels
8/9 (scenario20 onward), in both planning and engine checks, derived at runtime
so older saves apply it too. Explicit overrides win and spice still tapers the
normal target. Opposing Brutal maximum increased6 to7. Other tiers/earlier maps
unchanged. No save-format change.

Validation: 7/7 CTests (/tmp/ctest-697-final.log) and dependency checks. Native
army fixture proves Hunt survives dodge/arrival, ~2-tile movement, continued
building siege, assault-centre filtering, human-order protection; passed vanilla
and city. Pacing fixture verifies levels7/8/9, Hard vs Brutal, maximum20 vs spice
target9, override3, enemy7 and allied60s. Native pressure and defence fixtures
passed. Artifacts /tmp/dunecity-697-{army-final,army-city,pacing-final,pressure,defence}.
New regression available through run-campaign-balance.py --army-probe.

Built in build-692; local installation /Applications/dunecity.app, previous
version saved at /tmp/dunecity-before-697.app. No public push or web/download release.

## 2026-09-15 — Reserve opening cash for harvesters, 1.0.696

Stefan asked to prioritise early worker growth over military purchases after
695 Atreides reached 15 existing/queued workers only at 11.15 game minutes.
Session1789473383768916-0 (vanilla SCENA022, seed830102282) held a target15
until about19.5min; the spice taper was not the opening bottleneck. Starport
orders spent leftovers on cheap combat units while worker stock was exhausted,
and bought additional carryalls before the worker target was covered.

- Normal-development QuantBots now protect normal-price cash for the missing
  sustainable worker fleet, including the existing human-ally bargain exception.
  Pending workers count, so cash releases when orders cover the target.
- Military factories respect that reserve; harvester factories can spend it and
  prioritise workers while it is needed. Starport combat bargains use only cash
  above the worker reserve, including while workers are sold out or in transit.
- Additional Starport carryalls wait for the worker target; the first transport
  remains eligible. Existing construction/power/expansion rules are preserved.
- The reserve requires spice, a refinery and a worker producer, respects ground
  caps/explicit limits, and excludes campaign-scripted/support-only controllers.
  No timer or save-format change. Telemetry adds harvester_investment_reserve.

Validation: 7/7 CTests; pre/post dependency checks; native vanilla and city
Starport fixtures save800 during worker stockout, buy two workers at300 on
restock and keep200, then release cash for eight military bargains once the
fleet is covered. Existing nine-at180/fifteen-worker case still passes. A real
heavy factory spends one worker's protected cash on a harvester rather than
military/upgrades in both mods. Campaign helper fixture verifies paid cargo,
pending refinery workers and no duplicate capacity funding. Artifacts:
/tmp/dunecity-696-{imports-final,city-imports-final,helper}; /tmp/ctest-696.log.

Built in build-692. Local install is /Applications/dunecity.app; verify the
installed version before continuing. No public push or downloads/web release.

## 2026-09-15 — Allied harvester caps and radar power, 1.0.695

Stefan reported a shared Atreides Brutal helper ignoring 180-credit harvesters.
Live 694 session `1789471829089566-0`, vanilla SCENA022.INI, seed703750746,
62x62: telemetry confirmed `harvester_ai_limit=6`, `harvester_engine_limit=6`,
`harvester_target=6` on human house1. The six-worker difficulty restriction from
693 had been incorrectly applied to human helpers. Stefan clarified that this
is an ENEMY cap and classification must use alliance with human players.
Evidence excerpt: `../outputs/radar-allied-harvesters-695/evidence.json`.

- Extracted the existing human-team scan into `isAlliedWithHuman()`, shared by
  campaign enemy classification and harvester ceilings. Brutal's six-worker cap
  now excludes both human co-controllers and separate human-allied houses. Their
  configured/map/explicit ceiling applies (15 in the reported vanilla map).
- Campaign human-allied houses use normal economy development, including separate
  allied houses loaded with campaign mode. Classification uses actual controllers
  and team IDs, never local-player identity or the unreliable mixed-house AI flag.
- If harvesters are below normal price and spice remains, a human ally fills its
  permitted fleet in one affordable order instead of the ordinary remaining-spice
  target. Bargain workers precede the first carryall and military imports; cash,
  market stock, explicit overrides, engine cap, and pending workers are respected.
  Enemy targets remain difficulty-limited. Normal-price investment taper remains.
- Radar now directly requires an outpost and producedPower >= powerRequirement,
  independent of generic power exemptions and the rocket-turret power option.
  Previously vanilla hasPower() returned true despite the observed100/405 power
  deficit. The general power rules/turret option are unchanged. Radar transitions
  can reverse immediately if power is lost/restored mid-animation.

Validation: 7/7 CTests and dependency checks passed. Native Starport fixture bought
nine harvesters at180 for1620 with six already present, despite a stale target6,
filling a human-allied fleet of15 even with a carryall available. Pacing fixture
verified shared helper15, separate ally15/normal development, opposing Brutal6,
and lower explicit limits. Pressure/wave fixture passed. Native radar tests in
vanilla and Dune City cover deficit, exact equality, surplus, missing outpost,
shutdown, and interruption/reversal of activation. The initial test incorrectly
required an animation when an immediate completed-on state was also valid; fixed
that assertion and both mod tests passed. No save-format change.

Built in `build-692`; installed `/Applications/dunecity.app` 1.0.695 after verifying
the game was closed. Previous app at `/tmp/dunecity-before-695.app`. Signature and
version checked. No public push or downloads/web release in this change.

## 2026-09-15 — Starport-led QuantBot opening, 1.0.694

Stefan corrected the intended economy strategy: build four–five refineries,
then starport prerequisites/starport, repair support, and only then heavy/high-tech
production. Normal QuantBot construction now targets four refineries before its
first port, bounded by sustainable map workers. It walks the active mod's actual
prerequisites, counts pending buildings, saves for the next feasible step, and
uses the existing placement/foundation handling. Power recovery remains prior.
Missions below starport technology retain their early-tech progression; vanilla
Easy/Medium/Hard campaign enemies retain their authored rebuild lists.

Starports now purchase every available cheap combat type (including trikes/quads),
independent of factory mix targets, best relative market price first. Removed the
hardcoded four-type shortlist and the 2,000 spare-credit requirement. Needed
harvesters/carryalls and bargains can use generic reserved cash after already
committed construction costs; imports remain paid once, stock checked, queue
acceptance checked, and military-value bounded. Harvester imports are batched to
the useful worker target and engine ceiling, including existing/pending workers.
Brutal's six-worker ceiling is unchanged: four refinery workers leave two import
slots, five leave one. Market `Choam::isCheap` classification remains unchanged.

Dune City-specific requests:
- Removed the starport population gate; normal tech/prerequisites still apply.
- Every QuantBot difficulty may add a missing repair yard. Vanilla Medium remains
  replacement-only. Added repair-capacity fixture coverage for city Medium.
- After the first windtrap, a power order chooses an affordable, placeable nuclear
  plant rather than comparing spare windtrap sites/cost-per-current-power demand.
  Unavailable, unplaceable, or unaffordable nuclear falls back to wind power; the
  existing longer-term reactor saving policy still operates.

Validation: all seven CTest suites and dependency checks passed. Native fixtures
passed for vanilla/city Starport imports (eight discounted units across four types
for 800 credits), above-normal-price essential imports, five-worker bulk orders
with pending-worker accounting, vanilla repair replacement rules, city repair
addition, and nuclear ordering/temporary congestion/lost-site refunds/small-site
wind fallback. Nuclear fixture now starts with only a one-power deficit.

Isolated observer games (no human orders, seed486409243, Harkonnen campaign9):
vanilla Brutal helper ordered four refineries, port5.01min, repair7.65, heavy8.13,
high-tech9.92; first port order included two harvesters together. City Medium
ordered fourth refinery4.32, port4.69, repair9.47, heavy10.61. These are sequence
checks, not win-rate balance evidence. Campaign4 retained refinery/light/heavy
progression with no inaccessible-starport reservation. Diagnostics live in
`/tmp/dunecity-694-{imports,city-imports,workers,repair,city-repair2,nuclear,vanilla,city-medium,early}`.
The first city repair fixture needed free rock added to isolate policy from its
artificially crowded forced placements; the corrected fixture passed.

Built in `build-692` (historical directory name), installed as
`/Applications/dunecity.app` version1.0.694 after Stefan quit the game. No public
push, downloads, or website deployment in this change.

## 2026-09-15 — QuantBot difficulty balance, 1.0.693

Stefan requested the following after the 691 Atreides/Brutal-helper loss to
three Hard campaign enemies. Implemented:

- Brutal campaign military multiplier 4.0, readiness 25%. Brutal custom/shared
  helpers also use their difficulty threshold rather than the global 40%.
  Custom map-size military ceilings are unchanged.
- Medium automatic campaign waves use up to 2,500 credits; Hard uses up to
  3,500. Both are credit-based, without the old count/percentage ceilings.
  Each house waits independently 0–2 game minutes after the authored offensive
  reinforcement trigger and 1–3 minutes after each dispatch. Existing survivors
  do not reset/block the timer. Brutal still opens immediately at the trigger
  and launches again on readiness. Scripted reinforcements remain separate.
- Medium does not invent a repair-yard target when it starts without one.
  Stefan clarified that authored starting yards must still be replaced normally.
  The preserved initial count caps Medium additions, including queued yards;
  it can use existing yards. A missing authored yard/prerequisites is restored
  promptly. Damaged Medium troops without a yard remain eligible to fight.
- Hard/Brutal QuantBot houses have unlimited ground/infantry/air count capacity,
  including a human's shared house. House derives the effective limit from its
  controllers without overwriting the authored/saved cap. Easy/Medium/Defend
  and other-controller houses retain their caps. Monetary military limits remain.
- Brutal has a six-harvester ceiling in both planning and engine production,
  including shared houses. Lower scenario/lobby ceilings and spice reductions
  still win. Existing excess units are not destroyed. No save layout changed.

Built at build-692/bin/dunecity.app as 1.0.693 and installed to
/Applications/dunecity.app, verified version/signature. Updated only the two
Brutal numeric settings in existing local main/vanilla/dunecity/Tornie INIs;
backups /tmp/dunecity-693-config-backup. Previous installed app retained at
/tmp/dunecity-before-693.app. New campaigns initialize the new starting-value
multiplier; saved military ceilings and already-calculated openings persist.
No public push, release or website deployment in this task.

Validation: all seven CTest groups passed; dependency and version checks passed.
Real-engine pressure fixture covers independent openings, fixed wave budgets,
repeat timers with survivors, manual orders, saved bot state, restored low-tier
unit caps, high-tier infantry/air/ground exemptions and retained worker caps.
Pacing fixture covers six workers for Brutal enemies/helpers and lower overrides.
Repair fixture covers Medium zero-start refusal, authored replacement, queued
count protection and Hard repair establishment for both enemy/helper roles.
Evidence: /tmp/dunecity-693-final-{pressure,pacing}/,
/tmp/dunecity-693-replacement-repair/, /tmp/dunecity-693-release-ctest.log.

Natural native SCENA022, Atreides Brutal helper vs Hard, seed1599783965:
Hard dispatched 3,500 at12.01/12.23/12.27min; Ordos next3,500 at13.57 and
Harkonnen3,450 at14.21, within the independent1–3min gaps. All Hard/Brutal
snapshots show max_units0; helper worker limit6 and readiness10,000. The helper
still lost at14.82min, maximum sampled military4,040. These requested settings
work, but do not establish balanced play or solve the helper's military buildup.
This is an observer simulation with no human orders and concrete degradation on,
not a replay of Stefan's game. Evidence: /tmp/dunecity-693-natural-hard/.

## 2026-09-15 — Easy campaign pressure adjustment, 1.0.692

Stefan tested 691 and still found Harkonnen too passive with many launchers.
The exact process used build/bin/dunecity.app, session1789459263866226-0,
SCENA022, seed99480356. Harkonnen's timer was working: waves at 12.31,15.04,
17.40,21.04,23.76 game minutes each sent four units worth1,450. At17.40 an
older survivor remained (five active members after four new dispatches).
Snapshots around20min had six launchers and military3,900; no readiness
deferrals between20–24min. This is insufficient pressure under the existing
cap/cadence, not a repeat of the wave-survivor timer deadlock.

Stefan specified a 1–3-game-minute gap and corrected his requested late Easy
attack ceiling from2,000 to1,700 credits. Implemented those exact Easy values.
Early/middle Easy caps remain900/1,200 and late count cap remainsfive. Each
dispatch still spends at most50% of ready army value. Opening timing, Medium's
larger waves and2–4min gap, Hard/Brutal commitment, repair and defence policies
are unchanged. Uses existing serialized attackTimer; no new save state.

Built separately under build-692/bin/dunecity.app to preserve the app running
from build/bin. All seven CTest groups passed, as did the real-engine pressure
fixture (opening gates, repeat timer despite survivors, reserves, all four
tiers, save state). Version, dependency and app-signature checks passed.

Natural isolated native comparisons, full QuantBot Easy Atreides vs Easy,
Vanilla SCENA022 seed99480356, default harvester limit, no human orders:
691 won at31.37min; 692 won at29.39min. Enemy ground dispatches increased
from8 to9, dispatched value from10,600 to13,450, and total enemy raw damage
from11,281 to13,425. Harkonnen's first692 wave was1,700 credits at12.31min;
its second was1,550 at14.47min. Ordos/Sardaukar later launched fresh waves
with previous survivors still active. Every new Easy dispatch stayed within
five units/1,700 and its ready-army budget. The observer run uses concrete
degradation enabled, unlike Stefan's live game, and is not an exact replay.
These runs confirm increased pressure, not a conclusion about human balance.
A level4 regression naturally won at15.74min. No skip or forced victory.

The runner's postprocessing still rejected accumulated survivors above one
wave cap, contrary to691's explicitly requested overlapping-wave rule. It now
checks each dispatch's members/value and budget instead of aggregate survivors.
The first692 simulation finished normally but this stale check rejected its
report; rerunning after correction passed with identical cycle110200 result.
Evidence: /tmp/dunecity-691-live-seed-baseline/,
/tmp/dunecity-692-{live-seed-verified,level4,pressure}/,
/tmp/dunecity-692-ctest.log. Not installed or published.

## 2026-09-15 — Timed campaign waves and repair eligibility, 1.0.691

Stefan explicitly changed the Easy/Medium campaign rule: start the next-wave
countdown on dispatch, never wait for surviving attackers to finish. Each
successful dispatch now starts the existing deterministic 2–4 game-minute
attackTimer. Wave extinction no longer restarts it. Opening gates, readiness,
per-dispatch count/value limits, manual orders and saved-state layout remain.
Hard/Brutal retain readiness-driven attacks. This supersedes the 688 wave-end
cooldown described below.

Medium now prioritizes one missing repair yard (including prerequisites), as
Hard/Brutal already did, and can issue vehicle repair orders. Actual and queued
yards prevent duplicate priority orders. Easy campaign controllers without a
repair yard keep damaged combat units eligible for attacks and defence instead
of withdrawing/excluding them. Existing-yard repair and explicit retreat orders
remain protected; saboteurs stay independent.

Live 690 diagnosis: process 6279 wrote session 1789457442473833-0, SCENA022,
seed540497013. Atreides had actual support=1, named "Atreides (AI Support)".
This economy-only controller deliberately skips attack and defence commands;
the menu/factory mappings for full QuantBot Easy and AI Support Easy are correct.
Atreides military value was 9,590 against an 8,000 limit, explaining paused
military production despite abundant credits. Stefan will start a new game with
full QuantBot Easy. Harkonnen dispatched at 24.41min but had no wave-end/new
dispatch by 39.04min: 690 survivor gating held the next countdown. Sardaukar did
dispatch repeatedly, including 28.04, 33.11 and 38.08min. Late-map Easy's current
5-unit/1,500-value cap explains small expensive waves; this change does not
increase that cap. Live snapshots: ../outputs/campaign-690-live-diagnosis/.

Validation: all seven CTest groups passed; real-engine pressure fixture proves
another full wave dispatches after the timer with all first-wave units alive,
and casualties do not reset it. Defence fixture passed all four difficulties,
new Easy no-yard/with-yard transitions, manual retreat and saboteur isolation.
Medium repair fixture passed both helper/enemy roles, missing prerequisites and
exactly one queued yard. The fixture supplies sufficient power because Medium
correctly builds required windtraps first.

Independent natural native run: Vanilla Atreides level9, seed540497013, full
QuantBot Easy vs Easy, harvester limit100, no human commands or forced victory:
won at cycle117445 (31.32 game minutes). Atreides logged 13 ground dispatches,
10 defence responses and 89 production orders. Sardaukar dispatched at 13.32,
16.47 and 19.80min; enemies also recorded defence responses. This is a fresh
simulation, not a replay of Stefan's support-mode game; concrete degradation
was enabled in the simulation and disabled in the live session. One AI win is
not evidence of human difficulty balance.

Evidence: /tmp/dunecity-691-{pressure,defence,repair-medium-final,atreides-natural}/
and /tmp/dunecity-691-ctest.log. Dependency audits and native signature passed.
Built locally at build/bin/dunecity.app as 1.0.691; not installed or published.

## 2026-09-15 — Independent saboteurs, 1.0.690

Stefan's live 689 SCENH022 seed282721298 appeared cleared but victory did not
trigger. At 44min Ordos had no structures, two carryalls and one saboteur;
the saboteur legitimately kept House::isAlive true (carryalls do not). Logs
repeated its position (21,5), Area Guard, target assigned and forced order.
Campaign wave/defender control included saboteurs, while the normal Hunt
restoration skipped forced orders. Palace already spawns AI saboteurs in Hunt.

Stefan explicitly clarified: AI must leave saboteurs alone and never change
their orders. QuantBot now excludes them from campaign wave/holding/retaliation,
initial rally, ordinary attack selection, retreat, legacy squad conversion,
defence assignment maintenance and periodic tactical commands. Removed the
periodic Hunt setter and noisy saboteur diagnostic. No new timers/state or
victory-rule changes. Manual orders and saved orders remain untouched: this
prevents future corruption but does not rewrite an already stuck save's order.

The original engine regression reproduced the campaign override on 689.
Final defence fixture tests explicit forced orders preserved despite damage
and stale defence assignment; Palace-style Hunt survives pre-opening wave
control and a valid army retreat, then the real saboteur walks to and detonates
on an enemy launcher. Ordinary defence checks pass for all four difficulties.
All seven CTest groups and dependency/version/signature checks pass.
Evidence: /tmp/dunecity-690-saboteur-verified/, /tmp/dunecity-690-ctest.log;
live evidence ../outputs/dunecity-690-saboteur/.
Built and installed locally as 1.0.690 after the game exited; no remote release.

Separate outstanding Ordos economy finding (reported, not changed here):
its sole CY waited on an already-existing road (21,10) from ~2 to23.5min,
with 42 road_waiting_for_useful_gap events, then saved for a reactor until
34.44min. Only one refinery/harvester and no zones until35min. It lost its
combat army around26–28min and then waited for 3300 military readiness.
Road redirection must not indefinitely block the only yard when no gap exists;
reactor investment should not starve income recovery. These need a separate fix.

## 2026-09-14 — Hard co-op helper opening deadlock, 1.0.689

Live 688 Harkonnen human + Hard QuantBot helper, SCENH022 seed1221113892:
helper correctly used normal Custom logic, but its cramped starting rock
triggered an expansion-MCV reserve. With 1,000 credits, no factory, no power
and no income, reserving 900 left only 100 for a 300-credit first windtrap.
Repeated construction_rejected/committed_cash events explain the inactivity.
Evidence: ../outputs/dunecity-689-helper/live-688.json.

Only reserve expansion-MCV cash when an existing living heavy factory can
actually build an MCV. Uses the existing producer scan; no new timers, saved
state or changes to the attack/economy policy. Expansion production and its
upgrade path remain intact.

Extended the existing helper-economy engine fixture with this city opening:
it fails on 688 and passes on 689, requiring the first windtrap and power,
refinery and harvester within five game minutes. An independent ten-minute
normal same-map/seed run ordered three refineries, additional harvesters,
R/C/I, both factories, a high-tech factory, a carryall and military units.
Last snapshot (9.63min): three refineries, four harvesters, 5R/1C/1I;
5,665 spice credits refined, 2,804 city credits collected. This is an isolated
observer reproduction, not an exact replay of human commands.

All seven CTest groups and dependency/version/signature checks pass.
Evidence: /tmp/dunecity-689-helper-before/, /tmp/dunecity-689-helper-after/,
/tmp/dunecity-689-helper-natural/, /tmp/dunecity-689-ctest.log.
Built and installed locally as 1.0.689 after the running game exited.
No remote release, website update or save-format change.

## 2026-09-14 — Easy/Medium one wave then a break, 1.0.688

Stefan reported a large Easy Atreides attack and explicitly clarified one wave
at a time, then a break. Live 686 SCENH022 seed1294169503 sent 4/3/3 units in
about two seconds at 13.89–13.92 minutes: 10 active attackers/4,350 credits.
Each dispatch fit its cap, but the separate waves accumulated.

Easy/Medium now wait for their active automatic wave to end, then wait an
independent deterministic 2–4 game minutes using the existing serialized
attackTimer. Readiness is still required after that break. No survivor-budget
subtraction or new state. Hard/Brutal retain readiness-driven larger attacks
after the same map opening. Scripted arrivals remain separate. Includes 687's
city campaign construction fix. This supersedes 686's overlapping small waves
and removal of repeat cooldowns for Easy/Medium.

All seven CTest groups, dependency/signature/version checks, and the actual
engine pressure fixture pass (no stacking even with ready replacements,
break boundaries and preserved countdown). Same-map/seed Easy Dune City
observer run: Atreides sent4/1500 at13.89min, wave ended15.32min, break127.792s;
Ordos4/1400; Sardaukar3/1500 then another3/1500 only after a194.080s break.
City zoning continued for all three. Observer naturally lost at17.31min;
this is not a human difficulty assessment. Evidence `/tmp/dunecity-688-easy/`,
`/tmp/dunecity-688-pressure/`, `/tmp/dunecity-688-ctest.log`.
Local test build only; no remote release or website change.

## 2026-09-14 — Dune City campaign construction routing, 1.0.687

Live 686 SCENH022.INI, seed 1294169503, Easy enemies: Atreides, Ordos and
Sardaukar had no R/C/I zones despite positive residential demand. Classic
campaign rebuilding/upgrades took precedence, power deficits blocked the
last-resort zoning branch, Ordos spent its funds on a reactor, and Sardaukar
could not place a completed windtrap. Evidence preserved in
`../outputs/dunecity-687-city-campaign/live-686-evidence.json`.

One-condition fix: the classic campaign Construction Yard branch now requires
city simulation to be disabled. City campaigns use the existing city planner,
including its power investment and income priorities. Campaign mode, army
limits, attack timing/readiness and worker limits are unchanged. No new policy.

A ten-minute isolated observer run on the same map/seed confirms actual zone
construction: last sampled Atreides 6R/2C/1I, Ordos 3R/1C (I ordered at 9.93min),
Sardaukar 13R/4C/5I. The observer has an AI helper, so this verifies behavior,
not exact replay equivalence to Stefan's ongoing match. Evidence:
`/tmp/dunecity-687-city-campaign/summary.json`. Native dependency audit,
version/signature checks and all seven CTest groups pass. Built 687 locally;
the running/installed 686 app remains untouched. No remote publication.

## 2026-09-14 — Campaign readiness and separate waves, 1.0.686

Stefan clarified that surviving attackers occupy military capacity, not the
next wave's budget. This supersedes 685's concurrent-pressure interpretation.
After the unchanged map opening, campaign enemies assemble available troops
worth the configured fraction of their military limit (Easy 50%, Medium 40%,
Hard/Brutal 30%). Busy, injured, repairing, manual, scripted and already
committed troops cannot satisfy that fresh-wave check. Dispatch sends up to
50/50/80/100% of those available troops, with existing Easy/Medium count/value
ceilings applied per dispatch. Survivors retain their orders and membership;
no repeat cooldown or survivor subtraction. Military production caps are
unchanged. Removed the old readiness clamp to twice the wave value. The
existing post-15-minute depleted-spice fallback remains. Save format 9839.

Native build, signature/version/dependency checks and all seven CTest groups
pass. Real-engine pressure and pacing probes pass: Easy/Medium assemble a
second complete wave while every old attacker survives, insufficient ready
reserves do not trickle out, Hard sends 32/40 tanks and Brutal 40/40, opening
and saved-state handling remain intact. Evidence: `/tmp/dunecity-686-pressure/`,
`/tmp/dunecity-686-pacing/`, `/tmp/dunecity-686-ctest.log`. The pressure fixture
must run in vanilla: its unrelated helper power test assumes classic production;
a Dune City trial passed the combat assertions but failed that power fixture.
Built for local testing; no remote release or website changes.

Engineering preference reiterated by Stefan: keep behavior simple; avoid extra
accounting layers when a straightforward readiness-and-dispatch rule suffices.

## 2026-09-14 — Campaign attack capacity candidate 1.0.685

Stefan approved capacity-driven attacks after the opening. This supersedes
684's recurring 2–4 minute timers and requirement that the old wave disappear.
Campaign enemy houses retain the exact 684 opening signals/delays, then check
available capacity on ordinary AI updates (50 cycles, about 0.8 game seconds).
Surviving automatic attackers count against both the per-house count/value cap
and the army commitment budget: Easy/Medium 50%, Hard 80%, Brutal 100%.
New candidates exclude already committed units; repeated checks cannot
gradually spend the reserve. Scripted arrivals remain separate. The existing
last-unit Hard/Brutal fallback is allowed only when no automatic attackers
remain. Injured, repairing and manually controlled units stay protected.

Campaign enemy dispatch no longer waits for the old aggregate army-readiness
threshold once its opening is met. Helpers and custom-game cadence/readiness
are unchanged. Old saved repeat cooldowns are cleared after their stored
opening; save format remains 9839. No additional serialization was needed.

Native dependency/version checks and all seven CTest groups pass. The actual
engine pressure fixture verifies same-cycle Easy/Medium loss replacement with
survivors, ten repeated full-budget checks, 32/40 Hard versus 40/40 Brutal tank
commitment, a newly ready Brutal unit joining an existing assault, old-cooldown
discard, opening enforcement, scripted cargo and save/load. The simulation
driver now checks total active count/value and commitment, not only the latest
dispatch. Evidence: `/tmp/dunecity-685-budget-probe/summary.json` and
`/tmp/dunecity-685-ctest.log`. Built locally; not installed or published.
The level-9 Dune City Easy simulation also passes total-active budget checks:
Ordos made 9 dispatches (8 additions with survivors), Harkonnen 5 (4 additions),
Sardaukar 2 (1 addition). Ordos never exceeded 4 active units / 1,500 credits;
Harkonnen 3 / 1,450; Sardaukar 2 / 1,050. Openings remained
12.039/13.667/13.679 minutes. Observer run ended naturally at 15.487 minutes;
this is not a human difficulty/win-rate measurement. Evidence is under
`/tmp/dunecity-685-easy-budget-run/`.

## 2026-09-14 — Campaign attack candidate 1.0.684 (not installed or published)

Branch `fix/campaign-active-attacks` follows installed 1.0.683. Easy's military
cap remains 200% of starting combat credit value. Automatic campaign attacks
now use each enemy house's own wave budget and timer; allies do not take turns.
Opening is anchored to the map's first offensive allied-enemy reinforcement:
Easy/Medium delay 0–2 game minutes, Hard/Brutal delay zero. No tier launches
before the trigger. Successful launches schedule a separate 2–4 minute timer
per house; another wave waits for the existing wave to finish. Fighting/moving
survivors are not recalled merely because the old sortie clock expired.

Offensive reinforcement cargo is registered separately from automatic waves,
including while carried, and keeps assault intent after landing. Ordinary
Carryall combat drops use AREAGUARD, not HUNT, in this source; QuantBot must
explicitly recognize them. Home/economic deliveries are excluded. Save format
9839 persists this separate roster. Initialized 9838 saves retain their stored
opening because fired triggers no longer exist in TriggerManager; older saves
cannot retroactively identify already-landed scripted troops. New campaigns
receive the new opening timing.

Nuclear orders require a fresh valid site and reserve it on acceptance. A
finished reactor with no remaining geometric footprint is cancelled/refunded
through the normal API, allowing a smaller windtrap next. Temporary unit
blockage retains the reactor. Real-engine fixtures cover reservation, temporary
blockage, lost footprint/refund and windtrap fallback.

`tests/ai/audit-campaign-triggers.py` audits the canonical SCENARIO.PAK used by
both vanilla and Dune City: 66 maps, 54 with offensive reinforcements, 12 with
none (SCEN[A/H/O]001–004, levels 1–2). Level 3 starts at 5 minutes, levels 4–5
at 11, levels 6–9 at 12. Loose scenario INIs are separate Tornie content.
Stefan requested minutes 4–6 for the early maps. Their existing troops receive
an explicit four-minute opening signal: Easy/Medium add their usual 0–2 minute
delay; Hard/Brutal start at four. No extra units are granted. The fallback is
restricted to scenarios 001–004 in vanilla/Dune City and only applies when no
offensive reinforcement exists. Unknown missing triggers remain disabled.
The engine fixture covers all 12 maps, both mods and all four difficulties.

Native dependency audit and all seven CTest groups pass. Engine pressure,
pacing and nuclear probes pass, including saved scripted cargo, immediate
Hard/Brutal trigger boundaries, independent timers and active combat retention.
Level-9 Dune City Easy simulation opened at 12.039/13.667/13.679 game minutes
after the 12-minute trigger; vanilla Hard opened at 12.012–12.013 (AI tick).
Repeat intervals were 136570–238795 ms across those runs. These isolated
observer simulations are not human win-rate measurements. Evidence is under
`/tmp/dunecity-684-*`; the 66-map audit is retained in
`../outputs/dunecity-campaign-684/campaign-triggers.{json,md}`.
The final early-level-2 Dune City Easy run dispatched three units at 4.106 game
minutes. The final pressure probe also verifies 9838 opening preservation and
all 12 early maps across both mods/four tiers. Its driver excludes its deliberate
Hard/Brutal tier switches from the CLI Easy-only postcheck; the engine fixture
asserts each actual tier's budget. Final seven-group CTest run and dependency
audit pass. No installed app, active game, public build or website was changed.

## 2026-09-14 — Cursor rendering candidate 1.0.683

Desktop and browser now draw the original game cursor sprites as the last
frame overlay in every menu, briefing/cutscene, editor and gameplay frame.
Normal, move, attack, heal, capture and carryall-drop shapes are preserved.
Focus and visibility are re-evaluated on every presentation; Android retains
its existing touch/physical-pointer policy. Window coordinates are converted
to drawable pixels independently of scene zoom, letterboxing and clip state.

Auto uses original 1x size in window units, with explicit 1x–4x preferences
retained. Physical monitor DPI must not enlarge this again: the former 224-DPI
Mac default selected 3x and made the candidate cursor visibly too large.
The overlay already handles Retina/backing density once, equally on desktop
and web. Pixel-readback tests cover 100/125/150/200% backing density, all four
explicit scales, clipped/letterboxed content and renderer-state restoration.
Native SDL2 and sdl2-compat expose different explicit/logical scale behavior;
restoration handles both without multiplying the scene scale each frame.

Native and Emscripten builds and dependency audits pass; all seven native
CTest groups pass, including the new cursor readback cases and menu probes.
Browser package audit preserves Tornie's 769 files and Dune2R's six files.
Browser interaction verified main menu, campaign selection, briefing/letterbox,
normal/move/attack gameplay pointers, canvas leave/re-entry, and modal arrow
restoration (pause menu uses the arrow; closing it restores the attack cursor).
Native menu and Settings screenshots show the original-sized arrow. Installed
the final signed build in /Applications/dunecity.app only after checking the
installed app was closed; previous app is preserved under the task's
outputs/dunecity-menu-acceptance/cursor-683-small/previous-installed.app.
All seven CTest groups passed again after the modal correction. Native Windows
UI has not been exercised; the density regression tests run the shared renderer,
not Windows automation. Source is committed locally; no public 683 release yet.

## 2026-09-14 — Campaign AI release 1.0.682 published

PR #33 merged/tagged at d2dc426. Stable CI 34764559810 passed all platform builds
and tests; six downloaded GitHub packages match their published hashes.
SourceForge 34765728864 verified upload readbacks, dedicated source refs and
all three OS defaults. Website 9c82c5f publishes the exact stable browser artifact;
Deploy 34765821210, Web security 34765821206 and CodeQL 34765821213 passed.
Public manifest and all seven file hashes verify version 682/source d2dc426.
Combined campaign/map-browser release copy and the co-op guide are retained.

71 native scenarios finished naturally (46 wins, 25 losses): zero crashes,
no worker-cap overshoots, every enemy launched an attack, and all 21 advanced
level-9 helpers reached 15 workers. This is not a human win-rate assessment.
The tagged browser artifact also won Atreides level 4 Easy/Easy naturally in
15 game minutes, score 446; player/enemy kills 54/18. The player's house receives
the statistics. Earlier combined-build level-1 browser victory also verified.

Full evidence and publication links: [campaign-ai-release-682.md](docs/campaign-ai-release-682.md).
The subsequent Discord signaling work in PR #37 is server-only; preserve that
newer service on future browser publications. No installed app was replaced.

## 2026-09-14 — Direct-P2P Discord announcements repaired (server-only)

Custom lobbies bypass the legacy metaserver, which was the only Discord notification
path. The signaling service now calls a trusted local deployment hook after host
seating and match start, with a bounded snapshot that excludes all invitation and
transport credentials. Retries/recovered seating do not duplicate events; hook
errors cannot break hosting. The website owns delivery using the existing webhook.
Stefan requested both public and private announcements, omitting private codes.
166 real-HTTP signaling tests pass, including custom/coop, public/private,
recovery/start deduplication and notification failure isolation. No client protocol,
C++ source, simulation or browser binary changes; server infrastructure is deployed
separately from game releases. Publication is recorded in the website deployment.

## 2026-09-14 — Combined release candidate renumbered 1.0.682

Main advanced to 72ef00f (1.0.681 restored Custom Game map browser) while
campaign PR #33 was testing. Integrated that change and bumped to 1.0.682.
The campaign engine/AI is unchanged from the 71-scenario 680 validation.
Rebuilding native/browser and rerunning the combined menu tests before merge.
The separate 1.0.681 publication is not cancelled.

## Original custom map browser restored — 1.0.681 published, 14 September 2026

Stefan correctly reported that restoring the classic player layout had left
custom map selection reduced to a dropdown. The original CustomGameMenu was
still present but bypassed by playCustomGame. The unified custom flow now starts
with that scrolling map list, five original categories, preview, metadata and
mod/rule controls. Offline/Online and visibility are available before Players.
The existing classic player setup, explicit open-seat hosting and direct-P2P
lobby flow are preserved. Back, Escape and Browse Maps return to map selection
with the roster retained; choosing a different map/mod resets incompatible seats.
The current selected map is restored, falling back to All Maps if a quick player
screen selection is outside the previous category. Mod defaults update before
editing Game Rules. Uppercase map extensions resolve through the legacy helper.

The native probe now checks map-browser presence and rendering at 640×480,
854×480 and 1280×720, both connection modes, selected-map preservation and the
roster round trip. All seven CTest groups pass. The final isolated browser build passes dependency and bundled-mod audits.
Interactive checks confirm Custom Game opens the full browser, MP Maps filters
the list, preview follows selection, Online carries into Players, and Browse
Maps/Escape preserve the selected map and Atreides roster choice.

PR 34 merged as 72ef00f4e10509c86080ab2c0627e47e1748a6ca, tagged v1.0.681.
Stable release 34763123495 passed all tests and Windows/Linux/macOS/Emscripten
builds. All six GitHub packages were downloaded and verified. The CI Mac DMG
reports 681, passes strict signature verification and includes both mods.
SourceForge run 34764258867 verified all uploads, source branch/tag and three
platform defaults. The exact stable-run browser artifact was packaged with
production signaling; redundant browser rebuild 34764258860 was cancelled.

Website 154366c84ad94fb1c39eb29f9ddc4a64595e7e2b publishes that package and the
restored-map-browser guide. Deploy 34764315624, Web security 34764315629 and
CodeQL 34764315627 passed. Live build.json reports 681/source 72ef00f; all seven
artifact hashes match. Live payload verification confirms 769 Tornie entries,
six Dune2R files and 38,455,774 data bytes. Signaling health is status=ok/protocol=1.
The public browser opens the original map list; Online -> Players -> Create Lobby
successfully creates a private lobby with the chosen Habbanya-Penny map, preview
and invite code. Leaving that test lobby and Browse Maps retain the selection.
The exact CI browser package also starts an offline Habbanya-Sammy custom game.

Evidence: ../outputs/dunecity-menu-acceptance/live-681/verification.json,
release-681-assets, sourceforge-681.log and ci-681-macos-verification.json.
Different-network two-device multiplayer and multiplayer save/resume remain
unverified in this release pass. No human game was interrupted or installed app
replaced. The independent campaign work is proceeding as 1.0.682; it incorporates
this fix and must preserve the map-browser guide when publishing its website.

## 2026-09-14 — 1.0.680 validation complete; publication in progress

71 native scenarios resolved naturally: 46 wins, 25 losses, no crash/timeout.
All enemies attacked; all 21 advanced level-9 helpers reached 15 workers; zero
over-cap cases (679 had eight). Seven CTest groups and eight real-engine
behavioral fixtures pass. Browser build, bundled mods and 18 shell/packaging
checks pass; actual level-9 launch and selection after feedback pass.
See [680 validation](docs/campaign-ai-validation-680.md) for evidence and limits.
PR #33 is undergoing release checks. No production completion claimed yet.

## 1.0.679 published and verified — 14 September 2026 (Sydney)

The missing browser mods fix and menu release are live. PR 31 merged as
49d58edeb3e0710188305eb754c15843d856d21b, tagged v1.0.679. Stable release CI
34760679530 passed version checks, relay/signaling tests, native tests and
Windows/Linux/macOS/Emscripten builds. All six GitHub packages were downloaded
and verified against their release metadata. The Mac DMG has version 1.0.679,
a valid strict signature, both bundled mods and all 769 Tornie checksum files.

SourceForge run 34761805560 verified every uploaded checksum, published
`dunecity-v1.0.679`, advanced `dunecity` from b7db719 to 49d58ed and confirmed
Windows/macOS/Linux defaults. The original v1.0.674 tag remains immutable and
unpublished after cancellation; 679 supersedes it.

To avoid an unnecessary browser rebuild, the successful stable run's exact
DuneCity-Emscripten artifact was validated and packaged with production signaling
origin https://dunelegacy.com. Automatic browser rebuild 34761805565 was cancelled
before publication. Website commit d38c374 includes that package, matching private
service SOURCE.json, 679 download references and the revised menu/co-op guide.
Deploy to Droplet 34761923903 and Web security passed. Live build.json reports
version 1.0.679 and source 49d58ed; all seven downloaded browser artifact hashes
match. Live payload validation verifies 769 Tornie entries, six Dune2R files and
38,455,774 data bytes. The public browser Mods screen visibly lists Dune City,
Vanilla, Tornie and Dune2R. /p2p/v1/health reports status=ok, protocol=1.

Local fresh and upgraded browser profiles both have all four mods. Tornie and
Dune2R activation survive reload; Tornie custom reaches gameplay. Different-network
two-device multiplayer and multiplayer save/resume remain unverified as previously
disclosed and accepted by Stefan. No running human test game was interrupted or
local installed application replaced. The local native build is 679; the separate
running 673 guest copy was preserved.

Verification artifacts are in ../outputs/dunecity-menu-acceptance: live-679/
verification.json, release-679-assets, sourceforge-679.log, ctest-679.log and
ci-679-macos-verification.json. This section supersedes the publication-pending
statements in the historical entries below.

## 2026-09-14 — Integrated campaign release candidate 1.0.680

Merged main 49d58ed (menu navigation and reproducible browser/mod packaging)
with the campaign AI changes. Worker planning now reserves pending refinery
workers in every mode and only after a successful construction order. New
refineries wait when paid/queued workers already fill the engine cap, until
those workers arrive. This preserves paid deliveries and capacity expansion.
Validation and publication are in progress; see the preceding 679 assessment
for the eight over-cap scenarios being rerun.

## 2026-09-14 — Native release assessment, local AI 1.0.679

Tested clean source `552a59f`: 71 scenarios (59 matrix +12 other-house level-9
cases), 48 wins/22 losses/one 60-minute cutoff. Same-binary extended replay of
Ordos9 Brutal/Hard seed1 ended naturally in defeat at85.44 game minutes; all71
scenarios therefore resolved, no crashes. All13 Easy/Easy won, all6 Medium/Medium
won; every enemy attacked. All21 advanced-helper level9 cases reached >=15workers.
Eight cases overshot to16: pending refinery-supplied workers are missing from
vanilla AI commitment counts and can overlap factory/paid Starport arrivals.
Fix planning, not paid delivery or existing units. Native checks/fixtures pass.
Fetched main `49d58ed` has separate menu/browser changes ALSO numbered1.0.679;
read-only merge assessment reports HANDOVER conflict only. Need worker fix,
integration, new version and web validation before production. No code changes,
merge or push this turn. Full evidence and ratios:
[release assessment](docs/campaign-ai-release-readiness-679.md).

## 2026-09-13 — Advanced helper economy, 1.0.679

Stefan closed his level-9 game before inspection. Native reproduction found a
15-worker-cap Brutal helper reducing its target to six with 41k map spice left.
`83f6264` makes Hard/Brutal vanilla campaign helpers invest toward their allowed
capacity without dividing spice equally with enemies; prioritizes their Starport
and factory workers; fixes all-role double reservation of already-paid Starport
cargo. Enemy worker limits and Easy/Medium investment unchanged. Native build,
audits/signature, six CTest targets and final helper/Starport/pacing fixtures pass.
Six complete level-9 matches all reached 15 workers at 10.64–11.65 game minutes;
five defeats, one win show economy alone does not fix combat balance. Source,
ratios, comparison and fixture caveats: [679 validation](docs/campaign-ai-validation-679.md).
No push or deployment; browser unchanged. Readiness/ineffective routes remain open.

## 2026-09-13 — Hard/Brutal campaign commitment, 1.0.678

Stefan requested most-in Hard and all-in Brutal enemies. `063a7f5` implements
80%/100% commitment without count/value caps for those tiers, retaining two/all
simultaneous houses respectively. Easy/Medium and human-side helpers unchanged.
Native build, audits, signature, six CTest targets and pressure/defense/pacing
fixtures pass. Four 40-tank armies verify Hard sends 32 each from two houses,
Brutal 40 each from all four; manual/repair/damage exclusions remain effective.
Harness commit `4d77832` updates the old unconditional live cap assertion.
Six final native level-5/8/9 matches (Hard helper, Hard/Brutal enemy, seed 42)
completed: five player defeats, one victory (level 8 Hard), no timeouts. This is
much stronger pressure, not a finished balance claim. See
[678 validation](docs/campaign-ai-validation-678.md). No push/deployment; browser
remains unchanged. Helper readiness and ineffective attack routes remain open.

## 2026-09-13 — Native campaign resistance validation, 1.0.677

Local branch `fix/campaign-ai-attack-limits`, game commit `0861fba`, diagnostic snapshot `a6c54ef`. Added Hard/Brutal repair-yard and prerequisite priority for both roles, removed the repair-yard attack prerequisite, retired army readiness goals after spice exhaustion, and gave idle dispatched campaign attackers real scouting/visible-base reacquisition. Human orders, home defenders and shared enemy wave limits remain protected.

Final native matrix: 59 real matches, 49 player wins, 10 losses, no timeouts. Six CTest targets, dependency audit, signature and five native fixture invocations passed. Detailed ratios, checkpoints, reproduction and limitations: [campaign-ai-validation-677](docs/campaign-ai-validation-677.md). Two individual enemy no-damage spells remain; early difficulty overlap and inconsistent Brutal-helper performance mean this is not a claim of finished human balance. The browser remains on validated 672; 677 is committed/built locally only, with no push or deployment.

## Campaign defensive reserve refinement — 1.0.672 local, 13 September 2026

Stefan refined readiness: a level-4 Easy wave may be four 300-value units, so
2,400 army value should permit four attackers and four defenders. Readiness is
now min(legacy threshold, twice the shared wave value cap). Easy/Medium campaign
enemy selection uses at most 50 percent army value, still within shared count/
value caps, and does not use the depleted-army exception that could send its last
unit. Hard/Brutal retain configured fractions and their previous exception.
Zero configured attack fraction still disables dispatch. The 671 worker-limit
fix and 670 Area Guard/defensive response fixes remain included.

The actual eight-tank fixture passes: 2,399 is below readiness, 2,400 sends exactly
four tanks, opening is respected, and repeated checks cannot top up the wave.
The earlier 671 batch was interrupted on this refinement; do not describe its
partial results as final-672 validation. The 671 web optimizer was also stopped.
Final native defense/pressure/pacing fixtures pass, as do all six CTest targets,
dependency audit and native signature verification. The clean-source `27f14ff`
matrix completed 22 matches: 17 wins, four 60-minute time limits, one defeat.
Easy/Easy levels 4,5,9 and Hard/Easy levels 8,9 won both seeds (1 and 42).
Brutal/Hard levels 6 and 7 won both seeds; level 8 won seed 1 but timed out seed
42; level 9 timed out both. Medium/Medium level 9 seed 42 won, Hard/Hard timed
out, Brutal/Brutal lost. The reported Atreides level-4 settings reproduced in
ordinary campaign won at 15.94 minutes; Easy bought no extra workers and sent
four troops at 13.00 minutes. This is not a co-op transport reproduction.

The late time limits expose an unchanged human-partner readiness problem:
Brutal can wait for 16,000 army value after spice exhaustion (level 8 seed 42
had 15,080 and no enemy combat army left), Hard for 8,000 (7,950 remaining in
Hard/Hard level 9). These are not proof of healthy stalemates or human balance.
See `docs/campaign-ai-validation-672.md` and the full evidence under
`/tmp/dunecity-campaign-balance/672-validation`.

Fresh visible browser Harkonnen level 4, seed 1701707512, Easy/Easy completed
normally with victory briefing and score screen: 427 points, 16 minutes displayed.
No Skip or player combat/economy commands; maximum speed 4. Ordos sent one
four-unit/550-value wave at cycle 48,798 (13.01 minutes), recorded 24 defense
responses and 44 retaliations, and never exceeded one harvester in snapshots.
Last telemetry cycle is 59,270; do not call this the exact victory cycle (the
score screen remains open, before final game-summary teardown). Browser tab 12
is preserved on the score screen at `http://127.0.0.1:18766/play/`.

The local web build uses Release C++ objects with link override
`-O2 -sBINARYEN_EXTRA_PASSES=--no-stack-ir`; all seven served hashes match the
672 manifest, packaging commit `63e166f` (docs only after game-source `27f14ff`).
The stock O3 final StackIR pass was cancelled after over 30 wall minutes; an O1
experiment crashed browser startup and was replaced. The O2 artifact opens in
both Chrome and the in-app browser and completed the above mission. This does
not validate the cancelled stock O3 artifact. Details are in the validation doc.
No public push or deployment.

## Campaign worker caps and small-wave readiness — 1.0.671 local, 13 September 2026

Stefan reported excess enemy workers and no attacks in a closed match. Latest
local telemetry was Atreides level 4 SCENA008.INI, seed 1237721204, co-op campaign,
Medium partner/Easy Harkonnen enemy, harvester override 100. Harkonnen bought seven
workers, had six remaining, and repeatedly deferred below a 4,600-value readiness
threshold. The active native window/Brave tab no longer contained that match;
Stefan confirmed he had closed it and requested both fixes regardless.

Easy/Medium/Hard campaign enemy update now reconstructs the configured worker
limit from initial refinery allowance and difficulty multiplier; the game-wide
override can lower it but cannot raise it. Full human partners retain their
separate economy development; Brutal keeps its broader economy policy. Existing
excess workers are not deleted. Enemy readiness is capped at the shared sortie
value allowance (level-4 Easy 1,200 rather than 4,600 in the reported example).
Fair turn allocation uses the same threshold. Under-strength campaign enemies
recheck in at most 15 game seconds. Opening grace, recovery, percentage selection
and combined wave budgets remain enforced. Empty readiness checks no longer log
fake zero-unit ground_hunt events.

Native Release and six CTest targets pass. Real-engine `--pacing-probe --level 4`
passes with override 100: enemy limits 2/4/4 for two initial refineries, override
1 still lowers them, partner can exceed 2, and 1,500 army value can launch a
budgeted Easy sortie below the former 4,600 threshold without bypassing opening
or topping up an active wave. Evidence: `/tmp/dunecity-campaign-balance/pacing-671-v2`.
Defense/pressure fixtures and full match matrix are being rerun. The 670 web
build was cancelled in its optimizer after these additional user requests;
671 full browser build is in progress. No public deployment or push.

## Campaign defensive response — 1.0.670 local, 13 September 2026

Browser testing of 669 exposed passive enemy defenders: GUARD uses the tank's
short weapon range, while the seven-tile Easy/Medium base perimeter rejected
nine-tile launcher attacks. Waiting campaign troops now use AREAGUARD. Direct
hits trigger bounded retaliation independent of offensive opening/slots; bases
and remote harvesters summon the existing threat-sized reinforcement response.
Contact radii include the attacker's range plus two tiles. Self-defense uses the
existing saved defense assignment and guard point as its pursuit anchor. Orders
survive wave checks but expire when the target leaves that area. Repair retreats
are preserved. Campaign activation no longer consumes the first human hit without
also defending. UnitBase::isInWeaponRange now uses its argument instead of
incorrectly dereferencing the unit's current target (which could be null).

Real-engine `--defence-probe` passes for all four tiers: outranged tank response,
base first hit (including ordinary human activation), remote worker rescue,
orders surviving wave checks, bounded pursuit, Area Guard and repair retreat.
Evidence: `/tmp/dunecity-campaign-balance/defence-670-v2`. The existing pressure,
recovery, save/load and Windtrap probe passes at `pressure-670-v1`; all six CTest
targets, native Release, dependency audit and signature verification pass.
Browser build and corrected full-match matrix are in progress. No push/deploy.

The pre-fix 669 batch won Easy/Easy levels 4,5,9 and Hard/Easy levels 8,9 on
seeds 1 and 42. Medium/Medium level 9 seed 42 won; Hard/Hard lost; Brutal/Brutal
reached 60 minutes with no player base/economy remaining, not a healthy stalemate.
Brutal/Hard levels 6,7,8 won both seeds, level 9 won seed 42 and timed out seed 1.
All sampled shared offensive budgets passed, but these outcomes are provisional
because of the defense bug. Full artifacts are in `669-validation` and
`669-brutal-hard-validation` under `/tmp/dunecity-campaign-balance`.
The visible 669 Easy/Easy Harkonnen level 9 (seed 163683417) reached the ending
cinematic after the last telemetry read at 24.61 game minutes. The user closed
that browser tab before final score/summary capture; do not invent exact totals.
## Browser bundled mods restored — 1.0.679, 13 September 2026

Stefan reported that public browser 1.0.665 only listed Dune City and Vanilla.
Emscripten preloaded data/config/sprites but omitted mods/Tornie and mods/Dune2R.
Native packaging already included both. The pending 1.0.674 tag and duplicate
main CI runs (34759208672, 34759197806) were force-cancelled before publication;
GitHub latest remained 1.0.665. Keep the existing v1.0.674 tag immutable.

Version 1.0.679 adds both managed mod payloads to browser preloads, excluding
Dune2R optional downloadable art as native packaging does. Managed reseeding
recreates the empty Dune2R asset directories omitted by file-only archives,
preventing fallback to Vanilla on the next launch. Existing downloaded art is
preserved. The build and production web workflows validate the actual JS/data
archive for both mods and Tornie's SHA256 manifest. Six regression tests cover
metadata forms (including minified exponent offsets), missing mods, corruption,
truncated data and accidental art. The actual 679 archive verifies all 769
Tornie checksum entries and six Dune2R files in 38,455,774 bytes.
The check correctly rejects the previous CI 674 artifact for missing Tornie.
Native Release, dependency audit and all seven CTest groups pass.

Shipping authorization persists for 679, with the already disclosed WAN and
multiplayer save/resume limits. Keep Stefan's running local 673 game untouched.
Local browser acceptance passes on the existing 674 preview profile upgraded
to 679: all four mods appear in Extras, Custom Game and Campaign. Tornie and
Dune2R activate and remain active after individual reloads; an offline Tornie
custom game reaches gameplay. Public publication must be verified separately.

Stefan explicitly requested a GitHub administrator exception for ggtothemax.
Ruleset 23003149 now permits User 325456832 in pull_request bypass mode; all
other rules are preserved. Readback confirmed pull_requests_only. PR 30 merged
with that exception after checks passed, producing 72bae344. Do not broaden the
exception to other admins or direct pushes. The website copy is staged separately
in dunelegacy-release-674 and must be updated for 679 before publication.

## Release authorization — 1.0.674, 13 September 2026

Stefan explicitly requested shipping after being told that different-network,
two-device multiplayer and multiplayer save/resume remained unverified. Release
1.0.674 with those limits recorded in releases/desktop/1.0.674.md. This authorizes
normal stable-tag desktop, browser, website and SourceForge publication; keep the
current local human co-op session running. Source is a fast-forward of public
main b7db719. Publication outcome must be checked separately from local builds.

## Classic player setup restored — 1.0.674 local, 13 September 2026

Stefan rejected the sparse left-aligned player setup and requested the original
custom-player UI for both Offline and Online. The classic centered roster/map
composition, full player labels and dropdown widths (where space permits),
wide-screen button margins, and optional Bonus palette selector are restored.
The unified map/mod/connection/rules setup and 673 hosting validation remain;
private/public visibility appears only online. Compact shared-house rows fit
640 pixels. Simulation, saves and transport are unchanged.

Native Release and all seven CTest groups pass; after the final narrow Bonus
label adjustment, the menu probe passes again. It now asserts actual rendering
at 640×480, 854×480 and 1280×720, including six-house Offline/Online, shared
houses, campaign lobby and Tornie bonus colors. The test-only main bypasses
SDL dummy desktop clamping, which otherwise silently reduced 1280 to 1024.
The final web Release build and dependency audit also pass. Native/web 674
artifacts are separate from the running 673 session. See docs/menu-acceptance.md.

Stefan also personally hosted and started a private 673 Ordos campaign in the
browser. Codex joined as MenuHost using the matching native client; both rosters
were visible before Start, both entered the mission, and Stefan's unit movement
and exploration appeared natively. Peer reports passed cycle 27,749 without a
reported desync/disconnect in inspected diagnostics. This passes the human
hosting/start check locally; it does not establish WAN or a completed campaign.

IMPORTANT: Stefan is still playing. Keep browser tab 3, localhost:8769, the
private service localhost:60458, and the running 673 Dune Menu Host test copy
untouched. Do not close/reload the browser, replace its served play folder, stop
the test client, or overwrite its executable/profile. The 674 build belongs in
build/bin and a separate play-674 package; switch only after the current game.
No install, push, PR or public deployment performed.

## Hosting fixes — 1.0.673 local, 13 September 2026

Create Campaign/Custom from Join Online now commits the entered name before
building the roster. Custom online setup requires an explicit open guest slot;
existing AI choices survive the Offline → Online switch. Admission progress no
longer says Joined before receiving the host setup, and receipt/roster timestamps
make the transition observable. The source-controlled web Release link uses -O2
with O3 C++ compilation; the local linker override is cleared and normal native
and web Release builds pass.

All seven CTest groups and dependency/version/signature audits pass. Real 673
native-host/browser-guest public campaign and browser-host/native-guest private
custom games reached gameplay. Guest rosters were visible before Start; campaign
receipt-to-roster took 20–25 ms on two joins, peer traffic passed cycle 12,374,
and a browser infantry command appeared natively. Custom peer reports passed
2,249. No reported desync in inspected diagnostics. The older browser waiting
observation was not reproduced; do not invent a transport root cause or claim
one was repaired. The suspected native keyboard trap was input-testing trouble;
actual keyboard Start and a production-menu focus test pass.

Stefan's crash screenshot was traced to temporary host PID 30706 rejected by
macOS after replacing its executable without re-signing the copied app. Re-sign
fixed it. A separate menu-probe crash was corrected test setup (network callback
without a manager). Exact reports/evidence and scope: docs/menu-acceptance.md.
The playable delivery app passes strict codesign verification. Applications still
held 1.0.653; the 673 test candidate is build/bin/dunecity.app. No install, push or
deployment performed. Human hosting / second-device or network check remains
before release; no new saved-session, WAN, mobile or campaign-completion claim.

## Menu acceptance — 1.0.672 local, 13 September 2026

Stefan requested actual verification. Commit 2f440b5 fixes a live-discovered
Home keyboard trap: hidden legacy buttons participated in Tab navigation.
Only visible destinations now register in screen order, and initial focus is
assigned after registration. The production-menu probe checks a complete cycle.
Native Release, dependency/version/signature checks and all seven CTest groups
pass. A live cycle with Continue visible also passed.

Isolated native clients exercised all four core routes into actual gameplay:
offline Campaign and Custom, private-code online Custom (670), and public
Campaign co-op (672). Campaign retained Ordos/level 4 through cancellation and
hosting and loaded SCENO008.INI. Real directory filters distinguished campaign
from custom. Custom peer traffic reached cycle 11,999; campaign passed 2,600;
neither inspected log reported desync. Offline save → Home → Continue resumed
the mission. Online custom saves route into hosting lobbies in 670 and 672.
Mods and Map Editor opened from Extras; unified Graphics was inspected.

Exact evidence and scope are in `docs/menu-acceptance.md`, with logs and isolated
profiles under `../outputs/dunecity-menu-acceptance`. The browser test build
joined a fresh public native campaign, passed the start barrier, and ran to
cycle 14,624 with no reported desync. A browser unit command appeared on the
host. Browser Home/Join/Graphics/Audio mouse navigation passed. The browser
initial lobby display remains open: it could show Join Online's waiting screen
until the host started, despite the host recognizing the browser. Saved co-op
admission was checked, but actual saved-session resumption is not claimed.

Default web Release linking was stopped after over 20 minutes in Binaryen's
local2Stack optimizer (12.2 GB sampled footprint). The successful browser test
used existing Release objects with CMAKE_EXE_LINKER_FLAGS_RELEASE=-O2; default
project flags are unchanged. This does not validate the default O3 web release.
No WAN/mobile or completed-campaign claim. Nothing was pushed or deployed.
Existing Game Rules/New Map keyboard dismissal remains a follow-up; native
pointer automation was unreliable. The candidate is not fully signed off.

## Menu navigation — 1.0.670 local, 13 September 2026

Stefan approved implementing the menu review, including Mods and Map Editor in
Extras. This candidate starts from campaign-controls commit 81ff90a (1.0.669).
Home now goes directly to Campaign, Custom Game, Join Online, Load Game,
Settings and Extras. Campaign has explicit Start, Offline/Online, full campaign
or single mission; online hosting carries the selected setup into the lobby.
Custom combines map/mod/connection/rules/player choices, preserving the roster
when changing connection mode. Join Online has mode/mod filters, invitation
codes, separate public chat, and secondary legacy LAN/direct connections.

Graphics and interface controls share Settings > Graphics. Audio, Controls and
Advanced are separate tabs. Unchanged legacy network fields no longer block
unrelated settings changes. Setup rules persist globally only when requested.
Continue detects eligible recent offline saves; Load Game routes by saved type.
See `docs/menu-navigation.md` for the exact flow and remaining follow-ups.

Native Release and dependency audits pass. All six existing CTest targets pass;
the new real-menu probe verifies setup preservation, filters and settings
validation, and renders at 640×480 and 854×480 with isolated profiles. Its images
are in `build/menu-probe`. Small-screen player rows were corrected after visual
inspection, including shared-house controls. The app is in `build/bin`.
No public deployment, browser/mobile verification or live two-peer game test
is claimed for this menu candidate.

## Campaign difficulty implementation — 1.0.669 local, 13 September 2026

Stefan authorized implementing the matrix. `CampaignDifficultyPolicy.h` and
`QuantBotCampaign.cpp` now enforce alliance-wide campaign assaults. Easy/Medium
allow one attacking house, Hard two, Brutal all. Combined troop caps scale by
tech stage to 3–5 / 6–8 / 10–14 / 16–24 with simultaneous combat-value caps.
There are post-wave recovery periods and no continual top-ups. Timed sorties
withdraw survivors; authored HUNT arrivals wait outside an authorized wave.
Defensive pursuit and air interception are bounded around owned bases and nearby
harvesters. Offensive aircraft consume wave allowance. Easy/Medium share one
base objective; Hard/Brutal split targets and use a lateral approach when usable.

Easy/Medium campaign builders, including human-house partners and economy
support, queue Windtraps for actual/committed/planned power demand. Default
Vanilla power consequences are unchanged. Full campaign partners keep useful
economy/Starport behavior and 25/15/10/5 percent home reserves, without enemy wave
caps. Easy full partners can use the existing damage-triggered repair path.
Campaign setup descriptions explain partner and enemy behavior.

Save format 9838 adds QuantBot opening/launch/activity times, shared-front target
and member IDs; pre-9838 saves initialize that state safely. The real-engine
pressure fixture verifies slots at all tiers, combined count/value limits,
no repeated-check top-ups, serialized bot state, legacy loading, held HUNT
arrivals, sortie expiration/recovery, and actual Windtrap production for two
enemy tiers plus the human partner. Four menu states rendered; Easy/Brutal
screens inspected without clipping.

Native Release, dependency/signature audits and all six CTest targets pass.
Final-source level-9 runs (seed 486409243, no human commands or skip, continuous
budget assertions) won: Easy/Easy at 89,669 cycles (23.91 game minutes), Hard/Hard
at 108,599 cycles (28.96). Evidence under
`/tmp/dunecity-campaign-balance/{pressure,easy9,hard-hard9}-669-verified`.
Full Emscripten Release build passes. Local preview serves 669, game-source
commit `81ff90afa4cda8f8acffe768f194d4ee205aee36`; all seven served artifact hashes
match `play/build.json`. Browser tab 8 at `http://127.0.0.1:18766/play/` was opened
fresh, displayed v1.0.669, and reached the updated campaign setup successfully.
All four native menu descriptions were inspected without clipping. No public
deployment or push was performed. Human accessibility and broad seed/house coverage are not yet
established. See `docs/campaign-ai-difficulty-matrix.md` for exact values.

## Campaign difficulty design matrix — 13 September 2026 (superseded by implementation above)

`docs/campaign-ai-difficulty-matrix.md` records current and proposed behaviour
for all four difficulties, separately for enemies and the full QuantBot sharing
the human house. No balance implementation or deployment accompanies this doc.
Stefan requests Easy/Medium enemies take turns attacking and build Windtraps to
cover demand. Proposal adds combined assault budgets/recovery intervals; Hard
may overlap two houses, Brutal all. Trial sizes/timings remain unvalidated.
Vanilla general power is currently disabled for humans and AI alike; building
Windtraps does not itself restore shortage consequences. Corrected prior audit:
Easy has reactive on-hit evasion, but lacks proactive ranged spacing. Structure
and RETREAT repairs mean Easy/Medium do not lack every repair path.

## Campaign score attribution — 1.0.668 local, 13 September 2026

Game-source commit `f3c5cd1` on `fix/campaign-ai-attack-limits` fixes
`CampaignStatsMenu::calculateScore`: classify houses, surviving structures and
loaded harvester spice by the local team's ID instead of `House::isAI()`. A
human house shared with QuantBot sets that AI flag, so previous versions put its
harvests/kills under Enemy, subtracted its destroyed value and omitted its cash
and surviving-building score. The score formula itself and gameplay are unchanged.
Human opponents stay on Enemy; AI allies count with the local team.

`tests/ai/run-campaign-balance.py --level 9 --stats-probe --output-dir <new-dir>`
instantiates the actual results menu against a real shared-house campaign. It
checks ordinary-human versus AI-assisted score/rank parity, known kills and
harvest totals including carried spice, an AI ally and a human enemy. Fixture
screenshot verified: You 1,300 spice / 7 units / 3 buildings; Enemy 775 / 5 / 2;
score 567, Warlord. These are controlled test totals, not a played mission.
Evidence: `/tmp/dunecity-campaign-balance/stats-668-render-v2`. Native Release,
dependency and signature audits and all six CTest targets pass. Full Emscripten
build also passes; local preview packaging uses 668. The already-running 667
browser tab requires a fresh load to pick up this results-screen fix.

Stefan also asked why campaign difficulties look similar. Audit in
`docs/campaign-ai-balance.md` distinguishes enemy and shared-house partner paths:
enemy ground-HUNT budgets are 25/40/50/60%; initial attack delay and much planning
are shared. Partner uses Custom growth and has no enemy wave cap; its configured
harvester difficulty caps are overwritten by common dynamic map limits. Several
legacy defender-count/aircraft-threshold knobs are loaded but unused. No further
AI tuning was made. Public deployment remains 665; user's existing browser game
uses 667 and has not been restarted.

## Campaign economy and selection — 1.0.667 local candidate, 13 September 2026

Branch `fix/campaign-ai-attack-limits` in the campaign-controls worktree. Building
clicks now replace all previous building/unit selections, including Shift-click;
Shift-clicking a unit also removes selected buildings. The real SDL command
probe passes this regression on campaign levels 4 and 9.

Needed Starport harvesters and the first carryall can now spend Vanilla's reserved
economy cash. They already bypassed the cheap-price filter; the reserve was the
actual barrier. Purchases check availability, stock, affordability and accepted
queues, and stop at the sustainable worker target. A real Starport fixture bought
a 1,500-credit carryall and 1,200-credit harvester with exactly 2,700 credits,
placed the order and left zero credits. Other imports retain their existing rules.

Stefan raised the balance target: Easy should survive level 9 against Easy,
even if it cannot win. Four real-game simulations with a 60-minute cutoff all
**won** using the existing 25% enemy attack budget: seeds 486409243 / 1 / 42 /
257913089 finished in 31.95 / 30.60 / 29.81 / 26.59 game minutes. Hard also won
seed 486409243 in 22.21 minutes. These runs used 667's combined economy/selection
changes, with no human orders or skip. No further wave reduction is justified by
these samples; other houses, map variants and human play remain untested.

In the visible 666 browser campaign, Stefan explicitly confirmed the level-8
victory screen and continuation (Hard partner / Easy enemies, seed 493337323).
He subsequently closed the browser campaigns. Codex did not capture a final
level-9 browser victory screen; native wins are recorded separately.

Native Release, post-build dependency audit, signature verification, all six
CTest targets, real SDL selection probe, above-price Starport probe and full
Emscripten build pass. See `docs/campaign-ai-balance.md` for reproducible commands
and evidence. Version 667 is local only; installed app and public 665 unchanged.

## Campaign AI balance — 1.0.666 local candidate, 13 September 2026

Worktree `/Users/stefan/Documents/projects/dunecity-campaign-controls`, branch
`fix/campaign-ai-attack-limits`; original dev checkout remains untouched.
Game-source commit `05979ae` implements campaign enemy ground-hunt budgets from
existing QuantBot difficulty settings: Easy 25%, Medium 40%, Hard 50%, Brutal 60%.
Already committed hunters consume the budget; deterministic selection preserves
reserves. A lone cheapest unit may exceed an otherwise empty small-army budget.
The full-control human partner remains uncapped. No save-format, economy,
reinforcement, opening timer or skirmish changes.

Stefan's acceptance target: Easy partner should win or hold through levels 4–5;
Hard partner should beat Easy enemies on level 9. In two fixed-seed real-game
simulation sets, Easy won levels 4/5 and Hard won level 9. The local browser
level-4 Easy-versus-Easy run won in about 15 minutes. Its first enemy wave was
8 units / 1,050 value, versus 31 / 4,360 in public 1.0.665. All six CTest targets,
native Release and Emscripten builds pass. Native app signature and version 666
were verified. `build/bin/dunecity.app` is rebuilt; installed app not replaced.

See [campaign AI balance](docs/campaign-ai-balance.md) for measured outcomes,
repeatable diagnostic commands, limitations and source-verified Dune Dynasty
comparison. Dynasty gates team scripts on enemy contact and recruits small
scenario-defined teams; its reinforcement schedule is separate. QuantBot still
uses its existing 8–12 minute opening wait. Importing contact activation could
start attacks sooner and is not part of the demonstrated size fix.

**Release status:** 1.0.665 is merged and fully public (GitHub, SourceForge and
browser), source `b7db7199455d9b756043118b7412c1d1e9359d06`. Browser artifacts were
hash-verified. Website deployment `6042d82f9780e37474bbc7624e9b8f76ef817729` and
anonymous feedback service are live; no server sudo/package install is needed.
The Mac mini runner and caffeinate wrapper are restored; temporary Air builder
label removed. Original branch protection restored after PR26 merge.
**1.0.666 balance changes are committed locally, not pushed, merged, tagged or
publicly deployed.** Earlier dated entries below describe historical states.

## Direct command pacing correction — 13 September 2026

Local branch `fix/direct-p2p-command-pacing`, based on published 1.0.665 (`b7db719`).
Version 1.0.667 reserved here because the separate campaign-AI branch already uses 1.0.666.
This change is not deployed. User asked to remove relay behaviour from P2P gameplay after
reporting generally sluggish Brave multiplayer with VR48.

`CommandManager::update()` was incorrectly applying the legacy 100ms relay emission cadence
because `isRelaySession()` aliased all room sessions, including DirectP2P. Direct P2P now
sends the rolling command window on every simulation iteration, the same as ENet. The
ambiguous alias is removed; room lifecycle, validation, state digests and pause guards use
`isRoomSession()`, while only the actual legacy relay uses `usesBatchedCommands()`.
No gameplay fallback, protocol changes, command skipping, catch-up changes or server changes.

Regression coverage includes two simulated peers on an ordered lossless 286ms RTT path,
22-cycle lead and 10ms ticks. The old cadence loses over 5 seconds of simulated progress in
120 seconds; DirectP2P and ENet preserve >=99% of intended pace. This is a controlled timing
model, not a reproduction of all conditions in the reported WAN match. Full native build,
six CTest suites and Emscripten syntax checks of all changed translation units pass.
No fresh browser multiplayer playthrough yet; published 1.0.665 is unchanged.

The user's diagnostic text concatenated the usual desktop-equivalent general/performance
logs. There is also per-mission `ai-decisions/<session>/events.jsonl` in the browser virtual
filesystem: its `performance_window` events contain wall-clock intervals and `frame.tick_ms`.
Use that to measure actual simulation pace; legacy reported FPS excludes browser yield time
and NetworkWait excludes time between frames. No complete cause claim from those fields.

## Campaign controls release integration — 13 September 2026

Version 1.0.665 combines campaign controls, map-selection repair, AI partner choices
and anonymous feedback with current main direct-P2P changes. Feedback targets
`ggtothemax/dunecity` following repository transfer. The no-expiration token is
restricted to this repository, Issues read/write and Metadata read-only. It is
provisioned as the website Actions secret and server file outside the webroot.
The dated local checkpoints below describe earlier states.
Native Release build, dependency audits, all six CTest suites and the real-game
command/input/feedback/AI partner probe pass for the combined source. Website
feedback now follows the analytics service’s Python SQLite fallback when the PHP
driver is absent; no administrator package installation is needed.

# Campaign controls and feedback — 1.0.663 (local, 13 September 2026)

## 2026-09-13 — 1.0.664 input repair and anonymous feedback (local)

Worktree `/Users/stefan/Documents/projects/dunecity-campaign-controls`, branch
`feature/campaign-controls`; original dev checkout remains untouched.

- Fixed 1.0.663's selection regression: `BuilderList` returned handled for clicks
  outside its bounds once a builder was selected. The new Game input guard then
  discarded map clicks. Both left/right handlers now check bounds and visibility;
  hidden generic buttons also let clicks through. Real SDL input tests select a
  unit after a builder on campaign levels 4 and 9, alongside feedback open/close.
- Moved **Give feedback** to the top bar, retained campaign skip at bottom right,
  and let the news ticker shrink/clip within the available top-bar space.
- Feedback sends asynchronously to `/metaserver/feedback.php`, includes the active
  AI houses/types/difficulties, and displays **View request / OK** only after a
  confirmed issue URL. Failure preserves the entered text and retry id. No player
  GitHub account or credential in the game. Sending is bounded to 20 seconds;
  the modal cannot be dismissed while sending. Browser opens only on View request.
- Campaign AI partner selection has AI Support Easy/Medium/Hard/Brutal and full
  QuantBot Easy/Medium/Hard/Brutal/Defend. Descriptions distinguish economy/building
  assistance from full unit control. Existing QuantBot shared-human-house behavior
  already permits full campaign control; no simulation/save schema changed.
- Server counterpart is the isolated website worktree
  `/Users/stefan/Documents/projects/dunelegacy-feedback`, branch `feature/game-feedback`.
  See its `docs/game-feedback.md`. Fixed repo, server-only Issues-scoped token,
  input/rate limits, SQLite idempotency, and lost-response reconciliation. Mocked
  PHP tests create no real GitHub issues. **Not deployed.** Website Actions secret
  `FEEDBACK_GITHUB_TOKEN` was absent when checked; it must be provisioned before
  live submission. No broad local account token was copied to the service.
- Validation: native Release build; all six CTest targets (683 main cases passed,
  three expected skips); expanded real-game probe with input, AI context, retry
  and success states; standalone Emscripten compile of the browser feedback client;
  rendered dialog/top-bar/campaign layout checks. No full browser build or live
  GitHub issue creation was performed. Local app is `build/bin/dunecity.app`.
  No install, running-game restart, push, tag, PR or public release was performed.


Implemented on `feature/campaign-controls` in
`/Users/stefan/Documents/projects/dunecity-campaign-controls`, based on main
`b4d1471` (1.0.662). The older `dunecity` checkout/branch was left intact.

The opening menu now starts with Play Campaign and Play Other Modes. Campaign
remains accessible through Single Player. Both use the same campaign setup.
The house screen fits the 640x480 logical interface and adds Start from level
(1–9), Campaign mod with its metadata description, help text and Back. Levels
map to first scenarios 1, 2, 5, 8, 11, 14, 17, 20, 22. New settings retain the
Campaign game type, mod identity and normal save/progression behavior. Changing
mods activates its assets, available houses and effective game options; failed
activation restores the previous selection.

The lower-right playfield, beside the sidebar, has Give feedback on features
or issues and a campaign-only Skip mission button. This placement keeps full
build lists available at minimum resolution. UI mouse events are consumed so
these buttons cannot also place buildings or issue map orders. Skip is appended
to the command enum without renumbering existing commands. Only human controllers
of the campaign house can execute it, including shared-house co-op. It calls the
normal victory path; replay viewing cannot issue new skips.

Stefan chose a prefilled GitHub issue using the player's account. Feedback has
summary/multiline text fields, Unicode editing and paste, a visible game-context
preview, validation and URL encoding. Open GitHub targets VR48/dunecity and leaves
text available in the dialog. Players finish submission on GitHub; there is no
anonymous endpoint, embedded credential or automatic issue submission. Empty
labels avoid gettext's empty-string catalog metadata entry.

Validation: native Release build, pre/post Ninja dependency audits, signature
verification and version checks pass. CTest: all six targets pass (main suite
683 passed, 3 expected skips; 9,752,032 assertions). Extended real-game command
probe passes ordinary/final co-op mission victory/continuation, invalid issuer
and malformed skip rejection, non-campaign rejection, actual feedback button
opening, multiline/Unicode/select-all editing, plus previous ownership/batch/
relay-pause checks. Feedback window rendered and visually checked. Native UI
keyboard navigation verified the opening shortcut and level 4 with Dune City,
Atreides, SCENA008.INI and city simulation active, at 640x480 logical resolution.
CUA synthetic mouse clicks were unreliable even for pre-existing menu buttons;
the new feedback button was additionally checked through its real GUI handler.
No GitHub issue was posted during validation. Browser builds and live crossplay
were not rerun for these UI changes.

Local app: `build/bin/dunecity.app`. Test profiles and artifacts are under
`/tmp/dunecity-campaign-*`; normal user settings were not touched. No push,
release tag, installed-app replacement or website publication was performed.

## Direct P2P branch checkpoint — 13 September 2026

Branch `feat/p2pkit-direct-crossplay` is under test; the game is **not released**.
Stable remains 1.0.661. The companion Apache/PHP service is deployed and verified.
The branch vendors P2PKit commit `94ae7eb8818a629478e0a6ba0aa3232c5fc0b1ab` RTCTransport and
framing, with direct-only bounds. Browser gameplay uses those actual modules; native crossplay
uses pinned libdatachannel `443f6934d9007eb7076ab7825ba330f355fcbead` with compatible framing.
Apache/PHP only serves admission, public lobby/chat and SDP/ICE introductions. No TURN or
in-game forwarding/relay. Existing native ENet remains available.

Opus supplied the initial implementation and part of the review corrections. Stefan explicitly
asked Codex to **stop using Opus** on 13 September; do not resume its sessions for this task.
Codex completed the remaining fixes and owns coding/testing. Hermes has supplied independent
security findings; its third fixed-snapshot review finished without production approval.
Report: `../outputs/network-hardening/p2p-hermes-review3.txt`. Codex addressed and tested
its start-barrier/send-boundary, duplicate START and session lifecycle findings below;
Hermes has not reviewed those subsequent fixes.
Do not restart Opus or run further broad Hermes review rounds by default.

Current checks: six native CTest suites and 164 PHP HTTP/concurrency tests pass.
RTC and bridge tests cover synchronous send failure, bounded queue order and later failure.
The real three-client session fixture passes the prepare/ACK/commit handshake and exchanges
262128-byte ordered payloads for 75 seconds with all PHP workers stopped.
Hermes review3 findings are addressed by an authoritative roster CAS, host/guest start barrier,
one-shot START callbacks, bounded admission retry recovery and best-effort leave with host expiry.
The real fixture also caught a missing admitted-peer role assignment; it is fixed.
The direct host menu now registers the asynchronous countdown callback as well as guests.

Normal Release/O3 browser linking restored successful fresh main-menu startup after the temporary
-O1 build crashed. Do not use the temporary -O1 linker override.
Two actual browser clients played with movement and construction after every local PHP
worker was stopped: 23 matching simulation digests per client, zero mismatches.
Evidence: `../outputs/network-hardening/p2p-browser-outage-acceptance.json`.
Public browser/native play subsequently passed 417 matching digests through cycle 83400,
with a connected RTC channel and browser construction observed. This is same-Mac testing,
not proof of connectivity across different Internet NATs. Two fresh production-signaling
browser clients joined publicly as alice/bob, deployed both MCVs and moved a tank while
signaling requests were blocked in both test tabs. All 79 captured simulation digests per
client matched, through cycle 23000. Evidence: `../outputs/network-hardening/p2p-public-game-acceptance.json`.
The isolated browser-only test tabs were closed; browser/native gameplay was left running.
Final platform builds remain pending. CI cancellations reported repository transfer to
`ggtothemax/dunecity`; the restarted candidate run is 34734606729.
The Linux relay supervisor fixture now handles ESRCH while reading a disappearing procfs file.
Repeated successful match-phase requests no longer produce duplicate started analytics events;
the 164-test PHP suite verifies idempotent start logging.
Background directory refresh no longer disables the public list/join button and steals
keyboard focus. Native keyboard joining now works during an in-flight directory refresh.

The website companion branch `feat/p2p-signaling-web` in `../dunelegacy-p2p` adds private PHP
service installation and additive schema-3 direct-P2P lifecycle logging. Migration tests preserve
schema-1/2 records and legacy matches. Website PR #5 merged at ff3ec4f; deployment
34733529907 succeeded. Public health and origin rejection pass; SQLite records both browser
and native admissions as direct-p2p/signaling_service_v1, retaining old records and integrity.
Private service/config/state remain outside the webroot. A pre-migration SQLite backup is
in the deployment account's deployment-backups directory. Stable relay clients are unchanged.
See `docs/direct-play.md`, `tools/p2p-signaling/README.md` and `tools/p2p-session-smoke/README.md`.

## Public HTTPS polling acceptance — 13 September 2026

**1.0.661 is published on the normal website and desktop release channels.** Source
and stable tag are 43ec1dc. Candidate CI 34707717946 and stable run 34708449665 passed.
All six published GitHub packages match their API checksums. SourceForge run
34708945092 passed all uploaded checksums and verified Windows/macOS/Linux defaults;
an independent HTTPS git read confirms its source branch and tag point to 43ec1dc.
Browser publication 34708945094 and website deployment 34709214712 succeeded at
website commit 8f348f4. The live `/play/build.json` identifies 1.0.661 / 43ec1dc;
all six browser asset hashes, relay origins, CSP and WASM MIME type were checked.
Fresh production UI startup displayed 1.0.661 and opened the online lobby without
the previous crash. Relay health remained OK with zero rooms/connections after
deployment. Evidence: `../outputs/network-hardening/public-661-deployed-hashes.json`
and `../outputs/network-hardening/release-661-github-verification.json`.

Public 1.0.659 matches overflowed the polling queue (close 4431). 1.0.660 at 52ac12e
paces relay command history every 100 ms with a retention guard and removes two
redundant browser waits. Public browser/native and browser/browser matches then ran
approximately 22 and 19 minutes and ended intentionally, but simulation advanced
only about 37–42 cycles/s against 62.5 configured despite 60Hz browser RAF callbacks
(these callbacks are not direct SDL render-frame instrumentation).

Actual Claude Opus implemented a bounded startup allowance; Codex narrowed it to
HTTP polling, preserving ENet/WSS sizing and CommandValidation's existing bounds.
1.0.661 budgets 700–1120 ms, capped at 70 cycles, fixed for the match. It requests a
heartbeat at join and has a fallback before an answer arrives. More allowance adds
input delay; arbitrary jitter/asymmetry and faster game settings can still stall
lockstep. This is a bounded improvement, not elimination of network latency.

Actual Hermes and Opus found no code/security blocker in the final narrowed source.
Native CTest 6/6, dependency audits, generated-fetch regression and wasm32 network
wire 80 checks pass. Public crossplay ran over ten minutes and browser/browser over
seven, with movement, MCV deployment and Windtrap construction. Final retained digest
samples matched (148 crossplay and 124 per browser client), with no captured premature
close. Hosts exited normally and health returned to zero rooms/connections. Twenty-
second RAF captures averaged 16.66 ms; simulation was roughly 56–57 cycles/s. These
samples do not prove complete determinism or performance on every connection.
Evidence: `../outputs/network-hardening/public-661-evidence.json` and
`../outputs/network-hardening/poll-latency-review/`.

The website's scheduled download-count deployment removed the previously manual
relay gateway during test startup. Website main now includes the companion gateway
and analytics source at 9a85d48; deployment 34707911135, web security 34707911134 and
analytics compatibility 34707911193 passed. Relay health recovered and both actual
matches ran through this tracked gateway. Keep these files in website main: routine
rsync --delete removes anything merely uploaded to the webroot. Preview directories
also disappear on website deploy, so restore a preview only after that deploy ends.

The existing server now runs the restricted-account Apache/PHP HTTPS gateway at
`https://dunelegacy.com/relay`, with Node bound only to loopback. Deployment revision
10cd89f has v2 artifact manifests, release pinning and a single supervised child with
cron recovery and kernel parent-death protection. Actual Hermes rechecked and cleared
the two deployment findings (release-switch execution race and missing root-directory
permission checks). 38 manifest checks, 42 supervisor checks and the release-pinning
fixture pass on the server. Actual hung-child recovery took 80 seconds; killing the
supervisor recovered through cron in 70 seconds. No administrator access was used.

Actual public transport harnesses exchanged 17 matching digests each with no mismatch;
this proves transport only, and the later actual-game failure above supersedes any
readiness inference. SQLite schema-2 migration preserved the backed-up legacy rows;
signed relay lifecycle records arrived over local HTTPS. External event submissions
return 403. Bounded concurrent gateway requests and a short-body timeout test passed.
The limited test was not a capacity or DDoS certification.

## Crossplay candidate — 12 September 2026

Final candidate CI `34694065319` is successful on Windows, macOS and Linux, with
all six packages downloaded under `../outputs/network-hardening/release-657-artifacts-b4c6af3/`.
Actual Hermes's final installer recheck (`20260912_124943_2a850b`) clears the six
previous findings for bundle source `c1bd25e`. The archive is staged outside the live
website under `/home/dunelegacy-deploy/relay-deployment-c1bd25e/`; its checksum was
read back and verified. Operator instructions: `../outputs/network-hardening/relay-administrator-handoff.md`.
Administrator access and actual public WSS/proxy/SQLite verification are still required.

Branch `fix/network-hardening` is published for candidate packaging as
`release-1.0.657`; current main `8879732` is incorporated. Production main and the
stable tag are deliberately not advanced: the restricted metaserver SSH account
cannot install services/Apache configuration and no administrator access is known.
The configured production crossplay endpoint is `https://dunelegacy.com/relay`.

Native 5/5 CTest, 185 relay tests, 173 wasm32 wire checks, secure-WebSocket feature
checks and browser package policy/hash checks pass. Forced libcurl partial-write
fixture passes. Actual Claude and Hermes findings and scope are recorded in
`docs/crossplay-final-review.md`. Opus implemented the administrator bootstrap
hardening; Codex reviewed it and ran its 53 isolated helper checks. Public TLS/WSS,
proxy behavior and signed SQLite delivery still need on-host verification.

Stefan joined a browser-hosted match from the native app through the public list.
Actual browser/native digest samples agree through cycle 40,600, including a browser
menu interval; these are samples, not proof of complete determinism. Current running
clients predate the latest lobby presentation changes. The public join button is now
full-width immediately below the game list; private joining says “Join with invite
code”. These changes are committed and built for preview separately from the match.

The initial test-app launch failure was macOS CODESIGNING/Invalid Signature after
copying a binary; its bundle was re-signed. A pre-existing native test process survived
SIGTERM and remained on its earlier executable. Do not mistake copying a binary or a
new launch command for replacement of that process. Avoid interrupting Stefan's match.

# Browser match logging — 1.0.655

Extends the Play Online display hotfix below. Both start/end summaries carry an
optional schema-3 client_runtime (browser/native). Emscripten bypasses the native
MetaServerClient SDL thread, which is unavailable without pthreads, and queues
same-origin POSTs through the shell with two bounded attempts. Form payloads
avoid GET length limits; small requests use keepalive. Game code never waits on
analytics. Tab close/crash can still leave a start-only match.

Metaserver commit e02d40d adds client_runtime to analytics_matches in both PHP/PDO
and Python backends. Legacy/missing/invalid values default to unknown; missing
later values preserve known runtimes. No historical classification is invented.
Deployment 34673787973 and compatibility CI 34673787975 passed. Live health
migrated 1220 existing matches as unknown. SQLite backup before migration:
/tmp/dunecity-runtime-655/live-before.sqlite on metaserver. Query details and
compatibility tests are in the website repo's metaserver/ANALYTICS.md.

Local Emscripten and native Release builds passed; dependency audits and CTest
passed (588 passed, 3 expected skips). Actual Chrome skirmish start and quit
produced matching start/end IDs, client_runtime=browser, version 1.0.655, and
abandoned outcome (649/2602-byte summaries), captured at
/tmp/dunecity-analytics-events.jsonl. Shell tests cover ordered POST/retry/offline
behaviour. The packager now handles Emscripten's unquoted minified HTML attributes;
a regression test verifies all three asset references receive the build token.
Published website commit d2aebcd; deployment 34673987445 and browser security
check 34673987519 succeeded. Live manifest and all six SHA256s match game source
7124425. Production Chrome loaded 1280x720 with all five asset URLs sharing
?v=1.0.655-2c0245d2a43c. An actual live skirmish generated HTTP 200 OK for start/end
of m1-65b41f2ce0dc0-73d992ca-65b41f2ce0dc0; production SQLite confirms browser,
1.0.655, skirmish, abandoned, timestamps and both JSON records. All 1220 historic
match rows match the pre-migration snapshot byte-for-value on original columns.
The brief abandoned smoke-test row remains identifiable by that match ID.
Installed desktop remains 1.0.653; no new desktop release tag was published.
The desktop CI jobs triggered by the main push are separate from the verified
local native build and published browser package.

# Play Online display fixes — 1.0.654

Browser hotfix based on the latest released 1.0.653 gameplay. Removed the web
640x480 reset and SDL_WINDOW_RESIZABLE (SDL otherwise substitutes the CSS size
for the requested backing buffer). The shell fits the actual canvas ratio into
the stage. Display exposes working 4:3/16:9 controls; Options offers 19 backing
resolutions through 3840x2160, plus the saved custom size. Desktop default is
1280x720, rising to 1600x900/1920x1080 when the stage fits; small touch screens
start at 854x480. A one-time browser config marker migrates old forced VGA while
preserving subsequent deliberate VGA selections and other saved resolutions.

Emscripten 4.0.14 package built locally in /tmp/dunecity-web-build. Chrome tests
verified actual backing sizes, both aspect buttons with Automatic selected,
1920x1080 through the Options dropdown, reload persistence, browser resize and
fullscreen, old-VGA migration, explicit-VGA preservation and fresh touch default.
Native Release build/dependency audits passed; CTest: 588 passed, 3 expected skips.
Three initial Node shell tests cover defaults, aspect fitting and common asset versioning.
Local app rebuilt in build/bin; installed /Applications app left as 1.0.653.

Shared Python packager (also called from PowerShell) records source commit and
SHA256s, versions shell/WASM/data URLs together, and includes web/.htaccess.
Publish browser build follows successful stable desktop releases and refuses a
downgrade; see docs/release-operations.md. This workflow is configured for future
releases; local browser hotfix publication is verified separately below. Browser
multiplayer remains unavailable. The existing reports/ directory is unrelated.

# Public co-op release published — 1.0.653

Release version bump only over the user-tested 1.0.652 gameplay. Includes all
unreleased fixes since 1.0.642 plus campaign/skirmish shared-house co-op.
Release notes are releases/desktop/1.0.653.md. Local Release build and before/after
Ninja dependency audits passed; CTest reports 588 passed and 3 expected skips.
Installed /Applications/dunecity.app, SHA256
9647fa6549b42527dfb7dc42251d5c94683cbb4134d484d4903adb14cd2344e9.
Backup: /Applications/.dunecity-653-fvc2i619/dunecity-previous.app.
Published and verified on 2026-09-12. GitHub tag v1.0.653 points to eaaff4a753d00fe2916c1d8f9c951ae2193cd54d.
Build Dune Legacy run 34672540279 passed Windows, macOS (Mac mini), Linux,
Linux tests and release publication. All six packages are present in the stable release:
https://github.com/VR48/dunecity/releases/tag/v1.0.653.
SourceForge run 34673037345 succeeded: six packages plus notes/checksums were
read back and hash-verified; all three OS defaults now select 1.0.653.
SourceForge dunecity branch and peeled dunecity-v1.0.653 tag match eaaff4a.
Website commit dcfec4a / Deploy to Droplet run 34673168756 succeeded; live home,
Dune City, co-op guide, modding and sitemap match the published source. Co-op is
announced as available and all desktop download links point to 1.0.653.
Legacy SourceForge master commit 591b9e1 adds co-op hosting guidance. The changed
downloads.html was backed up, uploaded atomically, read back and byte-verified
against both the source and public page. Original user profile remains untouched.
Concurrent uncommitted 1.0.654 browser work and reports/ are outside this release.

# Internet-listed co-op smoke test — 2026-09-12

Tested installed 1.0.652 using two independent local app bundles, profiles and
ports (29851/29853). Host Campaign Co-op registered successfully with the live
metaserver; its list2 response and the guest's Internet Games UI both contained
the lobby. Guest joined from that listing, passed mod/config verification, and
both human controllers entered Ordos mission SCENO001.INI. Before save/reload,
337 valve-debug and 42 daily CitySim rows had identical shared prefixes, with
no desync logged. During Stefan's interaction, the guest rehosted a campaign
save and the original host joined through Internet Games; both completed all
save-load stages and resumed the shared game with reversed network roles.

Same-NAT detection selected the local address for game packets. This verifies
real Internet discovery plus local co-op joining/play/save hosting, not a
connection across separate routers. UPnP discovery found no usable IGD; STUN
and metaserver registration succeeded. The second local instance could not
bind the shared LAN discovery port and retried every five seconds, but Internet
listing/joining worked independently. Toggle buttons require Space rather than
Return for keyboard activation (Button::handleKeyPress).

Evidence snapshots: /tmp/internet-test-dunecity-internet-host.log and
/tmp/internet-test-dunecity-internet-guest-local.log. Live profiles are
/tmp/dunecity-internet-host and /tmp/dunecity-internet-guest-local; test apps
DuneInternetHost.app and DuneInternetGuestLocal.app remain open for Stefan.
Stefan subsequently confirmed testing two campaign missions and save/reload.
Final logs show both clients load SCENO002 after the shared save, and shut down
normally. After save/reload, 230 comparable CitySim valve/day records match;
after mission transition, 101 comparable records match. The first mission's
379 pre-load records also match. DESYNC DEBUG entries are routine diagnostics,
not reported desynchronizations. Final evidence snapshots use the prefix
/tmp/internet-test-final-dunecity-internet-*.log. Both test apps are now closed.

Published website guide https://dunelegacy.com/coop.html with a Multiplayer
Co-op navigation tab and home/DuneCity announcements (website commit 964ac35,
Deploy to Droplet run 34671938595 successful; six live files byte-verified).
Home link reads "Play campaign co-op with your friends online!" per Stefan.
Guide covers hosting, joining, QuantBot partners, skirmish and solo/shared saves.
At that checkpoint public desktop downloads were still 1.0.642. The 1.0.653
release above supersedes that pending state and updates the guide/download links.

The Mac mini guest was stopped after switching to the requested local test.
Its portable bundle needed SDL3 explicitly included beside SDL2: Homebrew's
sdl2-compat loads SDL3 dynamically, so otool dependency traversal alone misses
it. No game source, installed app, normal profile, or remote release changed.

# Campaign and mission shared-house co-op — 1.0.652 (local)

Campaign house selection and single-mission/skirmish setup have Host Co-op.
Multiplayer has Create LAN Game, Host Internet Custom Game, Host Campaign Co-op.
The campaign setup chooses house and mission (1–22), Internet or LAN only,
or loads a solo campaign save (save/) or shared campaign save (mpsave/).
The fixed-house lobby exposes exactly two controllers: primary human plus
another human or QuantBot (Easy/Medium/Hard/Brutal/Defend; support variants also
available). Existing server listing, connection, mod/config synchronization
and countdown machinery is used. Scenario enemy identities/teams are preserved.

Campaign continuation preserves both controllers and campaign progress, with
host-selected next scenario/seed sent reliably to the client. Old simulation
packets are rejected using their mission seed; callbacks are cleared before
replacing Game. Initial shared missions skip the blocking solo briefing.
Save loading distinguishes the original solo/network binary layout before
converting to co-op, including the outer mod header and saved house colors.
Existing matching partner state is retained; new partners initialize only
after all saved objects exist. New controller setup also preserves planned
future campaign enemy slots not present in the current save.

Stefan observed an inactive QuantBot partner after advancing an early mission.
The partner was present, but Campaign mode only rebuilt initial scenario
buildings: a human starting with only a construction yard had no windtrap
baseline. QuantBot now detects an actual HumanPlayer sharing its house and
uses Custom/normal economic and military planning at the selected difficulty,
while still obeying mission tech/build availability. Campaign enemies retain
campaign behavior. This also migrates saved partners on their first update.
Fresh QuantBot initialMilitaryValue=-1 is a serialized pending-init sentinel:
new midgame partners no longer skip initialization because cycle!=0 or read
uninitialized initialItemCount. Existing initialized bots keep saved baselines.

Save format is 9837 and network protocol 5. Both network clients need 1.0.652;
older saves remain readable, older executables reject new saves. POSIX
DUNECITY_USERDIR optionally selects an absolute isolated profile; ordinary
user paths are unchanged. Tests used separate app IDs, profiles and ports.

Validation: dependency audits before/after Release build, ctest (588 passed,
3 expected skips), version/whitespace checks, app signature and binary hash.
New tests exercise shared controller/settings/scenario round trips, human and
bot next-mission retention, old save headers/colors, malformed headers, and
preservation of absent future enemy slots when hosting a save.
Two separate localhost clients joined Atreides, ran in lockstep (462 CitySim
valve rows and 57 day rows had identical shared prefixes), and saved a shared
campaign. A separate test loaded that save with a new QuantBot Easy partner
at tech1/cycle15747; correct initial counts and Custom mode were logged.
Accepted construction began Windtrap, Residential, Industrial, Refinery,
then further economy/power construction. Stefan also confirmed it worked.
Evidence: /tmp/coop-two-client-host.log, /tmp/coop-two-client-guest.log;
/tmp/dunecity-coop-economy-verify/ai-decisions/1789179844509552-0/events.jsonl.
No live two-machine Internet test or complete two-human campaign transition;
next-mission settings are covered by tests and the bot transition was observed
in Stefan's test. Original user profile/logs were not overwritten.

Installed /Applications/dunecity.app, SHA256 4265200d009b95f97957b823b513282f4dcafc540a4a62a3d982cc99b3f885b6.
Previous app: /Applications/.dunecity-652-fyt_4fmo/dunecity-previous.app.
No remote deployment. Existing reports/ remains unstaged.

# Original single-mission house identities — 1.0.651 (local)

Current session1789170044652756-0 loads SCENA022.INI as GameType::Skirmish.
Its loose data/scena022.ini is a Tornie import (5a172ce) that changes original
Harkonnen -> Ordos, Sardaukar -> Harkonnen, Ordos -> Mercenary. House IDs were
instantiated correctly from that wrong source. Campaign already used
openCampaignFile; the single-mission picker bypassed it outside Tornie.

INIMap now routes both Campaign and Skirmish through openCampaignFile. Original
A/H/O scenarios resolve from SCENARIO.PAK; Tornie retains its campaign resolver.
Custom games/multiplayer still use their supplied map data. No hardcoded house
swaps or save-format changes. Original SCENA022 has Harkonnen left, Sardaukar
middle, Ordos right (the user's remembered left/right order was reversed).
Verified original units, structures, teams and reinforcements consistently
reference those houses; Mercenary absent. Existing saves retain old identities;
a newly started mission in the new executable gets the fix.

Release build, full ctest suite, before/after Ninja dependency checks, version
and whitespace checks passed. No fresh interactive mission run; current game
and logs preserved. Installed /Applications/dunecity.app SHA256 c5cb7207a0fecf98d1e41d1d8cb0a020437ef32c2a747d5b0664f4a673d847e1.
Previous650: /Applications/.dunecity-651-691z6krw/dunecity-previous.app.
No remote deployment; existing reports/ remains unstaged.

# Main-base proximity for MCV expansion — 1.0.650 (local)

Stefan reported MCVs passing nearby rock and explicitly requires distance to
main base to rank first. Current648 session1789167702882520-0, Ergsun-Fwiffo
seed740015832: Harkonnen MCV243 at cycle50750 selected47,72 via105 route tiles;
MCV257 at52350 selected124,107 via211 tiles. Previous policy maximized enemy
clearance, then room, and only finally MCV travel distance, causing long detours.

Eligible rock now ranks by Manhattan map distance from the oldest surviving
active construction yard first. Enemy clearance, local room and MCV route
length only break ties. No weighted safety detour overrides base distance.
Actual MCV-ground BFS remains a separate reachability filter; own occupied and
reserved formations, unsafe tiles, <48 free/local tiles and <12 enemy clearance
remain excluded. The original yard anchors expansion until destroyed, then the
oldest surviving yard replaces it; no averaged multi-base centre. First-yard
legacy placement unchanged. Telemetry v67 includes main-base anchor, distance,
route length and nearest-main-base selection reason. Save format unchanged.

Validation: 583 tests passed, 3 skipped; dependency audits, version/whitespace
checks and app signature/hash verification passed. Tests cover near small vs
far large islands, unnecessary enemy-clearance detours, actual threats,
unreachable/reserved rock and an MCV already beside the far island. No fresh
full-match verification. Current game/orders were not modified in memory.
Installed /Applications/dunecity.app SHA256 e4f33bd7442e18cb5d9de544398a263957acfb15f5aee99c0d4e6a72debd9723.
Previous649: /Applications/.dunecity-650-ne1awl99/dunecity-previous.app.
No remote deployment. Existing reports/ remains unstaged.

# Wider lobby AI selectors — 1.0.649 (local)

Both custom lobby player dropdowns widened from100 to180 logical pixels,
including their expanded lists, to fit QuantBot and support AI names.
Release build and before/after Ninja dependency audits passed; whitespace and
strict signature/hash verification passed. No new tests for this layout-only
change; no interactive visual run. Current app was not interrupted.
Installed /Applications/dunecity.app SHA256 aff77514e5f64c43ce28fdafa93b5d7ea3dcea0e04ab05826f84d597baa68d4c.
Previous648: /Applications/.dunecity-649-qxc4o3tw/dunecity-previous.app.
No remote changes.

# Ornithopter capacity, safe defence and AI defaults — 1.0.648 (local)

Current 647 session 1789142209744263-0 (Ergsun-Prometh, seed1050788573),
Sardaukar house4: High Tech completed cycle56346 at89,37 and IX cycle63946
at80,1. Both remained at400/400 health at cycle140646. At cycle161546,
Sardaukar had ~51.8k credits but army79950/80000 and air deficit569 at a
600-credit ornithopter price. This observation does not justify forced extra
factories at the cap. Source did reveal extra air capacity was behind all
heavy expansion (unless24 heavy factories), and required every air factory
currently producing an ornithopter, hiding mixed carryall workloads.

- Additional High Tech: funded unmet ornithopter demand of at least one plane,
  >=75% busy capacity, an operational unlocked producer, aircraft/military
  headroom, funds for factory + aircraft + working buffer. Queued factories
  prevent duplicate capacity. Evaluate before optional light/heavy expansion;
  essential economy, power and civic priorities remain. No one-factory cap.
- Ornithopters raid exposed buildings first; ground units are candidates only
  when in weapon range +3 tiles of a live own building or harvester. No roaming
  unit hunts. Defence uses the same visible launcher/rocket-turret coverage
  checks as raids, including footprint and direct approach. Safety margin is
  five tiles beyond weapon range (previously two), including turning room.
  Temporarily unpowered enemy rocket turrets still prohibit attacks nearby.
- General damage-response scramble previously bypassed safe-air planning and
  could assign ornithopters directly into covered combat. Exclude them and
  discard legacy air defence assignments. Safe planner owns their orders and
  guard points. On withdrawal use a reachable safe actual owned building,
  avoiding a dangerous base centroid; inside new AA coverage find a nearest
  safe straight exit. No guarantee against hidden/moving AA or missiles
  already in flight. Ground contact checks use LocalPointIndex.
- Dropdowns lead QuantBot Easy, Medium, Hard, Brutal, Defend, then other AIs.
  Registry labels now spell QuantBot; class identifiers remain qBot* for saves.
  Campaign and skirmish launch mappings match displayed order. New campaign
  default and fallback/config-generation default are qBotEasy. Existing explicit
  saved settings remain user choices. The campaign selector visibly starts Easy.
- Telemetry policy v66 adds usable air producers / military headroom and raid
  versus defensive-intercept reasons. Derived logic only; save version9836.

Validation: 582 tests passed, 3 skipped, dependency audits, version consistency,
menu-to-launch mapping checks, whitespace checks. Coverage includes mixed air
workloads, pending capacity, tech/funds/unit-cap blockers, raid ranking and
withdrawal through/around AA coverage. No fresh full-match or interactive menu
run; the live game was not interrupted. Local app signature/hash verified.
Installed /Applications/dunecity.app SHA256
de20ef211518ab0c038928bcecc1b724e29627076ca5612c32009d342d113216.
Previous647: /Applications/.dunecity-648-6z69dkyn/dunecity-previous.app.
No remote release or website changes. Existing reports/ remains unstaged.

# Unloading queues and safe rock expansion — 1.0.647 (local)

Stefan reported harvesters queueing at refineries and three adjacent MCV
expansions instead of colonising new rock. Current 646 session
1789140497380770-0, Ergsun-Prometh seed1217567228, Rebels house7:
cycle60293 economy forecast recorded 26 workers / 3 refineries, but zero
worker income/bay capacity and an empty refinery candidate. Forecasting was
inside the new-site guard, conflating an unavailable candidate/site with no
capacity pressure. New diagnostics distinguish availability and site failures.
MCVs also auto-deployed at a legal factory exit before selecting a site;
fallback search was only +/-25 tiles with a strong distance penalty.

- Scan owned dropoff occupancy and active loaded harvesters returning within
  six tiles of a busy bay. A net queue of >=2 beyond free bays sustained for
  ten simulated seconds triggers capacity investment. A pending refinery
  suppresses duplicate queue-based orders; normal fleet-capacity forecasts
  still account for committed bays. No harvester production cap added.
- Forecast worker/bay throughput using an existing refinery if a new site is
  unavailable. Queued cargo earns conservative relief credit (at most two
  loads, capped to actual waiting cargo) without assuming additional spice.
  Profitable queue relief reserves yard funds before optional civics/defence/
  zoning. No valid site still cannot authorize an illegal placement.
- Every fifteen simulated seconds survey free rock in current build range.
  Under 48 tiles, or sustained unloading queues with no refinery site, a
  feasible new rock site raises the yard target by one. Reserve MCV money
  from other producers; the selected heavy factory saves for the MCV/unlock
  instead of spending the same funds on harvesters or optional units.
- Expansion MCVs search distinct cardinal rock formations across the map.
  Require >=48 free rock tiles locally and in the formation, a 2x2 footprint,
  and a ground route avoiding known fire/recent-loss tiles. Rank distance
  from visible enemies first, usable space second, travel distance third.
  Own occupied formations and other MCVs' reserved formations are excluded.
  Hidden enemies are not consulted as tactical observations. The actual unit
  movement engine still chooses its route; this does not guarantee its route
  follows the survey's safe route or remains safe as enemies move.
- Expansion MCVs cannot immediately deploy at the factory exit. They deploy
  at assigned sites with fresh local danger/access checks; invalid coordinates
  are never ordered. First-yard deployment retains existing behaviour. If no
  new formation exists but current base has ample free rock, ordinary local
  capacity expansion remains possible; with insufficient space, wait/retry.
  Failed MCV surveys are throttled to five seconds. All new planning state is
  derived, not serialized; save version remains 9836.
- Telemetry policy v65 adds refinery busy/bookings snapshots, waiting cargo,
  queue pressure, placement rejection details, expansion site/room/enemy
  clearance/route length and free-base-rock fields.

Validation: 580 tests passed, 3 skipped; dependency audits, version consistency,
whitespace checks and strict deep signature verification passed. Added queue
burst/free-bay/pending-bay cases and safe/new/reserved/unreachable/threatened
formation tests. No fresh full gameplay run: deployment behaviour and queue
relief must still be observed in the next game. Current 646 game untouched.
Installed /Applications/dunecity.app 1.0.647, matching build SHA256:
01352468280c24aadb6020717f765b4878188671eda7cac95b95ccd5174b4a13.
Previous 646: /Applications/.dunecity-647-1n8_vg2n/dunecity-previous.app.
Intermediate 647 backup: /Applications/.dunecity-647-final-o6q66g18/dunecity-previous.app.
No remote release or website changes. Existing reports/ remains unstaged.

# Brutal opening economy, demanded civics and police anchoring — 1.0.646 (local)

Stefan's running 1.0.645 Harkonnen Brutal game, Moshpit seed 406506788,
session 1789138202535608-0, was slow to compound. Refineries completed at
cycles 2200 and 7100; the first factory harvester at 23505 (~6.3 simulated
minutes), with two bays still at cycle 81700. At cycle 73700 the refinery
forecast was cost 461 / four-minute proceeds 689 versus R cost 109 / proceeds
53, but a worker-capable factory vetoed the refinery. The broad tax hedge
also overrode early refinery ROI, and factory priority ended at four workers,
then required military capital twice worker capital plus a tank cash reserve.

- Brutal custom city games prioritize up to eight committed workers, bounded
  by the existing remaining-spice/map target. Lower difficulty/vanilla policy
  stays unchanged. This is a priority floor, never a worker cap. Afterward,
  Brutal compares equal army/worker capital rather than 2:1; the map target
  remains authoritative. Preferred workers can use their own purchase money
  without the optional IX reserve or an additional tank reserve. Optional
  custom orders cannot preempt them. Emergency reconstruction still applies.
- Preserve the first demanded R hedge. While the opening workforce is short,
  defer the broad one-third tax hedge and permit an economically worthwhile
  third refinery despite a worker-capable factory. The pre-factory opening
  no longer buys six lots before tech; it compares profitable refineries and
  then bootstraps vehicle production. Short/unsafe spice trips still lose on
  the existing cost/power/delay/risk forecast. Beyond the opening, more bays
  require fleet throughput pressure. Included workers count as committed.
- First carryall eligibility still begins at four workers, before a second
  heavy factory, while Brutal continues toward eight. Repair/optional tech
  waits for the larger workforce floor.
- Actual NeedStadium/NeedAirport demand selects a feasible civic investment
  before optional services, production, tech and further zoning. Reserve its
  purchase price from other factories (initial worker recovery remains an
  exception), and wait for funds instead of repeatedly buying cheap plots.
  Essential power/initial economy precede it. Committed civics suppress
  duplicates; unavailable/unplaceable civics do not lock money. No premature
  airport based on total population; palace-satisfied R has no stadium demand.
- Crime at industrial zone (30,4), cycle 83600: pre-police 275, coverage 42,
  final 233, with PD (24,5), rocket (29,3), full funding and sufficient power.
  policeSource incorrectly anchored service at its first adjoining road,
  shifting the source across six-tile district boundaries. Road access now
  affects strength only; the central occupied building tile anchors service
  (lower centre for even footprints). Runtime and placement estimates share
  this helper. Existing Micropolis diffusion, strength, funding/power/road
  penalties and turret 15% contribution remain. Regression reproduces this
  boundary layout and drops below Dangerous without a strength buff.
- Policy telemetry v64 adds opening refinery/Brutal opening fields and civic
  investment/funding decisions. Save format remains 9836.

Validation: 578 test cases passed, 3 skipped via ctest; dependency audits,
version consistency, diff whitespace and strict deep app signature passed.
Four added regression cases cover difficulty/spice limits, the observed
refinery ROI veto, civic feasibility/commitments, and road-side-independent
police coverage. No fresh full match was run; next-game growth speed remains
an empirical check, not a claimed measured improvement.

Installed /Applications/dunecity.app 1.0.646 without interrupting the running
645 game. Built/installed executable SHA256:
98af57a74efb6adc8020c9dd85d7fdac7934ccfbeb3181d246c2f1ddb8198dc8.
Previous app: /Applications/.dunecity-646-ep1iz24v/dunecity-previous.app.
No remote push, release or website deployment in this task. Existing untracked
reports/ belongs to earlier game analysis and was not staged.

# Outlying placement and city reinforcements — 1.0.645 (local)

Stefan reported finished R zones stuck despite open sites in widely separated
edge districts (Harkonnen/Neutral, live 1.0.642 Moshpit game). Verified that
findPlaceLocation searched only +/-50 tiles from the arithmetic base centre.
The screenshot supports this limitation; do not claim each shown tile was
individually proved legal. Normal build-range, terrain, occupancy, pollution,
road/exit, reactor and threat restrictions remain in force.

- Placement first evaluates its existing central region, then, only if no
  suitable site exists, the remaining map origins. Passes are disjoint; both
  results use the existing per-build cache and waiting-yard scheduling.
  Successful central searches incur no broad second search. Diagnostics add
  search centre/pass and reservation/road/neighbour rejection counters;
  completed-yard deferrals include placement quality.
- Harkonnen city-sim light factories can build trikes. The existing Harkonnen
  high-tech ornithopter exception is preserved in the shared CityFactionPolicy
  helper. Tech/upgrades/enabled flags still apply. Vanilla faction restrictions
  remain unchanged. Actual build lists feed QuantBot's available unit mix.
- Police always use Palace's HOUSE_FREMEN cooldown (5 simulated minutes),
  replacing twice the owning faction's palace cooldown (20 min for Harkonnen,
  10 for most houses). Patrol remains 3 troopers + 1 trike with existing caps.
  Harkonnen's earlier JSONL already records successful one-trike patrols; it
  was factory availability, not a universal police spawn ban.
- Airports automatically deploy a pair of free ornithopters every Palace
  Harkonnen missile cooldown (10 simulated minutes). Starts with a full timer;
  requires power to deploy, observes air-unit caps and enabled units, retries
  local blocked/capped deployment every five simulated seconds. Only on-map
  unoccupied air tiles qualify (AirUnit::canPass always returns true). Partial pairs
  retain only their missing aircraft; cooldown resets after the full pair.
  AI aircraft start STOP for QuantBot's explicit safe-target controller; human
  aircraft start GUARD, not Hunt. Airport sidebar shows countdown/power/cap
  status. New airport_unit_spawned/airport_reinforcements telemetry.
- Save format 9836 adds airport countdown and pending pair count. Older saves
  give existing airports a fresh timer. New saves restore partial batches.
  Policy version v63. Existing detailed telemetry stops routine capture near
  240 MiB of its 256 MiB limit: the still-running game's events.jsonl ends at
  cycle ~541k while Dune City.log continues past 1.1m. This limits attribution
  of the latest screenshot; do not present the old JSONL snapshot as current.

Installed /Applications/dunecity.app 1.0.645; strict deep signature verified.
Built/installed executable SHA256:
a3c883454b9e01707037c02a0cd2601f56e9c262b21c5b2154d21db057744099.
Previous app: /Applications/.dunecity-645-b4u98vvi/dunecity-previous.app.
The running 642 game was not interrupted.

Validation: dependency audits passed; 574 test cases passed, 3 skipped via
ctest. Added exhaustive disjoint search-region coverage for remote outposts,
Harkonnen factory/mode restrictions, and partial airport patrol persistence.
No tactical learner/kiting changes from the preceding analysis were requested
or implemented here. No remote release or website update in this task.

# Safe ornithopter raids and base air coverage — 1.0.644 (local only)

Stefan requested that ornithopters exploit buildings/units outside launcher and
rocket-turret protection instead of entering unrestricted Hunt when numerous,
and that QuantBot build enough distributed air defence for its whole base.

The old strike selector enabled HUNT at a map-scaled aircraft threshold and
reused active targets without rechecking air defence. Only the special nearby
reactor shortcut checked anti-air. Turret defence weights included Nuclear,
Heavy Factory and Repair Yard only, so R/C/I outskirts were amenity targets,
not assets requiring protection; centre-based square distance also exaggerated
coverage near diagonal edges.

- QuantBot aircraft now receive explicit forced attacks in STOP mode. STOP
  suppresses automatic acquisition/retaliatory Hunt; UnitBase::engageTarget
  still travels/fires at explicit forced targets. There is no size threshold
  or last-stand exception. Human-controlled units retain their orders.
- Each tactical pass builds one visible enemy anti-air map (Rocket Turret,
  Launcher, Elite Launcher, Deviator), using each weapon's actual range plus
  two tiles of manoeuvre margin and the game's octile distance. Unpowered
  rockets are ignored only when the match requires turret power. Unknown
  fogged defenders cannot be inferred. Entire target footprints and direct
  approaches must be clear. No path through defended space is invented.
- All enemy ground units and real structures, including zones omitted from
  configured priority tables, are candidates; existing priorities rank safe
  choices. Aircraft can independently choose reachable safe opportunities.
  Existing targets are revalidated as launchers move. Unsafe orders are
  cancelled and aircraft return to base. A kill no longer authorizes another
  autonomous target. Badly damaged aircraft are withdrawn.
- All real base buildings now count as defence assets; surfaces, walls and
  turrets do not recursively demand protection. Reactors retain two-cover
  priority. Coverage checks all footprint corners with octile range rather
  than a square around the centre. Existing/reserved turrets prevent duplicate
  coverage purchases by parallel construction yards.
- After opening workers, peaceful city coverage receives a slot after three
  non-service construction orders; known enemy aircraft make gap filling
  urgent. This proactive coverage slot is not capped at two turrets or gated
  on crime/land-value benefits. It still requires an available rocket turret,
  legal road-preserving site, power and affordable credits plus a zone reserve.
- New telemetry: ornithopter_safe_strike, ornithopter_hold, base_air_coverage;
  performance scope ai.ornithopter_safe_strikes. Policy version v62. No new
  saved fields, random draws or per-candidate full-map searches.

Validation: 571 runnable tests pass, 3 skipped. New tests cover protected
footprints, exposed districts, clear/blocked approaches, changing launcher
coverage, diagonal range, building-edge coverage and the expanded asset set.
Ninja dependency audits passed. Installed /Applications/dunecity.app 644 with
verified deep strict signature and matching rebuilt executable SHA256
d45406a6da1a70573eaed622339e599178337d1fae0463bddf6df7f01337c72c.
Previous bundle: /Applications/.dunecity-644-bw39wsr8/dunecity-previous.app.
No running match was interrupted. This is local only,
not a pushed cross-platform release. Restart to use 644.

# Opening economy, base defence and city traffic — 1.0.643 (local only)

Current source fixes Stefan's live 1.0.642 game reports. All 568 runnable tests
pass (3 skipped), including new opening-worker and traffic regressions; pre/post
Ninja dependency audits pass. No remote release or website update in this task.

Evidence: ai-decisions/1789131423179778-0/events.jsonl, map Moshpit with
Garbages, seed 845131971. House 0 had two harvesters after eleven simulated
minutes despite ample map spice and a 120-worker target. It ordered High Tech
at 379s, then a 700-credit Repair Yard at 475s with only 118 spendable credits.
No house ordered a factory harvester in the first eleven minutes. At 1136s,
crime spawned 14 hostile troopers; repeated defence responses dispatched five
units but counted zero already committed on subsequent passes.

- Opening city economies prioritise the first four existing/queued harvesters
  before optional technology, MCVs and repair yards. This is a priority floor,
  never a cap: the map/lobby target still bounds recruitment, and mature armies
  still balance military versus workers. First carryall remains ahead of a
  second Heavy Factory after the opening worker floor.
- Count outstanding production and upgrade costs once, reserve the next worker
  purchase, stop unaffordable optional construction, and run the existing
  demand/suitability/tax hedge before optional infrastructure. Actual power
  shortages can still queue recovery power. New policy telemetry includes
  worker priority, protected cash, queued costs and yard-upgrade spending.
- Distant Area Guard attacks were released as out of range by UnitBase. Defence
  now forces travel to the contact and releases forcing on arrival. Transit
  assignments survive intervening AI passes; human control, retreat, death and
  arrival release them. The old unused escort-assignment save slot is reused
  with unchanged binary layout; old friendly assignments expire safely.
- Airport construction uses the same positive-commercial-demand-blocked bit
  as the human civic notice (commercial population >100 internal), replacing
  the AI's premature >20 check. Derived bit is recomputed, not serialized.
  Starport build-menu availability is now 10,000 displayed population for
  humans and AI (other normal build prerequisites still apply).
- Traffic changes and comparison limits are documented in
  docs/city-traffic-balance.md. A logged mid-game snapshot (cycle 172225) had
  206 heavy cells out of 249 nonzero traffic cells. These are density cells,
  not percentages of all roads. Tests validate quiet, light and heavy flows,
  decay, repeatable alternate routes and no duplicate cell stamps at turns.

Installed /Applications/dunecity.app 643; deep strict signature and executable
SHA256 match the rebuilt bundle (f95545bd5826565b11973050dc9e972a656046a6fd089162bebd72399e5a04a7).
Previous app preserved at /Applications/.dunecity-643-865wluqn/dunecity-previous.app.
The running 642 match was not interrupted or relaunched.
New code takes effect on the next launch; existing traffic values decay through
normal simulation updates rather than being silently reset on load.

# Remote release 1.0.642 — verified 2026-09-11

Published the accumulated 631–642 changes at tag `v1.0.642`, commit `29b1f5f`.
Latest local 642 commit was amended before its first push to include release notes;
it replaces the earlier local-only `df40853` ID without changing game code.
Main and fix/dunecity-ui-quantbot were fast-forwarded. Release build
[34595302140](https://github.com/VR48/dunecity/actions/runs/34595302140) passed
version verification, Linux tests and all three desktop platforms. Six assets
published at https://github.com/VR48/dunecity/releases/tag/v1.0.642.
Duplicate main build 34595302154 was cancelled; ordinary cancellation did not
stop its always-conditioned jobs, so the Actions force-cancel endpoint was used.

SourceForge mirror [34596266575](https://github.com/VR48/dunecity/actions/runs/34596266575)
verified all eight uploaded files by SHA256, advanced the dedicated `dunecity`
source branch, published `dunecity-v1.0.642`, and confirmed Windows/macOS/Linux
platform defaults at 642. No source archive uploaded; Legacy master preserved.

Website automation advanced links, then website commit `c7fb363` updated release
prose. Deploy to Droplet `34596363046` passed. Live index and dune-city pages
returned HTTP 200 and all six 642 desktop links with the corrected summary;
Android remains independently versioned at 0.2.25.
Legacy SourceForge website copy committed on its master as `c06f68f`, deployed
by SFTP temporary upload/rename, compared byte-for-byte after readback and
verified in-browser at downloads.html?updated=642. Corrected obsolete road
upkeep/shared-construction-range claims and described economy, transport and
750 HP reactors. Backup: /tmp/dunecity-sf-web-642/downloads-before.html.
Plain HTTP tooling encountered SourceForge bot filtering (403); browser
verification worked. This game repo contains the same updated legacy HTML.

Local /Applications/dunecity.app was already 1.0.642 from the implementation
turn; version and deep strict signature verified again without launching.
The earlier local-only entries below are historical checkpoints, now released.

# Nuclear plant 750 HP — 1.0.642 (local only)

Stefan revised reactor health from Palace-equivalent 1,000 to 750 HP. Default and
Tornie data now use 750; new city matches force 750 instead of copying Palace HP,
so older user ObjectData files cannot silently retain the previous balance.
Palace HP, reactor power/cost and explosion damage/radius are unchanged. Existing
saves retain their saved stats. A 900-damage reactor blast or centered palace
strike can again destroy a full-health reactor. Updated the existing balance test
and Tornie's ObjectData checksum (the earlier edit left that checksum stale).

Validation: release build, dependency audits and CTest passed (564 passed,
3 optional skips); bundled data/checksum and signature verified. Installed locally
as /Applications/dunecity.app without launching; no remote push or release.
SHA256 6499b00a8624225b37ee4902652ecc0f2608844864664cf269d1c6d17a366848.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-642.b1x3jou6/dunecity.app.

# Palace-strength nuclear plants — 1.0.641 (local only)

Stefan requested nuclear HP equal to Palace HP. Default and Tornie reactor stats
increase from 500 to 1,000 HP. New city games now copy each house's Palace HP
(including custom overrides), replacing the previous Starport comparison.
Existing saves retain their saved object-data stats; start a new match for this
balance. Blast radius/damage, price and power output are unchanged. A full-health
standard reactor now survives one centered 900-damage strike/neighbor blast with
100 HP; damaged reactors can still chain-react. Updated the existing balance test.

Validation: dependency audits, release build, CTest (564 passed, 3 optional skips)
and bundled config checks passed. Signed/hash-verified /Applications/dunecity.app
1.0.641 installed without launching; no remote push or release.
SHA256 11d24800fb5797373ef2685130ec3c37d9dd192d4841af81d83c6460e24aa066.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-641.ogqah3tq/dunecity.app.

# Earlier nuclear, recovery budgets and road reuse — 1.0.640 (local only)

Stefan approved the remaining 638 match findings with one correction: redirect
finished redundant roads to another useful gap; do not cancel/refund them.

- City QuantBot plans nuclear once demand reaches three windtraps' output and
  available power approaches the growth reserve plus one windtrap. A legal,
  unlocked reactor and committed Heavy Factory are required; first transport
  stays ahead. Save the actual reactor price before optional yard/factory orders;
  release reservations during blackouts so affordable wind can restore power.
  Count queued generation and retain reactor clearance/placement checks. Existing
  zone maturity headroom anticipates regrowth after blackouts. No tax/power stats
  changed; this is investment timing, not a blanket opening nuclear order.
- Custom city AI reviews police funding every 30 simulated seconds. Major losses
  mean >=max(3, remaining structures/10) destroyed in the last three minutes.
  With cash <2,000 and police bill >75% of tax after power, cut up to 25 points,
  targeting half that income with a 25% funding floor. Restore 25 points when
  cash >=5,000 or post-power tax covers twice nominal police expense. Support
  mode does not change the shared house budget. Added city_police_budget events.
- Fixed CMD_CITY_SET_BUDGET applying to the local viewer: resolve the command's
  issuing player's house instead. Human UI keeps its local-house accessors.
- Finished redundant/blocked roads reuse the existing connected frontage/through
  gap candidate search. Exclude other queued sites; preserve the following plan.
  Hold the finished road if no useful gap exists, retry after five seconds; only
  one maintenance/redirect attempt per build pass. Restore its queue position if
  placement fails. No cancellation or additional pathfinding; a held road can
  keep that yard occupied until a useful site becomes available.
- Telemetry policy nuclear-budget-road-reuse-v60; nuclear_investment_due state,
  save_nuclear_growth/nuclear_growth_investment decisions and road replan reasons.
  Runtime review/retry timers reset on load; save layout is unchanged.

Validation: release build, dependency audits and CTest passed (564 passed,
3 optional skips, 9,726,326 assertions). Signed/hash-verified local installation
at /Applications/dunecity.app, without launching the game.
SHA256 047992f9666163a802ff7b59fad98f0218c1849f520b6749e17e96175c67ff5c.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-640.j206fhup/dunecity.app.
Live match behavior/balance still needs a new match. No remote push/release.

# Early carryalls, refinery capacity and continuing tax hedge — 1.0.639 (local only)

Current branch fix/dunecity-ui-quantbot. Stefan requested first transport before a
second heavy and earlier R/C/I alongside spice. Completed 638 session
1789115118630650-0 confirmed 22–32 refineries and only one R per AI at ~20min;
all 106 sampled refinery choices had no processing-capacity need. High Tech
followed 3–5 heavies. It was buying refineries for workers while military
factories stayed busy; the residential hedge stopped after one plot.

- Custom QuantBot in city/vanilla prioritizes first High Tech and carryall before
  additional Heavy Factories where tech/site/air capacity allow. Save actual HT
  price; protect first carryall funds from other builders, skip optional upgrades
  until affordable, and set minimum carryall target 1 for an active workforce.
  Counts pending orders, releases the cash reserve during power loss, and avoids
  indefinite ground expansion gates when no feasible transport build exists.
- City refinery investment now follows marginal processing capacity. A busy
  worker-capable Heavy Factory does not justify another otherwise unused bay.
  No worker factory, or workforce below two, can still justify included-worker
  recovery. Needed bays which repay their full cost get priority over tax hedge.
  Existing and queued bays/workers count; added capacity cannot duplicate itself
  across yards. Workers remain governed by spice/map limit and military priority,
  never a workers-per-refinery cap. Payouts/harvesting mechanics are unchanged.
- Continuing tax hedge: forecast tax >= one third of fleet income (25% combined),
  crediting half low-density income of demanded developing/queued lots. Suitable
  R/C/I get alternating ten-second early-priority windows so other infrastructure
  still has opportunities. Normal demand/site checks remain. No saved AI state or
  added world/path scan. Investment still uses ground-trip estimates rather than
  observed queues/carryall improvements; validate balance with a new full match.
- Telemetry policy transport-tax-hedge-v59. Added income/factory-supply fields and
  first_transport_factory, save_first_transport_factory, first_carryall,
  save_first_carryall, city_income_hedge and city_refinery_capacity decision rules.

Further observations (recommendations only): save toward nuclear earlier (only
Neutral bought it, at ~67min); reduce service-budget burden after city losses
(Ordos ~50min: 695 gross tax vs 575 police/min). Road cancellations mostly cancel
redundant single road steps: 115/125 already had roads, not whole building plans.
AI frame max 18.9ms/build max 12.1ms; frame max 170ms with three >100ms samples,
including one unit-update spike 159.2ms. Full evidence in docs/city-economy-balance.md.

Validation: dependency audits, release build and CTest pass: 560 passed, 3 optional
skips, 9,726,298 assertions. Signed/hash-verified /Applications/dunecity.app installed
as 1.0.639; no game launch, remote push or release.
SHA256 d13775877db6911712b370ca70b3639993f535cfe858e3d82cf30f233d8e1d2c.
Backup /var/folders/3y/kfqmr__n2wz56wnvn919zhxh0000gn/T/dunecity-before-639.q4lh_9qp/dunecity.app.

# Double private-zone tax — 1.0.638 (local only)

Stefan chose 2x R/C/I income rather than 3x fleet parity: tax is easier and less
exposed than harvesting. Added kZoneTaxMultiplier=2 in the shared weighted tax
base. All private zone densities and partial houses receive it; Palace remains
at unboosted R+C and other government infrastructure remains exempt. Census,
actual payouts, UI and AI share the multiplier; updated QuantBot indirect
residential tax forecast to use the helper too. Policy parallel-city-economy-v58.

At 7% tax/LV128, high R/C/I yield ~104.53/104.53/83.63 credits/min; Palace
~104.53 unchanged. Power, demand, jobs, growth, road upkeep and harvesting
unchanged. No extra scans or save-format changes. Detailed fleet comparison
and rates are in docs/city-economy-balance.md.

Yard/factory review: Stefan explicitly rejected 3/4 workers-per-refinery caps.
City factories target remaining-map-spice capacity and the map harvester limit;
refinery count never caps production. Removed direct 3-per-refinery gate for
vanilla QuantBot too; its existing target/refinery policy remains. Removed city
hypothetical-refinery reserve.
Yards choose zones alongside a funded idle factory that prefers a harvester unless processing
needs another bay; busy factories leave included-worker refinery option eligible.
Capacity uses 75% ideal unloading; marginal forecast credits actual first loads
and bay relief, not existing fleet or unqueued future workers. Queued refinery
workers counted across passes. Power capital uses owned generators plus foundation.
Added decision telemetry and regressions for spare/busy factory, shared bays,
queued bays, first delivery and unsafe fields. Vanilla refinery-build ratios unchanged; factory worker gate removed.

Factory allocation clarification: a spice target is not unconditional worker
priority. Recover fewer than two committed workers first; otherwise while an
affordable military order is needed, require military value of about 2x current
worker purchase value before spending another slot on a harvester. Once army
target is met, grow to spice/map target. Includes committed queues and falls
through to military selection when workers defer. Yard forecasts use the same
choice; telemetry factory_economy_priority. No new persistent AI state.

Validation: final dependency audit/build/CTest pass, 556 passed and 3 optional
skips. Signed/hash-verified local /Applications/dunecity.app 1.0.638 installed.
SHA256 2266a41ea980f68eeba7b777935c30cbab666b18708d6c1aa3e82162bc1aadf1.
Backup dunecity-before-638.ba21axvi. No game launch, remote push or release.
Live balance still needs the next match; capacity is a throughput forecast,
not a measurement of local queue wait times. No pathfinding was added.

# Micropolis tax, Palace income and road upkeep revert — 1.0.637 (local only)

Stefan authorized implementation of the Micropolis easy tax comparison, made
Palace an R+C tax exception, and requested road costs/ownership reverted.

- Tax now uses (taxableR/8+C+I)*landValue/120*taxPercent*1.4. Only R/C/I zones
  and Palace pay. Eighth-unit census preserves partial houses until aggregate
  annual rounding; smooth per-cycle payouts and 60s year retained. This is the
  easy formula, with less intermediate truncation than Micropolis tiny cities.
  Zero land value now yields zero actual tax; unknown future-land forecasts may
  assume 128. Government jobs/demand are separate from taxable population.
- Palace contributes BOTH R and C at its occupancy tier. At 7%/LV128, high R/C/I
  are ~52.27/52.27/41.81 credits per simulated minute; Palace ~104.53. Full tier
  table and harvester comparisons are in docs/city-economy-balance.md.
- Budget, QuantBot economy/service/production forecasts and Mentat use the same
  tax base. Also fixed two extracted MentatBuildOrder variants still using gross
  population (the Mentat variant is compiled), missed by earlier exemption.
  Telemetry replaces taxable_pop with tax_base_eighths; policy micropolis-tax-palace-v57.
- Removed road upkeep at every population and the maintenance ownership feature:
  no census, deductions, road-only owners, AI road expense, or old-save inference.
  Auto frontage and city road-overlay command preserve underlying tile owners.
  Normal manually placed foundations/roads retain their tile ownership behavior;
  enemy concrete/roads still do not extend construction range. Existing saved
  tile ownership stays; cannot distinguish historical inferred owners safely.
  Road overlay/foundation/traffic behavior, build prices and police upkeep remain.
- No save-format changes or added scans/pathfinding. Latest 636 factory roles and
  normal configured zone construction timing retained. Live balance validation
  remains pending; tests stub the full world runtime scans.

- Final dependency audit, build and CTest passed: 554 tests passed, 3 optional skips.
  Installed signed/hash-verified /Applications/dunecity.app 1.0.637.
  SHA256 3898ee9a75e6a21e4b05e7147b5be467b3072d9a767d9ba72a8eee6c50615644.
  Backup: dunecity-before-637.7w3j44an. No game launch or remote push/release.

# Revised factory roles and normal zone construction — 1.0.636 (local only)

Stefan revised the mapping again after 1.0.635: Light Factory is now low-density
I; Heavy Factory, High Tech Factory and Repair Yard medium-density I; House IX
high-density C. This supersedes 1.0.634's High Tech high-C interpretation.

- Roles, caps, supply/population/emissions helpers and sidebar labels agree.
  Existing load reconciliation clamps old high occupancy automatically. Factory
  emissions follow the tiers: Light 10; Heavy/HighTech/Repair 25. IX stays clean.
- Government infrastructure stays non-taxable; private R/C/I tax formula still
  unchanged. Refinery medium I, Silo low I, WindTrap power only and Starport
  seaport remain as previously requested. Includes 1.0.635 balanced zone choice.

- Stefan's final timing instruction is normal construction timing. R/C/I use
  their configured build time through the standard BuilderBase production path,
  removing the instant-zone override. Default 40 is ~9.6 simulated seconds at
  full builder speed; house/mod overrides respected. This supersedes the earlier
  silo-time request. Roads, instant-build mode and zone prices remain unchanged.
- QuantBot tax-investment delay includes normal configured construction time
  plus its existing 60s growth/foundation allowance. Population growth itself
  is not accelerated. Regression tests cover all zones, overrides and zero-time
  safeguards. No new save fields, scans or pathfinding.

- Final build, before/after dependency checks and CTest pass: 557 passed, 3 optional
  skips. Installed signed/hash-verified /Applications/dunecity.app 1.0.636,
  SHA256 7194a3cd6eece9e4a0cb8224e9ac1056d262e535ae992743a02a07eb1b24bc59. Backup: dunecity-before-636.66dh8ro3.
  No launch/interruption of ongoing game, no remote push or release. Runtime
  balance still needs a subsequent live game; tests stub the full world scans.

# Balanced R/C/I selection — 1.0.635 (local only)

Stefan's current game screenshot showed R -1110 / C +1360 / I +1500 and little
industry. Session 1789107959898835-0 is actually 1.0.634, DuneCity 192x192, seed 460150850.
Snapshot at ~cycle 82000: 49 of 52 evaluations with R<500 and both job demands
positive selected C; 3 selected I. House2 had built 32 R / 15 C / 5 I, so industry was
suppressed intermittently, not universally unavailable. Example cycle 21248:
5 R / 1 C / 1 I, demand -530/1500/1419, I marked lower_rank_not_evaluated. Another at
cycle 71648 chose R via infill despite demand 1685/224/1500 and counts 18/10/5.

- Removed the hard C-before-I 500 gate. Normalize demand maxima (R 2000, C/I 1500).
  Among positive candidates within 20% of strongest demand, choose underprovided
  built+queued plots with the existing 3:1:1 R/C/I tie-balance. Stronger demand
  wins outside that band. A fixed strongest reference preserves sort transitivity.
- Removed unconditional residential infill promotion across zone types. Existing
  placement scoring still favours gaps for housing when R is the selected need.
  First demanded R hedge preserved; no forced missing C/I at nonpositive demand.
- No new scans/state/pathfinding. Existing suitability/site fallback and marginal
  tax/refinery investment comparison remain. Telemetry rule now
  normalized_demand_band_then_committed_balance, policy balanced-zone-demand-v56.
- Regression cases reproduce screenshot and log states, queued commitments,
  sustained slightly unequal positive C/I demands, and near-zero I exclusion.
  Includes 1.0.634 government tax and employment changes; Micropolis formula
  remains a comparison only. Live post-fix balance validation pending.

- Build and before/after dependency audits passed; CTest 556 passed, 3 optional
  skips. Installed signed/hash-verified /Applications/dunecity.app 1.0.635,
  SHA256 e04112fdbd9b739e98bb6a85633e2506f69c608b0ca394f83655e0d751825230. Backup:  dunecity-before-635.4xsde6uo.
  Did not launch/interrupt the live 1.0.634 process. No remote push/release.

# Government tax exemption and infrastructure roles — 1.0.634 (local only)

Stefan requested Micropolis-equivalent R/C/I numbers (comparison only) plus
non-taxable government infrastructure and revised employment tiers.

- Only actual R/C/I zones pay tax. All government/non-zone infrastructure is
  excluded, retaining its other jobs/population roles. Partial R lots pay for
  actual houses. Gross city population still governs unemployment and the
  under-2000 road-upkeep exemption. Existing tax rate/time conversion unchanged.
- WindTrap no longer supplies industry. Light Factory/Refinery cap at medium I;
  Silo low I; Heavy Factory/Repair Yard high I; High Tech high C (final user item
  overrides its earlier contradictory high-I listing). Starport stays seaport.
  Sidebar role labels corrected, including old Refinery/Starport mislabelling.
- Tiers clamp jobs/population/emissions and loaded occupancy; existing aircraft
  manufacturing emissions retained for High Tech's commercial employment.
  Derived taxable census added to existing scans, payout, budget and QuantBot /
  Mentat income forecasts, including service-investment tax gains. No new scan
  or serialized field; telemetry adds taxable_pop for comparisons with R/C/I.
- Micropolis easy at tax7%, LV128 would yield high R/C/I approximately
  52.27/52.27/41.81 annually, also per simulated minute with the existing 60s year.
  Current zoned high R/C/I remain 186.67/23.33/18.67. R is 3.57x Micropolis;
  C/I are 0.446x. Formula restructuring has NOT been applied. Full tier table and
  government scope are in docs/city-economy-balance.md.
- Local build and dependency audits passed. CTest: 555 passed, 3 optional skips.
  Role/tier regression
  tests cover old occupancy, jobs versus tax, government structures and partial
  R lots. Runtime scans are stubbed in the test target; a live match remains
  necessary to validate long-term economy balance. No push or remote release.

- Installed /Applications/dunecity.app 1.0.634 without launching the game.
  Signature verified; executable SHA256 matches the built bundle:
  b043ad88c521adb18123d12acb18fa957b74ff5bc7a3091ffe914e5ce5309fe1.
  Previous app backed up in temporary dunecity-before-634.x8wfd95b directory.

# Demand-led tax/spice investment — 1.0.633 (local only)

Stefan wants an opening R tax hedge, no forced one-each R/C/I seed, and an
explicit refinery-versus-tax comparison that recognizes existing bay capacity.
The 1.0.631 Habbanya-Penny log 1789102780668848-0 includes opening C orders at
zero demand. I was positive at its recorded initial orders; the later screenshot
alone does not prove I was ordered at negative demand. Source rankZones did
explicitly allow missing types with zero/negative demand; that override is gone.

- First refinery retained for income/technology. Then a demanded R hedge;
  missing C/I no longer prerequisites for first vehicle factory. Opening and
  ongoing custom-city investments use one four-minute proceeds-per-credit model.
- No additional refinery investment if current/queued bays already cover the
  sustainable near-term fleet (workers plus three, capped by sustainable target).
  Compare marginal delivered spice, not a whole fleet credited to a new bay.
- Tax candidate uses live positive demand and existing valid-site selection,
  price/foundation/generation cost and future road upkeep, low/medium growth with a 60-second delay,
  tax/house land value, pollution/crime, pending/undeveloped same-type lots and
  limited job-enabled residential tax for C/I. Refinery includes fill/unload and
  construction delays, local sampled trip distance, spice share and danger.
- Vanilla economy retains its prior refinery policy. No extra pathfinding or
  save fields; bounded local spice sampling once per build pass. Telemetry-only
  per-yard throttle added, policy demand-tax-spice-investment-v55. Sampled
  city_economy_comparison exposes every forecast component and selected item.
- Tax amounts/budget balance unchanged. See docs/city-economy-balance.md for
  Micropolis comparison and exact cycle-based harvester equivalences. Budget
  /60 matches payouts, but excludes separate power/unit/construction costs.
  Raw R taxation makes mature R ~3.57x Micropolis easy-mode annual revenue;
  C/I ~0.446x, with upkeep largely retaining original scale.
- Local build, before/after dependency checks and CTest passed (553 cases,
  three optional skips). Includes local 631 power and 632 road fixes. No live
  match validation yet; forecast heuristics need follow-up logged matches.
- Installed /Applications/dunecity.app 1.0.633, signature checked and executable
  hash matched to build bundle. Previous app backed up under temporary
  dunecity-before-633 directory. No launch/interruption, remote push or release.

# Enemy roads are foundations, not construction anchors — 1.0.632 (local only)

Stefan clarified that enemy roads must behave like enemy concrete: a house can
build over them within its own normal build range, but cannot expand from an
unrelated enemy road network. This supersedes 1.0.630's shared-access rule below.

- Map::isWithinBuildRange again recognizes only tiles owned by the constructing
  house, with the existing two-tile search. Road flags and city mode do not grant
  additional reach. Applies equally to humans and every AI.
- Road foundations remain prepared ground, independently of owner; footprint
  placement retains terrain/occupation checks. Building on a road inside normal
  range clears the road under the footprint and stamps the new building's owner.
- MCV deployment remains unchanged: its new construction yard creates the owned
  foothold, allowing normal building around it. Enemy roads beyond that range
  do not extend the foothold. Existing road upkeep ownership is unchanged.
- Replaced the regression that endorsed map-wide shared road access with
  owner-only reach cases, retaining foundation and placement integration checks.
  No new scans, pathfinding or save fields. Policy owned-construction-reach-v54.
- Local build/dependency audits passed; CTest550 passed, three optional skips.
  Includes 1.0.631 power planning. Installed /Applications/dunecity.app version
  1.0.632, signed and executable verified against build bundle. Prior app backed
  up under a temporary dunecity-before-632 directory. No game launch/interruption.
  No remote push/release this turn; published 1.0.630 still has the old road rule.

# Growth-aware nuclear investment — 1.0.631 (local only)

Reviewed completed city 1.0.630 session 1789087168596776-0, SCENA021.INI,
62x62, ending cycle180523 (~48.14 simulated minutes). The logged QuantBot is
house1. Of 41 generator-choice records, 40 chose wind and one nuclear. At40.75min,
it chose wind with234,226 spendable credits, need489 and a valid reactor site:
five100-output windtraps cost1,500, below the2,000 reactor, so the old incremental
cost rule ignored the wealthy city's need for reserve capacity. 34 records had
nuclear_site=false; old telemetry combined unavailable tech and rejected sites,
so it cannot prove which placement restriction caused each failure. A completed
reactor was placed through power-recovery fallback at29.81min, risk500.

The log contains1,446 power_shortage decline events (all reduce population;
1,236 also reduce density). Only2/95 periodic snapshots showed deficits, so
snapshot averages hide short blackouts and subsequent zone shrinkage. The old
30-second trend discarded forecast growth when density/power demand fell.

Changes:
- Account for every owned zone's mature density3 load minus its exact registered
  power draw (including individual residential houses), plus mature queued zones
  and other queued consumers. Use the larger of latent zone load and observed
  two-minute growth, avoiding double counting. This headroom drives the early
  power trigger as well as generator selection, retaining normal reserve.
- When additional power is needed, prefer an affordable reactor during a deficit
  or when spendable cash after queued orders covers five reactor prices plus
  working capital (normally ~10k). Small funded starts still compare wind cost
  and space; exact cost parity now favours nuclear. Pending-generator guards stay.
- Reactor search ranks safe separated sites first; known threat/loss halos and
  four-tile blast clearance are preferences rather than blanket reactor vetoes.
  If none is safe/separated, use the best remaining legal footprint. Terrain,
  occupied buildings, ground exits, roads and neighbouring access remain checked.
  New critical buildings retain the separation veto beside existing reactors.
  Completed generator placement uses the same separation preference. Existing
  redevelopment paths remain conservative. Nuclear placement risks are logged.
- Add zone current/mature draw, committed load, growth headroom, shortage,
  nuclear availability and detailed candidate rejection/risk telemetry. Skip the
  extra wind-site counting scan when wealth/recovery already decides nuclear;
  zone loads share the existing structure loop. Policy zone-growth-power-v53.
- Regression tests cover the recorded rich-city decision, protected cash,
  blackout-recovery load invariance and reactor safety/separation ranking.
  CTest550 passed/3optional skips; dependency audits and build passed. Save layout
  unchanged. No final full-match replay validation or remote release this turn.
- Signed 1.0.631 installed at /Applications/dunecity.app; executable matches
  build/bin/dunecity.app SHA256
  b55294a1d3f74cda0696f46292b7f98a0ca5a55ba7c192948c238341c7ab1082.
  Prior app preserved in temporary dunecity-before-631.28kyr3x6 directory.
  No running match interrupted and no launch used for version verification.

# Shared city road access and desktop release — 1.0.630

Stefan requested enemy roads be reusable, then authorized local/remote builds
and both website updates, including accumulated 1.0.627–629 fixes.
- Roads already supplied prepared foundations independent of owner, but
  Map::isWithinBuildRange only recognized owned tiles. In city mode any road
  now supplies construction reach within the existing two-tile BUILDRANGE.
  This is shared access, including enemy/abandoned road networks; roads do not
  need to be connected to an owned network. Human/AI validation uses the same rule.
- Ownership/upkeep remains unchanged, enemy bare ground/concrete grants no reach,
  Vanilla retains owned-tile reach, and footprint occupation/terrain/AI lane and
  threat checks still apply. Strict foundation checks now accept roads as slabs.
  No path searches or new map-wide scans. Regression cases cover owners, modes,
  foundation/occupancy wiring. Policy shared-road-access-v52, save layout unchanged.
- Before/after dependency audits passed; CTest 547 passed, three optional skips.
- Local 1.0.630 installed at /Applications/dunecity.app, signature verified;
  executable matches build/bin/dunecity.app with SHA256
  fa704a707098914af1e82bd6159249123b3c548dda8484d0d249ed036c0bee82.
- SourceForge legacy downloads copy committed to old Git master e5fe423 and
  pushed. SFTP atomic upload backed up previous HTML under
  /tmp/dunecity-sf-web-630/, verified readback SHA256
  ecf6869ee156ac79918ba4ca93705173464a8ceee6f767796f01372a3992ee8d.
  Public browser page verified with ?updated=1.0.630.
- Game release tag v1.0.630 is 1339839, pushed to GitHub main and working branch.
- Remote build 34485222429 succeeded: Linux tests and all three desktop builds,
  six GitHub release assets verified (ZIP, DMG, AppImage, DEB, RPM, tar.gz).
  Redundant main build 34485222489 cancelled; tag build performed required checks.
- Release notes published from releases/desktop/1.0.630.md (added in follow-up
  docs commit f45314b; immutable release tag remains 1339839).
- SourceForge auto mirror 34486418547 succeeded. After release-note polishing,
  idempotent mirror retry 34486748145 verified eight uploads and all three OS
  defaults. Source branch dunecity and tag dunecity-v1.0.630 point to 1339839.
  No source archive uploaded.
- Main website automation commit 3a2b5a1 updates versions/links; prose commit
  e05c679 rebased onto it and pushed. Deploy 34486697186 succeeded. Both live
  pages returned HTTP 200 with new prose, desktop 1.0.630, Android 0.2.25 and
  all six package URLs matching the published assets. SourceForge web deployment
  and browser verification noted above. Completed 2026-09-11 Australia/Sydney.
- Restart the local game to load 1.0.630; no running match was interrupted or
  launched for verification. Automated validation passed; no full-match replay
  of the final release was performed.

# Repair-yard crash and earlier QuantBot repair support — 1.0.629

Crash evidence: `~/Library/Logs/DiagnosticReports/dunecity-2026-09-10-232316.ips`
(copied to `/tmp/dunecity-repair-yard-crash-20260910.ips`). Main thread crashed
with SIGSEGV at address 0xa0 in RepairYard::updateStructureSpecificStuff()+88.
The executable UUID 826D9D79-4767-3BA0-AB13-FF448179719F matches the saved 1.0.627
binary in `/tmp/dunecity-before-628.8MFLeK/`, despite the report's bundle metadata
saying 1.0.628: the running 627 binary remained mapped when its on-disk build
bundle was updated. Screenshot and crashed telemetry session agree on 1.0.627.

Crashed city session `1789045974757020-0`, Sardaukar Base, ends at cycle149250
with reactor9573 detonating and destroying launcher8051, the object ID held in
crash register x8. Nuclear damage walked inactive ground units, including repair
occupants still holding old map positions. RepairYard dereferenced the expired
ObjectPointer without checking it. The current Dune City.log was already replaced
by a later launch; use the macOS report and immutable session above for evidence.

- Nuclear blasts now target active ground units only. Cargo in repair/refinery/
  carryall storage is not independently hit at stale positions; a destroyed host
  retains its existing occupant-destruction behavior.
- Repair yards resolve a valid GroundUnit before update, deployment or destruction.
  Missing occupants clear repair state/animation and release the booking once.
  Late carryall pickup is safe; booking decrements cannot underflow. Clear state
  before handing off/destroying a unit. Save layout unchanged.
- Regression tests exercise disappearing occupants, repeated cleanup, other booked
  arrivals, subsequent reuse, empty booking counts and stored-unit blast exclusion.

Latest ongoing Vanilla session `1789046664718549-0` showed Atreides at3.07min
with one heavy factory/6,100 army value and no repair yard; at6.11min nine heavy
factories/71,468 credits and still no yard. First repair order6.16min.
- Custom QuantBot now selects feasible affordable baseline repair capacity before
  repeated factory/tech expansion after an operational heavy factory and refinery.
  Keep 1,000 credits beyond yard cost. First yard does not wait for saturation.
- Baseline is bounded by the existing one-per-two-heavy-factories, max-four cap,
  and increases with army value (one additional slot for each >8,000 value).
  Built and queued yards count. Existing saturation rule can still add capacity.
  Latest-game examples: one heavy/6,100 -> one yard; four heavy/8,050 -> two;
  fifteen heavy/29,050 -> four. Added regression cases and repair_baseline telemetry.
  Policy `early-repair-capacity-v51`. Includes prior ornithopter/harvester fixes.
- Validation: before/after dependency audits and CTest passed (545 passed, three
  optional skips). Signed local build and Applications install both 1.0.629,
  executable hashes match. Current game left running; fixes apply next launch.
  No full-match replay validation and no remote push/release performed.

# Less conservative adaptive spice fleet — 1.0.628

Stefan explicitly requested a modest adjustment to calculated harvester targets,
not special lobby-override behavior or a fixed 120-worker target.

Measured completed Vanilla session `1789043602798877-0` using Atreides snapshots,
map spice deltas and all-house worker counts. At 14.21 minutes: 711,263 spice,
189 harvesters across the map, 75 Atreides workers. Trailing ~2-minute depletion
was 75,903 spice/min, implying 9.4 minutes remaining at that rate; old Atreides
spice target 71. At 15.23 minutes: 638,690 spice, depletion 70,704/min, runway
9.0 minutes, old target 63. These are measured aggregate depletion rates and
constant-rate estimates, not guarantees of accessible spice or future duration.
Over 10.16–14.72 minutes, the map averaged 182.7 harvesters and 415 spice removed
per worker per minute; Atreides refined 301 credits per worker per minute.
Removal and refinery income differ because of cargo in transit/losses and other
map effects; do not equate them.

- Reduced desired spice-per-worker by 25%: Vanilla 2,000 -> 1,500; city 3,000 ->
  2,250. Targets increase about one-third where not capped. Five-house Vanilla
  examples: 711,263 spice -> 94 workers; 638,690 -> 85; 215,370 -> 28.
- Equal-share calculation, lobby/engine caps, existing global low-spice limit,
  refinery throughput, budget, queue and factory ordering rules remain intact.
  No override bypass and no production batching changes. Includes 1.0.627's air fix.
- Added actual-match and depletion/cap regression cases. Policy telemetry tag
  `spice-worker-runway-v50`. No save-format change.
- Before/after dependency audits passed; CTest 541 passed, 3 optional skips.
  Built and installed signed local 1.0.628; Applications executable SHA-256 matches
  the tested build. Existing running game left alone; new policy applies next
  launch. No remote push/release performed.

# Fund ornithopter production before ground overflow — 1.0.627

Reviewed last completed session `1789043602798877-0`: Vanilla 1.0.626,
`5P - 128x128 - All against Atreides`, 99,898 cycles (26m38s), ended manually.
Atreides built 0 ornithopters, despite 14.4% target when first available (7.60min)
and final 11.42%. IX completed at 7.15min. Of 42 sampled high-tech decisions:
22 spendable-below-air-threshold, 13 unavailable, 6 factory-busy, 1 carryall-priority.
All 18 accepted high-tech orders were carryalls. Carryalls took priority while
rich; queued/ground spending plus the 2,000 reserve then starved the >1,200 air
cash gate despite aircraft costing 600. Two destroyed high-tech factories also
caused temporary unavailability. Enemy house 3 sonic tanks were identified by
object-ID/type records as attackers in 62/76 Atreides harvester lethal-hit events.

- Priority: city ready/idle yards retain 3/2; high-tech factories 1, light 0,
  remaining structures -1. City yard rotation remains unchanged. Aircraft now
  access their share before ground factories' overflow spends it.
- Pure AirProductionState/chooseAirProduction policy: bootstrap first carryall;
  otherwise unmet affordable combat air precedes additional carryalls. Retain
  prerequisites, upgrades, queue, air and military caps, economy/strategic reserves.
  Use actual unit price after reserves instead of fixed >1,200 threshold. Once
  air target is filled (including queued aircraft), continue carryall production.
- Only accepted orders update planned counts/cash/military. Carryall orders now
  deduct planned cost too. Diagnostic reasons match policy, include availability
  and military cap, and report the same vehicle-plan air target used for selection.
  Policy version is `fund-air-allocation-v49`; no save format change.
- Regression cases cover ordering, carryall starvation, exact-price affordability,
  queued air saturation, both caps, reserves, busy/upgrading and tech prerequisites.
  Local 1.0.627 build, before/after dependency audits and CTest passed: 543 cases,
  540 passed, 3 optional skips. No game launched; live-match behavior still needs
  observation. Installed `/Applications/dunecity.app` 1.0.627; signature verified
  and executable SHA-256 matches the tested build. No new remote release performed.

Final Atreides production / reward-to-lost-value / vehicle-value target:
Launcher 255 / 3.16 / 51.52%; Sonic 122 / 1.76 / 21.61%; Siege 38 / 0.79 / 6.59%;
Tank 56 / 0.70 / 5.47%; Quad 48 / 0.34 / 2.16%; Trike 37 / 0.19 / 1.23%; Orni
0 / no evidence / 11.42%. Also 79 harvesters, 18 carryalls and 7 MCVs produced.
Soldiers/troopers had zero production events but 255/39 losses from starting/free
units; those losses must not be presented as paid infantry production. Counts
were cross-checked against unit_produced and final game_summary.

# Verified desktop deployment — 1.0.626 (2026-09-10)

- Released tag `v1.0.626` at `d1b30c3` to GitHub main and the working branch.
  Build run `34473274658` succeeded: Linux tests and Windows/Linux/macOS builds;
  all six desktop packages are uploaded. Redundant main build was cancelled.
- Local build and `/Applications/dunecity.app` both report 1.0.626. The old
  Applications copy was stale (plist 0.01); replaced after preserving a backup
  under `/tmp/dunecity-before-626.*`. Installed binary SHA256 matches the tested
  build; ad-hoc signature verification passed. No game launch/log truncation.
- Main website release links and new feature copy are deployed, website commit
  `e466be2`, successful deploy `34474219132`. Both public pages returned HTTP200,
  version 1.0.626 and new road/police text. Android remains independently 0.2.25.
- SourceForge mirror run `34474087516` verified all eight files (six packages,
  README, SHA256SUMS), advanced `dunecity` and `dunecity-v1.0.626`, and confirmed
  Windows ZIP, Mac DMG and Linux AppImage defaults. No source archive uploaded.
- SourceForge website copy committed to Legacy master as `91b6b9a`, pushed,
  backed up, deployed by SFTP temporary upload/rename and compared byte-for-byte
  after readback. Public browser confirmed new copy using
  `downloads.html?updated=20260910-626`; curl was blocked by Cloudflare and the
  plain web-tool URL returned stale cached HTML. Both tracked HTML copies match.

# Starter-city road exemption — 1.0.626

Road upkeep is waived while an individual house's displayed total population is
below 2,000. It starts at exactly 2,000 and stops again if population drops below
that threshold. Billing, budget forecast and AI telemetry use the same rule;
the budget explicitly says roads are free below 2,000. Existing road census and
ownership rules from 1.0.625 remain. Police patrols remain 3 troopers + 1 trike.
Local 1.0.626 build and before/after dependency audits passed. Full CTest:
539 cases, 536 passed, 3 optional skips, including population threshold/reversal
and per-house checks. Game not launched. User subsequently authorized remote
release, both websites and SourceForge; publication is verified above.

# Road upkeep and smaller police patrols — 1.0.625

- Added road maintenance using Micropolis `simulate.cpp::collectTax/doRoad`:
  annual cost is `floor((road tiles + heavy road tiles) * 0.7)`. This uses
  Micropolis's default EASY city rate (DuneCity has no city difficulty selector).
  Heavy traffic begins at 192, matching the road animation. Roads remain 1x1,
  so the 2x2 zone conversion does not scale their cost. Funding is fixed at 100%;
  this change does not add underfunding controls or deterioration.
- Census shares the existing budget map walk; no additional per-cycle map scan
  or UI map scan. Derived per-house counts are not serialized. The budget shows
  physical/heavy road counts and annual cost, included in Services and Cash Flow.
  Road charges use `House::takeCredits` after the existing tax/police settlement,
  so upkeep can consume city, spice and starting funds. No debt is introduced
  when funds run out. Telemetry records `roads_due` and actual `roads_charged`
  (also included in `spent_total`), plus road counts/expense in AI city health.
- Automatic perimeter roads and the legacy road tool now set existing Tile owner
  metadata; existing owned roads keep their owner. On load, old unowned roads
  infer ownership only from adjacent structures (nearest, house-ID tie break).
  Isolated unowned map roads remain public/unbilled. Saved owner metadata needs
  no save format change. Removing/building over a road removes it from the bill.
- Police deploy 3 individual Unit_Troopers and 1 Unit_Trike (four units total),
  replacing 9 troopers, 2 trikes and 1 quad. Tooltip and sidebar match. Existing
  cooldown, per-unit 250 military unit checks, QuantBot military value cap and
  blocked-spawn behavior are unchanged. Policy tag: road-upkeep-police-patrol-v47.
- Local app built as 1.0.625. Before/after Ninja dependency audits passed;
  full CTest: 538 cases, 535 passed, 3 optional skips. New regressions cover
  rates, traffic threshold, aggregate rounding, ownership/migration and fractional
  charges. Game not launched, so live visual/gameplay verification remains.
  No push or release requested.

# Windtrap sidebar label — 1.0.624

Corrected the remaining hard-coded `Role: I-medium` text in CityStatsBox to
`Role: I-light`. Simulation was already light in 1.0.623 (population 1,
maximum level 1, industrial supply 10); power and emissions are unchanged.
Local app rebuilt as 1.0.624. Full CTest and before/after dependency audits
passed. No game launch, push or release.

# Occupancy-based traffic, individual houses and city AI priorities — 1.0.623

User screenshots of tiny 1.0.622 settlements showed widespread heavy traffic.
Every occupied city-role structure emitted a successful road journey every city
day. Micropolis `zone.cpp` instead tests R population > random(35), and C/I
population > random(5); its random upper bound is inclusive. Traffic generation
now uses those probabilities (R pop/36, C/I pop/6) with a deterministic site/day
hash. Existing connectivity checks, route sampling (+50 at moves 2/4/6...),
240 cap, 24/34 decay and display thresholds (64 light, 192 heavy) remain.
This corrects trip frequency; it does not guarantee small shared bottlenecks
can never be heavy. Old inflated traffic clears naturally when no longer fed.

Residential zones now store real occupancy: 0–8 houses, then 16/24/32/40
apartment population, and the reverse on decline. The eighth house upgrades
only above local population density 64, as in Micropolis. Existing DuneCity
score, demand, pollution, supply and power gates still govern growth.
- Each successful free-lot growth adds one visible house; decline removes one.
- Counts feed population, demand, density, taxes, traffic, local supply, power,
  crime-service investment and telemetry. House lots are not vacant when
  scoring redevelopment. Sidebar shows Houses n/8.
- 29 residential models per land-value tier are packed in two 15-column rows;
  all zooms remain within the 2048px texture ceiling. Original 2x2 zone/1x1 road
  footprints remain. Civic overlays retain apartment-only eligibility.
- Save format 9835 adds one occupancy byte per ZoneStructure. Older saves
  consume no byte and migrate tile tiers to their existing 0/16/24/40 count;
  new saves preserve individual houses and the 32-pop apartment stage.

Additional requests during this change:
- Windtraps are light industry: maximum occupancy 1, industrial population 1
  and local job supply 10 instead of medium 3/25. Power/emissions unchanged.
  Loaded windtrap occupancy is reconciled; population/supply helpers also
  clamp older medium values.
- Zoning favours C when R <500; if R and C <500, favours I with positive demand.
  Residential infill only overrides other choices at R >=500. Missing bootstrap
  roles still take priority; unavailable/unsuitable land falls through normally.
- Police placement penalises every built/planned station within 12 tiles,
  with a stronger quadratic near-neighbour cost. Crime utility favours
  underserved properties. Actual coverage remains additive and unmodified.
- Service planning rebuilds the small 6-tile police map once per shared bounded
  search from current buildings plus reservations. This includes recently
  completed stations before the next city scan, with original sum-then-smooth
  rounding. Removed the old alternate unbounded police-site search; all police
  construction paths now use the same bounded scorer and overlap penalties.
  Exceptional crime may still justify overlapping stations; there is no spacing ban.

Live log evidence: partial session `1789037053114457-0` had 143 station investment
choices at inspection, 18 with overlap cost >=300 (roughly within five tiles of
an existing/planned station). The old nearest-only cost was easily outweighed
by thousands of utility points. Counts are a read of an ongoing log, not final
match totals. Policy telemetry is `occupancy-traffic-houses-v46`.

Validation: full CTest 534 cases, 531 passed, 3 optional skips; occupancy
progression/save stream alignment, sprite reachability and single-house changes,
occupancy trip probabilities, sparse/light vs busy/heavy roads, cluster costs,
500-demand boundaries and windtrap migration. Atlas reproduction, source/app
atlas hashes, bundle version, git whitespace and before/after Ninja dependency
audits pass. Logs: `/tmp/dunecity-houses-{build,tests}.log`.
Local `build/bin/dunecity.app` is 1.0.623. No game launch, Applications copy,
remote push or release. Live visual/game balance verification remains for the
next run; automated checks are not a claim of measured live FPS improvement.

# Bound repeated city AI placement work — 1.0.622

Completed game `1789031518687887-0` ran 1.0.620 (192x192 SimCity,
seed 1929923577, ended cycle 208163). Imported 181834 events into
`/tmp/dunecity-620-performance.sqlite`: 245 performance windows, zero dropped
samples. Worst frame was 340 ms, including 316 ms in AI. In the final active
minute, 108 frames exceeded 100 ms and 96 included AI over 100 ms. Construction
planning consumed 319 seconds across the session; turret placement (169 s) and
service investment (130 s, includes service site search) explain about 94%.
At cycle 196248, eight yards emitted 24 ineligible service candidates. This is
the measured cause of the recurring hitches. Paths remain a separate background
cost (8.7 ms/frame in the last minute). Rendering averaged 3.8 ms, city 1.9 ms.

Changes in 622:
- Check turret caps, enemy presence, power and affordability before expensive
  location searches where those guards previously came afterwards.
- One service search per house build pass scores police/rocket sites for all
  three selection modes together: normal, emergency and tax-value investment.
  Yards reuse positive and negative results. Each caller still filters its own
  build availability and spending reserve; the first yard's upgrade level must
  not suppress a later yard's rocket option.
- One city defensive turret search per build pass, shared between yard rules
  and placement. Reserved coverage for crime targets is computed once per
  target rather than again for every proposed tile.
- Each search examines at most 4096 origin tiles per item in a deterministic
  rotating batch. Candidate-mask generation is restricted to the same rows.
  All map tiles, including partial edge batches, remain reachable over a sweep.
  These are best-in-batch choices, not a full-city optimum every pass. A failed
  batch means try a different batch next pass, not that the city has no sites.
- New reservations, redevelopment and actual placements invalidate cached
  results without replenishing the pass budget. Later yards defer additional
  expensive searches; ordinary building choices/production keep running.
- Completed CY items take priority over new plans, with deterministic rotation
  within each CY priority group. For blocked reserved turrets, advance the map
  batch only after all ready yards have had a turn; this avoids scan/yard-count
  resonance stranding a yard on the same map strip forever.

Scheduling derives from simulation cycles, never elapsed wall time. Caches and
ready-yard count reset/derive within each build call; save format is unchanged.
City effects, service strengths, overlap rules, road access and unit pathfinding
are unchanged. Vanilla retains its existing placement search; cheap guard
reordering also applies there. Policy telemetry is now `bounded-city-planning-v45`.
New performance counters: `service.cache_hit`, `service.search_deferred`,
`service.scanned_tiles`, `turret.cache_hit`, `turret.search_deferred`,
`turret.scanned_tiles`. Compare these plus existing timed scopes in the next game.

Tests cover complete bounded map sweeps, edge cells, negative cache sharing,
geometry invalidation without renewed work, distinct reservation keys, ready
placement priority, fair yard rotation and every blocked yard visiting every
batch even when the yard count equals the batch count. Build/test logs:
`/tmp/dunecity-planning-{build,tests}.log`. Full CTest: 527 cases, 524 passed,
3 optional skips; before/after dependency audits and version/app metadata passed.
Local app 1.0.622; no live-game launch,
Applications copy, push or release. Actual FPS improvement needs a new game.

# Route-based traffic density and Micropolis decay — 1.0.621

Fixed the traffic animation's inflated input rather than raising sprite
thresholds. In the620 game1789031518687887-0 snapshot59905,5150of6491road
tiles were heavy (79.3%). Root causes: BFS discovered branches were all stamped,
successful destination duplicated, every visited tile added50into2x2cells,
and city-role buildings emitted a radial level*25 traffic halo.

TrafficSimulation now uses CityTrafficPolicy::RouteFinder, which preserves
existing deterministic N/E/S/W BFS connectivity/distance limit but reconstructs
only the successful route with parent links. Failure/NoRoad clears previous
path. Reusable generation stamps and vector queue avoid full-map visited clears
and per-call queue allocations across zone searches. No RNG or unit A* changes.

CityTraffic::addJourney samples moves2,4,6,... from perimeter start (route[0]),
matching original Micropolis tryDrive's dist&1 sampling for2x2traffic cells.
Actual road samples add50 capped240; turret connectors remain traversable but
only road tiles receive density. No extra global cell deduplication: sampling
matches original, including turns which can revisit a density cell.
runEffectsScans retains accumulated traffic and calls decay once per city day,
once per cell: <=24→0, >200→minus34, otherwise minus24. Removed full layer reset,
building halos and per-road minus15. Growth phase adds journeys after decay.
Original refs: MicropolisEngine/src/traffic.cpp and simulate.cpp::decTrafficMap.

This ports density sampling/decay, not original random-walk route selection,
probabilistic journey frequency or absolute calendar cadence. Existing BFS,
2x2zones,1x1roads, zone connectivity/growth checks, animation thresholds64/192
and animation speed remain. Traffic pollution/status use corrected density.
Traffic layer remains derived/unserialized: loading rebuilds it from journeys;
it warms up over subsequent days. No save format change.

New shared-policy tests exercise branched/looped road networks, deterministic
ties, distance bounds, failure/reset/map resize, exact sample positions, true
congestion/cap, nonroad connectors, decay thresholds and partial edge cells.
Full CTest523cases520passed/3optional skips; dependency audits and621version/app
metadata passed. Logs /tmp/dunecity-traffic-{build,tests}.log. Local app built,
no live game launch/Applications copy/remote push/release. Restart to load621.

# Performance investigation and session telemetry — 1.0.620

Last completed game1789026476214205-0 ran1.0.618,192x192 SimCity map,
Harkonnen0 vs Sardaukar4,109737cycles. It predates the619 animation port.
Last full120s performance window:14.9FPS/67.09ms frames, path32.86ms,
AI15.21ms,render7.83ms. Worst frame559.37ms; house update494.8ms.
Path budget5000 vs actual16106nodes/tick: budget enforced between complete
searches, no remaining-budget argument to resolver. Five ticks per frame
multiply that cost. Service evaluations repeat across yards at severe AI
spikes (7/16/18 evaluations in examples), but precise attribution was missing.
City phases also spike10–30ms. No gameplay optimization claimed this turn.

Added bounded five-second in-memory performance aggregation to existing JSONL
session logger, offline SQLite performance_windows/performance_metrics views.
Every frame counted, worst-frame full context, per-house AI build/search/combat
scopes, service site counts, city subphases, paths by unit type/owner, actual
budget/overshoot, queues and logging costs. Inclusive scopes must not be summed
across nesting. Normal stop flushes partial window; telemetry version11,
policy unchanged. Optional existing256MB cap and disabling env var retained.
No SQLite writes in the game loop, no wall timing changes simulation decisions.
Fixed worst-house overwrite across frame ticks, unreset per-frame path-node
and failure counters, stale empty-queue cycle metrics. See
[performance telemetry](docs/performance-telemetry.md) for evidence and queries.

Validated CTest519cases (516passed/3optional skips),12 Python importer tests,
real C++ capture→SQLite import, dependency audits and local620app metadata.
Original legacy text preserved alongside game session as performance-legacy.log.
Analysis database /tmp/dunecity-last-game-performance.sqlite contains80456
structured events plus7836 legacy slow-frame samples and1221 house spikes.
Logs /tmp/dunecity-performance-{build,tests,python-tests}.log.
Local app build only; not launched, no /Applications copy, remote push or release.
Live overhead/FPS improvements need the next game; no such measurement claimed.

# Micropolis building models and animations — 1.0.619

Restored all16 apartment models +12 house styles,20 commercial models,8
industrial models. Prior GFX runtime skipped inhabited d0 variants, highest
commercial d4, and all houses. Stable coordinate hash selects among appropriate
visual models within existing3 inhabited density levels. R/C/I gameplay2x2,
roads1x1 and specials3x3 unchanged; no population/effects/AI/save/RNG changes.

New scripts/build-city-atlases.py assembles tracked raw tiles into seven compact
runtime atlases (Pillow authoring only). Full importer invokes it and fixes
category bounds/omitted full stadium:800 is centre, base795. Factory smoke uses
corrected documented chimney IDs/positions, never the upstream vacant620 bug
or overwritten second chimney. Powered radar, periodic full stadium/football,
nuclear swirl and light/heavy traffic sequences restored. Traffic thresholds
64/192 match original doRoad. Preserve black vehicle pixels; old road recolour
andcentre dot erased cars. No live traffic through fog. Simulation-cycle visual
clock freezes on pause; stadium8of32seconds is an adapted presentation cadence.
See scripts/SPRITE-IMPORT.md for IDs/layouts/source links and regeneration.

GFXManager loads prepared atlases, validates dimensions, scales once and shares
surfaces/textures across houses; eliminates per-house duplication for these
seven larger sheets. Max dimension at3xzoom1728px. Constant-time source-frame
selection, no map scans/per-frame images. Missing/stale atlas data produces a
clear startup error; all assets are tracked and existing platform packaging
copies imported_sprites. Build-menu/editor icons use inhabited static models.

Full CTest passed (518cases,515passed/3optional skipped), before/after Ninja
dependency checks passed. PNG tests verify dimensions, real animation frames,
vacancy/power/traffic gating, all models reachable and clamped frame bounds.
Fresh full Micropolis import into/tmp reproduced all seven atlases pixel-for-
pixel; generated contact sheet inspected. All eight atlas directory files in
local app bundle match source. Version1.0.619 metadata verified. Live-game FPS
not measured; no app launch, /Applications install, push or remote release.
Build/test logs:/tmp/dunecity-animation-{build,tests}.log.

# Micropolis park terrain for walls/turrets — 1.0.618

Replaced walls/gun turrets/rocket turrets radial +15 land-value stamps with one
+15 raw park source per structure origin. Reference: local Micropolis scan.cpp
pollutionTerrainLandValueScan (one +15 per qualifying terrain tile) and
smoothTerrain non-dither branch: (center + cardinalSum/4)/2, single pass with
integer rounding. Original literal FOUNTAIN tile does not receive the tree
terrain increment; this implements Stefan's intended one-park equivalence,
not that original fountain quirk. Both turrets have identical park effects.

Scaling: original4x4 terrain cell corresponds to3-tile zone +1-tile road; use
3x3 terrain cells for2-tile zone + unchanged1-tile road. Gameplay zones remain
2x2, roads1x1 and land-value storage2x2. Average tile samples into the2x2 land
layer to avoid aliasing odd-coordinate sources across nonaligned3/2 grids.
One isolated source gives7 in its terrain cell,1 in cardinal neighbours,0
in diagonal/distant cells before resampling. Multiple sources accumulate raw
before smoothing, avoiding duplicated world-tile additions/rounded emitters.
Park contribution enters base land value before pollution subtraction and
clamping. Existing sand terrain/direct bonuses and Palace/Stadium stamps stay
unchanged; this is park-source parity, not a complete terrain-simulation port.

ParkTerrainPolicy is deterministic/local and derived, rebuilt each effects
scan, not serialized. CitySimulation initializes it for new/load state. AI
service investment and turret amenity estimates share exact marginal smoothed
contributions including planned sources and2x2 resampling. The old doubled
rocket radius is gone; getParkLandValueRadius for park sources is now only a
conservative search bound. Combat and police coverage unchanged.

Tests cover source counting, original kernel goldens, raw overlap aggregation,
map edges/partial cells, no negative-coordinate alias, rebuilding destroyed
sources, pollution-before-clamp, and exhaustive AI/runtime gain agreement on
an11x10 map with overlapping sources and nonaligned grids. Full CTest passed;
Ninja dependency audits passed before/after build. Local1.0.618 app metadata
verified. No app launch, Applications copy, remote push or release. Logs:
/tmp/dunecity-park-terrain-{build,tests}.log. Existing cities recalculate their
lower turret-driven values on the next effects scan; tax/crime may respond.

# Pollution sidebar overlay button — 1.0.617

Added Pollution below Land Value and Crime, using existing CityOverlayMode::Pollution.
Click toggles off/on, selected state tracks Shift+4, and visibility matches other
city-only buttons when selection is empty. Existing green-to-purple renderer and
legend reused; no simulation change. Local617 built and full CTest/dependency
checks passed. No remote release, app launch or /Applications copy.
Also verified user query: Micropolis scan.cpp subtracts pollution from land value;
our computeBaseLandValue does too. DuneCity adds park/turret/sand bonuses afterward,
so positive bonuses can offset pollution. No change to this formula requested.

# Zone suitability, labour demand and civic notices — 1.0.616

Reviewed completed 1.0.614 4 Corners session1789019169404635-0. In the
last building snapshots, all212 vacant R/C zones had pollution>=160. C/I
were capped in all280 state samples after5simminutes. Verified against local
Micropolis simulate.cpp: resHist stores resPop/8, but our saved prevResPop is
raw. computeDemandValves now divides previous residential population by8 at
the labour boundary; save fields and startup safeguards remain unchanged.
This removes the artificial1.3 labour saturation, not all legitimate positive
demand. Existing accumulated valves adjust through subsequent simulation ticks.

AI R/C sites are rejected when their origin pollution blocks growth, using the
same role-specific gate as runZoneGrowth. Industry still tolerates pollution.
Environmental/commute tier now precedes residential infill; safety stays first.
Positive residential demand still prioritizes usable infill, and no-site results
fall back to other demanded zone types. Rejections are logged as
pollution_rejections. No new path searches or pollution formula changes.

The demand calculation reports which positive valves were capped by missing
Stadium (or Palace substitute), Airport, or Starport. Local-house UI notices
name the required building. UI-only state deduplicates notices until resolved,
keeps simultaneous requests pending, spaces them10simseconds apart, and drops
pending requests if the requirement is satisfied. No save-format changes.

Regression tests cover oversupplied jobs draining capped C/I demand into negative
values, civic thresholds/Palace substitution, no false notices for negative
demand, notification spacing/resolution, and suitability before infill. Full
CTest and before/after Ninja dependency audits passed. Version1.0.616 built at
build/bin/dunecity.app, metadata verified. No game launch, /Applications copy,
remote push or release. Logs: /tmp/dunecity-demand-{build,tests}.log.
Pollution spreading still needs a separate audit against Micropolis; not changed
in this task. Live-game balance after the normalization fix is not yet measured.

# Roads are prepared foundations; reuse spare lanes — 1.0.615

Stefan's double-road screenshots showed factories wasting space and concrete.
Tile::hasPreparedFoundation now treats concrete OR a road as prepared ground,
without merging their tile states. StructureBase captures this before clearing
the road flag: previously it cleared the flag and checked only concrete, charging
placement damage on roads. Prepared roads also follow the existing concrete
foundation degradation rule. Covered road flags still clear normally.

QBot foundation scoring counts roads, slab-site search avoids paved roads, and
pre-concreting skips road/concrete cells. Slab4 is used only when all four cells
are bare; mixed footprints get individual missing Slab1s. Shared foundation
helpers are exercised with a3x2 footprint containing3road cells: exactly3Slab1
orders. Placement no longer penalises every covered road; it rewards reusing
redundant parallel lanes while retaining local access and road connectivity
checks. Telemetry reports redundant_roads_reused. No map-wide path tracking.
Tests cover spare-lane reuse, a single road with no alternate connection, mixed
foundations, and foundation capture before road flag clearing. Full CTest and
Ninja dependency audits passed; local615 built, no remote release or launch.
Logs: /tmp/dunecity-road-foundation-{build,tests}.log.

# Local road access and residential infill — 1.0.614

Supersedes 613's global connectivity search at Stefan's request. GroundAccessPolicy
now checks only a candidate footprint and its one-tile border, preserving local
passage between surviving border tiles. No full-map reset/flood, anchor, protected
unit paths or scan of all producers. A 3x2 candidate reads20 occupancy tiles.
QuantBot separately checks deployment openings only for touching producers and
nearby other-yard reservations; roads/slabs remain passable, transient units do
not reserve space. Rocket turrets allow diagonal local passage at road junctions.
Existing city road-perimeter planning and road-connection safeguards remain.
This incremental local rule does not diagnose/repair pre-existing distant traps.

Residential sites with R/C/I on at least two nearby sides (touching or across one
road tile) count as infill. Safe infill ranks above outward expansion; existing
safety checks stay in force. If positive R demand and a buildable infill site exist,
choose R before normal demand/count balancing. Bootstrap and C/I fallback remain
when no residential infill is available or R has no demand. Telemetry identifies
residential_infill and residential_infill_sides. Four-zone block scoring now
permits mixed R/C/I rather than only identical zone types.

Full CTest and dependency audits passed. Tests cover touching blocks, closing a
local lane, reservations/terrain, map edges, junctions, bounded occupancy reads,
and residential demand/infill priority. Synthetic placement-only comparison was
13.1ms for613 versus10.4ms for614 over11907 checks; not an FPS claim. Recent runtime
logs had AI updates up to310.7ms and separate unit path costs around20ms/frame;
live-game performance after this change remains unverified. Local614 built;
no remote release or game launch performed. Build/test logs are under
/tmp/dunecity-local-placement-{build,tests}.log.

# Preserve connectivity rather than fixed lanes — 1.0.613

Current 612 game 1789013651529445-0 (4 corners, city mode) showed the ground
access guard rejecting 55,961 of 70,669 candidate checks for Mercenary in a
five-simulation-minute sample (~79%; repeated candidates, not distinct tiles).
Fixed paths from every unit and every factory perimeter tile over-reserved land.
GroundAccessPolicy now retains unit endpoints and groups of producer exits,
not path tiles. A candidate may reroute access; each previously connected producer
needs at least one remaining outside-connected perimeter tile. Existing connected
units must remain connected and cannot be covered. New factories also need one
outside-connected exit. Existing isolated units/producers do not freeze unrelated
construction. Other-yard reservations remain static barriers in QuantBot's caller.
A connected local perimeter is a cheap proof that detours remain; otherwise an
early-exit flood checks longer detours, then validates protected endpoints when
components split. If the old outside anchor is covered, use the largest remaining
component rather than reserve the anchor forever. No random state or save fields.
Regression cases cover long alternate routes, sealing the final opening, corner
exit replacement, trapped units, and disconnected courtyards. Full CTest passed;
Ninja dependency audits passed before/after build. Local build is 1.0.613; live
612 game was not restarted, and no remote release was requested for this change.

## Current release/hosting entry point — 2026-09-10

Read `docs/release-operations.md` and `docs/sourceforge-releases.md` for current
operations. SourceForge sync is configured and verified; no credential setup is
pending. User policy: do not upload source archives to SourceForge Files. Publish
six binaries, README with tagged Git source link, and SHA256SUMS. Git branch/tag
mirroring remains enabled. The original nine-file publication below is historical;
the current release folder now contains eight files. Verification run
https://github.com/VR48/dunecity/actions/runs/34431698171 passed all eight release
safeguard tests, read back matching upload hashes, and confirmed the existing
Windows/macOS/Linux defaults at 1.0.612. The source archive was removed explicitly.
Retries now check current defaults before issuing an API update: a redundant PUT
returned HTTP 400 after an otherwise successful upload. Source remains available
via the README's tagged GitHub link and SourceForge's dedicated Git refs.

## SourceForge release automation — 2026-09-09

Added `.github/workflows/sourceforge.yml` and `scripts/sourceforge-release.py`.
They mirror successful stable GitHub releases to SourceForge `dunecity/<version>`,
verify uploaded checksums by readback, then publish namespaced source tags and
advance the dedicated `dunecity` branch/platform defaults for the latest release.
Legacy master and old files are preserved. Manual dispatch supports backfills.

SourceForge setup completed 2026-09-10: account svan058, dedicated SSH key and
pinned host keys, Releases API key and GitHub variable/secrets configured.
Workflow run https://github.com/VR48/dunecity/actions/runs/34416472472 succeeded
in 2m15s. All nine files read back with matching SHA256; public Files page lists
six packages, source archive, notes and checksums. API confirmed Windows ZIP,
macOS DMG and Linux AppImage defaults at 1.0.612. Source branch `dunecity` and
peeled `dunecity-v1.0.612` resolve to b949be92a1f44233c1964e3b1b3995088ac3a76e;
Legacy master was unchanged by that release sync; subsequent website-only commit
`dc69c5a` updated its downloads page. Future stable-tag
builds trigger the mirror automatically. Setup/operations documentation is in
`docs/sourceforge-releases.md`. Do not print or commit credential contents.
This is infrastructure-only, pushed with `[skip ci]`; game version remains 1.0.612.

# Preserve qBot factory exits and ground routes — 1.0.612

Stefan's Vanilla611 screenshots show rear factories surrounded by structures,
with their free deployment tiles inside sealed courtyards. Placement previously
checked adjacent open tiles without checking a route out. GroundAccessPolicy
now builds a deterministic static vehicle graph (mountains and structures block;
roads, slabs and transient units do not). It selects the largest component and
an anchor in its widest open area, then preserves routes from own production/
refinery/repair/police deployment rings (including corners) and active ground
units. Proposed ground producers require every free deployment-ring component
to connect outside without passing through their own footprint. This allows a
small detour around a new factory. Other buildings cannot cover protected lanes.
Other yards' reserved footprints are included; cached state clears per planning
pass/reservation/placement and simulation cycle. MCV deployment also checks lanes
and reservations; MCV positions themselves are exempt because deployment consumes
that unit. Generic, service/turret, redevelopment, generator fallback and final
placement paths use the guard. Rear safety scoring is retained. No save fields,
random draws or non-qBot controller behavior changed. Already enclosed bases are
not automatically demolished/repaired by this prevention change.

504 test cases passed, 3 optional skipped through CTest. Pre/post Ninja dependency
audits, deep strict signing and version612 checks passed. Synthetic access-only
benchmark for eight passes/80000 candidates: 34ms at128x128, 7ms at192x192, 10ms
at256x256 (not a live FPS measurement). Local build612 ready; not launched/pushed.
Build/test logs: /tmp/dunecity-612-build.log, /tmp/dunecity-612-tests.log.

Harvester investigation (no economy policy changes in612): Vanilla611 session
1788876460778739-0, Atreides qBotBrutal, override100. Both AI/engine caps were100;
actual fleet passed40 and peaked75 at14.21simulation minutes. 738249 spice still
remained but equal division by5 houses followed by2000 spice/harvester reduced
target to73. Consecutive samples had46-53 actively harvesting and roughly17-30k
credits/minute income: the cap worked, but this total-inventory/equal-share
heuristic is too weak to justify the expansion cutoff. Recommended follow-up is
usable local field/refinery-throughput demand, subject to explicit cap and cash,
instead of treating each house as entitled to exactly one-fifth of all spice.
User asked to evaluate that assumption; replacement policy is not implemented.

# Restore legacy AI harvester restart — 1.0.611

Stefan reported Original AI harvesters stranded in Vanilla610. Logs show empty,
respondable harvesters with mode6(STOP), no target/path, stationary30seconds.
610 removed native auto-resume for every AI; Original/Smart controllers rely on
that restart and only issue early-return orders themselves. The scope was wrong.
Harvester::checkPos now restores STOP->HARVEST for AI houses without a QuantBot
controller. A qBot co-controller retains responsibility for safety holds/resumption;
human STOP remains preserved. Runtime checks the house player list, not display
names or mod flags. The empty-refinery-loop fix remains. No save layout changes.
Regression covers legacy AI, qBot, human and human+qBot restart decisions.
Local611 built on MacBook Air: pre/post dependency audits passed, CTest495passed/
3optional skipped, deep strict signature and bundle version611 verified. Not
launched or pushed. Logs /tmp/dunecity-611-build.log and /tmp/dunecity-611-tests.log.

# Harvester empty-refinery oscillation — 1.0.610

Current Vanilla609 session1788874105288417-0 showed repeated retreat_refinery
orders with zero cargo (harvester619 had120 logged orders in the read snapshot).
Two conflicts: safety requested refinery trips for an empty vehicle whose old
spice destination was dangerous despite its current location being safe; native
Harvester::checkPos forcibly changed AI STOP back to HARVEST on every check.
Removed forced auto-resumption. Safety now requests a new refinery refuge for
immediate local danger, or cargo plus unsafe-job/return state. Empty safe vehicles
search safe spice or disperse/hold. Existing valid safe return trips still finish;
loaded unloading and threatened empty evacuation remain. No new timer/save fields.

Local610 build, pre/post dependency audits, signature and CTest494pass/3skip passed.
No game restart or remote push. Regression covers retreat then unloaded state,
unsafe empty job and loaded returns. Live behaviour requires the new process.

Current609 match at24.32simminutes: Harkonnen qBotBrutal71,810/80karmy,credits2888;
Atreides60, Fremen60, Mercenary2370army. Hark target mix tank12.43%siege14.67%
launcher54.99%special13.34%quad4.57%; reward/lostcost1.46/1.87/5.01/1.78/.91.
Merc orni reward/loss.15,target1.55%; launcher3.11,target41.13%.
These are live snapshots, not final outcomes; buildable-type availability changes
as factories are lost. Script /tmp/review-live609.py; no balance change beyond bugfix.

# Performance weighting, fair attack timing and local placement lookup — 1.0.609

Stefan approved score^1.5 plus the prior review recommendations and a local build.
UnitMixPolicy now sharpens normalised performance scores with an integer square
root, after the fading trial prior. Full-match reward/loss evidence, tech openings,
Vanilla evidence blending, the universal 80% type cap and Vanilla air cap remain.
Integer normalisation bounds arithmetic and avoids floating-point pow differences.
Telemetry records exponent1500 and per-type allocation_weight alongside the
original score; SQLite unit_allocation exposes both new fields, with old logs NULL.

Attack delays now derive 75–125% of the configured interval from seed, simulation
cycle and house through a fixed uint32 hash. Both initialization and resets use it.
This replaces the permanent (houseID-3)*15-second bias. No wall clock or extra RNG
consumption; existing saved countdowns still load, next reset uses the new policy.
An attack_schedule event records base and selected cycles. This is deterministic
by construction; a live multi-computer test has not been run.

Service planning snapshots own police and rocket positions once per decision,
instead of rescanning the global structure list per property/candidate. An 8-tile
spatial index visits only properties within the existing 23-tile Chebyshev radius.
Scoring, coverage stacking, reservations, candidate iteration/tie order and building
priorities are unchanged. The local snapshot is rebuilt per decision. Regression
checks compare indexed results against exhaustive scans, including map/bucket edges.
This addresses a visible repeated-scan cost; it does not claim to identify every
source of the logged 405ms AI spike or prove a live FPS improvement.

Local Release1.0.609 built on MacBook Air, dependency audits before/after passed,
CTest493passed/3optional skipped, SQLite importer11tests passed, version metadata
and deep strict ad-hoc signature checked. No game launch or remote push requested.
Policy performance-weighted-fair-attacks-v44; save layout unchanged at9834.
Logs /tmp/dunecity-609-build.log, /tmp/dunecity-609-tests.log,
/tmp/dunecity-609-sql-tests.log. Source is committed for the next release.

Previous release1.0.608 is public (a094d10), all GitHub platform/tests passed and
website links verified. The next reviewed match was actually1.0.606:
session1788869520113577-0,81.84simminutes,328467events imported/audited clean in
build/review-latest-606.sqlite. All4qBotHard70kcap,roughly947k–999999credits,24heavy
factories and8yards each. Latest mix snapshots76–77min: launcher shares55/47/49/42%,
reward/loss4.38/3.24/5.44/3.33; orni4.5/7.9/3.5/6.6%,reward/loss.35/.53/.37/.51.
Huntorders73/46/19/22 (Harkonnen/Ordos/Neutral/Rebels); readiness also affects counts.
Last performance window18.2FPS,AIaverage12.43ms,max405.02ms,~1000units,5cycles/frame.
No additional economy, factory, crime or military-limit changes made here.

# Road completion, launcher safety and grouped outbreaks — 1.0.607

Stefan approved all three findings from the completed 606 review. ConstructionYard
now recognises successful tile placement when House::placeStructure consumes the
completed queue entry, even though it returns nullptr for roads/concrete. This
fixes false failures and stale road reservations/retries in qBot. Snapshot the
queue length before placement so several identical queued roads are handled
correctly. Failed placement with unchanged queue still reports failure. Tile
mutation remains in House; no road-routing responsibility moves into the yard.

qBot's shared harvester danger grid adds 3 tiles beyond visible launcher weapon
range. Other weapons retain existing reach; crushable foot troops remain excluded.
Before missiles hit, threatened harvesters request a clearing team against the
nearest relevant visible launcher using the cached threat list. Proactive responses
use troops within 18 tiles and require enough total local/committed health-adjusted
value for the existing 125% threat budget; nearby defending turrets also count as
threats. Insufficient local strength sends no sacrificial partial team. Already
committed responders count, human orders/retreats/other live fights remain protected.
Proactive incident checks debounce at 5 seconds; actual damage responses retain
2 seconds and existing emergency behaviour. Both Vanilla and DuneCity qBot benefit.
Returning harvesters now validate the refinery corridor as well as its endpoint,
so they cannot keep an unloading trip through launcher fire merely because the
refinery itself is safe. Existing safe-refinery preference and field redirection
remain; paths use the existing corridor estimate, not a full pathfinding proof.
Telemetry includes clear_spice_launcher response reason and harvester threat radius.

City gangs retain each district's 4–6 minute crime buildup and occupied density
1/2/3 strengths, threshold192 and population gate5000. Once mature, an outbreak
waits up to approximately30 extra simulation seconds (city-scan quantisation) to
combine neighbouring ready districts. Eight-neighbour connected components are
restricted to the same house; only fully mature districts join. Any member's
expired gathering window releases the component together at its highest-crime
origin. Policing/vacancy/small population cancels pending readiness. Contributing
districts all reset once, even if engine capacity/space limits actual deployment.
This is bounded district-grid work, not per-trooper graph searches. Telemetry adds
contributing_districts, district_strengths and gathering_ms.

Save version9834 reuses the old64bit exposure slot for readyCycles. Loading9833
retains buildup progress but discards old exposure; older timers retain their
existing reset migration. Current saves preserve grouping delay; simulation cycles
and stable ordering only. Policy grouped-unrest-harvester-safety-v43.

Verified local Release1.0.607: dependency audits, version consistency, signature
and CTest490passed/3optional skipped. Regression checks cover tile queue success
versus rejection, launcher margin/crossing/escape, sufficient nearby clearing
forces, gathering delay/immature exclusion/house+row boundaries/policing, and saved
readiness/legacy exposure. Not launched, pushed or released. Live balance needs a
new game; no claim of measured FPS improvement.

Evidence prompting these fixes: completed606 session1788866702844001-0 lasted
34.023simminutes, both qBotHard cap70000 and both alive. SQLitebuild/review-606.sqlite
104213events, auditclean. Fremen harvester losses3 versus Harkonnen39;20of38 logged
Harkonnen harvester lethal events identify known-built launchers. All1753heavy
no_affordable_capacity decisions had<300remaining cap. Road placement189events
allfalse;353road cancellations allshow existingroad,175reservedoverlap. Gang141
outbreaks1402troopers,max54,39waves<=3;1969/3083defenceresponses targeted exactgang
IDs. Final32police each versus177/194rocket turrets,meancrime12/8. Reviewnotes at
/tmp/dunecity-606-review.md. No further army-limit/service-ratio change requested.

# Density-scaled hotspot outbreaks and idle road repairs — 1.0.606

Stefan corrected 605 outbreaks: one compact group at the worst crime hotspot,
with 1 trooper per low-density dangerous building, 2 per medium, 3 per high.
This supersedes the historical-average size, 12 minimum/60 maximum and spread
across several buildings in 605 below. Current occupied city level supplies the
weight; vacant zones and non-city-role structures contribute zero. Existing
per-house 16x16 district scope, dangerous threshold 192, population gate 5000,
and 4–6 simulation-minute buildup remain. The Micropolis crime formula is unchanged.

The entire district wave uses one highest-crime occupied building as its origin
and target. Legal infantry slots are filled nearest-first within 16 tiles of that
hotspot, with stable integer ordering. One advancing cursor avoids rescanning
blocked/full tiles for every trooper. Engine unit limits and available space can
still reduce actual deployment; telemetry retains requested versus spawned and
adds occupied_density_1_2_3 plus hotspot coordinates. The old buildingExposure
field remains reserved in save format 9833, preserving saved progress and byte
layout; it no longer affects strength. No wall-clock/random decisions added.

Idle qBot city yards now queue paid road repairs after strategic/city construction
selects no work. The older independent road helper scanned only 20 tiles from the
base centre; new maintenance considers edges/corners around all surviving owned
buildings, including outer-city road gaps and intersections. Broken through-roads
rank before junction/edge extensions. Only connected, legal, unoccupied terrain
outside reserved footprints qualifies. Queue at most 8 road segments once per
planning pass, respecting spendable cash above the economic reserve. Normal
construction retains priority. Road placement uses the ordinary yard production
and placement pipeline; no new free road command. Road repairs can reuse recently
destroyed areas like concrete, and cancel safely if their site becomes blocked
or has already been repaired. Telemetry: city_road_repair, idle_yard_road_gaps.
Vanilla is unaffected by these city-only changes.

Built/signed locally as 1.0.606; dependency audits before/after build, version
metadata and signature verification pass. CTest: 484 passed, 3 optional skipped.
Tests cover density-weighted totals, uncapped district sizes, 4/6-minute timing,
reset/population gates, saved legacy fields, stable nearest-hotspot sites and map
edges, and outer-city road gap prioritisation/deduplication/blocking. Not launched,
pushed or released. Live wave balance and road maintenance still need gameplay.

# Larger district outbreaks and flexible production — 1.0.605

Stefan requested larger coordinated rebel outbreaks after longer sustained crime,
then explicitly shortened the proposed wait to 4–6 simulation minutes. The old
three-trooper district spawn is replaced by a 12–60 trooper outbreak. Mean crime
among dangerous buildings controls buildup: 192 takes ~6 minutes, 250 takes ~4,
quantized to the city scan cadence. Dangerous threshold192 and the existing5000
population gate remain; the Micropolis crime formula itself is unchanged.

Each house's existing16x16 district timer accumulates dangerous-building exposure
throughout buildup. Force is3 times the crime-weighted average dangerous-building
count, bounded12–60. More buildings make a bigger wave, not a shorter timer; a
last-second surge cannot inherit a fully mature large force. No dangerous buildings
or population below5000 resets both accumulators. Wave groups of3 emerge together
around several dangerous buildings, distributed across the district when the force
cap permits only a subset. A blocked spawn location skips that trooper rather than
aborting all the other groups. Existing hostile faction selection, engine unit
limits, deployment cancellation and urgent news warning remain. A wave consumes
the buildup even when capacity/space limits it, avoiding rapid retries.

Save version9833 appends per-district64bit building exposure after the old progress
array. Old saves read the old array then reset outbreak timers, since they cannot
supply exposure history. New saves preserve both accumulators exactly. The codec
checks district count. This is deterministic simulation state; no wall clock/RNG.

Both Vanilla and DuneCity qBot can now add light factories when at least75% of
existing lanes are busy and funded light-unit shortage covers the factory cost.
Unfinished/queued factories block duplicate expansion; reserves, placement and
engine limits remain. Light backlog expansion comes before optional heavy-factory
cash expansion. Telemetry adds light busy/backlog/deficit and light_unit_backlog.

When every available heavy type is above its preferred share, heavy factories may
still fill spare funded army capacity. They choose the least overrepresented type
relative to its learned share, considering the next unit's cost. Fielded and queued
value count against the army cap and orders consume money/cap sequentially. No new
fixed troop ratios. Light factories receive troop-order priority so heavy overflow
cannot consume their immediate slots; city yards retain their existing first
priority. Whole-match learning and safer factory placement remain intact.

Policy district-outbreak-production-v42. Heavy telemetry identifies
available_factory_capacity fallback and no_affordable_capacity. Crime telemetry
includes dangerous-building count, requested force, mean crime and buildup rate;
member records identify each spawn-origin building.

Built and signed locally1.0.605. Ninja dependency audits passed, version metadata
consistent. CTest482passed, 3optional skipped. Tests cover4/6minute boundaries,
cluster size/history, policing/small-population reset, district spread, old/new
save codecs with trailing sentinels and invalid sizes, heavy overflow balancing,
parallel cap/cash consumption and light backlog expansion gates. Not pushed,
released or launched; live balance still needs a gameplay test.

# Police reinforcement ceiling and completed 603 match — 1.0.604

Stefan requested increasing police deployment's per-house count ceiling to 250.
PoliceStation now permits reinforcements below 250 military units and blocks at
250; the existing per-member batch recheck prevents overshoot. Count still excludes
harvesters, MCVs, carryalls, frigates, sandworms and ambient units, as before.
The qBot army-value ceiling and engine unit limits remain enforced. Automatic and
manual deployment, and the sidebar Unit limit reached indicator, share this gate.
Cooldown and composition unchanged. Built locally as 1.0.604; dependency audits,
version check, signature verification and CTest passed (474 passed, 3 skipped).
Not pushed, released or launched.

Reviewed completed 603 telemetry session1788861469063600-0, 2P - 192x192 - SimCity,
seed132153238. Ended without result at85818cycles/22.8848simulation minutes;
Fremen and Mercenary both alive, with458803 and600912credits. SQLite
build/review-603.sqlite imported52277records, zero invalid/incomplete records,
audit no issues. Full session captured; report/tmp/dunecity-603-report.txt.
This is the new simple-hunt/full-match-memory controller, unlike the old601 review.

Findings (recommendations only; 604 changes the police ceiling alone):
- Heavy production constrained by strict allocation shares, not lack of cash.
  Final snapshots show8/24 and4/22 heavy factories busy. In last~5minutes,
 207/314 and217/296 heavy-allocation decisions had no positive affordable deficit;
  final samples show all available heavy candidates affordable but above quota.
  Military values only~65k/~71k against100k ceiling. Light allocation rose from
  4% opening to39.80%/34.55%, while each house had one light factory. Candidate
  refinement: make production capacity follow actual deficits, and permit useful
  heavy production to fill spare army capacity while light production catches up.
-534crime events spawned1602troopers,1597 subsequently destroyed.4059/4288 defence
  dispatch events (94.7%) targeted those exact spawned IDs. Local crime hotspots
  remain despite final mean crime12/15. Suggested refinement: district gang spawn
  pacing/outstanding-gang controls and persistent incident handling, leaving the
  Micropolis crime calculation intact. Earliest spawn1.41min on a populated map;
  do not call this a tiny-population regression without checking starting population.
- Combined reward/lost-value ratios:launcher4.280,ornithopter0.680,quad1.377,
  trike1.827,raider1.359. Reward includes weighted actual damage plus unitkillbonus.
  Final launcher allocation33.31%/42.93%,air6.45%/5.50%. Light shares reflect actual
  results across three separate types; plentiful gang infantry may influence the
  matchup mix, but victim-specific reward attribution was not established here.
-17ground hunts issued, typical groups~100–135units; defence response median1unit,
  max12/13, total7363dispatches (orders, not distinct troops). Both ended with40
  harvesters; only3/6harvester losses and~229.5k/~227.1k refined spice each. Heavy
  factories lost5/7, significantly fewer absolute losses than the much longer601
  game, but duration/map/player differences prevent a causal comparison.
- No frame-time samples in this game's ordinary log; do not claim an FPS gain.

# Full-match unit learning and safer factories — 1.0.603

Stefan explicitly requested keeping the entire game's unit performance rather
than fading old evidence. This supersedes the longer-confidence/recent-results
proposal in the 601 review below. Both DuneCity and Vanilla qBot now calculate
performance and exploration confidence from cumulative House combat rewards and
losses. Cost-weighted actual damage and the 20% unit kill bonus are unchanged.
Idle time never removes evidence or restores an unsuccessful type's uncertainty.
Opening availability rules and existing allocation constraints are unchanged.

PerformanceHistory retains the former PerformanceWindow binary save layout.
On the next allocation, authoritative saved House totals replace loaded decayed
values, so old compatible saves regain all recorded evidence. Save version stays
9832. Telemetry identifies lifetime inputs and policy
`lifetime-mix-safe-factories-v41`; cumulative raw fields remain available for SQL.

Factory placement no longer rewards proximity to the army rally. Heavy, light,
high-tech factories and infantry production prefer greater clearance from visible
hostile weapon zones, after avoiding recent loss sites and before city frontage
and compactness preferences. The heavy-factory redevelopment fallback uses the
same safety ranking. Existing legal placement, fire-zone, road and reactor checks
still apply. A constrained legal site remains usable; this introduces no new veto.
Clearance saturates twelve tiles beyond the existing firing buffer to avoid
needlessly chasing remote map edges. Existing rear preference also applies to
light and infantry factories.

A deterministic two-pass distance transform is cached with the existing two-second
threat map: O(map area), not enemy scans per candidate. Placement telemetry includes
`enemy_clearance_tiles`. The cache is derived and rebuilt after loading.

Verified local build 1.0.603: dependency audit before/after build, ad-hoc signature,
version consistency and CTest passed (474 passed, 3 optional skipped). Regression
tests cover full-match retention through long idle periods/save-load, migration
from a decayed saved window, all 512 threat arrangements on a 3x3 map, footprint
clearance at edges, safety ranking and constrained-site fallback. Built and committed
locally; not pushed, released, launched or tested in a live match.

# Completed 601 match review after simple-controller build

Session 1788846458415706-0 ended cleanly at760548cycles/202.81simulation minutes,
local_result ended_without_result; all four houses alive with999999credits. This
was the old601 controller throughout, not a602 test. FinalSQLite import266721rows,
no invalid/deferred tails; auditclean. DBbuild/review-601-live.sqlite and final
report /tmp/dunecity-601-final-report.txt. Ordinary engine log confirms clean
teardown and reports metaserver analytics end recorded (not remote verification).

Detailed capture stopped at298448cycles/79.59minutes because the15/16 detail
allowance of256MiB was exhausted; final summary/session_end survived at203minutes.
No capture_limit event is emitted at this soft cutoff. Final totals cover the
full match; allocation/placement timeline claims stop at~80minutes.

Across four houses, credited damage+unit kill bonuses divided by lost replacement
value:launchers2.880 (6576built/6347lost),siege1.581(2042/1900),tanks1.080(4160/3999),
ornithopters0.363(4571/4548). Air losses cost2728800credits for991667reward.
Final heavy factories23–24 each; totals246built/151lost (Mercenary85/62).
Many unit types can have free/initial/captured spawns, so built minus lost is not
necessarily final count; do not treat MCV deployment losses as battlefield deaths.

At last detailed snapshots air targets3.60–4.71%; light targets12.06–20.34%.
Ordos recent air raw score~0.151 was lifted to0.408 by exploredScores uncertainty
prior. Recent evidence decays~2.6min half-life, making repeatedly poor performers
look uncertain again. Proposal:retain longer-lived confidence while using recent
performance for effectiveness, allowing exploration after actual changes without
repeatedly subsidising known poor matchups. No unit-specific hard cap proposed.

Civic investment60–80min:639rockets versus82police, so turret selection is working.
Crime means at last snapshots2/2/7/2, existing local hotspots explain some police
orders. Do not infer global police excess from final count alone. Prioritise602
battlefield test before further balance changes. Other proposals:protect costly
factory rebuilding from active fronts; keep compact periodic snapshots throughout
long games after detail sampling is curtailed. No additional gameplay changes.

# Simpler army control — 1.0.602

Stefan requested removing the formation controller after the live 601 game left
nearby troops gathering while cities were destroyed and slowed to ~15 FPS.
Reviewed session `1788846458415706-0`, DuneCity 192x192, seed316409388.
SQLite snapshot `build/review-601-live.sqlite`:99,644 events through cycle182298
(~48.6 simulation minutes), audit clean; one incomplete live JSONL tail deferred.
All four houses repeatedly assembled/regrouped. At cycle181945 house5 had42/150
members ready; earlier snapshots included185 troops waiting with131 ready and a
69-member force with zero engaged pursuing a target74tiles away. Code excluded
squad members from scramble defence and ordinarily limited city defence to a10%
reserve. These are direct causes of idle armies during nearby attacks.

The most recent1,000 FRAME SPIKE samples at review time had median79.35ms frames,
55.75ms unit work,16.4ms pathfinding,2.3ms rendering and442 queued paths (max633).
These are slow-frame samples, not an unbiased FPS average or proof that squad
logic accounts for all cost. AI itself occasionally spiked to291.2ms.

602 removes the assembly/formation/forced economic-target controller and its
unused policy/formation tests. Normal ground attacks issue native HUNT once to
available healthy troops, leaving current fights and human commands alone. No
readiness percentage, cohesion wait, shared base target or retreat-to-regroup gate.
Idle combat troops loosely gather around active harvesting centre of mass, offset
three tiles towards the nearest visible ground threat; base centre is the fallback
without working harvesters. Anchor search is bounded17x17 and cached30seconds,
with5tile position tolerance. No global flood fill or per-member connected slots.
Idle repositioning allows four orders per AI update, eight local candidate slots
per unit, skips stressed queues (>150), existing movement and queued destinations,
and never falls back onto an occupied centre. Human control and kiting remain.

Defence now draws from all usable AI troops, including hunters, when a building or
harvester takes a hit. It estimates the nearby8tile enemy force by health-adjusted
replacement value, requests125% strength, subtracts existing responders, then
recruits nearest compatible troops with deterministic ID ties. Engaged troops in
other fights and human commands are excluded. Local non-forced attacks permit
nearer target selection; AREAGUARD keeps the response local after the attacker dies.
Fixed base/escort pools are removed. Repeated hits are debounced2seconds per8tile
incident district, separately for air/ground. No artificial unit-number ceiling.
Also fixed the old damage callback sending pixel centre coordinates to a tile move.

SAVEGAMEVERSION9832 appends the small defence debounce map. Legacy squad save
fields remain readable; old squad orders are released once on the first AI update.
Rally order budget is local to each check, not unsaved cross-cycle state. Decisions
use simulation cycles, stable integer iteration and the existing deterministic
multiplayer path queue. No new random calls. Telemetry policy simple-hunt-v40 adds
`ground_hunt`, `defence_response`, `harvest_army_rally` in generic SQLite events.

Validation: local Release602 built, dependency audit passed before/after, CTest
passed (471 cases passed, 3optional skipped), app signature and version checked.
Tests cover defence force sizing, existing responders, insufficient armies,
deterministic nearest-first selection and bounded blocked rally destinations.
Live FPS/combat and two-peer save/load still require runtime verification.
No game launched/restarted; no release/tag/push requested for this change.

# Windows portability correction — 1.0.601

The 1.0.600 tag was not published as a release: its Windows compiler expands
the legacy Windows-header `near` macro, which collided with a local distance
predicate. Renamed it to `withinRallyRadius`. All other 600 CI jobs passed;
the new all-platform release gate correctly blocked publication. Version601
includes all changes and maps described below; the failed600 tag stays intact.

# Squad crash, obstructed rallies and desktop release — 1.0.600

Session `1788836338773483-0` crashed on 2026-09-08 in the gather lambda
of `QuantBot::updateGroundSquad`. `hasATarget()` checks a stored object ID;
resolving a destroyed target can still return null. The shared engagement
check now resolves once and verifies health/attackability before reading range.
The crash regression exercises the null resolution and dead-object cases.

Ordos made nine assemblies but launched once; seven ended `assembly_obstructed`.
Its 68-member launched force stayed around (159,166) until `wave_complete`.
Old anchors survived city growth, blocked formation slots collapsed onto one
tile, and autonomous target changes fought repeated squad orders. Rally selection
now checks connected terrain and army-sized capacity, ignores temporary friendly
traffic, and searches reachable clearings out to 48 tiles from the base centre.
Members receive unique legal destinations; failed assemblies invalidate the rally.
Rally radius scales with formation size and readiness uses the same square area.
The main core advances with >=70% cohesion while detached units catch up. Local
combat/kiting still takes priority; shared AI attack targets are committed so
individual searches cannot repeatedly replace them and clear paths.

The three-minute timeout now measures lack of movement/combat rather than time
since launch. SAVEGAMEVERSION 9831 saves its progress cycle/location; old saves
start with an invalid sample and establish one on the first update. New decisions
remain simulation-cycle/integer based. Telemetry `connected-army-v39` adds rally
capacity, assembly readiness, compact count, distance and stalled cycles.

Default maps now include the user's unchanged CC-BY-SA DuneCity 192x192 and
4 corners 128x128 maps. Stable release publication requires tests and all three
desktop builds; missing Linux/Mac downloads are no longer ignored. Explicit
nan/inf text rejection fixes the two Mac fast-math configuration test failures.
Validation: Release build, dependency audit, full CTest and app signature pass.
Game effectiveness and live multiplayer remain for gameplay verification; no
claim of a full match simulation is made by the policy regressions.

# Coordinated army and investment fixes — 1.0.599

Implemented Stefan's approval of the six recommendations in AI-598-TACTICAL-REVIEW.md,
plus the police eligibility fix from AI-598-POLICE-REVIEW.md. His correction overrides
the proposed blanket reactor city buffer: R/I/C may remain next to reactors.

- Heavy/light/air production use one funded army target and live+queued military
  accounting. Vehicle shares exclude committed infantry. Infantry accepted orders
  also debit the planning budget and respect the military cap. Air availability at
  the engine air-unit cap removes its share from the plan. Construction backlog
  calculations use the same vehicle plan, avoiding phantom factory demand.
- A saved ground squad gathers 80% of eligible healthy AI-controlled combat units,
  including existing hunters/AI forced orders. Fixed base rally, not harvester
  clusters. Launch at 85% gathered; after 90 seconds permit a >=70% original core,
  otherwise abort. Minimum6 units/3000 initial value. Stragglers stay for the next
  wave. Front-runners stop to wait; fragmented unengaged formations gather again.
  Local combat units override distant economic objectives. Shared objectives have
  20-second persistence, evaluated on deterministic two-second simulation intervals.
  Regroup below half strength or after three unengaged minutes. Base and harvester
  reserves are10% each; escort assignments stick to a surviving harvester.
- Economic targets include harvesters while spice remains, and production/power/city
  buildings thereafter. Endpoint and straight-corridor danger are weighted relative
  to force value; no absolute lightly-defended veto for a full squad. This corridor
  estimate is not a proof of path safety. Removed gameplay retargeting from telemetry.
- Network-replayed human unit commands create saved control leases. Squad gathering,
  ordinary unit handling, scramble defense and air strikes respect these; protection
  lasts at least120seconds and continues while the manual unit is moving/forced/engaged.
- Repeated heavy-factory losses accumulate placement danger for15minutes rather than5,
  with rear-placement preference for heavy/high-tech factories. Other losses retain
  five-minute influence. Existing safe/recovery placement handling remains.
- Reactor clearance applies both ways to reactors, construction/heavy/high-tech/repair
  yards, refineries, IX, palace and starport. R/I/C and low-cost services remain allowed.
- Unit allocation keeps a per-type uncertainty prior and recent combat evidence,
  decaying1/8 every30 simulation seconds (~2.6-minute half-life). No named-unit minimum.
  Recent and lifetime reward/loss inputs plus final shares are logged. Old saves seed
  the new window from available lifetime evidence; subsequent samples decay normally.
- Civic purchases need nonzero actual crime reduction plus sufficient weighted
  economic/growth utility. Raw cumulative crime reduction no longer bypasses cost.
  The emergency exception needs >=32 points of relief above191 crime. Coverage and
  Micropolis crime formulas are unchanged. Existing military turret paths remain.
- Routine city_growth_sample/harvest_rally_move_order observations sampled1/8; actual
  level changes retained. Large captures reserve1/16 for game_summary/session_end/
  simulation_exception. Detailed capture can cease before the end, but accounting
  continues and end summaries remain writable. Policy tag coordinated-army-v38.

SAVEGAMEVERSION9830 stores squad state/membership, manual orders, escort assignments,
placement-loss history and recent performance window; older saves default these fields.
All new decisions use simulation cycles/integer math, stable iteration and the existing
seeded commitment choice. No multiplayer runtime test was performed.

Validation: build and app ad-hoc signature passed; dependency audit passed before/after.
Bundle reports1.0.599. CTest473:468 passed,2 pre-existing parseDouble("nan") failures,
3 optional asset/atlas skips. Eight added tests cover assembly, concentration,
production ledger, exploration, reactor rules, civic purchase value, capture reserve
and recent-performance save/load. Logs: /tmp/dunecity-599-build.log and
/tmp/dunecity-599-tests.log. No game launched or restarted. Real-map squad navigation
and effectiveness need the next match; compile/policy tests do not prove combat wins.

# Player-centred metaserver analytics — 1.0.598

The structured payload now follows the existing multiplayer start model: map,
mod, version, and one row per actual player with display name, house, team and
controller. End events add house-owned results to each participant, including
spice, totals and sparse `[item id, name, kind, produced, killed, lost]` rows for
every unit or structure with activity. QBot rows retain final production weights
and combat score components. The payload schema is v2 and the bound is 64 KiB;
zero-only item rows are omitted. The metaserver normalizes item rows into
`analytics_player_items` and migrates existing SQLite databases in place.

Legacy multiplayer clients below v1.0.598 still create start-only rows from the
existing `House: Player` list. Newer multiplayer announcements no longer create
an additional legacy analytics row because the structured start/end reporter
owns that match. Python fallback storage was verified for start/end upsert,
player identity, item rows, QBot rows, and migration from the old player table.

# Metaserver analytics retry — 1.0.598

Production accepted both the start and end summaries for the completed v597
match. The preceding match had one start request hit the client's three-second
HTTP timeout with zero response bytes, while its end summary succeeded. Twelve
production health requests then completed in 0.814–0.936 seconds, so this was a
transient transport/server delay rather than an ongoing SQLite outage.

Compact match start/end writes now retry once with the same opaque match ID and
the same three-second bound. The metaserver's upsert makes this idempotent even
when the first request completed after the client timed out. Both attempts stay
on the analytics worker and remain independent of simulation and multiplayer
lockstep. The end payload now fills the existing SQLite damage-value and kill-
bonus columns separately as well as their combined reward; these fields were in
the deployed schema but had been omitted by the client serializer. No credentials
or local decision logs are added. Multiplayer display names are retained in the
participant rows, matching the existing game-start announcement.

Version 1.0.598 builds and signs successfully; dependency records are complete.
Ctest reports 460 passed, the two known `parseDouble("nan")` failures and three
skips. The retry path itself awaits a real transient failure in a future game.

# Cash-first city MCV expansion — 1.0.598

Session 1788799572693304-0 v597: both houses stayed at two yards with
~100k–140k credits because only one R/C/I valve was positive. A second positive
valve raised the target to six; both reached six within ~40 simulation seconds.
User rejects demand gating. Wealth now sets minimum yard targets5 at20k,
6 at50k,8 at100k after existing production commitments, even with no positive
valves. Low-cash demand targets remain. City MCV production/unlock upgrades
now precede extra harvesters and generic upgrades, preserving working cash
for a tank, needed harvester/refinery and minimum1000. Queued/live MCVs count
towards capacity; engine ground limits still apply. Vanilla unchanged.
Removed old late city MCV branch. Telemetry policyv37 adds city_mcv_cash,
city_mcv_working_reserve and city_cash_construction_capacity order/unlock rule.
No new persistent state or RNG; no game restarted.
Build, dependency checks and signature verification passed. CTest460 passed,
2 existing parseDouble("nan") failures,3 atlas skips; no new failures.

# Power-demand forecast and earlier turrets — 1.0.597

Latest completed session `1788797915444105-0` v596: houses0/6/7/3 ordered
85/79/74/78 windtraps and0/0/1/1 nuclear plants. Incremental reserve top-ups kept
the immediate gap below reactor break-even even with tens of thousands of cash.
Generator comparison now adds a two-minute demand-growth forecast, sampled each
30 simulation seconds, bounded by current demand and zero for flat/falling demand.
Forecast sample cycle, previous demand and projected growth are saved as of
SAVEGAMEVERSION9829 (old saves default to empty forecast). No wall-clock/random
state. Telemetry includes forecast_growth/seconds, nuclear site availability/price.
Offline snapshot approximation found13–22 financially eligible choices for houses
0/6 instead of zero; this is not a live placement test or predicted order count.

User-requested rocket land-value bonus halved30->15, radius unchanged. Shared
simulation/AI helper and expected tests updated. Profitable crime-reducing R/C
turret slot now checks before reserved police/service, every three non-service
orders. Exception: if >half developed zones are dangerous, strongest-service
comparison retains priority. Still requires some actual crime reduction and R/C
tax gain; zero-crime civic turrets remain forbidden. Police stacking unchanged.

Build/dependency/signature checks passed; no game restarted.

# Power choice, turret returns and spice workers — 1.0.596

Implemented user-approved v595 recommendations. City generator choices now use
current shortage + queued consumer demand + existing city reserve. Compare cost
of windtraps needed against reactor price, and count disjoint legal wind sites
to detect land shortage. Nuclear must leave working cash for a tank, needed
harvester and pending refinery expansion; actual brownouts may use this reserve.
All city power orders pass through shared final choice; vanilla unchanged.
Logs `city_generator_choice` demand, cash/reserve, wind count/sites and choice.

City harvester target no longer depends on number of combat vehicles. It is
min(map-share sustainable target, 3 * actual refineries). Orders need price plus
one tank's cost rather than price+1000, still one per build pass with actual
engine capacity checks. Other factories retain combat production; vanilla's
existing 1000 cash threshold is unchanged. Existing city bootstrap/refinery
expansion remains in effect.

Added an early profitable rocket-turret investment slot every four non-service
orders after core economy/factory bootstrap. Requires land-value tax plus
conservative growth tax alone to repay construction/upkeep/power/placement cost
within one year; crime utility cannot qualify this slot. Cash reserve and queued
costs are protected. Uses existing marginal coverage/park calculations and road
junction placement, so existing/planned services diminish additional benefit.
Police still competes for emergency crime and ordinary service orders; no hard
ban. Structure selection rule `turret_land_value_investment` identifies orders.
User correction before release: all civic turret candidates must reduce some
actual crime (zero reduction is rejected). Early land-value slots must additionally
improve R/C value. Ranking adds a 50% preference on the R/C share of forecast tax
gain; this is placement utility only, not extra reported income or simulation tax.
Actual payback checks use unmodified income. Candidate telemetry includes
`res_com_tax_gain`. Military threat-response branches remain distinct.

Build/dependency/signature checks passed. New policy tests cover generator
cost/space/reserve choices, harvester capacity and turret tax-only payback.
No game restarted; runtime effectiveness remains to be evaluated next game.

# Nuclear balance and completed-game review — 1.0.595

User set nuclear price2000 and nominal output2000. Updated source default plus
installed `mods/dunecity/ObjectData.ini`; other installed mods untouched. Existing
health-scaled nuclear output remains, windtraps stay300credits/100power independent
of damage while alive. Version reseeding will carry source defaults into DuneCity.

Completed session `1788795517885598-0` ran v593 (not v594 opening). White slot3 is
Fremen. At minute5 its land value68 vs orange Mercenary41, with identical population
and almost identical spice income. Earlier rocket service orders (minute9.39 vs
12.27) preceded value219/crime33 at minute12 vs orange55/210, producing much higher
tax income. At minute25 white had14ref/40harvesters/7HF vs orange5/13/3. Final white
city net241814 plus spice148545; orange125617+61865. White lost0ref/0HF/7harvesters;
orange11/5/21. All houses alive when user ended game. Purple Rebels also strong,
with18HF and124launchers at end; do not call this a confirmed white win.

Recommendation ONLY (not implemented): choose generators by actual/queued near-term
power shortfall, free legal land, industrial demand and available cash after
refinery/harvester/military reserves. Wind suits small incremental demand and
industrial jobs; nuclear saves land and wins direct capex once >=7 windtraps would
be needed at current prices. Preserve nuclear blast clearance/health risk. User's
claim equal power per credit is incorrect: wind3credits/power, new nuclear1.

# Spice-first city opening — 1.0.594

Live v593 session `1788795517885598-0`: all four houses ordered only one
refinery despite ~193000 spice share each. The first factory saving rule was
too early; radar/light/power spending delayed the factory until cycle~16000
while only one refinery supported income. City opening now reserves for up to
three refineries (bounded by sustainable map-share harvester target), ahead of
R/C/I seeding and factory prerequisites. Refineries provide initial harvesters.
Missing legal sites do not lock planning. Queued refineries count. Optional
power-surplus construction waits until this opening is complete; actual power
shortage recovery still precedes it. Added `city_spice_opening` telemetry with
target/count/spice share/price/funding. Vanilla unchanged. Policy tests cover
rich/scarce/no spice and a one-harvester limit. No game restarted.

# Starter survival and completed power placement — 1.0.593

Session `1788794508386046-0` confirmed tiny-settlement gang outbreaks: first
house4 outbreak cycle8346, residential population40 (800 displayed residents),
local crime250. Custom unrest now requires 5000 displayed total population per
owner, resetting progress below it. Micropolis crime calculations remain intact.
Outbreak events include population/minimum_population; boundary tests added.

House4's completed reactor was rejected seven times at (12,183): legal footprint
and blast clearance but threat300–500. Completed generators now fall back to
the least exposed legal site, maintaining blast spacing, roads and zone access.
Logs `placement_power_recovery`. Ordinary planned construction keeps its threat
veto. Primary city deficit/reserve power rules require funds for nuclear orders,
otherwise using windtraps instead of tying up a poor starter yard.

House5 ordered 75 R/C/I before its first heavy-factory order at cycle62495;
radar only cycle58795. Cheap zones consumed cash below infrastructure thresholds.
After one R/C/I seed and refinery, city AI saves actual price for an available,
placeable heavy factory or radar/light prerequisite, ahead of further zones or
civic services. Logs `city_bootstrap_reserve`. Extra city refineries now need
their price plus300 instead of4000, still requiring fleet demand and a factory.

Build, dependency checks and bundle signature passed. Ctest:455 passed,2 known
parseDouble("nan") failures,3 skipped (460 cases). No game launched/restarted;
priority changes still need live gameplay validation in the next test.

# Early crime and refinery retreat — 1.0.592

Session 1788793767206934-0 (v590, 4P192 DuneCity) showed average crime250
at cycle3800 with only 40 residential population. Density was incorrectly stamped
as overlapping radius2 halos. Now uses Micropolis populationDensityScan exactly:
point-set source min254, three non-dithered centre+cardinals /4 passes (clamp255),
then byte-map doubling. Map block2 is retained. No invented population crime cap.
Reference scan.cpp populationDensityScan/smoothDitherMap; default donDither=0.

Harvester safety logged 21 redirects; several vehicles carried 350+ spice with
current danger0 and old destination danger300. Visible threats were triplets of
troopers spawned by crime. Foot infantry no longer adds to harvester danger (still
counts for tactical defence). Threatened/unsafe-destination harvesters prefer a
safe owned refinery/dropoff using existing network movement commands. An active
safe unload trip retains control instead of being overwritten by spice searches.
If no safe refinery corridor exists, prior safe-field/dispersal fallback remains.
No game launched or restarted.

# Shared civic investment selection — 1.0.591

Replaced the city service picker with a shared police/rocket search, evaluating
legal police footprints and turret junctions independently. The prior fallback
amenity picker no longer bypasses this comparison. Essential military/power
priorities are unchanged. Each candidate compares credit-equivalent benefit
(occupied-property crime removed + one-year tax gain + conservative growth tax
+ threat-based defence value) against construction + funded annual upkeep +
power cost + a separate police overlap placement penalty. Emergency allocation
requires >=100 aggregate crime reduction; low-crime amenities can qualify with
positive net return without any immediate crime reduction.

Tax gain uses actual total city population, tax rate and property-average land
value sensitivity. Park prediction matches the existing coarse-cell accumulation
in stampFalloff, saturates at 250 and includes reserved amenity projects. Police
can earn tax credit by removing the existing >190 crime land-value penalty.
Growth is explicitly an estimate: up to 25% of one demanded additional level,
scaled by value improvement, only while powered and pollution <128. Defence is
a weighted estimate from visible armed enemies within 12 tiles of heavy factories,
repair yards and reactors (reactors doubled); existing/queued turrets discount it.
No enemy threat means no defence credit; unpowered turrets receive none.

Both best eligible candidates and the winner are logged as city_service_candidate
and city_service_investment, including every score/cost component. Policy tag is
civic-investment-v36. Integer ordering and deterministic tile traversal preserve
multiplayer behaviour. No game launched or restarted.

# Crime construction allocation and police spawn limits — 1.0.590

For populated, living owned R/C/I zones, dangerous means crime >=192. At >=25%
dangerous, reserve one in four construction orders for crime services; above 50%,
one in two. Essential power/bootstrap recovery still runs first. A saturating
non-service order counter advances only on accepted non-road/non-slab orders,
resets on a selected crime service, and is persisted in save version 9828 (older
saves default to immediate response). Existing fallback service selection remains.
Chosen crime-service location is retained instead of reselecting a different
defence site later. Telemetry includes the zone counts, interval and counter.

Police cooldown is twice the palace cooldown. Police batches stop at 100 military
units owned by that player (transport/harvesting/MCV/ambient excluded) and retain
house limits. Sidebar overlay reads "Unit limit reached" while capped and clears
automatically. The suggested 2000 map-wide cutoff was rejected and removed.
QuantBot Brutal-controlled houses also check
each unit against the same military valuation as the production allocator,
including earlier spawned batch members. At/above the limit nothing spawns;
fully blocked batches retain readiness. No game restarted.

Current Twin Cities session 1788790735257321-0 is still 1.0.589. Snapshot during
analysis: Harkonnen 49 police/5 rocket crime selections; Ordos 60/10. Old comparison
rejects zero-crime-benefit turret sites even with amenities, and uses raw amenity
points rather than projected tax or upkeep. User requested analysis, not a new
turret-versus-police balance change. Keep that comparison intact for now.

# Crash repair — 1.0.589

The September 7 23:55 crash was a stale-object ABI mismatch, not police diffusion.
macOS report `dunecity-2026-09-07-235532.ips` identifies the fault at
CityStatsBox::update +3032 (the in-game signal stack misleadingly reports the
previous call return address +1852). Disassembly reads pollution vector data at
CitySimulation+0x4a8 while the rebuilt simulation stores it at +0x5a8, following
the new environment summary fields. StructureBase.cpp.o was two hours old and
contributed the obsolete inline CityStatsBox implementation. Ninja recorded zero
dependencies for it and five other objects, so header changes did not rebuild it.

Performed a complete clean build. Verified sidebar now uses pollution+0x5a8 and
land value+0x5c0, matching simulation initialization. All existing object dependency
records are populated; bundle signature passes. Added scripts/check-build-deps.py
and required pre/post-build checks in AGENTS.md. Guard tested against a deliberately
empty Ninja dependency record. Crash log/binary preserved in /tmp/dunecity-588-*.
No game launched. Gameplay confirmation remains for the next user test.

# Police diffusion — 1.0.588

Replaced linear police halos with Micropolis source accumulation followed by
three `(center + neighbours / 4) / 2` integer smoothing passes. Full source
strength is 1000, turrets 150; funding, missing power, and missing perimeter
road scale the source, emitted at the first road. Grid uses six tiles instead
of eight to preserve two zone-plus-road pitches (2+1 here, 3+1 in Micropolis).
Actual overlapping sources are summed before smoothing, never penalised.
AI spacing penalty remains placement-only. Its isolated-source estimate uses
the same diffusion/boundaries, with small rounding differences versus combined
sources. No running game relaunched. Prior 586 build failure was corrected:
missing TextManager include. Budget summaries/sidebar categories are in bundle.

# Full-capacity heavy-factory allocation — 1.0.579

Live vanilla session `1788769143717332-0`, 5P128 All against Atreides, showed
Atreides with 6k–74k spendable credits and 8k–55k military against an 80k cap.
Of 7,109 heavy allocation decisions, 6,805 were
`no_affordable_positive_deficit`, leaving factories idle because the normal
one-unit mix horizon considered a proportionally balanced small army complete.
When that happens below the military cap, the allocator now uses a deterministic
doubling expansion horizon, selects the largest affordable configured-mix
shortfall, and fills the lane. It repeats in stages until the cap or resources
become the constraint. `expansion_horizon`, `expansion_fallback` and candidate
`expansion_deficit_scaled` make the reason directly queryable in telemetry
version 6 / policy `full-capacity-allocation-v31`. No game launch, commit or push.

# Main-force harvester strikes — 1.0.578

Removed the below-threshold 2–6 unit recovery raid. At a qualifying attack
window, a stateless multiplayer-safe roll selects a safe exposed enemy harvester
about one third of the time; every available main-force unit receives a forced
order against it. The base and harvester-escort reserves remain assigned. A
turret-covered field, returning/non-harvesting harvester, or local defender value
above 2000 falls back to the ordinary HUNT wave. The force budget is the entire
available force for a strike and the existing deterministic commitment percentage
for a hunt. `harvester_strike` events, five-second progress/outcome samples and
SQLite operation labels make full-force outcomes queryable; old captures remain
`legacy_small_raid`. No game launched, commit or push.

# Neutral radar visibility and light-raider tactics — 1.0.577

Neutral now uses a dedicated bright cyan radar marker in every mod, instead of
the vanilla grey palette entry that merges into rock. The override is minimap
only; Neutral sprites, UI and lobby colour mapping stay unchanged.

QuantBot trikes, raider trikes and quads now evade an armoured tank that is
actively targeting them within its weapon range, retreating two tiles beyond
that range. A tank hit uses the same immediate retreat even between AI updates.
While hunting and not on a forced command, light raiders choose local visible
launchers, harvesters, light raiders and infantry/troopers within 12 tiles over
their normal target. Decisions are deterministic and logged as
`light_raider_evade` and `light_raider_target`. No game launched, commit or
push.

# Approved final573 follow-up — 1.0.576

User approved recommendations 1,2,4,5 and explicitly declined 3. Implemented:
custom-match main-wave minimum actual dispatch of 6 units / 3000 value with
15-second retry; stable harvesting anchor (30-second dwell, 25% larger cluster,
immediate danger/depletion override); largest affordable positive HF allocation
deficit with queued units and one-funded-unit horizon, no unconditional tank
fallback; raid members/rewards/losses/outcomes and sampled duration logging.
Campaign dispatch thresholds are preserved. No ornithopter gate change: still
planning money >1200. No other air/production strategy changes.

Save format 9827 adds QuantBot rally selection cycle after supportMode. Older
saves expire the initial dwell. Pure fixed-order integer policies use no RNG.
Raid observations are runtime-only and do not affect decisions. Game teardown
flushes active raid outcomes before object cleanup and logger shutdown.
New SQLite views and details in AI-DECISION-TELEMETRY.md. No gameplay launched.
Built 1.0.576. C++: 444 passed, 2 existing parseDouble("nan") failures, 3 skipped.
Python analytics: 11 passed. Version metadata, bundle and codesign verified.
No launch/restart, no commit/push.

# Final573 match analysed

AI-573-FINAL-ANALYSIS.md;49,427events,auditclean,finalsummary/session_end.
LocalOrdosdefeat;Atreidesalsoeliminated,Sardaukar58,650army/25harvestersdominant,
Neutralalive1390army/2harvesters. Economyrefined218789Sardaukarversus113325/160099/
173800;harvesterloss27vs63/55/48. Gunselection0;defeatedcarryalls0;kitingcommandsactive.
19recoveryraidorders,outcomesnotlogged. Sardaukarrally1581evaluations543distinct
suggestions49>10tilejumps;do notclaimallareexecutedrelocations. Mainwaveeligibility
bug:25,350armybut900eligiblecaused1unitwave,41forcedground. Recommendations:
minimumeligiblemainwave;stableharvestanchor;perclusterescortmetrics;costbasedairgate;
deficit-basedHForders;raidmember/outcomelogging. No newgameplayeditsinanalysis.

# Ornithopter live review and decision diagnostics — 1.0.575

User askedwhyfewornithopters. Currentmatch573session1788753759241895-0,vanilla
4corners seed1137063083. AI-573-ORNITHOPTER-REVIEW.md capturesstable20minsample.
Air reward/lossAtreides.36,Ordos.57,Sardaukar.45,Neutral.70; targets~9.6/14.8/7.2/6.8%.
25/39/19/28acceptedairordersby20min. Actualaircountslowbecauseoflosses,notzeroorders.
Code subtractsqueuedcommitments/priorordersand>=2kreserve, thenrequires>1200for600
orni; carryallsandupgradebranchhavepriority. No gameplayretuningin575.
Addedperiodicair_production_decisionwithprecisereason,planningbudget/threshold,
shares/committedvalue,carryallcount/target,andproducerstate. Availableonlynew575runs.
Refactoredbranchbooleansmatchpriorconditions. Built575,440C++pass,2existingnanfail,
3skip;10Pythonpass,version/bundle/signatureverified. No gamelaunch/restartorcommit.
SQLitebuild/review-573-live.sqlite importedlivecapture; onepartialtaildeferrednormal.

# Required-power display restored — 1.0.574

WindTrapInterface always shows numeric Required alongside Output and Produced,
including vanilla. RemovedPower:Notrequiredreplacementwhichhidactualdemandwhen
rocket-turretpowerwasenabled. Displayonly; simulationpowerpolicyunchanged.
Build574, version/bundlesignaturecheck; no additional testsforlabel-onlychange.
No game launch/restart, no commit/push.

# Constant windtrap output — 1.0.573

User clarified damage must not reduce windtrap generation; only DuneCity nuclear
plants scale with health. generatorOutput helper returns full nominal whilealive
for WindTrap, AdvancedWindTrap andScoutpost; nuclear scalesonlyisCitySimEnabled.
Zerohealth removesoutput, preservingdeltaaccounting/destructorcleanup. Removed
QBot repairDamagedWindtraps power-recovery specialrule; ordinaryrepairsremain.
Game::load rebuilds producedPower afterallobjectsload fromallfourgeneratorclasses,
so olderhealth-scaledtotals do notremainstale. Powerdemand/saveformatunchanged.
Built573;440C++passed,2preexistingnanfailures,3skip; source/bundleversion/signature
verified. Unitcoverageincludesdamaged/full/dead/noncityreactor andzero-double-removal.
No interactive gameplay/save-load smoke test performed. No game launch, commit/push.
Policyconstant-windtrap-output-v29. Prior572rocketturretpowerexceptionretained.

# Vanilla rocket-turret power and defence preference — 1.0.572

User clarified vanilla: when rocketTurretsNeedPower is on, AI must supply power;
otherwise one windtrap suffices. Ordinary vanilla power bypass stays unchanged
(no production/radar penalty or power upkeep). RocketTurret checks actual global
produced>=required when toggleon, independentofHouse::hasPower bypass; historical
campaign/skirmishAI exemption is retained only for power-required nonvanilla modes.
QBot turretbuffer now respects toggleevenvanilla, restoresgeneration for existing/
queuedrocket turrets if actualpowerdeficit. Existing225buffer and onegeneratorpending
checks retained. Telemetryrocket_turrets_need_power distinguishes vanillaexception.
Vanilla gun turrets came from separate ground_defense fallback. It now chooses
rocket turrets if enabledandtechlevelavailable, never substitutes gun turrets while
waitingforCYupgrade/power. Gunsremain only belowrocket tech or rocketdisabled.
No removal of existing guns. No citycrime/economychanges, no newRNG/saveformat.
Policyvanilla-rocket-power-v28. Built572,439C++passed,2preexistingnanfailures,3skip;
source/bundleversionandsignatureverified. No game launch; no commit/push.

# Harvest-area main force and demand-based production — 1.0.571

User corrected569: extra high-tech needs priorities, not arbitrary cap; only
all factories making ornithopters should justify more. RemovedhighTechFactoryTarget.
needsProductionLane checks completed=committed, >=75%busy, funded unit deficit,
credits>=economyreserve+factoryprice+1000. CY computes next-wave heavy/air deficits
from allocation fractions versus actual+queued units. heavy_unit_backlog rule
comes after essential economy/earlyfactory rules and before optional Starport/tech.
Existing24HFceiling remains; missing first-air unlock remains as before.
Extra air requires ALL completed HTactively makingornithopters (notheld/upgrading),
no pendingHT, funded air deficit, and no heavybacklog unlessHFceiling reached.
Carryall queues alone cannot expandair. builder_status logs bothdeficits/backlogs
and high_tech_building_ornithopters. Snapshotbusycounts update each build pass.

findSquadRallyLocation picks safe adjacent tile beside densest radius6active
harvesting cluster, ignoring returning/inactiveharvesters; stabilizesnearoldanchor.
Refresh500cycles, resting combat units spread5x5nearanchor. Active targets/HUNT/
retreat/forced units andbase/escortroles preserved. Existing fallbackwhen no safe
workingfield. No path guarantee: sampled tile and normalpathfinder governroute.
At a normal attack window, `shouldUseMainHarvesterStrike` deterministically selects
an exposed, actively harvesting enemy harvester about one third of the time. It sends
the entire currently available main force (base and harvester-escort reserves remain)
with forced target orders; otherwise it launches the ordinary HUNT wave. It refuses
turret-covered fields and escorts worth more than 2000. The policy is stateless and
does not consume the multiplayer RNG stream. Telemetry records `harvester_strike`
and five-second progress/outcome samples; SQLite labels older small raids
`legacy_small_raid` and new operations `main_force_strike`.

Built571;438C++pass,2existingnanfailures,3skip;10Pythontestspass. Version/bundle/
signatureverified. No gameplay launch/test, no commit/push. Priorheartbeat paused.
Actualmatch behaviour stillneeds nextmatch validation. Saveversion9826unchanged.

# Final568 analysis and defeated carryall cleanup — 1.0.570

User exited568 with Sardaukar winning. Actual end result ended_without_result;
Atreides defeated, three houses alive. Final46,302 events audited; rewards valid.
AI-568-FOUR-CORNERS-ANALYSIS.md includes all-house performance/value/kill bonuses,
allocation histories at5minute intervals, economy and recommendations.
Engine log copied build/review-568-final-engine.log; heartbeat paused after final.
Carryall::update now destroys carrier when owner !isAlive(), returning immediately.
Uses normal destruction/bookings/cargo cleanup, also inherited ChemicalCarryall.
No recursive iteration over global units in House::lose. Team0 remains alive under
existing rules; an owned combat unit/MCV still prevents defeat as before.
Built570, version/bundle/signature verified;436 tests pass,2pre-existing nan failures,
3skip. No gameplay launch or runtime defeat test performed. No commit/push.
Recommendations are not implemented automatically;569 factory/kiting fixes included.

# Air capacity and defender kiting — 1.0.569

Live568 session1788747908951557-0 is vanilla4corners seed78385311, four Qbots
Atreides/Sardaukar/Mercenary/Neutral. By~12minutes each had12 heavy factories,
3–4 high-tech,15–17 refineries; cash fell to2–3k. No evidence to raise HF cap
again from this snapshot. Air expansion had no ceiling, only all-busy check.
Now high-tech target clamp(1+completedHF/8,1,3); queued high-tech counts against
it. First-air unlock remains unchanged. builder_status adds target/busy.
Defender/escort role early return bypassed existing launcher/deviator kiting.
Close ground-target check now runs before role exclusion. Existing range-2
trigger and Easy exemption retained. Stationary units no longer suppress kite
because of stale destinations; only genuinely moving-away destinations do.
Existing path-queue stress guard and retreat geometry retained. combat_kite
records issued moves for subsequent analysis;568 cannot show these new events.
Policyair-cap-kiting-v26; no save or random-stream changes. Monitor heartbeat
review-next-dunecity-match active every5minutes for exact568 session, quiet unless
material new findings; DBbuild/review-568-live.sqlite, statebuild/next-match-monitor.json.
No game launch/restart/control, no commit/push. Built569 for next user launch.
Validation:436 C++cases pass,2 pre-existing parseDouble nan failures,3skip;
10 Python tests pass. Version/bundle/signature verified. LiveSQLite31,337events
auditclean;6,192 reward rows,zero component-total mismatches;1,152 allocationrows.

# Value damaged plus20% killing-blow score and SQLite — 1.0.568

User approved credit-weighted actualdamage plus20%unitcostkillerbonus, thenaskedall
statsinSQLite. CombatReward.h calculatesclippedHPvalue, unit-onlykillbonus,noallied/
healing/deadobjectreward. ObjectBasecaptureshealthbefore/after, creditsattackingtype
once. Structuresgetdamagevaluebutno20%unitbonus. Deviatorconversionpreservesold10/100%
creditproxyseparatelywithoutkillbonus. House rewardstatsintegercreditmilli+HPmilli,
hits/killingblows. QBotusesreward/lostvalue,3kcreditrewardlearningthreshold; rawdamage
stilllogged. Existingvanillablend/capsandopeningavailabilityretained.
Save9826 persists rewards + rawdamage + pertype losses (previouslynotpersisted).
Olderloadsfreshrewardhistory; streams gatedonloadedversion. House summaryserializer
acceptsObjectDataparameter so Game destructor doesnotdereferencepossiblynullglobal.
Policyvalue-kill-reward-v25. All-houseperiodicsnapshots+game_summaryincludecombat_rewards.
SQLcombat_reward_samples/final andunit_allocation exposeallcomponentsandshares.
Noextraeveryhitevents. UpdatedAI-DECISION-TELEMETRY.md hascolumnsandexamplequery.
Do not claim old564captureincludesnewrewards. Pythonanalytics10testspass; old559DB
upgradedwithviews,auditcleanandnonewrewardrows(noinventedhistory).
Built568; CTest434pass/2existingnanfailures/3skip; Pythonanalytics10pass.
Source/bundleversionandsignatureverified; logsbuild/combat-reward-568-{build,tests}.log.
No game launch/restart; no commit/push.

# Tech-aware opening mix — 1.0.567

User wants small high-tech trike/quad opening ratios and tech/availability-dependent
defaults, plus advice on improving learning. UnitMixPolicy::openingMix allocates
light15%attech4,8%at5–6,4%at7+,30%below4whenheaviesalsoavailable. Onlylightavailable
means100%ofavailablevehiclemix; nofactoriesmeansallzero. Quadsweight2,trike/raider1
withinlightshare. Heavy/airhouseconfiguredratiosrenormalizeoveravailabletypes.
ActualownedLF/HF/HighTechbuildlistsdetermineavailability(includesupgradelocks);
CHOAMignored. Baselinesrefreshasproductionunlocks/disappears. Learningretains566
scoringandvanillablending; scoresmaskedforunavailabletypes. No hard4%learnedcap.
Infantrydifficultyquotaunchanged. Policytech-aware-opening-v24; unit_mixtech_level,
opening_light_bps; mix_inputsavailable/opening_bps. Deterministicintegerhelpers,
noRNG/savechanges. Testscovertechbands,unavailabletypes,missingproducers,upgrades,
zeroconfigfallbackandexactsharetotal. Built567; CTest432pass/2existingnanfailures/
3skip. Source/bundleversionandsignatureverified; logsbuild/tech-opening-567-*.log.
No game launched/restarted.
Algorithmrecommendationsareproposalsonly: AI-ALLOCATION-IMPROVEMENTS.md.

# Adaptive trikes and quads — 1.0.566

User explicitly requested trikes/quads participate in damage-versus-loss allocation.
QuantBot now allocates one normalized8-type vehicle/air mix: tank,siege,launcher,
specialgroup,ornithopter,trike,raidertrike,quad. New UnitMixPolicy.h usesint64scores
(damage*1e6/(lostreplacementvalue+oneunitprice)); specialgroupkeeps700prior.
Negative damage clamps0, disabled/tech-ineligible types get0weight. Learningdamage
nowincludesSonic/Deviatorandlighttypes, previouslyomitted. Zero-scorefallbackavoids
olddividebyzero. Openingdefaultlightshare12/16/20/24%difficulty dividedacrossenabled
lighttypes, deductedfromheavy/airdefaults; infantryquotaremainsseparate.
After3000damagelearnsall8together; vanilla50/50baselineblendand25%aircapretained,
80%singletypecapwherealternativesexist. Othermodesunblendedlearningstillapplies.
Lightfactoryselectshighestpositivevalue-deficitamongavailabletypes(countsqueues),
notfewestowned; onlyprelearningminimum2bootstrap. Citylightproductioncancontinue
withHFpresentwhenadaptiveallocationcallsforit. Acceptedordersdeductplanningcash.
ExistingHFopportunistictankfallbackremains; these aretargets,notexactarmycomposition.
Policyadaptive-light-vehicles-v23. unit_mix adds allocation_types=8, trike/raider/quad
bps, light_vehicle_bps,total_damage,mix_inputs(damage,lost_value,score). All8bpssum10000;
older5fieldscoveredheavy/airalone. rawOrni nowrawuncappedshareacross8beforeblend.
Testscovercostefficiency/losses,zero/negativedamage,openingdefaults,disabledraider,
normalization/caps,queuedvalue-deficitsandreproduciblepeerresults. Built566;
CTest430pass/2existingnanfailures/3skip. Version/bundlesignatureverified. Logs
build/adaptive-light-566-{build,tests}.log. No game launch/restart, no new save/RNGstate.

# First High Tech Factory priority — 1.0.565

User reports slow High Tech Factory. Live564 session1788709485434043-0, vanilla
All against Atreides seed1806040624: firstHeavyordered1.81min, MCVs2.59–3.81min,
16heavyordersbeforefirstHighTechat5.44min. Our earlyfactorypriority delayedunlock.
565 customvanilla now selects firstHighTech afteranactualHFexists, beforeexpanding
refineries/repeatedHFpriority. Checksaffordability, actualtech/placementavailability;
queuedHighTechcountpreventsduplicatesacrossyards. ExistinglaterfirstHTfallbackand
extraHTbusycapacityrulesremain. Earlieremergency/power/firstrefineryrulesretained.
Policyvanilla-early-hightech-v22; rulefirst_air_productionidentifiesthenewselection.
No city/Tornie/campaign changes. No game launch/restart. Built565; CTest426pass/
2existingnanfailures/3skip. Source/bundleversionsandsignatureverified. Logs
build/early-hightech-565-{build,tests}.log.

# Vehicle-focused custom vanilla — 1.0.564

User says the infantry barracks is unnecessary. Custom vanilla QBot no longer
selects Barracks or WOR in its generic construction priority, freeing yard time
for vehicle infrastructure. Existing infantry buildings may still produce units;
campaign rebuilding and other mods remain unchanged. Includes563parallelMCVs.
Telemetry policy vanilla-vehicle-opening-v21 identifies this build; no schema change.
Built564, CTest426passed/2existingnanfailures/3skipped. Source/bundle versions and
signature checked. Logs build/vehicle-opening-564-{build,tests}.log. No launch.

# Parallel MCV expansion — 1.0.563

User explicitly requested multiple MCVs. Removes the one-pending-MCV restriction
for custom vanilla priority. Each eligible idle factory can order an affordable
MCV while actual yards + existing/queued MCVs is below the cash/economy yard target
(max8). Counts and spending update after each accepted order, preventing same-pass
overshoot. At~100k with1yard, up to7MCVs can be pending across available factories.
MCV unlock upgrades can also run in parallel, bounded by the remaining shortfall;
upgrade counts are reconstructed each build pass, with no new saved state or RNG.
City/Tornie behavior unchanged. Telemetry policy vanilla-parallel-mcv-v20 adds
mcv_shortfall and mcv_upgrades_in_progress; old boolean mcv_upgrade_in_progress kept.
Built563, CTest426pass/2existingnanfailures/3skip; version and signature verified.
Logs build/parallel-mcv-563-{build,tests}.log. No game launched/restarted.

# Wealth-funded vanilla factory expansion — 1.0.562 (2026-09-07)

User wants the100k custom vanilla opening to expand aggressively viaMCVs/HFs.
Latest two sessions are559 campaigns (SCENH019/022), not a new custom test; no561
capture. Requeried build/review-559-vanilla.sqlite: factory target1 at97k, then
tech-policy blocks at93–95k. See appended AI-559-VANILLA-ANALYSIS.md follow-up.
562 changes vanillaFactoryTarget to allow cash-funded capacity above harvester cap:
min(existingpolicy,max(harvesters/3,1+max(0,spendable-10000)/4000)), bounded1..24.
Early custom vanilla factory selection at>=20k targets2HFs/CY, countsqueued, runs
after refinery needs andbeforestarport/optionalinfra. Legalavailability/placement,
army/unitlimits remain. Vanilla usesactualtechavailability, removingextraRepair/IX
policygate onHF expansion. City/Tornieunchanged. Includesall560/561MCV,cap,mixfixes.
Policyvanilla-cash-expansion-v19; rulecash_factory_expansion, builder_status adds
heavy_cash_target/heavy_economy_target/heavy_opening_target. No RNG/savechanges.
Build562 completed; CTest425pass/2existingnanfailures/3skip; bundle/version/signature
checked. Logs build/cash-expansion-562-{build,tests}.log. DO NOT launch/restart game.

# Current vanilla review and combined arms — 1.0.561

DO NOT launch/restart the game; no commits/pushes. Report AI-559-VANILLA-ANALYSIS.md.
559 session1788699472069518-0 finished at17.97min, ended_without_result/allhousesalive.
7990 events imported into build/review-559-vanilla.sqlite, audit clean; engine log
preserved. Army at10.16min only12590 versus41390 in557 (differentseed). Early wealth
failed to accelerate yards/MCV upgrades. Pending560 fixes below address this and
named-housecap40→60 fornewmatches. No560testmatchhasoccurred.
561 blends learned vanilla unit mix50/50 withconfiguredhouse mix andcapsair25%;
78%airtarget hadsqueezedlaunchers/Sonics, thenHFtankfallbackdominated(136built125lost).
Openingmix andcity/Tornieadaptationunchanged. Policyvanilla-combined-arms-v18.
Telemetry raw_ornithopter_bps, blended_damage_per_loss basis, forced_with_target/
forced_without_target. Do notcancel forcedorders blindly: mayalreadybefighting.
IMPORTANT: pre561 house_comparison.military wascumulative, notcurrent. Corrected
usingunitcounts×priceexcludingMCV/harvester/carryall/worm; military_basis marks it.
Build561 successful; CTest424pass/2existingnanfailures/3skip. No game restarted.

# Wealthy vanilla MCV priority — 1.0.560 (same pending build)

User observed559 with~95kcredits,18harvesters/6refineries,1HF/1CY,noMCV at6.61min.
They explicitly want MCVs prioritised with plentiful cash. Supersedes558 strict
harvester-gated yard target: target=max(economy target,1+spendable/10000), max8.
Vanilla custom QBot prioritises affordable MCV before more harvesters, one existing/
queuedMCV at a time; also prioritises the required HFupgrade before harvester orders
can starve the unlock. Only one factory unlock upgrade is in progress at a time.
Keep2kreserve andprice+1kspendableguard. Other factories can keep producing harvesters
while theMCV isqueued/deploying. City/Tornie order unchanged. Source560 also includes
named-house60capfix below. Current559game is unchanged; no restart/launch.
Policy vanilla-mcv-v17 adds cash_construction_capacity order rule, mcv_unlock event,
vanilla_yard_target computed fromcurrentloggedspendable, mcv_upgrade_in_progress.
Tests cover wealth override, affordability, oneMCVpending andmax8.
Built560 successfully; CTest423passed/2existing nan failures/3skipped. Version/plist
560 and bundle signature verified; logs build/mcv-priority-560-{build,tests}.log.

# Named-house vanilla cap correction — 1.0.560

User is playing559 session1788699472069518-0, vanilla All against Atreides,
seed2045383069, QBotBrutal. DO NOT restart it. Current559 correctly logs modeflags,
queue liabilities and house_comparison, but engine/AIcap40 exposed an omission:
558 applied +50% only in INIMapLoader::getOrCreateHouse, not the ordinary named-house
loading path. Both paths now apply the existing tested vanilla capacity helper;
explicit overrides and city/other-mod limits remain unchanged. New matches in560
will use60 here. Existing559 match/old saves keep40. This test can assess other
changes but must not be reported as evidence forcap60.
Monitoring automation reactivated for this exact session, comparisons against557,
then pause after result report. State in build/next-match-monitor.json. No gameplay
changes beyond fixing the omitted default-cap application. Build560 for next launch.

# Visible active mod — 1.0.559

Main menu replaces misleading generic Dune City logo with a centred `MOD: VANILLA`
(or active mod) banner above buttons, uppercase 24px white, thickened lettering on
opaque black. Works in classic/enlarged menu layouts and reflows when mods switch.
Version footer remains separate. Gameplay badge uses20px uppercase lettering onblack,
reads the match's mod from GameInitSettings and sizes to text. Existing watermark
visibility preference is retained. Changes are presentation-only. Build559 succeeded;
version metadata and signature checked. No game launched/restarted; visual runtime
verification remains for user's next launch. No new tests for this small UI change.

# Vanilla loss review and next build — 1.0.558 (2026-09-06)

DO NOT launch/restart the game. No commits/pushes. Built bundle is for user's next test.
Full report: AI-557-VANILLA-ANALYSIS.md. Completed vanilla session
1788694972180753-0 (23.62 simulation minutes), 12,578 records in
build/review-557-vanilla.sqlite, audit clean. Formally ended_without_result, nearly
wiped out. Four allied opponents start with 51 refineries/28 HF/588 rockets; not
an equal-start comparison. QBot had 8CY/6HF/1ref at6min, 40-harvester cap, bankrupt
later; only4 waves,17 army-threshold deferrals. No earlier-binary win-rate comparison.

558: max speed4ms (was8), accumulator allowance supports render pacing. Explicit
user request: vanilla ignores shortages/deterioration/upkeep, radar/production/
rockets use common House power rule. Keep windtrap prerequisites, actual outputs;
city and other named mods retain power rules using session mod settings.

Vanilla QBot prioritizes spice harvesters/refinery capacity; 2k planning reserve;
default harvester caps+50% (huge40→60), explicit lobby limits unchanged; engine old
save caps honored. Queue liabilities deducted from new-order budget. CY target
1+harvesters/8 (cash-bound,max8), HF target bounded byharvesters/3. Optional gun/wall
quotas await fleet/cash; emergency anti-air retained. Brutal vanilla custom threshold
cap24k/Hard28k; respect lower config. Brutal threshold recheck15s. Deterministic
commitment20–100 usesbest3samplesBrutal/best2Hard; city behavior unchanged.

Policy vanilla-economy-v16: queue liabilities, economy reserve, mode flags, both
harvester caps, all-house comparison at QBot snapshots, attack eligibility diagnostics.
See telemetry doc. Build success; CTest422pass/2known nan failures/3skipped; Python9pass.
Logs build/vanilla-558-{build,tests}.log. No runtime win/performance claim. Next
recommendation: general air anti-air corridor screening (59/63 ornithopters lost).

# Final555 capture reviewed after quit

User manually quit cleanly at74.428game minutes. Full197,457records in build/review-555.sqlite;
game_summary ended_without_result + session_end, no capture_limit or simulation_exception.
Allhousesalive. Final ordinarylog build/review-555-game-final.log. See final section of
AI-555-VANILLA-REVIEW.md. Newrecommendation: densityhysteresis/minimumleveldwell; Harkonnen
670declines+658growths inlast10min, individualzones26changes. NOT implemented ahead ofvanilla.
RichAIs24HF/~80karmy cap, so cashstockpile alone doesnotjustify morefactories. Source557
unchanged andready. Do notlaunchgameforuser.

# Live 555 review, city investment and vanilla audit — 1.0.557 (2026-09-06)

**No game launch/restart.** User next match will be vanilla. Built557 in build/bin/dunecity.app.
Review: AI-555-VANILLA-REVIEW.md. Evidence SQLite build/review-555.sqlite through41.87min,
98,194 records; ordinary log preserved build/review-555-game.log. Audit old data clean for
sequence/references (does not prove semantic correctness or completed match).

Critical live bug: Fremen674,200 reported power despite7reactors+2windtraps (max7,200).
Rejected placement constructs a generator and credits power, then directly deletes it;
default destructors leaked the contribution. Repeated failed reactor attempts explain
phantom surplus. WindTrap/NuclearPlant/AdvancedWindTrap/Scoutpost destructors now setHealth(0),
removing remaining power without detonating on cancellation/teardown. Failed House placement
marks cancelPlacement before delete, suppressing fake combat-loss callbacks. Full-health
windtrap demolition is covered too, relevant to vanilla. Existing running555/old inflated
save totals are not retroactively repaired. Next new match is the validation target.

City improvements: stable construction-yard-first planning (store IDs, resolve each time so
redevelopment cannot retain dangling zone pointers); demanded feasible zones ahead of
optional land-value turrets, defensive turrets still first. City MCV expansion keeps existing
income target/max8, one in flight, MCV cost+1000 working cash rather than strict>3000; may use
optional Palace reserve so construction investment does not starve. Accepted MCV subtracts
planning budget. Vanilla keeps money/4000 CY policy and original ordering. Small armies keep
one base defender; empty reserves fall back to configured emergency structure response.

Vanilla: all city-only object entries disabled on new-game init, upgrade-level calculation
also filters them. Generic ObjectData no longer silently rewrites reactor HP; city match init
applies Starport-equivalent HP. CityStatsBox attaches/updates only when city sim enabled;
windtrap output and requested auto-repair/demolish UI remain in both modes. City effects,
Harkonnen ornithopter exception, zoning/overlays/palette remain city-only. General QBot
balancing/escorts/deterministic attacks remain shared deliberately.

Telemetry policy city-investment-v15: post-plan yard_planning_result (pre-plan queue=0 not
lasting idleness), construction yard/MCV counts, planning order flag, crime above250 bin,
power_accounting reported/generator sum/difference in snapshots. Include all4 generator
classes; expose AdvancedWindTrap output read-only for telemetry. SQLite audit aggregates
power mismatches; old captures with missing fields accepted. Generator lifecycle test is
source-integration, not a full renderer/game test; Python test covers accounting alert.

Build successful. CTest418passed/2baseline parseDouble(nan) failures/3skipped; Python9pass.
Version source/config/plist557 agree; no tag atHEAD, no commits/pushes. git diff --check clean.
Detailed recommendations in review: smaller raids below32000 Brutal gate, placement stalls,
coalesced harvester telemetry; assess growth/outage timing after real power totals restored.

# Readable DuneCity house colours — 1.0.556 (2026-09-06)

User's current555 match remains running; DO NOT restart/launch apps. Built556 for nextlaunch.
Neutral is bright cyan, Fremen ivory; standard slots H/A/O/F/S/M/N/R now use distinct
red/blue/green/ivory/magenta/orange/cyan/violet. Definitions include/dunecity/HouseColors.h.
getHouseColorSDL returns these ramps only for active dunecity mod, slots0..7. GFXManager
uses existing private indexed/truecolour remapping path for these slots; avoids editing
shared IBM.PAL terrain/neutral metal colours. Explicit player colour-slot overrides remain.
getHouseRadarColor uses brightest shade; terrain radar colours dim to55% in dunecity
so spice and sand do not dominate ownership dots. Classic/Tornie palettes unaffected.
No simulation/save/network changes beyond matching game-version metadata.

Build success; CTest415passed,2known parseDouble(nan) failures,3skipped. New colour tests
check pair separation, shade order/alpha and Neutral cyan. Logs build/house-colors-{build,tests}.log.
Metadata/plist556. Runtime visual verification remains for user's next launch; no app opened.

# Startup boundary fixes — 1.0.555 (2026-09-06)

User reported match-start exit in553, then again554. STOP launching/reloading the game:
user explicitly requested this after UI verification attempts. No launch of555 performed.

Preserved initial failure: build/startup-553-crash.log contains Map.h:98 Tile(92,-1)
does not exist during initial heavy-factory search. First fix554 bounded road/paving
callbacks via CityPlacementPolicy::assessRoadsOnMap. Regression covers four edges/corners.

Further static audit found fourZoneBlockBonus independently reading off-map neighbours
and its road perimeter. 555 skips block layouts whose full4x4+road perimeter cannot fit;
individual edge lots remain legal, they merely receive no block bonus. Regression checks
all candidate origins/offsets on128x128. Placement failure telemetry also bounds tile reads.
Session 1788691499646985-0 endedcycle95 and1788691615471597-0 cycle99 in554; normal log
was overwritten by later menu launches, so exact second exception was not retained.
Do not claim full runtime verification. New simulation_exception event wraps updateGameState
before destructor closes telemetry; subsequent launches cannot erase that session evidence.

Built555 successfully; CTest414passed,2known parseDouble(nan) failures,3skipped. Logs
build/startup-boundary-{build,tests}.log. Source/plist555; no commit/push. GUI automation
resolved an old /Applications copy and had bundle-cache ambiguity; do not repeat it.

# Final review, multiplayer, unrest and escorts — 1.0.553 (2026-09-06)

Latest old-game capture: 88.17 min,493358 events, audit clean; still live/no session_end.
AI-FINAL-LIVE-547-REVIEW.md contains evidence and difficulty proposal. MULTIPLAYER-553-REVIEW.md
records lockstep review and remaining integration-test limits. Do not mistake old547
telemetry for results from these changes. No game restarted, commit or push.

Build553: attack commitment20–100% of eligible AVAILABLE ground force, deterministic
Uint32 mix of match seed/cycle/house/player. Excludes hunters, forced, damaged, retreat,
base-defender and escort units. Existing attack threshold unchanged; fixed force ratio
INI setting no longer determines main attack size. Logs percent/availablevalue/seed.
QBot aircraft favour visible reactor with half ready wing within12tiles and no visible
AA covering sampled straight approaches. Early distance filter bounds extra work.

Base defenders10% of active ground combat count (floor), prefer launchers; harvester
escorts20%, max2 per active harvester. Derived each check, excludes ongoing hunters/
forced/retreat/damaged, moves beside harvesters, bypasses old rally and attack allocation.
Base damage response restricted to base pool; harvester reactive scramble increased50%.
No claim of full tactical integration testing. defence_allocation logs targets/assigned.

Crime: removed250 and intermediate300 clamps; uint16 crime layer, overlay colour
saturates255 while query/SQL retain real values. Three Unit_Trooper individuals per
outbreak, per-owner16x16district. Timers~176sec at201,90sec250,60sec300+; reset if<=200.
Uses existing living opposing faction, rotating deterministic selection; no newhouse.
Respects unit capacity, enabled flag and local free space. No enemy => no spawn.
crime_unrest logs origin/owner/district/crime/spawned/hostilehouse/members/failure.
Save9825 appends district progress; older saves initialize zero.

Hostile armed visible units within4tiles of a property's footprint reduce landvalue:
max80 atcontact,64/48/32/16 at1/2/3/4tiles; strongest only, no cumulative army blob
penalty, floor1. Friendly/unarmed units excluded. Recomputed, no lingering loss.
Diagnostic hostile_value_penalty map and growthfield/SQLite city_growth column.

Network handshake now hard-rejects different game versions; mod sync cannot fix
executable differences. Tests cover acceptance/rejection and reproducible attack rolls.
Full two-peer play/save/load test remains. Existing foreign-player command validation
and whole-state checksums are separately documented follow-up concerns.

Validation logs build/unrest-{build,tests,analytics-tests}.log. Latest expected baseline:
CTest412passed,2preexisting parseDouble(nan) failures,3skipped; Python8passed.
Source/plist1.0.553. Previous growth/low-power timing recommendation remains UNIMPLEMENTED.

# Stalemate, crime, redevelopment and UI — 1.0.551 (2026-09-06)

Built successfully. CTest407passed,2known parseDouble(nan) failures,3skipped; analytics
Python8passed. Logs build/crime-coverage-{build,tests}.log. Metadata/plist1.0.551,
no HEAD tag, no commit/push/live restart. Visual and match behaviour need next-launch test.

AI-STALEMATE-547-REVIEW.md records live snapshot through40.74minutes,207,915SQLiteevents,
auditclean. 3,998power-associated declines; successive decline median1.248sec versus
growth19.968sec. Recommendations:30-45sec outage grace then~60sec perlevel; growth
45-60sec L1->2 /90-120sec L2->3, decoupled from taxation. NOT IMPLEMENTED timing changes.

Micropolis stacking verified in simulate.cpp1545 and scan.cpp415-432. Fixed duplicate
per-worldtile police stamping into2x2cells; distinct sources still add, existing16tile
falloff retained (not Micropolis diffusion). Wide basecrime300 then coverage then final250.
New derived, unsaved crime_before_police and police_coverage layers/snapshot/growthfields;
SQLite views upgraded with those and police_cost_milli. Policy crime-coverage-v12.

Human Destroy button in DefaultStructureInterface applies to owned buildings in allmodes;
new commands appended. Zones clear without explosions/refund, retain roads/concrete;
other buildings use their ordinary destruction effects, including nuclear blasts. Deliberate
removal excludes combat loss counters/callbacks; zone_demolished/building_demolished logs.
Zone density shows /3, turret lines Park:1fountain and Police:15%.

AI may redevelop up to4low-value(<=64) own R/C/I lots when no normal site exists for
needed heavy factories/windtraps/reactors. Normalized owner demand, density/value and
rear position rank displacement. No hospital/church removal. Demolition ONLY after
successful building order; reserved sites, threat, reactor-spacing and road checks remain.
Concrete is skipped for these redevelopment orders (building can start damaged, normal
repair applies). redevelopment_committed logs removed IDs/item/demand/value/density.

Soft 2x2 zone-block preference keeps each lot2x2; 5tile repeating block+roadgap and
completion bonus. CityRoadImpact models the actual automatic perimeter road additions
so adjacent lots can replace internal road segments without severing connectivity.
Important next-match watch: avoid immediate rezoning of demolished footprints and verify
actual factory placement completes, harvester unloading, and crime balance with true15%.

# Overlay buttons — 1.0.550 (2026-09-06)

Added Land Value and Crime buttons directly below Auto Repair in the empty-selection
DuneCity sidebar. Click an active button to clear the overlay; choosing the other switches
layers. Pressed states follow keyboard shortcuts too. Hidden in normal Dune mode and
while the object panel is showing, like Auto Repair. Local presentation only, no simulation
or save changes. Build log build/overlay-buttons-build.log; CTest402passed,2baseline
parseDouble(nan) failures,3skipped. Source metadata/plist1.0.550, no tag/commit/push/restart.
Live visual check remains for next launch.

# Current follow-up — 1.0.549 (2026-09-06)

Uncommitted; live match remains 1.0.547. Do not restart it. 1.0.548 added DuneCity-only
Harkonnen Ornithopters through both HighTech upgrade discovery and build-list gates;
normal Dune unchanged, standard IX/tech/upgrade requirements retained.

1.0.549: police sidebar reinforcement labels split into short rows, portrait region
fixed-height, taller stats rows, correct Police role. Budget now has station/rocket/gun
counts and separately funded annual costs. Both turrets give ONE fountain bonus (15),
15% police strength. Station100, rocket15, gun7.5 upkeep; FixPoint billing retains
fractions at every funding level. Saved legacy integer expense caches remain compatible
and round the aggregate; telemetry police_cost_milli is exact, police_cost rounded.

Harvester policy harvester-redistribution-v11: actual circular weapon reach instead of
construction's square range+2 buffer; no 30sec shelter veto, no120sec field veto.
Prefer reachable-by-corridor safe spice, soft recent-loss penalty, per-harvester destination
reservations and crowd penalties; if no safe field, disperse nearby without base attraction.
Escape corridor permits leaving danger but rejects rising danger/re-entry. This is a
straight-corridor approximation, not a pathfinder guarantee; checks recur every2sec.
New harvester_safety actions redirect_spice/disperse/no_safe_route log current and old
destination danger, candidate/rejected-route counts, memory/crowding penalties, cargo.

Live session1788686750413469-0 sampled:82,514 retreat commands,58,832 with zero current
position danger,900 already at commanded destination. Destination danger was not logged
in the old decision, so don't infer all58,832 were entirely safe.
User police house4 object1221 at(118,10),cycle38146: roads on all four footprint sides;
21 R/C zones within16 tiles before placement allcrime0, ten nearby rocket turrets.
Good geometric access, poor incremental crime payoff. Don't relocate user's station.

AI police auto-deployment no longer excludes local/spectated AI house (5sec retry).
Human houses retain manual deployment. Command-number combinations no longer trigger
city overlays/groups; Shift+5 land value, Shift+1 off remain.

Validation: build successful; CTest402passed,2known parseDouble(nan) failures,3skipped;
Python analytics8passed. git diff --check clean, three metadata files and app plist1.0.549,
no HEAD tag. Logs build/civic-harvester-{build,tests}.log. Panel layout/behaviour awaits
next-launch visual check; no live restart, commit or push.

# Handover — DuneCity session, 2026-09-06

## City analytics before next match: 1.0.547

User authorized complete city stats logging before starting the next match. Gameplay
unchanged (policy tactical-safety-v10), telemetry4. Added30sec crime bands/threshold
counts, initial/120sec full QBot building snapshots, city level-change causal records,
~120sec unchanged growth evaluations, and global terrain/roads/effect-layer snapshots.
See AI-DECISION-TELEMETRY.md for fields, cadence, raw population scale and phase caveats.
JSONL limit256MiB. scripts/ai-decisions.py has city_buildings/city_growth SQLite views.
Build1.0.547 successful; CTest400passed, same2nan failures,3skipped. Python8passed.
Logs build/city-analytics-{build,tests}.log. No gameplay tweaks, restart, commit or push.
Next match had not started at last check; prior completed session1788680806413568-0.
Heartbeat review-next-dunecity-match active every5min, waits quietly for first new
match, audits/analyzes at completion then pauses. Progress build/next-match-monitor.json.


## Police eligibility correction (analysis only; executable still1.0.546)

User explicitly rejects building police at low crime or for troop payoff. Removed
previous automatic-first-station proposal from AI-TACTICAL-STRATEGY.md.
AI-POLICE-VS-TURRETS.md compares actual costs and proposes persistent harmful crime
+ marginal benefit/payback against legal turret alternatives. One/two extra turrets
normally win; police niche is wide severe residual crime requiring several extra
turrets without significant additional turret amenity/defense value. Coverage must
model coarse stamps, not flat100/15. No police construction code added this turn.


## Strategy clarification and police assessment (no executable change)

User wants the proposed base response force to favour rocket launchers for air.
AI-TACTICAL-STRATEGY.md updated: launcher-heavy anti-air reserve with ground screen.
Army role allocation remains a proposal, not implemented. Source confirms QBot has
no PoliceStation construction rule, though AI-owned stations auto-spawn units.
Documented default economics:500build,20power,100upkeep per60game seconds;1400full
batch purchase value every5/10min. Proposed one station after essential power/initial
heavy production, extras for uncovered harmful crime or actual reinforcement need.
No police-building rule added in this analysis turn. Executable remains1.0.546.


## Tactical safety, factory pressure and reactor defense: 1.0.546

Implemented user-approved items from old-match analysis. See AI-TACTICAL-STRATEGY.md
for exact rules, limitations and the proposed70/20/10 army-role split (proposal only).
QBot caches visible weapon danger every2seconds; checks build and final placement.
Five-minute decaying overlapping structure-loss memory; previous60sec exclusion kept.
Reactors favour rear relative to visible enemy bases, four clear tiles from reactors/
HF/RY/CY (including queued reservations), and seek2rocket-turret coverage, weight2.
Factory target adds2..4 lanes under75% utilisation/recent2min HF losses with>=8000cash;
queued factories count, ceiling24, existing unit/army caps remain.
Harvesters proactively retreat, shelter30sec, blacklist fields120sec, assign safe fields
or wait; immediate damage reaction covers empty harvesters. Straight corridor danger
is a heuristic, actual pathfinding unchanged. Escorted formations are not implemented.
Runtime caches/memories are not serialized (save9824 unchanged).

Telemetry tactical-safety-v10: threat snapshots, placement risk/rejection counts,
heavy_losses_2min, harvester_safety actions. TacticalSafetyPolicy helpers tested.
Build1.0.546 successful, metadata/plist agree. Ctest400passed, same2nan failures,
3skipped; Python importer7passed. build/tactical-{build,tests}.log. No game restart,
commit or push. Needs same-map live test to assess survival and possible over-caution.


## Completed old-match analysis and police batch: 1.0.545

See AI-FINISHED-539-ANALYSIS.md for session1788680806413568-0 (75.94min).
54908 events audit clean; Harkonnen lost with170069credits; 344/627 completed R
zones died within60sec. Engine log preserved build/finished-539-engine.log.
Remaining proposals are analysis only. Source audit finds pollution growth/day parity
coupling, pre-police clamp mismatch, coarse stamp accumulation to investigate.

Police batch now9 individual troopers,1quad,2trikes, within3tiles of station using
complete nearest-first rings. No distant fallback. Palace and police share
Palace::getSpecialWeaponCooldownForHouse:5/10min normally, Tornie Rebels7.5min,
Wildspade10min. Existing save9824 timer layout preserved. UI shows actual seconds.
New police_unit_spawned logs IDs/positions; batch logs quads and skipped reasons.
Build1.0.545 successful, versions/plist agree. Ctest396passed, same2nan failures,
3skipped. build/police-batch-{build,tests}.log. Not restarted or committed.


## Factory cap, police budget breakdown and Palace roles: 1.0.544

User approved raising QBot's heavy-factory ceiling from 8 to24 (both city and
classic paths). City target remains max(1+estimatedTaxPerSec/50,
1+max(0,credits-2000)/2500), now clamped1..24. Classic keeps /4000 cash formula.
Queued counts, military80000 limit, prerequisites and other gates unchanged.
At23000citycredits target9; at59500target24; observed149408treasury nowtarget24.
Policy factory-cap24-v9. Boundary/current-match regression assertions updated.

City Budget now has two full-width rows beneath Police Services total: Police
stations count + annual paid cost, Rocket turrets count + annual paid cost.
Counts are live completed local-house items. Cost scales with pending funding,
uses actual CityEffects cost constants (100/15) and components sum to displayed
total. Forecast nominal uses the same live counts to avoid stale census mismatch.
Window height380->424 to fit44 extra pixels; width420 unchanged.

Palace changed from2R+2C population to one residential and one commercial zone:
raw R16/24/40 and C1/3/5 at occupancy1/2/3. Both share existing Palace occupancy,
capped3. Added commercial supply for Palace, previously missing despite its
commercial population; now both supply/population match one ordinary R/C zone.
No save layout change from9824. Existing Palace sidebar displays both portions.

Built app1.0.544, metadata/plist agree. Ctest396passed, same2nan baseline failures,
3skipped. Logs build/factory-cap24-{build,tests}.log. No restart/commit/push;
UI presentation and live AI effects await user's next launch.


## Auto repair, police reinforcements and city siting: 1.0.543

This supersedes the zero-police rocket behavior in 1.0.542: user now wants
rocket coverage AND annual budget upkeep at 15% of a police station. Values are
15 coverage / 15 yearly cost vs station 100/100. Rocket land-value strength 30,
intersection road connectivity and weighted asset defense siting are retained.

New Auto repair on/off sidebar button below Ornithopter (below Chemical Carryall
in Tornie), visible when nothing is selected, for normal Dune and all mods.
House-wide setting defaults off. Enabling starts normal paid repairs for living
damaged structures with >=5 credits; insufficient funds pause and funded future
updates restart. Off prevents new automatic starts; already-started/manual repairs
finish normally (tooltip says this). Command is attributed to the issuing player's
house, not a caller-supplied house ID, and runs through the command manager.
QBot starts reactor repairs for any damage whenever >=5 planning credits; existing
health-proportional power is unchanged. Fixed rich/turret repair branches starting
repairs on already-full structures. reactor_repair telemetry records health/cash.

Police stations gain the Palace/TechCenter READY picture-button and cooldown UI.
Default batch 3 trikes +6 individual troopers, interleaved, free, deployed around
the station in GUARD mode. Five-minute initial and repeat recharge (Fremen Palace
cadence). Respect unit limits, enabled unit types and deployment space. A partial
batch starts full cooldown; total failure keeps ability ready, AI retries every
five seconds. AI houses auto-deploy like Palace. Human commands check station
ownership. police_reinforcements logs actual counts, zero charge and cooldown.

Save format 9824: House bool after team ID; PoliceStation timer after base fields.
Both reads are version-gated; older saves default auto repair off and fresh police
cooldown. Existing command IDs are unchanged; two new commands appended before
CMD_MAX. Tests updated for appended IDs and save version.

City placement now accounts for whole footprints, polluting factories as well as
I zones, and other construction yards' queued sites. Candidate tiers outrank old
clustering scores: outside pollution radius (>5 footprint tiles) and within local
supply reach is preferred; nearby crowded sites are fallback, disconnected sites
last. Supply uses conservative origin distance <=16, with missing-role allowances
for bootstrap. R requires jobs; C requires available R/I; I requires R. Existing
road continuity/frontage checks remain. Local Micropolis source traffic.cpp and
micropolis.h use MAX_TRAFFIC_DISTANCE=30 road steps; DuneCity TrafficSimulation
uses 20 road steps and city growth kSupplyRadius=16 with coarse grid aggregation.
No simulation distances changed, and origin reach is not proof of a road route.
R/C scoring averages pollution/land value over the footprint and favors adjacent
open sand/dunes. Severe pollution outweighs sand/value. Clean industrial buildings
like windtraps do not get a pollution separation requirement. Placement details
in construction_selection.site.placement_quality include tier, score, supply flag,
nearest role origins, pollution buffer, mean value/pollution and adjacent sand.

Build 1.0.543 passes 395 C++ cases, same two nan baseline failures, three skipped.
Build/test logs build/repair-police-{build,tests}.log. Earlier Python importer tests
pass (7). Source and app plist checked. No user-game restart or GUI playtest; no
commit/push. New UI, saves and deployment behavior need the user's next launch.

## Live heavy-factory cap diagnosis (after 1.0.543 work)

User asks to explain cap before tweaking. Running match remains 1.0.539 session
1788680806413568-0. At cycle 239400 Harkonnen:149408 credits, eight actual HF,
zero queued, six busy, military9630/80000, no ground unit limit, power7200/5180.
Builders say heavy_target8, heavy_reason target-met. Last five game-minutes had
four lost HFs and four accepted replacement orders; two newly completed HFs were
lost almost immediately. Other survivor Rebels has530104credits, eight HF and
military81310/80000, blocked by military-limit instead.
Current shipped running formula: min(8, max(1+taxPerSecond/50,
1+max(0,credits-2000)/5000)). Updated source uses /2500 but still caps at8.
Neither adapts the cap to threat/losses. No further factory-cap change made yet;
user requested explanation and discussion of tuning.


## Rocket defense and land value: 1.0.542

User replaced rocket-turret crime suppression with twice-strength park amenity
and critical-asset defense, then R/C intersections. Read local MicropolisCore:
`../simcity/MicropolisCore/MicropolisEngine/src/tool.cpp` putDownPark picks either
WOODS2..5 or FOUNTAIN. `scan.cpp` pollutionTerrainLandValueScan adds 15 for terrain
IDs below RUBBLE, smooths terrain memory, then adds it to land value. FOUNTAIN=840
is not below RUBBLE=44: the core has no distinct positive fountain coefficient.
Use the agreed park/terrain reference 15 -> rocket strength 30 in DuneCity's
existing park stamp/falloff (radius 3). This is an adaptation, not a literal
port of fountain behavior. Existing block aggregation, land-value caps and tax
formula are unchanged. Rocket police coverage is now zero; gun turret remains
25. Higher value still has normal indirect city effects; rockets do not apply
a direct crime-reduction stamp. Sidebar says Land value +30.

Replaced crime-hotspot search and crime-triggered construction with weighted
uncovered defense and useful R/C amenity siting. Nuclear weight 2, HeavyFactory
and RepairYard weight 1. Coverage uses weapon range minus one tile from asset
center. Existing/queued turrets suppress duplicate coverage; relocation excludes
its own pending turret. Queued target buildings also count. Defense scores rank
before junction preference, R/C benefit and closeness. R/C-only sites require
cross/T/corner junction bonus and an uncovered zone below max land value within
park range. Once coverage is established, city zoning can continue instead of
building turrets endlessly. Rocket city siting has no generic crime/perimeter
fallback; gun turret placement keeps its ordinary defense search.

Existing road connection/render/traffic code retained; continuity and neighboring
zone-access checks remain. `RocketTurretPolicy.h` holds testable priorities and
bounded estimated benefit. `turret_site_evaluation` logs reason, position, weighted
uncovered defense, estimated R/C value benefit, reactor weight and state.
Policy rocket-amenity-v7 (schema 1, telemetry 3). Ctest: 392 passed, two existing
nan failures, three skipped; seven Python tests pass. Build/plist version 1.0.542,
logs build/rocket-amenity-{build,tests}.log. Ready for next launch; no in-game
placement/tax outcome claim yet. No restart, commit or push.


## Funded idle construction yards: 1.0.541

Confirmed in live session 1788680806413568-0 (running 1.0.539, seed 1424269878).
Harkonnen builder 78 idle with 26,298 credits (seq 6777), power 4,200/1,803,
maximum R/C/I valves, 15/5/9 zones; heavy target five already met. Zone decisions
explicitly reject all candidates as spice_economy_priority. The old hedge gate
requires spiceShare <30,000 or zones <max(6,harvesters), irrespective of cash.
Preserved 9,761 records through cycle 71,646 in build/city-growth-before.jsonl;
summary build/city-growth-before-summary.json. SQLite build/current-growth.sqlite
audit: zero issues. Last five game-minutes: 16/20 CY status samples idle (sampled
observations, not exact idle duration). Full capture: 129 candidate vetoes.

Removed hedge veto; ongoing city growth follows demand even on spice-rich maps.
User clarified that needed Dune buildings should retain priority, then idle yards
should zone whenever demand and a valid site exist. No new priority timer or
city-before-factories override. Existing affordability and power headroom guards
remain. Spice/refinery/harvester investment continues independently.

Factory cash step reduced from 5,000 to 2,500 above 2,000 working capital;
23,000 credits now targets eight factories, previously five. Income target,
actual-plus-queued counts, military/unit caps and classic AI ratios preserved.
Telemetry policy city-growth-v6 records independent zoning policy and zone result.
See AI-DECISION-TELEMETRY.md. 390 C++ tests pass, same two nan baseline failures,
three skipped; seven importer tests pass. build/city-growth-{build,tests}.log.
Version source and built plist agree on 1.0.541. Running game was not restarted;
behavioral playtest remains for next launch. No commit/push.


## Windtrap output and clean industry: 1.0.540

WindTrapInterface now shows the selected windtrap's actual health-scaled output,
using the same getter that updates house power, alongside existing house totals.
CityStatsBox replaces Coal Power with I-medium and shows Emissions: 0 separately
from Local pollution (ambient pollution from surrounding industry). The extra
emissions row is attached only for windtraps, preserving other panels' layout.

Windtraps now have Industrial city role and maximum occupancy level 2, providing
industrial supply/jobs through existing census, demand and growth code. Explicit
pollution exemption keeps windtraps clean at all levels despite the new role.
Power output remains independent of city occupancy. Existing windtraps acquire
the role on the next city scan after loading with this build.

Rebuilt build/bin/dunecity.app version 1.0.540; metadata and plist agree. Ctest:
389 passed, 2 known parseDouble nan failures, 3 skipped. Updated city-effects
regressions cover medium-tier supply/jobs and zero emissions. Build/test logs:
build/windtrap-{build,tests}.log. No game restart, commit or push; sidebar visual
confirmation remains for the user's next test.


## Nuclear chain reactions: 1.0.539

User requested reactor death explosions with twice palace-missile destruction
area, reactor HP equal to a Starport, and palace AI targeting reactors.
New `NuclearBlastPolicy` uses a circular 42-tile equivalent area (2x the existing
missile's 21 impact tiles), radius ~3.66 tiles /117 pixels. Radial tests are
integer-only. Structures intersecting the circle and ground units inside receive
900 damage once (the centered missile's nine 100-damage impacts); terrain/roads
and visible blasts use the disk's tile centers. Map edges are clipped. Air units
retain the normal ground-nuclear immunity. Adjacent plants die and detonate on
their own update, not recursively inside damage iteration.

NuclearPlant::destroy removes remaining power, records trigger/credit owner,
applies blast, then normal structure teardown. Destructor itself never explodes
on quit/load. Chain-reaction credit follows the initiating attacker when known;
direct destruction falls back to reactor owner. Pending credit is runtime-only.
ObjectBase ignores non-healing hits on already-dead objects to prevent duplicate
kill awards from a palace missile's multiple impacts.

Default/Tornie reactor HP now 500, same as Starport. INI loading copies each
house's Starport HP into reactor HP, including overrides. Existing saves retain
their saved object-data balance table; use a fresh match for the new HP table.
Centered palace strike already delivers up to 900; missile scatter is unchanged.
Shared Player targeting selects visible live enemy reactors, prefers clusters,
aims at their center, then falls back to existing target logic. Used by QuantBot,
AIPlayer, CampaignAIPlayer and Mentat, without overriding manual player aim.

New telemetry: palace_target (cluster score), palace_missile_launched (aim tiles,
scattered destination pixels), nuclear_detonation (center pixels, squared radius,
damage, trigger and credit house). Policy nuclear-chain-v5, telemetry remains 3.
Built app 1.0.539; 388 C++ cases pass, same two baseline nan failures, three
skipped. Logs build/nuclear-blast-{build,tests}.log. Full in-game chain/visual
verification remains for user's fresh match. No commit, push or launch.

## Power reserve follow-up: 1.0.538

User requested more surplus power, especially for large cities. City AI target
is now ceil(25% of current demand), increased to one owned generator's nominal
output where useful; this allowance is capped at 50% of demand for small bases.
Examples: demand 6,000 with a 1,000-output plant -> 1,500 surplus; demand 14,000
-> 3,500 surplus (formerly 1,400). Existing cross-yard pending-generator guard
remains, so expansion is reassessed after each generator completes. Zero demand
adds no reserve. No change to actual power consumption/output or classic AI.

Telemetry policy power-reserve-v4 (schema 1/telemetry 3) logs
city_power_reserve_target and largest_generator_nominal in decision state.
Rebuilt app 1.0.538. Validation recorded in build/power-reserve-tests.log.

## Codex follow-up: completed 199-minute match, 1.0.537

User finished the game and requested full analysis, fixes and better capture.
Read `AI-COMPLETED-MATCH-ANALYSIS.md`. Completed demand-first-v1 session has
104,867 consecutive valid records; no corrupt tails. Duration 198.98 minutes,
not the old zero-cycle session_end. Evidence/index/report retained under build/.

Confirmed concurrent overlapping yard plans (Atreides HF and C zone at 98,19
in cycle 99); total 177 HF orders, 56 completions, 107 placement cancellations.
No residential selections over stronger normalized jobs demand (8,688 evals).
Much late support construction replaced losses; Fremen silo lifetime ~7.4 sec
by location matching. Old capture lacks official result and lethal causes.

New source/runtime policy reserved-sites-v3: shared footprint reservations,
60-second avoidance of recent economic/production building losses, funded
factory recovery ahead of city seeding, pending storage/crime-defense guards.
Retains prior spice/road/concrete/civic/power/UI fixes. Runtime-only planner
state does not alter save layout. Records actual placement success.

Telemetry v3: final roster/result/cycle; fractional cumulative economy ledger;
producer progress/gates, harvesters, unit mix; producer/object completions,
object destruction and lethal attacker; attack new vs existing membership.
Engine lifecycle events use player -1 and supplement (do not add to) old
callbacks. Disabled TechCenter text spam suppressed. SQLite match-report,
economy_samples view, and conflicting-reimport rejection added.

Built source version 1.0.537 with script; all three metadata files agree.
385 C++ cases pass, same two baseline nan failures, three skipped; seven Python
tests pass. Logs: build/completed-match-build.log and completed-match-tests.log.
No full match run on v3 yet; user will test on return. No commit/push/launch.

## Codex follow-up: live audit, placement, spice economy and queue guards

Read `AI-LIVE-ANALYSIS.md` for the running 4-corners match (seed 1034718315,
session 1788669627998013-0, cutoff ~38:33). 23,375 events audit cleanly. Confirmed
11 factory and 248 turret placement cancellations, duplicate stadium/nuclear
orders across yards, and no residential choices over stronger normalized jobs
demand. Source/build now uses policy spice-road-v2; running match is still v1.

New changes: retain/replan finished buildings without full-concrete gating;
road-continuity-aware placement, rocket traffic junctions, restoration of road
surfaces after damage; spice-based harvesters/refineries with city hedge and
combat/cash constraints; queued civic/power guards; nuclear plant power panel.
Telemetry v2 adds detailed placement observations, all-producer statuses, crime
defense reasons, queued civic/power inputs, road scores, spice fleet targets and
credit provenance. SQLite tool adds audit/report. See telemetry doc for semantics.

Built `build/bin/dunecity.app`, metadata consistently 1.0.536. Validation:
`build/ai-placement-tests.log`: 382 passed, same two baseline parseDouble("nan")
failures, three skipped. Five Python importer/audit tests pass. New UI and policy
still need observation after user restarts; do not interrupt the running match.
No commit/push. Existing queues/buildings are not rewritten on save load.

## Codex follow-up: demand-first zoning and structured AI telemetry, 2026-09-06

Preserved current game in `build/zoning-before.log`; imported 2,328 logged zone
selections into `build/ai-decisions.sqlite`. Of 1,576 residential selections,
1,240 occurred with stronger normalized C/I demand. Root cause: `rankZones`
used demand only as a positive gate and ranked raw gaps from a fixed 3R:1I:1C
ratio. It now ranks normalized demand first (R*3, C/I*4), breaking ties by
weighted counts; bootstrap still seeds missing types. Campaign's duplicated
zoning branch now calls the same chooser.

Added per-session JSONL telemetry, SQLite importer/reports and tests. Read
`AI-DECISION-TELEMETRY.md` for event schema, paths, SQL, coverage and limits.
Captures are local under application support `ai-decisions/<session>/events.jsonl`.
No external DB service, save-format/RNG changes, commit or push. Snapshot/decision
inputs, candidate reasons, queue acceptance, placement requests, actual built/loss
callbacks, and main attack gates are separate records. Capture is bounded at
128 MiB per session; completed sessions are retained without automatic deletion.
Set DUNECITY_AI_TELEMETRY=0 to disable. Other AI classes and tactical/pathfinding
choices are not instrumented by this change.

Local build is now source version 1.0.536 (version files advanced elsewhere during
this work; this task did not bump them). `build/ai-telemetry-tests.log`: 375 passed,
the same two pre-existing parseDouble("nan") failures, three skipped. Three Python
importer tests pass; C++-written fixture imports as valid JSONL into SQLite with
zero invalid records. The existing open game has not been restarted; save/reload
in rebuilt `build/bin/dunecity.app` is required for live verification and capture.

## Codex follow-up: repair/factory balance, 2026-09-06

Current-game evidence saved in `build/ai-balance-before.log`: Neutral ordered
its fourth repair yard with two busy heavy factories and later held ~18k credits;
Mercenary ordered its sixth repair yard with two factories. Some heavy factories
were being built, but the city cash target added only one per 10k credits while
repair yards grew unconditionally with army value (one per 6k).

`QuantBotBuildPolicy` now targets an extra city heavy factory per 5k credits above
2k working capital, still taking the larger income target and capping at eight.
Extra repair yards require all existing yards busy, count queued yards as spare
capacity, and cap at ceil(completed heavy factories / 2), minimum one, maximum four.
The first-yard tech rule remains. City factory expansion uses actual build-list
availability without the additional policy-only repair-yard/IX prerequisite;
classic-mode factory prerequisites remain. Power recovery and economy seeding
still precede expansion, and military/unit limits still stop factory expansion.

Added 30-game-second per-CY `BUILD-BALANCE` logs (HF/RY completed/queued/busy,
factory target/reason, repair cap, estimated tax income, power) and `BUILD-CHOICE`
logs alongside the existing no-site/rejected-order diagnostics.

Local app rebuilt at `build/bin/dunecity.app`, source version 1.0.535 checked
consistent; no version bump, commit, or push. `build/ai-balance-tests.log` reports
372 passed, the same two pre-existing parseDouble("nan") failures, three skipped.
The running game must be saved, quit, and reloaded in this rebuilt app before
these changes and new logs take effect. Live post-change validation is pending.

## Codex follow-up: QuantBot production and civic graphics, 2026-09-06

User reported Brutal QuantBot's heavy factory idle, one construction yard
repeatedly building residential lots, the other idle, and building graphics
appearing in other places. Preserved live evidence in `build/quantbot-before.log`.
Bots held roughly 50–60k credits with military values far below their configured
80k limit. Logs repeatedly selected Heavy Factory, then deduplicated Palace,
then entered `PROACTIVE: Building Residential Zone`. The running game's user
override enables `Only One Palace`; the screenshot also showed `ALREADY BUILT`.

Verified production causes and fixes:

- QuantBot's city palace target ignored `onlyOnePalace` and mixed internal and
  displayed population. BuilderBase rejected the extra palace, while QuantBot
  counted the rejected order and reserved the entire production loop for it.
  The planner now respects the option and the documented 30k displayed-population
  scaling; counts update only on accepted normal construction orders. Subsequent
  yards re-evaluate palace/IX counts including queued orders.
- Strategic saving now reserves the item's price and allows factories to spend
  the remaining cash. An unplaceable strategic structure does not reserve funds.
- Removed unconditional residential fallback. City bootstrap, ongoing zoning,
  and idle-yard fallback rank R/I/C demand and count balance, trying another
  demanded type if the first cannot be placed. The fallback respects power.
- Heavy factory expansion considers both income and surplus cash, bounded at
  eight factories, and stops expansion when the military-value or ground-unit
  limit is reached. Corrected siege-tank budget accounting.
- Concrete placement plans are now per construction yard. The old shared FIFO
  could send one yard to the other's planned location. The old serialized list
  remains for save-layout compatibility; runtime per-yard plans rebuild after
  load. Placement cache clears after placing a structure.
- Construction-yard status logging is throttled per bot rather than using
  shared static state across all yards/houses. HF/CY diagnostics identify the
  builder, queue, hold state, budget and relevant limits/reserves.

Graphics cause: a zone's civic overlay selected a 1x1 hospital/church texture,
but StructureBase's per-draw refresh replaced it with the 4x4 residential atlas
using the unchanged graphicID. The full atlas then rendered over neighbouring
lots. ZoneStructure now updates graphicID with the civic image and restores the
zone ID and atlas dimensions together when the overlay clears or density is zero.

Build: `build/bin/dunecity.app` rebuilt successfully; source version remains
1.0.534, uncommitted. `build/quantbot-fix-tests.log`: 370 passed, the same two
pre-existing parseDouble("nan") failures, three skipped. New tests cover the
production policies and the civic texture-refresh contract. Live confirmation
after saving/restarting/reloading the user's current game is still pending.

## Codex follow-up: credits root cause confirmed, 2026-09-06

The live diagnostic fired with `dst=2515,135`, `output=2560x1600`,
`logical=0x0`, `target=screenTexture`, and `copy=0`. The active texture is
960x600. This supersedes the stale-texture hypothesis in §3.1: texture creation
already follows the final logical-size adjustment.

On this Mac's sdl2-compat backend, binding the texture clears the logical size,
but `SDL_GetRendererOutputSize` still returns the window's native pixel size.
`getRendererSize()` consequently positioned dynamically right-aligned elements
off the target, while the sidebar retained its correct construction-time position.
The fix in `include/misc/DrawingRectHelper.h` queries the active texture when
there is no logical size, falling back to output size only for the backbuffer.

`tests/RendererSizeTestCase.cpp` covers target switching, credits positioning,
and backbuffer restoration. It uses software rendering by default; run through
`DUNECITY_RENDERER_TEST_GPU=1 ctest --test-dir build --output-on-failure` to
exercise the native HiDPI backend. Before the fix the native test reproduced
`getRendererWidth() == 2560` where 960 was expected. The rebuilt app is at
`build/bin/dunecity.app`. Both software and native-backend suite runs now report
363 passed, the same 2 pre-existing `parseDouble("nan")` failures, and 3 skipped.
Logs are `build/credits-tests-before.log`, `build/credits-tests-after-native.log`,
and `build/credits-tests-after-software.log`. The temporary constructor/blit
diagnostics were removed; the signed digit arithmetic is retained. The currently
open game is still the previous executable and needs a restart for visual
confirmation. No commit or release/version change has been made.

The original handover below is retained as historical context.

Written by Claude Code for the next agent (Codex). Everything below is **uncommitted**
in the working tree on `main` at `24b57ff`, version `1.0.534` (all three version files agree).

23 files changed, ~554 insertions. Nothing has been committed, tagged or pushed.

---

## 1. Build environment on this Mac (this was not documented before)

Host is Stefan's MacBook Air (`Stefans-MacBook-Air.local`, Apple M5, 10 cores, macOS 26.5.2).
The repo docs describe vcpkg (CI) and a Windows laptop; neither applies here. **Homebrew, no
vcpkg, no Xcode** — Command Line Tools clang 21 is enough.

```bash
brew install cmake ninja sdl2_mixer sdl2_ttf miniupnpc catch2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew -DDUNECITY_BUILD_TESTS=ON
cmake --build build --parallel 10        # ~4 min cold -> build/bin/dunecity.app
cmake --build build --target dmg         # -> build/DuneCity-1.0.534-macOS.dmg
ctest --test-dir build --output-on-failure
```

Gotchas:

- `-DCMAKE_PREFIX_PATH=/opt/homebrew` is **required**, otherwise `find_library(MINIUPNPC_LIBRARY …)`
  in `src/CMakeLists.txt` fails and configure dies on a NOTFOUND link item.
- Homebrew `sdl2` is an alias of `sdl2-compat` (SDL2 API over SDL3). It works.
- `discord-rpc` is not in Homebrew and is optional in CMake, so Discord presence is compiled out.
- libcurl comes from the macOS SDK.
- Use `build/`. The tracked `build2/`, `build_phase4/`, `build.bad/`, `buildtests/` are stale
  CMake trees from claw.local (`CMAKE_HOME_DIRECTORY=/Users/stefanclaw/development/dunecity`).
  `CLAUDE.md`'s `~/development/dunecity` paths are claw.local's, not this machine's.
- This build links `/opt/homebrew` dylibs, so the app and its DMG are **not portable**.
  The distributable DMG still comes from CI's vcpkg static build.
- The bundle `Info.plist` shows version `0.01` because `IDE/xCode/Info.plist` uses Xcode
  `$(…)` variables CMake does not substitute. Real version: `build/include/config.h` and the
  log line `Starting DuneCity <version>`.

**Always run the test suite through `ctest`, never the binary directly.** `tests/CMakeLists.txt`
passes `DUNE_CITY_SOURCE_DIR` and `DUNECITY_DATADIR` via `set_tests_properties(... ENVIRONMENT ...)`;
running `./build/bin/dunelegacy_tests` by hand silently skips ~50 source-reading tests and
produces bogus failures.

### Test status

`ctest` → **362 passed / 2 failed / 3 skipped** (367 cases, 2422 assertions).

Both failures are **pre-existing, not caused by this session**:

- `tests/GenericNinthHouseRegressionTestCase.cpp:71` and `:130` — `parseDouble` accepts the
  string `"nan"` on macOS (`ModMentatConfig::parseDouble` / `CustomHouseConfig::parseDouble`
  in `include/mod/`). `std::stod`/`strtod` parse `nan` on this libc; the test expects rejection.
  Fix by rejecting non-finite results (`std::isfinite`) in both headers.

Separately, `tests/Dune2RAssetManagerTestCase.cpp` **segfaults non-deterministically** (line 32,
52 or 72 depending on the run) — 6 of 6 isolated runs crashed. It passes under `ctest` when the
whole suite runs, so it is order- or environment-dependent. This is a real bug and was **not**
investigated. An lldb backtrace needs a one-time macOS debugger authorisation that could not be
granted headlessly.

---

## 2. What was changed (all uncommitted)

### 2.1 Windowed resolution changes were ignored — FIXED, verified

`setVideoMode` in `src/main.cpp` snapped the requested window size to
`SDL_GetClosestDisplayMode` before creating the window. That is exclusive-fullscreen logic and
the game only ever uses `SDL_WINDOW_FULLSCREEN_DESKTOP`. On a Retina Mac SDL only offers
low-density modes as candidates, so 1280x800 → 1920x1200, 1440x900 → 1920x1200,
1024x768 → 2048x1326. The window was also created larger than the desktop (1710x1107 points).

Now: the requested size is used as-is, clamped to `SDL_GetDisplayUsableBounds` in windowed mode,
floor `SCREEN_MIN_WIDTH/HEIGHT`. The logical size derives from what is actually presented (the
desktop in fullscreen, the window otherwise), so the saved windowed size survives a fullscreen
round trip. New log line: `Window: <req> requested, <got> created (<mode>), <w>x<h> pixels`.

`OptionsMenu::determineAvailableScreenResolutions` now drops modes larger than
`SDL_GetDisplayBounds` (the native 2880x1864 panel mode could never be used).

### 2.2 Black bars and mushy text in windowed mode — FIXED, verified

Two causes. The `DISPLAY` screen's "SCREEN SHAPE" forced the logical width to 4:3 or 16:9 while
the height was pinned by the interface preset, so a 4:3 screen sat inside a 16:10 window. And
`SDL_HINT_VIDEO_HIGHDPI_DISABLED` was set to `"1"` (present since the initial commit), so a
1440x900 window had a 1440x900 pixel surface and 600 logical rows were scaled 1.5x.

Now: `SDL_WINDOW_ALLOW_HIGHDPI` is set on desktop, the HiDPI-disable hint is gone, and the
logical width follows the window's shape via a new `interfaceWidthForShape()` in `src/main.cpp`.
Only the height stays a preset. The SCREEN SHAPE row is hidden on desktop in
`src/Menu/DisplayMenu.cpp` (Android keeps the old behaviour) and that menu's layout was compacted.

Mouse mapping under sdl2-compat was verified with a standalone probe before enabling HiDPI:
events arrive in logical coordinates correctly.

**Caveat discovered later and NOT addressed** — see §3.1: `Game::renderFrame()` renders
everything into `screenTexture` at the logical size and then upscales, so the HiDPI surface does
not actually buy sharpness yet.

### 2.3 "Multiple players per house" forgotten — FIXED

New `settings.general.multiplePlayersPerHouse`, persisted as
`[General] Multiple Players Per House` in `Dune City.ini`. The checkbox in
`src/Menu/CustomGameMenu.cpp` seeds from it and writes on toggle. Also written by
`OptionsMenu::saveConfiguration2File` and included in the generated default config in `main.cpp`.

### 2.4 Second player slot per house missing — FIXED (was a regression)

Came in with commit `5a172ce` "Import Tornie 1.0.520 source snapshot" (2026-07-15), not with any
DuneCity fix. `CustomGamePlayers`'s constructor force-disabled
`setMultiplePlayersPerHouse(false)` for every custom game, calling the second slot "unusable",
and `onNext()` rejected two players in one house as `bTwoPlayersInSameHouse`. Both removed. The
newer duplicate-house and duplicate-colour checks were kept. The slot code itself is byte-identical
to v1.0.359.

**Not verified in play.** Stefan has not yet started a co-op game with two players in one house.

### 2.5 Credits SFX on by default — FIXED

Defaulted to off in all three places: the `getBoolValue` fallback in `main.cpp`, the generated
default config (which previously omitted the key entirely and so inherited `true`), and the
shipped template `config/Dune City.ini`. Existing configs keep the player's own value.

### 2.6 Zones placed on top of each other — PARTIALLY FIXED, needs play testing

`House::placeStructure`'s pre-placement check only tested `hasAGroundObject()` and never
`hasCityZone()`. It now refuses both and logs
`placeStructure: refused zone item <id> for house <h> at (x,y): tile (x,y) already belongs to a zone`.

**This log line has already fired once** in Stefan's session (`item 20 for house 0 at (24,13)`),
which proves the guard works and that something is still *requesting* overlapping placements.
Whether visible overlap remains is unconfirmed.

`Game::load` now re-attaches every zone structure to its 2x2 footprint after `objectManager.load`,
because zones from older saves could come back without owning their tiles. It logs
`Loaded game: re-attached N zone tiles, M zone tiles overlap another object`.

### 2.7 Zone road frontage and sand placement — DONE, needs play testing

In `src/players/QuantBot.cpp`, city-mode zone placement (`findPlaceLocation`, guarded by
`cityZonePlacement`):

- The per-adjacent-tile `locationScore += 10` compact-base bonus is suppressed for zones. That
  bonus is what produced solid packed blocks.
- New `alignedWithNeighbouringZone()` gives +50 for continuing an existing row or column, either
  touching or exactly one road tile apart.
- New `wouldLandlockNeighbouringZone()` rejects a lot that would take a neighbour's last open side.
- Small bonus per sand/dunes tile under the lot so rock stays free for Dune structures.

Sand rule, implemented in four places that all had their own copy of the terrain test:

- `DuneCity::isCityZoneTerrain()` (new, `include/dunecity/CityConstants.h`) — rock, slab, sand or dunes.
- `Map::okayToPlaceStructure(..., itemID)` — zones use the new predicate plus an `anchoredTiles > 0`
  requirement; other city-only structures keep the strict rock/slab rule.
- `ZoneStructure::canBePlacedAt` — same.
- `Game.cpp`'s placement preview — same, with `zoneFootprintAnchored` computed once per footprint.

`tests/ZoneStructureTestCase.cpp` had a source-text test asserting the old strict rule; it was
updated to assert the new one.

### 2.8 Game options only saved from the Options screen — FIXED

There were **three** layers, not two: `[Game Options]` in the main config, then the active mod's
`GameOptions.ini` overlaid on top. The Dune City mod's file lists every key, so it always won —
which is why changing a default under Options never stuck either.

New helpers in `src/globals.cpp` / `include/globals.h`: `userGameOptionsSection()`,
`writeGameOptionsToConfig()`, `applyGameOptionsFromConfig()`, `saveGameOptionsAsDefaults()`.
The player's choices are stored per mod in the main config as `[Game Options <modname>]` and
layered over the mod's defaults in `ModManager::loadEffectiveGameOptions`.

**The mod's own `GameOptions.ini` is deliberately left untouched** — `ModManager::updateChecksums`
hashes it for the multiplayer config-sync check, so writing into it would make two players with
different preferences fail to sync.

Every Game Options window now calls the helper on close: Options ("Change…"), the Custom Game
lobby, Skirmish, and the campaign house choice. `Restore Config Defaults` removes the override
section and its message was updated to say so. The `City Effects` key, which the Options screen
never persisted, is included.

### 2.9 Production stall diagnostics — ADDED

`BuilderBase::updateProductionProgress` had a silent branch: if on hold, at the unit limit, or at
zero credits, nothing happened and nothing was reported. It now logs every 10 s with the reason
and a credit breakdown, and posts a ticker message every 30 s for the unit limit and for no money.

This was added for Stefan's "heavy factories aren't building" report, which is **not diagnosed**.
Note `House::getCredits()` sums three pools (`cityCredits + storedCredits + startingCredits`) and
QuantBot in the same house shares the human's wallet.

### 2.10 macOS test link fix

`tests/CMakeLists.txt` did not compile `IDE/xCode/MacFunctions.m` on APPLE, so `dunelegacy_tests`
failed to link with `_getMacApplicationSupportFolder` undefined (from `fnkdat.cpp`). Added the
game target's `if(APPLE)` block plus the Cocoa/Foundation/CoreFoundation frameworks.

---

## 3. Open items for Codex

### 3.1 Credits counter shows nothing — THE MAIN OPEN BUG

Reported repeatedly: the credits box below the radar renders empty in-game.

**What has been ruled out**, from a diagnostic already in the running build
(`Interface: credits digits texture 80x8, sidebar 144x600 at x=816, credits 20000`):

- The digits texture loads fine: 80x8, i.e. 10 glyphs of 8x8 from `SHAPES.SHP` frames 2..11.
- The credits value is correct (20000 at construction).
- The geometry is correct. Logical screen 960x600, sidebar 144 wide at x=816. `PictureFactory`
  blits `creditsBorder` (63x13) at sidebar-relative (46,132) → global 862..925 x 132..145.
  `GameInterface` draws digits at x = 816+49+(6-n+i)*10, y=135, 8x8 → 875..923 x 135..143.
  That is inside the box.
- Palette is not the cause. The glyphs use only indices 31, 81 and 90; `Custom_IBM.PAL` in
  `Tornie.PAK` leaves all three identical to `IBM.PAL`, and `applyCustomPaletteRuntimeHouseRamps`
  only touches 52..59.
- Draw order is fine: credits are drawn after `Window::draw`, and the radar occupies y=0..132.
- `drawCityStatsOverlay()` is the only thing drawn afterwards in the same function and
  `showCityStatsOverlay` defaults to false.
- No `SDL_RenderSetClipRect` exists anywhere in the in-game render path.

**One real bug was found and fixed while looking**: `NumDigits` is `std::string::size_type`
(unsigned), so `6 - NumDigits` wrapped around for credits with more than 6 digits and threw every
glyph far off-screen. Now cast to `int`. This is **not** the reported symptom (5-digit credits
were affected too), but it would have bitten at 1,000,000 credits.

**A one-shot draw-time diagnostic is in the build at `build/bin/dunecity.app` (12:53).** On the
next in-game frame it logs one line to `~/Library/Application Support/Dune City/Dune City.log`:

```
Credits blit: value=… digits=… src=… dst=… copy=… output=…x… logical=…x… clip=… blend=… alpha=… rgbMod=… target=…
```

Start a game and grep for `Credits blit:`. That line settles it: whether `SDL_RenderCopy`
returns non-zero, whether a clip rect is active, whether the texture is fully transparent
(`alpha=0`) or colour-modulated to nothing, and which render target is bound.

**Strongest remaining hypothesis**, worth checking first: `Game::renderFrame()` sets the render
target to `screenTexture` (created at `settings.video.width x settings.video.height`), draws
everything, then copies it to the backbuffer. If `screenTexture` is stale or smaller than the
current logical size after a resolution change, the sidebar's right-hand columns would fall
outside it. `setVideoMode` recreates it, but check that every path that changes
`settings.video.width/height` also recreates `screenTexture` — the interface-preset override in
`setVideoMode` runs *after* the window is created and could leave the two out of step.

### 3.2 Budget window looks blurry

Not reproduced. `CityBudgetWindow` uses the same `Label`/`Window` pipeline as the sidebar text
that renders crisply, and the screenshot supplied was scaled ~1.3x, which would explain it.

If it is real, the likely cause is §3.1's `screenTexture`: the whole frame is composited at
960x600 and then upscaled by ~2.67x to 2560x1600 with nearest-neighbour
(`SDL_HINT_RENDER_SCALE_QUALITY` is `"0"`). Non-integer nearest scaling gives uneven pixel
doubling that reads as mushy. Rendering the UI at native density, or choosing an integer scale,
would fix it — but that is a real change to the render architecture, not a one-liner.

### 3.3 Heavy factories not building

Not diagnosed. The diagnostics from §2.9 are in the build; play a game and grep the log for
`Production stalled:`. Check the unit limit first: `House::isGroundUnitLimitReached()` counts
`numGroundUnit + (numItem[Unit_Soldier]+2)/3 + (numItem[Unit_Trooper]+2)/3 >= maxUnits`, and
Stefan's Game Options screenshot shows "Override max. number of units" **ticked with a value of 0**.
`INIMapLoader` treats an override `>= 0` as authoritative, and `maxUnits == 0` is documented as
"unlimited" in `House.h` — verify that 0 really means unlimited on every path rather than
"no units allowed", because the override is applied before that comment's assumption.

### 3.4 Verification still owed

Nothing in §2.4, §2.6 or §2.7 has been confirmed in actual play. The city-placement changes in
particular are scoring heuristics and need a game watched for a few minutes.

---

## 4. Before committing

`CLAUDE.md` requires the version bump in the same commit as any release work, and CI verifies
that the tag matches. This session did **not** bump anything; the tree is still 1.0.534, which is
already tagged. Decide on 1.0.535 and run `scripts/bump-version.sh 1.0.535` before tagging.

Suggested split, since these are unrelated fixes:

1. Windowed resolution + HiDPI + DISPLAY menu (§2.1, §2.2)
2. Custom game lobby: remembered checkbox + restored second slot (§2.3, §2.4)
3. Credits SFX default off (§2.5)
4. City zone placement: overlap guard, road frontage, sand (§2.6, §2.7)
5. Game option defaults saved from any pre-game screen (§2.8)
6. Diagnostics: production stalls, credits blit, macOS test link (§2.9, §2.10)

The credits-blit block in `src/GameInterface.cpp` is a temporary probe — remove it once §3.1 is
solved, but keep the unsigned-arithmetic fix.
# Bundled user maps — 1.0.599

The user-authored single-player maps `4P - 192x192 - DuneCity.ini` and
`4P - 128x128 - 4 corners.ini` are now part of the default map set. Their
source is the local Dune City user-map directory on this Mac. The files are
kept byte-for-byte unchanged, including their CC-BY-SA metadata, and are
packaged under `Resources/maps/singleplayer` by the existing data copy step.


## 2026-09-13 — HTTPS polling candidate 1.0.658

Current work on `fix/network-hardening` adds browser/native HTTP polling over the
existing Apache/PHP server, preserving the relay/game protocol. No main/public
release or cron activation yet. See `docs/https-relay-deployment.md` for verified
constraints, tests and remaining release gates; `docs/room-relay-http-polling.md`
for the transport. Node analytics accepts a private key file and emits schema2
with observed `https-poll`; the website receiver changes are in the separate
`dunelegacy-relay-analytics` worktree. Never bundle the deployed private keys.

User testing found two failures: generated EM_JS escaped a regex incorrectly,
rejecting successful open responses; duplicate default names were refused but
presented as an ended game with controls disabled. Both are fixed, with compiled
JS and relay-session regression coverage. The lobby has a separate Join Game
button and Change name action. Local two-browser gameplay has started successfully;
this does not attest public Apache multiplayer. Test service 18790 and web8768
serve the local 1.0.658 candidate, separate from old8787/8766 clients.

Follow-up verification on the same candidate: metaserver Node22.23.2 passed all
200 relay tests and its PHP8.3 gateway contract checks. A bounded launch of the
exact candidate under Landlock + Node permissions reported analytics enabled,
answered authenticated health and refused unauthenticated health. It was stopped
and the prior candidate symlink restored; no public gateway or cron installed.
Hermes reviewed frozen source402caf4 (artifact SHA256 recorded in the review),
reported no new source-verified blocker, and passed200 tests. Its bounded run
interrupted one parallel review worker; this is limited review, not full security
certification. Existing production/watchdog gates remain. Browser match logs
advanced beyond31500 cycles with a tested movement order and no reported state
digest mismatch; host and guest continued exchanging performance reports.

## 2026-09-18 — DuneCity skin synchronization eligibility

The automated DuneCity skin synchronizer now counts only Compact slots that its
selected engine packager can consume. Zone packages require `building_idle/d*_v*`
atlas cells within the manifest's declared density/value bounds; special buildings
use numbered `frame_*` slots, falling back to `default` only when no numbered frame
exists. Legacy default-only zone masters (currently Rebels Industrial) are reported
and skipped instead of entering the transactional staging pass and aborting all
otherwise valid packages. Regression coverage reproduces that legacy manifest shape.

The 2026-09-18 all-assets test deployment synchronized 20 packages (16 zones and
4 special buildings). All 256 populated zone cells are 128x128 RGBA sources with
`PixelsPerTile=64` while retaining their 2x2 logical engine footprint. Windows and
Android builds completed from that package tree; the Android APK assembled as
version 0.2.26 with DuneCity payload 1.0.708. ADB had no authorized device at the
end of the run, so installation was skipped without invalidating either build.

## 2026-09-23 — Metaserver map library (local 1.0.765)

Adds a Metaserver Maps tab to the custom-game chooser, combined mod/size/player
filters for local and remote maps, preview/download and version/mod details.
Workshop list retains its old wire response; catalogue=maps adds validated map
metadata and latest-revision/file deduplication. The private service maps folder
holds immutable INIs plus JSON metadata for later release curation. Map names
and installed filenames remain stable; revision numbers live in metadata.
Editor saves write BASIC.Mod and BASIC.MapVersion without repurposing format
BASIC.Version. Standard approved mods no longer bypass background map collection;
online hosted maps, including room sessions, use the durable publication outbox.
The map seeder inventories bundled/custom profile maps and reuses verified mod
snapshots. No saves or replay command streams are seeded.

Implementation/check evidence lives in ../outputs/metaserver-maps. Claude supplied
the bounded server catalogue patch; Codex reviewed it, completed sidecars and tests,
and implemented/reviewed client metadata, filters, uploads and seed preparation.

Follow-up: the user-facing Workshop hub is removed. Editors opens the map, mod
and asset editors directly; maps/mods have metaserver loaders, and editor saves
can upload the exact saved revision. The internal immutable content wire/cache
format stays compatible. Map editor loading confirms unsaved edits before changing
the active mod, then rebuilds the editor palette after input callbacks return.
Its unit palette scrolls to fit the metaserver controls at 640x480.

The website service source is deployed from dunelegacy.com commit 157d8f9;
map metadata cache schema 2 includes Rebels, custom houses and Player7-12.
Live verification found all 127 seeded distinct maps in a 139-map catalogue.
Alkozeltser 4 Cities matches DuneCity / 256x256 / 6 players. Its local Air copy
now has explicit Mod/MapVersion metadata, with the original backed up beside it.
Native menu probes pass 640x480, 854x480 and 1280x720; the content service has
25 passing integration tests including cached player-count migration.

Final native regression run: all 29 CTest targets passed (241.26 seconds), and
all three menu sizes passed the editor load/palette and scrolling checks.
Installed 1.0.765 on the MacBook Air at /Applications/dunecity.app; archive SHA256
02fe12f397ab04254bb2835fe444994a49bb83506af86fb75e0364301b18873b.
Signature/runtime checks passed on the Air; 512 profile INIs were preserved.
Previous app is /tmp/dunecity-install-765/dunecity-previous.app. Game code is
committed locally; no public game release or game-source push was performed.
Fresh-profile live smoke test also passed: real catalogue pagination and combined
filters, SHA-verified map plus full DuneCity dependency download, stable installed
filename, preview, exact mod activation and transition to player setup.

## 2026-09-23 — Map categories and editor conversion (local 1.0.766)

Map categories are Vanilla, Tornie and DuneCity. Classification uses placed
buildings rather than old tags, selected mod, or unit types. City structures
win on mixed maps; otherwise Tornie-specific buildings select Tornie. A name
containing city/cities with at most max(4, 2 * players) functional buildings
also selects DuneCity (walls/slabs excluded); other maps select Vanilla.
Exact gameplay dependencies remain separate and preserved in revision metadata.
Save writes the inferred category; Save As updates BASIC.Name. MapVersion is
revision metadata and BASIC.Version remains the file format.

Map Settings provides an explicit Convert button with an incompatible-content
preview and cancel-default confirmation. It changes the active editor palette,
removes incompatible placed objects, reinforcements and starport entries, and
supports undo/redo of both objects and mod. Terrain/spice/players are retained.
Empty converted maps remain Vanilla until appropriate buildings or the sparse
city-name exception apply. Settings scrolls at 640x480. Regression testing also
caught and fixed stale INI value bounds after repeated writes, which could
corrupt category strings and other changed values across successive saves.

Server schema 4 and seeder changes are committed in 9dba8818/d8ecc4ac. Website
service deployment 564e6d4 completed successfully (Actions 35806457305 and
associated checks). Live catalogue verification: 139 maps, 129 Vanilla and 10
DuneCity; all 127 seeded latest map hashes/category values verified. Existing
immutable map payloads remain preserved. Two sparse city starters have new
version 3 metadata; the mixed 4 corners map is DuneCity version 2 while retaining
its exact Tornie gameplay dependency. Names contain no added version suffix.
Evidence and seed receipts: ../outputs/map-types. Server suite: 220 tests passed;
content suite: 27 passed; seeder classifier: 4 passed.

Native verification: all 29 CTest targets pass in aggregate. The initial menu
run found a regression-fixture collision between resolution profiles; filenames
now include the resolution, and its focused CTest rerun passed all three sizes
(99.72 seconds). The other 28 targets passed in the full run (219.78 seconds).
Live native catalogue/download/player-setup smoke test passed. Dependency audits,
packaged runtime initialization/rendering, and archive transfer hash checks pass.
1.0.766 archive SHA256: 8f4d96c6dea1b76bfd84a3ea2859f3d9d315987e612c70d6ff78035b800b1dca.
The tested package/guarded installer are staged on the MacBook Air at
/tmp/dunecity-install-766. Installation is pending the running game being closed
(PID 95931 at last check); the existing app and profile are untouched.

Installation follow-up: 1.0.766 is now installed on the MacBook Air at
/Applications/dunecity.app. Archive hash and code signature verified; packaged
runtime initialization/rendering passed. All 512 tracked profile INIs were
preserved byte-for-byte. Previous app: /tmp/dunecity-install-766/dunecity-previous.app.
Receipt: ../outputs/map-types/install-766/receipt.json.

## 2026-09-23 — Shared credit storage limit (local 1.0.767)

House earned holdings (stored spice plus city credits) now share refinery/silo
capacity. Income and refunds discard overflow immediately. Unspent starting cash
remains exempt to allow opening construction; refunds never restore that exempt
pool. Capacity loss/transformation clamps immediately; loading defers enforcement
until structures are reconstructed and the game loop starts. No gameplay state
or save serialization layout was added. Gross tax and harvested-spice statistics
remain gross, while quota victory requires retained stored spice. Storage warnings
are throttled presentation state. The storage meter and all existing AI silo
thresholds use combined earned holdings. Protocol 23 prevents mixed-rule matches.

Infantry capture transfers the storage share before removing the old capacity,
preventing a double charge. It uses the correct captured structure capacity and
includes city holdings. Real silo/refinery capture probes pass in both modes.
Claude supplied the initial accounting patch and focused probes; Codex replaced
its gradual leakage and unpersisted refund exemption with immediate caps, reviewed
save/load behavior, and integrated UI/AI/protocol/capture changes.

Verification: 31 CTest targets pass in aggregate. The full original 29-target run
passed 25; four economy fixtures had injected over-capacity spice as scenario
funding. They now explicitly supply starting funds and all four focused reruns
pass. Two new storage probes pass in Vanilla and DuneCity, covering fractional
amounts, deposits in either order, refunds, actual silo destruction and captures,
capacity growth, loading reconstruction, in-memory House save/load, statistics,
maximum credits and retained-only quotas. Dependency audits and packaged runtime
initialization/rendering pass. Evidence: ../outputs/credit-storage.
The 1.0.767 package is ready under ../outputs/credit-storage/install-767; it is
not installed on the MBA yet. The last installed version remains 1.0.766.

Absolute-ceiling follow-up: total credits, including starting cash, may never
exceed 999999. New starting funds are clamped, and loaded credit pools are bounded
individually before summing to avoid overflow and enforce the ceiling even while
storage is being reconstructed. Storage enforcement also applies this hard cap
before its Loading guard. Statistics remain untouched. Added tests for a 2-million
starting allowance, a 2.4-million loaded balance, actual underlying pool values,
and ceiling enforcement during loading. Unit suite and both complete storage/
capture probes pass (3 targeted CTest targets, 12.79 seconds). The pending 1.0.767
package is refreshed with this follow-up; it has not been installed yet.

## 2026-09-23 — Pre-release follow-ups (local 1.0.768)

Approved fixes from the 767 review: new expansions target three rocket turrets
on every difficulty; necessary police funding cuts remain immediate while
increases wait 30 game-seconds and clear a five-point deadband; Save As forks
map identity/revision lineage; metaserver INI metadata understands inline
comments; refunds preserve the starting-cash part of their actual payment.

Expansion placement scores the producing expansion's own coverage first and
keeps priority through the third turret. Its turret upgrades and construction
have a bounded capital reservation so cheap zoning cannot repeatedly spend
that money. Core Heavy/High Tech/Repair Yard completion precedes new custom
city expansion; the next MCV waits for three completed covers at existing
expansions. Planned turrets prevent duplicate orders but do not unlock the
next expansion. Repeatedly lost sites remain excluded during retained loss
history. Main-base opening, shared budgets and 33%/50% recurring policing
limits remain in place. This is not a guarantee that a yard cannot be killed.

Refundable progressive construction and prepaid Starport/Love Factory orders
record starting-cash contributions, return only that share to the exempt pool,
and keep earned refunds capacity-limited. Cancellation, prerequisite loss,
completion and capture clear the appropriate provenance; capture cannot mint
the prior owner's exemption. All balances stay <=999999. Save format 9846 adds
these fields; legacy orders have no recorded provenance and conservatively
remain capacity-limited on refund. Protocol 24 excludes mixed-rule peers.

Save As starts a distinct map ID at MapVersion 1; ordinary Save retains its ID
and increments metadata. Editing immutable shared content also forks identity.
Server metadata schema 5 reparses older cached entries lazily and preserves
quoted comment markers. Server source commit e6f6309d is packaged in website
commit 82398cc; that server deployment is pending the explicit push approval
requested in this task. No game push, PR, public release or MBA installation.

Claude subscription worker supplied the initial AI/accounting patch and hit its
turn bound; Codex completed integration, expansion reservations/sequencing,
provenance capture cleanup and regression coverage. Evidence is under
../outputs/release-fixes-768/. The final 24-minute same-map/seed six-house
Alkozeltser 4 checks kept all houses alive, all with eight refineries:
- Medium: zero yard losses, 423 funding changes (767: 1028), 13–25 R/C/I
  buildings per house, 8–36 rockets. Three crime outbreaks (3, 4, 11 hostiles).
- Hard: two yard losses (767: 12), 351 funding changes (767: 1275), 13–35 R/C/I
  buildings per house, 2–29 rockets. Three crime outbreaks (3, 6, 17 hostiles).
These are one-seed bounded runs, not universal balance or performance proof.

Validation: clean full 31-target CTest pass (279.66 seconds), plus 221 server
integration tests and the focused content parser/cache tests. The placement
probe checks three covers on all four difficulties and verifies that plans do
not release the next MCV before all three turrets are actually completed.
Refund probes cover real opening cancellation, mixed/fractional payments,
prerequisite removal, completion, capture, full builder save/load cancellation,
prepaid queue serialization and legacy queue field alignment in Vanilla and
DuneCity. Menu probes verify distinct Save As IDs, ordinary-save lineage, and
immutable-source forks across resolutions. Native builds, dependency audits,
version agreement and the website service snapshot installer/security checks
passed. Final candidate still needs platform CI and live multiplayer release
checks; no broader release-readiness claim is made here.

Production follow-up (user authorized deployment): website commit 82398cc was
rebased over the hourly statistics update to b9ea124 and pushed to main. Deploy
run 35852799408 succeeded. Production installed service snapshot
 a6c72446afe7c11a37108e064dd672f5e58448511333e2d24dc197d843635bb6,
matching the locally verified package. Live /p2p/v1/health returned status=ok;
all 139 catalogue maps remain available (129 Vanilla, 10 DuneCity), all 127
seeded latest revisions/category values verified, and mod-filter counts match.
Evidence: ../outputs/release-fixes-768/deployment.log and
production-catalogue.json. Game release and MBA installation were not performed.

## 2026-09-23 — 1.0.768 published

User authorized actual game deployment after the metaserver rollout. PR #71
merged at c0cfb48d7c9b712d79405b4aaf8d625422ea5cbd; stable tag v1.0.768 and
release run 35856127626 succeeded. Windows, Linux, Emscripten and signed/notarized
Mac packages passed their CI gates. PR CI caught two release issues: host PHP
OPcache hid dynamic fixture configuration (disabled only in test workers; all
221 tests pass with an OPcache-enabled host), and MSVC rejected conditional
construction of the noncopyable INIFile parser (scoped metadata read now avoids
that copy). The latter has repeated-download and local-name-collision coverage;
rendered menu tests passed in 98.49 seconds after the native rebuild/dependency audit.

Independent public verification downloaded all 13 GitHub assets, matched their
published sizes/SHA256 digests, verified all three Ed25519 update manifests and
both appcast archive signatures. Mac app and DMG stapled tickets validate; the
app passes codesign --deep --strict and Gatekeeper as Notarized Developer ID.
The signed app's hidden desktop runtime/rendering check passed on claw.local.
SourceForge run 35857539387 verified all seven package uploads and three OS
defaults. Independent HTTPS Git reads confirm its dunecity branch and peeled
1.0.768 tag point to c0cfb48d.

The exact stable CI browser artifact was reused, with bundled-mod verification,
matching PHP service packaging and website security/hash checks. Redundant web
rebuild 35857538995 was cancelled. Website commit d7711e4 (browser plus current
release prose) deployed successfully in run 35857658989. Live /play/build.json
is 1.0.768/c0cfb48d; all eight public browser artifact hashes and both desktop
landing pages were verified. The packaged PHP implementation is byte-identical
to the already deployed schema-5 service.

Additional integration: three native DuneCity peers continued with matching
cycle-150 state. A CI browser candidate joined an isolated native game, loaded
checkpoint 5109, caught up and visibly continued without a reported desync.
This is local RTC integration, not a fresh complete public WAN match. The
additional native UI smoke test could not be completed through the available
locked UI connection. All task-owned test tabs and native test processes were
closed. Stefan reiterated: do not test on his MacBook Air; use the Mac mini.
Read-only SSH checks found no native DuneCity process on the MBA, but older
browser preview/PHP fixture servers remain; they were not stopped. No MBA app
installation or interruption was performed during this release.

Evidence: ../outputs/release-768/ (audit.json, validation.md, CI logs,
published-verification.json, live-verification.json, sourceforge.log,
mini-runtime.log, real-peers/ and browser-native/). The public release is at
https://github.com/ggtothemax/dunecity/releases/tag/v1.0.768 .

Audio cleanup follow-up: CoreAudio's live process list identified the task-owned
ReleaseNative process on the mini still outputting audio after SIGTERM. The MBA
had no native game process; its active outputs were Winamp and avconferenced,
so remote audio was a possible route, not proven. After verifying the exact
release-test executable and arguments, SIGKILL stopped PID 56535. A fresh
CoreAudio process query on the mini returned no active output process. Other
apps and the MBA's older fixtures were left untouched. Verify process exit,
not merely a successful signal request, when cleaning up future GUI tests.

## 2026-09-23 — automatic desktop update confirmation (1.0.769, local)

The main menu now opens the existing Install update / Later confirmation when
the desktop updater reports an available supported update. It offers once per
application launch, waits for other dialogs to close, and preserves Later across
main-menu recreation. The footer update button can still reopen it manually.
Installation still requires Install update; matches are not interrupted.

Native build and dependency audits passed on claw.local. The silent, isolated
menu_navigation_probe passed at 640/854/1280 widths, covering automatic display,
Later, menu recreation, manual reopening and dialog deferral. A negative-control
run with the automatic trigger removed failed as intended; restoring it and
rebuilding passed. No MBA tests or interactive game/audio launches were used.

Source and local app are 1.0.769; protocol 24 and save format 9846 unchanged.
This change is committed locally on fix/automatic-update-prompt, not published
or installed. Public release remains 1.0.768. Worker evidence is in
../outputs/update-prompt-769/claude.json; menu evidence is build/menu-probe/.

## 2026-09-23 — 1.0.769 deployed and verified

PR #72 merged at 224b43424e5d05ab5e8cf09f025b5783989a0c26 and was tagged
v1.0.769. Independent Claude review found no blockers. PR CI 35861588457
and stable release CI 35862919846 passed. Duplicate main build 35862892187
was cancelled (force-cancel was needed for its always-running Mac job); the
release used the Mac mini runner. No MBA or interactive/audio tests were run.

All 13 public GitHub release assets passed size and SHA256 verification. Three
update manifests and both Mac/Windows appcast archive signatures verified with
the checked-in Ed25519 key. The Mac app and DMG passed independent notarization
ticket checks; app codesign and Gatekeeper accepted the Notarized Developer ID.
Their hashes match the published packages. SourceForge run 35864497542 passed
all upload readback hashes and Windows/Mac/Linux default checks. Independent
HTTPS Git refs confirm its dunecity branch and 1.0.769 tag at the release commit.

Reused the exact stable CI browser artifact after bundled-mod checks, avoiding
a redundant browser rebuild (35864497734 cancelled). Website b6d847d deployed
successfully in 35864719716; Web security 35864719331 passed. Matching service
packaging changed provenance only, not PHP implementation. Live play/build.json
reports 1.0.769 and the release commit; all eight browser file hashes and both
release landing pages verified. Verification used gzip delivery and hashed the
decompressed bytes. No fresh interactive browser or multiplayer session was
run for this desktop-only UI change. Existing menu probes cover the change.

Release: https://github.com/ggtothemax/dunecity/releases/tag/v1.0.769
Evidence: ../outputs/release-769/ (review.json, PR/release CI logs,
published-verification.json, sourceforge.log, live-verification.json and website
deployment log). This supersedes the prior local-only status. Existing clients
need to install 1.0.769 before they gain the automatic update prompt.

## 2026-09-23 — map copies, repair sidebar and silo priority (local 1.0.770)

The custom-map chooser previously concatenated all four installed/user map
directories without copy detection. Same-name, equivalent-content copies now
collapse to the first stable path with the highest observed revision metadata.
Files are untouched. BASIC gameplay rules (format, tech, win/loss flags, timeout,
scale and unknown keys) remain part of identity; only explicit cosmetic/catalogue
keys and Workshop bookkeeping are ignored. Effective map category/dependency
also distinguish variants. Different terrain/units/buildings/rules remain listed.
The actual MBA pair was not inspected; the duplicate mechanism was reproduced
with real INI copies through the chooser on the mini.

RepairYardInterface now stacks stats and a centred repair-unit progress portrait
inside the sidebar. Shared city-stat text fits the actual font/column width using
abbreviations and bounded fallback shortening. The live repair fixture checks
both city and vanilla modes, including 640x480 and the normal 1024x768 profile;
rendered portraits stay inside the right and bottom edges.

QuantBot previously reached silos only after optional growth/defence choices, so
a mature city could repeatedly fill its bank without increasing storage. Silos
now receive early priority at 80 percent earned-storage usage, counting spice
and tax income together. Preserve Stefan's correction: a heavy factory must
exist first, and custom DuneCity also completes available high-tech/repair core
buildings first. Queued silo/refinery capacity prevents duplicate yard orders.
The 999999 ceiling stops further storage investment. Optional capital reserves
cannot prevent an eligible yard from funding this storage purchase. Protocol25
protects deterministic AI decisions; save format9846 is unchanged.

Claude implemented the bounded UI patch/tests; Codex reviewed it, corrected BASIC
rule/dependency identity, added minimum-window verification, and implemented the
silo/protocol changes and real-order tests. Native build and dependency audits
passed silently on claw.local. All31 CTest targets pass in aggregate: initial
full run passed29; the two remaining fixture issues were corrected and passed
a focused rerun. The 640-only repair check now exits before unrelated feedback
button coordinates; policing fixtures use explicit starting funds instead of
accidentally filling storage. Silo tests cover every difficulty in Vanilla and
DuneCity, both income pools, core reconstruction priority, low usage, two yards
and the hard ceiling. Menu probes pass640/854/1280. No MBA or audible tests.

Source/local app1.0.770 is committed on fix/map-duplicates-repair-layout; not
installed or deployed. Public release remains1.0.769. Evidence:
../outputs/map-repair-770/ (Claude retry report, final-build.log, ai-tests.log,
full-ctest.log and focused-rerun.log), build/command-probe/640/ and menu-probe/.

## 2026-09-24 — offline custom-map collection (local 1.0.771)

New offline CustomGame matches queue their original pinned scenario and exact selected
mod revision after successful map loading. Replays, campaigns, loaded saves, network
matches and cancelled setup are excluded. Original map files and sidecars are untouched.
The existing persistent outbox now distinguishes collection from normal publication;
offline game frames service it as well as menus. All new collection I/O failures are
caught and network errors preserve the entry for retry without blocking the match.

The service adds POST /v1/content/lookup, matching exact map-file hash plus mod revision
across publisher identities. A hit writes a separate local collection receipt rather
than inventing ownership/server version for a local revision. A miss uploads the mod
dependency and map through existing bounded transfers. Begin/commit recheck collect=1
under the service lock to handle concurrent collectors; old records lacking derived
map metadata are checked through their small immutable manifests. Manual editor
publication is unchanged. Map type remains separate from the selected mod dependency.

Claude implemented the client/server path and tests; Codex reviewed and fixed legacy
lookup, concurrent-upload duplication, malformed lookup replies and the outer launch
exception boundary. Source/local app version is 1.0.771; protocol25/save9846 unchanged.
Native build and before/after dependency audits passed. Final CTest unit suite and
map_collection_tests passed (8 focused cases), server content29 tests passed, real
client/PHP wire test passed cross-creator duplicate/new-map cases plus unavailable
server -> persistent queue -> successful retry. Claude additionally ran combined
content/signaling196 tests and network/security CTest targets before final refinements.
No production fixture uploads, GUI games, MBA tests, push or deployment.
Evidence: ../outputs/offline-map-collection/ (worker reports, final-build.log,
final-server.log, final-ctest.log, final-wire.log).

Rollout: deploy the checked-in metaserver router, rewrite and Content.php before
releasing the client. An older service returns404 and maps remain queued until upgraded.
See tools/p2p-signaling/README.md for the collection contract.

The earlier MBA install request remains pending: signed/notarized1.0.770 was copied
and verified on the MBA, staged at /Applications/.dunecity-install-770.vLeoFr/dunecity.app,
but the installed1.0.768 process was running. It was not interrupted or replaced.
No app was launched by this task.


## 2026-09-24 — QuantBot foundations, condition repairs and Vanilla power (local 1.0.777)

QuantBot now prioritises the Construction Yard bulk-slab upgrade whenever
Concrete is required and the actual technology permits it. It holds other yard
construction while saving or repairing for that upgrade; unavailable technology
uses single slabs. All ordinary construction receives a complete foundation.
The deterministic planner prefers 2x2 slabs for residual strips, extending them
beside the footprint when legal, preserves roads, and handles blocked boundaries.
Walls/roads/slabs/power lines and MCV deployment remain outside yard foundation
orders. Disabling concrete skips both foundations and the concrete-only upgrade.
Campaign and custom queues share this policy across all four difficulties.
Partial rejected queues refund their slabs, redevelopment plans for cleared
zones, and a final actual-coverage check refunds a finished bare building.

Repairs start below full health for health-scaled production/output and exactly
free-to-repair buildings; DuneCity maintains every structure for land value.
Other buildings qualify above 5,000 available credits, with existing survival
and engaged-defence rules retained. Repairs require the engine's five-credit
minimum. Original-owner prices and the integer repair formula are preserved.

Vanilla buys additional generation after the refinery/worker and available
Light/Heavy Factory core exists. The whole shortage must fit cash after reserves
and forecast income must replace its capital in 30 game seconds. It also needs
10-minute avoided-repair payback or more than 5,000 spare credits after reserve.
Existing generator damage and pending orders prevent redundant purchases.
City operational power rules stay unchanged. No damage rates or save fields
changed. Protocol31, save9847, telemetry policy foundation-first-repair-power-v79.

Claude implemented a bounded worker task and one focused continuation; Codex
reviewed and corrected overhang tie-breaking, queue rollback, redevelopment,
free-repair classification, upgrade priority and Vanilla campaign power gating.
Reference: docs/quantbot-foundation-repair-power.md. Evidence and worker reports:
../outputs/quantbot-foundation-repair/. Native build and dependency audits passed;
all 41 CTest targets pass across the full run and two focused reruns. The full
run passed 39/41; the two Starport tech fixtures had disabled concrete together
with unrelated purchases, so their required slab offers are now enabled and
both reruns pass. The native unit target passes (854 cases, 3 intentional skips).
Final logs: build4.log, full-ctest.log and starport-rerun.log. No push, release or
installation.


## 2026-09-24 — Vanilla QuantBot power buffer (local 1.0.778)

Air1.0.777 session1790237738860854-0 confirms generation chased live demand:
at cycle43099 supply1798 plus2 repairable power faced1840 demand, and the AI
ordered just one100-power windtrap. Subsequent small shortages repeatedly
triggered another single purchase as queued buildings landed.

Economic generation now targets actual plus queued building demand, plus the
larger of20% anticipated demand (rounded up) and one available standard
windtrap. Existing output, restorable condition and all pending generation
count toward the target; pending output no longer blocks an incomplete package.
The same core, income, cash reserve and benefit gates remain. If the buffered
package is refused, an operating shortage can pass the same gates on its own,
using its own cheapest generator. No city/other-mod operational power changes.
Telemetry records target, queued demand, effective capacity, reserve and fallback.
Version778/protocol32/policy vanilla-power-buffer-v80; no saved fields changed.

Claude's bounded run produced the implementation and tests but hit its turn
limit before a final report; Codex reviewed it and completed build/verification.
Native build and dependency audits pass. Unit and all three opening-economy
CTest targets pass, including a Vanilla fixture rerun after resetting its second
yard's leftover upgrade state. Added real queued-factory demand coverage.
Evidence: ../outputs/quantbot-power-buffer/ (Air capture, log-findings.json,
build.log, focused.log, vanilla-rerun.log). Full regression follows the queued
MCV deployment fix. Committed locally; not pushed or installed over the Air game.


## 2026-09-24 — Prompt local MCV deployment (local 1.0.779)

Air 1.0.777 DuneCity session 1790238318405393-0 confirms delayed deployment,
not a complete inability to deploy: Atreides had five MCVs and only one yard
at 275 game seconds, over 87,000 credits, and one Light/Heavy Factory. Yard two
appeared at 366 seconds; yard six at 853 seconds. The second Heavy Factory
appeared at 822 seconds. The base was eventually overwhelmed; these timings
identify the growth bottleneck but do not establish the only cause of defeat.

Every additional city yard previously passed the remote-colony defence gate,
including yards on home rock. findRockExpansionSite returned before surveying
free home rock when advanced factories/turret coverage were missing. Deployment
also independently required that gate, causing serial defence waits per yard.
Local deployment now searches reachable, safe, unreserved 2x2 sites on connected
rock already occupied by the base before considering remote colonisation.
Exact connected rock is used: nearby build range across sand is not ownership.
Existing engine deployment rules, ground access, threats, repeated-loss checks
and remote-colony defence requirements remain. Trips keep their destinations;
five seconds without movement permit a retry, while permanent site obstacles
trigger replanning. Live MCV reservations avoid duplicate sites. The actual
successful deployment result controls cleanup, with no reads of the deleted
MCV. Existing saved assignment/timing maps are reused; no save-format changes.
Deployment telemetry records location, target, ownership, defence gate and result.

Version 779/protocol 33/policy local-mcv-deployment-v81, including the preceding
778 power-buffer fix. Claude produced the planner and a bounded probe follow-up;
Codex reviewed and corrected connected-rock classification, candidate filtering,
retry timing and deployment lifetime/result handling. Evidence and captured Air
logs: ../outputs/quantbot-mcv-local-deploy/. Native build and dependency audits
pass. All 41 existing CTest targets pass (full-ctest.log), plus the new real-engine
quantbot_mcv_deployment target (mcv-ctest.log): twelve home-rock MCVs deploy on
the first pass across all four difficulties; a travelling MCV moves and deploys
in 550 cycles / 8.8 game seconds. Reservations, transient traffic, stalled retry,
permanent obstruction, disconnected nearby rock and first-yard cases pass.
The focused unit/opening-economy tests also pass in all three modes. No full
match win/loss claim is made by these regression fixtures.
No push, release or installation over the running Air game.
