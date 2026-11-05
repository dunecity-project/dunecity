# QuantBot army recovery

Custom-game Hard and Brutal bots use a house-level army posture, rather than
asking every unit to decide independently whether the whole army is losing.
Campaign roles, support bots, and Easy/Medium retain their existing pacing.

## Installment-first technology and existing composition

The separately hashed `[Force Concentration] SimpleTechSavings` alternative
is default OFF pending paired acceptance. It applies only to Custom
Medium/Hard/Brutal, not campaigns, Easy or builder-only authority. When both
experimental switches are set, this ledger-free policy takes precedence.

It starts the single next legal technology structure immediately, including
zero cash, using the ordinary founded construction queue and engine
installments. There is no full-quote, four-minute funding or cash-reserve
precondition. Needed yard/factory upgrades retain exactly the human full-credit
start rule; the AI starts at the actual quote without an extra operating reserve.
Engine upgrade admission, commands and wire layouts are unchanged.
The selected installment project also bypasses the yard's legacy 100-credit
entry threshold; other yard choices keep their existing admission rules.

Military selection uses owned-plus-accepted-queue replacement-value proportions.
While technology needs funding, ordinary purchases stop at the threat-sized
ground-defence minimum, including existing commitments; real defence, opening
workers, power and first-Starport priorities remain. Existing queues are not
cancelled or put on hold, and human/saboteur orders are unchanged. Ordinary
payment order still applies: a finite older backlog can delay a newer yard.
This alternative never initializes, allocates or debits the saved ledger and
adds no saved state or protocol change.

The ledger experiment below is retained OFF for comparison. At `4477575c`,
288 canonical games rejected it: city research-OFF HF/IX/Starport completion
was 101/55/168 OFF versus 59/2/47 ON per192 houses; all six Hard/Brutal
mod/research cells had four adverse dispatch and kill-value seed blocks.
Those results do not measure the installment-first alternative.

## Technology savings and guns/butter budgets

The experimental hashed `[Force Concentration] TechSavings` switch applies to
Custom Medium, Hard and Brutal games, in city mode and classic content. It is
off until paired outcome audits establish the default; campaigns, Easy,
Builder-only authority, human order leases and saboteur orders are unchanged.

The existing capital planner allocates real net receipts between production,
economic growth and military spending. Safe income shares are 30/35/35%;
an observed local ground threat, active economic defence or aircraft over the
base changes new receipts to 15/20/65%. Unspent allocations remain in their own
bucket. Full accepted orders, upgrades and research are committed once; later
installment payments do not charge the same allocation again. Repairs and
upkeep reduce net receipts rather than creating fictional gross income.
Emergency defence, opening workers, bootstrap income, power recovery and the
first legal Starport may borrow, recording debt in their own bucket for future
income to repay. Forecast receipts are never spendable credits.
With TechSavings ON, the existing bounded opening-worker floor also applies
to classic Medium/Hard games. An existing Starport with an enabled, listed
Harvester offer is a worker supplier, not just a Heavy Factory; listed but
sold-out imports retain their opening priority until real stock returns.
Admission and the single ledger debit use the supplier's actual quoted price.
The exact Economy allocation replaces the old blanket 2,000-credit classic
cash reserve; retaining both would strand an affordable defensive order even
while its Military bucket held enough actual money.

Production includes factories, tech buildings, upgrades and research. Economy
includes workers/transports, refineries/storage, power, zones/services and
expansion MCVs; combat units and defensive emplacements are military. Concrete
inherits the building's bucket. The same shared capital comparison reserves
the next legal first supplier, production upgrade, IX, Palace or research
project, including actual prerequisites and full foundations. It holds only
production's allocated cash, leaving economy and military allocations usable.
Below-quote technology saving requires six owned ground fighters, at least
the larger of 3,000 credits, the existing wave minimum and 1.5 times observed
local ground threat, stable income/power, and a four-minute funded quote.
Losing that dispatch-capable defence releases the hold for replenishment.
The first SP/HF chain, first legal LF and necessary research can precede the
floor only with a fully funded Production quote and real working cash. The
same bounded exception covers a missing capability when no legal unit can
advance the existing mix: requiring those units before their unlock would
deadlock growth. An optional Palace retains the normal readiness floor.

