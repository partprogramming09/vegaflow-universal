<#
.SYNOPSIS
    Stops the VegaFlow daemon with a clean shutdown (VegaFlow-Stop task) and waits for it to exit.
#>
$ErrorActionPreference = 'Stop'
Start-ScheduledTask -TaskName 'VegaFlow-Stop'
$deadline = (Get-Date).AddSeconds(40)
while ((Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { throw 'El daemon no salio tras VegaFlow-Stop.' }
'daemon parado'
