<#
.SYNOPSIS
    End-to-end test of install.ps1 and uninstall.ps1 on a real Windows machine.
.DESCRIPTION
    Run as administrator against an unzipped package. Installs into -InstallDir (a throwaway folder by default),
    applies a real change with the installed daemon, uninstalls, and checks that the system is back as it was.
    -Shell also exercises the Add/Remove Programs entry and the Start Menu shortcut (use it with the real
    install folder; it needs no other VegaFlow install, because the entry has a fixed name).
    Exit code 0 only if every check passed.
#>
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDir,
    [string]$InstallDir = (Join-Path $env:ProgramData ('vf-pkgtest-' + [guid]::NewGuid().ToString('N').Substring(0, 8))),
    [switch]$Shell
)
$ErrorActionPreference = 'Stop'
$PackageDir = (Resolve-Path $PackageDir).Path
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VegaFlow'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\VegaFlow.lnk'
$dwm = 'HKLM:\SOFTWARE\Microsoft\Windows\Dwm'
$pass = 0; $fail = 0
function Check($name, [bool]$ok, $detail = '') {
    if ($ok) { $script:pass++; Write-Host "  PASS  $name" } else { $script:fail++; Write-Host "  FAIL  $name  $detail" }
}
# Runs a package script in its own PowerShell so a `throw` becomes an exit code.
function Invoke-Pkg($script, [string[]]$extra) {
    # A child's stderr line would become a terminating error under 'Stop'; here it is just output to keep.
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script @extra 2>&1 | Out-String; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $old }
    [pscustomobject]@{ Code = $code; Output = $out }
}
function Get-Overlay { (Get-ItemProperty -Path $dwm -Name OverlayTestMode -ErrorAction SilentlyContinue).OverlayTestMode }
function Get-State {
    [pscustomobject]@{
        Overlay  = Get-Overlay
        # Only the GUID: the label's accents are decoded differently once a child process has changed the console.
        Power    = [regex]::Match((powercfg /getactivescheme | Out-String), '[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}').Value
        Key      = Test-Path $uninstallKey
        Shortcut = Test-Path $shortcut
        Task     = [bool](Get-ScheduledTask -TaskName 'VegaFlow' -ErrorAction SilentlyContinue)
    }
}
$shellArg = @(); if (-not $Shell) { $shellArg += '-NoShell' }; $shellArg += '-AllowOutsideProgramFiles'
$install = Join-Path $PackageDir 'install.ps1'
$exe = Join-Path $InstallDir 'vegaflow_daemon.exe'
$version = (Get-Content (Join-Path $PackageDir 'VERSION') -TotalCount 1).Trim()

if ($Shell -and (Test-Path $uninstallKey)) { throw 'Ya hay una entrada VegaFlow en Agregar o quitar programas: quítala antes de probar con -Shell.' }
if (Test-Path $InstallDir) { throw "$InstallDir ya existe: la prueba necesita una carpeta nueva." }
$before = Get-State
Write-Host "Paquete $version, instalación en $InstallDir$(if ($Shell) { ' (con Windows)' })"

Write-Host '1. Instalar con -DryRun no cambia nada'
$r = Invoke-Pkg $install (@('-InstallDir', $InstallDir, '-DryRun') + $shellArg)
Check 'dry-run termina bien' ($r.Code -eq 0) $r.Output
Check 'dry-run no crea la carpeta' (-not (Test-Path $InstallDir))
Check 'dry-run no toca Windows' ((Get-State | ConvertTo-Json) -eq ($before | ConvertTo-Json))

