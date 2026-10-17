# QuantBot play styles: research, style tournament and recommendations for AI play

Stefan: "do another round of ai work, testing additional play styles against each other then summarise
the updates and recommendations for ai play". Branch `feat/ai-play-styles` (from staging 688fd077 =
local 1.0.846: QuantBot default B W F C Q + U V X + I, WaveFormation None; protocol 81, save 9864).

**Result in one paragraph.** Eleven styles were built as test-only per-player overrides and played
2,406 recorded games (Fortress duels and Construction Yard starts on six bundled maps, Hard) on the mini,
MacBook Pro and Windows PC. The 1.0.846 default (BASE) is hard to beat: no fixed army composition and
no build-order style beats it; turtle, raid and boom lose clearly on Fortress (tempo). One style does:
**swarm** (keep the full-strength opening, then waves at 35% readiness instead of 60%) scores
**0.59 (0.51-0.67)** against BASE on CY starts over ~145 games and 0.52 (0.38-0.67) on Fortress, and it
tops every column of the round-robin. The styles do **not** form a rock-paper-scissors cycle: the
round-robin is close to transitive (swarm > BASE > timing ≈ siege > rush ≈ boom), so a per-game style
selector has nothing to exploit. **The QuantBot default is not changed in this round**: swarm has not yet
had its Brutal sample, `quantbot_all_against_atreides` or `/weak`; those runs are the next step
(Brutal and a further 108 CY games were started on the PC and the Air at the end of this round).

## 1. Research: RTS play styles and how bots choose between them

Sources were opened and read; `[INFERENCE]` marks reasoning of ours rather than a sourced claim.

### Archetypes, counters and what they mean for a Dune II engine

