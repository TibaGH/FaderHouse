; Windows installer for Faderhouse (Inno Setup 6.3 or newer).
;
;   iscc /DAppVersion=1.1.0 /DSourceExe=..\..\build\Faderhouse_artefacts\Release\Faderhouse.exe /DOutputDir=..\..\dist Faderhouse.iss
;
; Makes Faderhouse-<version>-Setup.exe. The in-app updater runs it with /SILENT; after a
; silent install it starts Faderhouse again.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\..\build\Faderhouse_artefacts\Release\Faderhouse.exe"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
; Extracted VB-CABLE driver pack (VB-Audio, donationware, www.vb-cable.com). Optional.
#ifndef CableDir
  #define CableDir ""
#endif
; Faderhouse voice plugins (VST3) for other audio apps. Optional.
#ifndef PluginsDir
  #define PluginsDir ""
#endif

[Setup]
AppId={{96CC0BC9-6483-4578-B3FB-1B4558EDA075}
AppName=Faderhouse
AppVersion={#AppVersion}
AppVerName=Faderhouse {#AppVersion}
AppPublisher=Balogh Productions
AppPublisherURL=https://github.com/TibaGH/faderhouse
AppSupportURL=https://github.com/TibaGH/faderhouse/issues
AppUpdatesURL=https://github.com/TibaGH/faderhouse/releases
DefaultDirName={autopf}\Faderhouse
DefaultGroupName=Faderhouse
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir={#OutputDir}
OutputBaseFilename=Faderhouse-{#AppVersion}-Setup
SetupIconFile=..\..\Resources\AppIcon.ico
UninstallDisplayIcon={app}\Faderhouse.exe
UninstallDisplayName=Faderhouse
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Tasks]
#if CableDir != ""
Name: "vbcable"; Description: "Install VB-CABLE for Mic FX (your processed mic for Discord / OBS). VB-CABLE is made by VB-Audio (www.vb-cable.com) and is donationware: all participations are welcome."; Check: not CableAlreadyInstalled
#endif
#if PluginsDir != ""
Name: "vst3"; Description: "Install the Faderhouse voice plugins (VST3) for your other audio apps. Inside Faderhouse they are built in."
#endif
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion
#if CableDir != ""
Source: "{#CableDir}\*"; DestDir: "{app}\VB-CABLE"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

#if PluginsDir != ""
Source: "{#PluginsDir}\*"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3
#endif

[Icons]
Name: "{autoprograms}\Faderhouse"; Filename: "{app}\Faderhouse.exe"
Name: "{autodesktop}\Faderhouse"; Filename: "{app}\Faderhouse.exe"; Tasks: desktopicon

[Run]
#if CableDir != ""
; VB-CABLE first. Windows asks once whether to trust VB-Audio's driver.
Filename: "{app}\VB-CABLE\VBCABLE_Setup_x64.exe"; Parameters: "-i -h"; StatusMsg: "Installing VB-CABLE (Mic FX)..."; Flags: waituntilterminated; Tasks: vbcable
#endif
; Normal install: the usual "Launch Faderhouse" tick box on the last page.
Filename: "{app}\Faderhouse.exe"; Description: "{cm:LaunchProgram,Faderhouse}"; Flags: nowait postinstall skipifsilent runasoriginaluser
; In-app update (silent): open Faderhouse again straight away.
Filename: "{app}\Faderhouse.exe"; Flags: nowait skipifnotsilent runasoriginaluser

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Remove "open at sign-in" (set from Faderhouse's settings) so no broken entry stays behind.
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Faderhouse');
end;

var
  CableInstalledAtStart: Boolean;

function CableAlreadyInstalled(): Boolean;
begin
  Result := CableInstalledAtStart;
end;

function InitializeSetup(): Boolean;
var
  Code: Integer;
begin
  // Is a VB-Audio virtual cable already there? (Then we don't install it again.)
  CableInstalledAtStart :=
    Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
         '-NoProfile -NonInteractive -Command "if (Get-PnpDevice -FriendlyName ''*VB-Audio*'' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"',
         '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
  Result := True;
end;

function NeedRestart(): Boolean;
begin
  // VB-Audio asks for a restart after installing the driver.
#if CableDir != ""
  Result := (not CableInstalledAtStart) and WizardIsTaskSelected('vbcable');
#else
  Result := False;
#endif
end;
