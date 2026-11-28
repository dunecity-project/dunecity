# Garrisoned lots: stable buildings, shelter and safe relocation

Stefan, 2026-11-28: "there's an issue with garrisoned buildings. they change type and the infantry disappear. dont let a garrisoned building change type."

## Current rules and fix

- An occupied lot cannot grow, decline, become vacant, or gain/lose its Hospital/Church designation. The mutation source is gated, not rolled back afterward; population, supply, tax and traffic accounting remain active. The lot becomes eligible again after its occupants leave.
- Five infantry can enter; a sixth is refused. The player policy, saved arrays, defensive/reinforcement AI and panel share that capacity. Front-line deployment quotas of two are strategy, not a capacity limit.
- Occupants take **zero damage from hits on the building**. This replaces the interim 10% rule.
- Every living occupant heals continuously while inside, independent of building repair. No generic passive Infantry/Trooper regeneration existed. `[Map Settings] GarrisonHealTime = 60` restores a full maximum-health bar from zero in **60 seconds of normal game time**. Fractional health and saved division remainders make the rate deterministic, exact and capped; leaving stops healing.
- Strictly below **30%** building HP, each crossing attempts the nearest reachable same-owner/friendly building with room, counting in-flight reservations. Occupants deploy, walk and enter; no teleport or ownership theft occurs. Without a destination they stay inside. Another attempt requires recovery to at least 30% and a later drop. Human and AI occupants use the same path.
- Destruction frees the footprint, deploys occupants, then applies `ceil(maxHP * GarrisonCollapseDamagePercent / 100)` through ordinary on-map unit damage. The tunable ObjectData value defaults to **50**. There is no minimum-one-HP clamp or invented blast radius. Wounded occupants can die; their normal map death animation and an owner-local **Garrison casualties: N** urgent message make the loss visible.
- Partial AI release can retain live Soldiers preferentially, then other live occupants in entry order, without momentarily returning a captured building. Dead IDs do not consume the retained quota; destruction ignores it.
- Save **9907** / protocol **160** carries healing-only state, collapse tuning, the crossing latch and transit intent. Older damage remainders are consumed and discarded. Relevant state and effective ObjectData values participate in deterministic multiplayer hashing. No new feature switch is introduced by this fix.

Safety invariant: each living occupant is either hidden in one registered garrison or visibly active in transit. Collapse deaths use normal unit damage and removal, never an unreported hidden deletion.

## Root causes

- `src/dunecity/CityEffectsRuntime.cpp:1304,1381,1434` (original lines): growth, demand decline and power-loss vacancy mutated occupied lots. Civic census at `1658-1670` could also designate/remove Hospitals and Churches. These mutations were real, but the controlled reproduction conserved both infantry.
- `src/structures/ZoneStructureGarrison.cpp:225-233` (original): half of every raw incoming hit, rounded up, hurt the front occupant before building damage. A one-HP chip therefore cost a full hidden infantry HP; repair could leave the building nearly pristine while its occupants died.
- `src/Bullet.cpp:757-763`: a ShellSmall projectile moved only 20 pixels before ground collision. The old edge-tile-centre garrison muzzle could still be inside its own structure. GarrisonStats reproduced self-fire and moved the muzzle beyond the footprint edge; the real firing occupant retains kill/damage attribution.
- Loaded civic overlays were restored too late, after reconciliation could overwrite the saved Hospital/Church roster. Restoration now precedes census. Reconciliation also ran an extra growth tick, declining an unpowered saved lot from population 24 to 16; it now rebuilds derived population/tax/civic census without advancing a city day, demand history or overlay pacing.
- Bundled scenario lots skipped city-tile initialization before the city simulator existed. Placement now initializes those tags, avoiding a pre-existing Dune/city footprint mismatch.
- Spice-bloom egress could resurrect a killed occupant and leave it hidden. Dead occupants now retain lethal health and complete normal visible removal.
- A stale Follow arrival after ownership changed could enter legacy direct zone capture/deletion. Occupied-zone/footprint guards prevent that path. Explicit Capture remains an assault after an enemy takeover rather than being mistaken for ordinary Follow.

## Before/after evidence

