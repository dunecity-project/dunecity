# Campaign attack balance — 1.0.666–667

## Finding

The September 2026 browser playtest reproduced excessive Easy campaign attacks.
`QuantBotConfig` already specified attack fractions by difficulty, but
`QuantBot::launchGroundHunt()` never read them and dispatched every eligible unit.

Unless stated otherwise, samples used Vanilla, Harkonnen, a full-control
QuantBot Easy partner and QuantBot Easy opponents, with no human building or combat commands. Times below
are simulation time (3,750 cycles per minute), not accelerated wall-clock time.

## Public 1.0.665 browser observations

Source: `b7db7199455d9b756043118b7412c1d1e9359d06` at
<https://dunelegacy.com/play/>. Browser speed was 4 ms per cycle for observation;
the normal simulation conversion remains 16 ms per cycle.

| Mission | Seed | Observation |
| --- | --- | --- |
| Level 1, SCENH001.INI | 1922307813 | Victory at 6.1 minutes, no friendly losses; no `ground_hunt` wave before victory. |
| Level 4, SCENH008.INI | 486409243 | At 8.01 minutes, Ordos dispatched 31 units worth 4,360 credits: its entire army. The friendly army was worth 1,700 shortly before the attack. Defeat at 10.84 minutes. |

Browser telemetry sessions were `1789276479975000-0` and
`1789277223304000-0`. Local evidence is in
`/tmp/dunecity-campaign-balance/baseline-level1.json` and
`/tmp/dunecity-campaign-balance/baseline-level4.json`.

## Updated browser verification

Local Emscripten 1.0.666 at game-source commit `05979ae`, level 4, seed
**501376578**, Easy partner and Easy enemies: **victory**, with the results
screen showing 15 game minutes. At cycle 30,048 the first Ordos wave was 8 units /
1,050 value against a 1,090 budget. The next wave was 6 units / 1,100 value against
1,127. No human building or combat orders were issued. Session:
`1789278977169999-0`; summary: `candidate-level4-browser.json` in the evidence
folder. Browser seed differs from the controlled native pair.

An additional public 1.0.665 level-9 browser sample (seed **257913089**, session
`1789278590744999-0`) sent 10 Sardaukar units / 2,900 value and 17 Atreides units /
4,750 value at minute 12. The friendly construction yard and factories were gone
by the snapshot at 13.17 minutes, followed by the defeat briefing. Exact end
cycle was not recovered from the buffered log. Evidence: `baseline-level9.json`.

The post-mission statistics currently classify the full-control partner's
production and kills under “Enemy”. Household telemetry, not those labels, was
used for the balance comparison. This separate display defect is fixed in local 1.0.668: campaign results now
group houses by the local team instead of `House::isAI()`. See HANDOVER.md.

## Controlled real-game comparison

The diagnostic harness runs the real native campaign, AI, economy, units and
command loop with an isolated profile and fixed seed **486409243**. It suppresses
the introductory UI and public analytics; it does not script victory or command
the player's units. Both sides use the same setup in each pair. The 100% case is
the new selection code with its budget set to the full army, providing a controlled
comparison of attack size; it is not a separate build of historical 1.0.665.

| Mission | Full-army budget | Easy 25% budget |
| --- | --- | --- |
| Level 4 | First wave 31 units / 4,360 value; defeat at cycle 40,427 (10.78 min) | First wave 8 units / 1,050 value against a 1,090 budget; victory at cycle 57,626 (15.37 min) |
| Level 9 | First Atreides wave 18 units / 5,200 value; defeat at cycle 61,427 (16.38 min) | First Atreides wave 3 units / 1,300 value; still alive with construction yard and army at the 75,000-cycle (20 min) cutoff. Opponents also remained alive. |

Level 9's first Sardaukar wave changed from 6 units / 2,000 value to 3 units /
650 value. Their next limited wave sent only 200 value because 450 was already
committed against a 650 budget. This exercises reserve accounting in the game.

Evidence directories under `/tmp/dunecity-campaign-balance/`:
`native-level4-unlimited-v2`, `native-level4-limited-v4`,
`native-level9-unlimited`, and `native-level9-limited`. Each contains a
`summary.json`, the full decision log, and the build/run logs. These measurements
used the code committed as `05979ae`, while the working tree was still uncommitted;
the harness records that distinction in its summaries.

## Player difficulty targets

The initial acceptance target was Easy winning or holding through levels 4–5
and Hard beating Easy on level 9. The 666 tests met those targets in both seeds.
Stefan subsequently raised the Easy target to survival on level 9 (see the 667
follow-up below):

