<#
.SYNOPSIS
    Installs VegaFlow from this package into a folder only administrators can write to.
.DESCRIPTION
    Run from the unzipped package, as administrator. Checks every file against SHA256SUMS, stops and reverts a
    previous install, copies the daemon, keeps an administrator-owned config.json, and (unless -NoShell) adds the
    Add/Remove Programs entry and a Start Menu shortcut. Nothing is installed at logon unless -Autostart is given.
    -DryRun prints each step and changes nothing.
#>
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'VegaFlow'),
    [switch]$Autostart,
    # No Add/Remove Programs entry, Start Menu shortcut or autostart task (used by the package tests).
    [switch]$NoShell,
    # Allows a folder outside Program Files. Only for a parent folder that no standard user can write: whoever can
    # rename the folder can put their own program where the shortcut and the autostart task point.
    [switch]$AllowOutsideProgramFiles,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VegaFlow'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\VegaFlow.lnk'
$taskName = 'VegaFlow'
if ($Autostart -and $NoShell) { throw '-Autostart no se puede combinar con -NoShell.' }
$say = if ($DryRun) { '[dry-run] ' } else { '' }
function Step($text) { Write-Host "$say$text" }

# --- 1. The package: every file must match SHA256SUMS -------------------------------------------------------
$sums = Join-Path $PSScriptRoot 'SHA256SUMS'
if (-not (Test-Path $sums)) { throw 'Falta SHA256SUMS: este no es un paquete de VegaFlow completo.' }
$listed = @{}
foreach ($line in Get-Content $sums) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$') { $listed[$Matches[2]] = $Matches[1].ToUpperInvariant() }
}
foreach ($name in @('vegaflow_daemon.exe', 'uninstall.ps1', 'config/config.json', 'VERSION')) {
    if (-not $listed.ContainsKey($name)) { throw "SHA256SUMS no lista $name." }
}
foreach ($name in $listed.Keys) {
    $path = Join-Path $PSScriptRoot ($name -replace '/', '\')
    if (-not (Test-Path $path)) { throw "Falta $name." }
    if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $listed[$name]) {
        throw "$name no coincide con SHA256SUMS: el paquete está dañado. No se instaló nada. (SHA256SUMS viaja dentro del zip: compara también el zip con su archivo .sha256, que viene por otro lado.)"
    }
}
$version = (Get-Content (Join-Path $PSScriptRoot 'VERSION') -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION no es MAJOR.MINOR.PATCH: '$version'." }
Step "Paquete $version verificado (SHA-256 de $($listed.Count) archivos)."

# --- 2. The target folder ---------------------------------------------------------------------------------
if ($InstallDir -notmatch '^[A-Za-z]:\\') { throw "InstallDir debe ser una ruta absoluta con unidad (C:\...): $InstallDir" }
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$programFiles = @($env:ProgramW6432, $env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') + '\' }
if (-not $AllowOutsideProgramFiles -and -not ($programFiles | Where-Object { $InstallDir.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) })) {
    throw "InstallDir debe estar dentro de Program Files (la carpeta de arriba debe ser solo de administradores). Usa -AllowOutsideProgramFiles solo si lo es."
}
$root = [System.IO.Path]::GetPathRoot($InstallDir).TrimEnd('\')
if ($InstallDir -eq $root -or $InstallDir -ieq $env:SystemRoot -or $InstallDir -ieq $env:ProgramFiles -or $InstallDir -ieq $env:ProgramData) {
    throw "InstallDir no puede ser $InstallDir (usa una subcarpeta propia)."
}
$exe = Join-Path $InstallDir 'vegaflow_daemon.exe'
$manifestPath = Join-Path $InstallDir 'install.json'
# install.json marks a folder as ours (it is written before the first file is copied, so a run that died halfway
# can be repeated or uninstalled).
$existing = Test-Path -LiteralPath $manifestPath
# A folder left by `uninstall.ps1 -KeepConfig` holds only config\ and is fine to install into again. A link
# named config or logs is not: icacls and the uninstaller would act on its target.
$foreign = @(Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -notin @('config', 'logs') -or ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint)
    })
if ((Test-Path -LiteralPath $InstallDir) -and -not $existing -and $foreign.Count -gt 0) {
    throw "$InstallDir existe, no está vacía y no es una instalación de VegaFlow. No se toca."
}

# --- 3. A previous install: stop it and hand the system back before replacing files -------------------------
if ($existing -and (Test-Path -LiteralPath $exe)) {
    $running = @(Get-Process -Name 'vegaflow_daemon' -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
    if ($running.Count -gt 0) {
        Step "Paro $($running.Count) proceso(s) de VegaFlow de la instalación anterior."
        if (-not $DryRun) { $running | Stop-Process -Force; $running | ForEach-Object { $null = $_.WaitForExit(10000) } }
    }
    Step 'Devuelvo los cambios pendientes de la instalación anterior (--restore-registry).'
    if (-not $DryRun) {
        & $exe --restore-registry | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "--restore-registry de la instalación anterior terminó con código $LASTEXITCODE. No se reemplazó nada." }
    }
}

