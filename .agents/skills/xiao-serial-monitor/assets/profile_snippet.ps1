# ===== XIAO ESP32S3 serial monitor shortcuts =====
# Append to the PowerShell profile at $PROFILE so `mon` and `flash` work from
# any directory. Create the file if it does not exist; the parent folder may
# also need creating. Delete this block to uninstall.
#
# Point this at wherever the skill's scripts/ folder ended up.
$env:XIAO_TOOLS = "C:\path\to\skills\xiao-serial-monitor\scripts"

# Serial monitor:  mon        |  mon COM6  |  mon COM6 9600
function mon {
    param([string]$Port = "", [int]$Baud = 115200)
    & "$env:XIAO_TOOLS\mon.ps1" -Port $Port -Baud $Baud
}

# Compile + upload + monitor:  flash blink  |  flash blink -NoMonitor
function flash {
    param(
        [Parameter(Position = 0)][string]$Sketch = ".",
        [string]$Port = "",
        [int]$Baud = 115200,
        [switch]$NoMonitor
    )
    & "$env:XIAO_TOOLS\flash.ps1" -Sketch $Sketch -Port $Port -Baud $Baud -NoMonitor:$NoMonitor
}

# Attached boards
function boards { & arduino-cli board list }
