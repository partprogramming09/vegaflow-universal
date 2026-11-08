<#
.SYNOPSIS
    Saves a screenshot of the primary screen (the desktop session) as a PNG and reports whether it came out black.
.DESCRIPTION
    Lets the Mac see what the laptop shows, e.g. to find a menu button in a game before pressing it. A game
    presented with Independent Flip may bypass the desktop compositor and read as black; the "black=" line says so.
    The file goes to C:\ProgramData\VegaFlow\screens\<Name>.png (fetch it with scp).
.PARAMETER Name
    File name without extension (letters, digits, dot, underscore, dash).
#>
param([Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_.-]{1,40}$')][string]$Name)

$ErrorActionPreference = 'Stop'
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
$brightness = [math]::Round($sum / $count, 1)

$dir = 'C:\ProgramData\VegaFlow\screens'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$path = Join-Path $dir "$Name.png"
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose(); $bitmap.Dispose()

"path=$path"
"size=$($bounds.Width)x$($bounds.Height)"
"brightness=$brightness"
"black=$($brightness -lt 3)"
