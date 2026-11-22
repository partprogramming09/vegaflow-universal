<#
.SYNOPSIS
    Opens Octopath Traveler II, loads the autosave and measures it with no one at the laptop, one block per game launch.
.DESCRIPTION
    For each condition in -Conditions (one launch each): closes the game, writes that condition's Engine.ini, opens the game
    through Steam (steam://rungameid via explorer, so it runs as the desktop user and not elevated), waits for the title,
    loads the autosave with Enter (waits of 4, 4, 4 and 50 s, the sequence that worked 4 of 4 times on 2026-11-20), checks
    that the scene loaded and measures with bench.ps1: an optional warm-up capture and -Captures captures of -Seconds.
    Before any key: screen not locked (no LogonUI) and the game window in the foreground. Scene loaded = dda screenshot not
    black, game in the foreground and a 15 s PresentMon check between 15 and 55 FPS (title and menus run at the 60 cap).
    Anything else: saves the screenshot (<tag>-fail.png), closes the game, puts Engine.ini back and fails. It never improvises.
    Conditions (groups of the Unreal preset in src/optimize/steam/engine_presets.cpp, with this laptop's values):
      factory   no [SystemSettings] block (the Engine.ini the job found)
      streaming r.ShaderPipelineCache.Enabled, the 7 r.Streaming.* and r.OneFrameThreadLag (lines 144 and 164-171)
      visual    the 18 quality cuts, r.SceneColorFringeQuality .. r.MaxAnisotropy (lines 172-189)
      async     r.RDG.AsyncCompute and D3D12.AsyncCompute (lines 190-191)
      full      all 29 lines, as the GUI wrote them on 2026-11-15 (the batch that passed the regression gate)
      keep      leaves Engine.ini as it is
    The Engine.ini the job found is saved to C:\ProgramData\VegaFlow\octo\Engine.orig.ini and put back by octo_close.ps1
    at the end, or by the octo-close job when -Leave 1 keeps the game open for another job (ab).
    Labels: <Prefix>-<cond>-bNN[cK] counted captures; wu-<Prefix>-... warm-up and chk-<Prefix>-... load check (not counted).
    Screenshots: C:\ProgramData\VegaFlow\screens\<Prefix>-bNN-title.png, -loaded.png and -fail.png.
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^(factory|streaming|visual|async|full|keep)(,(factory|streaming|visual|async|full|keep)){0,19}$')][string]$Conditions,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9]{1,20}$')][string]$Prefix,
    [ValidateRange(0, 5)][int]$Captures = 1,
    [ValidateRange(10, 300)][int]$Seconds = 120,
    [ValidateSet('0', '1')][string]$Warmup = '1',
    [ValidateSet('0', '1')][string]$Leave = '0'
)

$ErrorActionPreference = 'Stop'
$exe = 'Octopath_Traveler2-Win64-Shipping'
$ini = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Octopath_Traveler2\Saved\Config\WindowsNoEditor\Engine.ini'
$snap = 'C:\ProgramData\VegaFlow\octo\Engine.orig.ini'
$bench = Join-Path $PSScriptRoot 'bench.ps1'
$results = Join-Path (Split-Path $PSScriptRoot) 'bench\results'
$dll = 'C:\Program Files (x86)\Steam\steamapps\common\Octopath_Traveler2\Octopath_Traveler2\Binaries\Win64\d3d11.dll'

$groups = [ordered]@{
    streaming = @('r.ShaderPipelineCache.Enabled=1', 'r.Streaming.PoolSize=4096', 'r.Streaming.LimitPoolSizeToVRAM=0', 'r.Streaming.FullyLoadUsedTextures=1',
        'r.Streaming.FramesForFullUpdate=1', 'r.Streaming.MaxNumTexturesToStreamPerFrame=120', 'r.Streaming.AmortizeCPUToGPUCopy=0', 'r.Streaming.Boost=1.5',
        'r.OneFrameThreadLag=1')
    visual    = @('r.SceneColorFringeQuality=0', 'r.Tonemapper.GrainQuantization=0', 'r.MotionBlurQuality=0', 'r.DepthOfFieldQuality=0', 'r.BloomQuality=1',
        'r.Shadow.FilterMethod=0', 'r.Shadow.MaxResolution=1024', 'r.Shadow.MaxCSMResolution=1024', 'r.Shadow.CSM.MaxCascades=2', 'r.Shadow.RadiusThreshold=0.05',
        'r.Shadow.DistanceScale=0.6', 'r.TranslucencyLightingVolumeDim=24', 'r.VolumetricFog=0', 'r.SSR.Quality=0', 'r.AmbientOcclusionLevels=0', 'r.AOQuality=0',
        'r.DefaultFeature.AntiAliasing=1', 'r.MaxAnisotropy=4')
    async     = @('r.RDG.AsyncCompute=0', 'D3D12.AsyncCompute=0')
}
$allLines = @($groups.Values | ForEach-Object { $_ })
if ($allLines.Count -ne 29) { throw "El preset debe tener 29 lineas y tiene $($allLines.Count)." }

