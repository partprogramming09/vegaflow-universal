<#
.SYNOPSIS
    Mide FPS medio, 1% Low y 0.1% Low de un proceso con PresentMon y guarda el resultado en JSON.
.DESCRIPTION
    Independiente del daemon: usa PresentMon directamente, así que sirve para comparar
    "daemon/tweak apagado" contra "encendido" sin que la medición dependa del código medido.
    1% Low y 0.1% Low usan la misma definición que FrameTelemetryMonitor: promedio del frametime
    del 1% (0.1%) de cuadros más lentos, convertido a FPS.
.PARAMETER Process
    Nombre del ejecutable a medir (ej. game.exe).
.PARAMETER Label
    Nombre de la condición medida (ej. baseline, timer-on, mpo-off).
.PARAMETER Seconds
    Duración de la captura. Por defecto 60.
.PARAMETER WarmupSeconds
    Segundos iniciales que se descartan. Por defecto 5.
.PARAMETER PresentMon
    Ruta a PresentMon.exe (2.x). Por defecto busca la variable PRESENTMON_EXE y luego el PATH.
.PARAMETER FromCsv
    Analiza un CSV de PresentMon ya capturado en vez de ejecutar PresentMon.
.PARAMETER StartDelaySeconds
    Espera antes de capturar, para dar tiempo a poner el juego en primer plano.
.PARAMETER Compare
    Dos archivos JSON de resultados: imprime la diferencia entre el segundo y el primero.
.EXAMPLE
    .\scripts\bench.ps1 -Process game.exe -Label baseline
.EXAMPLE
    .\scripts\bench.ps1 -Compare bench\results\baseline-*.json,bench\results\timer-on-*.json