First HF/Repair/High-Tech/IX/Palace/Lab orders and optional HF/High-Tech upgrades
must match the active, fully funded project, including its builder and
operation. Legacy overdue/wealth thresholds cannot buy a different technology
ahead of it. Custom Medium ON can build one first Repair Yard through the same
planning/admission predicate; OFF, campaign and repeat replacement caps remain.
Bucket allocations persist even without an active plan; the army floor
controls the capital hold rather than creating or deleting those allocations.
Prerequisite walks stop at the stored mask's `.size()`, matching the engine
catalogue. The mask represents 27 legacy structure IDs, not every extended
structure accepted by `isStructure`; its save/protocol representation is not
widened. A catalogue-legal first HF is asserted to yield an actual HF quote.

The existing storage forecast can release a physical receipt-capacity
constraint even while Economy owes bootstrap debt. The paid capacity still
charges Economy; no debt is erased or forecast income invented. Existing
generator sizing avoids borrowing for an oversized reactor when a windtrap
suffices. A bucket-rejected yard choice yields to funded work in another
bucket instead of monopolizing the construction turn.
City reactor sizing and final admission both respect pending generation.
An emergency windtrap beside an unfinished reactor must not be converted back
into a second reactor order by a later sizing pass or parallel yard.

Unit selection also changes: target ratios are compared with the replacement
value of units actually owned plus accepted queue commitments, not with the
military limit or unspent money. The next indivisible vehicle minimizes the
whole current mix's basis-point error; every factory and Starport shares that
decision and updates it after acceptance. There is no routine cheap-unit
capacity-fill fallback. A cap-rejected or unfunded optional MCV, harvester,
carryall or upgrade cannot consume a factory turn ahead of an otherwise
legal, funded composition order.
An unavailable desired type does not authorize another already-overweight
cheap type. Empty or exactly balanced armies can take one indivisible step;
subsequent orders must again respect the existing-share deficit.

Technology quotes include busy producers, so completing an ordinary queue
does not leave the next choice blind. A fully funded producer upgrade lets
accepted units drain rather than appending replacements forever. A missing
first HF may precede an unfunded import prerequisite when opening workers or
a usable composition lane need it; first Starport retains the next priority.
Completing the port does not disable the bounded first-HF exception.
Heavy factories under TechSavings use the accepted item's real funding checks,
not the old generic 2,000-credit threshold ahead of an otherwise affordable
300-credit unit. Bucket allocations and the shared working reserve still apply.

Emergency composition bypass requires an actual local ground-defence
shortfall: nearby health-weighted ground troops and covering turrets are
compared with the existing 1.5-times threat requirement. A distant army
cannot satisfy local defence. Air-only pressure still changes income shares
and permits explicit anti-air responses, but does not authorize arbitrary
cheap ground replenishment. Ordinary standing troopers obey the existing-army
infantry quota; an observed hostile wing retains reactive anti-air exceptions.
Adequate local cover and air-only contact do not globally freeze funded
progression: the threatened income shares remain, but an actual unmet ground
deficit is the safety veto. A genuine reactive-AA troop shortage keeps its
capital precedence; an ordinary unobserved standing floor still yields to tech.

Save 9884 stores the exact integer bucket balances and committed-wealth
baseline. Saves 9882/9883 retain their frozen settings and load an empty,
uninitialized ledger. Protocol 113 separates older peers because transferred
multiplayer saves and observer checkpoints carry this new QuantBot state.

The audit harness opts into counterfactual OFF-arm feasibility searches with
`DUNECITY_TECH_SAVINGS_AUDIT=1`; ordinary decision logging does not run those
extra placement searches. Both logged arms record live existing-plus-queued
composition. An unused all-zero ledger is neutral in the lockstep digest, so
the default OFF arm retains its established simulation fingerprints.
New `tech-orders.csv` files keep scenario `city` and diagnostic `city_spend`
as unique columns. Historical captures with the old duplicate `city` header
are admitted table-aware: scenario metadata comes from the native game/setup,
not the shadowing spending diagnostic. Accepted city totals are unchanged.

### Measured rejected trial and compatibility

The initial `8a8239a5` factorial trial contains 576 successful logical games
(48 arms, two seeds), including independent TrooperSupport and
FundedClassicProduction OFF controls. It was rejected, not promoted: city
dispatches and kills fell to zero, and both classics had adverse dispatch/kill
signals. Its OFF acceptance-time composition mirror was defective; permanent
composition samples, spending, timings and outcomes remain usable.

