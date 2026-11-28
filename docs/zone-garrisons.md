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
| Own zone with room (fewer than 5 inside) | The soldier walks to it and enters from any tile touching it (`kEntryDistance` 1.5, diagonals included). Owner unchanged. |
| Own zone, full | Refused: the order is not taken. |
| Ally's zone | Manual Capture is refused. Automatic emergency relocation may use a friendly zone with room, without changing its owner. |
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

**Shelter.** Occupants take **no damage from hits on their building**. They retain their own
health and healing state; there is no per-hit cover-damage remainder. Destruction is different:
occupants are put on the map first, then take ordinary collapse damage and can die visibly.

**Regeneration.** Living occupants heal continuously while inside, independently of building repairs.
`[Map Settings] GarrisonHealTime = 60` in `ObjectData.ini` means a full heal from zero in 60 seconds
of game time. Fractional health and the per-cycle division remainder are saved, deterministic, and
clamped at maximum health. Leaving the building stops this regeneration.

**Stable fighting positions.** While occupied, a lot keeps its population, density and civic type.
Growth, decline, power-loss vacancy and Hospital/Church designation resume after the garrison leaves.
The lock is derived from the existing occupant list; no additional option is required.

**Coming out.**
- The owner's **Evacuate** button on the zone panel (`CMD_ZONE_EVACUATE`, only the owner's command is obeyed)
  deploys every soldier on free tiles beside the zone, in Guard. An own zone stays with its owner; a captured
  one goes back (below).
- **Below 30% health** (`kEvacuateHealthPercent`), once per drop below the line, occupants try to
  walk into the nearest reachable same-owner or friendly zone with room. Destination ownership
  does not change. With no eligible destination they stay inside; the attempt is not repeated
  until the source building recovers to at least 30% and drops below it again. This applies to
  human and AI garrisons alike. New entry into a building below the line is refused.
- Destruction or demolition deploys occupants before applying collapse injury through the normal
  on-map unit damage/death path. `[Map Settings] GarrisonCollapseDamagePercent = 50` in
  `ObjectData.ini` costs half maximum HP, rounded up. There is no minimum-one-HP guarantee:
  wounded occupants can die, leaving a normal map death and an owner-local casualty message.
  No new blast radius is introduced. A blocked perimeter uses the dying building's freed footprint.
- The original occupant objects leave, retaining their type, exact health (apart from the
  documented collapse injury), technology and combat identity through evacuation, relocation,
  capture/return and save/load. A destroyed **occupied** zone does not additionally spawn the
  generic half-health Soldiers normally rolled per building tile. Empty zones and ordinary
  structures retain that survivor rule; the per-tile random draws are retained in either case.
  `zone_garrison_identity_probe` checks Trooper-only and mixed garrisons through every exit,
  counts unexpected survivors, and uses a 100% survivor control to expose this distinction.
- **A captured zone is held only while occupied.** It remembers the house it was taken from (saved, format
  9903). Once its garrison is gone, by Evacuate, at low health or because the last soldier inside was killed, it
  returns to that house: owner, tiles, power draw and counts, as when it was taken. A house the capture
  defeated gets nothing back. The zone panel shows `(captured)` after the garrison count.
- Transport recovery (`Game::reconcileTransportContainment`) knows garrisons and leaves them inside.

**Look.** Sandbags drawn over the 2x2 lot after the zone sprite (`ZoneStructure::drawGarrisonSandbags`): bags a
seventh of the lot long and a tenth deep, with a sunlit face, a tie and a dark outline, laid in two staggered
courses on all four sides with the middle bag of the outer course left out as a firing slit, and one outlined
helmet per occupant along the top wall. Bags and helmets use the occupying house's remapped colour ramp
(bright face, body, dark tie, darkest outline), including visible enemy garrisons; fog still hides the overlay.
Original art drawn in code (no imported or C&C sprite); it scales with zoom. The zone
panel shows `Garrison: n/5` and the Evacuate button while it is held.

### Selected-building occupant panel (2026-11-28)

Stefan: "also when clicking on the building show the icon and health bar of each unit and allow up to 5 units to garrison".
The capacity/damage/healing rules are independent of this presentation change.
While an own zone is occupied, its portrait area becomes a three-column/two-row
grid: one unit portrait and its own coloured health strip per live occupant, in
entry order, up to the capacity of five. It uses the same house-specific unit
portraits and health colours as the existing unit interface/selection. The zone
name, count and existing whole-garrison Evacuate button remain below. Clicking an
icon issues no command. Empty zones have no empty slots and regain their building
portrait. Captured zones shorten the count line to `n/5 (captured)` to fit.

