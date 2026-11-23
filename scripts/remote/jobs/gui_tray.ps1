<#
.SYNOPSIS
    Drives the test-build GUI like the tray does (hide, minimise, restore, open the tray menu, "Copiar informe") and measures its CPU.
.DESCRIPTION
    Works only on the window of C:\dev\vf-run\build\vegaflow_daemon.exe (found by its class and process), so it can never
    touch another program. Actions:
      hide      SW_HIDE, the same call as the "Minimizar a Bandeja" button
      minimize  SW_MINIMIZE
      restore   WM_COMMAND with the tray's "Restaurar" id (3001)
      menu      the right click on the tray icon (opens the tray menu; close it with gui-keys {ESC})
      report    WM_COMMAND with the tray's "Copiar informe" id (3003); read the result with the clip job
      cpu       CPU time of the GUI process over Seconds (default 600), with the window state at both ends
    It presses no switch of Ajustes and sends no other command.
.PARAMETER Action
    One of the actions above.
.PARAMETER Seconds
    Length of the cpu measurement.
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('hide', 'minimize', 'restore', 'menu', 'report', 'cpu')][string]$Action,
    [ValidateRange(5, 1800)][int]$Seconds = 600
)

$ErrorActionPreference = 'Stop'
$exe = 'C:\dev\vf-run\build\vegaflow_daemon.exe'
$gui = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
if (-not $gui) { throw 'La GUI de prueba no esta abierta.' }

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TrayWin {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    public static IntPtr Find(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            StringBuilder sb = new StringBuilder(100);
            GetClassName(h, sb, 100);
            if (p == pid && sb.ToString() == "VegaFlowUniversalWindow") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$h = [TrayWin]::Find([uint32]$gui.Id)
if ($h -eq [IntPtr]::Zero) { throw 'No se encontro la ventana de VegaFlow.' }
function State { 'visible={0} iconic={1}' -f [TrayWin]::IsWindowVisible($h), [TrayWin]::IsIconic($h) }

switch ($Action) {
    'hide'     { $null = [TrayWin]::ShowWindow($h, 0); Start-Sleep -Milliseconds 500; "hide: $(State)" }
    'minimize' { $null = [TrayWin]::ShowWindow($h, 6); Start-Sleep -Milliseconds 500; "minimize: $(State)" }
    'restore'  { $null = [TrayWin]::PostMessage($h, 0x0111, [IntPtr]3001, [IntPtr]::Zero); Start-Sleep -Milliseconds 500; "restore: $(State)" }
    'menu'     { $null = [TrayWin]::PostMessage($h, 0x0465, [IntPtr]1001, [IntPtr]0x0205); Start-Sleep -Milliseconds 500; 'menu: right click sent to the tray icon' }
    'report'   { $null = [TrayWin]::PostMessage($h, 0x0111, [IntPtr]3003, [IntPtr]::Zero); Start-Sleep -Milliseconds 1500; "report: sent, $(State)" }
    'cpu' {
        $before = $gui.TotalProcessorTime.TotalMilliseconds
        $state0 = State
        $t0 = Get-Date
        Start-Sleep -Seconds $Seconds
        $gui.Refresh()
        $cpu = $gui.TotalProcessorTime.TotalMilliseconds - $before
        $secs = ((Get-Date) - $t0).TotalSeconds
        'cpu: {0:N1} ms over {1:N0} s ({2:N4} % of one core) threads={3} ws_mb={4:N1} window: {5} -> {6}' -f $cpu, $secs, (100 * $cpu / 1000 / $secs), $gui.Threads.Count, ($gui.WorkingSet64 / 1MB), $state0, (State)
    }
}
