# Infantry AI switches takeover — 2026-11-28

## Acceptance before changes

| Stefan's request | Observable acceptance |
|---|---|
| "the ai should use it if it wants to build these men" | SquadOrders: cheaper squads deliver three men; report paired rs160 and Air-map scores and activation. |
| "also include infantry in the calculation for what to build (same performance algorithm)" | InfantryInMix: purchases for the measured share actually produce infantry in more than half of rs160 house-games. Destroyed-building survivors never count as purchase success. |
| "additional infantry and wor should be built if the ai needs more capacity similar to factories" | InfantryLanes: sustained funded demand survives normal between-order gaps; an additional lane is ordered only with busy existing production and unmet demand. Report actual activation, not an assumed effect. |
| "Do NOT change defaults in code; recommend" | All three defaults remain zero; recommendations follow activation and paired-score evidence. |

Safety: queued first producers prevent duplicate purchases; city working cash remains protected; unavailable house technology is never selected. Progress is conditional on available build sites, prerequisite technology, positive infantry demand and spendable cash. A shortage of cash or demand is not proof of a capacity shortage.

Deviations inherited from the previous implementation: additional lanes require two minutes of sustained funded demand and at least three orders per existing lane. These remain explicit rather than manufacturing demand just to activate a switch.

## Inherited evidence

Source: `96c1a3ff507f120d0de1993d652756342ad02212`. Brutal, 24 seeds, six house rotations, 144 paired games/arm, 288 treatment house-games. rs160 limit 45 minutes; Air-map limit 60 minutes. Evidence: `/Users/stefan/Documents/projects/outputs/infantry-squads-20261128/`.

| Switch | Map | Paired score difference (95% CI) | Fired | Effect | Interpretation |
|---|---|---:|---:|---:|---|
| SquadOrders | rs160 | +0.027 (-0.08..+0.13) | 59% | 59% | Tested, not clearly worse |
| SquadOrders | Air | -0.010 (-0.10..+0.09) | 75% | 75% | Tested, not clearly worse |
| InfantryInMix | rs160 | +0.006 (-0.11..+0.13) | 34% | INVALID | Not tested: effect counted building survivors |
| InfantryInMix | Air | -0.100 (-0.18..-0.02) | 100% | INVALID | Purchase trigger active, score clearly worse; delivery not proven |
| InfantryLanes | rs160 three-game acceptance | not measured | 0% | 0% | Not tested |

Jobs: rs160 `20261128T150641-sq2-squad-orders-rs160-8de8b5`, `20261128T150657-sq2-infantry-in-mix-rs160-703d78`; Air `20261128T151735-sq3-squad-orders-air-b15d85`, `20261128T151741-sq3-infantry-in-mix-air-0b8292`. Both Air jobs completed; report tables were read from collected result CSVs. `dunecity-queue report` currently supports CTest jobs only, and rejects game-job IDs; game results are reported with `vanilla-match.py --report`.

Initial recommendation: SquadOrders ON under the assignment's not-clearly-worse rule; InfantryInMix OFF pending corrected delivery/trigger evidence (and adverse Air score); InfantryLanes OFF until a real capacity-limited arm proves it.

## Corrected implementation and targeted checks

- First producer: remove the duplicated economy reserve and arbitrary extra 1000-credit hurdle from an already reserved pass budget; retain the city working reserve. Consider the first Barracks/WOR before optional city growth. The execution gate permits a measured-share WOR without demanding anti-air support, while retaining working cash.
- Output evidence: the engine logs actual infantry deliveries (including singles), price and producer; the harness matches a full delivery to that producer's accepted `city_infantry_share` order. Other orders invalidate a pending match. Cancelled/unfilled orders, partial output, building survivors, census shares and ordinary Trooper support do not count.
- Lanes: keep the two-minute funded-demand clock across normal delivery/reorder gaps, but require busy existing lanes at the instant of expansion. Do not inflate demand to make an experiment fire.
- Harness isolation: every vanilla-match arm explicitly disables the lobby's new `AI Infantry Tactics` convenience option, leaving Config switches independent. The option itself is another agent's work.

Frozen source: `e1f7157563cdf47123fc71fcae7ccd439c308640` (off-branch test commit on `107061a46e738bccfcc9fd6651d09ca72f3a41bf`). Exact source patch: `outputs/infantry-squads-20261128/infantry-fix.patch` outside the repo. It excludes EconomyFirst's changes to the shared Yard file.