Write-Host '2. Un paquete modificado se rechaza antes de copiar nada'
$tampered = Join-Path $env:TEMP ('vf-tampered-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
Copy-Item $PackageDir $tampered -Recurse
[System.IO.File]::AppendAllText((Join-Path $tampered 'vegaflow_daemon.exe'), 'x')
$r = Invoke-Pkg (Join-Path $tampered 'install.ps1') (@('-InstallDir', $InstallDir) + $shellArg)
Check 'exe modificado: la instalación falla' ($r.Code -ne 0)
Check 'exe modificado: no se crea la carpeta' (-not (Test-Path $InstallDir))
Remove-Item $tampered -Recurse -Force

Write-Host '3. Instalar'
# F3.5: a folder another account prepared before the install (Everyone: Modify) must not keep that access.
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$planted = Get-Acl $InstallDir
$planted.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
            ([System.Security.Principal.SecurityIdentifier]'S-1-1-0'), 'Modify', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
Set-Acl $InstallDir $planted
$r = Invoke-Pkg $install (@('-InstallDir', $InstallDir) + $shellArg)
Check 'instalación termina bien' ($r.Code -eq 0) $r.Output
foreach ($f in 'vegaflow_daemon.exe', 'uninstall.ps1', 'VERSION', 'install.json', 'config\config.json') {
    Check "existe $f" (Test-Path (Join-Path $InstallDir $f))
}
Check '--version coincide con el paquete' ((& $exe --version) -match [regex]::Escape($version))
# Runs the installed daemon; its stderr is output to check, not a terminating error under 'Stop'.
function Invoke-Exe([string[]]$a) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & $exe @a 2>&1 | Out-String; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    [pscustomobject]@{ Code = $code; Output = $out }
}
# A mistyped option must stop the program, not start the real daemon (--dryrun used to run it for real).
$r = Invoke-Exe @('--no-such-option', '--version')
Check 'una opción desconocida se rechaza (código 2) y no sigue' ($r.Code -eq 2 -and $r.Output -notmatch [regex]::Escape($version)) "código $($r.Code)"
# --dry-run with --monitor must simulate, never run the real monitor cycle.
$r = Invoke-Exe @('--dry-run', '--monitor', '--once')
Check '--dry-run --monitor solo simula' ($r.Code -eq 0 -and $r.Output -match 'DRY-RUN SIMULATION' -and $r.Output -notmatch 'LIVE TELEMETRY DASHBOARD') $r.Output
# An option that needs a path must not take the next option as that path (--dxvk-install --dry-run lost the simulation).
foreach ($opt in '--dxvk-install', '--dxvk-remove') {
    $r = Invoke-Exe @('--dry-run', $opt, '--version')
    Check "$opt sin ruta se rechaza (código 2)" ($r.Code -eq 2 -and $r.Output -notmatch '\[DXVK\]') "código $($r.Code): $($r.Output)"
}
# An empty path is a missing one. '""' is how Windows PowerShell 5.1 hands an empty argument to a program.
$r = Invoke-Exe @('--dry-run', '--dxvk-install', '""')
Check '--dxvk-install con ruta vacía se rechaza (código 2)' ($r.Code -eq 2 -and $r.Output -notmatch '\[DXVK\]') "código $($r.Code): $($r.Output)"
# A dry run isolates nothing: it must say what it would do.
$r = Invoke-Exe @('--dry-run', '--once')
# With a game in the foreground the dry run takes its other branch ("Background Isolated Sets"), so accept either wording.
$sayWould = if ($r.Output -match 'Active 3D Game Detected') { 'Background Isolated Sets' } else { 'would be isolated' }
Check 'dry-run no dice que aisló' ($r.Output -notmatch 'Isolated in dry-run' -and $r.Output -match $sayWould) $r.Output
# --observe is safe mode by choice: the notice must not blame crashes.
$r = Invoke-Exe @('--observe', '--once')
Check '--observe explica su modo seguro' ($r.Code -eq 0 -and $r.Output -match '--observe' -and $r.Output -notmatch 'cierres anormales') $r.Output
# The monitor must honour safe mode too.
$r = Invoke-Exe @('--observe', '--monitor', '--once')
Check '--monitor respeta el modo seguro' ($r.Code -eq 0 -and $r.Output -match 'MODO SEGURO') $r.Output
# A fixed sleep before judging a background instance made these checks depend on how busy the machine was: the daemon
# is up once it owns its single-instance mutex, so wait for that (up to 30 s) instead of a guessed time.
function Wait-DaemonUp($proc, [int]$seconds = 30) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
        $m = $null
        if ([System.Threading.Mutex]::TryOpenExisting('Local\VegaFlowUniversal_SingleInstance', [ref]$m)) { $m.Dispose(); return $true }
        Start-Sleep -Milliseconds 200
    }
    $false
}
# Kills a background instance and waits until it is gone, so its mutex and its file lock cannot reach the next step.
function Stop-Daemon($proc) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; $null = $proc.WaitForExit(10000) }
# --elevate on an already elevated process must go on with --observe, not exit doing nothing.
$p = Start-Process -FilePath $exe -ArgumentList '--elevate', '--observe' -PassThru -WindowStyle Hidden
$up = Wait-DaemonUp $p
Start-Sleep -Seconds 1
$alive = $up -and -not $p.HasExited
Stop-Daemon $p
Check '--elevate --observe ya elevado sí observa (sigue corriendo)' $alive "salió con código $($p.ExitCode)"
# The arguments must reach the program as UTF-8: Windows hands main() the ANSI code page, which broke "ñ".
$oldEnc = [Console]::OutputEncoding
[Console]::OutputEncoding = [Text.Encoding]::UTF8
try { $r = Invoke-Exe @('--Año') } finally { [Console]::OutputEncoding = $oldEnc }
Check 'un argumento con ñ llega entero' ($r.Code -eq 2 -and $r.Output -match '--Año') $r.Output
# A second start while a windowless instance runs must not claim to focus a window.
$first = Start-Process -FilePath $exe -ArgumentList '--observe' -PassThru -WindowStyle Hidden
# Without the first instance up, the second would become the real one and never return, so it is not started.
if (Wait-DaemonUp $first) {
    $r = Invoke-Exe @('--observe')
    Check 'segundo arranque sin ventana lo dice' ($r.Code -eq 0 -and $r.Output -match 'already running' -and $r.Output -notmatch 'Focusing') $r.Output
} else {
    Check 'segundo arranque sin ventana lo dice' $false 'la primera instancia no tomó su mutex en 30 s'
}
Stop-Daemon $first
# Already elevated (this runs as Administrator), --elevate must not swallow --dry-run.
$r = Invoke-Exe @('--elevate', '--dry-run')
Check '--elevate --dry-run simula' ($r.Code -eq 0 -and $r.Output -match 'DRY-RUN SIMULATION') "código $($r.Code)"
$acl = Get-Acl $InstallDir
Check 'propietario: Administradores' ($acl.GetOwner([System.Security.Principal.SecurityIdentifier]).Value -eq 'S-1-5-32-544')
# WriteData 0x2, AppendData 0x4, WriteExtendedAttributes 0x10, WriteAttributes 0x100, Delete 0x10000, ChangePermissions 0x40000, TakeOwnership 0x80000
$writeMask = 0x2 -bor 0x4 -bor 0x10 -bor 0x100 -bor 0x10000 -bor 0x40000 -bor 0x80000
$usersWrite = @($acl.Access | Where-Object {
        $_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value -eq 'S-1-5-32-545' -and
        (([int]$_.FileSystemRights) -band $writeMask) -ne 0
    })
