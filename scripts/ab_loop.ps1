<#
.SYNOPSIS
    Interleaved A/B (ABBA) of an already-open game: VegaFlow daemon off versus on.
.DESCRIPTION
    With drawstress.exe running on the desktop, repeats -Reps times: one capture with the daemon off
    and another with the daemon on, alternating the order on each repetition (A,B / B,A / A,B ...).
    Before each capture it logs to ab_runs.log whether the daemon is running and the non-stock values.
    After stopping the daemon it logs whether it recorded a game session (session_reports.log).
    When done it prints a table with one row per capture.
.PARAMETER Api
    Label of the open game's API (d3d11 or d3d12); only used to name the captures.
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

# Appends a timestamped line to ab_runs.log.
function Write-Run($msg) { Add-Content -Path $runsLog -Value ('{0} {1}' -f (Get-Date -Format o), $msg) }

# Returns the status-report values that differ from stock, on a single line.
function Get-Drift {
    (& (Join-Path $WorkDir 'tweaks_state.ps1') 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '
}

# Captures one condition: checks the game is still alive, logs the state and measures with PresentMon.
function Invoke-Capture($cond, $i) {
    if (-not (Get-Process $procName -ErrorAction SilentlyContinue)) { throw "$Process ya no esta corriendo." }
    $label = "$cond-$Api-$i"
    $daemon = (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Run ("captura label=$label daemon=$daemon desviado=[$(Get-Drift)]")
    & $bench -Process $Process -Label $label -Seconds $Seconds -WarmupSeconds 5 | Out-Null
}

# Starts the daemon in the background and waits for it to apply its optimizations to the game.
function Start-Daemon {
    Start-Process powershell -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$ab`" -Start -Dir `"$WorkDir`""
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue)) { throw 'El daemon no arranco.' }
    Start-Sleep -Seconds 10
}

# Stops the daemon with a clean shutdown and logs whether it recorded a game session.
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
