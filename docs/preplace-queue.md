# Construction Yard pre-placement queue (as built)

Choose where a building goes when you order it. The Construction Yard builds its queue in order and
places each finished building on its planned plot automatically.

## Player flow

- Setting: **Settings → General → "Choose building location when ordering"**, default ON, stored
  in the ini as `[General] Choose Building Location`. OFF = the old flow (build, then click to place),
  including Shift+click = ×5 in the list.
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
  (upgrade level, one-palace rule) are skipped. With the setting OFF a list click still orders on
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
- **NETWORK_PROTOCOL_VERSION 71.**
- Plot validity (`ConstructionYard::checkReservationPlot`, rules in
  `include/structures/PlacementReservationPolicy.h`): normal placement rules with units ignored
  (`Map::okayToPlaceStructure` incl. city rules, no structure, no mountain, no spice for city-only
  items; slabs use the sidebar slab rules), build range satisfied by the house's own tiles **or any
  queued reservation of the same house in any yard (chain planning)**, and no overlap with any
  reservation of the house.
- Completion runs in `ConstructionYard::update()` in the tick: valid on the real map → placed via
  `House::placeStructure` (the `CMD_PLACE_STRUCTURE` path: same cost, one-palace rule, city hooks,
  "Construction complete"). Only units in the way → own units are ordered off (forced move,
  cycle-seeded RNG) once a second for up to 5 s game time. Still blocked, or a structure/lost
  adjacency → falls back: ready to place, red ghost, ticker "`<Building> could not be placed at the
  planned site`". While a reservation is pending the order is not "ready to place", so neither the
  player nor a partner AI can place it elsewhere.
- Yard destroyed or captured (capture recreates the structure): its queue and reservations vanish;
  money is handled exactly as for any dying builder today (progress lost, unstarted orders cost
  nothing). An owner change via `setOwner` also clears reservations.
- AI never creates reservations.

## Deviations from the spec

- "Settings → Game" does not exist; the checkbox is on the **General** tab.
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
