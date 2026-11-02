# Projectile alignment in local 1.0.759

Standard rockets now share Dynasty 4469449c movement, steering, arming and
arrival rules across Vanilla, Dune City and Dune2R. The earlier audit below is
a historical comparison against 1.0.758, not a description of the fixed build.

| Projectile | Tiles/second | Maximum steering degrees/second | Ground / air arming counter at 20Hz |
|---|---:|---:|---:|
| Launcher | 15 | 168.75 | 8 / 16 |
| Deviator | 15 | 168.75 | 7 / 14 |
| Turret | 11.25 | 675 | 60 / 120 (arrival bypasses arming) |
| Trooper / ornithopter mini-rocket | 13.75 | 421.875 | 3 / 6 |
| Death Hand | 18.75 | 168.75 | 15 for its normal ground target |

The port uses the original integer direction tables and distance metric, 20Hz
movement, 15Hz rotation, movement-before-aim-before-rotation order, and the
previous movement sample for arrival detection. A rational clock maps these
onto the host's 16ms updates without accumulating a timing error. Missile movement
is no longer dependent on the rendering tick. Explosions use actual missile
positions; expiry alone does not detonate a rocket. Launcher/gas scatter now uses
the correct coordinate scaling and rejects off-map scatter.

## Launcher versus aircraft

Dynasty uses the same Rocket projectile against ground and air, with an air-specific
firing/guidance rule, rather than switching to the turret's ARocket:

- A launcher/deviator can fire at a flying target without facing it, after an
  already-started body turn finishes (`Script_Unit_Fire`, script/unit.c:616-636).
- Flying targets double the arming/steering counter. Launcher rockets remain
  unarmed for 0.8 seconds; the previous immediate-air-impact bypass is removed.
- Steering follows the live position of any flying target while the counter is
  positive. The scattered impact destination remains fixed. An existing turn
  can finish after guidance expires. Target loss falls back to the stored goal.

## Intentional turret anti-air extension

Strict Dynasty arrival rules left the rocket turret with zero kills in the
64 isolated attacking-ornithopter encounters tested here, even after restoring
Dynasty's extended targeting range. It damaged the aircraft in seven encounters.

The final build adds a **one-eighth-tile physical intercept** for turret missiles
against flying targets. It sweeps the relative missile/aircraft motion between
movement samples. Crossing the same place at different times is not a collision.
A confirmed intercept detonates on the missile's own segment and deals normal
direct-hit damage to the intercepted aircraft; other splash uses normal falloff.
The target's later frame position cannot turn a proven direct hit into a miss.
This does not teleport explosions onto remote targets, increase missile speed or
turn rate, or give ornithopters a general splash-damage bonus. Launcher arming
and guidance remain Dynasty-style; they do not get this turret intercept rule.

Rocket turrets also regain Dynasty's triple acquisition/range against ornithopters
(including its special visibility exception), and fire their cannon inside three
tiles instead of becoming unable to fire at aircraft. The multiplier applies to
the configured ground WeaponRange; other aircraft retain normal targeting range.
Turret body rotation, reload and general target-selection scheduling remain the
host game's, so these are not claims of identical complete combat AI.

## DuneCity Trooper Anti-Air

The shared **Trooper Anti-Air** option (`GameOptionsClass::trooperAntiAir`,
default **on**) enables tracking for infantry-fired `Bullet_SmallRocket` shots
against flying targets **only for DuneCity content**. The gate uses the content
base, not the selected mod's folder name: Civilization Wars (`CivWars`, base
`dunecity`) and DuneCity-derived content receive the same rule. Vanilla, Dune2R
and Tornie-family content ignore the switch.

The firing-time source record identifies Troopers, including house-specific
Troopers and the individual Troopers deployed by squad orders. The source check
also covers the Trooper squad and Flame Trooper item identities when they fire a
small rocket; it never changes a flame projectile. Ornithopter and vehicle
mini-rockets do not gain this guidance. The recorded identity survives the
shooter's destruction or ownership change; old shots without that record can
still use a live shooter.

