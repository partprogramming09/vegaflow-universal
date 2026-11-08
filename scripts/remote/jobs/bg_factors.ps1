<#
.SYNOPSIS
    Isolates which background-isolation action protects the game: with the daemon off, applies each action by hand to the background load.
.DESCRIPTION
    With drawstress open on the desktop and the daemon off, starts a background load (windowless Chrome
    burning CPU) and measures the game with PresentMon under five conditions, rotated each round:
      base : load left untouched
      aff  : load limited to 2 logical threads (equivalent to the daemon's background CPU Sets)
      eco  : load in efficiency mode (EcoQoS, PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
      io   : load with very low I/O priority
      all  : the three actions together
    With -Mode threads it instead measures how many threads to give the background: base (no limit), aff2, aff4, aff6 and aff8
    (load limited to the last 2, 4, 6 or 8 logical threads; threads come in pairs per core with SMT).
    Prints one row per capture: label, average FPS, 1% Low, 0.1% Low, frametime deviation, processes
    touched and CPU seconds the background load obtained during the capture (bg_cpu_s).
.PARAMETER Reps
    Rounds; each round measures the five conditions.
.PARAMETER Seconds
    Duration of each PresentMon capture.
.PARAMETER Workers
    Threads the background load burns (1 to 12).
.PARAMETER Topo
    With -Mode sim simulates a machine with fewer processors: the game and the load are limited to 4 threads (4c: one thread on each of 4 cores;
    4t2c: 2 cores with SMT; 6c: 6 threads, one per core; 4c8t: 4 cores with SMT). Conditions: base (load on the 4 threads), c1 (load on 1 thread) and c2 (load on 2 threads).
.PARAMETER Kind
    cpu burns ALU; mem copies large buffers to saturate memory bandwidth.
    With -Mode narrow, 1 and 2 logical threads for the background are compared on the full machine.
.PARAMETER Api
    API of the game that is opened if it is not open (d3d11 or d3d12); also names the captures.
.PARAMETER Diag
    1 adds two rows per capture: diag (priority, affinity, power saving and CPU Sets of the game, PresentMon and Chrome) and cpu (average usage of each logical thread during the capture).
.PARAMETER Duty
    Percentage of every 100 ms that each load thread burns CPU (5 to 100); sets the total load at Workers x Duty / 100 logical processors.
    With -Mode confine only base and aff2 are measured (to see from what load confining is worthwhile).
#>
param(
    [ValidateRange(1, 6)][int]$Reps = 3,
    [ValidateRange(10, 120)][int]$Seconds = 30,
    [ValidateRange(1, 12)][int]$Workers = 6,
    [ValidateSet('d3d11', 'd3d12')][string]$Api = 'd3d11',
    [ValidateRange(5, 100)][int]$Duty = 100,
    [ValidateSet('factors', 'threads', 'confine', 'sim', 'narrow')][string]$Mode = 'factors',
    [ValidateSet('cpu', 'mem')][string]$Kind = 'cpu',
    [ValidateSet('4c', '4t2c', '6c', '4c8t')][string]$Topo = '4c',
    [ValidateSet('0', '1')][string]$Diag = '0'
)

$ErrorActionPreference = 'Stop'
$root = 'C:\ProgramData\VegaFlow'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$resultsDir = Join-Path (Split-Path $PSScriptRoot) 'bench\results'

Add-Type -Namespace VF -Name Bg -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetProcessInformation(IntPtr h, int cls, uint[] info, int size);
[DllImport("ntdll.dll")] public static extern int NtSetInformationProcess(IntPtr h, int cls, ref int info, int size);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetProcessInformation(IntPtr h, int cls, uint[] info, int size);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetProcessDefaultCpuSets(IntPtr h, uint[] ids, uint count, out uint required);
'@

# Summarizes priority, affinity, power saving (EcoQoS) and default CPU Sets of a process, in one string.
function Get-ProcDiag($p) {
    try {
        $eco = [uint32[]](1, 0, 0)
        $ecoText = if ([VF.Bg]::GetProcessInformation($p.Handle, 4, $eco, 12)) { "ctl=$($eco[1])/state=$($eco[2])" } else { 'n/d' }
        $req = [uint32]0
        [void][VF.Bg]::GetProcessDefaultCpuSets($p.Handle, $null, 0, [ref]$req)
        '{0}:{1} prio={2} aff=0x{3:X} eco={4} cpusets={5}' -f $p.ProcessName, $p.Id, $p.PriorityClass, [int64]$p.ProcessorAffinity, $ecoText, $req
    } catch { "$($p.ProcessName):$($p.Id) n/d" }
}

# Returns the Chrome processes started by the background load (identified by their profile folder).
function Get-LoadProcesses {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" | Where-Object { $_.CommandLine -match 'vf-chrome-bgf' } |
        ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
}

# Chrome profile folder and busy.html: the user area, not the runner's TEMP (Chrome does not start with WinBridge's protected TEMP).
$loadTemp = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'

# Starts the background load and waits for Chrome to create its processes.
function Start-Load {
    $page = Join-Path $loadTemp 'vf-busy.html'
    Copy-Item (Join-Path $root 'tools\busy.html') $page -Force
    Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=$(Join-Path $loadTemp 'vf-chrome-bgf')", "file:///$($page -replace '\\', '/')?n=$Workers&duty=$Duty&kind=$Kind" | Out-Null
    Start-Sleep -Seconds 8
}

# Closes only the Chrome processes of the background load.
function Stop-Load { Get-LoadProcesses | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } }

# Applies a condition to all the load's processes and returns how many it touched.
function Set-Condition($cond) {
    $n = 0
    foreach ($p in Get-LoadProcesses) {
        try {
            $mask = if ($Mode -eq 'sim') { @{ base = $simMasks.all; c1 = $simMasks.c1; c2 = $simMasks.c2 }[$cond] } else { @{ aff = 0xC00; all = 0xC00; aff1 = 0x800; aff2 = 0xC00; aff4 = 0xF00; aff6 = 0xFC0; aff8 = 0xFF0 }[$cond] }
            if ($mask) { $p.ProcessorAffinity = [IntPtr]$mask }
            if ($cond -in 'eco', 'all') { [void][VF.Bg]::SetProcessInformation($p.Handle, 4, [uint32[]](1, 1, 1), 12) }
            if ($cond -in 'io', 'all') { $v = 0; [void][VF.Bg]::NtSetInformationProcess($p.Handle, 33, [ref]$v, 4) }
            $n++
        } catch { }
    }
    return $n
}

if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { throw 'El daemon esta corriendo; esta prueba es con el daemon apagado.' }
if (-not (Get-Process drawstress -ErrorAction SilentlyContinue)) { & (Join-Path $PSScriptRoot 'game.ps1') -Api $Api | Out-Null }

$simMasks = @{ '4c' = @{ all = 0x55; c1 = 0x40; c2 = 0x50 }; '4t2c' = @{ all = 0x0F; c1 = 0x08; c2 = 0x0C }; '6c' = @{ all = 0x555; c1 = 0x400; c2 = 0x500 }; '4c8t' = @{ all = 0xFF; c1 = 0x80; c2 = 0xC0 } }[$Topo]
if ($Mode -eq 'sim') { (Get-Process drawstress | Select-Object -First 1).ProcessorAffinity = [IntPtr]$simMasks.all }
$names = switch ($Mode) { 'threads' { @('base', 'aff2', 'aff4', 'aff6', 'aff8') } 'confine' { @('base', 'aff2') } 'sim' { @('base', 'c1', 'c2') } 'narrow' { @('aff1', 'aff2') } default { @('base', 'aff', 'eco', 'io', 'all') } }
$prefix = switch ($Mode) { 'threads' { 'bgt' } 'confine' { 'bgc' } 'sim' { "bgs$Topo" } 'narrow' { 'bgn' } default { 'bgf' } }
$dutyTag = "$(if ($Duty -lt 100) { "-d$Duty" })$(if ($Kind -eq 'mem') { '-mem' })"

# Sums the CPU seconds of the background-load processes whose id is passed in.
function Get-LoadCpu($ids) {
    $sum = 0.0
    foreach ($id in $ids) { $p = Get-Process -Id $id -ErrorAction SilentlyContinue; if ($p) { $sum += $p.TotalProcessorTime.TotalSeconds } }
    return $sum
}

try {
    for ($i = 1; $i -le $Reps; $i++) {
        $order = for ($k = 0; $k -lt $names.Count; $k++) { $names[($k + $i - 1) % $names.Count] }
        foreach ($c in $order) {
            Start-Load
            $touched = Set-Condition $c
            Start-Sleep -Seconds 2
            $ids = @(Get-LoadProcesses | ForEach-Object { $_.Id })
            $cpu0 = Get-LoadCpu $ids
            $label = "$prefix-$c-$Api-w$Workers$dutyTag-$i"
            $sampler = $null
            if ($Diag -eq '1') {
                $chromeKinds = @(Get-LoadProcesses | ForEach-Object { '{0}/0x{1:X}' -f $_.PriorityClass, [int64]$_.ProcessorAffinity } | Sort-Object -Unique) -join ';'
                $sampler = Start-Job {
                    Start-Sleep -Seconds 12
                    $pm = Get-Process -Name 'PresentMon*' -ErrorAction SilentlyContinue | Select-Object -First 1
                    $pmText = if ($pm) { '{0}:{1} prio={2} aff=0x{3:X}' -f $pm.ProcessName, $pm.Id, $pm.PriorityClass, [int64]$pm.ProcessorAffinity } else { 'sin PresentMon' }
                    [pscustomobject]@{ pm = $pmText }
                    $acc = @{}
                    $n = 0
                    for ($k = 0; $k -lt 4; $k++) {
                        Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name<>'_Total'" | ForEach-Object { $acc[[int]$_.Name] = [double]$acc[[int]$_.Name] + $_.PercentProcessorTime }
                        $n++
                        Start-Sleep -Seconds 3
                    }
                    [pscustomobject]@{ cpu = (($acc.Keys | Sort-Object | ForEach-Object { '{0}={1:F0}' -f $_, ($acc[$_] / $n) }) -join ' ') }
                }
                $game = Get-ProcDiag (Get-Process drawstress | Select-Object -First 1)
            }
            & $bench -Process 'drawstress.exe' -Label $label -Seconds $Seconds -WarmupSeconds 5 | Out-Null
            $bgCpu = [math]::Round((Get-LoadCpu $ids) - $cpu0, 1)
            if ($sampler) {
                Wait-Job $sampler -Timeout 20 | Out-Null
                $got = @(Receive-Job $sampler -ErrorAction SilentlyContinue)
                Remove-Job $sampler -Force -ErrorAction SilentlyContinue
                $pmDiag = ($got | Where-Object { $_.pm } | Select-Object -First 1).pm
                $cpuDiag = ($got | Where-Object { $_.cpu } | Select-Object -First 1).cpu
                "diag,$label,game=[$game],presentmon=[$pmDiag],chrome=[$chromeKinds]"
                "cpu,$label,$cpuDiag"
            }
            Stop-Load
            $r = Get-Content (Get-ChildItem (Join-Path $resultsDir "$label-*.json") | Select-Object -Last 1).FullName -Raw | ConvertFrom-Json
            '{0},{1},{2},{3},{4},procesos={5},bg_cpu_s={6}' -f $label, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $touched, $bgCpu
        }
    }
} finally {
    Stop-Load
    Get-Process drawstress -ErrorAction SilentlyContinue | Stop-Process -Force
}
