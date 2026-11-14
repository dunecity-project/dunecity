# Campaign QuantBot: current behaviour and proposed difficulty design

## Current test-fixture contract (2026-11-13)

QuantBot difficulty checks must use actual `qBotEasy`, `qBotMedium`, `qBotHard`
or `qBotBrutal` enemies, matching the requested enemy tier. `CampaignAIPlayer`
is the separate legacy scenario-settings controller and has no QuantBot
difficulty; a win against it does not validate any column of this matrix.
The old 63/66 campaign recovery result used that legacy enemy and is not evidence
for the corrected QuantBot fixture or the reported Single mission behavior.

The existing scenario harness now passes `--enemy-ai quantbot` and
`--enemy-difficulty` explicitly, records/rejects mismatched actual rosters, and
uses distinct result identities for enemy tiers and `--campaign-mode full|single`.
Full campaign remains the default; Single mission must use its real native setup,
not a renamed campaign. Both retain the authored mission objectives and seeds.
Idle human plus QuantBot AI support remains the default; no-human diagnostics
cannot substitute for it. Support/custom economies must not acquire enemy limits.

Use the [focused fixture and engine commands](ai-scenario-matrix.md#focused-parity-and-difficulty-checks)
before any wider study. All four enemy tiers and both modes have harness coverage;
that is not a claim of new live-engine win-rate evidence. Equal-tier 45–55% and
one-tier-higher ≥95% were working calibration targets, not strict small-sample
gates. Keep actual losses, time limits and failures, native quota/conquest rules,
and exact source/config/options identity. This fixture correction neither tunes
AI economy defaults nor authorizes a broad calibration run.

The dated implementation/audit sections below remain historical evidence for
their named source revisions, not proof that the current engine or either menu
mode passes every proposed target.

## Vanilla level 9 partner calibration (2026-11-13)

Stefan's targets for idle human plus QuantBot support against QuantBot enemies on
Vanilla level 9 (mission 22): Easy v Easy at least 50%, Medium v Easy most games,
Hard v Medium at least 50%, Hard v Hard at least 10%, earlier levels higher.
Measured with the opt-in `campaign/partner-<tier>-v-quantbot-<tier>/vanilla`
cells, 3 houses x 8 seeds per pair.

| Player v enemy | Before | `CampaignAllyStrengthFloor` (seeds 9001-9008) | Held out (9101-9108) |
|---|---|---|---|
| Easy v Easy | 9/24 | 20/24 | 20/24 |
| Medium v Easy | 21/24 | 21/24 | 20/24 |
| Hard v Medium | 19/24 | 17/24 | 16/24 |
| Hard v Hard | 9/24 | 10/24 | 9/24 |

Cause of the Easy failure: the Easy partner's attack threshold (40% of its small
limit) was below its scripted starting army, so it sent that army into three
enemy bases in the first second and had nothing left for the first enemy waves
at 12 minutes. Enemy settings were not the cause: enemy limits, wave caps and
timers are unchanged since 1.0.833, and Easy enemies ran 1-2 harvesters each.
`CampaignAllyStrengthFloor` (hashed, default on) keeps a campaign partner's
ground waves home until its army matches the strongest opposing alliance, capped
at 75% of its own limit; `campaign-ally-strength-floor-off` is the control.

Level 8 (missions 20 and 21) was the exception, and was already so before the
partner change: those missions start each opponent with two refineries, so the
opponents ran more harvesters, banked idle credits and replaced every loss at
once, where the level 9 opponents have one refinery and run short.

An enemy army ceiling (final-level size 6600/8250/10000) was tested twice and
did not help, so it was removed. Also tried and removed, because none moved a
result beyond noise: Hard repeat delay +60 s, Hard level 9 army limit 8,000, a
partner strength floor capped at 95% of its limit or uncapped, a partner wave
that needs the home troops alone to meet the required army, and a partner Light
Factory opening with a threat production floor (worse).

### Helper v enemy difficulty gradient (2026-11-14)

