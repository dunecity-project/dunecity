# Fresh guest multiplayer map reception

## Problem and fix

A fresh guest could connect to an online custom lobby and appear in the host's
roster while remaining at “Connecting to the host and receiving game setup”.
The guest rejected `SENDGAMEINFO` with “Workshop file is missing or too large”.
This was reproduced on the public browser release 1.0.783.

The packet handler saves the received map before the lobby resolves its mod.
Writing the map's `.workshop.ini` metadata tried to read the mod revision to
obtain its engine name, even when that revision was not cached yet.

The metadata writer now omits that optional catalogue hint if the dependency
cannot be read. The map's hash, identity, version and immutable manifest still
pin the exact mod revision. The existing lobby code verifies and resolves that
revision before play. Reinstalling the map after resolution fills in the hint.
No network or save format changes are required.

## Verification, 27 September 2026

- The new real-engine menu probe drives `SENDGAMEINFO` with both the map and
  mod revisions removed from the guest's store. Against the old native object,
  it fails with the same missing-Workshop-file error observed in the browser.
- With the fix, reception succeeds and preserves map bytes and metadata.
  Altered bytes and a mismatched mod hash are rejected without damaging the
  received map. Restoring the dependency allows normal exact-mod activation.
- An unchanged 1.0.786 browser host and a patched guest were served from separate
  local origins against the real PHP signaling service. The guest revision
  directory was confirmed empty before joining. Habbanya-Penny with Dune City
  reached the lobby and gameplay on both clients. Chat worked both ways, and
  the guest's movement order visibly moved its units.
- The guest returned to the menu normally. The received map and its sidecar
  were still present after reloading the browser.
- Native and pinned Emscripten builds and native dependency audits passed.
  All 43 CTest targets passed, including the new regression at three resolutions.

The browser test used local signaling and same-machine peers. It does not prove
connectivity across different NATs or constitute a production deployment.

Evidence is in `../outputs/multiplayer-check-20260927/` relative to the checkout:
`regression-baseline.log`, `regression-baseline-runtime.log`, `ctest-fixed.log`,
`web-fix-build.log`, `browser-service.log`, and the delegated review result.