- Assertions-only baseline `3ba4c20907f9f80abdb6e351a49fe57c8c7b7cb1`, AWS `20261128T154842-garrison-keeps-zone-before-6f77d8`, reproduced occupied residential density **2→0** and population **24→0**, with tracked living occupants **2→2**. Commercial/industrial and civic mutations also activated. This is not evidence that a type change alone deletes infantry.
- The Air's copied log records two Troopers entering zone 6222 at cycles 30346/30387 and `killed_out` at 31543 with **199/200 HP**; zone 7153 similarly lost two at 52040 with **132/200 HP**. These are recorded deaths, not an established city-sync recreation.
- Old-rule arithmetic for two full 45-HP Troopers and repeated one-HP hits, with the building repaired between hits: first death at **45**, both dead by **90** [INFERENCE]. Under the final immunity rule, the observed **450 hits** leave **2→2** occupants with zero HP lost. No number of non-collapse building hits can kill sheltered occupants.
- A real 40-HP building loss and ten separate one-HP losses each leave both occupants unchanged. A 200-HP building reduced to **1 HP** still holds **2→2** living occupants.
- At 30 seconds, a Soldier starting at 1 HP reaches **11 HP**, and a Trooper reaches **23.5 HP**. Tests verify saturation, no healing after exit, independent damaged-building healing and a 30-second tuned rate.
- Collapse with an odd 91-HP maximum applies **46** damage: the wounded occupant dies normally, the full-health one survives at **45 HP**. A standing-unit control receives the same hit; both death animations and the friendly-building victim-owner ticker activate.
- Bloom egress before the fix left **2 living / 0 deaths / 1 hidden** after a lethal bloom. Afterward it leaves **1 living / 1 normal death / 0 hidden**.
- Five real Capture entries succeed; the sixth remains outside. Type/density/civic controls change again after real Evacuate. Both demolition commands eject **2→2**, visibly active and injured.
- The independent directional firing fixture records **21 real kills**, correct live statistics and **zero source-building HP lost**.

## Qualification receipts

- Final shelter qualification: `d9f3544aa9792b9da5723c4b1ec18318c692c108`, AWS `20261128T184029-garrison-shelter-160-v4-a6f95d`: **4/4 passed**. Main probe **52/52 cases**, 102.8 seconds; panel 21.8 seconds; statistics 43.7 seconds; Saboteur-zone 17.2 seconds. Green was observed and handed to Integrator at **2026-11-28 18:44:14 AEDT**, before starting broader coverage.
- Both Hospital and Church real Save-dialog round trips preserve **2→2**, exact fractional healing, unchanged population/tax census and both footprints. They cover both saved crossing-latch values. The cancellation case executes the real four-argument forced Move command and proves superseding orders revoke automatic ally-entry permission.
- Earlier previews exposed the extra load-time growth tick and invalid fixture setups. Repairs preserve every count/health assertion and the 45-second walking bound: compact journeys respect Soldier speed, active AI faces a real enemy front instead of legitimately recycling quiet-rear troops, and the failed-attempt save fixture has no healthy alternate shelter. Exceptions now print their reason rather than silently incrementing the failure count.
- Integrator landed the green source as `d6e273c9a38ee99db7d47123da09bcc9857785d8` and triggered automatic Air delivery without waiting for broad coverage. Installation/playtest is not independently verified here.
- Additional settings coverage: AWS `20261128T184442-garrison-shelter-capture-settings-2d1f42`, **1/1 passed**, `garrison_capture_settings`, at the same qualified source SHA.
- Historical interim core `c98b65daa3f4ff67c7e7c8fab46db887bff5944a`, AWS `20261128T164807-garrison-core-assault-command-6f0eca`: 2/2 passed, including all 33 then-current cases, and was handed to Integrator at 16:50:54 AEDT. That receipt qualifies the superseded 10% rule, not the final immunity/relocation rule.
- Independent muzzle/statistics `ad8beab4c57a58743e609b8ba2fabaa578137b97`, AWS `20261128T175506-garrison-stats-current-e0a409`: 2/2 passed before integration as protocol 159.
- Private Air save inspection `df50be0f065c54e6b199af7d26752c97242d8fd4`, AWS `20261128T165347-private-air-garrison-save-inspection-5c8f80`: passed in 37 seconds. **It contains personal saves and must not be landed or pushed.** Exact pinned Workshop assets matched the committed compatibility fixtures.

## Extended nightly coverage

