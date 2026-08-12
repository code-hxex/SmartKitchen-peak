# Read serial output from the XIAO ESP32S3 for a bounded time, optionally
# sending one line first. Opening the port resets the board (DTR), so output
# always starts from a fresh boot.
param(
    [string]$Port = "COM4",
    [int]$Seconds = 10,
    [string]$Send = "",
    [int]$Baud = 115200
)

if (-not ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port)) {
    Write-Output "ERROR: $Port not present. Board may be in deep sleep (wait for wake) or on another port. Run: arduino-cli board list"
    exit 1
}

$p = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$p.ReadTimeout = 1000
$p.NewLine = "`n"
$p.DtrEnable = $true
$p.RtsEnable = $true
try {
    $p.Open()
} catch {
    Write-Output "ERROR: could not open $Port ($($_.Exception.Message)). Another process may be holding it."
    exit 1
}

if ($Send -ne "") {
    Start-Sleep -Seconds 4   # let the sketch finish its post-boot delay first
    $p.WriteLine($Send)
}

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    try { Write-Output $p.ReadLine() } catch [TimeoutException] {}
}
$p.Close()
