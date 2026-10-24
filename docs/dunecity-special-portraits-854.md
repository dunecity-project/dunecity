# Special-building portraits and missing variants — 1.0.854 candidate

Adds 39 dedicated landscape portraits and seven previously missing map-building
variants to DuneCity's Dune2 graphics skin. The construction menu and selected
building panel can now use a matching portrait for every installed faction/type
package, rather than falling back to a map sprite or the original icon.

The 32 existing special buildings without portraits receive new icons. The seven
new buildings receive both their map sprite and a matching portrait:

| Building | New faction variants | Compact canvas | Logical footprint |
| --- | --- | --- | --- |
| Hospital | Ordos, Fremen, Sardaukar, Mercenary | 128×128 | 2×2 tiles |
| Nuclear Plant | Fremen, Sardaukar, Neutral | 256×256 | 3×3 tiles |

All new portraits use 182×110 pixels: twice the native 91×55 landscape icon,
with balanced reduction and opaque scenery. Their architecture follows the
accepted map sprite, translated into a coherent elevated/front perspective.
Roofs remain roofs rather than becoming upright facade shapes. Crops retain
important tower silhouettes. Approved House emblems are used where available;
other factions use their colors without invented House insignia. Sardaukar
accents are visibly purple and Rebels use grey/charcoal.

Coverage after this pass is 48 special-building variants (six types across eight
factions), plus 24 R/C/I packages, with 72 dedicated portraits. The seven new map
sprites follow the existing four-times-native source-canvas convention and the
same fixed engine destinations. This pass changes no zoom, placement, collision,
save, multiplayer or simulation behavior, and changes no Dune2R assets.

All 849 pre-existing skin files remain byte-identical to the preceding Compact
candidate, including the 33 previous portraits, R/C/I cells and 384 Industrial
activity frames. The addition consists of 39 icon PNGs, seven map PNGs and seven
building INIs, adding 2.25 MiB. Only production Compacts are packaged; source artwork and private
authoring records are excluded.

Validation covers exact new-file scope, complete faction/type mappings, frame
dimensions and transparency, all 72 portrait sizes/opacity, accepted-output
hashes and unchanged previous assets. Eight skin packaging tests and three
browser packaging tests pass; version metadata remains consistent at 1.0.854.
The local portable test copy receives the exact verified assets while retaining
the previously validated 1.0.854 executable. This artwork-only follow-up does
not claim a new native CI build or a published release.

No new activity sequences are introduced. Harkonnen Stadium's existing nine
frames remain intact; the other existing special-building animations are
unchanged. The two unauthored vacant Rebels Industrial cells still use their
existing native fallback. Public releases and the website remain at 1.0.853.
