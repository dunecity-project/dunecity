# Garrisoned lots: stable buildings, cover, regeneration and five occupants

Stefan, 2026-11-28: "there's an issue with garrisoned buildings. they change type and the infantry disappear. dont let a garrisoned building change type."

Subsequent requirements supersede the original damage/healing policy:
- "infantry and troopers heal over time when in buildings not when the building heals. but also they only take 10% of the damage when in buildings"
- "allow up to 5 units to garrison"

## Root causes and fix

- `CityEffectsRuntime.cpp:1304,1381,1434` (original lines): growth, demand decline and power-loss vacancy mutated occupied lots. Its civic census (`1658-1670`) could also designate/remove Hospitals and Churches while occupied. All those mutation sources now exclude held lots. Population/supply/tax and traffic accounting still include them; no compensating tile rollback or queued replacement is introduced.
- `ZoneStructureGarrison.cpp:225-233` (original): every incoming hit took half its raw damage, rounded up, from the front soldier, before applying building damage. Soldiers have 20 HP and Troopers 45 HP versus a 200-HP zone. One-HP chip hits therefore cost a full infantry HP each; repairs could keep the building nearly pristine while its hidden occupants died.
- New damage rule: **every occupant** takes 10% of the HP the building actually loses. The whole incoming/overkill value is not shared. Per-occupant 32.32 building-HP remainders accumulate exact whole damage points, applied through the existing normal damage/attribution/death path. No per-hit rounding up and no ObjectBase API change.
- New regeneration: every living occupant heals continuously while inside, independently of building repair. No generic passive Infantry/Trooper regeneration existed; Tornie medic weapons heal other units and are not passive regeneration. `[Map Settings] GarrisonHealTime = 60` in `ObjectData.ini` gives a full heal from zero in **60 seconds of normal game time**. Fractional health plus a saved division remainder makes this exact, deterministic and capped. Leaving stops this regeneration. The tunable value is saved and included in the effective multiplayer data hash.
- The capacity is **five**, shared by the player-entry policy, saved health arrays, UI denominator, defensive AI and reinforcement AI. Existing AI front quotas of two are deliberate deployment quotas, not capacity limits. The map's helmet indicators scale to fit five.
- Death inside now emits an owner-local **Garrison casualties: N** news-ticker message plus a per-occupant `event=casualty` log. Presentation does not affect simulation or random state.
- Loaded civic overlays are restored before load reconciliation can regenerate their roster. Previously, a saved Hospital/Church could lose its designation before deferred restoration; the new occupied-lot lock must not lock in that erroneous empty designation.
- Scenario lot placement now initializes its city tiles before the simulator exists. Previously, `setLocation` skipped that initialization for bundled-map lots, so their type tags could disagree with their Dune structures from the start.

**Save 9905 / protocol 153**, as assigned by Integrator. Old saves initialize fractional remainders to zero and use the 60-second rate; new saves carry the rate and each occupant's damage/regeneration remainders. Multiplayer state digests include occupied-zone IDs and both remainders. No new switch: this repairs the default-on feature.

## Reproduction and Air evidence

Assertions-only baseline `3ba4c20907f9f80abdb6e351a49fe57c8c7b7cb1`, AWS job `20261128T154842-garrison-keeps-zone-before-6f77d8` compiled and ran (45.80 s). It reproduced growth, decline, vacancy and civic mutation while occupied. Example: residential density **2→0**, population **24→0**, but occupants and tracked infantry stayed **2→2**. Commercial/industrial density also changed **2→0**. Civic types changed **None→Hospital** and **Hospital/Church→None**, occupants **2→2**. The 23 existing cases and both demolition commands passed. Its bundled-map precondition failed because the authored Metropolis conversion resets density to zero; the corrected fixture develops one existing lot before Capture.

The Air's copied `Dune City.log` (1.0.880dev, timestamp 2026-11-28 15:45; SHA-256 `4a3432cc8077ee46c500ca9197eb259c29067ed18c48cb399428cd125647fb46`) records actual two-Trooper losses:
- zone 6222: entries at cycles 30346/30387; `killed_out` and ownership return at 31543 with the building **199/200 HP**;
- zone 7153: two entries at 50354; `killed_out` at 52040 with **132/200 HP**.
These are recorded deaths, not evidence of a city-sync delete/recreate. The current simulator changes population/density in place. A type change does not reset building HP. They do not alone identify Stefan's exact observed lot.

Damage-only controlled comparison (zero elapsed game cycles, so no regeneration; building repaired between one-HP hits): with two full 45-HP Troopers, the old rule predicts first death at **45 hits**, both dead by **90**; the new rule predicts both surviving 90, first/both deaths at **450**. The regression prints and asserts the observed values. Normal regeneration is measured separately.

## Acceptance assertions

| Requirement | `zone_garrison_probe` assertion |
| --- | --- |
| No growth, negative-demand decline or unpowered vacancy while occupied; resumes afterward | all R/C/I types, two actual occupants, empty controls activate; real Capture/Evacuate widgets; compare IDs, population, density, four tiles and power draw |
| Civic type stays while occupied and resumes changing after evacuation | ordinary, Hospital and Church lots; saved overlay roster remains consistent |
| Works in a real bundled city/captured lot | Metropolis's existing lot and roads; Capture takes it; census blocked; Evacuate returns ownership and decline resumes |
| Five enter, sixth refused | actual Capture buttons and game cycles; five hidden occupants, sixth remains active |
| Damage is exactly 10% of actual loss | 30-HP hit costs each occupant 3 HP; ten one-HP hits add exactly one; an immunity-suppressed hit costs zero |
| Regeneration at the configured rate, capped and only inside | 30 seconds adds half maximum HP without repairing the building; saturation; no healing after real Evacuate; 30-second INI tuning and changed multiplayer hash |
| No chip-damage disappearance at the old limit | two full Troopers remain two after the old 90-hit limit; new casualty threshold asserted; explicit visible message and normal unit removal |
| Survives until nearly destroyed | a 200-HP lot driven to 1 HP still holds both mixed infantry alive |
| Destruction ejects normally | real overkill damage, only actual building loss shared; both Troopers eject alive, visible and hurt |
| Save/load preserves type, counts and fractional state | real Save dialog, production reload, Hospital and Church, two occupant IDs/HP, both remainders, configured rate, UI count 2/5, subsequent tenth damage point, real Evacuate |

The Saboteur-zone regression is migrated to the new exact damage rule: a full 200-HP zone's destruction costs **20 HP to each** 20-HP Soldier, so both die through normal damage; Trooper ejection is asserted separately. This is not the obsolete front-only death/one survivor policy.
The three-Trooper assault fixture still requires takeover and defender removal. With the new cover, the defender evacuates at the existing 60% building threshold rather than dying inside first. The attackers explicitly target the evacuated defender. Entry below 60% remains forbidden, so the fixture additionally observes that refusal, repairs the cleared lot through the owner's real Repair command, then reissues Capture and requires takeover. It does not bypass the low-health policy.

## Separate loss-path follow-up

Read-only review found a spice-bloom egress resurrection/visibility error, an ownership-changing Follow route into legacy direct capture deletion, and automatic release of human-shared garrisons. Per Main's playtest-first instruction, those independent fixes follow this core patch; AiTacticsOption owns the human-shared release fix. Air inspection copies and the latest three saves are retained in the task's leased evidence scratch. No game was launched on the Air.
