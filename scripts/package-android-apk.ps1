[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$AndroidSdk = "",
    [string]$AndroidNdk = "",
    [string]$VcpkgRoot = "",
    [string]$NativeBuildDir = "build-android-arm64-ndk",
    [string]$GradleExecutable = "",
    [switch]$BuildNative,
    [ValidateRange(1, 8)]
    [int]$NativeBuildJobs = 1,
    [switch]$BuildApk,
    [switch]$ReleaseBuild
)

$ErrorActionPreference = "Stop"
$MinimumAndroidNdkMajor = 26
$GradleWrapperVersion = "8.9.0"
$GradleWrapperSha256 = "498495120A03B9A6AB5D155F5DE3C8F0D986A449153702FB80FC80E134484F17"
$SdlSourceVersion = "2.32.10"
$SdlSourceSha512 = "D5622D6BB7266F7942A7B8AD43E8A22524893BF0C2EA1AF91204838D9B78D32768843F6FAA248757427B8404B8C6443776D4AFA6B672CD8571A4E0C03A829383"

function Get-AndroidNdkMajor([string]$Path) {
    $sourceProperties = Join-Path $Path "source.properties"
    if (Test-Path -LiteralPath $sourceProperties) {
        $revision = Select-String -LiteralPath $sourceProperties -Pattern '^Pkg\.Revision\s*=\s*([0-9]+)' |
            Select-Object -First 1
        if ($null -ne $revision) {
            return [int]$revision.Matches[0].Groups[1].Value
        }
    }

    $directoryName = Split-Path -Leaf $Path
    if ($directoryName -match '^([0-9]+)(?:\.|$)') {
        return [int]$Matches[1]
    }
    return 0
}

function Test-SupportedAndroidNdk([string]$Path) {
    return (Test-Path -LiteralPath (Join-Path $Path "build\cmake\android.toolchain.cmake")) -and
        ((Get-AndroidNdkMajor $Path) -ge $MinimumAndroidNdkMajor)
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
if ([string]::IsNullOrWhiteSpace($AndroidSdk)) {
    $AndroidSdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
}
if ([string]::IsNullOrWhiteSpace($AndroidSdk)) {
    throw "Set ANDROID_HOME or ANDROID_SDK_ROOT, or pass -AndroidSdk."
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    $AndroidNdk = $env:ANDROID_NDK_HOME
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    $ndkRoots = @(
        (Join-Path $AndroidSdk "ndk"),
        (Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk")
    ) | Select-Object -Unique
    foreach ($ndkRoot in $ndkRoots) {
        if (Test-Path -LiteralPath $ndkRoot) {
            $latestNdk = Get-ChildItem -LiteralPath $ndkRoot -Directory |
                Where-Object { Test-SupportedAndroidNdk $_.FullName } |
                Sort-Object Name -Descending |
                Select-Object -First 1
            if ($null -ne $latestNdk) {
                $AndroidNdk = $latestNdk.FullName
                break
            }
        }
    }
}
if ([string]::IsNullOrWhiteSpace($AndroidNdk)) {
    throw "Set ANDROID_NDK_HOME, install a complete NDK under an Android SDK ndk directory, or pass -AndroidNdk."
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = $env:VCPKG_ROOT
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $vcpkgCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\vcpkg",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\VC\vcpkg",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg"
    )
    $VcpkgRoot = $vcpkgCandidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "scripts\buildsystems\vcpkg.cmake") } |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "Set VCPKG_ROOT, pass -VcpkgRoot, or install the Visual Studio vcpkg component."
}

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-DirectoryContentFingerprint([string]$Path) {
    # Hashes relative paths plus file content, so re-authored artwork of an
    # identical byte count still produces a new fingerprint. Returns a short
    # token safe for an Android marker filename.
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Cannot fingerprint missing directory: $Path"
    }
    $root = (Get-FullPath $Path).TrimEnd('\')
    $files = @(Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName)
    $entries = foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length).Replace('\', '/')
        "$relative $((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
    }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes(($entries -join "`n"))
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($hasher.ComputeHash($bytes))).Replace('-', '').Substring(0, 16)
    } finally {
        $hasher.Dispose()
    }
}

