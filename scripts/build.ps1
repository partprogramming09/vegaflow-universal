<#
.SYNOPSIS
    Compila VegaFlow Universal Daemon y Suite de Pruebas.
.DESCRIPTION
    Script de compilación automatizada para VegaFlow Universal con g++ (MinGW-w64).
    Soporta ejecución opcional de la suite de pruebas mediante el parámetro -RunTests.
.PARAMETER RunTests
    Ejecuta vegaflow_tests.exe tras finalizar con éxito la compilación.
.EXAMPLE
    .\scripts\build.ps1
.EXAMPLE
    .\scripts\build.ps1 -RunTests
#>
[CmdletBinding()]
param(
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "  VegaFlow Universal - Pipeline de Compilacion      " -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

# 1. Deteccion de Compilador g++
$gpp = $null

if (Get-Command "g++" -ErrorAction SilentlyContinue) {
    $gpp = "g++"
} elseif (Test-Path "$env:USERPROFILE\scoop\apps\mingw\current\bin\g++.exe") {
    $gpp = "$env:USERPROFILE\scoop\apps\mingw\current\bin\g++.exe"
} elseif (Test-Path "C:\msys64\mingw64\bin\g++.exe") {
    $gpp = "C:\msys64\mingw64\bin\g++.exe"
} else {
    Write-Error "No se encontro el compilador g++. Asegurese de tener MinGW instalado en PATH o via Scoop."
    exit 1
}

$compilerVer = (& $gpp --version | Select-Object -First 1)
Write-Host "[+] Compilador detectado: $compilerVer" -ForegroundColor Green
Write-Host "[+] Ruta: $gpp" -ForegroundColor DarkGray

# 2. Asegurar directorio build
$BuildDir = Join-Path $ProjectRoot "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# 3. Parametros y fuentes comunes
$IncludeFlags = @(
    "-Iinclude",
    "-Iinclude/core",
    "-Iinclude/optimizers",
    "-Iinclude/security",
    "-Iinclude/gui"
)

$CommonSources = @(
    "src/core/privileges.cpp",
    "src/core/hardware_profiler.cpp",
    "src/core/process_detector.cpp",
    "src/core/system_info.cpp",
    "src/optimizers/graphics_optimizer.cpp",
    "src/optimizers/dxvk_manager.cpp",
    "src/optimizers/steam_engine_optimizer.cpp",
    "src/optimizers/fsr_scaler.cpp",
    "src/optimizers/cpu_orchestrator.cpp",
    "src/optimizers/memory_manager.cpp",
    "src/security/sandbox_guard.cpp",
    "src/security/frame_telemetry.cpp",
    "src/gui/gui_components.cpp",
    "src/gui/gui_tabs.cpp",
    "src/gui/gui_window.cpp"
)

$Libs = @(
    "-lntdll",
    "-ldxgi",
    "-ladvapi32",
    "-lpsapi",
    "-luser32",
    "-lole32",
    "-lgdi32",
    "-lshell32",
    "-ldwmapi",
    "-ltdh"
)

$BaseFlags = @(
    "-std=c++20",
    "-O2",
    "-Wall",
    "-Wextra",
    "-static",
    "-static-libgcc",
    "-static-libstdc++"
)

# 4. Compilacion de vegaflow_daemon.exe
Write-Host "`n[*] Compilando vegaflow_daemon.exe..." -ForegroundColor Yellow
$DaemonSources = $CommonSources + @("src/main.cpp")
$DaemonOutput = "build/vegaflow_daemon.exe"
$DaemonArgs = $BaseFlags + $IncludeFlags + $DaemonSources + @("-o", $DaemonOutput) + $Libs

& $gpp @DaemonArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_daemon.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_daemon.exe compilado exitosamente." -ForegroundColor Green

# 5. Compilacion de vegaflow_tests.exe
Write-Host "`n[*] Compilando vegaflow_tests.exe..." -ForegroundColor Yellow
$TestSources = $CommonSources + @("tests/test_main.cpp")
$TestOutput = "build/vegaflow_tests.exe"
$TestArgs = $BaseFlags + $IncludeFlags + $TestSources + @("-o", $TestOutput) + $Libs

& $gpp @TestArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_tests.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_tests.exe compilado exitosamente." -ForegroundColor Green

# 6. Ejecucion de Pruebas (si se solicito)
if ($RunTests) {
    Write-Host "`n=====================================================" -ForegroundColor Cyan
    Write-Host "  Ejecutando Suite de Pruebas Unitarias             " -ForegroundColor Cyan
    Write-Host "=====================================================" -ForegroundColor Cyan
    
    $TestExe = Join-Path $BuildDir "vegaflow_tests.exe"
    & $TestExe
    $testExit = $LASTEXITCODE
    
    # Limpieza de archivo temporal de prueba generado por Test 10
    $TestLog = Join-Path $BuildDir "test_session_report.log"
    if (Test-Path $TestLog) {
        Remove-Item -Path $TestLog -Force -ErrorAction SilentlyContinue
    }

    if ($testExit -ne 0) {
        Write-Error "Fallo la suite de pruebas (Codigo de salida: $testExit)"
        exit $testExit
    }
}

Write-Host "`n[+] Proceso completado exitosamente." -ForegroundColor Green
exit 0
