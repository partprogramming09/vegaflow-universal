<#
.SYNOPSIS
    Starts a SECOND copy of the test-build GUI while the first (job gui) is open, and reports whether the second one left and
    whether the first one is now the foreground window. Checks the "already running, bring it forward" message.
#>
$ErrorActionPreference = 'Stop'
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
$first = @(Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
if ($first.Count -ne 1) { throw "Hace falta exactamente una GUI abierta (job gui); hay $($first.Count)." }
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class VfFg { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid); [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c); }'
# Minimise the first window so "brought forward" is observable.
[VfFg]::ShowWindow($first[0].MainWindowHandle, 6) | Out-Null
Start-Sleep -Seconds 1
$p = Start-Process -FilePath $exe -ArgumentList '--gui', '--observe' -WorkingDirectory (Split-Path (Split-Path $exe)) -PassThru
$null = $p.WaitForExit(15000)
Start-Sleep -Seconds 1
$fg = 0; [void][VfFg]::GetWindowThreadProcessId([VfFg]::GetForegroundWindow(), [ref]$fg)
$still = @(Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
'segunda salio={0} (codigo {1}); GUIs abiertas={2}; ventana al frente es la primera={3}' -f $p.HasExited, $(if ($p.HasExited) { $p.ExitCode } else { 'n/a' }), $still.Count, ($fg -eq $first[0].Id)
