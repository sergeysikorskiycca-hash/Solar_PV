# Solar_PV Desktop — Windows 10/11

Windows desktop client for the Solar_PV station. The release provides both x86 and x64 builds.

## Features
- Russian-language dashboard for two PV strings.
- Four switchable desktop designs: Digital Instrument, Analog Station, Energy Flow, and Technical Blueprint.
- Selected design is saved locally and restored on the next launch.
- Local demo values, available without a controller.
- Read-only connection to the ESP32 endpoint `GET /api/config`.
- Automatic telemetry refresh every 3 seconds after a successful connection.
- In-app update check against the official Solar_PV GitHub release.
- Self-contained single-file Windows application for x86 and x64; .NET Runtime is not required on the target computer.
- Controller address is stored in the `Data` subfolder beside the EXE, so the complete application folder can be copied to another computer.
- Updates are downloaded as an architecture-matched ZIP package, SHA-256 verified, and applied after the app closes. The installer is not run during updates.
- No settings writes, counter resets, Wi-Fi changes, or other controller commands.

## Installer and portable use
The standalone `Solar_PV_Windows_Demo_Setup.exe` is the normal first-install option. One installer supports both 32-bit and 64-bit Windows and automatically installs the matching application build. It asks for the destination folder and can create Start Menu and Desktop shortcuts.

After installation, the complete `Solar_PV` folder can be copied to another compatible Windows 10/11 computer and `Solar_PV.exe` can be started directly. No second installation or .NET Runtime is required. Keep the `Data` subfolder with the EXE to retain the saved controller address. The app folder must be writable by the current Windows user; the installer grants that permission for its installation folder.

The installer is published as a standalone EXE. It is not wrapped inside a ZIP archive.

## Build
Requires .NET 8 SDK on Windows:

```powershell
dotnet publish SolarPV.Client.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o publish-x64
dotnet publish SolarPV.Client.csproj -c Release -r win-x86 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o publish-x86
```

The Inno Setup script is in `installer/Solar_PV.iss`. GitHub Actions builds both architectures, combines them into one automatic x86/x64 installer, and creates separate portable update packages. The running client selects the matching update package automatically.

## Updating
Click **Проверить обновления**. If a newer version is available, confirm the update. The client selects the portable ZIP package matching its own architecture, verifies the SHA-256 digest supplied by GitHub, then closes. A small helper waits for the process to exit, replaces the application files in the current folder, preserves the `Data` folder, and starts Solar_PV again. The Setup installer is not launched and the user does not need to repeat installation or answer UAC for an ordinary update.

If the download or integrity check fails, the current application files are left in place.

## Connect
Connect the computer to the same network as the ESP32, enter its address (for example `http://192.168.4.1`) and click **Подключить ESP32**. The client reads `/api/config` only. If the controller is unavailable, click **Демо** to return to sample values.

Controller API compatibility and behavior must be verified on the actual station.
