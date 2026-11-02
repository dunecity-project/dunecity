# Research Lab

`Research Lab` is an optional synchronized match rule, off by default. Each house starts at the
map/match tech level and can research independently up to level 9. With the rule off every
technology gate reads the original match level, no research runs, and the checkpoint digest keeps
its original fields and byte order.

## Building and researching

The new building is `Structure_ResearchLab` (item 88), map/ObjectData name `Research Lab`.
It uses Tornie's already-shipped Tech Center graphic (`ObjPic_TechCenter`, `data/TechCenter.png`),
with a 3×2 footprint. It does **not** change or replace the Tornie Tech Center or spawn vehicles.
The default ObjectData entry makes it available from the Construction Yard at tech 1, with no
other prerequisite or yard upgrade: 500 credits, 500 HP, 30 power and build time 96.
Mods can configure its own `[Research Lab]` ObjectData section just like any other building.

Select a lab and press **Research tech level N** in its sidebar. Only one research can run per
house, even with several labs. The complete price is paid on acceptance; a synchronized
`CMD_RESEARCH_TECH_LEVEL(LAB_OBJECT_ID)` validates the acting player, actual lab, ownership,
life/deployment, next level, lack of another active research and available credits. Shared/allied
control follows the existing structure-command authorization rules. Progress is shown as a
percentage, and completion refreshes every one of the house's build lists (normal building
prerequisites and factory/yard upgrades still apply).

The cost and duration depend only on the **target** level:

| Target level | Credits | Game seconds |
|---|---:|---:|
| 2 | 600 | 50 |
| 3 | 900 | 60 |
| 4 | 1,200 | 70 |
| 5 | 1,500 | 80 |
| 6 | 1,800 | 90 |
| 7 | 2,100 | 100 |
| 8 | 2,400 | 110 |
| 9 | 2,700 | 120 |

Formula: `300 * target` credits and `MILLI2CYCLES((30 + 10 * target) * 1000)` simulation
cycles. This is deterministic integer simulation, independent of render rate and wall clock.
Research has no additional power requirement beyond the building's ordinary draw and does not
pause for a power shortage. Losing **the lab performing the research** cancels it immediately,
without a refund; another existing lab or a rebuilt lab can start that level again. Completed
levels are permanent for that house, including after all its labs are lost.

## AI and Random Skirmish

QuantBot's hashed `[Force Concentration] ResearchLab` switch defaults on and is effective only
when the match rule is on. It builds/rebuilds a lab after the basic economy, power and defence
are secure, then researches with an economy/defence cash reserve. Fighter-only and switched-off
controllers do not start construction/research. AI decision telemetry records
`tech_research_started` (`from`, `to`, `cost`, `cycles`, `lab`),
`tech_research_completed` (`tech_level`) and cancellation. Match analytics add the current
`tech_level`, `research_target` and `research_remaining` only for research-enabled games.

Random Skirmish shows a **Research Lab** checkbox below Tech level only below level 9. It is
remembered as `[Random Skirmish] Research Lab`; starting uses that choice, not an implicit free
lab. Existing base/army/city generation is unchanged. The hosted Random Map screen passes the
same choice to its parent lobby; saved map files, like other maps, use the host's Game Rules
when subsequently selected manually.

## Compatibility and regression fixtures

Save 9881 stores the researched level, target, remaining cycles and active lab object ID per
house. Earlier saves initialize the per-house level from the saved match level and do not
activate research. GameInitSettings MOD15 appends the `Research Lab` and `Trooper Anti-Air`
rules; older streams load **both off** rather than adopting a future new-game default. Protocol
111 covers the new command and rules. No app-version change is made here.

`tests/units/run-research-lab-probe.py --build-dir build-int --output-dir <results>` exercises the
real engine's owner authorization, independent houses, build-list refresh, one-research limit,
active-research save/load continuation, cancellation/rebuild, the level-9 cap and the off-state
digest layout. It also runs a tech-1 DuneCity QuantBot custom game with no free lab and writes
levels/cycles to CSV. Run through the shared cluster queue, not a second build tree. Command
serialization and MOD15/old-marker compatibility are covered in `dunelegacy_tests`; the existing
skirmish/menu/gallery probes cover the checkbox at 640×480.
