# Original campaign AI

Requested 2026-11-14: restore the selectable `Campaign AI` opponent to original
Dune II campaign behaviour; keep QuantBot a separate choice.

Reference: [Dune Dynasty, revision 755876a710c0b188075ebe89cb40c068490e08e2](https://github.com/gameflorist/dunedynasty/tree/755876a710c0b188075ebe89cb40c068490e08e2).
Use its **original/non-Brutal** branches, distinguishing optional enhancements
from original behaviour. This is a policy adaptation to DuneCity's simulation,
not execution of the original engine or a claim of identical frame-by-frame combat.

Verified differences in the previous controller:

| Behaviour | Existing controller | Original reference |
| --- | --- | --- |
| Factory selection | fixed `(house + item) % 4`, menu order | random byte per available candidate, original unit order, 25% replacement then priority |
| Soldier/trooper production priority | 20/50 | 10/20 |
| Produced combat units | wait for damage, gather eight, 12-second wave | each unit starts its AI action (normally Hunt) |
| Mission teams | parsed then unused | never created: the original loader rejects every campaign team line (see Mission teams) |
| Base damage | scramble every available defender and launch full army | original full-scale-attack flag has no effective army order (original Carryall filter bug) |
| Harvesters attacked | whole-base scramble | no added controller-wide scramble |
| Rebuild queue | erase unavailable or temporarily blocked slots | retain five destroyed-structure slots; select first buildable, clear only successful placement |
| Death Hand | strongest house/base centre, nuclear override | first enemy structure in object order |
| Structure target priority | same +1 formula as units | combined priority divided by distance; no +1 |
| Factory upgrades | buy upgrades before production | AI structures receive available upgrades on placement |

Evidence is in upstream `src/ai.c` (`StructureAI_PickNextToBuild`),
`src/table/unitinfo.c` (`priorityBuild`, `actionAI`), `src/structure.c`
(`Structure_Place`, `Structure_HouseUnderAttack`, rebuild placement, palace),
`src/unit.c` (target selection) and `src/script/team.c`/`src/team.c`.
The locally supplied `DUNE.PAK` contains `TEAM.EMC`; its Normal and Guard
scripts are identical, Kamikaze prioritizes structures, Staging recruits and
regroups until the minimum, and Flee has no effective action. No extracted
commercial asset is added to source control.

Team execution reads the original `TEAM.EMC` from `DUNE.PAK`, retaining one
instruction per original 5–12 tick scheduler turn and the script's delay counts.
The saved state includes instruction pointer, return register, stack, delay,
team IDs and the next scheduler tick. It does not replace that timing with a
fixed wave interval. DuneCity's tile-centre movement/weapon/pathfinding rules
remain engine adaptations; this does not emulate original sub-tile combat.
Scripted team attacks use Hunt movement and retain the team's selected target
through autonomous target refresh, without marking the order as player-forced.
Explicit orders, legacy Campaign AI and other controllers do not use this hook.
Blocked rebuild locations remain blocked rather than reproducing original
collision-ignoring placement. Ornithopters use DuneCity's Hunt flight/combat;
the original STOP action's separate aircraft script is not emulated.
Mods whose factory has no legal original unit may build a legal replacement
combat unit using the same random/priority selection rule. That extension never
changes the original roster's candidate order or random draws.

## Activation (corrected 2026-11-15)

Dune II keeps a computer house asleep - no production, repairs, team scripts or palace
weapon - until the player's own sight uncovers one of its ground units (upstream
`src/unit.c`, `Unit_UpdateMap` calling `Unit_HouseUnitCount_Add(unit, g_playerHouseID)` for a
unit on a tile unveiled to the player; `Unit_Server_HouseUnitCount_Add` then sets
`isAIActive` on both houses unless they are allied or the unit is winged). A computer house
seeing the player's units wakes nobody, and seeing a structure does not count.

1.0.868/1.0.869 woke every house at the first update instead: `UnitBase::updateVisibleUnits`
tested the object visibility flag, which is set for every deployed unit. The pre-9886
controller tolerated that because its waves waited for damage; the original team scripts do
not, so they recruited the whole authored army at once and sent it at the player's starting
base within two game minutes (levels 6-9 lost in 3-7 minutes in the 2026-11-15 comparison,
`outputs/campaign-ai-fidelity-20261115`). An original-rules Campaign AI house now wakes only
when a house on the other side has had the tile of one of its ground units in sight
(`OriginalCampaignPolicy::sightActivates`). With "Start with Explored Map" only recorded
sight counts, not the pre-explored map. The pre-9886 controller and every other controller
keep waking at the first update. Protocol 126; save format unchanged (`isAIActive` was
already saved).

