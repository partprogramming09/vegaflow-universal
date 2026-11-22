<#
.SYNOPSIS
    Runs tests/package/package_tests.ps1 against a zip staged by `vfdev package-test` and keeps the whole output in a dated file.
.DESCRIPTION
    The zip and the script are in C:\ProgramData\VegaFlow\pkgtest (administrators only: this job runs elevated, so it must
    not run files a normal user could have replaced). Both are checked against the SHA-256 the Mac sent before anything runs.
    The zip is unpacked into a fresh folder each time. Exit code: 0 all checks passed, 1 a check failed, 2 the staging is
    missing or does not match its hash (nothing was run).
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ZipSha256,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ScriptSha256
)
$ErrorActionPreference = 'Stop'
$root = 'C:\ProgramData\VegaFlow\pkgtest'
$zip = Join-Path $root 'package.zip'
$script = Join-Path $root 'package_tests.ps1'
foreach ($f in @(@($zip, $ZipSha256), @($script, $ScriptSha256))) {
    if (-not (Test-Path -LiteralPath $f[0])) { "falta $($f[0]): corre ``vfdev package-test``, que lo copia"; exit 2 }
    $h = (Get-FileHash -LiteralPath $f[0] -Algorithm SHA256).Hash
    if ($h -ne $f[1]) { "SHA-256 de $(Split-Path $f[0] -Leaf) no coincide: esperado $($f[1]), hay $h"; exit 2 }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path $root 'out'
$work = Join-Path $root "run-$stamp"
New-Item -ItemType Directory -Force -Path $outDir, $work | Out-Null
$out = Join-Path $outDir "package-test-$stamp.txt"
try {
    try { Expand-Archive -LiteralPath $zip -DestinationPath $work -Force }
    catch { "el zip no se pudo abrir: $($_.Exception.Message)"; exit 2 }
    $pkg = @(Get-ChildItem $work -Directory)
    if ($pkg.Count -ne 1) { "el zip debe traer una sola carpeta, trae $($pkg.Count)"; exit 2 }
    $ErrorActionPreference = 'Continue'
    $lines = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -PackageDir $pkg[0].FullName 2>&1 | Out-String -Stream)
    $code = $LASTEXITCODE
    $lines | Set-Content -LiteralPath $out -Encoding UTF8
    $lines
    "salida=$out"
    "exit=$code"
    exit $(if ($code -eq 0) { 0 } else { 1 })
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
