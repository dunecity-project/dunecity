param(
    [string]$EmsdkRoot = 'D:\BotServer\Tools\emsdk',
    [string]$BuildRoot = 'D:\BotServer\Builds\Dune2R-web',
    [string]$WebsiteRoot = 'D:\BotServer\GitHub\dunelegacy.com\website',
    [switch]$ReleaseBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Emcmake = Join-Path $EmsdkRoot 'upstream\emscripten\emcmake.exe'
$PlayRoot = Join-Path $WebsiteRoot 'play'
$releaseBuildOption = if ($ReleaseBuild) { 'ON' } else { 'OFF' }

if (-not (Test-Path -LiteralPath $Emcmake)) {
    throw "Emscripten is not installed at $EmsdkRoot. Run scripts/install-emsdk.ps1 first."
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
& $Emcmake cmake -S $RepoRoot -B $BuildRoot -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DDUNECITY_RELEASE_BUILD=$releaseBuildOption" `
    -DDUNECITY_BUILD_TESTS=OFF `
    -DDUNECITY_ENABLE_PCH=OFF
if ($LASTEXITCODE -ne 0) { throw "Emscripten CMake configure failed with exit code $LASTEXITCODE" }

cmake --build $BuildRoot --target dunecity --parallel 8
if ($LASTEXITCODE -ne 0) { throw "Emscripten build failed with exit code $LASTEXITCODE" }

python (Join-Path $PSScriptRoot 'package-web.py') --build-root $BuildRoot --play-root $PlayRoot
if ($LASTEXITCODE -ne 0) { throw "Browser packaging failed with exit code $LASTEXITCODE" }
