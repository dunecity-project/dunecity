# QuantBot aircraft policy

## Difficulty and targets

Autonomous QuantBot Ornithopters hunt enemy combat units offensively on Hard and Brutal. Those difficulties retain the existing local launcher-first policy and healthy-wing ratio of four aircraft per launcher. The configured offensive-raid switch still applies.

On Easy and Medium, an enemy unit is eligible only while it attacks a live owned structure, Harvester or Rebel Harvester within the contact range, or for three seconds after a confirmed positive-damage hit on a live owned asset. Proximity to a friendly asset alone does not authorize an attack. A past air order does not extend this permission once the attack and grace end. Defend remains defensive only.

Easy and Medium may also attack visible enemy base buildings when their footprint and direct approach have no observed hostile anti-air coverage. Walls and concrete are excluded from these opportunities. Rocket turrets cover their actual aircraft reach of three times their ground weapon range, with the existing safety margin. Launchers, Elite Launchers, Deviators, Troopers and Trooper squads also count. Fleet size never overrides this strict coverage. A temporary power outage or reload does not remove a defender from the safety model. Ordinary tanks do not count as anti-air.

Active defence takes priority and remains allowed under anti-air cover. Easy and Medium do not promote a non-attacking launcher escort into a defensive target. Autonomous aircraft never attack enemy harvesters. Explicit human orders retain their existing leases and authority.

The policy covers autonomous QuantBot aircraft, including campaign autonomy, and preserves support control. Other selectable legacy controllers have separate planners and retain their existing behaviour.

## Enforcement and work bounds

The planner, generic target searches, retained-target check and pre-shot check share the policy. Structure-sortie permissions are cleared and rebuilt per planner pass. Unit permission is checked against the current attack or recent confirmed-hit contact rather than a cached order.

Strict coverage is built once per controller pass from the existing defender walk and is only allocated for the lower difficulties. Engine checks use small per-house map lookups and the candidate's own target; they do not scan the map or enemy list per aircraft. Existing deterministic rank, score and object-id tie-breaks remain.

## Checkpoints and saves

Network protocol67 and observer runtime8 carry aircraft sortie permissions and recent attack contacts. This preserves immediate targeting decisions for a spectator joining between planner passes. Ordinary disk saves retain the existing plan-rebuild convention and save layout9853. The persisted House combat ledger is unchanged.