# --- 4. The files ------------------------------------------------------------------------------------------
$configDst = Join-Path $InstallDir 'config\config.json'
# An existing config.json survives only if an administrator already owned it: one planted by another account
# before the install must not become trusted (the daemon applies the same rule when it reads it).
$keepConfig = $false
if (Test-Path -LiteralPath $configDst) {
    $ownerSid = (Get-Acl $configDst).GetOwner([System.Security.Principal.SecurityIdentifier]).Value
    $keepConfig = $ownerSid -in @('S-1-5-18', 'S-1-5-32-544')
}
Step "Instalo en $InstallDir$(if ($keepConfig) { ' (conservo config.json)' })."
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    # SIDs for SYSTEM, Administrators and Users: the text names fail on a Windows in another language.
    icacls $InstallDir /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo fijar los permisos de la carpeta de instalación.' }
    icacls $InstallDir /inheritance:r | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo quitar los permisos heredados de la carpeta de instalación.' }
    icacls "$InstallDir\*" /reset /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo restablecer los permisos de los archivos de la carpeta de instalación.' }
    # The manifest goes first: from here on, a run that dies halfway leaves a folder that uninstall.ps1 accepts.
    $manifest = [ordered]@{
        version      = $version
        installed_at = (Get-Date).ToString('o')
        files        = @('vegaflow_daemon.exe', 'uninstall.ps1', 'VERSION', 'install.json')
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir 'config'), (Join-Path $InstallDir 'logs') | Out-Null
    Copy-Item (Join-Path $PSScriptRoot 'vegaflow_daemon.exe') $exe -Force
    Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') (Join-Path $InstallDir 'uninstall.ps1') -Force
    Copy-Item (Join-Path $PSScriptRoot 'VERSION') (Join-Path $InstallDir 'VERSION') -Force
    if (-not $keepConfig) { Copy-Item (Join-Path $PSScriptRoot 'config\config.json') $configDst -Force }
    # Owner Administrators: the daemon reads config.json only when no other account owns or can write the folder.
    icacls $InstallDir /setowner '*S-1-5-32-544' /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls no pudo fijar el propietario de la carpeta de instalación.' }

    # The package folder is writable by whoever unzipped it: check the copies in the protected folder, which are the
    # ones that run as administrator, before the first one is executed.
    foreach ($name in @('vegaflow_daemon.exe', 'uninstall.ps1', 'VERSION')) {
        if ((Get-FileHash -LiteralPath (Join-Path $InstallDir $name) -Algorithm SHA256).Hash -ne $listed[$name]) {
            throw "$name cambió mientras se copiaba. Se aborta antes de ejecutarlo."
        }
    }

    $reported = (& $exe --version) -join ' '
    if ($reported -notmatch ('^VegaFlow Universal ' + [regex]::Escape($version) + '$')) {
        throw "El daemon instalado dice '$reported' y el paquete es $version."
    }
    Step "Comprobado: $reported."
}

# --- 5. Windows integration (optional) ---------------------------------------------------------------------
if (-not $NoShell) {
    Step 'Registro la entrada de Agregar o quitar programas y el acceso del menú Inicio.'
    if (-not $DryRun) {
        New-Item -Path $uninstallKey -Force | Out-Null
        $uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$InstallDir\uninstall.ps1`""
        $props = @{
            DisplayName = 'VegaFlow Universal'; DisplayVersion = $version; Publisher = 'VegaFlow'
            InstallLocation = $InstallDir; UninstallString = $uninstallCmd; DisplayIcon = $exe
        }
        foreach ($k in $props.Keys) { Set-ItemProperty -Path $uninstallKey -Name $k -Value $props[$k] }
        New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

        $sh = New-Object -ComObject WScript.Shell
        $lnk = $sh.CreateShortcut($shortcut)
        $lnk.TargetPath = $exe; $lnk.Arguments = '--gui'; $lnk.WorkingDirectory = $InstallDir
        $lnk.Description = 'VegaFlow Universal'; $lnk.Save()
    }
    if ($Autostart) {
        $other = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($other -and -not ($other.Actions | Where-Object { $_.Execute -ieq $exe })) {
            throw "Ya existe una tarea '$taskName' que no es de VegaFlow. No se reemplaza."
        }
        Step "Programo '$taskName' al iniciar sesión (nivel más alto, solo el daemon)."
        if (-not $DryRun) {
            $action = New-ScheduledTaskAction -Execute $exe -WorkingDirectory $InstallDir
            $trigger = New-ScheduledTaskTrigger -AtLogOn
            $principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest
            $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
            Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
        }
    }
}

Step "Listo: VegaFlow $version en $InstallDir. Para quitarlo: $InstallDir\uninstall.ps1"