function Assert-UnderRoot([string]$Path, [string]$Root) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside repo root: $fullPath"
    }
}

function Reset-Directory([string]$Path, [string]$Root) {
    Assert-UnderRoot $Path $Root
    if (Test-Path -LiteralPath $Path) {
        for ($attempt = 1; $attempt -le 3 -and (Test-Path -LiteralPath $Path); $attempt++) {
            try {
                Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
            } catch {
                Write-Verbose "Directory reset attempt $attempt failed for '$Path': $($_.Exception.Message)"
            }
            if (Test-Path -LiteralPath $Path) {
                Start-Sleep -Milliseconds (250 * $attempt)
            }
        }
        if (Test-Path -LiteralPath $Path) {
            $stalePath = "$Path.stale-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
            Assert-UnderRoot $stalePath $Root
            Move-Item -LiteralPath $Path -Destination $stalePath -ErrorAction Stop
            Write-Warning "Windows kept files open under '$Path'; moved the old directory to '$stalePath'."
        }
    }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Convert-ToPropertiesPath([string]$Path) {
    return $Path.Replace("\", "\\").Replace(":", "\:")
}

function Write-AndroidTvBanner([string]$IconPath, [string]$Destination) {
    Add-Type -AssemblyName System.Drawing

    $banner = New-Object System.Drawing.Bitmap 320, 180
    $graphics = [System.Drawing.Graphics]::FromImage($banner)
    $icon = [System.Drawing.Image]::FromFile($IconPath)
    $titleFont = New-Object System.Drawing.Font "Segoe UI", 22, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $subtitleFont = New-Object System.Drawing.Font "Segoe UI", 11, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $titleBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 232, 190, 82))
    $subtitleBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 225, 225, 225))
    $borderPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 155, 97, 26)), 2

    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(255, 18, 15, 12))
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $graphics.DrawRectangle($borderPen, 5, 5, 309, 169)
        $graphics.DrawImage($icon, 22, 26, 128, 128)
        $graphics.DrawString("DUNE LEGACY", $titleFont, $titleBrush, 164, 59)
        $graphics.DrawString("DuneCity", $subtitleFont, $subtitleBrush, 165, 94)

        $parent = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        $banner.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $borderPen.Dispose()
        $subtitleBrush.Dispose()
        $titleBrush.Dispose()
        $subtitleFont.Dispose()
        $titleFont.Dispose()
        $icon.Dispose()
        $graphics.Dispose()
        $banner.Dispose()
    }
}

