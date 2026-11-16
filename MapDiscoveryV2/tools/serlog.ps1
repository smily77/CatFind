# Liest eine serielle Schnittstelle mit, ohne das Board per DTR/RTS zurueckzusetzen.
# Aufruf: powershell -File serlog.ps1 COM18 20   (Port, Sekunden)
param([string]$Port = "COM18", [int]$Seconds = 20)
$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.DtrEnable = $false; $p.RtsEnable = $false
$p.Open()
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) { $s = $p.ReadExisting(); if ($s) { [Console]::Write($s) }; Start-Sleep -Milliseconds 100 }
$p.Close()