## Mission teams (corrected 2026-11-15)

Dune II never creates a campaign team. `Scenario_Load_Team` (upstream `src/scenario.c`,
identical in OpenDUNE) returns unless a comma follows the fifth value, and every `[TEAMS]`
line of the campaign ends at the fifth value (`1=Ordos,Normal,Foot,2,4`). `TEAM.EMC` therefore
never runs: mission-placed units keep their authored Guard, Area Guard, Ambush or Hunt action,
and the only attackers are built units and reinforcements, each on its own.

Measured with a windowless build of Dune Dynasty's game logic (original rules, enemy awake from
the first tick, idle player, levels 4-9, 90 runs): no team is created in any run, and about five
enemy units reach the player's base in the first ten minutes, one or two at a time. 1.0.868 and
1.0.869 parsed the teams with DuneCity's own tolerant loader and ran the script for them, which
put a median of ten units at the base at once
(`outputs/campaign-ai-fidelity-20261115/dune_dynasty_compare.py`).

An original-rules house now creates no mission team
(`OriginalCampaignPolicy::referenceCreatesTeam`). The script machine and its callbacks stay in
the code, covered by the runtime probe through a test-only switch, in case the fan "v1.07 fix"
behaviour (teams repaired, a harder game than the original) is ever wanted as an option. Teams
already saved by 1.0.868/1.0.869 keep running until their mission ends.

## Unit cap (corrected 2026-11-15)

Dune II refuses a house at its `MaxUnit` every new unit except aircraft (upstream
`src/pool/pool_unit.c`, `Unit_Allocate`; `MaxUnit` is read in `src/scenario.c` and defaults
to 39). The campaign gives its computer houses 15 to 25, and from level 5 they start with 11
to 23 units already placed, so the original enemy mostly replaces losses. Each unit counts as
one and a unit still in a factory counts, because `Structure_BuildObject` allocates it when
production starts. Mission-placed units and reinforcements are not refused.

DuneCity ships "Maximum Number of Units Override = 0" (unlimited), which replaced every
house's authored number, so an original-rules house produced from all its factories without
end. In an authored mission (campaign or single mission) such a house now keeps its own
`MaxUnit` under both defaults of the option (0 and -1); only an explicit number above 0
replaces it. `CampaignAIPlayer::pickNextToBuild` applies the original count
(`OriginalCampaignPolicy::unitCapBlocks`). The player's house, QuantBot, the pre-9886
controller, custom maps and saves made before this change are unaffected. Not reproduced:
the engine-wide pool of 102 units shared by all houses.

New games default to `OriginalCampaignAI=true` in `[General Behavior]` of the
runtime QuantBot configuration; `false` selects the previous controller for
controlled comparisons. QuantBot remains a separate controller. Do not add the
switch to the shipped default INI: changing that asset's checksum would reject
older campaign saves. The switch is included in the network configuration hash.
Save format 9886 persists the selected mode and team VM state; older saves select
the legacy mode. Protocol 125 separates these decisions from older network peers.

Final combined-source validation on `54903ac010ba78d4600a74c5393bf8be6c93848c`
(2026-11-14): AWS CTest `20261114T190827-original-campaign-reviewed-9f8da4`,
**5/5 passed**. `6de739e46` adds only handover documentation.
The runtime fixture covers produced Hunt, authored guards/team pursuit, rebuild
slots, scoped free upgrades, old controller stream boundaries and real saved-world
continuation (live/checkpoint and repeated ordinary loads, 1500 cycles each).
The 25-match fixture covers all three houses at levels 1/4/9, legacy mode,
DuneCity/Tornie content and repeatability. All three QuantBot OFF/ON controls have
identical final simulation digests. Policy/VM and config registry checks pass.
These checks do not assert identical combat to the original executable or a
particular win rate against QuantBot.

Full matrix on `6de739e46`: 1665 scenarios, zero engine errors. A fresh matched
350-case baseline on `8ecc2d433` gives identical outcomes and final simulation
digests in every shared case. The full tier reports actual wins, losses and
timeouts; it does not assert that every matchup wins. Evidence is retained in
`outputs/campaign-original-ai/baseline-comparison.json` and `full-report.md`.

Release automation-only fix `6470b576c` is merged without changing those game
inputs. Native build/focused checks, both All-against-Atreides gates, FFA, smoke
matrix, GUI performance and staging integration remain pending. The four AI
CTest registrations are macOS-only; a cloud attempt matched no tests. The
separately frozen 1.0.868 release is not changed by this feature; its coordinator
retains the native lane until its Mini release jobs finish. No feature deployment
has been performed.