Green: `20261128T154031-infantry-delivery-v3-c24bde`, **8/8 PASS** on aws-mel2, observed by 2026-11-28 15:49:49 AEDT: infantry_squad_probe, infantry_squads_real_game, quantbot_activation_report, dunelegacy_tests, quantbot_all_against_atreides, quantbot_all_against_atreides_dune2r, quantbot_scenario_matrix, quantbot_ffa_army_deploys. The superseded snapshot `c0111007c0fde91905823293f1aa769df8345d42` also passed its four targeted tests in `20261128T153908-infantry-delivery-v2-72be3b`, but does not exclude ordinary support from activation and must not be integrated instead.

The green real-game registration does **not** mean every default-off AI line is met:

| Assertion | Measured result | Outcome |
|---|---|---|
| S1 squad delivery and price | 43 / 39 / 53 squads; no short deliveries; 137 orders at 50 or 200 | PASS |
| S2 AI prefers cheaper squads | 11 buying house-games; 100% of men as squads in each | MET |
| S3 extra lanes raise output | No backlogs, no extra lanes in 12 house-games | NOT MET / NOT TESTED |
| S4 full measured-share purchase by minute 20 | Share moved in 12/12; half-share delivery reached 1/12; 13 first-producer selections | NOT MET |
| S5 infantry housing tax/population | 171 census rows; 252 building-years; no violations | PASS |

The first-producer count is selections, not proof all those buildings stood. The corrected paired arm is `20261128T154601-infantry-fixed-mix-rs160-28f7d7`, 144 paired rs160 Brutal games, priority 10, AWS only, exact frozen source above.

No defaults changed. Claims released; the green candidate was handed to Integrator for authorized local landing/playtesting. No claim is made here about its landing or installation on the Air. `perf_regression`, native GUI input and combined feature-batch testing were not run by this worker.


## Final controller correction and results

The first corrected arm (`e1f715756`, job `20261128T154601-infantry-fixed-mix-rs160-28f7d7`) exposed another real trigger conflict: only **138/288 (48%)** treatment house-games bought and delivered for the share. Its +0.026 (-0.08..+0.13) paired score is therefore **NOT TESTED**, not evidence of parity. Harkonnen activated in only 3/72 house-games: the reactive Trooper controller always got the WOR first when aircraft were visible.

The follow-up runs measured-share WOR recruitment first when InfantryInMix is ON, including when aircraft are visible. If the share cannot accept an order, reactive support remains the fallback. OFF preserves support-first ordering. It does not raise shares or manufacture infantry demand, and its success event was not broadened to ordinary support. A share order consuming a shared Trooper capital allocation marks that allocation consumed.

Final frozen source: **`9896a2ca0ffd5f27b06cce8d71c6fe6210e85f56`** = `e1f7157563cdf47123fc71fcae7ccd439c308640` plus the WOR controller change. Prepared entirely as an off-branch patch during the integrator's staging freeze; no live staging source was edited. Exact follow-up patch: `/Users/stefan/Documents/projects/outputs/infantry-squads-20261128/infantry-controller-followup.patch`. Preserve the other feature's `aiInfantryTacticsEnabled` wrappers when combining it.

Own tests: **4/4 PASS**, `20261128T160001-infantry-controller-own-6f942b`, AWS: infantry_squad_probe, infantry_squads_real_game, quantbot_activation_report, dunelegacy_tests. Durable green collection: **2026-11-28T16:06:54.034189+11:00**. The earlier e1 commit passed all four AI gates; those gates were not rerun by this worker on the final controller commit. The integrator owns combined-source qualification and Air installation.

Final paired rs160 job: **`20261128T160001-infantry-controller-rs160-be189e`**, 144 games, source above, Brutal, 45 game minutes, seeds 9101–9124 and six treatment/control house rotations, AWS priority 10.

| Arm / source | Map | Games | Paired score difference (95% CI) | Treatment fired / fully delivered | Interpretation |
|---|---|---:|---:|---:|---|
| SquadOrders / 96c1a3ff5 | rs160 | 144 | +0.027 (-0.08..+0.13) | 59% / 59% | Tested, not clearly worse |
| SquadOrders / 96c1a3ff5 | Air | 144 | -0.010 (-0.10..+0.09) | 75% / 75% | Tested, not clearly worse |
| InfantryInMix / 96c1a3ff5 | rs160 | 144 | +0.006 (-0.11..+0.13) | 34% / INVALID | Not tested |
| InfantryInMix / 96c1a3ff5 | Air | 144 | -0.100 (-0.18..-0.02) | 100% / INVALID | Adverse score, purchase delivery not proven |
| InfantryInMix / 9896a2ca0 | rs160 | 144 | **-0.003 (-0.11..+0.10)** | **186/288 (65%) / 186/288 (65%)** | **Majority activation and actual output proven; not clearly worse** |

