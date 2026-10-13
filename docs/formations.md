# Formation movement (player and AI)

A group ordered to one point arrives together and spread out, with heavy armour in front and
ranged units behind it. Without formations the group moves in single file and piles onto one tile.
This page describes what is built on branch `feat/formations`. The original spec is in the
section "Spec deviations and limits".

## Planner (`include/FormationPlanner.h`, `src/FormationPlanner.cpp`)

The planner is pure and deterministic, and uses integer and FixPoint arithmetic only. It takes
these inputs:

- the members, each with object id, item, current tile, role, reference speed and an infantry
  flag;
- the target tile;
- the map size;
- a `CanStand(member, tile)` query (terrain and structures, never units);
- the infantry capacity per tile (`NUM_INFANTRY_PER_TILE` = 5).

It returns one slot per member, sorted by object id, plus the group speed (the slowest
member's reference speed).

- **Member order:** members are sorted by object id before any other step, so the plan does not
  depend on the order the caller lists them in.
- **Coherent group:** a group is coherent when every member is within 6 tiles (Chebyshev) of the
  integer centroid. Its relative offsets are kept, but each axis is compressed so the gap
  between neighbouring values is at most 2 tiles. The result is recentred on the target.
- **Box:** any other group forms a box. It faces the 8-way direction from the centroid to the
  target. The width is `ceil(sqrt(1.5 n))`.
  - Rows are filled in this order: armour (tank, quad, trike, raider trike, devastator, sonic
    tank and others), then ranged (launcher, elite launcher, siege tanks, deviator, rocket
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

- `planGroup(units, target)` is **the single planning entry point**. It plans eligible units
  only: active ground units on the map, never aircraft or sandworms. `canStand` excludes tiles
  with a structure, and mountains for anything except infantry. Integration should route the
  RTS-controls attack-move, patrol and queued-waypoint orders (`Game::issuePositionOrder`)
  through this function.
- `join(units, groupID, slots)` sets membership and gives the whole group the slowest pace,
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

## UnitBase state (saved, gate 9857)

- **Saved fields** (contiguous, appended after the hunt-toward fields):
  - `formationGroup` (Uint32)
  - `formationSlot` (Coord; invalid means the member keeps its own order and only matches the
    group's pace)
  - `formationSpeedCap` (FixPoint)
- **Not saved:** `formationCatchUp` is recomputed at the start of every cycle, so a loaded game
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

## Command (protocol 71)

The command is `CMD_UNIT_FORMATION_MOVE(OBJECT_ID, X, Y, GROUP_TOKEN)`. It has 4 parameters,
is appended before `CMD_MAX`, and is classified as acting on an owned object.

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
- **`Ctrl+F`** switches formations on or off, shows a news-ticker line and persists the
  setting. Plain `F` still selects the next factory.
- **Options → Controls** has a "Move groups in formation" checkbox. It is on by default and is
  stored as `General/Formation Moves`.

## AI

Who is sent and when are unchanged everywhere. Only how a chosen group moves is changed.
`Player` has three new helpers:

- `doFormationMove(units, x, y, forced)` gives each unit a slot and a move to it.
- `doFormationHuntToward(units, x, y)` gives each unit a hunt-toward walk to its own slot.
- `doFormUp(units)` keeps each unit's existing order and only applies the group's pace.

All three need at least 3 eligible units. Below that, the units get the old plain order, or
nothing changes.

| Caller | What changed |
|---|---|
| QuantBot `launchGroundHunt` (custom Hunt waves, campaign waves incl. flanking) | `doFormUp(wave)` after the unchanged dispatch loop |
| QuantBot `scrambleWholeArmyToDefend` (1.0.832 nearest-hunt) | ≥3 responders: `doFormationHuntToward` to slots around the building's point nearest their centroid; <3: unchanged per-unit walk |
| QuantBot `scrambleUnitsAndDefend` (district defence) | `doFormUp(ground responders)` after the unchanged attack orders |
| QuantBot `applyArmyPosture` (army gathering / recall) | `doFormUp(units recalled this pass)`; each keeps its own assembly slot |
| `CampaignAIPlayer` attack team | `doFormUp(wave)` after HUNT + forced attack |
| `AIPlayer::attack` | `doFormationMove` to the wave destination, then HUNT, then `doFormUp` |
| `SmartBot::attack` | `doFormUp(hunters)` |

## Verification (Mac mini, Release, `--parallel 3`)

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
against QuantBot Hard, vanilla mod, seed 486409243, 30 game minutes.

- A wave is a new `customWave` dispatch with at least 3 ground members that are not already in
  the target area.
- A member arrives when it is within 10 tiles of a live enemy building.
- Shots are the members' primary shots in the 30 s after the wave's first arrival.

The before numbers are from the base build 40f4e7a8. The after numbers are from this branch.

| Map | Waves measured (before → after) | Mean arrival spread | Mean shots in first 30 s of contact |
|---|---|---|---|
| 2P 64x64 Face Off | 5 → 2 | 64.2 s → 45.0 s | 29.2 → 34.5 |
| 2P 128x128 Habbanya-Penny | 3 → 2 | 78.6 s → 83.4 s | 63.3 → 97.5 |
| 2P 128x64 Ergun-Sahij | 4 → 6 | 161.2 s → 109.0 s | 61.0 → 16.8 |
| All waves | 12 → 10 | 100.2 s → 91.1 s | 48.3 → 36.5 |

How to read this:

- The matches diverge before the first wave, because defence responses already use formations.
  So individual waves are not like-for-like, and with one seed the noise is large.
- Arrival spread went down overall and on two of three maps.
- Shots in the first 30 s went up on two maps. On Ergun-Sahij they fell sharply: several of the
  after waves were small or late reinforcements that arrived one at a time. Across all waves
  the mean fell by 24%.
- **This does not yet meet the acceptance criterion that shots must not go down materially.**
  Integration should run the measurement on more seeds and maps before merging. If the drop
  holds up, the place to look first is the defence-response `doFormUp`, which slows fast
  responders to the slowest one.

## Spec deviations and limits

- **No group command:** commands carry at most 4 parameters (`CommandValidation`), so the group
  is sent as one command per unit with a shared token, as the spec's alternative allows. The
  speed cap is computed by the executor from the members rather than sent.
- **Hunt waves get no slots:** QuantBot custom and Hard/Brutal waves are engine Hunt. Since
  1.0.804/1.0.832 the settled decision is that the engine picks each hunter's target; per-unit
  move orders walked waves back into base. These waves therefore get formation membership and
  the speed cap only. Slots are used where the AI already issues position moves: the base
  scramble, `AIPlayer` waves and the player path.
- **What "slowest" means:** slowest is measured on open sand (reference speed). The cap is set
  when the order is given and is not raised when the slowest member dies.
- **Catch-up lag:** "slot-relative progress" is the member's remaining tiles minus the smallest
  remaining distance in the group.
- **Contact ends membership:** contact, meaning an enemy target within `huntTowardRange()`,
  ends membership. The fight is not speed-capped.
- **Token reuse:** a token repeats after about 4.6 game hours, and at the 16th group ordered by
  one controller in the same cycle. A clash with a still-live group would merge the two
  deterministically.
- **SmartBot:** only `doFormUp` was added there.
- **Not built (non-goals):** a shape picker, air or naval formations, and changes to the
  pathfinder.
