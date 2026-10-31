# Construction Yard pre-placement queue (as built)

Choose where a building goes when you order it. The Construction Yard builds its queue in order and
places each finished building on its planned plot automatically.

## 2026-10-31: obstruction recovery

The default `Retry Planned Placement` match option is on. A finished planned building keeps its
plot and retries until it becomes usable, including when it is waiting for a building in another
yard to supply build range. It never becomes permanently manual because a plot was temporarily
blocked. A saved reservation with the old `reservationFailed` marker also resumes automatically;
unreserved orders still use the ordinary manual placement flow.

Own and allied ground units are moved off a finished plot once a second, including units in Hold
or a formation. Idle friendly units are also cleared from queued plots before completion. Enemy
units are identified by team, not by house: hostile ground units on a finished plot cancel only
that reservation, refund its paid production progress through the normal cancellation path, and
allow the next queue entry to proceed. Flying units do not occupy the building footprint. A blocked
friendly/terrain/range plan stays amber with `BLOCKED` and can still be placed elsewhere manually
or cancelled by the player.

The option is available in `[Game Options]` / mod `GameOptions.ini` for same-build control/on
regressions, is included in the settings hash, and is carried in GameInitSettings MOD10 (`MODA`),
save 9876 and protocol 105. Settings predating MOD10 adopt the repair. Setting it false retains
the legacy fallback/enemy-wait behaviour described below. The real-engine `preplace_queue_probe`
exercises both policies in one build, including two-yard range dependencies, recovery of a failed
reservation, a structure removed from a plot, own/allied Hold units, and hostile cancellation with
an exact refund and queue progress. The off/on setting round-trips in the unit tests.

## Legacy control behaviour

The detailed fallback and enemy-wait rules below apply when `Retry Planned Placement=false`.

## Player flow

- Game rule: **Game Rules → "Plan building sites when ordering"** (Custom Game and Random Skirmish),
  default ON. It is per match: `GameOptionsClass::planBuildingSites`, shared by every player in
  multiplayer, part of the game-options hash, carried in `GameInitSettings` (marker MOD8, appended
  after the MOD7 spice income block; MOD7 and older streams read as ON) and saved with the game
  (**SAVEGAMEVERSION 9858**, **NETWORK_PROTOCOL_VERSION 72**). Remembered defaults use the ini key
  `[Game Options] Plan Building Sites` and mod `GameOptions.ini` accepts the same key. OFF = the old
  flow (build, then click to place), including Shift+click = ×5 in the list. The former client
  setting (Options → General, `[General] Choose Building Location`) is gone.
- ON: left-click (or Shift+click) on a structure in a Construction Yard list does not start
  production; it enters the placing cursor for that item (same footprint ghost and colouring as
  ordinary placement; units are ignored, earlier reservations count for build range and overlapping
  one shows red). Clicking the same item again leaves planning.
- Left-click on the map reserves the plot and queues the item (one command). The building stays
  selected, so every further click plans another copy; **Esc**, right-click or clicking its
  sidebar icon again leaves planning without ordering more.
- Several Construction Yards: each planned building goes to the house's yard with the least work
  left (queued prices minus progress, counting orders still in flight); on a tie the yard it was
  picked from, then the lowest object id (`PlacementReservation::chooseYard`). Build range and
  overlap are house-wide, so any yard can take any valid plot; yards that cannot build the item
  (upgrade level, one-palace rule) are skipped. With the rule OFF a list click still orders on
  the selected yard, since that yard is where the finished building waits to be placed.
- All structures go through this flow (vanilla, slabs, R/C/I zones, Police, Stadium, Airport,
  Nuclear Plant). **Road and Power Line** keep the old flow (they build instantly).
