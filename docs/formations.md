# Formation movement (player and AI)

A group ordered to one point arrives together and spread out in a chosen shape. Without
formations the group moves in single file and piles onto one tile. Branch `feat/formations`
built the Box; branch `feat/formation-shapes` (save 9859, protocol 73) adds the shapes, the
sonic-lane rule, hunting in formation and the panel buttons described first below; branch
`feat/formation-form-up` (save 9861, protocol 75) makes those buttons form the selection up
where it stands.

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
