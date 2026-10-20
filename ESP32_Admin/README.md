# ESP32 Admin rewrite — audit and migration plan

Base reviewed: `ESP8266_Admin/ESP8266_Admin.ino` on `main` (766 lines; approximately 34.5 KB of source).

This document records the first code review before porting. The existing ESP8266 sketch is intentionally left unchanged.

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

### Fix in the new implementation

1. **Serial output in the main loop:** `stopAccessPointIfConnected()` is called on every pass through `loop()`, but currently only prints the router IP. Once connected, this can flood Serial output. The function name also implies it may stop the AP, while its actual behavior is to keep the AP running and log the IP. Replace it with explicit, non-repeating connection/status handling.
2. **Unused state:** `savedNetworkCount` is updated but never read. Remove it unless the new design has a real use for it.
3. **Filesystem recovery can erase settings:** the current sketch automatically formats LittleFS after a mount failure. The ESP32 rewrite should not automatically format storage on a mount error. Report the failure and preserve user data; formatting should be an explicit recovery choice.
4. **Platform-specific Wi-Fi constants:** scan security checks use ESP8266-specific `ENC_TYPE_NONE`. The ESP32 implementation must use the ESP32 core's authentication/security values and verify them against the installed core version.
5. **Configuration/API responsibilities are mixed:** JSON generation, Wi-Fi management, filesystem persistence, display drawing, request handlers, and a large embedded HTML document all live in one sketch. Separate these concerns to make the port easier to verify.

### Review notes

- Most named functions in the current sketch are referenced; do not delete a function merely because it looks small or has few call sites.
- The HTML/JS/CSS and device-side preview describe demo values. They must remain explicitly labelled as demo until real telemetry is integrated.
- Preserve the existing `/solarpv.cfg` key/value format where practical, so the new firmware can migrate settings without silently discarding them. ESP8266 and ESP32 have separate physical flash storage, so transfer of existing settings may require entering them again or a deliberate export/import path.
- Keep the current ESP8266 source untouched as a known-working fallback.

## Proposed ESP32 project structure

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
