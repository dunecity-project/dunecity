# AI roles: Builder, Fighter, Both

Every menu that picks an AI shows three choices instead of one long list:

| Box | Values |
|---|---|
| AI | QuantBot, Mentat, SmartBot, AI Player, Campaign AI (each menu offers the ones it supports) |
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

| Role | QuantBot class | Mentat class |
|---|---|---|
| Both | `qBot<Difficulty>` | `mentat<Difficulty>` |
| Builder | `qBotSupport<Difficulty>` | `mentatSupport<Difficulty>` |
| Fighter | `qBotFighter<Difficulty>` | — |

`PlayerFactory::PlayerData` records each class's family, difficulty label and role;
`getAIFamilies()`, `getDifficulties()`, `getRoles()` and `find()` drive the menus through
`AIPlayerSelector` (`include/GUI/dune/AIPlayerSelector.h`). The Fighter classes are appended to the factory
list, whose index the lobby sends, so they came with network protocol 104.

QuantBot saves its role as one byte where it used to save the support flag (save 9875). An older save's flag
loads as Builder or Both, and a `qBotSupport*` class still forces Builder.

A partner sharing the human's house is named after the house and its role, e.g. "Atreides (Builder)" or
"Atreides (Fighter)"; a Both partner keeps its AI's name, "Atreides (QuantBot)".

Match reports carry `ai_role` next to the existing `ai_support` (true for Builder).
