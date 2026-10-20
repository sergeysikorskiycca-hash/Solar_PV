# ESP32 Admin rewrite — port status and migration notes

Base reviewed: `ESP8266_Admin/ESP8266_Admin.ino` on `main` (766 lines; approximately 34.5 KB of source).

The first ESP32 port now exists in `ESP32_Admin.ino`; configurable TFT GPIO defaults are isolated in `DisplayConfig.h`. The existing ESP8266 sketch on `main` is intentionally left unchanged. The port is an initial platform adaptation, not yet a compile-verified release.

## Existing behavior to preserve

- Setup access point and router connection can coexist.
- Web admin page and JSON API:
  - `GET /`
  - `GET /api/config`
  - `GET /api/scan`
  - `POST /api/save`
  - `GET/POST /api/page`
  - `POST /api/wifi`
- Up to eight saved Wi-Fi networks; blank password reuses a saved password.
- Configuration stored in LittleFS, including display layout settings.
- ST7789 240x320 display, Cyrillic fonts, two pages, and partial footer updates.
- Device boots to TFT page 1.
- Existing HTML preview and plain-text Wi-Fi password entry.

## Findings from the code review

### Review findings and implementation status

1. **Serial output in the main loop — addressed in the port:** the loop no longer prints the router IP repeatedly. A connection IP is still logged once during boot connection.
2. **Unused state — addressed in the port:** removed `savedNetworkCount`, which was written but never read.
3. **Filesystem recovery can erase settings — addressed in the port:** LittleFS is mounted with automatic formatting disabled (`LittleFS.begin(false)`). A mount failure is reported over Serial instead of formatting storage.
4. **Platform-specific Wi-Fi constants — adapted, still needs compile verification:** the scan security check now uses `WIFI_AUTH_OPEN`.
5. **Configuration/API responsibilities are mixed:** JSON generation, Wi-Fi management, filesystem persistence, display drawing, request handlers, and a large embedded HTML document all live in one sketch. Separate these concerns to make the port easier to verify.

### Review notes

- Most named functions in the current sketch are referenced; do not delete a function merely because it looks small or has few call sites.
- The HTML/JS/CSS and device-side preview describe demo values. They must remain explicitly labelled as demo until real telemetry is integrated.
- Preserve the existing `/solarpv.cfg` key/value format where practical, so the new firmware can migrate settings without silently discarding them. ESP8266 and ESP32 have separate physical flash storage, so transfer of existing settings may require entering them again or a deliberate export/import path.
- Keep the current ESP8266 source untouched as a known-working fallback.

## Planned modular structure

```
ESP32_Admin/
  ESP32_Admin.ino       # startup, main loop, orchestration
  AppConfig.h           # configuration data model and shared declarations
  ConfigStore.h/.cpp    # LittleFS read/write and validation
  WifiManager.h/.cpp    # AP/STA, scanning, saved networks, connection status
  Display.h/.cpp        # ST7789, Cyrillic rendering, page drawing
  WebAdmin.h/.cpp       # HTTP routes and API handlers
  WebPage.h             # embedded HTML/CSS/JS
```

Use a module only where it reduces coupling; avoid creating extra abstractions without a clear benefit.

## Migration sequence

1. Audit and agree on behavior and module boundaries.
2. Create a separate ESP32 sketch and adapt platform APIs.
3. Keep the display pin mapping in one configuration header; confirm the actual ESP32 wiring before the first display test.
4. Compile and fix all errors before uploading.
5. Test AP access, router access, scanning, save/reboot, saved-network reuse, both TFT pages, Cyrillic text, and no-flicker footer updates.
6. Record compile memory statistics and compare them with the old ESP8266 build, noting that the old numbers are not measurements of the current sketch.

## Not yet verified

- No ESP32 compile or hardware test has been performed for this rewrite yet.
- Exact TFT GPIO wiring on the ESP32 must be confirmed before enabling the display driver.
- Exact Flash size and partition selection should be read from the board/core configuration or measured, not guessed from the product listing.
- The current sketch retains the existing `/solarpv.cfg` key/value format, but settings stored in the ESP8266 flash do not automatically appear in the separate ESP32 board.
