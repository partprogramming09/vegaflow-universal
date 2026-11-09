<#
.SYNOPSIS
    Builds the VegaFlow Universal Daemon and Test Suite.
.DESCRIPTION
    Automated build script for VegaFlow Universal using g++ (MinGW-w64).
    Optionally runs the test suite via the -RunTests parameter.
.PARAMETER RunTests
    Runs vegaflow_tests.exe after the build completes successfully.
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

# 1. Detect the g++ and cargo compilers
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

# Add directories to the PATH for resolving internal tools if needed
$gppDir = Split-Path -Parent $gpp
if ($gppDir -and ($env:PATH -split ';' -notcontains $gppDir)) {
    $env:PATH = "$gppDir;$env:PATH"
}
$cargoDir = Split-Path -Parent $cargo
if ($cargoDir -and ($env:PATH -split ';' -notcontains $cargoDir)) {
    $env:PATH = "$cargoDir;$env:PATH"
}

# 2. Ensure the build directory exists
$BuildDir = Join-Path $ProjectRoot "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# 3. Build the Rust Core Library (libvegaflow_core.a)
$CargoToml = Join-Path $ProjectRoot "rust/vegaflow-core/Cargo.toml"

if ($RunTests) {
    Write-Host "`n[*] Ejecutando cargo test (vegaflow-core)..." -ForegroundColor Yellow
    & $cargo test --manifest-path $CargoToml --target x86_64-pc-windows-gnu
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Fallo cargo test en vegaflow-core (Codigo de salida: $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
    Write-Host "[OK] cargo test superado exitosamente." -ForegroundColor Green
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

# 4. Common parameters and sources
$IncludeFlags = @("-Isrc")

# Sources are found on disk, the same way scripts/build-cross.sh does, so moving or adding a .cpp needs no edit here.
# src/app/ holds the daemon entry points; everything else in src/ is shared by the daemon and the test suite.
function Get-CppSources([string]$dir) {
    # -Filter also matches 8.3 short names (e.g. .cppx), so the extension is checked exactly.
    Get-ChildItem -Path (Join-Path $ProjectRoot $dir) -Recurse -File | Where-Object { $_.Extension -eq '.cpp' } |
        Sort-Object FullName | ForEach-Object { $_.FullName }
}
$AppSources = @(Get-CppSources "src/app")
$CommonSources = @(Get-CppSources "src" | Where-Object { $AppSources -notcontains $_ })

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

# 5. Build vegaflow_daemon.exe
Write-Host "`n[*] Compilando vegaflow_daemon.exe..." -ForegroundColor Yellow
$DaemonSources = $CommonSources + $AppSources
$DaemonOutput = "build/vegaflow_daemon.exe"
$DaemonArgs = $BaseFlags + $IncludeFlags + $DaemonSources + @($RustLib, "-o", $DaemonOutput) + $Libs

& $gpp @DaemonArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_daemon.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_daemon.exe compilado exitosamente." -ForegroundColor Green

# 6. Build vegaflow_tests.exe
Write-Host "`n[*] Compilando vegaflow_tests.exe..." -ForegroundColor Yellow
$TestSources = $CommonSources + @(Get-CppSources "tests")
$TestOutput = "build/vegaflow_tests.exe"
$TestArgs = $BaseFlags + $IncludeFlags + $TestSources + @($RustLib, "-o", $TestOutput) + $Libs

& $gpp @TestArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilacion de vegaflow_tests.exe (Codigo de salida: $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Write-Host "[OK] vegaflow_tests.exe compilado exitosamente." -ForegroundColor Green

# 7. Run the tests (if requested)
if ($RunTests) {
    Write-Host "`n=====================================================" -ForegroundColor Cyan
    Write-Host "  Ejecutando Suite de Pruebas Unitarias             " -ForegroundColor Cyan
    Write-Host "=====================================================" -ForegroundColor Cyan
    
    $TestExe = Join-Path $BuildDir "vegaflow_tests.exe"
    & $TestExe
    $testExit = $LASTEXITCODE
    
    # Clean up the temporary test file generated by Test 10
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
