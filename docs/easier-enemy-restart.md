# Restart on New Difficulty, and the default difficulty

Stefan, 2026-11-22: "set easiest as the default. include in main menu a restart on new
difficulty - you can select what difficulty". This replaces the one-step **Restart with Easier
AI** button added earlier the same day.

## The default enemy is QuantBot Easiest

`EnemyDifficultyPolicy::kDefaultEnemy` (`qBotEasiest`) is what these menus select for a new game:

- the campaign and DuneCity campaign enemy AI (`HouseChoiceMenu`; chosen again on every launch,
  the menu's choice is not saved);
- the Skirmish menu's enemy AI;
- the AI that fills the open seats of an offline Custom Game.

Not changed: Random Skirmish and Civilization Wars keep their saved choice or `qBotMedium`
(`SkirmishGenerator::Side`, `CivWarsGenerator::Params`), and `DEFAULTAIPLAYERCLASS`
(`qBotEasy`) remains the fallback for a class a map or save names that no longer exists.
QuantBot Easiest is still the last entry of the difficulty lists
(`PlayerFactory::getDifficulties`), because its lobby index was appended.

In a DuneCity campaign mission QuantBot Easiest plays the map as authored (`[DIFFICULTY]`
strength 1.0), or at Easy's value where that is lower (mission 6: 0.85), so it never starts
stronger than Easy (`MissionObjectivePolicy::enemyStrengthPermille`).

## Restart on New Difficulty

The in-game menu of a single-player game has **Restart on New Difficulty** under Restart Game.
It opens a dialog with the enemy AI being played, a list of difficulties with that one selected,
**Restart** and **Cancel** (Escape cancels). Restart starts the game again from the beginning
with every enemy on the chosen difficulty, easier or harder.

- The list: QuantBot Easiest, Easy, Medium, Hard, Brutal; in a campaign (Dune Legacy or
  DuneCity) also the original Campaign AI.
- Only enemies already on that list change. QuantBot Defend, role-limited QuantBots and the other
  AI families keep their class; when no enemy can change, the button is absent.
- An enemy is an AI in a house without a human player, on a team no human plays on. A partner
  sharing the player's house and an allied AI house keep their class.
- With enemies of mixed difficulty the dialog opens on the hardest, and all of them move to the
  chosen one.
- The button is absent in network games (co-op included: they have no Restart Game either) and
  when the game has no human player.
- The restart is the ordinary Restart Game with changed house settings
  (`Game::setNextGameInitSettings`). Later campaign missions copy the house list of the mission
  before them, so the campaign continues on the chosen difficulty.

Rule and tests: `include/players/EnemyDifficultyPolicy.h`,
`tests/EnemyDifficultyPolicyTestCase.cpp`. Dialog: `src/GUI/dune/RestartDifficultyWindow.cpp`,
opened from `src/GUI/dune/InGameMenu.cpp`. Real-menu probe: `verifyRestartDifficulty` in
`tests/menu/save-as-map-probe.inc`.

## Why

Player statistics on 2026-11-22 (games from 1.0.865, see the website repository's
`docs/usage-statistics.md`, "How often players win"): players almost never play on to a defeat
(83 losses in 4,933 tries); they quit and retry. Against Easy in a 1v1 custom game 16 of 39
players ever won, and on campaign level 8 about a quarter of the players who reach it never win
it (`docs/campaign-ai-difficulty-matrix.md`).

The same statistics do not show the Campaign AI to be easier than QuantBot Easy on levels 8-9:
from 1.0.870, 24 wins in 109 tries (22%) against the Campaign AI and 97 in 316 (31%) against
QuantBot Easy. The players choosing each are not the same people, so this is not a controlled
comparison; it is why the Campaign AI is a choice in the list and not the default.
