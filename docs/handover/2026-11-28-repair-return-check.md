# Repair return check — 2026-11-28

Stefan: “also make the ai decide if it wants to return a repaired unit to where it had been picked up from. if the enemy have superiority in that area don't return them”.

## Acceptance, before implementation

| # | Words mapped to observable acceptance | Required measurement |
|---|---|---|
| 1 | “ai decide” with RepairReturnCheck=1 | At least one real repaired AI unit evaluated at bay departure; activation asserted. |
| 2 | “enemy have superiority … don't return them” | 100% of dominated-area probe repairs redirected (stronger than 90% requested example); actual landing outside pickup radius. |
| 3 | Safe pickup areas still return | 100% of safe-area probe repairs land within 3 tiles of pickup. |
| 4 | Default 0 preserves existing behaviour | 100% of dominated-area OFF probe repairs return; zero redirects. |
| 5 | Human units unchanged | ON human dominated-area repair still returns; zero AI decisions. |
| 6 | Sent as well as carried to repair | Repeat dominated-area ON case with a unit that drives to the bay. |

Decision: use existing CombatPowerPolicy health-weighted combat power within 8 block-distance tiles of the return point, including allied units and the repaired unit exactly once; include turrets only if they cover the return point. Enemy superiority is strictly greater power, not all enemies anywhere on the map. Destination: QuantBot's existing protected base rally (`findProtectedRallyLocation`), or the repair yard when no rally is available. Default-OFF switch, no human change.

Safety: only the owning QuantBot of an AI-only house may change the guard/return point; OFF does no survey. Repair completion/payment and carrier ownership remain engine-owned. Progress: existing repair/carrier flight runs unchanged, with only the return coordinate replaced at actual bay departure. Real path probes must observe healing, release and landing, not merely a suggested order.

Deviations: fixed local radius 8 tiles defines “area”; true current local knowledge is used (not fog memory). This is not a claim of exhaustive strategic safety.

## Evidence

Targeted test commit `11c421030f404e67b9d8ceac93acae6573549f75`, parent `107061a46e738bccfcc9fd6651d09ca72f3a41bf`.
AWS job `20261128T161552-ctest-9d65d7`: `quantbot_repair_return_check` PASS, 89.80 s; collected 2026-11-28T05:18:20Z. Handed immediately to Integrator for the Air build, before the arm.

| Acceptance | Observed (each case repeated twice, identical records) | Result |
|---|---|---|
| 1, 2 | Unsafe carried: 1 check, 1 redirect, 1 asserted successful landing per run; pickup 34,36, final 5,0 | PASS |
| 3 | Safe (one equal-power local enemy plus 12 distant enemies): 1 check, 0 redirects; pickup 34,36, final 36,36 | PASS |
| 4 | Unsafe OFF: 0 checks/redirects; final 36,36 near pickup 34,36 | PASS |
| 5 | Human unsafe ON: 0 AI checks/redirects; final 29,27 equals pickup | PASS |
| 6 | AI drove into bay, late Carryall: 1 check, 1 redirect, 1 asserted successful landing; final 5,0 | PASS |
| 6, no carrier | Follow-on: AI drove into bay with no Carryall ever created; 1 check/redirect and actual walking to protected rally, repeated twice | PASS |

Total over 10 actual repair trips: 6 ON AI evaluations, 4 dominated-area redirects, 4 successful redirected landings, zero unintended redirects. Every trip asserts full healing, real bay release, actual landing and cleared transport booking. The probe temporarily uses the Builder role to isolate repair/transport from offensive army controllers; normal-role interactions are measured by the arm, not claimed by this fixture.

Follow-on aggregate `60db63cd10803749e8200a0fd43d00d980fca668`, parent `3f0cf01e64e3cade912878dc01706c4d633389cd`: AWS `20261128T162517-ctest-47ee6b`, PASS 106.6 s, collected 05:27:49Z. Gameplay code identical to `11c421`; only walking coverage and evidence were added. Six cases repeated twice: 12 repair trips, 8 AI evaluations, 6 redirects, 4 carrier landings plus 2 ground arrivals. Test-only delta handed to Integrator without holding the already-green gameplay landing.