Corrected `69d5f5c4` raw diagnostics contain 18 complete streams, one seed
across all three difficulties/content families, 4,451 certified cheap orders
and 108 exact house-level spending reconciliations. City ON completed no
HF/IX/Starport in its 18 house histories. The diagnosed cycle was a 3,000-value
army floor blocking the first 420-credit LF while a 2,250-value army could not
grow without it; Economy debt plus a physical cash cap also blocked receipts.
The bounded bootstrap/storage and local-emergency corrections are in
`8a2e9bf1`. These rejected-trial observations are not confirmation results for
that later policy.

The expanded `69d5f5c4` four-seed trial confirms rejection: 288 original native
games, 960 houses, 224,259 accepted quotes and 41,450 cheap orders reconcile to
whole raw streams and native sidecars. Hard/Brutal research-OFF matched
equal-game dispatch means were:

| Content | OFF | ON | ON minus OFF, exploratory 95% game interval |
| --- | ---: | ---: | ---: |
| DuneCity | 1.125 | 0 | -1.125 [-1.651, -0.641] |
| Dune2R | 4.281 | 1.885 | -2.396 [-3.115, -1.760] |
| Vanilla | 3.771 | 1.698 | -2.073 [-3.063, -1.208] |

All four independent seed blocks had adverse dispatch/kill signals; games and
houses within a seed are not independent replications. City research-OFF
completed HF/IX/SP counts were 101/55/168 out of 192 OFF histories versus
0/6/6 ON. These are observed achievements, not zero-imputed completion times.
The report separately records death and administrative censoring, conditional
unit-price coverage, paid money and gross income. It excludes 144 callback
replay attempts; three failed finalizers remain explicit infrastructure
failures, not a globally green receipt.

`4bcefd69` adds the planned-technology admission, bounded Medium Repair Yard
and adequately-defended-pressure corrections described above. Earlier rejected
trials do not establish adoption for this later source; the default stays OFF.

Source `85ca5918` passed the approved local full-Game 9882/9883 load, 600-cycle
advance, signed/subcredit ledger save9884 roundtrip, and 60 further cycles.
Private fixtures and re-saves are not committed. Receipts and reproducible
analysis are under `../outputs/tech-saving-20261105/`, including
`final-analysis-8a8239a5/`, `final-raw-analysis-69d5f5c4-certified/`,
`expanded-analysis-69d5f5c4/compact-summary.txt` and
`private-save-roundtrip-85ca5918/receipt.json`.


## Required Research Lab progression

With the match's Research Lab option enabled, the separately hashed
`[Force Concentration] ResearchLabProgression` switch treats the lab and its
research as prerequisites, not optional production expansion. It defaults ON;
OFF preserves the prior ordinary-match investment policy. Generated
Civilization Wars retains its required progression in either switch state.
`ResearchLab=false` still disables the AI's lab/research policy altogether.

The AI must have a powered, bootstrapped, positive-income economy and enough
uncommitted cash for the lab/foundation or the complete research price plus
working cash. Current defence emergencies and accepted production commitments
remain protected. It does not wait for the larger opening harvester fleet,
perfect crime coverage or repeated optional factory/city allocations: later
technology may be needed to supply those very workers and facilities.
Required research reserves its purchase before repeatable orders spend it,
and an idle yard tries the first lab after power/storage emergencies.

Build-list prerequisites, placement, foundations, generation and research
payment/timing still use the ordinary engine paths shared with humans.
Queue-inclusive counts prevent duplicate labs across yards. There is no
new saved AI state, save migration, protocol change or random draw.
Easy, Medium, Hard, Brutal, Defend and Builder authority use the same rule;
Fighter-only and switched-off QuantBots remain excluded.

`quantbot_research_lab_<vanilla|dunecity>_<difficulty>` runs a real tech-1
full-build fixture with paired hashed switches, paid placement, normal
level-2 research completion, Builder/Defend and negative legality/funding
controls. `tests/ai/research-lab-continuation.py` audits and resumes an exact
save through commit-pinned cluster units. Its frozen diagnostic input stores
the save's SHA256 and gzip/base64 bytes; measured OFF/ON units reload it
independently, without a diagnostic planning pass changing their starting
state.

## Dispatch and the military limit

The default outnumbered threshold is an observed enemy advantage of at least
1.5 times the effective friendly ground force. That blocks a fresh main ground
attack while healthy, deployable military value is below 80% of the configured
`militaryValueLimit`. Reaching 80% removes this particular veto; readiness,
recovery, and emergency base defence still apply.

