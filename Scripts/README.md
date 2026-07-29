# Firmware build scripts

Since the M5Unified migration, **one source** (`M5_NightscoutMon.ino`) compiles for the whole
M5Stack lineup. The board is auto-detected at runtime, so the only reason more than one binary
exists is **CPU architecture + flash size** — not source differences. That reduces to **three
firmwares**.

## Usage

```
Scripts\build.bat                 # interactive menu
Scripts\build.bat -Target All     # build all three
Scripts\build.ps1 -Target CoreS3  # one target (PowerShell directly)
```

`build.bat` is just a double-click launcher for `build.ps1` (bypasses the PowerShell execution
policy). Each build lands in `Binaries\<group>\`.

To use a different `arduino-cli`, pass `-ArduinoCli <path>` or set `$env:ARDUINO_CLI`. The default
is the one bundled with Arduino IDE 2.x.

Each target builds in its own cache folder (`%LOCALAPPDATA%\arduino\builds\M5_NightscoutMon\<target>`),
so different targets can build concurrently without corrupting each other's object files. Pass
`-Clean` to wipe a target's build folder first for a guaranteed-fresh build.

## The three firmwares

| Firmware (folder)     | Build (FQBN)                                                   | Runs on                                   |
|-----------------------|---------------------------------------------------------------|-------------------------------------------|
| `Binaries\Basic_4MB`  | `m5stack-core-esp32` · `min_spiffs`                           | **only** the old Basic (≤2020.5, 4 MB)    |
| `Binaries\ESP32_16MB` | `m5stack-fire` · `default` (16 MB) · **PSRAM off**            | Basic 16 MB/v2.7, Fire, **all** Core2     |
| `Binaries\CoreS3`     | `m5stack-cores3` · `default_16MB`                            | **all** CoreS3 (K128 / Lite / SE / K149)  |
| `Binaries\JC3248W535` | `esp32s3` · `app3M_fat9M_16MB` · PSRAM opi · flash dio · `-DDEVICE_JC3248W535` | Guition JC3248W535 3.5" only (non-M5 board) |

**JC3248W535 is not part of `-Target All`** (release) builds yet — build it explicitly with
`-Target JC3248W535` until it's hardware-validated. It additionally requires the
**"GFX Library for Arduino" (Arduino_GFX) 1.6.0** library (1.6.1 is reported broken with
its AXS15231B panel): `arduino-cli lib install "GFX Library for Arduino@1.6.0"`.

Core2 and CoreS3 sub-variants (AXP192 vs AXP2101 PMU, BMI270 vs MPU6886 IMU, RTC, touch) are all
detected at runtime by M5Unified — they need no separate binary.

### Why the split

* **The 4 MB Basic is on its own line.** OTA **cannot change the partition table**, and 4 MB flash
  only fits the small `min_spiffs` layout (2 × 1.875 MiB app slots; ~70 % full). Isolating it means
  every *other* board can use the full-16 MB `default` layout (2 × 6.25 MiB slots, ~21 % full) — so
  the app can grow ~5× and still self-update over-the-air, with no USB reflash to repartition.
  The 4 MB Basic remains the growth ceiling **for its own image only**.
* **`ESP32_16MB` is built PSRAM-off** so the single image is safe on the no-PSRAM Basic 16 MB while
  still running on the PSRAM-equipped Fire/Core2. The app needs no PSRAM (≈1.39 MB flash, 18 % RAM),
  so Fire/Core2 PSRAM simply sits idle.
* **CoreS3 is separate** only because ESP32-S3 is a different CPU architecture (incompatible machine
  code).

### Flashing rule

* `Basic_4MB` → the 4 MB Basic only.
* `ESP32_16MB` → any 16 MB **ESP32** board (Basic 16 MB, Fire, Core2). Safe on the no-PSRAM Basic.
* `CoreS3` → CoreS3 only.

Never flash `ESP32_16MB` to a 4 MB Basic (won't fit), and don't cross the ESP32 ↔ ESP32-S3 line.

## How to flash a built firmware

Each folder holds three files: `*.ino.bin` (app), `*.ino.bootloader.bin`, `*.ino.partitions.bin`.

**Easiest — let arduino-cli place them at the right offsets:**

```
arduino-cli upload -p COM5 `
  --fqbn esp32:esp32:m5stack-fire:PartitionScheme=default,PSRAM=disabled `
  --input-dir Binaries\ESP32_16MB
