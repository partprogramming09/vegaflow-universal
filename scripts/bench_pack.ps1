<#
.SYNOPSIS
    Measures a scene with the test-plan protocol and packages the results with the machine fingerprint.
.DESCRIPTION
    For sharing measurements between machines: with the game open on the scene to measure, it does a
    warm-up capture that does not count and N captures with bench.ps1, saves the state of the VegaFlow settings and the
    machine information (vegaflow_daemon.exe --system-info) and compresses it into bench\pack-LABEL-DATE.zip.
    The zip contains the JSON and frame CSV of each capture: analyze it with `vfdev frames` or `vfdev regress`.
    It does not turn the daemon on or off: it measures the machine as it is. It measures a single condition; to compare
    daemon off and on, run it twice or use ab_loop_task.ps1.
.PARAMETER Process
    Executable of the open game (e.g. game.exe).
.PARAMETER Label
    Name of the scene and condition (e.g. octopath-mundo-daemon-off). Only letters, digits, dot, hyphen and underscore.
.PARAMETER Captures
    Captures that count. Defaults to 5.
.PARAMETER Seconds
    Duration of each capture, with 5 s of warm-up discarded. Defaults to 125 (120 s of data).
.PARAMETER Daemon
    Path to vegaflow_daemon.exe for reading the machine information. Defaults to C:\ProgramData\VegaFlow or build.
#>
param(
    [Parameter(Mandatory)][string]$Process,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]{1,60}$')][string]$Label,
    [int]$Captures = 5,
    [int]$Seconds = 125,
    [string]$Daemon = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Daemon) { $Daemon = @('C:\ProgramData\VegaFlow\vegaflow_daemon.exe', (Join-Path $root 'build\vegaflow_daemon.exe')) | Where-Object { Test-Path $_ } | Select-Object -First 1 }
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$results = Join-Path $root 'bench\results'
if (-not (Get-Process ([IO.Path]::GetFileNameWithoutExtension($Process)) -ErrorAction SilentlyContinue)) { throw "$Process no esta abierto." }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$tag = "pack-$Label-$stamp"
& $bench -Process $Process -Label "$tag-warmup" -Seconds 35 | Out-Null
1..$Captures | ForEach-Object { & $bench -Process $Process -Label "$tag-$_" -Seconds $Seconds | Out-Null }

$extra = Join-Path $results "$tag-equipo.json"
$info = if ($Daemon -and (Test-Path $Daemon)) { (& $Daemon --system-info 2>&1) -join "`n" } else { 'vegaflow_daemon.exe no encontrado; sin informacion del equipo' }
$ts = @('C:\ProgramData\VegaFlow\tweaks_state.ps1', (Join-Path $PSScriptRoot 'tweaks_state.ps1')) | Where-Object { Test-Path $_ } | Select-Object -First 1
$state = if ($ts) { (& $ts 2>&1 | Out-String) } else { 'tweaks_state.ps1 no encontrado' }
[ordered]@{ label = $Label; process = $Process; captures = $Captures; seconds = $Seconds; system_info = $info; tweaks_state = $state } |
    ConvertTo-Json | Set-Content -Path $extra -Encoding UTF8

$zip = Join-Path $root "bench\$tag.zip"
Compress-Archive -Path (Join-Path $results "$tag-*") -DestinationPath $zip -Force
"Paquete: $zip"
