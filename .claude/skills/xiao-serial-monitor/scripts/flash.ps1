<#
.SYNOPSIS
  Compile, upload, then attach the serial monitor - one command.

.DESCRIPTION
  Collapses the loop a human repeats dozens of times per session. Attaching the
  monitor immediately after upload is the point: the board reboots as soon as
  flashing finishes, so this is the only way to reliably catch the boot banner
  without racing it by hand.

  Like mon.ps1 this ends up interactive, so an agent should hand the command to
  the user rather than run it. Use -NoMonitor for an agent-safe upload.

  Exit the monitor with Ctrl+C.

.PARAMETER Sketch
  Sketch folder name or path. A bare name is resolved against the repository
  root (the parent of this script's folder). Defaults to the current folder.

.PARAMETER Port
  COM port. Auto-detected when omitted.

.PARAMETER Baud
  Monitor baud rate. Default 115200.

.PARAMETER NoMonitor
  Upload only, then exit. Use this when a script or agent needs the exit code.

.PARAMETER Fqbn
  Board FQBN. Defaults to the XIAO ESP32S3.

.EXAMPLE
  .\flash.ps1 blink
  .\flash.ps1 blink -Port COM6
  .\flash.ps1 blink -NoMonitor
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Sketch = ".",
    [string]$Port = "",
    [int]$Baud = 115200,
    [switch]$NoMonitor,
    [string]$Fqbn = "esp32:esp32:XIAO_ESP32S3"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot "Find-BoardPort.ps1")

if (-not (Test-Path $Sketch)) {
    $candidate = Join-Path $root $Sketch
    if (Test-Path $candidate) {
        $Sketch = $candidate
    } else {
        Write-Host "No such sketch: $Sketch" -ForegroundColor Red
        Write-Host "Available sketches:" -ForegroundColor DarkGray
        Get-ChildItem $root -Directory |
            Where-Object { Get-ChildItem $_.FullName -Filter *.ino -ErrorAction SilentlyContinue } |
            ForEach-Object { Write-Host "  $($_.Name)" }
        exit 1
    }
}
$Sketch = (Resolve-Path $Sketch).Path

if (-not $Port) {
    $Port = Find-BoardPort -Explain
    if (-not $Port) { exit 1 }
}

Write-Host "=== $([System.IO.Path]::GetFileName($Sketch))  ->  $Port ===" -ForegroundColor Cyan

& arduino-cli compile --fqbn $Fqbn -u -p $Port $Sketch
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload failed (exit $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "If a monitor is open it owns the port and upload cannot succeed. Close it with Ctrl+C and retry." -ForegroundColor DarkYellow
    exit $LASTEXITCODE
}

Write-Host "Upload complete" -ForegroundColor Green

if (-not $NoMonitor) {
    Write-Host ""
    & (Join-Path $PSScriptRoot "mon.ps1") -Port $Port -Baud $Baud
}
