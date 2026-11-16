# Reworked campaign (Remastered)

The Campaign screen has a **Maps** choice next to **Start from**: *Original* plays the
Westwood missions from `SCENARIO.PAK`; *Reworked* plays the `RWRK_SCEN?0??.INI` missions
bundled in `data/maps/singleplayer`. Everything else is the normal campaign: the same
house choice, briefings, map choice, debriefing, statistics, cut-scenes, Full campaign or
Single mission, AI partner and enemy AI (QuantBot with its campaign rules, or the Campaign AI).
The game screen and the menus between reworked missions show a **REMASTERED** badge.

## Which missions exist

A house offers the reworked campaign only when all 22 of its missions exist
(`ReworkedCampaign::isAvailableForHouse`); the house list on the Campaign screen is
limited to those houses while *Reworked* is selected. Stock-take on 2026-11-16:

| House | Reworked missions | Complete |
|---|---|---|
| Atreides | 1-22 (1-11, 21, 22 by Dmitri Fatkin; 12-20 by DuneCity) | yes |
| Harkonnen | 1-3 | no |
| Ordos | 1 | no |
| Fremen | 1, 11 | no |
| Mercenary | 1, 22 | no |
| Sardaukar | 1 | no |

`RWRK_SCENG017/018/020.INI` are Fatkin's base-template sheets for those tech levels, not
missions. Every mission ID 1-22 is reachable from the region map (`MapChoice`), so a
complete house needs all 22.

## Missions 12-20 (Atreides)

Fatkin's set stopped at mission 11 and resumed at 21. The missing nine were rebuilt from
the original missions in his style, the way his `RWRK_SCENA022` reworks its original:

- **Same map**: the original seed (terrain), the player's start and units, the original
  enemy base, its turrets and blooms, briefing pictures and win/lose flags.
- **A second enemy house** on free rock away from the player (its centre 30 tiles from the
  player's start where the rock allows, never under 24): Mercenary beside Ordos (levels 5 and 7), Sardaukar beside
  Harkonnen (level 6), Sardaukar beside Harkonnen and Ordos at level 8 (mission 20; there is
  no room for a fourth base). Its base follows the level's tech: Construction Yard, Windtraps,
  Refineries, factories, Outpost, WOR, Repair, Hi-Tech, IX or Palace, turrets toward the player.
- **Economy and pressure** as in Fatkin's level 5 and 8 missions: two harvesters per enemy
  refinery, more defenders, about 15 spice fields, four or five AI teams a house, enemy waves
  (some repeating) and the player's reinforcements; player credits 7,500 / 10,000 / 12,500 /
  17,000 for levels 5-8, unit cap 32.

Every building lies on rock inside the map with no overlap; `tests/ReworkedCampaignTestCase.cpp`
checks that for all 22 missions against the real seed generator. The missions are generated
deterministically by `tools/reworked-campaign/rework_missions.py ORIGINALS OUT` from the
original `SCENA012-020.INI` (unpacked from `SCENARIO.PAK`); `scenario_map.py` validates any
seed-map scenario and renders previews, and can be reused to complete another house.

Fatkin's missions carry `License=LDCPUL` (`data/maps/singleplayer/LDCPUL.TXT`): his content is
licensed to Dune Legacy for non-commercial use, modifications permitted with his copyright and
licence kept. The DuneCity missions carry `Author="DuneCity"` and no licence line.

## How it works

- A reworked game is one whose scenario filename starts with `RWRK_`
  (`GameInitSettings::isReworkedCampaign`). The filename already travels through saves,
  restarts, the next mission and co-op payloads, so nothing else is stored and saves stay
  compatible. `GameInitSettings::useReworkedMaps()` switches a new campaign or single mission.
- `FileManager::openCampaignFile` reads `RWRK_` names from `maps/singleplayer` in the data
  search path, the same files the Custom Game list shows.
- The badge: `MenuBase::showsRemasteredWatermark()` (the Campaign screen while *Reworked* is
  selected; other menus while `startSinglePlayerGame` runs a reworked game) and
  `Game::drawScreen` for the map view. Co-op campaigns show it in game only.
- Not offered with the Tornie family editions, which bring their own campaigns.

## Testing

```bash
python3 tests/ai/scenario-matrix.py --tier full --categories campaign \
  --cells '^campaign/(reworked-)?player-brutal-v-quantbot-easy/vanilla$' --cases atreides --list
```

schedules the 22 reworked missions and the same 22 originals on the same seeds (idle human,
Brutal QuantBot support, QuantBot Easy enemies). Submit it with `dunecity-queue submit` on a
committed revision. `run-campaign-balance.py --scenario --reworked` plays one mission.
