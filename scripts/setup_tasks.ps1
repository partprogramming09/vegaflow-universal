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

Copy-Item (Join-Path $PSScriptRoot 'ab_profile.ps1') (Join-Path $dir 'ab_profile.ps1') -Force
$hashAb = (Get-FileHash (Join-Path $dir 'ab_profile.ps1') -Algorithm SHA256).Hash

# El daemon y su config se copian a la carpeta protegida: la tarea elevada no ejecuta nada de una carpeta escribible por el usuario.
$daemonSrc = Join-Path $PSScriptRoot '..\build\vegaflow_daemon.exe'
if (-not (Test-Path $daemonSrc)) { throw "Falta $daemonSrc; compila con scripts\build.ps1." }
Copy-Item $daemonSrc (Join-Path $dir 'vegaflow_daemon.exe') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $dir 'config'), (Join-Path $dir 'logs') | Out-Null
Copy-Item (Join-Path $PSScriptRoot '..\config\config.json') (Join-Path $dir 'config\config.json') -Force
$hashDaemon = (Get-FileHash (Join-Path $dir 'vegaflow_daemon.exe') -Algorithm SHA256).Hash

$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
$settingsLong = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 12)

function Register-VegaTask($name, $scriptArgs, $log, $script = 'tweaks_state.ps1', $taskSettings = $settings) {
    $cmd = "& '$dir\$script' $scriptArgs *> '$dir\$log'"
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -Command `"$cmd`""
    Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $taskSettings -Force | Out-Null
}

Register-VegaTask 'VegaFlow-State' '' 'state.log'
Register-VegaTask 'VegaFlow-Restore' '-Restore -Apply' 'restore.log'
Register-VegaTask 'VegaFlow-GamesOn' '-On' 'games.log' 'games_tweak.ps1'
Register-VegaTask 'VegaFlow-GamesOff' '-Off' 'games.log' 'games_tweak.ps1'
Register-VegaTask 'VegaFlow-Stop' '-Stop' 'ab_stop.log' 'ab_profile.ps1'
Register-VegaTask 'VegaFlow-Start' '-Start' 'ab_start.log' 'ab_profile.ps1' $settingsLong

$lines = @(
    "fecha=$(Get-Date -Format o)",
    "script_sha256=$hash",
    "games_sha256=$hashGames",
    "ab_profile_sha256=$hashAb",
    "daemon_sha256=$hashDaemon",
    "tareas=$((Get-ScheduledTask -TaskName 'VegaFlow-*' | ForEach-Object { $_.TaskName }) -join ', ')"
)
$lines | Set-Content $setupLog
$lines
