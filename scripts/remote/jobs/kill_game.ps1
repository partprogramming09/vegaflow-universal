<#
.SYNOPSIS
    Closes drawstress if it is open.
#>
$n = @(Get-Process drawstress -ErrorAction SilentlyContinue).Count
Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
"drawstress cerrados=$n"
