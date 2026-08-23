; Built only by packaging/build-installer.ps1, which reads the repository's
; authoritative HOTAS_VERSION file and supplies the preprocessor values below.
#ifndef MyAppVersion
  #error MyAppVersion must be supplied by packaging/build-installer.ps1
#endif
#ifndef SourceDir
  #error SourceDir must be supplied by packaging/build-installer.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by packaging/build-installer.ps1
#endif

[Setup]
AppId={{EF20B1AF-2A35-4B92-A883-8307F2E47146}
AppName=HOTAS BF6
AppVersion={#MyAppVersion}
AppPublisher=Kgray44
DefaultDirName={localappdata}\Programs\HOTAS BF6
DefaultGroupName=HOTAS BF6
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=HOTAS-BF6-Setup-v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
UninstallDisplayName=HOTAS BF6
UninstallDisplayIcon={app}\HOTAS BF6 Launcher.exe
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany=Kgray44
VersionInfoDescription=HOTAS BF6 Setup

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; Settings are stored under the established QSettings identity in AppData, not
; in {app}; replacing program files therefore preserves profiles and curves.
[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{group}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"
Name: "{autodesktop}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"; Tasks: desktopicon
