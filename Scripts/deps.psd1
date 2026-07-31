# Pinned build dependencies for M5_NightscoutMon.
# Read by setup.ps1 (installs them) and build.ps1 (pre-build check) via common.ps1.
@{
    # esp32:esp32 board core. Must stay on the 2.x line: the sketch and the
    # gpio_deep_sleep_hold_dis build flag target the 2.x Arduino-ESP32 API and
    # the project does not build on the 3.x core.
    EspCoreVersion  = '2.0.16'
    EspCoreIndexUrl = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'

    # Version = known-good (installed when missing; newer stays with a warning).
    # Exact   = other versions break the build, so setup replaces a mismatch.
    # OnlyFor = only required when that build target is part of the run.
    Libraries = @(
        @{ Name = 'M5Unified';               Version = '0.2.7' }
        @{ Name = 'M5GFX';                   Version = '0.2.9' }
        @{ Name = 'ArduinoJson';             Version = '7.4.2' }
        @{ Name = 'Adafruit NeoPixel';       Version = '1.15.1' }
        @{ Name = 'GFX Library for Arduino'; Version = '1.6.0'; Exact = $true; OnlyFor = 'JC3248W535'
           Note = '1.6.1 breaks the AXS15231B panel of the JC3248W535' }
    )
}