```
(use the matching FQBN for the group — see the table above).

**Manual — esptool with explicit offsets:**

ESP32 boards (`Basic_4MB`, `ESP32_16MB`) — bootloader at **0x1000**:
```
esptool --chip esp32 -p COM5 write_flash `
  0x1000  M5_NightscoutMon.ino.bootloader.bin `
  0x8000  M5_NightscoutMon.ino.partitions.bin `
  0x10000 M5_NightscoutMon.ino.bin
```

ESP32-S3 boards (`CoreS3`, `JC3248W535`) — bootloader at **0x0**:
```
esptool --chip esp32s3 -p COM5 write_flash `
  0x0     M5_NightscoutMon.ino.bootloader.bin `
  0x8000  M5_NightscoutMon.ino.partitions.bin `
  0x10000 M5_NightscoutMon.ino.bin
```

After the first full flash, subsequent updates can go over-the-air (OTA) — all three schemes keep
two app slots.

**Browser — the `Flasher\` page (end users):**

`Flasher\index.html` is an [ESP Web Tools](https://esphome.github.io/esp-web-tools/) page: one
Install button per firmware group, flashing over Web Serial from Chrome/Edge — no software or
driver knowledge needed. Each group has a static `Flasher\manifest-*.json` with the bins and
offsets; the page reads `Binaries\<group>\update.inf` at load time to display and stamp the
current version, so publishing an OTA update automatically updates the flasher too — the
manifests never need editing.

It must be served over HTTPS from the same origin as `Binaries\`: enable GitHub Pages
(Settings → Pages → Deploy from a branch → `master`, `/ (root)`) and the page is at
`https://<user>.github.io/M5_NightscoutMon/Flasher/`. It cannot run from `file://` (fetch of the
manifests fails); for local testing use `python -m http.server` from the repo root.

## Publishing an OTA update

The device pulls OTA firmware straight from this GitHub repo (`raw.githubusercontent.com`,
`master` branch, `Binaries/<group>/`) — there is no separate update server. Publishing an update
is:

1. `Scripts\build.ps1 -Target All` — a full build is treated as a release: it auto-bumps
   `M5NSversion` in `M5_NightscoutMon.ino` (same-day sequence `nn` +1, or a new day starts at `01`)
   *before* compiling, then (re)writes `Binaries\<group>\update.inf` with that new version for all
   three groups. Single-target builds (`-Target CoreS3` etc.) never bump the version — use those
   for test iterations.
2. Edit `Binaries\<group>\whatsnew.txt` by hand if you want a changelog shown on the device's web
   page (not generated by the build).
3. Commit and push both `M5_NightscoutMon.ino` (the bumped version line) and `Binaries\` to `master`.

There is no staging: the moment `Binaries/<group>/update.inf` on `master` shows a higher version
than a device's `M5NSversion`, that device's web page will offer the update, and `Binaries/<group>/M5_NightscoutMon.ino.bin`
on `master` is exactly what it will flash. Only push once the corresponding `.bin` is the one
you intend devices to install.

OTA connects over HTTPS with certificate validation disabled (`setInsecure()`) — this is
deliberate: GitHub only serves raw content over TLS (no plain-HTTP fallback exists), and skipping
chain validation means a future GitHub CA rotation can't break OTA the way a pinned certificate
would. There is no other integrity check beyond the ESP32 OTA slot's own image validation.
