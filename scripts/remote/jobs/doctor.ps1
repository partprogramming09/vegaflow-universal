<#
.SYNOPSIS
    Comprobacion previa: conectividad, sesion, energia, disco, procesos sobrantes y valores fuera de fabrica.
#>
$ErrorActionPreference = 'SilentlyContinue'
$os = Get-CimInstance Win32_OperatingSystem
$bat = Get-CimInstance Win32_Battery
$explorer = Get-Process explorer | Select-Object -First 1
'hora=' + (Get-Date -Format o)
'arranque=' + $os.LastBootUpTime.ToString('o')
'usuario_sesion=' + (Get-CimInstance Win32_ComputerSystem).UserName
'sesion_escritorio=' + $(if ($explorer) { $explorer.SessionId } else { 'ninguna' })
'este_proceso_sesion=' + (Get-Process -Id $PID).SessionId
'cargador=' + $(if ($bat.BatteryStatus -eq 2) { 'conectado' } else { "no (estado $($bat.BatteryStatus))" })
'bateria_pct=' + $bat.EstimatedChargeRemaining
'disco_libre_gb=' + [int]((Get-PSDrive C).Free / 1GB)
'servicios=' + ((Get-Service sshd, Tailscale | ForEach-Object { "$($_.Name):$($_.Status)" }) -join ' ')
'sobrantes=' + ((Get-Process drawstress, vegaflow_daemon, PresentMon* | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ' ')
'pantalla_bloqueada=' + [bool](Get-Process LogonUI)
'--- fuera de fabrica'
& 'C:\ProgramData\VegaFlow\tweaks_state.ps1' 2>&1 | Where-Object { $_ -match 'DIFERENTE' }
'fin'
