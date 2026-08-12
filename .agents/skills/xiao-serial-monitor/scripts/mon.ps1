<#
.SYNOPSIS
  Serial monitor for the XIAO ESP32S3 that survives board resets. Runs until
  Ctrl+C, or for a fixed number of seconds with -Seconds.

.DESCRIPTION
  Two modes, one script:

    mon.ps1                 interactive - runs until Ctrl+C. For the user's own
                            terminal, so they can watch a whole session.

    mon.ps1 -Seconds 10     bounded - reads for 10 s, prints, exits. Safe for an
                            agent to run while debugging, because it returns.

  The XIAO ESP32S3 talks over USB-Serial/JTAG, so the COM port belongs to the
  chip rather than to a separate UART bridge. Every reset or re-upload makes the
  port vanish and come back a moment later, and plain `arduino-cli monitor` just
  exits when that happens. Interactive mode waits and reattaches, so a single
  invocation covers an entire debugging session.

.PARAMETER Port
  COM port. Auto-detected by USB vendor ID when omitted.

.PARAMETER Baud
  Baud rate. Default 115200, matching Serial.begin(115200).

.PARAMETER Seconds
  Read for this many seconds and exit. Omit (or 0) for interactive mode.

.PARAMETER Send
  Bounded mode only: write this line to the board before reading, to exercise
  sketches that respond to serial input.

.EXAMPLE
  .\mon.ps1                          # interactive, hand this to the user
  .\mon.ps1 -Port COM6 -Baud 9600
  .\mon.ps1 -Seconds 10              # bounded, safe for an agent
  .\mon.ps1 -Seconds 10 -Send "ping"
#>
[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$Seconds = 0,
    [string]$Send = ""
)

. (Join-Path $PSScriptRoot "Find-BoardPort.ps1")

if (-not $Port) {
    $Port = Find-BoardPort -Explain
    if (-not $Port) { exit 1 }
    if ($Seconds -le 0) { Write-Host "Auto-detected port: $Port" -ForegroundColor DarkGray }
}

# ---------------------------------------------------------------- bounded mode
# Uses System.IO.Ports directly rather than `arduino-cli monitor`, which has no
# way to stop after a fixed time. Opening the port asserts DTR and resets the
# board, so output conveniently starts from a fresh boot banner.
if ($Seconds -gt 0) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
        Write-Output "ERROR: $Port not present. The board may be resetting, asleep, or on another port. Run: arduino-cli board list"
        exit 1
    }

    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.ReadTimeout = 1000
    $sp.NewLine = "`n"
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    try {
        $sp.Open()
    } catch {
        Write-Output "ERROR: could not open $Port ($($_.Exception.Message)). Another process is probably holding it - an interactive monitor, or the Arduino IDE."
        exit 1
    }

    if ($Send -ne "") {
        Start-Sleep -Seconds 4   # let the sketch clear its post-boot delay first
        $sp.WriteLine($Send)
    }

    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try { Write-Output $sp.ReadLine() } catch [TimeoutException] {}
    }
    $sp.Close()
    exit 0
}

# ------------------------------------------------------------ interactive mode
Write-Host "=== serial monitor  $Port @ $Baud ===" -ForegroundColor Cyan
Write-Host "Ctrl+C to exit" -ForegroundColor DarkGray
Write-Host ""

while ($true) {
    # The port is absent while the board resets or is being flashed. That is
    # normal, so wait for it rather than treating it as an error.
    $waited = 0
    while ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
        if ($waited -eq 0) { Write-Host "[waiting for $Port ...]" -ForegroundColor DarkYellow }
        Start-Sleep -Milliseconds 400
        $waited++
        if ($waited -gt 150) {   # 60 s
            Write-Host "$Port never came back. Exiting." -ForegroundColor Red
            exit 1
        }
    }

    & arduino-cli monitor -p $Port -c baudrate=$Baud

    # Reached only when the port dropped or the monitor died. Loop and reattach.
    Write-Host ""
    Write-Host "[disconnected - reattaching]" -ForegroundColor DarkYellow
    Start-Sleep -Milliseconds 800
}
