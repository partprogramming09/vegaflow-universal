$ErrorActionPreference = 'Stop'
$root = 'C:\ProgramData\VegaFlow'
$loadTemp = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'

Add-Type -Namespace VF -Name BgOcto -MemberDefinition @"
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetProcessInformation(IntPtr h, int cls, uint[] info, int size);
[DllImport("ntdll.dll")] public static extern int NtSetInformationProcess(IntPtr h, int cls, ref int info, int size);
"@

function Get-LoadProcesses {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" | Where-Object { $_.CommandLine -match 'vf-chrome-octof' } |
        ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
}

function Start-Load {
    $page = Join-Path $loadTemp 'vf-busy.html'
    Copy-Item (Join-Path $root 'tools\busy.html') $page -Force
    Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=$(Join-Path $loadTemp 'vf-chrome-octof')", "file:///$($page -replace '\\', '/')?n=6&duty=100&kind=cpu" | Out-Null
    Start-Sleep -Seconds 6
}

function Stop-Load {
    Get-LoadProcesses | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
}

function Set-Condition($cond) {
    $n = 0
    foreach ($p in Get-LoadProcesses) {
        try {
            if ($cond -in 'aff', 'all') { $p.ProcessorAffinity = [IntPtr]0xC00 }
            if ($cond -in 'eco', 'all') { [void][VF.BgOcto]::SetProcessInformation($p.Handle, 4, [uint32[]](1, 1, 1), 12) }
            if ($cond -in 'io', 'all') { $v = 0; [void][VF.BgOcto]::NtSetInformationProcess($p.Handle, 33, [ref]$v, 4) }
            $n++
        } catch { }
    }
    return $n
}

try {
    foreach ($c in @('base', 'aff', 'eco', 'io', 'all')) {
        Start-Load
        $touched = Set-Condition $c
        Start-Sleep -Seconds 2
        $label = "octofac-$c"
        $id = "bench_octofac_${c}_" + (Get-Date -Format "HHmmss")
        $req = ('{{"job":"bench","args":{{"Process":"Octopath_Traveler2-Win64-Shipping.exe","Label":"{0}","Seconds":"20","WarmupSeconds":"4"}}}}' -f $label)
        [IO.File]::WriteAllText("C:\ProgramData\WinBridgeJobs\inbox\$id.json", $req, (New-Object System.Text.UTF8Encoding $false))
        $deadline = (Get-Date).AddSeconds(60)
        while ((Get-Date) -lt $deadline) {
            $stFile = "C:\ProgramData\WinBridgeJobs\out\$id.status.json"
            if (Test-Path $stFile) {
                $st = Get-Content $stFile -Raw | ConvertFrom-Json
                if ($st.state -ne 'running') { break }
            }
            Start-Sleep -Seconds 1
        }
        Stop-Load
        $resFile = Get-ChildItem "C:\ProgramData\WinBridgeJobs\bench\results\$label-*.json" | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($resFile) {
            $r = Get-Content $resFile.FullName -Raw | ConvertFrom-Json
            Write-Output ('{0},frames={1},avg={2},low1={3},low01={4},sd={5},touched={6}' -f $c, $r.frames, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $touched)
        } else {
            Write-Output "$c,ERROR_NO_RESULT"
        }
        Start-Sleep -Seconds 2
    }
} finally {
    Stop-Load
}