Naming: the helper is the QuantBot on the player's side, the enemy is a campaign
opponent. Stefan's objective: easy for first-time players on Easy, a challenge
for veterans, and a gradient in both directions (an Easy helper loses to a Hard
enemy). Approved targets on the final mission with an idle human, win %, helper
rows by enemy columns Easy / Medium / Hard / Brutal: Easy 70 / 35 / 10 / 0,
Medium 85 / 50 / 20 / 5, Hard 95 / 75 / 30 / 10, Brutal 98 / 90 / 55 / 15.
Matching difficulties by level: levels 1-3 100 / 95 / 85 / 60, levels 4-6
95 / 80 / 60 / 40, levels 7-8 85 / 65 / 45 / 25. The Brutal enemy stays at
full strength.

All levers are hashed `[Force Concentration]` integers. There is no
outstanding-force or recovery blocking: an enemy wave is limited by its own cap.

| Enemy lever | Easy | Medium | Hard |
|---|---|---|---|
| `Campaign<Tier>Harvesters` (most it keeps; 0 = no ceiling) | 1 | 2 | 4 |
| `Campaign<Tier>WaveValue` (one ground wave) | 1700 | 2500 | 4000 |
| `Campaign<Tier>OpeningDelaySec` (after the map trigger) | 0 | 0 | 60 |

| Helper lever | Easy | Medium | Hard | Brutal |
|---|---|---|---|---|
| `CampaignHelper<Tier>Harvesters` (most it keeps; 0 = spice budget alone) | 4 | 6 | 8 | 0 |
| `CampaignHelper<Tier>ArmyFloor` (ground army it funds before its economy, from two refineries) | 0 | 0 | 3000 | 6000 |

- Before the helper levers every helper difficulty played the same game: army,
  harvesters, refineries and income were identical to the unit through minute
  12 on missions 14, 20 and 22, and Hard and Brutal through minute 18. The
  helper builds 8-11 refineries and 9-14 harvesters first; its first combat
  factory arrives near minute 9 and it adds no army before minute 11-12.
- With the harvester ceilings a mission 22 helper holds about 7,000 / 8,000 /
  14,000 / 15,000 army at minute 24 (Easy / Medium / Hard / Brutal).
- Easy keeps its staged 900 / 1,200 / 1,700 wave values and its 3/4/5 unit cap.
  Scaling Medium and Hard waves down on early missions was tried and removed:
  it erased the Hard column's gradient on levels 4-7.
- Opening: the anchor is the map's first enemy combat reinforcement (minute 12
  on mission 22), plus a 0-2 minute per-house stagger, plus the delay. Dune
  Dynasty likewise wakes its AI when the first scheduled reinforcement lands.
  Missions 1-4 have no trigger and always open in minutes 4-6; a later map
  without a trigger never attacks.
- A Medium enemy may build one Repair Yard (tech above 4) and adds a Spice Silo
  when its storage is 90% full; its storage is rarely full.
- No campaign enemy uses coordinated attacks at any difficulty: every
  coordination path requires a Custom game outside campaign policy.
- Tried and removed: a helper army value limit (a low limit lowers the attack
  requirement, so the helper sends its starting army at minute 0), a helper
  first-factory lever (the factory arrives by minute 6 but the yard keeps
  buying refineries and the factory stays idle), and `CampaignCombatBudget`
  for Hard/Brutal helpers (no effect against Brutal enemies).

Measured win % (games). **Scope: Full campaign, Vanilla rules, idle human,
helper role Both.** These are historical measurements of that one setup. They
are not Single mission results and not odds for the current release.

- Source: levels 4-9 were measured at `264f44e0a`; levels 1-3 were measured
  again at `f3c3cc09a`, after the quota saving rule below (that rule cannot act
  on later missions, which carry no quota). No released build was measured.
- Seeds: 3 houses x seeds 9601-9603 on every mission, with seeds 9604-9609
  added on missions 20-22.

Helper rows, enemy columns:

| Levels | Helper | Easy | Medium | Hard | Brutal |
|---|---|---|---|---|---|
| 1-3 (63) | Easy | 98 | 100 | 93 | 84 |
| | Medium | 100 | 98 | 96 | 85 |
| | Hard | 98 | 100 | 95 | 87 |
| | Brutal | 98 | 98 | 98 | 87 |
| 4-6 (81) | Easy | 100 | 95 | 59 | 9 |
| | Medium | 100 | 97 | 71 | 19 |
| | Hard | 100 | 100 | 95 | 20 |
| | Brutal | 100 | 100 | 95 | 18 |
| 7 (27) | Easy | 96 | 77 | 48 | 0 |
| | Medium | 96 | 85 | 51 | 11 |
| | Hard | 100 | 100 | 88 | 44 |
| | Brutal | 100 | 100 | 88 | 29 |
| 8 (54) | Easy | 81 | 33 | 1 | 0 |
| | Medium | 81 | 40 | 3 | 0 |
| | Hard | 100 | 90 | 31 | 0 |
| | Brutal | 96 | 81 | 40 | 0 |
| 9 (27) | Easy | 81 | 22 | 0 | 0 |
| | Medium | 96 | 62 | 0 | 0 |
| | Hard | 96 | 77 | 40 | 0 |
| | Brutal | 92 | 77 | 51 | 0 |

An average over a level hides single maps. The level 9 cell for a Hard helper
against Medium enemies is 21 of 27 (shown as 77), but on Harkonnen mission 22
that same pairing won 4 of 9. In the logged seed 9603 game, three Medium
houses launched waves within a minute of each other. Read a cell as "this
share of these maps and seeds", not as the chance of winning any given
mission.

#### Single mission, repair and yard-upgrade fixes (2026-11-14, 1.0.868)

A separate, smaller measurement made for the 1.0.868 fixes
(`CampaignRepairRoleFix`, `CampaignDeferredYardUpgrade`). Scope: Single
mission, Vanilla rules, idle human, helper role Both, missions 14, 20 and 22,
three houses, seeds 9701-9704, 80-minute limit, concrete on, powered turrets
off. Source `2778733d3`; all four arms ran on the same build. Wins of 36:

| Helper v enemy | Both off | Repair only | Deferred upgrade only | Both on |
|---|---:|---:|---:|---:|
| Hard v Medium | 31 | 31 | 32 | 32 |
| Medium v Hard | 10 | 17 | 10 | 16 |

- Medium helper against Hard enemies, both on against both off: six losses
  became wins and none went the other way (paired mean +0.33, bootstrap 95%
  interval +0.11 to +0.61).
- Hard helper against Medium enemies: three improvements and two regressions;
  no improvement is established.
- A Medium helper lost all 12 tested mission 22 games in every arm. That is a
  bounded observation, not proof that the mission cannot be won.
- Other maps, helper roles, options, the DuneCity ruleset and an active human
  were not measured.

Evidence: `~/Documents/projects/outputs/wtfrepairyard-20261114/release-handoff.md`
and `stats-audit.md` in the same directory.

