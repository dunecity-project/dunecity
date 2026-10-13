# Spec: Modern RTS controls

Goal: the command set players expect from StarCraft II / Age of Empires / C&C Remastered, layered on the
Dune Legacy input model (left selects, right orders; optional left-click orders; optional WASD camera).

Already present (verify, keep, do not re-implement): Stop (S / Shift+S with WASD), select same type on
screen (T) / on map (Ctrl+T), control groups Ctrl+0–9 / 0–9, factory rally points
(CMD_STRUCTURE_SETDEPLOYPOSITION), Guard/Area Guard/Hunt/Retreat modes, hover intent cursors,
auto-repair, Space = match pause. Existing key meanings: G next yard, F next factory, P place structure,
H harvester return, R repair / Shift+R road tool, M move, C capture, Shift+1..8 city overlays.

## 1. Attack-move
- Trigger: `A` then left-click (right-click when left-click orders are off follows existing cursor-mode
  conventions), or the new unit-interface button "Attack Move". With WASD camera on: `Shift+A`
  (same convention as Stop).
- Target semantics in Attack cursor: click an enemy object → attack that object (unchanged).
  Click ground → ATTACK-MOVE (new). `Ctrl` + click ground → forced fire at that position (the old
  attack-ground behaviour, preserved).
- Behaviour: unit walks to the point; any enemy within max(weapon range, view range) of the unit on the
  way is engaged; after the fight the unit resumes the walk. Leash: a unit does not chase more than 8
  tiles from where it broke off the walk, then resumes. On arrival the unit enters AREAGUARD at the
  destination (not Hunt). Reuse `UnitBase::doHuntToward` mechanics where possible but do not change the
  AI's existing hunt-toward behaviour (QuantBot depends on it; its probes must still pass).
- Applies to ground units, aircraft that can attack (Ornithopter) and infantry. Harvesters/MCVs/
  carryalls/Saboteurs ignore it as an attack (treat as a plain move; Saboteur keeps its normal rules).

## 2. Hold position
- Trigger: `H` and unit-interface button "Hold". With mixed selections, harvesters still return
  (existing H meaning) and every other selected unit holds.
- Behaviour: new attack mode HOLD appended to `ATTACKMODE` (after RETREAT, before ATTACKMODE_MAX).
  The unit never moves on its own: it fires at enemies within weapon range, turns turrets/bodies but does
  not chase, does not kite, does not auto-retreat to repair, does not scatter from sandworms or
  crushing (it may still be crushed). Launchers/siege fire at targets in range without approaching.
  Any explicit move order ends HOLD (unit returns to its prior default mode, AREAGUARD).
- AI never assigns HOLD unless already asked by authored INI (accept "Hold" as a map-editor/INI mode
  string too).

## 3. Shift-queued orders
- Holding `Shift` while issuing move, attack-move, attack-object, patrol, capture, or harvester return
  appends the order to each selected unit's order queue instead of replacing it. Without Shift, any order
  clears the queue (existing behaviour preserved).
- Queue per unit, max 16 entries, saved (gate 9855), executed in order; an order completes when its
  existing completion condition is met (arrived / target dead or gone / captured). Target-object orders
  whose target died are skipped. Stop or Hold clears the queue.
- Current Shift+left-drag/click (add to selection) is unchanged; queueing applies to the ORDER click.
- Visualisation (local player only, never for enemies/spectators unless they observe that house):
  for selected units, draw waypoint markers and thin lines from unit → each queued point, colour by
  order type (move green, attack/attack-move red, patrol blue). Reuse the existing destination/path line
  drawing if present.

## 4. Patrol
- Trigger: `Q` then click (WASD on: `Shift+Q`), unit-interface button "Patrol". Shift-queued patrol
  points extend the route.
- Behaviour: attack-move between the unit's position at issue time and the patrol point(s), looping
  forever (ping-pong for 2 points, cyclic for more). Saved with the queue.

## 5. Repeat production
- `Alt`+left-click on a unit item in a factory's sidebar build list toggles repeat for that item on that
  factory (Barracks, WOR, Light/Heavy/High-Tech Factory). Not for Construction Yard or Starport.
- When an item with repeat completes and leaves the factory, one more copy is queued automatically,
  respecting credits (it waits like a normal queued item), unit limits (`isUnitLimitReached`: it stays
  queued/paused, not cancelled), and on-hold. Cancelling the item (right-click) also clears repeat.
- Sidebar shows a small loop marker on repeating items. State saved per builder (gate 9855) and changed
  only through a new command (e.g. CMD_BUILDER_SETREPEAT(builder, item, enabled)).

## 6. Control-group extras
- `Alt`+number adds the current selection to that group (Shift+number stays the city overlay toggle).
- Pressing the same group number twice within 400 ms centres the camera on the group (if not already
  present; verify first).

