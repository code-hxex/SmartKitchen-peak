# Board COM-port auto-detection, shared by mon.ps1 and flash.ps1.
#
# Why this is not a one-liner: picking the first "Serial Port (USB)" row from
# `arduino-cli board list` is wrong on most real machines. Development PCs
# usually have another USB-UART bridge permanently attached (CP210x, CH340,
# FTDI), and it often enumerates on a LOWER COM number than the board, so the
# naive pick grabs the wrong device and the monitor silently shows nothing.
#
# The reliable key is the USB vendor ID:
#
#   303A  Espressif        <- ESP32-S3 USB-Serial/JTAG (XIAO ESP32S3)
#   2341  Arduino
#   1A86  QinHeng CH340
#   10C4  Silicon Labs CP210x
#   0403  FTDI
#
# Note that `arduino-cli board list` reports the XIAO as "Unknown" with an
# empty FQBN column even when the esp32 core is installed, so FQBN matching
# alone cannot find it. VID matching can.

function Find-BoardPort {
    [CmdletBinding()]
    param(
        # Print the candidate list when detection fails, so the user can see
        # which COM ports exist and pick one manually.
        [switch]$Explain
    )

    $ports = @()
    try {
        $ports = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction Stop |
            Where-Object { $_.FriendlyName -match '\((COM\d+)\)' } |
            ForEach-Object {
                [pscustomobject]@{
                    Port = ([regex]::Match($_.FriendlyName, '\((COM\d+)\)')).Groups[1].Value
                    Name = $_.FriendlyName
                    Vid  = ([regex]::Match($_.InstanceId, 'VID_([0-9A-Fa-f]{4})')).Groups[1].Value.ToUpper()
                }
            })
    } catch {
        # Get-PnpDevice is unavailable (older/limited hosts) - fall through to
        # the arduino-cli path below.
    }

    # 1st choice: a real Espressif or Arduino board.
    $board = $ports | Where-Object { $_.Vid -in @('303A', '2341') } | Select-Object -First 1
    if ($board) { return $board.Port }

    # 2nd choice: a port arduino-cli identified all the way down to an FQBN.
    $lines = @(& arduino-cli board list 2>$null)
    foreach ($line in $lines) {
        if ($line -match '^(COM\d+)\s+serial\s+.*\s+(esp32|arduino):\S+') { return $Matches[1] }
    }

    # 3rd choice: exactly one USB-UART bridge present, so it is unambiguous.
    $bridges = @($ports | Where-Object { $_.Vid -in @('1A86', '10C4', '0403') })
    if ($bridges.Count -eq 1) { return $bridges[0].Port }

    if ($Explain) {
        Write-Host "Could not identify the board automatically." -ForegroundColor Red
        if ($ports.Count) {
            Write-Host "COM ports present:" -ForegroundColor DarkGray
            foreach ($p in $ports) { Write-Host ("  {0,-6} VID_{1}  {2}" -f $p.Port, $p.Vid, $p.Name) }
            if ($bridges.Count -gt 1) {
                Write-Host "Several USB-UART devices are attached, so none can be assumed to be the board." -ForegroundColor DarkYellow
            }
        } else {
            Write-Host "  (no COM ports at all)" -ForegroundColor DarkGray
        }
        Write-Host "Check that the board is plugged in and powered, or pass -Port COM5 explicitly." -ForegroundColor DarkYellow
    }
    return ""
}
