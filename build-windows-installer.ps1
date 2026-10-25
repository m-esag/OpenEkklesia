#Requires -Version 5.1
<#
.SYNOPSIS
  Build the Windows installer (Inno Setup 6) or a portable ZIP for OpenEkklesia.

.DESCRIPTION
  Default mode builds the application with 'build-windows.ps1 -SkipTests',
  verifies that windeployqt really deployed the Qt runtime next to
  OpenEkklesia.exe, and compiles
  'dist\OpenEkklesia-<version>-win64-setup.exe' with Inno Setup 6.

  The installer copies the whole deployed Release tree, registers an
  uninstall entry and creates a Start Menu shortcut (plus an optional desktop
  shortcut) whose AppUserModelID is "OpenEkklesia.ChMS" - exactly the value
  src\main.cpp passes to SetCurrentProcessExplicitAppUserModelID(). Windows
  requires a Start Menu shortcut carrying the process AppUserModelID before it
  keeps an application's notification-area messages in the Action Center, so
  this shortcut is also what enables native toast notifications.

  '-Portable' builds the same tree into
  'dist\OpenEkklesia-<version>-win64-portable.zip' instead and does not need
  Inno Setup at all.

  When ISCC.exe cannot be found the script prints the winget command and exits
  with code 2. Pass '-InstallTool' to run that winget command first (winget
  may raise a UAC prompt).

.PARAMETER SkipBuild
  Use the existing build-windows\Release tree; do not run build-windows.ps1.

.PARAMETER Portable
  Produce the portable ZIP instead of the installer. Inno Setup is not needed.

.PARAMETER InstallTool
  Install Inno Setup via winget when ISCC.exe cannot be found.

.PARAMETER IsccPath
  Explicit path to ISCC.exe, or to the folder that contains it.

.PARAMETER QtPrefix
  Forwarded to build-windows.ps1: path of the Qt MSVC kit to build against.

.PARAMETER RegenerateIcon
  Rebuild assets\openekklesia.ico from assets\openekklesia.png before
  packaging (requires Python on PATH). Without this switch an existing .ico is
  used as-is; a missing .ico means the installer is built without an icon.

.EXAMPLE
  .\build-windows-installer.ps1

.EXAMPLE
  .\build-windows-installer.ps1 -SkipBuild

.EXAMPLE
  .\build-windows-installer.ps1 -Portable -SkipBuild

.EXAMPLE
  .\build-windows-installer.ps1 -InstallTool -SkipBuild

.NOTES
  Exit codes:
    0  success
    1  build failed or the deployed Release tree is incomplete
    2  Inno Setup (ISCC.exe) is missing and could not be installed
    3  ISCC reported success but the expected setup file was not produced
  A failing ISCC run returns ISCC's own exit code.

  The artifacts are not code-signed, so Windows SmartScreen may warn on first
  run. Test binaries from the build folder are never packaged.
