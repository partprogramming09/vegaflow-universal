<#
.SYNOPSIS
    Espera a bgfx drawstress (lanzado en el escritorio), lo mide con bench.ps1 (PresentMon) y lo cierra.
.DESCRIPTION
    Una captura de una condicion A/B. Anota en ab_runs.log la etiqueta, la API, si el daemon de
    VegaFlow estaba corriendo y los valores del informe de estado que no estan en fabrica.
.PARAMETER Api
    d3d11 o d3d12.
.PARAMETER Label
    Nombre de la corrida (ej. off-d3d11-1).
.PARAMETER Dim
    Lado de la cuadricula de cubos: dim^3 draw calls por cuadro.
.PARAMETER Seconds
    Duracion de la captura de PresentMon.
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
    # El juego debe correr en la sesion interactiva (PresentMon no ve presents en la sesion 0): se lanza desde el escritorio.
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
