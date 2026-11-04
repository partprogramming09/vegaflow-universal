<#
.SYNOPSIS
    A/B intercalado (ABBA) del daemon corriendo en la sesion del escritorio, con las tareas VegaFlow-Start y VegaFlow-Stop.
.DESCRIPTION
    Como ab_loop.ps1, pero el daemon se arranca con la tarea programada interactiva y elevada VegaFlow-Start,
    que corre en la sesion 1 y puede ver el juego (el detector solo ve el escritorio de su sesion).
    Con el juego ya abierto en el escritorio, alterna capturas con el daemon apagado y encendido, en orden
    A,B / B,A / A,B ... Antes de cada captura anota el estado en ab_runs.log; tras cada parada anota si el
    daemon registro una sesion de juego, y aborta si el daemon encendido corre fuera de la sesion 1.
.PARAMETER Api
    Etiqueta de la API del juego abierto (d3d11 o d3d12); solo nombra las capturas.
#>
param(
    [Parameter(Mandatory)][string]$Api,
    [int]$Reps = 5,
    [int]$Seconds = 30,
    [string]$Dir = 'C:\ProgramData\VegaFlow',
    [string]$Process = 'drawstress.exe',
    [string]$LabelPrefix = 'task'
)

$ErrorActionPreference = 'Stop'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$runsLog = Join-Path $Dir 'ab_runs.log'
$sessionLog = Join-Path $Dir 'logs\session_reports.log'
$procName = [IO.Path]::GetFileNameWithoutExtension($Process)

# Anota una linea con marca de tiempo en ab_runs.log.
function Write-Run($msg) { Add-Content -Path $runsLog -Value ('{0} {1}' -f (Get-Date -Format o), $msg) }

# Devuelve los valores del informe de estado que no estan en fabrica, en una sola linea.
function Get-Drift {
    (& (Join-Path $Dir 'tweaks_state.ps1') 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '
}

# Captura una condicion: comprueba que el juego sigue vivo, anota el estado y mide con PresentMon.
function Invoke-Capture($cond, $i) {
    if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) { throw "$Process ya no esta corriendo." }
    $label = "$LabelPrefix-$cond-$Api-$i"
    $d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
    Write-Run ("captura label=$label daemon=$(if ($d) { "sesion$($d.SessionId)" } else { 'apagado' }) desviado=[$(Get-Drift)]")
    & $bench -Process $Process -Label $label -Seconds $Seconds -WarmupSeconds 5 | Out-Null
}

# Lanza la tarea VegaFlow-Start y espera a que el daemon corra en la sesion 1 y aplique sus optimizaciones.
function Start-Daemon {
    Start-ScheduledTask -TaskName 'VegaFlow-Start'
    $deadline = (Get-Date).AddSeconds(40)
    while (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    $d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $d) { throw 'El daemon no arranco con VegaFlow-Start.' }
    if ($d.SessionId -eq 0) { throw 'El daemon corre en la sesion 0: no vera el juego.' }
    Start-Sleep -Seconds 10
}

# Espera a que las tareas VegaFlow-Start y VegaFlow-Stop dejen de correr; devuelve falso si pasa el limite.
function Wait-TasksIdle($seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $busy = @('VegaFlow-Start', 'VegaFlow-Stop') | Where-Object { (Get-ScheduledTask -TaskName $_).State -eq 'Running' }
        if (-not $busy) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Lanza la tarea VegaFlow-Stop, espera a que terminen las tareas (el cierre lanza un proceso breve --apu-balanced) y anota si registro una sesion de juego.
function Stop-Daemon {
    $before = if (Test-Path $sessionLog) { (Get-Item $sessionLog).Length } else { 0 }
    Start-ScheduledTask -TaskName 'VegaFlow-Stop'
    Start-Sleep -Seconds 2
    if (-not (Wait-TasksIdle 90)) { throw 'Las tareas de arranque y parada siguen corriendo tras 90 s.' }
    if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { throw 'El daemon sigue corriendo tras VegaFlow-Stop.' }
    $after = if (Test-Path $sessionLog) { (Get-Item $sessionLog).Length } else { 0 }
    Write-Run ("stop-daemon sesion_registrada=$($after -gt $before)")
    Start-Sleep -Seconds 5
}

Write-Run "inicio api=$Api reps=$Reps seconds=$Seconds prefijo=$LabelPrefix"
try {
    for ($i = 1; $i -le $Reps; $i++) {
        $order = if ($i % 2 -eq 1) { @('off', 'on') } else { @('on', 'off') }
        foreach ($cond in $order) {
            if ($cond -eq 'on') { Start-Daemon }
            Invoke-Capture $cond $i
            if ($cond -eq 'on') { Stop-Daemon }
        }
    }
} finally {
    if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { Start-ScheduledTask -TaskName 'VegaFlow-Stop'; Start-Sleep -Seconds 15 }
    Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Run 'fin'
}

Get-ChildItem (Join-Path (Split-Path $PSScriptRoot) 'bench\results') -Filter "$LabelPrefix-*-$Api-*.json" |
    Sort-Object LastWriteTime | ForEach-Object {
        $r = Get-Content $_.FullName -Raw | ConvertFrom-Json
        '{0},{1},{2},{3},{4},{5},{6}' -f $r.label, $r.frames, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $r.present_mode
    }
