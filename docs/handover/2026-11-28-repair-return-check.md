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

Total over 10 actual repair trips: 6 ON AI evaluations, 4 dominated-area redirects, 4 successful redirected landings, zero unintended redirects. Every trip asserts full healing, real bay release, actual landing and cleared transport booking. The probe temporarily uses the Builder role to isolate repair/transport from offensive army controllers; normal-role interactions are measured by the arm, not claimed by this fixture.

Current path (line numbers at test commit): `Carryall.cpp:597` stores the pickup as the guard point; `RepairYard.cpp:231` requests return transport to that guard point after healing; `RepairYard.cpp:139-180` hands off the unit, with the new AI check immediately before it and `:165` setting the carrier's final destination. `UnitBase.cpp:639-686` performs the real deployment. A drive-in unit retains its guard point.

An initial probe commit `a4bf36940d4000baa3e6c4f20d6a38e2e232ed62`, job `20261128T161226-ctest-743875`, passed the first three cases but failed the drive-in case: the fixture incorrectly clicked a human repair command on an AI unit. It was corrected to the AI's real `Player::doRepair` wrapper; the failure was not suppressed. `10a1aec9616f7c52a95910442e55e2dd7c2915cd` is an intermediate, untested measurement commit, not the green candidate.

Activation telemetry: `REPAIR_RETURN event=redirect` at actual bay departure; `event=landed met=1` only from the real Carryall drop, within 3 tiles of the chosen destination and outside the old area's 8 tiles. Pending-flight observation/counters are not decision state and are intentionally not saved. ON/OFF styles and activation-registry entries are committed with the feature.

Unverified so far: natural-game arm scores and activation coverage, Air installation/playtest, interrupted flight/save-load observation continuity, no-Carryall redirected walking, and broad integration/nightly/performance gates. Those are not implied by the fixture passing.
