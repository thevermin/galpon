; Instalador de Galpon para Windows (Inno Setup 6)
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6E3C5B7A-2F4D-4B8E-9A61-5D0C3E7F1A24}
AppName=Galpon
AppVersion={#AppVersion}
AppPublisher=Galpon
DefaultDirName={autopf}\Galpon
DefaultGroupName=Galpon
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=Galpon-{#AppVersion}-Windows-Instalador
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=galpon.ico
UninstallDisplayIcon={app}\Galpon.exe
LicenseFile=..\LICENSE
ChangesAssociations=yes

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"

[Files]
Source: "..\build\Galpon_artefacts\Release\Galpon.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Registry]
Root: HKA; Subkey: "Software\Classes\.galpon"; ValueType: string; ValueName: ""; ValueData: "Galpon.Song"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Galpon.Song"; ValueType: string; ValueName: ""; ValueData: "Canción de Galpon"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Galpon.Song\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Galpon.exe,0"
Root: HKA; Subkey: "Software\Classes\Galpon.Song\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Galpon.exe"" ""%1"""

[Icons]
Name: "{autoprograms}\Galpon"; Filename: "{app}\Galpon.exe"
Name: "{autodesktop}\Galpon"; Filename: "{app}\Galpon.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Galpon.exe"; Description: "Abrir Galpon"; Flags: nowait postinstall skipifsilent
