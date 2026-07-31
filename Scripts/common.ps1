# Shared helpers for the M5_NightscoutMon build scripts.
# Dot-sourced by build.ps1 and setup.ps1:   . (Join-Path $PSScriptRoot 'common.ps1')
# Everything works from the caller's environment plus the standard per-user
# Arduino folders, so the scripts run on any Windows machine without editing.
# Compatible with Windows PowerShell 5.1 (what build.bat/setup.bat launch).

# Where setup.ps1 drops the standalone arduino-cli when none is installed.
# Find-ArduinoCli probes the same folder, so a setup-downloaded CLI is found
# automatically on later builds.
$Script:StandaloneCliDir = Join-Path $env:USERPROFILE 'tools\arduino-cli'

function Get-BuildDeps {
    # Pinned core + library versions (single source of truth for both scripts).
    Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot 'deps.psd1')
}

function Find-ArduinoCli {
    <# Locates arduino-cli.exe. Order: -Candidate (the caller's -ArduinoCli
       param) > ARDUINO_CLI env var > PATH > known Arduino IDE 2.x install
       locations (per-user and all-users) > the setup.ps1 standalone folder.
       Throws with an actionable message unless -Quiet, which returns $null. #>
    param(
        [string]$Candidate,
        [switch]$Quiet
    )
    if (-not $Candidate) { $Candidate = $env:ARDUINO_CLI }
    if ($Candidate) {
        if (Test-Path $Candidate) { return $Candidate }
        if ($Quiet) { return $null }
        throw "arduino-cli not found at '$Candidate'."
    }
    $pathCmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($pathCmd) { return $pathCmd.Source }
    $probed = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe'),
        (Join-Path $env:ProgramFiles  'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path $env:ProgramFiles  'Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe'),
        (Join-Path $Script:StandaloneCliDir 'arduino-cli.exe')
    )
    $found = $probed | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($found) { return $found }
    if ($Quiet) { return $null }
    throw ("arduino-cli not found. Locations tried (after -ArduinoCli/`$env:ARDUINO_CLI/PATH):`n  " +
           ($probed -join "`n  ") +
           "`nRun Scripts\setup.bat (it can download a standalone arduino-cli), pass -ArduinoCli <path>, " +
           "set `$env:ARDUINO_CLI, or add arduino-cli to PATH.")
}

function Initialize-ArduinoDirs {
    # Respect the caller's env; otherwise use the standard per-user defaults
    # (GetFolderPath handles OneDrive-redirected Documents).
    if (-not $env:ARDUINO_DIRECTORIES_DATA) {
        $env:ARDUINO_DIRECTORIES_DATA = Join-Path $env:LOCALAPPDATA 'Arduino15'
    }
    if (-not $env:ARDUINO_DIRECTORIES_USER) {
        $env:ARDUINO_DIRECTORIES_USER = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino'
    }
}

function Get-InstalledEsp32CoreVersions {
    # Version folder names under the esp32:esp32 platform dir; @() if none.
    $coreRoot = Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\esp32\hardware\esp32'
    if (-not (Test-Path $coreRoot)) { return @() }
    @(Get-ChildItem $coreRoot -Directory | Select-Object -ExpandProperty Name)
}

function Get-InstalledLibraries {
    # Hashtable of library name -> version, read from each library.properties in
    # the user libraries folder (covers installs by arduino-cli, the IDE, or git
    # clone - as long as the library ships a library.properties).
    $result = @{}
    $libRoot = Join-Path $env:ARDUINO_DIRECTORIES_USER 'libraries'
    if (-not (Test-Path $libRoot)) { return $result }
    foreach ($dir in Get-ChildItem $libRoot -Directory) {
        $props = Join-Path $dir.FullName 'library.properties'
        if (-not (Test-Path $props)) { continue }
        $name = $null; $version = $null
        foreach ($line in Get-Content $props) {
            if     ($line -match '^\s*name\s*=\s*(.+?)\s*$')    { $name    = $Matches[1] }
            elseif ($line -match '^\s*version\s*=\s*(.+?)\s*$') { $version = $Matches[1] }
        }
        if ($name -and -not $result.ContainsKey($name)) { $result[$name] = $version }
    }
    return $result
}

function Get-DependencyProblems {
    <# Checks the installed environment against deps.psd1 for the given build
       targets. Returns @{ Problems = fatal issues; Warnings = notes }. #>
    param([string[]]$Targets)
    $deps = Get-BuildDeps
    $problems = @()
    $warnings = @()

    $cores = Get-InstalledEsp32CoreVersions
    if (-not $cores) {
        $problems += "esp32 board core (esp32:esp32) is not installed."
    }
    elseif (-not ($cores | Where-Object { $_ -like '2.*' })) {
        $problems += ("esp32 core {0} is installed, but this project needs the 2.x core " +
                      "(known-good: {1}) - it does not build on 3.x.") -f ($cores -join ', '), $deps.EspCoreVersion
    }

    $libs = Get-InstalledLibraries
    foreach ($lib in $deps.Libraries) {
        if ($lib.OnlyFor -and $Targets -notcontains $lib.OnlyFor) { continue }
        $note = if ($lib.Note) { " ($($lib.Note))" } else { '' }
        if (-not $libs.ContainsKey($lib.Name)) {
            $problems += "Library '$($lib.Name)' is not installed (need $($lib.Version))$note."
            continue
        }
        $installed = $libs[$lib.Name]
        if ($lib.Exact -and $installed -ne $lib.Version) {
            $problems += "Library '$($lib.Name)' must be exactly $($lib.Version), found $installed$note."
        }
        else {
            try {
                if ([version]$installed -lt [version]$lib.Version) {
                    $warnings += "Library '$($lib.Name)' $installed is older than the tested $($lib.Version); the build may still work."
                }
            } catch { }  # unparseable version (e.g. git clone) - assume OK
        }
    }

    @{ Problems = $problems; Warnings = $warnings }
}