Check 'Users no puede escribir en la carpeta' ($usersWrite.Count -eq 0)
$foreignAce = @($acl.Access | Where-Object {
        $_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value -notin @('S-1-5-18', 'S-1-5-32-544', 'S-1-5-32-545')
    })
Check 'ninguna cuenta ajena conserva permisos en la carpeta' ($foreignAce.Count -eq 0) (($foreignAce | ForEach-Object { "$($_.IdentityReference)" }) -join ', ')
if ($Shell) {
    Check 'Agregar o quitar programas: versión' ((Get-ItemProperty $uninstallKey).DisplayVersion -eq $version)
    Check 'Agregar o quitar programas: ubicación' ((Get-ItemProperty $uninstallKey).InstallLocation -ieq $InstallDir)
    Check 'acceso del menú Inicio' (Test-Path $shortcut)
}

Write-Host '4. Reinstalar conserva la configuración del administrador'
$cfg = Join-Path $InstallDir 'config\config.json'
(Get-Content $cfg -Raw) -replace '"poll_interval_ms": \d+', '"poll_interval_ms": 2222' | Set-Content $cfg -Encoding ASCII
$autoArg = @(); if ($Shell) { $autoArg += '-Autostart' }
$r = Invoke-Pkg $install (@('-InstallDir', $InstallDir) + $shellArg + $autoArg)
Check 'reinstalación termina bien' ($r.Code -eq 0) $r.Output
Check 'config.json sigue con 2222' ((Get-Content $cfg -Raw) -match '"poll_interval_ms": 2222')
if ($Shell) {
    $task = Get-ScheduledTask -TaskName 'VegaFlow' -ErrorAction SilentlyContinue
    Check '-Autostart: la tarea apunta al exe instalado' ([bool]$task -and [bool]($task.Actions | Where-Object { $_.Execute -ieq $exe }))
}

