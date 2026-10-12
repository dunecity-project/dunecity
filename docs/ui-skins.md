# Selectable UI skins

## Using the setting

Open **Settings → Graphics → UI Skin** in the main menu. Choose **Modern**
or **Classic Desert**, then **Apply**. Applying rebuilds the interface through
the existing menu reinitialization path; reopening Settings shows the saved
choice. Back discards a preview without changing the active style.

Modern remains the default for existing and new profiles. A missing or invalid
`[Video] UI Skin` value also selects Modern (`0`); Classic Desert is `1`.
The small Dune City sample previews both the chosen skin and Colors setting
without changing the live menus before Apply.

Classic Desert reuses the original FAME/SCREEN desert artwork. Its buttons have
sand-grained brass-gold faces, carved raised/recessed borders and small end
fittings when enough label space is available. Standard button lettering uses
the original dark red with a pale-gold relief; High Contrast retains dark ink.
Smaller controls keep their existing font sizing and hitboxes, and long labels
omit the end fittings rather than overlapping them. Other labels retain their
contrast-safe colors. The pause window and selected-building sidebar follow
the choice. Black radar and portrait regions remain intentional;
building images and world sprites are not recolored. This is an interface style,
not a flattened mockup or a replacement pack of generated world assets.

## Independent settings

| Setting | What it changes |
| --- | --- |
| UI Skin | Menu, controls, pause and sidebar appearance |
| Colors | Standard or High Contrast lettering for the chosen UI skin |
| Menu Layout | Start-menu navigation arrangement, not its artwork |
| Interface Size | Logical UI resolution, including Native 1:1 |
| World zoom | Existing map zoom preference |
| DuneCity building skin / Dune2R graphics | Mod-specific world artwork and presentation |

Changing UI Skin does not change mod selection, building skins, interface size,
zoom, Dune2R presentation scale, simulation, saves or network synchronization.
Native-resolution fixes already present in 1.0.829 are retained unchanged.
Skin selection is a local preference; it is not synchronized to other players.

For the classic unscaled 1080p view, choose **1920×1080**, **Native (1:1)**
Interface Size and **Zoom 1x** separately. Choosing Classic Desert by itself
does not alter any of those display choices or Dune2R's mod-specific scale.

## Implementation and verification

`MenuPalette.h` defines validated themes and contrast-safe label colors.
`DuneStyle` captures a theme per instance so a preview cannot alter the active
theme. Startup loads the preference before graphics caches are constructed.
Classic backgrounds use original bitmap resources; the safe fallback is a gold
fill when an original texture is unavailable. Applying in Settings reconstructs
the cached UI rather than leaving old-theme widgets behind.

The `classic_desert_ui_tests` CTest covers theme defaults, invalid values,
Modern palette preservation, Classic contrast, preview isolation, preference
storage and independence from interface/world scaling. Button palette checks
also cover red/gold lettering, the deeper pressed-state ink, High Contrast and
unchanged Modern colors. The renderer fixture checks opaque textured Classic
button faces, real glyph/emboss bounds and normal/pressed/focused states at
280×34 and 70×22 in both text palettes; Modern interiors remain flat.

For Windows visual diagnostics, `tests/menu/run-classic-skin-probe-windows.py`
links a private entry point against a completed Visual Studio Release build.
It uses an isolated profile and exercises actual menu objects, both skins,
preview/Back, Apply/reinitialization and persisted selection. PNG captures are
rendered by SDL, not generated concept images. The optional HUD probe uses the
bundled Harkonnen map in an offline Vanilla custom-game fixture. The probe is
not part of the shipped binary. Its output directory must be on NTFS because
the private runner uses asset-directory junctions.
The Windows diagnostic replaces only its private entry point and profile-path
object; the latter redirects user-file requests to a mandatory test directory.
The production Windows profile resolver and the player's existing settings are
not modified. Its dummy-display fixture holds each requested logical size for
layout verification; it is not an assertion of an interactive monitor capture.
Wait for the entire game build, including its asset-copy targets, to finish
before running this probe. On Windows the probe holds runtime PAK files open;
overlapping it with `copy_data` can fail the copy with a sharing violation.

The 1.0.830 Windows candidate passed all 11 CTest suites. The actual SDL
menu-rendering matrix passed at 640×480, 854×480, 1280×720 and 1920×1080, each
switching Modern → Classic Desert → Modern. It checked control bounds,
preview/Back isolation, Apply/reinitialization and saved selection, including
Native 1:1 at 1080p. Interface size, world zoom, building skin and menu layout
remained unchanged. Font-pixel checks also preserve white outlined labels over
world/original Mentat artwork while using contrasting dark labels on gold.
The optional static HUD/pause fixture also passed in both skins at 640×480 and
1920×1080. It renders the real selected-construction-yard build sidebar and pause
window without entering the game loop; it is not a live gameplay or general
building-properties-panel test.

The feature was subsequently carried onto production 1.0.832 source as a
separate local 1.0.833 candidate, retaining all newer upstream gameplay,
protocol/save and QuantBot changes. The same four-size skin-switch matrix,
including static construction-yard HUD/pause checks at every size, passed
again against the refreshed game executable. The complete Windows Release build
and all 11 CTest suites pass on that integration, and the separate portable
install passes its runtime check. These local candidates are not published
releases; broader evidence and the play-test handoff are recorded in HANDOVER.md.

Local 1.0.834 adds the textured brass buttons and red/gold typography described
above without altering the approved background. The full Windows Release build
and all 11 CTest suites pass; the Classic theme subset now has seven cases and
337 assertions. The actual four-size SDL skin-switch/HUD/pause matrix and the
new button-pixel checks pass, including compact controls, both text palettes,
and normal/pressed/focused states. Its separate portable install passes the
desktop runtime check. It remains a local play-test candidate, not a published
release.