An enabled rocket keeps its live aircraft destination and steering after its
detonation/arming counter reaches zero. It does not detonate merely because it
has passed the aircraft's **old** position. It reuses the existing turret/gas
rocket's **one-eighth-tile swept relative-motion intercept**, including normal
direct-hit damage at a confirmed contact. Missile movement, turn rate, rational
clocks and collision arithmetic remain integer/fixed-point, with no new RNG
draws. Losing the target falls back to the last stored destination. Existing
projectile save fields already retain the destination, steering, counter,
previous aircraft position and firing-time source; no extra projectile record is
needed.

This is a hit-delivery change, **not a stat increase**: the stock anti-air Trooper
payload remains **5**, and the stock Ornithopter remains **25 HP**. Ground fire,
other shooters, the disabled option and unrelated content keep their old rules.

### Real-engine one-pass consumer fixture

After the integrator's combined native build, run through the managed probe
queue (the runner reuses that build's objects, never another CMake tree):

```sh
python3 tests/units/run-trooper-aa-probe.py \
  --build-dir build-int --output-dir ../outputs/trooper-aa
```

The macOS Ninja consumer injects `runTrooperAntiAirProbe()` into a private
diagnostic main, using production `Game::updateGameState`, `UnitBase::attack`,
`AirUnit` movement and `Map::damage`. On a plain rock map, **3, 5 and 8** ready
Troopers hold position while one Ornithopter flies straight overhead in either
direction. Shots, reloads, infantry tile offsets, aircraft speed and health are
not replaced. The encounter ends at a real exit-plane crossing or death, before
a second volley is possible. An additional outbound single-shot case naturally
outlives the arming counter and must still steer and deliver its unchanged
payload after the counter reaches zero; the fixture never forces a fuse value.

Defaults are seeds `11,22,33`, mods `dunecity,CivWars,vanilla,Dune2R`, and both
switch states. `--mods` and `--seeds` select a subset. Every encounter is repeated
with the same inputs, comparing actual serialized object/projectile records,
house/object digests and RNG byte for byte (match options themselves are
intentionally excluded). Non-DuneCity content must also match across switch
states. Squad orders deploy ordinary individual Troopers, so the counted groups
exercise the same firing path as trained squads.

**Acceptance targets, not measured results:** three enabled Troopers damage but
do not kill the aircraft in their single volley; five and eight kill it during
the pass. The runner asserts these targets and retains the real numbers rather
than substituting estimated hit rates. No fixture or build was run while this
change was being edited.

Each mod writes `encounters.csv`, `frames.csv`, `projectiles.csv` and `run.log`;
the output root merges encounters and writes `summary.csv` (excluding duplicate
replays from aggregates). Important fields are `trooper_anti_air`,
`dunecity_content`, `scenario`, `seed`, `heading`, `troopers`, `repeat`,
`start_hp`, `end_hp`, `health_loss_capped`, `killed`, `shots`,
`rockets_observed`, `first_shot_tick`, `first_damage_tick`, `goal_updates`,
`armed_goal_updates`, `armed_heading_changes`, `path_world`,
`crossed_trooper_line`, `exit_crossed`, `ticks`, `final_rng`, `trace_hash` and
`outcome`. Projectile rows expose the actual payload, counter, heading, aim,
turn, position and moving destination. The sole completion marker per mod is
`TROOPER_AA_PROBE_PASS`, never the menu runner's completion marker.

### QuantBot avoidance boundary

No aircraft avoidance policy changes here. Easy/Medium's strict coverage already
includes Troopers and Trooper squads (`AirStrikePolicy::strictAntiAir`).
Hard/Brutal's ordinary offensive raid coverage still omits them
(`AirStrikePolicy::antiAir`); the stronger real interception warrants a separate
troop-cluster avoidance evaluation with aircraft outcomes. This human-order
fixture measures projectile delivery, not QuantBot's choice to enter that cover,
and does not claim that follow-up has been implemented or validated.

## Explosion and weapon corrections

- Standard missile blasts affect both ground and air. Radius is strictly below
  one tile, with damage halving each quarter tile using Dynasty's distance metric.
  Frigates retain Dynasty's explosion immunity. Structure damage uses the occupied
  tile/footprint. Non-rocket shells, sonic waves and mod-only flames/healing retain
  their existing flight rules, apart from enabling close turret shells to hurt air.
- Gas uses a radius strictly below two tiles and no longer damages structures as
  ordinary explosives. Existing house/mod deviation probabilities are retained;
  this change does not claim exact Dynasty deviation probability parity.
- Death Hand uses seventeen original offset blast centres, two-tile reaction
  radius and 200 damage per centre. Palace launch scatter is corrected to the
  original scale and resolves to the selected tile centre. Nuclear-plant damage
  remains based on its historical balance constants, independent of Palace missiles.
- Troopers reduce mini-rocket damage by a quarter at long range; the previous
  reduction on the close-range shell was reversed.
- INI template notes document the shared projectile rules and turret range
  multiplier. No unused projectile INI properties were invented. Existing shooter
  WeaponDamage/WeaponReloadTime and movement configuration remain active.

## Verification and reproduction

Headless production game code, isolated profiles, no user profile edits:

- 64 mechanic cases per mode: wrong-way launches, moving/static targets, splash,
  true/false physical crossings, target loss, mixed air/ground damage, close turret
  weapon selection and an actual Palace launch/17-centre explosion.
- 128 full Game::updateGameState combat encounters per mode: four seeds, four
  starting distances (4/8/12/18 tiles), four headings, launcher or turret against
  an attacking ornithopter. Final results are identical across modes: launcher
  15/64 kills (29 damaged), turret 16/64 kills (19 damaged). These prove killability
  and continued aircraft attacks, not a general game-balance win-rate estimate.
- 180 scenarios per mode exactly match 10,942 original-Dynasty sampled position,
  heading, counter and lifetime states. The additional 20 turret/air scenarios
  intentionally diverge at physical interception and have dedicated regression
  checks. Original Dynasty reference runs pass ASan/UBSan. Before adding that
  extension all 200 cases matched 11,744 states.
- 21 mid-flight save/observer scenarios per mode, 160 exact continuation frames
  each (10,080 across modes), including mod flame/healing. Previous save-byte
  layouts are also read and their old cycle timers migrated.
- Save format 9845 persists missile clock, aim, rotation, previous distance and
  previous aircraft position. Network protocol 17 prevents mixed simulation rules.
- Unit-speed/real ornithopter attack-pass probe passes all three modes. All 13
  existing CTest targets and all three newly registered projectile probes pass
  (16 total).

```sh
python3 tests/units/run-dynasty-projectile-probe.py --dynasty-dir ../dunedynasty \
  --reference-build-dir ../outputs/route-speed-alignment/extended3 \
  --output-dir ../outputs/projectile-fix/dynasty
python3 tests/units/run-unit-route-probe.py --projectile-trace --output-dir /tmp/projectile-trace
python3 tests/units/check-projectile-traces.py --dynasty ../outputs/projectile-fix/dynasty/trace.csv \
  --current-dir /tmp/projectile-trace
python3 tests/units/run-unit-route-probe.py --projectiles --output-dir /tmp/projectile-regressions
python3 tests/units/run-unit-route-probe.py --projectile-combat --output-dir /tmp/projectile-combat
python3 tests/units/run-unit-route-probe.py --projectile-continuation --output-dir /tmp/projectile-saves
```

Receipts: `../outputs/projectile-fix/` (`regression`, `combat4`, `trace2`,
`continuation3`, `dynasty`, `unit-speed`). Claude supplied the focused launcher
and explosion-layer review; Codex implemented and tested the corrections.
No push, PR, public release or MBA installation in this change.

---

# Rocket and missile audit: Dynasty versus current Legacy-based build

Audit of DuneCity 1.0.758 (32f92531) against Dynasty 4469449c. The older local
Legacy checkout at a381010 already contains the anti-air modifications; it is
not treated as pristine upstream Legacy. This audit changes no gameplay values.
The current three modes produced identical controlled projectile results.

## Nominal speed and maximum steering rate

Normal speed. Movement is tiles/second; steering is degrees/second, not sprite
animation rotation. Dynasty uses256 coordinates/tile,20 movement updates/second
and15 rotation updates/second. Current code uses64 world units/tile and62.5Hz.

| Shooter / projectile | Current speed | Dynasty speed | Current steering | Dynasty steering |
|---|---:|---:|---:|---:|
| Launcher / Rocket | 18.75 | 15 | 395.508 | 168.75 |
| Deviator / Gas rocket | 18.75 | 15 | 395.508 | 168.75 |
| Rocket turret / ARocket | 18.75 | 11.25 | 395.508 | 675 |
| Trooper at range / MiniRocket | 22.5 | 13.75 | 0 | 421.875 |
| Ornithopter / MiniRocket | 22.5 | 13.75 | 0 | 421.875 |
| Palace / Death Hand | 31.25 | 18.75 | 0 | 168.75 |

Steering capacity is not synonymous with active tracking. Dynasty steers toward
the stored destination while its fireDelay is nonzero, substituting the live
position of any flying target for steering. It does not replace the stored
impact destination with that live position. When the counter expires it stops
issuing new steering targets (an existing turn can still finish).

Our Launcher/Deviator steer continuously; only ornithopters update their live
impact destination. Carryalls retain the scattered launch destination. Turret
rockets continually update both aim and impact destination for any target.
MiniRockets and Death Hand do not steer in the current code.

Current `Bullet.cpp` interprets4.5 in256-units-per-revolution coordinates, so
4.5*360/256*62.5 =395.508 degrees/second. Its comments saying4.5 degrees/tick
are misleading. Numeric speed/steering/fuse settings live in C++, not ObjectData INIs.

Mappings: `src/units/{Launcher,Deviator,Ornithopter,Trooper}.cpp`,
`src/units/UnitBase.cpp:280`, `src/structures/{RocketTurret,Palace}.cpp`;
Dynasty `src/table/unitinfo.c`, `src/script/unit.c:644`,
`src/script/structure.c:577`. Troopers use shells within2tiles; Dynasty reduces
rocket damage by25% at longer range, whereas our code reduces the close-range
shell damage. This is a separate weapon-damage discrepancy, not a speed conversion.
Other standard combat units fire shells or sonic waves, outside this rocket audit.
Mod-only flames/healing projectiles do not have direct Dynasty counterparts.

## Detonation and counters

Dynasty `src/unit.c:1745-1813` declares arrival when distance increases relative
to the previous movement update, or distance is strictly below16 coordinates
(1/16tile). It uses its integer distance metric, max(dx,dy)+min(dx,dy)/2.
For most missiles, arrival detonates only after fireDelay reaches0. Turret
missiles bypass that arming gate. Expiry alone is not a universal self-destruct.
Explosions occur at the missile's newly computed position, not at the target.

Counters are decremented on the20Hz movement clock; air targets double them
(`Unit_CreateBullet`, `src/unit.c:2345-2397`). These are arming/steering durations,
not guaranteed missile lifetimes:

| Projectile | Dynasty ground / air duration | Current setting and effect |
|---|---|---|
| Rocket | 0.40 /0.80 seconds | 22cycles=0.352s ground;50=0.80s air, but air bypasses arming |
| Gas rocket | 0.35 /0.70 seconds | 19cycles=0.304s ground;50=0.80s air, but air bypasses arming |
| Turret rocket | 3 /6 seconds; impact bypasses arming | Constructor overwrites375 with60/120cycles:0.96/1.92s; expiry forces an explosion |
| MiniRocket | 0.15 /0.30 seconds | 7cycles=0.112s or50=0.80s; impact ignores the counter |
| Death Hand | 0.75 seconds for its normal ground target | No arming delay; arrival immediately detonates |

Our `Bullet.cpp:541-583` detects increasing Euclidean distance or distance<4
world units. Launcher/Deviator air impacts bypass the timer. Turret, MiniRocket
and Death Hand snap their coordinates to the destination before detonating.
That snap can turn a miss into a hit, even when the missile is far away.
Rockets in both engines generally pass over terrain/structures; the immediate
wall/building collision branch belongs to ordinary gun projectiles. Off-map
removal also differs: our code permits a5-tile margin.

Death Hand uses17 offset blast centres in Dynasty; our implementation uses21
positions in a5x5 footprint without corners. Gas impact invokes deviation, not
ordinary explosive damage. Dynasty's gas search radius is strictly below2tiles
(`Map_DeviateArea`, `src/map.c:656`); current air/ground gas checks use a half-tile
radius and different eligibility/probability handling. Those effects require
separate balancing checks when aligning detonation.

## Anti-ornithopter compensations and measured bugs

1. **Remote-hit snap:** with an eight-tile distant stationary ornithopter and
   a turret missile deliberately facing180degrees away, current production code
   kills the25HP target in one16ms update with a40-damage shot. The missile was
   eight tiles away before that update. Dynasty detonates near the missile after
   two movement updates and does0 damage. The setup deliberately stresses an
   overshoot; it demonstrates the bug, not its frequency in normal battles.
2. **Special full splash damage:** current `src/Map.cpp:184-192` explicitly
   gives ornithopters full damage throughout the half-tile blast radius. Dynasty
   uses distance falloff for them too. With8-damage explosions, measured damage:

   | Offset from ornithopter | Current | Dynasty |
   |---|---:|---:|
   | 0tiles | 8 | 8 |
   | 0.375tiles | 8 | 4 |
   | 0.625tiles | 0 | 2 |

   Dynasty's normal reaction radius is below1tile; damage halves at quarter-tile
   intervals using its integer metric (`src/map.c:432-492`). Current other-aircraft
   damage also starts at half damage at the centre, unlike Dynasty.
3. **Scatter scaling:** Dynasty `Tile_MoveByRandom` multiplies its integer offset
   by16 coordinate units (`src/tools/coord.c:359-360`). At four Dynasty coordinates
   per current world unit, an equivalent offset needs a factor4 here. Our code
   uses radius directly, making ordinary rocket/gas scatter approximately four
   times smaller, with different angular rounding. Ornithopter tracking then
   overwrites that scattered impact destination altogether.
4. **Turret close-range blind spot:** current `RocketTurret::attack` refuses to
   fire at flying targets inside3tiles. Dynasty's firing routine selects a cannon
   bullet below3tiles. Full target-acquisition/combat validation is still needed
   before changing this interaction.

The corrected ornithopter cruise rate is11.25tiles/s. Dynasty's turret missile
has that same nominal cruise rate and much faster turning; Launcher missiles
are faster than the aircraft. Replacing the compensations requires per-type
speed, steering, targeting, arming, actual-position detonation and falloff together.
Simply treating all missiles as one common fast homing projectile is inaccurate.

## Tests and limits

Headless tests execute real Bullet::update/Map::damage in the current game context
and original Dynasty Unit_CreateBullet/GameLoop_Unit/Map_MakeExplosion with the
actual UNIT.EMC. Dynasty source objects are the previously verified ASan/UBSan
reference. No rendering or live user profiles are involved.

- Current: six projectile types x three target types x three trajectory setups,
  plus three blast-offset checks =57 cases per mode;171 total, byte-identical.
- Dynasty: five native projectile types x three target types x three setups,
  plus three blast checks =48 cases. ASan/UBSan pass.
- Setups: stationary target, reversed launch heading, controlled crossing target.
  Initial impact destinations are fixed to the target to isolate motion/fuses
  from scatter. Flight targets are moved manually; this is not full combat AI,
  a natural-hit-rate benchmark or a statistical accuracy comparison. Synthetic
  target combinations include ones a normal shooter would not select.
- Unit tables/source prove maximum steering rates; CSV `turn_deg_s` is the
  observed first-update turn for the current engine and the configured maximum
  for Dynasty, so do not interpret that CSV field as identical measurements.
- Current crossing trajectory is a controlled11.25tiles/s, with map occupancy
  updated; the source fixture uses the equivalent48 coordinates/60Hz frame.

Reproduce using the existing route-reference build (or create it with
`compare-dynasty-routes.py` first):

```sh
python3 tests/units/run-unit-route-probe.py --projectiles \
  --output-dir ../outputs/projectile-audit/repro-city
python3 tests/units/run-dynasty-projectile-probe.py \
  --dynasty-dir ../dunedynasty \
  --reference-build-dir ../outputs/route-speed-alignment/extended3 \
  --output-dir ../outputs/projectile-audit/repro-dynasty
```

Artifacts and initial investigation: `../outputs/projectile-audit/`.
Claude supplied the bounded static-table audit; Codex traced runtime logic,
closed the unverified firing/steering/explosion questions and ran both engines.
No projectile fixes or new app installation were made during this audit.
