<#
.SYNOPSIS
    Saves a screenshot of the primary screen (the desktop session) as a PNG and reports whether it came out black.
.DESCRIPTION
    Lets the Mac see what the laptop shows, e.g. to find a menu button in a game before pressing it.
    Method dda (default when the tool is installed, see setup_capture.ps1): Desktop Duplication through dxcam. It sees
    games presented with a hardware flip. Method gdi (fallback): CopyFromScreen, which reads black on those games;
    the "black=" line says so. The "method=" line says which one ran.
    The file goes to C:\ProgramData\VegaFlow\screens\<Name>.png (fetch it with scp).
.PARAMETER Name
    File name without extension (letters, digits, dot, underscore, dash).
#>
param([Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_.-]{1,40}$')][string]$Name)

$ErrorActionPreference = 'Stop'
$dir = 'C:\ProgramData\VegaFlow\screens'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$path = Join-Path $dir "$Name.png"
$tool = 'C:\ProgramData\VegaFlow\tools\capture'
$py = Join-Path $tool 'venv\Scripts\python.exe'
$dda = Join-Path $tool 'capture_dda.py'

$result = $null
$brightness = $null
# Elevated: only use the tool when its venv runs the private Python that setup_capture.ps1 copied (administrators only).
$cfg = Join-Path $tool 'venv\pyvenv.cfg'
$trusted = (Test-Path -LiteralPath $py) -and (Test-Path -LiteralPath $dda) -and (Test-Path -LiteralPath $cfg) -and
    (((Get-Content $cfg | Where-Object { $_ -match '^home\s*=' }) -split '=', 2)[1].Trim().StartsWith($tool, [StringComparison]::OrdinalIgnoreCase))
if ($trusted) {
    Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue
    # Python writes warnings to stderr; with 'Stop' that would abort the job instead of falling back to GDI.
    $ErrorActionPreference = 'Continue'
    $out = & $py -I $dda $path 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $b = @($out) | Where-Object { $_ -like 'brightness=*' } | Select-Object -First 1
    if ($code -eq 0 -and $b -and (Test-Path -LiteralPath $path)) {
        $result = @('method=dda') + @($out)
        $brightness = [double]($b -replace 'brightness=', '')
    } else { "dda_failed=exit $code; " + ((@($out) | Select-Object -Last 3) -join ' | ') }
} else { 'dda_unavailable=run setup_capture.ps1 (or its Python is not under tools\capture)' }

if (-not $result) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class VfDpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }'
    [void][VfDpi]::SetProcessDPIAware()
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    # Average brightness over a coarse grid: near zero means the capture did not see the game.
    $sum = 0; $count = 0
    for ($y = 0; $y -lt $bitmap.Height; $y += 40) {
        for ($x = 0; $x -lt $bitmap.Width; $x += 40) {
            $c = $bitmap.GetPixel($x, $y)
            $sum += ($c.R + $c.G + $c.B) / 3; $count++
        }
    }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    $brightness = [math]::Round($sum / $count, 1)
    $result = @('method=gdi', "size=$($bounds.Width)x$($bounds.Height)", ('brightness=' + $brightness.ToString([Globalization.CultureInfo]::InvariantCulture)))
}

"path=$path"
$result
"black=$($brightness -lt 3)"