For an 80,000-credit configured limit, the bypass starts at 64,000 credits.
This denominator stays fixed even when Brutal's unit override permits building
past the normal limit. A zero unit override still means unlimited production.
This feature adds no production cap. A nonpositive military-value limit has no
meaningful percentage reference and does not grant an automatic bypass.

Limit accounting and tactical strength serve different purposes. The bypass
uses actual purchase prices weighted by remaining health, including deployable
combat aircraft. Ground-wave readiness and strength are assessed separately.
Harvesters, transports, inactive cargo, units inside repair yards, and units
reserved for repair do not count as deployable attackers. Human orders take
priority. Enemy assessments use observed hostile teams and exclude neutral
sandworms. Allies contribute only where they can support the assessed fight.

## Retreat and rebuilding

The saved postures are Offensive, Withdrawing, and Recovering. The normal
strategic trigger requires a complete 90-second combat-loss window, at least
1,500 credits lost, losses of at least 20% of the starting deployable force,
confirmed enemy losses below 65% of own losses, and corroborating deterioration
in readiness or front strength. These defaults are tuning parameters, not a
claim that purchase price predicts every unit matchup.

A badly outmatched local wave can withdraw before the whole house meets the
attrition trigger. Serious danger to the main army or production core can
trigger an immediate withdrawal. Ordinary raids and one isolated survivor must
not cause house-wide panic.

While recovering, the bot holds fresh troops at a protected rally and recalls
the tracked wave in bounded batches. Emergency defenders and repair remain
available. Recall uses forced movement in Area Guard: the engine suppresses
new target acquisition during movement and restores local defence on arrival.
The rally belongs to a real defended base cluster. Units use dispersed slots
and an assembly area that grows with the army. Existing movement orders and
path-queue backpressure prevent repeated path submissions. Each call issues at
most 12 recall orders; above 150 queued requests it permits replacements of
pending work and at most two fresh requests, preserving progress under contention.

Recovery releases a rebuilt army after the 25-second dwell when healthy ground
value meets the existing attack threshold (with the 3,000-credit main-wave
floor) and the outnumbered dispatch gate allows it. A true core emergency or
severe local defeat still holds it back. Assembly percentage and a period
without losses no longer block release: scattered reserves and ongoing raids
must not keep a rebuilt army at home indefinitely. Release starts a new
attrition observation window, preserving lifetime loss/kill totals. New losses
can still trigger a later withdrawal.

Custom Hard/Brutal launches every available ground combat unit on Hunt in one
pass, wherever it stands. Existing hunters retain their orders. Human control,
repair/transit and ongoing asset defence retain priority. Fresh recruits gather
for the next viable wave instead of being sent individually. Ordinary scans do
not move hunters back into a formation or replace their target selection.

Hunting standard and elite launchers retain close-range spacing: a visible
ground enemy that can attack them, inside the shorter of its reach plus one and
half the launcher range plus one, triggers a short escape. The launcher keeps
Hunt and its wave membership and can resume firing afterward. Damage from a
distant enemy does not trigger retreat. The scan uses nearby tile occupants;
workers, aircraft, human orders, repair and ordinary posted defence contacts
retain their existing handling.

## Exhausted Custom economies

`[Force Concentration] ExhaustedArmyDispatch=true` is a default-on, hashed
QuantBot switch (`force.exhaustedArmyDispatch`). It removes rebuild-only waits
when a Custom army cannot replenish itself. Setting it to `false` retains the
legacy minimum-wave, repair-yard and repair-reservation behaviour; no seed,
match timeout or victory condition changes with this switch.

The conservative exhaustion gate requires an active, non-builder-only Custom
controller outside a campaign, with the switch enabled, and all of the following:

- No harvestable spice reachable by an owned harvester or refinery/dropoff
  region, including the existing carryall route. A zero periodic resource
  survey is the no-spice observation; a positive survey is not exhaustion merely
  because one field is inaccessible.
- Actual starting and earned credit balances, before rounding, below the
  cheapest enabled, positive-price, current-tech nonambient unit. Existing
  builder offers, including Starport quotes, can lower that conservative funding
  boundary. Cash sufficient for even that cheapest purchase keeps normal pacing.
- No positive cargo in an owned Harvester or Rebel Harvester, including cargo
  still on its way home.
- No possible city tax income: a positive tax rate with either an existing tax
  base or any owned R/C/I lot inhibits the bypass. Developing lots are protected
  even before the census reports taxable population; a currently empty wallet
  is not proof that a city cannot recover.

