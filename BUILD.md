# Building Dune Legacy

This guide covers building Dune Legacy on all platforms using modern dependency management.

For the verified PREDATOR laptop workflow, including the current Dune2R Windows
ZIP, Android APK, warm-cache strategy, and exFAT workarounds, see
[`LOCAL_BUILD_GUIDE.md`](LOCAL_BUILD_GUIDE.md).

iPhone and iPad builds (simulator and device) are described in
[`IOS_PORT.md`](IOS_PORT.md).

## Quick Start

### Prerequisites

**All Platforms:**
- CMake 3.15+
- C++17 compatible compiler
- Git
- Node.js 20+ and npm

Before configuring either a native or browser build, install the pinned SDK headers:

```bash
npm ci --ignore-scripts --prefix platform/web
```

Use `npm ci --prefix platform/web` and `npm ci --prefix tools/p2pkit-build`
when rebuilding the JavaScript bundles (the SDK prepare script produces its ESM exports).
The dependency uses HTTPS and an immutable Git commit; GitHub SSH keys are not required.

**Platform-Specific:**
- **Windows**: Visual Studio 2019+ or MinGW-w64
- **macOS**: Xcode 11+ or Command Line Tools (`xcode-select --install`)
- **Linux**: Build essentials (`sudo apt install build-essential cmake`)

---

## ⚠️ IMPORTANT: Always Build with Installer Targets!

**DO NOT** just run `cmake --build build` - this only builds the binary, not the installer!

## Release/version gate

Before any build that will be handed to Stefan, tagged, or pushed for CI as a release candidate, verify the source-controlled version:

```bash
scripts/bump-version.sh --check
git diff -- CMakeLists.txt include/config.h vcpkg.json
git tag --points-at HEAD
```

If the intended tag is `vX.Y.Z`, the app version must already be `X.Y.Z` before tagging or reporting that CI is building the release. Version bumps belong in the same commit as the release work; do not tag first and patch the version afterward.

**ALWAYS** use the platform-specific installer target when testing or distributing:

```bash
# macOS: Build + Create DMG
cmake --build build --target dmg --config Release

# Windows: Build + Create Installer
cmake --build build --target installer --config Release

# Linux: Build + Create Packages
cmake --build build --target linux-packages --config Release
```

**Why?** The installer build:
- ✅ Packages all dependencies correctly
- ✅ Includes game data files
- ✅ Creates proper app bundles/installers
- ✅ Is what users will actually run
- ✅ **Automatically cleans CPack cache** to prevent stale files

**VS Code/Cursor Users:** Press `Cmd+Shift+B` (Mac) or `Ctrl+Shift+B` (Windows/Linux) and select:
- **"🚀 Build + Create DMG Installer (RECOMMENDED)"** - Use this!
- ~~"⚠️ Build Binary Only"~~ - Avoid this, DMG won't update!

### 🔧 How the Installer Targets Work

The `dmg`, `installer`, and `package` targets automatically:
1. **Clean CPack cache** (`_CPack_Packages/`, old installers)
2. **Clean install directory** (staging area)
3. **Build the binary**
4. **Install to staging**
5. **Create fresh installer** from staging

This ensures you **never** get stale files in your installer!

---

## Recommended: Build with vcpkg

vcpkg provides consistent, reproducible builds across all platforms. This is what our CI/CD uses.

### One-Time vcpkg Setup

```bash
# Clone vcpkg (pick a location like ~/vcpkg or C:\vcpkg)
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# Bootstrap
./bootstrap-vcpkg.sh      # macOS/Linux
.\bootstrap-vcpkg.bat     # Windows
```

### Build Dune Legacy

```bash
cd /path/to/dunelegacy

# Configure (replace /path/to/vcpkg with your vcpkg location)
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

**First build takes ~5 minutes** as vcpkg downloads and compiles SDL2 libraries.

### Run the Game

- **macOS**: `open build/bin/dunelegacy.app`
- **Windows**: `build\bin\Release\dunelegacy.exe`
- **Linux**: `build/bin/dunelegacy`

### What vcpkg Does

✅ Automatically downloads SDL2, SDL2_mixer, SDL2_ttf  
✅ Builds optimized binaries for your platform  
✅ Handles all linking (static or dynamic)  
✅ No manual dependency installation needed  

---

## Alternative: System Package Managers

If you prefer using system-installed libraries:

### macOS (Homebrew)

```bash
# Install dependencies
brew install sdl2 sdl2_mixer sdl2_ttf cmake

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt install libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev cmake

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Fedora/RHEL)

```bash
# Install dependencies
sudo dnf install SDL2-devel SDL2_mixer-devel SDL2_ttf-devel cmake

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Platform-Specific Builds

### macOS: Xcode Project (Legacy)

The native Xcode project is still maintained for local macOS development:

```bash
# Open project
open IDE/xCode/Dune\ Legacy.xcodeproj

# Build in Xcode (⌘B)
```

**Note**: Requires SDL2 frameworks in `external/frameworks/`. Download from [SDL Releases](https://github.com/libsdl-org/SDL/releases).

### Windows: Visual Studio

**Step-by-step for Visual Studio developers:**

1. **Install vcpkg** (one-time setup):
   ```powershell
   # PowerShell or Command Prompt
   cd C:\
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Clone Dune Legacy**:
   ```powershell
   cd C:\dev  # or your preferred location
   git clone <repository-url> dunelegacy
   cd dunelegacy
   ```

