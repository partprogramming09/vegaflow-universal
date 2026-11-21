<#
.SYNOPSIS
    Sets the Windows display scale of the primary monitor (100, 125, 150, 175 or 200 %) in session 1, to check the GUI at
    other scales. The scale it finds is written to C:\ProgramData\VegaFlow\dpi_original.txt the first time, and
    `Percent=original` puts that value back.
.DESCRIPTION
    Uses DisplayConfigGetDeviceInfo / DisplayConfigSetDeviceInfo with the DPI-scale device info types (-3 get, -4 set).
    Those two types are not in the public headers, but they are what the Settings app uses. Only scale steps the monitor
    reports as allowed are accepted. Programs started after the change use the new scale; open ones keep theirs.
#>
param([Parameter(Mandatory)][string]$Percent)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VfDpi {
  [StructLayout(LayoutKind.Sequential)] public struct LUID { public uint Low; public int High; }
  [StructLayout(LayoutKind.Sequential)] public struct HDR { public int Type; public uint Size; public LUID Adapter; public uint Id; }
  [StructLayout(LayoutKind.Sequential)] public struct GETDPI { public HDR H; public int Min; public int Cur; public int Max; }
  [StructLayout(LayoutKind.Sequential)] public struct SETDPI { public HDR H; public int Rel; }
  [StructLayout(LayoutKind.Sequential)] public struct RATIONAL { public uint N; public uint D; }
  [StructLayout(LayoutKind.Sequential)] public struct SRC { public LUID Adapter; public uint Id; public uint ModeIdx; public uint Status; }
  [StructLayout(LayoutKind.Sequential)] public struct TGT { public LUID Adapter; public uint Id; public uint ModeIdx; public int Tech; public int Rot; public int Scal; public RATIONAL Rate; public int Order; public int Avail; public uint Status; }
  [StructLayout(LayoutKind.Sequential)] public struct PATH { public SRC S; public TGT T; public uint Flags; }
  [DllImport("user32.dll")] public static extern int GetDisplayConfigBufferSizes(uint f, out uint p, out uint m);
  [DllImport("user32.dll")] public static extern int QueryDisplayConfig(uint f, ref uint p, [Out] PATH[] paths, ref uint m, IntPtr modes, IntPtr topo);
  [DllImport("user32.dll")] public static extern int DisplayConfigGetDeviceInfo(ref GETDPI i);
  [DllImport("user32.dll")] public static extern int DisplayConfigSetDeviceInfo(ref SETDPI i);
  public static readonly int[] Steps = {100,125,150,175,200,225,250,300,350,400,450,500};
  public static HDR Primary() {
    uint np, nm; if (GetDisplayConfigBufferSizes(2, out np, out nm) != 0) throw new Exception("GetDisplayConfigBufferSizes");
    var paths = new PATH[np]; var modes = Marshal.AllocHGlobal((int)nm * 64);
    if (QueryDisplayConfig(2, ref np, paths, ref nm, modes, IntPtr.Zero) != 0) throw new Exception("QueryDisplayConfig");
    var h = new HDR(); h.Adapter = paths[0].S.Adapter; h.Id = paths[0].S.Id; return h;
  }
  public static int[] Get() { // current %, min %, max %
    var g = new GETDPI(); g.H = Primary(); g.H.Type = -3; g.H.Size = (uint)Marshal.SizeOf(typeof(GETDPI));
    if (DisplayConfigGetDeviceInfo(ref g) != 0) throw new Exception("DisplayConfigGetDeviceInfo");
    int rec = -g.Min; return new[] { Steps[rec + g.Cur], Steps[rec + g.Min], Steps[rec + g.Max] };
  }
  public static void Set(int percent) {
    var g = new GETDPI(); g.H = Primary(); g.H.Type = -3; g.H.Size = (uint)Marshal.SizeOf(typeof(GETDPI));
    if (DisplayConfigGetDeviceInfo(ref g) != 0) throw new Exception("DisplayConfigGetDeviceInfo");
    int rec = -g.Min, want = Array.IndexOf(Steps, percent);
    if (want < 0 || want - rec < g.Min || want - rec > g.Max) throw new Exception("Escala no permitida por este monitor: " + percent);
    var s = new SETDPI(); s.H = g.H; s.H.Type = -4; s.H.Size = (uint)Marshal.SizeOf(typeof(SETDPI)); s.Rel = want - rec;
    if (DisplayConfigSetDeviceInfo(ref s) != 0) throw new Exception("DisplayConfigSetDeviceInfo");
  }
}
'@
$file = 'C:\ProgramData\VegaFlow\dpi_original.txt'
$now = [VfDpi]::Get()
if ($Percent -eq 'original') {
    if (-not (Test-Path $file)) { throw "No hay escala original guardada en $file" }
    $Percent = (Get-Content $file -Raw).Trim()
} elseif (-not (Test-Path $file)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $file) | Out-Null
    Set-Content $file $now[0]
}
[VfDpi]::Set([int]$Percent)
$after = [VfDpi]::Get()
'escala antes={0} % ahora={1} % (permitido {2}..{3} %); original guardada={4} %' -f $now[0], $after[0], $after[1], $after[2], (Get-Content $file -Raw).Trim()
