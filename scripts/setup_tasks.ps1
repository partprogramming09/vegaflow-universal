<#
.SYNOPSIS
    Creates two elevated, scoped scheduled tasks to restore and review the VegaFlow tweaks.
.DESCRIPTION
    Must be run once as administrator. Copies tweaks_state.ps1 to C:\ProgramData\VegaFlow,
    a folder only an administrator can write to, and registers:
      VegaFlow-State   : read-only report  -> state.log
      VegaFlow-Restore : restore to stock values (-Restore -Apply) -> restore.log
    The tasks only run on demand and point to the protected copy, not to the project folder.
    To change the script, run this installer again as administrator.
#>
#requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'

$dir = 'C:\ProgramData\VegaFlow'
$source = Join-Path $PSScriptRoot 'tweaks_state.ps1'
$setupLog = Join-Path $dir 'setup.log'

# Decided before the permissions below change: an existing config.json is kept only if an administrator already
# owned it. A file planted before the first install by another account would otherwise become trusted.
$configDst = Join-Path $dir 'config\config.json'
$keepConfig = $false
if (Test-Path $configDst) {
    $ownerSid = (Get-Acl $configDst).GetOwner([System.Security.Principal.SecurityIdentifier]).Value
    $keepConfig = $ownerSid -in @('S-1-5-18', 'S-1-5-32-544')
}

New-Item -ItemType Directory -Force -Path $dir | Out-Null
# SIDs for SYSTEM, Administrators and Users: text names fail on Windows in another language.
icacls $dir /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo fijar los permisos de la carpeta protegida.' }
icacls $dir /inheritance:r | Out-Null
icacls "$dir\*" /reset /T /C | Out-Null

Copy-Item $source (Join-Path $dir 'tweaks_state.ps1') -Force
Copy-Item (Join-Path $PSScriptRoot 'games_tweak.ps1') (Join-Path $dir 'games_tweak.ps1') -Force
$hash = (Get-FileHash (Join-Path $dir 'tweaks_state.ps1') -Algorithm SHA256).Hash
$hashGames = (Get-FileHash (Join-Path $dir 'games_tweak.ps1') -Algorithm SHA256).Hash

Copy-Item (Join-Path $PSScriptRoot 'ab_profile.ps1') (Join-Path $dir 'ab_profile.ps1') -Force
$hashAb = (Get-FileHash (Join-Path $dir 'ab_profile.ps1') -Algorithm SHA256).Hash

# The daemon and its config are copied to the protected folder: the elevated task runs nothing from a user-writable folder.
$daemonSrc = Join-Path $PSScriptRoot '..\build\vegaflow_daemon.exe'
if (-not (Test-Path $daemonSrc)) { throw "Falta $daemonSrc; compila con scripts\build.ps1." }
Copy-Item $daemonSrc (Join-Path $dir 'vegaflow_daemon.exe') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $dir 'config'), (Join-Path $dir 'logs') | Out-Null
# A reinstall keeps the settings already there; any other config.json is replaced by the one from the repository.
if (-not $keepConfig) { Copy-Item (Join-Path $PSScriptRoot '..\config\config.json') $configDst -Force }
$hashDaemon = (Get-FileHash (Join-Path $dir 'vegaflow_daemon.exe') -Algorithm SHA256).Hash
# Owner Administrators: the daemon only reads config.json when no other account owns or can write the folder.
icacls $dir /setowner '*S-1-5-32-544' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo fijar el propietario de la carpeta protegida.' }

$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest -LogonType Interactive
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10)
$settingsLong = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 12)

function Register-VegaTask($name, $scriptArgs, $log, $script = 'tweaks_state.ps1', $taskSettings = $settings) {
    $cmd = "& '$dir\$script' $scriptArgs *> '$dir\$log'"
    # Hidden: a visible console over a fullscreen game takes it out of exclusive fullscreen (Legacy Flip -> Independent Flip)
    # and skews every "daemon on" measurement (docs/mediciones/2026-11-09-diagnostico-laptop.md).
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -Command `"$cmd`""
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
