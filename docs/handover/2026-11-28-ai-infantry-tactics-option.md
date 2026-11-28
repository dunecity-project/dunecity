# Garrisoning and Capturing rule — 2026-11-28

## Corrected request and acceptance (recorded before rework)

Stefan: “i didn't ask for an ai infantry tactics button. i asked for a button for allowing garrisoning and capturing”. The earlier AI-only bundle below was the wrong feature despite its passing tests.

| Requested outcome | Observable acceptance assertion |
| --- | --- |
| One rule, not an AI infantry button or mod | The normal Options and match Game Rules show **Garrisoning and Capturing**, with an explanation applying to human and AI zone control. |
| Default ON in Dune City | The real city menu initially checks the rule; vanilla does not offer it and keeps normal non-city capture. |
| Human permission ON | Through the real Capture command and map click, soldiers enter an own zone and capture an enemy zone, with at least one observed entry/capture. |
| Human permission OFF | Capture is hidden/disabled for single and multi-selection; C cannot open the cursor; a city zone cannot be captured through an authoritative queued command or legacy damaged-building fallback. |
| AI permission ON | With the six zone-config switches off, real AI garrison/capture orders execute and reach a zone; activation and entry/capture counts are greater than zero. |
| AI permission OFF | Even with those config switches explicitly on, no AI garrison/capture orders or entries occur. |
| Three unrelated infantry switches remain independent | SquadOrders, InfantryLanes and InfantryInMix are read only from their own config switches; the rule cannot enable them. Integrator keeps all three on in the Air copy's DEV_SWITCHES. |
| Shared-human safety remains intact | Human-first and Human-last shared houses retain real-player garrisons 2→2; the AI-only quiet-zone control releases 2→0. |
| INI, settings and multiplayer consistency | One field/key, both states persist/reopen and round-trip, hashes differ, MOD18 bit5 carries OFF, and shipped MOD19 bytes 0/1/2/3 preserve installments bit0 without a stale AI-only setting. |
| Qualification and handoff | Targeted real-path tests pass at an immutable commit, then Integrator immediately lands/deploys to the Air before its extensive AI/nightly qualification. |

## State and compatibility invariants

`GameOptionsClass::garrisoningAndCapturing` is the single city-zone permission. OFF is authoritative for every human and AI, including explicit config requests and legacy capture execution. ON also selects the six AI garrison/capture behaviours. The old `zoneGarrisons` and `aiInfantryTactics` fields are removed, not retained as alternate live settings.

UI, hotkey/cursor handling, AI admission and actual infantry/zone execution enforce the same permission. SquadOrders, InfantryLanes and InfantryInMix do not consult it. Quiet human-shared garrisons remain protected by actual HumanPlayer membership, not the legacy House::isAI flag.

Protocol 152 already shipped on the Air. The corrected rule therefore uses a new compatibility version (Integrator tentatively reserves **157**). The existing MOD18 bit5 remains the zone permission; the shipped MOD19 bit1 is retired but safely ignored while decoding Technology Installments from bit0. No settings layout extension or reinterpretation of a legacy value 2/3 as a whole-byte bool is allowed. Save9906 and its economy counters are preserved; the final protocol remains the Integrator's monotonic choice.

The new INI key is **Garrisoning and Capturing**. A one-time profile migration preserves old Zone Garrisons permission where the new key is absent, removes both obsolete keys, and thereafter reads only the new key.

## Test design and expectation changes

- The real Options → Advanced → Default Game Rules path opens, renders, toggles, applies, persists and reopens the rule. Its saved/multiplayer settings feed the actual game.
- AI ON uses all six raw zone flags OFF; AI OFF deliberately uses all six ON. Real AI orders and completed entries are counted only inside the corresponding arm. This is a permission-wide comparison, not an isolated effectiveness claim for each of the six tactics.
- A real shared-house human uses Capture and map clicks on both friendly and enemy zones. OFF checks the single/multi controls and C shortcut; the unit probe additionally attacks direct/network/queued/old CAPTURE and damaged-zone arrival bypasses.
- Vanilla runs an actual human capture of a damaged ordinary building with the stored city preference both OFF and ON; neither value may remove vanilla's Capture command.
- Four real-startup profiles cover legacy OFF, legacy ON with AI-only OFF, conflicting new-key precedence, and AI-only OFF without an old zone permission. They also check inactive mixed-case mod sections, key retirement and preservation of unrelated sections.
- Synthetic unit-only garrison fixtures switch actual QuantBot controllers to `AIRole::Off`, not the global permission: human ON cases and core unit/weapon simulation must still run. The separate shared-house safety arms retain an active fighter QuantBot and prove both human roster orders keep two occupants while AI-only releases them.
- Existing `garrison_button_rule_off` now requires Capture hidden **and disabled**, rather than visible with a non-garrison tooltip. Baseline/control harnesses pin the unified permission OFF; garrisoned Saboteur cases explicitly pin it ON. No existing threshold or positive-path assertion is loosened.


