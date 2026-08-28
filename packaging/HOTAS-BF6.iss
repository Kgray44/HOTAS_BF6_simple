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
#ifndef VJoyVersion
  #error Dependency metadata must be supplied by packaging/build-installer.ps1
#endif
#ifndef HidHideVersion
  #error Dependency metadata must be supplied by packaging/build-installer.ps1
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
AlwaysRestart=no
RestartIfNeededByRun=no
SetupIconFile=..\assets\icons\HOTAS-BF6-Setup.ico
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
Name: "{group}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"; IconFilename: "{app}\HOTAS BF6 Launcher.exe"
Name: "{autodesktop}\HOTAS BF6"; Filename: "{app}\HOTAS BF6 Launcher.exe"; IconFilename: "{app}\HOTAS BF6 Launcher.exe"; Tasks: desktopicon

; Shared drivers are deliberately retained. The only HidHide cleanup performed
; by normal uninstall is this application's own allowlist entry; no global
; lists or physical-device rules from other software are touched.
[UninstallRun]
Filename: "{code:HidHideCliPath}"; Parameters: "--app-unreg ""{app}\HOTAS BF6.exe"""; Flags: runhidden; Check: HidHideCliAvailable

[Code]
var
  DependencyPage: TWizardPage;
  VJoyCheck: TNewCheckBox;
  HidHideCheck: TNewCheckBox;
  VJoyStatus: TNewStaticText;
  HidHideStatus: TNewStaticText;
  VJoyWasDetected: Boolean;
  HidHideWasDetected: Boolean;
  VJoyResult: String;
  HidHideResult: String;
  RestartRequired: Boolean;

function FindUninstallProduct(const Needle: String; var Version: String): Boolean;
var
  Names: TArrayOfString;
  Index: Integer;
  DisplayName: String;
begin
  Result := False;
  Version := '';
  if RegGetSubkeyNames(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', Names) then begin
    for Index := 0 to GetArrayLength(Names) - 1 do begin
      if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[Index],
          'DisplayName', DisplayName) and (Pos(Lowercase(Needle), Lowercase(DisplayName)) > 0) then begin
        RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[Index],
          'DisplayVersion', Version);
        Result := True;
        Exit;
      end;
    end;
  end;
  if RegGetSubkeyNames(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', Names) then begin
    for Index := 0 to GetArrayLength(Names) - 1 do begin
      if RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[Index],
          'DisplayName', DisplayName) and (Pos(Lowercase(Needle), Lowercase(DisplayName)) > 0) then begin
        RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Names[Index],
          'DisplayVersion', Version);
        Result := True;
        Exit;
      end;
    end;
  end;
end;

function VJoyDetected(): Boolean;
var
  Version: String;
begin
  Result := RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\vjoy') or
    FindUninstallProduct('vJoy', Version);
end;

function HidHideDetected(): Boolean;
var
  Version: String;
begin
  Result := RegValueExists(HKCR, 'Installer\Dependencies\NSS.Drivers.HidHide.x64', 'Version') or
    (RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\HidHide') and
     FindUninstallProduct('HidHide', Version));
end;

function HidHideCliPath(Param: String): String;
begin
  Result := ExpandConstant('{pf}\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe');
  if not FileExists(Result) then
    Result := ExpandConstant('{pf32}\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe');
end;

function HidHideCliAvailable(): Boolean;
begin
  Result := FileExists(HidHideCliPath(''));
end;

procedure AllowHotasBf6ThroughHidHide();
var
  ResultCode: Integer;
  MapperPath: String;
begin
  if not HidHideDetected() or not HidHideCliAvailable() then Exit;
  MapperPath := ExpandConstant('{app}\HOTAS BF6.exe');
  if not FileExists(MapperPath) then begin
    Log('HOTAS BF6 HidHide allowlist was skipped because the installed mapper executable is missing.');
    Exit;
  end;
  if Exec(HidHideCliPath(''), '--app-reg "' + MapperPath + '"', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) then
    Log('HOTAS BF6 executable was allowlisted in HidHide.')
  else
    Log('HOTAS BF6 HidHide allowlist could not be applied during install (exit ' + IntToStr(ResultCode) + '). The app offers a safe retry.');
