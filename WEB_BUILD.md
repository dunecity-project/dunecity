# Browser build

DuneCity can be compiled to WebAssembly with Emscripten and published as a
static browser application. The browser package includes the base game data,
configuration, JavaScript loader, and WebAssembly executable.

The bundled mods include Tornie, TornieLite, Jericho, JerichoLite, ArcaKev, and
ArcaKevLite alongside the existing Vanilla, DuneCity, and Dune2R content. The
native, Android, iOS, and browser packaging rules include the whole `mods/`
tree; Dune2R's optional downloadable artwork remains excluded.

Verify an already-built browser archive before publishing it:

```sh
python3 scripts/check-web-mods.py --build-root <browser-build>
```

This checks each Tornie-family mod against its own `checksums.sha256`, reports
verified file counts and payload bytes per mod, verifies the twelve shared
`data/frames/` faction images (downloaded once, not duplicated per mod), and
retains the DuneCity skins/maps and Dune2R base-content checks.

## Build

Install the pinned Emscripten SDK on the D: drive:

```powershell
.\scripts\install-emsdk.ps1
```

Build and copy the browser package into the website repository:

```powershell
.\scripts\package-web.ps1
```

The defaults use these paths:

- SDK: `D:\BotServer\Tools\emsdk`
- Build: `D:\BotServer\Builds\Dune2R-web`
- Website output: `D:\BotServer\GitHub\dunelegacy.com\website\play`

Serve the website directory over HTTP for local testing. Opening the generated
HTML directly from disk will not load the WebAssembly data package.

```powershell
python -m http.server 8098 --bind 127.0.0.1 --directory D:\BotServer\GitHub\dunelegacy.com\website
```

Then open `http://127.0.0.1:8098/play/`.

## Runtime

- The 640x480 game canvas scales to fit the browser while retaining its 4:3
  aspect ratio.
- Configuration and saves live under `/home/web_user` in the virtual file
  system and are synchronized to browser IndexedDB.
- The bootstrap JavaScript and CSS are external files so the deployed page can
  enforce a strict Content Security Policy without `unsafe-inline`.
- `build.json` records SHA-256 hashes for the complete browser artifact set.
- Menus and gameplay yield through Asyncify so the browser remains responsive.
- The original first-launch intro is skipped in the browser. It can still be
  enabled from the game options.
- Native builds keep their existing paths, delays, crash handlers, networking,
  and first-launch behavior.

## Networking boundary

The browser build is currently a single-player release. Browsers cannot open
the native UDP sockets used by ENet, and browser WebSockets cannot connect
directly to a native ENet server. Browser multiplayer requires a secure
WebSocket gateway that translates between WebSocket clients and the game's
native ENet protocol. Do not advertise browser multiplayer until that gateway
and protocol compatibility tests are deployed.