function Install-GradleWrapperJar([string]$Destination) {
    $url = "https://raw.githubusercontent.com/gradle/gradle/v$GradleWrapperVersion/gradle/wrapper/gradle-wrapper.jar"
    $temporaryPath = "$Destination.download"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $temporaryPath -UseBasicParsing
        $actualHash = (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash
        if ($actualHash -ne $GradleWrapperSha256) {
            throw "Gradle wrapper checksum mismatch: expected $GradleWrapperSha256, got $actualHash"
        }
        Move-Item -LiteralPath $temporaryPath -Destination $Destination -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Get-SdlAndroidSource([string]$Root) {
    $cacheRoot = Join-Path $Root "build-android-sdl-source"
    $sourceRoot = Join-Path $cacheRoot "SDL-release-$SdlSourceVersion"
    $androidActivity = Join-Path $sourceRoot "android-project\app\src\main\java\org\libsdl\app\SDLActivity.java"
    if (Test-Path -LiteralPath $androidActivity) {
        return Get-Item -LiteralPath $sourceRoot
    }

    Assert-UnderRoot $cacheRoot $Root
    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    $archive = Join-Path $cacheRoot "SDL-release-$SdlSourceVersion.tar.gz"
    $temporaryArchive = "$archive.download"
    $archiveUrl = "https://github.com/libsdl-org/SDL/archive/refs/tags/release-$SdlSourceVersion.tar.gz"

    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $archiveUrl -OutFile $temporaryArchive -UseBasicParsing
        $actualHash = (Get-FileHash -LiteralPath $temporaryArchive -Algorithm SHA512).Hash
        if ($actualHash -ne $SdlSourceSha512) {
            throw "SDL source checksum mismatch: expected $SdlSourceSha512, got $actualHash"
        }
        Move-Item -LiteralPath $temporaryArchive -Destination $archive -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryArchive) {
            Remove-Item -LiteralPath $temporaryArchive -Force
        }
    }

    if (Test-Path -LiteralPath $sourceRoot) {
        Assert-UnderRoot $sourceRoot $Root
        Remove-Item -LiteralPath $sourceRoot -Recurse -Force
    }
    $cmakeCommand = Get-Command cmake -ErrorAction Stop
    Push-Location $cacheRoot
    try {
        & $cmakeCommand.Source -E tar xzf $archive
        if ($LASTEXITCODE -ne 0) {
            throw "SDL source extraction failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
    if (-not (Test-Path -LiteralPath $androidActivity)) {
        throw "Downloaded SDL $SdlSourceVersion does not contain the expected Android project."
    }
    return Get-Item -LiteralPath $sourceRoot
}

$RepoRoot = Get-FullPath $RepoRoot
$AndroidSdk = Get-FullPath $AndroidSdk
$AndroidNdk = Get-FullPath $AndroidNdk
$VcpkgRoot = Get-FullPath $VcpkgRoot

if (-not (Test-SupportedAndroidNdk $AndroidNdk)) {
    $detectedMajor = Get-AndroidNdkMajor $AndroidNdk
    throw "Android NDK 26 or newer is required; '$AndroidNdk' reports major version $detectedMajor or is incomplete."
}

$requiredPaths = @(
    (Join-Path $AndroidSdk "platform-tools"),
    (Join-Path $AndroidNdk "build\cmake\android.toolchain.cmake"),
    (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"),
    (Join-Path $VcpkgRoot "vcpkg.exe")
)
foreach ($requiredPath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Missing Android build requirement: $requiredPath"
    }
}

$cmakeProject = Select-String -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Pattern '^project\(DuneCity VERSION ([0-9]+)\.([0-9]+)\.([0-9]+) '
if ($null -eq $cmakeProject) {
    throw "Could not read the DuneCity version from CMakeLists.txt."
}
$projectVersion = $cmakeProject.Matches[0].Groups[1..3].Value -join "."
$androidVersionFile = Join-Path $RepoRoot "android-version.json"
if (-not (Test-Path -LiteralPath $androidVersionFile)) {
    throw "Missing Android version metadata: $androidVersionFile"
}
$androidVersion = Get-Content -LiteralPath $androidVersionFile -Raw | ConvertFrom-Json
$androidVersionName = [string]$androidVersion.versionName
$androidVersionCode = [int]$androidVersion.versionCode
if ($androidVersionName -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "Invalid Android versionName '$androidVersionName'."
}
if ($androidVersionCode -le 0 -or $androidVersionCode -gt 2100000000) {
    throw "Android versionCode must be between 1 and 2100000000."
}
$payloadVersion = $androidVersionName -replace '[^0-9A-Za-z._-]', '_'
# The DuneCity skin tree carries its own marker inside graphics_skins, so its
# content hash no longer has to force a full payload recopy; only the engine
# fingerprint does. Artwork-only updates then re-extract that subtree alone and
# leave authored gameplay configuration, other mods, saves and settings in place.
$duneCitySkinsPath = Join-Path $RepoRoot 'mods\dunecity\graphics_skins'
$duneCitySkinsVersion = Get-DirectoryContentFingerprint $duneCitySkinsPath

$nativeBuildPath = if ([System.IO.Path]::IsPathRooted($NativeBuildDir)) {
    Get-FullPath $NativeBuildDir
} else {
    Get-FullPath (Join-Path $RepoRoot $NativeBuildDir)
}
Assert-UnderRoot $nativeBuildPath $RepoRoot

if ($BuildNative) {
    # vcpkg's nested Android compiler probe reads these from the process
    # environment rather than inheriting only top-level CMake -D arguments.
    $env:ANDROID_HOME = $AndroidSdk
    $env:ANDROID_SDK_ROOT = $AndroidSdk
    $env:ANDROID_NDK_HOME = $AndroidNdk
    $env:VCPKG_ROOT = $VcpkgRoot

    $cmakeCommand = Get-Command cmake -ErrorAction Stop
    $ninjaCommand = Get-Command ninja -ErrorAction Stop
    $cmakeVersion = (& $cmakeCommand.Source --version | Select-Object -First 1).Trim()
    $ninjaVersion = (& $ninjaCommand.Source --version | Select-Object -First 1).Trim()
    $vcpkgVersion = (& (Join-Path $VcpkgRoot "vcpkg.exe") version | Select-Object -First 1).Trim()
    $fingerprintData = [ordered]@{
        schema = 2
        repoRoot = $RepoRoot
        androidSdk = $AndroidSdk
        androidNdk = $AndroidNdk
        vcpkgRoot = $VcpkgRoot
        cmake = $cmakeVersion
        ninja = $ninjaVersion
        vcpkg = $vcpkgVersion
        abi = "arm64-v8a"
        platform = "android-28"
        triplet = "arm64-android"
        hostTriplet = "x64-windows"
        cmakeListsSha256 = (Get-FileHash -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Algorithm SHA256).Hash
        sourceCmakeListsSha256 = (Get-FileHash -LiteralPath (Join-Path $RepoRoot "src\CMakeLists.txt") -Algorithm SHA256).Hash
        manifestSha256 = (Get-FileHash -LiteralPath (Join-Path $RepoRoot "vcpkg.json") -Algorithm SHA256).Hash
    }
    $fingerprintJson = $fingerprintData | ConvertTo-Json -Compress
    $fingerprintBytes = [System.Text.Encoding]::UTF8.GetBytes($fingerprintJson)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $fingerprint = ([System.BitConverter]::ToString($sha256.ComputeHash($fingerprintBytes))).Replace("-", "")
    } finally {
        $sha256.Dispose()
    }
    $stampPath = Join-Path $nativeBuildPath ".android-toolchain.json"
    $cachedFingerprint = ""
    if (Test-Path -LiteralPath $stampPath) {
        try {
            $cachedFingerprint = [string]((Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json).fingerprint)
        } catch {
            $cachedFingerprint = ""
        }
    }

    if ($cachedFingerprint -ne $fingerprint) {
        Write-Host "Android toolchain requirements changed; recreating $nativeBuildPath"
        Reset-Directory $nativeBuildPath $RepoRoot
    } else {
        Write-Host "Android toolchain requirements unchanged; reusing warm native build cache."
    }

    # FetchContent checks out libdatachannel under the Android build tree.
    # This drive may not record ownership; authorize only that fetched clone
    # for this build process, without changing the user's global Git config.
    $env:GIT_CONFIG_COUNT = '1'
    $env:GIT_CONFIG_KEY_0 = 'safe.directory'
    $env:GIT_CONFIG_VALUE_0 = (Join-Path $nativeBuildPath '_deps\libdatachannel-src').Replace('\', '/')

    $configureArguments = @(
        "-S", $RepoRoot,
        "-B", $nativeBuildPath,
        "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$($ninjaCommand.Source)",
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
        "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$(Join-Path $AndroidNdk 'build\cmake\android.toolchain.cmake')",
        "-DVCPKG_TARGET_TRIPLET=arm64-android",
        "-DVCPKG_HOST_TRIPLET=x64-windows",
        "-DVCPKG_INSTALLED_DIR=$(Join-Path $nativeBuildPath 'vcpkg_installed')",
        "-DANDROID_ABI=arm64-v8a",
        "-DANDROID_PLATFORM=android-28",
        "-DANDROID_STL=c++_shared",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DDUNECITY_RELEASE_BUILD=$(if ($ReleaseBuild) { 'ON' } else { 'OFF' })",
        "-DDUNECITY_BUILD_TESTS=OFF"
    )
    [ordered]@{
        fingerprint = $fingerprint
        requirements = $fingerprintData
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $stampPath -Encoding ASCII

    & $cmakeCommand.Source @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Android CMake configure failed with exit code $LASTEXITCODE"
    }

    & $cmakeCommand.Source --build $nativeBuildPath --target dunecity --parallel $NativeBuildJobs
    if ($LASTEXITCODE -ne 0) {
        throw "Android native build failed with exit code $LASTEXITCODE"
    }

}

$nativeLib = Join-Path $nativeBuildPath "lib\libmain.so"
if (-not (Test-Path -LiteralPath $nativeLib)) {
    throw "Missing native library: $nativeLib. Run this script with -BuildNative."
}
$nativeFingerprint = (Get-FileHash -LiteralPath $nativeLib -Algorithm SHA256).Hash.Substring(0, 16)
$payloadVersion = "$payloadVersion`_$nativeFingerprint"

$sdlSourceRoots = @(
    (Join-Path $nativeBuildPath "vcpkg_installed\vcpkg\blds\sdl2\src"),
    (Join-Path $nativeBuildPath "vcpkg_installed\vcpkg\buildtrees\sdl2\src"),
    (Join-Path $VcpkgRoot "buildtrees\sdl2\src"),
    (Join-Path $VcpkgRoot "vcpkg_installed\vcpkg\blds\sdl2\src")
) | Select-Object -Unique
$sdlSource = $null
foreach ($sdlSourceRoot in $sdlSourceRoots) {
    if (-not (Test-Path -LiteralPath $sdlSourceRoot)) {
        continue
    }
    $sdlSource = Get-ChildItem -LiteralPath $sdlSourceRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "android-project\app\src\main\java\org\libsdl\app\SDLActivity.java") } |
        Select-Object -First 1
    if ($null -ne $sdlSource) {
        break
    }
}
if ($null -eq $sdlSource) {
    Write-Host "SDL source tree was not retained by vcpkg; restoring verified SDL $SdlSourceVersion source."
    $sdlSource = Get-SdlAndroidSource $RepoRoot
}

