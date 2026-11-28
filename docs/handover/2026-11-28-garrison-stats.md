# Infantry combat statistics

Stefan reported zero Infantry/Troopers rows in the end-of-game Units table.

The purchase IDs deploy three (or five) ordinary Soldier/Trooper objects in
`BuilderBase::update`; they never accumulate live units or combat records.
The table now hides those purchase-only rows and retains the actual Soldier and
Trooper rows, including owned types whose production is disabled.

Garrison fire already constructs each projectile with the occupant object ID;
Bullet snapshots its owner/type and ObjectBase credits that house's combat ledger.
No duplicate building credit or new serialized counter is introduced.

The live probe also exposed gunfire launched from inside the zone's edge tile:
the bullet's first 20-pixel step still hit its own building, preventing damage
and kill credit until low-health evacuation. Garrison weapons now start just
outside that tile on the firing ray. Occupant attribution is unchanged, with no
new projectile state or damage exception.
Protocol 159 separates the changed combat simulation; save layout is unchanged.

| Requested observation | Assertion |
| --- | --- |
| Garrison Trooper kills and damages an enemy | Real Capture widget, projectile kill, own Trooper reward and sidebar Stats row |
| Soldier is present and correctly named | Same path with a Soldier, production disabled while still owned |
| No misleading zero squad rows | Enabled three/five-man purchase IDs absent from the live table |
| Garrison fire does not hit its own cover | Soldier/Trooper fire in eight directions plus a ranged Trooper rocket; building HP stays unchanged on every cycle |

`zone_garrison_stats_probe` is the focused headless integration check. The generic
Stats UI probes additionally reject purchase rows even with stale counter history.
Collapse-casualty Stats assertions accompany the separate garrison loss-path change,
so the table correction and existing firing attribution do not depend on that rule.
