; Inno Setup script for Graphing  (https://jrsoftware.org/isinfo.php)
;
; Build the self-contained program first (from the Graphing folder):
;   dotnet publish Graphing.vbproj -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o dist
; then compile this script (from the Graphing folder):
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\Graphing.iss
; The installer is written to installer\Output\Graphing-Setup-<version>.exe

#define AppName "Graphing"
#define AppVersion "2.0.0"
#define AppExe "Graphing.exe"
#define ProgId "Graphing.Project"

[Setup]
AppId={{89537E33-96B5-4108-8F1F-512FA0FCE80F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=ASRD
AppCopyright=(c) 2026 ASRD
DefaultDirName={autopf}\Graphing
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); the user may choose
; "install for all users" in the dialog, which then needs elevation.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
OutputDir=Output
OutputBaseFilename=Graphing-Setup-{#AppVersion}
SetupIconFile=..\Graphing.ico
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
; Double-clicking a .graphproj file opens it in Graphing.
Root: HKA; Subkey: "Software\Classes\.graphproj"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Graphing project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
