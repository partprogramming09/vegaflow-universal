<#
.SYNOPSIS
    Removes VegaFlow and gives the system back first.
.DESCRIPTION
    Stops VegaFlow, runs `vegaflow_daemon.exe --restore-registry` (reverts everything in the change journal and the
    registry backups) and only if that succeeds deletes the files it installed, its Add/Remove Programs entry, its
    shortcut and its autostart task. Run as administrator, from the install folder or with -InstallDir.
    -KeepConfig keeps config\config.json; -Force deletes even if the restore failed. -DryRun prints each step and changes nothing.
#>
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$InstallDir,
    [switch]$KeepConfig,
    # Delete even if --restore-registry could not give everything back (the registry backups stay in
    # HKLM\SOFTWARE\VegaFlow\RegBackup; game files changed by VegaFlow keep their .vegaflow.bak copies).
    [switch]$Force,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VegaFlow'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\VegaFlow.lnk'
$taskName = 'VegaFlow'
$say = if ($DryRun) { '[dry-run] ' } else { '' }
function Step($text) { Write-Host "$say$text" }

if (-not $InstallDir) { $InstallDir = $PSScriptRoot }
if ($InstallDir -notmatch '^[A-Za-z]:\\') { throw "InstallDir debe ser una ruta absoluta con unidad (C:\...): $InstallDir" }
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$exe = Join-Path $InstallDir 'vegaflow_daemon.exe'
$manifestPath = Join-Path $InstallDir 'install.json'
# Only a folder this installer made is touched. install.json (not the exe) is the mark, so a run that stopped
# halfway can be repeated. The list of files to delete is fixed here, never read from the manifest.
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "$InstallDir no parece una instalación de VegaFlow (falta install.json). No se borra nada."
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$ownFiles = @('uninstall.ps1', 'VERSION')
foreach ($name in @('config', 'logs')) {
    $p = Join-Path $InstallDir $name
    if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$p es un enlace, no una carpeta: no se borra nada."
    }
}
Step "Desinstalo VegaFlow $($manifest.version) de $InstallDir."

# Leave the folder before deleting it: a process whose current directory is inside it blocks the removal.
Set-Location $env:SystemRoot

# --- 1. Stop VegaFlow ----------------------------------------------------------------------------------------
$running = @(Get-Process -Name 'vegaflow_daemon' -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
if ($running.Count -gt 0) {
    Step "Paro $($running.Count) proceso(s) de VegaFlow."
    if (-not $DryRun) { $running | Stop-Process -Force; $running | ForEach-Object { $null = $_.WaitForExit(10000) } }
}

# --- 2. Give the system back: if this fails, nothing is deleted (unless -Force) -----------------------------------
if (Test-Path -LiteralPath $exe) {
    Step 'Devuelvo los cambios de VegaFlow (--restore-registry).'
    if (-not $DryRun) {
        & $exe --restore-registry
        if ($LASTEXITCODE -ne 0) {
            $msg = "--restore-registry terminó con código ${LASTEXITCODE}: algo no se devolvió (un diario corrupto cuenta). No se borró nada, para no perder el diario ni las copias de los valores originales. Revisa la salida y vuelve a ejecutar; con -Force se borra de todos modos."
            if ($Force) { Write-Warning $msg } else { throw $msg }
        }
    }
} else {
    Write-Warning 'Falta vegaflow_daemon.exe (¿lo borró un antivirus?): no se pudo devolver nada. Las copias del registro siguen en HKLM\SOFTWARE\VegaFlow\RegBackup.'
}

# --- 3. Windows integration, only what points at this folder -------------------------------------------------
if (Test-Path $uninstallKey) {
    $loc = (Get-ItemProperty -Path $uninstallKey).InstallLocation
    if ($loc -and ($loc.TrimEnd('\') -ieq $InstallDir)) {
        Step 'Quito la entrada de Agregar o quitar programas.'
        if (-not $DryRun) { Remove-Item -Path $uninstallKey -Recurse -Force }
    }
}
if (Test-Path -LiteralPath $shortcut) {
    $target = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut).TargetPath
    if ($target -ieq $exe) {
        Step 'Quito el acceso del menú Inicio.'
        if (-not $DryRun) { Remove-Item -LiteralPath $shortcut -Force }
    }
}
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task -and ($task.Actions | Where-Object { $_.Execute -ieq $exe })) {
    Step "Quito la tarea '$taskName'."
    if (-not $DryRun) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false }
}

# --- 4. Files: the data first, then the program, then the marks that let this script run again ------------------
$logs = Join-Path $InstallDir 'logs'
if (Test-Path -LiteralPath $logs) { Step 'Borro logs\.'; if (-not $DryRun) { Remove-Item -LiteralPath $logs -Recurse -Force } }
$config = Join-Path $InstallDir 'config'
if ((Test-Path -LiteralPath $config) -and -not $KeepConfig) { Step 'Borro config\ (diario y configuración).'; if (-not $DryRun) { Remove-Item -LiteralPath $config -Recurse -Force } }
if ($KeepConfig) { Step 'Conservo config\ (-KeepConfig).' }
foreach ($name in $ownFiles) {
    $p = Join-Path $InstallDir $name
    if ((Test-Path -LiteralPath $p) -and $name -ne 'uninstall.ps1') { Step "Borro $name."; if (-not $DryRun) { Remove-Item -LiteralPath $p -Force } }
}
if ((Test-Path -LiteralPath $exe)) { Step 'Borro vegaflow_daemon.exe.'; if (-not $DryRun) { Remove-Item -LiteralPath $exe -Force } }

# install.json and this script go last: if anything above failed, they are still there to run again.
if (-not $DryRun) {
    Remove-Item -LiteralPath $manifestPath -Force
    $self = Join-Path $InstallDir 'uninstall.ps1'
    if (Test-Path -LiteralPath $self) { Remove-Item -LiteralPath $self -Force }
    if (-not (Get-ChildItem -LiteralPath $InstallDir -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $InstallDir -Force }
}
$left = if (-not $DryRun -and (Test-Path -LiteralPath $InstallDir)) { " Queda $InstallDir con: $((Get-ChildItem -LiteralPath $InstallDir -Force | ForEach-Object Name) -join ', ')." } else { '' }
Step "Listo: VegaFlow desinstalado.$left"