## 7. Selection extras
- Double-click a unit selects all own units of the same type on screen (same as T). Ctrl+double-click =
  whole map (same as Ctrl+T).
- `Ctrl+A`: select all own combat units on the map (excludes harvesters, MCVs, carryalls, frigates,
  sandworms, structures). With WASD on use `Ctrl+Shift+A` if Ctrl+A conflicts.

## 8. Hotkeys shown in tooltips (GitHub issue #87)
- Every unit-interface command button and every mode button shows its hotkey in the tooltip
  (e.g. "Attack Move (A)", "Hold (H)", "Stop (S)"), reflecting the WASD alternative when WASD camera is
  on. Sidebar structure buttons get "Repair (R)" etc. where hotkeys exist.

## Non-goals
Key remapping UI; formations (separate slice); changing AI behaviour.

## Acceptance
- Unit tests (Catch2) for: order-queue state machine (append, clear, skip dead target, cap 16,
  patrol cycling), attack-move leash/arrival rule, HOLD never producing a move, repeat production
  re-queue respecting limits and cancel. Save round-trip test for queue, HOLD mode and repeat flags.
- Real-engine probe(s) via the existing harness: attack-move column engages an enemy on route then
  arrives; HOLD launcher never moves while being shot; 3-waypoint shift queue completes in order;
  repeat production produces ≥3 units unattended; old-version (9854) save still loads.
- `tests/network/run-command-execution-probe.py` (or equivalent command-routing test) covers the new
  commands in multiplayer command execution.
- Smoke in the real game: screenshot showing queued waypoint lines and the tooltips with hotkeys.
- All existing QuantBot/army probes touching UnitBase order code still pass.

---

# As built (feat/rts-controls, 2026-10-13)

Status: implemented in the engine and UI; unit tests, the command-routing probe, the real-engine
behaviour probe and the rendered smoke frame pass.

## Code map
- `include/units/UnitOrderQueue.h` — pure order queue (`UnitOrderQueue`, max 16, save/load) and
  `UnitOrderPolicy` (engage range = max(weapon, view), leash 8 tiles Chebyshev from break-off,
  arrival radius 2 tiles, 3 walk retries, HOLD never approaches / never auto-repairs).
- `UnitBase` (`doIssueOrder`, `doIssuePatrol`, `recordPlayerOrder`, `clearOrderQueue`,
  `updateOrderQueue`, attack-move branch in `targeting()`, HOLD in `engageTarget`,
  `isInGuardRange`/`isInAttackRange`, `doSetAttackMode`, `handleDamage`; `GroundUnit` auto-repair gate).
- `include/structures/ProductionRepeat.h` + `BuilderBase` (`doSetRepeat`, `supportsRepeat`,
  `isRepeating`, re-queue in `update()`, cancel/remove clears repeat).
- `Game::issuePositionOrder(PositionOrder, units, target, queued)` — the single hook for
  attack-move, patrol and Shift-queued waypoints (formation planner plugs in here; today every
  unit gets the clicked tile).
- UI: `Game.cpp` input (A/Q/H/Ctrl+A/Alt+number/double-click/Shift handling, `CursorMode_Patrol`,
  `drawQueuedOrders`), `CursorAppearance` Patrol icon, `UnitInterface`/`MultiUnitInterface`
  (Patrol button, Hold mode button, hotkey tooltips via `UnitCommandTooltips.h`), `BuilderList`
  (Alt+click repeat, loop marker, tooltip).

## Commands, protocol, save
- New commands (before `CMD_MAX`): `CMD_UNIT_ATTACKMOVE(obj,x,y,queue)`,
  `CMD_UNIT_PATROL(obj,x,y,queue)`, `CMD_UNIT_QUEUEORDER(obj,type,x|target,y)` (Move,
  MoveToObject, AttackObject, Capture, HarvesterReturn), `CMD_BUILDER_SETREPEAT(obj,item,enabled)`.
  All owner-checked via `mayActOnObject`, fixed arity in `CommandValidation`.
- Classic unit commands now record their order as the queue front (so Shift after a plain order
  waits for it); ATTACKPOS, HEAL, carryall drop, repair, devastate and every SETMODE clear the
  queue. Explicit move/attack/capture/repair orders end HOLD (→ Area Guard).
- `NETWORK_PROTOCOL_VERSION` 71. `SAVEGAMEVERSION` 9855: UnitBase appends the queue block
  (count, entries, dispatched flag, retries, break-off) after huntToward; BuilderBase appends the
  repeat set after the build list. Both loads gated `>= 9855`.
- Attack mode `HOLD = 10` (after RETREAT); INI/map-editor name "Hold".

## Controls as built
- Attack cursor (A, WASD: Shift+A): object → attack; ground → attack-move; Ctrl+ground → old
  forced fire. Shift queues and keeps the cursor.
- Patrol (Q, WASD: Shift+Q): route = unit position at execution + point(s); Shift adds points
  before the closing anchor; legs within 2 tiles are refused.
