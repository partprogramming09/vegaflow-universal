<#
.SYNOPSIS
    Closes the test-build GUI opened by the gui job. It only matches C:\dev\vf-run\build\vegaflow_daemon.exe, never the installed daemon.
#>
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
$p = @(Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
$p | Stop-Process -Force
"gui cerradas=$($p.Count)"