#>
[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run', Mandatory)][string]$Process,
    [Parameter(ParameterSetName = 'Run', Mandatory)][string]$Label,
    [Parameter(ParameterSetName = 'Run')][int]$Seconds = 60,
    [Parameter(ParameterSetName = 'Run')][int]$WarmupSeconds = 5,
    [Parameter(ParameterSetName = 'Run')][string]$PresentMon = $env:PRESENTMON_EXE,
    [Parameter(ParameterSetName = 'Run')][string]$FromCsv,
    [Parameter(ParameterSetName = 'Run')][int]$StartDelaySeconds = 0,
    [Parameter(ParameterSetName = 'Compare', Mandatory)][string[]]$Compare
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ResultsDir = Join-Path $ProjectRoot 'bench\results'

function Get-Lows([double[]]$sorted, [double]$fraction) {
    $n = $sorted.Count
    $idx = [Math]::Min([int][Math]::Floor($fraction * $n), $n - 1)
    $slowest = $sorted[$idx..($n - 1)]
    $mean = ($slowest | Measure-Object -Average).Average
    if ($mean -gt 0) { 1000.0 / $mean } else { 0.0 }
}

if ($PSCmdlet.ParameterSetName -eq 'Compare') {
    if ($Compare.Count -ne 2) { throw 'Compare necesita exactamente dos archivos JSON.' }
    $a = Get-Content (Resolve-Path $Compare[0]) -Raw | ConvertFrom-Json
    $b = Get-Content (Resolve-Path $Compare[1]) -Raw | ConvertFrom-Json
    foreach ($m in 'avg_fps', 'fps_1pct_low', 'fps_0_1pct_low', 'frametime_stddev_ms') {
        $delta = $b.$m - $a.$m
        $pct = if ($a.$m -ne 0) { 100.0 * $delta / $a.$m } else { 0 }
        '{0,-22} {1,10:N2} -> {2,10:N2}   {3,+8:N2} ({4,+6:N1}%)' -f $m, $a.$m, $b.$m, $delta, $pct
    }
    "frames: $($a.frames) -> $($b.frames)   present_mode: $($a.present_mode) -> $($b.present_mode)   clean: $($a.clean) -> $($b.clean)"
    return
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$captureStart = $null

if ($FromCsv) {
    $csv = (Resolve-Path $FromCsv).Path
} else {
    if (-not $PresentMon) {
        $cmd = Get-Command 'PresentMon*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd) { $PresentMon = $cmd.Source }
    }
    if (-not $PresentMon -or -not (Test-Path $PresentMon)) {
        throw 'PresentMon.exe no encontrado. Pasa -PresentMon <ruta> o define PRESENTMON_EXE.'
    }

    $csv = Join-Path ([IO.Path]::GetTempPath()) "vegaflow-bench-$stamp.csv"
    if ($StartDelaySeconds -gt 0) { Start-Sleep -Seconds $StartDelaySeconds }
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $captureStart = Get-Date
    & $PresentMon --process_name $Process --output_file $csv --timed $Seconds --terminate_after_timed --no_console_stats --session_name vegaflow-bench --stop_existing_session 2>&1 | Out-Null
    $ErrorActionPreference = $prevEap
    if ($LASTEXITCODE -ne 0) { throw "PresentMon termino con codigo $LASTEXITCODE." }
    if (-not (Test-Path $csv)) { throw "PresentMon no genero datos. El proceso '$Process' debe estar en ejecucion y, si es de sistema, la consola debe ser administrador." }
}

$rows = Import-Csv $csv | Where-Object {
    $t = if ($null -ne $_.TimeInMs) { [double]$_.TimeInMs / 1000.0 } else { [double]$_.TimeInSeconds }
    $t -ge $WarmupSeconds -and $_.MsBetweenPresents -ne 'NA'
}
if ($rows.Count -lt 100) { throw "Solo $($rows.Count) cuadros tras el calentamiento: medicion invalida." }

$frametimes = [double[]]($rows | ForEach-Object { [double]$_.MsBetweenPresents } | Where-Object { $_ -gt 0 })
$sorted = $frametimes | Sort-Object
$mean = ($frametimes | Measure-Object -Average).Average
$variance = ($frametimes | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Average).Average
$modes = $rows | Group-Object PresentMode | Sort-Object Count -Descending

# Tirones: cuadros que tardan mas de 2 veces la mediana, por minuto de captura.
$medianFt = $sorted[[int][Math]::Floor($sorted.Count / 2)]
$hitches = @($frametimes | Where-Object { $_ -gt 2 * $medianFt }).Count
$minutes = ($frametimes | Measure-Object -Sum).Sum / 60000.0

# Tiempo entre cambios de imagen en pantalla (MsBetweenDisplayChange); 0 es un cuadro que no se mostro.
$display = [double[]]@($rows | Where-Object { $_.MsBetweenDisplayChange -and $_.MsBetweenDisplayChange -ne 'NA' } |
        ForEach-Object { [double]$_.MsBetweenDisplayChange } | Where-Object { $_ -gt 0 })
$displayStd = $null; $displayLate = $null
if ($display.Count -ge 100) {
    $dMean = ($display | Measure-Object -Average).Average
    $displayStd = [Math]::Round([Math]::Sqrt((($display | ForEach-Object { ($_ - $dMean) * ($_ - $dMean) } | Measure-Object -Average).Average)), 3)
    $dMedian = ($display | Sort-Object)[[int][Math]::Floor($display.Count / 2)]
    $displayLate = [Math]::Round(100.0 * @($display | Where-Object { $_ -gt 1.5 * $dMedian }).Count / $display.Count, 3)
}

# Eventos de fallo durante la captura: 4101 (reinicio del driver gráfico), 1000 y 1001 (fallo de aplicación o informe de errores), 141 y 117 (LiveKernelEvent).
$errors = $null
if ($captureStart) {
    $errors = @(Get-WinEvent -FilterHashtable @{ LogName = 'System', 'Application'; Id = 1000, 1001, 4101, 141, 117; StartTime = $captureStart } -ErrorAction SilentlyContinue |
            ForEach-Object { if ($_.Id -eq 1000) { '1000:' + $_.Properties[0].Value } else { '{0}:{1}' -f $_.Id, $_.ProviderName } })
}

$result = [ordered]@{
    label               = $Label
    process             = $Process
    timestamp           = (Get-Date).ToString('o')
    seconds             = $Seconds
    warmup_seconds      = $WarmupSeconds
    frames              = $frametimes.Count
    avg_fps             = [Math]::Round(1000.0 / $mean, 2)
    fps_1pct_low        = [Math]::Round((Get-Lows $sorted 0.99), 2)
    fps_0_1pct_low      = [Math]::Round((Get-Lows $sorted 0.999), 2)
    frametime_stddev_ms = [Math]::Round([Math]::Sqrt($variance), 3)
    present_mode        = $modes[0].Name
    present_mode_share  = [Math]::Round($modes[0].Count / $rows.Count, 3)
    clean               = (($modes[0].Count / $rows.Count) -ge 0.99)
    hitches_2x_median   = $hitches
    hitches_per_min     = [Math]::Round($hitches / $minutes, 2)
    display_frames      = $display.Count
    display_stddev_ms   = $displayStd
    display_late_pct    = $displayLate
    errors              = $errors
    windows_build       = [Environment]::OSVersion.Version.ToString()
}

$out = Join-Path $ResultsDir "$Label-$stamp.json"
$result | ConvertTo-Json | Set-Content -Path $out -Encoding UTF8
if (-not $FromCsv) { Move-Item $csv (Join-Path $ResultsDir "$Label-$stamp.csv") -Force -ErrorAction SilentlyContinue }

$result | ConvertTo-Json
"Guardado en $out"