- Alt is not used here (reserved for the controls slice's repeat toggle). Only Construction Yard
  lists change; factory lists are untouched.
- Ghosts: the owning house's reserved plots (and the house an observer views) are drawn as
  translucent footprints with a white outline, numbered by position in that yard's production queue
  (unreserved orders keep their number). The plot under construction shows its progress below the
  number. A plot that fell back turns red and shows `N !`. Other houses never see them.
- Cancel: right-click a ghost (with no own unit selected), or right-click the item in the list (the
  existing cancel, which removes the most recently queued order of that item, together with its
  reservation).

## Simulation

- `ProductionQueueItem` carries `reservationID` (0 = none), `plotX/plotY`, `reservationFailed`;
  `BuilderBase` carries `nextReservationID` and `reservedPlacementWaitCycles`. Saved from
  **SAVEGAMEVERSION 9856**; older saves load with no reservations.
- `CMD_BUILDER_RESERVE_PLACEMENT(BUILDER_ID, ITEM_ID, X, Y)`: validates ownership (CommandAuthorization),
  the item (in this yard's list, one-palace rule) and the plot, then queues and records atomically.
- `CMD_BUILDER_CANCEL_RESERVATION(BUILDER_ID, RESERVATION_ID)`: removes exactly that entry. If it was
  the entry under construction its progress is refunded to the pools that paid (as cancelling the
  current item does today); a not-yet-started entry costs nothing.
- **NETWORK_PROTOCOL_VERSION 71** (72 since the rule became a game option). With the rule OFF the
  executor refuses `CMD_BUILDER_RESERVE_PLACEMENT` on every peer.
- Plot validity (`ConstructionYard::checkReservationPlot`, rules in
  `include/structures/PlacementReservationPolicy.h`): normal placement rules with units ignored
  (`Map::okayToPlaceStructure` incl. city rules, no structure, no mountain, no spice for city-only
  items; slabs use the sidebar slab rules), build range satisfied by the house's own tiles **or any
  queued reservation of the same house in any yard (chain planning)**, and no overlap with any
  reservation of the house.
- Completion runs in `ConstructionYard::update()` in the tick: valid on the real map → placed via
  `House::placeStructure` (the `CMD_PLACE_STRUCTURE` path: same cost, one-palace rule, city hooks,
  "Construction complete").
- Units never cancel a plan. Only units in the way → the finished building **waits as long as it
  takes**: own units are ordered off (forced move, cycle-seeded RNG) once a second; another house's
  units are waited out, with a one-time ticker "`<Building> is waiting: enemy units are on the
  planned site`". The ghost turns amber with "waiting", the sidebar button shows **BLOCKED**, and
  the player may place the building elsewhere by hand (click the item or press P, then click; the
  `CMD_PLACE_STRUCTURE` executor allows a blocked reserved order). AI players never place a
  blocked order (`isWaitingToPlace()` stays false). As soon as the plot clears it is placed on plan.
- A structure on the plot or lost build range → falls back: ready to place, red ghost, ticker
  "`<Building> could not be placed at the planned site`".
- Planned plots are kept clear before completion too: once a second (staggered per yard) own units
  that have stopped on any planned plot of the yard are moved off it. Units walking through, or in
  Hold, are left alone.
- Yard destroyed or captured (capture recreates the structure): its queue and reservations vanish;
  money is handled exactly as for any dying builder today (progress lost, unstarted orders cost
  nothing). An owner change via `setOwner` also clears reservations.
- AI never creates reservations.

## Deviations from the spec

- The setting is a match rule in the Game Rules window, not a per-player option.
- Right-click on a ghost cancels only when no own unit that takes orders is selected; otherwise the
  normal right-click order wins, so a move order on a planned plot never cancels a building.
- Yard destruction refund is covered by the real-engine probe, not a unit test: there is no
  reservation-specific refund logic to unit test (the rule is "same as today").
- Power Line excluded alongside Road (also instant).

## Verification (Mac mini, Release, 2026-10-13)

- `ctest --test-dir build -R "^dunelegacy_tests$|preplace|network_wire_harness|lobby_authorization"`
  → 5/5 passed (unit tests incl. `[preplace]`: chained range, overlap, completion/fallback, cancel
  refund, mixed ordering, ids; updated protocol/enum/save-version tests).
- `preplace_queue_probe` (`run-campaign-balance.py --preplace-probe --mod dunecity`) passed: sidebar
  ON = planning / OFF = Shift ×5; planning stays on the building after a plot, pending overlap refused, Esc leaves; 5
  reservations (2 chained) + 6 refused (overlap, range, off-map, item, hostile coords, enemy player);
  save→load round trip identical; WindTrap, Barracks (chained), Silo (after moving a parked Quad)
  and a Residential zone placed exactly on plan with no placement command; 5th plot blocked by a
  wall fell back with the ticker message; mixed queue order; cancel refund 0 / exact progress; list
  cancel drops the newest reservation; ghost right-click; destroyed yard leaves money unchanged; a
  9854 save (`tests/ai/preplace-9854.sav.gz`, written by origin/main 40f4e7a8) loads and keeps the
  manual flow. Screenshots `preplace-ghosts.png` (numbered ghosts, "14%") and
  `preplace-fallback.png` (red ghost); also rendered with the real Metal renderer.
- `game_command_regressions` passed incl. new `PREPLACE_COMMAND_PROBE_PASS` (wire round trip, enemy
  refused, owner and co-op allowed, multiplayer batch path).
- `--sourceforge-probe` (palace bug 113) and `--city-placement-probe --mod dunecity` passed.
