<#
.SYNOPSIS
    Reads the memory of the test-build GUI process (C:\dev\vf-run\build\vegaflow_daemon.exe, opened by the gui job):
    working set and private working set, in MB. Read-only; it presses nothing and touches no other program.
.PARAMETER Wait
    Seconds to wait before reading (lets the GUI settle after it opens). Default 0.
#>
param([ValidateRange(0, 600)][int]$Wait = 0)
$ErrorActionPreference = 'Stop'
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
if ($Wait -gt 0) { Start-Sleep -Seconds $Wait }
$p = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
if (-not $p) { throw 'No hay GUI de prueba abierta: corre vfdev run gui antes.' }
$ws = [math]::Round($p.WorkingSet64 / 1MB, 2)
$perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($p.Id)"
$priv = [math]::Round($perf.WorkingSetPrivate / 1MB, 2)
'gui pid={0} workingset={1} MB privado={2} MB ventana="{3}"' -f $p.Id, $ws, $priv, $p.MainWindowTitle
