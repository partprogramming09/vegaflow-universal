<#
.SYNOPSIS
    Sends keys to the test-build GUI (or to a dialog it opened) and reports the foreground window afterwards.
.DESCRIPTION
    Lets the Mac answer the GUI's dialogs and walk the keyboard focus with Tab. It sends nothing unless the window in
    the foreground belongs to the process of C:\dev\vf-run\build\vegaflow_daemon.exe, so keys can never land in a game
    or in another program the person is using. Tokens are limited by the manifest pattern to Tab, Enter, Escape,
    Space and arrows.
.PARAMETER Keys
    A list of tokens such as {TAB}{TAB}{ENTER} (SendKeys syntax).
#>
param([Parameter(Mandatory = $true)][string]$Keys)

$ErrorActionPreference = 'Stop'
if ($Keys -notmatch '^(\{(TAB|ENTER|ESC|SPACE|LEFT|RIGHT|UP|DOWN)\}|\+\{TAB\}|\^[1-7]|[0-9,])+$') { throw 'Teclas no permitidas.' }
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
$gui = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
if (-not $gui) { throw 'La GUI de prueba no esta abierta.' }

Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class FgWin {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public static string Info(out uint pid) {
        IntPtr h = GetForegroundWindow();
        GetWindowThreadProcessId(h, out pid);
        StringBuilder sb = new StringBuilder(200);
        GetWindowText(h, sb, 200);
        return sb.ToString();
    }
}
'@
$fgPid = [uint32]0
$title = [FgWin]::Info([ref]$fgPid)
if ($fgPid -ne $gui.Id) {
    $null = (New-Object -ComObject WScript.Shell).AppActivate($gui.Id)
    Start-Sleep -Milliseconds 400
    $title = [FgWin]::Info([ref]$fgPid)
}
if ($fgPid -ne $gui.Id) { throw "La ventana en primer plano no es de la GUI (pid $fgPid, '$title'): no se envia nada." }
# SendKeys has no {SPACE} keyword: the space bar is a plain space.
[System.Windows.Forms.SendKeys]::SendWait(($Keys -replace '\{SPACE\}', ' '))
Start-Sleep -Milliseconds 800
$after = [FgWin]::Info([ref]$fgPid)
'enviado a pid={0} ventana="{1}"; ahora primer plano pid={2} ventana="{3}"' -f $gui.Id, $title, $fgPid, $after
