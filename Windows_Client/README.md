# Solar_PV Desktop — Windows 10/11 demo

A first read-only desktop client prototype for Windows 10 and Windows 11 (x64).

## What is included
- Russian-language dashboard for two PV strings.
- Local demo values, available without a controller.
- Read-only connection to the ESP32 endpoint `GET /api/config`.
- Automatic refresh every 3 seconds after a successful connection.
- No settings writes, counter resets, Wi-Fi changes, or other control commands.

## Build
Requires .NET 8 SDK on Windows:

```powershell
dotnet publish SolarPV.Client.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o publish
```

The Inno Setup script is in `installer/Solar_PV.iss`. The GitHub Actions workflow builds the installer and publishes it as the asset `Solar_PV_Windows_Demo_Setup.exe` on the rolling demo release.

## Connect
Connect the computer to the same network as the ESP32, enter its address (for example `http://192.168.4.1`) and click **Подключить ESP32**. The client reads `/api/config` only. If the controller is unavailable, click **Демо** to return to sample values.

This is an early demo, not a production release. Controller API compatibility and behavior must be verified on the actual station.
