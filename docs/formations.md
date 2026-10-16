# Formation movement (player and AI)

A group ordered to one point arrives together and spread out in a chosen shape. Without
formations the group moves in single file and piles onto one tile. Branch `feat/formations`
built the Box; branch `feat/formation-shapes` (save 9859, protocol 73) adds the shapes, the
sonic-lane rule, hunting in formation and the panel buttons described first below; branch
`feat/formation-form-up` (save 9861, protocol 75) makes those buttons form the selection up
where it stands.

## Concentrating force on attack and defence (branch `feat/ai-blob-waves`)

Stefan: "keep working on formations. we need to optimise how the ai concentrates forces on attack and
defense". Everything before this (shapes, fighting in formation, Blob waves and their variants) lost to
None on Hard; the open problem was that after contact the members engage one at a time (9-11% within
10 s). This round looks beyond formations: how strong bots and players win army fights.

### Research: how bots and doctrine concentrate force

- **Lanchester's square law** (F. W. Lanchester, *Aircraft in Warfare*, 1916, ch. V "The Principle of
  Concentration. The N-Square Law", [archive.org](https://archive.org/details/aircraftinwarfar00lancrich)):
  with aimed fire, strength grows with the square of the numbers. Blue 1,000 meeting Red's 500 + 500 one
  after the other loses about 134 in the first fight and 293 in all; met together, the two Reds are a
  match. Stanescu, Barriga & Buro, "Using Lanchester Attrition Laws for Combat Prediction in StarCraft"
  (AIIDE 2015, [PDF](https://cdn.aaai.org/ojs/12780/12780-52-16297-1-2-20201228.pdf)) fit an exponent of
  1.56 for StarCraft battles and note that focus fire is the same law at the micro level: killing a unit
  sooner removes its gun sooner. *For QuantBot:* a wave that arrives in pieces fights several small
  battles it loses; both arriving together and focus fire should pay.
- **UAlbertaBot** (StarCraft BW, [CombatCommander.cpp](https://github.com/davechurchill/ualbertabot/blob/master/UAlbertaBot/Source/CombatCommander.cpp),
  [Squad.cpp](https://github.com/davechurchill/ualbertabot/blob/master/UAlbertaBot/Source/Squad.cpp)):
  every combat unit is in one main attack squad; each 10 frames it simulates the local fight (SparCraft,
  300 px around the unit nearest the target) and regroups on a predicted loss, at the squad unit nearest
  the target that is not within 400 px of an enemy (a forward point, not home), with 100 frames of
  hysteresis. Defence: 2 defenders per enemy unit, nearest first. Targeting: threats before buildings.
  Its simulation scripts are NOK-AV: Churchill, Saffidine & Buro, "Fast Heuristic Search for RTS Game
  Combat Scenarios" (AIIDE 2012, [PDF](https://cdn.aaai.org/ojs/12527/12527-52-16049-1-2-20201228.pdf)):
  attack the enemy with the highest damage per hit point and never one already assigned lethal damage -
  "the only script ... that exhibits true collaborative behaviour" and the best tested.
- **Steamhammer** ([Squad.cpp](https://github.com/kant2002/steamhammer/blob/master/Steamhammer/Source/Squad.cpp),
  [CombatCommander.cpp](https://github.com/kant2002/steamhammer/blob/master/Steamhammer/Source/CombatCommander.cpp)):
  the squad is split into clusters; a cluster not near the enemy joins the nearest cluster closer to the
  goal, only the lead cluster advances; attack/retreat by simulation smoothed with an exponential average
  (0.15, attack at 0.5); never retreat near its own firing defences or base. Defence is sized by weighted
  enemy strength (2 per medium, 4 per heavy unit), starts at 19 tiles and ends at 29.
- **PurpleWave** ([JudgmentModifiers.scala](https://github.com/dgant/PurpleWave/blob/master/src/Information/Battles/Types/JudgmentModifiers.scala)):
  reluctant to leave a fight it has entered, patient before starting one ("patience will tend to let us
  gather more force"), fights harder near home; engages in arcs facing the threat.
- **sharpy-sc2** ([default_micro_methods.py](https://github.com/DrInfy/sharpy-sc2/blob/develop/sharpy/combat/default_micro_methods.py),
  [zone_attack.py](https://github.com/DrInfy/sharpy-sc2/blob/develop/sharpy/plans/tactics/zone_attack.py),
  [zone_defense.py](https://github.com/DrInfy/sharpy-sc2/blob/develop/sharpy/plans/tactics/zone_defense.py)):
  attacks at 1.2x the known enemy power; only a group holding 75% of the army's power attacks, the others
  join it; retreats when the local enemy x0.8 matches it within 18 tiles; defence sends 1.5x the attackers'
  power, nearest first; focus fire scores damaged high-value targets, +3 for the current target, x0.1 when
  lethal damage is already assigned.
- **OpenRA squad AI** (also the Dune 2000 mod; [SquadManagerBotModule.cs](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Traits/BotModules/SquadManagerBotModule.cs),
  [GroundStates.cs](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Traits/BotModules/Squads/States/GroundStates.cs),
  [AttackOrFleeFuzzy.cs](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Traits/BotModules/Squads/AttackOrFleeFuzzy.cs)):
  all idle units go once there are `SquadSize` (d2k 6/8/10) + random 0-30; the leader waits until every
  unit is within units/3 cells; attack or flee by fuzzy rules on relative health, power and speed; never
  flees with an own building within 10 cells. Defence: every idle base unit that can reach the attacker
  joins one protection squad, and squads within 15 cells retarget onto it.
- **Age of Empires II** ([strategic numbers](https://airef.github.io/strategic-numbers/sn-details.html)):
  attack groups gather to a gather point before tasking (`sn-attack-group-gather-spacing` 4,
  `sn-group-form-distance` 20); 75% of soldiers attack; outside the town 50% of the units within 25 tiles
  answer an attack. No primary source for AoE IV's AI was found.
- **Command & Conquer: Red Alert** ([TEAM.CPP](https://github.com/electronicarts/CnC_Remastered_Collection/blob/master/REDALERT/TEAM.CPP),
  [HOUSE.CPP](https://github.com/electronicarts/CnC_Remastered_Collection/blob/master/REDALERT/HOUSE.CPP)):
  scripted teams hold until laggers more than 2 cells away catch up, start only at full strength and
  regroup at a safe repair building at a third of their strength - but the skirmish attack is the
  counter-example, each armed unit hunting on its own with 75% probability (a trickle).
- **Spring / BAR (CircuitAI)** ([MilitaryManager.cpp](https://github.com/rlcevg/CircuitAI/blob/barbarian/src/circuit/module/MilitaryManager.cpp),
  [DefendTask.cpp](https://github.com/rlcevg/CircuitAI/blob/barbarian/src/circuit/task/fighter/DefendTask.cpp)):
  new attackers wait in a defend task until their power reaches the strongest enemy group's; defence near
  the base accepts up to 4x the threat. No primary source for Total War's battle AI was found.
- **Doctrine.** US Army FM 3-90 ([ch. 2](https://www.globalsecurity.org/military/library/policy/army/fm/3-90/ch2.htm)):
  "piecemeal commitment subjects the smaller committed forces to defeat in detail"; the defender masses
  the effects of its combat power and counterattacks with its reserve ([ch. 8](https://www.globalsecurity.org/military/library/policy/army/fm/3-90/ch8.htm)).
  ADP 3-0 ([PDF](https://irp.fas.org/doddir/army/adp3_0.pdf)): interior lines let a central reserve reach
  any threatened point sooner than the attacker can switch.

### What QuantBot does today (code read)

- **Attack.** Every 90 s (`AttackTimerMs`) `attack()` checks the house's military value against
  `AttackThresholdPercent` of the military limit (Custom Hard: the global 0.40; Brutal: its own 0.25) and
  the outnumbered dispatch gate (hold while the observed enemy front is 1.5x ours, unless 80% of the limit
  is healthy and deployable). `launchGroundHunt()` then sends the whole available ground army - every
  fighter not repairing, defending or escorting - once it is worth the difficulty's threshold (Hard 0.30,
  Brutal 0.25 of the limit) and a viable main wave (6 units, 3,000 credits). `AttackForceMilitaryValueRatio`
  (Hard 0.5, Brutal 0.6) is the share of the ground army an Easy/Medium Custom wave commits; the Hard/Brutal
  whole-army dispatch does not use it. Each member gets engine Hunt: it walks at its own speed to the
  nearest visible enemy anywhere and shoots the nearest target (no focus fire).
- **Defence.** A hit on one of our buildings (or an attacker in range of one, re-checked every unit pass)
  calls `scrambleWholeArmyToDefend` (Custom Hard/Brutal, since 1.0.832): the threat is the attacker plus the
  visible enemy fighters within 8 tiles of the building; the nearest free troops - wave members hunting
  elsewhere included - within a band of 12 tiles + 1 per 500 credits of threat (at most 40) are sent until,
  with the troops already fighting there, they are worth 1.25x the threat; each hunts its way to the
  building's nearest point at its own speed. Workers and other contacts get the district response
  (`scrambleUnitsAndDefend`: the same 1.25x, debounced per 8x8-tile district for 2 s). The army posture
  recalls the tracked wave to the protected shelter on strategic attrition, a core attack or a local
  1.3x disadvantage for 9 s, and colony guards hold threatened colonies while recovery is active.

### Candidates (test-only switches)

`QuantBot::forceVariantOverride` (duel label flags in upper case) and two more Blob bits
(`blobVariantOverride`, lower case); none is set by the game, saved or hashed. Rules as pure functions in
`include/players/ForceConcentrationPolicy.h` (`ForceConcentrationPolicyTestCase.cpp`), the code in
`src/players/QuantBotForce.cpp`, `scrambleWholeArmyToDefend` and `QuantBotBlob.cpp`.

| Flag | Candidate (task item) | Rule as built |
|---|---|---|
| `F` | focus fire (c) | Every unit pass each hunting ground fighter (wave member or base defender) takes the best enemy in its weapon range: fighters, then turrets, then workers, then buildings (walls never), fewest hit points first, lowest id; it leaves a target it can still shoot only for a better class or 75% of its hit points. |
| `N` | focus fire, attack value without overkill (c) | NOK-AV: the target with the most damage per hit point (weapon damage / reload x1000, cross-multiplied), fighters first; a target already assigned a lethal volley by the units shooting it is skipped; units in object-id order; switch only out of reach, when overkilled, for a better class or a third more value. |
| `C` | coordinated engage after contact (b) | A wave member that is not fighting walks (hunt-toward) to the nearest fight of its army within 24 tiles - a hunter of ours shooting an armed enemy in its reach - unless its own target is no further than the fight + 4 tiles. |
| `P` | pool defenders by strength (h) | The threat counts enemy fighters within 12 tiles of the attacked building (not 8); the defence sends 2x it (not 1.25x), nearest first, from up to 40 tiles whatever the threat. |
| `H` | recall raiders on a real base attack (i) | When the troops within the band cannot reach the requirement, the nearest troops beyond it - the raiding army - come home for it; a raid the troops nearby can answer recalls nobody. |
| `M` | gather defenders outside the fight (h) | Defenders sent from more than 14 tiles muster 8 tiles from the building on their side of it, out of enemy reach, and engage together at 75% there, on contact, or after 15 s. |
| `L` / `Q` | larger, rarer / smaller, more frequent waves (f) | Wave readiness (and the house attack threshold) at 150% / 60%. |
| `s` (Blob) | consolidate near the enemy (a) | No gather at home: the blob marches in one leg to a staging point 12 tiles short of its objective, outside observed enemy reach; only an armed enemy in reach releases it on the way; at the staging point it waits for the rest (quorum, or the slowest member's travel time, at most 120 s) and then attacks all at once. |
| `e` (Blob, with `s`) | engage only with local superiority (d) | At the staging point the party attacks only if its power is 1.5x the visible enemy fighters and turrets within 12 tiles of the objective; else it holds up to 60 s, and waves launched meanwhile join it. |
| `/weak` (label) | defence scenario | Half of that side's starting army is removed (every second ground fighter by id) before the first cycle. |

### Results (duel harness, None as control)

Hard 1v1, Random Skirmish mirrored maps 128/160/192, Fortress + 20,000-credit armies, seeds 7101 7202
7303 7404 7505 7606, one house pair per seed, both sides: 36 games per pairing, 40 game minutes,
time-outs adjudicated on a 1,000-credit unit trade. Raw data `docs/formation-results/force/games.csv`,
`waves-by-shape.csv`; printed reports `report-p2.md`, `report-p1.md`. Reproduce:
`formation-duel.py --results <dir> --tag p2 --pairs "none+F:none,..." --sizes 128 160 192 --seeds 7101 7202 7303 7404 7505 7606 --report`.

**Tag `p2`** (code: this branch after merging `fix/ai-regression-all-vs-atreides` a70f8666 - house
withdrawals off by default, front battery 8-12 - and `perf/headless-sim`; the None baseline changed
with that merge, so only `p2` decides). Wave columns are the candidate's waves; None's waves in the same
games engaged 7-9% within 10 s and traded 3.0-4.2 killed/lost in the first minute.

| Candidate v None (Hard) | Games | Score | 95% CI | Elim. W-L / time-outs | Contact s | Engaged ≤10 s | Killed / lost 60 s | Structures lost / destroyed per game | Home kill / loss | Win min |
|---|---|---|---|---|---|---|---|---|---|---|
| F focus fire | 36 | 0.583 | 0.42-0.75 | 18-7 / 11 | 40 | 8% | 2.96 | 51.8 / 65.8 | 0.36 | 31.6 |
| N attack value, no overkill | 36 | 0.472 | 0.31-0.64 | 11-7 / 18 | 33 | 8% | 3.76 | 45.3 / 41.8 | 0.51 | 28.2 |
| C converge after contact | 36 | 0.500 | 0.33-0.67 | 9-12 / 15 | 34 | 9% | 3.29 | 53.7 / 37.5 | 0.48 | 31.8 |
| FC | 36 | **0.611** | 0.44-0.78 | 12-8 / 16 | 35 | 9% | 4.58 | 33.6 / 45.2 | 0.36 | 31.4 |
| NC | 36 | 0.528 | 0.36-0.69 | 12-6 / 18 | 39 | 8% | 3.11 | 43.1 / 40.8 | 0.39 | 31.1 |
| P pool defenders 2x | 36 | 0.528 | 0.36-0.69 | 11-8 / 17 | 36 | 8% | 3.68 | 41.9 / 48.2 | 0.44 | 34.6 |
| H recall raiders | 36 | 0.528 | 0.36-0.69 | 10-9 / 17 | 37 | 8% | 3.53 | 42.5 / 45.2 | 0.41 | 34.0 |
| PH | 36 | 0.528 | 0.36-0.69 | 11-10 / 15 | 36 | 8% | 3.68 | 41.8 / 48.1 | 0.44 | 34.6 |
| M muster defenders | 36 | 0.500 | 0.33-0.67 | 10-10 / 16 | - | - | - | 44.9 / 44.9 | 0.42 | 34.0 |
| Q smaller waves (60%) | 36 | 0.556 | 0.39-0.72 | 17-8 / 11 | - | - | - | 28.7 / 60.8 | 0.38 | 32.4 |
| none/retreat (attack, fall back, reform) | 36 | 0.486 | 0.32-0.65 | 9-11 / 16 | - | - | - | 44.9 / 41.7 | 0.44 | 36.1 |
| blob+s staged near the enemy | 36 | 0.500 | 0.33-0.67 | 10-10 / 16 | - | - | - | 52.6 / 44.0 | 0.39 | 34.0 |
| blob+se staged, superiority | 36 | 0.500 | 0.33-0.67 | 10-10 / 16 | - | - | - | 53.1 / 44.1 | 0.40 | 34.3 |

("-": in `report-p2.md`.) How often the rules fired per game: F 1,433 target switches, N 961, C ~5,000
converge orders, P 45 pooled defenders, H 11 recalled raiders, M 4.2 musters, staged parties 0.7-0.9
(contact with an armed enemy releases almost every staged blob before it reaches its staging point;
the superiority hold fired 0.2 times a game).

**Tag `p1`** (pre-merge baseline, withdrawals still on; stopped after 20-23 games per pairing when the
regression fix landed): L (waves at 150% readiness) 0.000 in 8 games (0-3 eliminations, 5 time-outs,
-50,924 value per game) - the only clear result, so it was dropped; every other candidate 0.40-0.68 with
intervals across 0.5 (Q 0.682, H 0.591, F 0.565, blob+vs 0.400). Not used for the decision.

**What the games show.** Nothing separates from None in 36 games. Focus fire with convergence (FC) is
the best point estimate (0.611, first-minute trade 4.58 against None's 3.44 in the same games, 34 of our
structures lost per game against 45 destroyed), and focus fire alone wins the most eliminations (18-7),
but both intervals reach below 0.5. Convergence alone does not change how members engage (9% within 10 s
against 8%): the members it redirects are those not yet fighting, and they still arrive at their own
speed. The defence rules fire (P sends 45 defenders a game, H recalls 11 raiders) but do not move the
home trade (0.41-0.44 against 0.36-0.51 for the others). Staging near the enemy almost never stages.

**Decision: no change to the QuantBot default; `WaveFormation` stays None and every candidate stays a
test-only switch** (`forceVariantOverride`, Blob bits `s`/`e`), because no candidate's 95% interval is
above 0.5 on Hard. Protocol 77 / save 9862 unchanged by this round (no production AI decision changed).

**Not done in this round (request budget):** the 108-game confirmation and the Brutal 36 of FC and F
(the next step: `--pairs "none+FC:none,none+F:none" --all-houses` and `--difficulty brutal`), the
`/weak` defence scenario (built and smoke-tested, not run), and Stefan's three new ideas from the parent
(squad-centre bundling of idle troops as before 1.0.602, why big waves lose, a two-angle split attack).
On L, the only evidence is `p1`: fewer attacks in 40 minutes and -50,924 value per game; its cause was
not traced in replays.

## Blob waves: gather, march together, unblob on contact, reform, tactical retreat (branch `feat/ai-blob-waves`, save 9862 / protocol 76)

Stefan: "i would at least thing having a big blob that moves together towards the enemy after first
gathering up all available units in the hunt party for an offensive would be good, because otherwise
trikes run off ahead etc. it could blod up, move towards its desination and then unblob on making
contact. i think we also have a need to get it to reform. i think the retreat we do on damage should
be a tactical retreat back a bit to reform as a formation".

### Research: how RTS AIs keep an army together

- **OpenRA squad AI** ([`GroundStates.cs`](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Traits/BotModules/Squads/States/GroundStates.cs)):
  a squad elects a *leader* - the unit nearest the squad's centre among the units with the most
  restricted locomotor, so the others can follow it. While attack-moving, if fewer than all members
  are within `units / 3` cells of the leader, the leader *stops* and the stragglers attack-move to
  the leader ("units have different movement speeds, they get separated ... let them regroup into
  tighter formation"); once together the squad attack-moves to the target and, near an enemy,
  switches to an attack state where every unit attack-moves on its own. A squad that has not moved
  for 2.5 s drops back to idle (stuck guard). Its flee state sends the squad to an own building -
  a full retreat. *For QuantBot:* the anchor (leader) choice, "wait for the rear" cohesion and the
  per-unit fight after contact are exactly Stefan's blob; the stuck guard is needed too.
- **UAlbertaBot / Steamhammer (StarCraft: Brood War)** ([`Squad.cpp`](https://github.com/davechurchill/ualbertabot/blob/master/UAlbertaBot/Source/Squad.cpp),
  [paper](https://davechurchill.ca/publications/pdf/cog19_buildorder.pdf)): every second the squad
  simulates the local fight (SparCraft) when an enemy is in range; if it predicts a loss it
  *regroups*: every unit moves to the regroup position, which is the squad unit closest to the
  order's target that is **not** near an enemy - a short tactical step back to the front edge of
  the safe part of the squad, not home. A 100-frame hysteresis stops it flipping back to attack.
  *For QuantBot:* the tactical retreat should be a short fall-back to safe ground beside the party,
  triggered by a local strength comparison, with hysteresis.
- **sharpy-sc2** ([`group_combat_manager.py`](https://github.com/DrInfy/sharpy-sc2/blob/develop/sharpy/combat/group_combat_manager.py)):
  own units are clustered (DBSCAN); a configurable share of the attackers must be together before
  attacking (`regroup_percentage`), and a *faster* group regroups to a slower one. *For QuantBot:*
  a gather quorum (not 100%: blocked units must not stall the attack) and fast units waiting.
- **Age of Empires II AI** ([strategic numbers](https://airef.github.io/strategic-numbers/sn-index.html)):
  attack groups gather before attacking (`sn-attack-group-gather-spacing`, default 4; units count as
  formed within `sn-group-form-distance`, default 20, of the group commander chosen by
  `sn-group-commander-selection-method`). *For QuantBot:* gather point plus a formed-distance test.
- **Beyond All Reason / Spring** ([Move in Formation](https://www.beyondallreason.info/commands/move-in-formation)):
  "faster units will move at the speed of the slowest units in the group". No primary source for an
  AI regroup rule was found for BAR's bots, Total War or Command & Conquer beyond OpenRA's.
- **Blob versus formation:** the earlier rounds here (below) found that rigid shapes lose to plain
  Hunt, because a formation that keeps its shape into the fight fights with its front rows only; the
  sources above all keep cohesion only *until* contact and then let every unit fight on its own.

### What QuantBot does today (1.0.842, code read)

- **Who goes:** Custom Hard/Brutal `launchGroundHunt()` is a whole-army dispatch: every available
  ground fighter (not on a repair run, a live defence job, an escort or already hunting) once the
  available value reaches the readiness threshold and a viable main wave (6 units, 3,000 credits).
  They are tracked in `customWave`. Units already hunting join the tracked army unchanged.
- **Why trikes run ahead:** each unit simply gets engine Hunt (`doSetAttackMode(HUNT)`): each one
  walks at its own speed to its own nearest target (quad 1.28, tank 0.80, siege tank 0.64, trooper
  0.48, soldier 0.24 px/cycle), so the fast ones arrive first and alone. Nothing waits.
- **The retreat on damage today:** (1) per unit - a vehicle below 60% health with a repair yard is
  sent to the repair yard (`onDamage` → `doRepair`), alone, wherever it is; (2) the squad - when
  the enemy around the tracked army's centroid is 1.3x ours for 9 s (`localSquadWithdraw`) or 2x
  (severe: a house emergency), or on sustained attrition / a core attack, `applyArmyPosture()`
  recalls every tracked member to the protected shelter inside the base (forced moves, Area Guard);
  the house then waits in Withdrawing/Recovering until it is ready to attack again.

### Design as built

`WaveFormation = Blob` (QuantBot Config.ini, optional key, code default unchanged: None) makes a
Custom Hard/Brutal ground wave a **hunt party** (`QuantBot::BlobParty`, `src/players/QuantBotBlob.cpp`,
pure rules in `include/players/BlobWavePolicy.h`). Who goes and when is unchanged; Easy/Medium,
campaign waves and every other AI group move as None with Blob (`QuantBotConfig::formationShapeOf`).

- **Gather:** the anchor is OpenRA's leader (the member nearest the members' centre, vehicles
  first). The rally point is up to 6 tiles (a third of the way at most) from it towards the
  objective, on standable ground in the anchor's vehicle terrain region
  (`Map::vehicleTerrainRegion`) outside observed enemy weapon reach. Every member walks to its own
  hashed tile around it in Area Guard. The party marches when 90% stand there; or when the rest
  have all stopped (blocked), nobody arrived for 8 s and 60% are there; or at the latest after the
  slowest member's straight-line travel time x1.5 + 4 s (8-60 s).
- **Blob march (leg by leg):** each leg ends one leg (`max(8, 2 x blob radius)` tiles) from the
  anchor towards the objective; every member walks to its own tile there in Area Guard; the fast ones
  arrive first and **wait**; the next leg starts when 90% of the members that are not stragglers
  (more than two legs plus the radius away) are there, or after the slowest member's travel time
  (x1.5 + 4 s, at most 90 s). So the blob moves at its slowest member's pace and a fast member is at
  most about two legs plus two radii from the slow ones. A blob that gets no closer for 60 s, or
  comes within half a leg of the objective, is released.
- **Release on contact (unblob):** a visible enemy fighter or building within any member's
  `huntTowardRange()` (weapon or view range, the distance at which engine Hunt itself stops), or a
  hit on any member, releases the party: members already in contact keep their target in Hunt, the
  others hunt their way to the contact (`doHuntToward`), so every member fights as engine Hunt -
  exactly None's fight, all members engaged.
- **Reform:** when no enemy has been within reach of any member and nobody was hit for 6 s, the party
  reforms around the anchor (a gather with a 30 s cap) and marches on to the next objective (the old
  one if it still stands, else the wave's objective choice again).
- **Tactical retreat:** during a fight, our ground fighters against the visible enemy fighters within
  10 tiles of the anchor (the posture policy's 1.3x for 4 s, or 2x at once): the party falls back
  8 tiles away from the threat, bent towards home (away from the threat counts twice), on a forced
  move, then reforms there and marches again - at most 3 times per party. With the tactical retreat
  on, the squad-level recall home (`localSquadWithdraw`) and the severe-local-defeat house emergency
  are replaced by it; strategic attrition and core attacks still recall the whole army home, and
  then the parties dissolve. Per-unit repair runs below 60% health are unchanged (see the results).
- **Determinism:** parties are a vector in launch order, members ordered sets, every scan in a fixed
  list/tile order with id tie-breaks, integer arithmetic only. Party state is saved
  (SAVEGAMEVERSION 9862: phase, clocks, points, objective, fall-back count, members); a 9861 save
  loads with no party. Protocol 76 (AI orders differ from a 75 peer).
- **Ablation switches (test-only, not saved, not hashed):** `QuantBot::waveFormationOverride`
  (Blob/None) and `QuantBot::tacticalRetreatOverride` (-1 with Blob only, 0 off, 1 also for plain
  Hunt waves, which then track a party that only falls back and reforms). Duel labels: `blob`,
  `blob/noretreat`, `none/retreat`.
- **Tests:** `BlobWavePolicyTestCase.cpp` (dunelegacy_tests: radius/leg, gather quorum/stall/
  timeout, leg quorum, straggler and travel limits, fast-lead bound, rally and fall-back geometry,
  outmatch ratios, integer symmetry, Blob config/hash) and the real-engine probe
  `quantbot_blob_waves` (`tests/ai/blob-wave-probe.inc`: gather of every available fighter, gather
  timeout keeping stragglers, march with the fast-member lead within bound and below plain Hunt's,
  release into Hunt on contact, reform after 6 s quiet, fall-back ~8 tiles away from the threat and
  return to the march, order independence of party/orders/contact, save/load mid-gather, mid-march
  and mid-reform with identical re-saved bytes, None unchanged).

### Results (duel harness, None as control)

`tests/ai/formation-duel.py` (Random Skirmish 1v1, mirrored, Fortress + 20,000-credit armies, 40 game
minutes, time-outs adjudicated on a 1,000-credit unit trade), sizes 128/160/192, seeds 7101-7606,
`--all-houses` (all three house pairs per seed) and both side assignments: **108 games per pairing**.
Code: commit `0aba7ea8` (this branch). Raw data: `docs/formation-results/blob/games.csv`,
`waves-by-shape.csv` (tags `hard1`, `brutal1`) and the printed reports `report-hard1.md`,
`report-brutal1.md`; reproduce with `formation-duel.py --results <dir> --tag hard1 --pairs
"blob:none,blob/noretreat:none,none/retreat:none" --sizes 128 160 192 --seeds 7101 7202 7303 7404 7505 7606 --report`.

| Pairing (first side's score), Hard | Games | Score | 95% CI | Eliminations W-L / time-outs | Strict score | Value margin / game | Verdict |
|---|---|---|---|---|---|---|---|
| blob v none | 108 | 0.306 | 0.22-0.39 | 18-33 / 57 | 0.431 | -20,407 | loses to None |
| blob/noretreat v none (blob without the tactical retreat) | 108 | 0.343 | 0.26-0.44 | 23-31 / 54 | 0.463 | -16,384 | loses to None |
| none/retreat v none (tactical retreat without the blob) | 108 | 0.486 | 0.39-0.58 | 30-30 / 48 | 0.500 | -865 | not distinguishable |
| blob v none, **Brutal** (one house pair per seed) | 36 | 0.444 | 0.28-0.61 | 8-11 / 17 | 0.458 | -3,688 | not distinguishable |

Waves (every wave of the first side in those games; contact = first member engaging; together = living
members within 8 tiles of the member nearest their centre at contact; engaged within 10 s of contact;
first-minute value from contact; survival 5 min after launch), with None's waves in the same games:

| Side (Hard) | Waves | Contact s (median) | Arrival spread s | Together at contact | Engaged ≤10 s | Killed / lost 60 s | Killed / lost | Survival | None in those games: together, K/L, survival |
|---|---|---|---|---|---|---|---|---|---|
| blob | 621 | 54 | 71 | 50% | 11% | 2,675 / 925 | 2.89 | 71% | 22%, 3.67, 65% |
| blob/noretreat | 633 | 60 | 74 | 50% | 11% | 2,598 / 785 | 3.31 | 73% | 23%, 3.78, 64% |
| none/retreat | 658 | 33 | 86 | 24% | 8% | 3,523 / 966 | 3.65 | 59% | 23%, 3.63, 59% |
| blob (Brutal) | 196 | 58 | 75 | 49% | 10% | 2,819 / 855 | 3.30 | 75% | 20%, 3.40, 68% |

How often the party steps fired, per game (Hard, Blob with retreat): 5.8 parties, 2.8 gathers that
reached their quorum and 5.3 gathers/reforms that ended on their time limit, 26 legs, 17.8 releases,
7.4 reforms, 0.9 tactical fall-backs (0.3 completed back into a reform before the house posture or a
new fight took over), 0.7 stalled marches, 4.8 parties dissolved (too few members, no objective, or a
house recall). The retreat-only ablation fell back 1.1 times per game. Per-unit repair orders for party
members (onDamage calls, not distinct units): about 200 per game in every Blob variant.

An earlier pilot of the first build (leaders waiting for the rear on a leash instead of legs; 36 games,
one house pair per seed) scored 0.389 (0.22-0.56): the waiting leaders blocked the units behind them
(6 stalled marches in one instrumented game), which is why the march became leg by leg.

**What the games show.** The blob does what was asked of it - twice as many members are with the main
body at first contact (50% against 22%), the arrival spread drops from 100 to 71 s, the fast units stay
with the slow ones (probe: the largest fast-member lead on the march 4 tiles against 8 for plain Hunt)
and more of the wave lives (71% against 65% after 5 minutes) - but it does not win fights. Contact comes
21 s later (the gather and the slowest member's pace), and the first-minute trade is worse (2.89 against
3.67 killed per lost): marching together does not make the members engage together (11% engage within
10 s of contact, None 8%), because after the release every member is an engine hunter again and walks
in at its own speed, while None's waves have been fighting (and killing harvesters and outposts) for
longer. Removing the tactical retreat does not rescue the blob (0.343, the intervals overlap), and the
tactical retreat on its own is neutral (0.486): it fires about once a game and replaces a recall that
was also rare. On Brutal the blob is not separable from None in 36 games (0.444, 0.28-0.61).

**Decision: QuantBot `WaveFormation` stays None.** Blob's interval is below 0.5 on Hard (0.22-0.39), so
the condition for making it the default (Hard interval above 0.5 and not worse on Brutal) is not met.
Blob stays available as a config value (`WaveFormation = Blob`) with its ablation switches for further
work; nothing else in the game changes with None. Per-unit repair runs stay as they are: the data give
no reason to replace them (the retreat-only variant, which keeps them, is level with None, and the
repair counter could not be tied to wins or losses in this round).

### Second round: Blob variants (36-game screen, tag `pilot2`, commit `57d2b973`)

Test-only bits `QuantBot::blobVariantOverride`, duel label flags after `+`: **v** = vehicle pace (foot
infantry neither sets the pace nor holds a gather or leg; 75% quorum instead of 90%), **r** = release
only on a real fight (a building, or visible enemy fighters within 10 tiles of the contact worth at
least a quarter of the party; a lone scout or a single hit is shot by the three nearest members while
the blob marches on), **k** = a member below 60% health stays with a party that has not met a fight yet
instead of driving to the repair yard. Also fixed for every Blob variant: a damaged party member is no
longer pulled back to the squad rally by the old "rotate damaged units" rule. Hard, sizes 128/160/192,
seeds 7101-7606, one house pair per seed, both sides (36 games; 15 games of the last two pairings
failed on the 1,800 s wall limit with 7 parallel jobs and are left out, so `blob+vrk/noretreat` has 21).
Report: `docs/formation-results/blob/report-pilot2.md`.

| Candidate v None (Hard) | Games | Score | 95% CI | Contact s | Engaged ≤10 s | Killed / lost 60 s | Releases / game |
|---|---|---|---|---|---|---|---|
| blob (round 1, 108 games) | 108 | 0.306 | 0.22-0.39 | 54 | 11% | 2.89 | 17.8 |
| blob+v | 36 | 0.417 | 0.25-0.58 | 71 | 11% | 3.50 | 19.4 |
| blob+r | 36 | 0.361 | 0.22-0.53 | 54 | 9% | 2.94 | 8.6 |
| blob+vr | 36 | 0.361 | 0.22-0.53 | 71 | 11% | 2.75 | 8.4 |
| blob+vrk | 36 | 0.347 | 0.21-0.50 | 67 | 11% | 2.32 | 9.4 |
| blob+vrk/noretreat | 21 | 0.333 | 0.14-0.52 | 58 | 10% | 2.64 | 8.8 |

None of the variants beats None; none is separable from the first Blob either. Vehicle pace brings the
first-minute trade level with None (3.50 against 3.51) but still reaches contact later. Releasing only on
a real fight halves the release/reform churn (19 to 9 per game) but trades worse: the scouts it ignores
are what None's waves kill early. Keeping damaged members makes the trade worse still. The members still
engage piecemeal after the release (9-11% within 10 s, None 8%). **Decision unchanged: `WaveFormation`
stays None**; Blob and the variant switches stay opt-in/test-only.

**Open (handed on):** a coordinated engage after the release (all members converge on the threat centroid
at the blob's pace before free combat), a concave on the last leg, consolidating near the enemy then
attacking all at once, planner shapes for the approach only, attack-then-reorganise, an engage decision by
local strength, and focus fire. A new variant is one bit in `QuantBot::blobVariantOverride`
(`include/players/QuantBot.h`), its rule in `src/players/QuantBotBlob.cpp`, and a flag letter in
`tests/ai/formation-duel.inc` (`sidesFromEnv`); labels then run as `--pairs "blob+<flags>:none"`.

## Form up where they stand, save 9861 / protocol 75

Stefan: "when selecting men and then setting a formation, the troops should move into that
formation".

- **What triggers it:** with 2+ of the local house's ground units selected, a shape button of
  the unit panel (Box, Combined arms, Line, Column), `Ctrl+Shift+F`, or `Ctrl+F` switching
  formations on (`Game::chooseFormation`, on). **Off moves nobody and sends nothing.** The
  shape is remembered for later move, attack-move and patrol orders as before.
- **Who:** the selected, respondable, eligible units (`Game::issueSelectedFormUp`); harvesters
  never (they keep their order), aircraft never. Units in a fight re-form too: the order is forced.
- **Where:** `FormationPlanner::planInPlace` lays the shape out around the members' integer
  centroid (`centroidOf`), or the nearest tile there that the anchor member can stand on (the
  MCV, else the first vehicle, else the first member, by object id). The MCV takes the anchor.
  It is always the shape itself: a tight group does not keep its own shape in the Box, as it does
  on a move. Every other rule is `plan()`'s, and the slots are the same as a move's in that
  direction, translated onto the group (`formation_planner_tests`, `[inplace]`): sonic units in
  front with nothing in their lanes, siege tanks with the front armour, harvesters left out,
  slots resolved by the same ring search.
- **Facing** (`Game::formUpFacing`), the first that applies:
  1. the way the members were last sent: each member's formation facing while it marches in
     one, else the heading of this player's latest replacing move, attack-move, patrol, attack
     or formation order for it (`Game::orderedHeadings`, written by `rememberOrderHeading` from
     `CommandManager::addCommand`; client-side UI memory, never saved or sent, so it is empty
     after a load). The 8-way direction of the sum of those unit steps; a form-up records its
     own facing, so pressing another shape keeps it;
  2. towards the nearest enemy ground unit (not a sandworm) or structure the local team can see,
     from the centroid (squared distance, ties to the lower object id);
  3. the members' average body heading (sum of their drawn-angle steps);
  4. north.
- **How they move:** one `CMD_UNIT_FORMATION_MOVE` per member with the shared group token, the
  facing, and the new order kind **FormUp** (3). The executor joins the formation as for a move
  (forced, `Resume::ForcedMove`, the group's pace cap) and makes the slot the member's form-up
  tile (`UnitBase::startFormUp`), so the group is in its form-up phase from the start: members
  walk straight to their slots at their own speed and route round each other, the march begins
  once all stand on them (or cannot get closer) and ends at once. Why not the plain formation
  move: with the march rules, the probe's group re-arranging itself on the spot gridlocked -
  members waited for each other's sonic lanes (`stepEntersSonicLane`), and without a facing
  they waited behind marching members that themselves stood waiting (`marchesWith`), in a ring.
- **Stepping aside** (`FormationOrders::update`, `stepAsideForFormUp`, any form-up): units cannot
  swap tiles, so in the form-up phase the probe's Box still left a launcher and a quad on each
  other's tiles for good. Now a forming member standing still off its tile, whose tile holds a
  member of its group that stands still and is not in place either, has that member step aside
  when it has given its own walk up or when the two are in a ring of members on each other's
  tiles (followed up to 16 members): it moves to the nearest free tile within 3
  (`kStepAsideTiles`) that is no member's form-up tile, the other walks on, and the one that
  stepped aside walks back to its slot when the group marches. A member walking aside is not
  held (`March::Recover`). Only walking members (`Resume::Move`, `ForcedMove`) step aside; AI
  hunters and attack-move walks keep their orders. Derived from live state each cycle, in
  unit-list order: deterministic, nothing saved. The other probe steps (march, save, combined
  arms, hunt, block march) measure identically with and without it.
- **Protocol 75 / save 9861:** a 74 peer refuses kind FormUp and does not step aside; a 9860 build
  must not load a save or replay that may hold such commands. The save layout is unchanged.
- **Tooltips** of the shape buttons say "forms the selection up where it stands".
- **Verified** by `formation_movement_probe` step 9 (log line `FORMATION_PROBE_FORMUP`): a
  scattered group of 2 sonic tanks, 3 tanks, a siege tank, 2 launchers, a quad, a trike, a
  trooper, an MCV and a harvester. The facing rules on real units (bodies east → east; half
  west, half east → north; an enemy tank in sight to the south → south; a right-click east →
  east). Off issues nothing. The four buttons through `FormationBar`'s real click handler and
  `Ctrl+Shift+F` through `Game::handleKeyInput`: one FormUp command per member to its planned
  slot facing east with one token, none for the harvester, the MCV on the centroid, every member
  on its slot (Box 29.8 s from the scattered start, Combined arms 19.4 s, Line 12.1 s, Column
  11.4 s, Box again by hotkey 30.5 s), no member in a sonic lane in any formed shape, the line one
  rank deep, the column narrow and deep, two scouts ahead in Combined arms, the harvester never
  in a formation and gone on its own order. Screenshots `formup-before.png`, `formup-box.png`,
  `formup-combined.png`, `formup-line.png`, `formup-column.png`, `formup-hotkey-box.png` in the
  probe's output directory. `game_command_regressions` checks the FormUp kind on real units
  and refuses unknown kinds; `formation_planner_tests` `[inplace]` checks `planInPlace`.

## Formation research (branch `feat/formation-research`)

Stefan: "you don't need to keep mine default ... test different styles of formations, maybe do
some online research. come up with different formations, keep the best 5. do extensive play
testing on different sized maps." This section is the research, the candidates, the round-robin
and the choice; it supersedes the older sections below where they disagree.

### Sources and ideas

- **Age of Empires II / IV** ([AoE wiki, Unit formation](https://ageofempires.fandom.com/wiki/Unit_formation)).
  AoE II: *Line* (melee in front, ranged behind; the default), *Box* (weak units in the centre,
  protected from every side), *Staggered* (units farther apart against siege splash) and
  *Flank* (two subgroups that try to surround the enemy). AoE IV: default rectangle about twice
  as wide as deep, *Line* one or two deep (spreads ranged fire, limits pass-through hits),
  *Wedge* (one unit at the head, each row two more), *Staggered* (spacing and offset rows
  against area damage), *Column* (ships). All members move at the slowest unit's speed and
  only keep formation within a few tiles of each other - the same rules this planner uses.
- **US Army FM 3-21.71, ch. 3, mounted formations** ([GlobalSecurity](https://www.globalsecurity.org/military/library/policy/army/fm/3-21-71/ch3.htm)).
  *Column / staggered column*: fast and easy to control, flank fire excellent, front fire
  limited. *Wedge*: for unclear situations and likely contact - front fire excellent, flanks
  good. *Line*: the assault - maximum fire to the front, little depth, poor flanks. *Echelon*:
  covers one flank of a larger force. *Coil / herringbone*: stationary all-round security.
  Platoons are combined inside larger shapes ("wedges within a company vee").
- **Total War** ([Rome II Encyclopedia, formations](https://r2encv2.totalwar.com/en/manual/single-player/0081_enc_page_battle_play_phase_conflict_army_formations/index.html)).
  Sorted lines by unit type, *Crescent* (the line bows inward to draw the enemy into the centre
  and encircle it), *Spear point* (heavy in front, light on the flanks), *Wedge* (stab through
  a line), *Skirmish* (a missile screen in front), cavalry on the flanks; units keep their
  shape and spacing while carrying out orders.
- **StarCraft II, the concave** ([Game Design Scrapbook, "Micro for Noobs: The Concave"](http://gamedesignreviews.com/scrapbook/micro-for-noobs-the-concave/)).
  Ranged units fight best in an arc at weapon range around the enemy, all of them firing; a
  "pile-up" lets only the front few fire. Our Vee is the concave for a marching group.
- **Company of Heroes 2** ([manual](https://www.feralinteractive.com/en/manual/companyofheroes2/latest/steam)):
  vehicle armour is weaker at the sides and the rear - lead with the armour, keep the flanks
  covered. (Dune Legacy has no facing armour; the lesson that remains is armour in front.)
- **Beyond All Reason** (the Total Annihilation / Supreme Commander lineage,
  [Move in Formation](https://www.beyondallreason.info/commands/move-in-formation)): keep the
  formation and move at the slowest unit's speed. No primary source for Supreme Commander's own
  formation templates or for a Command & Conquer formation move was found in this round.
- **Dune II / Dune 2000 / Emperor** ([Dune RTS Wiki, Sonic Tank (Dune II)](https://dunerts.wiki.gg/wiki/Sonic_Tank_(Dune_II))):
  the sonic blast damages every friendly and enemy unit it touches and passes through objects,
  so nothing of ours may stand ahead of a sonic tank - the sonic-lane rule every candidate keeps.
  No formation templates of their own were found for these three games in this round.

### Candidates (ten, all in `FormationPlanner` during the research)

Every candidate keeps every rule of the planner: a pure, deterministic, integer function of
the members (any order), sonic units only where nothing but a sonic unit of their own type
stands in their lane, siege tanks with the front-line armour, harvesters left out, MCVs on the
target in the middle of the middle row, and every slot resolved to a usable, unique tile by the
ring search. Diagrams: 16 units (2 sonic tanks `S`, 3 tanks and a devastator `H`, 2 siege
tanks `G`, 3 quads/trikes `l`, 4 launchers `r`, a trooper squad `i`), marching up the page.

- **Box** (doctrine: box / coil): rows by role, armour, ranged, infantry; a tight group keeps
  its own shape.
- **Combined arms** (spear point; traveling overwatch): armour front row, ranged behind, light
  vehicles on the flanks, the fastest 1-3 as scouts 4-6 tiles ahead.
- **Line** (line abreast; AoE/Total War line): up to 16 abreast, one or two ranks.
- **Column** (column / staggered column): 1-3 abreast, deep.
- **Wedge** (wedge; AoE IV wedge): an arrowhead, one rank further back per column out; the
  front spots go to armour (siege tanks first), the inner back to ranged units.
- **Vee** (vee; Total War crescent; StarCraft concave): the flanks lead by one rank every two
  columns from the second column out, the centre (with the sonic units) hangs back.
- **Echelon** (echelon right; refused right flank): the left flank leads, one rank every two
  columns, up to 12 abreast.
- **Staggered** (AoE staggered; dispersion against splash): the box's rows two tiles apart,
  units two columns apart, alternate rows offset by one column - no two units adjacent.
- **Diamond** (diamond; all-round security): rows growing by two from a point to a waist and
  back; armour round the outside front first, light vehicles on the widest flanks, ranged units
  and infantry inside.
- **Pincer** (AoE II flank; double envelopment): the row layout split into two wings with a
  gap of about half a wing's width between them; MCVs stand in the gap.

```
Box          Combined     Line              Column  Wedge     Vee       Echelon       Staggered    Diamond  Pincer
H.r.G.l.H.S  ll...l       HiHrrrGSlGlHlHHr  SG      ...S...   G.....G   GG..........  .H.H.S.G.G   ..S..    HHGS...+...GHHH
....+......  ......                         HG      ..GGH..   HHHSHHl   ..HH........  ..........   .GlG.    irrr.......lllr
i.r.l.H.H.r  .HHSGG                         HH      .HHHHl.   .rllrr.   ....HS......  l.l.H.H.H.   HrlrH
...........  ..HHH.                         HH      rl..lrr   ...ri..   ....HHll....  ..........   HrriH
H.r.G.l....  ..rrrr                         ll      r.....i             ......lrrr..  .r.r.r.l.r   .lH..
             ...i..                         rl                          ..........ri  ..........
                                            rr                                        ....i.....
                                            ir
```

(Combined arms: 4 empty rows between the scouts and the front are left out. Box: a tight
group keeps its own spacing; `+` is the target where no unit stands on it.)

### The round-robin (`tests/ai/formation-duel.py`)

- **Game:** a Random Skirmish map from the real `SkirmishGenerator` (multiplayer 2 starts,
  *Mirrored starts* = point-symmetric terrain, vanilla, tech 9, Medium rock and spice), both
  starts with a **Fortress** base and a **20,000-credit** army, two QuantBots of one difficulty
  (`formation-duel.inc`, through `run-campaign-balance.py --duel-map-size --duel-map-seed
  --duel-sides --local-spectator`). Each side's attack waves march in its own shape: the
  test-only `QuantBot::waveFormationOverride` (not saved, not hashed, never set by the game);
  production keeps one `WaveFormation` config value. Everything else is the real game.
- **End:** one house eliminated (`House::isAlive`), or the time limit (40 game minutes).
- **Pairing:** every pair of shapes on every map size and seed, twice: the two shapes swap
  houses, the match seed, map and start assignment stay the same, so house and start
  differences cancel within the pair. Houses per seed: Atreides-Harkonnen, Ordos-Atreides,
  Harkonnen-Ordos (sonic tanks, devastators and deviators all appear).
- **Score:** 1 for a win, 0 for a loss; a time-out is adjudicated on the unit trade (the side
  whose killed unit value is ahead by 1,000 credits or more wins it, else 0.5). The strict
  score (eliminations only) is reported too. 95% intervals are bootstrap intervals over a
  shape's games (2,000 resamples, fixed seed); "by seed" is the range of the per-seed scores.
- **Metrics per game** (`games.csv`): winner, game time, each side's killed and lost unit
  value, enemy structures destroyed, structures and army left. **Per wave** (the existing
  `formation-wave-measure.inc`): value killed and lost in the first 60 s after contact,
  survival after 5 minutes, base-arrival spread, time to contact, members in formation at
  contact.
- **Runs:** one probe binary (`--prebuilt-probe`), 6 games in parallel on the 10-core Mac mini;
  each run directory is deleted when its rows are written. `--report` prints the tables below.

### Results (Hard vs Hard)

Raw data: `docs/formation-results/games.csv` (every game) and `waves-by-shape.csv` (waves summed
per round, size, seed and shape); reproduce with
`tests/ai/formation-duel.py --results <dir> --tag round1,round2 --sizes 64 128 160 192 --seeds 7101 7202 7303 7404 7505 7606 --report`.

- **Round 1** (sizes 64/128/192, seeds 7101/7202/7303) and **round 2** (128/160/192, seeds
  7404/7505/7606): the ten shapes, all 45 pairs, both house assignments, 1,620 games, 59%
  decided by elimination (median 29.8 min); most 192 games reach the 40-minute limit.
- Ten-shape ranking (adjudicated score, 95% CI, ~324 games each): Pincer 0.56 (0.51-0.61),
  Column 0.54 (0.48-0.59), Staggered 0.53 (0.48-0.59), Echelon 0.52, Box 0.51, Wedge 0.49,
  Vee 0.48, Diamond 0.47, Line 0.46 (0.41-0.51), Combined arms 0.44 (0.39-0.49). Only Pincer is
  clearly above the field and only Combined arms clearly below; places 2-8 are within noise.
- **64x64 tells nothing about shapes:** waves are in contact the moment they launch (median
  contact 0 s); in rounds 1-2 the same house won both assignments of almost every 64 pair.
- **Why shapes matter little:** a hunting formation breaks for every member at the first
  contact or the first hit (`FormationOrders::breakHunt`; in one instrumented 128 game 401 of
  458 membership ends were breakHunt, the rest QuantBot defence/re-position orders), so half
  the waves have no member left in formation when the measured contact comes; the median
  formation lifetime is 0-6 s after the 15 s form-up deadline.
- **Control (None, plain engine-Hunt waves), complete:** 360 games, 36 against each shape on the
  same maps, seeds and houses, both assignments. With None in the round-robin (1,980 games):
  **None 0.562 (0.51-0.61)**, Pincer 0.554, Column 0.543, Staggered 0.517, Echelon 0.510, Box
  0.506, Vee 0.486, Wedge 0.485, Line 0.454, Diamond 0.453, Combined arms 0.431. Against None
  directly the shapes together score **0.438 (0.39-0.49)**: formation waves lose to plain Hunt
  waves overall. Per shape (36 games each, none separable from None on its own): Column 0.61
  (0.44-0.75), Vee 0.50, Pincer 0.47, Box 0.47, Wedge 0.46, Staggered 0.39, Line 0.39, Echelon
  0.38, Combined arms 0.36 (0.19-0.50), Diamond 0.35 (0.19-0.50). No shape beats None.

### The choice (applied)

- **QuantBot default `WaveFormation = None`** (`QuantBotConfig`): plain engine-Hunt waves, as
  Stefan asked when None beats the shapes. A shape can still be set in `QuantBot Config.ini`.
- **The other AI formation moves follow the same setting** (no separate hard-coded shape any
  more; `FormationOrders::kAIShape` is gone, `Player::aiFormationShape()` reads `WaveFormation`):
  `Player::doFormationMove` / `doFormationHuntToward` default to it and give plain per-unit
  orders for None; `AIPlayer::attack` (formation move to the wave destination, then `doFormUp`),
  `CampaignAIPlayer` attack teams (formation hunt, else HUNT + forced attack as before formations)
  and QuantBot's army recall (`applyArmyPosture`: rally slots and `doFormUp`) all use it, and all
  three give the pre-formation orders with None. QuantBot's own waves use the per-player
  `waveFormationShape()` (the test-only override, else the config value). Defence scrambles are
  unchanged.
- **Kept five player shapes:** Box, Pincer, Column, Staggered, Echelon (panel and Ctrl+Shift+F
  order), plus Off. Removed: Combined arms (and all its scout code), Line, Wedge, Vee, Diamond.
  The removed shapes' names are unknown `WaveFormation` values (logged, the default is kept).
- **Stable shape ids** (`FormationPlanner::Shape`): Box 0, Column 3, Echelon 6, Staggered 7,
  Pincer 9 - Box and Column keep their released ids. Shape ids live only in the settings
  (`General/Formation Shape`) and the QuantBot config; formation commands and saved units carry
  slots and facings, never a shape, so saves and replays are unaffected. A removed or unknown id
  (1 Combined arms, 2 Line, 4, 5, 8, anything else) loads as the **Box** (`shapeFromId`, pinned in
  `formation_planner_tests`). No save or protocol change beyond the merged form-up branch
  (save 9861, protocol 75).

### Fighting in formation through first contact (measured, not kept)

Stefan approved making formations fight in formation through first contact so they could beat
None. Five engagement modes were built and measured against None and against today's scatter
(the first contact or hit breaks the whole group into engine Hunt, `FormationOrders::breakHunt`).
**None of them beats None or scatter; scatter stays and the modes were removed** (no save or
protocol change remains: save 9861, protocol 75).

- **Common rules of every mode:** members of a hunting formation target like HOLD (only what
  the weapon reaches, never chase) on the march and in the fight, keep Hunt and their membership;
  the group engages when a member has an enemy in weapon range or is hit, re-marches to its
  slots after 3 s with nothing in reach and no hit, and breaks into free Hunt when an enemy unit
  is within 2 tiles, a third of its members is lost, it is hit from beyond every member's reach
  (at once), after 8 s of hits with nothing in reach, or after 30 s. The facing turns towards the
  threat centroid for the sonic lanes; a sonic member does not fire through its own members;
  QuantBot launchers in such a formation do not kite.
- **Hold:** the group halts on contact and fires from its places (leash 0; `hold2` a 2-tile
  leash, walking back afterwards). **Advance:** the group marches on at its pace, a member with a
  target in weapon range stops to fire. **Deploy:** halt; a member with nothing in weapon range
  closes up to 6 tiles beyond it and fires from there. **Arc:** on contact the group spreads into
  one battle line across the threat through its member nearest to it (one tile per member, left
  to right as they stood, so every member has a free line of fire and no sonic lane is blocked),
  then fights like Deploy. Hold, Advance and Deploy are in commit `d09813f1`; Arc was measured from
  an uncommitted build on top of it.
- **Design:** the pilot of the full plan - Hard, the Random Skirmish duel (`formation-duel.py
  --pairs`), sizes 128/160/192, seeds 7101-7606, one house pair per seed, both side
  assignments, 36 games per pairing, Column and Box (the best and a middle shape of the round-robin).
  The full 108-game-per-pairing run (all five shapes, `--all-houses`) and the Brutal sample were
  to follow only for a mode whose 95% interval cleared 0.5 against None or scatter; none did.

| Pairing (first side's score) | Games | Score | 95% CI | Eliminations W-L / time-outs | Value margin / game |
|---|---|---|---|---|---|
| column/hold v none | 36 | 0.361 | 0.22-0.53 | 7-14 / 15 | -24,568 |
| column/hold2 v none | 36 | 0.278 | 0.14-0.42 | 6-15 / 15 | -20,469 |
| column/advance v none | 36 | 0.278 | 0.14-0.44 | 4-17 / 15 | -29,097 |
| column/deploy v none | 36 | 0.333 | 0.19-0.50 | 6-16 / 14 | -14,720 |
| column/arc v none | 36 | 0.361 | 0.19-0.53 | 5-11 / 20 | -13,662 |
| column/scatter v none | 36 | 0.444 | 0.31-0.61 | 7-13 / 16 | -2,518 |
| box/hold v none | 36 | 0.292 | 0.14-0.44 | 5-12 / 19 | -16,157 |
| box/hold2 v none | 36 | 0.389 | 0.22-0.56 | 5-14 / 17 | -2,313 |
| box/advance v none | 36 | 0.278 | 0.14-0.42 | 3-18 / 15 | -17,584 |
| box/deploy v none | 36 | 0.222 | 0.11-0.39 | 4-16 / 16 | -21,392 |
| box/arc v none | 36 | 0.417 | 0.25-0.58 | 5-14 / 17 | -7,563 |
| box/scatter v none | 36 | 0.444 | 0.28-0.61 | 6-15 / 15 | -8,219 |
| column/deploy v column/scatter | 36 | 0.486 | 0.33-0.65 | 9-9 / 18 | -1,324 |
| column/arc v column/scatter | 36 | 0.514 | 0.35-0.67 | 8-10 / 18 | -2,061 |
| box/deploy v box/scatter | 36 | 0.375 | 0.22-0.53 | 8-12 / 16 | -7,835 |
| box/arc v box/scatter | 36 | 0.500 | 0.33-0.67 | 9-9 / 18 | -1,449 |

Median game length 37-40 minutes in every pairing (most 192 games reach the 40-minute limit).
Waves of the first side (every wave of the 36 games; first-minute value from contact; survival
5 minutes after launch), with None's waves in the same games for comparison:

| Mode (Column / Box) | In formation at contact | In formation +10 s | Killed / lost, first minute | Survival | None's waves: killed / lost, survival |
|---|---|---|---|---|---|
| hold | 55% / 60% | 50% / 54% | 3.06 / 2.53 | 63% / 63% | 5.71, 68% / 6.04, 70% |
| hold2 | 56% / 54% | 52% / 48% | 2.95 / 3.04 | 64% / 63% | 5.65, 68% / 6.11, 65% |
| advance | 58% / 64% | 51% / 59% | 2.21 / 1.97 | 59% / 61% | 5.87, 68% / 5.65, 70% |
| deploy | 58% / 65% | 52% / 57% | 3.07 / 2.59 | 66% / 62% | 5.03, 66% / 6.56, 71% |
| arc | 51% / 64% | 45% / 55% | 3.18 / 3.05 | 62% / 65% | 5.62, 65% / 4.99, 67% |
| scatter | 45% / 54% | 20% / 26% | 3.56 / 3.03 | 63% / 62% | 4.57, 63% / 5.11, 65% |

- **What it shows:** the modes keep the formation through the fight (about half the members still
  in it 10 s after contact against 20-26% with scatter), but that does not win fights: the
  first-minute trade is no better than scatter's and well below None's. A halted deep group fights
  with its front rows only, and its short-range armour stands under longer-range fire; closing in
  (Deploy) or spreading into a line (Arc) recovers scatter's level, not more. Engagements ended
  53-73% by the re-march and 19-36% by an enemy within 2 tiles; the other break rules fired in
  1-6% each.
- **Decision:** QuantBot `WaveFormation` stays **None**; player hunting formations (attack-move,
  patrol) keep the scatter; the engagement code, its saved state and its test-only switch are gone.
  Kept from this work: the duel harness options `--pairs` (exact shape pairs and a per-pairing
  report) and `--all-houses` (every seed with all three house pairs), and the wave metric
  `in_formation_10s`.
- **Raw data:** `docs/formation-results/engagement/games.csv` and `waves-by-shape.csv` (tags
  `pilot` and `pilot2`; shape labels carry the mode as `shape/mode`; the `engagements`, `resumed`
  and `broke_*` columns are the removed build's per-house engagement counters).

## Formation fixes, save 9860 / protocol 74 (after the 1.0.839 play-test)

These rules supersede the older sections below where they disagree.

- **Siege tanks are front-line armour:** Box role Armour, class Heavy for the other shapes, and first
  among the heavy armour (then other heavy armour, then light vehicles) so they hold the front row
  when it cannot take all of it.
- **Harvesters never join** (`joinsFormations`): no slot, no membership, no pace. Selected with a
  group they keep their ordinary order (a plain move to the clicked tile).
- **MCVs are protected** (`isProtected`): the MCV takes the middle of the middle row
  (`rows.size() / 2`, behind the front row from two rows) on the target itself; the rest of that
  row moves aside, sonic cells next to it. A Box with an MCV always forms the box.
- **Sonic rule:** only a sonic unit of the same type may stand in a sonic unit's lane
  (`mayStandInLane`). The damage code (`Bullet::update` sonic branch, `Map::damage`,
  `SonicTank::handleDamage`, `SonicTrike::handleDamage`) hurts every unit in the wave, friends
  included; a sonic tank ignores damage whose shooter is a sonic tank, a sonic trike damage whose
  shooter is a sonic trike, and neither is immune to the other type or to a wave whose shooter has
  died. Damage rules are unchanged. Every shape, the slot fallback, the form-up tiles and the march
  check (`stepEntersSonicLane`, from the march on) keep the rule.
- **One speed.** The pace is the slowest member's reference speed, infantry included, and that
  member's type is the pace unit (saved). Members that joined this cycle form up first
  (`formUpTiles`: the shape centred on the group, moved forward until its foremost slot is level
  with the foremost member; skipped for a march under `kFormUpMinMarch` = 8 tiles): they walk at
  full speed to their form-up tiles and wait there; once every member stands on its tile (or cannot
  get closer) the group marches and each member's order towards its slot is given again
  (`FormationOrders::Resume`). On the march every capped Dynasty step takes at least as long as
  the pace unit's step on that tile (`UnitBase::formationPaceStepLength`); a member more than
  `kRecoverLagTiles` = 1 further from its slot than the leader runs at full speed, and members more
  than `kHoldLeadTiles` = 2 ahead of the last member wait (`marchState`), never while a member waits
  at a sonic lane. Marching members plan routes through and wait behind other marching members of
  their formation (`canPathThrough`, `marchesWith`), so rear rows do not detour round front rows.
  Applies to player moves, formation hunts until contact, and AI formation moves.
- **Saved (9860):** form-up tile, resume kind, pace unit type (13 bytes after the 9859 fields).
- **Measured** (`formation_movement_probe`, Habbanya-Autumn, Combined arms, 2 sonic tanks, 2 siege
  tanks, 2 tanks, 2 launchers, MCV, 2 quads, 2 troopers and a harvester, 384-cycle windows while the
  block's centre is 35-75% of the way): before (1.0.839) speed spread 25.6%, shape error 10.0 tiles,
  stretch 7.0 tiles beyond the planned depth; after 10.3-14.2%, 1.2-1.4 tiles, 2.2 tiles; MCV with
  members ahead, behind and on both sides on every steady cycle; harvester outside the formation;
  no unit in a sonic lane on the steady march. Form-up of that scattered group takes about 28 s
  (the troopers walk farthest).
- **Colonist escort ring** (`QuantBot::colonistEscortPlace`): the MCV keeps its own colonisation
  order; its escorts take places relative to its tile and heading (the two front corners 3 tiles
  ahead, then behind, then the flanks; sonic escorts only front-row places), in Area Guard with the
  guard point on the place, so they fight on contact and re-form. Keeping the places
  (`QuantBot::escortColonist`):
  - Upkeep runs every cycle while a colonist drives (`QuantBot::keepColonistEscortsInPlace`, from
    `QuantBot::update` before the unit-pass schedule), so a place that moved with the MCV's tile is
    followed at once, not up to 50 cycles later; it is derived from live state only (the per-cycle
    escort set is derived afresh by the unit pass afterwards).
  - An escort already walking to within 2 tiles (`kEscortPlaceSlack`) of its place keeps its route:
    every new order resets the path and the unit waits for a new one.
  - A new order aims where the place will be one MCV tile further along the heading; an escort
    standing there waits for the MCV to come up.
  - An unpassable place (terrain, structure, or a unit standing there - a route to an occupied
    tile fails and the engine waits ~500 cycles before retrying) becomes the nearest passable tile
    on the same side of the MCV, at least two tiles to that side, so the MCV's one-tile weaves do
    not swap the escort's side or put it in the MCV's lane.
  - No formation pace: an escort that was in a formation leaves it, so nothing caps it below the MCV.
  Measured in `quantbot_mcv_deployment` (two tanks): the colonist deploys; escort on 3+ sides on
  3229 of 3713 march cycles (86%), 4 sides on 480 (12%); before 1636/3008 (54%), 228. The march is
  longer (3713 vs 3008 cycles): on that fixture's structure-striped rock the right-hand escort has
  to cross the MCV's lane to reach its side, and the MCV waits for it.

## Shapes (`FormationPlanner::Shape`)

Every shape faces the 8-way direction from the group's centroid to the target
(`Plan::forward`), and every slot is validated as before (inside the map, standable, unique,
deterministic ring-search fallback).

- **Box** (default, index 0): today's layout (below). A tight group keeps its own shape
  unless that leaves someone in a sonic lane; then it forms the box.
- **Combined arms** (1), used by the AI: sonic units and tanks (tank, devastator, flame
  tank, other heavy armour) in the front row, further tanks in rows behind it; launchers,
  siege tanks, deviators and rocket trikes behind them; infantry, then harvesters/MCVs at
  the rear. Quads, trikes and raider trikes stand on both flanks of the fighting rows.
  The fastest light vehicles go ahead as **scouts**: 1 below 8 members, 2 below 16, 3 from
  16 (`scoutCount`), never more than there are light vehicles, `scoutDistance` = 4, 5 or 6
  tiles (10 and 20 members) ahead of the front row, spread across the front and outside
  every sonic lane. Siege tanks count as ranged (long-range artillery), by judgement.
- **Line** (2): every cell in the order sonic, heavy, light, ranged, infantry, support,
  up to 16 abreast (`kLineMaxWidth`) per rank.
- **Column** (3): the same order, 1 abreast up to 3 cells, 2 up to 16, then 3
  (`columnWidth`).

Within a row, sonic cells take a contiguous block in the middle and the others keep their
current left-to-right order either side of it.

## The sonic-lane rule (all shapes, player and AI)

`inSonicLane(sonic, other, forward)`: `other` is strictly ahead along `forward` and at most
one formation column to either side (1 tile straight, the diagonal column pitch diagonally).
Sonic tanks and sonic trikes (`isSonic`) fire a wave that hurts friends, so:

- All sonic units stand in the front row, which widens to hold them all; nothing is ever
  planned ahead of their lanes (scouts included).
- Slot resolution resolves sonic units first; a relocated slot is accepted only if it is
  out of every sonic lane (and a sonic unit only level with any sonic unit whose lane it
  shares). This holds whenever a usable tile exists within `kMaxSearchRadius`; a member
  with none stays where it is, as before.
- **On the march** (`FormationOrders::stepEntersSonicLane`, `UnitBase::navigate`): a member
  of a formation with a facing that would step into a marching sonic member's lane (judged
  on the tiles each member stands on and moves to), or a sonic member whose step would
  bring someone into its lane, waits like for a moving blocker. A pair that already shares
  a lane does not stop either unit. Formations without a facing (`doFormUp`, protocol-71
  replays) keep only the planned slots.

## Hunting in formation

A hunting formation (`UnitBase::isFormationHunt`, saved) marches at the group's pace like
any formation. As soon as any member makes contact (its target is an enemy within
`huntTowardRange()`, found by `FormationOrders::update()`) or is hit by an enemy
(`UnitBase::handleDamage`), `FormationOrders::breakHunt` ends the formation for every member
in the same cycle: pace and slots are dropped, and each fighter without a target attacks the
contact if its attack mode keeps a target that far, otherwise closes in on it, fighting
whatever it meets (`engageFormationContact`). Attack-move leashes and Hunt take over from
there; an attack-move or patrol resumes its walk afterwards. A hunting member also leaves
on reaching its slot (a patrol's next leg runs without pace). A plain formation move keeps
today's behaviour: only a member in contact leaves.

## Controls

- **Unit panel:** with 2+ of the local house's ground units selected, a row of five buttons
  under the command buttons (`FormationBar`): Off, Box, Combined arms, Line, Column. The
  current choice is held down; tooltips name the hotkeys. A click calls
  `Game::chooseFormation` and applies to the next move, attack-move or patrol order. A shape
  also forms the selected units up in it where they stand ("Form up where they stand" above);
  Off leaves them where they are.
- **`Ctrl+F`** switches formations on or off; **`Ctrl+Shift+F`** picks the next shape (and
  switches formations on). Switching on or picking a shape forms the selection up like the
  buttons. Plain `F` still selects the next factory. Both show a ticker line.
- Persisted in the ini as `General/Formation Moves` and `General/Formation Shape` (0-3).
- **Right-click / M move:** a formation move in the chosen shape (`CMD_UNIT_FORMATION_MOVE`).
- **Attack-move (A) and patrol (Q):** with formations on, a group order that replaces the
  orders is a hunt in formation (`CMD_UNIT_FORMATION_HUNT`): attack-move click = hunt in
  formation towards that point. Shift-queued orders keep their slots without pace.

## Planner (`include/FormationPlanner.h`, `src/FormationPlanner.cpp`)

The planner is pure and deterministic, and uses integer and FixPoint arithmetic only. It takes
these inputs:

- the members, each with object id, item, current tile, role, reference speed and an infantry
  flag;
- the target tile;
- the map size;
- a `CanStand(member, tile)` query (terrain and structures, never units);
- the infantry capacity per tile (`NUM_INFANTRY_PER_TILE` = 5).

and the shape. It returns one slot per member, sorted by object id, the group speed:
`groupPace()`, the slowest *vehicle's* reference speed (infantry set it only for an
all-infantry group), the facing (`forward`) and the scouts.

- **Member order:** members are sorted by object id before any other step, so the plan does not
  depend on the order the caller lists them in.
- **Coherent group:** a group is coherent when every member is within 6 tiles (Chebyshev) of the
  integer centroid. Its relative offsets are kept, but each axis is compressed so the gap
  between neighbouring values is at most 2 tiles. The result is recentred on the target.
- **Box:** any other group (or a tight one that would block a sonic lane) forms a box. It faces the 8-way direction from the centroid to the
  target. The width is `ceil(sqrt(1.5 n))`.
  - Rows are filled in this order: armour (sonic units first, all in the first row, then
    tank, quad, trike, raider trike, devastator and others), then ranged (launcher, elite launcher, siege tanks, deviator, rocket
    trike), then infantry, then support (harvester, MCV).
  - Each role starts a new row, except infantry. Infantry first fill the free flank cells of
    the row in front of them, then take rows of their own.
  - Short rows are centred.
  - Within a role, members keep their current left-to-right order, so their paths do not cross.
  - The box is centred on the target, with the first row ahead of it.
- **Tiles:** every vehicle gets its own tile. Infantry share a tile with other infantry, up to
  the capacity.
- **Invalid slots:** a slot that is off the map, cannot be stood on, already taken or full is
  replaced by the nearest valid tile. The search uses Chebyshev rings out to 32 tiles; within a
  ring the smallest squared distance wins, and ties go by ring order. Armour is resolved first.
  If nothing valid is found, the member stays where it is.
- **Small groups:** groups of 0 or 1 return `formation = false` and a plain move to the target.
- **Catch-up rule:** `runsAtFullSpeed(lag, centroidDistance)` returns true when `lag > 5` or
  `centroidDistance > 10`.
- **Group ids:** `makeToken(cycle, index)` is 20 bits of the game cycle plus an index of 1 to
  15, and is never 0. `makeGroupID(playerID, token)` puts the issuing player in the top byte,
  so two co-op players on one house can never share a group.

## Engine side (`include/FormationOrders.h`, `src/FormationOrders.cpp`)

- `planGroup(units, target, shape)` is **the single planning entry point**. It plans eligible units
  only: active ground units on the map, never aircraft or sandworms. `canStand` excludes tiles
  with a structure, and mountains for anything except infantry. Integration should route the
  RTS-controls attack-move, patrol and queued-waypoint orders (`Game::issuePositionOrder`)
  through this function.
- `join(units, groupID, slots, hunt, facing)` sets membership, the hunt flag, the facing, and gives the whole group the slowest pace,
  including members that joined through earlier per-unit commands.
- `revalidatedSlot(unit, slot)` handles a slot that has become unusable, for example because a
  building went up. It falls back to the nearest usable tile in the same deterministic way on
  every peer.
- `update()` runs every cycle in `Game::processObjects` before any unit moves.
  - A member leaves its formation when it is inactive, picked up or dead, or when it has an
    enemy target within `huntTowardRange()` (contact).
  - It also leaves when it is idle: not moving, no movement intent, no target, no attack
    position, and no pending target or path request. This covers arrival.
  - A group with only one member left is dissolved.
  - Each remaining member gets its catch-up flag. The lag is its remaining tiles to its slot
    (or to its destination if it has no slot) minus the group minimum. The centroid distance is
    measured to the group's mean tile.

## UnitBase state (saved, gates 9857 and 9859)

- **Saved fields** (contiguous, appended after the hunt-toward fields):
  - `formationGroup` (Uint32)
  - `formationSlot` (Coord; invalid means the member keeps its own order and only matches the
    group's pace)
  - `formationSpeedCap` (FixPoint)
- **9859, appended:** `formationHunt` (bool) and `formationFacing` (Uint8 facing code,
  0 = none). A 9857 save loads every formation as a plain march without a facing.
- **Not saved:** `formationCatchUp` and the per-cycle set of formations with sonic members is recomputed at the start of every cycle, so a loaded game
  and a spectator see the same value.
- **Old saves:** saves older than 9857 load with no unit in a formation.
- **Reference speed:** `getFormationReferenceSpeed()` is the unit's speed on open sand,
  undamaged, unloaded and off-road. For Dynasty units that is
  `maxSpeed * step(sand) / step(255)`. Examples: tank 0.80, launcher 1.04, siege tank 0.64,
  quad 1.28, trooper 0.48, soldier 0.24, devastator 0.32.
- **Speed cap:** the realized speed of each Dynasty step (`step * 0.08 * scale`) and the legacy
  `getTerrainAdjustedSpeed()` are both limited to the cap. The cap does not apply while the
  catch-up flag is set.
- **What ends membership:**
  - any player command that is a unit order (through the `applyMovementOrder` path, plus
    SETMODE, SENDTOREPAIR, CAPTURE and HARVESTER_RETURN);
  - any AI `Player::do*` unit order;
  - a carryall pickup (`setPickedUp`);
  - deviation, either direction;
  - the upkeep rules above.

  Engine-internal retargeting does not end membership.

## Commands (protocol 71, extended in 73 and 75)

`CMD_UNIT_FORMATION_MOVE(OBJECT_ID, X, Y, PARAMETER)` and, new in 73,
`CMD_UNIT_FORMATION_HUNT(OBJECT_ID, X, Y, PARAMETER)`. Both have 4 parameters and are
classified as acting on an owned object. `PARAMETER` (`FormationOrders::packParameter`) is
the 24-bit group token, the order kind in bits 24-27 (0 Move, 1 AttackMove, 2 Patrol, 3 FormUp
from 75) and the facing code in bits 28-31; a protocol-71 bare token reads as a Move without a
facing. The move executor refuses any kind but Move or FormUp (FormUp also makes the slot the
member's form-up tile), the hunt executor any kind but AttackMove or
Patrol, both an unknown facing (> 8). The hunt executor revalidates the slot, issues a
non-queued attack-move (`doIssueOrder`) or patrol (`doIssuePatrol`) to it and joins the
hunting group with the facing; ineligible units get the plain order.

- Commands are capped at 4 parameters, so a single command per group is not possible (see
  deviations). The client sends one command per unit, all carrying the same token.
- The executor checks ownership (`mayActOnObject`), a valid token and an on-map slot.
- It then revalidates the slot, issues a forced `doMove2Pos` and joins group
  `makeGroupID(issuer, token)`, recomputing the group's cap.
- A unit that is not eligible (for example an aircraft) gets the plain forced move.
- The command is counted as a human unit order for the QuantBot human-order lease.

## Player

- **Orders that use formations:** a right-click move (`handleSelectedObjectsActionClick`) and
  the M move cursor (`handleSelectedObjectsMoveClick`) go through
  `Game::issueSelectedFormationMove`.
- **Which units:** the formation covers the local house's selected, respondable, eligible units
  that this click simply moves. For a right click, that means no visible object under the
  cursor. At least 2 such units are needed.
- **Everything else in the selection** keeps its old per-unit action: attack or follow,
  aircraft, and structures.
- **`Ctrl+F`, `Ctrl+Shift+F` and the panel buttons:** see Controls above; choosing a shape forms
  the selection up where it stands through `Game::issueSelectedFormUp`.
- **Options → Controls** has a "Move groups in formation" checkbox. It is on by default and is
  stored as `General/Formation Moves`; the shape is `General/Formation Shape`.

## AI

Who is sent and when are unchanged everywhere. Only how a chosen group moves is changed.
Group moves with slots use Combined arms (`FormationOrders::kAIShape`), so scouts and the
sonic-lane rule apply. `Player` has three helpers:

- `doFormationMove(units, x, y, forced)` gives each unit a Combined-arms slot, a move to it
  and the formation's facing.
- `doFormationHuntToward(units, x, y)` gives each unit a hunt-toward walk to its own slot in
  a hunting formation with a facing: the first contact breaks the group into a fight.
- `doFormUp(units)` keeps each unit's existing order and only applies the group's pace (no
  facing, so no lane-keeping on the march).

All three need at least 3 eligible units. Below that, the units get the old plain order, or
nothing changes.

| Caller | What changed |
|---|---|
| QuantBot `launchGroundHunt` (custom Hunt waves, campaign waves incl. flanking) | hunting formation in the `WaveFormation` shape (default Combined arms), see QuantBot waves below; `None` keeps the old orders |
| QuantBot `scrambleWholeArmyToDefend` (1.0.832 nearest-hunt) | unchanged: no slots, no pace (defence is never slowed) |
| QuantBot `scrambleUnitsAndDefend` (district defence) | unchanged: no pace (defence is never slowed); a defence order on a wave member ends its membership |
| QuantBot `applyArmyPosture` (army gathering / recall) | the units recalled this pass take Combined-arms slots around the rally where the slot is safe assembly ground by `withdrawUnit()`'s rules (radius, not reserved, no bloom, passable, no danger) and nobody else's destination, in id order; the rest keep their assembly slot; then `doFormUp` |
| `CampaignAIPlayer` attack team | ground units (3+) `doFormationHuntToward` the chosen target's nearest point: a Combined-arms march at the slowest pace that breaks into a fight on contact, then Hunt; aircraft and smaller waves keep HUNT + forced attack; marching members are not restaged |
| `AIPlayer::attack` | `doFormationMove` (Combined arms) to the wave destination, then HUNT, then `doFormUp` |
| `SmartBot::attack` | unchanged: no pace (pure Hunt wave, same reason) |

Player orders from the RTS-controls work use the planner too: `Game::issuePositionOrder`
gives every unit its own formation slot in the chosen shape for attack-move, patrol and
Shift-queued waypoints when "Move groups in formation" is on. A non-queued attack-move or
patrol hunts in formation (pace, facing, break on contact); Shift-queued orders carry no
pace. A Shift right-click therefore queues slot moves instead of starting a formation march.

## QuantBot waves in formation

Who QuantBot sends, when, and at what is unchanged; only how a launched ground wave travels.

- **Setting:** `QuantBot Config.ini`, `[General Behavior] WaveFormation = Combined | Box | Line |
  Column | None` (code default `Combined`). The key is optional and deliberately NOT in the
  shipped `QuantBot Config.ini.default`: that file is part of every mod's effective checksum, and
  changing it makes saves from earlier builds refuse to load ("This game needs an older mod
  version"). Read with the other QuantBot settings and part of the QuantBot config hash, so every
  peer of a match runs the same shape.
- **Custom waves** (`launchGroundHunt`, Easy to Brutal): the ground members hunt in formation
  (`Player::doFormationHuntToward`) towards the closest point of the wave's objective - the
  tracked wave's front (`customWaveObjective` of the lead: the observed enemy base the strength
  survey already anchors on), else the target engine Hunt picks for the lead. First contact
  breaks the formation; from there every member is an engine hunter as before.
- **Campaign waves:** the attackers of one objective hunt in formation towards it and are sent
  at it again by `campaignControlsUnit()` when idle, as before; Hard/Brutal flankers march their
  detour as one forced formation move (`doFormationMove`) from the flank group's centre, then
  carry on as before. Harvesters, MCVs and aircraft are never members; groups below
  `kMinAIGroup` (3) and `None` get exactly the old per-unit orders (probe-checked).
- **Engine changes the measurement showed were needed** (they apply to AI hunting formations,
  `Resume::HuntToward`, so also `CampaignAIPlayer` attack teams):
  1. *Hunt kept on the break* (`engageFormationContact(enemy, hunter)`): the close-in walk was a
     plain move that left Hunt, so a broken wave stood in Guard after its first fight.
  2. *Form-up in Hunt* (`startFormUp` uses `doHuntToward` to the form-up tile; reaching it does
     not end the walk while forming): members forming up in Guard looked like idle home troops
     and QuantBot re-ordered them, which ended their membership - half the waves had dissolved
     within a second of launch (formation lifetime median 0 s).
  3. *Form-up deadline* `kHuntFormUpMaxMs` = 15 s (age read from the group token, no new save
     state): a 46-58 unit army forming up in a crowded base took 140-184 s before it marched.
  4. *Sonic lanes out of shape:* a sonic member still recovering its place (after the deadline)
     neither waits at a lane nor holds one; otherwise it and the members around it waited for
     each other for ever (the probe's 15-unit wave travelled 4 tiles in 25 s).
  5. *No stall airlift for formation members:* the 3-failed-paths carryall request ignored that
     a formation member waits on purpose; it took members out of the formation.
  6. A lone forming hunter left by its group hunts on to its slot instead of standing in Guard.

### Measurement (Hard vs Hard, vanilla, 30 game minutes)

`tests/ai/formation-wave-matrix.py` drives `run-campaign-balance.py --formation-measure
--wave-formation <shape>` (one run at a time, run directory deleted after each), maps 2P 64x64
Face Off, 2P 128x128 Habbanya-Penny, 2P 128x64 Ergun-Sahij, seeds 486409243, 1841712204,
41562270, the same seeds for every shape; `--report` prints these tables. A wave is a new
tracked dispatch with at least 3 ground members outside the enemy base area. *Contact* is the
first cycle a member has an enemy target within max(weapon, view) range, fires, or comes within
10 tiles of an enemy building. Shots: members' primary shots in the 30 s after contact. Lost /
killed: value of members destroyed, and of enemies destroyed within 10 tiles of a living
member, in the 60 s after contact. Base-arrival spread: first to last member within 10 tiles of
an enemy building (the older metric). Survival: members alive 5 min after dispatch. Seed range:
the per-run mean spread and per-run net trade (killed - lost) over the 9 runs.

Final code (mean / median per wave; 9 runs per shape; both houses use the same shape):

| Shape | Waves | Base-arrival spread s | Shots 30 s | Lost 60 s | Killed 60 s | Killed / lost | Net trade per run | Survival | In formation at contact |
|---|---|---|---|---|---|---|---|---|---|
| None | 63 | 114.3 / 110.9 | 33.1 / 22.0 | 872 / 600 | 3722 / 2820 | 4.27 | 4910..41600 | 51% | - |
| Combined | 61 | 75.7 / 66.2 | 27.4 / 19.0 | 978 / 750 | 3566 / 2575 | 3.64 | 5850..32150 | 55% | 71% |
| Box | 60 | 79.9 / 54.5 | 29.5 / 24.0 | 962 / 700 | 4135 / 3600 | 4.30 | 6510..45140 | 50% | 73% |
| Line | 62 | 71.8 / 55.1 | 26.0 / 20.0 | 942 / 550 | 3643 / 3310 | 3.87 | 6300..40940 | 58% | 63% |

Per map, killed / lost (None, Combined, Box, Line): Face Off 5.70, 8.79, 8.62, 12.37; Penny
5.16, 3.16, 5.22, 2.85; Ergun-Sahij 3.30, 3.25, 3.46, 3.55. Army value of both houses at
10 / 20 / 30 min: None 3844 / 15428 / 20527, Combined 3844 / 14822 / 21714, Box 3844 / 15081
/ 20362, Line 3844 / 15072 / 21412; 2-3 of 9 matches decided in 30 min for every shape (with both
sides on the same shape, the outcome says little about the shape).

Before/after the modifications (Combined, same seeds):

| Variant | Waves | Base-arrival spread s | Shots 30 s | Killed 60 s | Killed / lost | Survival | In formation at contact |
|---|---|---|---|---|---|---|---|
| Launch in formation only (engine as 1.0.840) | 105 | 99.0 / 90.5 | 17.2 / 11.0 | 2312 / 1395 | 4.41 | 74% | 15% |
| With changes 1-6 (shipped) | 61 | 75.7 / 66.2 | 27.4 / 19.0 | 3566 / 2575 | 3.64 | 55% | 71% |
| ... plus members already in contact at launch left out of the formation (rejected) | 64 | 108.9 / 99.9 | 24.2 / 19.0 | 3008 / 2350 | 3.28 | 58% | 83% |

Without the changes the members fell out of their formation while forming up, stood in Guard and
were re-dispatched as "new" waves (105 instead of ~62), so the wave hardly fought (17 shots,
killed 2312 per wave). The rejected variant kept more members in formation but arrived more
spread out and traded worse, so it was dropped.

**Default: Combined arms.** It narrows the arrival spread most consistently against None (75.7
against 114.3 s mean, 66 against 111 s median) and keeps more of the wave alive (55% against
51%), but it trades somewhat worse in the first minute (killed/lost 3.64 against 4.27; 17% fewer
shots), mostly on Penny, the large map. The per-run net trade ranges overlap widely
(5850..32150 against 4910..41600), so this is not clearly worse, and Combined stays the default
as Stefan prefers. Box traded best of the measured options (4.30, killed 4135 per wave, best or
level on all three maps) and is the one to switch to (`WaveFormation=Box`) if first-minute
trade matters more than arriving together; Line gave the tightest arrival but the weakest Penny
trade. Brutal vs Brutal was not measured in this round.

## Verification of the shapes branch (Mac mini, Release, `--parallel 4`)

`ctest --test-dir build -R "^(dunelegacy_tests|formation_planner_tests|formation_movement_probe|rts_controls_probe|rts_controls_tests|game_command_regressions|quantbot_whole_army_dispatch|quantbot_army_recovery|quantbot_custom_attack|menu_navigation_probe|deviation_reward_probe)$"`
passed 11 of 11 (after the wire-version pin was moved to 73; the unit suite runs 1067
cases: 1064 passed, 3 skipped).

- **`formation_planner_tests`** adds: unit classes, shape indices, scout count and distance,
  column width and facing codes; the lane geometry (straight and diagonal); Combined-arms
  role placement facing east, south and diagonally (tanks ahead of ranged, ranged ahead of
  infantry, flankers beside the fighting rows, the two fastest light vehicles exactly the
  scout distance ahead); scouts only when light vehicles exist (1 for 6 members, 3 for 30);
  Line 11 abreast in one rank and Column 2 abreast and 6 deep for 12 members, a second Line
  rank above 16; sonic tanks level, adjacent and in front in every shape, the front widening
  for 5 sonic tanks; a tight group with a tank ahead of a sonic tank forms the box; every
  shape identical over 24 member permutations with obstacles; and a property sweep of
  4 shapes x sizes 2-30 x 8 facings x {open, ~20% blocked} (1856 plans) checking that no
  slot is in a sonic lane, slots are unique, standable and on the map, and that the obstacle
  fields really relocated slots.
- **`formation_movement_probe`** (real engine, real player paths, Habbanya-Autumn):
  - Box on/off as before: spread 16.4 s on, 31.2 s off.
  - Hunting attack-move in Combined arms saved 300 cycles into the march: group, slot, pace,
    hunt flag and facing restored; all 12 reached their slots after the reload. A 9854 record
    loads without membership, a 9857 record as a plain march without a facing.
  - Combined arms, 13 units with 2 sonic tanks, 3 tanks, 3 launchers, a siege tank, 2 quads
    and 2 trikes, right-clicked 30 tiles east: 2 scouts planned 5 tiles ahead of the sonic
    front; every unit reached its slot (71.7 s); **0 steps into a sonic lane** during the
    march (judged every cycle on the group facing) and none at the end.
  - Line and Column of 10 tanks: 10 abreast x 1 deep, 2 abreast x 5 deep.
  - Hunt in formation (attack-move, 9 units, Combined arms) towards 4 enemy tanks: the group
    stayed in formation for 35.3 s, broke for every member in one cycle 3 tiles from the
    nearest enemy, every member was fighting or closing in on the enemy in the same cycle,
    and every member had a target within 9.9 s (the siege tank last).
  - Frames: `formation-march.png` (Box) and `formation-combined.png`: the Combined-arms
    group with its scouts out in front, the panel's five formation buttons with Combined
    arms held down and its tooltip naming Ctrl+Shift+F.
- **`game_command_regressions`** adds `FORMATION_HUNT_COMMAND_PROBE_PASS`: enemy issuer, zero
  token, kind Move, unknown kind, unknown facing and off-map slot refused; attack-move joins
  one hunting group with the slowest pace, the sent facing and unforced attack-move walks;
  a hit on one member breaks the group and every member targets the attacker; a patrol
  hunt broken by a member's contact in `update()`; a plain formation move does not break.
- **Pinned versions updated:** SaveCompat (9859), NetworkManager (73), CityRoad (enum order),
  NetworkHardening (arity and classification of the new command), deviation-reward (the
  formation record is 22 bytes).

## Verification of the Box branch (`feat/formations`, Mac mini, Release, `--parallel 3`)

`ctest --test-dir build -R "^(dunelegacy_tests|formation_planner_tests|formation_movement_probe|game_command_regressions|quantbot_whole_army_dispatch|quantbot_army_recovery|quantbot_custom_attack|deviation_reward_probe|unit_speed_probe|quantbot_launcher_kiting)$"`
passed 10 of 10 tests. The whole unit suite (`dunelegacy_tests`) ran 1013 test cases: 1010
passed and 3 were skipped.

- **`formation_planner_tests`** (`[formation]`, 12 cases):
  - determinism across 24 member permutations;
  - coherent-shape preservation and compression;
  - box role ordering facing east, south and diagonally;
  - fallback for impassable or occupied slots;
  - vehicle uniqueness and infantry capacity;
  - map-corner clipping;
  - catch-up boundaries;
  - token and group-id rules.
- **`formation_movement_probe`** (new; real engine, real right-click path, Habbanya-Autumn).
  Twelve units were used: 4 tanks, 4 launchers, 2 siege tanks and 2 quads, scattered and
  moved about 35 tiles east across sand.
  - With formations on, every unit got exactly the planned slot and all 12 arrived. Arrival
    spread (first to last unit within 4 tiles of the target) was **16.4 s**. The rearmost tank
    finished ahead of the foremost launcher, and membership cleared on arrival.
  - With formations off, every unit got the old forced move to the clicked tile and none joined
    a formation. Arrival spread was **31.2 s**.
  - The probe also saves mid-march, reloads, and confirms that group, slot and pace come back
    exactly and that all units then reach their slots.
  - A 9854 unit record loads into this build with no formation state.
  - It writes `build/formation-probe/formation-march.png`: the group marching abreast at the
    shared pace, rendered by the real `Game::drawScreen`.
- **`game_command_regressions`** prints `FORMATION_COMMAND_PROBE_PASS`. On real objects it
  shows that:
  - an enemy issuer is refused;
  - a zero or oversized token, or an off-map slot, is ignored;
  - the owner's three-unit plan forms one group with the slowest pace and forced moves;
  - a co-op partner's identical token maps to a different group;
  - a slot on a building moves to a usable tile deterministically;
  - a plain move ends membership;
  - an aircraft keeps the plain move;
  - a lone member is dissolved.
- **Updated version-pinned tests:** SaveCompat (9857), NetworkManager (71), CityRoad (enum
  order), NetworkHardening (arity and classification), and deviation-reward (strips the new
  20-byte record when building a pre-9849 unit record).

### AI wave measurement

Setup: `tests/ai/run-campaign-balance.py --formation-measure --local-spectator`, QuantBot Hard
against QuantBot Hard, vanilla mod, 30 game minutes. Three seeds (486409243, 1841712204,
41562270) on each of three maps.

- A wave is a new `customWave` dispatch with at least 3 ground members that are not already in
  the target area.
- A member arrives when it is within 10 tiles of a live enemy building.
- Spread is the time from the first to the last member arriving. Shots are the members' primary
  shots in the 30 s after the wave's first arrival.
- Only waves where at least 3 members arrived are counted.

The before numbers are from the base build 40f4e7a8. The after numbers are from this branch,
with defence uncapped and the pace set by the slowest vehicle.

| Map | Waves (before → after) | Spread mean (before → after) | Spread median (before → after) | Shots mean (before → after) | Shots median (before → after) |
|---|---|---|---|---|---|
| 2P 64x64 Face Off | 11 → 15 | 86.1 → 129.4 s | 50.5 → 112.5 s | 43.7 → 39.5 | 48 → 35 |
| 2P 128x128 Habbanya-Penny | 4 → 1 | 62.2 → 40.1 s | 38.6 → 40.1 s | 61.5 → 58.0 | 60 → 58 |
| 2P 128x64 Ergun-Sahij | 14 → 17 | 114.2 → 101.5 s | 109.7 → 92.5 s | 58.9 → 51.4 | 60 → 40 |
| All | 29 → 33 | 96.4 → 112.3 s | 78.1 → 100.8 s | 53.5 → 46.2 | 56 → 36 |

Total first-contact shots summed over all measured waves went from 1552 to 1524 (−2%). The
per-wave mean falls by 14% because the after runs split into more, smaller waves. The noise
between seeds is large: Ergun-Sahij seed 41562270 went from 293 to 82 shots, while seed
1841712204 on the same map went from 288 to 493.

There are two intermediate results:

1. **Slowest member, including infantry, sets the pace; defence also slowed.** Mean shots fell
   from 53.5 to 38.9 (−27%). The cause was holding mixed waves to the soldier's pace (0.24,
   against 0.80 for a tank). This is why the pace now comes from the slowest vehicle.
2. **First single-seed run (initial commit).** Mean shots fell from 48.3 to 36.5.

**Conclusion:**

- After these fixes, the shots drop per wave is within the seed-to-seed noise, and total shots
  are level.
- Pacing engine-Hunt waves does not narrow the arrival spread on this metric. Hunt picks each
  member's own target, so a wave spreads out by target rather than by speed.
- Pacing is still worth keeping where units share a destination: the player path and
  `AIPlayer` waves. The 12-unit real-engine probe shows the spread going from 31.2 s to 16.4 s.
- **Integration decision:** pace was removed from pure engine-Hunt waves (QuantBot
  `launchGroundHunt` and `SmartBot::attack`), since it widened QuantBot's measured arrival spread
  (96.4 → 112.3 s) without a shared destination to converge on. These waves behave as in
  1.0.833. Waves with a shared destination keep it: `CampaignAIPlayer` (one attack target) and
  `AIPlayer` (formation move to the wave destination), plus army recall.

## Spec deviations and limits

- **No group command:** commands carry at most 4 parameters (`CommandValidation`), so the group
  is sent as one command per unit with a shared token, as the spec's alternative allows. The
  speed cap is computed by the executor from the members rather than sent.
- **Hunt waves:** QuantBot waves march as hunting formations since the QuantBot waves work above
  (`WaveFormation=None` restores pure engine Hunt). The engine still picks each hunter's target
  after the formation breaks; the formation only carries the wave to its objective.
- **What "slowest" means:** the pace is the slowest *vehicle* measured on open sand (reference
  speed). Infantry set it only for an all-infantry group, because waiting for infantry measurably
  cost mixed waves their first-contact fire. The cap is set
  when the order is given and is not raised when the slowest member dies.
- **Catch-up lag:** "slot-relative progress" is the member's remaining tiles minus the smallest
  remaining distance in the group.
- **Defence moves are never slowed:** this applies to the base scramble and district defence,
  as decided by the integration owner after the first measurement.
- **Contact ends membership:** contact, meaning an enemy target within `huntTowardRange()`,
  ends membership; in a hunting formation it ends every member's. The fight is not
  speed-capped.
- **Token reuse:** a token repeats after about 4.6 game hours, and at the 16th group ordered by
  one controller in the same cycle. A clash with a still-live group would merge the two
  deterministically.
- **SmartBot:** unchanged after integration (see the integration decision above).
- **Lane-keeping only on the march of formations with a facing:** player orders,
  `doFormationMove` and `doFormationHuntToward` carry one; `doFormUp` groups (QuantBot recall,
  `AIPlayer` after its HUNT) keep only lane-clear slots. A member whose own slot lies ahead
  in another column can wait behind a sonic tank until that tank arrives and leaves the
  formation; units path individually, so the rule makes them wait rather than re-route.
- **Hunt break and Area Guard:** a member out of its attack mode's reach of the contact
  (Area Guard: 10 tiles, launchers 12) closes in on it instead of targeting it at once, so
  "every member hunting" is immediate and "every member has a target" took 9.9 s in the probe.
- **Command parameter packing:** with 4 parameters, the order kind and facing ride in the top
  byte of the token parameter of both formation commands (protocol 73).
- **Not built (non-goals):** air or naval formations, changes to who/when QuantBot sends
  units, and changes to the pathfinder.
- **Form-up in place, one speed:** the brief asked for the formation move path with all its
  rules. The re-form uses that path (planner, slots, harvesters out, MCV centred, sonic lanes
  clear in the formed shape, group pace cap) but as the group's form-up phase, in which
  members walk at their own speed and the lane wait does not apply - as in the form-up before
  every march. Marching on the spot gridlocked (see "Form up where they stand").
- **`Ctrl+F` forms up too:** switching formations on chooses the current shape, so it forms the
  selection up like that shape's button; the brief named the buttons and `Ctrl+Shift+F`.
- **Short formation moves (not changed):** a formation move whose march is under
  `kFormUpMinMarch` skips the form-up and marches straight to the slots under the march rules.
  When the slots overlap the group (a short move into the area it stands in), that is the
  situation that gridlocked the form-up in place, so it can gridlock the same way [inference
  from the probe; no short-move fixture was run].
