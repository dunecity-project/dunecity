# QuantBot play styles: research, style tournament and recommendations for AI play

Stefan: "do another round of ai work, testing additional play styles against each other then summarise
the updates and recommendations for ai play". Branch `feat/ai-play-styles` (from staging 688fd077 =
local 1.0.846: QuantBot default B W F C Q + U V X + I, WaveFormation None; protocol 81, save 9864).

**Update (branch `feat/ai-strategy-mix`, protocol 82, save 9865): see section 6.** Unit mix and tempo are
now two production strategy factors with their own weighted, multiplayer-deterministic random pick. The
finished round-robin, Brutal and swarm-confirmation runs of this round are in 6.1. The default changes to
the learned mix with the **Swarm** tempo, the only strategy measured above 1.0.846. `Mode=Random` plays the
measured weights: it gives more variety and was level with 1.0.846 in the measurements.

**Scoring changed (branch `test/match-scoring`): see section 7.** AI test games are now scored by
placement: +1 for first, -1 for last, and a time-out is a negative draw for every surviving house. A
candidate becomes the default only when its paired score difference against the control is above 0 with
95% confidence. Sections 3-6 were scored with the old rule (1/0, time-outs decided on killed value or
0.5, interval above 0.5); section 7 re-scores them. Swarm still clears the rule on CY starts. Round 5's
UVX default would no longer have passed it.
**Update (branch `feat/ai-ratio-tuning`, protocol 83, save 9866): see section 8.** Every starting mix is
bounded (each producible unit type 2%-50%); new `Tuned` and `Bounded` (random within the bounds) mixes;
randomised, control and tuned arms.

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
to the side 1,000+ credits ahead on killed value; 95% bootstrap intervals (the legacy scoring, re-scored
with the placement score in section 7). Data:
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