## Corrected-rule results

**Qualified source:** `414d3c73c512b96dd76ed8f3b0a1268d2430edf8`, AWS-mel2 job `20261128T173406-garrison-capture-cutover-v1-2b9cb2`: **6/6 passed**, 2m02s submission-to-result. The last test ended at 2026-11-28 17:36:07 AEDT and its successful receipt was collected at 17:36:08.

| Check | Observed result |
|---|---|
| `garrison_capture_settings` | 7,899 assertions in 2 cases; settings/hash/following-record round trips, permission bit5, legacy MOD19 bytes0/1/2/3 with both old extensions |
| Real AI, raw zone flags opposed to permission | ON (all raw0): 3 orders, 3 completed entries, enemy zone taken (`captured=1`); OFF (all raw1): 0 orders, 0 entries, 0 captures. Each arm ran3,750 cycles |
| Real human Capture | ON: 2 infantry in the friendly zone, 2 in the enemy zone and ownership changed to the human; OFF: 0 in either and enemy owner retained, after actual single/multi widget and C/map paths |
| Shared-house safety | Active AI-only2→0; active Human-first2→2 and Human-last2→2, both shared houses reporting engine_ai=1 over3,751 cycles |
| Vanilla | Actual damaged-building Capture succeeded with each stored city preference0/1 (320 cycles each); city control hidden, zone authority false |
| Startup migration | All4 real-startup profiles passed new-key precedence, old-zone permission retention, AI-only retirement, mixed-case inactive mod and unrelated-section checks |
| Existing positive/negative paths | `zone_garrison_probe`: all33 cases, including health/regeneration/redevelopment/save coverage; `zone_garrison_panel_probe`, `saboteur_zone_probe` and `infantry_squad_probe` all passed |

No corrected-rule targeted failure occurred. The green source was handed to the Integrator immediately for the Air playtest-first landing; its final monotonic protocol, actual installation, four AI gates and broad `-L nightly -LE gui` qualification belong to that integration lane and are **not claimed passed here**.

**Unverified manually:** the menu's appearance on a real screen and a live old-save playthrough. The headless probe did render and bound-check the real640×480 menu and wrapped explanation; legacy settings bytes were tested, not a human's whole saved session.


## Superseded AI-only implementation evidence

### Latest human-safe green source

- **Test commit:** `453629d86acbd123235911e4ec3ae29dffb2a57c`; test-snapshot parent `a0ed4bc273125782dab2dcf1cea33934e6f90e19`.
- **AWS job:** `20261128T162634-ai-infantry-human-safe-v5-667861`, **3/3 PASS**; green result collected **2026-11-28 16:27:22 AEDT**.
- The real option/gameplay probe passed in **16.4 s**. Settings passed **576 assertions**. The full logic binary passed **1,519 cases and 15,035,906 assertions**, with two existing opt-in asset/network cases skipped.
- The own option patch is relative to green Infantry `e1f7157563cdf47123fc71fcae7ccd439c308640`; no unaccepted EconomyFirst hunks were imported. Staging was never edited during the safety follow-up.

| Acceptance | Observed result |
| --- | --- |
| Real Options/Game Rules path, label, wrapped explanation and bounds | PASS at 640×480 on the headless software renderer, with actual checkbox/OK/APPLY clicks |
| Default on in Dune City; absent/inert in vanilla | `default_city_on=1`; `vanilla_hidden=1`, vanilla effective option `0` even with the stored preference on |
| INI and reopen | Both off and on persisted in the mod's player-options section and reopened with the selected value |
| Saved/multiplayer settings | Both states, installments, three tech levels and the following record preserved; 576 settings assertions passed |
| AI on, all nine config switches off | In 3,750 cycles (60 game seconds), **3 orders, 3 completed entries, one enemy zone captured** |
| AI off, same bundled map/seed and config | **0 orders, 0 entries, 0 occupied/captured zones** |
| Human first, QuantBot second in an ordinary shared house | Two soldiers entered through the real Capture button/map click and remained **2→2** for 60 seconds; legacy `engine_ai=1` throughout the case |
| QuantBot first, Human last in the shared house | Same real player path and **2→2** retention; legacy `engine_ai=1` |
| AI-only quiet-zone control | **2→0** release, proving the human guard does not suppress normal AI release |
| Non-vacuous shared-house check | The owning QuantBot was asserted initialized and active; no direct local-player reassignment or Capture bypass |

