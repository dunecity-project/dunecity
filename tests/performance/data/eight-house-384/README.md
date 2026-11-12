# Immutable eight-house save fixture

The saved game pins the exact Workshop mod/map manifests stored here. Rebuild
them by content hash; do not substitute newer artwork/configuration or change the save.

`files/` contains files unavailable in the current production tree. The
SHA-256-named PNG archives preserve 31 original skin Compacts replaced by the
1.0.854 detail pass. Their decompressed bytes come from v1.0.853 and match the
pinned mod manifest exactly.

`ObjectData.ini` and `QuantBot Config.ini` preserve the gameplay configuration
from 1.0.849 commit `4018fe36b213508f1e10e1323ace89d7c5b07705`:
`config/ObjectData.ini.default` and `config/QuantBot Config.ini.default`,
respectively. Their byte sizes and SHA-256 hashes match the pinned mod manifest:

| Fixture | Bytes | SHA-256 |
| --- | ---: | --- |
| `ObjectData.ini` | 20149 | `a9446d1441d34be2dc0206eb90c0dde9e3a498bdaabb9bb983dde9454e96e87a` |
| `QuantBot Config.ini` | 11507 | `4a4a503d714541e4decb1e2f175c30977d3990036b4ccb19352555d4197141fb` |

These are test data only, not active skin assets or gameplay defaults, and are
not copied into application packages. Reconstruction needs neither Git history
nor a player's mutable profile; the other files resolve by hash from the tree.

`test_fixture_reconstruction.py` checks the saved game, manifest hashes and
every reconstructed payload file without launching or compiling the game, with
production `config/` excluded from the reconstruction sources.