No extra panel height is added: the grid replaces the existing 54-pixel portrait
area, with compact 22-pixel status/control rows keeping the full properties column
inside the shared 640x480 logical minimum (including mobile's shared interface).
The count/status row uses 12-pixel text and a separate two-pixel gap before Evacuate,
so the complete `n/5 (captured)` marker stays on one line in the classic bitmap skin.
Physical iPhone/iPad rendering and real-screen readability still need playtesting.

Enemy inspection preserves `mayShowOwnerInterface()`: ordinary players get the
generic building/owner panel, not hidden occupant identities or HP; observers/debug
retain the existing full-panel permission. The on-map sandbags/helmets still identify
a visible enemy garrison. Possible future policies are a visible-enemy count-only
indicator (less information) or the full roster (reveals composition and exact HP);
neither is silently enabled here, and neither should reveal buildings through fog.

| Acceptance | Enforcement / targeted assertion |
|---|---|
| Five actual occupants, correct icon and independent HP | `zone_garrison_panel_probe`: real Capture widget/map click; model equals each hidden unit |
| Live damage and healing | Same open panel after a real zone hit and normal garrison-healing game cycles |
| Fits compact/desktop layout, paints every icon/bar | SDL dummy/software offscreen assertions and PNGs at 640x480 and 1024x768 |
| No stale empty slots or new enemy disclosure | Real Evacuate clears roster/restores portrait; enemy map selection keeps generic inspect panel |
| House-coloured bags and helmets, including enemies | Remapped own/enemy/captured-house pixel assertions at all three zooms; fogged render equals the plain building |
| Captured Hospital marker stays clear of Evacuate | Complete single-line status, capacity-five text and two-pixel gap assertions; modern/lapis/classic offscreen PNGs at 640x480 and Air 960x600 logical at 2x/3x physical scale |


**Save format 9900.** A zone saves its occupants (object ids in the order they entered), its garrison's target
and its search clock. The soldiers themselves are saved as hidden units, as Carryall cargo is.

**Current save format 9907.** Healing remainders, the once-per-crossing relocation flag, in-flight
destination intent and the tunable collapse percentage survive save/load. The obsolete damage
remainders in 9905/9906 are consumed for compatibility, not reapplied under the immunity rule.

## Follow-up (protocol 148, save 9903)

Stefan, 2026-11-27: "ok make 2 changes. 1 make infantry cost 30 credits. next make troopers and infantry
evacuate buildings with an evacuate when it gets below X% health (test different). if the building explodes
they are ejectedb ut are damaged. if units evacuate the building returns to its original owners control.
also try behaviour where the ai tries capuring undefended buildings or mass spamming infantry to try
capture buildings"

- **Price.** The Soldier costs 30 credits (`config/ObjectData.ini.default`, was 60); the Trooper keeps 100.
  The default content is what DuneCity, vanilla and Dune2R games use. Mods with their own `ObjectData.ini`
  (Tornie, ArcaKev, Jericho and their Lite forms, the community mods) keep their authors' prices.
- **Historical evacuation experiments** used `DUNECITY_TEST_GARRISON_EVACUATE_PERCENT`.
  The current 30% relocation rule is deterministic match logic, not a per-process override.
- **Entering.** A capture-ordered soldier enters on any cycle it stands within reach of its zone, not only the
  cycle it stopped (`InfantryBase::checkPos`). It reads its zone's owner every cycle, so it keeps the order when
  a comrade takes the zone first (its cached friendliness made it drop the order). It never fires on the zone it
  walks into: the Dune Legacy capture branch falls through to firing, which classic captures need for red
  health; it fires only on a zone enemy soldiers hold (`UnitBase::engageTarget`). The player-path test found
  all three.

### Historical evacuation threshold measurements

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
survival. These measurements selected 60% for the former damage-and-exit rule. Stefan's
2026-11-28 shelter/30%-relocation instruction replaces that rule; this is not evidence for the new one.

## QuantBot

`[Force Concentration] ZoneGarrison`, `ZoneCaptureRaid` and `ZoneCaptureLevy` (all default **0**; play styles
`zone-garrison`, `zone-capture-raid`, `zone-capture-levy` and `zone-garrison-raid-levy` for all three).
Stateless, each unit pass (`QuantBotGarrison.cpp`), only with the match rule on in a city game.

ZoneGarrison:

1. **Threat.** Every visible enemy ground unit that can attack or capture within 7 tiles of one of our zones adds
   1 to it, 3 for a soldier that can capture.
2. **Defence.** The most threatened zones first: up to `min(5, (threat + 1) / 2)` soldiers, counting those inside
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

## Controls

- **Capture** (C) is the only zone order. Stefan, 2026-11-28: "capture button acts as garrison button if used on
  own r/i/c no need for 2 different buttons, just capture".
  - A Soldier or Trooper sent at an own R/C/I zone garrisons it; sent at an enemy zone it takes it.
  - With the rule on in a city game, the tooltip says both.
  - The separate Garrison button and its I key (added 2026-11-27 after "i cant garison my own buildings there no
    button") are gone.
  - Nothing of them was saved. A profile's old `Garrison` hotkey entry is ignored on load, and the click always
    sent the capture command.
- **Cursor hint:** the Capture cursor shows its symbol over a target the click would order the selection at: an
  own or enemy zone, or an enemy building it can take. Elsewhere it shows a plain pointer, and a click there is
  refused with the cursor kept open.
- **Evacuate:** the zone panel's button, shown while the zone is garrisoned and enabled only for its owner.

## No pulling soldiers away

Stefan, 2026-11-27: "and sto the ai pulling soldiers away on their way in". A soldier on its way into a zone
(capture order, zone target) refuses every AI controller's order except the garrison controller's own:
- moves, hunts and attacks (attack waves, defence pulls, retreats, escorts, idle sweeps);
- formation orders;
- attack-mode changes;
- repair trips;
- Carryall requests.

The match log counts the refusals (`ZONE_GARRISON: event=redirect_refused`, once per soldier per 30 s).
`QuantBot::updateZoneGarrisons` ends the order itself when:
- the zone is gone;
- the zone is held by another house;
- the soldier has not got closer for 45 s.

Every order has a logged outcome:
- `entered`;
- `died_on_way`;
- `refused_at_zone` (full or below the threshold on arrival);
- `order_ended`.

`zone_garrison_real_game` A9 pairs each order with its outcome.

## House to house (QuantBot switches, default off)

Stefan, 2026-11-27: "we want to be fighting house to house basically".
- **ZoneGarrisonReinforce:** Troopers within 16 tiles fill a threatened garrison of ours (own or captured) up to
  five.
- **ZoneFrontGarrison:** our 4 zones nearest a visible enemy building (the front, however far) each get two
  soldiers from within 20 tiles. Front garrisons are not released when the area is quiet.
- **ZoneCaptureChain:** the 2 empty enemy zones nearest any zone we hold (own or captured), however far, each get a
  soldier from within 40 tiles. Every zone taken becomes a new starting point, so the chain moves block by
  block.

Play styles: `zone-garrison-reinforce` (with ZoneGarrison), `zone-front-garrison`, `zone-capture-chain`, and
`house-to-house` (all six switches). Each is registered in `tests/ai/activation-events.json` with its fire
event (the order) and success event (the entry).

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

All level: no switch becomes a default. Raid and levy on `rs192-dunecity` (start from nothing) and Metropolis: HANDOVER.md, "Zone Garrisons follow-up". The house-to-house switches, and every arm re-measured with activation rates, are in the HANDOVER entry "Zone Garrisons: Garrison button, ...".

## Tests

- `zone_garrison_probe` (headless nightly; `tests/units/zone-garrison-probe.inc`): own zone (five
  enter via the player's Capture button, a sixth refused, owner kept), take (zone, tiles, density and power draw move), fire out (kill credited
  to the Trooper), assault (clear a held zone, defeat the evacuated defender, repair and capture),
  manual evacuation (owner's command, an enemy's refused), collapse (normal damage and visible
  deaths after deployment), captured-zone return, and exact 30% relocation boundaries with room,
  reachability, friendly ownership, no-destination retention and saved once-per-crossing state,
  original-owner save/load, rule-off cases, pictures `garrison-zoom0..2.png`, and **the player's own path**: units
  selected as a click selects them, the unit panel's real Capture button (and the C key), a real SDL mouse click
  on the zone through `Game::doInput`, the zone panel's `Garrison: n/5` and `(captured)` text and its real
  Evacuate button; a full own zone, an ally's zone and a damaged zone refused; an enemy's Evacuate disabled.
- The probe also covers:
  - Capture-only controls for Soldiers, Troopers and mixed groups, no separate Garrison button or I key,
    correct cursor hints, sand refusal, own-zone entry and enemy-zone capture;
  - reinforcement, with mixed own and captured garrisons;
  - a garrisoned Trooper hitting an Ornithopter and a tank;
  - no pulling away (nine other controllers' orders refused; the controller's own obeyed);
  - the 30-credit Soldier in the Barracks build list.

- `zone_garrison_real_game` (headless nightly; `tests/ai/run-zone-garrison-real-game.py`): three `rs160-dunecity`
  Brutal games with every switch on (`house-to-house`), one ACCEPT line per request (A1-A12); HANDOVER.md has the
  acceptance table.
- `CoopGameSettingsTestCase`: protocol 144 bit 5 round trip and hash; bit 7 is the first unknown city rule.
- `CityRoadTestCase`, `NetworkHardeningTestCase`: `CMD_ZONE_EVACUATE` is the last command and acts on an owned object.
- `zone_trooper_rush_probe` (headless integration): a free Trooper answers an own threatened shelter
  before ordinary idle/rally work; ground/air proximity and confirmed distant hits trigger the response.
  It prefers one Trooper per zone and replaces at most one Soldier in a full AI-only, guest-free shelter.
  Combat, defence, repair, escort, human and colony/wave reservations are retained; no speed boost.
  The probe requires an order within one second, at least 80% entry within 20 seconds for starts
  within ten tiles, and reports every farther travel/loss deviation plus protected-role samples.
