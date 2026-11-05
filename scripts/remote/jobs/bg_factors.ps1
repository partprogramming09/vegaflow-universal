<#
.SYNOPSIS
    Aisla que accion del aislamiento de fondo protege al juego: con el daemon apagado, aplica a mano cada accion a la carga de fondo.
.DESCRIPTION
    Con drawstress abierto en el escritorio y el daemon apagado, arranca una carga de fondo (Chrome sin ventana
    quemando CPU) y mide el juego con PresentMon bajo cinco condiciones, rotadas en cada ronda:
      base : sin tocar la carga
      aff  : carga limitada a 2 hilos logicos (equivale a los CPU Sets de fondo del daemon)
      eco  : carga en modo de eficiencia (EcoQoS, PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
      io   : carga con prioridad de I/O muy baja
      all  : las tres acciones juntas
    Con -Mode threads mide en cambio cuantos hilos dar al fondo: base (sin limite), aff2, aff4, aff6 y aff8
    (carga limitada a los 2, 4, 6 u 8 ultimos hilos logicos; los hilos van de dos en dos por nucleo con SMT).
    Imprime una fila por captura: etiqueta, FPS medio, 1% Low, 0.1% Low, desviacion del frametime, procesos
    tocados y segundos de CPU que consiguio la carga de fondo durante la captura (bg_cpu_s).
.PARAMETER Reps
    Rondas; cada ronda mide las cinco condiciones.
.PARAMETER Seconds
    Duracion de cada captura de PresentMon.
.PARAMETER Workers
    Hilos que quema la carga de fondo (1 a 12).
.PARAMETER Topo
    Con -Mode sim simula un equipo con menos procesadores: el juego y la carga quedan limitados a 4 hilos (4c: un hilo en cada uno de 4 nucleos;
    4t2c: 2 nucleos con SMT; 6c: 6 hilos, uno por nucleo; 4c8t: 4 nucleos con SMT). Condiciones: base (carga en los 4 hilos), c1 (carga en 1 hilo) y c2 (carga en 2 hilos).
.PARAMETER Kind
    cpu quema ALU; mem copia buffers grandes para saturar el ancho de banda de memoria.
    Con -Mode narrow se comparan 1 y 2 hilos logicos para el fondo en el equipo completo.
.PARAMETER Api
    API del juego que se abre si no esta abierto (d3d11 o d3d12); tambien nombra las capturas.
.PARAMETER Diag
    1 anade por captura dos filas: diag (prioridad, afinidad, ahorro de energia y CPU Sets del juego, de PresentMon y de Chrome) y cpu (uso medio de cada hilo logico durante la captura).
.PARAMETER Duty
    Porcentaje de cada 100 ms que cada hilo de la carga quema CPU (5 a 100); fija la carga total en Workers x Duty / 100 procesadores logicos.
    Con -Mode confine solo se miden base y aff2 (para ver desde que carga conviene confinar).
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

# Resume prioridad, afinidad, ahorro de energia (EcoQoS) y CPU Sets por defecto de un proceso, en una cadena.
function Get-ProcDiag($p) {
    try {
        $eco = [uint32[]](1, 0, 0)
        $ecoText = if ([VF.Bg]::GetProcessInformation($p.Handle, 4, $eco, 12)) { "ctl=$($eco[1])/state=$($eco[2])" } else { 'n/d' }
        $req = [uint32]0
        [void][VF.Bg]::GetProcessDefaultCpuSets($p.Handle, $null, 0, [ref]$req)
        '{0}:{1} prio={2} aff=0x{3:X} eco={4} cpusets={5}' -f $p.ProcessName, $p.Id, $p.PriorityClass, [int64]$p.ProcessorAffinity, $ecoText, $req
    } catch { "$($p.ProcessName):$($p.Id) n/d" }
}

# Devuelve los procesos de Chrome que arranco la carga de fondo (los identifica por su carpeta de perfil).
function Get-LoadProcesses {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" | Where-Object { $_.CommandLine -match 'vf-chrome-bgf' } |
        ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
}

# Carpeta del perfil de Chrome y de busy.html: el area del usuario, no el TEMP del ejecutor (con el TEMP protegido de WinBridge Chrome no arranca).
$loadTemp = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'

# Arranca la carga de fondo y espera a que Chrome cree sus procesos.
function Start-Load {
    $page = Join-Path $loadTemp 'vf-busy.html'
    Copy-Item (Join-Path $root 'tools\busy.html') $page -Force
    Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=$(Join-Path $loadTemp 'vf-chrome-bgf')", "file:///$($page -replace '\\', '/')?n=$Workers&duty=$Duty&kind=$Kind" | Out-Null
    Start-Sleep -Seconds 8
}

# Cierra solo los procesos de Chrome de la carga de fondo.
function Stop-Load { Get-LoadProcesses | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } }

# Aplica una condicion a todos los procesos de la carga y devuelve cuantos tocó.
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

# Suma los segundos de CPU de los procesos de la carga de fondo cuyo id se pasa.
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
