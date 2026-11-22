# Restart with Easier AI

The in-game menu of a single-player game has **Restart with Easier AI** under
Restart Game. It restarts the game with every enemy AI one difficulty lower,
after a confirmation that names the change ("Enemy AI changes from QuantBot Hard
to QuantBot Medium.").

## The ladder

QuantBot Brutal → Hard → Medium → Easy. In a campaign (Dune Legacy or DuneCity)
the step below QuantBot Easy is the original Campaign AI (Stefan, 2026-11-22).
Outside a campaign QuantBot Easy is the bottom. QuantBot Defend, the Campaign AI
itself, role-limited QuantBots and the other AI families have no easier step.

The button is absent when no enemy can step down, in network games (co-op
included: they have no Restart Game either) and when the game has no human
player.

## What changes

- Only enemies: an AI in a house without a human player, on a team no human
  plays on. A partner sharing the player's house and an allied AI house keep
  their class.
- Each enemy steps down by one, so enemies of mixed difficulty stay mixed. The
  confirmation names the hardest enemy that can still step down.
- The restart is the ordinary Restart Game with changed house settings
  (`Game::setNextGameInitSettings`). Later campaign missions copy the house list
  of the mission before them, so the campaign continues against the easier
  enemies. The campaign menu's own Enemy AI choice is not changed.

Rule and tests: `include/players/EasierEnemyPolicy.h`,
`tests/EasierEnemyPolicyTestCase.cpp`. Menu: `src/GUI/dune/InGameMenu.cpp`.

## Why

Player statistics on 2026-11-22 (games from 1.0.865, see the website
repository's `docs/usage-statistics.md`, "How often players win"): players almost
never play on to a defeat (83 losses in 4,933 tries); they quit and retry. On
Easy they win 71–87% of tries on campaign levels 1–6 but 23.5% on level 8 and
29% on level 9, and about a quarter of the players who reach those levels never
win them.

The same statistics do not show the Campaign AI to be easier than QuantBot Easy
on levels 8–9: from 1.0.870, 24 wins in 109 tries (22%) against the Campaign AI
and 97 in 316 (31%) against QuantBot Easy; on levels 5–7, 72% against 67%. The
players choosing each are not the same people, so this is not a controlled
comparison, but the bottom step should be re-measured once the button is in use.
