# Ordinary weapon damage and splash

## Default ground damage

Ordinary guns, shells and rockets use the base Dune II/Dynasty blast distance:
`max(abs(dx), abs(dy)) + min(abs(dx), abs(dy))/2`. The blast reaches strictly
less than one tile. Each quarter-tile band halves damage; fractions are discarded.
HP losses remain whole numbers. Dynasty optional direction enhancements are not used.

Light weapons keep their full configured projectile payload. Heavy weapons use
half their payload at the centre, restoring the ground rocket strength from
before the September 2026 rocket alignment. Projectile flight, scatter, reloads
and interception are independent of this rule.

| Weapon | Centre | 0.25 tile | 0.5 tile | 0.75 tile |
|---|---:|---:|---:|---:|
| Soldier | 3 | 1 | 0 | 0 |
| Trooper close | 5 | 2 | 1 | 0 |
| Trooper ranged | 4 | 2 | 1 | 0 |
| Trike / Raider | 5 | 2 | 1 | 0 |
| Quad | 7 | 3 | 1 | 0 |
| Tank | 12 | 6 | 3 | 1 |
| Siege Tank | 15 | 7 | 3 | 1 |
| Devastator cannon | 20 | 10 | 5 | 2 |
| Launcher | 37 | 18 | 9 | 4 |
| Ornithopter | 22 | 11 | 5 | 2 |
| Gun Turret / Rocket Turret close cannon | 10 | 5 | 2 | 1 |
| Rocket Turret missile | 15 | 7 | 3 | 1 |
| Elite Launcher | 47 | 23 | 11 | 5 |
| Rocket Trike | 2 | 1 | 0 | 0 |

These are losses per projectile against a ground unit with enough remaining HP,
using stock weapon data. They are not damage per second. Custom weapon payloads
retain the same proportional rules. The projectile records its source type, so
a Trooper rocket remains a light weapon after its shooter dies or changes owner.
Older projectiles without source records use the live shooter if available;
otherwise a small rocket uses the heavy-weapon rule in default mode. In original
mode, an unknown source receives full payload without the Ornithopter correction,
because an old save does not identify which unit fired it.

Building hits, aircraft eligibility and aircraft damage retain their existing
paths. Deviator conversion, Sonic waves, Devastator death blasts, Death Hand,
flame, sandworm attacks and sabotage retain their separate calculations.

## Historical distinction

Legacy 0.96.2 split payloads between weapons when firing and used a proportional,
radius-based splash calculation. Legacy 0.96.3 (2013) removed that split and
instead halved ordinary ground damage at impact, including at the blast centre.
The two changes had different effects on one-weapon and two-weapon units.
Original Dune II/Dynasty classic is a separate ruleset, not an exact reconstruction
of Legacy 0.96.2. The original authors' intent for the 2013 change is unconfirmed.

## Original Dune II option

Game Rules has an **Original Dune II damage** checkbox, off by default.
It selects base Dune II/Dynasty classic ordinary damage against ground units.
Both choices use the same quarter-tile splash bands and strict one-tile cutoff.
The checkbox does not select Dynasty optional direction enhancements.

| Weapon | Centre | 0.25 tile | 0.5 tile | 0.75 tile |
|---|---:|---:|---:|---:|
| Soldier | 3 | 1 | 0 | 0 |
| Trooper close | 5 | 2 | 1 | 0 |
| Trooper ranged | 4 | 2 | 1 | 0 |
| Trike / Raider | 5 | 2 | 1 | 0 |
| Quad | 7 | 3 | 1 | 0 |
| Tank | 25 | 12 | 6 | 3 |
| Siege Tank | 30 | 15 | 7 | 3 |
| Devastator cannon | 40 | 20 | 10 | 5 |
| Launcher | 75 | 37 | 18 | 9 |
| Ornithopter | 38 | 19 | 9 | 4 |
| Gun Turret / Rocket Turret close cannon | 20 | 10 | 5 | 2 |
| Rocket Turret missile | 30 | 15 | 7 | 3 |
| Elite Launcher (City extension) | 94 | 47 | 23 | 11 |
| Rocket Trike (City extension) | 5 | 2 | 1 | 0 |

The original Ornithopter ground payload is 38: Dynasty configures 50 and subtracts
an integer quarter (12) for the mini rocket. City configures 45 instead, so the
checked option scales its ground payload by 38/45 using integer arithmetic.
Custom Ornithopter damage remains proportional, with fractions discarded.

City extensions use the same full-payload rule even though they have no original
Dune II counterpart. This option restores ordinary ground damage, not an entire
historical engine: building and aircraft damage, special weapons, reload times,
projectile flight and shot counts retain their City behavior.

The shared option is stored in config, saves and multiplayer setup, and can be
remembered for new games. Old settings without the MOD6 flag default it off.
The custom hover overlay explains the change with Tank and Launcher examples.
