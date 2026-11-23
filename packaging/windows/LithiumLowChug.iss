; Lithium LowChug installer for Windows (Inno Setup 6 or 7).
;
; scripts/package.ps1 stages the build into a folder and compiles this with
;   ISCC /DAppVersion=<version> /DStaging=<staged folder> /DOutputDir=<dist> LithiumLowChug.iss
; The staged folder holds plugins\ (the .vst3 and .lv2 bundles), standalone\,
; LICENSE, THIRD-PARTY-NOTICES.txt and README.md.
;
; The wizard: welcome (what Lithium LowChug is and the two ways to use it),
; license, components (standalone app, VST3, LV2), Start menu and desktop
; shortcuts, install, and a finish page that offers to open the standalone
; app. The factory presets are built into the plugin, so there is no presets
; folder to install.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Staging
  #define Staging "..\..\dist\staging"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName "Lithium LowChug"
#define Company "Calamity Audio"
#define Author "Isaac Joseph"
#define Website "https://github.com/thecalamityjoe87"
#define Assets "..\..\assets\icon"

[Setup]
; Never change AppId: Windows uses it to find an earlier install to upgrade.
; It is LowChug's own (Lithium Drums has a different one), and must stay this
; value for every release from now on.
AppId={{957779C3-85A2-4455-B575-E9BD3ACADC00}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Company}
AppPublisherURL={#Website}
AppSupportURL={#Website}
AppCopyright=Copyright (C) 2026 {#Author}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#Company}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#Staging}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=LithiumLowChug-{#AppVersion}-windows-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The plugin folders are under Common Files, which needs admin rights.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName}
#if FileExists(AddBackslash(SourcePath) + Assets + "\lithium-lowchug.ico")
SetupIconFile={#Assets}\lithium-lowchug.ico
UninstallDisplayIcon={app}\Lithium LowChug.exe
#endif
#if FileExists(AddBackslash(SourcePath) + Assets + "\installer-side.bmp")
WizardImageFile={#Assets}\installer-side.bmp,{#Assets}\installer-side-2x.bmp
WizardSmallImageFile={#Assets}\installer-small.bmp,{#Assets}\installer-small-2x.bmp
#endif

[Messages]
WelcomeLabel2=This installs [name/ver] by {#Company}.%n%nLithium LowChug plays your guitar tuned lower (Eb, D, Drop C, B standard, an octave down) without retuning it. It works two ways:%n%n  - as a plugin (VST3 or LV2) in your DAW, on a guitar track;%n  - as a standalone app: plug in your guitar and play, no DAW needed.
FinishedLabel=Lithium LowChug is installed.%n%nIn your DAW, rescan plugins and add Lithium LowChug (by {#Company}) to a guitar track. Or open the standalone app from the Start menu.

[Types]
Name: "full"; Description: "Standalone app and all plugin formats"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone app (runs on its own, no DAW needed)"; Types: full custom
Name: "vst3"; Description: "VST3 plugin (Cubase, Reaper, Studio One, FL Studio, Ableton Live, Bitwig...)"; Types: full custom
Name: "lv2"; Description: "LV2 plugin (Reaper, Ardour, Carla...)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: standalone; Flags: unchecked

[Files]
Source: "{#Staging}\standalone\Lithium LowChug.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#Staging}\plugins\Lithium LowChug.vst3\*"; DestDir: "{commoncf64}\VST3\Lithium LowChug.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; LV2 goes in the system-wide folder the LV2 spec gives for Windows,
; %COMMONPROGRAMFILES%\LV2 (C:\Program Files\Common Files\LV2), next to VST3.
; (The per-user one, %APPDATA%\LV2, doesn't fit an admin install.)
Source: "{#Staging}\plugins\Lithium LowChug.lv2\*"; DestDir: "{commoncf64}\LV2\Lithium LowChug.lv2"; Components: lv2; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Staging}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Staging}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Staging}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Lithium LowChug"; Filename: "{app}\Lithium LowChug.exe"; Components: standalone
Name: "{group}\Uninstall Lithium LowChug"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Lithium LowChug"; Filename: "{app}\Lithium LowChug.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Lithium LowChug.exe"; Description: "Open the Lithium LowChug standalone app now"; Components: standalone; Flags: postinstall nowait skipifsilent unchecked

; Uninstalling removes only what the installer put there. The standalone
; app's settings (in %APPDATA%) stay.