$stageDir = Join-Path $RepoRoot "build-android-apk"
$payloadDir = Join-Path $RepoRoot "build-android-payload"
Reset-Directory $stageDir $RepoRoot
Reset-Directory $payloadDir $RepoRoot

$sdlAndroidProject = Join-Path $sdlSource.FullName "android-project"
Copy-Item -Path (Join-Path $sdlAndroidProject "*") -Destination $stageDir -Recurse -Force

$wrapperJar = Join-Path $stageDir "gradle\wrapper\gradle-wrapper.jar"
Install-GradleWrapperJar $wrapperJar

$jniDir = Join-Path $stageDir "app\jni"
if (Test-Path -LiteralPath $jniDir) {
    Remove-Item -LiteralPath $jniDir -Recurse -Force
}

$wrapperProperties = Join-Path $stageDir "gradle\wrapper\gradle-wrapper.properties"
(Get-Content -LiteralPath $wrapperProperties) |
    ForEach-Object {
        if ($_ -like "distributionUrl=*") {
            "distributionUrl=https\://services.gradle.org/distributions/gradle-8.9-bin.zip"
        } else {
            $_
        }
    } |
    Set-Content -LiteralPath $wrapperProperties -Encoding ASCII

$rootBuildGradle = @'
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.7.3'
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