AWS `20261128T184442-garrison-shelter-nightly-fc4f88` ran the requested **`-L nightly -LE gui`** at the qualified D9 source: **276/291 passed**, 15 failed. This is standalone D9 evidence, **not qualification of the later combined d6/B build**.

All nine garrison/capture/Saboteur registrations passed: the 52-case probe, occupant panel, live statistics, settings, master-option probe, natural real-game acceptance, and Saboteur zone/target/effect probes. Natural real-game acceptance passed in 406.1 seconds; the three Saboteur probes passed in 18.5/33.1/213.7 seconds. The original strict assertions remain.

The initial 15 failures were triaged without changing thresholds:

| Family | Count | Cause / owner |
| --- | ---: | --- |
| Formation, deviation and two Civ Wars layouts | 4 | Synthetic older records retained the 9907 four-byte relocation tail. Fix `21363e185290e00725c2f016ddfaf855c7dc63c0`, AWS `20261128T191154-garrison-9907-legacy-fixtures-3ee7ee`: **4/4 passed**, with exact-EOF/state assertions unchanged |
| Cross-platform city golden | 1 | All three native platforms agree below. Main-approved pin `2db18612e286b0dd75c842e84a8eb1c640a1ad45`, AWS `20261128T192111-garrison-native-digest-pin-f7051a`: **1/1 passed** |
| QuantBot force/strategy historical records | 2 | Synthetic 9863/9893 records retained EconomyFirst's 9906 tail; QualTriage's `be6a02dc0f729a48554d9dd4de86782988ed390f` qualifies 2/2 separately |
| Game Rules overflow | 2 | Earlier fixed-height layout defect, independently bounded before the occupied-lot guard; GarrisonPanel owns scrolling and unchanged bounds assertions |
| City-redevelopment lane Parks | 1 | Independently fails before the occupied-lot guard; QualTriage owns the earlier regression |
| Pollution relocation | 1 | Lower pollution was accepted despite clamped, unchanged land value; QualTriage owns clamp-aware selection |
| Strategic-site negative control | 1 | Positive guarded arm passes; legacy arm reaches a credits-cap failure instead of the exact idle-yard failure required by the control; QualTriage owns it |
| Economy variants B/C/E | 3 | Experimental economy variants; EconomyFirst owns their analysis |

The complete `research_lab_probe` also passes after historical full-save projection is corrected: `fc2c613af67021a5c0d2a328155f1bcf063fc33e`, AWS `20261128T193047-garrison-research-legacy-layout-ff1ab0`, **1/1 passed**, 57.9 seconds. The synthetic 9882/9883 copies remove the current ObjectData healing/collapse suffix, UnitBase relocation field and newer Palace-worm suffix before the unchanged historical assertions run. This qualification includes GarrisonPanel's approved minimal scrolling prerequisite; the IO-only delta is kept separate from its UI ownership. No narrowed probe mode, production rule or save-version change is introduced by these fixture repairs.


## Native cross-platform digest

Main approved a golden update only after every covered platform agrees. The exact D9 source, seed 9101, 25-minute city case finishes at cycle **93750** with digest **`3b3265b11dc676e5`** on all three native platforms:

| Platform | Measurement job | Final digest |
| --- | --- | --- |
| Linux / AWS | `20261128T184442-garrison-shelter-nightly-fc4f88` | `3b3265b11dc676e5` |
| macOS / Mini | `20261128T190739-garrison-city-digest-mac-68c91e` | `3b3265b11dc676e5` |
| Native Windows / PC | `20261128T190739-garrison-city-digest-windows-07c4df` | `3b3265b11dc676e5` |

Those measurement CTests correctly failed the obsolete **`070bbafc6799818b`** golden; they are not reported as passing tests. No cross-platform divergence was observed. The replacement pin explicitly cites the occupied-zone/shelter rule source `d9f3544aa9792b9da5723c4b1ec18318c692c108`.

## Read-only Air inspection and limits

No game was launched, stopped, or changed on the Air. Copied log SHA-256: `4a3432cc8077ee46c500ca9197eb259c29067ed18c48cb399428cd125647fb46`.

| Save | Cycle | Held zones / occupants | Units / structures |
| --- | ---: | ---: | ---: |
| `drillturrethole.dls` | 450872 | 0 / 0 | 433 / 4919 |
| `moremaginot.dls` | 356134 | 0 / 0 | 518 / 4923 |
| `seewhatiddid.dls` | 270409 | 0 / 0 | 913 / 4736 |

