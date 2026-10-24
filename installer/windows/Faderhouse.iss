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
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Faderhouse"; Filename: "{app}\Faderhouse.exe"
Name: "{autodesktop}\Faderhouse"; Filename: "{app}\Faderhouse.exe"; Tasks: desktopicon

[Run]
; Normal install: the usual "Launch Faderhouse" tick box on the last page.
Filename: "{app}\Faderhouse.exe"; Description: "{cm:LaunchProgram,Faderhouse}"; Flags: nowait postinstall skipifsilent runasoriginaluser
; In-app update (silent): open Faderhouse again straight away.
Filename: "{app}\Faderhouse.exe"; Flags: nowait skipifnotsilent runasoriginaluser
