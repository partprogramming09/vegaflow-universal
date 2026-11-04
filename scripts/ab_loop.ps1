<#
.SYNOPSIS
    A/B intercalado (ABBA) de un juego ya abierto: daemon de VegaFlow apagado contra encendido.
.DESCRIPTION
    Con drawstress.exe corriendo en el escritorio, repite -Reps veces: una captura con el daemon apagado
    y otra con el daemon encendido, alternando el orden en cada repeticion (A,B / B,A / A,B ...).
    Antes de cada captura anota en ab_runs.log si el daemon corre y los valores fuera de fabrica.
    Despues de parar el daemon anota si registro una sesion de juego (session_reports.log).
    Al terminar imprime una tabla con una fila por captura.
.PARAMETER Api
    Etiqueta de la API del juego abierto (d3d11 o d3d12); solo nombra las capturas.
#>
param(
    [Parameter(Mandatory)][string]$Api,
    [int]$Reps = 5,
    [int]$Seconds = 30,
    [string]$WorkDir = 'C:\dev\vf-ab',
    [string]$Process = 'drawstress.exe'
)

$ErrorActionPreference = 'Stop'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$ab = Join-Path $WorkDir 'ab_profile.ps1'
$runsLog = Join-Path $WorkDir 'ab_runs.log'
$sessionLog = Join-Path $WorkDir 'logs\session_reports.log'
$procName = [IO.Path]::GetFileNameWithoutExtension($Process)

# Anota una linea con marca de tiempo en ab_runs.log.
function Write-Run($msg) { Add-Content -Path $runsLog -Value ('{0} {1}' -f (Get-Date -Format o), $msg) }

# Devuelve los valores del informe de estado que no estan en fabrica, en una sola linea.
function Get-Drift {
    (& (Join-Path $WorkDir 'tweaks_state.ps1') 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '
}

# Captura una condicion: comprueba que el juego sigue vivo, anota el estado y mide con PresentMon.
function Invoke-Capture($cond, $i) {
    if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) { throw "$Process ya no esta corriendo." }
    $label = "$cond-$Api-$i"
    $daemon = (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Run ("captura label=$label daemon=$daemon desviado=[$(Get-Drift)]")
    & $bench -Process $Process -Label $label -Seconds $Seconds -WarmupSeconds 5 | Out-Null
}

# Arranca el daemon en segundo plano y espera a que aplique sus optimizaciones al juego.
function Start-Daemon {
    Start-Process powershell -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$ab`" -Start -Dir `"$WorkDir`""
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue)) { throw 'El daemon no arranco.' }
    Start-Sleep -Seconds 10
}

# Para el daemon con cierre limpio y anota si registro una sesion de juego.
function Stop-Daemon {
    $before = if (Test-Path $sessionLog) { (Get-Item $sessionLog).Length } else { 0 }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $ab -Stop -Dir $WorkDir | Out-Null
    $after = if (Test-Path $sessionLog) { (Get-Item $sessionLog).Length } else { 0 }
    Write-Run ("stop-daemon sesion_registrada=$($after -gt $before)")
    Start-Sleep -Seconds 5
}

Write-Run "inicio api=$Api reps=$Reps seconds=$Seconds"
for ($i = 1; $i -le $Reps; $i++) {
    $order = if ($i % 2 -eq 1) { @('off', 'on') } else { @('on', 'off') }
    foreach ($cond in $order) {
        if ($cond -eq 'on') { Start-Daemon }
        Invoke-Capture $cond $i
        if ($cond -eq 'on') { Stop-Daemon }
    }
}
Write-Run 'fin'
Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force

Get-ChildItem (Join-Path (Split-Path $PSScriptRoot) 'bench\results') -Filter "*-$Api-*.json" |
    Sort-Object LastWriteTime | ForEach-Object {
        $r = Get-Content $_.FullName -Raw | ConvertFrom-Json
        '{0},{1},{2},{3},{4},{5},{6}' -f $r.label, $r.frames, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $r.present_mode
    }