#>
param(
    [switch]$SkipBuild,
    [switch]$Portable,
    [switch]$InstallTool,
    [string]$IsccPath = "",
    [string]$QtPrefix = "",
    [switch]$RegenerateIcon
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repoRoot = (Get-Location).Path

$releaseDir = Join-Path $repoRoot "build-windows\Release"
$distDir = Join-Path $repoRoot "dist"

function Fail {
    param([string]$Message, [int]$Code)
    Write-Host ""
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit $Code
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-AppVersion {
    $cmakeLists = Join-Path $repoRoot "CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $cmakeLists)) {
        Fail "CMakeLists.txt not found in '$repoRoot'. Run this script from the OpenEkklesia repository." 1
    }
    $projectLine =
        Select-String -LiteralPath $cmakeLists -Pattern 'project\s*\(\s*OpenEkklesia\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
        Select-Object -First 1
    if (-not $projectLine) {
        Fail "Cannot read the application version from CMakeLists.txt (expected 'project(OpenEkklesia VERSION x.y.z ...)')." 1
    }
    return $projectLine.Matches[0].Groups[1].Value
}

function Invoke-AppBuild {
    $buildScript = Join-Path $repoRoot "build-windows.ps1"
    if (-not (Test-Path -LiteralPath $buildScript)) {
        Fail "build-windows.ps1 was not found next to this script." 1
    }

    # Re-enter the same PowerShell edition so $LASTEXITCODE always reflects the
    # build result and a failure inside build-windows.ps1 cannot terminate this
    # script before the artifacts are checked.
    if ($PSVersionTable.PSEdition -eq "Core") { $hostExe = "pwsh.exe" } else { $hostExe = "powershell.exe" }
    $hostPath = Join-Path $PSHOME $hostExe
    if (-not (Test-Path -LiteralPath $hostPath)) { $hostPath = $hostExe }

    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $buildScript, "-Config", "Release", "-SkipTests")
    if ($QtPrefix) { $arguments += @("-QtPrefix", $QtPrefix) }

    Write-Step "Building the deployed Release tree (build-windows.ps1 -SkipTests)"
    & $hostPath @arguments | Out-Host
    $buildExit = $LASTEXITCODE
    if ($null -eq $buildExit) { $buildExit = 1 }
    if ($buildExit -ne 0) {
        Fail ("build-windows.ps1 failed with exit code {0}. Fix the build first; an installer is never produced from a broken tree." -f $buildExit) $buildExit
    }
}

function Assert-DeployedTree {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        Fail ("The deployed Release folder '{0}' does not exist.`nBuild it first with:  .\build-windows.ps1 -SkipTests`n(or re-run this script without -SkipBuild)." -f $Path) 1
    }

    $required = @(
        "OpenEkklesia.exe",
        "Qt6Core.dll",
        "platforms\qwindows.dll",
        "sqldrivers\qsqlite.dll"
    )
    $missing = @()
    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $relative))) { $missing += $relative }
    }
    if ($missing.Count -gt 0) {
        Fail ("The Release tree is incomplete - missing: {0}`nRe-run the build without -NoDeploy so windeployqt copies the Qt runtime, plugins and SQL drivers next to the executable:`n  .\build-windows.ps1 -SkipTests" -f ($missing -join ", ")) 1
    }

    # windeployqt occasionally leaves 0-byte files behind (for example a plugin
    # that was locked). They would ship as broken plugins, so warn loudly.
    $emptyFiles = @(Get-ChildItem -LiteralPath $Path -Recurse -File | Where-Object { $_.Length -eq 0 })
    if ($emptyFiles.Count -gt 0) {
        $names = ($emptyFiles | ForEach-Object { $_.FullName.Substring($Path.Length + 1) }) -join ", "
        Write-Host ""
        Write-Warning "The deployed tree contains $($emptyFiles.Count) empty file(s): $names"
        Write-Warning "Empty Qt plugins or DLLs break those features at runtime. Re-run .\build-windows.ps1 -SkipTests (without -NoDeploy) before shipping."
    }
}

function Resolve-AppIcon {
    $icon = Join-Path $repoRoot "assets\openekklesia.ico"
    $existing = Test-Path -LiteralPath $icon
    if ($existing -and -not $RegenerateIcon) { return $icon }

    $generator = Join-Path $repoRoot "installer\make-icon.py"
    if (-not (Test-Path -LiteralPath $generator)) {
        Write-Warning "assets\openekklesia.ico is missing and installer\make-icon.py was not found; packaging without an icon."
        return $null
    }
    if (-not $RegenerateIcon) {
        # Do not auto-run "python" here: on machines without Python that name
        # is often the Microsoft Store alias, which would pop up the Store.
        Write-Warning "assets\openekklesia.ico is missing; packaging without an icon. Regenerate it with: python installer\make-icon.py"
        return $null
    }

    $python = Get-Command -Name "python.exe" -ErrorAction SilentlyContinue
    if (-not $python) {
        Write-Warning "Python was not found on PATH, so assets\openekklesia.ico cannot be regenerated; packaging without an icon."
        return $null
    }

    Write-Step "Generating assets\openekklesia.ico from assets\openekklesia.png"
    & $python.Source $generator | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $icon)) {
        Write-Warning "Icon generation failed; continuing without an icon (the installer still works)."
        return $null
    }
    return $icon
}

