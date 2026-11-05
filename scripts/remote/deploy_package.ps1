<#
.SYNOPSIS
    Instala o actualiza el paquete vegaflow en el ejecutor de trabajos de WinBridge y los archivos de VegaFlow que usan sus trabajos.
.DESCRIPTION
    Requiere administrador (una sesion SSH elevada sirve; no hay UAC). Desde una carpeta de paso (Staging) con
    manifest.json, scripts\*.ps1, tweaks_state.ps1, ab_profile.ps1 y busy.html:
      - copia el manifiesto y los scripts a packages\vegaflow del ejecutor y ejecuta Install-WinBridgeJobs -Update
        (recalcula los hashes; no registra tareas);
      - copia tweaks_state.ps1 y ab_profile.ps1 a C:\ProgramData\VegaFlow (los usan las tareas VegaFlow-*) y busy.html a tools\;
      - copia el daemon compilado del repositorio si no esta corriendo.
.PARAMETER Staging
    Carpeta con los archivos a instalar.
.PARAMETER Repo
    Repositorio de VegaFlow en el laptop (de donde se toma el daemon compilado).
.PARAMETER JobsRoot
    Carpeta del ejecutor de WinBridge que contiene Install-WinBridgeJobs.ps1 y packages\.
.PARAMETER Root
    Instalacion de VegaFlow con las tareas VegaFlow-* y las herramientas.
#>
param(
    [string]$Staging = 'C:\dev\vf-pkg',
    [string]$Repo = 'C:\Users\testuser\OneDrive\Escritorio\proyectos\direct',
    [string]$JobsRoot = 'C:\Users\testuser\AppData\Local\winbridge\bin\jobs',
    [string]$Root = 'C:\ProgramData\VegaFlow'
)

$ErrorActionPreference = 'Stop'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Requiere administrador.' }

$pkg = Join-Path $JobsRoot 'packages\vegaflow'
New-Item -ItemType Directory -Force -Path (Join-Path $pkg 'scripts') | Out-Null
Copy-Item (Join-Path $Staging 'manifest.json') $pkg -Force
Copy-Item (Join-Path $Staging 'scripts\*.ps1') (Join-Path $pkg 'scripts') -Force

foreach ($f in 'tweaks_state.ps1', 'ab_profile.ps1') { Copy-Item (Join-Path $Staging $f) (Join-Path $Root $f) -Force }
Copy-Item (Join-Path $Staging 'busy.html') (Join-Path $Root 'tools\busy.html') -Force

$daemon = Join-Path $Repo 'build\vegaflow_daemon.exe'
if (-not (Test-Path $daemon)) {
    'daemon: no hay build en el repositorio, no se copia'
} elseif (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) {
    'daemon: esta corriendo, no se copia'
} else {
    Copy-Item $daemon (Join-Path $Root 'vegaflow_daemon.exe') -Force
    'daemon: copiado'
}

& (Join-Path $JobsRoot 'Install-WinBridgeJobs.ps1') -Packages (Join-Path $JobsRoot 'packages') -AgentUser testuser -Update