- **No default change.** The rule at the time (now replaced by section 7.2's paired placement-score rule):
  a style becomes the default only with its CY interval above 0.5
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


## 6. Strategies: unit mix x tempo (production, protocol 82, save 9865)

Stefan: "the ai should try different unit ratio strategies and one could even be massing say quads etc";
"add that, separate thing to simulate - we need controls for both"; "still allow the ai to 'learn' better
ratios based on unit performance per current logic". Branch `feat/ai-strategy-mix` (from `c8fcaaa8`).

### 6.1 The play-style runs finished

All of these are CY, Hard. Scores are against BASE.
- **swarm, pooled** (screen, round-robin and the Air's 108 `c1c-air` games): **0.58 (0.52-0.64), n=270**.
- **Round-robin, 94 games per cell.** Row means: swarm 0.606, BASE 0.526, timing 0.505, boom 0.453,
  siege 0.465, rush 0.447. swarm beats rush 0.61, boom 0.63 and siege 0.61, each with its interval
  above 0.5. There is still no cycle.
- **Fortress round-robin** (`r1f`, 14-16 games per cell): row means swarm 0.586, rush 0.559, BASE 0.557,
  siege 0.529, boom 0.409, timing 0.361.
- **Brutal** (`b1c-w`, 24 games each): swarm 0.54, siege 0.54, timing 0.50.

### 6.2 Mechanism

**Factors.** `QuantBotStrategy` (`include/players/QuantBotStrategy.h`, `src/players/QuantBotStrategy.cpp`)
has two factors:
- **Mix:** Learned, QuadMass, TrikeSwarm, TankMass, Artillery, Specials, Air, Infantry, Counter.
- **Tempo:** Base, Swarm, Timing, Rush, Flood.

**Config.** `[Strategy]` in QuantBot Config.ini. The keys are optional, never written by `save()`, and hashed:
- `Mode = Random | Fixed:<Mix>,<Tempo>`. The default is `Fixed:Learned,Swarm`.
- `MixWeights = Learned:50,QuadMass:15,Counter:10,Artillery:10,Air:10,TrikeSwarm:5` (the defaults).
- `TempoWeights = Swarm:80,Flood:20` (the defaults).
- A malformed value keeps the previous one and is logged. Values are `name:weight` because INIFile
  rejects a second `=`.

**Determinism.**
- Each factor is picked once by `QuantBotStrategy::pick(settings, seed, house, eligible)`.
- The draw is SplitMix64 over the game's random seed (`GameInitSettings::getRandomSeed()`), the house id
  and a per-factor salt. No simulation RNG draw, clock or local setting is involved, and the result
  does not depend on the order bots are created or updated.
- The pick happens at the bot's first `update()` (`QuantBot::pickStrategy`).
- It is saved (SAVEGAMEVERSION 9865: picked flag, mix, tempo). A 9864 save loads unpicked and is picked
  by the same function.
- Telemetry: the `strategy` decision plus a `QUANTBOT_STRATEGY:` log line. Harness `houses.csv` has a
  `strategy` column.

**Where strategies apply.** Custom Hard/Brutal only (`autonomousCustomHardBrutal()`). Every other bot
plays Learned/Base.

**Eligibility.** A mix needs at least half of its prior's weight in slots the house can build
(ObjectData builder/tech level). Harkonnen therefore never draws Air or TrikeSwarm, and a fixed mix the
house cannot field becomes Learned. Infantry needs a buildable infantry unit.

**A mix is the prior, not a frozen composition.** The strategy's ratios replace the configured opening
mix (`defaults`), which `UnitMixPolicy::allocate` blends combat evidence into (QuantBotBuild.cpp). The
learning is unchanged:
- Before evidence, production follows the prior.
- On vanilla maps the evidence blend moves production toward what performs as the lost value grows
  against the fielded army.
- `holdPercent` (100 for every mix) can make a prior hold longer.

**Other per-strategy rules.**
- **Counter:** its prior is the counter of the remembered enemy army (`enemyCounterPrior`).
- **Infantry:** 35% of the army on foot; Custom vanilla then founds a barracks and a WOR.
- **Air:** unlocks ornithopters by upgrading the Hi-Tech Factory first. It pays from credits the reserves
  hold back, because otherwise the upgrade never happened.

| Mix | Starting ratios (tank, siege, launcher, special, ornithopter, trike, raider, quad) | Notes |
|---|---|---|
| Learned | configured opening (1.0.846) | |
| QuadMass | 20 / 0 / 0 / 0 / 0 / 0 / 10 / 70 | raider trikes only for Ordos |
| TrikeSwarm | 10 / 0 / 0 / 0 / 0 / 45 / 45 / 0 | not Harkonnen |
| TankMass | 70 / 15 / 15 / 0 / 0 / 0 / 0 / 0 | |
| Artillery | 20 / 45 / 35 / 0 / 0 / 0 / 0 / 0 | Ordos launchers masked |
| Specials | 25 / 15 / 10 / 50 / 0 / 0 / 0 / 0 | Devastator / Sonic Tank / Deviator |
| Air | 15 / 10 / 15 / 0 / 60 / 0 / 0 / 0 | not Harkonnen |
| Infantry | configured opening, infantry 35% | |
| Counter | counter of the remembered enemy army | |

| Tempo | Wave readiness | Smallest wave | Other |
|---|---|---|---|
| Base | configured (60%) | 6 units / 3,000 | 1.0.846 |
| Swarm | 35% | 4 / 2,000 | = the play-style `swarm` (replays bit-identical) |
| Timing | 100% | 6 / 3,000 | |
| Rush | 15% | 4 / 1,200 | light factory first, no repair-yard wait, no full-readiness opening; first 20 minutes |
| Flood | 20% | 3 / 1,500 | |

**Harness labels.**
- `default#<Mix>-<Tempo>` plays a fixed strategy and `default#random` plays the config's weights;
  `default#Learned-Base` is BASE.
- `default#<Mix>-<Tempo>-fixed` is the "strategy fixed" arm: the test-only style mix at 100%, which
  holds the ratios against the learning.
- `tests/ai/play-styles/*-fixed*.ini`, `*-swarm.ini` and `flood.ini` are the earlier fixed arms.

**Parity.** These games matched the reference row for row:
- 1.0.846 BASE and the `@swarm`/`@timing` styles replayed bit-identical through `#Learned-Base`,
  `#Learned-Swarm` and `#Learned-Timing` (`par-old`/`par-new`, 16 games against `s1c`).
- MacBook Pro and Air matched the mini, including QuadMass (`par2-*`).
- Windows matched apart from the known ±0.1 s `end_s` (`par-win`).

### 6.3 Experiments

All of these are CY starts on the six bundled maps, Hard, all three house pairs, both sides, against BASE.
Scores are from the strategy's side, with the play-style scoring rules (legacy; section 7 re-scores them)
and 95% bootstrap intervals; bold means the interval excludes 0.5. A house that cannot field a mix plays
Learned; those games are counted
under the strategy actually played. Data: `docs/formation-results/play-styles/strategy/<tag>/`.

**(a) Mix experiment.** Tempo is held at Base (control = Learned/Base = BASE). Seeds 10202 (mini),
10303 (PC) and 10404 (mini).

| Mix (prior + learning), Base tempo | Score v BASE |
|---|---|
| Artillery | 0.54 (0.44-0.63) n=108 |
| Specials | 0.53 (0.44-0.62) n=108 |
| Air (Atreides/Ordos) | 0.52 (0.38-0.67) n=48 |
| QuadMass | 0.50 (0.41-0.59) n=108 |
| TrikeSwarm (Atreides/Ordos) | 0.48 (0.37-0.59) n=72 |
| Counter | 0.46 (0.38-0.56) n=108 |
| Infantry | 0.45 (0.36-0.55) n=108 |
| TankMass | 0.42 (0.33-0.52) n=108 |

**(b) Tempo experiment.** The mix is held at Learned (control = Base tempo = BASE). Seeds 10202 and 10303
(Air) are pooled with the bit-identical play-style runs.

| Tempo, Learned mix | Score v BASE |
|---|---|
| Swarm | **0.57** (0.52-0.62) n=352 |
| Flood | 0.53 (0.44-0.62) n=108 |
| Rush | 0.49 (0.36-0.61) n=72 |
| Timing | 0.46 (0.40-0.53) n=240 |

**(c) Factorial: every mix x Swarm.** Seeds 10202 and 10303 (Pro). The third seed, `mC3-air` on the
Air, was not collected: the Air went offline.

| Mix | x Base | x Swarm | Swarm effect |
|---|---|---|---|
| Learned | 0.50 (BASE) | **0.57** | +0.07 |
| QuadMass | 0.50 | 0.54 (0.43-0.65) n=72 | +0.04 |
| Air | 0.52 | 0.52 (0.38-0.67) n=48 | 0 |
| Counter | 0.46 | 0.51 (0.40-0.62) n=72 | +0.05 |
| Artillery | 0.54 | 0.49 (0.38-0.61) n=72 | -0.05 |
| Infantry | 0.45 | 0.49 (0.36-0.60) n=72 | +0.04 |
| TrikeSwarm | 0.48 | 0.48 (0.33-0.62) n=48 | 0 |
| TankMass | 0.42 | 0.46 (0.35-0.57) n=72 | +0.04 |
| Specials | 0.53 | 0.44 (0.33-0.56) n=72 | -0.09 |

Main effects: tempo matters more than mix. Swarm adds about +0.04 to +0.07 to the cheap and mixed armies
(Learned, QuadMass, Counter, TankMass, Infantry). It costs the slow heavy priors (Specials -0.09,
Artillery -0.05): slow units in small early waves die piecemeal. No mix improves on Learned x Swarm, and
none of the interactions has an interval that excludes 0. QuadMass is the clearest pairing: level at Base,
0.54 with Swarm, and 0.61 (0.44-0.78) on Brutal.

**(d) Prior + learning v fixed v learned.** All scores are against BASE. The "fixed" arms hold the ratios
(the old styles at 70%, the new `-fixed` arms at 100%).

| Mix | Fixed, Base | Prior + learning, Base | Fixed, Swarm | Prior + learning, Swarm |
|---|---|---|---|---|
| QuadMass | 0.53 n=36 | 0.50 n=108 | 0.42 n=36 | 0.54 n=72 |
| TrikeSwarm | 0.39 n=36 | 0.48 n=72 | 0.50 n=36 | 0.48 n=48 |
| TankMass (armor) | **0.28** n=64 | 0.42 n=108 | 0.36 n=36 | 0.46 n=72 |
| Artillery (siege) | 0.46 n=158 | 0.54 n=108 | 0.53 n=36 | 0.49 n=72 |
| Specials (heavy) | **0.34** n=64 | 0.53 n=108 | 0.42 n=36 | 0.44 n=72 |
| Air | 0.42 n=64 | 0.52 n=48 | 0.42 n=36 | 0.52 n=48 |
| Counter (60% blend) | 0.45 n=64 | 0.46 n=108 | - | 0.51 n=72 |

The learning matters. Every fixed heavy composition lost: armor 0.28 and heavy 0.34, each with its
interval below 0.5. As priors with the learning on, the same ratios are level with BASE (0.42-0.54). The
earlier result that fixed compositions lose was mostly the mix being held, not the tempo; Swarm adds a
little to the fixed arms too (armor 0.28 -> 0.36, heavy 0.34 -> 0.42, siege 0.46 -> 0.53).

**(e) Brutal (top 3 + random), seed 11404, all houses (PC, mini):**
- QuadMass-Swarm 0.61 (0.44-0.78) n=36.
- Learned-Swarm 0.53 (0.40-0.67) n=60 (with `b1c-w`).
- Counter-Swarm 0.47 (0.31-0.64) n=36.
- The random bot 0.58 (0.42-0.75) n=36.

### 6.4 The random-strategy bot

**Weights and why.**
- **Tempo:** Swarm is the only tempo above BASE. Flood is level (0.53) and is kept at 20 for variety.
  Timing and Rush are left out.
- **Mix:** Learned is half the weight because Learned x Swarm is the best cell. The rest goes to the
  mixes level with it under Swarm: QuadMass 15 (best mix x Swarm, best Brutal), Counter 10, Artillery 10,
  Air 10 and TrikeSwarm 5 (Air and TrikeSwarm fall back to Learned for Harkonnen).
- **Left out:** TankMass and Infantry (below BASE at Base tempo) and Specials (0.44 with Swarm).
- **Expected score** from the cells: 0.54 against BASE.

Results, CY:

| Random bot (`default#random`) | Score | Notes |
|---|---|---|
| v BASE, Pro `rC-pro` | 0.51 (0.42-0.60) n=108 | 3 seeds, all houses: only 9 distinct (seed, house) picks; its 3 Learned-Swarm picks scored 0.33 |
| v BASE, Pro `rC2-pro` | 0.58 (0.50-0.66) n=144 | 12 seeds: 24 picks |
| **v BASE, pooled** | **0.55 (0.49-0.61) n=252** | Learned-Swarm alone: 0.57 (0.52-0.62) n=352 |
| v `none` (old rules) | **0.62** (0.53-0.71) n=108 | BASE v none 0.56 |
| Fortress v BASE (`rF`) | 0.47 (0.31-0.64) n=36 | |
| `/weak` v none | 0.46 n=36 | BASE `/weak` 0.42 |
| Brutal v BASE | 0.58 (0.42-0.75) n=36 | |

In the pooled games, the picks the random bot played scored: Air-Swarm 0.62 (24), Counter-Swarm 0.62 (24),
QuadMass-Swarm 0.57 (30), Learned-Flood 0.56 (54), Learned-Swarm 0.44 (90).

**Decision (the stated default rule, legacy scoring; section 7.3 re-scores it and the decision stands).**
The random bot's pooled interval reaches 0.49, so it does not
qualify as the default. The single winner does:
- **Learned-Swarm on CY:** 0.57 (0.52-0.62), n=352.
- **Fortress:** 0.58 (0.42-0.75) over 36 games on this branch (`sF-pro`); pooled with the 72 earlier
  games, 0.55 (0.45-0.64), n=108.
- **Brutal:** 0.53 (0.40-0.67), n=60.
- **`/weak`:** 0.43 (0.28-0.58) against `none`, n=36, so not worse than BASE `/weak` (0.42).
- **`quantbot_all_against_atreides`:** passes.

So the default is `Mode=Fixed:Learned,Swarm`. `Mode=Random` keeps the measured weights for anyone who
wants the AI to vary. The tradeoff is about 0.02 of expected score against BASE (0.55 v 0.57), not
significant at these sample sizes. Every pick in the random pool is level with or above BASE, so it is a
safe "more varied" setting, and it was the stronger bot against the old rules (0.62).

**Not done** (this round's request budget):
- **Round-robin among the new strategies:** none was run. The counter structure therefore rests on the
  play-style round-robin (6.1), which has no cycle.
- **Mid-game strategy switch:** not built, because no cycle was found to exploit.
- **mC3-air:** the third factorial seed on the Air was not collected (the Air was offline).
- **4-player FFA:** `--ffa default#random,default#Learned-Base,default#Learned-Swarm,none` on Four Chambers
  and Equilibrium (`rX-pro` on the Pro). Only 8 of 16 games had finished when this was written; they are
  not analysed. The first mini attempt failed: duplicate labels in `--ffa` collide in the run directory.

### 6.5 Recommendations

- **Default:** learned mix, Swarm tempo. Players get a full-strength first attack, then frequent smaller
  waves (35% readiness) and a mix that adapts to theirs.
- **For variety, set `Mode=Random`.** Each house then plays its own mix/tempo per game, the same on every
  peer. Examples: quad swarms, artillery behind tanks, ornithopter wings for Atreides/Ordos, or a mix
  built to counter the army it has seen.
- **Mixes stay priors.** A fixed composition loses to the learning; keep `holdPercent` at 100 unless a
  mix measures better held.
- **Avoid Specials with Swarm, TankMass, Infantry and the Timing tempo.**

## 7. Scoring and the default rule (branch `test/match-scoring`)

Stefan: "the AIs have to attack. they can't just send a small raiding party out for eternity. if that is
done then the games will all end in draws. you need to make a draw negative. as in not as much as a loss
but still negative score. also on maps with more than one player a win needs to count for more, e.g. if
you come first big score, second smaller, etc. e.g. if 8 players 4 get positive scores most for the
winner, 4 get negative."

### 7.1 The placement score

One function scores every report (`tests/ai/match_score.py`, `score_game`). `formation-duel.py --report`,
`vanilla-match.py --report` (1v1 and `--ffa`) and `play-style-matrix.py` all use it.

- **Score of place p among N entrants:** $s(p) = 1 - 2(p-1)/(N-1)$. First is +1 and last is -1, linear in
  between. A 1v1 win is +1 and a loss -1. With 8 players the scores are +1, +0.71, +0.43, +0.14, -0.14,
  -0.43, -0.71, -1: the top four are positive and the bottom four negative.
- **Places:** the last entrant standing is first. Eliminated entrants are ranked by elimination time,
  later being better. Entrants eliminated at the same time share the mean of their places.
- **Time-outs are draws, and draws are negative.** When k ≥ 2 entrants are alive at the time limit,
  nobody wins. The survivors share places 1..k. Each scores the mean of those places' scores minus the
  draw penalty $D = 0.25$. The result is clamped between the best eliminated entrant's score (a loss when
  nobody was eliminated) and +1. Examples:
  - A 1v1 draw is $0 - 0.25 = -0.25$, worse than nothing and better than a loss.
  - 4P with two survivors: each scores $(1 + 0.33)/2 - 0.25 = +0.42$; the eliminated houses score -0.33
    and -1.
  - 4P with all four alive: each scores -0.25.
- `--draw-score` sets the 1v1 draw value (default -0.25; must be above -1 and below 0).
- **No kill-value adjudication.** A time-out is no longer decided on killed unit value. The value margin
  is still reported (Value margin / game).
- **Teams:** the entrants are teams. A team is eliminated when all its houses are, at the last house's
  elimination time, and every house of a team gets the team's score. (No harness plays team games
  today: vanilla-match puts every house on its own team and the duels are 1v1.)
- **Per game:** `--scores FILE` on any of the three reports writes every game's sides with eliminated,
  place, score, drawn (alive at a time-out), game length (`end_s`) and decisive (ended before the limit).
  The CSVs the harnesses record are unchanged.

### 7.2 The default rule

**A candidate beats the control when the 95% bootstrap interval of the mean paired score difference
(candidate minus control) is wholly above 0.** It loses when the interval is wholly below 0, and is level
otherwise. The bootstrap uses 2,000 resamples with a fixed seed.
- Head-to-head games (candidate v control, 1v1 or FFA): the difference is taken within each game. In a
  1v1 it is +2 for a win, -2 for a loss and 0 for a draw, so the rule is "significantly more eliminations
  won than lost".
- Against a common opponent (e.g. candidate v old rules and control v old rules): `--baseline CONTROL` on
  `formation-duel.py` and `vanilla-match.py` pairs the two sides by setup (difficulty, map/size, seed,
  own house, opponent's house).
- Every comparison reports the time-out rate of each arm. A candidate that only wins by drawing less
  than the control still shows up as better. That is intended.

The other conditions of a default change stay as they were: not worse on Fortress, Brutal and `/weak`, and
`quantbot_all_against_atreides` passes. `--scoring legacy` reprints any report with the old rule for
comparing with old write-ups. It reproduces the published numbers, e.g. the play-style screen and round
5's +0.120 (+0.03..+0.21).

Unit tests: `python3 -m unittest tests.test_match_score` covers 1v1 win/loss/draw, the 4P and 8P values,
ties, time-outs with k survivors and the clamp, teams, the legacy score and the decision rule.

### 7.3 Earlier results re-scored

Each row gives the old score and its interval (verdict against 0.5), then the new rule: the candidate's
mean placement score and the paired difference Δ with its interval (verdict against 0). Commands are in
7.4.

**The 1.0.847 default (Learned/Swarm v 1.0.846 = BASE), with the random bot.** Data: the play-style dirs
(`default@swarm` = `default#Learned-Swarm`, bit-identical) and `play-styles/strategy/`.

| Comparison | n | Old | New: score, Δ (95% CI) | Time-outs (each arm) |
|---|---|---|---|---|
| Swarm v BASE, CY Hard (screen, round-robin, `c1c-air`, `mB*`) | 356 | 0.567 (0.51-0.62) **beats** | +0.142, Δ +0.29 (+0.08..+0.50) **beats** | 2% / 2% |
| Swarm v BASE, Fortress Hard (`s1f*`, `r1f`, `sF`) | 108 | 0.546 (0.46-0.63) level | -0.028, Δ +0.11 (-0.17..+0.41) level | 33% / 33% |
| Swarm v BASE, Brutal CY (`b1c-w`, `sB`) | 60 | 0.533 (0.40-0.67) level | +0.058, Δ +0.13 (-0.37..+0.63) level | 3% / 3% |
| Swarm `/weak` v none, paired with BASE `/weak` v none (`sF`, `rF`) | 36 | +0.014 (-0.11..+0.14) level | -0.375 v -0.375, Δ 0.00 (-0.14..+0.15) level | 39% / 39% |
| BASE `/weak` v none (`rF`) | 36 | 0.417 (0.28-0.58) level | -0.375, Δ -0.56 (-1.00..-0.11) **loses** | 39% |
| Random v BASE, CY (`rC`, `rC2`) | 252 | 0.552 (0.49-0.61) level | +0.098, Δ +0.20 (-0.05..+0.44) level | 0% / 0% |
| Random v none, paired with BASE v none (`rC`) | 108 | +0.065 (-0.04..+0.17) level | +0.229 v +0.100, Δ +0.13 (-0.07..+0.33) level | 1% / 1% |
| Random v BASE, Fortress (`rF`) | 36 | 0.472 (0.31-0.64) level | -0.076, Δ +0.06 (-0.44..+0.56) level | 42% / 42% |
| Random v BASE, Brutal (`rB`) | 36 | 0.583 (0.42-0.75) level | +0.188, Δ +0.39 (-0.22..+1.00) level | 3% / 3% |
| Random `/weak` v none, paired with BASE `/weak` | 36 | +0.042 (-0.11..+0.19) level | Δ +0.02 (-0.19..+0.26) level | 42% / 39% |

**Does Swarm still win once its time-outs count as negative draws? Yes. The 1.0.847 default stands.**
- Swarm v BASE time-outs are almost all Fortress duels: 36 of 108, and 2% on CY starts.
- Swarm does not draw more than other styles. In the Fortress round-robin the time-out rate is 31% for
  Swarm's row against 28-38% for the others (BASE 34%).
- In a head-to-head game the draw penalty hits both sides equally. It moves both arms' scores down (Swarm
  Fortress -0.03, BASE -0.14) but cannot change a head-to-head verdict; only removing the kill-value
  adjudication can.
- On CY, Swarm beats BASE: Δ +0.29 (+0.08..+0.50). On Fortress and Brutal it is level, so not worse.
  Against the old rules from `/weak` it equals BASE.
- The random bot stays level with BASE.
- None of the mix and tempo cells of 6.3 (`mA*`, `mB*`, `mC*`, counted by the strategy each house
  played, `--by-strategy`) changes its verdict: all are level under both rules. Learned-Timing is the
  lowest at Δ -0.33 (-0.78..+0.11).

**Play-style screen (every style v BASE, 3).**
- CY verdicts are unchanged: heavy and armor lose, every other style is level.
- Fortress:
  - raid and turtle lose under both rules (raid Δ -1.39 (-1.72..-1.06), turtle Δ -1.22).
  - **boom now loses** (old 0.333 (0.19-0.50) level; new Δ -0.78 (-1.28..-0.22)). The old rule gave boom
    its time-outs on kill value.
  - timing is at the edge: Δ -0.56 (-1.06..+0.00).

**Round-robin (3, 6.1).**
- CY (108 games per cell): the same significant cells under both rules. Swarm beats rush (Δ +0.41) and
  boom (+0.44). BASE v Swarm is at the edge (Δ -0.39, -0.76..+0.00). Row means: swarm +0.18, BASE +0.03,
  timing +0.01, boom -0.07, siege -0.08, rush -0.09.
- Fortress (`r1f`, 36 per cell): many more cells separate.
  - Old: only swarm and rush over timing were significant.
  - New: BASE, swarm, rush and siege each beat boom (Δ +0.56 to +0.89). swarm, rush and siege beat
    timing.
  - Row means: swarm +0.14, rush +0.12, BASE +0.03, siege -0.05, timing -0.35, boom -0.38. That is the
    old order, apart from boom and timing swapping at the bottom.
  - The waiting styles (boom, timing) were rescued by adjudicated time-outs.

**Force concentration, rounds 4 and 5 (`docs/formations.md`; `docs/formation-results/force/`).**

| Comparison | n | Old | New: score, Δ (95% CI) | Time-outs |
|---|---|---|---|---|
| BWFCQ v none, Hard (`r4f`) | 108 | 0.870 (0.81-0.93) beats | +0.667, Δ +1.44 (+1.24..+1.63) beats | 22% |
| BWFCQ v FCQ, Hard (`r4f`) | 108 | 0.676 (0.58-0.76) beats | +0.306, Δ +0.78 (+0.50..+1.04) beats | 33% |
| BWFCQ v none, Brutal (`r4fb`) | 36 | 0.931 (0.85-1.00) beats | +0.722, Δ +1.56 (+1.28..+1.83) beats | 22% |
| BWFCQD `/weak` paired with BWFCQ `/weak`, v none (`r4d`) | 36 | +0.056 (-0.06..+0.17) level | Δ -0.06 (-0.26..+0.12) level | 33% / 42% |
| **UVX `/weak` paired with BWFCQ `/weak`, v none (`c2`)** | 108 | **+0.120 (+0.03..+0.21) beats** | -0.417 v -0.472, **Δ +0.06 (-0.09..+0.20) level** | 33% / 33% |
| UVX `/weak`, the same with the 36-game pilot (`c2`, `p6`) | 144 | +0.132 (+0.06..+0.21) beats | Δ +0.08 (-0.03..+0.21) level | 33% / 33% |
| A (k100) / A k120 `/weak` paired with BWFCQ `/weak` (`p5`) | 36 | -0.208 / -0.167 loses | Δ -0.16 (-0.37..+0.03) / -0.18 (-0.39..+0.01) level | 42% / 39% v 33% |
| UVX v none, Hard (`c2`) | 108 | 0.926 (0.87-0.97) beats | +0.720, Δ +1.54 (+1.37..+1.70) beats | 19% |
| UVX v none, Brutal (`c3b`) | 36 | 0.944 (0.86-1.00) beats | +0.701, Δ +1.50 (+1.17..+1.83) beats | 19% |
| UVX v BWFCQ, Hard (`c3h`) | 108 | 0.528 (0.44-0.62) level | -0.053, Δ +0.06 (-0.26..+0.35) level | 32% |

**Which earlier decisions change.**
- **Round 5's UVX default would not pass the new rule.** It was adopted on the `/weak` paired difference
  of +0.120 (+0.03..+0.21). Re-scored it is +0.06 (-0.09..+0.20), even with the pilot games.
  - UVX `/weak` won 18 and lost 54 eliminations against BWFCQ `/weak`'s 15-57, with 36 time-outs each.
  - Most of its old margin came from the adjudication handing it the time-outs it led on killed value
    (+19.7k value margin per game).
  - UVX is not worse anywhere (attack v none +1.54, v BWFCQ level). Keeping or reverting it is a new
    decision for Stefan; nothing in the game was changed here.
- **Round 4 (BWFCQ default) stands.** It beats none and FCQ by a wide margin under both rules.
- **The 1.0.847 Learned/Swarm default stands** (above).
- **The `/weak` absolute scores flip to "loses to none"** (BASE `/weak` -0.56, BWFCQ `/weak` -0.78). The
  old 0.42/0.31 hid how often the weak side drew. No decision used the absolute `/weak` score; only "not
  worse than BASE `/weak`" was used, and that still holds.
- **4-player FFA, BWFCQ v none** (`scenarios/ffa`, 72 games, 2+2 houses):
  - Old place score 0.541 (0.50-0.58).
  - New: BWFCQ +0.006 v none -0.152, Δ +0.16 (-0.01..+0.33), level.
  - 24 of 72 games timed out (Habbanya-Archer almost always). Houses alive at a time-out: BWFCQ 30%,
    none 28%.
  - Both arms score near or below 0 because a time-out makes every survivor lose 0.25.
- **4-player FFA, `rX-pro`** (8 games, one house each): none loses to Learned-Base, Δ -0.75
  (-1.25..-0.33). Learned-Swarm (Δ -0.42) and the random bot (Δ -0.50) are level with Learned-Base in so
  few games.

### 7.4 Commands

```bash
cd docs/formation-results/play-styles
S=../../../tests/ai/play-style-matrix.py   # add --scoring legacy for the old numbers
python3 $S --duel s1f s1f-b s1f-pro --vanilla s1c s1c-pro s1c-pro2 s1c-win s1c-win2 s2c-m1 s2c-m2 s2c-w1 s2c-w2 --screen default
python3 $S --vanilla r1c-m r1c-m2 r1c-pro r1c-w1 r1c-w2 --matrix default swarm timing rush boom siege
python3 $S --duel r1f --matrix default swarm timing rush boom siege
python3 $S --vanilla s1c s1c-pro s1c-pro2 s1c-win s1c-win2 s2c-m1 s2c-m2 s2c-w1 s2c-w2 r1c-m r1c-m2 r1c-pro r1c-w1 r1c-w2 \
  c1c-air strategy/mB1-air strategy/mB2-air --duel s1f s1f-b s1f-pro r1f strategy/sF-pro --difficulty hard \
  --same default#Learned-Swarm=default@swarm default#Learned-Base=default --screen default
python3 $S --vanilla b1c-w strategy/sB-win --difficulty brutal --same default#Learned-Swarm=default@swarm default#Learned-Base=default --screen default
python3 $S --vanilla strategy/mA1 strategy/mA2-win strategy/mA3 strategy/mB1-air strategy/mB2-air strategy/mC1-pro strategy/mC2-pro \
  --difficulty hard --by-strategy --screen default#Learned-Base
python3 $S --vanilla strategy/rC-pro strategy/rC2-pro --screen default#Learned-Base
python3 ../../../tests/ai/vanilla-match.py --report --results strategy/rC-pro --tags rC \
  --pairs "default#random:default#Learned-Base,default#random:none,default#Learned-Base:none" --baseline default#Learned-Base
# Fortress /weak: rF and sF-pro concatenated into one games.csv (same header), then
python3 ../../../tests/ai/formation-duel.py --report --results MERGED --tag rF,sF --seeds 12101 12202 12303 12404 12505 12606 \
  --pairs "default#Learned-Base/weak:none,default#Learned-Swarm/weak:none,default#random/weak:none" --baseline default#Learned-Base/weak
cd ../force
python3 ../../../tests/ai/formation-duel.py --report --results round5 --tag c1,c2 --pairs "none+BWFCQ/weak:none,none+BWFCQUVX/weak:none" \
  --baseline none+BWFCQ/weak --seeds 7101 7202 7303 7404 7505 7606 --sizes 128 160 192
python3 ../../../tests/ai/vanilla-match.py --report --results ../scenarios/ffa --tags cy1 --ffa none+BWFCQ,none
```

## 8. Starting unit ratios: bounds, randomised, control and tuned arms (protocol 83, save 9866/9868)

Stefan: "i want randomised runs, control runs and new mixes you tune. for example you can look at ratios
at the end of the game and see what has worked and tune that way. don't allow any one unit to have over
50% at start, and not under 2% so we always have a chance to introduce a variety of units." The in-game
learning stays as it is. Branch `feat/ai-ratio-tuning` (from staging 7eaa52ab = local 1.0.847).

### 8.1 The bounds rule

- **Unit types that count:** the unit-mix slots QuantBot's production allocates - tank, siege tank,
  launcher, Devastator, Sonic Tank, Deviator, ornithopter, trike, raider trike, quad. Since save 9868
  (8.9) each special has its own slot, share and learning; before, the three shared one "special" slot
  and the tables of 8.3-8.6 use that eight-slot layout (one special column = the house's one special).
  Infantry is not part of the mix: it has its own difficulty quota (10-12% of the army value, 35% for
  the Infantry strategy) and is not learned, so it is outside the bounds.
- **Rule:** every starting mix - the configured `[Unit Ratios]` opening (Learned), the presets, Counter,
  Tuned and Bounded - is bounded before the learning sees it (`QuantBotStrategy::bound`, in
  `QuantBot::startingPrior`): over the slots the house can produce *in this game* (ObjectData and the
  game's tech level, `fieldableMixSlots`) each slot gets 2%-50%; slots it cannot produce get 0 and the rest
  is renormalised within the bounds. Precisely x_i = clamp(t * w_i, 2%, 50%) with the one scale t that sums
  to 100% (integer basis points, rounding remainder to the largest fractions). A 0 becomes 2%; one
  producible slot gets 100% (the cap cannot hold). Each production pass then plays that bounded mix over
  the slots producible at that pass (factories standing), renormalised: a factory not yet built does not
  re-shape the starting mix. Learning may move away from the bounds during the game.
- **Why per game, not per pass:** the first version bounded the mix again over the slots producible at
  each pass (e.g. 50% cap with only a light factory and a heavy factory up). It failed
  `quantbot_all_against_atreides` on seed 41562270 (no house eliminated in 41 minutes, 5/6); bounding over
  the game's producible slots passes 6/6. **The randomised and bounds-control games in 8.3-8.4 were played
  with that first (per-pass) version**; the starting mix over all producible slots is the same in both, they
  differ only while factories are still missing. Round-1 tuned games were stopped for the same reason.
- **Configured ratios:** Atreides Tank 0.05 stays about 5% (4.8% of the heavy share at tech 8); the
  opening's light units (4% at tech 7+, quads twice trikes) rise to 2% each; Harkonnen's launcher 0.50 x
  0.96 stays 48%. `config/QuantBot Config.ini.default` is unchanged (it is in the mod checksum).
- **Presets** are the 1.0.847 presets clamped and renormalised (pinned in the unit tests):

| Mix | 1.0.847 (tank/siege/launcher/special/orni/trike/raider/quad) | Now |
|---|---|---|
| QuadMass | 20/0/0/0/0/0/10/70 | 26.67/2/2/2/2/2/13.33/50 |
| TrikeSwarm | 10/0/0/0/0/45/45/0 | 9/2/2/2/2/40.5/40.5/2 |
| TankMass | 70/15/15/0/0/0/0/0 | 50/20/20/2/2/2/2/2 |
| Artillery | 20/45/35/0/0/0/0/0 | 18/40.5/31.5/2/2/2/2/2 |
| Specials | 25/15/10/50/0/0/0/0 | 23/13.8/9.2/46/2/2/2/2 |
| Air | 15/10/15/0/60/0/0/0 | 15.75/10.5/15.75/2/50/2/2/2 |

### 8.2 New mixes and keys

- `Tuned`: the per-house `[Unit Ratios Tuned]` ratios, `<House> = t,s,l,dv,so,de,o,tr,r,q` (percent: tank,
  siege, launcher, Devastator, Sonic Tank, Deviator, ornithopter, trike, raider trike, quad; the house's
  top tech band, and every band without its own key) and `<House>_Tech<N> = ...` or `= Opening` for the
  tech band from level N (8.9); optional keys, never written by `save()`, hashed. Code defaults: the top
  mixes of 8.5 for Harkonnen, Atreides, Ordos (8.8); no band keys yet (8.10).
- `Bounded`: a random starting mix within the bounds each game - uniform over the bounded mixes of the
  slots the house can produce (sorted uniform cut points = flat Dirichlet, redrawn until within the
  bounds, at most 64 times, then `bound()`), a pure SplitMix64 function of game seed + house + salt.
  Usable as `Mode=Fixed:Bounded,<Tempo>` or in `MixWeights` (`Mode=Random` with `MixWeights=Bounded:1`
  is "random within bounds" as a production option; no separate `MixRandom` key is needed).
- Tuned/Bounded resolve their ratios once at the pick and save them (SAVEGAMEVERSION 9866: eight 16-bit
  basis points after the strategy; 9868: ten, one per special - a 9866/9867 save loads its eight with the
  special share split between the house's specials). A 9865 save loads with its strategy and no resolved
  mix (none of its mixes needs one); a 9864 save picks afresh, the same mix. Protocol 83.
- Test-only `[Play Style]` keys: `Learning=0` (the learning-off arm; style `nolearn`) and
  `StartBounds=0` (the unbounded 1.0.847 opening; style `unbounded`).
- Harness: `houses.csv` has `start_mix` (from the `QUANTBOT_STRATEGY:` log line); `mix.csv` has, per house,
  value built / lost / standing and combat reward per slot (+ infantry) at 10, 20, 30 minutes and the end
  (`UNIT_MIX_SAMPLE:`); `--seed-per-map STRIDE` gives every map its own seed so the Bounded draw differs
  by map. Analysis: `tests/ai/ratio-analysis.py`.

### 8.3 Scoring and setup

All arms: CY starts on the six bundled maps (habbanya-penny, ergun-sahij, tuono-soolin, sihaya-pip,
x-factor, sihaya-lauper), Hard unless stated, all three house pairs, both sides (36 games per seed),
`--seed-per-map 1`. Control = `default#Learned-Swarm` (the 1.0.847 default with the bounded configured
opening). Score = the placement score of section 7 (win +1, loss -1, a time-out -0.25 each; no kill-value
adjudication); **Δ** = mean paired difference candidate - control per game, 95% bootstrap interval; bold =
the interval excludes 0 (the default rule). House columns are Δ with the candidate playing that house;
they include house strength (Ordos is strong, Harkonnen weak; compare columns across arms, not with 0).
Runners: mini, MacBook Pro, Windows PC; the Pro and the PC were synced and matched the mini's parity set
(`par`, 12 games: Pro identical, PC identical except the known ±0.1 s timing fields).
Data: `docs/formation-results/play-styles/ratios/<arm>/` (games + houses; `fF`/`gF` Fortress games only);
report with `python3 tests/ai/ratio-analysis.py DIR... --arms A:B` (Fortress: `formation-duel.py --report`).

### 8.4 Randomised v control, re-scored (3,094 games)

| Arm | Δ v control | Atreides / Harkonnen / Ordos | W-L / time-outs |
|---|---|---|---|
| Randomised (`Bounded-Swarm`; `rR`) | **+0.087** (+0.02..+0.16) n=3,094 | +0.08 / -0.29 / +0.47 | 1,604-1,470 / 20 (0.6%) |
| Bounded control v 1.0.847 unbounded opening (`cU`) | +0.000 (-0.37..+0.37) n=108 | -0.22 / -0.11 / +0.33 | 54-54 / 0 |

Legacy score of the same games: 0.520 (0.50-0.54). The randomised arm still edges the configured ratios;
the bounds themselves cost nothing. (These games used the superseded per-pass bounds, 8.1.)

**What worked** (randomised arm; derived with the legacy score, unchanged by re-scoring - 20 time-outs):

- **The starting mix matters little once the learning runs.** Ridge-logistic coefficients of the result on
  the starting shares are within about 2 standard errors of 0: Atreides siege +1.10±0.58, special
  -0.61±0.57; Harkonnen quad +0.67±0.52; Ordos ornithopter +1.04±0.59, quad +0.66. +10 points of a slot
  moves the result by at most ~3 points.
- **Winners and losers end with the same composition** (built value, tank/siege/launcher/special/orni/
  trike/raider/quad): Atreides 11/12/17/16/17/13/-/14 v 9/13/18/14/18/13/-/14; Harkonnen
  13/17/21/11/-/-/-/25 v 12/18/22/12/-/-/-/23; Ordos 12/15/14/11/18/-/12/14 v 11/14/16/12/17/-/11/13.
- **Cost-effectiveness** (combat reward per value built): siege tanks best for every house (1.8-2.2), then
  the Sonic Tank (Atreides, 2.4) and launchers (Ordos, 1.75).

### 8.5 Tuned mixes: round 1, round 2 and the final per-house mix

Percent: tank, siege, launcher, special, ornithopter, trike, raider trike, quad (0 = the house cannot
field it; every mix is bounded 2%-50% over the fieldable slots at game start).

| Candidate (style) | Atreides | Harkonnen | Ordos |
|---|---|---|---|
| r1 regression | 10,39,11,7,10,9,0,14 | 10,13,10,23,0,0,0,44 | 9,9,7,13,35,0,7,20 |
| r1 gradient | 13,26,15,6,13,10,0,17 | 14,16,15,22,0,0,0,33 | 12,11,6,17,27,0,5,22 |
| r1 winners' end | 11,12,17,16,17,13,0,14 | 15,19,24,13,0,0,0,29 | 13,15,15,11,19,0,13,14 |
| r1 cost | 10,18,15,23,12,10,0,12 | 16,28,19,19,0,0,0,18 | 11,20,16,14,12,0,12,15 |
| r2 best (per house the best r1 effect) | 11,12,17,16,17,13,0,14 | 14,16,15,22,0,0,0,33 | 9,9,7,13,35,0,7,20 |
| r2 siege (r2 best +8 siege; Atreides +6 Sonic) | 9,20,13,22,13,10,0,13 | 13,24,14,19,0,0,0,30 | 9,17,7,13,27,0,7,20 |
| **Tuned code defaults** (per house the better r2 mix) | **9,20,13,22,13,10,0,13** | **14,16,15,22,0,0,0,33** | **9,17,7,13,27,0,7,20** |
| configured opening, bounded (control, for reference) | 4.8,9.5,33.4,33.4,14.3,2,0,2.7 | 16.3,16.3,48,15.4,0,0,0,4 | 23.4,23.4,2,23.4,23.4,0,2,2.6 |

| Arm (v control, Swarm) | Δ | Atreides / Harkonnen / Ordos | W-L / time-outs |
|---|---|---|---|
| r1 regression (`r1`) | +0.194 (-0.12..+0.51) n=144 | +0.58 / -0.38 / +0.38 | 78-64 / 2 (1.4%) |
| r1 gradient | +0.264 (-0.06..+0.58) n=144 | +0.42 / +0.00 / +0.38 | 81-62 / 1 (0.7%) |
| r1 winners' end | +0.236 (-0.08..+0.57) n=144 | +0.88 / -0.33 / +0.17 | 80-63 / 1 (0.7%) |
| r1 cost | +0.083 (-0.25..+0.42) n=144 | +0.17 / -0.25 / +0.33 | 75-69 / 0 |
| r2 best (`r2`) | +0.269 (-0.01..+0.53) n=216 | +0.17 / -0.08 / +0.72 | 122-93 / 1 (0.5%) |
| r2 siege | +0.241 (-0.02..+0.50) n=216 | -0.17 / -0.11 / +1.00 | 121-95 / 0 |
| Tuned defaults, `default#Tuned-Swarm` (`g`, fresh seeds) | +0.194 (-0.07..+0.45) n=216 | -0.14 / +0.08 / +0.64 | 116-95 / 5 (2.3%) |
| Tuned defaults, settle run (`h1`, seeds 68000-68060, cluster) | **+0.341** (+0.09..+0.57) n=252 | +0.29 / -0.12 / +0.86 | 146-103 / 3 (1.2%) |
| **Tuned defaults, `g` + `h1`** | **+0.274** (+0.09..+0.45) n=468 | +0.09 / -0.03 / +0.76 | 262-198 / 8 (1.7%) |

Every tuned mix is ahead of the configured ratios. The per-house pick did not clear the rule on its first 216
fresh games (+0.19), and does over 468 (+0.27, interval above 0). Harkonnen and Ordos gain most (their columns sit 0.3-0.5 above the
randomised arm's), Atreides least. **Atreides:** the r2 best mix (winners' end composition) failed
`quantbot_all_against_atreides` (seed 1430976924 won at 40.7 min, 5/6); the siege/Sonic mix passes 6/6
(29.9-37.5 min), so it is the Atreides default. End-of-game composition (built value, Tuned v control,
`g`): Atreides 10/13/14/14/18/14/-/16 v 9/12/20/16/19/11/-/13; Harkonnen 11/15/16/12/-/-/-/32 v
11/18/24/13/-/-/-/21; Ordos 12/13/13/10/20/-/9/18 v 16/15/12/12/21/-/8/11 - the learning pulls both
towards the same mix, the tuned start leaves more quads (Harkonnen, Ordos) and fewer launchers.

### 8.6 The tuned mix elsewhere, the learning, and random-within-bounds

| Arm | Δ (95% CI) n | W-L / time-outs |
|---|---|---|
| Tuned defaults, Base tempo: `#Tuned-Base` v `#Learned-Base` (`g`) | +0.296 (-0.07..+0.67) n=108 | 62-46 / 0 |
| r2 best, Base tempo (`f1`) | **+0.407** (+0.04..+0.78) n=108 | 65-43 / 0 |
| Tuned defaults, Brutal (`gB`) | **+0.481** (+0.11..+0.85) n=108 | 67-41 / 0 |
| r2 best, Brutal (`fB`) | +0.222 (-0.15..+0.59) n=108 | 60-48 / 0 |
| Tuned defaults, Fortress 128/160/192, seeds 12101-12606 (`gF`) | -0.056 (-0.56..+0.50) n=36 | 12-13 / 11 (31%) |
| r2 best, Fortress (`fF`) | +0.056 (-0.50..+0.56) n=36 | 12-11 / 13 (36%) |
| Tuned `/weak` v none, paired with Learned `/weak` v none (`gF`) | +0.000 (-0.18..+0.18) n=36 | 36% / 36% |
| r2 best `/weak`, paired the same (`fF`) | -0.111 (-0.32..+0.07) n=36 | 44% / 44% |
| all-against-Atreides (Tuned defaults as the default, 9c0e2afa) | 6/6, 29.9-37.5 min | - |
| Learning on v off, r2 best (`f1`) | +0.028 (-0.24..+0.29) n=216 | 109-106 / 1 |
| Learning on v off, random start (`#Bounded-Swarm` v `@nolearn#Bounded-Swarm`, `g`) | +0.074 (-0.30..+0.44) n=108 | 56-52 / 0 |
| Learning on v off, Tuned defaults (`gL` + `h2`) | +0.000 (-0.27..+0.27) n=216 | 107-107 / 2 |
| **Random within bounds** (`Mode=Random`, `MixWeights=Bounded:1`: `default@mixbounded#random`, `pB`) | +0.185 (-0.19..+0.56) n=108 | 59-49 / 0 |

- **Not worse anywhere:** Base tempo, Brutal (beats), Fortress and `/weak` level.
- **The learning does not measurably add value on CY 1v1** - not from a tuned start, not from a random one
  (540 games, all level). Its measured value is the earlier result (6.3 d): held fixed, heavy compositions
  lost (armor 0.28, heavy 0.34) where the same ratios as priors with learning were level. It is kept (Stefan's
  rule; it protects against bad starting mixes), but this round cannot show a gain over a good start.
- **Random within bounds** (the pick: 98 Bounded-Swarm, 10 Bounded-Flood) is level and ahead in sign, like the
  3,094-game randomised arm (+0.087, beats): fine as a variety option.

### 8.7 Decision

- **Default: `Fixed:Tuned,Swarm`** - the measured per-house ratios (8.5), bounded like every starting mix
  (2%-50%). Tuned v the bounded configured ratios +0.27 (+0.09..+0.45), n=468; not worse on Base tempo,
  Fortress, `/weak`; beats on Brutal; `quantbot_all_against_atreides` 6/6. Fremen, Sardaukar and Mercenary
  have no tuned top mix (none beat the control, 8.10): under Tuned they play their configured opening, bounded.
- **Learned** (the configured ratios, bounded) stays available: `Mode=Fixed:Learned,Swarm`.
- **Variety:** `Mode=Random` with `MixWeights=Bounded:1` (not worse than the control).

### 8.8 Specials fix and one slot per special (save 9868)

- **Bug (fixed, 4c3ee80b):** the capital/Starport production path gave each buildable special the whole
  special share as its own target, so a house fielding several specials could build up to that many times
  the share. Fix: the share is split between the buildable specials (`UnitMixPolicy::splitShare`; unit test
  `[specials]`).
- **Layout (32adfb8a):** ten mix slots (tank, siege, launcher, Devastator, Sonic Tank, Deviator,
  ornithopter, trike, raider trike, quad). Each special has its own share, learning and heavy-factory
  candidate (`SpecialUnitPolicy` removed). The group form - `[Unit Ratios]` Special, the presets, the
  counter prior, the test play-style `Mix=` - keeps one special share, split between the house's specials
  (`expandGroups`); bounds apply per buildable slot. Each special keeps the group's prior price (700), so a
  one-special house (Harkonnen Devastator, Atreides Sonic Tank, Ordos Deviator) plays exactly as before:
  `quantbot_strategy` reproduces every lead-share value of 8.5-8.6 (e.g. Tuned 3196 v 2467, TankMass 3456 v
  2467) and the Bounded draw keeps its salts for houses with up to nine slots. The tuned top mixes keep
  their values on the house's special (Harkonnen 14,16,15,22,0,0,0,0,0,33; Atreides 9,20,13,0,22,0,13,10,0,13;
  Ordos 9,17,7,0,0,13,27,0,7,20). Captured specials of another kind now count in their own (unfieldable)
  slot, not in the house's special.
- **Save 9868:** the resolved Tuned/Bounded ratios hold ten slots; 9866/9867 saves load their eight with the
  special share split. The learning window keeps its eight-slot serialized form (every pass replaces it
  with the match totals before it is read). Protocol stays 83 (unreleased).
- **Which specials:** in vanilla ObjectData Fremen, Sardaukar and Mercenary build Devastator and Sonic
  Tank (Deviator is Ordos-only: `Builder(F/S/M) = Invalid`); in Tornie Sardaukar has Devastator+Sonic,
  Mercenary Devastator+Deviator, Fremen none. No vanilla house builds all three; the code handles it.

### 8.9 Tech bands

`QuantBot::fieldableMixSlots(tech)` now follows the builder and prerequisite chain (House IX at 7 gates the
specials and the ornithopter); `QuantBot::techBand()` is the first level with the same fieldable set.
`tests/ai/tech-bands.py` derives the bands from ObjectData (vanilla `config/ObjectData.ini.default`;
`--object-data mods/Tornie/ObjectData.ini` for the mod: there Ordos gains siege at 6, Sardaukar lights at 2).
Vanilla:

| House | 1 | 2 | 3 | 4 | 5 | 6 | 7-9 (top) |
|---|---|---|---|---|---|---|---|
| Harkonnen | - (1-2) | | quad | tank, quad | +launcher | +siege | +Devastator |
| Atreides | - | trike | trike, quad | +tank | +launcher | +siege | +Sonic, ornithopter |
| Ordos | - | raider | raider, quad | +tank | +launcher (5-6) | | +siege, Deviator, ornithopter |
| Fremen, Mercenary | - | trike, raider | +quad | +tank | +launcher | +siege | +Devastator, Sonic, ornithopter |
| Sardaukar | - (1-2) | | trike, raider, quad | +tank | +launcher | +siege | +Devastator, Sonic, ornithopter |

A band with one fieldable slot plays it alone; with two the bounds force 50/50. Lookup at a game's band:
`<House>_Tech<band>`, `Opening` = the bounded configured opening, absent = the house's top mix renormalised
over the band's slots. Harness: `vanilla-match.py --tech N...` plays a copy of each map with `[BASIC]
TechLevel=N` (map `<map>@t<N>`); `ratio-analysis.py --tech N` analyses one level and writes
`<House>_Tech<band>` candidates.

**Lower bands are set by design, not measured** (Stefan, 2026-10-18: no tech-level ratio testing). Each
band's mix follows what it can field: light units only at 3 (quads the strongest), tank-heavy at 4 (50%
tanks), tanks and launchers lead at 5 (40/25), siege joins at 6 (30/25/20). The light remainder follows
the house's top mix (Harkonnen quads; Atreides trike/quad; Ordos raider/quad). Code defaults
(`QuantBotConfig.cpp`), percent of the fieldable slots:

| House | Tech2 | Tech3 | Tech4 | Tech5 | Tech6 |
|---|---|---|---|---|---|
| Harkonnen | - | quad 100 (one slot) | tank 50, quad 50 | tank 40, launcher 25, quad 35 | tank 30, siege 25, launcher 20, quad 25 |
| Atreides | trike 100 (one slot) | trike 50, quad 50 | tank 50, trike 20, quad 30 | tank 40, launcher 25, trike 15, quad 20 | tank 30, siege 25, launcher 20, trike 10, quad 15 |
| Ordos | raider 100 (one slot) | raider 50, quad 50 | tank 50, raider 20, quad 30 | tank 40, launcher 25, raider 15, quad 20 (tech 5-6) | (Tech5) |
| Fremen, Mercenary | trike 50, raider 50 | trike 30, raider 30, quad 40 | tank 50, trike 15, raider 15, quad 20 | tank 40, launcher 25, trike 10, raider 10, quad 15 | tank 30, siege 25, launcher 20, trike 8, raider 7, quad 10 |
| Sardaukar | - (Tech1: 1-2) | as Fremen | as Fremen | as Fremen | as Fremen |

Each designed mix is already within 2%-50% over its band's slots, so `bound()` plays it unchanged. Checks:
unit test `the shipped tech-band ratios are valid for their band` (every house x band: buildable slots
only, 2-50%, sum 100, the lookup returns it, the top band the measured mix) and `quantbot_strategy`
section 5. In a tech-4 Fortress 1v1 (Atreides v Ordos) both houses start Tuned from the band mix
5000/2000/3000 bps. Every production pass before the learning plays it, and tanks are 54%/52% of the value
built before the learning (40%/45% over 12 minutes, the largest share).

The exploration jobs at tech 3-6 (`x3`-`x6`) were cancelled unplayed when the scope changed. `rR6` (540
games) used the old eight-slot layout with the specials bug and is not used.

### 8.10 Top band: Fremen, Sardaukar, Mercenary, and the three main houses re-confirmed

Setup: CY 1v1, Hard, Swarm, six maps (Habbanya-Penny, Ergun-Sahij, Tuono-Soolin, Sihaya-Pip, X-Factor,
Sihaya-Lauper), all six houses, all 15 pairs, both assignments, `--seed-per-map 1`, bundled maps (tech 8).
Arms: `default@<style>#Tuned-Swarm` v the control `default#Learned-Swarm` (the configured opening, bounded).
A candidate file sets only the Fremen/Sardaukar/Mercenary top keys, so Harkonnen, Atreides and Ordos play
their shipped mixes in the same games (their re-confirmation after the specials change). The decision
statistic is the per-house effect (8.9: least squares over both assignments of every pair, bootstrap 95%
interval). n = games with the candidate on that house; the house is in twice as many.

- **Exploration** `x8` (source 32adfb8a; `default#Bounded-Swarm` v Learned, seeds 71000-71020): 272 games
  were played before the run was cut short (scope change). Per house that is about 20 random-side games,
  too few for the regression or gradient. Pooled over both arms, the cost-effectiveness ranking is the same
  for all three houses and matches the main houses: Sonic Tank 2.5-3.1, siege 1.8-2.2, launcher 1.5-1.8,
  ornithopter ~1.3, quad ~1.25, Devastator ~1.1, tank/trike/raider ~1.0. Candidates (percent: tank, siege,
  launcher, Devastator, Sonic, Deviator, ornithopter, trike, raider, quad):

| Candidate | Fremen | Sardaukar | Mercenary |
|---|---|---|---|
| `t8-cost` (cost-effectiveness) | 8,15,12,9,20,0,10,8,8,10 | 7,15,13,8,23,0,9,8,8,9 | 7,16,12,8,22,0,9,8,8,10 |
| `t8-winners-end` (winners' end composition) | 11,10,13,5,8,0,18,11,13,11 | 8,14,17,5,10,0,17,9,9,11 | 8,10,16,5,12,0,17,12,8,12 |

- **Round 1** (source e276196e, seeds 73000-73030, 720 games per candidate, priority 2; `r8-*`) and
  **confirmation** of the better candidate per house (`t8-best`: Fremen winners' end, Sardaukar and
  Mercenary cost; merged source a35c2c56 with `fix/ai-army-stuck-in-base`; fresh seeds 74000-74060, 1,260
  games; `c8-t8-best`):

| Per-house effect v Learned | Harkonnen | Atreides | Ordos | Fremen | Sardaukar | Mercenary |
|---|---|---|---|---|---|---|
| r1 `t8-cost` (n=120 each) | +0.38 (-0.12..+0.94) | -0.01 (-0.54..+0.50) | -0.10 (-0.60..+0.46) | +0.07 (-0.48..+0.65) | +0.01 (-0.57..+0.49) | +0.36 (-0.18..+0.88) |
| r1 `t8-winners-end` (n=120 each) | +0.48 (-0.06..+0.99) | +0.17 (-0.36..+0.68) | -0.02 (-0.59..+0.48) | +0.42 (-0.08..+0.94) | -0.06 (-0.60..+0.44) | +0.15 (-0.37..+0.70) |
| confirmation `t8-best` (n=210 each) | **+0.52** (+0.14..+0.92) | +0.15 (-0.25..+0.53) | +0.11 (-0.30..+0.51) | +0.34 (-0.04..+0.75) | **-0.46** (-0.85..-0.03) | -0.13 (-0.50..+0.27) |

| Arm, overall | Δ (95% CI) n | W-L / time-outs |
|---|---|---|
| r1 `t8-cost` | +0.119 (-0.03..+0.26) n=720 | 381-338 / 1 (0.1%) |
| r1 `t8-winners-end` | **+0.192** (+0.05..+0.33) n=720 | 392-323 / 5 (0.7%) |
| confirmation `t8-best` | +0.089 (-0.02..+0.20) n=1,260 | 657-601 / 2 (0.2%) |

**Decision: no Fremen, Sardaukar or Mercenary top mix is accepted**, so their top band keeps the
configured opening, bounded (no key). Fremen with the winners' end mix is ahead in every run (+0.42, then
+0.34 on fresh seeds), but its interval still includes 0. Sardaukar with the cost mix is worse on fresh
seeds (interval below 0). Mercenary is level. **Harkonnen, Atreides and Ordos keep their tuned mixes:** after
the specials change Harkonnen beats the control on fresh seeds (+0.52), and Atreides and Ordos are ahead in
sign (+0.15, +0.11) with the earlier pooled rule (+0.27, n=468, 8.5) unchanged. The by-house Δ of the
confirmation run puts Atreides at **+0.31** (+0.06..+0.55) and Ordos at **+0.33** (+0.06..+0.59).

Not run (scope cut by Stefan, 2026-10-18): Brutal, Fortress and `/weak` with the band defaults, and
exploration at tech 3-6. To continue Fremen: `t8-winners-end`, another 1,260 fresh games (seeds 75000-75060).
Compact rows: `docs/formation-results/play-styles/ratios/{x8,r8-t8-cost,r8-t8-winners-end,c8-t8-best}`.