end;

procedure UpdateDependencyStatus();
begin
  if VJoyWasDetected then begin
    VJoyStatus.Caption := 'vJoy  ·  Detected already; it will not be changed.';
    VJoyCheck.Checked := False;
    VJoyCheck.Enabled := False;
  end else begin
    VJoyStatus.Caption := 'vJoy  ·  Not detected. Required for virtual controller output.';
    VJoyCheck.Checked := True;
    VJoyCheck.Enabled := True;
  end;
  if HidHideWasDetected then begin
    HidHideStatus.Caption := 'HidHide  ·  Detected already; it will not be changed.';
    HidHideCheck.Checked := False;
    HidHideCheck.Enabled := False;
  end else begin
    HidHideStatus.Caption := 'HidHide  ·  Not detected. Optional device-hiding component.';
    HidHideCheck.Checked := True;
    HidHideCheck.Enabled := True;
  end;
end;

procedure InitializeWizard();
begin
  VJoyWasDetected := VJoyDetected();
  HidHideWasDetected := HidHideDetected();
  DependencyPage := CreateCustomPage(wpSelectDir, 'HOTAS BF6 — Required Components',
    'Review optional driver setup before installation.');

  VJoyStatus := TNewStaticText.Create(DependencyPage);
  VJoyStatus.Parent := DependencyPage.Surface;
  VJoyStatus.Left := 0;
  VJoyStatus.Top := ScaleY(8);
  VJoyStatus.Width := DependencyPage.SurfaceWidth;
  VJoyStatus.Height := ScaleY(28);
  VJoyStatus.WordWrap := True;

  VJoyCheck := TNewCheckBox.Create(DependencyPage);
  VJoyCheck.Parent := DependencyPage.Surface;
  VJoyCheck.Left := ScaleX(16);
  VJoyCheck.Top := ScaleY(42);
  VJoyCheck.Width := DependencyPage.SurfaceWidth - ScaleX(16);
  VJoyCheck.Caption := 'Download and run the official vJoy {#VJoyVersion} installer';

  HidHideStatus := TNewStaticText.Create(DependencyPage);
  HidHideStatus.Parent := DependencyPage.Surface;
  HidHideStatus.Left := 0;
  HidHideStatus.Top := ScaleY(86);
  HidHideStatus.Width := DependencyPage.SurfaceWidth;
  HidHideStatus.Height := ScaleY(28);
  HidHideStatus.WordWrap := True;

  HidHideCheck := TNewCheckBox.Create(DependencyPage);
  HidHideCheck.Parent := DependencyPage.Surface;
  HidHideCheck.Left := ScaleX(16);
  HidHideCheck.Top := ScaleY(120);
  HidHideCheck.Width := DependencyPage.SurfaceWidth - ScaleX(16);
  HidHideCheck.Caption := 'Download and run the official HidHide {#HidHideVersion} installer';

  with TNewStaticText.Create(DependencyPage) do begin
    Parent := DependencyPage.Surface;
    Left := 0;
    Top := ScaleY(164);
    Width := DependencyPage.SurfaceWidth;
    Height := ScaleY(54);
    WordWrap := True;
    Caption := 'Selected components are downloaded from their pinned official releases, checked against SHA-256 and Authenticode before their normal vendor installer opens. Windows UAC and restart decisions remain under your control.';
  end;
  UpdateDependencyStatus();
end;

function VerifyAuthenticode(const FileName, ExpectedSubject: String): Boolean;
var
  ResultCode: Integer;
  Parameters: String;
