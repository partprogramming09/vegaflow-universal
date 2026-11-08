<#
.SYNOPSIS
    Pre-flight check: connectivity, session, power, disk, leftover processes and non-stock values.
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
# Users (S-1-5-32-545), Everyone (S-1-1-0) or Authenticated Users (S-1-5-11) with write permission on the protected folder.
$writeRights = [Security.AccessControl.FileSystemRights]'WriteData,AppendData,Delete,WriteAttributes,WriteExtendedAttributes,ChangePermissions,TakeOwnership'
$weak = (Get-Acl 'C:\ProgramData\VegaFlow').Access | Where-Object {
    $_.AccessControlType -eq 'Allow' -and
    ($_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -in 'S-1-5-32-545', 'S-1-1-0', 'S-1-5-11') -and
    (($_.FileSystemRights -band $writeRights) -ne 0)
}
'carpeta_escribible_por_usuarios=' + [bool]$weak
'--- fuera de fabrica'
& 'C:\ProgramData\VegaFlow\tweaks_state.ps1' 2>&1 | Where-Object { $_ -match 'DIFERENTE' }
'fin'