tasks.register('clean', Delete) {
    delete rootProject.buildDir
}
'@
Set-Content -LiteralPath (Join-Path $stageDir "build.gradle") -Value $rootBuildGradle -Encoding ASCII

$appBuildGradle = @'
apply plugin: 'com.android.application'

android {
    namespace "net.dunecity.dune2r"
    compileSdkVersion 34

    defaultConfig {
        applicationId "net.dunecity.dune2r"
        minSdkVersion 23
        targetSdkVersion 34
        versionCode __VERSION_CODE__
        versionName "__VERSION_NAME__"

        ndk {
            abiFilters "arm64-v8a"
        }
    }

    sourceSets {
        main {
            jniLibs.srcDirs = ["src/main/jniLibs"]
        }
    }

    packagingOptions {
        jniLibs {
            useLegacyPackaging true
        }
    }

    lint {
        abortOnError false
    }
}
'@
$appBuildGradle = $appBuildGradle.Replace("__VERSION_CODE__", [string]$androidVersionCode).Replace("__VERSION_NAME__", $androidVersionName)
Set-Content -LiteralPath (Join-Path $stageDir "app\build.gradle") -Value $appBuildGradle -Encoding ASCII

$manifest = @'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    android:installLocation="auto">

    <uses-feature android:glEsVersion="0x00020000" />
    <uses-feature android:name="android.hardware.touchscreen" android:required="false" />
    <uses-feature android:name="android.hardware.bluetooth" android:required="false" />
    <uses-feature android:name="android.hardware.gamepad" android:required="false" />
    <uses-feature android:name="android.hardware.usb.host" android:required="false" />
    <uses-feature android:name="android.hardware.type.pc" android:required="false" />
    <uses-feature android:name="android.software.leanback" android:required="false" />

    <uses-permission android:name="android.permission.VIBRATE" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <application
        android:label="@string/app_name"
        android:icon="@mipmap/ic_launcher"
        android:banner="@drawable/tv_banner"
        android:allowBackup="true"
        android:isGame="true"
        android:appCategory="game"
        android:theme="@style/AppTheme"
        android:hardwareAccelerated="true"
        android:extractNativeLibs="true">

        <activity
            android:name="net.dunecity.dune2r.Dune2RActivity"
            android:label="@string/app_name"
            android:alwaysRetainTaskState="true"
            android:launchMode="singleInstance"
            android:screenOrientation="sensorLandscape"
            android:resizeableActivity="true"
            android:configChanges="colorMode|density|layoutDirection|locale|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|touchscreen|keyboard|keyboardHidden|navigation"
            android:preferMinimalPostProcessing="true"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LEANBACK_LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
