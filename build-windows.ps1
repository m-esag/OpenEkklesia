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

  Output: with the Visual Studio generator the executable lands in
  build-windows\$Config\OpenEkklesia.exe (for example
  build-windows\Release\OpenEkklesia.exe). Single-config generators
  (Ninja/MinGW) put it at build-windows\OpenEkklesia.exe. The script resolves
  the real path, prints its absolute form, and (unless -NoDeploy) copies the Qt
  runtime next to it with windeployqt so it can be launched by double-click.

.PARAMETER QtPrefix
  Path to the Qt MSVC kit, e.g. C:\Qt\6.8.3\msvc2022_64. If omitted the script
  checks $env:CMAKE_PREFIX_PATH, then $env:Qt6_DIR, then C:\Qt\6*\msvc*_64.

.PARAMETER Config
  Build configuration: Release (default) or Debug.

.PARAMETER SkipTests
  Build (and deploy) only; do not run the test suite.

.PARAMETER NoDeploy
  Do not run windeployqt. The executable then needs Qt's bin folder on PATH.

.PARAMETER Run
  Launch the built executable when the build (and tests) succeed.

.EXAMPLE
  .\build-windows.ps1
  .\build-windows.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
  .\build-windows.ps1 -Config Debug -SkipTests -Run
#>
param(
    [string]$QtPrefix = "",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$SkipTests,
    [switch]$NoDeploy,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-Qt {
    foreach ($cand in @($QtPrefix, $env:CMAKE_PREFIX_PATH, $env:Qt6_DIR)) {
        if ($cand -and (Test-Path (Join-Path $cand "lib\cmake\Qt6\Qt6Config.cmake"))) {
            return $cand
        }
    }
    # Version-aware ordering: a plain name sort is lexical, so "6.8.0" would
    # beat "6.10.0". Parse the version directory and prefer the newest kit.
    $kits = @()
    foreach ($verDir in (Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue)) {
        $ver = $null
        if (-not [version]::TryParse($verDir.Name, [ref]$ver)) { continue }
        foreach ($kit in (Get-ChildItem $verDir.FullName -Directory -Filter "msvc*_64" -ErrorAction SilentlyContinue)) {
            $kits += [pscustomobject]@{ Version = $ver; Path = $kit.FullName }
        }
    }
    $kits = $kits | Sort-Object -Property @{ Expression = { $_.Version }; Descending = $true },
                                           @{ Expression = { $_.Path }; Descending = $true }
    foreach ($kit in $kits) {
        if (Test-Path (Join-Path $kit.Path "lib\cmake\Qt6\Qt6Config.cmake")) {
            return $kit.Path
        }
    }
    return $null
}

# Visual Studio is multi-config (exe under build-windows\<Config>), Ninja/Make
# are single-config (exe under build-windows). Try both, in that order.
function Resolve-BuildOutput([string]$fileName) {
    $candidates = @(
        (Join-Path (Join-Path "build-windows" $Config) $fileName),
        (Join-Path "build-windows" $fileName)
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
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
$qtBin = Join-Path $qt "bin"
if ($env:PATH -notlike "*$qtBin*") {
    $env:PATH = "$qtBin;$env:PATH"
}

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

$jobs = $env:NUMBER_OF_PROCESSORS
if (-not $jobs) { $jobs = [Environment]::ProcessorCount }
& cmake --build build-windows --config $Config -j $jobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Report the executable before doing anything slow (tests, deploy): that way an
# interrupted or failing test run never leaves you wondering where it is.
$appExe = Resolve-BuildOutput "OpenEkklesia.exe"
if (-not $appExe) {
    Write-Host ""
    Write-Host "ERROR: the build reported success but OpenEkklesia.exe was not found." -ForegroundColor Red
    Write-Host "Searched: build-windows\$Config\OpenEkklesia.exe and build-windows\OpenEkklesia.exe"
    exit 1
}
Write-Host ""
Write-Host "Executable built:" -ForegroundColor Green
Write-Host "  $appExe" -ForegroundColor Green

if (-not $NoDeploy) {
    $windeployqt = Join-Path $qtBin "windeployqt.exe"

    # Stage the i18n dictionary next to the executable (<exe dir>/assets/i18n/)
    # so AppTranslator finds it in dev builds, the portable ZIP and the Inno
    # Setup installer (which ships this Release tree recursively).
    $i18nSrc = Join-Path $PSScriptRoot "assets\i18n\es.json"
    if (Test-Path $i18nSrc) {
        $i18nDir = Join-Path (Split-Path -Parent $appExe) "assets\i18n"
        New-Item -ItemType Directory -Force -Path $i18nDir | Out-Null
        Copy-Item $i18nSrc $i18nDir -Force
    }

    if (Test-Path $windeployqt) {
        Write-Host "Deploying Qt runtime next to the executable (windeployqt)..."
        $deployMode = if ($Config -eq "Debug") { "--debug" } else { "--release" }
        $deployArgs = @("--no-translations", "--no-opengl-sw", "--no-system-d3d-compiler",
                        "--no-system-dxc-compiler", "--no-compiler-runtime", $deployMode,
                        "--dir", (Split-Path -Parent $appExe), $appExe)
        # Native tools write warnings to stderr; with ErrorActionPreference=Stop
        # that would abort the script even though windeployqt succeeded.
        $previousEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $windeployqt @deployArgs 2>&1 | Out-Null
        $deployExit = $LASTEXITCODE
        $ErrorActionPreference = $previousEap
        if ($deployExit -ne 0) {
            Write-Host "WARNING: windeployqt failed; launch with Qt's bin on PATH or re-run with -NoDeploy." -ForegroundColor Yellow
        } else {
            Write-Host "Qt runtime deployed - the executable can be started directly."
        }
    } else {
        Write-Host "WARNING: windeployqt.exe not found in $qtBin; add it to PATH before launching." -ForegroundColor Yellow
    }
}

if (-not $SkipTests) {
    Write-Host ""
    Write-Host "Running tests (offscreen)..." -ForegroundColor Green
    $env:PATH = "$qtBin;$env:PATH"
    $testExe = Resolve-BuildOutput "openekklesia_tests.exe"
    if (-not $testExe) {
        Write-Host "WARNING: openekklesia_tests.exe not found; skipping the test run." -ForegroundColor Yellow
    } else {
        # The deployed build only ships the "windows" platform plugin, but the
        # widget suite needs the "offscreen" one. Point Qt at the kit's plugin
        # directory for the test run so deployment never breaks the suite.
        $previousPluginPath = $env:QT_PLUGIN_PATH
        $env:QT_PLUGIN_PATH = Join-Path $qt "plugins"
        $env:QT_QPA_PLATFORM = "offscreen"
        # Capture the test process's two streams separately: Windows PowerShell
        # turns a native tool's stderr into scary NativeCommandError records,
        # and the harness legitimately logs to stderr.
        $testOut = Join-Path $env:TEMP ("oe_tests_" + [guid]::NewGuid().ToString("N") + ".out")
        $testErr = Join-Path $env:TEMP ("oe_tests_" + [guid]::NewGuid().ToString("N") + ".err")
        Push-Location (Split-Path -Parent $testExe)
        try {
            $testProc = Start-Process -FilePath $testExe -WorkingDirectory (Get-Location).Path `
                -RedirectStandardOutput $testOut -RedirectStandardError $testErr `
                -NoNewWindow -Wait -PassThru
            $testExit = $testProc.ExitCode
        } finally {
            Pop-Location
            $env:QT_PLUGIN_PATH = $previousPluginPath
        }
        if (Test-Path -LiteralPath $testOut) { Get-Content -LiteralPath $testOut }
        if (Test-Path -LiteralPath $testErr) { Get-Content -LiteralPath $testErr }
        Remove-Item -LiteralPath $testOut, $testErr -ErrorAction SilentlyContinue
        if ($testExit -ne 0) {
            Write-Host ""
            Write-Host "TESTS FAILED (see above). The executable itself is still at:" -ForegroundColor Red
            Write-Host "  $appExe"
            exit $testExit
        }
    }
}

Write-Host ""
Write-Host "All green. Executable:" -ForegroundColor Green
Write-Host "  $appExe" -ForegroundColor Green
Write-Host ""
if ($NoDeploy) {
    Write-Host "Qt's bin folder is not copied next to the exe; launch it from a shell with:"
    Write-Host "  `$env:PATH = `"$qtBin;`" + `$env:PATH; & `"$appExe`""
} else {
    Write-Host "Double-click it, or run: `"$appExe`""
}
Write-Host "First run stores data in %APPDATA%\OpenEkklesia."
Write-Host "First run on the Dell: detach, tap through Graph (pinch-zoom), and check"
Write-Host "Help -> About / log for 'touchMode' detection if anything feels off."

if ($Run) {
    Start-Process -FilePath $appExe -WorkingDirectory (Split-Path -Parent $appExe)
}
