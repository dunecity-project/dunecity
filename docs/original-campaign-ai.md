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
| Mission teams | parsed then unused | authored movement class, min/max membership and team script |
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

Implementation and validation are in progress. The separately frozen 1.0.868
release is not changed by this work. New state requires explicit save-version
coordination; older saves must retain the previous controller behaviour.
