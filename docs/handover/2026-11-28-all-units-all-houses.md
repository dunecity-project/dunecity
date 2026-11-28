# All Units for All Houses — 2026-11-28

Stefan: “also in dune city make all units available to all houses - no house at a disadvantage. deviators troopers etc”.

## Player option

**All Units for All Houses** is a built-in Dune City rule under **Options → Advanced → Default Game Rules → Change…**, also available in the match's Game Rules window. It defaults on in Dune City and off/hidden in vanilla. The per-content INI key has the same name. A player's explicit on/off choice is preserved; no separate mod is installed.

## Arsenal before and after

This is ordinary production at sufficient technology, factory upgrades and prerequisite buildings, not starting units, Palace summons or map-authored reinforcements. The reference is the shipped `config/ObjectData.ini.default`; installer-managed Dune City and vanilla both seed this table. City-only Harkonnen Trike/Ornithopter exceptions already exist and remain when the option is off.

| Unit / producer | Dune City before / option off: houses denied | Option on |
| --- | --- | --- |
| Soldier, three-Soldier Infantry / Barracks | Harkonnen (cannot construct Barracks) | All houses |
| Trooper, three-Trooper Troopers / WOR | Atreides (cannot construct WOR) | All houses |
| Trike / Light Factory | Ordos; vanilla additionally denies Harkonnen | All houses |
| Raider Trike / Light Factory | Atreides, Harkonnen | All houses |
| Quad / Light Factory | None; normal Harkonnen tech/upgrade difference remains | All houses |
| Tank, Siege Tank, Launcher / Heavy Factory | None; normal Ordos Siege tech/upgrade difference remains | All houses |
| Devastator / Heavy Factory | Atreides, Ordos | All houses |
| Sonic Tank / Heavy Factory | Harkonnen, Ordos | All houses |
| Deviator / Heavy Factory | Harkonnen, Atreides, Fremen, Sardaukar, Mercenary | All houses |
| Harvester, MCV / Heavy Factory | None | All houses |
| Carryall / Hightech Factory | None | All houses |
| Ornithopter / Hightech Factory | None in city; vanilla denies Harkonnen | All houses |
| Starport | All houses share the map-authored CHOAM catalogue; typical bundled maps omit infantry, Raider, all three heavy specials | All 17 ordinary products, with authored stocks retained |

At full tech the ordinary factory/barracks union is Harkonnen **12→17**, Atreides **12→17**, Ordos **14→17**, Fremen/Sardaukar/Mercenary **16→17** each. Neutral, Rebels and Custom already have the full 17-product table; their access remains 17. The tests inspect the actual active roster, rather than assuming six house slots.

### Palace specials (unchanged; deliberate deviation)

Harkonnen/Sardaukar retain Death Hand, Atreides retain Fremen warriors, Ordos/Mercenary retain Saboteurs, and Fremen retain their existing Palace Worm rule (Fremen warriors when that independent rule is off). Neutral/Rebels keep their existing light-vehicle call. These are house-specific superweapons, not ordinary purchased units. Stefan may overrule this exclusion. Saboteurs, Palace worms, ambient aircraft, free Frigates and content-family-exclusive units are not added to production or CHOAM.

## Enforcement and compatibility

- `ObjectData::applyAllUnitsForAllHouses` runs only for a **new city match** with the option on, before house/map initialization. It enables the 17 normal producer permissions plus Barracks/WOR construction, without changing per-house prices, combat statistics, technology, upgrades or prerequisites. All consumers, including sidebar lists and QuantBot, see the same effective table.
- Factories continue using `BuilderBase::updateBuildList` and existing command admission. A factory missing its ordinary upgrade, tech or prerequisites must still refuse the unit.
- CHOAM retains explicit authored quantities, including sold-out zero. Missing ordinary products start with **one**, then use normal random prices and restocking. An absent CHOAM section receives the ordinary roster too. The expanded Starport catalogue respects product technology and prerequisite structures; Starports have no factory-upgrade levels. Off preserves the authored catalogue and original Starport gates exactly.
- The effective object table, queues and stocks are saved normally. Loading a legacy save never upgrades its arsenal; the absent option reads off. The settings hash distinguishes on/off. MOD19 byte bit 2 (`4`) carries the option; bit 0 remains Technology Installments and shipped/retired bit 1 stays ignored. Values 0/1/2/3 must read off and 4/5/6/7 on without shifting following data. No save-layout extension is required; the Integrator owns the monotonic multiplayer protocol bump.

## QuantBot

The real activation probe exposed a restriction in the old house-tuned priors: Harkonnen's full shared arsenal still reserved almost the entire special budget for Devastators, leaving Sonic Tanks and Deviators at the 2% minimum. The whole-unit allocator completed ordinary units but none newly available to that house in three game minutes.

With the option on in a city, `QuantBot::startingPrior` now divides the **existing aggregate special-unit prior** evenly across the fieldable Devastator/Sonic Tank/Deviator slots. It neither increases the military budget nor forces a purchase. Existing unit-specific combat learning and normal producer admission remain authoritative. Off and vanilla retain the exact pre-option prior recipe, asserted independently in the probe. Light vehicles and infantry retain their existing data-driven choices.

