# AI scenario matrix: 635 games

placement scoring: 1st +1 ... last -1, time-out survivors share the top places minus 0.25 (1v1 draw -0.25); verdict = 95% interval of the paired difference against the control above / below 0

## skirmish

| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |
|---|---|---|---|---|---|---|---|
| 2p-small-smallarmy/vanilla | 4 | 4/4 | T1 QH 3, T2 AH 1 | 17.3 | wins 1 | 3/4 [0.30-0.95] | - |
| 3p-ffa-mcv-smallarmy/vanilla | 4 | 4/4 | T1 QH 4 | 34.3 | - | - | - |
| 2v2-fortress-largearmy/vanilla | 4 | 4/4 | T1 QH 4 | 19.5 | wins 1 | 4/4 [0.51-1.00] | - |
| 2v2-medium-noarmy/dunecity | 4 | 3/4 | T1 AH+QH 3 | 33.9 | wins 1 | 3/4 [0.30-0.95] | time-out 1 |
| 2p-medium-noarmy/dunecity | 4 | 3/4 | T1 QB 3 | 45.3 | wins 1 | 3/4 [0.30-0.95] | time-out 1 |
| 4p-ffa-medium-smallarmy/vanilla | 4 | 1/4 | T1 QB 1 | 54.4 | - | - | nobody eliminated 2, time-out 3 |
| 5p-ffa-small-noarmy/vanilla | 4 | 1/4 | T1 QB 1 | 57.5 | - | - | time-out 3 |
| 3v3-medium-smallarmy/vanilla | 4 | 4/4 | T2 QM 4 | 31.3 | wins 1 | 0/4 [0.00-0.49] | - |
| 4p-ffa-small-largearmy/dunecity | 4 | 3/4 | T1 QH 2, T3 AH 1 | 32.6 | - | - | time-out 1 |
| 7p-ffa-mcv-noarmy/vanilla | 4 | 1/4 | T1 QH 1 | 77.3 | - | - | time-out 3 |
| 6p-ffa-small-smallarmy/dunecity | 4 | 0/4 | - | 60.0 | - | - | time-out 4 |
| 8p-ffa-medium-smallarmy/vanilla | 4 | 1/4 | T2 QH 1 | 74.8 | - | - | time-out 3 |
| 4v4-fortress-largearmy/dunecity | 4 | 1/4 | T1 QH 1 | 69.1 | wins 1 | 1/4 [0.05-0.70] | time-out 3 |

Placement score by controller (all games of the category; teams are entrants):

| Controller | Houses | Mean score [95% CI] |
|---|---|---|
| QB (qBotBrutal) | 20 | +0.30 [+0.09, +0.50] |
| QH (qBotHard) | 80 | +0.15 [-0.01, +0.32] |
| QM (qBotMedium) | 72 | -0.14 [-0.29, +0.02] |
| AH (AIPlayerHard) | 32 | -0.28 [-0.50, -0.04] |
| QE (qBotEasy) | 28 | -0.35 [-0.56, -0.14] |
| AM (AIPlayerMedium) | 20 | -0.57 [-0.77, -0.36] |

QuantBot order QB > QH > QM > QE by mean score: holds

## cy

| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |
|---|---|---|---|---|---|---|---|
| 1v1-qh-v-ah/vanilla | 4 | 4/4 | T1 QH 4 | 24.1 | wins 1 | 4/4 [0.51-1.00] | - |
| 1v1-qb-v-qe/dunecity | 4 | 4/4 | T1 QB 4 | 36.0 | wins 1 40 | 2/4 [0.15-0.85] | - |
| 1v1-qh-v-qm/dunecity | 4 | 4/4 | T1 QH 3, T2 QM 1 | 32.8 | wins 1 | 3/4 [0.30-0.95] | - |
| 1v1-qh-mirror/vanilla | 4 | 4/4 | T1 QH 1, T2 QH 3 | 35.5 | - | - | - |
| 4p-ffa/vanilla | 4 | 1/4 | T1 QH 1 | 56.4 | - | - | time-out 3 |
| 2v2-mirror/dunecity | 4 | 4/4 | T1 QH+QM 4 | 44.1 | - | - | - |
| 4p-ffa/dunecity | 4 | 2/4 | T1 QB 1, T2 QH 1 | 52.6 | - | - | nobody eliminated 1, time-out 2 |

Placement score by controller (all games of the category; teams are entrants):