`tests/ai/scenario-matrix.py --attack-events` returns `campaign-ai.csv` (the
game's own `CAMPAIGN_AI:` log lines) and
`tests/ai/campaign-game-report.py RESULTS... [--game REGEX] [--lost]` prints
one line per game or a full game story from it.

Still open:

- The historical level-8 Brutal-helper/Hard-enemy aggregate was 22 wins,
  31 losses and one time limit in 54 Full campaign games (40.7% wins), on
  `264f44e0a1d3bfc0b39466d805cf7fd4badc94bf`. Harkonnen mission 20 was
  4/9 wins; mission 21 was **0/9**. These are measured results, not the
  separate 55% final-mission target, and do not establish Single mission
  performance. The `losing` save from 1.0.867 is Single mission Harkonnen
  mission 21, Brutal helper against Hard enemies. Its captured first 24
  minutes show no helper ground dispatch and ten enemy dispatches; the
  helper's army falls below the opposing-alliance readiness floor while
  suffering losses at home. This identifies a failure mode, not proof
  that removing the floor improves results.
- The Hard enemy wave default increases from 3500 to 4000 for 1.0.868.
  Historical results above used 3500 and must not be presented as measured
  performance of the new default. Unpinned 1.0.867 saves
  accept this exact config migration only after reconstructing and matching
  the entire old checksum; unrelated content mismatches still fail. Saves
  with immutable Workshop revisions retain their exact pinned content.

- Brutal enemies win every game on levels 8-9 and most from level 4. A Brutal
  enemy sends its whole army on the trigger (5,000-7,000 on missions 8-12,
  about 25,000 from three houses on mission 22) while the helper has earned
  7,000-14,000 credits in total. The army floor raises the helper's army at
  minute 12 from about 1,650 to 3,300 on mission 8, which does not close it.
- Hard and Brutal helpers are still close (8 against 10-14 harvesters), and the
  Brutal helper is not better than Hard against Easy and Medium enemies.
- Easy and Medium helpers win no level 9 game against Hard enemies (targets
  10 and 20).
- Harkonnen mission 22 is the weakest final map: five of the six Brutal helper
  losses to Medium enemies are there, with 3,400-4,700 army at minute 12.
- Atreides mission 20: the helper attacks two bases with 16 rocket turrets at
  minute 13-16 and its map has about 45,000 credits of spice.
- Ordos mission 4, seed 9602, against a Hard enemy: the helper loses all but
  two structures to the minute 4 opening, keeps 1,009 credits it does not
  spend, and the enemy holds 700 army it does not send. The game runs to the
  80-minute limit for every helper difficulty.

Quota saving (`CampaignHelperQuotaSaving`, hashed, default on): missions 1-4
carry a credit quota (1,000 or 2,700). Mission 1 and Atreides mission 4 have
win flags 6, so destroying the enemy does not end them; only the house's banked
credits do (`checkQuotaWin` in `src/House.cpp`). Hard and Brutal helpers spent
every credit: on Atreides mission 4 the enemy was gone by minute 12, the helper
bought 9-10 harvesters and 8,000-9,300 army, the map's 15,000 spice ran out by
minute 18 and the game reached the 80-minute limit in 12 of 24 Hard/Brutal
helper games. A helper sharing the player's house now stops placing orders
once half the quota is banked, or once the map holds less than three times
the spice still needed. After the fix all 48 Atreides mission 4 games are won,
and quota missions overall went from 537 to 558 wins of 576 (time limits 21 to
4, all four on Ordos mission 4 seed 9602 above).



## Implemented local candidate: 1.0.672

Stefan authorized implementation after reviewing this matrix. The following
settings now apply to QuantBot campaign enemies. The original audit/proposal
below remains the design history, not the current release status.

| Setting | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Simultaneous assaulting houses | 1 | 1 | 2 | All |
| Combined troops: tech ≤3 / 4–6 / ≥7 | 3 / 4 / 5 | 6 / 7 / 8 | 10 / 12 / 14 | 16 / 20 / 24 |
| Combined combat value: same stages | 900 / 1,200 / 1,500 | 2,550 / 2,975 / 3,400 | 5,500 / 6,600 / 7,700 | 11,200 / 14,000 / 16,800 |
| Recovery seconds: same stages | 180 / 150 / 120 | 120 / 105 / 90 | 75 / 60 / 45 | 40 / 30 / 20 |
| Extra opening grace | 120 seconds | 60 seconds | None | None |
| Maximum sortie duration before withdrawal | 150 seconds | 180 seconds | 240 seconds | 300 seconds |
| Human partner's home reserve by army value | 25% | 15% | 10% | 5% |

Campaign enemy readiness is the smaller of its legacy army threshold and
twice the combined sortie value cap. This lets Easy retain a defensive reserve without first
amassing an oversized reserve; failed readiness checks retry within 15 game
seconds. Turn selection uses the same criterion. The opening/recovery gates
still apply. Easy/Medium/Hard enemy worker limits retain their configured initial
refinery multipliers even under a higher game-wide maximum. A lower user ceiling
still applies. Human-house partners retain their growth policy; Brutal keeps its
broader economy policy. Existing excess workers are not removed.

All times are game time. Combat value uses purchase value with a minimum of 100
for free/cheap scripted troops. Easy/Medium enemies can commit at most half their army value; Hard/Brutal keep
their configured fractions. The shared allowance applies in addition. Multiple eligible houses share the troop
and value allowance. A depleted Hard/Brutal house may send one unit above its percentage
allowance, but never above the shared count/value ceiling; Easy/Medium keep their
half-army reserve. An active wave cannot
be topped up; one house's aircraft and ground troops occupy the same saved slot.
Mixed-difficulty enemy alliances use their lowest active difficulty's pressure
profile. Eligible houses rotate by oldest launch, with deterministic house ties.

The opening uses the previous tech-based 8–12-minute anchor, extended toward the
house's first scheduled combat reinforcement when later, capped at 12 minutes,
then adds tier grace. It preserves reinforcement arrival times. This consumes
mission reinforcement metadata; it does not emulate Dynasty's TEAM.EMC/contact
activation engine. Save 9838 stores opening, launch, last activity, front target
and member IDs; earlier saves initialize these conservatively on first update.

Easy/Medium use one shared base objective and avoid deliberate light-raider
harassment. Hard/Brutal split base/economic targets and try a lateral approach
for alternating ground units when terrain permits; Brutal varies sides by
house. Existing tactical spacing/repairs and offensive-air permissions remain.
Defensive contacts near owned structures include the attacker's weapon range
plus two tiles (at least 7 on Easy/Medium and 10 on Hard/Brutal). Harvesters are
protected at remote spice fields too: at least four tiles, extended for the
attacker's range. Waiting defenders use Area Guard. Direct hits trigger bounded
retaliation; buildings and harvesters summon threat-sized reinforcements even
during opening grace/recovery. Defensive pursuit remains anchored to the contact.
Repair retreats are preserved. These corrections in 670 replace the overly
restrictive 669 defense perimeter; pre-fix wins do not establish human balance.
Scripted HUNT troops outside the wave are held and recalled instead of creating
an extra assault. Ordinary local defense remains available during recovery.

Easy/Medium campaign builders now cover actual power deficits, queued demand and
the next planned structure with Windtraps. This includes economy support and the
full partner; city mode keeps its existing generator planner. No general Vanilla
power penalty was enabled. Needed Starport imports remain unchanged. Partners
retain the existing growth and army-target differences plus the home reserves
above; Easy full partners can now use the normal damage-triggered repair path.
The game menu describes these implemented behaviors. Further expansion/micro
ideas in the proposal are future tuning, not a claim of a new economy planner.

Validation on clean 672 source `27f14ff`: all six CTest targets and the real-engine
defense, pressure and pacing fixtures pass. Twenty-two full native matches
completed with 17 wins, four time limits and one defeat. Easy/Easy levels 4,5,9
and Hard/Easy levels 8,9 won both seeds. Late Hard/Brutal partner thresholds can
still stall an army after spice exhaustion; these time limits do not establish
healthy balance. See [the full results](campaign-ai-validation-672.md) for the
matrix, exact conditions and limitations. A fresh 672 browser Easy/Easy level-4
match also won normally, with 427 points and 16 minutes displayed. The local
preview serves that tested build with an O2/no-StackIR link override, documented
in the validation report; stock O3 optimization was cancelled. This candidate
has not been pushed or publicly deployed.

## Original audit and proposal

13 September 2026. Audited against local source `6aaa634` (1.0.668) and the
shipped Vanilla QuantBot configuration, also checked in the running browser.
The running 667 game has the same AI behaviour; 668 changes score attribution.
This section records the original design proposal before the 669 implementation above.
Player config overrides and other mods can change the baseline.

Stefan's requirements: Easy self-play success does not demonstrate accessibility
for humans; Easy should survive late campaigns against Easy; Easy/Medium should
build Windtraps to cover required power; multiple Easy/Medium enemies must take
turns attacking. Overlapping enemy attacks are reserved for Hard/Brutal.
Numerical proposals below are initial test ranges, not validated defaults.

## Current enemy AI

Values are per enemy house unless stated otherwise. Army value means the bot's
combat-value accounting, not a troop count or free units.

| Behaviour | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Ground troops committed to HUNT | Up to 25% of army value | 40% | 50% | 60% |
| Multiple enemies attacking together | Allowed; no shared limit | Same | Same | Same |
| First offensive timer | Tech-dependent 8–12 game minutes | Same | Same | Same |
| Subsequent attack checks | 90 seconds with 75–125% jitter | Same | Same | Same |
| Guaranteed recovery after an assault | None | None | None | None |
| Army production goal / starting combat value | 2.0x | 2.5x | 3.0x | 3.5x |
| Readiness / army goal | 50% | 40% | 30% | 30% |
| Infantry share target / military value | 18% | 15% | 12% | 10% |
| Regular AI update cadence | 50 cycles (0.8 game seconds); damage callbacks also react | Same | Same | Same |
| Base development | Mostly replace scenario starting base plus permitted additions | Same | Same, minimum refinery target 2 | Broader development planner, minimum refinery target 4 |
| Windtraps in default Vanilla | Restore starting base; no requirement to cover actual demand | Same | Same | Prerequisites/development; no general demand requirement |
| Necessary Starport harvesters + first carryall | Buy when stocked, affordable and needed; no bargain-price requirement | Same | Same | Same |
| Local defense | Shared planner, about 1.25x nearby threat value | Same | Same | Same |
| Ranged-unit evasion | Reactive on-hit evasion | Also proactive spacing | Also proactive spacing | Also proactive spacing |
| Damage-triggered vehicle rotation/repair | No non-Easy rotation branch | Rotation; campaign damage-triggered manual repair suppressed | Rotation and eligible repairs | Rotation and eligible repairs |
| Offensive aircraft raids | Disabled; defensive interception remains | Same | Enabled | Enabled |
| Targeting and harassment | Much shared logic; no explicit Easy-only front/harassment limit | Same | Same, plus air raids | Same, plus air raids |
| Mission reinforcements | Authored schedule; outside ground-HUNT budget | Same | Same | Same |
| Construction-yard loss recovery by MCV | No Hard/Brutal rescue path | Same | Rescue path available | Rescue path available |

Important qualifications:

- Scenario 21+ (scenario ID, not displayed level) sets Easy's army goal to at
  least 2,000, Medium's to at least 4,000, and Hard's to 10,000. Brutal retains
  its normal calculation. Refinery minima are build targets, not free buildings.
- Multiplying normal readiness by the army goal yields approximately
  1.0/1.0/0.9/1.05 times the starting army. This weakens separation between
  difficulties' first attack strength. A timer is not a promise of an attack:
  readiness and, at higher tech, a Repair Yard also matter.
- Ground budgets count troops already committed. They allow one cheapest
  over-budget unit when there is no existing commitment, to avoid deadlock.
  They do not limit all other sources of offensive pressure.
- Repairs are not entirely disabled on Easy/Medium: structures can be repaired,
  and the periodic RETREAT handling has a repair path. The table describes the
  particular damage-triggered vehicle decision, not every repair mechanism.
- Easy already evades hits with launchers/deviators and light raiders threatened
  by tanks. The earlier audit's blanket claim that Easy does not kite was wrong.

## Proposed enemy AI

These rules control pressure on the human team. They do not give an AI free
credits or force it to ignore an attack on its own base.

| Behaviour | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Enemy houses allowed to assault simultaneously | **1** | **1** | Up to 2 initially | All; deliberate coordination allowed |
| Active attackers across all enemy houses | 3–5 | 6–8 | 10–14 | 16–24 |
| Combat-value ceiling | Add a mission-scaled ceiling as well as troop count | Same, larger allowance | Larger allowance | Largest allowance |
| Recovery after the combined wave ends | 2–3 game minutes | 90–120 seconds | 45–75 seconds | 20–40 seconds |
| Opening | Mission-script-aware start plus generous grace | Script-aware start plus moderate grace | Script-aware start; shorter grace | Script-aware start; earliest intended main assault |
| Fronts and targets | One obvious front; no deliberate repeated harvester raids | One main front; occasional limited economy raid | Flanks and deliberate economy raids within combined budget | Coordinated fronts and economy pressure within combined budget |
| Base development / army production | Keep competent economy; constrain deployed offense first | Same | Keep growth/recovery advantages | Keep broad development/recovery |
| Unit mix / update cadence | Keep existing settings in first experiment | Same | Same | Same |
| Windtrap construction | **Build to actual required power**, including planned next structure | **Build to actual required power**, including planned next structure | Keep current power policy in first experiment | Keep current power policy in first experiment |
| Starport economy | Keep prompt necessary harvesters and first carryall | Same | Same | Same |
| Defensive response | Protect local base and harvesters; bounded pursuit | Same | Stronger tactical response permitted | Strongest tactical response permitted |
| Combat micro | Keep basic self-preservation; no added proactive harassment micro | Basic spacing and limited rotation | Full spacing, rotation and repair use | Full micro with coordinated groups |
| Aircraft | Defensive only | Defensive only initially | Offensive raids included in pressure budget | Coordinated offensive raids included in pressure budget |
| Reinforcements | Preserve arrival script; stage offensive units until a slot/budget permits | Same | May join overlapping assaults within budget | May coordinate within budget |

For Easy/Medium, an attack slot stays occupied while that house's assault is
active. Another house cannot launch merely because the previous launch command
finished. After the combined assault dies, retreats or disengages, enforce the
recovery interval before the next house launches. Rotate houses fairly. Troops
from another enemy cannot join the same assault as a loophole. Bound defensive
pursuit so it cannot become an unlimited second assault. Mixed-difficulty teams
need an explicit rule before implementation; a conservative starting rule is
that any Easy/Medium participant makes the alliance use single-house assaults.

Scale the trial ranges by mission and available unit types. The combat-value
ceilings still need calibration: five launchers are not equivalent to five
infantry. Assault aircraft and offensive reinforcements must also consume
pressure allowance. Mission-critical scripted events need review rather than
silently delaying every reinforcement, including defensive ones.

### What the Windtrap change means

In default Vanilla campaigns, general power requirements are disabled for
**everyone**, including the human. Factory production does not currently slow
when power is short. Windtrap demand-covering construction would therefore add
real construction time and expense to Easy/Medium, but would not by itself make
power shortages shut their bases down. Re-enabling power consequences is a
separate, undecided change; it is not assumed in this proposal. Rocket-turret
power is a separate option and is disabled by default in Vanilla.

## Current AI on the human's team

Here, partner means the full QuantBot selected to share the human's house. It
switches internally to the Custom growth planner even inside a campaign. A
separate allied AI-only house retains the campaign path; team membership alone
does not make it use the partner rules.

| Behaviour | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Army-value goal on maps over 32x32 and up to 64x64 | 8,000 | 12,000 | 20,000 | 40,000 |
| Enemy campaign wave limits | Do not apply | Do not apply | Do not apply | Do not apply |
| Offensive readiness | Normally 40% of army goal | Same | Same; Vanilla threshold capped at 28,000 | Same; Vanilla threshold capped at 24,000 |
| Attack checks | Base 90 seconds with 75–125% jitter, including first check | Same | Same | Same; 15-second retries when below army threshold in Vanilla |
| Economy and harvesters | Shared map/spice/capacity planner | Same | Same | Same |
| Starport harvesters / first carryall | Needed imports ignore bargain requirement and may use economy reserve | Same | Same | Same |
| Base development / power | Growth planner; no general Vanilla power demand requirement | Same | Same, plus rescue-MCV path | Same, plus rescue-MCV path |
| Defense / harvesting safety | Shared local defense and harvester protection | Same | Same | Same |
| Evasion and repairs | Reactive evasion; no non-Easy damage rotation | Proactive spacing, rotation and eligible damage-triggered repairs | Same | Same |
| Offensive aircraft | Disabled | Disabled | Enabled | Enabled |
| Human manual orders | Same temporary human-control protection | Same | Same | Same |

The initial per-difficulty harvester caps do not describe sustained partner
behaviour: dynamic updates replace them with common map limits, then apply
Vanilla capacity, engine limits and remaining spice. Hard's configured harvester
multiplier of 2.5 is also read as an integer (2). Several legacy defender-count
and ornithopter-threshold settings are loaded but unused. Adjusting those INI
numbers alone would not deliver the expected differentiation.

## Proposed AI on the human's team

Make partner difficulty describe its playing strength. Enemy difficulty controls
how much pressure the human faces. A capable partner should not inherit the
enemy's beginner-facing wave restriction.

| Behaviour | Easy | Medium | Hard | Brutal |
| --- | --- | --- | --- | --- |
| Role | Reliable survival and recovery | Balanced defense and steady progress | Strong campaign autopilot | Maximum-strength campaign autopilot |
| Economy / Starport | Competent; prompt necessary imports | Same | Same, more proactive expansion | Same, strongest expansion planning |
| Windtraps | Cover shared house's actual/planned demand | Same | Existing policy initially | Existing policy initially |
| Army-value goals | Keep 8,000 medium-map baseline initially | Keep 12,000 | Keep 20,000 | Keep 40,000 |
| Attacking | Secure economy and retain home defense; cautious opportunities | Balanced attacks with a reserve | Sustained attacks, repairs and flanking | Coordinated groups and multiple fronts |
| Micro / repairs | Basic survival; recover damaged units safely | Spacing and routine repair rotations | Full combat micro | Full micro and coordination |
| Air | Defense | Defense initially | Offense and defense | Coordinated offense and defense |
| Human control | Preserve explicit human orders; no extra enemy wave cap | Same | Same | Same |
| Acceptance target | Recover from setbacks and survive late Easy enemies | Survive and make steadier progress | Reproducible level 8/9 wins against Easy | Strongest results, without hidden resources |

The separate **AI Support** option remains economy/build assistance rather than
full army autopilot. Describe that distinction in the menu. Its intended scope
is construction, production and economy; do not assume it has absolutely no
special-unit handling without auditing those paths. The windtrap/economy policy
should be consistent wherever Easy/Medium support uses the same build planner.

## Validation and sources

First isolate pressure changes from economy/power-policy changes so their effect
can be measured. Existing AI-vs-AI wins establish a solvable regression case,
not human difficulty. At normal speed, test representative early/middle/late
missions and multiple houses/seeds. Record first assault, combined unit/value
peak, overlapping attacker houses, recovery time, and ability to replace a lost
refinery or army. Verify that defenders/reinforcements/air cannot bypass caps,
and that attack-slot state survives save/load deterministically. Keep separate
results for assisted and unassisted humans.

Source locations: `src/players/QuantBot.cpp` (constructor role selection, update,
onDamage), `src/players/QuantBotBuild.cpp` (build), `src/players/QuantBotArmy.cpp`
(attack, launchGroundHunt and checkAllUnits),
`src/players/QuantBotConfig.cpp`, `config/QuantBot Config.ini.default`,
`include/dunecity/VanillaEconomy.h`, `include/dunecity/PowerRules.h`,
`src/House.cpp`, `src/structures/BuilderBase.cpp`,
`src/structures/RocketTurret.cpp` and `src/mod/ModManager.cpp`.
See [campaign AI balance evidence](campaign-ai-balance.md) for measured runs and
the prior Dune Dynasty timing investigation.
