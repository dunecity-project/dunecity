# Linux renderer startup and issue #120

The Linux startup path prefers an accelerated SDL renderer with target-texture
support. If none can initialize, it falls back to the software renderer, still
requiring target textures. Other platforms keep their existing acceleration
requirement. This shipped in 1.0.865.

## Fallback order (next release after 1.0.878)

SDL's own `SDL_CreateRenderer(window, -1, ...)` search ends with "Couldn't find
matching render driver" and throws away the reason each backend failed, which
is all the 1.0.864 log could tell us. `createStartupRenderer`
(`include/misc/RendererInitialization.h`) now walks the compiled drivers itself,
by explicit index, in the order `startupRendererOrder` gives:

1. the driver named by `SDL_RENDER_DRIVER` (or the Windows `direct3d11` default
   hint), of any kind, so `SDL_RENDER_DRIVER=software` is always honoured; an
   unknown name is logged and ignored;
2. every accelerated driver with target textures, in SDL's priority order
   (Linux: `opengl`, `opengles2`, `opengles`);
3. on Linux desktop and with the dummy video driver, `software`.

Each failure is logged as `Render driver <name> failed: <SDL error>`, a
recovery as `Renderer fallback: ...; continuing with <driver>`, and the chosen
driver as `Render driver: <name>`. Only when every candidate fails does start-up
stop, and then it shows a message box with all the errors (and on Linux the
`SDL_RENDER_DRIVER=software` hint) instead of exiting silently. VSync is set
after creation and never blocks start-up. `RendererFallbackOrderTestCase.cpp`
(in `dunelegacy_tests`, every platform) checks the order without a display;
`renderer_initialization` (Linux) checks real recovery with the dummy driver.

## Evidence from 1.0.864

Issue: https://github.com/dunecity-project/dunecity/issues/120

The official 1.0.864 AppImage (SHA-256
`e810ed5972bbcc1f195ac98e616af49174b9cc5625d64642c74d568b6219d554`)
and Linux x64 tarball (SHA-256
`34731b2295a57c3ac9160868db45bfd58370f783128cba1945e4ef4419ffe94d`)
contain statically linked SDL with only GLES2 and software render drivers.
The 1.0.878 AppImage was rechecked on 2026-11-26: its binary contains the
`opengl`, `opengles2`, `opengles` and `software` drivers, and the AppImage
bundles no libGL/libEGL/libGLX (only libdbus), so the system NVIDIA or Mesa GL
libraries are the ones SDL loads.
`SDL_GetNumRenderDrivers_REAL` returns two, and the binaries contain
`GLES2_RenderDriver` and `SW_RenderDriver` but no `GL_RenderDriver`.
Desktop OpenGL/GLX is therefore missing from those builds.

The released tarball exits with the reported
`SDL_CreateRenderer failed: Couldn't find matching render driver` message when
acceleration is unavailable. This was reproduced with SDL's dummy video backend
and separately with the offscreen backend while intentionally making GL/EGL
library loading fail. Setting `SDL_RENDER_DRIVER=software` gets the original
release past renderer initialization into its intro in the controlled test.

These observations do not establish why EGL fails on the reporter's Linux Mint
installation or certify their NVIDIA driver. An actual Mint/NVIDIA retest is
still needed. A temporary diagnostic workaround is:

```sh
SDL_RENDER_DRIVER=software ./DuneCity-1.0.864-Linux-x86_64.AppImage
```

Software rendering can be slower, especially at high resolutions.

## Build and package checks

SDL's Linux configuration auto-detects GL/GLX development headers. The release
workflow now installs the GL/EGL development packages explicitly and uses a new
vcpkg cache generation so it cannot silently restore an SDL archive built
without desktop OpenGL. SDL IBus support also requires the explicit `dbus`
manifest feature; omitting it fails clean builds when IBus headers are installed. The
current resolved D-Bus dependency graph also builds libsystemd, which requires
Python Jinja2 at build time; the Linux workflow installs it explicitly.

`--check-video-runtime` prints the compiled video/render backend inventory and
selected backend, then creates a target texture, draws a known colour, copies
it back to the window and verifies the pixel. It uses the same renderer
initializer as the game and does not open a player profile or require assets.
The older `--check-desktop-runtime` updater check retains its original semantics.

```sh
python3 -m unittest discover -s scripts/tests -p 'test_video_runtime.py'
cmake --build build --target renderer_initialization_tests
ctest --test-dir build -R '^renderer_initialization$' --output-on-failure
xvfb-run -a python3 scripts/check-video-runtime.py \
  --binary build/bin/dunecity --require-driver opengl --require-accelerated
```

The Python checker runs normal accelerated selection, explicit software
selection, and automatic recovery with the dummy video backend. It fails if
desktop OpenGL is absent or normal startup falls back to software. Release CI
runs it against the build, tarball and AppImage, and no longer forces software
for the full package startup smoke.

## Validation limits

The development verification used Debian 13, GCC 14 and Debian SDL 2.32.4, with
native direct P2P disabled. It is a diagnostic build, not a portable release for
Linux Mint. A release must rebuild the pinned vcpkg SDL 2.32.10 on the existing
Ubuntu 22.04 ABI baseline, pass the package checks, and follow the normal version
gate before distribution. Linux headless/offscreen checks do not replace X11,
Wayland, NVIDIA hardware, or gameplay/performance testing.
