# Sidebar build grid

The sidebar build list (`BuilderList`) used to show one column of 91x55 buttons, so a
DuneCity Construction Yard (25 items with every yard item unlocked: the vanilla structures
plus R/C/I zones, Road, Power Line, Police, Stadium, Airport and Nuclear Plant) showed 7
items at 1024x768 and the player scrolled constantly. The list is now a two-column grid of
scaled-down icons. Every `BuilderList` (Construction Yard, factories, Barracks/WOR,
Starport, Chaos Factory) uses it.

## Layout

All geometry lives in `include/GUI/dune/BuilderListLayout.h`, a header-only integer layout
that `BuilderList` draws and hit-tests with and that `tests/BuilderListLayoutTestCase.cpp`
checks directly.

| Part | Value |
| --- | --- |
| Widget width | `SIDEBARWIDTH - 25` = 119 px, the whole object-interface column |
| List body (black) | x 3..111 (108 px); cap art overhangs 3 px left, 8 px right |
| Columns | 2 |
| Button | 51x31 px: the 91x55 picture at 0.56, aspect ratio kept (31.3 rounded) |
| Margin / gap | 2 px left and right of the grid, 2 px between buttons both ways |
| Row pitch | 33 px; 5 px of black above the first and below the last row |
| Fixed height | 71 px for arrows, insets, Order button and spacing |
| Rows | `(height - 71) / 33`; slots = rows x 2 |

- **Pictures** are fitted into the button (aspect kept, centred, never enlarged) with linear
  filtering for the draw only; the texture's own scale mode is restored afterwards.
- **Scrolling** moves one row (two items) for the arrows and the mouse wheel. The first
  visible item never goes past `maxFirstItem`, so the bottom visible row always holds items.
  A resize clamps the position the same way.
- **Hit-testing**: `getButton(x,y)` returns the build-list index under the point, or -1 for
  margins, the 2 px gaps, the arrow and Order strips and empty slots after the last item.
- **Tooltips** (full name, `Hotkey: P`, Alt+click repeat hints) appear left of the list at
  the hovered row, whichever column is hovered.
- **Price** (12 pt, bottom left), **queued count** (red, bottom right), **progress fill**
  (scaled to the button width) and the **repeat loop marker** (top right) keep their
  positions relative to the button.
- **Structure-size lattice** is drawn at two thirds of its size (4 px cells instead of 6).
- **Overlays** (PLACE IT, ON HOLD, SOLD OUT, ALREADY BUILT, UNIT LIMIT, HARVESTER LIMIT) are
  rendered at the largest font size from 12 down to 8 pt whose lines fit the button.
  `UNIT LIMIT\nREACHED` and `HARVESTER LIMIT\nREACHED` did not fit at any size; they are
  now `UNIT\nLIMIT` and `HARVESTER\nLIMIT` (new msgids, untranslated until the .po files
  are updated). A harvester at its limit now shows HARVESTER LIMIT; before, the dedicated
  texture existed but the list always drew UNIT LIMIT.
- **Starport Order button** spans the list body; sold-out items are dimmed with SOLD OUT.
  The Chaos Factory's exhausted stock uses the same SOLD OUT overlay.
- **Caps**: the upper and lower cap art (cut for a 101 px list) keeps both ends and repeats
  a plain 20 px middle strip to span the 108 px body; the brown divider is drawn at the
  body's right edge, as before.

Three columns were rejected: in the 108 px body a button would be 33x20 px, too small to
hold the 12 pt price under the structure-size lattice.

## Items visible

List heights are the object-interface heights at native 1:1 interface size
(renderer height - 228). Measured in the real engine by the sidebar grid probe.

| Viewport | List height | Before (slots) | After (slots) | DuneCity yard, 25 items: before → after |
| --- | --- | --- | --- | --- |
| 1024x768 | 540 | 7 | 28 | 7 → 25 (no scrolling) |
| 1920x1080 | 852 | 13 | 46 | 13 → 25 (no scrolling) |

## Evidence

- `sidebar_grid_tests` (`[sidebar-grid]` in `dunelegacy_tests`): for heights 0..853 and
  0..100 items, slots = rows x columns that fit, every slot is a unique non-overlapping
  rectangle inside the widget and the list body, corners and centre hit their own item,
  the pixel past each edge hits nothing, scrolling moves one row and ends with the last
  item visible and no empty row, and 1024x768 / 1920x1080 show at least twice the old
  column.
- `sidebar_grid_probe_1024x768`, `sidebar_grid_probe_1920x1080`
  (`tests/ai/sidebar-grid-probe.inc` via `run-campaign-balance.py --sidebar-grid-probe`):
  selects a DuneCity Construction Yard, a Heavy Factory with a repeating unit in production
  and three queued, and a Starport with a sold-out item through the real selection path,
  checks the widget width, every slot through `getButton`, the overlay text sizes and the
  yard fitting unscrolled, hovers an item for its tooltip, and renders `Game::drawScreen`
  to `sidebar-{yard,yard-placeit,factory,factory-onhold,starport}-<viewport>.png`.
- The local spectator probe now expects one wheel step to move `BuilderListLayout::Columns`
  items.