| Controller | Houses | Mean score [95% CI] |
|---|---|---|
| QB (qBotBrutal) | 8 | +0.34 [-0.25, +0.84] |
| QH (qBotHard) | 32 | +0.28 [-0.02, +0.58] |
| QM (qBotMedium) | 20 | -0.15 [-0.51, +0.22] |
| QE (qBotEasy) | 12 | -0.42 [-0.69, -0.16] |
| AH (AIPlayerHard) | 8 | -0.92 [-1.00, -0.75] |

QuantBot order QB > QH > QM > QE by mean score: holds

## aaa

| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |
|---|---|---|---|---|---|---|---|
| qb-v-4ah/vanilla | 6 | 6/6 | T2 QB 6 | 31.3 | wins 2 40 | 6/6 [0.61-1.00] | - |
| reverse-qm-v-4qh/vanilla | 6 | 6/6 | T1 QH 6 | 31.7 | loses 2 | 6/6 [0.61-1.00] | - |
| qb-v-4ah/dunecity | 6 | 6/6 | T1 AH 1, T2 QB 5 | 40.5 | wins 2 | 5/6 [0.44-0.97] | - |

## campaign

| Mod | Partner | Enemy | Missions | Won | Lost | Not finished | Crash | Win by conquest/economic/quota | Mean min (won) | Triggers fired |
|---|---|---|---|---|---|---|---|---|---|---|
| dunecity | easy | QuantBot Easy | 48 | 46 | 1 | 1 | 0 | 7/37/2 | 18.1 | 148 |
| vanilla | easy | QuantBot Easy | 48 | 42 | 6 | 0 | 0 | 33/0/9 | 21.1 | 162 |
| vanilla | hard | CampaignAIPlayer | 27 | 23 | 4 | 0 | 0 | 18/0/5 | 20.1 | 97 |
| dunecity | hard | QuantBot Easy | 96 | 90 | 2 | 4 | 0 | 12/74/4 | 16.1 | 250 |
| vanilla | hard | QuantBot Easy | 96 | 84 | 10 | 2 | 0 | 66/0/18 | 19.8 | 337 |
| dunecity | medium | QuantBot Easy | 48 | 47 | 1 | 0 | 0 | 8/37/2 | 16.6 | 135 |
| vanilla | medium | QuantBot Easy | 48 | 43 | 5 | 0 | 0 | 34/0/9 | 19.3 | 158 |

Hard partner per mission (W won, L lost, T not finished, X crash; e = economic, q = quota win):

| Mod | House | m1 | m2 | m3 | m5 | m6 | m8 | m9 | m11 | m12 | m14 | m15 | m17 | m18 | m20 | m21 | m22 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| vanilla | atreides | WqWq | WqWq | WqWq | WW | WW | WW | WW | LW | WW | WW | WW | WW | WW | LL | LW | WW |
| vanilla | harkonnen | WqWq | WqWq | WqWq | WW | WW | WW | WW | WW | WW | TW | WW | WW | WW | LL | WL | LL |
| vanilla | ordos | WqWq | WqWq | WqWq | WW | WW | WW | WW | WW | WW | WW | WW | WW | WT | WW | WL | WW |
| dunecity | atreides | WW | WqW | WW | WWe | WeW | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | LWe |
| dunecity | harkonnen | WW | WeWe | WeT | WeWe | WeW | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeL |
| dunecity | ordos | WqT | WW | WqWq | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | WeWe | TT | WeWe | WeWe | WeWe | WeWe |

## ladder

| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |
|---|---|---|---|---|---|---|---|
| QB-v-QM-rs-vanilla/vanilla | 8 | 8/8 | T1 QB 4, T2 QM 4 | 29.6 | wins 1 | 4/8 [0.22-0.78] | - |
| QH-v-QE-rs-vanilla/vanilla | 8 | 8/8 | T1 QH 7, T2 QE 1 | 28.0 | wins 1 | 7/8 [0.53-0.98] | - |
| QB-v-QM-rs-dunecity/dunecity | 8 | 6/8 | T1 QB 5, T2 QM 1 | 41.8 | wins 1 | 5/8 [0.31-0.86] | time-out 2 |
| QH-v-QE-rs-dunecity/dunecity | 8 | 8/8 | T1 QH 8 | 33.4 | wins 1 | 8/8 [0.68-1.00] | - |
| QH-v-QE-habbanya-penny/vanilla | 8 | 8/8 | T1 QH 8 | 44.6 | wins 1 | 8/8 [0.68-1.00] | - |
| QM-v-QE-rs-vanilla/vanilla | 8 | 8/8 | T1 QM 5, T2 QE 3 | 33.2 | wins 1 | 5/8 [0.31-0.86] | - |
| QB-v-QM-habbanya-penny/vanilla | 8 | 8/8 | T1 QB 8 | 39.5 | wins 1 | 8/8 [0.68-1.00] | - |
| QM-v-QE-rs-dunecity/dunecity | 8 | 2/8 | T1 QM 2 | 56.5 | wins 1 | 2/8 [0.07-0.59] | time-out 6 |
| QM-v-QE-habbanya-penny/vanilla | 8 | 0/8 | - | 60.0 | wins 1 | 0/8 [0.00-0.32] | time-out 8 |

