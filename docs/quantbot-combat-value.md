# Combat value and Trooper air defence

Unit damage earns learning value from the hostile HP actually removed, weighted
by the victim's purchase price. A unit's killing blow adds a completion bonus:

```
prior = 4 * victim_price * 1000
bonus_permille = clamp(200 * (damage_milli + prior) / (lost_value_milli + prior), 50, 600)
kill_bonus_milli = victim_price * bonus_permille
```

The inputs are the destroyed type's cumulative damage value and losses under its
original house. Damage excludes earlier completion bonuses and conversion
estimates. With no evidence, or equal damage and loss value, the bonus is 20%.
Effective types earn a higher completion bonus, up to 60%; unsuccessful types
fall towards 5%. The four-unit prior softens small samples. The current victim's
loss enters the ledger after its bonus is sampled. Arithmetic is integer and
bounded; no unit scan or random draw is added to a killing blow.

Captured attackers still credit their controller's Deviator. Commanded captured
Devastator completion uses the same victim-type calculation, once. Friendly
damage, surviving victims, repeated hits on dead units and absorbed captive
losses earn no completion bonus. These are learning rewards, not spendable
credits or weapon damage. Existing saved ledgers remain compatible; session
telemetry records the bonus policy and its limits.

In city Custom games, autonomous Hard and Brutal bots may build one WOR and
produce Troopers while hostile aircraft are observed. Troopers supplement the
existing 20% Launcher value floor. They use their existing SmallRocket and
weapon range; no weapon buff is applied. Troop orders include queued infantry
in the aggregate infantry quota (Hard 12%, Brutal 10%) and preserve city and
economy funds. Core economy and progression take priority over the WOR. New
city troop orders stop when the observed air threat ends.

Autonomous rocket infantry in Hunt ignore aircraft beyond their exact weapon
range so they can advance towards reachable ground targets. Aircraft inside
range remain valid targets. Launcher spacing and wave tracking stay specific
to Launchers. Human orders and leases, support controllers, campaigns and
Easy/Medium controllers retain their existing behaviour.