begin
  Parameters := '-NoLogo -NoProfile -NonInteractive -Command "$s = Get-AuthenticodeSignature -LiteralPath ''' +
    FileName + '''; if (($s.Status -ne ''Valid'') -or ($s.SignerCertificate.Subject -notlike ''*' +
    ExpectedSubject + '*'') ) { exit 1 }"';
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Parameters, '',
    SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function DownloadVerifyAndRun(const Name, Url, FileName, Sha256, Signer: String): Boolean;
var
  Payload: String;
  ResultCode: Integer;
begin
  Result := False;
  Payload := AddBackslash(ExpandConstant('{tmp}')) + FileName;
  try
    DownloadTemporaryFile(Url, FileName, Sha256, nil);
  except
    Log(Name + ' download or SHA-256 validation failed: ' + GetExceptionMessage);
    DeleteFile(Payload);
    MsgBox(Name + ' was not installed because its verified download failed. HOTAS BF6 setup can continue.',
      mbError, MB_OK);
    Exit;
  end;
  if CompareText(GetSHA256OfFile(Payload), Sha256) <> 0 then begin
    Log(Name + ' SHA-256 mismatch after download.');
    DeleteFile(Payload);
    MsgBox(Name + ' was not installed because its SHA-256 verification failed. HOTAS BF6 setup can continue.',
      mbError, MB_OK);
    Exit;
  end;
  if not VerifyAuthenticode(Payload, Signer) then begin
    Log(Name + ' Authenticode validation failed.');
    DeleteFile(Payload);
    MsgBox(Name + ' was not installed because its trusted Authenticode signature could not be verified. HOTAS BF6 setup can continue.',
      mbError, MB_OK);
    Exit;
  end;
  if not Exec(Payload, '', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then begin
    Log(Name + ' installer could not be launched: ' + SysErrorMessage(ResultCode));
    MsgBox(Name + ' installer could not be started. HOTAS BF6 setup can continue.', mbError, MB_OK);
    Exit;
  end;
  if (ResultCode <> 0) and (ResultCode <> 3010) then begin
    Log(Name + ' installer exited with code ' + IntToStr(ResultCode));
    MsgBox(Name + ' did not report successful installation. HOTAS BF6 setup can continue.', mbError, MB_OK);
    Exit;
  end;
  if ResultCode = 3010 then RestartRequired := True;
  Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID <> DependencyPage.ID then Exit;
  if WizardSilent then begin
    Log('Silent setup skips vJoy/HidHide bootstrap because explicit driver consent is unavailable.');
    Exit;
  end;
  if VJoyCheck.Checked and not VJoyWasDetected then begin
    if DownloadVerifyAndRun('vJoy', '{#VJoyUrl}', '{#VJoyFileName}', '{#VJoySha256}', '{#VJoySigner}') then
      VJoyResult := 'Installed during setup'
    else
      VJoyResult := 'Not installed';
  end else if VJoyWasDetected then
    VJoyResult := 'Already present'
  else
    VJoyResult := 'Skipped by user';
  if HidHideCheck.Checked and not HidHideWasDetected then begin
    if DownloadVerifyAndRun('HidHide', '{#HidHideUrl}', '{#HidHideFileName}', '{#HidHideSha256}', '{#HidHideSigner}') then
      HidHideResult := 'Installed during setup'
    else
      HidHideResult := 'Not installed';
  end else if HidHideWasDetected then
    HidHideResult := 'Already present'
  else
    HidHideResult := 'Skipped by user';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    AllowHotasBf6ThroughHidHide();
    if RestartRequired then
      Log('HOTAS BF6 installed. vJoy: ' + VJoyResult + '; HidHide: ' + HidHideResult +
        '; Restart required: yes')
    else
      Log('HOTAS BF6 installed. vJoy: ' + VJoyResult + '; HidHide: ' + HidHideResult +
        '; Restart required: no');
    if not WizardSilent then begin
      if RestartRequired then
        MsgBox('HOTAS BF6: Installed' + #13#10 + 'vJoy: ' + VJoyResult + #13#10 +
          'HidHide: ' + HidHideResult + #13#10 + 'Restart: Required (not performed automatically).',
          mbInformation, MB_OK)
      else
        MsgBox('HOTAS BF6: Installed' + #13#10 + 'vJoy: ' + VJoyResult + #13#10 +
          'HidHide: ' + HidHideResult + #13#10 +
          'Restart: Not reported by the vendor installer.', mbInformation, MB_OK);
    end;
  end;
end;