When these conditions hold, the ordinary Custom whole-army selector may send
its remaining available ground troops below the six-unit/3,000-credit main-wave
floor, and `attack()` does not wait for an unaffordable missing repair yard.
This is not permission to override a tactical withdrawal, emergency defence,
campaign timing or a human command. Easy's existing standing-army share remains
its own launch path.

The same gate releases only AI-owned repair work that cannot finish under the
yard's real payment rule:
`cash < 0.5 + (ceil(missing health)-1) * UNIT_REPAIRCOST`.
`House::payCredits` requires rounded credits of at least one before every debit,
so a sub-half-credit balance cannot pay even an otherwise affordable last point.
The gate makes damaged combat troops available, cancels repair-yard travel and
releases unaffordable AI yard occupants;
the engine's automatic repair decision uses the same owner check so it cannot
immediately rebook them. Human orders, saboteurs, live/base/posted defenders,
colony guards and colonist escorts keep their orders and repair authority.
Campaign repair and dispatch paths are unchanged, even in an empty economy.

## Whole-army base defence

In Custom Hard/Brutal games, a real attack on any owned building, including a
remote R/C/I colony, sends every available ground combat unit to Hunt the
invasion. Damage callbacks and ordinary scans both recognise building attacks;
the scan also handles hostile turrets firing at a forward building. Recovery,
assembly, military strength, the local response radius and proportional quotas
do not withhold the ground army during this emergency. Human commands, the
existing repair reservation policy, carryall pickups, workers, builders,
transports and aircraft retain their authority. Air rescue keeps its existing
specialised response; ordinary harvester raids keep their bounded local response.

Remote defenders travel towards the actual attacker, then let engine Hunt own
combat inside weapon range. Repeated scans retain an existing live base contact,
including when several districts are attacked, instead of restarting travel
orders. A worker raid cannot redirect troops away from that live base response.
Hunting launchers retain their short escape and resume firing afterward. When
the original attacker dies or stops attacking the building, its defence
assignment and forced travel expire on the normal scan.

These mobile Hunt defenders use an invalid guard point in the existing saved
order fields; ordinary posted defenders keep their valid guard point. This
distinction preserves local defence authority without adding serialized state.
Protocol 55 separates peers with different emergency AI behaviour; save format
9852 and observer runtime 7 remain unchanged.

## Batteries and special units

Once two refineries, a heavy factory, and a repair yard exist, important bases
can grow overlapping rocket batteries towards the observed enemy approach.
Coverage demand sizes the goal inside an 8–12 turret band; the band is also the
ceiling. The former open-ended goal (demand plus up to half again for a threat)
bought a rich base 90–100 turrets and, through the wall rule, as many walls, and
lost or stretched `quantbot_all_against_atreides` (see
`docs/quantbot-all-against-atreides.md`). First cover for critical buildings, remote
colonies, economy reserves, power rules, construction exits, and movement
corridors retain priority. Battery construction is spread across passes.
Destroyed hostile buildings count as kills in the attrition trade, at their
purchase price, so assaulting a turret-defended base is not by itself a bad trade.

A city's due, buildable first-cover rocket takes priority over founding its
first WOR for standing trooper support. Extra overlap on already covered core
assets still shares construction with growth; losing a refinery's only
covering turret restores the first-cover priority immediately. The existing
`quantbot_police_budget` and `quantbot_city_growth` probes preserve these
distinctions after core suppliers are present, without disabling
`TrooperSupport` or pre-filling its missing supplier.

In DuneCity Custom Hard/Brutal games, a battery that has no usable free site can
replace an owned residential, commercial or industrial lot on the observed
enemy side of its construction colony. Free ground wins, including ground that
opens while construction is underway. Lower displacement cost wins among lots:
less development, lower land value and weaker demand are preferred.

Clearance leaves at least three lots of the displaced type and six total lots
within two rocket-turret weapon ranges of the yard. A distant colony cannot
supply this floor. Hospitals and churches are protected. Modest lots (density
at most one, with at most eight residents) remain eligible during construction;
more developed lots must account for at most 20% of the local population or jobs
of their type. Roads, factory exits, neighbouring zone frontage, movement
corridors, turret spacing and the front quota are checked on the map after the
proposed clearance. Human/shared houses, essential infrastructure and foreign
lots cannot be cleared by this fallback.

