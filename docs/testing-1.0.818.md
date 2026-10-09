# Testing local candidate 1.0.818

This candidate fixes the eight-house Colonists spectator startup crash, saved-map
filtering and installment funding for needed city infrastructure. Public production
remains 1.0.816. It is installed on the MBA at `/Applications/dunecity.app`, with
the usual Desktop shortcut. All 2,408 checked profile files and configuration are
unchanged; the previous 817 app is backed up. Independent installed-app verification
passes. Detailed receipts are recorded in HANDOVER.md.

## Spectator startup

1. Open `/Applications/dunecity.app` on the MBA using the usual Desktop shortcut.
2. Choose Custom Game, Offline, mod **Dune City 1.002**, and
   **8P - 256x256 - Colonists**.
3. Enable **Spectate**. Leave **Shared house** unchecked; it is optional.
4. Set all eight houses to **QuantBot Brutal**, with separate teams 1–8.
5. Start. The map and minimap should be fully visible. Select buildings of several
   colours: the detail panel should show the selected owner's figures. Orders and
   city-budget edits are unavailable to the observer.
6. Save, exit the match, load the save, then try Restart Game. Both should remain
   all-AI matches with the same spectator access. Shared house can also be tested on.

The failure was two null human/view references during loading of preplaced units
and sandworms. Shared house was not required. The exact captured Colonists map was
tested with both settings, eight Brutal houses including Neutral, deployment,
ticks, full visibility, foreign panels, read-only commands, save/load and restart.
Both runs passed 172 checks. They use a controlled seed and stronger fog/authority
checks, rather than replaying the original MBA session byte for byte.

## Saved maps

Choose Browse Maps, **SP User Maps** (or All Maps), type **DuneCity**, size
**129–192**, and **4 players**. The 192x192 **New Beginning** and **BigCityLife**
files belong here. Use Any size and Any players to remove other filter restrictions.
The editor's Load Map list should show their real map rows, without extra
`.ini.workshop` rows. Those files contain revision metadata and are not maps.

Both original MBA maps were intact when captured. New Beginning's playable content
classified as Vanilla while its authored dependency was DuneCity; the type filter
now accepts either without changing the map category or bytes. BigCityLife already
appeared in the baseline scan, so its earlier absence has not been assigned the
same cause. Real editor/Custom Game lists are checked using isolated fixtures with
these names and dimensions, and browsing must leave the map bytes unchanged.

## AI infrastructure without the full price saved

Start a fresh **4P - 192x192 - New Beginning** match with Dune City 1.002, SimCity
enabled, normal construction, concrete required, and Brutal AI houses. Spectate
to watch their queues. Use separate teams for the captured-match configuration;
putting houses on one team makes inspection easier but changes their spending.

A needed Stadium or Airport, or a planned Nuclear Plant, can enter a yard's queue
below its full price when there is spare cash for foundations and an initial
installment, plus positive ongoing income. Availability, prerequisites, space and
the absence of an existing/queued copy still matter. An empty till pauses normal
production; income resumes it. Blackout recovery can build cheap wind power while
a reactor is pending. The observer cannot issue these orders itself.

The final 66-minute controlled match on the captured map and seed 59770805 accepted
reactors at 236, 438, 533 and 704 credits (price 2,000) and a Stadium at 686 credits
(price 3,000). Final cumulative telemetry records four completed reactors and one
completed Stadium. The 1.0.817 comparison queued none of these projects. This is a
fresh-start comparison, not a saved-match replay or a claim that every AI builds
them at the same time. The map starts spice-free; blooms later generate spice.
This run did not exercise an Airport order; the dedicated real-AI funding fixture
does, with commercial-cap demand and scarce cash.

The dedicated fixture checks all three projects through QuantBot's actual planner
with prices exceeding cash plus the four-minute forecast, concrete foundations,
duplicate prevention across yards, paid progress, zero-credit pause, resumed
income and completion under a declared test income grant. That grant verifies
payment/progress mechanics; it does not predict completion time at ordinary tax rates.

## Verification receipt

All 69 native CTest gates have passing results. The full run passed 68 in 794.84
seconds; its menu gate encountered stale generated fixture sidecars after cache
cleanup. Retrying the entire menu output in a fresh isolated directory passed in
234.79 seconds at 640x480, 854x480 and 1280x720. No shipping source changed between
these runs. Native dependency audits, version metadata and focused startup/
infrastructure checks pass. Network protocol is 59; save format remains 9852.
No test hook ships in the app.

Private evidence is under `../outputs/map-visibility-818/`,
`../outputs/stadium-ai-818/final-validated/`,
`../outputs/spectator-colonists-crash-818/independent/` and
`../outputs/install-818-mba/`. The portable bundle is checked against the tested
native executable's code/string sections, dependency paths and deep/strict signature.

| Property | Verification |
| --- | --- |
| AI names cannot create an invalid local human pair | Real human type required in both loader paths; eight-house startup and ordinary-human controls |
| Map-unit loading needs no local view | Null callback/warning guards preserve unit deployment and target state |
| Observer has no mutation authority | Foreign panels and visibility, forbidden commands, real-owner positive controls, save/reload/restart |
| Partial-price permission is bounded | Only a needed, legal, placeable civic/planned reactor receives the per-yard installment marker |
| Credits are not spent twice | Foundations booked once, full unpaid queue obligations reserved on subsequent planning passes |
| Work stops without payment | Real BuilderBase progress pauses at zero and resumes with arriving credits |
| Completion requires future income | Bounded fixture completion assumes its declared grant; no unconditional future-income guarantee |
| Reactor planning cannot suppress blackout recovery | Pending-reactor fixture also accepts wind power with its real foundation queue |
| Browsing preserves saved content | Actual lists and unchanged fixture bytes; category and authored dependency kept distinct |