An additional seconds-long assertion over the collected final tables passed: 144 error-free games, >50% treatment house-games fired and fully delivered, and **zero** OFF-control `city_infantry_share` buys or matched delivery events. Per house, treatment successes were Harkonnen 49/72, Atreides 35/72, Ordos 56/72 and Sardaukar 46/72. Atreides still does not activate in most of its own house-games; the requested most-house-games criterion is met overall, not per faction.

The final source's three-game combined-switch acceptance reports S1 PASS (75/39/53 squads delivered, zero short), S2 MET (11 buying house-games, 100% squad men), S3 NOT MET (no backlogs or extra lanes), S4 NOT MET (half-share delivered by minute 20 in **3/12**, up from 1/12), S5 PASS (170 tax census rows, no violations). The original stronger S4 requirement is not silently replaced by the majority-activation experiment: it remains explicitly unmet.

A corrected-source Air arm was attempted after the rs160 result, but **not admitted**: Mini free disk was below the policy's 60 GB research watermark. No job was created, no guard was bypassed, and no latest-source Air qualification is claimed. The inherited Air table is not presented as a measurement of the new controller.

### Default recommendations (no defaults changed)

- **SquadOrders: recommend ON** under the assignment's rule (in-game squad checks pass; neither map's paired interval is clearly worse).
- **InfantryInMix: recommend OFF for now.** The requested corrected majority-activation arm is now valid and level on rs160, but full S4 remains unmet, and corrected-source Air evidence is unavailable. The old source's Air arm was clearly adverse. Run the same corrected-source Air arm when normal resource admission allows it before considering promotion.
- **InfantryLanes: recommend OFF / NOT TESTED.** The cheap demand-clock correction does not invent a backlog; no qualifying backlog or added lane occurred in the real-game cases, so output improvement is unproven. This is not a negative score verdict on the idea.

Nothing was pushed or directly committed to the integration branch by this worker. Exact green source and follow-up were handed to Integrator; report updates are supplied as a separate patch because its first-build freeze includes documentation bytes and claims. Native GUI input, final combined feature batch, performance regression and final controller Air-map coverage remain outside the verified evidence here.

## Completed corrected Air arm and real capacity causality

This section supersedes the earlier Air-admission limitation and the earlier
"extra-lane output unproven" statement. It does not rewrite historical failed experiments.

| Corrected InfantryInMix source | Map | Paired games | Paired score difference (95% CI) | Fired and fully delivered |
|---|---|---:|---:|---:|
| `9896a2ca0ffd5f27b06cce8d71c6fe6210e85f56` | rs160 | 144 | -0.003 (-0.11..+0.10) | 186/288 (65%) |
| same | Air-1v3 | 144 | -0.058 (-0.14..+0.03) | 288/288 (100%) |

The corrected Air job was `20261128T162021-infantry-controller-air-6d84e4`;
all 144 games were error-free and OFF controls had zero measured-share purchases
or matched delivery events. Unlike the inherited adverse Air arm, this measures
actual output from the requested build-mix orders, not building survivors.
The stronger S4 half-share-delivered-by-minute-20 requirement remains **3/12,
NOT MET**; majority activation is not presented as satisfying S4.

Natural rs160 diagnostics (`20261128T163619-infantry-lanes-diagnostics-560423`)
found no qualifying backlog in 3 games / 12 house-games. Of 430 demand samples,
320 were below the demand threshold, 99 had no producer, 9 lacked reserved cash,
1 had an idle producer, and 1 was sustaining demand. Existing producers were
busy in only 36/331 applicable samples; median deficit was zero for every faction.
The longest sampled demand run was 1,300 cycles (20.8 seconds), below the
required two minutes. The trigger is reachable; ordinary demand does not keep
one lane saturated long enough. No demand or threshold was fabricated in production.

The forced, funded capacity fixture starts with exactly one producer and a real
35% Infantry strategy, 48 initial Tanks and a one-time 100,000-credit scenario grant.
Its initial infantry deficit is 41,156. Full AI planning, normal orders, placement,
production and simulation ticks run without further cash grants.

| Producer | OFF added | ON added | Matched post-placement OFF men | ON men | First order / standing samples |
|---|---:|---:|---:|---:|---|
| Barracks | 0 | 1 | 36 | 72 | 137 / 155 |
| WOR | 0 | 2 | 22 | 47 | 138 / 164 |