| Partner / level | Seed 486409243 | Seed 1 |
| --- | --- | --- |
| Easy / 4 | Victory, 15.37 min (57,626 cycles) | Victory, 14.99 min (56,204 cycles) |
| Easy / 5 | Victory, 21.43 min (80,361 cycles) | Victory, 16.19 min (60,699 cycles) |
| Hard / 9 | Victory, 25.49 min (95,578 cycles) | Victory, 25.33 min (94,984 cycles) |

The Hard partner uses its actual Hard economy/combat settings; opponents remain
Easy. Added evidence directories: `native-level5-easy`,
`native-level9-hard-partner`, and `native-level{4,5,9}-seed1`.
Reproduce Hard with `--partner-difficulty hard --level 9 --minutes 45`.
Those initial tests did not establish long-term Easy survival on level 9; the
follow-up below extends the runs to an outcome or a 60-minute cutoff.

## 1.0.667: Easy level-9 survival and economy follow-up

Stefan's current target is **Easy surviving level 9 against Easy**, even if the
result is a stalemate. Four runs of the combined 667 changes, each with a
60-game-minute cutoff, all reached genuine victory without human commands or
mission skipping. Enemies retained the 25% per-house attack-value budget; no
additional wave reduction was made.

| Partner | Seed | Outcome | End cycle | Game minutes |
| --- | --- | --- | --- | --- |
| Easy | 486409243 | Victory | 119807 | 31.95 |
| Easy | 1 | Victory | 114740 | 30.60 |
| Easy | 42 | Victory | 111799 | 29.81 |
| Easy | 257913089 | Victory | 99712 | 26.59 |
| Hard | 486409243 | Victory | 83294 | 22.21 |

Evidence is under `/tmp/dunecity-campaign-balance/`, in
`easy9-667-60-seed{486409243,1,42,257913089}` and
`hard9-667-60-seed486409243`. Summaries identify the previous committed HEAD and
`workingTreeModified: true` because these measurements preceded the 667 commit.
This is four Harkonnen scenario-22 seeds, not proof of every house/map or human
player's experience. A reported collapse should be reproduced before lowering
wave budgets or adding aggregate limits across enemy houses.

The Starport already ignored cheap-price checks for needed harvesters and the
first carryall. Its problem was that the economy reserve was unavailable to
those imports. Version 667 lets them use that cash, accepts exact affordability,
counts only accepted orders and stops buying workers at the sustainable target.
The dedicated fixture uses real Starport ordering/payment, actual map spice and
forced above-normal prices: first carryall 1,500, harvester 1,200, available cash
2,700. Both are queued, order submitted, final cash zero. Evidence:
`starport-667-v3`, marker `STARPORT_ECONOMY_PROBE_PASS`. The initial fixture omitted
the normal AI spice survey and therefore had a zero worker target; this was a
fixture error, corrected before the passing run.

Reproduce from the built native tree:

```sh
python3 tests/ai/run-campaign-balance.py --output-dir /tmp/easy9-new-run --level 9 --minutes 60 --seed 42
python3 tests/ai/run-campaign-balance.py --output-dir /tmp/starport-new-run --level 9 --partner-difficulty hard --starport-probe
```

In the 666 browser test, Hard on level 8 (seed 493337323, SCENH020.INI,
session `1789280309911000-0`) destroyed both enemy construction yards by the last
flushed snapshot at 21.7 minutes. Stefan explicitly confirmed seeing victory and
continuing to level 9. A separate Hard level-9 browser run was strong at the last
20.4-minute snapshot, but its final victory screen was not captured before the
browser tabs were closed. Native victory evidence above is separate.

Building selection also received a 667 regression fix: clicking a building
clears previous units/buildings even with Shift held, so its dedicated sidebar
can open. Real SDL click probes pass on campaign levels 4/9. Native Release,
all six CTest targets, the command probe, dependency/signature audits and the
full Emscripten build pass. This candidate is not publicly deployed.

## Dune Dynasty campaign activation and scripts

Inspected the local upstream reference at commit
`4469449c75f51388ad2725297a95f09a6c601905` (gameflorist/dunedynasty).
It does not use QuantBot's universal opening timer:

- [GameLoop_Team in src/team.c](https://github.com/gameflorist/dunedynasty/blob/4469449c75f51388ad2725297a95f09a6c601905/src/team.c)
  skips team scripts while the house's `isAIActive` flag is false, then honors
  each script's delay. New teams have zero initial script delay.
- [Unit_Server_HouseUnitCount_Add in src/unit.c](https://github.com/gameflorist/dunedynasty/blob/4469449c75f51388ad2725297a95f09a6c601905/src/unit.c)
  activates both houses when registering an opposing non-flying normal unit
  (there is also a sandworm path). Thus ordinary ground contact activates the
  main team machinery; it is not simply a fixed minute in every mission.
- [Scenario_Load_Team in src/scenario.c](https://github.com/gameflorist/dunedynasty/blob/4469449c75f51388ad2725297a95f09a6c601905/src/scenario.c)
  loads behavior, movement type, minimum and maximum members from `[TEAMS]`.
  [Script_Team_AddClosestUnit](https://github.com/gameflorist/dunedynasty/blob/4469449c75f51388ad2725297a95f09a6c601905/src/script/team.c)
  enforces the maximum while recruiting. The original `TEAM.EMC` in our local
  `DUNE.PAK` contains staging/member checks, target selection, attack calls and
  600-tick delay calls, rather than a mission-length initial sleep. Team loop
  cadence also matters, so those delay constants are not exact wall-clock times.
- Reinforcements have a separate scenario schedule. Dynasty loads their time as
  `scenario_value * 6 + 1`, and the house loop decrements it every 600 game ticks
  (normal speed is 60 ticks/second). Values approximately represent mission
  minutes. They can introduce enemy contact and therefore activate main teams.

The original **SCENARIO.PAK**, not the repository's altered loose scenario files,
sets level 4 (`SCENH008.INI`) teams to 2–6 members by type, enemy-base troop drops
at minute 11 and recurring Sardaukar drops from minute 20. Level 5
(`SCENH011.INI`) uses 3–7-member teams and enemy-base drops at minutes 11 and 20.
Level 9 (`SCENH022.INI`) starts recurring enemy-base drops at minutes 12 and 14.
Multiple teams can operate concurrently; those per-team limits are not an
aggregate limit for the entire enemy alliance.

DuneCity loads `[TEAMS]` into House metadata and schedules the original
reinforcements, but QuantBot does not consume the team definitions. Its opening
attack timer is 8 minutes for tech levels up to 5, 9 at tech 6, 10 at tech 7 and
12 at tech 8. Its on-damage `campaignAIAttackFlag` is written and saved but is not
read as an attack gate. The 1.0.666 fix changes attack size only. Copying Dynasty's
contact gate could actually start QuantBot attacks earlier when the partner
scouts or attacks, so it should not be introduced as an assumed difficulty fix.
A fuller scripted campaign mode is a separate behavior change, requiring explicit
activation, team recruitment and save/load semantics, not just a longer timer.

## Behavior implemented

| Enemy difficulty | Maximum committed ground-combat value |
| --- | --- |
| Easy | 25% |
| Medium | 40% |
| Hard | 50% |
| Brutal | 60% |

The existing configuration values now govern campaign enemy hunts. Units already
hunting consume the budget, selection is deterministic by object ID, and smaller
units can fit when a more expensive one cannot. If no unit fits and nobody is
already committed, one cheapest available unit may exceed the budget so a small
army can still attack. Defend continues not to launch attacks.

This cap applies to actual campaign enemies. The player's full-control partner,
skirmish/custom modes, base defence, scripted reinforcements, aircraft, attack
timing and economy rules retain their existing behavior. Save data is unchanged.
Telemetry records the eligible army value, committed value, percentage and budget
alongside the number and value of units dispatched.

## Reproduction and limits

Build the native macOS Ninja target, then run each case into a new output folder:

```sh
python3 tests/ai/run-campaign-balance.py --level 4 --seed 486409243 \
  --attack-percent 25 --minutes 20 --output-dir /tmp/campaign-level4-25
python3 tests/ai/run-campaign-balance.py --level 4 --seed 486409243 \
  --attack-percent 100 --minutes 20 --output-dir /tmp/campaign-level4-100
```

Use `--level 9` for the late-game pair. The harness links a test-only main against
the existing native game objects; no diagnostic entry point enters the shipped
game. All six CTest targets pass, including difficulty scaling, committed waves,
mixed unit costs, one-unit exceptions and deterministic selection. Native and
Emscripten Release builds also pass.

These are targeted samples, not a measured human win rate or an exhaustive check
of all houses, mods and seeds. The Easy partner automates economy and combat and
may outperform or make different mistakes from a beginner. The smaller waves
address the demonstrated pressure spike; further tuning should use human trials
before altering income, attack intervals or mission reinforcements.

## Difficulty audit — 1.0.668

Campaign enemies and the AI sharing the human house use different paths. The
shared-house QuantBot switches to Custom growth rules inside a Campaign game;
this is intentional so it can build the player's base instead of merely replace
the enemy scenario's starting structures.

Current shipped Vanilla configuration (user config overrides can differ):

| Enemy setting | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Committed ground-HUNT budget / army value | 25% | 40% | 50% | 60% |
| Base army-value limit / initial army | 2.0x | 2.5x | 3.0x | 3.5x |
| Readiness / army limit | 50% | 40% | 30% | 30% |
| Offensive air target selection | No | No | Yes | Yes |
| Initial refinery build-target minimum | None | None | 2 | 4 |

Army limits have late-scenario exceptions: scenario 21+ Easy minimum 2,000, Medium
minimum 4,000, Hard fixed 10,000. This is scenario numbering, not displayed level.
All enemy difficulties share the tech-dependent 8–12-minute initial wait and
90-second base subsequent attack check with 75–125% deterministic jitter.
Readiness times the base army multiplier equals roughly the initial army value
for every difficulty (1.0x,1.0x,0.9x,1.05x), reducing separation in first readiness.
Scripted reinforcement schedules are unchanged across these choices.

For a full-control partner on maps larger than 32x32 and up to 64x64, the configured
army limits are 8,000 / 12,000 / 20,000 / 40,000. Enemy wave budgets do not apply to
that partner. The dynamic Custom-mode harvester adjustment overwrites its
configured per-difficulty cap with common ObjectData map limits, then applies
Vanilla capacity, engine/explicit caps and spice availability. Do not advertise
the initial 2/4/7/10 configuration as its sustained harvester caps.

Combat differences also exist: Easy has reactive on-hit launcher/deviator and
light-raider evasion, but skips the proactive ranged spacing and non-Easy
damage-response rotation. Campaign Easy/Medium suppress that damage-triggered
manual vehicle repair; other structure/RETREAT repair paths still exist.
Medium+ adds proactive spacing; Hard/Brutal have offensive air targets.
Infantry quotas are 18%/15%/12%/10%. Most targeting, factory planning,
Starport economy decisions and local defense planning are shared. Some legacy
settings (`structureDefenders`, `harvesterDefenders`, `ornithopterAttackThreshold`)
are loaded but not referenced by the current QuantBot implementation. No further
difficulty tuning was made during this audit.

Sources: `config/QuantBot Config.ini.default`, `src/players/QuantBotConfig.cpp`,
`src/players/QuantBot.cpp` initialization, update and onDamage, `QuantBotBuild.cpp`
build, `QuantBotArmy.cpp` attack and launchGroundHunt; `include/dunecity/VanillaEconomy.h`.

## Human difficulty proposal — not implemented

**Update:** the initial pressure/power experiment is implemented in local 669.
See the [implementation settings and verification](campaign-ai-difficulty-matrix.md).
The paragraphs below retain the reasoning and original proposed ranges.

The complete [current/proposed enemy and partner matrix](campaign-ai-difficulty-matrix.md)
extends this proposal with Stefan's Easy/Medium single-house assault requirement
and demand-covering Windtrap construction. General power is currently disabled
for default Vanilla campaigns, so construction and power consequences are
separate design decisions.

Stefan clarified that Easy self-play victory is not evidence that Easy is
comfortable for a human. Treat it as a regression check for AI solvability;
calibrate difficulty through human play at normal speed, including recovery from
ordinary mistakes. Four Easy-9 wins do not establish beginner accessibility.

Proposed first experiment: differentiate campaign enemy attack pressure using an
alliance-wide active-assault budget, both unit count and combat value, plus a
quiet interval after a wave ends. Existing per-house percentages permit multiple
houses' waves to stack, and replacements can maintain pressure. Count scripted
reinforcements that join the offensive toward the same pressure allowance.
Defenders should still protect their bases, with bounded pursuit so a defensive
response cannot bypass the assault limit.

Initial values to test, not validated defaults: Easy 3–5 active attackers and
2–3 minutes recovery; Medium 6–8 and 90–120 seconds; Hard 10–14 and 45–75 seconds;
Brutal 16–24 and 20–40 seconds. Scale and validate by mission, with a combat-value
cap preventing five expensive vehicles from being treated like five infantry.
Easy should use one front and limited economy harassment; Hard/Brutal can use
flanks, deliberate harvester raids, repair rotations and coordinated fronts.
Keep useful economic behaviour (including necessary Starport purchases) intact.
The opening grace period should be mission-aware and generous on Easy.

Measure first attack, concurrent enemy attack strength, quiet time, base/economy
survival after a lost engagement, and whether a human can rebuild a lost refinery
or army. Test representative early, middle and late missions with humans at
normal speed. No additional balance change was implemented in response to this
design question.
