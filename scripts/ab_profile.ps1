<#
.SYNOPSIS
    Arranca o detiene el daemon de VegaFlow como condicion "aplicado" de las pruebas A/B.
.DESCRIPTION
    -Start : lanza vegaflow_daemon.exe oculto desde C:\ProgramData\VegaFlow, guarda su PID y se queda
             esperando hasta que el daemon sale (la tarea queda "Running" mientras haya daemon).
    -Stop  : pide un cierre limpio con el evento Global\VegaFlowStop (el daemon ejecuta RestoreAll),
             espera hasta 20 s, restaura el perfil de energia APU y deja el estado en ab.log.
    Si el daemon no sale en 20 s, -Stop falla y no lo mata: matar el proceso saltaria RestoreAll.
    Requiere administrador. Se ejecuta desde la copia protegida de C:\ProgramData\VegaFlow.
#>
param(
    [switch]$Start,
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
if ($Start -eq $Stop) { throw 'Indica exactamente uno: -Start o -Stop.' }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Requiere administrador.' }

$dir = 'C:\ProgramData\VegaFlow'
$exe = Join-Path $dir 'vegaflow_daemon.exe'
$pidFile = Join-Path $dir 'ab.pid'
$log = Join-Path $dir 'ab.log'

# Escribe una linea con marca de tiempo en ab.log y en la salida.
function Write-Ab($msg) {
    $line = '{0} {1}' -f (Get-Date -Format o), $msg
    Add-Content -Path $log -Value $line
    $line
}

# Devuelve el proceso del daemon guardado en ab.pid, o $null si no corre.
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