Samples are 62 cycles / 0.992 seconds. Both arms use the ON arm's same output
window through sample 480; at most one extra producer is pending. Original
causal source `8c34e6d52a757f1264481160ebb9a39aad8159d4`, job
`20261128T163618-infantry-lanes-causality-3c0f38`, passed. The same physical
capacity assertions passed again on the unified-permission/native strict source
in `20261128T174357-strict-squads-cutover-v6-b89537`.
The original causal source used raw Config gates; the current permission cutover
also keeps all three infantry switches raw and independent. Its common fixture
pins the separate capture/garrison permission OFF, so the capacity OFF arm is isolated.

## Stefan's mandatory-squad follow-up

> "so ai should always buy the squads over singles. its simple economics"

Additional acceptance: with raw SquadOrders ON, zero ordinary Soldier/Trooper
single purchases when that producer offers its squad; full quote saved rather
than falling back to a single; no separate squad-value admission or per-man-price
comparison; delivered men/minute does not fall. An exceptional single requires
an actual nearby air engagement, an affordable Trooper, and a squad not fundable
within three seconds from spendable cash plus the existing net-income forecast.
Spendable cash retains the existing city working reserve. Every such exception
is counted with its threat, cash, quote, forecast and cycle evidence.

The authoritative QuantBot order boundary rejects ordinary available-squad singles;
normal selectors, support, capital choices and capture recruitment use the same
available squad. A single remains legal when its producer genuinely lacks squad
technology. One single-only producer cannot release another producer's saved squad cash.
Human purchases retain their existing singles/squads behavior. No raw defaults changed.

Native strict source: `0ebd5ffe9fef9644a7931288e4c08bb9ad6e64dc`, based on the
green unified-permission cutover `414d3c73c512b96dd76ed8f3b0a1268d2430edf8`.
Its strict, capacity, human-squad and garrison-settings checks all passed in
`20261128T174357-strict-squads-cutover-v6-b89537`. That job was **4/5, not green**:
the unrelated selected-zone source-text assertion was stale after the roster UI change.
Main authorized replacing that brittle scan with actual selected-widget behavior.
Standalone Catch does not link the full Game/GUI engine, so the regression moved
into the existing real panel fixture: selected empty Dune2 zone shows its actual
house portrait, five real occupants replace icon/preview, and Evacuate restores
the same portrait. Nothing is skipped or replaced with a no-op; no production UI logic changed.

Final test source `c4e0a092021db9a2e57293c6041b14d67ad25ea9` changes only those two
test files relative to the native strict source. Unit and actual panel checks passed
**2/2** in `20261128T175413-strict-squads-widget-v7-1c72bd` (4.0 / 17.1 seconds),
durably collected **2026-11-28T17:55:13.533675+11:00**. The four unchanged gameplay
checks were not redundantly repeated, per Integrator's instruction. The original
five requested checks are therefore satisfied across the native run and its
test-only follow-up, with additional actual-widget coverage; this is not a claim
that one five-test job passed on the final hash.

Strict probe: 9 real scenarios, **53 squad orders, zero ordinary available-squad
singles, one counted real-air emergency**. The genuine single-only-tech boundary
also produces exactly one legal single plus one available squad, four men total.
Full quote minus one waits; the fully saved quote buys three with only one-single
value room. Deliberately premium squads (120 vs single 30; 400 vs single 100)
still buy three, proving neither per-man comparison nor separate squad admission
allows a fallback. Each cash/premium case physically delivers six men.

| Equal 1.984-minute production window | OFF delivered | ON delivered | OFF men/min | ON men/min |
|---|---:|---:|---:|---:|
| Soldier | 26 | 78 | 13.105 | 39.315 |
| Trooper | 16 | 48 | 8.065 | 24.194 |

These rates are independently checked from actual factory-delivery events, not
house counts or free building survivors. The permission-enabled capture case
delivers six men as squads; all six zone tactics are enabled there, so it is **not**
an isolated raw-levy A/B claim.

Earlier strict probe failures were repaired, not hidden: v1 exposed the generic
military-emergency bypass of saving; v2 used an incomplete white-box capital plan
and crashed; v3's deliberate price edit failed to refresh the real cached catalogue.
The final fixture uses the complete production planning pipeline and actual rebuilt offers.
The v5 pre-cutover source also passed unit, human-squad and strict probes 3/3 in
`20261128T173544-strict-squads-own-v5-4fd5cd` before the permission-native migration.

