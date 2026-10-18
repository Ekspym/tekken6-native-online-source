# One practice instance (T6_PRACTICE=1, the launcher's PRACTICE button) with screenshots at the given seconds:
#   tools\practice_shots.ps1 -Name pr1 -At 40,60,80 [-Char 33] [-Until 1630]  -> runs\<Name>\shots\<sec>.png + tekken6.log
param([string]$Name = 'pr1', [int[]]$At = @(40, 60, 80), [int]$Char = 33, [int]$Until = 1630, [int]$Seconds = 0, [string]$Backend = 'mmjoystick', [string]$PadMap = '', [string]$Fullscreen = 'false', [string]$Rtp = 'rov', [int]$VirtualTb = 1, [string]$Hold = '', [int]$Stage = -1, [int]$X = 0)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinShotP { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
try { [WinShotP]::SetProcessDPIAware() | Out-Null } catch {}
. "$PSScriptRoot\t6_env.ps1"
$run = "$root\runs\$Name"
Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$run\shots" | Out-Null
foreach ($v in 'T6_NET', 'T6_NET_HOLD', 'T6_REC', 'T6_HASH', 'T6_SNAPTEST', 'T6_AUTO_CONFIRM') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
$env:T6_PRACTICE = '1'; $env:T6_PLAY = "$root\tools\menu_vs.bin"; $env:T6_PLAY_UNTIL = "$Until"
$env:T6_FIX_SEED = '1'; if ($VirtualTb) { $env:T6_VIRTUAL_TB = '1' } else { Remove-Item Env:T6_VIRTUAL_TB -ErrorAction SilentlyContinue }; $env:T6_PRACTICE_HOLD = $Hold; if ($Stage -ge 0) { $env:T6_STAGE = "$Stage" } else { Remove-Item Env:T6_STAGE -ErrorAction SilentlyContinue }; $env:T6_CHAR_P1 = "$Char"; $env:T6_CHAR_P2 = "$Char"
$env:REX_GPU_VSYNC = '1'; $env:T6_PAD_MAP = $PadMap
$a = @("--game_data_root=$root\game", "--user_data_root=$root\user2", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
       "--fullscreen=$Fullscreen", '--async_shader_compilation=true', "--render_target_path_d3d12=$Rtp", "--input_backend=$Backend")
$proc = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\build" -PassThru
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    "$root\tools\window_guard.ps1", '-ProcessId', $proc.Id, '-X', $X, '-Y', 0, '-Title', 'tekken' | Out-Null
$t0 = Get-Date
foreach ($sec in $At) {
    while (((Get-Date) - $t0).TotalSeconds -lt $sec -and -not $proc.HasExited) { Start-Sleep -Milliseconds 500 }
    if ($proc.HasExited) { break }
    $h = [WinShotP]::Find([uint32]$proc.Id); if ($h -eq [IntPtr]::Zero) { continue }
    $r = New-Object WinShotP+RECT; [WinShotP]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hg = $r.B - $r.T; if ($w -lt 50) { continue }
    $bmp = New-Object System.Drawing.Bitmap $w, $hg
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [WinShotP]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save("$run\shots\$sec.png"); $bmp.Dispose()
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Select-String -Path "$run\tekken6.log" -Pattern 'T6 practice|replay stopped|player setup|T6 crash' | ForEach-Object { $_.Line.Substring([Math]::Min(31, $_.Line.Length)) }
