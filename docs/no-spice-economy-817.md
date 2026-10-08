# No-spice city economy regression — local 1.0.817

QuantBot now protects the income opening on Dune City maps whose remaining spice
cannot support a sustainable worker. Essential power comes first, followed by
credit storage and a seed of four demanded city lots. Optional Starport technology,
repair capacity, peaceful coverage and bargain imports wait for that opening.
The ordinary demand chooser selects R/I/C; it does not force commercial or
industrial lots when their demand is negative.

One Refinery remains necessary in the shipped mod: it provides 1,005 credits of
storage at tech 1 and unlocks the 10,000-credit Silo. Without storage, city tax is
discarded. The new `city_income_storage` rule uses the cheapest legal, placeable
capacity source from the actual mod tables. It does not buy a refinery for spice
income when no spice exists. A transport requires a real harvesting fleet or
vehicle repair traffic.

An established city with an existing Heavy Factory, real storage and tax exceeding
upkeep retains ordinary parallel growth, services and military spending. A funded
new factory can also proceed when cash and the four-minute forecast cover the
entire remaining seed and production commitments. A small first tax receipt alone
does not release an otherwise scarce opening grant.

## Captured defect

The completed MBA session was `1791440520505988-0`, Dune City 1.0.816,
**4P - 192x192 - NewBeginning**, seed **148680876**, four Brutal QuantBots,
2,000 starting credits, concrete required, city simulation enabled, no spice.
The `no money.dls` snapshot embeds the same map. Its SHA256 is
`03dc2e2defdf33af7f4c61f0f38e7377044da4c9688eefd1f2e3d5c63cd30188`.

The final cumulative SQLite/telemetry records show all four houses at zero credits,
zero R/I/C lots and zero city income after 42,189 cycles (11.25 simulated minutes).
The opening consumed its grant on a yard upgrade, power, a Refinery, a Starport,
foundations and imported transport or units. The unaffordable Repair Yard then
kept the construction ladder ahead of zoning. A native pre-fix 1.0.817 run on the
captured map also reproduced the failed opening in three houses; the fourth built
one industrial lot. This is a fresh-start comparison, not a byte-identical replay
of the completed 1.0.816 save.

## Final-source simulations

All simulation profiles were isolated; the user's new MBA match was left running.
Original map grants and geometry were preserved. Extra zero-spice variants remove
spice and blooms only from scratch copies; the first real engine snapshot confirms
zero remaining spice. No fixture money was injected into these games.

| Scenario | Runs | Duration per run | Result |
| --- | ---: | ---: | --- |
| Captured NewBeginning, original seed, Easy/Medium/Hard/Brutal | 4 | 12 minutes | All 16 AI openings bank tax; first lot ordered at 62.8–64.9 seconds |
| Captured NewBeginning, three additional seeds | 3 | 12 minutes | All 12 AI openings bank tax and grow demanded lots |
| Zero-spice DuneCity 192x192, three difficulties/seeds | 3 | 12 minutes | All 12 AI openings bank tax and grow R/I/C |
| Zero-spice Sardaukar 128x128, Hard/Brutal | 2 | 12 minutes | All 10 active AI bases bank tax; the prebuilt industrial workforce permits demand-led R/C growth |
| Zero-spice 3 vs 1 64x64, lower 1,500-credit grant for Harkonnen | 1 | 12 minutes | All four openings bank tax; one defeated house builds only three lots before losing its base |
| Normal-spice Dune City, Vanilla and Dune2R controls | 5 | 12 minutes | All captures complete; city/spice economy and non-city modes remain operational |
| Captured NewBeginning, original seed, Brutal | 1 | 60 minutes | All four houses remain alive, grow cities and advance into military production |

Across the 13 zero-spice short scenarios, all **54 active AI openings** bank more
than 100 credits of city income. Every surviving city builds at least four lots;
fresh starts build residential and industrial lots. The observed cumulative banked
city income ranges from 152 to 16,864 credits. Defeat and income progress are
reported separately; an AI can lose a match despite establishing income.

The exact-seed Brutal long run reaches its 225,000-cycle limit:

| House | Residential / Industrial / Commercial built | Banked city income | Final cash |
| --- | --- | ---: | ---: |
| Harkonnen | 19 / 22 / 19 | 26,293 | 21 |
| Ordos | 20 / 15 / 8 | 14,192 | 0 |
| Sardaukar | 23 / 28 / 21 | 32,530 | 0 |
| Rebels | 19 / 31 / 29 | 40,695 | 543 |

Zero current cash can now mean active spending backed by a tax-producing city;
it no longer means the reported empty-city bankruptcy. These are bounded progress
checks, not claims that the AI wins every no-spice map or repairs a save whose
grant is already exhausted.

## Regression gates and invariants

| Property | Enforcement | Verification |
| --- | --- | --- |
| Scarce opening money reaches income before optional technology | Dynamic bootstrap reserve and early yard rules in `QuantBot::build` | Real queues; first-tick preplaced market; ordinary-game banked-income gates at 1,500/2,000 credits |
| Tax is bankable | Actual plus queued capacity; same-pass accepted capacity updates | Cumulative `city_net_applied`, rather than gross tax or a forecast |
| Pending orders share the seed | Queue-inclusive zone counts, incremented only on accepted production | Parallel-yard real queues; income and existing shared-spending probes |
| Growth respects actual resources | Existing command budgets, legal placement, power and demand checks | Broke, low-power, tiny-field, negative-demand and no-site cases |
| Depletion changes policy without a saved latch | Sustainable live-map worker target, runtime-derived commitments | Spice/depleted controls and AI-state save round trip |
| Established cities keep services and production | Bankable net income plus an existing factory releases opening priority | Existing shared-spending, city-growth, Starport and opening-economy gates |

The native Release build and pre/post Ninja dependency audits pass. Three new
CTest gates cover the queue policy and two ordinary-game income scenarios. The
complete CTest suite passes **65/65 gates** in 894.19 seconds. Its receipt and
remaining release limits are recorded in `HANDOVER.md`.
All diagnostics link shipping engine objects; fixture access is confined to the
test executable. No serialized fields were added; save version remains 9852.
Network protocol rises from 57 to 58 because deterministic AI orders change.

Evidence, captured save/map, original SQLite import, worker reports and final
simulation receipts are retained at
`/Users/stefan/Documents/projects/outputs/no-spice-economy-817/`.
`final-results.json`, `final-run-receipts.json`, `final-assessment.log` and
`final-60-minute/summary.json` describe the final source. Earlier prototype runs
are diagnostic history and are excluded from these results.

This is a local 1.0.817 candidate. Production and the MBA installation are still
1.0.816; platform CI, portable packaging, signing and public publication have not
been performed for this change.