Write-Host '5. Un cambio real con el daemon instalado'
& $exe --mpo-disable | Out-Null
Check 'MPO desactivado (OverlayTestMode = 5)' ((Get-Overlay) -eq 5) "valor: $(Get-Overlay)"
Check 'la copia del valor original queda en RegBackup' (@(Get-ChildItem 'HKLM:\SOFTWARE\VegaFlow\RegBackup' -ErrorAction SilentlyContinue).Count -gt 0)

Write-Host '6. Desinstalar -DryRun no cambia nada'
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @('-DryRun')
Check 'dry-run termina bien' ($r.Code -eq 0) $r.Output
Check 'el daemon sigue instalado' (Test-Path $exe)
Check 'el cambio de MPO sigue aplicado' ((Get-Overlay) -eq 5)

Write-Host '7. Desinstalar -KeepConfig devuelve el sistema y guarda la configuración'
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @('-KeepConfig')
Check 'desinstalación termina bien' ($r.Code -eq 0) $r.Output
Check 'MPO devuelto a como estaba' ((Get-Overlay) -eq $before.Overlay) "antes: $($before.Overlay), ahora: $(Get-Overlay)"
Check 'el daemon ya no está' (-not (Test-Path $exe))
Check 'config.json se conservó' ((Test-Path $cfg) -and ((Get-Content $cfg -Raw) -match '2222'))
Check 'RegBackup quedó vacío' (@(Get-ChildItem 'HKLM:\SOFTWARE\VegaFlow\RegBackup' -ErrorAction SilentlyContinue).Count -eq 0)
Check 'no queda entrada, acceso ni tarea' (-not (Get-State).Key -and -not (Get-State).Shortcut -and -not (Get-State).Task)

