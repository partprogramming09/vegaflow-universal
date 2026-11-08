<#
.SYNOPSIS
    Installs or updates the vegaflow package in the WinBridge job runner and the VegaFlow files its jobs use.
.DESCRIPTION
    Requires administrator (an elevated SSH session works; there is no UAC). From a staging folder with
    manifest.json, scripts\*.ps1, tweaks_state.ps1, ab_profile.ps1 and busy.html:
      - copies the manifest and scripts to the runner's packages\vegaflow and runs Install-WinBridgeJobs -Update
        (recomputes the hashes; does not register tasks);
      - copies tweaks_state.ps1 and ab_profile.ps1 to C:\ProgramData\VegaFlow (used by the VegaFlow-* tasks) and busy.html to tools\;
      - copies the repository's compiled daemon if it is not running.
.PARAMETER Staging
    Folder with the files to install.
.PARAMETER Repo
    VegaFlow repository on the laptop (where the compiled daemon is taken from).
.PARAMETER JobsRoot
    WinBridge runner folder containing Install-WinBridgeJobs.ps1 and packages\.
.PARAMETER Root
    VegaFlow installation with the VegaFlow-* tasks and the tools.
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
