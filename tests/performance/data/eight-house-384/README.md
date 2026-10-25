# Immutable eight-house save fixture

The saved game pins the exact Workshop mod/map manifests stored here. Rebuild
them by content hash; do not substitute newer artwork or change the save.

`files/` contains files unavailable in the current production tree. The
SHA-256-named PNG archives preserve 31 original skin Compacts replaced by the
1.0.854 detail pass. Their decompressed bytes come from v1.0.853 and match the
pinned mod manifest exactly. They are test data only, not active skin assets
and not copied into application packages. All other files continue to resolve
by hash from the current tree.

`test_fixture_reconstruction.py` checks the saved game, manifest hashes and
every reconstructed payload file without launching or compiling the game.
