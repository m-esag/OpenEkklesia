#Requires -Version 5.1
<#
.SYNOPSIS
  Build OpenEkklesia v0.8.0 on Windows (replaces the Linux/macOS scripts).

.DESCRIPTION
  Native build — no cross-compiling. Copy the source folder over by USB (see
  "What to copy" below), install Qt 6 + MSVC + CMake once, then run:
      powershell -ExecutionPolicy Bypass -File .\build-windows.ps1

  Prerequisites (install once):
    1. Visual Studio 2022 Community (free) with "Desktop development with C++".
    2. Qt 6 (e.g. 6.8+) for MSVC2022 64-bit from the Qt online installer.
       In the installer tick "Additional Libraries -> Qt Charts" (required).
    3. CMake 3.16+ (winget install Kitware.CMake).

.PARAMETER QtPrefix
  Path to the Qt MSVC kit, e.g. C:\Qt\6.8.3\msvc2022_64. If omitted the script
  checks $env:CMAKE_PREFIX_PATH, then $env:Qt6_DIR, then C:\Qt\6*\msvc*_64.

.PARAMETER Config
  Build configuration: Release (default) or Debug.

.EXAMPLE
  .\build-windows.ps1
  .\build-windows.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
#>
param(
    [string]$QtPrefix = "",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-Qt {
    foreach ($cand in @($QtPrefix, $env:CMAKE_PREFIX_PATH, $env:Qt6_DIR)) {
        if ($cand -and (Test-Path (Join-Path $cand "lib\cmake\Qt6\Qt6Config.cmake"))) {
            return $cand
        }
    }
    $kits = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter "msvc*_64" -ErrorAction SilentlyContinue } |
        Sort-Object FullName -Descending
    foreach ($kit in $kits) {
        if (Test-Path (Join-Path $kit.FullName "lib\cmake\Qt6\Qt6Config.cmake")) {
            return $kit.FullName
        }
    }
    return $null
}

$qt = Find-Qt
if (-not $qt) {
    Write-Host "ERROR: Qt 6 MSVC kit not found." -ForegroundColor Red
    Write-Host "Install Qt 6 for MSVC2022 64-bit (with Qt Charts), then re-run:"
    Write-Host "  .\build-windows.ps1 -QtPrefix C:\Qt\<ver>\msvc2022_64"
    exit 1
}
Write-Host "Using Qt: $qt"

$cmakeArgs = @("-S", ".", "-B", "build-windows",
               "-DCMAKE_PREFIX_PATH=$qt",
               "-DCMAKE_BUILD_TYPE=$Config")

# Prefer Visual Studio generator when available, else fall back to MinGW/Ninja.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not $vs) {
        Write-Host "WARNING: MSVC not found via vswhere; letting CMake pick a generator." -ForegroundColor Yellow
    }
} else {
    Write-Host "WARNING: vswhere not found (no Visual Studio?); letting CMake pick a generator." -ForegroundColor Yellow
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build build-windows --config $Config -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Build OK. Running tests (offscreen)..." -ForegroundColor Green
$env:QT_QPA_PLATFORM = "offscreen"
$testExe = Join-Path "build-windows" $Config | Join-Path -ChildPath "openekklesia_tests.exe"
if (-not (Test-Path $testExe)) {
    # Single-config generators (MinGW/Ninja) drop the exe at the build root.
    $testExe = Join-Path "build-windows" "openekklesia_tests.exe"
}
& $testExe
if ($LASTEXITCODE -ne 0) {
    Write-Host "TESTS FAILED (see above)." -ForegroundColor Red
    exit $LASTEXITCODE
}

$appExe = Join-Path "build-windows" $Config | Join-Path -ChildPath "OpenEkklesia.exe"
if (-not (Test-Path $appExe)) {
    $appExe = Join-Path "build-windows" "OpenEkklesia.exe"
}
Write-Host ""
Write-Host "All green. Launch with:" -ForegroundColor Green
Write-Host "  $appExe"
Write-Host "First run on the Dell: detach, tap through Graph (pinch-zoom), and check"
Write-Host "Help -> About / log for 'touchMode' detection if anything feels off."
