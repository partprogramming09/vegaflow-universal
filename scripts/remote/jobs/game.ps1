<#
.SYNOPSIS
    Abre bgfx drawstress en el escritorio (sesion 1) con la API y el tamano indicados.
.PARAMETER Api
    d3d11 o d3d12.
.PARAMETER Dim
    Lado de la cuadricula de cubos: dim^3 draw calls por cuadro.
#>
param([string]$Api = 'd3d11', [int]$Dim = 30)

$ErrorActionPreference = 'Stop'
$exe = 'C:\ProgramData\VegaFlow\tools\drawstress\drawstress.exe'
Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
$env:DRAWSTRESS_DIM = $Dim
$p = Start-Process -FilePath $exe -ArgumentList "--$Api" -WorkingDirectory (Split-Path $exe) -PassThru
Start-Sleep -Seconds 8
$g = Get-Process drawstress -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $g) { throw 'drawstress salio al arrancar.' }
'drawstress pid={0} sesion={1} api={2} dim={3}' -f $g.Id, $g.SessionId, $Api, $Dim
