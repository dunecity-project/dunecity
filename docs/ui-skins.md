# Selectable UI skins

## Using the setting

Open **Settings → Graphics → UI Skin** in the main menu. Choose **Modern**,
**Lapis Enamel** or **Classic Desert**, then **Apply**. Applying rebuilds the interface through
the existing menu reinitialization path; reopening Settings shows the saved
choice. Back discards a preview without changing the active style.

Classic Desert is the default for new profiles and profiles without a saved
`[Video] UI Skin` value. Explicit existing selections are preserved. Invalid
values safely select Modern (`0`); Lapis Enamel is `1` and
Classic Desert is `2`.
ID `1` was labelled Classic Desert in the earlier local candidates, so existing
preferences keep the approved appearance without migration. **Classic Desert**
is a separate, original-DuneLegacy-inspired design introduced in local 1.0.838.
The small Dune City sample previews both the chosen skin and Colors setting
without changing the live menus before Apply.

Lapis Enamel reuses the original FAME/SCREEN desert artwork. Its buttons have
sand-grained brass-gold faces, carved raised/recessed borders and small end
fittings when enough label space is available. Standard button lettering uses
deep lapis-blue faces and a fine one-pixel pale-gold relief, without a heavy
dark shadow or hollow face. High Contrast retains dark foreground ink. Roomy
buttons and large headings use an arched Art Deco display face inspired by the
approved Lapis mockup; small controls/body text retain the original font. Button hitboxes stay
unchanged, and long labels
omit the end fittings rather than overlapping them. Other labels retain their
contrast-safe colors. The pause window and selected-building sidebar follow
the choice. Black radar and portrait regions remain intentional;
building images and world sprites are not recolored. This is an interface style,
not a flattened mockup or a replacement pack of generated world assets.

Classic Desert shares the authentic desert backdrop. Since local 1.0.839,
its selected **2B / Chunky Classic** treatment has brighter sand-blasted ochre
button faces, fine limited-color sandy grain and small raised/recessed bevels.
Buttons use an original hand-authored bitmap interpretation of the approved
arched Deco lettering, not a downsampled TTF or text baked into button images.
Button labels uppercase ASCII; large menu headings also use the pixel face.
Small body labels and explicitly outlined world/Mentat text keep Philosopher.

Standard button ink is a slightly lighter copper-orange, with a crisp pale-gold
one-pixel engraving highlight. Sparse darker grain is confined to thick glyph
interiors; edges and thin strokes remain intact. Compact labels and High Contrast
skip glyph grain. The lighter ink is used only on the brighter control face;
body text stays dark for the original gold backdrop. Button face colors and the
minimum grain values under lettering exceed 4.5:1 against the corresponding
ink. This does not describe every decorative border or background pixel.
Classic has no Lapis carved bands or decorative end fittings. Modern and Lapis
are unchanged; there is no whole-window downscaling or gameplay zoom change.

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
Interface Size and **Zoom 1x** separately. Choosing either desert skin by itself
does not alter any of those display choices or Dune2R's mod-specific scale.

## Lapis display lettering

The shipped face is unmodified **Righteous Regular**, rendered at 80% horizontal
width for the taller proportions of the Lapis-inspired mockup. This is a real
font, not text baked into button images. Only the display face uses light
hinting/antialiasing and a transparent, straight-alpha compression intermediate;
the default font's existing rendering path stays unchanged. Lapis buttons
uppercase ASCII while leaving UTF-8 byte sequences intact. Buttons at least
28 pixels high use the display face with measured fitting; labels at least 22 pixels use it for large
headings. Small body text and black-outlined labels over world/Mentat artwork
keep Philosopher. Modern keeps its existing/default font rendering, metrics
and colors. Classic Desert uses its separate bitmap font path as described below.

Display/default faces have separate caches. If Righteous lacks any character,
the entire label uses Philosopher, with matching fallback metrics. Missing or
damaged optional font files also fall back rather than preventing startup.
Standard Lapis Enamel lettering replaces the low-contrast copper experiment
from local 1.0.835. The blue foreground exceeds 4.5:1 against the nominal and
minimum central brass colors in both normal and pressed controls; this does
not imply every antialiased edge or decorative border meets that ratio.
Use **Colors → High Contrast** for dark foreground lettering instead of blue.

