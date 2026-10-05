# QuantBot army recovery

Custom-game Hard and Brutal bots use a house-level army posture, rather than
asking every unit to decide independently whether the whole army is losing.
Campaign roles, support bots, and Easy/Medium retain their existing pacing.

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
workers, aircraft, human orders, repair and live defence contacts retain their
existing handling.

## Batteries and special units

Once two refineries, a heavy factory, and a repair yard exist, important bases
can grow overlapping rocket batteries towards the observed enemy approach.
Coverage demand drives the goal: an opening battery starts around 8–12 turrets
and a mature base can exceed 24. First cover for critical buildings, remote
colonies, economy reserves, power rules, construction exits, and movement
corridors retain priority. Battery construction is spread across passes.

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
