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

- Current complete candidate: `e4c8e8fa4636d1eef32060ca34df05e5c3de9b46`, AWS `20261128T181613-garrison-shelter-160-v2-700fb0`: `saboteur_zone_probe`, `zone_garrison_panel_probe` and `zone_garrison_stats_probe` pass. The main probe passes **45/52**, including the positive immunity/healing/collapse/bloom/type/capacity assertions above. Its seven failures are civic save reconciliation and complex relocation/AI scenarios; this candidate is **not a green handoff**.
- Historical interim core `c98b65daa3f4ff67c7e7c8fab46db887bff5944a`, AWS `20261128T164807-garrison-core-assault-command-6f0eca`: 2/2 passed, including all 33 then-current cases, and was handed to Integrator at 16:50:54 AEDT. That receipt qualifies the superseded 10% rule, not the final immunity/relocation rule.
- Independent muzzle/statistics `ad8beab4c57a58743e609b8ba2fabaa578137b97`, AWS `20261128T175506-garrison-stats-current-e0a409`: 2/2 passed before integration as protocol 159.
- Private Air save inspection `df50be0f065c54e6b199af7d26752c97242d8fd4`, AWS `20261128T165347-private-air-garrison-save-inspection-5c8f80`: passed in 37 seconds. **It contains personal saves and must not be landed or pushed.** Exact pinned Workshop assets matched the committed compatibility fixtures.

## Read-only Air inspection and limits

No game was launched, stopped, or changed on the Air. Copied log SHA-256: `4a3432cc8077ee46c500ca9197eb259c29067ed18c48cb399428cd125647fb46`.

| Save | Cycle | Held zones / occupants | Units / structures |
| --- | ---: | ---: | ---: |
| `drillturrethole.dls` | 450872 | 0 / 0 | 433 / 4919 |
| `moremaginot.dls` | 356134 | 0 / 0 | 518 / 4923 |
| `seewhatiddid.dls` | 270409 | 0 / 0 | 913 / 4736 |

Counts were unchanged after production save/load. Those zero-activation saves cannot qualify occupied-lot preservation or identify Stefan's exact observed lot. No autosave/quick-save path was found in the inspected input/save code; the constructed regression uses the real manual Save dialog. Physical Air UI/readability and the exact Air lot/type-change correlation remain unverified.

