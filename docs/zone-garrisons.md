# Zone Garrisons

Stefan, 2026-11-27: "i want infantry and troopers to be able to occupy friendly and enemy R/I/C - using
the capture command. if they enter it the building needs a garasioned look with sandbags added to it like
in C&C and it now is owned by the house that occupies it. AI can also garasion its own buildings
defensively when under attack or when enemy is looking to caputre the building - this will make infantry
far more valuable."

## The match rule

**Zone Garrisons** (`GameOptions::zoneGarrisons`, INI key `Zone Garrisons`, default on; bit 5 of the MOD18
city rules byte, set only when off; protocol 144). Off, a zone is captured as any building is (the red-health
capture that replaces the building), as up to protocol 143.

Who: the units that can capture (`UnitBase::canCaptureStructures`): Soldier and Trooper (and the Flame
Trooper in Tornie content). What: Residential, Commercial and Industrial zones (`ZoneStructure`), the 2x2
lot, whatever its density or civic overlay.

| Situation | What a capture order does |
|---|---|
| Own zone with room (fewer than 4 inside) | The soldier walks to it and enters from any tile touching it (`kEntryDistance` 1.5, diagonals included). Owner unchanged. |
| Own zone, full | Refused: the order is not taken. |
| Ally's zone | Refused. Allies' zones are never taken or shared (one house a garrison). |
| Enemy zone nobody holds | The soldier walks in and the zone becomes its house's: owner, tiles, power draw and building counts move; density, residents, civic overlay and health stay (`ZoneStructure::transferOwnership`). Counted as a loss for the old owner (its AI hears of it). |
| Enemy zone enemy soldiers hold | The soldier cannot enter: it fires on the zone from weapon range (`UnitBase::captureTargetHeld`) until the garrison is gone, then walks in and takes it. |

Inside, soldiers are hidden exactly as a Carryall's cargo is (`UnitBase::enterGarrison`: off the map, inactive,
unselectable, no orders, no AI orders). Each keeps its health, house and weapon reload.

**Firing out.** Each cycle the zone ticks its soldiers' reload clocks; when one is ready it picks the nearest
visible enemy unit within the longest weapon range of its garrison, measured from the zone tile nearest the
target (lower object id on a tie; a search that finds nothing waits 8 cycles). Every soldier whose weapon
reaches it fires its own weapon from that tile (`UnitBase::fireFromGarrison`: the Trooper's gun up close and its
rocket further out, Trooper anti-air at aircraft, the Soldier only at ground targets). Kills and damage are
credited to the soldier's type, as in the open. Garrisons shoot units, not buildings.

**Cover, not immunity.** Every hit on a held zone also takes half of it (rounded up) off the soldier at the front
of the garrison, the first to have entered (`occupantDamage`). The building takes the hit in full as before. So
attackers empty a garrison by shooting the zone; a soldier killed inside is credited to whoever fired.

**Coming out.**
- The owner's **Evacuate** button on the zone panel (`CMD_ZONE_EVACUATE`, only the owner's command is obeyed)
  deploys every soldier on free tiles beside the zone, in Guard. An own zone stays with its owner; a captured
  one goes back (below).
- **Below 60% health** (`kEvacuateHealthPercent`) the garrison leaves on its own, and no soldier may enter or
  take a zone that damaged.
- A zone that is destroyed, sold or demolished puts its soldiers out beside the ruin first, each having lost
  half its maximum hit points (rounded up, never below 1: `healthAfterEjection`); one with no free tile
  around dies in it.
- **A captured zone is held only while occupied.** It remembers the house it was taken from (saved, format
  9903). Once its garrison is gone, by Evacuate, at low health or because the last soldier inside was killed, it
  returns to that house: owner, tiles, power draw and counts, as when it was taken. A house the capture
  defeated gets nothing back. The zone panel shows `(captured)` after the garrison count.
- Transport recovery (`Game::reconcileTransportContainment`) knows garrisons and leaves them inside.

**Look.** Sandbags drawn over the 2x2 lot after the zone sprite (`ZoneStructure::drawGarrisonSandbags`): bags a
seventh of the lot long and a tenth deep, tan with a sunlit face, a tie and a dark outline, laid in two staggered
courses on all four sides with the middle bag of the outer course left out as a firing slit; the garrison house's
colour along the front course and an outlined olive helmet per soldier along the top wall. Original art drawn in code (no imported or C&C sprite); it scales with the zoom level. The zone
panel shows `Garrison: n/4` and the Evacuate button while it is held.