Write-Host '8. Instalar sobre la carpeta que dejó -KeepConfig, y desinstalar del todo'
$r = Invoke-Pkg $install (@('-InstallDir', $InstallDir) + $shellArg)
Check 'reinstalación termina bien' ($r.Code -eq 0) $r.Output
Check 'config.json de antes sigue con 2222' ((Get-Content $cfg -Raw) -match '"poll_interval_ms": 2222')
& $exe --mpo-disable | Out-Null
# A corrupt journal means game-file changes were not reverted: the uninstaller must stop, keep everything and be runnable again.
Set-Content (Join-Path $InstallDir 'config\change_journal.vfj') 'esto no es un diario' -Encoding ASCII
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @()
Check 'diario corrupto: la desinstalación se detiene' ($r.Code -ne 0)
Check 'diario corrupto: no se borró nada' ((Test-Path $exe) -and (Test-Path (Join-Path $InstallDir 'config\change_journal.vfj')) -and (Test-Path (Join-Path $InstallDir 'install.json')))
# The manifest must not steer deletions: a name with .. is ignored.
$victim = Join-Path (Split-Path $InstallDir -Parent) ('vf-victim-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.txt')
Set-Content $victim 'no me borres'
$mf = Get-Content (Join-Path $InstallDir 'install.json') -Raw | ConvertFrom-Json
$mf.files = @($mf.files) + ('..\' + (Split-Path $victim -Leaf))
$mf | ConvertTo-Json | Set-Content (Join-Path $InstallDir 'install.json') -Encoding UTF8
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @('-Force')
Check 'desinstalación completa (-Force) termina bien' ($r.Code -eq 0) $r.Output
Check 'un nombre con .. en install.json no borra fuera de la carpeta' (Test-Path $victim)
Remove-Item $victim -Force -ErrorAction SilentlyContinue
Check 'la carpeta desaparece' (-not (Test-Path $InstallDir)) $(if (Test-Path $InstallDir) { (Get-ChildItem $InstallDir -Force).Name -join ', ' })
Check 'MPO devuelto a como estaba' ((Get-Overlay) -eq $before.Overlay)

Write-Host '8b. Rutas y combinaciones rechazadas'
$outside = Join-Path $env:ProgramData ('vf-outside-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$r = Invoke-Pkg $install @('-InstallDir', $outside, '-NoShell')
Check 'fuera de Program Files sin -AllowOutsideProgramFiles falla' ($r.Code -ne 0)
Check '... y no crea la carpeta' (-not (Test-Path $outside))
$r = Invoke-Pkg $install @('-InstallDir', $outside, '-NoShell', '-Autostart', '-AllowOutsideProgramFiles')
Check '-Autostart con -NoShell falla' ($r.Code -ne 0)
Check '... y no crea la carpeta' (-not (Test-Path $outside))
$r = Invoke-Pkg $install @('-InstallDir', 'relativa\vf', '-NoShell', '-AllowOutsideProgramFiles')
Check 'una ruta relativa falla' ($r.Code -ne 0)

Write-Host '8c. Un enlace en logs\ no se sigue'
$r = Invoke-Pkg $install (@('-InstallDir', $InstallDir) + $shellArg)
Check 'instalación para la prueba del enlace' ($r.Code -eq 0) $r.Output
$linkTarget = Join-Path $env:TEMP ('vf-link-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $linkTarget | Out-Null
Set-Content (Join-Path $linkTarget 'mio.txt') 'importante'
$logsDir = Join-Path $InstallDir 'logs'
Remove-Item $logsDir -Recurse -Force
cmd /c mklink /J "$logsDir" "$linkTarget" | Out-Null
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @()
Check 'logs\ es un enlace: la desinstalación falla' ($r.Code -ne 0)
Check 'el destino del enlace no se tocó' (Test-Path (Join-Path $linkTarget 'mio.txt'))
cmd /c rmdir "$logsDir" | Out-Null
$r = Invoke-Pkg (Join-Path $InstallDir 'uninstall.ps1') @()
Check 'sin el enlace, la desinstalación termina bien' ($r.Code -eq 0) $r.Output
Remove-Item $linkTarget -Recurse -Force

Write-Host '9. Carpetas ajenas no se tocan'
$foreignDir = Join-Path $env:TEMP ('vf-foreign-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $foreignDir | Out-Null
Set-Content (Join-Path $foreignDir 'mio.txt') 'importante'
$r = Invoke-Pkg $install (@('-InstallDir', $foreignDir) + $shellArg)
Check 'instalar en una carpeta con archivos ajenos falla' ($r.Code -ne 0)
Check 'su archivo sigue ahí' (Test-Path (Join-Path $foreignDir 'mio.txt'))
$uninstallCopy = Join-Path $PackageDir 'uninstall.ps1'
$r = Invoke-Pkg $uninstallCopy @('-InstallDir', $foreignDir)
Check 'desinstalar una carpeta que no es de VegaFlow falla' ($r.Code -ne 0)
Check 'su archivo sigue ahí tras desinstalar' (Test-Path (Join-Path $foreignDir 'mio.txt'))
Remove-Item $foreignDir -Recurse -Force
$r = Invoke-Pkg $install @('-InstallDir', $env:ProgramFiles, '-NoShell')
Check 'instalar en C:\Program Files a secas falla' ($r.Code -ne 0)

Write-Host '10. El sistema quedó como al empezar'
$after = Get-State
Check 'plan de energía, MPO, entrada, acceso y tarea iguales' (($after | ConvertTo-Json) -eq ($before | ConvertTo-Json)) "antes: $($before | ConvertTo-Json -Compress) ahora: $($after | ConvertTo-Json -Compress)"

Write-Host ''
Write-Host "PACKAGE TESTS: $pass PASSED, $fail FAILED"
exit $(if ($fail -eq 0) { 0 } else { 1 })
