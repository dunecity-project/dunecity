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