Add-Type -Namespace VF -Name Octo -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
'@

$script:tag = "$Prefix-b00"
function Get-FgPid { $p = [uint32]0; [void][VF.Octo]::GetWindowThreadProcessId([VF.Octo]::GetForegroundWindow(), [ref]$p); [int]$p }

# Saves a dda screenshot and returns its summary line; ok means Desktop Duplication saw a non-black screen.
function Save-Screen($name) {
    $o = @(& (Join-Path $PSScriptRoot 'screenshot.ps1') -Name $name 2>&1 | ForEach-Object { "$_" })
    $m = $o | Where-Object { $_ -like 'method=*' } | Select-Object -First 1
    $br = $o | Where-Object { $_ -like 'brightness=*' } | Select-Object -First 1
    $bk = $o | Where-Object { $_ -like 'black=*' } | Select-Object -First 1
    [pscustomobject]@{ ok = ($m -eq 'method=dda' -and $bk -eq 'black=False'); text = "screen=$name.png $m $br $bk" }
}

# Unexpected state: keep the evidence and stop the whole job (the finally block closes the game and restores Engine.ini).
function Stop-Block($why) {
    $s = Save-Screen "$script:tag-fail"
    throw "$why ($($s.text))"
}

function Test-Locked { [bool](Get-Process LogonUI -ErrorAction SilentlyContinue) }

function Close-Game {
    Get-Process $exe -ErrorAction SilentlyContinue | Stop-Process -Force
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Process $exe -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (Get-Process $exe -ErrorAction SilentlyContinue) { throw 'El juego sigue abierto tras Stop-Process.' }
}

# Writes the condition's Engine.ini from the saved original; returns how many preset lines it carries.
function Set-EngineIni($cond) {
    if ($cond -eq 'keep') { return -1 }
    $lines = switch ($cond) { 'factory' { @() } 'full' { $allLines } default { $groups[$cond] } }
    $text = $script:orig
    if ($lines.Count) { $text = $script:orig.TrimEnd("`r", "`n") + "`r`n`r`n[SystemSettings]`r`n" + ($lines -join "`r`n") + "`r`n" }
    [IO.File]::WriteAllText($ini, $text, (New-Object Text.UTF8Encoding $false))
    $lines.Count
}

# Counts the preset lines present in Engine.ini (to see whether the game kept them when it rewrote the file at start).
function Get-IniPresetCount { @(Get-Content -LiteralPath $ini | Where-Object { $allLines -contains $_.Trim() }).Count }

function Start-Game {
    Start-Process explorer.exe 'steam://rungameid/1971650' | Out-Null
    $deadline = (Get-Date).AddSeconds(120)
    do {
        Start-Sleep -Seconds 2
        $g = Get-Process $exe -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    } until ($g -or (Get-Date) -gt $deadline)
    if (-not $g) { Stop-Block 'el juego no abrio su ventana en 120 s (Steam pide iniciar sesion, actualiza o muestra un aviso)' }
    $ppid = (Get-CimInstance Win32_Process -Filter "ProcessId=$($g.Id)").ParentProcessId
    $parent = (Get-Process -Id $ppid -ErrorAction SilentlyContinue).ProcessName
    if ($parent -ne 'steam') { Stop-Block "el juego no lo lanzo Steam (padre: '$parent')" }
    $g
}