| Archetype | What it is | Beats / loses to | Evidence | DuneCity / QuantBot mapping |
|---|---|---|---|---|
| Rush | Attack before the opponent's economy, tech or defence is up, with cheap fast units | Beats greedy expansion; loses to scouted early defence, and a failed rush leaves the rusher behind | [Rush (video games)](https://en.wikipedia.org/wiki/Rush_(video_games)); Weber & Mateas, [CIG 2009](https://eis.ucsc.edu/papers/cig_2009.pdf) ("overwhelm an opponent with inexpensive units early"); UAlbertaBot's default strategies are rushes ([config](https://raw.githubusercontent.com/davechurchill/ualbertabot/master/UAlbertaBot/bin/UAlbertaBot_Config.txt)); OpenRA Rush AI `SquadSize: 20` v Normal 40 ([ra ai.yaml](https://raw.githubusercontent.com/OpenRA/OpenRA/bleed/mods/ra/rules/ai.yaml)) | Trike / Raider Trike / Quad from a light factory built first; small early waves (`rush`) |
| Timing attack | Build to a trigger (army size, tech, time), then one hard hit | Beats an opponent still teching at the trigger; loses to a defence that is ready then [INFERENCE] | Weber & Mateas (above); EISBot ([AAAI FS 2011](https://cdn.aaai.org/ocs/4209/4209-17783-1-PB.pdf)) fires timing attacks from build-order annotations; Dune II guides: stockpile, then one maximal attack at factories and the yard ([Nahoo](https://duneii.nahoo.net/strategies)) | Waves only at full readiness (`timing`); bigger waves were already measured to lose (round 3 `L`) |
| Turtle / boom | Static defence, economy and tech behind it, attack late | Beats rushes and under-sized waves; loses to out-ranging artillery and to an opponent taking the map's resources | [Turtling](https://en.wikipedia.org/wiki/Turtling_(gameplay)); OpenRA Turtle AI (attack delays 100000, defence fractions 7-13%); Dune II: 5-6 turrets on the attack path (Nahoo) | Eight baseline rocket turrets, waves at 130% (`turtle`) |
| Fast expand / greedy economy | More harvesters, refineries and yards early, less army | Beats turtles; loses to rushes and early timings | Steamhammer's `NakedExpand`/`SafeExpand` plans ([opponent model](http://satirist.org/ai/starcraft/blog/archives/474-the-opponent-model-in-Steamhammer-1.4.html)); "Expand" the most common Protoss tier-2 plan (Weber & Mateas); "always build at least 2 refineries" (Nahoo) | Harvest as if alone on the map, an extra refinery, a second yard (`boom`) |
| Harass / raid, economic denial | Small fast groups hit harvesters and outlying buildings, never the main fight | Beats unescorted economies; loses to escorts, turrets at refineries, quick responders; denial gimmicks can be weak (Steamhammer's gas steal "achieved nothing significant", [AIIDE 2022](http://satirist.org/ai/starcraft/blog/archives/1199-AIIDE-2022-what-Steamhammer-learned.html)) | Dune 2000: Raider trikes "harass an opponent's harvesters", Ordos hit-and-run ([OpenRA book](https://raw.githubusercontent.com/OpenRA/book/master/src/playing/strategy-guides/Dune-2000.md)) | Six light vehicles hunting enemy harvesters, then refineries, all game (`raid`) |
| Tech switch | Open with one composition, pivot once the opponent commits | Beats over-committed counters; loses to a timing that lands mid-switch [INFERENCE] | Overkill's learner defends a rush, then techs to lurker/mutalisk ([wiki](https://github.com/sijiaxu/Overkill/wiki)) | `rush` switches from light vehicles to the learned heavy mix after 12 minutes |
| Air | Mass air units against an opponent short of anti-air | Beats ground-only armies and gun-turret turtles; loses to anti-air (rocket turrets kill Ornithopters) | Dune II mission 7: the AI "will always send a series of Ornithopters"; rocket turrets as the answer (Nahoo, [Dune 2 unit statistics](https://unitstatistics.com/dune2)) | Ornithopter-heavy mix (`air`; Atreides and Ordos only - Harkonnen has none) |
| Artillery / siege | Long-range units break defences from out of turret range | Beats turtles; loses to fast units that close in (quads next to launchers) | Turtling article; Dune II: launchers escorted by tanks kill rocket turrets and walls (Nahoo) | Siege tanks and launchers behind a tank screen (`siege`) |
| Super-unit push | The house special (Devastator, Sonic Tank, Deviator) as the core | [INFERENCE] strong per unit but slow and expensive; Dune 2000 bots weight specials heavily (OpenRA d2k Vidius: sonic 50, devastator 40, [d2k ai.yaml](https://raw.githubusercontent.com/OpenRA/OpenRA/bleed/mods/d2k/rules/ai.yaml)) | | Half the style mix in the special (`heavy`) |
| Mass armour | Cheap, sturdy main battle tanks | [INFERENCE] beats light units and infantry; loses to rockets | | 65% combat tanks (`armor`) |
| Counter-composition | Scout, then build what beats the enemy mix | Beats fixed compositions; loses to deception, fast switches, scouting lag | Strategy selection as "determining which unit types to produce ... to counter opponent strategies" (EISBot); Steamhammer counters by predicted plan | Blend the mix towards counters of the remembered enemy army (`counter`) |

The literature expects **cycles, not a ranking**: Tavares, Azpúrua, Santos & Chaimowicz, "Rock, Paper,
StarCraft: Strategy Selection in Real-Time Strategy Games" ([AIIDE 2016](https://ojs.aaai.org/index.php/AIIDE/article/download/12857/12704/16373))
found that among the AIIDE 2015 Protoss bots "Skynet dominates CruzBot, which dominates Xelnaga, which
dominates Skynet" - no pure strategy dominated. The textbook triangle is rush > fast expand > turtle >
rush, with siege beating turtle and anti-air beating air.

### Bot personalities are mostly configuration

OpenRA's Rush, Normal, Turtle and Naval AIs differ only in `ai.yaml` values (squad size 20/40/10/1,
attack delays, building fractions); the Dune 2000 mod's Omnius, Vidius and Gladius are small deltas of
one another. C&C Generals' `AIData.ini` holds economy-tempo knobs and leaves personality to scripts. This
is the design used here: a style is a QuantBot Config delta plus, only where no key exists, a small
test-only behaviour.

### Choosing a style: strategy selection

- **UCB1** (Auer, Cesa-Bianchi & Fischer, [Machine Learning 2002](https://people.eecs.berkeley.edu/~russell/classes/cs294/s11/readings/Auer+al:2002.pdf)):
  play each arm once, then the arm with the largest $\bar{x}_j + \sqrt{2\ln n / n_j}$.
  Overkill picks its opening with $\bar{x}_j + 0.7\sqrt{\ln i / n_j}$ over previous games against the
  same opponent ([ucb.py](https://raw.githubusercontent.com/sijiaxu/Overkill/master/ucb.py));
  UAlbertaBot has the same idea ([StrategyManager.cpp](https://raw.githubusercontent.com/davechurchill/ualbertabot/master/UAlbertaBot/Source/StrategyManager.cpp))
  but switched off in its own code.
- **Others:** MegaBot ε-greedy over whole sub-bots; tscmoo by past success; Steamhammer predicts the
  opponent's plan from discounted game records and picks a counter opening from hand-weighted lists;
  PurpleWave fits a Bayesian logistic regression of win on strategy with recency weighting
  ([StrategyScore.scala](https://raw.githubusercontent.com/dgant/PurpleWave/master/src/Strategery/Selection/StrategyScore.scala)).
- **What works:** in Tavares et al.'s tournament of selectors, reply-last won 60.9% on average,
  α-Nash 54.7%, ε-greedy 53.0%, frequentist 52.9%, Nash 50.5%, a single fixed choice 28.1%. Bandits
  assume a stationary opponent; QuantBot itself adapts its unit mix within a game.
- **For QuantBot** these selectors need memory *across* games against the same opponent, which a
  skirmish AI does not have (every game starts fresh, and multiplayer requires every peer to agree).
  What it can use is information available in the game: map size and shape, its house, the opponent's
  house and what it has seen of the enemy army.

## 2. Styles as test-only per-player overrides

### Mechanism (protocol and save unchanged; inert in every real game)

- `QuantBot::configOverride` (`std::shared_ptr<const QuantBotConfig>`) and `QuantBot::botConfig()`:
  every QuantBot read of the config goes through `botConfig()`, which is the global config unless the
  harness gave this bot its own copy. `QuantBotConfig::applyOverrides(path)` applies the keys present in an
  INI file over a copy (any `QuantBot Config.ini` key) without creating or writing a file; `load()` now
  shares the same reader. Not saved, not hashed (`getConfigHash()` still hashes only the global config).
- `QuantBot::PlayStyle playStyle` (`src/players/QuantBotStyle.cpp`): behaviours no key expresses -
  a style unit mix blended into the learned mix (`MixBps`, `Mix`, `MixMinutes`), counter-composition from the
  remembered enemy army (`CounterBps`), a light-factory-first opening without the repair-yard attack wait
  (`LightFirst`), viable wave size (`MinWaveUnits/Value`), baseline rocket turrets (`RocketTurrets`),
  greedy harvesting / extra refineries / a yard floor (`GreedyHarvest`, `ExtraRefineries`, `MinYards`) and
  harvester raiders (`RaidUnits`: light vehicles hunting the nearest enemy harvester, then refinery).
  `style()` returns inert defaults outside Custom Hard/Brutal. Raiders are runtime state only.
- Harness labels: `default` = the configured rules (no force/formation override; bit-identical to
  `none+BWFCQUVXI`, PROBE_DIGEST checked), `@style` = `tests/ai/play-styles/<style>.ini` (config keys +
  `[Play Style]`), e.g. `default@swarm`. `vanilla-match.py --ffa a,b,c,d` (one label per house, four
  rotations); `--house-offset` on both harnesses so seed-split runs keep the full list's house pairs;
  `tests/ai/play-style-matrix.py` prints the screen, matrix and per-map/house breakdowns.
- Production behaviour is unchanged: replays of 1.0.846 reference games (`merge-846` m846h/m846cy, 8
  games) matched winner, end time and house rows exactly; runner parity (headless-bench `--compare`): Pro
  identical, Windows identical apart from the known ±0.1 s `FORMATION_WAVE`/`end_s` timing fields; 12
  style games replayed on Windows matched the mini's rows and per-minute samples.

### The styles (all on top of BASE)

| Style | Archetype | Definition (`tests/ai/play-styles/*.ini`) |
|---|---|---|
| `rush` | rush / tech switch | Light factory first, no repair-yard wait, `OpeningReadiness=0`, `WaveReadinessPercent=15`, waves from 4 units / 1,200; 80% of the mix quads/trikes/raiders + tanks for 12 minutes, then the learned mix |
| `swarm` | constant pressure | Full-strength opening kept, then `WaveReadinessPercent=35`, waves from 4 units / 2,000 |
| `raid` | harass / economic denial | 6 light raiders on enemy harvesters then refineries all game; 25% of the mix raiders/quads |
| `turtle` | turtle | 8 baseline rocket turrets (default 2), waves at 130% |
| `boom` | fast expand / greedy economy | Harvest as if alone, +1 refinery, a second yard after the heavy factory, waves at 100% |
| `timing` | timing attack | Every wave at 100% readiness |
| `air` | air | 70% of the mix from 60% ornithopters (Harkonnen: ground slots only) |
| `siege` | artillery / siege | 70% of the mix from siege 50 / launcher 30 / tank 20 |
| `heavy` | super-unit push | 70% of the mix from special 50 / tank 25 / siege 15 / launcher 10 |
| `armor` | mass armour | 70% of the mix from tank 65 / siege 20 / launcher 15 |
| `counter` | counter-composition | 60% of the mix towards counters of the remembered enemy army (light → tanks/siege, armour → launchers/siege, launchers → quads/tanks, air → launchers) |

## 3. Tournament

Hard. Fortress = `formation-duel.py` Random Skirmish 128/160/192, seeds 7101-7606 (one house pair per
seed), 40 game minutes. CY = `vanilla-match.py` on Habbanya-Penny, Ergun-Sahij, Tuono-Soolin, Sihaya-Pip,
X-Factor, Sihaya-Lauper, 80 game minutes. Both label assignments for every seed/map/house; time-outs go
to the side 1,000+ credits ahead on killed value; 95% bootstrap intervals. Data:
`docs/formation-results/play-styles/<tag>/games.csv` (+ `houses.csv` for CY); `s*` = screen, `r*` =
round-robin. Games recorded: mini 1,197, MacBook Pro 712, Windows PC 497 (2,406; plus 26 parity games;
the Air was on battery for most of the round and ran only its 6-game parity set). Note: the first 54 Pro
and 7 PC screen games (`s1c-pro`, `s1c-win`) were Atreides v Harkonnen only (a single-seed run picks house
pair 0; fixed with `--house-offset`); they are valid games and included, the house table shows the mix.

### Screen: every style v BASE

| Style v BASE | Fortress score (95% CI) n W-L/T | CY score (95% CI) n W-L/T | CY verdict | Pooled | CY win / loss min (median) |
|---|---|---|---|---|---|
| swarm | 0.528 (0.36-0.69) n=36 13-11/12 | 0.588 (0.47-0.71) n=68 39-27/2 | level with | 0.567 | 37.8 / 43.6 |
| boom | 0.333 (0.19-0.50) n=36 7-21/8 | 0.500 (0.38-0.62) n=66 33-33/0 | level with | 0.441 | 42.4 / 40.7 |
| timing | 0.361 (0.19-0.53) n=36 8-18/10 | 0.484 (0.38-0.61) n=64 30-32/2 | level with | 0.440 | 43.3 / 39.0 |
| siege | 0.486 (0.33-0.64) n=36 9-10/17 | 0.484 (0.36-0.61) n=64 30-33/1 | level with | 0.485 | 44.9 / 41.7 |
| rush | 0.514 (0.36-0.68) n=36 12-10/14 | 0.456 (0.34-0.57) n=68 31-37/0 | level with | 0.476 | 37.7 / 42.6 |
| counter | 0.500 (0.33-0.67) n=36 9-12/15 | 0.453 (0.34-0.58) n=64 29-35/0 | level with | 0.470 | 43.3 / 46.3 |
| air | 0.472 (0.31-0.64) n=36 12-10/14 | 0.422 (0.31-0.55) n=64 26-37/1 | level with | 0.440 | 44.2 / 42.1 |
| raid | **0.139** (0.03-0.25) n=36 1-26/9 | 0.412 (0.29-0.53) n=68 28-40/0 | level with | 0.317 | 40.4 / 34.3 |
| turtle | **0.194** (0.08-0.33) n=36 2-24/10 | 0.388 (0.28-0.51) n=67 26-41/0 | level with | 0.320 | 42.2 / 45.8 |
| heavy | 0.403 (0.25-0.57) n=36 10-12/14 | **0.344** (0.22-0.47) n=64 22-41/1 | loses to | 0.365 | 43.7 / 41.8 |
| armor | 0.431 (0.28-0.60) n=36 9-11/16 | **0.281** (0.17-0.39) n=64 18-46/0 | loses to | 0.335 | 39.8 / 43.5 |

By map (CY, score v BASE; bold = interval excludes 0.5): rush wins X-Factor 0.80 (32x32) but loses
Habbanya-Penny 0.29; swarm 0.80 on Sihaya-Pip and X-Factor; turtle **0.08** on Habbanya-Penny; heavy
**0.17** Ergun-Sahij, **0.10** Sihaya-Pip; armor **0.00** Tuono-Soolin. By house: rush, turtle, raid,
heavy and armor all do worst as Harkonnen (**0.23-0.29**); swarm is the only style ≥0.54 for every house
(Atreides 0.55, Harkonnen 0.54, Ordos 0.72).

### Round-robin (BASE + the five best screen styles)

CY, 6 maps, seeds 9101/9202/9303, all three house pairs (target 108 games per pair; 68-76 per cell had
finished when this report was cut - the rest is still running, `r1c-*`):

| Row v column | BASE | swarm | timing | rush | boom | siege | Row mean |
|---|---|---|---|---|---|---|---|
| BASE | - | 0.41 (0.30-0.51) | 0.53 (0.43-0.64) | **0.62** (0.51-0.73) | 0.54 (0.43-0.65) | 0.57 (0.46-0.68) | 0.534 |
| swarm | 0.59 (0.49-0.70) | - | 0.61 (0.50-0.72) | **0.62** (0.51-0.73) | 0.62 (0.50-0.73) | **0.65** (0.54-0.76) | 0.618 |
| timing | 0.47 (0.36-0.57) | 0.39 (0.28-0.50) | - | 0.57 (0.46-0.68) | 0.52 (0.41-0.63) | 0.53 (0.41-0.64) | 0.495 |
| rush | **0.38** (0.27-0.49) | **0.38** (0.27-0.49) | 0.43 (0.32-0.54) | - | 0.56 (0.43-0.67) | 0.49 (0.37-0.60) | 0.446 |
| boom | 0.46 (0.35-0.57) | 0.38 (0.27-0.50) | 0.48 (0.37-0.59) | 0.44 (0.33-0.57) | - | 0.43 (0.31-0.54) | 0.437 |
| siege | 0.43 (0.32-0.54) | **0.35** (0.24-0.46) | 0.47 (0.36-0.59) | 0.51 (0.40-0.63) | 0.57 (0.46-0.69) | - | 0.469 |

Fortress (same pairs, 11-12 games per cell so far, `r1f`): swarm row mean 0.617, BASE 0.600, rush 0.545,
siege 0.527, boom 0.361, timing 0.350; no cell's interval excludes 0.5.

**Swarm v BASE pooled** (screen + round-robin): CY **0.59 (0.51-0.67)**, ~145 games; Fortress 0.52
(0.38-0.67), 48 games.

**Rock-paper-scissors check.** No cycle: swarm is ≥0.59 against every entrant, BASE is ≥0.53 against all
but swarm, and the weaker styles order roughly timing > siege > rush ≈ boom. The textbook triangle does
not appear: rush does not beat boom (0.56, level), boom does not beat turtle-like play (turtle was not
competitive enough to enter). The few map-specific edges (rush on 32x32 X-Factor) are small samples.

Not done in this round (request budget): the Brutal sample, the 4-player FFA with mixed styles (the
harness supports it: `vanilla-match.py --ffa default,default@swarm,default@timing,default@siege`), the
remaining round-robin games, and the full 12x12 matrix of the weaker styles.

## 4. Decision

- **No default change.** The rule: a style becomes the default only with its CY interval above 0.5
  over ≥108 games, not worse on Fortress and Brutal, `quantbot_all_against_atreides` 6/6 and `/weak` not
  worse. Swarm meets the first two (0.59, 0.51-0.67; Fortress 0.52) but its Brutal sample, the
  all-against-Atreides match and `/weak` have not been run. Swarm is config-only
  (`[Force Concentration] WaveReadinessPercent=35` plus a 4-unit / 2,000-credit minimum wave, which today is
  a fixed 6 / 3,000 in code), so adopting it is small: the INI default, the minimum-wave constants as config
  keys, protocol 82 (no save change).
- **No strategy-selection rule.** Selection pays only when styles counter each other; the matrix is close
  to transitive, so a selector (per map, house or scouting, or bandit-style) would at best pick swarm every
  time. Map-specific selection (rush on 32x32) is not supported by enough games.

## 5. Recommendations for AI play

**For QuantBot (by game type, map, house):**
- **Keep the learned unit mix.** Every fixed composition (armour, super-units, siege, air) lost to the
  learned mix, armour and super-units clearly on CY starts. Composition should stay a result of combat
  feedback, not a personality.
- **Pressure beats patience.** The next candidate default is more frequent waves after a full-strength
  first wave (swarm). Waiting styles lose: turtle and boom lose Fortress games badly (0.19, 0.33) and are
  level at best from a CY start; timing (100% waves) is slightly worse than 60%; 150% was already
  measured to lose (round 3). This extends rounds 3-5: tempo first, but never a weak *first* wave (the
  opening readiness I stays).
- **Do not add a harvester-raid personality** (0.14 Fortress, 0.41 CY): six light units hunting
  harvesters are lost to the defence and missing from the main fight. Raiding as opportunistic targeting
  by units already near harvesters (what QuantBot's light-raider prey rule does) is enough.
- **Do not rush from a CY start**, except possibly on 32x32 maps (X-Factor 0.80 in the screen, 0.79 in the
  round-robin; a small sample, worth a dedicated test before any rule).
- **By house:** Harkonnen is the house where deviating from the default costs most (no ornithopters, slow
  army; rush/turtle/raid/heavy/armor 0.23-0.29 as Harkonnen); Ordos gains most from swarm (0.72).
- **By map:** large open maps (Habbanya-Penny 128x128) punish turtling (0.08) and siege (0.17 in the
  round-robin); long thin maps (Sihaya-Pip/Lauper) favour pressure (swarm 0.57-0.80).

**What players should expect from QuantBot (1.0.846, unchanged by this round):** a full-strength first
attack from a CY start (around minute 15-20 on Hard), then a steady stream of waves at 60% readiness
launched from a forward bundle, focus fire in fights, a perimeter hold with rocket turrets while it is
outnumbered, and a unit mix that shifts towards what has worked against you. It does not raid
harvesters, rush with light vehicles or turtle; walling up and teching against it is risky because it
keeps attacking, and it adapts its composition to yours.

**Next steps:** finish the running round-robin (`r1c-*`, `r1f`), the Brutal sample (`b1c-w` on the PC:
swarm/timing/siege v BASE) and the extra 108 swarm-v-BASE CY games (`c1c-air` on the Air); if swarm holds,
run `/weak` (`formation-duel.py --pairs "default@swarm/weak:none"`, against BASE's 0.42) and
`quantbot_all_against_atreides` with `WaveReadinessPercent=35`, then make it the default (protocol 82).