Integration receives only the own delta `strict-squads-feature-v6-cutover.patch`
and `zone-portrait-behavior-followup.patch`, under the evidence directory above.
Off-branch snapshots contain older-tree reversions and must never be whole-picked.
Staging source was not edited during the freeze. Installation, protocol bump,
combined-stack qualification and performance regression remain Integrator's responsibility.


### Mandatory-squad Air-1v3 ON/OFF result

Job **`20261128T175531-strict-squads-air-e454d0`**, final source
`c4e0a092021db9a2e57293c6041b14d67ad25ea9`, completed
**2026-11-28T18:06:53.521180+11:00**. AWS/aws-mel2, priority 10, Brutal,
24 explicit seeds 9101–9124, six assignments each, **144 error-free paired games**,
60-minute limit, Neutral/Atreides/Ordos/Mercenary. Only raw SquadOrders changes;
InfantryInMix and InfantryLanes are OFF. The historical Air rule snapshot lacks
the new global permission, so the harness explicitly keeps capture/garrison
permission OFF in both arms. This is not a score qualification of the all-six
permission-enabled combined stack; that path has the separate physical probe above.

| Air arm | House-games | Mean placement score | Sole wins | Eliminated | Alive at timeout | Paired difference (95% CI) |
|---|---:|---:|---:|---:|---:|---:|
| OFF default | 288 | -0.151 | 5 | 80 | 203 | control |
| ON mandatory squads | 288 | -0.197 | 5 | 85 | 198 | **-0.047 (-0.13..+0.05)** |

The existing report verdict is **level with default**, not a demonstrated score
improvement: its interval includes zero. There were 134 multi-survivor timeouts
and 10 completed victories. Activation and actual full-squad output both occurred
in **216/288 (75%)** treatment house-games: Neutral, Ordos and Mercenary each
72/72. Atreides had no ordinary infantry purchase demand in this isolated arm;
its Soldier purchase path is exercised by the controlled production probe.

| Actual purchase/output evidence | OFF | ON |
|---|---:|---:|
| Accepted squad orders | 0 | 2,587 |
| Accepted single orders | 4,206 | 2 |
| Ordinary singles with a squad offered | 4,206 | **0** |
| Counted, exactly matched real-air exceptions | 0 | **2** |
| Actual delivered men | 4,191 | **7,718** |
| Observed house-minutes through elimination/end | 15,592.820 | 15,403.112 |
| Actual delivered men per house-minute | 0.268778 | **0.501068** |

ON delivery rate is **1.864x (+86.4%)** OFF. Every counted exception exactly
matches one accepted Single order's game/house/builder/item/cycle. Both have
actual engaged aircraft, Trooper price 100, squad quote 200 and a three-second
forecast; source CSV evidence is:

| Seed / Mercenary | Spendable credits | Forecast receipts in 3 sec | Sum | Hostile aircraft / engaged |
|---|---:|---:|---:|---|
| 9103 | 114 | 84 | 198 < 200 | 5 / true |
| 9118 | 101 | 85 | 186 < 200 | 3 / true |

No unmatched single, multiply-counted exception or short actual delivery passed
the acceptance assertions. Ordered but unfinished/cancelled production is not
counted as delivered men. Collected CSVs and the acceptance summary are under
`/Users/stefan/Documents/projects/outputs/infantry-squads-20261128/strict-squads-air`.

### Final default recommendations — no code defaults changed

| Switch | Recommendation | Basis and remaining limitation |
|---|---|---|
| SquadOrders | **ON** under the requested rule | Strict purchase/forecast/technology assertions pass; new Air score is not clearly worse; actual men/minute rises. Historical rs160 score is +0.027 (-0.08..+0.13), but the new mandatory policy was not newly A/B tested on rs160. |
| InfantryInMix | **ON for the requested majority-activation/not-clearly-worse criterion** | Corrected rs160 65% and Air 100% actual purchase success, both paired intervals include zero. Stronger S4 half-share-by-20 remains 3/12 and is not waived or claimed met. |
| InfantryLanes | **OFF as a general default / natural score benefit untested** | Forced real backlog proves added capacity and higher output; ordinary diagnostics have zero backlog activation, so no natural paired-score promotion is supported. |

Keeping all three raw switches ON for Stefan's authorized Air playtest is a
separate Integrator/owner decision, not a statistical promotion claim for lanes.
This worker did not change defaults, push, edit frozen staging source, or claim
an Air installation. The only added scope was Main's explicitly authorized
replacement of the stale UI source assertion with stronger real-widget behavior.

