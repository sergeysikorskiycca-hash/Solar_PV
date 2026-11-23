#ifndef MyAppVersion
  #define MyAppVersion "0.3.9"
#endif
#define MyAppName "Solar_PV"
#define MyPublisher "Solar_PV"

[Setup]
AppId={{B3D61F30-8A20-4D31-A2B2-77C8B56A8A01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyPublisher}
DefaultDirName={autopf}\Solar_PV
DefaultGroupName=Solar_PV
OutputDir=..\..\dist
OutputBaseFilename=Solar_PV_Windows_Demo_Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\Solar_PV.exe
CloseApplications=yes
RestartApplications=no

[Dirs]
Name: "{app}"; Permissions: users-modify

[Files]
Source: "..\publish\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Solar_PV"; Filename: "{app}\Solar_PV.exe"
Name: "{autodesktop}\Solar_PV"; Filename: "{app}\Solar_PV.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Дополнительные задачи:"