**Save format 9900.** A zone saves its occupants (object ids in the order they entered), its garrison's target
and its search clock. The soldiers themselves are saved as hidden units, as Carryall cargo is.

## Follow-up (protocol 148, save 9903)

Stefan, 2026-11-27: "ok make 2 changes. 1 make infantry cost 30 credits. next make troopers and infantry
evacuate buildings with an evacuate when it gets below X% health (test different). if the building explodes
they are ejectedb ut are damaged. if units evacuate the building returns to its original owners control.
also try behaviour where the ai tries capuring undefended buildings or mass spamming infantry to try
capture buildings"

- **Price.** The Soldier costs 30 credits (`config/ObjectData.ini.default`, was 60); the Trooper keeps 100.
  The default content is what DuneCity, vanilla and Dune2R games use. Mods with their own `ObjectData.ini`
  (Tornie, ArcaKev, Jericho and their Lite forms, the community mods) keep their authors' prices.
- **Evacuation threshold**, the rules above. `DUNECITY_TEST_GARRISON_EVACUATE_PERCENT` overrides it for
  experiments only (read once per process, never part of the match rules).
- **Entering.** A capture-ordered soldier enters on any cycle it stands within reach of its zone, not only the
  cycle it stopped (`InfantryBase::checkPos`). It reads its zone's owner every cycle, so it keeps the order when
  a comrade takes the zone first (its cached friendliness made it drop the order). It never fires on the zone it
  walks into: the Dune Legacy capture branch falls through to firing, which classic captures need for red
  health; it fires only on a zone enemy soldiers hold (`UnitBase::engageTarget`). The player-path test found
  all three.

### Evacuation threshold

`rs160-dunecity`, four Brutal QuantBots, 45 minutes, AWS, build `893c5acc0` with the threshold set by the
experiment variable; results in `~/Documents/projects/outputs/zone-garrison-followup-20261127/`. Mixed games:
`default` against `default@zone-garrison-raid-levy` (all three AI switches on, so garrisons are used heavily),
seeds 9101-9148, 288 games each. Garrison counts: self-play with traces, 29 games kept (of 48; the rest were
lost when the `aws` worker was stopped).

| Threshold | Placement difference (95% CI) | Kill / loss value of the garrison houses | Soldiers out alive per entry | Garrisons caught in an exploding zone per game | Zones taken per game |
|---|---|---|---:|---:|---:|
| 0 (never leave) | -0.046 (-0.12..+0.03) | 1.00 | 53% | 14.0 | 44 |
| 25% | -0.002 (-0.08..+0.08) | 1.01 | 59% | 2.2 | 24 |
| 40% | -0.064 (-0.14..+0.02) | 1.00 | 59% | 0.7 | 34 |
| **60%** | **+0.009 (-0.07..+0.09)** | 1.00 | **65%** | 0.9 | 27 |

Placement and exchange value are level at every threshold, and zones held at the last census do not
separate (paired differences +2 to +5 zones, intervals about ±15). The measure that moves is soldier
survival. 60 brings the most soldiers out alive and loses as few garrisons in exploding zones as 40, so the
rule is 60.

## QuantBot

`[Force Concentration] ZoneGarrison`, `ZoneCaptureRaid` and `ZoneCaptureLevy` (all default **0**; play styles
`zone-garrison`, `zone-capture-raid`, `zone-capture-levy` and `zone-garrison-raid-levy` for all three).
Stateless, each unit pass (`QuantBotGarrison.cpp`), only with the match rule on in a city game.

ZoneGarrison:

1. **Threat.** Every visible enemy ground unit that can attack or capture within 7 tiles of one of our zones adds
   1 to it, 3 for a soldier that can capture.
2. **Defence.** The most threatened zones first: up to `min(4, (threat + 1) / 2)` soldiers, counting those inside
   and those already ordered in. Each comes from our nearest Soldier or Trooper within 12 tiles that is at least
   half healthy and not held by a human order, the repair reserve, an escort or a colony guard.
3. **Release.** A garrison of ours in an own zone with no visible enemy within 11 tiles comes out (a house with a
   human player keeps its garrisons; a captured zone keeps its garrison, or it would go back).
4. **Offence.** One of our soldiers within 5 tiles of an empty visible enemy zone takes it, one soldier a zone a pass.

