<#
.SYNOPSIS
    Waits for bgfx drawstress (launched on the desktop), measures it with bench.ps1 (PresentMon) and closes it.
.DESCRIPTION
    One capture of an A/B condition. Writes to ab_runs.log the label, the API, whether the VegaFlow daemon
    was running and the status-report values that differ from stock.
.PARAMETER Api
    d3d11 or d3d12.
.PARAMETER Label
    Run name (e.g. off-d3d11-1).
.PARAMETER Dim
    Side of the cube grid: dim^3 draw calls per frame.
.PARAMETER Seconds
    Duration of the PresentMon capture.
#>
param(
    [Parameter(Mandatory)][ValidateSet('d3d11', 'd3d12')][string]$Api,
    [Parameter(Mandatory)][string]$Label,
    [int]$Dim = 30,
    [int]$Seconds = 30,
    [string]$Exe = 'C:\Users\testuser\AppData\Local\metal-gpu-trace\drawstress-f7e25cbb8c4a\drawstress\drawstress.exe',
    [string]$WorkDir = 'C:\dev\vf-ab'
)

$ErrorActionPreference = 'Stop'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$stateScript = Join-Path $WorkDir 'tweaks_state.ps1'
$runsLog = Join-Path $WorkDir 'ab_runs.log'

$daemon = (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Measure-Object).Count
$drift = (& $stateScript 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch ' ok$' -and $_ -notmatch '^(valor|AMD|  |UserGpu|integridad|admin)' }) -join '; '

$game = $null
try {
    # The game must run in the interactive session (PresentMon sees no presents in session 0): it is launched from the desktop.
    $deadline = (Get-Date).AddSeconds(90)
    while (-not $game -and (Get-Date) -lt $deadline) {
        $game = Get-Process drawstress -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $game) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $game) { throw 'drawstress.exe no aparecio en 90 s; lanzalo desde el escritorio (Win+R).' }
    if ($game.SessionId -eq 0) { throw 'drawstress corre en la sesion 0: PresentMon no vera cuadros.' }
    Start-Sleep -Seconds 10
    & $bench -Process 'drawstress.exe' -Label $Label -Seconds $Seconds -WarmupSeconds 5
} finally {
    Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
    Add-Content -Path $runsLog -Value ('{0} label={1} api={2} dim={3} daemon={4} desviado=[{5}]' -f (Get-Date -Format o), $Label, $Api, $Dim, $daemon, $drift)
}
