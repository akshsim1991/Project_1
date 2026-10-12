; Inno Setup script for Feather CAD  (https://jrsoftware.org/isinfo.php)
;
; Build the portable folder first:
;   cmake --install build --config Release --prefix dist
; then compile this script (from the FeatherCAD folder):
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\FeatherCAD.iss
; The installer is written to installer\Output\FeatherCAD-Setup-<version>.exe

#define AppName "Feather CAD"
#define AppVersion "1.7.1"
#define AppExe "FeatherCAD.exe"
#define ProgId "FeatherCAD.Drawing"

[Setup]
AppId={{0B9D2A6E-5C1F-4E43-9B57-3E2D8A71C4F0}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Akshaya Simha
AppCopyright=(c) 2026 Akshaya Simha
DefaultDirName={autopf}\Feather CAD
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); "install for all users"
; can be chosen in the dialog, which then needs elevation.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
OutputDir=Output
OutputBaseFilename=FeatherCAD-Setup-{#AppVersion}
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; .fcad is Feather CAD's own format, so it opens with Feather CAD.
Root: HKA; Subkey: "Software\Classes\.fcad"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Feather CAD drawing"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
