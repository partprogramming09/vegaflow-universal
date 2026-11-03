<#
.SYNOPSIS
    Crea dos tareas programadas elevadas y acotadas para restaurar y revisar los tweaks de VegaFlow.
.DESCRIPTION
    Debe ejecutarse una vez como administrador. Copia tweaks_state.ps1 a C:\ProgramData\VegaFlow,
    una carpeta que solo un administrador puede escribir, y registra:
      VegaFlow-State   : informe de solo lectura  -> state.log
      VegaFlow-Restore : restauracion a valores de fabrica (-Restore -Apply) -> restore.log
    Las tareas solo se ejecutan bajo demanda y apuntan a la copia protegida, no a la carpeta del proyecto.
    Para cambiar el script hay que volver a ejecutar este instalador como administrador.
#>
#requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'

$dir = 'C:\ProgramData\VegaFlow'
$source = Join-Path $PSScriptRoot 'tweaks_state.ps1'
$setupLog = Join-Path $dir 'setup.log'

New-Item -ItemType Directory -Force -Path $dir | Out-Null
icacls $dir /inheritance:r /grant:r 'Administrators:(OI)(CI)F' 'SYSTEM:(OI)(CI)F' 'Users:(OI)(CI)RX' | Out-Null

Copy-Item $source (Join-Path $dir 'tweaks_state.ps1') -Force
Copy-Item (Join-Path $PSScriptRoot 'games_tweak.ps1') (Join-Path $dir 'games_tweak.ps1') -Force
$hash = (Get-FileHash (Join-Path $dir 'tweaks_state.ps1') -Algorithm SHA256).Hash
$hashGames = (Get-FileHash (Join-Path $dir 'games_tweak.ps1') -Algorithm SHA256).Hash

$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

function Register-VegaTask($name, $scriptArgs, $log, $script = 'tweaks_state.ps1') {
    $cmd = "& '$dir\$script' $scriptArgs *> '$dir\$log'"
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -Command `"$cmd`""
    Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings -Force | Out-Null
}

Register-VegaTask 'VegaFlow-State' '' 'state.log'
Register-VegaTask 'VegaFlow-Restore' '-Restore -Apply' 'restore.log'
Register-VegaTask 'VegaFlow-GamesOn' '-On' 'games.log' 'games_tweak.ps1'
Register-VegaTask 'VegaFlow-GamesOff' '-Off' 'games.log' 'games_tweak.ps1'

$lines = @(
    "fecha=$(Get-Date -Format o)",
    "script_sha256=$hash",
    "games_sha256=$hashGames",
    "tareas=$((Get-ScheduledTask -TaskName 'VegaFlow-*' | ForEach-Object { $_.TaskName }) -join ', ')"
)
$lines | Set-Content $setupLog
$lines
