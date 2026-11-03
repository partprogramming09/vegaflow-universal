<#
.SYNOPSIS
    Pide una resolucion de temporizador NT y la mantiene mientras el script vive.
.DESCRIPTION
    Condicion aislada para medir el efecto del temporizador de 0.5 ms sin el resto del daemon.
    La peticion se libera al terminar el script. Escribe la resolucion vista en bench\timer.log.
.PARAMETER Ns100
    Resolucion pedida en unidades de 100 ns. 5000 = 0.5 ms.
.PARAMETER Seconds
    Tiempo que mantiene la peticion.
#>
param(
    [uint32]$Ns100 = 5000,
    [int]$Seconds = 600
)

Add-Type -Namespace VF -Name Nt -MemberDefinition @'
[DllImport("ntdll.dll")] public static extern int NtSetTimerResolution(uint desired, bool set, out uint current);
[DllImport("ntdll.dll")] public static extern int NtQueryTimerResolution(out uint min, out uint max, out uint current);
'@

$logPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'bench\timer.log'
[uint32]$cur = 0
$status = [VF.Nt]::NtSetTimerResolution($Ns100, $true, [ref]$cur)
[uint32]$min = 0; [uint32]$max = 0; [uint32]$now = 0
[void][VF.Nt]::NtQueryTimerResolution([ref]$min, [ref]$max, [ref]$now)
"{0} set_status={1} requested={2} current_after={3} min={4} max={5}" -f (Get-Date -Format o), $status, $Ns100, $now, $min, $max | Add-Content $logPath

Start-Sleep -Seconds $Seconds

[void][VF.Nt]::NtSetTimerResolution($Ns100, $false, [ref]$cur)
