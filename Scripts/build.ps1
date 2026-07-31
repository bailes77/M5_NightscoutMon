<#
.SYNOPSIS
    Compiles M5_NightscoutMon into the minimum set of firmwares for the whole
    M5Stack lineup plus supported third-party boards, and exports them to
    <repo>\Binaries\<group>\.

.DESCRIPTION
    One M5Unified source now covers every board; the firmware count is driven only
    by chip architecture + flash size. Four build groups:

      Basic4MB   - old Basic (<=2020.5, 4 MB, no PSRAM). min_spiffs is the only
                   4 MB scheme that fits AND keeps OTA. This board is the growth
                   ceiling for its own image; it no longer caps the others.
      ESP32_16MB - Basic 16 MB/v2.7, Fire, all Core2 (16 MB ESP32). default_16MB
                   gives 2x 6.25 MiB OTA slots (~5 MB expansion headroom), so the
                   app can grow and still self-update OTA with no repartition/USB
                   reflash. Built PSRAM-off so the single image is also safe on the
                   no-PSRAM Basic 16 MB (Fire/Core2 PSRAM sits idle; app needs none).
      CoreS3     - all CoreS3 (K128/Lite/SE/K149). Separate binary only because
                   ESP32-S3 is a different CPU architecture.
      JC3248W535 - Guition JC3248W535 3.5" (non-M5 ESP32-S3 board): the M5Unified
                   calls are swapped for the hal_jc3248w535 shim via
                   -DDEVICE_JC3248W535. Needs "GFX Library for Arduino" 1.6.0.

    Board sub-variants (AXP192/AXP2101 PMU, IMU, RTC, touch) are auto-detected by
    M5Unified at runtime, so no further binaries are needed.

    First time on a machine? Run Scripts\setup.ps1 (or setup.bat) once - it
    installs arduino-cli (if needed), the pinned esp32 board core and all
    required libraries (versions in Scripts\deps.psd1). This script re-checks
    those dependencies before every build and fails with instructions if
    something is missing.

.PARAMETER Target
    Basic4MB | ESP32_16MB | CoreS3 | JC3248W535 | All. Omit for an interactive menu.

.PARAMETER ArduinoCli
    Path to arduino-cli.exe. If omitted, the script looks at the ARDUINO_CLI
    environment variable, then PATH, then the known Arduino IDE 2.x install
    locations (per-user and all-users).

.EXAMPLE
    .\build.ps1                 # interactive menu
    .\build.ps1 -Target All     # build all four firmwares
#>