function Find-Iscc {
    if ($IsccPath) {
        if (Test-Path -LiteralPath $IsccPath -PathType Container) {
            $candidate = Join-Path $IsccPath "ISCC.exe"
            if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
        } elseif (Test-Path -LiteralPath $IsccPath) {
            return (Resolve-Path -LiteralPath $IsccPath).Path
        }
        Fail "ISCC.exe was not found at '$IsccPath'. Point -IsccPath at the compiler or at the folder containing it." 2
    }

    $onPath = Get-Command -Name "ISCC.exe" -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    # Standard per-machine and per-user install locations.
    $direct = @()
    if (${env:ProgramFiles(x86)}) { $direct += (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe") }
    if ($env:ProgramFiles) { $direct += (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe") }
    if ($env:LOCALAPPDATA) { $direct += (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe") }
    foreach ($candidate in $direct) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    # Any "Inno Setup *" folder under the usual program roots.
    $installRoots = @()
    if ($env:ProgramFiles) { $installRoots += $env:ProgramFiles }
    if (${env:ProgramFiles(x86)}) { $installRoots += ${env:ProgramFiles(x86)} }
    if ($env:LOCALAPPDATA) { $installRoots += (Join-Path $env:LOCALAPPDATA "Programs") }
    foreach ($root in $installRoots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $folders = Get-ChildItem -LiteralPath $root -Directory -Filter "Inno Setup*" -ErrorAction SilentlyContinue
        foreach ($folder in $folders) {
            $candidate = Join-Path $folder.FullName "ISCC.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    # Last resort: the uninstall registry entry written by Inno Setup itself.
    $uninstallKeys = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"
    )
    foreach ($key in $uninstallKeys) {
        if (-not (Test-Path -LiteralPath $key)) { continue }
        $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($entry -and $entry.InstallLocation) {
            $candidate = Join-Path $entry.InstallLocation "ISCC.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    return $null
}

function Install-InnoSetup {
    $winget = Get-Command -Name "winget.exe" -ErrorAction SilentlyContinue
    if (-not $winget) {
        Fail "winget is not available, so -InstallTool cannot install Inno Setup. Install it manually from https://jrsoftware.org/isdl.php and re-run this script." 2
    }
    Write-Step "Installing Inno Setup 6 with winget (a UAC prompt may appear)"
    & $winget.Source install --id JRSoftware.InnoSetup -e --accept-source-agreements --accept-package-agreements | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "winget exited with code $LASTEXITCODE (this can also mean the package is already installed)." -ForegroundColor Yellow
    }
}

function Get-IsccVersion {
    param([string]$Path)
    try {
        $versionText = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
        if (-not $versionText) { $versionText = (Get-Item -LiteralPath $Path).VersionInfo.FileVersion }
        if ($versionText) {
            $versionText = ($versionText -split "\s+")[0]
            $parsed = $null
            if ([version]::TryParse($versionText, [ref]$parsed)) { return $parsed }
        }
    } catch {
        # Unknown version: assume a current compiler and use the modern define.
    }
    return $null
}

function New-PortableZip {
    param([string]$Path, [string]$Destination, [string]$Version)

    $folderName = "OpenEkklesia-$Version-win64"
    $stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("OpenEkklesia-portable-" + [guid]::NewGuid().ToString("N"))
    $stagingApp = Join-Path $stagingRoot $folderName

    try {
        New-Item -ItemType Directory -Path $stagingApp -Force | Out-Null
        Write-Host "    Staging the deployed tree..."
        Copy-Item -Path (Join-Path $Path "*") -Destination $stagingApp -Recurse -Force

        # Never ship the test harness, the benchmark runner or linker leftovers.
        foreach ($name in @("openekklesia_tests.exe", "openekklesia_benchmarks.exe")) {
            $stale = Join-Path $stagingApp $name
            if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force }
        }
        $dropExtensions = @(".pdb", ".ilk", ".exp", ".lib", ".obj", ".map")
        Get-ChildItem -LiteralPath $stagingApp -Recurse -File |
            Where-Object { $dropExtensions -contains $_.Extension.ToLowerInvariant() } |
            Remove-Item -Force

        # Short note so a portable user knows where data lives and why toasts
        # may not stick to the Action Center without the installer's shortcut.
        $readme = @(
            "OpenEkklesia $Version - portable Windows build",
            "",
            "No installation is needed: double-click OpenEkklesia.exe.",
            "",
            "Data is stored per Windows user in:",
            "  %APPDATA%\OpenEkklesia",
            "Copy that folder to move the church's data to another PC.",
            "",
            "About Action Center notifications:",
            "Windows only keeps this app's reminder notifications in the Action",
            "Center when it was started from a Start Menu shortcut whose",
            "AppUserModelID is OpenEkklesia.ChMS. This ZIP does not create such",
            "a shortcut, so notifications may appear only as transient messages.",
            "Use the installer (OpenEkklesia-<version>-win64-setup.exe) if you",
            "want full toast behaviour.",
            "",
            "The Qt runtime, plugins and SQL drivers are included, so this",
            "folder is self-contained once extracted."
        )
        Set-Content -LiteralPath (Join-Path $stagingApp "PORTABLE-README.txt") -Value $readme -Encoding ASCII

        if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
        Write-Host "    Compressing..."
        Compress-Archive -Path (Join-Path $stagingRoot "*") -DestinationPath $Destination -CompressionLevel Optimal -Force

        if (-not (Test-Path -LiteralPath $Destination)) {
            Fail "Compress-Archive did not produce '$Destination'." 3
        }

        # Re-open the archive: a zip that cannot be listed is not shippable.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [System.IO.Compression.ZipFile]::OpenRead($Destination)
        try {
            $entryCount = $archive.Entries.Count
        } finally {
            $archive.Dispose()
        }
        if ($entryCount -lt 3) {
            Fail "The portable ZIP contains only $entryCount entries; the deployment tree looks empty." 3
        }
        Write-Host "    ZIP contains $entryCount entries."
    } finally {
        if (Test-Path -LiteralPath $stagingRoot) {
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function New-Installer {
    param([string]$Iscc, [string]$IconPath, [string]$Version)

    $issFile = Join-Path $repoRoot "installer\OpenEkklesia.iss"
    if (-not (Test-Path -LiteralPath $issFile)) {
        Fail "The Inno Setup script '$issFile' is missing." 1
    }

    $setupPath = Join-Path $distDir "OpenEkklesia-$Version-win64-setup.exe"

    # Inno Setup 6.3 introduced "x64compatible"; older 6.x compilers only know
    # "x64". Keep the .iss default modern and downgrade only for old tooling.
    $architecture = "x64compatible"
    $isccVersion = Get-IsccVersion -Path $Iscc
    if ($isccVersion -and $isccVersion.Major -eq 6 -and $isccVersion.Minor -lt 3) {
        $architecture = "x64"
        Write-Host ("    Inno Setup {0} predates 'x64compatible'; compiling with the legacy 'x64' architecture instead. Upgrading (winget upgrade JRSoftware.InnoSetup) is recommended." -f $isccVersion) -ForegroundColor Yellow
    }

    # Only scalars cross the /D boundary: the script resolves all paths itself
    # (it runs with the repository layout this script validates), so no Windows
    # path ever has to survive Inno preprocessor string syntax.
    $arguments = @(
        "/DMyAppVersion=$Version",
        "/DMyArch=$architecture"
    )
    if ($IconPath) { $arguments += "/DHasIcon=1" }
    $arguments += $issFile

    Write-Step "Compiling the installer with $Iscc"
    & $Iscc @arguments | Out-Host
    $isccExit = $LASTEXITCODE
    if ($null -eq $isccExit) { $isccExit = 1 }
    if ($isccExit -ne 0) {
        Fail ("ISCC.exe failed with exit code {0}. The installer was NOT produced; fix the compiler errors printed above (script: installer\OpenEkklesia.iss)." -f $isccExit) $isccExit
    }
    if (-not (Test-Path -LiteralPath $setupPath)) {
        Fail "ISCC.exe reported success but '$setupPath' does not exist. Check OutputDir and OutputBaseFilename in installer\OpenEkklesia.iss." 3
    }
    return $setupPath
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Write-Step "OpenEkklesia Windows packaging"
$appVersion = Get-AppVersion
Write-Host "    Repository: $repoRoot"
Write-Host "    Version:    $appVersion"
if ($Portable) {
    Write-Host "    Mode:       portable ZIP (Inno Setup not required)"
} else {
    Write-Host "    Mode:       installer (Inno Setup 6)"
}

if ($SkipBuild) {
    Write-Host "    Build:      skipped (-SkipBuild); using the existing Release tree."
} else {
    Invoke-AppBuild
}

Assert-DeployedTree -Path $releaseDir
if (-not (Test-Path -LiteralPath $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}

$iconPath = $null
if (-not $Portable) {
    $iconPath = Resolve-AppIcon
    if ($iconPath) {
        Write-Host "    Icon:       $iconPath"
    } else {
        Write-Host "    Icon:       none (installer builds without SetupIconFile / shortcut icon)"
    }
}

$produced = @()
if ($Portable) {
    Write-Step "Creating the portable ZIP"
    $zipPath = Join-Path $distDir "OpenEkklesia-$appVersion-win64-portable.zip"
    New-PortableZip -Path $releaseDir -Destination $zipPath -Version $appVersion
    $produced += $zipPath
} else {
    $iscc = Find-Iscc
    if (-not $iscc -and $InstallTool) {
        Install-InnoSetup
        $iscc = Find-Iscc
    }
    if (-not $iscc) {
        Write-Host ""
        Write-Host "Inno Setup 6 (ISCC.exe) was not found on PATH or in the usual install folders." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Install it with one of:" -ForegroundColor Yellow
        Write-Host "  winget install --id JRSoftware.InnoSetup -e --accept-source-agreements --accept-package-agreements"
        Write-Host "  https://jrsoftware.org/isdl.php"
        Write-Host ""
        Write-Host "Then re-run:                      .\build-windows-installer.ps1 -SkipBuild"
        Write-Host "Or let this script install it:    .\build-windows-installer.ps1 -SkipBuild -InstallTool"
        Write-Host "Or point at an existing compiler: .\build-windows-installer.ps1 -IsccPath <folder with ISCC.exe>"
        Write-Host ""
        Write-Host "Inno Setup is not needed for the portable ZIP:"
        Write-Host "  .\build-windows-installer.ps1 -Portable -SkipBuild"
        Write-Host ""
        exit 2
    }
    Write-Host "    Compiler:   $iscc"
    $setupPath = New-Installer -Iscc $iscc -IconPath $iconPath -Version $appVersion
    $produced += $setupPath
}

Write-Host ""
Write-Host "Artifacts:" -ForegroundColor Green
foreach ($artifact in $produced) {
    $item = Get-Item -LiteralPath $artifact
    $hash = (Get-FileHash -Path $artifact -Algorithm SHA256).Hash
    Write-Host "  $artifact" -ForegroundColor Green
    Write-Host ("    {0:n1} MiB   sha256 {1}" -f ($item.Length / 1MB), $hash)
}
Write-Host ""
Write-Host "The artifacts are not code-signed, so Windows SmartScreen may warn on first run."
if (-not $Portable) {
    Write-Host "The installed Start Menu shortcut carries AppUserModelID 'OpenEkklesia.ChMS', which is what"
    Write-Host "lets Windows keep OpenEkklesia reminder toasts in the Action Center."
}
