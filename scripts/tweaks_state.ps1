<#
.SYNOPSIS
    Shows the state of the VegaFlow registry tweaks against the Windows stock value.
.DESCRIPTION
    Read-only by default. With -Restore it simulates restoring the values marked as safe;
    with -Restore -Apply it performs the restore (requires administrator). Values without a confirmed
    stock value are only reported and never touched.
.PARAMETER Restore
    Computes which values would be restored.
.PARAMETER Apply
    With -Restore, writes the changes to the registry.
#>
param(
    [switch]$Restore,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

$dwm = 'HKLM:\SOFTWARE\Microsoft\Windows\Dwm'
$mem = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management'
$sys = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile'
$games = "$sys\Tasks\Games"
$prio = 'HKLM:\SYSTEM\CurrentControlSet\Control\PriorityControl'
$tcp = 'HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters'
$amd0 = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000'

# Path, Name, Stock (stock value; $null = does not exist), Restorable (safe to restore)
$entries = @(
    @($dwm, 'OverlayTestMode', $null, $true),
    @($mem, 'DisablePagingExecutive', 0, $true),
    @($mem, 'LargeSystemCache', 0, $true),
    @($sys, 'SystemResponsiveness', 20, $true),
    @($sys, 'NetworkThrottlingIndex', 10, $true),
    @($prio, 'Win32PrioritySeparation', 2, $true),
    @($games, 'GPU Priority', 8, $true),
    @($games, 'Priority', 2, $true),
    @($games, 'Scheduling Category', 'Medium', $true),
    @($games, 'SFIO Priority', 'Normal', $true),
    @($games, 'Clock Rate', 10000, $true),
    @($tcp, 'TcpAckFrequency', $null, $true),
    @($tcp, 'TCPNoDelay', $null, $true),
    @('HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem', 'NtfsMemoryUsage', $null, $true),
    @('HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem', 'NtfsDisableLastAccessUpdate', 2147483650, $false),
    @($amd0, 'KMD_EnableSAM', $null, $true),
    @($amd0, 'LargePageSupport', $null, $true)
)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($Restore -and $Apply -and -not $isAdmin) { throw 'Restaurar con -Apply requiere una consola de administrador.' }

# With -Restore -Apply, the values VegaFlow saved before changing them (SOFTWARE\VegaFlow\RegBackup) go back to what was there, not to the stock value.
$backedUp = @{}
if ($Restore -and $Apply) {
    foreach ($r in @(Get-ChildItem 'HKLM:\SOFTWARE\VegaFlow\RegBackup' -ErrorAction SilentlyContinue)) {
        $v = Get-ItemProperty $r.PSPath
        $backedUp[('{0}|{1}' -f $v.subkey.ToLower(), $v.name.ToLower())] = $true
    }
    $daemonExe = 'C:\ProgramData\VegaFlow\vegaflow_daemon.exe'
    if ($backedUp.Count -gt 0 -and (Test-Path $daemonExe)) { & $daemonExe --restore-registry | ForEach-Object { "daemon: $_" } }
}

function Get-Current($path, $name) {
    $p = Get-ItemProperty -Path $path -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $null }
    return $p.$name
}

'{0,-26} {1,-14} {2,-10} {3}' -f 'valor', 'actual', 'fabrica', 'estado'
foreach ($e in $entries) {
    $path, $name, $stock, $safe = $e
    $cur = Get-Current $path $name
    $diff = ($null -ne $cur) -and ($cur -ne $stock)
    $state = if ($diff) { if ($safe) { 'DIFERENTE' } else { 'DIFERENTE (solo informe)' } } else { 'ok' }
    '{0,-26} {1,-14} {2,-10} {3}' -f $name, $(if ($null -eq $cur) { '(ausente)' } else { $cur }), $(if ($null -eq $stock) { '(ausente)' } else { $stock }), $state

    $key = '{0}|{1}' -f (($path -replace '^HKLM:\\', '').ToLower()), $name.ToLower()
    if ($Restore -and $diff -and $safe -and $backedUp.ContainsKey($key)) {
        '  -> restaurado desde la copia guardada: ' + $name
    } elseif ($Restore -and $diff -and $safe) {
        $action = if ($null -eq $stock) { "borrar $name" } else { "escribir $name = $stock" }
        if ($Apply) {
            if ($null -eq $stock) { Remove-ItemProperty -Path $path -Name $name } else { Set-ItemProperty -Path $path -Name $name -Value $stock }
            "  -> hecho: $action"
        } else {
            "  -> simulacion: $action"
        }
    }
}