[CmdletBinding()]
param(
    [ValidateSet('Basic4MB', 'ESP32_16MB', 'CoreS3', 'JC3248W535', 'All')]
    [string]$Target,

    [string]$ArduinoCli,

    # Wipe each target's build folder before compiling (guaranteed-fresh build).
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

# --- Paths --------------------------------------------------------------------
# The sketch is the single .ino in the repo root (this script lives in <repo>\Scripts\).
$RepoRoot = Split-Path $PSScriptRoot -Parent
$inoFiles = @(Get-ChildItem -Path $RepoRoot -Filter *.ino -File)
if ($inoFiles.Count -ne 1) {
    throw "Expected exactly one .ino sketch in $RepoRoot, found $($inoFiles.Count)."
}
$Sketch     = $inoFiles[0].FullName
$SketchName = $inoFiles[0].BaseName

# --- Locate arduino-cli + Arduino folders (helpers in common.ps1) -------------
# Order: -ArduinoCli param > ARDUINO_CLI env var > PATH > known install locations
# (Arduino IDE 2.x per-user/all-users layouts, plus the setup.ps1 standalone
# folder). Cores/libraries resolve to the standard per-user folders unless
# ARDUINO_DIRECTORIES_DATA / ARDUINO_DIRECTORIES_USER are already set.
. (Join-Path $PSScriptRoot 'common.ps1')
$ArduinoCli = Find-ArduinoCli -Candidate $ArduinoCli
Initialize-ArduinoDirs

# --- Target table -------------------------------------------------------------
# Order matters for the interactive menu.
$Targets = [ordered]@{
    'Basic4MB'   = @{ Fqbn = 'esp32:esp32:m5stack-core-esp32:PartitionScheme=min_spiffs';           Folder = 'Basic_4MB';   Desc = 'old Basic <=2020.5' }
    'ESP32_16MB' = @{ Fqbn = 'esp32:esp32:m5stack-fire:PartitionScheme=default,PSRAM=disabled';      Folder = 'ESP32_16MB';  Desc = 'Basic 16MB, Fire, all Core2' }
    'CoreS3'     = @{ Fqbn = 'esp32:esp32:m5stack-cores3:PartitionScheme=default_16MB';              Folder = 'CoreS3';      Desc = 'all CoreS3 (ESP32-S3)' }
    # Non-M5 board: generic S3 FQBN + the DEVICE define that swaps M5Unified for the
    # hal_jc3248w535 shim. Requires the Arduino_GFX library ("GFX Library for Arduino",
    # pin 1.6.0 - 1.6.1 breaks the AXS15231B panel). Part of the release ('All') set:
    # it is distributed through the web flasher and OTA like the M5 groups.
    'JC3248W535' = @{ Fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,FlashMode=dio,CDCOnBoot=cdc'; Folder = 'JC3248W535'; Desc = 'Guition JC3248W535 3.5in (ESP32-S3)'; Extra = '-DDEVICE_JC3248W535' }
}

# Common flag: matches the PlatformIO fix for the missing gpio_deep_sleep_hold_dis
# method on this core build. Harmless where not needed.
$ExtraFlags = 'compiler.cpp.extra_flags=-Dgpio_deep_sleep_hold_dis=_NOP'

# --- Firmware version (drives OTA's update.inf) -------------------------------
# OTA (M5NSWebConfig.cpp) compares this string lexicographically, so it must stay
# a fixed-width, zero-padded, monotonically increasing token (currently YYYYMMDDnn).
$versionMatch = Select-String -Path $Sketch -Pattern 'String\s+M5NSversion\("([^"]+)"\)' | Select-Object -First 1
if (-not $versionMatch) {
    throw "Could not find M5NSversion in $Sketch"
}
$FirmwareVersion = $versionMatch.Matches[0].Groups[1].Value

# --- Interactive menu (only if no -Target given) ------------------------------
if (-not $Target) {
    Write-Host ''
    Write-Host 'M5_NightscoutMon - firmware build' -ForegroundColor Cyan
    Write-Host '---------------------------------'
    $i = 1
    $indexMap = @{}
    foreach ($name in $Targets.Keys) {
        Write-Host ("  [{0}] {1,-11} ({2})" -f $i, $name, $Targets[$name].Desc)
        $indexMap[[string]$i] = $name
        $i++
    }
    Write-Host ("  [{0}] All" -f $i)
    $indexMap[[string]$i] = 'All'
    Write-Host ''
    $choice = Read-Host 'Select target'
    if (-not $indexMap.ContainsKey($choice)) { throw "Invalid selection: '$choice'" }
    $Target = $indexMap[$choice]
}

# --- Resolve selection to a list of target names ------------------------------
# 'All' = the release set; targets marked SkipInAll build only when named explicitly.
$toBuild = if ($Target -eq 'All') { @($Targets.Keys | Where-Object { -not $Targets[$_].SkipInAll }) } else { @($Target) }

# --- Dependency sanity check --------------------------------------------------
# Fail early with an actionable message instead of a cryptic compile error when
# the esp32 core or a required library is missing or the wrong version
# (pinned versions: deps.psd1; checks: common.ps1).
$depCheck = Get-DependencyProblems -Targets $toBuild
foreach ($w in $depCheck.Warnings) { Write-Host "WARNING: $w" -ForegroundColor Yellow }
if ($depCheck.Problems) {
    throw ("Missing/incompatible build dependencies:`n  - " + ($depCheck.Problems -join "`n  - ") +
           "`nRun Scripts\setup.bat (or Scripts\setup.ps1) to install everything automatically.")
}

# --- Auto-bump version on release ('All') builds ------------------------------
# A full build of all targets is treated as a release: bump the same-day
# sequence number (or start a new day at 01) and write it back into the sketch
# *before* compiling, so the binary, the on-device version display, and
# update.inf all carry the new version. Single-target builds are test
# iterations and never bump.
if ($Target -eq 'All') {
    if ($FirmwareVersion -notmatch '^(\d{8})(\d{2})$') {
        throw "M5NSversion '$FirmwareVersion' is not in YYYYMMDDnn format; cannot auto-bump."
    }
    $today = Get-Date -Format 'yyyyMMdd'
    if ($Matches[1] -eq $today) {
        $seq = [int]$Matches[2] + 1
        if ($seq -gt 99) {
            throw "Already at sequence 99 for $today; cannot auto-bump further today."
        }
    } else {
        $seq = 1
    }
    $NewVersion = '{0}{1:D2}' -f $today, $seq

    Write-Host ("Bumping version: {0} -> {1}" -f $FirmwareVersion, $NewVersion) -ForegroundColor Yellow

    $inoText = [IO.File]::ReadAllText($Sketch)
    $inoText = $inoText -replace [regex]::Escape("M5NSversion(`"$FirmwareVersion`")"), "M5NSversion(`"$NewVersion`")"
    [IO.File]::WriteAllText($Sketch, $inoText)

    $FirmwareVersion = $NewVersion
}

# --- Build --------------------------------------------------------------------
# Each target gets its own arduino-cli build-path: arduino-cli otherwise derives
# one shared cache folder from the sketch path alone, so concurrent builds of
# different targets clobbered each other's object files. (Concurrent builds of
# the *same* target will still collide - don't launch build.ps1 twice for the
# same -Target at once.)
$BuildCacheRoot = Join-Path $env:LOCALAPPDATA "arduino\builds\$SketchName"

$results = @()
foreach ($name in $toBuild) {
    $t         = $Targets[$name]
    $outDir    = Join-Path (Join-Path $RepoRoot 'Binaries') $t.Folder
    $buildPath = Join-Path $BuildCacheRoot $name

    Write-Host ''
    Write-Host ("=== Building $name ===") -ForegroundColor Green
    Write-Host ("    FQBN : {0}" -f $t.Fqbn)
    Write-Host ("    Out  : {0}" -f $outDir)

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    if ($Clean -and (Test-Path $buildPath)) {
        Remove-Item -Recurse -Force $buildPath
    }
    New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

    $targetFlags = $ExtraFlags
    if ($t.Extra) { $targetFlags += ' ' + $t.Extra }

    & $ArduinoCli compile `
        --fqbn $t.Fqbn `
        --build-property $targetFlags `
        --build-path $buildPath `
        --output-dir $outDir `
        $Sketch

    if ($LASTEXITCODE -ne 0) {
        throw "Build FAILED for $name (arduino-cli exit $LASTEXITCODE)."
    }

    # Keep only the flashable bins; drop the heavy .elf/.map debug artifacts (~40 MB/group).
    Get-ChildItem $outDir -Include *.elf, *.map -File -Recurse | Remove-Item -Force

    # OTA (M5NSWebConfig.cpp) pulls this file to decide whether a newer firmware is
    # available; regenerate it every build so it always matches what was just built.
    # whatsnew.txt is hand-authored and intentionally left untouched here.
    $infPath = Join-Path $outDir 'update.inf'
    Set-Content -Path $infPath -Value $FirmwareVersion -NoNewline -Encoding ascii

    $results += $name
}

Write-Host ''
Write-Host ("Done. Built: {0}" -f ($results -join ', ')) -ForegroundColor Cyan
Write-Host "Binaries are in <repo>\Binaries\<group>\ (app + bootloader + partitions). See Scripts\README.md to flash."
