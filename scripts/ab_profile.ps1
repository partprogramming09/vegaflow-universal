<#
.SYNOPSIS
    Starts or stops the VegaFlow daemon as the "applied" condition of the A/B tests.
.DESCRIPTION
    -Start : launches vegaflow_daemon.exe hidden from C:\ProgramData\VegaFlow, saves its PID and keeps
             waiting until the daemon exits (the task stays "Running" while the daemon is alive).
    -Stop  : requests a clean shutdown with the Global\VegaFlowStop event (the daemon runs RestoreAll),
             waits up to 20 s, restores the APU power profile and leaves the state in ab.log.
    If the daemon does not exit within 20 s, -Stop fails and does not kill it: killing the process would skip RestoreAll.
    Requires administrator. By default it uses the protected copy in C:\ProgramData\VegaFlow; -Dir allows
    another folder with vegaflow_daemon.exe, config\config.json, logs\ and tweaks_state.ps1 (elevated SSH session, no tasks).
    The daemon only reads config\config.json when only administrators can write that folder; otherwise it uses the
    factory values and warns only on its (hidden) console.
#>
param(
    [switch]$Start,
    [switch]$Stop,
    [string]$Dir = 'C:\ProgramData\VegaFlow'
)

$ErrorActionPreference = 'Stop'
if ($Start -eq $Stop) { throw 'Indica exactamente uno: -Start o -Stop.' }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Requiere administrador.' }

$dir = $Dir
$exe = Join-Path $dir 'vegaflow_daemon.exe'
$pidFile = Join-Path $dir 'ab.pid'
$log = Join-Path $dir 'ab.log'

# Writes a timestamped line to ab.log and to the output.
function Write-Ab($msg) {
    $line = '{0} {1}' -f (Get-Date -Format o), $msg
    Add-Content -Path $log -Value $line
    $line
}

# Returns the daemon process saved in ab.pid, or $null if it is not running.
function Get-AbDaemon {
    if (-not (Test-Path $pidFile)) { return $null }
    $id = [int](Get-Content $pidFile -Raw)
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq 'vegaflow_daemon') { return $p }
    return $null
}

if ($Start) {
    if (Get-AbDaemon) { throw 'El daemon ya esta corriendo (ab.pid).' }
    if (-not (Test-Path $exe)) { throw "No existe $exe; ejecuta setup_tasks.ps1." }
    $p = Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle Hidden -PassThru
    Set-Content -Path $pidFile -Value $p.Id
    Start-Sleep -Seconds 3
    if ($p.HasExited) { throw "El daemon salio al arrancar (codigo $($p.ExitCode))." }
    Write-Ab "Start pid=$($p.Id) sha256=$((Get-FileHash $exe -Algorithm SHA256).Hash)"
    $p.WaitForExit()
    Write-Ab "Start: el daemon salio con codigo $($p.ExitCode)"
    return
}

$p = Get-AbDaemon
if (-not $p) {
    Write-Ab 'Stop: el daemon no corria.'
} else {
    $ev = [Threading.EventWaitHandle]::OpenExisting('Global\VegaFlowStop')
    [void]$ev.Set()
    if (-not $p.WaitForExit(20000)) { Write-Ab "Stop FALLO: pid=$($p.Id) sigue vivo tras 20 s"; throw 'El daemon no cerro limpio.' }
    Write-Ab "Stop limpio pid=$($p.Id) codigo=$($p.ExitCode)"
}
Remove-Item $pidFile -ErrorAction SilentlyContinue

& $exe --apu-balanced 2>&1 | ForEach-Object { Add-Content -Path $log -Value "apu-balanced: $_" }
Write-Ab 'Perfil de energia APU restaurado.'
& (Join-Path $dir 'tweaks_state.ps1') 2>&1 | ForEach-Object { Add-Content -Path $log -Value "estado: $_" }
