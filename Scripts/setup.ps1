<#
.SYNOPSIS
    One-time Windows environment setup for building M5_NightscoutMon:
    arduino-cli + esp32 board core + required libraries.

.DESCRIPTION
    Turns a fresh Windows PC (nothing but this git clone) into a working build
    environment. Idempotent - re-run any time; anything already present is
    skipped. Steps:

      1. Locate arduino-cli (-ArduinoCli param > ARDUINO_CLI env var > PATH >
         Arduino IDE 2.x installs). If none is found, offers to download the
         standalone CLI to %USERPROFILE%\tools\arduino-cli - a location
         build.ps1 also probes, so later builds find it automatically.
      2. Install the esp32:esp32 board core pinned in deps.psd1 (2.x line;
         the project does not build on the 3.x core).
      3. Install the libraries pinned in deps.psd1. Existing installs are left
         alone (a warning if older than the tested version) unless an exact
         version is required (Arduino_GFX 1.6.0 for the JC3248W535 panel).

    Cores and libraries land in the standard per-user Arduino folders
    (%LOCALAPPDATA%\Arduino15 and Documents\Arduino), shared with the Arduino
    IDE. If yours live elsewhere, set $env:ARDUINO_DIRECTORIES_DATA /
    $env:ARDUINO_DIRECTORIES_USER before running.

.PARAMETER ArduinoCli
    Path to arduino-cli.exe, if auto-discovery should be skipped.

.PARAMETER DownloadCli
    Download the standalone arduino-cli without asking when none is found
    (for unattended runs).

.EXAMPLE
    .\setup.ps1              # normal interactive run
    .\setup.ps1 -DownloadCli # unattended, fetches arduino-cli if needed
#>

[CmdletBinding()]
param(
    [string]$ArduinoCli,
    [switch]$DownloadCli
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$deps = Get-BuildDeps

Write-Host ''
Write-Host 'M5_NightscoutMon - build environment setup' -ForegroundColor Cyan
Write-Host '------------------------------------------'

# --- 1. arduino-cli -----------------------------------------------------------
$cli = Find-ArduinoCli -Candidate $ArduinoCli -Quiet
if (-not $cli) {
    Write-Host 'arduino-cli not found (checked -ArduinoCli, $env:ARDUINO_CLI, PATH, Arduino IDE 2.x installs).'
    if (-not $DownloadCli) {
        $answer = Read-Host "Download the standalone arduino-cli to $Script:StandaloneCliDir ? [Y/n]"
        if ($answer -match '^[nN]') {
            throw 'Aborted. Install the Arduino IDE 2.x or arduino-cli (https://arduino.github.io/arduino-cli/) and re-run.'
        }
    }
    # Official "latest" redirect from Arduino; TLS 1.2 needed on Windows PowerShell 5.1.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $zip = Join-Path $env:TEMP 'arduino-cli_Windows_64bit.zip'
    Write-Host 'Downloading arduino-cli (latest Windows 64-bit)...'
    $oldProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'   # progress bar slows IWR badly on 5.1
    try {
        Invoke-WebRequest -Uri 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' `
                          -OutFile $zip -UseBasicParsing
    } finally {
        $ProgressPreference = $oldProgress
    }
    New-Item -ItemType Directory -Force -Path $Script:StandaloneCliDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $Script:StandaloneCliDir -Force
    Remove-Item $zip -Force
    $cli = Join-Path $Script:StandaloneCliDir 'arduino-cli.exe'
    if (-not (Test-Path $cli)) { throw "Download/extract failed: $cli not found." }
}
Write-Host ("arduino-cli : {0}" -f $cli)

Initialize-ArduinoDirs
Write-Host ("Cores in    : {0}" -f $env:ARDUINO_DIRECTORIES_DATA)
Write-Host ("Libraries in: {0}" -f (Join-Path $env:ARDUINO_DIRECTORIES_USER 'libraries'))

# --- 2. esp32 board core ------------------------------------------------------
Write-Host ''
Write-Host '=== esp32 board core ===' -ForegroundColor Green
$cores = Get-InstalledEsp32CoreVersions
if ($cores -contains $deps.EspCoreVersion) {
    Write-Host ("esp32:esp32 {0} already installed - OK" -f $deps.EspCoreVersion)
}
elseif ($cores | Where-Object { $_ -like '2.*' }) {
    Write-Host ("esp32:esp32 {0} already installed (2.x) - keeping it. Known-good is {1}; to switch:" -f ($cores -join ', '), $deps.EspCoreVersion)
    Write-Host ("  arduino-cli core install esp32:esp32@{0} --additional-urls {1}" -f $deps.EspCoreVersion, $deps.EspCoreIndexUrl)
}
else {
    if ($cores) {
        Write-Host ("esp32 core {0} found - replacing with {1} (the project does not build on 3.x)." -f ($cores -join ', '), $deps.EspCoreVersion) -ForegroundColor Yellow
    }
    Write-Host ("Installing esp32:esp32@{0} (large download, takes a while)..." -f $deps.EspCoreVersion)
    & $cli core update-index --additional-urls $deps.EspCoreIndexUrl
    if ($LASTEXITCODE -ne 0) { throw "arduino-cli core update-index failed (exit $LASTEXITCODE)." }
    & $cli core install "esp32:esp32@$($deps.EspCoreVersion)" --additional-urls $deps.EspCoreIndexUrl
    if ($LASTEXITCODE -ne 0) { throw "esp32 core install failed (exit $LASTEXITCODE)." }
}

# --- 3. Libraries -------------------------------------------------------------
Write-Host ''
Write-Host '=== Libraries ===' -ForegroundColor Green
$indexUpdated = $false
$installed = Get-InstalledLibraries
foreach ($lib in $deps.Libraries) {
    $spec = "$($lib.Name)@$($lib.Version)"
    $have = if ($installed.ContainsKey($lib.Name)) { $installed[$lib.Name] } else { $null }

    if ($have -eq $lib.Version) {
        Write-Host ("{0} {1} already installed - OK" -f $lib.Name, $have)
        continue
    }
    if ($have -and -not $lib.Exact) {
        Write-Host ("{0} {1} already installed - keeping it (tested with {2})" -f $lib.Name, $have, $lib.Version)
        continue
    }
    if ($have) {
        # Exact pin mismatch - replace.
        Write-Host ("{0} {1} found but {2} is required ({3}) - replacing..." -f $lib.Name, $have, $lib.Version, $lib.Note) -ForegroundColor Yellow
    } else {
        Write-Host ("Installing {0}..." -f $spec)
    }
    if (-not $indexUpdated) {
        & $cli lib update-index
        if ($LASTEXITCODE -ne 0) { throw "arduino-cli lib update-index failed (exit $LASTEXITCODE)." }
        $indexUpdated = $true
    }
    & $cli lib install $spec
    if ($LASTEXITCODE -ne 0) { throw "Library install failed for '$spec' (exit $LASTEXITCODE)." }
}

# --- 4. Final verification ----------------------------------------------------
Write-Host ''
$check = Get-DependencyProblems -Targets @('Basic4MB', 'ESP32_16MB', 'CoreS3', 'JC3248W535')
foreach ($w in $check.Warnings) { Write-Host "WARNING: $w" -ForegroundColor Yellow }
if ($check.Problems) {
    throw ("Setup finished but the environment still has problems:`n  - " + ($check.Problems -join "`n  - "))
}
Write-Host 'Environment ready. Build with Scripts\build.bat (or Scripts\build.ps1).' -ForegroundColor Cyan
