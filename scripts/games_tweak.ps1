<#
.SYNOPSIS
    Activa o desactiva el perfil MMCSS de juegos (Tasks\Games) usado en las pruebas A/B.
.DESCRIPTION
    -On  : Priority=6, Scheduling Category=High, SFIO Priority=High.
    -Off : valores de fabrica de Windows 11 (Priority=2, Medium, Normal).
    Requiere administrador. Escribe el resultado en C:\ProgramData\VegaFlow\games.log.
    MMCSS lee estos valores cuando un hilo se registra, asi que el juego debe reiniciarse
    tras cambiarlos.
#>
param(
    [switch]$On,
    [switch]$Off
)

$ErrorActionPreference = 'Stop'
if ($On -eq $Off) { throw 'Indica exactamente uno: -On o -Off.' }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Requiere administrador.' }

$key = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games'
$values = if ($On) {
    @{ 'Priority' = 6; 'Scheduling Category' = 'High'; 'SFIO Priority' = 'High' }
} else {
    @{ 'Priority' = 2; 'Scheduling Category' = 'Medium'; 'SFIO Priority' = 'Normal' }
}

foreach ($name in $values.Keys) {
    Set-ItemProperty -Path $key -Name $name -Value $values[$name]
}

$now = Get-ItemProperty -Path $key
'{0} modo={1} Priority={2} Category={3} SFIO={4}' -f (Get-Date -Format o), $(if ($On) { 'On' } else { 'Off' }), $now.Priority, $now.'Scheduling Category', $now.'SFIO Priority'
