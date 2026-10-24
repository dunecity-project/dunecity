# Community mod review — 2026-10-24

Source: the [community catalogue](https://dunelegacy.com/p2p/v1/content/) and its
[documented API](../tools/p2p-signaling/README.md#workshop-maps-and-mods).
All **167 revisions / 163 item IDs** were obtained, with sequential delayed requests;
latest revision per item was selected. Existing local or earlier downloaded blobs
were reused only after SHA-256 and length verification. **2,197 unique files** were
verified. No downloaded code was executed.

`mods/community-mods.json` records the accepted files' exact hashes/sizes, catalogue
item IDs, revision hashes and version labels, and rejected items/reasons. The three
accepted payloads total **10,736,436 bytes** (1,544 catalogue files), before tiny
identity sidecars. Their catalogue files are unchanged. They are optional choices,
not the default and not newly approved multiplayer mods.

## Inventory and verdicts

Sizes are uncompressed catalogue payloads, not download/archive sizes. “Bundled”
means selected by the content review; runtime acceptance is separately recorded below.

| Mod | Base | Size | Verdict | Review |
| --- | --- | ---: | --- | --- |
| deo-t | Tornie | 5.11 MiB / 770 files | Bundled | Tornie with Sandworm disabled; inherited units, houses, art, sound and campaigns unchanged. |
| TornUge | Tornie | 5.11 MiB / 770 files | Bundled | Tornie with concrete required and instant build off; normal production rather than instant construction. |
| new - modded | `new` (Vanilla-style engine family) | 28.0 KiB / 4 files | Bundled | TijnAG07's retro turret/AI preset; original rules for Scattered War. |
| aa | Dune2R | 60.2 KiB / 7 files | Rejected | Placeholder name/default editor description; no gameplay difference from its historical Dune2R source. |
| aaaa | Dune2R | 59.3 KiB / 7 files | Rejected | Cheat/test Sonic Tank: 99,000 HP, price 1, damage 5,000, build time 1. |
| C&C on Arrakis | Vanilla | 200.6 KiB / 80 files | Rejected | Local GDI/Nod prototype with copyrighted C&C artwork; no redistribution grant supplied. |
| Dune City Tornie | Dune City | 26.49 MiB / 793 files | Rejected | Not a combined Tornie/city mod: only disables Deviator and sets an ineffective Advanced Windtrap enable flag. |
| GMD-8 | `new` | 27.9 KiB / 4 files | Rejected | Metadata-only duplicate of new - modded's three gameplay INIs. |
| Vanilla (modified snapshot, `cb9892b4…`) | Vanilla | 32.7 KiB / 4 files | Rejected | Unlabelled experimental speeds/ambient-aircraft builders, falsely described as default rules; must not replace shipped Vanilla. |

There are **eight distinctly named custom items plus one modified item labelled
Vanilla**. The latter was discovered by content comparison, not by its name.

## Accepted mods

### deo-t — catalogue v1

Installed as `community-93d3ab43ccc64ade`. Compared with shipped Tornie, the only
non-metadata change is `[Sandworm] Enabled = false`. ObjectData's other unit and
building stats, GameOptions and QuantBot config are unchanged. The complete
Tornie houses, custom buildings/units, graphics, voices, palettes and campaigns
are inherited, not new contributions by this uploader. This is a simple no-worm
rule variant, not evidence of better balance. No bundled community map requires it.

Its media, campaign files and attribution documents are identical to shipped
Tornie. `PROVENANCE.md` credits Tornie_Panther and preserves inherited third-party
cautions. The uploader identifies only as `User`; that does not replace upstream
credit. No new third-party artwork or sound was found.

### TornUge — catalogue v1

Installed as `community-c8baed9d35d53315`. Compared with shipped Tornie,
`Concrete Required` changes false → true and `Instant Build` true → false.
ObjectData, CustomHouse, QuantBot config, all graphics/sound and all campaigns
are unchanged. This is a normal-construction Tornie rules variant, not a new
unit/house pack or a measured balance improvement. Provenance/licensing cautions
are the same inherited Tornie ones as above.

**SONOB** pins this exact revision. It was reconsidered, not silently forgotten:
its title advertises nine players but it has only four recognised lobby sections,
plus an unrecognised `Corruptique` section. Harkonnen and Atreides do have MCVs in
time-zero reinforcements, so the earlier blanket “no starts” description was too
strong. TornUge's two option changes cannot create the missing nine-player roster
or repair that house section. The map remains rejected as authored, rather than
rewritten or relabelled. No accepted bundled map currently needs TornUge.

### new - modded — catalogue v4

Installed as **`new`**, preserving the original engine-base identity: its mod.ini
has empty `Base Mod`, and the catalogue base is `new`. Renaming that folder to an
arbitrary bundle alias would change its content family/fingerprint. The menu still
displays **new - modded**, and the four authored files are unchanged. Author:
**TijnAG07**.

Compared with its closest historical shipped defaults, the deliberate ObjectData
changes are gun-turret reload 240 → 70 and rocket-turret reload 360 → 70. QuantBot
structure targets change gun turrets 150 → 1, rocket turrets 75 → 2 and walls
30 → 0. Its older baseline also retains older vehicle/aircraft speeds, launcher
reloads and difficulty budgets; those differ from today's defaults and are part
of this pinned preset, not fixes applied to the shipped mods. In particular,
today's shipped rocket-turret reload is 170, versus this preset's 70.

There are no new graphics, sounds, houses, units or campaigns. Old absent
Tornie-only ObjectData sections do not turn this into Tornie. The legacy
`Structures Degrade On Concrete = true` key is retained for byte identity but is
**ignored by the current engine** (the old option was removed); this preset is
not a promise to reproduce every historical engine rule. Faster turrets and
smaller AI turret targets materially affect play; balance has not been established.

**Scattered War** now selects this exact v4 dependency through its workshop
sidecar, not Vanilla. The map bytes/revision are unchanged. Its previous two-minute
Vanilla digest is retained only as `previous_validation`, not presented as proof
of a game in the newly associated mod. This INI-only rules contribution has no
new third-party media; it inherits the base game's existing asset requirements.

## Rejected mods

### aa and aaaa

`aa` changes only mod metadata from a historical Dune2R snapshot: default `User`
author, placeholder name and generic editor description. No art packs are included.
`aaaa` is a real stat edit, but an obvious cheat/test rather than a balanced release:
its Sonic Tank has 900 times normal HP (99,000 versus 110), price/build time 1,
damage 5,000, range 12, view 40, reload 80 and speed 10. The latest revision is v2;
the earlier v1 was also downloaded. Both contain only INI/Markdown files, no
executable content, and no new assets/campaigns/houses or QuantBot edits. They
were excluded before runtime acceptance, not claimed to crash.

### C&C on Arrakis

The README calls this a **local prototype** for a locally modified DuneCity build.
It adapts Dune units/buildings to GDI/Nod: tanks, Humvee/Buggy, infantry/rocket
soldiers, Artillery, power/refinery/factory/barracks and defensive structures.
`Presentation.ini` requests C&C house/item names. ObjectData changes many builders,
prerequisites and stats, disables city/special units, and GameOptions disables
concrete, enables fog and limits palaces. QuantBot config is unchanged. There are
73 PNGs, five INIs, a README and an import-provenance JSON; no sound/campaign pack.
PNG integrity checks passed, but that is not proof that a local-engine prototype
works completely in this unmodified engine.

The README explicitly says the images came from the user's classic Command &
Conquer files and should be regenerated locally, not tracked as distributed media.
`graphics_compact/import-manifest.json` identifies `CONQUER.MIX/*.shp` inputs and
their hashes. Attribution/source hashes and possession of a game copy do **not**
provide a redistribution grant. No such grant accompanies this upload. Its
copyrighted media is therefore **not bundled**, and it was not executed or used
for an acceptance game.

### Dune City Tornie

All media are an older shipped Dune City skin set, not newly supplied Tornie art.
Against its contemporary Dune City source, only Advanced Windtrap's enable flag
(false → true) and Deviator's enable flag (true → false) change beyond metadata.
The Advanced Windtrap still has `Builder = Invalid`; this flag alone does not
make it buildable. There are no Tornie houses, unit pack, voices or campaigns,
and it retains city mode. QuantBot is unchanged. It is omitted as an incomplete,
misleading hybrid with 26.49 MiB of duplicated city art, not falsely described as
a proven combined mode or a crash. PNG files passed integrity checks.

### GMD-8

The ObjectData, GameOptions and QuantBot files are byte-identical to new - modded
v4. Only the name/author/description/base metadata differ. It has no additional
assets, houses, units, campaigns or maps. Keep the attributed original rather than
presenting two identical rule presets as independent new content.

### Modified “Vanilla” — catalogue item `cb9892b4d000033363da1f26662b50ae`

Revision `3d771f0a…` is not a historical shipped rules snapshot. Its ObjectData
has no matching blob in committed default-rule history: Harvester speed changes
1.28 → 1.7 and turn speed .03 → .13; Soldier speed .56 → 1.56; Trooper speed
1.12 → 1.5; ambient airplane/helicopter gain Heavy Factory builders. The metadata
still says Vanilla / Default game settings and credits Dune City, without an
independent authored identity or explanation. These experimental assignments are
not approved default rules and are not imported over the protected built-in mod.
There are only four INIs; no new media/campaign/QuantBot content.

Wezowe Pola was published with this revision. Its provenance note is corrected;
it remains tested/bundled with current Vanilla, **not** misrepresented as retaining
those custom speeds. The other referenced map, North vs, is not a new bundled map.

## Shipped copies — counts only

| Shipped family | Copies (latest item revisions) | Current-equivalent | Older payload/defaults |
| --- | ---: | ---: | ---: |
| Dune City | 73 | 9 | 64 |
| Vanilla | 65 | 10 | 55 |
| Dune2R | 13 | 2 | 11 |
| Tornie | 3 | 1 | 2 |
| **Total** | **154** | **22** | **132** |

Comparison normalises LF/CRLF, excludes mod.ini Version/Game Version stamps and
Workshop-excluded dotfiles, and compares the Dune2R installer shell separately
from its official optional downloaded art. These are current-equivalent copies,
not a claim that every Windows text file had identical line endings. Older copies
retain earlier ObjectData/QuantBot defaults, the removed degradation option,
city concrete/manual-drop defaults, smaller/older city skin sets or Dune2R asset
catalogues. Tornie's older variants have earlier reload/options/checksum manifests.
Their ObjectData and QuantBot variants match committed source history. They are
not installed as “new” mods; no shipped folder is modified. Per-item changed-file
lists are in the external review evidence, rather than 154 duplicate catalogue rows.

## Safety, compatibility and runtime acceptance

- All 167 manifests passed portable relative-path, traversal, device-name,
  case-collision, file/directory-collision and size-limit checks. All referenced
  lengths and SHA-256 values were verified; all custom PNGs parsed successfully.
- File types are configuration/documentation, PNG, and inherited Tornie
  VOC/OGG/CPS/PAL/PAK data. No scripts, executables, executable signatures or
  symlinks are imported. Media parsing is not a claim of complete runtime coverage.
- New paths are `-text` in `.gitattributes`, preserving catalogue bytes on Windows.
  Existing user mod folders are never overwritten by generic first-install seeding;
  selecting a new mod remains an explicit player action.
- Existing mods' effective checksums, installer/approved tokens and multiplayer
  content fingerprints are unchanged. A selected community mod uses its own exact
  catalogue fingerprint and effective rules checksum. The four-name approved list
  stays Vanilla, Dune City, Tornie, Dune2R; community games still require the normal
  authored-mod lobby/transfer flow. No protocol or save-format change is introduced.
- `menu_navigation_probe` actually selects the three mods through Custom Game's
  dropdown, activates rules/assets, checks the exact catalogue fingerprint and
  checks that none acquired built-in approval, then restores the prior selection.
- New `community_mods_startup` checks the recorded bytes, then runs each mod twice
  for **five game minutes**, seed 4242, using its real GameOptions and two AI houses.
  Starting from bare yards, it requires a factory, power plant and combat vehicle
  to appear, plus matching final digests. Separate rock/spice islands avoid an early
  conquest ending the production smoke. This is load/production/determinism
  coverage, **not a balance certification**. `community_maps_startup` also loads
  Scattered War under its corrected mod association.

**Parent runtime verification:** source `cdf9b101`, cluster job
`20261024T162952-combined-cdf9b101-e51206`: `community_mods_startup` passed on the
MacBook Air in 98.00 s, with matching five-minute digests for both runs of each
mod. `community_maps_startup` passed there too (165.61 s); Scattered War ran in
authored `new` with all eight lobby houses surviving at cycle 3750. The preload
payload checker passed on the mini; this is not a claim of a browser package run.

Source `09441f0b`'s shared-build `menu_navigation_probe` passed in 355.48 s at
640, 854 and 1280 widths. The fixture now supplies the actual `CustomPlaySetup`,
which owns the mod-selection callback, rather than an inert legacy chooser.
All three catalogue identities/non-approval checks passed, and the three
640-wide selected-mod frames were visually inspected.

The final combined source `e286003d100a` then passed **118/118 CTests** in
job `20261024T213618-combined-vuk-final-3ee6bc`, using all four hosts. This
includes `community_mods_startup` (Air, 67.5 s), `community_maps_startup`
(Air, 80.0 s), `menu_navigation_probe` (mini, 290.1 s) and the payload check
(Air). The startup/chooser integration is accepted; this is still not a
balance certification or an exercised browser-package claim. Rejected items
were excluded on the review grounds above, not falsely reported as runtime-tested.