Current path (line numbers at test commit): `Carryall.cpp:597` stores the pickup as the guard point; `RepairYard.cpp:231` requests return transport to that guard point after healing; `RepairYard.cpp:139-180` hands off the unit, with the new AI check immediately before it and `:165` setting the carrier's final destination. `UnitBase.cpp:639-686` performs the real deployment. A drive-in unit retains its guard point.

An initial probe commit `a4bf36940d4000baa3e6c4f20d6a38e2e232ed62`, job `20261128T161226-ctest-743875`, passed the first three cases but failed the drive-in case: the fixture incorrectly clicked a human repair command on an AI unit. It was corrected to the AI's real `Player::doRepair` wrapper; the failure was not suppressed. `10a1aec9616f7c52a95910442e55e2dd7c2915cd` is an intermediate, untested measurement commit, not the green candidate.

Activation telemetry: `REPAIR_RETURN event=redirect` at actual bay departure; `event=landed met=1` only from the real Carryall drop, within 3 tiles of the chosen destination and outside the old area's 8 tiles. Pending-flight observation/counters are not decision state and are intentionally not saved. ON/OFF styles and activation-registry entries are committed with the feature.

## ON/OFF arm

Main explicitly clarified at 16:33 AEDT that arm measurement may run concurrently with deployment: extensive testing must not delay the Air, rather than waiting for the deploy. AWS priority-10 job `20261128T163321-repair-return-check-24-e1db72` ran immutable `60db63cd10803749e8200a0fd43d00d980fca668`: rs160-dunecity, Brutal, seeds 9101–9124, both label/house assignments, 35-game-minute ceiling. All 48/48 games completed.

| Arm | House-games | Redirect activations | Per game | Observed successful landings | Per game | Games activated / effect |
|---|---:|---:|---:|---:|---:|---|
| RepairReturnCheck=0 | 48 | 0 | 0 | 0 | 0 | 0/48; intentional negative control |
| RepairReturnCheck=1 | 48 | 514 | 10.708 | 371 | 7.729 | 45/48 (93.75%) / 44/48 (91.67%) |

ON reaches its proximate effect in more than half of house-games, so the harness regards it as tested. OFF's registry prints NOT TESTED because its intentional negative control has no activations; the dedicated OFF probe proves old behaviour rather than claiming an OFF activation. Landing success was observed for 371/514 redirects (72.18%); the remaining 143 do not have a successful landing recorded. They are not silently counted as successes. Ground redirects, losses/interruption and other landing failures were not fully classified; successful-game raw traces are not retained by the harness.

Placement score: ON **0.031**, OFF **−0.052**, difference **+0.083**, 95% bootstrap interval **[−0.46, +0.62]**. ON won 24, lost 22, and 2 games timed out. Harness verdict: **level / statistically inconclusive**, not a demonstrated improvement or proof of equivalence. Keep the public default 0; Integrator may enable 1 in the Air dev build for Stefan.

Evidence: `/Users/stefan/Documents/projects/outputs/repair-return-check-20261128/summary.txt`, `activation.csv`, `games.csv`, `houses.csv`; queue-owned targeted logs under `~/.local/state/dunecity-queue/ctest/<job>/`.

Unverified: Air installation/playtest; interrupted-flight/save-load observation continuity; a cause-by-cause breakdown of the 143 redirects without observed landing success; and broad integration/nightly/performance gates (Integrator-owned). Those are not implied by the targeted probe or the arm.

## Pre-push registry contract fix

The bounded pre-push job `20261128T171455-integrate-air-bounded-ceed-f07b75` exposed the missing genome registration for `(Force Concentration, RepairReturnCheck)`, breaking seven shared registry consumers. Add the standard boolean gene `repair_return_check`, levels `[0, 1]`, default `0`, and cover its effective default, required-presence contract and rendered ON/OFF values in `test_genome_model.py`. Gameplay and the shipping switch default are unchanged.