Queue acceptance leaves the lot intact. Immediately before deletion the yard
must be active, owned, holding finished material and still have a legal,
needed battery project. With concrete off, deletion and placement of the
finished turret occur in the same build call. With concrete required, deletion
occurs when the completed one-tile foundation is placed, with the turret still
queued and all queued costs funded. Cash shortfalls retain the lot and ready
material; invalid projects cancel without deleting the lot. After foundation
placement, destruction of the yard or cancellation can still prevent the
turret from completing. This construction-time exposure is not an atomic
guarantee of a completed turret under attack.

Execution clears at most one lot per build pass across every yard, regardless
of how many earlier reservations are ready. Clearance and free-site caches are
invalidated by geometry and planning-builder changes. The local economic census
runs once per search, rather than once per candidate tile. No additional state
is serialized: save format remains 9852; protocol 51 separates peers whose AI
would preserve the lots.

Devastators, Sonic Tanks, and Deviators retain their shared outer production
budget. Selection within that group uses per-type recorded return/loss evidence
and an exploration prior, so candidate ordering alone does not favour the first
affordable type. The strength model does not reuse those scores as unit damage
weights.

## Verification and limits

`quantbot_army_recovery` exercises the real-engine integration alongside unit
policy tests and existing custom/campaign/production probes. Important checks
include the 80% boundary, minor raids, recovery/resumption, old-save migration,
ordinary save and scoped observer AI continuation, legal battery sites, and actual
movement of a large recalled army. Changes to saved state and multiplayer
behaviour require explicit format/protocol gates.

`quantbot_custom_attack` contains same-runtime OFF/ON/OFF exhausted-economy
coverage through the actual `exhaustedArmyEconomy()` predicate,
`declinesUnfundedRepair()`/repair reservation owner, `launchGroundHunt()` and
`attack()` paths. Its controls retain legacy holds for reachable spice, exact
cheapest-unit funding (with a fractional-shortfall boundary), pending harvester
cargo, city tax income and developing lots. It compares actual campaign orders
across switch states and protects human, defence and saboteur orders. Actual
repair-yard trips and bay occupants exercise AI-only release, protected human
and defender occupants, and the engine's automatic-repair no-rebooking rule.
The single-bay traveller controls establish one real booking at a time.
The actual repair-yard update proves both sides of the rounded-credit boundary
for one and five missing health points.
These fixture assertions describe coverage, not an unrun full-match or win-rate
certification.

`quantbot_whole_army_dispatch` checks whole-army building defence for both Hard
and Brutal in all three postures, including a reserve 47 tiles away, remote
colonies, two simultaneous attacks, hostile turret fire, actual movement and
damage, repeated scans and contact expiry. It retains human, repair, transport,
worker and aircraft boundaries and the existing bounded harvester response.
`quantbot_launcher_kiting` checks actual spacing and subsequent damage during
the base Hunt response, including a simultaneous worker raid, while retaining
ordinary local defence authority.

`quantbot_battery_clearance` uses real production and engine frames with concrete
on and off. It checks completed turrets, intact lots before material is ready,
cancellation, destroyed yards, changed occupancy, free-site retargeting, two
colonies, shared-human authority, local economic floors, all R/C/I types, and
the one-clearance limit across two yards holding real finished turrets. The
funding check includes a one-credit shortfall and exact funding after queued
costs have been withheld. Ready material also refuses changed occupancy, shared
human ownership and lost power. One/four actual path workers produce the same
clearance sites, timings in simulation frames, placements and zone counts. Bot-stream reload checks derived site selection;
this is not a new full-world save/observer certification. The progress fixture
supplies stable cash, power and an observed enemy approach without interference.

Purchase price and health provide a cheap, deterministic strength proxy. They
do not model every counter, splash hit, terrain bottleneck, or human manoeuvre.
Confirmed hostile fatalities use the victim's original-house purchase price,
independently of telemetry. Friendly fire, support units, repeated damage on a
dead object, and deviation rewards without a hostile fatality do not credit the
kill ledger. The existing own-loss callback supplies only the item type, so
borrowed units' own-loss cost remains an approximation using this house's price.
Full-match results must be read with these limits; fixture success does not
establish improved win rate.

The continuation probe starts from a canonical ordinary load and crosses the
unit/build phase boundary. It compares saved bot bytes, all unit orders, every
object's physical digest, RNG and the tested house's exact credit balances for
100 frames, and checks fresh survey reconstruction before both build passes.
Repeated ordinary loads compare complete house-inclusive digests. An observed
one-credit difference in another house's pre-existing city checkpoint accounting
means this is not certification of general full-world spectator parity.
