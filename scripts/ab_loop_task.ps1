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
.PARAMETER Sensors
    1 muestrea cada 5 s la temperatura de la zona termica ACPI y el rendimiento de la CPU durante cada captura y
    anade al final una fila sens por captura (la potencia no se mide: el equipo no expone contadores de potencia).
#>
param(
    [Parameter(Mandatory)][string]$Api,
    [int]$Reps = 5,
    [int]$Seconds = 30,
    [string]$Dir = 'C:\ProgramData\VegaFlow',
    [string]$Process = 'drawstress.exe',
    [string]$LabelPrefix = 'task',
    [ValidateSet('none', 'chrome')][string]$Load = 'none',
    [ValidateRange(1, 12)][int]$LoadWorkers = 6,
    [ValidateSet('0', '1')][string]$Sensors = '0',
    [ValidateSet('0', '1')][string]$Warmup = '0'
)

$ErrorActionPreference = 'Stop'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$runsLog = Join-Path $Dir 'ab_runs.log'
$sessionLog = Join-Path $Dir 'logs\session_reports.log'
$procName = [IO.Path]::GetFileNameWithoutExtension($Process)
$sensRows = [System.Collections.Generic.List[string]]::new()

# Anota una linea con marca de tiempo en ab_runs.log.
function Write-Run($msg) { Add-Content -Path $runsLog -Value ('{0} {1}' -f (Get-Date -Format o), $msg) }

# Devuelve los valores del informe de estado que no estan en fabrica, en una sola linea.
function Get-Drift {
    (& (Join-Path $Dir 'tweaks_state.ps1') 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '
}

# Carpeta del perfil de Chrome y de busy.html: el area del usuario, no el TEMP del ejecutor (con el TEMP protegido de WinBridge Chrome no arranca).
$loadTemp = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'

# Arranca la carga de fondo (Chrome sin ventana quemando CPU con 6 workers) y espera a que se asiente.
function Start-Load {
    if ($Load -ne 'chrome') { return }
    $page = Join-Path $loadTemp 'vf-busy.html'
    Copy-Item (Join-Path $Dir 'tools\busy.html') $page -Force
    Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=$(Join-Path $loadTemp 'vf-chrome-load')", "file:///$($page -replace '\\', '/')?n=$LoadWorkers" | Out-Null
    Start-Sleep -Seconds 8
}

# Cierra solo los procesos de Chrome que arranco Start-Load (los identifica por su carpeta de perfil).
function Stop-Load {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" | Where-Object { $_.CommandLine -match 'vf-chrome-load' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

# Captura una condicion: comprueba que el juego sigue vivo, anota el estado y mide con PresentMon, con la carga de fondo si se pidio.
function Invoke-Capture($cond, $i) {
    if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) { throw "$Process ya no esta corriendo." }
    $label = "$LabelPrefix-$cond-$Api-$i"
    $d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
    Write-Run ("captura label=$label load=$Load daemon=$(if ($d) { "sesion$($d.SessionId)" } else { 'apagado' }) desviado=[$(Get-Drift)]")
    Start-Load
    $sampler = $null
    if ($Sensors -eq '1') {
        $sampler = Start-Job {
            while ($true) {
                $t = Get-CimInstance Win32_PerfFormattedData_Counters_ThermalZoneInformation | Select-Object -First 1
                $p = Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation -Filter "Name='_Total'"
                [pscustomobject]@{ temp = $t.HighPrecisionTemperature / 10 - 273.15; perf = [double]$p.PercentProcessorPerformance; mhz = [double]$p.ProcessorFrequency }
                Start-Sleep -Seconds 5
            }
        }
    }
    try { & $bench -Process $Process -Label $label -Seconds $Seconds -WarmupSeconds 5 | Out-Null } finally { Stop-Load }
    if ($sampler) {
        Stop-Job $sampler
        $samples = @(Receive-Job $sampler)
        Remove-Job $sampler -Force
        if ($samples.Count -gt 0) {
            $eff = ($samples | ForEach-Object { $_.mhz * $_.perf / 100 } | Measure-Object -Average).Average
            $sensRows.Add(('sens,{0},n={1},temp_first={2:F1},temp_avg={3:F1},temp_max={4:F1},temp_last={5:F1},perf_avg={6:F0},perf_min={7:F0},mhz_eff_avg={8:F0}' -f `
                $label, $samples.Count, $samples[0].temp, ($samples | Measure-Object temp -Average).Average, ($samples | Measure-Object temp -Maximum).Maximum, `
                $samples[-1].temp, ($samples | Measure-Object perf -Average).Average, ($samples | Measure-Object perf -Minimum).Minimum, $eff))
            # Media de temperatura y de rendimiento por minuto (12 muestras de 5 s), para ver la tendencia en el tiempo.
            $minutes = for ($m = 0; $m * 12 -lt $samples.Count; $m++) {
                $b = $samples[($m * 12)..([math]::Min($samples.Count, ($m + 1) * 12) - 1)]
                '{0}:{1:F1}C/{2:F0}%' -f ($m + 1), ($b | Measure-Object temp -Average).Average, ($b | Measure-Object perf -Average).Average
            }
            $sensRows.Add("sensmin,$label,$($minutes -join ' ')")
        }
    }
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

# Si el juego no esta abierto y es drawstress, lo abre con game.ps1 (el ejecutor remoto lo instala junto a este script).
if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) {
    $game = Join-Path $PSScriptRoot 'game.ps1'
    if ($procName -ne 'drawstress' -or -not (Test-Path $game)) { throw "$Process no esta abierto." }
    & $game -Api $Api | Out-Null
}
Write-Run "inicio api=$Api reps=$Reps seconds=$Seconds prefijo=$LabelPrefix warmup=$Warmup"
try {
    # Captura de calentamiento sin daemon: no cuenta (su etiqueta no coincide con el patron del resumen final).
    if ($Warmup -eq '1') {
        Start-Load
        try { & $bench -Process $Process -Label "$LabelPrefix-warmup" -Seconds $Seconds -WarmupSeconds 5 | Out-Null } finally { Stop-Load }
    }
    for ($i = 1; $i -le $Reps; $i++) {
        $order = if ($i % 2 -eq 1) { @('off', 'on') } else { @('on', 'off') }
        foreach ($cond in $order) {
            if ($cond -eq 'on') { Start-Daemon }
            Invoke-Capture $cond $i
            if ($cond -eq 'on') { Stop-Daemon }
        }
    }
} finally {
    Stop-Load
    if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { Start-ScheduledTask -TaskName 'VegaFlow-Stop'; Start-Sleep -Seconds 15 }
    if ($procName -eq 'drawstress') { Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force }
    Write-Run 'fin'
}

Get-ChildItem (Join-Path (Split-Path $PSScriptRoot) 'bench\results') -Filter "$LabelPrefix-*-$Api-*.json" |
    Sort-Object LastWriteTime | ForEach-Object {
        $r = Get-Content $_.FullName -Raw | ConvertFrom-Json
        '{0},{1},{2},{3},{4},{5},{6}' -f $r.label, $r.frames, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $r.present_mode
    }
$sensRows
