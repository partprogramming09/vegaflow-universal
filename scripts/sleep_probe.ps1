<#
.SYNOPSIS
    Mide cuanto dura realmente Sleep(1) en este proceso.
.DESCRIPTION
    Sirve para comprobar si una resolucion de temporizador pedida por OTRO proceso
    (scripts\hold_timer.ps1) cambia la planificacion de este. No requiere administrador.
.PARAMETER Iterations
    Numero de Sleep(1) medidos.
#>
param([int]$Iterations = 1000)

$sw = [Diagnostics.Stopwatch]::new()
$ms = New-Object 'System.Collections.Generic.List[double]'
for ($i = 0; $i -lt $Iterations; $i++) {
    $sw.Restart()
    [Threading.Thread]::Sleep(1)
    $ms.Add($sw.Elapsed.TotalMilliseconds)
}
$sorted = $ms | Sort-Object
$avg = ($ms | Measure-Object -Average).Average
'n={0} media={1:N3} ms mediana={2:N3} ms p99={3:N3} ms max={4:N3} ms' -f $Iterations, $avg, $sorted[[int]($Iterations / 2)], $sorted[[int]($Iterations * 0.99)], $sorted[-1]
