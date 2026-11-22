<#
.SYNOPSIS
    Closes Octopath Traveler II and puts back the Engine.ini that the octo job saved before writing its variants.
.DESCRIPTION
    Stop-Process on the game (a kill, so the game does not write its config on the way out), then, if the octo job left
    its snapshot in C:\ProgramData\VegaFlow\octo, copies it back over the game's Engine.ini, checks the SHA-256 and
    deletes the snapshot (a stale snapshot must never overwrite a later, legitimate change). Safe to run twice.
#>
$ErrorActionPreference = 'Stop'
$exe = 'Octopath_Traveler2-Win64-Shipping'
$ini = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Octopath_Traveler2\Saved\Config\WindowsNoEditor\Engine.ini'
$snap = 'C:\ProgramData\VegaFlow\octo\Engine.orig.ini'

$n = @(Get-Process $exe -ErrorAction SilentlyContinue).Count
Get-Process $exe -ErrorAction SilentlyContinue | Stop-Process -Force
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Process $exe -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (Get-Process $exe -ErrorAction SilentlyContinue) { throw 'El juego sigue abierto tras Stop-Process.' }
"juego cerrado=$n"

if (Test-Path -LiteralPath $snap) {
    Copy-Item -LiteralPath $snap -Destination $ini -Force
    $a = (Get-FileHash -LiteralPath $snap).Hash
    $b = (Get-FileHash -LiteralPath $ini).Hash
    if ($a -ne $b) { throw "Engine.ini restaurado no coincide con la copia ($b, copia $a); la copia se conserva en $snap." }
    Remove-Item -LiteralPath $snap -Force
    "engine_ini=restaurado sha256=$b"
} else {
    "engine_ini=sin copia del job (no se toco) sha256=$((Get-FileHash -LiteralPath $ini).Hash)"
}
