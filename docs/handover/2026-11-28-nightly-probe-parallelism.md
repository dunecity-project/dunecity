# Nightly native-probe parallelism — 2026-11-28

## Acceptance contract

The coordinator asked to split the roughly nineteen-minute `city_plane_effect`, then the other four critical paths, preserving "identical assertions and coverage" and "each shard independent", aiming for a headless nightly below eight minutes. Before implementation the workstream established these measurable requirements:

| Requirement | Acceptance |
| --- | --- |
| Preserve statistical gates | The same complete seed/case/arm rows reach the existing coordinator, confidence intervals, report and assertions. Never replace a full-cohort interval with smaller per-shard intervals. |
| Independent execution | Each native worker owns its profile, saves, logs and output. Compile the helper once per test. Merge a complete, nonduplicated partition in deterministic original order. Any worker failure fails the test. |
| Determinism | Run seeded serial/parallel measurement comparisons, with no numerical normalization. Keep serial CLI operation available. |
| Honest admission | CTest declarations and the cluster runner reserve the configured concurrent native/path workers. Timing-sensitive tests retain their separate lane. |
| Real performance result | Run the full `-L nightly -LE gui` scope on the final landed source and report its time. Component timings alone do not establish the eight-minute target. |

No production game logic or gameplay assertions are changed by this work. Test-only helpers and driver execution change. Existing fixture/source provenance and Workshop/save pins remain authoritative.

## Why the long tests differ

`city_plane_effect` previously ran all encounters serially in one native process: 480 paired turret passes (960 encounters), 117 three-arm bombing cases (351 encounters), and nineteen fixed-check groups. It now partitions independent native cases over four processes and restores the exact byte-row order before the unchanged report/statistics.

`trooper_ornithopter_effect` similarly had 486 pairs / 972 rows in one native process. Four private workers now produce the same full matrix before the unchanged full-cohort assertions.

Menu navigation is a stateful core sequence plus a self-contained six-edition family matrix at each of 640x480, 854x480 and 1280x720. Six CTests preserve those three core and eighteen edition rows, sharing the existing immutable compiled fixture. The old canonical menu test is replaced, not aliased. Its scheduling-contract consumer is migrated to the expanded names and supports CMake template property declarations.

Turret drill was already running its two twenty-three-minute audit continuations concurrently. The driver change compiles once instead of playing unrelated warm-up games. An approved trial of four background path workers passed a short equivalence diagnostic but failed the full unchanged gameplay gate; it is not qualified for landing. The replacement candidate retains the original single-worker inline path algorithm and tests only the compile-once driver change.

City lessons is one seeded fifteen-minute Metropolis trajectory with interacting houses, not a collection of independent trajectories. It already bypasses rendering and pacing and uses four path workers. Splitting its observers would duplicate the simulation rather than shorten it. The approved trial only permits the existing headless-assets startup path for this non-rendering probe; all cycles, telemetry and gameplay checks remain.

## Completed component proofs

These are observed component timings, not a controlled same-source speedup experiment. The old 48-slot baseline used source `96c1a3ff507f120d0de1993d652756342ad02212`; the first shard candidates were private-index commits on `4a98624449ded483b97cc46e093f692a4528f785`, with shared cluster load.

| Component | Immutable candidate | Queue job | Outcome |
| --- | --- | --- | --- |
| City | `bd439d1c87433aa9d7040debaff9ba82c9214ae7` | `20261128T173243-city-shards-148440` | 2/2 passed: full unchanged matrix 306.93s versus baseline 1142.86s; seeded four-case-per-cohort exact byte-row diagnostic 53.56s. |
| Trooper | `431b94dcd57f3036e089d646c44d0bf4d7827e6f` | `20261128T173251-trooper-shards-aea838` | 2/2 passed: full matrix 213.02s versus 818.61s; twelve-pair, all-terrain exact measurement diagnostic 39.42s. |
| Initial menu split | `a49fafae40dc38d84edbc1dbb0d5fbcb4a6ba98c` | `20261128T173255-menu-shards-9c2ca6` | All six passed; longest shard 195.78s versus the 726.94s monolith. |