### Qualification history and corrected new-test expectations

The initial job `20261128T155335-ai-infantry-options-own-247d66`, source `f3de0ad4032989aee14685d7d742030375a76bc4`, failed all three registrations because the new tests incorrectly expected **City Effects** to serialize. This existing mod-derived field is absent from GameInitSettings, as already documented in `degradation-probe.inc`. The correction retains full equality/hash assertions for every persisted field and directly checks the new option. No existing assertion or threshold was loosened.

Corrected combined source `0d4bb0e50de6b95b1049a688d36bd3cfbecdcae7` passed 3/3 in `20261128T155915-ai-infantry-options-own-v2-39e851`. Independent source `7bf4e245612220e0f18f2d5e421acf6658d3a37b`, excluding all EconomyFirst changes, then passed 3/3 in `20261128T160349-ai-infantry-options-independent-be35b6` at **16:05:08 AEDT**.

GarrisonKeepsZone subsequently identified the stale shared-house AI-flag regression. Safety candidate `db8fe386ba60721d4ab7dd2bd7481e59229d2ebb` passed settings/logic but its new UI fixture failed in `20261128T162017-ai-infantry-human-safe-f828e9`: a second offline human took local control because the map loader adopts each offline HumanPlayer in roster order. The corrected fixture gives the shared house the sole local human and uses a real switched-off QuantBot opponent to keep the zone quiet/non-front. Production release logic was unchanged between these safety candidates; the latest job above passed every real-player assertion.

### Integration handoff and remaining coverage

Per Stefan/Main's playtest-first flow, the green source was handed immediately to **Integrator** for landing and Air deployment. Artifacts in `projects/outputs/ai-infantry-tactics-option-20261128/`:

- `option-own-infantry-only-v5-human-safe.patch`: complete own-only option patch against green Infantry.
- `option-shared-human-followup-v5.patch`: three-file follow-up after the original option-v3 patch.
- `option-handover-evidence-human-safe.patch`: final evidence update, superseding the earlier handover-evidence patch.

Integrator owns the subsequent four AI gates and AWS nightly (`-L nightly -LE gui`) on the landed source; those results are **not claimed here**. Real-window appearance/input on a human screen and the installed Air build remain unverified here. The option probe observes representative garrison/capture activation, not a separate activation measurement for every one of the nine underlying behaviours.

## Captured tax and enemy-garrison capture follow-up

Stefan: “ensure captured buildings generate tax for the side that captured them”; “if a building is already garrisoned by the enemy then it cant be captured it can only be attacked”.

| Property | Acceptance evidence |
|---|---|
| Live holder receives the full R/C/I tax | Human and actual QuantBot captures; normal game budget ticks increase the holder's exact fixed-point city credits and gross receipts by the zone's complete calculated tax |
| No duplicate or former-owner tax | Every sampled tick conserves one payout: former owner and unrelated house get zero; per-house budget/tax-base figures agree |
| Tax follows later ownership | Sample original ownership, capture, real evacuation/release and retake through the normal command path |
| Military capture levy is not another tax | `ZoneCaptureLevy` recruits/orders Soldiers and spends their price; the same exact one-payout assertion applies with the city tactics enabled, without adding another credit path |
| An enemy garrison prevents Capture | Healthy/red-health, direct/network/queued/current Capture, Hunt and legacy entry paths refuse another house's occupants; the real Capture cursor/message explains it |
| Attacking and later capture still work | A deliberate Attack has a real effect; after death/evacuation empties the garrison, ordinary Capture succeeds |

Tax observations use ordinary storage capacity, a nonzero live tax base/land value, and no other taxable structures or police costs. They test actual credits and receipts, not just a cached UI figure. Existing city tax storage/credit limits are not bypassed.

Initial source trace: `ZoneStructure::transferOwnership` changes the live structure owner; `CitySimulation::advancePhase` dispatches `runDailyBudget`, whose structure walk selects that live owner and credits it once. `House::addCityTaxReceipts` is a statistic, while `addCityCredits` changes spendable tax money. The budget probe must establish the runtime result before any claimed tax fix.

### Existing-tax baseline evidence