'@
Set-Content -LiteralPath (Join-Path $stageDir "app\src\main\AndroidManifest.xml") -Value $manifest -Encoding ASCII

$activityDir = Join-Path $stageDir "app\src\main\java\net\dunecity\dune2r"
New-Item -ItemType Directory -Force -Path $activityDir | Out-Null
$activity = @'
package net.dunecity.dune2r;

import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class Dune2RActivity extends SDLActivity {
    private static final String TAG = "Dune2RActivity";
    private static final String PAYLOAD_ROOT = "dune2r_payload";
    private static final String PAYLOAD_MARKER = ".dune2r_payload___PAYLOAD_VERSION__";
    private static final String SKINS_RELATIVE_PATH = "mods/dunecity/graphics_skins";
    private static final String SKINS_MARKER_PREFIX = ".dune2r_graphics_skins_";
    private static final String SKINS_MARKER = SKINS_MARKER_PREFIX + "__SKINS_VERSION__";

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        copyBundledPayload();
        super.onCreate(savedInstanceState);
        logWindowConfiguration("created");
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        // SDLActivity updates its SurfaceView and calls nativeSetScreenResolution.
        // Keep that behavior, then record enough window data to diagnose OEM
        // fold, unfold, DeX, and multi-window behavior from an ADB log.
        super.onConfigurationChanged(newConfig);
        logWindowConfiguration("changed");
    }

    private void logWindowConfiguration(String event) {
        int width;
        int height;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            width = bounds.width();
            height = bounds.height();
        } else {
            DisplayMetrics metrics = getResources().getDisplayMetrics();
            width = metrics.widthPixels;
            height = metrics.heightPixels;
        }
        Configuration config = getResources().getConfiguration();
        Log.i(TAG, event + " window=" + width + "x" + height
                + " dp=" + config.screenWidthDp + "x" + config.screenHeightDp
                + " smallestDp=" + config.smallestScreenWidthDp
                + " densityDpi=" + config.densityDpi
                + " orientation=" + config.orientation);
    }

    private void copyBundledPayload() {
        File outputRoot = getExternalFilesDir(null);
        if (outputRoot == null) {
            outputRoot = getFilesDir();
        }

        File marker = new File(outputRoot, PAYLOAD_MARKER);
        if (marker.exists()) {
            // The full payload is current. The presentation cache is tracked
            // separately so an artwork-only update, or a cache cleared from
            // Settings > Advanced, re-extracts just that subtree without
            // rewriting authored gameplay configuration or other mods.
            refreshGraphicsSkins(outputRoot);
            return;
        }

        try {
            copyAssetTree(getAssets(), PAYLOAD_ROOT, outputRoot);
            writeGraphicsSkinsMarker(outputRoot);
            marker.createNewFile();
        } catch (IOException e) {
            throw new RuntimeException("Failed to stage Dune2R payload", e);
        }
    }

    private void refreshGraphicsSkins(File outputRoot) {
        File skinsRoot = new File(outputRoot, SKINS_RELATIVE_PATH);
        if (new File(skinsRoot, SKINS_MARKER).exists()) {
            return;
        }

        try {
            // Deleting graphics_skins also deletes the marker, so this both
            // restores a cleared cache and installs a newer skin revision.
            copyAssetTree(getAssets(), PAYLOAD_ROOT + "/" + SKINS_RELATIVE_PATH, outputRoot);
            writeGraphicsSkinsMarker(outputRoot);
            Log.i(TAG, "Re-extracted DuneCity graphics skins from the APK payload");
        } catch (IOException e) {
            // A missing presentation cache is recoverable: the engine falls
            // back to its built-in artwork rather than failing to start.
            Log.e(TAG, "Failed to stage DuneCity graphics skins", e);
        }
    }

    private void writeGraphicsSkinsMarker(File outputRoot) throws IOException {
        File skinsRoot = new File(outputRoot, SKINS_RELATIVE_PATH);
        if (!skinsRoot.isDirectory()) {
            return;
        }
        File[] existing = skinsRoot.listFiles();
        if (existing != null) {
            for (File candidate : existing) {
                if (candidate.isFile() && candidate.getName().startsWith(SKINS_MARKER_PREFIX)) {
                    candidate.delete();
                }
            }
        }
        new File(skinsRoot, SKINS_MARKER).createNewFile();
    }

    private void copyAssetTree(AssetManager assets, String assetPath, File outputRoot) throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, outputRoot);
            }
            return;
        }

        String relativePath = assetPath.substring(PAYLOAD_ROOT.length() + 1);
        File outputFile = new File(outputRoot, relativePath);
        if (outputFile.exists() && outputFile.length() > 0 && relativePath.equals("config/Dune City.ini")) {
            return;
        }

        File parent = outputFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Could not create directory: " + parent);
        }

        try (InputStream in = assets.open(assetPath);
             OutputStream out = new FileOutputStream(outputFile)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        }
    }
}
'@
$activity = $activity.Replace("__PAYLOAD_VERSION__", $payloadVersion)
$activity = $activity.Replace("__SKINS_VERSION__", $duneCitySkinsVersion)
Set-Content -LiteralPath (Join-Path $activityDir "Dune2RActivity.java") -Value $activity -Encoding ASCII

