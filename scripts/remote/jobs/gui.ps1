<#
.SYNOPSIS
    Opens the GUI of the test build (C:\dev\vf-run\build, the one `vfdev cross-test` sends) on the desktop (session 1).
.DESCRIPTION
    Runs `--gui --observe` (no automatic changes, no restore-point dialog: the desktop has the person's own programs,
    and a plain --gui would treat any 3D window, e.g. SCSKiller, as a game and tune it). --dry-run would print the simulation and exit, never opening the window.
    The GUI needs administrator
    and relaunches itself through UAC otherwise, which the screenshot job cannot answer: the job refuses to start when
    this session is not elevated. Buttons still act if pressed: the keys job never presses them. It never touches the installed daemon in C:\ProgramData\VegaFlow.
#>
$ErrorActionPreference = 'Stop'
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
if (-not (Test-Path $exe)) { throw "No existe ${exe}: corre vfdev cross-test antes." }
Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
# kill-gui ends the process abruptly, so the journal of this test folder keeps an open session and the next start would
# ask whether to restore it. Nothing is applied in --observe, so set the file aside (kept, not deleted) in this folder only.
$journal = Join-Path (Split-Path $exe) 'config\change_journal.vfj'
if (Test-Path $journal) { Move-Item $journal ($journal + '.stale-' + (Get-Date -Format 'yyyyMMddHHmmss')) -Force }
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Esta sesion no es de administrador: la GUI pediria UAC y nadie puede contestarlo.' }
$p = Start-Process -FilePath $exe -ArgumentList '--gui', '--observe' -WorkingDirectory (Split-Path (Split-Path $exe)) -PassThru
Start-Sleep -Seconds 6
$g = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if (-not $g) { throw 'La GUI salio al arrancar.' }
'gui pid={0} sesion={1} ventana="{2}"' -f $g.Id, $g.SessionId, $g.MainWindowTitle