Unchanged production tax accounting passed `captured_zone_tax_probe` at `74a40d739364698a407c2a18147c8d60d38aea7d`, AWS job `20261128T181931-captured-tax-baseline-v2-569692` (20.65s test time). All six real human/QuantBot R/C/I cases passed original → captured → released → retaken, 16 actual budget ticks per phase: 384 sampled single payouts, with full exact fixed-point credits and receipts to the live holder and zero to the other houses. Example R/C: original ownership paid annual107 (980397872 raw credit units per16 ticks); captured ownership paid annual106 (971235264 raw), with zero to the other house in either phase. Industrial likewise followed each holder's own current tax/land-value calculation (28 versus43 annual). No production tax-credit path was added, avoiding double counting.

The first baseline snapshot `95583af0556af07a67362e6a68c5fcf0e31952e1`, job `20261128T181428-captured-tax-baseline-v1-ac8f2a`, failed only native-probe compilation: the fixture used an invented attack-mode command identifier. Replacing it with existing `CMD_UNIT_SETMODE` retained every accounting assertion; no income observation is claimed from that failed compile.

The capture-only registration covers native attack/Hunt behavior alongside occupied-garrison authority; it does not register or claim the separate Trooper-rush controller's qualification.

### Occupied-target qualification and measurement

- Combined candidate `26b531f8dbbd1d51f48baa4353d02f1af05230f1`, AWS job
  `20261128T183525-captured-tax-and-lock-v1-a18d7a`, passed 5/7 selected tests.
  `occupied_zone_capture_probe` exposed an unconditional cursor reset on a refused Capture:
  no responder accepted the order, yet the cursor closed. The reset now belongs only to the
  accepted-responder branch; the refusal assertions are unchanged.
- `infantry_zone_capture_probe` initially stopped after its first delivered squad child's
  target lost 1 HP. That observer measured all target HP decline, not infantry-inflicted
  damage. The native power-shortage timer also reduces structure HP, independently of combat.
  The corrected observer requires complete attacker/target combat telemetry, zero hit and
  signed-damage deltas for the whole attacking house/type cohort, and exact equality between
  gross target HP decline and independently witnessed native power-timer loss. It never
  subtracts an assumed background loss. All three delivered children are attempted.
- The existing assault case now first proves Capture refusal, then uses explicit Attack to
  clear the occupied lot, defeats the evacuee, repairs the lot, and issues a fresh Capture.
  This replaces the retired Capture-as-Assault expectation, not a failed capture assertion.
- Rebasing retains the integrated shelter immunity, exact 30% relocation rule and friendly
  automatic relocation. Such relocation never transfers ownership and is not a player Capture.
- The targeted cursor candidate `14cfcd68d2047649822856bc202ec5fb88c92cc5`
  (`20261128T185259-captured-tax-and-lock-ui-v3-27b7a1`) passed the tax probe and the
  occupied probe's healthy, red-health and existing-order cases. The next legacy fixture
  reported its Soldier lost while waiting for an interior position that ON-rule hostile
  Follow rejects. Its replacement separately asserts that ON rejection, then drives the
  unchanged exact `0.5`-tile legacy-arrival assertion with permission revoked on the already
  occupied zone. Soldier survival, object identity, ownership and foreign occupation remain
  mandatory in both healthy and red cases; no damage or capture assertion is waived.
- Candidate v3 was cancelled with the broad garrison probe unrun; v4 and v5 were cancelled
  before their test registries were listed. None is reported as a successful qualification.
- The final overlay rebases onto landed Rush/identity source
  `b16f24dbfd1aafdc7809448cba4ca62dd24a8580`, retaining its current shared runner and Rush
  implementation, including the 16-order, near-4/far-12 and protected-5/5/4/3 assertions.
- Candidate `cf4ce7b1e04d4c21fdab45518aa86150399f5772`, job
  `20261128T194445-captured-tax-and-lock-v6-377543`, finished 6/9: tax, all seven occupied-target
  cases, settings/menu option, RTS controls, identity and combat stats passed. The full garrison
  probe passed 50/52 cases. Its friendly-crew owner reinforcement exposed an over-broad admission
  predicate; all map/cursor/command paths now share the corrected occupant-scanning predicate.
  The other case still required Capture-as-Assault and is migrated under Main's explicit approval.
- Stefan's rule is quoted in that case: "if a building is already garrisoned by the enemy then it
  cant be captured it can only be attacked". It now requires both live Capture cancellations and
  a separately issued Attack with actual Trooper damage credit, preserving the takeover/defender
  activation preconditions and verifying no Attack ownership transfer.