- Hold (H): harvesters return (Shift+H queues the return), all other selected units HOLD.
- Shift + right-click / M / C: queued move, attack, follow, capture.
- Alt+click factory unit: toggle repeat (queues one copy if none queued); right-click cancel clears it.
- Alt+1..9 add to group; double-click unit = T, Ctrl+double-click = Ctrl+T; Ctrl+A (and
  Ctrl+Shift+A) selects all own combat units.
- Waypoints drawn for selected local-house units with >1 queued order or an attack-move/patrol:
  move green, attack/attack-move/capture red, patrol blue (loop closed).

## Deviations from the spec
- No separate "Attack Move" button: the Attack cursor *is* the attack-move cursor (spec §1), so the
  existing Attack button is relabelled "Attack Move (A)" with all three click meanings; a second
  button entering the same mode would be a duplicate.
- Double-tap group centring: already present (pressing a group number while that group is
  selected centres the camera, with no time window) — kept as is.
- Mode buttons Guard/Area Guard/Ambush/Hunt/Retreat and Deploy/Destruct/Heal/Paths have no
  hotkey to show; their tooltips are unchanged. Structure Repair (R) / Upgrade (U) / Place (P)
  already show hotkeys.
- HOLD "no scatter from worms / crushing": the engine has no scatter behaviour for player units,
  so nothing needed changing; Saboteurs in HOLD do not force-chase targets.
- A queued MoveToObject on a friendly *unit* follows it indefinitely and blocks later orders
  (same as a classic follow order).
- Attack-move/patrol is abandoned (queue cleared) if the engine sends the unit to a repair yard.

## Verification (exercised)
All through ctest, Release build, `--parallel 3`:

`ctest --test-dir build -R "^(dunelegacy_tests|rts_controls_tests|lobby_authorization_tests|game_command_regressions|rts_controls_probe|deviation_reward_probe|quantbot_whole_army_dispatch|quantbot_launcher_kiting)$"`
→ 8/8 passed.

- `rts_controls_tests` / `dunelegacy_tests`: the `[rts-controls]` Catch2 cases in
  `tests/RtsControlsTestCase.cpp` (queue append/clear/cap 16/skip dead target, patrol cycling,
  engage range, leash, arrival/resume/give-up, HOLD never approaches or auto-repairs, repeat
  re-queue through limits and cancel, save round trip and corrupt-input rejection, command arity).
- `game_command_regressions` requires `RTS_COMMAND_PROBE_PASS` (`tests/network/rts-command-probe.inc`):
  each new command through the wire format and `CommandValidation`, enemy/co-op/owner
  authority, malformed parameters, queue order, HOLD, a lockstep `CommandManager` batch, and
  repeat on a real heavy factory (never on a construction yard).
- `rts_controls_probe` (`tests/units/run-rts-controls-probe.py` + `rts-controls-probe.inc`, real
  units on Tuono-Avon over real `Game::updateGameState` cycles):
  - the committed 9854 save (`tests/units/fixtures/save-9854-orders.dls`) loads with empty queues,
    the factory keeps its two queued items and no repeat, movers continue, and it re-saves as 9855;
  - three tanks attack-moving through a trike target it, kill it while still walking, and all end
    in Area Guard within 2 tiles of the goal;
  - a HOLD launcher attacked by a forced tank stays on its tile, with no destination, in HOLD for
    900 cycles while being hit, and damages the tank;
  - a move plus two Shift-queued moves are reached in order 1, 2, 3 and end on the last waypoint;
  - repeat production builds ≥3 units with no further input, pauses (stays queued and repeating)
    at a reached unit limit, and resumes when the limit is lifted (one copy already paid for
    still rolls out after the limit is reached — existing engine rule);
  - queue, patrol route, HOLD and the repeat flag survive save and load;
  - smoke frame: a rendered 1280x800 frame contains the green move, red attack-move and blue
    patrol waypoint colours on the map; the group panel tooltips start with "Attack Move (A)",
    "Patrol (Q)" and "Hold (H)".
- `deviation_reward_probe`, `quantbot_whole_army_dispatch`, `quantbot_launcher_kiting`: unchanged
  behaviour of existing UnitBase/QuantBot order code.

## Smoke screenshots
Rendered by the probe through the production `Game::drawScreen` (SDL software renderer):
- `/tmp/dunecity-rts-controls-smoke/rts-controls-waypoints.png` — two selected tanks: a move,
  a Shift-queued move and a queued attack-move (green, green, red), and a two-point patrol (blue loop).
- `/tmp/dunecity-rts-controls-smoke/rts-controls-tooltip.png` — the same frame with the hovered
  Patrol button tooltip "Patrol (Q): attack-move back and forth from here; Shift+click adds patrol points".
Each probe run regenerates both files (also copied into `build/rts-controls-probe/`).
