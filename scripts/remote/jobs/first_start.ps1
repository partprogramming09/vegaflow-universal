<#
.SYNOPSIS
    First unattended start: does a daemon with no change journal reach Global\VegaFlowStop in bounded time?
.DESCRIPTION
    The daemon staged by `vfdev first-start` (C:\ProgramData\VegaFlow\firststart\stage, administrators only, checked
    against the SHA-256 the Mac sent) is copied into a fresh folder with no config\change_journal.vfj and started:
      -Mode hidden : Start-Process -WindowStyle Hidden, like the VegaFlow-Start task (ab_profile.ps1 -Start).
      -Mode task   : a temporary scheduled task built like the installer's autostart task (scripts/package/install.ps1:
                     the .exe with no arguments, group Users, RunLevel Highest), started by hand and deleted at the end.
      -Mode wt     : opened in a new Windows Terminal window, as a person running the .exe there.
    It reports whether the daemon's console window is visible, how long the stop event took and the journal's SESSION
    line, then stops the daemon with the event (a clean RestoreAll) or, if the event never appeared, kills it (it is
    still at a prompt, before BeginSession). The real journal in C:\ProgramData\VegaFlow\config is never touched.
    Exit code: 0 the event appeared within the limit, 1 it did not, 2 the staging is missing or wrong (nothing ran).
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExeSha256,
    [ValidateSet('hidden', 'task', 'wt')][string]$Mode = 'hidden',
    [ValidateSet('none', 'once', 'dry-run')][string]$Arg = 'none',
    [ValidateSet('console', 'file', 'pipe')][string]$Stdin = 'console',
    [ValidateSet('none', 'unclean', 'corrupt')][string]$Journal = 'none',
    [ValidateSet('none', 'n')][string]$Answer = 'none',
    [ValidateRange(5, 120)][int]$LimitSeconds = 45
)
# Answer n (Mode=task only): types "n" + Enter into the daemon's visible console, as a person would. Never "s": that
# would create a real restore point on the test laptop.
# --once and --dry-run end by themselves (no stop event): for them success is a clean exit within the limit.
# Stdin file/pipe carries "s": if the daemon read it, it would revert or create a restore point; it must not.
# Journal unclean: a previous run that did not end cleanly (the revert question); corrupt: an unreadable journal.
$ErrorActionPreference = 'Stop'
$root = 'C:\ProgramData\VegaFlow\firststart'
$staged = Join-Path $root 'stage\vegaflow_daemon.exe'
$taskName = 'VegaFlow-FirstStartTest'
if (-not (Test-Path -LiteralPath $staged)) { "falta $staged`: corre ``vfdev first-start``"; exit 2 }
$h = (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash
if ($h -ne $ExeSha256) { "SHA-256 no coincide: esperado $ExeSha256, hay $h"; exit 2 }
if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { 'ya corre un vegaflow_daemon: no se prueba'; exit 2 }
$ev = $null
if ([Threading.EventWaitHandle]::TryOpenExisting('Global\VegaFlowStop', [ref]$ev)) { $ev.Dispose(); 'Global\VegaFlowStop ya existe: no se prueba'; exit 2 }

Add-Type -Namespace VfFirst -Name Con -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern bool FreeConsole();
[DllImport("kernel32.dll")] public static extern bool AttachConsole(uint pid);
[DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow();
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[StructLayout(LayoutKind.Explicit, CharSet = CharSet.Unicode)] public struct KeyRecord {
    [FieldOffset(0)] public ushort EventType; [FieldOffset(4)] public int KeyDown; [FieldOffset(8)] public ushort Repeat;
    [FieldOffset(10)] public ushort VirtualKey; [FieldOffset(12)] public ushort ScanCode; [FieldOffset(14)] public char Char;
    [FieldOffset(16)] public uint ControlState; }
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern System.IntPtr CreateFileW(string name, uint access, uint share, System.IntPtr sa, uint disposition, uint flags, System.IntPtr template);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool WriteConsoleInputW(System.IntPtr h, KeyRecord[] records, uint count, out uint written);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
'@
# Types text + Enter into the console of process $id (key down and key up per character).
function Send-ConsoleKeys([int]$id, [string]$text) {
    [void][VfFirst.Con]::FreeConsole()
    if (-not [VfFirst.Con]::AttachConsole([uint32]$id)) { return 'no se pudo unir a la consola' }
    $h = [VfFirst.Con]::CreateFileW('CONIN$', [uint32]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    $recs = foreach ($c in ($text + "`r").ToCharArray()) {
        foreach ($down in 1, 0) {
            $r = New-Object VfFirst.Con+KeyRecord
            $r.EventType = 1; $r.KeyDown = $down; $r.Repeat = 1; $r.Char = $c
            $r.VirtualKey = if ($c -eq "`r") { 0x0D } else { [uint16][char]::ToUpper($c) }
            $r
        }
    }
    $w = 0
    $ok = [VfFirst.Con]::WriteConsoleInputW($h, [VfFirst.Con+KeyRecord[]]$recs, [uint32]$recs.Count, [ref]$w)
    [void][VfFirst.Con]::CloseHandle($h)
    [void][VfFirst.Con]::FreeConsole()
    return "teclas_escritas=$w ok=$ok"
}
# Attaches to the daemon's console for a moment to read whether its window is visible.
function Get-ConsoleVisible([int]$id) {
    [void][VfFirst.Con]::FreeConsole()
    if (-not [VfFirst.Con]::AttachConsole([uint32]$id)) { return 'sin-consola' }
    $w = [VfFirst.Con]::GetConsoleWindow()
    $v = if ($w -eq [IntPtr]::Zero) { 'sin-ventana' } else { [string][VfFirst.Con]::IsWindowVisible($w) }
    [void][VfFirst.Con]::FreeConsole()
    return $v
}

$work = Join-Path $root ("run-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path (Join-Path $work 'config') | Out-Null
$exe = Join-Path $work 'vegaflow_daemon.exe'
Copy-Item -LiteralPath $staged $exe
$journalPath = Join-Path $work 'config\change_journal.vfj'
if ($Journal -eq 'unclean') {
    # The header FileChangeJournal writes (change_journal.cpp PersistLocked), with a session left open.
    Set-Content -LiteralPath $journalPath -Value "VFJOURNAL`t1`nSESSION`tactive=1`tunclean_streak=1`trestore_point_prompted=0" -NoNewline -Encoding ASCII
} elseif ($Journal -eq 'corrupt') {
    Set-Content -LiteralPath $journalPath -Value "CORRUPT_HEADER_GARBAGE`nENTRY`tbroken`n" -NoNewline -Encoding ASCII
}
$argList = switch ($Arg) { 'once' { @('--once') } 'dry-run' { @('--dry-run') } default { @() } }
"argumentos=$($argList -join ' ') stdin=$Stdin diario_inicial=$Journal"
$p = $null
$code = 1
try {
    if ($Mode -eq 'hidden' -and $Stdin -eq 'pipe') {
        $si = New-Object Diagnostics.ProcessStartInfo $exe
        $si.WorkingDirectory = $work
        $si.Arguments = $argList -join ' '
        $si.UseShellExecute = $false
        $si.CreateNoWindow = $true
        $si.RedirectStandardInput = $true
        $p = [Diagnostics.Process]::Start($si)
        $p.StandardInput.WriteLine('s')  # left open: a reader would block or act on it
    } elseif ($Mode -eq 'hidden') {
        $sp = @{ FilePath = $exe; WorkingDirectory = $work; WindowStyle = 'Hidden'; PassThru = $true }
        if ($argList) { $sp.ArgumentList = $argList }
        if ($Stdin -eq 'file') {
            $answers = Join-Path $work 'answers.txt'
            Set-Content -LiteralPath $answers -Value "s`r`ns`r`n" -NoNewline -Encoding ASCII
            $sp.RedirectStandardInput = $answers
        }
        $p = Start-Process @sp
        $null = $p.Handle  # keeps a handle so ExitCode is readable after it exits
    } else {
        if ($Arg -ne 'none' -or $Stdin -ne 'console') { "Mode=$Mode`: sin argumentos ni stdin redirigido"; exit 2 }
        if ($Mode -eq 'wt') {
            # A person opening the .exe in Windows Terminal: its ConPTY console may report its window as hidden.
            $wt = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\wt.exe'  # the App Execution Alias; not on the job's PATH
            if (-not (Test-Path -LiteralPath $wt)) { "no hay Windows Terminal en $wt"; exit 2 }
            Start-Process -FilePath $wt -ArgumentList @('-w', 'new', '-d', $work, $exe)
        } else {
            $action = New-ScheduledTaskAction -Execute $exe -WorkingDirectory $work
            $trigger = New-ScheduledTaskTrigger -AtLogOn
            $principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest
            $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
            Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
            Start-ScheduledTask -TaskName $taskName
        }
        $deadline = (Get-Date).AddSeconds(15)
        while (-not $p -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 200
            $p = Get-Process vegaflow_daemon -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
        }
        if (-not $p) { 'la tarea no arranco el daemon'; exit 1 }
        $null = $p.Handle  # keeps a handle so ExitCode is readable after it exits
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Milliseconds 500
    "modo=$Mode pid=$($p.Id) sesion=$($p.SessionId) consola_visible=$(Get-ConsoleVisible $p.Id)"
    if ($Answer -eq 'n') {
        if ($Mode -ne 'task') { 'Answer=n solo con Mode=task (consola visible)'; exit 2 }
        Start-Sleep -Seconds 2  # lets the daemon reach the question first
        "respuesta=n $(Send-ConsoleKeys $p.Id 'n')"
    }
    while ($sw.Elapsed.TotalSeconds -lt $LimitSeconds -and -not $p.HasExited) {
        if ([Threading.EventWaitHandle]::TryOpenExisting('Global\VegaFlowStop', [ref]$ev)) { break }
        Start-Sleep -Milliseconds 200
    }
    $p.Refresh()
    $cpu = '{0:N2}' -f $p.TotalProcessorTime.TotalSeconds
    if ($ev) {
        "evento=Global\VegaFlowStop a_los_s={0:N1} cpu_s=$cpu" -f $sw.Elapsed.TotalSeconds
        [void]$ev.Set()
        if (-not $p.WaitForExit(30000)) { 'el daemon no salio 30 s despues del evento'; $p.Kill() } else { "salida_limpia codigo=$($p.ExitCode)"; $code = 0 }
        $ev.Dispose()
    } elseif ($p.HasExited) {
        "salio_solo a_los_s={0:N1} codigo=$($p.ExitCode) cpu_s=$cpu" -f $sw.Elapsed.TotalSeconds
        if ($Arg -ne 'none' -and $p.ExitCode -eq 0) { $code = 0 }
    } else {
        "evento=ausente tras ${LimitSeconds} s cpu_s=$cpu salio=$($p.HasExited)"
        if (-not $p.HasExited) { $p.Kill(); [void]$p.WaitForExit(5000); 'daemon colgado: matado (no habia empezado sesion)' }
    }
    if (Test-Path -LiteralPath $journalPath) { 'diario: ' + ((Get-Content -LiteralPath $journalPath | Select-String '^SESSION') -replace "`t", ' ') } else { 'diario: no existe' }
} finally {
    if ($Mode -eq 'task') { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue }
    if ($p -and -not $p.HasExited) { $p.Kill() }
    Start-Sleep -Milliseconds 500
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    "tarea_temporal=$(if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) { 'QUEDA' } else { 'borrada' })"
}
exit $code
