# This script assumes that the Pico is already in bootloader mode
$SourceDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir = Join-Path $SourceDir "build"

# Create the build directory if it doesn't exist
if (-not (Test-Path -Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Change to the build directory
Set-Location -Path $BuildDir

# Run cmake configure
cmake .. -G "Ninja"

# Build the project
cmake --build .

# Return to the source directory
Set-Location -Path $SourceDir

# Flash the firmware to the Pico with picotool
$FirmwarePath = Join-Path $BuildDir "pico-2-dormant-mode.elf"
if (Test-Path -Path $FirmwarePath) {
    Write-Host "Flashing firmware to the Pico..."
    picotool load -f $FirmwarePath
    picotool reboot
} else {
    Write-Host "Firmware file not found: $FirmwarePath"
}
