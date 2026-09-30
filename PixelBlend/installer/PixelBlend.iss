; Inno Setup script for PixelBlend  (https://jrsoftware.org/isinfo.php)
;
; Build the portable folder first (from the PixelBlend folder):
;   msbuild PixelBlend.sln /p:Configuration=Release
;   copy bin\Release\PixelBlend.exe and bin\Release\PixelBlend.exe.config into dist\
; then compile this script (from the PixelBlend folder):
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\PixelBlend.iss
; The installer is written to installer\Output\PixelBlend-Setup-<version>.exe

#define AppName "PixelBlend"
#define AppVersion "2.0.0"
#define AppExe "PixelBlend.exe"

[Setup]
AppId={{9A6C0A08-74F5-4100-9676-C160D7BDDA8C}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Akshaya Simha
AppCopyright=(c) 2023 Akshaya Simha
DefaultDirName={autopf}\PixelBlend
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); the user may choose
; "install for all users" in the dialog, which then needs elevation.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1
OutputDir=Output
OutputBaseFilename=PixelBlend-Setup-{#AppVersion}
SetupIconFile=..\tile.ico
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

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// PixelBlend needs .NET Framework 4.7.2 or later (built into Windows 10 April 2018 Update and newer).
function IsDotNet472Installed(): Boolean;
var
  Release: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full', 'Release', Release)
            and (Release >= 461808);
end;

function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;
  if not IsDotNet472Installed() then
  begin
    if SuppressibleMsgBox('PixelBlend needs Microsoft .NET Framework 4.7.2 or later, which is not installed on this PC.' + #13#10 + #13#10 +
                          'Click OK to open the download page, then run this setup again.',
                          mbError, MB_OKCANCEL, IDCANCEL) = IDOK then
      ShellExecAsOriginalUser('open', 'https://dotnet.microsoft.com/download/dotnet-framework/net472', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
    Result := False;
  end;
end;
