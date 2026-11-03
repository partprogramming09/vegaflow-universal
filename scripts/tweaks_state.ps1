<#
.SYNOPSIS
    Muestra el estado de los tweaks de registro de VegaFlow frente al valor de fabrica de Windows.
.DESCRIPTION
    Por defecto solo lee. Con -Restore simula la restauracion de los valores marcados como seguros;
    con -Restore -Apply la ejecuta (requiere administrador). Los valores sin valor de fabrica
    confirmado solo se informan y nunca se tocan.
.PARAMETER Restore
    Calcula que valores se restaurarian.
.PARAMETER Apply
    Con -Restore, escribe los cambios en el registro.
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

# Path, Name, Stock (valor de fabrica; $null = no existe), Restorable (seguro de restaurar)
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

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrator')
if ($Restore -and $Apply -and -not $isAdmin) { throw 'Restaurar con -Apply requiere una consola de administrador.' }

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

    if ($Restore -and $diff -and $safe) {
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
"admin=$isAdmin  modo=$(if ($Restore) { if ($Apply) { 'restaurar' } else { 'simulacion' } } else { 'lectura' })"
