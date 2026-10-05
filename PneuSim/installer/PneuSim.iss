; Inno Setup script for PneuSim. Built by the GitHub workflow:
;   iscc /DSourceDir=<folder with PneuSim.exe and examples> /DOutputDir=<out> PneuSim.iss
; Installs per user (no administrator rights needed) and opens .pneu files with PneuSim.

#ifndef AppVersion
  #define AppVersion "3.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\PneuSim"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6F1E9A52-3C1B-4B7E-9D0A-5A2C3E7B8D41}
AppName=PneuSim
AppVersion={#AppVersion}
AppVerName=PneuSim {#AppVersion}
AppPublisher=Akshaya Simha
AppCopyright=(c) 2026 Akshaya Simha
VersionInfoVersion={#AppVersion}
VersionInfoDescription=PneuSim Setup
DefaultDirName={autopf}\PneuSim
DefaultGroupName=PneuSim
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
OutputDir={#OutputDir}
OutputBaseFilename=PneuSim-Setup-{#AppVersion}
SetupIconFile=..\Resources\PneuSim.ico
UninstallDisplayIcon={app}\PneuSim.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=license.txt

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "associate"; Description: "Open .pneu circuit files with PneuSim"; GroupDescription: "File types:"

[Files]
Source: "{#SourceDir}\PneuSim.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\PneuSim.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\Examples\*"; DestDir: "{app}\Examples"; Flags: ignoreversion recursesubdirs
Source: "{#SourceDir}\Generated examples\*"; DestDir: "{app}\Generated examples"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\PneuSim"; Filename: "{app}\PneuSim.exe"
Name: "{autoprograms}\PneuSim examples"; Filename: "{app}\Examples"
Name: "{autodesktop}\PneuSim"; Filename: "{app}\PneuSim.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.pneu"; ValueType: string; ValueName: ""; ValueData: "PneuSim.Circuit"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PneuSim.Circuit"; ValueType: string; ValueName: ""; ValueData: "PneuSim circuit"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PneuSim.Circuit\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\PneuSim.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PneuSim.Circuit\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\PneuSim.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\PneuSim.exe"; Description: "{cm:LaunchProgram,PneuSim}"; Flags: nowait postinstall skipifsilent
