# AI roles: Builder, Fighter, Both

Every menu that picks an AI shows three choices instead of one long list:

| Box | Values |
|---|---|
| AI | QuantBot, SmartBot, AI Player, Campaign AI (each menu offers the ones it supports) |
| Difficulty | the AI's levels, e.g. QuantBot Easy, Medium, Hard, Brutal, Defend |
| Role | Both, Builder, Fighter (only for AIs that implement roles; hidden where only opponents are picked) |

Opponent pickers (campaign and skirmish "Enemy AI", random-skirmish allies and enemies) show AI and
Difficulty only: an AI that owns a house plays Both.

## What each role does

| Role | Builds, produces, repairs, runs the city | Commands units: waves, defence, air, palace weapon |
|---|---|---|
| Both (`AIRole::Both`) | yes | yes |
| Builder (`AIRole::Builder`, formerly "AI Support") | yes | no: never attacks, its attack timer stays off |
| Fighter (`AIRole::Fighter`) | no | yes |

Roles matter most for a partner that shares the human's house. A Builder leaves the army to the human; a
Fighter leaves the base to the human. A unit the human orders belongs to the human for 120 seconds (or while
the order lasts) and both roles leave it alone (`QuantBot::humanControls`).

A Fighter differs from Both in four places (`QuantBot::fighterOnly()`):

- `update()` runs `fighterPass()` instead of `build()`: it sets the retreat point and fires ready palace
  weapons (`firePalaceWeapon`, the same code `build()` uses), and skips `keepPlannedSitesClear()` and
  `manageCityBuilding()`.
- `checkAllUnits()` leaves MCVs and the harvester routine alone; it still pulls harvesters out of danger.
- `onDamage()` defends a hit building but does not pay to repair it.
- `attack()` does not wait for a Repair Yard, and `militaryUnitValue()` leaves out units the human is
  ordering, so a wave is not judged ready on troops that will not join it.

Every rule that was switched off for the former support mode is now keyed to `builderOnly()`, so Builder and
Both behave exactly as the support and normal QuantBot did before.

## Player classes

The class string stays the identity that saves, replays, lobby events and match reports carry
(`PlayerFactory`):

| Role | QuantBot class |
|---|---|
| Both | `qBot<Difficulty>` |
| Builder | `qBotSupport<Difficulty>` |
| Fighter | `qBotFighter<Difficulty>` |

The retired Mentat AI's classes (`mentat<Difficulty>`, `mentatSupport<Difficulty>`) are never listed;
`getByPlayerClass` still resolves them to the QuantBot Both and Builder classes so old saves, replays and
settings load.

`PlayerFactory::PlayerData` records each class's family, difficulty label and role;
`getAIFamilies()`, `getDifficulties()`, `getRoles()` and `find()` drive the menus through
`AIPlayerSelector` (`include/GUI/dune/AIPlayerSelector.h`). The Fighter classes are appended to the factory
list, whose index the lobby sends, so they came with network protocol 104.

QuantBot saves its role as one byte where it used to save the support flag (save 9875). An older save's flag
loads as Builder or Both, and a pre-9875 `qBotSupport*` class loads as Builder. From save 9880 the byte may
also be 3 (Off); a 9875-9879 save holding 3 is corrupt and loads as Both.

A partner sharing the human's house is named after the house and its role, e.g. "Atreides (Builder)" or
"Atreides (Fighter)"; a Both partner keeps its AI's name, "Atreides (QuantBot)".

Match reports carry `ai_role` next to the existing `ai_support` (true for Builder). For a QuantBot they
report its live role, so a partner switched in game reports what it was doing at the end.

## Switching the partner in game (AI behaviour)

A human whose house has a QuantBot partner gets an "AI behaviour" button on the right: "AI: None",
"AI: Build", "AI: Fight" or "AI: Both". Each click moves to the next role in that order and wraps around.

- Layout (`GameInterface`): the sidebar row under Auto repair, shown while nothing is selected, when the
  whole stack below it (Movement paths, the three city overlays, Stats, Skip mission) still fits the screen.
  On a compact screen (480 lines with the city overlays and Skip mission) it sits beside the zoom controls
  above the sidebar, always visible, so no sidebar row is lost. No partner, observer or replay: no button.
- The click sends `CMD_AI_SUPPORT_ROLE(role)` (protocol 110). Every peer accepts it only from a
  `HumanPlayer` with a house and a role 0-3, and calls `QuantBot::setRole()` on every QuantBot in that
  house. The bot's class string and name do not change; queued production and current unit orders stay.
- None is `AIRole::Off`: `update()` and `onDamage()` return at once, so the partner gives no orders,
  builds nothing and repairs nothing. It also imposes no difficulty house rules: no city population
  ceiling, no harvester ceiling, no lifted unit-count limit or military-value override, no police
  reinforcement cap, no autonomous air or rocket hunts, and harvesters stopped by the engine restart
  as they do for a human house.
- Switching to Build or None turns the attack timer off, as a Builder starts. Switching from Build or
  None to Fight or Both starts it the way a human-allied partner starts (0 in a campaign, otherwise the
  seeded custom attack delay); Fight and Both switch between each other without touching it.
- The probe `custom-city-cap-probe.inc` (ctest `quantbot_custom_attack`) checks the command's
  authorization, Off's limits and empty update, and the timer on every switch; `army-recovery-probe.inc`
  checks the save round trip.
