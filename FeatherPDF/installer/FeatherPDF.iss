; Inno Setup script for Feather PDF  (https://jrsoftware.org/isinfo.php)
;
; Build the portable folder first:
;   cmake --install build --config Release --prefix dist
; then compile this script (from the FeatherPDF folder):
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\FeatherPDF.iss
; The installer is written to installer\Output\FeatherPDF-Setup-<version>.exe

#define AppName "Feather PDF"
#define AppVersion "1.3.0"
#define AppExe "FeatherPDF.exe"
#define ProgId "FeatherPDF.Document"

[Setup]
AppId={{6CF354F2-02CD-4DDA-AA26-1547B4A9D980}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Akshaya Simha
AppCopyright=(c) 2026 Akshaya Simha
DefaultDirName={autopf}\Feather PDF
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
OutputBaseFilename=FeatherPDF-Setup-{#AppVersion}
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
; ProgID
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "PDF Document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
; "Open with" entry for .pdf
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
; Capabilities: lists the app under Settings > Apps > Default apps
Root: HKA; Subkey: "Software\FeatherPDF\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\FeatherPDF\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "A fast, lightweight PDF viewer."
Root: HKA; Subkey: "Software\FeatherPDF\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#ProgId}"
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "FeatherPDF"; ValueData: "Software\FeatherPDF\Capabilities"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; Windows does not let installers silently take over .pdf; offer the settings page instead.
Filename: "ms-settings:defaultapps?registeredAppUser=FeatherPDF"; Description: "Choose Feather PDF as the default PDF viewer"; Flags: shellexec postinstall skipifsilent unchecked nowait
