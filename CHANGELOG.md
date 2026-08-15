# M5Stack Nightscout monitor project Changelog

## Revisions


### 15 August 2026 (Waveshare ESP32-Touch-LCD-3.5 port)

* Second non-M5Stack target: the Waveshare ESP32-Touch-LCD-3.5 (classic ESP32-D0WDR2, 16 MB flash, 2 MB PSRAM, 3.5" 320x480 ST7796S IPS on SPI, FT6336 touch, TCA9554 IO expander, AXP2101 PMIC, ES8311 codec, microSD). Built with `Scripts\build.ps1 -Target WS_TouchLCD35` into `Binaries\WS_TouchLCD35\`; excluded from release (`-Target All`) builds until hardware-validated. Web flasher card + manifest and OTA path (`WS_TouchLCD35`) added; no new library is needed - the panel, backlight and touch are driven by M5GFX's own LovyanGFX classes (`hal_ws_touchlcd35.h/.cpp`, selected by `-DDEVICE_WS_TOUCH_LCD_35` in `M5NSDevice.h`).
* Same shim strategy as the JC3248W535: the unchanged 320x240 UI draws into an offscreen sprite, a background task composes it exactly 1.5x into a 480x320 landscape frame and pushes it whole (the ST7796 rotates in hardware, so no rotation trick is needed). Touch thirds at the bottom become BtnA/B/C. Because the panel shares its SPI bus with the microSD, the frame push is serialised on the Arduino SPI transaction mutex that the SD driver also uses.
* Unlike the JC, this board has a PMIC: battery level/voltage come from the AXP2101 fuel gauge (the battery icon shows) and long-press power-off is a real AXP2101 shutdown - the PWR button turns the board back on. Alarms are synthesized I2S tones through the ES8311 (32-bit slots, MCLK derived from BCLK as in the vendor examples). Board bring-up knobs are documented in the shim: `WS_LAND_ROT` (landscape direction), panel `invert`/`rgb_order`, TCA9554 EXIO mapping.

### 28 July 2026 (Guition JC3248W535 port - branch JC3248W535)

* First non-M5Stack target: the Guition JC3248W535 (ESP32-S3, 3.5" 320x480 IPS, AXS15231B panel+touch on QSPI/I2C, microSD, NS4168 I2S speaker, no physical buttons). Built with `Scripts\build.ps1 -Target JC3248W535` into `Binaries\JC3248W535\`; intentionally excluded from release (`-Target All`) builds until hardware-validated. Requires the "GFX Library for Arduino" (Arduino_GFX) **1.6.0** (1.6.1 is reported broken with this panel).
* The port is a device shim, not a UI rewrite: `M5NSDevice.h` selects between M5Unified (all M5 boards, unchanged) and `hal_jc3248w535.h/.cpp`, which provides the same `M5` object surface. The unchanged 320x240 UI draws into an offscreen M5GFX sprite; a background task composes it rotated + exactly 1.5x into a full 320x480 frame and pushes it whole - full-frame-only is mandatory on this panel anyway (no hardware rotation, and some silicon batches ignore window-address commands, breaking partial updates). Arduino_GFX drives only panel init + frame push.
* Touch from the AXS15231B is mapped back into UI coordinates; the bottom-of-screen thirds act as BtnA/B/C exactly like M5Unified's touch zones on Core2/CoreS3. Alarms play as synthesized I2S tones on the NS4168. No fuel gauge: the battery icon stays hidden; long-press power-off becomes deep sleep (reset button restarts). SD runs on its own SPI3/HSPI bus (the display owns SPI2), so `M5NS.INI`, boot picture and logfile work as usual; without a card the existing NVS fallback applies.
* Fixed a pre-existing OTA footgun while at it: firmware variant selection treated *every* ESP32-S3 build as CoreS3 - a JC3248W535 device would have OTA-flashed the CoreS3 image (wrong panel/pins). The JC variant now resolves first.

### 23 July 2026 (LibreLinkUp graph fetch fix)

* Fixed the LibreLinkUp data source failing nearly every poll on real hardware with alternating "No current reading" / "JSON parse failed" errors. The graph response was parsed straight off `http.getStream()`, but the ESP32 `HTTPClient` does not decode `Transfer-Encoding: chunked` on the raw stream (only `getString()` does), so the interleaved chunk-size lines corrupted the JSON. The graph request is now made as HTTP/1.0, which forbids chunked responses (the standard ArduinoJson recommendation for streamed parsing). Found by comparing against xDrip's web-follower transport, which decodes chunking transparently via OkHttp.
* Fixed a login-flow ordering bug: any nonzero login `status` was treated as bad credentials (with permanent backoff) before the terms-of-use step was examined, making the ToU auto-accept unreachable - LibreLinkUp reports a pending ToU as `status` 4. The redirect and ToU steps are now handled before the status check.
* JSON parse failures from LibreLinkUp now log the ArduinoJson error reason (`NoMemory`, `InvalidInput`, ...) to serial for easier field diagnosis, and requests carry `cache-control: no-cache` matching other LibreLinkUp clients.

### 18 July 2026 (LibreLinkUp data source)

* Added LibreLinkUp ("LibreView") follower accounts as the third glucose data source (`data_source = 2`), filling in the slot the Dexcom Share integration reserved. New config: `libre_user`, `libre_pass`, `libre_server` (region index, default 5 = EU), settable via `M5NS.INI`, NVS flash, or the web config UI's "Data source" section (region dropdown covers all 12 LibreLinkUp regions). Fetch logic lives in the new `M5NSLibre.cpp`/`.h`: logs in via `/llu/auth/login`, follows a region "redirect" response automatically (persisting the corrected region), resolves the first followed patient via `/llu/connections`, then reads `/llu/connections/{id}/graph` for the current reading plus recent history. The graph response is parsed with an ArduinoJson streaming filter (only the fields actually used) into a local document, since the unfiltered payload is too large for the shared 16 KB `JSONdoc`. Reuses the existing `directionToArrowAngle()` trend mapping and the Dexcom-style bad-credential backoff.

### 16 July 2026 (unique per-device name)

* The device name (`deviceName`, previously always the fixed `M5NS`) now defaults to a per-device `M5NS-XXXX`, where `XXXX` is derived from the last two bytes of the device's ESP32 eFuse MAC address. This makes the mDNS name (`m5ns-xxxx.local`), SoftAP SSID, Wi-Fi join QR code, and DHCP hostname unique, so `<name>.local` no longer ambiguously resolves to whichever of several devices on the same network answers first. A `device_name` that is empty, or still the legacy fixed `M5NS`/`m5ns`, is treated as unset and replaced by the derived default; a name you've customized in the config UI or `M5NS.INI` is left untouched. The Android M5StackLoader app derives the identical name independently from the same eFuse bytes and writes it into the device's NVS during flashing, so even a device with older firmware picks up its unique name once reflashed with a current app build.
* Also sets the DHCP client hostname (`WiFi.setHostname`) to match, so the device shows up under its unique name in the router's client list too - previously the DHCP hostname was left at the ESP32 core's generic default.

### 17 July 2026 (Dexcom Share data source)

* Added Dexcom Share ("Follow") as an alternative glucose data source, selectable via the new `data_source` setting (0 = Nightscout, 1 = Dexcom Share). Only one source is active at a time. New config: `dexcom_user`, `dexcom_pass`, `dexcom_server` (US/outside US/Japan), settable via `M5NS.INI`, NVS flash, or the web config UI (new "Data source" section on the status page). Fetch logic lives in the new `M5NSDexcom.cpp`/`.h`, dispatched from a new `readDataSource()` alongside the existing `readNightscout()`. TLS uses `WiFiClientSecure::setInsecure()`, matching the existing OTA/non-Heroku pattern. Architecture reserves `data_source = 2` for a future LibreLinkUp (LibreView) source.

### 12 July 2026 (web config page rework, on-device OTA button)

* Reworked the web config page: settings are now grouped into collapsible sections instead of one long flat list, Yes/No settings are tap-to-toggle switches, two-value settings are side-by-side buttons, and only the section you just changed stays expanded (previously every change collapsed the whole page back to the top).
* The firmware version/update check no longer runs on every page load (it used to block the whole page on a GitHub fetch); it's now a separate "Check for update" link.
* Disabling the internal web server now shows a confirmation first, since doing so from the page removes the page itself. The change also can't actually be applied that way any more (the page becomes unreachable before the Save button can be clicked, so a restart reverts it) - the confirmation explains this and points to setting `disable_web_server = 1` in `M5NS.INI` instead. Separately, bootstrap/setup mode now always serves the config page regardless of this setting, fixing setup mode being silently unreachable for anyone who did set it in the INI.
* The Error Log and web config QR pages no longer show the `L: ... B: ...` loop/basal status row - it isn't relevant on either page.
* The web config QR page now checks GitHub for a newer firmware release when opened and shows **UPDATE** on the bottom row if one exists; the middle button (snooze everywhere else) installs it directly from the device when on this page, no browser required.
* WiFi networks entered during SoftAP setup are now saved to the SD card and flash immediately on Apply, instead of waiting for a separate trip to "Save configuration to M5NS.INI". Losing an entered network previously meant restarting into an endless re-bootstrap loop (a fresh random SoftAP password every reboot, with no way back to what was just typed) - now the credentials are safely on disk before the confirmation page is even sent, and the page tells you plainly that a restart is what's needed next. The WiFi network scan on the edit page also no longer runs on every settings edit, only when actually editing WiFi networks - it forces a temporary AP+STA mode switch that can knock the phone off the SoftAP mid-scan.

### 12 July 2026 (QR code pairing + web config page)

* Added a QR code to the SoftAP setup screen so a phone can join the device's Wi-Fi with one tap instead of typing the generated passphrase (`WIFI:S:...;T:WPA;P:...;;` format, recognized natively by iOS/Android camera apps).
* Added a new display page (page 4, cycled with the right button) showing a QR code encoding the device's local IP address, so the web config page can be opened from a phone camera without typing an address or relying on `<name>.local` (which is ambiguous with multiple devices on the same network). Falls back to a status message if Wi-Fi is down or the web server is disabled.
* Also hardened `Scripts/build.ps1`: each build target now uses its own arduino-cli build-cache folder (previously all three shared one folder derived from the sketch path, so concurrent builds of different targets corrupted each other's object files) and added a `-Clean` switch to force a fresh build. A full `-Target All` build is now treated as a release and auto-bumps the `YYYYMMDDnn` firmware version (same-day sequence, or a new day starts at `01`) before compiling, so publishing an OTA update no longer requires a manual version edit.

### 11 July 2026 (remove Sugarmate integration)

* Removed the obsolete Sugarmate follower workaround (`is_Sugarmate` detection/parsing path, used historically for Dexcom via `sugarmate.io` JSON URLs). Only the native Nightscout API path remains; behavior for Nightscout users is unchanged.

### 11 July 2026 (font size fix)

* Fixed all on-screen text rendering tiny after the M5Unified migration. Every `drawString`/`drawNumber` call passed `GFXFF` (=1) as the trailing font-number argument. Under the old M5Stack/TFT_eSPI library this meant "use the free font set by `setFreeFont`", but M5GFX interprets the integer as a built-in font index, overriding the free font with its small `Font1`. Dropped the `, GFXFF` argument from all 59 calls so the current free font (and its `setTextSize` scaling) is honored again.
* Bumped the displayed firmware version (Info page) to `2026071101`.
* Fixed the bottom info row (`L: ... B: ...`, `Snooze`, `LOOP`/`ERR`, sensor info) overlapping the button area. M5GFX positions free-font text lower than the old TFT_eSPI (it reserves the full-font ascent+descender), so `TL_DATUM` at y=220 pushed the baseline past the 240 px screen edge. The row is now bottom-anchored (`BL_DATUM`, y=240) so it stays on screen.
* Fixed the large BG value on the big-number screen (page 2) sitting too high. `MC_DATUM` centres M5GFX's full font cell (ascent + descender), but the value is digits only, so the glyphs floated up. The draw anchor is now shifted down by descender×textSize/2 (FSSB24 → y=144, FSSB18 → y=136) to visually centre the digits again.

### 10 July 2026 (build scripts)

* Added `Scripts/build.ps1` + `Scripts/build.bat` to compile the **minimum firmware set** for the whole lineup into `Binaries/`. Three groups, split by chip + flash: **Basic_4MB** (old 4 MB Basic, `min_spiffs`), **ESP32_16MB** (Basic 16 MB, Fire and all Core2 — `default` 16 MB layout, PSRAM off), and **CoreS3** (ESP32-S3). The 16 MB boards now get 6.25 MiB OTA slots, so the app can grow and still update over-the-air without a repartition/USB reflash. See `Scripts/README.md`.

### 10 July 2026

* Migrated to the **M5Unified** library. One board-agnostic firmware now builds for the whole current 16 MB lineup: **M5Stack Basic v2.7, Fire v2.7, Core2 v1.1 and CoreS3 (ESP32-S3)**. All the `#ifdef ARDUINO_M5STACK_Core2` conditionals are gone; the board is auto-detected at runtime.
* Sound now goes through `M5.Speaker` (DAC on Basic/Fire, I2S on Core2/CoreS3) instead of the old hand-rolled DAC/I2S code. Brightness, buttons/touch and battery all use the unified `M5.Display` / `M5.BtnA-C` / `M5.Power` APIs.
* Vibration motor, RGB LED strip and Micro Dot pHAT are now governed by `M5NS.INI` on every board (no longer force-disabled on Core2).
* Build note: the classic Basic (`m5stack-core-esp32`) needs the `Minimal SPIFFS` partition scheme; the other boards fit the default partition.

### 2 October 2022

* Updates for Railway (longer token, accept "trend" in addition to "direction", response count limited to 10).  

### 28 May 2021

* Previous delta (plugins) value display corrected. Nightscount read postponed from 300 to 305 seconds after the last update to allow plugins (delta, iob, cob, ...) to update.  
* Corrected error when "/" character was at the end of Nightscout URL.  
* Redirection for HTTP codes 301 and 302.  
* HTTPClient and WiFiClientSecure returned back to readNightscout() function.  

### 21 April 2021

* setReuse (keep-alive) returned back (removed) as it was causing crashes in HTTPClient library for Sugarmate.

### 20 April 2021

* Stability and server load improvements.  
* Binary built with older versions of M5Stack board 1.0.6 and M5Stack library 0.3.0 as newer versions did not recover after HTTP error.  
* Reusing of http connection disabled (keep-alive).  
* HTTPS GET uses root certificate “DigiCert High Assurance EV Root CA” for herokuapp.com site. It behaves „the old way“ for other servers.  
* Reduce Nightscout connections (NS is queried for update only once every minute and only if time from the last reading is bigger than 5 minutes).  
* Free memory and Up time added to the info page for diagnostic purposes.  
* Date format update (added 0 to days bellow 10).  

### 12 March 2021

* SoftAP mode added for configuration possibility on a new WiFi (hold button A and then click reset on M5Stack models with mechanical buttons, touch button A while you see CONFIG progress during boot on M5Stack Core2). 
* No SD card needed. When you save configuration from the web interface, it is saved to both - SD card and internal flash memory. You can then remove the SD card and reboot. To archive your settings to SD card, insert SD card and save configuration from web interface.  
* If you start blank M5Stack just programmed with M5 Nightscout Monitor firmware, it will enter the SoftAP mode directly for easy web configuration.  
* SSID name length extended to 63 characters.  

### 31 January 2021

* Added SHT30 compatibility, so now temperature and humidity works with newer version of BTC TICKER stand and ENV.II Unit.  

### 6 December 2020

* Mini graph corrected for Sugarmate users.  
* Alarm sound to loop error is now possible.  
* Support for 12-hour AM/PM time format.  
* Vibration motor unit support for hearing impaired.  
* Flexible RGB LED strip or M5Stack Fire internal RGB LEDs support.  
* Micro Dot pHAT support.  
* Core2 online update possibility (on next update, now you have to use M5Burner or build from sources).  

### 24 July 2020

* Delta displayed in correct M5NS.INI units regardles from Nightscout settings. (romkuru)  
* Corrected "bellow->below" typo in web interface. (guydavies)  
* Replacing Unicode TAB character "/u000b" defined by Medtronic by space to correctly parse JSON without enabling Unicode.  
* Added SHT30 compatibility, so now temperature and humidity works with newer version of BTC TICKER stand and ENV.II Unit.  

### 12 May 2020

* Delta displayed in correct M5NS.INI units regardles from Nightscout settings. (romkuru)  
* Corrected "bellow->below" typo in web interface. (guydavies)  

### 12 April 2020

* WiFi passwords are now hidden on all web pages and forms.  
* "snooze" part completely reworked.  
* Possibility to multiply alarm/warning sound snooze time by multiple presses of middle button. The button has to be pressed in less than 2s after the last press. The steps are 1x-2x-3x-4x-OFF and again.  
* Snooze (middle button) from one device works on all devices on the same network subnet with the same user (Nightscout URL). UDP broadcast is distrubuted in local network subnet.  

### 13 February 2020

* Improved time handling when Epoch in milliseconds has decimal places (CGMBLEKit).
* Added version number to the latest (log) page.  
* Some minor diagnostics monitor/log updates.  

### 09 February 2020

* Support for OpenAPS loop (set info_line = 3 in M5NS.INI).  
* Corrected error in token implementation for IOB, COB, Loop, OpenAPS, basal.  
* Corrected an error in shifting SGV history graph buffer.  
* Commented out some unused variables  
* Renamed WiFiMulti to WiFiMultiple. (Dominik Dzienia)  
* Some externals moved to extern.h  (Dominik Dzienia)  
* Sources are now possible to compile in VSCode & Platform.IO, which is about 4 times faster, has better syntax highlighting, ... Just rename .ino to .cpp (Dominik Dzienia)  
* Cleanup of README.md, changelog refactored to CHANGELOG.md (Dominik Dzienia)  

### 26 December 2019

* HTTP Server now supports full configuration edit and save.  
* WebConfig sources moved to separate files.  
* Backup copy of `M5NS.INI` is copied to M5NS.BAK during save.  

### 20 October 2019

This is mostly test release with new "geek" features. Should be mostly for people with specific troubles and early adopters.

* Maximum password length extended to 63 characters.
* Possibility to connect to open WiFi network (without password).  
device_name key added to `M5NS.INI` (default is M5NS).  
* mDNS added, you can connect to M5 Nightscout monitor by device_name.local (default M5NS.local).  
* mDNS name and IP address is displayed on Error log page.  
* Experimental web server added. Display configuration and check the current and the latest firmware version.  
* Online update option from internal web page.  

> Please note, there is no security implemented yet. Internal web page is on unsecured port 80. Updates are going from server port 80, no SLL used currently. Use on your own risk.  

### 10 October 2019

* Key invert_display (default = -1 when key is not present) added to `M5NS.INI`. You should not use it in `M5NS.INI` unless you have a problem with display inversion. Set invert_display = 0 or invert_display = 1 if you have troubles to call M5.Lcd.invertDisplay(0) or M5.Lcd.invertDisplay(1). Under normal circumstances 0 should be normal display and 1 should be inverted display.  

### 23 September 2019

* Key sgv_only (default 0) added to `M5NS.INI`. You should set it to 1 if you use xDrip, Spike or similar to filter out calibrations etc.  
* Explicit M5.Lcd.invertDisplay(0) added to try to prevent inverted display.  

### 21 September 2019

* Added support for Dexcom by using Sugarmate connection workaround. Thanks to Patrick Sonnerat.  
* Only SGV entries are now queried, so no more troubles with calibration. Thanks to Sulka Haro.  
* Fast page switching. No need to wait for NS data (does not work while accessing Nightscout = while blue WiFi icon displayed).  
* New page with analog clock and Temperature/Humidity. Display environment values requires DHT12 - ENV Unit or BTC Standing Base.  
* New key in `M5NS.INI` temperature_unit = 1 for CELSIUS, 2 for KELVIN, 3 for FAHRENHEIT. Can be omitted (default is Celsius).  
* Display rotation possibility added. New key in `M5NS.INI` display_rotation = 1 (buttons down, default, can be omitted), 3 = buttons up, 5 = mirror buttons up, 7 = mirror buttons down.  
* US date format added. New key in `M5NS.INI` date_format = 0 (dd.mm., default, can be omitted), 1 = MM/DD.  
* JSON query update for some Bluetooth Glucose Meters.  

### 20 June 2019

* Split of Nightscout read and display code (this should allow simpler user display code update and more different "faces" from users).  
* New concept of display pages (different display designs, information, faces).  
* Switch page by short press of the right button.  
* Power OFF by the right button long press (4 seconds).  
* Right button works also as power ON after power off by this button.  
* New page added with large simple info (large BG, clock, delta + arrow and  few icons only).  
* New `M5NS.INI` key "default_page" added (default 0).  
* Smaller WiFi symbol (now as blue WiFi icon in 2 sizes for the 2 different Nightscout queries).  
* Buttons do not work during Nightscout communication (blue WiFi symbol displayed).  
* Bigger delta value even with COB+IOB values displayed (COB: and IOB: shortened to C: and I: ).  
* Errors now logged silently (log can be displayed as the last page).  
* Warning triangle icon added to show that errors are in the log (up to 5 errors - grey, more - yellow). Only last 10 errors can be displayed.  
* New `M5NS.INI` key "restart_at_time" added (default no restart) to restart M5Stack regularly at predefined time to reconnect to WiFi access point and clear possible other errors. No startup sound during soft restart, snooze state reapplied, errors cleared.  
* New `M5NS.INI` key "restart_at_logged_errors" added (default no restart) to restart M5Stack after predefined amount of errors logged in error log to reconnect to WiFi access point and clear possible other errors. No startup sound during soft restart, snooze state reapplied, errors cleared.  
* Right button power icon changed to door icon to better express the page change/power off functions.  

### 12 June 2019

* More silent speaker. Found the way how to switch off adc1 after sound play.  
* Added token key in `M5NS.INI` to allow connection to secured Nightscout sites (thanks to Peter Leimbach).  
* Added keys snd_warning_at_startup and snd_alarm_at_startup to play warning/alarm sound test during startup (1 = play, 0 = do not play).  

### 09 June 2019

* More WiFi APs possible. Now you can create section [wlan0], [wlan1], up to [wlan9] in `M5NS.INI`.  
* Added SD card info for better error handling.  
* Added empty Nightscout check. No restarts repeat if Nightscout is empty.  
* Wait for NTP time synchronization.  

### 07 June 2019

* Added check for http/https in Nightscout URL in `M5NS.INI`  
* Increased default warning volume to 50.  
* Added last 2 weeks revisions to README.  

### 02 June 2019

* Large DELTA value displayed if no COB/IOB on display.  
* Sample `M5NS.INI` file now has default values in mg/dL.  
* Corrected volume bug, it did not work at all. Now accepts values from M5NI.INI correctly.  

### 30 May 2019 

* Added battery icon. This feature works only on newer M5Stack units. Removed seconds from time to make more place for possibly more icons.  

### 23 May 2019

* properties were not defined on Nightscout.  

### 18 May 2019

* Added button function icons (set `M5NS.INI` key info_line = 1). This is now default option.  
* Added loop and basal info (set `M5NS.INI` key info_line = 2).  
* Original sensor information available when `M5NS.INI` key info_line = 0  
* Small changes to silence background hiss as much as possible.  

### 12 May 2019

* BG/calibration/unknown entries are now filtered.  
* Missed reading sound alert added. You can adjust it by snd_no_readings key in `M5NS.INI` (default 20 minutes).  
* Added possibility to change warning sound volume by warning_volume key in `M5NS.INI` (0-100, default=20, 0=silent).  
* Added possibility to change alarm sound volume by alarm_volume key in `M5NS.INI` (0-100, default=100, 0=silent).  
* Reorganized left upper part of display to get space for COB and IOB display.  
* Added show_COB_IOB key to `M5NS.INI`. If show_COB_IOB = 1 then carbs and insulin on board are displayed. Set 1 (ON) by default.  
* COB and IOB are grey if 0 and white if any carbs or IU on board.  
* Added key show_current_time to `M5NS.INI`. If show_current_time = 1 (default now) then current clock is displayed instead of last sensor reding time.  

### 2 May 2019

* Snooze alarm function introduced and placed on the middle button.  
* New `M5NS.INI` key snooze_timeout (default 30 min) to specify time for how long should be sound alarm silent after press of the middle button.  
* New `M5NS.INI` key alarm_repeat to specify time (default 5 min) when sound alarm should repeat if its reason remains.  
* Corrected bug with alarm sometimes repeating twice.  
* WiFi symbol moved to the source code. External SD file is no more needed.  
* Configuration file `M5NS.INI` handling moved to separate source files.  

### 27 Apr 2019 - 2

Larger JSONDocument size for xDrip and possibly other Nightscout upload application compatibility.  
A little bit better HTTP error handling and error printing to the M5Stack screen.  

* When `show_mgdl = 1`, then all values in `M5NS.INI` have to be in mg/dL instead of mmol/L.  
* Updated device detection for xDrip.  

### 27 Apr 2019

* Only one query to Nightscout for minigraph as well as the last value. Faster code execution, less traffic.  

### 20 Apr 2019 - 2

* Added the main source code M5_NightscoutMon.ino to GitHub. Sorry I forgot in initial commit ;-)  

### 20 Apr 2019

* Initial GitHub commit  