$stringsPath = Join-Path $stageDir "app\src\main\res\values\strings.xml"
$strings = @'
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">Dune Legacy</string>
</resources>
'@
Set-Content -LiteralPath $stringsPath -Value $strings -Encoding ASCII

$iconSource = Join-Path $RepoRoot "dunecity-128x128.png"
if (Test-Path -LiteralPath $iconSource) {
    foreach ($density in @("mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi", "mipmap-xxhdpi", "mipmap-xxxhdpi")) {
        $iconDir = Join-Path $stageDir "app\src\main\res\$density"
        New-Item -ItemType Directory -Force -Path $iconDir | Out-Null
        Copy-Item -LiteralPath $iconSource -Destination (Join-Path $iconDir "ic_launcher.png") -Force
    }

    Write-AndroidTvBanner $iconSource (Join-Path $stageDir "app\src\main\res\drawable-xhdpi\tv_banner.png")
}

$jniLibsArm64 = Join-Path $stageDir "app\src\main\jniLibs\arm64-v8a"
New-Item -ItemType Directory -Force -Path $jniLibsArm64 | Out-Null
Copy-Item -LiteralPath $nativeLib -Destination (Join-Path $jniLibsArm64 "libmain.so") -Force

$libcxx = Join-Path $AndroidNdk "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so"
if (-not (Test-Path -LiteralPath $libcxx)) {
    throw "Missing libc++_shared.so: $libcxx"
}
Copy-Item -LiteralPath $libcxx -Destination (Join-Path $jniLibsArm64 "libc++_shared.so") -Force

