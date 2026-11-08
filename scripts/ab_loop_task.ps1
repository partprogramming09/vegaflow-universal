<#
.SYNOPSIS
    Interleaved A/B (ABBA) of the daemon running in the desktop session, using the VegaFlow-Start and VegaFlow-Stop tasks.
.DESCRIPTION
    Like ab_loop.ps1, but the daemon is started by the interactive, elevated scheduled task VegaFlow-Start,
    which runs in session 1 and can see the game (the detector only sees its own session's desktop).
    With the game already open on the desktop, it alternates captures with the daemon off and on, in the order
    A,B / B,A / A,B ... Before each capture it logs the state to ab_runs.log; after each stop it logs whether the
    daemon recorded a game session, and aborts if the running daemon is outside session 1.
.PARAMETER Api
    Label of the open game's API (d3d11 or d3d12); only used to name the captures.
.PARAMETER Sensors
    1 samples the ACPI thermal-zone temperature and CPU performance every 5 s during each capture and
    appends one sens row per capture at the end (power is not measured: the machine exposes no power counters).
#>
param(
    [Parameter(Mandatory)][string]$Api,
    [int]$Reps = 5,
    [int]$Seconds = 30,
    [string]$Dir = 'C:\ProgramData\VegaFlow',
    [string]$Process = 'drawstress.exe',
    [string]$LabelPrefix = 'task',
    [ValidateSet('none', 'chrome', 'mix')][string]$Load = 'none',
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

# Appends a timestamped line to ab_runs.log.
function Write-Run($msg) { Add-Content -Path $runsLog -Value ('{0} {1}' -f (Get-Date -Format o), $msg) }

# Returns the status-report values that differ from stock, on a single line.
function Get-Drift {
    (& (Join-Path $Dir 'tweaks_state.ps1') 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '
}

# Chrome profile folder and busy.html: the user area, not the runner's TEMP (Chrome does not start with WinBridge's protected TEMP).
$loadTemp = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'

# Starts the background load and waits for it to settle. 'chrome': windowless Chrome burning CPU with LoadWorkers workers.
# 'mix': 3 Chrome tabs at a 40% duty cycle (bursts) and 2 processes that endlessly hash system files (like a launcher verifying files).
function Start-Load {
    if ($Load -eq 'none') { return }
    $page = Join-Path $loadTemp 'vf-busy.html'
    Copy-Item (Join-Path $Dir 'tools\busy.html') $page -Force
    $query = if ($Load -eq 'mix') { 'n=3&duty=40' } else { "n=$LoadWorkers" }
    Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=$(Join-Path $loadTemp 'vf-chrome-load')", "file:///$($page -replace '\\', '/')?$query" | Out-Null
    if ($Load -eq 'mix') {
        1..2 | ForEach-Object {
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', '$vfhashloop = 1; while ($true) { Get-ChildItem C:\Windows\System32 -Filter *.dll -File | Select-Object -First 300 | Get-FileHash -Algorithm SHA256 | Out-Null }' | Out-Null
        }
    }
    Start-Sleep -Seconds 8
}

# Closes only the processes Start-Load started: Chrome ones by their profile folder and hashing ones by their marker.
function Stop-Load {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" | Where-Object { $_.CommandLine -match 'vf-chrome-load' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -match 'vfhashloop' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

# Captures one condition: checks the game is still alive, logs the state and measures with PresentMon, with the background load if requested.
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
            # Per-minute average of temperature and performance (12 samples of 5 s), to see the trend over time.
            $minutes = for ($m = 0; $m * 12 -lt $samples.Count; $m++) {
                $b = $samples[($m * 12)..([math]::Min($samples.Count, ($m + 1) * 12) - 1)]
                '{0}:{1:F1}C/{2:F0}%' -f ($m + 1), ($b | Measure-Object temp -Average).Average, ($b | Measure-Object perf -Average).Average
            }
            $sensRows.Add("sensmin,$label,$($minutes -join ' ')")
        }
    }
}

# Launches the VegaFlow-Start task and waits for the daemon to run in session 1 and apply its optimizations.
function Start-Daemon {
    Start-ScheduledTask -TaskName 'VegaFlow-Start'
    $deadline = (Get-Date).AddSeconds(40)
    while (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    $d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $d) { throw 'El daemon no arranco con VegaFlow-Start.' }
    if ($d.SessionId -eq 0) { throw 'El daemon corre en la sesion 0: no vera el juego.' }
    Start-Sleep -Seconds 10
}

# Waits for the VegaFlow-Start and VegaFlow-Stop tasks to stop running; returns false if the limit is exceeded.
function Wait-TasksIdle($seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $busy = @('VegaFlow-Start', 'VegaFlow-Stop') | Where-Object { (Get-ScheduledTask -TaskName $_).State -eq 'Running' }
        if (-not $busy) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Launches the VegaFlow-Stop task, waits for the tasks to finish (shutdown launches a brief --apu-balanced process) and logs whether it recorded a game session.
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

# If the game is not open and it is drawstress, opens it with game.ps1 (the remote runner installs it next to this script).
if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) {
    $game = Join-Path $PSScriptRoot 'game.ps1'
    if ($procName -ne 'drawstress' -or -not (Test-Path $game)) { throw "$Process no esta abierto." }
    & $game -Api $Api | Out-Null
}
Write-Run "inicio api=$Api reps=$Reps seconds=$Seconds prefijo=$LabelPrefix warmup=$Warmup"
try {
    # Warm-up capture without the daemon: it does not count (its label does not match the final summary pattern).
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