- The Native Capture and Rush tests in v6 stopped at a shared observer-only compile error:
  `const UnitBase::getOwner()` returns `const House*`, but the new observer member was `House*`.
  Its member is now const-correct. Neither Native Capture nor Rush runtime is claimed from v6.
- Mixed friendly crews are checked member-by-member, not only by their first house. A new occupied
  case requires real allied relocation and owner reinforcement, then refuses an ownership takeover
  whose first crew matches the proposed owner but a later crew does not. The final ownership-transfer
  boundary uses the same all-occupants requirement; ordinary allied shelter and own reinforcement
  remain non-capturing operations.

- Candidate `879c0acb446063e8d8edf33940c9bcc23adb0d3d`, job
  `20261128T200905-captured-tax-and-lock-v7-25c080`, passed 8/9, including the full garrison
  probe, mixed-crew authority, Native Capture and preserved Rush. Its sole failure was the old
  option probe rejecting the newly landed `ScrollView` container. That probe now uses the
  established real wheel/window-click routing and scroll-offset bounds calculation, retaining
  all viewport, label, tooltip, persistence, permission and activation assertions.

## Game Rules minimum-viewport layout repair

The added city rule exposed a pre-existing fixed-height limit in the two-column
form. Its rule columns now use the existing `ScrollView` widget, preserving
checkbox sizes and both original columns. The caption and OK/Remember controls
remain fixed, while mouse-wheel/scrollbar input reaches rules below the viewport.
Future rows such as All Units for All Houses do not require a taller window.
No match-rule semantics, protocol or save version changes.

The original `city_zone_power` and `research_lab_probe` fit assertions remain
unchanged. `city_zone_power` additionally exercises every actual checkbox through
the window's real click/wheel routing, checks that the last controls can be fully
revealed, and saves offscreen top/bottom PNGs for DuneCity/Tornie in all three UI
skins at 640x480 and Air 960x600 logical at 2x/3x physical scale. The research UI
bounds helper now accounts for the viewport's actual scroll offset.
Real-screen/touch scrolling remains unverified.

## Captured tax and occupied-capture targeted green

- Tested commit: `a4b59dcbeda3f0b115337f73740addcc3acaac16` (parent
  `4bd6f788cebd5a24cbb5c5ba3eb44ef5199b66d8`).
- AWS job: `20261128T202002-captured-tax-and-lock-v8-255d4e`; terminal report read at
  **2026-11-28 20:23:15 AEDT**. **9/9 passed**, 2m47s submission-to-completion:
  `rts_controls_tests`, `captured_zone_tax_probe`, `occupied_zone_capture_probe`,
  `zone_garrison_identity_probe`, `garrison_capture_option_probe`,
  `zone_garrison_stats_probe`, `zone_garrison_probe`, `infantry_zone_capture_probe`,
  `zone_trooper_rush_probe`.
- Tax: all six human/QuantBot R/C/I cases completed original/captured/released/retaken windows,
  16 real budget ticks each: **384 exact conserved payouts** to the current holder, zero to the
  other houses. Existing production tax accounting was already correct; no duplicate payout
  implementation was added.
- Permission/menu: default city ON, INI/settings retention and vanilla-hidden assertions passed.
  With configured tactics OFF and the permission ON, AI occupied **5** and captured **1**;
  with configured tactics ON and permission OFF, both counts were **0**. Human ON occupied
  own/enemy zones and captured; OFF did neither. The checkbox was fully visible at
  `(31,362,314,24)` without needing a wheel event; checkbox and OK used real window input.
- Capture authority: all **8** occupied-target cases and all **52** full garrison cases passed,
  including mixed allied crews, actual owner reinforcement, live Capture cancellation and a
  separately issued Attack with real attacker damage credit.
- Native capture: **17/17 eligible captures**, **13** properly excluded engagements and **0**
  infantry-inflicted damage on eligible captures; all three genuinely purchased squad children
  completed physical entry. The independent power-timer witness accounts for background loss.
  Preserved Rush: **16/16 entries**, all 16 orders within 1s, **4** near entries within 20s and
  **12** far deviations, with protected-role assertions retained.
- Stefan: "if a building is already garrisoned by the enemy then it cant be captured it can only
  be attacked". The ownership-change regression quotes this rule and tests the corresponding
  live cancellation plus credited explicit Attack, not the retired Capture-as-Assault behavior.
- This is the bounded targeted green, not a claim for the four AI gates or broad nightly on
  this SHA. Integration/qualification owns those subsequent checks. Real-screen menu appearance
  and the installed Air build remain unverified by this worker. No new save state was introduced;
  the integrator owns any protocol allocation.
