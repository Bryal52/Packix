; Packix for Windows — Inno Setup script. Built by .github/workflows/windows.yml:
;   ISCC /DVersion=1.0.0 /DSource=dist\Packix /DIcon=build\icons\packix.ico installer\packix.iss
; Installs for the current user (no administrator rights needed).

#ifndef Version
  #define Version "1.0.0"
#endif
#ifndef Source
  #define Source "..\dist\Packix"
#endif
#ifndef Icon
  #define Icon "..\build\icons\packix.ico"
#endif

[Setup]
AppId={{6E0C1C2B-5D0F-4C1E-9C4B-2F7A1E3D8B51}
AppName=Packix
AppVersion={#Version}
AppVerName=Packix {#Version}
AppPublisher=Bryal52
AppPublisherURL=https://github.com/Bryal52/Packix
AppSupportURL=https://github.com/Bryal52/Packix/issues
DefaultDirName={autopf}\Packix
DefaultGroupName=Packix
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Packix-{#Version}-Setup
SetupIconFile={#Icon}
UninstallDisplayIcon={app}\Packix.exe
UninstallDisplayName=Packix
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes
CloseApplications=force

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
russian.ArchiveVerb=Архивировать в Packix
english.ArchiveVerb=Archive with Packix
russian.OpenVerb=Открыть в Packix
english.OpenVerb=Open with Packix
russian.TaskDefault=Сделать Packix архиватором по умолчанию
english.TaskDefault=Make Packix the default archiver
russian.TaskLogin=Запускать при входе в систему
english.TaskLogin=Open at login
russian.TaskDesktop=Значок на рабочем столе
english.TaskDesktop=Desktop shortcut
russian.ArchiveType=Архив
english.ArchiveType=Archive
russian.AppDescription=Архиватор: zip, 7z, rar, tar и другие, архивы с паролем
english.AppDescription=Archiver: zip, 7z, rar, tar and more, password-protected archives

[Tasks]
Name: "default"; Description: "{cm:TaskDefault}"
Name: "login"; Description: "{cm:TaskLogin}"
Name: "desktop"; Description: "{cm:TaskDesktop}"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Packix"; Filename: "{app}\Packix.exe"
Name: "{autodesktop}\Packix"; Filename: "{app}\Packix.exe"; Tasks: desktop

[Registry]
; "Archive with Packix" in Explorer for files and folders; one Packix collects a multi-selection.
Root: HKA; Subkey: "Software\Classes\*\shell\Packix.Archive"; ValueType: string; ValueName: ""; ValueData: "{cm:ArchiveVerb}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\*\shell\Packix.Archive"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\Packix.exe"",0"
Root: HKA; Subkey: "Software\Classes\*\shell\Packix.Archive"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"
Root: HKA; Subkey: "Software\Classes\*\shell\Packix.Archive\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Packix.exe"" --archive ""%1"""
Root: HKA; Subkey: "Software\Classes\Directory\shell\Packix.Archive"; ValueType: string; ValueName: ""; ValueData: "{cm:ArchiveVerb}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Directory\shell\Packix.Archive"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\Packix.exe"",0"
Root: HKA; Subkey: "Software\Classes\Directory\shell\Packix.Archive"; ValueType: string; ValueName: "MultiSelectModel"; ValueData: "Player"
Root: HKA; Subkey: "Software\Classes\Directory\shell\Packix.Archive\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Packix.exe"" --archive ""%1"""

; How archives open in Packix.
Root: HKA; Subkey: "Software\Classes\Packix.Archive"; ValueType: string; ValueName: ""; ValueData: "{cm:ArchiveType}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Packix.Archive\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\Packix.exe"",0"
Root: HKA; Subkey: "Software\Classes\Packix.Archive\shell\open"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenVerb}"
Root: HKA; Subkey: "Software\Classes\Packix.Archive\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Packix.exe"" ""%1"""

; Listed in "Open with" and in Settings → Default apps.
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Packix"; ValueData: "Software\Packix\Capabilities"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Packix\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Packix"
Root: HKA; Subkey: "Software\Packix\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:AppDescription}"
Root: HKA; Subkey: "Software\Classes\.zip\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".zip"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.zip"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.7z\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".7z"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.7z"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.rar\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".rar"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.rar"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.tar\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tar"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.tar"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.gz\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".gz"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.gz"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.tgz\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tgz"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.tgz"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.bz2\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".bz2"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.bz2"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.tbz2\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tbz2"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.tbz2"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.xz\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xz"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.xz"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.txz\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".txz"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.txz"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.zst\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".zst"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.zst"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.tzst\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tzst"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.tzst"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue
Root: HKA; Subkey: "Software\Classes\.001\OpenWithProgids"; ValueType: string; ValueName: "Packix.Archive"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Packix\Capabilities\FileAssociations"; ValueType: string; ValueName: ".001"; ValueData: "Packix.Archive"
Root: HKA; Subkey: "Software\Classes\.001"; ValueType: string; ValueName: ""; ValueData: "Packix.Archive"; Tasks: default; Flags: uninsclearvalue

; Start at login, quietly.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Packix"; ValueData: """{app}\Packix.exe"" --background"; Tasks: login; Flags: uninsdeletevalue

[Run]
Filename: "{app}\Packix.exe"; Parameters: "--background"; Flags: nowait runasoriginaluser; Tasks: login
Filename: "ms-settings:defaultapps?registeredAppUser=Packix"; Flags: shellexec nowait postinstall skipifsilent unchecked; Description: "{cm:TaskDefault}"; Tasks: default

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM Packix.exe /F"; Flags: runhidden; RunOnceId: "StopPackix"
