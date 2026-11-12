#!/bin/bash
# Helper script to build Dune Legacy with installer
# Usage: ./scripts/build.sh [clean] [--release-build]

set -e

cd "$(dirname "$0")/.."

RELEASE_BUILD=OFF
for arg in "$@"; do
    case "$arg" in
        clean)
            echo "🧹 Cleaning build directory..."
            rm -rf build-vcpkg
            ;;
        --release-build) RELEASE_BUILD=ON ;;
        *) echo "Usage: $0 [clean] [--release-build]" >&2; exit 2 ;;
    esac
done

# Reconfigure on every invocation so a normal build clears a previous release opt-in.
echo "🔧 Configuring CMake with vcpkg..."
cmake -B build-vcpkg -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDUNECITY_RELEASE_BUILD="$RELEASE_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic

echo "🚀 Building Dune Legacy + DMG installer..."
echo "   (CPack cache will be cleaned automatically)"
cmake --build build-vcpkg --target dmg --config Release

echo ""
echo "✅ Build complete!"
echo "📦 DMG: $(ls -lh build-vcpkg/*.dmg 2>/dev/null | awk '{print $9, "(" $5 ")"}')"
echo ""
echo "💡 The DMG is guaranteed fresh (CPack cache cleaned during build)"
echo ""
echo "To install: cp -R build-vcpkg/install/dunelegacy.app /Applications/"
echo "To run:     open build-vcpkg/install/dunelegacy.app"

