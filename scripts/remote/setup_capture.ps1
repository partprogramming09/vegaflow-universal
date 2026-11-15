<#
.SYNOPSIS
    Installs the screen-capture tool the `screenshot` job uses: a private Python plus a venv with dxcam (Desktop Duplication).
.DESCRIPTION
    Requires administrator (an elevated SSH session works). GDI CopyFromScreen reads black when a game is presented
    with a hardware flip; Desktop Duplication sees it. Without this tool the job falls back to GDI ("method=gdi").
    The job runs elevated, so everything it executes must be writable by administrators only:
      - the tool lives under C:\ProgramData\VegaFlow\tools\capture (the script refuses to go on if users can write there);
      - the venv only holds site-packages; its interpreter and standard library come from the Python in `pyvenv.cfg`.
        A per-user Python (uv, %LOCALAPPDATA%) is user-writable, so its folder is COPIED to tools\capture\python and the
        venv is built from the copy. The copy trusts that Python as it is at install time: run this on a clean machine state;
      - the packages are pinned to the versions tested on 2026-11-15 (no hashes: a new PyPI release is not picked up silently).
.PARAMETER Root
    VegaFlow installation folder (administrator-only).
#>
param([string]$Root = 'C:\ProgramData\VegaFlow')

$ErrorActionPreference = 'Stop'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Requiere administrador.' }

# Users (S-1-5-32-545), Everyone (S-1-1-0) or Authenticated Users (S-1-5-11) with write permission on the folder.
function Test-UserWritable([string]$Path) {
    $writeRights = [Security.AccessControl.FileSystemRights]'WriteData,AppendData,Delete,WriteAttributes,WriteExtendedAttributes,ChangePermissions,TakeOwnership'
    $weak = (Get-Acl $Path).Access | Where-Object {
        $_.AccessControlType -eq 'Allow' -and
        ($_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -in 'S-1-5-32-545', 'S-1-1-0', 'S-1-5-11') -and
        (($_.FileSystemRights -band $writeRights) -ne 0)
    }
    [bool]$weak
}

$dir = Join-Path $Root 'tools\capture'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
if (Test-UserWritable $dir) { throw "$dir lo pueden escribir usuarios sin permisos de administrador; ejecuta antes scripts\setup_tasks.ps1 (protege $Root)." }

$basePrefix = (python -c 'import sys; print(sys.base_prefix)').Trim()
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $basePrefix)) { throw 'No se encontro Python 3 en el PATH.' }
$pyHome = Join-Path $dir 'python'
robocopy $basePrefix $pyHome /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy del Python fallo (codigo $LASTEXITCODE)." }
if (Test-UserWritable $pyHome) { throw "$pyHome es escribible por usuarios." }

$venv = Join-Path $dir 'venv'
if (Test-Path -LiteralPath $venv) { Remove-Item -LiteralPath $venv -Recurse -Force }
& (Join-Path $pyHome 'python.exe') -m venv $venv
if ($LASTEXITCODE -ne 0) { throw 'No se pudo crear el venv.' }
$home_ = ((Get-Content (Join-Path $venv 'pyvenv.cfg') | Where-Object { $_ -match '^home\s*=' }) -split '=', 2)[1].Trim()
if (-not $home_.StartsWith($dir, [StringComparison]::OrdinalIgnoreCase)) { throw "El venv usa un Python fuera de $dir ($home_)." }

$py = Join-Path $venv 'Scripts\python.exe'
& $py -m pip install --quiet --disable-pip-version-check dxcam==0.3.0 pillow==13.0.0 opencv-python-headless==5.0.0.93 numpy==2.5.4 comtypes==1.5.0
if ($LASTEXITCODE -ne 0) { throw 'pip install fallo.' }
Copy-Item (Join-Path $PSScriptRoot 'capture_dda.py') (Join-Path $dir 'capture_dda.py') -Force
& $py -m pip list --disable-pip-version-check | Select-String 'dxcam|numpy|pillow|comtypes|opencv'
'listo: ' + $dir
