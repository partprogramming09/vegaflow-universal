<#
.SYNOPSIS
    Cierra drawstress si esta abierto.
#>
$n = @(Get-Process drawstress -ErrorAction SilentlyContinue).Count
Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
"drawstress cerrados=$n"
