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
| Harkonnen | 1-22 (1-3 by Dmitri Fatkin; 4-22 by DuneCity) | yes |
| Ordos | 1-22 (1 by Dmitri Fatkin; 2-22 by DuneCity) | yes |
| Fremen | 1, 11 | no |
| Mercenary | 1, 22 | no |
| Sardaukar | 1 | no |

Fremen, Mercenary and Sardaukar have no Westwood originals in `SCENARIO.PAK`; theirs are in
`OPENSD2.PAK` and are not reworked yet.

`RWRK_SCENG017/018/020.INI` are Fatkin's base-template sheets for those tech levels, not
missions. Every mission ID 1-22 is reachable from the region map (`MapChoice`), so a
complete house needs all 22.

## The DuneCity missions

Fatkin's Atreides set stopped at mission 11 and resumed at 21; his Harkonnen set stops at 3 and
his Ordos set at 1. The missing missions were rebuilt from the originals in his style, the way his
`RWRK_SCENA022` reworks its original:

- **Same map**: the original seed (terrain), the player's start and units, the original
  enemy base, its turrets and blooms, briefing pictures and win/lose flags.
- **Missions 2-4** (tech level 2) are 32x32 maps with no room for another base. As in Fatkin's
  own level 2 reworks they keep the original teams and get three or four more defenders, a
  harvester, enemy credits of at least 300, a few more spice fields and two enemy waves.
- **A second enemy house** from mission 5, on free rock away from the player (its centre 30
  tiles from the player's start where the rock allows, never under 24). Each campaign pairs the
  original enemy with a fixed partner (`CAMPAIGNS` in the generator):

  | Campaign | Atreides enemy | Harkonnen enemy | Ordos enemy |
  |---|---|---|---|
  | Atreides | - | Sardaukar | Mercenary |
  | Harkonnen | Sardaukar | - | Mercenary |
  | Ordos | Mercenary | Sardaukar | - |

  Missions 20-22 (tech level 8) add Sardaukar and Mercenary where the mission lacks them and
  there is room: Atreides 20 and Harkonnen 20 and 22 have no room for a Mercenary base. The
  base follows the level's tech: from Fatkin's level 3 and 4 bases (Barracks, Light and Heavy
  Factory, WOR, Outpost) up to Repair, Hi-Tech, IX and turrets toward the player. No added
  base gets a Palace: the original Palaces are each mission's superweapons.
- **Economy and pressure** as in Fatkin's level 3-8 missions: harvesters for every enemy
  refinery, more defenders, about 15 spice fields, three to five AI teams a house, enemy waves
  (some repeating) and the player's reinforcements; player credits 2,250 / 4,750 / 7,500 /
  10,000 / 12,500 / 17,000 for levels 3-8. Each house gets its own special units: Sonic Tanks
  for Atreides, Devastators for Harkonnen, Deviators for an Ordos enemy (an Ordos player's heavy
  reinforcement is a Siege Tank), Raider Trikes for Ordos and Quads for Harkonnen, which build no
  trikes. Nothing is placed on a spice bloom or field. A unit a map started with on a bloom
  used to load invisible to everyone, so its house could never be defeated; the loader now keeps
  it visible with the bloom intact (`UnitBase::deployAtMapStart`), and the missions stay clear anyway.
- **Faults in the originals are left out**: a structure the game would drop on loading (the
  Hi-Tech over the east edge of the map in `SCENH015`/`SCENO015`, the Light Factory and Spice
  Silo listed twice in `SCENO005`) is not written, so every listed structure is one the mission
  really has.

Every building lies on rock inside the map with no overlap and no unit stands on a bloom;
`tests/ReworkedCampaignTestCase.cpp` checks that for all 22 missions of each complete house
against the real seed generator. The missions are generated deterministically by
`tools/reworked-campaign/rework_missions.py ORIGINALS OUT [A|H|O...]` from the original
`SCEN?0??.INI` (unpacked from `SCENARIO.PAK`); regenerating the Atreides missions reproduces the
bundled files byte for byte. `scenario_map.py` validates any seed-map scenario and renders
previews.

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

schedules a house's 22 reworked missions and the same 22 originals on the same seeds (idle
human, Brutal QuantBot support, QuantBot Easy enemies); `--cases 'harkonnen|ordos'` takes those
two houses, `--cases 'atreides|harkonnen|ordos'` all three. Every vanilla campaign cell has a
`reworked-` twin, so the other difficulties use the same pattern: `--enemy-difficulty medium` with
`player-brutal-v-quantbot-medium` (a strong player) or `partner-medium-v-quantbot-medium` (support
of the enemy's tier, the calibration's even match), and `partner-brutal-v-campaign-ai-release` for
the Campaign AI enemy. Submit it with `dunecity-queue submit` on a committed revision.
`run-campaign-balance.py --scenario --reworked` plays one mission.
