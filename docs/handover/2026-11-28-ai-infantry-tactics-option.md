# AI infantry tactics option — 2026-11-28

## Request and acceptance (recorded before implementation)

Stefan: “i just want it as a feature, not a mod” and “you can just have it as an option to be enabled from the menu that is on by default for dunecity not a different mod”.

| Requested outcome | Observable acceptance assertion |
| --- | --- |
| One normal game option, no separate mod | The real Options → Game Rules window shows **AI infantry tactics** beside the Dune City rules, with a tooltip describing garrisons, zone capture, reinforcement, squads and infantry production. |
| Default on for Dune City only | A newly selected city game's effective option is on; a vanilla game's effective option is off and the city-only control is not offered. |
| Persistent choice | Clicking the real rule checkbox and accepting Options writes `AI Infantry Tactics` in the player's per-content Game Options section (`Game Options dunecity` for Dune City); reopening/restoring game options preserves the choice. |
| Settings/multiplayer parity | Both states round-trip through GameInitSettings serialization; equality and the options hash distinguish effective city ON/OFF, and the network protocol is advanced with the new packed flag. |
| Real AI activation on | A headless real-menu probe starts a city game with all nine QuantBot config switches off and this option on, then asserts more than zero real garrison/capture or squad events and their proximate effect. |
| No bundled activation off | The paired probe turns this option off through the same menu path and asserts zero bundled garrison/capture/squad events. Config switches remain available for explicit independent experiments. |
| Existing controls retain meaning | AI control/default fixtures explicitly set the new option off. Existing assertion thresholds are unchanged. |
| Preserve a human's garrison when sharing a house with QuantBot | With the option on, two soldiers put into a quiet non-front zone through the real Capture button and map click remain **2→2** for 60 game seconds, with Human-first and Human-last controller ordering. The same AI-only quiet zone still releases **2→0**; the existing option-off arm remains inactive. |
| Tested immutable source | An off-branch test commit passes the affected settings/menu tests, four AI gates, and AWS nightly (`-L nightly -LE gui`); report all failures and their causes without suppressing them. |

## State and invariants

The persisted choice belongs to GameOptionsClass. The match's city flag determines whether that choice is effective. Each requested QuantBot switch is enabled iff its original config switch is enabled OR the city match's option is enabled; turning the menu option off must not disable a deliberately enabled experimental config switch. Menu acceptance, INI reload, multiplayer serialization and save/load must preserve the same choice without modifying shared global QuantBot defaults.

## Implementation

- **Options → Advanced → Default Game Rules → Change… → AI infantry tactics**, also the normal match Game Rules dialog. The wrapped explanation lists garrisons/reinforcement, capture raids and chains, squads, infantry production lanes and army composition.
- `GameOptionsClass::aiInfantryTactics` stores the default-on preference. The control is offered only by city-mode content; `QuantBot::aiInfantryTacticsEnabled` uses the real game's city-mode flag (not the independent City Effects rule). Vanilla does not offer the control or gain any tactics from it.
- Every requested config switch is ORed with the city-match option at its existing execution gate. No shared QuantBot config is mutated or copied. Explicit config experiments still work with the menu option off.
- INI key **AI Infantry Tactics** follows the existing option persistence paths, including `[Game Options dunecity]` for the player's Dune City choices and mod `GameOptions.ini` overrides.
- Protocol **152**: bit 1 of the existing MOD19 installments byte is set when tactics is **off**; bit 0 remains Technology Installments. The MOD20 tech-level byte is already full. No settings/save layout extension; older bytes inherit the new default. Equality includes the choice and the options hash spells out only its off state.
- Existing control INIs and AI/related gameplay probes explicitly pin the bundle off. `vanilla-match.py` defaults it off only when an input INI does not explicitly choose it. No existing assertion threshold is weakened.
- New checks: `ai_infantry_tactics_settings` (both states, city/control flags, installments, tech levels, following-record integrity and old-byte defaults) and `ai_infantry_tactics_option_probe` (real menu clicks, rendered explanation/bounds, INI/reopen, actual QuantBot garrison orders and entries on a bundled map, paired off arm, vanilla exclusion, and real-player shared-house garrison retention in both controller orders).
- A human-shared house's quiet garrisons are protected by actual `HumanPlayer` membership, not `House::isAI()`: ordinary shared-house player insertion can leave that legacy flag true. Only the owning house's controllers are checked; AI-only houses retain their release behaviour. No new stored state or protocol change.


## Results

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
