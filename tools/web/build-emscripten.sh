#!/usr/bin/env bash
# Reproducible Emscripten browser build for DuneCity.
#
# Output (under build/emscripten/bin/ by default):
#   dunecity.html
#   dunecity.js
#   dunecity.wasm
#   dunecity.data   (preloaded game assets)
#   shell.js / shell.css (production browser shell)
#
# Requires: git, cmake, python3, node (for webrtc glue unit tests only).
#
# Artifact sizes use tools/web/file-size-bytes.sh (portable wc -c) so the
# script works on Linux CI and macOS dev machines.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_VERSION="$(tr -d '[:space:]' < "${ROOT}/tools/web/emsdk-version.txt")"
EMSDK_REVISION="$(tr -d '[:space:]' < "${ROOT}/tools/web/emsdk-revision.txt")"
EMSDK_ORIGIN="https://github.com/emscripten-core/emsdk.git"
EMSDK_DIR="${EMSDK_DIR:-${ROOT}/.emsdk}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/emscripten}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-2}"
FILE_SIZE="${ROOT}/tools/web/file-size-bytes.sh"

[[ "${EMSDK_VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid SDK version" >&2; exit 1; }
[[ "${EMSDK_REVISION}" =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid SDK revision" >&2; exit 1; }
# Never remove a caller-supplied directory. Also refuse an in-source build or
# one in an ancestor directory before downloading or executing any SDK code.
BUILD_DIR="$(python3 - "${ROOT}" "${BUILD_DIR}" <<'PY'
import pathlib, sys
source, build = (pathlib.Path(p).resolve() for p in sys.argv[1:])
if build == source or build in source.parents or build == pathlib.Path.home():
    sys.exit("Refusing unsafe build directory: " + str(build))
print(build)
PY
)"

echo "==> DuneCity Emscripten build"
echo "    repo:        ${ROOT}"
echo "    emsdk:       ${EMSDK_DIR}"
echo "    emsdk ver:   ${EMSDK_VERSION}"
echo "    build dir:   ${BUILD_DIR}"
echo "    build type:  ${BUILD_TYPE}"

if [[ ! -e "${EMSDK_DIR}" ]]; then
    echo "==> Fetching pinned emsdk into ${EMSDK_DIR}"
    git init -q "${EMSDK_DIR}"
    git -C "${EMSDK_DIR}" remote add origin "${EMSDK_ORIGIN}"
    git -C "${EMSDK_DIR}" fetch --depth 1 origin "${EMSDK_REVISION}"
    git -C "${EMSDK_DIR}" -c core.hooksPath=/dev/null checkout --detach "${EMSDK_REVISION}"
fi

[[ "$(git -C "${EMSDK_DIR}" remote get-url origin)" == "${EMSDK_ORIGIN}" ]] || {
    echo "Refusing SDK checkout from an unexpected origin" >&2; exit 1;
}
[[ "$(git -C "${EMSDK_DIR}" rev-parse HEAD)" == "${EMSDK_REVISION}" ]] || {
    echo "Refusing SDK checkout at an unreviewed revision; use a fresh EMSDK_DIR" >&2; exit 1;
}
[[ -z "$(git -C "${EMSDK_DIR}" status --porcelain --untracked-files=no)" ]] || {
    echo "Refusing SDK checkout with modified tracked files" >&2; exit 1;
}

pushd "${EMSDK_DIR}" >/dev/null
# The pinned installer lists available releases even with --installed; finding
# the version there does not prove its Node/compiler dependencies exist. install
# is idempotent and checks the full pinned dependency set before activation.
echo "==> Ensuring Emscripten ${EMSDK_VERSION} and its dependencies are installed"
./emsdk install "${EMSDK_VERSION}"
./emsdk activate "${EMSDK_VERSION}"
# shellcheck disable=SC1091
source ./emsdk_env.sh
popd >/dev/null

command -v emcc >/dev/null
EMCC_VERSION="$(emcc --version | head -1)"
if ! emcc --version 2>/dev/null | grep -q "${EMSDK_VERSION}"; then
    echo "ERROR: active emcc is not pinned ${EMSDK_VERSION}: ${EMCC_VERSION}" >&2
    exit 1
fi
echo "==> Using ${EMCC_VERSION}"

echo "==> WebRTC glue source checks"
node "${ROOT}/tools/web/verify-dunecity-js.mjs" --source "${ROOT}/platform/web/dunecity_webrtc_config.js"

echo "==> Prebuilding Emscripten SDL ports (serial cache warmup)"
unset EM_CACHE_IS_LOCKED
embuilder build sdl2 sdl2_mixer sdl2_ttf zlib

# CMake validates existing cache/source compatibility; preserve build outputs
# and unrelated files instead of recursively deleting BUILD_DIR.
emcmake cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DDUNECITY_BUILD_TESTS=OFF \
    -DDUNECITY_ENABLE_PCH=OFF

cmake --build "${BUILD_DIR}" --target dunecity -j "${JOBS}"

OUT_DIR="${BUILD_DIR}/bin"
HTML="${OUT_DIR}/dunecity.html"
JS="${OUT_DIR}/dunecity.js"
WASM="${OUT_DIR}/dunecity.wasm"
DATA="${OUT_DIR}/dunecity.data"

for artifact in "${HTML}" "${JS}" "${WASM}" "${DATA}" "${OUT_DIR}/shell.js" "${OUT_DIR}/shell.css"; do
    if [[ ! -s "${artifact}" ]]; then
        echo "ERROR: expected non-empty artifact missing: ${artifact}" >&2
        exit 1
    fi
done

if [[ -f "${OUT_DIR}/dunecity.worker.js" ]]; then
    echo "ERROR: pthread worker artifact present; browser build must be single-threaded" >&2
    exit 1
fi

# Prepend the committed p2pkit IIFE bundle so globalThis.P2PKIT_IIFE exists
# before dunecity.js runs; the SDK glue resolves it lazily at runtime. The
# bundle is the p2pkit dependency's own build output (its prepare script
# builds dist/p2pkit.iife.js during npm install in platform/web), copied
# verbatim into platform/web/dist/ by tools/web/build-p2pkit-iife.mjs and
# committed so offline builds need neither npm nor network. If it is missing,
# regenerate it with that script (needs the installed dependency). Set
# P2PKIT_SKIP_BUILD=1 to fail instead of regenerating (offline sandboxes).
P2PKIT_IIFE="${ROOT}/platform/web/dist/p2pkit.iife.js"
if [[ ! -s "${P2PKIT_IIFE}" ]]; then
    if [[ -n "${P2PKIT_SKIP_BUILD:-}" ]]; then
        echo "ERROR: p2pkit IIFE bundle missing: ${P2PKIT_IIFE}" >&2
        echo "       regenerate with: npm install (in platform/web) && node tools/web/build-p2pkit-iife.mjs" >&2
        exit 1
    fi
    echo "==> p2pkit IIFE bundle missing; regenerating it from the installed dependency" >&2
    node "${ROOT}/tools/web/build-p2pkit-iife.mjs" || exit 1
    if [[ ! -s "${P2PKIT_IIFE}" ]]; then
        echo "ERROR: p2pkit IIFE bundle still missing after regeneration: ${P2PKIT_IIFE}" >&2
        exit 1
    fi
fi
echo "==> prepending p2pkit IIFE to ${JS}"
node "${ROOT}/tools/web/prepend-p2pkit.mjs" "${P2PKIT_IIFE}" "${JS}"

node "${ROOT}/tools/web/verify-dunecity-js.mjs" --built "${JS}"
python3 "${ROOT}/scripts/check-web-mods.py" --build-root "${BUILD_DIR}"

echo ""
echo "==> Build succeeded"
echo "    ${HTML}  $("${FILE_SIZE}" "${HTML}") bytes"
echo "    ${JS}    $("${FILE_SIZE}" "${JS}") bytes"
echo "    ${WASM}  $("${FILE_SIZE}" "${WASM}") bytes"
echo "    ${DATA}  $("${FILE_SIZE}" "${DATA}") bytes"
echo ""
echo "Serve locally, e.g.:"
echo "  cd ${OUT_DIR} && python3 -m http.server 8080"
echo "  open http://127.0.0.1:8080/dunecity.html"
