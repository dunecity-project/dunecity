# House statistics and spectator budgets

The Stats sidebar window reads the selected house's in-memory counters. It does
not import or query SQLite. House selection belongs to each window: observing
another house does not change the local house, local player or command identity.
Observers can cycle valid match houses in house-id order and wrap to the first.
Participants see their own house.

## House summary meanings

- Units produced: cumulative units completed by the house, rather than units
  currently alive.
- Spice harvested: cumulative spice delivered for refining. The existing
  harvestedSpice counter stores gross refinery income after the fixed per-house
  income multiplier, so raw spice is that counter divided by the multiplier.
  Cargo still aboard harvesters is not included in this counter.
- Tax collected: cumulative gross city tax receipts. Police spending, current
  treasury and storage caps do not reduce this total.
- Population: the selected house's current city population at the same display
  scale as the game sidebar. Tax and population are unavailable in rulesets
  without a city economy.

The existing save format preserves the cumulative counters. The UI must not add
new simulation state or recompute a cumulative total from current objects.

## Unit ledger meanings

Active is the current registered count of that unit type in the selected
house's census, read from its existing item counter
(`House::getNumItems`). It is constant time per type: the column never scans the
world or the unit list. Passengers, units under repair and units being carried
are still owned, so they are still counted; units that have been destroyed are
not. The count is resampled with the rest of the ledger once per simulation
second while the window is open, so it can lag a very recent loss by up to that
one sample. Temporary deviation retains the original house census accounting,
as in the rest of the engine.

Kills come from CombatReward::Totals::kills, attributed to the attacker unit
type. House::getNumKilledItems instead indexes the victim type and cannot supply
this column. Deaths come from getNumLostItems. Damage is hpRemovedMilli divided
by 1000: actual HP removed, not the credit-equivalent damageMilli value used by
learning. Captured/deviated unit attribution follows the existing engine ledger.

Ambient Airplane, Ambient Helicopter, Rocket Trike and Elite Launcher are never
listed. The exclusion is applied before any counter is read, so it holds even
when such a type is enabled, carries ledger history or the house owns one.
Every other type is listed when the house can build it, when it has ledger
history, or when its Active count is positive: registered units remain listed
even when production of their type is disabled.

The five columns are measured against the font in use at construction: each
count column holds its header and the widest number it can carry, and the type
column takes the remainder. Numbers are never shortened; only a long type name
is given an ellipsis. The house summary figures are cumulative and separate from
Active, which is a current count.

## QuantBot allocation

A production pass exposes an observational snapshot of its actual performance
scores and intended army-value shares. Reading the window must never update
learning, call production, issue commands or compute a second allocation. The
snapshot is sampled with production and carried by the supplemental observer
checkpoint. An ordinary loaded game may show pending allocation until the next
production pass.

The internal score uses a scale of 1000000 per 1x. The window converts it to
`xN.N`, rounded to one decimal (2631248 displays as x2.6; 1347286 as x1.3).
Its base is reward value divided by
loss cost plus a one-unit price prior; measured learning uses its existing
performance history and fading exploration prior before allocation. The displayed
score is that pass's final score input, not a lifetime K/D or UI recomputation.
Goal % is the resulting target mix after the observed-air Launcher floor.

Grouped vehicle slots have a single group goal. The separate aggregate infantry
quota must not be presented as a per-type vehicle goal; its actual aggregate cap is shown separately. Performance and goal
numbers must remain readable instead of being removed by name truncation.

## Budget authority

Spectator fiscal controls are hidden and disabled. Callbacks also refuse to emit
tax or police-funding commands while observing. Participant callbacks use their
own command identity. Budget displays and house cycling are observational only.

Dialogs render after the game HUD so sidebar power/spice bars, credit digits
and the Spectating label cannot overwrite their text in the small viewport.