Each unit in these three initial green proofs had one Passed attempt, with no retries or lost attempts. Full terminal reports were read. Later failures below remain failures; they are not replaced by these earlier results. Reports and minimal own-only patches are under `outputs/aws-fleet-20261128/` on claw.local.

## Final component proofs and retained failures

- Menu candidate `0147ca8a36d701b4e12911b5f502df65736f3b7a`, job `20261128T180312-menu-final-proof-cc37c6`: all six unchanged native shards passed (longest 206.1s), but the scheduling-contract check failed. Its CMake parser incorrectly matched both the concrete menu namespace and an unrelated unresolved `${prefix}` namespace.
- Corrected menu candidate `0074f3ef97a401a47372b0f0e5088d8ca6f80acf`, job `20261128T181158-menu-contract-repair-f7fb9e`: all eight Python scheduling checks passed in 7.33s, including ambiguous-template and unknown-prefix regressions. Only the parser changed after the six native passes. Integrator landed the minimal v4 patch as `d11ac3824a44480a96fc4b743e5a054b7d603a5c`; no v3 landing or duplicate native rerun.
- Turret path trial `9b3ba6bce438ebd0bf558366c5e60976495d91dd`, job `20261128T180312-turret-path-proof-54e4be`: short one-minute row equivalence passed in 132.8s. The full twenty-three-minute acceptance failed after 773.60s: seven gates passed, but Ordos structure losses were 281 in the drill arm versus 298 in control. This is not an equivalence proof of the full trajectory, and the trial must not land. Further work on that owned failed job was cancelled; original failure evidence is retained.
- Inline-path replacement `9c348b0485328e67e0db61901b45138794e620a7` retains two independent arms and the original inline path setting, declaring two processors. Job `20261128T183037-turret-inline-equivalence-789012` passed its short exact-row diagnostic once on aws in 209.9s. It is not full acceptance: QualTriage confirmed the parent trajectory is already known red and explicitly requested no duplicate full run. Keep the compile-once/inline patch unqualified until the genuine game regression is fixed; no path-worker waiver or reduced gameplay assertion.
- Initial lessons trial `9a3bc604c60a5f6562b833082c85c79e8ad1bff4` and control `80e8c39c690ce1ed760c2e8ae9538c6cde79cd6c` share parent `21ef5e7b08ae41a099bc1360de35f0bbbf1cd3b8`. Jobs `20261128T180312-lessons-assets-proof-c85d9b` and `20261128T180313-lessons-assets-control-58998e` each passed once on aws-mel2, taking 464.84s and 471.52s. Their successful native logs were discarded by the normal retention policy; equal final PASS markers are not evidence of equal measurement rows.
- The driver now echoes every native `CITY_LESSONS_PROBE:` measurement into the retained CTest log. Trial `54f36c8ef2abe35d4a1015d8cf35aded00f2d82f` and control `5cc53a5ad93be3fb19ec2fe60720d7a9fb015ebc` share parent `2659756454dcc6ab4f6a3308058f9683e49eb732`. Jobs `20261128T182716-lessons-native-rows-trial-af6d39` and `20261128T182716-lessons-native-rows-control-d520c9` each passed once on aws without loss, taking 559.91s and 580.39s under shared load. All eight complete native measurement rows match exactly after removing only the logging prefix, including first-minute passes, planned sites/cycles, land-value effect and full built totals. First-minute activation is 26 on passes against zero off; the full on count is 551. All original game assertions, telemetry and fifteen-minute trajectory remain. Exact evidence: `outputs/aws-fleet-20261128/lessons-native-rows-equivalence.json`.

No final landed-source nightly duration is established by these component timings. The eight-minute target requires the full headless registry on the exact final source, not a sum, estimate or restamped result.

## Runner boundary

Runner commits `ee3288ccb1` and `4c44689b08` retain 48 weighted CTest slots within each AWS worker's 64-weight total. Reservations are City/Trooper 8 each, menu 2 per shard, turret path trial 10 (two callers plus eight background workers), and city lessons 5. The inline turret replacement declares two processors; the historical runner reservation remains conservative until its cutover. The last controller-only rollout passed all 610 system-Python tests. This is admission policy, not hidden source rewriting: an older frozen SHA still runs its original scripts and registry. Qualification must use a new exact landed source/plan; no result is restamped across SHAs.