# Gradle can miss the selected NDK and silently package unstripped libraries.
# Strip only the staged copies; keep the native build's symbols for crash reports.
$llvmStrip = Join-Path $AndroidNdk "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
if (-not (Test-Path -LiteralPath $llvmStrip)) {
    throw "Missing Android native library stripper: $llvmStrip"
}
foreach ($runtimeLibrary in @("libmain.so", "libc++_shared.so")) {
    & $llvmStrip --strip-unneeded (Join-Path $jniLibsArm64 $runtimeLibrary)
    if ($LASTEXITCODE -ne 0) {
        throw "Stripping staged $runtimeLibrary failed with exit code $LASTEXITCODE"
    }
}

Set-Content -LiteralPath (Join-Path $stageDir "local.properties") -Value ("sdk.dir=" + (Convert-ToPropertiesPath $AndroidSdk)) -Encoding ASCII

Copy-Item -LiteralPath (Join-Path $RepoRoot "data") -Destination (Join-Path $payloadDir "data") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "config") -Destination (Join-Path $payloadDir "config") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "mods") -Destination (Join-Path $payloadDir "mods") -Recurse -Force
$optionalAssetRoots = @(
    (Join-Path $payloadDir "mods\Dune2R\graphics_hd\units"),
    (Join-Path $payloadDir "mods\Dune2R\graphics_compact\objpics")
)
foreach ($optionalAssetRoot in $optionalAssetRoots) {
    Assert-UnderRoot $optionalAssetRoot $payloadDir
    if (Test-Path -LiteralPath $optionalAssetRoot) {
        Remove-Item -LiteralPath $optionalAssetRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $optionalAssetRoot | Out-Null
}
if (Test-Path -LiteralPath (Join-Path $RepoRoot "imported_sprites")) {
    Copy-Item -LiteralPath (Join-Path $RepoRoot "imported_sprites") -Destination (Join-Path $payloadDir "imported_sprites") -Recurse -Force
}

$assetPayloadDir = Join-Path $stageDir "app\src\main\assets\dune2r_payload"
New-Item -ItemType Directory -Force -Path (Split-Path $assetPayloadDir -Parent) | Out-Null
Copy-Item -LiteralPath $payloadDir -Destination $assetPayloadDir -Recurse -Force

if ($BuildApk) {
    if ([string]::IsNullOrWhiteSpace($env:JAVA_HOME)) {
        throw "Set JAVA_HOME before building the APK."
    }
    $env:ANDROID_HOME = $AndroidSdk
    $env:ANDROID_SDK_ROOT = $AndroidSdk
    $env:ANDROID_NDK_HOME = $AndroidNdk
    $env:Path = "$env:JAVA_HOME\bin;$AndroidSdk\platform-tools;$env:Path"
    Push-Location $stageDir
    try {
        $gradleCommand = if ([string]::IsNullOrWhiteSpace($GradleExecutable)) {
            Join-Path $stageDir "gradlew.bat"
        } else {
            Get-FullPath $GradleExecutable
        }
        if (-not (Test-Path -LiteralPath $gradleCommand)) {
            throw "Missing Gradle executable: $gradleCommand"
        }
        & $gradleCommand assembleDebug --no-daemon --max-workers=1
        if ($LASTEXITCODE -ne 0) {
            throw "Gradle assembleDebug failed with exit code $LASTEXITCODE"
        }

        $debugApk = Join-Path $stageDir "app\build\outputs\apk\debug\app-debug.apk"
        $namedApk = Join-Path $stageDir "app\build\outputs\apk\debug\DuneLegacy.apk"
        if (Test-Path -LiteralPath $debugApk) {
            Copy-Item -LiteralPath $debugApk -Destination $namedApk -Force
        }
    } finally {
        Pop-Location
    }
}

Write-Host "APK project staged: $stageDir"
Write-Host "APK version: $androidVersionName ($androidVersionCode)"
Write-Host "DuneCity payload version: $projectVersion"
Write-Host "DuneCity graphics skins version: $duneCitySkinsVersion"
Write-Host "Data payload staged: $payloadDir"
Write-Host "Native libs staged: $jniLibsArm64"
Write-Host "Install APK: adb install -r `"$stageDir\app\build\outputs\apk\debug\DuneLegacy.apk`""
Write-Host "The APK contains the payload and stages it into Android app storage on first launch."
Write-Host "Manual payload push, if needed for debugging: adb push `"$payloadDir\.`" /sdcard/Android/data/net.dunecity.dune2r/files/"
