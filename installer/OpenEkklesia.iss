; ===========================================================================
; OpenEkklesia - Inno Setup 6 installer script.
;
; Build it through build-windows-installer.ps1 in the repository root. That
; script builds the deployed Release tree with build-windows.ps1 -SkipTests,
; locates ISCC.exe and passes these /D defines:
;
;   /DMyAppVersion=0.8.0      application version, taken from CMakeLists.txt
;   /DMyArch=x64compatible    x64, when an Inno Setup older than 6.3 is found
;   /DHasIcon=1               only when assets\openekklesia.ico exists
;
; Paths are deliberately written inline (relative to this script's own
; directory, which is the compiler's source directory) instead of being passed
; as defines: a define value is an Inno preprocessor string, and Windows paths
; are better kept out of that string syntax entirely.
;
; Running ISCC.exe on this file directly from the installer folder works too;
; only the version would be missing (the default below matches CMakeLists.txt).
;
; Toast notifications: OpenEkklesia calls
; SetCurrentProcessExplicitAppUserModelID(L"OpenEkklesia.ChMS") in src\main.cpp.
; Windows only keeps an application's notification-area messages in the Action
; Center when the process was started from a Start Menu shortcut whose shortcut
; AppUserModelID equals that string. Every shortcut created below therefore
; sets AppUserModelID. Keep this value in sync with src\main.cpp if the app ID
; ever changes.
; ===========================================================================

#define MyAppName "OpenEkklesia"
#ifndef MyAppVersion
  ; Fallback for direct ISCC runs; the pipeline always passes the version
  ; parsed from CMakeLists.txt via /DMyAppVersion.
  #define MyAppVersion "0.8.4"
#endif
#ifndef MyAppPublisher
  #define MyAppPublisher "OpenEkklesia"
#endif

#define MyAppExeName "OpenEkklesia.exe"

; Must equal the argument of SetCurrentProcessExplicitAppUserModelID() in
; src\main.cpp (line 22): L"OpenEkklesia.ChMS".
#define MyAppUserModelID "OpenEkklesia.ChMS"

; "x64compatible" needs Inno Setup 6.3 or newer (it also accepts ARM64 Windows
; that emulate the x64 build). build-windows-installer.ps1 passes "x64" when it
; detects an older 6.x compiler, so direct ISCC runs on 6.0-6.2 should pass
; /DMyArch=x64 as well.
#ifndef MyArch
  #define MyArch "x64compatible"
#endif

[Setup]
; Stable application identity - changing it breaks upgrades and uninstall
; entries for every existing installation.
AppId={{AC730911-EFCB-4CA9-BDFE-70A20966F8EA}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoDescription={#MyAppName} installer
VersionInfoProductName={#MyAppName}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; "../dist" is relative to this script's directory, i.e. <repo>\dist.
OutputDir=..\dist
OutputBaseFilename=OpenEkklesia-{#MyAppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Log every install to %TEMP%\Setup Log*.txt - invaluable when a user reports a
; silent-install or deployment problem.
SetupLogging=yes
; Machine-wide install: Program Files plus a common Start Menu shortcut, so the
; toast-capable shortcut exists for every Windows account on the PC without a
; per-user installation. This is why admin rights are required.
PrivilegesRequired=admin
MinVersion=10.0
ArchitecturesAllowed={#MyArch}
ArchitecturesInstallIn64BitMode={#MyArch}
#ifdef HasIcon
SetupIconFile=..\assets\openekklesia.ico
UninstallDisplayIcon={app}\openekklesia.ico
#else
UninstallDisplayIcon={app}\{#MyAppExeName}
#endif
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start OpenEkklesia when Windows starts"; GroupDescription: "Startup:"; Flags: unchecked

[Registry]
; Auto-start (task-selected): per-user HKCU Run entry so a non-admin login
; session can launch the machine-wide install. Removed on uninstall.
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "OpenEkklesia.ChMS"; \
    ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Files]
; The complete windeployqt deployment produced by build-windows.ps1: the
; executable, Qt DLLs and the plugin folders (platforms, sqldrivers, styles,
; tls, imageformats, ...). Test/benchmark binaries and linker by-products from
; the shared build folder are deliberately not shipped. The dev-state files
; (.portable marker, local database, WAL/SHM sidecars, data/ and backups/)
; are excluded too: the installed flavor must run in APPDATA mode, and a dev
; database must never end up inside Program Files.
Source: "..\build-windows\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "openekklesia_tests.exe,openekklesia_benchmarks.exe,*.pdb,*.ilk,*.exp,*.lib,*.obj,*.map,.portable,open_ekklesia.db*,data,backups"
#ifdef HasIcon
; Installed so the shortcuts below (and the uninstall entry) can show the
; proper artwork even though the executable itself carries no icon resource.
Source: "..\assets\openekklesia.ico"; DestDir: "{app}"; DestName: "openekklesia.ico"; Flags: ignoreversion
#endif

[Icons]
; AppUserModelID is the toast/Action Center requirement: the shortcut carries
; the same app ID that src\main.cpp registers for the running process.
#ifdef HasIcon
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\openekklesia.ico"; AppUserModelID: "{#MyAppUserModelID}"
#else
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppUserModelID}"
#endif
#ifdef HasIcon
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\openekklesia.ico"; Tasks: desktopicon; AppUserModelID: "{#MyAppUserModelID}"
#else
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; AppUserModelID: "{#MyAppUserModelID}"
#endif
; Uninstall shortcut in the same Start Menu folder (the "Apps & features" entry
; is added automatically through the uninstall registry data).
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
