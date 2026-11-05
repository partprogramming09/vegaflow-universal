<#
.SYNOPSIS
    Mide una escena con el protocolo del plan de pruebas y empaqueta los resultados con la huella del equipo.
.DESCRIPTION
    Para compartir mediciones entre equipos: con el juego abierto en la escena a medir, hace una captura de
    calentamiento que no cuenta y N capturas con bench.ps1, guarda el estado de los ajustes de VegaFlow y la
    informacion del equipo (vegaflow_daemon.exe --system-info) y lo comprime en bench\pack-ETIQUETA-FECHA.zip.
    El zip trae los JSON y CSV de cuadros de cada captura: se analiza con `vfdev frames` o `vfdev regress`.
    No enciende ni apaga el daemon: mide el equipo tal como esta. Mide una sola condicion; para comparar el
    daemon apagado y encendido se corre dos veces o se usa ab_loop_task.ps1.
.PARAMETER Process
    Ejecutable del juego abierto (ej. game.exe).
.PARAMETER Label
    Nombre de la escena y condicion (ej. octopath-mundo-daemon-off). Solo letras, numeros, punto, guion y guion bajo.
.PARAMETER Captures
    Capturas que cuentan. Por defecto 5.
.PARAMETER Seconds
    Duracion de cada captura, con 5 s de calentamiento descartados. Por defecto 125 (120 s de datos).
.PARAMETER Daemon
    Ruta de vegaflow_daemon.exe para leer la informacion del equipo. Por defecto C:\ProgramData\VegaFlow o build.
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