3. **Generate Visual Studio Solution**:
   ```powershell
   cmake -B build -G "Visual Studio 17 2022" -A x64 `
     -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
   ```
   
   *Note: Use `-G "Visual Studio 16 2019"` for VS 2019*
   
   **First run takes 5-10 minutes** as vcpkg downloads and builds SDL2 libraries.

4. **Open in Visual Studio**:
   - Double-click `build/DuneLegacy.sln`
   - Or: File → Open → Project/Solution → Select `build/DuneLegacy.sln`

5. **Set Build Configuration**:
   - Select **Release** or **Debug** from the toolbar dropdown
   - Set **dunelegacy** as the startup project (right-click → Set as Startup Project)

6. **Build**:
   - Press **F7** or Build → Build Solution
   - Or click the green **▶ Local Windows Debugger** button to build and run

7. **Run the Game**:
   - Press **F5** (with debugging) or **Ctrl+F5** (without debugging)
   - Or manually run: `build\bin\Release\dunelegacy.exe`

**Output Locations:**
- Debug build: `build\bin\Debug\dunelegacy.exe`
- Release build: `build\bin\Release\dunelegacy.exe`
- SDL2 DLLs automatically copied alongside .exe

**Rebuilding after code changes:**
- Just press F7 in Visual Studio
- No need to re-run CMake unless you modify `CMakeLists.txt`

### macOS: Performance Build

For optimized dynamic libraries (better FPS):

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

---

## Distribution Builds

For creating distributable installers/packages:

### macOS: DMG Installer

**Quick method** (builds + packages):
```bash
cmake --build build --target dmg
```

**Output**: `build/DuneLegacy-0.98.6.6-macOS.dmg`

**Manual method**:
```bash
cd build
cpack -G DragNDrop
```

**Legacy script** (still works):
```bash
./create_dmg.sh
```

### Windows: NSIS Installer

**Quick method** (builds + packages):
```bash
cmake --build build --target installer
```

**Output**: 
- `build/DuneLegacy-0.98.6.6-Windows-x64.exe` (installer)
- `build/DuneLegacy-0.98.6.6-Windows-x64.zip` (portable)

**Manual method**:
```bash
cd build
cpack -G NSIS  # For installer
cpack -G ZIP   # For portable zip
```

**Requirements**: NSIS must be installed for the installer target.

### Linux: DEB/RPM/TGZ Packages

**Quick method** (builds + packages all formats):
```bash
cmake --build build --target linux-packages
```

**Output**:
- `build/DuneLegacy-0.98.6.6-Linux-x64.deb` (Debian/Ubuntu)
- `build/DuneLegacy-0.98.6.6-Linux-x64.rpm` (RedHat/Fedora)
- `build/DuneLegacy-0.98.6.6-Linux-x64.tar.gz` (Generic)

**Manual method** (individual formats):
```bash
cd build
cpack -G DEB  # Debian/Ubuntu package
cpack -G RPM  # RedHat/Fedora package
cpack -G TGZ  # Tarball
```

---

## Troubleshooting

### "SDL2 not found"

**With vcpkg:**
- Ensure `-DCMAKE_TOOLCHAIN_FILE` points to correct vcpkg location
- Delete `build/` and reconfigure
- Check `vcpkg.json` exists in project root

**Without vcpkg:**
- Install SDL2 via your package manager
- macOS: `brew install sdl2 sdl2_mixer sdl2_ttf`
- Linux: `sudo apt install libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev`

### "vcpkg not found"

```bash
# Use absolute path to vcpkg toolchain file
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/full/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### macOS Code Signing Errors

For local development builds:
```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="" \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO
```

### Linux Runtime Library Issues

If using system packages and libraries aren't found:
```bash
sudo ldconfig
```

Or use static linking with vcpkg:
```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux-static
```

---

## Advanced Configuration

### vcpkg Triplets

Control how dependencies are built:

- `arm64-osx` / `x64-osx` - macOS static libs
- `arm64-osx-dynamic` / `x64-osx-dynamic` - macOS dynamic libs (better performance)
- `x64-windows` - Windows DLLs
- `x64-windows-static` - Windows static linking
- `x64-linux` / `x64-linux-static` - Linux variants

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic
```

### Build Types

- `Release` - Optimized, no debug symbols (recommended)
- `Debug` - Debug symbols, no optimization
- `RelWithDebInfo` - Optimized with debug symbols

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

---

## Continuous Integration

GitHub Actions automatically builds for all platforms on every commit:

- **Windows** (x64) - MSVC + vcpkg
- **macOS** (arm64) - Clang + vcpkg  
- **Linux** (x64) - GCC + vcpkg

See `.github/workflows/build.yml` for the configuration.

Artifacts are uploaded for each successful build.

---

## For Contributors

- **Use vcpkg** for consistent dependency versions
- `vcpkg.json` defines all dependencies
- Run `cmake --build build` to build after making changes
- The project auto-formats code - follow existing style

### Project Structure

```
dunelegacy/
├── src/              - Game source code
├── include/          - Headers
├── data/             - Game assets (.PAK files)
├── config/           - Configuration templates
├── IDE/xCode/        - Xcode project (macOS)
├── vcpkg.json        - vcpkg dependencies
└── CMakeLists.txt    - Build configuration
```

---

## Getting Original Dune II Data Files

Dune Legacy requires the original Dune II data files (.PAK files). These should be placed in:

- **Development**: `dunelegacy/data/` directory
- **Installed game**: Game will prompt for location on first run

Sources:
- [Dune II - The Building of a Dynasty](https://www.myabandonware.com/game/dune-ii-the-building-of-a-dynasty-1d4)
- Original game CDs/floppies

---

## Need Help?

- **Issues**: https://github.com/dunelegacy/dunelegacy/issues
- **Website**: https://dunelegacy.sourceforge.net
- **Discord**: [Community link]

---

## Version Information

This build system uses:
- **SDL2**: 2.32.10+
- **SDL2_mixer**: 2.8.1+
- **SDL2_ttf**: 2.24.0+

Versions automatically managed by vcpkg baseline in `vcpkg.json`.
