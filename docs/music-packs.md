# Optional local soundtrack

Settings → Audio → **Alternative soundtrack** is off by default. It uses a local
pack at `<user data>/music/dune2-remake/manifest.ini`; on macOS, user data is
`~/Library/Application Support/Dune City`. `DUNECITY_USERDIR` overrides that root
for isolated profiles. Apply switches the player immediately, without changing
`Music Type` (`adl`, `xmi`, or `directory`). Missing packs, missing slots/files,
and files SDL_mixer cannot decode use that existing backend instead. The option
can be enabled after a valid pack is installed; an unavailable saved pack can
still be switched off.

The pack is local presentation data, independent of the active mod. It is not
included in workshop revisions, multiplayer transfers, or content hashes.
Dune2R and other graphics mods keep their normal music unless this option is on.
**Installing or selecting a pack is not permission to redistribute its audio.**
Do not add private audio or private download links to Git, release packages,
public hosting, or workshop uploads without the author's release approval.

## Manifest format 1

Use an INI file (UTF-8), with one section per music slot. Each key in a slot is
one track; key names are arbitrary and entries are selected at random. Paths
are relative to the pack directory; quoted paths can contain spaces. Absolute
paths, `.`/`..` path components, and symlinks outside the pack are ignored.
Supported extensions are `.ogg`, `.mp3`, `.wav`, `.flac`, and `.mid` (case
insensitive); actual decoder availability depends on the SDL_mixer build.
Keep source originals when converting to Ogg Vorbis with ffmpeg.

```ini
[Soundtrack]
Format = 1
Name = My alternative soundtrack
Author = The credited musician
YouTube = https://www.youtube.com/@the-author-supplied-channel

[peace]
1 = "tracks/quiet one.ogg"
2 = tracks/quiet-two.mp3

[attack]
1 = tracks/battle.ogg

[intro]
1 = tracks/introduction-without-dialogue.ogg

[credits]
1 = tracks/credits.ogg
```

`Format = 1`, a nonempty `Name`, and at least one existing supported audio file
are required. `Author` and `YouTube` are displayed verbatim in About and the
Settings tooltip, even when the option is off. Missing credit fields are shown
as not supplied; never guess a channel URL. The intro should omit recorded
narration because the game's intro already plays dialogue.

Available slots, matching the existing directory player, are `attack`, `peace`,
`intro`, `menu`, `briefingH`, `briefingA`, `briefingO`, `winH`, `winA`, `winO`,
`loseH`, `loseA`, `loseO`, `gamestats`, `mapchoice`, `meanwhile`, `finaleH`,
`finaleA`, and `finaleO`. `credits` adds About-screen music; without a replacement
it uses menu music. Leave slots out rather than assigning unrelated songs to
them (in particular, menu and mission selection if the pack has neither).

The runtime log records `Alternative soundtrack: slot=... file=...` for a pack
track and `Alternative soundtrack fallback: slot=... backend=...` followed by
the existing backend's filename for fallback. In-game tracks play once, then
the existing music check selects the next peace track. F11 and music-volume
controls apply to both backends. Run parser/slot/fallback tests with:

```sh
ctest --test-dir build-int -R '^soundtrack_pack_tests$' --output-on-failure
```
