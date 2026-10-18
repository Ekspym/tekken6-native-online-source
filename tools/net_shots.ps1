# A net_x360.ps1 run with screenshots of the game windows (full size) at the given seconds after the start:
#   tools\net_shots.ps1 -Name rk1 -At 75,90,105 [-NetArgs '-Rec menu -Seconds 110 -LagMs 25 ...']  -> runs\<Name>_shots\<sec>_<n>.png
# Environment variables set before the call (T6_RANK_P1, T6_NAME_P1, ...) reach both instances.
param([string]$Name = 'rk1', [int[]]$At = @(75, 90, 105), [string]$NetArgs = '-Rec menu -Seconds 110 -LagMs 25 -Char1 3 -Char2 33 -Stage 5')
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinShot3 { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
try { [WinShot3]::SetProcessDPIAware() | Out-Null } catch {}
. "$PSScriptRoot\t6_env.ps1"
$dir = "$root\runs\${Name}_shots"; New-Item -ItemType Directory -Force $dir | Out-Null
$run = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command `"& '$root\net_x360.ps1' -Name $Name $NetArgs | Out-Null`"" -PassThru -WindowStyle Hidden
$t0 = Get-Date
foreach ($sec in $At) {
    while (((Get-Date) - $t0).TotalSeconds -lt $sec -and -not $run.HasExited) { Start-Sleep -Milliseconds 500 }
    $n = 0
    foreach ($p in Get-Process tekken6 -ErrorAction SilentlyContinue) {
        $n++
        $h = [WinShot3]::Find([uint32]$p.Id); if ($h -eq [IntPtr]::Zero) { continue }
        $r = New-Object WinShot3+RECT; [WinShot3]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hg = $r.B - $r.T; if ($w -lt 50) { continue }
        $bmp = New-Object System.Drawing.Bitmap $w, $hg; $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc(); [WinShot3]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc); $g.Dispose()
        $bmp.Save("$dir\${sec}_$n.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    }
}
$run.WaitForExit()
Get-ChildItem $dir | Select-Object -ExpandProperty Name