The 43,104-byte `data/Righteous-Regular.ttf` comes from
[Google Fonts revision 90abd17](https://github.com/google/fonts/blob/90abd17b4f97671435798b6147b698aa9087612f/ofl/righteous/Righteous-Regular.ttf).
Its SHA256 is
`2FFB3FE5C27D7E6571210B800448C4E234E651B46C6B4426C1BB567E5341348A`.
The complete copyright and SIL Open Font License 1.1 accompany it in
`data/Righteous-OFL.txt`; the font binary/name are unchanged. Windows install
rules explicitly include both loose files, since neither is a LEGACY.PAK entry.

## Classic pixel lettering

The new bitmap face has its own character masks and font cache. It provides
printable ASCII, with lowercase mapped to the corresponding capital forms.
Measured advances and rendering use the same nearest-neighbor glyph sizing;
there is no filtering or subpixel antialiasing. Unsupported text uses the
complete original Philosopher label, including matching fallback metrics and
pixels. The default and Lapis font paths are not changed.

This is an original interpretation of the chosen concept, not a claim to ship
the image generator's exact letterforms or the original Dune II typeface.
The original Dune II bitmap font resources are present in the legacy PAKs as
historical references, but they are not replaced or repackaged by this change.

## Original DuneLegacy references for Classic Desert

Lapis Enamel is the approved new blue/gold treatment, not a reproduction of
the historical interface. Classic Desert started from those original controls,
using dark-orange rather than red lettering at the user's request, and now adds
the approved sandy 2B pixel-font interpretation. A genuine
main-menu/options pair appears in this
[2019 hands-on review of the DuneLegacy 0.96.3 AmigaOS port](https://oldschoolgameblog.com/2019/12/02/gaming-on-amigaos-4-1-dune-legacy-dune-legacy-team-zettmaster-and-big-h/).
The archived official 0.98.6.3 release also includes `extra-menu-option.png`,
showing the red-lettered settings screen with ochre controls, small bevels,
the original desert texture and sculpted gold framing.

This is appearance-only: current tabs, campaign/co-op features, editors,
navigation and settings remain. It does not restore obsolete menu feature sets
or relabel the approved Lapis design as faithful Classic.

## Implementation and verification

`MenuPalette.h` defines validated themes and contrast-safe label colors.
`DuneStyle` captures a theme per instance so a preview cannot alter the active
theme. Startup loads the preference before graphics caches are constructed.
Both desert backgrounds use original bitmap resources; the safe fallback is a gold
fill when an original texture is unavailable. Applying in Settings reconstructs
the cached UI rather than leaving old-theme widgets behind.

The historically named `classic_desert_ui_tests` CTest covers theme defaults,
invalid values, Modern/Lapis palette preservation, Classic contrast, preview isolation,
preference storage and independence from interface/world scaling. Button palette checks
also cover lapis-blue/pale-gold colors, the deeper pressed-state ink, High Contrast and
unchanged Modern colors and the Classic copper palette. The renderer fixture checks opaque textured Lapis/Classic
button faces, real glyph/shadow bounds and normal/pressed/focused states at
280×34 and 70×22 in both text palettes; Modern interiors remain flat. Font checks
verify the bundled typeface actually loads, display/default measurement and
rendering agree, their caches stay separate, and Cyrillic uses a whole-label
fallback with the original pixels.

For Windows visual diagnostics, `tests/menu/run-classic-skin-probe-windows.py`
links a private entry point against a completed Visual Studio Release build.
It uses an isolated profile and exercises actual menu objects, all three skins,
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

Local 1.0.834 introduced textured brass buttons and the earlier red/gold
typography without altering the approved background. The full Windows Release build
and all 11 CTest suites pass; the Classic theme subset now has seven cases and
337 assertions. The actual four-size SDL skin-switch/HUD/pause matrix and the
new button-pixel checks pass, including compact controls, both text palettes,
and normal/pressed/focused states. Its separate portable install passes the
desktop runtime check. It remains a local play-test candidate, not a published
release.

Local 1.0.835 adds the copper Art Deco display treatment. The complete Windows
Release build and all 11 CTest suites pass; the Classic subset has seven cases
and 346 assertions. The four-resolution SDL matrix passes again, including
static HUD/pause rendering and checks of the real display font, accented Latin,
invalid UTF-8, whole-label fallback, isolated caches and unchanged Modern/world
labels. The separate portable installation passes its desktop runtime check;
its executable, font, license and configuration template match the build/source
copies. This is a local play-test candidate, not a published release.

After play-testing the copper candidate, the user requested a return to
**Lapis Enamel** in local 1.0.836. Standard buttons and the Classic main-menu
mod title now use deep blue; button relief is pale gold. The font renderer,
metrics, brass artwork, layout and Modern appearance remain unchanged. All 11
Windows CTest suites pass (Classic subset: seven cases, 352 assertions), as do
the four-resolution SDL skin-switch/HUD/pause matrix and portable runtime check.
This remains local-only, with previous portable candidates preserved.

Local 1.0.837 gives the approved appearance its own visible **Lapis Enamel**
name. Saved ID `1`, default selection and rendering are unchanged. The native
probe asserts the names and IDs, rejects an unfinished Classic Desert entry,
and verifies saved selection, preview/Back and Apply/reinitialization. The full
Windows build, all 11 CTest suites, the four-resolution SDL matrix and separate
portable runtime check pass. Classic Desert remains a planned distinct skin.

Local 1.0.838 implements the separate **Classic Desert** skin as ID `2`, with
original-style ochre controls and dark burnt-orange Philosopher lettering.
IDs `0`/`1` and their appearances remain intact. The full Windows Release build
and all 11 CTest suites pass; the skin subset has eight cases and 851 assertions.
The actual SDL Modern → Lapis → Classic → Modern matrix passes at 640×480,
854×480, 1280×720 and Native 1920×1080, including static HUD/pause rendering
and the original outlined world/Mentat labels. The 148 native PNG captures are
local diagnostic evidence, not generated concepts or live gameplay certification.
Modern/Lapis main-menu pixels match the 1.0.837 candidate at every size outside
the updated version label. The separate portable installation passes its
desktop runtime check; executable, font, license and template copies match.
This remains a local play-test candidate on the production 1.0.832 source base,
not a newer-main integration or a published release.
