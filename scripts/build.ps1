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

# 1. Deteccion de Compilador g++ y cargo
$gpp = $null

if (Get-Command "g++" -ErrorAction SilentlyContinue) {
    $gpp = (Get-Command "g++").Source
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

$cargo = $null
if (Get-Command "cargo" -ErrorAction SilentlyContinue) {
    $cargo = (Get-Command "cargo").Source
} elseif (Test-Path "$env:USERPROFILE\.cargo\bin\cargo.exe") {
    $cargo = "$env:USERPROFILE\.cargo\bin\cargo.exe"
} else {
    Write-Error "No se encontro cargo. Asegurese de tener Rust instalado en PATH o en %USERPROFILE%\.cargo\bin."
    exit 1
}

$cargoVer = (& $cargo --version | Select-Object -First 1)
Write-Host "[+] Cargo detectado: $cargoVer" -ForegroundColor Green
Write-Host "[+] Ruta: $cargo" -ForegroundColor DarkGray

# Agregar directorios al PATH para resolucion de herramientas internas si es necesario
$gppDir = Split-Path -Parent $gpp
if ($gppDir -and ($env:PATH -split ';' -notcontains $gppDir)) {
    $env:PATH = "$gppDir;$env:PATH"
}
$cargoDir = Split-Path -Parent $cargo
if ($cargoDir -and ($env:PATH -split ';' -notcontains $cargoDir)) {
    $env:PATH = "$cargoDir;$env:PATH"
}

# 2. Asegurar directorio build
$BuildDir = Join-Path $ProjectRoot "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# 3. Compilacion de Rust Core Library (libvegaflow_core.a)
$CargoToml = Join-Path $ProjectRoot "rust/vegaflow-core/Cargo.toml"

if ($RunTests) {
    Write-Host "`n[*] Ejecutando cargo test (vegaflow-core)..." -ForegroundColor Yellow
    & $cargo test --manifest-path $CargoToml --target x86_64-pc-windows-gnu
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Fallo cargo test en vegaflow-core (Codigo de salida: $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
    Write-Host "[OK] cargo test superado exitosamente (15/15 pruebas)." -ForegroundColor Green
}

Write-Host "`n[*] Compilando libreria estatica de Rust (libvegaflow_core.a)..." -ForegroundColor Yellow
& $cargo build --release --manifest-path $CargoToml --target x86_64-pc-windows-gnu
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de libvegaflow_core.a (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}

$RustLib = Join-Path $ProjectRoot "rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a"
if (-not (Test-Path $RustLib)) {
    Write-Error "No se encontro la libreria estatica de Rust en: $RustLib"
    exit 1
}
Write-Host "[OK] libvegaflow_core.a generada exitosamente." -ForegroundColor Green

# 4. Parametros y fuentes comunes
$IncludeFlags = @("-Isrc")

$CommonSources = @(
    "src/windows/privileges.cpp",
    "src/detect/hardware_profiler.cpp",
    "src/detect/process_detector.cpp",
    "src/detect/system_info.cpp",
    "src/optimize/graphics/amd_driver_tweaks.cpp",
    "src/optimize/graphics/display_tweaks.cpp",
    "src/optimize/graphics/dxgi_flip.cpp",
    "src/optimize/graphics/kernel_tweaks.cpp",
    "src/optimize/graphics/mmcss.cpp",
    "src/optimize/graphics/power_mode.cpp",
    "src/optimize/graphics/registry_backup.cpp",
    "src/optimize/graphics/shader_cache.cpp",
    "src/optimize/graphics/timer.cpp",
    "src/optimize/graphics/tweaks_query.cpp",
    "src/optimize/graphics/vram_reporting.cpp",
    "src/optimize/dxvk_manager.cpp",
    "src/optimize/steam/apu_shadow_config.cpp",
    "src/optimize/steam/engine_detection.cpp",
    "src/optimize/steam/engine_presets.cpp",
    "src/optimize/steam/steam_internal.cpp",
    "src/optimize/steam/steam_library.cpp",
    "src/optimize/fsr/fsr_internal.cpp",
    "src/optimize/fsr/fsr_scaler.cpp",
    "src/optimize/fsr/render_loop.cpp",
    "src/optimize/cpu_orchestrator.cpp",
    "src/optimize/memory_manager.cpp",
    "src/safety/sandbox_guard.cpp",
    "src/safety/frame_telemetry.cpp",
    "src/ui/gui_components.cpp",
    "src/ui/gui_tabs.cpp",
    "src/ui/gui_window.cpp"
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
    "-ltdh",
    "-lws2_32",
    "-luserenv",
    "-lbcrypt"
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

# 5. Compilacion de vegaflow_daemon.exe
Write-Host "`n[*] Compilando vegaflow_daemon.exe..." -ForegroundColor Yellow
$DaemonSources = $CommonSources + @(
    "src/app/main.cpp",
    "src/app/app_state.cpp",
    "src/app/cli.cpp",
    "src/app/daemon_loop.cpp",
    "src/app/gui_mode.cpp",
    "src/app/config.cpp",
    "src/app/monitor.cpp",
    "src/app/reports.cpp",
    "src/app/run_cycle.cpp",
    "src/app/text_convert.cpp"
)
$DaemonOutput = "build/vegaflow_daemon.exe"
$DaemonArgs = $BaseFlags + $IncludeFlags + $DaemonSources + @($RustLib, "-o", $DaemonOutput) + $Libs

& $gpp @DaemonArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_daemon.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_daemon.exe compilado exitosamente." -ForegroundColor Green

# 6. Compilacion de vegaflow_tests.exe
Write-Host "`n[*] Compilando vegaflow_tests.exe..." -ForegroundColor Yellow
$TestSources = $CommonSources + @("tests/test_main.cpp")
$TestOutput = "build/vegaflow_tests.exe"
$TestArgs = $BaseFlags + $IncludeFlags + $TestSources + @($RustLib, "-o", $TestOutput) + $Libs

& $gpp @TestArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_tests.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_tests.exe compilado exitosamente." -ForegroundColor Green

# 7. Ejecucion de Pruebas (si se solicito)
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
