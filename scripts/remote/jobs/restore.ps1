<#
.SYNOPSIS
    Interruptor de emergencia: para el daemon con cierre limpio, restaura el perfil de energia y los valores de fabrica, y cierra drawstress.
#>
$ErrorActionPreference = 'Continue'
$root = 'C:\ProgramData\VegaFlow'

$d = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Select-Object -First 1
if ($d) {
    try {
        $ev = [Threading.EventWaitHandle]::OpenExisting('Global\VegaFlowStop')
        [void]$ev.Set()
        if (-not $d.WaitForExit(20000)) { 'AVISO: el daemon no salio en 20 s' } else { 'daemon parado con cierre limpio' }
    } catch { "AVISO: no se pudo senalar el cierre: $($_.Exception.Message)" }
}
& "$root\vegaflow_daemon.exe" --apu-balanced 2>&1 | ForEach-Object { "apu-balanced: $_" }
Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
& "$root\tweaks_state.ps1" -Restore -Apply 2>&1