Ladder (stronger side's placement score; the order holds when the interval is above 0):

| Pair | Map | Games | Strong W-L-T | Strong score [95% CI] | Order |
|---|---|---|---|---|---|
| QB v QM | all maps | 24 | 17-5-2 | +0.48 [+0.12, +0.78] | holds |
| QB v QM | habbanya-penny | 8 | 8-0-0 | +1.00 [+1.00, +1.00] | holds |
| QB v QM | rs-dunecity | 8 | 5-1-2 | +0.44 [-0.12, +0.84] | not shown |
| QB v QM | rs-vanilla | 8 | 4-4-0 | +0.00 [-0.75, +0.75] | not shown |
| QH v QE | all maps | 24 | 23-1-0 | +0.92 [+0.75, +1.00] | holds |
| QH v QE | habbanya-penny | 8 | 8-0-0 | +1.00 [+1.00, +1.00] | holds |
| QH v QE | rs-dunecity | 8 | 8-0-0 | +1.00 [+1.00, +1.00] | holds |
| QH v QE | rs-vanilla | 8 | 7-1-0 | +0.75 [+0.25, +1.00] | holds |
| QM v QE | all maps | 24 | 7-3-14 | +0.02 [-0.23, +0.31] | not shown |
| QM v QE | habbanya-penny | 8 | 0-0-8 | -0.25 [-0.25, -0.25] | INVERTED |
| QM v QE | rs-dunecity | 8 | 2-0-6 | +0.06 [-0.25, +0.53] | not shown |
| QM v QE | rs-vanilla | 8 | 5-3-0 | +0.25 [-0.50, +0.75] | not shown |

## mp

| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |
|---|---|---|---|---|---|---|---|
| 1v1-habbanya-penny-qh-mirror/vanilla | 4 | 4/4 | T1 QH 2, T2 QH 2 | 51.1 | - | - | - |
| 1v1-sihaya-chvrches-qh-mirror/vanilla | 4 | 4/4 | T1 QH 4 | 37.4 | - | - | - |
| 2v2-sihaya-toto-qh-v-ah/vanilla | 4 | 4/4 | T1 QH 4 | 20.2 | wins 1 | 4/4 [0.51-1.00] | - |
| 2v2-sihaya-toto-qh-mirror/vanilla | 4 | 4/4 | T1 QH 2, T2 QH 2 | 34.0 | - | - | - |
| 1v1-tuono-orac-qh-mirror/vanilla | 4 | 4/4 | T1 QH 1, T2 QH 3 | 51.1 | - | - | - |
| 2v2-deserted-qh-v-ah/vanilla | 4 | 4/4 | T1 QH 4 | 23.9 | wins 1 | 4/4 [0.51-1.00] | - |
| 3v3-sihaya-lenka-qh-v-ah/vanilla | 4 | 4/4 | T1 QH 4 | 18.3 | wins 1 | 4/4 [0.51-1.00] | - |
| 3v3-sihaya-lenka-qh-mirror/vanilla | 4 | 3/4 | T1 QH 1, T2 QH 2 | 38.0 | - | - | time-out 1 |
| 2v2-deserted-qh-mirror/vanilla | 4 | 4/4 | T1 QH 3, T2 QH 1 | 44.1 | - | - | - |
| 2v2-dunecity-qh-v-ah/dunecity | 4 | 4/4 | T1 QH 4 | 34.3 | wins 1 | 4/4 [0.51-1.00] | - |
| 2v2-dunecity-qh-mirror/dunecity | 4 | 1/4 | T1 QH 1 | 54.6 | - | - | nobody eliminated 3, time-out 3 |
| 3v3-alkozeltser-4-qh-mirror/vanilla | 2 | 1/2 | T1 QH 1 | 62.5 | - | - | nobody eliminated 1, time-out 1 |
| 3v3-full-wormage-qh-v-ah/vanilla | 4 | 4/4 | T1 QH 4 | 24.4 | wins 1 | 4/4 [0.51-1.00] | - |
| 3v3-full-wormage-qh-mirror/vanilla | 4 | 4/4 | T1 QH 1, T2 QH 3 | 38.2 | - | - | - |

Placement score by controller (all games of the category; teams are entrants):

| Controller | Houses | Mean score [95% CI] |
|---|---|---|
| QH (qBotHard) | 180 | +0.23 [+0.09, +0.37] |
| AH (AIPlayerHard) | 48 | -1.00 [-1.00, -1.00] |

QuantBot order QH by mean score: holds

## Problems

- skirmish 2p-small-smallarmy/vanilla: expectation wins:1 missed in 1/4
- skirmish 2v2-medium-noarmy/dunecity: expectation wins:1 missed in 1/4
- skirmish 2p-medium-noarmy/dunecity: expectation wins:1 missed in 1/4
- skirmish 3v3-medium-smallarmy/vanilla: expectation wins:1 inverted (0/4)
- skirmish 6p-ffa-small-smallarmy/dunecity: every game a time-out
- skirmish 4v4-fortress-largearmy/dunecity: expectation wins:1 missed in 3/4
- cy 1v1-qb-v-qe/dunecity: expectation wins:1:40 missed in 2/4
- cy 1v1-qh-v-qm/dunecity: expectation wins:1 missed in 1/4
- aaa qb-v-4ah/dunecity: expectation wins:2 missed in 1/6
- campaign campaign/partner-hard-v-campaign-ai/vanilla/atreides-m5/29303: Hard partner lost at 19.15 min
- campaign campaign/partner-hard-v-campaign-ai/vanilla/atreides-m20/74708: Hard partner lost at 19.56 min
- campaign campaign/partner-hard-v-campaign-ai/vanilla/ordos-m5/22240: Hard partner lost at 15.66 min
- campaign campaign/partner-hard-v-campaign-ai/vanilla/atreides-m2/26276: Hard partner lost at 23.26 min
- campaign campaign/partner-hard/dunecity/atreides-m22/76726: Hard partner lost at 22.63 min
- campaign campaign/partner-hard/dunecity/ordos-m17/73699: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/dunecity/harkonnen-m22/95897: Hard partner lost at 20.27 min
- campaign campaign/partner-hard/dunecity/ordos-m17/74708: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/dunecity/harkonnen-m3/46456: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/dunecity/ordos-m1/19213: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/vanilla/atreides-m11/74708: Hard partner lost at 42.20 min
- campaign campaign/partner-hard/vanilla/atreides-m20/74708: Hard partner lost at 29.03 min
- campaign campaign/partner-hard/vanilla/harkonnen-m14/95897: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/vanilla/harkonnen-m22/94888: Hard partner lost at 16.85 min
- campaign campaign/partner-hard/vanilla/harkonnen-m20/92870: Hard partner lost at 41.51 min
- campaign campaign/partner-hard/vanilla/atreides-m20/75717: Hard partner lost at 31.96 min
- campaign campaign/partner-hard/vanilla/harkonnen-m22/95897: Hard partner lost at 15.48 min
- campaign campaign/partner-hard/vanilla/harkonnen-m20/93879: Hard partner lost at 53.18 min
- campaign campaign/partner-hard/vanilla/atreides-m21/75717: Hard partner lost at 33.34 min
- campaign campaign/partner-hard/vanilla/harkonnen-m21/94888: Hard partner lost at 17.03 min
- campaign campaign/partner-hard/vanilla/ordos-m18/75717: Hard partner time_limit at 80.00 min
- campaign campaign/partner-hard/vanilla/ordos-m21/69663: Hard partner lost at 35.08 min
- ladder QB-v-QM-rs-vanilla/vanilla: expectation wins:1 missed in 4/8
- ladder QH-v-QE-rs-vanilla/vanilla: expectation wins:1 missed in 1/8
- ladder QB-v-QM-rs-dunecity/dunecity: expectation wins:1 missed in 3/8
- ladder QM-v-QE-rs-vanilla/vanilla: expectation wins:1 missed in 3/8
- ladder QM-v-QE-rs-dunecity/dunecity: expectation wins:1 missed in 6/8
- ladder QM-v-QE-habbanya-penny/vanilla: expectation wins:1 inverted (0/8)
- ladder QM-v-QE-habbanya-penny/vanilla: every game a time-out
- ladder QM v QE: order not shown (strong score +0.02 [-0.23, +0.31])
