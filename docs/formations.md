# Spec: Formation movement (player and AI)

Goal: a group ordered to a point arrives together, spread out, heavy armour in front and ranged units
behind, instead of a single-file conga line that piles onto one tile.

## Planner (pure, unit-tested)
- `include/FormationPlanner.h` (+ .cpp): deterministic function from
  members {objectID, itemID, current tile, role, speed, footprint/movement class} + target tile +
  map passability query → per-member destination tile, plus the group speed.
- Shape: if the group is already coherent (all members within 6 tiles of the group centroid) keep the
  relative offsets, compressed so that spacing never exceeds 2 tiles. Otherwise build a box facing the
  direction centroid→target: rows perpendicular to travel; front rows = melee/armour
  (tanks, quads, trikes, devastators, sonic tanks), then ranged/artillery (launchers, siege tanks,
  deviators), infantry on flanks/back, support (harvester/MCV if selected) at the rear; width ≈
  ceil(sqrt(n·1.5)). One tile per vehicle; infantry may share tiles up to the engine's per-tile
  infantry capacity.
- Slots must be passable for that member's movement class, inside the map, not occupied by structures
  and unique; otherwise take the nearest valid free tile by deterministic spiral search. Sorting ties by
  objectID so every peer computes identical results.
- Aircraft are excluded (they keep current behaviour). Groups of 1 never use formations.

## Speed matching
- Members of an active formation move at the speed of the slowest member, EXCEPT a member more than 5
  tiles behind its slot-relative progress (or more than 10 tiles from the group centroid) moves at full
  speed until it catches up. Formation membership (group id + speed cap) is UnitBase simulation state,
  saved (gate 9857), set via command, cleared on arrival, on any new non-formation order, on death of
  all other members, or when the member is carried by a carryall.
- Units still path individually (existing pathfinder); the planner only chooses destinations and the
  speed cap.

## Player
- Right-click move (and M move cursor) with ≥2 ground units selected uses formations. Toggle hotkey
  `Ctrl+F` and an options checkbox "Move groups in formation" (default ON, persisted). `F` keeps its
  current meaning (next factory).
- Issue path: the issuing client computes the plan and sends one command per group
  (e.g. `CMD_UNIT_FORMATION_MOVE(GROUP_ID, SPEED_CAP, then id,x,y triples)`) or per-unit commands carrying
  destination + group id; executor validates ownership and passability again (destinations that became
  invalid fall back to a nearest-valid search inside the executor so all peers agree).
- Expose the planning entry point as one function so integration can also route attack-move, patrol and
  shift-queued waypoints (being built in a parallel slice) through it.

## AI
- QuantBot: army gathering, attack waves and defence-response moves of ≥3 ground units use the planner
  (front armour, ranged behind) and the speed cap so waves arrive together. Do NOT change who is sent or
  when (target selection, scramble/hunt-toward rules from 1.0.832 stay); only how a chosen group moves.
  Campaign AI (`CampaignAIPlayer`) and `AIPlayer` group attacks use the same planner if they issue group
  moves; SmartBot only if trivial.
- Measure: on a fixed seed AI-vs-AI skirmish, compare before/after: spread of arrival times of a wave
  (first to last member reaching the target area) and the number of shots fired in the first 30 s of
  contact. Record numbers in the doc. Arrival spread must go down; shots must not go down materially.

## Non-goals
Formation shape picker UI, air formations, naval, changing pathfinding internals.

## Acceptance
- Unit tests: determinism (same input → same slots, independent of member order), coherent-group offset
  preservation, box layout role ordering, impassable/occupied slot fallback, uniqueness, map-edge
  clipping, speed-cap catch-up rule.
- Save round-trip for formation state; 9854 saves load.
- Real-engine probe: 12 mixed units moved across open sand arrive within a bounded arrival spread with
  tanks ahead of launchers; same order with formations off reproduces the old behaviour.
- QuantBot probes that cover army movement/defence (incl. the 1.0.832 nearest-hunt probe) still pass;
  AI wave measurement recorded.
- Multiplayer command-execution probe covers the new command.
- Real-game smoke screenshot of a group in formation.