ZoneCaptureRaid: every empty, seen enemy zone with no enemy unit that can fight or capture within 6 tiles
and no enemy turret within 9 gets the nearest free Soldier or Trooper within 24 tiles (nearest pairs first, one
soldier a zone), which stays inside.

ZoneCaptureLevy: while enemy zones are there to take, each idle Barracks trains a Soldier (30 credits) until
the house has 16 Soldiers, keeping 300 credits; every free Soldier walks into the nearest empty, seen enemy
zone within 48 tiles.

At most 6 capture orders a house a pass, all switches together. A zone is chosen only with a free tile beside it (a packed city block
cannot be entered). `Player`'s order helpers leave a soldier on its way into a zone alone (`garrisonBound`), so
the army, defence and formation passes do not pull it away; one left in capture mode without a zone goes back
to Area Guard. Decision log events: `zone_garrison_order` (why `defend` / `take`),
`zone_garrison_release`, `zone_capture_levy_train`; engine events `zone_garrison_returned`, `zone_garrison_enter`, `zone_garrison_taken`, `zone_garrison_exit`.

## Evidence

`rs160-dunecity` (Stefan's 4P DuneCity free-for-all), four Brutal QuantBots, seeds 9101-9148, 45 minutes, AWS,
results in `~/Documents/projects/outputs/zone-garrison-20261127/`:

| Version | ON score | OFF score | Difference (95% CI) | Verdict |
|---|---:|---:|---|---|
| v1 `2979efc5d`, 288 mixed games | -0.117 | -0.067 | -0.050 (-0.13..+0.03) | level |
| v2 `d89b7a9c4`, 288 mixed games | -0.101 | -0.079 | -0.022 (-0.11..+0.06) | level |

Self-play with the switch on, 12 games: v2 soldiers entered 5,211 garrisons and took 884 enemy zones (v1 needed
22,055 orders for 4,393 entries; v2 8,729). The AI uses garrisons heavily but they do not win it games on this
map, so `ZoneGarrison` stays off; HANDOVER.md, "Zone Garrisons", has the gates.

Follow-up rules (30-credit Soldier, 60% threshold, captured zones return), build `893c5acc0`, 288 mixed games
each, against `default`:

| Arm | ON score | OFF score | Difference (95% CI) | Wins ON / OFF | Time-outs |
|---|---:|---:|---|---:|---:|
| ZoneGarrison | -0.107 | -0.098 | -0.009 (-0.09..+0.08) | 67 / 68 | 153/288 |
| ZoneCaptureRaid | -0.102 | -0.086 | -0.016 (-0.10..+0.07) | 64 / 76 | 148/288 |
| ZoneCaptureLevy | -0.106 | -0.079 | -0.027 (-0.11..+0.05) | 73 / 73 | 142/288 |
| all three (threshold 60) | -0.097 | -0.106 | +0.009 (-0.07..+0.09) | 65 / 65 | 158/288 |

All level: no switch becomes a default. Raid and levy on `rs192-dunecity` (start from nothing) and Metropolis: HANDOVER.md, "Zone Garrisons follow-up".

## Tests

- `zone_garrison_probe` (headless nightly; `tests/units/zone-garrison-probe.inc`), 16 cases: own zone (four
  enter, a fifth refused, owner kept), take (zone, tiles, density and power draw move), fire out (kill credited
  to the Trooper), assault (three Troopers shoot one held soldier out and take the zone), evacuate (owner's
  command, an enemy's refused), destroyed (soldiers out of the ruin, each down half its hit points), return
  (evacuated and killed garrisons hand captured zones back; an own zone stays), low health (held at the line,
  out below it, a captured zone goes back, no entry below it), ally refused, containment, save and load (with
  the original owner), rule off, pictures `garrison-zoom0..2.png`, and **the player's own path**: units
  selected as a click selects them, the unit panel's real Capture button (and the C key), a real SDL mouse click
  on the zone through `Game::doInput`, the zone panel's `Garrison: n/4` and `(captured)` text and its real
  Evacuate button; a full own zone, an ally's zone and a damaged zone refused; an enemy's Evacuate disabled.
- `CoopGameSettingsTestCase`: protocol 144 bit 5 round trip and hash; bit 7 is the first unknown city rule.
- `CityRoadTestCase`, `NetworkHardeningTestCase`: `CMD_ZONE_EVACUATE` is the last command and acts on an owned object.