$mm = Get-MMAgent -ErrorAction SilentlyContinue
if ($mm) { 'MemoryCompression         {0,-14} {1,-10} {2}' -f $mm.MemoryCompression, 'True', $(if ($mm.MemoryCompression) { 'ok' } else { 'DIFERENTE (solo informe)' }) }

$amd = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}'
foreach ($sub in '0000', '0001', '0002', '0003') {
    $k = Get-ItemProperty -Path "$amd\$sub" -ErrorAction SilentlyContinue
    if ($null -eq $k -or $null -eq $k.DriverDesc) { continue }
    "AMD/GPU $sub : $($k.DriverDesc)"
    foreach ($n in 'EnableUlps', 'ShaderCache') {
        $v = $k.$n
        if ($null -ne $v) { "  $n = $v (solo informe)" }
    }
    $umd = Get-ItemProperty -Path "$amd\$sub\UMD" -ErrorAction SilentlyContinue
    if ($umd) {
        foreach ($n in 'Tessellation', 'Tessellation_OPTION', 'SurfaceFormatReplacements', 'FlipQueueSize', 'TFQ', 'Main3D_DEF', 'TurboSync') {
            $v = $umd.$n
            if ($null -ne $v) { "  UMD\$n = $(if ($v -is [byte[]]) { [Text.Encoding]::ASCII.GetString($v).TrimEnd([char]0) } else { $v }) (solo informe)" }
        }
    }
}

$ugp = Get-ItemProperty 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences' -ErrorAction SilentlyContinue
if ($ugp) {
    $names = $ugp.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' }
    "UserGpuPreferences: $(@($names).Count) entradas (solo informe)"
}

# Reads the AC value of a setting in the active plan: the last two 0x figures (AC and DC), independent of language.
function Get-PowerAc($subgroup, $alias) {
    $hex = @(powercfg /qh SCHEME_CURRENT $subgroup $alias | Select-String '0x[0-9a-fA-F]+' | ForEach-Object { $_.Matches[0].Value })
    if ($hex.Count -lt 2) { return $null }
    [Convert]::ToInt32($hex[-2], 16)
}
# Subgroup, alias, value of this laptop's Balanced plan (report only: no confirmed stock value on other machines).
$powerEntries = @(
    @('SUB_PROCESSOR', 'CPMINCORES', 50),
    @('SUB_PROCESSOR', 'PERFEPP', 50),
    @('SUB_PROCESSOR', 'PERFBOOSTMODE', 2),
    @('SUB_PCIEXPRESS', 'ASPM', 1)
)
foreach ($pe in $powerEntries) {
    $sub, $alias, $stock = $pe
    $cur = Get-PowerAc $sub $alias
    $state = if ($null -ne $cur -and $cur -ne $stock) { 'DIFERENTE (solo informe)' } else { 'ok' }
    '{0,-26} {1,-14} {2,-10} {3}' -f "energia:$alias", $(if ($null -eq $cur) { '(ausente)' } else { $cur }), $stock, $state
}

$gcs = Get-ItemProperty 'HKCU:\System\GameConfigStore' -ErrorAction SilentlyContinue
if ($gcs) {
    foreach ($n in 'GameDVR_Enabled', 'GameDVR_FSEBehaviorMode', 'GameDVR_DXGIHonorFSEWindowsCompatible') {
        if ($null -ne $gcs.$n) { "  GameConfigStore\$n = $($gcs.$n) (solo informe)" }
    }
}
$integrity = (whoami /groups | Select-String 'Mandatory Label') -replace '\s+', ' '
"integridad=$integrity"
"admin=$isAdmin  modo=$(if ($Restore) { if ($Apply) { 'restaurar' } else { 'simulacion' } } else { 'lectura' })"
