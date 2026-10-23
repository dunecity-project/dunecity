# Arrakis city scenarios

Twelve standalone single-player missions, each with one **Atreides human** slot.
The P count in filenames is the number of houses, not the number of humans.
Select **DuneCity** in Custom Game. The shared viewpoint and mission order are
ready for a campaign conversion; these maps are not yet wired into the campaign
menu. Read the briefings here before starting: custom maps do not display their
INI header comments as an in-game briefing.

| Order | Mission | Win condition | What needs doing |
| --- | --- | --- | --- |
| 1 | Sihaya Basin | Defeat Harkonnen | Your tiny shelf cannot support a competing city. Escort two MCVs across sand, colonise empty rock and protect new harvesting routes. |
| 2 | Ash Quarter | Defeat Harkonnen and Mercenaries | A long industrial town has abandoned its southern tenements. Restore policing or rezone the polluted blocks before persistent crime produces hostile gangs. |
| 3 | Hagal Flats | Store 24,000 credits of refined spice | Expand storage and harvesting while two rival houses raid the fields. Starting cash and city taxes do not satisfy the spice quota. |
| 4 | Arrakeen Blackout | Defeat Harkonnen and Mercenaries | Restore power to a sprawling damaged city before its neighbourhoods empty. Decide which districts to repair and defend first. |
| 5 | Harg Pass Convoy | Defeat Harkonnen and Mercenaries | Five small supply towns share one construction yard. Protect the useful links and choose which exposed settlements to evacuate or reinforce. |
| 6 | Cielago Watch | Survive 30 minutes | Hold a compact mesa under pressure from two directions. The timer wins the mission; destroying an enemy base does not end it early. |
| 7 | Tuono Crossing | Defeat Harkonnen and Sardaukar | Start with a mobile column and supply ledge, without a construction yard. Move an MCV to open rock and establish a real base. |
| 8 | Shield Wall Rift | Defeat all three rival houses | Harkonnen, Ordos and Fremen are on separate teams and fight each other. Exploit their conflict without letting one rival take over. |
| 9 | Carthag Vise | Defeat all three hostile houses | A valley city faces northern and southern armies and a Sardaukar battery. Balance the defence of its core and outlying refineries. |
| 10 | Coriolis Gap | Defeat Harkonnen and Sardaukar | Take the middle plateau and breach a much larger fortified eastern city through its guarded gates. |

Losing all your buildings loses every mission. Keep Tuono's starting depot alive
until an MCV deploys. There is no population victory: 5,000 displayed population
is merely the crime system's eligibility threshold, not a meaningful challenge.
Gang outbreaks require sustained dangerous crime and surviving hostile houses.

## Habbanya-Penny additions

Two additional missions take their geography from Rippsblack's **Habbanya-Penny**:
a broad enclosing mountain rim, paired crescent-shaped spice basins, winding
sand battle lanes, opposed home shelves and secondary rock for expansion.
Neutral sandworms threaten exposed harvesting and convoy routes.
They use original terrain variations and leave substantial open building space.

| Mission | Objective | Opening problem |
| --- | --- | --- |
| Habbanya Crescent | Defeat the opposing house | Grow a small township onto neighbouring rock, protect harvesting through the crescent lanes, then assault an established rival city. |
| Habbanya Narrows | Defeat both hostile houses | Move two MCVs off a small supply foothold, establish a colony and break the outposts controlling separate approaches through the basins. The Harkonnen and Ordos also fight each other. |

Both are single-player Atreides missions. Keep Narrows' starting buildings alive
until an MCV deploys. Neither uses a population victory condition. These are
additional standalone chapters, not replacements for the ten-map campaign order.
Both are published as version 1 and installed in the local **SP User Maps** list.
The live catalogue, complete downloads and manifest/file hashes were verified.

The final files regenerate byte-for-byte and pass structural/route checks.
Every starting MCV can reach distant empty yard footprints and both the inner
and outer spice fields. Both load without warnings; active Medium AI versus
Medium AI checks lasted 25 simulated minutes without an opening defeat or gang
outbreak. An idle player lost Crescent near minute 12, while Narrows remained
alive at minute 12. These are engine checks, not complete human victory tests.

## Availability and checks

The missions ship in `data/maps/singleplayer`. Use **Custom Game → SP Maps** and
filter to **DuneCity**. Sihaya Basin, Ash Quarter and Coriolis Gap are **version 2**;
the seven additional missions are **version 1**. They were also published to the
former in-game community catalogue on 2026-09-24; that catalogue and its upload
path were removed from the game on 2026-10-18 (see `docs/workshop.md`).

All ten load in the real engine without map warnings, pass placement/format
validation and regenerate byte-for-byte. Opening simulations used an idle human
slot against Medium QuantBots. Ash was also checked with a second seed: real
hostile gang outbreaks appeared at about five minutes in both runs. The two
expansion maps have traversable paths from both starting MCVs to distant legal
yard footprints. Hagal's starting storage is below its 24,000-spice quota and
can reach it by adding two silos. No mission uses population victory.

These are opening and mechanics checks, not completed human balance playtests.
The active AI benchmarks held Sihaya for 35 minutes; Cielago and Coriolis
remained difficult, with the helper losing before the holdout deadline or
conquest. Enemy cities can also suffer unrest: their stronger production and
armies remain part of the challenge. Custom-game difficulty remains selectable.

## City and terrain design

The ten maps use different landforms and street patterns: isolated islands,
a diagonal ribbon town, fortified gates, a small mesa, scattered depots, a narrow
crossing, a valley, a broken ring city, four rival shelves and a chain of hamlets.
Vacant lots, road stubs, damaged buildings and underserved pockets are deliberate.
The maps do not assume that a city should already be solved when you arrive.

Dune structures supply their real city roles: factories, refineries, yards,
silos and starports provide industrial destinations; Outposts and House IX
provide commerce; Barracks, WOR and Palaces provide housing. Local mixed
neighbourhoods use those roles instead of separating residential and industrial
areas across the map. Const Yards, Silos and Starports provide industry without
industrial pollution. Power is house-wide, not transmitted along roads.

The references are the actual Micropolis scenario city layouts and the existing
Dune maps All Against Atreides, The Sardaukar Outpost and Alkozeltser. These
inspire urban shape and tactical problems; no unsupported flood, earthquake,
escort-arrival or scripted city-rating objectives are claimed.

## SimCity repair

The original 192×192 SimCity map had large separated R/C/I districts. The real
engine found complementary road destinations for only **487 of 1,087 zones**.
The replacement repeats compact mixed blocks and uses Dune infrastructure as
city destinations. All **1,222 zones** now pass the same engine route check.

Each city's roads form one connected network. Terrain, mission flags, CHOAM
and player sections are preserved. The generator also repairs orphaned
Player3 sandworms by assigning them to the neutral Fremen house.


## Reproduction

```sh
python3 scripts/gen_city_scenarios.py
python3 scripts/gen_habbanya_scenarios.py
python3 scripts/validate_city_maps.py
```

The main generator creates the ten missions deterministically; the Habbanya
generator adds the two Penny-inspired missions. The separate
`scripts/pack_simcity_user_map.py` reproduces the repaired SimCity map.
Validation rejects illegal placements and invalid mission data. Authored
scenario service defects are reported for playtesting, while SimCity retains
strict traffic and connectivity checks.
