# Economy-first variants and AirliftColonists (2026-11-28)

Owner: omp EconomyFirst. All three switches remain off by default; no integration-branch commit or deployment is authorized here.

## Requirements and acceptance fixed before implementation

Stefan: "yes the ai needs to escape the poverty trap on low / no spice maps"; "the mcvs can expand whenever, no need to only deploy at home"; "but sometimes if there is a lot of rock connected to main base there is no need to expand. expansion is for when you lack space"; on copying his unit/turret opening: "try different things".

| Request | Observable acceptance |
|---|---|
| Escape poverty trap | EconomyFirst acts only in custom low/no-spice city games. Real Air-map game orders >=2 MCVs and has >=3 yards by minute 5; >=10 more zones placed by minute 10 than control. Report his full footprint separately, never substitute activation for acceptance. |
| Expand for space | Home has fewer than 4 free usable 2x2 footprints: away mission allowed even with one yard. Plenty of room: keeps a new MCV home despite having two yards. Switch off retains original share rule. Yard target is one plus one per 10 free 2x2 footprints, capped at 9 and the lobby yard limit. |
| Compare openings | A: no pacing/no turret schedule; B: all non-MCV unit orders paced + turret schedule; C: all-unit pacing only; D: turret schedule only; E: Starport-only pacing. Pacing uses factory-value orders, 3,850 by minute 3 and 8,050 by minute 10, released under a core threat. Scheduled new Rocket Turret orders: 0 before minute 3, 29 by 10, 50 by 20, 77 by 31. Log every rejected paced order, accepted scheduled turret and real placement. |
| Real comparison | Same immutable binary, 24+ seeds per arm/map on air-1v3 and BigCityLife, AWS priority 10. Print paired placement score and 95% interval, timeout rate, opening spend at minutes 3/10, activation and effect per house-game, plus the existing twelve regressions for each variant. No default recommendation without positive interval, green regressions and >50% effect coverage. |

The zone rule now runs only as idle-yard fallback (2026-11-28, to preserve quantbot_no_spice_city's ordinary ladder). Its count is always printed but not asserted; the added-zone placement lead is asserted instead, as agreed by the previous coordinator at 15:15. The units-half-of-control line belongs to paced variants only: A/D explicitly test no pacing and E tests only Starport pacing. Keep those measurements visible, not mislabeled as a cap those variants do not implement.

## AirliftColonists

Stefan: "if carryall drops are on, the ai can use a carryall to transport the mcv, making it safe during transit and can have a rocket turret queued to be placed. if carryall drops aren't on the ai can have a rocket turret queued to be placed waiting on the mcv deploy".

| Request | Observable acceptance |
|---|---|
| Carry away-bound MCV | With Manual Carryall Drops on, AI books an existing Carryall through the existing airlift mission machinery, the actual MCV is picked up, delivered at its away site and deploys. Count air share of all away trips, not just successful trips. With AirliftColonists off: zero colonist airlifts. Works with EconomyFirst on or off. |
| Turret ready before arrival | An existing eligible yard actually finishes and holds a Rocket Turret before the MCV arrives; place it within 10 seconds of the new yard deploying. Report number/share of bases covered, distance to the new yard, nearest enemy, and direction. |
| Carryall drops off | Zero colonist airlifts; MCV drives and the finished held turret is placed after deploy by the same path. |
| Transit safety | Report MCVs lost before deploy, air/ground split, against inherited 51/221 (23%, 25 Ornithopter kills). A Carryall is not invulnerable: transported MCV is hidden from Ornithopters but anti-air can kill its carrier. |
| Cost and home defence | Log seconds an existing yard is blocked by the held turret, ready-before-arrival share and missing-power/site failures. Do not consume home defence assets with a hostile within 12 tiles of a core yard/factory/Palace. Count delayed trips; if >10%, also measure without that caution. |

Deviations to report: the 12-tile home-threat caution (not requested by Stefan); pacing uses factory-value prices because the replay's actual Starport invoice is unavailable; interpolated schedules between his measured marks. No hidden cap/fallback is evidence that the requested behaviour happened.

## Safety and progress properties

- Accepted orders, not proposals, consume the pacing budget; all producer paths revalidate in produceItemWithLogging. Optional MCVs must preserve the existing projected production runway, independently of the opening-capital exception.
- One MCV owns at most one transport booking and held turret; destruction, cancelled trips and missing builders release ownership. An accepted transport request is not counted as delivery.
- Turret placement must pass the engine's live build-range, ownership, terrain and power checks. A queued/held building is not counted as placed.
- Progress assumes a surviving Carryall, legal landing/build footprint, funding, power and an eligible existing yard. Failures remain visible instead of being reported as successful airlifts/covered colonies.

## Evidence

Pending implementation and committed-source measurements. Inherited ef-runway 1bd955289defa34dd7787838fca34adbf3595c81, job 20261128T151432-ctest-8fd332: 15/17, failing shared-spending runway and economy_first (unit spend, zone-rule count, zone lead). This is not a green baseline.

## Opening save/checkpoint state (next batch)

Integrator allocated save 9906 / protocol 155 after the 9905 / 153 garrison core and independently reserved garrison loss-path protocol 154. Accepted unit/Starport spend, zone/turret order counts and policy latches now append to QuantBot's existing stream instead of resetting on reload. Scheduled material identities are ordered, persisted builder IDs. Older streams consume no new tail and retain their previous empty-counter defaults. The MCV probe round-trips the real full QuantBot stream byte-for-byte for this state, checks exact consumption, and reads an exact 9905 prefix without inventing a tail. A focused `city_lessons_policy` CTest selects the existing [city-lessons] unit/config cases. Queue outcomes are recorded separately once observed.
