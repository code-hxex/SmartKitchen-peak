# Watch a COM port appear/disappear WITHOUT opening it (opening resets the
# board). Use to verify deep-sleep cycles: the port vanishes while asleep and
# reappears for ~3 s on each wake.
param(
    [string]$Port = "COM4",
    [int]$Seconds = 55
)

$deadline = (Get-Date).AddSeconds($Seconds)
$last = $null
while ((Get-Date) -lt $deadline) {
    $present = [System.IO.Ports.SerialPort]::GetPortNames() -contains $Port
    if ($present -ne $last) {
        $state = if ($present) { "APPEARED (awake)" } else { "GONE (sleeping)" }
        Write-Output ("{0:HH:mm:ss.f}  {1} {2}" -f (Get-Date), $Port, $state)
        $last = $present
    }
    Start-Sleep -Milliseconds 200
}
