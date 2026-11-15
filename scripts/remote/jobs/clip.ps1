<#
.SYNOPSIS
    Shows the clipboard text only when it is the VegaFlow report ("VegaFlow Universal ..."); otherwise only its length, so the
    person's own clipboard is never read out. Used to check the "Copiar informe" button.
#>
$t = Get-Clipboard -Raw -ErrorAction SilentlyContinue
if ($null -ne $t -and $t.StartsWith('VegaFlow Universal')) { $t } else { 'portapapeles: no es el informe de VegaFlow (longitud {0})' -f ([string]$t).Length }
