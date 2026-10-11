# Campaign QuantBot: current behaviour and proposed difficulty design

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