The paid factory-output probe observes six game minutes at the normal 50-cycle build cadence: 450 ordinary build turns per house/arm. The longer bound also gives low-prior expensive units time to become a whole-unit allocation; it does not waive the nonzero new-unit assertion or seed/force any production order. The no-yard/no-Starport/no-Palace fixture isolates factory output, suspending unrelated unit/MCV controllers between decisions while advancing real engine production cycles.

The six-minute gate also exposed Sardaukar starvation: the allocator always paid the largest absolute Siege Tank deficit before the small positive special slots. Shared-city heavy factories now use the existing `capacityFill` chooser directly: least post-order committed value relative to the intended share. Normal availability, price, cash, unit-limit and total military-budget checks remain intact. Off/vanilla keep `fundedDeficit` and its original fallback unchanged. This is composition balancing, not a forced new-unit purchase or an extra quota.

The activation fixture gives every house six ordinary Heavy Factories, uniformly in both arms. Four factories produced only 27,150 credits of Mercenary heavy units in six minutes: too little for its 2% expensive-unit share to reach a completed 750-credit Deviator. The capacity increase leaves the six-minute bound, costs, configuration, cash, admission gates and all completed-unit assertions intact; no unit is preselected or artificially queued.

## Required observable acceptance

| Property | Real-path assertion |
| --- | --- |
| Player can choose the rule | Real Options/Advanced window, checkbox, explanation, OK/APPLY, INI reload and reopening; default city on and vanilla hidden |
| All houses receive the ordinary arsenal | Every active city house's factory/barracks lists contain all 17 on; exact previous lists off |
| No technology shortcut | Below-tech, missing-upgrade and missing-prerequisite products remain unavailable |
| Imports match the rule | Normal map stock on/off, expanded Starport list and paid delivery |
| Real production works | Human production input reaches a deployed newly available unit |
| QuantBot uses new units | Each of the six houses with denied baseline products completes at least one newly available factory unit on; off completes zero denied products |
| Settings stay aligned | On/off, installments and legacy retired-bit combinations round-trip with following bytes intact |

The probe is bounded evidence of these states and completed production, not a claim of improved battle win rate or exhaustive multiplayer/platform qualification. Exact-SHA broad AI regression and performance qualification remain the integration/release owners' gates.

## Exact-source acceptance and handoff

- Tested source: `a8187877a41bacd50fbab183d5bb74ac22516c79`.
- AWS job: `20261128T205355-all-units-deployment-proof-03b20c`, terminal **3/3 passed**: `all_units_settings`, `all_units_all_houses_probe` (53.7 s), `garrison_capture_settings`.
- Retained evidence: `$HOME/.local/state/dunecity-queue/ctest/20261128T205355-all-units-deployment-proof-03b20c/all_units_all_houses_probe@aws.log`.
- Every house/arm used seed `20261128`, configuration `3ce525ea25b7b92f`, 22,500 real game cycles and 450 ordinary build turns.

| QuantBot house | Completed OFF | Denied products completed OFF | Completed ON | New-to-house completions ON |
| --- | ---: | ---: | ---: | ---: |
| Harkonnen | 143 | 0 | 144 | 17 |
| Atreides | 139 | 0 | 150 | 22 |
| Ordos | 155 | 0 | 148 | 15 |
| Fremen | 158 | 0 | 161 | 6 |
| Sardaukar | 155 | 0 | 156 | 1 |
| Mercenary | 164 | 0 | 162 | 1 |

Neutral, Rebels and Custom are explicitly not applicable to the *new*-unit assertion: they already had all 17 ordinary products; their on/off catalogue assertions still run.

The same green job verified nine-house on/off catalogues, ordinary tech/upgrade/prerequisite refusals, real menu/INI round-trips, legacy packed-byte alignment, 16 paid newly available factory orders yielding 20 individual units, and eight paid newly stocked Starport orders yielding 12 individual deployments for 2,837 quoted credits. Vanilla Harkonnen stayed at 10 factory products and nine Starport products even with the stored flag forced on; its checkbox stayed hidden and its AI prior stayed legacy-identical.

Starport completion is asserted using exact per-type House built-counter deltas, incremented by `StarPort::deploySingleUnit` only after successful creation and real `deploy()`, plus independent live physical witnesses for every delivered type and exact paid queue/stock provenance. The earlier end-of-cycle observer saw three Soldiers while the engine recorded all four successful deployments; demanding all four remain active and detectable at a later sampling point was a different state condition, not a missing paid delivery. The full twelve-unit and complete-squad quantities remain mandatory.

Earlier diagnostic jobs are not acceptance: `20261128T195414-all-units-paid-completion-c6c1b9` exposed the original zero-new-unit AI behavior; `20261128T201657-all-units-balanced-chooser-ead877` exposed the four-factory Mercenary capacity shortfall; `20261128T204904-all-units-exact-completion-55c1f3` retained the overly strict Starport observation failure. Only the exact-source green job above closes this feature gate.

Integrator handoff is the cumulative feature delta from approved baseline `2659756454dcc6ab4f6a3308058f9683e49eb732`, preserving later shared-source work rather than installing intermediate diagnostic commits. The evidence-only documentation successor does not change tested code. This is a built-in city rule, not a new mod. Palace powers remain per house for Stefan to overrule; normal house-specific prices, statistics and admission prerequisites remain unchanged.
