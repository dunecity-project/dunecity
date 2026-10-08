# Testing local candidate 1.0.819

This candidate fixes required city infrastructure losing the construction yard to
optional spending. It is installed on the MBA at `/Applications/dunecity.app`.
Open the usual Desktop **DuneCity.app** shortcut. The installed version and
bundled runtime were independently verified; saves, maps and settings are unchanged.

## Watch the AI build a stadium

1. Choose Custom Game, Offline, **Dune City 1.002**, and
   **4P - 192x192 - New Beginning** from SP User Maps.
2. Enable **Spectate**, leave Shared house unchecked, and choose **QuantBot Brutal**
   for all four occupied slots. Enable SimCity, normal construction and concrete.
3. Start the game. Select an AI Construction Yard to inspect its production.
4. Once its residential population passes the stadium requirement, it should
   pursue the stadium before routine coverage, extra power capacity and optional
   economy/technology purchases. If Radar is missing, it should build that first.
5. Watch its queue and credits: it can start the 3,000-credit stadium below
   3,000 credits, then pay gradually as income arrives. Production pauses at zero
   credits and resumes with income. Airport and planned nuclear power retain the
   same gradual-payment behaviour.

A stadium that already exists elsewhere in that house satisfies the requirement.
The requirement uses residential population, rather than the combined population
display. Blackouts and present defence emergencies still take priority. Space,
prerequisites, legal technology, a foundation/start budget and positive forecast
income must be available. These are not promises of a fixed completion time in a
match with combat or interrupted income.

## What changed

The AI now derives its need from the city simulation's population thresholds and
existing/queued buildings. The previous positive-demand clipping flag could go
quiet while residential demand was negative, even though the city still needed a
stadium. Unavailable prerequisites no longer erase that need: the planner walks
the mod's prerequisite chain and can restore Radar. Queue-inclusive counts prevent
parallel yards from ordering duplicates.

The civic rule runs before optional protection saving, extra generation, spice
capacity and technology expansion. Actual storage overflow, blackout recovery,
observed aircraft and an identified dangerous-crime emergency remain ahead of it.
Only a validated civic/prerequisite order receives the installment acceptance
budget; full unpaid queue costs remain reserved and foundations are charged once.
No serialized fields changed: save format remains 9852. Deterministic orders
changed, so the network protocol is 60.

## Controlled whole-match comparison

Both runs use the captured 192x192 map bytes, seed **223970290**, four Brutal AIs,
detached spectator, concrete and ordinary paid production. No income grants or
instant construction were used. Slot factions are Sardaukar, Mercenary, Harkonnen,
Neutral; controlled teams are 1, 2, 3, 5 from the map's Brain entries. The live
faction order was verified, but the live lobby teams were not captured, so these
are fresh controlled matches rather than an exact replay of the user's session.

The map starts without harvestable spice but includes 12 blooms; spice can appear
later. Both runs reached 247,500 cycles (66 game minutes).

| House | 1.0.818 stadiums built | 1.0.819 first order at credits | 1.0.819 first completion |
| --- | ---: | ---: | ---: |
| Harkonnen | 0 | 341 / price 3,000 | 32.97 min |
| Sardaukar | 0 | 291 / price 3,000 | 42.25 min |
| Mercenary | 0 | 391 / price 3,000 | 42.99 min |
| Neutral | 0 | 239 / price 3,000 | 35.95 min |

Harkonnen also completed an airport at 53.77 minutes after ordering it at 734
credits versus its 5,000-credit price. Neutral rebuilt a destroyed stadium by
44.24 minutes. Sardaukar's early stadium order did not complete; a later order
completed before the house was defeated. The comparison proves observed civic
progress in this scenario, not improved match balance or uninterrupted production.
The final candidate capture imported 84,365 events with zero invalid or incomplete
records. Cumulative `game_summary`, accepted `production_order` and `object_built`
records supply these counts and timings.

## Verification status

All **70 registered native CTest gates** pass: 32 core gates in 535.85 seconds
and 38 AI/economy gates in 417.32 seconds. The sets are disjoint and together match
every registered gate. Native build, version consistency, pre/post Ninja dependency
audits and the controlled whole match pass against the same shipping source and
executable fingerprints.

The real-threshold fixture checks paid production and placement of the missing
Radar followed by an AI stadium order, duplicate prevention, foundation placement,
paid progress, zero-credit pause/resume, negative demand, observed aircraft and
blackout recovery. The fixture's completion bound assumes a declared 2-credit
income grant per cycle; ordinary tax/combat timing is measured in the whole match.

The priority-only fixture sets a positive clipping mask while keeping the real
population threshold exceeded and prerequisites present. With the preserved
1.0.818 QuantBot object it fails on priority: after two yard passes a reactor is
queued and the stadium is absent. The 1.0.819 policy queues the stadium after one
pass and does not queue that competing reactor. This diagnostic changes only
the linked AI object; it is not a full 1.0.818 binary or saved-session replay.

The synthetic routine-protection subcase explicitly skips: its dense district has
crime 250, above the 192 emergency threshold, so it cannot represent routine
coverage. It is not counted as protection evidence. Instead, the ordinary whole
match supplies direct shared-rule coverage: at cycle 196625, Harkonnen yard 913's
winning capital candidate is an unurgent Rocket Turret for `uncovered_base`, but
the required Airport enters that same yard instead. Dangerous-crime urgency is
source-reviewed; it was not forced in a dedicated fixture.

The portable app has 36 ARM64 Mach-O files, portable library paths, executable
code/string equivalence with the native build, a deep/strict ad-hoc signature and
bundled SDL initialization plus hidden-window rendering. Packaged executable SHA256:
`fbb900ce1e9dcf852aee2f820bf432e9dca201be12575d2ec1f3c167a6dd7282`.
The guarded installer completed with no game running and retains 1.0.818 at
`/Applications/.dunecity-backup-before-819-20261008/dunecity.app`. Independent
readback verifies the installed version/hash, signature and runtime, the Desktop
shortcut and all 2,482 `.ini`/`.dls` profile files unchanged. No match was stopped
or started. Receipts are in
`/Users/stefan/Documents/projects/outputs/install-819-mba/install-819-receipt.json`
and `independent-verification.json` in the same directory.

The earlier forced-flag funding fixture did not establish normal game scheduling
progress; its passing result should not be used for that claim.