Counts were unchanged after production save/load. Those zero-activation saves cannot qualify occupied-lot preservation or identify Stefan's exact observed lot. No autosave/quick-save path was found in the inspected input/save code; the constructed regression uses the real manual Save dialog. Physical Air UI/readability and the exact Air lot/type-change correlation remain unverified.

## Turret-drill saved-game comparison isolation

QualTriage bounded the saved `turret_drill_save` comparison change to the first garrison-health/capacity/type-lock patch, `d2ced4dcc151bff1c05a08e9874473be9abcbba1`. Its immediate parent `d3532aab32d882b1d71a6409bcdc5d59bfb68221` passed (`20261128T201517-qual-turret-garrison-parent-7e271e`); D2 failed identically on two Linux hosts (`20261128T201517-qual-turret-garrison-70ebd1`). Seven of the original eight turret assertions still passed, including 305k versus 219k Rocket Turret damage; only Ordos structures lost versus the control inverted, 282 versus 298.

Retained native logs show the new garrison treatment was active, not an unused rule:

| Ordos continuation | Parent entries / garrison wipes | D2 entries / garrison wipes |
| --- | ---: | ---: |
| Turret drill | 431 / 47 | 597 / 8 |
| Control | 407 / 49 | 689 / 4 |

Control-arm Trooper losses fell from 542 to 491, Soldier losses from 249 to 230. Both versions loaded identical Ordos population R/C/I 15474/952/955, 221 combat units, 4490 credits and 603 lots. Thus the continuation now compares materially different garrison survival/capture dynamics as well as turret construction. The garrison protection, healing, capacity and occupied-type lock are intentional player rules and are not rolled back to satisfy the old turret comparison.

The turret fixture now freezes the existing public **Garrisoning and Capturing** option off in **both** arms, in the same checksummed-save input. The saved-world audit forwards that explicit `[Game Options]` section through the existing frozen-option interface. The native harness parses it once and reapplies only its declared keys after restoring the save, before game cycles and AI styles; otherwise loading replaces the earlier default/control options with the saved setup.

The audit reports the actual engine rule plus held-zone/occupant counts for every house at load, periodic snapshots and the final audit. A ninth, additional acceptance line requires the rule off, no occupied zones/units and no garrison-entry events in either arm. **All original eight assertions, the 23-game-minute horizon, absolute thresholds and drill-versus-control comparisons remain unchanged.** Production garrison behavior, default enablement, type locks, save format and protocol are untouched.

| Guarantee | Enforcement / evidence |
| --- | --- |
| The turret treatment is the only arm difference | Both arms consume the same frozen save and explicit garrison-off rule; only house 0's turret-drill style differs |
| The pin reaches the restored engine, not merely process settings | Post-load frozen-option application and actual per-house JSON rule/count fields |
| No garrison confound is hidden between snapshots | The additional acceptance line also requires zero native garrison-entry events over the entire continuation |
| Turrets still produce the requested outcome | Every original mass, range, engagement, damage and enemy-loss assertion remains in place with its original bound |
| Stefan's gameplay fix remains active outside this fixture | No shipped source or default rule changes; normal garrison/master-option probes accompany the saved-drill test |

Qualification: `c0908b2eddbb890c7394c03151fd9d4c43e2b437`, AWS `20261128T210811-turret-drill-garrison-isolation-1c3966`, terminal wait success and **3/3 passed**. The saved drill passes in 715.8 seconds with all eight original checks plus the new isolation check: both arms report rule/held zones/occupants/entries **0/0/0/0**; drill versus control Ordos structure losses **292 versus 253**, Rocket Turret damage **392k versus 231k**. The drill issues 172 orders, places and engages 136 turrets, leaves 148 new turrets standing, 86% touching, median enemy distance 9 and 38 within eight tiles.

The same source also passes `zone_garrison_probe` (103.9 seconds) and `garrison_capture_option_probe` (36.9 seconds). Normal enabled garrisons still activate, retain **2→2** occupants through blocked R/C/I growth and civic mutation, and permit actual growth after evacuation. The fix was handed to Integrator immediately after green was observed at **2026-11-28 21:22:36 AEDT**. These are targeted test-harness results, not a new combined-build/nightly or Air-installation qualification.

