# Solar_PV Desktop — Windows 10/11

Windows x64 desktop client for the Solar_PV station.

## Features
- Russian-language dashboard for two PV strings.
- Local demo values, available without a controller.
- Read-only connection to the ESP32 endpoint `GET /api/config`.
- Automatic telemetry refresh every 3 seconds after a successful connection.
- In-app update check against the official Solar_PV GitHub release.
- Installer download with SHA-256 verification, followed by unattended installation and application restart.
- No settings writes, counter resets, Wi-Fi changes, or other controller commands.

## Build
Requires .NET 8 SDK on Windows:

```powershell
dotnet publish SolarPV.Client.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o publish
```

The Inno Setup script is in `installer/Solar_PV.iss`. GitHub Actions builds the installer and publishes it as `Solar_PV_Windows_Demo_Setup.exe` on the rolling `solar-pv-windows-demo` release. The release notes contain the client version used by the in-app updater.

## Updating
Click **Проверить обновления**. If a newer version is available, confirm the installation. The client downloads the official installer, verifies its SHA-256 digest supplied by GitHub, closes itself, runs the installer, and starts again. Windows may display the normal User Account Control prompt because the installer updates the application under Program Files.

If the download or integrity check fails, the installer is not run and the current installation is left in place.

## Connect
Connect the computer to the same network as the ESP32, enter its address (for example `http://192.168.4.1`) and click **Подключить ESP32**. The client reads `/api/config` only. If the controller is unavailable, click **Демо** to return to sample values.

Controller API compatibility and behavior must be verified on the actual station.