function Send-Enter($game) {
    if (Test-Locked) { Stop-Block 'pantalla bloqueada (LogonUI)' }
    if ((Get-FgPid) -ne $game.Id) { $null = (New-Object -ComObject WScript.Shell).AppActivate($game.Id); Start-Sleep -Milliseconds 500 }
    if ((Get-FgPid) -ne $game.Id) { Stop-Block "la ventana en primer plano no es el juego (pid $(Get-FgPid))" }
    [VF.Octo]::keybd_event(0x0D, 0x1C, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [VF.Octo]::keybd_event(0x0D, 0x1C, 2, [UIntPtr]::Zero)
}

# Runs bench.ps1 and returns its JSON result.
function Measure-Bench($label, $secs) {
    & $bench -Process "$exe.exe" -Label $label -Seconds $secs -WarmupSeconds 5 | Out-Null
    $f = Get-ChildItem $results -Filter "$label-2*.json" | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $f) { throw "bench.ps1 no dejo resultado para $label." }
    Get-Content $f.FullName -Raw | ConvertFrom-Json
}

if (Get-Process vegaflow_daemon -ErrorAction SilentlyContinue) { throw 'El daemon esta corriendo: este job mide con el daemon apagado.' }
if (Test-Locked) { throw 'Pantalla bloqueada (LogonUI): inicia sesion en el laptop.' }
"inicio condiciones=$Conditions prefijo=$Prefix capturas=$Captures segundos=$Seconds warmup=$Warmup scskiller=$([bool](Get-Process SCSKiller -ErrorAction SilentlyContinue)) d3d11_dll=$(Test-Path -LiteralPath $dll)"

$ok = $false
$counted = [System.Collections.Generic.List[string]]::new()
try {
    if (-not (Test-Path -LiteralPath $snap)) {
        Close-Game
        New-Item -ItemType Directory -Force -Path (Split-Path $snap) | Out-Null
        Copy-Item -LiteralPath $ini -Destination $snap
    }
    $script:orig = [IO.File]::ReadAllText($snap)
    if ($script:orig -match '(?m)^\[SystemSettings\]') { throw 'El Engine.ini guardado ya tiene [SystemSettings]: no se sabe cual es el de fabrica.' }
    "engine_ini_original sha256=$((Get-FileHash -LiteralPath $snap).Hash)"

    $i = 0
    foreach ($cond in $Conditions -split ',') {
        $i++
        $script:tag = '{0}-b{1:D2}' -f $Prefix, $i
        Close-Game
        Start-Sleep -Seconds 10
        $written = Set-EngineIni $cond
        $game = Start-Game
        Start-Sleep -Seconds 45
        $title = Save-Screen "$script:tag-title"
        if (-not $title.ok) { Stop-Block "titulo: la captura no es dda o sale negra" }
        foreach ($wait in 4, 4, 4, 50) { Send-Enter $game; Start-Sleep -Seconds $wait }
        $loaded = Save-Screen "$script:tag-loaded"
        if (-not $loaded.ok) { Stop-Block 'tras cargar: la captura no es dda o sale negra' }
        if ((Get-FgPid) -ne $game.Id) { Stop-Block "tras cargar: la ventana en primer plano no es el juego (pid $(Get-FgPid))" }
        $chk = Measure-Bench "chk-$script:tag-$cond" 20
        if ($chk.avg_fps -lt 15 -or $chk.avg_fps -gt 55) { Stop-Block "tras cargar: $($chk.avg_fps) FPS no es la escena (15-55; titulo y menus van a 60)" }
        "bloque=$i cond=$cond pid=$($game.Id) lineas_escritas=$written lineas_tras_arranque=$(Get-IniPresetCount) check_fps=$($chk.avg_fps) modo=$($chk.present_mode) | $($title.text) | $($loaded.text)"
        if ($Warmup -eq '1') { $null = Measure-Bench "wu-$script:tag-$cond" ($Seconds + 5) }
        for ($k = 1; $k -le $Captures; $k++) {
            $label = if ($Captures -eq 1) { '{0}-{1}-b{2:D2}' -f $Prefix, $cond, $i } else { '{0}-{1}-b{2:D2}c{3}' -f $Prefix, $cond, $i, $k }
            $r = Measure-Bench $label ($Seconds + 5)
            $counted.Add(('{0},{1},{2},{3},{4},{5},{6}' -f $r.label, $r.frames, $r.avg_fps, $r.fps_1pct_low, $r.fps_0_1pct_low, $r.frametime_stddev_ms, $r.present_mode))
        }
    }
    $ok = $true
} catch {
    "FALLO: $($_.Exception.Message)"
} finally {
    if (-not ($ok -and $Leave -eq '1')) { & (Join-Path $PSScriptRoot 'octo_close.ps1') }
}
$counted
if (-not $ok) { exit 1 }
'fin'
