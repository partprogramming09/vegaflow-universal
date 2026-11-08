<#
.SYNOPSIS
    Measures how long Sleep(1) really takes in this process.
.DESCRIPTION
    Used to check whether a timer resolution requested by ANOTHER process
    (scripts\hold_timer.ps1) changes the scheduling of this one. Does not require administrator.
.PARAMETER Iterations
    Number of Sleep(1) calls measured.
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
