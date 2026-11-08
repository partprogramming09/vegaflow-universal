<#
.SYNOPSIS
    Starts the VegaFlow daemon with the VegaFlow-Start task (desktop session, elevated) and waits for it to run.
#>
$ErrorActionPreference = 'Stop'
Start-ScheduledTask -TaskName 'VegaFlow-Start'
$deadline = (Get-Date).AddSeconds(40)
while (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
$d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $d) { throw 'El daemon no arranco con VegaFlow-Start.' }
'daemon pid={0} sesion={1}' -f $d.Id, $d.SessionId
